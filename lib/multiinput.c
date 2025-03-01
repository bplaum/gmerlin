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
#define LOG_DOMAIN "multiinput"

typedef struct
  {
  gavl_dictionary_t mi;
  gavl_dictionary_t * ti;

  
  
  bg_media_source_t src;

  /* Global handle */
  bg_plugin_handle_t * h;
  bg_controllable_t controllable;
  
  } multi_t;

static int set_track_multi(void * priv, int track);

static void free_stream(void * priv)
  {
  bg_plugin_unref(priv);
  }

static void forward_command(multi_t * m, gavl_msg_t * msg)
  {
  int i;
  
  if(m->h)
    bg_msg_sink_put_copy(m->h->control.cmd_sink, msg);
  
  for(i = 0; i < m->src.num_streams; i++)
    {
    if(m->src.streams[i]->user_data)
      {
      bg_plugin_handle_t * h = m->src.streams[i]->user_data;
      bg_msg_sink_put_copy(h->control.cmd_sink, msg);
      }
    }
  }

static void start_multi(void * priv)
  {
  int i;
  multi_t * m = priv;
  const char * uri;
  
  int can_seek = 0, can_pause = 0;
  const gavl_dictionary_t * track;
  gavl_dictionary_t * metadata = NULL;
  bg_media_source_stream_t * stream;
  gavl_time_t pts_to_clock_time = GAVL_TIME_UNDEFINED;
  gavl_time_t clock_time_start  = GAVL_TIME_UNDEFINED;
  
  for(i = 0; i < m->src.num_streams; i++)
    {
    uri = NULL;
    if(!gavl_track_get_src(m->src.streams[i]->s, GAVL_META_SRC, 0, NULL, &uri))
      {
      /* Stream belongs to main handle */
      m->h->src->streams[i]->action = m->src.streams[i]->action;
      }
    }

  if(m->h)
    {
    bg_input_plugin_start(m->h);
    
    track = bg_input_plugin_get_track_info(m->h, -1);
    
    gavl_dictionary_merge2(gavl_track_get_metadata_nc(m->ti),
                           gavl_track_get_metadata(track));

    gavl_dictionary_set(gavl_track_get_metadata_nc(m->ti),
                        GAVL_META_CAN_SEEK, NULL);
    
    if(pts_to_clock_time == GAVL_TIME_UNDEFINED)
      {
      pts_to_clock_time = gavl_track_get_pts_to_clock_time(track);
      clock_time_start = gavl_track_get_start_time(track) + pts_to_clock_time;
      }
    }
  
  for(i = 0; i < m->src.num_streams; i++)
    {
    if(m->src.streams[i]->action == BG_STREAM_ACTION_OFF)
      continue;
    
    uri = NULL;
    if(!gavl_track_get_src(m->src.streams[i]->s, GAVL_META_SRC, 0, NULL, &uri))
      {
      /* Stream belongs to main handle */
      stream = m->h->src->streams[i];
      }
    else
      {
      bg_plugin_handle_t * h;
      char * real_uri = NULL;

      /* Set start time */
      if(clock_time_start != GAVL_TIME_UNDEFINED)
        {
        real_uri = gavl_strdup(uri);

        /*
         *  Load external streams from 0.5 seconds before time of the main stream.
         *  Most likely external URIs are audio or subtitles, which are cheaper to skip.
         *  This makes it easier to quickly synchonize the stream if indivudual
         *  start times differ.
         */
        
        //        real_uri = gavl_url_add_var_long(real_uri, GAVL_URL_VAR_CLOCK_TIME,
        //                                         clock_time_start - GAVL_TIME_SCALE/2);
        
        /* Make sure we end up in the same segment */
        real_uri = gavl_url_add_var_long(real_uri, GAVL_URL_VAR_CLOCK_TIME,
                                         clock_time_start + GAVL_TIME_SCALE/2);
        
        uri = real_uri;
        }
      
      gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Loading external uri: %s", uri);
      if(!(h = bg_input_plugin_load(uri)))
        return;
      
      /* Stream is external */
      m->src.streams[i]->user_data = h;
      m->src.streams[i]->free_user_data = free_stream;
      
      bg_input_plugin_set_track(h, 0);

      /* For now, we assume that from exteral uris, we always load stream 0 */
      h->src->streams[0]->action = m->src.streams[i]->action;
      bg_input_plugin_start(h);

      stream = h->src->streams[0];
      
      track = bg_input_plugin_get_track_info(h, -1);
      
      if(pts_to_clock_time == GAVL_TIME_UNDEFINED)
        pts_to_clock_time = gavl_track_get_pts_to_clock_time(track);

      if(real_uri)
        {
        free(real_uri);
        real_uri = NULL;
        }
      uri = NULL;
      }
    
    m->src.streams[i]->psrc = stream->psrc;
    m->src.streams[i]->vsrc = stream->vsrc;
    m->src.streams[i]->asrc = stream->asrc;
    m->src.streams[i]->msghub = stream->msghub;

    /* Copy stream infos */
    //    gavl_dictionary_reset(m->src.streams[i]->s);
    //    gavl_dictionary_copy(m->src.streams[i]->s, stream->s);

    gavl_dictionary_copy_value(m->src.streams[i]->s, stream->s, GAVL_META_STREAM_STATS);
    gavl_dictionary_copy_value(m->src.streams[i]->s, stream->s, GAVL_META_STREAM_COMPRESSION_INFO);
    gavl_dictionary_copy_value(m->src.streams[i]->s, stream->s, GAVL_META_STREAM_FORMAT);

    gavl_dictionary_merge2(gavl_stream_get_metadata_nc(m->src.streams[i]->s),
                           gavl_stream_get_metadata(stream->s));
    
    }

  metadata = gavl_track_get_metadata_nc(m->ti);
  
  if(pts_to_clock_time != GAVL_TIME_UNDEFINED)
    gavl_track_set_pts_to_clock_time(m->ti, pts_to_clock_time);

  /* Check if we can seek */

  if(m->h)
    {
    track = bg_input_plugin_get_track_info(m->h, -1);
    can_seek = gavl_track_can_seek(track);
    can_pause = gavl_track_can_pause(track);

    //    fprintf(stderr, "Started main URI %d %d\n", can_seek, can_pause);
    }
  else
    {
    can_seek       = 1;
    can_pause      = 1;
    }

  for(i = 0; i < m->src.num_streams; i++)
    {
    if((m->src.streams[i]->action == BG_STREAM_ACTION_OFF) ||
       !m->src.streams[i]->user_data)
      continue;

    track = bg_input_plugin_get_track_info(m->src.streams[i]->user_data, -1);
    
    if(can_seek && !gavl_track_can_seek(track))
      can_seek = 0;
    if(can_pause && !gavl_track_can_pause(track))
      can_pause = 0;

    //    fprintf(stderr, "Started external URI %d %d\n", can_seek, can_pause);
    }
  
  if(can_seek)
    gavl_dictionary_set_int(metadata, GAVL_META_CAN_SEEK, 1);
  
  if(can_pause)
    gavl_dictionary_set_int(metadata, GAVL_META_CAN_PAUSE, 1);
  
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
          int track = gavl_msg_get_arg_int(msg, 0);
          set_track_multi(priv, track);
          }
          break;
        case GAVL_CMD_SRC_START:
          start_multi(data);
          break;
        case GAVL_CMD_SRC_SEEK:
          // seek_multi(priv, msg);
          // break;
        case GAVL_CMD_SRC_PAUSE:
        case GAVL_CMD_SRC_RESUME:
          {
          forward_command(priv, msg);
          }
        }
      break;
    }
  return 1;
  }


static bg_media_source_t * get_src_multi(void * priv)
  {
  multi_t * m = priv;
  return &m->src;
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

static int set_track_multi(void * priv, int track)
  {
  int i;
  multi_t * m = priv;

  if(m->h)
    bg_input_plugin_set_track(m->h, 0);

  /* Close all external plugins */
  for(i = 0; i < m->src.num_streams; i++)
    {
    m->src.streams[i]->action = BG_STREAM_ACTION_OFF;
    
    if(m->src.streams[i]->user_data)
      {
      bg_plugin_unref(m->src.streams[i]->user_data);
      m->src.streams[i]->user_data = NULL;
      }
    m->src.streams[i]->asrc = NULL;
    m->src.streams[i]->vsrc = NULL;
    m->src.streams[i]->psrc = NULL;
    }
  
  return 1;
  }

static void close_multi(void * priv)
  {
  multi_t * m = priv;
  bg_media_source_cleanup(&m->src);
  if(m->h)
    bg_plugin_unref(m->h);
  gavl_dictionary_free(&m->mi);
  memset(m, 0, sizeof(*m));
  }

static void destroy_multi(void * priv)
  {
  multi_t * m = priv;
  bg_controllable_cleanup(&m->controllable);
  close_multi(priv);
  free(priv);
  }



/*
 *  This meta plugin loads (optionally) one media file with multiple streams
 *  plus any number of additional streams from separate uris. This is used for
 *  subtitles from separate files and IPTV-scenarios, where different elementary
 *  streams come from different sources
 */

static const bg_input_plugin_t multi_plugin =
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
      //      .get_parameters = get_parameters_edl,
      //      .set_parameter =  set_parameter_edl
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

bg_plugin_info_t * bg_multi_input_get_info()
  {
  return bg_plugin_info_create(&multi_plugin.common);
  }


bg_plugin_handle_t * bg_input_plugin_load_multi(const gavl_dictionary_t * track, bg_plugin_handle_t * h)
  {
  //  int num_streams;
  const char * uri = NULL;
  gavl_stream_type_t type;

  char * real_uri = NULL;
  
  /* Main URI */
  const gavl_dictionary_t * s;
  const gavl_dictionary_t * vars = NULL;
  
  bg_plugin_handle_t * ret;
  int num, i;
  bg_media_source_stream_t * src_s;
  multi_t * priv = calloc(1, sizeof(*priv));

  ret = calloc(1, sizeof(*ret));

  if(track)
    vars = bg_track_get_uri_vars(track);
  
  ret->plugin = (bg_plugin_common_t*)&multi_plugin;
  ret->info = bg_plugin_find_by_name("i_multi");

  if(!track)
    track = bg_input_plugin_get_track_info(h, -1);
  
  //  fprintf(stderr, "bg_input_plugin_load_multi\n");
  //  gavl_dictionary_dump(track, 2);
  //  fprintf(stderr, "\n");
  
  //  pthread_mutex_init(&ret->mutex, NULL);

  ret->priv = priv;
  ret->refcount = 1;

  priv->ti = gavl_append_track(&priv->mi, NULL);
  priv->src.track = priv->ti;
  
  /* Load main uri */

  if(!h)
    {
    if(gavl_track_get_src(track, GAVL_META_SRC, 0, NULL, &uri))
      {
      if(vars)
        {
        real_uri = gavl_strdup(uri);
        real_uri = gavl_url_append_vars(real_uri, vars);
        uri = real_uri;
        }

      gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Loading primary uri: %s", uri);
      
      if(!(h = bg_input_plugin_load(uri)))
        goto fail;
      bg_input_plugin_set_track(h, 0);

      if(real_uri)
        {
        free(real_uri);
        real_uri = NULL;
        }
      }
    }

  if(h)
    {
    gavl_dictionary_t * ti;
    
    ti = bg_input_plugin_get_track_info(h, 0);
    
    num = gavl_track_get_num_streams_all(ti);
    for(i = 0; i < num; i++)
      {
      if(!(s = gavl_track_get_stream_all(ti, i)) ||
         ((type = gavl_stream_get_type(s)) == GAVL_STREAM_NONE))
        return 0;
      src_s = bg_media_source_append_stream(&priv->src, type);
      gavl_dictionary_reset(src_s->s);
      gavl_dictionary_copy(src_s->s, s);
    
      if(type == GAVL_STREAM_MSG)
        gavl_stream_get_id(src_s->s, &src_s->stream_id);
      }
    priv->h = h;
    }
  
  num = gavl_track_get_num_external_streams(track);

  for(i = 0; i < num; i++)
    {
    if((s = gavl_track_get_external_stream(track, i)) &&
       ((type = gavl_stream_get_type(s)) != GAVL_STREAM_NONE))
      {
      src_s = bg_media_source_append_stream(&priv->src, type);
      gavl_dictionary_reset(src_s->s);
      gavl_dictionary_copy(src_s->s, s);
      }
    }
  
  bg_controllable_init(&priv->controllable,
                       bg_msg_sink_create(handle_cmd, priv, 1),
                       bg_msg_hub_create(1));
  
  bg_plugin_handle_connect_control(ret);

  //  fprintf(stderr, "Loaded multi plugin\n");
  //  gavl_dictionary_dump(priv->ti, 2);
  return ret;

  fail:
  if(ret)
    bg_plugin_unref(ret);
  return NULL;

  }
