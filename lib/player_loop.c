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
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>

#include <gmerlin/player.h>
#include <playerprivate.h>
#include <gmerlin/log.h>
#include <gmerlin/mdb.h>

#include <gavl/metatags.h>
#include <gavl/http.h>

#define LOG_DOMAIN "player"

// #define INIT_THEN_PAUSE   (1<<16) //!< Initialize but go to pause status after



static void stop_cmd(bg_player_t * player, int new_state);
static int play_source(bg_player_t * p, int flags);

static void load_next_track(bg_player_t * player, int advance);

static void msg_gapless(gavl_msg_t * msg,
                        const void * data)
  {
  gavl_msg_set_id_ns(msg, BG_PLAYER_MSG_TRANSITION, BG_MSG_NS_PLAYER);
  gavl_msg_set_arg_int(msg, 0, 1);
  }

static void msg_transition(gavl_msg_t * msg,
                           const void * data)
  {
  gavl_msg_set_id_ns(msg, BG_PLAYER_MSG_TRANSITION, BG_MSG_NS_PLAYER);
  gavl_msg_set_arg_int(msg, 0, 0);
  }


static void msg_interrupt(gavl_msg_t * msg,
                          const void * data)
  {
  gavl_msg_set_id_ns(msg, BG_PLAYER_MSG_INTERRUPT, BG_MSG_NS_PLAYER);
  }

static void msg_interrupt_resume(gavl_msg_t * msg,
                                 const void * data)
  {
  gavl_msg_set_id_ns(msg, BG_PLAYER_MSG_INTERRUPT_RESUME, BG_MSG_NS_PLAYER);
  }

#if 0
static void msg_chapter_changed(gavl_msg_t * msg, const void * data)
  {
  gavl_msg_set_id_ns(msg, BG_PLAYER_MSG_CHAPTER_CHANGED, BG_MSG_NS_PLAYER);
  gavl_msg_set_arg_int(msg, 0, *((int*)data));
  }
#endif

/*
 *  Interrupt playback so all plugin threads are waiting inside
 *  keep_going();
 *
 *  Called for pause and seeking
 */

static void interrupt_cmd(bg_player_t * p, int new_state)
  {
  int old_state;
  
  /* Get the old state */
  old_state = bg_player_get_status(p);
  
  /* Set the new state */
  bg_player_set_status(p, new_state);

  if(old_state == BG_PLAYER_STATUS_PAUSED)
    return;
  
  bg_threads_pause(p->threads, PLAYER_MAX_THREADS);
  
  bg_player_time_stop(p);

  if(DO_AUDIO(p->flags))
    bg_player_oa_stop(&p->audio_stream);

  if(DO_VIDEO(p->flags))
    p->flags |= PLAYER_FREEZE_FRAME;
  }


/* Start playback */

static void start_playback(bg_player_t * p)
  {
  bg_player_set_status(p, BG_PLAYER_STATUS_PLAYING);
    
  /* Start timer */
  
  bg_player_time_start(p);

  if(DO_AUDIO(p->flags))
    bg_player_oa_start(&p->audio_stream);

  p->flags &= ~PLAYER_FREEZE_FRAME;

  bg_threads_start(p->threads, PLAYER_MAX_THREADS);
  }

/* Pause command */

static void pause_cmd(bg_player_t * p)
  {
  int state;

  if(!p->can_pause)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Cannot pause stream");
    return;
    }

  state = bg_player_get_status(p);

  if(state == BG_PLAYER_STATUS_STARTING)
    {
    play_source(p, BG_PLAYER_STATUS_PAUSED); 
    bg_input_plugin_pause(p->src->input_handle);
    }
  else if(state == BG_PLAYER_STATUS_PLAYING)
    {
    interrupt_cmd(p, BG_PLAYER_STATUS_PAUSED);
    if(DO_VIDEO(p->flags))
      bg_player_ov_update_still(p);

    if(DO_VISUALIZE(p->flags) && p->visualizer)
      bg_visualizer_pause(p->visualizer);

    bg_input_plugin_pause(p->src->input_handle);
    }
  else if(state == BG_PLAYER_STATUS_PAUSED)
    {
    bg_input_plugin_resume(p->src->input_handle);
    
    start_playback(p);
    }
  }

static int init_audio_stream(bg_player_t * p)
  {
  if(!bg_player_audio_init(p, p->src->audio_stream))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Initializing audio stream failed");
    bg_player_set_status(p, BG_PLAYER_STATUS_ERROR);
    return 0;
    }
  return 1;  
  }

static int init_video_stream(bg_player_t * p)
  {
  if(!bg_player_video_init(p, p->src->video_stream))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Initializing video stream failed");
    bg_player_set_status(p, BG_PLAYER_STATUS_ERROR);
    return 0;
    }
  return 1;
  }

static int init_subtitle_stream(bg_player_t * p)
  {
  if(!bg_player_subtitle_init(p))
    {
    bg_player_set_status(p, BG_PLAYER_STATUS_ERROR);
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Initializing subtitle stream failed");
    return 0;
    }
  return 1;
  }

/* Initialize playback pipelines */

static int init_streams(bg_player_t * p)
  {
  if(DO_SUBTITLE_ONLY(p->flags))
    {
    if(!init_audio_stream(p) ||
       !init_subtitle_stream(p) ||
       !init_video_stream(p))
      return 0;
    }
  else
    {
    if(!init_audio_stream(p) ||
       !init_video_stream(p) ||
       !init_subtitle_stream(p))
      return 0;
    }
  return 1;
  }

/* Cleanup everything */

static void cleanup_streams(bg_player_t * player)
  {
  if(DO_AUDIO(player->flags))
    bg_player_oa_cleanup(&player->audio_stream);
  
  if(DO_VIDEO(player->flags))
    bg_player_ov_cleanup(&player->video_stream);
  
  bg_player_time_stop(player);

  /* Subtitles must be cleaned up as long as the ov plugin
     is still open */
  bg_player_subtitle_cleanup(player);
  
  bg_player_video_cleanup(player);
  bg_player_audio_cleanup(player);
  bg_player_time_reset(player);
  }

static void player_cleanup(bg_player_t * player)
  {
  cleanup_streams(player);

  // Input must be cleaned up at the end because it may contain hardware handles also used by the output.
  bg_player_input_cleanup(player);
  
  player->dpy_time_offset = 0;
  
  bg_player_broadcast_time(player, 0);
  }

/* Initialize playback (called when playback starts or after
   streams have changed) */

static int init_playback(bg_player_t * p, gavl_time_t time, int state)
  {
  gavl_value_t val;

  /* Initialize audio and video streams  */
  
  if(!init_streams(p))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "init_playback: failed to initialize streams");
    return 0;
    }
  /* Set up visualizations */
  
  if(DO_VISUALIZE(p->flags))
    {
    bg_visualizer_start(p->visualizer, p->video_stream.ov);
    
    /* Update audio format */
    bg_visualizer_init(p->visualizer, &p->audio_stream.output_format);
    }
  
  /* Send input messages */
  p->current_chapter = 0;

  gavl_value_init(&val);

  gavl_value_set_int(&val, p->current_chapter);
  bg_player_state_set_local(p, 1, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_CHAPTER, &val);
  gavl_value_reset(&val);
  
  /* Send messages about formats */
  if(DO_AUDIO(p->flags))
    gavl_value_set_int(&val, p->src->audio_stream);
  else
    gavl_value_set_int(&val, -1);
  
  bg_player_state_set_local(p, 1, BG_PLAYER_STATE_CTX,
                            BG_PLAYER_STATE_AUDIO_STREAM_CURRENT, &val);
  
  if(DO_VIDEO(p->flags))
    gavl_value_set_int(&val, p->src->video_stream);
  else if(!DO_VISUALIZE(p->flags))
    {
    bg_player_ov_standby(&p->video_stream);
    gavl_value_set_int(&val, -1);
    }
  
  bg_player_state_set_local(p, 1, BG_PLAYER_STATE_CTX,
                            BG_PLAYER_STATE_VIDEO_STREAM_CURRENT, &val);
  
  if(DO_SUBTITLE(p->flags))
    gavl_value_set_int(&val, p->src->subtitle_stream);
  else
    gavl_value_set_int(&val, -1);
    
  bg_player_state_set_local(p, 1, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_SUBTITLE_STREAM_CURRENT, &val);
  
  /* Count the threads */

  if(DO_AUDIO(p->flags))
    {
    bg_thread_set_func(p->audio_stream.th, bg_player_oa_thread, p);
    }
  else
    {
    bg_thread_set_func(p->audio_stream.th, NULL, NULL);
    }
  
  if(DO_VIDEO(p->flags))
    {
    bg_thread_set_func(p->video_stream.th, bg_player_ov_thread, p);
    }
  else
    {
    bg_thread_set_func(p->video_stream.th, NULL, NULL);
    }
  
  bg_player_time_init(p);

  if((time > 0) && (p->can_seek))
    {
    bg_player_input_seek(p, time, GAVL_TIME_SCALE, -1.0);
    bg_player_time_sync(p);
    }
  else
    {
    if(DO_AUDIO(p->flags))
      bg_audio_filter_chain_reset(p->audio_stream.fc);
    
    if(DO_VIDEO(p->flags))
      bg_video_filter_chain_reset(p->video_stream.fc);
    }

  bg_threads_init(p->threads, PLAYER_MAX_THREADS);
  
  if(state == BG_PLAYER_STATUS_PAUSED)
    {
    bg_player_set_status(p, BG_PLAYER_STATUS_PAUSED);
    if(DO_VIDEO(p->flags))
      {
      bg_player_ov_reset(p);
      bg_player_ov_update_still(p);
      p->flags |= PLAYER_FREEZE_FRAME;
      }
    }
  else
    start_playback(p);
  
  /* Set start time to zero */

  bg_player_broadcast_time(p, time);

  return 1;
  }

int bg_player_source_open(bg_player_t * p, bg_player_source_t * src, int primary)
  {
  int ret = 0;
  bg_plugin_handle_t * h = NULL;
  //  int track_index = 0;
  //  char * real_location = NULL;
  
  if(primary)
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Opening location");
  else
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Opening next location");

  if(!(h = bg_load_track(&src->track)))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Loading failed (primary: %d)", primary);
    // fprintf(stderr, "Loading %s failed (primary: %d)\n", src->location, primary);
    //    free(real_location);
    
    goto fail;
    }
  
#if 0 
  if(src->location)
    {
    gavl_dictionary_t vars;
    /* Support the -ip option */

    real_location = gavl_strdup(src->location);
    
    gavl_dictionary_init(&vars);
    gavl_url_get_vars(real_location, &vars);

    gavl_dictionary_set_int(&vars, BG_URL_VAR_CMDLINE, 1);

    real_location = bg_url_append_vars(real_location, &vars);
    
    gavl_dictionary_free(&vars);
    
    if(!(h = bg_input_plugin_load_full(real_location)))
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Loading %s failed (primary: %d)", src->location, primary);
      // fprintf(stderr, "Loading %s failed (primary: %d)\n", src->location, primary);
      free(real_location);

      goto fail;
      }
    free(real_location);
    track_index = -1;
    }
  else
    {
    goto fail;
    }
#endif

  /* Shut down from last playback if necessary */
  if((src == p->src) && src->input_handle)
    player_cleanup(p);
  
  // src->input_handle = h;
  
  if(!bg_player_source_set_from_handle(p, src, h))
    goto fail;

  ret = 1;
  
  fail:

  if(!ret)
    {
    if(primary)
      {
      bg_player_set_status(p, BG_PLAYER_STATUS_ERROR);
      bg_player_ov_standby(&p->video_stream);
      }
    }
  

  
  return ret;
  }
                                      
static int set_source_from_track(bg_player_t * p,
                                 bg_player_source_t * src,
                                 const gavl_dictionary_t * track)
  {
  bg_player_source_cleanup(src);
  if(track)
    gavl_dictionary_copy(&src->track, track);
  return 1;
  }


static int play_source(bg_player_t * p, int state)
  {
  if(!p->src->track_info)
    return 0;
  
  /* Start input plugin, so we get the formats */
  if(!bg_player_input_start(p) ||
     !init_playback(p, 0, state))
    {
    bg_player_set_status(p, BG_PLAYER_STATUS_ERROR);
    bg_player_ov_standby(&p->video_stream);
    
    return 0;
    }
  return 1;
  }

static void cleanup_playback(bg_player_t * player,
                             int old_state, int new_state)
  {
  if(old_state == BG_PLAYER_STATUS_STOPPED)
    return;
  
  bg_player_set_status(player, new_state);
  
  switch(old_state)
    {
    case BG_PLAYER_STATUS_CHANGING:
      break;
    case BG_PLAYER_STATUS_STARTING:
    case BG_PLAYER_STATUS_PAUSED:
    case BG_PLAYER_STATUS_SEEKING:
    case BG_PLAYER_STATUS_PLAYING:
      bg_threads_join(player->threads, PLAYER_MAX_THREADS);
      
      if(DO_AUDIO(player->flags))
        bg_player_oa_stop(&player->audio_stream);
    default:
      break;
    }

  if(new_state == BG_PLAYER_STATUS_INTERRUPTED)
    {
    if(DO_VISUALIZE(player->flags))
      bg_visualizer_stop(player->visualizer);
    }
  
  if(new_state == BG_PLAYER_STATUS_STOPPED)
    {
    if(DO_VISUALIZE(player->flags))
      {
      /* Must clear this here */
      player->flags &= ~PLAYER_DO_VISUALIZE;
      bg_visualizer_stop(player->visualizer);
      }
    bg_player_ov_standby(&player->video_stream);
    }
  return;
  }

static void stop_cmd(bg_player_t * player, int new_state)
  {
  int old_state;
  
  old_state = bg_player_get_status(player);

  cleanup_playback(player, old_state, new_state);
  
  if((old_state == BG_PLAYER_STATUS_PLAYING) ||
     (old_state == BG_PLAYER_STATUS_PAUSED) ||
     (old_state == BG_PLAYER_STATUS_ERROR))
    {
    if((new_state == BG_PLAYER_STATUS_STOPPED) ||
       (new_state == BG_PLAYER_STATUS_CHANGING))
      player_cleanup(player);
    }
  player->old_flags = player->flags;
  player->flags &= 0xFFFF0000;

  /* Clear metadata */
  if(new_state == BG_PLAYER_STATUS_STOPPED)
    {
    gavl_dictionary_t * dict;
    gavl_value_t val;
    gavl_value_init(&val);
    dict = gavl_value_set_dictionary(&val);
    gavl_dictionary_get_dictionary_create(dict, GAVL_META_METADATA);
    bg_player_state_set_local(player, 1, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_CURRENT_TRACK, &val);
    }
  
  }


void bg_player_stream_change_init(bg_player_t * player)
  {
  int old_state;
  old_state = bg_player_get_status(player);

  if(old_state == BG_PLAYER_STATUS_INTERRUPTED)
    return;
  
  player->saved_state.state = old_state;
  bg_player_time_get(player, 1, &player->saved_state.time);
  
  if((old_state != BG_PLAYER_STATUS_STOPPED)  &&
     (old_state != BG_PLAYER_STATUS_CHANGING) &&
     (old_state != BG_PLAYER_STATUS_ERROR))
    {
    /* Interrupt and pretend we are seeking */
    cleanup_playback(player, old_state, BG_PLAYER_STATUS_INTERRUPTED);
    cleanup_streams(player);
    bg_player_source_stop(player, player->src);
    player->old_flags = player->flags;
    }
  }

int bg_player_stream_change_done(bg_player_t * player)
  {
  if(bg_player_get_status(player) != BG_PLAYER_STATUS_INTERRUPTED)
    {
    return 1;
    }
  
  if((player->saved_state.state == BG_PLAYER_STATUS_PLAYING) ||
     (player->saved_state.state == BG_PLAYER_STATUS_PAUSED))
    {
    int track_idx = bg_input_plugin_get_track(player->src->input_handle);
    if(!bg_input_plugin_set_track(player->src->input_handle, track_idx))
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Cannot select track %d after stream change",
               track_idx);
      goto fail;
      }

    bg_player_source_select_streams(player, player->src);
    
    if(!bg_player_source_start(player, player->src))
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Cannot start source after stream change");
      goto fail;
      }
    if(!bg_player_input_start(player))
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Cannot start input after stream change");

      goto fail;
      }
    if(!init_playback(player, player->saved_state.time, player->saved_state.state))
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Cannot initialize playback after stream change");

      goto fail;
      }
    }
  return 1;
  fail:

  bg_player_set_status(player, BG_PLAYER_STATUS_ERROR);

  bg_player_ov_standby(&player->video_stream);
  bg_player_time_reset(player);
  
  bg_player_input_cleanup(player);
  bg_player_broadcast_time(player, 0);
  return 0;
  }

static void seek_cmd(bg_player_t * player, gavl_time_t t, int scale, double percentage)
  {
  int new_chapter;
  const gavl_dictionary_t * cl;
  
  int old_state;
  
  //  fprintf(stderr, "seek_cmd 1: %"PRId64" %d (%f)\n", t, scale, (double)t / (double)scale );
  
  old_state = bg_player_get_status(player);

  if(old_state == BG_PLAYER_STATUS_PAUSED)
    bg_input_plugin_resume(player->src->input_handle);
  
  //  gavl_video_frame_t * vf;
  interrupt_cmd(player, BG_PLAYER_STATUS_SEEKING);
  
  if(player->can_seek)
    bg_player_input_seek(player, t, scale, percentage);
  
  //  fprintf(stderr, "seek_cmd 2: %"PRId64" %d (%f)\n", sync_time, scale, (double)t / (double)scale);
  
  /* Clear fifos and filter chains */

  if(DO_AUDIO(player->flags))
    bg_audio_filter_chain_reset(player->audio_stream.fc);
  
  if(DO_VIDEO(player->flags))
    bg_video_filter_chain_reset(player->video_stream.fc);

  /* Resync */
  
  t = bg_player_time_sync(player);
  
  //  fprintf(stderr, "seek_cmd: %f %f\n", (double)t / (double)scale, gavl_time_to_seconds(sync_time));
  
  if(DO_VIDEO(player->flags))
    bg_player_ov_reset(player);

  /* Update position in chapter list */
  if((cl = gavl_dictionary_get_chapter_list(gavl_track_get_metadata(player->src->track_info))))
    {
    new_chapter = gavl_chapter_list_get_current(cl, t);
    
    if(new_chapter != player->current_chapter)
      {
      gavl_value_t val;

      fprintf(stderr, "Chapter changed: %d -> %d\n", 
              player->current_chapter, new_chapter);
      
      player->current_chapter = new_chapter;
      
      gavl_value_init(&val);
      gavl_value_set_int(&val, player->current_chapter);
      bg_player_state_set_local(player, 1, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_CHAPTER, &val);
      gavl_value_reset(&val);
      }
    }
  
  if(old_state == BG_PLAYER_STATUS_PAUSED)
    {
    bg_player_set_status(player, BG_PLAYER_STATUS_PAUSED);

    /* Need to update slider and time for seeking case */
    bg_player_broadcast_time(player, t);
    
    if(DO_VIDEO(player->flags))
      bg_player_ov_update_still(player);
    
    bg_input_plugin_pause(player->src->input_handle);
    }
  else
    start_playback(player);
  }

static void set_audio_stream_cmd(bg_player_t * player, int stream)
  {
  if(stream == player->audio_stream_user)
    return;
  bg_player_stream_change_init(player);
  player->audio_stream_user = stream;
  bg_player_stream_change_done(player);
  if(DO_VIDEO(player->flags))
    bg_osd_show_audio_menu(player->video_stream.osd);
  }

static void set_video_stream_cmd(bg_player_t * player, int stream)
  {
  if(stream == player->video_stream_user)
    return;
  bg_player_stream_change_init(player);
  player->video_stream_user = stream;
  bg_player_stream_change_done(player);
  }

static void set_subtitle_stream_cmd(bg_player_t * player, int stream)
  {
  if(stream == player->subtitle_stream_user)
    return;
  bg_player_stream_change_init(player);
  player->subtitle_stream_user = stream;
  bg_player_stream_change_done(player);
  if(DO_VIDEO(player->flags))
    bg_osd_show_subtitle_menu(player->video_stream.osd);
  }

static void chapter_cmd(bg_player_t * player, int chapter)
  {
  int64_t t;
  const gavl_dictionary_t * dict;
  int state;
  
  if(!player->can_seek)
    return;
  
  state = bg_player_get_status(player);
  
  if((state != BG_PLAYER_STATUS_PLAYING) &&
     (state != BG_PLAYER_STATUS_PAUSED))
    return;

  if(!player->src->chapterlist)
    return;
  
  if(!(dict = gavl_chapter_list_get(player->src->chapterlist, chapter)) ||
     !gavl_dictionary_get_long(dict, GAVL_CHAPTERLIST_TIME, &t))
    return;
  
  seek_cmd(player, t, gavl_chapter_list_get_timescale(player->src->chapterlist), -1.0);
  
  bg_osd_show_chapter_menu(player->video_stream.osd);
  
  }

static void play_cmd(bg_player_t * player)
  {
  stop_cmd(player, BG_PLAYER_STATUS_CHANGING);
  
  if(!(player->src->flags & SRC_HAS_TRACK))
    {
    gavl_dictionary_t * track;

    if(!(track = bg_player_tracklist_get_current_track(&player->tl)))
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "No track selected");
      bg_player_set_status(player, BG_PLAYER_STATUS_ERROR);
      return;
      }
    if(!set_source_from_track(player, player->src, track) ||
       !bg_player_source_open(player, player->src, 1))
      {
      gavl_msg_t * msg;
      bg_player_set_status(player, BG_PLAYER_STATUS_ERROR);
              
      /* Signal error track */
      gavl_track_set_gui_state(track, GAVL_META_GUI_ERROR, 1);
      
      /* Send  to the event queue */
      
      msg = bg_msg_sink_get(player->ctrl.evt_sink);
      gavl_msg_set_id_ns(msg, BG_MSG_DB_OBJECT_CHANGED, BG_MSG_NS_DB);

      gavl_dictionary_set_string(&msg->header, GAVL_MSG_CONTEXT_ID,
                                 gavl_track_get_id(track));
      
      gavl_msg_set_arg_dictionary(msg, 0, track);
      bg_msg_sink_put(player->ctrl.evt_sink, msg);
      
      bg_player_source_cleanup(player->src);
      return;
      }
    
    /*
     *  HACK:
     *  The following can happen if we got the track via e.g. the upnp frontend,
     *  where a duration might be unavailable
     */
            
    if(player->tl.duration == GAVL_TIME_UNDEFINED)
      {
      const gavl_dictionary_t * m;
      int64_t duration = GAVL_TIME_UNDEFINED;
      
      m = gavl_track_get_metadata(player->src->track_info);
      if((gavl_dictionary_get_long(m, GAVL_META_APPROX_DURATION, &duration)) &&
         (duration > 0))
        {
        player->tl.duration = duration;
        gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Getting right duration");
        }
      }

    
    }
  
  play_source(player, BG_PLAYER_STATUS_PLAYING);

  }

/* Process command, return FALSE if thread should be ended */


int bg_player_handle_command(void * priv, gavl_msg_t * command)
  {
  //  int arg_i1;
  //  int state;

  bg_player_t * player = priv;

#if 0
  fprintf(stderr, "bg_player_handle_command\n");
  gavl_msg_dump(command, 2);
  fprintf(stderr, "\n");
#endif

  player->tl.current_changed = 0;
  player->tl.list_changed = 0;
  
  if(bg_player_tracklist_handle_message(&player->tl, command))
    {
    if(player->tl.list_changed || player->tl.current_changed)
      bg_player_source_cleanup(player->src_next);
    
    if(player->tl.list_changed)
      {
      player->tl.list_changed = 0;
      }
    if(player->tl.current_changed)
      {
      player->tl.current_changed = 0;
      stop_cmd(player, BG_PLAYER_STATUS_STOPPED);
      }
    return 1;
    }
  
  switch(command->NS)
    {
    case BG_MSG_NS_PARAMETER:
      switch(command->ID)
        {
        case BG_MSG_SET_PARAMETER_CTX:
          {
          const char * name = NULL;
          const char * ctx = NULL;
          bg_cfg_ctx_t * cfg_ctx;
          gavl_value_t val;
          //          const bg_parameter_info_t * info = NULL;
          gavl_value_init(&val);
          bg_msg_get_parameter_ctx(command, &ctx, &name, &val);
          
          //          fprintf(stderr, "set_parameter backend %s %s\n", ctx, name);
          //          gavl_msg_dump(command, 2);

          if(!ctx)
            {
            //            fprintf(stderr, "set_parameter backend %s %s\n", ctx, name);
            gavl_value_free(&val);
            bg_player_stream_change_done(player);
            break;
            }
          
          /* Termination */
#if 0
          if(!name)
            {
            int i = 0;

            fprintf(stderr, "Terminating configuration %s\n", ctx);
            
            while(player->cfg[i].p)
              {
              if(player->cfg[i].set_param)
                player->cfg[i].set_param(player->cfg[i].cb_data, NULL, NULL);
              i++;
              }
            
            }
          else
#endif
          if(!(cfg_ctx = bg_cfg_ctx_find(player->cfg, ctx)))
            {
            gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Couldn't find parameter named %s", name);
            }
          else
            {
            if(cfg_ctx->set_param)
              cfg_ctx->set_param(cfg_ctx->cb_data, name, &val);
            bg_parameter_value_free(&val);
            }
          
          }
          break;
        case BG_MSG_SET_CHAIN_PARAMETER_CTX:
          {
          const char * ctx;
          const char * name;
          
          if((ctx = gavl_msg_get_arg_string_c(command, 0)) &&
             (name = gavl_msg_get_arg_string_c(command, 1)))
            {
            fprintf(stderr, "set_chain_parameter: %s %s\n",
                    ctx, name);
            
            
            if(!strcmp(ctx, "audiofilter") && !strcmp(name, "audio_filters"))
              bg_player_handle_audio_filter_command(player, command);
            else if(!strcmp(ctx, "videofilter") && !strcmp(name, "video_filters"))
              bg_player_handle_video_filter_command(player, command);
            else
              return 1;
            }
          }
          break;
        }
      break;
    case BG_MSG_NS_STATE:
      switch(gavl_msg_get_id(command))
        {
        case BG_CMD_SET_STATE_REL:
          {
          gavl_msg_t cmd;
          
          gavl_value_t val;
          gavl_value_t add;

          const char * ctx;
          const char * var;
          
          int last = 0;
          
          gavl_value_init(&val);
          gavl_value_init(&add);
          
          bg_msg_get_state(command, &last, &ctx, &var, &add, NULL);
          
          /* Add (and clamp) value */

          bg_state_add_value(&player->state, ctx, var, &add, &val);
          
          gavl_msg_init(&cmd);
          bg_msg_set_state(&cmd, BG_CMD_SET_STATE, last, ctx, var, &val);
          bg_player_handle_command(priv, &cmd);
          gavl_msg_free(&cmd);

          
          gavl_value_free(&val);
          gavl_value_free(&add);
          }
          break;
        case BG_CMD_SET_STATE:
          {
          gavl_value_t val;
          const char * ctx;
          const char * var;

          int last = 0;
          int player_ctx_len = strlen(BG_PLAYER_STATE_CTX);
          
          gavl_value_init(&val);

          bg_msg_get_state(command,
                           &last,
                           &ctx,
                           &var,
                           &val, NULL);
          
          //          fprintf(stderr, "Player set state: %s %s %d\n", ctx, var, last);
          //          gavl_value_dump(&val, 0);
          //          fprintf(stderr, "\n");
#if 0 // Message accumulation is disabled for now because it might lead to deep recursions when the state is set
          /* Check if there are more messages */
          
          while(bg_msg_sink_peek(player->ctrl.cmd_sink, &next_id, &next_ns) && 
                (next_id == BG_CMD_SET_STATE) &&
                (next_ns == BG_MSG_NS_STATE))
            {
            command = bg_msg_sink_get_read(player->ctrl.cmd_sink);

            

            bg_msg_sink_done_read(player->ctrl.cmd_sink);
            }
#endif     
          if(gavl_string_starts_with(ctx, BG_PLAYER_STATE_CTX) &&
             ((ctx[player_ctx_len] == '/') ||
              (ctx[player_ctx_len] == '\0')))
            {
            // ctx += player_ctx_len;
            
            if(!strcmp(ctx, BG_PLAYER_STATE_CTX"/"BG_PLAYER_STATE_CURRENT_TIME))          // dictionary
              {
              /* Seek */

              int state;
              
              if(!player->can_seek)
                break;
              
              state = bg_player_get_status(player);
              if((state != BG_PLAYER_STATUS_PLAYING) &&
                 (state != BG_PLAYER_STATUS_PAUSED))
                break;           
              
              if(!strcmp(var, BG_PLAYER_TIME))
                {
                gavl_time_t t = GAVL_TIME_UNDEFINED;

                /* TODO: Support rational number */
                if(gavl_value_get_long(&val, &t) &&
                   (t != GAVL_TIME_UNDEFINED))
                  {
                  seek_cmd(player, t, GAVL_TIME_SCALE, -1.0);
                  bg_osd_show_time(player->video_stream.osd);
                  }
                }
              else if(!strcmp(var, BG_PLAYER_TIME_PERC))
                {
                double perc = -1.0;

                int state = bg_player_get_status(player);
                if((state != BG_PLAYER_STATUS_PLAYING) &&
                   (state != BG_PLAYER_STATUS_PAUSED))
                  break;           
                
                if(gavl_value_get_float(&val, &perc) &&
                   (perc >= 0.0))
                  {
                  seek_cmd(player, GAVL_TIME_UNDEFINED, 0, perc);
                  bg_osd_show_time(player->video_stream.osd);
                  }
                }
              
#if 0 // TODO
              else if(!strcmp(var, BG_PLAYER_TIME_REM))
                {
                
                }
              else if(!strcmp(var, BG_PLAYER_TIME_ABS))
                {
                
                }
              else if(!strcmp(var, BG_PLAYER_TIME_REM_ABS))
                {
                
                }
#endif
              
              }
            else if(!strcmp(var, BG_PLAYER_STATE_VOLUME))     // float
              {
              double volume;
              if(gavl_value_get_float(&val, &volume))
                bg_player_oa_set_volume(&player->audio_stream, volume);
              }
            else if(!strcmp(var, BG_PLAYER_STATE_MODE))          // int
              {
              if(val.type != GAVL_TYPE_INT)
                return 1;
              bg_player_tracklist_set_mode(&player->tl, &val.v.i);
              }
            else if(!strcmp(var, BG_PLAYER_STATE_MUTE))          // int
              {
              if(val.type != GAVL_TYPE_INT)
                return 1;
              val.v.i &= 1;
              pthread_mutex_lock(&player->audio_stream.mute_mutex);
              player->audio_stream.mute = val.v.i;
              pthread_mutex_unlock(&player->audio_stream.mute_mutex);
              }
            else if(!strcmp(var, BG_PLAYER_STATE_AUDIO_STREAM_USER))
              {
              int stream;
              if(!gavl_value_get_int(&val, &stream))
                return 1;
              set_audio_stream_cmd(player, stream);
              }
            else if(!strcmp(var, BG_PLAYER_STATE_VIDEO_STREAM_USER))
              {
              int stream;
              if(!gavl_value_get_int(&val, &stream))
                return 1;
              set_video_stream_cmd(player, stream);
              }
            else if(!strcmp(var, BG_PLAYER_STATE_SUBTITLE_STREAM_USER))
              {
              int stream;
              if(!gavl_value_get_int(&val, &stream))
                return 1;
              set_subtitle_stream_cmd(player, stream);
              }
            else if(!strcmp(var, BG_PLAYER_STATE_CHAPTER))
              {
              int chapter;

              if(!gavl_value_get_int(&val, &chapter))
                return 1;
              chapter_cmd(player, chapter);
              }
#if 0
            else if(!strcmp(var, GAVL_META_LABEL) ||
                    !strcmp(var, BG_PLAYER_STATE_PROTOCOLS) ||
                    !strcmp(var, BG_PLAYER_STATE_MIMETYPES))
              break;
#endif       
            bg_player_state_set_local(player, last, ctx, var, &val);
            }
          else if(!strcmp(ctx, BG_STATE_CTX_OV))
            {
            //  fprintf(stderr, "set_state OV %s %p %d\n", var, player->video_stream.ov_ctrl, val.type);
            //  gavl_value_dump(&val, 2);
            
            if(player->video_stream.ov_ctrl)
              {
              bg_plugin_handle_t * h = bg_ov_get_plugin(player->video_stream.ov);
              bg_plugin_lock(h);
              bg_msg_sink_put(player->video_stream.ov_ctrl->cmd_sink, command);
              bg_plugin_unlock(h);
              }
            /* Need to store this locally for the plugin registry */
            bg_player_state_set_local(player, last, ctx, var, &val);
            }
          
          if(player->state_init  && last && !strcmp(ctx, BG_PLAYER_STATE_CTX))
            {
            /*
             *  Broadcast initial state
             */
            bg_state_apply(&player->state, player->ctrl.evt_sink, BG_MSG_STATE_CHANGED);
            player->state_init = 0;
            }
          gavl_value_free(&val);
          }
          break;
        }
      break; 

    case BG_MSG_NS_PLAYER:
      {
      switch(command->ID)
        {
        case BG_PLAYER_CMD_SET_TRACK:
          {
          int state;
          gavl_value_t val;
          gavl_value_init(&val);
          
          gavl_msg_get_arg(command, 0, &val);
          
          state = bg_player_get_status(player);
          
          switch(state)
            {
            case BG_PLAYER_STATUS_PLAYING:
            case BG_PLAYER_STATUS_CHANGING:
            case BG_PLAYER_STATUS_PAUSED:
              stop_cmd(player, BG_PLAYER_STATUS_STOPPED);
              break;
            }
          
          bg_player_tracklist_splice(&player->tl, 0, -1, &val, gavl_msg_get_client_id(command));
          gavl_value_free(&val);
          
          /* Set current track */
          bg_player_tracklist_set_current_by_idx(&player->tl, 0);
          }
          break;
        case BG_PLAYER_CMD_SET_LOCATION:
          {
          char * id;
          gavl_msg_t msg1;
          
          int state = bg_player_get_status(player);
          
          switch(state)
            {
            case BG_PLAYER_STATUS_PLAYING:
            case BG_PLAYER_STATUS_CHANGING:
            case BG_PLAYER_STATUS_PAUSED:
              stop_cmd(player, BG_PLAYER_STATUS_STOPPED);
              break;
            }

          gavl_msg_init(&msg1);

          /* After the last track */

          bg_mdb_set_load_uri(&msg1, BG_PLAYQUEUE_ID, -1, gavl_msg_get_arg_string_c(command, 0));
          bg_player_tracklist_handle_message(&player->tl, &msg1);
          
          bg_player_source_cleanup(player->src_next);

          id = bg_player_tracklist_id_from_uri(NULL, gavl_msg_get_arg_string_c(command, 0));
          bg_player_tracklist_set_current_by_id(&player->tl, id);
          free(id);
          
          if(gavl_msg_get_arg_int(command, 1))
            play_cmd(player);
          
          gavl_msg_free(&msg1);
          }
          break;
        case BG_PLAYER_CMD_QUIT:
          {
          int state = bg_player_get_status(player);
          switch(state)
            {
            case BG_PLAYER_STATUS_PLAYING:
            case BG_PLAYER_STATUS_CHANGING:
            case BG_PLAYER_STATUS_PAUSED:
              stop_cmd(player, BG_PLAYER_STATUS_STOPPED);
              break;
            }
          return 0;
          }
          break;
        case BG_PLAYER_CMD_NEXT:
        case BG_PLAYER_CMD_PREV:
        case BG_PLAYER_CMD_PLAY_BY_ID:
          {
          gavl_dictionary_t * track;
          gavl_dictionary_t * last_track = NULL;
          int was_playing = 0;
          int idx = 0;
          int state = bg_player_get_status(player);
          switch(state)
            {
            case BG_PLAYER_STATUS_PLAYING:
            case BG_PLAYER_STATUS_PAUSED:
              was_playing = 1;
              last_track =
                bg_player_tracklist_get_current_track(&player->tl);
              break;
            }
          
          bg_player_source_cleanup(player->src_next);
          
          if(command->ID == BG_PLAYER_CMD_NEXT)
            {
            if(!bg_player_tracklist_advance(&player->tl, 1))
              break;
            }
          else if(command->ID == BG_PLAYER_CMD_PREV)
            {
            if(!bg_player_tracklist_back(&player->tl))
              break;
            }
          else // BG_PLAYER_CMD_PLAY_BY_ID
            {
            const char * id = gavl_msg_get_arg_string_c(command, 0);
            if(!bg_player_tracklist_set_current_by_id(&player->tl, id))
              break;

            if(!was_playing)
              {
              /* Start new playback */
              play_cmd(player);
              break;
              }
            }
          
          if(was_playing)
            {
            bg_plugin_handle_t * last_handle = NULL;
            int switch_track = 0;
            
            track = bg_player_tracklist_get_current_track(&player->tl);

            /* Check if the current and following track are from the same source */
            
            if(last_track &&
               bg_track_is_multitrack_sibling(last_track, track, &idx))
              {
              last_handle = player->src->input_handle;
              bg_plugin_ref(last_handle);

              gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Switching plugin to track idx %d", idx);

              gavl_dictionary_copy(&player->src_next->track, track);
              
              switch_track = 1;
              }
            
            stop_cmd(player, BG_PLAYER_STATUS_CHANGING);
            if(player->src->input_handle)
              player_cleanup(player);
            
            bg_player_set_status(player, BG_PLAYER_STATUS_STARTING);
            
            if(switch_track)
              {
              bg_input_plugin_set_track(last_handle, idx);
              
              if(!bg_player_source_set_from_handle(player, player->src_next, last_handle))
                {
                bg_player_set_status(player, BG_PLAYER_STATUS_ERROR);
                bg_player_ov_standby(&player->video_stream);
                break;
                }
              bg_player_swap_sources(player);
              bg_player_source_cleanup(player->src_next);
              }
            else
              {
              if(!set_source_from_track(player, player->src, track) ||
                 !bg_player_source_open(player, player->src, 1))
                {
                bg_player_set_status(player, BG_PLAYER_STATUS_ERROR);
                bg_player_ov_standby(&player->video_stream);
                break;
                }
              }
            play_source(player, BG_PLAYER_STATUS_PLAYING);
            }
          }
          break;
        case BG_PLAYER_CMD_PLAY:
          play_cmd(player);
          break;
        case BG_PLAYER_CMD_STOP:
          {
          int state = bg_player_get_status(player);
          switch(state)
            {
            case BG_PLAYER_STATUS_PLAYING:
            case BG_PLAYER_STATUS_PAUSED:
            case BG_PLAYER_STATUS_CHANGING:
              stop_cmd(player, BG_PLAYER_STATUS_STOPPED);
              break;
            }
          }
          break;
        case BG_PLAYER_CMD_SET_ERROR:
          {
          int status = gavl_msg_get_arg_int(command, 0);
          switch(status)
            {
            case BG_PLAYER_STATUS_ERROR:
              stop_cmd(player, BG_PLAYER_STATUS_STOPPED);
              bg_player_set_status(player, BG_PLAYER_STATUS_ERROR);
            }
          }
          break;
        case BG_PLAYER_CMD_PAUSE:
          pause_cmd(player);
          break;
        case BG_PLAYER_CMD_INTERRUPT:
          /* Interrupt playback and restart */
          bg_player_stream_change_init(player);
          bg_msg_hub_send_cb(player->ctrl.evt_hub,
                             msg_interrupt,
                             &player);

          while(1)
            {
            command = bg_msg_sink_get_read(player->ctrl.cmd_sink);
            if(gavl_msg_get_id(command) != BG_PLAYER_CMD_INTERRUPT_RESUME)
              gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN,
                     "Ignoring command while playback is interrupted");
            else
              break;
            bg_msg_sink_done_read(player->ctrl.cmd_sink);
            }
          bg_player_stream_change_done(player);
          bg_msg_hub_send_cb(player->ctrl.evt_hub,
                             msg_interrupt_resume,
                             &player);
          break;
        case BG_PLAYER_CMD_SHOW_INFO:
          bg_osd_show_info(player->video_stream.osd);
          break;
        case BG_PLAYER_CMD_SHOW_TIME:
          bg_osd_show_time(player->video_stream.osd);
          break;
        case BG_PLAYER_CMD_AUDIO_STREAM_MENU:
          bg_osd_show_audio_menu(player->video_stream.osd);
          break;
        case BG_PLAYER_CMD_SUBTITLE_STREAM_MENU:
          bg_osd_show_subtitle_menu(player->video_stream.osd);
          break;
        case BG_PLAYER_CMD_CHAPTER_MENU:
          bg_osd_show_chapter_menu(player->video_stream.osd);
          break;
        case BG_PLAYER_MSG_ACCEL:
          bg_player_accel_pressed(&player->ctrl, gavl_msg_get_arg_int(command, 0));
          break;
        case BG_PLAYER_CMD_NEXT_VISUALIZATION:
          {
          int max_visualization;
          int state = bg_player_get_status(player);

          if(state != BG_PLAYER_STATUS_PLAYING)
            break;

          max_visualization = bg_get_num_plugins(BG_PLUGIN_VISUALIZATION, 0);

          player->visualization_mode++;
          if(player->visualization_mode == max_visualization)
            player->visualization_mode = -1;
          
          if(player->visualization_mode == -1)
            {
            /* Switch off */
            bg_player_stream_change_init(player);
            bg_player_stream_change_done(player);
            }
          else if(player->visualization_mode == 0)
            {
            bg_visualizer_set_plugin(player->visualizer, player->visualization_mode);
            /* Switch on */
            bg_player_stream_change_init(player);
            bg_player_stream_change_done(player);
            }
          else
            {
            /* Change mode */
            bg_visualizer_set_plugin(player->visualizer, player->visualization_mode);
            }
          }
          
          break;
        }
      }
      break;
    case BG_MSG_NS_PLAYER_PRIV:
      {
      switch(command->ID)
        {
        case BG_PLAYER_CMD_EOF:
          {
          if(player->finish_mode == BG_PLAYER_FINISH_CHANGE)
            {
            int advance = 1;
            int restart = 0; // bg_player_get_restart(player);
            
            // gavl_time_t last_clock_time = GAVL_TIME_UNDEFINED;
            gavl_time_t last_time = GAVL_TIME_UNDEFINED;
            
            gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Detected EOF");

            if(gavl_track_get_duration(player->src->track_info) == GAVL_TIME_UNDEFINED)
              {
              gavl_time_t pts_to_clock_time;

              bg_player_time_get(player, 0, &last_time);
              
              // bg_player_get_time
              
              /* Live streams */
              restart = 1;

              pts_to_clock_time = gavl_track_get_pts_to_clock_time(player->src->track_info);

              if(pts_to_clock_time != GAVL_TIME_UNDEFINED)
                {
                player->initial_seek_time = last_time + pts_to_clock_time;
                gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Restarting live stream, clock_time: %"PRId64,
                         player->initial_seek_time);
                
                }
              else
                gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Restarting live stream");
              }
            /* TODO: seekable http sources */
            
            bg_threads_join(player->threads, PLAYER_MAX_THREADS);
            
            if(restart)
              {
              bg_player_source_cleanup(player->src_next);
              // gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Restarting playback");
              advance = 0;
              }
            
            /* Initialize new source from the current one if we re-use the plugin */
            if(player->src->next_track >= 0)
              {
              bg_input_plugin_set_track(player->src->input_handle, player->src->next_track);
              bg_plugin_ref(player->src->input_handle);
              
              gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Switching plugin to track %d",
                       player->src->next_track);
              
              bg_player_source_set_from_handle(player, player->src_next, player->src->input_handle);
              }
            else if(!player->src_next->input_handle)
              {
              load_next_track(player, advance);
              }
            
            player_cleanup(player);
            
            /* Check if the next input is already open but the
               gapless transition failed */
            
            if(player->src_next->input_handle)
              {
              bg_player_swap_sources(player);
              bg_player_source_cleanup(player->src_next);

              if((advance && !bg_player_tracklist_advance(&player->tl, 0)))
                {
                bg_player_set_status(player, BG_PLAYER_STATUS_ERROR);
                bg_player_ov_standby(&player->video_stream);
                break;
                }
              
              if(!bg_player_input_start(player))
                {
                bg_player_set_status(player, BG_PLAYER_STATUS_ERROR);
                bg_player_ov_standby(&player->video_stream);
                break;
                }
              bg_msg_hub_send_cb(player->ctrl.evt_hub,
                                 msg_transition,
                                 &player);
              
              play_source(player, BG_PLAYER_STATUS_PLAYING);
              if(advance)
                gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Switched to next track");
              }
            else if(player->src->next_track >= 0)
              {
              bg_player_swap_sources(player);
              bg_player_input_start(player);
              play_source(player, BG_PLAYER_STATUS_PLAYING);
              gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Switched plugin to next track");
              }
            else
              {
              bg_player_set_status(player, BG_PLAYER_STATUS_STOPPED);
              gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Nothing left to play");
              // stop_cmd(player, BG_PLAYER_STATUS_STOPPED);

              if(DO_VISUALIZE(player->flags))
                {
                /* Must clear this here */
                player->flags &= ~PLAYER_DO_VISUALIZE;
                bg_visualizer_stop(player->visualizer);
                }
              bg_player_ov_standby(&player->video_stream);

              /* Check whether to quit */
              if(player->empty_mode == BG_PLAYER_EMPTY_QUIT)
                bg_player_set_status(player, BG_PLAYER_STATUS_QUIT);
              }
            }
          else
            {
            interrupt_cmd(player, BG_PLAYER_STATUS_PAUSED);
            
            if(DO_AUDIO(player->flags))
              bg_player_oa_stop(&player->audio_stream);
            
            if(DO_VIDEO(player->flags))
              bg_player_ov_update_still(player);
            }
          }
          break;
        }
      }
      break;
    }
  return 1;
  }

static void load_next_track(bg_player_t * player, int advance)
  {
  gavl_dictionary_t * track;

  if(advance)
    track = bg_player_tracklist_get_next(&player->tl);
  else
    track = bg_player_tracklist_get_current_track(&player->tl);
  
  if(track)
    {
    set_source_from_track(player, player->src_next, track);
    /* Open next location */
              
    if(bg_track_is_multitrack_sibling(&player->src->track,
                                      track, &player->src->next_track))
      {
      gavl_dictionary_copy(&player->src_next->track, track);
      }
    else
      {
      bg_player_source_open(player, player->src_next, 0);
      }
    }
  
  }
                            

static void * player_thread(void * data)
  {
  bg_player_t * player;
  int64_t seconds;
  int do_exit;
  int state;
  int actions;
  
  player = data;

  bg_player_set_status(player, BG_PLAYER_STATUS_STOPPED);

  player->last_seconds = GAVL_TIME_UNDEFINED;
    
  do_exit = 0;
  while(1)
    {
    actions = 0;
    
    /* Process commands */

    if(!(bg_msg_sink_iteration(player->ctrl.cmd_sink)))
      do_exit = 1;

    actions += bg_msg_sink_get_num(player->ctrl.cmd_sink);

    if(!(bg_msg_sink_iteration(player->src_msg_sink)))
      do_exit = 1;

    actions += bg_msg_sink_get_num(player->src_msg_sink);
    
    if(do_exit)
      break;
    
    bg_osd_handle_messages(player->video_stream.osd);

    if(player->flags & PLAYER_FREEZE_FRAME)
      bg_player_ov_handle_events(&player->video_stream);
    
    state = bg_player_get_status(player);
    switch(state)
      {
      case BG_PLAYER_STATUS_PLAYING:
        {
        int chapter;
        const gavl_dictionary_t * cl;
        gavl_time_t time;
        
        pthread_mutex_lock(&player->src_mutex);
        bg_player_time_get(player, 1, &time);

        if(player->time_update_mode == TIME_UPDATE_SECOND)
          {
          seconds = time / GAVL_TIME_SCALE;
          if(seconds > player->last_seconds)
            {
            player->last_seconds = seconds;
            bg_player_broadcast_time(player, time);
            actions++;
#if 0
            fprintf(stderr, "Broadcast time: %f %f %f\n",
                    time / ((double)(GAVL_TIME_SCALE) * 3600.0),
                    player->display_time_offset / ((double)(GAVL_TIME_SCALE) * 3600.0),
                    (time + player->display_time_offset) / ((double)(GAVL_TIME_SCALE) * 3600.0)
                    );
#endif
            }
          }
        if((cl = gavl_dictionary_get_chapter_list(player->src->metadata)) &&
           ((chapter = gavl_chapter_list_get_current(cl, time)) != player->current_chapter))
          {
          gavl_value_t val;
          
          player->current_chapter = chapter;
          
          gavl_value_init(&val);
          
          gavl_value_set_int(&val, player->current_chapter);
          bg_player_state_set_local(player, 1, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_CHAPTER, &val);
          gavl_value_free(&val);
          actions++;
          }
        
        /* Check whether to open the next input */
        
        if(time >= player->src->duration - 5 * GAVL_TIME_SCALE)
          {
          if(!player->src_next->input_handle && (player->src->next_track < 0))
            {
            load_next_track(player, 1);
            }
          actions++;
          }
        pthread_mutex_unlock(&player->src_mutex);
        }
        break;
      }

    if(!actions)
      gavl_time_delay(&player->wait_time);
    }
  return NULL;
  }


void bg_player_broadcast_time(bg_player_t * player, gavl_time_t pts_time)
  {
  gavl_value_t val;

  gavl_time_t t_abs;
  gavl_time_t t_rem;
  gavl_time_t t_rem_abs;
  double percentage = -1.0;
  gavl_dictionary_t * dict;

  gavl_time_t t;
  
  gavl_value_init(&val);
  dict = gavl_value_set_dictionary(&val);

  pthread_mutex_lock(&player->dpy_time_offset_mutex);
  t = pts_time + player->dpy_time_offset;
  pthread_mutex_unlock(&player->dpy_time_offset_mutex);
  
  bg_player_tracklist_get_times(&player->tl, t, &t_abs, &t_rem, &t_rem_abs, &percentage);
  
  //  fprintf(stderr, "Got perc 1: %f\n", percentage);

  if((percentage < 0.0) && (player->flags & PLAYER_SEEK_WINDOW))
    {
    gavl_time_t win_start = 0;
    gavl_time_t win_end = 0;
    
    if(bg_player_get_seek_window(player, &win_start, &win_end))
      percentage = gavl_time_to_seconds(t - win_start) / gavl_time_to_seconds(win_end - win_start);

    //    fprintf(stderr, "Got percentage: %"PRId64" %"PRId64" %"PRId64"\n",
    //            t, win_start, win_end);
    }
  
  gavl_dictionary_set_long(dict, BG_PLAYER_TIME, t);
  gavl_dictionary_set_long(dict, BG_PLAYER_TIME_ABS, t_abs);
  gavl_dictionary_set_long(dict, BG_PLAYER_TIME_REM, t_rem);
  gavl_dictionary_set_long(dict, BG_PLAYER_TIME_REM_ABS, t_rem_abs);
  gavl_dictionary_set_float(dict, BG_PLAYER_TIME_PERC, percentage);
  bg_player_state_set_local(player, 1, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_CURRENT_TIME, &val);
  gavl_value_free(&val);
  }

void bg_player_run(bg_player_t * player)
  {
  pthread_create(&player->player_thread, NULL, player_thread, player);
  }

void bg_player_quit(bg_player_t *player)
  {
  gavl_msg_t * msg;
  msg = bg_msg_sink_get(player->ctrl.cmd_sink);
  gavl_msg_set_id_ns(msg, BG_PLAYER_CMD_QUIT, BG_MSG_NS_PLAYER);
  bg_msg_sink_put(player->ctrl.cmd_sink, msg);
  
  //  pthread_cancel(player->player_thread);
  pthread_join(player->player_thread, NULL);
  }

int bg_player_advance_gapless(bg_player_t * player)
  {
  gavl_audio_format_t old_fmt;
  const gavl_audio_format_t * new_fmt;
  int ret = 0;
  gavl_time_t time;
  
  // gavl_log(GAVL_LOG_DEBUG, LOG_DOMAIN, "Trying gapless transition");
  
  pthread_mutex_lock(&player->src_mutex);
  
  /* Return early for obvious cases */
  if(player->src->video_src ||
     player->src->text_src ||
     player->src->ovl_src ||
     !player->src->audio_src)
    {
    gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Gapless transition failed: Invalid streams for this track");
    goto fail;
    }
  /* Check for next source */

  memset(&old_fmt, 0, sizeof(old_fmt));
  gavl_audio_format_copy(&old_fmt, gavl_audio_source_get_src_format(player->src->audio_src));
  
  if(player->src->input_handle && (player->src->next_track >= 0))
    {
    // fprintf(stderr, "SWITCH TRACK %d\n", player->src_next->track_idx);
    bg_plugin_ref(player->src->input_handle);
    bg_input_plugin_set_track(player->src->input_handle, player->src->next_track);
    bg_player_source_set_from_handle(player, player->src_next, player->src->input_handle);
    }

  if(!player->src_next->track_info)
    {
    bg_player_time_get(player, 0, &time);
    
    if(player->src->duration != GAVL_TIME_UNDEFINED)
      gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN,
               "Gapless transition failed: Next track not loaded (unexpected EOF cur: %f, dur: %f)",
               gavl_time_to_seconds(time),
               gavl_time_to_seconds(player->src->duration)
               );
    else
      gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN,
               "Gapless transition failed: Next track not loaded");
    
    goto fail;
    }
  
  if(player->src_next->video_src ||
     player->src_next->text_src ||
     player->src_next->ovl_src ||
     !player->src_next->audio_src ||
     (player->finish_mode != BG_PLAYER_FINISH_CHANGE))
    {
    gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN,
             "Gapless transition failed: Invalid streams for next track");
    goto fail;
    }
  
  //  if(1)
  //    goto fail;
  
  /* Check streams */
  
  new_fmt = gavl_audio_source_get_src_format(player->src_next->audio_src);
  
  if((old_fmt.samplerate != new_fmt->samplerate) ||
     (old_fmt.num_channels != new_fmt->num_channels))
    {
    gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN,
             "Gapless transition failed: format mismatch %d Hz, %d ch != %d Hz, %d ch",
           old_fmt.samplerate, old_fmt.num_channels, new_fmt->samplerate, new_fmt->num_channels);
    goto fail;
    }
  if(!bg_player_tracklist_advance(&player->tl, 0))
    {
    gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Gapless transition failed: Tracklist advance failed");
    goto fail;
    }
  /* Exchange audio source */

  bg_player_swap_sources(player);

  //  fprintf(stderr, "seamless transition %08d\n", gavl_audio_source_get_dst_flags(player->src_next->audio_src));
  //  gavl_audio_format_dump(gavl_audio_source_get_dst_format(player->src_next->audio_src));
  
  gavl_audio_source_set_dst(player->src->audio_src,
                            gavl_audio_source_get_dst_flags(player->src_next->audio_src),
                            gavl_audio_source_get_dst_format(player->src_next->audio_src));
  
  player->audio_stream.in_src_int = player->src->audio_src;

  /* */
  
  player->can_seek = !!(player->src->flags & SRC_CAN_SEEK);
  player->can_pause = !!(player->src->flags & SRC_CAN_PAUSE);
  
  /* From here on, we can send the messages about the input format */
  bg_msg_hub_send_cb(player->ctrl.evt_hub, msg_gapless, &player);
  
  bg_player_set_current_track(player, player->src->track_info);
  
  pthread_mutex_lock(&player->dpy_time_offset_mutex);
  bg_player_time_get(player, 0, &player->dpy_time_offset);
  player->dpy_time_offset = -player->dpy_time_offset;
  pthread_mutex_unlock(&player->dpy_time_offset_mutex);

  player->last_seconds = GAVL_TIME_UNDEFINED;
  
  bg_player_source_cleanup(player->src_next);
  
  ret = 1;

  fail:

  
  if(ret)
    {
    player->flags |= PLAYER_GAPLESS;
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Gapless transition succeeded");
    }
  else
    gavl_log(GAVL_LOG_DEBUG, LOG_DOMAIN, "Gapless transition failed");
  
  pthread_mutex_unlock(&player->src_mutex);
  
  return ret;
  }
