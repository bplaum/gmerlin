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
#include <ctype.h>

#include <config.h>
#include <gmerlin/translation.h>

#include <gmerlin/plugin.h>
#include <gmerlin/utils.h>
#include <gmerlin/log.h>

#include <gavl/metatags.h>

#include "lqt_common.h"
#include "lqtgavl.h"

#define LOG_DOMAIN "i_lqt"

#define PARAM_AUDIO 1
#define PARAM_VIDEO 3

static const bg_parameter_info_t parameters[] = 
  {
    {
      .name =      "audio",
      .long_name = TRS("Audio"),
      .type =      BG_PARAMETER_SECTION,
    },
    {
      .name =      "audio_codecs",
      .opt =       "ac",
      .long_name = TRS("Audio Codecs"),
      .help_string = TRS("Sort and configure audio codecs"),
    },
    {
      .name =      "video",
      .long_name = TRS("Video"),
      .type =      BG_PARAMETER_SECTION,
    },
    {
      .name =      "video_codecs",
      .opt =       "vc",
      .long_name = TRS("Video Codecs"),
      .help_string = TRS("Sort and configure video codecs"),
    },
    { /* End of parameters */ }
  };

typedef struct
  {
  int quicktime_index;
  int64_t pts_offset;
    
  quicktime_t * file; // Points to global struct
  gavl_audio_source_t * src;
  gavl_packet_source_t * psrc;

  gavl_audio_format_t * fmt;

  gavl_compression_info_t ci;
  } audio_stream_t;
  
typedef struct
  {
  int quicktime_index;
  unsigned char ** rows;
  int has_timecodes;
  int64_t pts_offset;
  
  quicktime_t * file; // Points to global struct
  gavl_video_source_t * src;
  gavl_packet_source_t * psrc;
  
  gavl_video_format_t * fmt;
  gavl_compression_info_t ci;
  } video_stream_t;

typedef struct
  {
  int quicktime_index;
  int64_t pts_offset;
  quicktime_t * file; // Points to global struct
  
  char * text;
  int text_alloc;
  gavl_packet_source_t * src;
  } text_stream_t;

typedef struct
  {
  quicktime_t * file;
  bg_parameter_info_t * parameters;

  char * audio_codec_string;
  char * video_codec_string;

  bg_track_info_t track_info;

  audio_stream_t * audio_streams;
  video_stream_t * video_streams;
  text_stream_t * text_streams;
  
  } i_lqt_t;

static void * create_lqt()
  {
  i_lqt_t * ret = calloc(1, sizeof(*ret));

  lqt_set_log_callback(bg_lqt_log, NULL);

  return ret;
  }

static void setup_chapters(i_lqt_t * e, int track)
  {
  int i, num;
  int64_t timestamp, duration;
  char * text = NULL;
  int text_alloc = 0;
  
  e->track_info.chapter_list = gavl_chapter_list_create(0);
  e->track_info.chapter_list->timescale = lqt_text_time_scale(e->file, track);

  num = lqt_text_samples(e->file, track);
  
  for(i = 0; i < num; i++)
    {
    if(lqt_read_text(e->file, track, &text, &text_alloc, &timestamp, &duration))
      {
      gavl_chapter_list_insert(e->track_info.chapter_list, i, timestamp, text);
      }
    else
      break;
    }
  if(text) free(text);
  }

static int open_lqt(void * data, const char * arg)
  {
  const char * tag;
  int i;
  char * filename;
  int num_audio_streams = 0;
  int num_video_streams = 0;
  int num_text_streams = 0;
  gavl_dictionary_t * m;
  i_lqt_t * e = data;

  lqt_codec_info_t ** codec_info;

  char lang[4];
  lang[3] = '\0';
  
  /* We want to keep the thing const-clean */
  filename = gavl_strdup(arg);
  e->file = quicktime_open(filename, 1, 0);
  free(filename);

  if(!e->file)
    return 0;

  bg_set_track_name_default(&e->track_info, arg);

  /* Set metadata */

  m = &e->track_info.metadata;
  
  tag = quicktime_get_name(e->file);
  if(tag)
    gavl_dictionary_set_string(m, GAVL_META_TITLE, tag);

  tag = quicktime_get_copyright(e->file);
  if(tag)
    gavl_dictionary_set_string(m, GAVL_META_COPYRIGHT, tag);

  tag = lqt_get_comment(e->file);
  if(!tag)
    tag = quicktime_get_info(e->file);

  if(tag)
    gavl_dictionary_set_string(m, GAVL_META_COMMENT, tag);

  

  tag = lqt_get_track(e->file);
  if(tag)
    gavl_dictionary_set_string(m, GAVL_META_TRACKNUMBER, tag);

  tag = lqt_get_artist(e->file);
  if(tag)
    gavl_dictionary_set_string(m, GAVL_META_ARTIST, tag);

  tag = lqt_get_album(e->file);
  if(tag)
    gavl_dictionary_set_string(m, GAVL_META_ALBUM, tag);

  tag = lqt_get_genre(e->file);
  if(tag)
    gavl_dictionary_set_string(m, GAVL_META_GENRE, tag);

  tag = lqt_get_author(e->file);
  if(tag)
    gavl_dictionary_set_string(m, GAVL_META_AUTHOR, tag);
  
  
  /* Query streams */

  num_audio_streams = quicktime_audio_tracks(e->file);
  num_video_streams = quicktime_video_tracks(e->file);
  num_text_streams  = lqt_text_tracks(e->file);

  e->track_info.flags = BG_TRACK_SEEKABLE | BG_TRACK_PAUSABLE;
  if(num_audio_streams)
    {
    e->audio_streams = calloc(num_audio_streams, sizeof(*e->audio_streams));
    e->track_info.audio_streams =
      calloc(num_audio_streams, sizeof(*e->track_info.audio_streams));
    
    for(i = 0; i < num_audio_streams; i++)
      {
      if(quicktime_supported_audio(e->file, i))
        {
        m = &e->track_info.audio_streams[e->track_info.num_audio_streams].m;
        
        e->audio_streams[e->track_info.num_audio_streams].quicktime_index = i;
        e->audio_streams[e->track_info.num_audio_streams].pts_offset =
          lqt_get_audio_pts_offset(e->file, i);
        
        codec_info = lqt_audio_codec_from_file(e->file, i);

        gavl_dictionary_set_string(m, GAVL_META_FORMAT, codec_info[0]->long_name);

        lqt_destroy_codec_info(codec_info);

        lqt_get_audio_language(e->file, i, lang);
        gavl_dictionary_set_string(m, GAVL_META_LANGUAGE, lang);
        

        e->track_info.num_audio_streams++;
        }
      }
    }

  if(num_video_streams)
    {
    e->video_streams = calloc(num_video_streams, sizeof(*e->video_streams));
    e->track_info.video_streams =
      calloc(num_video_streams, sizeof(*e->track_info.video_streams));
    
    
    for(i = 0; i < num_video_streams; i++)
      {
      if(quicktime_supported_video(e->file, i))
        {
        m = &e->track_info.video_streams[e->track_info.num_video_streams].m;
        
        e->video_streams[e->track_info.num_video_streams].quicktime_index = i;
        
        codec_info = lqt_video_codec_from_file(e->file, i);
        gavl_dictionary_set_string(m, GAVL_META_FORMAT, codec_info[0]->long_name);
        lqt_destroy_codec_info(codec_info);
        
        e->video_streams[e->track_info.num_video_streams].rows =
          lqt_gavl_rows_create(e->file, i);
        e->video_streams[e->track_info.num_video_streams].pts_offset =
          lqt_get_video_pts_offset(e->file, i);
        e->track_info.num_video_streams++;
        }
      }
    }
  if(num_text_streams)
    {
    e->text_streams = calloc(num_text_streams, sizeof(*e->text_streams));
    e->track_info.text_streams =
      calloc(num_text_streams, sizeof(*e->track_info.text_streams));
    
    for(i = 0; i < num_text_streams; i++)
      {
      if(lqt_is_chapter_track(e->file, i))
        {
        if(e->track_info.chapter_list)
          {
          gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN,
                 "More than one chapter track found, using first one");
          }
        else
          setup_chapters(e, i);
        }
      else
        {
        m = &e->track_info.text_streams[e->track_info.num_text_streams].m;
        
        e->text_streams[e->track_info.num_text_streams].file = e->file;
        e->text_streams[e->track_info.num_text_streams].quicktime_index = i;

        e->track_info.text_streams[e->track_info.num_text_streams].timescale =
          lqt_text_time_scale(e->file, i);
        
        e->text_streams[e->track_info.num_text_streams].pts_offset =
          lqt_get_text_pts_offset(e->file, i);
        
        lqt_get_text_language(e->file, i, lang);
        gavl_dictionary_set_string(m, GAVL_META_LANGUAGE, lang);
        
        e->track_info.num_text_streams++;
        }
      }
    
    
    }

  gavl_dictionary_set_string_long(&e->track_info.metadata, GAVL_META_APPROX_DURATION,
                         lqt_gavl_duration(e->file));
  
  if(lqt_is_avi(e->file))
    gavl_dictionary_set_string(&e->track_info.metadata, GAVL_META_FORMAT, "AVI (lqt)");
  else
    gavl_dictionary_set_string(&e->track_info.metadata, GAVL_META_FORMAT, "Quicktime (lqt)");
  
  //  if(!e->track_info.num_audio_streams && !e->track_info.num_video_streams)
  //    return 0;
  return 1;
  }

static int get_num_tracks_lqt(void * data)
  {
  return 1;
  }

static bg_track_info_t * get_track_info_lqt(void * data, int track)
  {
  i_lqt_t * e = data;
  return &e->track_info;
  }

/* Read one audio frame (returns FALSE on EOF) */
static  
gavl_source_status_t read_audio_func(void * data, gavl_audio_frame_t ** fp)
  {
  gavl_audio_frame_t * f;
  audio_stream_t * as = data;
  
  f = *fp;
  
  if(!lqt_gavl_decode_audio(as->file, as->quicktime_index,
                            f, as->fmt->samples_per_frame))
    {
    return GAVL_SOURCE_EOF;
    }
  f->timestamp += as->pts_offset;

  //  fprintf(stderr, "Decode %d\n", f->valid_samples);
  
  return f->valid_samples ? GAVL_SOURCE_OK : GAVL_SOURCE_EOF;
  }


/* Read one video frame (returns FALSE on EOF) */

static gavl_source_status_t
read_video_func(void * data, gavl_video_frame_t ** fp)
  {
  gavl_video_frame_t * f;
  video_stream_t * vs = data;
  f = *fp;
  if(!lqt_gavl_decode_video(vs->file, vs->quicktime_index, f, vs->rows))
    return GAVL_SOURCE_EOF;
  f->timestamp += vs->pts_offset;
  return GAVL_SOURCE_OK;
  }


static void close_lqt(void * data)
  {
  int i;
  i_lqt_t * e = data;
  
  if(e->file)
    {
    quicktime_close(e->file);
    e->file = NULL;
    }
  if(e->audio_streams)
    {
    for(i = 0; i < e->track_info.num_audio_streams; i++)
      {
      if(e->audio_streams[i].src)
        gavl_audio_source_destroy(e->audio_streams[i].src);

      if(e->audio_streams[i].psrc)
        gavl_packet_source_destroy(e->audio_streams[i].psrc);
      gavl_compression_info_free(&e->audio_streams[i].ci);
      }
    free(e->audio_streams);
    e->audio_streams = NULL;
    }
  if(e->video_streams)
    {
    for(i = 0; i < e->track_info.num_video_streams; i++)
      {
      if(e->video_streams[i].rows)
        free(e->video_streams[i].rows);
      if(e->video_streams[i].src)
        gavl_video_source_destroy(e->video_streams[i].src);
      if(e->video_streams[i].psrc)
        gavl_packet_source_destroy(e->video_streams[i].psrc);
      gavl_compression_info_free(&e->video_streams[i].ci);
      }
    free(e->video_streams);
    e->video_streams = NULL;
    }
  if(e->text_streams)
    {
    for(i = 0; i < e->track_info.num_text_streams; i++)
      {
      if(e->text_streams[i].src)
        gavl_packet_source_destroy(e->text_streams[i].src);
      }
    free(e->text_streams);
    e->text_streams = NULL;
    }
  bg_track_info_free(&e->track_info);  
  }

static void seek_lqt(void * data, gavl_time_t * time, int scale)
  {
  int i;
  i_lqt_t * e = data;

  //  fprintf(stderr, "Seek lqt\n");
  
  lqt_gavl_seek_scaled(e->file, time, scale);

  for(i = 0; i < e->track_info.num_audio_streams; i++)
    {
    if(e->audio_streams[i].src)
      gavl_audio_source_reset(e->audio_streams[i].src);
    }
  for(i = 0; i < e->track_info.num_video_streams; i++)
    {
    if(e->video_streams[i].src)
      gavl_video_source_reset(e->video_streams[i].src);
    }
  }


static void destroy_lqt(void * data)
  {
  i_lqt_t * e = data;
  close_lqt(data);

  if(e->parameters)
    bg_parameter_info_destroy_array(e->parameters);

  if(e->audio_codec_string)
    free(e->audio_codec_string);

  if(e->video_codec_string)
    free(e->video_codec_string);
      
  
  free(e);
  }

static void create_parameters(i_lqt_t * e)
  {

  e->parameters = bg_parameter_info_copy_array(parameters);
    
  bg_lqt_create_codec_info(&e->parameters[PARAM_AUDIO],
                           1, 0, 0, 1);
  bg_lqt_create_codec_info(&e->parameters[PARAM_VIDEO],
                           0, 1, 0, 1);

  
  }

static const bg_parameter_info_t * get_parameters_lqt(void * data)
  {
  i_lqt_t * e = data;
  
  if(!e->parameters)
    create_parameters(e);
  
  return e->parameters;
  }

static void set_parameter_lqt(void * data, const char * name,
                              const gavl_value_t * val)
  {
  i_lqt_t * e = data;
  char * pos;
  char * tmp_string;
  if(!name)
    return;

  
  if(!e->parameters)
    create_parameters(e);
#if 0
  if(bg_lqt_set_parameter(name, val, &e->parameters[PARAM_AUDIO]) ||
     bg_lqt_set_parameter(name, val, &e->parameters[PARAM_VIDEO]))
    return;
#endif
  if(!strcmp(name, "audio_codecs"))
    {
    e->audio_codec_string = gavl_strrep(e->audio_codec_string, val->v.str);
    }
  else if(!strcmp(name, "video_codecs"))
    {
    e->video_codec_string = gavl_strrep(e->video_codec_string, val->v.str);
    }
  else if(!strncmp(name, "audio_codecs.", 13))
    {
    tmp_string = gavl_strdup(name+13);
    pos = strchr(tmp_string, '.');
    *pos = '\0';
    pos++;
    
    bg_lqt_set_audio_decoder_parameter(tmp_string, pos, val);
    free(tmp_string);
    
    }
  else if(!strncmp(name, "video_codecs.", 13))
    {
    tmp_string = gavl_strdup(name+13);
    pos = strchr(tmp_string, '.');
    *pos = '\0';
    pos++;
    
    bg_lqt_set_video_decoder_parameter(tmp_string, pos, val);
    free(tmp_string);
    
    }
  }

static int get_audio_compression_info_lqt(void * data,
                                          int stream, gavl_compression_info_t * ci)
  {
  i_lqt_t * e = data;
  if((e->audio_streams[stream].ci.id == GAVL_CODEC_ID_NONE) &&
     !lqt_gavl_get_audio_compression_info(e->file, stream, &e->audio_streams[stream].ci))
    return 0;

  gavl_compression_info_copy(ci, &e->audio_streams[stream].ci);
  return 1;
  }

static int get_video_compression_info_lqt(void * data,
                                          int stream, gavl_compression_info_t * ci)
  {
  i_lqt_t * e = data;
  if((e->video_streams[stream].ci.id == GAVL_CODEC_ID_NONE) &&
     !lqt_gavl_get_video_compression_info(e->file, stream, &e->video_streams[stream].ci))
    return 0;

  gavl_compression_info_copy(ci, &e->video_streams[stream].ci);
  return 1;
  }

static gavl_source_status_t read_audio_packet_lqt(void * data, gavl_packet_t ** p)
  {
  audio_stream_t * s = data;
  if(!lqt_gavl_read_audio_packet(s->file, s->quicktime_index, *p))
    return GAVL_SOURCE_EOF;

  (*p)->pts += s->pts_offset;
  return GAVL_SOURCE_OK;
  }

static gavl_source_status_t read_video_packet_lqt(void * data, gavl_packet_t ** p)
  {
  video_stream_t * s = data;
  if(!lqt_gavl_read_video_packet(s->file, s->quicktime_index, *p))
    return GAVL_SOURCE_EOF;

  (*p)->pts += s->pts_offset;
  return GAVL_SOURCE_OK;
  }

static gavl_source_status_t read_text_func(void * priv,
                                           gavl_packet_t ** ret)
  {
  int len;
  int64_t start_time;
  int64_t duration;

  text_stream_t * ts = priv;
  
  int result =  lqt_read_text(ts->file,
                              ts->quicktime_index,
                              &ts->text, &ts->text_alloc,
                              &start_time, &duration);
  if(!result)
    return GAVL_SOURCE_EOF;
  start_time += ts->pts_offset;

  len = strlen(ts->text);
  
  gavl_packet_alloc(*ret, len);
  memcpy((*ret)->data, ts->text, len);
  (*ret)->data_len = len;
  (*ret)->pts = start_time;
  (*ret)->duration = duration;
  
  return GAVL_SOURCE_OK;
  }

static int start_lqt(void * data)
  {
  int i;
  i_lqt_t * e = data;

  for(i = 0; i < e->track_info.num_audio_streams; i++)
    {
    audio_stream_t * as = &e->audio_streams[i];
    as->fmt = &e->track_info.audio_streams[i].format;

    lqt_gavl_get_audio_format(e->file, as->quicktime_index, as->fmt);

    as->src =
      gavl_audio_source_create(read_audio_func, as, 0, as->fmt);

    as->psrc =
      gavl_packet_source_create_audio(read_audio_packet_lqt,
                                      as, 0, &as->ci, as->fmt);
    
    as->file = e->file;
    }
  for(i = 0; i < e->track_info.num_video_streams; i++)
    {
    video_stream_t * vs = &e->video_streams[i];
    vs->fmt = &e->track_info.video_streams[i].format;
    
    lqt_gavl_get_video_format(e->file, vs->quicktime_index, vs->fmt, 0);

    vs->src =
      gavl_video_source_create(read_video_func, vs, 0, vs->fmt);

    vs->psrc =
      gavl_packet_source_create_video(read_video_packet_lqt,
                                      vs, 0, &vs->ci, vs->fmt);
    
    vs->file = e->file;
    }
  for(i = 0; i < e->track_info.num_text_streams; i++)
    {
    text_stream_t * ts = &e->text_streams[i];

    ts->src =
      gavl_packet_source_create_text(read_text_func, ts, 0, e->track_info.text_streams[i].timescale);
    ts->file = e->file;
    }
  
  return 1;
  }

static gavl_audio_source_t *
get_audio_source_lqt(void * data, int stream)
  {
  i_lqt_t * e = data;
  return e->audio_streams[stream].src;
  }

static gavl_video_source_t *
get_video_source_lqt(void * data, int stream)
  {
  i_lqt_t * e = data;
  return e->video_streams[stream].src;
  }

static gavl_packet_source_t *
get_audio_packet_source_lqt(void * data, int stream)
  {
  i_lqt_t * e = data;
  return e->audio_streams[stream].psrc;
  }

static gavl_packet_source_t *
get_video_packet_source_lqt(void * data, int stream)
  {
  i_lqt_t * e = data;
  return e->video_streams[stream].psrc;
  }

static gavl_packet_source_t *
get_text_source_lqt(void * data, int stream)
  {
  i_lqt_t * e = data;
  return e->text_streams[stream].src;
  }

char const * const extensions = "mov";

static const char * get_extensions(void * priv)
  {
  return extensions;
  }

const bg_input_plugin_t the_plugin =
  {
    .common =
    {
      BG_LOCALE,
      .name =            "i_lqt",       /* Unique short name */
      .long_name =       TRS("libquicktime input plugin"),
      .description =     TRS("Input plugin based on libquicktime"),
      .type =            BG_PLUGIN_INPUT,
      .flags =           BG_PLUGIN_FILE,
      .priority =        5,
      .create =          create_lqt,
      .destroy =         destroy_lqt,
      .get_parameters =  get_parameters_lqt,
      .set_parameter =   set_parameter_lqt,
    },

    .get_extensions =    get_extensions,
    .open =              open_lqt,
    .get_num_tracks =    get_num_tracks_lqt,
    .get_track_info =    get_track_info_lqt,
    //    .set_audio_stream =  set_audio_stream_lqt,
    //    .set_video_stream =  set_audio_stream_lqt,

    .get_audio_source = get_audio_source_lqt,
    .get_video_source = get_video_source_lqt,

    .get_audio_packet_source = get_audio_packet_source_lqt,
    .get_video_packet_source = get_video_packet_source_lqt,
    
    .get_audio_compression_info = get_audio_compression_info_lqt,
    .get_video_compression_info = get_video_compression_info_lqt,

    .start =             start_lqt,
    
    //    .has_subtitle =       has_subtitle_lqt,
    //    .read_subtitle_text = read_subtitle_text_lqt,

    .get_text_source = get_text_source_lqt,
    
    //    .read_audio_packet = read_audio_packet_lqt,
    //    .read_video_packet = read_video_packet_lqt,
    
    .seek =               seek_lqt,
    //    .stop =               stop_lqt,
    .close =              close_lqt
    
  };

/* Include this into all plugin modules exactly once
   to let the plugin loader obtain the API version */
BG_GET_PLUGIN_API_VERSION;
