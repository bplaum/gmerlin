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


#define LOG_DOMAIN "gavf-recompress"

static gavl_dictionary_t ac_options;
static gavl_dictionary_t vc_options;
static gavl_dictionary_t oc_options;

static const bg_parameter_info_t * ac_parameters = NULL;
static const bg_parameter_info_t * vc_parameters = NULL;
static const bg_parameter_info_t * oc_parameters = NULL;

static bg_cfg_section_t ** ac_sections = NULL;
static bg_cfg_section_t ** vc_sections = NULL;
static bg_cfg_section_t ** oc_sections = NULL;

static int num_audio_streams = 0;
static int num_video_streams = 0;
static int num_overlay_streams = 0;


static int force_audio = 0;
static int force_video = 0;
static int force_overlay = 0;

static bg_cfg_section_t **
create_stream_sections(const bg_parameter_info_t * parameters,
                       int num, gavl_dictionary_t * options)
  {
  int i;
  const char * opt;
  bg_cfg_section_t ** ret;

  if(!num)
    return NULL;

  ret = calloc(num, sizeof(*ret));

  for(i = 0; i < num; i++)
    {
    opt = bg_cmdline_get_stream_options(options, i);
    if(opt)
      {
      ret[i] = bg_cfg_section_create_from_parameters("compression",
                                                     parameters);
      if(!bg_cfg_section_set_parameters_from_string(ret[i], parameters,
                                                    opt))
        {
        exit(-1);
        }
      }
    }
  return ret;
  }

static void destroy_stream_sections(bg_cfg_section_t ** sec, int num)
  {
  int i;
  if(!sec)
    return;
  for(i = 0; i < num; i++)
    {
    if(sec[i])
      bg_cfg_section_destroy(sec[i]);
    }
  free(sec);
  }

static void opt_ac(void * data, int * argc, char *** _argv, int arg)
  {
  if(arg >= *argc)
    {
    fprintf(stderr, "Option -ac requires an argument\n");
    exit(-1);
    }
  if(!bg_cmdline_set_stream_options(&ac_options,(*_argv)[arg]))
    exit(-1);
  bg_cmdline_remove_arg(argc, _argv, arg);
  }

static void opt_vc(void * data, int * argc, char *** _argv, int arg)
  {
  if(arg >= *argc)
    {
    fprintf(stderr, "Option -vc requires an argument\n");
    exit(-1);
    }
  if(!bg_cmdline_set_stream_options(&vc_options,(*_argv)[arg]))
    exit(-1);
  bg_cmdline_remove_arg(argc, _argv, arg);
  }

static void opt_oc(void * data, int * argc, char *** _argv, int arg)
  {
  if(arg >= *argc)
    {
    fprintf(stderr, "Option -oc requires an argument\n");
    exit(-1);
    }
  if(!bg_cmdline_set_stream_options(&oc_options,(*_argv)[arg]))
    exit(-1);
  bg_cmdline_remove_arg(argc, _argv, arg);
  }

static void opt_fa(void * data, int * argc, char *** _argv, int arg)
  {
  force_audio = 1;
  }

static void opt_fv(void * data, int * argc, char *** _argv, int arg)
  {
  force_video = 1;
  }

static void opt_fo(void * data, int * argc, char *** _argv, int arg)
  {
  force_overlay = 1;
  }

static bg_cmdline_arg_t global_options[] =
  {
    GAVFTOOLS_INPLUG_OPTIONS,
    GAVFTOOLS_OUTPLUG_OPTIONS,
    GAVFTOOLS_AUDIO_STREAM_OPTIONS,
    GAVFTOOLS_VIDEO_STREAM_OPTIONS,
    GAVFTOOLS_TEXT_STREAM_OPTIONS,
    GAVFTOOLS_OVERLAY_STREAM_OPTIONS,
    {
      .arg =         "-ac",
      .help_arg =    "<options>",
      .help_string = TRS("Audio compression options"),
      .callback =    opt_ac,
    },
    {
      .arg =         "-vc",
      .help_arg =    "<options>",
      .help_string = TRS("Video compression options"),
      .callback =    opt_vc,
    },
    {
      .arg =         "-oc",
      .help_arg =    "<options>",
      .help_string = TRS("Overlay compression options"),
      .callback =    opt_oc,
    },
    {
      .arg =         "-fa",
      .help_string = TRS("Force audio recompression even if the input stream has the desired compression already."),
      .callback =    opt_fa,
    },
    {
      .arg =         "-fv",
      .help_string = TRS("Force video recompression even if the input stream has the desired compression already."),
      .callback =    opt_fv,
    },
    {
      .arg =         "-fo",
      .help_string = TRS("Force overlay recompression even if the input stream has the desired compression already."),
      .callback =    opt_fo,
    },
    { /* End */ },
  };

const bg_cmdline_app_data_t app_data =
  {
    .package =  PACKAGE,
    .version =  VERSION,
    .synopsis = TRS("[options]\n"),
    .help_before = TRS("gavf recompressor\n"),
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

static bg_stream_action_t
get_stream_action(bg_stream_action_t action, bg_cfg_section_t * section,
                  int force, const gavl_compression_info_t * ci)
  {
  gavl_codec_id_t id;
  switch(action)
    {
    case BG_STREAM_ACTION_OFF:
    case BG_STREAM_ACTION_DECODE:
      return action;
      break;
    case BG_STREAM_ACTION_READRAW:
      if(!section)
        return BG_STREAM_ACTION_READRAW;
      id = bg_plugin_registry_get_compressor_id(bg_plugin_reg, section);
      if((id != ci->id) || force)
        return BG_STREAM_ACTION_DECODE;
      else
        return BG_STREAM_ACTION_READRAW;
      break;
    }
  return BG_STREAM_ACTION_OFF;
  }

int main(int argc, char ** argv)
  {
  int ret = EXIT_FAILURE;
  int i;
  int num_text_streams;
  int num;
  bg_mediaconnector_stream_t * mc;
  bg_media_source_t * src;
  gavl_compression_info_t ci;
  
  const gavl_dictionary_t * s;
  bg_stream_action_t action;
  
  bg_app_init("gavf-recompress", TRS("Recompress streams of a gavf stream"));
  
  gavl_dictionary_init(&ac_options);
  gavl_dictionary_init(&vc_options);
  gavl_dictionary_init(&oc_options);
  
  bg_mediaconnector_init(&gavftools_conn);
  gavftools_init();

  ac_parameters = bg_plugin_registry_get_audio_compressor_parameter();
  vc_parameters = bg_plugin_registry_get_video_compressor_parameter();
  oc_parameters = bg_plugin_registry_get_overlay_compressor_parameter();
  
  bg_cmdline_arg_set_parameters(global_options, "-ac", ac_parameters);
  bg_cmdline_arg_set_parameters(global_options, "-vc", vc_parameters);
  bg_cmdline_arg_set_parameters(global_options, "-oc", oc_parameters);

  /* Handle commandline options */
  bg_cmdline_init(&app_data);
  
  bg_cmdline_parse(global_options, &argc, &argv, NULL);
  
  if(!bg_cmdline_check_unsupported(argc, argv))
    goto fail;
  
  /* Open input plug */
  gavftools_in_plug = gavftools_create_in_plug();
  gavftools_out_plug = gavftools_create_out_plug();

  if(!gavftools_open_input(gavftools_in_plug, gavftools_in_file))
    goto fail;
 

  /* Check which streams we have */
  src = bg_plug_get_source(gavftools_in_plug);
  
  num_audio_streams = gavl_track_get_num_streams(src->track, GAVL_STREAM_AUDIO);
  num_video_streams = gavl_track_get_num_streams(src->track, GAVL_STREAM_VIDEO);
  num_overlay_streams = gavl_track_get_num_streams(src->track, GAVL_STREAM_OVERLAY);
  num_text_streams = gavl_track_get_num_streams(src->track, GAVL_STREAM_TEXT);
  
  ac_sections =
    create_stream_sections(ac_parameters,
                           num_audio_streams, &ac_options);
  vc_sections =
    create_stream_sections(vc_parameters,
                           num_video_streams, &vc_options);

  oc_sections =
    create_stream_sections(oc_parameters,
                           num_overlay_streams, &oc_options);

  
  
  for(i = 0; i < num_audio_streams; i++)
    {
    s = gavl_track_get_audio_stream(src->track, i);
    gavl_compression_info_init(&ci);
    gavl_stream_get_compression_info(s, &ci);
    action = gavftools_get_stream_action(GAVL_STREAM_AUDIO, i);
    
    bg_media_source_set_stream_action(src, GAVL_STREAM_AUDIO, i,
                                      get_stream_action(action,
                                                        ac_sections[i],
                                                        force_audio, &ci));
    gavl_compression_info_free(&ci);
    }
  for(i = 0; i < num_video_streams; i++)
    {
    s = gavl_track_get_audio_stream(src->track, i);
    gavl_compression_info_init(&ci);
    gavl_stream_get_compression_info(s, &ci);
    action = gavftools_get_stream_action(GAVL_STREAM_VIDEO, i);
    
    bg_media_source_set_stream_action(src, GAVL_STREAM_VIDEO, i,
                                      get_stream_action(action,
                                                        vc_sections[i],
                                                        force_video, &ci));

    gavl_compression_info_free(&ci);
    }
  for(i = 0; i < num_overlay_streams; i++)
    {
    s = gavl_track_get_audio_stream(src->track, i);
    gavl_compression_info_init(&ci);
    gavl_stream_get_compression_info(s, &ci);
    action = gavftools_get_stream_action(GAVL_STREAM_OVERLAY, i);

    bg_media_source_set_stream_action(src, GAVL_STREAM_OVERLAY, i,
                                      get_stream_action(action,
                                                        oc_sections[i],
                                                        force_overlay, &ci));
    gavl_compression_info_free(&ci);
    }
  for(i = 0; i < num_text_streams; i++)
    {
    action = gavftools_get_stream_action(GAVL_STREAM_TEXT, i);
    bg_media_source_set_stream_action(src, GAVL_STREAM_TEXT, i, action);
    }
  /* Start decoder and initialize media connector */

  if(!bg_plug_start(gavftools_in_plug) ||
     !bg_plug_setup_reader(gavftools_in_plug, &gavftools_conn))
    goto fail;

  bg_mediaconnector_create_conn(&gavftools_conn);

  /* Set encode sections in the media connector */

  num = bg_mediaconnector_get_num_streams(&gavftools_conn, GAVL_STREAM_AUDIO);
  for(i = 0; i < num; i++)
    {
    mc = bg_mediaconnector_get_stream(&gavftools_conn, GAVL_STREAM_AUDIO, i);
    if(mc->asrc)
      {
      gavl_dictionary_t * encode_section = bg_stream_get_cfg_encoder_nc(mc->s);
      gavl_dictionary_copy(encode_section, ac_sections[mc->src_index]);
      }
    }

  num = bg_mediaconnector_get_num_streams(&gavftools_conn, GAVL_STREAM_VIDEO);
  for(i = 0; i < num; i++)
    {
    mc = bg_mediaconnector_get_stream(&gavftools_conn, GAVL_STREAM_VIDEO, i);
    if(mc->vsrc)
      {
      gavl_dictionary_t * encode_section = bg_stream_get_cfg_encoder_nc(mc->s);
      gavl_dictionary_copy(encode_section, vc_sections[mc->src_index]);
      }
    
    }

  num = bg_mediaconnector_get_num_streams(&gavftools_conn, GAVL_STREAM_OVERLAY);
  for(i = 0; i < num; i++)
    {
    mc = bg_mediaconnector_get_stream(&gavftools_conn, GAVL_STREAM_OVERLAY, i);
    if(mc->vsrc)
      {
      gavl_dictionary_t * encode_section = bg_stream_get_cfg_encoder_nc(mc->s);
      gavl_dictionary_copy(encode_section, vc_sections[mc->src_index]);
      }
    }
  
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
  
  /* Cleanup */

  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Cleaning up");
  
  ret = EXIT_SUCCESS;
  fail:
  
  destroy_stream_sections(ac_sections, num_audio_streams);
  destroy_stream_sections(vc_sections, num_video_streams);
  destroy_stream_sections(oc_sections, num_overlay_streams);
  
  gavftools_cleanup();

  return ret;
  }
