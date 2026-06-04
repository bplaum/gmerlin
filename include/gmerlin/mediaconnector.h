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


// #define BG_MEDIACONNECTOR_FLAG_EOF     (1<<0)
// #define BG_MEDIACONNECTOR_FLAG_DISCONT (1<<1)

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

  /* Used for exporting frame (most likely to DMA buffers) */
  gavl_audio_source_t  * asrc_export;
  gavl_video_source_t  * vsrc_export;
  
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
                                     const bg_media_source_t * src);

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
int bg_media_source_get_stream_idx(bg_media_source_t * src, const bg_media_source_stream_t * st);


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

int bg_media_source_set_export(bg_media_source_t * src,
                               const gavl_array_t * abuf, const gavl_array_t * vbuf);

/* Load filters: Between the two functions the source must get started */

int bg_media_source_filter_init(bg_media_source_t * filter_src,
                                bg_media_source_t * src);

int bg_media_source_filter_connect(bg_media_source_t * filter_src,
                                   bg_media_source_t * src);

/*
 * Encoder frontend: Connect sinks and provide processing routines.
 * This is the generic multistream processing engine, which is used 
 * by gmerlin-transoder and the gavftools
 */

/* One A/V stream is the master: When it's processed,
   the non-continuous streams (subtitles) are also flushed.
   It's better than having them in own threads, which are idle most of the time */

#define BG_ENCODER_STREAM_DELAY    (1<<1)
#define BG_ENCODER_STREAM_NONCONT  (1<<2)
#define BG_ENCODER_STREAM_EOF      (1<<3)
/* Non-continuous streams need this */
#define BG_ENCODER_GOT_SINK_FRAME  (1<<4)
#define BG_ENCODER_GOT_SRC_FRAME   (1<<5)

typedef struct
  {
  gavl_audio_sink_t * asink;
  gavl_video_sink_t * vsink;
  gavl_packet_sink_t * psink;
  bg_msg_sink_t * msink;
  
  int src_scale;
  uint32_t dst_scale;
  
  gavl_time_t time;
  int idx;
  int flags;
  
  gavl_compression_info_t ci;
  /* Packets and video frames are stored here between process calls */
  gavl_packet_t * p;
  gavl_video_frame_t * vframe;
  gavl_source_status_t (*process)(bg_media_source_stream_t * st, gavl_time_t t);
  } bg_encoder_stream_t;

typedef struct
  {
  bg_plugin_handle_t * h;
  gavl_time_t time;
  pthread_mutex_t mutex;
  } bg_encoder_t;

bg_encoder_stream_t * bg_encoder_stream_create(bg_media_source_stream_t * st);
bg_encoder_t * bg_encoder_create(bg_media_source_t * src);


int bg_media_encoder_init(bg_media_source_t * src,
                          bg_plugin_handle_t * h);



int bg_media_encoder_connect(bg_media_source_t * enc_src,
                             bg_media_source_t * src,
                             bg_plugin_handle_t * h);

void bg_media_encoder_finalize(bg_media_source_t * src_enc);


void bg_media_encoder_finalize(bg_media_source_t * src_enc);


/* Do one iteration (single- or multithread */
gavl_source_status_t bg_media_encoder_process_stream(bg_media_source_stream_t * st,
                                                     gavl_time_t time);

/* Do one singlethread iteration */
gavl_source_status_t bg_media_encoder_process(bg_media_source_t * src, gavl_time_t * time);



#endif // BG_MEDIACONNECTOR_H_INCLUDED

