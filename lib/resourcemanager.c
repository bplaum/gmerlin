#include <string.h>
#include <unistd.h>

#include <gavl/gavltime.h>
#include <gavl/log.h>
#define LOG_DOMAIN "resourcemanager"

#include <gmerlin/resourcemanager.h>
#include <gmerlin/pluginregistry.h>
#include <gmerlin/backend.h>

/*
 *  Unified manager for system resources (devices, removable media, media sources/sinks etc)
 */

typedef struct
  {
  pthread_t th;
  bg_controllable_t ctrl;

  bg_msg_sink_t * plugin_sink;
  
  gavl_array_t local;
  gavl_array_t remote;

  pthread_mutex_t mutex;

  int num_plugins;

  struct
    {
    bg_plugin_handle_t * h;
    
    } * plugins;
  
  } bg_resourcemanager_t;

static bg_resourcemanager_t * resman = NULL;
static pthread_mutex_t resman_mutex = PTHREAD_MUTEX_INITIALIZER;

static void forward_message(gavl_msg_t * msg);


static int find_resource_idx_by_string(int local, const char * tag, const char * val)
  {
  int i;
  const char * test_val;
  const gavl_dictionary_t * dict;
  gavl_array_t * arr = local ? &resman->local : &resman->remote;

  for(i = 0; i < arr->num_entries; i++)
    {
    
    if((dict = gavl_value_get_dictionary(&arr->entries[i])) &&
       (test_val = gavl_dictionary_get_string(dict, tag)) &&
       !strcmp(test_val, val))
      return i;
    }
  
  return -1;
  }

/*
 *  Create a unique ID of a backend. This is used to detect, that different frontends
 *  belong to the same backend (and favor the ones with the native protocol).
 * 
 *  The unique ID of a backend is: md5 of hostname-pid-type
 *  This assumes, that we never have more than one backend of the same type within the same process.
 */

char * bg_make_backend_id(const char * klass)
  {
  char * str;
  char hostname[HOST_NAME_MAX+1];
  char * hash = malloc(GAVL_MD5_LENGTH);
  
  gethostname(hostname, HOST_NAME_MAX+1);
  
  str = gavl_sprintf("%s-%d-%s", hostname, getpid(), klass);
  gavl_md5_buffer_str(str, strlen(str), hash);
  free(str);
  
  return hash;
  }

static void set_backend_id(gavl_dictionary_t * dict)
  {
  const char * klass;
  
  if(!(klass = gavl_dictionary_get_string(dict, GAVL_META_CLASS)) ||
     !gavl_string_starts_with(klass, "backend"))
    return;
  
  gavl_dictionary_set_string_nocopy(dict, GAVL_META_HASH, bg_make_backend_id(klass));
  }

static int resource_supported(const gavl_dictionary_t * dict)
  {
  int ret = 0;
  char * protocol = NULL;
  const char * klass;

  if(!(klass = gavl_dictionary_get_string(dict, GAVL_META_CLASS)))
    return 0;

  if(!strcmp(klass, GAVL_META_CLASS_BACKEND_RENDERER))
    {
    const char * uri = gavl_dictionary_get_string(dict, GAVL_META_URI);
    
    if(!uri)
      goto fail;
    
    if(!gavl_url_split(uri, &protocol, NULL, NULL, NULL, NULL, NULL))
      goto fail;
    
    if(!bg_plugin_find_by_protocol(protocol, BG_PLUGIN_BACKEND_RENDERER))
      {
      gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "No plugin found for resource %s", uri);
      goto fail;
      }
    ret = 1;
    }
  else if(!strcmp(klass, GAVL_META_CLASS_BACKEND_MDB))
    {
    const char * uri = gavl_dictionary_get_string(dict, GAVL_META_URI);
    
    if(!uri)
      goto fail;
    
    if(!gavl_url_split(uri, &protocol, NULL, NULL, NULL, NULL, NULL))
      goto fail;
    
    if(!bg_plugin_find_by_protocol(protocol, BG_PLUGIN_BACKEND_MDB))
      {
      gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "No plugin found for resource %s", uri);
      goto fail;
      }
    ret = 1;
    }
  else
    {
    ret = 1;
    }

  fail:
  if(protocol)
    free(protocol);
  
  return ret;
  }

static void add(int local, gavl_dictionary_t * dict, const char * id)
  {
  gavl_value_t val;
  gavl_dictionary_t * dict_new;
  gavl_array_t * arr;
  
  if(local)
    arr = &resman->local;
  else
    arr = &resman->remote;

  gavl_value_init(&val);
  dict_new = gavl_value_set_dictionary(&val);
  
  gavl_dictionary_copy(dict_new, dict);
  gavl_dictionary_set_string(dict_new, GAVL_META_ID, id);

  /*
   *  Save the resource in the array *before* sending the messages because
   *  the plugins might want to access it
   */
  
  gavl_array_splice_val_nocopy(arr, -1, 0, &val);
      
  if(!local)
    {
    gavl_msg_t * msg;

    if(resource_supported(dict_new))
      {
      msg = bg_msg_sink_get(resman->ctrl.evt_sink);
      gavl_msg_set_id_ns(msg, GAVL_MSG_RESOURCE_ADDED, GAVL_MSG_NS_GENERIC);
      gavl_dictionary_set_string(&msg->header, GAVL_MSG_CONTEXT_ID, id);
      gavl_msg_set_arg_dictionary(msg, 0, dict_new);
      bg_msg_sink_put(resman->ctrl.evt_sink);
      }
    }
  else
    {
    gavl_msg_t msg;

    gavl_msg_init(&msg);
    set_backend_id(dict_new);
    gavl_msg_set_id_ns(&msg, GAVL_MSG_RESOURCE_ADDED, GAVL_MSG_NS_GENERIC);
    gavl_dictionary_set_string(&msg.header, GAVL_MSG_CONTEXT_ID, id);
    gavl_msg_set_arg_dictionary(&msg, 0, dict_new);
    forward_message(&msg);
    gavl_msg_free(&msg);
    }
  
  }

static void del(int local, int idx)
  {
  gavl_array_t * arr;
  if(local)
    arr = &resman->local;
  else
    arr = &resman->remote;

  if(!local)
    {
    const char * id;
    const gavl_dictionary_t * dict;

    if((dict = gavl_value_get_dictionary(&arr->entries[idx])) &&
       (id = gavl_dictionary_get_string(dict, GAVL_META_ID)) &&
       resource_supported(dict))
      {
      gavl_msg_t * msg;
      msg = bg_msg_sink_get(resman->ctrl.evt_sink);
      gavl_msg_set_id_ns(msg, GAVL_MSG_RESOURCE_DELETED, GAVL_MSG_NS_GENERIC);
      gavl_dictionary_set_string(&msg->header, GAVL_MSG_CONTEXT_ID, id);
      bg_msg_sink_put(resman->ctrl.evt_sink);
      }
    }
  
  gavl_array_splice_val(arr, idx, 1, NULL);
  }

static void forward_message(gavl_msg_t * msg)
  {
  int i;
  bg_controllable_t * ctrl;
  
  for(i = 0; i < resman->num_plugins; i++)
    {
    if(!resman->plugins[i].h ||
       !resman->plugins[i].h->plugin->get_controllable ||
       !(ctrl = resman->plugins[i].h->plugin->get_controllable(resman->plugins[i].h->priv)))
      continue;
    bg_msg_sink_put_copy(ctrl->cmd_sink, msg);
    }
  }


/* Handle message from application space */
static int handle_msg_external(void * data, gavl_msg_t * msg)
  {
  switch(msg->NS)
    {
    case GAVL_MSG_NS_GENERIC:
      switch(msg->ID)
        {
        case GAVL_MSG_RESOURCE_ADDED:
          {
          const char * id;
          gavl_dictionary_t dict;
          
          id = gavl_dictionary_get_string(&msg->header, GAVL_MSG_CONTEXT_ID);
          
          if(find_resource_idx_by_string(1, GAVL_META_ID, id) >= 0)
            return 1;
          gavl_dictionary_init(&dict);
          gavl_msg_get_arg_dictionary_c(msg, 0, &dict);
          add(1, &dict, id);
          gavl_dictionary_free(&dict);
          
          }
          break;
        case GAVL_MSG_RESOURCE_DELETED:
          {
          int idx;
          const char * id;

          id = gavl_dictionary_get_string(&msg->header, GAVL_MSG_CONTEXT_ID);
          
          if((idx = find_resource_idx_by_string(1, GAVL_META_ID, id)) < 0)
            return 1;
          
          forward_message(msg);
          del(1, idx);
          }
          break;
        case GAVL_MSG_QUIT:
          return 0;
          break;
        }
      break;
    }
  return 1;
  }

/* Handle message from detector plugin */
static int handle_msg_plugin(void * data, gavl_msg_t * msg)
  {
  switch(msg->NS)
    {
    case GAVL_MSG_NS_GENERIC:
      switch(msg->ID)
        {
        case GAVL_MSG_RESOURCE_ADDED:
          {
          const char * id;
          const char * hash;
          const char * uri;
          gavl_dictionary_t dict;

          id = gavl_dictionary_get_string(&msg->header, GAVL_MSG_CONTEXT_ID);
          
          /* Ignore already added resources */
          if(find_resource_idx_by_string(0, GAVL_META_ID, id) >= 0)
            return 1;

          gavl_dictionary_init(&dict);
          gavl_msg_get_arg_dictionary(msg, 0, &dict);

          if((uri = gavl_dictionary_get_string(&dict, GAVL_META_URI)) &&
             (find_resource_idx_by_string(1, GAVL_META_URI, uri) >= 0))
            {
            gavl_dictionary_free(&dict);
            return 1;
            }
          /* Check for backends with the same ID */
          if((hash = gavl_dictionary_get_string(&dict, GAVL_META_HASH)))
            {
            int idx;
            const gavl_dictionary_t * test_dict;
            
            if(((idx = find_resource_idx_by_string(0, GAVL_META_HASH, hash)) >= 0) &&
               (test_dict = gavl_value_get_dictionary(&resman->remote.entries[idx])))
              {
              int prio = -1, test_prio = -1;

              if(gavl_dictionary_get_int(&dict, BG_RESOURCE_PRIORITY, &prio) &&
                 gavl_dictionary_get_int(test_dict, BG_RESOURCE_PRIORITY, &test_prio))
                {
                if(prio > test_prio)
                  {
                  gavl_log(GAVL_LOG_DEBUG, LOG_DOMAIN, "Replacing resource %s with %s",
                           gavl_dictionary_get_string(test_dict, GAVL_META_URI),
                           gavl_dictionary_get_string(&dict, GAVL_META_URI));
                  /* Replace entry */
                  del(0, idx);
                  }
                else if(prio < test_prio)
                  {
                  /* Leave entry */
                  gavl_log(GAVL_LOG_DEBUG, LOG_DOMAIN, "Not adding resource %s (%s is already there)",
                           gavl_dictionary_get_string(&dict, GAVL_META_URI),
                           gavl_dictionary_get_string(test_dict, GAVL_META_URI));
                  return 1;
                  }
                
                }
              
              }
            
            }
#if 0
          fprintf(stderr, "Got resource %s:\n", id);
          gavl_dictionary_dump(&dict, 2);
          fprintf(stderr, "\n");
#endif
          add(0, &dict, id);
          gavl_dictionary_free(&dict);
          }
          break;
        case GAVL_MSG_RESOURCE_DELETED:
          {
          const char * id;
          int idx;

          id = gavl_dictionary_get_string(&msg->header, GAVL_MSG_CONTEXT_ID);
          
          if((idx = find_resource_idx_by_string(0, GAVL_META_ID, id)) < 0)
            return 1;

#if 0
          fprintf(stderr, "Deleted resource: %s\n", id);
#endif
          del(0, idx);
          }
          break;
        }
      break;
      
    }
  return 1;
  }

static void * thread_func(void * data)
  {
  int result;
  int i;
  gavl_time_t delay_time = GAVL_TIME_SCALE; // 1000 ms

  /* Delay for 1 sec to give other modules a chance to connect their
     message sinks */
  gavl_time_delay(&delay_time);
  
  delay_time = GAVL_TIME_SCALE / 20; // 50 ms
  
  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Started thread");

  while(1)
    {
    pthread_mutex_lock(&resman->mutex);
    
    result = 0;
    
    /* Handle external messages */
    if(!bg_msg_sink_iteration(resman->ctrl.cmd_sink))
      {
      pthread_mutex_unlock(&resman->mutex);
      break;
      }
    
    result += bg_msg_sink_get_num(resman->ctrl.cmd_sink);
    
    /* Update plugins */
    for(i = 0; i < resman->num_plugins; i++)
      {
      bg_controllable_plugin_t * plugin;

      if(resman->plugins[i].h)
        {
        plugin = (bg_controllable_plugin_t *)resman->plugins[i].h->plugin;

        if(plugin->update)
          result += plugin->update(resman->plugins[i].h->priv);
        
        }
      }

    i = 0;
    /* Remove expired resouces */
    while(i < resman->remote.num_entries)
      {
      gavl_time_t t;
      const gavl_dictionary_t * d;

      if((d = gavl_value_get_dictionary(&resman->remote.entries[i])) &&
         gavl_dictionary_get_long(d, BG_RESOURCE_EXPIRE_TIME, &t) &&
         gavl_time_get_monotonic() > t)
        {
        const char * id;
        gavl_msg_t * msg;
        
        id = gavl_dictionary_get_string(d, GAVL_META_ID);
        
        /* TODO: Remove expired resource */
        gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Removing resource %s (expired)", id);

        msg = bg_msg_sink_get(resman->ctrl.evt_sink);
        gavl_msg_set_id_ns(msg, GAVL_MSG_RESOURCE_DELETED, GAVL_MSG_NS_GENERIC);
        gavl_dictionary_set_string(&msg->header, GAVL_MSG_CONTEXT_ID, id);
        bg_msg_sink_put(resman->ctrl.evt_sink);
        
        gavl_array_splice_val(&resman->remote, i, 1, NULL);
        
        result++;
        }
      else
        i++;
      }

    pthread_mutex_unlock(&resman->mutex);
    
    if(!result) // idle
      gavl_time_delay(&delay_time);
    
    }

  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Finished thread");
  
  return NULL;
  }


static void bg_resourcemanager_init()
  {
  int i;

  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Initializing resource manager");
  
  if(resman)
    return;
  resman = calloc(1, sizeof(*resman));

  bg_controllable_init(&resman->ctrl,
                       bg_msg_sink_create(handle_msg_external, NULL, 0),
                       bg_msg_hub_create(1));

  resman->plugin_sink = bg_msg_sink_create(handle_msg_plugin, NULL, 1);

  /* Load plugins */
  
  resman->num_plugins = bg_get_num_plugins(BG_PLUGIN_RESOURCE_DETECTOR, 0);
  resman->plugins = calloc(resman->num_plugins, sizeof(*resman->plugins));

  for(i = 0; i < resman->num_plugins; i++)
    {
    bg_controllable_t * plugin_ctrl;
    const bg_plugin_info_t * info = bg_plugin_find_by_index(i, BG_PLUGIN_RESOURCE_DETECTOR, 0);
    resman->plugins[i].h = bg_plugin_load(info);

    if(resman->plugins[i].h->plugin->get_controllable &&
       (plugin_ctrl = resman->plugins[i].h->plugin->get_controllable(resman->plugins[i].h->priv)))
      bg_msg_hub_connect_sink(plugin_ctrl->evt_hub, resman->plugin_sink);
    }
  
  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Loaded %d plugins", resman->num_plugins);

  pthread_mutex_init(&resman->mutex, NULL);
  
  pthread_create(&resman->th, NULL, thread_func, NULL);
  
  
  }


bg_controllable_t * bg_resourcemanager_get_controllable()
  {
  pthread_mutex_lock(&resman_mutex);

  if(!resman)
    bg_resourcemanager_init();
  
  pthread_mutex_unlock(&resman_mutex);

  return &resman->ctrl;
  }

void bg_resourcemanager_publish(const char * id, const gavl_dictionary_t * dict)
  {
  gavl_msg_t * msg;
  bg_controllable_t * ctrl = bg_resourcemanager_get_controllable();
  
  msg = bg_msg_sink_get(ctrl->cmd_sink);

  gavl_msg_set_id_ns(msg, GAVL_MSG_RESOURCE_ADDED, GAVL_MSG_NS_GENERIC);
  gavl_dictionary_set_string(&msg->header, GAVL_MSG_CONTEXT_ID, id);
  gavl_msg_set_arg_dictionary(msg, 0, dict);
  bg_msg_sink_put(ctrl->cmd_sink);
  }

void bg_resourcemanager_unpublish(const char * id)
  {
  gavl_msg_t * msg;
  bg_controllable_t * ctrl = bg_resourcemanager_get_controllable();

  msg = bg_msg_sink_get(ctrl->cmd_sink);
  
  gavl_msg_set_id_ns(msg, GAVL_MSG_RESOURCE_DELETED, GAVL_MSG_NS_GENERIC);
  gavl_dictionary_set_string(&msg->header, GAVL_MSG_CONTEXT_ID, id);
  
  bg_msg_sink_put(ctrl->cmd_sink);
  }

gavl_dictionary_t * bg_resource_get_by_id(int local, const char * id)
  {
  int idx;
  gavl_array_t * arr;

  if(!resman)
    return NULL;

  if(local)
    arr = &resman->local;
  else
    arr = &resman->remote;

  if((idx = find_resource_idx_by_string(local, GAVL_META_ID, id)) >= 0)
    return gavl_value_get_dictionary_nc(&arr->entries[idx]);
  else
    return NULL;
  }

gavl_dictionary_t * bg_resource_get_by_idx(int local, int idx)
  {
  gavl_array_t * arr;

  if(!resman)
    return NULL;
  
  if(local)
    arr = &resman->local;
  else
    arr = &resman->remote;

  if((idx < 0) || (idx >= arr->num_entries))
    return NULL;
  
  return gavl_value_get_dictionary_nc(&arr->entries[idx]);
  }

int bg_resource_idx_for_label(const gavl_array_t * arr, const char * label, int off)
  {
  int i;
  const char * test_label;
  const gavl_dictionary_t * test_dict;
  
  if(!(arr->num_entries - off))
    return off;

  for(i = off; i < arr->num_entries; i++)
    {
    if(!(test_dict = gavl_value_get_dictionary(&arr->entries[i])) ||
       !(test_label = gavl_dictionary_get_string(test_dict, GAVL_META_LABEL)))
      continue;

    if(strcoll(label, test_label) < 0)
      return i;
    
    }
  return arr->num_entries;
  }

static int compare_func(const void * val1, const void * val2, void * priv)
  {
  const gavl_dictionary_t * dict1;
  const gavl_dictionary_t * dict2;

  const char * str1;
  const char * str2;

  if(!(dict1 = gavl_value_get_dictionary(val1)) ||
     !(dict2 = gavl_value_get_dictionary(val2)))
    return 0;

  if(!(str1 = gavl_dictionary_get_string(dict1, GAVL_META_LABEL)) ||
     !(str2 = gavl_dictionary_get_string(dict2, GAVL_META_LABEL)))
    return 0;

  if(!strcmp(str1, str2))
    {

    if(!(str1 = gavl_dictionary_get_string(dict1, GAVL_META_URI)) ||
       !(str2 = gavl_dictionary_get_string(dict2, GAVL_META_URI)))
      return 0;
    }
  
  return strcmp(str1, str2);
  }

void bg_resource_get_by_class(const char * klass, int full_match, gavl_time_t timeout, gavl_array_t * arr)
  {
  int i;
  const gavl_dictionary_t  * test_dict;
  const char  * test_klass;

  if(!resman)
    bg_resourcemanager_init();
  
  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Scanning for resources for %.2f seconds please wait",
           gavl_time_to_seconds(timeout));
  gavl_time_delay(&timeout);
  
  pthread_mutex_lock(&resman->mutex);

  for(i = 0; i < resman->remote.num_entries; i++)
    {
    if((test_dict = gavl_value_get_dictionary(&resman->remote.entries[i])) &&
       (test_klass = gavl_dictionary_get_string(test_dict, GAVL_META_CLASS)))
      {
      if(full_match)
        {
        if(!strcmp(test_klass, klass))
          gavl_array_splice_val(arr, -1, 0, &resman->remote.entries[i]);
        }
      else
        {
        if(gavl_string_starts_with(test_klass, klass))
          gavl_array_splice_val(arr, -1, 0, &resman->remote.entries[i]);
        }
      }
    }
  
  pthread_mutex_unlock(&resman->mutex);
  gavl_array_sort(arr, compare_func, NULL);
  
  }


void bg_resource_get_by_protocol(const char * protocol, int full_match, gavl_time_t timeout, gavl_array_t * arr)
  {
  int i;
  const gavl_dictionary_t  * test_dict;
  const char  * test_uri;

  gavl_time_delay(&timeout);
  
  pthread_mutex_lock(&resman->mutex);

  for(i = 0; i < resman->remote.num_entries; i++)
    {
    if((test_dict = gavl_value_get_dictionary(&resman->remote.entries[i])) &&
       (test_uri = gavl_dictionary_get_string(test_dict, GAVL_META_CLASS)))
      {
      char *test_protocol = NULL;
      if(gavl_url_split(test_uri, &test_protocol, NULL, NULL, NULL, NULL, NULL))
        {
        if(full_match)
          {
          if(!strcmp(test_protocol, protocol))
            gavl_array_splice_val(arr, -1, 0, &resman->remote.entries[i]);
          }
        else
          {
          if(gavl_string_starts_with(test_protocol, protocol))
            gavl_array_splice_val(arr, -1, 0, &resman->remote.entries[i]);
          }
        }
      if(test_protocol)
        free(test_protocol);
      
      }
    }
  
  pthread_mutex_unlock(&resman->mutex);

  gavl_array_sort(arr, compare_func, NULL);
  
  }

static void list_resource_array(gavl_array_t * arr)
  {
  int i;
  const gavl_dictionary_t * dict;
  for(i = 0; i < arr->num_entries; i++)
    {
    if((dict = gavl_value_get_dictionary(&arr->entries[i])))
      {
      printf("# %s\n", gavl_dictionary_get_string(dict, GAVL_META_LABEL));
      printf("%s\n", gavl_dictionary_get_string(dict, GAVL_META_URI));
      }
    }
  }

void bg_resource_list_by_class(const char * klass, int full_match, gavl_time_t timeout)
  {
  gavl_array_t arr;
  gavl_array_init(&arr);
  bg_resource_get_by_class(klass, full_match, timeout, &arr);
  list_resource_array(&arr);
  gavl_array_free(&arr);
  }
  
void bg_resource_list_by_protocol(const char * protocol, int full_match, gavl_time_t timeout)
  {
  gavl_array_t arr;
  gavl_array_init(&arr);
  bg_resource_get_by_protocol(protocol, full_match, timeout, &arr);
  list_resource_array(&arr);
  gavl_array_free(&arr);
  }

void bg_opt_list_recording_sources(void * data, int * argc,
                                   char *** _argv, int arg)
  {
  bg_resource_list_by_class("item.recorder.", 0, 1000);
  }

void bg_resourcemanager_cleanup()
  {
  int i;
  
  if(resman)
    {
    gavl_msg_t * msg;
    
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Freeing resource manager");

    msg = bg_msg_sink_get(resman->ctrl.cmd_sink);
    gavl_msg_set_id_ns(msg, GAVL_MSG_QUIT, GAVL_MSG_NS_GENERIC);
    bg_msg_sink_put(resman->ctrl.cmd_sink);

    pthread_join(resman->th, NULL);
    
    /* Finish thread */

    for(i = 0; i < resman->num_plugins; i++)
      {
      if(resman->plugins[i].h)
        {
        bg_plugin_unref(resman->plugins[i].h);
        }
      }

    if(resman->plugins)
      free(resman->plugins);
    
    gavl_array_free(&resman->local);
    gavl_array_free(&resman->remote);
    bg_controllable_cleanup(&resman->ctrl);

    bg_msg_sink_destroy(resman->plugin_sink);

    pthread_mutex_destroy(&resman->mutex);
    
    free(resman);
    }
  
  }

