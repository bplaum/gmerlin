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
#define LOG_DOMAIN "res_pulse"

#include <gavl/utils.h>

#include <gmerlin/translation.h>
#include <gmerlin/plugin.h>

#include <pulse/pulseaudio.h>
#include "pulseaudio_common.h"

#define FLAG_READY       (1<<0)
#define FLAG_ERROR       (1<<1)
#define FLAG_GOT_SOURCES (1<<2)
#define FLAG_GOT_SINKS   (1<<3)

typedef struct
  {
  bg_controllable_t ctrl;

  /* Pulseaudio */
  pa_mainloop *pa_ml;
  pa_operation *pa_op;
  pa_context *pa_ctx;

  int flags;
  
  //  int pa_ready;
  //  int pa_got_initial_devs;
  
  int num_ops;
  
  char hostname[HOST_NAME_MAX+1];
  int defaults_added;
  
  } pulse_t;

static int handle_msg(void * priv, gavl_msg_t * msg)
  {
  return 1;
  }


/* */

// This callback gets called when our context changes state.  We really only
// care about when it's ready or if it has failed
static void pa_state_cb(pa_context *c, void *userdata)
  {
  pa_context_state_t state;
  pulse_t * p = userdata;
  
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

static char * make_id(const char * klass, int idx)
  {
  const char * prefix = NULL;

  
  
  if(!strcmp(klass, GAVL_META_CLASS_AUDIO_RECORDER))
    prefix = "pulseaudio-source";
  else if(!strcmp(klass, GAVL_META_CLASS_SINK_AUDIO))
    prefix = "pulseaudio-sink";

  if(!prefix)
    return NULL;
  
  if(idx >= 0)
    return gavl_sprintf("%s-%d", prefix, idx);
  else
    return gavl_sprintf("%s-default", prefix);
  }
  
static void add_device(pulse_t * reg, gavl_dictionary_t * dict, int idx)
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

static void del_device(pulse_t * reg, const char * klass, int idx)
  {
  gavl_msg_t * msg = bg_msg_sink_get(reg->ctrl.evt_sink);
  
  gavl_msg_set_id_ns(msg, GAVL_MSG_RESOURCE_DELETED, GAVL_MSG_NS_GENERIC);
  gavl_dictionary_set_string_nocopy(&msg->header, GAVL_MSG_CONTEXT_ID, make_id(klass, idx));
  bg_msg_sink_put(reg->ctrl.evt_sink);
  }

static void pa_source_cb(pa_context *c, const pa_source_info *l, int eol, void *userdata)
  {
  pulse_t * reg = userdata;

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

      gavl_dictionary_set_string_nocopy(&dict, GAVL_META_URI, gavl_sprintf(PULSE_SOURCE_PROTOCOL"://%s/%s",
                                                                           hostname, pos));
      
      free(hostname);
      }
    else
      gavl_dictionary_set_string_nocopy(&dict, GAVL_META_URI, gavl_sprintf(PULSE_SOURCE_PROTOCOL"://%s/%s",
                                                                           reg->hostname, l->name));
    
    add_device(reg, &dict, l->index);
    gavl_dictionary_free(&dict);
    }
  }

static void pa_sink_cb(pa_context *c, const pa_sink_info *l, int eol, void *userdata)
  {
  pulse_t * reg = userdata;

  if(l)
    {
    gavl_dictionary_t dict;
    gavl_dictionary_init(&dict);
    
    //    fprintf(stderr, "Got sink: %s\n", l->name);
    
    gavl_dictionary_set_string(&dict, GAVL_META_LABEL, l->description);
    gavl_dictionary_set_string(&dict, GAVL_META_CLASS, GAVL_META_CLASS_SINK_AUDIO);
    gavl_dictionary_set_string_nocopy(&dict, GAVL_META_URI, gavl_sprintf(PULSE_SINK_PROTOCOL"://%s/%s",
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
  
  pulse_t * reg = userdata;

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
      char * id = gavl_sprintf(PULSE_SOURCE_PROTOCOL"-%d", idx);
      del_device(reg, id);
      reg->pa_op = pa_context_get_source_info_by_index(c, idx, pa_source_cb, userdata);
      fprintf(stderr, "%d changed\n", idx);
      free(id);
      break;
      }
#endif
    }
  
  }


static void * create_pulse()
  {
  pulse_t * ret;
  pa_mainloop_api *pa_mlapi;

  ret = calloc(1, sizeof(*ret));

  gethostname(ret->hostname, HOST_NAME_MAX+1);

    // Create a mainloop API and connection to the default server
  ret->pa_ml = pa_mainloop_new();
  
  pa_mlapi = pa_mainloop_get_api(ret->pa_ml);
  ret->pa_ctx = pa_context_new(pa_mlapi, "gmerlin-pulseaudio-devices");
  
  // This function connects to the pulse server
  if(pa_context_connect(ret->pa_ctx, NULL, PA_CONTEXT_NOAUTOSPAWN, NULL) < 0)
    {
    ret->flags |= FLAG_ERROR;
    }
  else
    {
    // This function defines a callback so the server will tell us it's state.
    // Our callback will wait for the state to be ready.  The callback will
    // modify the variable to 1 so we know when we have a connection and it's
    // ready.
    // If there's an error, the callback will set pa_ready to 2
    pa_context_set_state_callback(ret->pa_ctx, pa_state_cb, ret);

    // Now we'll enter into an infinite loop until we get the data we receive
    // or if there's an error
    for (;;)
      {
      // We can't do anything until PA is ready, so just iterate the mainloop
      // and continue
      if(!(ret->flags & (FLAG_READY | FLAG_ERROR)))
        {
        pa_mainloop_iterate(ret->pa_ml, 1, NULL);
        continue;
        }
      // We couldn't get a connection to the server, so exit out
      else
        break;
      }
    }

  if(ret->flags &FLAG_READY)
    ret->pa_op = pa_context_get_source_info_list(ret->pa_ctx, pa_source_cb, ret);
  else
    {
    gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Connection to pulseaudio failed");
    //    pa_context_disconnect(ret->pa_ctx);
    pa_context_unref(ret->pa_ctx);
    pa_mainloop_free(ret->pa_ml);
    ret->pa_ctx = NULL;
    ret->pa_ml  = NULL;
    }

  bg_controllable_init(&ret->ctrl,
                       bg_msg_sink_create(handle_msg, ret, 1),
                       bg_msg_hub_create(1));
  
  return ret;
  }

static void destroy_pulse(void * priv)
  {
  pulse_t * reg = priv;

  if(reg->pa_op)
    pa_operation_unref(reg->pa_op);

  if(reg->pa_ctx)
    {
    pa_context_disconnect(reg->pa_ctx);
    pa_context_unref(reg->pa_ctx);
    }
  if(reg->pa_ml)
    pa_mainloop_free(reg->pa_ml);

  bg_controllable_cleanup(&reg->ctrl);
  
  free(reg);
  }

static int update_pulse(void * priv)
  {
  pulse_t * reg = priv;

  reg->num_ops = 0;
  
  if(!reg->pa_ml)
    return 0;

  if(!reg->defaults_added)
    {
    gavl_dictionary_t dict;
    gavl_dictionary_init(&dict);
    gavl_dictionary_set_string_nocopy(&dict, GAVL_META_URI, gavl_sprintf("%s://%s", PULSE_SOURCE_PROTOCOL, reg->hostname));
    gavl_dictionary_set_string(&dict, GAVL_META_LABEL, TR("System default recorder"));
    gavl_dictionary_set_string(&dict, GAVL_META_CLASS, GAVL_META_CLASS_AUDIO_RECORDER);
    add_device(reg, &dict, -1);
    
    gavl_dictionary_set_string_nocopy(&dict, GAVL_META_URI, gavl_sprintf("%s://%s", PULSE_SINK_PROTOCOL, reg->hostname));
    gavl_dictionary_set_string(&dict, GAVL_META_LABEL, TR("System default sink"));
    gavl_dictionary_set_string(&dict, GAVL_META_CLASS, GAVL_META_CLASS_SINK_AUDIO);
    add_device(reg, &dict, -1);
    
    reg->defaults_added = 1;
    }
  
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
  return reg->num_ops;
  }

static bg_controllable_t * get_controllable_pulse(void * priv)
  {
  pulse_t * p = priv;
  return &p->ctrl;
  }

bg_controllable_plugin_t the_plugin =
  {
    .common =
    {
      BG_LOCALE,
      .name =      "res_pulse",
      .long_name = TRS("Pulseaudio device manager"),
      .description = TRS("Manages pulseaudio"),
      .type =     BG_PLUGIN_RESOURCE_DETECTOR,
      .flags =    0,
      .create =   create_pulse,
      .destroy =   destroy_pulse,
      .get_controllable =   get_controllable_pulse,
      .priority =         1,
    },
    .update = update_pulse,

  };

/* Include this into all plugin modules exactly once
   to let the plugin loader obtain the API version */
BG_GET_PLUGIN_API_VERSION;
