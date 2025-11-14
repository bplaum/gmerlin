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
#include <stdlib.h>

#include <config.h>


#include <gmerlin/utils.h>
#include <gmerlin/parameter.h>
#include <gmerlin/cfg_registry.h>

#include <gavl/log.h>
#define LOG_DOMAIN "parameter"

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

void bg_parameter_info_copy(bg_parameter_info_t * dst,
                                const bg_parameter_info_t * src)
  {
  int num_options, i;

  dst->name = gavl_strrep(dst->name, src->name);
  
  dst->long_name = gavl_strrep(dst->long_name, src->long_name);
  dst->help_string = gavl_strrep(dst->help_string, src->help_string);
  dst->type = src->type;
  dst->flags = src->flags;

  dst->gettext_domain    = gavl_strrep(dst->gettext_domain,    src->gettext_domain);
  dst->gettext_directory = gavl_strrep(dst->gettext_directory, src->gettext_directory);

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
                bg_parameter_info_copy_array(src->multi_parameters[i]);
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


bg_parameter_info_t *
bg_parameter_info_copy_array(const bg_parameter_info_t * src)
  {
  int num_parameters, i;
  bg_parameter_info_t * ret;

  num_parameters = 0;
  
  while(src[num_parameters].name)
    num_parameters++;
      
  ret = calloc(num_parameters + 1, sizeof(bg_parameter_info_t));
  
  for(i = 0; i < num_parameters; i++)
    bg_parameter_info_copy(&ret[i], &src[i]);
  
  return ret;
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
    val.type = gavl_parameter_type_to_gavl(info->type);

    gavl_value_from_string(&val, pos);
    gavl_dictionary_set_nocopy(dict, args[i], &val);
    
    i++;
    }

  ret = 1;
  fail:

  gavl_strbreak_free(args);
  return ret;
  }

typedef struct
  {
  gavl_dictionary_t * dst;
  const bg_parameter_info_t * info;
  } from_strings_t;

static void from_strings_foreach_func(void * priv, const char * name,
                                      const gavl_value_t * val)
  {
  gavl_value_t val1;
  const bg_parameter_info_t * info;
  from_strings_t * f = priv;

  if(!(info = bg_parameter_find(f->info, name)))
    return;     /* Ignore */

  gavl_value_init(&val1);
  val1.type = gavl_parameter_type_to_gavl(info->type);
  gavl_value_from_string(&val1, gavl_value_get_string(val));
  gavl_dictionary_set_nocopy(f->dst, name, &val1);
  }


void bg_cfg_section_from_strings(const gavl_dictionary_t * src, gavl_dictionary_t * dst,
                                 const bg_parameter_info_t * info)
  {
  from_strings_t f;

  f.info = info;
  f.dst = dst;
  gavl_dictionary_foreach(src, from_strings_foreach_func, &f);
  }
