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

/* Standalone visualizer */

#include <gavl/gavl.h>

#include <config.h>

#include <gmerlin/translation.h>
#include <gmerlin/visualize.h>
#include <gmerlin/pluginregistry.h>
// #include <gmerlin/cmdline.h>

#include "gavftools.h"

static bg_cmdline_arg_t global_options[] =
  {
    GAVFTOOLS_INPLUG_OPTIONS,
    {
      /* End */
    }
  };


const bg_cmdline_app_data_t app_data =
  {
    .package =  PACKAGE,
    .version =  VERSION,
    .synopsis = TRS("[options]\n"),
    .help_before = TRS("gavf music visualizer\n"),
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
  bg_plug_t * in_plug = NULL;
  bg_media_source_t * src;
  gavf_t * g;

  bg_app_init("gavf-visualize", "Run a music visualization");
  
  gavftools_init();

  bg_cmdline_init(&app_data);
  bg_cmdline_parse(global_options, &argc, &argv, NULL);

  
  
  }
