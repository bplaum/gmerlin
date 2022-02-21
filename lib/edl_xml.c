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

#include <inttypes.h>
#include <string.h>

#include <gmerlin/utils.h>
#include <gmerlin/xmlutils.h>
#include <gmerlin/bggavl.h>

gavl_dictionary_t * bg_edl_load(const char * filename)
  {
  gavl_dictionary_t * ret = calloc(1, sizeof(*ret));
  
  if(!bg_dictionary_load_xml(ret, filename, "EDL"))
    {
    gavl_dictionary_free(ret);
    free(ret);
    return NULL;
    }
  return ret;
  }

void bg_edl_save(const gavl_dictionary_t * edl, const char * filename)
  {
  bg_dictionary_save_xml(edl, filename, "EDL");
  }
