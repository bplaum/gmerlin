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

#include <gmerlin/mediaconnector.h>

/*
 *   Values for the EOF status:
 *
 *   Resync
 *     -> Interrupt all threads, update time, restart everything
 *  
 *   EOF
 *     -> Gapless transition: Let output threads run
 *        -> Switch track
 *        -> New file
 *
 *     -> Transition with Gap: Reinit output threads
 *        -> Switch track
 *        -> New file
 */



void bg_media_source_init(bg_media_source_t * src)
  {
  memset(src, 0, sizeof(*src));
  }

void bg_media_source_cleanup(bg_media_source_t * src)
  {
  int i;

  for(i = 0; i < src->num_streams; i++)
    {
    if(src->streams[i]->asrc_priv)
      gavl_audio_source_destroy(src->streams[i]->asrc_priv);
    if(src->streams[i]->vsrc_priv)
      gavl_video_source_destroy(src->streams[i]->vsrc_priv);
    if(src->streams[i]->psrc_priv)
      gavl_packet_source_destroy(src->streams[i]->psrc_priv);
    if(src->streams[i]->msghub_priv)
      bg_msg_hub_destroy(src->streams[i]->msghub_priv);
    if(src->streams[i]->free_user_data && src->streams[i]->user_data)
      src->streams[i]->free_user_data(src->streams[i]->user_data);
    free(src->streams[i]);
    }
  if(src->streams)
    free(src->streams);

  if(src->free_user_data && src->user_data)
    src->free_user_data(src->user_data);

  if(src->track_priv)
    gavl_dictionary_destroy(src->track_priv);
  }

void bg_media_source_drain(bg_media_source_t * src)
  {
  int i;
  for(i = 0; i < src->num_streams; i++)
    {
    if(src->streams[i]->psrc)
      gavl_packet_source_drain(src->streams[i]->psrc);
    
    if(src->streams[i]->asrc)
      gavl_audio_source_drain(src->streams[i]->asrc);
    if(src->streams[i]->vsrc)
      gavl_video_source_drain(src->streams[i]->vsrc);
    }
  
  
  }

void bg_media_source_drain_nolock(bg_media_source_t * src)
  {
  int i;
  for(i = 0; i < src->num_streams; i++)
    {
    if(src->streams[i]->psrc)
      gavl_packet_source_drain_nolock(src->streams[i]->psrc);
    
    if(src->streams[i]->asrc)
      gavl_audio_source_drain_nolock(src->streams[i]->asrc);
    if(src->streams[i]->vsrc)
      gavl_video_source_drain_nolock(src->streams[i]->vsrc);
    }
  }

void bg_media_source_set_eof(bg_media_source_t * src, int eof)
  {
  int i;
  for(i = 0; i < src->num_streams; i++)
    {
    if(src->streams[i]->asrc)
      gavl_audio_source_set_eof(src->streams[i]->asrc, eof);
    if(src->streams[i]->vsrc)
      gavl_video_source_set_eof(src->streams[i]->vsrc, eof);
    if(src->streams[i]->psrc)
      gavl_packet_source_set_eof(src->streams[i]->psrc, eof);
    }
  }

int bg_media_source_get_num_streams(const bg_media_source_t * src, gavl_stream_type_t type)
  {
  int i;
  int ret = 0;
  
  for(i = 0; i < src->num_streams; i++)
    {
    if(src->streams[i]->type == type)
      ret++;
    }
  return ret;
  }

static gavl_time_t get_start_time(bg_media_source_stream_t * s)
  {
  int timescale = 0;
  gavl_stream_stats_t stats;
  const gavl_dictionary_t * m = NULL;

  //  fprintf(stderr, "Get start time:\n");
  //  gavl_dictionary_dump(s->s, 2);
  
  gavl_stream_stats_init(&stats);
  if((gavl_stream_get_stats(s->s, &stats)) &&
     (m = gavl_stream_get_metadata(s->s)) &&
     gavl_dictionary_get_int(m, GAVL_META_STREAM_SAMPLE_TIMESCALE, &timescale))
    return gavl_time_unscale(timescale, stats.pts_start);
  else
    return 0;
  }

gavl_time_t bg_media_source_get_start_time(bg_media_source_t * src)
  {
  int num;
  int i;
  bg_media_source_stream_t * s;
  
  if((num = bg_media_source_get_num_streams(src, GAVL_STREAM_AUDIO)))
    {
    for(i = 0; i < num; i++)
      {
      s = bg_media_source_get_stream(src, GAVL_STREAM_AUDIO, i);
      if(s->action != BG_STREAM_ACTION_OFF)
        return get_start_time(s);
      }
    }

  if((num = bg_media_source_get_num_streams(src, GAVL_STREAM_VIDEO)))
    {
    for(i = 0; i < num; i++)
      {
      s = bg_media_source_get_stream(src, GAVL_STREAM_VIDEO, i);
      if(s->action != BG_STREAM_ACTION_OFF)
        return get_start_time(s);
      }
    }
  return 0;
  }


bg_media_source_stream_t *
bg_media_source_append_stream(bg_media_source_t * src, gavl_stream_type_t type)
  {
  int idx;
  bg_media_source_stream_t * ret;

  idx = bg_media_source_get_num_streams(src, type);
  
  if(src->streams_alloc < src->num_streams + 1)
    {
    src->streams_alloc += 16;
    src->streams = realloc(src->streams, src->streams_alloc * sizeof(*src->streams));
    memset(src->streams + src->num_streams, 0, (src->streams_alloc - src->num_streams) *
           sizeof(*src->streams));
    }
  src->streams[src->num_streams] = calloc(1, sizeof(*src->streams[src->num_streams]));
  ret = src->streams[src->num_streams];
  src->num_streams++;

  ret->type = type;

  if(!(ret->s = gavl_track_get_stream_nc(src->track, type, idx)))
    ret->s = gavl_track_append_stream(src->track, type);
  
  gavl_stream_get_id(ret->s, &ret->stream_id);
    
  return ret;
  }

bg_media_source_stream_t *
bg_media_source_append_audio_stream(bg_media_source_t * src)
  {
  return bg_media_source_append_stream(src, GAVL_STREAM_AUDIO);
  }

bg_media_source_stream_t *
bg_media_source_append_video_stream(bg_media_source_t * src)
  {
  return bg_media_source_append_stream(src, GAVL_STREAM_VIDEO);
  }

bg_media_source_stream_t *
bg_media_source_append_text_stream(bg_media_source_t * src)
  {
  return bg_media_source_append_stream(src, GAVL_STREAM_TEXT);
  }

bg_media_source_stream_t *
bg_media_source_append_overlay_stream(bg_media_source_t * src)
  {
  return bg_media_source_append_stream(src, GAVL_STREAM_OVERLAY);
  }

bg_media_source_stream_t *
bg_media_source_append_msg_stream_by_id(bg_media_source_t * src, int id)
  {
  bg_media_source_stream_t * ret;
  ret = bg_media_source_append_stream(src, GAVL_STREAM_MSG);
  
  gavl_stream_set_id(ret->s, id);
  gavl_stream_get_id(ret->s, &ret->stream_id);
  
  return ret;
  }

void bg_media_source_set_from_source(bg_media_source_t * dst,
                                     bg_media_source_t * src)
  {
  int i;
  
  dst->streams = calloc(src->num_streams, sizeof(*dst->streams));
  
  dst->track_priv = gavl_dictionary_clone(src->track);
  dst->track = dst->track_priv;
  
  memcpy(dst->streams, src->streams, src->num_streams * sizeof(*dst->streams));
  dst->num_streams = src->num_streams;
  
  for(i = 0; i < dst->num_streams; i++)
    {
    dst->streams[i]->user_data = NULL;
    dst->streams[i]->free_user_data = NULL;
    
    dst->streams[i]->s = gavl_track_get_stream_all_nc(dst->track, i);
    
    /* Remove private members */
    dst->streams[i]->asrc_priv   = NULL;
    dst->streams[i]->vsrc_priv   = NULL;
    dst->streams[i]->psrc_priv   = NULL;
    dst->streams[i]->msghub_priv = NULL;
    }
  
  }

int bg_media_source_set_from_track(bg_media_source_t * src,
                                   gavl_dictionary_t * track)
  {
  gavl_stream_type_t type;
  int i, num;
  gavl_dictionary_t * s;
  bg_media_source_stream_t * src_s;

  src->track = track;
  
  num = gavl_track_get_num_streams_all(track);

  for(i = 0; i < num; i++)
    {
    if(!(s = gavl_track_get_stream_all_nc(track, i)) ||
       ((type = gavl_stream_get_type(s)) == GAVL_STREAM_NONE))
      return 0;
    src_s = bg_media_source_append_stream(src, type);
    src_s->s = s;

    if(type == GAVL_STREAM_MSG)
      gavl_stream_get_id(src_s->s, &src_s->stream_id);
    
    }
  return 1;
  }
  
bg_media_source_stream_t * bg_media_source_get_stream(bg_media_source_t * src, int type, int idx)
  {
  int i;
  int cnt = 0;
  
  for(i = 0; i < src->num_streams; i++)
    {
    if(type == src->streams[i]->type)
      {
      if(cnt == idx)
        return src->streams[i];
      else
        cnt++;
      }
    }
  return NULL;
  }

bg_media_source_stream_t * bg_media_source_get_stream_by_id(bg_media_source_t * src, int id)
  {
  int i;
  
  for(i = 0; i < src->num_streams; i++)
    {
    if(src->streams[i]->stream_id == id)
      return src->streams[i];
    }
  return NULL;
  
  }
  
bg_media_source_stream_t * bg_media_source_get_audio_stream(bg_media_source_t * src, int idx)
  {
  return bg_media_source_get_stream(src, GAVL_STREAM_AUDIO, idx);
  }

bg_media_source_stream_t * bg_media_source_get_video_stream(bg_media_source_t * src, int idx)
  {
  return bg_media_source_get_stream(src, GAVL_STREAM_VIDEO, idx);
  }

bg_media_source_stream_t * bg_media_source_get_text_stream(bg_media_source_t * src, int idx)
  {
  return bg_media_source_get_stream(src, GAVL_STREAM_TEXT, idx);
  }

bg_media_source_stream_t * bg_media_source_get_overlay_stream(bg_media_source_t * src, int idx)
  {
  return bg_media_source_get_stream(src, GAVL_STREAM_OVERLAY, idx);
  }

bg_media_source_stream_t * bg_media_source_get_msg_stream_by_id(bg_media_source_t * src, int id)
  {
  return bg_media_source_get_stream_by_id(src, id);
  }


gavl_audio_source_t * bg_media_source_get_audio_source(bg_media_source_t * src, int idx)
  {
  bg_media_source_stream_t * s;

  if(!(s = bg_media_source_get_audio_stream(src, idx)))
    return NULL;
  return s->asrc;
  }

gavl_video_source_t * bg_media_source_get_video_source(bg_media_source_t * src, int idx)
  {
  bg_media_source_stream_t * s;
  if(!(s = bg_media_source_get_video_stream(src, idx)))
    return NULL;
  return s->vsrc;
  
  }

gavl_video_source_t * bg_media_source_get_overlay_source(bg_media_source_t * src, int idx)
  {
  bg_media_source_stream_t * s;
  if(!(s = bg_media_source_get_overlay_stream(src, idx)))
    return NULL;
  return s->vsrc;

  }

gavl_packet_source_t * bg_media_source_get_audio_packet_source(bg_media_source_t * src, int idx)
  {
  bg_media_source_stream_t * s;
  if(!(s = bg_media_source_get_audio_stream(src, idx)))
    return NULL;
  return s->psrc;
  }

gavl_packet_source_t * bg_media_source_get_video_packet_source(bg_media_source_t * src, int idx)
  {
  bg_media_source_stream_t * s;
  if(!(s = bg_media_source_get_video_stream(src, idx)))
    return NULL;
  return s->psrc;
  
  }

gavl_packet_source_t * bg_media_source_get_text_source(bg_media_source_t * src, int idx)
  {
  bg_media_source_stream_t * s;
  if(!(s = bg_media_source_get_text_stream(src, idx)))
    return NULL;
  return s->psrc;
  
  }

gavl_packet_source_t * bg_media_source_get_overlay_packet_source(bg_media_source_t * src, int idx)
  {
  bg_media_source_stream_t * s;
  if(!(s = bg_media_source_get_overlay_stream(src, idx)))
    return NULL;
  return s->psrc;
 
  }

gavl_packet_source_t * bg_media_source_get_msg_packet_source_by_id(bg_media_source_t * src, int id)
  {
  bg_media_source_stream_t * s;
  if(!(s = bg_media_source_get_msg_stream_by_id(src, id)))
    return NULL;
  return s->psrc;
  }

bg_msg_hub_t * bg_media_source_get_msg_hub_by_id(bg_media_source_t * src, int id)
  {
  bg_media_source_stream_t * s;
  if(!(s = bg_media_source_get_msg_stream_by_id(src, id)))
    return NULL;
  return s->msghub;
  }

int bg_media_source_set_stream_action(bg_media_source_t * src, gavl_stream_type_t type, int idx,
                                       bg_stream_action_t action)
  {
  bg_media_source_stream_t * s;

  if(!(s = bg_media_source_get_stream(src, type, idx)))
    return 0;
  s->action = action;
  return 1;
  }

int bg_media_source_set_audio_action(bg_media_source_t * src, int idx,
                                     bg_stream_action_t action)
  {
  return bg_media_source_set_stream_action(src, GAVL_STREAM_AUDIO, idx, action);
  }

int bg_media_source_set_video_action(bg_media_source_t * src, int idx,
                                     bg_stream_action_t action)
  {
  return bg_media_source_set_stream_action(src, GAVL_STREAM_VIDEO, idx, action);
  
  }

int bg_media_source_set_text_action(bg_media_source_t * src, int idx,
                                    bg_stream_action_t action)
  {
  return bg_media_source_set_stream_action(src, GAVL_STREAM_TEXT, idx, action);
  
  }

int bg_media_source_set_overlay_action(bg_media_source_t * src, int idx,
                                       bg_stream_action_t action)
  {
  return bg_media_source_set_stream_action(src, GAVL_STREAM_OVERLAY, idx, action);
  
  }

int bg_media_source_set_msg_action_by_id(bg_media_source_t * src, int id,
                                         bg_stream_action_t action)
  {
  bg_media_source_stream_t * s;
  
  if(!(s = bg_media_source_get_msg_stream_by_id(src, id)))
    return 0;
  
  s->action = action;
  return 1;
  }

/* Synchronous seeking */

#if 0
typedef struct
  {
  int scale;
  int64_t * time;
  } seek_t;

static int handle_msg_seek(void * data, gavl_msg_t * msg)
  {
  seek_t * s = data;

  if((msg->NS == GAVL_MSG_NS_SRC) && 
     (msg->ID == GAVL_MSG_SRC_RESYNC))
    {
    int64_t msg_time = 0;
    int msg_scale    = 0;

    gavl_msg_get_src_resync(msg, &msg_time, &msg_scale, NULL, NULL);
    
    //    int flush = gavl_msg_get_arg_int(msg, 1);
    
    fprintf(stderr, "mediasrc: Got seek resync\n");
    
    *s->time = gavl_time_rescale(msg_scale, s->scale, msg_time);
    }
  return 1;
  }
#endif

void bg_media_source_seek(bg_media_source_t * src, bg_msg_sink_t * cmd_sink, bg_msg_hub_t * evt_hub,
                          int64_t * t, int scale)
  {
  //  seek_t s;

  gavl_msg_t * cmd;
  //  bg_msg_sink_t * sink;

  //  s.time = t;
  //  s.scale = scale;
  
  //  sink = bg_msg_sink_create(handle_msg_seek, &s, 1);

  //  bg_msg_hub_connect_sink(evt_hub, sink);
  
  /* Here we emulate synchronous seeking for asynchronous sources */
  /* Currently the only asynchronous source is the plug over pipes or sockets */
  
  //  src_async = gavl_track_is_async(src->track);
  
  cmd = bg_msg_sink_get(cmd_sink);
  gavl_msg_set_id_ns(cmd, GAVL_CMD_SRC_SEEK, GAVL_MSG_NS_SRC);
  gavl_msg_set_arg_long(cmd, 0, *t);
  gavl_msg_set_arg_int(cmd, 1, scale);
  
  bg_msg_sink_put(cmd_sink, cmd);
  
  //  bg_msg_hub_disconnect_sink(evt_hub, sink);
  }

/*

void bg_media_sink_init(bg_media_sink_t * sink);
void bg_media_sink_cleanup(bg_media_sink_t * sink);

bg_media_sink_stream_t *
bg_media_sink_append_stream(bg_media_sink_t * sink, gavl_stream_type_t type);

bg_media_sink_stream_t *
bg_media_sink_append_audio_stream(bg_media_sink_t * sink);

bg_media_sink_stream_t *
bg_media_sink_append_video_stream(bg_media_sink_t * sink);

bg_media_sink_stream_t *
bg_media_sink_append_text_stream(bg_media_sink_t * sink);

bg_media_sink_stream_t *
bg_media_sink_append_overlay_stream(bg_media_sink_t * sink);

bg_media_sink_stream_t *
bg_media_sink_append_msg_stream(bg_media_sink_t * sink);

bg_media_sink_stream_t * bg_media_sink_get_stream(bg_media_sink_t * sink, int type, int idx);
bg_media_sink_stream_t * bg_media_sink_get_audio_stream(bg_media_sink_t * sink, int idx);
bg_media_sink_stream_t * bg_media_sink_get_video_stream(bg_media_sink_t * sink, int idx);
bg_media_sink_stream_t * bg_media_sink_get_text_stream(bg_media_sink_t * sink, int idx);
bg_media_sink_stream_t * bg_media_sink_get_overlay_stream(bg_media_sink_t * sink, int idx);

*/


void bg_media_sink_init(bg_media_sink_t * sink)
  {
  memset(sink, 0, sizeof(*sink));
  }

void bg_media_sink_cleanup(bg_media_sink_t * sink)
  {
  int i;

  for(i = 0; i < sink->num_streams; i++)
    {
    if(sink->streams[i]->asink_priv)
      gavl_audio_sink_destroy(sink->streams[i]->asink_priv);
    if(sink->streams[i]->vsink_priv)
      gavl_video_sink_destroy(sink->streams[i]->vsink_priv);
    if(sink->streams[i]->psink_priv)
      gavl_packet_sink_destroy(sink->streams[i]->psink_priv);
    if(sink->streams[i]->msgsink_priv)
      bg_msg_sink_destroy(sink->streams[i]->msgsink_priv);
    
    if(sink->streams[i]->free_user_data && sink->streams[i]->user_data)
      sink->streams[i]->free_user_data(sink->streams[i]->user_data);

    free(sink->streams[i]);
    }
  if(sink->streams)
    free(sink->streams);

  if(sink->free_user_data && sink->user_data)
    sink->free_user_data(sink->user_data);
  
  }

bg_media_sink_stream_t *
bg_media_sink_append_stream(bg_media_sink_t * sink, gavl_stream_type_t type, gavl_dictionary_t * s)
  {
  bg_media_sink_stream_t * ret;

  if(sink->streams_alloc < sink->num_streams + 1)
    {
    sink->streams_alloc += 16;
    sink->streams = realloc(sink->streams, sink->streams_alloc * sizeof(*sink->streams));
    memset(sink->streams + sink->num_streams, 0, (sink->streams_alloc - sink->num_streams) *
           sizeof(*sink->streams));
    }
  sink->streams[sink->num_streams] = calloc(1, sizeof(*sink->streams[sink->num_streams]));
  ret = sink->streams[sink->num_streams];
  sink->num_streams++;

  ret->type = type;
  
  if(!s)
    ret->s = gavl_track_append_stream(sink->track, type);
  else
    ret->s = s;
  
  return ret;
  }

bg_media_sink_stream_t *
bg_media_sink_append_audio_stream(bg_media_sink_t * sink, gavl_dictionary_t * s)
  {
  return bg_media_sink_append_stream(sink, GAVL_STREAM_AUDIO, s);
  }

bg_media_sink_stream_t *
bg_media_sink_append_video_stream(bg_media_sink_t * sink, gavl_dictionary_t * s)
  {
  return bg_media_sink_append_stream(sink, GAVL_STREAM_VIDEO, s);
  }

bg_media_sink_stream_t *
bg_media_sink_append_text_stream(bg_media_sink_t * sink, gavl_dictionary_t * s)
  {
  return bg_media_sink_append_stream(sink, GAVL_STREAM_TEXT, s);
  }

bg_media_sink_stream_t *
bg_media_sink_append_overlay_stream(bg_media_sink_t * sink, gavl_dictionary_t * s)
  {
  return bg_media_sink_append_stream(sink, GAVL_STREAM_OVERLAY, s);
  }

bg_media_sink_stream_t *
bg_media_sink_append_msg_stream_by_id(bg_media_sink_t * sink, gavl_dictionary_t * s, int id)
  {
  bg_media_sink_stream_t * ret = bg_media_sink_append_stream(sink, GAVL_STREAM_MSG, s);
  gavl_stream_set_id(ret->s, id);
  gavl_stream_get_id(ret->s, &ret->stream_id);
  return ret;
  }

bg_media_sink_stream_t * bg_media_sink_get_stream(bg_media_sink_t * sink, int type, int idx)
  {
  int i;
  int cnt = 0;
  
  for(i = 0; i < sink->num_streams; i++)
    {
    if(type == sink->streams[i]->type)
      {
      if(cnt == idx)
        return sink->streams[i];
      else
        cnt++;
      }
    }
  return NULL;
  }

bg_media_sink_stream_t * bg_media_sink_get_audio_stream(bg_media_sink_t * sink, int idx)
  {
  return bg_media_sink_get_stream(sink, GAVL_STREAM_AUDIO, idx);
  }

bg_media_sink_stream_t * bg_media_sink_get_video_stream(bg_media_sink_t * sink, int idx)
  {
  return bg_media_sink_get_stream(sink, GAVL_STREAM_VIDEO, idx);
  }

bg_media_sink_stream_t * bg_media_sink_get_text_stream(bg_media_sink_t * sink, int idx)
  {
  return bg_media_sink_get_stream(sink, GAVL_STREAM_TEXT, idx);
  }

bg_media_sink_stream_t * bg_media_sink_get_overlay_stream(bg_media_sink_t * sink, int idx)
  {
  return bg_media_sink_get_stream(sink, GAVL_STREAM_OVERLAY, idx);
  }

bg_media_sink_stream_t * bg_media_sink_get_stream_by_id(bg_media_sink_t * sink, int id)
  {
  int i;

  for(i = 0; i < sink->num_streams; i++)
    {
    if(sink->streams[i]->stream_id == id)
      return sink->streams[i];
    }
  return NULL;
  }



#if 0
void bg_media_sink_set_from_sink(bg_media_sink_t * dst,
                                 bg_media_sink_t * src)
  {
  int i;
  
  dst->streams = calloc(src->num_streams, sizeof(*dst->streams));
  
  dst->track = gavl_dictionary_clone(src->track);
  
  memcpy(dst->streams, src->streams,
         src->num_streams * sizeof(*dst->streams));

  dst->num_streams = src->num_streams;
  
  for(i = 0; i < dst->num_streams; i++)
    {
    dst->streams[i].user_data = NULL;
    dst->streams[i].free_user_data = NULL;
    
    dst->streams[i].s = gavl_track_get_stream_all_nc(dst->track, i);
    
    /* Remove private members */
    dst->streams[i].asink_priv   = NULL;
    dst->streams[i].vsink_priv   = NULL;
    dst->streams[i].psink_priv   = NULL;
    dst->streams[i].msgsink_priv = NULL;
    }
  
  }
#endif
