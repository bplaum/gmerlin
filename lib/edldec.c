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
  const edl_segment_t * seg;

  edl_segment_t * segs;
  int num_segs;
  
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
  int streaming;
  
  int num_sources;
  source_t * sources;

  stream_t * streams;
  int num_streams;
  
  //  const gavl_edl_t * edl;
  //  const gavl_edl_track_t * t;

  gavl_dictionary_t mi;
  gavl_dictionary_t * ti_cur;

  bg_media_source_t src;
  
  bg_plugin_registry_t * plugin_reg;

  bg_controllable_t controllable;
  
  };

typedef struct edldec_s edldec_t;

static void streams_destroy(stream_t * s, int num);

static void free_segments(edl_segment_t * s, int len)
  {
  free(s);
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
  
  if(!bg_input_plugin_load(bg_plugin_reg,
                           s->location,
                           &s->h,
                           NULL))
    return 0;
  
  s->track = seg->track;
  s->stream = seg->stream;
  s->type = type;

  s->plugin = (bg_input_plugin_t*)(s->h->plugin);
  
  if(!bg_input_plugin_set_track(s->h, s->track))
    return 0;
  ti = bg_input_plugin_get_track_info(s->h, s->track);
  
  if(!gavl_track_can_seek(ti) && !dec->streaming)
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

  if(!gavl_dictionary_get_int(dict,  GAVL_EDL_TRACK_IDX,  &seg->track) ||
     !gavl_dictionary_get_int(dict,  GAVL_EDL_STREAM_IDX, &seg->stream) ||
     !gavl_dictionary_get_int(dict,  GAVL_META_STREAM_SAMPLE_TIMESCALE, &seg->timescale) ||
     !gavl_dictionary_get_long(dict, GAVL_EDL_SRC_TIME,   &seg->src_time) ||
     !gavl_dictionary_get_long(dict, GAVL_EDL_DST_TIME,   &seg->dst_time) ||
     !gavl_dictionary_get_long(dict, GAVL_EDL_DST_DUR,    &seg->dst_duration) ||
     !gavl_dictionary_get_int(dict,  GAVL_EDL_SPEED_NUM,  &seg->speed_num) ||
     !gavl_dictionary_get_int(dict,  GAVL_EDL_SPEED_DEN,  &seg->speed_den))
    return 0;

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
        segment_from_dictionary(&s->segs[i], dict);
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




static const edl_segment_t *
edl_dst_time_to_src(edldec_t * dec,
                    const stream_t * st,
                    int64_t dst_time,
                    int64_t * src_time,
                    int64_t * mute_time)
  {
  int i;
  const edl_segment_t * ret = NULL;

  /* Streaming case */
  if((st->num_segs == 1) && (st->segs[0].dst_duration < 0))
    {
    *src_time = 0;
    *mute_time = 0;
    return &st->segs[0];
    }
  
  for(i = 0; i < st->num_segs; i++)
    {
    if(st->segs[i].dst_time + st->segs[i].dst_duration > dst_time)
      {
      ret = &st->segs[i];
      break;
      }
    }

  if(!ret) // After the last segment
    {
    gavl_time_t duration = gavl_track_get_duration(dec->ti_cur);

    *mute_time = gavl_time_scale(st->timescale, duration) - dst_time;
    if(*mute_time < 0)
      *mute_time = 0;

    return NULL;
    }

  /* Get the next segment */

  *src_time = ret->src_time;
  
  if(ret->dst_time > dst_time)
    *mute_time = ret->dst_time - dst_time;

  if(dst_time > ret->dst_time)
    {
    *src_time += gavl_time_rescale(st->timescale,
                                   ret->timescale,
                                   dst_time - ret->dst_time);
    }
  return ret;
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

  if(!dec->streaming)
    {
    gavl_log(GAVL_LOG_DEBUG, LOG_DOMAIN, "Seeking to %"PRId64, src_time);
    bg_input_plugin_seek(ret->h, &src_time, seg->timescale);
    gavl_log(GAVL_LOG_DEBUG, LOG_DOMAIN, "Seeked to %"PRId64, src_time);
    }
  
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

static void streams_create(edldec_t * dec)
  {
  int num, i;
  
  if(!(num = gavl_track_get_num_streams_all(dec->ti_cur)))
    return;
  
  dec->streams = calloc(num, sizeof(*dec->streams));

  //  fprintf(stderr, "streams_create\n");
  //  gavl_dictionary_dump(dec->ti_cur, 2);

  bg_media_source_set_from_track(&dec->src, dec->ti_cur);
  
  dec->streaming = 1;
  
  for(i = 0; i < num; i++)
    {
    gavl_dictionary_t * d;

    if((d = gavl_track_get_stream_all_nc(dec->ti_cur, i)))
      {
      if(!stream_init(&dec->streams[i], d, dec))
        goto fail;
      dec->streams[i].src_s = dec->src.streams[i];

      if((dec->streams[i].num_segs > 1) ||
         ((dec->streams[i].num_segs == 1) && (dec->streams[i].segs[0].dst_duration > 0)))
        dec->streaming = 0;
      }
    }

  if(dec->streaming)
    {
    for(i = 0; i < num; i++)
      dec->streams[i].pts = GAVL_TIME_UNDEFINED;
    
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Switching to streaming mode");
    }
  return;
  
  fail:
  
  streams_destroy(dec->streams, num);
  }

static void streams_destroy(stream_t * s, int num)
  {
  int i;
  for(i = 0; i < num; i++)
    stream_cleanup(&s[i]);
  if(s)
    free(s);
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
    streams_destroy(ed->streams, gavl_track_get_num_streams_all(ed->ti_cur));
    bg_media_source_cleanup(&ed->src);
    bg_media_source_init(&ed->src);
    }
  ed->ti_cur = gavl_get_track_nc(&ed->mi, track);

  streams_create(ed);
  
  return 1;
  }

static void destroy_edl(void * priv)
  {
  int i;
  edldec_t * ed = priv;

  if(ed->ti_cur)
    streams_destroy(ed->streams, gavl_track_get_num_streams_all(ed->ti_cur));
  
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

  
static gavl_audio_source_t * get_audio_source(edldec_t * ed,
                                              const edl_segment_t * seg,
                                              source_t ** src,
                                              int64_t src_time)
  {
  if(*src)
    source_unref(*src);
  if(!(*src = get_source(ed, GAVL_STREAM_AUDIO, seg, src_time)))
    return NULL;
  source_ref(*src);
  return bg_media_source_get_audio_source((*src)->h->src, seg->stream);
  }

static gavl_video_source_t * get_video_source(edldec_t * ed,
                                              const edl_segment_t * seg,
                                              source_t ** src,
                                              int64_t src_time)
  {
  if(*src)
    source_unref(*src);
  if(!(*src = get_source(ed, GAVL_STREAM_VIDEO, seg, src_time)))
    return NULL;
  source_ref(*src);

  return bg_media_source_get_video_source((*src)->h->src, seg->stream);
  }

static gavl_packet_source_t * get_text_source(edldec_t * ed,
                                              const edl_segment_t * seg,
                                              source_t ** src,
                                              int64_t src_time)
  {
  if(*src)
    source_unref(*src);
  if(!(*src = get_source(ed, GAVL_STREAM_TEXT, seg, src_time)))
    return NULL;
  source_ref(*src);

  return bg_media_source_get_text_source((*src)->h->src, seg->stream);
  }

static gavl_video_source_t * get_overlay_source(edldec_t * ed,
                                                const edl_segment_t * seg,
                                                source_t ** src,
                                                int64_t src_time)
  {
  if(*src)
    source_unref(*src);

  if(!(*src = get_source(ed, GAVL_STREAM_OVERLAY, seg, src_time)))
    return NULL;
  source_ref(*src);
  return bg_media_source_get_overlay_source((*src)->h->src, seg->stream);
  }

static gavl_source_status_t read_audio(void * priv,
                                       gavl_audio_frame_t ** frame)
  {
  gavl_source_status_t st;
  stream_t * s = priv;

  /* Early return */
  if(!s->seg && !s->mute_time)
    return GAVL_SOURCE_EOF;
  
  /* Check for segment end */
  if((s->pts != GAVL_TIME_UNDEFINED) && s->seg && (s->pts >= s->seg->dst_time + s->seg->dst_duration))
    {
    int64_t src_time;

    s->seg = edl_dst_time_to_src(s->dec, s, s->pts,
                                 &src_time, &s->mute_time);

    if(!s->seg && !s->mute_time)
      return GAVL_SOURCE_EOF;

    if(s->seg)
      {
      if(!(s->asrc_int = get_audio_source(s->dec, s->seg, &s->src, src_time)))
        return GAVL_SOURCE_EOF;
      gavl_audio_source_set_dst(s->asrc_int, 0, s->afmt);
      }
    else
      s->asrc_int = NULL;
    }
  
  /* Check for mute */
  if(s->mute_time)
    {
    if(s->pts == GAVL_TIME_UNDEFINED)
      s->pts = 0;
    gavl_audio_frame_mute(*frame, s->afmt);
    if((*frame)->valid_samples > s->mute_time)
      (*frame)->valid_samples = s->mute_time;
    s->mute_time -= (*frame)->valid_samples;
    (*frame)->timestamp = s->pts;
    s->pts += (*frame)->valid_samples;
    return GAVL_SOURCE_OK;
    }
  
  /* Read audio */
  st = gavl_audio_source_read_frame(s->asrc_int, frame);
  if(st != GAVL_SOURCE_OK)
    {
    gavl_audio_frame_mute(*frame, s->afmt);
    (*frame)->valid_samples =
      s->seg->dst_time + s->seg->dst_duration - s->pts;
    if((*frame)->valid_samples > s->afmt->samples_per_frame)
      (*frame)->valid_samples = s->afmt->samples_per_frame;
    }
  else
    {
    if(s->pts == GAVL_TIME_UNDEFINED)
      s->pts = (*frame)->timestamp;
    
    /* Limit duration */ 
    if(s->seg->dst_duration > 0 && (s->pts + (*frame)->valid_samples >
                                    s->seg->dst_time + s->seg->dst_duration))
      {
      (*frame)->valid_samples =
        s->seg->dst_time + s->seg->dst_duration - s->pts;
      }
    }
  
  /* Set PTS */
  (*frame)->timestamp = s->pts;

  //  fprintf(stderr, "Read audio %"PRId64" %d\n",
  //          s->pts, (*frame)->valid_samples);
  
  s->pts += (*frame)->valid_samples;
  return GAVL_SOURCE_OK;
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
  
  if(s->src_s->action == BG_STREAM_ACTION_OFF)
    return 1;
  if(!(s->seg = edl_dst_time_to_src(ed, s, 0,
                                    &src_time, &s->mute_time)))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Got no audio segment");
    return 0;
    }
  if(!(s->asrc_int = get_audio_source(ed, s->seg, &s->src, src_time)))
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

  if(!ed->streaming)
    {
    s->afmt->samples_per_frame = 1024;
  
    /* Set destination format */
    gavl_audio_source_set_dst(s->asrc_int, 0, s->afmt);

    /* Create external source */
    s->src_s->asrc_priv =
      gavl_audio_source_create(read_audio, s, GAVL_SOURCE_SRC_FRAMESIZE_MAX, s->afmt);
  
    s->src_s->asrc = s->src_s->asrc_priv;
    }
  else
    {
    s->src_s->asrc = s->asrc_int;
    }
  
  return 1;
  }

static gavl_source_status_t read_video(void * priv,
                                       gavl_video_frame_t ** frame)
  {
  gavl_source_status_t st;
  stream_t * s = priv;

  /* Early return */
  if(!s->seg && !s->mute_time)
    return GAVL_SOURCE_EOF;

  // fprintf(stderr, "read_video %"PRId64" %"PRId64"\n", s->pts, s->seg->dst_time + s->seg->dst_duration);
  
  /* Check for segment end */
  if((s->pts != GAVL_TIME_UNDEFINED) && s->seg && (s->pts >= s->seg->dst_time + s->seg->dst_duration))
    {
    int64_t src_time;

    s->seg = edl_dst_time_to_src(s->dec, s, s->pts,
                                 &src_time, &s->mute_time);

    if(!s->seg && !s->mute_time)
      return GAVL_SOURCE_EOF;
    
    if(s->seg)
      {
      if(!(s->vsrc_int = get_video_source(s->dec, s->seg, &s->src, src_time)))
        return GAVL_SOURCE_EOF;
      gavl_video_source_set_dst(s->vsrc_int, 0, s->vfmt);
      }
    else
      s->vsrc_int = NULL;
    }

  /* Check for mute */

  if(s->mute_time > 0)
    {
    if(s->pts == GAVL_TIME_UNDEFINED)
      s->pts = 0;
    
    gavl_video_frame_clear(*frame, s->vfmt);
    (*frame)->timestamp = s->pts;
    (*frame)->duration = s->mute_time;

    /* TODO: Limit frame duration */
    (*frame)->timestamp = s->pts;
    s->pts += (*frame)->duration;
    s->mute_time -= (*frame)->duration;
    return GAVL_SOURCE_OK;
    }
  
  /* Read video */
  //  fprintf(stderr, "Read video...");
  st = gavl_video_source_read_frame(s->vsrc_int, frame);
  //  fprintf(stderr, "Read video done");
  if(st != GAVL_SOURCE_OK)
    {
    /* Can happen due to some inaccuracies */
    gavl_video_frame_clear(*frame, s->vfmt);
    (*frame)->duration =
      s->seg->dst_time + s->seg->dst_duration - s->pts;
    }
  else
    {
    if(s->pts == GAVL_TIME_UNDEFINED)
      s->pts = (*frame)->timestamp;
    
    /* Limit duration */ 
    if((s->seg->dst_duration > 0) &&
       ((s->pts + (*frame)->duration) >
        s->seg->dst_time + s->seg->dst_duration))
      {
      (*frame)->duration =
        s->seg->dst_time + s->seg->dst_duration - s->pts;
      }
    }
  
  /* Set PTS */
  (*frame)->timestamp = s->pts;
  s->pts += (*frame)->duration;

  fprintf(stderr, "read_video frame %"PRId64" %"PRId64"\n", (*frame)->timestamp, (*frame)->duration);
  
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

  if(!(s->seg = edl_dst_time_to_src(ed, s, 0,
                                         &src_time, &s->mute_time)))
    return 0;
  
  if(!(s->vsrc_int = get_video_source(ed, s->seg, &s->src, src_time)))
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

  if(!ed->streaming)
    {
    s->vfmt->frame_duration = 0;
    s->vfmt->framerate_mode = GAVL_FRAMERATE_VARIABLE;
  
    /* Set destination format */
    gavl_video_source_set_dst(s->vsrc_int, 0, s->vfmt);

    /* Create external source */
    s->src_s->vsrc_priv = gavl_video_source_create(read_video, s, 0, s->vfmt);
    s->src_s->vsrc = s->src_s->vsrc_priv;
    }
  else
    s->src_s->vsrc = s->vsrc_int;
  return 1;
  }

static gavl_source_status_t read_text(void * priv,
                                      gavl_packet_t ** p)
  {
  int64_t src_time;
  gavl_source_status_t st;
  stream_t * s = priv;

  /* Early return */
  if(!s->seg)
    return GAVL_SOURCE_EOF;

  while(1)
    {
    /* Read text */
    st = gavl_packet_source_read_packet(s->psrc_int, p);
    
    if(st == GAVL_SOURCE_OK)
      {
      (*p)->pts = edl_src_time_to_dst(s, s->seg, (*p)->pts);
      (*p)->duration = gavl_time_rescale(s->seg->timescale, s->timescale, (*p)->duration);
      
      /* Check for segment end */
      if((*p)->pts < s->seg->dst_time + s->seg->dst_duration)
        {
        /* Adjust duration to the segment end */
        if((*p)->pts + (*p)->duration > s->seg->dst_time + s->seg->dst_duration)
          (*p)->duration = s->seg->dst_time + s->seg->dst_duration - (*p)->pts;
        return GAVL_SOURCE_OK;
        }
      }
    
    /* Get next segment */
    
    s->seg = edl_dst_time_to_src(s->dec, s, s->seg->dst_time + s->seg->dst_duration,
                                 &src_time, &s->mute_time);
    
    if(!s->seg)
      break;
    
    if(!(s->psrc_int = get_text_source(s->dec, s->seg, &s->src, src_time)))
      return GAVL_SOURCE_EOF;
    }
  
  return GAVL_SOURCE_EOF;
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
  s->seg = edl_dst_time_to_src(ed, s, 0, &src_time, &s->mute_time);
  if(!s->seg)
    return 0;
  
  if(!(s->psrc_int = get_text_source(ed, s->seg, &s->src, src_time)))
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
  int64_t src_time;
  gavl_source_status_t st;
  gavl_video_format_t fmt;
  stream_t * s = priv;

  /* Early return */
  if(!s->seg)
    return GAVL_SOURCE_EOF;

  while(1)
    {
    /* Read text */
    st = gavl_video_source_read_frame(s->vsrc_int, f);
    
    if(st == GAVL_SOURCE_OK)
      {
      (*f)->timestamp = edl_src_time_to_dst(s, s->seg, (*f)->timestamp);
      (*f)->duration = gavl_time_rescale(s->seg->timescale, s->timescale, (*f)->duration);
      
      /* Check for segment end */
      if((*f)->timestamp < s->seg->dst_time + s->seg->dst_duration)
        {
        /* Adjust duration to the segment end */
        if((*f)->timestamp + (*f)->duration > s->seg->dst_time + s->seg->dst_duration)
          (*f)->duration = s->seg->dst_time + s->seg->dst_duration - (*f)->timestamp;
        return GAVL_SOURCE_OK;
        }
      }
    
    /* Get next segment */
    
    s->seg = edl_dst_time_to_src(s->dec, s, s->seg->dst_time + s->seg->dst_duration,
                                 &src_time, &s->mute_time);
    
    if(!s->seg)
      break;
    
    if(!(s->vsrc_int = get_overlay_source(s->dec, s->seg, &s->src, src_time)))
      return GAVL_SOURCE_EOF;
    
    gavl_video_format_copy(&fmt, s->vfmt);
    fmt.timescale = s->seg->timescale;

    /* Set destination format */
    gavl_video_source_set_dst(s->vsrc_int, 0, &fmt);
    }
  
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
  s->seg = edl_dst_time_to_src(ed, s, 0, &src_time, &s->mute_time);
  if(!s->seg)
    return 0;
  
  if(!(s->vsrc_int = get_overlay_source(ed, s->seg, &s->src, src_time)))
    return 0;

  /* Get format */
  s->vfmt = gavl_track_get_overlay_format_nc(ed->ti_cur, idx_rel);

  gavl_video_format_copy(s->vfmt,
                         gavl_video_source_get_src_format(s->vsrc_int));

  s->vfmt->timescale = s->timescale;
  s->vfmt->frame_duration = 0;
  s->vfmt->framerate_mode = GAVL_FRAMERATE_VARIABLE;

  gavl_video_format_copy(&fmt, s->vfmt);
  fmt.timescale = s->seg->timescale;

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
  
  if(ed->streaming)
    {
    /* Copy stream stats */
    num = gavl_track_get_num_streams_all(ed->ti_cur);

    for(i = 0; i < num; i++)
      {
      /* TODO */
      gavl_dictionary_t * dst_dict;
      const gavl_dictionary_t * src_dict;

      if(!ed->streams[i].src)
        continue;

      src_dict = ed->streams[i].src->st;
      dst_dict = ed->streams[i].s;

      gavl_dictionary_merge2(dst_dict, src_dict);
      
      //      fprintf(stderr, "Copy stats\n");
      //      gavl_dictionary_dump(src_dict, 2);

      gavl_dictionary_set(dst_dict,
                          GAVL_META_STREAM_STATS,
                          gavl_dictionary_get(src_dict, GAVL_META_STREAM_STATS));

      src_dict = gavl_track_get_metadata(src_dict);
      dst_dict = gavl_track_get_metadata_nc(dst_dict);
      
      gavl_dictionary_merge2(dst_dict, src_dict);
      
      }
    }
  
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

    s->seg = edl_dst_time_to_src(ed, s, time_scaled,
                                 &src_time, &s->mute_time);
    

    switch(s->src_s->type)
      {
      case GAVL_STREAM_AUDIO:
        if(s->seg)
          {
          if((s->asrc_int = get_audio_source(s->dec, s->seg, &s->src, src_time)))
            gavl_audio_source_set_dst(s->asrc_int, 0, s->afmt);
          }
        else
          s->asrc_int = NULL;
        break;
      case GAVL_STREAM_VIDEO:
        if(s->seg)
          {
          if((s->vsrc_int = get_video_source(s->dec, s->seg, &s->src, src_time)))
            gavl_video_source_set_dst(s->vsrc_int, 0, s->vfmt);
          }
        else
          s->vsrc_int = NULL;
        break;
      case GAVL_STREAM_TEXT:
        if(s->seg)
          s->psrc_int = get_text_source(s->dec, s->seg, &s->src, src_time);
        else
          s->psrc_int = NULL;
        break;
      case GAVL_STREAM_OVERLAY:
        if(s->seg)
          {
          if((s->vsrc_int = get_overlay_source(s->dec, s->seg, &s->src, src_time)))
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
            }
          
          }
          break;
        case GAVL_CMD_SRC_START:
          if(!start_edl(data))
            gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Starting EDL decoder failed");
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

int bg_input_plugin_load_edl(bg_plugin_registry_t * reg,
                             const gavl_dictionary_t * edl,
                             bg_plugin_handle_t ** ret1,
                             bg_control_t * ctrl)
  {
  int i;
  int max_streams = 0;
  bg_plugin_handle_t * ret;
  edldec_t * priv;
  int num_tracks;
  
  ret = calloc(1, sizeof(*ret));
    
  ret->plugin = (bg_plugin_common_t*)&edl_plugin;
  ret->info = bg_plugin_find_by_name(reg, "i_edldec");
  
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
  if(*ret1)
    bg_plugin_unref(*ret1);
  *ret1 = ret;

  
  priv->plugin_reg = reg;

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
  return 1;
  }

