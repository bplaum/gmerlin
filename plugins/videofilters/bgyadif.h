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



typedef struct bg_yadif_s bg_yadif_t;

bg_yadif_t * bg_yadif_create();

void bg_yadif_destroy(bg_yadif_t * d);



void bg_yadif_init(bg_yadif_t * di,
                   gavl_video_format_t * format,
                   gavl_video_format_t * out_format,
                   gavl_video_options_t * opt, int mode);

gavl_source_status_t
bg_yadif_read(void * priv, gavl_video_frame_t ** frame, gavl_video_source_t * src);

void bg_yadif_reset(bg_yadif_t * di);
