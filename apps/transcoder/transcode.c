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


#include "transcode.h"
#include "app.h"

#include <gavl/log.h>
#define LOG_DOMAIN "transcode"

#include <gmerlin/bggavl.h>
#include <gavl/utils.h>

static void transfer_config(transcoder_t * t, const gavl_dictionary_t * track)
  {
  const gavl_dictionary_t * src_dict;
  gavl_dictionary_t * dst_dict;
  const gavl_dictionary_t * src_stream;
  int i;

  /* Transfer config- and metadata from the track */
  if((src_dict = gavl_track_get_metadata(track)))
    {
    dst_dict = gavl_track_get_metadata_nc(t->src->track);
    gavl_dictionary_reset(dst_dict);
    gavl_dictionary_copy(dst_dict, src_dict);
    }

  if((src_dict = gavl_dictionary_get_dictionary(track, BG_TRACK_CONFIG_TAG)))
    {
    dst_dict = gavl_dictionary_get_dictionary_create(t->src->track, BG_TRACK_CONFIG_TAG);
    gavl_dictionary_reset(dst_dict);
    gavl_dictionary_copy(dst_dict, src_dict);
    }

  for(i = 0; i < t->src->num_streams; i++)
    {
    int idx;
    const char * action;
    bg_media_source_stream_t * s = t->src->streams[i];

    idx = bg_media_source_get_stream_idx(t->src, s);
    src_stream = gavl_track_get_stream(track, s->type, idx);

    if(!src_stream)
      {
      //      fprintf(stderr, "Got no stream\n");
      //      gavl_dictionary_dump(track, 2);
      continue; // Probably nothing to do
      }
    
    /* Metadata (nothing for now) */
    if((src_dict = gavl_stream_get_metadata(src_stream)))
      {
      dst_dict = gavl_stream_get_metadata_nc(s->s);
      gavl_dictionary_reset(dst_dict);
      gavl_dictionary_copy(dst_dict, src_dict);
      }
    
    /* Configuration */
    if((src_dict = bg_track_get_config(src_stream, BG_TRACK_CONFIG_TRANSCODE)))
      {
      //      fprintf(stderr, "Got stream config\n");
      //      gavl_dictionary_dump(src_dict, 2);
      
      dst_dict = bg_track_get_config_nc(s->s, BG_TRACK_CONFIG_TRANSCODE);
      gavl_dictionary_reset(dst_dict);
      gavl_dictionary_copy(dst_dict, src_dict);
      
      /* Language -> copy to metadata */
      dst_dict = gavl_stream_get_metadata_nc(s->s);
      gavl_dictionary_copy_value(dst_dict, src_dict, GAVL_META_LANGUAGE);
      
      /* Action */
      action = gavl_dictionary_get_string(src_dict, "action");
      if(!strcmp(action, "transcode"))
        s->action = BG_STREAM_ACTION_DECODE;
      else if(!strcmp(action, "copy"))
        s->action = BG_STREAM_ACTION_READRAW;
      else
        s->action = BG_STREAM_ACTION_OFF;
      }
    
    }
  }

static void sanitize_label(char* str)
  {
  int len;
  int i;
  
  if(str == NULL || *str == '\0')
    {
    return;
    }
  
  len = strlen(str);
  
  for(i = 0; i < len; i++)
    {
    char c = str[i];
    
    if((c >= 0 && c <= 31) || c == 127)
      {
      str[i] = '_';
      continue;
      }
    
    if(strchr("<>:\"/\\|?*", c) != NULL)
      {
      str[i] = '_';
      }
    }
  
  for(i = 0; i < len && str[i] == '.'; i++)
    {
    str[i] = '_';
    }
  
  for(i = len - 1; i > 0 && (str[i] == '.' || str[i] == ' '); i--)
    {
    str[i] = '_';
    len = i;
    }
  }


static int open_output(transcoder_t * t, const gavl_dictionary_t * track)
  {
  char * label = NULL;
  char * outfile = NULL;
  const char * var;
  const char * ext;
  const char * path;
  /* TODO: Subdir */
  //  const char * subdir;
  const gavl_dictionary_t * dict;
  const gavl_array_t * arr;
  int result = 0;
  const gavl_dictionary_t * m;
  bg_encoder_plugin_t * enc;
  enc = (bg_encoder_plugin_t*)t->encoder->plugin;
  
  if(!(m = gavl_track_get_metadata(track)) ||
     !(var = gavl_dictionary_get_string(m, GAVL_META_LABEL)))
    goto fail;
  
  label = gavl_strdup(var);
  sanitize_label(label);

  if(!(dict = bg_cfg_registry_find_section(bg_cfg_registry, PREFS_OUTPUT)) ||
     !(path = gavl_dictionary_get_string(dict, "output_path")))
    path = ".";
  
  if(!(arr = bg_plugin_info_get_extensions(t->encoder->info)) ||
     !(ext = gavl_string_array_get(arr, 0)))
    goto fail;

  outfile = gavl_sprintf("%s/%s.%s", path, label, ext);

  fprintf(stderr, "Opening %s\n", outfile);

  result = enc->open(t->encoder->priv, outfile, m);

  t->progress_msg = gavl_sprintf("Encoding %s.%s", label, ext);
  
  fail:
  
  if(outfile)
    free(outfile);
  
  if(label)
    free(label);
  
  return result;
  }

static int transcoder_init(transcoder_t * t, const gavl_dictionary_t * track)
  {
  int num_variants = 0;
  int variant = 0;
  bg_input_plugin_t * input_plugin;
  const gavl_dictionary_t * cfg;
  const gavl_dictionary_t * m;
  
  /* Open input */
  if(!(t->input = bg_load_track(track, variant, &num_variants)))
    return 0;

  input_plugin = (bg_input_plugin_t*)t->input->plugin;

  t->src = input_plugin->get_src(t->input->priv);
  
  /* Check if streams match */
  if((bg_media_source_get_num_streams(t->src, GAVL_STREAM_AUDIO) !=
      gavl_track_get_num_streams(track, GAVL_STREAM_AUDIO)) ||
     (bg_media_source_get_num_streams(t->src, GAVL_STREAM_VIDEO) !=
      gavl_track_get_num_streams(track, GAVL_STREAM_VIDEO)) ||
     (bg_media_source_get_num_streams(t->src, GAVL_STREAM_TEXT) !=
      gavl_track_get_num_streams(track, GAVL_STREAM_TEXT)) ||
     (bg_media_source_get_num_streams(t->src, GAVL_STREAM_OVERLAY) !=
      gavl_track_get_num_streams(track, GAVL_STREAM_OVERLAY)))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Number of streams don't match");
    return 0;
    }

  /*
   *   Apply the configuration. This also sets the initial
   *   stream actions
   */
  
  transfer_config(t, track);

  m = gavl_track_get_metadata(t->src->track);
  gavl_dictionary_get_long(m, GAVL_META_APPROX_DURATION, &t->duration);
  
  /* Switch off streams, which cannot be encoded anyway */

  if(!(cfg = bg_track_get_config(track, BG_TRACK_CONFIG_ENCODER)) ||
     !(cfg = gavl_dictionary_get_dictionary(cfg, "plugin")))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Got no encoding plugin");
    return 0;
    }
  fprintf(stderr, "Load encoder:\n");
  gavl_dictionary_dump(cfg, 2);
  fprintf(stderr, "\n");
  
  t->encoder = bg_plugin_load_with_options(cfg);

  /* Open output file */
  if(!open_output(t, track))
    return 0;
  
  /* Select source streams according to what the transcoder can do */
  
  if(!bg_media_encoder_init(t->src, t->encoder))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "No streams to encode");
    return 0;
    }
  
  /* Check for filtering (might set more streams to decoding mode) */
  bg_media_source_filter_init(&t->src_filter, t->src);
  
  bg_input_plugin_start(t->input);

  /* input -> filters  */
  bg_media_source_filter_connect(&t->src_filter, t->src);

  /* filters -> encoder */

  if(!bg_media_encoder_connect(&t->src_encoder, &t->src_filter, t->encoder))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Connecting encoders failed");
    return 0;
    }

  gavl_dictionary_copy(&t->track, track);
  
  /* */
  return 1;
  }

static int handle_cmd(void * data, gavl_msg_t * msg)
  {
  //  transcoder_t * t = data;
  switch(msg->NS)
    {
    case GAVL_MSG_NS_GENERIC:
      switch(msg->ID)
        {
        case GAVL_CMD_QUIT:
          /* Quit */
          return 0;
          break;
        }
      break;
    }
  return 1;
  }

static void * thread_func(void * data)
  {
  transcoder_t * t = data;
  gavl_source_status_t st;

  gavl_time_t transcoded;
  gavl_time_t cur;
  
  while(1)
    {
    
    st = bg_media_encoder_process(&t->src_encoder, &transcoded);
    
    if(st != GAVL_SOURCE_OK)
      {
      /* Encoding completed */
      gavl_msg_t * msg = bg_msg_sink_get(t->ctrl.evt_sink);
      gavl_msg_set_id_ns(msg, GAVL_MSG_QUIT, GAVL_MSG_NS_GENERIC);
      bg_msg_sink_put(t->ctrl.evt_sink);

      //      fprintf(stderr, "Transcoding complete\n");
      

      break;
      }

    //    fprintf(stderr, "Transcoder iteration %f\n", gavl_time_to_seconds(transcoded));
    
    if(!bg_msg_sink_iteration(t->ctrl.cmd_sink))
      {
      /* Got cancel command */
      gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Got cancel command");
      t->flags |= TRANSCODER_FLAG_DELETE;
      break;
      }

    cur = gavl_timer_get(t->timer);
    if((t->duration != GAVL_TIME_UNDEFINED) &&
       ((t->last_progress_time == GAVL_TIME_UNDEFINED) ||
        (cur - t->last_progress_time >= GAVL_TIME_SCALE / 2)))
      {
      double percentage;
      gavl_msg_t * msg = bg_msg_sink_get(t->ctrl.evt_sink);
      gavl_msg_set_id_ns(msg, GAVL_MSG_PROGRESS, GAVL_MSG_NS_GENERIC);

      percentage = (double)transcoded / (double)t->duration;
      
      gavl_msg_set_arg_float(msg, 0, percentage);

      
      gavl_msg_set_arg_string(msg, 1, t->progress_msg);
      
      bg_msg_sink_put(t->ctrl.evt_sink);
      
      t->last_progress_time = cur;
      }
    }
  return NULL;
  } 


transcoder_t * transcoder_create(const gavl_dictionary_t * track, bg_msg_sink_t * sink)
  {
  transcoder_t * ret = calloc(1, sizeof(*ret));

  ret->last_progress_time = GAVL_TIME_UNDEFINED;
  ret->duration = GAVL_TIME_UNDEFINED;

  ret->timer = gavl_timer_create();
  gavl_timer_start(ret->timer);
  
  
  if(!transcoder_init(ret, track))
    {
    transcoder_destroy(ret);
    ret = NULL;
    }

  bg_controllable_init(&ret->ctrl,
                       bg_msg_sink_create(handle_cmd, ret, 0),
                       bg_msg_hub_create(1));

  bg_msg_hub_connect_sink(ret->ctrl.evt_hub, sink);
  
  pthread_create(&ret->th, NULL, thread_func, ret);
  
  return ret;
  }

static void transcoder_cleanup(transcoder_t * t)
  {
  bg_encoder_plugin_t * enc;
  /* Close */
  if(t->progress_msg)
    free(t->progress_msg);

  enc = (bg_encoder_plugin_t*)t->encoder->plugin;

  enc->close(t->encoder->priv, !!(t->flags & TRANSCODER_FLAG_DELETE));
  
  bg_controllable_cleanup(&t->ctrl);
  
  bg_plugin_unref(t->input);
  bg_plugin_unref(t->encoder);
  }

void transcoder_destroy(transcoder_t * t)
  {
  /* Send stop signal */
  gavl_msg_t * msg = bg_msg_sink_get(t->ctrl.cmd_sink);
  gavl_msg_set_id_ns(msg, GAVL_CMD_QUIT, GAVL_MSG_NS_GENERIC);
  bg_msg_sink_put(t->ctrl.cmd_sink);
  
  pthread_join(t->th, NULL);

  bg_media_encoder_dump_stats(&t->src_encoder);
  
  transcoder_cleanup(t);

  gavl_timer_destroy(t->timer);
  
  free(t);
  }

