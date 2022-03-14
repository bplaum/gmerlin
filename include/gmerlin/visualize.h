/*****************************************************************
 * gmerlin - a general purpose multimedia framework and applications
 *
 * Copyright (c) 2001 - 2012 Members of the Gmerlin project
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

#ifndef BG_VISUALIZE_H_INCLUDED
#define BG_VISUALIZE_H_INCLUDED

#include <gmerlin/ov.h>
#include <gmerlin/osd.h>
#include <gmerlin/player.h>


/* We use the singleton plugin registry */

/*
   The visualizer owns the visualization plugin and renders into the
   video output handle.

   All OpenGL rendering is done via the OpenGL 2.0 framebuffer object and
   the result is passed in a texture ID.

   The actual displaying is then done by drawing a quad with that texture
   
*/

typedef struct bg_visualizer_s bg_visualizer_t;

const bg_parameter_info_t * bg_visualizer_get_parameters(bg_visualizer_t * visualizer);


bg_visualizer_t * bg_visualizer_create(bg_player_t * p);
void bg_visualizer_destroy(bg_visualizer_t * vis);

void bg_visualizer_start(bg_visualizer_t * vis, bg_ov_t * ov);
void bg_visualizer_stop(bg_visualizer_t * vis);

void bg_visualizer_set_parameter(void * data, // bg_visualizer_t *
                                 const char * name,
                                 const gavl_value_t * val);

int bg_visualizer_init(bg_visualizer_t * v,
                       const gavl_audio_format_t * fmt);

void bg_visualizer_update(bg_visualizer_t * v, gavl_audio_frame_t * frame);
  
  
/* Stuff used by the plugins */

#define BG_VISUALIZE_PLUGIN_PARAM_SIZE(def_w, def_h)                  \
  {                                                                   \
  .name =        "width",                                             \
  .long_name =   TRS("Width"),                                        \
  .type =        BG_PARAMETER_INT,                                    \
  .val_min =     GAVL_VALUE_INIT_INT(0),                              \
  .val_max =     GAVL_VALUE_INIT_INT(32768),                          \
  .val_default = GAVL_VALUE_INIT_INT(def_w),                          \
  .help_string = TRS("Image with. Set to 0 to take global value. Needs restart of playback,"),   \
  },                                                                  \
  {                                                                   \
  .name =       "height",                                             \
  .long_name =  TRS("Height"),                                        \
  .type =       BG_PARAMETER_INT,                                     \
  .val_min =    GAVL_VALUE_INIT_INT(0),                               \
  .val_max =    GAVL_VALUE_INIT_INT(32768),                           \
    .val_default = GAVL_VALUE_INIT_INT(def_h),                        \
  .help_string = TRS("Image height. Set to 0 to take global value. Needs restart of playback,"), \
  }

#define BG_VISUALIZE_PLUGIN_PARAM_FRAMERATE(def)        \
  {                                         \
  .name =       "framerate",                                        \
  .long_name =  TRS("Framerate"),                                   \
  .type =       BG_PARAMETER_FLOAT,                                 \
  .val_min =    GAVL_VALUE_INIT_FLOAT(0.1),                         \
  .val_max =    GAVL_VALUE_INIT_FLOAT(240.0),                       \
  .val_default = GAVL_VALUE_INIT_FLOAT(def),                       \
  .help_string = TRS("Framerate. For values < 1.0 the global default will be used"), \
  }

int bg_visualize_set_format_parameter(gavl_video_format_t * fmt, const char * name,
                                      const gavl_value_t * val);

void bg_visualize_set_format(gavl_video_format_t * fmt,
                             const gavl_video_format_t * default_fmt);

void bg_visualizer_set_plugin(bg_visualizer_t * v, int plugin);

void bg_visualizer_pause(bg_visualizer_t * v);

#endif // BG_VISUALIZE_H_INCLUDED

