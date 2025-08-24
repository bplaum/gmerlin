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
#include <stdlib.h>

#include <gavl/keycodes.h>
#include <gmerlin/accelerator.h>

#include <gmerlin/player.h>
#include <playerprivate.h>

#include <gmerlin/log.h>
#define LOG_DOMAIN "player.video_output"

// #define DUMP_SUBTITLE
// #define DUMP_TIMESTAMPS

#define NOSKIP

#define STATE_READ  1  // Try to read frame
#define STATE_WAIT  2  // Wait to show frame
#define STATE_STILL 3 // Show still image
#define STATE_SHOW  4 // Show frame

/* Accellerators */

#define ACCEL_RESET_ZOOMSQUEEZE  3<<8
#define ACCEL_INC_ZOOM           4<<8
#define ACCEL_DEC_ZOOM           5<<8
#define ACCEL_INC_SQUEEZE        6<<8
#define ACCEL_DEC_SQUEEZE        7<<8
#define ACCEL_INC_BRIGHTNESS     8<<8
#define ACCEL_DEC_BRIGHTNESS     9<<8
#define ACCEL_INC_SATURATION    10<<8
#define ACCEL_DEC_SATURATION    11<<8
#define ACCEL_INC_CONTRAST      12<<8
#define ACCEL_DEC_CONTRAST      13<<8
#define ACCEL_FIT_WINDOW        16<<8
#define ACCEL_SHRINK_WINDOW     17<<8
#define ACCEL_ROT_PLUS          18<<8
#define ACCEL_ROT_MINUS         19<<8


static const bg_accelerator_t ov_accels[] =
  {
    { GAVL_KEY_HOME,   GAVL_KEY_CONTROL_MASK, ACCEL_RESET_ZOOMSQUEEZE },
    { GAVL_KEY_HOME,                     0, ACCEL_FIT_WINDOW        },
    { GAVL_KEY_HOME,     GAVL_KEY_SHIFT_MASK, ACCEL_SHRINK_WINDOW     },
    { GAVL_KEY_PLUS,   GAVL_KEY_CONTROL_MASK, ACCEL_INC_SQUEEZE       },
    { GAVL_KEY_MINUS,  GAVL_KEY_CONTROL_MASK, ACCEL_DEC_SQUEEZE       },
    { GAVL_KEY_PLUS,       GAVL_KEY_ALT_MASK, ACCEL_INC_ZOOM          },
    { GAVL_KEY_MINUS,      GAVL_KEY_ALT_MASK, ACCEL_DEC_ZOOM          },

    { GAVL_KEY_b,        GAVL_KEY_CONTROL_MASK,                     ACCEL_DEC_BRIGHTNESS    },
    { GAVL_KEY_b,        GAVL_KEY_CONTROL_MASK|GAVL_KEY_SHIFT_MASK, ACCEL_INC_BRIGHTNESS    },
    { GAVL_KEY_s,        GAVL_KEY_CONTROL_MASK,                     ACCEL_DEC_SATURATION    },
    { GAVL_KEY_s,        GAVL_KEY_CONTROL_MASK|GAVL_KEY_SHIFT_MASK, ACCEL_INC_SATURATION    },
    { GAVL_KEY_c,        GAVL_KEY_CONTROL_MASK,                     ACCEL_DEC_CONTRAST      },
    { GAVL_KEY_c,        GAVL_KEY_CONTROL_MASK|GAVL_KEY_SHIFT_MASK, ACCEL_INC_CONTRAST      },

    { GAVL_KEY_r,        GAVL_KEY_CONTROL_MASK|GAVL_KEY_SHIFT_MASK, ACCEL_ROT_PLUS    },
    { GAVL_KEY_r,        GAVL_KEY_CONTROL_MASK,                     ACCEL_ROT_MINUS   },

    { GAVL_KEY_NONE,                     0,  0                      },
  };



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
          int id;
          
          gavl_msg_get_gui_key(msg, &key, &mask, &x, &y, pos);
          
          //          fprintf(stderr, "Key pressed\n");

          if(bg_accelerator_map_has_accel(p->video_stream.accel_map_ov,
                                          key, mask, &id))
            {
            gavl_msg_t * msg;
            gavl_value_t val;
            gavl_value_init(&val);
            switch(id)
              {
              case ACCEL_RESET_ZOOMSQUEEZE:
                msg = bg_msg_sink_get(p->ctrl.cmd_sink);
                gavl_value_set_float(&val, 100.0);
                gavl_msg_set_state(msg, 
                                   GAVL_CMD_SET_STATE,
                                   1,
                                   BG_STATE_CTX_OV,
                                   BG_STATE_OV_ZOOM,
                                   &val);
                bg_msg_sink_put(p->ctrl.cmd_sink);

                msg = bg_msg_sink_get(p->ctrl.cmd_sink);
                gavl_value_set_float(&val, 0.0);
                gavl_msg_set_state(msg, 
                                   GAVL_CMD_SET_STATE,
                                   1,
                                   BG_STATE_CTX_OV,
                                   BG_STATE_OV_SQUEEZE,
                                   &val);
                bg_msg_sink_put(p->ctrl.cmd_sink);
                
                break;
              case ACCEL_INC_ZOOM:
                msg = bg_msg_sink_get(p->ctrl.cmd_sink);
                gavl_value_set_float(&val, BG_ZOOM_DELTA);
                
                gavl_msg_set_state(msg, 
                                   GAVL_CMD_SET_STATE_REL,
                                   1,
                                   BG_STATE_CTX_OV,
                                   BG_STATE_OV_ZOOM,
                                   &val);
                
                bg_msg_sink_put(p->ctrl.cmd_sink);
                
                break;
              case ACCEL_DEC_ZOOM:
                msg = bg_msg_sink_get(p->ctrl.cmd_sink);
                gavl_value_set_float(&val, -BG_ZOOM_DELTA);
                
                gavl_msg_set_state(msg, 
                                   GAVL_CMD_SET_STATE_REL,
                                   1,
                                   BG_STATE_CTX_OV,
                                   BG_STATE_OV_ZOOM,
                                   &val);
                
                bg_msg_sink_put(p->ctrl.cmd_sink);
                break;
              case ACCEL_INC_SQUEEZE:
                msg = bg_msg_sink_get(p->ctrl.cmd_sink);
                gavl_value_set_float(&val, BG_SQUEEZE_DELTA);
                
                gavl_msg_set_state(msg, 
                                   GAVL_CMD_SET_STATE_REL,
                                   1,
                                   BG_STATE_CTX_OV,
                                   BG_STATE_OV_SQUEEZE,
                                   &val);
                
                bg_msg_sink_put(p->ctrl.cmd_sink);
                
                break;
              case ACCEL_DEC_SQUEEZE:
                msg = bg_msg_sink_get(p->ctrl.cmd_sink);
                gavl_value_set_float(&val, -BG_SQUEEZE_DELTA);
                
                gavl_msg_set_state(msg, 
                                   GAVL_CMD_SET_STATE_REL,
                                   1,
                                   BG_STATE_CTX_OV,
                                   BG_STATE_OV_SQUEEZE,
                                   &val);
                
                bg_msg_sink_put(p->ctrl.cmd_sink);
                break;
              case ACCEL_INC_BRIGHTNESS:
                msg = bg_msg_sink_get(p->ctrl.cmd_sink);
                gavl_value_set_float(&val, BG_BRIGHTNESS_DELTA);
                
                gavl_msg_set_state(msg, 
                                   GAVL_CMD_SET_STATE_REL,
                                   1,
                                   BG_STATE_CTX_OV,
                                   BG_STATE_OV_BRIGHTNESS,
                                   &val);
                bg_msg_sink_put(p->ctrl.cmd_sink);
                
                break;
              case ACCEL_DEC_BRIGHTNESS:
                msg = bg_msg_sink_get(p->ctrl.cmd_sink);
                gavl_value_set_float(&val, -BG_BRIGHTNESS_DELTA);
                
                gavl_msg_set_state(msg, 
                                   GAVL_CMD_SET_STATE_REL,
                                   1,
                                   BG_STATE_CTX_OV,
                                   BG_STATE_OV_BRIGHTNESS,
                                   &val);
                bg_msg_sink_put(p->ctrl.cmd_sink);
                break;
              case ACCEL_INC_SATURATION:
                msg = bg_msg_sink_get(p->ctrl.cmd_sink);
                gavl_value_set_float(&val, BG_SATURATION_DELTA);
                
                gavl_msg_set_state(msg, 
                                   GAVL_CMD_SET_STATE_REL,
                                   1,
                                   BG_STATE_CTX_OV,
                                   BG_STATE_OV_SATURATION,
                                   &val);
                bg_msg_sink_put(p->ctrl.cmd_sink);
                break;
              case ACCEL_DEC_SATURATION:
                msg = bg_msg_sink_get(p->ctrl.cmd_sink);
                gavl_value_set_float(&val, -BG_SATURATION_DELTA);
                
                gavl_msg_set_state(msg, 
                                   GAVL_CMD_SET_STATE_REL,
                                   1,
                                   BG_STATE_CTX_OV,
                                   BG_STATE_OV_SATURATION,
                                   &val);
                bg_msg_sink_put(p->ctrl.cmd_sink);
                break;
              case ACCEL_INC_CONTRAST:
                msg = bg_msg_sink_get(p->ctrl.cmd_sink);
                gavl_value_set_float(&val, BG_CONTRAST_DELTA);
                
                gavl_msg_set_state(msg, 
                                   GAVL_CMD_SET_STATE_REL,
                                   1,
                                   BG_STATE_CTX_OV,
                                   BG_STATE_OV_CONTRAST,
                                   &val);
                bg_msg_sink_put(p->ctrl.cmd_sink);
                break;
              case ACCEL_DEC_CONTRAST:
                msg = bg_msg_sink_get(p->ctrl.cmd_sink);
                gavl_value_set_float(&val, -BG_CONTRAST_DELTA);
                
                gavl_msg_set_state(msg, 
                                   GAVL_CMD_SET_STATE_REL,
                                   1,
                                   BG_STATE_CTX_OV,
                                   BG_STATE_OV_CONTRAST,
                                   &val);
                bg_msg_sink_put(p->ctrl.cmd_sink);
                break;

              case ACCEL_ROT_PLUS:
                
                msg = bg_msg_sink_get(p->ctrl.cmd_sink);
                gavl_value_set_int(&val, 1);
                
                gavl_msg_set_state(msg, 
                                   GAVL_CMD_SET_STATE_REL,
                                   1,
                                   BG_STATE_CTX_OV,
                                   BG_STATE_OV_ORIENTATION,
                                   &val);
                bg_msg_sink_put(p->ctrl.cmd_sink);
                break;
              case ACCEL_ROT_MINUS:
                msg = bg_msg_sink_get(p->ctrl.cmd_sink);
                gavl_value_set_int(&val, -1);
                
                gavl_msg_set_state(msg, 
                                   GAVL_CMD_SET_STATE_REL,
                                   1,
                                   BG_STATE_CTX_OV,
                                   BG_STATE_OV_ORIENTATION,
                                   &val);
                bg_msg_sink_put(p->ctrl.cmd_sink);
                break;
              case ACCEL_FIT_WINDOW:
                break;
              case ACCEL_SHRINK_WINDOW:
                break;
              }
            gavl_value_free(&val);
            return 1;
            }
          else
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
  bg_accelerator_map_append_array(s->accel_map_ov, ov_accels);
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

void bg_player_set_ov_uri(bg_player_t * player, const char * uri)
  {
  bg_plugin_handle_t * handle;
  bg_player_video_stream_t * ctx = &player->video_stream;
  
  ctx->sink_uri = gavl_strrep(ctx->sink_uri, uri);

  if(!ctx->sink_uri)
    ctx->sink_uri = bg_get_default_sink_uri(BG_PLUGIN_OUTPUT_VIDEO);
  
  if(!(handle = bg_output_plugin_load(ctx->sink_uri, BG_PLUGIN_OUTPUT_VIDEO)))
    return;

  /* Remove urlvars */
  gavl_url_get_vars(ctx->sink_uri, NULL);
  
  bg_player_stream_change_init(player);

  if(ctx->ov)
    {
    bg_ov_destroy(ctx->ov);
    ctx->ov = NULL;
    ctx->ov_ctrl = NULL;
    }
  
  if(handle)
    {
    bg_ov_plugin_t * plugin = (bg_ov_plugin_t*)handle->plugin;

    if(plugin->get_hw_context)
      ctx->hwctx = plugin->get_hw_context(handle->priv);
    
    if(handle->plugin && handle->plugin->get_controllable && 
       (ctx->ov_ctrl = handle->plugin->get_controllable(handle->priv)))
      bg_msg_hub_connect_sink(ctx->ov_ctrl->evt_hub, ctx->ov_evt_sink);
    
    ctx->ov = bg_ov_create(handle);
    
    //  bg_ov_set_callbacks(ctx->ov, &ctx->callbacks);
    
    /* ov holds a private reference */
    bg_plugin_unref(handle);
    
    }

  bg_player_stream_change_done(player);
  
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

  if(ctx->ov_evt_sink)
    bg_msg_sink_destroy(ctx->ov_evt_sink);

  if(ctx->timer)
    gavl_timer_destroy(ctx->timer);
  
  }

int bg_player_ov_init(bg_player_video_stream_t * vs)
  {
  gavl_video_sink_t * osd_sink;
  gavl_video_source_t * src;
  int result;
  gavl_video_format_t osd_fmt;
  
  gavl_video_source_support_hw(vs->src);
  
  gavl_video_format_copy(&vs->output_format, gavl_video_source_get_src_format(vs->src));

  //  vs->last_time = GAVL_TIME_UNDEFINED;
  
  result = bg_ov_open(vs->ov, vs->sink_uri, &vs->output_format,
                      gavl_video_source_get_src_flags(vs->src));
  
  bg_ov_set_window_title(vs->ov, "Video output");
  
  if(!result)
    return 0;
  
  bg_osd_clear(vs->osd);
  bg_ov_show_window(vs->ov, 1);
  
  src = bg_osd_init(vs->osd, &vs->output_format);

  gavl_video_format_copy(&osd_fmt, gavl_video_source_get_src_format(src));
  osd_sink = bg_ov_add_overlay_stream(vs->ov, &osd_fmt);
  bg_osd_set_sink(vs->osd, osd_sink);
  //  vs->last_time = GAVL_TIME_UNDEFINED;
  return result;
  }

void bg_player_ov_update_still(bg_player_t * p)
  {
  gavl_video_sink_t * sink;
  gavl_video_frame_t * frame = NULL;
  bg_player_video_stream_t * s = &p->video_stream;

  sink = bg_ov_get_sink(s->ov);
  
  frame = gavl_video_sink_get_frame(sink);
  
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

  s->frame = gavl_video_sink_get_frame(bg_ov_get_sink(s->ov));
  
  if((st = gavl_video_source_read_frame(s->src, &s->frame)) != GAVL_SOURCE_OK)
    {
    bg_player_video_set_eof(p);
    if(!bg_thread_wait_for_start(s->th))
      return GAVL_SOURCE_EOF;
    
    }
  
  s->decode_duration = gavl_timer_get(s->timer) - time_before;
  
  //  fprintf(stderr, "Reading took %f seconds\n", gavl_time_to_seconds(s->decode_duration));

  if(!s->frame)
    return GAVL_SOURCE_EOF;
    
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

#define QOS_FRAMES 100 // Check QOS after this many frames

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

  /* Evaluate QOS after this number of frames */
  int frames_since_qos = 0;

  gavl_time_t current_time;
  
  s = &p->video_stream;

  s->state = STATE_READ;

  /* Read the first frame because this might take a while */
  
  s->frame_time = GAVL_TIME_UNDEFINED;
  s->skip = 0;

  s->frame = NULL;


  bg_player_time_get(p, 1, &current_time);
  while(1)
    {
    if(read_frame(p) != GAVL_SOURCE_OK)
      return NULL;

    if(s->frame_time >= current_time)
      break;
    else
      {
      gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Skipping initial video frame");
      s->frame = NULL;
      }
    }
  
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
        s->skip = 0;
        }

      if(frames_since_qos >= QOS_FRAMES)
        {
        /* If we lag by more than half a second, assume we cannot
           keep sync */
        if(diff_time < -GAVL_TIME_SCALE)
          {
          bg_player_speed_up(p);
          }
        frames_since_qos = 0;
        }
      else
        frames_since_qos++;
      
#if 0
      /* Drop frame */
      else if((diff_time < -GAVL_TIME_SCALE / 20) && (frames_shown > 10)) // 50 ms
        {
        gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Dropping frame (diff: %f, cur: %f, frame: %f)",
                 gavl_time_to_seconds(diff_time),
                 gavl_time_to_seconds(current_time),
                 gavl_time_to_seconds(s->frame_time));
        s->skip++;
        s->state = STATE_READ;
        continue;
        }
#endif
      s->state = STATE_SHOW;
      }
    
    if(s->state == STATE_STILL)
      {
      gavl_time_t delay_time = GAVL_TIME_SCALE / 20;
      
      if(!s->frame)
        {
        sink = bg_ov_get_sink(s->ov);
        s->frame = gavl_video_sink_get_frame(sink);
        
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

      bg_ov_handle_events(s->ov);
      
      s->frame = NULL;

      
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


#if 0
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
#endif

