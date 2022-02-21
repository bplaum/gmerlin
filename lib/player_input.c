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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#include <gmerlin/player.h>
#include <playerprivate.h>
#include <gmerlin/log.h>

#include <gavl/metatags.h>

#define LOG_DOMAIN "player.input"

// #define DUMP_TIMESTAMPS

#define DEBUG_COUNTER

/* Send messages about the URL */

struct stream_info_s
  {
  const gavl_dictionary_t * track;
  int index;
  };

/* */

void bg_player_input_destroy(bg_player_t * player)
  {
  bg_player_source_cleanup(&player->srcs[0]);
  bg_player_source_cleanup(&player->srcs[1]);
  }

int bg_player_input_start(bg_player_t * p)
  {
  int num_audio_streams;
  int num_video_streams;
  int num_text_streams;
  int num_overlay_streams;

  bg_media_source_t * ms;
  bg_media_source_stream_t * s = NULL;
  
  bg_player_audio_stream_t * as = &p->audio_stream;
  bg_player_video_stream_t * vs = &p->video_stream;
  bg_player_subtitle_stream_t * ss = &p->subtitle_stream;

  //  fprintf(stderr, "bg_player_input_start\n");

  num_audio_streams   = gavl_track_get_num_audio_streams(p->src->track_info);
  num_video_streams   = gavl_track_get_num_video_streams(p->src->track_info);
  num_text_streams    = gavl_track_get_num_text_streams(p->src->track_info);
  num_overlay_streams = gavl_track_get_num_overlay_streams(p->src->track_info);
  
  if(!p->video_stream.ov)
    {
    p->video_stream_user = -1;
    p->subtitle_stream_user = -1;
    }
  
  /* Check if the streams are actually there */
  p->flags &= 0xFFFF0000;
  
  as->eof = 1;
  vs->eof = 1;
  ss->eof = 1;
  
  if((p->src->audio_stream >= 0) &&
     (p->src->audio_stream < num_audio_streams))
    {
    as->eof = 0;
    p->flags |= PLAYER_DO_AUDIO;
    }
  if((p->src->video_stream >= 0) &&
     (p->src->video_stream < num_video_streams))
    {
    vs->eof = 0;
    p->flags |= PLAYER_DO_VIDEO;
    }

  if((p->src->subtitle_stream >= 0) &&
     (p->src->subtitle_stream <
      num_text_streams + num_overlay_streams))
    {
    p->flags |= PLAYER_DO_SUBTITLE;
    ss->eof = 0;
    }
  
  if(DO_AUDIO(p->flags) &&
     !DO_VIDEO(p->flags) &&
     !DO_SUBTITLE(p->flags) &&
     (p->visualization_mode >= 0))
    {
    p->flags |= PLAYER_DO_VISUALIZE;
    }
  else if(!DO_VIDEO(p->flags) &&
          DO_SUBTITLE(p->flags))
    {
    double col[3];
    p->flags |= PLAYER_DO_VIDEO;
    vs->eof = 0;
    
    p->flags |= PLAYER_DO_SUBTITLE_ONLY;
    
    pthread_mutex_lock(&p->video_stream.config_mutex);
    /* Get background color */
    gavl_video_options_get_background_color(p->video_stream.options.opt, col);
    
    pthread_mutex_unlock(&p->video_stream.config_mutex);

    vs->bg_color[0] = col[0];
    vs->bg_color[1] = col[1];
    vs->bg_color[2] = col[2];
    vs->bg_color[3] = 1.0;
    }

  if(DO_SUBTITLE(p->flags))
    {
    if(p->src->subtitle_stream < num_text_streams)
      p->flags |= PLAYER_DO_SUBTITLE_TEXT;
    else
      p->flags |= PLAYER_DO_SUBTITLE_OVERLAY;
    }
  /* Check for still image mode */
  
  if(p->flags & PLAYER_DO_VIDEO)
    {
    const gavl_video_format_t * video_format = 
      gavl_track_get_video_format(p->src->track_info, p->src->video_stream);
    
    if(video_format->framerate_mode == GAVL_FRAMERATE_STILL)
      p->flags |= PLAYER_DO_STILL;
    }
  p->audio_stream.has_first_timestamp_i = 0;
  
  if((ms = p->src->input_plugin->get_src(p->src->input_handle->priv)) &&
     (s = bg_media_source_get_stream_by_id(ms, GAVL_META_STREAM_ID_MSG_PROGRAM)) &&
     (s->msghub))
    {
    bg_msg_hub_connect_sink(s->msghub, p->src_msg_sink);
    }
  else
    {
    fprintf(stderr, "Cannot connect input messages %p\n", s);
    }
  
  
  /* */
  
  p->can_seek = p->src->can_seek;
  p->can_pause = p->src->can_pause;
  
  /* From here on, we can send the messages about the input format */
  bg_player_set_current_track(p, p->src->track_info);
  return 1;
  }

void bg_player_input_cleanup(bg_player_t * p)
  {
#ifdef DEBUG_COUNTER
  char tmp_string[128];
#endif
  bg_player_source_cleanup(p->src);
#ifdef DEBUG_COUNTER
  sprintf(tmp_string, "%" PRId64, p->audio_stream.samples_read);
  
  gavl_log(GAVL_LOG_DEBUG, LOG_DOMAIN, "Audio sample counter: %s",
         tmp_string);

  p->audio_stream.samples_read = 0;
  
  sprintf(tmp_string, "%" PRId64, p->video_stream.frames_read);
  
  gavl_log(GAVL_LOG_DEBUG, LOG_DOMAIN, "Video frame counter: %s",
         tmp_string);
  p->video_stream.frames_read = 0;
#endif
  }

static gavl_source_status_t
read_audio(void * priv, gavl_audio_frame_t ** frame)
  {
  gavl_source_status_t st;
  bg_player_t * p = priv;
  gavl_audio_frame_t * f = NULL;
  bg_player_audio_stream_t * as = &p->audio_stream;

  if((st = gavl_audio_source_read_frame(p->audio_stream.in_src_int, &f)) != GAVL_SOURCE_OK)
    {
    if(bg_player_advance_gapless(p))
      {
      as->has_first_timestamp_i = 0;
      if((st = gavl_audio_source_read_frame(p->audio_stream.in_src_int, &f)) != GAVL_SOURCE_OK)
        {
        return st;
        }
      }
    else
      return st;
    }
  if(!as->has_first_timestamp_i)
    {
    as->samples_read = f->timestamp;
    as->has_first_timestamp_i = 1;
    }

  //  fprintf(stderr, "Samples read: %"PRId64" Timestamp: %"PRId64"\n", as->samples_read, f->timestamp);

  as->samples_read += f->valid_samples;
  *frame = f;
  return GAVL_SOURCE_OK;
  }

int bg_player_input_get_audio_format(bg_player_t * p)
  {
  if(!p->src->audio_src)
    return 0;
  
  p->audio_stream.in_src_int = p->src->audio_src;
  
  gavl_audio_format_copy(&p->audio_stream.input_format,
                         gavl_audio_source_get_src_format(p->audio_stream.in_src_int));

  gavl_audio_source_set_dst(p->audio_stream.in_src_int, 0, &p->audio_stream.input_format);
  p->audio_stream.in_src = gavl_audio_source_create(read_audio, p, GAVL_SOURCE_SRC_ALLOC |
                                                    GAVL_SOURCE_SRC_FRAMESIZE_MAX,
                                                    &p->audio_stream.input_format);


  
  return 1;
  }

/* Video input */

static gavl_source_status_t
read_video_still(void * priv, gavl_video_frame_t ** frame)
  {
  gavl_source_status_t st;

  bg_player_t * p = priv;
  bg_player_video_stream_t * vs = &p->video_stream;
  const gavl_video_format_t * format;
  
  format = gavl_video_source_get_dst_format(vs->in_src_int);
  
  if((st = gavl_video_source_read_frame(vs->in_src_int, frame)) != GAVL_SOURCE_OK)
    return st;
  
  if(!DO_AUDIO(p->flags) &&
     (p->src->duration != GAVL_TIME_UNDEFINED) &&
     gavl_time_unscale(format->timescale,
                       (*frame)->timestamp) > p->src->duration)
    return GAVL_SOURCE_EOF;
  
#ifdef DUMP_TIMESTAMPS
  bg_debug("Input timestamp: %"PRId64" (timescale: %d)\n",
           gavl_time_unscale(format->timescale,
                             (*frame)->timestamp), format->timescale);
#endif
  return st;
  }

static gavl_source_status_t
read_video_subtitle_only(void * priv,
                         gavl_video_frame_t ** frame)
  {
  bg_player_t * p = priv;
  bg_player_video_stream_t * vs = &p->video_stream;
  
  gavl_video_frame_fill(*frame, &vs->output_format, vs->bg_color);

  (*frame)->duration  = vs->output_format.frame_duration;
  
  (*frame)->timestamp = (int64_t)vs->frames_read * vs->output_format.frame_duration;
  vs->frames_read++;
  return GAVL_SOURCE_OK;
  }

static gavl_source_status_t
read_video(void * priv, gavl_video_frame_t ** frame)
  {
  gavl_source_status_t st;
  bg_player_t * p = priv;
  bg_player_video_stream_t * vs = &p->video_stream;
  
  /* Skip */
#if 0 // Disabled for now because it was done the wrong way anyway
  if((vs->skip > 1000000) && p->input_plugin->skip_video)
    {
    gavl_time_t skip_time = 
      vs->frame_time + vs->skip;

    pthread_mutex_lock(&vs->config_mutex);
    
    if(vs->do_skip)
      {
      gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Skipping frame, delta: %f",
             gavl_time_to_seconds(vs->skip));

      bg_plugin_lock(p->input_handle);
      p->input_plugin->skip_video(p->input_handle->priv, p->current_video_stream,
                                  &skip_time, GAVL_TIME_SCALE, 0);
      bg_plugin_unlock(p->input_handle);
      }
    pthread_mutex_unlock(&vs->config_mutex);
    }
#endif
  if((st = gavl_video_source_read_frame(vs->in_src_int, frame)) != GAVL_SOURCE_OK)
    return st;

#ifdef DUMP_TIMESTAMPS
  bg_debug("Input timestamp: %"PRId64"\n",
           gavl_time_unscale(vs->input_format.timescale,
                              (*frame)->timestamp));
#endif
  vs->frames_read++;
  
  return st;
  }

int bg_player_input_get_video_format(bg_player_t * p)
  {
  if(!p->src->video_src)
    return 0;
  
  p->video_stream.in_src_int = p->src->video_src;
  
  gavl_video_source_support_hw(p->video_stream.in_src_int);
  
  gavl_video_format_copy(&p->video_stream.input_format,
                         gavl_video_source_get_src_format(p->video_stream.in_src_int));
  
  if(DO_STILL(p->flags))
    {
    gavl_video_format_t fmt;

    gavl_video_format_copy(&fmt, &p->video_stream.input_format);
    
    fmt.timescale = GAVL_TIME_SCALE;
    fmt.framerate_mode = GAVL_FRAMERATE_CONSTANT;
    
    pthread_mutex_lock(&p->config_mutex);
    fmt.frame_duration =
      (int)((float)GAVL_TIME_SCALE / p->still_framerate);
    pthread_mutex_unlock(&p->config_mutex);

    gavl_video_source_set_dst(p->video_stream.in_src_int, 0, &fmt);
    
    p->video_stream.in_src = gavl_video_source_create(read_video_still, p, GAVL_SOURCE_SRC_ALLOC, &fmt);
    }
  else if(DO_SUBTITLE_ONLY(p->flags))
    {
    gavl_video_source_set_dst(p->video_stream.in_src_int, 0, &p->video_stream.input_format);
    p->video_stream.in_src = gavl_video_source_create(read_video_subtitle_only, p, 0,
                                                      &p->video_stream.input_format);
    }

  
  else
    {
    gavl_video_source_set_dst(p->video_stream.in_src_int, 0, &p->video_stream.input_format);
    p->video_stream.in_src = gavl_video_source_create(read_video, p, GAVL_SOURCE_SRC_ALLOC,
                                                      &p->video_stream.input_format);
    }

  
  return 1;
  }



void bg_player_input_seek(bg_player_t * p,
                          gavl_time_t * time, int scale)
  {
  int do_audio, do_video, do_subtitle;

  bg_player_audio_stream_t * as;
  bg_player_video_stream_t * vs;
  bg_player_subtitle_stream_t * ss;

  as = &p->audio_stream;
  vs = &p->video_stream;
  ss = &p->subtitle_stream;

  //  fprintf(stderr, "Seek: %ld (%d)", *time, scale);
  
  //  bg_plugin_lock(p->input_handle);

  bg_input_plugin_seek(p->src->input_handle, time, scale);
  //  bg_plugin_unlock(p->input_handle);

  //  fprintf(stderr, " %ld\n", *time);
  
  as->samples_read =
    gavl_time_to_samples(as->input_format.samplerate, *time);

  // This wasn't set before if we switch streams or replug filters
  as->has_first_timestamp_i = 1;
  
  if(DO_SUBTITLE_ONLY(p->flags))
    vs->frames_read =
      gavl_time_to_frames(vs->output_format.timescale, vs->output_format.frame_duration,
                          *time);
  else
    {
    vs->frames_written =
      gavl_time_to_frames(vs->input_format.timescale, vs->input_format.frame_duration,
                          *time);
    }

  if(vs->in_src)
    gavl_video_source_reset(vs->in_src);
  if(as->in_src)
    gavl_audio_source_reset(as->in_src);
  
  // Clear EOF states
  do_audio = DO_AUDIO(p->flags);
  do_video = DO_VIDEO(p->flags);
  do_subtitle = DO_SUBTITLE(p->flags);
  
  // fprintf(stderr, "Seek, do video: %d\n", do_video);
  
  ss->eof = !do_subtitle;
  as->eof = !do_audio;
  vs->eof = !do_video;

  as->send_silence = 0;
  vs->skip = 0;
  }


/* Configuration stuff */

static const bg_parameter_info_t parameters[] =
  {
    {
      .name =        "still_framerate",
      .long_name =   "Still image repitition rate",
      .type =        BG_PARAMETER_FLOAT,
      .val_default = GAVL_VALUE_INIT_FLOAT(10.0),
      .val_min =     GAVL_VALUE_INIT_FLOAT(0.5),
      .val_max =     GAVL_VALUE_INIT_FLOAT(100.0),
      .help_string = TRS("When showing still images, gmerlin repeats them periodically to make realtime filter tweaking work."),
    },
    {
      .name =        "sync_offset",
      .long_name =   "Sync offset [ms]",
      .type      =   BG_PARAMETER_SLIDER_INT,
      .flags     =   BG_PARAMETER_SYNC,
      .val_default = GAVL_VALUE_INIT_INT(0),
      .val_min =     GAVL_VALUE_INIT_INT(-1000),
      .val_max =     GAVL_VALUE_INIT_INT(1000),
      .help_string = TRS("Use this for playing buggy files, which have a constant offset between audio and video. Use positive values if the video is ahead of audio"),
    },
    { /* End of parameters */ }
  };


const bg_parameter_info_t * bg_player_get_input_parameters(bg_player_t * p)
  {
  return parameters;
  }

void bg_player_set_input_parameter(void * data, const char * name,
                                   const gavl_value_t * val)
  {
  bg_player_t * player = (bg_player_t*)data;

  if(!name)
    return;

  pthread_mutex_lock(&player->config_mutex);
  if(!strcmp(name, "still_framerate"))
    player->still_framerate = val->v.d;
  else if(!strcmp(name, "sync_offset"))
    player->sync_offset = gavl_time_unscale(1000, val->v.i);

  pthread_mutex_unlock(&player->config_mutex);
  }


/* Source functions */

void bg_player_source_select_streams(bg_player_t * player,
                                     bg_player_source_t * src)
  {
  int i;
  int idx, is_text;

  int num_audio_streams;
  int num_video_streams;
  int num_text_streams;
  int num_overlay_streams;

  num_audio_streams   =
    gavl_track_get_num_audio_streams(src->track_info);
  num_video_streams   =
    gavl_track_get_num_video_streams(src->track_info);
  num_text_streams    =
    gavl_track_get_num_text_streams(src->track_info);
  num_overlay_streams =
    gavl_track_get_num_overlay_streams(src->track_info);
  
  src->audio_stream = player->audio_stream_user;
  src->video_stream = player->video_stream_user;
  src->subtitle_stream = player->subtitle_stream_user;
  
  /* Adjust stream indices */
  if(src->audio_stream >= num_audio_streams)
    src->audio_stream = 0;

  if(src->video_stream >= num_video_streams)
    src->video_stream = 0;
  
  if(src->subtitle_stream >= num_text_streams + num_overlay_streams)
    src->subtitle_stream = 0;
  
  if(!player->audio_stream.plugin_handle)
    src->audio_stream = -1;

  if(!player->video_stream.ov)
    {
    src->video_stream = -1;
    src->subtitle_stream = -1;
    }

  /* Select streams */
  
  /* En-/Disable streams at the input plugin */
  
  for(i = 0; i < num_audio_streams; i++)
    {
    if(i == src->audio_stream) 
      bg_media_source_set_audio_action(src->input_handle->src, i,
                                       BG_STREAM_ACTION_DECODE);
    else
      bg_media_source_set_audio_action(src->input_handle->src, i,
                                       BG_STREAM_ACTION_OFF);
    }

  for(i = 0; i < num_video_streams; i++)
    {
    if(i == src->video_stream) 
      bg_media_source_set_video_action(src->input_handle->src, i,
                                       BG_STREAM_ACTION_DECODE);
    else
      bg_media_source_set_video_action(src->input_handle->src, i,
                                       BG_STREAM_ACTION_OFF);
    }
  
  idx = bg_player_get_subtitle_index(src->track_info,
                                     src->subtitle_stream,
                                     &is_text);
  
  for(i = 0; i < num_text_streams; i++)
    {
    if(is_text && (idx == i))
      bg_media_source_set_text_action(src->input_handle->src, i,
                                      BG_STREAM_ACTION_DECODE);
    else
      bg_media_source_set_text_action(src->input_handle->src, i,
                                      BG_STREAM_ACTION_OFF);
    }

  for(i = 0; i < num_overlay_streams; i++)
    {
    if(!is_text && (idx == i))
      bg_media_source_set_overlay_action(src->input_handle->src, i,
                                         BG_STREAM_ACTION_DECODE);
    else
      bg_media_source_set_overlay_action(src->input_handle->src, i,
                                         BG_STREAM_ACTION_OFF);
    }
  }

int bg_player_source_set_from_handle(bg_player_t * player, bg_player_source_t * src,
                                     bg_plugin_handle_t * h, int track_index)
  {
  const char * var;
  
  gavl_dictionary_t * m1;
  gavl_dictionary_t * m2;

  int ret = 0;

  src->input_handle = h;
  src->input_plugin = (bg_input_plugin_t*)src->input_handle->plugin;

  src->track_info = bg_input_plugin_get_track_info(src->input_handle, track_index);
  
  /* From DB (has priority) */
  m1 = gavl_track_get_metadata_nc(&src->track);

  /*  */
  if((var = gavl_dictionary_get_string(m1, GAVL_META_MEDIA_CLASS)) &&
     !strcmp(var, GAVL_META_MEDIA_CLASS_LOCATION))
    gavl_dictionary_set(m1, GAVL_META_MEDIA_CLASS, NULL);
  
  if(m1) /* Merge metadata from DB */
    {
    gavl_dictionary_t m;

    gavl_dictionary_init(&m);
    
    /* From plugin */
    m2 = gavl_track_get_metadata_nc(src->track_info);
    gavl_dictionary_merge(&m, m1, m2);

    gavl_dictionary_free(m2);
    gavl_dictionary_move(m2, &m);
    }
  
  src->metadata = gavl_track_get_metadata(src->track_info);
  src->chapterlist = gavl_dictionary_get_chapter_list(src->metadata);
  
  if((track_index >= 0) && !bg_input_plugin_set_track(src->input_handle, track_index))
    goto fail;


  if(src->input_plugin->common.get_controllable)
    src->input_ctrl = src->input_plugin->common.get_controllable(src->input_handle->priv);
  
  /* Select streams */
  bg_player_source_select_streams(player, src);
  
  /* Start */

  if(!bg_player_source_start(player, src))
    goto fail;
  
  src->duration = gavl_track_get_duration(src->track_info);
  
  if(gavl_track_can_seek(src->track_info) &&
     (src->duration != GAVL_TIME_UNDEFINED))
    src->can_seek = 1;
  else
    src->can_seek = 0;
  
  if(gavl_track_can_pause(src->track_info))
    src->can_pause = 1;
  else
    src->can_pause = 0;
  
  if(!gavl_track_get_num_audio_streams(src->track_info) &&
     !gavl_track_get_num_video_streams(src->track_info))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN,
            "Track has neither audio nor video, skipping");
    goto fail;
    }
  
  /* Get metadata */
  
  // gavl_dictionary_set_int(&src->m, GAVL_META_CAN_SEEK, !!src->can_seek);
  // gavl_dictionary_set_int(&src->m, GAVL_META_CAN_PAUSE, !!src->can_pause);
  
  ret = 1;
  
  fail:

  if(!ret)
    bg_player_source_cleanup(src);

  return ret;
  
  }

void bg_player_source_close(bg_player_source_t * src)
  {
  if(src->input_handle)
    bg_plugin_unref(src->input_handle);
  
  src->input_plugin = NULL;
  src->track_info = NULL;
  src->audio_src = NULL;
  src->video_src = NULL;
  src->text_src = NULL;
  src->ovl_src = NULL;
  src->input_handle = NULL;
  
  }

int bg_player_handle_input_message(void * priv, gavl_msg_t * msg)
  {
  bg_player_t * p = priv;

  switch(msg->NS)
    {
    case GAVL_MSG_NS_SRC:
      switch(msg->ID)
        {
        case GAVL_MSG_SRC_METADATA_CHANGED:
          {
          gavl_dictionary_t * m_dst;
          const gavl_dictionary_t * m_new;
          gavl_dictionary_t tmp;
          
          if(!p->src || !p->src->track_info)
            return 1;
          
          // fprintf(stderr, "player metadata changed\n");
          
          m_dst = gavl_track_get_metadata_nc(p->src->track_info);
          m_new = gavl_value_get_dictionary_nc(gavl_msg_get_arg_nc(msg, 0));
          
          gavl_dictionary_init(&tmp);
          gavl_dictionary_merge(&tmp, m_new, m_dst);
          
          gavl_dictionary_free(m_dst);
          gavl_dictionary_move(m_dst, &tmp);
          
          bg_player_set_current_track(p, p->src->track_info);
          }
          break;
        case GAVL_MSG_SRC_RESYNC:
          {
          int64_t msg_time;
          int msg_scale;
          // int discard;

          gavl_msg_get_src_resync(msg, &msg_time, &msg_scale, NULL, NULL);
          
          bg_player_set_resync(p, msg_time, msg_scale);
          }


        }
      break;
      
    }
  
  return 1;
  }


void bg_player_source_cleanup(bg_player_source_t * src)
  {
  
  bg_player_source_close(src);

  if(src->location)
    free(src->location);
  
  gavl_dictionary_free(&src->track);
  gavl_dictionary_free(&src->url_vars);
  memset(src, 0, sizeof(*src));
  }

void bg_player_source_stop(bg_player_t * player, bg_player_source_t * p)
  {
  if(p->input_plugin && p->input_plugin->stop)
    p->input_plugin->stop(p->input_handle->priv);
  
  }

int bg_player_source_start(bg_player_t * player, bg_player_source_t * src)
  {
  bg_media_source_t * ms;
  
  int num_audio_streams;
  int num_video_streams;
  int num_text_streams;
  int num_overlay_streams;
  
  num_audio_streams   = gavl_track_get_num_audio_streams(src->track_info);
  num_video_streams   = gavl_track_get_num_video_streams(src->track_info);
  num_text_streams    = gavl_track_get_num_text_streams(src->track_info);
  num_overlay_streams = gavl_track_get_num_overlay_streams(src->track_info);
  
  bg_input_plugin_start(src->input_handle);
  
  ms = src->input_plugin->get_src(src->input_handle->priv);
  
  if((src->audio_stream >= 0) && (src->audio_stream < num_audio_streams))
    src->audio_src = bg_media_source_get_audio_source(ms, src->audio_stream);
  
  if((src->video_stream >= 0) && (src->video_stream < num_video_streams))
    src->video_src = bg_media_source_get_video_source(ms, src->video_stream);
  
  if((src->subtitle_stream >= 0) && (src->subtitle_stream < num_overlay_streams + num_text_streams))
    {
    int is_text = 0;
    int idx = bg_player_get_subtitle_index(src->track_info, src->subtitle_stream, &is_text);
    
    if(is_text)
      src->text_src = bg_media_source_get_text_source(ms, idx);
    else
      src->ovl_src = bg_media_source_get_overlay_source(ms, idx);
    }
  return 1;
  }
