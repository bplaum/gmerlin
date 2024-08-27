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
#include <string.h>
#include <ctype.h>
#include <time.h>

#include <config.h>
#include <gmerlin/translation.h>

#include <gmerlin/parameter.h>
#include <gmerlin/streaminfo.h>
#include <gmerlin/utils.h>

#include <gavl/compression.h>
#include <gavl/metatags.h>

#define PARAM_LABEL \
    { \
      .name =      GAVL_META_LABEL,       \
      .long_name = TRS("Label"), \
      .type =      BG_PARAMETER_STRING, \
      .help_string = TRS("Used for generation of output file names"),   \
    }

#define PARAM_ARTIST \
    { \
      .name =      GAVL_META_ARTIST, \
      .long_name = TRS("Artist"), \
      .type =      BG_PARAMETER_STRING, \
      .help_string = TRS("Separate multiple entries by semicolons"),   \
    }

#define PARAM_ALBUMARTIST \
    { \
      .name =      GAVL_META_ALBUMARTIST, \
      .long_name = TRS("Album artist"), \
      .type =      BG_PARAMETER_STRING, \
      .help_string = TRS("Separate multiple entries by semicolons"),   \
    }

#define PARAM_TITLE \
    { \
      .name =      GAVL_META_TITLE, \
      .long_name = TRS("Title"), \
      .type =      BG_PARAMETER_STRING, \
    }

#define PARAM_ALBUM \
    { \
      .name =      GAVL_META_ALBUM, \
      .long_name = TRS("Album"), \
      .type =      BG_PARAMETER_STRING, \
    }

#define PARAM_TRACK \
    { \
      .name =      GAVL_META_TRACKNUMBER, \
      .long_name = TRS("Track"), \
      .type =      BG_PARAMETER_INT, \
    }

#define PARAM_GENRE \
    { \
      .name =      GAVL_META_GENRE, \
      .long_name = TRS("Genre"), \
      .type =      BG_PARAMETER_STRING, \
      .help_string = TRS("Separate multiple entries by semicolons"),   \
    }

#define PARAM_AUTHOR \
    { \
      .name =      GAVL_META_AUTHOR, \
      .long_name = TRS("Author"), \
      .type =      BG_PARAMETER_STRING, \
      .help_string = TRS("Separate multiple entries by semicolons"),   \
    }

#define PARAM_COPYRIGHT \
    { \
      .name =      GAVL_META_COPYRIGHT, \
      .long_name = TRS("Copyright"), \
      .type =      BG_PARAMETER_STRING, \
    }

#define PARAM_DATE \
    { \
      .name =        GAVL_META_YEAR, \
      .long_name =   TRS("Year"), \
      .type =        BG_PARAMETER_STRING, \
    }

#define PARAM_COMMENT \
    { \
      .name =      GAVL_META_COMMENT, \
      .long_name = TRS("Comment"), \
      .type =      BG_PARAMETER_STRING, \
    }

static const bg_parameter_info_t parameters[] =
  {
    PARAM_LABEL,
    PARAM_ARTIST,
    PARAM_TITLE,
    PARAM_ALBUM,
    PARAM_TRACK,
    PARAM_GENRE,
    PARAM_AUTHOR,
    PARAM_ALBUMARTIST,
    PARAM_COPYRIGHT,
    PARAM_DATE,
    PARAM_COMMENT,
    { /* End of parameters */ }
  };

static const bg_parameter_info_t parameters_common[] =
  {
    PARAM_ARTIST,
    PARAM_ALBUM,
    PARAM_GENRE,
    PARAM_AUTHOR,
    PARAM_ALBUMARTIST,
    PARAM_COPYRIGHT,
    PARAM_DATE,
    PARAM_COMMENT,
    { /* End of parameters */ }
  };


#define SP(s)                            \
  if(!strcmp(ret[i].name, s))                       \
    {                                               \
    if((val = gavl_dictionary_get(m, s)))   \
      gavl_value_copy(&ret[i].val_default, val);    \
    }

#define SP_MULTI(s)                                \
  if(!strcmp(ret[i].name, s))                                 \
    {                                                         \
    char * str;                                               \
    if((str = gavl_metadata_join_arr(m, s, "; ")))    \
      gavl_value_set_string_nocopy(&ret[i].val_default, str); \
    }

static
bg_parameter_info_t * get_parameters(const gavl_dictionary_t * m, int common)
  {
  int i, year;
  bg_parameter_info_t * ret;
  const gavl_value_t * val;
  
  ret = bg_parameter_info_copy_array(common ? parameters_common : parameters);

  if(!m)
    return ret;
  
  i = 0;
  while(ret[i].name)
    {
    SP(GAVL_META_LABEL);
    SP_MULTI(GAVL_META_ARTIST);
    SP_MULTI(GAVL_META_ALBUMARTIST);
    SP(GAVL_META_TITLE);
    SP(GAVL_META_ALBUM);
    
    SP(GAVL_META_TRACKNUMBER);

    if(!strcmp(ret[i].name, GAVL_META_YEAR))
      {
      year = bg_metadata_get_year(m);
      if(year)
        gavl_value_set_string_nocopy(&ret[i].val_default, bg_sprintf("%d", year));
      }
    SP_MULTI(GAVL_META_GENRE);
    SP(GAVL_META_COMMENT);

    SP_MULTI(GAVL_META_AUTHOR);
    SP(GAVL_META_COPYRIGHT);
    
    i++;
    }
  
  return ret;
  }

#undef SP
#undef SP_MULTI

bg_parameter_info_t * bg_metadata_get_parameters(const gavl_dictionary_t * m)
  {
  return get_parameters(m, 0);
  }

bg_parameter_info_t * bg_metadata_get_parameters_common(const gavl_dictionary_t * m)
  {
  return get_parameters(m, 1);
  }


#define SP(s) \
  if(!strcmp(name, s))       \
    { \
    gavl_dictionary_set(m, s, val); \
    return; \
    }

#define SP_STR_MULTI(s) \
  if(!strcmp(name, s))       \
    { \
    char ** arr = NULL;                  \
    int i; \
    gavl_dictionary_set(m, s, NULL);       \
    if(val && (arr = gavl_strbreak(val->v.str, ';'))) \
      { \
      i = 0;        \
      while(arr[i]) \
        { \
        gavl_dictionary_append_string_array(m, s, arr[i]); \
        i++; \
        } \
      } \
    if(arr) \
      gavl_strbreak_free(arr); \
    return; \
    }

void bg_metadata_set_parameter(void * data, const char * name,
                               const gavl_value_t * val)
  {
  gavl_dictionary_t * m = (gavl_dictionary_t*)data;
  
  if(!name)
    return;

  SP_STR_MULTI(GAVL_META_ARTIST);
  SP_STR_MULTI(GAVL_META_ALBUMARTIST);
  SP(GAVL_META_TITLE);
  SP(GAVL_META_ALBUM);
  SP(GAVL_META_LABEL);
  
  SP(GAVL_META_TRACKNUMBER);
  
  if(!strcmp(name, GAVL_META_YEAR))
    {
    gavl_dictionary_set(m, GAVL_META_YEAR, val);
    gavl_dictionary_set(m, GAVL_META_DATE, NULL);
    }
  
  SP_STR_MULTI(GAVL_META_GENRE);
  SP(GAVL_META_COMMENT);
  
  SP_STR_MULTI(GAVL_META_AUTHOR);
  SP(GAVL_META_COPYRIGHT);
  }

#undef SP
#undef SP_STR_MULTI

/* Tries to get a 4 digit year from an arbitrary formatted
   date string.
   Return 0 is this wasn't possible.
*/

static int check_year(const char * pos1)
  {
  if(isdigit(pos1[0]) &&
     isdigit(pos1[1]) &&
     isdigit(pos1[2]) &&
     isdigit(pos1[3]))
    {
    return
      (int)(pos1[0] -'0') * 1000 + 
      (int)(pos1[1] -'0') * 100 + 
      (int)(pos1[2] -'0') * 10 + 
      (int)(pos1[3] -'0');
    }
  return 0;
  }

int bg_metadata_get_year(const gavl_dictionary_t * m)
  {
  int result;

  const char * pos1;

  pos1 = gavl_dictionary_get_string(m, GAVL_META_YEAR);
  if(pos1)
    return atoi(pos1);

  pos1 = gavl_dictionary_get_string(m, GAVL_META_DATE);
  
  if(!pos1)
    return 0;

  while(1)
    {
    /* Skip nondigits */
    
    while(!isdigit(*pos1) && (*pos1 != '\0'))
      pos1++;
    if(*pos1 == '\0')
      return 0;

    /* Check if we have a 4 digit number */
    result = check_year(pos1);
    if(result)
      return result;

    /* Skip digits */

    while(isdigit(*pos1) && (*pos1 != '\0'))
      pos1++;
    if(*pos1 == '\0')
      return 0;
    }
  return 0;
  }

/*
 *  %p:    Artist
 *  %a:    Album
 *  %g:    Genre
 *  %t:    Track name
 *  %<d>n: Track number (d = number of digits, 1-9)
 *  %y:    Year
 *  %c:    Comment
 */

char * bg_create_track_name(const gavl_dictionary_t * metadata,
                            const char * format)
  {
  int tag_i;
  char * buf;
  const char * end;
  const char * f;
  const char * tag;

  char * tmp_string;
  
  char * ret = NULL;
  char track_format[5];
  f = format;

  while(*f != '\0')
    {
    end = f;
    while((*end != '%') && (*end != '\0'))
      end++;
    if(end != f)
      ret = gavl_strncat(ret, f, end);

    if(*end == '%')
      {
      end++;

      /* %p:    Artist */
      if(*end == 'p')
        {
        end++;
        tmp_string = gavl_metadata_join_arr(metadata,
                                            GAVL_META_ARTIST, ", ");
        if(tmp_string)
          {
          ret = gavl_strcat(ret, tmp_string);
          free(tmp_string);
          }
        else
          goto fail;
        }
      /* %a:    Album */
      else if(*end == 'a')
        {
        end++;
        tag = gavl_dictionary_get_string(metadata, GAVL_META_ALBUM);
        if(tag)
          ret = gavl_strcat(ret, tag);
        else
          goto fail;
        }
      /* %g:    Genre */
      else if(*end == 'g')
        {
        end++;
        tmp_string = gavl_metadata_join_arr(metadata,
                                            GAVL_META_GENRE, ", ");
        if(tmp_string)
          {
          ret = gavl_strcat(ret, tmp_string);
          free(tmp_string);
          }
        else
          goto fail;
        }
      /* %t:    Track name */
      else if(*end == 't')
        {
        end++;
        tag = gavl_dictionary_get_string(metadata, GAVL_META_TITLE);
        if(tag)
          ret = gavl_strcat(ret, tag);
        else
          goto fail;
        }
      /* %c:    Comment */
      else if(*end == 'c')
        {
        end++;
        tag = gavl_dictionary_get_string(metadata, GAVL_META_COMMENT);
        if(tag)
          ret = gavl_strcat(ret, tag);
        else
          goto fail;
        }
      /* %y:    Year */
      else if(*end == 'y')
        {
        end++;
        tag_i = bg_metadata_get_year(metadata);
        if(tag_i > 0)
          {
          buf = bg_sprintf("%d", tag_i);
          ret = gavl_strcat(ret, buf);
          free(buf);
          }
        else
          goto fail;
        }
      /* %<d>n: Track number (d = number of digits, 1-9) */
      else if(isdigit(*end) && end[1] == 'n')
        {
        if(gavl_dictionary_get_int(metadata, GAVL_META_TRACKNUMBER, &tag_i))
          {
          track_format[0] = '%';
          track_format[1] = '0';
          track_format[2] = *end;
          track_format[3] = 'd';
          track_format[4] = '\0';
          
          buf = bg_sprintf(track_format, tag_i);
          ret = gavl_strcat(ret, buf);
          free(buf);
          end+=2;
          }
        else
          goto fail;
        }
      else
        {
        ret = gavl_strcat(ret, "%");
        end++;
        }
      f = end;
      }
    }
  return ret;
  fail:
  if(ret)
    free(ret);
  return NULL;
  }


void bg_metadata_date_now(gavl_dictionary_t * m, const char * key)
  {
  struct tm tm;
  time_t t = time(NULL);
  localtime_r(&t, &tm);
  
  tm.tm_mon++;
  tm.tm_year+=1900;
  gavl_dictionary_set_date_time(m, key,
                              tm.tm_year,
                              tm.tm_mon,
                              tm.tm_mday,
                              tm.tm_hour,
                              tm.tm_min,
                              tm.tm_sec);
  }

char * bg_metadata_bitrate_string(const gavl_dictionary_t * m, const char * key)
  {
  int val;
  if(!gavl_dictionary_get_string(m, key))
    return NULL;
  
  gavl_dictionary_get_int(m, key, &val);
  if(val == GAVL_BITRATE_VBR)
    return gavl_strdup(TR("VBR"));
  else if(val == GAVL_BITRATE_LOSSLESS)
    return gavl_strdup(TR("Lossless"));
  else if(val > 0)
    return bg_sprintf("%.1f kbps", ((double)val / 1000.0));
  
  return NULL;
  }
  
