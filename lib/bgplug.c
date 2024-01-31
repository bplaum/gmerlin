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

#include <config.h>

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <gavl/metatags.h>

#include <gavl/gavf.h>
#include <gavl/gavlsocket.h>

#include <gmerlin/plugin.h>
#include <gmerlin/bggavl.h>

#include <gmerlin/bgplug.h>
#include <gmerlin/utils.h>
#include <gmerlin/http.h>

#include <gmerlin/translation.h>
#include <gmerlin/log.h>

#define LOG_DOMAIN "plug"


#define PLUG_FLAG_HAS_MSG_READ                (1<<1)
#define PLUG_FLAG_HAS_MSG_WRITE               (1<<2)
// #define PLUG_FLAG_HAS_MSG_READ_THREAD         (1<<3)
// #define PLUG_FLAG_MSG_READ_THREAD_RUNNING     (1<<4)
// #define PLUG_FLAG_MSG_READ_THREAD_STOP        (1<<5)
// #define PLUG_FLAG_GOT_RESYNC                  (1<<8)
#define PLUG_FLAG_SEEKING                     (1<<9)

// #define DUMP_MESSAGES



typedef struct
  {
  /* Is set before the header exists */
  gavl_stream_type_t type;
  int id;
    
  /* Reading */
  //  gavl_packet_source_t * src_int;
  
  /* Writing */
  gavl_packet_sink_t * sink_int;
  
  bg_plug_t * plug;

  gavl_packet_t * p_ext;
  
  /* Audio stuff */
  gavl_audio_format_t * afmt;
  
  /* Video stuff */
  gavl_video_format_t * vfmt;
  gavl_compression_info_t ci;

  gavl_dictionary_t * m;
  int timescale;
  
  bg_plugin_handle_t * codec_handle;
  
  gavl_dsp_context_t * dsp;

  bg_media_source_stream_t * source_s;
  bg_media_sink_stream_t * sink_s;

  gavl_dictionary_t * h;
  
  bg_msg_sink_t * sink;
  
  } stream_t;

typedef struct
  {
  bg_plugin_handle_t * codec_handle;

  /* Audio stuff */
  gavl_audio_format_t * afmt;
  
  /* Video stuff */
  gavl_video_format_t * vfmt;
  gavl_compression_info_t ci;
  } stream_priv_t;

static void free_stream_priv(void * data)
  {
  stream_priv_t * p = data;
  if(p->codec_handle)
    bg_plugin_unref(p->codec_handle);

  gavl_compression_info_free(&p->ci);
  
  }

static stream_priv_t * create_stream_priv(gavl_dictionary_t * s, gavl_stream_type_t type)
  {
  stream_priv_t * ret = calloc(1, sizeof(*ret));

  gavl_stream_get_compression_info(s, &ret->ci);
  
  switch(type)
    {
    case GAVL_STREAM_AUDIO:
      ret->afmt = gavl_stream_get_audio_format_nc(s);
      break;
    case GAVL_STREAM_VIDEO:
      ret->vfmt = gavl_stream_get_video_format_nc(s);
      break;
    case GAVL_STREAM_OVERLAY:
      ret->vfmt = gavl_stream_get_video_format_nc(s);
      break;
    case GAVL_STREAM_TEXT:
    case GAVL_STREAM_NONE:
    case GAVL_STREAM_MSG:
      break;
    }
  return ret;
  }
  
struct bg_plug_s
  {
  int wr;
  gavf_t * g;

  //  gavl_io_t * io_orig;
  //  gavl_io_t * io;
  
  int flags;
  pthread_mutex_t flags_mutex;
  
  stream_t * streams;
  
  int io_flags;  
  
  gavf_options_t * opt;


  pthread_mutex_t mutex;
  
  gavl_packet_t skip_packet;
  
  int got_error;
  int timeout;
  int nomsg;
  
  bg_controllable_t controllable;
  //  bg_control_t control;
  
  gavl_dictionary_t * mi_priv;
  gavl_dictionary_t * mi;
  gavl_dictionary_t * cur;

  bg_media_source_t src;
  bg_media_sink_t sink;

  int num_streams;
  
  };


static int handle_cmd_reader(void * data, gavl_msg_t * msg);
// static int msg_sink_func_write(void * data, gavl_msg_t * msg);

#if 0
static int plug_was_locked(bg_plug_t * p, int keep_lock)
  {
  if(!pthread_mutex_trylock(&p->mutex))
    {
    if(!keep_lock)
      pthread_mutex_unlock(&p->mutex);
    return 0;
    }
  else
    return 1;
  }

static void set_flag(bg_plug_t * p, int flag)
  {
  pthread_mutex_lock(&p->flags_mutex);
  p->flags |= flag;
  pthread_mutex_unlock(&p->flags_mutex);
  }


static int has_flag(bg_plug_t * p, int flag)
  {
  int ret;
  pthread_mutex_lock(&p->flags_mutex);
  
  ret = !!(p->flags & flag);
  
  pthread_mutex_unlock(&p->flags_mutex);
  return ret;
  }

static int has_flag_all(bg_plug_t * p, int flag)
  {
  int ret;
  pthread_mutex_lock(&p->flags_mutex);
  
  if((p->flags & flag) == flag)
    ret = 1;
  else
    ret = 0;
  
  pthread_mutex_unlock(&p->flags_mutex);
  return ret;
  }
#endif


/* Backchannel functions */

/* Handle backchannel messages for writing plugs */

static int handle_msg_backchannel_wr(void * data, gavl_msg_t * msg)
  {
  //  bg_plug_t * plug = data;

  //  fprintf(stderr, "handle_msg_backchannel_wr\n");
  //  gavl_msg_dump(msg, 2);
  
  switch(msg->NS)
    {
    case GAVL_MSG_NS_GAVF:
      switch(msg->ID)
        {
        }
      break;
    case GAVL_MSG_NS_GENERIC:
      switch(msg->ID)
        {
        case GAVL_CMD_QUIT:
          return 0;
        }
      break;
    case GAVL_MSG_NS_SRC:
      switch(msg->ID)
        {
        case GAVL_CMD_SRC_SELECT_TRACK:
          {
          
          }
          break;
        case GAVL_CMD_SRC_SEEK:
          {
          
          }
          break;
        case GAVL_CMD_SRC_PAUSE:
        case GAVL_CMD_SRC_RESUME:
          {
          
          }
          break;
        case GAVL_CMD_SRC_START:
          {
          
          }
          break;
          
        }
      break;
    }
  return 1;
  }

int bg_plug_ping(bg_plug_t * plug)
  {
  if(plug->wr)
    {
    gavf_read_writer_command(plug->g, 0);
    }
  return 1;
  }

#if 0
static void clear_flag(bg_plug_t * p, int flag)
  {
  pthread_mutex_lock(&p->flags_mutex);
  p->flags &= ~flag;
  pthread_mutex_unlock(&p->flags_mutex);
  }
#endif

// static void init_streams_read(bg_plug_t * p);
static void cleanup_streams(bg_plug_t * p);

gavl_dictionary_t * bg_plug_get_media_info(bg_plug_t * p)
  {
  return p->mi;
  }

const gavl_dictionary_t * bg_plug_get_metadata(bg_plug_t * p)
  {
  return gavl_track_get_metadata(p->cur);
  }

gavl_dictionary_t * bg_plug_get_current_track(bg_plug_t * p)
  {
  return p->cur;
  }


/* Message callbacks for the gavf instances */



static bg_plug_t * create_common()
  {
  bg_plug_t * ret = calloc(1, sizeof(*ret));

  ret->opt = gavf_options_create();
  ret->timeout = 10000;
  ret->g = gavf_create();

  
  pthread_mutex_init(&ret->mutex, NULL);
  pthread_mutex_init(&ret->flags_mutex, NULL);

  return ret;
  }



bg_plug_t * bg_plug_create_reader()
  {
  bg_plug_t * ret;
  ret = create_common();

  bg_controllable_init(&ret->controllable,
                       bg_msg_sink_create(handle_cmd_reader, ret, 0),
                       bg_msg_hub_create(1));

  
  return ret;
  }

bg_plug_t * bg_plug_create_writer()
  {
  bg_plug_t * ret = create_common();
  ret->wr = 1;
  gavf_set_msg_cb(ret->g, handle_msg_backchannel_wr, ret);
  
  //  bg_control_init(&ret->control,
  //                  bg_msg_sink_create(msg_sink_func_write, ret, 1));
  
  return ret;
  }

static void flush_streams(stream_t * streams, int num)
  {
  int i;
  stream_t * s;
  for(i = 0; i < num; i++)
    {
    s = streams + i;

    /* Flush the codecs, might write some final packets */
    if(s->codec_handle)
      bg_plugin_unref(s->codec_handle);
    if(s->dsp)
      gavl_dsp_context_destroy(s->dsp);
    }
  }

static void cleanup_streams(bg_plug_t * p)
  {
#if 0
  int i;
  stream_t * s;

  
  for(i = 0; i < p->num_streams; i++)
    {
    s = p->streams + i;
    
    if(s->aframe)
      {
      gavl_audio_frame_null(s->aframe);
      gavl_audio_frame_destroy(s->aframe);
      }
    if(s->vframe)
      {
      gavl_video_frame_null(s->vframe);
      gavl_video_frame_destroy(s->vframe);
      }
    gavl_compression_info_free(&s->ci);
    }
  if(p->streams)
    free(p->streams);

  p->streams = NULL;
  p->num_streams = 0;
#endif
  
  bg_media_source_cleanup(&p->src);
  bg_media_source_init(&p->src);

  bg_media_sink_cleanup(&p->sink);
  bg_media_sink_init(&p->sink);
  
  }

void bg_plug_destroy(bg_plug_t * p)
  {
  flush_streams(p->streams, p->num_streams);

  gavf_options_destroy(p->opt);

  if(p->g)
    gavf_close(p->g, 0);
  
  cleanup_streams(p);
  
  gavl_packet_free(&p->skip_packet);
  
  pthread_mutex_destroy(&p->mutex);
  pthread_mutex_destroy(&p->flags_mutex);

  if(p->mi_priv)
    gavl_dictionary_destroy(p->mi_priv);
  
  free(p);
  }

static const bg_parameter_info_t input_parameters[] =
  {
    {
      .name = "dh",
      .long_name = TRS("Dump gavf headers"),
      .type = BG_PARAMETER_CHECKBUTTON,
      .help_string = TRS("Use this for debugging"),
    },
    {
      .name = "dp",
      .long_name = TRS("Dump gavf packets"),
      .type = BG_PARAMETER_CHECKBUTTON,
      .help_string = TRS("Use this for debugging"),
    },
    {
      .name = "dm",
      .long_name = TRS("Dump inline metadata"),
      .type = BG_PARAMETER_CHECKBUTTON,
      .help_string = TRS("Use this for debugging"),
    },
    {
      .name = "to",
      .type = BG_PARAMETER_INT,
      .val_default = GAVL_VALUE_INIT_INT(5000),
      .val_min =     GAVL_VALUE_INIT_INT(500),
      .val_max =     GAVL_VALUE_INIT_INT(100000),
      .long_name = TRS("Timeout (milliseconds)"),
    },
    {
      .name = "nomsg",
      .long_name = TRS("No messages"),
      .type = BG_PARAMETER_CHECKBUTTON,
      .help_string = TRS("Don't attempt to make a back-channel for messages. Forward messages might still be sent."),
    },
    { /* End */ },
  };

static const bg_parameter_info_t output_parameters[] =
  {
    {
      .name = "to",
      .type = BG_PARAMETER_INT,
      .val_default = GAVL_VALUE_INIT_INT(5000),
      .val_min =     GAVL_VALUE_INIT_INT(500),
      .val_max =     GAVL_VALUE_INIT_INT(100000),
      .long_name = TRS("Timeout (milliseconds)"),
    },
    {
      .name = "dp",
      .long_name = TRS("Dump gavf packets"),
      .type = BG_PARAMETER_CHECKBUTTON,
      .help_string = TRS("Use this for debugging"),
    },
    { /* End */ }
  };

const bg_parameter_info_t *
bg_plug_get_input_parameters()
  {
  return input_parameters;
  }

const bg_parameter_info_t *
bg_plug_get_output_parameters()
  {
  return output_parameters;
  }

#define SET_GAVF_FLAG(f) \
  int flags = gavf_options_get_flags(p->opt); \
  if(val->v.i) \
    flags |= f; \
  else \
    flags &= ~f; \
  gavf_options_set_flags(p->opt, flags);

void bg_plug_set_parameter(void * data, const char * name,
                           const gavl_value_t * val)
  {
  bg_plug_t * p = data;
  
  if(!name)
    return;
  else if(!strcmp(name, "dh"))
    {
    SET_GAVF_FLAG(GAVF_OPT_FLAG_DUMP_HEADERS);
    }
  else if(!strcmp(name, "dp"))
    {
    SET_GAVF_FLAG(GAVF_OPT_FLAG_DUMP_PACKETS);
    }
  else if(!strcmp(name, "dm"))
    {
    SET_GAVF_FLAG(GAVF_OPT_FLAG_DUMP_METADATA);
    }
  else if(!strcmp(name, "nomsg"))
    p->nomsg = val->v.i;
  else if(!strcmp(name, "timeout"))
    p->timeout = val->v.i;
  }

#undef SET_GAVF_FLAG

/* Read/write */
#if 0
static void init_streams_read(bg_plug_t * p)
  {
  int i;
  stream_t * s;

  const gavl_dictionary_t * dict;
  
  p->cur = gavf_get_current_track_nc(p->g);
  p->src.track = p->cur;
  p->num_streams = gavl_track_get_num_streams_all(p->cur);
  
  p->streams = calloc((unsigned int)p->num_streams, sizeof(*p->streams));
  
  p->src.streams = calloc((unsigned int)p->num_streams, sizeof(*p->src.streams));
  p->src.num_streams = p->num_streams;
  
  /* Initialize streams for a reader, the action can be changed
     later on */
  
  for(i = 0; i < p->num_streams; i++)
    {
    p->src.streams[i] = calloc(1, sizeof(*p->src.streams[i]));
    
    s = p->streams + i;
    s->source_s = p->src.streams[i];
    
    s->h = gavl_track_get_stream_all_nc(p->cur, i);
    s->m = gavl_stream_get_metadata_nc(s->h);
    
    gavl_stream_get_id(s->h, &s->id);
    s->source_s->stream_id = s->id;
    
    gavl_stream_get_compression_info(s->h, &s->ci);
    
    s->type = gavl_stream_get_type(s->h);
    s->plug = p;

    s->source_s->type = s->type;
    s->source_s->s    = s->h;
    
    switch(s->type)
      {
      case GAVL_STREAM_AUDIO:
        {
        s->afmt = gavl_stream_get_audio_format_nc(s->h);
        }
        break;
      case GAVL_STREAM_VIDEO:
        {
        s->vfmt = gavl_stream_get_video_format_nc(s->h);
        }
        break;
      case GAVL_STREAM_TEXT:
        {
        if(s->ci.id == GAVL_CODEC_ID_NONE)
          s->source_s->action = BG_STREAM_ACTION_DECODE;
        else
          s->source_s->action = BG_STREAM_ACTION_READRAW;
        }
      case GAVL_STREAM_OVERLAY:
        {
        s->vfmt = gavl_stream_get_video_format_nc(s->h);
        if(s->ci.id == GAVL_CODEC_ID_NONE)
          s->source_s->action = BG_STREAM_ACTION_DECODE;
        else
          s->source_s->action = BG_STREAM_ACTION_READRAW;
        }
        break;
      case GAVL_STREAM_MSG:
        s->source_s->action = BG_STREAM_ACTION_DECODE;
        break;
      case GAVL_STREAM_NONE:
        break;
      }
    }
  }
#endif
/* Read support */

static int init_read_common(bg_plug_t * p,
                            stream_t * s)
  {
  int id;
  // fprintf(stderr, "init_read_common:\n");
  // gavl_dictionary_dump(s->h, 2);
  
  gavl_stream_get_id(s->h, &id);
  
  if(s->source_s->action == BG_STREAM_ACTION_OFF)
    {
    gavf_stream_set_skip(p->g, id);
    return 0;
    }
  
  s->source_s->psrc = gavf_get_packet_source(p->g, id);

  return 1;
  }

#if 0
/* Uncompressed source funcs */
static gavl_source_status_t read_audio_func(void * priv,
                                            gavl_audio_frame_t ** f)
  {
  gavl_source_status_t st;
  gavl_packet_t * p = NULL;
  stream_t * s = priv;

  if((st = gavl_packet_source_read_packet(s->source_s->psrc, &p))
     != GAVL_SOURCE_OK)
    return st;

  gavf_packet_to_audio_frame(p, s->aframe,
                             s->afmt,
                             s->m, &s->dsp);
  *f = s->aframe;
  return GAVL_SOURCE_OK;
  }

static gavl_source_status_t read_video_func(void * priv,
                                            gavl_video_frame_t ** f)
  {
  gavl_source_status_t st;
  gavl_packet_t * p = NULL;
  stream_t * s = priv;
  // fprintf(stderr, "Read video func\n");
  if((st = gavl_packet_source_read_packet(s->source_s->psrc, &p))
     != GAVL_SOURCE_OK)
    return st;
  
  gavf_packet_to_video_frame(p, s->vframe,
                             s->vfmt,
                             s->m, &s->dsp);
  *f = s->vframe;

  // fprintf(stderr, "Read video func done\n");
  
  return GAVL_SOURCE_OK;
  }

static gavl_source_status_t read_overlay_func(void * priv,
                                              gavl_video_frame_t ** f)
  {
  gavl_source_status_t st;
  gavl_packet_t * p = NULL;
  stream_t * s = priv;
  // fprintf(stderr, "Read video func\n");
  if((st = gavl_packet_source_read_packet(s->source_s->psrc, &p))
     != GAVL_SOURCE_OK)
    return st;
  gavf_packet_to_overlay(p, *f, s->vfmt);
  // fprintf(stderr, "Read video func done\n");
  return GAVL_SOURCE_OK;
  }
#endif

static const struct
  {
  bg_plugin_type_t type;
  const char * name;
  }
codec_types[] =
  {
    { BG_PLUGIN_DECOMPRESSOR_AUDIO, "audio" },
    { BG_PLUGIN_DECOMPRESSOR_VIDEO, "video" },
    { }
  };

static const char * codec_type_name(int type)
  {
  int i = 0;
  while(codec_types[i].name)
    {
    if(type == codec_types[i].type)
      return codec_types[i].name;
    i++;
    }
  return NULL;
  }

static bg_plugin_handle_t * load_decompressor(gavl_codec_id_t id,
                                              int type_mask)
  {
  /* Add decoder */
  const bg_plugin_info_t * info;
  bg_plugin_handle_t * ret;
  info = bg_plugin_find_by_compression(id, 0, type_mask);
  if(!info)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN,
           "Cannot find %s decompressor for %s",
           codec_type_name(type_mask),
           gavl_compression_get_short_name(id));
    return NULL;
    }
  ret = bg_plugin_load(info);
  if(!ret)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN,
           "Cannot load %s decompressor for %s",
           codec_type_name(type_mask),
           gavl_compression_get_short_name(id));
    return NULL;
    }
  return ret;
  }

static int init_read(bg_plug_t * p)
  {
  int i; 
  stream_t * s;
  
  for(i = 0; i < p->num_streams; i++)
    {
    s = p->streams + i;
    
    if(!init_read_common(p, s))
      continue; // Stream is switched off

    switch(s->type)
      {
      case GAVL_STREAM_NONE:
        break;
      case GAVL_STREAM_AUDIO:
        
        if(s->ci.id == GAVL_CODEC_ID_NONE)
          {
          int id;
          gavl_stream_get_id(s->h, &id);
          
          if(s->source_s->action == BG_STREAM_ACTION_DECODE)
            {
            s->source_s->asrc = gavf_get_audio_source(p->g, id);
            s->source_s->psrc = NULL;
            }
          }
        else if(s->source_s->action == BG_STREAM_ACTION_DECODE)
          {
          //          bg_codec_plugin_t * codec;
          /* Add decoder */
          if(!(s->codec_handle = load_decompressor(s->ci.id,
                                                   BG_PLUGIN_DECOMPRESSOR_AUDIO)))
            return 0;

          //          codec = (bg_codec_plugin_t*)s->codec_handle->plugin;
#if 0 // TODO
          s->source_s->asrc = codec->connect_decode_audio(s->codec_handle->priv,
                                                          s->source_s->psrc,
                                                          &s->ci,
                                                          s->afmt,
                                                          s->m);
#endif
          if(!s->source_s->asrc)
            {
            gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Initializing audio decoder failed");
            return 0;
            }
          }

        break;
      case GAVL_STREAM_VIDEO:

        if(s->ci.id == GAVL_CODEC_ID_NONE)
          {
          int id;
          gavl_stream_get_id(s->h, &id);

          if(s->source_s->action == BG_STREAM_ACTION_DECODE)
            {
            s->source_s->vsrc = gavf_get_video_source(p->g, id);
            s->source_s->psrc = NULL;
            }
          }
        else if(s->source_s->action == BG_STREAM_ACTION_DECODE)
          {
          //          bg_codec_plugin_t * codec;
          /* Add decoder */
          if(!(s->codec_handle = load_decompressor(s->ci.id,
                                                   BG_PLUGIN_DECOMPRESSOR_VIDEO)))
            return 0;
          //          codec = (bg_codec_plugin_t*)s->codec_handle->plugin;
#if 0 // TODO
          s->source_s->vsrc = codec->connect_decode_video(s->codec_handle->priv,
                                                          s->source_s->psrc,
                                                          &s->ci,
                                                          s->vfmt,
                                                          s->m);
#endif          
          if(!s->source_s->vsrc)
            {
            gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Initializing video decoder failed");
            return 0;
            }

          }

        break;
      case GAVL_STREAM_TEXT:
        break;
      case GAVL_STREAM_OVERLAY:
        if(s->ci.id == GAVL_CODEC_ID_NONE)
          {
          int id;
          gavl_stream_get_id(s->h, &id);

          if(s->source_s->action == BG_STREAM_ACTION_DECODE)
            {
            s->source_s->vsrc = gavf_get_video_source(p->g, id);
            s->source_s->psrc = NULL;
            }
          }
        else if(s->source_s->action == BG_STREAM_ACTION_DECODE)
          {
#if 0 // TODO
          bg_codec_plugin_t * codec;
          /* Add decoder */
          if(!(s->codec_handle = load_decompressor(s->ci.id,
                                                   BG_PLUGIN_DECOMPRESSOR_VIDEO)))
            return 0;
          codec = (bg_codec_plugin_t*)s->codec_handle->plugin;
      
          s->source_s->vsrc = codec->connect_decode_overlay(s->codec_handle->priv,
                                                            s->source_s->psrc,
                                                            &s->ci,
                                                            s->vfmt,
                                                            s->m);
          if(!s->source_s->vsrc)
            {
            gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Initializing overlay decoder failed");
            return 0;
            }
#endif
          
          }
        break;
      case GAVL_STREAM_MSG:
        // fprintf(stderr, "Init_read msg %d\n", s->source_s->action);
        if(s->source_s->action == BG_STREAM_ACTION_DECODE)
          {
          s->source_s->msghub_priv = bg_msg_hub_create(1);
          s->source_s->msghub = s->source_s->msghub_priv;

          /* Redirect to the central event sink */
          // bg_msg_hub_connect_sink(s->source_s->msghub, p->controllable.evt_sink);
          break;
          }
      }
    
    }
  
  return 1;
  }

/* Uncompressed Sink funcs */
#if 0
static gavl_audio_frame_t * get_audio_func(void * priv)
  {
  stream_t * as = priv;
  as->p_ext = gavl_packet_sink_get_packet(as->sink_s->psink);
  gavl_packet_alloc(as->p_ext, as->ci.max_packet_size);
  as->aframe->valid_samples = as->afmt->samples_per_frame;
  gavl_audio_frame_set_channels(as->aframe, as->afmt,
                                as->p_ext->data);
  return as->aframe;
  }

static gavl_sink_status_t put_audio_func(void * priv,
                                         gavl_audio_frame_t * f)
  {
  stream_t * as = priv;
  gavf_audio_frame_to_packet_metadata(f, as->p_ext);
  as->p_ext->data_len = as->ci.max_packet_size;
  gavf_shrink_audio_frame(as->aframe, as->p_ext, as->afmt);

#ifdef DUMP_PACKETS
  fprintf(stderr, "Got audio packet\n");
  gavl_packet_dump(as->p_ext);
#endif
  return gavl_packet_sink_put_packet(as->sink_s->psink, as->p_ext);
  }

static gavl_video_frame_t * get_video_func(void * priv)
  {
  stream_t * vs = priv;
  vs->p_ext = gavl_packet_sink_get_packet(vs->sink_s->psink);
  gavl_packet_alloc(vs->p_ext, vs->ci.max_packet_size);
  gavl_video_frame_set_planes(vs->vframe, vs->vfmt,
                              vs->p_ext->data);
  return vs->vframe;
  }

static gavl_sink_status_t put_video_func(void * priv,
                                         gavl_video_frame_t * f)
  {
  stream_t * vs = priv;
  gavl_video_frame_to_packet_metadata(f, vs->p_ext);
  vs->p_ext->data_len = vs->ci.max_packet_size;
#ifdef DUMP_PACKETS
  fprintf(stderr, "Got video packet\n");
  gavl_packet_dump(vs->p_ext);
#endif
  return gavl_packet_sink_put_packet(vs->sink_s->psink, vs->p_ext);;
  }

static gavl_sink_status_t put_overlay_func(void * priv,
                                           gavl_video_frame_t * f)
  {
  stream_t * vs = priv;
  vs->p_ext = gavl_packet_sink_get_packet(vs->sink_s->psink);
  gavl_packet_alloc(vs->p_ext, vs->ci.max_packet_size);
  gavf_overlay_to_packet(f, vs->p_ext, vs->vfmt);
#ifdef DUMP_PACKETS
  fprintf(stderr, "Got video packet\n");
  gavl_packet_dump(vs->p_ext);
#endif
  return gavl_packet_sink_put_packet(vs->sink_s->psink, vs->p_ext);;
  }
#endif


/* Packet */

#if 0
static int init_write_common(bg_plug_t * p, stream_t * s)
  {
  s->plug = p;
  s->sink_int = gavf_get_packet_sink(p->g, s->id);
  
  s->sink_s->psink = s->sink_int;

  if(s->codec_handle)
    {
    bg_codec_plugin_t * codec = (bg_codec_plugin_t*)s->codec_handle->plugin;
    codec->set_packet_sink(s->codec_handle->priv, s->sink_s->psink);
    }
  
  return 1;
  }

static int handle_msg_forward(void * data, gavl_msg_t * msg)
  {
  stream_t * s = data;

  switch(msg->NS)
    {
    case GAVL_MSG_NS_SRC:

      switch(msg->ID)
        {
        case GAVL_MSG_SRC_RESYNC:
          return 1;
          break;
        }
      
    }
  
  fprintf(stderr, "bg_plug write_msg forward\n");
  gavl_msg_dump(msg, 2);
  
  /* Write the message */

  if(s && s->sink_s && s->sink_s->psink)
    {
    gavl_packet_t * p;
    p = gavl_packet_sink_get_packet(s->sink_s->psink);
    gavf_msg_to_packet(msg, p);
    return gavl_packet_sink_put_packet(s->sink_int, p);
    }
  //  else
  //    fprintf(stderr, "Blupp 2\n");
  return 1;
  }
#endif

static void create_sinks(bg_plug_t * p)
  {
#if 0
  int i;
  stream_t * s;
  
  /* Create sinks */
  for(i = 0; i < p->num_streams; i++)
    {
    s = p->streams + i;
    
    init_write_common(p, s);

    switch(s->type)
      {
      case GAVL_STREAM_NONE:
        break;
      case GAVL_STREAM_AUDIO:
        
        if(s->ci.id == GAVL_CODEC_ID_NONE)
          {
          s->sink_s->asink_priv = gavl_audio_sink_create(get_audio_func,
                                                         put_audio_func,
                                                         s, s->afmt);
          s->aframe = gavl_audio_frame_create(NULL);
          s->sink_s->asink = s->sink_s->asink_priv;
          }
        
        break;
      case GAVL_STREAM_VIDEO:
        if(s->ci.id == GAVL_CODEC_ID_NONE)
          {
          s->sink_s->vsink_priv = gavl_video_sink_create(get_video_func,
                                                         put_video_func,
                                                         s, s->vfmt);
          s->sink_s->vsink = s->sink_s->vsink_priv;
          s->vframe = gavl_video_frame_create(NULL);
          }
        break;
      case GAVL_STREAM_TEXT:
        break;
      case GAVL_STREAM_OVERLAY:
        if(s->ci.id == GAVL_CODEC_ID_NONE)
          {
          s->sink_s->vsink_priv = gavl_video_sink_create(NULL,
                                                         put_overlay_func,
                                                         s, s->vfmt);
          s->sink_s->vsink = s->sink_s->vsink_priv;
          s->vframe = gavl_video_frame_create(NULL);
          }
        break;
      case GAVL_STREAM_MSG:
        /* Create sink */
        s->sink_s->msgsink_priv = bg_msg_sink_create(handle_msg_forward, s, 1);
        s->sink_s->msgsink = s->sink_s->msgsink_priv;
        break;
      }
    }
#endif
  }

int bg_plug_start_program(bg_plug_t * p, const bg_media_source_t * src)
  {
  int i;
  gavl_dictionary_t dict;
  
  if(!p->wr)
    return 0;

  gavl_dictionary_init(&dict);
  gavl_dictionary_copy(&dict, src->track);
  
  /* TODO: Create sink streams */
  for(i = 0; i < src->num_streams; i++)
    {
    bg_media_source_stream_t * s;
    s = src->streams[i];
    
    if(s->action == BG_STREAM_ACTION_OFF)
      continue;

    
    
    }
  
  /* TODO: Initialize encoders */
  
  return 1;
  }


static int init_write(bg_plug_t * p)
  {
#if 0
  
  int i;
  stream_t * s;
  
  p->num_streams = gavl_track_get_num_streams_all(p->cur);
  p->streams = calloc(p->num_streams, sizeof(*p->streams));
  p->sink.track = p->cur;
  
  /* 1. Set up compressors and add gavf streams */
  
  for(i = 0; i < p->num_streams; i++)
    {
    s = p->streams + i;
    
    s->h = gavl_track_get_stream_all_nc(p->cur, i);
    
    s->type = gavl_stream_get_type(s->h);

    gavl_stream_get_id(s->h, &s->id);
    
    gavl_stream_get_compression_info(s->h, &s->ci);
    s->m = gavl_dictionary_get_dictionary_create(s->h, GAVL_META_METADATA);
    
    switch(s->type)
      {
      case GAVL_STREAM_NONE:
        break;
      case GAVL_STREAM_AUDIO:
        
        s->sink_s = bg_media_sink_append_audio_stream(&p->sink, s->h);
        
        if(s->codec_handle)
          {
          bg_codec_plugin_t * codec = (bg_codec_plugin_t*)s->codec_handle->plugin;
      
          gavl_metadata_delete_compression_fields(s->m);

          s->sink_s->asink =
            codec->open_encode_audio(s->codec_handle->priv,
                                     &s->ci, s->afmt, s->m);

          if(!s->sink_s->asink)
            return 0;
          s->sink_s->asink = s->sink_s->asink_priv;
          }

        
        s->afmt = gavl_stream_get_audio_format_nc(s->h);
        
        s->ci.max_packet_size = gavf_get_max_audio_packet_size(s->afmt, &s->ci);
    
        if((s->id = gavf_append_audio_stream(p->g, &s->ci, s->afmt, s->m)) < 0)
          return 0;
        
        break;
      case GAVL_STREAM_VIDEO:

        s->sink_s = bg_media_sink_append_video_stream(&p->sink, s->h);

        if(s->codec_handle)
          {
          bg_codec_plugin_t * codec = (bg_codec_plugin_t*)s->codec_handle->plugin;
          gavl_metadata_delete_compression_fields(s->m);
          
          s->sink_s->vsink = codec->open_encode_video(s->codec_handle->priv,
                                                      &s->ci, s->vfmt, s->m);
          if(!s->sink_s->vsink)
            return 0;
          }

        s->vfmt = gavl_stream_get_video_format_nc(s->h);
        s->ci.max_packet_size = gavf_get_max_video_packet_size(s->vfmt, &s->ci);

        if((s->id = gavf_append_video_stream(p->g, &s->ci, s->vfmt, s->m)) < 0)
          return 0;

        break;
      case GAVL_STREAM_TEXT:

        s->sink_s = bg_media_sink_append_text_stream(&p->sink, s->h);

        gavl_dictionary_get_int(s->m, GAVL_META_STREAM_SAMPLE_TIMESCALE, &s->timescale);
        
        if((s->id = gavf_append_text_stream(p->g, s->timescale, s->m)) < 0)
          return 0;
        break;
      case GAVL_STREAM_OVERLAY:

        s->sink_s = bg_media_sink_append_overlay_stream(&p->sink, s->h);
        
        if(s->codec_handle)
          {
          bg_codec_plugin_t * codec = (bg_codec_plugin_t*)s->codec_handle->plugin;
          gavl_metadata_delete_compression_fields(s->m);
          s->sink_s->vsink = codec->open_encode_overlay(s->codec_handle->priv,
                                                        &s->ci, s->vfmt, s->m);
          if(!s->sink_s->vsink)
            return 0;
          }

        s->vfmt = gavl_stream_get_video_format_nc(s->h);

        s->ci.max_packet_size = gavf_get_max_video_packet_size(s->vfmt, &s->ci);

        if((s->id = gavf_append_overlay_stream(p->g, &s->ci, s->vfmt, s->m)) < 0)
          return 0;
        break;
      case GAVL_STREAM_MSG:

        fprintf(stderr, "Append MSG stream %d\n", s->id);
        s->sink_s = bg_media_sink_append_msg_stream_by_id(&p->sink, s->h, s->id);
        gavf_add_msg_stream(p->g, s->id);
        break;
      }
    
    }
  
  fprintf(stderr, "gavf_start...\n");
  
  if(!gavf_start(p->g))
    {
    fprintf(stderr, "gavf_start failed\n");
    return 0;
    }
  fprintf(stderr, "gavf_start done\n");

  //  bg_plug_dump(p);
  create_sinks(p);
#endif  
  return 1;
  }

int bg_plug_set_media_info(bg_plug_t * p, const gavl_dictionary_t * mi)
  {
  if(!gavf_set_media_info(p->g, mi))
    return 0;
  
  return 1;
  }

int bg_plug_set_from_track(bg_plug_t * p, const gavl_dictionary_t * track)
  {
  
  if(!p->wr)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN,
           "bg_plug_set_from_track only works for write plugs");
    return 0;
    }

  gavl_dictionary_reset(p->cur);
  gavl_dictionary_copy(p->cur, track);
  p->num_streams = gavl_track_get_num_streams_all(p->cur);
  
  if(!gavf_start(p->g))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "gavf_start failed");
    return 0;
    }
  create_sinks(p);
  
  return 1;
  }

int bg_plug_start(bg_plug_t * p)
  {
  if(p->wr)
    return init_write(p);
  else
    return init_read(p);
  }

#if 0
int bg_plug_open(bg_plug_t * p, gavl_io_t * io, int io_flags)
  {
  int flags;
  char buf[8];
  
  p->io = io;
  p->io_flags = io_flags;
  
  /* Write */
  if(p->wr)
    {
    if(!p->nomsg)
      {
      if(has_messages(p))
        {
        // gavf_add_msg_stream(p->g, NULL);
        if(p->io_flags & BG_PLUG_IO_IS_SOCKET) /* Duplex connection already there */
          {
          /* Read messages (backchannel) */
          
          p->flags |= (PLUG_FLAG_HAS_MSG_READ | PLUG_FLAG_HAS_MSG_READ_THREAD);
          }
        else
          {
          /* Error? */
          }
        }
      }

    /* Set proper flags */
    flags = gavf_options_get_flags(p->opt);

    if(p->io_flags & BG_PLUG_IO_IS_REGULAR)
      flags |= GAVF_OPT_FLAG_PACKET_INDEX;
    else
      flags &= ~GAVF_OPT_FLAG_PACKET_INDEX;
    
    gavf_options_set_flags(p->opt, flags);
    
    return 1;
    }
  
  /* Read */
  
  //  flags = gavf_options_get_flags(p->opt);
  //  flags |= GAVF_OPT_FLAG_BUFFER_READ;
  //  gavf_options_set_flags(p->opt, flags);
  
  /* Multitrack mode */
  
  if((gavl_io_get_data(p->io, (uint8_t*)buf, 8) == 8) &&
     !memcmp(buf, BG_PLUG_MULTI_HEADER, 8))
    {
    gavf_chunk_t chunk;
    gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Multitrack mode detected");

    p->mi_priv = gavl_dictionary_create();
    p->mi = p->mi_priv;
    
    if(!gavf_chunk_read_header(p->io, &chunk) ||
       !gavf_read_dictionary(p->io,
                             &chunk,
                             p->mi))
      return 0;

    fprintf(stderr, "Got multitrack header:\n");
    gavl_dictionary_dump(p->mi, 2);
    
    set_flag(p, PLUG_FLAG_MULTITRACK_BGPLUG);

    }
  else
    {
    p->g = gavf_create();
    gavf_set_options(p->g, p->opt);
    gavf_set_msg_cb(p->g, msg_cb_read_av, p);
    
    if(!gavf_open_read(p->g, p->io))
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "gavf_open_read failed");
      return 0;
      }

    p->mi = gavf_get_media_info_nc(p->g);
    if(gavl_get_num_tracks(p->mi) > 1)
      set_flag(p, PLUG_FLAG_MULTITRACK_GAVF);
      
    }
  
  //  fprintf(stderr, "Opened stream:\n");
  //  gavl_dictionary_dump(p->mi, 2); 
  
  return 1;
  }
#endif


void bg_plug_finish_program(bg_plug_t * p, int discard)
  {
  if(!p->g)
    return;
  
  gavf_close(p->g, discard);

  bg_media_sink_cleanup(&p->sink);
  bg_media_sink_init(&p->sink);

  gavl_dictionary_destroy(p->mi_priv);

  p->mi_priv = NULL;
  p->mi = NULL;
  p->cur = NULL;
  
  p->g = NULL;
  }

#if 0
int bg_plug_start_program(bg_plug_t * p, const gavl_dictionary_t * m, int discard)
  {
  int ret = 0;

  bg_plug_finish_program(p, discard);

  p->mi_priv = gavl_dictionary_create();
  p->mi = p->mi_priv;
  p->cur = gavl_append_track(p->mi, NULL);
  
  p->g = gavf_create();
  gavf_set_options(p->g, p->opt);
  //  gavf_set_msg_cb(p->g, msg_cb_write_av, p);
  
  p->io_orig = p->io;
  p->io = gavl_io_create_sub_write(p->io_orig);
  
  if(!gavf_open_write(p->g, p->io, m))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "gavf_open_write failed");
    goto fail;
    }

  ret = 1;

  fail:
  
  return ret;
  }
#endif

int bg_plug_open_location(bg_plug_t * p, const char * location)
  {
  if(p->wr)
    {
    if(!gavf_open_uri_write(p->g, location))
      return 0;
    }
  else
    {
    if(!gavf_open_uri_read(p->g, location))
      return 0;

    if(!(p->mi = gavf_get_media_info_nc(p->g)))
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Could not read media info");
      return 0;
      }
    }
  
  return 1;
  }

gavf_t * bg_plug_get_gavf(bg_plug_t * p)
  {
  return p->g;
  }



/* Utility functions */
#if 0
const gavf_stream_header_t *
bg_plug_header_from_index(bg_plug_t * p, int index)
  {
  return &p->ph->streams[index];
  }

const gavf_stream_header_t *
bg_plug_header_from_id(bg_plug_t * p, uint32_t id)
  {
  int i;
  for(i = 0; i < p->ph->num_streams; i++)
    {
    if(p->ph->streams[i].id == id)
      return &p->ph->streams[i];
    }
  return NULL;
  }
#endif

/* Get stream sources */

static stream_t * find_stream_by_id(bg_plug_t * p, uint32_t id)
  {
  int i;
  for(i = 0; i < p->num_streams; i++)
    {
    if(p->streams[i].id == id)
      return &p->streams[i];
    }
  return NULL;
  }


#if 0
int bg_plug_get_stream_source(bg_plug_t * p,
                              const gavf_stream_header_t * h,
                              gavl_audio_source_t ** as,
                              gavl_video_source_t ** vs,
                              gavl_packet_source_t ** ps)
  {
  stream_t * s = NULL;
  if(!(s = find_stream_by_id_all(p, h->id)))
    return 0;
  
  if(as)
    {
    *as = s->asrc_proxy ? s->asrc_proxy : s->asrc;
    if(*as)
      return 1;
    }
  else if(vs)
    {
    *vs = s->vsrc_proxy ? s->vsrc_proxy : s->vsrc;
    if(*vs)
      return 1;
    }
  if(ps)
    *ps = s->psrc_proxy ? s->psrc_proxy : s->src_ext;
  
  return 1;
  }

int bg_plug_get_stream_sink(bg_plug_t * p,
                            const gavf_stream_header_t * h,
                            gavl_audio_sink_t ** as,
                            gavl_video_sink_t ** vs,
                            gavl_packet_sink_t ** ps)
  {
  stream_t * s = NULL;
  if(!(s = find_stream_by_id_all(p, h->id)))
    return 0;
     
  if(s->asink && as)
    *as = s->asink;
  else if(s->vsink && vs)
    *vs = s->vsink;
  else if(ps)
    *ps = s->sink_ext;
  else
    return 0;
  return 1;
  }
#endif


/* Compression parameters */

typedef struct
  {
  stream_t * s;
  bg_plugin_registry_t * plugin_reg;
  } set_codec_parameter_t;

#if 0
static void set_codec_parameter(void * data, const char * name,
                                const gavl_value_t * v)
  {
  set_codec_parameter_t * p = data;
  bg_plugin_registry_set_compressor_parameter(p->plugin_reg,
                                              &p->s->codec_handle,
                                              name,
                                              v);
  }
#endif

int bg_plug_add_stream(bg_plug_t * p,
                       const gavl_dictionary_t * dict, gavl_stream_type_t type)
  {
  int ret = 0;
  gavl_dictionary_t * s = gavl_track_append_stream(p->cur, type);

  gavl_dictionary_reset(s);
  gavl_dictionary_copy(s, dict);
  gavl_stream_get_id(s, &ret);

  if(type == GAVL_STREAM_TEXT)
    {
    //    fprintf(stderr, "bg_plug_add_text_stream\n");
    //    gavl_dictionary_dump(dict, 2);
    }
  
  return ret;
  }
  
int bg_plug_add_msg_stream(bg_plug_t * p,
                           const gavl_dictionary_t * m, int id)
  {
  int ret = 0;
  gavl_dictionary_t * s = gavl_track_append_msg_stream(p->cur, id);
  
  if(m)
    gavl_dictionary_copy(gavl_stream_get_metadata_nc(s), m);
  
  return ret;
  }

int bg_plug_add_mediaconnector_stream(bg_plug_t * p,
                                      bg_mediaconnector_stream_t * s)
  {
  if(s->type == GAVL_STREAM_MSG)
    {
    int id = 0;
    gavl_stream_get_id(s->s, &id);
    if(bg_plug_add_msg_stream(p, s->m, id) < 0)
      return 0;
    
    }
  else
    {
    if(bg_plug_add_stream(p, s->s, s->type) < 0)
      return 0;
    }
  return 1;
  }

int bg_plug_connect_mediaconnector_stream(bg_mediaconnector_stream_t * s,
                                          bg_media_sink_stream_t * ms)
  {
  if(ms->asink && s->aconn)
    gavl_audio_connector_connect(s->aconn, ms->asink);
  else if(ms->vsink && s->vconn)
    gavl_video_connector_connect(s->vconn, ms->vsink);
  else if(ms->psink && s->pconn)
    gavl_packet_connector_connect(s->pconn, ms->psink);
  else if(s->msghub && ms->msgsink)
    bg_msg_hub_connect_sink(s->msghub, ms->msgsink);
  else
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Connecting mediaconnector stream failed");
    return 0;
    }
  return 1;
  }

/* Setup writer */

#if 0
int bg_plug_setup_writer(bg_plug_t * p, bg_mediaconnector_t * conn)
  {
  int i;
  bg_mediaconnector_stream_t * s;
  bg_media_sink_stream_t * ms;
  
  for(i = 0; i < conn->num_streams; i++)
    {
    s = conn->streams[i];
    if(!bg_plug_add_mediaconnector_stream(p, s))
      return 0;
    }

  if(!bg_plug_start(p))
    return 0;
  
  /* Get sinks and connect them */

  for(i = 0; i < conn->num_streams; i++)
    {
    s = conn->streams[i];
    ms = p->sink.streams[i];
      
    if(!bg_plug_connect_mediaconnector_stream(s, ms))
      return 0;
    }
  return 1;
  }
#endif

/* Setup the input side for the media connector. The plug needs to be started already */
int bg_plug_setup_reader(bg_plug_t * p, bg_mediaconnector_t * conn)
  {
  bg_mediaconnector_set_from_source(conn, &p->src);
  return 1;
  }

#if 0
int bg_plug_got_error(bg_plug_t * p)
  {
  int ret;
  pthread_mutex_lock(&p->mutex);
  ret = p->got_error || gavl_io_got_error(p->io);
  pthread_mutex_unlock(&p->mutex);
  return ret;
  }
#endif

gavl_sink_status_t bg_plug_put_packet(bg_plug_t * p,
                                      gavl_packet_t * pkt)
  {
  stream_t * s = find_stream_by_id(p, pkt->id);
  if(!s)
    return GAVL_SINK_ERROR;
  return gavl_packet_sink_put_packet(s->sink_s->psink, pkt);
  }

/*
  
 */

static void init_media_source(bg_plug_t* p)
  {
  int i;
  bg_media_source_set_from_track(&p->src, p->cur);

  for(i = 0; i < p->src.num_streams; i++)
    {
    p->src.streams[i]->user_data = create_stream_priv(p->src.streams[i]->s, p->src.streams[i]->type);
    p->src.streams[i]->free_user_data = free_stream_priv;
    }
  
  }

static int handle_cmd_reader(void * data, gavl_msg_t * msg)
  {
  bg_plug_t* p = data;
  
  switch(msg->NS)
    {
    case GAVL_MSG_NS_SRC:
      switch(msg->ID)
        {
        case GAVL_CMD_SRC_SELECT_TRACK:
          {
          gavf_handle_reader_command(p->g, msg);
          bg_media_source_cleanup(&p->src);
          bg_media_source_init(&p->src);
          p->cur = gavf_get_current_track_nc(p->g);
          init_media_source(p);
          }
          break;
          
        case GAVL_CMD_SRC_START:
          {
          /* Enable streams */
          int i, id;
          
          for(i = 0; i < p->src.num_streams; i++)
            {
            if(!gavl_stream_get_id(p->src.streams[i]->s, &id))
              continue;

            if(p->src.streams[i]->action == BG_STREAM_ACTION_OFF)
              gavf_stream_set_skip(p->g, id);
            }
          
          gavf_handle_reader_command(p->g, msg);

          /* TODO: Initialize codecs */
          for(i = 0; i < p->src.num_streams; i++)
            {
            int id;

            stream_priv_t * s = p->src.streams[i]->user_data;
            gavl_stream_get_id(p->src.streams[i]->s, &id);
            
            switch(p->src.streams[i]->action)
              {
              case BG_STREAM_ACTION_OFF:
                continue;
                break;
              case BG_STREAM_ACTION_DECODE:
                {
                if(s->ci.id == GAVL_CODEC_ID_NONE)
                  {
                  
                  switch(p->src.streams[i]->type)
                    {
                    case GAVL_STREAM_AUDIO:
                      p->src.streams[i]->asrc = gavf_get_audio_source(p->g, id);
                      break;
                    case GAVL_STREAM_VIDEO:
                    case GAVL_STREAM_OVERLAY:
                      p->src.streams[i]->vsrc = gavf_get_video_source(p->g, id);
                      break;
                    case GAVL_STREAM_TEXT:
                      p->src.streams[i]->psrc = gavf_get_packet_source(p->g, id);
                      break;
                    case GAVL_STREAM_MSG:
                    case GAVL_STREAM_NONE:
                      break;
                    }
                  
                  }
                else
                  {
                  /* Initialize decoders */
                  
                  switch(p->src.streams[i]->type)
                    {
                    case GAVL_STREAM_AUDIO:
                      {
#if 0                      
                      bg_codec_plugin_t * codec;
                      /* Add decoder */
                      if(!(s->codec_handle = load_decompressor(s->ci.id,
                                                               BG_PLUGIN_DECOMPRESSOR_AUDIO)))
                        return 0;

                      codec = (bg_codec_plugin_t*)s->codec_handle->plugin;

                      p->src.streams[i]->asrc = codec->connect_decode_audio(s->codec_handle->priv,
                                                                            gavf_get_packet_source(p->g, id),
                                                                            &s->ci,
                                                                            s->afmt,
                                                                            gavl_stream_get_metadata_nc(p->src.streams[i]->s));
#endif
                      if(!p->src.streams[i]->asrc)
                        {
                        gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Initializing audio decoder failed");
                        return 0;
                        }
                      }
                      break;
                    case GAVL_STREAM_VIDEO:
                      {
#if 0
                      bg_codec_plugin_t * codec;
                      /* Add decoder */
                      if(!(s->codec_handle = load_decompressor(s->ci.id,
                                                               BG_PLUGIN_DECOMPRESSOR_VIDEO)))
                        return 0;

                      codec = (bg_codec_plugin_t*)s->codec_handle->plugin;

                      p->src.streams[i]->vsrc = codec->connect_decode_video(s->codec_handle->priv,
                                                                            gavf_get_packet_source(p->g, id),
                                                                            &s->ci,
                                                                            s->vfmt,
                                                                            gavl_stream_get_metadata_nc(p->src.streams[i]->s));
#endif
                      if(!p->src.streams[i]->vsrc)
                        {
                        gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Initializing audio decoder failed");
                        return 0;
                        }
                      }

                    case GAVL_STREAM_OVERLAY:
                      {
#if 0
                      bg_codec_plugin_t * codec;
                      /* Add decoder */
                      if(!(s->codec_handle = load_decompressor(s->ci.id,
                                                               BG_PLUGIN_DECOMPRESSOR_VIDEO)))
                        return 0;
                      codec = (bg_codec_plugin_t*)s->codec_handle->plugin;
                      
                      p->src.streams[i]->vsrc = codec->connect_decode_overlay(s->codec_handle->priv,
                                                                              gavf_get_packet_source(p->g, id),
                                                                              &s->ci,
                                                                              s->vfmt,
                                                                              gavl_stream_get_metadata_nc(p->src.streams[i]->s));
#endif
                      if(!p->src.streams[i]->vsrc)
                        {
                        gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Initializing overlay decoder failed");
                        return 0;
                        }

                      }
                      break;
                    case GAVL_STREAM_TEXT:
                    case GAVL_STREAM_MSG:
                    case GAVL_STREAM_NONE:
                      break;
                    }

                  }
                }
                break;
              case BG_STREAM_ACTION_READRAW:
                p->src.streams[i]->psrc = gavf_get_packet_source(p->g, id);
                break;
              }
            }
          
          }
          break;
        case GAVL_CMD_SRC_PAUSE:
          gavf_handle_reader_command(p->g, msg);
          //          bgav_pause(avdec->dec);
          break;
        case GAVL_CMD_SRC_RESUME:
          gavf_handle_reader_command(p->g, msg);
          //          bgav_resume(avdec->dec);
          break;
        case GAVL_CMD_SRC_SEEK:
          {
          gavf_handle_reader_command(p->g, msg);
#if 0
          
          bg_msg_hub_t * hub;
          int64_t time = gavl_msg_get_arg_long(msg, 0);
          int scale = gavl_msg_get_arg_int(msg, 1);

          /* Seek */
          bgav_seek_scaled(avdec->dec, &time, scale);

          hub = bg_media_source_get_msg_hub_by_id(&avdec->src, GAVL_META_STREAM_ID_MSG_PROGRAM);

          if(hub)
            {
            gavl_msg_t * resp;
            bg_msg_sink_t * sink = bg_msg_hub_get_sink(hub);
            
            resp = bg_msg_sink_get(sink);
            gavl_msg_set_src_resync(resp, time, scale, 1, 1);
            bg_msg_sink_put(sink);
            }
#endif
          }
          break;
        }
      
      break;
    }
  return 1;
  }

bg_controllable_t * bg_plug_get_controllable(bg_plug_t * p)
  {
  return &p->controllable;
  }

void bg_plug_select_track(bg_plug_t * p, int track)
  {
  gavl_msg_t * cmd = bg_msg_sink_get(p->controllable.cmd_sink);
  gavl_msg_set_id_ns(cmd, GAVL_CMD_SRC_SELECT_TRACK, GAVL_MSG_NS_SRC);
  gavl_msg_set_arg_int(cmd, 0, track);
  bg_msg_sink_put(p->controllable.cmd_sink);
  }

/*
 *  Input plugin
 */

static void * plug_plugin_create()
  {
  return NULL;
  }

#if 0
static int plug_plugin_start(void * priv)
  {
  bg_plug_t * plug = priv;
  return bg_plug_start(plug);
  }
#endif


static bg_controllable_t *
plug_plugin_get_controllable(void * priv)
  {
  return bg_plug_get_controllable(priv);
  }

static bg_media_source_t *
plug_plugin_get_src(void * priv)
  {
  bg_plug_t * plug = priv;
  return &plug->src;
  }

static void plug_plugin_destroy(void * priv)
  {
  bg_plug_t * plug = priv;

  bg_plug_destroy(plug);
  }

static const bg_parameter_info_t * plug_plugin_get_parameters(void * priv)
  {
  return bg_plug_get_input_parameters();
  }

static gavl_dictionary_t * plug_plugin_get_media_info(void * p)
  {
  bg_plug_t * plug = p;
  return bg_plug_get_media_info(plug);
  }

char const * const
bg_plug_plugin_protocols =
  GAVF_PROTOCOL_TCP" "\
  GAVF_PROTOCOL_TCPSERV" "\
  GAVF_PROTOCOL_UNIX" "\
  GAVF_PROTOCOL_UNIXSERV" "\
  GAVF_PROTOCOL;

char const * const
bg_plug_plugin_extensions =
  GAVF_EXTENSION;

// char const * const
// bg_plug_mimetype = "application/x-bgplug";

static const char * plug_plugin_get_protocols(void * priv)
  {
  return bg_plug_plugin_protocols;
  }

static const char * plug_plugin_get_extensions(void * priv)
  {
  return bg_plug_plugin_extensions;
  }

#if 0
static const char * plug_plugin_get_mimetypes(void * priv)
  {
  return bg_plug_mimetype;
  }
#endif

static int plug_plugin_open(void * priv, const char * location)
  {
  bg_plug_t * plug = priv;
  //  fprintf(stderr, "plug_plugin_open: %s\n", location);
  return bg_plug_open_location(plug, location);
  }

static void plug_plugin_close(void * priv)
  {
  return;
  }

static const bg_input_plugin_t plug_input =
  {
  common:
    {
    BG_LOCALE,
      .name =           "i_bgplug",
      .long_name =      TRS("Gmerlin plug plugin"),
      .description =    TRS("Reader for gmerlin standardized media streams"),
      .type =           BG_PLUGIN_INPUT,
      .flags =          BG_PLUGIN_FILE|BG_PLUGIN_URL|BG_PLUGIN_CALLBACKS,
      .priority =       BG_PLUGIN_PRIORITY_MAX,

      .create           = plug_plugin_create,
      .destroy          = plug_plugin_destroy,
      .get_parameters   = plug_plugin_get_parameters,
      .set_parameter    = bg_plug_set_parameter,
      .get_controllable = plug_plugin_get_controllable,
      },

  .get_protocols = plug_plugin_get_protocols,
  //  .get_mimetypes = plug_plugin_get_mimetypes,
  .get_extensions = plug_plugin_get_extensions,

  .open = plug_plugin_open,

  //  .open_io = plug_plugin_open_io,
  
  .get_media_info = plug_plugin_get_media_info,
  
  //  .get_audio_compression_info = plug_plugin_get_audio_compression_info,
  //  .get_video_compression_info = plug_plugin_get_video_compression_info,
  //  .get_overlay_compression_info = plug_plugin_get_overlay_compression_info,

  .get_src           = plug_plugin_get_src,
  
  .close = plug_plugin_close,
  
  };

bg_plugin_handle_t * bg_input_plugin_create_plug()
  {
  bg_plugin_handle_t * ret;
  bg_plug_t * priv;
  
  ret = calloc(1, sizeof(*ret));
  
  ret->plugin = (bg_plugin_common_t*)&plug_input;
  ret->info = bg_plugin_find_by_name("i_bgplug");
  
  pthread_mutex_init(&ret->mutex, NULL);

  priv = bg_plug_create_reader();
  
  ret->priv = priv;
  ret->refcount = 1;

  bg_plugin_handle_connect_control(ret);
  
  /* Unload the plugin after we copied the EDL, since it might be owned by the plugin */
  
  return ret;
  }

bg_plugin_info_t * bg_plug_input_get_info()
  {
  bg_plugin_info_t * ret = bg_plugin_info_create(&plug_input.common);

  ret->extensions = gavl_value_set_array(&ret->extensions_val);
  bg_string_to_string_array(bg_plug_plugin_extensions, ret->extensions);
  
  ret->protocols = gavl_value_set_array(&ret->protocols_val);
  bg_string_to_string_array(bg_plug_plugin_protocols, ret->protocols);
  
  //  ret->mimetypes = gavl_value_set_array(&ret->mimetypes_val);
  //  bg_string_to_string_array(bg_plug_mimetype, ret->mimetypes);
  
  return ret;
  }

bg_media_source_t * bg_plug_get_source(bg_plug_t * p)
  {
  if(p->wr)
    return NULL;
  
  return &p->src;
  }

bg_media_sink_t * bg_plug_get_sink(bg_plug_t * p)
  {
  if(!p->wr)
    return NULL;

  return &p->sink;
  }

