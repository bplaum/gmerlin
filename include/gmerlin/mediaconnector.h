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



#ifndef BG_MEDIACONNECTOR_H_INCLUDED
#define BG_MEDIACONNECTOR_H_INCLUDED


#include <config.h>
#include <gavl/connectors.h>

#include <gmerlin/cfg_registry.h>
#include <gmerlin/bgthread.h>
#include <gmerlin/bgmsg.h>


#define BG_MEDIACONNECTOR_FLAG_EOF     (1<<0)
#define BG_MEDIACONNECTOR_FLAG_DISCONT (1<<1)

typedef struct bg_plugin_handle_s bg_plugin_handle_t;

/** \brief Stream actions
 *
 *  These describe how streams should be handled by the input
 *  plugin. Note that by default, each stream is switched off.
 */

typedef enum
  {
    BG_STREAM_ACTION_OFF = 0, //!< Stream is switched off and will be ignored
    BG_STREAM_ACTION_DECODE,  //!< Stream is switched on and will be decoded
    BG_STREAM_ACTION_READRAW, //!< Stream will be read in compressed form
    
  } bg_stream_action_t;

typedef enum
  {
    BG_EOF_RESYNC         = 1,
    BG_EOF_END_OF_PROGRAM = 2,
  } bg_eof_mode_t;

typedef struct
  {
  void * user_data;
  void (*free_user_data)(void* user_data);

  bg_plugin_handle_t * codec_handle;
  
  gavl_stream_type_t type;
  
  gavl_dictionary_t    * s;
  gavl_audio_source_t  * asrc;
  gavl_video_source_t  * vsrc;
  gavl_packet_source_t * psrc;
  bg_msg_hub_t         * msghub;

  gavl_audio_source_t  * asrc_priv;
  gavl_video_source_t  * vsrc_priv;
  gavl_packet_source_t * psrc_priv;
  bg_msg_hub_t         * msghub_priv;

  bg_stream_action_t action;

  /* Mostly for message streams */
  int stream_id;

  int flags;
  } bg_media_source_stream_t;

typedef struct
  {
  void * user_data;
  void (*free_user_data)(void* user_data);
  
  gavl_dictionary_t * track;
  gavl_dictionary_t * track_priv;
  
  bg_media_source_stream_t ** streams;
  int num_streams;
  int streams_alloc;
  
  int flags;

  bg_eof_mode_t eof_mode;
  int eof;
  
  } bg_media_source_t;

/* Drop everything until EOF */
void bg_media_source_drain(bg_media_source_t * src);
void bg_media_source_reset(bg_media_source_t * src);

//void bg_media_source_set_eof(bg_media_source_t * src, int eof);

void bg_media_source_init(bg_media_source_t * src);
void bg_media_source_cleanup(bg_media_source_t * src);

int bg_media_source_set_from_track(bg_media_source_t * src,
                                   gavl_dictionary_t * track);

void bg_media_source_set_from_source(bg_media_source_t * dst,
                                     bg_media_source_t * src);

bg_media_source_stream_t *
bg_media_source_append_stream(bg_media_source_t * src, gavl_stream_type_t type);

bg_media_source_stream_t *
bg_media_source_append_audio_stream(bg_media_source_t * src);

bg_media_source_stream_t *
bg_media_source_append_video_stream(bg_media_source_t * src);

bg_media_source_stream_t *
bg_media_source_append_text_stream(bg_media_source_t * src);

bg_media_source_stream_t *
bg_media_source_append_overlay_stream(bg_media_source_t * src);

bg_media_source_stream_t *
bg_media_source_append_msg_stream_by_id(bg_media_source_t * src, int id);

bg_media_source_stream_t * bg_media_source_get_stream(bg_media_source_t * src, int type, int idx);

bg_media_source_stream_t * bg_media_source_get_stream_by_id(bg_media_source_t * src, int id);


bg_media_source_stream_t * bg_media_source_get_audio_stream(bg_media_source_t * src, int idx);
bg_media_source_stream_t * bg_media_source_get_video_stream(bg_media_source_t * src, int idx);
bg_media_source_stream_t * bg_media_source_get_text_stream(bg_media_source_t * src, int idx);
bg_media_source_stream_t * bg_media_source_get_overlay_stream(bg_media_source_t * src, int idx);
bg_media_source_stream_t * bg_media_source_get_msg_stream_by_id(bg_media_source_t * src, int id);

gavl_audio_source_t * bg_media_source_get_audio_source(bg_media_source_t * src, int idx);
gavl_video_source_t * bg_media_source_get_video_source(bg_media_source_t * src, int idx);
gavl_video_source_t * bg_media_source_get_overlay_source(bg_media_source_t * src, int idx);

gavl_packet_source_t * bg_media_source_get_audio_packet_source(bg_media_source_t * src, int idx);
gavl_packet_source_t * bg_media_source_get_video_packet_source(bg_media_source_t * src, int idx);
gavl_packet_source_t * bg_media_source_get_overlay_packet_source(bg_media_source_t * src, int idx);
gavl_packet_source_t * bg_media_source_get_msg_packet_source_by_id(bg_media_source_t * src, int id);
bg_msg_hub_t * bg_media_source_get_msg_hub_by_id(bg_media_source_t * src, int id);

gavl_packet_source_t * bg_media_source_get_text_source(bg_media_source_t * src, int idx);

int bg_media_source_set_stream_action(bg_media_source_t * src, gavl_stream_type_t type, int idx,
                                       bg_stream_action_t action);

int bg_media_source_set_audio_action(bg_media_source_t * src, int idx, bg_stream_action_t action);
int bg_media_source_set_video_action(bg_media_source_t * src, int idx, bg_stream_action_t action);
int bg_media_source_set_text_action(bg_media_source_t * src, int idx, bg_stream_action_t action);
int bg_media_source_set_overlay_action(bg_media_source_t * src, int idx, bg_stream_action_t action);
int bg_media_source_set_msg_action_by_id(bg_media_source_t * src, int id, bg_stream_action_t action);

int bg_media_source_get_num_streams(const bg_media_source_t * src, gavl_stream_type_t type);

/* Load implicit decoders */
int bg_media_source_load_decoders(bg_media_source_t * src);

/* Sink */

typedef struct
  {
  void * user_data;
  void (*free_user_data)(void* user_data);

  gavl_stream_type_t type;

  gavl_dictionary_t    * s;
  //  gavl_dictionary_t    info;
  gavl_audio_sink_t  * asink;
  gavl_video_sink_t  * vsink;
  gavl_packet_sink_t * psink;
  bg_msg_sink_t      * msgsink;

  gavl_audio_sink_t  * asink_priv;
  gavl_video_sink_t  * vsink_priv;
  gavl_packet_sink_t * psink_priv;
  bg_msg_sink_t      * msgsink_priv;

  /* Mostly for message streams */
  int stream_id;
  
  } bg_media_sink_stream_t;

typedef struct
  {
  void * user_data;
  void (*free_user_data)(void* user_data);
  
  gavl_dictionary_t * track;
  bg_media_sink_stream_t ** streams;
  int num_streams;
  int streams_alloc;
  } bg_media_sink_t;

void bg_media_sink_init(bg_media_sink_t * sink);
void bg_media_sink_cleanup(bg_media_sink_t * sink);

bg_media_sink_stream_t *
bg_media_sink_append_stream(bg_media_sink_t * sink, gavl_stream_type_t type, gavl_dictionary_t * s);

bg_media_sink_stream_t *
bg_media_sink_append_audio_stream(bg_media_sink_t * sink, gavl_dictionary_t * s);

bg_media_sink_stream_t *
bg_media_sink_append_video_stream(bg_media_sink_t * sink, gavl_dictionary_t * s);

bg_media_sink_stream_t *
bg_media_sink_append_text_stream(bg_media_sink_t * sink, gavl_dictionary_t * s);

bg_media_sink_stream_t *
bg_media_sink_append_overlay_stream(bg_media_sink_t * sink, gavl_dictionary_t * s);

bg_media_sink_stream_t *
bg_media_sink_append_msg_stream_by_id(bg_media_sink_t * sink, gavl_dictionary_t * s, int id);

bg_media_sink_stream_t * bg_media_sink_get_stream(bg_media_sink_t * sink, int type, int idx);

bg_media_sink_stream_t * bg_media_sink_get_audio_stream(bg_media_sink_t * sink, int idx);
bg_media_sink_stream_t * bg_media_sink_get_video_stream(bg_media_sink_t * sink, int idx);
bg_media_sink_stream_t * bg_media_sink_get_text_stream(bg_media_sink_t * sink, int idx);
bg_media_sink_stream_t * bg_media_sink_get_overlay_stream(bg_media_sink_t * sink, int idx);
bg_media_sink_stream_t * bg_media_sink_get_stream_by_id(bg_media_sink_t * sink, int id);


/* Connector */


typedef struct bg_mediaconnector_s bg_mediaconnector_t;

typedef struct bg_mediaconnector_stream_s
  {
  gavl_audio_source_t    * asrc;
  gavl_audio_connector_t * aconn;

  gavl_video_source_t    * vsrc;
  gavl_video_connector_t * vconn;

  gavl_packet_source_t    * psrc;
  gavl_packet_connector_t * pconn;
  
  bg_msg_hub_t * msghub;
  
  int timescale;
  gavl_stream_type_t type;        // GAVL_STREAM_*
  
  int flags;
  
  gavl_dictionary_t * s;
  gavl_dictionary_t * m;
  
  gavl_time_t time;

  int64_t counter;
  
  bg_thread_t * th;
  bg_mediaconnector_t * conn;
  
  /* Discontinuous stream support: Don't output a packet
     too early */
  
  gavl_video_frame_t * discont_vframe;
  gavl_packet_t      * discont_packet;
  
  gavl_video_source_t * discont_vsrc;
  gavl_packet_source_t * discont_psrc;

  int src_index; // index in the primary source
  int dst_index; // index in the destination

  void * priv;
  void (*free_priv)(struct bg_mediaconnector_stream_s *);

  gavl_source_status_t last_status;
  
  } bg_mediaconnector_stream_t;

struct bg_mediaconnector_s
  {
  bg_mediaconnector_stream_t ** streams;
  int num_streams;
  int num_threads; // Can be < num_streams
  
  bg_thread_common_t * tc;

  bg_thread_t ** th;
  
  pthread_mutex_t time_mutex;
  gavl_time_t time;

  pthread_mutex_t running_threads_mutex;
  int running_threads;

  gavl_dictionary_t track;
  };

void
bg_mediaconnector_init(bg_mediaconnector_t * conn);

bg_mediaconnector_stream_t *
bg_mediaconnector_append_audio_stream(bg_mediaconnector_t * conn,
                                      const gavl_dictionary_t * s,
                                      gavl_audio_source_t * asrc,
                                      gavl_packet_source_t * psrc);

bg_mediaconnector_stream_t *
bg_mediaconnector_append_video_stream(bg_mediaconnector_t * conn,
                                      const gavl_dictionary_t * s,
                                      gavl_video_source_t * vsrc,
                                      gavl_packet_source_t * psrc);

bg_mediaconnector_stream_t *
bg_mediaconnector_append_overlay_stream(bg_mediaconnector_t * conn,
                                        const gavl_dictionary_t * s,
                                        gavl_video_source_t * vsrc,
                                        gavl_packet_source_t * psrc);

bg_mediaconnector_stream_t *
bg_mediaconnector_append_text_stream(bg_mediaconnector_t * conn,
                                     const gavl_dictionary_t * s,
                                     gavl_packet_source_t * psrc);

bg_mediaconnector_stream_t *
bg_mediaconnector_append_msg_stream(bg_mediaconnector_t * conn,
                                    const gavl_dictionary_t * s,
                                    bg_msg_hub_t * msghub,
                                    gavl_packet_source_t * psrc);

int bg_mediaconnector_get_num_streams(bg_mediaconnector_t * conn,
                                      gavl_stream_type_t type);

bg_mediaconnector_stream_t *
bg_mediaconnector_get_stream(bg_mediaconnector_t * conn,
                             gavl_stream_type_t type, int idx);


void
bg_mediaconnector_create_conn(bg_mediaconnector_t * conn);


void
bg_mediaconnector_update_time(bg_mediaconnector_t * conn,
                              gavl_time_t time);

gavl_time_t
bg_mediaconnector_get_time(bg_mediaconnector_t * conn);

gavl_time_t
bg_mediaconnector_get_min_time(bg_mediaconnector_t * conn);

void
bg_mediaconnector_create_threads(bg_mediaconnector_t * conn, int all);

void
bg_mediaconnector_create_threads_common(bg_mediaconnector_t * conn);

void
bg_mediaconnector_create_thread(bg_mediaconnector_t * conn, int index, int all);

void
bg_mediaconnector_start(bg_mediaconnector_t * conn);

void
bg_mediaconnector_reset(bg_mediaconnector_t * conn);

void
bg_mediaconnector_free(bg_mediaconnector_t * conn);

int bg_mediaconnector_iteration(bg_mediaconnector_t * conn);

void
bg_mediaconnector_threads_init_separate(bg_mediaconnector_t * conn);



void
bg_mediaconnector_threads_start(bg_mediaconnector_t * conn);

void
bg_mediaconnector_threads_stop(bg_mediaconnector_t * conn);

int bg_mediaconnector_done(bg_mediaconnector_t * conn);

void bg_mediaconnector_set_from_source(bg_mediaconnector_t * conn, bg_media_source_t * src);

/* Media hub */

typedef struct bg_media_hub_s bg_media_hub_t;

bg_media_hub_t * bg_media_hub_create(bg_media_source_t * src);

void bg_media_hub_connect(bg_media_hub_t * hub, bg_media_sink_t * sink);
void bg_media_hub_disconnect(bg_media_hub_t * hub, bg_media_sink_t * sink);
void bg_media_hub_iteration(bg_media_hub_t * hub);
void bg_media_hub_destroy(bg_media_hub_t * hub);


#endif // BG_MEDIACONNECTOR_H_INCLUDED

