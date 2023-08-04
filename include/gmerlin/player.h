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

#ifndef BG_PLAYER_H_INCLUDED
#define BG_PLAYER_H_INCLUDED

#include <gmerlin/pluginregistry.h>
#include <gmerlin/msgqueue.h>
#include <gmerlin/state.h>

#include <gmerlin/cfgctx.h>


/** \defgroup player Player
 *  \brief Multimedia player
 */

#define BG_PLAYER_SEEK_STEP      10 // Seconds
#define BG_PLAYER_SEEK_FAST_STEP 120 // Seconds

#define BG_PLAYER_CFG_INPUT         0
#define BG_PLAYER_CFG_AUDIOPLUGIN   1
#define BG_PLAYER_CFG_AUDIO         2
#define BG_PLAYER_CFG_AUDIOFILTER   3
#define BG_PLAYER_CFG_VIDEOPLUGIN   4
#define BG_PLAYER_CFG_VIDEO         5
#define BG_PLAYER_CFG_VIDEOFILTER   6
#define BG_PLAYER_CFG_SUBTITLE      7
#define BG_PLAYER_CFG_OSD           8
#define BG_PLAYER_CFG_VISUALIZATION 9

#define BG_PLAYER_CFG_NUM           10

typedef struct bg_player_s bg_player_t;

extern const bg_accelerator_t bg_player_accels[];

typedef struct
  {
  int mode;

  gavl_dictionary_t * cnt; // Container
  
  int idx;
  int idx_real;

  int * shuffle_list;
  
  gavl_time_t duration_before;
  gavl_time_t duration_after;
  gavl_time_t duration;
  
  int list_changed;
  int current_changed;

  int has_next;
  
  bg_msg_sink_t * evt_sink; // Set by client
  bg_msg_hub_t * hub;

  /* We use the player state to check early, if a media stream is loadable */
  gavl_dictionary_t * application_state;
  
  } bg_player_tracklist_t;


void bg_player_tracklist_splice(bg_player_tracklist_t * l, int idx, int del, gavl_value_t * val, const char * client_id);

char * bg_player_tracklist_make_id(const char * client_id, const char * original_id);
char * bg_player_tracklist_id_from_uri(const char * client_id, const char * location);

int bg_player_tracklist_handle_message(bg_player_tracklist_t * l, gavl_msg_t * msg);

void bg_player_tracklist_free(bg_player_tracklist_t * l);
void bg_player_tracklist_init(bg_player_tracklist_t * l, bg_msg_sink_t * evt_sink);
void bg_player_tracklist_set_mode(bg_player_tracklist_t * l, int * mode);

void bg_player_tracklist_set_current_by_idx(bg_player_tracklist_t * l, int idx);
int bg_player_tracklist_set_current_by_id(bg_player_tracklist_t * l, const char * id);

void bg_player_tracklist_clear(bg_player_tracklist_t * l);

void bg_player_state_init(gavl_dictionary_t * dict, const char * label,
                          const gavl_array_t * protocols, const gavl_array_t * mimetypes);

void bg_player_state_reset(gavl_dictionary_t * dict);

const char * bg_player_track_get_uri(gavl_dictionary_t * state, const gavl_dictionary_t * track);

void bg_player_tracklist_get_times(bg_player_tracklist_t * l, gavl_time_t t, gavl_time_t * t_abs,
                                   gavl_time_t * t_rem, gavl_time_t * t_rem_abs, double * percentage);

int bg_player_tracklist_advance(bg_player_tracklist_t * l, int force);
int bg_player_tracklist_back(bg_player_tracklist_t * l);

gavl_dictionary_t *
bg_player_tracklist_get_current_track(bg_player_tracklist_t * l);

gavl_dictionary_t *
bg_player_tracklist_get_next(bg_player_tracklist_t * l);

#include <gmerlin/playermsg.h>

/* player.c */

/** \ingroup player
 *  \brief Create a player
 *  \param plugin_reg A plugin registry
 *  \returns A newly allocated player
 */

bg_player_t * bg_player_create(void);

/* What to do when the queue is empty */
void bg_player_set_empty_mode(bg_player_t *, int do_quit);

void bg_player_set_window_config(bg_player_t *, const char * display_string);

/** \ingroup player
 *  \brief Get parameters
 *  \param player A player 
 *  \returns An array of parameters
 *
 *  This returns only some internal parameters, which should never be
 *  changed by the user. For user settable parameters, see
 *  \ref bg_player_get_input_parameters,
 *  \ref bg_player_get_audio_parameters,
 *  \ref bg_player_get_audio_filter_parameters,
 *  \ref bg_player_get_video_parameters,
 *  \ref bg_player_get_video_filter_parameters
 *  \ref bg_player_get_subtitle_parameters and
 *  \ref bg_player_get_osd_parameters
 */

const bg_parameter_info_t * bg_player_get_parameters(bg_player_t * player);

/** \ingroup player
 *  \brief Set parameter
 *  \param player A player cast to void
 *  \param name The name of the parameter
 *  \param val The parameter value
 */

void bg_player_set_parameter(void * player, const char * name,
                             const gavl_value_t * val);

/** \ingroup player
 *  \brief Set accelerators
 *  \param player A newly created player 
 *  \param list A list of accelerators, terminated with BG_KEY_NONE
 *
 */

void bg_player_add_accelerators(bg_player_t * player,
                                const bg_accelerator_t * list);

/** \ingroup player
 *  \brief Destroy a player
 *  \param player A player
 */

void bg_player_destroy(bg_player_t * player);


/** \ingroup player
 *  \brief Get the control handle of the player
 *  \param player A player
 *  \returns A handle, which can be connected with controls
 */

bg_controllable_t * bg_player_get_controllable(bg_player_t * player);

/** \ingroup player
 *  \brief Start the player thread
 *  \param player A player
 */

void bg_player_run(bg_player_t * player);

/** \ingroup player
 *  \brief Quit the player thread
 *  \param player A player
 */

void bg_player_quit(bg_player_t * player);

/** \ingroup player
 *  \brief Handle accelerator
 *  \param player A player
 *  \param id ID
 */

void bg_player_accel_pressed(bg_controllable_t * ctrl, int id);

/*
 *  Thread save functions for controlling the player (see playercmd.c)
 *  These just create messages and send them into the command queue
 */

/** \defgroup player_cmd Commands, which can be sent to the player
 *  \ingroup player
 *
 *  Most of these are called in an aynchronous manner.
 *
 *  @{
 */

/** \brief Play a track
 *  \param player A player
 *  \param url Location
 *  
 */

void bg_player_play_track(bg_msg_sink_t * sink, const gavl_dictionary_t * location);

/** \brief Seek to a specific time
 *  \param player A player
 *  \param time Time to seek to
 *  \param scale Timescale by which the time is scaled
 */

void bg_player_seek(bg_msg_sink_t * sink, gavl_time_t time, int scale);

/** \brief Seek to a specific percentage
 *  \param player A player
 *  \param perc Percentage (0.0 .. 100.0)
 */

void bg_player_seek_perc(bg_msg_sink_t * sink, float perc);


/** \brief Seek relative by a specific time
 *  \param player A player
 *  \param time Time offset (can be negative to seek backwards)
 */

void bg_player_seek_rel(bg_msg_sink_t * sink, gavl_time_t time);

/** \brief Set the volume
 *  \param player A player
 *  \param volume Volume (in dB, max is 0.0)
 */

void bg_player_set_volume(bg_msg_sink_t * sink, float volume);

/** \brief Set the volume relative
 *  \param player A player
 *  \param volume Volume offset (in dB)
 */

void bg_player_set_volume_rel(bg_msg_sink_t * sink, float volume);

/** \brief Stop playback
 *  \param player A player
 */

void bg_player_stop(bg_msg_sink_t * sink);

/** \brief Toggle pause
 *  \param player A player
 */

void bg_player_pause(bg_msg_sink_t * sink);

/** \brief Display OSD info
 *  \param player A player
 */

void bg_player_show_info(bg_msg_sink_t * sink);

/** \brief Display OSD time
 *  \param player A player
 */

void bg_player_show_time(bg_msg_sink_t * sink);

/** \brief Display OSD audio menu
 *  \param player A player
 */

void bg_player_audio_stream_menu(bg_msg_sink_t * sink);

/** \brief Display OSD subtitle menu
 *  \param player A player
 */

void bg_player_subtitle_stream_menu(bg_msg_sink_t * sink);

/** \brief Display OSD chapter menu
 *  \param player A player
 */

void bg_player_chapter_menu(bg_msg_sink_t * sink);


/** \brief Trigger an error
 *  \param player A player
 */

void bg_player_error(bg_msg_sink_t * sink);

/** \brief Set audio output plugin
 *  \param player A player
 *  \param handle A plugin handle
 */

// void bg_player_set_oa_plugin(bg_player_t * player, bg_plugin_handle_t * handle);

/** \brief Set video output plugin
 *  \param player A player
 *  \param handle A plugin handle
 */

// void bg_player_set_ov_plugin(bg_player_t * player, bg_plugin_handle_t * handle);

/** \brief Set audio stream
 *  \param player A player
 *  \param stream Stream index (starts with 0, -1 means no audio playback)
 */

void bg_player_set_audio_stream(bg_msg_sink_t * sink, int stream);

/** \brief Set video stream
 *  \param player A player
 *  \param stream Stream index (starts with 0, -1 means no video playback)
 */

void bg_player_set_video_stream(bg_msg_sink_t * sink, int stream);

/** \brief Set subtitle stream
 *  \param player A player
 *  \param stream Stream index (starts with 0, -1 means no subtitle playback)
 */

void bg_player_set_subtitle_stream(bg_msg_sink_t * sink, int stream);

/** \brief Shut down playback
 *  \param player A player
 *  \param flags A combination of BG_PLAY_FLAG_* flags
 */

// void bg_player_change(bg_player_t * p, int flags);

/** \brief Toggle mute
 *  \param player A player
 */

void bg_player_toggle_mute(bg_msg_sink_t * sink);

/** \brief Set mute
 *  \param player A player
 */

void bg_player_set_mute(bg_msg_sink_t * sink, int mute);

/** \brief Set fullscreen
 *  \param player A player
 *  \param fs Mode
 *
 *  0: Windowed, 1: Fullscreen, 2: toggle
 */

void bg_player_set_fullscreen(bg_msg_sink_t * sink, int fs);


/** \brief Goto a specified chapter
 *  \param player A player
 *  \param chapter Chapter index (starting with 0)
 */

void bg_player_set_chapter(bg_msg_sink_t * sink, int chapter);

/** \brief Goto the next chapter
 *  \param player A player
 */

void bg_player_next_chapter(bg_msg_sink_t * sink);

/** \brief Goto the previous chapter
 *  \param player A player
 */

void bg_player_prev_chapter(bg_msg_sink_t * sink);


/** \brief Set the next location
 *  \param p A player
 *  \param url Location
 *  \param m Metadata
 */

void bg_player_set_next_track(bg_msg_sink_t * s, const gavl_dictionary_t * loc);

void bg_player_set_track(bg_msg_sink_t * s, const gavl_dictionary_t * loc);

void bg_player_load_uri(bg_msg_sink_t * s,
                        const char * url,
                        int start_playing);


void bg_player_state_set(bg_player_t * p,
                         int last,
                         const char * var,
                         gavl_value_t * val);

/** \brief Set the location
 *  \param p A player
 *  \param url Location
 *  \param m Metadata
 *
 *  This also stops playback.
 */



void bg_player_play(bg_msg_sink_t * sink);

void bg_player_play_by_id(bg_msg_sink_t * sink, const char * id);


void bg_player_next(bg_msg_sink_t * sink);

void bg_player_prev(bg_msg_sink_t * sink);

void bg_player_load_uri(bg_msg_sink_t * s,
                        const char * uri,
                        int start_playing);


/** @} */

/** \defgroup player_cfg Player configuration
 * \ingroup player
 * @{
 */

/** \brief Get visualization parameters
 *  \param player A player
 *  \returns Null terminated parameter array.
 *
 *  Returned parameters can be passed to
 *  \ref bg_player_set_visualization_parameter
 */

const bg_parameter_info_t *
bg_player_get_visualization_parameters(bg_player_t *  player);

/** \brief Set a visualization parameter
 *  \param data Player casted to void*
 *  \param name Name
 *  \param val Value
 */

void
bg_player_set_visualization_parameter(void*data,
                                      const char * name, const gavl_value_t*val);


/**
 *  Configuration context
 */

bg_cfg_ctx_t * bg_player_get_cfg(bg_player_t *  player);

void bg_player_apply_cmdline(bg_cfg_ctx_t * ctx);


#define BG_PLAYER_VOLUME_INT_MAX 100

float bg_player_volume_to_dB(int volume);
int bg_player_volume_from_dB(float volume);



/** @} */



#endif // BG_PLAYER_H_INCLUDED

