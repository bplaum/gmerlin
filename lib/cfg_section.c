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



#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <config.h>
#include <gmerlin/translation.h>


#include <gmerlin/cfg_registry.h>
#include <registry_priv.h>
#include <gmerlin/utils.h>

static void merge_func_item(void * priv, const char * name, const gavl_value_t * val)
  {
  gavl_dictionary_t * dst = priv;
  gavl_value_t * val_dst;

  if(*name == '$')
    return;
  
  if((val_dst = gavl_dictionary_get_nc(dst, name)))
    gavl_value_copy(val_dst, val);
  }

static void merge_func_child(void * priv, const char * name, const gavl_value_t * val)
  {
  const gavl_dictionary_t * src;
  gavl_dictionary_t * dst;

  if(*name == '$')
    return;
  
  if((src = gavl_value_get_dictionary(val)) &&
     (dst = gavl_dictionary_get_dictionary_nc(priv, name)))
    bg_cfg_section_merge(dst, src);
  }

void bg_cfg_section_merge(gavl_dictionary_t * dst,
                          const gavl_dictionary_t * src)
  {
  const gavl_dictionary_t * src_children;
  gavl_dictionary_t * dst_children;
  
  gavl_dictionary_foreach(src, merge_func_item, dst);

  if((src_children = gavl_dictionary_get_dictionary(src, BG_CFG_TAG_CHILDREN)) &&
     (dst_children = gavl_dictionary_get_dictionary_nc(dst, BG_CFG_TAG_CHILDREN)))
    gavl_dictionary_foreach(src_children, merge_func_child, dst_children);
  }

gavl_dictionary_t * bg_cfg_section_create(const char * name)
  {
  gavl_dictionary_t * ret = calloc(1, sizeof(*ret));
  gavl_dictionary_set_string(ret, BG_CFG_TAG_NAME, name);
  return ret;
  }

gavl_dictionary_t * bg_cfg_section_find_subsection(gavl_dictionary_t * s,
                                                   const char * name)
  {
  s = gavl_dictionary_get_dictionary_create(s, BG_CFG_TAG_CHILDREN);
  s = gavl_dictionary_get_dictionary_create(s, name);
  gavl_dictionary_set_string(s, BG_CFG_TAG_NAME, name);
  return s;
  }

void bg_cfg_section_delete_subsection_by_name(gavl_dictionary_t * s,
                                              const char * name)
  {
  if((s = gavl_dictionary_get_dictionary_nc(s, BG_CFG_TAG_CHILDREN)))
    gavl_dictionary_set(s, name, NULL);
  return;
  }

const gavl_dictionary_t * bg_cfg_section_find_subsection_c(const gavl_dictionary_t * s,
                                                          const char * name)
  {
  if((s = gavl_dictionary_get_dictionary(s, BG_CFG_TAG_CHILDREN)) &&
     (s = gavl_dictionary_get_dictionary(s, name)))
    return s;
  return NULL;
  }

int bg_cfg_section_has_subsection(const gavl_dictionary_t * s,
                                  const char * name)
  {
  if((s = gavl_dictionary_get_dictionary(s, BG_CFG_TAG_CHILDREN)) &&
     (s = gavl_dictionary_get_dictionary(s, name)))
    return 1;
  return 0;
  }

gavl_value_t * bg_cfg_section_find_item(gavl_dictionary_t * section,
                                         const bg_parameter_info_t * info)
  {
  gavl_value_t * ret;
  if(!(ret = gavl_dictionary_get_nc(section, info->name)))
    {
    gavl_dictionary_t param;
    
    gavl_dictionary_set(section, info->name, &info->val_default);
    ret = gavl_dictionary_get_nc(section, info->name);

    gavl_dictionary_init(&param);
    gavl_parameter_init(&param, info->name, info->long_name, info->type);
    bg_parameter_init_value(&param, ret);
    
    if(ret->type == GAVL_TYPE_UNDEFINED)
      gavl_value_set_type(ret, gavl_parameter_type_to_gavl(info->type));
    }
  
  return ret;
  }

static gavl_value_t * find_item_nocreate(gavl_dictionary_t * section,
                                          const bg_parameter_info_t * info)
  {
  return gavl_dictionary_get_nc(section, info->name);
  }

static const gavl_value_t * find_item_c(const gavl_dictionary_t * section,
                                         const bg_parameter_info_t * info)
  {
  return gavl_dictionary_get(section, info->name);
  }

/*
 *  Get/Set values
 */

void bg_cfg_section_set_parameter(gavl_dictionary_t * section,
                                  const bg_parameter_info_t * info,
                                  const gavl_value_t * value)
  {
  gavl_value_t * item;
  if(!value)
    return;
  item = bg_cfg_section_find_item(section, info);
  if(!item)
    return;
  
  gavl_value_copy(item, value);
  }



void bg_cfg_section_get_parameter(gavl_dictionary_t * section,
                                  const bg_parameter_info_t * info,
                                  gavl_value_t * value)
  {
  gavl_value_t * item;
  item = bg_cfg_section_find_item(section, info);

  if(!value || !item)
    return;

  gavl_value_copy(value, item);
  }

const gavl_value_t * bg_cfg_section_get_parameter_c(const gavl_dictionary_t * section,
                                                            const char * name)
  {
  return gavl_dictionary_get(section, name);
  }

void bg_cfg_section_destroy(gavl_dictionary_t * s)
  {
  gavl_dictionary_free(s);
  free(s);
  }

typedef struct
  {
  bg_set_parameter_func_t func;
  void * callback_data;
  
  }
apply_t;

static void apply_func(void * priv, const char * name,
                       const gavl_value_t * val)
  {
  apply_t * a;
  if(name && (*name == '$'))
    return;

  a = priv;
  a->func(a->callback_data, name, val);
  }

static void do_apply(const gavl_dictionary_t * section,
                     const bg_parameter_info_t * infos,
                     bg_set_parameter_func_t func,
                     void * callback_data, int terminate)
  {
  int num;
  const gavl_value_t * item;
  gavl_type_t type;
  num = 0;

  if(infos)
    {
    while(infos[num].name)
      {
      if(infos[num].type == BG_PARAMETER_BUTTON)
        {
        num++;
        continue;
        }
      
      item = find_item_c(section,&infos[num]);
    
      if(!item)
        {
        num++;

        if(infos[num].val_default.type)
          func(callback_data, infos[num].name, &infos[num].val_default);
      
        continue;
        }

      type = gavl_parameter_type_to_gavl(infos[num].type);

      if((item->type == GAVL_TYPE_STRING) &&
         (type != GAVL_TYPE_STRING))
        {
        gavl_value_t val;
        gavl_value_init(&val);

        val.type = type;
        gavl_value_from_string(&val, gavl_value_get_string(item));
        func(callback_data, infos[num].name, &val);
        gavl_value_free(&val);
        }
      else
        func(callback_data, infos[num].name, item);
      num++;
      }
    }
  else
    {
    apply_t a;
    a.func = func;
    a.callback_data = callback_data;
    gavl_dictionary_foreach(section, apply_func, &a);
    }
  if(terminate)
    func(callback_data, NULL, NULL);
  }

void bg_cfg_section_apply(const gavl_dictionary_t * section,
                          const bg_parameter_info_t * infos,
                          bg_set_parameter_func_t func,
                          void * callback_data)
  {
  do_apply(section, infos, func, callback_data, 1);
  }

void bg_cfg_section_apply_noterminate(gavl_dictionary_t * section,
                                      const bg_parameter_info_t * infos,
                                      bg_set_parameter_func_t func,
                                      void * callback_data)
  {
  do_apply(section, infos, func, callback_data, 0);
  }

/* Get parameter values, return 0 if no such entry */

int bg_cfg_section_get_parameter_int(const gavl_dictionary_t * section,
                                     const char * name, int * value)
  {
  return gavl_dictionary_get_int(section, name, value);
  }

int bg_cfg_section_get_parameter_float(const gavl_dictionary_t * section,
                                       const char * name, float * value)
  {
  double val_dbl = 0.0;
  if(gavl_dictionary_get_float(section, name, &val_dbl))
    {
    *value = val_dbl;
    return 1;
    }
  else
    return 0;
  }

int bg_cfg_section_get_parameter_string(const gavl_dictionary_t * section,
                                        const char * name,
                                        const char ** value)
  {
  if((*value = gavl_dictionary_get_string(section, name)))
    return 1;
  else
    return 0;
  }

int bg_cfg_section_get_parameter_time(const gavl_dictionary_t * section,
                                      const char * name, gavl_time_t * value)
  
  {
  return gavl_dictionary_get_long(section, name, value);
  }

/* Copy one config section to another */

gavl_dictionary_t * bg_cfg_section_copy(const gavl_dictionary_t * src)
  {
  gavl_dictionary_t * ret = calloc(1, sizeof(*ret));
  gavl_dictionary_copy(ret, src);
  return ret;
  }

void bg_cfg_section_transfer(gavl_dictionary_t * src, gavl_dictionary_t * dst)
  {
  /* Copy items */
  gavl_dictionary_copy(dst, src);
  }

const char * bg_cfg_section_get_name(gavl_dictionary_t * s)
  {
  if(!s)
    return NULL;
  return gavl_dictionary_get_string(s, BG_CFG_TAG_NAME);
  }


gavl_dictionary_t *
bg_cfg_section_create_from_parameters(const char * name,
                                      const bg_parameter_info_t * parameters)
  {
  gavl_dictionary_t * ret;
  ret = bg_cfg_section_create(name);
  bg_cfg_section_create_items(ret, parameters);
  return ret;
  }

void bg_cfg_section_create_items(gavl_dictionary_t * section,
                                 const bg_parameter_info_t * info)
  {
  gavl_dictionary_t params;
  gavl_dictionary_init(&params);
  gavl_parameter_info_append_static(&params, info);
  bg_cfg_section_set_from_params(section, &params);
  gavl_dictionary_free(&params);
  // create_items(section, info, 0);
  }

void bg_cfg_section_delete_subsection(gavl_dictionary_t * section,
                                      gavl_dictionary_t * subsection)
  {
  const char * name;
  if(!(section = gavl_dictionary_get_dictionary_create(section, BG_CFG_TAG_CHILDREN)) ||
     !(name = gavl_dictionary_get_string(subsection, BG_CFG_TAG_NAME)))
    return;
  gavl_dictionary_set(section, name, NULL);
  }

void bg_cfg_section_delete_subsections(gavl_dictionary_t * section)
  {
  if(gavl_dictionary_set(section, BG_CFG_TAG_CHILDREN, NULL))
    return;
  }

void bg_cfg_section_restore_defaults(gavl_dictionary_t * s,
                                     const bg_parameter_info_t * info)
  {
  gavl_value_t * item;
  int i;
  gavl_dictionary_t * subsection;
  gavl_dictionary_t * subsubsection;
  
  while(info->name)
    {
    if(info->flags & BG_PARAMETER_HIDE_DIALOG)
      {
      info++;
      continue;
      }
    item = find_item_nocreate(s, info);

    if(!item)
      {
      info++;
      continue;
      }
    
    gavl_value_copy(item, &info->val_default);
    
    if(info->multi_parameters && bg_cfg_section_has_subsection(s, info->name))
      {
      subsection = bg_cfg_section_find_subsection(s, info->name);
      i = 0;
      
      while(info->multi_names[i])
        {
        if(info->multi_parameters[i] &&
           bg_cfg_section_has_subsection(subsection, info->multi_names[i]))
          {
          subsubsection =
            bg_cfg_section_find_subsection(subsection, info->multi_names[i]);
          bg_cfg_section_restore_defaults(subsubsection,
                                          info->multi_parameters[i]);
          }
        i++;
        }
      }
    info++;
    }
  }

void bg_cfg_section_set_parameter_func(void * data,
                                       const char * name,
                                       const gavl_value_t * value)
  {
  if(name)
    gavl_dictionary_set(data, name, value);
  }

void bg_cfg_section_set_from_params(gavl_dictionary_t * section,
                                    const gavl_dictionary_t * params)
  {
  int num;
  int i;

  num = gavl_parameter_num_params(params);

  gavl_dictionary_set_string(section, BG_CFG_TAG_NAME,
                             gavl_dictionary_get_string(params, GAVL_META_NAME));
  
  for(i = 0; i < num; i++)
    {
    const char * name;
    const gavl_dictionary_t * param = gavl_parameter_get_param(params, i);
    gavl_value_t val;
    
    if(!(name = gavl_dictionary_get_string(param, GAVL_META_NAME)))
      continue;

    gavl_value_init(&val);
    
    if(bg_parameter_init_value(param, &val))
      gavl_dictionary_set_nocopy(section, name, &val);
    
    }
  
  num = gavl_parameter_num_sections(params);
  for(i = 0; i < num; i++)
    {
    const char * name;
    gavl_dictionary_t * subsection;
    const gavl_dictionary_t * subparams = gavl_parameter_get_section(params, i);

    if(!(name = gavl_dictionary_get_string(subparams, GAVL_META_NAME)))
      continue;
    
    subsection =
      bg_cfg_section_find_subsection(section, name);
    
    bg_cfg_section_set_from_params(subsection, subparams);
    }
  
  }

int
bg_parameter_init_value(const gavl_dictionary_t * param,
                        gavl_value_t * val)
  {
  const gavl_value_t * val_default;
  int type = 0;
  
  if(!gavl_dictionary_get_int(param, GAVL_PARAMETER_TYPE, &type) ||
     !type)
    return 0;
    
  if((val_default = gavl_dictionary_get(param, GAVL_PARAMETER_DEFAULT)))
    {
    gavl_value_copy(val, val_default);
    return 1;
    }
  
  switch((gavl_parameter_type_t)type)
    {
    case GAVL_PARAMETER_SECTION:       //!< Dummy type. It contains no data but acts as a separator in notebook style configuration windows
      break;
    case GAVL_PARAMETER_CHECKBUTTON:   //!< Bool
    case GAVL_PARAMETER_INT:           //!< Integer spinbutton
    case GAVL_PARAMETER_SLIDER_INT:    //!< Integer slider
      gavl_value_set_int(val, 0);
      return 1;
      break;
    case GAVL_PARAMETER_FLOAT:         //!< Float spinbutton
    case GAVL_PARAMETER_SLIDER_FLOAT:  //!< Float slider
      gavl_value_set_float(val, 0.0);
      return 1;
      break;
    case GAVL_PARAMETER_STRING:        //!< String (one line only)
    case GAVL_PARAMETER_STRING_HIDDEN: //!< Encrypted string (displays as ***)
    case GAVL_PARAMETER_FONT:          //!< Font (contains fontconfig compatible fontname)
    case GAVL_PARAMETER_FILE:          //!< File
    case GAVL_PARAMETER_DIRECTORY:     //!< Directory
      gavl_value_set_string(val, NULL);
      return 1;
      break;
    case GAVL_PARAMETER_STRINGLIST:    //!< Popdown menu with string values
      /* Select first */
      {
      const gavl_dictionary_t * opt;
      if((opt = gavl_parameter_get_option(param, 0)))
        gavl_value_set_string(val, gavl_dictionary_get_string(opt, GAVL_META_NAME));
      return 1;
      }
      break;
    case GAVL_PARAMETER_DIRLIST:       //!< List of directories
    case GAVL_PARAMETER_MULTI_CHAIN:   //!< Several subitems (including suboptions) can be arranged in a chain
      gavl_value_set_array(val);      /* Empty array */
      return 1;
      break;
    case GAVL_PARAMETER_COLOR_RGB:     //!< RGB Color
      gavl_value_set_color_rgb(val);      /* Empty array */
      return 1;
      break;
    case GAVL_PARAMETER_COLOR_RGBA:    //!< RGBA Color
      gavl_value_set_color_rgba(val);      /* Empty array */
      return 1;
      break;
    case GAVL_PARAMETER_MULTI_MENU:    //!< Menu with config- and infobutton
      {
      /* Select first */
      gavl_dictionary_t * dict;
      const gavl_dictionary_t * opt;

      if((opt = gavl_parameter_get_option(param, 0)))
        {
        dict = gavl_value_set_dictionary(val);

        bg_cfg_section_set_from_params(dict, opt);
#if 0
        fprintf(stderr, "Got options\n");
        gavl_dictionary_dump(dict, 2);
        fprintf(stderr, "\nOPT:\n");
        gavl_dictionary_dump(opt, 2);
        fprintf(stderr, "\nParam:\n");
        gavl_dictionary_dump(param, 2);
        fprintf(stderr, "\n");
#endif
        return 1;
        }
      }
      break;
    case GAVL_PARAMETER_MULTI_LIST:    //!< List with config- and infobutton
      {
      /* Build array and parameters */
      gavl_array_t * arr;
      gavl_dictionary_t * dict;
      const gavl_dictionary_t * opt;
      int j;
      int num_options = gavl_parameter_num_options(param);
        
      arr = gavl_value_set_array(val);
        
      for(j = 0; j < num_options; j++)
        {
        if(!(opt = gavl_parameter_get_option(param, j)))
          break;

        dict = gavl_array_append_dictionary(arr);
            
        bg_cfg_section_set_from_params(dict, opt);
        }
#if 0
      fprintf(stderr, "Got multi list\n");
      gavl_array_dump(arr, 2);
      fprintf(stderr, "\n");
#endif
      return 1;
      }
      break;
    case GAVL_PARAMETER_TIME:          //!< Time
      gavl_value_set_long(val, 0);
      return 1;
      break;
    case GAVL_PARAMETER_POSITION:      //!< Position (x/y coordinates, scaled 0..1)
      gavl_value_set_position(val);
      return 1;
      break;
    case GAVL_PARAMETER_BUTTON:        //!< Pressing the button causes set_parameter to be called with NULL value
      gavl_value_set_string(val, NULL);
      return 1;
      break;
    }
  return 0;
  }
