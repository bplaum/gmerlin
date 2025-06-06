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
#include <stdlib.h>
#include <stdio.h>

#include <gmerlin/log.h>
#define LOG_DOMAIN "player.audio"

#include <gmerlin/player.h>
#include <playerprivate.h>

void bg_player_audio_create(bg_player_t * p)
  {
  bg_player_audio_stream_t * s = &p->audio_stream;
  
  bg_gavl_audio_options_init(&s->options);

  s->th = bg_thread_create(p->thread_common);
  
  s->fc =
    bg_audio_filter_chain_create(&s->options);
  
  
  s->volume = gavl_volume_control_create();
  s->peak_detector = gavl_peak_detector_create();
  gavl_peak_detector_set_callbacks(s->peak_detector, NULL,
                                   bg_player_peak_callback, p);
    
  pthread_mutex_init(&s->volume_mutex,NULL);
  pthread_mutex_init(&s->config_mutex,NULL);
  pthread_mutex_init(&s->time_mutex,NULL);
  pthread_mutex_init(&s->mute_mutex, NULL);
  pthread_mutex_init(&s->eof_mutex,NULL);
  
  s->timer = gavl_timer_create();
  }

void bg_player_audio_destroy(bg_player_t * p)
  {
  bg_player_audio_stream_t * s = &p->audio_stream;
  bg_gavl_audio_options_free(&s->options);
  bg_audio_filter_chain_destroy(s->fc);
  
  gavl_volume_control_destroy(s->volume);
  gavl_peak_detector_destroy(s->peak_detector);
  pthread_mutex_destroy(&s->volume_mutex);
  pthread_mutex_destroy(&s->eof_mutex);

  pthread_mutex_destroy(&s->time_mutex);
  gavl_timer_destroy(s->timer);
  
  if(s->plugin_handle)
    bg_plugin_unref(s->plugin_handle);

  if(s->sink_uri)
    free(s->sink_uri);
  
  bg_thread_destroy(s->th); 
  
  }

int bg_player_audio_init(bg_player_t * player, int audio_stream)
  {
  const gavl_dictionary_t * sd;
  gavl_stream_stats_t stats;
  
  bg_player_audio_stream_t * s;
  //  int do_filter;

  if(!DO_AUDIO(player->flags))
    return 1;
  
  s = &player->audio_stream;
  s->send_silence = 0;
  
  s->options.options_changed = 0;
  
  if(!bg_player_input_get_audio_format(player))
    return 0;

  pthread_mutex_lock(&s->config_mutex);
  s->src = bg_audio_filter_chain_connect(s->fc, s->in_src);
  pthread_mutex_unlock(&s->config_mutex);

  if(!s->src)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Initializing audio filters failed");
    return 0;
    }
  
  gavl_audio_format_copy(&s->output_format,
                         gavl_audio_source_get_src_format(s->src));
  
  if(!bg_player_oa_init(s))
    return 0;

  gavl_audio_source_set_dst(s->src, 0, &s->output_format);
    
  /* Volume control */
  gavl_volume_control_set_format(s->volume,
                                 &s->output_format);
  gavl_peak_detector_set_format(s->peak_detector,
                                &s->output_format);
  
  /* Initialize time */
  
  s->samples_written = 0;
  
  gavl_stream_stats_init(&stats);
  
  if((sd = gavl_track_get_audio_stream(player->src->track_info, audio_stream)) &&
     gavl_stream_get_stats(sd, &stats))
    {
    s->samples_written = stats.pts_start;

    /* Handle SBR */
    if(gavl_stream_is_sbr(sd))
      {
      s->samples_written *= 2;
      gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Got SBR");
      }
    
    }
  
  return 1;
  }

void bg_player_audio_cleanup(bg_player_t * player)
  {
  bg_player_audio_stream_t * s;
  s = &player->audio_stream;
  
  if(s->mute_frame)
    {
    gavl_audio_frame_destroy(s->mute_frame);
    s->mute_frame = NULL;
    }
  if(s->in_src)
    {
    gavl_audio_source_destroy(s->in_src);
    s->in_src = NULL;
    }
  }

/* Configuration stuff */

static const bg_parameter_info_t parameters[] =
  {
    BG_GAVL_PARAM_CONVERSION_QUALITY,
    BG_GAVL_PARAM_AUDIO_DITHER_MODE,
    BG_GAVL_PARAM_RESAMPLE_MODE,
    BG_GAVL_PARAM_CHANNEL_SETUP,
    { /* End of parameters */ }
  };



const bg_parameter_info_t * bg_player_get_audio_parameters(bg_player_t * p)
  {
  return parameters;
  }

const bg_parameter_info_t * bg_player_get_audio_filter_parameters(bg_player_t * p)
  {
  return bg_audio_filter_chain_get_parameters(p->audio_stream.fc);
  }

void bg_player_set_audio_parameter(void * data, const char * name,
                                   const gavl_value_t * val)
  {
  int state;
  bg_player_t * p = (bg_player_t*)data;
  int need_restart = 0;
  int is_interrupted;
  int do_init;
  int check_restart;

  state = bg_player_get_status(p);

  //  fprintf(stderr, "bg_player_set_audio_parameter: %d\n", state);
  
  do_init = (state == BG_PLAYER_STATUS_INIT);
  
  is_interrupted = (state == BG_PLAYER_STATUS_INTERRUPTED);
  
  bg_gavl_audio_set_parameter(&p->audio_stream.options,
                              name, val);

  if(!do_init && !is_interrupted)
    check_restart = 1;
  else
    check_restart = 0;
  
  if(check_restart)
    need_restart = p->audio_stream.options.options_changed;
  
  if(!need_restart && check_restart)
    {
    bg_audio_filter_chain_lock(p->audio_stream.fc);
    need_restart =
        bg_audio_filter_chain_need_restart(p->audio_stream.fc);
    bg_audio_filter_chain_unlock(p->audio_stream.fc);
    }

  if(state != BG_PLAYER_STATUS_PLAYING)
    need_restart = 0;
  
  if(need_restart)
    {
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN,
           "Restarting playback due to changed audio options");

    bg_player_stream_change_init(p);
    }
  
  }

void bg_player_set_audio_filter_parameter(void * data, const char * name,
                                          const gavl_value_t * val)
  {
  int state;
  int need_restart = 0;
  int is_interrupted;
  int do_init;
  bg_player_t * p = (bg_player_t*)data;

  state = bg_player_get_status(p);
  
  do_init = (state == BG_PLAYER_STATUS_INIT);
  is_interrupted = (state == BG_PLAYER_STATUS_INTERRUPTED);
  
  bg_audio_filter_chain_lock(p->audio_stream.fc);
  bg_audio_filter_chain_set_parameter(p->audio_stream.fc, name, val);

  need_restart =
    bg_audio_filter_chain_need_restart(p->audio_stream.fc);
  
  bg_audio_filter_chain_unlock(p->audio_stream.fc);
  
  if(!do_init && need_restart && !is_interrupted)
    {
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN,
           "Restarting playback due to changed audio filters");
    bg_player_stream_change_init(p);
    }
  
  }

void bg_player_handle_audio_filter_command(bg_player_t * p, gavl_msg_t * msg)
  {
  int state;
  int need_restart = 0;
  int is_interrupted;

  bg_msg_sink_t * sink;
  sink = bg_audio_filter_chain_get_cmd_sink(p->audio_stream.fc);
  state = bg_player_get_status(p);
  is_interrupted = (state == BG_PLAYER_STATUS_INTERRUPTED);
  
  bg_audio_filter_chain_lock(p->audio_stream.fc);
  bg_msg_sink_put_copy(sink, msg);

  need_restart =
    bg_audio_filter_chain_need_restart(p->audio_stream.fc);
  
  bg_audio_filter_chain_unlock(p->audio_stream.fc);

  if(need_restart && !is_interrupted)
    {
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN,
           "Restarting playback due to changed audio filters");
    bg_player_stream_change_init(p);
    }
  }

int bg_player_audio_set_eof(bg_player_t * p)
  {
  int ret = 1;

  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Detected EOF");
  pthread_mutex_lock(&p->video_stream.eof_mutex);
  pthread_mutex_lock(&p->audio_stream.eof_mutex);

  p->audio_stream.eof = 1;
  
  if(p->video_stream.eof || (gavl_track_get_duration(p->src->track_info) == GAVL_TIME_UNDEFINED))
    {
    bg_player_set_eof(p);
    }
  else
    {
    ret = 0;
    p->audio_stream.send_silence = 1;
    }
  pthread_mutex_unlock(&p->audio_stream.eof_mutex);
  pthread_mutex_unlock(&p->video_stream.eof_mutex);
  return ret;
  }
