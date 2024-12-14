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



#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <langinfo.h>
#include <iconv.h>
#include <gmerlin/utils.h>

#define BYTES_INCREMENT 10

static char const * const try_charsets[] = 
  {
    "ISO8859-1",
    "UTF-8",
    NULL,
  };
  
char * bg_system_to_utf8(const char * str, int len)
  {
  int i;

  gavl_charset_converter_t * cnv;
  
  char * system_charset;
  char * ret;
  int got_error = 0;
  char * tmp_string;
    
  if(len < 0)
    len = strlen(str);

  system_charset = nl_langinfo(CODESET);
  //  if(!strcmp(system_charset, "UTF-8"))
  //    return gavl_strndup(str, str+len);
  
  tmp_string = malloc(len+1);
  memcpy(tmp_string, str, len);
  tmp_string[len] = '\0';
  

  //  system_charset = "ISO-8859-1";
  
  cd = iconv_open("UTF-8", system_charset);
  ret = do_convert(cd, tmp_string, len, &got_error);
  iconv_close(cd);

  if(got_error)
    {
    if(ret)
      free(ret);
    i = 0;
    
    while(try_charsets[i])
      {
      got_error = 0;

      cd = iconv_open("UTF-8", try_charsets[i]);
      ret = do_convert(cd, tmp_string, len, &got_error);
      iconv_close(cd);
      if(!got_error)
        {
        free(tmp_string);
        return ret;
        }
      else if(ret)
        free(ret);
      i++;
      }
    
    strncpy(tmp_string, str, len);
    tmp_string[len] = '\0';

    
    }
  
  free(tmp_string);
  return ret;
  }

#if 1

char * bg_utf8_to_system(const char * str, int len)
  {
  iconv_t cd;
  char * system_charset;
  char * ret;

  char * tmp_string;
  
  if(len < 0)
    len = strlen(str);

  system_charset = nl_langinfo(CODESET);
  if(!strcmp(system_charset, "UTF-8"))
    return gavl_strndup(str, str+len);
    
  tmp_string = malloc(len+1);
  memcpy(tmp_string, str, len);
  tmp_string[len] = '\0';


  cd = iconv_open(system_charset, "UTF-8");
  ret = do_convert(cd, tmp_string, len, NULL);
  iconv_close(cd);
  free(tmp_string);
  return ret;
  }
#endif
