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



#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

#include <gavl/gavl.h>
#include <gavl/metadata.h>
#include <gavl/metatags.h>
#include <gavl/edl.h>

#include <gmerlin/edl.h>
#include <gmerlin/utils.h>

static void set_segment(gavl_edl_segment_t * seg,
                        const char * url, int track, int stream)
  {
  seg->speed_num = 1;
  seg->speed_den = 1;
  seg->url = gavl_strdup(url);
  seg->track = track;
  seg->stream = stream;
  }

static int64_t get_segment_duration(const bg_track_info_t * info,
                                    gavl_time_t stream_duration,
                                    int segment_scale, int stream_scale)
  {
  gavl_time_t track_duration;
  
  if(stream_duration)
    return gavl_time_rescale(segment_scale, stream_scale, stream_duration);

  gavl_dictionary_get_long(&info->metadata, GAVL_META_APPROX_DURATION, &track_duration);
  return gavl_time_rescale(GAVL_TIME_SCALE, stream_scale, track_duration);
