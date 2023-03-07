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

#include <string.h>

#include <gmerlin/utils.h>
#include <gmerlin/msgqueue.h>
#include <gmerlin/state.h>
#include <gmerlin/application.h>
#include <gmerlin/bggavl.h>

#include <gmerlin/backend.h>
#include <backend_priv.h>
// #include <gmerlin/httpserver.h>
#include <gmerlin/websocket.h>
#include <gavl/metatags.h>
#include <gavl/log.h>

#define LOG_DOMAIN "backend_gmerlin"

typedef struct
  {
  bg_websocket_connection_t * conn;
  
  } gmerlin_backend_t;

static void destroy_gmerlin(bg_backend_handle_t * dev)
  {
  gmerlin_backend_t * g = dev->priv;

  if(g->conn)
    {
    bg_websocket_connection_destroy(g->conn);
    }

  free(g);
  }

static int create_gmerlin(bg_backend_handle_t * dev, const char * uri_1, const char * root_uri)
  {
  int ret = 0;
  
  char * uri = 0;
  const char * pos;

  gmerlin_backend_t * g = calloc(1, sizeof(*g));

  dev->priv = g;

  if((pos = strstr(uri_1, "://")))
    uri = bg_sprintf("ws%s", pos);
  else
    uri = gavl_strdup(uri_1);
  
  if(!(g->conn = bg_websocket_connection_create(uri, 5000, NULL)))
    {
    goto fail;
    }

  dev->ctrl_ext = bg_websocket_connection_get_controllable(g->conn);
  dev->ctrl = dev->ctrl_ext;
  
  ret = 1;
  
  fail:

  if(uri)
    free(uri);
  
  return ret;
  }

static int ping_gmerlin(bg_backend_handle_t * dev)
  {
  gmerlin_backend_t * g = dev->priv;
  
  if(!bg_websocket_connection_iteration(g->conn))
    return -1;
  else
    return 0;
  }

const bg_remote_dev_backend_t bg_remote_dev_backend_gmerlin_renderer =
  {
    .name = "gmerlin media renderer",
    .uri_prefix = BG_BACKEND_URI_SCHEME_GMERLIN_RENDERER"://",
    .type = BG_BACKEND_RENDERER,
    
    .ping    = ping_gmerlin,
    .create    = create_gmerlin,
    .destroy   = destroy_gmerlin,
  };

const bg_remote_dev_backend_t bg_remote_dev_backend_gmerlin_mediaserver =
  {
    .name = "gmerlin media server",
    .uri_prefix = BG_BACKEND_URI_SCHEME_GMERLIN_MDB"://",

    .type = BG_BACKEND_MEDIASERVER,
    
    .ping    = ping_gmerlin,
    .create    = create_gmerlin,
    .destroy   = destroy_gmerlin,
  };


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
  
  if(bg_backend_is_local(addr, ret))
    return 1;
  
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
  
#if 0
  bg_msg_sink_t * sink = NULL;
  bg_controllable_t * ctrl;
  get_node_info_t ni;
  bg_backend_handle_t * h = NULL;
  gavl_time_t delay_time = GAVL_TIME_SCALE/20; // 50 ms
  int result;
  int num = 0;

  const char * addr = gavl_dictionary_get_string(ret, GAVL_META_URI);
  
  if(bg_backend_is_local(addr, ret))
    {
    return 1;
    }

  ni.dict = ret;
  ni.done = 0;
  
  if(!(h = bg_backend_handle_create(ret, NULL)))
    goto fail;
  
  ctrl = bg_backend_handle_get_controllable(h);
  
  
  sink = bg_msg_sink_create(handle_msg_get_node_info, &ni, 1);

  bg_msg_hub_connect_sink(ctrl->evt_hub, sink);

  //  bg_backend_handle_start(h);
  
  while(!ni.done)
    {
    result = bg_backend_handle_ping(h);
    
    if(ni.done)
      break;
    
    if(result)
      break;
    
    gavl_time_delay(&delay_time);
    num++;
    if(num > 100)
      break;
    }

  //  bg_backend_handle_stop(h);

  if(!ni.done)
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Couldn't get Node info for %s", addr);

  fail:

  if(h)
    bg_backend_handle_destroy(h);

  if(sink)
    bg_msg_sink_destroy(sink);
  
  return ni.done;
#endif

  

  }
