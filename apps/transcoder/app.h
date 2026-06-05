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


/*
 * app.h - Application-level callbacks (startup / activate / shutdown).
 */

#pragma once

#include <gtk/gtk.h>
#include <gmerlin/plugin.h>
#include <gmerlin/cfgctx.h>
#include "transcode.h"

#define TRANSCODER_TRACKS_MIMETYPE "application/gmerlin-transcoder"

#define PREFS_OUTPUT       "prefs-output"
#define PREFS_ENCODER      "prefs-encoder"
#define PREFS_AUDIO        "prefs-audio"
#define PREFS_VIDEO        "prefs-video"
#define PREFS_TEXT         "prefs-text"
#define PREFS_OVERLAY      "prefs-overlay"
#define PREFS_AUDIOFILTERS "prefs-af"
#define PREFS_VIDEOFILTERS "prefs-vf"
#define PREFS_PREFIX       "prefs-"

#define TRACK_PREFIX       "track-"

#define TRACK_ENCODER      "track-encoder"
#define TRACK_METADATA     "track-metadata"
#define TRACK_MASSTAG      "track-masstag"
#define TRACK_RENAME       "track-rename"
#define TRACK_AUTONUMBER   "track-autonumber"
#define TRACK_YEARS        "track-years"

/*
 *  stream-audio:0:general
 *  stream-audio:0:encoder
 *  stream-audio:0:filter
 */

#define STREAM_PREFIX      "stream-"

#define STREAM_AUDIO        "stream-audio"
#define STREAM_VIDEO        "stream-video"
#define STREAM_TEXT         "stream-text"
#define STREAM_OVERLAY      "stream-overlay"
#define STREAM_FILTER      "filter"
#define STREAM_ENCODER     "encoder"

extern const gavl_parameter_info_t metadata_song_parameters[];
extern const gavl_parameter_info_t metadata_movie_parameters[];
extern const gavl_parameter_info_t metadata_generic_parameters[];

extern const gavl_parameter_info_t metadata_bulk_parameters[];

const gavl_parameter_info_t * get_metadata_parameters(const gavl_dictionary_t * track);


typedef struct
  {
  gavl_array_t * tracks;

  GtkSizeGroup * sg_type;
  GtkSizeGroup * sg_duration;
  GtkWidget * listbox;

  GtkWidget * status_left;
  GtkWidget * status_right;
  GtkWidget * progress;
  
  bg_msg_sink_t * dlg_sink;
  bg_msg_sink_t * transcoder_sink;
  
  /* Application state */
  gavl_dictionary_t state;

  gavl_dictionary_t cur;
  
  transcoder_t * t;
  guint idle_tag;
  } app_data_t;

/* Called once before the first window is shown. Load resources, menus, etc. */
void app_startup(GtkApplication *app, gpointer user_data);

/* Called every time the application is activated (e.g. launched without args). */
void app_activate(GtkApplication *app, gpointer user_data);

/* Called when the last window is closed and the main loop is about to exit. */
void app_shutdown(GtkApplication *app, gpointer user_data);

app_data_t * create_app_data(void);
void destroy_app_data(void * priv);


void register_window_actions(GtkWidget * w);

void show_tracks_init(app_data_t * ad);
