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

#include <pipewire/pipewire.h>

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

#if 0
// This callback gets called when our context changes state.  We really only
// care about when it's ready or if it has failed
static void pa_state_cb(pa_context *c, void *userdata)
  {
  pa_context_state_t state;
  pipewire_t * p = userdata;
  
  state = pa_context_get_state(c);
  switch(state)
    {
    // There are just here for reference
    case PA_CONTEXT_UNCONNECTED:
    case PA_CONTEXT_CONNECTING:
    case PA_CONTEXT_AUTHORIZING:
    case PA_CONTEXT_SETTING_NAME:
    default:
      break;
    case PA_CONTEXT_FAILED:
    case PA_CONTEXT_TERMINATED:
      p->flags |= FLAG_ERROR;
      break;
    case PA_CONTEXT_READY:
      p->flags |= FLAG_READY;
      break;
    }
  }

#endif

static char * make_id(const char * klass, int idx)
  {
  if(!strcmp(klass, GAVL_META_CLASS_AUDIO_RECORDER))
    return gavl_sprintf("pipewire-source-%d", idx);

  if(!strcmp(klass, GAVL_META_CLASS_SINK_AUDIO))
    return gavl_sprintf("pipewire-sink-%d", idx);
  
  return NULL;
  }
  
static void add_device(pipewire_t * reg, gavl_dictionary_t * dict, int idx)
  {
  gavl_msg_t * msg;
  const char * klass;

  klass = gavl_dictionary_get_string(dict, GAVL_META_CLASS);
  
  msg = bg_msg_sink_get(reg->ctrl.evt_sink);
  
  gavl_msg_set_id_ns(msg, GAVL_MSG_RESOURCE_ADDED, GAVL_MSG_NS_GENERIC);
  gavl_dictionary_set_string_nocopy(&msg->header, GAVL_MSG_CONTEXT_ID, make_id(klass, idx));
  gavl_msg_set_arg_dictionary(msg, 0, dict);
  
  bg_msg_sink_put(reg->ctrl.evt_sink);

  }

static void del_device(pipewire_t * reg, const char * klass, int idx)
  {
  gavl_msg_t * msg = bg_msg_sink_get(reg->ctrl.evt_sink);
  
  gavl_msg_set_id_ns(msg, GAVL_MSG_RESOURCE_DELETED, GAVL_MSG_NS_GENERIC);
  gavl_dictionary_set_string_nocopy(&msg->header, GAVL_MSG_CONTEXT_ID, make_id(klass, idx));
  bg_msg_sink_put(reg->ctrl.evt_sink);
  }

static void registry_event_global(void *data, uint32_t id,
                                  uint32_t permissions,
                                  const char *type, uint32_t version,
                                  const struct spa_dict *props)
  {
  int i;
  pipewire_t * reg = data;

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
  else if(!strcmp(type, PW_TYPE_INTERFACE_Node))
    {
    fprintf(stderr, "Got Node\n");
    for(i = 0; i < props->n_items; i++)
      {
      fprintf(stderr, "  %s: %s\n", props->items[i].key, props->items[i].value);
      }
    return;
    }
  else if(!strcmp(type, PW_TYPE_INTERFACE_Factory))
    {
    fprintf(stderr, "Got Factory\n");
    for(i = 0; i < props->n_items; i++)
      {
      fprintf(stderr, "  %s: %s\n", props->items[i].key, props->items[i].value);
      }
    return;
    }
  }
  
static void registry_event_global_remove(void *data, uint32_t id)
  {

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

#if 0  
  
  if(!reg->pa_ml)
    return 0;
  
  pa_mainloop_iterate(reg->pa_ml, 0, NULL);

  if(reg->pa_op && (pa_operation_get_state(reg->pa_op) == PA_OPERATION_DONE))
    {
    pa_operation_unref(reg->pa_op);
    reg->pa_op = NULL;

    if(!(reg->flags & FLAG_GOT_SOURCES))
      {
      reg->flags |= FLAG_GOT_SOURCES;
      reg->pa_op = pa_context_get_sink_info_list(reg->pa_ctx, pa_sink_cb, reg);
      reg->num_ops++;
      }
    else
      {
      reg->flags |= FLAG_GOT_SINKS;

      pa_context_set_subscribe_callback(reg->pa_ctx, pa_subscribe_callback, reg);
      pa_context_subscribe(reg->pa_ctx, PA_SUBSCRIPTION_MASK_SOURCE | PA_SUBSCRIPTION_MASK_SINK, NULL, NULL);
      reg->num_ops++;
      }
    
    }
#else

  pw_loop_iterate(pw_main_loop_get_loop(reg->loop), 0);
  
#endif
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
