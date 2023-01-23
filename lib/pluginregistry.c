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
#include <gmerlin/bgplug.h>
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

static int
get_multipart_edl(const gavl_dictionary_t * track, gavl_dictionary_t * edl);

static void
set_locations(gavl_dictionary_t * dict, const char * location);


static int probe_image(bg_plugin_registry_t * r,
                       const char * filename,
                       gavl_video_format_t * format,
                       gavl_dictionary_t * m, bg_plugin_handle_t ** h);


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
      bg_ovl2text_name,
      bg_ovl2text_info,
      bg_ovl2text_get,
      bg_ovl2text_create,
    },
    {
      bg_recorder_input_name,
      bg_recorder_input_info,
      bg_recorder_input_get,
      bg_recorder_input_create,
    },
    { /* End */ }
  };


struct bg_plugin_registry_s
  {
  bg_plugin_info_t * entries;
  int changed;
  
  gavl_dictionary_t * state;
  pthread_mutex_t state_mutex;

  };


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
    bg_msg_sink_put(h->control.cmd_sink, msg);
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
          bg_msg_get_state(msg, &last, &ctx_p, &var_p, &val,
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
  
  bg_msg_sink_put(h->ctrl_ext.evt_sink, msg);
  
  return 1;
  }


void bg_plugin_info_destroy(bg_plugin_info_t * info)
  {
  
  if(info->gettext_domain)
    free(info->gettext_domain);
  if(info->gettext_directory)
    free(info->gettext_directory);

  if(info->name)
    free(info->name);
  if(info->long_name)
    free(info->long_name);
  if(info->description)
    free(info->description);

  gavl_value_free(&info->mimetypes_val);
  gavl_value_free(&info->extensions_val);
  gavl_value_free(&info->protocols_val);
  
  if(info->module_filename)
    free(info->module_filename);
  if(info->devices)
    bg_device_info_destroy(info->devices);
  if(info->cmp_name)
    free(info->cmp_name);
  if(info->compressions)
    free(info->compressions);
  
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
  bg_bindtextdomain(i->gettext_domain,
                    i->gettext_directory);

  tmp_string =
    bg_utf8_to_system(TRD(i->long_name, i->gettext_domain), -1);
  
  len = strxfrm(NULL, tmp_string, 0);
  i->cmp_name = malloc(len+1);
  strxfrm(i->cmp_name, tmp_string, len+1);
  free(tmp_string);


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
    if(!strcmp(info->name, name))
      return info;
    info = info->next;
    }
  return NULL;
  }

const bg_plugin_info_t * bg_plugin_find_by_name(const char * name)
  {
  return find_by_name(bg_plugin_reg->entries, name);
  }

const bg_plugin_info_t * bg_plugin_find_by_protocol(const char * protocol)
  {
  const bg_plugin_info_t * info = bg_plugin_reg->entries;
  
  while(info)
    {
    if(info->protocols && (gavl_string_array_indexof(info->protocols, protocol) >= 0))
      return info;
    info = info->next;
    }
  return NULL;
  }

const bg_plugin_info_t * bg_plugin_find_by_filename(const char * filename,
                                                    int typemask)
  {
  char * extension;
  bg_plugin_info_t * info, *ret = NULL;
  int max_priority = BG_PLUGIN_PRIORITY_MIN - 1;

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
       !(info->flags & BG_PLUGIN_FILE) ||
       !info->extensions ||
       !info->extensions->num_entries)
      {
      info = info->next;
      continue;
      }
    if(gavl_string_array_indexof_i(info->extensions, extension) >= 0)
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

const bg_plugin_info_t * bg_plugin_find_by_mimetype(const char * mimetype,
                                                    int typemask)
  {
  bg_plugin_info_t * info, *ret = NULL;
  int max_priority = BG_PLUGIN_PRIORITY_MIN - 1;

  if(!mimetype)
    return NULL;
  
  info = bg_plugin_reg->entries;
  
  while(info)
    {
    if(!(info->type & typemask) || !info->mimetypes || !info->mimetypes->num_entries)
      {
      info = info->next;
      continue;
      }
    if(gavl_string_array_indexof_i(info->mimetypes, mimetype) >= 0)
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
     !(klass = gavl_dictionary_get_string(m, GAVL_META_MEDIA_CLASS)) ||
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

  fprintf(stderr, "Got multipart EDL:\n");
  gavl_dictionary_dump(edl, 2);
  
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

      if(bg_plugin_find_by_protocol(protocol))
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

const bg_plugin_info_t *
bg_plugin_find_by_compression(gavl_codec_id_t id,
                              int typemask)
  {
  int i;
  bg_plugin_info_t * info, *ret = NULL;
  int max_priority = BG_PLUGIN_PRIORITY_MIN - 1;

  info = bg_plugin_reg->entries;
  
  while(info)
    {
    if(!(info->type & typemask) ||
       !info->compressions)
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
      if(info_1->name && info_2->name &&
         !strcmp(info_1->name, info_2->name))
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

static void set_preset_path(bg_parameter_info_t * info, const char * prefix)
  {
  //  int i;
  
  if(info->type == BG_PARAMETER_SECTION)
    info->flags |= BG_PARAMETER_GLOBAL_PRESET;
  info->preset_path = gavl_strrep(info->preset_path, prefix);
  }

bg_plugin_info_t * bg_plugin_info_create(const bg_plugin_common_t * plugin)
  {
  bg_plugin_info_t * new_info;
  new_info = calloc(1, sizeof(*new_info));

  new_info->name = gavl_strrep(new_info->name, plugin->name); 	 
	  	 
  new_info->long_name =  gavl_strrep(new_info->long_name, 	 
                                   plugin->long_name); 	 
	  	 
  new_info->description = gavl_strrep(new_info->description, 	 
                                    plugin->description);
  
  new_info->gettext_domain = gavl_strrep(new_info->gettext_domain, 	 
                                       plugin->gettext_domain); 	 
  new_info->gettext_directory = gavl_strrep(new_info->gettext_directory, 	 
                                          plugin->gettext_directory); 	 
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
  char * prefix;
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
    if(new_info->parameters)
      {
      prefix = bg_sprintf("plugins/%s", new_info->name);
      set_preset_path(new_info->parameters, prefix);
      free(prefix);
      }
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
      if(new_info->audio_parameters)
        {
        prefix = bg_sprintf("plugins/%s/audio", new_info->name);
        set_preset_path(new_info->audio_parameters, prefix);
        free(prefix);
        }
      }
    
    if(encoder->get_video_parameters)
      {
      parameter_info = encoder->get_video_parameters(plugin_priv);
      new_info->video_parameters = bg_parameter_info_copy_array(parameter_info);
      if(new_info->video_parameters)
        {
        prefix = bg_sprintf("plugins/%s/video", new_info->name);
        set_preset_path(new_info->video_parameters, prefix);
        free(prefix);
        }
      }
    if(encoder->get_text_parameters)
      {
      parameter_info = encoder->get_text_parameters(plugin_priv);
      new_info->text_parameters = bg_parameter_info_copy_array(parameter_info);
      if(new_info->text_parameters)
        {
        prefix = bg_sprintf("plugins/%s/text", new_info->name);
        set_preset_path(new_info->text_parameters, prefix);
        free(prefix);
        }
      }
    if(encoder->get_overlay_parameters)
      {
      parameter_info = encoder->get_overlay_parameters(plugin_priv);
      new_info->overlay_parameters =
        bg_parameter_info_copy_array(parameter_info);
      if(new_info->overlay_parameters)
        {
        prefix = bg_sprintf("plugins/%s/overlay", new_info->name);
        set_preset_path(new_info->overlay_parameters, prefix);
        free(prefix);
        }
      }
    }
  if(plugin->type & BG_PLUGIN_INPUT)
    {
    bg_input_plugin_t  * input;
    input = (bg_input_plugin_t*)plugin;

    if(input->get_mimetypes)
      {
      new_info->mimetypes = gavl_value_set_array(&new_info->mimetypes_val);
      bg_string_to_string_array(input->get_mimetypes(plugin_priv), new_info->mimetypes);
      }
    if(input->get_extensions)
      {
      new_info->extensions = gavl_value_set_array(&new_info->extensions_val);
      bg_string_to_string_array(input->get_extensions(plugin_priv), new_info->extensions);
      }
    
    if(input->get_protocols)
      {
      new_info->protocols = gavl_value_set_array(&new_info->protocols_val);
      bg_string_to_string_array(input->get_protocols(plugin_priv), new_info->protocols);
      }
    }
  if(plugin->type & BG_PLUGIN_IMAGE_READER)
    {
    bg_image_reader_plugin_t  * ir;
    ir = (bg_image_reader_plugin_t*)plugin;

    new_info->extensions = gavl_value_set_array(&new_info->extensions_val);
    new_info->mimetypes = gavl_value_set_array(&new_info->mimetypes_val);
    
    bg_string_to_string_array(ir->extensions, new_info->extensions);
    bg_string_to_string_array(ir->mimetypes, new_info->mimetypes);
    }
  if(plugin->type & BG_PLUGIN_IMAGE_WRITER)
    {
    bg_image_writer_plugin_t  * iw;
    iw = (bg_image_writer_plugin_t*)plugin;

    new_info->extensions = gavl_value_set_array(&new_info->extensions_val);
    new_info->mimetypes = gavl_value_set_array(&new_info->mimetypes_val);
    
    bg_string_to_string_array(iw->extensions, new_info->extensions);
    bg_string_to_string_array(iw->mimetypes, new_info->mimetypes);
    }
  if(plugin->type & (BG_PLUGIN_COMPRESSOR_AUDIO | \
                     BG_PLUGIN_COMPRESSOR_VIDEO | \
                     BG_PLUGIN_DECOMPRESSOR_AUDIO | \
                     BG_PLUGIN_DECOMPRESSOR_VIDEO))
    {
    bg_codec_plugin_t  * p;
    int num = 0;
    const gavl_codec_id_t * compressions;
    p = (bg_codec_plugin_t*)plugin;

    compressions = p->get_compressions(plugin_priv);
    
    while(compressions[num])
      num++;
    new_info->compressions = calloc(num+1, sizeof(*new_info->compressions));
    memcpy(new_info->compressions, compressions,
           num * sizeof(*new_info->compressions));
    }
  
  if(plugin->find_devices)
    new_info->devices = plugin->find_devices();

  return new_info;
  }

static bg_plugin_info_t * get_info(void * test_module,
                                   const char * filename,
                                   const bg_plugin_registry_options_t * opt)
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

  if(opt->blacklist)
    {
    int i = 0;
    while(opt->blacklist[i])
      {
      if(!strcmp(plugin->name, opt->blacklist[i]))
        {
        gavl_log(GAVL_LOG_INFO, LOG_DOMAIN,
               "Not loading %s (blacklisted)", plugin->name);
        return NULL;
        }
      i++;
      }
    }
  
  /* Get parameters */

  plugin_priv = plugin->create();
  new_info = plugin_info_create(plugin, plugin_priv, filename);
  plugin->destroy(plugin_priv);
  
  return new_info;
  }


static bg_plugin_info_t *
scan_directory_internal(const char * directory, bg_plugin_info_t ** _file_info,
                        int * changed,
                        bg_cfg_section_t * cfg_section, bg_plugin_api_t api,
                        const bg_plugin_registry_options_t * opt)
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
  
  bg_cfg_section_t * plugin_section;
  bg_cfg_section_t * stream_section;
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
      if((st.st_mtime == new_info->module_time) &&
         (bg_cfg_section_has_subsection(cfg_section,
                                        new_info->name)))
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
      }
    
    if(!(*changed))
      {
      // fprintf(stderr, "Registry changed %s\n", filename);
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
        new_info = get_info(test_module, filename, opt);
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
      
      plugin_section =
        bg_cfg_section_find_subsection(cfg_section, tmp_info->name);
    
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
               bg_cfg_section_t * cfg_section, bg_plugin_api_t api,
               const bg_plugin_registry_options_t * opt, int * reg_changed)
  {
  int changed = 0;
  bg_plugin_info_t * file_info;
  bg_plugin_info_t * file_info_next;
  char * tmp_string, *pos;
  bg_plugin_info_t * ret;
  
  ret = scan_directory_internal(directory, _file_info,
                                &changed, cfg_section, api, opt);
  
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
  
  *reg_changed = 1;
  
  free_info_list(ret);
  ret = scan_directory_internal(directory, _file_info,
                                &changed, cfg_section, api, opt);
  return ret;
  }

static bg_plugin_info_t * scan_multi(const char * path,
                                     bg_plugin_info_t ** _file_info,
                                     bg_cfg_section_t * section,
                                     bg_plugin_api_t api, const bg_plugin_registry_options_t * opt,
                                     int * reg_changed)
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
                                section, api, opt, reg_changed);
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
bg_plugin_registry_create_1(bg_cfg_section_t * section)
  {
  bg_plugin_registry_options_t opt;
  memset(&opt, 0, sizeof(opt));
  bg_plugin_registry_create_with_options(section, &opt);
  }

void
bg_plugin_registry_create_with_options(bg_cfg_section_t * section,
                                       const bg_plugin_registry_options_t * opt)
  {
  int i;
  bg_plugin_registry_t * ret;
  bg_plugin_info_t * file_info;
  bg_plugin_info_t * tmp_info;
  bg_plugin_info_t * tmp_info_next;
  char * filename;
  char * env;

  char * path;
  
  ret = calloc(1, sizeof(*ret));
  bg_plugin_reg = ret;
  
  pthread_mutex_init(&ret->state_mutex, NULL);
  
  /* Load registry file */

  file_info = NULL; 
  
  filename = bg_search_file_read("", "plugins.xml");
  if(filename)
    {
    file_info = bg_plugin_registry_load(filename);
    free(filename);
    }
  else
    ret->changed = 1;
  
  /* Native plugins */
  env = getenv("GMERLIN_PLUGIN_PATH");
  if(env)
    path = bg_sprintf("%s:%s", env, PLUGIN_DIR);
  else
    path = bg_sprintf("%s", PLUGIN_DIR);
  
  tmp_info = scan_multi(path, &file_info, section, BG_PLUGIN_API_GMERLIN, opt, &ret->changed);
  if(tmp_info)
    ret->entries = append_to_list(ret->entries, tmp_info);
  free(path);
  /* Ladspa plugins */
  
  env = getenv("LADSPA_PATH");
  if(env)
    path = bg_sprintf("%s:/usr/lib64/ladspa:/usr/local/lib64/ladspa:/usr/lib/ladspa:/usr/local/lib/ladspa", env);
  else
    path = bg_sprintf("/usr/lib64/ladspa:/usr/local/lib64/ladspa:/usr/lib/ladspa:/usr/local/lib/ladspa");

  tmp_info = scan_multi(path, &file_info, section, BG_PLUGIN_API_LADSPA, opt, &ret->changed);
  if(tmp_info)
    ret->entries = append_to_list(ret->entries, tmp_info);
  
  free(path);
  
  /* Frei0r */
  tmp_info = scan_multi("/usr/lib64/frei0r-1:/usr/local/lib64/frei0r-1:/usr/lib/frei0r-1:/usr/local/lib/frei0r-1", &file_info, 
                        section, BG_PLUGIN_API_FREI0R, opt, &ret->changed);
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

  tmp_info = bg_plug_input_get_info();
  if(tmp_info)
    ret->entries = append_to_list(ret->entries, tmp_info);
  
  if(ret->entries)
    {
    /* Sort */
    ret->entries = sort_by_priority(ret->entries);

    if(ret->changed && !opt->dont_save)
      bg_plugin_registry_save(ret->entries);
  
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
  }

int bg_plugin_registry_changed(bg_plugin_registry_t * reg)
  {
  return reg->changed;
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

void bg_plugin_registry_scan_devices(bg_plugin_registry_t * plugin_reg,
                                     uint32_t type_mask, uint32_t flag_mask)
  {
  int i;
  bg_plugin_info_t * info;
  bg_plugin_common_t * plugin;
  void * priv;
  void * module;
  const bg_parameter_info_t * parameters;
  int num = bg_get_num_plugins(type_mask, flag_mask);
  
  for(i = 0; i < num; i++)
    {
    info = find_by_index(plugin_reg->entries, i, type_mask, flag_mask);
    
    if(!(info->flags & BG_PLUGIN_DEVPARAM))
      continue;
    module = dlopen(info->module_filename, RTLD_NOW);
    plugin = (bg_plugin_common_t*)(dlsym(module, "the_plugin"));
    if(!plugin)
      {
      dlclose(module);
      continue;
      }
    priv = plugin->create();
    parameters = plugin->get_parameters(priv);

    if(info->parameters)
      bg_parameter_info_destroy_array(info->parameters);
    info->parameters = bg_parameter_info_copy_array(parameters);
    
    dlclose(module);
    }
  
  }

#if 0
void bg_plugin_registry_set_extensions(bg_plugin_registry_t * reg,
                                       const char * plugin_name,
                                       const char * extensions)
  {
  bg_plugin_info_t * info;
  info = find_by_name(reg->entries, plugin_name);
  if(!info)
    return;
  if(!(info->flags & BG_PLUGIN_FILE))
    return;
  info->extensions = gavl_strrep(info->extensions, extensions);
  
  bg_plugin_registry_save(reg->entries);
  
  }

void bg_plugin_registry_set_protocols(bg_plugin_registry_t * reg,
                                      const char * plugin_name,
                                      const char * protocols)
  {
  bg_plugin_info_t * info;
  info = find_by_name(reg->entries, plugin_name);
  if(!info)
    return;
  if(!(info->flags & BG_PLUGIN_URL))
    return;
  info->protocols = gavl_strrep(info->protocols, protocols);
  bg_plugin_registry_save(reg->entries);

  }
#endif

void bg_plugin_registry_set_priority(bg_plugin_registry_t * reg,
                                     const char * plugin_name,
                                     int priority)
  {
  bg_plugin_info_t * info;
  info = find_by_name(reg->entries, plugin_name);
  if(!info)
    return;
  info->priority = priority;
  reg->entries = sort_by_priority(reg->entries);
  bg_plugin_registry_save(reg->entries);
  }

bg_cfg_section_t *
bg_plugin_registry_get_section(bg_plugin_registry_t * reg,
                               const char * plugin_name)
  {
  bg_cfg_section_t * section = bg_cfg_registry_find_section(bg_cfg_registry, "plugins");
  return bg_cfg_section_find_subsection(section, plugin_name);
  }


void bg_plugin_ref(bg_plugin_handle_t * h)
  {
  bg_plugin_lock(h);
  h->refcount++;

  gavl_log(GAVL_LOG_DEBUG, LOG_DOMAIN, "bg_plugin_ref %s: %d",
         h->info->name, h->refcount);
  bg_plugin_unlock(h);
  
  }

static void unload_plugin(bg_plugin_handle_t * h)
  {
  bg_cfg_section_t * section;
 
  if(h->plugin->get_parameter)
    {
    section = bg_plugin_registry_get_section(bg_plugin_reg, h->info->name);
    bg_cfg_section_get(section,
                       h->plugin->get_parameters(h->priv),
                       h->plugin->get_parameter,
                       h->priv);
    }
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
  const char * klass = gavl_dictionary_get_string(metadata, GAVL_META_MEDIA_CLASS);
  
  gavl_dictionary_init(&m);

  memset(&in_fmt, 0, sizeof(in_fmt));
  
  if(!klass)
    return NULL;
  
  if(!strcmp(klass, GAVL_META_MEDIA_CLASS_AUDIO_FILE) ||
     !strcmp(klass, GAVL_META_MEDIA_CLASS_SONG))
    {
    if((img = gavl_dictionary_get_image_max(metadata,
                                            GAVL_META_COVER_URL, max_width, max_height, NULL)))
      {
      uri = gavl_dictionary_get_string(img, GAVL_META_URI);
      mimetype = gavl_dictionary_get_string(img, GAVL_META_MIMETYPE);
      }
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
                                          bg_sprintf("%"PRId64"-%"PRId64, offset, offset+size));
        
        uri_priv = gavl_strdup(uri);
        uri_priv = bg_url_append_vars(uri_priv, &url_vars);
        
        uri = uri_priv;
        gavl_dictionary_free(&url_vars);

        mimetype = gavl_dictionary_get_string(img, GAVL_META_MIMETYPE);
        }
      }
    
    if(uri)
      goto have_uri;
    
    uri = DATA_DIR"/web/icons/music_nocover.png";
    goto have_uri;
    }
  else if(!strcmp(klass, GAVL_META_MEDIA_CLASS_AUDIO_BROADCAST))
    {
    if(!(uri = gavl_dictionary_get_string(metadata, GAVL_META_LOGO_URL)))
      uri = DATA_DIR"/web/icons/radio_nocover.png";
    goto have_uri;
    }
  else if(!strcmp(klass, GAVL_META_MEDIA_CLASS_VIDEO_FILE) ||
          !strcmp(klass, GAVL_META_MEDIA_CLASS_MOVIE) ||
          !strcmp(klass, GAVL_META_MEDIA_CLASS_MOVIE_PART))
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
  else if(!strcmp(klass, GAVL_META_MEDIA_CLASS_TV_EPISODE))
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

  if(mimetype)
    fprintf(stderr, "load cover %s %s\n", uri, mimetype);
  else
    fprintf(stderr, "load cover %s\n", uri);
    
  if(!mimetype)
    goto fail;
     
  if(!(info = bg_plugin_find_by_mimetype(mimetype, BG_PLUGIN_IMAGE_READER)))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "No plugin found for mime type %s", mimetype);
    goto fail;
    }
  
  if(!(h = bg_plugin_load(info)))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Loading %s failed", info->name);
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


static int probe_image(bg_plugin_registry_t * r,
                       const char * filename,
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
  
  if(!probe_image(r, filename, format,
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
        if(!bg_lv_load(ret, info->name, info->flags, NULL))
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
      if(!strcmp(meta_plugins[i].name, info->name))
        {
        ret->plugin = meta_plugins[i].get_plugin();
        ret->priv   = meta_plugins[i].create();
        break;
        }
      i++;
      }
    }
  
  ret->info = info;

  bg_plugin_handle_connect_control(ret);
  
  if(ret->ctrl_plugin && bg_plugin_reg->state)
    {
    bg_state_apply_ctx(bg_plugin_reg->state, ret->info->name, ret->ctrl_ext.cmd_sink, BG_CMD_SET_STATE);

    /* Some plugin types have generic state variables also */

    if(info->type == BG_PLUGIN_OUTPUT_VIDEO)
      {
      //      fprintf(stderr, "Apply ov state %p\n", ret->plugin_reg->state);
      //      gavl_dictionary_dump(ret->plugin_reg->state, 2);
      
      bg_state_apply_ctx(bg_plugin_reg->state, BG_STATE_CTX_OV, ret->ctrl_ext.cmd_sink, BG_CMD_SET_STATE);
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
  bg_cfg_section_t * section;

  gavl_dictionary_t tmp_section;

  gavl_dictionary_init(&tmp_section);
  
  /* Apply saved parameters */

  if(!ret->plugin->get_parameters)
    return;

  parameters = ret->plugin->get_parameters(ret->priv);
  
  section = bg_plugin_registry_get_section(bg_plugin_reg, ret->info->name);
  
  if(user_params)
    {
    gavl_dictionary_merge(&tmp_section, user_params, section);
    gavl_dictionary_set(&tmp_section, BG_CFG_TAG_NAME, NULL);
    section = &tmp_section;
    
    }
  
  bg_cfg_section_apply(section, parameters, ret->plugin->set_parameter,
                       ret->priv);
  
  gavl_dictionary_free(&tmp_section);
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
    return NULL;

  ret = load_plugin(info);

  if(ret && ret->plugin->set_parameter)
    apply_parameters(ret, dict);
  
  return ret;
  
  }

bg_plugin_handle_t * bg_ov_plugin_load(const gavl_dictionary_t * options,
                                       const char * window_id)
  {
  bg_plugin_handle_t * ret;
  const char * name;
  const bg_plugin_info_t * info;

  if(!options)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "No OV plugin to load");
    return NULL;
    }
  
  if(!(name = gavl_dictionary_get_string(options, BG_CFG_TAG_NAME)))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Plugin config doesn't contain a %s field", BG_CFG_TAG_NAME);
    return NULL;
    }

  if(!(info = bg_plugin_find_by_name(name)))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "No such plugin: %s", name);
    return NULL;
    }

  if(info->type != BG_PLUGIN_OUTPUT_VIDEO)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "No plugin %s has wrong type (Should be video output)", name);
    return NULL;
    }
  
  ret = load_plugin(info);
  
  if(window_id)
    {
    gavl_value_t val;
    gavl_value_init(&val);
    gavl_value_set_string(&val, window_id);
    
    bg_plugin_handle_set_state(ret, BG_STATE_CTX_OV, BG_STATE_OV_WINDOW_ID, &val);
    gavl_value_free(&val);
    }
  
  if(ret)
    apply_parameters(ret, options);
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

void bg_plugin_registry_add_device(bg_plugin_registry_t * reg,
                                   const char * plugin_name,
                                   const char * device,
                                   const char * name)
  {
  bg_plugin_info_t * info;

  info = find_by_name(reg->entries, plugin_name);
  if(!info)
    return;

  info->devices = bg_device_info_append(info->devices,
                                        device, name);

  bg_plugin_registry_save(reg->entries);
  }

void bg_plugin_registry_set_device_name(bg_plugin_registry_t * reg,
                                        const char * plugin_name,
                                        const char * device,
                                        const char * name)
  {
  int i;
  bg_plugin_info_t * info;

  info = find_by_name(reg->entries, plugin_name);
  if(!info || !info->devices)
    return;
  
  i = 0;
  while(info->devices[i].device)
    {
    if(!strcmp(info->devices[i].device, device))
      {
      info->devices[i].name = gavl_strrep(info->devices[i].name, name);
      bg_plugin_registry_save(reg->entries);
      return;
      }
    i++;
    }
  
  }

static int my_strcmp(const char * str1, const char * str2)
  {
  if(!str1 && !str2)
    return 0;
  else if(str1 && str2)
    return strcmp(str1, str2); 
  return 1;
  }

void bg_plugin_registry_remove_device(bg_plugin_registry_t * reg,
                                      const char * plugin_name,
                                      const char * device,
                                      const char * name)
  {
  bg_plugin_info_t * info;
  int index;
  int num_devices;
  info = find_by_name(reg->entries, plugin_name);
  if(!info)
    return;
    
  index = -1;
  num_devices = 0;
  while(info->devices[num_devices].device)
    {
    if(!my_strcmp(info->devices[num_devices].name, name) &&
       !strcmp(info->devices[num_devices].device, device))
      {
      index = num_devices;
      }
    num_devices++;
    }


  if(index != -1)
    memmove(&info->devices[index], &info->devices[index+1],
            sizeof(*(info->devices)) * (num_devices - index));
    
  bg_plugin_registry_save(reg->entries);
  }

void bg_plugin_registry_find_devices(bg_plugin_registry_t * reg,
                                     const char * plugin_name)
  {
  bg_plugin_info_t * info;
  bg_plugin_handle_t * handle;
  
  info = find_by_name(reg->entries, plugin_name);
  if(!info)
    return;

  handle = bg_plugin_load(info);
    
  bg_device_info_destroy(info->devices);
  info->devices = NULL;
  
  if(!handle || !handle->plugin->find_devices)
    return;

  info->devices = handle->plugin->find_devices();
  bg_plugin_registry_save(reg->entries);
  bg_plugin_unref(handle);
  }

char ** bg_plugin_registry_get_plugins(uint32_t type_mask,
                                       uint32_t flag_mask)
  {
  int num_plugins, i;
  char ** ret;
  const bg_plugin_info_t * info;
  
  num_plugins = bg_get_num_plugins(type_mask, flag_mask);
  ret = calloc(num_plugins + 1, sizeof(char*));
  for(i = 0; i < num_plugins; i++)
    {
    info = bg_plugin_find_by_index(i, type_mask, flag_mask);
    ret[i] = gavl_strdup(info->name);
    }
  return ret;
  
  }

void bg_plugin_registry_free_plugins(char ** plugins)
  {
  int index = 0;
  if(!plugins)
    return;
  while(plugins[index])
    {
    free(plugins[index]);
    index++;
    }
  free(plugins);
  
  }

static void load_input_plugin(bg_plugin_registry_t * reg,
                              const bg_plugin_info_t * info,
                              const gavl_dictionary_t * options,
                              bg_plugin_handle_t ** ret)
  {
  if(!(*ret) || !(*ret)->info || strcmp((*ret)->info->name, info->name))
    {
    if(*ret)
      {
      bg_plugin_unref(*ret);
      *ret = NULL;
      }

    if(options)
      bg_plugin_load_with_options(options);
    else if(!strcmp(info->name, "i_bgplug"))
      *ret = bg_input_plugin_create_plug();
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
  
  if(access(filename, R_OK) || !probe_image(plugin_reg, filename, &fmt, &m, NULL))
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
  
  file = bg_sprintf("%s/cover.jpg", path);
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
  
  file = bg_sprintf("%s/%s.jpg", path, basename);
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
  
  file = bg_sprintf("%s/%s.fanart.jpg", path, basename);
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

static void detect_nfo(const char * path, const char * basename, gavl_dictionary_t * dict)
  {
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
    return;
  
  file = bg_sprintf("%s/%s.nfo", path, basename);
  
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
  
  fail:
  if(in)
    fclose(in);
  if(doc)
    xmlFreeDoc(doc);
  free(file);
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
  container_klass = gavl_dictionary_get_string(container_m, GAVL_META_MEDIA_CLASS);

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
  
  if(!strcmp(container_klass, GAVL_META_MEDIA_CLASS_TV_SEASON))
    {
    int season = 0;

    gavl_dictionary_get_int(child_m, GAVL_META_SEASON, &season);
    gavl_dictionary_set_int(container_m, GAVL_META_SEASON, season);
    gavl_dictionary_set_string_nocopy(container_m, GAVL_META_TITLE, bg_sprintf("Season %d", season));
    
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
  else if(!strcmp(container_klass, GAVL_META_MEDIA_CLASS_TV_SHOW))
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
  else if(!strcmp(container_klass, GAVL_META_MEDIA_CLASS_MUSICALBUM))
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
  
  if((klass = gavl_dictionary_get_string(m, GAVL_META_MEDIA_CLASS)))
    {
    if(basename && !strcmp(klass, GAVL_META_MEDIA_CLASS_MOVIE_PART))
      {
      char * pos = strrchr(basename, ')'); // Closing parenthesis of the year
      if(pos)
        {
        pos++;
        *pos = '\0';
        }
      }
    
    if((!strcmp(klass, GAVL_META_MEDIA_CLASS_MOVIE) ||
        !strcmp(klass, GAVL_META_MEDIA_CLASS_MOVIE_PART)))
      {
      detect_movie_poster(bg_plugin_reg, path, basename, m);
      detect_movie_wallpaper(bg_plugin_reg, path, basename, m);
      detect_nfo(path, basename, m);
      create_language_arrays(ti);
      bg_track_find_subtitles(ti);
      }
    else if(!strcmp(klass, GAVL_META_MEDIA_CLASS_SONG))
      {
      if(path)
        detect_album_cover(bg_plugin_reg, path, m);
      }
    else if(!strcmp(klass, GAVL_META_MEDIA_CLASS_TV_EPISODE))
      {
      gavl_dictionary_t show_m;
      char * pos;
      
      char * path = gavl_strdup(location);

      if((pos = strrchr(path, '/')))
        *pos = '\0';

      gavl_dictionary_init(&show_m);
      gavl_dictionary_set_string(&show_m, GAVL_META_MEDIA_CLASS, GAVL_META_MEDIA_CLASS_TV_SHOW);
      
      detect_nfo(path, gavl_dictionary_get_string(m, GAVL_META_SHOW), &show_m);
      gavl_dictionary_set(m, GAVL_META_SHOW, gavl_dictionary_get(&show_m, GAVL_META_TITLE));
      gavl_dictionary_free(&show_m);
      bg_track_find_subtitles(ti);
      create_language_arrays(ti);

      
      if(!detect_movie_poster(bg_plugin_reg, path, basename, m))
        {
        int result;
        const char * pos;
        char * parent_basename;

        pos = gavl_detect_episode_tag(basename, NULL, NULL, NULL);

        pos++;
        while(isdigit(*pos))
          pos++;
        
        parent_basename = gavl_strndup(basename, pos);
        result = detect_movie_poster(bg_plugin_reg, path, parent_basename, m);
        free(parent_basename);

        if(!result)
          {
          parent_basename = gavl_strdup(gavl_dictionary_get_string(m, GAVL_META_SHOW));
          result = detect_movie_poster(bg_plugin_reg, path, parent_basename, m);
          free(parent_basename);
          }
        }
      
      if(!detect_movie_wallpaper(bg_plugin_reg, path, basename, m))
        {
        int result;
        const char * pos;
        char * parent_basename;

        pos = gavl_detect_episode_tag(basename, NULL, NULL, NULL);

        pos++;
        while(isdigit(*pos))
          pos++;
        
        parent_basename = gavl_strndup(basename, pos);
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
    gavl_dictionary_set_string(m, GAVL_META_MEDIA_CLASS, GAVL_META_MEDIA_CLASS_FILE);
  
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
  
  int num_plugins, i;
  uint32_t flags;
  bg_input_plugin_t * plugin;
  int try_and_error = 1;
  const bg_plugin_info_t * first_plugin = NULL;
  
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
      if(bg_url_split(location,
                      &protocol,
                      NULL, // user,
                      NULL, // password,
                      NULL, // hostname,
                      NULL,   //  port,
                      &path))
        info = bg_plugin_find_by_protocol(protocol);
      }
    else if(!strcmp(location, "-"))
      {
      info = bg_plugin_find_by_protocol("stdin");
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
                                           info->long_name);
      return 0;
      }
    
    plugin = (bg_input_plugin_t*)((*ret)->plugin);

    if(!plugin->open((*ret)->priv, real_location))
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, TRS("Opening %s with \"%s\" failed"),
             real_location, info->long_name);
      }
    else
      {
      input_plugin_finalize(*ret, real_location);

      if(protocol)
        free(protocol);
      if(path)
        free(path);
      return 1;
      }
    }
  
  if(protocol) free(protocol);
  if(path)     free(path);
  
  if(!try_and_error)
    return 0;
  
  flags = bg_string_is_url(real_location) ? BG_PLUGIN_URL : BG_PLUGIN_FILE;
  
  num_plugins = bg_get_num_plugins(BG_PLUGIN_INPUT, flags);
  for(i = 0; i < num_plugins; i++)
    {
    info = bg_plugin_find_by_index(i, BG_PLUGIN_INPUT, flags);

    if(info == first_plugin)
      continue;
        
    load_input_plugin(bg_plugin_reg, info, NULL, ret);

    if(!*ret)
      continue;
    
    plugin = (bg_input_plugin_t*)((*ret)->plugin);
    if(!plugin->open((*ret)->priv, real_location))
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, TRS("Opening %s with \"%s\" failed"),
             location, info->long_name);
      }
    else
      {
      input_plugin_finalize(*ret, real_location);
      return 1;
      }
    }
  return 0;
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
  gavl_dictionary_set(vars, BG_URL_VAR_TRACK,   NULL);
  gavl_dictionary_set(vars, BG_URL_VAR_VARIANT, NULL);
  gavl_dictionary_set(vars, BG_URL_VAR_SEEK,    NULL);
  gavl_dictionary_set(vars, BG_URL_VAR_PLUGIN,  NULL);
  gavl_dictionary_set(vars, BG_URL_VAR_CMDLINE, NULL);
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

    if((klass = gavl_dictionary_get_string(m, GAVL_META_MEDIA_CLASS)) &&
       !strcmp(klass, GAVL_META_MEDIA_CLASS_LOCATION))
      continue;

    if(!(src = gavl_metadata_get_src_nc(m, GAVL_META_SRC, 0)))
      src = gavl_metadata_add_src(m, GAVL_META_SRC, NULL, NULL);

    new_location = gavl_strdup(location);
    
    if(num > 1)
      {
      gavl_dictionary_t vars;
      gavl_dictionary_init(&vars);
      
      gavl_url_get_vars(new_location, &vars);
      gavl_dictionary_set_int(&vars, BG_URL_VAR_TRACK, i+1);
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

#if 0
static int input_plugin_load_full(bg_plugin_registry_t * reg,
                                  const char * location,
                                  bg_plugin_handle_t ** ret,
                                  char ** redirect_url)
  {
  const char * url;
  const char * klass;
  gavl_dictionary_t * track_info;
  int track_index = 0;
  gavl_dictionary_t vars;
  int result = 0;
  gavl_dictionary_t * tm;
  int variant = 0;
  
  gavl_dictionary_init(&vars);
  gavl_url_get_vars_c(location, &vars);

  if(gavl_dictionary_get_int(&vars, BG_URL_VAR_TRACK, &track_index))
    track_index--;
  
  gavl_dictionary_get_int(&vars, BG_URL_VAR_VARIANT, &variant);
  
  gavl_dictionary_free(&vars);
  
  if(!(*ret = bg_input_plugin_load(location)))
    goto end;
  
  track_info = bg_input_plugin_get_track_info(*ret, track_index);
  
  if(!track_info)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "No track %d in %s", track_index + 1, location);
    bg_plugin_unref(*ret);
    return 0;
    }
  
  tm = gavl_track_get_metadata_nc(track_info);

  /* Do redirection */
  if((klass = gavl_dictionary_get_string(tm, GAVL_META_MEDIA_CLASS)) &&
     !strcmp(klass, GAVL_META_MEDIA_CLASS_LOCATION))
    {
    //    const gavl_dictionary_t * src;

#if 0 // TODO: Handle multivariant    
    val_i = 0;
    gavl_dictionary_get_int(tm, GAVL_META_MULTIVARIANT, &val_i);
    if(val_i)
      {
      int num_src = gavl_dictionary_get_num_items(tm, GAVL_META_SRC);
      if(variant >= num_src)
        {
        gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "No such variant %d. Track seems unplayable.", variant);
        goto end;
        }
      
      gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Choosing variant %d", variant);
      src = gavl_metadata_get_src(tm, GAVL_META_SRC, variant, NULL, &url);
      }
    else
#endif
      

    if(gavl_metadata_get_src(tm, GAVL_META_SRC, 0, NULL, &url) && url)
      {
      gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Got redirected to %s", url);
      *redirect_url = gavl_strdup(url);
      return 1;
      }
    }
  else
    result = 1;
  
  if(!result)
    goto end;
  
  /* Select track */
  bg_input_plugin_set_track(*ret, track_index);
  //  fprintf(stderr, "Select track:\n");
  //  gavl_dictionary_dump(track_info, 2);

  /* Check for external subtitles */
  if(gavl_track_get_num_video_streams(track_info) > 0)
    bg_track_find_subtitles(track_info);

  /* Check for external streams */
  if(gavl_track_get_num_external_streams(track_info))
    {
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, " %s", url);
    }
  
  end:

  if(!result)
    {
    if(*ret)
      {
      bg_plugin_unref(*ret);
      *ret = NULL;
      }
    }
  
  return result;
  }
#endif

bg_plugin_handle_t * bg_input_plugin_load_full(const char * location)
  {
  bg_plugin_handle_t * ret;
  gavl_dictionary_t track;
  gavl_dictionary_init(&track);
  gavl_track_from_location(&track, location);
  ret = bg_load_track(&track);
  gavl_dictionary_free(&track);
  return ret;
  }

#if 0
bg_plugin_handle_t * bg_input_plugin_load_full(const char * location)
  {
  int i;
  char * redirect_url = NULL;
  char * location = gavl_strdup(location1);
  
  for(i = 0; i < MAX_REDIRECTIONS; i++)
    {
    if(!input_plugin_load_full(reg,
                               location,
                               ret,
                               &redirect_url))
      {
      free(location);
      return 0;
      }
    
    if(redirect_url)
      {
      free(location);
      location = redirect_url;
      redirect_url = NULL;
      }
    else
      {
      free(location);
      return 1;
      }
    }
  
  gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Too many redirections");
  return 0;
  }
#endif

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
      .flags     = BG_PARAMETER_OWN_SECTION,
    },
    { } // End
  };

static const bg_parameter_info_t encoder_section_video[] =
  {
    {
      .name      = "$video",
      .long_name = TRS("Video"),
      .type      = BG_PARAMETER_SECTION,
      .flags     = BG_PARAMETER_OWN_SECTION,
    },
    { } // End
  };

static const bg_parameter_info_t encoder_section_overlay[] =
  {
    {
      .name      = "$overlay",
      .long_name = TRS("Overlay subtitles"),
      .type      = BG_PARAMETER_SECTION,
      .flags     = BG_PARAMETER_OWN_SECTION,
    },
    { } // End
  };

static const bg_parameter_info_t encoder_section_text[] =
  {
    {
      .name      = "$text",
      .long_name = TRS("Text subtitles"),
      .type      = BG_PARAMETER_SECTION,
      .flags     = BG_PARAMETER_OWN_SECTION,
    },
    { } // End
  };

static bg_parameter_info_t *
create_encoder_parameters(const bg_plugin_info_t * info, int stream_params)
  {
  bg_parameter_info_t * ret = NULL;
  
  //  if(!strcmp(info->name, "e_mpeg"))
  //    fprintf(stderr, "e_mpeg\n");
  
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

  if(ret)
    {
    char * tmp_string;
    ret->flags |= BG_PARAMETER_GLOBAL_PRESET;
    tmp_string = bg_sprintf("plugins/%s", info->name);
    ret->preset_path = gavl_strrep(ret->preset_path, tmp_string);
    free(tmp_string);
    }

  //  if(!strcmp(info->name, "e_mpeg"))
  //    bg_parameters_dump(ret, "encoder_parameters");
    
  return ret;
  }

static void set_parameter_info(bg_plugin_registry_t * reg,
                               uint32_t type_mask,
                               uint32_t flag_mask,
                               bg_parameter_info_t * ret, int stream_params)
  {
  int num_plugins, start_entries, i;
  const bg_plugin_info_t * info;
  
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

  ret->flags |= BG_PARAMETER_PLUGIN;
  
  for(i = 0; i < num_plugins; i++)
    {
    info = bg_plugin_find_by_index(i,
                                   type_mask, flag_mask);
    ret->multi_names_nc[start_entries+i] = gavl_strdup(info->name);

    /* First plugin is the default one */

    if((ret->type != BG_PARAMETER_MULTI_CHAIN) && !ret->val_default.v.str)
      gavl_value_set_string(&ret->val_default, info->name);
    
    bg_bindtextdomain(info->gettext_domain, info->gettext_directory);
    ret->multi_descriptions_nc[start_entries+i] = gavl_strdup(TRD(info->description,
                                                                  info->gettext_domain));
    
    ret->multi_labels_nc[start_entries+i] =
      gavl_strdup(TRD(info->long_name,
                          info->gettext_domain));
    
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

static const bg_parameter_info_t registry_settings_parameter =
  {
    .name = "$registry",
    .long_name = TRS("Registry settings"),
    .type = BG_PARAMETER_SECTION,
  };

static const bg_parameter_info_t plugin_settings_parameter =
  {
    .name = "$plugin",
    .long_name = TRS("Plugin settings"),
    .type = BG_PARAMETER_SECTION,
  };

#if 0
static const bg_parameter_info_t extensions_parameter =
  {
    .name = "$extensions",
    .long_name = TRS("Extensions"),
    .type = BG_PARAMETER_STRING,
  };

static const bg_parameter_info_t protocols_parameter =
  {
    .name = "$protocols",
    .long_name = TRS("Protocols"),
    .type = BG_PARAMETER_STRING,
  };
#endif

static const bg_parameter_info_t priority_parameter =
  {
    .name = "$priority",
    .long_name = TRS("Priority"),
    .type = BG_PARAMETER_INT,
    .val_min = GAVL_VALUE_INIT_INT(1),
    .val_max = GAVL_VALUE_INIT_INT(10),
  };

void bg_plugin_registry_set_parameter_info_input(bg_plugin_registry_t * reg,
                                                 uint32_t type_mask,
                                                 uint32_t flag_mask,
                                                 bg_parameter_info_t * ret)
  {
  int num_plugins, i;
  const bg_plugin_info_t * info;
  int index, index1, num_parameters;
  
  num_plugins =
    bg_get_num_plugins(type_mask, flag_mask);

  ret->type = BG_PARAMETER_MULTI_LIST;
  
  ret->multi_names_nc      = calloc(num_plugins + 1, sizeof(*ret->multi_names));
  ret->multi_labels_nc     = calloc(num_plugins + 1, sizeof(*ret->multi_labels));
  ret->multi_parameters_nc = calloc(num_plugins + 1,
                                 sizeof(*ret->multi_parameters));

  ret->multi_descriptions_nc = calloc(num_plugins + 1,
                                   sizeof(*ret->multi_descriptions));

  bg_parameter_info_set_const_ptrs(ret);
  
  for(i = 0; i < num_plugins; i++)
    {
    info = bg_plugin_find_by_index(i,
                                   type_mask, flag_mask);
    ret->multi_names_nc[i] = gavl_strdup(info->name);

    /* First plugin is the default one */
    if(!i && (ret->type != BG_PARAMETER_MULTI_CHAIN)) 
      {
      gavl_value_set_string(&ret->val_default, info->name);
      }
    
    bg_bindtextdomain(info->gettext_domain, info->gettext_directory);
    ret->multi_descriptions_nc[i] = gavl_strdup(TRD(info->description,
                                                        info->gettext_domain));
    
    ret->multi_labels_nc[i] = gavl_strdup(TRD(info->long_name,
                                               info->gettext_domain));

    /* Create parameters: Extensions and protocols are added to the array
       if necessary */

    num_parameters = 1; /* Priority */
    if(info->flags & BG_PLUGIN_FILE)
      num_parameters++;
    if(info->flags & BG_PLUGIN_URL)
      num_parameters++;

    if(info->parameters && (info->parameters[0].type != BG_PARAMETER_SECTION))
      num_parameters++; /* Plugin section */

    if(info->parameters)
      num_parameters++; /* Registry */
    
    //    prefix = bg_sprintf("%s.", info->name);

    
    if(info->parameters)
      {
      index = 0;
      while(info->parameters[index].name)
        {
        index++;
        num_parameters++;
        }
      }
    
    ret->multi_parameters_nc[i] =
      calloc(num_parameters+1, sizeof(*ret->multi_parameters_nc[i]));

    index = 0;

    /* Now, build the parameter array */

    if(info->parameters && (info->parameters[0].type != BG_PARAMETER_SECTION))
      {
      bg_parameter_info_copy(&ret->multi_parameters_nc[i][index],
                             &plugin_settings_parameter);
      index++;
      }
    
    if(info->parameters)
      {
      index1 = 0;

      while(info->parameters[index1].name)
        {
        bg_parameter_info_copy(&ret->multi_parameters_nc[i][index],
                               &info->parameters[index1]);
        index++;
        index1++;
        }
      }

    if(info->parameters)
      {
      bg_parameter_info_copy(&ret->multi_parameters_nc[i][index],
                             &registry_settings_parameter);
      index++;
      }


    bg_parameter_info_copy(&ret->multi_parameters_nc[i][index],
                           &priority_parameter);
    
    gavl_value_set_int(&ret->multi_parameters_nc[i][index].val_default, info->priority);
    index++;
    }
  
  }

static int find_parameter_input(bg_plugin_registry_t * plugin_reg,
                                const char * name,
                                const bg_parameter_info_t ** parameter_info,
                                bg_plugin_info_t ** plugin_info,
                                bg_cfg_section_t ** section,
                                const char ** parameter_name)
  {
  const char * pos1;
  const char * pos2;
  char * plugin_name;
  int ret = 0;
  
  pos1 = strchr(name, '.');
  if(!pos1)
    return 0;
  pos1++;

  pos2 = strchr(pos1, '.');
  if(!pos2)
    return 0;

  plugin_name = gavl_strndup( pos1, pos2);
  pos2++;

  *parameter_name = pos2;
  
  *plugin_info = find_by_name(plugin_reg->entries, plugin_name);
  if(!(*plugin_info))
    goto fail;
  
  if(*pos2 != '$')
    {
    *section = bg_plugin_registry_get_section(plugin_reg,
                                              plugin_name);
    
    *parameter_info = bg_parameter_find((*plugin_info)->parameters, pos2);
    if(!(*parameter_info))
      goto fail;
    }
  else
    {
    *section = NULL;
    *parameter_info = NULL;
    }
  
  //  fprintf(stderr, "name: %s, plugin: %s, parameter: %s, section: %p, pi: %p\n",
  //          name, plugin_name, *parameter_name, *section, *parameter_info);
  
  ret = 1;
  fail:
  free(plugin_name);
  return ret;
  }

void bg_plugin_registry_set_parameter_input(void * data, const char * name,
                                            const gavl_value_t * val)
  {
  bg_plugin_registry_t * plugin_reg = data;
  bg_cfg_section_t * cfg_section;
  const bg_parameter_info_t * parameter_info;
  bg_plugin_info_t * plugin_info;
  const char * parameter_name;
  
  if(!name)
    return;

  //  fprintf(stderr,
  //          "bg_plugin_registry_set_parameter_input\n");

  if(!find_parameter_input(plugin_reg, name, &parameter_info,
                           &plugin_info, &cfg_section, &parameter_name))
    return;
  
  if(!strcmp(parameter_name, "$priority"))
    bg_plugin_registry_set_priority(plugin_reg, plugin_info->name, val->v.i);
  else
    bg_cfg_section_set_parameter(cfg_section, parameter_info, val);
  }

int bg_plugin_registry_get_parameter_input(void * data, const char * name,
                                            gavl_value_t * val)
  {
  bg_plugin_registry_t * plugin_reg = data;
  bg_cfg_section_t * cfg_section;
  const bg_parameter_info_t * parameter_info;
  bg_plugin_info_t * plugin_info;
  const char * parameter_name;
  
  if(!name)
    return 0;

  //  fprintf(stderr,
  // "bg_plugin_registry_get_parameter_input\n");
  
  if(!find_parameter_input(plugin_reg, name, &parameter_info,
                           &plugin_info, &cfg_section, &parameter_name))
    return 0;
    
  if(!strcmp(parameter_name, "$priority"))
    val->v.i = plugin_info->priority;
  else
    bg_cfg_section_get_parameter(cfg_section, parameter_info, val);
  return 1;
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

  /* Set preset path */
  
  ret[0].preset_path = gavl_strdup("encoders");
  
  return ret;
  }

static const gavl_value_t * get_encoder_context(const bg_cfg_section_t * s,
                                                gavl_stream_type_t stream_type)
  {
  
  switch(stream_type)
    {
    case GAVL_STREAM_AUDIO:
      return gavl_dictionary_get(s, "ae");
      break;
    case GAVL_STREAM_TEXT:
      return  gavl_dictionary_get(s, "te");
      break;
    case GAVL_STREAM_OVERLAY:
      return gavl_dictionary_get(s, "oe");
      break;
    case GAVL_STREAM_VIDEO:
      return gavl_dictionary_get(s, "ve");
      break;
    default:
      return NULL;
    }
  }

const char * 
bg_encoder_section_get_plugin(const bg_cfg_section_t * s,
                              gavl_stream_type_t stream_type)
  {
  const gavl_value_t * val;
  const gavl_dictionary_t * dict;
  const char * ret = NULL;

  val = get_encoder_context(s, stream_type);
  
  dict = bg_multi_menu_get_selected(val);
  ret = gavl_dictionary_get_string(dict, BG_CFG_TAG_NAME);
  
  if(!strcmp(ret, "$to_video"))
    return NULL;
  
  return ret;
  }

void
bg_encoder_section_get_plugin_config(bg_plugin_registry_t * plugin_reg,
                                     const bg_cfg_section_t * s,
                                     gavl_stream_type_t stream_type,
                                     const bg_cfg_section_t ** section_ret,
                                     const bg_parameter_info_t ** params_ret)
  {
  const gavl_value_t * val;
  
  const char * plugin_name;
  const bg_plugin_info_t * info;

  plugin_name =
    bg_encoder_section_get_plugin(s, stream_type);
  
  if(section_ret)
    *section_ret = NULL;
  if(params_ret)
    *params_ret = NULL;

  if(!plugin_name)
    return;

  val = get_encoder_context(s, stream_type);

  if(section_ret)
    *section_ret = bg_multi_menu_get_selected(val);
  
  info = bg_plugin_find_by_name(plugin_name);
  
  if(!info->parameters)
    return;
  
  if(params_ret)
    {
    *params_ret = info->parameters;
    }
  }

void
bg_encoder_section_get_stream_config(bg_plugin_registry_t * plugin_reg,
                                     const bg_cfg_section_t * s,
                                     gavl_stream_type_t stream_type,
                                     const bg_cfg_section_t ** section_ret,
                                     const bg_parameter_info_t ** params_ret)
  {
  const gavl_value_t * val = NULL;
  
  const char * plugin_name;
  const bg_plugin_info_t * info;
  const bg_cfg_section_t * subsection = NULL;
  
  plugin_name =
    bg_encoder_section_get_plugin(s, stream_type);
  
  if(!plugin_name)
    {
    plugin_name =
      bg_encoder_section_get_plugin(s, GAVL_STREAM_VIDEO);

    val = gavl_dictionary_get(s, "ve");
    }
  
  info = bg_plugin_find_by_name(plugin_name);
  
  if(section_ret)
    *section_ret = NULL;
  if(params_ret)
    *params_ret = NULL;
  
  switch(stream_type)
    {
    case GAVL_STREAM_AUDIO:
      if(params_ret)
        *params_ret = info->audio_parameters;
      
      if(section_ret && info->audio_parameters)
        {
        if(!val)
          val = gavl_dictionary_get(s, "ae");

        subsection = bg_multi_menu_get_selected(val);
        
        *section_ret = bg_cfg_section_find_subsection_c(subsection, "$audio");
        }

      break;
    case GAVL_STREAM_TEXT:
      if(params_ret)
        *params_ret = info->text_parameters;

      if(section_ret && info->text_parameters)
        {
        if(!val)
          val = gavl_dictionary_get(s, "te");

        subsection = bg_multi_menu_get_selected(val);
        *section_ret = bg_cfg_section_find_subsection_c(subsection, "$text");
        
        }
      
      break;
    case GAVL_STREAM_OVERLAY:
      if(params_ret)
        *params_ret = info->overlay_parameters;

      if(section_ret && info->overlay_parameters)
        {
        if(!val)
          val = gavl_dictionary_get(s, "oe");

        subsection = bg_multi_menu_get_selected(val);
        *section_ret = bg_cfg_section_find_subsection_c(subsection, "$overlay");
        
        }
      
      break;
    case GAVL_STREAM_VIDEO:
      if(params_ret)
        *params_ret = info->video_parameters;

      if(section_ret && info->video_parameters)
        {
        if(!val)
          val = gavl_dictionary_get(s, "ve");

        subsection = bg_multi_menu_get_selected(val);
        *section_ret = bg_cfg_section_find_subsection_c(subsection, "$video");
        }
      break;
    case GAVL_STREAM_NONE:
    case GAVL_STREAM_MSG:
      break;
    }
  }

#if 0
static const bg_parameter_info_t compressor_parameters[] =
  {
    {
      .name = "codec",
      .long_name = TRS("Codec"),
      .type = BG_PARAMETER_MULTI_MENU,
      .flags = BG_PARAMETER_PLUGIN,
      .val_default = GAVL_VALUE_INIT_STRING("none"),
      .multi_names = (const char *[]){ "none", NULL },
      .multi_labels = (const char *[]){ TRS("None"), NULL },
      .multi_descriptions = (const char *[]){ TRS("Write stream as uncompressed if possible"), NULL },
      .multi_parameters = (const bg_parameter_info_t*[]) { NULL, NULL },
    },
    { /* End */ },
  };
#endif

#if 0
bg_parameter_info_t *
bg_plugin_registry_create_compressor_parameters(bg_plugin_registry_t * plugin_reg,
                                                bg_plugin_type_t type)
  {
  bg_parameter_info_t * ret =
    bg_parameter_info_copy_array(compressor_parameters);
  bg_plugin_registry_set_parameter_info(plugin_reg, type, 0, ret);
  return ret;
  }
#endif

void
bg_plugin_registry_set_compressor_parameter(bg_plugin_registry_t * plugin_reg,
                                            bg_plugin_handle_t ** plugin,
                                            const char * name,
                                            const gavl_value_t * val)
  {
  if(!name)
    {
    if(*plugin && (*plugin)->plugin->set_parameter)
      (*plugin)->plugin->set_parameter((*plugin)->priv, NULL, NULL);
    return;
    }

  if(!strcmp(name, "codec"))
    {
    if(*plugin && (!val->v.str || strcmp((*plugin)->info->name, val->v.str)))
      {
      bg_plugin_unref(*plugin);
      *plugin = NULL;
      }

    if(val->v.str && !strcmp(val->v.str, "none"))
      return;
    
    if(val->v.str && !(*plugin))
      {
      const bg_plugin_info_t * info;
      info = bg_plugin_find_by_name(val->v.str);
      if(!info)
        {
        gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Cannot find plugin %s",
               val->v.str);
        return;
        }
      *plugin = bg_plugin_load(info);
      }
    
    }
  else
    {
    if(*plugin)
      (*plugin)->plugin->set_parameter((*plugin)->priv, name, val);
    }
  
  }

gavl_codec_id_t
bg_plugin_registry_get_compressor_id(bg_plugin_registry_t * plugin_reg,
                                     bg_cfg_section_t * section)
  {
  const bg_plugin_info_t * info;
  const char * codec = NULL;
  if(!bg_cfg_section_get_parameter_string(section, "codec", &codec))
    return GAVL_CODEC_ID_NONE;

  info = bg_plugin_find_by_name(codec);
  if(!info)
    return GAVL_CODEC_ID_NONE;
  return info->compressions[0];
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
    bg_msg_sink_put(h->ctrl_ext.cmd_sink, cmd);
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

#if 0
typedef struct
  {
  int scale;
  int64_t * time;
  } seek_t;

static int handle_msg_seek(void * data, gavl_msg_t * msg)
  {
  seek_t * s = data;

  if((msg->NS == GAVL_MSG_NS_SRC) && 
     (msg->ID == GAVL_MSG_SRC_RESYNC_1))
    {
    int64_t msg_time = gavl_msg_get_arg_long(msg, 0);
    int msg_scale    = gavl_msg_get_arg_int(msg, 1);

    fprintf(stderr, "Got seek resync\n");
    
    *s->time = gavl_time_rescale(msg_scale, s->scale, msg_time);
    }
  return 1;
  }
#endif

void bg_input_plugin_seek(bg_plugin_handle_t * h, int64_t * time, int scale)
  {
  gavl_msg_t * cmd;

  //  fprintf(stderr, "bg_input_plugin_seek\n");
  
  cmd = bg_msg_sink_get(h->control.cmd_sink);
  gavl_msg_set_id_ns(cmd, GAVL_CMD_SRC_SEEK, GAVL_MSG_NS_SRC);
  gavl_msg_set_arg_long(cmd, 0, *time);
  gavl_msg_set_arg_int(cmd, 1, scale);
  
  bg_msg_sink_put(h->control.cmd_sink, cmd);
  //  fprintf(stderr, "bg_input_plugin_seek done\n");
  }

void bg_input_plugin_start(bg_plugin_handle_t * h)
  {
  gavl_msg_t * cmd;

  if(!h->ctrl_ext.cmd_sink)
    return;
  
  cmd = bg_msg_sink_get(h->ctrl_ext.cmd_sink);
  gavl_msg_set_id_ns(cmd, GAVL_CMD_SRC_START, GAVL_MSG_NS_SRC);
  bg_msg_sink_put(h->ctrl_ext.cmd_sink, cmd);
  }

void bg_input_plugin_pause(bg_plugin_handle_t * h)
  {
  gavl_msg_t * cmd;

  if(!h->ctrl_ext.cmd_sink)
    return;

  cmd = bg_msg_sink_get(h->ctrl_ext.cmd_sink);
  gavl_msg_set_id_ns(cmd, GAVL_CMD_SRC_PAUSE, GAVL_MSG_NS_SRC);
  bg_msg_sink_put(h->ctrl_ext.cmd_sink, cmd);
  }

void bg_input_plugin_resume(bg_plugin_handle_t * h)
  {
  gavl_msg_t * cmd;

  if(!h->ctrl_ext.cmd_sink)
    return;

  cmd = bg_msg_sink_get(h->ctrl_ext.cmd_sink);
  gavl_msg_set_id_ns(cmd, GAVL_CMD_SRC_RESUME, GAVL_MSG_NS_SRC);
  bg_msg_sink_put(h->ctrl_ext.cmd_sink, cmd);
  }



int bg_plugin_handle_set_state(bg_plugin_handle_t * h, const char * ctx, const char * name, const gavl_value_t * val)
  {
  if(!h->control.cmd_sink)
    return 0;

  if(!ctx)
    ctx = h->info->name;
  
  bg_plugin_lock(h);
  bg_state_set(&h->state,
               1, ctx, name, val,
               h->ctrl_ext.cmd_sink, BG_CMD_SET_STATE);
  bg_plugin_unlock(h);
  
  return 1;
  }

int bg_plugin_handle_get_state(bg_plugin_handle_t * h, const char * ctx, const char * name, gavl_value_t * ret)
  {
  const gavl_value_t * src;

  if(!ctx)
    ctx = h->info->name;

  bg_plugin_lock(h);
  
  if(!h->control.cmd_sink ||
     !(src = bg_state_get(&h->state, ctx, name)))
    {
    bg_plugin_unlock(h);
    return 0;
    }
  gavl_value_copy(ret, src);
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

const char * bg_input_plugin_get_disk_name(bg_plugin_handle_t * h)
  {
  gavl_dictionary_t * mi;
  const gavl_dictionary_t * m;
  
  if(!(mi = bg_input_plugin_get_media_info(h)) ||
     !(m = gavl_dictionary_get_dictionary(mi, GAVL_META_METADATA)))
    return 0;
  
  return gavl_dictionary_get_string(m, GAVL_META_DISK_NAME);
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

    if(!gavl_dictionary_get_string(m, GAVL_META_MEDIA_CLASS))
      gavl_dictionary_set_string(m, GAVL_META_MEDIA_CLASS,
                                 GAVL_META_MEDIA_CLASS_MULTITRACK_FILE);
    
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

  free(location);
  
  return ret;
  }

static void load_location(bg_plugin_registry_t * reg,
                          const char * str, int flags,
                          gavl_array_t * ret)
  {
  const gavl_dictionary_t * edl;

  const gavl_array_t * tracks;
  gavl_dictionary_t * mi = bg_plugin_registry_load_media_info(reg, str, flags);

  if(!mi)
    return;

  /* Take EDL instead */
  if((edl = gavl_dictionary_get_dictionary(mi, GAVL_META_EDL)))
    tracks = gavl_get_tracks(edl);
  else
    tracks = gavl_get_tracks(mi);
  
  if(tracks &&
     (tracks->num_entries > 0))
    gavl_array_splice_array(ret, -1, 0, tracks);
  
  gavl_dictionary_destroy(mi);
  }


/* Value can be either a single string or a string array */
void bg_plugin_registry_tracks_from_locations(bg_plugin_registry_t * reg,
                                              const gavl_value_t * val,
                                              int flags,
                                              gavl_array_t * ret)
  {
  const char * uri;
  
  if(val->type == GAVL_TYPE_STRING)
    {
    uri = gavl_value_get_string(val);
#if 0
    if(uri_is_bgplug(uri))
      load_location_plug(uri, ret);
    else
#endif
      load_location(reg, uri, flags, ret);
    }
  else if(val->type == GAVL_TYPE_ARRAY)
    {
    int i;
    const gavl_array_t * arr = gavl_value_get_array(val);
    
    for(i = 0; i < arr->num_entries; i++)
      {
      if((uri = gavl_string_array_get(arr, i)))
        {
#if 0
        if(!i && (arr->num_entries == 1) &&
           uri_is_bgplug(uri))
          load_location_plug(uri, ret);
        else
#endif
          load_location(reg, uri, flags, ret);
        }
      }
    }
  }
  
void bg_plugin_registry_get_input_mimetypes(bg_plugin_registry_t * reg,
                                            gavl_array_t * ret)
  {
  int i, j;
  int num_plugins;
  const bg_plugin_info_t * info;
  
  num_plugins = bg_get_num_plugins(BG_PLUGIN_INPUT, BG_PLUGIN_URL);
  for(i = 0; i < num_plugins; i++)
    {
    info = bg_plugin_find_by_index(i, BG_PLUGIN_INPUT, BG_PLUGIN_URL);

    if(!info->mimetypes)
      continue;
    
    for(j = 0; j < info->mimetypes->num_entries; j++)
      gavl_string_array_add(ret, gavl_string_array_get(info->mimetypes, j));
    }
  }

void bg_plugin_registry_get_input_protocols(bg_plugin_registry_t * reg,
                                            gavl_array_t * ret)
  {
  int i, j;
  int num_plugins;
  const bg_plugin_info_t * info;
  
  num_plugins = bg_get_num_plugins(BG_PLUGIN_INPUT, BG_PLUGIN_URL);
  for(i = 0; i < num_plugins; i++)
    {
    info = bg_plugin_find_by_index(i, BG_PLUGIN_INPUT, BG_PLUGIN_URL);

    if(!info->protocols)
      continue;
    
    for(j = 0; j < info->protocols->num_entries; j++)
      gavl_string_array_add(ret, gavl_string_array_get(info->protocols, j));
    }
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
     gavl_dictionary_get_int(&cur_vars, BG_URL_VAR_TRACK, &cur_track) &&
     (cur_track >= 0) &&
     gavl_dictionary_get_int(&next_vars, BG_URL_VAR_TRACK, &next_track) &&
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
  bg_cfg_section_t * cfg_section;
  
  if(bg_plugin_reg)
    return;
  
  if(!bg_cfg_registry)
    bg_cfg_registry_init("generic");

  cfg_section = bg_cfg_registry_find_section(bg_cfg_registry, "plugins");
  
  bg_plugin_registry_create_1(cfg_section);
  }


void bg_plugins_cleanup()
  {
  if(bg_plugin_reg)
    {
    bg_plugin_registry_destroy_1(bg_plugin_reg);
    bg_plugin_reg = NULL;
    }
  }

void bg_plugin_registry_list_plugins(bg_plugin_type_t type, int flags)
  {
  int i, num;
  const bg_plugin_info_t * info;
  num = bg_get_num_plugins(type, flags);

  for(i = 0; i < num; i++)
    {
    info = bg_plugin_find_by_index(i, type, flags);
    printf("%s\n", info->name);
    }
  
  }

void bg_plugin_registry_list_input(void * data, int * argc,
                                           char *** _argv, int arg)
  {
  bg_plugin_registry_list_plugins(BG_PLUGIN_INPUT, BG_PLUGIN_FILE | BG_PLUGIN_URL |
                                  BG_PLUGIN_TUNER | BG_PLUGIN_REMOVABLE);
  }

void bg_plugin_registry_list_oa(void * data, int * argc,
                                        char *** _argv, int arg)
  {
  bg_plugin_registry_list_plugins(BG_PLUGIN_OUTPUT_AUDIO, BG_PLUGIN_PLAYBACK);
  }


void bg_plugin_registry_list_ov(void * data, int * argc,
                                        char *** _argv, int arg)
  {
  bg_plugin_registry_list_plugins(BG_PLUGIN_OUTPUT_VIDEO, BG_PLUGIN_PLAYBACK);

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

void bg_plugin_registry_list_ra(void * data, int * argc,
                                        char *** _argv, int arg)
  {
  bg_plugin_registry_list_plugins(BG_PLUGIN_RECORDER_AUDIO, 0);

  }

void bg_plugin_registry_list_rv(void * data, int * argc,
                                        char *** _argv, int arg)
  {
  bg_plugin_registry_list_plugins(BG_PLUGIN_RECORDER_VIDEO, 0);

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

  if(info->extensions)
    {
    tmp_string = gavl_string_array_join(info->extensions, " ");
    fprintf(stderr, "Extensions: %s\n", tmp_string);
    }

  if(info->protocols)
    {
    tmp_string = gavl_string_array_join(info->protocols, " ");
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

int bg_plugin_config_parse_multi(gavl_array_t * arr,
                                 const char * str)
  {
  char ** plugins;
  gavl_value_t val;
  gavl_dictionary_t * dict;
  int i = 0;
  int ret = 0;
  
  plugins = gavl_strbreak(str, '$');


  while(plugins[i])
    {
    gavl_value_init(&val);
    dict = gavl_value_set_dictionary(&val);

    if(!bg_plugin_config_parse_single(dict, plugins[i]))
      {
      gavl_value_free(&val);
      goto fail;
      }
    
    gavl_array_splice_val_nocopy(arr, -1, 0, &val);
    i++;
    }

  ret = 1;
  
  fail:
  
  
  gavl_strbreak_free(plugins);

  return ret;
  }

/* Load and store per type plugin configuration globally */

const gavl_value_t * bg_plugin_config_get(bg_plugin_type_t type)
  {
  const gavl_dictionary_t * s = bg_plugin_config_get_section(type);

  if(!s)
    return NULL;
  
  return gavl_dictionary_get(s, "$cfg");
  }

void bg_plugin_config_set(bg_plugin_type_t type, const gavl_value_t * val)
  {
  gavl_dictionary_t * s = bg_plugin_config_get_section_nc(type);
  gavl_dictionary_set(s, "$cfg", val);
  }

gavl_dictionary_t * bg_plugin_config_get_section_nc(bg_plugin_type_t type)
  {
  gavl_dictionary_t * ret;
  char * name;

  if(!bg_cfg_registry)
    return NULL;

  name = bg_sprintf("PluginConfig%s", bg_plugin_type_to_string(type));

  ret = bg_cfg_registry_find_section(bg_cfg_registry, name);
  free(name);
  return ret;
  }

const gavl_dictionary_t * bg_plugin_config_get_section(bg_plugin_type_t type)
  {
  const gavl_dictionary_t * ret;
  char * name;

  if(!bg_cfg_registry)
    return NULL;

  name = bg_sprintf("PluginConfig%s", bg_plugin_type_to_string(type));
  ret = bg_cfg_section_find_subsection_c(bg_cfg_registry, name);
  free(name);
  return ret;
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
  if(arg >= *argc)
    {
    fprintf(stderr, "Option -oa requires an argument\n");
    exit(-1);
    }
  parse_plugin_single((*_argv)[arg], BG_PLUGIN_OUTPUT_AUDIO);
  bg_cmdline_remove_arg(argc, _argv, arg);
  }

void bg_plugin_registry_opt_ov(void * data, int * argc,
                               char *** _argv, int arg)
  {
  if(arg >= *argc)
    {
    fprintf(stderr, "Option -ov requires an argument\n");
    exit(-1);
    }
  parse_plugin_single((*_argv)[arg], BG_PLUGIN_OUTPUT_VIDEO);
  bg_cmdline_remove_arg(argc, _argv, arg);
  }

void bg_plugin_registry_opt_ra(void * data, int * argc,
                               char *** _argv, int arg)
  {
  if(arg >= *argc)
    {
    fprintf(stderr, "Option -ra requires an argument\n");
    exit(-1);
    }
  parse_plugin_single((*_argv)[arg], BG_PLUGIN_RECORDER_AUDIO);
  bg_cmdline_remove_arg(argc, _argv, arg);
  }

void bg_plugin_registry_opt_rv(void * data, int * argc,
                               char *** _argv, int arg)
  {
  if(arg >= *argc)
    {
    fprintf(stderr, "Option -rv requires an argument\n");
    exit(-1);
    }
  parse_plugin_single((*_argv)[arg], BG_PLUGIN_RECORDER_VIDEO);
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

static void init_plugin_parameter(bg_parameter_info_t * ret, 
                                  bg_parameter_type_t type,
                                  const char * name,
                                  const char * long_name)
  {
  memset(ret, 0, 2*sizeof(*ret));
  
  ret->name = gavl_strdup(name);
  ret->long_name = gavl_strdup(long_name);
  ret->type = type;
  
  }

const bg_parameter_info_t * bg_plugin_registry_get_audio_compressor_parameter()
  {
  bg_parameter_info_t * ret = NULL;

  ret = &info_ca[0];

  init_plugin_parameter(ret, 
                        BG_PARAMETER_MULTI_MENU,
                        BG_PARAMETER_NAME_PLUGIN, TRS("Compression"));

  bg_plugin_registry_set_parameter_info(bg_plugin_reg,
                                        BG_PLUGIN_COMPRESSOR_AUDIO,
                                        0,
                                        ret);
  
  return ret;
  }

const bg_parameter_info_t * bg_plugin_registry_get_video_compressor_parameter()
  {
  bg_parameter_info_t * ret = NULL;
  ret = &info_cv[0];

  init_plugin_parameter(ret, 
                        BG_PARAMETER_MULTI_MENU,
                        BG_PARAMETER_NAME_PLUGIN, TRS("Compression"));

  bg_plugin_registry_set_parameter_info(bg_plugin_reg,
                                        BG_PLUGIN_COMPRESSOR_VIDEO,
                                        0,
                                        ret);

  
  return ret;
  }

const bg_parameter_info_t * bg_plugin_registry_get_overlay_compressor_parameter()
  {
  bg_parameter_info_t * ret = NULL;
  ret = &info_co[0];

  init_plugin_parameter(ret, 
                        BG_PARAMETER_MULTI_MENU,
                        BG_PARAMETER_NAME_PLUGIN, TRS("Compression"));

  bg_plugin_registry_set_parameter_info(bg_plugin_reg,
                                        BG_PLUGIN_COMPRESSOR_VIDEO,
                                        BG_PLUGIN_HANDLES_OVERLAYS,
                                        ret);
  
  return ret;
  }

const bg_parameter_info_t * bg_plugin_registry_get_plugin_parameter(bg_plugin_type_t type)
  {
  bg_parameter_info_t * ret = NULL;
  
  switch(type)
    {
    case BG_PLUGIN_OUTPUT_AUDIO:
      ret = &info_oa[0];

      if(!ret->name)
        {
        init_plugin_parameter(ret, 
                              BG_PARAMETER_MULTI_MENU,
                              BG_PARAMETER_NAME_PLUGIN, TRS("Plugin"));
        
        bg_plugin_registry_set_parameter_info(bg_plugin_reg,
                                              BG_PLUGIN_OUTPUT_AUDIO,
                                              BG_PLUGIN_PLAYBACK,
                                              ret);
        }
      break;
    case BG_PLUGIN_OUTPUT_VIDEO:
      ret = &info_ov[0];
      if(!ret->name)
        {
        init_plugin_parameter(ret, 
                              BG_PARAMETER_MULTI_MENU,
                              BG_PARAMETER_NAME_PLUGIN, TRS("Plugin"));

        bg_plugin_registry_set_parameter_info(bg_plugin_reg,
                                              BG_PLUGIN_OUTPUT_VIDEO,
                                              BG_PLUGIN_PLAYBACK,
                                              ret);
        }
      
      break;
    case BG_PLUGIN_INPUT:
      ret = &info_i[0];
      if(!ret->name)
        {
        init_plugin_parameter(ret, 
                              BG_PARAMETER_MULTI_MENU,
                              BG_PARAMETER_NAME_PLUGIN, TRS("Plugin"));
        
        }

      break;
    case BG_PLUGIN_VISUALIZATION:
      ret = &info_i[0];
      if(!ret->name)
        {
        init_plugin_parameter(ret, 
                              BG_PARAMETER_MULTI_MENU,
                              BG_PARAMETER_NAME_PLUGIN, TRS("Plugin"));

        bg_plugin_registry_set_parameter_info(bg_plugin_reg,
                                              BG_PLUGIN_VISUALIZATION,
                                              0, ret);
        }

      break;
    case BG_PLUGIN_FILTER_AUDIO:
      ret = &info_fa[0];
      if(!ret->name)
        {
        init_plugin_parameter(ret, 
                              BG_PARAMETER_MULTI_CHAIN,
                              BG_FILTER_CHAIN_PARAM_PLUGINS, TRS("Filters"));
        bg_plugin_registry_set_parameter_info(bg_plugin_reg,
                                              BG_PLUGIN_FILTER_AUDIO,
                                              BG_PLUGIN_FILTER_1,
                                              ret);
        }
      
      break;
    case BG_PLUGIN_FILTER_VIDEO:
      ret = &info_fv[0];
      if(!ret->name)
        {
        init_plugin_parameter(ret, 
                              BG_PARAMETER_MULTI_CHAIN,
                              BG_FILTER_CHAIN_PARAM_PLUGINS, TRS("Filters"));
        bg_plugin_registry_set_parameter_info(bg_plugin_reg,
                                              BG_PLUGIN_FILTER_VIDEO,
                                              BG_PLUGIN_FILTER_1,
                                              ret);
        }
      
      break;
    default:
      break;
    }
  return ret;
  }

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
  }

#if 0

  const char * url;
  const char * klass;
  gavl_dictionary_t * track_info;
  int track_index = 0;
  gavl_dictionary_t vars;
  int result = 0;
  gavl_dictionary_t * tm;
  int variant = 0;
  
  
  gavl_url_get_vars_c(location, &vars);

  if(gavl_dictionary_get_int(&vars, BG_URL_VAR_TRACK, &track_index))
    track_index--;
  
  gavl_dictionary_get_int(&vars, BG_URL_VAR_VARIANT, &variant);
  
  gavl_dictionary_free(&vars);
  
  if(!bg_input_plugin_load(reg,
                           location,
                           ret,
                           ctrl))
    goto end;
  
  track_info = bg_input_plugin_get_track_info(*ret, track_index);
  
  if(!track_info)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "No track %d in %s", track_index + 1, location);
    bg_plugin_unref(*ret);
    return 0;
    }
  
  tm = gavl_track_get_metadata_nc(track_info);

  /* Do redirection */

  const char * klass;
  if((klass = gavl_dictionary_get_string(tm, GAVL_META_MEDIA_CLASS)) &&
     !strcmp(klass, GAVL_META_MEDIA_CLASS_LOCATION))
    {
    const gavl_dictionary_t * src;

    src = gavl_metadata_get_src(tm, GAVL_META_SRC, 0, NULL, &url);

    if(url)
      {
      gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Got redirected to %s", url);
      *redirect_url = gavl_strdup(url);
      return 1;
      }
    }
  else
    result = 1;
  
  if(!result)
    goto end;
  
  /* Select track */
  bg_input_plugin_set_track(*ret, track_index);
  //  fprintf(stderr, "Select track:\n");
  //  gavl_dictionary_dump(track_info, 2);

  /* Check for external subtitles */
  if(gavl_track_get_num_video_streams(track_info) > 0)
    bg_track_find_subtitles(track_info);

  /* Check for external streams */
  if(gavl_track_get_num_external_streams(track_info))
    {
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, " %s", url);
    }
  
  end:

  if(!result)
    {
    if(*ret)
      {
      bg_plugin_unref(*ret);
      *ret = NULL;
      }
    }


#endif

bg_plugin_handle_t * bg_load_track(const gavl_dictionary_t * track)
  {
  int i;
  
  gavl_dictionary_t dict;

  const gavl_dictionary_t * src_track = track; // Track (or subtrack) containing the location
  int num_variants;
  const gavl_dictionary_t * edl = NULL;
  bg_plugin_handle_t * ret = NULL;

  int src_idx;
  int track_index = 0;
  gavl_dictionary_t vars;
  int variant;
  
  gavl_dictionary_init(&vars);
  gavl_dictionary_init(&dict);

  /* Multipart movie */
  if(get_multipart_edl(track, &dict))
    edl = &dict;
  
  variant = bg_track_get_variant(track);
  
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
    
    if((num_variants = gavl_track_get_num_variants(track)))
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
      track_index = 0;
      gavl_dictionary_init(&vars);
      gavl_url_get_vars_c(location, &vars);
      
      if(gavl_dictionary_get_int(&vars, BG_URL_VAR_TRACK, &track_index))
        track_index--;
      // gavl_dictionary_get_int(&vars, BG_URL_VAR_VARIANT, &variant);
      gavl_dictionary_free(&vars);
      
      if(!(ret = bg_input_plugin_load(location)))
        {
        src_idx++;
        continue;
        }
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
    
    if(!(klass = gavl_dictionary_get_string(tm, GAVL_META_MEDIA_CLASS)) ||
       strcmp(klass, GAVL_META_MEDIA_CLASS_LOCATION))
      {
      bg_input_plugin_set_track(ret, track_index);
      
      if(gavl_track_get_num_external_streams(ti))
        {
        ret = bg_input_plugin_load_multi(NULL, ret);
        bg_input_plugin_set_track(ret, 0);
        }
      break;
      }
    else // Redirector -> Prepare for next iteration
      {
      gavl_dictionary_copy(&dict, ti);
      track = &dict;
      src_track = &dict;
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

#define BG_TRACK_FORCE_RAW        "force_raw" // non-edl
#define BG_TRACK_CURRENT_LOCATION "location"
#define BG_TRACK_CURRENT_TRACK    "track"

/*
 *  Values in the track dictionary for configuring
 */
#define BG_TRACK_DICT_PLUGINREG   "$plugin_reg"

#define BG_TRACK_FORCE_RAW        "force_raw" // non-edl
#define BG_TRACK_CURRENT_LOCATION "location"
#define BG_TRACK_VARIANT          "variant"

void
bg_track_set_force_raw(gavl_dictionary_t * dict, int force_raw)
  {
  dict = gavl_dictionary_get_dictionary_create(dict, BG_TRACK_DICT_PLUGINREG);
  gavl_dictionary_set_int(dict, BG_TRACK_FORCE_RAW, force_raw);
  }

void
bg_track_set_variant(gavl_dictionary_t * dict, int variant)
  {
  dict = gavl_dictionary_get_dictionary_create(dict, BG_TRACK_DICT_PLUGINREG);
  gavl_dictionary_set_int(dict, BG_TRACK_VARIANT, variant);
  }

void
bg_track_set_current_location(gavl_dictionary_t * dict, const char * location)
  {
  dict = gavl_dictionary_get_dictionary_create(dict, BG_TRACK_DICT_PLUGINREG);
  gavl_dictionary_set_string(dict, BG_TRACK_CURRENT_LOCATION, location);
  }


int
bg_track_get_force_raw(const gavl_dictionary_t * dict)
  {
  int ret = 0;
  if(!(dict = gavl_dictionary_get_dictionary(dict, BG_TRACK_DICT_PLUGINREG)))
    return 0;
  gavl_dictionary_get_int(dict, BG_TRACK_FORCE_RAW, &ret);
  return ret;
  }

int
bg_track_get_variant(const gavl_dictionary_t * dict)
  {
  int ret = 0;
  if(!(dict = gavl_dictionary_get_dictionary(dict, BG_TRACK_DICT_PLUGINREG)))
    return 0;
  gavl_dictionary_get_int(dict, BG_TRACK_VARIANT, &ret);
  return ret;
  }

const char *
bg_track_get_current_location(const gavl_dictionary_t * dict)
  {
  if(!(dict = gavl_dictionary_get_dictionary(dict, BG_TRACK_DICT_PLUGINREG)))
    return NULL;
  
  return gavl_dictionary_get_string(dict, BG_TRACK_CURRENT_LOCATION);
  }



#if defined(__GNUC__)

static void cleanup_plugin_registry() __attribute__ ((destructor));

static void free_plugin_params(bg_parameter_info_t * info)
  {
  if(!info->name)
    return;
  bg_parameter_info_free(info);
  memset(info, 0, sizeof(*info));
  }

static void cleanup_plugin_registry()
  {
  bg_plugins_cleanup();
  
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

#endif


