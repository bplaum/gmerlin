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

#include <gmerlin/upnp/upnputils.h>

static int check_client_generic(const gavl_dictionary_t * m)
  {
  return 1;
  }


static int check_client_hama_dir3100(const gavl_dictionary_t * m)
  {
  const char * var;
  if((var = gavl_dictionary_get_string(m, "User-Agent")) &&
     !strcmp(var, "FSL DLNADOC/1.50 UPnP Stack/1.0")) // Probably true for 100s of different models
    return 1; 
  return 0;
  }

static int check_client_foobar2000(const gavl_dictionary_t * m)
  {
  const char * var;
  if((var = gavl_dictionary_get_string(m, "User-Agent")) &&
     strstr(var, "foobar2000"))
    return 1; 
  return 0;
  }

static const bg_upnp_client_t clients[] =
  {
    {
      .check = check_client_hama_dir3100,
      .mimetypes = (const char*[]) { "audio/mpeg",
                                     "audio/aac",
                                     "audio/aacp",
                                     "audio/flac",
                                     "image/jpeg", 
                                     NULL },
    },
    {
      .check = check_client_foobar2000,
      .mimetypes = (const char*[]) { "audio/mpeg",
                                     "audio/aac",
                                     "audio/aacp",
                                     "audio/flac",
                                     "image/jpeg", 
                                     NULL },
      .album_thumbnail_width = 600,
    },
    {
      .check = check_client_generic,
      .mimetypes = (const char*[]) { "*/*",
                                     NULL },
    },
    { /* End */ },
  };

const bg_upnp_client_t * bg_upnp_detect_client(const gavl_dictionary_t * m)
  {
  int i = 0;
  while(clients[i].check)
    {
    if(clients[i].check(m))
      return &clients[i];
    i++;
    }
  return NULL;
  }

int bg_upnp_client_supports_mimetype(const bg_upnp_client_t * cl,
                                     const char * mimetype)
  {
  int i = 0;

  if(strstr(mimetype, "gavf"))
    {
    return 0;
    }
  
  if(!strcmp(cl->mimetypes[0], "*/*"))
    return 1;
  
  while(cl->mimetypes[i])
    {
    /* Directly supported */
    if(!strcmp(cl->mimetypes[i], mimetype))
      return 1;
    i++;
    }
  return 0;
  }

const char *
bg_upnp_client_translate_mimetype(const bg_upnp_client_t * cl,
                                  const char * mimetype)
  {
  int i = 0;
  if(!cl->mt)
    return mimetype;

  while(cl->mt[i].gmerlin)
    {
    if(!strcmp(cl->mt[i].gmerlin, mimetype))
      return cl->mt[i].client;
    i++;
    }
  return mimetype;
  }
