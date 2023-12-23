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

// #define INFO_WINDOW

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <config.h>
#include <gmerlin/player.h>
#include <gmerlin/pluginregistry.h>
#include <gavl/gavl.h>
#include <gavl/metatags.h>

#include <gmerlin/utils.h>
#include <gmerlin/cmdline.h>
#include <gmerlin/log.h>
#include <gmerlin/translation.h>
#include <gmerlin/bggavl.h>
#include <gmerlin/iconfont.h>
#include <gmerlin/frontend.h>
#include <gmerlin/application.h>
#include <gmerlin/recordingdevice.h>

#ifdef INFO_WINDOW
#include <gtk/gtk.h>
#include <gui_gtk/infowindow.h>
#endif // INFO_WINDOW

#define LOG_DOMAIN "gmerlin-play"

/* In commandline apps, global variables are allowed :) */


int num_tracks;
int current_track = -1;

bg_player_t * player;

bg_controllable_t * player_ctrl;

bg_cfg_ctx_t * cfg;


bg_plugin_handle_t * input_handle = NULL;
int display_time = 1;

char ** gmls = NULL;
int gml_index = 0;


const bg_plugin_info_t * ov_info = NULL;
char * window_id = NULL;

int subtitle_stream = -1;
int audio_stream    = 0;

char * track_spec = NULL;
char * track_spec_ptr;

static bg_frontend_t * frontend = NULL;

#ifdef HAVE_NCURSES
static int do_ncurses = 0;
#endif

/*
 *  Commandline options stuff
 */



static void opt_nt(void * data, int * argc, char *** _argv, int arg)
  {
  display_time = 0;
  }

#ifdef HAVE_NCURSES
static void opt_nc(void * data, int * argc, char *** _argv, int arg)
  {
  display_time = 0;
  do_ncurses = 1;
  }
#endif

static void opt_vol(void * data, int * argc, char *** _argv, int arg)
  {
  if(arg >= *argc)
    {
    fprintf(stderr, "Option -vol requires an argument\n");
    exit(-1);
    }
  bg_player_set_volume(player_ctrl->cmd_sink, strtod((*_argv)[arg], NULL));
  bg_cmdline_remove_arg(argc, _argv, arg);
  }

static void opt_tracks(void * data, int * argc, char *** _argv, int arg)
  {
  if(arg >= *argc)
    {
    fprintf(stderr, "Option -tracks requires an argument\n");
    exit(-1);
    }
  track_spec = gavl_strrep(track_spec, (*_argv)[arg]);
  track_spec_ptr = track_spec;
  bg_cmdline_remove_arg(argc, _argv, arg);
  }

static void opt_as(void * data, int * argc, char *** _argv, int arg)
  {
  if(arg >= *argc)
    {
    fprintf(stderr, "Option -as requires an argument\n");
    exit(-1);
    }
  audio_stream = atoi((*_argv)[arg]) - 1;
  bg_cmdline_remove_arg(argc, _argv, arg);
  }

static void opt_ss(void * data, int * argc, char *** _argv, int arg)
  {
  if(arg >= *argc)
    {
    fprintf(stderr, "Option -ss requires an argument\n");
    exit(-1);
    }
  subtitle_stream = atoi((*_argv)[arg]) - 1;
  bg_cmdline_remove_arg(argc, _argv, arg);
  }


// static void opt_help(void * data, int * argc, char *** argv, int arg);

static bg_cmdline_arg_t global_options[] =
  {
   BG_PLUGIN_OPT_INPUT,
    {
      .arg =         "-aud",
      .help_arg =    "<audio_options>",
      .help_string = "Set audio processing options",
    },
    {
      .arg =         "-vid",
      .help_arg =    "<video_options>",
      .help_string = "Set video processing options",
    },
    {
      .arg =         "-vis",
      .help_arg =    "<visualization options>",
      .help_string = "Set visualization options",
    },
    BG_PLUGIN_OPT_OA,
    BG_PLUGIN_OPT_OV,
    BG_PLUGIN_OPT_FA,
    BG_PLUGIN_OPT_FV,
    {
      .arg =         "-inopt",
      .help_arg =    "<input_options>",
      .help_string = "Set generic input options",
    },
    {
      .arg =         "-osd",
      .help_arg =    "<osd_options>",
      .help_string = "Set OSD options",
    },
    {
      .arg =         "-nt",
      .help_string = "Disable time display",
      .callback =    opt_nt,
    },
#ifdef HAVE_NCURSES
    {
      .arg =         "-nc",
      .help_string = "Use ncurses frontend",
      .callback =    opt_nc,
    },
#endif
    {
      .arg =         "-vol",
      .help_arg =    "<volume>",
      .help_string = "Set volume (0.0 - 1.0)",
      .callback =    opt_vol,
    },
    {
      .arg =         "-tracks",
      .help_arg =    "<track_spec>",
      .help_string = "<track_spec> can be a ranges mixed with comma separated tracks",
      .callback =    opt_tracks,
    },
    {
      .arg =         "-as",
      .help_arg =    "<idx>",
      .help_string = "Selects audio stream index (starting with 1)",
      .callback =    opt_as,
    },
    {
      .arg =         "-ss",
      .help_arg =    "<idx>",
      .help_string = "Selects subtitle stream index (starting with 1)",
      .callback =    opt_ss,
    },
    BG_OPT_LIST_RECORDERS,
    BG_PLUGIN_OPT_RA,
    BG_PLUGIN_OPT_RV,
    BG_PLUGIN_OPT_LIST_INPUT,
    BG_PLUGIN_OPT_LIST_OA,
    BG_PLUGIN_OPT_LIST_OV,
    BG_PLUGIN_OPT_LIST_FA,
    BG_PLUGIN_OPT_LIST_FV,
    BG_PLUGIN_OPT_LIST_OPTIONS,
    { /* End of options */ }
  };

static void update_global_options()
  {
  bg_cmdline_arg_set_cfg_ctx(global_options, "-aud", &cfg[BG_PLAYER_CFG_AUDIO]);
  bg_cmdline_arg_set_cfg_ctx(global_options, "-vid", &cfg[BG_PLAYER_CFG_VIDEO]);

  bg_cmdline_arg_set_cfg_ctx(global_options, "-osd", &cfg[BG_PLAYER_CFG_OSD]);
  bg_cmdline_arg_set_cfg_ctx(global_options, "-inopt", &cfg[BG_PLAYER_CFG_INPUT]);

  bg_cmdline_arg_set_cfg_ctx(global_options, "-vis", &cfg[BG_PLAYER_CFG_VISUALIZATION]);
  }

/* Input plugin stuff */

static int play_track(bg_player_t * player, const char * gml)
  {
  bg_player_load_uri(player_ctrl->cmd_sink, gml, 1);
  return 1;
  }


#ifdef INFO_WINDOW
static gboolean idle_callback(gpointer data)
  {
  bg_msg_t * msg;
  bg_msg_queue_t * q = (bg_msg_queue_t *)data;
  
  msg = bg_msg_queue_try_lock_read(q);
  if(!msg)
    return TRUE;
  
  if(!handle_message(player, msg))
    gtk_main_quit();
  bg_msg_queue_unlock_read(q);
  return TRUE;
  }

static void info_close_callback(bg_gtk_info_window_t * info_window,
                                void * data)
  {
  fprintf(stderr, "Info window now closed\n");
  }
#endif

const bg_cmdline_app_data_t app_data =
  {
    .package =  PACKAGE,
    .version =  VERSION,
    .synopsis = TRS("[options] gml...\n"),
    .help_before = TRS("Commandline mediaplayer\n"),
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


int main(int argc, char ** argv)
  {

#ifdef INFO_WINDOW
  bg_gtk_info_window_t * info_window;
#endif
  bg_cfg_section_t * cfg_section;
  gavl_timer_t * timer;

  bg_app_init("gmerlin_play", TRS("Gmerlin commandline player"));
  

  timer = gavl_timer_create();
  gavl_timer_start(timer);
  
  bg_iconfont_init();

#ifdef INFO_WINDOW
  bg_gtk_init(&argc, &argv);
#endif

  /* Create plugin regsitry */
  bg_plugins_init();
  
  player = bg_player_create();
  /* Quit when playqueue is empty */
  bg_player_set_empty_mode(player, 1);
  
  cfg = bg_cfg_ctx_copy_array(bg_player_get_cfg(player));

  player_ctrl = bg_player_get_controllable(player);
  
  bg_player_set_volume(player_ctrl->cmd_sink, 0.5);
  
#ifdef INFO_WINDOW
  info_window =
    bg_gtk_info_window_create(player, info_close_callback, NULL);
  bg_gtk_info_window_show(info_window);
#endif
  
  /* Apply default options */
  cfg_section = bg_cfg_registry_find_section(bg_cfg_registry, "player");
  
  bg_cfg_ctx_set_cb_array(cfg, NULL, NULL);
  
  /* Create config sections */
  bg_cfg_ctx_array_create_sections(cfg, cfg_section);

  
  bg_cfg_ctx_set_sink_array(cfg, player_ctrl->cmd_sink);

  update_global_options();
  
  /* Get commandline options */
  bg_cmdline_init(&app_data);
  bg_cmdline_parse(global_options, &argc, &argv, NULL);
  
  bg_player_apply_cmdline(cfg);
  
  bg_cfg_ctx_apply_array(cfg);
  
  /* Create frontend */
  
#ifdef HAVE_NCURSES
  if(do_ncurses)
    {
    frontend = bg_frontend_create_player_ncurses(player_ctrl);
    }
  else
    {
#endif
    frontend = bg_frontend_create_player_console(player_ctrl, display_time);
#ifdef HAVE_NCURSES
    }
#endif
  
  gmls = bg_cmdline_get_locations_from_args(&argc, &argv);

  if(!gmls)
    {
    fprintf(stderr, "No input files given\n");
    return 0;
    }
  
  /* Start the player thread */

  bg_player_run(player);

  fprintf(stderr, "Setting audio stream %d\n", audio_stream);
  bg_player_set_audio_stream(player_ctrl->cmd_sink, audio_stream);
  
  fprintf(stderr, "Setting subtitle stream %d\n", subtitle_stream);
  bg_player_set_subtitle_stream(player_ctrl->cmd_sink, subtitle_stream);
  
  /* Play first track */
  
  play_track(player, gmls[gml_index]);
  
  /* Main loop */
  
#ifndef INFO_WINDOW
  while(1)
    {
    int result;
    gavl_time_t delay_time = GAVL_TIME_SCALE / 100;

    result = bg_frontend_ping(frontend, gavl_timer_get(timer));

    if(bg_frontend_finished(frontend))
      break;

    if(!result)
      gavl_time_delay(&delay_time);
    }
#else
  g_timeout_add(10, idle_callback, message_queue);
  gtk_main();
#endif // INFO_WINDOW

  bg_player_quit(player);
  bg_player_destroy(player);

  if(input_handle)
    bg_plugin_unref(input_handle);
  
  bg_cfg_registry_save();
  
  bg_plugins_cleanup();
  bg_cfg_registry_cleanup();
  
  fprintf(stderr, "gmerlin_play finished\n");
  
  return 0;
  }
