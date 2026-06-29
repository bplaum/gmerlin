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

#include <gmerlin/mediaconnector.h>
#include <gmerlin/pluginregistry.h>
#include <gavl/log.h>
#include <gmerlin/filters.h>
#include <gavl/utils.h>

#define LOG_DOMAIN "mediaencoder"

static gavl_source_status_t process_audio(bg_media_source_stream_t * st, gavl_time_t t);
static gavl_source_status_t process_video(bg_media_source_stream_t * st, gavl_time_t t);
static gavl_source_status_t process_video_noncont(bg_media_source_stream_t * st, gavl_time_t t);
static gavl_source_status_t process_packet(bg_media_source_stream_t * st, gavl_time_t t);
static gavl_source_status_t process_packet_noncont(bg_media_source_stream_t * st, gavl_time_t t);

int bg_media_encoder_init(bg_media_source_t * src,
                          bg_plugin_handle_t * h)
  {
  bg_media_source_stream_t * st;

  int i;
  
  int min_audio_streams;
  int min_video_streams;
  int min_text_streams;
  int min_overlay_streams;

  int max_audio_streams;
  int max_video_streams;
  int max_text_streams;
  int max_overlay_streams;

  int num_audio_streams   = 0;
  int num_video_streams   = 0;
  int num_text_streams    = 0;
  int num_overlay_streams = 0;
  const gavl_audio_format_t * afmt;
  const gavl_video_format_t * vfmt;
  gavl_compression_info_t ci;
  
  bg_encoder_plugin_t * encoder_plugin = (bg_encoder_plugin_t*)h->plugin;
  
  
  max_audio_streams   = bg_plugin_info_get_max_audio_streams(h->info);
  max_video_streams   = bg_plugin_info_get_max_video_streams(h->info);
  max_text_streams    = bg_plugin_info_get_max_text_streams(h->info);
  max_overlay_streams = bg_plugin_info_get_max_overlay_streams(h->info);

  min_audio_streams   = bg_plugin_info_get_min_audio_streams(h->info);
  min_video_streams   = bg_plugin_info_get_min_video_streams(h->info);
  min_text_streams    = bg_plugin_info_get_min_text_streams(h->info);
  min_overlay_streams = bg_plugin_info_get_min_overlay_streams(h->info);

  
  /* Check for unsupported scenarios */

  if((bg_media_source_get_num_streams(src, GAVL_STREAM_AUDIO) < min_audio_streams) ||
     (bg_media_source_get_num_streams(src, GAVL_STREAM_VIDEO) < min_video_streams) ||
     (bg_media_source_get_num_streams(src, GAVL_STREAM_TEXT) < min_text_streams) ||
     (bg_media_source_get_num_streams(src, GAVL_STREAM_OVERLAY) < min_overlay_streams))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Nothing to encode");
    return 0;
    }
  
  for(i = 0; i < src->num_streams; i++)
    {
    st = src->streams[i];

    if(st->action == BG_STREAM_ACTION_OFF)
      continue;
      
    switch(st->type)
      {
      case GAVL_STREAM_AUDIO:
        {
        if((max_audio_streams >= 0) && (num_audio_streams >= max_audio_streams))
          {
          gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Dropping audio stream (not supported by encoder)");
          st->action = BG_STREAM_ACTION_OFF;
          continue;
          }
        num_audio_streams++;
        
        if(st->action == BG_STREAM_ACTION_DECODE)
          {
          gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Decompressing audio stream as requested by user");
          continue;
          }

        afmt = gavl_stream_get_audio_format(st->s);
        
        gavl_compression_info_init(&ci);
        gavl_stream_get_compression_info(st->s, &ci);
        if(ci.id != GAVL_CODEC_ID_NONE)
          {
          if(encoder_plugin->writes_compressed_audio &&
             encoder_plugin->writes_compressed_audio(h->priv, afmt, &ci))
            {
            gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Copying compressed audio stream (%s)",
                     gavl_compression_get_long_name(ci.id));
            st->action = BG_STREAM_ACTION_READRAW;
            }
          else
            {
            gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Decompressing audio stream (%s)",
                     gavl_compression_get_long_name(ci.id));
            st->action = BG_STREAM_ACTION_DECODE;
            }
          }
        else
          {
          gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Got uncompressed audio stream");
          st->action = BG_STREAM_ACTION_DECODE;
          }
        gavl_compression_info_free(&ci);
        }
        break;
      case GAVL_STREAM_VIDEO:
        {
        if((max_video_streams >= 0) && (num_video_streams >= max_video_streams))
          {
          gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Dropping video stream (not supported by encoder)");
          st->action = BG_STREAM_ACTION_OFF;
          continue;
          }
        num_video_streams++;

        if(st->action == BG_STREAM_ACTION_DECODE)
          {
          gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Decompressing video stream as requested by user");
          continue;
          }

        
        vfmt = gavl_stream_get_video_format(st->s);
        gavl_compression_info_init(&ci);
        gavl_stream_get_compression_info(st->s, &ci);
        if(ci.id != GAVL_CODEC_ID_NONE)
          {
          fprintf(stderr, "Got compressed video stream: %s\n",
                  gavl_compression_get_long_name(ci.id));

          if(encoder_plugin->writes_compressed_video &&
             encoder_plugin->writes_compressed_video(h->priv,
                                                     vfmt, &ci))
            {
            gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Copying compressed video stream (%s)",
                     gavl_compression_get_long_name(ci.id));
            st->action = BG_STREAM_ACTION_READRAW;
            }
          else
            {
            gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Decompressing video stream (%s)",
                     gavl_compression_get_long_name(ci.id));
            st->action = BG_STREAM_ACTION_DECODE;
            }
          }
        else
          {
          fprintf(stderr, "Got uncompressed video stream\n");
          st->action = BG_STREAM_ACTION_DECODE;
          }
        gavl_compression_info_free(&ci);
        }
        break;
      case GAVL_STREAM_TEXT:
        if((max_text_streams >= 0) && (num_text_streams >= max_text_streams))
          {
          gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Dropping text stream (not supported by encoder)");
          st->action = BG_STREAM_ACTION_OFF;
          continue;
          }
        num_text_streams++;
        st->action = BG_STREAM_ACTION_DECODE;
        break;
      case GAVL_STREAM_OVERLAY:
        {
        if((max_overlay_streams >= 0) && (num_overlay_streams >= max_overlay_streams))
          {
          gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Dropping overlay stream (not supported by encoder)");
          st->action = BG_STREAM_ACTION_OFF;
          continue;
          }
        
        num_overlay_streams++;

        if(st->action == BG_STREAM_ACTION_DECODE)
          {
          gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Decompressing overlay stream as requested by user");
          continue;
          }
        
        gavl_compression_info_init(&ci);
        gavl_stream_get_compression_info(st->s, &ci);
        vfmt = gavl_stream_get_video_format(st->s);

        if(ci.id != GAVL_CODEC_ID_NONE)
          {
          fprintf(stderr, "Got compressed overlay stream: %s\n",
                  gavl_compression_get_long_name(ci.id));
          
          if(encoder_plugin->writes_compressed_overlay &&
             encoder_plugin->writes_compressed_overlay(h->priv,
                                                       vfmt, &ci))
            {
            gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Copying compressed overlay stream (%s)",
                     gavl_compression_get_long_name(ci.id));
            }
          else
            {
            gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Decompressing overlay stream (%s)",
                     gavl_compression_get_long_name(ci.id));
            st->action = BG_STREAM_ACTION_DECODE;
            }
          }
        else
          {
          fprintf(stderr, "Got uncompressed overlay stream\n");
          st->action = BG_STREAM_ACTION_DECODE;
          }
        gavl_compression_info_free(&ci);
        }
        break;
      case GAVL_STREAM_MSG:
        if(encoder_plugin->add_msg_stream)
          st->action = BG_STREAM_ACTION_DECODE;
        else
          st->action = BG_STREAM_ACTION_OFF;
        break;
      case GAVL_STREAM_NONE:
        break;
      }
    }

  return num_audio_streams +
    num_video_streams +
    num_text_streams +
    num_overlay_streams;
  
  }

static void free_encoder(void * priv)
  {
  bg_encoder_t * s = priv;
  pthread_mutex_destroy(&s->mutex);
  free(s);
  }

static bg_encoder_t * get_common(bg_media_source_t * src)
  {
  bg_encoder_t * ret;

  if(src->user_data)
    return src->user_data;
  
  ret = calloc(1, sizeof(*ret));

  pthread_mutex_init(&ret->mutex, NULL);

  ret->time = GAVL_TIME_UNDEFINED;
  
  src->user_data = ret;
  src->free_user_data = free_encoder;
  return src->user_data;
  }

static void free_encoder_stream(void * priv)
  {
  bg_encoder_stream_t * s = priv;
  
  gavl_compression_info_free(&s->ci);
  free(s);
  }

bg_encoder_stream_t * bg_encoder_stream_create(bg_media_source_t * src,
                                               bg_media_source_stream_t * st)
  {
  bg_encoder_stream_t * ret = calloc(1, sizeof(*ret));
  ret->com = get_common(src);
  st->user_data = ret;
  st->free_user_data = free_encoder_stream;
  gavl_stream_stats_init(&ret->stats);
  return ret;
  }

static gavl_dictionary_t * get_stream_config(gavl_dictionary_t * s, const char * tag,
                                             const gavl_parameter_info_t * params)
  {
  const gavl_dictionary_t * cfg;
  
  if((cfg = bg_track_get_config(s, tag)))
    return gavl_dictionary_clone(cfg);

  return bg_cfg_section_create_from_parameters("section", params);
  }

typedef struct
  {
  void (*func)(void * data, int index, const char * name,
               const gavl_value_t*val);
  bg_encoder_stream_t * s;
  void * priv;
  } set_stream_encoder_parameter_t;

static void set_stream_encoder_param(void * priv, const char * name,
                                     const gavl_value_t * val)
  {
  set_stream_encoder_parameter_t * s = priv;
  s->func(s->priv, s->s->idx, name, val);
  }

void bg_media_encoder_finalize(bg_media_source_t * src_enc)
  {
  int i;
  bg_media_source_stream_t * st;
  bg_encoder_stream_t * s;
  
  for(i = 0; i < src_enc->num_streams; i++)
    {
    st = src_enc->streams[i];

    if(st->action == BG_STREAM_ACTION_OFF)
      continue;

    s = st->user_data;
      
    switch(st->type)
      {
      case GAVL_STREAM_AUDIO:
        {
        const gavl_audio_format_t * afmt;
        if(st->asrc)
          {
          afmt = gavl_audio_sink_get_format(s->asink);
          gavl_audio_source_set_dst(st->asrc, 0, afmt);
          s->process = process_audio;
          
          }
        else if(st->psrc)
          {
          afmt = gavl_stream_get_audio_format(st->s);
          
          s->process = process_packet;
          
          }
        s->dst_scale = afmt->samplerate;
        }
        break;
      case GAVL_STREAM_VIDEO:
        {
        const gavl_video_format_t * vfmt;
        
        if(st->vsrc)
          {
          vfmt = gavl_video_sink_get_format(s->vsink);

          gavl_video_source_set_dst(st->vsrc, 0, vfmt);
          
          
          if(vfmt->framerate_mode == GAVL_FRAMERATE_STILL)
            {
            s->flags |= BG_ENCODER_STREAM_NONCONT;
            s->process = process_video_noncont;
            }
          else
            s->process = process_video;
            
          }
        else if(st->psrc)
          {
          vfmt = gavl_stream_get_video_format(st->s);
          if(vfmt && (vfmt->framerate_mode == GAVL_FRAMERATE_STILL))
            {
            s->flags |= BG_ENCODER_STREAM_NONCONT;
            s->process = process_packet_noncont;
            }
          else
            s->process = process_packet;

          if(s->ci.flags & GAVL_COMPRESSION_HAS_B_FRAMES)
            {
            s->flags |= BG_ENCODER_STREAM_DELAY;
            
            }
          }
        s->dst_scale = vfmt->timescale;
        }
        break;
      case GAVL_STREAM_TEXT:
        {
        const gavl_dictionary_t * m;
        
        if(!s->src_scale && (m = gavl_stream_get_metadata(st->s)))
          gavl_dictionary_get_int(m, GAVL_META_STREAM_SAMPLE_TIMESCALE,
                                  &s->src_scale);
        
        s->process = process_packet_noncont;
        s->flags |= BG_ENCODER_STREAM_NONCONT;
        }
        break;
      case GAVL_STREAM_OVERLAY:
        {
        const gavl_video_format_t * vfmt;
        
        s->flags |= BG_ENCODER_STREAM_NONCONT;

        if(st->vsrc)
          {
          vfmt = gavl_video_sink_get_format(s->vsink);
          gavl_video_source_set_dst(st->vsrc, 0, vfmt);
          
          s->process = process_video_noncont;
          
          }
        else if(st->psrc)
          {
          vfmt = gavl_stream_get_video_format(st->s);
          s->process = process_packet_noncont;
          
          }
        if(vfmt)
          s->dst_scale = vfmt->timescale;
        }
        break;
      case GAVL_STREAM_MSG:
        break;
      case GAVL_STREAM_NONE:
        break;
      }
    }
  }

int bg_media_encoder_connect(bg_media_source_t * enc_src,
                             bg_media_source_t * src,
                             bg_plugin_handle_t * h)
  {
  int i;
  bg_media_source_stream_t * st;
  bg_encoder_stream_t * s;
  bg_encoder_plugin_t * enc = (bg_encoder_plugin_t*)h->plugin;
  const gavl_parameter_info_t * p;
  gavl_dictionary_t * cfg;

  set_stream_encoder_parameter_t sep;

  bg_media_source_set_from_source(enc_src, src);
  
  /* Add streams */
  
  for(i = 0; i < enc_src->num_streams; i++)
    {
    st = enc_src->streams[i];

    if(st->action == BG_STREAM_ACTION_OFF)
      continue;
      
    switch(st->type)
      {
      case GAVL_STREAM_AUDIO:
        s = bg_encoder_stream_create(enc_src, st);
        if(st->asrc)
          {
          s->idx = enc->add_audio_stream(h->priv,
                                         gavl_stream_get_metadata(st->s),
                                         gavl_audio_source_get_src_format(st->asrc));
          /* Apply parameters */

          if(enc->get_audio_parameters && (p = enc->get_audio_parameters(h->priv)))
            {
            sep.func = enc->set_audio_parameter;
            sep.priv = h->priv;
            sep.s = s;
            cfg = get_stream_config(st->s, BG_TRACK_CONFIG_ENCODER, p);

            bg_cfg_section_apply(cfg, p, set_stream_encoder_param, &sep);
            }
          
          
          }
        else if(st->psrc)
          {

          gavl_stream_get_compression_info(st->s, &s->ci);

          s->idx = enc->add_audio_stream_compressed(h->priv,
                                                    gavl_stream_get_metadata(st->s),
                                                    gavl_stream_get_audio_format(st->s),
                                                    &s->ci);
          }
        
        break;
      case GAVL_STREAM_VIDEO:
        {
        s = bg_encoder_stream_create(enc_src, st);
        
        if(st->vsrc)
          {
          s->idx = enc->add_video_stream(h->priv,
                                         gavl_stream_get_metadata(st->s),
                                         gavl_video_source_get_src_format(st->vsrc));

          /* Apply parameters */

          if(enc->get_video_parameters && (p = enc->get_video_parameters(h->priv)))
            {
            sep.func = enc->set_video_parameter;
            sep.priv = h->priv;
            sep.s = s;
            cfg = get_stream_config(st->s, BG_TRACK_CONFIG_ENCODER, p);

            bg_cfg_section_apply(cfg, p, set_stream_encoder_param, &sep);
            
            }

          }
        else if(st->psrc)
          {
          gavl_stream_get_compression_info(st->s, &s->ci);

          s->idx = enc->add_video_stream_compressed(h->priv,
                                                    gavl_stream_get_metadata(st->s),
                                                    gavl_stream_get_video_format(st->s),
                                                    &s->ci);
          
          }
        }

        break;
      case GAVL_STREAM_TEXT:
        {
        const gavl_dictionary_t * m;
        s = bg_encoder_stream_create(enc_src, st);
        
        if((m = gavl_stream_get_metadata(st->s)))
          gavl_dictionary_get_int(m, GAVL_META_STREAM_SAMPLE_TIMESCALE,
                                  &s->src_scale);
        s->dst_scale = s->src_scale;
        
        s->idx = enc->add_text_stream(h->priv,
                                      gavl_stream_get_metadata(st->s), &s->dst_scale);
        }
        break;
      case GAVL_STREAM_OVERLAY:
        s = bg_encoder_stream_create(enc_src, st);

        if(st->vsrc)
          {
          
          s->idx = enc->add_overlay_stream(h->priv,
                                           gavl_stream_get_metadata(st->s),
                                           gavl_video_source_get_src_format(st->vsrc));
          
          /* Apply parameters */

          if(enc->get_overlay_parameters && (p = enc->get_overlay_parameters(h->priv)))
            {
            sep.func = enc->set_overlay_parameter;
            sep.priv = h->priv;
            sep.s = s;
            cfg = get_stream_config(st->s, BG_TRACK_CONFIG_ENCODER, p);
            bg_cfg_section_apply(cfg, p, set_stream_encoder_param, &sep);
            }
          }
        else if(st->psrc)
          {
          gavl_compression_info_t ci;
          gavl_compression_info_init(&ci);

          gavl_stream_get_compression_info(st->s, &ci);

          s->idx = enc->add_overlay_stream_compressed(h->priv,
                                                      gavl_stream_get_metadata(st->s),
                                                      gavl_stream_get_video_format(st->s), &ci);
          
          gavl_compression_info_free(&ci);
          }
        
        s->flags |= BG_ENCODER_STREAM_NONCONT;
        
        break;
      case GAVL_STREAM_MSG:
        s = bg_encoder_stream_create(enc_src, st);
        //        s->flags |= BG_ENCODER_STREAM_NONCONT;
        s->msink = enc->add_msg_stream(h->priv, GAVL_META_STREAM_ID_MSG_PROGRAM);
        break;
      default:
        break;
      }
    }

  /* Start encoder */
  if(enc->start && !enc->start(h->priv))
    return 0;

  /* Get sinks */
  
  for(i = 0; i < src->num_streams; i++)
    {
    st = enc_src->streams[i];
    
    if(st->action == BG_STREAM_ACTION_OFF)
      continue;
    
    switch(st->type)
      {
      case GAVL_STREAM_AUDIO:
        s = st->user_data;
        if(st->asrc)
          s->asink = enc->get_audio_sink(h->priv, s->idx);
        else
          s->psink = enc->get_audio_packet_sink(h->priv, s->idx);
        break;
      case GAVL_STREAM_VIDEO:
        s = st->user_data;
        if(st->vsrc)
          s->vsink = enc->get_video_sink(h->priv, s->idx);
        else
          s->psink = enc->get_video_packet_sink(h->priv, s->idx);
          
        break;
      case GAVL_STREAM_TEXT:
        s = st->user_data;
        if(st->psrc)
          s->psink = enc->get_text_sink(h->priv, s->idx);
        
        break;
      case GAVL_STREAM_OVERLAY:
        s = st->user_data;
        if(st->vsrc)
          s->vsink = enc->get_overlay_sink(h->priv, s->idx);
        else
          s->psink = enc->get_overlay_packet_sink(h->priv, s->idx);
        
        break;
      case GAVL_STREAM_MSG:
        s = st->user_data;
        break;
      case GAVL_STREAM_NONE:
        break;
      }
    }

  /* Negotiate formats */
  bg_media_encoder_finalize(enc_src);
  
  return 1;
  }

static gavl_source_status_t process_audio(bg_media_source_stream_t * st, gavl_time_t t)
  {
  gavl_source_status_t result;
  bg_encoder_stream_t * s = st->user_data;

  gavl_audio_frame_t * f = gavl_audio_sink_get_frame(s->asink);
  result = gavl_audio_source_read_frame(st->asrc, &f);
  if(result != GAVL_SOURCE_OK)
    {
    /* TODO: Undo get_frame? */
          
    return result;
    }

  gavl_stream_stats_update_params(&s->stats, f->timestamp, f->valid_samples,
                                  0, 0);
  
  s->time = gavl_time_unscale(s->dst_scale, f->timestamp + f->valid_samples);
        
  if(gavl_audio_sink_put_frame(s->asink, f) == GAVL_SINK_OK)
    return GAVL_SOURCE_OK;
  else
    return GAVL_SOURCE_EOF;
  }

static gavl_source_status_t process_video(bg_media_source_stream_t * st, gavl_time_t t)
  {
  gavl_source_status_t result;
  bg_encoder_stream_t * s = st->user_data;
  
  gavl_video_frame_t * f = gavl_video_sink_get_frame(s->vsink);
  result = gavl_video_source_read_frame(st->vsrc, &f);
  if(result != GAVL_SOURCE_OK)
    {
    /* TODO: Undo get_frame? */
    //    fprintf(stderr, "Video EOF\n");
    //    gavl_stream_stats_dump(&s->stats, 2);
    return result;
    }

  s->time = gavl_time_unscale(s->dst_scale, f->timestamp + f->duration);

  gavl_stream_stats_update_params(&s->stats, f->timestamp, f->duration,
                                  0, 0);

  
  if(gavl_video_sink_put_frame(s->vsink, f) == GAVL_SINK_OK)
    return GAVL_SOURCE_OK;
  else
    return GAVL_SOURCE_EOF;
  }


static gavl_source_status_t process_packet(bg_media_source_stream_t * st, gavl_time_t t_unused)
  {
  gavl_source_status_t result;
  bg_encoder_stream_t * s = st->user_data;

  gavl_time_t t;

  if(!(s->p))
    {
    s->p = gavl_packet_sink_get_packet(s->psink);
    result = gavl_packet_source_read_packet(st->psrc, &s->p);
    
    if(result != GAVL_SOURCE_OK)
      {
      /* TODO: Undo get_frame? */
          
      return result;
      }
    }
  
  t = gavl_time_unscale(s->dst_scale, s->p->pts + s->p->duration);

  gavl_stream_stats_update_params(&s->stats, s->p->pts, s->p->duration,
                                  s->p->buf.len, s->p->flags);

  
  if(gavl_packet_sink_put_packet(s->psink, s->p) != GAVL_SINK_OK)
    {
    s->p = NULL;
    return GAVL_SOURCE_EOF;
    }

  s->p = NULL;
  
  if(s->flags & BG_ENCODER_STREAM_DELAY)
    {
    if(t > s->time)
      s->time = t;

    /* Flush B-frames */
          
    while(1)
      {

      s->p =
        gavl_packet_sink_get_packet(s->psink);
      result = gavl_packet_source_read_packet(st->psrc, &s->p);

      if(result != GAVL_SOURCE_OK)
        {
        /* TODO: Undo get_frame? */
          
        return result;
        }

      if((s->p->flags & GAVL_PACKET_TYPE_MASK) != GAVL_PACKET_TYPE_B)
        {
        break;
        }

      if(gavl_packet_sink_put_packet(s->psink, s->p) != GAVL_SINK_OK)
        {
        s->p = NULL;
        return GAVL_SOURCE_EOF;
        }

      s->p = NULL;
      }
          
    }
  else
    s->time = t;
  
  return GAVL_SOURCE_OK;
  }

static gavl_source_status_t process_packet_noncont(bg_media_source_stream_t * st, gavl_time_t t)
  {
  gavl_source_status_t result;
  bg_encoder_stream_t * s = st->user_data;

  if(!(s->flags & BG_ENCODER_GOT_SINK_FRAME))
    {
    s->p = gavl_packet_sink_get_packet(s->psink);
    s->flags |= BG_ENCODER_GOT_SINK_FRAME;
    }

  if(!(s->flags & BG_ENCODER_GOT_SRC_FRAME))
    {
    result = gavl_packet_source_read_packet(st->psrc, &s->p);
    
    switch(result)
      {
      case GAVL_SOURCE_OK:
        s->flags |= BG_ENCODER_GOT_SRC_FRAME;
        s->time = gavl_time_unscale(s->dst_scale, s->p->pts);
        break;
      case GAVL_SOURCE_EOF:
        return result;
        break;
      case GAVL_SOURCE_AGAIN:
        return result;
        break;
      }
    }

  if(!(s->flags & BG_ENCODER_GOT_SRC_FRAME) ||
     ((t != GAVL_TIME_UNDEFINED) && (s->time - t > GAVL_TIME_SCALE/2)))
    return GAVL_SOURCE_AGAIN;

    
  if(gavl_packet_sink_put_packet(s->psink, s->p) == GAVL_SINK_OK)
    result = GAVL_SOURCE_OK;
  else
    result = GAVL_SOURCE_EOF;

  gavl_stream_stats_update_params(&s->stats, s->p->pts, s->p->duration,
                                  s->p->buf.len, s->p->flags);
  
  s->flags &= ~(BG_ENCODER_GOT_SINK_FRAME|BG_ENCODER_GOT_SRC_FRAME);
  s->p = NULL;
  return result;
  
  }

static gavl_source_status_t process_video_noncont(bg_media_source_stream_t * st, gavl_time_t t)
  {
  gavl_source_status_t result;
  bg_encoder_stream_t * s = st->user_data;

  if(!(s->flags & BG_ENCODER_GOT_SINK_FRAME))
    {
    s->vframe = gavl_video_sink_get_frame(s->vsink);
    s->flags |= BG_ENCODER_GOT_SINK_FRAME;
    }
  
  if(!(s->flags & BG_ENCODER_GOT_SRC_FRAME))
    {
    result = gavl_video_source_read_frame(st->vsrc, &s->vframe);
    
    switch(result)
      {
      case GAVL_SOURCE_OK:
        s->flags |= BG_ENCODER_GOT_SRC_FRAME;
        s->time = gavl_time_unscale(s->dst_scale, s->vframe->timestamp);
        break;
      case GAVL_SOURCE_EOF:
        return result;
        break;
      case GAVL_SOURCE_AGAIN:
        return result;
        break;
      }
    }

  if(!(s->flags & BG_ENCODER_GOT_SRC_FRAME) ||
     ((t != GAVL_TIME_UNDEFINED) && (s->time - t > GAVL_TIME_SCALE/2)))
    return GAVL_SOURCE_AGAIN;
  
  if(gavl_video_sink_put_frame(s->vsink, s->vframe) == GAVL_SINK_OK)
    result = GAVL_SOURCE_OK;
  else
    result = GAVL_SOURCE_EOF;

  gavl_stream_stats_update_params(&s->stats, s->vframe->timestamp, s->vframe->duration,
                                  0, 0);
  
  s->flags &= ~(BG_ENCODER_GOT_SINK_FRAME|BG_ENCODER_GOT_SRC_FRAME);
  s->vframe = NULL;
  return result;
  }


gavl_source_status_t bg_media_encoder_process_stream(bg_media_source_stream_t * st,
                                                     gavl_time_t t)
  {
  gavl_source_status_t result = GAVL_SOURCE_OK;
  bg_encoder_stream_t * s = st->user_data;


  result = s->process(st, t);

  if(result == GAVL_SOURCE_EOF)
    s->flags |= BG_ENCODER_STREAM_EOF;
  
  return result;
  }

/* Do one singlethread iteration */
gavl_source_status_t bg_media_encoder_process(bg_media_source_t * src, gavl_time_t * time)
  {
  int i;
  bg_media_source_stream_t * st;
  bg_encoder_stream_t * s;
  /* Find stream with smallest timestamp */
  int min_idx = -1;
  gavl_time_t min_time = GAVL_TIME_UNDEFINED;
  gavl_time_t max_time = GAVL_TIME_UNDEFINED;

  for(i = 0; i < src->num_streams; i++)
    {
    st = src->streams[i];
    if(st->action == BG_STREAM_ACTION_OFF)
      continue;
    
    s = st->user_data;
    if(s->flags & BG_ENCODER_STREAM_NONCONT)
      continue;

    if((max_time == GAVL_TIME_UNDEFINED) ||
       (s->time > max_time))
      max_time = s->time;
    
    if(s->flags & BG_ENCODER_STREAM_EOF)
      continue;
    
    if((min_idx < 0) || (s->time < min_time))
      {
      min_idx = i;
      min_time = s->time;
      }
    
    }
  if(min_idx < 0)
    return GAVL_SOURCE_EOF;

  /* Process stream */
  
  bg_media_encoder_process_stream(src->streams[min_idx], GAVL_TIME_UNDEFINED);
  
  s = src->streams[min_idx]->user_data;
  
  if(s->time > max_time)
    max_time = s->time;
    
  for(i = 0; i < src->num_streams; i++)
    {
    st = src->streams[i];
    if(st->action == BG_STREAM_ACTION_OFF)
      continue;
    
    s = st->user_data;
    if((s->flags & (BG_ENCODER_STREAM_NONCONT | BG_ENCODER_STREAM_EOF)) !=
       BG_ENCODER_STREAM_NONCONT)
      continue;
    
    bg_media_encoder_process_stream(src->streams[i], max_time);
    }
  
  if(time)
    *time = max_time;
    
  return GAVL_SOURCE_OK;
  }

static void * encoder_stream_thread(void * data)
  {
  gavl_source_status_t result;
  bg_media_source_stream_t * st = data;
  bg_encoder_stream_t * s = st->user_data;

  pthread_barrier_wait(&s->com->barrier);
  
  
  while(1)
    {
    pthread_mutex_lock(&s->com->mutex);
    if(s->com->state != BG_ENCODER_STATE_RUNNING)
      result = GAVL_SOURCE_EOF;
    pthread_mutex_unlock(&s->com->mutex);

    if(result == GAVL_SOURCE_EOF)
      break;

    result = bg_media_encoder_process_stream(st, GAVL_TIME_UNDEFINED);

    if(result == GAVL_SOURCE_EOF)
      break;
    }
  
  pthread_mutex_lock(&s->com->mutex);
  s->com->num_threads--;
  if(!s->com->num_threads)
    s->com->state = BG_ENCODER_STATE_EOF; 
  
  pthread_mutex_unlock(&s->com->mutex);
  return NULL;
  
  } 


void bg_media_encoder_start(bg_media_source_t * src)
  {
  /* Count threads */
  int i;
  bg_encoder_stream_t * s;
  bg_encoder_t * enc = src->user_data;

  for(i = 0; i < src->num_streams; i++)
    {
    if((src->streams[i]->action == BG_STREAM_ACTION_OFF) ||
       (src->streams[i]->type == GAVL_STREAM_MSG))
      continue;
    enc->num_threads++;
    }

  enc->state = BG_ENCODER_STATE_RUNNING;
  
  pthread_barrier_init(&enc->barrier, NULL, enc->num_threads + 1);
  
  for(i = 0; i < src->num_streams; i++)
    {
    if((src->streams[i]->action == BG_STREAM_ACTION_OFF) ||
       (src->streams[i]->type == GAVL_STREAM_MSG))
      continue;
    
    s = src->streams[i]->user_data;
    pthread_create(&s->th, NULL, encoder_stream_thread, src->streams[i]);
    }

  pthread_barrier_wait(&enc->barrier);
  pthread_barrier_destroy(&enc->barrier);
  }

void bg_media_encoder_stop(bg_media_source_t * src)
  {
  int i;
  bg_encoder_stream_t * s;

  bg_encoder_t * enc = src->user_data;
  pthread_mutex_lock(&enc->mutex);
  enc->state = BG_ENCODER_STATE_STOP;
  pthread_mutex_unlock(&enc->mutex);

  for(i = 0; i < src->num_streams; i++)
    {
    if((src->streams[i]->action == BG_STREAM_ACTION_OFF) ||
       (src->streams[i]->type == GAVL_STREAM_MSG))
      continue;
    
    s = src->streams[i]->user_data;
    pthread_join(s->th, NULL);
    }
  
  }

int bg_media_encoder_eof(bg_media_source_t * src)
  {
  int ret = 0;
  bg_encoder_t * enc = src->user_data;

  pthread_mutex_lock(&enc->mutex);
  if(enc->state == BG_ENCODER_STATE_EOF)
    ret = 1;
  pthread_mutex_unlock(&enc->mutex);
  return ret;
  }

static void dump_stats_type(bg_media_source_t * src, gavl_stream_type_t type)
  {
  int i, num;

  bg_encoder_stream_t * s;
  bg_media_source_stream_t * st;
  
  num = bg_media_source_get_num_streams(src, type);
  
  for(i = 0; i < num; i++)
    {
    st = bg_media_source_get_stream(src, type, i);

    if(!(s = st->user_data))
      continue;
    
    gavl_dprintf("%s stream %d:\n", gavl_stream_type_name(type), i+1);
    gavl_stream_stats_dump(&s->stats, 2);
    gavl_dprintf("\n");
    }
  
  }

void bg_media_encoder_dump_stats(bg_media_source_t * src)
  {
  dump_stats_type(src, GAVL_STREAM_AUDIO);
  dump_stats_type(src, GAVL_STREAM_VIDEO);
  dump_stats_type(src, GAVL_STREAM_TEXT);
  dump_stats_type(src, GAVL_STREAM_OVERLAY);
  }
