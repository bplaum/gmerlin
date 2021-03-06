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
#include <gavl/metatags.h>

#define LOG_DOMAIN "gavf-demux"

static bg_plug_t ** out_plugs = NULL;

static int num_out_plugs = 0;

static char ** outfiles_a = NULL;
static char ** outfiles_v = NULL;
static char ** outfiles_t = NULL;
static char ** outfiles_o = NULL;

static int num_outfiles_a = 0;
static int num_outfiles_v = 0;
static int num_outfiles_t = 0;
static int num_outfiles_o = 0;

static int files_added_a = 0;
static int files_added_v = 0;
static int files_added_t = 0;
static int files_added_o = 0;

static char * prefix = NULL;

static void
opt_oa(void * data, int * argc, char *** _argv, int arg)
  {
  if(arg >= *argc)
    {
    fprintf(stderr, "Option -oa requires an argument\n");
    exit(-1);
    }
  
  outfiles_a = realloc(outfiles_a, sizeof(*outfiles_a) * (num_outfiles_a+1));
  outfiles_a[num_outfiles_a] = (*_argv)[arg];
  num_outfiles_a++;
  bg_cmdline_remove_arg(argc, _argv, arg);
  }

static void
opt_ov(void * data, int * argc, char *** _argv, int arg)
  {
  if(arg >= *argc)
    {
    fprintf(stderr, "Option -ov requires an argument\n");
    exit(-1);
    }
  
  outfiles_v = realloc(outfiles_v, sizeof(*outfiles_v) * (num_outfiles_v+1));
  outfiles_v[num_outfiles_v] = (*_argv)[arg];
  num_outfiles_v++;
  bg_cmdline_remove_arg(argc, _argv, arg);
  }

static void
opt_ot(void * data, int * argc, char *** _argv, int arg)
  {
  if(arg >= *argc)
    {
    fprintf(stderr, "Option -ot requires an argument\n");
    exit(-1);
    }
  
  outfiles_t = realloc(outfiles_t, sizeof(*outfiles_t) * (num_outfiles_t+1));
  outfiles_t[num_outfiles_t] = (*_argv)[arg];
  num_outfiles_t++;
  bg_cmdline_remove_arg(argc, _argv, arg);
  }

static void
opt_oo(void * data, int * argc, char *** _argv, int arg)
  {
  if(arg >= *argc)
    {
    fprintf(stderr, "Option -oo requires an argument\n");
    exit(-1);
    }
  
  outfiles_o = realloc(outfiles_o, sizeof(*outfiles_o) * (num_outfiles_o+1));
  outfiles_o[num_outfiles_o] = (*_argv)[arg];
  num_outfiles_o++;
  bg_cmdline_remove_arg(argc, _argv, arg);
  }

static bg_cmdline_arg_t global_options[] =
  {
    GAVFTOOLS_INPLUG_OPTIONS,
    GAVFTOOLS_AUDIO_STREAM_OPTIONS,
    GAVFTOOLS_VIDEO_STREAM_OPTIONS,
    GAVFTOOLS_TEXT_STREAM_OPTIONS,
    GAVFTOOLS_OVERLAY_STREAM_OPTIONS,
    {
      .arg =         "-oa",
      .help_arg =    "<output>",
      .help_string = TRS("Output file or location for audio. Use this option multiple times to add more outputs."),
      .callback    = &opt_oa,
    },
    {
      .arg =         "-ov",
      .help_arg =    "<output>",
      .help_string = TRS("Output file or location for video. Use this option multiple times to add more outputs."),
      .callback    = &opt_ov,
    },
    {
      .arg =         "-ot",
      .help_arg =    "<output>",
      .help_string = TRS("Output file or location for text. Use this option multiple times to add more outputs."),
      .callback    = &opt_ot,
    },
    {
      .arg =         "-oo",
      .help_arg =    "<output>",
      .help_string = TRS("Output file or location for overlays. Use this option multiple times to add more outputs."),
      .callback    = &opt_oo,
    },
    {
      .arg =         "-pre",
      .help_arg =    "<prefix>",
      .help_string = TRS("Output file prefix"),
      .argv    = &prefix,
    },
    GAVFTOOLS_OOPT_OPTIONS,
    { /* End */ },
  };

const bg_cmdline_app_data_t app_data =
  {
    .package =  PACKAGE,
    .version =  VERSION,
    .synopsis = TRS("[options]\n"),
    .help_before = TRS("gavf demuxer\n"),
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


static char * get_out_name(bg_mediaconnector_stream_t * st)
  {
  const gavl_dictionary_t * m;
  int * num_outfiles;
  int * files_added = NULL;
  char ** outfiles = NULL;
  char * ret = NULL;
  const char * label;
  char * opt = NULL;
  
  m = bg_plug_get_metadata(gavftools_in_plug);
  
  switch(st->type)
    {
    case GAVL_STREAM_AUDIO:
      num_outfiles = &num_outfiles_a;
      files_added = &files_added_a;
      outfiles = outfiles_a;
      opt = "-oa";
      break;
    case GAVL_STREAM_VIDEO:
      num_outfiles = &num_outfiles_v;
      files_added = &files_added_v;
      outfiles = outfiles_v;
      opt = "-ov";
      break;
    case GAVL_STREAM_TEXT:
      num_outfiles = &num_outfiles_t;
      files_added = &files_added_t;
      outfiles = outfiles_t;
      opt = "-ot";
      break;
    case GAVL_STREAM_OVERLAY:
      num_outfiles = &num_outfiles_o;
      files_added = &files_added_o;
      outfiles = outfiles_o;
      opt = "-oo";
      break;
    case GAVL_STREAM_NONE:
    case GAVL_STREAM_MSG:
      break;
    }
  if(*num_outfiles > *files_added)
    {
    ret = gavl_strdup(outfiles[*files_added]);
    (*files_added)++;
    }
  else if(prefix)
    {
    ret = bg_sprintf("%s_%s_%03d.gavf", prefix,
                     gavf_stream_type_name(st->type), st->src_index + 1);
    }
  else if((label = gavl_dictionary_get_string(m, GAVL_META_LABEL)))
    {
    ret = bg_sprintf("%s_%s_%03d.gavf", label,
                     gavf_stream_type_name(st->type),
                     st->src_index + 1);
    }
  else
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN,
           "No output filename given, use -pre or %s",
           opt);
    }
  return ret;
  }


int main(int argc, char ** argv)
  {
  int ret = EXIT_FAILURE;
  int i;
  bg_mediaconnector_stream_t * st;

  bg_app_init("gavf-demux", TRS("Gavf Demultiplexer"));
  
  gavftools_init();
  
  gavftools_block_sigpipe();

  gavftools_set_cmdline_parameters(global_options);

  bg_cmdline_init(&app_data);
  bg_cmdline_parse(global_options, &argc, &argv, NULL);

  if(!bg_cmdline_check_unsupported(argc, argv))
    return -1;

  gavftools_in_plug = gavftools_create_in_plug();
  
  if(!gavftools_open_input(gavftools_in_plug, gavftools_in_file))
    goto fail;
  
  gavftools_set_stream_actions(bg_plug_get_source(gavftools_in_plug));

  if(!bg_plug_start(gavftools_in_plug))
    goto fail;
  
  if(!bg_plug_setup_reader(gavftools_in_plug, &gavftools_conn))
    goto fail;

  bg_mediaconnector_create_conn(&gavftools_conn);

  /* Create out plugs */
  num_out_plugs = gavftools_conn.num_streams;
  out_plugs = calloc(num_out_plugs, sizeof(*out_plugs));
  
  for(i = 0; i < gavftools_conn.num_streams; i++)
    {
    char * filename;
    //    const gavf_stream_header_t * h;
    bg_media_sink_stream_t * ms;
    bg_media_sink_t * sink;
    
    st = gavftools_conn.streams[i];

    if(!(filename = get_out_name(st)))
      goto fail;
    
    out_plugs[i] = gavftools_create_out_plug();
    
    if(!gavftools_open_out_plug_from_in_plug(out_plugs[i], filename,
                                             gavftools_in_plug) ||
       !bg_plug_add_mediaconnector_stream(out_plugs[i], st) ||
       !bg_plug_start(out_plugs[i]) ||
       !(sink = bg_plug_get_sink(out_plugs[i])) ||
       !(ms = bg_media_sink_get_stream(sink, 0, st->type)) ||
       !bg_plug_connect_mediaconnector_stream(st, ms))
      goto fail;

    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Opened %s", filename);
    
    free(filename);
    }

  /* Fire up connector */
  bg_mediaconnector_start(&gavftools_conn);
  
  /* Main loop */
  
  while(1)
    {

    if(gavftools_stop() ||
       !bg_mediaconnector_iteration(&gavftools_conn))
      break;
    }
  
  ret = EXIT_SUCCESS;
  fail:

  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Cleaning up");

  for(i = 0; i < num_out_plugs; i++)
    {
    if(out_plugs[i])
      bg_plug_destroy(out_plugs[i]);
    }

  if(outfiles_a)
    free(outfiles_a);
  if(outfiles_v)
    free(outfiles_v);
  if(outfiles_t)
    free(outfiles_t);
  if(outfiles_o)
    free(outfiles_o);
  if(out_plugs)
    free(out_plugs);
  
  gavftools_cleanup();
  

  
  return ret;

  }
