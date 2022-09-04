/*****************************************************************
 * Gmerlin - a general purpose multimedia framework and applications
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
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>

#include <gavl/metatags.h>


#include <gmerlin/player.h>
#include <gmerlin/application.h>

#include <gavl/keycodes.h>

#include <playerprivate.h>

#define LOG_DOMAIN "player"
#include <gmerlin/log.h>

/* State variables */

static const bg_state_var_desc_t state_vars[] =
  {
    { BG_PLAYER_STATE_VOLUME,          GAVL_TYPE_FLOAT,      GAVL_VALUE_INIT_FLOAT(0.5) },
    { BG_PLAYER_STATE_STATUS,          GAVL_TYPE_INT,        GAVL_VALUE_INIT_INT(BG_PLAYER_STATUS_INIT) },
    { BG_PLAYER_STATE_CURRENT_TRACK,   GAVL_TYPE_DICTIONARY  },
    { BG_PLAYER_STATE_CURRENT_TIME,    GAVL_TYPE_DICTIONARY  },
    { BG_PLAYER_STATE_MODE,                 GAVL_TYPE_INT,        }, // Zero
    { BG_PLAYER_STATE_MUTE,                 GAVL_TYPE_INT,        },
    { BG_PLAYER_STATE_AUDIO_STREAM_USER,    GAVL_TYPE_INT,        },
    { BG_PLAYER_STATE_VIDEO_STREAM_USER,    GAVL_TYPE_INT,        },
    { BG_PLAYER_STATE_SUBTITLE_STREAM_USER, GAVL_TYPE_INT,        },
    { BG_PLAYER_STATE_CHAPTER,              GAVL_TYPE_INT,        },
    { BG_PLAYER_STATE_MIMETYPES,       GAVL_TYPE_ARRAY },
    { BG_PLAYER_STATE_PROTOCOLS,       GAVL_TYPE_ARRAY },
    { BG_PLAYER_STATE_LABEL,           GAVL_TYPE_STRING,     },
    { BG_PLAYER_STATE_QUEUE_IDX,       GAVL_TYPE_INT,        },
    { BG_PLAYER_STATE_QUEUE_LEN,       GAVL_TYPE_INT,        },
    { /* End */ },
  };

/* Builtin accelerators */
const bg_accelerator_t bg_player_accels[] =
  {
    { GAVL_KEY_LEFT,    0,                                       BG_PLAYER_ACCEL_SEEK_BACKWARD           },
    { GAVL_KEY_RIGHT,   0,                                       BG_PLAYER_ACCEL_SEEK_FORWARD            },
    { GAVL_KEY_LEFT,    GAVL_KEY_SHIFT_MASK,                     BG_PLAYER_ACCEL_SEEK_BACKWARD_FAST      },
    { GAVL_KEY_RIGHT,   GAVL_KEY_SHIFT_MASK,                     BG_PLAYER_ACCEL_SEEK_FORWARD_FAST       },
    { GAVL_KEY_REWIND,  0,                                       BG_PLAYER_ACCEL_SEEK_BACKWARD           },
    { GAVL_KEY_FORWARD, 0,                                       BG_PLAYER_ACCEL_SEEK_FORWARD            },
    { GAVL_KEY_REWIND,  GAVL_KEY_SHIFT_MASK,                     BG_PLAYER_ACCEL_SEEK_BACKWARD_FAST      },
    { GAVL_KEY_FORWARD, GAVL_KEY_SHIFT_MASK,                     BG_PLAYER_ACCEL_SEEK_FORWARD_FAST       },
    { GAVL_KEY_VOLUME_MINUS, 0,                                  BG_PLAYER_ACCEL_VOLUME_DOWN             },
    { GAVL_KEY_VOLUME_PLUS,  0,                                  BG_PLAYER_ACCEL_VOLUME_UP               },
    { GAVL_KEY_MINUS,        0,                                  BG_PLAYER_ACCEL_VOLUME_DOWN             },
    { GAVL_KEY_PLUS,         0,                                  BG_PLAYER_ACCEL_VOLUME_UP               },
    { GAVL_KEY_MUTE,      0,                                     BG_PLAYER_ACCEL_MUTE                    },
    { GAVL_KEY_0,         0,                                     BG_PLAYER_ACCEL_SEEK_START              },
    { GAVL_KEY_1,         0,                                     BG_PLAYER_ACCEL_SEEK_10                 },
    { GAVL_KEY_2,         0,                                       BG_PLAYER_ACCEL_SEEK_20                 },
    { GAVL_KEY_3,         0,                                       BG_PLAYER_ACCEL_SEEK_30                 },
    { GAVL_KEY_4,         0,                                       BG_PLAYER_ACCEL_SEEK_40                 },
    { GAVL_KEY_5,         0,                                       BG_PLAYER_ACCEL_SEEK_50                 },
    { GAVL_KEY_6,         0,                                       BG_PLAYER_ACCEL_SEEK_60                 },
    { GAVL_KEY_7,         0,                                       BG_PLAYER_ACCEL_SEEK_70                 },
    { GAVL_KEY_8,         0,                                       BG_PLAYER_ACCEL_SEEK_80                 },
    { GAVL_KEY_9,         0,                                       BG_PLAYER_ACCEL_SEEK_90                 },
    { GAVL_KEY_SPACE,     0,                                       BG_PLAYER_ACCEL_PAUSE                   },
    { GAVL_KEY_PAUSE,     0,                                       BG_PLAYER_ACCEL_PAUSE                   },
    { GAVL_KEY_MUTE,      0,                                       BG_PLAYER_ACCEL_MUTE                    },
    { GAVL_KEY_NEXT,      GAVL_KEY_CONTROL_MASK,                   BG_PLAYER_ACCEL_NEXT_CHAPTER            },
    { GAVL_KEY_PREV,      GAVL_KEY_CONTROL_MASK,                   BG_PLAYER_ACCEL_PREV_CHAPTER            },
    { GAVL_KEY_PLAY,      0,                                       BG_PLAYER_ACCEL_PLAY                    },
    { GAVL_KEY_NEXT,      0, BG_PLAYER_ACCEL_NEXT                      },
    { GAVL_KEY_PREV,      0, BG_PLAYER_ACCEL_PREV                      },
    { GAVL_KEY_n,         0, BG_PLAYER_ACCEL_NEXT                      },
    { GAVL_KEY_m,         0, BG_PLAYER_ACCEL_MUTE                      },
    { GAVL_KEY_p,         0, BG_PLAYER_ACCEL_PREV                      },
    { GAVL_KEY_i,         0, BG_PLAYER_ACCEL_SHOW_INFO                 },
    { GAVL_KEY_t,         0, BG_PLAYER_ACCEL_SHOW_TIME                 },
    { GAVL_KEY_a,         0, BG_PLAYER_ACCEL_AUDIO_STREAM_MENU         },
    { GAVL_KEY_s,         0, BG_PLAYER_ACCEL_SUBTITLE_STREAM_MENU      },
    { GAVL_KEY_c,         0, BG_PLAYER_ACCEL_CHAPTER_MENU              },
    { GAVL_KEY_TAB,       0, BG_PLAYER_ACCEL_FULLSCREEN_TOGGLE       },
    { GAVL_KEY_F11,       0, BG_PLAYER_ACCEL_FULLSCREEN_TOGGLE       },
    { GAVL_KEY_BACKSPACE, 0, BG_PLAYER_ACCEL_STOP                    },
    { GAVL_KEY_v,         0, BG_PLAYER_ACCEL_NEXT_VISUALIZATION        },
    { GAVL_KEY_NONE,      0,                                         },
  };


void bg_player_key_pressed(bg_player_t * player, int key, int mask)
  {
  int id = 0;
  if(bg_accelerator_map_has_accel(player->video_stream.accel_map,
                                  key, mask, &id))
    {
    bg_player_accel_pressed(&player->ctrl, id);
    return;
    }
  if(player->video_stream.osd && bg_osd_key_pressed(player->video_stream.osd, key, mask))
    return;
  }

/* Input callbacks */


#if 0
static void buffer_notify(void * data, float percentage)
  {
  gavl_msg_t * m;
  bg_player_t * p = data;
  
  m = bg_msg_sink_get(p->ctrl.evt_sink);
  gavl_msg_set_src_buffering(m, percentage);
  bg_msg_sink_put(p->ctrl.evt_sink, m);
  
  }
#endif

static void state_init_time(gavl_dictionary_t * dict)
  {
  gavl_value_t val;
  gavl_dictionary_t * current_time;

  /* Initialize the time dictionary */
  gavl_value_init(&val);
  current_time = gavl_value_set_dictionary(&val);
  
  gavl_dictionary_set_long(current_time, BG_PLAYER_TIME, 0);
  gavl_dictionary_set_long(current_time, BG_PLAYER_TIME_REM, GAVL_TIME_UNDEFINED);
  gavl_dictionary_set_long(current_time, BG_PLAYER_TIME_ABS, GAVL_TIME_UNDEFINED);
  gavl_dictionary_set_long(current_time, BG_PLAYER_TIME_REM_ABS, GAVL_TIME_UNDEFINED);
  gavl_dictionary_set_float(current_time, BG_PLAYER_TIME_PERC, -1.0);

  bg_state_set(dict, 1, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_CURRENT_TIME, &val, NULL, 0);
  
  gavl_value_free(&val);
  }

static void state_init_track(gavl_dictionary_t * dict)
  {
  gavl_value_t val;
  gavl_dictionary_t * d;

  gavl_value_init(&val);

  d = gavl_value_set_dictionary(&val);
  d = gavl_dictionary_get_dictionary_create(d, GAVL_META_METADATA);
  gavl_dictionary_set_int(d, GAVL_META_IDX, 0);
  bg_state_set(dict, 1, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_CURRENT_TRACK, &val, NULL, 0);
  }

void bg_player_state_init(gavl_dictionary_t * dict, const char * label,
                          const gavl_array_t * protocols, const gavl_array_t * mimetypes)
  {
  gavl_value_t val;
  gavl_array_t * arr;
    
  bg_state_init_ctx(dict, BG_PLAYER_STATE_CTX, state_vars);
  
  bg_state_set_range_float(dict,
                           BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_VOLUME,
                           0.0, 1.0);
  
  bg_state_set_range_float(dict,
                           BG_PLAYER_STATE_CTX "/" BG_PLAYER_STATE_CURRENT_TIME, BG_PLAYER_TIME_PERC,
                           0.0, 1.0);

  if(protocols)
    {
    gavl_value_init(&val);
    arr = gavl_value_set_array(&val);
    gavl_array_copy(arr, protocols);
    bg_state_set(dict, 0, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_PROTOCOLS, &val, NULL, 0);
    gavl_value_free(&val);
    }
  else
    bg_state_set(dict, 0, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_PROTOCOLS, NULL, NULL, 0);
    
  
  if(mimetypes)
    {
    gavl_value_init(&val);
    arr = gavl_value_set_array(&val);
    gavl_array_copy(arr, mimetypes);
    bg_state_set(dict, 0, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_MIMETYPES, &val, NULL, 0);
    gavl_value_free(&val);
    }
  else
    bg_state_set(dict, 0, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_MIMETYPES, NULL, NULL, 0);
    
  
  if(label)
    {
    gavl_value_init(&val);
    gavl_value_set_string(&val, label);
    bg_state_set(dict, 0, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_LABEL, &val, NULL, 0);
    gavl_value_free(&val);
    }
  else
    bg_state_set(dict, 0, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_LABEL, NULL, NULL, 0);
  
  
  //  fprintf(stderr, "player_state_init %s\n", label);
  //  gavl_dictionary_dump(dict, 2);

  
  state_init_time(dict);

  bg_state_init_ctx(dict, BG_STATE_CTX_OV, bg_ov_state_vars);

  }

const char * bg_player_track_get_uri(gavl_dictionary_t * state, const gavl_dictionary_t * track)
  {
  const char * uri;
  const char * mimetype;
  int idx = 0;
  const gavl_dictionary_t * m = gavl_track_get_metadata(track);

  const gavl_value_t * val;
  const gavl_array_t * mimetypes = NULL;
  const gavl_array_t * protocols = NULL;
  
  if((val = bg_state_get(state, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_MIMETYPES)))
    mimetypes = gavl_value_get_array(val);

  if((val = bg_state_get(state, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_PROTOCOLS)))
    protocols = gavl_value_get_array(val);
  
  while(gavl_dictionary_get_src(m, GAVL_META_SRC, idx, &mimetype, &uri))
    {
    const char * ret = NULL;
    char * protocol;
    char * mimetype_priv;
    const char * end;

    protocol = NULL;
    mimetype_priv = NULL;
    
    if(*uri == '/')
      protocol = gavl_strdup("file");
    else if((end = strstr(uri, "://")))
      protocol = gavl_strndup(uri, end);
    
    if(!protocol ||
       (gavl_string_array_indexof(protocols, protocol) < 0))
      goto end;
    
    if((end = strchr(mimetype, ';')))
      {
      mimetype_priv = gavl_strndup(mimetype, end);
      gavl_strtrim(mimetype_priv);
      mimetype = mimetype_priv;
      }
      
    if((gavl_string_array_indexof(mimetypes, mimetype) >= 0))
      ret = uri;

    
    
    end:

    if(protocol)
      free(protocol);
    if(mimetype_priv)
      free(mimetype_priv);
    
    if(ret)
      return ret;
    
    idx++;
    }
  
  return NULL;

  }


/* Reset state after loading. Some variables are saved along with the state but need to be reset
   on program load */

void bg_player_state_reset(gavl_dictionary_t * dict)
  {
  gavl_value_t val;
  
  state_init_track(dict);
  state_init_time(dict);

  gavl_value_init(&val);
  gavl_value_set_int(&val, BG_PLAYER_STATUS_INIT);
  bg_state_set(dict, 0, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_STATUS, &val, NULL, 0);

  gavl_value_reset(&val);
  gavl_value_set_int(&val, 0);
  bg_state_set(dict, 1, BG_STATE_CTX_OV, BG_STATE_OV_VISIBLE, &val, NULL, 0);
  
  gavl_value_free(&val);
  }

void bg_player_apply_cmdline(bg_cfg_ctx_t * ctx)
  {
  const gavl_value_t * val;
  gavl_dictionary_t * s;

  
  if((s = ctx[BG_PLAYER_CFG_AUDIOFILTER].s) && (val = bg_plugin_config_get(BG_PLUGIN_FILTER_AUDIO)))
    {
    gavl_dictionary_set(s, BG_FILTER_CHAIN_PARAM_PLUGINS, val);
    }
  

  if((s = ctx[BG_PLAYER_CFG_VIDEOFILTER].s) && (val = bg_plugin_config_get(BG_PLUGIN_FILTER_VIDEO)))
    {
    gavl_dictionary_set(s, BG_FILTER_CHAIN_PARAM_PLUGINS, val);
    }


  if((s = ctx[BG_PLAYER_CFG_AUDIOPLUGIN].s) && (val = bg_plugin_config_get(BG_PLUGIN_OUTPUT_AUDIO)))
    {
    gavl_dictionary_set(s, BG_PARAMETER_NAME_PLUGIN, val);
    }

  if((s = ctx[BG_PLAYER_CFG_VIDEOPLUGIN].s) && (val = bg_plugin_config_get(BG_PLUGIN_OUTPUT_VIDEO)))
    {
    gavl_dictionary_set(s, BG_PARAMETER_NAME_PLUGIN, val);
    }
  
  }

bg_player_t * bg_player_create()
  {
  gavl_value_t mimetypes_val;
  gavl_array_t * mimetypes_arr;

  gavl_value_t protocols_val;
  gavl_array_t * protocols_arr;
  
  bg_player_t * ret;
  
  ret = calloc(1, sizeof(*ret));
  bg_plugin_registry_set_state(bg_plugin_reg, &ret->state);
  
  /* Callbacks */
  //  ret->input_callbacks.data = ret;
  //  ret->input_callbacks.duration_changed = duration_changed;
  //  ret->input_callbacks.metadata_changed = metadata_changed;
  //  ret->input_callbacks.buffer_notify    = buffer_notify;

  ret->state_init = 1;
  
  ret->visualization_mode = -1;
  
  gavl_value_init(&mimetypes_val);
  gavl_value_init(&protocols_val);

  mimetypes_arr = gavl_value_set_array(&mimetypes_val);
  protocols_arr = gavl_value_set_array(&protocols_val);

  bg_plugin_registry_get_input_mimetypes(bg_plugin_reg, mimetypes_arr);
  bg_plugin_registry_get_input_protocols(bg_plugin_reg, protocols_arr);
  
  bg_player_state_init(&ret->state, bg_app_get_label(), protocols_arr, mimetypes_arr);
  
  /* Create message queues */
  
  bg_controllable_init(&ret->ctrl,
                       bg_msg_sink_create(bg_player_handle_command, ret, 0),
                       bg_msg_hub_create(1));

  ret->src_msg_sink = bg_msg_sink_create(bg_player_handle_input_message, ret, 0);
  
  bg_player_tracklist_init(&ret->tl, ret->ctrl.evt_sink);
  
  //  bg_msg_hub_set_cache(ret->ctrl.evt_hub, BG_REMOTE_DEVICE_RENDERER);
  

  ret->thread_common = bg_thread_common_create();
  
  /* Create contexts */

  bg_player_audio_create(ret, bg_plugin_reg);
  bg_player_video_create(ret, bg_plugin_reg);
  bg_player_subtitle_create(ret);
  
  bg_player_ov_create(ret);
  ret->visualizer = bg_visualizer_create(ret);
  
  ret->threads[0] = ret->audio_stream.th;
  ret->threads[1] = ret->video_stream.th;
  
  pthread_mutex_init(&ret->state_mutex, NULL);
  pthread_mutex_init(&ret->restart_mutex, NULL);

  pthread_mutex_init(&ret->config_mutex, NULL);
  pthread_mutex_init(&ret->src_mutex, NULL);
  pthread_mutex_init(&ret->time_offset_mutex, NULL);

  /* Subtitles are off by default */
  ret->subtitle_stream_user = -1;
  //  ret->current_subtitle_stream = 5;
  ret->status = BG_PLAYER_STATUS_INIT;

  ret->wait_time = 10000;

  ret->src =      &ret->srcs[0];
  ret->src_next = &ret->srcs[1];
  
  bg_player_add_accelerators(ret, bg_player_accels);

  bg_cfg_ctx_init(&ret->cfg[BG_PLAYER_CFG_INPUT],
                  bg_player_get_input_parameters(ret),
                  "input",
                  TR("Input"),
                  bg_player_set_input_parameter,
                  ret);
  
  bg_cfg_ctx_init(&ret->cfg[BG_PLAYER_CFG_AUDIO], 
                  bg_player_get_audio_parameters(ret),
                  "audio",
                  TR("Audio"),
                  bg_player_set_audio_parameter,
                  ret);

  bg_cfg_ctx_init(&ret->cfg[BG_PLAYER_CFG_VIDEO], 
                  bg_player_get_video_parameters(ret),
                  "video",
                  TR("Video"),
                  bg_player_set_video_parameter,
                  ret);

  bg_cfg_ctx_init(&ret->cfg[BG_PLAYER_CFG_AUDIOFILTER], 
                  bg_player_get_audio_filter_parameters(ret),
                  "audiofilter",
                  TR("Audio filter"),
                  bg_player_set_audio_filter_parameter,
                  ret);
  
  bg_cfg_ctx_init(&ret->cfg[BG_PLAYER_CFG_VIDEOFILTER], 
                  bg_player_get_video_filter_parameters(ret),
                  "videofilter",
                  TR("Video filter"),
                  bg_player_set_video_filter_parameter,
                  ret);
  
  
  bg_cfg_ctx_init(&ret->cfg[BG_PLAYER_CFG_AUDIOPLUGIN], 
                  bg_player_get_oa_plugin_parameters(ret),
                  "oa",
                  TR("Audio output plugin"),
                  bg_player_set_oa_plugin_parameter,
                  ret);
  
  bg_cfg_ctx_init(&ret->cfg[BG_PLAYER_CFG_VIDEOPLUGIN], 
                  bg_player_get_ov_plugin_parameters(ret),
                  "ov",
                  TR("Video output plugin"),
                  bg_player_set_ov_plugin_parameter,
                  ret);
  
  bg_cfg_ctx_init(&ret->cfg[BG_PLAYER_CFG_SUBTITLE], 
                  bg_player_get_subtitle_parameters(ret),
                  "subtitles",
                  TR("Subtitles"),
                  bg_player_set_subtitle_parameter,
                  ret);

  bg_cfg_ctx_init(&ret->cfg[BG_PLAYER_CFG_OSD], 
                  bg_player_get_osd_parameters(ret),
                  "osd",
                  TR("OSD"),
                  bg_player_set_osd_parameter,
                  ret);

  bg_cfg_ctx_init(&ret->cfg[BG_PLAYER_CFG_VISUALIZATION], 
                  bg_player_get_visualization_parameters(ret),
                  "visualization",
                  TR("Visualization"),
                  bg_player_set_visualization_parameter,
                  ret);

  gavl_value_free(&mimetypes_val);
  gavl_value_free(&protocols_val);
  
  return ret;
  }



void bg_player_destroy(bg_player_t * player)
  {
  int i;
  
  bg_player_input_destroy(player);
  bg_player_ov_destroy(player);
  bg_player_audio_destroy(player);
  bg_player_video_destroy(player);
  bg_player_subtitle_destroy(player);

  bg_visualizer_destroy(player->visualizer);

  bg_controllable_cleanup(&player->ctrl);

  if(player->src_msg_sink)
    bg_msg_sink_destroy(player->src_msg_sink);
  
  pthread_mutex_destroy(&player->state_mutex);
  pthread_mutex_destroy(&player->restart_mutex);

  pthread_mutex_destroy(&player->config_mutex);
  pthread_mutex_destroy(&player->src_mutex);
  pthread_mutex_destroy(&player->time_offset_mutex);
  
  bg_thread_common_destroy(player->thread_common);

  for(i = 0; i < BG_PLAYER_CFG_NUM; i++)
    bg_cfg_ctx_free(&player->cfg[i]);

  bg_player_tracklist_free(&player->tl);

  gavl_dictionary_free(&player->state);
  
  free(player);
  }

bg_controllable_t * bg_player_get_controllable(bg_player_t * player)
  {
  return &player->ctrl;
  }

int bg_player_get_restart(bg_player_t * player)
  {
  int ret;
  pthread_mutex_lock(&player->restart_mutex);
  ret = player->restart;
  pthread_mutex_unlock(&player->restart_mutex);
  return ret;
  }


void bg_player_set_restart(bg_player_t * player, int restart)
  {
  pthread_mutex_lock(&player->restart_mutex);
  player->restart = restart;
  pthread_mutex_unlock(&player->restart_mutex);
  }

int  bg_player_get_status(bg_player_t * player)
  {
  int ret;
  pthread_mutex_lock(&player->state_mutex);
  ret = player->status;
  pthread_mutex_unlock(&player->state_mutex);
  return ret;
  }


void bg_player_set_status(bg_player_t * player, int status)
  {
  gavl_value_t val;
  pthread_mutex_lock(&player->state_mutex);
  player->status = status;
  pthread_mutex_unlock(&player->state_mutex);

  gavl_value_init(&val);
  gavl_value_set_int(&val, status);
  
  /* Broadcast this message */
  bg_player_state_set_local(player, 1, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_STATUS, &val);
  
  }

const bg_parameter_info_t *
bg_player_get_visualization_parameters(bg_player_t *  player)
  {
  return bg_visualizer_get_parameters(player->visualizer);
  }

void
bg_player_set_visualization_parameter(void*data,
                                      const char * name,
                                      const gavl_value_t*val)
  {
  bg_player_t * p;

  p = (bg_player_t*)data;

  bg_player_stream_change_init(p);
  
  bg_visualizer_set_parameter(p->visualizer, name, val);
  
  }

static const bg_parameter_info_t parameters[] =
  {
    {
      .name        = "message_interval",
      .long_name   = TRS("Control loop interval"),
      .type        = BG_PARAMETER_INT,
      .val_default = GAVL_VALUE_INIT_INT(10),
    },
    {
      .name         = "time_update",
      .long_name    = TRS("Time update interval"),
      .type         = BG_PARAMETER_STRINGLIST,
      .multi_names  = (char const *[]){ "seconds", "frames", NULL },
      .multi_labels = (char const *[]){ TRS("Seconds"), TRS("frames"), NULL },
      .val_default  = GAVL_VALUE_INIT_STRING("seconds"),
    },
    {
      .name         = "report_peak",
      .long_name    = TRS("Report peak values for audio"),
      .type         = BG_PARAMETER_CHECKBUTTON,
    },
    {
      .name         = "finish_mode",
      .long_name    = TRS("Finish mode"),
      .type         = BG_PARAMETER_STRINGLIST,
      .multi_names  = (char const *[]){ "change", "pause", NULL },
      .multi_labels = (char const *[]){ TRS("Change"), TRS("Pause"), NULL },
      .val_default  = GAVL_VALUE_INIT_STRING("change"),
    },
    { /* End of parameters */ }
  };




const bg_parameter_info_t * bg_player_get_parameters(bg_player_t * player)
  {
  return parameters;
  }


void bg_player_set_parameter(void * player, const char * name,
                             const gavl_value_t * val)
  {
  bg_player_t * p = player;
  if(!name)
    return;
  else if(!strcmp(name, "message_interval"))
    {
    p->wait_time = val->v.i;
    p->wait_time *= 1000;
    }
  else if(!strcmp(name, "time_update"))
    {
    if(!strcmp(val->v.str, "second"))
      {
      p->time_update_mode = TIME_UPDATE_SECOND;
      }
    else if(!strcmp(val->v.str, "frame"))
      {
      p->time_update_mode = TIME_UPDATE_FRAME;
      }
    }
  else if(!strcmp(name, "finish_mode"))
    {
    if(!strcmp(val->v.str, "change"))
      {
      p->finish_mode = BG_PLAYER_FINISH_CHANGE;
      }
    else if(!strcmp(val->v.str, "pause"))
      {
      p->finish_mode = BG_PLAYER_FINISH_PAUSE;
      }
    }
  else if(!strcmp(name, "report_peak"))
    {
    if(val->v.i)
      p->flags |= PLAYER_DO_REPORT_PEAK;
    else
      p->flags &= ~PLAYER_DO_REPORT_PEAK;
    }
  }

void bg_player_swap_sources(bg_player_t * p)
  {
  p->src_cur_idx = 1 - p->src_cur_idx;
  p->src      = &p->srcs[p->src_cur_idx];
  p->src_next = &p->srcs[1-p->src_cur_idx];
  }

bg_cfg_ctx_t * bg_player_get_cfg(bg_player_t *  player)
  {
  return player->cfg;
  }

void bg_player_set_eof(bg_player_t * p)
  {
  gavl_msg_t * msg = bg_msg_sink_get(p->ctrl.cmd_sink);
  gavl_msg_set_id_ns(msg, BG_PLAYER_CMD_EOF, BG_MSG_NS_PLAYER_PRIV);
  bg_msg_sink_put(p->ctrl.cmd_sink, msg);
  }

void bg_player_state_set_local(bg_player_t * p,
                               int last,
                               const char * ctx,
                               const char * var,
                               const gavl_value_t * val)
  {
  //  if(!strcmp(ctx, "player") && !strcmp(var, "track"))
  //    fprintf(stderr, "*** bg_player_state_set_local %s %s\n", ctx, var);

  bg_state_set(&p->state,
               last,
               ctx,
               var,
               val,
               p->ctrl.evt_sink, BG_MSG_STATE_CHANGED);
  }

void bg_player_state_set(bg_player_t * p,
                         int last,
                         const char * var,
                         gavl_value_t * val)
  {
  bg_state_set(&p->state,
               last,
               BG_PLAYER_STATE_CTX,
               var,
               val,
               p->ctrl.cmd_sink, BG_CMD_SET_STATE);
  }
