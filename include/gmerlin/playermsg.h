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

#ifndef BG_PLAYERMSG_H_INCLUDED
#define BG_PLAYERMSG_H_INCLUDED

/** \defgroup player_states Player states
 *  \ingroup player_msg
 *  \brief State definitions for the player
 *
 *  @{
*/

#define BG_PLAYQUEUE_ID        "/playqueue"

/* Special metadata tags for tracks in the play queue */

#define BG_PLAYER_META_CLIENT_ID   GAVL_MSG_CLIENT_ID
#define BG_PLAYER_META_ORIGINAL_ID "OriginalID"

/* State variables */

#define BG_PLAYER_STATE_CTX           "player"
#define BG_PLAYER_STATE_VOLUME        "volume"        // float
#define BG_PLAYER_STATE_STATUS        "status"        // int
#define BG_PLAYER_STATE_CURRENT_TRACK "track"         // dictionary
// #define BG_PLAYER_STATE_CURRENT_TIME  "current_time"  // dictionary

#define BG_PLAYER_STATE_TIME          "time"         // long
#define BG_PLAYER_STATE_TIME_REM      "time_rem"     // long
#define BG_PLAYER_STATE_TIME_REM_ABS  "time_rem_abs" // long
#define BG_PLAYER_STATE_TIME_ABS      "time_abs"      // long
#define BG_PLAYER_STATE_TIME_PERC     "time_perc"     // float

// #define BG_PLAYER_STATE_CURRENT_URI   "uri"           // string

/* Some read-only variables */

#define BG_PLAYER_STATE_PROTOCOLS     "protocols"
#define BG_PLAYER_STATE_MIMETYPES     "mimetypes"
#define BG_PLAYER_STATE_LABEL         GAVL_META_LABEL

#define BG_PLAYER_STATE_MODE          "mode"          // int
#define BG_PLAYER_STATE_MUTE          "mute"          // int

/* All of them are read/write */
#define BG_PLAYER_STATE_AUDIO_STREAM_USER     "audio_stream_user"    // int
#define BG_PLAYER_STATE_VIDEO_STREAM_USER     "video_stream_user"    // int
#define BG_PLAYER_STATE_SUBTITLE_STREAM_USER  "subtitle_stream_user" // int

#define BG_PLAYER_STATE_AUDIO_STREAM_CURRENT     "audio_stream_current"    // int
#define BG_PLAYER_STATE_VIDEO_STREAM_CURRENT     "video_stream_current"    // int
#define BG_PLAYER_STATE_SUBTITLE_STREAM_CURRENT  "subtitle_stream_current" // int

#define BG_PLAYER_STATE_QUEUE_IDX "QueueIdx"
#define BG_PLAYER_STATE_QUEUE_LEN "QueueLen"

#define BG_PLAYER_STATE_CHAPTER          "chapter"         // int

/* Statuses */

#define BG_PLAYER_STATUS_INIT            -1 //!< Initializing
#define BG_PLAYER_STATUS_STOPPED         0 //!< Stopped, waiting for play command
#define BG_PLAYER_STATUS_PLAYING         1 //!< Playing
#define BG_PLAYER_STATUS_SEEKING         2 //!< Seeking
#define BG_PLAYER_STATUS_CHANGING        3 //!< Changing the track
#define BG_PLAYER_STATUS_INTERRUPTED     4 //!< Playback interrupted (due to parameter- or stream change)
#define BG_PLAYER_STATUS_PAUSED          5 //!< Paused
#define BG_PLAYER_STATUS_STARTING        7 //!< Starting playback
#define BG_PLAYER_STATUS_ERROR           8 //!< Error
#define BG_PLAYER_STATUS_QUIT            9 //!< Mail loop done (commandline player after tracklist got empty)

#define BG_PLAYER_MODE_NORMAL            0 //!< Normal playback
#define BG_PLAYER_MODE_REPEAT            1 //!< Repeat current album
#define BG_PLAYER_MODE_SHUFFLE           2 //!< Shuffle (implies repeat)
#define BG_PLAYER_MODE_ONE               3 //!< Play one track and stop
#define BG_PLAYER_MODE_LOOP              4 //!< Loop current track
#define BG_PLAYER_MODE_MAX               5 //!< Maximum

/**
 *  @}
 */

/* Message definition for the player */

/****************************
 *  Commands for the player
 ****************************/

// dvd:///dev/dvd?track=1
// dvb:///dev/dvd?channel=Phoenix
// cda:///dev/cdrom?track=1
// vcd:///dev/cdrom?track=1

/* Stop playing                              */

#define BG_PLAYER_CMD_STOP     1


/* Set the state of the player */
/*  arg1: New state             */

/*
 */

#define BG_PLAYER_CMD_SET_ERROR     3

/* Quit playback thread (used by bg_player_quit()) */

#define BG_PLAYER_CMD_QUIT          4

/* Change output plugins, arg1 is plugin handle of the opened plugin */

#define BG_PLAYER_CMD_NEXT          5
#define BG_PLAYER_CMD_PREV          6

/* Act like a pause button */

#define BG_PLAYER_CMD_PAUSE              8

#define BG_PLAYER_CMD_SET_NEXT_TRACK  9

/* Start playing previously selected URL */
#define BG_PLAYER_CMD_PLAY                26

/* Show info in the video window             */
#define BG_PLAYER_CMD_SHOW_INFO           30

#define BG_PLAYER_CMD_SHOW_TIME           31

#define BG_PLAYER_CMD_AUDIO_STREAM_MENU    32
#define BG_PLAYER_CMD_SUBTITLE_STREAM_MENU 33
#define BG_PLAYER_CMD_CHAPTER_MENU         34

#define BG_PLAYER_CMD_SET_AUTH             37

/* arg0: new mode (int) */
// #define BG_PLAYER_CMD_SET_MODE             39

/* arg0: Track ID (string) */
#define BG_PLAYER_CMD_SET_CURRENT_TRACK    40

/* arg0: track (dictionary)        */
/* arg1: start_playing (int) */

#define BG_PLAYER_CMD_SET_TRACK           41

/* arg0: uri (string)        */
/* arg1: start_playing (int) */

#define BG_PLAYER_CMD_SET_LOCATION         42

/* arg0: ID of the track to select */
#define BG_PLAYER_CMD_PLAY_BY_ID           43

#define BG_PLAYER_CMD_NEXT_VISUALIZATION    44

/* arg0: plugin_name?opt1=val1&opt2=val2 */
#define BG_PLAYER_CMD_SET_VISUALIZATION     45

/********************************
 * Messages from the player
********************************/

/** \defgroup player_msg Messages from the player
 *  \ingroup player
*
 *  @{
 */

/** \brief A key was pressed in the video window
 *
 *  arg0: keycode (see \ref keycodes)
 *
 *  arg1: mask (see \ref keycodes)
 *
 *  This message is only emitted if key+mask were not handled
 *  by the video plugin or by the player.
 */

#define BG_PLAYER_MSG_ACCEL               113 /* A key shortcut
                                                     was pressed */

/** \brief Player just cleaned up
 *
 *  A previously triggerend cleanup operation is finished.
 */

#define BG_PLAYER_MSG_CLEANUP             114

/** \brief Playback interrupted
 */

#define BG_PLAYER_MSG_INTERRUPT              119

/** \brief Interrupted playback resumed 
 */

#define BG_PLAYER_MSG_INTERRUPT_RESUME       120

/** \brief Audio peaks
 *  arg0: Number of samples processed
 *  arg1: Left peak
 *  arg2: Right peak
 */

#define BG_PLAYER_MSG_AUDIO_PEAK             122

/** \brief Transition to a new track
 *  arg0: 1 if transition was gapless
 *
 *  This message is emitted after the track was changed
 *  in gapless mode. Clients should expect messages for the
 *  new duration and metadata soon.
 */

#define BG_PLAYER_MSG_TRANSITION            124

/** \brief Play queue cleared
 *  Clients can use this to relize, that they don't own the playlist anymore
 */


#define BG_PLAYER_MSG_CURRENT_TRACK_CHANGED 130

/** \brief Maximum ID
 * IDs starting with BG_PLAYER_MSG_MAX can
 * savely be used for private purposes
 */

#define BG_PLAYER_MSG_MAX                    200

/** \brief Accelerators
 *  Must be in sync with player_gmerlin.js
 */

#define BG_PLAYER_ACCEL_VOLUME_DOWN             1
#define BG_PLAYER_ACCEL_VOLUME_UP               2
#define BG_PLAYER_ACCEL_SEEK_BACKWARD           3
#define BG_PLAYER_ACCEL_SEEK_FORWARD            4
#define BG_PLAYER_ACCEL_SEEK_START              5
#define BG_PLAYER_ACCEL_PAUSE                   6
#define BG_PLAYER_ACCEL_MUTE                    7
#define BG_PLAYER_ACCEL_NEXT_CHAPTER            8
#define BG_PLAYER_ACCEL_PREV_CHAPTER            9
#define BG_PLAYER_ACCEL_NEXT                   10
#define BG_PLAYER_ACCEL_PREV                   11
#define BG_PLAYER_ACCEL_PLAY                   12
#define BG_PLAYER_ACCEL_STOP                   13


#define BG_PLAYER_ACCEL_SEEK_10                21
#define BG_PLAYER_ACCEL_SEEK_20                22
#define BG_PLAYER_ACCEL_SEEK_30                23
#define BG_PLAYER_ACCEL_SEEK_40                24
#define BG_PLAYER_ACCEL_SEEK_50                25
#define BG_PLAYER_ACCEL_SEEK_60                26
#define BG_PLAYER_ACCEL_SEEK_70                27
#define BG_PLAYER_ACCEL_SEEK_80                28
#define BG_PLAYER_ACCEL_SEEK_90                29
#define BG_PLAYER_ACCEL_SHOW_INFO              30
#define BG_PLAYER_ACCEL_SHOW_TIME              31

#define BG_PLAYER_ACCEL_AUDIO_STREAM_MENU      32
#define BG_PLAYER_ACCEL_SUBTITLE_STREAM_MENU   33
#define BG_PLAYER_ACCEL_CHAPTER_MENU           34

#define BG_PLAYER_ACCEL_SEEK_BACKWARD_FAST     35
#define BG_PLAYER_ACCEL_SEEK_FORWARD_FAST      36     

#define BG_PLAYER_ACCEL_FULLSCREEN_ON          37
#define BG_PLAYER_ACCEL_FULLSCREEN_OFF         38
#define BG_PLAYER_ACCEL_FULLSCREEN_TOGGLE      39

#define BG_PLAYER_ACCEL_NEXT_VISUALIZATION     40

#define BG_PLAYER_ACCEL_PRIV                  100

/**
 *
 *
 */

void bg_player_stop_m(gavl_msg_t * msg);
void bg_player_set_mute_m(gavl_msg_t * msg, int mute);

void bg_player_pause_m(gavl_msg_t * msg);
void bg_player_show_info_m(gavl_msg_t * msg);
void bg_player_show_time_m(gavl_msg_t * msg);

void bg_player_audio_stream_menu_m(gavl_msg_t * msg);
void bg_player_subtitle_stream_menu_m(gavl_msg_t * msg);
void bg_player_chapter_menu_m(gavl_msg_t * msg);

void bg_player_set_parameter_idx_m(gavl_msg_t * msg, int idx,
                                   const char * name, const gavl_value_t * val);

void bg_player_set_audio_stream_m(gavl_msg_t * msg, int index);
void bg_player_set_video_stream_m(gavl_msg_t * msg, int index);
void bg_player_set_subtitle_stream_m(gavl_msg_t * msg, int index);
void bg_player_seek_m(gavl_msg_t * msg, gavl_time_t time, int scale);
void bg_player_seek_rel_m(gavl_msg_t * msg, gavl_time_t t);
void bg_player_seek_perc_m(gavl_msg_t * msg, float perc);

void bg_player_set_volume_m(gavl_msg_t * msg, float volume);
void bg_player_set_volume_rel_m(gavl_msg_t * msg, float volume);
void bg_player_set_chapter_m(gavl_msg_t * msg, int chapter);
void bg_player_prev_chapter_m(gavl_msg_t * msg);
void bg_player_next_chapter_m(gavl_msg_t * msg);

void bg_player_prev_m(gavl_msg_t * msg);
void bg_player_next_m(gavl_msg_t * msg);

void bg_player_set_visualization(bg_msg_sink_t * sink, const char * arg);

void bg_player_set_fullscreen_m(gavl_msg_t * msg, int fullscreen);

/* Extract infos from messages */
void bg_player_msg_get_audio_stream(gavl_msg_t * msg, int * idx,
                                    gavl_audio_format_t * in,
                                    gavl_audio_format_t * out,
                                    gavl_dictionary_t * m);

void bg_player_msg_get_video_stream(gavl_msg_t * msg, int * idx,
                                    gavl_video_format_t * in,
                                    gavl_video_format_t * out,
                                    gavl_dictionary_t * m);

void bg_player_msg_get_subtitle_stream(gavl_msg_t * msg, int * idx,
                                       int * is_text,
                                       gavl_video_format_t * in,
                                       gavl_video_format_t * out,
                                       gavl_dictionary_t * m);

void bg_player_msg_get_stream_metadata(gavl_msg_t * msg, int * idx,
                                       gavl_dictionary_t * m);

void bg_player_msg_get_num_streams(gavl_msg_t * msg, int * as, int * vs, int * ss);


const char * bg_player_get_state_icon(int state);


/* Genetate messages (used by the player itself and by proxies */

// void bg_player_msg_volume_changed(gavl_msg_t * msg, float volume);
// void bg_player_msg_mute(gavl_msg_t * msg, int m);
// void bg_player_msg_metadata(gavl_msg_t * msg, const gavl_dictionary_t * m);
// void bg_player_msg_time(gavl_msg_t * msg, gavl_time_t t, bg_player_tracklist_t * tl);
// void bg_player_msg_state(gavl_msg_t * msg, int state);



/**  @}
 */

#endif // BG_PLAYERMSG_H_INCLUDED
