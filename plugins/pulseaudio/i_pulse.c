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

#include <pulseaudio_common.h>
#include <string.h>

#include <gmerlin/utils.h>

#define INIT_SAMPLES 16

static gavl_source_status_t read_func_pulse(void * p, gavl_audio_frame_t ** frame)
  {
  bg_pa_t * priv;
  int error = 0;

  gavl_audio_frame_t * f = *frame;

  //  fprintf(stderr, "Read pulse 1\n");
  
  priv = p;
  if(pa_simple_read(priv->pa, f->samples.u_8,
                    priv->block_align * priv->format.samples_per_frame,
                    &error) < 0)
    {
    return GAVL_SOURCE_EOF;
    }

  f->timestamp = priv->timestamp;
  f->valid_samples = priv->format.samples_per_frame;

  priv->timestamp += f->valid_samples;

  //  fprintf(stderr, "Read pulse 2: %"PRId64"\n", f->timestamp);
  
  return GAVL_SOURCE_OK;
  }

static void start_pulse(bg_pa_t * priv)
  {
  int error = 0;
  char * buffer = malloc(priv->block_align * INIT_SAMPLES);

  if(pa_simple_read(priv->pa, buffer,
                    priv->block_align * INIT_SAMPLES,
                    &error) < 0)
    {
    fprintf(stderr, "Couldn't get first samples\n");
    }
  else
    fprintf(stderr, "Reading initialized\n");
  
  }

static int handle_cmd(void * data, gavl_msg_t * msg)
  {
  bg_pa_t * priv;
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

static int open_pulse(void * data,
                      gavl_audio_format_t * format,
                      gavl_video_format_t * video_format,
                      gavl_dictionary_t * m)
  {
  bg_pa_t * priv;
  priv = data;

  // gavl_audio_format_copy(&priv->format, format);
  
  if(!bg_pa_open(priv, 1))
    return 0;
  
  gavl_audio_format_copy(format, &priv->format);
  priv->src = gavl_audio_source_create(read_func_pulse, priv, 0, format);
  priv->timestamp = 0;
  return 1;
  }

static void close_pulse(void * p)
  {
  bg_pa_t * priv = p;
  gavl_audio_source_destroy(priv->src);
  priv->src = NULL;
  
  bg_pa_close(priv);
  }

static gavl_audio_source_t * get_audio_source_pulse(void * p)
  {
  bg_pa_t * priv = p;
  return priv->src;
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
    {
      .name =        "server",
      .long_name =   TRS("Server"),
      .type =        BG_PARAMETER_STRING,
      .help_string = TRS("Server to connect to. Leave empty for default."),
    },
    {
      .name =        "dev",
      .long_name =   TRS("Source"),
      .type =        BG_PARAMETER_STRING,
      .help_string = TRS("Name of the source to open. Call \"pactl list sources\" for available sources."),
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
  bg_pa_t * priv = p;
  
  if(!name)
    return;
  
  if(!strcmp(name, "channel_mode"))
    {
    if(!strcmp(val->v.str, "mono"))
      priv->num_channels = 1;
    else if(!strcmp(val->v.str, "stereo"))
      priv->num_channels = 2;
    }
  else if(!strcmp(name, "bits"))
    {
    if(!strcmp(val->v.str, "8"))
      priv->bytes_per_sample = 1;
    else if(!strcmp(val->v.str, "16"))
      priv->bytes_per_sample = 2;
    }
  else if(!strcmp(name, "samplerate"))
    {
    priv->samplerate = val->v.i;
    }
  else if(!strcmp(name, "dev"))
    {
    priv->dev = gavl_strrep(priv->dev, val->v.str);
    }
  else if(!strcmp(name, "server"))
    {
    priv->server = gavl_strrep(priv->server, val->v.str);
    }
  }

static void * i_pulse_create(void)
  {
  bg_pa_t * p = bg_pa_create();
  bg_controllable_init(&p->ctrl, bg_msg_sink_create(handle_cmd, p, 1),
                       bg_msg_hub_create(1));
  return p;
  }


const bg_recorder_plugin_t the_plugin =
  {
    .common =
    {
      BG_LOCALE,
      .name =          "i_pulse",
      .long_name =     TRS("PulseAudio"),
      .description =   TRS("PulseAudio capture. You can specify the source, where we'll get the audio."),
      .type =          BG_PLUGIN_RECORDER_AUDIO,
      .flags =         0,
      .priority =      BG_PLUGIN_PRIORITY_MAX,
      .create =        i_pulse_create,
      .destroy =       bg_pa_destroy,

      .get_parameters = get_parameters_pulse,
      .set_parameter =  set_parameter_pulse,
      .get_controllable = bg_pa_get_controllable,
    },
    
    .open =          open_pulse,
    .get_audio_source = get_audio_source_pulse,

    .close =         close_pulse,
  };
/* Include this into all plugin modules exactly once
   to let the plugin loader obtain the API version */
BG_GET_PLUGIN_API_VERSION;
