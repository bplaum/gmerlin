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
#include <stdio.h>
#include <stdlib.h>

#include <gavl/keycodes.h>
#include <gmerlin/accelerator.h>

#include <gmerlin/player.h>
#include <playerprivate.h>

#include <gmerlin/log.h>
#define LOG_DOMAIN "player.video_output"

// #define DUMP_SUBTITLE
// #define DUMP_TIMESTAMPS

// #define NOSKIP

#define STATE_READ  1  // Try to read frame
#define STATE_WAIT  2  // Wait to show frame
#define STATE_STILL 3 // Show still image
#define STATE_SHOW  4 // Show frame

static int handle_message(void * data, gavl_msg_t * msg)
  {
  bg_player_t * p = data;
  
  switch(msg->NS)
    {
    case BG_MSG_NS_STATE:
      /* Propagate state changes to player */
      bg_msg_sink_put_copy(p->ctrl.evt_sink, msg);
      break;
    case GAVL_MSG_NS_GUI:
      {
      switch(msg->ID)
        {
        case GAVL_MSG_GUI_KEY_PRESS:
          {
          int key;
          int mask;
          int x;
          int y;
          double pos[2];
          gavl_msg_get_gui_key(msg, &key, &mask, &x, &y, pos);
          bg_player_key_pressed(p, key, mask);
          }
          break;
        case GAVL_MSG_GUI_KEY_RELEASE:
          break;
        case GAVL_MSG_GUI_BUTTON_PRESS:
          {
          int button;
          int mask;
          int x;
          int y;
          double pos[2];
          gavl_msg_get_gui_button(msg, &button, &mask, &x, &y, pos);

          if(button == 4)
            {
            bg_player_seek_rel(p->ctrl.cmd_sink, 2 * GAVL_TIME_SCALE);
            return 1;
            }
          else if(button == 5)
            {
            bg_player_seek_rel(p->ctrl.cmd_sink, - 2 * GAVL_TIME_SCALE);
            return 1;
            }
          bg_msg_sink_put_copy(p->ctrl.evt_sink, msg);
          }
          break;
        case GAVL_MSG_GUI_BUTTON_RELEASE:
          {
          int button;
          int mask;
          int x;
          int y;
          double pos[2];
          gavl_msg_get_gui_button(msg, &button, &mask, &x, &y, pos);

          if((button != 4) && (button == 5))
            {
            /* Broadcast */
            bg_msg_sink_put_copy(p->ctrl.evt_sink, msg);
            }
          
          }
          break;
        case GAVL_MSG_GUI_MOUSE_MOTION:
          break;
        case GAVL_MSG_GUI_ACCEL:
          break;
        }
      }
      break;
    }
  
  return 1;
  }

void bg_player_ov_create(bg_player_t * player)
  {
  bg_player_video_stream_t * s = &player->video_stream;
  s->ov_evt_sink = bg_msg_sink_create(handle_message, player, 1);
  s->timer = gavl_timer_create();
  gavl_timer_start(s->timer);
  
  }

void bg_player_add_accelerators(bg_player_t * player,
                                const bg_accelerator_t * list)
  {
  bg_accelerator_map_append_array(player->video_stream.accel_map, list);
  }


void bg_player_ov_standby(bg_player_video_stream_t * ctx)
  {
  if(!ctx->ov)
    return;
  bg_ov_show_window(ctx->ov, 0);
  }

static int ov_set_plugin(bg_player_t * player, const gavl_value_t * val)
  {
  const gavl_dictionary_t * options;
  bg_plugin_handle_t * handle;
  bg_player_video_stream_t * ctx = &player->video_stream;

  if(!(options = bg_multi_menu_get_selected(val)) ||
     !(handle = bg_ov_plugin_load(options, ctx->display_string)))
    return 0;
  
  bg_player_stream_change_init(player);
  
  if(ctx->ov)
    {
    bg_ov_destroy(ctx->ov);
    ctx->ov = NULL;
    ctx->ov_ctrl = NULL;
    }
  
  if(handle)
    {
    if(handle->plugin && handle->plugin->get_controllable && 
       (ctx->ov_ctrl = handle->plugin->get_controllable(handle->priv)))
      bg_msg_hub_connect_sink(ctx->ov_ctrl->evt_hub, ctx->ov_evt_sink);
    
    ctx->ov = bg_ov_create(handle);
    
    //  bg_ov_set_callbacks(ctx->ov, &ctx->callbacks);
    
    /* ov holds a private reference */
    bg_plugin_unref(handle);
    }
  //  bg_player_stream_change_done(player);
  return 1;
  }

gavl_time_t bg_player_ov_resync(bg_player_t * p)
  {
  bg_player_video_stream_t * s = &p->video_stream;

  if(gavl_video_source_read_frame(s->src, &s->frame) != GAVL_SOURCE_OK)
    return GAVL_TIME_UNDEFINED;
  else
    {
    s->state = STATE_READ;
    return gavl_time_unscale(s->output_format.timescale, s->frame->timestamp);
    }
  }

void bg_player_ov_destroy(bg_player_t * player)
  {
  bg_player_video_stream_t * ctx = &player->video_stream;
  
  if(ctx->ov)
    bg_ov_destroy(ctx->ov);

  if(ctx->display_string)
    free(ctx->display_string);

  if(ctx->ov_evt_sink)
    bg_msg_sink_destroy(ctx->ov_evt_sink);

  if(ctx->timer)
    gavl_timer_destroy(ctx->timer);
  
  if(ctx->plugin_params)
    bg_parameter_info_destroy_array(ctx->plugin_params);

  }

int bg_player_ov_init(bg_player_video_stream_t * vs)
  {
  gavl_video_sink_t * osd_sink;
  
  int result;

  gavl_video_source_support_hw(vs->src);
  
  gavl_video_format_copy(&vs->output_format, gavl_video_source_get_src_format(vs->src));

  //  vs->last_time = GAVL_TIME_UNDEFINED;
  
  result = bg_ov_open(vs->ov, &vs->output_format, 1);
  
  bg_ov_set_window_title(vs->ov, "Video output");
  
  if(!result)
    return 0;
  
  bg_osd_clear(vs->osd);
  bg_ov_show_window(vs->ov, 1);
  
  memset(&vs->osd_format, 0, sizeof(vs->osd_format));
  
  osd_sink = bg_ov_add_overlay_stream(vs->ov, &vs->osd_format);
  bg_osd_init(vs->osd, osd_sink, &vs->output_format);
  
  /* Fixme: Lets just hope, that the OSD format doesn't get changed
     by this call. Otherwise, we would need a gavl_video_converter */
  
  //  vs->last_time = GAVL_TIME_UNDEFINED;
  return result;
  }

void bg_player_ov_update_still(bg_player_t * p)
  {
  gavl_video_sink_t * sink;
  gavl_video_frame_t * frame = NULL;
  bg_player_video_stream_t * s = &p->video_stream;

  sink = bg_ov_get_sink(s->ov);
  
  //  frame = gavl_video_sink_get_frame(sink);
  
  if(gavl_video_source_read_frame(s->src, &frame) != GAVL_SOURCE_OK)
    return;
  s->frame_time =
    gavl_time_unscale(s->output_format.timescale,
                      frame->timestamp);
  
  if(DO_SUBTITLE(p->flags))
    bg_subtitle_handler_update(s->sh, frame->timestamp);
  
  bg_osd_update(s->osd);

  frame->duration = -1;

  //  fprintf(stderr, "ov_put_frame (s): %p\n", frame);
  gavl_video_sink_put_frame(sink, frame);
  }

void bg_player_ov_cleanup(bg_player_video_stream_t * s)
  {
  
  //  destroy_frame(s, s->frame);
  //  s->frame = NULL;
  bg_ov_close(s->ov);
  }

void bg_player_ov_reset(bg_player_t * p)
  {
  bg_player_video_stream_t * s = &p->video_stream;
  // fprintf(stderr, "bg_player_ov_reset\n");
  
  //  s->last_time = GAVL_TIME_UNDEFINED;
  
  if(DO_SUBTITLE(p->flags))
    {
    bg_subtitle_handler_reset(s->sh);
    }
  }

/* Set this extra because we must initialize subtitles after the video output */
void bg_player_ov_set_subtitle_format(bg_player_video_stream_t * s)
  {
  gavl_video_format_copy(&s->ss->output_format,
                         &s->ss->input_format);
  
  /* Add subtitle stream for plugin */
  
  s->subtitle_sink =
    bg_ov_add_overlay_stream(s->ov, &s->ss->output_format);

  bg_subtitle_handler_init(s->sh,
                           &s->output_format,
                           s->ss->vsrc,
                           s->subtitle_sink);
  }

void bg_player_ov_handle_events(bg_player_video_stream_t * s)
  {
  bg_osd_update(s->osd);
  bg_ov_handle_events(s->ov);
  }

static gavl_source_status_t read_frame(bg_player_t * p)
  {
  bg_player_video_stream_t * s;
  gavl_time_t frame_time;
  gavl_time_t time_before;

  gavl_source_status_t st;
  
  s = &p->video_stream;
  
  //    fprintf(stderr, "do read\n");

  time_before = gavl_timer_get(s->timer);
  
  if((st = gavl_video_source_read_frame(s->src, &s->frame)) != GAVL_SOURCE_OK)
    {
    bg_player_video_set_eof(p);
    if(!bg_thread_wait_for_start(s->th))
      return GAVL_SOURCE_EOF;
    }
  
  s->decode_duration = gavl_timer_get(s->timer) - time_before;
  
  //      fprintf(stderr, "do read done\n");

  frame_time = gavl_time_unscale(s->output_format.timescale,
                                 s->frame->timestamp);

  pthread_mutex_lock(&p->config_mutex);
  frame_time += p->sync_offset; // Passed by user
  pthread_mutex_unlock(&p->config_mutex);
      
  if(DO_STILL(p->flags) && (s->frame_time == GAVL_TIME_UNDEFINED))
    gavl_timer_set(s->timer, frame_time);
      
  s->frame_time = frame_time;

  //  if(s->last_time != GAVL_TIME_UNDEFINED)
  //    fprintf(stderr, "Last duration: %"PRId64"\n", frame_time - s->last_time);
  
  //  s->last_time = s->frame_time;
  
  if(DO_STILL(p->flags))
    s->state = STATE_STILL;
  else
    {
    /* Subtitle handling */
    if(DO_SUBTITLE(p->flags))
      bg_subtitle_handler_update(s->sh, s->frame->timestamp);
    
    if(p->flags & PLAYER_SYNC_NONE)
      s->state = STATE_SHOW;
    else
      s->state = STATE_WAIT;
    }
  
  return GAVL_SOURCE_OK;
  }

void * bg_player_ov_thread(void * data)
  {
  bg_player_video_stream_t * s;
  bg_player_t * p = data;
  //  gavl_video_frame_t * frame = NULL;
  gavl_video_sink_t * sink;
  //  gavl_timer_t * timer = gavl_timer_create();
  gavl_source_status_t st = GAVL_SOURCE_OK;
  int warn_wait = 0;
  int done = 0;
  int still_shown = 0;
  int frames_shown = 0;
  
  s = &p->video_stream;

  s->state = STATE_READ;

  /* Read the first frame because this might take a while */
  
  s->frame_time = GAVL_TIME_UNDEFINED;
  s->skip = 0;

  s->frame = NULL;

  if(read_frame(p) != GAVL_SOURCE_OK)
    return NULL;
  
  bg_thread_wait_for_start(s->th);
  
  while(!done)
    {
    if(!bg_thread_check(s->th))
      {
      // fprintf(stderr, "bla 1");
      break;
      }

    /* TODO: Check if we are able to play this at all */
    
    
    /* Check whether to read a frame */

    if(s->state == STATE_READ)
      {
      warn_wait = 0;
      if(read_frame(p) != GAVL_SOURCE_OK)
        {
        done = 1;
        continue;
        }
      }

    if(s->state == STATE_WAIT)
      {
      gavl_time_t diff_time;
      gavl_time_t current_time;
      
      bg_player_time_get(p, 1, &current_time);
      diff_time =  s->frame_time - current_time;

#ifdef DUMP_TIMESTAMPS
      bg_debug("C: %"PRId64", F: %"PRId64", Diff: %"PRId64"\n",
               current_time, s->frame_time, s->frame_time - current_time);
#endif

      
      if((diff_time >= GAVL_TIME_SCALE / 2) && !warn_wait)
        {
        gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Waiting  %f sec. (cur: %f, frame: %f)",
                 gavl_time_to_seconds(diff_time), gavl_time_to_seconds(current_time),
                 gavl_time_to_seconds(s->frame_time));
        warn_wait = 1;
        }
      
      if(diff_time > GAVL_TIME_SCALE / 10)
        {
        diff_time = GAVL_TIME_SCALE / 10;
        gavl_time_delay(&diff_time);
        }
      else if(diff_time > 0)
        {
        gavl_time_delay(&diff_time);
        s->state = STATE_SHOW;
        }
#ifndef NOSKIP
      /* Drop frame */
      else if((diff_time < -GAVL_TIME_SCALE / 20) && !frames_shown) // 50 ms
        {
        gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Dropping frame (diff: %f)", gavl_time_to_seconds(diff_time));
        s->skip++;
        s->state = STATE_READ;
        continue;
        }
#endif
      else
        s->state = STATE_SHOW;
      }
    
    if(s->state == STATE_STILL)
      {
      gavl_time_t delay_time = GAVL_TIME_SCALE / 20;
      
      if(!s->frame)
        {
        if((st = gavl_video_source_read_frame(s->src, &s->frame)) != GAVL_SOURCE_OK)
          s->frame = NULL;
        }
      
      if(s->frame)
        {
        gavl_time_t current_time;
        
        if(!still_shown)
          {
          s->state = STATE_SHOW;
          still_shown = 1;
          continue;
          }

        bg_player_time_get(p, 1, &current_time);
        if(s->frame_time <= current_time)
          {
          s->state = STATE_SHOW;
          still_shown = 1;
          continue;
          }
        }

      /* Idle action for still mode */
      bg_osd_update(s->osd);
      bg_ov_handle_events(s->ov);
      gavl_time_delay(&delay_time);
      }
    
    if(s->state == STATE_SHOW)
      {
      gavl_time_t time_before;
      
      bg_osd_update(s->osd);

      if(p->time_update_mode == TIME_UPDATE_FRAME)
        bg_player_broadcast_time(p, s->frame_time);
      
      sink = bg_ov_get_sink(s->ov);
      //    fprintf(stderr, "ov_put_frame (v): %p\n", frame);

      time_before = gavl_timer_get(s->timer);
      
      gavl_video_sink_put_frame(sink, s->frame);

      s->render_duration = gavl_timer_get(s->timer) - time_before;
      s->skip = 0;
      
      s->frame = NULL;

      frames_shown++;
      
      if(DO_STILL(p->flags))
        s->state = STATE_STILL;
      else
        s->state = STATE_READ;

      /* TODO: Update stats */
      
      }
    
    }
  
  return NULL;
  }

const bg_parameter_info_t * bg_player_get_osd_parameters(bg_player_t * p)
  {
  return bg_osd_get_parameters(p->video_stream.osd);
  }

void bg_player_set_osd_parameter(void * data,
                                 const char * name,
                                 const gavl_value_t*val)
  {
  bg_player_t * p = data;
  bg_osd_set_parameter(p->video_stream.osd, name, val);
  }

static const bg_parameter_info_t plugin_params[] =
  {
    {
      .name =      BG_PARAMETER_NAME_PLUGIN,
      .long_name = "Video output plugin",
      .type =      BG_PARAMETER_MULTI_MENU,
    },
    { /* End */ }
  };

const bg_parameter_info_t * bg_player_get_ov_plugin_parameters(bg_player_t * p)
  {
  if(!p->video_stream.plugin_params)
    {
    p->video_stream.plugin_params = bg_parameter_info_copy_array(plugin_params);

    bg_plugin_registry_set_parameter_info(bg_plugin_reg,
                                          BG_PLUGIN_OUTPUT_VIDEO,
                                          BG_PLUGIN_PLAYBACK,
                                          &p->video_stream.plugin_params[0]);
    }
  return p->video_stream.plugin_params;
  }

void bg_player_set_ov_plugin_parameter(void * data, const char * name,
                                       const gavl_value_t * val)
  {
  
  bg_player_t * player = data;
  
  //  fprintf(stderr, "bg_player_set_ov_plugin_parameter %s\n", name);
  
  if(!name)
    return;

  if(!strcmp(name, BG_PARAMETER_NAME_PLUGIN))
    {
    ov_set_plugin(player, val);
    return;
    }
  else if(player->video_stream.ov)
    {
    bg_plugin_handle_t * h = bg_ov_get_plugin(player->video_stream.ov);

    if(h->plugin->set_parameter)
      {
      bg_plugin_lock(h);
      h->plugin->set_parameter(h->priv, name, val);
      bg_plugin_unlock(h);
      }
    }
  }

void bg_player_set_window_config(bg_player_t * p, const char * display_string)
  {
  p->video_stream.display_string = gavl_strrep(p->video_stream.display_string, display_string);
  }
