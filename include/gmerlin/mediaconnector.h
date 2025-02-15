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



#endif // BG_MEDIACONNECTOR_H_INCLUDED

