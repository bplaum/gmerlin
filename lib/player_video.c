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
#include <stdio.h>

#include <gavl/metatags.h>


#include <gmerlin/player.h>
#include <playerprivate.h>

#include <gmerlin/log.h>
#define LOG_DOMAIN "player.video"

void bg_player_video_create(bg_player_t * p)
  {
  bg_player_video_stream_t * s = &p->video_stream;
  
  s->th = bg_thread_create(p->thread_common);
  
  bg_gavl_video_options_init(&s->options);

  s->fc = bg_video_filter_chain_create(&s->options);
  
  pthread_mutex_init(&s->config_mutex,NULL);
  pthread_mutex_init(&s->eof_mutex,NULL);
  s->ss = &p->subtitle_stream;
  
  s->accel_map = bg_accelerator_map_create();
  s->accel_map_ov = bg_accelerator_map_create();
  s->osd = bg_osd_create(&p->ctrl);
  s->sh = bg_subtitle_handler_create();
  }

void bg_player_video_destroy(bg_player_t * p)
  {
  bg_player_video_stream_t * s = &p->video_stream;
  pthread_mutex_destroy(&s->config_mutex);
  pthread_mutex_destroy(&s->eof_mutex);
  bg_gavl_video_options_free(&s->options);
  bg_video_filter_chain_destroy(s->fc);
  bg_thread_destroy(s->th);
  
  bg_osd_destroy(s->osd);
  bg_accelerator_map_destroy(s->accel_map);
  bg_accelerator_map_destroy(s->accel_map_ov);
  bg_subtitle_handler_destroy(s->sh);

  if(s->sink_uri)
    free(s->sink_uri);
  
  }



int bg_player_video_init(bg_player_t * player, int video_stream)
  {
  bg_player_video_stream_t * s;
  s = &player->video_stream;

  s->skip = 0;
  s->frames_read = 0;

  
  if(!DO_VIDEO(player->flags))
    return 1;
  
  if(!bg_player_input_get_video_format(player))
    return 0;

  s->src = s->in_src_int;

  //  s->vi = &player->src->track_info->video_streams[video_stream];
  
  if(!DO_SUBTITLE_ONLY(player->flags))
    s->src = bg_video_filter_chain_connect(s->fc, s->src);

  if(!s->src)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Initializing video filters failed");
    return 0;
    }
  
  if(!bg_player_ov_init(&player->video_stream))
    return 0;
  
  if(!DO_SUBTITLE_ONLY(player->flags))
    gavl_video_source_set_dst(s->src, 0, &s->output_format);

  /* Read first video frame(s). Here we skip initial video frames if the
     audio stream starts later */

  
  
#if 0  
  if(DO_SUBTITLE_ONLY(player->flags))
    {
    /* Video output already initialized */
    bg_player_ov_set_subtitle_format(&player->video_stream);

    bg_player_subtitle_init_converter(player);
    
    s->in_func = bg_player_input_read_video_subtitle_only;
    s->in_data = player;
    s->in_stream = 0;
    }
#endif
  return 1;
  }

void bg_player_video_cleanup(bg_player_t * player)
  {
  
  //  s->vi = NULL;
  }

void bg_player_handle_video_filter_command(bg_player_t * p, gavl_msg_t * msg)
  {
  int state;
  int need_restart = 0;
  int is_interrupted;

  bg_msg_sink_t * sink;
  sink = bg_video_filter_chain_get_cmd_sink(p->video_stream.fc);
  state = bg_player_get_status(p);
  is_interrupted = (state == BG_PLAYER_STATUS_INTERRUPTED);
  
  bg_video_filter_chain_lock(p->video_stream.fc);
  bg_msg_sink_put_copy(sink, msg);

  need_restart =
    bg_video_filter_chain_need_restart(p->video_stream.fc);
  
  bg_video_filter_chain_unlock(p->video_stream.fc);

  if(need_restart && !is_interrupted)
    {
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN,
           "Restarting playback due to changed video filters");
    bg_player_stream_change_init(p);
    }
  }


/* Configuration stuff */

static const bg_parameter_info_t parameters[] =
  {
    BG_GAVL_PARAM_CONVERSION_QUALITY,
    BG_GAVL_PARAM_ALPHA,
    BG_GAVL_PARAM_RESAMPLE_CHROMA,
    {
      .name = "skip",
      .long_name = TRS("Skip frames"),
      .type =      BG_PARAMETER_CHECKBUTTON,
      .val_default = GAVL_VALUE_INIT_INT(1),
      .help_string = TRS("Skip frames to keep A/V sync"),
    },
    BG_GAVL_PARAM_THREADS,
    { /* End of parameters */ }
  };

const bg_parameter_info_t * bg_player_get_video_parameters(bg_player_t * p)
  {
  return parameters;
  }

void bg_player_set_video_parameter(void * data, const char * name,
                                   const gavl_value_t * val)
  {
  bg_player_t * p = (bg_player_t*)data;
  int need_restart = 0;
  int is_interrupted;
  int do_init;
  int check_restart;
  int state = bg_player_get_status(p);
  
  do_init = (state == BG_PLAYER_STATUS_INIT);
  is_interrupted = (state == BG_PLAYER_STATUS_INTERRUPTED);
  
  if(!bg_gavl_video_set_parameter(&p->video_stream.options,
                                  name, val))
    {
    if(!strcmp(name, "skip"))
      p->video_stream.do_skip = val->v.i;
    }
  
  if(!do_init && !is_interrupted)
    check_restart = 1;
  else
    check_restart = 0;
  
  if(check_restart)
    need_restart = p->video_stream.options.options_changed;
  

  if(!need_restart && check_restart)
    {
    bg_video_filter_chain_lock(p->video_stream.fc);
    need_restart =
        bg_video_filter_chain_need_restart(p->video_stream.fc);
    bg_video_filter_chain_unlock(p->video_stream.fc);
    }

  if(need_restart)
    {
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN,
           "Restarting playback due to changed video options");
    bg_player_stream_change_init(p);
    }
  
  }



const bg_parameter_info_t *
bg_player_get_video_filter_parameters(bg_player_t * p)
  {
  return bg_video_filter_chain_get_parameters(p->video_stream.fc);
  }

void bg_player_set_video_filter_parameter(void * data, const char * name,
                                          const gavl_value_t * val)
  {
  int need_restart = 0;
  int is_interrupted;
  int do_init;
  int state;
  bg_player_t * p = (bg_player_t*)data;

  state = bg_player_get_status(p);
  
  do_init = (state == BG_PLAYER_STATUS_INIT);
  is_interrupted = (state == BG_PLAYER_STATUS_INTERRUPTED);
  
  bg_video_filter_chain_lock(p->video_stream.fc);
  bg_video_filter_chain_set_parameter(p->video_stream.fc, name, val);
  
  need_restart =
    bg_video_filter_chain_need_restart(p->video_stream.fc);
  
  bg_video_filter_chain_unlock(p->video_stream.fc);
  
  if(!do_init && need_restart && !is_interrupted)
    {
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN,
           "Restarting playback due to changed video filters");

    bg_player_stream_change_init(p);
    }
  
  }


void bg_player_video_set_eof(bg_player_t * p)
  {
  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Detected EOF");
  pthread_mutex_lock(&p->video_stream.eof_mutex);
  pthread_mutex_lock(&p->audio_stream.eof_mutex);

  p->video_stream.eof = 1;
  
  if(p->audio_stream.eof  || (gavl_track_get_duration(p->src->track_info) == GAVL_TIME_UNDEFINED))
    bg_player_set_eof(p);
  
  pthread_mutex_unlock(&p->audio_stream.eof_mutex);
  pthread_mutex_unlock(&p->video_stream.eof_mutex);
  }
