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



#include <unistd.h>
#include <string.h>

#include <pulseaudio_common.h>
#include <gavl/log.h>
#define LOG_DOMAIN "oa_pulse"

#include <gavl/utils.h>


static gavl_sink_status_t
write_func_pulse(void * p, gavl_audio_frame_t * f)
  {
  bg_pa_output_t * priv;
  int error;
  priv = p;

  //  fprintf(stderr, "write frame pulse %d\n", f->valid_samples);
  
  pa_simple_write(priv->com.pa,
                  f->samples.u_8,
                  priv->com.block_align * f->valid_samples,
                  &error);
  return GAVL_SINK_OK;
  }

static int open_pulse(void * data, const char * uri,
                      gavl_audio_format_t * format)
  {
  char thishost[HOST_NAME_MAX];
  bg_pa_output_t * priv;

  char * host = NULL;
  char * path = NULL;
  
  priv = data;
  
  if(!gavl_url_split(uri, NULL, NULL, NULL, &host, NULL, &path))
    return 0;

  if(*host == '\0')
    {
    free(host);
    host = NULL;
    }
  
  if(path && ((*path == '\0') || !strcmp(path, "/")))
    {
    free(path);
    path = NULL;
    }
  
  //  if(!host)
  //    host = gavl_strdup("default");

  if(host)
    {
    gethostname(thishost, HOST_NAME_MAX);
    if(!strcmp(thishost, host) || !strcmp(host, "default")) // Revert to default
      {
      free(host);
      host = NULL;
      }
    }
  
  //  if(!path)
  //    path = gavl_strdup("/default");
  
  
  gavl_audio_format_copy(&priv->com.format, format);
  
  if(!bg_pa_open(&priv->com, host, (path ? (path+1) : NULL), 0))
    return 0;
  
  gavl_audio_format_copy(format, &priv->com.format);

  priv->sink = gavl_audio_sink_create(NULL, write_func_pulse, priv,
                                      &priv->com.format);
  
  return 1;
  }

static char const * const protocols = PULSE_SINK_PROTOCOL;

static const char * get_protocols_pulse(void * priv)
  {
  return protocols;
  }


static int start_pulse(void * p)
  {
  return 1;
  }

static void stop_pulse(void * p)
  {
  
  }

static void close_pulse(void * p)
  {
  bg_pa_output_t * priv = p;
  bg_pa_close_common(&priv->com);
  
  if(priv->sink)
    {
    gavl_audio_sink_destroy(priv->sink);
    priv->sink = NULL;
    }
  
  }

static gavl_audio_sink_t * get_sink_pulse(void * p)
  {
  bg_pa_output_t * priv = p;
  return priv->sink;
  }

static int get_delay_pulse(void * p)
  {
  bg_pa_output_t * priv;
  int error;
  int ret;
  priv = p;
  ret = gavl_time_rescale(1000000, priv->com.format.samplerate,
                          pa_simple_get_latency(priv->com.pa, &error));
  return ret;
  }

static void * create_pulse_output()
  {
  bg_pa_output_t * priv;
  priv = calloc(1, sizeof(*priv));
  return priv;
  }

static void destroy_pulse_output(void * priv)
  {
  bg_pa_output_t * p = priv;
  bg_controllable_cleanup(&p->com.ctrl);

  
  free(p);
  }

const bg_oa_plugin_t the_plugin =
  {
    .common =
    {
      BG_LOCALE,
      .name =          "oa_pulse",
      .long_name =     TRS("PulseAudio"),
      .description =   TRS("PulseAudio output"),
      .type =          BG_PLUGIN_OUTPUT_AUDIO,
      .flags =         0,
      .priority =      BG_PLUGIN_PRIORITY_MAX,
      .create =        create_pulse_output,
      .destroy =       destroy_pulse_output,
      
      .get_protocols = get_protocols_pulse,
    },

    .open =          open_pulse,
    .get_sink =      get_sink_pulse,
    .start =         start_pulse,
    .stop =          stop_pulse,
    .close =         close_pulse,
    .get_delay =     get_delay_pulse,
  };

/* Include this into all plugin modules exactly once
   to let the plugin loader obtain the API version */
BG_GET_PLUGIN_API_VERSION;
