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


#include <gmerlin/plugin.h>

typedef struct decoder_s decoder_t;

struct decoder_s
  {
  void * priv;

  gavl_packet_source_t * psrc;
  gavl_video_source_t * vsrc; /* Owned */

  void (*decode)(decoder_t * dec, gavl_packet_t * p, gavl_video_frame_t * frame);
  void (*cleanup)(decoder_t * dec);
  
  gavl_video_format_t * fmt;
  gavl_video_frame_t * tmp_frame;
  };

/* Helper fuction */

void init_p207(decoder_t * dec);

