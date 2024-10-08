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



#ifndef BG_TEXTRENDERER_H_INCLUDED
#define BG_TEXTRENDERER_H_INCLUDED

#include <gavl/connectors.h>

typedef struct bg_text_renderer_s bg_text_renderer_t;

bg_text_renderer_t * bg_text_renderer_create();
void bg_text_renderer_destroy(bg_text_renderer_t *);

const bg_parameter_info_t * bg_text_renderer_get_parameters(void);

void bg_text_renderer_set_parameter(void * data, const char * name,
                                    const gavl_value_t * val);

/* Frame format can be NULL */

void bg_text_renderer_init(bg_text_renderer_t*,
                           const gavl_video_format_t * frame_format,
                           gavl_video_format_t * overlay_format);

void bg_text_renderer_get_frame_format(bg_text_renderer_t * r,
                                       gavl_video_format_t * frame_format);


gavl_video_frame_t * bg_text_renderer_render(bg_text_renderer_t*,
                                             const char * string);

gavl_video_source_t * bg_text_renderer_connect(bg_text_renderer_t * r,
                                               gavl_packet_source_t * src,
                                               const gavl_video_format_t * frame_format,
                                               gavl_video_format_t * overlay_format);

#endif // BG_TEXTRENDERER_H_INCLUDED

