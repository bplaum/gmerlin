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



#include <string.h>


#include <config.h>
#include <gmerlin/plugin.h>
#include <gmerlin/translation.h>
#include <gavl/keycodes.h>
#include <gmerlin/utils.h>
#include <gmerlin/log.h>
#define LOG_DOMAIN "ov_x11"

#include <x11/x11.h>



static void close_x11(void * data);

typedef struct
  {
  bg_x11_window_t * win;
  int window_created;

  int is_open;
  
  bg_parameter_info_t * parameters;
  char * window_id;
  

  /* Width/Height of the window */
  int window_width, window_height;

  /* Currently played video format */
  gavl_video_format_t video_format;
  
  /* Drawing coords */
  gavl_rectangle_f_t src_rect_f;
  gavl_rectangle_i_t dst_rect;
  
  /* Format with updated window size */
  gavl_video_format_t window_format;
  
  /* Still image stuff */
  
  /* Accelerator map */

  bg_accelerator_map_t * accel_map;
  
  } x11_t;

/* Utility functions */

#define PADD_IMAGE_SIZE(s) s = ((s + 15) / 16) * 16

static void ensure_window(x11_t * priv)
  {
  if(!priv->win)
    priv->win = bg_x11_window_create(priv->window_id, priv->accel_map);
  }

static void ensure_window_realized(x11_t * priv)
  {
  ensure_window(priv);
  if(!priv->window_created)
    {
    bg_x11_window_realize(priv->win);
    priv->window_created = 1;
    }
  }

static void * create_x11()
  {
  x11_t * priv;
  
  priv = calloc(1, sizeof(x11_t));
  priv->accel_map = bg_accelerator_map_create();
  
  return priv;
  }

static void destroy_x11(void * data)
  {
  x11_t * priv = data;

  close_x11(data);
  
  if(priv->parameters)
    bg_parameter_info_destroy_array(priv->parameters);

  if(priv->win)
    bg_x11_window_destroy(priv->win);
  
  if(priv->window_id)
    free(priv->window_id);
  
  bg_accelerator_map_destroy(priv->accel_map);
  free(priv);
  }

static void create_parameters(x11_t * priv)
  {
  bg_parameter_info_t const * parameters[2];

  ensure_window(priv);
  parameters[0] = bg_x11_window_get_parameters(priv->win);
  parameters[1] = NULL;

  priv->parameters = bg_parameter_info_concat_arrays(parameters);
  }

static const bg_parameter_info_t * get_parameters_x11(void * data)
  {
  x11_t * priv = data;
  if(!priv->parameters)
    create_parameters(priv);
  return priv->parameters;
  }

static void set_parameter_x11(void * data,
                              const char * name,
                              const gavl_value_t * val)
  {
  x11_t * priv = data;
  ensure_window(priv);
  bg_x11_window_set_parameter(priv->win, name, val);
#if 0
  fprintf(stderr, "set parameter %s\n", name);
  if(val)
    gavl_value_dump(val, 2);
  fprintf(stderr, "\n");
#endif
  }

static void set_accel_map_x11(void * data, const bg_accelerator_map_t * accel_map)
  {
  x11_t * priv = data;
  bg_accelerator_map_append_array(priv->accel_map, bg_accelerator_map_get_accels(accel_map));
  }

static int open_x11(void * data, const char * uri, gavl_video_format_t * format)
  {
  x11_t * priv = data;
  int result;
  ensure_window_realized(priv);
  
  result = bg_x11_window_open_video(priv->win, format);
  gavl_video_format_copy(&priv->video_format, format);
  gavl_video_format_copy(&priv->window_format, format);

  /* FIXME: Here, we assume square x11 pixels */
  priv->window_format.pixel_width = 1;
  priv->window_format.pixel_height = 1;
  
  priv->is_open = 1;
  
  
  return result;
  }

static gavl_video_sink_t * get_sink_x11(void * data)
  {
  x11_t * priv = data;
  return bg_x11_window_get_sink(priv->win);
  }

static gavl_video_sink_t *
add_overlay_stream_x11(void * data, gavl_video_format_t * format)
  {
  x11_t * priv = data;
  return bg_x11_window_add_overlay_stream(priv->win, format);
  }

static void handle_events_x11(void * data)
  {
  x11_t * priv = data;
  bg_x11_window_handle_events(priv->win, 0);

  /* Check redraw */
  bg_x11_window_check_redraw(priv->win);
  }

static void close_x11(void * data)
  {
  x11_t * priv = data;
  if(priv->is_open)
    {
    priv->is_open = 0;
    bg_x11_window_close_video(priv->win);
    }
  }


static bg_controllable_t * get_controllable_x11(void * data)
  {
  x11_t * priv = data;
  ensure_window(priv);
  return bg_x11_window_get_controllable(priv->win);
  }

static char const * const protocols = "x11-sink";

static const char * get_protocols_x11(void * priv)
  {
  return protocols;
  }


const bg_ov_plugin_t the_plugin =
  {
    .common =
    {
      BG_LOCALE,
      .name =          "ov_x11",
      .long_name =     TRS("X11"),
      .description =   TRS("X11 display driver with support for XVideo, XImage and OpenGL. Shared memory (XShm) is used where available."),
      .type =          BG_PLUGIN_OUTPUT_VIDEO,
      .flags =         BG_PLUGIN_EMBED_WINDOW | BG_PLUGIN_OV_STILL,
      .priority =      BG_PLUGIN_PRIORITY_MAX,
      .create =        create_x11,
      .destroy =       destroy_x11,

      .get_parameters   = get_parameters_x11,
      .set_parameter    = set_parameter_x11,
      .get_controllable = get_controllable_x11,
      .get_protocols = get_protocols_x11,
    },
    .set_accel_map      = set_accel_map_x11,
    
    .open               = open_x11,
    .get_sink           = get_sink_x11,
    
    .add_overlay_stream = add_overlay_stream_x11,

    .handle_events      = handle_events_x11,
    .close              = close_x11,
    
  };

/* Include this into all plugin modules exactly once
   to let the plugin loader obtain the API version */
BG_GET_PLUGIN_API_VERSION;
