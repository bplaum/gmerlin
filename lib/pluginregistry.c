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



#include <dlfcn.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <limits.h>
#include <errno.h>
#include <fcntl.h>
#include <glob.h>

#include <config.h>

#include <gavl/metatags.h>


#include <gmerlin/cfg_registry.h>
#include <gmerlin/pluginregistry.h>
#include <pluginreg_priv.h>
#include <config.h>
#include <gmerlin/utils.h>
#include <gmerlin/singlepic.h>
#include <gmerlin/state.h>
#include <gmerlin/xmlutils.h>
#include <gmerlin/bggavl.h>
// #include <gmerlin/bgplug.h>
#include <gmerlin/cmdline.h>

// #include <gavfenc.h>

#include <gmerlin/translation.h>

#include <gmerlin/log.h>
#include <gmerlin/filters.h>
#include <gavl/http.h>

#include <bgladspa.h>
#include <bgfrei0r.h>

#include <ovl2text.h>

#undef HAVE_LV
#define KEEP_OPEN

#ifdef HAVE_LV
#include <bglv.h>
#endif

#define MAX_REDIRECTIONS 5


/* TODO Make this configurable */
static const char * load_blacklist_ext   =
  "sh log rar zip gz bz2 7z tar txt html htm pdf doc docx ppt pptx xls xlsx sub idx db nfo sqlite part "
  "lib so o a exe dll ocx bmk pl py ps eps tex dvi";


static const char * load_blacklist_names  = "README lock";

#define LOG_DOMAIN "pluginregistry"

#define TO_VIDEO "$to_video"

#define SECTION_MASK (BG_PLUGIN_INPUT|BG_PLUGIN_IMAGE_READER)

static int
get_multipart_edl(const gavl_dictionary_t * track, gavl_dictionary_t * edl);

static void
set_locations(gavl_dictionary_t * dict, const char * location);

#define FLAG_CHANGED      (1<<0)
#define FLAG_HAVE_CONFIG  (1<<1)

static struct
  {
  const char * name;
  bg_plugin_info_t *        (*get_info)(void);
  const bg_plugin_common_t* (*get_plugin)(void);
  void *                    (*create)(void);
  }
meta_plugins[] =
  {
    {
      bg_singlepic_stills_input_name,
      bg_singlepic_stills_input_info,
      bg_singlepic_stills_input_get,
      bg_singlepic_stills_input_create,
    },
    {
      bg_singlepic_input_name,
      bg_singlepic_input_info,
      bg_singlepic_input_get,
      bg_singlepic_input_create,
    },
    {
      bg_singlepic_encoder_name,
      bg_singlepic_encoder_info,
      bg_singlepic_encoder_get,
      bg_singlepic_encoder_create,
    },
    {
      bg_rb_plugin_name,
      bg_rb_plugin_get_info,
      bg_rb_plugin_get,
      bg_rb_plugin_create,
    },
    { /* End */ }
  };


struct bg_plugin_registry_s
  {
  bg_plugin_info_t * entries;
  int flags;
  
  gavl_dictionary_t * state;
  pthread_mutex_t state_mutex;

  gavl_array_t * input_mimetypes;
  gavl_array_t * input_protocols;

  gavl_array_t * input_extensions;
  gavl_array_t * image_extensions;
  
  /* Configuration contexts for plugins */
  bg_cfg_ctx_t cfg_ir;
  bg_cfg_ctx_t cfg_fa;
  bg_cfg_ctx_t cfg_fv;
  bg_cfg_ctx_t cfg_i;
  bg_cfg_ctx_t cfg_vis;
  
  bg_msg_sink_t * cfg_sink;
  
  };

static int handle_cfg_message(void * priv, gavl_msg_t * msg)
  {
  switch(msg->NS)
    {
    case BG_MSG_NS_PARAMETER:
      switch(msg->ID)
        {
        case BG_CMD_SET_PARAMETER:
          {
          const char * name;
          gavl_value_t val;

          gavl_value_init(&val);
          bg_msg_get_parameter(msg, &name, &val);

          if(name)
            {
            gavl_dictionary_t * cfg_section;
            const char * ctx;
            ctx = gavl_dictionary_get_string(&msg->header, GAVL_MSG_CONTEXT_ID);
            
            cfg_section = bg_cfg_registry_find_section(bg_cfg_registry, BG_PLUGIN_CONFIG);
            cfg_section = bg_cfg_section_find_subsection(cfg_section, ctx);
            gavl_dictionary_set_nocopy(cfg_section, name, &val);
            }
          
          gavl_value_free(&val);
          }
          break;
        }
      break;
    }
  return 1;
  }

static void init_plugin_config(bg_cfg_ctx_t * ctx,
                               const char * long_name,
                               gavl_parameter_type_t type,
                               bg_plugin_type_t plugin_type)
  {
  bg_cfg_ctx_init(ctx, NULL,
                  bg_plugin_type_to_string(plugin_type),
                  long_name, NULL, NULL);

  ctx->s = bg_cfg_registry_find_section(bg_cfg_registry, BG_PLUGIN_CONFIG);
  ctx->s = bg_cfg_section_find_subsection(ctx->s, ctx->name);
  
  ctx->parameters_priv = calloc(2, sizeof(*ctx->parameters_priv));
  ctx->parameters_priv[0].name = gavl_strdup(BG_PLUGIN_CONFIG_PLUGIN);
  ctx->parameters_priv[0].long_name = gavl_strdup(long_name);
  ctx->parameters_priv[0].type = type;
  
  bg_plugin_registry_set_parameter_info(bg_plugin_reg, 
                                        plugin_type, 0,
                                        &ctx->parameters_priv[0]);

  ctx->parameters = ctx->parameters_priv;
  
  ctx->sink = bg_plugin_reg->cfg_sink;

  if(type == GAVL_PARAMETER_MULTI_LIST)
    bg_cfg_section_create_items(ctx->s, ctx->parameters);
  }

static void plugin_config_init()
  {
  if(bg_plugin_reg->flags & FLAG_HAVE_CONFIG)
    return;
  
  bg_plugin_reg->flags |= FLAG_HAVE_CONFIG;

  bg_plugin_reg->cfg_sink = bg_msg_sink_create(handle_cfg_message, NULL, 1);
  
  init_plugin_config(&bg_plugin_reg->cfg_ir, TR("Image readers"),
                     GAVL_PARAMETER_MULTI_LIST, BG_PLUGIN_IMAGE_READER);

  init_plugin_config(&bg_plugin_reg->cfg_fa, TR("Audio filters"),
                     GAVL_PARAMETER_MULTI_CHAIN, BG_PLUGIN_FILTER_AUDIO);

  init_plugin_config(&bg_plugin_reg->cfg_fv, TR("Video filters"),
                     GAVL_PARAMETER_MULTI_CHAIN, BG_PLUGIN_FILTER_VIDEO);

  init_plugin_config(&bg_plugin_reg->cfg_i, TR("Input plugins"),
                     GAVL_PARAMETER_MULTI_LIST, BG_PLUGIN_INPUT);

  init_plugin_config(&bg_plugin_reg->cfg_vis, TR("Visualizations"),
                     GAVL_PARAMETER_MULTI_LIST, BG_PLUGIN_VISUALIZATION);

  //  fprintf(stderr, "Initialized plugin config\n");
  //  gavl_dictionary_dump(bg_plugin_reg->cfg_vis.s, 2);
  }

bg_cfg_ctx_t * bg_plugin_config_get_ctx(bg_plugin_type_t type)
  {
  plugin_config_init();
  
  switch(type)
    {
    case BG_PLUGIN_INPUT:        //!< Media input
      return &bg_plugin_reg->cfg_i;
      break;
    case BG_PLUGIN_IMAGE_READER: //!< Image reader
      return &bg_plugin_reg->cfg_ir;
      break;
    case BG_PLUGIN_FILTER_AUDIO: //!< Audio filter
      return &bg_plugin_reg->cfg_fa;
      break;
    case BG_PLUGIN_FILTER_VIDEO: //!< Video filter
      return &bg_plugin_reg->cfg_fv;
      break;
    case BG_PLUGIN_VISUALIZATION: //!< Visual
      return &bg_plugin_reg->cfg_vis;
      break;
    default:
      return NULL;
    }
  return NULL;
  }

const gavl_dictionary_t * bg_plugin_config_get_section(bg_plugin_type_t type)
  {
  bg_cfg_ctx_t * ctx;

  if((ctx = bg_plugin_config_get_ctx(type)))
    return ctx->s;
  else
    return NULL;
  }

void bg_plugin_registry_set_state(bg_plugin_registry_t* plugin_reg, gavl_dictionary_t * state)
  {
  plugin_reg->state = state;
  }

static int handle_ext_message(void * priv, gavl_msg_t * msg)
  {
  bg_plugin_handle_t * h = priv;
  /* Maybe do something here? */
  
  //  fprintf(stderr, "handle_ext_message\n");
  //  gavl_msg_dump(msg, 2);

  
  if(h->control.cmd_sink)
    bg_msg_sink_put_copy(h->control.cmd_sink, msg);
  return 1;
  }

static int handle_plugin_message(void * priv, gavl_msg_t * msg)
  {
  bg_plugin_handle_t * h = priv;
  
  switch(msg->NS)
    {
    case BG_MSG_NS_STATE:
      switch(msg->ID)
        {
        case BG_MSG_STATE_CHANGED:
          {
#if 1
          int last;
          const char * ctx_p = NULL;
          const char * var_p = NULL;
          gavl_value_t val;
          
          gavl_value_init(&val);
          gavl_msg_get_state(msg, &last, &ctx_p, &var_p, &val,
                           bg_plugin_reg->state);
          
          // gavl_log(GAVL_LOG_DEBUG, LOG_DOMAIN, "Storing plugin state %s %s", ctx_p, var_p);

          //          fprintf(stderr, "Storing plugin state %s %s ", ctx_p, var_p);
          //          gavl_value_dump(&val, 0);
          //          fprintf(stderr, "\n");
          
          gavl_value_free(&val);
#endif
          }
          break;
        }
      break;
    }

  //  fprintf(stderr, "Got plugin message:\n");
  //  gavl_msg_dump(msg, 2);
  
  bg_msg_sink_put_copy(h->ctrl_ext.evt_sink, msg);
  
  return 1;
  }


void bg_plugin_info_destroy(bg_plugin_info_t * info)
  {
  gavl_dictionary_free(&info->dict);
  

  if(info->module_filename)
    free(info->module_filename);
  if(info->cmp_name)
    free(info->cmp_name);
  if(info->compressions)
    free(info->compressions);
  if(info->codec_tags)
    free(info->codec_tags);
  
  if(info->parameters)
    bg_parameter_info_destroy_array(info->parameters);
  if(info->audio_parameters)
    bg_parameter_info_destroy_array(info->audio_parameters);
  if(info->video_parameters)
    bg_parameter_info_destroy_array(info->video_parameters);
  if(info->text_parameters)
    bg_parameter_info_destroy_array(info->text_parameters);
  if(info->overlay_parameters)
    bg_parameter_info_destroy_array(info->overlay_parameters);

  free(info);
  }

static void free_info_list(bg_plugin_info_t * entries)
  {
  bg_plugin_info_t * info;
  
  info = entries;

  while(info)
    {
    entries = info->next;
    bg_plugin_info_destroy(info);
    info = entries;
    }
  }

static void make_cmp_name(bg_plugin_info_t * i)
  {
  char * tmp_string;
  int len;
  const char * gettext_domain = bg_plugin_info_get_gettext_domain(i);
  bg_bindtextdomain(gettext_domain,
                    bg_plugin_info_get_gettext_directory(i));
  
  tmp_string = TRD(bg_plugin_info_get_long_name(i),
                   gettext_domain);
  
  len = strxfrm(NULL, tmp_string, 0);
  i->cmp_name = malloc(len+1);
  strxfrm(i->cmp_name, tmp_string, len+1);
  
  }

static int compare_swap(bg_plugin_info_t * i1,
                        bg_plugin_info_t * i2)
  {
  if((i1->flags & BG_PLUGIN_FILTER_1) &&
     (i2->flags & BG_PLUGIN_FILTER_1))
    {
    if(!i1->cmp_name)
      {
      make_cmp_name(i1);
      }
    if(!i2->cmp_name)
      {
      make_cmp_name(i2);
      }

    return strcmp(i1->cmp_name, i2->cmp_name) > 0;
    }
  else if((!(i1->flags & BG_PLUGIN_FILTER_1)) &&
          (!(i2->flags & BG_PLUGIN_FILTER_1)))
    {
    return i1->priority < i2->priority;
    }
  else if((!(i1->flags & BG_PLUGIN_FILTER_1)) &&
          (i2->flags & BG_PLUGIN_FILTER_1))
    return 1;
  
  return 0;
  }
                           

static bg_plugin_info_t * sort_by_priority(bg_plugin_info_t * list)
  {
  int i, j;
  bg_plugin_info_t * info;
  bg_plugin_info_t ** arr;
  int num_plugins = 0;
  int keep_going;

  if(NULL==list)
    return NULL;
  
  /* Count plugins */

  info = list;
  while(info)
    {
    num_plugins++;
    info = info->next;
    }

  /* Allocate array */
  arr = malloc(num_plugins * sizeof(*arr));
  info = list;
  for(i = 0; i < num_plugins; i++)
    {
    arr[i] = info;
    info = info->next;
    }

  /* Bubblesort */

  for(i = 0; i < num_plugins - 1; i++)
    {
    keep_going = 0;
    for(j = num_plugins-1; j > i; j--)
      {
      if(compare_swap(arr[j-1], arr[j]))
        {
        info  = arr[j];
        arr[j]   = arr[j-1];
        arr[j-1] = info;
        keep_going = 1;
        }
      }
    if(!keep_going)
      break;
    }

  /* Rechain */

  for(i = 0; i < num_plugins-1; i++)
    arr[i]->next = arr[i+1];
  if(num_plugins>0)
    arr[num_plugins-1]->next = NULL;
  list = arr[0];
  /* Free array */
  free(arr);
  
  return list;
  }

static bg_plugin_info_t *
find_by_dll(bg_plugin_info_t * info, const char * filename)
  {
  while(info)
    {
    if(info->module_filename && !strcmp(info->module_filename, filename))
      return info;
    info = info->next;
    }
  return NULL;
  }

static bg_plugin_info_t *
find_by_name(bg_plugin_info_t * info, const char * name)
  {
  while(info)
    {
    if(!strcmp(bg_plugin_info_get_name(info), name))
      return info;
    info = info->next;
    }
  return NULL;
  }

const bg_plugin_info_t * bg_plugin_find_by_name(const char * name)
  {
  return find_by_name(bg_plugin_reg->entries, name);
  }

const bg_plugin_info_t * bg_plugin_find_by_protocol(const char * protocol, int type_mask)
  {
  const bg_plugin_info_t * ret = NULL;
  char * protocol_priv = NULL;
  const char * pos;
  const bg_plugin_info_t * info = bg_plugin_reg->entries;
  const gavl_array_t * protocols;
  
  if((pos = strstr(protocol, "://")))
    {
    protocol_priv = gavl_strndup(protocol, pos);
    protocol = protocol_priv;
    }
  
  while(info)
    {
    if((info->type & type_mask) &&
       (protocols = bg_plugin_info_get_protocols(info)) &&
       (gavl_string_array_indexof(protocols, protocol) >= 0))
      {
      ret = info;
      break;
      }
    info = info->next;
    }
  
  if(protocol_priv)
    free(protocol_priv);
  return ret;
  }

const bg_plugin_info_t * bg_plugin_find_by_filename(const char * filename,
                                                    int typemask)
  {
  char * extension;
  bg_plugin_info_t * info, *ret = NULL;
  int max_priority = BG_PLUGIN_PRIORITY_MIN - 1;
  const gavl_array_t * extensions;
  
  if(!filename)
    return NULL;
  
  
  info = bg_plugin_reg->entries;
  extension = strrchr(filename, '.');

  if(!extension)
    {
    return NULL;
    }
  extension++;
  
  
  while(info)
    {
    if(!(info->type & typemask) ||
       !(extensions = bg_plugin_info_get_extensions(info)))
      {
      info = info->next;
      continue;
      }
    //    fprintf(stderr, "Trying: %s\n", bg_plugin_info_get_name(info));
    if(gavl_string_array_indexof_i(extensions, extension) >= 0)
      {
      if(max_priority < info->priority)
        {
        max_priority = info->priority;
        ret = info;
        }
      // return info;
      }
#if 0
    else
      {
      fprintf(stderr, "Extension mismatch: %s\n", bg_plugin_info_get_name(info));
      gavl_array_dump(info->extensions, 2);
      }
#endif
    
    info = info->next;
    }
  return ret;
  }

const bg_plugin_info_t * bg_plugin_find_by_mimetype(const char * mimetype,
                                                    int typemask)
  {
  bg_plugin_info_t * info, *ret = NULL;
  int max_priority = BG_PLUGIN_PRIORITY_MIN - 1;
  const gavl_array_t * mimetypes;
  
  if(!mimetype)
    return NULL;
  
  info = bg_plugin_reg->entries;
  
  while(info)
    {
    if(!(info->type & typemask) ||
       !(mimetypes = bg_plugin_info_get_mimetypes(info)))
      {
      info = info->next;
      continue;
      }
    if(gavl_string_array_indexof_i(mimetypes, mimetype) >= 0)
      {
      if(max_priority < info->priority)
        {
        max_priority = info->priority;
        ret = info;
        }
      // return info;
      }
    info = info->next;
    }
  return ret;
  }

static int
get_multipart_edl(const gavl_dictionary_t * track, gavl_dictionary_t * edl)
  {
  int i;
  
  const gavl_dictionary_t * m;
  gavl_dictionary_t * mi;
  const gavl_dictionary_t * part_track;
  const gavl_dictionary_t * part;
  const gavl_dictionary_t * src;
  
  const char * location;
  const char * klass;

  int num_parts;
  
  /* Build an edl description from a multipart movie so we can play them
     as if they were one file */
  
  if(!(m = gavl_track_get_metadata(track)) ||
     !(klass = gavl_dictionary_get_string(m, GAVL_META_CLASS)) ||
     !(num_parts = gavl_track_get_num_parts(track)))
    return 0;
  
  edl = gavl_append_track(edl, NULL);
  
  for(i = 0; i < num_parts; i++)
    {
    if((part = gavl_track_get_part(track, i)) &&
       (src = bg_plugin_registry_get_src(bg_plugin_reg, part, NULL)) &&
       (location = gavl_dictionary_get_string(src, GAVL_META_URI)))
      {
      mi = bg_plugin_registry_load_media_info(bg_plugin_reg, location, BG_INPUT_FLAG_GET_FORMAT);
      
      /* Assume single track files */
      part_track = gavl_get_track(mi, 0);
      //      fprintf(stderr, "Got movie part:\n");
      //      gavl_dictionary_dump(part_track, 2);
      gavl_edl_append_track_to_timeline(edl, part_track, !i ? 1 : 0 /* init */);
      gavl_dictionary_destroy(mi);
      }
    }

  //  fprintf(stderr, "Got multipart EDL:\n");
  //  gavl_dictionary_dump(edl, 2);
  
  return 1;
  }

const gavl_dictionary_t * bg_plugin_registry_get_src(bg_plugin_registry_t * reg,
                                                     const gavl_dictionary_t * track,
                                                     int * idx_p)
  {
  int idx = 0;
  const char * location = NULL;
  const char * mimetype = NULL;
  const gavl_dictionary_t * ret;
  
  const gavl_dictionary_t * m = gavl_track_get_metadata(track);
  
  //  fprintf(stderr, "bg_plugin_registry_src_from_track\n"); 
  //  gavl_dictionary_dump(track, 2);
  //  fprintf(stderr, "\n"); 

  if(!m)
    return NULL;
  
  while((ret = gavl_metadata_get_src(m, GAVL_META_SRC, idx, &mimetype, &location)))
    {
    const char * pos;
    //    fprintf(stderr, "Trying 1: %s\n", location);

    if(gavl_dictionary_get(ret, GAVL_META_EDL))
      return ret;
    
    if(!strncasecmp(location, "file://", 7))
      location += 7;

    if((location[0] == '/')) // Regular file
      {
      char * tmp_string = gavl_strdup(location);

      gavl_url_get_vars(tmp_string, NULL);
      
      if(access(tmp_string, R_OK))
        {
        gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Couldn't access regular file %s: %s",
               tmp_string, strerror(errno));
        idx++;
        free(tmp_string);
        continue;
        }
      free(tmp_string);
      }

    /* Look for protocol */
    if((pos = strstr(location, "://")))
      {
      char * protocol = gavl_strndup(location, pos);

      if(bg_plugin_find_by_protocol(protocol, BG_PLUGIN_INPUT))
        {
        if(idx_p)
          *idx_p = idx;
        free(protocol);
        return ret;
        }

      free(protocol);
      }
    
    //    fprintf(stderr, "Trying 2: %s\n", location);
    if(!mimetype)
      {
      if(idx_p)
        *idx_p = idx;
      return ret;
      }
    else if(bg_plugin_find_by_mimetype(mimetype, BG_PLUGIN_INPUT))
      {
      if(idx_p)
        *idx_p = idx;
      return ret;
      }
    else
      {
      gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "No plugin found for mimetype %s", mimetype);
      idx++;
      }
    }
  gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Couldn't get readable location from track");
  return NULL;
  }

static const bg_plugin_info_t *
find_by_codec_tag(uint32_t codec_tag, int typemask)
  {
  int i;
  bg_plugin_info_t * info, *ret = NULL;
  int max_priority = BG_PLUGIN_PRIORITY_MIN - 1;

  info = bg_plugin_reg->entries;
  
  while(info)
    {
    if(!(info->type & typemask) ||
       !info->codec_tags)
      {
      info = info->next;
      continue;
      }

    fprintf(stderr, "checking: %s %p %d\n", bg_plugin_info_get_name(info), info->codec_tags, codec_tag);
    
    i = 0;
    while(info->codec_tags[i])
      {
      fprintf(stderr, "checking1: %d\n", info->codec_tags[i]);
      
      if(info->codec_tags[i] == codec_tag)
        {
        if(max_priority < info->priority)
          {
          max_priority = info->priority;
          ret = info;
          }
        }
      i++;
      }
    
    info = info->next;
    }
  return ret;
  
  }

const bg_plugin_info_t *
bg_plugin_find_by_compression(gavl_codec_id_t id,
                              uint32_t codec_tag,
                              int typemask)
  {
  int i;
  bg_plugin_info_t * info, *ret = NULL;
  int max_priority = BG_PLUGIN_PRIORITY_MIN - 1;

  if(id == GAVL_CODEC_ID_EXTENDED)
    return find_by_codec_tag(codec_tag, typemask);
  
  info = bg_plugin_reg->entries;
  
  while(info)
    {
    if(!(info->type & typemask) || !info->compressions)
      {
      info = info->next;
      continue;
      }
    
    i = 0;
    while(info->compressions[i] != GAVL_CODEC_ID_NONE)
      {
      if(info->compressions[i] == id)
        {
        if(max_priority < info->priority)
          {
          max_priority = info->priority;
          ret = info;
          break;
          }
        }
      i++;
      }
    info = info->next;
    }
  return ret;
  }


static bg_plugin_info_t * remove_from_list(bg_plugin_info_t * list,
                                           bg_plugin_info_t * info)
  {
  bg_plugin_info_t * before;
  if(info == list)
    {
    list = list->next;
    info->next = NULL;
    return list;
    }

  before = list;

  while(before->next != info)
    before = before->next;
    
  before->next = info->next;
  info->next = NULL;
  return list;
  }

static bg_plugin_info_t * remove_duplicate(bg_plugin_info_t * list)
  {
  bg_plugin_info_t * info_1, * info_2, * next;
  int del = 0;
  info_1 = list;

  while(info_1)
    {
    /* Check if info_1 is already in the list */
    info_2 = list;
    del = 0;
    
    while(info_2 != info_1)
      {
      const char * name1;
      const char * name2;

      
      
      if((name1 = bg_plugin_info_get_name(info_1)) &&
         (name2 = bg_plugin_info_get_name(info_2)) &&
         !strcmp(name1, name2))
        {
        next = info_1->next;
        list = remove_from_list(list, info_1);
        info_1 = next;
        del = 1;
        break;
        }
      else
        info_2 = info_2->next;
      }
    if(!del)
      info_1 = info_1->next;
    }
  return list;
  }

static bg_plugin_info_t * append_to_list(bg_plugin_info_t * list,
                                         bg_plugin_info_t * info)
  {
  bg_plugin_info_t * end;
  if(!list)
    return info;
  
  end = list;
  while(end->next)
    end = end->next;
  end->next = info;
  return list;
  }

static int check_plugin_version(void * handle)
  {
  int (*get_plugin_api_version)();

  get_plugin_api_version = dlsym(handle, "get_plugin_api_version");
  if(!get_plugin_api_version)
    return 0;

  if(get_plugin_api_version() != BG_PLUGIN_API_VERSION)
    return 0;
  return 1;
  }


bg_plugin_info_t * bg_plugin_info_create(const bg_plugin_common_t * plugin)
  {
  bg_plugin_info_t * new_info;
  new_info = calloc(1, sizeof(*new_info));

  bg_plugin_info_set_gettext_domain(new_info, plugin->gettext_domain);
  bg_plugin_info_set_gettext_directory(new_info, plugin->gettext_directory);

  bg_plugin_info_set_name(new_info, plugin->name);
  bg_plugin_info_set_long_name(new_info, plugin->long_name);

  bg_plugin_info_set_description(new_info,plugin->description);
  
  new_info->type        = plugin->type; 	 
  new_info->flags       = plugin->flags; 	 
  new_info->priority    = plugin->priority;

  if(plugin->type & (BG_PLUGIN_ENCODER_AUDIO|
                     BG_PLUGIN_ENCODER_VIDEO|
                     BG_PLUGIN_ENCODER_TEXT |
                     BG_PLUGIN_ENCODER_OVERLAY |
                     BG_PLUGIN_ENCODER ))
    {
    bg_encoder_plugin_t * encoder;
    encoder = (bg_encoder_plugin_t*)plugin;
    new_info->max_audio_streams = encoder->max_audio_streams;
    new_info->max_video_streams = encoder->max_video_streams;
    new_info->max_text_streams = encoder->max_text_streams;
    new_info->max_overlay_streams = encoder->max_overlay_streams;
    }
  
  return new_info;
  }

static bg_plugin_info_t * plugin_info_create(const bg_plugin_common_t * plugin,
                                             void * plugin_priv,
                                             const char * module_filename)
  {
  bg_plugin_info_t * new_info;
  const bg_parameter_info_t * parameter_info;
  
  new_info = bg_plugin_info_create(plugin);

  new_info->module_filename = gavl_strrep(new_info->module_filename, 	 
                                        module_filename);
  
  if(plugin->get_parameters)
    {
    parameter_info = plugin->get_parameters(plugin_priv);
    if(parameter_info)
      new_info->parameters = bg_parameter_info_copy_array(parameter_info);
    }

  if(plugin->get_extensions)
    {
    gavl_array_t * extensions = bg_plugin_info_set_extensions(new_info);
    bg_string_to_string_array(plugin->get_extensions(plugin_priv), extensions);
    }

  if(plugin->get_protocols)
    {
    gavl_array_t * protocols = bg_plugin_info_set_protocols(new_info);
    bg_string_to_string_array(plugin->get_protocols(plugin_priv), protocols);
    }

  
  if(plugin->type & (BG_PLUGIN_ENCODER_AUDIO|
                     BG_PLUGIN_ENCODER_VIDEO|
                     BG_PLUGIN_ENCODER_TEXT |
                     BG_PLUGIN_ENCODER_OVERLAY |
                     BG_PLUGIN_ENCODER ))
    {
    bg_encoder_plugin_t * encoder;
    encoder = (bg_encoder_plugin_t*)plugin;
    
    if(encoder->get_audio_parameters)
      {
      parameter_info = encoder->get_audio_parameters(plugin_priv);
      new_info->audio_parameters = bg_parameter_info_copy_array(parameter_info);
      }
    
    if(encoder->get_video_parameters)
      {
      parameter_info = encoder->get_video_parameters(plugin_priv);
      new_info->video_parameters = bg_parameter_info_copy_array(parameter_info);
      }
    if(encoder->get_text_parameters)
      {
      parameter_info = encoder->get_text_parameters(plugin_priv);
      new_info->text_parameters = bg_parameter_info_copy_array(parameter_info);
      }
    if(encoder->get_overlay_parameters)
      {
      parameter_info = encoder->get_overlay_parameters(plugin_priv);
      new_info->overlay_parameters =
        bg_parameter_info_copy_array(parameter_info);
      }
    }
  if(plugin->type & BG_PLUGIN_INPUT)
    {
    bg_input_plugin_t  * input;
    input = (bg_input_plugin_t*)plugin;

    if(input->get_mimetypes)
      {
      gavl_array_t * mimetypes = bg_plugin_info_set_mimetypes(new_info);
      bg_string_to_string_array(input->get_mimetypes(plugin_priv), mimetypes);
      }
    
    }
  if(plugin->type & BG_PLUGIN_IMAGE_READER)
    {
    gavl_array_t * mimetypes;
    bg_image_reader_plugin_t  * ir;
    ir = (bg_image_reader_plugin_t*)plugin;

    mimetypes = bg_plugin_info_set_mimetypes(new_info);
    bg_string_to_string_array(ir->mimetypes, mimetypes);
    }
  if(plugin->type & BG_PLUGIN_IMAGE_WRITER)
    {
    gavl_array_t * mimetypes;
    bg_image_writer_plugin_t  * iw;
    iw = (bg_image_writer_plugin_t*)plugin;
    mimetypes = bg_plugin_info_set_mimetypes(new_info);
    bg_string_to_string_array(iw->mimetypes, mimetypes);
    }
  if(plugin->type & (BG_PLUGIN_BACKEND_MDB | BG_PLUGIN_BACKEND_RENDERER))
    {
    bg_backend_plugin_t  * p;
    gavl_array_t * protocols;
    p = (bg_backend_plugin_t*)plugin;

    protocols =bg_plugin_info_set_protocols(new_info);
    bg_string_to_string_array(p->protocol, protocols);
    }
  if(plugin->type & BG_PLUGIN_CONTROL)
    {
    bg_control_plugin_t  * p;
    gavl_array_t * protocols;
    p = (bg_control_plugin_t*)plugin;

    protocols =bg_plugin_info_set_protocols(new_info);
    bg_string_to_string_array(p->protocols, protocols);
    }
  if(plugin->type & (BG_PLUGIN_COMPRESSOR_AUDIO |
                     BG_PLUGIN_COMPRESSOR_VIDEO |
                     BG_PLUGIN_DECOMPRESSOR_AUDIO |
                     BG_PLUGIN_DECOMPRESSOR_VIDEO))
    {
    int num_compressions = 0;
    const gavl_codec_id_t * cmp;
    bg_codec_plugin_t  * p;
    p = (bg_codec_plugin_t*)plugin;

    if(p->get_compressions)
      {
      cmp = p->get_compressions(plugin_priv);
    
      while(cmp[num_compressions] != GAVL_CODEC_ID_NONE)
        num_compressions++;

      new_info->compressions = calloc(num_compressions+1, sizeof(*new_info->compressions));
      memcpy(new_info->compressions, cmp, (num_compressions+1)*sizeof(*new_info->compressions));
      }
    
    }

  return new_info;
  }

static bg_plugin_info_t * get_info(void * test_module,
                                   const char * filename)
  {
  bg_plugin_info_t * new_info;
  bg_plugin_common_t * plugin;
  void * plugin_priv;
  
  if(!check_plugin_version(test_module))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN,
           "Plugin %s has no or wrong version. Recompiling the plugin should fix this.",
           filename);
    return NULL;
    }
  plugin = (bg_plugin_common_t*)(dlsym(test_module, "the_plugin"));
  if(!plugin)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "No symbol the_plugin in %s", filename);
    return NULL;
    }
  if(!plugin->priority)
    gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Plugin %s has zero priority",
           plugin->name);

  
  /* Get parameters */

  if(!(plugin_priv = plugin->create()))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Plugin creation failed");
    return NULL;
    }
  
  new_info = plugin_info_create(plugin, plugin_priv, filename);
  plugin->destroy(plugin_priv);
  
  return new_info;
  }


static bg_plugin_info_t *
scan_directory_internal(const char * directory, bg_plugin_info_t ** _file_info,
                        int * changed,
                        gavl_dictionary_t * cfg_section, bg_plugin_api_t api)
  {
  bg_plugin_info_t * ret;
  DIR * dir;
  struct dirent * entry;
  char filename[FILENAME_MAX];
  struct stat st;
  char * pos;
  void * test_module;
  int old_flags;
  bg_plugin_info_t * file_info;
  bg_plugin_info_t * new_info;
  bg_plugin_info_t * tmp_info;
  
  gavl_dictionary_t * plugin_section;
  gavl_dictionary_t * stream_section;
  if(_file_info)
    file_info = *_file_info;
  else
    file_info = NULL;
  
  ret = NULL;

  /* Some alsa/pulseaudio plugins have some really weird bugs, which
     causes fileno(stdin) (i.e. fd 0) to be non-blocking after the plugin is opened */
  
  old_flags = fcntl(fileno(stdin), F_GETFL);
  
  dir = opendir(directory);
  
  if(!dir)
    return NULL;

  while((entry = readdir(dir)))
    {
    /* Check for the filename */
    
    pos = strrchr(entry->d_name, '.');
    if(!pos)
      continue;
    
    if(strcmp(pos, ".so"))
      continue;
    
    sprintf(filename, "%s/%s", directory, entry->d_name);
    if(stat(filename, &st))
      continue;
    
    /* Check if the plugin is already in the registry */

    new_info = find_by_dll(file_info, filename);
    if(new_info)
      {
      if(st.st_mtime == new_info->module_time)
        {
        file_info = remove_from_list(file_info, new_info);
        
        ret = append_to_list(ret, new_info);
        
        /* Remove other plugins as well */
        while((new_info = find_by_dll(file_info, filename)))
          {
          file_info = remove_from_list(file_info, new_info);
          ret = append_to_list(ret, new_info);
          }
        
        continue;
        }
      //      else
      //        fprintf(stderr, "Blupp\n");
      }
    
    if(!(*changed))
      {
      fprintf(stderr, "Registry changed %s\n", filename);
      *changed = 1;
      closedir(dir);
      if(_file_info)
        *_file_info = file_info;
      return ret;
      }
    
    /* Open the DLL and see what's inside */
    
    test_module = dlopen(filename, RTLD_NOW);
    if(!test_module)
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "dlopen failed for %s: %s",
             filename, dlerror());
      continue;
      }

    switch(api)
      {
      case BG_PLUGIN_API_GMERLIN:
        new_info = get_info(test_module, filename);
        break;
      case BG_PLUGIN_API_LADSPA:
        new_info = bg_ladspa_get_info(test_module, filename);
        break;
      case BG_PLUGIN_API_FREI0R:
        new_info = bg_frei0r_get_info(test_module, filename);
        break;
      case BG_PLUGIN_API_LV:
#ifdef HAVE_LV
        new_info = bg_lv_get_info(filename);
#endif
        break;
      }

    tmp_info = new_info;
    while(tmp_info)
      {
      tmp_info->module_time = st.st_mtime;
      
      /* Create parameter entries in the registry */

      if(!(tmp_info->type & SECTION_MASK))
        {
        tmp_info = tmp_info->next;
        continue;
        }
      plugin_section =
        bg_cfg_section_find_subsection(cfg_section, bg_plugin_info_get_name(tmp_info));
    
      if(tmp_info->parameters)
        {
        bg_cfg_section_create_items(plugin_section,
                                    tmp_info->parameters);
        }
      if(tmp_info->audio_parameters)
        {
        stream_section = bg_cfg_section_find_subsection(plugin_section,
                                                        "$audio");
        
        bg_cfg_section_create_items(stream_section,
                                    tmp_info->audio_parameters);
        }
      if(tmp_info->video_parameters)
        {
        stream_section = bg_cfg_section_find_subsection(plugin_section,
                                                        "$video");
        bg_cfg_section_create_items(stream_section,
                                    tmp_info->video_parameters);
        }
      if(tmp_info->text_parameters)
        {
        stream_section = bg_cfg_section_find_subsection(plugin_section,
                                                        "$text");
        bg_cfg_section_create_items(stream_section,
                                    tmp_info->text_parameters);
        }
      if(tmp_info->overlay_parameters)
        {
        stream_section = bg_cfg_section_find_subsection(plugin_section,
                                                        "$overlay");
        bg_cfg_section_create_items(stream_section,
                                    tmp_info->overlay_parameters);
        }
      
      tmp_info = tmp_info->next;
      }
#ifndef KEEP_OPEN 
    dlclose(test_module);
#endif
    ret = append_to_list(ret, new_info);
    }
  
  closedir(dir);
  if(_file_info)
    *_file_info = file_info;
  
  if(fcntl(fileno(stdin), F_GETFL) != old_flags)
    {
    gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Resetting filedescriptor flags for stdin");
    fcntl(fileno(stdin), F_SETFL, old_flags);
    }
  return ret;
  }

static bg_plugin_info_t *
scan_directory(const char * directory, bg_plugin_info_t ** _file_info,
               gavl_dictionary_t * cfg_section, bg_plugin_api_t api,
               int * reg_flags)
  {
  int changed = 0;
  bg_plugin_info_t * file_info;
  bg_plugin_info_t * file_info_next;
  char * tmp_string, *pos;
  bg_plugin_info_t * ret;
  
  ret = scan_directory_internal(directory, _file_info,
                                &changed, cfg_section, api);
  
  /* Check if there are entries from the file info left */
  
  file_info = *_file_info;
  
  while(file_info)
    {
    tmp_string = gavl_strdup(file_info->module_filename);
    pos = strrchr(tmp_string, '/');
    if(pos) *pos = '\0';
    
    if(!strcmp(tmp_string, directory))
      {
      file_info_next = file_info->next;
      *_file_info = remove_from_list(*_file_info, file_info);
      bg_plugin_info_destroy(file_info);
      file_info = file_info_next;
      changed = 1;
      }
    else
      file_info = file_info->next;
    free(tmp_string);
    }
  
  if(!changed)
    return ret;
  
  *reg_flags |= FLAG_CHANGED;
  
  free_info_list(ret);
  ret = scan_directory_internal(directory, _file_info,
                                &changed, cfg_section, api);
  return ret;
  }

static bg_plugin_info_t * scan_multi(const char * path,
                                     bg_plugin_info_t ** _file_info,
                                     gavl_dictionary_t * section,
                                     bg_plugin_api_t api,
                                     int * reg_flags)
  {
  char ** paths;
  char ** real_paths;
  int num;
  
  bg_plugin_info_t * ret = NULL;
  bg_plugin_info_t * tmp_info;
  int do_scan;
  int i, j;
  paths = gavl_strbreak(path, ':');
  if(!paths)
    return ret;

  num = 0;
  i = 0;
  while(paths[i++])
    num++;
  
  real_paths = calloc(num, sizeof(*real_paths));

  for(i = 0; i < num; i++)
    real_paths[i] = bg_canonical_filename(paths[i]);
  
  for(i = 0; i < num; i++)
    {
    if(!real_paths[i])
      continue;

    do_scan = 1;
    
    for(j = 0; j < i; j++)
      {
      if(real_paths[j] && !strcmp(real_paths[j], real_paths[i]))
        {
        do_scan = 0; /* Path already scanned */
        break;
        }
      }
    
    if(do_scan)
      {
      tmp_info = scan_directory(real_paths[i],
                                _file_info, 
                                section, api, reg_flags);
      if(tmp_info)
        ret = append_to_list(ret, tmp_info);
      }
    }
  gavl_strbreak_free(paths);

  for(i = 0; i < num; i++)
    {
    if(real_paths[i])
      free(real_paths[i]);
    }
  free(real_paths);
  
  return ret;
  }


void
bg_plugin_registry_create_1(gavl_dictionary_t * section)
  {
  int i;
  bg_plugin_registry_t * ret;
  bg_plugin_info_t * file_info;
  bg_plugin_info_t * tmp_info;
  bg_plugin_info_t * tmp_info_next;
  char * filename = NULL;
  char * env;
  char * path;
  int changed;
  
  ret = calloc(1, sizeof(*ret));
  bg_plugin_reg = ret;
  
  pthread_mutex_init(&ret->state_mutex, NULL);
  
  /* Load registry file */

  file_info = NULL; 

  
  filename = bg_plugin_registry_get_cache_file_name();

  if(!access(filename, R_OK))
    file_info = bg_plugin_registry_load(filename);
  else
    ret->flags |= FLAG_CHANGED;

  
  /* Native plugins */
  env = getenv("GMERLIN_PLUGIN_PATH");
  if(env)
    path = gavl_sprintf("%s:%s", env, PLUGIN_DIR);
  else
    path = gavl_sprintf("%s", PLUGIN_DIR);

  changed = 0;
  
  tmp_info = scan_multi(path, &file_info, section, BG_PLUGIN_API_GMERLIN, &changed);
  if(changed)
    ret->flags |= FLAG_CHANGED;

  if(tmp_info)
    ret->entries = append_to_list(ret->entries, tmp_info);
  free(path);
  /* Ladspa plugins */
  
  env = getenv("LADSPA_PATH");
  if(env)
    path = gavl_sprintf("%s:/usr/lib64/ladspa:/usr/local/lib64/ladspa:/usr/lib/ladspa:/usr/local/lib/ladspa", env);
  else
    path = gavl_sprintf("/usr/lib64/ladspa:/usr/local/lib64/ladspa:/usr/lib/ladspa:/usr/local/lib/ladspa");

  tmp_info = scan_multi(path, &file_info, section, BG_PLUGIN_API_LADSPA, &ret->flags);
  if(tmp_info)
    ret->entries = append_to_list(ret->entries, tmp_info);
  
  free(path);
  
  /* Frei0r */
  tmp_info = scan_multi("/usr/lib64/frei0r-1:/usr/local/lib64/frei0r-1:/usr/lib/frei0r-1:/usr/local/lib/frei0r-1", &file_info, 
                        section, BG_PLUGIN_API_FREI0R, &ret->flags);
  if(tmp_info)
    ret->entries = append_to_list(ret->entries, tmp_info);
    
#ifdef HAVE_LV
  tmp_info = scan_directory(LV_PLUGIN_DIR,
                            &file_info, 
                            section, BG_PLUGIN_API_LV, opt, &ret->changed);
  if(tmp_info)
    ret->entries = append_to_list(ret->entries, tmp_info);
#endif
  
  /* Now we have all external plugins, time to create the meta plugins */

  i = 0;
  while(meta_plugins[i].name)
    {
    tmp_info = meta_plugins[i].get_info();
    if(tmp_info)
      ret->entries = append_to_list(ret->entries, tmp_info);
    i++;
    }

  tmp_info = bg_edldec_get_info();
  if(tmp_info)
    ret->entries = append_to_list(ret->entries, tmp_info);

  tmp_info = bg_multi_input_get_info();
  if(tmp_info)
    ret->entries = append_to_list(ret->entries, tmp_info);

#if 0  
  tmp_info = bg_plug_input_get_info();
  if(tmp_info)
    ret->entries = append_to_list(ret->entries, tmp_info);
#endif
  
  if(ret->entries)
    {
    /* Sort */
    ret->entries = sort_by_priority(ret->entries);

    if(ret->flags & FLAG_CHANGED)
      {
      bg_plugin_registry_save(ret->entries);

      /* Also save config registry */
      bg_cfg_registry_save();
      }
    /* Remove duplicate external plugins */
    ret->entries = remove_duplicate(ret->entries);
    }

  /* Kick out unsupported plugins */
  tmp_info = ret->entries;

  while(tmp_info)
    {
    if(tmp_info->flags & BG_PLUGIN_UNSUPPORTED)
      {
      tmp_info_next = tmp_info->next;
      ret->entries = remove_from_list(ret->entries, tmp_info);
      bg_plugin_info_destroy(tmp_info);
      tmp_info = tmp_info_next;
      }
    else
      tmp_info = tmp_info->next;
    }
#if 0 /* Shouldn't be neccesary if the above code is bugfree */
  /* Kick out eventually remaining infos from the file */
  tmp_info = file_info;
  while(tmp_info)
    {
    tmp_info_next = tmp_info->next;
    bg_plugin_info_destroy(tmp_info);
    tmp_info = tmp_info_next;
    }
#endif

  if(filename)
    free(filename);
  
  }


void bg_plugin_registry_destroy_1(bg_plugin_registry_t * reg)
  {
  bg_plugin_info_t * info;

  info = reg->entries;

  while(info)
    {
    reg->entries = info->next;
    bg_plugin_info_destroy(info);
    info = reg->entries;
    }
  pthread_mutex_destroy(&reg->state_mutex);

  if(reg->input_protocols)
    gavl_array_destroy(reg->input_protocols);
  if(reg->input_mimetypes)
    gavl_array_destroy(reg->input_mimetypes);
  if(reg->input_extensions)
    gavl_array_destroy(reg->input_extensions);
  if(reg->image_extensions)
    gavl_array_destroy(reg->image_extensions);
  
  free(reg);
  }

static bg_plugin_info_t * find_by_index(bg_plugin_info_t * info,
                                        int index, uint32_t type_mask,
                                        uint32_t flag_mask)
  {
  int i;
  bg_plugin_info_t * test_info;

  i = 0;
  test_info = info;

  while(test_info)
    {
    if((test_info->type & type_mask) &&
       ((flag_mask == BG_PLUGIN_ALL) ||
        !flag_mask || (test_info->flags & flag_mask)))
      {
      if(i == index)
        return test_info;
      i++;
      }
    test_info = test_info->next;
    }
  return NULL;
  }

#if 0
static bg_plugin_info_t * find_by_priority(bg_plugin_info_t * info,
                                           uint32_t type_mask,
                                           uint32_t flag_mask)
  {
  bg_plugin_info_t * test_info, *ret = NULL;
  int priority_max = BG_PLUGIN_PRIORITY_MIN - 1;
  
  test_info = info;

  while(test_info)
    {
    if((test_info->type & type_mask) &&
       ((flag_mask == BG_PLUGIN_ALL) ||
        (test_info->flags & flag_mask) ||
        (!test_info->flags && !flag_mask)))
      {
      if(priority_max < test_info->priority)
        {
        priority_max = test_info->priority;
        ret = test_info;
        }
      }
    test_info = test_info->next;
    }
  return ret;
  }
#endif

const bg_plugin_info_t *
bg_plugin_find_by_index(int index,
                        uint32_t type_mask, uint32_t flag_mask)
  {
  return find_by_index(bg_plugin_reg->entries, index,
                       type_mask, flag_mask);
  }

int
bg_plugin_get_index(const char * name,
                    uint32_t type_mask, uint32_t flag_mask)
  {
  int ret = 0;
  const bg_plugin_info_t * info;

  while((info = find_by_index(bg_plugin_reg->entries, ret,
                              type_mask, flag_mask)))
    {
    if(!strcmp(bg_plugin_info_get_name(info), name))
      return ret;
    ret++;
    }
  return -1;
  }


int bg_get_num_plugins(uint32_t type_mask, uint32_t flag_mask)
  {
  bg_plugin_info_t * info;
  int ret = 0;
  
  info = bg_plugin_reg->entries;

  while(info)
    {
    if((info->type & type_mask) &&
       (!flag_mask || (info->flags & flag_mask)))
      ret++;

    info = info->next;
    }
  return ret;
  }


static gavl_dictionary_t *
bg_plugin_registry_get_section(bg_plugin_registry_t * reg,
                               const char * plugin_name)
  {
  gavl_dictionary_t * section = bg_cfg_registry_find_section(bg_cfg_registry, "plugins");
  return bg_cfg_section_find_subsection(section, plugin_name);
  }


void bg_plugin_ref(bg_plugin_handle_t * h)
  {
  bg_plugin_lock(h);
  h->refcount++;

  gavl_log(GAVL_LOG_DEBUG, LOG_DOMAIN, "bg_plugin_ref %s: %d",
           bg_plugin_info_get_name(h->info), h->refcount);
  bg_plugin_unlock(h);
  
  }

static void unload_plugin(bg_plugin_handle_t * h)
  {
  if(h->info)
    {
    switch(h->info->api)
      {
      case BG_PLUGIN_API_GMERLIN:
        if(h->priv && h->plugin->destroy)
          h->plugin->destroy(h->priv);
        break;
      case BG_PLUGIN_API_LADSPA:
        bg_ladspa_unload(h);
        break;
      case BG_PLUGIN_API_FREI0R:
        bg_frei0r_unload(h);
        break;
      case BG_PLUGIN_API_LV:
#ifdef HAVE_LV
        bg_lv_unload(h);
#endif
        break;
      }
    }
  else if(h->priv && h->plugin->destroy)
    h->plugin->destroy(h->priv);
  
#ifndef KEEP_OPEN 
  // Some few libs (e.g. the OpenGL lib shipped with NVidia)
  // seem to install pthread cleanup handlers, which point to library
  // functions. dlclosing libraries causes programs to crash
  // mysteriously when the thread lives longer than the plugin.
  //
  // So we leave them open and
  // rely on dlopen() never loading the same lib twice
  if(h->dll_handle)
    dlclose(h->dll_handle);
#endif
  pthread_mutex_destroy(&h->mutex);
  gavl_dictionary_free(&h->state);
  
  //  if(h->evt_sink)
  //    bg_msg_sink_destroy(h->evt_sink);

  bg_control_cleanup(&h->control);
  bg_controllable_cleanup(&h->ctrl_ext);
  
  free(h);
  }

void bg_plugin_unref_nolock(bg_plugin_handle_t * h)
  {
  h->refcount--;
  gavl_log(GAVL_LOG_DEBUG, LOG_DOMAIN, "bg_plugin_unref_nolock %s: %d",
         h->plugin->name, h->refcount);
  if(!h->refcount)
    unload_plugin(h);
  }

void bg_plugin_unref(bg_plugin_handle_t * h)
  {
  int refcount;
  bg_plugin_lock(h);
  h->refcount--;
  gavl_log(GAVL_LOG_DEBUG, LOG_DOMAIN, "bg_plugin_unref %s: %d",
         h->plugin->name, h->refcount);

  refcount = h->refcount;
  bg_plugin_unlock(h);
  if(!refcount)
    unload_plugin(h);
  }

/* Load cover (or poster or whatever) for a track */
gavl_video_frame_t * 
bg_plugin_registry_load_cover_full(bg_plugin_registry_t * r,
                                   gavl_video_format_t * fmt_ret,
                                   const gavl_dictionary_t * metadata,
                                   int max_width, int max_height,
                                   gavl_pixelformat_t pfmt, int shrink)
  {
  gavl_video_converter_t * cnv = NULL;

  double ext_x, ext_y, ar;
  
  const bg_plugin_info_t * info;
  bg_image_reader_plugin_t * ir;
  bg_plugin_handle_t * h = NULL;

  gavl_video_frame_t * frame = NULL;
  
  const gavl_dictionary_t * img;
  gavl_dictionary_t m;

  gavl_video_format_t in_fmt;
  
  char * uri_priv = NULL;
  const char * uri = NULL;
  const char * mimetype = NULL;
  const char * klass = gavl_dictionary_get_string(metadata, GAVL_META_CLASS);
  
  gavl_dictionary_init(&m);

  memset(&in_fmt, 0, sizeof(in_fmt));
  
  if(!klass)
    return NULL;
  
  if(!strcmp(klass, GAVL_META_CLASS_AUDIO_FILE) ||
     !strcmp(klass, GAVL_META_CLASS_SONG))
    {
    if((img = gavl_dictionary_get_image_max(metadata,
                                            GAVL_META_COVER_URL, max_width, max_height, NULL)))
      {
      uri = gavl_dictionary_get_string(img, GAVL_META_URI);
      mimetype = gavl_dictionary_get_string(img, GAVL_META_MIMETYPE);
      }
#if 0 // TODO
    else if((img = gavl_dictionary_get_image_max(metadata,
                                                 GAVL_META_COVER_EMBEDDED, max_width, max_height, NULL)))
      {
      int64_t offset;
      int64_t size;

      if(gavl_dictionary_get_long(img, GAVL_META_COVER_OFFSET, &offset) &&
         gavl_dictionary_get_long(img, GAVL_META_COVER_SIZE, &size) &&
         gavl_metadata_get_src(metadata, GAVL_META_SRC, 0, NULL, &uri))
        {
        gavl_dictionary_t url_vars;
        gavl_dictionary_init(&url_vars);

        gavl_dictionary_set_string_nocopy(&url_vars, "byterange",
                                          gavl_sprintf("%"PRId64"-%"PRId64, offset, offset+size));
        
        uri_priv = gavl_strdup(uri);
        uri_priv = bg_url_append_vars(uri_priv, &url_vars);
        
        uri = uri_priv;
        gavl_dictionary_free(&url_vars);

        mimetype = gavl_dictionary_get_string(img, GAVL_META_MIMETYPE);
        }
      }
#endif
    if(uri)
      goto have_uri;
    
    uri = DATA_DIR"/web/icons/music_nocover.png";
    goto have_uri;
    }
  else if(!strcmp(klass, GAVL_META_CLASS_AUDIO_BROADCAST))
    {
    if(!(uri = gavl_dictionary_get_string(metadata, GAVL_META_LOGO_URL)))
      uri = DATA_DIR"/web/icons/radio_nocover.png";
    goto have_uri;
    }
  else if(!strcmp(klass, GAVL_META_CLASS_VIDEO_FILE) ||
          !strcmp(klass, GAVL_META_CLASS_MOVIE) ||
          !strcmp(klass, GAVL_META_CLASS_MOVIE_PART))
    {
    if((img = gavl_dictionary_get_image_max(metadata,
                                            GAVL_META_POSTER_URL, max_width, max_height, NULL)) &&
       (uri = gavl_dictionary_get_string(img, GAVL_META_URI)))
      {
      mimetype = gavl_dictionary_get_string(img, GAVL_META_MIMETYPE);
      goto have_uri;
      }
    uri = DATA_DIR"/web/icons/movie_nocover.png";
    goto have_uri;
    }
  else if(!strcmp(klass, GAVL_META_CLASS_TV_EPISODE))
    {
    if((img = gavl_dictionary_get_image_max(metadata,
                                            GAVL_META_POSTER_URL, max_width, max_height, NULL)) &&
       (uri = gavl_dictionary_get_string(img, GAVL_META_URI)))
      {
      mimetype = gavl_dictionary_get_string(img, GAVL_META_MIMETYPE);
      goto have_uri;
      }
    uri = DATA_DIR"/web/icons/tv_nocover.png";
    goto have_uri;
    }
  
  if(!uri)
    goto fail;
  
  have_uri:

  if(!mimetype)
    {
    char * pos;
    char * tmp_string;
    
    if((pos = strrchr(uri, '.')))
      {
      pos++;

      tmp_string = gavl_strdup(pos);

      if((pos = strchr(tmp_string, '?')))
        *pos = '\0';
      if((pos = strchr(tmp_string, '#')))
        *pos = '\0';
      
      mimetype = bg_ext_to_mimetype(tmp_string);
      free(tmp_string);
      }
    }
#if 0  
  if(mimetype)
    fprintf(stderr, "load cover %s %s\n", uri, mimetype);
  else
    fprintf(stderr, "load cover %s\n", uri);
#endif
  
  if(!mimetype)
    goto fail;
     
  if(!(info = bg_plugin_find_by_mimetype(mimetype, BG_PLUGIN_IMAGE_READER)))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "No plugin found for mime type %s", mimetype);
    goto fail;
    }
  
  if(!(h = bg_plugin_load(info)))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Loading %s failed", bg_plugin_info_get_name(info));
    goto fail;
    }
  
  ir = (bg_image_reader_plugin_t*)h->plugin;

  if(!ir->read_header(h->priv, uri, &in_fmt))
    goto fail;
  
  frame = gavl_video_frame_create(&in_fmt);

  if(!ir->read_image(h->priv, frame))
    {
    gavl_video_frame_destroy(frame);
    frame = NULL;
    goto fail;
    }
  
  in_fmt.framerate_mode = GAVL_FRAMERATE_STILL;
  in_fmt.timescale = GAVL_TIME_SCALE;
  
  /* Convert */
  

  cnv = gavl_video_converter_create();
  
  if(shrink)
    {
    gavl_video_format_copy(fmt_ret, &in_fmt);
    
    if(pfmt != GAVL_PIXELFORMAT_NONE)
      fmt_ret->pixelformat = pfmt;
    
    ext_x = max_width > 0 ? ((double)in_fmt.image_width / (double)max_width) : 1.0;
    ext_y = max_height > 0 ? ((double)in_fmt.image_height / (double)max_height) : 1.0;
    
    if((ext_x > 1.0) || (ext_y > 1.0))
      {
      ar = (double)in_fmt.image_width / (double)in_fmt.image_height;
      
      if(ext_x > ext_y) // Fit to max_width
        {
        fmt_ret->image_width  = max_width;
        fmt_ret->image_height = (int)((double)max_width / ar + 0.5);
        }
      else // Fit to max_height
        {
        fmt_ret->image_height  = max_height;
        fmt_ret->image_width = (int)((double)max_height * ar + 0.5);
        }
      }
    gavl_video_format_set_frame_size(fmt_ret, 0, 0);
    }
  else
    {
    gavl_rectangle_f_t in_rect;
    gavl_rectangle_i_t out_rect;
    gavl_video_options_t * opt = gavl_video_converter_get_options(cnv);
    
    gavl_rectangle_f_set_all(&in_rect, &in_fmt);
    
    gavl_rectangle_fit_aspect(&out_rect,   // gavl_rectangle_t * r,
                              &in_fmt,  // gavl_video_format_t * src_format,
                              &in_rect,    // gavl_rectangle_t * src_rect,
                              fmt_ret, // gavl_video_format_t * dst_format,
                              1.0,        // float zoom,
                              0.0        // float squeeze
                              );

    gavl_video_options_set_rectangles(opt, &in_rect, &out_rect);
    }
  
  if(gavl_video_converter_init(cnv, &in_fmt, fmt_ret))
    {
    gavl_video_frame_t * frame1;
    frame1 = gavl_video_frame_create(fmt_ret);
    gavl_video_frame_clear(frame1, fmt_ret);
    gavl_video_convert(cnv, frame, frame1);
    gavl_video_frame_destroy(frame);
    frame = frame1;
    }
  
  fail:
  
  if(uri_priv)
    free(uri_priv);

  if(cnv)
    gavl_video_converter_destroy(cnv);
  
  if(h)
    bg_plugin_unref(h);
  
  return frame;
  }

gavl_video_frame_t * 
bg_plugin_registry_load_cover(bg_plugin_registry_t * r,
                              gavl_video_format_t * fmt,
                              const gavl_dictionary_t * track)
  {
  return bg_plugin_registry_load_cover_full(r, fmt, track, -1, -1,
                                            GAVL_PIXELFORMAT_NONE, 1);
  }

gavl_video_frame_t * 
bg_plugin_registry_load_cover_cnv(bg_plugin_registry_t * r,
                                  const gavl_video_format_t * fmt1,
                                  const gavl_dictionary_t * track)
  {
  gavl_video_format_t fmt;
  memset(&fmt, 0, sizeof(fmt));
  gavl_video_format_copy(&fmt, fmt1);
  
  return bg_plugin_registry_load_cover_full(r, &fmt, track, -1, -1,
                                            GAVL_PIXELFORMAT_NONE, 0);
  }


int bg_plugin_registry_probe_image(const char * filename,
                                   gavl_video_format_t * format,
                                   gavl_dictionary_t * m, bg_plugin_handle_t ** h)
  {
  bg_image_reader_plugin_t * ir;
  bg_plugin_handle_t * handle = NULL;
  const gavl_dictionary_t * m_ret;

  const bg_plugin_info_t * info;
  info = bg_plugin_find_by_filename(filename, BG_PLUGIN_IMAGE_READER);

  if(!info)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "No plugin found for image %s", filename);
    return 0;
    }

  handle = bg_plugin_load(info);
  if(!handle)
    return 0;

  ir = (bg_image_reader_plugin_t*)handle->plugin;

  if(!ir->read_header(handle->priv, filename, format))
    {
    bg_plugin_unref(handle);
    return 0;
    }

  if(ir->get_metadata && m && (m_ret = ir->get_metadata(handle->priv)))
    gavl_dictionary_copy(m, m_ret);

  if(h)
    *h = handle;
  else
    bg_plugin_unref(handle);
  return 1;
  }


gavl_video_frame_t *
bg_plugin_registry_load_image(bg_plugin_registry_t * r,
                              const char * filename,
                              gavl_video_format_t * format,
                              gavl_dictionary_t * m)
  {
  bg_image_reader_plugin_t * ir;
  bg_plugin_handle_t * handle = NULL;
  gavl_video_frame_t * ret = NULL;

  // fprintf(stderr, "bg_plugin_registry_load_image\n");
  memset(format, 0, sizeof(*format));
  
  if(!bg_plugin_registry_probe_image(filename, format,
                                     m, &handle))
    goto fail;
  
  ir = (bg_image_reader_plugin_t*)handle->plugin;
  
  ret = gavl_video_frame_create(format);
  if(!ir->read_image(handle->priv, ret))
    goto fail;

  format->framerate_mode = GAVL_FRAMERATE_STILL;
  format->timescale = GAVL_TIME_SCALE;
  
  bg_plugin_unref(handle);
  return ret;

  fail:
  if(ret)
    gavl_video_frame_destroy(ret);
  return NULL;
  }

gavl_video_frame_t *
bg_plugin_registry_load_image_convert(bg_plugin_registry_t * r,
                                      const char * filename,
                                      const gavl_video_format_t * format,
                                      gavl_dictionary_t * m)
  {
  gavl_video_converter_t * cnv;
  gavl_video_format_t in_fmt;
    
  gavl_video_frame_t * in_frame;

  memset(&in_fmt, 0, sizeof(in_fmt));
  
  in_frame = bg_plugin_registry_load_image(r, filename, &in_fmt, m);

  cnv = gavl_video_converter_create();

  if(gavl_video_converter_init(cnv, &in_fmt, format))
    {
    gavl_video_frame_t * out_frame = gavl_video_frame_create(format);
    gavl_video_convert(cnv, in_frame, out_frame);
    gavl_video_frame_destroy(in_frame);
    in_frame = out_frame;
    }
  
  gavl_video_converter_destroy(cnv);
  return in_frame;
  }



void
bg_plugin_registry_save_image(bg_plugin_registry_t * r,
                              const char * filename,
                              gavl_video_frame_t * frame,
                              const gavl_video_format_t * format,
                              const gavl_dictionary_t * m)
  {
  const bg_plugin_info_t * info;
  gavl_video_format_t tmp_format;
  gavl_video_converter_t * cnv;
  bg_image_writer_plugin_t * iw;
  bg_plugin_handle_t * handle = NULL;
  gavl_video_frame_t * tmp_frame = NULL;
  
  info = bg_plugin_find_by_filename(filename, BG_PLUGIN_IMAGE_WRITER);

  cnv = gavl_video_converter_create();
  
  if(!info)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "No plugin found for image %s", filename);
    goto fail;
    }
  
  handle = bg_plugin_load(info);
  if(!handle)
    goto fail;
  
  iw = (bg_image_writer_plugin_t*)(handle->plugin);

  gavl_video_format_copy(&tmp_format, format);
  
  if(!iw->write_header(handle->priv, filename, &tmp_format, m))
    goto fail;

  if(gavl_video_converter_init(cnv, format, &tmp_format))
    {
    tmp_frame = gavl_video_frame_create(&tmp_format);
    gavl_video_convert(cnv, frame, tmp_frame);
    if(!iw->write_image(handle->priv, tmp_frame))
      goto fail;
    }
  else
    {
    if(!iw->write_image(handle->priv, frame))
      goto fail;
    }
  bg_plugin_unref(handle);
  fail:
  if(tmp_frame)
    gavl_video_frame_destroy(tmp_frame);
  gavl_video_converter_destroy(cnv);
  }

bg_plugin_handle_t * bg_plugin_handle_create()
  {
  bg_plugin_handle_t * ret;
  ret = calloc(1, sizeof(*ret));
  pthread_mutex_init(&ret->mutex, NULL);
  return ret;
  }

void bg_plugin_handle_connect_control(bg_plugin_handle_t * ret)
  {
  //  fprintf(stderr, "bg_plugin_handle_connect_control %p %p %s\n", ret, ret->plugin->get_controllable, ret->info->name);
  
  if(ret->plugin->get_controllable &&
     (ret->ctrl_plugin = ret->plugin->get_controllable(ret->priv)))
    {
    bg_control_init(&ret->control, bg_msg_sink_create(handle_plugin_message, ret, 1));
    bg_controllable_connect(ret->ctrl_plugin, &ret->control);
    
    bg_controllable_init(&ret->ctrl_ext,
                         bg_msg_sink_create(handle_ext_message, ret, 1),
                         bg_msg_hub_create(1));   // Owned
    }
  }

  
static bg_plugin_handle_t * load_plugin(const bg_plugin_info_t * info)
  {
  bg_plugin_handle_t * ret;
  
  if(!info)
    return NULL;
  
  ret = bg_plugin_handle_create();
  
  pthread_mutex_init(&ret->mutex, NULL);

  if(info->module_filename)
    {
    if(info->api != BG_PLUGIN_API_LV)
      {
      /* We need all symbols global because some plugins might reference them */
      ret->dll_handle = dlopen(info->module_filename, RTLD_NOW | RTLD_GLOBAL);
      if(!ret->dll_handle)
        {
        gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "dlopen failed for %s: %s", info->module_filename,
               dlerror());
        goto fail;
        }
      }
    
    switch(info->api)
      {
      case BG_PLUGIN_API_GMERLIN:
        if(!check_plugin_version(ret->dll_handle))
          {
          gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Plugin %s has no or wrong version",
                 info->module_filename);
          goto fail;
          }
        ret->plugin = dlsym(ret->dll_handle, "the_plugin");
        if(!ret->plugin)
          {
          gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "dlsym failed for %s: %s",
                 info->module_filename, dlerror());
          goto fail;
          }
        ret->priv = ret->plugin->create();
        break;
      case BG_PLUGIN_API_LADSPA:
        if(!bg_ladspa_load(ret, info))
          goto fail;
        break;
      case BG_PLUGIN_API_FREI0R:
        if(!bg_frei0r_load(ret, info))
          goto fail;
        break;
      case BG_PLUGIN_API_LV:
#ifdef HAVE_LV
        if(!bg_lv_load(ret, bg_plugin_info_get_name(info), info->flags, NULL))
          goto fail;
#endif
        break;
      }
    }
  else
    {
    int i = 0;
    while(meta_plugins[i].name)
      {
      if(!strcmp(meta_plugins[i].name, bg_plugin_info_get_name(info)))
        {
        ret->plugin = meta_plugins[i].get_plugin();
        ret->priv   = meta_plugins[i].create();
        break;
        }
      i++;
      }
    }

  if(!ret->plugin)
    goto fail;
  
  ret->info = info;

  bg_plugin_handle_connect_control(ret);
  
  if(ret->ctrl_plugin)
    {
    if(bg_plugin_reg->state)
      {
      bg_state_apply_ctx(bg_plugin_reg->state,
                         bg_plugin_info_get_name(ret->info),
                         ret->ctrl_ext.cmd_sink, BG_CMD_SET_STATE);
      
      /* Some plugin types have generic state variables also */

      if(info->type == BG_PLUGIN_OUTPUT_VIDEO)
        {
        //      fprintf(stderr, "Apply ov state %p\n", ret->plugin_reg->state);
        //      gavl_dictionary_dump(ret->plugin_reg->state, 2);
      
        bg_state_apply_ctx(bg_plugin_reg->state, BG_STATE_CTX_OV, ret->ctrl_ext.cmd_sink, BG_CMD_SET_STATE);
        }
      }
    }
  else
    bg_controllable_init(&ret->ctrl_ext,
                         bg_msg_sink_create(handle_ext_message, ret, 1),
                         bg_msg_hub_create(1));   // Owned
  
  bg_plugin_ref(ret);
  return ret;

fail:
  pthread_mutex_destroy(&ret->mutex);
  if(ret->dll_handle)
    dlclose(ret->dll_handle);
  free(ret);
  return NULL;
  }

static void apply_parameters(bg_plugin_handle_t * ret, const gavl_dictionary_t * user_params)
  {
  const bg_parameter_info_t * parameters;
  gavl_dictionary_t params;

  gavl_dictionary_t tmp_section;

  gavl_dictionary_init(&tmp_section);
  
  /* Apply saved parameters */

  if(!ret->plugin->get_parameters)
    return;

  parameters = ret->plugin->get_parameters(ret->priv);

  /* Generate default section */
  gavl_dictionary_init(&params);

  gavl_parameter_info_append_static(&params, parameters);
  
  bg_cfg_section_set_from_params(&tmp_section, &params);
  
  if(ret->info->type & SECTION_MASK)
    {
    /* Merge section from plugin registry */
    const gavl_dictionary_t * section =
      bg_plugin_registry_get_section(bg_plugin_reg,
                                     bg_plugin_info_get_name(ret->info));
    bg_cfg_section_merge(&tmp_section, section);
    }
  
  if(user_params)
    {
    bg_cfg_section_merge(&tmp_section, user_params);
    }
  
  gavl_dictionary_set(&tmp_section, BG_CFG_TAG_NAME, NULL);
  bg_cfg_section_apply(&tmp_section, parameters, ret->plugin->set_parameter,
                       ret->priv);
  
  gavl_dictionary_free(&tmp_section);
  gavl_dictionary_free(&params);
  }

bg_plugin_handle_t * bg_plugin_load(const bg_plugin_info_t * info)
  {
  bg_plugin_handle_t * ret;
  ret = load_plugin(info);

  if(ret)
    apply_parameters(ret, NULL);
  return ret;
  }

bg_plugin_handle_t * bg_plugin_load_with_options(const gavl_dictionary_t * dict)
  {
  const char * plugin_name;
  const bg_plugin_info_t * info;
  bg_plugin_handle_t * ret;
  
  if(!dict || !(plugin_name = gavl_dictionary_get_string(dict, BG_CFG_TAG_NAME)))
    return NULL;
  
  if(!(info = bg_plugin_find_by_name(plugin_name)))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Loading plugin %s failed, no such plugin", plugin_name);
    return NULL;
    }
  ret = load_plugin(info);

  if(ret && ret->plugin->set_parameter)
    apply_parameters(ret, dict);
  
  return ret;
  
  }

void bg_plugin_lock(void * p)
  {
  bg_plugin_handle_t * h = p;
  pthread_mutex_lock(&h->mutex);
  }

void bg_plugin_unlock(void * p)
  {
  bg_plugin_handle_t * h = p;
  pthread_mutex_unlock(&h->mutex);
  }

char * bg_get_default_sink_uri(int plugin_type)
  {
  int i, num_plugins;
  
  const char * protocol;
  const bg_plugin_info_t * info;
  const gavl_array_t * protocols;
  
  num_plugins = bg_get_num_plugins(plugin_type, 0);
  for(i = 0; i < num_plugins; i++)
    {
    info = bg_plugin_find_by_index(i, plugin_type, 0);

    /* A bit uglyish, but does what it should and every other
       window system detection would be much more complicated */
    
    if(!strcmp(bg_plugin_info_get_name(info), "ov_x11") && !getenv("DISPLAY"))
      continue;
    else if(!strcmp(bg_plugin_info_get_name(info), "ov_wayland") && !getenv("WAYLAND_DISPLAY"))
      continue;

    if((protocols = bg_plugin_info_get_protocols(info)) &&
       (protocol = gavl_string_array_get(protocols, 0)))
      return gavl_sprintf("%s:///", protocol);
    }
  return NULL;
  }

static void load_input_plugin(bg_plugin_registry_t * reg,
                              const bg_plugin_info_t * info,
                              const gavl_dictionary_t * options,
                              bg_plugin_handle_t ** ret)
  {
  if(!(*ret) || !(*ret)->info || strcmp(bg_plugin_info_get_name((*ret)->info),
                                        bg_plugin_info_get_name(info)))
    {
    if(*ret)
      {
      bg_plugin_unref(*ret);
      *ret = NULL;
      }

    if(options)
      bg_plugin_load_with_options(options);
#if 0
    else if(!strcmp(info->name, "i_bgplug"))
      *ret = bg_input_plugin_create_plug();
#endif
    else
      *ret = bg_plugin_load(info);
    }
  }

/* Detect various database features */

static int check_image(bg_plugin_registry_t * plugin_reg,
                       const char * filename,
                       const char * key,
                       gavl_dictionary_t * dict)
  {
  gavl_video_format_t fmt;
  gavl_dictionary_t m;
  
  memset(&fmt, 0, sizeof(fmt));
  gavl_dictionary_init(&m);
  
  if(access(filename, R_OK) || !bg_plugin_registry_probe_image(filename, &fmt, &m, NULL))
    return 0;
  
  gavl_metadata_add_image_uri(dict, key, fmt.image_width, fmt.image_height,
                              gavl_dictionary_get_string(&m, GAVL_META_MIMETYPE), filename);
  gavl_dictionary_free(&m);
  return 1;
  }

static void detect_album_cover(bg_plugin_registry_t * plugin_reg,
                               const char * path, gavl_dictionary_t * dict)
  {
  char * file = NULL;
  int result;

  if(!path)
    return;
  
  file = gavl_sprintf("%s/cover.jpg", path);
  result = check_image(plugin_reg, file, GAVL_META_COVER_URL, dict);
  free(file);

  if(result)
    return;
  
  }

static int detect_movie_poster(bg_plugin_registry_t * plugin_reg,
                               const char * path, const char * basename, gavl_dictionary_t * dict)
  {
  char * file = NULL;
  int result;

  if(!path || !basename)
    return 0;
  
  file = gavl_sprintf("%s/%s.jpg", path, basename);
  result = check_image(plugin_reg, file, GAVL_META_POSTER_URL, dict);
  free(file);

  return result;
  
  }

static int detect_movie_wallpaper(bg_plugin_registry_t * plugin_reg,
                                   const char * path, const char * basename, gavl_dictionary_t * dict)
  {
  char * file = NULL;
  int result;

  if(!path || !basename)
    return 0;
  
  file = gavl_sprintf("%s/%s.fanart.jpg", path, basename);
  result = check_image(plugin_reg, file, GAVL_META_WALLPAPER_URL, dict);
  free(file);
  
  return result;
  
  }

static char * normalize_genre(char * genre)
  {
  char * pos = genre;

  while(*pos != '\0')
    {
    if(*pos == '-')
      *pos = ' ';
    pos++;
    }
  return bg_capitalize(genre);
  }

static int detect_nfo(const char * path, const char * basename, gavl_dictionary_t * dict)
  {
  int ret = 0;
  char * file = NULL;

  FILE * in = NULL;
  xmlDocPtr doc = NULL;
  xmlNodePtr root;
  xmlNodePtr node;
  int i;

  int num_countries = 0;
  int num_actors    = 0;
  int num_directors = 0;

  if(!path || !basename)
    goto fail;
    
  file = gavl_sprintf("%s/%s.nfo", path, basename);
  
  in = fopen(file, "r");
  if(!in)
    goto fail;

  doc = bg_xml_load_FILE(in);
  if(!doc)
    goto fail;
  
  root = bg_xml_find_doc_child(doc, "movie");
  if(!root)
    root = bg_xml_find_doc_child(doc, "tvshow");

  if(!root)
    goto fail;
  
  node = root->children;

  while(node)
    {
    if(!node->name)
      {
      node = node->next;
      continue;
      }
    if(!BG_XML_STRCMP(node->name, "originaltitle"))
      gavl_dictionary_set_string(dict, GAVL_META_ORIGINAL_TITLE, bg_xml_node_get_text_content(node));
    else if(!BG_XML_STRCMP(node->name, "title"))
      gavl_dictionary_set_string(dict, GAVL_META_TITLE, bg_xml_node_get_text_content(node));
    else if(!BG_XML_STRCMP(node->name, "rating"))
      {
      char * rating;
      char * pos;

      rating = gavl_strdup(bg_xml_node_get_text_content(node));

      if((pos = strchr(rating, ',')))
        *pos = '.';

      gavl_dictionary_set_float(dict, GAVL_META_RATING, strtod(rating, NULL) / 10.0);
      }
    
    else if(!BG_XML_STRCMP(node->name, "outline") || !BG_XML_STRCMP(node->name, "plot"))
      gavl_dictionary_set_string(dict, GAVL_META_PLOT, bg_xml_node_get_text_content(node));
    else if(!BG_XML_STRCMP(node->name, "mpaa"))
      gavl_dictionary_set_string(dict, GAVL_META_PARENTAL_CONTROL, bg_xml_node_get_text_content(node));
#if 0
    else if(!BG_XML_STRCMP(node->name, "year"))
      {
      info->date.year = atoi(bg_xml_node_get_text_content(node));
      info->date.month = 1;
      info->date.day   = 1;
      }
#endif
    else if(!BG_XML_STRCMP(node->name, "genre"))
      {
      char ** genres;

      genres = gavl_strbreak(bg_xml_node_get_text_content(node), ',');

      if(genres)
        {
        gavl_value_t genre_val;
        gavl_value_init(&genre_val);
        
        i = 0;
        while(genres[i])
          {
          gavl_value_set_string_nocopy(&genre_val, bg_strip_space(normalize_genre(genres[i])));

          if(!i)
            gavl_dictionary_set_nocopy(dict, GAVL_META_GENRE, &genre_val);
          else
            {
            gavl_value_t * val_p = gavl_dictionary_get_nc(dict, GAVL_META_GENRE);
            gavl_value_append_nocopy(val_p, &genre_val);
            }
          i++;
          }
        gavl_strbreak_free(genres);
        }
      }
    else if(!BG_XML_STRCMP(node->name, "director"))
      {
      gavl_value_t sub_val;
      gavl_value_t * val_p;

      if(!num_directors)
        gavl_dictionary_set(dict, GAVL_META_DIRECTOR, NULL);
      num_directors++;
      
      gavl_value_init(&sub_val);
      gavl_value_set_string_nocopy(&sub_val, bg_strip_space(gavl_strdup(bg_xml_node_get_text_content(node))));

      if((val_p = gavl_dictionary_get_nc(dict, GAVL_META_DIRECTOR)))
        gavl_value_append_nocopy(val_p, &sub_val);
      else
        gavl_dictionary_set_nocopy(dict, GAVL_META_DIRECTOR, &sub_val);
      }
    else if(!BG_XML_STRCMP(node->name, "actor"))
      {
      xmlNodePtr child = bg_xml_find_node_child(node, "name");

      if(child)
        {
        gavl_value_t sub_val;
        gavl_value_t * val_p;

        if(!num_actors)
          gavl_dictionary_set(dict, GAVL_META_ACTOR, NULL);
        num_actors++;
                
        gavl_value_init(&sub_val);
        gavl_value_set_string_nocopy(&sub_val, bg_strip_space(gavl_strdup(bg_xml_node_get_text_content(child))));
        
        if((val_p = gavl_dictionary_get_nc(dict, GAVL_META_ACTOR)))
          gavl_value_append_nocopy(val_p, &sub_val);
        else
          gavl_dictionary_set_nocopy(dict, GAVL_META_ACTOR, &sub_val);
        }
      }
    else if(!BG_XML_STRCMP(node->name, "country"))
      {
      gavl_value_t sub_val;
      gavl_value_t * val_p;

      if(!num_countries)
        gavl_dictionary_set(dict, GAVL_META_COUNTRY, NULL);
      num_countries++;
      
      gavl_value_init(&sub_val);
      gavl_value_set_string_nocopy(&sub_val, bg_strip_space(gavl_strdup(bg_xml_node_get_text_content(node))));

      if((val_p = gavl_dictionary_get_nc(dict, GAVL_META_COUNTRY)))
        gavl_value_append_nocopy(val_p, &sub_val);
      else
        gavl_dictionary_set_nocopy(dict, GAVL_META_COUNTRY, &sub_val);
      }
    
    node = node->next;
    }

  gavl_dictionary_set_string(dict, GAVL_META_NFO_FILE, file);

  //  fprintf(stderr, "Got NFO %s\n", gavl_dictionary_get_string(dict, GAVL_META_TITLE));

  /* Set country to unknown */
  if(!gavl_dictionary_get(dict, GAVL_META_COUNTRY))
    gavl_dictionary_set_string(dict, GAVL_META_COUNTRY, "Unknown");

  ret = 1;
  fail:
  if(in)
    fclose(in);
  if(doc)
    xmlFreeDoc(doc);
  free(file);
  return ret;
  }

void bg_plugin_registry_get_container_data(bg_plugin_registry_t * plugin_reg,
                                           gavl_dictionary_t * container,
                                           const gavl_dictionary_t * child)
  {
  const char * container_klass;
  const gavl_dictionary_t * child_m;
  gavl_dictionary_t * container_m;
  const char * location;
  const char * pos_c;
  char * pos;

  char * path = NULL;
  char * basename = NULL;

  child_m = gavl_track_get_metadata(child);
  container_m = gavl_track_get_metadata_nc(container);
  container_klass = gavl_dictionary_get_string(container_m, GAVL_META_CLASS);

  if(gavl_metadata_get_src(child_m, GAVL_META_SRC, 0,
                             NULL, &location) &&
     (location[0] == '/'))
    {
    char * pos;

    path = gavl_strdup(location);
    
    if((pos = strrchr(path, '/')))
      {
      *pos = '\0';
      basename = gavl_strdup(pos + 1);

      if((pos = strrchr(basename, '.')))
        *pos = '\0';
      }
    }
  
  if(!strcmp(container_klass, GAVL_META_CLASS_TV_SEASON))
    {
    int season = 0;

    gavl_dictionary_get_int(child_m, GAVL_META_SEASON, &season);
    gavl_dictionary_set_int(container_m, GAVL_META_SEASON, season);
    gavl_dictionary_set_string_nocopy(container_m, GAVL_META_TITLE, gavl_sprintf("Season %d", season));
    
    if(basename)
      {
      pos_c = gavl_detect_episode_tag(basename, NULL, NULL, NULL);
      
      pos = basename + (int)(pos_c - basename);

      while(*pos != '\0')
        {
        if((*pos == 'e') ||
           (*pos == 'E'))
          {
          *pos = '\0';
          break;
          }
        pos++;
        }

      detect_movie_poster(plugin_reg, path, basename, container_m);
      detect_movie_wallpaper(plugin_reg, path, basename, container_m);
      detect_nfo(path, basename, container_m);
      }
    }
  else if(!strcmp(container_klass, GAVL_META_CLASS_TV_SHOW))
    {
    if(basename)
      {
      pos_c = gavl_detect_episode_tag(basename, NULL, NULL, NULL);
      
      pos = basename + (int)(pos_c - basename);
      *pos = '\0';
      gavl_strtrim(basename);
      
      detect_movie_poster(plugin_reg, path, basename, container_m);
      detect_movie_wallpaper(plugin_reg, path, basename, container_m);
      detect_nfo(path, basename, container_m);

      gavl_dictionary_set_string(container_m, GAVL_META_SEARCH_TITLE,
                                 bg_get_search_string(gavl_dictionary_get_string(container_m, GAVL_META_TITLE)));
      }
    }
  else if(!strcmp(container_klass, GAVL_META_CLASS_MUSICALBUM))
    {
    const char * title;

    gavl_dictionary_set(container_m, GAVL_META_ARTIST, gavl_dictionary_get(child_m, GAVL_META_ALBUMARTIST));
    gavl_dictionary_set(container_m, GAVL_META_DATE, gavl_dictionary_get(child_m, GAVL_META_DATE));
    
    title = gavl_dictionary_get_string(child_m, GAVL_META_ALBUM);
  
    gavl_dictionary_set_string(container_m, GAVL_META_TITLE, title);
    gavl_dictionary_set_string(container_m, GAVL_META_SEARCH_TITLE, bg_get_search_string(title));
    gavl_dictionary_set(container_m, GAVL_META_GENRE, gavl_dictionary_get(child_m, GAVL_META_GENRE));

    detect_album_cover(plugin_reg, path, container_m);
    }
  
  if(path)
    free(path);
  if(basename)
    free(basename);
  }

static void create_language_arrays(gavl_dictionary_t * ti)
  {
  gavl_value_t val;
  gavl_array_t * arr;
  gavl_dictionary_t * m;
  const gavl_dictionary_t * sm;
  int nstreams1;
  int nstreams2;
  int i;
  const char * lang;
  gavl_value_t lang_val;
  
  m = gavl_track_get_metadata_nc(ti);

  if((nstreams1 = gavl_track_get_num_audio_streams(ti)))
    {
    gavl_value_init(&val);
    arr = gavl_value_set_array(&val);
    
    for(i = 0; i < nstreams1; i++)
      {
      gavl_value_init(&lang_val);

      if((sm = gavl_track_get_audio_metadata(ti, i)) &&
         (lang = gavl_dictionary_get_string(sm, GAVL_META_LANGUAGE)))
        gavl_value_set_string(&lang_val, bg_get_language_name(lang));
      else
        gavl_value_set_string(&lang_val, bg_get_language_name("und"));

      gavl_array_splice_val_nocopy(arr, -1, 0, &lang_val);
      }
    
    gavl_dictionary_set_nocopy(m, GAVL_META_AUDIO_LANGUAGES, &val);
    }

  nstreams1 = gavl_track_get_num_text_streams(ti);
  nstreams2 = gavl_track_get_num_overlay_streams(ti);
  
  if(nstreams1 || nstreams2)
    {
    gavl_value_init(&val);
    arr = gavl_value_set_array(&val);

    for(i = 0; i < nstreams1; i++)
      {
      gavl_value_init(&lang_val);

      if((sm = gavl_track_get_text_metadata(ti, i)) &&
         (lang = gavl_dictionary_get_string(sm, GAVL_META_LANGUAGE)))
        gavl_value_set_string(&lang_val, bg_get_language_name(lang));
      else
        gavl_value_set_string(&lang_val, bg_get_language_name("und"));

      gavl_array_splice_val_nocopy(arr, -1, 0, &lang_val);
      }

    for(i = 0; i < nstreams2; i++)
      {
      gavl_value_init(&lang_val);

      if((sm = gavl_track_get_overlay_metadata(ti, i)) &&
         (lang = gavl_dictionary_get_string(sm, GAVL_META_LANGUAGE)))
        gavl_value_set_string(&lang_val, bg_get_language_name(lang));
      else
        gavl_value_set_string(&lang_val, bg_get_language_name("und"));

      gavl_array_splice_val_nocopy(arr, -1, 0, &lang_val);
      }
    
    gavl_dictionary_set_nocopy(m, GAVL_META_SUBTITLE_LANGUAGES, &val);
    }
  
  }

static int input_plugin_finalize_track(bg_plugin_handle_t * h, const char * location, int track, int total_tracks)
  {
  gavl_dictionary_t * ti;
  gavl_dictionary_t * m;
  const char * klass;
  const char * var;
  char * path     = NULL;
  char * basename = NULL;
  
  if(!(ti = bg_input_plugin_get_track_info(h, track)))
    return 0;

  gavl_track_finalize(ti);
  
  if(location && (*location == '/'))
    {
    char * pos;

    path = gavl_strdup(location);
    
    if((pos = strrchr(path, '/')))
      {
      *pos = '\0';
      basename = pos + 1;

      if((pos = strrchr(basename, '.')))
        *pos = '\0';
      }
    }
  
  m = gavl_track_get_metadata_nc(ti);

  if(total_tracks > 1)
    {
    gavl_dictionary_set_int(m, GAVL_META_TRACKNUMBER, track+1);
    gavl_dictionary_set_int(m, GAVL_META_TOTAL_TRACKS, total_tracks);
    }
  
  //  mimetype = gavl_dictionary_get_string(m, GAVL_META_MIMETYPE);
  //  gavl_metadata_add_src(m, GAVL_META_SRC, mimetype, location);
  
  if((klass = gavl_dictionary_get_string(m, GAVL_META_CLASS)))
    {
    if(basename && !strcmp(klass, GAVL_META_CLASS_MOVIE_PART))
      {
      char * pos = strrchr(basename, ')'); // Closing parenthesis of the year
      if(pos)
        {
        pos++;
        *pos = '\0';
        }
      }
    
    if((!strcmp(klass, GAVL_META_CLASS_MOVIE) ||
        !strcmp(klass, GAVL_META_CLASS_MOVIE_PART)))
      {
      detect_movie_poster(bg_plugin_reg, path, basename, m);
      detect_movie_wallpaper(bg_plugin_reg, path, basename, m);
      detect_nfo(path, basename, m);
      create_language_arrays(ti);
      bg_track_find_subtitles(ti);
      }
    else if(!strcmp(klass, GAVL_META_CLASS_SONG))
      {
      if(path)
        detect_album_cover(bg_plugin_reg, path, m);
      }
    else if(!strcmp(klass, GAVL_META_CLASS_TV_EPISODE))
      {
      gavl_dictionary_t show_m;
      char * p;
      
      char * path = gavl_strdup(location);

      if((p = strrchr(path, '/')))
        *p = '\0';

      gavl_dictionary_init(&show_m);
      gavl_dictionary_set_string(&show_m, GAVL_META_CLASS, GAVL_META_CLASS_TV_SHOW);

      /* Detecting the NFO a second time might fail if the title inside the .nfo and
         the filename don't match */
      
      if(detect_nfo(path, gavl_dictionary_get_string(m, GAVL_META_SHOW), &show_m))
        gavl_dictionary_set(m, GAVL_META_SHOW, gavl_dictionary_get(&show_m, GAVL_META_TITLE));
      
      gavl_dictionary_free(&show_m);
      bg_track_find_subtitles(ti);
      create_language_arrays(ti);
      
      if(!detect_movie_poster(bg_plugin_reg, path, basename, m))
        {
        const char * tag;
        if(basename && (tag = gavl_detect_episode_tag(basename, NULL, NULL, NULL)))
          {
          int result;
          char * parent_basename;
          
          tag++;
          while(isdigit(*tag))
            tag++;
        
          parent_basename = gavl_strndup(basename, tag);
          result = detect_movie_poster(bg_plugin_reg, path, parent_basename, m);
          free(parent_basename);

          if(!result)
            {
            parent_basename = gavl_strdup(gavl_dictionary_get_string(m, GAVL_META_SHOW));
            result = detect_movie_poster(bg_plugin_reg, path, parent_basename, m);
            free(parent_basename);
            }

          }

        }
      
      if(!detect_movie_wallpaper(bg_plugin_reg, path, basename, m))
        {
        const char * tag;
        if(basename && (tag = gavl_detect_episode_tag(basename, NULL, NULL, NULL)))
          {
          int result;
          char * parent_basename;

          tag++;
          while(isdigit(*tag))
            tag++;
        
          parent_basename = gavl_strndup(basename, tag);
          result = detect_movie_wallpaper(bg_plugin_reg, path, parent_basename, m);
          free(parent_basename);

          if(!result)
            {
            parent_basename = gavl_strdup(gavl_dictionary_get_string(m, GAVL_META_SHOW));
            result = detect_movie_wallpaper(bg_plugin_reg, path, parent_basename, m);
            free(parent_basename);
            }
          
          }
        
        }
      free(path);
      }

    if((var = gavl_dictionary_get_string(m, GAVL_META_TITLE)))
      gavl_dictionary_set_string(m, GAVL_META_SEARCH_TITLE,
                                 bg_get_search_string(var));

    if((var = gavl_dictionary_get_string(m, GAVL_META_YEAR)) &&
       !gavl_dictionary_get(m, GAVL_META_DATE))
      {
      gavl_dictionary_set_date(m, GAVL_META_DATE, atoi(var), 99, 99);
      }
    }
  else
    gavl_dictionary_set_string(m, GAVL_META_CLASS, GAVL_META_CLASS_FILE);
  
  gavl_track_set_countries(ti);

  if(location)
    bg_set_track_name_default(ti, location);
  
  if(path)
    free(path);

  return 1;
  }

static int input_plugin_finalize(bg_plugin_handle_t * h, const char * location)
  {
  int i;
  int num;
  gavl_dictionary_t * edl;
  gavl_dictionary_t * mi = bg_input_plugin_get_media_info(h);
  num = gavl_get_num_tracks(mi);
  
  for(i = 0; i < num; i++)
    {
    if(!input_plugin_finalize_track(h, location, i, num))
      return 0;
    }
  
  set_locations(mi, location);

  if((edl = gavl_dictionary_get_dictionary_nc(mi, GAVL_META_EDL)))
    set_locations(edl, location);
  
  return 1;
  }

static int input_plugin_load(const char * location,
                             const bg_plugin_info_t * info,
                             const gavl_dictionary_t * options,
                             bg_plugin_handle_t ** ret)
  {
  const char * real_location;
  char * protocol = NULL, * path = NULL;
  const gavl_array_t * protocols;
  int num_plugins, i;
  bg_input_plugin_t * plugin;
  int try_and_error = 1;
  const bg_plugin_info_t * first_plugin = NULL;

  int result = 0;
  
  if(!location)
    return 0;
  
  if(!strncmp(location, "file://", 7))
    location += 7;
  
  real_location = location;

  if(!strstr(real_location, "://") && access(real_location, R_OK))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN,
             TRS("Cannot access file \"%s\": %s"), real_location,
             strerror(errno));
    return 0;
    }
  
  if(!info && !options) /* No plugin given, seek one */
    {
    if(bg_string_is_url(location))
      {
      if(gavl_url_split(location,
                      &protocol,
                      NULL, // user,
                      NULL, // password,
                      NULL, // hostname,
                      NULL,   //  port,
                      &path))
        info = bg_plugin_find_by_protocol(protocol, BG_PLUGIN_INPUT);
      }
    else if(!strcmp(location, "-"))
      {
      info = bg_plugin_find_by_protocol("stdin", BG_PLUGIN_INPUT);
      }
    else
      {
      info = bg_plugin_find_by_filename(real_location,
                                        (BG_PLUGIN_INPUT));
      }
    first_plugin = info;
    }
  else
    try_and_error = 0; /* We never try other plugins than the given one */
  
  if(info || options)
    {
    /* Try to load this */

    load_input_plugin(bg_plugin_reg, info, options, ret);

    if(!info)
      info = bg_plugin_find_by_name(gavl_dictionary_get_string(options, BG_CFG_TAG_NAME));
    
    if(!(*ret))
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, TRS("Loading plugin \"%s\" failed"),
               bg_plugin_info_get_long_name(info));
      goto fail;
      }
    
    plugin = (bg_input_plugin_t*)((*ret)->plugin);

    if(!plugin->open((*ret)->priv, real_location))
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, TRS("Opening %s with \"%s\" failed"),
               real_location, bg_plugin_info_get_long_name(info));
      }
    else
      {
      input_plugin_finalize(*ret, real_location);
      goto done;
      }
    }
  
  
  if(!try_and_error)
    goto fail;
  
  num_plugins = bg_get_num_plugins(BG_PLUGIN_INPUT, 0);
  for(i = 0; i < num_plugins; i++)
    {
    info = bg_plugin_find_by_index(i, BG_PLUGIN_INPUT, 0);

    if(info == first_plugin)
      continue;

    if((protocols = bg_plugin_info_get_protocols(info)))
      {
      if(protocol)
        {
        if(gavl_string_array_indexof(protocols, protocol) < 0)
          continue;
        }
      else
        {
        if(gavl_string_array_indexof(protocols, "file") < 0)
          continue;
        }
      }
    load_input_plugin(bg_plugin_reg, info, NULL, ret);

    if(!*ret)
      continue;
    
    plugin = (bg_input_plugin_t*)((*ret)->plugin);
    if(!plugin->open((*ret)->priv, real_location))
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, TRS("Opening %s with \"%s\" failed"),
               location, bg_plugin_info_get_long_name(info));
      }
    else
      {
      input_plugin_finalize(*ret, real_location);
      goto done;
      }
    }

  done:
  result = 1;
  fail:

  if(protocol)
    free(protocol);
  if(path)
    free(path);
  
  return result;
  }

int bg_file_is_blacklisted(const char * url)
  {
  const char * pos;

  if(strncmp(url, "file:", 5) && (*url != '/'))  // Remote file
    return 0;

  pos = strrchr(url, '.');
  if(pos)
    {
    pos++;
    if(bg_string_match(pos, load_blacklist_ext))
      {
      gavl_log(GAVL_LOG_DEBUG, LOG_DOMAIN, "Not loading %s (blacklisted extension)", url);
      return 1;
      }
    }
  
  pos = strrchr(url, '/');
  if(pos)
    {
    pos++;
    if(bg_string_match(pos, load_blacklist_names))
      {
      gavl_log(GAVL_LOG_DEBUG, LOG_DOMAIN, "Not loading %s (blacklisted filename)", url);
      return 1;
      }
    }
  return 0;
  }

static void remove_gmerlin_url_vars(gavl_dictionary_t * vars)
  {
  gavl_dictionary_set(vars, GAVL_URL_VAR_TRACK,   NULL);
  gavl_dictionary_set(vars, GAVL_URL_VAR_VARIANT, NULL);
  gavl_dictionary_set(vars, BG_URL_VAR_PLUGIN,  NULL);
  gavl_dictionary_set(vars, BG_URL_VAR_CMDLINE, NULL);
  gavl_dictionary_set(vars, GAVL_URL_VAR_CLOCK_TIME, NULL);
  }

static void set_locations(gavl_dictionary_t * dict, const char * location)
  {
  int num, i;
  
  num = gavl_get_num_tracks(dict);

  for(i = 0; i < num; i++)
    {
    const char * klass;
    char * new_location;
    gavl_dictionary_t * src;
    gavl_dictionary_t * track;
    gavl_dictionary_t * m;

    track = gavl_get_track_nc(dict, i);

    if(!(m = gavl_track_get_metadata_nc(track)))
      continue;

    if((klass = gavl_dictionary_get_string(m, GAVL_META_CLASS)) &&
       !strcmp(klass, GAVL_META_CLASS_LOCATION))
      continue;

    if(!(src = gavl_metadata_get_src_nc(m, GAVL_META_SRC, 0)))
      src = gavl_metadata_add_src(m, GAVL_META_SRC, NULL, NULL);

    new_location = gavl_strdup(location);
    
    if(num > 1)
      {
      gavl_dictionary_t vars;
      gavl_dictionary_init(&vars);
      
      gavl_url_get_vars(new_location, &vars);
      gavl_dictionary_set_int(&vars, GAVL_URL_VAR_TRACK, i+1);
      new_location = bg_url_append_vars(new_location, &vars);
      gavl_dictionary_reset(&vars);
      }
    gavl_dictionary_set_string_nocopy(src, GAVL_META_URI, new_location);
    
    }
  }

bg_plugin_handle_t * bg_input_plugin_load(const char * location_c)
  {
  int i;
  char * location = NULL;
  //  char * tmp_string = NULL;
  
  gavl_dictionary_t vars;
  const char * plugin_name;
  const bg_plugin_info_t * info = NULL;
  
  const gavl_value_t * options_val;
  const gavl_dictionary_t * options = NULL;
  bg_plugin_handle_t * ret = NULL;
  
  gavl_dictionary_init(&vars);
  location = gavl_strdup(location_c);

  gavl_url_get_vars(location, &vars);

  /* Check for a forbidden extension */
  
  if(bg_file_is_blacklisted(location))
    {
    gavl_log(GAVL_LOG_DEBUG, LOG_DOMAIN, "Not loading %s: blacklisted name", location);
    goto end;
    }
  
  if((plugin_name = gavl_dictionary_get_string(&vars, BG_URL_VAR_PLUGIN)))
    info = bg_plugin_find_by_name(plugin_name);

  /* Apply -ip option */
  i = 0;
  if(gavl_dictionary_get_int(&vars, BG_URL_VAR_CMDLINE, &i) && i &&
     (options_val = bg_plugin_config_get(BG_PLUGIN_INPUT)))
    {
    options = gavl_value_get_dictionary(options_val);
    }
  
  /* Remove the gmerlin specific variables and append the others */
  remove_gmerlin_url_vars(&vars);
  location = bg_url_append_vars(location, &vars);
  
  if(!input_plugin_load(location, info, options, &ret))
    {
    if(ret)
      {
      bg_plugin_unref(ret);
      ret = NULL;
      }
    goto end;
    }
  end:
  
  if(location)
    free(location);
  
  return ret;
  }


bg_plugin_handle_t * bg_input_plugin_load_full(const char * location)
  {
  bg_plugin_handle_t * ret;
  gavl_dictionary_t track;
  int num_variants = 0;
  int variant = 0;
  
  gavl_dictionary_init(&track);
  gavl_track_from_location(&track, location);
  ret = bg_load_track(&track, variant, &num_variants);
  gavl_dictionary_free(&track);
  return ret;
  }


static const bg_parameter_info_t encoder_section_general[] =
  {
    {
      .name        = "$general",
      .long_name   = TRS("General"),
      .type        = BG_PARAMETER_SECTION,
    },
    { } // End
  };
    
static const bg_parameter_info_t encoder_section_audio[] =
  {
    {
      .name      = "$audio",
      .long_name = TRS("Audio"),
      .type      = BG_PARAMETER_SECTION,
    },
    { } // End
  };

static const bg_parameter_info_t encoder_section_video[] =
  {
    {
      .name      = "$video",
      .long_name = TRS("Video"),
      .type      = BG_PARAMETER_SECTION,
    },
    { } // End
  };

static const bg_parameter_info_t encoder_section_overlay[] =
  {
    {
      .name      = "$overlay",
      .long_name = TRS("Overlay subtitles"),
      .type      = BG_PARAMETER_SECTION,
    },
    { } // End
  };

static const bg_parameter_info_t encoder_section_text[] =
  {
    {
      .name      = "$text",
      .long_name = TRS("Text subtitles"),
      .type      = BG_PARAMETER_SECTION,
    },
    { } // End
  };

static bg_parameter_info_t *
create_encoder_parameters(const bg_plugin_info_t * info, int stream_params)
  {
  bg_parameter_info_t * ret = NULL;
  
  if(stream_params &&
     (info->audio_parameters ||
      info->video_parameters ||
      info->text_parameters ||
      info->overlay_parameters))
    {
    int i = 0;
    const bg_parameter_info_t * src[11];
    if(info->parameters)
      {
      if(info->parameters[0].type != BG_PARAMETER_SECTION)
        {
        src[i] = encoder_section_general;
        i++;
        }
      src[i] = info->parameters;
      i++;
      }

    if(stream_params)
      {
      if(info->audio_parameters)
        {
        src[i] = encoder_section_audio;
        i++;
        src[i] = info->audio_parameters;
        i++;
        }

      if(info->text_parameters)
        {
        src[i] = encoder_section_text;
        i++;
        src[i] = info->text_parameters;
        i++;
        }

      if(info->overlay_parameters)
        {
        src[i] = encoder_section_overlay;
        i++;
        src[i] = info->overlay_parameters;
        i++;
        }

      if(info->video_parameters)
        {
        src[i] = encoder_section_video;
        i++;
        src[i] = info->video_parameters;
        i++;
        }

      }
    src[i] = NULL;
    ret = bg_parameter_info_concat_arrays(src);
    }
  else if(info->parameters)
    ret = bg_parameter_info_copy_array(info->parameters);
  
  return ret;
  }

static void set_parameter_info(bg_plugin_registry_t * reg,
                               uint32_t type_mask,
                               uint32_t flag_mask,
                               bg_parameter_info_t * ret, int stream_params)
  {
  int num_plugins, start_entries, i;
  const bg_plugin_info_t * info;

  //  fprintf(stderr, "set_parameter_info: %d %d\n", type_mask, flag_mask);
  
  num_plugins =
    bg_get_num_plugins(type_mask, flag_mask);

  start_entries = 0;
  if(ret->multi_names_nc)
    {
    while(ret->multi_names_nc[start_entries])
      start_entries++;
    }

#define REALLOC(arr) \
  ret->arr = realloc(ret->arr, (start_entries + num_plugins + 1)*sizeof(*ret->arr)); \
  memset(ret->arr + start_entries, 0, (num_plugins + 1)*sizeof(*ret->arr));

  REALLOC(multi_names_nc);
  REALLOC(multi_labels_nc);
  REALLOC(multi_parameters_nc);
  REALLOC(multi_descriptions_nc);
#undef REALLOC
    
  bg_parameter_info_set_const_ptrs(ret);

  for(i = 0; i < num_plugins; i++)
    {
    const char * gettext_domain;
    const char * gettext_directory;

    info = bg_plugin_find_by_index(i, type_mask, flag_mask);

    gettext_domain = bg_plugin_info_get_gettext_domain(info);
    gettext_directory = bg_plugin_info_get_gettext_directory(info);
    
    ret->multi_names_nc[start_entries+i] = gavl_strdup(bg_plugin_info_get_name(info));

    //    fprintf(stderr, "set_parameter_info: %d %d %s\n", i, num_plugins, bg_plugin_info_get_name(info));
    
    bg_bindtextdomain(gettext_domain, gettext_directory);
    

    ret->multi_descriptions_nc[start_entries+i] =
      gavl_strdup(TRD(bg_plugin_info_get_description(info),
                      gettext_domain));
    
    ret->multi_labels_nc[start_entries+i] =
      gavl_strdup(TRD(bg_plugin_info_get_long_name(info),
                      gettext_domain));
    
    if(info->type & (BG_PLUGIN_ENCODER_AUDIO |
                     BG_PLUGIN_ENCODER_VIDEO |
                     BG_PLUGIN_ENCODER_TEXT |
                     BG_PLUGIN_ENCODER_OVERLAY |
                     BG_PLUGIN_ENCODER))
      ret->multi_parameters_nc[start_entries+i] =
        create_encoder_parameters(info, stream_params);
    else if(info->parameters)
      {
      ret->multi_parameters_nc[start_entries+i] =
        bg_parameter_info_copy_array(info->parameters);
      }
    }
  }
  
void bg_plugin_registry_set_parameter_info(bg_plugin_registry_t * reg,
                                           uint32_t type_mask,
                                           uint32_t flag_mask,
                                           bg_parameter_info_t * ret)
  {
  set_parameter_info(reg, type_mask, flag_mask, ret, 1);
  }

static const bg_parameter_info_t audio_encoder_param =
  {
    .name      = "ae",
    .long_name = TRS("Audio"),
    .type      = BG_PARAMETER_MULTI_MENU,
  };

static const bg_parameter_info_t audio_encoder_param_v =
  {
    .name               = "ae",
    .long_name          = TRS("Audio"),
    .type               = BG_PARAMETER_MULTI_MENU,
    .multi_names        = (const char *[]){ TO_VIDEO, NULL },
    .multi_labels       = (const char *[]){ "Write to video file", NULL },
    .multi_parameters   = (const bg_parameter_info_t*[]){ NULL, NULL },
    .multi_descriptions = (const char *[]){ NULL, NULL },
  };

static const bg_parameter_info_t video_encoder_param =
  {
    .name       = "ve",
    .long_name = TRS("Video"),
    .type      = BG_PARAMETER_MULTI_MENU,
  };

static const bg_parameter_info_t text_encoder_param =
  {
    .name      = "te",
    .long_name = TRS("Text subtitles"),
    .type      = BG_PARAMETER_MULTI_MENU,
  };

static const bg_parameter_info_t text_encoder_param_v =
  {
    .name      = "te",
    .long_name = TRS("Text subtitles"),
    .type      = BG_PARAMETER_MULTI_MENU,
    .multi_names  = (const char *[]){ TO_VIDEO, NULL },
    .multi_labels = (const char *[]){ "Write to video file", NULL },
    .multi_parameters = (const bg_parameter_info_t*[]){ NULL, NULL },
    .multi_descriptions = (const char *[]){ NULL, NULL },
  };

static const bg_parameter_info_t overlay_encoder_param =
  {
    .name       = "oe",
    .long_name = TRS("Overlay subtitles"),
    .type      = BG_PARAMETER_MULTI_MENU,
  };

static const bg_parameter_info_t overlay_encoder_param_v =
  {
    .name       = "oe",
    .long_name = TRS("Overlay subtitles"),
    .type      = BG_PARAMETER_MULTI_MENU,
    .multi_names  = (const char *[]){ TO_VIDEO, NULL },
    .multi_labels = (const char *[]){ "Write to video file", NULL },
    .multi_parameters = (const bg_parameter_info_t*[]){ NULL, NULL },
    .multi_descriptions = (const char *[]){ NULL, NULL },
  };

bg_parameter_info_t *
bg_plugin_registry_create_encoder_parameters(bg_plugin_registry_t * reg,
                                             uint32_t type_mask,
                                             uint32_t flag_mask, int stream_params)
  {
  int do_audio = 0;
  int do_video = 0;
  int do_text = 0;
  int do_overlay = 0;
  int i;
  
  bg_parameter_info_t * ret;
  
  /* Determine what stream we want */

  if(type_mask & GAVL_STREAM_AUDIO)
    do_audio = 1;
  if(type_mask & GAVL_STREAM_VIDEO)
    do_video = 1;
  if(type_mask & GAVL_STREAM_TEXT)
    do_text = 1;
  if(type_mask & GAVL_STREAM_OVERLAY)
    do_overlay = 1;
  
  /* Count parameters */
  i = 0;
  if(do_audio)
    i++;
  if(do_text)
    i++;
  if(do_overlay)
    i++;
  if(do_video)
    i++;

  ret = calloc(i+1, sizeof(*ret));

  i = 0;

  if(do_audio)
    {
    if(do_video)
      bg_parameter_info_copy(&ret[i], &audio_encoder_param_v);
    else
      bg_parameter_info_copy(&ret[i], &audio_encoder_param);

    set_parameter_info(reg,
                       BG_PLUGIN_ENCODER_AUDIO,
                       flag_mask, &ret[i], stream_params);

    i++;
    }
  if(do_text)
    {
    if(do_video)
      bg_parameter_info_copy(&ret[i], &text_encoder_param_v);
    else 
      bg_parameter_info_copy(&ret[i], &text_encoder_param);
    
    set_parameter_info(reg,
                       BG_PLUGIN_ENCODER_TEXT,
                       flag_mask, &ret[i], stream_params);
    
    i++;
    }
  if(do_overlay)
    {
    if(do_video)
      bg_parameter_info_copy(&ret[i], &overlay_encoder_param_v);
    else
      bg_parameter_info_copy(&ret[i], &overlay_encoder_param);
    
    set_parameter_info(reg,
                       BG_PLUGIN_ENCODER_OVERLAY,
                       flag_mask, &ret[i], stream_params);
    i++;
    }
  if(do_video)
    {
    bg_parameter_info_copy(&ret[i], &video_encoder_param);

    set_parameter_info(reg,
                       BG_PLUGIN_ENCODER_VIDEO | BG_PLUGIN_ENCODER,
                       flag_mask, &ret[i], stream_params);
    i++;
    }

  return ret;
  }

int bg_input_plugin_set_track(bg_plugin_handle_t * h, int track)
  {
  gavl_dictionary_t * mi;
  gavl_dictionary_t * ti;
  bg_input_plugin_t * p = (bg_input_plugin_t *)h->plugin;

  /* Try GAVL_CMD_SRC_SELECT_TRACK */
  if(h->control.cmd_sink)
    {
    gavl_msg_t * cmd = bg_msg_sink_get(h->ctrl_ext.cmd_sink);
    gavl_msg_set_id_ns(cmd, GAVL_CMD_SRC_SELECT_TRACK, GAVL_MSG_NS_SRC);
    gavl_msg_set_arg_int(cmd, 0, track);
    bg_msg_sink_put(h->ctrl_ext.cmd_sink);
    }
  
  if(!(mi = bg_input_plugin_get_media_info(h)))
    return 0;
  
  gavl_set_current_track(mi, track);
  ti = gavl_get_track_nc(mi, track);
  
  input_plugin_finalize_track(h, bg_track_get_current_location(ti), track, gavl_get_num_tracks(mi));

  if(p->get_src)
    {
    h->src = p->get_src(h->priv);
    bg_media_source_set_msg_action_by_id(h->src, GAVL_META_STREAM_ID_MSG_PROGRAM,
                                         BG_STREAM_ACTION_DECODE);
    }
  return 1;
  }

int bg_input_plugin_get_track(bg_plugin_handle_t * h)
  {
  gavl_dictionary_t * mi;
  if(!(mi = bg_input_plugin_get_media_info(h)))
    return -1;
  return gavl_get_current_track(mi);
  }

void bg_input_plugin_set_video_hw_context(bg_plugin_handle_t * h,
                                          gavl_hw_context_t * ctx)
  {
  bg_input_plugin_t * p = (bg_input_plugin_t *)h->plugin;

  if(p->set_video_hw_context)
    p->set_video_hw_context(h->priv, ctx);
  }

void bg_input_plugin_seek(bg_plugin_handle_t * h, int64_t time, int scale)
  {
  gavl_msg_t * cmd;

  //  fprintf(stderr, "bg_input_plugin_seek\n");
  
  cmd = bg_msg_sink_get(h->control.cmd_sink);
  gavl_msg_set_id_ns(cmd, GAVL_CMD_SRC_SEEK, GAVL_MSG_NS_SRC);
  gavl_msg_set_arg_long(cmd, 0, time);
  gavl_msg_set_arg_int(cmd, 1, scale);
  
  bg_msg_sink_put(h->control.cmd_sink);
  //  fprintf(stderr, "bg_input_plugin_seek done\n");
  }

void bg_input_plugin_seek_percentage(bg_plugin_handle_t * h, double percentage)
  {
  gavl_time_t duration;
  gavl_time_t t;
  gavl_dictionary_t * track =
    bg_input_plugin_get_track_info(h, -1);
  
  duration = gavl_track_get_duration(track);
  if(duration > 0)
    {
    t = gavl_seconds_to_time(gavl_time_to_seconds(duration)*percentage);
    t += gavl_track_get_start_time(track);
    //    fprintf(stderr, "Percentage: %f time: %"PRId64"\n", percentage, t);
    }
  else
    {
    const gavl_value_t * val;
    const gavl_dictionary_t * dict;
    const gavl_dictionary_t * seek_window;
    gavl_time_t start = 0;
    gavl_time_t end = 0;
    bg_media_source_stream_t * ss;
    
    
    if(!(dict = gavl_track_get_metadata(track)) ||
       !(seek_window = gavl_dictionary_get_dictionary(dict, GAVL_STATE_SRC_SEEK_WINDOW)))
      {
      /* Error */
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Percentage seeking not possible: Neither seek window nor duration given");
      return;
      }

    if((ss = bg_media_source_get_msg_stream_by_id(h->src, GAVL_META_STREAM_ID_MSG_PROGRAM)) &&
       ss->msghub &&
       (val = bg_state_get(bg_msg_hub_get_state(ss->msghub), GAVL_STATE_CTX_SRC, GAVL_STATE_SRC_SEEK_WINDOW)) &&
       (dict = gavl_value_get_dictionary(val)))
      {
      if(!gavl_dictionary_get_long(dict, GAVL_STATE_SRC_SEEK_WINDOW_START, &start) ||
         !gavl_dictionary_get_long(dict, GAVL_STATE_SRC_SEEK_WINDOW_END, &end))
        {
        /* Error */
        gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Percentage seeking not possible: Invalid seek window");
        gavl_dictionary_dump(&h->state, 2);
        return;
        }
      }
    
    t = start + gavl_seconds_to_time(gavl_time_to_seconds(end - start) * percentage);
    }
  bg_input_plugin_seek(h, t, GAVL_TIME_SCALE);
  }

void bg_input_plugin_start(bg_plugin_handle_t * h)
  {
  gavl_msg_t * cmd;

  if(!h->ctrl_ext.cmd_sink)
    return;
  
  cmd = bg_msg_sink_get(h->ctrl_ext.cmd_sink);
  gavl_msg_set_id_ns(cmd, GAVL_CMD_SRC_START, GAVL_MSG_NS_SRC);
  bg_msg_sink_put(h->ctrl_ext.cmd_sink);
  }

void bg_input_plugin_pause(bg_plugin_handle_t * h)
  {
  gavl_msg_t * cmd;

  if(!h->ctrl_ext.cmd_sink)
    return;

  cmd = bg_msg_sink_get(h->ctrl_ext.cmd_sink);
  gavl_msg_set_id_ns(cmd, GAVL_CMD_SRC_PAUSE, GAVL_MSG_NS_SRC);
  bg_msg_sink_put(h->ctrl_ext.cmd_sink);
  }

void bg_input_plugin_resume(bg_plugin_handle_t * h)
  {
  gavl_msg_t * cmd;

  if(!h->ctrl_ext.cmd_sink)
    return;

  cmd = bg_msg_sink_get(h->ctrl_ext.cmd_sink);
  gavl_msg_set_id_ns(cmd, GAVL_CMD_SRC_RESUME, GAVL_MSG_NS_SRC);
  bg_msg_sink_put(h->ctrl_ext.cmd_sink);
  }

int bg_plugin_handle_set_state(bg_plugin_handle_t * h, const char * ctx, const char * name,
                               const gavl_value_t * val)
  {
  if(!h->control.cmd_sink)
    return 0;

  if(!ctx)
    ctx = bg_plugin_info_get_name(h->info);
  
  bg_plugin_lock(h);
  bg_state_set(&h->state,
               1, ctx, name, val,
               h->ctrl_ext.cmd_sink, BG_CMD_SET_STATE);
  bg_plugin_unlock(h);
  
  return 1;
  }

void bg_ov_plugin_set_fullscreen(bg_plugin_handle_t * h, int fs)
  {
  gavl_value_t val;
  gavl_value_init(&val);
  gavl_value_set_int(&val, fs);
  bg_plugin_handle_set_state(h, BG_STATE_CTX_OV, BG_STATE_OV_FULLSCREEN, &val);
  
  }

void bg_ov_plugin_set_paused(bg_plugin_handle_t * h, int paused)
  {
  gavl_value_t val;
  gavl_value_init(&val);
  gavl_value_set_int(&val, paused);
  bg_plugin_handle_set_state(h, BG_STATE_CTX_OV, BG_STATE_OV_PAUSED, &val);
  }

void bg_ov_plugin_set_visible(bg_plugin_handle_t * h, int visible)
  {
  gavl_value_t val;
  gavl_value_init(&val);
  gavl_value_set_int(&val, visible);
  bg_plugin_handle_set_state(h, BG_STATE_CTX_OV, BG_STATE_OV_VISIBLE, &val);
  }

void bg_ov_plugin_set_window_title(bg_plugin_handle_t * h, const char * window_title)
  {
  gavl_value_t val;
  gavl_value_init(&val);
  gavl_value_set_string(&val, window_title);
  bg_plugin_handle_set_state(h, BG_STATE_CTX_OV, BG_STATE_OV_TITLE, &val);
  gavl_value_free(&val);
  }

gavl_dictionary_t * bg_input_plugin_get_media_info(bg_plugin_handle_t * h)
  {
  bg_input_plugin_t * plugin;
  gavl_dictionary_t * ret;
  
  if(!h->info || (h->info->type != BG_PLUGIN_INPUT))
    return 0;

  plugin = (bg_input_plugin_t*)h->plugin;

  if(plugin->get_media_info && (ret = plugin->get_media_info(h->priv)))
    return ret;
  
  return NULL;
  }
  
int bg_input_plugin_get_num_tracks(bg_plugin_handle_t * h)
  {
  gavl_dictionary_t * mi;

  if(!(mi = bg_input_plugin_get_media_info(h)))
    return 0;
  
  return gavl_get_num_tracks(mi);
  }

const gavl_dictionary_t * bg_input_plugin_get_edl(bg_plugin_handle_t * h)
  {
  const gavl_dictionary_t * dict;

  if(!(dict = bg_input_plugin_get_media_info(h)) ||
     !(dict = gavl_dictionary_get_dictionary(dict, GAVL_META_EDL)))
    return NULL;
  
  return dict;
  }

gavl_dictionary_t * bg_input_plugin_get_track_info(bg_plugin_handle_t * h, int idx)
  {
  gavl_dictionary_t * mi;

  if(!(mi = bg_input_plugin_get_media_info(h)))
    return 0;
  return gavl_get_track_nc(mi, idx);
  }


static int set_track_info(bg_plugin_handle_t * h)
  {
  int num;
  int i;
  const gavl_dictionary_t * info;
  info = bg_input_plugin_get_track_info(h, -1);

  num = gavl_track_get_num_audio_streams(info);
  for(i = 0; i < num; i++)
    bg_media_source_set_audio_action(h->src, i, BG_STREAM_ACTION_DECODE);

  num = gavl_track_get_num_video_streams(info);
  for(i = 0; i < num; i++)
    bg_media_source_set_video_action(h->src, i, BG_STREAM_ACTION_DECODE);

  num = gavl_track_get_num_text_streams(info);
  for(i = 0; i < num; i++)
    bg_media_source_set_text_action(h->src, i, BG_STREAM_ACTION_DECODE);

  num = gavl_track_get_num_overlay_streams(info);
  for(i = 0; i < num; i++)
    bg_media_source_set_overlay_action(h->src, i, BG_STREAM_ACTION_DECODE);

  bg_input_plugin_start(h);
  
  return 1;
  }


gavl_dictionary_t * bg_plugin_registry_load_media_info(bg_plugin_registry_t * reg,
                                                       const char * location1,
                                                       int flags)
  {
  int i;
  int result = 0;
  int num_tracks;
  bg_plugin_handle_t * h = NULL;
  gavl_dictionary_t * ret = NULL;
  gavl_dictionary_t * edl = NULL;

  char * location = gavl_strdup(location1);
  
  if(flags & BG_INPUT_FLAG_PREFER_EDL)
    {
    if(!strchr(location, '?'))
      location = gavl_strcat(location, "?");
    else
      location = gavl_strcat(location, "&");
    }
  
  if(flags & BG_INPUT_FLAG_SELECT_TRACK)
    {
    if(!(h = bg_input_plugin_load_full(location)))
      goto fail;
    }
  else if(!(h = bg_input_plugin_load(location)))
    goto fail;
  
  if(!(flags & BG_INPUT_FLAG_SELECT_TRACK))
    {
    int num_tracks = bg_input_plugin_get_num_tracks(h);

    for(i = 0; i < num_tracks; i++)
      {
      bg_track_set_current_location(bg_input_plugin_get_track_info(h, i),
                                    location);
      
      if(!bg_input_plugin_set_track(h, i))
        goto fail;

      if(flags & BG_INPUT_FLAG_GET_FORMAT)
        {
        set_track_info(h);
        }
      /* Need to do this again as it might override things */
      input_plugin_finalize_track(h, location, i, num_tracks);
      }
    }
  else
    {
    if(flags & BG_INPUT_FLAG_GET_FORMAT)
      set_track_info(h);
    }
  
  /* Copy the whole media info */
  ret = gavl_dictionary_create();

  if(!(flags & BG_INPUT_FLAG_SELECT_TRACK))
    gavl_dictionary_copy(ret, bg_input_plugin_get_media_info(h));
  else
    gavl_dictionary_copy(ret, bg_input_plugin_get_track_info(h, -1));

  /* Handle multitrack files */
    
  if(!(flags & BG_INPUT_FLAG_SELECT_TRACK) &&
     ((num_tracks = gavl_get_num_tracks(ret)) > 1))
    {
    const char * pos;
    gavl_dictionary_t * m;
        
    m = gavl_dictionary_get_dictionary_create(ret, GAVL_META_METADATA);

    if(!gavl_dictionary_get_string(m, GAVL_META_CLASS))
      gavl_dictionary_set_string(m, GAVL_META_CLASS,
                                 GAVL_META_CLASS_MULTITRACK_FILE);
    
    if(!gavl_dictionary_get(m, GAVL_META_LABEL) && (pos = strrchr(location1, '/')))
      gavl_dictionary_set_string(m, GAVL_META_LABEL, pos + 1);

    /* Some plugins might fail to do this */
    gavl_track_update_children(ret);
    }
  
  result = 1;
  fail:
  
  if(h)
    bg_plugin_unref(h);

  if(!result && ret)
    {
    gavl_dictionary_destroy(ret);
    ret = NULL;
    }

  /* Check whether to return edl instead */

  if(ret && (edl = gavl_dictionary_get_dictionary_nc(ret, GAVL_META_EDL)))
    {
    gavl_dictionary_t * tmp = calloc(1, sizeof(*tmp));
    gavl_edl_finalize(edl);
    gavl_dictionary_move(tmp, edl);
    gavl_dictionary_destroy(ret);
    ret = tmp;
    }
  
  free(location);
  
  return ret;
  }

static int load_location(const char * str, int flags, gavl_array_t * ret, int idx)
  {
  int result = 0;
  const gavl_dictionary_t * edl;
  
  const gavl_array_t * tracks;
  gavl_dictionary_t * mi = bg_plugin_registry_load_media_info(bg_plugin_reg, str, flags);

  if(!mi)
    return 0;

  /* Take EDL instead */
  if((edl = gavl_dictionary_get_dictionary(mi, GAVL_META_EDL)))
    tracks = gavl_get_tracks(edl);
  else
    tracks = gavl_get_tracks(mi);
  
  if(tracks && ((result = tracks->num_entries) > 0))
    {
    const gavl_dictionary_t * dict_src;

    if((dict_src = gavl_value_get_dictionary(&ret->entries[idx])) &&
       (dict_src = gavl_track_get_metadata(dict_src)))
      {
      int i = 0;
      gavl_dictionary_t * dict_dst;
      
      while(i < tracks->num_entries)
        {
        if((dict_dst = gavl_value_get_dictionary_nc(&tracks->entries[i])) &&
           (dict_dst = gavl_track_get_metadata_nc(dict_dst)))
          {
          gavl_dictionary_merge2(dict_dst, dict_src);
          }
        i++;
        }
      
      }
    
    gavl_array_splice_array(ret, idx, 1, tracks);
    }
  gavl_dictionary_destroy(mi);
  return result;
  }


static int resolve_locations(gavl_array_t * dst, int flags)
  {
  int ret = 0;
  int i = 0;
  int num_added;
  const gavl_dictionary_t * m;
  const gavl_dictionary_t * dict;
  const char * klass;
  const char * uri;
  
  while(i < dst->num_entries)
    {
    if(!(dict = gavl_value_get_dictionary(&dst->entries[i])))
      {
      i++;
      continue;
      }

    if((m = gavl_track_get_metadata(dict)) &&
       (klass = gavl_dictionary_get_string(m, GAVL_META_CLASS)) &&
       !strcmp(klass, GAVL_META_CLASS_LOCATION) &&
       (gavl_metadata_get_src(m, GAVL_META_SRC, 0, NULL, &uri)))
      {
      num_added = load_location(uri, flags, dst, i);
      if(num_added)
        {
        i += num_added;
        ret++;
        }
      else
        i++;
      }
    else
      i++;
    }
  return ret;
  }

void bg_tracks_resolve_locations(const gavl_value_t * src, gavl_array_t * dst, int flags)
  {
  int i;
  const char * label;
  const char * last_label;

  const char * klass;
  const char * last_klass;
  gavl_dictionary_t * last_m = NULL;
  
  if(src->type == GAVL_TYPE_DICTIONARY)
    gavl_array_splice_val(dst, -1, 0, src);
  else if(src->type == GAVL_TYPE_ARRAY)
    {
    const gavl_array_t * arr = gavl_value_get_array(src);
    gavl_array_splice_array(dst, -1, 0, arr);
    }

  for(i = 0; i < 3; i++)
    {
    if(!resolve_locations(dst, flags))
      break;
    }

  /* If we encounter subsequent entries with the same class and label,
     we assume that it's just different URIs for the same media source.
     This is used for m3u files, which contain multiple URIs for the
     same broadcast */
  i = 0;

  last_label = NULL;
  last_klass = NULL;
  
  while(i < dst->num_entries)
    {
    gavl_dictionary_t * dict;

    if(!(dict = gavl_value_get_dictionary_nc(&dst->entries[i])) ||
       !(dict = gavl_track_get_metadata_nc(dict)))
      {
      i++;
      last_label = NULL;
      last_klass = NULL;
      continue;
      }

    if(!last_label || !last_klass)
      {
      last_m = dict;
      last_label = gavl_dictionary_get_string(last_m, GAVL_META_LABEL);
      last_klass = gavl_dictionary_get_string(last_m, GAVL_META_CLASS);
      i++;
      }
    else
      {
      label = gavl_dictionary_get_string(dict, GAVL_META_LABEL);
      klass = gavl_dictionary_get_string(dict, GAVL_META_CLASS);
      
      if(label && last_label && klass && last_klass &&
         !strcmp(label, last_label) && !strcmp(klass, last_klass))
        {
        gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Merging track \"%s\" (%s)\n", label, klass);

        gavl_dictionary_append(last_m, GAVL_META_SRC,
                               gavl_dictionary_get_item(dict, GAVL_META_SRC, 0));

        gavl_array_splice_val(dst, i, 1, NULL);
        }
      else
        {
        last_label = label;
        last_klass = klass;
        last_m = dict;
        i++;
        }
      
      }
    
    }
                                  
  
  }

const gavl_array_t * bg_plugin_registry_get_input_mimetypes()
  {
  int i, j;
  int num_plugins;
  const bg_plugin_info_t * info;
  const gavl_array_t * mimetypes;
  
  if(!bg_plugin_reg->input_mimetypes)
    {
    bg_plugin_reg->input_mimetypes = gavl_array_create();

    num_plugins = bg_get_num_plugins(BG_PLUGIN_INPUT, 0);
    for(i = 0; i < num_plugins; i++)
      {
      info = bg_plugin_find_by_index(i, BG_PLUGIN_INPUT, 0);
      
      if(!(mimetypes = bg_plugin_info_get_mimetypes(info)))
        continue;
      
      for(j = 0; j < mimetypes->num_entries; j++)
        gavl_string_array_add(bg_plugin_reg->input_mimetypes, gavl_string_array_get(mimetypes, j));
      }
    }
  
  return bg_plugin_reg->input_mimetypes;
  }

const gavl_array_t * bg_plugin_registry_get_input_protocols()
  {
  int i, j;
  int num_plugins;
  const bg_plugin_info_t * info;
  const gavl_array_t * protocols;

  if(!bg_plugin_reg->input_protocols)
    {
    bg_plugin_reg->input_protocols = gavl_array_create();
    
    num_plugins = bg_get_num_plugins(BG_PLUGIN_INPUT, 0);
    for(i = 0; i < num_plugins; i++)
      {
      info = bg_plugin_find_by_index(i, BG_PLUGIN_INPUT, 0);
      
      if(!(protocols = bg_plugin_info_get_protocols(info)))
        continue;
      
      for(j = 0; j < protocols->num_entries; j++)
        gavl_string_array_add(bg_plugin_reg->input_protocols, gavl_string_array_get(protocols, j));
      }
    }
  return bg_plugin_reg->input_protocols;
  }

const gavl_array_t * bg_plugin_registry_get_input_extensions()
  {
  int i, j;
  int num_plugins;
  const bg_plugin_info_t * info;
  const gavl_array_t * extensions;
  
  if(!bg_plugin_reg->input_extensions)
    {
    bg_plugin_reg->input_extensions = gavl_array_create();

    num_plugins = bg_get_num_plugins(BG_PLUGIN_INPUT, 0);
    for(i = 0; i < num_plugins; i++)
      {
      info = bg_plugin_find_by_index(i, BG_PLUGIN_INPUT, 0);
      
      if(!(extensions = bg_plugin_info_get_extensions(info)))
        continue;
      
      for(j = 0; j < extensions->num_entries; j++)
        gavl_string_array_add(bg_plugin_reg->input_extensions, gavl_string_array_get(extensions, j));
      }
    }
  
  return bg_plugin_reg->input_extensions;
  }

const gavl_array_t * bg_plugin_registry_get_image_extensions()
  {
  int i, j;
  int num_plugins;
  const bg_plugin_info_t * info;
  const gavl_array_t * extensions;
  
  if(!bg_plugin_reg->image_extensions)
    {
    bg_plugin_reg->image_extensions = gavl_array_create();

    num_plugins = bg_get_num_plugins(BG_PLUGIN_IMAGE_READER, 0);
    for(i = 0; i < num_plugins; i++)
      {
      info = bg_plugin_find_by_index(i, BG_PLUGIN_IMAGE_READER, 0);
      
      if(!(extensions = bg_plugin_info_get_extensions(info)))
        continue;
      
      for(j = 0; j < extensions->num_entries; j++)
        gavl_string_array_add(bg_plugin_reg->image_extensions, gavl_string_array_get(extensions, j));
      }
    }
  
  return bg_plugin_reg->image_extensions;
  }


int bg_track_is_multitrack_sibling(const gavl_dictionary_t * cur, const gavl_dictionary_t * next, int * next_idx)
  {
  int ret = 0;
  gavl_dictionary_t next_vars;
  gavl_dictionary_t cur_vars;

  int cur_track = 0;
  int next_track = 0;
  
  const char * cur_url = NULL;
  const char * next_url = NULL;
  char * cur_url_priv = NULL;
  char * next_url_priv = NULL;
  
  if(!(cur = gavl_track_get_metadata(cur)) ||
     !(next = gavl_track_get_metadata(next)) ||
     !gavl_metadata_get_src(cur, GAVL_META_SRC, 0, NULL, &cur_url) ||
     !gavl_metadata_get_src(next, GAVL_META_SRC, 0, NULL, &next_url))
    {
    return 0;
    }

  gavl_dictionary_init(&cur_vars);
  gavl_dictionary_init(&next_vars);

  //  fprintf(stderr, "bg_track_is_multitrack_sibling %s %s\n",
  //          cur_url, next_url);
  
  cur_url_priv  = gavl_strdup(cur_url);
  next_url_priv = gavl_strdup(next_url);

  gavl_url_get_vars(cur_url_priv, &cur_vars);
  gavl_url_get_vars(next_url_priv, &next_vars);

  if(!strcmp(cur_url_priv, next_url_priv) &&
     gavl_dictionary_get_int(&cur_vars, GAVL_URL_VAR_TRACK, &cur_track) &&
     (cur_track >= 0) &&
     gavl_dictionary_get_int(&next_vars, GAVL_URL_VAR_TRACK, &next_track) &&
     (next_track >= 0))
    {
    ret = 1;
    }
  
  free(cur_url_priv);
  free(next_url_priv);

  gavl_dictionary_free(&cur_vars);
  gavl_dictionary_free(&next_vars);

  if(ret && next_idx)
    *next_idx = next_track - 1;
  
  return ret;
  }

//bg_medi

gavl_audio_source_t * bg_input_plugin_get_audio_source(bg_plugin_handle_t * h, int stream)
  {
  bg_media_source_stream_t * s;

  if(!h->src)
    return NULL;

  if(!(s = bg_media_source_get_audio_stream(h->src, stream)))
    return NULL;
     
  return s->asrc;
  }

gavl_video_source_t * bg_input_plugin_get_video_source(bg_plugin_handle_t * h, int stream)
  {
  bg_media_source_stream_t * s;

  if(!h->src)
    return NULL;

  if(!(s = bg_media_source_get_video_stream(h->src, stream)))
    return NULL;

  return s->vsrc;
  
  
  }

gavl_packet_source_t * bg_input_plugin_get_audio_packet_source(bg_plugin_handle_t * h, int stream)
  {
  bg_media_source_stream_t * s;

  if(!h->src)
    return NULL;

  if(!(s = bg_media_source_get_audio_stream(h->src, stream)))
    return NULL;

  return s->psrc;

  }

gavl_packet_source_t * bg_input_plugin_get_video_packet_source(bg_plugin_handle_t * h, int stream)
  {
  bg_media_source_stream_t * s;

  if(!h->src)
    return NULL;

  if(!(s = bg_media_source_get_video_stream(h->src, stream)))
    return NULL;

  return s->psrc;

  }

gavl_packet_source_t * bg_input_plugin_get_overlay_packet_source(bg_plugin_handle_t * h, int stream)
  {
  bg_media_source_stream_t * s;
  
  if(!h->src)
    return NULL;

  if(!(s = bg_media_source_get_overlay_stream(h->src, stream)))
    return NULL;
  return s->psrc;
  
  
  }

gavl_video_source_t * bg_input_plugin_get_overlay_source(bg_plugin_handle_t * h, int stream)
  {
  bg_media_source_stream_t * s;

  if(!h->src)
    return NULL;

  if(!(s = bg_media_source_get_overlay_stream(h->src, stream)))
    return NULL;
  
  return s->vsrc;
  }
  
gavl_packet_source_t * bg_input_plugin_get_text_source(bg_plugin_handle_t * h, int stream)
  {
  bg_media_source_stream_t * s;

  if(!h->src)
    return NULL;

  if(!(s = bg_media_source_get_text_stream(h->src, stream)))
    return NULL;
  
  return s->psrc;
  }

bg_msg_hub_t * bg_input_plugin_get_msg_hub_by_id(bg_plugin_handle_t * h, int id)
  {
  bg_media_source_stream_t * s;

  if(!h->src)
    return NULL;
  
  if(!(s = bg_media_source_get_stream_by_id(h->src, id)))
    return NULL;

  return s->msghub;
  }

/* Singleton stuff */

bg_plugin_registry_t * bg_plugin_reg = NULL;

/* Must be called in the main function before using any threads */

void bg_plugins_init()
  {
  gavl_dictionary_t * cfg_section;
  
  if(bg_plugin_reg)
    return;
  
  bg_cfg_registry_init();

  cfg_section = bg_cfg_registry_find_section(bg_cfg_registry, "plugins");
  
  bg_plugin_registry_create_1(cfg_section);
  }

void bg_plugin_registry_list_plugins(bg_plugin_type_t type, int flags)
  {
  int i, num;
  const bg_plugin_info_t * info;
  num = bg_get_num_plugins(type, flags);

  for(i = 0; i < num; i++)
    {
    info = bg_plugin_find_by_index(i, type, flags);
    printf("%s\n", bg_plugin_info_get_name(info));
    }
  
  }

void bg_plugin_registry_list_input(void * data, int * argc,
                                           char *** _argv, int arg)
  {
  bg_plugin_registry_list_plugins(BG_PLUGIN_INPUT, 0);
  }


void bg_plugin_registry_list_fe_renderer(void * data, int * argc,
                                         char *** _argv, int arg)
  {
  bg_plugin_registry_list_plugins(BG_PLUGIN_FRONTEND_RENDERER, 0);
  }

void bg_plugin_registry_list_fe_mdb(void * data, int * argc,
                                    char *** _argv, int arg)
  {
  bg_plugin_registry_list_plugins(BG_PLUGIN_FRONTEND_MDB, 0);
  }



void bg_plugin_registry_list_fa(void * data, int * argc,
                                        char *** _argv, int arg)
  {
  bg_plugin_registry_list_plugins(BG_PLUGIN_FILTER_AUDIO, BG_PLUGIN_FILTER_1);

  }


void bg_plugin_registry_list_fv(void * data, int * argc,
                                        char *** _argv, int arg)
  {
  bg_plugin_registry_list_plugins(BG_PLUGIN_FILTER_VIDEO, BG_PLUGIN_FILTER_1);
  }

void bg_plugin_registry_list_vis(void * data, int * argc,
                                         char *** _argv, int arg)
  {
  bg_plugin_registry_list_plugins(BG_PLUGIN_VISUALIZATION, 0);
  }


void bg_plugin_registry_list_plugin_parameters(void * data, int * argc,
                                               char *** _argv, int arg)
  {
  /* TODO */
  char * tmp_string;
  const bg_plugin_info_t * info;
  const gavl_array_t * arr;
  
  if(arg >= *argc)
    {
    fprintf(stderr, "Option -list-plugin-parameters requires an argument\n");
    exit(-1);
    }

  if(!(info = bg_plugin_find_by_name((*_argv)[arg])))
    {
    fprintf(stderr, "No such plugin: %s\n", (*_argv)[arg]);
    exit(-1);
    }

  if((arr = bg_plugin_info_get_extensions(info)))
    {
    tmp_string = gavl_string_array_join(arr, " ");
    fprintf(stderr, "Extensions: %s\n", tmp_string);
    }

  if((arr = bg_plugin_info_get_protocols(info)))
    {
    tmp_string = gavl_string_array_join(arr, " ");
    fprintf(stderr, "Protocols: %s\n", tmp_string);
    }
  
  if(info->parameters)
    {
    bg_cmdline_print_help_parameters(info->parameters, BG_HELP_FORMAT_TERM);
    }
  
  bg_cmdline_remove_arg(argc, _argv, arg);
  }

/* Commandline parsing struff */

int bg_plugin_config_parse_single(gavl_dictionary_t * dict,
                                  const char * string)
  {
  const char * pos;
  const bg_plugin_info_t * info;
  
  const char * name;
  
  if(!(pos = gavl_find_char_c(string, '?')))
    {
    gavl_dictionary_set_string(dict, BG_CFG_TAG_NAME, string);
    }
  else
    {
    gavl_dictionary_set_string_nocopy(dict, BG_CFG_TAG_NAME, gavl_strndup(string, pos));
    pos++;
    }

  name = gavl_dictionary_get_string(dict, BG_CFG_TAG_NAME);
  
  if(!(info = bg_plugin_find_by_name(name)))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "No such plugin: %s", name);
    return 0;
    }

  if(pos)
    return bg_parameter_parse_string(pos, dict, info->parameters);
  else
    return 1;
  }


/* Load and store per type plugin configuration globally */

const gavl_value_t * bg_plugin_config_get(bg_plugin_type_t type)
  {
  gavl_dictionary_t * dict;
  dict = bg_cfg_registry_find_section(bg_cfg_registry, BG_PLUGIN_CONFIG);
  dict = bg_cfg_section_find_subsection(dict, bg_plugin_type_to_string(type));
  return gavl_dictionary_get(dict, BG_PLUGIN_CONFIG_PLUGIN);
  }

void bg_plugin_config_set(bg_plugin_type_t type, const gavl_value_t * val)
  {
  gavl_dictionary_t * dict;
  dict = bg_cfg_registry_find_section(bg_cfg_registry, BG_PLUGIN_CONFIG);
  dict = bg_cfg_section_find_subsection(dict, bg_plugin_type_to_string(type));
  gavl_dictionary_set(dict, BG_PLUGIN_CONFIG_PLUGIN, val);
  }

/* Commandline options */

static void parse_plugin_single(const char * arg, bg_plugin_type_t type)
  {
  gavl_value_t val;
  gavl_dictionary_t * dict;

  gavl_value_init(&val);
  dict = gavl_value_set_dictionary(&val);
  bg_plugin_config_parse_single(dict, arg);
  bg_plugin_config_set(type, &val);  
  gavl_value_free(&val);
  }

static void parse_plugin_multi(const char * arg, bg_plugin_type_t type)
  {
  gavl_value_t val;
  gavl_value_t val_array;
  
  gavl_array_t * array;
  gavl_dictionary_t * dict;
  
  const gavl_value_t * val_array_c;
  const gavl_array_t * array_c;
  
  /* TODO */
  
  gavl_value_init(&val);
  dict = gavl_value_set_dictionary(&val);

  bg_plugin_config_parse_single(dict, arg);

  /* Create array */
  gavl_value_init(&val_array);
  array = gavl_value_set_array(&val_array);

  /* Check if the array already exists */
  
  if((val_array_c = bg_plugin_config_get(type)) &&
     (array_c = gavl_value_get_array(val_array_c)))
    gavl_array_splice_array(array, 0, 0, array_c);

  /* Add new element */
  gavl_array_splice_val_nocopy(array, -1, 0, &val);

  /* Store */
  bg_plugin_config_set(type, &val_array);

  gavl_value_free(&val_array);
  gavl_value_free(&val);
  }

void bg_plugin_registry_opt_oa(void * data, int * argc,
                               char *** _argv, int arg)
  {
  gavl_value_t val;
  if(arg >= *argc)
    {
    fprintf(stderr, "Option -oa requires an argument\n");
    exit(-1);
    }
  
  gavl_value_init(&val);
  gavl_value_set_string(&val, (*_argv)[arg]);
  bg_plugin_config_set(BG_PLUGIN_OUTPUT_AUDIO, &val);  
  gavl_value_free(&val);
  bg_cmdline_remove_arg(argc, _argv, arg);
  }

void bg_plugin_registry_opt_ov(void * data, int * argc,
                               char *** _argv, int arg)
  {
  gavl_value_t val;
  if(arg >= *argc)
    {
    fprintf(stderr, "Option -ov requires an argument\n");
    exit(-1);
    }
  
  gavl_value_init(&val);
  gavl_value_set_string(&val, (*_argv)[arg]);
  bg_plugin_config_set(BG_PLUGIN_OUTPUT_VIDEO, &val);  
  gavl_value_free(&val);
  bg_cmdline_remove_arg(argc, _argv, arg);
  }

void bg_plugin_registry_opt_fa(void * data, int * argc,
                               char *** _argv, int arg)
  {
  if(arg >= *argc)
    {
    fprintf(stderr, "Option -fa requires an argument\n");
    exit(-1);
    }
  parse_plugin_multi((*_argv)[arg], BG_PLUGIN_FILTER_AUDIO);
  bg_cmdline_remove_arg(argc, _argv, arg);

  }

void bg_plugin_registry_opt_fv(void * data, int * argc,
                               char *** _argv, int arg)
  {
  if(arg >= *argc)
    {
    fprintf(stderr, "Option -fv requires an argument\n");
    exit(-1);
    }
  parse_plugin_multi((*_argv)[arg], BG_PLUGIN_FILTER_VIDEO);
  bg_cmdline_remove_arg(argc, _argv, arg);
  }

void bg_plugin_registry_opt_vis(void * data, int * argc,
                                char *** _argv, int arg)
  {
  if(arg >= *argc)
    {
    fprintf(stderr, "Option -vis requires an argument\n");
    exit(-1);
    }
  parse_plugin_single((*_argv)[arg], BG_PLUGIN_VISUALIZATION);
  bg_cmdline_remove_arg(argc, _argv, arg);
  }

void bg_plugin_registry_opt_ip(void * data, int * argc,
                               char *** _argv, int arg)
  {
  if(arg >= *argc)
    {
    fprintf(stderr, "Option -ip requires an argument\n");
    exit(-1);
    }
  parse_plugin_single((*_argv)[arg], BG_PLUGIN_INPUT);
  bg_cmdline_remove_arg(argc, _argv, arg);
  }

static bg_parameter_info_t info_oa[2]  = { { .name = NULL } };
static bg_parameter_info_t info_ov[2]  = { { .name = NULL } };
static bg_parameter_info_t info_fa[2]  = { { .name = NULL } };
static bg_parameter_info_t info_fv[2]  = { { .name = NULL } };
static bg_parameter_info_t info_vis[2] = { { .name = NULL } };
static bg_parameter_info_t info_i[2]   = { { .name = NULL } };

static bg_parameter_info_t info_ca[2]  = { { .name = NULL } };
static bg_parameter_info_t info_cv[2]  = { { .name = NULL } };
static bg_parameter_info_t info_co[2]  = { { .name = NULL } };



void bg_track_find_subtitles(gavl_dictionary_t * track)
  {
  int i;
  char * pattern;
  const char * location;
  const char * pos;
  glob_t g;
  gavl_dictionary_t * s;
  gavl_dictionary_t * sm;
  const gavl_dictionary_t * m = gavl_track_get_metadata(track);
  
  if(!m)
    return;
  
  if(!gavl_metadata_get_src(m, GAVL_META_SRC, 0, NULL, &location))
    return;

  if(!(pos = strrchr(location, '.')))
    return;

  pattern = gavl_strndup(location, pos);
  pattern = gavl_strcat(pattern, ".*");
  pattern = gavl_escape_string(pattern, "[]?");
  
  glob(pattern, 0, NULL /* errfunc */, &g);

  for(i = 0; i < g.gl_pathc; i++)
    {
    if(gavl_string_ends_with_i(g.gl_pathv[i], ".srt"))
      {
      const char * start;
      const char * end;

      s = gavl_track_append_external_stream(track,
                                            GAVL_STREAM_TEXT,
                                            "application/x-subrip", g.gl_pathv[i]);

      if(!s) // Stream already added
        continue;
      
      sm = gavl_stream_get_metadata_nc(s);
      
      start = g.gl_pathv[i] + (int)(pos - location);
      start++;
      end = strrchr(g.gl_pathv[i], '.');
      if(start < end)
        {
        const char * label;
        char * str = gavl_strndup(start, end);
        if((label = gavl_language_get_label_from_code(str)))
          {
          gavl_dictionary_set_string(sm, GAVL_META_LABEL, label);
          gavl_dictionary_set_string(sm, GAVL_META_LANGUAGE, gavl_language_get_iso639_2_b_from_code(str));
          free(str);
          }
        else
          gavl_dictionary_set_string_nocopy(sm, GAVL_META_LABEL, str);
        
        gavl_dictionary_set_int(sm, GAVL_META_STREAM_PACKET_TIMESCALE, 1000);
        gavl_dictionary_set_int(sm, GAVL_META_STREAM_SAMPLE_TIMESCALE, 1000);
        }
      }
    }
  globfree(&g);
  free(pattern);
  }

bg_plugin_handle_t * bg_load_track(const gavl_dictionary_t * track,
                                   int variant, int * num_variants)
  {
  int i;
  
  gavl_dictionary_t dict;

  const gavl_dictionary_t * src_track = track; // Track (or subtrack) containing the location
  const gavl_dictionary_t * edl = NULL;
  bg_plugin_handle_t * ret = NULL;
  const gavl_dictionary_t * extra_vars = NULL;

  int src_idx;
  int track_index = 0;
  gavl_dictionary_t vars;
  
  gavl_dictionary_init(&vars);
  gavl_dictionary_init(&dict);

  /* Multipart movie */
  if(get_multipart_edl(track, &dict))
    edl = &dict;

  //  fprintf(stderr, "bg_load_track:\n");
  //  gavl_dictionary_dump(track, 2);
  //  fprintf(stderr, "\n");
  
  extra_vars = bg_track_get_uri_vars(track);

#if 0  
  if(extra_vars)
    {
    fprintf(stderr, "bg_load_track: Got extra vars\n");
    gavl_dictionary_dump(extra_vars, 2);
    fprintf(stderr, "\n");
    }
#endif
  
  /* Loop until we get real media */
  for(i = 0; i < MAX_REDIRECTIONS; i++)
    {
    gavl_dictionary_t * ti;
    gavl_dictionary_t * tm;
    const char * klass;
    const char * location = NULL;
    
    if(edl)
      {
      ret = bg_input_plugin_load_edl(edl);
      bg_input_plugin_set_track(ret, track_index);
      goto end;
      }
    
    if((*num_variants = gavl_track_get_num_variants(track)))
      {
      src_track = gavl_track_get_variant(track, variant);
      if(!src_track)
        {
        gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "No variants left");
        goto end;
        }
      }

    if(gavl_track_get_num_external_streams(src_track))
      {
      /* Load multi plugin */
      if(!(ret = bg_input_plugin_load_multi(src_track, NULL)))
        goto end;
      
      bg_input_plugin_set_track(ret, 0);
      goto end;
      }
    
    /* Open location */
    src_idx = 0;
    while(gavl_track_get_src(src_track, GAVL_META_SRC, src_idx, NULL, &location))
      {
      /* Get url vars */
      gavl_dictionary_t vars;
      char * real_location;

      real_location = gavl_strdup(location);
      
      track_index = 0;
      gavl_dictionary_init(&vars);

      gavl_url_get_vars(real_location, &vars);
      
      if(gavl_dictionary_get_int(&vars, GAVL_URL_VAR_TRACK, &track_index))
        track_index--;
      
      if(extra_vars)
        gavl_dictionary_merge2(&vars, extra_vars);
      
      real_location = gavl_url_append_vars(real_location, &vars);
      
      gavl_dictionary_free(&vars);

      if(!(ret = bg_input_plugin_load(real_location)))
        {
        free(real_location);
        src_idx++;
        continue;
        }
      free(real_location);
      break;
      }

    if(!ret)
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "No playable location found");
      gavl_dictionary_dump(track, 2);

      goto end;
      }
    if((edl = bg_input_plugin_get_edl(ret)))
      {
      /* TODO: Add option which allows forcing the raw file instead of the EDL */
      gavl_dictionary_reset(&dict);
      gavl_dictionary_copy(&dict, edl);
      edl = &dict;
      /* EDL will be loaded next iteration */
      continue;
      }

    ti = bg_input_plugin_get_track_info(ret, track_index);
    
    if(!ti)
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "No track %d in %s", track_index + 1, location);
      bg_plugin_unref(ret);
      ret = NULL;
      goto end;
      }

    bg_track_set_current_location(ti, location);
    
    tm = gavl_track_get_metadata_nc(ti);
    
    if(!(klass = gavl_dictionary_get_string(tm, GAVL_META_CLASS)) ||
       strcmp(klass, GAVL_META_CLASS_LOCATION))
      {
      bg_input_plugin_set_track(ret, track_index);
      
      if(gavl_track_get_num_external_streams(ti))
        {
        /* Propagate uri vars */
        if(extra_vars)
          bg_track_set_uri_vars(ti, extra_vars);
        
        ret = bg_input_plugin_load_multi(NULL, ret);
        bg_input_plugin_set_track(ret, 0);
        }
      break;
      }
    else // Redirector -> Prepare for next iteration
      {
      gavl_dictionary_copy(&dict, ti);
      /* Propagate uri vars */
      if(extra_vars)
        bg_track_set_uri_vars(&dict, extra_vars);
      
      track = &dict;
      src_track = &dict;

      //      fprintf(stderr, "Got redirector:\n");
      //      gavl_dictionary_dump(&dict, 2);
      
      }
    
    } // Redirector loop
  
  end:

  gavl_dictionary_free(&dict);
  gavl_dictionary_free(&vars);
  
  return ret;
  }

/* Track items set by or for the plugin registry */

/*
 *  Values in the track dictionary for configuring
 */
#define BG_TRACK_DICT_PLUGINREG   "$plugin_reg"

#define BG_TRACK_CURRENT_LOCATION "location"
#define BG_TRACK_CURRENT_TRACK    "track"
#define BG_TRACK_URIVARS          "urivars"


void
bg_track_set_current_location(gavl_dictionary_t * dict, const char * location)
  {
  dict = gavl_dictionary_get_dictionary_create(dict, BG_TRACK_DICT_PLUGINREG);
  gavl_dictionary_set_string(dict, BG_TRACK_CURRENT_LOCATION, location);
  }

void
bg_track_set_uri_vars(gavl_dictionary_t * track, const gavl_dictionary_t * uri_vars)
  {
  int num_variants;
  gavl_dictionary_t * dict;
  
  dict = gavl_dictionary_get_dictionary_create(track, BG_TRACK_DICT_PLUGINREG);

  if(!uri_vars)
    gavl_dictionary_set(dict, BG_TRACK_URIVARS, NULL);
  else
    gavl_dictionary_set_dictionary(dict, BG_TRACK_URIVARS, uri_vars);

  num_variants = gavl_track_get_num_variants(track);

  //  fprintf(stderr, "bg_track_set_uri_vars, num_variants: %d\n", num_variants);
  
  if(num_variants)
    {
    int i;
    for(i = 0; i < num_variants; i++)
      {
      bg_track_set_uri_vars(gavl_track_get_variant_nc(track, i),
                            uri_vars);
      }
    }
  }

const gavl_dictionary_t * 
bg_track_get_uri_vars(const gavl_dictionary_t * dict)
  {
  if((dict = gavl_dictionary_get_dictionary(dict, BG_TRACK_DICT_PLUGINREG)))
    return gavl_dictionary_get_dictionary(dict, BG_TRACK_URIVARS);
  else
    return NULL;
  }

const char *
bg_track_get_current_location(const gavl_dictionary_t * dict)
  {
  if(!(dict = gavl_dictionary_get_dictionary(dict, BG_TRACK_DICT_PLUGINREG)))
    return NULL;
  
  return gavl_dictionary_get_string(dict, BG_TRACK_CURRENT_LOCATION);
  }

static void free_plugin_params(bg_parameter_info_t * info)
  {
  if(!info->name)
    return;
  bg_parameter_info_free(info);
  memset(info, 0, sizeof(*info));
  }

void bg_plugins_cleanup()
  {
  if(bg_plugin_reg)
    {
    bg_plugin_registry_destroy_1(bg_plugin_reg);
    bg_plugin_reg = NULL;
    }

  free_plugin_params(info_oa);
  free_plugin_params(info_fa);
  free_plugin_params(info_ov);
  free_plugin_params(info_fv);
  free_plugin_params(info_vis);
  free_plugin_params(info_i);
  free_plugin_params(info_ca);
  free_plugin_params(info_cv);
  free_plugin_params(info_co);

  }

bg_plugin_handle_t * bg_output_plugin_load(const char * sink_uri, int type)
  {
  bg_plugin_handle_t * ret;
  const bg_plugin_info_t * info;
  gavl_dictionary_t options_s;
  gavl_dictionary_t options_v;

  gavl_dictionary_init(&options_s);
  gavl_dictionary_init(&options_v);
  
  gavl_url_get_vars_c(sink_uri, &options_s);
  
  if(!(info = bg_plugin_find_by_protocol(sink_uri, type)))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "No plugin found for uri: %s", sink_uri);
    return NULL;
    }
  
  ret = load_plugin(info);
  
  if(ret && ret->plugin->get_parameters)
    {
    const bg_parameter_info_t * info = ret->plugin->get_parameters(ret->priv);

    bg_cfg_section_from_strings(&options_s, &options_v, info);
    apply_parameters(ret, &options_v);
    }

  gavl_dictionary_free(&options_s);
  gavl_dictionary_free(&options_v);
  
  return ret;
  
  }

int
bg_plugin_registry_extract_embedded_cover(const char * uri,
                                          gavl_buffer_t * buf,
                                          gavl_dictionary_t * m)
  {
  int ret = 0;
  const gavl_value_t * val;
  const gavl_dictionary_t * dict;
  const gavl_buffer_t * buf_src;
  gavl_dictionary_t * mi;
  
  if(!(mi = bg_plugin_registry_load_media_info(bg_plugin_reg, uri, 0)))
    return 0;
  
  if((dict = gavl_get_track(mi, 0)) &&
     (dict = gavl_track_get_metadata(dict)) &&
     (val = gavl_dictionary_get(dict, GAVL_META_COVER_EMBEDDED)))
    {
    if(val->type == GAVL_TYPE_DICTIONARY)
      dict = val->v.dictionary;
    else if(val->type == GAVL_TYPE_ARRAY)
      {
      if((val = gavl_array_get(val->v.array, 0)) &&
         (val->type == GAVL_TYPE_DICTIONARY))
        dict = val->v.dictionary;
      }
    
    if((buf_src = gavl_dictionary_get_binary(dict, GAVL_META_IMAGE_BUFFER)))
      {
      gavl_dictionary_copy_value(m, dict, GAVL_META_MIMETYPE);
      gavl_buffer_append(buf, buf_src);
      ret = 1;
      }
    }
  
  if(mi)
    gavl_dictionary_destroy(mi);
  
  return ret;
  }

char * bg_plugin_registry_get_cache_file_name(void)
  {
  char * dir = gavl_search_cache_dir(PACKAGE, NULL, NULL);

  if(!dir)
    return NULL;
  
  return gavl_sprintf("%s/plugins.xml", dir);
  }
