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

#include <locale.h>

#include <config.h>

#include "server.h"

#include <gmerlin/cfgctx.h>
#include <gmerlin/cmdline.h>

#include <gmerlin/translation.h>
#include <gmerlin/application.h>

#include <gmerlin/utils.h>
#include <gmerlin/upnp/upnputils.h>

#include <signal.h>

static bg_cmdline_arg_t global_options[] =
  {
   BG_OPT_LOAD_CONFIG,
   BG_OPT_SAVE_CONFIG, 
   { /* End */ }
  };

const bg_cmdline_app_data_t app_data =
  {
    .package =  PACKAGE,
    .version =  VERSION,
    .synopsis = TRS("[options]\n"),
    .help_before = TRS("Gmerlin Mediaserver\n"),
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
  int result;
  int ret = EXIT_FAILURE;
  gavl_time_t delay_time = GAVL_TIME_SCALE / 50; // 20 ms
  server_t s;

  bg_app_init("gmerlin-server", TRS("Gmerlin media server"), "server");
  
  /* Make strcasecmp work */
  setlocale(LC_COLLATE, "");
  
  bg_handle_sigint();
  signal(SIGPIPE, SIG_IGN);

  bg_cfg_registry = gavl_dictionary_create();
  bg_cfg_registry_find_section(bg_cfg_registry, "server"); // Ensure that the server section comes first in the configfile
  
  bg_plugins_init();
  
  bg_cmdline_init(&app_data);
  bg_cmdline_parse(global_options, &argc, &argv, NULL);
  
  result = server_init(&s);

  bg_cfg_registry_save_config();
  
  
  if(!result)
    goto fail;
  
  while(1)
    {
    if(!server_iteration(&s))
      {
      if(bg_got_sigint())
        break;
      
      gavl_time_delay(&delay_time);
      }
    }
  ret = EXIT_SUCCESS;

  fail:
  
  /* TODO: Save state */
  
  server_cleanup(&s);

  bg_global_cleanup();

  return ret;
  }
