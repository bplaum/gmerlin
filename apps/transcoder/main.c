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

#include <config.h>
#include <time.h>
#include <stdlib.h>

#include <gtk/gtk.h>
#include <gui_gtk/gtkutils.h>

#include <gmerlin/pluginregistry.h>
#include <gmerlin/cfgctx.h>
#include <gmerlin/cmdline.h>
#include <gmerlin/iconfont.h>
#include <gmerlin/application.h>

#include "transcoder_window.h"

static void opt_p(void * data, int * argc, char *** argv, int arg)
  {
  FILE * out = stderr;
  transcoder_window_t * win;
  win = (transcoder_window_t*)data;
  
  if(arg >= *argc)
    {
    fprintf(out, "Option -p requires an argument\n");
    exit(-1);
    }

  transcoder_window_load_profile(win, (*argv)[arg]);
  bg_cmdline_remove_arg(argc, argv, arg);
  }


bg_cmdline_arg_t args[] =
  {
    {
      .arg = "-p",
      .help_arg = "<file>",
      .help_string = "Load a profile from the given file",
      .callback =    opt_p,
    },
    { /* End of args */ }
  };

const bg_cmdline_app_data_t app_data =
  {
    .package =  PACKAGE,
    .version =  VERSION,
    .synopsis = TRS("[options]\n"),
    .help_before = TRS("GTK multimedia transcoder\n"),
    .args = (bg_cmdline_arg_array_t[]) { { TRS("Options"), args },
                                       {  } },
    .env = (bg_cmdline_ext_doc_t[])
    { { /* End */ } },
  };


int main(int argc, char ** argv)
  {
  transcoder_window_t * win;

  bg_app_init("gmerlin_transcoder", TRS("Gmerlin transcoder"), "transcoder_icon");
  
  bg_iconfont_init();

  bg_cfg_registry_init("transcoder");
  bg_plugins_init();
  

  /* We must initialize the random number generator if we want the
     Vorbis encoder to work */
  srand(time(NULL));
    

  bg_cmdline_init(&app_data);

  bg_gtk_init(&argc, &argv);
  win = transcoder_window_create();
  bg_cmdline_parse(args, &argc, &argv, win);
  
  transcoder_window_run(win);

  transcoder_window_destroy(win);

  bg_plugins_cleanup();
  bg_cfg_registry_cleanup();
  
  return 0;
  }
