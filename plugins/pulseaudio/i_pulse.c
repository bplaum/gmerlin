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



#include <pulseaudio_common.h>
#include <string.h>

#include <gmerlin/utils.h>

#define INIT_SAMPLES 16

static gavl_source_status_t read_func_pulse(void * p, gavl_audio_frame_t ** frame)
  {
  bg_pa_recorder_t * priv;
  int error = 0;

  gavl_audio_frame_t * f = *frame;

  //  fprintf(stderr, "Read pulse 1\n");
  
  priv = p;
  if(pa_simple_read(priv->com.pa, f->samples.u_8,
                    priv->com.block_align * priv->com.format.samples_per_frame,
                    &error) < 0)
    {
    return GAVL_SOURCE_EOF;
    }

  f->timestamp = priv->timestamp;
  f->valid_samples = priv->com.format.samples_per_frame;

  priv->timestamp += f->valid_samples;

  //  fprintf(stderr, "Read pulse 2: %"PRId64"\n", f->timestamp);
  
  return GAVL_SOURCE_OK;
  }

static void start_pulse(bg_pa_recorder_t * priv)
  {
  int error = 0;
  char * buffer = malloc(priv->com.block_align * INIT_SAMPLES);

  if(pa_simple_read(priv->com.pa, buffer,
                    priv->com.block_align * INIT_SAMPLES,
                    &error) < 0)
    {
    fprintf(stderr, "Couldn't get first samples\n");
    }
  else
    fprintf(stderr, "Reading initialized\n");
  
  }

static int handle_cmd(void * data, gavl_msg_t * msg)
  {
  bg_pa_recorder_t * priv;
  priv = data;
  
  switch(msg->NS)
    {
    case GAVL_MSG_NS_SRC:
      switch(msg->ID)
        {
        case GAVL_CMD_SRC_START:
          /* Start */
          //          fprintf(stderr, "i_pulse: Got start command\n");
          start_pulse(priv);
          break;
        case GAVL_CMD_SRC_PAUSE:
          break;
        case GAVL_CMD_SRC_RESUME:
          break;
        }
    }
  return 1;
  }

static int open_pulse(void * data, const char * location)
  {
  bg_pa_recorder_t * priv;
  char * server = NULL;
  char * path = NULL;
  gavl_dictionary_t * t;
  gavl_dictionary_t * s;
  gavl_dictionary_t * m;
  bg_media_source_stream_t * st;
    
  priv = data;
  
  // gavl_audio_format_copy(&priv->format, format);

  if(!gavl_url_split(location,
                     NULL,
                     NULL,
                     NULL,
                     &server,
                     NULL,
                     &path))
    {
    return 0;
    }

  if(!strlen(server) || gavl_host_is_us(server))
    {
    free(server);
    server = NULL;
    }
  
  if(!strcmp(path, "/"))
    {
    if(!bg_pa_open(&priv->com, server, NULL, 1))
      return 0;
    }
  else
    {
    if(!bg_pa_open(&priv->com, server, path + 1 /* skip '/' */, 1))
      return 0;
    }
  
  t = gavl_append_track(&priv->mi, NULL);
  m = gavl_track_get_metadata_nc(t);

  gavl_metadata_add_src(m, GAVL_META_SRC, NULL, location);

  s = gavl_track_append_audio_stream(t);
  gavl_audio_format_copy(gavl_stream_get_audio_format_nc(s), &priv->com.format);
  
  gavl_dictionary_set_string(m, GAVL_META_CLASS, GAVL_META_CLASS_AUDIO_RECORDER);

  bg_media_source_set_from_track(&priv->source, t);

  st = bg_media_source_get_audio_stream(&priv->source, 0);
  
  st->asrc_priv = gavl_audio_source_create(read_func_pulse, priv, 0, &priv->com.format);
  st->asrc = st->asrc_priv;
  
  priv->timestamp = 0;
  return 1;
  }

static void close_pulse(void * p)
  {
  bg_pa_recorder_t * priv = p;
  bg_pa_close_common(&priv->com);
  
  bg_media_source_cleanup(&priv->source);
  gavl_dictionary_free(&priv->mi);
  }

static bg_media_source_t * get_source_pulse(void * p)
  {
  bg_pa_recorder_t * priv = p;
  return &priv->source;
  }


static const bg_parameter_info_t parameters[] =
  {
    {
      .name =        "channel_mode",
      .long_name =   TRS("Channel Mode"),
      .type =        BG_PARAMETER_STRINGLIST,
      .val_default = GAVL_VALUE_INIT_STRING("stereo"),
      .multi_names =   (char const *[]){ "mono", "stereo", NULL },
      .multi_labels =  (char const *[]){ TRS("Mono"), TRS("Stereo"), NULL },
    },
    {
      .name =        "bits",
      .long_name =   TRS("Bits"),
      .type =        BG_PARAMETER_STRINGLIST,
      .val_default = GAVL_VALUE_INIT_STRING("16"),
      .multi_names =     (char const *[]){ "8", "16", NULL },
    },
    {
      .name =        "samplerate",
      .long_name =   TRS("Samplerate [Hz]"),
      .type =        BG_PARAMETER_INT,
      .val_default = GAVL_VALUE_INIT_INT(44100),
      .val_min =     GAVL_VALUE_INIT_INT(8000),
      .val_max =     GAVL_VALUE_INIT_INT(96000),
    },
    { },
  };

static const bg_parameter_info_t * get_parameters_pulse(void * data)
  {
  return parameters;
  }

static void
set_parameter_pulse(void * p, const char * name,
                    const gavl_value_t * val)
  {
  bg_pa_recorder_t * priv = p;
  
  if(!name)
    return;
  
  if(!strcmp(name, "channel_mode"))
    {
    if(!strcmp(val->v.str, "mono"))
      priv->com.num_channels = 1;
    else if(!strcmp(val->v.str, "stereo"))
      priv->com.num_channels = 2;
    }
  else if(!strcmp(name, "bits"))
    {
    if(!strcmp(val->v.str, "8"))
      priv->com.bytes_per_sample = 1;
    else if(!strcmp(val->v.str, "16"))
      priv->com.bytes_per_sample = 2;
    }
  else if(!strcmp(name, "samplerate"))
    {
    priv->com.samplerate = val->v.i;
    }
  }

static void * create_pulse(void)
  {
  bg_pa_recorder_t * p = calloc(1, sizeof(*p));
  
  bg_controllable_init(&p->com.ctrl, bg_msg_sink_create(handle_cmd, p, 1),
                       bg_msg_hub_create(1));
  return p;
  }

static void destroy_pulse(void * priv)
  {
  bg_pa_recorder_t * p = priv;
  close_pulse(p);
  bg_controllable_cleanup(&p->com.ctrl);
  free(p);
  }

static gavl_dictionary_t * get_media_info_pulse(void * priv)
  {
  bg_pa_recorder_t * p = priv;
  return &p->mi;
  }

static char const * const protocols = "pulseaudio-source";

static const char * get_protocols_pulse(void * priv)
  {
  return protocols;
  }



const bg_input_plugin_t the_plugin =
  {
    .common =
    {
      BG_LOCALE,
      .name =          "i_pulse",
      .long_name =     TRS("Pulseaudio"),
      .description =   TRS("PulseAudio capture"),
      .type =          BG_PLUGIN_INPUT,
      .flags =         0,
      .priority =      BG_PLUGIN_PRIORITY_MAX,
      .create =        create_pulse,
      .destroy =       destroy_pulse,

      .get_parameters = get_parameters_pulse,
      .set_parameter =  set_parameter_pulse,
      .get_controllable = bg_pa_get_controllable,
    },
    
    .get_media_info  = get_media_info_pulse,
    .get_src       = get_source_pulse,
    .get_protocols = get_protocols_pulse,
    .open          = open_pulse,
    .close         = close_pulse,
  };

/* Include this into all plugin modules exactly once
   to let the plugin loader obtain the API version */
BG_GET_PLUGIN_API_VERSION;
