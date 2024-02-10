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


#include <config.h>
#include <gavl/gavlsocket.h>

#include <gmerlin/translation.h>
#include <gmerlin/log.h>
#include <gmerlin/parameter.h>
#include <gmerlin/cfg_registry.h>
#include <gmerlin/cfgctx.h>
#include <gmerlin/cmdline.h>

#include <gmerlin/httpserver.h>
#include <gmerlin/application.h>

#include <gmerlin/websocket.h>
#include <gmerlin/utils.h>

#define LOG_DOMAIN "gmerlin-server"


bg_controllable_t ctrl;


const bg_parameter_info_t * s_params;
bg_cfg_section_t * s_section = NULL;

bg_websocket_context_t * ctx = NULL;


static void opt_s(void * data, int * argc, char *** _argv, int arg)
  {
  if(arg >= *argc)
    {
    fprintf(stderr, "Option -s requires an argument\n");
    exit(-1);
    }

  if(!s_section)
    s_section =
      bg_cfg_section_create_from_parameters("server", s_params);
  
  if(!bg_cmdline_apply_options(s_section,
                               NULL,
                               NULL,
                               s_params,
                               (*_argv)[arg]))
    exit(-1);
  bg_cmdline_remove_arg(argc, _argv, arg);
  }

static bg_cmdline_arg_t global_options[] =
  {
    {
      .arg =         "-s",
      .help_arg =    "<options>",
      .help_string = "Server options",
      .callback =    opt_s,
    },
    { /* End */ }
  };

const bg_cmdline_app_data_t app_data =
  {
    .package =  PACKAGE,
    .version =  VERSION,
    .synopsis = TRS("[options]\n"),
    .help_before = TRS("Websocket tester\n"),
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

static int msg_callback(void * data, gavl_msg_t * msg)
  {
  fprintf(stderr, "Got message\n");
  gavl_msg_dump(msg, 2);

  /* Echo message */
  
  bg_msg_sink_put_copy(ctrl.evt_sink, msg);
  return 1;
  }

gavl_time_t delay_time = GAVL_TIME_SCALE / 20; // 50 ms

int main(int argc, char ** argv)
  {
  const char * root_url;
  bg_http_server_t * srv;

  bg_app_init("websocket_test", TRS("Websocket tester"), NULL);

  
  bg_handle_sigint();
  
  bg_cmdline_init(&app_data);
  bg_cmdline_parse(global_options, &argc, &argv, NULL);
  
  /* Create server */
  srv = bg_http_server_create();
  bg_http_server_set_static_path(srv, "/static/");

  bg_controllable_init(&ctrl,
                       bg_msg_sink_create(msg_callback, NULL, 1),
                       bg_msg_hub_create(1));
  
  ctx = bg_websocket_context_create(GAVL_META_CLASS_BACKEND_RENDERER, NULL, &ctrl);
  
  bg_http_server_start(srv);

  root_url = bg_http_server_get_root_url(srv);
  if(!strncmp(root_url, "http://0.0.0.0:", 15))
    {
    fprintf(stderr, "Go to http://127.0.0.1:%s/static/websocket.html\n",
            root_url + 15);
    }
  else
    fprintf(stderr, "Go to %s/static/websocket.html\n",
            root_url);
  
  while(1)
    {
    if(!bg_http_server_iteration(srv) &&
       !bg_websocket_context_iteration(ctx))
      {
      gavl_time_delay(&delay_time);
      }
    
    if(bg_got_sigint())
      break;
    }
  
  bg_http_server_destroy(srv);
  bg_websocket_context_destroy(ctx);
  
  return 0;
  }
