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
#include <time.h>
#include <ctype.h>


#include <gmerlin/utils.h> 
#include <gmerlin/translation.h> 

#include <gmerlin/recorder.h>
#include <recorder_private.h>
#include <gmerlin/subprocess.h>
#include <gmerlin/log.h>

#define LOG_DOMAIN "recorder"

const uint32_t bg_recorder_stream_mask =
  GAVL_STREAM_AUDIO | GAVL_STREAM_VIDEO;

const uint32_t bg_recorder_plugin_mask = BG_PLUGIN_FILE | BG_PLUGIN_BROADCAST;

/* Handle commands */
static int handle_command(void * priv, gavl_msg_t * msg)
  {
  bg_recorder_t * r = priv;
  switch(msg->NS)
    {
    case BG_MSG_NS_PARAMETER:
      switch(msg->ID)
        {
        case BG_MSG_SET_CHAIN_PARAMETER_CTX:
          {
          const char * ctx;
          const char * name;

          fprintf(stderr, "set chain 1\n");
          
          if((ctx = gavl_msg_get_arg_string_c(msg, 0)) &&
             (name = gavl_msg_get_arg_string_c(msg, 1)))
            {
            fprintf(stderr, "set chain 2: %s %s\n", ctx, name);
            
            if(!strcmp(ctx, "audiofilter") && !strcmp(name, "audio_filters"))
              bg_recorder_handle_audio_filter_command(r, msg);
            else if(!strcmp(ctx, "videofilter") && !strcmp(name, "video_filters"))
              bg_recorder_handle_video_filter_command(r, msg);
            else
              return 1;
            }
          }
          break;
        default:
          break;
        }
    default:
      break;
    }
  
  return 1;
  }

static void init_cfg(bg_recorder_t * ret)
  {
  bg_cfg_ctx_init(&ret->cfg[BG_RECORDER_CFG_AUDIO],
                  bg_recorder_get_audio_parameters(ret),
                  "audio",
                  TR("Audio"),
                  bg_recorder_set_audio_parameter,
                  ret);
  bg_cfg_ctx_init(&ret->cfg[BG_RECORDER_CFG_AUDIOFILTER],
                  bg_recorder_get_audio_filter_parameters(ret),
                  "audiofilter",
                  TR("Audio filter"),
                  bg_recorder_set_audio_filter_parameter,
                  ret);
  bg_cfg_ctx_init(&ret->cfg[BG_RECORDER_CFG_VIDEO],
                  bg_recorder_get_video_parameters(ret),
                  "video",
                  TR("Video"),
                  bg_recorder_set_video_parameter,
                  ret);
  bg_cfg_ctx_init(&ret->cfg[BG_RECORDER_CFG_VIDEOFILTER],
                  bg_recorder_get_video_filter_parameters(ret),
                  "videofilter",
                  TR("Video filter"),
                  bg_recorder_set_video_filter_parameter,
                  ret);

  bg_cfg_ctx_init(&ret->cfg[BG_RECORDER_CFG_MONITOR],
                  bg_recorder_get_video_monitor_parameters(ret),
                  "monitor",
                  TR("Monitor"),
                  bg_recorder_set_video_monitor_parameter,
                  ret);

  bg_cfg_ctx_init(&ret->cfg[BG_RECORDER_CFG_OUTPUT],
                  bg_recorder_get_output_parameters(ret),
                  "output",
                  TR("Output"),
                  bg_recorder_set_output_parameter,
                  ret);

  bg_cfg_ctx_init(&ret->cfg[BG_RECORDER_CFG_ENCODERS],
                  bg_recorder_get_encoder_parameters(ret),
                  "encoder",
                  TR("Encoder"),
                   NULL,
                  ret);

  bg_cfg_ctx_init(&ret->cfg[BG_RECORDER_CFG_SNAPSHOT],
                  bg_recorder_get_video_snapshot_parameters(ret),
                  "snapshot",
                  TR("Snapshot"),
                  bg_recorder_set_video_snapshot_parameter,
                  ret);
  
  
  }

bg_recorder_t * bg_recorder_create(bg_plugin_registry_t * plugin_reg)
  {
  bg_recorder_t * ret = calloc(1, sizeof(*ret));
  
  bg_plugin_registry_scan_devices(plugin_reg,
                                  BG_PLUGIN_RECORDER_AUDIO,
                                  0);

  bg_plugin_registry_scan_devices(plugin_reg,
                                  BG_PLUGIN_RECORDER_VIDEO,
                                  0);

  
  ret->plugin_reg = plugin_reg;
  ret->tc = bg_thread_common_create();
  
  bg_recorder_create_audio(ret);
  bg_recorder_create_video(ret);

  ret->th[0] = ret->as.th;
  ret->th[1] = ret->vs.th;

  pthread_mutex_init(&ret->time_mutex, NULL);
  pthread_mutex_init(&ret->snapshot_mutex, NULL);
  
  bg_controllable_init(&ret->ctrl,   
                       bg_msg_sink_create(handle_command, ret, 0),
                       bg_msg_hub_create(1));

  init_cfg(ret);
  
  return ret;
  }

bg_controllable_t * bg_recorder_get_controllable(bg_recorder_t * rec)
  {
  return &rec->ctrl;
  }

void bg_recorder_destroy(bg_recorder_t * rec)
  {
  if(rec->flags & FLAG_RUNNING)
    bg_recorder_stop(rec);

  bg_recorder_destroy_audio(rec);
  bg_recorder_destroy_video(rec);

  bg_thread_common_destroy(rec->tc);

  free(rec->display_string);
  
  if(rec->encoder_parameters)
    bg_parameter_info_destroy_array(rec->encoder_parameters);

  if(rec->output_directory)       free(rec->output_directory);
  if(rec->output_filename_mask)   free(rec->output_filename_mask);
  if(rec->snapshot_directory)     free(rec->snapshot_directory);
  if(rec->snapshot_filename_mask) free(rec->snapshot_filename_mask);

  gavl_dictionary_free(&rec->m);
  
  pthread_mutex_destroy(&rec->time_mutex);
  pthread_mutex_destroy(&rec->snapshot_mutex);
  
  free(rec);
  }

static void init_encoding(bg_recorder_t * rec)
  {
  struct tm brokentime;
  time_t t;
  char time_string[512];
  char * filename_base;
  const gavl_dictionary_t * m;
  
  time(&t);
  localtime_r(&t, &brokentime);
  strftime(time_string, 511, rec->output_filename_mask, &brokentime);

  filename_base =
    bg_sprintf("%s/%s", rec->output_directory, time_string);
  
  rec->as.flags |= STREAM_ENCODE;
  rec->vs.flags |= STREAM_ENCODE;
  
  rec->enc = bg_encoder_create(rec->encoder_section,
                               NULL,
                               bg_recorder_stream_mask,
                               bg_recorder_plugin_mask);

  m = &rec->m;
  
  bg_encoder_open(rec->enc, filename_base, m);
  free(filename_base);
  }

static int finalize_encoding(bg_recorder_t * rec)
  {
  if(!bg_encoder_start(rec->enc))
    return 0;
  
  if(rec->as.flags & STREAM_ACTIVE)
    bg_recorder_audio_finalize_encode(rec);
  if(rec->vs.flags & STREAM_ACTIVE)
    bg_recorder_video_finalize_encode(rec);
  
  rec->encoding_finalized = 1;
  return 1;
  }

int bg_recorder_run(bg_recorder_t * rec)
  {
  int do_audio = 0;
  int do_video = 0;

  rec->encoding_finalized = 0;
  
  if(rec->flags & FLAG_DO_RECORD)
    {
    init_encoding(rec);
    rec->recording_time = 0;
    rec->last_recording_time = - 2 * GAVL_TIME_SCALE;
    }
  else
    {
    rec->as.flags &= ~STREAM_ENCODE;
    rec->vs.flags &= ~STREAM_ENCODE;
    }
  
  if(rec->as.flags & STREAM_ACTIVE)
    {
    if(!bg_recorder_audio_init(rec))
      rec->as.flags &= ~STREAM_ACTIVE;
    else
      do_audio = 1;
    }

  bg_recorder_audio_set_eof(&rec->as, !do_audio);
  
  if(rec->vs.flags & STREAM_ACTIVE)
    {
    if(!bg_recorder_video_init(rec))
      rec->vs.flags &= ~STREAM_ACTIVE;
    else
      do_video = 1;
    }

  bg_recorder_video_set_eof(&rec->vs, !do_video);
  
  if(rec->flags & FLAG_DO_RECORD)
    {
    if(!finalize_encoding(rec))
      {
      if(rec->as.flags & STREAM_ACTIVE)
        bg_recorder_audio_cleanup(rec);
      if(rec->vs.flags & STREAM_ACTIVE)
        bg_recorder_video_cleanup(rec);

      bg_recorder_msg_running(rec, 0, 0);
      return 0;
      }
    }
  
  if(rec->as.flags & STREAM_ACTIVE)
    bg_thread_set_func(rec->as.th, bg_recorder_audio_thread, rec);
  else
    bg_thread_set_func(rec->as.th, NULL, NULL);

  if(rec->vs.flags & STREAM_ACTIVE)
    bg_thread_set_func(rec->vs.th, bg_recorder_video_thread, rec);
  else
    bg_thread_set_func(rec->vs.th, NULL, NULL);
  
  if(rec->flags & FLAG_DO_RECORD)
    rec->flags &= FLAG_RECORDING;
  
  bg_threads_init(rec->th, NUM_THREADS);
  bg_threads_start(rec->th, NUM_THREADS);
  
  rec->flags |= FLAG_RUNNING;

  bg_recorder_msg_running(rec,
                          do_audio, do_video);
  
  return 1;
  }

/* Encoders */

const bg_parameter_info_t *
bg_recorder_get_encoder_parameters(bg_recorder_t * rec)
  {
  if(!rec->encoder_parameters)
    rec->encoder_parameters =
      bg_plugin_registry_create_encoder_parameters(rec->plugin_reg,
                                                   bg_recorder_stream_mask,
                                                   bg_recorder_plugin_mask, 1);
  return rec->encoder_parameters;
  }

void bg_recorder_set_encoder_section(bg_recorder_t * rec,
                                     bg_cfg_section_t * s)
  {
  rec->encoder_section = s;
  }

void bg_recorder_stop(bg_recorder_t * rec)
  {
  if(!(rec->flags & FLAG_RUNNING))
    return;
  bg_threads_join(rec->th, NUM_THREADS);
  bg_recorder_audio_cleanup(rec);
  bg_recorder_video_cleanup(rec);

  if(rec->enc)
    {
    bg_encoder_destroy(rec->enc, 0);
    rec->enc = NULL;
    bg_recorder_msg_time(rec,
                         GAVL_TIME_UNDEFINED);
    }
  rec->flags &= ~(FLAG_RECORDING | FLAG_RUNNING);
  }

void bg_recorder_record(bg_recorder_t * rec, int record)
  {
  int was_running = !!(rec->flags & FLAG_RUNNING);

  if(was_running)
    bg_recorder_stop(rec);

  if(record)
    rec->flags |= FLAG_DO_RECORD;
  else
    rec->flags &= ~FLAG_DO_RECORD;

  if(was_running)
    bg_recorder_run(rec);
  }

void bg_recorder_set_display_string(bg_recorder_t * rec, const char * str)
  {
  rec->display_string = gavl_strrep(rec->display_string, str);
  }

/* Message stuff */

static void msg_framerate(gavl_msg_t * msg,
                          const void * data)
  {
  const float * f = data;
  gavl_msg_set_id_ns(msg, BG_RECORDER_MSG_FRAMERATE, BG_MSG_NS_RECORDER);
  gavl_msg_set_arg_float(msg, 0, *f);
  }
                       
void bg_recorder_msg_framerate(bg_recorder_t * rec,
                               float framerate)
  {
  bg_msg_hub_send_cb(rec->ctrl.evt_hub, msg_framerate, &framerate);
  }

typedef struct
  {
  double * l;
  int samples;
  } audiolevel_t;

static void msg_audiolevel(gavl_msg_t * msg,
                           const void * data)
  {
  const audiolevel_t * d = data;
  
  gavl_msg_set_id_ns(msg, BG_RECORDER_MSG_AUDIOLEVEL, BG_MSG_NS_RECORDER);
  gavl_msg_set_arg_float(msg, 0, d->l[0]);
  gavl_msg_set_arg_float(msg, 1, d->l[1]);
  gavl_msg_set_arg_int(msg, 2, d->samples);
  }

void bg_recorder_msg_audiolevel(bg_recorder_t * rec,
                                double * level, int samples)
  {
  audiolevel_t d;
  d.l = level;
  d.samples = samples;

  bg_msg_hub_send_cb(rec->ctrl.evt_hub, msg_audiolevel, &d);
  }

static void msg_time(gavl_msg_t * msg,
                     const void * data)
  {
  const gavl_time_t * t = data;
  gavl_msg_set_id_ns(msg, BG_RECORDER_MSG_TIME, BG_MSG_NS_RECORDER);
  gavl_msg_set_arg_long(msg, 0, *t);
  }
                       
void bg_recorder_msg_time(bg_recorder_t * rec,
                          gavl_time_t t)
  {
  bg_msg_hub_send_cb(rec->ctrl.evt_hub, msg_time, &t);
  }

typedef struct
  {
  int do_audio;
  int do_video;
  } running_t;

static void msg_running(gavl_msg_t * msg,
                        const void * data)
  {
  const running_t * r = data;
  gavl_msg_set_id_ns(msg, BG_RECORDER_MSG_RUNNING, BG_MSG_NS_RECORDER);
  gavl_msg_set_arg_int(msg, 0, r->do_audio);
  gavl_msg_set_arg_int(msg, 1, r->do_video);
  }

void bg_recorder_msg_running(bg_recorder_t * rec,
                             int do_audio, int do_video)
  {
  running_t r;
  r.do_audio = do_audio;
  r.do_video = do_video;
  
  bg_msg_hub_send_cb(rec->ctrl.evt_hub, msg_running, &r);
  }


/* Parameter stuff */

static const bg_parameter_info_t output_parameters[] =
  {
    {
      .name      = "output_directory",
      .long_name = TRS("Output directory"),
      .type      = BG_PARAMETER_DIRECTORY,
      .val_default = GAVL_VALUE_INIT_STRING("."),
    },
    {
      .name      = "output_filename_mask",
      .long_name = TRS("Output filename mask"),
      .type      = BG_PARAMETER_STRING,
      .val_default = GAVL_VALUE_INIT_STRING("%Y-%m-%d-%H-%M-%S"),
      .help_string = TRS("Extension is appended by the plugin\n\
For the date and time formatting, consult the documentation\n\
of the strftime(3) function"),
    },
    {
      .name      = "snapshot_directory",
      .long_name = TRS("Snapshot directory"),
      .type      = BG_PARAMETER_DIRECTORY,
      .val_default = GAVL_VALUE_INIT_STRING("."),
    },
    {
      .name      = "snapshot_filename_mask",
      .long_name = TRS("Snapshot filename mask"),
      .type      = BG_PARAMETER_STRING,
      .val_default = GAVL_VALUE_INIT_STRING("shot_%5n"),
      .help_string = TRS("Extension is appended by the plugin\n\
%t    Inserts time\n\
%d    Inserts date\n\
%<i>n Inserts Frame number with <i> digits")
    },
    { /* End */ }
  };

const bg_parameter_info_t *
bg_recorder_get_output_parameters(bg_recorder_t * rec)
  {
  return output_parameters;
  }

void
bg_recorder_set_output_parameter(void * data,
                                 const char * name,
                                 const gavl_value_t * val)
  {
  bg_recorder_t * rec;
  if(!name)
    return;

  rec = data;
  
  if(!strcmp(name, "output_directory"))
    rec->output_directory = gavl_strrep(rec->output_directory, val->v.str);
  else if(!strcmp(name, "output_filename_mask"))
    rec->output_filename_mask =
      gavl_strrep(rec->output_filename_mask, val->v.str);
  else if(!strcmp(name, "snapshot_directory"))
    rec->snapshot_directory =
      gavl_strrep(rec->snapshot_directory, val->v.str);
  else if(!strcmp(name, "snapshot_filename_mask"))
    rec->snapshot_filename_mask =
      gavl_strrep(rec->snapshot_filename_mask, val->v.str);
  }


const bg_parameter_info_t *
bg_recorder_get_metadata_parameters(bg_recorder_t * rec)
  {
  if(!rec->metadata_parameters)
    rec->metadata_parameters = bg_metadata_get_parameters(&rec->m);
  return rec->metadata_parameters;
  }

void
bg_recorder_set_metadata_parameter(void * data,
                                   const char * name,
                                   const gavl_value_t * val)
  {
  bg_recorder_t * rec = data;
  bg_metadata_set_parameter(&rec->m, name, val);
  }

void bg_recorder_update_time(bg_recorder_t * rec, gavl_time_t t)
  {
  pthread_mutex_lock(&rec->time_mutex);

  if(rec->recording_time < t)
    rec->recording_time = t;

  if(rec->recording_time - rec->last_recording_time >
     GAVL_TIME_SCALE)
    {
    bg_recorder_msg_time(rec,
                         rec->recording_time);
    rec->last_recording_time = rec->recording_time;
    }
  pthread_mutex_unlock(&rec->time_mutex);
  }

void bg_recorder_interrupt(bg_recorder_t * rec)
  {
  if(rec->flags & FLAG_RUNNING)
    {
    bg_recorder_stop(rec);
    rec->flags |= FLAG_INTERRUPTED;
    }
  }

void bg_recorder_resume(bg_recorder_t * rec)
  {
  if(rec->flags & FLAG_INTERRUPTED)
    {
    rec->flags &= ~FLAG_INTERRUPTED;
    bg_recorder_run(rec);
    }
  }

void bg_recorder_snapshot(bg_recorder_t * rec)
  {
  pthread_mutex_lock(&rec->snapshot_mutex);
  rec->snapshot = 1;
  pthread_mutex_unlock(&rec->snapshot_mutex);
  //  fprintf(stderr, "Snapshot\n");
  }


#define CHECK_STRING(key, val) \
  len = strlen(key); \
  if(!strncmp(key, line, len)) \
    val = gavl_strrep(val, line + len)



int bg_recorder_ping(bg_recorder_t * rec)
  {
  bg_msg_sink_iteration(rec->ctrl.cmd_sink);
  
  if(bg_recorder_video_get_eof(&rec->vs) &&
     bg_recorder_audio_get_eof(&rec->as))
    return 0;
  return 1;
  }

bg_cfg_ctx_t * bg_recorder_get_cfg(bg_recorder_t * rec)
  {
  return rec->cfg;
  }
