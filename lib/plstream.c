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


#include <config.h>
#include <gavl/gavl.h>
#include <gavl/metatags.h>
#include <gavl/trackinfo.h>
#include <gavl/utils.h>

#include <gmerlin/translation.h>


#include <gmerlin/pluginregistry.h>
#include <gmerlin/player.h>

#include <playerprivate.h>

/*
 *  Variables: srate, channels, fmt, shuffle
 */

#define SEND_METADATA (1<<0)

typedef struct 
  {
  bg_media_source_t src;
  gavl_dictionary_t mi;

  gavl_array_t tracks;
  int idx;
  
  bg_controllable_t ctrl;

  bg_plugin_handle_t * h;

  /* Format from the url variables. Missing entries
     are taken from the first input file */

  gavl_audio_format_t * fmt;
  gavl_audio_source_t * src_int;

  int flags;

  int64_t pts;
  
  } plstream_t;

static int load_file(plstream_t * p)
  {
  const gavl_dictionary_t * mi;
  const gavl_dictionary_t * t;
  const gavl_dictionary_t * s;
  gavl_dictionary_t track;
  int num_variants = 0;
  int first_idx = p->idx;

  if(p->h)
    {
    bg_plugin_unref(p->h);
    p->h = NULL;
    }
  
  gavl_dictionary_init(&track);

  while(1)
    {
    gavl_track_from_location(&track, gavl_string_array_get(&p->tracks, p->idx));
    
    if((p->h = bg_load_track(&track, 0, &num_variants)) &&
       (mi = bg_input_plugin_get_media_info(p->h)) &&
       (t = gavl_get_track(mi, 0)) &&
       (s = gavl_track_get_audio_stream(t, 0)))
      break;

    if(p->h)
      {
      bg_plugin_unref(p->h);
      p->h = NULL;
      }
    
    p->idx++;
    if(p->idx >= p->tracks.num_entries)
      p->idx = 0;

    if(p->idx == first_idx)
      return 0;
    
    }
  
  if(!bg_input_plugin_set_track(p->h, 0))
    return 0;

  bg_media_source_set_audio_action(p->h->src, 0, BG_STREAM_ACTION_DECODE);
  bg_input_plugin_start(p->h);
  
  if(!(p->src_int = bg_media_source_get_audio_source(p->h->src, 0)))
    return 0;
  
  return 1;
  }

static int advance(plstream_t * p)
  {
  p->idx++;
  if(p->idx == p->tracks.num_entries)
    p->idx = 0;

  if(!load_file(p))
    return 0;

  gavl_audio_source_set_dst(p->src_int, 0, p->fmt);
  return 1;
  }

static int open_plstream(void * priv, const char * file)
  {
  int i = 0;
  plstream_t * p = priv;
  gavl_dictionary_t * mi;
  const gavl_dictionary_t * dict;
  gavl_array_t * arr;
  const char * uri;
  char * real_file;
  gavl_dictionary_t vars;
  int val_i = 0;
  const char * val_s;

  gavl_dictionary_t * track;
  gavl_dictionary_t * m;
  gavl_dictionary_t * s;
  
  if(gavl_string_starts_with(file, "plstream://"))
    file += strlen("plstream://");

  real_file = gavl_strdup(file);
  gavl_dictionary_init(&vars);
  gavl_url_get_vars(real_file, &vars);
  
  if(!(mi = bg_plugin_registry_load_media_info(bg_plugin_reg, real_file, 0)))
    {
    free(real_file);
    return 0;
    }
  free(real_file);
  
  if(!(arr = gavl_get_tracks_nc(mi)) || !arr->num_entries)
    return 0;
  
  while(i < arr->num_entries)
    {
    uri = NULL;
    if(!(dict = gavl_value_get_dictionary(&arr->entries[i])) ||
       !(dict = gavl_track_get_metadata(dict)) ||
       !gavl_metadata_get_src(dict, GAVL_META_SRC, 0, NULL, &uri) ||
       !uri)
      {
      // fprintf(stderr, "Blupp %d\n", i);
      gavl_array_splice_val(arr, i, 1, NULL);
      continue;
      }
    
    gavl_string_array_add(&p->tracks, uri);
    i++;
    }
  
  if(gavl_dictionary_get_int(&vars, "shuffle", &val_i) && val_i)
    {
    int i;
    int * indices;
    
    indices = bg_create_shuffle_list(p->tracks.num_entries);

    for(i = 0; i < p->tracks.num_entries; i++)
      {
      if(indices[i] != i)
        gavl_value_swap(&p->tracks.entries[i],
                        &p->tracks.entries[indices[i]]);
      }
    free(indices);
    }
  
  fprintf(stderr, "Got %d tracks\n", p->tracks.num_entries);
  
  track = gavl_append_track(&p->mi, NULL);
  m = gavl_track_get_metadata_nc(track);

  gavl_dictionary_set_string_nocopy(m, GAVL_META_LABEL, gavl_filename_get_base(file));
  
  s = gavl_track_append_audio_stream(track);

  p->fmt = gavl_stream_get_audio_format_nc(s);

  if(gavl_dictionary_get_int(&vars, "srate", &val_i))
    p->fmt->samplerate = val_i;

  if(gavl_dictionary_get_int(&vars, "channels", &val_i))
    p->fmt->num_channels = val_i;

  if((val_s = gavl_dictionary_get_string(&vars, "sfmt")))
    p->fmt->sample_format = gavl_short_string_to_sample_format(val_s);
    
  s = gavl_track_append_msg_stream(track, GAVL_META_STREAM_ID_MSG_PROGRAM);
  
  return 1;
  }

static void flush_metadata(plstream_t * p)
  {
  bg_media_source_stream_t * st;
  gavl_msg_t * msg;
  const gavl_dictionary_t * dict;

  if((dict = bg_input_plugin_get_media_info(p->h)) &&
     (dict = gavl_get_track(dict, 0)) &&
     (dict = gavl_track_get_metadata(dict)))
    {
    gavl_value_t val;
    gavl_dictionary_t * dst;
    
    st = bg_media_source_get_msg_stream_by_id(&p->src, GAVL_META_STREAM_ID_MSG_PROGRAM);

    msg = bg_msg_sink_get(bg_msg_hub_get_sink(st->msghub));

    dst = gavl_value_set_dictionary(&val);
    
    gavl_dictionary_copy(dst, dict);

    gavl_dictionary_set(dst, GAVL_META_CAN_SEEK, NULL);
    gavl_dictionary_set(dst, GAVL_META_CAN_PAUSE, NULL);
    gavl_dictionary_set(dst, GAVL_META_SRC, NULL);
    gavl_dictionary_set(dst, GAVL_META_HASH, NULL);
    gavl_dictionary_set(dst, GAVL_META_IDX, NULL);
    gavl_dictionary_set(dst, GAVL_META_TOTAL, NULL);
    gavl_dictionary_set(dst, GAVL_META_APPROX_DURATION, NULL);
    
    gavl_msg_set_state(msg, GAVL_MSG_STATE_CHANGED, 1,
                       GAVL_STATE_CTX_SRC, GAVL_STATE_SRC_METADATA, &val);
    
    /*
    fprintf(stderr, "Got metadata\n");
    gavl_dictionary_dump(dst, 2);
    */
    
    gavl_value_free(&val);
    
    bg_msg_sink_put(bg_msg_hub_get_sink(st->msghub));
    }
  
  }

static gavl_source_status_t read_audio(void * priv,
                                       gavl_audio_frame_t ** frame)
  {
  gavl_source_status_t ret;
  plstream_t * p = priv;

  if(!p->pts)
    flush_metadata(p);
  
  ret = gavl_audio_source_read_frame(p->src_int, frame);

  if(ret != GAVL_SOURCE_OK)
    {
    if(!advance(p))
      return GAVL_SOURCE_EOF;

    flush_metadata(p);
    
    if((ret = gavl_audio_source_read_frame(p->src_int, frame)) != GAVL_SOURCE_OK)
      return GAVL_SOURCE_EOF;
    }
  
  if(frame)
    p->pts += (*frame)->valid_samples;
  
  return GAVL_SOURCE_OK;
  }


static int handle_cmd(void * data, gavl_msg_t * msg)
  {
  plstream_t * p = data;
  
  switch(msg->NS)
    {
    case GAVL_MSG_NS_SRC:
      switch(msg->ID)
        {
        case GAVL_CMD_SRC_SELECT_TRACK:
          {
          fprintf(stderr, "Select track plstreams\n");
          bg_media_source_set_from_track(&p->src, gavl_get_track_nc(&p->mi, 0));
          }
          break;
        case GAVL_CMD_SRC_START:
          {
          gavl_audio_format_t fmt;
          bg_media_source_stream_t * st;
          
          fprintf(stderr, "Start plstreams\n");
          if(!load_file(p))
            {
            fprintf(stderr, "Loading file failed\n");
            }
          gavl_audio_format_copy(&fmt, gavl_audio_source_get_src_format(p->src_int));
          if(p->fmt->samplerate)
            fmt.samplerate = p->fmt->samplerate;

          if(p->fmt->num_channels)
            {
            fmt.num_channels = p->fmt->num_channels;
            gavl_set_channel_setup(&fmt);
            }
          gavl_audio_format_copy(p->fmt, &fmt);

          fprintf(stderr, "Got format:\n");
          gavl_audio_format_dump(p->fmt);

          st = bg_media_source_get_audio_stream(&p->src, 0);
          st->asrc_priv = gavl_audio_source_create(read_audio, p, GAVL_SOURCE_SRC_FRAMESIZE_MAX, p->fmt);
          st->asrc = st->asrc_priv;

          st = bg_media_source_get_msg_stream_by_id(&p->src, GAVL_META_STREAM_ID_MSG_PROGRAM);
          st->msghub_priv = bg_msg_hub_create(1);
          st->msghub = st->msghub_priv;
          }
          break;
        }
      break;
    }
  return 1;

  }

static void close_plstream(void * priv)
  {
  plstream_t * p = priv;

  if(p->h)
    {
    bg_plugin_unref(p->h);
    p->h = NULL;
    }

  gavl_dictionary_reset(&p->mi);
  gavl_array_reset(&p->tracks);
  
  bg_media_source_cleanup(&p->src);
  bg_media_source_init(&p->src);
  
  }


static void destroy_plstream(void * priv)
  {
  plstream_t * p = priv;
  close_plstream(priv);
  bg_controllable_cleanup(&p->ctrl);

  free(priv);
  }




static bg_media_source_t * get_src_plstream(void * priv)
  {
  plstream_t * p = priv;
  return &p->src;
  }


static gavl_dictionary_t * get_media_info_plstream(void * priv)
  {
  plstream_t * p = priv;
  return &p->mi;
  }

static bg_controllable_t * get_controllable_plstream(void * priv)
  {
  plstream_t * p = priv;
  return &p->ctrl;
  }


/*
 *  This is an audio-only plugin for making a stream with metadata updates
 *  for sending over the network
 */

static const bg_input_plugin_t plstream_plugin =
  {
    .common =
    {
      BG_LOCALE,
      .name =           bg_plstream_name,
      .long_name =      TRS("Playlist streamer"),
      .description =    TRS("Play audio files in one stream"),
      .type =           BG_PLUGIN_INPUT,
      .flags =          0,
      .priority =       1,
      .destroy =        destroy_plstream,

      .get_controllable = get_controllable_plstream,
      
    },
    .open           = open_plstream,

    .get_media_info = get_media_info_plstream,
    
    .get_src           = get_src_plstream,
    
    /* Read one video frame (returns FALSE on EOF) */
    .close = close_plstream,
  };

/* */

bg_plugin_info_t * bg_plstream_get_info(void)
  {
  bg_plugin_info_t * ret;
  gavl_array_t * proto;
  
  ret = bg_plugin_info_create(&plstream_plugin.common);
  proto = bg_plugin_info_set_protocols(ret);
  gavl_string_array_add(proto, "plstream");
  return ret;
  }

void * bg_plstream_create(void)
  {
  plstream_t * p = calloc(1, sizeof(*p));

  bg_controllable_init(&p->ctrl,
                       bg_msg_sink_create(handle_cmd, p, 1),
                       bg_msg_hub_create(1));
  
  return p;
  }

const bg_plugin_common_t* bg_plstream_get(void)
  {
  return &plstream_plugin.common;
  }

