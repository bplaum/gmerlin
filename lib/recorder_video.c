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
#include <string.h>
#include <ctype.h>
#include <unistd.h> // access()

#include <gmerlin/translation.h>
#include <gmerlin/utils.h>

#include <gmerlin/recorder.h>
#include <recorder_private.h>
#include <gmerlin/state.h>

#include <gmerlin/log.h>
#define LOG_DOMAIN "recorder.video"

#define FRAMERATE_INTERVAL 10

#include <gavl/metatags.h>

static int create_snapshot_cb(void * data, const char * filename)
  {
  bg_recorder_t * rec = data;
  bg_recorder_video_stream_t * vs = &rec->vs;

  int overwrite;

  pthread_mutex_lock(&rec->snapshot_mutex);
  overwrite = vs->flags & STREAM_SNAPSHOT_OVERWRITE;
  pthread_mutex_unlock(&rec->snapshot_mutex);
  
  if(!overwrite && !access(filename, R_OK))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Won't save snapshot %s (file exists)",
           filename);
    return 0;
    }
  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Saving snapshot %s", filename);
  return 1;
  }


void bg_recorder_create_video(bg_recorder_t * rec)
  {
  bg_recorder_video_stream_t * vs = &rec->vs;
  
  bg_gavl_video_options_init(&vs->opt);
  
  vs->fc = bg_video_filter_chain_create(&vs->opt, rec->plugin_reg);

  vs->th = bg_thread_create(rec->tc);
  vs->timer = gavl_timer_create();

  pthread_mutex_init(&vs->config_mutex, NULL);

  vs->snapshot_cb.create_output_file = create_snapshot_cb;
  vs->snapshot_cb.data = rec;

  pthread_mutex_init(&vs->eof_mutex, NULL);
  }

void bg_recorder_video_set_eof(bg_recorder_video_stream_t * s, int eof)
  {
  pthread_mutex_lock(&s->eof_mutex);
  s->eof = eof;
  pthread_mutex_unlock(&s->eof_mutex);
  }

int  bg_recorder_video_get_eof(bg_recorder_video_stream_t * s)
  {
  int ret;
  pthread_mutex_lock(&s->eof_mutex);
  ret = s->eof;
  pthread_mutex_unlock(&s->eof_mutex);
  return ret;
  }


void bg_recorder_destroy_video(bg_recorder_t * rec)
  {
  bg_recorder_video_stream_t * vs = &rec->vs;
  
  
  bg_video_filter_chain_destroy(vs->fc);
  bg_thread_destroy(vs->th);
  gavl_timer_destroy(vs->timer);
  pthread_mutex_destroy(&vs->config_mutex);

  if(vs->monitor_handle)
    bg_plugin_unref(vs->monitor_handle);
  if(vs->input_handle)
    bg_plugin_unref(vs->input_handle);
  if(vs->snapshot_handle)
    bg_plugin_unref(vs->snapshot_handle);

  if(vs->snapshot_cnv)
    gavl_video_converter_destroy(vs->snapshot_cnv);
  
  bg_gavl_video_options_free(&vs->opt);
  pthread_mutex_destroy(&vs->eof_mutex);
  }

static const bg_parameter_info_t parameters[] =
  {
    {
      .name = "do_video",
      .long_name = TRS("Record video"),
      .type = BG_PARAMETER_CHECKBUTTON,
      .val_default = GAVL_VALUE_INIT_INT(1),
    },
    {
      .name      = "plugin",
      .long_name = TRS("Plugin"),
      .type      = BG_PARAMETER_MULTI_MENU,
      .flags     = BG_PARAMETER_PLUGIN,
    },
    { },
  };

const bg_parameter_info_t *
bg_recorder_get_video_parameters(bg_recorder_t * rec)
  {
  bg_recorder_video_stream_t * vs = &rec->vs;
  if(!vs->parameters)
    {
    vs->parameters = bg_parameter_info_copy_array(parameters);
    
    bg_plugin_registry_set_parameter_info(rec->plugin_reg,
                                          BG_PLUGIN_RECORDER_VIDEO,
                                          0,
                                          &vs->parameters[1]);
    }
  
  return vs->parameters;
  }


void
bg_recorder_set_video_parameter(void * data,
                                const char * name,
                                const gavl_value_t * val)
  {
  bg_recorder_t * rec = data;
  bg_recorder_video_stream_t * vs = &rec->vs;
  
  if(!name)
    return;
  
  //  if(name)
  //    fprintf(stderr, "bg_recorder_set_video_parameter %s\n", name);

  if(!strcmp(name, "do_video"))
    {
    if((rec->flags & FLAG_RUNNING) &&
       (!!(vs->flags & STREAM_ACTIVE) != val->v.i))
      bg_recorder_interrupt(rec);

    if(val->v.i)
      vs->flags |= STREAM_ACTIVE;
    else
      vs->flags &= ~STREAM_ACTIVE;

    }
  else if(!strcmp(name, "plugin"))
    {
    const char * plugin_name;
    
    plugin_name = bg_multi_menu_get_selected_name(val);
    
    if(!vs->input_handle ||
       strcmp(vs->input_handle->info->name, plugin_name))
      {
      if(rec->flags & FLAG_RUNNING)
        bg_recorder_interrupt(rec);
    
      if(vs->input_handle)
        bg_plugin_unref(vs->input_handle);
      
      if((vs->input_handle = bg_plugin_load_with_options(rec->plugin_reg,
                                                         bg_multi_menu_get_selected(val))))
        vs->input_plugin = (bg_recorder_plugin_t*)vs->input_handle->plugin;
      }
    
    }
  
  }

/* Monitor */

static const bg_parameter_info_t monitor_parameters[] =
  {
    {
      .name = "do_monitor",
      .long_name = TRS("Enable monitor"),
      .type = BG_PARAMETER_CHECKBUTTON,
      .val_default = GAVL_VALUE_INIT_INT(1),
    },
    {
      .name      = "plugin",
      .long_name = TRS("Plugin"),
      .type      = BG_PARAMETER_MULTI_MENU,
      .flags     = BG_PARAMETER_PLUGIN,
    },
    { },
  };

const bg_parameter_info_t *
bg_recorder_get_video_monitor_parameters(bg_recorder_t * rec)
  {
  bg_recorder_video_stream_t * vs = &rec->vs;
  if(!vs->monitor_parameters)
    {
    vs->monitor_parameters = bg_parameter_info_copy_array(monitor_parameters);
    
    bg_plugin_registry_set_parameter_info(rec->plugin_reg,
                                          BG_PLUGIN_OUTPUT_VIDEO,
                                          BG_PLUGIN_PLAYBACK,
                                          &vs->monitor_parameters[1]);
    }
  
  return vs->monitor_parameters;
  }

void
bg_recorder_set_video_monitor_parameter(void * data,
                                        const char * name,
                                        const gavl_value_t * val)
  {
  bg_recorder_t * rec = data;
  bg_recorder_video_stream_t * vs = &rec->vs;
  
  if(!name)
    return;
  
  //  if(name)
  //    fprintf(stderr, "bg_recorder_set_video_monitor_parameter %s\n", name);

  if(!strcmp(name, "do_monitor"))
    {
    if(!!(vs->flags & STREAM_MONITOR) != val->v.i)
      bg_recorder_interrupt(rec);

    if(val->v.i)
      vs->flags |= STREAM_MONITOR;
    else
      vs->flags &= ~STREAM_MONITOR;
    }
  else if(!strcmp(name, "plugin"))
    {
    //    const bg_plugin_info_t * info;
    const char * plugin_name;
    const gavl_dictionary_t * sel;
    
    sel = bg_multi_menu_get_selected(val);
    plugin_name = bg_multi_menu_get_selected_name(val);
    
    if(!vs->monitor_handle ||
       strcmp(vs->monitor_handle->info->name, plugin_name))
      {
      bg_recorder_interrupt(rec);
    
      if(vs->monitor_handle)
        bg_plugin_unref(vs->monitor_handle);

      //      info = bg_plugin_find_by_name(rec->plugin_reg, plugin_name);

      fprintf(stderr, "Opening monitor plugin %s\n", rec->display_string);
      
      if((vs->monitor_handle = bg_ov_plugin_load(rec->plugin_reg,
                                                 sel,
                                                 rec->display_string)))
        vs->monitor_plugin = (bg_ov_plugin_t*)vs->monitor_handle->plugin;
      else
        vs->monitor_plugin = NULL;
      
      if(vs->monitor_plugin && vs->monitor_plugin->common.get_controllable)
        {
        bg_controllable_t * ctrl;
        ctrl =
          vs->monitor_plugin->common.get_controllable(vs->monitor_handle->priv);
        bg_msg_hub_connect_sink(ctrl->evt_hub, rec->ctrl.evt_sink);
        }
      }
    
    if(vs->monitor_handle && vs->monitor_plugin->common.set_parameter)
      {
      bg_cfg_section_apply(bg_multi_menu_get_selected(val),
                           NULL,
                           vs->monitor_plugin->common.set_parameter,
                           vs->monitor_handle->priv);
      }
    }
  }

static const bg_parameter_info_t snapshot_parameters[] =
  {
    {
      .name = "snapshot_auto",
      .long_name = TRS("Automatic"),
      .type = BG_PARAMETER_CHECKBUTTON,
    },
    {
      .name = "snapshot_interval",
      .long_name = TRS("Snapshot interval"),
      .type = BG_PARAMETER_FLOAT,
      .val_default = GAVL_VALUE_INIT_FLOAT(5.0),
      .val_min     = GAVL_VALUE_INIT_FLOAT(0.5),
      .val_max     = GAVL_VALUE_INIT_FLOAT(10000.0),
      .num_digits  = 1,
    },
    {
      .name = "snapshot_overwrite",
      .long_name = TRS("Overwrite existing files"),
      .type = BG_PARAMETER_CHECKBUTTON,
    },
    {
      .name      = "plugin",
      .long_name = TRS("Plugin"),
      .type      = BG_PARAMETER_MULTI_MENU,
    },
    { /* End */ }
  };

const bg_parameter_info_t *
bg_recorder_get_video_snapshot_parameters(bg_recorder_t * rec)
  {
  bg_recorder_video_stream_t * vs = &rec->vs;
  if(!vs->snapshot_parameters)
    {
    vs->snapshot_parameters = bg_parameter_info_copy_array(snapshot_parameters);
    bg_plugin_registry_set_parameter_info(rec->plugin_reg,
                                          BG_PLUGIN_IMAGE_WRITER,
                                          BG_PLUGIN_FILE,
                                          &vs->snapshot_parameters[3]);
    }
  return vs->snapshot_parameters;
  }

void
bg_recorder_set_video_snapshot_parameter(void * data,
                                         const char * name,
                                         const gavl_value_t * val)
  {
  bg_recorder_t * rec;
  bg_recorder_video_stream_t * vs;

  if(!name)
    return;

  rec = data;
  vs = &rec->vs;
  
  if(!strcmp(name, "snapshot_auto"))
    {
    pthread_mutex_lock(&rec->snapshot_mutex);
    if(val->v.i)
      vs->flags |= STREAM_SNAPSHOT_AUTO;
    else
      vs->flags &= ~STREAM_SNAPSHOT_AUTO;
    pthread_mutex_unlock(&rec->snapshot_mutex);
    }
  else if(!strcmp(name, "snapshot_overwrite"))
    {
    if(val->v.i)
      vs->flags |= STREAM_SNAPSHOT_OVERWRITE;
    else
      vs->flags &= ~STREAM_SNAPSHOT_OVERWRITE;
    }
  else if(!strcmp(name, "snapshot_interval"))
    vs->snapshot_interval = gavl_seconds_to_time(val->v.d);
  else if( !strcmp(name, "plugin"))
    {
    const char * plugin_name;

    plugin_name = bg_multi_menu_get_selected_name(val);
    
    if(!vs->snapshot_handle ||
       strcmp(vs->snapshot_handle->info->name, plugin_name))
      {
      bg_recorder_interrupt(rec);
    
      if(vs->snapshot_handle)
        bg_plugin_unref(vs->snapshot_handle);

      if((vs->snapshot_handle = bg_plugin_load_with_options(rec->plugin_reg,
                                                            bg_multi_menu_get_selected(val))))
        {
        vs->snapshot_plugin = (bg_image_writer_plugin_t*)vs->snapshot_handle->plugin;
      
        if(vs->snapshot_plugin->set_callbacks)
          vs->snapshot_plugin->set_callbacks(vs->snapshot_handle->priv,
                                             &vs->snapshot_cb);
        }
      
      }

    
    }
  }


const bg_parameter_info_t *
bg_recorder_get_video_filter_parameters(bg_recorder_t * rec)
  {
  bg_recorder_video_stream_t * vs = &rec->vs;
  return bg_video_filter_chain_get_parameters(vs->fc);
  }

void
bg_recorder_set_video_filter_parameter(void * data,
                                       const char * name,
                                       const gavl_value_t * val)
  {
  int need_restart;
  
  bg_recorder_t * rec = data;
  bg_recorder_video_stream_t * vs = &rec->vs;

  if(!name)
    {
    bg_recorder_resume(rec);
    return;
    }

  bg_video_filter_chain_lock(vs->fc);
  bg_video_filter_chain_set_parameter(vs->fc, name, val);
  
  if(bg_video_filter_chain_need_restart(vs->fc))
    need_restart = 1;
  else
    need_restart = 0;

  bg_video_filter_chain_unlock(vs->fc);
  
  if(need_restart)
    bg_recorder_interrupt(rec);
  }

#define BUFFER_SIZE 256

static char * create_snapshot_filename(bg_recorder_t * rec, int * have_count)
  {
  char mask[16];
  char buf[BUFFER_SIZE];
  char * pos;
  char * end;
  char * filename;
  int have_time = 0;
  time_t t;
  struct tm time_date;
  
  filename = bg_sprintf("%s/", rec->snapshot_directory);
  
  pos = rec->snapshot_filename_mask;

  if(have_count)
    *have_count = 0;
  
  while(1)
    {
    end = pos;
    
    while((*end != '%') && (*end != '\0'))
      {
      end++;
      }
    
    if(end - pos)
      filename = gavl_strncat(filename, pos, end);
    
    if(*end == '\0')
      break;

    pos = end;
    
    /* Insert frame count */
    if(isdigit(pos[1]) && (pos[2] == 'n'))
      {
      mask[0] = '%';
      mask[1] = '0';
      mask[2] = pos[1];
      mask[3] = 'd';
      mask[4] = '\0';
      sprintf(buf, mask, rec->vs.snapshot_counter);
      filename = gavl_strcat(filename, buf);
      pos += 3;

      if(have_count)
        *have_count = 1;
      }
    /* Insert date */
    else if(pos[1] == 'd')
      {
      if(!have_time)
        {
        time(&t);
        localtime_r(&t, &time_date);
        have_time = 1;
        }
      strftime(buf, BUFFER_SIZE, "%Y-%m-%d", &time_date);
      filename = gavl_strcat(filename, buf);
      pos += 2;
      }
    /* Insert date */
    else if(pos[1] == 't')
      {
      if(!have_time)
        {
        time(&t);
        localtime_r(&t, &time_date);
        have_time = 1;
        }
      strftime(buf, BUFFER_SIZE, "%H-%M-%S", &time_date);
      filename = gavl_strcat(filename, buf);
      pos += 2;
      }
    else
      {
      filename = gavl_strcat(filename, "%");
      pos++;
      }
    }
  return filename;
  }

static void check_snapshot(void * data, gavl_video_frame_t * frame)
  {
  int doit = 0;
  char * filename;
  gavl_time_t frame_time;

  bg_recorder_t * rec = data;
  
  bg_recorder_video_stream_t * vs = &rec->vs;

  frame_time =
    gavl_time_unscale(vs->process_format->timescale,
                      frame->timestamp);
  
  /* Check whether to make a snapshot */

  pthread_mutex_lock(&rec->snapshot_mutex);
  if(rec->snapshot)
    {
    doit = 1;
    rec->snapshot = 0;
    }
  
  if(!doit &&
     ((vs->flags & STREAM_SNAPSHOT_AUTO) &&
      (!(vs->flags & STREAM_SNAPSHOT_INIT) ||
       frame_time >= vs->last_snapshot_time + vs->snapshot_interval)))
    {
    doit = 1;
    }
  pthread_mutex_unlock(&rec->snapshot_mutex);
  
  if(!doit)
    return;
  
  filename = create_snapshot_filename(rec, NULL);
  
  /* Initialize snapshot plugin */
  if(!(vs->flags & STREAM_SNAPSHOT_INIT))
    gavl_video_format_copy(&vs->snapshot_format,
                           vs->process_format);

  if(!vs->snapshot_plugin->write_header(vs->snapshot_handle->priv,
                                        filename,
                                        &vs->snapshot_format,
                                        &rec->m))
    return;
  
  if(!(vs->flags & STREAM_SNAPSHOT_INIT))
    {
    vs->do_convert_snapshot =
      gavl_video_converter_init(vs->snapshot_cnv,
                                vs->process_format,
                                &vs->snapshot_format);

    if(vs->do_convert_snapshot)
      vs->snapshot_frame = gavl_video_frame_create(&vs->snapshot_format);
    vs->flags |= STREAM_SNAPSHOT_INIT;
    }

  if(vs->do_convert_snapshot)
    {
    gavl_video_convert(vs->snapshot_cnv, frame, vs->snapshot_frame);
    vs->snapshot_plugin->write_image(vs->snapshot_handle->priv, vs->snapshot_frame);
    }
  else
    {
    vs->snapshot_plugin->write_image(vs->snapshot_handle->priv,
                                     frame);
    }
  vs->snapshot_counter++;
  vs->last_snapshot_time = frame_time;
  }

static void process_func(void * data, gavl_video_frame_t * frame)
  {
  bg_recorder_t * rec = data;

  /* Check whether to make a snapshot */
  check_snapshot(rec, frame);
  }

void * bg_recorder_video_thread(void * data)
  {
  bg_recorder_t * rec = data;
  bg_recorder_video_stream_t * vs = &rec->vs;
  gavl_time_t idle_time = GAVL_TIME_SCALE / 100; // 10 ms

  gavl_video_connector_start(vs->conn);
  
  vs->process_format = gavl_video_connector_get_process_format(vs->conn);
  
  bg_thread_wait_for_start(vs->th);

  gavl_timer_set(vs->timer, 0);
  gavl_timer_start(vs->timer);
  
  while(1)
    {
    if(!bg_thread_check(vs->th))
      break;

    if(bg_recorder_video_get_eof(vs))
      {
      gavl_time_delay(&idle_time);
      continue;
      }

    if(!gavl_video_connector_process(vs->conn))
      break;
    
    if(vs->monitor_plugin && vs->monitor_plugin->handle_events)
      vs->monitor_plugin->handle_events(vs->monitor_handle->priv);
    
    /* */
    
    }
  gavl_timer_stop(vs->timer);
  
  return NULL;
  }

static gavl_source_status_t
read_video_internal(void * data, gavl_video_frame_t ** frame)
  {
  gavl_source_status_t st;
  gavl_time_t time_after, cur_time;

  gavl_video_frame_t * f = NULL;
  
  bg_recorder_t * rec = data;
  bg_recorder_video_stream_t * vs = &rec->vs;
  
  cur_time = gavl_timer_get(vs->timer);

  if((st = gavl_video_source_read_frame(vs->in_src, &f)) != GAVL_SOURCE_OK)
    return st;
  
  time_after = gavl_timer_get(vs->timer);

  vs->last_capture_duration = time_after - cur_time;
  vs->frame_counter++;

  /* Calculate actual framerate */
  
  vs->fps_frame_counter++;
  if(vs->fps_frame_counter == FRAMERATE_INTERVAL)
    vs->fps_frame_counter = 0;
  
  if(!vs->fps_frame_counter)
    {
    bg_recorder_msg_framerate(rec, (double)(FRAMERATE_INTERVAL*GAVL_TIME_SCALE) /
                              (double)(time_after - vs->fps_frame_time));
    vs->fps_frame_time = time_after;
    }
  vs->last_frame_time = time_after;
  *frame = f;
  return st;
  }

int bg_recorder_video_init(bg_recorder_t * rec)
  {
  gavl_video_format_t fmt;
  gavl_video_source_t * src;
  bg_recorder_video_stream_t * vs = &rec->vs;

  
  vs->frame_counter = 0;
  vs->fps_frame_time = 0;
  vs->fps_frame_counter = 0;

  memset(&fmt, 0, sizeof(fmt));
  
  /* Open input */
  if(!vs->input_plugin->open(vs->input_handle->priv, NULL,
                             &fmt, &vs->m))
    return 0;
  bg_metadata_date_now(&vs->m, GAVL_META_DATE_CREATE);
  
  vs->flags |= STREAM_INPUT_OPEN;

  vs->in_src = vs->input_plugin->get_video_source(vs->input_handle->priv);
  vs->src = gavl_video_source_create(read_video_internal, rec, GAVL_SOURCE_SRC_ALLOC,
                                     gavl_video_source_get_src_format(vs->in_src)); 

  gavl_video_source_set_dst(vs->in_src, 0,
                            gavl_video_source_get_src_format(vs->src));
  
  /* Set up filter chain */

  src = bg_video_filter_chain_connect(vs->fc, vs->src);
  
  vs->conn = gavl_video_connector_create(src);

  gavl_video_connector_set_process_func(vs->conn, process_func, rec);
  
  /* Set up monitoring */

  if(vs->flags & STREAM_MONITOR)
    {
    gavl_video_format_copy(&fmt, gavl_video_source_get_src_format(src));
    if(!vs->monitor_plugin->open(vs->monitor_handle->priv,
                                 &fmt, 1))
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN,
             "Opening monitor plugin failed");
      return 0;
      }
    
    gavl_video_connector_connect(vs->conn, vs->monitor_plugin->get_sink(vs->monitor_handle->priv));
    
    vs->flags |= STREAM_MONITOR_OPEN;

    bg_ov_plugin_set_visible(vs->monitor_handle, 1);
    bg_ov_plugin_set_window_title(vs->monitor_handle, "Gmerlin recorder "VERSION);
    }
  
  /* Set up encoding */

  if(vs->flags & STREAM_ENCODE)
    {
    vs->enc_index =
      bg_encoder_add_video_stream(rec->enc, &vs->m, gavl_video_source_get_src_format(vs->src), 0, NULL);
    }
  
  /* Initialize snapshot counter */
  
  return 1;
  }

void bg_recorder_video_finalize_encode(bg_recorder_t * rec)
  {
  bg_recorder_video_stream_t * vs = &rec->vs;
  gavl_video_connector_connect(vs->conn, bg_encoder_get_video_sink(rec->enc, vs->enc_index));
  
  vs->flags |= STREAM_ENCODE_OPEN;
  }

void bg_recorder_video_cleanup(bg_recorder_t * rec)
  {
  bg_recorder_video_stream_t * vs = &rec->vs;
  if(vs->flags & STREAM_INPUT_OPEN)
    vs->input_plugin->close(vs->input_handle->priv);
  
  if(vs->flags & STREAM_MONITOR_OPEN)
    vs->monitor_plugin->close(vs->monitor_handle->priv);

  if(vs->conn)
    gavl_video_connector_destroy(vs->conn);

  if(vs->snapshot_frame)
    gavl_video_frame_destroy(vs->snapshot_frame);
  
  vs->flags &= ~(STREAM_INPUT_OPEN |
                 STREAM_ENCODE_OPEN |
                 STREAM_MONITOR_OPEN |
                 STREAM_SNAPSHOT_INIT);
  
  }

void bg_recorder_handle_video_filter_command(bg_recorder_t * p, gavl_msg_t * msg)
  {
  int need_restart = 0;
  bg_msg_sink_t * sink;
  
  bg_video_filter_chain_lock(p->as.fc);

  sink = bg_video_filter_chain_get_cmd_sink(p->vs.fc);
  bg_msg_sink_put(sink, msg);

  need_restart =
    bg_video_filter_chain_need_restart(p->vs.fc);
  
  bg_video_filter_chain_unlock(p->vs.fc);

  if(need_restart)
    {
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN,
           "Restarting recorder due to changed video filters");
    bg_recorder_interrupt(p);
    bg_recorder_resume(p);
    }
  }
