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

#ifndef BG_GRAB_H_INCLUDED
#define BG_GRAB_H_INCLUDED

#include <gavl/gavl.h>
#include <gavl/connectors.h>


typedef struct bg_x11_grab_window_s bg_x11_grab_window_t;

const bg_parameter_info_t *
bg_x11_grab_window_get_parameters(bg_x11_grab_window_t * win);

void bg_x11_grab_window_set_parameter(void * data, const char * name,
                                      const gavl_value_t * val);

int bg_x11_grab_window_get_parameter(void * data, const char * name,
                                     gavl_value_t * val);


bg_x11_grab_window_t * bg_x11_grab_window_create(void);
void bg_x11_grab_window_destroy(bg_x11_grab_window_t *);

int bg_x11_grab_window_init(bg_x11_grab_window_t *, gavl_video_format_t * format);

gavl_source_status_t
bg_x11_grab_window_grab(void *, gavl_video_frame_t ** frame);

void bg_x11_grab_window_close(bg_x11_grab_window_t * win);


#endif // BG_GRAB_H_INCLUDED
