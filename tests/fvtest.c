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

#include <config.h>
#include <gmerlin/pluginregistry.h>
#include <gmerlin/utils.h>
#include <gmerlin/log.h>
#include <gmerlin/filters.h>
#include <gmerlin/cfgctx.h>
#include <gmerlin/cmdline.h>
#include <gmerlin/translation.h>
#include <gmerlin/application.h>

bg_video_filter_chain_t * fc;

const bg_parameter_info_t * fv_parameters;
gavl_dictionary_t * fv_section = NULL;
gavl_dictionary_t * opt_section = NULL;

bg_gavl_video_options_t opt;

int frameno = 0;
int dump_format = 0;

static void opt_frame(void * data, int * argc, char *** _argv, int arg)
  {
  if(arg >= *argc)
    {
    fprintf(stderr, "Option -frame requires an argument\n");
    exit(-1);
    }
  frameno = atoi((*_argv)[arg]);  
  bg_cmdline_remove_arg(argc, _argv, arg);
  }

static void opt_df(void * data, int * argc, char *** _argv, int arg)
  {
  dump_format = 1;
  }


static void opt_fv(void * data, int * argc, char *** _argv, int arg)
  {
  if(arg >= *argc)
    {
    fprintf(stderr, "Option -fv requires an argument\n");
    exit(-1);
    }
  if(!bg_cmdline_apply_options(fv_section,
                               bg_video_filter_chain_set_parameter,
                               fc,
                               fv_parameters,
                               (*_argv)[arg]))
    exit(-1);
  bg_cmdline_remove_arg(argc, _argv, arg);
  }

static bg_parameter_info_t opt_parameters[] =
  {
    BG_GAVL_PARAM_THREADS,
    { /* */ },
  };

static void opt_set_param(void * data, const char * name,
                   const gavl_value_t * val)
  {
  bg_gavl_video_set_parameter(data, name, val);
  }


static void opt_opt(void * data, int * argc, char *** _argv, int arg)
  {
  if(arg >= *argc)
    {
    fprintf(stderr, "Option -opt requires an argument\n");
    exit(-1);
    }
  if(!bg_cmdline_apply_options(opt_section,
                               opt_set_param,
                               &opt,
                               opt_parameters,
                               (*_argv)[arg]))
    exit(-1);
  bg_cmdline_remove_arg(argc, _argv, arg);
  }


static bg_cmdline_arg_t global_options[] =
  {
    {
      .arg =         "-fv",
      .help_arg =    "<filter options>",
      .help_string = "Set filter options",
      .callback =    opt_fv,
    },
    {
      .arg =         "-opt",
      .help_arg =    "<video options>",
      .help_string = "Set video options",
      .callback =    opt_opt,
      .parameters =  opt_parameters,
    },
    {
      .arg =         "-frame",
      .help_arg =    "<number>",
      .help_string = "Output that frame (default 0)",
      .callback =    opt_frame,
    },
    {
      .arg =         "-df",
      .help_string = "Dump format",
      .callback =    opt_df,
    },
    { /* End of options */ }
  };

static void update_global_options()
  {
  global_options[0].parameters = fv_parameters;
  }

const bg_cmdline_app_data_t app_data =
  {
    .package =  PACKAGE,
    .version =  VERSION,
    .synopsis = TRS("[options] infile outimage\n"),
    .help_before = TRS("Test tool for video filters\n"),
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
  int i;
  gavl_dictionary_t * info;

  
  /* Plugin handles */
  bg_plugin_handle_t * input_handle = NULL;

  /* Frames */
  gavl_video_frame_t * frame = NULL;
  gavl_video_format_t in_format;
  gavl_video_format_t out_format;
  
  /* Filter chain */
  /* Create registries */
  
  char ** gmls = NULL;
  
  gavl_video_source_t * src;

  gavl_timer_t * timer = gavl_timer_create();

  bg_app_init("fvtest", "Video filter chain test", NULL);

  bg_plugins_init("generic");
  
  /* Create filter chain */
  memset(&opt, 0, sizeof(opt));
  bg_gavl_video_options_init(&opt);
  fc = bg_video_filter_chain_create(&opt);
  fv_parameters = bg_video_filter_chain_get_parameters(fc);
  fv_section =
    bg_cfg_section_create_from_parameters("fv", fv_parameters);
  opt_section =
    bg_cfg_section_create_from_parameters("opt", opt_parameters);
  
  /* Get commandline options */
  bg_cmdline_init(&app_data);

  update_global_options();
  
  bg_cmdline_parse(global_options, &argc, &argv, NULL);
  gmls = bg_cmdline_get_locations_from_args(&argc, &argv);

  if(!gmls || !gmls[0])
    {
    fprintf(stderr, "No input file given\n");
    return 0;
    }
  if(!gmls[1])
    {
    fprintf(stderr, "No output file given\n");
    return 0;
    }
  if(gmls[2])
    {
    fprintf(stderr, "Unknown argument %s\n", gmls[2]);
    }
  
  /* Load input plugin */
  if(!(input_handle = bg_input_plugin_load(gmls[0])))
    {
    fprintf(stderr, "Cannot open %s\n", gmls[0]);
    return -1;
    }
  
  info = bg_input_plugin_get_track_info(input_handle, 0);
  
  /* Select track */
  bg_input_plugin_set_track(input_handle, 0);
  
  if(!gavl_track_get_num_video_streams(info))
    {
    fprintf(stderr, "File %s has no video\n", gmls[0]);
    return -1;
    }

  bg_media_source_set_video_action(input_handle->src, 0, BG_STREAM_ACTION_DECODE);
  bg_input_plugin_start(input_handle);

  gavl_video_format_copy(&in_format, gavl_track_get_video_format(info, 0));
  
  /* Initialize filter chain */

  src = bg_media_source_get_video_source(input_handle->src, 0);
  
  src = bg_video_filter_chain_connect(fc, src);
  
  
  if(frameno >= 0)
    {
    frame = NULL;
    gavl_timer_start(timer);
    for(i = 0; i < frameno+1; i++)
      {
      if(gavl_video_source_read_frame(src, &frame) != GAVL_SOURCE_OK)
        {
        fprintf(stderr, "Unexpected EOF\n");
        return -1;
        }
      }
    gavl_timer_stop(timer);
    }
  else
    {
    frame = NULL;
    gavl_timer_start(timer);
    while(1)
      {
      if(gavl_video_source_read_frame(src, &frame) != GAVL_SOURCE_OK)
        {
        break;
        }
      }
    gavl_timer_stop(timer);
    }

  fprintf(stderr, "Processing took %f seconds\n", gavl_time_to_seconds(gavl_timer_get(timer)));
  
  bg_plugin_registry_save_image(bg_plugin_reg, gmls[1], frame, &out_format, NULL);

  /* Destroy everything */
  bg_plugin_unref(input_handle);
  bg_video_filter_chain_destroy(fc);
  bg_gavl_video_options_free(&opt);
  bg_plugins_cleanup();
  bg_cfg_registry_cleanup();
  gavl_timer_destroy(timer);
  return 0;
  }
