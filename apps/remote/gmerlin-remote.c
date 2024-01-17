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


int interactive = 0;

#ifdef HAVE_NCURSES
static int do_ncurses = 0;
#endif

#ifdef HAVE_DBUS
static int do_mpris = 0;
#endif

static int do_gmerlin = 0;
static int do_upnp    = 0;

static int have_state = 0;

bg_http_server_t * srv = NULL;
bg_controllable_t proxy_ctrl;

static char * label = NULL;

static void cmd_scan(void * data, int * argc, char *** _argv, int arg)
  {
  bg_resourcemanager_get_controllable();
  bg_resource_list_by_class(GAVL_META_MEDIA_CLASS_BACKEND_RENDERER, 1, 3*GAVL_TIME_SCALE);
  }
  
static void cmd_play(void * data, int * argc, char *** _argv, int arg)
  {
  gavl_msg_t * msg = bg_msg_sink_get(backend_ctrl->cmd_sink);
  
  gavl_msg_set_id_ns(msg, BG_PLAYER_MSG_ACCEL, BG_MSG_NS_PLAYER);
  gavl_msg_set_arg_int(msg, 0, BG_PLAYER_ACCEL_PLAY);
  
  bg_msg_sink_put(backend_ctrl->cmd_sink);
  }

static void cmd_next(void * data, int * argc, char *** _argv, int arg)
  {
  gavl_msg_t * msg = bg_msg_sink_get(backend_ctrl->cmd_sink);
  
  gavl_msg_set_id_ns(msg, BG_PLAYER_MSG_ACCEL, BG_MSG_NS_PLAYER);
  gavl_msg_set_arg_int(msg, 0, BG_PLAYER_ACCEL_NEXT);
  
  bg_msg_sink_put(backend_ctrl->cmd_sink);

  }

static void cmd_prev(void * data, int * argc, char *** _argv, int arg)
  {
  gavl_msg_t * msg = bg_msg_sink_get(backend_ctrl->cmd_sink);


  gavl_msg_set_id_ns(msg, BG_PLAYER_MSG_ACCEL, BG_MSG_NS_PLAYER);
  gavl_msg_set_arg_int(msg, 0, BG_PLAYER_ACCEL_PREV);
  
  bg_msg_sink_put(backend_ctrl->cmd_sink);
  }

static void cmd_stop(void * data, int * argc, char *** _argv, int arg)
  {
  gavl_msg_t * msg = bg_msg_sink_get(backend_ctrl->cmd_sink);

  gavl_msg_set_id_ns(msg, BG_PLAYER_MSG_ACCEL, BG_MSG_NS_PLAYER);
  gavl_msg_set_arg_int(msg, 0, BG_PLAYER_ACCEL_STOP);

  bg_msg_sink_put(backend_ctrl->cmd_sink);
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


#ifdef HAVE_NCURSES
static void opt_nc(void * data, int * argc, char *** argv, int arg)
  {
  do_ncurses = 1;
  interactive = 1;
  }
#endif

#ifdef HAVE_DBUS
static void opt_mpris(void * data, int * argc, char *** argv, int arg)
  {
  do_mpris = 1;
  interactive = 1;
  }
#endif

static void opt_upnp(void * data, int * argc, char *** argv, int arg)
  {
  do_upnp = 1;
  interactive = 1;
  }

static void opt_gmerlin(void * data, int * argc, char *** argv, int arg)
  {
  do_gmerlin = 1;
  interactive = 1;
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
#ifdef HAVE_NCURSES
    {
      .arg =         "-nc",
      .help_string = TRS("Use ncurses frontend"),
      .callback =    opt_nc,
    },
#endif
#ifdef HAVE_DBUS
    {
      .arg =         "-mpris",
      .help_string = TRS("Use mpris frontend"),
      .callback =    opt_mpris,
    },
#endif
    {
      .arg =         "-upnp",
      .help_string = TRS("Use upnp frontend"),
      .callback =    opt_upnp,
    },
    {
      .arg =         "-gmerlin",
      .help_string = TRS("Use websocket based gmerlin frontend"),
      .callback =    opt_gmerlin,
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

static int handle_backend_message(void * data, gavl_msg_t * msg)
  {
  if((msg->NS == BG_MSG_NS_STATE) &&
     (msg->ID == BG_MSG_STATE_CHANGED))
    {
    const char * ctx = NULL;
    const char * var = NULL;
    int last = 0;
    gavl_value_t val;
    
    gavl_value_init(&val);

    gavl_msg_get_state(msg, &last, &ctx, &var, &val, NULL);
    
    //    fprintf(stderr, "Got backend message\n");
    //    gavl_msg_dump(msg, 2);
    
    if(last && !strcmp(ctx, BG_APP_STATE_NETWORK_NODE))
      have_state = 1;

    /* Send application icon via proxy */
    if(!strcmp(ctx, BG_APP_STATE_NETWORK_NODE))
      {
      if(!strcmp(var, GAVL_META_LABEL))
        {
        gavl_msg_t * msg1;
        gavl_value_t name_val;
        gavl_value_init(&name_val);

        
        gavl_value_set_string_nocopy(&name_val, bg_sprintf("%s (via proxy)", gavl_value_get_string(&val)));
        
        msg1 = bg_msg_sink_get(proxy_ctrl.evt_sink);
                                     
        gavl_msg_set_state_nocopy(msg1, BG_MSG_STATE_CHANGED, last, ctx, GAVL_META_LABEL, &name_val);
        gavl_dictionary_copy(&msg1->header, &msg->header);
        bg_msg_sink_put(proxy_ctrl.evt_sink);
        return 1;
        }
      else if(!strcmp(var, GAVL_META_ICON_NAME) && srv)
        {
        gavl_value_t val_1;
        gavl_array_t * arr;
        gavl_msg_t * msg1;

        gavl_value_t url_val;
        gavl_dictionary_t * url;

        fprintf(stderr, "Icon name: %s\n", gavl_value_get_string(&val));
        
        msg1 = bg_msg_sink_get(proxy_ctrl.evt_sink);
        
        /* TODO: Set Icon URI */
        gavl_value_init(&val_1);
        arr = gavl_value_set_array(&val_1);
        
        gavl_value_init(&url_val);
        url = gavl_value_set_dictionary(&url_val);

        gavl_dictionary_set_string_nocopy(url, GAVL_META_URI,
                                          bg_sprintf("%s" BG_HTTP_APPICON_PATH "%s.png",
                                                     bg_http_server_get_root_url(srv),
                                                     gavl_value_get_string(&val)));
        
        gavl_dictionary_set_string(url, GAVL_META_MIMETYPE, "image/png");
        gavl_dictionary_set_int(url, GAVL_META_WIDTH, 48);
        gavl_dictionary_set_int(url, GAVL_META_HEIGHT, 48);

        gavl_array_splice_val_nocopy(arr, 0, 0, &url_val);

        //  fprintf(stderr, "Icon arr:\n");
        //  gavl_array_dump(arr, 2);
        

        gavl_msg_set_state_nocopy(msg1, BG_MSG_STATE_CHANGED, last, ctx, GAVL_META_ICON_URL, &val_1);
        
        gavl_dictionary_copy(&msg1->header, &msg->header);
        bg_msg_sink_put(proxy_ctrl.evt_sink);
        return 1;
        }
      
      }

    gavl_value_free(&val);
    }
  
  bg_msg_sink_put_copy(proxy_ctrl.evt_sink, msg);
  
  return 1;
  }

static int handle_frontend_command(void * data, gavl_msg_t * msg)
  {
  bg_msg_sink_put_copy(backend_ctrl->cmd_sink, msg);
  return 1;
  }

int main(int argc, char ** argv)
  {
  gavl_dictionary_t dev;
  bg_msg_sink_t * sink;

  gavl_time_t delay_time = GAVL_TIME_SCALE / 50;

#ifdef HAVE_NCURSES
  bg_frontend_t * fe_ncurses = NULL;
#endif

#ifdef HAVE_DBUS
  bg_frontend_t * fe_mpris = NULL;
#endif

  bg_frontend_t * fe_upnp = NULL;
  bg_frontend_t * fe_gmerlin = NULL;

  bg_plugin_handle_t * backend;
  
  //  bg_websocket_connection_t * remote;
  
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
    remote_addr = BG_BACKEND_URI_SCHEME_GMERLIN_RENDERER"://localhost:10101/ws/renderer";

  srv = create_server();

  gavl_dictionary_set_string(&dev, GAVL_META_URI, remote_addr);

  if(label)
    gavl_dictionary_set_string(&dev, GAVL_META_LABEL, label);
  
  if(!(backend = bg_backend_handle_create(&dev)))
    goto fail;
  
  backend_ctrl = bg_backend_handle_get_controllable(backend);

  bg_controllable_init(&proxy_ctrl,
                       bg_msg_sink_create(handle_frontend_command, NULL, 1),
                       bg_msg_hub_create(1));
  
  sink = bg_msg_sink_create(handle_backend_message, NULL, 1);
  bg_msg_hub_connect_sink(backend_ctrl->evt_hub, sink);
  
  //  fprintf(stderr, "Got backend state:\n");
  //  gavl_dictionary_dump(&backend_state, 2);
  
  /* Create frontends */
#ifdef HAVE_NCURSES
  if(do_ncurses)
    fe_ncurses = bg_frontend_create_player_ncurses(&proxy_ctrl);
#endif

#ifdef HAVE_DBUS
  if(do_mpris)
    fe_mpris = bg_frontend_create_player_mpris2(&proxy_ctrl,
                                                "org.mpris.MediaPlayer2.gmerlin-remote",
                                                "gmerlin-remote");
#endif
    
  if(do_upnp)
    {
    
    //      remote_addr
      
    fe_upnp = bg_frontend_create_player_upnp(&proxy_ctrl);
    }
  
  if(do_gmerlin)
    fe_gmerlin = bg_frontend_create_player_gmerlin(&proxy_ctrl);
  
  bg_cmdline_parse(commands, &argc, &argv, NULL);
  
  if(interactive)
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
      
#ifdef HAVE_NCURSES
      if(fe_ncurses)
        ret += bg_frontend_ping(fe_ncurses, gavl_timer_get(timer));
#endif
      
#ifdef HAVE_DBUS
      if(fe_mpris)
        ret += bg_frontend_ping(fe_mpris, gavl_timer_get(timer));
#endif

      if(fe_upnp)
        ret += bg_frontend_ping(fe_upnp, gavl_timer_get(timer));

      if(fe_gmerlin)
        ret += bg_frontend_ping(fe_gmerlin, gavl_timer_get(timer));

      if(srv)
        ret += bg_http_server_iteration(srv);
      
      if(!ret)
        gavl_time_delay(&delay_time);
      }

    gavl_timer_destroy(timer);
    
    }

  
  fail:
  
#ifdef HAVE_NCURSES
  if(fe_ncurses)
    bg_frontend_destroy(fe_ncurses);
#endif
  
#ifdef HAVE_DBUS
  if(fe_mpris)
    bg_frontend_destroy(fe_mpris);
#endif
  
  if(fe_upnp)
    bg_frontend_destroy(fe_upnp);
  if(fe_gmerlin)
    bg_frontend_destroy(fe_gmerlin);

  if(srv)
    bg_http_server_destroy(srv);

  if(backend)
    bg_plugin_unref(backend);

  gavl_dictionary_free(&dev);

  return 0;
  }
