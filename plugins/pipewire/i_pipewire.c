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

#include <gavl/log.h>
#define LOG_DOMAIN "i_pipewire"

#include <gavl/utils.h>

#include <gmerlin/translation.h>
#include <gmerlin/plugin.h>


#include "pipewire_common.h"

typedef struct
  {
  bg_controllable_t ctrl;
  bg_media_source_t src;
  gavl_dictionary_t mi;
  
  struct pw_main_loop *loop;

  struct pw_stream *stream;
  int64_t pts;

  gavl_audio_frame_t * frame;
  gavl_audio_format_t fmt;

  int sample_pos;

  int block_align;

  enum pw_stream_state stream_state;
  
  } bg_pw_recorder_t;

static void start_pulse(bg_pw_recorder_t*);

static gavl_source_status_t read_func_pulse(void * p, gavl_audio_frame_t ** frame)
  {
  int samples_copied;
  
  bg_pw_recorder_t * pw = p;

  (*frame)->valid_samples = 0;

  
  
  while((*frame)->valid_samples < pw->fmt.samples_per_frame)
    {
    if(pw->sample_pos == pw->frame->valid_samples)
      {
      pw->frame->valid_samples = 0;
      pw->sample_pos = 0;
      
      pw_loop_iterate(pw_main_loop_get_loop(pw->loop), -1 /* block until event */);

      if(!pw->frame->valid_samples)
        {
        fprintf(stderr, "Failed to read samples\n");
        
        if(!(*frame)->valid_samples)
          {
          return GAVL_SOURCE_EOF;
          }
        else
          break;
        }
      else
        fprintf(stderr, "Read %d samples\n", pw->frame->valid_samples);
      }
    
    samples_copied = gavl_audio_frame_copy(&pw->fmt,
                                           (*frame),
                                           pw->frame,
                                           (*frame)->valid_samples,
                                           pw->sample_pos,
                                           pw->fmt.samples_per_frame - (*frame)->valid_samples,
                                           pw->frame->valid_samples - pw->sample_pos);
    
    (*frame)->valid_samples += samples_copied;
    pw->sample_pos += samples_copied;
    }

  (*frame)->timestamp = pw->pts;
  pw->pts += (*frame)->valid_samples;
  
  return GAVL_SOURCE_OK;
  }

void start_pulse(bg_pw_recorder_t * p)
  {
  int ret;
  struct spa_audio_info_raw info = { 0 };
  uint8_t buffer[1024];
  const struct spa_pod *params[1]; 

  struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
  
  p->pts = GAVL_TIME_UNDEFINED;
  
  bg_pipewire_audio_format_to_spa(&p->fmt, &info);

  params[0] = spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &info);

  ret = pw_stream_connect(p->stream,
                          PW_DIRECTION_INPUT,
                          PW_ID_ANY,
                          PW_STREAM_FLAG_AUTOCONNECT |
                          PW_STREAM_FLAG_MAP_BUFFERS,
                          //                          PW_STREAM_FLAG_RT_PROCESS,
                          params, 1);
#if 0
  if(ret < 0)
    {
    fprintf(stderr, "pw_stream_connect failed\n");
    }
  else
    fprintf(stderr, "pw_stream_connect succeeded: %d\n", ret);
#endif
  
  while((p->stream_state != PW_STREAM_STATE_STREAMING) &&
        (p->stream_state != PW_STREAM_STATE_ERROR))
    pw_loop_iterate(pw_main_loop_get_loop(p->loop), -1 /* block until event */);

  if(p->stream_state == PW_STREAM_STATE_ERROR)
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Stream reported error");

  }

static int handle_cmd(void * data, gavl_msg_t * msg)
  {
  bg_pw_recorder_t * p = data;
  switch(msg->NS)
    {
    case GAVL_MSG_NS_SRC:
      switch(msg->ID)
        {
        case GAVL_CMD_SRC_START:
          /* Start */
          //          fprintf(stderr, "i_pulse: Got start command\n");
          start_pulse(p);
          break;
        case GAVL_CMD_SRC_PAUSE:
          break;
        case GAVL_CMD_SRC_RESUME:
          break;
        }
    }
  return 1;
  }

static char const * const protocols = PIPEWIRE_SOURCE_PROTOCOL" "PIPEWIRE_MONITOR_PROTOCOL;

static const char * get_protocols_pipewire(void * priv)
  {
  return protocols;
  }

static void * create_pulse(void)
  {
  bg_pw_recorder_t * p = calloc(1, sizeof(*p));
  
  bg_controllable_init(&p->ctrl, bg_msg_sink_create(handle_cmd, p, 1),
                       bg_msg_hub_create(1));

  pw_init(NULL, NULL);

  p->loop = pw_main_loop_new(NULL);
  
  return p;
  }

static void destroy_pulse(void * priv)
  {
  bg_pw_recorder_t * p = priv;
  //  close_pulse(p);
  bg_controllable_cleanup(&p->ctrl);
  
  free(p);
  }

static bg_controllable_t * get_controllable_pipewire(void * priv)
  {
  bg_pw_recorder_t * p = priv;
  return &p->ctrl;
  }

static gavl_dictionary_t * get_media_info_pipewire(void * priv)
  {
  bg_pw_recorder_t * p = priv;
  return &p->mi;
  }

static bg_media_source_t * get_source_pipewire(void * priv)
  {
  bg_pw_recorder_t * p = priv;
  return &p->src;
  }

static void set_pts(bg_pw_recorder_t * p)
  {
  if(p->pts == GAVL_TIME_UNDEFINED)
    {
    struct pw_time pw_time;
    pw_stream_get_time_n(p->stream, &pw_time, sizeof(pw_time));
    p->pts = gavl_time_scale(p->fmt.samplerate, pw_time.now / 1000);
    }
  }

static void on_process_interleaved(void * priv)
  {
  struct spa_buffer *buf;
  bg_pw_recorder_t * p = priv;
  
  
  struct pw_buffer *b = pw_stream_dequeue_buffer(p->stream);

  //  fprintf(stderr, "on_process_interleaved\n");
  
  if(!b)
    return;

  set_pts(p);
  
  //  
  
  buf = b->buffer;
  
  p->frame->valid_samples = buf->datas[0].chunk->size / p->block_align;
  memcpy(p->frame->samples.s_8, buf->datas[0].data, buf->datas[0].chunk->size);

  //  fprintf(stderr, "process %d\n", p->frame->valid_samples);
  
  /* ... write samples directly to your ring buffer / file ... */
  pw_stream_queue_buffer(p->stream, b);
  }

static void on_process_planar(void * priv)
  {
  int i;
  
  struct spa_buffer *buf;
  bg_pw_recorder_t * p = priv;
  
  struct pw_buffer *b = pw_stream_dequeue_buffer(p->stream);

  //  fprintf(stderr, "on_process_planar\n");

  if(!b)
    return;

  set_pts(p);
  
  //  pw_stream_get_time_n();
  
  buf = b->buffer;


  if(buf->n_datas != p->fmt.num_channels)
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Buffers-channels mismatch");

  p->frame->valid_samples = buf->datas[0].chunk->size / p->block_align;

  for(i = 0; i < p->fmt.num_channels; i++)
    memcpy(p->frame->channels.s_8[i], buf->datas[i].data, buf->datas[i].chunk->size);
  
  pw_stream_queue_buffer(p->stream, b);
  }

static void on_state_changed(void *priv, enum pw_stream_state old,
                              enum pw_stream_state state, const char *error)
  {
  bg_pw_recorder_t * p = priv;

  p->stream_state = state;
  
  printf("stream state: %s -> %s%s%s\n",
         pw_stream_state_as_string(old),
         pw_stream_state_as_string(state),
         error ? " error: " : "", error ? error : "");
  }

static const struct pw_stream_events stream_events_interleaved = {
    PW_VERSION_STREAM_EVENTS,
    .process = on_process_interleaved,
    .state_changed = on_state_changed,
};

static const struct pw_stream_events stream_events_planar = {
    PW_VERSION_STREAM_EVENTS,
    .process = on_process_planar,
    .state_changed = on_state_changed,
};


static int open_pipewire(void * priv, const char * location)
  {
  int ret = 0;
  char * name = NULL;
  char * host = NULL;
  bg_pw_recorder_t * p = priv;
  struct pw_properties *props;
  gavl_audio_format_t afmt;
  bg_media_source_stream_t * st;
  char * str;
  int max_buffer_size = 0;

  gavl_dictionary_t * t;
  gavl_dictionary_t * s;
  gavl_dictionary_t * m;

  
  memset(&afmt, 0, sizeof(afmt));
  if(!gavl_url_split(location,
                     NULL, NULL, NULL, 
                     &host, NULL, &name))
    return 0;
  
  if(!name)
    return 0;
  

  /* Build track info */
  t = gavl_append_track(&p->mi, NULL);
  m = gavl_track_get_metadata_nc(t);

  gavl_metadata_add_src(m, GAVL_META_SRC, NULL, location);

  s = gavl_track_append_audio_stream(t);
  
  gavl_dictionary_set_string(m, GAVL_META_CLASS, GAVL_META_CLASS_AUDIO_RECORDER);

  bg_media_source_set_from_track(&p->src, t);

  /* Query device */
  if(!bg_pipewire_query_device(name+1, &afmt, &max_buffer_size, gavl_stream_get_metadata_nc(s))) // Skip "/"
    return 0;
  
  
  props = 
    pw_properties_new(PW_KEY_MEDIA_TYPE,     "Audio",
                      PW_KEY_MEDIA_CATEGORY, "Capture",
                      PW_KEY_MEDIA_ROLE,     "Production",
                      NULL);
  
  str = gavl_sprintf("1/%d", afmt.samplerate);
  pw_properties_set(props, PW_KEY_NODE_RATE, str);
  free(str);
  
  pw_properties_set(props, PW_KEY_NODE_FORCE_RATE, "0");
  pw_properties_set(props, PW_KEY_TARGET_OBJECT, name + 1);

  if(afmt.interleave_mode == GAVL_INTERLEAVE_ALL)
    {
    p->stream = pw_stream_new_simple(pw_main_loop_get_loop(p->loop),
                                     "raw-audio-capture",
                                     props,
                                     &stream_events_interleaved, /* events */
                                     p);             /* data - passed back as `userdata` in every callback */
    p->block_align = gavl_bytes_per_sample(afmt.sample_format) * afmt.num_channels;
    }
  else
    {
    p->stream = pw_stream_new_simple(pw_main_loop_get_loop(p->loop),
                                     "raw-audio-capture",
                                     props,
                                     &stream_events_planar, /* events */
                                     p);             /* data - passed back as `userdata` in every callback */

    p->block_align = gavl_bytes_per_sample(afmt.sample_format);
    }
  
  gavl_audio_format_copy(&p->fmt, &afmt);
  gavl_audio_format_copy(gavl_stream_get_audio_format_nc(s), &p->fmt);
  
  afmt.samples_per_frame = max_buffer_size;
  p->frame = gavl_audio_frame_create(&afmt);


  st = bg_media_source_get_audio_stream(&p->src, 0);
  
  st->asrc_priv = gavl_audio_source_create(read_func_pulse, p, 0, &p->fmt);
  st->asrc = st->asrc_priv;
  

  
  ret = 1;

  //  fail:
  if(host)
    free(host);
  if(name)
    free(name);
  
  return ret;
  }

static void close_pipewire(void * p)
  {
  }

const bg_input_plugin_t the_plugin =
  {
    .common =
    {
      BG_LOCALE,
      .name =          "i_pipewire",
      .long_name =     TRS("Pipewire"),
      .description =   TRS("Pipewire capture"),
      .type =          BG_PLUGIN_INPUT,
      .flags =         0,
      .priority =      BG_PLUGIN_PRIORITY_MAX,
      .create =        create_pulse,
      .destroy =       destroy_pulse,

      //      .get_parameters = get_parameters_pulse,
      //      .set_parameter =  set_parameter_pulse,
      .get_controllable = get_controllable_pipewire,
      .get_protocols = get_protocols_pipewire,
    },
    
    .get_media_info  = get_media_info_pipewire,
    .get_src       = get_source_pipewire,
    .open          = open_pipewire,
    .close         = close_pipewire,
  };

/* Include this into all plugin modules exactly once
   to let the plugin loader obtain the API version */
BG_GET_PLUGIN_API_VERSION;
