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


#pragma once

#include <gmerlin/pluginregistry.h>
#include <gmerlin/mediaconnector.h>
#include <gmerlin/bgmsg.h>


/*

 */

#define TRANSCODER_FLAG_DELETE (1<<0)

typedef struct
  {
  bg_plugin_handle_t * input;
  bg_plugin_handle_t * encoder;
  bg_media_source_t * src;
  bg_media_source_t src_filter;
  bg_media_source_t src_encoder;
  gavl_dictionary_t track;

  bg_controllable_t ctrl;
  pthread_t th;

  gavl_timer_t * timer;
  gavl_time_t last_progress_time;
  gavl_time_t duration;
  
  char * progress_msg;

  int flags;
  } transcoder_t;

transcoder_t * transcoder_create(const gavl_dictionary_t * track,
                                 bg_msg_sink_t * msg_sink);

void transcoder_destroy(transcoder_t * t);

