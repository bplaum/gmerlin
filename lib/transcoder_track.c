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



#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <config.h>
#include <gmerlin/translation.h>

#include <gmerlin/pluginregistry.h>
#include <gmerlin/transcoder_track.h>
#include <gmerlin/utils.h>

#include <gmerlin/bggavl.h>

#include <gmerlin/textrenderer.h>

#include <gmerlin/filters.h>


#include <gmerlin/log.h>
#define LOG_DOMAIN "transcoder_track"

#include <gavl/metatags.h>

void bg_transcoder_track_set_encoders(bg_transcoder_track_t * t,
                                      const gavl_dictionary_t * encoder_section)
  {
  int i;
  int num;
  int num_text_streams;

  const gavl_value_t * val;
  const gavl_dictionary_t * enc = NULL;
  const gavl_dictionary_t * dict = NULL;
  const gavl_dictionary_t * video_dict;
  const char * name;
  gavl_dictionary_t * track_encoder_section;
  gavl_dictionary_t * stream_encoder_section;

  fprintf(stderr, "bg_transcoder_track_set_encoders\nEncoder section:\n");
  gavl_dictionary_dump(encoder_section, 2);
  
  if((val = gavl_dictionary_get(encoder_section, "ve")))
    video_dict = gavl_value_get_dictionary(val);
  else
    video_dict = NULL;
  
  track_encoder_section = bg_transcoder_track_get_cfg_encoder_nc(t);
  gavl_dictionary_reset(track_encoder_section);
  
  if((num = gavl_track_get_num_audio_streams(t)))
    {
    dict = gavl_dictionary_get_dictionary(encoder_section, "ae");

    gavl_dictionary_copy_value(track_encoder_section,
                               encoder_section, "ae");
    
    if((!(name = gavl_dictionary_get_string(dict, BG_CFG_TAG_NAME)) ||
        !strcmp(name, "$to_video")))
      {
      if(video_dict)
        enc = bg_cfg_section_find_subsection_c(video_dict, "$audio");
      else
        fprintf(stderr, "Wrong audio encoder configuration");
      }
    else
      enc = bg_cfg_section_find_subsection_c(dict, "$audio");
    
    for(i = 0; i < num; i++)
      {
      stream_encoder_section = bg_transcoder_track_get_cfg_encoder_nc(gavl_track_get_audio_stream_nc(t, i));
      gavl_dictionary_reset(stream_encoder_section);
      if(enc)
        gavl_dictionary_copy(stream_encoder_section, enc);
      }
    }

  if((num = gavl_track_get_num_video_streams(t)))
    {
    gavl_dictionary_copy_value(track_encoder_section,
                               encoder_section, "ve");
    
    enc = bg_cfg_section_find_subsection_c(video_dict, "$video");
    
    for(i = 0; i < num; i++)
      {
      stream_encoder_section = bg_transcoder_track_get_cfg_encoder_nc(gavl_track_get_video_stream_nc(t, i));
      gavl_dictionary_reset(stream_encoder_section);
      if(enc)
        gavl_dictionary_copy(stream_encoder_section, enc);
      }

    }
  if((num_text_streams = gavl_track_get_num_text_streams(t)))
    {
    gavl_dictionary_copy_value(track_encoder_section,
                               encoder_section, "te");
    
    if(!(name = gavl_dictionary_get_string(dict, BG_CFG_TAG_NAME)) ||
       !strcmp(name, "$to_video"))
      enc = bg_cfg_section_find_subsection_c(video_dict, "$text");
    else
      enc = bg_cfg_section_find_subsection_c(dict, "$text");

    for(i = 0; i < num_text_streams; i++)
      {
      stream_encoder_section = bg_transcoder_track_get_cfg_encoder_text_nc(gavl_track_get_text_stream_nc(t, i));
      gavl_dictionary_reset(stream_encoder_section);
      if(enc)
        gavl_dictionary_copy(stream_encoder_section, enc);
      }
    }

  num = 0;
  if(num_text_streams || (num = gavl_track_get_num_overlay_streams(t)))
    {
    gavl_dictionary_copy_value(track_encoder_section,
                               encoder_section, "oe");
    
    
    if(!(name = gavl_dictionary_get_string(dict, BG_CFG_TAG_NAME)) ||
       !strcmp(name, "$to_video"))
      enc = bg_cfg_section_find_subsection_c(video_dict, "$overlay");
    else
      enc = bg_cfg_section_find_subsection_c(dict, "$overlay");
    
    for(i = 0; i < num_text_streams; i++)
      {
      stream_encoder_section = bg_transcoder_track_get_cfg_encoder_overlay_nc(gavl_track_get_text_stream_nc(t, i));
      gavl_dictionary_reset(stream_encoder_section);
      if(enc)
        gavl_dictionary_copy(stream_encoder_section, enc);
      }
    
    for(i = 0; i < num; i++)
      {
      stream_encoder_section = bg_transcoder_track_get_cfg_encoder_nc(gavl_track_get_overlay_stream_nc(t, i));
      gavl_dictionary_reset(stream_encoder_section);
      if(enc)
        gavl_dictionary_copy(stream_encoder_section, enc);
      }
    }
  }

void bg_transcoder_track_get_encoders(const bg_transcoder_track_t * t,
                                      gavl_dictionary_t * encoder_section)
  {
  //  gavl_value_t * val;
  //  const gavl_dictionary_t * dict;
  const gavl_dictionary_t * track_encoder_section;

  track_encoder_section = bg_transcoder_track_get_cfg_encoder(t);
  
  gavl_dictionary_copy_value(encoder_section,
                             track_encoder_section, "ae");

  /*
  fprintf(stderr, "Get encoders:\n");
  gavl_value_dump(gavl_dictionary_get(encoder_section, "ae"), 2);
  fprintf(stderr, "\n");
  */
  
  gavl_dictionary_copy_value(encoder_section,
                             track_encoder_section, "ve");
  gavl_dictionary_copy_value(encoder_section,
                             track_encoder_section, "te");

  /*
  fprintf(stderr, "Get encoders te:\n");
  gavl_value_dump(gavl_dictionary_get(encoder_section, "te"), 2);
  fprintf(stderr, "\n");
  */
  
  gavl_dictionary_copy_value(encoder_section,
                             track_encoder_section, "oe");
  
  }

static const bg_parameter_info_t parameters_general[] =
  {
    {
      .name =        "subdir",
      .long_name =   TRS("Subdirectory"),
      .type =        BG_PARAMETER_STRING,
      .help_string = TRS("Subdirectory, where this track will be written to"),
    },
    { /* End of parameters */ }
  };

/* Subtitle text parameters */

static const bg_parameter_info_t general_parameters_text[] =
  {
    {
      .name =        "action",
      .long_name =   TRS("Action"),
      .type =        BG_PARAMETER_STRINGLIST,
      .val_default = GAVL_VALUE_INIT_STRING("forget"),
      .multi_names =  (char const *[]){ "forget",
                                        "transcode",
                                        "transcode_overlay",
                                        "blend",
                                        NULL },
      .multi_labels = (char const *[]){ TRS("Forget"),
                                        TRS("Transcode as text"),
                                        TRS("Transcode as overlay"),
                                        TRS("Blend onto video"),
                                        NULL },
      .help_string = TRS("Select action for this subtitle stream.")
    },
    {
      .name =        GAVL_META_LANGUAGE,
      .long_name =   TRS("Language"),
      .type =        BG_PARAMETER_STRINGLIST,
      .val_default = GAVL_VALUE_INIT_STRING("eng"),
      .multi_names =  bg_language_codes,
      .multi_labels = bg_language_labels,
    },
    {
      .name =        "force_language",
      .long_name =   TRS("Force language"),
      .type =        BG_PARAMETER_CHECKBUTTON,
      .val_default = GAVL_VALUE_INIT_INT(0),
      .help_string = TRS("Force the given language even if the input has the language set differently.")
    },
    {
      .name      = "time_offset",
      .long_name = TRS("Time offset"),
      .flags     = BG_PARAMETER_SYNC,
      .type      = BG_PARAMETER_FLOAT,
      .val_min   = GAVL_VALUE_INIT_FLOAT(-600.0),
      .val_max   = GAVL_VALUE_INIT_FLOAT(600.0),
      .num_digits = 3,
    },
    { /* End of parameters */ }
  };

/* Subtitle overlay parameters */

static const bg_parameter_info_t general_parameters_overlay[] =
  {
    {
      .name =        "action",
      .long_name =   TRS("Action"),
      .type =        BG_PARAMETER_STRINGLIST,
      .multi_names =  (char const *[]){ "forget",
                                        "copy",
                                        "transcode",
                                        "blend",
                                        NULL },
      .multi_labels = (char const *[]){ TRS("Forget"),
                                        TRS("Copy (if possible)"),
                                        TRS("Transcode"),
                                        TRS("Blend onto video"),
                                        NULL },
      .val_default = GAVL_VALUE_INIT_STRING("forget"),
    },
    {
      .name =        GAVL_META_LANGUAGE,
      .long_name =   TRS("Language"),
      .type =        BG_PARAMETER_STRINGLIST,
      .val_default = GAVL_VALUE_INIT_STRING("eng"),
      .multi_names =  bg_language_codes,
      .multi_labels = bg_language_labels,
    },
    {
      .name =        "force_language",
      .long_name =   TRS("Force language"),
      .type =        BG_PARAMETER_CHECKBUTTON,
      .val_default = GAVL_VALUE_INIT_INT(0),
      .help_string = TRS("Force the given language even if the input has the language set differently.")
    },
    { /* End of parameters */ }
  };

/* Create parameters if the config sections are already there */

bg_parameter_info_t *
bg_transcoder_track_create_parameters(bg_transcoder_track_t * track)
  {
  bg_parameter_info_t * ret;
  ret = bg_parameter_info_copy_array(parameters_general);
  return ret;
  }

static char * create_stream_label(const gavl_dictionary_t * m)
  {
  const char * info;
  const char * language;

  info = gavl_dictionary_get_string(m, GAVL_META_LABEL);
  language = gavl_dictionary_get_string(m, GAVL_META_LANGUAGE);
  
  if(language && info)
    return gavl_sprintf("%s [%s]", info, bg_get_language_name(language));
  else if(language)
    return gavl_strdup(bg_get_language_name(language));
  else if(info)
    return gavl_strdup(info);
  else
    return NULL;
  }

static void set_language(gavl_dictionary_t * general_section_dst,
                         const gavl_dictionary_t * general_section_cfg,
                         const gavl_dictionary_t * stream_m)
  {
  const char * lang_m;
  const char * lang_cfg;

  lang_m = gavl_dictionary_get_string(stream_m, GAVL_META_LANGUAGE);
  lang_cfg = gavl_dictionary_get_string(general_section_cfg, GAVL_META_LANGUAGE);

  if(lang_m && lang_cfg && strcmp(lang_m, lang_cfg))
    {
    int force = 0;

    gavl_dictionary_get_int(general_section_cfg, "force_language", &force);

    gavl_dictionary_set_string(general_section_dst,
                               GAVL_META_LANGUAGE,
                               force ? lang_cfg : lang_m);
    
    }
  }

static void sanitize_label(char* str)
  {
  int len;
  int i;
  
  if(str == NULL || *str == '\0')
    {
    return;
    }
  
  len = strlen(str);
  
  for(i = 0; i < len; i++)
    {
    char c = str[i];
    
    if((c >= 0 && c <= 31) || c == 127)
      {
      str[i] = '_';
      continue;
      }
    
    if(strchr("<>:\"/\\|?*", c) != NULL)
      {
      str[i] = '_';
      }
    }
  
  for(i = 0; i < len && str[i] == '.'; i++)
    {
    str[i] = '_';
    }
  
  for(i = len - 1; i > 0 && (str[i] == '.' || str[i] == ' '); i--)
    {
    str[i] = '_';
    len = i;
    }
  }

static int set_track(bg_transcoder_track_t * track,
                     const gavl_dictionary_t * media_info,
                     const gavl_dictionary_t * track_info,
                     gavl_dictionary_t * track_defaults_section,
                     gavl_dictionary_t * encoder_section)
  {
  int i;
  const char * disk_name;
  
  int num;
  gavl_dictionary_t * sm;
  gavl_dictionary_t * s;
  const gavl_dictionary_t * global_metadata;
  gavl_dictionary_t * metadata;

  /* Create sections */
  gavl_dictionary_t * general_section_cfg;
  gavl_dictionary_t * general_section_dst;
  const gavl_dictionary_t * filter_section;
  gavl_dictionary_t * textrenderer_section;
  gavl_dictionary_t * sec;
  
  gavl_dictionary_copy(track, track_info);

  if((metadata  = gavl_track_get_metadata_nc(track)))
    {
    char * label = gavl_dictionary_get_string_nc(metadata, GAVL_META_LABEL);
    if(label)
      sanitize_label(label);
    }
  
  if(!gavl_dictionary_get(track, GAVL_META_STREAMS))
    {
    int num_variants;
    bg_plugin_handle_t * h;
    gavl_dictionary_t * ti;
    /* Open location to load the streams */
    if(!(h = bg_load_track(track, 0, &num_variants)))
      return 0;
    ti = bg_input_plugin_get_track_info(h, -1);
    gavl_dictionary_copy_value(track, ti, GAVL_META_STREAMS);
    bg_plugin_unref(h);
    }
  
  /* Create sections */

  if(bg_transcoder_track_get_cfg_general(track))
    return 1; /* Transcoding specific stuff is already there */
  
  general_section_cfg =
    bg_cfg_section_find_subsection(track_defaults_section, "general");

  sec = bg_transcoder_track_get_cfg_general_nc(track);

  
  gavl_dictionary_copy(sec, general_section_cfg);
  
  if(media_info &&
     (global_metadata = gavl_dictionary_get_dictionary(media_info, GAVL_META_METADATA)) &&
     (disk_name = gavl_dictionary_get_string(global_metadata, GAVL_META_DISK_NAME)))
    gavl_dictionary_set_string(sec, "subdir", disk_name);
  
  bg_transcoder_track_set_encoders(track, encoder_section);
  
  if((num = gavl_track_get_num_audio_streams(track)))
    {
    general_section_cfg =
      bg_cfg_section_find_subsection(track_defaults_section, "audio");
    filter_section = bg_plugin_config_get_section(BG_PLUGIN_FILTER_AUDIO);
    
    for(i = 0; i < num; i++)
      {
      s = gavl_track_get_audio_stream_nc(track, i);
      sm = gavl_track_get_audio_metadata_nc(track, i);

      general_section_dst = bg_transcoder_track_get_cfg_general_nc(s);
      gavl_dictionary_copy(general_section_dst, general_section_cfg);
      
      gavl_dictionary_copy(bg_transcoder_track_get_cfg_filter_nc(s), filter_section);
      
      gavl_dictionary_set_string_nocopy(general_section_dst, GAVL_META_LABEL,   
                                        create_stream_label(sm));

      set_language(general_section_dst, general_section_cfg, sm);
      }
    }

  if((num = gavl_track_get_num_video_streams(track)))
    {
    general_section_cfg = bg_cfg_section_find_subsection(track_defaults_section,
                                                         "video");
    filter_section =
      bg_plugin_config_get_section(BG_PLUGIN_FILTER_VIDEO);
    
    for(i = 0; i < num; i++)
      {
      s = gavl_track_get_video_stream_nc(track, i);
      sm = gavl_track_get_video_metadata_nc(track, i);
      general_section_dst = bg_transcoder_track_get_cfg_general_nc(s);

      gavl_dictionary_copy(general_section_dst, general_section_cfg);
      gavl_dictionary_copy(bg_transcoder_track_get_cfg_filter_nc(s), filter_section);
      
      gavl_dictionary_set_string_nocopy(general_section_dst,
                                        GAVL_META_LABEL,   
                                        create_stream_label(sm));
      }
    }

  if((num = gavl_track_get_num_text_streams(track)))
    {
    general_section_cfg = bg_cfg_section_find_subsection(track_defaults_section,
                                                         "text");
    textrenderer_section = bg_cfg_section_find_subsection(track_defaults_section,
                                                          "textrenderer");
    for(i = 0; i < num; i++)
      {
      sm = gavl_track_get_text_metadata_nc(track, i);
      s = gavl_track_get_text_stream_nc(track, i);
      general_section_dst = bg_transcoder_track_get_cfg_general_nc(s);

      gavl_dictionary_copy(general_section_dst, general_section_cfg);
      gavl_dictionary_copy(bg_transcoder_track_get_cfg_textrenderer_nc(s), textrenderer_section);
      
      gavl_dictionary_set_string_nocopy(general_section_dst,
                                        GAVL_META_LABEL,   
                                        create_stream_label(sm));
      set_language(general_section_dst, general_section_cfg, sm);
      }
    }

  if((num = gavl_track_get_num_overlay_streams(track)))
    {
    general_section_cfg = bg_cfg_section_find_subsection(track_defaults_section,
                                                     "overlay");
    for(i = 0; i < num; i++)
      {
      s = gavl_track_get_overlay_stream_nc(track, i);
      sm = gavl_track_get_overlay_metadata_nc(track, i);     
      general_section_dst = bg_transcoder_track_get_cfg_general_nc(s);

      gavl_dictionary_copy(general_section_dst, general_section_cfg);

      gavl_dictionary_set_string_nocopy(general_section_cfg,
                                        GAVL_META_LABEL,   
                                        create_stream_label(sm));
      set_language(general_section_dst, general_section_cfg, sm);
      }
    }
  return 1;
  }

gavl_array_t *
bg_transcoder_track_create(const char * url,
                           gavl_dictionary_t * track_defaults_section,
                           gavl_dictionary_t * encoder_section)
  {
  int i;
  
  int num_tracks;
  gavl_dictionary_t * media_info;
  const gavl_dictionary_t * edl;
  const gavl_dictionary_t * dict;
  gavl_dictionary_t vars;
  gavl_array_t * ret = NULL;
  int track = -1;
  const char * var;

  //  fprintf(stderr, "bg_transcoder_track_create\n");
  //  gavl_dictionary_dump(encoder_section, 2);
  
  /* Load the plugin */
  gavl_dictionary_init(&vars);
  gavl_url_get_vars_c(url, &vars); 

  if((var = gavl_dictionary_get_string(&vars, GAVL_URL_VAR_TRACK)))
    track = atoi(var) - 1;
  
  gavl_dictionary_free(&vars);
  
  if(!(media_info = bg_plugin_registry_load_media_info(bg_plugin_reg, url, 0)))
    return NULL;

  if((edl = gavl_dictionary_get_dictionary(media_info, GAVL_META_EDL)))
    dict = edl;
  else
    dict = media_info;
  
  if((num_tracks = gavl_get_num_tracks(dict)))
    {
    gavl_value_t val;
    gavl_dictionary_t * t;
    
    ret = gavl_array_create();
    
    for(i = 0; i < num_tracks; i++)
      {
      if((track >= 0) && (i != track))
        continue;
      
      gavl_value_init(&val);
      
      t = gavl_value_set_dictionary(&val);
      
      set_track(t, dict, gavl_get_track(dict, i),
                track_defaults_section, 
                encoder_section);
      
      gavl_array_splice_val_nocopy(ret, ret->num_entries, 0, &val);
            
      gavl_value_free(&val);
      }
    }
  
  if(media_info)
    gavl_dictionary_destroy(media_info);
  
  return ret;
  }

gavl_array_t *
bg_transcoder_tracks_import(const gavl_array_t * tracks,
                            gavl_dictionary_t * track_defaults_section,
                            gavl_dictionary_t * encoder_section)
  {
  int i;

  gavl_array_t * ret = gavl_array_create();

  for(i = 0; i < tracks->num_entries; i++)
    {
    gavl_value_t dst_val;
    gavl_dictionary_t * dst;
    const gavl_dictionary_t * src = gavl_value_get_dictionary(&tracks->entries[i]);

    if(!src)
      continue;
    
    gavl_value_init(&dst_val);
    dst = gavl_value_set_dictionary(&dst_val);
    
    if(set_track(dst, NULL, src,
                 track_defaults_section,
                 encoder_section))
      gavl_array_splice_val_nocopy(ret, -1, 0, &dst_val);
    else
      {
      gavl_value_free(&dst_val);
      }
    
    }

  fprintf(stderr, "Imported tracks:\n");
  gavl_array_dump(ret, 2);
  
  return ret;
  
  }

gavl_array_t *
bg_transcoder_track_create_from_urilist(const char * list,
                                        int len,
                                        gavl_dictionary_t * track_defaults_section,
                                        gavl_dictionary_t * encoder_section)
  {
  int i;
  char ** uri_list;

  gavl_array_t * arr = NULL;
  gavl_array_t * ret = NULL;
  
  uri_list = bg_urilist_decode(list, len);

  if(!uri_list)
    return NULL;
  
  i = 0;

  while(uri_list[i])
    {
    if((arr = bg_transcoder_track_create(uri_list[i], track_defaults_section, 
                                         encoder_section)))
      {
      if(!ret)
        ret = gavl_array_create();
      gavl_array_splice_array(ret, ret->num_entries, 0, arr);
      gavl_array_destroy(arr);
      }
    i++;
    }
  bg_urilist_free(uri_list);
  
  return ret;
  }

static const bg_parameter_info_t general_parameters_video[] =
  {
    {
      .name =       "general",
      .long_name =  TRS("General"),
      .type =       BG_PARAMETER_SECTION
    },
    {
      .name =        "action",
      .long_name =   TRS("Action"),
      .type =        BG_PARAMETER_STRINGLIST,
      .multi_names = (char const *[]){ "transcode", "copy", "forget", NULL },
      .multi_labels =  (char const *[]){ TRS("Transcode"),
                                         TRS("Copy (if possible)"),
                                         TRS("Forget"), NULL },
      .val_default = GAVL_VALUE_INIT_STRING("transcode"),
      .help_string = TRS("Choose the desired action for the stream. If copying is not possible, the stream will be transcoded"),

    },
    {
      .name =       "twopass",
      .long_name =  TRS("Enable 2-pass encoding"),
      .type =       BG_PARAMETER_CHECKBUTTON,
      .help_string = TRS("Encode this stream in 2 passes, i.e. analyze it first and do the final\
 transcoding in the second pass. This enables higher quality within the given bitrate constraints but roughly doubles the video encoding time."),
    },
    BG_GAVL_PARAM_CONVERSION_QUALITY,
    BG_GAVL_PARAM_ALPHA,
    BG_GAVL_PARAM_RESAMPLE_CHROMA,
    BG_GAVL_PARAM_THREADS,
    { /* End of parameters */ }
  };

static const bg_parameter_info_t general_parameters_audio[] =
  {
    {
      .name =        "action",
      .long_name =   TRS("Action"),
      .type =        BG_PARAMETER_STRINGLIST,

      .multi_names = (char const *[]){ "transcode", "copy", "forget", NULL },
      .multi_labels =  (char const *[]){ TRS("Transcode"),
                                         TRS("Copy (if possible)"),
                                         TRS("Forget"), NULL },
      .val_default = GAVL_VALUE_INIT_STRING("transcode"),
      .help_string = TRS("Choose the desired action for the stream. If copying is not possible, the stream will be transcoded"),
    },
    {
      .name =        GAVL_META_LANGUAGE,
      .long_name =   TRS("Language"),
      .type =        BG_PARAMETER_STRINGLIST,
      .val_default = GAVL_VALUE_INIT_STRING("eng"),
      .multi_names =  bg_language_codes,
      .multi_labels = bg_language_labels,
    },
    {
      .name =        "force_language",
      .long_name =   TRS("Force language"),
      .type =        BG_PARAMETER_CHECKBUTTON,
      .val_default = GAVL_VALUE_INIT_INT(0),
      .help_string = TRS("Force the given language even if the input has the language set differently.")
    },
    {
      .name =        "normalize",
      .long_name =   TRS("Normalize audio"),
      .type =        BG_PARAMETER_CHECKBUTTON,
      .help_string = TRS("This will enable 2 pass transcoding. In the first pass, the peak volume\
 is detected. In the second pass, the stream is transcoded with normalized volume.")
    },
    BG_GAVL_PARAM_CONVERSION_QUALITY,
    BG_GAVL_PARAM_AUDIO_DITHER_MODE,
    BG_GAVL_PARAM_RESAMPLE_MODE,
    BG_GAVL_PARAM_CHANNEL_SETUP,
    { /* End of parameters */ }
  };


/* Audio stream parameters */

const bg_parameter_info_t *
bg_transcoder_track_audio_get_general_parameters()
  {
  return general_parameters_audio;
  }

/* Video stream parameters */

const bg_parameter_info_t *
bg_transcoder_track_video_get_general_parameters()
  {
  return general_parameters_video;
  }

const bg_parameter_info_t *
bg_transcoder_track_text_get_general_parameters()
  {
  return general_parameters_text;
  }

const bg_parameter_info_t *
bg_transcoder_track_overlay_get_general_parameters()
  {
  return general_parameters_overlay;
  }

const bg_parameter_info_t *
bg_transcoder_track_get_general_parameters()
  {
  return parameters_general;
  }


const char * bg_transcoder_track_get_name(const bg_transcoder_track_t * t)
  {
  return gavl_dictionary_get_string(gavl_track_get_metadata(t), GAVL_META_LABEL);
  }

const char * bg_transcoder_track_get_audio_encoder(const bg_transcoder_track_t * t)
  {
  const char * ret;
  const gavl_dictionary_t * dict;
  
  if((dict = gavl_dictionary_get_dictionary(bg_transcoder_track_get_cfg_encoder(t),
                                            "ae")) &&
     (ret = gavl_dictionary_get_string(dict, BG_CFG_TAG_NAME)) &&
     strcmp(ret, "$to_video"))
    return ret;
  else
    return NULL;
  
  }

const char * bg_transcoder_track_get_video_encoder(const bg_transcoder_track_t * t)
  {
  const gavl_dictionary_t * dict;

  if((dict = gavl_dictionary_get_dictionary(bg_transcoder_track_get_cfg_encoder(t),
                                            "ve")))
    return gavl_dictionary_get_string(dict, BG_CFG_TAG_NAME);
  else
    return NULL;
  }

const char * bg_transcoder_track_get_text_encoder(const bg_transcoder_track_t * t)
  {
  const char * ret;
  const gavl_dictionary_t * dict;

  if((dict = gavl_dictionary_get_dictionary(bg_transcoder_track_get_cfg_encoder(t),
                                            "te")) &&
     (ret = gavl_dictionary_get_string(dict, BG_CFG_TAG_NAME)) &&
     strcmp(ret, "$to_video"))
    return ret;
  else
    return NULL;
  }

const char * bg_transcoder_track_get_overlay_encoder(const bg_transcoder_track_t * t)
  {
  const char * ret;
  const gavl_dictionary_t * dict;

  if((dict = gavl_dictionary_get_dictionary(bg_transcoder_track_get_cfg_encoder(t),
                                            "oe")) &&
     (ret = gavl_dictionary_get_string(dict, BG_CFG_TAG_NAME)) &&
     strcmp(ret, "$to_video"))
    return ret;
  else
    return NULL;
  }

#if 1
void
bg_transcoder_track_split_at_chapters(gavl_array_t * ret, const bg_transcoder_track_t * t)
  {
  int i;
  gavl_time_t time;
  const char * label_orig = NULL;
  char * label_new;
  int num;
  int timescale;
  const gavl_dictionary_t * cl;
  const gavl_dictionary_t * m;

  gavl_value_t new_track_val;
  gavl_dictionary_t * new_track;
  gavl_dictionary_t * new_m;
  gavl_dictionary_t * general_section;
  
  
  if(!(m = gavl_track_get_metadata(t)) ||
     !(cl = gavl_dictionary_get_chapter_list(m)) ||
     ((num = gavl_chapter_list_get_num(cl)) <= 1))
    return;
  
  label_orig = gavl_dictionary_get_string(m, GAVL_META_LABEL);
  timescale = gavl_chapter_list_get_timescale(cl);

  for(i = 0; i < num; i++)
    {
    gavl_value_init(&new_track_val);
    new_track = gavl_value_set_dictionary(&new_track_val);

    gavl_dictionary_copy(new_track, t);

    new_m = gavl_track_get_metadata_nc(new_track);

    
    gavl_dictionary_set(new_m, GAVL_CHAPTERLIST_CHAPTERLIST, NULL);

    general_section = bg_transcoder_track_get_cfg_general_nc(new_track);
    
    if(i > 0)
      {
      time = gavl_time_unscale(timescale, gavl_chapter_list_get_time(cl, i));
      
      gavl_dictionary_set_int(general_section,  "set_start_time", 1);
      gavl_dictionary_set_long(general_section, "start_time", time);
      }
    if(i < num-1)
      {
      time = gavl_time_unscale(timescale, gavl_chapter_list_get_time(cl, i + 1));

      gavl_dictionary_set_int(general_section,  "set_end_time", 1);
      gavl_dictionary_set_long(general_section, "end_time", time);
      }
    
    if(label_orig)
      label_new = gavl_sprintf("%s (Chapter %02d)", label_orig, i+1);
    else
      label_new = gavl_sprintf("Chapter %02d", i+1);
    
    gavl_dictionary_set_string(new_m, GAVL_META_LABEL, label_new);
    gavl_array_splice_val_nocopy(ret, -1, 0, &new_track_val);
    }
  }
#endif
