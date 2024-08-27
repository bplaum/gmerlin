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



#include <stdio.h>
#include <string.h>

#include <config.h>

#include <gmerlin/utils.h>
#include <gmerlin/pluginregistry.h>

#include <gmerlin/visualize.h>
#include <gmerlin/translation.h>
#include <gmerlin/cfgctx.h>

#include <gmerlin/cmdline.h>
#include <gmerlin/application.h>

#include <gmerlin/log.h>
#define LOG_DOMAIN "visualization"

static bg_visualizer_t * visualizer = NULL;

static bg_cfg_section_t * vis_section = NULL;
static bg_parameter_info_t const * vis_parameters = NULL;


static void opt_visualizer(void * data, int * argc,
                           char *** _argv, int arg)
  {
  if(arg >= *argc)
    {
    fprintf(stderr, "Option -vis requires an argument\n");
    exit(-1);
    }
  if(!bg_cmdline_apply_options(vis_section,
                               bg_visualizer_set_parameter,
                               visualizer,
                               vis_parameters,
                               (*_argv)[arg]))
    exit(-1);
  bg_cmdline_remove_arg(argc, _argv, arg);
  }

static bg_cmdline_arg_t visualizer_options[] =
  {
    {
      .arg =         "-vis",
      .help_string = "options",
      .callback =    opt_visualizer,
    },
    { /* End of options */ }
    
  };

const bg_cmdline_app_data_t app_data =
  {
    .package =  PACKAGE,
    .version =  VERSION,
    .synopsis = TRS("[<options>]\n"),
    .help_before = TRS("Visualize test\n"),
    .args = (const bg_cmdline_arg_array_t[]) { { TRS("Options"), visualizer_options },
                                               {  } },
  };


int main(int argc, char ** argv)
  {
  const bg_plugin_info_t * info;
  gavl_dictionary_t m_global;
  gavl_dictionary_t m_stream;
  
  gavl_audio_format_t format;
  gavl_audio_frame_t * frame;
  
  bg_plugin_handle_t * input_handle;
  bg_recorder_plugin_t     * input_plugin;
  bg_plugin_handle_t * ov_handle;

  gavl_audio_source_t * src;
  
  /* Load config registry */
  bg_plugins_init();
  
  bg_app_init("visualization", TRS("Visualize test"));
    
  vis_section = bg_cfg_registry_find_section(bg_cfg_registry,
                                             "visualization");
  
  /* Create visualizer */
  
  visualizer = bg_visualizer_create();

  vis_parameters = bg_visualizer_get_parameters(visualizer);
  visualizer_options[0].parameters = vis_parameters;
  bg_cmdline_init(&app_data);
  bg_cmdline_parse(visualizer_options, &argc, &argv, NULL);
  
  /* Load input */

  info = bg_plugin_registry_get_default(bg_plugin_reg, BG_PLUGIN_RECORDER_AUDIO, BG_PLUGIN_RECORDER);
  if(!info)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "No input plugin defined");
    return -1;
    }

  input_handle = bg_plugin_load(bg_plugin_reg, info);
  if(!input_handle)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Input plugin could not be loaded");
    return -1;
    }
  
  input_plugin = (bg_recorder_plugin_t*)(input_handle->plugin);
  
  /* Load output */
  info = bg_plugin_registry_get_default(bg_plugin_reg, BG_PLUGIN_OUTPUT_VIDEO, BG_PLUGIN_PLAYBACK);
  if(!info)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "No output plugin defined");
    return -1;
    }

  ov_handle = bg_plugin_load(bg_plugin_reg, info);
  if(!ov_handle)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Output plugin could not be loaded");
    return -1;
    }
  
  /* Open input */

  memset(&format, 0, sizeof(format));
  gavl_dictionary_init(&m_global);
  gavl_dictionary_init(&m_stream);
  
  format.samplerate = 44100;
  format.sample_format = GAVL_SAMPLE_S16;
  format.samples_per_frame = 2048;
  format.interleave_mode = GAVL_INTERLEAVE_ALL;
  format.num_channels   = 2;
  gavl_set_channel_setup(&format);

  if(!input_plugin->open(input_handle->priv, &format, NULL, &m_global))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Opening input plugin failed");
    return -1;
    }

  src = input_plugin->get_audio_source(input_handle->priv);
  
  /* Start */
  
  bg_visualizer_open_plugin(visualizer, &format, &m_global, &m_stream, ov_handle);
  
  while(1)
    {
    frame = NULL;
    if(gavl_audio_source_read_frame(src, &frame) != GAVL_SOURCE_OK)
      break;
    bg_visualizer_update(visualizer, frame);
    }
  
  bg_visualizer_close(visualizer);
  return 0;
  }
