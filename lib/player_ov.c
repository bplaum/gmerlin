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

static int handle_message(void * data, gavl_msg_t * msg)
  {
  bg_player_t * p = data;
  
  switch(msg->NS)
    {
    case BG_MSG_NS_STATE:
      /* Propagate state changes to player */
      bg_msg_sink_put(p->ctrl.evt_sink, msg);
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
          bg_msg_sink_put(p->ctrl.evt_sink, msg);
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
            bg_msg_sink_put(p->ctrl.evt_sink, msg);
            }
          
          }
          break;
        case GAVL_MSG_GUI_MOUSE_MOTION:
          break;
        case GAVL_MSG_GUI_ACCEL:
          break;
        }
      }
    }
  
  return 1;
  }

void bg_player_ov_create(bg_player_t * player)
  {
  bg_player_video_stream_t * s = &player->video_stream;
  s->ov_evt_sink = bg_msg_sink_create(handle_message, player, 1);
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
     !(handle = bg_ov_plugin_load(bg_plugin_reg, options, ctx->display_string)))
    return 0;
  
  bg_player_stream_change_init(player);
  

#if 0
  if(plugin->set_window_options && ctx->window_options)
    {
    gavl_video_frame_t * icon = NULL;
    gavl_video_format_t icon_format;
    char * icon_path = NULL;
    char ** opt;
    
    opt = gavl_strbreak(ctx->window_options, ',');
    
    memset(&icon_format, 0, sizeof(icon_format));
    
    /* Load icon */

    if(*(opt[2]) != '/')
      {
      icon_path = bg_search_file_read("icons", opt[2]);
      if(icon_path)
        {
        icon = 
          bg_plugin_registry_load_image(bg_plugin_reg,
                                        icon_path,
                                        &icon_format,
                                        NULL);
        }
      }
    else
      icon = 
        bg_plugin_registry_load_image(bg_plugin_reg,
                                      opt[2],
                                      &icon_format,
                                      NULL);
    
    plugin->set_window_options(handle->priv,
                               opt[0], opt[1],
                               icon, &icon_format);
    if(icon)
      gavl_video_frame_destroy(icon);
    if(icon_path)
      free(icon_path);
    gavl_strbreak_free(opt);
    }
#endif
  
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

void bg_player_ov_destroy(bg_player_t * player)
  {
  bg_player_video_stream_t * ctx = &player->video_stream;
  
  if(ctx->ov)
    bg_ov_destroy(ctx->ov);

  if(ctx->display_string)
    free(ctx->display_string);

  if(ctx->ov_evt_sink)
    bg_msg_sink_destroy(ctx->ov_evt_sink);

  if(ctx->plugin_params)
    bg_parameter_info_destroy_array(ctx->plugin_params);

  }

int bg_player_ov_init(bg_player_video_stream_t * vs)
  {
  gavl_video_sink_t * osd_sink;
  
  int result;

  gavl_video_source_support_hw(vs->src);
  
  gavl_video_format_copy(&vs->output_format, gavl_video_source_get_src_format(vs->src));

  vs->last_time = GAVL_TIME_UNDEFINED;
  
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
  
  vs->frames_written = 0;
  vs->last_time = GAVL_TIME_UNDEFINED;
  return result;
  }

void bg_player_ov_update_still(bg_player_t * p)
  {
  gavl_video_sink_t * sink;
  gavl_video_frame_t * frame = NULL;
  bg_player_video_stream_t * s = &p->video_stream;

  sink = bg_ov_get_sink(s->ov);
  
  //  frame = gavl_video_sink_get_frame(sink);
  
  if(!bg_player_read_video(p, &frame))
    return;
  s->frame_time =
    gavl_time_unscale(s->output_format.timescale,
                      frame->timestamp);
  
  if(DO_SUBTITLE(p->flags))
    bg_subtitle_handler_update(s->sh, frame);
  
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
  
  s->last_time = GAVL_TIME_UNDEFINED;
  
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

static int wait_or_skip(bg_player_t * p, gavl_time_t diff_time)
  {
  bg_player_video_stream_t * s;
  s = &p->video_stream;
  
  if(diff_time > 0)
    {
    gavl_time_delay(&diff_time);
    s->skip = 0;
    }
  /* Drop frame */
  else if(diff_time < -GAVL_TIME_SCALE / 50) // 20 ms
    {
    gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Dropping frame");
    s->skip++;
    return 0;
    }
  return 1;
  }

void * bg_player_ov_thread(void * data)
  {
  bg_player_video_stream_t * s;
  gavl_time_t diff_time;
  gavl_time_t current_time;
  gavl_time_t frame_time;
  
  bg_player_t * p = data;
  gavl_video_frame_t * frame;
  gavl_video_sink_t * sink;
  gavl_timer_t * timer = gavl_timer_create();
  
  s = &p->video_stream;
  
  bg_thread_wait_for_start(s->th);
  
  s->frame_time = GAVL_TIME_UNDEFINED;
  s->skip = 0;
  
  while(1)
    {
    if(!bg_thread_check(s->th))
      {
      // fprintf(stderr, "bla 1");
      break;
      }
    /*
     *  TODO: Check if we had a gapless transition and need to update the cover. We do that
     *  in the vidoe thread so the rest is not disturbed
     */

    frame = NULL;
    
    if(!bg_player_read_video(p, &frame))
      {
      bg_player_video_set_eof(p);
      if(!bg_thread_wait_for_start(s->th))
        {
        break;
        }
      continue;
      }

    //    fprintf(stderr, "Got frame: %p\n", frame);
    
    /* Get frame time */

    frame_time = gavl_time_unscale(s->output_format.timescale,
                                   frame->timestamp);

    pthread_mutex_lock(&p->config_mutex);
    frame_time += p->sync_offset; // Passed by user
    pthread_mutex_unlock(&p->config_mutex);

    pthread_mutex_lock(&p->time_offset_mutex);
    frame_time -= p->time_offset_src;
    pthread_mutex_unlock(&p->time_offset_mutex);
    
    if((s->frame_time == GAVL_TIME_UNDEFINED))
      {
      gavl_timer_set(timer, frame_time);
      }
    s->frame_time = frame_time;
    
    /* Subtitle handling */
    if(DO_SUBTITLE(p->flags))
      bg_subtitle_handler_update(s->sh, frame);
//      handle_subtitle(p);
    
    /* Handle stuff */
    bg_osd_update(s->osd);

    /* Check Timing */
    
    /* Check for underruns */
    if(s->frame_time - gavl_timer_get(timer) < -GAVL_TIME_SCALE)
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN,
               "Detected underrun due to slow reading (e.g. slow network)");
      }
    
    bg_player_time_get(p, 1, NULL, &current_time);
    diff_time =  s->frame_time - current_time;

#if 0
    if(diff_time < -GAVL_TIME_SCALE)
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Detected underrun due to slow video pipeline");
      }
#endif
    
#ifdef DUMP_TIMESTAMPS
    bg_debug("C: %"PRId64", F: %"PRId64", Diff: %"PRId64"\n",
             current_time, s->frame_time, diff_time);
#endif
    /* Wait until we can display the frame */

    if((s->last_time == GAVL_TIME_UNDEFINED) ||
       (current_time > s->last_time))
      {
      //      diff_time = current_time - s->last_time;
      //      fprintf(stderr, "Diff time: %"PRId64"\n", diff_time);
      if(!wait_or_skip(p, diff_time))
        continue;
      }
    
    s->last_time = current_time;
    
    if(p->time_update_mode == TIME_UPDATE_FRAME)
      {
      bg_player_broadcast_time(p, s->frame_time);
      }

    sink = bg_ov_get_sink(s->ov);
    //    fprintf(stderr, "ov_put_frame (v): %p\n", frame);
    gavl_video_sink_put_frame(sink, frame);
    s->frames_written++;

    if(!gavl_timer_is_running(timer))
      gavl_timer_start(timer);
    }
  
  gavl_timer_destroy(timer);
  
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
