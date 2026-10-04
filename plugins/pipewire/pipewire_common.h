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

#include <gavl/gavl.h>
#include <gavl/value.h>
#include <pipewire/pipewire.h>

#include <spa/param/audio/format-utils.h>

#define PIPEWIRE_SOURCE_PROTOCOL  "pipewire-source"
#define PIPEWIRE_SINK_PROTOCOL    "pipewire-sink"
#define PIPEWIRE_MONITOR_PROTOCOL "pipewire-monitor"

int bg_pipewire_query_device(const char * name, gavl_audio_format_t * afmt,
                             int * max_buffer_size, gavl_dictionary_t * m);

int bg_pipewire_sample_format_from_spa(uint32_t id, gavl_audio_format_t * fmt);

int bg_pipewire_channel_positions_from_spa(uint32_t * ids, int num_ids,
                                           gavl_audio_format_t * fmt);

void bg_pipewire_audio_format_to_spa(const gavl_audio_format_t * fmt,
                                     struct spa_audio_info_raw * spa);
