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

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <math.h>

// mkdir()
#include <sys/stat.h> 
#include <sys/types.h>

#include <config.h>
#include <gmerlin/translation.h>

#include <gmerlin/pluginregistry.h>
#include <gmerlin/log.h>
#include <gmerlin/msgqueue.h>
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
#include <gavl/peakdetector.h>

// #define DUMP_VIDEO_TIMESTAMPS
// #define DUMP_AUDIO_TIMESTAMPS

#define LOG_DOMAIN "transcoder"

#define STREAM_STATE_OFF      0
#define STREAM_STATE_ON       1
#define STREAM_STATE_FINISHED 2

#define STREAM_TYPE_AUDIO   0
#define STREAM_TYPE_VIDEO   1
#define STREAM_TYPE_TEXT    2
#define STREAM_TYPE_OVERLAY 3

#define STREAM_ACTION_FORGET    0
#define STREAM_ACTION_TRANSCODE 1
#define STREAM_ACTION_COPY      2

/* The followings are for subtitles only */

#define STREAM_ACTION_BLEND           3
/* The following is only for text subtitles and means, that they'll be
   converted to graphical overlays using the text renderer */
#define STREAM_ACTION_TRANSCODE_OVERLAY  4

#define TRANSCODER_STATE_INIT     0
#define TRANSCODER_STATE_RUNNING  1
#define TRANSCODER_STATE_FINISHED 2
#define TRANSCODER_STATE_ERROR    3


typedef struct subtitle_stream_s subtitle_stream_t;

typedef struct
  {
  
  int status;
  int type;

  int action;
  
  int in_index;
  int out_index;
  gavl_time_t time;

  bg_plugin_handle_t * in_handle;
  bg_input_plugin_t  * in_plugin;
  
  int do_encode; /* Whether this stream should be really encoded */
  int do_decode; /* Whether this stream should be decoded */
  int do_copy;   /* Whether this stream should be copied */
  
  gavl_compression_info_t ci;
  gavl_packet_t packet;
  gavl_dictionary_t m;
  gavl_packet_source_t * psrc;  
  gavl_packet_sink_t * psink;

  bg_transcoder_t * t;

  int64_t pts_offset;
  int64_t pts_end;    // When we don't decode to the very end
  } stream_t;

static int set_stream_parameters_general(stream_t * s,
                                         const char * name,
                                         const gavl_value_t * val)
  {
  if(!strcmp(name, "action"))
    {
    if(!strcmp(val->v.str, "transcode"))
      s->action = STREAM_ACTION_TRANSCODE;
    else if(!strcmp(val->v.str, "copy"))
      s->action = STREAM_ACTION_COPY;
    else if(!strcmp(val->v.str, "transcode_overlay"))
      s->action = STREAM_ACTION_TRANSCODE_OVERLAY;
    else if(!strcmp(val->v.str, "blend"))
      s->action = STREAM_ACTION_BLEND;
    else
      s->action = STREAM_ACTION_FORGET;
    return 1;
    }
  return 0;
  }

typedef struct
  {
  stream_t com;

  bg_audio_filter_chain_t * fc;

  /* TODO: These can probably be kicked out as well */  
  gavl_audio_format_t in_format;
  gavl_audio_format_t out_format;
  
  /* Set by set_parameter */
  bg_gavl_audio_options_t options;

  /* Do normalization */
  int normalize;
  
  int64_t samples_read;    /* Samples read so far (in OUTPUT samplerate) */
  
  int initialized;

  gavl_peak_detector_t * peak_detector;
  gavl_volume_control_t * volume_control;

  gavl_audio_source_t * asrc;        // Output of the filter chain
  gavl_audio_sink_t * asink;
  } audio_stream_t;

static void set_audio_parameter_general(void * data, const char * name,
                                        const gavl_value_t * val)
  {
  audio_stream_t * stream;
  stream = (audio_stream_t*)data;
  
  if(!name)
    return;

  if(!strcmp(name, "normalize"))
    {
    stream->normalize = val->v.i;
    return;
    }
  if(!strcmp(name, GAVL_META_LANGUAGE))
    {
    gavl_dictionary_set_string(&stream->com.m, GAVL_META_LANGUAGE,val->v.str);
    return;
    }
  else if(set_stream_parameters_general(&stream->com,
                                   name, val))
    return;
  else if(bg_gavl_audio_set_parameter(&stream->options, name, val))
    return;

  }

typedef struct
  {
  stream_t com;
  
  bg_video_filter_chain_t * fc;
  
  gavl_video_frame_t * frame;
  
  gavl_video_format_t in_format;
  gavl_video_format_t out_format;

  int64_t frames_written;
    
  /* Set by set_parameter */

  bg_gavl_video_options_t options;
  
  /* Other stuff */

  int initialized;
  
  /* Whether 2-pass transcoding is requested */
  int twopass;
  char * stats_file;

  /* Subtitle streams for blending */
  int num_subtitle_streams;
  subtitle_stream_t ** subtitle_streams;
  
  int b_frames_seen;
  int flush_b_frames;

  gavl_video_source_t * vsrc;
  gavl_video_sink_t   * vsink;
  } video_stream_t;

static void set_video_parameter_general(void * data,
                                        const char * name,
                                        const gavl_value_t * val)
  {
  video_stream_t * stream;
  stream = (video_stream_t*)data;
  
  if(!name)
    return;

  if(!strcmp(name, "twopass"))
    stream->twopass = val->v.i;
  
  if(set_stream_parameters_general(&stream->com,
                                   name, val))
    return;

  else if(bg_gavl_video_set_parameter(&stream->options,
                                 name, val))
    return;
  }

struct subtitle_stream_s
  {
  stream_t com;
  
  gavl_video_frame_t * ovl;
  
  int has_current;
  // int has_next;

  bg_subtitle_handler_t * sh;
  gavl_overlay_blend_context_t * blend_context;
  int video_stream;

  gavl_video_format_t in_format;
  gavl_video_format_t out_format;
  
  //  int do_blend; /* Set by check_video_blend() */
  
  int eof; /* Set STREAM finished not before the last subtitle expired */
  
  int64_t time_offset;
  int64_t time_offset_scaled;

  int64_t subtitle_start_unscaled;

  gavl_video_source_t * vsrc;
  gavl_video_sink_t * vsink;
  };

typedef struct
  {
  subtitle_stream_t com;
  bg_text_renderer_t * textrenderer;
  int timescale;
  } text_stream_t;

static void set_subtitle_parameter_general(void * data,
                                           const char * name,
                                           const gavl_value_t * val)
  {
  subtitle_stream_t * stream;
  stream = (subtitle_stream_t*)data;

  if(!name)
    return;

  if(!strcmp(name, "video_stream"))
    stream->video_stream = val->v.i-1;
  else if(!strcmp(name, GAVL_META_LANGUAGE))
    {
    gavl_dictionary_set_string(&stream->com.m, GAVL_META_LANGUAGE,val->v.str);
    return;
    }
  else if(!strcmp(name, "time_offset"))
    {
    stream->time_offset = (int64_t)(val->v.d * GAVL_TIME_SCALE + 0.5);
    return;
    }
  else if(set_stream_parameters_general(&stream->com,
                                        name, val))
    return;
  }

struct bg_transcoder_s
  {
  int num_audio_streams;
  int num_video_streams;

  int num_text_streams;
  int num_overlay_streams;

  int num_audio_streams_real;
  int num_video_streams_real;

  int num_text_streams_real;
  int num_overlay_streams_real;
      
  audio_stream_t * audio_streams;
  video_stream_t * video_streams;

  text_stream_t * text_streams;
  subtitle_stream_t      * overlay_streams;
  
  float percentage_done;
  gavl_time_t remaining_time; /* Remaining time (Transcoding time, NOT track time!!!) */
  double last_seconds;
    
  int state;
    
  bg_plugin_handle_t * in_handle;
  bg_input_plugin_t  * in_plugin;
  gavl_dictionary_t * track_info;

  bg_plugin_handle_t * out_handle;

  
  /* Set by set_parameter */

  char * name;
  const char * location;
  
  int track;

  gavl_time_t start_time;
  gavl_time_t end_time;
  int set_start_time;
  int set_end_time;
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

  int total_passes;
  int pass;
  /* Track we are created from */
  bg_transcoder_track_t * transcoder_track;

  /* Encoder frontend */
  bg_encoder_t * enc;
  
  /* Postprocess only */
  int pp_only;
  
  bg_encoder_callbacks_t cb;

  char ** output_files;
  int num_output_files;
  };

static void add_output_file(bg_transcoder_t * t, const char * filename)
  {
  t->output_files = realloc(t->output_files,
                            (t->num_output_files+2) * sizeof(*t->output_files));

  memset(t->output_files + t->num_output_files, 0, 2 * sizeof(*t->output_files));

  t->output_files[t->num_output_files] = gavl_strdup(filename);
  t->num_output_files++;
  }

static void free_output_files(bg_transcoder_t * t)
  {
  int i = 0;
  
  for(i = 0; i < t->num_output_files; i++)
    free(t->output_files[i]);

  if(t->output_files)
    free(t->output_files);
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

static void init_audio_stream(audio_stream_t * ret,
                              const gavl_dictionary_t * s,
                              int in_index)
  {
  ret->com.type = STREAM_TYPE_AUDIO;
  
  gavl_dictionary_copy(&ret->com.m, gavl_stream_get_metadata(s));
  
  /* Default options */

  ret->volume_control = gavl_volume_control_create();
  ret->peak_detector = gavl_peak_detector_create();
  
  /* Create converter */
  
  bg_gavl_audio_options_init(&ret->options);
  ret->fc = bg_audio_filter_chain_create(&ret->options);
  
  /* Apply parameters */

  bg_cfg_section_apply(bg_transcoder_track_get_cfg_general(s), NULL,
                       set_audio_parameter_general, ret);

  bg_cfg_section_apply(bg_transcoder_track_get_cfg_filter(s),
                       bg_audio_filter_chain_get_parameters(ret->fc),
                       bg_audio_filter_chain_set_parameter, ret->fc);
  
  ret->com.in_index = in_index;
  }

static void start_audio_stream_i(audio_stream_t * ret,
                                 bg_plugin_handle_t * in_handle)
  {
  bg_stream_action_t action = BG_STREAM_ACTION_OFF;
  
  ret->com.in_handle = in_handle;
  ret->com.in_plugin = (bg_input_plugin_t*)(in_handle->plugin);
  
  /* Set stream */

  if(ret->com.do_decode)
    action = BG_STREAM_ACTION_DECODE;
  else if(ret->com.do_copy)
    action = BG_STREAM_ACTION_READRAW;
  
  bg_media_source_set_audio_action(ret->com.in_handle->src, ret->com.in_index, action);
  }


static void init_video_stream(video_stream_t * ret,
                              const gavl_dictionary_t * s,
                              int in_index)
  {
  ret->com.type = STREAM_TYPE_VIDEO;
  gavl_dictionary_copy(&ret->com.m, gavl_stream_get_metadata(s));
  /* Default options */
  bg_gavl_video_options_init(&ret->options);
  
  /* Create converter */

  ret->fc  = bg_video_filter_chain_create(&ret->options);
  
  /* Apply parameters */

  bg_cfg_section_apply(bg_transcoder_track_get_cfg_general(s), NULL,
                       set_video_parameter_general, ret);

  bg_cfg_section_apply(bg_transcoder_track_get_cfg_filter(s),
                       bg_video_filter_chain_get_parameters(ret->fc),
                       bg_video_filter_chain_set_parameter, ret->fc);
  
  ret->com.in_index = in_index;
  }

static void start_video_stream_i(video_stream_t * ret,
                                 bg_plugin_handle_t * in_handle)
  {
  bg_stream_action_t action = BG_STREAM_ACTION_OFF;

  ret->com.in_handle = in_handle;
  ret->com.in_plugin = (bg_input_plugin_t*)(in_handle->plugin);
  
  /* Set stream */
  if(ret->com.do_decode)
    action = BG_STREAM_ACTION_DECODE;
  else if(ret->com.do_copy)
    action = BG_STREAM_ACTION_READRAW;

  bg_media_source_set_video_action(ret->com.in_handle->src, ret->com.in_index, action);
  }


static void init_overlay_stream(subtitle_stream_t * ret,
                                const gavl_dictionary_t * s, int idx)
  {
  ret->com.type = STREAM_TYPE_OVERLAY;
  gavl_dictionary_copy(&ret->com.m, gavl_stream_get_metadata(s));  
  /* Apply parameters */
  
  bg_cfg_section_apply(bg_transcoder_track_get_cfg_general(s), NULL,
                       set_subtitle_parameter_general, ret);
  ret->com.in_index = idx;
  
  if(ret->com.action == STREAM_ACTION_BLEND)
    {
    ret->blend_context = gavl_overlay_blend_context_create();
    ret->sh = bg_subtitle_handler_create();
    }

  }

static void init_text_stream(text_stream_t * ret,
                             const gavl_dictionary_t * s, int idx)
  {
  ret->com.com.type = STREAM_TYPE_TEXT;
  gavl_dictionary_copy(&ret->com.com.m, gavl_stream_get_metadata(s));
  /* Apply parameters */
  
  bg_cfg_section_apply(bg_transcoder_track_get_cfg_general(s),
                       NULL,
                       set_subtitle_parameter_general, ret);
  ret->com.com.in_index = idx;
  
  if(ret->com.com.action == STREAM_ACTION_BLEND)
    {
    ret->com.blend_context = gavl_overlay_blend_context_create();
    
    ret->textrenderer = bg_text_renderer_create();
    bg_cfg_section_apply(bg_transcoder_track_get_cfg_textrenderer(s),
                         bg_text_renderer_get_parameters(),
                         bg_text_renderer_set_parameter,
                         ret->textrenderer);
    ret->com.sh = bg_subtitle_handler_create();
    }
  else if(ret->com.com.action == STREAM_ACTION_TRANSCODE_OVERLAY)
    {
    ret->textrenderer = bg_text_renderer_create();
    bg_cfg_section_apply(bg_transcoder_track_get_cfg_textrenderer(s),
                         bg_text_renderer_get_parameters(),
                         bg_text_renderer_set_parameter,
                         ret->textrenderer);
    }
  }

static void start_text_stream_i(subtitle_stream_t * ret,
                                bg_plugin_handle_t * in_handle)
  {
  bg_stream_action_t action = BG_STREAM_ACTION_OFF;

  ret->com.in_handle = in_handle;
  ret->com.in_plugin = (bg_input_plugin_t*)(in_handle->plugin);
  
  /* Set stream */
  
  if(ret->com.action != STREAM_ACTION_FORGET)
    action = BG_STREAM_ACTION_DECODE;
  
  bg_media_source_set_text_action(ret->com.in_handle->src, ret->com.in_index, action);
  }

static void start_overlay_stream_i(subtitle_stream_t * ret,
                                   bg_plugin_handle_t * in_handle)
  {
  bg_stream_action_t action = BG_STREAM_ACTION_OFF;

  ret->com.in_handle = in_handle;
  ret->com.in_plugin = (bg_input_plugin_t*)(in_handle->plugin);
  
  /* Set stream */
  if(ret->com.action == STREAM_ACTION_COPY)
    action = BG_STREAM_ACTION_READRAW;
    
  else if(ret->com.action != STREAM_ACTION_FORGET)
    action = BG_STREAM_ACTION_DECODE;
    
  bg_media_source_set_overlay_action(ret->com.in_handle->src, ret->com.in_index, action);
  }

static int set_input_formats(bg_transcoder_t * ret)
  {
  int i;
  for(i = 0; i < ret->num_audio_streams; i++)
    {
    audio_stream_t * s = &ret->audio_streams[i];
    
    if(!s->com.do_decode && !s->com.do_copy)
      continue;
    
    if(s->com.do_copy)
      {
      s->com.psrc =
        bg_media_source_get_audio_packet_source(s->com.in_handle->src, s->com.in_index);
      gavl_audio_format_copy(&s->in_format,
                             gavl_packet_source_get_audio_format(s->com.psrc));
                             
      }
    else
      {
      gavl_audio_source_t * src =
        bg_media_source_get_audio_source(s->com.in_handle->src, s->com.in_index);

      if(!src)
        return 0;
      
      gavl_audio_format_copy(&s->in_format,
                             gavl_audio_source_get_src_format(src));
      }
    }
  for(i = 0; i < ret->num_video_streams; i++)
    {
    video_stream_t * s = &ret->video_streams[i];
    
    if(!s->com.do_decode && !s->com.do_copy)
      continue;
    
    gavl_video_format_copy(&s->in_format, gavl_track_get_video_format(ret->track_info, s->com.in_index));

    if(s->com.do_copy)
      s->com.psrc =
        bg_media_source_get_video_packet_source(s->com.in_handle->src, s->com.in_index);
    
    }
  for(i = 0; i < ret->num_overlay_streams; i++)
    {
    subtitle_stream_t * s = &ret->overlay_streams[i];

    if(!s->com.do_decode && !s->com.do_copy)
      continue;

    if(s->com.do_decode)
      s->vsrc = bg_media_source_get_overlay_source(s->com.in_handle->src, s->com.in_index);
    else if(s->com.do_copy)
      s->com.psrc = bg_media_source_get_overlay_packet_source(s->com.in_handle->src, s->com.in_index);
    
    gavl_video_format_copy(&s->in_format,
                           gavl_track_get_overlay_format(ret->track_info, s->com.in_index));
    }
  for(i = 0; i < ret->num_text_streams; i++)
    {
    text_stream_t * s = &ret->text_streams[i];

    if(!s->com.com.do_decode)
      continue;

    s->com.com.psrc = bg_media_source_get_text_source(s->com.com.in_handle->src, s->com.com.in_index);
    s->timescale = gavl_packet_source_get_timescale(s->com.com.psrc);
    
    /* This is needed by correct_subtitle_timestamp */
    s->com.in_format.timescale = s->timescale;
    s->com.out_format.timescale = s->timescale;
    }

  return 1;
  }

static void add_audio_stream(audio_stream_t * ret,
                             bg_transcoder_t * t)
  {
  ret->asrc = 
    bg_audio_filter_chain_connect(ret->fc, 
                                  bg_media_source_get_audio_source(ret->com.in_handle->src,  ret->com.in_index));
  gavl_audio_format_copy(&ret->out_format,
                         gavl_audio_source_get_src_format(ret->asrc));

  /* We set the frame size so we have roughly half second long audio chunks */
  ret->out_format.samples_per_frame =
    gavl_time_to_samples(ret->out_format.samplerate,
                         GAVL_TIME_SCALE/2);
  
  /* Add the audio stream */

  ret->com.out_index =
    bg_encoder_add_audio_stream(t->enc,
                                &ret->com.m,
                                &ret->out_format,
                                ret->com.in_index, NULL);
  }

static void add_audio_stream_compressed(audio_stream_t * ret,
                                        bg_transcoder_t * t)
  {
  /* Add the audio stream */
  
  ret->com.out_index =
    bg_encoder_add_audio_stream_compressed(t->enc,
                                           &ret->com.m,
                                           gavl_packet_source_get_audio_format(ret->com.psrc),
                                           &ret->com.ci,
                                           ret->com.in_index);
  
  }


static void add_text_stream(text_stream_t * ret,
                            const gavl_dictionary_t * s,
                            bg_transcoder_t * t)
  {
  if(ret->com.com.action == STREAM_ACTION_TRANSCODE)
    {
    gavl_video_format_copy(&ret->com.out_format,
                           &ret->com.in_format);

    
    /* TODO: timescale might get changed by the encoder!!! */
    ret->com.com.out_index =
      bg_encoder_add_text_stream(t->enc,
                                 &ret->com.com.m,
                                 ret->com.out_format.timescale,
                                 ret->com.com.in_index, NULL);
    
    }
  else if(ret->com.com.action == STREAM_ACTION_TRANSCODE_OVERLAY)
    {
    /* Get the video format for overlay encoding. This is a bit nasty, since we have no idea yet,
       how the format will look like (the video output format isn't known by now). We'll copy the
       format, which was passed to the video encoder and hope that the overlay encoder will choose
       a proper pixelformat for us. Then, we pass the same format as *frame* format to the
       textrenderer */

    if(t->video_streams[ret->com.video_stream].com.do_encode ||
       t->video_streams[ret->com.video_stream].com.do_copy)
      {
      /* Obtain the video format for a rendered text subtitle stream */
      gavl_overlay_blend_context_t * ctx;
      ctx = gavl_overlay_blend_context_create();

      /* This will copy frame dimensions */
      gavl_video_format_copy(&ret->com.in_format,
                             &t->video_streams[ret->com.video_stream].in_format);
      
      /* Obtain a suitable pixelformat */
      gavl_overlay_blend_context_init(ctx,
                                      &t->video_streams[ret->com.video_stream].in_format,
                                      &ret->com.in_format);
      gavl_overlay_blend_context_destroy(ctx);

      /* Get the timing right */
      ret->com.in_format.timescale = ret->timescale;
      ret->com.in_format.frame_duration = 0;
      
      ret->com.in_format.framerate_mode = GAVL_FRAMERATE_VARIABLE;
      ret->com.in_format.timecode_format.int_framerate = 0;
      
      gavl_video_format_copy(&ret->com.out_format,
                             &ret->com.in_format);
      }
    else
      {
      /* Video stream won't get encoded: Use video format associatd with the text subtitle stream */
      /* TODO: This is going to fail, no video format is associated with a text stream!!! */
      gavl_video_format_copy(&ret->com.out_format, &ret->com.in_format);
      }

    //    fprintf(stderr, "Add overlay stream, video format:\n");
    //    gavl_video_format_dump(&t->video_streams[ret->com.video_stream].in_format);
    //    fprintf(stderr, "Overlay format:\n");
    //    gavl_video_format_dump(&ret->com.out_format);
    ret->com.com.out_index =
      bg_encoder_add_overlay_stream(t->enc,
                                    &ret->com.com.m,
                                    &ret->com.out_format,
                                    ret->com.com.in_index,
                                    GAVL_STREAM_TEXT, NULL);
    }
  }

static void
add_overlay_stream(subtitle_stream_t * ret,
                            const gavl_dictionary_t * s,
                            bg_transcoder_t * t)
  {
  gavl_video_format_copy(&ret->out_format, &ret->in_format);
  
  ret->com.out_index =
    bg_encoder_add_overlay_stream(t->enc,
                                  &ret->com.m,
                                  &ret->out_format, ret->com.in_index,
                                  GAVL_STREAM_OVERLAY, NULL);
  }

static void
add_overlay_stream_compressed(subtitle_stream_t * ret,
                              const gavl_dictionary_t * s,
                              bg_transcoder_t * t)
  {
  gavl_video_format_copy(&ret->out_format, &ret->in_format);

  
  ret->com.out_index =
    bg_encoder_add_overlay_stream_compressed(t->enc,
                                             &ret->com.m,
                                             &ret->out_format,
                                             &ret->com.ci,
                                             ret->com.in_index);
  }

static void add_video_stream(video_stream_t * ret,
                             bg_transcoder_t * t)
  {
  ret->vsrc = 
    bg_video_filter_chain_connect(ret->fc, 
                                  bg_media_source_get_video_source(ret->com.in_handle->src,  ret->com.in_index));
  gavl_video_format_copy(&ret->out_format, gavl_video_source_get_src_format(ret->vsrc));

  /* Add the video stream */

  ret->com.out_index =
    bg_encoder_add_video_stream(t->enc,
                                &ret->com.m,
                                &ret->out_format,
                                ret->com.in_index, NULL);
  }

static void add_video_stream_compressed(video_stream_t * ret,
                                        bg_transcoder_t * t)
  {
  /* Add the video stream */

  ret->com.out_index =
    bg_encoder_add_video_stream_compressed(t->enc,
                                           &ret->com.m,
                                           &ret->in_format,
                                           &ret->com.ci,
                                           ret->com.in_index);
  }

static int set_video_pass(bg_transcoder_t * t, int i)
  {
  video_stream_t * s;

  s = &t->video_streams[i];
  if(!s->twopass)
    return 1;
  
  if(!s->stats_file)
    {
    s->stats_file = bg_sprintf("%s/%s_video_%02d.stats", t->output_directory, t->name, i+1);
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Using statistics file: %s", s->stats_file);
    }

  bg_encoder_set_video_pass(t->enc,
                            s->com.out_index, t->pass, t->total_passes,
                            s->stats_file);
  return 1;
  }

static int audio_iteration(audio_stream_t*s, bg_transcoder_t * t)
  {
  int ret = 1;
  
  /* Get one frame worth of input data */

  if(!s->initialized)
    {
    if((t->start_time != GAVL_TIME_UNDEFINED) &&
       (t->end_time != GAVL_TIME_UNDEFINED) &&
       (t->end_time > t->start_time))
      {
      s->com.pts_end = gavl_time_to_samples(s->out_format.samplerate,
                                            t->end_time - t->start_time);
      }
    else if(t->end_time != GAVL_TIME_UNDEFINED)
      {
      s->com.pts_end = gavl_time_to_samples(s->out_format.samplerate,
                                            t->end_time);
      }
    else
      s->com.pts_end = 0; /* Zero == Infinite */

    if(t->start_time != GAVL_TIME_UNDEFINED)
      s->com.pts_offset = -gavl_time_to_samples(s->out_format.samplerate,
                                                t->start_time);
    s->initialized = 1;
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
    if(s->com.pts_end && (s->com.pts_end <= s->samples_read))
      s->com.status = STREAM_STATE_FINISHED;

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
    frame->timestamp += s->com.pts_offset;

    if(s->com.pts_end && 
       (s->samples_read + frame->valid_samples > s->com.pts_end))
      {
      frame->valid_samples = s->com.pts_end - s->samples_read;
      if(frame->valid_samples < 0)
        {
        s->com.status = STREAM_STATE_FINISHED;
        return ret;
        }
      }
   
    /* Update sample counter before the frame is set to NULL
       after peak-detection */

    s->samples_read += frame->valid_samples;
 
    /* Volume normalization */
    if(s->normalize)
      {
      if(t->pass == t->total_passes)
        gavl_volume_control_apply(s->volume_control, frame);
      else if((t->pass > 1) && (t->pass < t->total_passes))
        frame = NULL;
      else if(t->pass == 1)
        {
        gavl_peak_detector_update(s->peak_detector, frame);
        frame = NULL;
        }
      }
        
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
  
  if(s->com.pts_end && (s->com.pts_end <= s->samples_read))
    s->com.status = STREAM_STATE_FINISHED;
  
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
  
  if(t->start_time != GAVL_TIME_UNDEFINED)
    {
    *start -= gavl_time_scale(s->in_format.timescale, t->start_time);
    if(*start < 0)
      {
      *duration += *start;
      if(*duration < 0)
        *duration = 0;
      *start = 0;
      }
    }
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
  if((t->end_time != GAVL_TIME_UNDEFINED) &&
     (gavl_time_unscale(s->in_format.timescale, (*ovl)->timestamp) >= t->end_time))
    {
    st = GAVL_SOURCE_EOF;
    s->eof = 1;
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

  if((t->end_time != GAVL_TIME_UNDEFINED) &&
     (gavl_time_unscale(s->in_format.timescale, pp->pts) >= t->end_time))
    {
    s->eof = 1;
    return GAVL_SOURCE_EOF;
    }
  
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

  if((t->end_time != GAVL_TIME_UNDEFINED) &&
     (gavl_time_unscale(s->com.in_format.timescale, pp->pts) >= t->end_time))
    {
    s->com.eof = 1;
    return GAVL_SOURCE_EOF;
    }
  
  correct_subtitle_timestamp(&s->com, &s->com.com.packet.pts,
                             &s->com.com.packet.duration, t);
  
  return GAVL_SOURCE_OK;
  }

#if 0
static int check_video_blend(video_stream_t * vs,
                             bg_transcoder_t * t, int64_t time)
  {
  gavl_source_status_t st;
  gavl_overlay_t * tmp_ovl;
  int i;
  subtitle_stream_t * ss;
  int ret = 0;
  int current_changed = 0;
  
  for(i = 0; i < vs->num_subtitle_streams; i++)
    {
    ss = vs->subtitle_streams[i];
    
    if(ss->com.status != STREAM_STATE_ON)
      continue;
    
    /* Check if the overlay expired */
    if(ss->has_current)
      {
      if(bg_overlay_too_old(time,
                            ss->current_ovl->timestamp,
                            ss->current_ovl->duration))
        {
        tmp_ovl = ss->current_ovl;
        ss->current_ovl = ss->next_ovl;
        ss->next_ovl = tmp_ovl;

        ss->has_current = ss->has_next;
        ss->has_next = 0;
        
        if(ss->has_current)
          current_changed = 1;
        }
      }
    
    /* Check if the next overlay replaces the current one */
    if(ss->has_next)
      {
      if(!bg_overlay_too_new(time,
                             ss->next_ovl->timestamp))
        {
        tmp_ovl = ss->current_ovl;
        ss->current_ovl = ss->next_ovl;
        ss->next_ovl = tmp_ovl;

        ss->has_current = 1;
        
        ss->has_next = 0;
        
        if(bg_overlay_too_old(time,
                              ss->current_ovl->timestamp,
                              ss->current_ovl->duration))
          ss->has_current = 0;
        else
          current_changed = 1;
        }
      }

    if(!ss->has_current && !ss->eof)
      {
      st = decode_overlay(ss, t, ss->current_ovl);
      if(st == GAVL_SOURCE_OK)
        {
        ss->has_current = 1;
        current_changed = 1;
        }
      else if(st == GAVL_SOURCE_EOF)
        ss->eof = 1;
      else
        continue;
      }
    if(!ss->has_next && !ss->eof)
      {
      st = decode_overlay(ss, t, ss->next_ovl);
      if(st == GAVL_SOURCE_OK)
        ss->has_next = 1;
      else if(st == GAVL_SOURCE_EOF)
        ss->eof = 1;
      }
    
    if(ss->has_current &&
       !bg_overlay_too_new(time,
                           ss->current_ovl->timestamp))
      {
      ss->do_blend = 1;
      ret = 1;
      }
    else
      ss->do_blend = 0;

    if(current_changed)
      gavl_overlay_blend_context_set_overlay(ss->blend_context,
                                             ss->current_ovl);

    if(!ss->has_current && !ss->has_next &&
       ss->eof)
      ss->com.status = STREAM_STATE_FINISHED;
    }
  return ret;
  }
#endif

#define SWAP_FRAMES \
  tmp_frame=s->in_frame_1;\
  s->in_frame_1=s->in_frame_2;\
  s->in_frame_2=tmp_frame

static int video_iteration(video_stream_t * s, bg_transcoder_t * t)
  {
  int ret = 1;
  int i;
  
  if(!s->initialized)
    {
    if((t->start_time != GAVL_TIME_UNDEFINED) &&
       (t->end_time != GAVL_TIME_UNDEFINED) &&
       (t->end_time > t->start_time))
      {
      s->com.pts_end = gavl_time_scale(s->out_format.timescale,
                                       t->end_time - t->start_time);
      }
    else if(t->end_time != GAVL_TIME_UNDEFINED)
      {
      s->com.pts_end = gavl_time_scale(s->out_format.timescale,
                                       t->end_time);
      }
    else
      s->com.pts_end = 0; /* Zero == Infinite */

    if(t->start_time != GAVL_TIME_UNDEFINED)
      {
      s->com.pts_offset = -gavl_time_scale(s->out_format.timescale,
                                           t->start_time);
      }
    s->initialized = 1;
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
    
    if((s->com.packet.flags & GAVL_PACKET_TYPE_MASK) != GAVL_PACKET_TYPE_B)
      {
      if(s->flush_b_frames)
        {
        s->com.status = STREAM_STATE_FINISHED;
        return ret;
        }
      s->com.time = gavl_time_unscale(s->in_format.timescale,
                                      s->com.packet.pts + s->com.packet.duration);
      
      if((t->end_time != GAVL_TIME_UNDEFINED) &&
         (s->com.time >= t->end_time))
        {
        if(s->b_frames_seen)
          s->flush_b_frames = 1;
        else
          {
          s->com.status = STREAM_STATE_FINISHED;
          return ret;
          }
        }
      }
    else
      {
      s->b_frames_seen = 1;
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
      /* Set this also for all attached subtitle streams */
      for(i = 0; i < s->num_subtitle_streams; i++)
        s->subtitle_streams[i]->com.status = STREAM_STATE_FINISHED;
      return ret;
      }
    frame->timestamp += s->com.pts_offset;

    s->com.time = gavl_time_unscale(s->out_format.timescale,
                                    frame->timestamp + frame->duration);

    if(s->com.pts_end &&
       (frame->timestamp + frame->duration >= s->com.pts_end))
      s->com.status = STREAM_STATE_FINISHED;
    
    for(i = 0; i < s->num_subtitle_streams; i++)
      {
      bg_subtitle_handler_update(s->subtitle_streams[i]->sh, frame);
      gavl_overlay_blend(s->subtitle_streams[i]->blend_context, frame);
      }

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
      if(ts->com.com.action == STREAM_ACTION_TRANSCODE)
        {
        st = decode_text(ts, t);
        if(st == GAVL_SOURCE_OK)
          ts->com.has_current = 1;
        else if(st == GAVL_SOURCE_EOF)
          ts->com.eof = 1;
        }
      else if(ts->com.com.action == STREAM_ACTION_TRANSCODE_OVERLAY)
        {
        st = decode_overlay((subtitle_stream_t*)ts, t, &ts->com.ovl);

        if(st == GAVL_SOURCE_OK)
          ts->com.has_current = 1;
        else if(st == GAVL_SOURCE_EOF)
          ts->com.eof = 1;
        }
      }
    
    /* Check for encoding */
    if(ts->com.has_current)
      {
      vs = &t->video_streams[ts->com.video_stream];

      if(do_write_subtitle(ts->com.subtitle_start_unscaled, vs))
        {
#if 0
        fprintf(stderr, "Encode subtitle %"PRId64" %"PRId64" %"PRId64"\n",
                ts->com.subtitle_start_unscaled,
                vs->com.time,
                ts->com.subtitle_start_unscaled -
                vs->com.time);
#endif
        if(ts->com.com.action == STREAM_ACTION_TRANSCODE)
          {
          ret =
            bg_encoder_write_text(t->enc,
                                  (char*)(ts->com.com.packet.buf.buf),
                                  ts->com.com.packet.pts,
                                  ts->com.com.packet.duration,
                                  ts->com.com.out_index);

          ts->com.com.time = gavl_time_unscale(ts->com.out_format.timescale,
                                               ts->com.com.packet.pts);
          
          if(ts->com.com.time > t->time)
            t->time = ts->com.com.time;
          }
        else if(ts->com.com.action == STREAM_ACTION_TRANSCODE_OVERLAY)
          {
          ret =
            bg_encoder_write_overlay(t->enc, ts->com.ovl, ts->com.com.out_index);
          ts->com.com.time = gavl_time_unscale(ts->com.out_format.timescale,
                                               ts->com.ovl->timestamp);
          
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
        vs = &t->video_streams[ss->video_stream];

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
        vs = &t->video_streams[ss->video_stream];

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

#define SP_TIME(s) if(!strcmp(name, # s)) \
    { \
    t->s = val->v.l; \
    return; \
    }

static void
set_parameter_general(void * data, const char * name,
                      const gavl_value_t * val)
  {
  int i, name_len;
  bg_transcoder_t * t;
  t = (bg_transcoder_t *)data;

  if(!name)
    {
    /* Set start and end times */

    if(!t->set_start_time)
      t->start_time = GAVL_TIME_UNDEFINED;

    if(!t->set_end_time)
      t->end_time = GAVL_TIME_UNDEFINED;

    /* Replace all '/' by '-' */

    if(t->name)
      {
      name_len = strlen(t->name);
      for(i = 0; i < name_len; i++)
        {
        if(t->name[i] == '/')
          t->name[i] = '-';
        }
      }

    /* Create the subdirectory if necessary */
    if(t->subdir)
      {
      t->output_directory = bg_sprintf("%s/%s", t->output_path, t->subdir);

      if(mkdir(t->output_directory,
               S_IRUSR|S_IWUSR|S_IXUSR|S_IRGRP|S_IXGRP|S_IROTH|S_IWOTH) == -1)
        {
        if(errno != EEXIST)
          {
          gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Cannot create directory %s: %s, using default",
                 t->output_directory, strerror(errno));
          t->output_directory = gavl_strrep(t->output_directory, t->output_path);
          }
        }
      else
        gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Created directory %s", t->output_directory);
      }
    else
      t->output_directory = gavl_strrep(t->output_directory, t->output_path);
    
    return;
    }
  // SP_STR(name);
  SP_STR(subdir);
  
  SP_INT(track);
    
  SP_INT(set_start_time);
  SP_INT(set_end_time);
  SP_TIME(start_time);
  SP_TIME(end_time);
  SP_INT(pp_only);
  }

#undef SP_INT
#undef SP_TIME
#undef SP_STR


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
    tmp_string = bg_sprintf(TR("Postprocessing %s"), t->location);
    }
  else if(t->total_passes > 1)
    {
    tmp_string = bg_sprintf(TR("Transcoding %s [Pass %d/%d]"),
                            t->location, t->pass, t->total_passes);
    }
  else
    {
    tmp_string = bg_sprintf(TR("Transcoding %s"),
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
          case STREAM_ACTION_BLEND:
            gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Text subtitle stream %d: Blending onto video stream %d",
                   i+1, t->text_streams[i].com.video_stream);
            break;
          case STREAM_ACTION_TRANSCODE:
          case STREAM_ACTION_TRANSCODE_OVERLAY:
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
          case STREAM_ACTION_BLEND:
            gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Overlay subtitle stream %d: Blending onto video stream %d",
                   i+1, t->overlay_streams[i].video_stream);
            break;
          case STREAM_ACTION_TRANSCODE:
          case STREAM_ACTION_TRANSCODE_OVERLAY:
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
  int i, j;
  
  for(i = 0; i < ret->num_audio_streams; i++)
    {
    if(!(ret->audio_streams[i].com.action == STREAM_ACTION_COPY))
      continue;

    /* Check if we can read compressed data */

    

    if(!gavl_track_get_audio_compression_info(ret->track_info, i,
                                             &ret->audio_streams[i].com.ci))
      {
      gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Audio stream %d cannot be read compressed", i+1);
      ret->audio_streams[i].com.action = STREAM_ACTION_TRANSCODE;
      continue;
      }
    
    /* Check if we can write compressed data */
    if(!bg_encoder_writes_compressed_audio(ret->enc,
                                           gavl_track_get_audio_format(ret->track_info, i),
                                           &ret->audio_streams[i].com.ci))
      {
      gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Audio stream %d cannot be written compressed", i+1);
      ret->audio_streams[i].com.action = STREAM_ACTION_TRANSCODE;

      gavl_compression_info_dump(&ret->audio_streams[i].com.ci);

      continue;
      }

    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Copying compressed audio stream %d", i+1);
    //    bg_dprintf("Copying compressed audio stream %d\n", i+1);
    //    gavl_compression_info_dump(&ret->audio_streams[i].com.ci);
    }
  for(i = 0; i < ret->num_video_streams; i++)
    {
    if(ret->video_streams[i].com.action != STREAM_ACTION_COPY)
      continue;
    
    /* Check if we can read compressed data */
    if(!gavl_track_get_video_compression_info(ret->track_info, i,
                                             &ret->video_streams[i].com.ci))
      {
      gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Video stream %d cannot be read compressed", i+1);
      ret->video_streams[i].com.action = STREAM_ACTION_TRANSCODE;
      continue;
      }
    
    /* Check if we need to blend text subtitles */
    for(j = 0; j < ret->num_text_streams; j++)
      {
      if((ret->text_streams[j].com.com.action == STREAM_ACTION_BLEND) &&
         (ret->text_streams[j].com.video_stream == i))
        {
        gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN,
               "Not copying video stream %d: Will blend subtitles", i+1);
        ret->video_streams[i].com.action = STREAM_ACTION_TRANSCODE;
        break;
        }
      }
    if(ret->video_streams[i].com.action != STREAM_ACTION_COPY)
      continue;
    
    /* Check if we need to blend overlay subtitles */
    for(j = 0; j < ret->num_overlay_streams; j++)
      {
      if((ret->overlay_streams[j].com.action == STREAM_ACTION_BLEND) &&
         (ret->overlay_streams[j].video_stream == i))
        {
        gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN,
               "Not copying video stream %d: Will blend subtitles", i+1);
        ret->video_streams[i].com.action = STREAM_ACTION_TRANSCODE;
        }
      }
    if(ret->video_streams[i].com.action != STREAM_ACTION_COPY)
      continue;


    /* Check if we can write compressed data */
    if(!bg_encoder_writes_compressed_video(ret->enc,
                                           gavl_track_get_video_format(ret->track_info, i),
                                           &ret->video_streams[i].com.ci))
      {
      gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Video stream %d cannot be written compressed", i+1);
      ret->video_streams[i].com.action = STREAM_ACTION_TRANSCODE;
      continue;
      }

    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Copying compressed video stream %d", i+1);
    //    bg_dprintf("Copying compressed video stream %d\n", i+1);
    //    gavl_compression_info_dump(&ret->video_streams[i].com.ci);
    }

  /* Overlay subtitles */
  for(i = 0; i < ret->num_overlay_streams; i++)
    {
    if(!(ret->overlay_streams[i].com.action == STREAM_ACTION_COPY))
      continue;

    /* Check if we can read compressed data */
    if(!gavl_track_get_overlay_compression_info(ret->track_info, i,
                                                &ret->overlay_streams[i].com.ci))
      {
      gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Overlay stream %d cannot be read compressed", i+1);
      ret->overlay_streams[i].com.action = STREAM_ACTION_TRANSCODE;
      continue;
      }
    
    /* Check if we can write compressed data */
    if(!bg_encoder_writes_compressed_overlay(ret->enc,
                                           gavl_track_get_overlay_format(ret->track_info, i),
                                           &ret->overlay_streams[i].com.ci))
      {
      gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Overlay stream %d cannot be written compressed", i+1);
      ret->overlay_streams[i].com.action = STREAM_ACTION_TRANSCODE;
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
    init_audio_stream(&ret->audio_streams[i],
                      gavl_track_get_audio_stream(track, i),
                      i);
    if(ret->audio_streams[i].com.action == STREAM_ACTION_TRANSCODE)
      ret->num_audio_streams_real++;
    }

  
  for(i = 0; i < ret->num_video_streams; i++)
    {
    ret->video_streams[i].com.t = ret;
    init_video_stream(&ret->video_streams[i],
                      gavl_track_get_video_stream(track, i),
                      i);
    if(ret->video_streams[i].com.action == STREAM_ACTION_TRANSCODE)
      ret->num_video_streams_real++;
    }

  
  for(i = 0; i < ret->num_text_streams; i++)
    {
    ret->text_streams[i].com.com.t = ret;
    init_text_stream(&ret->text_streams[i],
                     gavl_track_get_text_stream(track, i),
                     i);

    if(ret->text_streams[i].com.com.action == STREAM_ACTION_TRANSCODE)
      ret->num_text_streams_real++;
    else if(ret->text_streams[i].com.com.action == STREAM_ACTION_TRANSCODE_OVERLAY)
      ret->num_overlay_streams_real++;
    }
  
  for(i = 0; i < ret->num_overlay_streams; i++)
    {
    ret->overlay_streams[i].com.t = ret;
    init_overlay_stream(&ret->overlay_streams[i],
                        gavl_track_get_overlay_stream(track, i),
                        i);
    if(ret->overlay_streams[i].com.action == STREAM_ACTION_TRANSCODE)
      ret->num_overlay_streams_real++;
    }
  }

static int start_input(bg_transcoder_t * ret)
  {
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
  
  bg_input_plugin_start(ret->in_handle);
  
  /* Check if we must seek to the start position */

  if(ret->start_time != GAVL_TIME_UNDEFINED)
    {
    if(!gavl_track_can_seek(ret->track_info))
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Cannot seek to start point");
      goto fail;
      }

    bg_input_plugin_seek(ret->in_handle, &ret->start_time, GAVL_TIME_SCALE);
    
    /* This happens, if the decoder reached EOF during the seek */

    if(ret->start_time == GAVL_TIME_UNDEFINED)
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Cannot seek to start point");
      goto fail;
      }
    }

  /* Check, if the user entered bullshit */

  if((ret->start_time != GAVL_TIME_UNDEFINED) &&
     (ret->end_time != GAVL_TIME_UNDEFINED) &&
     (ret->end_time < ret->start_time))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "End time if before start time");
    goto fail;
    }
  

  return 1;
  fail:
  return 0;
  }

static void check_passes(bg_transcoder_t * ret)
  {
  int i;
  ret->total_passes = 1;

  for(i = 0; i < ret->num_audio_streams; i++)
    {
    if((ret->audio_streams[i].com.action == STREAM_ACTION_TRANSCODE) &&
       ret->audio_streams[i].normalize)
      {
      ret->total_passes = 2;
      return;
      }
    }

  for(i = 0; i < ret->num_video_streams; i++)
    {
    if((ret->video_streams[i].com.action == STREAM_ACTION_TRANSCODE) &&
       ret->video_streams[i].twopass)
      {
      ret->total_passes = 2;
      return;
      }
    }
  }

static int setup_pass(bg_transcoder_t * ret)
  {
  int i;
  int result = 0;
  
  for(i = 0; i < ret->num_audio_streams; i++)
    {
    /* Reset the samples already decoded */
    ret->audio_streams[i].samples_read = 0;

    if(ret->audio_streams[i].com.action == STREAM_ACTION_COPY)
      {
      if(ret->pass == ret->total_passes)
        {
        ret->audio_streams[i].com.do_copy = 1;
        result = 1;
        }
      else
        ret->audio_streams[i].com.do_copy = 0;
      }
    else if(ret->audio_streams[i].com.action != STREAM_ACTION_TRANSCODE)
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
    if(ret->video_streams[i].com.action == STREAM_ACTION_COPY)
      {
      if(ret->pass == ret->total_passes)
        {
        ret->video_streams[i].com.do_copy = 1;
        result = 1;
        }
      else
        ret->video_streams[i].com.do_copy = 0;
      }
    else if(ret->video_streams[i].com.action != STREAM_ACTION_TRANSCODE)
      {
      ret->video_streams[i].com.do_decode = 0;
      ret->video_streams[i].com.do_encode = 0;
      }
    else if((ret->pass == ret->total_passes) || ret->video_streams[i].twopass)
      {
      ret->video_streams[i].com.do_decode = 1;
      ret->video_streams[i].com.do_encode = 1;
      result = 1;
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
      case STREAM_ACTION_FORGET:
        ret->text_streams[i].com.com.do_decode = 0;
        ret->text_streams[i].com.com.do_encode = 0;
        break;
      case STREAM_ACTION_BLEND:
        if((ret->video_streams[ret->text_streams[i].com.video_stream].twopass) ||
           (ret->pass == ret->total_passes))
          {
          ret->text_streams[i].com.com.do_decode = 1;
          ret->text_streams[i].com.com.do_encode = 0;
          }
        else
          {
          ret->text_streams[i].com.com.do_decode = 0;
          ret->text_streams[i].com.com.do_encode = 0;
          }
        result = 1;
        break;
      case STREAM_ACTION_TRANSCODE:
      case STREAM_ACTION_TRANSCODE_OVERLAY:
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
        ret->text_streams[i].com.com.action = STREAM_ACTION_FORGET;
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
      case STREAM_ACTION_FORGET:
        ret->overlay_streams[i].com.do_decode = 0;
        ret->overlay_streams[i].com.do_encode = 0;
        break;
      case STREAM_ACTION_BLEND:
        if(ret->video_streams[ret->overlay_streams[i].video_stream].com.do_encode)
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
      case STREAM_ACTION_TRANSCODE:
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
      case STREAM_ACTION_COPY:
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
        ret->overlay_streams[i].com.action = STREAM_ACTION_FORGET;
        break;
      }
    
    if(!ret->overlay_streams[i].com.do_decode)
      ret->overlay_streams[i].com.status = STREAM_STATE_OFF;
    else
      ret->overlay_streams[i].com.status = STREAM_STATE_ON;
    }
  return result;
  }

static int create_output_file_cb(void * priv, const char * filename)
  {
  bg_transcoder_t * t = priv;

  if(!strcmp(filename, t->location))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Input and output are the same file");
    return 0;
    }
  
  if(t->pass == t->total_passes)
    add_output_file(t, filename);
  
  return 1;
  }

static void create_encoder(bg_transcoder_t * ret)
  {
  ret->enc = bg_encoder_create(NULL,
                               ret->transcoder_track,
                               GAVL_STREAM_AUDIO |
                               GAVL_STREAM_VIDEO |
                               GAVL_STREAM_TEXT |
                               GAVL_STREAM_OVERLAY,
                               BG_PLUGIN_FILE);
  }

static int init_encoder(bg_transcoder_t * ret)
  {
  int i;
  char * tmp_string;
  
  ret->cb.create_output_file = create_output_file_cb;
  ret->cb.data = ret;
  
  bg_encoder_set_callbacks(ret->enc, &ret->cb);
  
  tmp_string = bg_sprintf("%s/%s", ret->output_directory, ret->name);
  
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
      {
      add_video_stream(&ret->video_streams[i], ret);
      set_video_pass(ret, i);
      }
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
  
  /* Create frames */
  if(!ret->frame)
    {
    ret->frame = gavl_video_frame_create(&ret->out_format);
    // fprintf(stderr, "Created video frame %p %p\n", ret->frame, ret->frame->planes[0]);
    }
  gavl_video_frame_clear(ret->frame, &ret->out_format);
  
  return 1;
  }

static void subtitle_init_blend(subtitle_stream_t * ss, video_stream_t * vs)
  {
  vs->subtitle_streams =
    realloc(vs->subtitle_streams,
            sizeof(*(vs->subtitle_streams))*(vs->num_subtitle_streams+1));
  vs->subtitle_streams[vs->num_subtitle_streams] = ss;
  vs->num_subtitle_streams++;

  /* Check whether to initialize the text renderer */
  if(ss->com.type == STREAM_TYPE_TEXT)
    {
    text_stream_t * sst = (text_stream_t*)ss;

    ss->vsrc = bg_text_renderer_connect(sst->textrenderer,
                                        sst->com.com.psrc,
                                        &vs->out_format,
                                        &ss->in_format);
    }
  
  gavl_overlay_blend_context_init(ss->blend_context,
                                  &vs->out_format, &ss->in_format);

  bg_subtitle_handler_init(ss->sh,
                           &vs->out_format,
                           ss->vsrc,
                           gavl_overlay_blend_context_get_sink(ss->blend_context));
  
  gavl_video_format_copy(&ss->out_format, &vs->out_format);
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
  text_stream_t * sst;

  bg_encoder_get_overlay_format(t->enc, ss->com.out_index, &ss->out_format);
  /* Check whether to initialize the text renderer */
  if(ss->com.type == STREAM_TYPE_TEXT)
    {
    video_stream_t * vs;
    sst = (text_stream_t*)ss;
    
    if(ss->video_stream < t->num_video_streams)
      vs = &t->video_streams[ss->video_stream];
    else
      vs = NULL;

    /* We force the text renderer to produce the overlays in the format
       the output plugin wants. */
    
    gavl_video_format_copy(&ss->in_format, &ss->out_format);
    
    ss->vsrc = bg_text_renderer_connect(sst->textrenderer,
                                        ss->com.psrc,
                                        vs ? &vs->out_format : NULL,
                                        &ss->in_format);
    
    // gavl_video_format_copy(&ss->in_format, gavl_video_source_get_src_format(ss->vsrc));
    
    }
  
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

    /* These could be left over from the last pass */
    if(vs->subtitle_streams)
      {
      free(ret->video_streams[i].subtitle_streams);
      ret->video_streams[i].subtitle_streams = NULL;
      ret->video_streams[i].num_subtitle_streams = 0;
      }
    }

  for(i = 0; i < ret->num_text_streams; i++)
    {
    if(!ret->text_streams[i].com.com.do_decode)
      continue;

    switch(ret->text_streams[i].com.com.action)
      {
      case STREAM_ACTION_BLEND:
        subtitle_init_blend((subtitle_stream_t*)(&ret->text_streams[i]),
                            &ret->video_streams[ret->text_streams[i].com.video_stream]);
        break;
      case STREAM_ACTION_TRANSCODE:
        subtitle_init_encode_text((&ret->text_streams[i]), ret);
        break;
      case STREAM_ACTION_TRANSCODE_OVERLAY:
        subtitle_init_encode_overlay((subtitle_stream_t*)&ret->text_streams[i], ret);
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
      case STREAM_ACTION_BLEND:
        subtitle_init_blend(os, &ret->video_streams[os->video_stream]);
        break;
      case STREAM_ACTION_TRANSCODE:
        subtitle_init_encode_overlay(os, ret);
        break;
      case STREAM_ACTION_COPY:
        subtitle_init_copy_overlay(os, ret);
        break;
      }
    }
  return 1;
  }

static void init_normalize(bg_transcoder_t * ret)
  {
  int i;
  double absolute;
  double volume_dB = 0.0;
  for(i = 0; i < ret->num_audio_streams; i++)
    {
    if(!ret->audio_streams[i].normalize)
      continue;

    if(ret->pass == 1)
      {
      gavl_peak_detector_set_format(ret->audio_streams[i].peak_detector,
                                    &ret->audio_streams[i].out_format);
      }
    else if(ret->pass == ret->total_passes)
      {
      gavl_volume_control_set_format(ret->audio_streams[i].volume_control,
                                     &ret->audio_streams[i].out_format);
      /* Set the volume */
      gavl_peak_detector_get_peak(ret->audio_streams[i].peak_detector,
                                  NULL, NULL, &absolute);
      if(absolute == 0.0)
        {
        gavl_volume_control_set_volume(ret->audio_streams[i].volume_control, volume_dB);
        gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Zero peaks detected (silent file?). Disabling normalization.");
        }
      else
        {
        volume_dB = - 20.0 * log10(absolute);
        gavl_volume_control_set_volume(ret->audio_streams[i].volume_control, volume_dB);
        gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Correcting volume by %.2f dB", volume_dB);
        }
      }
    }
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
                                      bg_sprintf("%s-99-99", var1));
  
  bg_transcoder_track_get_duration(track, &duration, NULL);

  if(duration != GAVL_TIME_UNDEFINED)
    gavl_dictionary_set_long(&ret->metadata, GAVL_META_APPROX_DURATION, duration);
  
  ret->name = gavl_strrep(ret->name, bg_transcoder_track_get_name(track));

  gavl_metadata_get_src(&ret->metadata, GAVL_META_SRC, 0, NULL, &ret->location);
  
  /* Postprocess only: Send messages and return */
  if(ret->pp_only)
    {
    ret->output_filename = gavl_strrep(ret->output_filename, ret->location);
    
    send_init_messages(ret);
    ret->state = STREAM_STATE_FINISHED;
    return 1;
    }
  
  
  /* Open input plugin */
  if(!open_input(ret))
    goto fail;

  create_encoder(ret);
  
  create_streams(ret, track);

  check_compressed(ret);
  
  /* Check how many passes we must do */
  check_passes(ret);
  ret->pass = 1;

  /* Set up this pass */
  if(!setup_pass(ret))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "No stream to encode");
    goto fail;
    }
  
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

  /* Init normalizing */
  init_normalize(ret);
  
  /* Send messages */
  send_init_messages(ret);  
  
  /* Set duration */

  if(ret->set_start_time && ret->set_end_time)
    ret->duration = ret->end_time - ret->start_time;
  else if(ret->set_start_time)
    ret->duration = gavl_track_get_duration(ret->track_info) - ret->start_time;
  else if(ret->set_end_time)
    ret->duration = ret->end_time;
  else
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
  if(s->volume_control)
    gavl_volume_control_destroy(s->volume_control);
  if(s->peak_detector)
    gavl_peak_detector_destroy(s->peak_detector);
  
  bg_gavl_audio_options_free(&s->options);
  }

static void cleanup_video_stream(video_stream_t * s)
  {
  cleanup_stream(&s->com);

  /* Free all resources */

  if(s->frame)
    {
    // fprintf(stderr, "gavl_video_frame_destroy %p %p\n", s->frame, s->frame->planes[0]);
    gavl_video_frame_destroy(s->frame);
    }
  if(s->fc)
    bg_video_filter_chain_destroy(s->fc);
  
  if(s->subtitle_streams)
    free(s->subtitle_streams);
  
  /* Delete stats file */
  if(s->stats_file)
    {
    remove(s->stats_file);
    free(s->stats_file);
    }
  bg_gavl_video_options_free(&s->options);
  }

static void cleanup_subtitle_stream(subtitle_stream_t * s)
  {
  text_stream_t * ts;
  cleanup_stream(&s->com);
  if(s->blend_context)
    gavl_overlay_blend_context_destroy(s->blend_context);
  if(s->sh)
    bg_subtitle_handler_destroy(s->sh);
  if(s->com.type == STREAM_TYPE_TEXT)
    {
    ts = (text_stream_t*)s;
    if(ts->textrenderer) bg_text_renderer_destroy(ts->textrenderer);
    }
  }

static void reset_streams(bg_transcoder_t * t)
  {
  int i;
  for(i = 0; i < t->num_audio_streams; i++)
    {
    t->audio_streams[i].samples_read = 0;
    t->audio_streams[i].com.time = 0;
    }
  for(i = 0; i < t->num_video_streams; i++)
    {
    t->video_streams[i].frames_written = 0;
    t->video_streams[i].com.time = 0;
    }
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

/* Switch to next pass */

static void next_pass(bg_transcoder_t * t)
  {
  char * tmp_string;

  bg_encoder_destroy(t->enc, 1);
  
  close_input(t);
  t->pass++;
  
  /* Reset stream times */
  reset_streams(t);

  gavl_timer_stop(t->timer);
  gavl_timer_set(t->timer, 0);
  t->percentage_done = 0.0;
  t->time = 0;
  t->last_seconds = 0.0;
  
  open_input(t);
  create_encoder(t);
  
  /* Decide, which stream will be en/decoded*/
  setup_pass(t);

  start_input(t);

  /* Some streams don't have this already */
  set_input_formats(t);
  
  /* Initialize encoding plugins */
  init_encoder(t);

  /* Set formats */
  init_converters(t);

  /* Init normalizing */
  init_normalize(t);
  
  /* Send message */
  tmp_string = bg_sprintf(TR("Transcoding %s [Pass %d/%d]"),
                          t->location, t->pass, t->total_passes);
  
  bg_transcoder_send_msg_start(t->message_hub, tmp_string);
  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "%s", tmp_string);
  free(tmp_string);

  gavl_timer_start(t->timer);
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

  if(t->pp_only)
    {
    t->state = TRANSCODER_STATE_FINISHED;
    bg_transcoder_send_msg_finished(t->message_hub);
    log_transcoding_time(t);
    return 0;
    }
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

  /* Detecting EOF for subtitles doesn't work when we transcode until
     a specified time */
  if(t->end_time == GAVL_TIME_UNDEFINED)
    {
    for(i = 0; i < t->num_text_streams; i++)
      {
      if(t->text_streams[i].com.com.status != STREAM_STATE_ON)
        continue;
      done = 0;
      }
    for(i = 0; i < t->num_overlay_streams; i++)
      {
      if(t->overlay_streams[i].com.status != STREAM_STATE_ON)
        continue;
      done = 0;
      }
    }
  
  if(done)
    {
    if(t->pass < t->total_passes)
      {
      log_transcoding_time(t);
      
      next_pass(t);
      return 1;
      }
    else
      {
      t->state = TRANSCODER_STATE_FINISHED;
      bg_transcoder_send_msg_finished(t->message_hub);
      log_transcoding_time(t);
      return 0;
      }
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
    if(stream->type == STREAM_TYPE_AUDIO)
      {
      if(!audio_iteration((audio_stream_t*)stream, t))
        {
        t->state = TRANSCODER_STATE_ERROR;
        bg_transcoder_send_msg_error(t->message_hub);
        return 0;
        }
      }
    
    else if(stream->type == STREAM_TYPE_VIDEO)
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
    if((t->video_streams[i].com.action != STREAM_ACTION_FORGET) && !do_delete)
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
    if((t->audio_streams[i].com.action != STREAM_ACTION_FORGET) && !do_delete)
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

