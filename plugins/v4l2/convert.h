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



typedef struct bg_v4l2_convert_s bg_v4l2_convert_t;

bg_v4l2_convert_t * bg_v4l2_convert_create(int fd, uint32_t * v4l_fmt,
                                           gavl_pixelformat_t * gavl_fmt,
                                           int width, int height);

void bg_v4l2_convert_convert(bg_v4l2_convert_t *,
                             uint8_t * data, int size,
                             gavl_video_frame_t ** frame);

void bg_v4l2_convert_destroy(bg_v4l2_convert_t *);

