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

#include <gmerlin/filters.h>
#include <gmerlin/bggavl.h>

#define LOG_DOMAIN "gavf-filter"

static gavl_dictionary_t af_options;
static gavl_dictionary_t vf_options;

static bg_parameter_info_t * af_parameters;
static bg_parameter_info_t * vf_parameters;


static void opt_af(void * data, int * argc, char *** _argv, int arg)
  {
  if(arg >= *argc)
    {
    fprintf(stderr, "Option -af requires an argument\n");
    exit(-1);
    }
  if(!bg_cmdline_set_stream_options(&af_options,(*_argv)[arg]))
    exit(-1);
  bg_cmdline_remove_arg(argc, _argv, arg);
  }

static void opt_vf(void * data, int * argc, char *** _argv, int arg)
  {
  if(arg >= *argc)
    {
    fprintf(stderr, "Option -vf requires an argument\n");
    exit(-1);
    }
  if(!bg_cmdline_set_stream_options(&vf_options,(*_argv)[arg]))
    exit(-1);
  bg_cmdline_remove_arg(argc, _argv, arg);
  }

static bg_cmdline_arg_t global_options[] =
  {
    GAVFTOOLS_INPLUG_OPTIONS,
    GAVFTOOLS_AQ_OPTIONS,
    GAVFTOOLS_VQ_OPTIONS,
    GAVFTOOLS_AUDIO_STREAM_OPTIONS,
    GAVFTOOLS_VIDEO_STREAM_OPTIONS,
    GAVFTOOLS_TEXT_STREAM_OPTIONS,
    GAVFTOOLS_OVERLAY_STREAM_OPTIONS,
    {
      .arg =         "-af",
      .help_arg =    "<options>",
      .help_string = "Audio filter options",
      .callback =    opt_af,
    },
    {
      .arg =         "-vf",
      .help_arg =    "<options>",
      .help_string = "Video filter options",
      .callback =    opt_vf,
    },
    GAVFTOOLS_OUTPLUG_OPTIONS,
    { /* End */ },
  };

const bg_cmdline_app_data_t app_data =
  {
    .package =  PACKAGE,
    .version =  VERSION,
    .synopsis = TRS("[options]\n"),
    .help_before = TRS("gavf filter\n"),
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

static bg_parameter_info_t * create_af_parameters()
  {
  bg_parameter_info_t * ret;
  bg_audio_filter_chain_t * ch =
    bg_audio_filter_chain_create(&gavltools_aopt);

  ret =
    bg_parameter_info_copy_array(bg_audio_filter_chain_get_parameters(ch));
  
  bg_audio_filter_chain_destroy(ch);
  return ret;
  }

static bg_parameter_info_t * create_vf_parameters()
  {
  bg_parameter_info_t * ret;
  bg_video_filter_chain_t * ch =
    bg_video_filter_chain_create(&gavltools_vopt);

  ret =
    bg_parameter_info_copy_array(bg_video_filter_chain_get_parameters(ch));
  
  bg_video_filter_chain_destroy(ch);
  return ret;
  }
  

int main(int argc, char ** argv)
  {
  int ret = EXIT_FAILURE;
  bg_stream_action_t action;

  const gavl_dictionary_t * sh;
  
  int num_audio_streams;
  int num_video_streams;
  int num, i;
  bg_media_source_t src;
  bg_media_source_t * in_src;
  
  bg_media_source_stream_t * src_stream;
  bg_media_source_stream_t * dst_stream;

  gavl_compression_info_t ci;

  bg_app_init("gavf-filter", TRS("Gavf A/V filter"));
    
  gavftools_init();
  
  gavftools_block_sigpipe();
  bg_mediaconnector_init(&gavftools_conn);

  bg_media_source_init(&src);
  

  /* Create dummy audio and video filter chain to obtain the parameters */
  af_parameters = create_af_parameters();
  vf_parameters = create_vf_parameters();

  bg_cmdline_arg_set_parameters(global_options, "-af",
                                af_parameters);
  bg_cmdline_arg_set_parameters(global_options, "-vf",
                                vf_parameters);
  
  gavl_dictionary_init(&af_options);
  gavl_dictionary_init(&vf_options);

  gavftools_set_cmdline_parameters(global_options);
  
  bg_cmdline_init(&app_data);
  bg_cmdline_parse(global_options, &argc, &argv, NULL);

  if(!bg_cmdline_check_unsupported(argc, argv))
    return -1;
  
  gavftools_in_plug = gavftools_create_in_plug();
  gavftools_out_plug = gavftools_create_out_plug();

  if(!gavftools_open_input(gavftools_in_plug, gavftools_in_file))
    goto fail;
  
  in_src = bg_plug_get_source(gavftools_in_plug);
  
  /* Get stream actions */

  num_audio_streams = gavl_track_get_num_streams(in_src->track, GAVL_STREAM_AUDIO);

 
  for(i = 0; i < num_audio_streams; i++)
    {
    sh = gavl_track_get_stream(in_src->track, i, GAVL_STREAM_AUDIO);

    gavl_compression_info_init(&ci);
    gavl_stream_get_compression_info(sh, &ci);
    action = gavftools_get_stream_action(GAVL_STREAM_AUDIO, i);
    
    if((ci.id != GAVL_CODEC_ID_NONE) &&
       (bg_cmdline_get_stream_options(&af_options, i)))
      {
      gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN,
             "Decompressing audio stream %d", i+1);
      bg_media_source_set_stream_action(in_src, GAVL_STREAM_AUDIO, i, BG_STREAM_ACTION_DECODE);
      }
    else
      {
      bg_media_source_set_stream_action(in_src, GAVL_STREAM_AUDIO, i, action);
      }
    gavl_compression_info_free(&ci);
    }
  
  num_video_streams = gavl_track_get_num_streams(in_src->track, GAVL_STREAM_VIDEO);

  for(i = 0; i < num_video_streams; i++)
    {
    sh = gavl_track_get_stream(in_src->track, i, GAVL_STREAM_VIDEO);
    gavl_compression_info_init(&ci);
    gavl_stream_get_compression_info(sh, &ci);
    action = gavftools_get_stream_action(GAVL_STREAM_VIDEO, i);

    if((ci.id != GAVL_CODEC_ID_NONE) &&
       (bg_cmdline_get_stream_options(&vf_options, i)))
      {
      gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN,
             "Decompressing video stream %d", i+1);
      bg_media_source_set_stream_action(in_src, GAVL_STREAM_VIDEO, i, BG_STREAM_ACTION_DECODE);
      }
    else
      bg_media_source_set_stream_action(in_src, GAVL_STREAM_VIDEO, i, action);

    gavl_compression_info_free(&ci);
    }

  num = gavl_track_get_num_streams(in_src->track, GAVL_STREAM_TEXT);
  

  for(i = 0; i < num; i++)
    {
    sh = gavl_track_get_stream(in_src->track, i, GAVL_STREAM_TEXT);
    action = gavftools_get_stream_action(GAVL_STREAM_TEXT, i);
    bg_media_source_set_stream_action(in_src, GAVL_STREAM_TEXT, i, action);
    }

  num = gavl_track_get_num_streams(in_src->track, GAVL_STREAM_OVERLAY);

  for(i = 0; i < num; i++)
    {
    sh = gavl_track_get_stream(in_src->track, i, GAVL_STREAM_OVERLAY);
    action = gavftools_get_stream_action(GAVL_STREAM_OVERLAY, i);
    bg_media_source_set_stream_action(in_src, GAVL_STREAM_OVERLAY, i, action);
    }
  
  
  /* Start input plug */

  if(!bg_plug_start(gavftools_in_plug))
    goto fail;
  
  bg_media_source_set_from_source(&src, in_src);
  
  /* Set up filters */

  for(i = 0; i < num_audio_streams; i++)
    {
    const char * filter_options;
    bg_cfg_section_t * sec;

    src_stream = bg_media_source_get_audio_stream(in_src, i);
    dst_stream = bg_media_source_get_audio_stream(&src, i);
    
    filter_options = bg_cmdline_get_stream_options(&af_options, i);
    if(!filter_options)
      continue;

    dst_stream->user_data = bg_audio_filter_chain_create(&gavltools_aopt);
    dst_stream->free_user_data = bg_audio_filter_chain_destroy;
    
    sec = bg_cfg_section_create_from_parameters("af", af_parameters);

    if(!bg_cmdline_apply_options(sec,
                                 bg_audio_filter_chain_set_parameter,
                                 dst_stream->user_data,
                                 af_parameters,
                                 filter_options))
      exit(-1);

    dst_stream->asrc = bg_audio_filter_chain_connect(dst_stream->user_data, src_stream->asrc);
    bg_cfg_section_destroy(sec);
    }

  for(i = 0; i < num_video_streams; i++)
    {
    const char * filter_options;
    bg_cfg_section_t * sec;

    filter_options = bg_cmdline_get_stream_options(&vf_options, i);
    if(!filter_options)
      continue;

    src_stream = bg_media_source_get_audio_stream(in_src, i);
    dst_stream = bg_media_source_get_audio_stream(&src, i);

    dst_stream->user_data = bg_video_filter_chain_create(&gavltools_vopt);
    dst_stream->free_user_data = bg_video_filter_chain_destroy;
    
    sec = bg_cfg_section_create_from_parameters("vf", vf_parameters);

    if(!bg_cmdline_apply_options(sec,
                                 bg_video_filter_chain_set_parameter,
                                 dst_stream->user_data,
                                 vf_parameters,
                                 filter_options))
      exit(-1);
    
    dst_stream->vsrc = bg_video_filter_chain_connect(dst_stream->user_data,
                                                     src_stream->vsrc);
    
    bg_cfg_section_destroy(sec);
    }

  bg_mediaconnector_set_from_source(&gavftools_conn, &src);
  bg_mediaconnector_create_conn(&gavftools_conn);
    
  /* Set up out plug */

  /* Open output plug */

  if(!gavftools_open_out_plug_from_in_plug(gavftools_out_plug, NULL, gavftools_in_plug))
    goto fail;

  
  /* Fire up connector */

  bg_mediaconnector_start(&gavftools_conn);
  
  /* Run */

  while(1)
    {

    if(gavftools_stop() ||
       !bg_mediaconnector_iteration(&gavftools_conn))
      break;
    }


  ret = EXIT_SUCCESS;
  
  fail:
  
  /* Cleanup */

  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Cleaning up");
  
  gavftools_cleanup();

  bg_media_source_cleanup(&src);
  
  return ret;
  }
