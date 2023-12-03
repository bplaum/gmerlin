/*****************************************************************
 * gmerlin - a general purpose multimedia framework and applications
 *
 * Copright (c) 2001 - 2012 Members of the Gmerlin project
 * gmerlin-general@lists.sourceforge.net
 * http://gmerlin.sourceforge.net
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

#include <errno.h>
#include <config.h>
#include <unistd.h>
#include <string.h>

#ifdef HAVE_PULSEAUDIO
#include <pulse/pulseaudio.h>
#endif

#ifdef HAVE_LINUX_VIDEODEV2_H

#ifndef HAVE_INOTIFY
#undef HAVE_LINUX_VIDEODEV2_H
#else

#include <gavl/hw_v4l2.h>
#include <sys/inotify.h>

#endif 
#endif

#include <gmerlin/recordingdevice.h>
#include <gavl/log.h>
#define LOG_DOMAIN "recordingdevice"

#define SYSFSPATH "sysfs"

/* Detect recording devices */

struct bg_recording_device_registry_s
  {
  bg_msg_hub_t * hub;
  bg_msg_sink_t * sink;

  char hostname[HOST_NAME_MAX+1];
  
#ifdef HAVE_PULSEAUDIO
  /* Pulseaudio */
  pa_mainloop *pa_ml;
  pa_operation *pa_op;
  pa_context *pa_ctx;
  
  int pa_got_initial_devs;
#endif
  
  /* Video4linux */

#ifdef HAVE_LINUX_VIDEODEV2_H
  int v4l2_fd;
  int v4l2_wd;
  int v4l_got_initial_devs;
#endif
  
  };

static void add_device(bg_recording_device_registry_t * reg,
                       gavl_dictionary_t * dev)
  {
  gavl_msg_t * msg;
  
  const char * uri;
  char md5[GAVL_MD5_LENGTH];

  uri = gavl_dictionary_get_string(dev, GAVL_META_URI);
  gavl_md5_buffer_str(uri, strlen(uri), md5);

  if(!gavl_dictionary_get(dev, GAVL_META_ID))
    gavl_dictionary_set_string(dev, GAVL_META_ID, md5);
  
  gavl_dictionary_set_string(dev, GAVL_META_HASH, md5);

  //  fprintf(stderr, "Add device:\n");
  //  gavl_dictionary_dump(dev, 2);
  
  msg = bg_msg_sink_get(reg->sink);
  gavl_msg_set_id_ns(msg, GAVL_MSG_RESOURCE_ADDED, GAVL_MSG_NS_GENERIC);
  gavl_dictionary_set_string(&msg->header, GAVL_MSG_CONTEXT_ID, gavl_dictionary_get_string(dev, GAVL_META_ID));
  gavl_msg_set_arg_dictionary(msg, 0, dev);
  bg_msg_sink_put(reg->sink);
  }

static void del_device(bg_recording_device_registry_t * reg,
                       const char * id)
  {
  gavl_msg_t * msg;
  
  msg = bg_msg_sink_get(reg->sink);
  gavl_msg_set_id_ns(msg, GAVL_MSG_RESOURCE_DELETED, GAVL_MSG_NS_GENERIC);
  gavl_dictionary_set_string(&msg->header, GAVL_MSG_CONTEXT_ID, id);
  bg_msg_sink_put(reg->sink);
  }

#ifdef HAVE_PULSEAUDIO
// This callback gets called when our context changes state.  We really only
// care about when it's ready or if it has failed
static void pa_state_cb(pa_context *c, void *userdata)
  {
  pa_context_state_t state;
  int *pa_ready = userdata;

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
      *pa_ready = 2;
      break;
    case PA_CONTEXT_READY:
      *pa_ready = 1;
      break;
    }
  }

static void pa_source_cb(pa_context *c, const pa_source_info *l, int eol, void *userdata)
  {
  bg_recording_device_registry_t * reg = userdata;

  if(l)
    {
    gavl_dictionary_t dict;
    gavl_dictionary_init(&dict);
    
    //    fprintf(stderr, "Got source: %s\n", l->name);
    
    gavl_dictionary_set_string_nocopy(&dict, GAVL_META_ID, gavl_sprintf("pulseaudio-source-%d", l->index));
    
    gavl_dictionary_set_string(&dict, GAVL_META_LABEL, l->description);
    gavl_dictionary_set_string(&dict, GAVL_META_MEDIA_CLASS, GAVL_META_MEDIA_CLASS_AUDIO_RECORDER);
    gavl_dictionary_set_string_nocopy(&dict, GAVL_META_URI, gavl_sprintf("pulseaudio-source://%s/%s", reg->hostname,
                                                                         l->name));
    
    add_device(reg, &dict);
    gavl_dictionary_free(&dict);
    }
  }

static void pa_subscribe_callback(pa_context *c,
                                  pa_subscription_event_type_t type,
                                  uint32_t idx, void *userdata)
  {
  bg_recording_device_registry_t * reg = userdata;
  
  fprintf(stderr, "pa_subscribe_callback %d %d\n", type, idx);

  if((type & PA_SUBSCRIPTION_EVENT_FACILITY_MASK) != PA_SUBSCRIPTION_EVENT_SOURCE)
    return;
  
  switch(type & PA_SUBSCRIPTION_EVENT_TYPE_MASK)
    {
    case PA_SUBSCRIPTION_EVENT_NEW:
      fprintf(stderr, "added\n");
      reg->pa_op = pa_context_get_source_info_by_index(c, idx, pa_source_cb, userdata);
      break;
    case PA_SUBSCRIPTION_EVENT_REMOVE:
      {
      char * id = gavl_sprintf("pulseaudio-source-%d", idx);
      del_device(reg, id);
      free(id);
      }
      break;
    case PA_SUBSCRIPTION_EVENT_CHANGE:
      fprintf(stderr, "changed\n");
      break;
    }
  
  }

#endif

#ifdef HAVE_LINUX_VIDEODEV2_H
static void v4l_add_device(bg_recording_device_registry_t * reg,
                           gavl_dictionary_t * dict)
  {
  char * real_uri;
  
  gavl_dictionary_set(dict, GAVL_V4L2_SRC_FORMATS, NULL);
  gavl_dictionary_set(dict, GAVL_V4L2_TYPE, NULL);
  gavl_dictionary_set(dict, GAVL_V4L2_TYPE_STRING, NULL);
  
  real_uri = gavl_sprintf("v4l2-capture://%s%s", reg->hostname, gavl_dictionary_get_string(dict, GAVL_META_URI));
  gavl_dictionary_set_string_nocopy(dict, GAVL_META_URI, real_uri);
  
  gavl_dictionary_set_string(dict, GAVL_META_MEDIA_CLASS,
                             GAVL_META_MEDIA_CLASS_VIDEO_RECORDER);
  
  add_device(reg, dict);
  }
#endif

bg_recording_device_registry_t * bg_recording_device_registry_create()
  {
#ifdef HAVE_PULSEAUDIO
  int pa_ready = 0;
  pa_mainloop_api *pa_mlapi;
#endif
  
  bg_recording_device_registry_t * ret = calloc(1, sizeof(*ret));

  gethostname(ret->hostname, HOST_NAME_MAX+1);
  
  ret->hub = bg_msg_hub_create(1);
  ret->sink = bg_msg_hub_get_sink(ret->hub);
  
#ifdef HAVE_PULSEAUDIO

  // Create a mainloop API and connection to the default server
  ret->pa_ml = pa_mainloop_new();
  
  pa_mlapi = pa_mainloop_get_api(ret->pa_ml);
  ret->pa_ctx = pa_context_new(pa_mlapi, "gmerlin-recording-devices");
  
  // This function connects to the pulse server
  if(pa_context_connect(ret->pa_ctx, NULL, 0, NULL) < 0)
    {
    pa_ready = 2;
    }
  else
    {
    // This function defines a callback so the server will tell us it's state.
    // Our callback will wait for the state to be ready.  The callback will
    // modify the variable to 1 so we know when we have a connection and it's
    // ready.
    // If there's an error, the callback will set pa_ready to 2
    pa_context_set_state_callback(ret->pa_ctx, pa_state_cb, &pa_ready);

    // Now we'll enter into an infinite loop until we get the data we receive
    // or if there's an error
    for (;;)
      {
      // We can't do anything until PA is ready, so just iterate the mainloop
      // and continue
      if(!pa_ready)
        {
        pa_mainloop_iterate(ret->pa_ml, 1, NULL);
        continue;
        }
      // We couldn't get a connection to the server, so exit out
      else
        break;
      }
    }

  if(pa_ready == 1)
    ret->pa_op = pa_context_get_source_info_list(ret->pa_ctx, pa_source_cb, ret);
  else
    {
    gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Connection to pulseaudio failed");
    pa_context_disconnect(ret->pa_ctx);
    pa_context_unref(ret->pa_ctx);
    pa_mainloop_free(ret->pa_ml);
    ret->pa_ctx = NULL;
    ret->pa_ml  = NULL;
    }
  
#endif

#ifdef HAVE_LINUX_VIDEODEV2_H
  ret->v4l2_fd = inotify_init1(IN_NONBLOCK);
  ret->v4l2_wd = inotify_add_watch(ret->v4l2_fd, "/dev", IN_CREATE | IN_DELETE);

  
#endif
  
  return ret;
  
  }



void bg_recording_device_registry_update(bg_recording_device_registry_t * reg)
  {
  int result = 0;


  
#ifdef HAVE_LINUX_VIDEODEV2_H
  struct
    {
    struct inotify_event evt;
    char name[NAME_MAX+32]; // might need some padding
    } evt;
  
  while(1)
    {
    result = read(reg->v4l2_fd, &evt, sizeof(evt));

    if((result < 0) && (errno == EAGAIN))
      break;

    if(result > 0)
      {
      if(evt.evt.mask & IN_CREATE)
        {
        int dummy;
        char * path;
        gavl_dictionary_t dict;
        int type;
        if(sscanf(evt.evt.name, "video%d", &dummy) != 1)
          continue;

        path = gavl_sprintf("/dev/%s", evt.evt.name);
        
        gavl_dictionary_init(&dict);
        gavl_v4l2_get_device_info(path, &dict);
        
        if(gavl_dictionary_get_int(&dict, GAVL_V4L2_TYPE, &type) &&
           (type == GAVL_V4L2_DEVICE_SOURCE))
          {
          v4l_add_device(reg, &dict);
          fprintf(stderr, "Adding v4l device %s\n", evt.evt.name);
          }
        else
          {
          fprintf(stderr, "Not adding v4l device %s\n", evt.evt.name);
          gavl_dictionary_dump(&dict, 2);
          fprintf(stderr, "\n");                               
          }
        gavl_dictionary_free(&dict);
        free(path);
        
        }
      else if(evt.evt.mask & IN_DELETE)
        {
        int dummy;
        char * uri;
        char md5[GAVL_MD5_LENGTH];
        if(sscanf(evt.evt.name, "video%d", &dummy) != 1)
          continue;
        uri = gavl_sprintf("v4l2-capture://%s/dev/%s", reg->hostname, evt.evt.name);
        gavl_md5_buffer_str(uri, strlen(uri), md5);
        
        del_device(reg, md5);
        free(uri);
        }
      }
    }

  if(!reg->v4l_got_initial_devs)
    {
    gavl_array_t v4l2_devs;
    int i;
    
    gavl_array_init(&v4l2_devs);
    gavl_v4l2_devices_scan_by_type(GAVL_V4L2_DEVICE_SOURCE, &v4l2_devs);
  
    for(i = 0; i < v4l2_devs.num_entries; i++)
      v4l_add_device(reg, gavl_value_get_dictionary_nc(&v4l2_devs.entries[i]));

    gavl_array_free(&v4l2_devs);
    reg->v4l_got_initial_devs = 1;
    }

  
#endif


#ifdef HAVE_PULSEAUDIO

  if(reg->pa_ml)
    {
    pa_mainloop_iterate(reg->pa_ml, 0, &result);

    if(reg->pa_op && (pa_operation_get_state(reg->pa_op) == PA_OPERATION_DONE))
      {
      pa_operation_unref(reg->pa_op);
      reg->pa_op = NULL;

      if(!reg->pa_got_initial_devs)
        {
        pa_context_set_subscribe_callback(reg->pa_ctx, pa_subscribe_callback, reg);
        pa_context_subscribe(reg->pa_ctx, PA_SUBSCRIPTION_MASK_SOURCE, NULL, NULL);
        }
      }
    }
  
#endif

  }

bg_msg_hub_t * bg_recording_device_registry_get_msg_hub(bg_recording_device_registry_t * reg)
  {
  return reg->hub;
  }

void bg_recording_device_registry_destroy(bg_recording_device_registry_t * reg)
  {
#ifdef HAVE_PULSEAUDIO
  pa_context_disconnect(reg->pa_ctx);
  pa_context_unref(reg->pa_ctx);
  pa_mainloop_free(reg->pa_ml);
  
  if(reg->pa_op)
    pa_operation_unref(reg->pa_op);
  
#endif

#ifdef HAVE_LINUX_VIDEODEV2_H
  close(reg->v4l2_fd);
#endif

  free(reg);
  }
