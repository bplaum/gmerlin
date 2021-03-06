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

#include <string.h>
#include <signal.h>

#include "gavftools.h"

#include <gavl/metatags.h>

#include <language_table.h>

#define LOG_DOMAIN "gavf-record"



/* Global stuff */


static bg_plug_t * out_plug = NULL;

static bg_cfg_section_t * audio_section = NULL;
static bg_cfg_section_t * video_section = NULL;

/* Recorder module */

static const bg_parameter_info_t audio_parameters[] =
  {
    {
      .name      = "plugin",
      .long_name = TRS("Plugin"),
      .type      = BG_PARAMETER_MULTI_MENU,
      .flags     = BG_PARAMETER_PLUGIN,
    },
    {
      .name      = "language",
      .long_name = TRS("Language"),
      .type      = BG_PARAMETER_STRINGLIST,
      .val_default = GAVL_VALUE_INIT_STRING("eng"),
      .multi_names = bg_language_codes,
      .multi_labels = bg_language_labels,
    },
    { /* End */ },
  };

static const bg_parameter_info_t video_parameters[] =
  {
    {
      .name      = "plugin",
      .long_name = TRS("Plugin"),
      .type      = BG_PARAMETER_MULTI_MENU,
      .flags     = BG_PARAMETER_PLUGIN,
    },
    { /* End */ },
  };

typedef struct
  {
  bg_plugin_handle_t * h;
  bg_recorder_plugin_t * plugin;
  bg_parameter_info_t * parameters;

  gavl_dictionary_t stream;
  gavl_dictionary_t * m;
  } recorder_stream_t;

typedef struct
  {
  recorder_stream_t as;
  recorder_stream_t vs;
  } recorder_t;

static recorder_t rec;

static void recorder_stream_init(recorder_stream_t * s,
                                 const bg_parameter_info_t * parameters,
                                 const char * name, bg_cfg_section_t ** section,
                                 uint32_t type_mask)
  {
  memset(s, 0, sizeof(*s));
  s->parameters = bg_parameter_info_copy_array(parameters);
  
  bg_plugin_registry_set_parameter_info(bg_plugin_reg,
                                        type_mask,
                                        0,
                                        &s->parameters[0]);
  if(!(*section))
    *section = bg_cfg_section_create_from_parameters(name, s->parameters);
  }

static void recorder_stream_cleanup(recorder_stream_t * s)
  {
  if(s->parameters)
    bg_parameter_info_destroy_array(s->parameters);
  gavl_dictionary_free(&s->stream);
  if(s->h)
    bg_plugin_unref(s->h);
  }

static void recorder_init(recorder_t * rec)
  {
  recorder_stream_init(&rec->as,
                       audio_parameters,
                       "audio", &audio_section,
                       BG_PLUGIN_RECORDER_AUDIO);
  recorder_stream_init(&rec->vs,
                       video_parameters,
                       "video", &video_section,
                       BG_PLUGIN_RECORDER_VIDEO);
  }

static void recorder_cleanup(recorder_t * rec)
  {
  recorder_stream_cleanup(&rec->as);
  recorder_stream_cleanup(&rec->vs);
  }

static void recorder_stream_set_parameter(void * sp, const char * name,
                                          const gavl_value_t * val)
  {
  recorder_stream_t * s= sp;

  // fprintf(stderr, "recorder_stream_set_parameter %s\n", name);
  
  if(!name)
    {
    if(s->h && s->plugin->common.set_parameter)
      s->plugin->common.set_parameter(s->h->priv, NULL, NULL);
    return;
    }
  else if(!strcmp(name, "language"))
    gavl_dictionary_set_string(s->m, GAVL_META_LANGUAGE, val->v.str);
  else if(!strcmp(name, "plugin"))
    {
    if(s->h)
      bg_plugin_unref(s->h);
    
    s->h = bg_plugin_load_with_options(bg_plugin_reg, bg_multi_menu_get_selected(val));
    s->plugin = (bg_recorder_plugin_t*)(s->h->plugin);
    // if(s->input_plugin->set_callbacks)
    // s->input_plugin->set_callbacks(s->h->priv, &rec->recorder_cb);
    }
  
  }

static int recorder_stream_open(recorder_stream_t * s, int type,
                                bg_mediaconnector_t * conn)
  {
  //  gavl_compression_info_t ci;
  gavl_audio_format_t afmt;
  gavl_video_format_t vfmt;
  
  if(!s->h)
    {
    gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Not recording %s: No plugin set",
           (type == GAVL_STREAM_AUDIO ? "audio" : "video"));
    return 0;
    }
  //  memset(&ci, 0, sizeof(ci));
  memset(&afmt, 0, sizeof(afmt));
  memset(&vfmt, 0, sizeof(vfmt));

  s->m = gavl_dictionary_get_dictionary_create(&s->stream, GAVL_META_METADATA);
  
  if(!s->plugin->open(s->h->priv, &afmt, &vfmt, s->m))
    {
    gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Opening %s recorder failed",
           (type == GAVL_STREAM_AUDIO ? "audio" : "video"));
    return 0;
    }

  bg_metadata_date_now(s->m, GAVL_META_DATE_CREATE);
  
  if(type == GAVL_STREAM_AUDIO)
    {
    gavl_audio_source_t * asrc;
    asrc = s->plugin->get_audio_source(s->h->priv);
    bg_mediaconnector_append_audio_stream(conn, &s->stream, asrc, NULL);
    }
  else if(type == GAVL_STREAM_VIDEO)
    {
    gavl_video_source_t * vsrc;
    vsrc = s->plugin->get_video_source(s->h->priv);
    bg_mediaconnector_append_video_stream(conn, &s->stream, vsrc, NULL);
    }
  return 1;
  }

static int recorder_open(recorder_t * rec, bg_mediaconnector_t * conn)
  {
  int do_audio;
  int do_video;

  do_audio = recorder_stream_open(&rec->as, GAVL_STREAM_AUDIO, conn);
  do_video = recorder_stream_open(&rec->vs, GAVL_STREAM_VIDEO, conn);
  
  bg_mediaconnector_create_conn(conn);
  
  if(!do_audio && !do_video)
    return 0;
  return 1;
  }

/* Config stuff */

static void opt_aud(void * data, int * argc, char *** _argv, int arg)
  {
  if(arg >= *argc)
    {
    fprintf(stderr, "Option -aud requires an argument\n");
    exit(-1);
    }
  if(!bg_cmdline_apply_options(audio_section,
                               recorder_stream_set_parameter,
                               &rec.as,
                               rec.as.parameters,
                               (*_argv)[arg]))
    exit(-1);
  bg_cmdline_remove_arg(argc, _argv, arg);
  }

static void opt_vid(void * data, int * argc, char *** _argv, int arg)
  {
  if(arg >= *argc)
    {
    fprintf(stderr, "Option -vid requires an argument\n");
    exit(-1);
    }
  if(!bg_cmdline_apply_options(video_section,
                               recorder_stream_set_parameter,
                               &rec.vs,
                               rec.vs.parameters,
                               (*_argv)[arg]))
    exit(-1);
  bg_cmdline_remove_arg(argc, _argv, arg);
  }

static bg_cmdline_arg_t global_options[] =
  {
    {
      .arg =         "-aud",
      .help_arg =    "<audio_options>",
      .help_string = "Set audio recording options",
      .callback =    opt_aud,
    },
    {
      .arg =         "-vid",
      .help_arg =    "<video_options>",
      .help_string = "Set video recording options",
      .callback =    opt_vid,
    },
    GAVFTOOLS_OUTPLUG_OPTIONS,
    {
      /* End */
    }
  };

const bg_cmdline_app_data_t app_data =
  {
    .package =  PACKAGE,
    .version =  VERSION,
    .synopsis = TRS("[options]\n"),
    .help_before = TRS("gavf recorder\n"),
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
  bg_mediaconnector_t conn;
  gavl_time_t delay_time = GAVL_TIME_SCALE / 100;
  gavl_dictionary_t m;
  gavftools_block_sigpipe();
  bg_mediaconnector_init(&conn);

  bg_app_init("gavf-record", TRS("gavf recorder"));
  
  gavftools_init();
  
  /* Create plugin regsitry */
  
  /* Initialize streams */
  recorder_init(&rec);

  bg_cmdline_arg_set_parameters(global_options, "-aud", rec.as.parameters);
  bg_cmdline_arg_set_parameters(global_options, "-vid", rec.vs.parameters);

  gavftools_set_cmdline_parameters(global_options);
  
    
  /* Handle commandline options */
  bg_cmdline_init(&app_data);
  bg_cmdline_parse(global_options, &argc, &argv, NULL);

  out_plug = gavftools_create_out_plug();
    
  /* Open plugins */
  if(!recorder_open(&rec, &conn))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Opening recorder plugins failed");
    goto the_end;
    }

  gavl_dictionary_init(&m);
  gavftools_set_output_metadata(&m);

  /* Open output plug */
  if(!bg_plug_open_location(out_plug, gavftools_out_file))
    goto the_end;
  
  gavl_dictionary_free(&m);

  /* Initialize output plug */
  if(!bg_plug_setup_writer(out_plug, &conn))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Setting up plug writer failed");
    goto the_end;
    }
  
  /* Initialize threads */
  bg_mediaconnector_start(&conn);
  bg_mediaconnector_create_threads(&conn, 0);
  bg_mediaconnector_threads_init_separate(&conn);

  bg_mediaconnector_threads_start(&conn);
  
  /* Main loop */
  while(1)
    {
    if(gavftools_stop() ||
       bg_mediaconnector_done(&conn))
      break;
    gavl_time_delay(&delay_time);
    }

  bg_mediaconnector_threads_stop(&conn);
  
  /* Cleanup */
  
  ret = EXIT_SUCCESS;
  the_end:
  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Cleaning up");

  bg_mediaconnector_free(&conn);

  if(out_plug)
    bg_plug_destroy(out_plug);

  recorder_cleanup(&rec);
  
  gavftools_cleanup();

  if(audio_section)
    bg_cfg_section_destroy(audio_section);
  if(video_section)
    bg_cfg_section_destroy(video_section);
  
  return ret;
  }
