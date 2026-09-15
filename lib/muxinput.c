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

#include <string.h>


#include <config.h>

#include <gmerlin/translation.h>

#include <gmerlin/pluginregistry.h>
#include <gmerlin/utils.h>
#include <pluginreg_priv.h>

#include <gavl/trackinfo.h>

#include <gavl/log.h>
#define LOG_DOMAIN "muxinput"

#define MUXER_KEY "muxer"

typedef struct
  {
  bg_plugin_handle_t * h;
  char * uri;
  } plugin_t;

typedef struct
  {
  gavl_dictionary_t mi;
  gavl_dictionary_t * ti;
  
  bg_media_source_t src;

  /* Plugin handles */

  plugin_t * plugins;
  int num_plugins;
  int plugins_alloc;
  
  bg_controllable_t controllable;
  
  } multi_t;

static gavl_dictionary_t * get_stream_config_wr(bg_media_source_stream_t * st)
  {
  return gavl_dictionary_get_dictionary_create(st->s, MUXER_KEY);
  }

static const gavl_dictionary_t * get_stream_config(const bg_media_source_stream_t * st)
  {
  return gavl_dictionary_get_dictionary(st->s, MUXER_KEY);
  }

static void close_inputs(multi_t * m)
  {
  int i;
  
  for(i = 0; i < m->num_plugins; i++)
    {
    if(m->plugins[i].h)
      bg_plugin_unref(m->plugins[i].h);
    if(m->plugins[i].uri)
      free(m->plugins[i].uri);
    }

  if(m->plugins)
    free(m->plugins);

  m->plugins = NULL;
  m->num_plugins = 0;
  m->plugins_alloc = 0;
  
  }

static bg_plugin_handle_t * load_input(multi_t * m, const char * uri)
  {
  int i;
  
  for(i = 0; i < m->num_plugins; i++)
    {
    if(!strcmp(m->plugins[i].uri, uri))
      return m->plugins[i].h;
    }

  if(m->num_plugins == m->plugins_alloc)
    {
    m->plugins_alloc += 16;
    m->plugins = realloc(m->plugins, sizeof(*m->plugins) * m->plugins_alloc);
    memset(m->plugins + m->num_plugins, 0,
           sizeof(*m->plugins) * (m->plugins_alloc - m->num_plugins));
    }

  m->plugins[m->num_plugins].h = bg_input_plugin_load_full(uri);
  m->plugins[m->num_plugins].uri = gavl_strdup(uri);
  m->num_plugins++;
  return m->plugins[m->num_plugins-1].h;
  }

                                       
/*
 *  This meta plugin loads (optionally) one media file with multiple streams
 *  plus any number of additional streams from separate uris. This is used for
 *  subtitles from separate files and IPTV-scenarios, where different elementary
 *  streams come from different sources
 */

static void close_multi(void * priv)
  {
  multi_t * m = priv;
  bg_media_source_cleanup(&m->src);
  gavl_dictionary_free(&m->mi);
  close_inputs(m);
  memset(m, 0, sizeof(*m));
  }

static void destroy_multi(void * priv)
  {
  multi_t * m = priv;
  bg_controllable_cleanup(&m->controllable);
  close_multi(priv);
  free(priv);
  }

static bg_controllable_t * get_controllable_multi(void * priv)
  {
  multi_t * m = priv;
  return &m->controllable;
  }

static gavl_dictionary_t * get_media_info_multi(void * priv)
  {
  multi_t * m = priv;
  return &m->mi;
  }

static bg_media_source_t * get_src_multi(void * priv)
  {
  multi_t * m = priv;
  return &m->src;
  }

static void start_multi(multi_t * m)
  {
  int i;
  bg_plugin_handle_t * h;
  bg_media_source_stream_t * st;
  
  /* Step 1: Load plugins and set stream actions */
  for(i = 0; i < m->src.num_streams; i++)
    {
    int idx = 0;
    const char * uri = NULL;
    const gavl_dictionary_t * dict;
    
    if(m->src.streams[i]->action == BG_STREAM_ACTION_OFF)
      continue;

    if(!(dict = get_stream_config(m->src.streams[i])) ||
       !gavl_dictionary_get_int(dict, GAVL_META_IDX, &idx) ||
       !(uri = gavl_dictionary_get_string(dict, GAVL_META_URI)))
      continue;

    h = load_input(m, uri);

    st = h->src->streams[idx];

    st->action = m->src.streams[i]->action;
    m->src.streams[i]->user_data = st;
    }

  /* Step 2: Start plugins */
  for(i = 0; i < m->num_plugins; i++)
    bg_input_plugin_start(m->plugins[i].h);

  /* Step 3: Set sources and formats */
  for(i = 0; i < m->src.num_streams; i++)
    {
    if(m->src.streams[i]->action == BG_STREAM_ACTION_OFF)
      continue;

    st = m->src.streams[i]->user_data;

    gavl_dictionary_copy_value(m->src.streams[i]->s, st->s, GAVL_META_STREAM_FORMAT);
    gavl_dictionary_copy_value(m->src.streams[i]->s, st->s, GAVL_META_METADATA);
    gavl_dictionary_copy_value(m->src.streams[i]->s, st->s, GAVL_META_STREAM_STATS);
    
    if((m->src.streams[i]->asrc = st->asrc))
      {
      gavl_audio_format_copy(gavl_stream_get_audio_format_nc(m->src.streams[i]->s),
                             gavl_audio_source_get_src_format(st->asrc));
      }
    if((m->src.streams[i]->vsrc = st->vsrc))
      {
      gavl_video_format_copy(gavl_stream_get_video_format_nc(m->src.streams[i]->s),
                             gavl_video_source_get_src_format(st->vsrc));
      }
    
    m->src.streams[i]->psrc = st->psrc;
    }
  
  }

static void forward_command(multi_t * m, gavl_msg_t * msg)
  {
  int i;

  for(i = 0; i < m->num_plugins; i++)
    bg_msg_sink_put_copy(m->plugins[i].h->control.cmd_sink, msg);

  }

static int handle_cmd(void * data, gavl_msg_t * msg)
  {
  multi_t * priv = data;

  //  fprintf(stderr, "Handle CMD\n");
  //  gavl_msg_dump(msg, 2);
  
  switch(msg->NS)
    {
    case GAVL_MSG_NS_SRC:
      switch(msg->ID)
        {
        case GAVL_CMD_SRC_SELECT_TRACK:
          {
          /* Close plugins */
          
          }
          break;
        case GAVL_CMD_SRC_START:
          start_multi(data);
          break;
        case GAVL_CMD_SRC_SEEK:
        case GAVL_CMD_SRC_PAUSE:
        case GAVL_CMD_SRC_RESUME:
          forward_command(priv, msg);
          break;
        }
      break;
    }
  return 1;
  }



static const bg_input_plugin_t mux_plugin =
  {
    .common =
    {
      BG_LOCALE,
      .name =           "i_multi",
      .long_name =      TRS("Multi source decoder"),
      .description =    TRS("This metaplugin decodes multiple streams, which can come from separate sources"),
      .type =           BG_PLUGIN_INPUT,
      .flags =          0,
      .priority =       1,
      .create =         NULL,
      .destroy =        destroy_multi,

      .get_controllable = get_controllable_multi,
    },
    .get_media_info = get_media_info_multi,
    
    .get_src           = get_src_multi,
    
    /* Read one video frame (returns FALSE on EOF) */
    
    /*
     *  Do percentage seeking (can be NULL)
     *  Media streams are supposed to be seekable, if this
     *  function is non-NULL AND the duration field of the track info
     *  is > 0
     */
    //    .seek = seek_edl,
    /* Stop playback, close all decoders */
    //    .stop = stop_edl,
    .close = close_multi,
  };

bg_plugin_info_t * bg_mux_input_get_info()
  {
  return bg_plugin_info_create(&mux_plugin.common);
  }



bg_plugin_handle_t * bg_input_plugin_load_mux(const gavl_array_t * arr)
  {
  int i, j, num_streams;
  bg_plugin_handle_t * ret;
  int can_seek  = 1;
  int can_pause = 1;
  gavl_dictionary_t * m;

  multi_t * priv = calloc(1, sizeof(*priv));
  
  ret = calloc(1, sizeof(*ret));

  ret->plugin = (bg_plugin_common_t*)&mux_plugin;
  ret->info = bg_plugin_find_by_name("i_mux");

  ret->priv = priv;
  
  ret->refcount = 1;

  priv->ti = gavl_append_track(&priv->mi, NULL);
  priv->src.track = priv->ti;
  
  for(i = 0; i < arr->num_entries; i++)
    {
    const char * uri;
    gavl_dictionary_t * track;
    gavl_dictionary_t * cfg;
    bg_media_source_stream_t * st;

    uri = gavl_string_array_get(arr, i);
    
    track = bg_plugin_registry_load_media_info(bg_plugin_reg, uri,
                                               BG_INPUT_FLAG_SELECT_TRACK);

    if(!track)
      goto fail;

    if(!gavl_track_can_pause(track))
      can_pause = 0;
    
    if(!gavl_track_can_seek(track))
      can_seek = 0;
    
    num_streams = gavl_track_get_num_streams_all(track);

    for(j = 0; j < num_streams; j++)
      {
      const gavl_dictionary_t * s;
      int type;

      s = gavl_track_get_stream_all(track, j);

      type = gavl_stream_get_type(s);
        
      if((type != GAVL_STREAM_AUDIO) &&
         (type != GAVL_STREAM_VIDEO) &&
         (type != GAVL_STREAM_TEXT) &&
         (type != GAVL_STREAM_OVERLAY))
        continue;
      
      st = bg_media_source_append_stream(&priv->src, type);
      cfg = get_stream_config_wr(st);
      gavl_dictionary_set_int(cfg, GAVL_META_IDX, j);
      gavl_dictionary_set_string(cfg, GAVL_META_URI, uri);
      }

    gavl_dictionary_destroy(track);
    }
  
  if((m = gavl_track_get_metadata_nc(priv->src.track)))
    {
    gavl_dictionary_set_int(m, GAVL_META_CAN_PAUSE, can_pause);
    gavl_dictionary_set_int(m, GAVL_META_CAN_SEEK, can_seek);
    }
  
  bg_controllable_init(&priv->controllable,
                       bg_msg_sink_create(handle_cmd, priv, 1),
                       bg_msg_hub_create(1));
  
  bg_plugin_handle_connect_control(ret);
  return ret;
  
  fail:
  if(ret)
    bg_plugin_unref(ret);
  return NULL;
  }
