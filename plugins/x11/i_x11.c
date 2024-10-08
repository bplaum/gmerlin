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

#include <x11/x11.h>

typedef struct
  {
  bg_x11_grab_window_t * win;
  gavl_video_source_t * src;
  } x11_t;

static void * create_x11()
  {
  x11_t * x11;
  x11 = calloc(1, sizeof(*x11));

  x11->win = bg_x11_grab_window_create();
  
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

static int open_x11(void * priv,
                    gavl_audio_format_t * audio_format,
                    gavl_video_format_t * format, gavl_dictionary_t * m)
  {
  x11_t * x11 = priv;
  if(!bg_x11_grab_window_init(x11->win, format))
    return 0;
  x11->src = gavl_video_source_create(bg_x11_grab_window_grab, x11->win,
                                      GAVL_SOURCE_SRC_ALLOC, format);
  return 1;
  }

static void close_x11(void * priv)
  {
  x11_t * x11 = priv;
  bg_x11_grab_window_close(x11->win);
  gavl_video_source_destroy(x11->src);
  x11->src = NULL;
  }

static void destroy_x11(void * priv)
  {
  x11_t * x11 = priv;

  if(x11->src)
    close_x11(priv);

  bg_x11_grab_window_destroy(x11->win);
  free(priv);  
  }



static gavl_video_source_t * get_video_source_x11(void * priv)
  {
  x11_t * x11 = priv;
  return x11->src;
  }

const bg_recorder_plugin_t the_plugin =
  {
    .common =
    {
      BG_LOCALE,
      .name =          "i_x11",
      .long_name =     TRS("X11"),
      .description =   TRS("X11 grabber"),
      .type =          BG_PLUGIN_RECORDER_VIDEO,
      .flags =         0,
      .priority =      BG_PLUGIN_PRIORITY_MAX-1,
      .create =        create_x11,
      .destroy =       destroy_x11,

      .get_parameters = get_parameters_x11,
      .set_parameter =  set_parameter_x11,
      .get_parameter =  get_parameter_x11,
    },
    
    .open =       open_x11,
    .close =      close_x11,
    .get_video_source = get_video_source_x11,
  };

/* Include this into all plugin modules exactly once
   to let the plugin loader obtain the API version */
BG_GET_PLUGIN_API_VERSION;
