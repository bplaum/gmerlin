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
#include <gmerlin/utils.h>
#include <gmerlin/xmlutils.h>
#include <gmerlin/bggavl.h>

#include "cdaudio.h"

#define XML_ROOT "CD"

int bg_cdaudio_load(gavl_dictionary_t * mi, const char * filename)
  {
  return bg_dictionary_load_xml(mi, filename, "CD");
  }

void bg_cdaudio_save(gavl_dictionary_t * mi,
                     const char * filename)
  {
  return bg_dictionary_save_xml(mi, filename, "CD");
  }
