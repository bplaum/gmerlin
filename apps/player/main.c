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

/* System includes */
#include <stdlib.h>
#include <time.h>

#include <gtk/gtk.h>

/* Gmerlin includes */

#include <config.h>

#include <gmerlin/player.h>

#include "gmerlin.h"
#include "player_remote.h"

#include <gmerlin/utils.h>
#include <gmerlin/cmdline.h>
#include <gui_gtk/gtkutils.h>

#include <gmerlin/translation.h>
#include <gmerlin/log.h>
#include <gmerlin/bggavl.h>
#include <gmerlin/upnp/upnputils.h>

#include <gmerlin/iconfont.h>
#include <gmerlin/application.h>

#define STATE_XML_NODE "PLAYERSTATE"
#define LOG_DOMAIN "main"

// #define MTRACE

#ifdef MTRACE
#include <mcheck.h>
#endif

static char * db_path = NULL;

static void opt_db(void * data, int * argc, char *** _argv, int arg)
  {
  if(arg >= *argc)
    {
    fprintf(stderr, "Option -db requires an argument\n");
    exit(-1);
    }

  db_path = (*_argv)[arg];
  bg_cmdline_remove_arg(argc, _argv, arg);

  fprintf(stderr, "Usinf db path from commandline: %s\n", db_path);
  }

static bg_cmdline_arg_t cmdline_args[] =
  {
    {
      .arg =         "-db",
      .help_string = "Use other database path",
      .callback =    opt_db,
    },
    BG_PLUGIN_OPT_OA,
    BG_PLUGIN_OPT_OV,
    BG_PLUGIN_OPT_FA,
    BG_PLUGIN_OPT_FV,
    { /* */ },
  };

const bg_cmdline_app_data_t app_data =
  {
    .package =  PACKAGE,
    .version =  VERSION,
    .synopsis = TRS("[options] [gmls ...]\n"),
    .help_before = TRS("Gmerlin GUI Player"),

    .args = (bg_cmdline_arg_array_t[]) { { TRS("Options"), cmdline_args },
                                       {  } },
    
#if 0
    .env = (bg_cmdline_ext_doc_t[])
    { { PLAYER_REMOTE_ENV,
        TRS("Default port for the remote control") },
      { /* End */ }
    },
#endif
    .files = (bg_cmdline_ext_doc_t[])
    { { "~/.gmerlin/plugins.xml",
        TRS("Cache of the plugin registry (shared by all applications)") },
      { "~/.gmerlin/player/cfg.xml",
        TRS("Used for configuration data. Delete this file if you think you goofed something up.") },
      { "~/.gmerlin/player/tree/tree.xml",
        TRS("Media tree is saved here. The albums are saved as separate files in the same directory.") },
      { /* End */ }
    },
  };

int main(int argc, char ** argv)
  {
  gmerlin_t * gmerlin;
  char * tmp_path;
  char ** locations;

  int have_state = 0;
  gavl_dictionary_t state;

  bg_app_init("gmerlin", TRS("Gmerlin Player"), "player");

  srand(time(NULL));
  
  gavl_dictionary_init(&state);
  
#ifdef MTRACE
  mtrace();
#endif

  bg_cmdline_init(&app_data);
  bg_cmdline_parse(cmdline_args, &argc, &argv, NULL);
  
  bg_iconfont_init();
  /* Initialize random generator (for shuffle) */

  srand(time(NULL));
  
  bg_translation_init();
  bg_gtk_init(&argc, &argv);

  bg_cfg_registry_init("player");
  bg_plugins_init();
  
  /* Load state if available */
  if((tmp_path = bg_search_file_read("player", "state.xml")))
    {
    if(bg_dictionary_load_xml(&state, tmp_path, STATE_XML_NODE))
      have_state = 1;
    free(tmp_path);
    }

  /* Restore plugin states */

  /* Fire up the actual player */
  if(!(gmerlin = gmerlin_create((have_state ? (&state) : NULL), db_path)))
    return EXIT_FAILURE;
  
  gavl_dictionary_free(&state);
  
  /* */

  
  /* Get locations from the commandline */

  
  locations = bg_cmdline_get_locations_from_args(&argc, &argv);
  
  gmerlin_run(gmerlin, (const char**)locations);

  /* Save plugin state */
  
  tmp_path =  bg_search_file_write("player", "state.xml");
  if(tmp_path)
    {
    bg_player_state_reset(&gmerlin->state);
    
    
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Saving state to %s", tmp_path);
    //    fprintf(stderr, "Saving state to %s\n", tmp_path);
    bg_dictionary_save_xml(&gmerlin->state, tmp_path, STATE_XML_NODE);
    }
  
  gmerlin_destroy(gmerlin);
  
  bg_cfg_registry_save();

  bg_global_cleanup();
  
  return EXIT_SUCCESS;
  }

