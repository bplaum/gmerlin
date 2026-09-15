/*****************************************************************
 * gmerlin - a general purpose multimedia framework and applications
 *
 * Copyright (c) 2001 - 2024 Members of the Gmerlin project
 * http://github.com/bplaum
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * *****************************************************************/


#include <unistd.h>
#include <string.h>

#include <config.h>

#include <gavl/log.h>
#define LOG_DOMAIN "res_pipewire"

#include <gavl/utils.h>

#include <gmerlin/translation.h>
#include <gmerlin/plugin.h>
#include <gmerlin/resourcemanager.h>

#include <pipewire/pipewire.h>

#include "pipewire_common.h"

#define FLAG_READY       (1<<0)
#define FLAG_ERROR       (1<<1)
#define FLAG_GOT_SOURCES (1<<2)
#define FLAG_GOT_SINKS   (1<<3)

typedef struct
  {
  bg_controllable_t ctrl;

  struct pw_main_loop *loop;
  struct pw_context *context;
  struct pw_core *core;
  struct pw_registry *registry;
  struct spa_hook registry_listener;
  
  int flags;
  int num_ops;
  
  char hostname[HOST_NAME_MAX+1];
  
  } pipewire_t;

static int handle_msg(void * priv, gavl_msg_t * msg)
  {
  return 1;
  }


/* */

static char * make_id(uint32_t id, int monitor)
  {
  if(monitor)
    return gavl_sprintf("pipewire-monitor-%08x", id);
  else
    return gavl_sprintf("pipewire-node-%08x", id);
  }
  
static void add_device(pipewire_t * reg, gavl_dictionary_t * dict, char * id)
  {
  gavl_msg_t * msg;
  
  msg = bg_msg_sink_get(reg->ctrl.evt_sink);
  
  gavl_msg_set_id_ns(msg, GAVL_MSG_RESOURCE_ADDED, GAVL_MSG_NS_GENERIC);
  gavl_dictionary_set_string_nocopy(&msg->header, GAVL_MSG_CONTEXT_ID, id);
  gavl_msg_set_arg_dictionary(msg, 0, dict);
#if 0
  fprintf(stderr, "Add pipewire device:\n");
  gavl_dictionary_dump(dict, 2);
  fprintf(stderr, "\n");
#endif
  bg_msg_sink_put(reg->ctrl.evt_sink);

  }

static void del_device(pipewire_t * reg, int id)
  {
  gavl_msg_t * msg = bg_msg_sink_get(reg->ctrl.evt_sink);
  
  gavl_msg_set_id_ns(msg, GAVL_MSG_RESOURCE_DELETED, GAVL_MSG_NS_GENERIC);
  gavl_dictionary_set_string_nocopy(&msg->header, GAVL_MSG_CONTEXT_ID, make_id(id, 0));
  bg_msg_sink_put(reg->ctrl.evt_sink);

  msg = bg_msg_sink_get(reg->ctrl.evt_sink);
  
  gavl_msg_set_id_ns(msg, GAVL_MSG_RESOURCE_DELETED, GAVL_MSG_NS_GENERIC);
  gavl_dictionary_set_string_nocopy(&msg->header, GAVL_MSG_CONTEXT_ID, make_id(id, 1));
  bg_msg_sink_put(reg->ctrl.evt_sink);
  
  }

static void registry_event_global(void *data, uint32_t id,
                                  uint32_t permissions,
                                  const char *type, uint32_t version,
                                  const struct spa_dict *props)
  {
  pipewire_t * reg = data;
  //  int i;

#if 0  
  /* We aren't interested in these for now */
  if(!strcmp(type, PW_TYPE_INTERFACE_Port) ||
     !strcmp(type, PW_TYPE_INTERFACE_Core) ||
     !strcmp(type, PW_TYPE_INTERFACE_Client))
    {
    //    fprintf(stderr, "Got Port\n");
    return;
    }

  fprintf(stderr, "object: id:%u type:%s/%d\n", id, type, version);

  if(!strcmp(type, PW_TYPE_INTERFACE_Device))
    {
    fprintf(stderr, "Got device\n");
    for(i = 0; i < props->n_items; i++)
      {
      fprintf(stderr, "  %s: %s\n", props->items[i].key, props->items[i].value);
      }
    return;
    }
  else
#endif

  if(!strcmp(type, PW_TYPE_INTERFACE_Node))
    {
    const char * klass;
    const char * name;
    const char * label;
    gavl_dictionary_t dict;
    gavl_dictionary_init(&dict);
    
    //    fprintf(stderr, "Got Node\n");
    
    if(!(klass = spa_dict_lookup(props, PW_KEY_MEDIA_CLASS)))
      return; // Don't care

    if(!(name = spa_dict_lookup(props, PW_KEY_NODE_NAME)))
      return; // Don't care
    
    if(!strcmp(klass, "Audio/Source"))
      {
      gavl_dictionary_set_string(&dict, GAVL_META_CLASS, GAVL_META_CLASS_AUDIO_RECORDER);
      gavl_dictionary_set_string_nocopy(&dict, GAVL_META_URI, gavl_sprintf(PIPEWIRE_SOURCE_PROTOCOL"://%s/%s",
                                                                           reg->hostname, name));
      }
    else if(!strcmp(klass, "Audio/Sink"))
      {
      gavl_dictionary_set_string(&dict, GAVL_META_CLASS, GAVL_META_CLASS_SINK_AUDIO);
      gavl_dictionary_set_string_nocopy(&dict, GAVL_META_URI, gavl_sprintf(PIPEWIRE_SINK_PROTOCOL"://%s/%s",
                                                                           reg->hostname, name));
      }
    else
      return; // Don't care
    

    if(!(label = spa_dict_lookup(props, PW_KEY_NODE_NICK)))
      label = spa_dict_lookup(props, PW_KEY_NODE_DESCRIPTION);

#if 0
    fprintf(stderr, "Got %s %s %s\n", klass, name, label);
    
    for(i = 0; i < props->n_items; i++)
      {
      fprintf(stderr, "  %s: %s\n", props->items[i].key, props->items[i].value);
      }
#endif
    gavl_dictionary_set_string(&dict, GAVL_META_LABEL, label);
    
    add_device(reg, &dict, make_id(id, 0));

    gavl_dictionary_free(&dict);
    
    return;
    }
  if(!strcmp(type, PW_TYPE_INTERFACE_Port))
    {
    uint32_t id;
    const char * var;
    
    if((var = spa_dict_lookup(props, PW_KEY_PORT_MONITOR)) &&
       !strcmp(var, "true") &&
       (var = spa_dict_lookup(props, PW_KEY_NODE_ID)) &&
       (sscanf(var, "%"PRIu32, &id) == 1))
      {
      const gavl_dictionary_t * node;
      char * monitor_id = NULL;
      char * node_id    = NULL;

      gavl_dictionary_t dict;
      gavl_dictionary_init(&dict);
      
      
      monitor_id = make_id(id, 1);

      if((node = bg_resource_get_by_id(0, monitor_id)))
        {
        free(monitor_id);
        return;
        }

      node_id = make_id(id, 0);

      if(!(node = bg_resource_get_by_id(0, node_id)))
        {
        free(monitor_id);
        free(node_id);
        return;
        }
      
      
      var = gavl_dictionary_get_string(node, GAVL_META_URI);
      
      //      fprintf(stderr, "Got monitor port for %s\n", var);

      var = strstr(var, "://");
      var += 3;
      
      gavl_dictionary_set_string(&dict, GAVL_META_CLASS, GAVL_META_CLASS_AUDIO_RECORDER);
      gavl_dictionary_set_string_nocopy(&dict, GAVL_META_URI,
                                        gavl_sprintf(PIPEWIRE_MONITOR_PROTOCOL"://%s", var));
      
      gavl_dictionary_set_string(&dict, GAVL_META_LABEL,
                                 gavl_sprintf("Monitor of %s",
                                              gavl_dictionary_get_string(node, GAVL_META_LABEL)));
      
      add_device(reg, &dict, monitor_id);
      
      //      free(node_id);
      gavl_dictionary_free(&dict);
      }
#if 0
    fprintf(stderr, "Got port\n");
    for(i = 0; i < props->n_items; i++)
      {
      fprintf(stderr, "  %s: %s\n", props->items[i].key, props->items[i].value);
      }
#endif
    }
  
#if 0
  else if(!strcmp(type, PW_TYPE_INTERFACE_Factory))
    {
    fprintf(stderr, "Got Factory\n");
    for(i = 0; i < props->n_items; i++)
      {
      fprintf(stderr, "  %s: %s\n", props->items[i].key, props->items[i].value);
      }
    return;
    }
#endif
  }
  
static void registry_event_global_remove(void *data, uint32_t id)
  {
  pipewire_t * reg = data;
  del_device(reg, id);
  
  }
 
static const struct pw_registry_events registry_events = {
  PW_VERSION_REGISTRY_EVENTS,
  .global = registry_event_global,
  .global_remove = registry_event_global_remove,
};

#if 0
static void pa_source_cb(pa_context *c, const pa_source_info *l, int eol, void *userdata)
  {
  pipewire_t * reg = userdata;

  if(l)
    {
    gavl_dictionary_t dict;
    gavl_dictionary_init(&dict);
    
    //    fprintf(stderr, "Got source: %s\n", l->name);
    
    gavl_dictionary_set_string(&dict, GAVL_META_LABEL, l->description);
    gavl_dictionary_set_string(&dict, GAVL_META_CLASS, GAVL_META_CLASS_AUDIO_RECORDER);

    if(gavl_string_starts_with(l->name, "tunnel."))
      {
      char * hostname;
      
      const char * pos;
      const char * end_pos;

      pos = l->name + 7;
      end_pos = strchr(pos, '.'); // after hostname

      end_pos++;
      end_pos = strchr(end_pos, '.'); /// after .local
      
      hostname = gavl_strndup(pos, end_pos);

      pos = end_pos + 1;

      gavl_dictionary_set_string_nocopy(&dict, GAVL_META_URI, gavl_sprintf("pipewire-source://%s/%s",
                                                                           hostname, pos));
      
      free(hostname);
      }
    else
      gavl_dictionary_set_string_nocopy(&dict, GAVL_META_URI, gavl_sprintf("pipewire-source://%s/%s",
                                                                           reg->hostname, l->name));
    
    add_device(reg, &dict, l->index);
    gavl_dictionary_free(&dict);
    }
  }

static void pa_sink_cb(pa_context *c, const pa_sink_info *l, int eol, void *userdata)
  {
  pipewire_t * reg = userdata;

  if(l)
    {
    gavl_dictionary_t dict;
    gavl_dictionary_init(&dict);
    
    //    fprintf(stderr, "Got source: %s\n", l->name);
    
    gavl_dictionary_set_string(&dict, GAVL_META_LABEL, l->description);
    gavl_dictionary_set_string(&dict, GAVL_META_CLASS, GAVL_META_CLASS_SINK_AUDIO);
    gavl_dictionary_set_string_nocopy(&dict, GAVL_META_URI, gavl_sprintf("pipewire-sink://%s/%s",
                                                                         reg->hostname,
                                                                         l->name));
    
    add_device(reg, &dict, l->index);
    gavl_dictionary_free(&dict);
    }
  }

static void pa_subscribe_callback(pa_context *c,
                                  pa_subscription_event_type_t type,
                                  uint32_t idx, void *userdata)
  {
  int source = 0;
  
  pipewire_t * reg = userdata;

  reg->num_ops++;
  
  if((type & PA_SUBSCRIPTION_EVENT_FACILITY_MASK) == PA_SUBSCRIPTION_EVENT_SOURCE)
    source = 1;
  else if((type & PA_SUBSCRIPTION_EVENT_FACILITY_MASK) != PA_SUBSCRIPTION_EVENT_SINK)
    return; // We handle only sources and sinks
    
  switch(type & PA_SUBSCRIPTION_EVENT_TYPE_MASK)
    {
    case PA_SUBSCRIPTION_EVENT_NEW:
      {
      pa_operation *op;
      
      if(source)
        op = pa_context_get_source_info_by_index(c, idx, pa_source_cb, userdata);
      else
        op = pa_context_get_sink_info_by_index(c, idx, pa_sink_cb, userdata);
      
      pa_operation_unref(op);
      
      break;
      }
    case PA_SUBSCRIPTION_EVENT_REMOVE:
      {
      if(source)
        del_device(reg, GAVL_META_CLASS_AUDIO_RECORDER, idx);
      else
        del_device(reg, GAVL_META_CLASS_SINK_AUDIO, idx);
      
      }
      break;
#if 0
    case PA_SUBSCRIPTION_EVENT_CHANGE:
      {
      char * id = gavl_sprintf("pipewire-source-%d", idx);
      del_device(reg, id);
      reg->pa_op = pa_context_get_source_info_by_index(c, idx, pa_source_cb, userdata);
      fprintf(stderr, "%d changed\n", idx);
      free(id);
      break;
      }
#endif
    }
  
  }
#endif




static void * create_pipewire()
  {
  pipewire_t * ret;
  
  ret = calloc(1, sizeof(*ret));

  gethostname(ret->hostname, HOST_NAME_MAX+1);

  pw_init(NULL, NULL);
  
    // Create a mainloop API and connection to the default server
  ret->loop = pw_main_loop_new(NULL);

  ret->context = pw_context_new(pw_main_loop_get_loop(ret->loop),
                                NULL /* properties */,
                                0 /* user_data size */);
  
  ret->core = pw_context_connect(ret->context,
                                 NULL /* properties */,
                                 0 /* user_data size */);
  
  ret->registry = pw_core_get_registry(ret->core, PW_VERSION_REGISTRY,
                                       0 /* user_data size */);
  
  spa_zero(ret->registry_listener);
  pw_registry_add_listener(ret->registry, &ret->registry_listener,
                           &registry_events, ret);
  
  bg_controllable_init(&ret->ctrl,
                       bg_msg_sink_create(handle_msg, ret, 1),
                       bg_msg_hub_create(1));
  
  return ret;
  }

static void destroy_pipewire(void * priv)
  {
  pipewire_t * reg = priv;

  pw_proxy_destroy((struct pw_proxy*)reg->registry);
  pw_core_disconnect(reg->core);
  pw_context_destroy(reg->context);
  pw_main_loop_destroy(reg->loop);
  
  bg_controllable_cleanup(&reg->ctrl);
  
  free(reg);
  }

static int update_pipewire(void * priv)
  {
  pipewire_t * reg = priv;

  reg->num_ops = 0;
  pw_loop_iterate(pw_main_loop_get_loop(reg->loop), 0);
  return reg->num_ops;
  }

static bg_controllable_t * get_controllable_pipewire(void * priv)
  {
  pipewire_t * p = priv;
  return &p->ctrl;
  }

bg_controllable_plugin_t the_plugin =
  {
    .common =
    {
      BG_LOCALE,
      .name =      "res_pipewire",
      .long_name = TRS("Pipewire resource manager"),
      .description = TRS("Manages pipewire sources and sinks"),
      .type =     BG_PLUGIN_RESOURCE_DETECTOR,
      .flags =    0,
      .create =   create_pipewire,
      .destroy =   destroy_pipewire,
      .get_controllable =   get_controllable_pipewire,
      .priority =         1,
    },
    .update = update_pipewire,

  };

/* Include this into all plugin modules exactly once
   to let the plugin loader obtain the API version */
BG_GET_PLUGIN_API_VERSION;
