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

#include <gmerlin/translation.h>

#include <gmerlin/recorder.h>
#include <recorder_private.h>
#include <language_table.h>
#include <gmerlin/utils.h>

#include <gmerlin/log.h>
#define LOG_DOMAIN "recorder.audio"

#include <gavl/metatags.h>

void bg_recorder_create_audio(bg_recorder_t * rec)
  {
  bg_recorder_audio_stream_t * as = &rec->as;
  
  bg_gavl_audio_options_init(&as->opt);
  
  as->fc = bg_audio_filter_chain_create(&as->opt, rec->plugin_reg);
  as->th = bg_thread_create(rec->tc);
  as->pd = gavl_peak_detector_create();
  
  pthread_mutex_init(&as->eof_mutex, NULL);
  }

void bg_recorder_audio_set_eof(bg_recorder_audio_stream_t * s, int eof)
  {
  pthread_mutex_lock(&s->eof_mutex);
  s->eof = eof;
  pthread_mutex_unlock(&s->eof_mutex);
  }

int  bg_recorder_audio_get_eof(bg_recorder_audio_stream_t * s)
  {
  int ret;
  pthread_mutex_lock(&s->eof_mutex);
  ret = s->eof;
  pthread_mutex_unlock(&s->eof_mutex);
  return ret;
  }


void bg_recorder_destroy_audio(bg_recorder_t * rec)
  {
  bg_recorder_audio_stream_t * as = &rec->as;
  bg_audio_filter_chain_destroy(as->fc);
  bg_thread_destroy(as->th);

  gavl_peak_detector_destroy(as->pd);
  pthread_mutex_destroy(&as->eof_mutex);
  gavl_dictionary_free(&as->m);
  }

static const bg_parameter_info_t parameters[] =
  {
    {
      .name = "do_audio",
      .long_name = TRS("Record audio"),
      .type = BG_PARAMETER_CHECKBUTTON,
      .val_default = GAVL_VALUE_INIT_INT(1),
    },
    {
      .name      = "plugin",
      .long_name = TRS("Plugin"),
      .type      = BG_PARAMETER_MULTI_MENU,
      .flags     = BG_PARAMETER_PLUGIN,
    },
    {
      .name      = GAVL_META_LANGUAGE,
      .long_name = TRS("Language"),
      .type      = BG_PARAMETER_STRINGLIST,
      .val_default = GAVL_VALUE_INIT_STRING("eng"),
      .multi_names = bg_language_codes,
      .multi_labels = bg_language_labels,
    },
    { },
  };

const bg_parameter_info_t *
bg_recorder_get_audio_parameters(bg_recorder_t * rec)
  {
  bg_recorder_audio_stream_t * as = &rec->as;
  if(!as->parameters)
    {
    as->parameters = bg_parameter_info_copy_array(parameters);
    
    bg_plugin_registry_set_parameter_info(rec->plugin_reg,
                                          BG_PLUGIN_RECORDER_AUDIO,
                                          0,
                                          &as->parameters[1]);
    }
  return as->parameters;
  }

void
bg_recorder_set_audio_parameter(void * data,
                                const char * name,
                                const gavl_value_t * val)
  {
  bg_recorder_t * rec = data;
  bg_recorder_audio_stream_t * as = &rec->as;
  
  if(!name)
    return;
  
  //  if(name)
  //    fprintf(stderr, "bg_recorder_set_audio_parameter %s\n", name);

  if(!strcmp(name, "do_audio"))
    {
    if(!!(as->flags & STREAM_ACTIVE) != val->v.i)
      bg_recorder_interrupt(rec);
    
    if(val->v.i)
      as->flags |= STREAM_ACTIVE;
    else
      as->flags &= ~STREAM_ACTIVE;
    }
  else if(!strcmp(name, GAVL_META_LANGUAGE))
    gavl_dictionary_set_string(&as->m, GAVL_META_LANGUAGE, val->v.str);
  else if(!strcmp(name, "plugin"))
    {
    const char * plugin_name;

    plugin_name = bg_multi_menu_get_selected_name(val);
    
    if(!as->input_handle ||
       strcmp(as->input_handle->info->name, plugin_name))
      {
      if(rec->flags & FLAG_RUNNING)
        bg_recorder_interrupt(rec);

      if(as->input_handle)
        bg_plugin_unref(as->input_handle);
    
      as->input_handle = bg_plugin_load_with_options(rec->plugin_reg,
                                                     bg_multi_menu_get_selected(val));
      as->input_plugin = (bg_recorder_plugin_t*)(as->input_handle->plugin);
      }
    }
  }

const bg_parameter_info_t *
bg_recorder_get_audio_filter_parameters(bg_recorder_t * rec)
  {
  bg_recorder_audio_stream_t * as = &rec->as;
  return bg_audio_filter_chain_get_parameters(as->fc);
  }

void
bg_recorder_set_audio_filter_parameter(void * data,
                                       const char * name,
                                       const gavl_value_t * val)
  {
  
  bg_recorder_t * rec = data;
  bg_recorder_audio_stream_t * as = &rec->as;
  if(!name)
    {
    if(!(rec->flags & FLAG_RUNNING))
      bg_recorder_resume(rec);
    return;
    }
  bg_recorder_interrupt(rec);
  
  bg_audio_filter_chain_lock(as->fc);
  bg_audio_filter_chain_set_parameter(as->fc, name, val);
  bg_audio_filter_chain_unlock(as->fc);
  }

void bg_recorder_handle_audio_filter_command(bg_recorder_t * p, gavl_msg_t * msg)
  {
  int need_restart = 0;
  bg_msg_sink_t * sink;
  
  bg_audio_filter_chain_lock(p->as.fc);

  sink = bg_audio_filter_chain_get_cmd_sink(p->as.fc);
  bg_msg_sink_put(sink, msg);

  need_restart =
    bg_audio_filter_chain_need_restart(p->as.fc);
  
  bg_audio_filter_chain_unlock(p->as.fc);

  if(need_restart)
    {
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN,
           "Restarting recorder due to changed audio filters");
    bg_recorder_interrupt(p);
    bg_recorder_resume(p);
    }
  }

static void process_func(void * data, gavl_audio_frame_t * frame)
  {
  bg_recorder_t * rec = data;
  bg_recorder_audio_stream_t * as = &rec->as;
  
  bg_recorder_update_time(rec,
                          gavl_time_unscale(as->process_format->samplerate,
                                            frame->timestamp +
                                            frame->valid_samples));
  
  }

void * bg_recorder_audio_thread(void * data)
  {
  gavl_time_t idle_time = GAVL_TIME_SCALE / 100; // 10 ms
  bg_recorder_t * rec = data;
  bg_recorder_audio_stream_t * as = &rec->as;

  /* Fire up connectors */
  gavl_audio_connector_start(as->conn);
  as->process_format = gavl_audio_connector_get_process_format(as->conn);
  
  bg_thread_wait_for_start(as->th);
  
  while(1)
    {
    if(!bg_thread_check(as->th))
      break;

    if(bg_recorder_audio_get_eof(as))
      {
      gavl_time_delay(&idle_time);
      continue;
      }
    if(!gavl_audio_connector_process(as->conn))
      break;
    }
  return NULL;

  }

static void peaks_callback(void * priv,
                           int samples,
                           const double * min,
                           const double * max,
                           const double * abs)
  {
  const gavl_audio_format_t * fmt;
  double peaks[2]; /* Doesn't work for > 2 channels!! */
  bg_recorder_t * rec = priv;
  bg_recorder_audio_stream_t * as = &rec->as;

  fmt = gavl_peak_detector_get_format(as->pd);
  
  peaks[0] = abs[0];
  
  if(fmt->num_channels == 1)
    peaks[1] = abs[0];
  else
    peaks[1] = abs[1];

  bg_recorder_msg_audiolevel(rec, peaks, samples);
  gavl_peak_detector_reset(as->pd);
  }

int bg_recorder_audio_init(bg_recorder_t * rec)
  {
  gavl_audio_source_t * src;
  bg_recorder_audio_stream_t * as = &rec->as;
  gavl_audio_format_t input_format;
  memset(&input_format, 0, sizeof(input_format));
  
  /* Open input */
  if(!as->input_plugin->open(as->input_handle->priv, &input_format, NULL, &as->m))
    {
    return 0;
    }

  bg_metadata_date_now(&as->m, GAVL_META_DATE_CREATE);
  as->flags |= STREAM_INPUT_OPEN;
  
  src = as->input_plugin->get_audio_source(as->input_handle->priv);
  
  /* Set up filter chain */
  
  src = bg_audio_filter_chain_connect(as->fc, src);
  
  /* Set up peak detection */
  
  gavl_peak_detector_set_format(as->pd, gavl_audio_source_get_src_format(src));
  gavl_peak_detector_set_callbacks(as->pd, NULL, peaks_callback, rec);
  
  /* Create connector */

  as->conn = gavl_audio_connector_create(src);
  gavl_audio_connector_connect(as->conn, gavl_peak_detector_get_sink(as->pd));

  gavl_audio_connector_set_process_func(as->conn, process_func, rec);
  
  /* Set up output */

  if(as->flags & STREAM_ENCODE)
    {
    as->enc_index = bg_encoder_add_audio_stream(rec->enc, &as->m,
                                                gavl_audio_source_get_src_format(src), 0, NULL);
    }
  
  return 1;
  }


void bg_recorder_audio_cleanup(bg_recorder_t * rec)
  {
  bg_recorder_audio_stream_t * as = &rec->as;

  if(as->flags & STREAM_INPUT_OPEN)
    as->input_plugin->close(as->input_handle->priv);

  as->flags &= ~(STREAM_INPUT_OPEN | STREAM_ENCODE_OPEN);
  
  if(as->conn)
    gavl_audio_connector_destroy(as->conn);
  }

void bg_recorder_audio_finalize_encode(bg_recorder_t * rec)
  {
  bg_recorder_audio_stream_t * as = &rec->as;
  gavl_audio_connector_connect(as->conn, bg_encoder_get_audio_sink(rec->enc, as->enc_index));
  as->flags |= STREAM_ENCODE_OPEN;
  }
