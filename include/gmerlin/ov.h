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



#ifndef BG_OV_H_INCLUDED
#define BG_OV_H_INCLUDED

#include <gmerlin/plugin.h>
#include <gmerlin/pluginregistry.h>
#include <gmerlin/state.h>

typedef struct bg_ov_s bg_ov_t;

bg_ov_t * bg_ov_create(bg_plugin_handle_t * h);

bg_plugin_handle_t * bg_ov_get_plugin(bg_ov_t *);

void bg_ov_destroy(bg_ov_t * ov);

void bg_ov_set_window(bg_ov_t * ov, const char * window_id);
  
// const char * bg_ov_get_window(bg_ov_t * ov);


void bg_ov_set_window_title(bg_ov_t * ov, const char * title);

int  bg_ov_open(bg_ov_t * ov, gavl_video_format_t * format, int keep_aspect);

gavl_video_sink_t * bg_ov_get_sink(bg_ov_t * ov);

  
gavl_video_sink_t *
bg_ov_add_overlay_stream(bg_ov_t * ov, gavl_video_format_t * format);

void bg_ov_handle_events(bg_ov_t * ov);

void bg_ov_update_aspect(bg_ov_t * ov, int pixel_width, int pixel_height);

  
void bg_ov_close(bg_ov_t * ov);

void bg_ov_show_window(bg_ov_t * ov, int show);
void bg_ov_set_fullscreen(bg_ov_t * ov, int fullscreen);

bg_controllable_t * bg_ov_get_controllable(bg_ov_t * ov);

extern const bg_state_var_desc_t bg_ov_state_vars[];

#define BG_OV_SQUEEZE_MIN -1.0
#define BG_OV_SQUEEZE_MAX 1.0
#define BG_OV_SQUEEZE_DELTA 0.04

#define BG_OV_ZOOM_MIN 20.0
#define BG_OV_ZOOM_MAX 180.0
#define BG_OV_ZOOM_DELTA 2.0


#endif // BG_OV_H_INCLUDED

