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

#include "gavftools.h"

static int do_demux = 0;

static void opt_demux(void * data, int * argc, char *** _argv, int arg)
  {
  do_demux = 1;
  }

static bg_cmdline_arg_t global_options[] =
  {
    GAVFTOOLS_INPLUG_OPTIONS,
    {
      .arg =         "-demux",
      .help_string = TRS("Demultiplex the whole track."),
      .callback =    opt_demux,
    },
    {
      /* End */
    }
  };

const bg_cmdline_app_data_t app_data =
  {
    .package =  PACKAGE,
    .version =  VERSION,
    .synopsis = TRS("[options]\n"),
    .help_before = TRS("gavf info\n"),
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

  bg_app_init("gavf-info", "Print gavf stream info");
    
  gavftools_init();

  bg_cmdline_init(&app_data);
  bg_cmdline_parse(global_options, &argc, &argv, NULL);

  if(!bg_cmdline_check_unsupported(argc, argv))
    goto fail;

  in_plug = gavftools_create_in_plug();

  if(!gavftools_open_input(in_plug, gavftools_in_file))
    goto fail;
  
  /* Do a full open */
  gavftools_set_stream_actions(bg_plug_get_source(in_plug));
  if(!bg_plug_start(in_plug))
    goto fail;

  src = bg_plug_get_source(in_plug);
  g = bg_plug_get_gavf(in_plug);
  
  /* Dump info */
  gavl_dictionary_dump(src->track, 0);
  ret = EXIT_SUCCESS;
  fail:

  if(ret == EXIT_SUCCESS)
    {
    if(do_demux)
      {
      while(gavf_demux_iteration(g) == GAVL_SOURCE_OK)
        gavf_clear_buffers(g);
      }
    }

    
  if(in_plug)
    bg_plug_destroy(in_plug);

  gavftools_cleanup();

  return ret;
  }
