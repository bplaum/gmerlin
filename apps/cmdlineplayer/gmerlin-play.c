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
#include <gmerlin/resourcemanager.h>


#define LOG_DOMAIN "gmerlin-play"

/* In commandline apps, global variables are allowed :) */


int num_tracks;
int current_track = -1;

bg_player_t * player;

bg_controllable_t * player_ctrl;

bg_cfg_ctx_t * cfg;
int display_time = 1;

char ** uris = NULL;
int uri_index = 0;


const bg_plugin_info_t * ov_info = NULL;
char * window_id = NULL;

int subtitle_stream = -1;
int audio_stream    = 0;

char * track_spec = NULL;
char * track_spec_ptr;

static bg_frontend_t ** frontends = NULL;
int num_frontends = 0;

gavl_array_t fe_arr;

/*
 *  Commandline options stuff
 */

static void opt_fullscreen(void * data, int * argc, char *** _argv, int arg)
  {
  bg_player_set_fullscreen(player_ctrl->cmd_sink, 1);
  }

static void opt_vis(void * data, int * argc, char *** _argv, int arg)
  {
  char * plugin_name;
  gavl_dictionary_t vars;
  gavl_dictionary_init(&vars);
  

  if(arg >= *argc)
    {
    fprintf(stderr, "Option -vis requires an argument\n");
    exit(-1);
    }

  plugin_name = gavl_strdup((*_argv)[arg]);
  
  gavl_url_get_vars(plugin_name, &vars);

  bg_cfg_section_apply(&vars,
                       cfg[BG_PLAYER_CFG_VISUALIZATION].p,
                       bg_cfg_section_set_parameter_func, cfg[BG_PLAYER_CFG_VISUALIZATION].s);
  
  bg_player_set_visualization(player_ctrl->cmd_sink, plugin_name);
  bg_cmdline_remove_arg(argc, _argv, arg);
  
  gavl_dictionary_free(&vars);
  free(plugin_name);
  }

static void opt_nt(void * data, int * argc, char *** _argv, int arg)
  {
  display_time = 0;
  }

static void opt_fe(void * data, int * argc, char *** argv, int arg)
  {
  if(arg >= *argc)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Option -fe requires an argument");
    exit(-1);
    }

  bg_frontend_set_option(&fe_arr, (*argv)[arg]);
  bg_cmdline_remove_arg(argc, argv, arg);
  }


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
      .help_arg =    "plugin_name[?option1=value1[&...]]",
      .help_string = "Set visualization plugin",
      .callback =    opt_vis,
    },
    {
      .arg =         "-fullscreen",
      .help_string = "Switch to fullscreen mode",
      .callback =    opt_fullscreen,
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
    BG_PLUGIN_OPT_LIST_INPUT,
    BG_OPT_LIST_OA,
    BG_OPT_LIST_OV,
    BG_PLUGIN_OPT_LIST_FA,
    BG_PLUGIN_OPT_LIST_FV,
    BG_PLUGIN_OPT_LIST_OPTIONS,
    {
      .arg =         "-fe",
      .help_arg = "frontend1[,frontend2]",
      .help_string = TRS("Comma separated list of frontends. Use -list-fe to list available frontends. The prefix fe_ can be omitted"),
      .callback = opt_fe,
    },
    {
      .arg =         "-list-fe",
      .help_string = TRS("List available frontends"),
      .callback = bg_plugin_registry_list_fe_renderer,
    },
    { /* End of options */ }
  };

static void update_global_options()
  {
  bg_cmdline_arg_set_cfg_ctx(global_options, "-aud", &cfg[BG_PLAYER_CFG_AUDIO]);
  bg_cmdline_arg_set_cfg_ctx(global_options, "-vid", &cfg[BG_PLAYER_CFG_VIDEO]);

  bg_cmdline_arg_set_cfg_ctx(global_options, "-osd", &cfg[BG_PLAYER_CFG_OSD]);
  bg_cmdline_arg_set_cfg_ctx(global_options, "-inopt", &cfg[BG_PLAYER_CFG_INPUT]);
  }

/* Input plugin stuff */

static int play_track(bg_player_t * player, const char * uri)
  {
  bg_player_load_uri(player_ctrl->cmd_sink, uri, 1);
  return 1;
  }



const bg_cmdline_app_data_t app_data =
  {
    .package =  PACKAGE,
    .version =  VERSION,
    .synopsis = TRS("[options] uri...\n"),
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
  bg_cfg_section_t * cfg_section;
  
  gavl_array_init(&fe_arr);
  bg_frontend_set_option(&fe_arr, "console");
  
  bg_app_init("gmerlin_play", TRS("Gmerlin commandline player"), "renderer");
  
  
  bg_iconfont_init();


  /* Create plugin regsitry */
  bg_plugins_init();
  
  player = bg_player_create();
  /* Quit when playqueue is empty */
  bg_player_set_empty_mode(player, 1);
  
  cfg = bg_cfg_ctx_copy_array(bg_player_get_cfg(player));

  player_ctrl = bg_player_get_controllable(player);
  
  bg_player_set_volume(player_ctrl->cmd_sink, 0.5);
  
  
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
  
  frontends = bg_frontends_create(player_ctrl,
                                  BG_PLUGIN_FRONTEND_RENDERER, &fe_arr, &num_frontends);
  
  uris = bg_cmdline_get_locations_from_args(&argc, &argv);

  if(!uris)
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
  
  play_track(player, uris[uri_index]);
  
  /* Main loop */
  
  while(1)
    {
    int result;
    gavl_time_t delay_time = GAVL_TIME_SCALE / 100;

    if(bg_player_get_status(player) == BG_PLAYER_STATUS_QUIT)
      break;
    
    result = bg_frontends_ping(frontends, num_frontends);

    
    if(!result)
      gavl_time_delay(&delay_time);
    }
  
  bg_player_quit(player);
  bg_player_destroy(player);
  
  //  bg_cfg_registry_save();
  
  fprintf(stderr, "gmerlin_play finished\n");

  bg_global_cleanup();
  
  return 0;
  }
