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

#include <config.h>
#include <gmerlin/translation.h>

#include <gmerlin/plugin.h>
#include <gmerlin/pluginfuncs.h>
#include <gmerlin/utils.h>
#include <gmerlin/log.h>
#include <gavl/metatags.h>

#include "lqt_common.h"
#include "lqtgavl.h"

#define LOG_DOMAIN "e_lqt"

#define CODEC_PARAMETER \
    { \
      .name =      "codec", \
      .long_name = TRS("Codec"), \
    }


static const bg_parameter_info_t audio_parameters[] = 
  {
    CODEC_PARAMETER,
    { /* End */ }
  };

static const bg_parameter_info_t video_parameters[] = 
  {
    BG_ENCODER_FRAMERATE_PARAMS, /* Must come first (before the codec is set) */
    CODEC_PARAMETER,
    { /* End */ }
  };

typedef struct e_lqt_s e_lqt_t;

typedef struct
  {
  gavl_audio_format_t format;
  lqt_codec_info_t ** codec_info;
  int64_t samples_written;
  
  gavl_audio_sink_t * sink;
  gavl_packet_sink_t * psink;

  int compressed;
  int index;

  e_lqt_t * e;
  } audio_stream_t;

typedef struct
  {
  gavl_video_format_t format;
  uint8_t ** rows;
  lqt_codec_info_t ** codec_info;
  bg_encoder_framerate_t fr;
  int64_t frames_written;
  int64_t pts_offset;
    
  gavl_video_sink_t * sink;
  gavl_packet_sink_t * psink;

  int compressed;
  int index;

  e_lqt_t * e;
  } video_stream_t;

typedef struct
  {
  char language[4];
  int timescale;
  int64_t last_end_time;

  uint16_t text_box[4];

  uint16_t fg_color[4];
  uint16_t bg_color[4];
  gavl_packet_sink_t * psink;

  int index;
  e_lqt_t * e;
  } text_stream_t;

struct e_lqt_s
  {
  int max_riff_size;
  char * filename;
  char * filename_tmp;
  quicktime_t * file;
  
  char * audio_codec_name;
  char * video_codec_name;

  bg_parameter_info_t * audio_parameters;
  bg_parameter_info_t * video_parameters;
  
  lqt_file_type_t file_type;
  int make_streamable;
  
  int num_video_streams;
  int num_audio_streams;
  int num_text_streams;
  
  /* Needed for calculating the duration of the last chapter */
  gavl_time_t duration;
  
  bg_encoder_callbacks_t * cb;

  audio_stream_t         * audio_streams;
  video_stream_t         * video_streams;
  text_stream_t * text_streams;
  
  const gavl_chapter_list_t * chapter_list;
  
  int chapter_track_id;
  };

static void * create_lqt()
  {
  e_lqt_t * ret = calloc(1, sizeof(*ret));

  lqt_set_log_callback(bg_lqt_log, NULL);
  
  return ret;
  }

static void set_callbacks_lqt(void * data, bg_encoder_callbacks_t * cb)
  {
  e_lqt_t * e = data;
  e->cb = cb;
  }

static const struct
  {
  int type_mask;
  char * extension;
  }
extensions[] =
  {
    { LQT_FILE_QT  | LQT_FILE_QT_OLD,      "mov" },
    { LQT_FILE_AVI | LQT_FILE_AVI_ODML,    "avi" },
    { LQT_FILE_MP4,                        "mp4" },
    { LQT_FILE_M4A,                        "m4a" },
    { LQT_FILE_3GP,                        "3gp" },
  };

static const char * get_extension(int type)
  {
  int i;
  for(i = 0; i < sizeof(extensions)/sizeof(extensions[0]); i++)
    {
    if(extensions[i].type_mask & type)
      return extensions[i].extension;
    }
  return extensions[0].extension; /* "mov" */
  }

static int open_lqt(void * data, const char * filename,
                    const gavl_dictionary_t * m,
                    const gavl_chapter_list_t * chapter_list)
  {
  e_lqt_t * e = data;

  e->filename =
    bg_filename_ensure_extension(filename, get_extension(e->file_type));

  if(!bg_encoder_cb_create_output_file(e->cb, e->filename))
    return 0;
  
  if(e->make_streamable && !(e->file_type & (LQT_FILE_AVI|LQT_FILE_AVI_ODML)))
    {
    e->filename_tmp = bg_sprintf("%s.tmp", e->filename);
    
    if(!bg_encoder_cb_create_temp_file(e->cb, e->filename_tmp))
      return 0;
    
    e->file = lqt_open_write(e->filename_tmp, e->file_type);
    }
  else
    {
    e->file = lqt_open_write(e->filename, e->file_type);
    }
  if(!e->file)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Cannot open file %s", e->filename);
    return 0;
    }

  if(e->file_type == LQT_FILE_AVI_ODML)
    lqt_set_max_riff_size(e->file, e->max_riff_size);
    
  /* Set metadata */

  if(m)
    {
    char * tag;

    /* TODO: Make the arguments in lqt const friendly */
    
    tag = gavl_strdup(gavl_dictionary_get_string(m, GAVL_META_COPYRIGHT));
    if(tag)
      {
      quicktime_set_copyright(e->file, tag);
      free(tag);
      }

    tag = gavl_strdup(gavl_dictionary_get_string(m, GAVL_META_TITLE));
    if(tag)
      {
      quicktime_set_name(e->file, tag);
      free(tag);
      }
    
    tag = gavl_strdup(gavl_dictionary_get_string(m, GAVL_META_COMMENT));
    if(tag)
      {
      lqt_set_comment(e->file, tag);
      free(tag);
      }
    
    tag = gavl_strdup(gavl_dictionary_get_string(m, GAVL_META_ARTIST));
    if(tag)
      {
      lqt_set_artist(e->file, tag);
      free(tag);
      }
    
    tag = gavl_strdup(gavl_dictionary_get_string(m, GAVL_META_GENRE));
    if(tag)
      {
      lqt_set_genre(e->file, tag);
      free(tag);
      }
    
    tag = gavl_strdup(gavl_dictionary_get_string(m, GAVL_META_TRACKNUMBER));
    if(tag)
      {
      lqt_set_track(e->file, tag);
      free(tag);
      }

    tag = gavl_strdup(gavl_dictionary_get_string(m, GAVL_META_ALBUM));
    if(tag)
      {
      lqt_set_album(e->file, tag);
      free(tag);
      }

    tag = gavl_strdup(gavl_dictionary_get_string(m, GAVL_META_AUTHOR));
    if(tag)
      {
      lqt_set_author(e->file, tag);
      free(tag);
      }
    }
  
  e->chapter_list = chapter_list;
  
  return 1;
  }

static int writes_compressed_audio_lqt(void * data,
                                       const gavl_audio_format_t * format,
                                       const gavl_compression_info_t * ci)
  {
  e_lqt_t * e = data;
  return lqt_gavl_writes_compressed_audio(e->file_type, format, ci);
  }

static int writes_compressed_video_lqt(void * data,
                                       const gavl_video_format_t * format,
                                       const gavl_compression_info_t * ci)
  {
  e_lqt_t * e = data;
  return lqt_gavl_writes_compressed_video(e->file_type, format, ci);
  }

static audio_stream_t * append_audio_stream(e_lqt_t * e)
  {
  audio_stream_t * ret;
  e->audio_streams =
    realloc(e->audio_streams,
            (e->num_audio_streams+1)*sizeof(*(e->audio_streams)));

  ret = &e->audio_streams[e->num_audio_streams];
  ret->index = e->num_audio_streams;
  
  memset(ret, 0, sizeof(*ret));
  ret->e = e;
  e->num_audio_streams++;
  return ret;
  }

static video_stream_t * append_video_stream(e_lqt_t * e)
  {
  video_stream_t * ret;
  e->video_streams =
    realloc(e->video_streams,
            (e->num_video_streams+1)*sizeof(*(e->video_streams)));

  ret = &e->video_streams[e->num_video_streams];
  ret->index = e->num_video_streams;
  
  memset(ret, 0, sizeof(*ret));
  ret->e = e;
  e->num_video_streams++;
  return ret;
  }

static int add_audio_stream_lqt(void * data,
                                const gavl_dictionary_t * m,
                                const gavl_audio_format_t * format)
  {
  const char * tag;
  audio_stream_t * as;
  e_lqt_t * e = data;
  
  as = append_audio_stream(e);

  gavl_audio_format_copy(&as->format, format);
  
  lqt_gavl_add_audio_track(e->file, &as->format,
                           NULL);

  tag = gavl_dictionary_get_string(m, GAVL_META_LANGUAGE);
  if(tag)
    lqt_set_audio_language(e->file, as->index, tag);
  return as->index;
  }

static int
add_audio_stream_compressed_lqt(void * data,
                                const gavl_dictionary_t * m,
                                const gavl_audio_format_t * format,
                                const gavl_compression_info_t * ci)
  {
  const char * tag;
  audio_stream_t * as;
  e_lqt_t * e = data;

  as = append_audio_stream(e);
  as->compressed = 1;
  lqt_gavl_add_audio_track_compressed(e->file, format, ci);

  tag = gavl_dictionary_get_string(m, GAVL_META_LANGUAGE);
  if(tag)
    lqt_set_audio_language(e->file, as->index, tag);
  return as->index;
  }

static int add_text_stream_lqt(void * data,
                                        const gavl_dictionary_t * m,
                                        uint32_t * timescale)
  {
  const char * tag;
  text_stream_t * s;
  e_lqt_t * e = data;

  e->text_streams =
    realloc(e->text_streams,
            (e->num_text_streams+1)*
            sizeof(*(e->text_streams)));

  s = e->text_streams + e->num_text_streams;
  
  memset(s, 0, sizeof(*s));

  tag = gavl_dictionary_get_string(m, GAVL_META_LANGUAGE);
  if(tag)
    strncpy(s->language, tag, 3);
  
  s->timescale = *timescale;
  s->index = e->num_text_streams;
  s->e = e;
  
  e->num_text_streams++;
  return s->index;
  }



static int add_video_stream_lqt(void * data,
                                const gavl_dictionary_t* m,
                                const gavl_video_format_t* format)
  {
  video_stream_t * vs;
  e_lqt_t * e = data;
  vs = append_video_stream(e);
  gavl_video_format_copy(&vs->format, format);
  lqt_gavl_add_video_track(e->file, &vs->format, NULL);
  return vs->index;
  }

static int
add_video_stream_compressed_lqt(void * data,
                                const gavl_dictionary_t* m,
                                const gavl_video_format_t* format,
                                const gavl_compression_info_t * ci)
  {
  video_stream_t * vs;
  e_lqt_t * e = data;
  vs = append_video_stream(e);
  lqt_gavl_add_video_track_compressed(e->file, format, ci);
  vs->compressed = 1;
  return vs->index;
  }

static gavl_video_sink_t * get_video_sink_lqt(void * data, int stream)
  {
  e_lqt_t * e = data;
  return e->video_streams[stream].sink;
  }

static gavl_audio_sink_t * get_audio_sink_lqt(void * data, int stream)
  {
  e_lqt_t * e = data;
  return e->audio_streams[stream].sink;
  }

static gavl_packet_sink_t * get_video_packet_sink_lqt(void * data, int stream)
  {
  e_lqt_t * e = data;
  return e->video_streams[stream].psink;
  }

static gavl_packet_sink_t * get_text_sink_lqt(void * data, int stream)
  {
  e_lqt_t * e = data;
  return e->text_streams[stream].psink;
  }

static gavl_packet_sink_t * get_audio_packet_sink_lqt(void * data, int stream)
  {
  e_lqt_t * e = data;
  return e->audio_streams[stream].psink;
  }

static gavl_sink_status_t
write_audio_func_lqt(void * data, gavl_audio_frame_t* frame)
  {
  gavl_time_t test_time;
  audio_stream_t * as = data;
  
  if(!as->samples_written && frame->timestamp)
    lqt_set_audio_pts_offset(as->e->file, as->index, frame->timestamp);
  
  as->samples_written += frame->valid_samples;
  
  test_time = gavl_time_unscale(as->format.samplerate,
                                as->samples_written);
  if(as->e->duration < test_time)
    as->e->duration = test_time;
  
  return lqt_encode_audio_raw(as->e->file, frame->samples.s_8,
                              frame->valid_samples, as->index) ?
    GAVL_SINK_OK : GAVL_SINK_ERROR;
  }



static gavl_sink_status_t
write_video_func_lqt(void * data, gavl_video_frame_t* frame)
  {
  gavl_time_t test_time;
  video_stream_t * vs = data;
  
  test_time = gavl_time_unscale(vs->format.timescale,
                                frame->timestamp);
  if(vs->e->duration < test_time)
    vs->e->duration = test_time;

  if(!vs->frames_written)
    {
    vs->pts_offset = frame->timestamp;
    if(vs->pts_offset)
      lqt_set_video_pts_offset(vs->e->file, vs->index,
                               vs->pts_offset);
    }
  vs->frames_written++;
  
  return lqt_gavl_encode_video(vs->e->file, vs->index, frame,
                               vs->rows,
                               vs->pts_offset) ?
    GAVL_SINK_ERROR : GAVL_SINK_OK;
  }

static gavl_sink_status_t
write_video_packet_func(void * data, gavl_packet_t * p)
  {
  gavl_time_t test_time;
  video_stream_t * s = data;
  
  test_time = gavl_time_unscale(s->format.timescale,
                                p->pts);
  if(s->e->duration < test_time)
    s->e->duration = test_time;
  
  if(!s->frames_written)
    {
    s->pts_offset = p->pts;
    if(s->pts_offset)
      lqt_set_video_pts_offset(s->e->file, s->index,
                               s->pts_offset);
    }
  s->frames_written++;
  
  return lqt_gavl_write_video_packet(s->e->file, s->index, p) ?
    GAVL_SINK_OK : GAVL_SINK_ERROR;
  }

static gavl_sink_status_t
write_audio_packet_func(void * data, gavl_packet_t * p)
  {
  gavl_time_t test_time;
  audio_stream_t * s = data;
  
  if(!s->samples_written && p->pts)
    lqt_set_audio_pts_offset(s->e->file, s->index, p->pts);
  s->samples_written += p->duration;

  test_time = gavl_time_unscale(s->format.samplerate,
                                s->samples_written);
  if(s->e->duration < test_time)
    s->e->duration = test_time;
  
  return lqt_gavl_write_audio_packet(s->e->file, s->index, p) ?
    GAVL_SINK_OK : GAVL_SINK_ERROR;
  }

static gavl_sink_status_t
write_text_func(void * data,
                         gavl_packet_t * p)
  {
  text_stream_t * s = data;
  
  if(s->e->file_type & (LQT_FILE_AVI|LQT_FILE_AVI_ODML))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "AVI subtitles not supported");
    return GAVL_SINK_ERROR;
    }

  //  fprintf(stderr, "write subtitle %"PRId64", %"PRId64", \"%s\"\n",
  //        start, duration, text);
  

  /* Put empty subtitle if the last end time is not equal to
     this start time */
  if(s->last_end_time < p->pts)
    {
    if(lqt_write_text(s->e->file, s->index, "",
                      p->pts - s->last_end_time))
      return GAVL_SINK_ERROR;
    }
  
  if(lqt_write_text(s->e->file, s->index, (char*)p->data, p->duration))
    return 0;
  s->last_end_time = p->pts + p->duration;
  return GAVL_SINK_OK;
  }


static int start_lqt(void * data)
  {
  int i, tmp;
  e_lqt_t * e = data;

  for(i = 0; i < e->num_audio_streams; i++)
    {
    audio_stream_t * as = &e->audio_streams[i];
    /* Ugly hack */
    tmp = as->format.samples_per_frame;
    lqt_gavl_get_audio_format(e->file,
                              i,
                              &as->format);
    as->format.samples_per_frame = tmp;

    if(!as->compressed)
      as->sink = gavl_audio_sink_create(NULL, write_audio_func_lqt,
                                        as, &as->format);
    else
      as->psink = gavl_packet_sink_create(NULL, write_audio_packet_func,
                                          as);
    }
  for(i = 0; i < e->num_video_streams; i++)
    {
    video_stream_t * vs = &e->video_streams[i];
    lqt_gavl_get_video_format(e->file, i, &vs->format, 1);
    if(!vs->compressed)
      vs->sink = gavl_video_sink_create(NULL,
                                        write_video_func_lqt,
                                        vs, &vs->format);
    else
      vs->psink = gavl_packet_sink_create(NULL, write_video_packet_func,
                                          vs);
    }

  if(!(e->file_type & (LQT_FILE_AVI|LQT_FILE_AVI_ODML)))
    {
    /* Add the subtitle tracks */
    for(i = 0; i < e->num_text_streams; i++)
      {
      text_stream_t * ts;
      ts = &e->text_streams[i];

      lqt_add_text_track(e->file, ts->timescale);
      lqt_set_text_language(e->file, i, ts->language);
    
      lqt_set_text_box(e->file, i,
                       ts->text_box[0],
                       ts->text_box[1],
                       ts->text_box[2],
                       ts->text_box[3]);

      lqt_set_text_fg_color(e->file, i,
                            ts->fg_color[0],
                            ts->fg_color[1],
                            ts->fg_color[2],
                            ts->fg_color[3]);

      lqt_set_text_bg_color(e->file, i,
                            ts->bg_color[0],
                            ts->bg_color[1],
                            ts->bg_color[2],
                            ts->bg_color[3]);

      ts->psink = gavl_packet_sink_create(NULL, write_text_func,
                                          ts);
      
      }
  
    /* Add the chapter track */
    if(e->chapter_list)
      {
      lqt_add_text_track(e->file, e->chapter_list->timescale);
      e->chapter_track_id = e->num_text_streams;
      lqt_set_chapter_track(e->file, e->chapter_track_id);
      }
    }
  else /* AVI case: Check if QT-only audio codecs were added */
    {
    for(i = 0; i < e->num_audio_streams; i++)
      {
      if(!e->audio_streams[i].codec_info[0]->wav_ids ||
         // TODO: Make LQT_WAV_ID_NONE part of the public lqt API
         (e->audio_streams[i].codec_info[0]->wav_ids[0] == -1)) 
        {
        gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN,
               "Audio codec %s cannot be written to AVI files",
               e->audio_streams[i].codec_info[0]->name);
        return 0;
        }
      }
    }
  
  return 1;
  }



static int close_lqt(void * data, int do_delete)
  {
  int i;
  gavl_time_t chapter_time;
  int num_chapters;
  e_lqt_t * e = data;
  
  if(!e->file)
    return 1;
  
  if(!(e->file_type & (LQT_FILE_AVI|LQT_FILE_AVI_ODML)) &&
     e->chapter_list)
    {
    /* Count the chapters actually written */
    
    num_chapters = 0;
    for(i = 0; i < e->chapter_list->num_chapters; i++)
      {
      chapter_time = gavl_time_unscale(e->chapter_list->timescale,
                                       e->chapter_list->chapters[i].time);
      
      if(chapter_time > e->duration)
        {
        gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN,
               "Omitting chapter %d: time (%f) > duration (%f)",
               i+1,
               gavl_time_to_seconds(chapter_time),
               gavl_time_to_seconds(e->duration));
        break;
        }
      else
        num_chapters++;
      }
    
    /* Write chapter information */
    for(i = 0; i < num_chapters; i++)
      {
      if(i < num_chapters-1)
        {
        if(lqt_write_text(e->file, e->chapter_track_id,
                          e->chapter_list->chapters[i].name,
                          e->chapter_list->chapters[i+1].time -
                          e->chapter_list->chapters[i].time))
          return 0;
        }
      else
        {
        if(lqt_write_text(e->file, e->chapter_track_id,
                          e->chapter_list->chapters[i].name,
                          e->duration - e->chapter_list->chapters[i].time))
          return 0;
        }
      }
    }
  
  quicktime_close(e->file);
  e->file = NULL;
  
  if(do_delete)
    remove(e->filename);

  else if(e->filename_tmp)
    {
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Making streamable....");
    quicktime_make_streamable(e->filename_tmp, e->filename);
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Making streamable....done");
    remove(e->filename_tmp);
    }
  
  if(e->filename)
    {
    free(e->filename);
    e->filename = NULL;
    }
  if(e->filename_tmp)
    {
    free(e->filename_tmp);
    e->filename_tmp = NULL;
    }
  if(e->audio_streams)
    {
    for(i = 0; i < e->num_audio_streams; i++)
      {
      audio_stream_t * as = &e->audio_streams[i];
      if(as->codec_info)
        lqt_destroy_codec_info(as->codec_info);
      if(as->sink)
        gavl_audio_sink_destroy(as->sink);
      if(as->psink)
        gavl_packet_sink_destroy(as->psink);
      }

    free(e->audio_streams);
    e->audio_streams = NULL;
    }
  if(e->video_streams)
    {
    for(i = 0; i < e->num_video_streams; i++)
      {
      video_stream_t * vs = &e->video_streams[i];
      
      if(vs->codec_info)
        lqt_destroy_codec_info(vs->codec_info);
      lqt_gavl_rows_destroy(vs->rows);

      if(vs->sink)
        gavl_video_sink_destroy(vs->sink);
      }
    free(e->video_streams);
    e->video_streams = NULL;
    }
  if(e->text_streams)
    {
    for(i = 0; i < e->num_text_streams; i++)
      {
      text_stream_t * ts = &e->text_streams[i];
      if(ts->psink)
        gavl_packet_sink_destroy(ts->psink);
      }
    free(e->text_streams);
    e->text_streams = NULL;
    }
  
  e->num_audio_streams = 0;
  e->num_video_streams = 0;
  e->num_text_streams = 0;
  return 1;
  }

static void destroy_lqt(void * data)
  {
  e_lqt_t * e = data;

  close_lqt(data, 1);
  
  if(e->audio_parameters)
    bg_parameter_info_destroy_array(e->audio_parameters);
  if(e->video_parameters)
    bg_parameter_info_destroy_array(e->video_parameters);

  free(e);
  }

static void create_parameters(e_lqt_t * e)
  {
  e->audio_parameters = bg_parameter_info_copy_array(audio_parameters);
  e->video_parameters = bg_parameter_info_copy_array(video_parameters);
  
  bg_lqt_create_codec_info(&e->audio_parameters[0],
                           1, 0, 1, 0);
  bg_lqt_create_codec_info(&e->video_parameters[2],
                           0, 1, 1, 0);
  
  }

static const bg_parameter_info_t common_parameters[] =
  {
    {
      .name =      "format",
      .long_name = TRS("Format"),
      .type =      BG_PARAMETER_STRINGLIST,
      .multi_names =    (char const *[]) { "quicktime", "avi", "avi_opendml",   "mp4", "m4a", "3gp", NULL },
      .multi_labels =   (char const *[]) { TRS("Quicktime"), TRS("AVI"), TRS("AVI (Opendml)"),
                                  TRS("MP4"), TRS("M4A"), TRS("3GP"), NULL },
      .val_default = GAVL_VALUE_INIT_STRING("quicktime"),
    },
    {
      .name =      "make_streamable",
      .long_name = TRS("Make streamable"),
      .type =      BG_PARAMETER_CHECKBUTTON,
      .help_string = TRS("Make the file streamable afterwards (uses twice the diskspace)"),
    },
    {
      .name =      "max_riff_size",
      .long_name = TRS("Maximum RIFF size"),
      .type =      BG_PARAMETER_INT,
      .val_min =     GAVL_VALUE_INIT_INT(1),
      .val_max =     GAVL_VALUE_INIT_INT(1024),
      .val_default = GAVL_VALUE_INIT_INT(1024),
      .help_string = TRS("Maximum RIFF size (in MB) for OpenDML AVIs. The default (1GB) is reasonable and should only be changed by people who know what they do."),
    },
    { /* End of parameters */ }
  };

static const bg_parameter_info_t * get_parameters_lqt(void * data)
  {
  return common_parameters;
  }

static void set_parameter_lqt(void * data, const char * name,
                              const gavl_value_t * val)
  {
  e_lqt_t * e = data;
  if(!name)
    return;

  else if(!strcmp(name, "format"))
    {
    if(!strcmp(val->v.str, "quicktime"))
      e->file_type = LQT_FILE_QT;
    else if(!strcmp(val->v.str, "avi"))
      e->file_type = LQT_FILE_AVI;
    else if(!strcmp(val->v.str, "avi_opendml"))
      e->file_type = LQT_FILE_AVI_ODML;
    else if(!strcmp(val->v.str, "mp4"))
      e->file_type = LQT_FILE_MP4;
    else if(!strcmp(val->v.str, "m4a"))
      e->file_type = LQT_FILE_M4A;
    else if(!strcmp(val->v.str, "3gp"))
      e->file_type = LQT_FILE_3GP;
    }
  else if(!strcmp(name, "make_streamable"))
    e->make_streamable = val->v.i;
  else if(!strcmp(name, "max_riff_size"))
    e->max_riff_size = val->v.i;
  
  }

static const bg_parameter_info_t * get_audio_parameters_lqt(void * data)
  {
  e_lqt_t * e = data;
  
  if(!e->audio_parameters)
    create_parameters(e);
  
  return e->audio_parameters;
  }

static const bg_parameter_info_t * get_video_parameters_lqt(void * data)
  {
  e_lqt_t * e = data;
  
  if(!e->video_parameters)
    create_parameters(e);
  
  return e->video_parameters;
  }

static void set_audio_parameter_lqt(void * data, int stream, const char * name,
                                    const gavl_value_t * val)
  {
  e_lqt_t * e = data;
    
  if(!name)
    return;

  if(!strcmp(name, "codec"))
    {
    /* Now we can add the stream */

    e->audio_streams[stream].codec_info =
      lqt_find_audio_codec_by_name(val->v.str);
    
    lqt_gavl_set_audio_codec(e->file, stream,
                             &e->audio_streams[stream].format,
                             *e->audio_streams[stream].codec_info);
    }
  else
    {
    bg_lqt_set_audio_parameter(e->file,
                               stream,
                               name,
                               val,
                               e->audio_streams[stream].codec_info[0]->encoding_parameters);
      
    }

  

  }

static int set_video_pass_lqt(void * data, int stream, int pass,
                              int total_passes, const char * stats_file)
  {
  e_lqt_t * e = data;
  return lqt_set_video_pass(e->file, pass, total_passes, stats_file, stream);
  }



static void set_video_parameter_lqt(void * data, int stream, const char * name,
                                    const gavl_value_t * val)
  {
  e_lqt_t * e = data;
  
  if(!name)
    return;
  else if(bg_encoder_set_framerate_parameter(&e->video_streams[stream].fr,
                                             name, val))
    return;
  else if(!strcmp(name, "codec"))
    {
    /* Now we can add the stream */

    e->video_streams[stream].codec_info =
      lqt_find_video_codec_by_name(val->v.str);
    
    if(e->file_type & (LQT_FILE_AVI|LQT_FILE_AVI_ODML))
      {
      e->video_streams[stream].format.pixel_width = 1;
      e->video_streams[stream].format.pixel_height = 1;

      bg_encoder_set_framerate(&e->video_streams[stream].fr,
                               &e->video_streams[stream].format);
      }
    
    lqt_gavl_set_video_codec(e->file, stream, &e->video_streams[stream].format,
                             *e->video_streams[stream].codec_info);
    
    e->video_streams[stream].rows = lqt_gavl_rows_create(e->file, stream);
    }
  else
    {

    bg_lqt_set_video_parameter(e->file,
                               stream,
                               name,
                               val,
                               e->video_streams[stream].codec_info[0]->encoding_parameters);

    }
  }



/* Subtitle parameters */

static const bg_parameter_info_t text_parameters[] =
  {
    {
      .name =      "box_top",
      .long_name = TRS("Text box (top)"),
      .type =      BG_PARAMETER_INT,
      .val_min =   GAVL_VALUE_INIT_INT(0),
      .val_max =   { .val_i = 0xffff },
    },
    {
      .name =      "box_left",
      .long_name = TRS("Text box (left)"),
      .type =      BG_PARAMETER_INT,
      .val_min =   GAVL_VALUE_INIT_INT(0),
      .val_max =   { .val_i = 0xffff },
    },
    {
      .name =      "box_bottom",
      .long_name = TRS("Text box (bottom)"),
      .type =      BG_PARAMETER_INT,
      .val_min =   GAVL_VALUE_INIT_INT(0),
      .val_max =   { .val_i = 0xffff },
    },
    {
      .name =      "box_right",
      .long_name = TRS("Text box (right)"),
      .type =      BG_PARAMETER_INT,
      .val_min =   GAVL_VALUE_INIT_INT(0),
      .val_max =   { .val_i = 0xffff },
    },
    {
      .name =        "fg_color",
      .long_name =   TRS("Text color"),
      .type =        BG_PARAMETER_COLOR_RGBA,
      .val_default = { .val_color = { 1.0, 1.0, 1.0, 1.0 }},
    },
    {
      .name =        "bg_color",
      .long_name =   TRS("Background color"),
      .type =        BG_PARAMETER_COLOR_RGBA,
      .val_default = { .val_color = { 0.0, 0.0, 0.0, 1.0 }},
    },
    { /* End of parameters */ },
  };

static const bg_parameter_info_t *
get_text_parameters_lqt(void * priv)
  {
  return text_parameters;
  }

#define CNV_COLOR(c) (int)(c * 65535.0 + 0.5)

static void
set_text_parameter_lqt(void * priv, int stream,
                                const char * name,
                                const gavl_value_t * val)
  {
  e_lqt_t * e = priv;
  text_stream_t * ts;
  
  if(!name)
    return;

  ts = &e->text_streams[stream];
  
  if(!strcmp(name, "box_top"))
    ts->text_box[0] = val->v.i;
  else if(!strcmp(name, "box_left"))
    ts->text_box[1] = val->v.i;
  else if(!strcmp(name, "box_bottom"))
    ts->text_box[2] = val->v.i;
  else if(!strcmp(name, "box_right"))
    ts->text_box[3] = val->v.i;
  else if(!strcmp(name, "fg_color"))
    {
    ts->fg_color[0] = CNV_COLOR(val->val_color[0]);
    ts->fg_color[1] = CNV_COLOR(val->val_color[1]);
    ts->fg_color[2] = CNV_COLOR(val->val_color[2]);
    ts->fg_color[3] = CNV_COLOR(val->val_color[3]);
    }
  else if(!strcmp(name, "bg_color"))
    {
    ts->bg_color[0] = CNV_COLOR(val->val_color[0]);
    ts->bg_color[1] = CNV_COLOR(val->val_color[1]);
    ts->bg_color[2] = CNV_COLOR(val->val_color[2]);
    ts->bg_color[3] = CNV_COLOR(val->val_color[3]);
    }
  }

const bg_encoder_plugin_t the_plugin =
  {
    .common =
    {
      BG_LOCALE,
      .name =           "e_lqt",       /* Unique short name */
      .long_name =      TRS("Quicktime encoder"),
      .description =    TRS("Encoder based on libquicktime (http://libquicktime.sourceforge.net)\
 Writes Quicktime, AVI (optionally ODML), MP4, M4A and 3GPP. Supported codecs range from \
high quality uncompressed formats for professional applications to consumer level formats \
like H.264/AVC, AAC, MP3, Divx compatible etc. Also supported are chapters and text subtitles"),
      .type =           BG_PLUGIN_ENCODER,
      .flags =          BG_PLUGIN_FILE,
      .priority =       BG_PLUGIN_PRIORITY_MAX,
      .create =         create_lqt,
      .destroy =        destroy_lqt,
      .get_parameters = get_parameters_lqt,
      .set_parameter =  set_parameter_lqt,
    },
    
    .max_audio_streams =         -1,
    .max_video_streams =         -1,
    .max_text_streams = -1,

    .set_callbacks =         set_callbacks_lqt,
    
    .get_audio_parameters =         get_audio_parameters_lqt,
    .get_video_parameters =         get_video_parameters_lqt,
    .get_text_parameters = get_text_parameters_lqt,

    .open =                 open_lqt,

    .writes_compressed_audio = writes_compressed_audio_lqt,
    .writes_compressed_video = writes_compressed_video_lqt,
    
    .add_audio_stream =     add_audio_stream_lqt,
    .add_audio_stream_compressed =     add_audio_stream_compressed_lqt,
    
    .add_text_stream = add_text_stream_lqt,

    .add_video_stream =     add_video_stream_lqt,
    .add_video_stream_compressed =     add_video_stream_compressed_lqt,
    .set_video_pass =       set_video_pass_lqt,
    
    .set_audio_parameter =          set_audio_parameter_lqt,
    .set_video_parameter =          set_video_parameter_lqt,
    .set_text_parameter =  set_text_parameter_lqt,
    
    .get_audio_sink =     get_audio_sink_lqt,
    .get_video_sink =     get_video_sink_lqt,

    .get_audio_packet_sink =     get_audio_packet_sink_lqt,
    .get_video_packet_sink =     get_video_packet_sink_lqt,
    .get_text_sink =     get_text_sink_lqt,
    .start =                start_lqt,
    .close =                close_lqt,
  };

/* Include this into all plugin modules exactly once
   to let the plugin loader obtain the API version */
BG_GET_PLUGIN_API_VERSION;
