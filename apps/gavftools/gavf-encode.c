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

#include <gmerlin/encoder.h>
#include <gavl/metatags.h>

#define LOG_DOMAIN "gavf-encode"

static gavl_dictionary_t ac_options;
static gavl_dictionary_t vc_options;
static gavl_dictionary_t oc_options;
static gavl_dictionary_t tc_options;

const uint32_t stream_mask =
GAVL_STREAM_AUDIO | GAVL_STREAM_VIDEO | GAVL_STREAM_TEXT | GAVL_STREAM_OVERLAY;

const uint32_t plugin_mask = BG_PLUGIN_FILE | BG_PLUGIN_BROADCAST;

static bg_parameter_info_t * enc_params = NULL;
static bg_parameter_info_t * enc_params_simple = NULL;

static bg_cfg_section_t * enc_section = NULL;

static const char * out_file = NULL;

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

static void opt_tc(void * data, int * argc, char *** _argv, int arg)
  {
  if(arg >= *argc)
    {
    fprintf(stderr, "Option -tc requires an argument\n");
    exit(-1);
    }
  if(!bg_cmdline_set_stream_options(&oc_options,(*_argv)[arg]))
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

static void opt_enc(void * data, int * argc, char *** _argv, int arg)
  {
  if(arg >= *argc)
    {
    fprintf(stderr, "Option -enc requires an argument\n");
    exit(-1);
    }

  if(!bg_cmdline_apply_options(enc_section,
                               NULL,
                               NULL,
                               enc_params_simple,
                               (*_argv)[arg]))
    exit(-1);
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
      .arg =         "-enc",
      .help_arg =    "<options>",
      .help_string = "Encoding options",
      .callback =    opt_enc,
    },
    {
      .arg =         "-ac",
      .help_arg =    "<options>",
      .help_string = "Audio compression options",
      .callback =    opt_ac,
    },
    {
      .arg =         "-vc",
      .help_arg =    "<options>",
      .help_string = "Video compression options",
      .callback =    opt_vc,
    },
    {
      .arg =         "-tc",
      .help_arg =    "<options>",
      .help_string = "Text  compression options",
      .callback =    opt_tc,
    },
    {
      .arg =         "-oc",
      .help_arg =    "<options>",
      .help_string = "Overlay compression options",
      .callback =    opt_oc,
    },
    {
      .arg =         "-o",
      .help_arg =    "<output>",
      .help_string = TRS("Output file (without extension) or location "),
      .argv    =    (char**)&out_file,
    },

    { /* End */ },
  };

const bg_cmdline_app_data_t app_data =
  {
    .package =  PACKAGE,
    .version =  VERSION,
    .synopsis = TRS("[options]\n"),
    .help_before = TRS("gavf encoder\n"),
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

static bg_cfg_section_t * get_stream_section(bg_encoder_t * enc,
                                             gavl_stream_type_t t, int in_index)
  {
  bg_cfg_section_t * ret;
  const bg_cfg_section_t * sec;

  const char * options_string;
  gavl_dictionary_t * options;
  const bg_parameter_info_t * params;
  
  switch(t)
    {
    case GAVL_STREAM_AUDIO:
      options = &ac_options;
      break;
    case GAVL_STREAM_VIDEO:
      options = &vc_options;
      break;
    case GAVL_STREAM_TEXT:
      options = &tc_options;
      break;
    case GAVL_STREAM_OVERLAY:
      options = &oc_options;
      break;
    case GAVL_STREAM_MSG:
    case GAVL_STREAM_NONE:
      break;
    }
  params = bg_encoder_get_stream_parameters(enc, t);
  options_string = bg_cmdline_get_stream_options(options, in_index);

  sec = bg_encoder_get_stream_section(enc, t);
  if(!sec)
    return NULL;
  
  ret = bg_cfg_section_copy(bg_encoder_get_stream_section(enc, t));
  
  if(options_string)
    {
    if(!bg_cmdline_apply_options(ret, NULL, NULL,
                                 params, options_string))
      exit(-1);
    }
  return ret;
  }

int main(int argc, char ** argv)
  {
  int do_delete;
  int ret = EXIT_FAILURE;
  int i, num;
  bg_encoder_t * enc = NULL;
  bg_mediaconnector_stream_t * cs;

  gavl_audio_sink_t * asink;
  gavl_video_sink_t * vsink;
  gavl_packet_sink_t * psink;
  
  bg_stream_action_t action;
  
  bg_media_source_t * src;
  const gavl_dictionary_t * m; 
  const gavl_dictionary_t * sh; 
  gavl_compression_info_t ci;
  const gavl_audio_format_t * afmt;
  const gavl_video_format_t * vfmt;

  bg_app_init("gavf-encode", TRS("Gavf stream encoder"));

  
  
  gavl_dictionary_init(&ac_options);
  gavl_dictionary_init(&vc_options);
  gavl_dictionary_init(&oc_options);
  gavl_dictionary_init(&tc_options);
  
  gavftools_init();

  /* Create encoder parameters */
  enc_params =
    bg_plugin_registry_create_encoder_parameters(bg_plugin_reg,
                                                 stream_mask,
                                                 plugin_mask, 1);
  enc_params_simple =
    bg_plugin_registry_create_encoder_parameters(bg_plugin_reg,
                                                 stream_mask,
                                                 plugin_mask, 0);

  enc_section = bg_cfg_section_create_from_parameters("encoders",
                                                      enc_params);
  
  // bg_parameters_dump(enc_params, "enc_params.xml");
  // bg_parameters_dump(enc_params_simple, "enc_params_simple.xml");

  bg_cmdline_arg_set_parameters(global_options, "-enc",
                                enc_params_simple);

  gavftools_set_cmdline_parameters(global_options);
  
  bg_cmdline_init(&app_data);
  bg_cmdline_parse(global_options, &argc, &argv, NULL);

  gavftools_in_plug = gavftools_create_in_plug();
  
  enc = bg_encoder_create(enc_section, // bg_cfg_section_t * section,
                          NULL, // bg_transcoder_track_t * tt,
                          stream_mask,
                          plugin_mask);
  
  /* Open */

  if(!gavftools_open_input(gavftools_in_plug, gavftools_in_file))
    goto fail;
  
  src = bg_plug_get_source(gavftools_in_plug);

  m = gavl_track_get_metadata(src->track);
  
  if(!out_file && m)
    out_file = gavl_dictionary_get_string(m, GAVL_META_LABEL);
#if 0
  if(!out_file)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "No output file given");
    goto fail;
    }
#endif
  
  if(!bg_encoder_open(enc, out_file, m))
    goto fail;
  
  /* Set stream actions */
  
  num = gavl_track_get_num_streams(src->track, GAVL_STREAM_AUDIO);
  
  for(i = 0; i < num; i++)
    {
    gavl_compression_info_init(&ci);
    sh = gavl_track_get_stream(src->track, i, GAVL_STREAM_AUDIO);

    afmt = gavl_stream_get_audio_format(sh);
    gavl_stream_get_compression_info(sh, &ci);

    action = gavftools_get_stream_action(GAVL_STREAM_AUDIO, i);
    
    /* Check if we can write the stream compressed */
    if((ci.id != GAVL_CODEC_ID_NONE) &&
       (action == BG_STREAM_ACTION_READRAW) && 
        !bg_encoder_writes_compressed_audio(enc, afmt, &ci))
      {
      gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Audio stream %d cannot be written compressed", i+1);
      action = BG_STREAM_ACTION_DECODE;
      }
    bg_media_source_set_stream_action(src, GAVL_STREAM_AUDIO, i, action);
    gavl_compression_info_free(&ci);
    }

  num = gavl_track_get_num_streams(src->track, GAVL_STREAM_VIDEO);
  
  for(i = 0; i < num; i++)
    {
    gavl_compression_info_init(&ci);
    sh = gavl_track_get_stream(src->track, i, GAVL_STREAM_VIDEO);

    vfmt = gavl_stream_get_video_format(sh);
    gavl_stream_get_compression_info(sh, &ci);
    action = gavftools_get_stream_action(GAVL_STREAM_VIDEO, i);
    
    /* Check if we can write the stream compressed */
    if((ci.id != GAVL_CODEC_ID_NONE) &&
       (action == BG_STREAM_ACTION_READRAW) &&
        !bg_encoder_writes_compressed_video(enc, vfmt, &ci))
      {
      gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Video stream %d cannot be written compressed", i+1);
      action = BG_STREAM_ACTION_DECODE;
      }      
    bg_media_source_set_stream_action(src, GAVL_STREAM_VIDEO, i, action);
    gavl_compression_info_free(&ci);
    }

  num = gavl_track_get_num_streams(src->track, GAVL_STREAM_TEXT);
  
  for(i = 0; i < num; i++)
    {
    action = gavftools_get_stream_action(GAVL_STREAM_TEXT, i);
    bg_media_source_set_stream_action(src, GAVL_STREAM_TEXT, i, action);
    }

  num = gavl_track_get_num_streams(src->track, GAVL_STREAM_OVERLAY);

  for(i = 0; i < num; i++)
    {
    gavl_compression_info_init(&ci);

    sh = gavl_track_get_stream(src->track, i, GAVL_STREAM_OVERLAY);

    vfmt = gavl_stream_get_video_format(sh);
    gavl_stream_get_compression_info(sh, &ci);
    action = gavftools_get_stream_action(GAVL_STREAM_OVERLAY, i);

    /* Check if we can write the stream compressed */
    if((ci.id != GAVL_CODEC_ID_NONE) &&
       (action == BG_STREAM_ACTION_READRAW) &&
        !bg_encoder_writes_compressed_overlay(enc, vfmt, &ci))
      {
      gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Overlay stream %d cannot be written compressed", i+1);
      action = BG_STREAM_ACTION_DECODE;
      }
    bg_media_source_set_stream_action(src, GAVL_STREAM_OVERLAY, i, action);
    gavl_compression_info_free(&ci);
    }
  
  /* Start decoder and initialize media connector */

  if(!bg_plug_start(gavftools_in_plug) ||
     !bg_plug_setup_reader(gavftools_in_plug, &gavftools_conn))
    return 0;

  bg_mediaconnector_create_conn(&gavftools_conn);
  
  /* Set up encoder */

  for(i = 0; i < gavftools_conn.num_streams; i++)
    {
    cs = gavftools_conn.streams[i];
    switch(cs->type)
      {
      case GAVL_STREAM_AUDIO:
        if(cs->asrc)
          {
          bg_cfg_section_t * s = get_stream_section(enc, GAVL_STREAM_AUDIO, cs->src_index);
          cs->dst_index =
            bg_encoder_add_audio_stream(enc, cs->m,
                                        gavl_audio_source_get_src_format(cs->asrc),
                                        cs->src_index, s);
          if(s)
            bg_cfg_section_destroy(s);
          }
        else
          {
          gavl_compression_info_t ci;
          const gavl_dictionary_t * si = gavl_packet_source_get_stream(cs->psrc);

          gavl_compression_info_init(&ci);
          gavl_stream_get_compression_info(si, &ci);
          
          cs->dst_index =
            bg_encoder_add_audio_stream_compressed(enc, cs->m,
                                                   gavl_packet_source_get_audio_format(cs->psrc),
                                                   &ci,
                                                   cs->src_index);
          gavl_compression_info_free(&ci);
          }
        break;
      case GAVL_STREAM_VIDEO:
        if(cs->vsrc)
          {
          bg_cfg_section_t * s = get_stream_section(enc, GAVL_STREAM_VIDEO, cs->src_index);
          cs->dst_index =
            bg_encoder_add_video_stream(enc, cs->m,
                                        gavl_video_source_get_src_format(cs->vsrc),
                                        cs->src_index, s);
          if(s)
            bg_cfg_section_destroy(s);
          }
        else
          {
          gavl_compression_info_t ci;
          const gavl_dictionary_t * si = gavl_packet_source_get_stream(cs->psrc);

          gavl_compression_info_init(&ci);
          gavl_stream_get_compression_info(si, &ci);
          
          cs->dst_index =
            bg_encoder_add_video_stream_compressed(enc, cs->m,
                                                   gavl_packet_source_get_video_format(cs->psrc),
                                                   &ci,
                                                   cs->src_index);
          
          gavl_compression_info_free(&ci);
          }
        break;
      case GAVL_STREAM_TEXT:
        {
        bg_cfg_section_t * s = get_stream_section(enc, GAVL_STREAM_TEXT, cs->src_index);
        cs->dst_index = bg_encoder_add_text_stream(enc, cs->m,
                                                   cs->timescale, cs->src_index, s);
        if(s)
          bg_cfg_section_destroy(s);
        }
        break;
      case GAVL_STREAM_OVERLAY:
        if(cs->vsrc)
          {
          bg_cfg_section_t * s = get_stream_section(enc, GAVL_STREAM_OVERLAY, cs->src_index);
          bg_encoder_add_overlay_stream(enc,
                                        cs->m,
                                        gavl_video_source_get_src_format(cs->vsrc),
                                        cs->src_index,
                                        GAVL_STREAM_OVERLAY,
                                        s);
          if(s)
            bg_cfg_section_destroy(s);
          }
        else
          {
          gavl_compression_info_t ci;
          const gavl_dictionary_t * si = gavl_packet_source_get_stream(cs->psrc);

          gavl_compression_info_init(&ci);
          gavl_stream_get_compression_info(si, &ci);
          

          bg_encoder_add_overlay_stream_compressed(enc,
                                                   cs->m,
                                                   gavl_packet_source_get_video_format(cs->psrc),
                                                   &ci,
                                                   cs->src_index);
          gavl_compression_info_free(&ci);
          }
        break;
      case GAVL_STREAM_NONE:
      case GAVL_STREAM_MSG:
        break;
      }
    }

  /* Start */
  if(!bg_encoder_start(enc))
    goto fail;

  /* Connect sinks */

  for(i = 0; i < gavftools_conn.num_streams; i++)
    {
    cs = gavftools_conn.streams[i];

    asink = NULL;
    vsink = NULL;
    psink = NULL;
    
    switch(cs->type)
      {
      case GAVL_STREAM_AUDIO:
        if(cs->aconn)
          asink = bg_encoder_get_audio_sink(enc, cs->dst_index);
        else
          psink = bg_encoder_get_audio_packet_sink(enc, cs->dst_index);
        break;
      case GAVL_STREAM_VIDEO:
        if(cs->vconn)
          vsink = bg_encoder_get_video_sink(enc, cs->dst_index);
        else
          psink = bg_encoder_get_video_packet_sink(enc, cs->dst_index);
        break;
      case GAVL_STREAM_TEXT:
        psink = bg_encoder_get_text_sink(enc, cs->dst_index);
        break;
      case GAVL_STREAM_OVERLAY:
        if(cs->vconn)
          vsink = bg_encoder_get_overlay_sink(enc, cs->dst_index);
        else
          psink = bg_encoder_get_overlay_packet_sink(enc, cs->dst_index);
        break;
      case GAVL_STREAM_NONE:
      case GAVL_STREAM_MSG:
        break;
      }

    if(asink)
      gavl_audio_connector_connect(cs->aconn, asink);
    else if(vsink)
      gavl_video_connector_connect(cs->vconn, vsink);
    else if(psink)
      gavl_packet_connector_connect(cs->pconn, psink);
    }

  /* Fire up connector */

  bg_mediaconnector_start(&gavftools_conn);
  
  /* Main loop */

  do_delete = 0;
  
  while(1)
    {
    if(gavftools_stop() ||
       !bg_mediaconnector_iteration(&gavftools_conn))
      break;
    }
  
  ret = EXIT_SUCCESS;
  
  /* Cleanup */

  fail:
  
  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Cleaning up");
  
  if(enc)
    bg_encoder_destroy(enc, do_delete);
  
  gavl_dictionary_free(&ac_options);
  gavl_dictionary_free(&vc_options);
  gavl_dictionary_free(&oc_options);

  
  gavftools_cleanup();

  if(enc_params)
    bg_parameter_info_destroy_array(enc_params);
  if(enc_params_simple)
    bg_parameter_info_destroy_array(enc_params_simple);

  if(enc_section)
    bg_cfg_section_destroy(enc_section);
  
  return ret;
  }
