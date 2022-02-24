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

#include <bgshm.h>
#include <gavl/metatags.h>

#include <gavl/gavf.h>

#include <gmerlin/plugin.h>
#include <gmerlin/bggavl.h>

#include <gmerlin/bgplug.h>
#include <gmerlin/utils.h>
#include <gmerlin/bgsocket.h>
#include <gmerlin/http.h>

#include <gmerlin/translation.h>
#include <gmerlin/log.h>

#define LOG_DOMAIN "plug"

#define META_SHM_SIZE     "bgplug_shm_size"
#define META_RETURN_QUEUE "bgplug_return_queue"

#define PLUG_FLAG_HAS_MSG_READ                (1<<1)
#define PLUG_FLAG_HAS_MSG_WRITE               (1<<2)
#define PLUG_FLAG_HAS_MSG_READ_THREAD         (1<<3)
#define PLUG_FLAG_MSG_READ_THREAD_RUNNING     (1<<4)
#define PLUG_FLAG_MSG_READ_THREAD_STOP        (1<<5)
#define PLUG_FLAG_MULTITRACK_GAVF             (1<<6)
#define PLUG_FLAG_MULTITRACK_BGPLUG           (1<<7)
#define PLUG_FLAG_GOT_RESYNC                  (1<<8)
#define PLUG_FLAG_SEEKING                     (1<<9)

// #define DUMP_MESSAGES

// #define SHM_THRESHOLD 1024 // Minimum max_packet_size to switch to shm

typedef struct
  {
  int id;  /* ID of the segment */
  int len; /* Real length       */
  } shm_info_t;

typedef struct
  {
  /* Is set before the header exists */
  gavl_stream_type_t type;
  int id;
    
  bg_shm_pool_t * sp;
  bg_shm_t * shm_segment;

  int shm_size; // Size of an shm seqment */
  
  /* Reading */
  gavl_packet_source_t * src_int;
  
  /* Writing */
  gavl_packet_sink_t * sink_int;
  
  bg_plug_t * plug;

  gavl_packet_t p_shm;
  gavl_packet_t * p_ext;
  
  /* Audio stuff */
  gavl_audio_frame_t * aframe;
  gavl_audio_format_t * afmt;
  
  /* Video stuff */
  gavl_video_frame_t * vframe;
  gavl_video_format_t * vfmt;
  gavl_compression_info_t ci;

  gavl_dictionary_t * m;
  int timescale;
  
  bg_plugin_handle_t * codec_handle;
  
  gavl_dsp_context_t * dsp;

  bg_media_source_stream_t * source_s;
  bg_media_sink_stream_t * sink_s;

  gavl_dictionary_t * h;

  } stream_t;

struct bg_plug_s
  {
  int wr;
  gavf_t * g;

  gavf_io_t * io_orig;
  gavf_io_t * io;
  
  int flags;
  pthread_mutex_t flags_mutex;
  
  /* Opposite direction */
  gavf_io_t * io_msg;
  
  int num_audio_streams;
  int num_video_streams;
  int num_text_streams;
  int num_overlay_streams;
  int num_msg_streams;

  int num_streams;
  
  stream_t * streams;
  
  int io_flags;  
  
  gavf_options_t * opt;


  pthread_mutex_t mutex;
  
  gavl_packet_t skip_packet;
  
  int got_error;
  int shm_threshold;
  int timeout;
  int nomsg;
  
  bg_controllable_t controllable;
  bg_control_t control;
  
  gavl_dictionary_t * mi_priv;
  gavl_dictionary_t * mi;
  gavl_dictionary_t * cur;

  pthread_t msg_read_thread;

  bg_media_source_t src;
  
  bg_media_sink_t sink;

  /* The read thread puts the messages there */
  bg_msg_sink_t * msg_sink_int;

  int got_packet_ack;

  int64_t resync_time;
  int     resync_scale;
  int     resync_discard;
  int     resync_discont;
  
  };


static int msg_sink_func_read(void * data, gavl_msg_t * msg);
static int msg_sink_func_write(void * data, gavl_msg_t * msg);

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

#if 0
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
  bg_plug_t * plug = data;

  //  fprintf(stderr, "handle_msg_backchannel_wr\n");
  //  gavl_msg_dump(msg, 2);
  
  switch(msg->NS)
    {
    case GAVL_MSG_NS_GAVF:
      switch(msg->ID)
        {
        case GAVL_MSG_GAVF_PACKET_ACK:
          {
          plug->got_packet_ack = 1;
          return 1;
          }
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
          if(!has_flag(plug, PLUG_FLAG_MULTITRACK_BGPLUG))
            return 1;
          }
        }
      break;
    }
  /* Transfer to sink */
  bg_msg_sink_put(plug->control.cmd_sink, msg);
  return 1;
  }

/* The *writing* plug opens a separate thread for *reading* messages from the destination plug */

static void * msg_read_func(void * priv)
  {
  //  int id;
  //  const gavl_dictionary_t * dict;
  bg_plug_t * plug = priv;
  gavl_msg_t * msg;
  
  fprintf(stderr, "msg_read_func started\n");
  
  if(!plug->io_msg)
    {
    /* Shouldn't happen */
    return NULL;
    }
  
  while(1)
    {
    /* Read message */
    if(has_flag(plug, PLUG_FLAG_MSG_READ_THREAD_STOP))
      break;
    
    /* Select */

    if(!gavf_io_can_read(plug->io_msg, 50))
      continue;
    
    msg = bg_msg_sink_get(plug->msg_sink_int);
    
    if(!gavl_msg_read(msg, plug->io_msg))
      {
      gavl_dprintf("gavl_msg_read failed\n");
      gavl_msg_free(msg);
      gavl_msg_set_id_ns(msg, GAVL_CMD_QUIT, GAVL_MSG_NS_GENERIC);
      bg_msg_sink_put(plug->msg_sink_int, msg);
      break;
      }
    
#ifdef DUMP_MESSAGES
    gavl_dprintf("Got message from backchannel:\n");
    gavl_msg_dump(msg, 2);
#endif // DUMP_MESSAGES
    
    bg_msg_sink_put(plug->msg_sink_int, msg);
    }
  
  fprintf(stderr, "msg_read_func finished\n");
  
  return NULL;
  }

static void close_read_thread(bg_plug_t * plug)
  {
  int running = 0;
  
  pthread_mutex_lock(&plug->flags_mutex);

  if(plug->flags & PLUG_FLAG_MSG_READ_THREAD_RUNNING)
    {
    plug->flags |= PLUG_FLAG_MSG_READ_THREAD_STOP;
    running = 1;  
    }
  
  pthread_mutex_unlock(&plug->flags_mutex);
  
  if(running)
    pthread_join(plug->msg_read_thread, NULL);
  }


/* Called by the source */
static int accept_backchannel_socket(bg_plug_t * p)
  {
  int ret = 0;
  gavl_dictionary_t req;
  gavl_dictionary_init(&req);
  
  if(has_flag(p, PLUG_FLAG_HAS_MSG_READ_THREAD) &&
     !has_flag(p, PLUG_FLAG_MSG_READ_THREAD_RUNNING))
    {
    p->msg_sink_int = bg_msg_sink_create(handle_msg_backchannel_wr, p, 0);
    pthread_create(&p->msg_read_thread, NULL, msg_read_func, p);
    set_flag(p, PLUG_FLAG_MSG_READ_THREAD_RUNNING);
    }

  ret = 1;
  
  gavl_dictionary_free(&req);

  if(!ret)
    {
    gavf_io_destroy(p->io_msg);
    p->io_msg = NULL;
    }

  return 1;
  }

/* Called by the destination */
static int connect_backchannel(bg_plug_t * p, const gavl_dictionary_t * dict)
  {
  if(p->io_flags & BG_PLUG_IO_IS_SOCKET) /* Duplex connection already there */
    {
    /* Write messages (back) */
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Using duplex backchannel");

    p->io_msg = gavf_io_create_socket(gavf_io_get_socket(p->io), 1000, 0);
    }
                
  if(p->io_msg)
    {
    //  fprintf(stderr, "PLUG_FLAG_HAS_MSG_WRITE\n");
    set_flag(p, PLUG_FLAG_HAS_MSG_WRITE);
    }
  else
    {
    /* Error? */
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Couldn't open backchannel");
    return 0;
    }
  return 1;
  }


#if 0
void bg_plug_dump(const bg_plug_t * plug)
  {
  fprintf(stderr, "bgplug\n");
  gavl_dictionary_dump(plug->mi, 2);
  }
#endif

static void bg_plug_flush_backchannel_msg(bg_plug_t * plug)
  {
  bg_msg_sink_iteration(plug->msg_sink_int);
  }

int bg_plug_next_backchannel_msg(bg_plug_t * plug)
  {
  int ret = 0;
  gavl_msg_t * msg = bg_msg_sink_get_read_block(plug->msg_sink_int);

  ret = handle_msg_backchannel_wr(plug, msg);
  bg_msg_sink_done_read(plug->msg_sink_int);
  return ret;
  }


static void clear_flag(bg_plug_t * p, int flag)
  {
  pthread_mutex_lock(&p->flags_mutex);
  p->flags &= ~flag;
  pthread_mutex_unlock(&p->flags_mutex);
  }

static void init_streams_read(bg_plug_t * p);
static void cleanup_streams(bg_plug_t * p);

gavl_dictionary_t * bg_plug_get_media_info(bg_plug_t * p)
  {
  return p->mi;
  }

const gavl_dictionary_t * bg_plug_get_metadata(bg_plug_t * p)
  {
  return gavl_track_get_metadata(p->cur);
  }

static int has_messages(bg_plug_t * p)
  {
  if(p->controllable.cmd_sink || p->control.cmd_sink)
    return 1;
  else
    return 0;
  }

/* Message callbacks for the gavf instances */

/* gavf instance, which *reads* A/V data */
static int msg_cb_read_av(void * data, gavl_msg_t * msg)
  {
  bg_plug_t * p = data;

  switch(msg->NS)
    {
    case GAVL_MSG_NS_SRC :
      switch(msg->ID)
        {
        case GAVL_MSG_SRC_RESYNC:
          {
          
          gavl_msg_get_src_resync(msg, &p->resync_time, &p->resync_scale, &p->resync_discard, &p->resync_discont);
          
          set_flag(p, PLUG_FLAG_GOT_RESYNC);
          
          fprintf(stderr, "bgplug: Got resync %"PRId64" %d %d %d\n",
                  p->resync_time, p->resync_scale, p->resync_discard, p->resync_discont);
          
          break;
          }
        }
      break;
    case GAVL_MSG_NS_GAVF:
      switch(msg->ID)
        {
        case GAVL_MSG_GAVF_GOT_EOF:
          fprintf(stderr, "GAVL_MSG_GAVF_GOT_EOF av rd\n");

          
          
          break;
        case GAVL_MSG_GAVF_READ_PROGRAM_HEADER_START:
          fprintf(stderr, "GAVL_MSG_GAVF_READ_PROGRAM_HEADER_START av rd\n");
          break;
        case GAVL_MSG_GAVF_READ_PROGRAM_HEADER_END:
          {
          gavl_value_t arg_val;
          const gavl_dictionary_t * dict;
          
          fprintf(stderr, "GAVL_MSG_GAVF_READ_PROGRAM_HEADER_END av rd %p\n", p->controllable.cmd_sink);
          
          gavl_value_init(&arg_val);
          gavl_msg_get_arg(msg, 0, &arg_val);
          
          // gavl_dictionary_dump(dict, 0);
          
          /* Back channel */
          if(!p->nomsg && p->controllable.cmd_sink)
            {
            // fprintf(stderr, "p->controllable.cmd_sink: %p\n", p->controllable.cmd_sink);
      
            /* Write messages (backchannel) */

            if(!p->io_msg && (p->io_flags & (BG_PLUG_IO_IS_SOCKET|BG_PLUG_IO_IS_LOCAL)))
              {
              if((dict = gavl_value_get_dictionary(&arg_val)))
                {
                if(!connect_backchannel(p, dict))
                  ;
                }
              }
            }
          gavl_value_free(&arg_val);
          }
          // gavf_program_header_dump(p->ph);
          break;
        case GAVL_MSG_GAVF_READ_SYNC_HEADER_START:
          // fprintf(stderr, "GAVL_MSG_GAVF_READ_SYNC_HEADER_START av rd\n");
          
          break;
        case GAVL_MSG_GAVF_READ_SYNC_HEADER_END:
          // fprintf(stderr, "GAVL_MSG_GAVF_READ_SYNC_HEADER_END av rd\n");
          
          break;
        case GAVL_MSG_GAVF_READ_PACKET_START:
          //          fprintf(stderr, "GAVL_MSG_GAVF_READ_PACKET_START av rd\n");
          break;
        case GAVL_MSG_GAVF_READ_PACKET_END:
          /* TODO: Send reply */
          // fprintf(stderr, "GAVL_MSG_GAVF_READ_PACKET_END av rd %p\n", p->controllable.cmd_sink);
          
          if(p->io_msg)
            {
            gavl_msg_t msg;
            gavl_msg_init(&msg);
            gavl_msg_set_id_ns(&msg, GAVL_MSG_GAVF_PACKET_ACK, GAVL_MSG_NS_GAVF);
            gavl_msg_write(&msg, p->io_msg);
            gavl_msg_free(&msg);
            }
          
          if(has_flag(p, PLUG_FLAG_HAS_MSG_WRITE))
            {
            
            }
          
          break;
        }
    }
  
  return 1;
  }


/* gavf instance, which *writes* A/V data */
static int msg_cb_write_av(void * data, gavl_msg_t * msg)
  {
  bg_plug_t * p = data;

  
  switch(msg->NS)
    {
    case GAVL_MSG_NS_GAVF:
      switch(msg->ID)
        {
        case GAVL_MSG_GAVF_WRITE_HEADER_START:
          //          fprintf(stderr, "GAVL_MSG_GAVF_WRITE_HEADER_START av wr\n");
          break;
        case GAVL_MSG_GAVF_WRITE_HEADER_END:
          //          fprintf(stderr, "GAVL_MSG_GAVF_WRITE_HEADER_END av wr\n");
          if(!accept_backchannel_socket(p))
            return 0;
          break;
        case GAVL_MSG_GAVF_WRITE_SYNC_HEADER_START:
          // fprintf(stderr, "GAVL_MSG_GAVF_WRITE_SYNC_HEADER_START av wr\n");
          
          break;
        case GAVL_MSG_GAVF_WRITE_SYNC_HEADER_END:
          // fprintf(stderr, "GAVL_MSG_GAVF_WRITE_SYNC_HEADER_END av wr\n");

          break;
        case GAVL_MSG_GAVF_WRITE_PACKET_START:
          // fprintf(stderr, "GAVL_MSG_GAVF_WRITE_PACKET_START av wr\n");

          break;
        case GAVL_MSG_GAVF_WRITE_PACKET_END:
          /* TODO: Wait reply */
          // fprintf(stderr, "GAVL_MSG_GAVF_WRITE_PACKET_END av wr\n");
          
          if(has_flag(p, PLUG_FLAG_HAS_MSG_READ))
            {
            while(1)
              {
              bg_msg_sink_iteration(p->msg_sink_int);
              if(p->got_packet_ack)
                {
                //         fprintf(stderr, "Got packet ack\n");
                p->got_packet_ack = 0;
                break;
                }
              }
            }
          break;
        }
      break;
    }
  return 1;
  }

static bg_plug_t * create_common()
  {
  bg_plug_t * ret = calloc(1, sizeof(*ret));

  ret->opt = gavf_options_create();
  
  ret->timeout = 10000;
  ret->shm_threshold = 1024;
  
  pthread_mutex_init(&ret->mutex, NULL);
  pthread_mutex_init(&ret->flags_mutex, NULL);

  return ret;
  }

/* Assumes everything is locked */
static void handle_msg_read(bg_plug_t * plug)
  {
  int i;
  stream_t * s;
  gavl_packet_t * p = NULL;
  gavl_source_status_t st;
  
  for(i = 0; i < plug->num_streams; i++)
    {
    s = &plug->streams[i];

    if((s->type != GAVL_STREAM_MSG) || !s->source_s || !s->source_s->psrc)
      continue;
    
    while((st = gavl_packet_source_read_packet(s->source_s->psrc, &p)) == GAVL_SOURCE_OK)
      {

      if(s->source_s->msghub)
        {
        bg_msg_sink_t * sink;
        gavl_msg_t * msg;

        sink = bg_msg_hub_get_sink(s->source_s->msghub);
        
        msg = bg_msg_sink_get(sink);
        
        gavf_packet_to_msg(p, msg);
        
        //        fprintf(stderr, "*** Got message\n");
        //        gavl_msg_dump(msg, 2);
        bg_msg_sink_put(sink, msg);
        }
      
      }
    }
  
  }

static void lock_func_read(void * priv)
  {
  stream_t * s = priv;
  //  fprintf(stderr, "Lock func read...\n");
  pthread_mutex_lock(&s->plug->mutex);
  //  fprintf(stderr, "Lock func read done\n");
  
  handle_msg_read(s->plug);
  }

static void lock_func_write(void * priv)
  {
  stream_t * s = priv;
  //  fprintf(stderr, "Lock func write...\n");
  pthread_mutex_lock(&s->plug->mutex);
  bg_plug_flush_backchannel_msg(s->plug);
  //  fprintf(stderr, "Lock func write done\n");
  }

static void unlock_func(void * priv)
  {
  stream_t * s = priv;
  pthread_mutex_unlock(&s->plug->mutex);
  //  fprintf(stderr, "Unlock func\n");
  }

bg_plug_t * bg_plug_create_reader()
  {
  bg_plug_t * ret;
  ret = create_common();
  
  bg_controllable_init(&ret->controllable,
                       bg_msg_sink_create(msg_sink_func_read, ret, 1),
                       bg_msg_hub_create(1));
  
  
  return ret;
  }

bg_plug_t * bg_plug_create_writer()
  {
  bg_plug_t * ret = create_common();
  ret->wr = 1;
  
  gavf_options_set_sync_distance(ret->opt, GAVL_TIME_SCALE / 2);
  
  bg_control_init(&ret->control,
                  bg_msg_sink_create(msg_sink_func_write, ret, 1));
  
  
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
  int i;
  stream_t * s;

  for(i = 0; i < p->num_streams; i++)
    {
    s = p->streams + i;
    if(s->sp)
      bg_shm_pool_destroy(s->sp);
    
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
  
  if(p->io)
    gavf_io_destroy(p->io);

  cleanup_streams(p);
  
  gavl_packet_free(&p->skip_packet);
  
  close_read_thread(p);
  
  pthread_mutex_destroy(&p->mutex);
  pthread_mutex_destroy(&p->flags_mutex);

  if(p->mi_priv)
    gavl_dictionary_destroy(p->mi_priv);

  bg_controllable_cleanup(&p->controllable);
  bg_control_cleanup(&p->control);
  
  if(p->msg_sink_int)
    bg_msg_sink_destroy(p->msg_sink_int);
  
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
      .name = "shm",
      .long_name = TRS("SHM threshold"),
      .type = BG_PARAMETER_INT,
      .val_default = GAVL_VALUE_INIT_INT(1024),
      .help_string = TRS("Select the minimum packet size for using shared memory for a stream. Non-positive values disable shared memory completely"),
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
  else if(!strcmp(name, "shm"))
    p->shm_threshold = val->v.i;
  else if(!strcmp(name, "timeout"))
    p->timeout = val->v.i;
  }

#undef SET_GAVF_FLAG

/* Read/write */

static void init_streams_read(bg_plug_t * p)
  {
  int i;
  stream_t * s;

  const gavl_dictionary_t * dict;
  
  p->cur = gavf_get_current_track_nc(p->g);
  p->src.track = p->cur;
  
  p->num_streams = gavl_track_get_num_streams_all(p->cur);
  
  for(i = 0; i < p->num_streams; i++)
    {
    gavl_stream_type_t type;
    
    dict = gavl_track_get_stream_all(p->cur, i);
    type = gavl_stream_get_type(dict);
    
    switch(type)
      {
      case GAVL_STREAM_AUDIO:
        p->num_audio_streams++;
        break;
      case GAVL_STREAM_VIDEO:
        p->num_video_streams++;
        break;
      case GAVL_STREAM_TEXT:
        p->num_text_streams++;
        break;
      case GAVL_STREAM_OVERLAY:
        p->num_overlay_streams++;
        break;
      case GAVL_STREAM_MSG:
        p->num_msg_streams++;
        break;
      case GAVL_STREAM_NONE:
        break;
      }
    }

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
        s->vfmt = gavl_stream_get_video_format_nc(s->h);
        if(s->ci.id == GAVL_CODEC_ID_NONE)
          s->source_s->action = BG_STREAM_ACTION_DECODE;
        else
          s->source_s->action = BG_STREAM_ACTION_READRAW;
        }
      case GAVL_STREAM_OVERLAY:
        {
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

/* Read support */

static gavl_source_status_t read_packet_shm(void * priv,
                                            gavl_packet_t ** ret)
  {
  shm_info_t si;
  gavl_source_status_t st;
  gavl_packet_t * p = NULL;
  stream_t * s = priv;
  
  if((st = gavl_packet_source_read_packet(s->src_int, &p)) !=
     GAVL_SOURCE_OK)
    return st;

  /* Sanity check */
  if(p->data_len != sizeof(si))
    {
    gavl_log(GAVL_LOG_DEBUG, LOG_DOMAIN, "SHM packet data size mismatch, %d != %d\n",
           (int)p->data_len, (int)(sizeof(si)));
    return GAVL_SOURCE_EOF;
    }
  memcpy(&si, p->data, sizeof(si));

  /* Unref the old segment if there is one */
  if(s->shm_segment)
    bg_shm_unref(s->shm_segment);
  
  /* Obtain memory segment */
  s->shm_segment = bg_shm_pool_get_read(s->sp, si.id);

  if(!s->shm_segment)
    return GAVL_SOURCE_EOF;
  
  /* Copy metadata */
  
  memcpy(&s->p_shm, p, sizeof(*p));
  
  /* Exchange pointers */
  s->p_shm.data       = bg_shm_get_buffer(s->shm_segment, &s->p_shm.data_alloc);
  s->p_shm.data_len   = si.len;
  
  *ret = &s->p_shm;
  return GAVL_SOURCE_OK;
  }

static void unref_shm_packet(gavl_packet_t * p,
                             void * priv)
  {
  shm_info_t si;
  stream_t * s = priv;
  
  /* Sanity check */
  if(s->plug->skip_packet.data_len != sizeof(si))
    return;
  
  memcpy(&si, s->plug->skip_packet.data, sizeof(si));
  
  /* Obtain memory segment */
  s->shm_segment = bg_shm_pool_get_read(s->sp, si.id);
  
  /* Unref the segment if there is one */
  if(s->shm_segment)
    bg_shm_unref(s->shm_segment);
  
  }

static int init_read_common(bg_plug_t * p,
                            stream_t * s)
  {
  int id;
  gavl_dictionary_t * m = gavl_stream_get_metadata_nc(s->h);

  // fprintf(stderr, "init_read_common:\n");
  // gavl_dictionary_dump(s->h, 2);
  
  gavl_dictionary_get_int(m, META_SHM_SIZE, &s->shm_size);

  gavl_stream_get_id(s->h, &id);
  
  if(s->shm_size)
    {
    /* Create shared memory pool */
    s->sp = bg_shm_pool_create(s->shm_size, 0);
    /* Clear metadata tags */
    gavl_dictionary_set_string(m, META_SHM_SIZE, NULL);

    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Using shm for reading (segment size: %d)",
           s->shm_size);
    }

  if(s->shm_size)
    gavf_stream_set_unref(p->g, id, unref_shm_packet, s);
  
  if(s->source_s->action == BG_STREAM_ACTION_OFF)
    {
    gavf_stream_set_skip(p->g, id);
    return 0;
    }
  
  s->src_int = gavf_get_packet_source(p->g, id);

  if(s->type != GAVL_STREAM_MSG)
    gavl_packet_source_set_lock_funcs(s->src_int, lock_func_read, unlock_func, s);
  
  if(s->shm_size)
    {
    s->source_s->psrc_priv =
      gavl_packet_source_create_source(read_packet_shm, s,
                                       GAVL_SOURCE_SRC_ALLOC,
                                       s->src_int);
    s->source_s->psrc = s->source_s->psrc_priv;
    }
  else
    s->source_s->psrc = s->src_int;

  return 1;
  }

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
  info = bg_plugin_find_by_compression(bg_plugin_reg, id,
                                       type_mask);
  if(!info)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN,
           "Cannot find %s decompressor for %s",
           codec_type_name(type_mask),
           gavl_compression_get_short_name(id));
    return NULL;
    }
  ret = bg_plugin_load(bg_plugin_reg, info);
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
          s->source_s->asrc_priv = gavl_audio_source_create(read_audio_func, s,
                                                            GAVL_SOURCE_SRC_ALLOC,
                                                            s->afmt);
          
          s->source_s->asrc = s->source_s->asrc_priv;

          s->aframe = gavl_audio_frame_create(NULL);
          }
        else if(s->source_s->action == BG_STREAM_ACTION_DECODE)
          {
          bg_codec_plugin_t * codec;
          /* Add decoder */
          if(!(s->codec_handle = load_decompressor(s->ci.id,
                                                   BG_PLUGIN_DECOMPRESSOR_AUDIO)))
            return 0;

          codec = (bg_codec_plugin_t*)s->codec_handle->plugin;

          s->source_s->asrc = codec->connect_decode_audio(s->codec_handle->priv,
                                                          s->source_s->psrc,
                                                          &s->ci,
                                                          s->afmt,
                                                          s->m);
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
          s->source_s->vsrc_priv =
            gavl_video_source_create(read_video_func, s,
                                     GAVL_SOURCE_SRC_ALLOC,
                                     s->vfmt);

          s->source_s->vsrc = s->source_s->vsrc_priv;
          
          s->vframe = gavl_video_frame_create(NULL);
          }
        else if(s->source_s->action == BG_STREAM_ACTION_DECODE)
          {
          bg_codec_plugin_t * codec;
          /* Add decoder */
          if(!(s->codec_handle = load_decompressor(s->ci.id,
                                                   BG_PLUGIN_DECOMPRESSOR_VIDEO)))
            return 0;
          codec = (bg_codec_plugin_t*)s->codec_handle->plugin;
      
          s->source_s->vsrc = codec->connect_decode_video(s->codec_handle->priv,
                                                          s->source_s->psrc,
                                                          &s->ci,
                                                          s->vfmt,
                                                          s->m);
          
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
          s->source_s->vsrc_priv =
            gavl_video_source_create(read_overlay_func, s,
                                     GAVL_SOURCE_SRC_ALLOC |
                                     GAVL_SOURCE_SRC_DISCONTINUOUS, 
                                     s->vfmt);
          s->source_s->vsrc = s->source_s->vsrc_priv;
          s->vframe = gavl_video_frame_create(NULL);
          }
        else if(s->source_s->action == BG_STREAM_ACTION_DECODE)
          {
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
  gavf_video_frame_to_packet_metadata(f, vs->p_ext);
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



/* Packet */

static gavl_packet_t * get_packet_shm(void * priv)
  {
  stream_t * s = priv;

  s->shm_segment = bg_shm_pool_get_write(s->sp);
  
  s->p_shm.data = bg_shm_get_buffer(s->shm_segment, &s->p_shm.data_alloc);
  gavl_packet_reset(&s->p_shm);
  return &s->p_shm;
  }

static gavl_sink_status_t put_packet_shm(void * priv, gavl_packet_t * pp)
  {
  gavl_packet_t * p;
  shm_info_t si;
  stream_t * s = priv;

  /* Exchange pointers */
  si.id = bg_shm_get_id(s->shm_segment);
  si.len = s->p_shm.data_len;

  p = gavl_packet_sink_get_packet(s->sink_int);
  
  gavl_packet_copy_metadata(p, &s->p_shm);
  gavl_packet_alloc(p, sizeof(si));
  memcpy(p->data, &si, sizeof(si));
  p->data_len = sizeof(si);
  return gavl_packet_sink_put_packet(s->sink_int, p);
  }

static void check_shm_write(bg_plug_t * p, stream_t * s,
                            gavl_dictionary_t * m,
                            const gavl_compression_info_t * ci)
  {
  /* TODO */
  
  if((p->io_flags & BG_PLUG_IO_IS_LOCAL) &&
     (p->shm_threshold > 0) &&
     (ci->max_packet_size > p->shm_threshold))
    {
    s->shm_size = ci->max_packet_size + GAVL_PACKET_PADDING;
    s->sp = bg_shm_pool_create(s->shm_size, 1);
    gavl_dictionary_set_int(m, META_SHM_SIZE, s->shm_size);
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Using shm for writing (segment size: %d)",
           s->shm_size);
    
    }
  }

static int init_write_common(bg_plug_t * p, stream_t * s)
  {
  s->plug = p;
  s->sink_int = gavf_get_packet_sink(p->g, s->id);
  gavl_packet_sink_set_lock_funcs(s->sink_int, lock_func_write, unlock_func, s);
  
  if(s->shm_size)
    {
    s->sink_s->psink = gavl_packet_sink_create(get_packet_shm,
                                               put_packet_shm,
                                               s);
    s->sink_s->psink_priv = s->sink_s->psink;
    }
  else
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

static void create_sinks(bg_plug_t * p)
  {
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
  }

static int init_write(bg_plug_t * p)
  {
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
        check_shm_write(p, s, s->m, &s->ci);
    
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
        check_shm_write(p, s, s->m, &s->ci);

        if((s->id = gavf_append_video_stream(p->g, &s->ci, s->vfmt, s->m)) < 0)
          return 0;

        break;
      case GAVL_STREAM_TEXT:

        s->sink_s = bg_media_sink_append_text_stream(&p->sink, s->h);

        check_shm_write(p, s, s->m, &s->ci);

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
        check_shm_write(p, s, s->m, &s->ci);

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
  
  return 1;
  }

void bg_plug_transfer_messages(bg_plug_t * in_plug, bg_plug_t * out_plug)
  {
  /* in_plug -> out_plug */
  bg_controllable_connect(bg_plug_get_controllable(in_plug),
                          bg_plug_get_control(out_plug));
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
  if(!p->io)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Cannot start %s plug (call open first)",
           (p->wr ? "output" : "input"));
    return 0;
    }
  
  if(p->wr)
    return init_write(p);
  else
    return init_read(p);
  }


int bg_plug_open(bg_plug_t * p, gavf_io_t * io, int io_flags)
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
          
          /* Start read thread */
          p->io_msg = gavf_io_create_socket(gavf_io_get_socket(p->io), 5000, 0);
          
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
      flags |= (GAVF_OPT_FLAG_SYNC_INDEX | GAVF_OPT_FLAG_PACKET_INDEX);
    else
      flags &= ~(GAVF_OPT_FLAG_SYNC_INDEX | GAVF_OPT_FLAG_PACKET_INDEX);
    
    gavf_options_set_flags(p->opt, flags);
    
    return 1;
    }
  
  /* Read */
  
  flags = gavf_options_get_flags(p->opt);
  flags |= GAVF_OPT_FLAG_BUFFER_READ;
  gavf_options_set_flags(p->opt, flags);
  
  /* Multitrack mode */
  
  if((gavf_io_get_data(p->io, (uint8_t*)buf, 8) == 8) &&
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

    /* Connect backchannel */
    connect_backchannel(p, p->mi);
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

void bg_plug_finish_program(bg_plug_t * p, int discard)
  {
  if(!p->g)
    return;
  
  gavf_close(p->g, discard);
  gavf_io_destroy(p->io);
  p->io = p->io_orig;

  bg_media_sink_cleanup(&p->sink);
  bg_media_sink_init(&p->sink);

  gavl_dictionary_destroy(p->mi_priv);

  p->mi_priv = NULL;
  p->mi = NULL;
  p->cur = NULL;
  
  p->g = NULL;
  p->io_orig = NULL;
  }

int bg_plug_start_program(bg_plug_t * p, const gavl_dictionary_t * m, int discard)
  {
  int ret = 0;

  bg_plug_finish_program(p, discard);

  p->mi_priv = gavl_dictionary_create();
  p->mi = p->mi_priv;
  p->cur = gavl_append_track(p->mi, NULL);
  
  p->g = gavf_create();
  gavf_set_options(p->g, p->opt);
  gavf_set_msg_cb(p->g, msg_cb_write_av, p);
  
  p->io_orig = p->io;
  p->io = gavf_io_create_sub_write(p->io_orig);
  
  if(!gavf_open_write(p->g, p->io, m))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "gavf_open_write failed");
    goto fail;
    }

  ret = 1;

  fail:
  
  return ret;
  }

int bg_plug_open_location(bg_plug_t * p, const char * location1)
  {
  int ret = 0;
  int io_flags = 0;
  gavf_io_t * io;
  gavl_dictionary_t vars;
  char * location = gavl_strdup(location1);

  gavl_dictionary_init(&vars);

  if(location)
    bg_url_get_vars(location, &vars);
  
  io = bg_plug_io_open_location(location,
                                p->wr ? BG_PLUG_IO_METHOD_WRITE :
                                BG_PLUG_IO_METHOD_READ,
                                &io_flags, p->timeout);
  
  if(io)
    {
    if(!bg_plug_open(p, io, io_flags))
      goto fail;
    }
  else
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "bg_plug_open [%s] failed for %s",
           (p->wr ? "write" : "read"), location);
    goto fail;
    }

  if(!p->wr)
    {
    bg_cfg_section_apply(&vars, bg_plug_get_input_parameters(),
                         bg_plug_set_parameter, p);
    
    
    }
  
  ret = 1;
    
  fail:
  
  gavl_dictionary_free(&vars);
  free(location);

  return ret;
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

/* Setup the input side for the media connector. The plug needs to be started already */
int bg_plug_setup_reader(bg_plug_t * p, bg_mediaconnector_t * conn)
  {
  bg_mediaconnector_set_from_source(conn, &p->src);
  return 1;
  }


int bg_plug_got_error(bg_plug_t * p)
  {
  int ret;
  pthread_mutex_lock(&p->mutex);
  ret = p->got_error || gavf_io_got_error(p->io);
  pthread_mutex_unlock(&p->mutex);
  return ret;
  }

gavl_sink_status_t bg_plug_put_packet(bg_plug_t * p,
                                      gavl_packet_t * pkt)
  {
  stream_t * s = find_stream_by_id(p, pkt->id);
  if(!s)
    return GAVL_SINK_ERROR;
  return gavl_packet_sink_put_packet(s->sink_s->psink, pkt);
  }

#if 0
static int index_ext_to_int(bg_plug_t * p, int idx, gavl_stream_type_t type)
  {
  int i;
  int cnt = 0;
  
  for(i = 0; i < p->num_streams; i++)
    {
    if(p->streams[i].type == type)
      {
      if(cnt == idx)
        return i;
      cnt++;
      }
    }
  return -1;
  }
#endif

/* Writable */
static void put_msg(gavl_msg_t * msg, gavl_packet_sink_t * sink)
  {
  gavl_packet_t * pkt = NULL;
  pkt = gavl_packet_sink_get_packet(sink);
  gavf_msg_to_packet(msg, pkt);
  gavl_packet_sink_put_packet(sink, pkt);
  }

static int msg_sink_func_write(void * data, gavl_msg_t * msg)
  {
  bg_plug_t* p = data;
  
  /* Interleave messages with A/V data */
  bg_media_sink_stream_t * s = bg_media_sink_get_stream_by_id(&p->sink, GAVL_META_STREAM_ID_MSG_PROGRAM);
  put_msg(msg, s->psink);
  return 1;
  }

/*
  
 */

int bg_plug_select_track_by_idx(bg_plug_t * p, int track)
  {
  int ret;
  gavl_msg_t msg;
  gavl_msg_init(&msg);
  gavl_msg_set_id_ns(&msg, GAVL_CMD_SRC_SELECT_TRACK, GAVL_MSG_NS_SRC);
  gavl_msg_set_arg_int(&msg, 0, track);
  ret = bg_plug_select_track(p, &msg);
  gavl_msg_free(&msg);
  return ret;
  }

int bg_plug_select_track(bg_plug_t * p, const gavl_msg_t * msg)
  {
  int track = gavl_msg_get_arg_int(msg, 0);

  
  if(has_flag(p, PLUG_FLAG_MULTITRACK_GAVF))
    {
    cleanup_streams(p);
    gavf_select_track(p->g, track);
    p->cur = gavf_get_current_track_nc(p->g);
    }
  else if(has_flag(p, PLUG_FLAG_MULTITRACK_BGPLUG) && p->io_msg)
    {
    gavl_msg_write(msg, p->io_msg);
    
    /* Wait until we are done */

    if(p->g)
      {
      bg_media_source_drain_nolock(&p->src);
      gavf_close(p->g, 0);
      
      gavf_io_destroy(p->io);
      p->io = p->io_orig;
      }

    cleanup_streams(p);
    
    p->g = gavf_create();
    gavf_set_options(p->g, p->opt);
    gavf_set_msg_cb(p->g, msg_cb_read_av, p);

    gavf_io_align_read(p->io);
    
    p->io_orig = p->io;
    p->io = gavf_io_create_sub_read(p->io_orig, gavf_io_position(p->io_orig), 0);

    if(!gavf_open_read(p->g, p->io))
      {
      
      }
    
    gavf_select_track(p->g, 0);
    p->cur = gavf_get_current_track_nc(p->g);
    }
  else
    {
    cleanup_streams(p);
    gavf_select_track(p->g, track);
    p->cur = gavf_get_current_track_nc(p->g);
    }
  
  fprintf(stderr, "Selected track:\n");
  gavl_dictionary_dump(p->cur, 2); 
  
  init_streams_read(p);
  return 1;
  }

int bg_plug_seek(bg_plug_t * p, const gavl_msg_t * msg)
  {
  int64_t time;
  int scale;
  int was_locked;
  
  int flags = gavf_get_flags(p->g);

  time = gavl_msg_get_arg_long(msg, 0);
  scale = gavl_msg_get_arg_int(msg, 1);

  was_locked = plug_was_locked(p, 0);
  
  fprintf(stderr, "bg_plug_seek: %"PRId64" %d %d %d\n", time, scale,
          !!(flags & GAVF_FLAG_STREAMING), was_locked);
  
  if(flags & GAVF_FLAG_STREAMING)
    {
    if(p->nomsg)
      return 0;
    
    gavl_msg_write(msg, p->io_msg);
    
    /* Wait until we are done */
    bg_media_source_drain_nolock(&p->src);
    
    if(has_flag(p, PLUG_FLAG_GOT_RESYNC))
      {
      int i;
      clear_flag(p, PLUG_FLAG_GOT_RESYNC);
      
      /* Empty the queue */
      //            fprintf(stderr, "Drain...\n");
      // bg_media_source_drain(&p->src);
      // fprintf(stderr, "Drain...done\n");

      /* Clear EOF. From now, we should be able to read packets
         from the new position. */
      
      gavf_clear_eof(p->g);
      
      if(p->resync_discard || p->resync_discont)
        {
        /* Flush all codecs and seek to the next sample position */
        for(i = 0; i < p->num_streams; i++)
          {
          bg_codec_plugin_t * codec = NULL;

          if(p->streams[i].codec_handle)
            {
            /* Flush decoder */
            codec = (bg_codec_plugin_t*)p->streams[i].codec_handle->plugin;
            
            if(codec->reset)
              codec->reset(p->streams[i].codec_handle->priv);
            }
          
          if(p->src.streams[i]->asrc)
            {
            gavl_audio_source_reset(p->src.streams[i]->asrc);

            fprintf(stderr, "Skip audio\n");
            
            gavl_audio_source_skip_to(p->src.streams[i]->asrc,
                                      p->resync_time, p->resync_scale);
            fprintf(stderr, "Skip audio done\n");
            }
          
          if(p->src.streams[i]->vsrc)
            {
            gavl_video_source_reset(p->src.streams[i]->vsrc);

            fprintf(stderr, "Skip video\n");
            
            if(codec && codec->skip)
              codec->skip(p->streams[i].codec_handle->priv, p->resync_time, p->resync_scale);
            fprintf(stderr, "Skip video done\n");
            }
          
          }
        }
      }
    else
      {
      fprintf(stderr, "Got no resync\n");
      }
    
    }
  else
    {
    /* TODO: Seek in local files */
    
    }
  fprintf(stderr, "bg_plug_seek done: %"PRId64" %d\n", time, scale);
  return 1;
  }

/* Handle messages *before* they are sent to the backchannel by the reader */

static int msg_sink_func_read(void * data, gavl_msg_t * msg)
  {
  bg_plug_t* p = data;
  
  //  fprintf(stderr, "msg_sink_func_read\n");
  //  gavl_msg_dump(msg, 2);
  
  switch(msg->NS)
    {
    case GAVL_MSG_NS_SRC:
      {
      switch(msg->ID)
        {
        case GAVL_CMD_SRC_SELECT_TRACK:
          bg_plug_select_track(p, msg);
          return 1;
        case GAVL_CMD_SRC_SEEK:
          bg_plug_seek(p, msg);
          return 1;
          break;
        case GAVL_CMD_SRC_START:
          bg_plug_start(p);
          return 1;
          break;
        }
      break;
      
      default:
        break;
      }
    }
  
  if(p->nomsg)
    return 1;
  
  /* Backchannel */
  
  return gavl_msg_write(msg, p->io_msg);
  }

bg_controllable_t * bg_plug_get_controllable(bg_plug_t * p)
  {
  /* Need to be read only */
  if(p->wr)
    {
    gavl_dprintf("BUG: controllable is only available for readable plugs\n");
    return NULL;
    }
  
  return &p->controllable;
  }

bg_control_t * bg_plug_get_control(bg_plug_t * p)
  {
  if(!p->wr)
    {
    gavl_dprintf("BUG: control is only available for writable plugs\n");
    return NULL;
    }
  return &p->control;
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
  BG_PLUG_SCHEMA_UNIX" "\
  BG_PLUG_SCHEMA_UNIXSERV" "\
  BG_PLUG_SCHEMA_TCP" "\
  BG_PLUG_SCHEMA_TCPSERV" "\
  BG_PLUG_SCHEMA;



char const * const
bg_plug_plugin_extensions =
  "gavf";

char const * const
bg_plug_mimetype = "application/x-bgplug";

static const char * plug_plugin_get_protocols(void * priv)
  {
  return bg_plug_plugin_protocols;
  }

static const char * plug_plugin_get_extensions(void * priv)
  {
  return bg_plug_plugin_extensions;
  }

static const char * plug_plugin_get_mimetypes(void * priv)
  {
  return bg_plug_mimetype;
  }

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
  .get_mimetypes = plug_plugin_get_mimetypes,
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
  ret->info = bg_plugin_find_by_name(bg_plugin_reg, "i_bgplug");
  
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
  
  ret->mimetypes = gavl_value_set_array(&ret->mimetypes_val);
  bg_string_to_string_array(bg_plug_mimetype, ret->mimetypes);
  
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

int bg_plug_set_multitrack(bg_plug_t * p, const gavl_dictionary_t * mi_c)
  {
  int i;
  int ret = 0;
  gavf_io_t * bufio;
  gavl_dictionary_t mi_static;
  gavf_chunk_t chunk;
  int num_tracks;
  
  gavl_dictionary_init(&mi_static);
  gavl_dictionary_copy(&mi_static, mi_c);
  mi_c = &mi_static;
  
  num_tracks = gavl_get_num_tracks(mi_c);

  for(i = 0; i < num_tracks; i++)
    {
    gavl_dictionary_t * track;
    if((track = gavl_get_track_nc(&mi_static, i)))
      gavl_track_delete_implicit_fields(track);
    }
  
  bufio = gavf_chunk_start_io(p->io, &chunk, BG_PLUG_MULTI_HEADER);
  
  /* Write metadata */
  if(!gavl_dictionary_write(bufio, mi_c))
    goto fail;
  
  ret = 1;
  
  set_flag(p, PLUG_FLAG_MULTITRACK_BGPLUG);
  
  fail:
  
  /* size */
  
  gavf_chunk_finish_io(p->io, &chunk, bufio);
  gavl_dictionary_free(&mi_static);

  if(ret && !accept_backchannel_socket(p))
    ret = 0;
  
  return ret;
  
  }

void bg_plug_write_resync(bg_plug_t * plug,
                          int64_t time, int scale, int discard, int discont)
  {
  if(!plug->wr)
    return;
  
  /* TODO: Reset on-the-fly encoders */
  
  gavf_write_resync(plug->g, time, scale, discard, discont);
  }
