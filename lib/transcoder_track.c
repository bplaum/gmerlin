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
  const gavl_dictionary_t * dict;
  const gavl_dictionary_t * video_dict;
  const char * name;
  gavl_dictionary_t * track_encoder_section;
  gavl_dictionary_t * stream_encoder_section;
  
  if((val = gavl_dictionary_get(encoder_section, "ve")))
    video_dict = bg_multi_menu_get_selected(val);
  else
    video_dict = NULL;
  
  track_encoder_section = bg_track_get_cfg_encoder_nc(t);
  gavl_dictionary_reset(track_encoder_section);
  
  if((num = gavl_track_get_num_audio_streams(t)))
    {
    val = gavl_dictionary_get(encoder_section, "ae");
    dict = bg_multi_menu_get_selected(val);

    gavl_dictionary_set_dictionary(track_encoder_section, "ae", dict);

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
      stream_encoder_section = bg_stream_get_cfg_encoder_nc(gavl_track_get_audio_stream_nc(t, i));
      gavl_dictionary_reset(stream_encoder_section);
      if(enc)
        gavl_dictionary_copy(stream_encoder_section, enc);
      }
    }

  if((num = gavl_track_get_num_video_streams(t)))
    {
    gavl_dictionary_set_dictionary(track_encoder_section, "ve", video_dict);

    enc = bg_cfg_section_find_subsection_c(video_dict, "$video");
    
    for(i = 0; i < num; i++)
      {
      stream_encoder_section = bg_stream_get_cfg_encoder_nc(gavl_track_get_video_stream_nc(t, i));
      gavl_dictionary_reset(stream_encoder_section);
      if(enc)
        gavl_dictionary_copy(stream_encoder_section, enc);
      }

    }
  if((num_text_streams = gavl_track_get_num_text_streams(t)))
    {
    val = gavl_dictionary_get(encoder_section, "te");
    dict = bg_multi_menu_get_selected(val);

    gavl_dictionary_set_dictionary(track_encoder_section, "te", dict);

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
    val = gavl_dictionary_get(encoder_section, "oe");
    dict = bg_multi_menu_get_selected(val);
    
    gavl_dictionary_set_dictionary(track_encoder_section, "oe", dict);
    
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
      stream_encoder_section = bg_stream_get_cfg_encoder_nc(gavl_track_get_overlay_stream_nc(t, i));
      gavl_dictionary_reset(stream_encoder_section);
      if(enc)
        gavl_dictionary_copy(stream_encoder_section, enc);
      }
    }
  }

void bg_transcoder_track_get_encoders(const bg_transcoder_track_t * t,
                                      gavl_dictionary_t * encoder_section)
  {
  gavl_value_t * val;
  const gavl_dictionary_t * dict;
  const gavl_dictionary_t * track_encoder_section;

  track_encoder_section = bg_track_get_cfg_encoder(t);
  
  if((dict = gavl_dictionary_get_dictionary(track_encoder_section, "ae")) &&
     (val = gavl_dictionary_get_nc(encoder_section, "ae")))
    {
    bg_multi_menu_set_selected(val, dict);
    }

  if((dict = gavl_dictionary_get_dictionary(track_encoder_section, "ve")) &&
     (val = gavl_dictionary_get_nc(encoder_section, "ve")))
    {
    bg_multi_menu_set_selected(val, dict);
    }

  if((dict = gavl_dictionary_get_dictionary(track_encoder_section, "te")) &&
     (val = gavl_dictionary_get_nc(encoder_section, "te")))
    {
    bg_multi_menu_set_selected(val, dict);
    }

  if((dict = gavl_dictionary_get_dictionary(track_encoder_section, "oe")) &&
     (val = gavl_dictionary_get_nc(encoder_section, "ov")))
    {
    bg_multi_menu_set_selected(val, dict);
    }
  
  }

static const bg_parameter_info_t parameters_general[] =
  {
    {
      .name =        "subdir",
      .long_name =   TRS("Subdirectory"),
      .type =        BG_PARAMETER_STRING,
      .help_string = TRS("Subdirectory, where this track will be written to"),
    },
    {
      .name =      "set_start_time",
      .long_name = TRS("Set start time"),
      .type =      BG_PARAMETER_CHECKBUTTON,
      .flags =     BG_PARAMETER_HIDE_DIALOG,
      .val_default = GAVL_VALUE_INIT_INT(0),
      .help_string = TRS("Specify a start time below. This time will be slightly wrong if the input \
format doesn't support sample accurate seeking.")
    },
    {
      .name =      "start_time",
      .long_name = TRS("Start time"),
      .type =      BG_PARAMETER_TIME,
      .flags =     BG_PARAMETER_HIDE_DIALOG,
      .val_default = GAVL_VALUE_INIT_LONG(0)
    },
    {
      .name =      "set_end_time",
      .long_name = TRS("Set end time"),
      .type =      BG_PARAMETER_CHECKBUTTON,
      .val_default = GAVL_VALUE_INIT_INT(0),
      .help_string = TRS("Specify an end time below.")
    },
    {
      .name =      "end_time",
      .long_name = TRS("End time"),
      .type =      BG_PARAMETER_TIME,
      .val_default = GAVL_VALUE_INIT_LONG(0)
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
      .name =        "in_language",
      .long_name =   TRS("Input Language"),
      .type =        BG_PARAMETER_STRING,
      .flags =       BG_PARAMETER_HIDE_DIALOG,
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

/* Create subtitle parameters */
#if 0
static void create_subtitle_parameters(bg_transcoder_track_t * track)
  {
  int i;
  bg_parameter_info_t * info;

  /* Create subtitle parameters. These depend on the number of video streams */

  for(i = 0; i < track->num_text_streams; i++)
    {
    /* Forget, Dump, Blend #1, Blend #2 ... */
    track->text_streams[i].general_parameters =
      bg_parameter_info_copy_array(general_parameters_text);
    info = track->text_streams[i].general_parameters;

    if(track->num_video_streams > 1)
      {
      gavl_value_set_int(&info[1].val_max, track->num_video_streams);
      info[1].flags &= ~BG_PARAMETER_HIDE_DIALOG;
      }
    
    }
  for(i = 0; i < track->num_overlay_streams; i++)
    {
    /* Forget, Blend #1, Blend #2 ... */

    track->overlay_streams[i].general_parameters =
      bg_parameter_info_copy_array(general_parameters_overlay);
    info = track->overlay_streams[i].general_parameters;

    if(track->num_video_streams > 1)
      {
      gavl_value_set_int(&info[1].val_max, track->num_video_streams);
      info[1].flags &= ~BG_PARAMETER_HIDE_DIALOG;
      }
    }
  }
#endif

/* Create parameters if the config sections are already there */

bg_parameter_info_t *
bg_transcoder_track_create_parameters(bg_transcoder_track_t * track)
  {
  gavl_time_t duration = GAVL_TIME_UNDEFINED;
  int i;
  int can_seek = 0;
  bg_parameter_info_t * ret;
  const gavl_dictionary_t * track_metadata;

  track_metadata = gavl_track_get_metadata(track);
  
  ret = bg_parameter_info_copy_array(parameters_general);
    
  gavl_dictionary_get_long(track_metadata, GAVL_META_APPROX_DURATION, &duration);
  gavl_dictionary_get_int(track_metadata, GAVL_META_CAN_SEEK, &can_seek);
    
  if(duration != GAVL_TIME_UNDEFINED)
    {
    i = 0;
      
    while(ret[i].name)
      {
      if(!strcmp(ret[i].name, "start_time"))
        {
        gavl_value_set_long(&ret[i].val_max, duration);
        if(can_seek)
          ret[i].flags &= ~BG_PARAMETER_HIDE_DIALOG;

        }
      else if(!strcmp(ret[i].name, "end_time"))
        {
        gavl_value_set_long(&ret[i].val_max, duration);
        }
      else if(!strcmp(ret[i].name, "set_start_time"))
        {
        if(can_seek)
          ret[i].flags &= ~BG_PARAMETER_HIDE_DIALOG;
        }
      i++;
      }
    }
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

static void set_track(bg_transcoder_track_t * track,
                      const gavl_dictionary_t * media_info,
                      int track_index,
                      gavl_dictionary_t * track_defaults_section,
                      gavl_dictionary_t * encoder_section)
  {
  int i;
  const char * disk_name;
  
  int num;
  gavl_dictionary_t * sm;
  gavl_dictionary_t * s;
  const gavl_dictionary_t * global_metadata;

  /* Create sections */
  gavl_dictionary_t * general_section_cfg;
  gavl_dictionary_t * general_section_dst;
  gavl_dictionary_t * filter_section;
  gavl_dictionary_t * textrenderer_section;
  gavl_dictionary_t * sec;
  
  const gavl_dictionary_t * track_info = gavl_get_track(media_info, track_index);
  
  gavl_dictionary_copy(track, track_info);
  
  /* Create sections */
  
  general_section_cfg =
    bg_cfg_section_find_subsection(track_defaults_section, "general");

  sec = bg_transcoder_track_get_cfg_general_nc(track);

  gavl_dictionary_copy(sec, general_section_cfg);
  
  if((global_metadata = gavl_dictionary_get_dictionary(media_info, GAVL_META_METADATA)) &&
     (disk_name = gavl_dictionary_get_string(global_metadata, GAVL_META_DISK_NAME)))
    gavl_dictionary_set_string(sec, "subdir", disk_name);
  
  bg_transcoder_track_set_encoders(track, encoder_section);
  
  if((num = gavl_track_get_num_audio_streams(track)))
    {
    general_section_cfg =
      bg_cfg_section_find_subsection(track_defaults_section, "audio");
    filter_section =
      bg_cfg_section_find_subsection(track_defaults_section, "audiofilters");
    
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
      bg_cfg_section_find_subsection(track_defaults_section, "videofilters");
    
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
    //    gavl_dictionary_t * tm;

    //    int cover_w, cover_h;
    //    const char * cover_uri;
    //    const char * cover_mimetype;
    
    ret = gavl_array_create();
    
    for(i = 0; i < num_tracks; i++)
      {
      if((track >= 0) && (i != track))
        continue;
      
      gavl_value_init(&val);
      
      t = gavl_value_set_dictionary(&val);
      
      set_track(t, dict, i, track_defaults_section, 
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
      .name =        "in_language",
      .long_name =   TRS("Input Language"),
      .type =        BG_PARAMETER_STRING,
      .flags =       BG_PARAMETER_HIDE_DIALOG,
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
  
  if((dict = gavl_dictionary_get_dictionary(bg_track_get_cfg_encoder(t),
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

  if((dict = gavl_dictionary_get_dictionary(bg_track_get_cfg_encoder(t),
                                            "ve")))
    return gavl_dictionary_get_string(dict, BG_CFG_TAG_NAME);
  else
    return NULL;
  }

const char * bg_transcoder_track_get_text_encoder(const bg_transcoder_track_t * t)
  {
  const char * ret;
  const gavl_dictionary_t * dict;

  if((dict = gavl_dictionary_get_dictionary(bg_track_get_cfg_encoder(t),
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

  if((dict = gavl_dictionary_get_dictionary(bg_track_get_cfg_encoder(t),
                                            "oe")) &&
     (ret = gavl_dictionary_get_string(dict, BG_CFG_TAG_NAME)) &&
     strcmp(ret, "$to_video"))
    return ret;
  else
    return NULL;
  }

void bg_transcoder_track_get_duration(const bg_transcoder_track_t * t, gavl_time_t * ret,
                                      gavl_time_t * ret_total)
  {
  gavl_time_t start_time = 0, end_time = 0, duration_total = 0;
  int set_start_time = 0, set_end_time = 0;
  const gavl_dictionary_t * general_section = bg_transcoder_track_get_cfg_general(t);
  const gavl_dictionary_t * metadata_section = gavl_track_get_metadata(t);
  bg_cfg_section_get_parameter_int(general_section,  "set_start_time", &set_start_time);
  bg_cfg_section_get_parameter_int(general_section,  "set_end_time", &set_end_time);

  bg_cfg_section_get_parameter_time(metadata_section, GAVL_META_APPROX_DURATION, &duration_total);
  bg_cfg_section_get_parameter_time(general_section, "start_time", &start_time);
  bg_cfg_section_get_parameter_time(general_section, "end_time",   &end_time);

  if(ret_total)
    *ret_total = duration_total;
  
  if(duration_total == GAVL_TIME_UNDEFINED)
    {
    if(set_end_time)
      *ret = end_time;
    else
      *ret = duration_total;
    }
  else
    {
    if(set_start_time)
      {
      if(set_end_time) /* Start and end */
        {
        *ret = end_time - start_time;
        if(*ret < 0)
          *ret = 0;
        }
      else /* Start only */
        {
        *ret = duration_total - start_time;
        if(*ret < 0)
          *ret = 0;
        }
      }
    else
      {
      if(set_end_time) /* End only */
        {
        *ret = end_time;
        }
      else
        {
        *ret = duration_total;
        }
      }
    }
  
  return;
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
