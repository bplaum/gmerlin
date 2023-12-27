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

#include <uuid/uuid.h>
#include <stdio.h>
#include <string.h>
#include <gmerlin/player.h>
#include <gavl/metatags.h>
#include <playerprivate.h>
#include <gmerlin/iconfont.h>


void bg_player_set_track(bg_msg_sink_t * sink, const gavl_dictionary_t * loc)
  {
  gavl_msg_t * msg;

  msg = bg_msg_sink_get(sink);
  gavl_msg_set_id_ns(msg, BG_PLAYER_CMD_SET_TRACK, BG_MSG_NS_PLAYER);
  gavl_msg_set_arg_dictionary(msg, 0, loc);
  bg_msg_sink_put(sink);
  }

void bg_player_set_next_track(bg_msg_sink_t * s, const gavl_dictionary_t * loc)
  {
  gavl_msg_t * msg;

  msg = bg_msg_sink_get(s);
  gavl_msg_set_id_ns(msg, BG_PLAYER_CMD_SET_NEXT_TRACK, BG_MSG_NS_PLAYER);
  gavl_msg_set_arg_dictionary(msg, 0, loc);
  bg_msg_sink_put(s);
  }

void bg_player_load_uri(bg_msg_sink_t * s,
                        const char * uri,
                        int start_playing)
  {
  gavl_msg_t * msg;
  
  msg = bg_msg_sink_get(s);
  gavl_msg_set_id_ns(msg, BG_PLAYER_CMD_SET_LOCATION, BG_MSG_NS_PLAYER);
  gavl_msg_set_arg_string(msg, 0, uri);
  gavl_msg_set_arg_int(msg, 1, start_playing);
  bg_msg_sink_put(s);
  }

void bg_player_play_track(bg_msg_sink_t * sink,
                             const gavl_dictionary_t * dict)
  {
  bg_player_set_track(sink, dict);
  bg_player_play(sink);
  }

void bg_player_play(bg_msg_sink_t * sink)
  {
  gavl_msg_t * msg;
  msg = bg_msg_sink_get(sink);
  gavl_msg_set_id_ns(msg, BG_PLAYER_CMD_PLAY, BG_MSG_NS_PLAYER);
  bg_msg_sink_put(sink);
  }

void bg_player_play_by_id(bg_msg_sink_t * sink, const char * id)
  {
  gavl_msg_t * msg;
  msg = bg_msg_sink_get(sink);
  gavl_msg_set_id_ns(msg, BG_PLAYER_CMD_PLAY_BY_ID, BG_MSG_NS_PLAYER);
  gavl_msg_set_arg_string(msg, 0, id);
  bg_msg_sink_put(sink);
  }

void bg_player_stop_m(gavl_msg_t * msg)
  {
  gavl_msg_set_id_ns(msg, BG_PLAYER_CMD_STOP, BG_MSG_NS_PLAYER);
  }

void bg_player_stop(bg_msg_sink_t * sink)
  {
  gavl_msg_t * msg;
  msg = bg_msg_sink_get(sink);
  bg_player_stop_m(msg);
  bg_msg_sink_put(sink);
  }

void bg_player_set_mute_m(gavl_msg_t * msg, int mute)
  {
  gavl_value_t val;
  gavl_value_init(&val);
  
  if((mute != 0) && (mute != 1))
    {
    gavl_value_set_int(&val, 1);
    gavl_msg_set_state(msg, BG_CMD_SET_STATE_REL, 1, BG_PLAYER_STATE_CTX,
                     BG_PLAYER_STATE_MUTE, &val);
    }
  else
    {
    gavl_value_set_int(&val, mute);
    gavl_msg_set_state(msg, BG_CMD_SET_STATE, 1, BG_PLAYER_STATE_CTX,
                     BG_PLAYER_STATE_MUTE, &val);
    }
    gavl_value_free(&val);
  }


void bg_player_toggle_mute(bg_msg_sink_t * sink)
  {
  gavl_msg_t * msg;
  msg = bg_msg_sink_get(sink);
  bg_player_set_mute_m(msg, 2);
  bg_msg_sink_put(sink);
  }

void bg_player_set_mute(bg_msg_sink_t * sink, int mute)
  {
  gavl_msg_t * msg;
  msg = bg_msg_sink_get(sink);
  bg_player_set_mute_m(msg, mute);
  bg_msg_sink_put(sink);
  }


void bg_player_pause_m(gavl_msg_t * msg)
  {
  gavl_msg_set_id_ns(msg, BG_PLAYER_CMD_PAUSE, BG_MSG_NS_PLAYER);
  }

void bg_player_pause(bg_msg_sink_t * sink)
  {
  gavl_msg_t * msg;
  msg = bg_msg_sink_get(sink);
  bg_player_pause_m(msg);
  bg_msg_sink_put(sink);
  }

void bg_player_show_info_m(gavl_msg_t * msg)
  {
  gavl_msg_set_id_ns(msg, BG_PLAYER_CMD_SHOW_INFO, BG_MSG_NS_PLAYER);
  }

void bg_player_show_info(bg_msg_sink_t * sink)
  {
  gavl_msg_t * msg;
  msg = bg_msg_sink_get(sink);
  bg_player_show_info_m(msg);
  bg_msg_sink_put(sink);
  }

void bg_player_audio_stream_menu_m(gavl_msg_t * msg)
  {
  gavl_msg_set_id_ns(msg, BG_PLAYER_CMD_AUDIO_STREAM_MENU, BG_MSG_NS_PLAYER);
  }

void bg_player_subtitle_stream_menu_m(gavl_msg_t * msg)
  {
  gavl_msg_set_id_ns(msg, BG_PLAYER_CMD_SUBTITLE_STREAM_MENU, BG_MSG_NS_PLAYER);
  }

void bg_player_chapter_menu_m(gavl_msg_t * msg)
  {
  gavl_msg_set_id_ns(msg, BG_PLAYER_CMD_CHAPTER_MENU, BG_MSG_NS_PLAYER);
  }

void bg_player_audio_stream_menu(bg_msg_sink_t * sink)
  {
  gavl_msg_t * msg;
  msg = bg_msg_sink_get(sink);
  bg_player_audio_stream_menu_m(msg);
  bg_msg_sink_put(sink);
  }

void bg_player_subtitle_stream_menu(bg_msg_sink_t * sink)
  {
  gavl_msg_t * msg;
  msg = bg_msg_sink_get(sink);
  bg_player_subtitle_stream_menu_m(msg);
  bg_msg_sink_put(sink);
  }

void bg_player_chapter_menu(bg_msg_sink_t * sink)
  {
  gavl_msg_t * msg;
  msg = bg_msg_sink_get(sink);
  bg_player_chapter_menu_m(msg);
  bg_msg_sink_put(sink);
  }

void bg_player_show_time_m(gavl_msg_t * msg)
  {
  gavl_msg_set_id_ns(msg, BG_PLAYER_CMD_SHOW_TIME, BG_MSG_NS_PLAYER);
  }

void bg_player_show_time(bg_msg_sink_t * sink)
  {
  gavl_msg_t * msg;
  msg = bg_msg_sink_get(sink);
  bg_player_show_time_m(msg);
  bg_msg_sink_put(sink);
  }

void bg_player_set_audio_stream_m(gavl_msg_t * msg, int index)
  {
  gavl_value_t val;
  gavl_value_init(&val);
  gavl_value_set_int(&val, index);
  gavl_msg_set_state(msg, BG_CMD_SET_STATE, 1,
                   BG_PLAYER_STATE_CTX,
                   BG_PLAYER_STATE_AUDIO_STREAM_USER, &val);
  gavl_msg_set_arg_int(msg, 0, index);
  }
  
void bg_player_set_audio_stream(bg_msg_sink_t * sink, int index)
  {
  gavl_msg_t * msg;
  msg = bg_msg_sink_get(sink);
  bg_player_set_audio_stream_m(msg, index);
  bg_msg_sink_put(sink);
  }

void bg_player_set_video_stream_m(gavl_msg_t * msg, int index)
  {
  gavl_value_t val;
  gavl_value_init(&val);
  gavl_value_set_int(&val, index);
  gavl_msg_set_state(msg, BG_CMD_SET_STATE, 1,
                   BG_PLAYER_STATE_CTX,
                   BG_PLAYER_STATE_VIDEO_STREAM_USER, &val);
  gavl_msg_set_arg_int(msg, 0, index);
  }

void bg_player_set_video_stream(bg_msg_sink_t * sink, int index)
  {
  gavl_msg_t * msg;
  msg = bg_msg_sink_get(sink);
  bg_player_set_video_stream_m(msg, index);
  bg_msg_sink_put(sink);
  }

void bg_player_set_subtitle_stream_m(gavl_msg_t * msg, int index)
  {
  gavl_value_t val;
  gavl_value_init(&val);
  gavl_value_set_int(&val, index);
  gavl_msg_set_state(msg, BG_CMD_SET_STATE, 1,
                   BG_PLAYER_STATE_CTX,
                   BG_PLAYER_STATE_SUBTITLE_STREAM_USER, &val);
  gavl_msg_set_arg_int(msg, 0, index);
  }

void bg_player_set_subtitle_stream(bg_msg_sink_t * sink, int index)
  {
  gavl_msg_t * msg;
  msg = bg_msg_sink_get(sink);
  bg_player_set_subtitle_stream_m(msg, index);
  bg_msg_sink_put(sink);
  }

void bg_player_set_current_track(bg_player_t * player, const gavl_dictionary_t * dict)
  {
  gavl_value_t val;
  gavl_dictionary_t * d;
  gavl_time_t duration;
  const gavl_dictionary_t * m;
  const gavl_value_t * vp;
  
  //  fprintf(stderr, "bg_player_set_current_track\n");
  
  gavl_value_init(&val);
  d = gavl_value_set_dictionary(&val);
  memcpy(d, dict, sizeof(*d));
  bg_player_state_set_local(player, 1, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_CURRENT_TRACK, &val);

  if((m = gavl_track_get_metadata(dict)) && (vp = gavl_dictionary_get(m, GAVL_STATE_SRC_SEEK_WINDOW)))
    bg_player_state_set_local(player, 1, BG_PLAYER_STATE_CTX, GAVL_STATE_SRC_SEEK_WINDOW, &val);
  
  /* Set duration range */
  if((duration = gavl_track_get_duration(d)) != GAVL_TIME_UNDEFINED)
    bg_state_set_range_long(&player->state,
                            BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_TIME,
                            0, duration);
  else
    bg_state_set_range_long(&player->state,
                            BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_TIME,
                            0, 0);
  
  }

void bg_player_set_fullscreen_m(gavl_msg_t * msg, int fs)
  {
  gavl_value_t val;
  gavl_value_init(&val);
  
  if((fs != 0) && (fs != 1))
    {
    gavl_value_set_int(&val, 1);
    gavl_msg_set_state(msg, BG_CMD_SET_STATE_REL, 1, BG_STATE_CTX_OV, BG_STATE_OV_FULLSCREEN, &val);
    }
  else
    {
    gavl_value_set_int(&val, fs);
    gavl_msg_set_state(msg, BG_CMD_SET_STATE, 1, BG_STATE_CTX_OV, BG_STATE_OV_FULLSCREEN, &val);
    }
  gavl_value_free(&val);
  }

void bg_player_set_fullscreen(bg_msg_sink_t * sink, int fs)
  {
  gavl_msg_t * msg;
  msg = bg_msg_sink_get(sink);
  bg_player_set_fullscreen_m(msg, fs);
  bg_msg_sink_put(sink);
  }


void bg_player_accel_pressed(bg_controllable_t * ctrl, int id)
  {
  switch(id)
    {
    case BG_PLAYER_ACCEL_VOLUME_DOWN:
      bg_player_set_volume_rel(ctrl->cmd_sink, -0.02);
      break;
    case BG_PLAYER_ACCEL_VOLUME_UP:
      bg_player_set_volume_rel(ctrl->cmd_sink, 0.02);
      break;
    case BG_PLAYER_ACCEL_SEEK_BACKWARD:
      bg_player_seek_rel(ctrl->cmd_sink,   -BG_PLAYER_SEEK_STEP * GAVL_TIME_SCALE );
      break;
    case BG_PLAYER_ACCEL_SEEK_FORWARD:
      bg_player_seek_rel(ctrl->cmd_sink,   BG_PLAYER_SEEK_STEP * GAVL_TIME_SCALE );
      break;
    case BG_PLAYER_ACCEL_SEEK_BACKWARD_FAST:
      bg_player_seek_rel(ctrl->cmd_sink,   -BG_PLAYER_SEEK_FAST_STEP * GAVL_TIME_SCALE );
      break;
    case BG_PLAYER_ACCEL_SEEK_FORWARD_FAST:
      bg_player_seek_rel(ctrl->cmd_sink,   BG_PLAYER_SEEK_FAST_STEP * GAVL_TIME_SCALE );
      break;
    case BG_PLAYER_ACCEL_SEEK_START:
      bg_player_seek(ctrl->cmd_sink, 0, GAVL_TIME_SCALE );
      break;
    case BG_PLAYER_ACCEL_PAUSE:
      bg_player_pause(ctrl->cmd_sink);
      break;
    case BG_PLAYER_ACCEL_MUTE:
      bg_player_toggle_mute(ctrl->cmd_sink);
      break;
    case BG_PLAYER_ACCEL_NEXT_CHAPTER:
      bg_player_next_chapter(ctrl->cmd_sink);
      break;
    case BG_PLAYER_ACCEL_PREV_CHAPTER:
      bg_player_prev_chapter(ctrl->cmd_sink);
      break;
    case BG_PLAYER_ACCEL_SEEK_10:
      bg_player_seek_perc(ctrl->cmd_sink, 0.1);
      break;
    case BG_PLAYER_ACCEL_SEEK_20:
      bg_player_seek_perc(ctrl->cmd_sink, 0.2);
      break;
    case BG_PLAYER_ACCEL_SEEK_30:
      bg_player_seek_perc(ctrl->cmd_sink, 0.3);
      break;
    case BG_PLAYER_ACCEL_SEEK_40:
      bg_player_seek_perc(ctrl->cmd_sink, 0.4);
      break;
    case BG_PLAYER_ACCEL_SEEK_50:
      bg_player_seek_perc(ctrl->cmd_sink, 0.5);
      break;
    case BG_PLAYER_ACCEL_SEEK_60:
      bg_player_seek_perc(ctrl->cmd_sink, 0.6);
      break;
    case BG_PLAYER_ACCEL_SEEK_70:
      bg_player_seek_perc(ctrl->cmd_sink, 0.7);
      break;
    case BG_PLAYER_ACCEL_SEEK_80:
      bg_player_seek_perc(ctrl->cmd_sink, 0.8);
      break;
    case BG_PLAYER_ACCEL_SEEK_90:
      bg_player_seek_perc(ctrl->cmd_sink, 0.9);
      break;
    case BG_PLAYER_ACCEL_SHOW_INFO:
      bg_player_show_info(ctrl->cmd_sink);
      break;
    case BG_PLAYER_ACCEL_SHOW_TIME:
      bg_player_show_time(ctrl->cmd_sink);
      break;
    case BG_PLAYER_ACCEL_AUDIO_STREAM_MENU:
      bg_player_audio_stream_menu(ctrl->cmd_sink);
      break;
    case BG_PLAYER_ACCEL_SUBTITLE_STREAM_MENU:
      bg_player_subtitle_stream_menu(ctrl->cmd_sink);
      break;
    case BG_PLAYER_ACCEL_CHAPTER_MENU:
      bg_player_chapter_menu(ctrl->cmd_sink);
      break;
    case BG_PLAYER_ACCEL_FULLSCREEN_ON:
      bg_player_set_fullscreen(ctrl->cmd_sink, 1);
      break;
    case BG_PLAYER_ACCEL_FULLSCREEN_OFF:
      bg_player_set_fullscreen(ctrl->cmd_sink, 0);
      break;
    case BG_PLAYER_ACCEL_FULLSCREEN_TOGGLE:
      bg_player_set_fullscreen(ctrl->cmd_sink, 2);
      // fprintf(stderr, "Toggle fullscreen\n");
      break;
    case BG_PLAYER_ACCEL_STOP:
      bg_player_stop(ctrl->cmd_sink);
      break;
    case BG_PLAYER_ACCEL_NEXT:
      bg_player_next(ctrl->cmd_sink);
      break;
    case BG_PLAYER_ACCEL_PREV:
      bg_player_prev(ctrl->cmd_sink);
      break;
    case BG_PLAYER_ACCEL_NEXT_VISUALIZATION:
      {
      gavl_msg_t * msg = bg_msg_sink_get(ctrl->cmd_sink);
      gavl_msg_set_id_ns(msg, BG_PLAYER_CMD_NEXT_VISUALIZATION, BG_MSG_NS_PLAYER);
      bg_msg_sink_put(ctrl->cmd_sink);
      }
      break;
      
#if 1
    default:
      {
      /* Send  to the event queue */
      gavl_msg_t * msg = bg_msg_sink_get(ctrl->evt_sink);
      gavl_msg_set_id_ns(msg, BG_PLAYER_MSG_ACCEL, BG_MSG_NS_PLAYER);
      gavl_msg_set_arg_int(msg, 0, id);
      bg_msg_sink_put(ctrl->evt_sink);
      }
      break;
#endif
    }
  }


void bg_player_seek_m(gavl_msg_t * msg, gavl_time_t time, int scale)
  {
  gavl_value_t val;
  /* TODO: Allow rational number */

  gavl_value_init(&val);
  gavl_value_set_long(&val, gavl_time_unscale(scale, time));
  
  gavl_msg_set_state(msg,
                   BG_CMD_SET_STATE,
                   1,
                   BG_PLAYER_STATE_CTX,
                   BG_PLAYER_STATE_TIME,
                   &val);
  }

void bg_player_seek(bg_msg_sink_t * sink, gavl_time_t time, int scale)
  {
  gavl_msg_t * msg;
  msg = bg_msg_sink_get(sink);
  bg_player_seek_m(msg, time, scale);
  bg_msg_sink_put(sink);
  }

void bg_player_seek_perc_m(gavl_msg_t * msg, float perc)
  {
  gavl_value_t val;
  /* TODO: Allow rational number */

  gavl_value_init(&val);
  gavl_value_set_float(&val, perc);
  
  gavl_msg_set_state(msg,
                   BG_CMD_SET_STATE,
                   1,
                   BG_PLAYER_STATE_CTX,
                   BG_PLAYER_STATE_TIME_PERC,
                   &val);
  }

void bg_player_seek_perc(bg_msg_sink_t * sink, float perc)
  {
  gavl_msg_t * msg;
  msg = bg_msg_sink_get(sink);
  bg_player_seek_perc_m(msg, perc);
  bg_msg_sink_put(sink);
  }


void bg_player_seek_rel_m(gavl_msg_t * msg, gavl_time_t t)
  {
  gavl_value_t val;
  /* TODO: Allow rational number */

  gavl_value_init(&val);
  gavl_value_set_long(&val, t);
  
  gavl_msg_set_state(msg,
                   BG_CMD_SET_STATE_REL,
                   1,
                   BG_PLAYER_STATE_CTX,
                   BG_PLAYER_STATE_TIME,
                   &val);
  }

void bg_player_seek_rel(bg_msg_sink_t * sink, gavl_time_t t)
  {
  gavl_msg_t * msg;
  msg = bg_msg_sink_get(sink);
  bg_player_seek_rel_m(msg, t);
  bg_msg_sink_put(sink);
  }

void bg_player_set_volume_m(gavl_msg_t * msg, float volume)
  {
  gavl_value_t val;
  gavl_value_init(&val);
  gavl_value_set_float(&val, volume);
  
  gavl_msg_set_state(msg,
                   BG_CMD_SET_STATE, 1,
                   BG_PLAYER_STATE_CTX,
                   BG_PLAYER_STATE_VOLUME,
                   &val);
  }

void bg_player_set_volume(bg_msg_sink_t * sink, float volume)
  {
  gavl_msg_t * msg;
  
  msg = bg_msg_sink_get(sink);
  bg_player_set_volume_m(msg, volume);
  bg_msg_sink_put(sink);
  }

void bg_player_set_volume_rel_m(gavl_msg_t * msg, float volume)
  {
  gavl_value_t val;
  
  gavl_value_init(&val);
  gavl_value_set_float(&val, volume);
  
  gavl_msg_set_state(msg,
                   BG_CMD_SET_STATE_REL,
                   1,
                   BG_PLAYER_STATE_CTX,
                   BG_PLAYER_STATE_VOLUME,
                   &val);
  }

void bg_player_set_volume_rel(bg_msg_sink_t * sink, float volume)
  {
  gavl_msg_t * msg;
  msg = bg_msg_sink_get(sink);
  bg_player_set_volume_rel_m(msg, volume);
  bg_msg_sink_put(sink);
  }

void bg_player_error(bg_msg_sink_t * sink)
  {
  gavl_msg_t * msg;
  
  msg = bg_msg_sink_get(sink);
  gavl_msg_set_id_ns(msg, BG_PLAYER_CMD_SET_ERROR, BG_MSG_NS_PLAYER);
  gavl_msg_set_arg_int(msg, 0, BG_PLAYER_STATUS_ERROR);
  bg_msg_sink_put(sink);
  }


void bg_player_next_chapter_m(gavl_msg_t * msg)
  {
  gavl_value_t val;
  gavl_value_init(&val);
  gavl_value_set_int(&val, 1);

  gavl_msg_set_state(msg, BG_CMD_SET_STATE_REL,
                   1, BG_PLAYER_STATE_CTX,
                   BG_PLAYER_STATE_CHAPTER, &val);
  }

void bg_player_prev_m(gavl_msg_t * msg)
  {
  gavl_msg_set_id_ns(msg, BG_PLAYER_CMD_PREV, BG_MSG_NS_PLAYER);
  }

void bg_player_next_m(gavl_msg_t * msg)
  {
  gavl_msg_set_id_ns(msg, BG_PLAYER_CMD_NEXT, BG_MSG_NS_PLAYER);
  }

void bg_player_next(bg_msg_sink_t * sink)
  {
  gavl_msg_t * msg;
  msg = bg_msg_sink_get(sink);
  bg_player_next_m(msg);
  bg_msg_sink_put(sink);
  }

void bg_player_prev(bg_msg_sink_t * sink)
  {
  gavl_msg_t * msg;
  msg = bg_msg_sink_get(sink);
  bg_player_prev_m(msg);
  bg_msg_sink_put(sink);
  }



void bg_player_next_chapter(bg_msg_sink_t * sink)
  {


  gavl_msg_t * msg;
  msg = bg_msg_sink_get(sink);
  bg_player_next_chapter_m(msg);
  bg_msg_sink_put(sink);
  }

void bg_player_prev_chapter_m(gavl_msg_t * msg)
  {
  gavl_value_t val;
  gavl_value_init(&val);
  gavl_value_set_int(&val, -1);
  gavl_msg_set_state(msg, BG_CMD_SET_STATE_REL,
                   1, BG_PLAYER_STATE_CTX,
                   BG_PLAYER_STATE_CHAPTER, &val);
  }

void bg_player_prev_chapter(bg_msg_sink_t * sink)
  {
  gavl_msg_t * msg;
  msg = bg_msg_sink_get(sink);
  bg_player_prev_chapter_m(msg);
  bg_msg_sink_put(sink);
  }

void bg_player_set_chapter_m(gavl_msg_t * msg, int chapter)
  {
  gavl_value_t val;
  gavl_value_init(&val);
  gavl_value_set_int(&val, chapter);
  gavl_msg_set_state(msg, BG_CMD_SET_STATE,
                   1, BG_PLAYER_STATE_CTX,
                   BG_PLAYER_STATE_CHAPTER, &val);
  }

void bg_player_set_chapter(bg_msg_sink_t * sink, int chapter)
  {
  gavl_msg_t * msg;
  msg = bg_msg_sink_get(sink);
  bg_player_set_chapter_m(msg, chapter);
  bg_msg_sink_put(sink);
  }

void bg_player_underrun(bg_msg_sink_t * sink)
  {
  gavl_msg_t * msg;
  msg = bg_msg_sink_get(sink);
  gavl_msg_set_id_ns(msg, GAVL_MSG_SINK_UNDERRUN, GAVL_MSG_NS_SINK);
  bg_msg_sink_put(sink);
  }

void bg_player_set_visualization(bg_msg_sink_t * sink, const char * arg)
  {
  gavl_msg_t * msg;
  msg = bg_msg_sink_get(sink);
  gavl_msg_set_id_ns(msg, BG_PLAYER_CMD_SET_VISUALIZATION, BG_MSG_NS_PLAYER);
  gavl_msg_set_arg_string(msg, 0, arg);
  bg_msg_sink_put(sink);
  }

/* Extract infos from messages */

void bg_player_msg_get_audio_stream(gavl_msg_t * msg, int * idx,
                                    gavl_audio_format_t * in,
                                    gavl_audio_format_t * out,
                                    gavl_dictionary_t * m)
  {
  if(idx)
    *idx = gavl_msg_get_arg_int(msg, 0);

  if(in)
    gavl_msg_get_arg_audio_format(msg, 1, in);

  if(out)
    gavl_msg_get_arg_audio_format(msg, 2, out);

  if(m)
    gavl_msg_get_arg_dictionary(msg, 3, m);
  }

void bg_player_msg_get_video_stream(gavl_msg_t * msg, int * idx,
                                    gavl_video_format_t * in,
                                    gavl_video_format_t * out,
                                    gavl_dictionary_t * m)
  {
  if(idx)
    *idx = gavl_msg_get_arg_int(msg, 0);

  if(in)
    gavl_msg_get_arg_video_format(msg, 1, in);

  if(out)
    gavl_msg_get_arg_video_format(msg, 2, out);

  if(m)
    gavl_msg_get_arg_dictionary(msg, 3, m);
  }

void bg_player_msg_get_subtitle_stream(gavl_msg_t * msg,
                                       int * idx,
                                       int * is_text,
                                       gavl_video_format_t * in,
                                       gavl_video_format_t * out,
                                       gavl_dictionary_t * m)
  {
  if(idx)
    *idx = gavl_msg_get_arg_int(msg, 0);

  if(is_text)
    *is_text = gavl_msg_get_arg_int(msg, 1);

  if(in)
    gavl_msg_get_arg_video_format(msg, 2, in);

  if(out)
    gavl_msg_get_arg_video_format(msg, 3, out);
  
  if(m)
    gavl_msg_get_arg_dictionary(msg, 4, m);
  
  }

void bg_player_msg_get_stream_metadata(gavl_msg_t * msg, int * idx,
                                       gavl_dictionary_t * m)
  {
  if(idx)
    *idx = gavl_msg_get_arg_int(msg, 0);
  if(m)
    gavl_msg_get_arg_dictionary(msg, 1, m);
  }

void bg_player_msg_get_num_streams(gavl_msg_t * msg, int * as, int * vs, int * ss)
  {
  if(as)
    *as = gavl_msg_get_arg_int(msg, 0);
  if(vs)
    *vs = gavl_msg_get_arg_int(msg, 1);
  if(ss)
    *ss = gavl_msg_get_arg_int(msg, 2);
  }

static const struct
  {
  int status;
  const char * icon;
  }
player_state_icons[] =
  {

    { BG_PLAYER_STATUS_INIT,        BG_ICON_STOP },
    { BG_PLAYER_STATUS_STOPPED,     BG_ICON_STOP },
    { BG_PLAYER_STATUS_PLAYING,     BG_ICON_PLAY },
    { BG_PLAYER_STATUS_SEEKING,     BG_ICON_TRANSITION },
    { BG_PLAYER_STATUS_CHANGING,    BG_ICON_TRANSITION },
    { BG_PLAYER_STATUS_INTERRUPTED, BG_ICON_TRANSITION },
    { BG_PLAYER_STATUS_PAUSED,      BG_ICON_PAUSE      },
    { BG_PLAYER_STATUS_STARTING,    BG_ICON_TRANSITION },
    { BG_PLAYER_STATUS_ERROR,       BG_ICON_WARNING },
  };

const char * bg_player_get_state_icon(int status)
  {
  int i;

  for(i = 0; i < sizeof(player_state_icons) / sizeof(player_state_icons[0]); i++)
    {
    if(player_state_icons[i].status == status)
      return player_state_icons[i].icon; 
    }
  return NULL;
  }
