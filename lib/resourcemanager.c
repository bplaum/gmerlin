#include <string.h>

#include <gavl/log.h>
#define LOG_DOMAIN "resourcemanager"

#include <gmerlin/resourcemanager.h>
#include <gmerlin/pluginregistry.h>

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

static void add(gavl_array_t * arr, gavl_dictionary_t * dict, const char * id)
  {
  gavl_value_t val;
  gavl_dictionary_t * dict_new;
  gavl_value_init(&val);
  dict_new = gavl_value_set_dictionary(&val);
  
  gavl_dictionary_copy(dict_new, dict);
  gavl_dictionary_set_string(dict_new, GAVL_META_ID, id);
  gavl_array_splice_val_nocopy(arr, -1, 0, &val);
  }

static void del(gavl_array_t * arr, int idx)
  {
  gavl_array_splice_val(arr, idx, 1, NULL);
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
          gavl_msg_get_arg_dictionary(msg, 0, &dict);
          add(&resman->local, &dict, id);
          
          }
          break;
#if 0 // Deleting local resources is not needed for now
        case GAVL_MSG_RESOURCE_DELETED:
          break;
#endif
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
          gavl_dictionary_t dict;

          id = gavl_dictionary_get_string(&msg->header, GAVL_MSG_CONTEXT_ID);
          
          if(find_resource_idx_by_string(0, GAVL_META_ID, id) >= 0)
            return 1;

          gavl_dictionary_init(&dict);
          gavl_msg_get_arg_dictionary(msg, 0, &dict);

          fprintf(stderr, "Got resource:\n");
          gavl_dictionary_dump(&dict, 2);
          fprintf(stderr, "\n");
          
          add(&resman->remote, &dict, id);

          }
          break;
        case GAVL_MSG_RESOURCE_DELETED:
          {
          const char * id;
          int idx;

          id = gavl_dictionary_get_string(&msg->header, GAVL_MSG_CONTEXT_ID);
          
          if((idx = find_resource_idx_by_string(0, GAVL_META_ID, id)) < 0)
            return 1;

          fprintf(stderr, "Deleted resource: %s\n", id);
          del(&resman->remote, idx);
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
  gavl_time_t delay_time = GAVL_TIME_SCALE / 20; // 50 ms

  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Started thread");

  while(1)
    {
    result = 0;

    /* Handle external messages */
    if(!bg_msg_sink_iteration(resman->ctrl.cmd_sink))
      break;

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

#if defined(__GNUC__)

static void cleanup_resources() __attribute__ ((destructor));

static void cleanup_resources()
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
        bg_plugin_unref(resman->plugins[i].h);
      }
    
    gavl_array_free(&resman->local);
    gavl_array_free(&resman->remote);
    bg_controllable_cleanup(&resman->ctrl);
        
    free(resman);
    }
  }

#endif
