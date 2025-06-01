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
#include <signal.h>
#include <locale.h>

#include <uuid/uuid.h>

#include <config.h>

#include <gavl/metatags.h>
#include <gavl/gavlsocket.h>


#include <gmerlin/cfg_registry.h>
#include <gmerlin/cfgctx.h>
#include <gmerlin/cmdline.h>
#include <gmerlin/utils.h>
// #include <gmerlin/remote.h>
#include <gmerlin/translation.h>
#include <gmerlin/player.h>
#include <gmerlin/playermsg.h>

#include <gmerlin/bgmsg.h>
#include <gmerlin/http.h>
#include <gmerlin/httpserver.h>
#include <gmerlin/websocket.h>
#include <gmerlin/frontend.h>

#include <gmerlin/bggavl.h>
#include <gmerlin/log.h>
#include <gmerlin/application.h>
#include <gmerlin/resourcemanager.h>



#define LOG_DOMAIN "gmerlin-remote"

#ifdef HAVE_DBUS
#include <gmerlin/bgdbus.h> 
#endif


gavl_time_t duration = GAVL_TIME_UNDEFINED;

static char * remote_addr = NULL;
static int local_port = 0;


bg_controllable_t * backend_ctrl = NULL;
bg_plugin_handle_t * backend = NULL;

bg_http_server_t * srv = NULL;

static char * label = NULL;

static gavl_array_t fe_arr;

static void flush_command()
  {
  //  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Flush 1");
  while(bg_backend_handle_ping(backend) > 0)
    ;
  //  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Flush 2");
  }

static void cmd_scan(void * data, int * argc, char *** _argv, int arg)
  {
  bg_resourcemanager_get_controllable();
  bg_resource_list_by_class(GAVL_META_CLASS_BACKEND_RENDERER, 1, 3*GAVL_TIME_SCALE);
  exit(EXIT_SUCCESS);
  }
  
static void cmd_play(void * data, int * argc, char *** _argv, int arg)
  {
  gavl_msg_t * msg = bg_msg_sink_get(backend_ctrl->cmd_sink);
  
  gavl_msg_set_id_ns(msg, BG_PLAYER_MSG_ACCEL, BG_MSG_NS_PLAYER);
  gavl_msg_set_arg_int(msg, 0, BG_PLAYER_ACCEL_PLAY);
  
  bg_msg_sink_put(backend_ctrl->cmd_sink);
  flush_command();
  }

static void cmd_next(void * data, int * argc, char *** _argv, int arg)
  {
  gavl_msg_t * msg = bg_msg_sink_get(backend_ctrl->cmd_sink);
  
  gavl_msg_set_id_ns(msg, BG_PLAYER_MSG_ACCEL, BG_MSG_NS_PLAYER);
  gavl_msg_set_arg_int(msg, 0, BG_PLAYER_ACCEL_NEXT);
  
  bg_msg_sink_put(backend_ctrl->cmd_sink);
  flush_command();
  }

static void cmd_prev(void * data, int * argc, char *** _argv, int arg)
  {
  gavl_msg_t * msg = bg_msg_sink_get(backend_ctrl->cmd_sink);


  gavl_msg_set_id_ns(msg, BG_PLAYER_MSG_ACCEL, BG_MSG_NS_PLAYER);
  gavl_msg_set_arg_int(msg, 0, BG_PLAYER_ACCEL_PREV);
  
  bg_msg_sink_put(backend_ctrl->cmd_sink);
  flush_command();
  }

static void cmd_stop(void * data, int * argc, char *** _argv, int arg)
  {
  gavl_msg_t * msg = bg_msg_sink_get(backend_ctrl->cmd_sink);

  gavl_msg_set_id_ns(msg, BG_PLAYER_MSG_ACCEL, BG_MSG_NS_PLAYER);
  gavl_msg_set_arg_int(msg, 0, BG_PLAYER_ACCEL_STOP);

  bg_msg_sink_put(backend_ctrl->cmd_sink);
  flush_command();
  }

static void cmd_toggle_mute(void * data, int * argc, char *** _argv, int arg)
  {
  bg_player_toggle_mute(backend_ctrl->cmd_sink);
  }


static void cmd_pause(void * data, int * argc, char *** _argv, int arg)
  {
  gavl_msg_t * msg = bg_msg_sink_get(backend_ctrl->cmd_sink);
  bg_player_pause_m(msg);
  bg_msg_sink_put(backend_ctrl->cmd_sink);
  flush_command();
  }

static void cmd_addplay(void * data, int * argc, char *** _argv, int arg)
  {
  char ** argv = *_argv;
  gavl_dictionary_t m;
  gavl_dictionary_init(&m);
  
  if(arg >= *argc)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Option -addplay requires an argument\n");
    exit(-1);
    }

  bg_player_load_uri(backend_ctrl->cmd_sink, argv[arg], 1);
  bg_cmdline_remove_arg(argc, _argv, arg);
  flush_command();
  }

static void cmd_add(void * data, int * argc, char *** _argv, int arg)
  {
  char ** argv = *_argv;
  if(arg >= *argc)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Option -add requires an argument\n");
    exit(-1);
    }

  bg_player_load_uri(backend_ctrl->cmd_sink, argv[arg], 0);
  bg_cmdline_remove_arg(argc, _argv, arg);
  flush_command();
  }

static void cmd_volume(void * data, int * argc, char *** _argv, int arg)
  {
  char ** argv = *_argv;
  float vol;
  gavl_msg_t * msg = bg_msg_sink_get(backend_ctrl->cmd_sink);

  if(arg >= *argc)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Option -volume requires an argument\n");
    exit(-1);
    }
  vol = strtod(argv[arg], NULL);
  bg_player_set_volume_m(msg, vol);
  bg_msg_sink_put(backend_ctrl->cmd_sink);
  bg_cmdline_remove_arg(argc, _argv, arg);
  flush_command();
  }

static void cmd_volume_rel(void * data, int * argc, char *** _argv, int arg)
  {
  char ** argv = *_argv;
  float vol;
  gavl_msg_t * msg = bg_msg_sink_get(backend_ctrl->cmd_sink);

  if(arg >= *argc)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Option -volume_rel requires an argument\n");
    exit(-1);
    }

  vol = strtod(argv[arg], NULL);
  bg_player_set_volume_rel_m(msg, vol);
  bg_msg_sink_put(backend_ctrl->cmd_sink);
  bg_cmdline_remove_arg(argc, _argv, arg);
  flush_command();
  }

static void cmd_seek_rel(void * data, int * argc, char *** _argv, int arg)
  {
  char ** argv = *_argv;
  float diff;
  gavl_msg_t * msg = bg_msg_sink_get(backend_ctrl->cmd_sink);

  if(arg >= *argc)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Option -seek_rel requires an argument\n");
    exit(-1);
    }

  diff = strtod(argv[arg], NULL);
  
  bg_player_seek_rel_m(msg, gavl_seconds_to_time(diff));
  bg_msg_sink_put(backend_ctrl->cmd_sink);
  bg_cmdline_remove_arg(argc, _argv, arg);
  flush_command();
  }

static void cmd_seek_perc(void * data, int * argc, char *** _argv, int arg)
  {
  char ** argv = *_argv;
  float perc;
  gavl_msg_t * msg = bg_msg_sink_get(backend_ctrl->cmd_sink);

  if(arg >= *argc)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Option -seek_rel requires an argument\n");
    exit(-1);
    }

  perc = strtod(argv[arg], NULL);
  bg_player_seek_perc_m(msg, perc / 100.0);
  bg_msg_sink_put(backend_ctrl->cmd_sink);
  bg_cmdline_remove_arg(argc, _argv, arg);
  flush_command();
  }

static void cmd_chapter(void * data, int * argc, char *** _argv, int arg)
  {
  char ** argv = *_argv;
  int index;
  gavl_msg_t * msg = bg_msg_sink_get(backend_ctrl->cmd_sink);

  if(arg >= *argc)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Option -chapter requires an argument\n");
    exit(-1);
    }

  if(!strcmp(argv[arg], "+"))
    bg_player_next_chapter_m(msg);
  else if(!strcmp(argv[arg], "-"))
    bg_player_prev_chapter_m(msg);
  else
    {
    index = atoi(argv[arg]);
    bg_player_set_chapter_m(msg, index);
    }
  bg_msg_sink_put(backend_ctrl->cmd_sink);
  bg_cmdline_remove_arg(argc, _argv, arg);
  flush_command();
  }

bg_cmdline_arg_t commands[] =
  {
    {
      .arg =         "-play",
      .help_string = TRS("Play current track"),
      .callback =    cmd_play,
    },
    {
      .arg =         "-next",
      .help_string = TRS("Switch to next track"),
      .callback =    cmd_next,
    },
    {
      .arg =         "-prev",
      .help_string = TRS("Switch to previous track"),
      .callback =    cmd_prev,
    },
    {
      .arg =         "-stop",
      .help_string = TRS("Stop playback"),
      .callback =    cmd_stop,
    },
    {
      .arg =         "-pause",
      .help_string = TRS("Pause playback"),
      .callback =    cmd_pause,
    },
    {
      .arg =         "-mute",
      .help_string = TRS("Toggle mute"),
      .callback =    cmd_toggle_mute,
    },
#if 1
    {
      .arg =         "-add",
      .help_arg =    TRS("<gml>"),
      .help_string = TRS("Add <gml> to the incoming album"),
      .callback =    cmd_add,
    },
    {
      .arg =         "-addplay",
      .help_arg =    TRS("<gml>"),
      .help_string = TRS("Add <gml> to the incoming album and play it"),
      .callback =    cmd_addplay,
    },
#endif
    {
      .arg =         "-volume",
      .help_arg =    TRS("<volume>"),
      .help_string = TRS("Set player volume. <volume> is in dB, 0.0 is max"),
      .callback =    cmd_volume,
    },
    {
      .arg =         "-volume-rel",
      .help_arg =    TRS("<diff>"),
      .help_string = TRS("In- or decrease player volume. <diff> is in dB"),
      .callback =    cmd_volume_rel,
    },
    {
      .arg =         "-seek-rel",
      .help_arg =    TRS("<diff>"),
      .help_string = TRS("Seek relative. <diff> is in seconds."),
      .callback =    cmd_seek_rel,
    },
    {
      .arg =         "-seek-perc",
      .help_arg =    TRS("<percentage>"),
      .help_string = TRS("Seek percentage based. <percentage> is between 0 and 100."),
      .callback =    cmd_seek_perc,
    },
    {
      .arg =         "-chapter",
      .help_arg =    "[num|+|-]",
      .help_string = TRS("Go to the specified chapter. Use '+' and '-' to go to the next or previous chapter respectively"),
      .callback =    cmd_chapter,
    },
    { /* End of options */ }
  };

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

static void opt_addr(void * data, int * argc, char *** argv, int arg)
  {
  
  if(arg >= *argc)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Option -addr requires an argument");
    exit(-1);
    }
  remote_addr = gavl_strrep(remote_addr, (*argv)[arg]);
  bg_cmdline_remove_arg(argc, argv, arg);
  }

static void opt_port(void * data, int * argc, char *** argv, int arg)
  {
  
  if(arg >= *argc)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Option -port requires an argument");
    exit(-1);
    }
  
  local_port = atoi((*argv)[arg]);
  bg_cmdline_remove_arg(argc, argv, arg);
  }

static void opt_label(void * data, int * argc, char *** argv, int arg)
  {
  if(arg >= *argc)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Option -label requires an argument");
    exit(-1);
    }
  
  label = (*argv)[arg];
  bg_cmdline_remove_arg(argc, argv, arg);
  }



static bg_cmdline_arg_t global_options[] =
  {
    {
      .arg =         "-scan",
      .help_string = TRS("Scan for devices"),
      .callback =    cmd_scan,
    },
    {
      .arg =         "-addr",
      .help_arg =    "<addr>",
      .help_string = TRS("Address to connect to. Default is "BG_BACKEND_URI_SCHEME_GMERLIN_RENDERER"://localhost:10101"),
      .callback =    opt_addr,
    },
    {
      .arg =         "-port",
      .help_arg =    "<port>",
      .help_string = TRS("Local port"),
      .callback =    opt_port,
    },
    {
      .arg =         "-label",
      .help_string = TRS("Label to use"),
      .callback =    opt_label,
    },
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

#if 0
static void opt_help(void * data, int * argc, char *** argv, int arg)
  {
  FILE * out = stderr;
  
  fprintf(out, "Usage: %s [options] command\n\n", (*argv)[0]);
  fprintf(out, "Options:\n\n");
  bg_cmdline_print_help(global_options);
  fprintf(out, "\ncommand is of the following:\n\n");
  bg_cmdline_print_help(commands);
  exit(0);
  }
#endif

const bg_cmdline_app_data_t app_data =
  {
    .package =  PACKAGE,
    .version =  VERSION,
    .synopsis = TRS("[options] command\n"),
    .help_before = TRS("Remote control command for the Gmerlin GUI Player\n"),
    .args = (bg_cmdline_arg_array_t[]) { { TRS("Global options"), global_options },
                                       { TRS("Commands"),       commands       },
                                       {  } },

  };

static bg_http_server_t * create_server()
  {
  bg_http_server_t * ret = bg_http_server_create();

  if(local_port > 0)
    bg_http_server_set_default_port(ret, local_port);
  
  bg_http_server_set_static_path(ret, "/static");
  bg_http_server_enable_appicons(ret);
    
  /* Start server */
  
  bg_http_server_start(ret);
  
  
  return ret;
  }



int main(int argc, char ** argv)
  {
  gavl_dictionary_t dev;

  gavl_time_t delay_time = GAVL_TIME_SCALE / 50;

  bg_frontend_t ** frontends = NULL;
  int num_frontends = 0;
  
  
  //  bg_websocket_connection_t * remote;
  
  gavl_array_init(&fe_arr);
    
  bg_plugins_init();

  gavl_dictionary_init(&dev);
  
  bg_app_init("gmerlin-remote", TRS("Gmerlin remote control"), "remote");
  
  setlocale(LC_ALL, "");
  setlocale(LC_NUMERIC, "C");

  bg_handle_sigint();
  
  bg_cmdline_init(&app_data);
  
  if(argc < 2)
    bg_cmdline_print_help(argv[0], 0);
  

  bg_cmdline_init(&app_data);

  bg_cmdline_parse(global_options, &argc, &argv, NULL);
  
  if(!remote_addr)
    remote_addr = BG_BACKEND_URI_SCHEME_GMERLIN_RENDERER"://127.0.0.1:10101/ws/"GAVL_META_CLASS_BACKEND_RENDERER;

  srv = create_server();

  gavl_dictionary_set_string(&dev, GAVL_META_URI, remote_addr);

  if(label)
    gavl_dictionary_set_string(&dev, GAVL_META_LABEL, label);
  
  if(!(backend = bg_backend_handle_create(&dev)))
    goto fail;
  
  backend_ctrl = bg_backend_handle_get_controllable(backend);
    
  bg_cmdline_parse(commands, &argc, &argv, NULL);

  frontends = bg_frontends_create(backend_ctrl,
                                  BG_PLUGIN_FRONTEND_RENDERER, &fe_arr, &num_frontends);
  
  if(num_frontends > 0)
    {
    gavl_timer_t * timer;
    int ret;
    
    timer = gavl_timer_create();
    gavl_timer_start(timer);
    
    while(1)
      {
      if(bg_got_sigint())
        break;
      
      ret = 0;
      
      //      ret += bg_backend_handle_ping(backend);

      bg_msg_sink_iteration(backend_ctrl->evt_sink);
      ret += bg_msg_sink_get_num(backend_ctrl->evt_sink);

      ret += bg_backend_handle_ping(backend);

      ret +=  bg_frontends_ping(frontends, num_frontends);
      
      if(srv)
        ret += bg_http_server_iteration(srv);
      
      if(!ret)
        gavl_time_delay(&delay_time);
      }

    gavl_timer_destroy(timer);
    
    }

  
  fail:
  
  bg_frontends_destroy(frontends, num_frontends);
  
  if(srv)
    bg_http_server_destroy(srv);

  if(backend)
    bg_plugin_unref(backend);

  gavl_dictionary_free(&dev);

  return 0;
  }
