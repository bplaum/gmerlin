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



#ifndef BG_SUBTITLE_H_INCLUDED
#define BG_SUBTITLE_H_INCLUDED


typedef struct bg_subtitle_handler_s bg_subtitle_handler_t;

bg_subtitle_handler_t * bg_subtitle_handler_create(void);
void bg_subtitle_handler_destroy(bg_subtitle_handler_t *);

void bg_subtitle_handler_init(bg_subtitle_handler_t * h,
                              gavl_video_format_t * video_format,
                              gavl_video_source_t * src,
                              gavl_video_sink_t * sink);

void bg_subtitle_handler_update(bg_subtitle_handler_t *,
                                int64_t frame_time);

void bg_subtitle_handler_reset(bg_subtitle_handler_t *);


#endif // BG_SUBTITLE_H_INCLUDED


