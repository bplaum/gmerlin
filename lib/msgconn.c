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

#include <config.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <uuid/uuid.h>

#include <gavl/gavlsocket.h>

#include <gmerlin/parameter.h>
#include <gmerlin/msgqueue.h>
#include <gmerlin/state.h>

#include <gmerlin/translation.h>
#include <gmerlin/log.h>
#define LOG_DOMAIN "msgconn"

#include <msgconn.h>



/* */

void bg_msg_add_function_tag(gavl_msg_t * msg)
  {
  char uuid_str[37];
  uuid_t uuid;

  if(gavl_dictionary_get(&msg->header, BG_FUNCTION_TAG))
    return;
  
  uuid_generate(uuid);
  uuid_unparse(uuid, uuid_str);
  gavl_dictionary_set_string(&msg->header, BG_FUNCTION_TAG, uuid_str);
  }

gavl_dictionary_t * 
bg_function_push(gavl_array_t * arr, gavl_msg_t * msg)
  {
  gavl_value_t val;
  
  
  gavl_dictionary_t * ret;

  gavl_dictionary_t * dict;
  
  bg_msg_add_function_tag(msg);
  
  gavl_value_init(&val);
  dict = gavl_value_set_dictionary(&val);
  gavl_dictionary_set_string(dict, BG_FUNCTION_TAG, gavl_dictionary_get_string(&msg->header, BG_FUNCTION_TAG) );
  ret = gavl_dictionary_get_dictionary_create(dict, "data");
  gavl_array_splice_val_nocopy(arr, -1, 0, &val);
  return ret;
  }

gavl_dictionary_t *
bg_function_get(gavl_array_t * arr, const gavl_msg_t * msg, int * idxp)
  {
  int i;
  gavl_dictionary_t * func;
  const char * ft;
  const char * functag = gavl_dictionary_get_string(&msg->header, BG_FUNCTION_TAG);
  if(!functag)
    return NULL;
  
  for(i = 0; i < arr->num_entries; i++)
    {
    if((func = gavl_value_get_dictionary_nc(&arr->entries[i])))
      {
      if(!(ft = gavl_dictionary_get_string(func, BG_FUNCTION_TAG)) ||
         strcmp(ft, functag))
        continue;
      
      if(idxp)
        *idxp = i;
      
      return gavl_dictionary_get_dictionary_nc(func, "data");
      }
    }

  if(idxp)
    *idxp = -1;
  return NULL;
  }
