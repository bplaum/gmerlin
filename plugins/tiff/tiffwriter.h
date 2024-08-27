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



typedef struct bg_tiff_writer_s bg_tiff_writer_t;

bg_tiff_writer_t * bg_tiff_writer_create();

const bg_parameter_info_t * bg_tiff_writer_get_parameters(void);

void bg_tiff_writer_destroy(bg_tiff_writer_t*);

void bg_tiff_writer_set_parameter(bg_tiff_writer_t*, const char * name,
                                  const gavl_value_t * val);

int bg_tiff_writer_write_header(bg_tiff_writer_t * tiff, const char * filename,
                                gavl_packet_t * p,
                                gavl_video_format_t * format,
                                const gavl_dictionary_t * metadata);

int bg_tiff_writer_write_image(bg_tiff_writer_t * tiff,
                               gavl_video_frame_t * frame);

