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

#include <string.h>
#include <stdlib.h>
#include <time.h>

#include <gmerlin/pluginregistry.h>
#include <gmerlin/utils.h>

#include <gmerlin/translation.h>
#include <gmerlin/application.h>

#include <gmerlin/cfgctx.h>
#include <gmerlin/cmdline.h>
#include <gmerlin/log.h>
#define LOG_DOMAIN "gmerlin_visualize"

/* Globals */
static int width = 640;
static int height = 480;
static int fps_num = 25;
static int fps_den = 1;

bg_cfg_section_t * vis_section = NULL;

bg_plugin_handle_t        * vis_handle = NULL;
bg_plugin_handle_t        * enc_handle = NULL;

bg_plugin_registry_t * plugin_reg = NULL;

// int encode_audio = 0;

int audio_index = -1;
int video_index = -1;

gavl_audio_converter_t * audio_cnv_v = NULL;
gavl_audio_converter_t * audio_cnv_e = NULL;
gavl_video_converter_t * video_cnv_e = NULL;
int convert_audio_v = 0;
int convert_audio_e = 0;
int convert_video_e = 0;

gavl_audio_frame_t * aframe_v = NULL;
gavl_audio_frame_t * aframe_e = NULL;

gavl_video_frame_t * vframe_v = NULL;
gavl_video_frame_t * vframe_e = NULL;

gavl_audio_format_t afmt_i, afmt_e, afmt_v;
gavl_video_format_t vfmt_v, vfmt_e;

gavl_audio_source_t * src_i;

gavl_time_t track_duration;
gavl_time_t start_duration = 0;
gavl_time_t end_duration = 0;

gavl_audio_sink_t * asink = NULL;
gavl_video_sink_t * vsink = NULL;

static int load_input_file(bg_plugin_registry_t * plugin_reg,
                           bg_plugin_handle_t ** input_handle,
                           bg_input_plugin_t ** input_plugin,
                           gavl_audio_source_t ** src,
                           const char * file)
  {
  gavl_dictionary_t * ti;
  *input_handle = NULL;
  if(!bg_input_plugin_load(plugin_reg,
                           file,
                           input_handle,
                           NULL))
    {
    fprintf(stderr, "Cannot open %s\n", file);
    return 0;
    }
  *input_plugin = (bg_input_plugin_t*)((*input_handle)->plugin);

  ti = bg_input_plugin_get_track_info(*input_handle, 0);

  bg_input_plugin_set_track(*input_handle, 0);

  
  if(!gavl_track_get_num_audio_streams(ti))
    {
    fprintf(stderr, "File %s has no audio\n", file);
    return 0;
    }
  
  track_duration = gavl_track_get_duration(ti);

  /* Select first stream */
  bg_media_source_set_audio_action((*input_handle)->src, 0, BG_STREAM_ACTION_DECODE);
  
  /* Start playback */
  bg_input_plugin_start((*input_handle));
  
  /* Get video format */

  *src = bg_media_source_get_audio_source((*input_handle)->src, 0);
  
  return 1;
  }

static void set_audio_parameter(void * priv, const char * name,
                                const gavl_value_t * val)
  {
  bg_encoder_plugin_t * plugin;
  bg_plugin_handle_t * handle = priv;
  plugin = (bg_encoder_plugin_t *)(handle)->plugin;
  
  plugin->set_audio_parameter(handle->priv, audio_index, name, val);
  }

static void set_video_parameter(void * priv, const char * name,
                                const gavl_value_t * val)
  {
  bg_encoder_plugin_t * plugin;
  bg_plugin_handle_t * handle = priv;
  plugin = (bg_encoder_plugin_t *)(handle)->plugin;
  plugin->set_video_parameter(handle->priv, video_index, name, val);
  //  fprintf(stderr, "Set video parameter %s\n", name);
  }

static int open_encoder(bg_plugin_registry_t * plugin_reg,
                        bg_plugin_handle_t ** handle,
                        bg_encoder_plugin_t ** plugin,
                        const char * file_prefix,
                        gavl_audio_format_t * audio_format,
                        gavl_video_format_t * video_format)
  {
  const bg_plugin_info_t * info;
  
  info = bg_plugin_registry_get_default(plugin_reg, BG_PLUGIN_ENCODER_VIDEO, BG_PLUGIN_FILE);
  if(!info)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "No encoder plugin present");
    return 0;
    }
  *handle = bg_plugin_load(plugin_reg, info);
  if(!(*handle))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Cannot load plugin %s", info->name);
    return 0;
    }
  *plugin = (bg_encoder_plugin_t *)(*handle)->plugin;
  
  /* Open */

  if(!(*plugin)->open((*handle)->priv, file_prefix, NULL))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Opening %s failed", file_prefix);
    }

  /* Add video stream */
  video_index = (*plugin)->add_video_stream((*handle)->priv, NULL, video_format);

  /* Add audio stream */
  if((*plugin)->add_audio_stream)
    audio_index = (*plugin)->add_audio_stream((*handle)->priv, NULL, audio_format);

  /* Set video parameters */

  if((*plugin)->get_video_parameters)
    {
    bg_cfg_section_t * s;
    s = bg_plugin_registry_get_section(plugin_reg, info->name);
    s = bg_cfg_section_find_subsection(s, "$video");

    bg_cfg_section_apply(s,
                         (*plugin)->get_video_parameters((*handle)->priv),
                         set_video_parameter,
                         *handle);
    }
  
  /* Set audio parameters */
  if((*plugin)->get_audio_parameters)
    {
    bg_cfg_section_t * s;
    s = bg_plugin_registry_get_section(plugin_reg, info->name);
    s = bg_cfg_section_find_subsection(s, "$audio");

    bg_cfg_section_apply(s,
                         (*plugin)->get_audio_parameters((*handle)->priv),
                         set_audio_parameter,
                         *handle);
    }

  if((*plugin)->start && !(*plugin)->start((*handle)->priv))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Starting %s failed", info->name);
    return 0;
    }
  
  if((*plugin)->add_audio_stream)
    asink = (*plugin)->get_audio_sink((*handle)->priv, audio_index);
  else
    asink = NULL;
  
  vsink = (*plugin)->get_video_sink((*handle)->priv, video_index);
  return 1;
  }

static void opt_width(void * data, int * argc, char *** argv, int arg)
  {
  if(arg >= *argc)
    {
    fprintf(stderr, "Option -w requires an argument\n");
    exit(-1);
    }
  width = atoi((*argv)[arg]);
  bg_cmdline_remove_arg(argc, argv, arg);
  }

static void opt_height(void * data, int * argc, char *** argv, int arg)
  {
  if(arg >= *argc)
    {
    fprintf(stderr, "Option -h requires an argument\n");
    exit(-1);
    }
  height = atoi((*argv)[arg]);
  bg_cmdline_remove_arg(argc, argv, arg);
  }

static void opt_fps(void * data, int * argc, char *** argv, int arg)
  {
  if(arg >= *argc)
    {
    fprintf(stderr, "Option -fps requires an argument\n");
    exit(-1);
    }

  if(sscanf((*argv)[arg], "%d:%d", &fps_num, &fps_den) < 2)
    {
    fprintf(stderr, "Framerate must be in the form num:den\n");
    exit(-1);
    }
  bg_cmdline_remove_arg(argc, argv, arg);
  }

static bg_parameter_info_t vis_parameters[] =
  {
    {
      .name =      "plugin",
      .long_name = "Visualization plugin",
      .opt =       "p",
      .type =      BG_PARAMETER_MULTI_MENU,
    },
    { /* End of parameters */ }
  };

static void set_vis_parameter(void * data, const char * name,
                             const gavl_value_t * val)
  {
  //  fprintf(stderr, "set_vis_parameter: %s\n", name);

  if(name && !strcmp(name, "plugin"))
    {
    //    fprintf(stderr, "set_vis_parameter: plugin: %s\n", val->v.str);
    
    vis_handle = bg_plugin_load_with_options(plugin_reg, bg_multi_menu_get_selected(val));
    }
  else
    {
    if(vis_handle && vis_handle->plugin && vis_handle->plugin->set_parameter)
      vis_handle->plugin->set_parameter(vis_handle->priv, name, val);
    }
  }

static void opt_vis(void * data, int * argc, char *** _argv, int arg)
  {
  if(arg >= *argc)
    {
    fprintf(stderr, "Option -vis requires an argument\n");
    exit(-1);
    }
  if(!bg_cmdline_apply_options(vis_section,
                               set_vis_parameter,
                               NULL,
                               vis_parameters,
                               (*_argv)[arg]))
    exit(-1);
  bg_cmdline_remove_arg(argc, _argv, arg);
  }


static bg_cmdline_arg_t global_options[] =
  {
    {
      .arg =         "-w",
      .help_arg =    "<width>",
      .help_string = "Image width",
      .callback =    opt_width,
    },
    {
      .arg =         "-h",
      .help_arg =    "<height>",
      .help_string = "Image height",
      .callback =    opt_height,
    },
    {
      .arg =         "-fps",
      .help_arg =    "<num:den>",
      .help_string = "Framerate",
      .callback =    opt_fps,
    },
    {
      .arg =         "-vis",
      .help_arg =    "<visualization_options>",
      .help_string = "Set visualization plugin",
      .callback =    opt_vis,
      .parameters =  vis_parameters,
    },
    { /* End of options */ }
  };

bg_cmdline_app_data_t app_data =
  {
    .package =  PACKAGE,
    .version =  VERSION,
    .synopsis = TRS("[-w width -h height -fps num:den] infile out_base\n"),
    .help_before = TRS("Visualize audio files and save as A/V files\n"),
    .args = (bg_cmdline_arg_array_t[]) { { TRS("Options"), global_options },
                                       {  } },
  };

int64_t next_frame_time = 0;
int64_t audio_time      = 0;

static void iteration(gavl_audio_frame_t * frame)
  {
  float percentage;
  bg_visualization_plugin_t * vis_plugin;
  vis_plugin = (bg_visualization_plugin_t*)vis_handle->plugin;
  
  /* Update visualization */
  if(convert_audio_v)
    {
    gavl_audio_convert(audio_cnv_v, frame, aframe_v);
    vis_plugin->update(vis_handle->priv, aframe_v);
    }
  else
    {
    vis_plugin->update(vis_handle->priv, frame);
    }

  audio_time += frame->valid_samples;
  
  /* Write audio */

  if(audio_index >= 0)
    {
    if(convert_audio_e)
      {
      gavl_audio_convert(audio_cnv_e, frame, aframe_e);
      gavl_audio_sink_put_frame(asink, aframe_e);
      }
    else
      {
      gavl_audio_sink_put_frame(asink, frame);
      }
    }
  
  /* Write video */

  if(gavl_time_rescale(afmt_i.samplerate,
                       vfmt_e.timescale, audio_time) >= next_frame_time)
    {
    vis_plugin->draw_frame(vis_handle->priv, vframe_v);
    
    if(convert_audio_e)
      {
      gavl_video_convert(video_cnv_e, vframe_v, vframe_e);
      vframe_e->timestamp = next_frame_time;
      vframe_e->duration = vfmt_e.frame_duration;
      gavl_video_sink_put_frame(vsink, vframe_e);
      }
    else
      {
      vframe_v->timestamp = next_frame_time;
      vframe_v->duration = vfmt_v.frame_duration;
      gavl_video_sink_put_frame(vsink, vframe_v);
      }
    next_frame_time += vfmt_e.frame_duration;
    }

  percentage = ((double)audio_time / (double)afmt_i.samplerate) /
    gavl_time_to_seconds(track_duration + start_duration + end_duration);

  printf("Done %6.2f %%\r", percentage * 100.0);
  fflush(stdout);
  }

int main(int argc, char ** argv)
  {

  bg_plugin_handle_t * input_handle;
  bg_input_plugin_t * input_plugin;

  bg_encoder_plugin_t * enc_plugin;
  
  bg_visualization_plugin_t * vis_plugin;
  
  char ** files;
  
  gavl_audio_frame_t * aframe_i = NULL;

  bg_app_init("gmerlin_visualize", "Gmerlin visualize");
    
  /* Initialize random generator */
  srand(time(NULL));
  
  /* Create registries */

  bg_plugins_init();

  /* Set the plugin parameter for the commandline */
  
  bg_plugin_registry_set_parameter_info(bg_plugin_reg,
                                        BG_PLUGIN_VISUALIZATION,
                                        BG_PLUGIN_VISUALIZE_FRAME,
                                        &vis_parameters[0]);
  
  vis_section = bg_cfg_section_create_from_parameters("vis", vis_parameters);

  
  /* Parse commandline */

  bg_cmdline_init(&app_data);
  bg_cmdline_parse(global_options, &argc, &argv, NULL);
  
  files = bg_cmdline_get_locations_from_args(&argc, &argv);
  
  if(!files || !files[0] || !files[1] || files[2])
    {
    bg_cmdline_print_help(argv[0], 0);
    return -1;
    }
  
  /* Open input */
  if(!load_input_file(plugin_reg,
                      &input_handle,
                      &input_plugin,
                      &src_i,
                      files[0]))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Cannot open %s", files[0]);
    return -1;
    }

  gavl_audio_format_copy(&afmt_i, gavl_audio_source_get_src_format(src_i));


  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Opened %s", files[0]);
  // gavl_audio_format_dump(&afmt_i);

  /* Open visualization */
  vis_plugin = (bg_visualization_plugin_t*)vis_handle->plugin;

  gavl_audio_format_copy(&afmt_v, &afmt_i);

  memset(&vfmt_v, 0, sizeof(vfmt_v));

  vfmt_v.image_width  = width;
  vfmt_v.image_height = height;
  vfmt_v.frame_width  = width;
  vfmt_v.frame_height = height;
  vfmt_v.pixel_width  = 1;
  vfmt_v.pixel_height = 1;
  vfmt_v.timescale      = fps_num;
  vfmt_v.frame_duration = fps_den;
  
  if(!vis_plugin->open_ov(vis_handle->priv,
                          &afmt_v, &vfmt_v));

  gavl_video_format_dump(&vfmt_v);

  /* Open encoder */

  gavl_audio_format_copy(&afmt_e, &afmt_i);
  gavl_video_format_copy(&vfmt_e, &vfmt_v);
  
  if(!open_encoder(plugin_reg,
                   &enc_handle,
                   &enc_plugin,
                   files[1],
                   &afmt_e,
                   &vfmt_e))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Cannot open encoder");
    return -1;
    }

  /* Initialize converters */

  audio_cnv_v = gavl_audio_converter_create();
  convert_audio_v = gavl_audio_converter_init(audio_cnv_v, &afmt_i, &afmt_v);
  
  if(audio_index >= 0)
    {
    audio_cnv_e = gavl_audio_converter_create();
    convert_audio_e = gavl_audio_converter_init(audio_cnv_e, &afmt_i, &afmt_e);
    }

  gavl_video_format_dump(&vfmt_v);
  gavl_video_format_dump(&vfmt_e);
  
  
  video_cnv_e = gavl_video_converter_create();
  convert_video_e = gavl_video_converter_init(video_cnv_e, &vfmt_v, &vfmt_e);

  /* Create frames */

  afmt_e.samples_per_frame = afmt_v.samples_per_frame;
  afmt_i.samples_per_frame = afmt_v.samples_per_frame;

  
  if(convert_audio_v)
    aframe_v = gavl_audio_frame_create(&afmt_v);

  if((audio_index >= 0) && (convert_audio_e))
    aframe_e = gavl_audio_frame_create(&afmt_e);

  vframe_v = gavl_video_frame_create(&vfmt_v);
  if(convert_video_e)
    vframe_e = gavl_video_frame_create(&vfmt_e);
  
  while(1)
    {
    aframe_i = NULL;
    if(gavl_audio_source_read_frame(src_i, &aframe_i) != GAVL_SOURCE_OK)
      break;
    
    iteration(aframe_i);
    }
  
  enc_plugin->close(enc_handle->priv, 0);
  
  return 0;
  }
