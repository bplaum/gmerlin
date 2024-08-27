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



#include <config.h>
#include <gmerlin/translation.h>

#include <gmerlin/plugin.h>
#include <gmerlin/utils.h>
#include <gmerlin/log.h>
#include <gmerlin/bggavl.h>

#define LOG_DOMAIN "i_edl"

typedef struct
  {
  gavl_dictionary_t mi;
  } edl_t;

static void * create_edl()
  {
  edl_t * ret = calloc(1, sizeof(*ret));
  return ret;
  }

static void close_edl(void * data)
  {
  edl_t * e = data;
  gavl_dictionary_reset(&e->mi);
  }

static void destroy_edl(void * data)
  {
  edl_t * e = data;
  close_edl(e);
  free(e);
  }

static int open_edl(void * data, const char * arg)
  {
  gavl_dictionary_t * edl;
  gavl_dictionary_t * edl1;

  edl_t * e = data;

  if((edl = bg_edl_load(arg)))
    {
    edl1 = gavl_edl_create(&e->mi);
    gavl_dictionary_copy(edl1, edl);
    gavl_dictionary_free(edl);
    free(edl);
    
    return 1;
    }
  return 0;
  }

static gavl_dictionary_t * get_media_info_edl(void * data)
  {
  edl_t * e = data;
  return &e->mi;
  }

const bg_input_plugin_t the_plugin =
  {
    .common =
    {
      BG_LOCALE,
      .name =            "i_edl",       /* Unique short name */
      .long_name =       TRS("Parser for gmerlin EDLs"),
      .description =     TRS("This parses the XML file and exports an EDL, which can be played with the builtin EDL decoder."),
      .type =            BG_PLUGIN_INPUT,
      .flags =           BG_PLUGIN_FILE,
      .priority =        5,
      .create =          create_edl,
      .destroy =         destroy_edl,
    },
    
    .open =              open_edl,
    .get_media_info =    get_media_info_edl,
    .close =              close_edl,
    
  };

/* Include this into all plugin modules exactly once
   to let the plugin loader obtain the API version */
BG_GET_PLUGIN_API_VERSION;
