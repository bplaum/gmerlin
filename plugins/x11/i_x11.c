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



#include <config.h>
#include <gmerlin/translation.h>
#include <gmerlin/plugin.h>

#include "grab.h"

typedef struct
  {
  bg_x11_grab_window_t * win;
  gavl_video_source_t * vsrc;
  bg_controllable_t ctrl;
  gavl_dictionary_t mi;
  bg_media_source_t src;
  gavl_dictionary_t * s;
  } x11_t;

static gavl_dictionary_t * get_media_info_x11(void * priv)
  {
  x11_t * x11 = priv;
  return &x11->mi;
  }

static bg_media_source_t * get_src_x11(void * priv)
  {
  x11_t * x11;
  x11 = priv;
  return &x11->src;
  }


static int handle_cmd(void * data, gavl_msg_t * msg)
  {
  //  x11_t * x11 = data;
  
  switch(msg->NS)
    {
    case GAVL_MSG_NS_SRC:
      switch(msg->ID)
        {
        case GAVL_CMD_SRC_START:
          {
          }
          break;
        case GAVL_CMD_SRC_PAUSE:
          break;
        case GAVL_CMD_SRC_RESUME:
          break;
        }
      break;
    }
  return 1;
  }

static bg_controllable_t * get_controllable_x11(void * priv)
  {
  x11_t * x11;
  x11 = priv;
  return &x11->ctrl;
  }

static void * create_x11()
  {
  x11_t * x11;
  x11 = calloc(1, sizeof(*x11));

  x11->win = bg_x11_grab_window_create();

  bg_controllable_init(&x11->ctrl,
                       bg_msg_sink_create(handle_cmd, x11, 1),
                       bg_msg_hub_create(1));

  
  return x11;
  }


static const bg_parameter_info_t * get_parameters_x11(void * priv)
  {
  x11_t * x11 = priv;
  return bg_x11_grab_window_get_parameters(x11->win);
  }

static void set_parameter_x11(void * priv, const char * name,
                              const gavl_value_t * val)
  {
  x11_t * x11 = priv;
  bg_x11_grab_window_set_parameter(x11->win, name, val);
  }

static int get_parameter_x11(void * priv, const char * name,
                             gavl_value_t * val)
  {
  x11_t * x11 = priv;
  return bg_x11_grab_window_get_parameter(x11->win, name, val);
  }

static int open_x11(void * priv, const char * location)
  {
  x11_t * x11 = priv;

  gavl_dictionary_t * t;
  //  gavl_dictionary_t * m;
  bg_media_source_stream_t * st;
  gavl_video_format_t * fmt;
  
  t = gavl_append_track(&x11->mi, NULL);
  //  m = gavl_track_get_metadata_nc(t);

  x11->s = gavl_track_append_video_stream(t);

  fmt = gavl_stream_get_video_format_nc(x11->s);

  /* Move to start () */
  if(!bg_x11_grab_window_init(x11->win, fmt))
    return 0;
  x11->vsrc = gavl_video_source_create(bg_x11_grab_window_grab, x11->win,
                                       GAVL_SOURCE_SRC_ALLOC, fmt);

  bg_media_source_set_from_track(&x11->src, t);
  st = bg_media_source_get_video_stream(&x11->src, 0);
  st->vsrc = x11->vsrc;
  
  return 1;
  }

static void close_x11(void * priv)
  {
  x11_t * x11 = priv;
  bg_x11_grab_window_close(x11->win);
  gavl_video_source_destroy(x11->vsrc);
  x11->vsrc = NULL;
  }

static void destroy_x11(void * priv)
  {
  x11_t * x11 = priv;

  if(x11->vsrc)
    close_x11(priv);

  bg_controllable_cleanup(&x11->ctrl);
  gavl_dictionary_free(&x11->mi);
  bg_media_source_cleanup(&x11->src);
  bg_x11_grab_window_destroy(x11->win);
  free(priv);  
  }


static char const * const protocols = "x11-src";

static const char * get_protocols_x11(void * priv)
  {
  return protocols;
  }


const bg_input_plugin_t the_plugin =
  {
    .common =
    {
      BG_LOCALE,
      .name =          "i_x11",
      .long_name =     TRS("X11"),
      .description =   TRS("X11 grabber"),
      .type =          BG_PLUGIN_INPUT,
      .flags =         0,
      .priority =      BG_PLUGIN_PRIORITY_MAX,
      .create =        create_x11,
      .destroy =       destroy_x11,

      .get_parameters = get_parameters_x11,
      .set_parameter =  set_parameter_x11,
      .get_parameter =  get_parameter_x11,
      .get_controllable = get_controllable_x11,
      .get_protocols = get_protocols_x11,
    },
    
    .get_media_info  = get_media_info_x11,
    .get_src       = get_src_x11,
    .open          = open_x11,
    .close         = close_x11,
  };

/* Include this into all plugin modules exactly once
   to let the plugin loader obtain the API version */
BG_GET_PLUGIN_API_VERSION;
