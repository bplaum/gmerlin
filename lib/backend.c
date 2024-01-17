/*****************************************************************
 * Gmerlin - a general purpose multimedia framework and applications
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

#include <string.h>
#include <config.h>
#include <pthread.h>
#include <unistd.h>

#include <gavl/metatags.h>

#include <gmerlin/backend.h>

#include <gmerlin/utils.h>
#include <gmerlin/bgmsg.h>
#include <gmerlin/player.h>
#include <gmerlin/playermsg.h>
#include <gmerlin/translation.h>
#include <gmerlin/log.h>
#include <gmerlin/httpserver.h>
#include <gmerlin/websocket.h>
#include <gmerlin/bggavl.h>

#define LOG_DOMAIN "backend"

#include <backend_priv.h>

bg_plugin_handle_t *
bg_backend_handle_create(const gavl_dictionary_t * dev)
  {
  bg_plugin_handle_t * ret = NULL;
  bg_backend_plugin_t * p;
  char * protocol = NULL;
  const char * uri = gavl_dictionary_get_string(dev, GAVL_META_URI);
  const bg_plugin_info_t * info;
  
  if(!uri)
    return NULL;

  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Creating backend handle for %s", uri);
  
  if(!gavl_url_split(uri, &protocol, NULL, NULL, NULL, NULL, NULL))
    goto fail;
  
  if(!(info = bg_plugin_find_by_protocol(protocol, BG_PLUGIN_BACKEND_SERVER | BG_PLUGIN_BACKEND_RENDERER)))
    goto fail;

  if(!(ret = bg_plugin_load(info)))
    goto fail;

  p = (bg_backend_plugin_t *)ret->plugin;
  if(!p->open(ret->priv, uri))
    {
    bg_plugin_unref(ret);
    ret = NULL;
    }
  
  fail:

  if(protocol)
    free(protocol);
  
  return ret;
  }

int bg_backend_handle_ping(bg_plugin_handle_t * d)
  {
  bg_backend_plugin_t * p;

  // fprintf(stderr, "bg_backend_handle_ping\n");

  p = (bg_backend_plugin_t *)d->plugin;
  if(p->update)
    return p->update(d->priv);
  else
    return 0;
  }

bg_controllable_t * bg_backend_handle_get_controllable(bg_plugin_handle_t * d)
  {
  if(d->plugin->get_controllable)
    return d->plugin->get_controllable(d->priv);
  else
    return NULL;
  }

/* Gmerlin backend */

typedef struct
  {
  bg_websocket_connection_t * conn;
  
  } gmerlin_backend_t;

void * bg_backend_gmerlin_create()
  {
  gmerlin_backend_t * priv = calloc(1, sizeof(*priv));
  return priv;
  }

void bg_backend_gmerlin_destroy(void * priv)
  {
  gmerlin_backend_t * g = priv;

  if(g->conn)
    {
    bg_websocket_connection_destroy(g->conn);
    }

  free(g);

  }

int bg_backend_gmerlin_open(void * priv, const char * uri_1)
  {
  int ret = 0;
  char * uri = NULL;
  const char * pos;
  gmerlin_backend_t * g = priv;
  
  if((pos = strstr(uri_1, "://")))
    uri = bg_sprintf("ws%s", pos);
  else
    uri = gavl_strdup(uri_1);
  
  if(!(g->conn = bg_websocket_connection_create(uri, 5000, NULL)))
    {
    goto fail;
    }

  ret = 1;
  fail:
  if(uri)
    free(uri);

  return ret;
  }

bg_controllable_t * bg_backend_gmerlin_get_controllable(void * priv)
  {
  gmerlin_backend_t * g = priv;

  if(g->conn)
    return bg_websocket_connection_get_controllable(g->conn);
  else
    return NULL;
  }

int bg_backend_gmerlin_update(void * priv)
  {
  gmerlin_backend_t * g = priv;
  bg_websocket_connection_iteration(g->conn);
  return 0;
  }

/* Obtain label and icons */
int bg_backend_get_node_info(gavl_dictionary_t * ret)
  {
  char * uri = NULL;
  const char * pos;
  int result = 0;
  json_object * obj = NULL;
  gavl_dictionary_t dict;
  const char * addr = gavl_dictionary_get_string(ret, GAVL_META_URI);
  
  gavl_dictionary_init(&dict);
  
  pos = strstr(addr, "://");
  uri = bg_sprintf("http%s/info", pos);

  // fprintf(stderr, "Getting node info for %s (info URI: %s)\n", addr, uri);

  if(!(obj = bg_json_from_url(uri, NULL)))
    goto fail;
  
  if(bg_dictionary_from_json(&dict, obj))
    {
    gavl_dictionary_merge2(ret, &dict);
    result = 1;
    }
  
  fail:

  if(obj)
    json_object_put(obj);
  
  gavl_dictionary_free(&dict);

  if(uri)
    free(uri);
  return result;
  

  }
