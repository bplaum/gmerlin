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

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <config.h>

#include <gmerlin/player.h>
#include <playerprivate.h>

#include <gmerlin/translation.h>
#include <gmerlin/log.h>

#define LOG_DOMAIN "player.time"

void bg_player_time_init(bg_player_t * player)
  {
  bg_player_audio_stream_t * s = &player->audio_stream;

  if(s->plugin && (s->plugin->get_delay) &&
     DO_AUDIO(player->flags))
    {
    s->sync_mode = SYNC_SOUNDCARD;
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Synchronizing with soundcard");
    
    }
  else
    {
    s->sync_mode = SYNC_SOFTWARE;
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Synchronizing with software timer");
    }
  
  }

void bg_player_time_start(bg_player_t * player)
  {
  bg_player_audio_stream_t * ctx = &player->audio_stream;
 
  /* Set timer */

  if(ctx->sync_mode == SYNC_SOFTWARE)
    {
    pthread_mutex_lock(&ctx->time_mutex);
    gavl_timer_set(ctx->timer, player->display_time_offset);
    gavl_timer_start(ctx->timer);
    pthread_mutex_unlock(&ctx->time_mutex);
    }
  }

void bg_player_time_stop(bg_player_t * player)
  {
  bg_player_audio_stream_t * ctx = &player->audio_stream;

  if(ctx->sync_mode == SYNC_SOFTWARE)
    {
    pthread_mutex_lock(&ctx->time_mutex);
    gavl_timer_stop(ctx->timer);
    pthread_mutex_unlock(&ctx->time_mutex);
    }
  }

void bg_player_time_reset(bg_player_t * player)
  {
  bg_player_audio_stream_t * ctx = &player->audio_stream;
  if(ctx->sync_mode == SYNC_SOFTWARE)
    {
    pthread_mutex_lock(&ctx->time_mutex);
    gavl_timer_stop(ctx->timer);
    pthread_mutex_unlock(&ctx->time_mutex);
    }
  bg_player_time_set(player, 0);
  }

/* Set player time from stream timestamps */
void bg_player_time_sync(bg_player_t * p)
  {
  gavl_time_t t = GAVL_TIME_UNDEFINED;
  
  if(DO_AUDIO(p->flags))
    t = bg_player_oa_resync(p);
  else if(DO_VIDEO(p->flags))
    t = bg_player_ov_resync(p);
  else
    t = 0;

  //  fprintf(stderr, "bg_player_time_sync: %f\n", gavl_time_to_seconds(t));
  
  bg_player_time_set(p, t);
  }
  
/* Get the current time */

void bg_player_time_get(bg_player_t * player, int exact,
                        gavl_time_t * ret)
  {
  gavl_time_t t;
  bg_player_audio_stream_t * ctx = &player->audio_stream;
  int samples_in_soundcard;
  
  if(!exact)
    {
    pthread_mutex_lock(&ctx->time_mutex);
    t = ctx->current_time;
    pthread_mutex_unlock(&ctx->time_mutex);
    }
  else
    {
    if(ctx->sync_mode == SYNC_SOFTWARE)
      {
      pthread_mutex_lock(&ctx->time_mutex);
      ctx->current_time = gavl_timer_get(ctx->timer);
      t = ctx->current_time;
      pthread_mutex_unlock(&ctx->time_mutex);
      }
    else
      {
      samples_in_soundcard = 0;
      bg_plugin_lock(ctx->plugin_handle);
      if(ctx->output_open)
        samples_in_soundcard = ctx->plugin->get_delay(ctx->priv);
      bg_plugin_unlock(ctx->plugin_handle);

      // fprintf(stderr, "Samples: %s: Got latency: %f\n", (double)samples_in_soundcard / ctx->output_format.samplerate);
            
      pthread_mutex_lock(&ctx->time_mutex);
      //      if(test_time > ctx->current_time)
      ctx->current_time = gavl_samples_to_time(ctx->output_format.samplerate,
                                               ctx->samples_written-samples_in_soundcard);
      
      t = ctx->current_time;
      pthread_mutex_unlock(&ctx->time_mutex);
      }
    }
  
  if(ret)
    *ret = t;
  }

void bg_player_time_set(bg_player_t * player, gavl_time_t time)
  {
  bg_player_audio_stream_t * ctx = &player->audio_stream;
 
  pthread_mutex_lock(&ctx->time_mutex);
  if(ctx->sync_mode == SYNC_SOFTWARE)
    gavl_timer_set(ctx->timer, time);
  else if(ctx->sync_mode == SYNC_SOUNDCARD)
    {
    ctx->samples_written =
      gavl_time_to_samples(ctx->output_format.samplerate,
                           time);
    }

  if(player->flags & PLAYER_GAPLESS)
    {
    player->flags &= ~PLAYER_GAPLESS;
    pthread_mutex_lock(&player->display_time_offset_mutex);
    player->display_time_offset = 0;
    pthread_mutex_unlock(&player->display_time_offset_mutex);
    }
  
  ctx->current_time = time;
  pthread_mutex_unlock(&ctx->time_mutex);
  
  player->last_seconds = GAVL_TIME_UNDEFINED;
  }

