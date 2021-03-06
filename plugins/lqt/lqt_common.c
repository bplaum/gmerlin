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

#include <config.h>

#include <gmerlin/translation.h>

#include <gmerlin/plugin.h>
#include <gmerlin/utils.h>
#include <gmerlin/log.h>

#include "lqt_common.h"

void bg_lqt_create_codec_info(bg_parameter_info_t * info,
                              int audio, int video, int encode, int decode)
  {
  int num_codecs;
  int i, j, k, index;
  int num_parameters;
  
  lqt_codec_info_t ** codec_info;

  lqt_parameter_info_t * lqt_parameter_info;
    
  codec_info = lqt_query_registry(audio, video, encode, decode);
  info->type = (encode) ? BG_PARAMETER_MULTI_MENU :
    BG_PARAMETER_MULTI_LIST;

  num_codecs = 0;
  while(codec_info[num_codecs])
    num_codecs++;

  info->multi_names_nc        = calloc(num_codecs + 1, sizeof(char*));
  info->multi_labels_nc       = calloc(num_codecs + 1, sizeof(char*));
  info->multi_descriptions_nc = calloc(num_codecs + 1, sizeof(char*));
  info->multi_parameters_nc   = calloc(num_codecs + 1,
                                       sizeof(bg_parameter_info_t*));
  bg_parameter_info_set_const_ptrs(info);

  index = 0;
  
  for(i = 0; i < num_codecs; i++)
    {
    /* Warning: Taking the first codec will break if external
       libquicktime codecs appear. Unlikely to happen though */
    if(!info->gettext_domain)
      {
      info->gettext_domain =
        gavl_strrep(info->gettext_domain,
                  codec_info[i]->gettext_domain);
      info->gettext_directory =
        gavl_strrep(info->gettext_directory,
                  codec_info[i]->gettext_directory);
      }

    if(encode && (codec_info[i]->compatibility_flags & LQT_CODEC_OBSOLETE))
      continue;
    
    lqt_parameter_info = (encode) ? codec_info[i]->encoding_parameters :
      codec_info[i]->decoding_parameters;
    num_parameters = (encode) ? codec_info[i]->num_encoding_parameters :
      codec_info[i]->num_decoding_parameters;
    
    if(!info->val_default.val_str)
      info->val_default.val_str = gavl_strdup(codec_info[i]->name);
    
    info->multi_names_nc[index] = gavl_strdup(codec_info[i]->name);
    info->multi_labels_nc[index] = gavl_strdup(codec_info[i]->long_name);

    if(encode)
      {
      info->multi_descriptions_nc[index] = bg_sprintf(TR("%s Compatible with"),
                                               codec_info[i]->description);
      
      if(codec_info[i]->compatibility_flags & (LQT_FILE_QT | LQT_FILE_QT_OLD))
        info->multi_descriptions_nc[index] = gavl_strcat(info->multi_descriptions_nc[index], " QT");
      if(codec_info[i]->compatibility_flags & ( LQT_FILE_MP4))
        info->multi_descriptions_nc[index] = gavl_strcat(info->multi_descriptions_nc[index], " MP4");
      if(codec_info[i]->compatibility_flags & ( LQT_FILE_M4A))
        info->multi_descriptions_nc[index] = gavl_strcat(info->multi_descriptions_nc[index], " M4A");
      if(codec_info[i]->compatibility_flags & ( LQT_FILE_AVI))
        info->multi_descriptions_nc[index] = gavl_strcat(info->multi_descriptions_nc[index], " AVI");
      if(codec_info[i]->compatibility_flags & ( LQT_FILE_3GP))
        info->multi_descriptions_nc[index] = gavl_strcat(info->multi_descriptions_nc[index], " 3GP");
      }
    else
      info->multi_descriptions_nc[index] = bg_sprintf("%s",
                                                      codec_info[i]->description);
    
    if(num_parameters)
      info->multi_parameters_nc[index] = calloc(num_parameters + 1,
                                            sizeof(bg_parameter_info_t));
    
    for(j = 0; j < num_parameters; j++)
      {
      if(!j)
        {
        info->multi_parameters_nc[index][j].gettext_domain =
          gavl_strrep(info->multi_parameters_nc[index][j].gettext_domain,
                    codec_info[i]->gettext_domain);
        info->multi_parameters_nc[index][j].gettext_directory =
          gavl_strrep(info->multi_parameters_nc[index][j].gettext_directory,
                    codec_info[i]->gettext_directory);
        }
      
      //      if(encode)
        info->multi_parameters_nc[index][j].name =
          gavl_strrep(info->multi_parameters_nc[index][j].name,
                    lqt_parameter_info[j].name);
        //      else
        //        info->multi_parameters[i][j].name =
        //          bg_sprintf("%s.%s", info->multi_names[i], lqt_parameter_info[j].name);

      info->multi_parameters_nc[index][j].long_name = 
        gavl_strdup(lqt_parameter_info[j].real_name);

      if(lqt_parameter_info[j].help_string)
        {
        info->multi_parameters_nc[index][j].help_string = 
          gavl_strdup(lqt_parameter_info[j].help_string);
        
        }
      
      switch(lqt_parameter_info[j].type)
        {
        case LQT_PARAMETER_INT:
          if(lqt_parameter_info[j].val_min.val_int <
             lqt_parameter_info[j].val_max.val_int)
            {
            if((lqt_parameter_info[j].val_min.val_int == 0) &&
               (lqt_parameter_info[j].val_max.val_int == 1))
              {
              info->multi_parameters_nc[index][j].type = BG_PARAMETER_CHECKBUTTON;
              }
            else
              {
              info->multi_parameters_nc[index][j].type = BG_PARAMETER_SLIDER_INT;
              info->multi_parameters_nc[index][j].val_min.val_i =
                lqt_parameter_info[j].val_min.val_int;
              info->multi_parameters_nc[index][j].val_max.val_i =
                lqt_parameter_info[j].val_max.val_int;
              }
            }
          else
            {
            info->multi_parameters_nc[index][j].type = BG_PARAMETER_INT;
            }
          info->multi_parameters_nc[index][j].val_default.val_i =
            lqt_parameter_info[j].val_default.val_int;
          break;
        case LQT_PARAMETER_FLOAT:
          if(lqt_parameter_info[j].val_min.val_float <
             lqt_parameter_info[j].val_max.val_float)
            {
            info->multi_parameters_nc[index][j].type = BG_PARAMETER_SLIDER_FLOAT;
            info->multi_parameters_nc[index][j].val_min.val_f =
              lqt_parameter_info[j].val_min.val_float;
            info->multi_parameters_nc[index][j].val_max.val_f =
              lqt_parameter_info[j].val_max.val_float;
            }
          else
            {
            info->multi_parameters_nc[index][j].type = BG_PARAMETER_FLOAT;
            }
          info->multi_parameters_nc[index][j].num_digits =
            lqt_parameter_info[j].num_digits;

          info->multi_parameters_nc[index][j].val_default.val_f =
            lqt_parameter_info[j].val_default.val_float;
          break;
        case LQT_PARAMETER_STRING:
          info->multi_parameters_nc[index][j].type = BG_PARAMETER_STRING;
          info->multi_parameters_nc[index][j].val_default.val_str =
            gavl_strdup(lqt_parameter_info[j].val_default.val_string);
          
          break;
        case LQT_PARAMETER_STRINGLIST:
          info->multi_parameters_nc[index][j].type = BG_PARAMETER_STRINGLIST;
          info->multi_parameters_nc[index][j].val_default.val_str =
            gavl_strdup(lqt_parameter_info[j].val_default.val_string);

          info->multi_parameters_nc[index][j].multi_names_nc =
            calloc(lqt_parameter_info[j].num_stringlist_options+1,
                   sizeof(char*));
          
          for(k = 0; k < lqt_parameter_info[j].num_stringlist_options; k++)
            {
            info->multi_parameters_nc[index][j].multi_names_nc[k] =
              gavl_strdup(lqt_parameter_info[j].stringlist_options[k]);
            }
          bg_parameter_info_set_const_ptrs(&info->multi_parameters_nc[index][j]);
          break;
        case LQT_PARAMETER_SECTION:
          info->multi_parameters_nc[index][j].type = BG_PARAMETER_SECTION;
          break;
        }
      
      }
    index++;
    }
  lqt_destroy_codec_info(codec_info);
  }

static const void * get_value(lqt_parameter_info_t * lqt_parameter_info,
                              const char * name,
                              const gavl_value_t * val,
                              float * dummy_f)
  {
  int index;
  index = 0;

  while(lqt_parameter_info[index].name)
    {
    if(!strcmp(lqt_parameter_info[index].name, name))
      {
      switch(lqt_parameter_info[index].type)
        {
        case LQT_PARAMETER_INT:
          return &val->v.i;
          break;
        case LQT_PARAMETER_FLOAT:
          *dummy_f = val->v.d;
          return dummy_f;
          break;
        case LQT_PARAMETER_STRING:
        case LQT_PARAMETER_STRINGLIST:
          return val->v.str;
          break;
        case LQT_PARAMETER_SECTION:
          return NULL;
        }
      break;
      }
    index++;
    }
  return NULL;
  }
                        
void bg_lqt_set_audio_parameter(quicktime_t * file,
                                int stream,
                                const char * name,
                                const gavl_value_t * val,
                                lqt_parameter_info_t * lqt_parameter_info)
  {
  const void * val_ptr;
  float dummy_f;
  val_ptr = get_value(lqt_parameter_info, name, val, &dummy_f);
  if(val_ptr)
    {
    lqt_set_audio_parameter(file, stream, name, val_ptr);
    }
  
  }

void bg_lqt_set_video_parameter(quicktime_t * file,
                                int stream,
                                const char * name,
                                const gavl_value_t * val,
                                lqt_parameter_info_t * lqt_parameter_info)
  {
  const void * val_ptr;
  float dummy_f;
  val_ptr = get_value(lqt_parameter_info, name, val, &dummy_f);
  if(val_ptr)
    {
    lqt_set_video_parameter(file, stream, name, val_ptr);
    }
  }

static void set_decoder_parameter(const char * codec_name,
                                  const char * parameter_name,
                                  const gavl_value_t * val,
                                  lqt_codec_info_t ** codec_info_arr)
  {
  int i;
  lqt_codec_info_t *  codec_info = NULL;

  lqt_parameter_value_t lqt_val;
  lqt_parameter_info_t * lqt_parameter_info = NULL;
  
  /* This code stores the values in the lqt plugin
     registry, which is also not good.
     
     To fix this, we need to keep our own bg_cfg_section_t,
     save the values there and call lqt_set_[audio|video]_parameter
     for each opened file */


  
  i = 0;
  
  while(codec_info_arr[i])
    {
    if(!strcmp(codec_info_arr[i]->name, codec_name))
      {
      codec_info = codec_info_arr[i];
      break;
      }
    i++;
    }
  if(!codec_info)
    {
    return;
    }
  /* Get the decoding parameters */

  for(i = 0; i < codec_info->num_decoding_parameters; i++)
    {
    if(!strcmp(codec_info->decoding_parameters[i].name,
               parameter_name))
      {
      lqt_parameter_info = &codec_info->decoding_parameters[i];
      }
    }

  if(!lqt_parameter_info)
    {
    return;
    }
    
  switch(lqt_parameter_info->type)
    {
    case LQT_PARAMETER_INT:
      lqt_val.val_int = val->v.i;
      break;
    case LQT_PARAMETER_FLOAT:
      lqt_val.val_float = val->v.d;
      break;
    case LQT_PARAMETER_STRING:
    case LQT_PARAMETER_STRINGLIST:
      lqt_val.val_string = val->v.str;
      break;
    case LQT_PARAMETER_SECTION:
      return;
    }
  
  lqt_set_default_parameter(codec_info->type, 0,
                            codec_info->name,
                            lqt_parameter_info->name,
                            &lqt_val);
  }

void bg_lqt_set_audio_decoder_parameter(const char * codec_name,
                                        const char * parameter_name,
                                        const gavl_value_t * val)
  {
  lqt_codec_info_t ** codec_info_arr;
  codec_info_arr = lqt_query_registry(1, 0, 0, 1);

  set_decoder_parameter(codec_name, parameter_name, val, codec_info_arr);

  if(codec_info_arr)
    lqt_destroy_codec_info(codec_info_arr);

  
  }

void bg_lqt_set_video_decoder_parameter(const char * codec_name,
                                        const char * parameter_name,
                                        const gavl_value_t * val)
  {
  lqt_codec_info_t ** codec_info_arr;
  codec_info_arr = lqt_query_registry(0, 1, 0, 1);

  set_decoder_parameter(codec_name, parameter_name, val, codec_info_arr);
  
  if(codec_info_arr)
    lqt_destroy_codec_info(codec_info_arr);
  }

void bg_lqt_log(lqt_log_level_t level, const char * log_domain, const char * message,
                void * data)
  {
  gavl_log_level_t gmerlin_level = GAVL_LOG_INFO;
  char * gmerlin_domain = bg_sprintf("lqt.%s", log_domain);
  
  switch(level)
    {
    case LQT_LOG_INFO:
      gmerlin_level = GAVL_LOG_INFO;
      break;
    case LQT_LOG_ERROR:
      gmerlin_level = GAVL_LOG_ERROR;
      break;
    case LQT_LOG_WARNING:
      gmerlin_level = GAVL_LOG_WARNING;
      break;
    case LQT_LOG_DEBUG:
      gmerlin_level = GAVL_LOG_DEBUG;
      break;
    }

  gavl_logs_notranslate(gmerlin_level, gmerlin_domain, message);
  free(gmerlin_domain);
  }
