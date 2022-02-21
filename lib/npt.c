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

#include <stdlib.h>
#include <ctype.h>

#include <gmerlin/utils.h>

int bg_npt_parse(const char * str, gavl_time_t * ret)
  {
  return gavl_time_parse(str, ret);
  }

char * bg_npt_to_string(gavl_time_t time)
  {
  char * ret;

  if(time == 0)
    return gavl_strdup("0");
  
  ret = calloc(GAVL_TIME_STRING_LEN_MS+1, 1);
  
  gavl_time_prettyprint_ms(time, ret);
  return ret;
  }

int bg_npt_parse_range(const char * str, gavl_time_t * start, gavl_time_t * end)
  {
  int result;
  int ret = 0;

  /* Skip space */
  while(isspace(str[ret]) && (str[ret] != '\0'))
    ret++;
  
  if((result = bg_npt_parse(str + ret, start)) <= 0) 
    return 0;

  ret += result;

  /* Skip space */
  while(isspace(str[ret]) && (str[ret] != '\0'))
    ret++;

  if(ret != '-')
    return 0;

  ret++;

  /* Skip space */
  while(isspace(str[ret]) && (str[ret] != '\0'))
    ret++;

  if((result = bg_npt_parse(str + ret, end)) <= 0) 
    *end = GAVL_TIME_UNDEFINED;
  else
    ret += result;
  
  return ret;
  }

char * bg_npt_range_to_string(gavl_time_t start, gavl_time_t end)
  {
  char * ret;
  char * start_str;
  
  start_str = bg_npt_to_string(start);
  
  if(end == GAVL_TIME_UNDEFINED)
    ret = bg_sprintf("%s-", start_str);
  else
    {
    char * end_str;
    end_str = bg_npt_to_string(end);
    ret = bg_sprintf("%s-%s", start_str, end_str);
    free(end_str);
    }
  free(start_str);
  return ret;
  }
