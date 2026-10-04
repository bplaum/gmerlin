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

#include <config.h>



#include "pipewire_common.h"

#include <spa/pod/parser.h>
#include <spa/param/audio/format-utils.h>
#include <pipewire/extensions/metadata.h>

#include <gavl/metatags.h>


#include <gavl/log.h>
#define LOG_DOMAIN "res_pipewire"


#include <spa/utils/result.h>
#include <spa/debug/pod.h>
#include <spa/debug/dict.h>

typedef struct
  {
  /* Pipewire structures */
  struct pw_main_loop *loop;
  struct pw_context *context;
  struct pw_core *core;
  struct pw_registry *registry;
  struct pw_node *node;
  //  struct pw_metadata *settings;
  
  /* Hooks */
  struct spa_hook registry_listener;
  struct spa_hook core_listener;
  struct spa_hook node_listener;
  struct spa_hook metadata_listener;
  
  
  const char * name;

  int error;
  int found;

  int pending_seq;

  gavl_audio_format_t * afmt;

  int * max_buffer_size;

  gavl_dictionary_t * m;
  } query_t;

/* Node events */

 
static void node_event_info(void *data, const struct pw_node_info *info)
  {
  query_t *qd = data;

  //  fprintf(stderr, "Got node %p %d\n", (void*)info->change_mask, info->n_params);
  
#if 0
    //    struct node_query_result *res = qd->result;
    //    res->id = info->id;
 
    const char *name = spa_dict_lookup(info->props, PW_KEY_NODE_NAME);
    if (name)
        snprintf(res->name, sizeof(res->name), "%s", name);
 
    const char *media_class = spa_dict_lookup(info->props, PW_KEY_MEDIA_CLASS);
    if (media_class)
        snprintf(res->media_class, sizeof(res->media_class), "%s", media_class);
#endif
    
    /* We now know which params exist, but not their values yet - the
     * values arrive asynchronously via node_event_param() below. Ask
     * for every param the node reports as readable. */
    if (info->change_mask & PW_NODE_CHANGE_MASK_PARAMS)
      {
      for(uint32_t i = 0; i < info->n_params; i++)
        {
        if(!(info->params[i].flags & SPA_PARAM_INFO_READ))
          continue;
        pw_node_enum_params(qd->node,
                            0,                    /* seq, unused here */
                            info->params[i].id,
                            0, UINT32_MAX,         /* start, num */
                            NULL);                 /* no filter */
        }
      }
    qd->pending_seq = pw_core_sync(qd->core, PW_ID_CORE, 0);
  }
 
static void node_event_param(void *data, int seq, uint32_t id,
                              uint32_t index, uint32_t next,
                              const struct spa_pod *param)
  {
  query_t *qd = data;

  //  fprintf(stderr, "node_event_param %d %d %d %d\n", seq, id, index, next);
  
  //  struct node_query_result *res = qd->result;
  //  struct spa_pod *copy;
 
  if(param == NULL)
    return;

  switch(id)
    {
    case SPA_PARAM_EnumFormat:
      {
      uint32_t media_type, media_subtype;
      const struct spa_pod_prop *prop;
      const struct spa_pod *val;
      const struct spa_pod_object *obj = (const struct spa_pod_object *)param;

      //      fprintf(stderr, "SPA_PARAM_EnumFormat\n");
      
      if(spa_format_parse(param, &media_type, &media_subtype) < 0)
        return;
      if((media_type != SPA_MEDIA_TYPE_audio) || (media_subtype != SPA_MEDIA_SUBTYPE_raw))
        return;

      SPA_POD_OBJECT_FOREACH(obj, prop)
        {
        switch(prop->key)
          {
          case SPA_FORMAT_AUDIO_rate:
            {
            //            fprintf(stderr, "Got rate\n");
            
            val = &prop->value;

            if(spa_pod_is_int(val))
              {
              int32_t rate;
              spa_pod_get_int(val, &rate);
              // printf("Got rate (fixed): %d\n", rate);
              qd->afmt->samplerate = rate;
              }
            else if(spa_pod_is_choice(val))
              {
              const struct spa_pod_choice *choice = (const struct spa_pod_choice *)val;
              uint32_t n_vals = SPA_POD_CHOICE_N_VALUES(val);

              if(spa_pod_is_int(&choice->body.child) && (n_vals >= 1))
                {
                int32_t *vals   = (int32_t *)SPA_POD_CHOICE_VALUES(val);
                qd->afmt->samplerate = vals[0];
                
                // printf("rate: default=%d Hz\n", vals[0]);
                }
              }
            }
            break;
          case SPA_FORMAT_AUDIO_format:
            {
            //            fprintf(stderr, "Got format\n");
            
            val = &prop->value;
            if(spa_pod_is_id(val))
              {
              uint32_t id;
              spa_pod_get_id(val, &id);
              //              printf("Format: %s\n", spa_debug_type_find_name(spa_type_audio_format, id));

              bg_pipewire_sample_format_from_spa(id, qd->afmt);
              }
            else if(spa_pod_is_choice(val))
              {
              const struct spa_pod_choice *choice = (const struct spa_pod_choice *)val;
              uint32_t n_vals = SPA_POD_CHOICE_N_VALUES(val);

              if(spa_pod_is_id(&choice->body.child) && (n_vals >= 1))
                {
                uint32_t *vals   = (uint32_t *)SPA_POD_CHOICE_VALUES(val);
                bg_pipewire_sample_format_from_spa(vals[0], qd->afmt);
                //                printf("Format: %s\n", spa_debug_type_find_name(spa_type_audio_format, vals[0]));
                }
              
              }
            break;
            }
          case SPA_FORMAT_AUDIO_position:
            {
            uint32_t n_vals;
            uint32_t *vals;
            
            //            printf("Got position\n");
            val = &prop->value;
            //            spa_debug_pod(0, NULL, val);

            if(spa_pod_is_array(val) &&
               spa_pod_is_id(SPA_POD_ARRAY_CHILD(val)))
              {
              n_vals = SPA_POD_ARRAY_N_VALUES(val);
              vals = (uint32_t *)SPA_POD_ARRAY_VALUES(val);
              bg_pipewire_channel_positions_from_spa(vals, n_vals, qd->afmt);
              }
            
            if(spa_pod_is_choice(val))
              {
              const struct spa_pod_choice *choice = (const struct spa_pod_choice *)val;
              uint32_t n_vals = SPA_POD_CHOICE_N_VALUES(val);

              if(spa_pod_is_id(&choice->body.child) && (n_vals >= 1))
                {
                uint32_t *vals   = (uint32_t *)SPA_POD_CHOICE_VALUES(val);
                //bg_pipewire_sample_format_from_spa(vals[0], qd->afmt);

                bg_pipewire_channel_positions_from_spa(vals, n_vals, qd->afmt);
                }
              }
            
            
            }
            break;
          case SPA_FORMAT_AUDIO_channels:
            {
            int32_t num = 0;
            val = &prop->value;
            
            //            printf("Got channels\n");
            //            spa_debug_pod(0, NULL, val);
            
            if(spa_pod_is_int(val))
              {
              spa_pod_get_int(val, &num);
              //              printf("Got %d channels\n", num);
              qd->afmt->num_channels = num;
              }
            else if(spa_pod_is_choice(val))
              {
              const struct spa_pod_choice *choice = (const struct spa_pod_choice *)val;
              uint32_t n_vals = SPA_POD_CHOICE_N_VALUES(val);

              if(spa_pod_is_int(&choice->body.child) && (n_vals >= 1))
                {
                int32_t *vals   = (int32_t *)SPA_POD_CHOICE_VALUES(val);
                //                printf("Got channels (choice): %d\n", vals[0]);
                qd->afmt->num_channels = vals[0];
                }
              
              }
            break;
            }
          default:
            //            spa_debug_pod(0, NULL, &prop->value);
            break;
            
          }
        }
      }
      break;
    case SPA_PARAM_Format:
      {
      fprintf(stderr, "SPA_PARAM_Format\n");

      
      }
      break;
    }
  
  }
 
static const struct pw_node_events node_events =
  {
    PW_VERSION_NODE_EVENTS,
    .info = node_event_info,
    .param = node_event_param,
  };

/* Core events */

static void core_event_done(void *data, uint32_t id, int seq)
  {
  query_t *qd = data;
  
  if(id == PW_ID_CORE && seq == qd->pending_seq)
    pw_main_loop_quit(qd->loop);
  }
 
static void core_event_error(void *data, uint32_t id, int seq,
                              int res, const char *message)
  {
  query_t *qd = data;
  //  fprintf(stderr, "pipewire error: id=%u seq=%d res=%d (%s): %s\n",
  //          id, seq, res, spa_strerror(res), message);
  qd->error = 1;
  pw_main_loop_quit(qd->loop);
  }

static void on_core_info(void *data, const struct pw_core_info *info)
  {
  query_t *qd = data;
  //  printf("Core id=%u, name=%s, version=%s\n",
  //         info->id, info->name, info->version);
  
  /* info->props is a struct spa_dict * (or NULL if none sent yet) */
  if(info->props)
    {
    //    printf("Core properties:\n");
    //    spa_debug_dict(0, info->props);   /* quick dump, from earlier answer */
    
    const char * var;

    //    fprintf(stderr, "Got core\n");
    //    spa_debug_dict(0, info->props);
    
    var = spa_dict_lookup(info->props, "default.clock.quantum-limit");
    if(var)
      {
      //      fprintf(stderr, "Got quantum limit %s\n", var);
      if(qd->max_buffer_size)
        *qd->max_buffer_size = atoi(var);
      }
  
    var = spa_dict_lookup(info->props, "default.clock.quantum");
    if(var)
      {
      //      fprintf(stderr, "Got quantum %s\n", var);
      if(qd->afmt)
        qd->afmt->samples_per_frame = atoi(var);
      }



    }
  }

static const struct pw_core_events core_events =
  {
    PW_VERSION_CORE_EVENTS,
    .done = core_event_done,
    .error = core_event_error,
    .info = on_core_info,
  };


/* Registry events */
 
static void registry_event_global(void *data, uint32_t id,
                                   uint32_t permissions, const char *type,
                                   uint32_t version, const struct spa_dict *props)
  {
  query_t *qd = data;
  const char * name;

  //  fprintf(stderr, "registry_event_global %s %s\n", type, spa_dict_lookup(props, PW_KEY_NODE_NAME));

  
  if(qd->found ||
     strcmp(type, PW_TYPE_INTERFACE_Node) ||
     !(name = spa_dict_lookup(props, PW_KEY_NODE_NAME)) ||
     strcmp(name, qd->name))
    return;

  //  fprintf(stderr, "Got device: %s\n", name);
  //  spa_debug_dict(0, props);
  
  qd->found = 1;

  if(qd->m)
    {
    gavl_dictionary_set_string(qd->m, GAVL_META_DEVICE, spa_dict_lookup(props, PW_KEY_NODE_NICK));
    }
  
  qd->node = pw_registry_bind(qd->registry, id, type, PW_VERSION_NODE, 0);
  if(!qd->node)
    {
    qd->error = 1;
    return;
    }
 
  pw_node_add_listener(qd->node, &qd->node_listener, &node_events, qd);

  qd->pending_seq = pw_core_sync(qd->core, PW_ID_CORE, 0);

  }
 
static const struct pw_registry_events registry_events =
  {
    PW_VERSION_REGISTRY_EVENTS,
    .global = registry_event_global,
  };
 

int bg_pipewire_query_device(const char * name, gavl_audio_format_t * afmt,
                             int * max_buffer_size, gavl_dictionary_t * m)
  {
  int ret = 0;
  query_t qd = { 0 };

  pw_init(NULL, NULL);

  qd.name = name;

  qd.afmt = afmt;
  
  qd.loop = pw_main_loop_new(NULL);
  qd.m = m;
  
  if(!qd.loop)
    goto fail;
  
  qd.max_buffer_size = max_buffer_size;
  
  qd.context = pw_context_new(pw_main_loop_get_loop(qd.loop), NULL, 0);

  if(!qd.context)
    goto fail;
  
  qd.core = pw_context_connect(qd.context, NULL, 0);

  if(!qd.core)
    goto fail;

  pw_core_add_listener(qd.core, &qd.core_listener, &core_events, &qd);

  qd.registry = pw_core_get_registry(qd.core, PW_VERSION_REGISTRY, 0);

  if(!qd.registry)
    goto fail;
  
  pw_registry_add_listener(qd.registry, &qd.registry_listener,
                           &registry_events, &qd);

  /* 1st round: Query node */
  qd.pending_seq = pw_core_sync(qd.core, PW_ID_CORE, 0);
  pw_main_loop_run(qd.loop);

  if(qd.error)
    goto fail;

  if(!qd.found)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Didn't find device in registry");
    goto fail;
    }

  ret = 1;

  fail:

  if(qd.node)
    pw_proxy_destroy((struct pw_proxy *)qd.node);

  if(qd.registry)
    pw_proxy_destroy((struct pw_proxy *)qd.registry);

  if(qd.core)
    pw_core_disconnect(qd.core);

  if(qd.context)
    pw_context_destroy(qd.context);

  if(qd.loop)
    pw_main_loop_destroy(qd.loop);

  //  fprintf(stderr, "Got audio format\n");
  //  gavl_audio_format_dump(qd.afmt);
  
  return ret;
  
  }

static const struct
  {
  uint32_t spa;
  gavl_sample_format_t sampleformat;
  gavl_interleave_mode_t interleave;
  }
sampleformats[] = 
  {
    { SPA_AUDIO_FORMAT_S8P,  GAVL_SAMPLE_S8,     GAVL_INTERLEAVE_NONE },
    { SPA_AUDIO_FORMAT_U8P,  GAVL_SAMPLE_U8,     GAVL_INTERLEAVE_NONE },
    { SPA_AUDIO_FORMAT_S16P, GAVL_SAMPLE_S16,    GAVL_INTERLEAVE_NONE },
    { SPA_AUDIO_FORMAT_S32P, GAVL_SAMPLE_S32,    GAVL_INTERLEAVE_NONE },
    { SPA_AUDIO_FORMAT_F32P, GAVL_SAMPLE_FLOAT,  GAVL_INTERLEAVE_NONE },
    { SPA_AUDIO_FORMAT_F64P, GAVL_SAMPLE_DOUBLE, GAVL_INTERLEAVE_NONE },
    { SPA_AUDIO_FORMAT_S8,   GAVL_SAMPLE_S8,     GAVL_INTERLEAVE_ALL  },
    { SPA_AUDIO_FORMAT_S16,  GAVL_SAMPLE_S16,    GAVL_INTERLEAVE_ALL  },
    { SPA_AUDIO_FORMAT_U8,   GAVL_SAMPLE_U8,     GAVL_INTERLEAVE_ALL  },
    { SPA_AUDIO_FORMAT_U16,  GAVL_SAMPLE_U16,    GAVL_INTERLEAVE_ALL  },
    { SPA_AUDIO_FORMAT_S32,  GAVL_SAMPLE_S32,    GAVL_INTERLEAVE_ALL  },
    { SPA_AUDIO_FORMAT_F32,  GAVL_SAMPLE_FLOAT,  GAVL_INTERLEAVE_ALL  },
    { SPA_AUDIO_FORMAT_F64,  GAVL_SAMPLE_DOUBLE, GAVL_INTERLEAVE_ALL  },
    { /* */ },
  };

int bg_pipewire_sample_format_from_spa(uint32_t id, gavl_audio_format_t * fmt)
  {
  int i = 0;
  
  while(sampleformats[i].sampleformat)
    {
    if(sampleformats[i].spa == id)
      {
      fmt->interleave_mode = sampleformats[i].interleave;
      fmt->sample_format = sampleformats[i].sampleformat;
      return 1;
      }
    i++;
    }
  return 0;
  }

static const struct
  {
  uint32_t spa;
  gavl_channel_id_t channel_id;
  }
channel_ids[] = 
  {
    { SPA_AUDIO_CHANNEL_FC,   GAVL_CHID_FRONT_CENTER },       /*!< For mono                                  */
    { SPA_AUDIO_CHANNEL_FL,   GAVL_CHID_FRONT_LEFT },         /*!< Front left                                */
    { SPA_AUDIO_CHANNEL_FR,   GAVL_CHID_FRONT_RIGHT },        /*!< Front right                               */
    { SPA_AUDIO_CHANNEL_FLC,  GAVL_CHID_FRONT_CENTER_LEFT },  /*!< Left of Center                            */
    { SPA_AUDIO_CHANNEL_FRC,  GAVL_CHID_FRONT_CENTER_RIGHT }, /*!< Right of Center                           */
    { SPA_AUDIO_CHANNEL_RL,   GAVL_CHID_REAR_LEFT },          /*!< Rear left                                 */
    { SPA_AUDIO_CHANNEL_RR,   GAVL_CHID_REAR_RIGHT },         /*!< Rear right                                */
    { SPA_AUDIO_CHANNEL_RC,   GAVL_CHID_REAR_CENTER },        /*!< Rear Center                               */
    { SPA_AUDIO_CHANNEL_SL,   GAVL_CHID_SIDE_LEFT },          /*!< Side left                                 */
    { SPA_AUDIO_CHANNEL_SR,   GAVL_CHID_SIDE_RIGHT },         /*!< Side right                                */
    { SPA_AUDIO_CHANNEL_LFE,  GAVL_CHID_LFE },                /*!< Subwoofer                                 */
    { SPA_AUDIO_CHANNEL_AUX0, GAVL_CHID_AUX },                /*!< Additional channel (can be more than one) */
    { /* */ },
  };

int bg_pipewire_channel_positions_from_spa(uint32_t * ids, int num_ids,
                                           gavl_audio_format_t * fmt)
  {
  int i, j;
  for(i = 0; i < num_ids; i++)
    {
    j = 0;
    while(channel_ids[j].channel_id)
      {
      if(ids[i] == channel_ids[j].spa)
        {
        fmt->channel_locations[i] = channel_ids[j].channel_id;
        break;
        }
      j++;
      }

    if(!channel_ids[j].channel_id)
      fmt->channel_locations[i] = GAVL_CHID_AUX;
    }
  return 1;
  }

void bg_pipewire_audio_format_to_spa(const gavl_audio_format_t * fmt,
                                     struct spa_audio_info_raw * spa)
  {
  int i = 0;
  int j;
  
  while(sampleformats[i].sampleformat)
    {
    if((fmt->sample_format == sampleformats[i].sampleformat) &&
       (fmt->interleave_mode == sampleformats[i].interleave))
      {
      spa->format = sampleformats[i].spa;
      break;
      }
    i++;
    }

  spa->rate = fmt->samplerate;
  spa->channels = fmt->num_channels;

  for(i = 0; i < spa->channels; i++)
    {
    j = 0;

    while(channel_ids[j].channel_id)
      {
      if(channel_ids[j].channel_id == fmt->channel_locations[i])
        {
        spa->position[i] = channel_ids[j].spa;
        break;
        }
      j++;
      }
    }
  
  }
   
