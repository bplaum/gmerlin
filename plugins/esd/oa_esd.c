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
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include <config.h>
#include <gmerlin/translation.h>

#include <gmerlin/plugin.h>
#include <gmerlin/utils.h>
#include <esd.h>

typedef struct
  {
  int esd_socket;
  char * hostname;
  int block_align;
  int esd_format;
  int samplerate;

  gavl_audio_sink_t * sink;
  } esd_t;

static const bg_parameter_info_t parameters[] =
  {
    {
      .name =      "esd_host",
      .long_name = TRS("Host (empty: local)"),
      .type =      BG_PARAMETER_STRING,
    },
    { /* End of parameters */ }
  };

static void set_parameter_esd(void * data, const char * name,
                              const gavl_value_t * val)
  {
  upnp_t * e = data;

  if(!name)
    return;
  
  if(!strcmp(name, "esd_host"))
    {
    e->hostname = gavl_strrep(e->hostname, val->v.str);
    }
  }

static void * create_upnp()
  {
  upnp_t * ret = calloc(1, sizeof(*ret));

  return ret;
  }

static void destroy_upnp(void *data)
  {
  esd_t * e = data;

  if(e->hostname)
    free(e->hostname);
  free(e);
  }

static gavl_sink_status_t
write_func_esd(void * p, gavl_audio_frame_t * f)
  {
  esd_t * e = p;
  if(write(e->esd_socket, f->channels.s_8[0],
           f->valid_samples * e->block_align) < 0)
    return GAVL_SINK_ERROR;
  return GAVL_SINK_OK;
  }

static int open_esd(void * data, gavl_audio_format_t * format)
  {
  esd_t * e = data;
  e->block_align = 1;

  format->interleave_mode = GAVL_INTERLEAVE_ALL;
    
  e->esd_format = ESD_STREAM | ESD_PLAY;
  
  /* Delete unwanted channels */

  switch(format->num_channels)
    {
    case 1:
      e->esd_format |= ESD_MONO;
      format->num_channels = 1;
      format->channel_locations[0] = GAVL_CHID_FRONT_CENTER;
      break;
    default:
      e->block_align *= 2;
      e->esd_format |= ESD_STEREO;
      format->num_channels = 2;
      format->channel_locations[0] = GAVL_CHID_FRONT_LEFT;
      format->channel_locations[1] = GAVL_CHID_FRONT_RIGHT;
      break;
    }
  /* Set bits */
  
  if(gavl_bytes_per_sample(format->sample_format)==1)
    {
    format->sample_format = GAVL_SAMPLE_U8;
    e->esd_format |= ESD_BITS8;
    }
  else
    {
    format->sample_format = GAVL_SAMPLE_S16;
    e->esd_format |= ESD_BITS16;
    e->block_align *= 2;
    }

  e->samplerate = format->samplerate;
  
  e->sink = gavl_audio_sink_create(NULL, write_func_esd, e, format);
  return 1;
  }

static gavl_audio_sink_t * get_sink_esd(void * p)
  {
  esd_t * e = p;
  return e->sink;
  }

// static int stream_count = 0;

static int start_esd(void * p)
  {
  esd_t * e = p;
  e->esd_socket = esd_play_stream(e->esd_format,
                                  e->samplerate,
                                  e->hostname,
                                  "gmerlin output");
  if(e->esd_socket < 0)
    return 0;
  return 1;
  }

static void write_esd(void * p, gavl_audio_frame_t * f)
  {
  esd_t * e = p;
  gavl_audio_sink_put_frame(e->sink, f);
  }

static void close_esd(void * p)
  {
  esd_t * e = p;
  if(e->sink)
    {
    gavl_audio_sink_destroy(e->sink);
    e->sink = NULL;
    }
  }

static void stop_esd(void * p)
  {
  esd_t * e = p;
  esd_close(e->esd_socket);
  }

static const bg_parameter_info_t *
get_parameters_esd(void * priv)
  {
  return parameters;
  }

const bg_oa_plugin_t the_plugin =
  {
    .common =
    {
      BG_LOCALE,
      .name =          "oa_esd",
      .long_name =     TRS("EsounD output driver"),
      .description =   TRS("EsounD output driver"),
      .type =          BG_PLUGIN_OUTPUT_AUDIO,
      .flags =         BG_PLUGIN_PLAYBACK,
      .priority =      BG_PLUGIN_PRIORITY_MIN,
      .create =        create_esd,
      .destroy =       destroy_esd,

      .get_parameters = get_parameters_esd,
      .set_parameter =  set_parameter_esd
    },
    .open =          open_esd,
    .start =         start_esd,
    .write_audio =   write_esd,
    .stop =          stop_esd,
    .close =         close_esd,
    .get_sink =      get_sink_esd,
  };

/* Include this into all plugin modules exactly once
   to let the plugin loader obtain the API version */
BG_GET_PLUGIN_API_VERSION;
