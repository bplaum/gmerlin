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

#include <string.h>
#include <stdlib.h>

#include <config.h>


#include <gmerlin/utils.h>
#include <gmerlin/parameter.h>
#include <gmerlin/cfg_registry.h>

#include <gavl/log.h>
#define LOG_DOMAIN "parameter"

// static int bg_multi_menu_get_selected_idx(const gavl_value_t * val);

static char ** copy_string_array(char const * const * arr)
  {
  int i, num;
  char ** ret;
  if(!arr)
    return NULL;

  num = 0;
  while(arr[num])
    num++;

  ret = calloc(num+1, sizeof(*ret));

  for(i = 0; i < num; i++)
    ret[i] = gavl_strrep(ret[i], arr[i]);
  return ret;
  }

static void free_string_array(char ** arr)
  {
  int i = 0;
  if(!arr)
    return;

  while(arr[i])
    {
    free(arr[i]);
    i++;
    }
  free(arr);
  }

void bg_parameter_info_copy_pfx(bg_parameter_info_t * dst,
                                const bg_parameter_info_t * src, const char * pfx)
  {
  int num_options, i;

  if(pfx)
    dst->name = bg_sprintf("%s.%s", pfx, src->name);
  else
    dst->name = gavl_strrep(dst->name, src->name);
  
  dst->long_name = gavl_strrep(dst->long_name, src->long_name);
  dst->help_string = gavl_strrep(dst->help_string, src->help_string);
  dst->type = src->type;
  dst->flags = src->flags;

  dst->gettext_domain    = gavl_strrep(dst->gettext_domain,    src->gettext_domain);
  dst->gettext_directory = gavl_strrep(dst->gettext_directory, src->gettext_directory);
  dst->preset_path       = gavl_strrep(dst->preset_path,       src->preset_path);

  gavl_value_copy(&dst->val_default, &src->val_default);
  
  switch(dst->type)
    {
    case BG_PARAMETER_CHECKBUTTON:
      break;
    case BG_PARAMETER_INT:
    case BG_PARAMETER_SLIDER_INT:
      gavl_value_copy(&dst->val_min, &src->val_min);
      gavl_value_copy(&dst->val_max, &src->val_max);
      break;
    case BG_PARAMETER_FLOAT:
    case BG_PARAMETER_SLIDER_FLOAT:
      gavl_value_copy(&dst->val_min, &src->val_min);
      gavl_value_copy(&dst->val_max, &src->val_max);
      dst->num_digits        = src->num_digits;
      break;
    case BG_PARAMETER_STRING:
    case BG_PARAMETER_STRING_HIDDEN:
    case BG_PARAMETER_FONT:
    case BG_PARAMETER_FILE:
    case BG_PARAMETER_DIRECTORY:
    case BG_PARAMETER_DIRLIST:
      break;
    case BG_PARAMETER_MULTI_MENU:
    case BG_PARAMETER_MULTI_LIST:
    case BG_PARAMETER_MULTI_CHAIN:
      dst->multi_names_nc        = copy_string_array(src->multi_names);
      dst->multi_labels_nc       = copy_string_array(src->multi_labels);
      dst->multi_descriptions_nc = copy_string_array(src->multi_descriptions);
      
      i = 0;

      if(src->multi_names)
        {
        num_options = 0;
        
        while(src->multi_names[num_options])
          num_options++;

        if(src->multi_parameters)
          {
          dst->multi_parameters_nc =
            calloc(num_options, sizeof(*(src->multi_parameters_nc)));
          i = 0;
          
          while(src->multi_names[i])
            {
            if(src->multi_parameters[i])
              dst->multi_parameters_nc[i] =
                bg_parameter_info_copy_array_pfx(src->multi_parameters[i], pfx);
            i++;
            }
          }
        }
      break;
    case BG_PARAMETER_STRINGLIST:
      /* Copy stringlist options */
      
      if(src->multi_names)
        dst->multi_names_nc = copy_string_array(src->multi_names);
      if(src->multi_labels)
        dst->multi_labels_nc = copy_string_array(src->multi_labels);
      dst->multi_names = (char const **)dst->multi_names_nc;
      dst->multi_labels = (char const **)dst->multi_labels_nc;
      break;
    case BG_PARAMETER_COLOR_RGB:
      break;
    case BG_PARAMETER_COLOR_RGBA:
      break;
    case BG_PARAMETER_POSITION:
      dst->num_digits        = src->num_digits;
      break;
    case BG_PARAMETER_TIME:
      break;
    case BG_PARAMETER_SECTION:
    case BG_PARAMETER_BUTTON:
      break;
    }
  bg_parameter_info_set_const_ptrs(dst);
  }

void bg_parameter_info_copy(bg_parameter_info_t * dst,
                            const bg_parameter_info_t * src)
  {
  bg_parameter_info_copy_pfx(dst, src, NULL);
  }

bg_parameter_info_t *
bg_parameter_info_copy_array_pfx(const bg_parameter_info_t * src, const char * pfx)
  {
  int num_parameters, i;
  bg_parameter_info_t * ret;

  num_parameters = 0;
  
  while(src[num_parameters].name)
    num_parameters++;
      
  ret = calloc(num_parameters + 1, sizeof(bg_parameter_info_t));
  
  for(i = 0; i < num_parameters; i++)
    bg_parameter_info_copy_pfx(&ret[i], &src[i], pfx);
  
  return ret;
  }

bg_parameter_info_t *
bg_parameter_info_copy_array(const bg_parameter_info_t * src)
  {
  return bg_parameter_info_copy_array_pfx(src, NULL);
  }

void bg_parameter_info_free(bg_parameter_info_t * info)
  {
  int i;
  
  free(info->name);
  if(info->long_name)
    free(info->long_name);
  if(info->help_string)
    free(info->help_string);
  if(info->gettext_domain)
    free(info->gettext_domain);
  if(info->gettext_directory)
    free(info->gettext_directory);
  if(info->preset_path)
    free(info->preset_path);

  gavl_value_free(&info->val_min);
  gavl_value_free(&info->val_max);
  gavl_value_free(&info->val_default);
    
  switch(info->type)
    {
    case BG_PARAMETER_STRINGLIST:
      free_string_array(info->multi_names_nc);
      free_string_array(info->multi_labels_nc);
        
      break;
    case BG_PARAMETER_STRING:
    case BG_PARAMETER_STRING_HIDDEN:
    case BG_PARAMETER_FONT:
    case BG_PARAMETER_FILE:
    case BG_PARAMETER_DIRECTORY:
    case BG_PARAMETER_DIRLIST:
      break;
    case BG_PARAMETER_SECTION:
    case BG_PARAMETER_BUTTON:
    case BG_PARAMETER_CHECKBUTTON:
    case BG_PARAMETER_INT:
    case BG_PARAMETER_FLOAT:
    case BG_PARAMETER_TIME:
    case BG_PARAMETER_SLIDER_INT:
    case BG_PARAMETER_SLIDER_FLOAT:
    case BG_PARAMETER_COLOR_RGB:
    case BG_PARAMETER_COLOR_RGBA:
    case BG_PARAMETER_POSITION:
      break;
    case BG_PARAMETER_MULTI_MENU:
    case BG_PARAMETER_MULTI_LIST:
    case BG_PARAMETER_MULTI_CHAIN:
      i = 0;

      if(info->multi_parameters)
        {
        while(info->multi_names[i])
          {
          if(info->multi_parameters[i])
            bg_parameter_info_destroy_array(info->multi_parameters_nc[i]);
          i++;
          }
        free(info->multi_parameters_nc);
        }
        
      free_string_array(info->multi_names_nc);
      free_string_array(info->multi_labels_nc);
      free_string_array(info->multi_descriptions_nc);
        
    }
  }

void bg_parameter_info_destroy_array(bg_parameter_info_t * info)
  {
  int index = 0;
  while(info[index].name)
    {
    bg_parameter_info_free(&info[index]);
    index++;
    }
  free(info);
  }

bg_parameter_info_t *
bg_parameter_info_concat_arrays(bg_parameter_info_t const ** srcs)
  {
  int i, j, dst, num_parameters;

  bg_parameter_info_t * ret;

  /* Count the parameters */
  num_parameters = 0;
  i = 0;

  while(srcs[i])
    {
    j = 0;
    while(srcs[i][j].name)
      {
      num_parameters++;
      j++;
      }
    i++;
    }

  /* Allocate destination */

  ret = calloc(num_parameters+1, sizeof(*ret));

  /* Copy stuff */
  
  i = 0;
  dst = 0;

  while(srcs[i])
    {
    j = 0;
    while(srcs[i][j].name)
      {
      bg_parameter_info_copy(&ret[dst], &srcs[i][j]);
      dst++;
      j++;
      }
    i++;
    }
  return ret;
  }

int bg_parameter_get_selected(const bg_parameter_info_t * info,
                              const gavl_value_t * val)
  {
  int ret = -1, i;

  const char * str;

  if(!val)
    return 0;
  else if(val->type == GAVL_TYPE_STRING)
    str = val->v.str;
  else if(val->type == GAVL_TYPE_DICTIONARY)
    str = gavl_dictionary_get_string(val->v.dictionary, BG_CFG_TAG_NAME);
  else
    return 0;
  
  i = 0;
  while(info->multi_names[i])
    {
    if(!strcmp(str, info->multi_names[i]))
      {
      ret = i;
      break;
      }
    i++;
    }
  
  if((ret < 0) && info->val_default.v.str)
    {
    i = 0;
    /* Try default value */
    while(info->multi_names[i])
      {
      if(!strcmp(info->val_default.v.str, info->multi_names[i]))
        {
        ret = i;
        break;
        }
      i++;
      }
    }
  
  if(ret < 0)
    return 0;
  else
    return ret;
  }

const bg_parameter_info_t *
bg_parameter_find(const bg_parameter_info_t * info,
                  const char * name)
  {
  int i, j;
  const bg_parameter_info_t * child_ret;
  i = 0;
  while(info[i].name)
    {
    if(!strcmp(name, info[i].name))
      return &info[i];

    if(info[i].multi_parameters && info[i].multi_names)
      {
      j = 0;
      while(info[i].multi_names[j])
        {
        if(info[i].multi_parameters[j])
          {
          child_ret = bg_parameter_find(info[i].multi_parameters[j], name);
          if(child_ret)
            return child_ret;
          }
        j++;
        }
      }
    i++;
    }
  return NULL;
  }

void bg_parameter_info_set_const_ptrs(bg_parameter_info_t * ret)
  {
  ret->multi_names = (char const **)ret->multi_names_nc;
  ret->multi_labels = (char const **)ret->multi_labels_nc;
  ret->multi_descriptions = (char const **)ret->multi_descriptions_nc;
  ret->multi_parameters =
    (bg_parameter_info_t const * const *)ret->multi_parameters_nc;
  }

gavl_type_t bg_parameter_type_to_gavl(bg_parameter_type_t type)
  {
  switch(type)
    {
    case BG_PARAMETER_SECTION:       //!< Dummy type. It contains no data but acts as a separator in notebook style configuration windows
      return GAVL_TYPE_UNDEFINED;
      break;
    case BG_PARAMETER_CHECKBUTTON:   //!< Bool
    case BG_PARAMETER_INT:           //!< Integer spinbutton
    case BG_PARAMETER_SLIDER_INT:    //!< Integer slider
      return GAVL_TYPE_INT;
      break;
    case BG_PARAMETER_FLOAT:         //!< Float spinbutton
    case BG_PARAMETER_SLIDER_FLOAT:  //!< Float slider
      return GAVL_TYPE_FLOAT;
      break;
    case BG_PARAMETER_STRING:        //!< String (one line only)
    case BG_PARAMETER_STRING_HIDDEN: //!< Encrypted string (displays as ***)
    case BG_PARAMETER_STRINGLIST:    //!< Popdown menu with string values
    case BG_PARAMETER_FONT:          //!< Font (contains fontconfig compatible fontname)
    case BG_PARAMETER_FILE:          //!< File
    case BG_PARAMETER_DIRECTORY:     //!< Directory
      return GAVL_TYPE_STRING;
      break;
    case BG_PARAMETER_MULTI_MENU:    //!< Menu with config- and infobutton
      return GAVL_TYPE_DICTIONARY;
      break;
    case BG_PARAMETER_DIRLIST:
    case BG_PARAMETER_MULTI_LIST:    //!< List with config- and infobutton
    case BG_PARAMETER_MULTI_CHAIN:   //!< Several subitems (including suboptions) can be arranged in a chain
      return GAVL_TYPE_ARRAY;
      break;
    case BG_PARAMETER_COLOR_RGB:     //!< RGB Color
      return GAVL_TYPE_COLOR_RGB;
      break;
    case BG_PARAMETER_COLOR_RGBA:    //!< RGBA Color
      return GAVL_TYPE_COLOR_RGBA;
      break;
    case BG_PARAMETER_TIME:          //!< Time
      return GAVL_TYPE_LONG;
      break;
    case BG_PARAMETER_POSITION:      //!< Position (x/y coordinates, scaled 0..1)
      return GAVL_TYPE_POSITION;
      break;
    case BG_PARAMETER_BUTTON:        //!< Pressing the button causes set_parameter to be called with NULL value
      return GAVL_TYPE_STRING;
      break;
    }
  return GAVL_TYPE_UNDEFINED;
  }

void bg_parameter_info_append_option(bg_parameter_info_t * dst,
                                     const char * opt, const char * label)
  {
  int i;
  
  int num = 0;

  if(dst->multi_names)
    {
    while(dst->multi_names[num])
      num++;
    }

  if(num && !dst->multi_names_nc)
    {
    dst->multi_names_nc = malloc((num + 2)*sizeof(*dst->multi_names_nc));
    dst->multi_labels_nc = malloc((num + 2)*sizeof(*dst->multi_labels_nc));

    for(i = 0; i < num; i++)
      {
      dst->multi_names_nc[i] = gavl_strdup(dst->multi_names[i]);
      dst->multi_labels_nc[i] = gavl_strdup(dst->multi_labels[i]);
      }
    }
  else
    {
    dst->multi_names_nc = realloc(dst->multi_names_nc, (num + 2)*sizeof(*dst->multi_names_nc));
    dst->multi_labels_nc = realloc(dst->multi_labels_nc, (num + 2)*sizeof(*dst->multi_labels_nc));
    }

  dst->multi_names_nc[num]  = gavl_strdup(opt);
  dst->multi_labels_nc[num] = gavl_strdup(label);

  dst->multi_names_nc[num+1]  = '\0';
  dst->multi_labels_nc[num+1] = '\0';
  
  bg_parameter_info_set_const_ptrs(dst);
  }
  
const gavl_dictionary_t * bg_multi_menu_get_selected(const gavl_value_t * val)
  {
  int idx;
  const gavl_array_t * arr;
  
  if(((idx = bg_multi_menu_get_selected_idx(val)) < 0) ||
     !(arr = gavl_dictionary_get_array(val->v.dictionary, BG_CFG_TAG_CHILDREN)))
    {
    if((val->type == GAVL_TYPE_DICTIONARY) &&
       (gavl_dictionary_get_string(val->v.dictionary, BG_CFG_TAG_NAME)))
      return  val->v.dictionary;
    else
      return NULL;
    }

  if(!arr->num_entries)
    return NULL;
  
  return gavl_value_get_dictionary(&arr->entries[idx]);
  }

gavl_dictionary_t * bg_multi_menu_get_selected_nc(gavl_value_t * val)
  {
  int idx;
  gavl_array_t * arr;
  
  if(((idx = bg_multi_menu_get_selected_idx(val)) < 0) ||
     !(arr = gavl_dictionary_get_array_nc(val->v.dictionary, BG_CFG_TAG_CHILDREN)))
    {
    if((val->type == GAVL_TYPE_DICTIONARY) &&
       (gavl_dictionary_get_string(val->v.dictionary, BG_CFG_TAG_NAME)))
      return  val->v.dictionary;
    else
      return NULL;
    }
  return gavl_value_get_dictionary_nc(&arr->entries[idx]);
  }

const char * bg_multi_menu_get_selected_name(const gavl_value_t * val)
  {
  const gavl_dictionary_t * dict;

  if(!(dict = bg_multi_menu_get_selected(val)))
    {
    const char * name;
    
    if((dict = gavl_value_get_dictionary(val)) &&
       (name = gavl_dictionary_get_string(dict, BG_CFG_TAG_NAME)))
      return name;
    else
      return NULL;
    }
  return gavl_dictionary_get_string(dict, BG_CFG_TAG_NAME);
  }

void bg_multi_menu_set_selected_idx(gavl_value_t * val, int idx)
  {
  gavl_dictionary_t * dict;

  if(!(dict = gavl_value_get_dictionary_nc(val)))
    return;
  
  gavl_dictionary_set_int(dict, BG_CFG_TAG_IDX, idx);
  }


int bg_multi_menu_get_selected_idx(const gavl_value_t * val)
  {
  int ret = -1;
  const gavl_dictionary_t * dict;

  if(!(dict = gavl_value_get_dictionary(val)))
    return ret;
  
  gavl_dictionary_get_int(dict, BG_CFG_TAG_IDX, &ret);
  return ret;
  }

int bg_multi_menu_get_num(const gavl_value_t * val)
  {
  const gavl_array_t      * arr;
  const gavl_dictionary_t * dict;

  if(!(dict = gavl_value_get_dictionary(val)) ||
     !(arr = gavl_dictionary_get_array(dict, BG_CFG_TAG_CHILDREN)))
    {
    return 0;
    }
  
  return arr->num_entries;
  }

void bg_multi_menu_set_selected_name(gavl_value_t * val, const char * name)
  {
  int i;
  gavl_array_t      * arr;
  gavl_dictionary_t * dict;

  if(!(dict = gavl_value_get_dictionary_nc(val)) ||
     !(arr = gavl_dictionary_get_array_nc(dict, BG_CFG_TAG_CHILDREN)))
    {
    return;
    }

  for(i = 0; i < arr->num_entries; i++)
    {
    const gavl_dictionary_t * child;
    const char * child_name;
    
    if((child = gavl_value_get_dictionary(&arr->entries[i])) &&
       (child_name = gavl_dictionary_get_string(child, BG_CFG_TAG_NAME)) &&
       !strcmp(name, child_name))
      {
      bg_multi_menu_set_selected_idx(val, i);
      break;
      }
    }
  }

void bg_multi_menu_set_selected(gavl_value_t * val, const gavl_dictionary_t * src)
  {
  int i;
  
  gavl_array_t      * arr;
  const char * src_name;
  gavl_dictionary_t * dict;
  gavl_dictionary_t * child;
  const char * child_name;
  
  if(!(src_name = gavl_dictionary_get_string(src, BG_CFG_TAG_NAME)) ||
     !(dict = gavl_value_get_dictionary_nc(val)) ||
     !(arr = gavl_dictionary_get_array_nc(dict, BG_CFG_TAG_CHILDREN)))
    {
    return;
    }

  for(i = 0; i < arr->num_entries; i++)
    {
    if((child = gavl_value_get_dictionary_nc(&arr->entries[i])) &&
       (child_name = gavl_dictionary_get_string(child, BG_CFG_TAG_NAME)) &&
       !strcmp(src_name, child_name))
      {
      gavl_dictionary_free(child);
      gavl_dictionary_init(child);
      gavl_dictionary_copy(child, src);

      //      fprintf(stderr, "bg_multi_menu_set_selected: %d\n", i);
      
      bg_multi_menu_set_selected_idx(val, i);
      break;
      }
    }
  }

void bg_multi_menu_create(gavl_value_t * val,
                          const bg_parameter_info_t * info)
  {
  int i;
  gavl_value_t val_arr;

  gavl_dictionary_t * dict;
  gavl_array_t * arr;
  
  dict = gavl_value_set_dictionary(val);
  
  gavl_value_init(&val_arr);

  gavl_dictionary_set_int(dict, BG_CFG_TAG_IDX, 0);

  arr = gavl_value_set_array(&val_arr);

  i = 0;

  if(info->multi_names)
    {
    while(info->multi_names[i])
      {
      gavl_value_t val_child;
      gavl_dictionary_t * child;

      gavl_value_init(&val_child);
      child = gavl_value_set_dictionary(&val_child);
      gavl_dictionary_set_string(child, BG_CFG_TAG_NAME, info->multi_names[i]);
      
      if(info->multi_parameters && info->multi_parameters[i])
        bg_cfg_section_create_items(child, info->multi_parameters[i]);

      gavl_array_splice_val_nocopy(arr, -1, 0, &val_child);
      i++;
      }
    }
  gavl_dictionary_set_nocopy(dict, BG_CFG_TAG_CHILDREN, &val_arr);
  }

void bg_multi_menu_update(gavl_value_t * val,
                          const bg_parameter_info_t * info)
  {
  int i = 0;
  int j;
  int found;
  gavl_array_t * arr;
  gavl_dictionary_t * dict;
  const char * name;
  gavl_dictionary_t * child;
  
  dict = gavl_value_get_dictionary_nc(val);
  arr = gavl_dictionary_get_array_nc(dict, BG_CFG_TAG_CHILDREN);

#if 0  
  if(!strcmp(info->name, "plugin"))
    {
    fprintf(stderr, "bg_multi_menu_update %s:\n", info->name);
    gavl_dictionary_dump(dict, 2);
    fprintf(stderr, "\n");
    gavl_array_dump(arr, 2);
    }
#endif
  
  /* Check if entries were added */

  if(info->multi_names)
    {
    while(info->multi_names[i])
      {
      found = 0;

      for(j = 0; j < arr->num_entries; j++)
        {
        if((child = gavl_value_get_dictionary_nc(&arr->entries[j])) &&
           (name = gavl_dictionary_get_string(child, BG_CFG_TAG_NAME)) &&
           !strcmp(name, info->multi_names[i]))
          {
          found = 1;
          break;
          }
        }
    
      if(!found)
        {
        gavl_value_t val_child;

        gavl_value_init(&val_child);
        child = gavl_value_set_dictionary(&val_child);
        gavl_dictionary_set_string(child, BG_CFG_TAG_NAME, info->multi_names[i]);

        if(info->multi_parameters && info->multi_parameters[i])
          bg_cfg_section_create_items(child, info->multi_parameters[i]);
      
        gavl_array_splice_val_nocopy(arr, -1, 0, &val_child);
        }
    
      i++;
      }

    }
  

  /* Check if entries were removed */

  j = 0;
  
  while(j < arr->num_entries)
    {
    found = 0;
    
    if((child = gavl_value_get_dictionary_nc(&arr->entries[j])) &&
       (name = gavl_dictionary_get_string(child, BG_CFG_TAG_NAME)))
      {
      i = 0;

      if(info->multi_names)
        {
        while(info->multi_names[i])
          {
          if(!strcmp(info->multi_names[i], name))
            {
            found = 1;
            break;
            }
          i++;
          }
        }
      }
    
    if(!found)
      gavl_array_splice_val(arr, j, 1, NULL);
    else
      j++;
    }
  
  }

/* Parse parameters in the form: param1=val1&param2=val2 */

int bg_parameter_parse_string(const char * str, gavl_dictionary_t * dict, const bg_parameter_info_t * params)
  {
  int i;
  int ret = 0;
  char ** args;
  char * pos;
  const bg_parameter_info_t * info;
  gavl_value_t val;
  
  args = gavl_strbreak(str, '&');

  i = 0;

  while(args[i])
    {
    if(!(pos = gavl_find_char(args[i], '=')))
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Invalid parameter %s (expected name=value)", args[i]);
      goto fail;
      }

    *pos = '\0';

    pos++;

    if(!(info = bg_parameter_find(params, args[i])))
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "No such parameter: %s", args[i]);
      goto fail;
      }

    gavl_value_init(&val);
    val.type = bg_parameter_type_to_gavl(info->type);

    gavl_value_from_string(&val, pos);
    gavl_dictionary_set_nocopy(dict, args[i], &val);
    
    i++;
    }

  ret = 1;
  fail:

  gavl_strbreak_free(args);
  return ret;
  }
