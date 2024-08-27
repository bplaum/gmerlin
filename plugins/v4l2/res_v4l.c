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
// #include <string.h>

#include <gavl/hw_v4l2.h>
#include <gavl/log.h>
#define LOG_DOMAIN "res_v4l2"

#include <gmerlin/translation.h>
#include <gmerlin/plugin.h>


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

static void * create_v4l2()
  {
  v4l2_t * ret;

  ret = calloc(1, sizeof(*ret));
  gethostname(ret->hostname, HOST_NAME_MAX+1);

  
  } 

static void destroy_v4l2(void * priv)
  {
  v4l2_t * reg = priv;
  }

static int update_v4l2(void * priv)
  {
  int ret = 0;
  v4l2_t * reg = priv;

  
  return ret;
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
