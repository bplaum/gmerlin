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
#include <signal.h>


#include "gavf-decode.h"

#include <gmerlin/http.h>

#define LOG_DOMAIN "gavf-read"

int current_track = -1;

/* Need to interrupt playback due to select_track event */
// int got_select_track = -1;

static char * input_file   = "-";

//static char * input_plugin = NULL;
static int selected_track = 0;

bg_stream_action_t * audio_actions = NULL;
bg_stream_action_t * video_actions = NULL;
bg_stream_action_t * text_actions = NULL;
bg_stream_action_t * overlay_actions = NULL;

double seek_pos = -1.0;

bg_control_t open_ctrl;
bg_controllable_t out_ctrl;
bg_plugin_handle_t * h = NULL;


static void opt_t(void * data, int * argc, char *** _argv, int arg)
  {
  if(arg >= *argc)
    {
    fprintf(stderr, "Option -t requires an argument\n");
    exit(-1);
    }
  selected_track = atoi((*_argv)[arg]) - 1;
  bg_cmdline_remove_arg(argc, _argv, arg);
  }

static void opt_seek(void * data, int * argc, char *** _argv, int arg)
  {
  if(arg >= *argc)
    {
    fprintf(stderr, "Option -seek requires an argument\n");
    exit(-1);
    }
  seek_pos = strtod((*_argv)[arg], NULL);
  bg_cmdline_remove_arg(argc, _argv, arg);
  }

static bg_cmdline_arg_t global_options[] =
  {
    {
      .arg =         "-i",
      .help_arg =    "<location>",
      .help_string = TRS("Set input file or location"),
      .argv     =    &input_file,
    },
    {
      .arg =         "-t",
      .help_arg =    "<track>",
      .help_string = TRS("Track (starting with 1)"),
      .callback =    opt_t,
    },
    {
      .arg =         "-seek",
      .help_arg =    "<seconds>",
      .help_string = TRS("Seek to position"),
      .callback =    opt_seek,
    },
    BG_PLUGIN_OPT_RA,
    BG_PLUGIN_OPT_RV,
    BG_PLUGIN_OPT_LIST_RA,
    BG_PLUGIN_OPT_LIST_RV,
    BG_PLUGIN_OPT_LIST_OPTIONS,
    GAVFTOOLS_AUDIO_STREAM_OPTIONS,
    GAVFTOOLS_VIDEO_STREAM_OPTIONS,
    GAVFTOOLS_TEXT_STREAM_OPTIONS,
    GAVFTOOLS_OVERLAY_STREAM_OPTIONS,
    GAVFTOOLS_OUTPLUG_OPTIONS,
    {
      /* End */
    }
  };

const bg_cmdline_app_data_t app_data =
  {
    .package =  PACKAGE,
    .version =  VERSION,
    .synopsis = TRS("[options]\n"),
    .help_before = TRS("gavf reader\n"),
    .args = (bg_cmdline_arg_array_t[]) { { TRS("Options"), global_options },
                                       {  } },
    .files = (bg_cmdline_ext_doc_t[])
    { { "~/.gmerlin/plugins.xml",
        TRS("Cache of the plugin registry (shared by all applications)") },
      { "~/.gmerlin/generic/cfg.xml",
        TRS("Default plugin parameters are read from there. Use gmerlin_plugincfg to change them.") },
      { /* End */ }
    },
    
  };

/* For buffering messages */
static int handle_msg_open(void * priv, gavl_msg_t * msg)
  {
  bg_plug_t * plug = priv;
  bg_media_sink_stream_t * s;
  bg_media_sink_t * sink;

  //  fprintf(stderr, "handle_msg_open\n");
  //  gavl_msg_dump(msg, 2);

  /* Must be sent *after* the header is sent! */
  
  if((sink = bg_plug_get_sink(plug)) &&
     (s = bg_media_sink_get_stream_by_id(sink, GAVL_META_STREAM_ID_MSG_PROGRAM)) &&
     s->msgsink)
    bg_msg_sink_put_copy(s->msgsink, msg);
  
  //  ctrl = bg_plug_get_control(plug);
  // bg_msg_sink_put(ctrl->cmd_sink, msg);


  //  bg_media_sink_stream_t * bg_media_sink_get_stream_by_id(bg_media_sink_t * sink, int id)

  
  return 1;
  }

static void select_track(int track)
  {
  gavl_dictionary_t * track_info;
  gavl_dictionary_t m;
  
  bg_input_plugin_set_track(h, track);
  track_info = bg_input_plugin_get_track_info(h, track);

  //  num_msg_streams     = gavl_track_get_num_msg_streams(track_info);
  
  /* Extract metadata */
  gavl_dictionary_init(&m);
  gavl_dictionary_copy(&m, gavl_track_get_metadata(track_info));

  gavftools_set_stream_actions(h->src);
  
  bg_media_source_set_msg_action_by_id(h->src, GAVL_META_STREAM_ID_MSG_PROGRAM, BG_STREAM_ACTION_DECODE);
  }

static int handle_msg_out(void * priv, gavl_msg_t * msg)
  {
  fprintf(stderr, "gavf-read: Got backchannel message\n");
  gavl_msg_dump(msg, 2);

  switch(msg->NS)
    {
    case GAVL_MSG_NS_SRC:
      switch(msg->ID)
        {
        case GAVL_CMD_SRC_SELECT_TRACK:
          {
          int t;
          t = gavl_msg_get_arg_int(msg, 0);
          
          gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Select track: %d", t);
          /* Select input track */
          select_track(t);

          gavftools_src = h->src;
          }
          break;
        case GAVL_CMD_SRC_SEEK:
          {
          int64_t t;
          int scale;
          
          /* To the caller we are async. */
          
          t = gavl_msg_get_arg_long(msg, 0);
          scale = gavl_msg_get_arg_int(msg, 1);

          gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Seek: %"PRId64" %d", t, scale);
          bg_input_plugin_seek(h, t, scale);
          gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Seek done");
          }
          break;
        case GAVL_CMD_SRC_RESUME:
          {
          bg_input_plugin_resume(h);
          }
          break;
        case GAVL_CMD_SRC_PAUSE:
          {
          bg_input_plugin_pause(h);
          }
          break;
        case GAVL_CMD_SRC_START:
          {
          /* Called implicitely */
          //  bg_input_plugin_start(h);

          /* Set up media connector */
          
          }
          break;
        }
    case GAVL_MSG_NS_GAVF:
      switch(msg->ID)
        {
        case GAVL_CMD_GAVF_SELECT_STREAM:
          {
          int id = gavl_msg_get_arg_int(msg, 0);
          int on = gavl_msg_get_arg_int(msg, 1);

          bg_media_source_set_msg_action_by_id(h->src, id,
                                               on ? 
                                               BG_STREAM_ACTION_READRAW : BG_STREAM_ACTION_DECODE);
          }
        }
      
    }
  
  return 1;
  }

/* Play one track. If return value is non-zero, decode the next track. */
#if 0
static int play_track(bg_plugin_handle_t * h, int track)
  {
  bg_controllable_t * controllable;
  
  gavl_dictionary_t * track_info;
  int num_msg_streams; 
  gavl_dictionary_t m;
  bg_mediaconnector_init(&conn);  
  
  bg_input_plugin_set_track(h, track);
  track_info = bg_input_plugin_get_track_info(h, track);

  num_msg_streams     = gavl_track_get_num_msg_streams(track_info);
  
  /* Extract metadata */
  gavl_dictionary_init(&m);
  gavl_dictionary_copy(&m, gavl_track_get_metadata(track_info));

  gavftools_set_output_metadata(&m);
  gavftools_set_stream_actions(h->src);
  
  bg_media_source_set_msg_action_by_id(h->src, GAVL_META_STREAM_ID_MSG_PROGRAM, BG_STREAM_ACTION_DECODE);
  
  /* Start plugin */
  bg_input_plugin_start(h);
  
  bg_mediaconnector_set_from_source(&conn, h->src);
  
  if(seek_pos > 0.0)
    {
    gavl_time_t seek_time;
    
    if(!gavl_track_can_seek(track_info))
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "%s is not seekable",
             input_file);
      return 0;
      }
    seek_time = gavl_seconds_to_time(seek_pos);

    bg_input_plugin_seek(h, &seek_time, GAVL_TIME_SCALE);
    }
  
  bg_mediaconnector_create_conn(&conn);

  if(!bg_plug_start_program(gavftools_out_plug, &m, 0))
    return 0;

  /* Initialize output plug */
  if(!bg_plug_setup_writer(gavftools_out_plug, &conn))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Setting up plug writer failed");
    return 0;
    }
  
#if 1
  
  bg_mediaconnector_start(&conn);
  
  /* Flush events */
  if(open_ctrl.evt_sink)  
    bg_msg_sink_iteration(open_ctrl.evt_sink);
  
  if(h->plugin->get_controllable &&
     (controllable = h->plugin->get_controllable(h->priv)))
    {
    /*Disconnect */
    if(open_ctrl.evt_sink)
      {
      bg_controllable_disconnect(controllable, &open_ctrl);
      bg_control_cleanup(&open_ctrl);
      memset(&open_ctrl, 0, sizeof(open_ctrl));
      }
    
    /* Reconnect */
    //    bg_controllable_connect(controllable, bg_plug_get_control(gavftools_out_plug));
    }
#endif
  
  while(1)
    {
    if(gavftools_stop())
      return 0;
    
    if(!bg_mediaconnector_iteration(&conn))
      break;

    if(got_select_track >= 0)
      break;
    }
  
  if(got_select_track >= 0)
    bg_plug_finish_program(gavftools_out_plug, 1);
  else
    bg_plug_finish_program(gavftools_out_plug, 0);

#if 0  
  if(h->plugin->get_controllable &&
     (controllable = h->plugin->get_controllable(h->priv)))
    bg_controllable_disconnect(controllable, bg_plug_get_control(gavftools_out_plug));
#endif
  
  bg_mediaconnector_free(&conn);
  bg_mediaconnector_init(&conn);
  
  return 1;
  }
#endif

/* Main */

int main(int argc, char ** argv)
  {
  int ret = EXIT_FAILURE;
  
  gavl_dictionary_t m;

  bg_app_init("gavf-read", TRS("Open media locations and output a gavf stream"));
  
  //  gavf_t * g;
  
  memset(&open_ctrl, 0, sizeof(open_ctrl));
  
  gavl_dictionary_init(&m); 
  
  gavftools_block_sigpipe();

  gavftools_init();

  gavftools_set_cmdline_parameters(global_options);
  
  /* Handle commandline options */
  bg_cmdline_init(&app_data);
  bg_cmdline_parse(global_options, &argc, &argv, NULL);

  if(!bg_cmdline_check_unsupported(argc, argv))
    return ret;
  
  /* Create out plug */

  gavftools_out_plug = gavftools_create_out_plug();

  
  
  if(!bg_plug_open_location(gavftools_out_plug, gavftools_out_file))
    {
    //    gavl_dictionary_free(&m);
    return ret;
    }

#if 0  
  bg_controllable_init(&out_ctrl,
                       bg_msg_sink_create(handle_msg_out, NULL, 1),
                       bg_msg_hub_create(1));

  bg_controllable_connect(&out_ctrl, bg_plug_get_control(gavftools_out_plug));
#endif
  
  if(!input_file)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "No input file or source given");
    return ret;
    }
  
  /* */

  bg_control_init(&open_ctrl, bg_msg_sink_create(handle_msg_open, gavftools_out_plug, 0));

  /* Open input file */

  if(!(h = bg_input_plugin_load(input_file)))
    return ret;

  gavftools_ctrl_in = &h->ctrl_ext;
  
  if(!(gavftools_media_info_in = bg_input_plugin_get_media_info(h)))
    return ret;

  gavftools_run(handle_msg_out, NULL);
  
  ret = EXIT_SUCCESS;
  
  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Cleaning up");
  
  /* Cleanup */
  if(audio_actions)
    free(audio_actions);
  if(video_actions)
    free(video_actions);
  if(text_actions)
    free(text_actions);
  if(overlay_actions)
    free(overlay_actions);
  
  if(h)
    bg_plugin_unref(h);
  
  gavftools_cleanup();

  bg_control_cleanup(&open_ctrl);
  
  return ret;
  }
