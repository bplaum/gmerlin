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
#include <gmerlin/player.h>
#include <playerprivate.h>
#include <gmerlin/log.h>

#define BG_PLAYER_VOLUME_MIN (-40.0)

#define LOG_DOMAIN "player.audio_output"

#define CHECK_PEAK(id, pos) \
   index = gavl_channel_index(fmt, id); \
   if(index >= 0) \
     { \
     if(d.peak_out[pos] < peak[index]) \
       d.peak_out[pos] = peak[index]; \
     }

#define CHECK_PEAK_2(id) \
   index = gavl_channel_index(fmt, id); \
   if(index >= 0) \
     { \
     if(d.peak_out[0] < peak[index]) \
       d.peak_out[0] = peak[index]; \
     if(d.peak_out[1] < peak[index]) \
       d.peak_out[1] = peak[index]; \
     }

typedef struct
  {
  double peak_out[2];
  int num_samples;
  } peak_data_t;

static void msg_peak(gavl_msg_t * msg,
                     const void * data)
  {
  const peak_data_t * d = data;
  gavl_msg_set_id_ns(msg, BG_PLAYER_MSG_AUDIO_PEAK, BG_MSG_NS_PLAYER);
  gavl_msg_set_arg_int(msg, 0, d->num_samples);
  gavl_msg_set_arg_float(msg, 1, d->peak_out[0]);
  gavl_msg_set_arg_float(msg, 2, d->peak_out[1]);
  }

void bg_player_peak_callback(void * priv, int num_samples,
                             const double * min,
                             const double * max,
                             const double * peak)
  {
  peak_data_t d;
  bg_player_t * p = priv;
  bg_player_audio_stream_t * s;
  const gavl_audio_format_t * fmt;
  int index;
  s = &p->audio_stream;
  
  fmt = gavl_peak_detector_get_format(s->peak_detector);

  d.peak_out[0] = 0.0;
  d.peak_out[1] = 0.0;
  d.num_samples = num_samples;
  
  /* Collect channels and merge into 2 */
  CHECK_PEAK(GAVL_CHID_FRONT_LEFT, 0);
  CHECK_PEAK(GAVL_CHID_FRONT_RIGHT, 1);

  CHECK_PEAK(GAVL_CHID_REAR_LEFT, 0);
  CHECK_PEAK(GAVL_CHID_REAR_RIGHT, 1);

  CHECK_PEAK(GAVL_CHID_SIDE_LEFT, 0);
  CHECK_PEAK(GAVL_CHID_SIDE_RIGHT, 1);

  CHECK_PEAK(GAVL_CHID_FRONT_CENTER_LEFT, 0);
  CHECK_PEAK(GAVL_CHID_FRONT_CENTER_RIGHT, 1);
  
  CHECK_PEAK_2(GAVL_CHID_FRONT_CENTER);
  CHECK_PEAK_2(GAVL_CHID_LFE);

  /* Broadcast */
  bg_msg_hub_send_cb(p->ctrl.evt_hub, msg_peak, &d);

  /* Reset */
  gavl_peak_detector_reset(s->peak_detector);
  }

/* Audio output thread */

static void process_frame(bg_player_t * p, gavl_audio_frame_t * frame)
  {
  int do_mute;
  bg_player_audio_stream_t * s;
  //  char tmp_string[128];
  s = &p->audio_stream;
  if(frame->valid_samples)
    {
    pthread_mutex_lock(&s->mute_mutex);
    do_mute = s->mute;
    pthread_mutex_unlock(&s->mute_mutex);
    
    if(DO_VISUALIZE(p->flags))
      bg_visualizer_update(p->visualizer, frame);
    
    if(DO_PEAK(p->flags))
      gavl_peak_detector_update(s->peak_detector, frame);
    
    if(do_mute)
      {
      //      fprintf(stderr, "Mute\n");
      gavl_audio_frame_mute(frame,
                            &s->output_format);
      }
    else
      {
      //      fprintf(stderr, "Volume\n");
      
      pthread_mutex_lock(&s->volume_mutex);
      gavl_volume_control_apply(s->volume,
                                frame);
      
      pthread_mutex_unlock(&s->volume_mutex);
      }
    
    }
  }

static int read_frame(bg_player_audio_stream_t * s)
  {
  s->frame = gavl_audio_sink_get_frame(s->sink);
    
  if(s->send_silence)
    {
    if(!s->frame)
      {
      if(!s->mute_frame)
        s->mute_frame = gavl_audio_frame_create(&s->output_format);
      s->frame = s->mute_frame;
      }
    gavl_audio_frame_mute(s->frame, &s->output_format);
    }
  else
    {
    if(gavl_audio_source_read_frame(s->src, &s->frame) != GAVL_SOURCE_OK)
      return 0;
    }
  return 1;
  }

gavl_time_t bg_player_oa_resync(bg_player_t * p)
  {
  bg_player_audio_stream_t * s;
  s = &p->audio_stream;

  gavl_audio_source_reset(p->audio_stream.in_src);
  
  if(!read_frame(s))
    return GAVL_TIME_UNDEFINED;

  //  fprintf(stderr, "bg_player_oa_resync %d %"PRId64"\n",
  //          s->output_format.samplerate, s->frame->timestamp);

  return gavl_time_unscale(s->output_format.samplerate, s->frame->timestamp);
  }


void * bg_player_oa_thread(void * data)
  {
  bg_player_audio_stream_t * s;
  gavl_time_t wait_time;

  bg_player_t * p = data;

  //  gavl_audio_frame_t * f;
  
  s = &p->audio_stream;
  
  /* Wait for playback */

  bg_thread_wait_for_start(s->th);
  
  while(1)
    {
    if(!bg_thread_check(s->th))
      break;

    if(!s->frame && !read_frame(s))
      {
      if(bg_player_audio_set_eof(p))
        {
        /* Stop here (don't send silence) */
        if(!bg_thread_wait_for_start(s->th))
          break;
        }
      continue;
      }

        
    process_frame(p, s->frame);
    
    if(s->frame->valid_samples)
      {
      
      if(gavl_audio_sink_put_frame(s->sink, s->frame) != GAVL_SINK_OK)
        {
        if(bg_player_audio_set_eof(p))
          {
          /* Stop here (don't send silence) */
          if(!bg_thread_wait_for_start(s->th))
            break;
          }
        }
      
      pthread_mutex_lock(&s->time_mutex);
      s->samples_written += s->frame->valid_samples;
      pthread_mutex_unlock(&s->time_mutex);
      
      /* Now, wait a while to give other threads a chance to access the
         player time */
      wait_time =
        gavl_samples_to_time(s->output_format.samplerate,
                             s->frame->valid_samples)/2;
      }
    
    s->frame = NULL;
    
    if(wait_time != GAVL_TIME_UNDEFINED)
      gavl_time_delay(&wait_time);
    }
  return NULL;
  }

int bg_player_oa_init(bg_player_audio_stream_t * ctx)
  {
  int result;
  bg_plugin_lock(ctx->plugin_handle);
  result =
    ctx->plugin->open(ctx->priv, ctx->sink_uri, &ctx->output_format);
  if(result)
    ctx->output_open = 1;
  else
    ctx->output_open = 0;
  
  if(result)
    {
    ctx->sink = ctx->plugin->get_sink(ctx->priv);
    gavl_audio_sink_set_lock_funcs(ctx->sink,
                                   bg_plugin_lock, bg_plugin_unlock,
                                   ctx->plugin_handle);
    }
  
  bg_plugin_unlock(ctx->plugin_handle);
  
  ctx->samples_written = 0;
  
  return result;
  }



void bg_player_oa_cleanup(bg_player_audio_stream_t * ctx)
  {
  bg_plugin_lock(ctx->plugin_handle);
  ctx->plugin->close(ctx->priv);
  ctx->output_open = 0;
  bg_plugin_unlock(ctx->plugin_handle);

  }

int bg_player_oa_start(bg_player_audio_stream_t * ctx)
  {
  int result = 1;
  bg_plugin_lock(ctx->plugin_handle);
  if(ctx->plugin->start)
    result = ctx->plugin->start(ctx->priv);
  bg_plugin_unlock(ctx->plugin_handle);
  return result;
  }

void bg_player_oa_stop(bg_player_audio_stream_t * ctx)
  {
  bg_plugin_lock(ctx->plugin_handle);
  if(ctx->plugin->stop)
    ctx->plugin->stop(ctx->priv);
  bg_plugin_unlock(ctx->plugin_handle);
  
  }

float bg_player_volume_to_dB(int volume)
  {
  return gavl_volume_to_float(volume,
                              BG_PLAYER_VOLUME_MIN,
                              BG_PLAYER_VOLUME_INT_MAX);
  }

int bg_player_volume_from_dB(float volume)
  {
  return gavl_volume_to_int(volume,
                            BG_PLAYER_VOLUME_MIN,
                            BG_PLAYER_VOLUME_INT_MAX);
  }

void bg_player_oa_set_volume(bg_player_audio_stream_t * ctx,
                             float volume)
  {
  int volume_i = (int)(volume * BG_PLAYER_VOLUME_INT_MAX + 0.5);
  
  pthread_mutex_lock(&ctx->volume_mutex);
  gavl_volume_control_set_volume(ctx->volume,
                                 gavl_volume_to_float(volume_i,
                                                      BG_PLAYER_VOLUME_MIN,
                                                      BG_PLAYER_VOLUME_INT_MAX));
  pthread_mutex_unlock(&ctx->volume_mutex);
  }

int bg_player_oa_get_latency(bg_player_audio_stream_t * ctx)
  {
  int ret;
  if(!ctx->priv || !ctx->plugin || !ctx->plugin->get_delay ||
     !ctx->output_open)
    {
    return 0;
    }
  bg_plugin_lock(ctx->plugin_handle);
  ret = ctx->plugin->get_delay(ctx->priv);
  bg_plugin_unlock(ctx->plugin_handle);
  return ret;
  }

void bg_player_set_oa_uri(bg_player_t * player, const char * uri)
  {
  bg_plugin_handle_t * handle;
  bg_player_audio_stream_t * ctx = &player->audio_stream;

  ctx->sink_uri = gavl_strrep(ctx->sink_uri, uri);

  if(!ctx->sink_uri)
    ctx->sink_uri = bg_get_default_sink_uri(BG_PLUGIN_OUTPUT_AUDIO);    

  
  if(!(handle = bg_output_plugin_load(ctx->sink_uri, BG_PLUGIN_OUTPUT_AUDIO)))
    return;

  /* Remove urlvars */
  gavl_url_get_vars(ctx->sink_uri, NULL);
  
  bg_player_stream_change_init(player);
  
  if(ctx->plugin_handle)
    bg_plugin_unref(ctx->plugin_handle);
  
  ctx->plugin_handle = handle;

  if(handle)
    {
    ctx->plugin = (bg_oa_plugin_t*)ctx->plugin_handle->plugin;
    ctx->priv = ctx->plugin_handle->priv;
    }

  bg_player_stream_change_done(player);
  
  }

#if 0
static int oa_set_plugin(bg_player_t * player,
                         const gavl_value_t * val)
  {
  bg_plugin_handle_t * handle;
  bg_player_audio_stream_t * ctx = &player->audio_stream;
  
  //  fprintf(stderr, "Loading oa plugin %s\n", bg_multi_menu_get_selected_name(val));
  //  gavl_value_dump(val, 2);
  
  if(!(handle = bg_plugin_load_with_options(bg_multi_menu_get_selected(val))))
    return 0;
  
  bg_player_stream_change_init(player);
  
  if(ctx->plugin_handle)
    bg_plugin_unref(ctx->plugin_handle);
  
  ctx->plugin_handle = handle;

  if(handle)
    {
    ctx->plugin = (bg_oa_plugin_t*)ctx->plugin_handle->plugin;
    ctx->priv = ctx->plugin_handle->priv;
    }
  return 1;
  }
#endif

#if 0
void bg_player_set_oa_plugin_parameter(void * data, const char * name,
                                       const gavl_value_t * val)
  {
  bg_plugin_handle_t * h;
  bg_player_t * player = data;
  
  if(!name)
    return;

  if(!strcmp(name, BG_PARAMETER_NAME_PLUGIN))
    {
    oa_set_plugin(player, val);
    return;
    }
  else if((h = player->audio_stream.plugin_handle) && h->plugin && h->plugin->set_parameter)
    {
    bg_plugin_lock(h);
    h->plugin->set_parameter(h->priv, name, val);
    bg_plugin_unlock(h);
    }
  }
#endif
