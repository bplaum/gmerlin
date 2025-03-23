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
#include <stdlib.h>
#include <string.h>

#include <gavl/hw_v4l2.h>
#include <gavl/log.h>
#define LOG_DOMAIN "res_v4l2"

#include <gavl/utils.h>

#include <gmerlin/translation.h>
#include <gmerlin/plugin.h>

#include "v4l2_common.h"


#include <libudev.h>

#define FLAG_HAS_INITIAL_DEVS (1<<0)

typedef struct
  {
  bg_controllable_t ctrl;

  int flags;
  struct udev *udev;
  struct udev_monitor *udev_mon;


  char hostname[HOST_NAME_MAX+1];
  } v4l2_t;

static int handle_msg(void * priv, gavl_msg_t * msg)
  {
  return 1;
  }

#if 0
static char * make_id(const char * host, const char * node)
  {
  return gavl_sprintf("v4l-%s-%s", host, node);
  }
#endif

static void * create_v4l2()
  {
  v4l2_t * ret;

  ret = calloc(1, sizeof(*ret));
  gethostname(ret->hostname, HOST_NAME_MAX+1);

  ret->udev = udev_new();
  ret->udev_mon = udev_monitor_new_from_netlink(ret->udev, "udev");
  udev_monitor_filter_add_match_subsystem_devtype(ret->udev_mon, "video4linux", NULL);
  udev_monitor_enable_receiving(ret->udev_mon);
  
  bg_controllable_init(&ret->ctrl,
                       bg_msg_sink_create(handle_msg, ret, 1),
                       bg_msg_hub_create(1));
  
  return ret;
  } 

static void destroy_v4l2(void * priv)
  {
  v4l2_t * reg = priv;

  udev_monitor_unref(reg->udev_mon);
  udev_unref(reg->udev);
  bg_controllable_cleanup(&reg->ctrl);
  
  free(reg);

  }

static void add_device(v4l2_t * reg, gavl_dictionary_t * dict)
  {
  int type;
  char * real_uri;
  const char * node;
  gavl_msg_t * msg;
  
  char id[GAVL_MD5_LENGTH];

  
  if(!gavl_dictionary_get_int(dict, GAVL_V4L2_TYPE, &type))
    return;

  //  fprintf(stderr, "Add device %d\n", type);

  if(type == GAVL_V4L2_DEVICE_LOOPBACK)
    {
    gavl_dictionary_t dict1;
    gavl_dictionary_init(&dict1);
    gavl_dictionary_copy(&dict1, dict);
    gavl_dictionary_set_int(&dict1, GAVL_V4L2_TYPE, GAVL_V4L2_DEVICE_SOURCE);
    add_device(reg, &dict1);
    
    gavl_dictionary_reset(&dict1);
    gavl_dictionary_copy(&dict1, dict);
    gavl_dictionary_set_int(&dict1, GAVL_V4L2_TYPE, GAVL_V4L2_DEVICE_SINK);
    add_device(reg, &dict1);

    gavl_dictionary_free(&dict1);
    return;
    }
  
  gavl_dictionary_set(dict, GAVL_V4L2_SRC_FORMATS, NULL);
  gavl_dictionary_set(dict, GAVL_V4L2_SINK_FORMATS, NULL);
  gavl_dictionary_set(dict, GAVL_V4L2_TYPE, NULL);
  gavl_dictionary_set(dict, GAVL_V4L2_TYPE_STRING, NULL);
  gavl_dictionary_set(dict, GAVL_V4L2_CAPABILITIES, NULL);

  node = gavl_dictionary_get_string(dict, GAVL_META_URI);
  
  switch(type)
    {
    case GAVL_V4L2_DEVICE_SOURCE:
      real_uri = gavl_sprintf(V4L2_PROTOCOL_CAPTURE"://%s%s", reg->hostname, node);
      gavl_dictionary_set_string(dict, GAVL_META_CLASS,
                                 GAVL_META_CLASS_VIDEO_RECORDER);

      break;
    case GAVL_V4L2_DEVICE_SINK:
      real_uri = gavl_sprintf(V4L2_PROTOCOL_OUTPUT"://%s%s", reg->hostname, node);
      gavl_dictionary_set_string(dict, GAVL_META_CLASS,
                                 GAVL_META_CLASS_SINK_VIDEO);
      break;
    default:
      return;
    }

  // id = make_id(reg->hostname, node);

  gavl_md5_buffer_str(real_uri, strlen(real_uri), id);
  
  gavl_dictionary_set_string_nocopy(dict, GAVL_META_URI, real_uri);

  msg = bg_msg_sink_get(reg->ctrl.evt_sink);
  
  gavl_msg_set_id_ns(msg, GAVL_MSG_RESOURCE_ADDED, GAVL_MSG_NS_GENERIC);
  
  gavl_dictionary_set_string(&msg->header, GAVL_MSG_CONTEXT_ID, id);
  
  gavl_msg_set_arg_dictionary(msg, 0, dict);
  
  bg_msg_sink_put(reg->ctrl.evt_sink);
  }

static void del_device(v4l2_t * reg, const char *id)
  {
  gavl_msg_t * msg;
  
  msg = bg_msg_sink_get(reg->ctrl.evt_sink);
  gavl_msg_set_id_ns(msg, GAVL_MSG_RESOURCE_DELETED, GAVL_MSG_NS_GENERIC);
  gavl_dictionary_set_string(&msg->header, GAVL_MSG_CONTEXT_ID, id);
  bg_msg_sink_put(reg->ctrl.evt_sink);
  }

static int update_v4l2(void * priv)
  {
  int ret = 0;
  v4l2_t * reg = priv;

  if(!(reg->flags & FLAG_HAS_INITIAL_DEVS))
    {
    gavl_array_t v4l2_devs;
    int i;
    
    gavl_array_init(&v4l2_devs);
    gavl_v4l2_devices_scan_by_type(GAVL_V4L2_DEVICE_SOURCE | GAVL_V4L2_DEVICE_SINK | GAVL_V4L2_DEVICE_LOOPBACK,
                                   &v4l2_devs);
    
    for(i = 0; i < v4l2_devs.num_entries; i++)
      add_device(reg, gavl_value_get_dictionary_nc(&v4l2_devs.entries[i]));
    
    gavl_array_free(&v4l2_devs);
    reg->flags |= FLAG_HAS_INITIAL_DEVS;
    }
  
  while(gavl_fd_can_read(udev_monitor_get_fd(reg->udev_mon), 0))
    {
    struct udev_device *dev;
    
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
           (type & (GAVL_V4L2_DEVICE_SOURCE|GAVL_V4L2_DEVICE_SINK|GAVL_V4L2_DEVICE_LOOPBACK)))
          add_device(reg, &dict);
        
        gavl_dictionary_free(&dict);
        }
      else if(!strcmp(action, "remove"))
        {
        char * uri;
        char md5[GAVL_MD5_LENGTH];
        
        uri = gavl_sprintf(V4L2_PROTOCOL_CAPTURE"://%s%s", reg->hostname, node);
        gavl_md5_buffer_str(uri, strlen(uri), md5);

        del_device(reg, md5);
        free(uri);

        uri = gavl_sprintf(V4L2_PROTOCOL_OUTPUT"://%s%s", reg->hostname, node);
        gavl_md5_buffer_str(uri, strlen(uri), md5);

        del_device(reg, md5);
        free(uri);
        //        fprintf(stderr, "Removing %s\n", node);
        }
      
      udev_device_unref(dev);
      }
    }
 
  return ret;
  }

static bg_controllable_t * get_controllable_v4l2(void * priv)
  {
  v4l2_t * p = priv;
  return &p->ctrl;
  }


bg_controllable_plugin_t the_plugin =
  {
    .common =
    {
      BG_LOCALE,
      .name =      "res_v4l2",
      .long_name = TRS("Video device manager"),
      .description = TRS("Manages v4l2 sources and sinks"),
      .type =     BG_PLUGIN_RESOURCE_DETECTOR,
      .flags =    0,
      .create =   create_v4l2,
      .destroy =   destroy_v4l2,
      .get_controllable =   get_controllable_v4l2,
      .priority =         1,
    },
    .update = update_v4l2,

  };

/* Include this into all plugin modules exactly once
   to let the plugin loader obtain the API version */
BG_GET_PLUGIN_API_VERSION;
