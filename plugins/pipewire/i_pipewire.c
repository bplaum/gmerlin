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
#define LOG_DOMAIN "res_pipewire"

#include <gavl/utils.h>

#include <gmerlin/translation.h>
#include <gmerlin/plugin.h>


#include "pipewire_common.h"

typedef struct
  {
  bg_controllable_t ctrl;
  bg_media_source_t src;
  gavl_dictionary_t mi;
  
  //  struct pw_thread_loop *loop;
  struct pw_main_loop *loop;

  struct pw_stream *stream;
  int64_t pts;
  
  } bg_pw_recorder_t;

static void start_pulse(bg_pw_recorder_t*);

static gavl_source_status_t read_func_pulse(void * p, gavl_audio_frame_t ** frame)
  {
  bg_pw_recorder_t * pw = p;
  
  pw_loop_iterate(pw_main_loop_get_loop(pw->loop), -1 /* block until event */);
  
  
  
  return GAVL_SOURCE_EOF;
  }

void start_pulse(bg_pw_recorder_t * p)
  {
  
  }

static int handle_cmd(void * data, gavl_msg_t * msg)
  {
  // bg_pw_recorder_t * priv = data;

  bg_pw_recorder_t * p = data;
  switch(msg->NS)
    {
    case GAVL_MSG_NS_SRC:
      switch(msg->ID)
        {
        case GAVL_CMD_SRC_START:
          /* Start */
          //          fprintf(stderr, "i_pulse: Got start command\n");
          //          start_pulse(priv);
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

static void on_process(void * priv)
  {
  struct spa_buffer *buf;
  bg_pw_recorder_t * p = priv;
  int16_t *samples;
  uint32_t n_samples;
  struct pw_time pw_time;

  
  struct pw_buffer *b = pw_stream_dequeue_buffer(p->stream);
  if(!b)
    return;

  //  pw_stream_get_time_n();

  
  buf = b->buffer;
  samples = buf->datas[0].data;      /* raw S16_LE, untouched */
  n_samples = buf->datas[0].chunk->size / sizeof(int16_t);
  
  /* ... write samples directly to your ring buffer / file ... */
  pw_stream_queue_buffer(p->stream, b);
  }

static const struct pw_stream_events stream_events = {
    PW_VERSION_STREAM_EVENTS,
    .process = on_process,
};


static int open_pipewire(void * priv, const char * location)
  {
  char * name;
  char * host;
  bg_pw_recorder_t * p = priv;
  struct pw_properties *props;
  gavl_audio_format_t afmt;
  memset(&afmt, 0, sizeof(afmt));
  if(!gavl_url_split(location,
                     NULL, NULL, NULL, 
                     &host, NULL, &name))
    return 0;

  if(!name)
    return 0;
    
  
  if(!bg_pipewire_query_device(name+1, &afmt)) // Skip "/"
    return 0;
  
  props = 
    pw_properties_new(PW_KEY_MEDIA_TYPE,     "Audio",
                      PW_KEY_MEDIA_CATEGORY, "Capture",
                      PW_KEY_MEDIA_ROLE,     "Production",
                      PW_KEY_NODE_RATE,      "1/48000",
                      PW_KEY_NODE_FORCE_QUANTUM, "960",   /* force exactly 960 samples/cycle */
                      PW_KEY_NODE_FORCE_RATE,    "48000",
                      NULL);
  
  p->stream = pw_stream_new_simple(pw_main_loop_get_loop(p->loop),
                                   "raw-audio-capture",
                                   props,
                                   &stream_events,   /* events */
                                   p);              /* data - passed back as `userdata` in every callback */
  
  return 1;
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
