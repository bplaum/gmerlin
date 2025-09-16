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
    gavl_dictionary_set(section, info->name, &info->val_default);
    ret = gavl_dictionary_get_nc(section, info->name);
    
    if(ret->type == GAVL_TYPE_UNDEFINED)
      gavl_value_set_type(ret, bg_parameter_type_to_gavl(info->type));
    
    /* For the MULTI_LIST type, we create the default value from the parameters */
    if(info->type == BG_PARAMETER_MULTI_LIST)
      {
      int i = 0;
      gavl_array_t * arr = gavl_value_set_array(ret);

      //      fprintf(stderr, "Creating default multi_list\n");
      
      while(info->multi_names[i])
        {
        gavl_dictionary_t * dict;
        gavl_value_t val;

        gavl_value_init(&val);
        dict = gavl_value_set_dictionary(&val);
        gavl_dictionary_set_string(dict, BG_CFG_TAG_NAME, info->multi_names[i]);
        
        if(info->multi_parameters[i])
          bg_cfg_section_create_items(dict, info->multi_parameters[i]);
        gavl_array_splice_val_nocopy(arr, i, 0, &val);
        i++;
        }
      }
    else if(info->type == BG_PARAMETER_MULTI_MENU)
      bg_multi_menu_create(ret, info);
    }
  else // Entry alredy present, update from parameter info
    {
    if(info->type == BG_PARAMETER_MULTI_MENU)
      bg_multi_menu_update(ret, info);
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

static char * parse_string(const char * str, int * len_ret)
  {
  const char * end_c;
  char cpy_str[2];
  char * ret = NULL;
  end_c = str;

  cpy_str[1] = '\0';
      
  while(*end_c != '\0')
    {
    if(*end_c == '\\')
      {
      if((*(end_c+1) == ':') ||
         (*(end_c+1) == '{') ||
         (*(end_c+1) == '}'))
        {
        end_c++;
        cpy_str[0] = *end_c;
        ret = gavl_strcat(ret, cpy_str);
        }
      else
        {
        cpy_str[0] = *end_c;
        ret = gavl_strcat(ret, cpy_str);
        }
      }
    else if((*end_c == ':') ||
            (*end_c == '{') ||
            (*end_c == '}'))
      {
      break;
      }
    else
      {
      cpy_str[0] = *end_c;
      ret = gavl_strcat(ret, cpy_str);
      }
    end_c++;
    }
  *len_ret = end_c - str;
  
  return ret;
  }

static const bg_parameter_info_t *
find_parameter(const bg_parameter_info_t * info,
               const char * str, int * len, char ** subsection_name)
  {
  FILE * out = stderr;
  int i;
  const char * end;

  *subsection_name = NULL;
  
  /* Get the options name */
  end = str;
  while((*end != '=') && (*end != '\0'))
    end++;
  
  if(*end == '\0')
    return NULL;
  
  /* Now, find the parameter info */
  i = 0;

  while(info[i].name)
    {
    if((info[i].type == BG_PARAMETER_SECTION) &&
       (info[i].flags & BG_PARAMETER_OWN_SECTION))
      *subsection_name = info[i].name;
    
    if((strlen(info[i].name) == (end - str)) &&
       !strncmp(info[i].name, str, end - str))
      break;
    i++;
    }
  
  if(!info[i].name)
    {
    fprintf(out, "No such option ");
    fwrite(str, 1, end - str, out);
    fprintf(out, "\n");
    return NULL;
    }
  *len = (end - str + 1); // Skip '=' as well
  return &info[i];
  }

static int check_option(const bg_parameter_info_t * info,
                        char * name)
  {
  int i = 0;
  FILE * out = stderr;

  if(!info->multi_names) // Can be that there are no options available
    return 1;
  
  while(info->multi_names[i])
    {
    if(!strcmp(info->multi_names[i], name))
      return 1;
    i++;
    }
  fprintf(out, "Unsupported option: %s\n", name);
  return 0;
  }


/* Returns characters read or 0 */
int
bg_cfg_section_set_parameters_from_string(gavl_dictionary_t * sec,
                                          const bg_parameter_info_t * parameters,
                                          const char * str_start)
  {
  FILE * out = stderr;
  char * end;
  const char * end_c;
  gavl_value_t * item;
  int len = 0, i, index;
  const bg_parameter_info_t * info;
  char * real_section_name;
  
  gavl_dictionary_t * real_section;
  gavl_dictionary_t * subsection;
  
  const char * str = str_start;
  
  while(1)
    {
    if((*str == '\0') || (*str == '}'))
      return str - str_start;
    
    info = find_parameter(parameters, str, &len, &real_section_name);

    if(real_section_name)
      real_section = bg_cfg_section_find_subsection(sec, real_section_name);
    else
      real_section = sec;
    
    if(!info || (info->type == BG_PARAMETER_SECTION) ||
       (info->type == BG_PARAMETER_BUTTON))
      {
      fprintf(out, "Unsupported parameter ");
      fwrite(str, 1, len, out);
      fprintf(out, "\n");
      goto fail;
      }
    item = bg_cfg_section_find_item(real_section, info);
    gavl_value_free(item);
    gavl_value_init(item);
    
    str += len;
    
    switch(info->type)
      {
      case BG_PARAMETER_CHECKBUTTON:
      case BG_PARAMETER_INT:
      case BG_PARAMETER_SLIDER_INT:
        gavl_value_set_int(item, strtol(str, &end, 10));
        if(str == end)
          goto fail;
        str = end;        
        break;
      case BG_PARAMETER_TIME:
        gavl_value_set_long(item, 0);

        end_c = str;
        if(*end_c == '{')
          end_c++;
        end_c += gavl_time_parse(end_c, &item->v.l);
        if(*end_c == '}')
          end_c++;
        str = end_c;
        break;
      case BG_PARAMETER_FLOAT:
      case BG_PARAMETER_SLIDER_FLOAT:
        gavl_value_set_float(item, strtod(str, &end));
        if(str == end)
          goto fail;
        str = end;
        break;
      case BG_PARAMETER_FILE:
      case BG_PARAMETER_DIRECTORY:
      case BG_PARAMETER_FONT:
      case BG_PARAMETER_STRING:
      case BG_PARAMETER_STRINGLIST:
      case BG_PARAMETER_STRING_HIDDEN:
        gavl_value_set_string_nocopy(item, parse_string(str, &len));

        /* Check if the string is in the options */
        if(info->type == BG_PARAMETER_STRINGLIST)
          {
          if(!check_option(info, item->v.str))
            goto fail;
          }
        
        str += len;
        break;
      case BG_PARAMETER_MULTI_LIST:
      case BG_PARAMETER_MULTI_CHAIN:
        {
        gavl_value_t val;
        gavl_array_t * arr;
        
        if(*str != '{')
          {
          fprintf(out,
                  "%s must be in form {option[{suboptions}][:option[{suboption}]]}...\n",
                  info->name);
          goto fail;
          }
        str++;

        arr = gavl_value_set_array(item);
        
        index = 0;
        while(1)
          {
          char * tmp_string;
          /* Loop over options */
          tmp_string = parse_string(str, &len);
          if(!check_option(info, tmp_string))
            {
            free(tmp_string);
            goto fail;
            }

          gavl_value_init(&val);
          subsection = gavl_value_set_dictionary(&val);
          gavl_dictionary_set_string(subsection, BG_CFG_TAG_NAME, tmp_string);
          
          str += len;
          
          /* Suboptions */
          if(*str == '{')
            {
            str++;

            i = 0;
            
            while(info->multi_names[i])
              {
              if(!strcmp(info->multi_names[i], tmp_string))
                break;
              i++;
              }
            if(!info->multi_names[i])
              {
              free(tmp_string);
              return 0; // Already checked by check_option above
              }
            str += bg_cfg_section_set_parameters_from_string(subsection,
                                                             info->multi_parameters[i],
                                                             str);
            if(*str != '}')
              {
              free(tmp_string);
              goto fail;
              }
            str++;
            }
          if(*str == '}')
            {
            str++;
            break;
            }
          else if(*str != ':')
            {
            free(tmp_string);
            goto fail;
            }
          str++;
          free(tmp_string);
          index++;
          
          gavl_array_splice_val(arr, arr->num_entries, -1, &val);
          }
        break;
        }
      case BG_PARAMETER_MULTI_MENU:
        {
        char * tmp_string;
        
        bg_multi_menu_create(item, info);

        tmp_string = parse_string(str, &len);
        bg_multi_menu_set_selected_name(item, tmp_string);
        
        if(!check_option(info, tmp_string))
          {
          free(tmp_string);
          goto fail;
          }
        free(tmp_string);
        
        str += len;
        
        /* Parse sub parameters */

        if(*str == '{')
          {
          str++;
          subsection = bg_cfg_section_find_subsection(real_section, info->name);
          subsection = bg_cfg_section_find_subsection(subsection, item->v.str);
          i = 0;

          while(info->multi_names[i])
            {
            if(!strcmp(info->multi_names[i], item->v.str))
              break;
            i++;
            }
          if(!info->multi_names[i])
            return 0;

          str += bg_cfg_section_set_parameters_from_string(subsection,
                                                           info->multi_parameters[i],
                                                           str);
          if(*str != '}')
            goto fail;
          str++;
          }
        }
        break;
      case BG_PARAMETER_COLOR_RGB:
      case BG_PARAMETER_COLOR_RGBA:
        {
        double * c;

        if(info->type == BG_PARAMETER_COLOR_RGBA)
          c = gavl_value_set_color_rgba(item);
        else
          c = gavl_value_set_color_rgb(item);
        
        if(*str == '\0')
          goto fail;
        c[0] = strtod(str, &end);
        str = end;

        if(*str == '\0')
          goto fail;
        str++; // ,
        c[1] = strtod(str, &end);
        str = end;

        if(*str == '\0')
          goto fail;
        str++; // ,
        c[2] = strtod(str, &end);
        if(str == end)
          goto fail;
        
        if(info->type == BG_PARAMETER_COLOR_RGBA)
          {
          str = end;
          if(*str == '\0')
            goto fail;
          str++; // ,
          c[3] = strtod(str, &end);
          
          if(str == end)
            goto fail;
          }
        str = end;
        }
        break;
      case BG_PARAMETER_POSITION:
        if(*str == '\0')
          goto fail;
        item->v.position[0] = strtod(str, &end);
        str = end;

        if(*str == '\0')
          goto fail;
        str++; // ,
        item->v.position[1] = strtod(str, &end);
        str = end;
        break;
      case BG_PARAMETER_DIRLIST:
        {
        int idx;
        char ** dirs;
        gavl_array_t * arr;
        
        /* Dirlist is colon separated */
        char * tmp_string = parse_string(str, &len);

        dirs = gavl_strbreak(tmp_string, ':');

        arr = gavl_value_set_array(item);
        
        idx = 0;

        while(dirs[idx])
          {
          gavl_string_array_add(arr, dirs[idx]);
          idx++;
          }
        
        gavl_strbreak_free(dirs);
        free(tmp_string);
        }
        break;
      case BG_PARAMETER_SECTION:
      case BG_PARAMETER_BUTTON:
        break;
      }
    if(*str == ':')
      str++;
    else if(*str == '}')
      break;
    else if(*str == '\0')
      break;
    else
      goto fail;
    }

  return str - str_start;
  
  fail:
  fprintf(out, "Error parsing option\n");
  fprintf(out, "%s\n", str_start);
  
  for(i = 0; i < (int)(str - str_start); i++)
    fprintf(out, " ");
  fprintf(out, "^\n");
  return 0;
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

      type = bg_parameter_type_to_gavl(infos[num].type);

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
    gavl_dictionary_foreach(section, func, callback_data);

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

void bg_cfg_section_get(gavl_dictionary_t * section,
                        const bg_parameter_info_t * infos,
                        bg_get_parameter_func_t func,
                        void * callback_data)
  {
  int num;
  gavl_value_t * item;

  if(!func || !infos)
    return;
  
  num = 0;

  while(infos[num].name)
    {
    item = bg_cfg_section_find_item(section, &infos[num]);
    if(item)
      func(callback_data, infos[num].name, item);
    num++;
    }
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

static void create_items(gavl_dictionary_t * section,
                         const bg_parameter_info_t * info, int stop_at_section)
  {
  int i;
  int j;

  gavl_dictionary_t * subsection;
  gavl_dictionary_t * subsubsection;

  i = 0;

  //  if(section->name && !strcmp(section->name, "e_mpeg"))
  //    fprintf(stderr, "e_mpeg\n");
  
  while(info[i].name)
    {
    if((info[i].type == BG_PARAMETER_SECTION) &&
       stop_at_section)
      return;
    
    if(info[i].flags & BG_PARAMETER_OWN_SECTION)
      {
      subsection =
        bg_cfg_section_find_subsection(section, info[i].name);
      i++;
      create_items(subsection,
                   &info[i], 1);

      /* Skip what we read so far */
      while(info[i].type != BG_PARAMETER_SECTION)
        {
        if(!info[i].name)
          return;
        i++;
        }
      }
    else
      {
#if 1
      //      fprintf(stderr, "Section: %s, parameter: %s\n",
      //            section->name, info[i].name);

      if(info[i].type != BG_PARAMETER_SECTION)
        bg_cfg_section_find_item(section, &info[i]);
      
      if(info[i].multi_parameters &&
         (info[i].type != BG_PARAMETER_MULTI_CHAIN) &&
         (info[i].type != BG_PARAMETER_MULTI_MENU))
        {
        j = 0;

        subsection = bg_cfg_section_find_subsection(section, info[i].name);
  
        while(info[i].multi_names[j])
          {
          if(info[i].multi_parameters[j])
            {
            subsubsection =
              bg_cfg_section_find_subsection(subsection, info[i].multi_names[j]);
          
            bg_cfg_section_create_items(subsubsection,
                                        info[i].multi_parameters[j]);
            }
          j++;
          }
        }
#endif
      i++;
      }
    }
  }

void bg_cfg_section_create_items(gavl_dictionary_t * section,
                                 const bg_parameter_info_t * info)
  {
  create_items(section, info, 0);
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
