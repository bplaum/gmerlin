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

#include <config.h>

#include <gavl/metatags.h>


#include <gmerlin/parameter.h>
#include <gmerlin/streaminfo.h>
#include <gmerlin/utils.h>
#include <gmerlin/translation.h>

void bg_chapter_list_set_default_names(gavl_chapter_list_t * list)
  {
  int i;
  int num;

  num = gavl_chapter_list_get_num(list);
  
  for(i = 0; i < num; i++)
    {
    gavl_dictionary_t * dict = gavl_chapter_list_get_nc(list, i);
    if(!gavl_dictionary_get_string(dict, GAVL_META_LABEL))
      gavl_dictionary_set_string(dict, GAVL_META_LABEL, bg_sprintf(TR("Chapter %d"), i+1));
    }
  }
