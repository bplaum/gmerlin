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

#ifndef PLAYERPRIVATE_H_INCLUDED
#define PLAYERPRIVATE_H_INCLUDED


#include <pthread.h>
#include <config.h>

#include <gmerlin/pluginregistry.h>
#include <gmerlin/utils.h>
#include <gmerlin/bggavl.h>
#include <gmerlin/textrenderer.h>
#include <gmerlin/osd.h>
#include <gmerlin/filters.h>
#include <gmerlin/visualize.h>

#include <gmerlin/translation.h>
#include <gmerlin/ov.h>
#include <gmerlin/subtitle.h>

#include <gavl/peakdetector.h>


#include <gmerlin/bgthread.h>


/* Each thread get it's private context */

typedef enum
  {
    SYNC_SOFTWARE,
    SYNC_SOUNDCARD,
  } bg_player_sync_mode_t;

typedef enum
  {
    TIME_UPDATE_SECOND = 0,
    TIME_UPDATE_FRAME,
  } bg_player_time_update_mode_t;

typedef enum
  {
    BG_PLAYER_FINISH_CHANGE = 0,
    BG_PLAYER_FINISH_PAUSE,
  } bg_player_finish_mode_t;

/* Stream structures */

typedef struct
  {
  gavl_audio_source_t * in_src_int;
  gavl_audio_source_t * in_src;

  gavl_audio_source_t * src;
  
  /* Pipeline */
  
  bg_audio_filter_chain_t * fc;
  
  gavl_audio_frame_t * mute_frame;
  
  pthread_mutex_t config_mutex;
  bg_gavl_audio_options_t options;
  
  gavl_audio_format_t input_format;
  gavl_audio_format_t output_format;

  /* Volume control */
  gavl_volume_control_t * volume;
  pthread_mutex_t volume_mutex;

  /* Mute */
  int mute;
  pthread_mutex_t mute_mutex;
  
  int send_silence;
  gavl_peak_detector_t * peak_detector;
  
  /* Output plugin */
  bg_plugin_handle_t * plugin_handle;
  bg_oa_plugin_t     * plugin;
  void               * priv;

  /*
   *  Sync mode, set ONLY in init function,
   *  read by various threads
   */
  
  bg_player_sync_mode_t sync_mode;

  int output_open;
  /* Timing stuff */
  
  pthread_mutex_t time_mutex;
  gavl_time_t     current_time;
  gavl_timer_t *  timer;
  
  
  int64_t samples_written;

  pthread_mutex_t eof_mutex;
  int eof;

  bg_thread_t * th;

  gavl_audio_sink_t * sink;

  bg_parameter_info_t * plugin_params;
  bg_control_t * oa_ctrl;

  gavl_time_t time_offset;
  
  } bg_player_audio_stream_t;

typedef struct
  {
  bg_text_renderer_t * renderer;
  pthread_mutex_t config_mutex;

  gavl_video_source_t * vsrc;
  
  gavl_video_format_t input_format;
  gavl_video_format_t output_format;
  
  int eof;

  bg_parameter_info_t * parameters;
  
  int64_t time_offset_user;

  //  bg_overlay_info_t * oi;
  //  bg_text_info_t * ti;

  } bg_player_subtitle_stream_t;

typedef struct
  {
  bg_video_filter_chain_t * fc;
  pthread_mutex_t config_mutex;
  
  bg_gavl_video_options_t options;
  
  float still_framerate;
    
  gavl_video_format_t input_format;
  gavl_video_format_t output_format;


  int lock_fullscreen;
  
  bg_ov_t * ov;
  gavl_time_t frame_time;

  /* Stream id for subtitles in the output plugin */
  gavl_video_sink_t * subtitle_sink; 

  gavl_video_format_t osd_format;
  
  bg_osd_t * osd;
  
  bg_msg_sink_t * evt_sink;
  
  int64_t frames_written;
  int64_t frames_read;
  
  bg_accelerator_map_t * accel_map;
  
  bg_player_subtitle_stream_t * ss;
  bg_subtitle_handler_t * sh;
  
  //  gavl_video_frame_t * still_frame_in;
  //  int do_still;
  
  int eof;
  pthread_mutex_t eof_mutex;
  
  float bg_color[4];

  bg_thread_t * th;

  /* 1 if we are currently displaying a subtitle */
  int subtitle_active;
  
  int64_t skip;
  int64_t last_frame_time;

  int do_skip;
  
  gavl_video_source_t * in_src_int;
  gavl_video_source_t * in_src;

  gavl_video_source_t * src;
  //  bg_video_info_t * vi;
  
  bg_parameter_info_t * plugin_params;

  char * display_string;

  bg_controllable_t * ov_ctrl;
  bg_msg_sink_t * ov_evt_sink;
  
  gavl_time_t last_time;

  } bg_player_video_stream_t;


typedef struct
  {
  int state;
  gavl_time_t time;
  } bg_player_saved_state_t;

/* Player flags */

#define PLAYER_DO_AUDIO            (1<<0)
#define PLAYER_DO_VIDEO            (1<<1)
#define PLAYER_DO_SUBTITLE         (1<<2) /* Set by open() */
#define PLAYER_DO_SUBTITLE_OVERLAY (1<<3) /* Set by start() */
#define PLAYER_DO_SUBTITLE_TEXT    (1<<4) /* Set by start() */
#define PLAYER_DO_SUBTITLE_ONLY    (1<<5)
#define PLAYER_DO_VISUALIZE        (1<<6)
#define PLAYER_DO_STILL            (1<<7)
#define PLAYER_SINGLE_AV_THREAD    (1<<9)
#define PLAYER_SEEK_WINDOW         (1<<10)

#define PLAYER_DO_REPORT_PEAK      (1<<16)
#define PLAYER_FREEZE_FRAME        (1<<17)

#define DO_SUBTITLE_TEXT(f) \
 (f & PLAYER_DO_SUBTITLE_TEXT)

#define DO_SUBTITLE_OVERLAY(f) \
 (f & PLAYER_DO_SUBTITLE_OVERLAY)

#define DO_SUBTITLE(f) \
 (f & (PLAYER_DO_SUBTITLE))

#define DO_SUBTITLE_ONLY(f) \
  (f & PLAYER_DO_SUBTITLE_ONLY)

#define DO_AUDIO(f) \
  (f & PLAYER_DO_AUDIO)

#define DO_VIDEO(f) \
  (f & PLAYER_DO_VIDEO)

#define DO_VISUALIZE(f) \
 (f & PLAYER_DO_VISUALIZE)

#define DO_PEAK(f) \
 (f & PLAYER_DO_REPORT_PEAK)

#define DO_STILL(f)                              \
 (f & PLAYER_DO_STILL)

#define SINGLE_AV_THREAD(f)                       \
 (f & PLAYER_SINGLE_AV_THREAD)

/* The player */

#define PLAYER_MAX_THREADS 2

#define PLAYER_USE_SRC

typedef struct
  {
  char * location;
  
  gavl_dictionary_t * track_info;
  const gavl_dictionary_t * metadata;
  const gavl_dictionary_t * chapterlist;
  
  bg_plugin_handle_t * input_handle;
  bg_input_plugin_t  * input_plugin;

  /* <1 = switched off */
  int audio_stream;
  int video_stream;
  int subtitle_stream;
  
  gavl_audio_source_t  * audio_src;
  gavl_video_source_t  * video_src;
  gavl_packet_source_t * text_src;
  gavl_video_source_t  * ovl_src;

  gavl_dictionary_t track; // External metadata

  const gavl_dictionary_t * edl; // EDL
  
  gavl_dictionary_t url_vars; // URL variables
  
  const bg_plugin_info_t * plugin_info;

  int track_idx;
  
  int can_pause;
  int can_seek;

  /* Take plugin from last track and just switch the track */
  int switch_track;
  
  gavl_time_t duration;
  bg_controllable_t * input_ctrl;
  
  
  
  } bg_player_source_t;

struct bg_player_s
  {
  pthread_t player_thread;

  bg_thread_common_t * thread_common;

  bg_thread_t * threads[PLAYER_MAX_THREADS];

  /* Sources */
  bg_player_source_t srcs[2];
  bg_player_source_t * src;
  bg_player_source_t * src_next;
  int src_cur_idx;
  pthread_mutex_t src_mutex;
  
  /* Input plugin and stuff */
  
  //  bg_input_callbacks_t input_callbacks;

  /* Stream Infos */
    
  bg_player_audio_stream_t    audio_stream;
  bg_player_video_stream_t    video_stream;
  bg_player_subtitle_stream_t subtitle_stream;
  
  /*
   *  Visualizer: Initialized and cleaned up by the player
   *  core, run by the audio thread
   */
  
  bg_visualizer_t * visualizer;
  
  /*
   * Flags are set by bg_player_input_start()
   */
  
  int flags;
  int old_flags; /* Flags from previous playback */
  
  /*
   *  Stream selection
   *  Values set by the user are here
   *  Actual values are the
   *  audio_stream, video_stream and
   *  subtitle_stream members of the source
   */
  int audio_stream_user; 
  int video_stream_user;
  int subtitle_stream_user;

  int current_chapter;
  int current_track;
  
  /* Can we seek? */

  int can_seek;
  int can_pause;
  
  /* Message stuff */
  bg_controllable_t ctrl;

  /* Control handle for the input */
  bg_msg_sink_t * src_msg_sink;
  
  /* Inernal state variables */

  int status;
  int last_status;
  
  pthread_mutex_t state_mutex;
  
  gavl_time_t display_time_offset; // Time offset for the current source (e.g. live streams)
  gavl_time_t time_offset;
  pthread_mutex_t time_offset_mutex;

  /* Seek window */
  gavl_time_t seek_window_start; // Time offset for the current source (e.g. live streams)
  gavl_time_t seek_window_end;
  gavl_time_t seek_window_start_absolute; // Absolute start time of seek window (if available)
  pthread_mutex_t seek_window_mutex;
    
  
  /* Stuff for synchronous stopping and starting of the playback */
  
  float volume; /* Current volume in dB (0 == max) */
  
  bg_player_saved_state_t saved_state;
  
  pthread_mutex_t config_mutex;
  float still_framerate;
  gavl_time_t sync_offset;
  
  bg_player_time_update_mode_t time_update_mode;
  
  bg_player_finish_mode_t finish_mode;
  gavl_time_t wait_time;

  bg_cfg_ctx_t cfg[BG_PLAYER_CFG_NUM+1];

  /* Track list */
  bg_player_tracklist_t tl;

  /* State */
  gavl_dictionary_t state;

  int state_init;

  /* Resync */

  //  int64_t resync_time;
  //  int     resync_scale;
  //  pthread_mutex_t resync_mutex;
  
  int visualization_mode;

  /* Restart mode */
  int restart;
  pthread_mutex_t restart_mutex;
  
  };

// void bg_player_set_resync(bg_player_t * player, int64_t time, int scale);
// int bg_player_get_resync(bg_player_t * player, int64_t * time, int * scale);

// void bg_player_set_current(bg_player_t * player);

void bg_player_underrun(bg_msg_sink_t * sink);

int  bg_player_get_status(bg_player_t * player);
void bg_player_set_status(bg_player_t * player, int status);

int  bg_player_get_restart(bg_player_t * player);
void bg_player_set_restart(bg_player_t * player, int restart);


int bg_player_handle_input_message(void * priv, gavl_msg_t * msg);

void bg_player_state_set_local(bg_player_t * p,
                               int last,
                               const char * ctx,
                               const char * var,
                               const gavl_value_t * val);

int bg_player_handle_command(void * priv, gavl_msg_t * command);

/* Get the current time (thread save) */

void bg_player_time_get(bg_player_t * player, int exact, gavl_time_t * ret_total, gavl_time_t * ret);
void bg_player_time_stop(bg_player_t * player);
void bg_player_time_start(bg_player_t * player);
void bg_player_time_init(bg_player_t * player);
void bg_player_time_reset(bg_player_t * player);
void bg_player_time_set(bg_player_t * player, gavl_time_t time);
void bg_player_broadcast_time(bg_player_t * player, gavl_time_t time);

/* player_input.c */

void bg_player_input_destroy(bg_player_t * player);

int bg_player_input_start(bg_player_t * p);

void bg_player_input_cleanup(bg_player_t * p);


int
bg_player_input_get_audio_format(bg_player_t * ctx);
int
bg_player_input_get_video_format(bg_player_t * ctx);

int
bg_player_input_read_audio(void * priv, gavl_audio_frame_t * frame, int stream, int samples);

int
bg_player_input_read_video(void * priv, gavl_video_frame_t * frame, int stream);

int
bg_player_input_read_video_still(void * priv, gavl_video_frame_t * frame, int stream);

int
bg_player_input_read_video_subtitle_only(void * priv, gavl_video_frame_t * frame, int stream);

int
bg_player_input_get_subtitle_format(bg_player_t * ctx);

void bg_player_input_seek(bg_player_t * ctx,
                          gavl_time_t * time, int scale);


// void bg_player_source_set(bg_player_t * player, bg_player_source_t * src,
//                          const char * location, const gavl_dictionary_t * m);

void bg_player_source_select_streams(bg_player_t * player, bg_player_source_t * src);
int bg_player_source_set_from_handle(bg_player_t * player, bg_player_source_t * src,
                                     bg_plugin_handle_t * h, int track_index);

int bg_player_source_open(bg_player_t * player, bg_player_source_t * src, int primary);
void bg_player_source_close(bg_player_source_t * src);
void bg_player_source_cleanup(bg_player_source_t * src);

void bg_player_source_stop(bg_player_t * player, bg_player_source_t * p);
int bg_player_source_start(bg_player_t * player, bg_player_source_t * p);

/* player_ov.c */

void bg_player_ov_create(bg_player_t * player);

void bg_player_ov_reset(bg_player_t * player);

void bg_player_ov_destroy(bg_player_t * player);
int bg_player_ov_init(bg_player_video_stream_t * vs);

void bg_player_ov_cleanup(bg_player_video_stream_t * ctx);
void * bg_player_ov_thread(void *);

/* Update still image: To be called during pause */
void bg_player_ov_update_still(bg_player_t * p);
void bg_player_ov_handle_events(bg_player_video_stream_t * s);

void bg_player_ov_standby(bg_player_video_stream_t * ctx);

/* Set this extra because we must initialize subtitles after the video output */
void bg_player_ov_set_subtitle_format(bg_player_video_stream_t * ctx);

// void bg_player_ov_set_plugin(bg_player_t * player,
//                             bg_plugin_handle_t * handle);

/* Plugin handle is needed by the core to fire up the visualizer */

// bg_plugin_handle_t * bg_player_ov_get_plugin(bg_player_ov_context_t * ctx);

/*
 *  This call will let the video plugin adjust the playertime from the
 *  next frame to be displayed
 */
void bg_player_ov_sync(bg_player_t * p);

/* player_video.c */

int bg_player_video_init(bg_player_t * p, int video_stream);
void bg_player_video_cleanup(bg_player_t * p);
void bg_player_video_create(bg_player_t * p, bg_plugin_registry_t * plugin_reg);
void bg_player_video_destroy(bg_player_t * p);

int bg_player_read_video(bg_player_t * p,
                         gavl_video_frame_t ** frame);

void bg_player_video_set_eof(bg_player_t * p);


/* player_oa.c */

int bg_player_oa_init(bg_player_audio_stream_t * ctx);
int bg_player_oa_start(bg_player_audio_stream_t * ctx);
void bg_player_oa_stop(bg_player_audio_stream_t * ctx);

void bg_player_oa_cleanup(bg_player_audio_stream_t * ctx);
void * bg_player_oa_thread(void *);

// void bg_player_oa_set_plugin(bg_player_t * player,
//                              bg_plugin_handle_t * handle);

void bg_player_oa_set_volume(bg_player_audio_stream_t * ctx,
                             float volume);

void bg_player_peak_callback(void * priv, int num_samples,
                             const double * min,
                             const double * max,
                             const double * abs);


/*
 *  Audio output must be locked, since the sound latency is
 *  obtained from here by other threads
 */

int  bg_player_oa_get_latency(bg_player_audio_stream_t * ctx);

/* player_audio.c */

int  bg_player_audio_init(bg_player_t * p, int audio_stream);
void bg_player_audio_cleanup(bg_player_t * p);

void bg_player_audio_create(bg_player_t * p, bg_plugin_registry_t * plugin_reg);
void bg_player_audio_destroy(bg_player_t * p);

// int bg_player_read_audio(bg_player_t * p, gavl_audio_frame_t * frame);

/* Returns 1 if the thread should be finished, 0 if silence should be sent */
int bg_player_audio_set_eof(bg_player_t * p);


/* player_subtitle.c */

int bg_player_subtitle_init(bg_player_t * player);
void bg_player_subtitle_cleanup(bg_player_t * p);

void bg_player_subtitle_create(bg_player_t * p);
void bg_player_subtitle_destroy(bg_player_t * p);

void bg_player_subtitle_init_converter(bg_player_t * player);

void bg_player_key_pressed(bg_player_t * player, int key, int mask);

//void bg_player_set_duration(bg_player_t * player, gavl_time_t duration, int can_seek);

void bg_player_set_current_track(bg_player_t * player, const gavl_dictionary_t * dict);
//void bg_player_set_metadata(bg_player_t * player, const gavl_dictionary_t * m);

int bg_player_get_subtitle_index(const gavl_dictionary_t * info, int stream_index, int * is_text);

void bg_player_subtitle_unscale_time(bg_player_subtitle_stream_t * s, gavl_overlay_t * ovl);

void bg_player_swap_sources(bg_player_t * p);

int bg_player_advance_gapless(bg_player_t * player);


/* Interrupt playback for changing streams or output plugins */
void bg_player_stream_change_init(bg_player_t * player);
int bg_player_stream_change_done(bg_player_t * player);

/** \brief Get input parameters
 *  \param player A player
 *  \returns Null terminated parameter array.
 *
 *  Returned parameters can be passed to \ref bg_player_set_input_parameter
 */

const bg_parameter_info_t * bg_player_get_input_parameters(bg_player_t *  player);

/** \brief Set an input parameter
 *  \param data Player casted to void*
 *  \param name Name
 *  \param val Value
 */
void bg_player_set_input_parameter(void * data, const char * name,
                                   const gavl_value_t * val);

/** \brief Get audio parameters
 *  \param player A player
 *  \returns Null terminated parameter array.
 *
 *  Returned parameters can be passed to \ref bg_player_set_audio_parameter
 */

const bg_parameter_info_t * bg_player_get_audio_parameters(bg_player_t * player);

/** \brief Get audio filter parameters
 *  \param player A player
 *  \returns Null terminated parameter array.
 *
 *  Returned parameters can be passed to \ref bg_player_set_audio_filter_parameter
 */

const bg_parameter_info_t * bg_player_get_audio_filter_parameters(bg_player_t * player);

/** \brief Set an audio parameter
 *  \param data Player casted to void*
 *  \param name Name
 *  \param val Value
 */
void bg_player_set_audio_parameter(void*data, const char * name,
                                   const gavl_value_t*val);

/** \brief Set an audio filter parameter
 *  \param data Player casted to void*
 *  \param name Name
 *  \param val Value
 */
void bg_player_set_audio_filter_parameter(void*data, const char * name,
                                          const gavl_value_t*val);


void bg_player_handle_audio_filter_command(bg_player_t * p, gavl_msg_t * msg);

/** \brief Get video parameters
 *  \param player A player
 *  \returns Null terminated parameter array.
 *
 *  Returned parameters can be passed to \ref bg_player_set_video_parameter
 */
const bg_parameter_info_t * bg_player_get_video_parameters(bg_player_t * player);

/** \brief Get video filter parameters
 *  \param player A player
 *  \returns Null terminated parameter array.
 *
 *  Returned parameters can be passed to \ref bg_player_set_video_parameter
 */
const bg_parameter_info_t * bg_player_get_video_filter_parameters(bg_player_t * player);

/** \brief Get video output plugin parameters
 *  \param player A player
 *  \returns Null terminated parameter array.
 *
 *  Returned parameters can be passed to \ref bg_player_set_ov_plugin_parameter
 */
const bg_parameter_info_t * bg_player_get_ov_plugin_parameters(bg_player_t * p);

/** \brief Get audio output plugin parameters
 *  \param player A player
 *  \returns Null terminated parameter array.
 *
 *  Returned parameters can be passed to \ref bg_player_set_ov_plugin_parameter
 */
const bg_parameter_info_t * bg_player_get_oa_plugin_parameters(bg_player_t * p);

/** \brief Set a video parameter
 *  \param data Player casted to void*
 *  \param name Name
 *  \param val Value
 */
void bg_player_set_video_parameter(void*data, const char * name,
                                   const gavl_value_t*val);

/** \brief Set a video filter parameter
 *  \param data Player casted to void*
 *  \param name Name
 *  \param val Value
 */
void bg_player_set_video_filter_parameter(void*data, const char * name,
                                          const gavl_value_t*val);

void bg_player_handle_video_filter_command(bg_player_t * p, gavl_msg_t * msg);

/** \brief Set a video output plugin parameter
 *  \param data Player casted to void*
 *  \param name Name
 *  \param val Value
 */

void bg_player_set_ov_plugin_parameter(void * data, const char * name, const gavl_value_t * val);

/** \brief Set a audio output plugin parameter
 *  \param data Player casted to void*
 *  \param name Name
 *  \param val Value
 */

void bg_player_set_oa_plugin_parameter(void * data, const char * name, const gavl_value_t * val);

/** \brief Get subtitle parameters
 *  \param player A player
 *  \returns Null terminated parameter array.
 *
 *  Returned parameters can be passed to \ref bg_player_set_subtitle_parameter
 */

const bg_parameter_info_t * bg_player_get_subtitle_parameters(bg_player_t * player);

/** \brief Set a subtitle parameter
 *  \param data Player casted to void*
 *  \param name Name
 *  \param val Value
 */
void bg_player_set_subtitle_parameter(void*data, const char * name, const gavl_value_t*val);

/** \brief Get OSD parameters
 *  \param player A player
 *  \returns Null terminated parameter array.
 *
 *  Returned parameters can be passed to \ref bg_player_set_osd_parameter
 */

const bg_parameter_info_t * bg_player_get_osd_parameters(bg_player_t * player);

/** \brief Set an OSD parameter
 *  \param data Player casted to void*
 *  \param name Name
 *  \param val Value
 */

void bg_player_set_osd_parameter(void*data, const char * name, const gavl_value_t*val);

void bg_player_set_eof(bg_player_t * p);


#define BG_PLAYER_CMD_EOF 1

#endif // PLAYERPRIVATE_H_INCLUDED

