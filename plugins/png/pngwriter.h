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



#include <png.h>

#define BITS_AUTO  0
#define BITS_8     8
#define BITS_16   16

typedef struct
  {
  png_structp png_ptr;
  png_infop   info_ptr;
  int transform_flags;
  FILE * output;
  int bit_mode;
  int compression_level;
  gavl_video_format_t format;

  png_text * text;
  int num_text;
  int dont_force_extension;

  int color_type;
  int have_format;
  int bits;
  
  gavl_packet_t * p;
  
  } bg_pngwriter_t;

void bg_pngwriter_adjust_format(bg_pngwriter_t * png,
                               gavl_video_format_t * format);


int bg_pngwriter_write_header(void * priv, const char * filename,
                              gavl_packet_t * p,
                              gavl_video_format_t * format,
                              const gavl_dictionary_t * metadata);

int bg_pngwriter_write_image(void * priv, gavl_video_frame_t * frame);

void bg_pngwriter_set_parameter(void * p, const char * name,
                                const gavl_value_t * val);


