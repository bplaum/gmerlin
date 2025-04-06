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



#include <string.h>

#include <gmerlin/mediaconnector.h>
#include <gmerlin/pluginregistry.h>
#include <gavl/log.h>

#define LOG_DOMAIN "mediasrcsink"

/*
 *   Values for the EOF status:
 *
 *   Resync
 *     -> Interrupt all threads, update time, restart everything
 *  
 *   EOF
 *     -> Gapless transition: Let output threads run
 *        -> Switch track
 *        -> New file
 *
 *     -> Transition with Gap: Reinit output threads
 *        -> Switch track
 *        -> New file
 */



void bg_media_source_init(bg_media_source_t * src)
  {
  memset(src, 0, sizeof(*src));
  }

void bg_media_source_cleanup(bg_media_source_t * src)
  {
  int i;

  for(i = 0; i < src->num_streams; i++)
    {
    if(src->streams[i]->asrc_priv)
      gavl_audio_source_destroy(src->streams[i]->asrc_priv);
    if(src->streams[i]->vsrc_priv)
      gavl_video_source_destroy(src->streams[i]->vsrc_priv);
    if(src->streams[i]->psrc_priv)
      gavl_packet_source_destroy(src->streams[i]->psrc_priv);
    if(src->streams[i]->msghub_priv)
      bg_msg_hub_destroy(src->streams[i]->msghub_priv);
    if(src->streams[i]->free_user_data && src->streams[i]->user_data)
      src->streams[i]->free_user_data(src->streams[i]->user_data);

    if(src->streams[i]->codec_handle)
      bg_plugin_unref(src->streams[i]->codec_handle);
    
    free(src->streams[i]);
    }
  if(src->streams)
    free(src->streams);

  if(src->free_user_data && src->user_data)
    src->free_user_data(src->user_data);

  if(src->track_priv)
    gavl_dictionary_destroy(src->track_priv);
  }

void bg_media_source_reset(bg_media_source_t * src)
  {
  int i;
  for(i = 0; i < src->num_streams; i++)
    {
    if(src->streams[i]->asrc)
      gavl_audio_source_reset(src->streams[i]->asrc);
    if(src->streams[i]->vsrc)
      gavl_video_source_reset(src->streams[i]->vsrc);
    if(src->streams[i]->psrc)
      gavl_packet_source_reset(src->streams[i]->psrc);
    }
  }

void bg_media_source_drain(bg_media_source_t * src)
  {
  int i;
  for(i = 0; i < src->num_streams; i++)
    {
    if(src->streams[i]->psrc)
      gavl_packet_source_drain(src->streams[i]->psrc);
    
    if(src->streams[i]->asrc)
      gavl_audio_source_drain(src->streams[i]->asrc);
    if(src->streams[i]->vsrc)
      gavl_video_source_drain(src->streams[i]->vsrc);
    }
  
  
  }


#if 0
void bg_media_source_set_eof(bg_media_source_t * src, int eof)
  {
  int i;
  for(i = 0; i < src->num_streams; i++)
    {
    if(src->streams[i]->asrc)
      gavl_audio_source_set_eof(src->streams[i]->asrc, eof);
    if(src->streams[i]->vsrc)
      gavl_video_source_set_eof(src->streams[i]->vsrc, eof);
    if(src->streams[i]->psrc)
      gavl_packet_source_set_eof(src->streams[i]->psrc, eof);
    }
  }
#endif

int bg_media_source_get_num_streams(const bg_media_source_t * src, gavl_stream_type_t type)
  {
  int i;
  int ret = 0;
  
  for(i = 0; i < src->num_streams; i++)
    {
    if(src->streams[i]->type == type)
      ret++;
    }
  return ret;
  }


bg_media_source_stream_t *
bg_media_source_append_stream(bg_media_source_t * src, gavl_stream_type_t type)
  {
  int idx;
  bg_media_source_stream_t * ret;

  idx = bg_media_source_get_num_streams(src, type);
  
  if(src->streams_alloc < src->num_streams + 1)
    {
    src->streams_alloc += 16;
    src->streams = realloc(src->streams, src->streams_alloc * sizeof(*src->streams));
    memset(src->streams + src->num_streams, 0, (src->streams_alloc - src->num_streams) *
           sizeof(*src->streams));
    }
  src->streams[src->num_streams] = calloc(1, sizeof(*src->streams[src->num_streams]));
  ret = src->streams[src->num_streams];
  src->num_streams++;

  ret->type = type;

  if(!(ret->s = gavl_track_get_stream_nc(src->track, type, idx)))
    ret->s = gavl_track_append_stream(src->track, type);
  
  gavl_stream_get_id(ret->s, &ret->stream_id);
    
  return ret;
  }

bg_media_source_stream_t *
bg_media_source_append_audio_stream(bg_media_source_t * src)
  {
  return bg_media_source_append_stream(src, GAVL_STREAM_AUDIO);
  }

bg_media_source_stream_t *
bg_media_source_append_video_stream(bg_media_source_t * src)
  {
  return bg_media_source_append_stream(src, GAVL_STREAM_VIDEO);
  }

bg_media_source_stream_t *
bg_media_source_append_text_stream(bg_media_source_t * src)
  {
  return bg_media_source_append_stream(src, GAVL_STREAM_TEXT);
  }

bg_media_source_stream_t *
bg_media_source_append_overlay_stream(bg_media_source_t * src)
  {
  return bg_media_source_append_stream(src, GAVL_STREAM_OVERLAY);
  }

bg_media_source_stream_t *
bg_media_source_append_msg_stream_by_id(bg_media_source_t * src, int id)
  {
  bg_media_source_stream_t * ret;
  ret = bg_media_source_append_stream(src, GAVL_STREAM_MSG);
  
  gavl_stream_set_id(ret->s, id);
  gavl_stream_get_id(ret->s, &ret->stream_id);
  
  return ret;
  }


void bg_media_source_set_from_source(bg_media_source_t * dst,
                                     bg_media_source_t * src)
  {
  int i;
  
  dst->streams = calloc(src->num_streams, sizeof(*dst->streams));
  
  dst->track_priv = gavl_dictionary_clone(src->track);
  dst->track = dst->track_priv;
  
  memcpy(dst->streams, src->streams, src->num_streams * sizeof(*dst->streams));
  dst->num_streams = src->num_streams;
  
  for(i = 0; i < dst->num_streams; i++)
    {
    dst->streams[i]->user_data = NULL;
    dst->streams[i]->free_user_data = NULL;
    
    dst->streams[i]->s = gavl_track_get_stream_all_nc(dst->track, i);
    
    /* Remove private members */
    dst->streams[i]->asrc_priv   = NULL;
    dst->streams[i]->vsrc_priv   = NULL;
    dst->streams[i]->psrc_priv   = NULL;
    dst->streams[i]->msghub_priv = NULL;
    }
  
  }

int bg_media_source_set_from_track(bg_media_source_t * src,
                                   gavl_dictionary_t * track)
  {
  gavl_stream_type_t type;
  int i, num;
  gavl_dictionary_t * s;
  bg_media_source_stream_t * src_s;

  src->track = track;
  
  num = gavl_track_get_num_streams_all(track);

  for(i = 0; i < num; i++)
    {
    if(!(s = gavl_track_get_stream_all_nc(track, i)) ||
       ((type = gavl_stream_get_type(s)) == GAVL_STREAM_NONE))
      return 0;
    src_s = bg_media_source_append_stream(src, type);
    src_s->s = s;

    if(type == GAVL_STREAM_MSG)
      gavl_stream_get_id(src_s->s, &src_s->stream_id);
    
    }
  return 1;
  }
  
bg_media_source_stream_t * bg_media_source_get_stream(bg_media_source_t * src, int type, int idx)
  {
  int i;
  int cnt = 0;
  
  for(i = 0; i < src->num_streams; i++)
    {
    if(type == src->streams[i]->type)
      {
      if(cnt == idx)
        return src->streams[i];
      else
        cnt++;
      }
    }
  return NULL;
  }

bg_media_source_stream_t * bg_media_source_get_stream_by_id(bg_media_source_t * src, int id)
  {
  int i;
  
  for(i = 0; i < src->num_streams; i++)
    {
    if(src->streams[i]->stream_id == id)
      return src->streams[i];
    }
  return NULL;
  
  }
  
bg_media_source_stream_t * bg_media_source_get_audio_stream(bg_media_source_t * src, int idx)
  {
  return bg_media_source_get_stream(src, GAVL_STREAM_AUDIO, idx);
  }

bg_media_source_stream_t * bg_media_source_get_video_stream(bg_media_source_t * src, int idx)
  {
  return bg_media_source_get_stream(src, GAVL_STREAM_VIDEO, idx);
  }

bg_media_source_stream_t * bg_media_source_get_text_stream(bg_media_source_t * src, int idx)
  {
  return bg_media_source_get_stream(src, GAVL_STREAM_TEXT, idx);
  }

bg_media_source_stream_t * bg_media_source_get_overlay_stream(bg_media_source_t * src, int idx)
  {
  return bg_media_source_get_stream(src, GAVL_STREAM_OVERLAY, idx);
  }

bg_media_source_stream_t * bg_media_source_get_msg_stream_by_id(bg_media_source_t * src, int id)
  {
  return bg_media_source_get_stream_by_id(src, id);
  }


gavl_audio_source_t * bg_media_source_get_audio_source(bg_media_source_t * src, int idx)
  {
  bg_media_source_stream_t * s;

  if(!(s = bg_media_source_get_audio_stream(src, idx)))
    return NULL;
  return s->asrc;
  }

gavl_video_source_t * bg_media_source_get_video_source(bg_media_source_t * src, int idx)
  {
  bg_media_source_stream_t * s;
  if(!(s = bg_media_source_get_video_stream(src, idx)))
    return NULL;
  return s->vsrc;
  
  }

gavl_video_source_t * bg_media_source_get_overlay_source(bg_media_source_t * src, int idx)
  {
  bg_media_source_stream_t * s;
  if(!(s = bg_media_source_get_overlay_stream(src, idx)))
    return NULL;
  return s->vsrc;

  }

gavl_packet_source_t * bg_media_source_get_audio_packet_source(bg_media_source_t * src, int idx)
  {
  bg_media_source_stream_t * s;
  if(!(s = bg_media_source_get_audio_stream(src, idx)))
    return NULL;
  return s->psrc;
  }

gavl_packet_source_t * bg_media_source_get_video_packet_source(bg_media_source_t * src, int idx)
  {
  bg_media_source_stream_t * s;
  if(!(s = bg_media_source_get_video_stream(src, idx)))
    return NULL;
  return s->psrc;
  
  }

gavl_packet_source_t * bg_media_source_get_text_source(bg_media_source_t * src, int idx)
  {
  bg_media_source_stream_t * s;
  if(!(s = bg_media_source_get_text_stream(src, idx)))
    return NULL;
  return s->psrc;
  
  }

gavl_packet_source_t * bg_media_source_get_overlay_packet_source(bg_media_source_t * src, int idx)
  {
  bg_media_source_stream_t * s;
  if(!(s = bg_media_source_get_overlay_stream(src, idx)))
    return NULL;
  return s->psrc;
 
  }

gavl_packet_source_t * bg_media_source_get_msg_packet_source_by_id(bg_media_source_t * src, int id)
  {
  bg_media_source_stream_t * s;
  if(!(s = bg_media_source_get_msg_stream_by_id(src, id)))
    return NULL;
  return s->psrc;
  }

bg_msg_hub_t * bg_media_source_get_msg_hub_by_id(bg_media_source_t * src, int id)
  {
  bg_media_source_stream_t * s;
  if(!(s = bg_media_source_get_msg_stream_by_id(src, id)))
    return NULL;
  return s->msghub;
  }

static bg_plugin_handle_t * load_decoder_plugin(gavl_dictionary_t * s, uint32_t type_mask)
  {
  const bg_plugin_info_t * info;
  bg_plugin_handle_t * ret = NULL;
  gavl_compression_info_t ci;

  gavl_compression_info_init(&ci);
  gavl_stream_get_compression_info(s, &ci);
  
  if(!(info = bg_plugin_find_by_compression(ci.id,
                                            ci.codec_tag,
                                            type_mask)))
    {
    if(ci.codec_tag)
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Cannot find decompressor for tag %c%c%c%c",
               (ci.codec_tag >> 24) & 0xff,
               (ci.codec_tag >> 16) & 0xff,
               (ci.codec_tag >> 8) & 0xff,
               ci.codec_tag & 0xff);
    else
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Cannot find decompressor for %s",
               gavl_compression_get_long_name(ci.id));

    gavl_dictionary_dump(s, 2);

    goto fail;
    }
  
  ret = bg_plugin_load(info);
  
  fail:
  gavl_compression_info_free(&ci);
  
  return ret;
  
  }

int bg_media_source_load_decoders(bg_media_source_t * src)
  {
  int i;
  bg_codec_plugin_t * plugin;

  bg_media_source_stream_t * s;
  
  for(i = 0; i < src->num_streams; i++)
    {
    s = src->streams[i];
    switch(s->type)
      {
      case GAVL_STREAM_AUDIO:
        if((s->action == BG_STREAM_ACTION_DECODE) &&
           (!s->asrc && s->psrc))
          {
          if((s->codec_handle = load_decoder_plugin(s->s, BG_PLUGIN_DECOMPRESSOR_AUDIO)))
            {
            plugin = (bg_codec_plugin_t *)s->codec_handle->plugin;
            s->asrc = plugin->open_decode_audio(s->codec_handle->priv, s->psrc, s->s);
            }
          }
        break;
      case GAVL_STREAM_VIDEO:
        if((src->streams[i]->action == BG_STREAM_ACTION_DECODE) &&
           (!src->streams[i]->vsrc && src->streams[i]->psrc))
          {
          if((src->streams[i]->codec_handle = load_decoder_plugin(s->s, BG_PLUGIN_DECOMPRESSOR_VIDEO)))
            {
            plugin = (bg_codec_plugin_t *)s->codec_handle->plugin;
            s->vsrc = plugin->open_decode_video(s->codec_handle->priv, s->psrc, s->s);
            }
          }
        break;
      case GAVL_STREAM_OVERLAY:
        if((src->streams[i]->action == BG_STREAM_ACTION_DECODE) &&
           (!src->streams[i]->vsrc && src->streams[i]->psrc))
          {
          if((src->streams[i]->codec_handle = load_decoder_plugin(s->s, BG_PLUGIN_DECOMPRESSOR_VIDEO)))
            {
            plugin = (bg_codec_plugin_t *)s->codec_handle->plugin;
            s->vsrc = plugin->open_decode_overlay(s->codec_handle->priv, s->psrc, s->s);
            }
          }
        break;
      default:
        break;
      }
    
    }
  return 1;
  }

int bg_media_source_set_stream_action(bg_media_source_t * src, gavl_stream_type_t type, int idx,
                                       bg_stream_action_t action)
  {
  bg_media_source_stream_t * s;

  if(!(s = bg_media_source_get_stream(src, type, idx)))
    return 0;
  s->action = action;
  return 1;
  }

int bg_media_source_set_audio_action(bg_media_source_t * src, int idx,
                                     bg_stream_action_t action)
  {
  return bg_media_source_set_stream_action(src, GAVL_STREAM_AUDIO, idx, action);
  }

int bg_media_source_set_video_action(bg_media_source_t * src, int idx,
                                     bg_stream_action_t action)
  {
  return bg_media_source_set_stream_action(src, GAVL_STREAM_VIDEO, idx, action);
  
  }

int bg_media_source_set_text_action(bg_media_source_t * src, int idx,
                                    bg_stream_action_t action)
  {
  return bg_media_source_set_stream_action(src, GAVL_STREAM_TEXT, idx, action);
  
  }

int bg_media_source_set_overlay_action(bg_media_source_t * src, int idx,
                                       bg_stream_action_t action)
  {
  return bg_media_source_set_stream_action(src, GAVL_STREAM_OVERLAY, idx, action);
  
  }

int bg_media_source_set_msg_action_by_id(bg_media_source_t * src, int id,
                                         bg_stream_action_t action)
  {
  bg_media_source_stream_t * s;
  
  if(!(s = bg_media_source_get_msg_stream_by_id(src, id)))
    return 0;
  
  s->action = action;
  return 1;
  }

