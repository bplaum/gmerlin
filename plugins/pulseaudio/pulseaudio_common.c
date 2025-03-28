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



#include "pulseaudio_common.h"
#include <string.h>

#include <sys/types.h>
#include <unistd.h>

#include <gmerlin/utils.h>

#include <gmerlin/log.h>
#define LOG_DOMAIN "pulse"

static const struct
  {
  gavl_channel_id_t     ga;
  pa_channel_position_t pa;
  }
channels[] =
  {
    { GAVL_CHID_FRONT_CENTER, PA_CHANNEL_POSITION_FRONT_CENTER },
    { GAVL_CHID_FRONT_LEFT,PA_CHANNEL_POSITION_FRONT_LEFT },
    { GAVL_CHID_FRONT_RIGHT,PA_CHANNEL_POSITION_FRONT_RIGHT },
    { GAVL_CHID_FRONT_CENTER_LEFT,PA_CHANNEL_POSITION_FRONT_LEFT_OF_CENTER },
    { GAVL_CHID_FRONT_CENTER_RIGHT,PA_CHANNEL_POSITION_FRONT_RIGHT_OF_CENTER },
    { GAVL_CHID_REAR_LEFT,PA_CHANNEL_POSITION_REAR_LEFT },
    { GAVL_CHID_REAR_RIGHT,PA_CHANNEL_POSITION_REAR_RIGHT },
    { GAVL_CHID_REAR_CENTER,PA_CHANNEL_POSITION_REAR_CENTER },
    { GAVL_CHID_SIDE_LEFT,PA_CHANNEL_POSITION_SIDE_LEFT },
    { GAVL_CHID_SIDE_RIGHT,PA_CHANNEL_POSITION_SIDE_RIGHT },
    { GAVL_CHID_LFE,PA_CHANNEL_POSITION_LFE },
    { GAVL_CHID_AUX,PA_CHANNEL_POSITION_AUX0 }
  };

static void init_channel_map(const gavl_audio_format_t * format,
                             pa_channel_map * map)
  {
  int i, j;
  map->channels = format->num_channels;
  
  if(map->channels == 1)
    {
    map->map[0] = PA_CHANNEL_POSITION_MONO;
    return;
    }
  else if(map->channels == 2)
    {
    map->map[0] = PA_CHANNEL_POSITION_LEFT;
    map->map[1] = PA_CHANNEL_POSITION_RIGHT;
    return;
    }
  
  for(i = 0; i < map->channels; i++)
    {
    for(j = 0; j < sizeof(channels)/sizeof(channels[0]); j++)
      {
      if(format->channel_locations[i] == channels[j].ga)
        {
        map->map[i] = channels[j].pa;
        break;
        }
      }
    }
  
  }


int bg_pa_open(bg_pa_common_t * p, char * server, char * dev, int record)
  {
  struct pa_sample_spec ss;
  pa_channel_map map;
  //  pa_buffer_attr attr;
  
  int error;
  char * app_name, *stream_name;

  if(record)
    {
    memset(&p->format, 0, sizeof(p->format));
    p->format.num_channels = p->num_channels;
    p->format.samplerate = p->samplerate;
    switch(p->bytes_per_sample)
      {
      case 1:
        p->format.sample_format = GAVL_SAMPLE_U8;
        break;
      case 2:
        p->format.sample_format = GAVL_SAMPLE_S16;
        break;
      }
    p->format.samples_per_frame = 1024;
    gavl_set_channel_setup(&p->format);
    }
  else
    {
    p->format.samples_per_frame = 4096;
    }
  
  memset(&map, 0, sizeof(map));
  ss.channels = p->format.num_channels;
  ss.rate = p->format.samplerate;

  switch(p->format.sample_format)
    {
    case GAVL_SAMPLE_U8:
    case GAVL_SAMPLE_S8:
      p->format.sample_format = GAVL_SAMPLE_U8;
      ss.format = PA_SAMPLE_U8;
      break;
    case GAVL_SAMPLE_U16:
    case GAVL_SAMPLE_S16:
      //    case GAVL_SAMPLE_FLOAT: 
      p->format.sample_format = GAVL_SAMPLE_S16;
#ifdef WORDS_BIGENDIAN
      ss.format = PA_SAMPLE_S16BE;
#else
      ss.format = PA_SAMPLE_S16LE;
#endif
      break;
    case GAVL_SAMPLE_S32:
#if 0
#ifdef WORDS_BIGENDIAN
      ss.format = PA_SAMPLE_S32BE;
#else
      ss.format = PA_SAMPLE_S32LE;
#endif
      break;
#endif
    case GAVL_SAMPLE_DOUBLE: 
      p->format.sample_format = GAVL_SAMPLE_FLOAT;
      /* Fall through */
    case GAVL_SAMPLE_FLOAT: 
      ss.format = PA_SAMPLE_FLOAT32NE;
      break;

    case GAVL_SAMPLE_NONE:
      break;
    }

  p->format.interleave_mode = GAVL_INTERLEAVE_ALL;
  
  init_channel_map(&p->format, &map);

  //  memset(&attr, 0, sizeof(attr));
  //  attr.fragsize  = -1; // Let server choose
  //  attr.maxlength = -1; // Let server choose
  
  app_name = gavl_sprintf("Gmerlin [%d]", getpid());

  if(record)
    stream_name =
      gavl_sprintf("Gmerlin capture [%d]", getpid());
  else
    stream_name =
      gavl_sprintf("Gmerlin playback [%d]", getpid());
  
  p->pa = pa_simple_new(server,
                        app_name,
                        record ? PA_STREAM_RECORD : PA_STREAM_PLAYBACK,
                        dev,
                        stream_name,
                        &ss,
                        &map,
                        NULL, // &attr,
                        &error);

  free(app_name);
  free(stream_name);

  // fprintf(stderr, "Fragement size: %d, maxlength: %d\n", attr.fragsize, attr.maxlength);  

  if(!p->pa)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Connection to Pulseaudio (%s/%s) failed: %s",
             server, dev, pa_strerror(error));
    return 0;
    }
  p->block_align = p->format.num_channels *
    gavl_bytes_per_sample(p->format.sample_format);
  
  return 1;
  }

void bg_pa_close_common(bg_pa_common_t * priv)
  {
  if(priv->pa)
    {
    pa_simple_free(priv->pa);
    priv->pa = NULL;
    }
  
  }

bg_controllable_t * bg_pa_get_controllable(void * p)
  {
  bg_pa_common_t * priv = p;
  return &priv->ctrl;
  }
