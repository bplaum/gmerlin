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



#include <config.h>

#include "renderer.h"

#include <gmerlin/cfgctx.h>
#include <gmerlin/cmdline.h>
#include <gavl/log.h>
#define LOG_DOMAIN "gmerlin-renderer"

#include <gmerlin/translation.h>
#include <gmerlin/utils.h>
#include <gmerlin/upnp/upnputils.h>
#include <gmerlin/iconfont.h>
#include <gmerlin/application.h>

#include <signal.h>

static char * save_config = NULL;
static gavl_array_t fe_arr;

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


static bg_cmdline_arg_t global_options[] =
  {
    {
      .arg = "-sc",
      .help_arg = "<file>",
      .help_string = TRS("Save config to file"),
      .argv = &save_config,
    },
    BG_OPT_LOAD_CONFIG,
    {
      .arg =         "-i",
      .help_arg =    "<options>",
      .help_string = TRS("Input options"),
    },
    {
      .arg =         "-aud",
      .help_arg =    "<options>",
      .help_string = TRS("Audio options"),
    },
    {
      .arg =         "-vid",
      .help_arg =    "<options>",
      .help_string = TRS("Video options"),
    },
    BG_PLUGIN_OPT_OA,
    BG_PLUGIN_OPT_OV,
    BG_PLUGIN_OPT_FA,
    BG_PLUGIN_OPT_FV,
    {
      .arg =         "-vis",
      .help_arg =    "<options>",
      .help_string = TRS("Audio visualization"),
    },
    {
      .arg =         "-osd",
      .help_arg =    "<options>",
      .help_string = TRS("OSD Options"),
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
    { /* End */ }
  };


const bg_cmdline_app_data_t app_data =
  {
    .package =  PACKAGE,
    .version =  VERSION,
    .synopsis = TRS("[options]\n"),
    .help_before = TRS("Gmerlin Mediarenderer\n"),
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
  int ret = EXIT_FAILURE;
  gavl_time_t delay_time = GAVL_TIME_SCALE / 50; // 20 ms
  renderer_t s;

  gavl_array_init(&fe_arr);
  bg_frontend_set_option(&fe_arr, "renderer_gmerlin,renderer_upnp,mpris");
  
  bg_app_init("gmerlin-renderer", TRS("Gmerlin renderer"), "renderer");
  
  bg_handle_sigint();
  bg_iconfont_init();
  
  signal(SIGPIPE, SIG_IGN);

  /* Create global config registry */
  bg_cfg_registry = gavl_dictionary_create();
  bg_plugins_init();

  /* Pre-initialize the frontends */
  
  bg_cmdline_init(&app_data);
  bg_cmdline_parse(global_options, &argc, &argv, NULL);

  renderer_init(&s, &fe_arr);
    
  if(save_config)
    {
    gavl_dictionary_t tmp;
    gavl_dictionary_init(&tmp);
    gavl_dictionary_copy(&tmp, bg_cfg_registry);
    bg_cfg_section_delete_subsection_by_name(&tmp, "plugins");
    bg_cfg_registry_save_to(&tmp, save_config);
    gavl_dictionary_free(&tmp);
    }
  while(1)
    {
    if(!renderer_iteration(&s))
      {
      if(bg_got_sigint())
        break;
      
      gavl_time_delay(&delay_time);
      
      }
    }

  /* TODO: Save state */
  
  renderer_cleanup(&s);
  
  bg_global_cleanup();

  return ret;
  }
