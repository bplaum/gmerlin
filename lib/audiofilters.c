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



#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include <config.h>
#include <gmerlin/translation.h>

#include <gmerlin/pluginregistry.h>
// #include <gmerlin/converters.h>


#include <gmerlin/utils.h>

#include <gmerlin/filters.h>

#include <gmerlin/log.h>
#define LOG_DOMAIN "audiofilters"

/* Audio */

typedef struct
  {
  bg_plugin_handle_t * handle;
  bg_fa_plugin_t     * plugin;
  gavl_audio_source_t * out_src;
  } audio_filter_t;

struct bg_audio_filter_chain_s
  {
  bg_msg_sink_t * cmd_sink;
  
  gavl_audio_source_t * out_src; // Last filter element
  gavl_audio_source_t * in_src; // Legacy!!

  int num_filters;
  
  /* Legacy !! */
  int in_samples;
  int out_samples;
  gavl_audio_frame_t * out_frame;
  
  audio_filter_t * filters;
  const bg_gavl_audio_options_t * opt;
  
  bg_parameter_info_t * parameters;

  gavl_array_t filter_arr;
  
  int need_rebuild;
  int need_restart;

  pthread_mutex_t mutex;

  };

int bg_audio_filter_chain_need_restart(bg_audio_filter_chain_t * ch)
  {
  return ch->need_restart || ch->need_rebuild;
  }

static int audio_filter_create(audio_filter_t * f,
                               bg_audio_filter_chain_t * ch,
                               const gavl_dictionary_t * dict)
  {
  if(!(f->handle = bg_plugin_load_with_options(dict)))
    return 0;
  f->plugin = (bg_fa_plugin_t*)f->handle->plugin;
  return 1;
  }

void bg_audio_filter_chain_lock(void * priv)
  {
  bg_audio_filter_chain_t * cnv = priv;
  pthread_mutex_lock(&cnv->mutex);
  }

void bg_audio_filter_chain_unlock(void * priv)
  {
  bg_audio_filter_chain_t * cnv = priv;
  pthread_mutex_unlock(&cnv->mutex);
  }

static void audio_filter_destroy(audio_filter_t * f)
  {
  if(f->handle)
    bg_plugin_unref_nolock(f->handle);
  }

static void destroy_audio_chain(bg_audio_filter_chain_t * ch)
  {
  int i;
  
  /* Destroy previous filters */
  for(i = 0; i < ch->num_filters; i++)
    audio_filter_destroy(&ch->filters[i]);
  if(ch->filters)
    {
    free(ch->filters);
    ch->filters = NULL;
    }
  }

static int bg_audio_filter_chain_rebuild(bg_audio_filter_chain_t * ch)
  {
  int i;
  ch->need_rebuild = 0;
  destroy_audio_chain(ch);
  
  ch->filters = calloc(ch->filter_arr.num_entries, sizeof(*ch->filters));
  ch->num_filters = ch->filter_arr.num_entries;
  
  for(i = 0; i < ch->filter_arr.num_entries; i++)
    {
    if(!audio_filter_create(&ch->filters[i], ch, ch->filter_arr.entries[i].v.dictionary))
      return 0;
    }
  return 1;
  }

static int handle_cmd(void * priv, gavl_msg_t * msg)
  {
  bg_audio_filter_chain_t * ch = priv;
  switch(msg->NS)
    {
    case BG_MSG_NS_PARAMETER:
      switch(msg->ID)
        {
        case BG_MSG_SET_CHAIN_PARAMETER_CTX:
          {
          int idx = 0;
          const char * sub_name = NULL;
          gavl_value_t val;
          audio_filter_t * f;
          
          gavl_value_init(&val);
          bg_msg_get_chain_parameter_ctx(msg, NULL, NULL, &idx, &sub_name, &val);
          
          if(ch->filters)
            {
            f = ch->filters + idx;
            
            if(f->plugin->common.set_parameter)
              {
              f->plugin->common.set_parameter(f->handle->priv, sub_name, &val);
              if(f->plugin->need_restart && f->plugin->need_restart(f->handle->priv))
                ch->need_restart = 1;
              }
            }

          gavl_dictionary_set_nocopy(ch->filter_arr.entries[idx].v.dictionary, sub_name, &val);
          
          }
          break;
        }
      break;
    }
  return 1;
  }

bg_audio_filter_chain_t *
bg_audio_filter_chain_create(const bg_gavl_audio_options_t * opt)
  {
  bg_audio_filter_chain_t * ret;
  ret = calloc(1, sizeof(*ret));
  ret->opt = opt;
  ret->cmd_sink = bg_msg_sink_create(handle_cmd, ret, 1);
  pthread_mutex_init(&ret->mutex, NULL);
  return ret;
  }

static const bg_parameter_info_t params[] =
  {
    {
      .name      = BG_FILTER_CHAIN_PARAM_PLUGINS,
      .long_name = TRS("Audio Filters"),
      .preset_path = "audiofilters",
      .gettext_domain = PACKAGE,
      .gettext_directory = LOCALE_DIR,
      .type = BG_PARAMETER_MULTI_CHAIN,
      .flags = BG_PARAMETER_SYNC,
    },
    { /* End */ }
  };

static void create_audio_parameters(bg_audio_filter_chain_t * ch)
  {
  ch->parameters = bg_parameter_info_copy_array(params);
  bg_plugin_registry_set_parameter_info(bg_plugin_reg,
                                        BG_PLUGIN_FILTER_AUDIO,
                                        BG_PLUGIN_FILTER_1,
                                        ch->parameters);
  }

const bg_parameter_info_t *
bg_audio_filter_chain_get_parameters(bg_audio_filter_chain_t * ch)
  {
  if(!ch->parameters)
    create_audio_parameters(ch);
  return ch->parameters;
  }

void
bg_audio_filter_chain_set_parameter(void * data, const char * name,
                                    const gavl_value_t * val)
  {
  bg_audio_filter_chain_t * ch;
  int i;
  audio_filter_t * f;
  
  ch = (bg_audio_filter_chain_t *)data;
  
  if(!name)
    {
    if(ch->filters)
      {
      for(i = 0; i < ch->filter_arr.num_entries; i++)
        {
        f = ch->filters + i;
        if(f->plugin->common.set_parameter)
          f->plugin->common.set_parameter(f->handle->priv, NULL, NULL);
        }
      }
    return;
    }
  
  if(!strcmp(name, BG_FILTER_CHAIN_PARAM_PLUGINS))
    {
    if(!ch->filter_arr.num_entries && !val->v.array->num_entries)
      return;
    
    if(ch->filter_arr.num_entries != val->v.array->num_entries)
      ch->need_rebuild = 1;
    else
      {
      const char * name1;
      const char * name2;
      
      for(i = 0; i < ch->filter_arr.num_entries; i++)
        {
        name1 = gavl_dictionary_get_string(ch->filter_arr.entries[i].v.dictionary,
                                           BG_CFG_TAG_NAME);
        name2 = gavl_dictionary_get_string(val->v.array->entries[i].v.dictionary,
                                          BG_CFG_TAG_NAME);
        if(!name1 || !name2 || strcmp(name1, name2))
          {
          /* Rebuild chain */
          ch->need_rebuild = 1;
          break;
          }
        }
      }
    
    /* */
    if(!ch->need_rebuild) // Apply parameters
      {
      for(i = 0; i < ch->filter_arr.num_entries; i++)
        {
        if(gavl_dictionary_compare(ch->filter_arr.entries[i].v.dictionary,
                                   val->v.array->entries[i].v.dictionary))
          {
          if(ch->filters)
            {
            f = ch->filters + i;
        
            if(f->plugin->common.set_parameter && f->handle->info->parameters)
              {
              bg_cfg_section_apply(val->v.array->entries[i].v.dictionary,
                                   f->handle->info->parameters,
                                   f->plugin->common.set_parameter,
                                   f->handle->priv);
              if(f->plugin->need_restart && f->plugin->need_restart(f->handle->priv))
                ch->need_restart = 1;

              }
            }

          gavl_dictionary_reset(ch->filter_arr.entries[i].v.dictionary);
          gavl_dictionary_copy(ch->filter_arr.entries[i].v.dictionary,
                               val->v.array->entries[i].v.dictionary);

          }
        }
      }
    else
      {
      gavl_array_reset(&ch->filter_arr);
      gavl_array_copy(&ch->filter_arr, val->v.array);
      }


    }
  
  return;
  }


void bg_audio_filter_chain_destroy(void * ch1)
  {
  bg_audio_filter_chain_t * ch = ch1;
  
  if(ch->parameters)
    bg_parameter_info_destroy_array(ch->parameters);

  if(ch->cmd_sink)
    bg_msg_sink_destroy(ch->cmd_sink);
  
  gavl_array_free(&ch->filter_arr);
  
  destroy_audio_chain(ch);
  pthread_mutex_destroy(&ch->mutex);
  free(ch);
  }

void bg_audio_filter_chain_reset(bg_audio_filter_chain_t * ch)
  {
  int i;
  for(i = 0; i < ch->filter_arr.num_entries; i++)
    {
    if(ch->filters[i].plugin->reset)
      ch->filters[i].plugin->reset(ch->filters[i].handle->priv);
    gavl_audio_source_reset(ch->filters[i].out_src);
    }
  }

gavl_audio_source_t *
bg_audio_filter_chain_connect(bg_audio_filter_chain_t * ch,
                              gavl_audio_source_t * src_orig)
  {
  int i;
  gavl_audio_source_t * src = src_orig;
  
  if(ch->need_rebuild && !bg_audio_filter_chain_rebuild(ch))
    return NULL;
  
  for(i = 0; i < ch->filter_arr.num_entries; i++)
    {
    gavl_audio_options_copy(gavl_audio_source_get_options(src),
                            ch->opt->opt);
    
    ch->filters[i].out_src =
      ch->filters[i].plugin->connect(ch->filters[i].handle->priv,
                                     src, ch->opt->opt);
    src = ch->filters[i].out_src;
    }
  
  ch->out_src = src;

  if(ch->out_src != src_orig)
    gavl_audio_source_set_lock_funcs(ch->out_src,
                                     bg_audio_filter_chain_lock,
                                     bg_audio_filter_chain_unlock,
                                     ch);
  return ch->out_src;
  }

bg_msg_sink_t * bg_audio_filter_chain_get_cmd_sink(bg_audio_filter_chain_t * ch)
  {
  return ch->cmd_sink;
  }
