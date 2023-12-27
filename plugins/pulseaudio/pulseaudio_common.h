/*****************************************************************
 * gmerlin - a general purpose multimedia framework and applications
 *
 * Copyright (c) 2001 - 2012 Members of the Gmerlin project
 * gmerlin-general@lists.sourceforge.net
 * http://gmerlin.sourceforge.net
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

#include <config.h>
#include <gmerlin/plugin.h>
#include <gmerlin/translation.h>

#include <pulse/simple.h>
#include <pulse/error.h>

typedef struct
  {
  struct pa_simple *pa;
  int record;
  gavl_audio_format_t format;
  int block_align;

  int num_channels;
  int bytes_per_sample;
  int samplerate;

  bg_controllable_t ctrl;
  
  } bg_pa_common_t;

typedef struct
  {
  /* Must be first */
  bg_pa_common_t com;
  gavl_dictionary_t mi;
  bg_media_source_t source;

  int64_t timestamp;
  } bg_pa_recorder_t;

typedef struct
  {
  /* Must be first */
  bg_pa_common_t com;
  gavl_audio_sink_t * sink;  // Playback
  char *server;
  char *dev;
  } bg_pa_output_t;

int bg_pa_open(bg_pa_common_t * p, char * server, char * dev, int record);

void bg_pa_close_common(bg_pa_common_t * priv);

bg_controllable_t * bg_pa_get_controllable(void * p);
