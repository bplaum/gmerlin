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

#include <gmerlin/frontend.h>
#include <gmerlin/websocket.h>
#include <gmerlin/utils.h>
#include <gmerlin/state.h>
#include <gmerlin/application.h>
#include <gmerlin/resourcemanager.h>

#include <gavl/log.h>
#define LOG_DOMAIN "frontend_gmerlin"

typedef struct
  {
  bg_websocket_context_t * ws;

  } frontend_priv_t;

int bg_frontend_gmerlin_ping(void * data)
  {
  int ret = 0;
  frontend_priv_t * p = data;
  ret += bg_websocket_context_iteration(p->ws);
  return ret;
  }

void * bg_frontend_gmerlin_create()
  {
  frontend_priv_t * p;
  p = calloc(1, sizeof(*p));
  return p;
  }

void bg_frontend_gmerlin_destroy(void * priv)
  {
  frontend_priv_t * p = priv;

  if(p->ws)
    bg_websocket_context_destroy(p->ws);
  
  
  free(p);
  }


static int frontend_gmerlin_open(void * data, bg_controllable_t * ctrl, const char * klass)
  {
  gavl_dictionary_t local_dev;
    
  const char * uri_scheme = NULL;
  const char * server_label = NULL;
  char * uri;
  const char * root_uri;
  bg_http_server_t * srv;
  frontend_priv_t * p = data;
  
  if(!(srv = bg_http_server_get()))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "No http server present");
    return 0;
    }

  p->ws = bg_websocket_context_create(klass, NULL, ctrl);

  /* Announce device */
  
  if(!strcmp(klass, GAVL_META_CLASS_BACKEND_RENDERER))
    uri_scheme = BG_BACKEND_URI_SCHEME_GMERLIN_RENDERER;
  else if(!strcmp(klass, GAVL_META_CLASS_BACKEND_MDB))
    uri_scheme = BG_BACKEND_URI_SCHEME_GMERLIN_MDB;
    
  if(!uri_scheme)
    return 1;
    
  root_uri = bg_http_server_get_root_url(srv);
  uri = gavl_sprintf("%s%s/ws/%s", uri_scheme, root_uri + 4 /* ://..." */, klass);
    
  /* Create local device */

  if(!(server_label = bg_app_get_label()))
    {
    free(uri);
    return 0;
    }
    
  gavl_dictionary_init(&local_dev);
  gavl_dictionary_set_string(&local_dev, GAVL_META_URI, uri);
  gavl_dictionary_set_string(&local_dev, GAVL_META_LABEL, server_label);
  gavl_dictionary_set_string(&local_dev, GAVL_META_CLASS, klass);
  
  bg_resourcemanager_publish(uri, &local_dev);
    
  gavl_dictionary_free(&local_dev);
  
  free(uri);
  return 1;
  }

int bg_frontend_gmerlin_open_mdb(void * data, bg_controllable_t * ctrl)
  {
  return frontend_gmerlin_open(data, ctrl, GAVL_META_CLASS_BACKEND_MDB);
  }

int bg_frontend_gmerlin_open_renderer(void * data, bg_controllable_t * ctrl)
  {
  return frontend_gmerlin_open(data, ctrl, GAVL_META_CLASS_BACKEND_RENDERER);
  }

