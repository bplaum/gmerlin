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

// #undef HAVE_LINUX_VIDEODEV2_H

#ifdef HAVE_LINUX_VIDEODEV2_H
#include <gavl/hw_v4l2.h>
#include <libudev.h>
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

  int num_ops;
  
#ifdef HAVE_PULSEAUDIO
  /* Pulseaudio */
  pa_mainloop *pa_ml;
  pa_operation *pa_op;
  pa_context *pa_ctx;
  int pa_ready;
  int pa_got_initial_devs;
#endif
  
  /* Video4linux */

#ifdef HAVE_LINUX_VIDEODEV2_H
  int v4l_got_initial_devs;

  struct udev *udev;
  struct udev_monitor *udev_mon;
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

  reg->num_ops++;

  }

static void del_device(bg_recording_device_registry_t * reg,
                       const char * id)
  {
  gavl_msg_t * msg;
  
  msg = bg_msg_sink_get(reg->sink);
  gavl_msg_set_id_ns(msg, GAVL_MSG_RESOURCE_DELETED, GAVL_MSG_NS_GENERIC);
  gavl_dictionary_set_string(&msg->header, GAVL_MSG_CONTEXT_ID, id);
  bg_msg_sink_put(reg->sink);

  reg->num_ops++;

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
  
  if((type & PA_SUBSCRIPTION_EVENT_FACILITY_MASK) != PA_SUBSCRIPTION_EVENT_SOURCE)
    return;
  
  switch(type & PA_SUBSCRIPTION_EVENT_TYPE_MASK)
    {
    case PA_SUBSCRIPTION_EVENT_NEW:
      reg->pa_op = pa_context_get_source_info_by_index(c, idx, pa_source_cb, userdata);
      break;
    case PA_SUBSCRIPTION_EVENT_REMOVE:
      {
      char * id = gavl_sprintf("pulseaudio-source-%d", idx);
      del_device(reg, id);
      free(id);
      }
      break;
#if 0
    case PA_SUBSCRIPTION_EVENT_CHANGE:
      {
      char * id = gavl_sprintf("pulseaudio-source-%d", idx);
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
  if(pa_context_connect(ret->pa_ctx, NULL, PA_CONTEXT_NOAUTOSPAWN, NULL) < 0)
    {
    ret->pa_ready = 2;
    }
  else
    {
    // This function defines a callback so the server will tell us it's state.
    // Our callback will wait for the state to be ready.  The callback will
    // modify the variable to 1 so we know when we have a connection and it's
    // ready.
    // If there's an error, the callback will set pa_ready to 2
    pa_context_set_state_callback(ret->pa_ctx, pa_state_cb, &ret->pa_ready);

    // Now we'll enter into an infinite loop until we get the data we receive
    // or if there's an error
    for (;;)
      {
      // We can't do anything until PA is ready, so just iterate the mainloop
      // and continue
      if(!ret->pa_ready)
        {
        pa_mainloop_iterate(ret->pa_ml, 1, NULL);
        continue;
        }
      // We couldn't get a connection to the server, so exit out
      else
        break;
      }
    }

  if(ret->pa_ready == 1)
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
  
#endif

#ifdef HAVE_LINUX_VIDEODEV2_H
  //  ret->v4l2_fd = inotify_init1(IN_NONBLOCK);
  //  ret->v4l2_wd = inotify_add_watch(ret->v4l2_fd, "/dev", IN_CREATE | IN_DELETE);

  ret->udev = udev_new();
  ret->udev_mon = udev_monitor_new_from_netlink(ret->udev, "udev");
  udev_monitor_filter_add_match_subsystem_devtype(ret->udev_mon, "video4linux", NULL);
  udev_monitor_enable_receiving(ret->udev_mon);
  
#endif
  
  return ret;
  
  }

int bg_recording_device_registry_update(bg_recording_device_registry_t * reg)
  {
  int result = 0;

#ifdef HAVE_LINUX_VIDEODEV2_H
  struct udev_device *dev;
#endif
  
  reg->num_ops = 0;
  
#ifdef HAVE_LINUX_VIDEODEV2_H
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

  while(gavl_fd_can_read(udev_monitor_get_fd(reg->udev_mon), 0))
    {
    if((dev = udev_monitor_receive_device(reg->udev_mon)))
      {
      const char * action;
      const char * node;
      //      fprintf(stderr, "Got device\n");

      action = udev_device_get_action(dev);
      node   = udev_device_get_devnode(dev);

      if(!strcmp(action, "add"))
        {
        gavl_dictionary_t dict;
        int type = 0;
        
        //        fprintf(stderr, "Adding %s\n", node);
        gavl_dictionary_init(&dict);
        gavl_v4l2_get_device_info(node, &dict);
        
        if(gavl_dictionary_get_int(&dict, GAVL_V4L2_TYPE, &type) &&
           (type == GAVL_V4L2_DEVICE_SOURCE))
          v4l_add_device(reg, &dict);
        
        gavl_dictionary_free(&dict);
        }
      else if(!strcmp(action, "remove"))
        {
        char * uri;
        char md5[GAVL_MD5_LENGTH];

        uri = gavl_sprintf("v4l2-capture://%s%s", reg->hostname, node);
        gavl_md5_buffer_str(uri, strlen(uri), md5);
        
        del_device(reg, md5);
        free(uri);
        
        //        fprintf(stderr, "Removing %s\n", node);
        }
      
      udev_device_unref(dev);
      }
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
        reg->pa_got_initial_devs = 1;
        }
      }
    }
  
#endif
  
  return reg->num_ops;
  }

bg_msg_hub_t * bg_recording_device_registry_get_msg_hub(bg_recording_device_registry_t * reg)
  {
  return reg->hub;
  }

void bg_recording_device_registry_destroy(bg_recording_device_registry_t * reg)
  {
#ifdef HAVE_PULSEAUDIO

  if(reg->pa_op)
    pa_operation_unref(reg->pa_op);
  
  pa_context_disconnect(reg->pa_ctx);
  pa_context_unref(reg->pa_ctx);
  pa_mainloop_free(reg->pa_ml);
#endif

#ifdef HAVE_LINUX_VIDEODEV2_H
  udev_monitor_unref(reg->udev_mon);
  udev_unref(reg->udev);

#endif

  free(reg);
  }

static int find_dev(const gavl_array_t * arr, const char * id)
  {
  int i;
  const gavl_dictionary_t * dict;
  const char * str;
  
  for(i = 0; i < arr->num_entries; i++)
    {
    if((dict = gavl_value_get_dictionary(&arr->entries[i])) &&
       (str = gavl_dictionary_get_string(dict, GAVL_META_ID)) &&
       !(strcmp(str, id)))
      return i;
    }
  return -1;
  }

static int handle_message_simple(void * data, gavl_msg_t * msg)
  {
  gavl_array_t * arr = data;
  switch(msg->NS)
    {
    case GAVL_MSG_NS_GENERIC:
      switch(msg->ID)
        {
        case GAVL_MSG_RESOURCE_ADDED:
          {
          gavl_value_t add_val;
          gavl_dictionary_t * add_dict;
          const char * ctx_id = gavl_dictionary_get_string(&msg->header, GAVL_MSG_CONTEXT_ID);
          
          if(find_dev(arr, ctx_id) >= 0)
            return 1;
          
          gavl_value_init(&add_val);
          add_dict = gavl_value_set_dictionary(&add_val);
          gavl_msg_get_arg_dictionary(msg, 0, add_dict);
          gavl_dictionary_set_string(add_dict, GAVL_META_ID,
                                     ctx_id);

          //       gavl_value_dump(&add_val, 2);
              
          gavl_array_splice_val_nocopy(arr, -1, 0, &add_val);
          }
          break;
        case GAVL_MSG_RESOURCE_DELETED:
          {
          int idx;
          const char * ctx_id = gavl_dictionary_get_string(&msg->header, GAVL_MSG_CONTEXT_ID);
          if((idx = find_dev(arr, ctx_id)) < 0)
            return 1;
          gavl_array_splice_val_nocopy(arr, idx, 1, NULL);
          }
          break;
        }
    }
  return 1;
  }

gavl_array_t * bg_get_recording_devices(int timeout)
  {
  bg_msg_sink_t * sink;
  bg_recording_device_registry_t * reg;
  gavl_timer_t * timer = gavl_timer_create();
  gavl_time_t delay_time = GAVL_TIME_SCALE / 20;
  gavl_array_t * ret = gavl_array_create();

  //  gavl_timer_start(timer);
  
  reg = bg_recording_device_registry_create();

  sink = bg_msg_sink_create(handle_message_simple, ret, 1);
  bg_msg_hub_connect_sink(bg_recording_device_registry_get_msg_hub(reg), sink);
  
  while(gavl_timer_get(timer) / 1000 < timeout)
    {
    if(!bg_recording_device_registry_update(reg))
      gavl_time_delay(&delay_time);
    }
  
  bg_msg_hub_disconnect_sink(bg_recording_device_registry_get_msg_hub(reg), sink);
  bg_msg_sink_destroy(sink);

  bg_recording_device_registry_destroy(reg);
  gavl_timer_destroy(timer);
  return ret;
  }

void bg_list_recording_devices(int timeout)
  {
  int i;
  const char * klass;
  const char * label;
  const char * uri;
  const gavl_dictionary_t * dict;
  
  gavl_array_t * arr = bg_get_recording_devices(timeout);

  if(!arr)
    return;
  
  for(i = 0; i < arr->num_entries; i++)
    {
    dict = gavl_value_get_dictionary(&arr->entries[i]);
    klass = gavl_dictionary_get_string(dict, GAVL_META_MEDIA_CLASS);
    uri   = gavl_dictionary_get_string(dict, GAVL_META_URI);
    label = gavl_dictionary_get_string(dict, GAVL_META_LABEL);

    if(!strcmp(klass, GAVL_META_MEDIA_CLASS_AUDIO_RECORDER))
      printf("# Audio source: %s\n", label);
    else if(!strcmp(klass, GAVL_META_MEDIA_CLASS_VIDEO_RECORDER))
      printf("# Video source: %s\n", label);
    printf("%s\n", uri);
    }
  
  gavl_array_destroy(arr);
  }

void bg_opt_list_recording_sources(void * data, int * argc,
                                   char *** _argv, int arg)
  {
  bg_list_recording_devices(1000);
  }

