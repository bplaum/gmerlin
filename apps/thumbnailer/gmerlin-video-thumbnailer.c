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

#include <sys/types.h> /* stat() */
#include <sys/stat.h>  /* stat() */
#include <unistd.h>    /* stat() */

#include <config.h>

#include <gmerlin/pluginregistry.h>
#include <gmerlin/utils.h>
#include <gmerlin/cfgctx.h>
#include <gmerlin/cmdline.h>
#include <gmerlin/translation.h>
#include <gmerlin/application.h>

#include <gmerlin/log.h>
#define LOG_DOMAIN "thumbnailer"

static int thumb_size = 128;

static gavl_time_t seek_time = GAVL_TIME_UNDEFINED;
static float seek_percentage = 10.0;

static void opt_size(void * data, int * argc, char *** _argv, int arg)
  {
  if(arg >= *argc)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Option -s requires an argument");
    exit(-1);
    }
  thumb_size = atoi((*_argv)[arg]);
  bg_cmdline_remove_arg(argc, _argv, arg);
  }

static void opt_time(void * data, int * argc, char *** _argv, int arg)
  {
  char * str;
  if(arg >= *argc)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Option -t requires an argument");
    exit(-1);
    }
  str = (*_argv)[arg];
  if(str[strlen(str)-1] == '%')
    seek_percentage = strtod(str, NULL);
  else
    seek_time       = gavl_seconds_to_time(strtod(str, NULL));
  
  bg_cmdline_remove_arg(argc, _argv, arg);
  }

static bg_cmdline_arg_t global_options[] =
  {
    {
      .arg =         "-s",
      .help_arg =    "<size>",
      .help_string = "Maximum width or height",
      .callback =    opt_size,
    },
    {
      .arg =         "-t",
      .help_arg =    "<time>",
      .help_string = "Time in seconds or percentage of duration (terminated with '%')",
      .callback =    opt_time,
    },
    { /* End */ }
  };

const bg_cmdline_app_data_t app_data =
  {
    .package =  PACKAGE,
    .version =  VERSION,
    .synopsis = TRS("[options] video_file thumbnail_file...\n"),
    .help_before = TRS("Video thumbnailer\n"),
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
  char ** files = NULL;
  char * in_file = NULL;
  char * out_file = NULL;
  
  bg_image_writer_plugin_t * output_plugin;
  
  gavl_dictionary_t metadata;
  
  gavl_dictionary_t * info;
  char * tmp_string;
  gavl_time_t duration;

  const bg_plugin_info_t * plugin_info;
  
  /* Plugin handles */
  
  bg_plugin_handle_t * input_handle = NULL;
  bg_plugin_handle_t * output_handle = NULL;

  /* Formats */

  gavl_video_format_t input_format;
  gavl_video_format_t output_format;
  
  /* Frames */
  
  gavl_video_frame_t * input_frame = NULL;
  gavl_video_frame_t * output_frame = NULL;

  /* Converter */
  
  gavl_video_converter_t * cnv;
  int do_convert, have_frame;
  struct stat st;
  gavl_value_t val;

  gavl_video_source_t * src;

  bg_app_init("gmerlin-video-thumbnailer", "Video thumbnailer", NULL);
  
  memset(&metadata, 0, sizeof(metadata));
  
  /* Get commandline options */
  bg_cmdline_init(&app_data);
  
  bg_cmdline_parse(global_options, &argc, &argv, NULL);
  files = bg_cmdline_get_locations_from_args(&argc, &argv);

  if(!files || !(files[0]) || !(files[1]) || files[2])
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "No input files given");
    return -1;
    }
  
  in_file = files[0];
  out_file = files[1];

  /* stat() */
  
  if(stat(in_file, &st))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Cannot stat %s", in_file);
    return -1;
    }
  
  /* Create registries */

  bg_plugins_init();
  
  /* Load input plugin */
  if(!(input_handle = bg_input_plugin_load(in_file)))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Cannot open %s", in_file);
    return -1;
    }
  
  if(!bg_input_plugin_get_num_tracks(input_handle))
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "%s has no tracks", in_file);

  info = bg_input_plugin_get_track_info(input_handle, 0);
  
  /* Copy metadata (extend them later) */
  gavl_dictionary_copy(&metadata, gavl_track_get_metadata(info));
  
  /* Select track */
  bg_input_plugin_set_track(input_handle, 0);
    
  if(!gavl_track_get_num_video_streams(info))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "File %s has no video", in_file);
    return -1;
    }

  /* Select first stream */
  
  bg_media_source_set_video_action(input_handle->src, 0, BG_STREAM_ACTION_DECODE);
  bg_input_plugin_start(input_handle);
  
  src = bg_media_source_get_video_source(input_handle->src, 0);
  
  duration = gavl_track_get_duration(info);
  
  /* Get video format */
  gavl_video_format_copy(&input_format, gavl_track_get_video_format(info, 0));

  /* Get the output format */
  
  gavl_video_format_copy(&output_format, &input_format);
  
  /* Scale the image to square pixels */
  
  output_format.image_width *= output_format.pixel_width;
  output_format.image_height *= output_format.pixel_height;
  
  if(output_format.image_width > input_format.image_height)
    {
    output_format.image_height = (thumb_size * output_format.image_height) /
      output_format.image_width;
    output_format.image_width = thumb_size;
    }
  else
    {
    output_format.image_width      = (thumb_size * output_format.image_width) /
      output_format.image_height;
    output_format.image_height = thumb_size;
    }
  
  output_format.pixel_width = 1;
  output_format.pixel_height = 1;
  output_format.interlace_mode = GAVL_INTERLACE_NONE;
  output_format.pixelformat = GAVL_RGBA_32;
  output_format.frame_width = output_format.image_width;
  output_format.frame_height = output_format.image_height;
  
  /* Load output plugin */
  
  plugin_info =
    bg_plugin_find_by_name("iw_png");
  
  if(!plugin_info)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "No plugin for %s", out_file);
    return -1;
    }
  
  output_handle = bg_plugin_load(plugin_info);

  if(!output_handle)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Loading %s failed", plugin_info->long_name);
    return -1;
    }
  
  output_plugin = (bg_image_writer_plugin_t*)output_handle->plugin;

  /* Don't force file extension */

  gavl_value_init(&val);
  gavl_value_set_int(&val, 1);
  output_plugin->common.set_parameter(output_handle->priv, "dont_force_extension", &val);
  
  /* Seek to the time */

  if(seek_time == GAVL_TIME_UNDEFINED)
    {
    if(duration == GAVL_TIME_UNDEFINED)
      {
      seek_time = 10 * GAVL_TIME_SCALE;
      }
    else
      {
      seek_time =
        (gavl_time_t)((seek_percentage / 100.0) * (double)(duration)+0.5);
      }
    }

  have_frame = 0;
  
  if(gavl_track_can_seek(info))
    {
    bg_input_plugin_seek(input_handle, seek_time, GAVL_TIME_SCALE);

    // input_plugin->seek(input_handle->priv, &seek_time, GAVL_TIME_SCALE);

    input_frame = NULL;
    if(gavl_video_source_read_frame(src, &input_frame) == GAVL_SOURCE_OK)
      have_frame = 1;
    else
      have_frame = 0;

    if(!have_frame) // Seeking failed, reset the stream and try without seeking
      {
      if(!bg_input_plugin_set_track(input_handle, 0))
        {
        gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Cannot reset stream after failed seek");
        return -1;
        }
      
      /* Select first stream */
      bg_media_source_set_video_action(input_handle->src, 0, BG_STREAM_ACTION_DECODE);
      
      /* Start playback */
      bg_input_plugin_start(input_handle);
      src = bg_media_source_get_video_source(input_handle->src, 0);
      }
    }

  if(!have_frame)
    {
    while(1)
      {
      input_frame = NULL;
      if(gavl_video_source_read_frame(src, &input_frame) == GAVL_SOURCE_OK)
        have_frame = 1;
      else
        have_frame = 0;

      if(!have_frame)
        break;
      
      if(gavl_time_unscale(input_format.timescale,
                           input_frame->timestamp) >= seek_time)
        break;
      }
    }
  
  if(!have_frame)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Couldn't read frame");
    return -1;
    }
  /* Extended metadata */

  tmp_string = bg_string_to_uri(in_file, -1);
  gavl_dictionary_set_string(&metadata, "Thumb::URI", tmp_string);
  free(tmp_string);

  tmp_string = gavl_sprintf("%"PRId64, (int64_t)st.st_mtime);
  gavl_dictionary_set_string(&metadata, "Thumb::MTime", tmp_string);
  free(tmp_string);

  gavl_dictionary_set_string(&metadata, "Software", "gmerlin-video-thumbnailer");
  
  tmp_string = gavl_sprintf("%"PRId64, (int64_t)st.st_size);
  gavl_dictionary_set_string(&metadata, "Thumb::Size", tmp_string);
  free(tmp_string);

  if(duration != GAVL_TIME_UNDEFINED)
    {
    tmp_string = gavl_sprintf("%d", (int)(gavl_time_to_seconds(duration)));
    gavl_dictionary_set_string(&metadata, "Thumb::Movie::Length", tmp_string);
    free(tmp_string);
    }
  
  /* Initialize image writer */
  
  if(!output_plugin->write_header(output_handle->priv,
                                  out_file, &output_format, &metadata))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Writing image header failed");
    return -1;
    }

  //  gavl_video_format_dump(&input_format);
  //  gavl_video_format_dump(&output_format);
  
  /* Initialize video converter */
  cnv = gavl_video_converter_create();
  do_convert = gavl_video_converter_init(cnv, &input_format, &output_format);
  
  if(do_convert)
    {
    output_frame = gavl_video_frame_create(&output_format);
    gavl_video_convert(cnv, input_frame, output_frame);
    if(!output_plugin->write_image(output_handle->priv,
                                   output_frame))
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Writing image failed");
      return -1;
      }
    gavl_video_frame_destroy(output_frame);
    }
  else
    {
    if(!output_plugin->write_image(output_handle->priv,
                                   input_frame))
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Writing image failed");
      return -1;
      }
    }

  bg_plugin_unref(input_handle);
  bg_plugin_unref(output_handle);

  gavl_video_converter_destroy(cnv);
  gavl_dictionary_free(&metadata);
  
  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Successfully saved %s", out_file);
  
  return 0;
  }
