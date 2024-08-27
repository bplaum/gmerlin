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
#include <pthread.h>

#include <gmerlin/mediaconnector.h>

#include <gmerlin/translation.h>
#include <gmerlin/log.h>
#define LOG_DOMAIN "mediaconnector"


static bg_mediaconnector_stream_t *
append_stream(bg_mediaconnector_t * conn,
              const gavl_dictionary_t * s, gavl_packet_source_t * psrc, gavl_stream_type_t type)
  {
  const gavl_dictionary_t * m;
  bg_mediaconnector_stream_t * ret;
  conn->streams = realloc(conn->streams,
                          (conn->num_streams+1) * sizeof(*conn->streams));
  
  ret = calloc(1, sizeof(*ret));

  conn->streams[conn->num_streams] = ret;
  conn->num_streams++;
  
  ret->s = gavl_track_append_stream(&conn->track, type);

  if(type == GAVL_STREAM_MSG)
    {
    int id;
    gavl_stream_get_id(s, &id);
    gavl_stream_set_id(ret->s, id);
    }
  
  ret->m = gavl_stream_get_metadata_nc(ret->s);
  
  if((m = gavl_stream_get_metadata(s)))
    gavl_dictionary_copy(ret->m, m);
  
  ret->psrc = psrc;
  ret->conn = conn;
  ret->last_status = GAVL_SOURCE_OK; 
  return ret;
  }

void
bg_mediaconnector_init(bg_mediaconnector_t * conn)
  {
  memset(conn, 0, sizeof(*conn));
  pthread_mutex_init(&conn->time_mutex, NULL);
  pthread_mutex_init(&conn->running_threads_mutex, NULL);
  }
                       

static void process_cb_audio(void * priv, gavl_audio_frame_t * frame)
  {
  bg_mediaconnector_stream_t * s = priv;
  s->time = gavl_time_unscale(s->timescale,
                              frame->timestamp + frame->valid_samples);
  bg_mediaconnector_update_time(s->conn, s->time);
  }

static void process_cb_video(void * priv, gavl_video_frame_t * frame)
  {
  bg_mediaconnector_stream_t * s = priv;
  s->time = gavl_time_unscale(s->timescale,
                              frame->timestamp + frame->duration);
  bg_mediaconnector_update_time(s->conn, s->time);
  }

static void process_cb_packet(void * priv, gavl_packet_t * p)
  {
  bg_mediaconnector_stream_t * s = priv;
  s->time = gavl_time_unscale(s->timescale,
                              p->pts + p->duration);
  bg_mediaconnector_update_time(s->conn, s->time);
  }

static gavl_source_status_t read_video_discont(void * data,
                                               gavl_video_frame_t ** f)
  {
  gavl_time_t frame_time, global_time;
  gavl_source_status_t st;
  bg_mediaconnector_stream_t * s = data;

  if(!s->discont_vframe)
    {
    if((st = gavl_video_source_read_frame(s->vsrc, &s->discont_vframe)) != GAVL_SOURCE_OK)
      return st;
    }
  
  global_time = bg_mediaconnector_get_time(s->conn);
  frame_time = gavl_time_unscale(s->timescale, s->discont_vframe->timestamp);

  if(frame_time < global_time + GAVL_TIME_SCALE / 2)
    {
    *f = s->discont_vframe;
    s->discont_vframe = NULL;
    return GAVL_SOURCE_OK;
    }
  return GAVL_SOURCE_AGAIN;
  }

static gavl_source_status_t read_packet_discont(void * data,
                                                gavl_packet_t ** p)
  {
  gavl_time_t frame_time, global_time;
  gavl_source_status_t st;
  bg_mediaconnector_stream_t * s = data;

  if(!s->discont_packet)
    {
    if((st = gavl_packet_source_read_packet(s->psrc, &s->discont_packet)) != GAVL_SOURCE_OK)
      return st;
    }
  
  global_time = bg_mediaconnector_get_time(s->conn);
  frame_time = gavl_time_unscale(s->timescale, s->discont_packet->pts);

  //  fprintf(stderr, "read_packet_discont %ld %ld\n", global_time, frame_time);
  
  if((global_time != GAVL_TIME_UNDEFINED) &&
     (frame_time < global_time + GAVL_TIME_SCALE / 2))
    {
    *p = s->discont_packet;
    s->discont_packet = NULL;
    return GAVL_SOURCE_OK;
    }
  return GAVL_SOURCE_AGAIN;
  }


static void create_connector(bg_mediaconnector_stream_t * ret)
  {
  if(ret->asrc)
    {
    ret->aconn = gavl_audio_connector_create(ret->asrc);
    gavl_audio_connector_set_process_func(ret->aconn,
                                          process_cb_audio,
                                          ret);
    }
  else if(ret->vsrc)
    {
    if(ret->flags & BG_MEDIACONNECTOR_FLAG_DISCONT)
      {
      ret->discont_vsrc = gavl_video_source_create(read_video_discont, ret, GAVL_SOURCE_SRC_ALLOC,
                                                   gavl_video_source_get_src_format(ret->vsrc));
      ret->vconn = gavl_video_connector_create(ret->discont_vsrc);
      }
    else
      ret->vconn = gavl_video_connector_create(ret->vsrc);
    gavl_video_connector_set_process_func(ret->vconn,
                                          process_cb_video,
                                          ret);
    }
  else if(ret->psrc)
    {
    if(ret->flags & BG_MEDIACONNECTOR_FLAG_DISCONT)
      {
      ret->discont_psrc =
        gavl_packet_source_create_source(read_packet_discont, ret, GAVL_SOURCE_SRC_ALLOC,
                                         ret->psrc);
      ret->pconn = gavl_packet_connector_create(ret->discont_psrc);
      }
    else
      ret->pconn = gavl_packet_connector_create(ret->psrc);
    gavl_packet_connector_set_process_func(ret->pconn,
                                           process_cb_packet,
                                           ret);
    }
  }


bg_mediaconnector_stream_t *
bg_mediaconnector_append_audio_stream(bg_mediaconnector_t * conn,
                                      const gavl_dictionary_t * s,
                                      gavl_audio_source_t * asrc,
                                      gavl_packet_source_t * psrc)
  {
  const gavl_audio_format_t * afmt = NULL;
  const gavl_compression_info_t * ci = NULL;
  gavl_compression_info_t ci_none;
  
  bg_mediaconnector_stream_t * ret = append_stream(conn, s, psrc, GAVL_STREAM_AUDIO);
  ret->type = GAVL_STREAM_AUDIO;
  ret->asrc = asrc;

  memset(&ci_none, 0, sizeof(ci_none));
  
  if(psrc)
    {
    //    ci = gavl_packet_source_get_ci(psrc);
    afmt = gavl_packet_source_get_audio_format(psrc);
    }
  else if(asrc)
    {
    ci = &ci_none;
    afmt = gavl_audio_source_get_src_format(asrc);
    }

  if(afmt)
    gavl_audio_format_copy(gavl_stream_get_audio_format_nc(ret->s), afmt);
  
  if(ci)
    gavl_stream_set_compression_info(ret->s, ci);
  
  return ret;
  }

bg_mediaconnector_stream_t *
bg_mediaconnector_append_video_stream(bg_mediaconnector_t * conn,
                                   const gavl_dictionary_t * s,
                                   gavl_video_source_t * vsrc,
                                   gavl_packet_source_t * psrc)
  {
  const gavl_video_format_t * vfmt = NULL;
  const gavl_compression_info_t * ci = NULL;
  gavl_compression_info_t ci_none;

  bg_mediaconnector_stream_t * ret = append_stream(conn, s, psrc, GAVL_STREAM_VIDEO);
  ret->type = GAVL_STREAM_VIDEO;
  ret->vsrc = vsrc;
  memset(&ci_none, 0, sizeof(ci_none));

  if(psrc)
    {
    //    ci = gavl_packet_source_get_ci(psrc);
    vfmt = gavl_packet_source_get_video_format(psrc);
    }
  else if(vsrc)
    {
    ci = &ci_none;
    vfmt = gavl_video_source_get_src_format(vsrc);
    }
  
  if(vfmt->framerate_mode == GAVL_FRAMERATE_STILL)
    ret->flags |= BG_MEDIACONNECTOR_FLAG_DISCONT;

  gavl_video_format_copy(gavl_stream_get_video_format_nc(ret->s), vfmt);
  gavl_stream_set_compression_info(ret->s, ci);
  
  return ret;
  }

bg_mediaconnector_stream_t *
bg_mediaconnector_append_overlay_stream(bg_mediaconnector_t * conn,
                                        const gavl_dictionary_t * s,
                                        gavl_video_source_t * vsrc,
                                        gavl_packet_source_t * psrc)
  {
  const gavl_video_format_t * vfmt = NULL;
  const gavl_compression_info_t * ci = NULL;
  gavl_compression_info_t ci_none;

  bg_mediaconnector_stream_t * ret = append_stream(conn, s, psrc, GAVL_STREAM_OVERLAY);

  memset(&ci_none, 0, sizeof(ci_none));
  
  ret->type = GAVL_STREAM_OVERLAY;
  ret->vsrc = vsrc;
  
  if(psrc)
    {
    //    ci = gavl_packet_source_get_ci(psrc);
    vfmt = gavl_packet_source_get_video_format(psrc);
    }
  else if(vsrc)
    {
    ci = &ci_none;
    vfmt = gavl_video_source_get_src_format(vsrc);
    }

  ret->flags |= BG_MEDIACONNECTOR_FLAG_DISCONT;

  if(vfmt)
    gavl_video_format_copy(gavl_stream_get_video_format_nc(ret->s), vfmt);
  
  if(ci)
    gavl_stream_set_compression_info(ret->s, ci);
  
  return ret;
  }

bg_mediaconnector_stream_t *
bg_mediaconnector_append_text_stream(bg_mediaconnector_t * conn,
                                     const gavl_dictionary_t * s,
                                     gavl_packet_source_t * psrc)
  {
  bg_mediaconnector_stream_t * ret = append_stream(conn, s, psrc, GAVL_STREAM_TEXT);
  ret->type = GAVL_STREAM_TEXT;
  ret->flags |= BG_MEDIACONNECTOR_FLAG_DISCONT;
  
  gavl_dictionary_get_int(ret->m, GAVL_META_STREAM_SAMPLE_TIMESCALE, &ret->timescale);
  return ret;
  }

bg_mediaconnector_stream_t *
bg_mediaconnector_append_msg_stream(bg_mediaconnector_t * conn,
                                    const gavl_dictionary_t * s,
                                    bg_msg_hub_t * msghub,
                                    gavl_packet_source_t * psrc)
  {
  bg_mediaconnector_stream_t * ret = append_stream(conn, s, psrc, GAVL_STREAM_MSG);
  ret->type = GAVL_STREAM_MSG;
  ret->flags |= BG_MEDIACONNECTOR_FLAG_DISCONT;
  ret->timescale = GAVL_TIME_SCALE;
  ret->msghub = msghub;
  return ret;
  }


void
bg_mediaconnector_create_conn(bg_mediaconnector_t * conn)
  {
  int i;
  for(i = 0; i < conn->num_streams; i++)
    create_connector(conn->streams[i]);
  }

void
bg_mediaconnector_free(bg_mediaconnector_t * conn)
  {
  int i;
  bg_mediaconnector_stream_t * s;
  if(conn->streams)
    {
    for(i = 0; i < conn->num_streams; i++)
      {
      s = conn->streams[i];

      if(s->free_priv)
        s->free_priv(s);
      
      if(s->discont_psrc)
        gavl_packet_source_destroy(s->discont_psrc);
      if(s->aconn)
        gavl_audio_connector_destroy(s->aconn);
      if(s->vconn)
        gavl_video_connector_destroy(s->vconn);
      if(s->pconn)
        gavl_packet_connector_destroy(s->pconn);
      if(s->th)
        bg_thread_destroy(s->th);
      free(s);
      }
    free(conn->streams);
    }
  gavl_dictionary_free(&conn->track);
  if(conn->tc)
    bg_thread_common_destroy(conn->tc);
  if(conn->th)
    free(conn->th);

  pthread_mutex_destroy(&conn->time_mutex);
  pthread_mutex_destroy(&conn->running_threads_mutex);
  }


void
bg_mediaconnector_reset(bg_mediaconnector_t * conn)
  {
  int i;
  
  conn->time = GAVL_TIME_UNDEFINED;
  for(i = 0; i < conn->num_streams; i++)
    {
    conn->streams[i]->time = GAVL_TIME_UNDEFINED;

    if(conn->streams[i]->aconn)
      gavl_audio_connector_reset(conn->streams[i]->aconn);
    if(conn->streams[i]->vconn)
      gavl_video_connector_reset(conn->streams[i]->vconn);
    if(conn->streams[i]->asrc)
      gavl_audio_source_reset(conn->streams[i]->asrc);
    if(conn->streams[i]->vsrc)
      gavl_video_source_reset(conn->streams[i]->vsrc);
#if 0
    if(conn->streams[i]->psrc)
      gavl_packet_source_reset(conn->streams[i]->psrc);
    if(conn->streams[i]->pconn)
      gavl_packet_connector_reset(conn->streams[i]->pconn);
#endif    
    }
  }
  
void
bg_mediaconnector_start(bg_mediaconnector_t * conn)
  {
  int i;
  bg_mediaconnector_stream_t * s;

  bg_mediaconnector_reset(conn);
  
  for(i = 0; i < conn->num_streams; i++)
    {
    s = conn->streams[i];
    
    switch(s->type)
      {
      case GAVL_STREAM_AUDIO:
        {
        const gavl_audio_format_t * afmt;
        if(s->aconn)
          {
          gavl_audio_connector_start(s->aconn);
          afmt = gavl_audio_connector_get_process_format(s->aconn);
          }
        else
          afmt = gavl_packet_source_get_audio_format(s->psrc);
        s->timescale = afmt->samplerate;
        }
        break;
      case GAVL_STREAM_VIDEO:
        {
        const gavl_video_format_t * vfmt;
        if(s->vconn)
          {
          gavl_video_connector_start(s->vconn);
          vfmt = gavl_video_connector_get_process_format(s->vconn);
          }
        else
          vfmt = gavl_packet_source_get_video_format(s->psrc);

        if(vfmt->framerate_mode == GAVL_FRAMERATE_STILL)
          s->flags |= BG_MEDIACONNECTOR_FLAG_DISCONT;
        
        s->timescale = vfmt->timescale;
        }
        break;
      case GAVL_STREAM_OVERLAY:
        {
        const gavl_video_format_t * vfmt;
        if(s->vconn)
          {
          gavl_video_connector_start(s->vconn);
          vfmt = gavl_video_connector_get_process_format(s->vconn);
          }
        else
          vfmt = gavl_packet_source_get_video_format(s->psrc);

        s->flags |= BG_MEDIACONNECTOR_FLAG_DISCONT;
        s->timescale = vfmt->timescale;
        }
        break;
      case GAVL_STREAM_TEXT:
        {
        s->flags |= BG_MEDIACONNECTOR_FLAG_DISCONT;
        }
        break;
      case GAVL_STREAM_MSG:
        {
        
        }
        break;
      case GAVL_STREAM_NONE:
        break;
      }
    }
  }

static int process_stream(bg_mediaconnector_stream_t * s)
  {
  int ret = 0;

  if(s->msghub)
    fprintf(stderr, "process_stream 1: %p %p %p\n", s->msghub, s->psrc, s->pconn);
  
  if(s->aconn)
    {
    ret = gavl_audio_connector_process(s->aconn);
    s->last_status = gavl_audio_connector_get_source_status(s->aconn);
    }
  else if(s->vconn)
    {
    ret = gavl_video_connector_process(s->vconn);
    s->last_status = gavl_video_connector_get_source_status(s->vconn);
    }
  else if(s->pconn)
    {
    ret = gavl_packet_connector_process(s->pconn);
    s->last_status = gavl_packet_connector_get_source_status(s->pconn);
    }
  if(!ret)
    s->flags |= BG_MEDIACONNECTOR_FLAG_EOF;
  else
    s->counter++;

  if(s->msghub)
    fprintf(stderr, "process_stream 2: %d\n", ret);
  
  return ret;
  }

gavl_time_t
bg_mediaconnector_get_min_time(bg_mediaconnector_t * conn)
  {
  int i;
  bg_mediaconnector_stream_t * s;
  gavl_time_t min_time= GAVL_TIME_UNDEFINED;
  
  for(i = 0; i < conn->num_streams; i++)
    {
    s = conn->streams[i];
    if(s->flags & (BG_MEDIACONNECTOR_FLAG_EOF |
                   BG_MEDIACONNECTOR_FLAG_DISCONT))
      continue;
    
    if((min_time == GAVL_TIME_UNDEFINED) ||
       (s->time < min_time))
      {
      min_time = s->time;
      }
    }
  return min_time;
  }

int bg_mediaconnector_iteration(bg_mediaconnector_t * conn)
  {
  int i;
  gavl_time_t min_time= GAVL_TIME_UNDEFINED;
  int min_index = -1;
  bg_mediaconnector_stream_t * s;
  int ret = 0;
  
  /* Process discontinuous streams */
  for(i = 0; i < conn->num_streams; i++)
    {
    s = conn->streams[i];

    if(s->flags & BG_MEDIACONNECTOR_FLAG_EOF)
      continue;

    if(s->msghub)
      continue;
    
    if(s->flags & BG_MEDIACONNECTOR_FLAG_DISCONT)
      ret += process_stream(s);
    }

  /* Find stream with minimum timestamp */
  
  for(i = 0; i < conn->num_streams; i++)
    {
    s = conn->streams[i];
    
    if(s->flags & (BG_MEDIACONNECTOR_FLAG_EOF |
                   BG_MEDIACONNECTOR_FLAG_DISCONT))
      continue;

    /* If we didn't get any data the last time, avoid an infinite
       loop and take another stream */
    if(s->last_status == GAVL_SOURCE_AGAIN)
      continue;

    ret++;
    
    if(s->time == GAVL_TIME_UNDEFINED)
      {
      min_index = i;
      break;
      }

    if((min_index == -1) ||
       (s->time < min_time))
      {
      min_time = s->time;
      min_index = i;
      }
    }
  
  /* Process this stream */
  if(min_index < 0)
    {
    if(!ret)
      gavl_log(GAVL_LOG_DEBUG, LOG_DOMAIN, "EOF (No stream left to process)");
    return ret;
    }
  s = conn->streams[min_index];
  
  //  fprintf(stderr, "Process stream %d\n", min_index);
  
  if(!process_stream(s))
    {
    if(!s->counter)
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Couldn't get first frame/packet");
      return 0;
      }
    }
  
  for(i = 0; i < conn->num_streams; i++)
    {
    s = conn->streams[i];
    if(i == min_index)
      continue;

    if(s->flags & (BG_MEDIACONNECTOR_FLAG_EOF |
                   BG_MEDIACONNECTOR_FLAG_DISCONT))
      continue;

    /* If we didn't get any data from this stream yet, it's worth to
       try again */
    s->last_status = GAVL_SOURCE_OK;
    }

  ret++;
  return ret;
  }

void
bg_mediaconnector_update_time(bg_mediaconnector_t * conn,
                              gavl_time_t time)
  {
  pthread_mutex_lock(&conn->time_mutex);
  if((conn->time == GAVL_TIME_UNDEFINED) ||
     (time > conn->time))
    conn->time = time;
  pthread_mutex_unlock(&conn->time_mutex);
  }

gavl_time_t
bg_mediaconnector_get_time(bg_mediaconnector_t * conn)
  {
  gavl_time_t ret;
  pthread_mutex_lock(&conn->time_mutex);
  ret = conn->time;
  pthread_mutex_unlock(&conn->time_mutex);
  return ret;
  }


/* Thread stuff */

void
bg_mediaconnector_create_threads_common(bg_mediaconnector_t * conn)
  {
  conn->tc = bg_thread_common_create();
  conn->th = calloc(conn->num_streams, sizeof(*conn->th));
  }

void
bg_mediaconnector_create_thread(bg_mediaconnector_t * conn, int index, int all)
  {
  bg_mediaconnector_stream_t * s;
  s = conn->streams[index];

  if(!all &&
     ((s->type == GAVL_STREAM_TEXT) ||
      (s->type == GAVL_STREAM_OVERLAY)))
    return;
  
  s->th = bg_thread_create(conn->tc);
  conn->th[conn->num_threads] = s->th;
  conn->num_threads++;
  }

void
bg_mediaconnector_create_threads(bg_mediaconnector_t * conn, int all)
  {
  int i;
  bg_mediaconnector_create_threads_common(conn);
  for(i = 0; i < conn->num_streams; i++)
    {
    bg_mediaconnector_create_thread(conn, i, all);
    }
  }

static void * thread_func_separate(void * data)
  {
  bg_mediaconnector_stream_t * s = data;

  if(!bg_thread_wait_for_start(s->th))
    return NULL;
  
  while(1)
    {
    if(!bg_thread_check(s->th))
      break;
    if(!process_stream(s))
      {
      pthread_mutex_lock(&s->conn->running_threads_mutex);
      s->conn->running_threads--;
      pthread_mutex_unlock(&s->conn->running_threads_mutex);
      bg_thread_exit(s->th);
      break;
      }
    }
  //  fprintf(stderr, "Thread done\n");
  return NULL;
  }

void
bg_mediaconnector_threads_init_separate(bg_mediaconnector_t * conn)
  {
  int i;
  bg_mediaconnector_stream_t * s;
  for(i = 0; i < conn->num_streams; i++)
    {
    s = conn->streams[i];
    if(s->th)
      bg_thread_set_func(s->th,
                         thread_func_separate, s);
    }
  conn->running_threads = conn->num_threads;
  }

void
bg_mediaconnector_threads_start(bg_mediaconnector_t * conn)
  {
  bg_threads_init(conn->th, conn->num_threads);
  bg_threads_start(conn->th, conn->num_threads);
  }

void
bg_mediaconnector_threads_stop(bg_mediaconnector_t * conn)
  {
  bg_threads_join(conn->th, conn->num_threads);
  }

int bg_mediaconnector_done(bg_mediaconnector_t * conn)
  {
  int ret;
  pthread_mutex_lock(&conn->running_threads_mutex);
  if(conn->running_threads == 0)
    ret = 1;
  else
    ret = 0;
  pthread_mutex_unlock(&conn->running_threads_mutex);
  return ret;
  }

int bg_mediaconnector_get_num_streams(bg_mediaconnector_t * conn,
                                      gavl_stream_type_t type)
  {
  int ret = 0;
  int i;
  for(i = 0; i < conn->num_streams; i++)
    {
    if(conn->streams[i]->type == type)
      ret++;
    }
  return ret;
  }

bg_mediaconnector_stream_t *
bg_mediaconnector_get_stream(bg_mediaconnector_t * conn,
                             gavl_stream_type_t type, int idx)
  {
  int i;
  int count = 0;
  for(i = 0; i < conn->num_streams; i++)
    {
    if(conn->streams[i]->type == type)
      {
      if(count == idx)
        return conn->streams[i];
      count++;
      }
    }
  return NULL;
  }

/* Setup the input side for the media connector. The source needs to be started already */
void bg_mediaconnector_set_from_source(bg_mediaconnector_t * conn, bg_media_source_t * src)
  {
  int i;

  bg_media_source_stream_t * s;
  bg_mediaconnector_stream_t * cs;
  
  for(i = 0; i < src->num_streams; i++)
    {
    s = src->streams[i];
    
    if(s->action == BG_STREAM_ACTION_OFF)
      continue;
    
    switch(s->type)
      {
      case GAVL_STREAM_AUDIO:
        {
        cs = bg_mediaconnector_append_audio_stream(conn, s->s, s->asrc, s->psrc);
        cs->src_index = gavl_track_stream_idx_to_rel(src->track, i);
        }
        break;
      case GAVL_STREAM_VIDEO:
        {
        cs = bg_mediaconnector_append_video_stream(conn, s->s, s->vsrc, s->psrc);
        cs->src_index = gavl_track_stream_idx_to_rel(src->track, i);
        }
        break;
      case GAVL_STREAM_TEXT:
        {
        cs = bg_mediaconnector_append_text_stream(conn, s->s, s->psrc);
        cs->src_index = gavl_track_stream_idx_to_rel(src->track, i);
        }
        break;
      case GAVL_STREAM_OVERLAY:
        {
        cs = bg_mediaconnector_append_overlay_stream(conn, s->s, s->vsrc, s->psrc);
        cs->src_index = gavl_track_stream_idx_to_rel(src->track, i);
        }
        break;
      case GAVL_STREAM_MSG:
        {
        cs = bg_mediaconnector_append_msg_stream(conn, s->s, s->msghub, s->psrc);
        cs->src_index = gavl_track_stream_idx_to_rel(src->track, i);
        }
        break;
      case GAVL_STREAM_NONE:
        break;
      }
    }
  }
