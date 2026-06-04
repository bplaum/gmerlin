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
#include <errno.h>
#include <math.h>

// mkdir()

#include <config.h>
#include <gmerlin/translation.h>

#include <gmerlin/pluginregistry.h>
#include <gmerlin/log.h>
#include <gmerlin/bgmsg.h>
#include <gmerlin/transcoder.h>
#include <gmerlin/transcodermsg.h>
#include <gmerlin/utils.h>
#include <gmerlin/bggavl.h>
#include <gmerlin/subtitle.h>

#include <gmerlin/textrenderer.h>
#include <gmerlin/filters.h>
#include <gmerlin/encoder.h>

#include <gmerlin/subprocess.h>

#include <gavl/metatags.h>

// #define DUMP_VIDEO_TIMESTAMPS
// #define DUMP_AUDIO_TIMESTAMPS

#define LOG_DOMAIN "transcoder"


/* The followings are for subtitles only */

#define STREAM_FLAG_EOF     (1<<0) 
#define STREAM_FLAG_DISCONT (1<<1) 

#define TRANSCODER_STATE_INIT     0
#define TRANSCODER_STATE_RUNNING  1
#define TRANSCODER_STATE_FINISHED 2
#define TRANSCODER_STATE_ERROR    3

typedef struct
  {
  void * fc; // Filter chain

  bg_media_source_stream_t * st;
  
  int status;
  int timescale;
  
  int out_index;
  gavl_time_t time;
  
  gavl_compression_info_t ci;
  gavl_packet_t packet;
  gavl_dictionary_t m;

  gavl_packet_sink_t * psink;
  gavl_audio_sink_t * asink;
  gavl_video_sink_t * vsink;

  gavl_audio_source_t * asrc;
  gavl_video_source_t * vsrc;
  
  bg_transcoder_t * t;

  int64_t samples_read;
  
  } stream_t;

static int set_stream_parameters_general(stream_t * s,
                                         const char * name,
                                         const gavl_value_t * val)
  {
  if(!strcmp(name, "action"))
    {
    if(!strcmp(val->v.str, "transcode"))
      s->st->action = BG_STREAM_ACTION_DECODE;
    else if(!strcmp(val->v.str, "copy"))
      s->st->action = BG_STREAM_ACTION_READRAW;
    else
      s->st->action = BG_STREAM_ACTION_OFF;
    return 1;
    }
  return 0;
  }

static void set_audio_parameter_general(void * data, const char * name,
                                        const gavl_value_t * val)
  {
  stream_t * stream = data;
  
  if(!name)
    return;

  if(!strcmp(name, GAVL_META_LANGUAGE))
    {
    gavl_dictionary_set_string(&stream->m, GAVL_META_LANGUAGE,val->v.str);
    return;
    }
  else if(set_stream_parameters_general(stream, name, val))
    return;
  }

static void set_video_parameter_general(void * data,
                                        const char * name,
                                        const gavl_value_t * val)
  {
  stream_t * stream = data;
  
  if(!name)
    return;
    
  if(set_stream_parameters_general(stream, name, val))
    return;
  }

static void set_subtitle_parameter_general(void * data,
                                           const char * name,
                                           const gavl_value_t * val)
  {
  stream_t * stream = data;

  if(!name)
    return;

  if(!strcmp(name, GAVL_META_LANGUAGE))
    {
    gavl_dictionary_set_string(&stream->m, GAVL_META_LANGUAGE,val->v.str);
    return;
    }
  else if(set_stream_parameters_general(stream, name, val))
    return;
  }

struct bg_transcoder_s
  {
  stream_t * streams;
  int num_streams;
  
  float percentage_done;
  gavl_time_t remaining_time; /* Remaining time (Transcoding time, NOT track time!!!) */
  double last_seconds;
    
  int state;
    
  bg_plugin_handle_t * in_handle;
  bg_input_plugin_t  * in_plugin;

  bg_plugin_handle_t * enc_handle;
  bg_encoder_plugin_t * enc_plugin;
  
  gavl_dictionary_t * track_info;
  bg_media_source_t * src;
  
  /* Set by set_parameter */

  char * name;
  const char * location;
  
  int track;

  gavl_dictionary_t metadata;
  
  /* General configuration stuff */

  char * output_directory;
  char * output_path;
  char * subdir;
  
  int delete_incomplete;

  /* Timing stuff */

  gavl_timer_t * timer;
  gavl_time_t time;

  /* Duration of the section to be transcoded */
    
  gavl_time_t duration;
  
  char * output_filename;
  /* Message queues */
  bg_msg_hub_t * message_hub;
  
  /* Multi threading stuff */
  pthread_t thread;

  int do_stop;
  pthread_mutex_t stop_mutex;

  /* Multipass */

  /* Track we are created from */
  bg_transcoder_track_t * transcoder_track;
  
  };

/* Parameter passing for the Transcoder */

#define SP_STR(s) if(!strcmp(name, # s))   \
    { \
    t->s = gavl_strrep(t->s, val->v.str);  \
    return; \
    }

#define SP_INT(s) if(!strcmp(name, # s)) \
    { \
    t->s = val->v.i; \
    return; \
    }

static void
set_parameter_general(void * data, const char * name,
                      const gavl_value_t * val)
  {
  bg_transcoder_t * t = data;

  if(!name)
    return;

  SP_STR(subdir);
  
  }

#undef SP_INT
#undef SP_STR



/* Initialize */

int bg_transcoder_init(bg_transcoder_t * ret, gavl_dictionary_t * track)
  {
  int i, idx;
  ret->transcoder_track = track;
  
  /* Set general parameter */
  
  bg_cfg_section_apply(bg_transcoder_track_get_cfg_general(track),
                       NULL,
                       set_parameter_general,
                       ret);
  
  /* Set Metadata */

  gavl_dictionary_copy(&ret->metadata, gavl_track_get_metadata(track));


  /* Open source */
  
  gavl_metadata_get_src(&ret->metadata, GAVL_META_SRC, 0, NULL, &ret->location);

  if(!(ret->in_handle = bg_input_plugin_load_full(ret->location)))
    goto fail;

  ret->src = ret->in_handle->src;

  ret->num_streams =
    bg_media_source_get_num_streams(ret->in_handle->src, GAVL_STREAM_AUDIO) +
    bg_media_source_get_num_streams(ret->in_handle->src, GAVL_STREAM_VIDEO) +
    bg_media_source_get_num_streams(ret->in_handle->src, GAVL_STREAM_TEXT) +
    bg_media_source_get_num_streams(ret->in_handle->src, GAVL_STREAM_OVERLAY);
  
  /* Create streams */
  ret->streams = calloc(ret->num_streams, sizeof(*ret->streams));

  for(i = 0; i < ret->in_handle->src.num_streams; i++)
    {
    /* Apply parameters */
    switch(ret->in_handle->src.streams[i]->type)
      {
      }
    }
  
  /* Check disabled streams */

  fail:
  return 0;
  
  }


static int init_encoder(bg_transcoder_t * ret)
  {
  int i;
  char * tmp_string;
  
  ret->cb.create_output_file = create_output_file_cb;
  ret->cb.data = ret;
  
  bg_encoder_set_callbacks(ret->enc, &ret->cb);
  
  tmp_string = gavl_sprintf("%s/%s", ret->output_directory, ret->name);
  
  bg_encoder_open(ret->enc, tmp_string,
                  &ret->metadata);
  free(tmp_string);
  
  for(i = 0; i < ret->num_audio_streams; i++)
    {
    if(ret->audio_streams[i].com.do_decode) /* If we don't encode we still need to open the
                                                plugin to get the final output format */
      add_audio_stream(&ret->audio_streams[i], ret);
    else if(ret->audio_streams[i].com.do_copy)
      add_audio_stream_compressed(&ret->audio_streams[i], ret);
    }
  
  /* Video streams: Must be added before the subtitle streams, because we need to know
     the output format (at least the output size) of the stream */
  for(i = 0; i < ret->num_video_streams; i++)
    {
    if(ret->video_streams[i].com.do_decode)/* If we don't encode we still need to open the
                                               plugin to get the final output format */
      add_video_stream(&ret->video_streams[i], ret);
    else if(ret->video_streams[i].com.do_copy)
      add_video_stream_compressed(&ret->video_streams[i], ret);
    
    }
    
  for(i = 0; i < ret->num_text_streams; i++)
    {
    if(!ret->text_streams[i].com.com.do_encode)
      continue;
    add_text_stream(&ret->text_streams[i],
                    gavl_track_get_text_stream(ret->transcoder_track, i), ret);
    }

  for(i = 0; i < ret->num_overlay_streams; i++)
    {
    if(ret->overlay_streams[i].com.do_encode)
      add_overlay_stream(&ret->overlay_streams[i],
                         gavl_track_get_overlay_stream(ret->transcoder_track, i), ret);
    else if(ret->overlay_streams[i].com.do_copy)
      add_overlay_stream_compressed(&ret->overlay_streams[i],
                                    gavl_track_get_overlay_stream(ret->transcoder_track, i), ret);
    }
  
  return bg_encoder_start(ret->enc);
  }



static void log_transcoding_time(bg_transcoder_t * t)
  {
  gavl_time_t transcoding_time;
  char time_str[GAVL_TIME_STRING_LEN];

  transcoding_time = gavl_timer_get(t->timer);
  gavl_time_prettyprint(transcoding_time, time_str);
  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Transcoding took %s (%.2f %% of realtime duration)",
         time_str,
         100.0 * gavl_time_to_seconds(transcoding_time) /
         gavl_time_to_seconds(t->duration));
  }

static const bg_parameter_info_t parameters[] =
  {
    {
      .name =      "output_path",
      .long_name = TRS("Output Directory"),
      .type =      BG_PARAMETER_DIRECTORY,
      .val_default = GAVL_VALUE_INIT_STRING("."),
    },
    {
      .name =        "delete_incomplete",
      .long_name =   TRS("Delete incomplete output files"),
      .type =        BG_PARAMETER_CHECKBUTTON,
      .val_default = GAVL_VALUE_INIT_INT(1),
      .help_string = TRS("Delete the encoded files if you hit the stop button. \
This option will automatically be disabled, when the track is an URL"),
    },
    { /* End of parameters */ }
  };

void bg_transcoder_set_parameter(void * data, const char * name, const gavl_value_t * val)
  {
  bg_transcoder_t * w = (bg_transcoder_t*)data;
  
  if(!name)
    return;

  if(!strcmp(name, "output_path"))
    {
    w->output_path = gavl_strrep(w->output_path, val->v.str);
    }
  else if(!strcmp(name, "delete_incomplete"))
    {
    w->delete_incomplete = val->v.i;
    }
  }

const bg_parameter_info_t * bg_transcoder_get_parameters()
  {
  return parameters;
  }

/* Message stuff */

typedef struct
  {
  int id;
  int num;
  } message_num_t;

static void set_message_num(gavl_msg_t * msg, const void * data)
  {
  message_num_t * num = (message_num_t *)data;
  gavl_msg_set_id_ns(msg, num->id, BG_MSG_NS_TRANSCODER);
  gavl_msg_set_arg_int(msg, 0, num->num);
  }

void bg_transcoder_send_msg_num_audio_streams(bg_msg_hub_t * l,
                                              int num)
  {
  message_num_t n;
  n.id = BG_TRANSCODER_MSG_NUM_AUDIO_STREAMS;
  n.num = num;

  bg_msg_hub_send_cb(l, set_message_num, &n);
  }

void bg_transcoder_send_msg_num_video_streams(bg_msg_hub_t * l,
                                                     int num)
  {
  message_num_t n;
  n.id = BG_TRANSCODER_MSG_NUM_VIDEO_STREAMS;
  n.num = num;
  bg_msg_hub_send_cb(l,
                         set_message_num, &n);
  }


typedef struct
  {
  int id;
  int index;
  gavl_audio_format_t * ifmt;
  gavl_audio_format_t * ofmt;
  } message_af_t;

static void set_message_audio_format(gavl_msg_t * msg, const void * data)
  {
  message_af_t * m = (message_af_t *)data;

  gavl_msg_set_id_ns(msg, m->id, BG_MSG_NS_TRANSCODER);
  gavl_msg_set_arg_int(msg, 0, m->index);
  gavl_msg_set_arg_audio_format(msg, 1, m->ifmt);
  gavl_msg_set_arg_audio_format(msg, 2, m->ofmt);
  }

void bg_transcoder_send_msg_audio_format(bg_msg_hub_t * l,
                                         int index,
                                         gavl_audio_format_t * input_format,
                                         gavl_audio_format_t * output_format)
  {
  message_af_t m;
  m.id = BG_TRANSCODER_MSG_AUDIO_FORMAT;
  m.index = index;
  m.ifmt = input_format;
  m.ofmt = output_format;
  bg_msg_hub_send_cb(l,
                         set_message_audio_format, &m);
  }

typedef struct
  {
  int id;
  int index;
  gavl_video_format_t * ifmt;
  gavl_video_format_t * ofmt;
  } message_vf_t;

static void set_message_video_format(gavl_msg_t * msg, const void * data)
  {
  message_vf_t * m = (message_vf_t *)data;

  gavl_msg_set_id(msg, m->id);
  gavl_msg_set_arg_int(msg, 0, m->index);
  gavl_msg_set_arg_video_format(msg, 1, m->ifmt);
  gavl_msg_set_arg_video_format(msg, 2, m->ofmt);
  }


void bg_transcoder_send_msg_video_format(bg_msg_hub_t * l,
                                  int index,
                                  gavl_video_format_t * input_format,
                                  gavl_video_format_t * output_format)
  {
  message_vf_t m;
  m.id = BG_TRANSCODER_MSG_VIDEO_FORMAT;
  m.index = index;
  m.ifmt = input_format;
  m.ofmt = output_format;
  bg_msg_hub_send_cb(l,
                         set_message_video_format, &m);
  }

typedef struct
  {
  const char * name;
  int pp_only;
  } message_file_t;

static void set_message_file(gavl_msg_t * msg, const void * data)
  {
  message_file_t * m = (message_file_t *)data;

  gavl_msg_set_id_ns(msg, BG_TRANSCODER_MSG_FILE, BG_MSG_NS_TRANSCODER);
  
  gavl_msg_set_arg_string(msg, 0, m->name);
  gavl_msg_set_arg_int(msg, 1, m->pp_only);
  }

void bg_transcoder_send_msg_file(bg_msg_hub_t * l,
                                 const char * filename, int pp_only)
  {
  message_file_t m;
  m.name = filename;
  m.pp_only = pp_only;
  bg_msg_hub_send_cb(l,
                         set_message_file, &m);
  
  }

typedef struct
  {
  float perc;
  gavl_time_t rem;
  } message_progress_t;

static void set_message_progress(gavl_msg_t * msg, const void * data)
  {
  message_progress_t * m = (message_progress_t *)data;
  gavl_msg_set_id_ns(msg, BG_TRANSCODER_MSG_PROGRESS, BG_MSG_NS_TRANSCODER);
  gavl_msg_set_arg_float(msg, 0, m->perc);
  gavl_msg_set_arg_long(msg, 1, m->rem);
  }

void bg_transcoder_send_msg_progress(bg_msg_hub_t * l,
                                            float percentage_done,
                                            gavl_time_t remaining_time)
  {
  message_progress_t n;
  n.perc = percentage_done;
  n.rem = remaining_time;
  bg_msg_hub_send_cb(l,
                         set_message_progress, &n);
  }

static void set_message_finished(gavl_msg_t * msg, const void * data)
  {
  gavl_msg_set_id_ns(msg, BG_TRANSCODER_MSG_FINISHED, BG_MSG_NS_TRANSCODER);
  }


void bg_transcoder_send_msg_finished(bg_msg_hub_t * l)
  {
  bg_msg_hub_send_cb(l, set_message_finished, NULL);
  }

static void set_message_start(gavl_msg_t * msg, const void * data)
  {
  gavl_msg_set_id_ns(msg, BG_TRANSCODER_MSG_START, BG_MSG_NS_TRANSCODER);
  gavl_msg_set_arg_string(msg, 0, (char*)data);
  }

void bg_transcoder_send_msg_start(bg_msg_hub_t * l, char * what)
  {
  bg_msg_hub_send_cb(l,
                         set_message_start, what);
  }

static void set_message_error(gavl_msg_t * msg, const void * data)
  {
  gavl_msg_set_id_ns(msg, BG_TRANSCODER_MSG_ERROR, BG_MSG_NS_TRANSCODER);
  }

void bg_transcoder_send_msg_error(bg_msg_hub_t * l)
  {
  bg_msg_hub_send_cb(l, set_message_error, NULL);
  }


static void set_message_metadata(gavl_msg_t * msg, const void * data)
  {
  gavl_msg_set_id_ns(msg, BG_TRANSCODER_MSG_METADATA, BG_MSG_NS_TRANSCODER);
  gavl_msg_set_arg_dictionary(msg, 0, (const gavl_dictionary_t*)data);
  }

void bg_transcoder_send_msg_metadata(bg_msg_hub_t * l,
                                     gavl_dictionary_t * m)
  {
  //  fprintf(stderr, "Encoding metadata:\n");
  //  gavl_dictionary_dump(m, 2);
  
  bg_msg_hub_send_cb(l, set_message_metadata, m);
  }

static void init_audio_stream(stream_t * ret,
                              const gavl_dictionary_t * s,
                              int in_index)
  {
  //  ret->com.st->type = STREAM_TYPE_AUDIO;
  
  gavl_dictionary_copy(&ret->m, gavl_stream_get_metadata(s));
  
  
  /* Create converter */
  
  ret->fc = bg_audio_filter_chain_create(NULL);
  
  /* Apply parameters */

  bg_cfg_section_apply(bg_transcoder_track_get_cfg_general(s), NULL,
                       set_audio_parameter_general, ret);

  bg_cfg_section_apply(bg_transcoder_track_get_cfg_filter(s),
                       NULL,
                       bg_audio_filter_chain_set_parameter, ret->fc);
  
  }


static void init_video_stream(stream_t * ret,
                              const gavl_dictionary_t * s,
                              int in_index)
  {
  gavl_dictionary_copy(&ret->m, gavl_stream_get_metadata(s));
  
  /* Create converter */

  ret->fc  = bg_video_filter_chain_create(NULL);
  
  /* Apply parameters */

  bg_cfg_section_apply(bg_transcoder_track_get_cfg_general(s), NULL,
                       set_video_parameter_general, ret);

  bg_cfg_section_apply(bg_transcoder_track_get_cfg_filter(s),
                       NULL,
                       bg_video_filter_chain_set_parameter, ret->fc);
  
  }


static void init_overlay_stream(stream_t * ret,
                                const gavl_dictionary_t * s, int idx)
  {
  gavl_dictionary_copy(&ret->m, gavl_stream_get_metadata(s));  
  /* Apply parameters */
  
  bg_cfg_section_apply(bg_transcoder_track_get_cfg_general(s), NULL,
                       set_subtitle_parameter_general, ret);
  
  }

static void init_text_stream(stream_t * ret,
                             const gavl_dictionary_t * s, int idx)
  {
  gavl_dictionary_copy(&ret->m, gavl_stream_get_metadata(s));
  /* Apply parameters */
  
  bg_cfg_section_apply(bg_transcoder_track_get_cfg_general(s),
                       NULL,
                       set_subtitle_parameter_general, ret);
  
  }


static int audio_iteration(stream_t*s, bg_transcoder_t * t)
  {
  int ret = 1;
  
  /* Get one frame worth of input data */

  if(s->asrc)
    {
    gavl_audio_frame_t * f = NULL;
    
    }
  else
    {
    
    }
  
  if(s->com.do_copy)
    {
    gavl_packet_t * pp = &s->com.packet;
    
    if(gavl_packet_source_read_packet(s->com.psrc, &pp) != GAVL_SOURCE_OK)
      {
      /* EOF */
      s->com.status = STREAM_STATE_FINISHED;
      return ret;
      }

#ifdef DUMP_AUDIO_TIMESTAMPS
      bg_debug("Audio packet timestamp: %"PRId64" dur: %"PRId64"\n",
               pp->pts, pp->duration);
#endif
    
    ret = (gavl_packet_sink_put_packet(s->com.psink, &s->com.packet) == GAVL_SINK_OK);

    s->samples_read += s->com.packet.duration;
    
    s->com.time = gavl_samples_to_time(s->in_format.samplerate,
                                       s->samples_read);
    }
  else
    {
    gavl_source_status_t st;
    gavl_audio_frame_t * frame = NULL;

    if((st = gavl_audio_source_read_frame(s->asrc, &frame)) != GAVL_SOURCE_OK)
      {
      s->com.status = STREAM_STATE_FINISHED;
      return ret;
      }
       
    /* Update sample counter before the frame is set to NULL
       after peak-detection */

    s->samples_read += frame->valid_samples;
 
    
    if(frame)
      {
#ifdef DUMP_AUDIO_TIMESTAMPS
      bg_debug("Output timestamp (audio): %"PRId64"\n",
               frame->timestamp);
#endif

      ret = (gavl_audio_sink_put_frame(s->asink, frame) == GAVL_SINK_OK);
      }

    s->com.time = gavl_samples_to_time(s->out_format.samplerate,
                                       s->samples_read);
    }
  
  /* Last samples */
  
  
  if(!ret)
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Encoding audio failed");
  return ret;
  }


static void correct_subtitle_timestamp(subtitle_stream_t * s,
                                       int64_t * start,
                                       int64_t * duration,
                                       bg_transcoder_t * t)
  {
  /* Add offset */
  *start += gavl_time_scale(s->in_format.timescale, s->time_offset);
  
  /* Correct timestamps */
  
  /* Rescale */
  *start = gavl_time_rescale(s->in_format.timescale, s->out_format.timescale,
                             *start);
  *duration = gavl_time_rescale(s->in_format.timescale, s->out_format.timescale,
                                *duration);
  s->subtitle_start_unscaled = gavl_time_unscale(s->out_format.timescale,
                                                 *start);
  }

static gavl_source_status_t
decode_overlay(subtitle_stream_t * s, bg_transcoder_t * t, gavl_overlay_t ** ovl)
  {
  gavl_source_status_t st;
  st = gavl_video_source_read_frame(s->vsrc, ovl);

  if(st != GAVL_SOURCE_OK)
    {
    if(st == GAVL_SOURCE_EOF)
      s->com.status = STREAM_STATE_FINISHED;
    return st;
    }
  correct_subtitle_timestamp(s, &((*ovl)->timestamp), &((*ovl)->duration), t);
  return st;
  }

static gavl_source_status_t
read_overlay_packet(subtitle_stream_t * s, bg_transcoder_t * t)
  {
  gavl_source_status_t st;
  gavl_packet_t * pp = &s->com.packet;
  
  if(s->eof)
    return GAVL_SOURCE_EOF;
  
  st = gavl_packet_source_read_packet(s->com.psrc, &pp);

  if(st == GAVL_SOURCE_EOF)
    s->eof = 1;
  
  if(st != GAVL_SOURCE_OK)
    return st;
  
  correct_subtitle_timestamp(s, &s->com.packet.pts,
                             &s->com.packet.duration, t);
  
  return GAVL_SOURCE_OK;
  }


static gavl_source_status_t
decode_text(text_stream_t * s, bg_transcoder_t * t)
  {
  gavl_source_status_t st;
  gavl_packet_t * pp = &s->com.com.packet;
  
  if(s->com.eof)
    return GAVL_SOURCE_EOF;
  
  st = gavl_packet_source_read_packet(s->com.com.psrc, &pp);

  if(st == GAVL_SOURCE_EOF)
    s->com.eof = 1;
  
  if(st != GAVL_SOURCE_OK)
    return st;

  
  correct_subtitle_timestamp(&s->com, &s->com.com.packet.pts,
                             &s->com.com.packet.duration, t);
  
  return GAVL_SOURCE_OK;
  }


static int video_iteration(video_stream_t * s, bg_transcoder_t * t)
  {
  int ret = 1;
  
  if(s->com.do_copy)
    {
    gavl_packet_t * p = NULL;
    if(gavl_packet_source_read_packet(s->com.psrc, &p) != GAVL_SOURCE_OK)
      {
      /* EOF */
      s->com.status = STREAM_STATE_FINISHED;
      return ret;
      }
    
    if((p->flags & GAVL_PACKET_TYPE_MASK) != GAVL_PACKET_TYPE_B)
      {
      s->com.time = gavl_time_unscale(s->in_format.timescale,
                                      s->com.packet.pts + s->com.packet.duration);
      }
    ret = (gavl_packet_sink_put_packet(s->com.psink, &s->com.packet) == GAVL_SINK_OK);
    }
  else
    {
    gavl_source_status_t st;
    gavl_video_frame_t * frame = NULL;

    if((st = gavl_video_source_read_frame(s->vsrc, &frame)) != GAVL_SOURCE_OK)
      {
      s->com.status = STREAM_STATE_FINISHED;
      return ret;
      }

    s->com.time = gavl_time_unscale(s->out_format.timescale,
                                    frame->timestamp + frame->duration);


#ifdef DUMP_VIDEO_TIMESTAMPS
    bg_debug("Output timestamp (video): %"PRId64"\n", frame->timestamp);
#endif
    ret = (gavl_video_sink_put_frame(s->vsink, frame) == GAVL_SINK_OK);
    }
  
  
  if(!ret)
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Encoding video failed");
  
  s->frames_written++;
  return ret;
  }

/* Time offset of 0.5 seconds means, that we encode subtitles maximum
   0.5 seconds before the subtitle should appear. This is only interesting
   for formats, which don't allow random access to subtitles */

#define SUBTITLE_TIME_OFFSET (GAVL_TIME_SCALE/2)

static int do_write_subtitle(gavl_time_t time_unscaled,
                             video_stream_t * vs)
  {
  if(!vs->com.do_encode && !vs->com.do_copy)
    return 1;
  if(vs->com.status == STREAM_STATE_FINISHED)
    return 1;
  if(time_unscaled - vs->com.time < SUBTITLE_TIME_OFFSET)
    return 1;
  return 0;
  }

static int subtitle_iteration(bg_transcoder_t * t)
  {
  int i, ret = 1;
  text_stream_t * ts;
  subtitle_stream_t      * ss;
  video_stream_t         * vs;
  gavl_source_status_t st;
  
  for(i = 0; i < t->num_text_streams; i++)
    {
    ts = &t->text_streams[i];

    if(!ts->com.com.do_encode)
      continue;
    
    if(ts->com.eof)
      {
      ts->com.com.status = STREAM_STATE_FINISHED;
      continue;
      }
    
    /* Check for decoding */
    if(!ts->com.has_current)
      {
      if(ts->com.com.action == BG_STREAM_ACTION_DECODE)
        {
        st = decode_text(ts, t);
        if(st == GAVL_SOURCE_OK)
          ts->com.has_current = 1;
        else if(st == GAVL_SOURCE_EOF)
          ts->com.eof = 1;
        }
      }
    
    /* Check for encoding */
    if(ts->com.has_current)
      {
      vs = &t->video_streams[0];
      
      if(do_write_subtitle(ts->com.subtitle_start_unscaled, vs))
        {
#if 0
        fprintf(stderr, "Encode subtitle %"PRId64" %"PRId64" %"PRId64"\n",
                ts->com.subtitle_start_unscaled,
                vs->com.time,
                ts->com.subtitle_start_unscaled -
                vs->com.time);
#endif
        if(ts->com.com.action == BG_STREAM_ACTION_DECODE)
          {
          ret = (gavl_packet_sink_put_packet(ts->com.com.psink,
                                             &ts->com.com.packet) == GAVL_SINK_OK);
          
          ts->com.com.time = gavl_time_unscale(ts->com.out_format.timescale,
                                               ts->com.com.packet.pts);
          
          if(ts->com.com.time > t->time)
            t->time = ts->com.com.time;
          }
        ts->com.has_current = 0;
        }
      }
    if(!ret)
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Encoding subtitles failed");
    }
  
  if(!ret)
    return ret;
  
  for(i = 0; i < t->num_overlay_streams; i++)
    {
    ss = &t->overlay_streams[i];

    if(!ss->com.do_copy && !ss->com.do_encode)
      continue;
    
    if(ss->eof)
      {
      ss->com.status = STREAM_STATE_FINISHED;
      continue;
      }

    /* Transcode */    
    if(ss->com.do_encode)
      {
      /* Check for decoding */
      if(!ss->has_current)
        ss->has_current = (decode_overlay(ss, t, &ss->ovl) == GAVL_SOURCE_OK);
      
      /* Check for encoding */
      if(ss->has_current)
        {
        vs = &t->video_streams[0];

        if(do_write_subtitle(ss->subtitle_start_unscaled, vs))
          {
          // fprintf(stderr, "Write overlay %ld %ld\n", ss->subtitle_start_unscaled, ss->ovl->timestamp);
          ret = (gavl_video_sink_put_frame(ss->vsink, ss->ovl) == GAVL_SINK_OK);
          ss->com.time = gavl_time_unscale(ss->out_format.timescale,
                                           ss->ovl->timestamp);
          
          if(ss->com.time > t->time)
            t->time = ss->com.time;
          ss->has_current = 0;
          }
        }
      
      }
    else // Copy
      {
      /* Check for reading */
      if(!ss->has_current)
        ss->has_current = (read_overlay_packet(ss, t) == GAVL_SOURCE_OK);
      
      if(ss->has_current)
        {
        vs = &t->video_streams[0];

        if(do_write_subtitle(ss->subtitle_start_unscaled, vs))
          {
          fprintf(stderr, "Write overlay packet\n");
          gavl_packet_dump(&ss->com.packet);
            
          ret = (gavl_packet_sink_put_packet(ss->com.psink,
                                             &ss->com.packet) == GAVL_SINK_OK);
          ss->com.time = gavl_time_unscale(ss->out_format.timescale,
                                           ss->com.packet.pts);
          
          if(ss->com.time > t->time)
            t->time = ss->com.time;
          ss->has_current = 0;
          }
        }
      
      }
    

    if(!ret)
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Encoding subtitles failed");
      break;
      }
    }
  return ret;
  }

 
void bg_transcoder_add_message_sink(bg_transcoder_t * t, bg_msg_sink_t * sink)
  {
  bg_msg_hub_connect_sink(t->message_hub, sink);
  }


bg_transcoder_t * bg_transcoder_create()
  {
  bg_transcoder_t * ret;
  ret = calloc(1, sizeof(*ret));
  ret->timer = gavl_timer_create();
  ret->message_hub = bg_msg_hub_create(1);
  pthread_mutex_init(&ret->stop_mutex,  NULL);
  return ret;
  }

static void send_init_messages(bg_transcoder_t * t)
  {
  int i;
  char * tmp_string;

  if(t->pp_only)
    {
    tmp_string = gavl_sprintf(TR("Postprocessing %s"), t->location);
    }
  else
    {
    tmp_string = gavl_sprintf(TR("Transcoding %s"),
                            t->location);
    }
  bg_transcoder_send_msg_start(t->message_hub, tmp_string);
  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "%s", tmp_string);
  free(tmp_string);
    
  bg_transcoder_send_msg_metadata(t->message_hub, &t->metadata);

#if 0  
  tmp_string = bg_metadata_to_string(&t->metadata, 1);
  if(tmp_string)
    {
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Metadata:\n%s", tmp_string);
    free(tmp_string);
    }
#endif
  
  if(!t->pp_only)
    {
    bg_transcoder_send_msg_num_audio_streams(t->message_hub, t->num_audio_streams);
    for(i = 0; i < t->num_audio_streams; i++)
      {
      if(t->audio_streams[i].com.do_decode)
        {
        bg_transcoder_send_msg_audio_format(t->message_hub, i,
                                            &t->audio_streams[i].in_format,
                                            &t->audio_streams[i].out_format);
      
        tmp_string = bg_audio_format_to_string(&t->audio_streams[i].in_format,
                                               1);
        gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Audio stream %d input format:\n%s",
               i+1, tmp_string);
        free(tmp_string);

        tmp_string = bg_audio_format_to_string(&t->audio_streams[i].out_format,
                                               1);
        gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Audio stream %d output format:\n%s",
               i+1, tmp_string);
        free(tmp_string);
        }
      else if(t->audio_streams[i].com.do_copy)
        {
        tmp_string = bg_audio_format_to_string(&t->audio_streams[i].in_format,
                                               0);
        gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Audio stream %d format:\n%s",
               i+1, tmp_string);
        free(tmp_string);
        }
      }
    bg_transcoder_send_msg_num_video_streams(t->message_hub, t->num_video_streams);
    for(i = 0; i < t->num_video_streams; i++)
      {
      if(t->video_streams[i].com.do_decode)
        {
        bg_transcoder_send_msg_video_format(t->message_hub, i,
                                            &t->video_streams[i].in_format,
                                            &t->video_streams[i].out_format);
        tmp_string = bg_video_format_to_string(&t->video_streams[i].in_format,
                                               0);
        gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Video stream %d input format:\n%s",
               i+1, tmp_string);
        free(tmp_string);

        tmp_string = bg_video_format_to_string(&t->video_streams[i].out_format, 0);
        gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Video stream %d output format:\n%s",
               i+1, tmp_string);
        free(tmp_string);
        }
      else if(t->video_streams[i].com.do_copy)
        {
        tmp_string = bg_video_format_to_string(&t->video_streams[i].in_format, 0);
        gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Video stream %d format:\n%s",
               i+1, tmp_string);
        free(tmp_string);
        }
      }
    for(i = 0; i < t->num_text_streams; i++)
      {
      if(t->text_streams[i].com.com.do_decode)
        {
        switch(t->text_streams[i].com.com.action)
          {
          case BG_STREAM_ACTION_DECODE:
            gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Text subtitle stream %d: Exporting to file", i+1);
            break;
          }
        }
      
      }

    for(i = 0; i < t->num_overlay_streams; i++)
      {
      if(t->overlay_streams[i].com.do_decode)
        {
        switch(t->overlay_streams[i].com.action)
          {
          case BG_STREAM_ACTION_DECODE:
            gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Overlay subtitle stream %d: Exporting to file", i+1);
            break;
          }
        }
      
      }


    }
  }

static void send_file_messages(bg_transcoder_t * t)
  {
  int i;

  for(i = 0; i < t->num_output_files; i++)
    {
    bg_transcoder_send_msg_file(t->message_hub,
                                t->output_files[i],
                                t->pp_only);
    }
  }

static int open_input(bg_transcoder_t * ret)
  {
  int track_index;
  
  if(!(ret->in_handle = bg_input_plugin_load_full(ret->location)))
    goto fail;
  
  ret->in_plugin = (bg_input_plugin_t*)ret->in_handle->plugin;

  track_index = bg_url_get_track(ret->location);
  ret->track_info = bg_input_plugin_get_track_info(ret->in_handle, track_index);
  
  
  
  return 1;
  fail:
  return 0;
  
  }

static void check_compressed(bg_transcoder_t * ret)
  {
  int i;
  
  for(i = 0; i < ret->num_audio_streams; i++)
    {
    if(!(ret->audio_streams[i].com.action == BG_STREAM_ACTION_READRAW))
      continue;

    /* Check if we can read compressed data */

    

    if(!gavl_track_get_audio_compression_info(ret->track_info, i,
                                             &ret->audio_streams[i].com.ci))
      {
      gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Audio stream %d cannot be read compressed", i+1);
      ret->audio_streams[i].com.action = BG_STREAM_ACTION_DECODE;
      continue;
      }
    
    /* Check if we can write compressed data */
    if(!bg_encoder_writes_compressed_audio(ret->enc,
                                           gavl_track_get_audio_format(ret->track_info, i),
                                           &ret->audio_streams[i].com.ci))
      {
      gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Audio stream %d cannot be written compressed", i+1);
      ret->audio_streams[i].com.action = BG_STREAM_ACTION_DECODE;
      //      gavl_compression_info_dump(&ret->audio_streams[i].com.ci);
      continue;
      }

    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Copying compressed audio stream %d", i+1);
    //    bg_dprintf("Copying compressed audio stream %d\n", i+1);
    //    gavl_compression_info_dump(&ret->audio_streams[i].com.ci);
    }
  for(i = 0; i < ret->num_video_streams; i++)
    {
    if(ret->video_streams[i].com.action != BG_STREAM_ACTION_READRAW)
      continue;
    
    /* Check if we can read compressed data */
    if(!gavl_track_get_video_compression_info(ret->track_info, i,
                                             &ret->video_streams[i].com.ci))
      {
      gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Video stream %d cannot be read compressed", i+1);
      ret->video_streams[i].com.action = BG_STREAM_ACTION_DECODE;
      continue;
      }
    
    /* Check if we can write compressed data */
    if(!bg_encoder_writes_compressed_video(ret->enc,
                                           gavl_track_get_video_format(ret->track_info, i),
                                           &ret->video_streams[i].com.ci))
      {
      gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Video stream %d cannot be written compressed", i+1);
      ret->video_streams[i].com.action = BG_STREAM_ACTION_DECODE;
      continue;
      }

    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Copying compressed video stream %d", i+1);
    //    bg_dprintf("Copying compressed video stream %d\n", i+1);
    //    gavl_compression_info_dump(&ret->video_streams[i].com.ci);
    }

  /* Overlay subtitles */
  for(i = 0; i < ret->num_overlay_streams; i++)
    {
    if(!(ret->overlay_streams[i].com.action == BG_STREAM_ACTION_READRAW))
      continue;

    /* Check if we can read compressed data */
    if(!gavl_track_get_overlay_compression_info(ret->track_info, i,
                                                &ret->overlay_streams[i].com.ci))
      {
      gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Overlay stream %d cannot be read compressed", i+1);
      ret->overlay_streams[i].com.action = BG_STREAM_ACTION_DECODE;
      continue;
      }
    
    /* Check if we can write compressed data */
    if(!bg_encoder_writes_compressed_overlay(ret->enc,
                                           gavl_track_get_overlay_format(ret->track_info, i),
                                           &ret->overlay_streams[i].com.ci))
      {
      gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Overlay stream %d cannot be written compressed", i+1);
      ret->overlay_streams[i].com.action = BG_STREAM_ACTION_DECODE;
      continue;
      }

    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Copying compressed overlay stream %d", i+1);
    //    bg_dprintf("Copying compressed overlay stream %d\n", i+1);
    //    gavl_compression_info_dump(&ret->overlay_streams[i].com.ci);
    }
  
  
  }

static void create_streams(bg_transcoder_t * ret,
                           const gavl_dictionary_t * track)
  {
  int i;
  /* Allocate streams */
  
  ret->num_audio_streams = gavl_track_get_num_audio_streams(track);
  if(ret->num_audio_streams)
    ret->audio_streams = calloc(ret->num_audio_streams,
                                sizeof(*ret->audio_streams));
  
  ret->num_video_streams = gavl_track_get_num_video_streams(track);
  
  if(ret->num_video_streams)
    ret->video_streams = calloc(ret->num_video_streams,
                                sizeof(*ret->video_streams));

  ret->num_text_streams = gavl_track_get_num_text_streams(track);;

  if(ret->num_text_streams)
    ret->text_streams = calloc(ret->num_text_streams,
                                sizeof(*ret->text_streams));

  ret->num_overlay_streams = gavl_track_get_num_overlay_streams(track);

  if(ret->num_overlay_streams)
    ret->overlay_streams = calloc(ret->num_overlay_streams,
                                           sizeof(*ret->overlay_streams));
  
  /* Prepare streams */
    
  ret->num_audio_streams_real = 0;
  ret->num_video_streams_real = 0;
  ret->num_text_streams_real = 0;
  ret->num_overlay_streams_real = 0;
    
  for(i = 0; i < ret->num_audio_streams; i++)
    {
    ret->audio_streams[i].com.t = ret;
    ret->audio_streams[i].com.st = bg_media_source_get_audio_stream(ret->src, i);
    
    init_audio_stream(&ret->audio_streams[i],
                      gavl_track_get_audio_stream(track, i),
                      i);
    if(ret->audio_streams[i].com.action == BG_STREAM_ACTION_DECODE)
      ret->num_audio_streams_real++;
    }

  
  for(i = 0; i < ret->num_video_streams; i++)
    {
    ret->video_streams[i].com.t = ret;
    ret->audio_streams[i].com.st = bg_media_source_get_video_stream(ret->src, i);

    init_video_stream(&ret->video_streams[i],
                      gavl_track_get_video_stream(track, i),
                      i);
    if(ret->video_streams[i].com.action == BG_STREAM_ACTION_DECODE)
      ret->num_video_streams_real++;
    }

  
  for(i = 0; i < ret->num_text_streams; i++)
    {
    ret->text_streams[i].com.com.t = ret;
    init_text_stream(&ret->text_streams[i],
                     gavl_track_get_text_stream(track, i),
                     i);

    if(ret->text_streams[i].com.com.action == BG_STREAM_ACTION_DECODE)
      ret->num_text_streams_real++;
    }
  
  for(i = 0; i < ret->num_overlay_streams; i++)
    {
    ret->overlay_streams[i].com.t = ret;
    init_overlay_stream(&ret->overlay_streams[i],
                        gavl_track_get_overlay_stream(track, i),
                        i);
    if(ret->overlay_streams[i].com.action == BG_STREAM_ACTION_DECODE)
      ret->num_overlay_streams_real++;
    }
  }

static int start_input(bg_transcoder_t * ret)
  {
#if 0
  int i;
  for(i = 0; i < ret->num_audio_streams; i++)
    {
    if(ret->audio_streams[i].com.do_decode || ret->audio_streams[i].com.do_copy)
      {
      start_audio_stream_i(&ret->audio_streams[i],
                           ret->in_handle);
      }
    }
  for(i = 0; i < ret->num_video_streams; i++)
    {
    if(ret->video_streams[i].com.do_decode || ret->video_streams[i].com.do_copy)
      {
      start_video_stream_i(&ret->video_streams[i],
                           ret->in_handle);
      }
    }
  for(i = 0; i < ret->num_text_streams; i++)
    {
    if(ret->text_streams[i].com.com.do_decode)
      {
      start_text_stream_i((subtitle_stream_t*)(&ret->text_streams[i]),
                              ret->in_handle);
      }
    }
  for(i = 0; i < ret->num_overlay_streams; i++)
    {
    if(ret->overlay_streams[i].com.do_decode ||
       ret->overlay_streams[i].com.do_copy)
      {
      start_overlay_stream_i(&ret->overlay_streams[i],
                             ret->in_handle);
      }
    }
#endif  
  bg_input_plugin_start(ret->in_handle);
  

  return 1;
  }

#if 0
static int setup_pass(bg_transcoder_t * ret)
  {
  int i;
  int result = 0;
  
  for(i = 0; i < ret->num_audio_streams; i++)
    {
    /* Reset the samples already decoded */
    ret->audio_streams[i].samples_read = 0;

    if(ret->audio_streams[i].com.action == BG_STREAM_ACTION_READRAW)
      {
      if(ret->pass == ret->total_passes)
        {
        ret->audio_streams[i].com.do_copy = 1;
        result = 1;
        }
      else
        ret->audio_streams[i].com.do_copy = 0;
      }
    else if(ret->audio_streams[i].com.action != BG_STREAM_ACTION_DECODE)
      {
      ret->audio_streams[i].com.do_decode = 0;
      ret->audio_streams[i].com.do_encode = 0;
      }
    else if(ret->pass == ret->total_passes)
      {
      ret->audio_streams[i].com.do_decode = 1;
      ret->audio_streams[i].com.do_encode = 1;
      result = 1;
      }
    else if((ret->pass == 1) && (ret->audio_streams[i].normalize))
      {
      ret->audio_streams[i].com.do_decode = 1;
      ret->audio_streams[i].com.do_encode = 0;
      result = 1;
      }
    else
      {
      ret->audio_streams[i].com.do_decode = 0;
      ret->audio_streams[i].com.do_encode = 0;
      }
    if(ret->audio_streams[i].com.do_decode ||
       ret->audio_streams[i].com.do_copy)
      ret->audio_streams[i].com.status = STREAM_STATE_ON;
    else
      ret->audio_streams[i].com.status = STREAM_STATE_OFF;

    }
  
  for(i = 0; i < ret->num_video_streams; i++)
    {
    if(ret->video_streams[i].com.action == BG_STREAM_ACTION_READRAW)
      {
      if(ret->pass == ret->total_passes)
        {
        ret->video_streams[i].com.do_copy = 1;
        result = 1;
        }
      else
        ret->video_streams[i].com.do_copy = 0;
      }
    else if(ret->video_streams[i].com.action != BG_STREAM_ACTION_DECODE)
      {
      ret->video_streams[i].com.do_decode = 0;
      ret->video_streams[i].com.do_encode = 0;
      }
    else
      {
      ret->video_streams[i].com.do_decode = 0;
      ret->video_streams[i].com.do_encode = 0;
      }

    if(ret->video_streams[i].com.do_decode ||
       ret->video_streams[i].com.do_copy)
      ret->video_streams[i].com.status = STREAM_STATE_ON;
    else
      ret->video_streams[i].com.status = STREAM_STATE_OFF;

    }
  
  /* Subtitles */

  for(i = 0; i < ret->num_text_streams; i++)
    {
    switch(ret->text_streams[i].com.com.action)
      {
      case BG_STREAM_ACTION_OFF:
        ret->text_streams[i].com.com.do_decode = 0;
        ret->text_streams[i].com.com.do_encode = 0;
        break;
      case BG_STREAM_ACTION_DECODE:
        if(ret->pass == ret->total_passes)
          {
          ret->text_streams[i].com.com.do_decode = 1;
          ret->text_streams[i].com.com.do_encode = 1;
          result = 1;
          }
        else
          {
          ret->text_streams[i].com.com.do_decode = 0;
          ret->text_streams[i].com.com.do_encode = 0;
          }
        break;
      default:
        gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Subtitle stream cannot be handled");
        ret->text_streams[i].com.com.do_decode = 0;
        ret->text_streams[i].com.com.do_encode = 0;
        ret->text_streams[i].com.com.action = BG_STREAM_ACTION_OFF;
        break;
      }
    
    if(!ret->text_streams[i].com.com.do_decode)
      ret->text_streams[i].com.com.status = STREAM_STATE_OFF;
    else
      ret->text_streams[i].com.com.status = STREAM_STATE_ON;
    }

  for(i = 0; i < ret->num_overlay_streams; i++)
    {
    switch(ret->overlay_streams[i].com.action)
      {
      case BG_STREAM_ACTION_OFF:
        ret->overlay_streams[i].com.do_decode = 0;
        ret->overlay_streams[i].com.do_encode = 0;
        break;
      case BG_STREAM_ACTION_DECODE:
        if(ret->pass == ret->total_passes)
          {
          ret->overlay_streams[i].com.do_decode = 1;
          ret->overlay_streams[i].com.do_encode = 1;
          result = 1;
          }
        else
          {
          ret->overlay_streams[i].com.do_decode = 0;
          ret->overlay_streams[i].com.do_encode = 0;
          }
        break;
      case BG_STREAM_ACTION_READRAW:
        if(ret->pass == ret->total_passes)
          {
          ret->overlay_streams[i].com.do_copy = 1;
          result = 1;
          }
        else
          {
          ret->overlay_streams[i].com.do_decode = 0;
          ret->overlay_streams[i].com.do_encode = 0;
          ret->overlay_streams[i].com.do_copy = 0;
          }
        break;
      default:
        gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Subtitle stream cannot be handled");
        ret->overlay_streams[i].com.do_decode = 0;
        ret->overlay_streams[i].com.do_encode = 0;
        ret->overlay_streams[i].com.action = BG_STREAM_ACTION_OFF;
        break;
      }
    
    if(!ret->overlay_streams[i].com.do_decode)
      ret->overlay_streams[i].com.status = STREAM_STATE_OFF;
    else
      ret->overlay_streams[i].com.status = STREAM_STATE_ON;
    }
  return result;
  }
#endif

static int create_output_file_cb(void * priv, const char * filename)
  {
  bg_transcoder_t * t = priv;

  if(!strcmp(filename, t->location))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Input and output are the same file");
    return 0;
    }
  
  add_output_file(t, filename);
  
  return 1;
  }

static void create_encoder(bg_transcoder_t * ret)
  {
  ret->enc = bg_encoder_create(ret->transcoder_track,
                               GAVL_STREAM_AUDIO |
                               GAVL_STREAM_VIDEO |
                               GAVL_STREAM_TEXT |
                               GAVL_STREAM_OVERLAY,
                               BG_PLUGIN_FILE);
  }


static int init_audio_converter(audio_stream_t * ret, bg_transcoder_t * t)
  {
  bg_encoder_get_audio_format(t->enc,
                              ret->com.out_index,
                              &ret->out_format);

  gavl_audio_source_set_dst(ret->asrc, 0, &ret->out_format);

  return 1;
  }


static int init_video_converter(video_stream_t * ret, bg_transcoder_t * t)
  {
  bg_encoder_get_video_format(t->enc,
                              ret->com.out_index, &ret->out_format);
  
  /* Initialize converter */

  gavl_video_source_set_dst(ret->vsrc, 0, &ret->out_format);
  
  return 1;
  }


static void subtitle_init_encode_text(text_stream_t * ss,
                                      bg_transcoder_t * t)
  {
  /* Nothing to do here for now */
  bg_encoder_get_text_timescale(t->enc,
                                ss->com.com.out_index,
                                &ss->com.out_format.timescale);
  ss->com.com.psink = bg_encoder_get_text_sink(t->enc, ss->com.com.out_index);
  }

static void subtitle_init_copy_overlay(subtitle_stream_t * ss,
                                       bg_transcoder_t * t)
  {
  ss->com.psink = bg_encoder_get_overlay_packet_sink(t->enc, ss->com.out_index);
  }

static void subtitle_init_encode_overlay(subtitle_stream_t * ss,
                                         bg_transcoder_t * t)
  {
  bg_encoder_get_overlay_format(t->enc, ss->com.out_index, &ss->out_format);
  
  gavl_video_source_set_dst(ss->vsrc, 0, &ss->out_format);

  /* Prevent rescaling since this will done by the source */
  ss->in_format.timescale = ss->out_format.timescale;
  
  fprintf(stderr, "Input format:\n");
  gavl_video_format_dump(&ss->in_format);
  fprintf(stderr, "Output format:\n");
  gavl_video_format_dump(&ss->out_format);

  
  
  ss->vsink = bg_encoder_get_overlay_sink(t->enc, ss->com.out_index);
  }

static int init_converters(bg_transcoder_t * ret)
  {
  int i;
  for(i = 0; i < ret->num_audio_streams; i++)
    {
    audio_stream_t * as = ret->audio_streams + i;
    if(as->com.do_copy)
      {
      gavl_audio_format_copy(&as->out_format, &as->in_format);
      as->com.psink = bg_encoder_get_audio_packet_sink(ret->enc, as->com.out_index);
      continue;
      }
    if(!as->com.do_decode)
      continue;
    if(!init_audio_converter(as, ret))
      return 0;
    as->asink = bg_encoder_get_audio_sink(ret->enc, as->com.out_index);
    }

  for(i = 0; i < ret->num_video_streams; i++)
    {
    video_stream_t * vs = ret->video_streams + i;
    if(vs->com.do_copy)
      {
      gavl_video_format_copy(&vs->out_format, &vs->in_format);
      vs->com.psink = bg_encoder_get_video_packet_sink(ret->enc, vs->com.out_index);
      continue;
      }
    
    if(!vs->com.do_decode)
      continue;

    if(!init_video_converter(vs, ret))
      return 0;
 
    vs->vsink = bg_encoder_get_video_sink(ret->enc, vs->com.out_index);    

    }

  for(i = 0; i < ret->num_text_streams; i++)
    {
    if(!ret->text_streams[i].com.com.do_decode)
      continue;

    switch(ret->text_streams[i].com.com.action)
      {
      case BG_STREAM_ACTION_DECODE:
        subtitle_init_encode_text((&ret->text_streams[i]), ret);
        break;
      }
    }

  for(i = 0; i < ret->num_overlay_streams; i++)
    {
    subtitle_stream_t * os = ret->overlay_streams + i;
    if(!os->com.do_decode && !os->com.do_copy)
      continue;

    switch(os->com.action)
      {
      case BG_STREAM_ACTION_DECODE:
        subtitle_init_encode_overlay(os, ret);
        break;
      case BG_STREAM_ACTION_READRAW:
        subtitle_init_copy_overlay(os, ret);
        break;
      }
    }
  return 1;
  }


int bg_transcoder_init(bg_transcoder_t * ret,
                       bg_transcoder_track_t * track)
  {
  const char * var1;
  const char * var2;
  gavl_time_t duration;
  ret->transcoder_track = track;
  
  /* Initialize encoder info */
  
  
  /* Set general parameter */
  
  bg_cfg_section_apply(bg_transcoder_track_get_cfg_general(track),
                       NULL,
                       set_parameter_general,
                       ret);

  /* Set Metadata */

  gavl_dictionary_copy(&ret->metadata, gavl_track_get_metadata(track));

  /* Hack some fields right (TODO: Maybe do this generically */
  if((var1 = gavl_dictionary_get_string(&ret->metadata, GAVL_META_YEAR)) &&
     (!(var2 = gavl_dictionary_get_string(&ret->metadata, GAVL_META_DATE)) ||
      strncmp(var1, var2, 4)))
    gavl_dictionary_set_string_nocopy(&ret->metadata, GAVL_META_DATE,
                                      gavl_sprintf("%s-99-99", var1));
  
  duration = gavl_track_get_duration(track);

  if(duration != GAVL_TIME_UNDEFINED)
    gavl_dictionary_set_long(&ret->metadata, GAVL_META_APPROX_DURATION, duration);
  
  ret->name = gavl_strrep(ret->name, bg_transcoder_track_get_name(track));

  gavl_metadata_get_src(&ret->metadata, GAVL_META_SRC, 0, NULL, &ret->location);
    
  /* Open input plugin */
  if(!open_input(ret))
    goto fail;

  create_encoder(ret);
  
  create_streams(ret, track);

  check_compressed(ret);
  
  /* Check how many passes we must do */
  //  check_passes(ret);
  //  ret->pass = 1;

  /* Set up this pass */
#if 0
  if(!setup_pass(ret))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "No stream to encode");
    goto fail;
    }
#endif
  /* Start input plugin */
  
  if(!start_input(ret))
    goto fail;

  if(!set_input_formats(ret))
    goto fail;
  
  /* Set up the streams in the encoders */
  if(!init_encoder(ret))
    goto fail;
  
  /* Set formats */
  if(!init_converters(ret))
    goto fail;
  
  /* Send messages */
  send_init_messages(ret);  
  
  /* Set duration */

  ret->duration = gavl_track_get_duration(ret->track_info);
  
  /* Start timer */

  gavl_timer_start(ret->timer);
  
  ret->state = TRANSCODER_STATE_RUNNING;
  
  return 1;

  fail:


  return 0;
  
  }

#define FREE_STR(str) if(str)free(str);

static void cleanup_stream(stream_t * s)
  {
  gavl_compression_info_free(&s->ci);
  gavl_packet_free(&s->packet);
  gavl_dictionary_free(&s->m);
  }

static void cleanup_audio_stream(audio_stream_t * s)
  {
  cleanup_stream(&s->com);
  
  /* Free all resources */

  if(s->fc)
    bg_audio_filter_chain_destroy(s->fc);
  
  bg_gavl_audio_options_free(&s->options);
  }

static void cleanup_video_stream(video_stream_t * s)
  {
  cleanup_stream(&s->com);

  /* Free all resources */

  if(s->fc)
    bg_video_filter_chain_destroy(s->fc);
  
  bg_gavl_video_options_free(&s->options);
  }

static void cleanup_subtitle_stream(subtitle_stream_t * s)
  {
  cleanup_stream(&s->com);
  }


static void close_input(bg_transcoder_t * t)
  {
  if(t->pp_only)
    return;
  
  if(t->in_plugin)
    {
    if(t->in_plugin->stop)
      t->in_plugin->stop(t->in_handle->priv);
    t->in_plugin->close(t->in_handle->priv);
    }
  if(t->in_handle)
    bg_plugin_unref(t->in_handle);
  t->in_handle = NULL;
  }


/*
 *  Do one iteration.
 *  If return value is FALSE, we are done
 */

int bg_transcoder_iteration(bg_transcoder_t * t)
  {
  int i;
  gavl_time_t time;
  stream_t * stream = NULL;

  gavl_time_t real_time;
  double real_seconds;

  double remaining_seconds;
  
  int done = 1;

  time = GAVL_TIME_MAX;
  
  /* Find the stream with the smallest time */
  
  for(i = 0; i < t->num_audio_streams; i++)
    {
    /* Check for the most urgent audio/video stream */
    if(t->audio_streams[i].com.status != STREAM_STATE_ON)
      continue;
    done = 0;
    if(t->audio_streams[i].com.time < time)
      {
      time = t->audio_streams[i].com.time;
      stream = &t->audio_streams[i].com;
      }
    }
  for(i = 0; i < t->num_video_streams; i++)
    {
    if(t->video_streams[i].com.status != STREAM_STATE_ON)
      continue;
    done = 0;
    
    if(t->video_streams[i].com.time < time)
      {
      time = t->video_streams[i].com.time;
      stream = &t->video_streams[i].com;
      }
    }
  
  if(done)
    {
    t->state = TRANSCODER_STATE_FINISHED;
    bg_transcoder_send_msg_finished(t->message_hub);
    log_transcoding_time(t);
    return 0;
    }
  
  /* Do the actual transcoding */
  /* Subtitle iteration must always be done */
  if(!subtitle_iteration(t))
    {
    t->state = TRANSCODER_STATE_ERROR;
    bg_transcoder_send_msg_error(t->message_hub);
    return 0;
    }


  if(stream)
    {
    if(stream->st->type == GAVL_STREAM_AUDIO)
      {
      if(!audio_iteration((audio_stream_t*)stream, t))
        {
        t->state = TRANSCODER_STATE_ERROR;
        bg_transcoder_send_msg_error(t->message_hub);
        return 0;
        }
      }
    
    else if(stream->st->type == GAVL_STREAM_VIDEO)
      {
      if(!video_iteration((video_stream_t*)stream, t))
        {
        t->state = TRANSCODER_STATE_ERROR;
        bg_transcoder_send_msg_error(t->message_hub);
        return 0;
        }
      }
    if(stream->time > t->time)
      t->time = stream->time;
    
    }

  
  /* Update status */

  real_time = gavl_timer_get(t->timer);
  real_seconds = gavl_time_to_seconds(real_time);

  t->percentage_done =
    gavl_time_to_seconds(t->time) /
    gavl_time_to_seconds(t->duration);
  
  if(t->percentage_done < 0.0)
    t->percentage_done = 0.0;
  if(t->percentage_done > 1.0)
    t->percentage_done = 1.0;
  if(t->percentage_done == 0.0)
    remaining_seconds = 0.0;
  else
    {
    remaining_seconds = real_seconds *
      (1.0 / t->percentage_done - 1.0);
    }
  
  t->remaining_time =
    gavl_seconds_to_time(remaining_seconds);

  if(real_seconds - t->last_seconds > 1.0)
    {
    bg_transcoder_send_msg_progress(t->message_hub, t->percentage_done, t->remaining_time);
    t->last_seconds = real_seconds;
    }
  
  
  return 1;
  }



void bg_transcoder_destroy(bg_transcoder_t * t)
  {
  int i;
  int do_delete = 0;
  char tmp_string[128];
  
  if((t->state == TRANSCODER_STATE_RUNNING) && t->delete_incomplete &&
     (t->duration > 0))
    do_delete = 1;
  else if(t->state == TRANSCODER_STATE_INIT)
    do_delete = 1;
  else if(t->state == TRANSCODER_STATE_ERROR)
    do_delete = 1;
  
  /* Close all encoders so the files are finished */

  if(t->enc)
    bg_encoder_destroy(t->enc, do_delete);
  
  /* Send created files to gmerlin */

  if((t->state != TRANSCODER_STATE_RUNNING) && !do_delete)
    {
    send_file_messages(t);
    }
  
  free_output_files(t);
  
  /* Cleanup streams */

  for(i = 0; i < t->num_video_streams; i++)
    {
    if((t->video_streams[i].com.action != BG_STREAM_ACTION_OFF) && !do_delete)
      {
      sprintf(tmp_string, "%" PRId64, t->video_streams[i].frames_written);
      gavl_log(GAVL_LOG_INFO, LOG_DOMAIN,
             "Video stream %d: Transcoded %s frames", i+1,
             tmp_string);
      }
    cleanup_video_stream(&t->video_streams[i]);
    }
  for(i = 0; i < t->num_audio_streams; i++)
    {
    if((t->audio_streams[i].com.action != BG_STREAM_ACTION_OFF) && !do_delete)
      {
      sprintf(tmp_string, "%" PRId64, t->audio_streams[i].samples_read);
      gavl_log(GAVL_LOG_INFO, LOG_DOMAIN,
             "Audio stream %d: Transcoded %s samples", i+1,
             tmp_string);
      }
    cleanup_audio_stream(&t->audio_streams[i]);
    }

  for(i = 0; i < t->num_text_streams; i++)
    {
    cleanup_subtitle_stream((subtitle_stream_t*)(&t->text_streams[i]));
    }
  for(i = 0; i < t->num_overlay_streams; i++)
    {
    cleanup_subtitle_stream(&t->overlay_streams[i]);
    }
      
  if(t->audio_streams)
    free(t->audio_streams);
  if(t->video_streams)
    free(t->video_streams);
  if(t->text_streams)
    free(t->text_streams);
  if(t->overlay_streams)
    free(t->overlay_streams);
  
  /* Free metadata */

  gavl_dictionary_free(&t->metadata);
  
  /* Close and destroy the input plugin */

  close_input(t);
  
  /* Free rest */

  FREE_STR(t->name);
  FREE_STR(t->output_directory);
  FREE_STR(t->output_path);
  FREE_STR(t->subdir);

  FREE_STR(t->output_filename);
  
  gavl_timer_destroy(t->timer);

  bg_msg_hub_destroy(t->message_hub);
  pthread_mutex_destroy(&t->stop_mutex);

  
  free(t);
  }


static void * thread_func(void * data)
  {
  int do_stop;
  bg_transcoder_t * t = (bg_transcoder_t*)data;
  while(bg_transcoder_iteration(t))
    {
    pthread_mutex_lock(&t->stop_mutex);
    do_stop = t->do_stop;
    pthread_mutex_unlock(&t->stop_mutex);
    if(do_stop)
      break;
    }
  
  return NULL;
  }

void bg_transcoder_run(bg_transcoder_t * t)
  {
  pthread_create(&t->thread, NULL, thread_func, t);
  }

void bg_transcoder_stop(bg_transcoder_t * t)
  {
  pthread_mutex_lock(&t->stop_mutex);
  t->do_stop = 1;
  pthread_mutex_unlock(&t->stop_mutex);
  }

void bg_transcoder_finish(bg_transcoder_t * t)
  {
  pthread_join(t->thread, NULL);
  }

