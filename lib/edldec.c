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
#include <gavl/metatags.h>

#include <gmerlin/translation.h>


#include <gmerlin/pluginregistry.h>
#include <gmerlin/utils.h>

#include <pluginreg_priv.h>
#include <gavl/trackinfo.h>
#include <gavl/edl.h>


#include <gmerlin/log.h>
#define LOG_DOMAIN "edldec"

typedef struct
  {
  int track;        //!<  Track index for multitrack inputs
  int stream;       //!<  Index of the A/V stream
  int timescale;    //!<  Source timescale
    
  int64_t src_time; //!< Time within the source in source timescale
  
  /* Time and duration within the destination in destination
     timescale */
  int64_t dst_time;  //!< Time  within the destination in destination timescale
  int64_t dst_duration; //!< Duration within the destination in destination timescale

  /*  */
  int32_t speed_num; //!< Playback speed numerator
  int32_t speed_den; //!< Playback speed demoninator

  const gavl_dictionary_t * seg;
  } edl_segment_t;

typedef struct
  {
  bg_plugin_handle_t * h;
  bg_input_plugin_t * plugin;
  int track;
  const char * location;
  int refcount;
  int stream;
  gavl_stream_type_t type;  
  gavl_dictionary_t * st;
  } source_t;

typedef struct
  {
  gavl_audio_source_t * asrc_int;
  gavl_video_source_t * vsrc_int;
  gavl_packet_source_t * psrc_int;
  
  gavl_dictionary_t * s;
  //  const edl_segment_t * seg;
  
  edl_segment_t * segs;
  int num_segs;
  int seg_idx;
  
  source_t * src;
  
  int64_t mute_time;
  int64_t pts;

  struct edldec_s * dec;

  gavl_audio_format_t * afmt;
  gavl_video_format_t * vfmt;

  gavl_stream_stats_t stats;

  int timescale;
  bg_media_source_stream_t * src_s;
  //  gavl_stream_type_t type;  
  
  } stream_t;

struct edldec_s
  {
  int num_sources;
  source_t * sources;

  stream_t * streams;
  int num_streams;
  
  //  const gavl_edl_t * edl;
  //  const gavl_edl_track_t * t;

  gavl_dictionary_t mi;
  gavl_dictionary_t * ti_cur;

  bg_media_source_t src;
  
  bg_controllable_t controllable;
  
  };

typedef struct edldec_s edldec_t;

static void streams_destroy(edldec_t *dec);

static void free_segments(edl_segment_t * s, int len)
  {
  free(s);
  }

static int stream_eof(const stream_t * st)
  {
  if((st->seg_idx >= st->num_segs) &&
     (st->mute_time <= 0))
    return 1;
  else
    return 0;
  }

static int64_t edl_src_time_to_dst(const stream_t * st,
                                   const edl_segment_t * seg,
                                   int64_t src_time)
  {
  int64_t ret;
  /* Offset from the segment start in src scale */
  ret = src_time - seg->src_time; 
  
  /* Src scale -> dst_scale */
  ret = gavl_time_rescale(seg->timescale, st->timescale, ret);
  
  /* Add offset of the segment start in dst scale */
  ret += seg->dst_time;
  
  return ret;
  }


static int source_init(source_t * s, 
                       edldec_t * dec,
                       const edl_segment_t * seg,
                       gavl_stream_type_t type)
  {
  bg_media_source_stream_t * src_s;
  gavl_dictionary_t * ti;
  
  s->location = gavl_dictionary_get_string(seg->seg, GAVL_META_URI);
  if(!s->location)
    s->location = gavl_dictionary_get_string(&dec->mi, GAVL_META_URI);
  
  if(!(s->h = bg_input_plugin_load(s->location)))
    return 0;
  
  s->track = seg->track;
  s->stream = seg->stream;
  s->type = type;

  s->plugin = (bg_input_plugin_t*)(s->h->plugin);
  
  if(!bg_input_plugin_set_track(s->h, s->track))
    return 0;
  ti = bg_input_plugin_get_track_info(s->h, s->track);
  
  if(!gavl_track_can_seek(ti))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "EDL only works with seekable sources");
    return 0;
    }

  bg_media_source_set_stream_action(s->h->src, type, s->stream, BG_STREAM_ACTION_DECODE);
  
  bg_input_plugin_start(s->h);

  src_s = bg_media_source_get_stream(s->h->src, type, s->stream);
  
  s->st = src_s->s;
  
#if 0  
  /* Copy stream stats */
  if(dec->streaming)
    {
    bg_media_source_stream_t * src_stream = bg_media_source_get_stream(s->h->src, type, s->stream);
    
    }
#endif
  return 1;
  }

static void source_cleanup(source_t * s)
  {
  if(s->h)
    bg_plugin_unref(s->h);
  memset(s, 0, sizeof(*s));
  }

static void source_ref(source_t * s)
  {
  s->refcount++;
  }

static void source_unref(source_t * s)
  {
  s->refcount--;
  }

static int segment_from_dictionary(edl_segment_t * seg, 
                                   const gavl_dictionary_t * dict)
  {
  seg->seg = dict;

  seg->speed_num = 1;
  seg->speed_den = 1;
  
  if(!gavl_dictionary_get_int(dict,  GAVL_EDL_TRACK_IDX,  &seg->track) ||
     !gavl_dictionary_get_int(dict,  GAVL_EDL_STREAM_IDX, &seg->stream) ||
     !gavl_dictionary_get_int(dict,  GAVL_META_STREAM_SAMPLE_TIMESCALE, &seg->timescale) ||
     !gavl_dictionary_get_long(dict, GAVL_EDL_SRC_TIME,   &seg->src_time) ||
     !gavl_dictionary_get_long(dict, GAVL_EDL_DST_TIME,   &seg->dst_time) ||
     !gavl_dictionary_get_long(dict, GAVL_EDL_DST_DUR,    &seg->dst_duration))
    {
    fprintf(stderr, "segment_from_dictionary\n");
    gavl_dictionary_dump(dict, 2);
    return 0;
    }
     
  gavl_dictionary_get_int(dict,  GAVL_EDL_SPEED_NUM,  &seg->speed_num);
  gavl_dictionary_get_int(dict,  GAVL_EDL_SPEED_DEN,  &seg->speed_den);
     
  return 1;
  }

static int stream_init(stream_t * s, gavl_dictionary_t * es,
                       struct edldec_s * dec)
  {
  int i;
  const gavl_array_t * segs;
  const gavl_dictionary_t * dict;
  const gavl_value_t * val;
  
  s->s = es;

  gavl_stream_stats_init(&s->stats);
  
  if((segs = gavl_dictionary_get_array(es, GAVL_EDL_SEGMENTS)))
    {
    s->num_segs = segs->num_entries;
    s->segs = calloc(s->num_segs, sizeof(*s->segs));

    for(i = 0; i < s->num_segs; i++)
      {

      if((val = gavl_array_get(segs, i)) &&
         (dict = gavl_value_get_dictionary(val)))
        {
        if(!segment_from_dictionary(&s->segs[i], dict))
          return 0;
        }
      }
    }

  dict = gavl_stream_get_metadata(es);
  
  gavl_dictionary_get_int(dict, GAVL_META_STREAM_SAMPLE_TIMESCALE, &s->timescale);
  //  fprintf(stderr, "Got timescale %d\n", s->timescale);
  
  s->dec = dec;

  return 1;
  }

static void stream_cleanup(stream_t * s)
  {
  free_segments(s->segs, s->num_segs);
  }


static int
edl_dst_time_to_src(edldec_t * dec,
                    const stream_t * st,
                    int64_t dst_time,
                    int64_t * src_time,
                    int64_t * mute_time)
  {
  int i;
  const edl_segment_t * seg = NULL;
  
  for(i = 0; i < st->num_segs; i++)
    {
    if(st->segs[i].dst_time + st->segs[i].dst_duration > dst_time)
      {
      seg = &st->segs[i];
      break;
      }
    }

  if(!seg) // After the last segment
    {
    gavl_time_t duration = gavl_track_get_duration(dec->ti_cur);

    *mute_time = gavl_time_scale(st->timescale, duration) - dst_time;
    if(*mute_time < 0)
      *mute_time = 0;

    return st->num_segs;
    }
    
  *src_time = seg->src_time;
  
  if(seg->dst_time > dst_time)
    *mute_time = seg->dst_time - dst_time;

  if(dst_time > seg->dst_time)
    {
    *src_time += gavl_time_rescale(st->timescale,
                                   seg->timescale,
                                   dst_time - seg->dst_time);
    }
  return i;
  }


static source_t * get_source(edldec_t * dec, gavl_stream_type_t type,
                             const edl_segment_t * seg,
                             int64_t src_time)
  {
  int i;
  source_t * ret = NULL;
  const char * location =
    gavl_dictionary_get_string(seg->seg, GAVL_META_URI);

  if(!location)
    location = gavl_dictionary_get_string(&dec->mi, GAVL_META_URI);
  
  /* Find a cached source */
  for(i = 0; i < dec->num_sources; i++)
    {
    source_t * s = dec->sources + i;

    if(s->location &&
       !strcmp(s->location, location) &&
       (s->track == seg->track) &&
       (s->type == type) &&
       (s->stream == seg->stream) &&
       !s->refcount)
      ret = s;
    }
  
  /* Find an empty slot */
  if(!ret)
    {
    for(i = 0; i < dec->num_sources; i++)
      {
      source_t * s = dec->sources + i;
      if(!s->location)
        {
        ret = s;
        break;
        }
      }
    }

  /* Free an existing slot */
  if(!ret)
    {
    for(i = 0; i < dec->num_sources; i++)
      {
      source_t * s = dec->sources + i;
      if(!s->refcount)
        {
        ret = s;
        source_cleanup(ret);
        break;
        }
      }
    }

  if(!ret->location)
    {
    if(!source_init(ret, dec, seg, type))
      return NULL;
    }

  gavl_log(GAVL_LOG_DEBUG, LOG_DOMAIN, "Seeking to %"PRId64, src_time);
  bg_input_plugin_seek(ret->h, src_time, seg->timescale);
  gavl_log(GAVL_LOG_DEBUG, LOG_DOMAIN, "Seeked to %"PRId64, src_time);
  
  return ret;  
  }

static bg_media_source_t * get_src_edl(void * priv)
  {
  edldec_t * ed = priv;
  return &ed->src;
  }


static gavl_dictionary_t * get_media_info_edl(void * priv)
  {
  edldec_t * ed = priv;
  return &ed->mi;
  }

static int streams_create(edldec_t * dec)
  {
  int num, i;
  
  if(!(num = gavl_track_get_num_streams_all(dec->ti_cur)))
    return 1;
  
  dec->streams = calloc(num, sizeof(*dec->streams));

  //  fprintf(stderr, "streams_create\n");
  //  gavl_dictionary_dump(dec->ti_cur, 2);

  bg_media_source_set_from_track(&dec->src, dec->ti_cur);
  
  for(i = 0; i < num; i++)
    {
    gavl_dictionary_t * d;

    if((d = gavl_track_get_stream_all_nc(dec->ti_cur, i)))
      {
      if(!stream_init(&dec->streams[i], d, dec))
        goto fail;
      dec->streams[i].src_s = dec->src.streams[i];
      }
    }

  return 1;
  
  fail:
  
  streams_destroy(dec);
  dec->streams = NULL;
  return 0;
  }

static void streams_destroy(edldec_t * ed)
  {
  int i;
  int num = 0;
  if(!ed->streams)
    return;

  if(ed->ti_cur)
    num = gavl_track_get_num_streams_all(ed->ti_cur);
  
  for(i = 0; i < num; i++)
    stream_cleanup(&ed->streams[i]);
  
  free(ed->streams);
  }

static int set_track_edl(void * priv, int track)
  {
  int i;
  edldec_t * ed = priv;
  /* Reset sources */
  for(i = 0; i < ed->num_sources; i++)
    ed->sources[i].refcount = 0;
  
  /* Clean up earlier streams */
  if(ed->ti_cur)
    {
    streams_destroy(ed);
    bg_media_source_cleanup(&ed->src);
    bg_media_source_init(&ed->src);
    }
  ed->ti_cur = gavl_get_track_nc(&ed->mi, track);

  if(!streams_create(ed))
    return 0;
  
  return 1;
  }

static void destroy_edl(void * priv)
  {
  int i;
  edldec_t * ed = priv;

  if(ed->ti_cur)
    streams_destroy(ed);
  
  if(ed->sources)
    {
    for(i = 0; i < ed->num_sources; i++)
      source_cleanup(&ed->sources[i]);
    free(ed->sources);
    }

  bg_media_source_cleanup(&ed->src);
  
  gavl_dictionary_free(&ed->mi);

  bg_controllable_cleanup(&ed->controllable);
  free(ed);
  }

  
static gavl_audio_source_t * get_audio_source(stream_t * s,
                                              int64_t src_time)
  {
  const edl_segment_t * seg = &s->segs[s->seg_idx];
    
  if(s->src)
    source_unref(s->src);
  if(!(s->src = get_source(s->dec, GAVL_STREAM_AUDIO, seg, src_time)))
    return NULL;
  source_ref(s->src);
  return bg_media_source_get_audio_source((s->src)->h->src, seg->stream);
  }

static gavl_video_source_t * get_video_source(stream_t * s,
                                              int64_t src_time)
  {
  const edl_segment_t * seg = &s->segs[s->seg_idx];
  if(s->src)
    source_unref(s->src);
  if(!(s->src = get_source(s->dec, GAVL_STREAM_VIDEO, seg, src_time)))
    return NULL;
  source_ref(s->src);
  
  return bg_media_source_get_video_source((s->src)->h->src, seg->stream);
  }

static gavl_packet_source_t * get_text_source(stream_t * s,
                                              int64_t src_time)
  {
  const edl_segment_t * seg = &s->segs[s->seg_idx];
  if(s->src)
    source_unref(s->src);
  if(!(s->src = get_source(s->dec, GAVL_STREAM_TEXT, seg, src_time)))
    return NULL;
  source_ref(s->src);
  return bg_media_source_get_text_source((s->src)->h->src, seg->stream);
  }

static gavl_video_source_t * get_overlay_source(stream_t * s,
                                                int64_t src_time)
  {
  const edl_segment_t * seg = &s->segs[s->seg_idx];
  if(s->src)
    source_unref(s->src);

  if(!(s->src = get_source(s->dec, GAVL_STREAM_OVERLAY, seg, src_time)))
    return NULL;
  source_ref(s->src);
  return bg_media_source_get_overlay_source((s->src)->h->src, seg->stream);
  }

/* Check if pts is inside the segment. */

static int64_t next_segment_start_pts(stream_t * s)
  {
  if(s->seg_idx >= s->num_segs-1)
    return GAVL_TIME_UNDEFINED;
  else
    return s->segs[s->seg_idx+1].dst_time;
  }

// -1: pts is before segment
//  0: pts is inside of segment
//  1: pts is after segment

static int check_segment(stream_t * s, int64_t pts)
  {
  if(s->seg_idx >= s->num_segs)
    return -1;

  if(s->seg_idx < 0)
    return 1;

  if(s->segs[s->seg_idx].dst_time > pts)
    return -1;

  if(s->segs[s->seg_idx].dst_time + s->segs[s->seg_idx].dst_duration < pts)
    return 1;
  
  return 0; // Inside
  }


static void advance_segment(stream_t * s, int64_t * src_time)
  {
  /* Set pts of the mute frames */
  s->pts = s->segs[s->seg_idx].dst_time + s->segs[s->seg_idx].dst_duration;
  s->seg_idx = edl_dst_time_to_src(s->dec, s, s->pts,
                                   src_time, &s->mute_time);
  }

static int64_t truncate_duration(stream_t * s, int64_t pts, int64_t duration)
  {
  if(duration <= 0)
    return duration;

  if((s->seg_idx >= 0) && 
     (s->seg_idx < s->num_segs) &&
     (duration > 0) &&
     (pts + duration > 
      s->segs[s->seg_idx].dst_time + s->segs[s->seg_idx].dst_duration))
    {
    duration = s->segs[s->seg_idx].dst_time + s->segs[s->seg_idx].dst_duration - pts;
    if(duration < 0)
      duration = 0;
    }
  return duration;
  }

static gavl_source_status_t read_audio(void * priv,
                                       gavl_audio_frame_t ** frame)
  {
  int i;
  gavl_source_status_t st;
  stream_t * s = priv;

  /* Early return */
  if(stream_eof(s))
    return GAVL_SOURCE_EOF;

  /* Check for mute */
  if(s->mute_time)
    {
    gavl_audio_frame_mute(*frame, s->afmt);
    if((*frame)->valid_samples > s->mute_time)
      (*frame)->valid_samples = s->mute_time;
    s->mute_time -= (*frame)->valid_samples;
    (*frame)->timestamp = s->pts;
    s->pts += (*frame)->valid_samples;
    return GAVL_SOURCE_OK;
    }

  i = check_segment(s, s->pts);

  if(i > 0) // PTS is after segment
    {
    int64_t src_time;
    //    fprintf(stderr, "Advancing audio segment\n");
    advance_segment(s, &src_time);

    if(s->seg_idx < s->num_segs)
      {
      if(!(s->asrc_int = get_audio_source(s, src_time)))
        return GAVL_SOURCE_EOF;
      gavl_audio_source_set_dst(s->asrc_int, 0, s->afmt);
      }
    else
      s->asrc_int = NULL;
    
    /* Call ourselves again */
    return read_audio(priv, frame);
    }
  
  
  /* Read audio */
  st = gavl_audio_source_read_frame(s->asrc_int, frame);
  if(st != GAVL_SOURCE_OK)
    {
    int64_t next_pts;

    /* Unexpected EOF but more segments are coming: Insert silence */
    if((next_pts = next_segment_start_pts(s)) != GAVL_TIME_UNDEFINED)
      {
      s->mute_time = next_pts - s->pts;

      if(s->mute_time > 0)
        {
        //        fprintf(stderr, "Inserting silence %"PRId64" samples\n", s->mute_time);
        
        /* Correct wrong segment duration so that advancing succeeds later on */
        s->segs[s->seg_idx].dst_duration = s->pts - s->segs[s->seg_idx].dst_time;
        
        /* Call ourselves again */
        return read_audio(priv, frame);
        }
      else
        return st;
      }
    else
      return st;
    }
  (*frame)->timestamp = s->pts;
  (*frame)->valid_samples = truncate_duration(s, (*frame)->timestamp, (*frame)->valid_samples);

  s->pts += (*frame)->valid_samples;
  
  return (*frame)->valid_samples > 0 ? GAVL_SOURCE_OK : GAVL_SOURCE_EOF;
  }

static int start_audio_stream(edldec_t * ed, int idx_rel)
  {
  int64_t src_time;
  stream_t * s;
  int idx_abs;
  gavl_dictionary_t * m;
  
  idx_abs = gavl_track_stream_idx_to_abs(ed->ti_cur, GAVL_STREAM_AUDIO, idx_rel);
  
  s = &ed->streams[idx_abs];

  // bg_audio_info_t * ai
  
  if(!s || !s->src_s)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Stream not initialized ");
    return 0;
    }

  
  if(s->src_s->action == BG_STREAM_ACTION_OFF)
    return 1;

  if(!s->num_segs)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Got no audio segment");
    return 0;
    }
  
  s->seg_idx = edl_dst_time_to_src(ed, s, 0, &src_time, &s->mute_time);

  if(!(s->asrc_int = get_audio_source(s, src_time)))
    return 0;

  /* Get format */
  //  fprintf(stderr, "Got track\n");
  //  gavl_dictionary_dump(ed->ti_cur, 2);
  
  s->afmt = gavl_track_get_audio_format_nc(ed->ti_cur, idx_rel);
  
  gavl_audio_format_copy(s->afmt,
                         gavl_audio_source_get_src_format(s->asrc_int));

  m = gavl_track_get_audio_metadata_nc(ed->ti_cur, idx_rel);
  
  /* Adjust format */

  if(s->timescale)
    s->afmt->samplerate = s->timescale;
  else
    s->timescale = s->afmt->samplerate;

  gavl_dictionary_set_int(m, GAVL_META_STREAM_SAMPLE_TIMESCALE, s->timescale);

  s->afmt->samples_per_frame = 1024;
  
  /* Set destination format */
  gavl_audio_source_set_dst(s->asrc_int, 0, s->afmt);

  /* Create external source */
  s->src_s->asrc_priv =
    gavl_audio_source_create(read_audio, s, GAVL_SOURCE_SRC_FRAMESIZE_MAX, s->afmt);
  
  s->src_s->asrc = s->src_s->asrc_priv;
  s->pts = 0;
  
  return 1;
  }

static gavl_source_status_t read_video(void * priv,
                                       gavl_video_frame_t ** frame)
  {
  gavl_source_status_t st;
  stream_t * s = priv;

  /* Early return */
  if(stream_eof(s))
    return GAVL_SOURCE_EOF;

  if(s->mute_time > 0)
    {
    gavl_video_frame_clear(*frame, s->vfmt);
    (*frame)->timestamp = s->pts;
    (*frame)->duration = s->mute_time;
    return GAVL_SOURCE_EOF;
    }

  st = gavl_video_source_read_frame(s->vsrc_int, frame);

  if(st == GAVL_SOURCE_OK)
    (*frame)->timestamp = edl_src_time_to_dst(s, s->segs + s->seg_idx, (*frame)->timestamp);
  
  //  fprintf(stderr, "Read video done");
  if((st != GAVL_SOURCE_OK) || (check_segment(s, (*frame)->timestamp) > 0))
    {
    int64_t src_time;
    advance_segment(s, &src_time);
    
    if(s->seg_idx < s->num_segs)
      {
      if(!(s->vsrc_int = get_video_source(s, src_time)))
        return GAVL_SOURCE_EOF;
      gavl_video_source_set_dst(s->vsrc_int, 0, s->vfmt);
      }
    else
      s->vsrc_int = NULL;
    
    /* Call ourselves again */
    return read_video(priv, frame);
    }
  
  (*frame)->duration = truncate_duration(s, (*frame)->timestamp, (*frame)->duration);
  return GAVL_SOURCE_OK;
  }

static int start_video_stream(edldec_t * ed, int idx_rel)
  {
  int64_t src_time;
  int idx_abs;
  stream_t * s;
  gavl_dictionary_t * m;

  idx_abs = gavl_track_stream_idx_to_abs(ed->ti_cur, GAVL_STREAM_VIDEO, idx_rel);
  
  s = &ed->streams[idx_abs];
  // bg_video_info_t * vi
  
  if(s->src_s->action == BG_STREAM_ACTION_OFF)
    return 1;

  if(!s->num_segs)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Got no video segment");
    return 0;
    }
  
  s->seg_idx = edl_dst_time_to_src(ed, s, 0, &src_time, &s->mute_time);
  
  if(!(s->vsrc_int = get_video_source(s, src_time)))
    return 0;

  /* Get format */
  s->vfmt = gavl_track_get_video_format_nc(ed->ti_cur, idx_rel);

  m = gavl_track_get_video_metadata_nc(ed->ti_cur, idx_rel);
  
  gavl_video_format_copy(s->vfmt,
                         gavl_video_source_get_src_format(s->vsrc_int));

  /* Adjust format */
  if(s->timescale)
    s->vfmt->timescale = s->timescale;
  else
    s->timescale = s->vfmt->timescale;

  gavl_dictionary_set_int(m, GAVL_META_STREAM_SAMPLE_TIMESCALE, s->timescale);

  s->vfmt->frame_duration = 0;
  s->vfmt->framerate_mode = GAVL_FRAMERATE_VARIABLE;
  
  /* Set destination format */
  gavl_video_source_set_dst(s->vsrc_int, 0, s->vfmt);

  /* Create external source */
  s->src_s->vsrc_priv = gavl_video_source_create(read_video, s, 0, s->vfmt);
  s->src_s->vsrc = s->src_s->vsrc_priv;
  s->pts = 0;
  return 1;
  }

static gavl_source_status_t read_text(void * priv,
                                      gavl_packet_t ** p)
  {
  gavl_source_status_t st;
  stream_t * s = priv;

  /* Early return */

  if(stream_eof(s))
    return GAVL_SOURCE_EOF;

  st = gavl_packet_source_read_packet(s->psrc_int, p);
  
  if(st == GAVL_SOURCE_OK)
    {
    (*p)->pts = edl_src_time_to_dst(s, s->segs + s->seg_idx, (*p)->pts);
    (*p)->duration = gavl_time_rescale(s->segs[s->seg_idx].timescale, s->timescale, (*p)->duration);
    }

  if((st != GAVL_SOURCE_OK) ||
     (check_segment(s, (*p)->pts) > 0))
    {
    int64_t src_time;
    advance_segment(s, &src_time);
    
    if(s->seg_idx < s->num_segs)
      {
      if(!(s->psrc_int = get_text_source(s, src_time)))
        return GAVL_SOURCE_EOF;
      }
    else
      return GAVL_SOURCE_EOF;
    
    /* Call ourselves again */
    return read_text(priv, p);
    }

  (*p)->duration = truncate_duration(s, (*p)->pts, (*p)->duration);

  return GAVL_SOURCE_OK;
  }

static int start_text_stream(edldec_t * ed, int idx_rel)
  {
  int64_t src_time;
  gavl_dictionary_t * dict;
  stream_t * s;
  int idx_abs;
  
  idx_abs = gavl_track_stream_idx_to_abs(ed->ti_cur, GAVL_STREAM_TEXT, idx_rel);
  
  s = &ed->streams[idx_abs];
  
  if(s->src_s->action == BG_STREAM_ACTION_OFF)
    return 1;

  if(!s->num_segs)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Got no text segment");
    return 0;
    }
  
  s->seg_idx = edl_dst_time_to_src(ed, s, 0, &src_time, &s->mute_time);
  
  if(!(s->psrc_int = get_text_source(s, src_time)))
    return 0;
  
  dict = gavl_track_get_text_metadata_nc(ed->ti_cur, idx_rel);
  gavl_dictionary_set_int(dict, GAVL_META_STREAM_SAMPLE_TIMESCALE, s->timescale);
  
  /* Create external source */
  s->src_s->psrc_priv = gavl_packet_source_create(read_text, s, 0, dict);
  s->src_s->psrc = s->src_s->psrc_priv;
  
  return 1;
  }

static gavl_source_status_t read_overlay(void * priv,
                                         gavl_video_frame_t ** f)
  {
  gavl_source_status_t st;
  stream_t * s = priv;

  /* Early return */

  if(stream_eof(s))
    return GAVL_SOURCE_EOF;

  st = gavl_video_source_read_frame(s->psrc_int, f);
  
  if(st == GAVL_SOURCE_OK)
    {
    (*f)->timestamp = edl_src_time_to_dst(s, s->segs + s->seg_idx, (*f)->timestamp);
    (*f)->duration = gavl_time_rescale(s->segs[s->seg_idx].timescale, s->timescale, (*f)->duration);
    }

  if((st != GAVL_SOURCE_OK) ||
     (check_segment(s, (*f)->timestamp) > 0))
    {
    gavl_video_format_t fmt;
    int64_t src_time;
    advance_segment(s, &src_time);
    
    if(s->seg_idx < s->num_segs)
      {
      if(!(s->vsrc_int = get_overlay_source(s, src_time)))
        return GAVL_SOURCE_EOF;

      gavl_video_format_copy(&fmt, s->vfmt);
      fmt.timescale = s->segs[s->seg_idx].timescale;
      
      /* Set destination format */
      gavl_video_source_set_dst(s->vsrc_int, 0, &fmt);
      
      }
    else
      return GAVL_SOURCE_EOF;
    
    /* Call ourselves again */
    return read_overlay(priv, f);
    }

  (*f)->duration = truncate_duration(s, (*f)->timestamp, (*f)->duration);
  
  
  return GAVL_SOURCE_EOF;
  }

static int start_overlay_stream(edldec_t * ed, int idx_rel)
  {
  int64_t src_time;
  gavl_video_format_t fmt;
  stream_t * s;
  int idx_abs;
  
  idx_abs = gavl_track_stream_idx_to_abs(ed->ti_cur, GAVL_STREAM_OVERLAY, idx_rel);

  s = &ed->streams[idx_abs];
  
  if(s->src_s->action == BG_STREAM_ACTION_OFF)
    return 1;

  if(!s->num_segs)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Got no overlay segment");
    return 0;
    }
  
  s->seg_idx = edl_dst_time_to_src(ed, s, 0, &src_time, &s->mute_time);
  
  if(!(s->vsrc_int = get_overlay_source(s, src_time)))
    return 0;
  
  /* Get format */
  s->vfmt = gavl_track_get_overlay_format_nc(ed->ti_cur, idx_rel);

  gavl_video_format_copy(s->vfmt,
                         gavl_video_source_get_src_format(s->vsrc_int));

  s->vfmt->timescale = s->timescale;
  s->vfmt->frame_duration = 0;
  s->vfmt->framerate_mode = GAVL_FRAMERATE_VARIABLE;

  gavl_video_format_copy(&fmt, s->vfmt);
  fmt.timescale = s->segs[s->seg_idx].timescale;
  
  /* Set destination format */
  gavl_video_source_set_dst(s->vsrc_int, 0, &fmt);
  
  /* Create external source */
  s->src_s->vsrc_priv = gavl_video_source_create(read_overlay, s, 0, &fmt);
  s->src_s->vsrc = s->src_s->vsrc_priv;
  
  return 1;
  }

static int start_edl(void * priv)
  {
  int i;
  int num;
  bg_media_source_stream_t * ss;
  edldec_t * ed = priv;
  
  num = gavl_track_get_num_audio_streams(ed->ti_cur);
  
  for(i = 0; i < num; i++)
    {
    if(!start_audio_stream(ed, i))
      return 0;
    }

  num = gavl_track_get_num_video_streams(ed->ti_cur);

  for(i = 0; i < num; i++)
    {
    if(!start_video_stream(ed, i))
      return 0;
    }

  num = gavl_track_get_num_text_streams(ed->ti_cur);

  for(i = 0; i < num; i++)
    {
    if(!start_text_stream(ed, i))
      return 0;
    }

  num = gavl_track_get_num_overlay_streams(ed->ti_cur);

  for(i = 0; i < num; i++)
    {
    if(!start_overlay_stream(ed, i))
      return 0;
    }

  ss = bg_media_source_get_msg_stream_by_id(&ed->src, GAVL_META_STREAM_ID_MSG_PROGRAM);
  ss->msghub_priv = bg_msg_hub_create(1);
  ss->msghub = ss->msghub_priv;
    
  //  fprintf(stderr, "Started EDL decoder\n");
  //  gavl_dictionary_dump(ed->ti_cur, 2);
  
  return 1;
  }

static void seek_edl(void * priv, int64_t * time, int scale)
  {
  int i;
  int64_t src_time;
  int time_scaled;
  stream_t * s;
  edldec_t * ed = priv;
  int num;
  
  num = gavl_track_get_num_streams_all(ed->ti_cur);
  
  for(i = 0; i < num; i++)
    {
    s = ed->streams + i;
    
    if(s->src_s->action == BG_STREAM_ACTION_OFF)
      continue;
    
    time_scaled = gavl_time_rescale(scale, s->timescale, *time);

    s->seg_idx = edl_dst_time_to_src(ed, s, time_scaled,
                                     &src_time, &s->mute_time);
    
    switch(s->src_s->type)
      {
      case GAVL_STREAM_AUDIO:
        if(s->seg_idx < s->num_segs)
          {
          if((s->asrc_int = get_audio_source(s, src_time)))
            gavl_audio_source_set_dst(s->asrc_int, 0, s->afmt);
          }
        else
          s->asrc_int = NULL;
        break;
      case GAVL_STREAM_VIDEO:
        if(s->seg_idx < s->num_segs)
          {
          if((s->vsrc_int = get_video_source(s, src_time)))
            gavl_video_source_set_dst(s->vsrc_int, 0, s->vfmt);
          }
        else
          s->vsrc_int = NULL;
        break;
      case GAVL_STREAM_TEXT:
        if(s->seg_idx < s->num_segs)
          s->psrc_int = get_text_source(s, src_time);
        else
          s->psrc_int = NULL;
        break;
      case GAVL_STREAM_OVERLAY:
        if(s->seg_idx < s->num_segs)
          {
          if((s->vsrc_int = get_overlay_source(s, src_time)))
            gavl_video_source_set_dst(s->vsrc_int, 0, s->vfmt);
          }
        else
          s->vsrc_int = NULL;
        break;
      case GAVL_STREAM_NONE:
      case GAVL_STREAM_MSG:
        break;
      }
    s->pts = time_scaled;
    }

  bg_media_source_reset(&ed->src);
  
  }

static void close_edl(void * priv)
  {

  }

static bg_controllable_t * get_controllable_edl(void * priv)
  {
  edldec_t * ed = priv;
  return &ed->controllable;
  }

static const bg_input_plugin_t edl_plugin =
  {
    .common =
    {
      BG_LOCALE,
      .name =           "i_edldec",
      .long_name =      TRS("EDL decoder"),
      .description =    TRS("This metaplugin decodes an EDL as if it was a single file."),
      .type =           BG_PLUGIN_INPUT,
      .flags =          0,
      .priority =       1,
      .create =         NULL,
      .destroy =        destroy_edl,

      .get_controllable = get_controllable_edl,
      //      .get_parameters = get_parameters_edl,
      //      .set_parameter =  set_parameter_edl
    },
    .get_media_info = get_media_info_edl,
    
    .get_src           = get_src_edl,
    
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
    .close = close_edl,
  };

bg_plugin_info_t * bg_edldec_get_info()
  {
  return bg_plugin_info_create(&edl_plugin.common);
  }

static int handle_cmd(void * data, gavl_msg_t * msg)
  {
  edldec_t * priv = data;

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

          if(!set_track_edl(priv, track))
            {
            /* TODO */
            gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Selecting track failed");
            }
          
          }
          break;
        case GAVL_CMD_SRC_START:
          if(!start_edl(data))
            {
            gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Starting EDL decoder failed");
            //            gavl_dictionary_dump(priv->ti_cur, 2);
            }
          break;
        case GAVL_CMD_SRC_SEEK:
          {
          int64_t time = gavl_msg_get_arg_long(msg, 0);
          int scale = gavl_msg_get_arg_int(msg, 1);

          /* Seek */
          seek_edl(priv, &time, scale);
          
          }
          break;
        }
      break;
    }
  return 1;
  }

bg_plugin_handle_t * bg_input_plugin_load_edl(const gavl_dictionary_t * edl)
  {
  int i;
  int max_streams = 0;
  bg_plugin_handle_t * ret;
  edldec_t * priv;
  int num_tracks;
  
  ret = calloc(1, sizeof(*ret));
    
  ret->plugin = (bg_plugin_common_t*)&edl_plugin;
  ret->info = bg_plugin_find_by_name("i_edldec");
  
  //  fprintf(stderr, "bg_input_plugin_load_edl\n");
  //  gavl_dictionary_dump(edl, 2);
  
  pthread_mutex_init(&ret->mutex, NULL);

  priv = calloc(1, sizeof(*priv));
  ret->priv = priv;
  ret->refcount = 1;

  bg_controllable_init(&priv->controllable,
                       bg_msg_sink_create(handle_cmd, priv, 1),
                       bg_msg_hub_create(1));

  bg_plugin_handle_connect_control(ret);
  
  gavl_dictionary_copy(&priv->mi, edl);

  /* Unload the plugin after we copied the EDL, since it might be owned by the plugin */

  num_tracks = gavl_get_num_tracks(&priv->mi);
  
  for(i = 0; i < num_tracks; i++)
    {
    gavl_dictionary_t * ti;
    gavl_dictionary_t * m;

    int total_streams;
    
    ti = gavl_get_track_nc(&priv->mi, i);

    /* Ensure message stream */
    gavl_track_append_msg_stream(ti, GAVL_META_STREAM_ID_MSG_PROGRAM);
    
    m = gavl_track_get_metadata_nc(ti);
    
    total_streams = 
      gavl_track_get_num_audio_streams(ti) +
      gavl_track_get_num_video_streams(ti) +
      gavl_track_get_num_text_streams(ti) +
      gavl_track_get_num_overlay_streams(ti);
    
    if(total_streams > max_streams)
      max_streams = total_streams;
    
    gavl_dictionary_set_int(m, GAVL_META_CAN_SEEK, 1);
    gavl_dictionary_set_int(m, GAVL_META_CAN_PAUSE, 1);
    }

  priv->num_sources = max_streams * 2; // We can keep twice as many files open than we have streams
  priv->sources = calloc(priv->num_sources, sizeof(*priv->sources));
  return ret;
  }

