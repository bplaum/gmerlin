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


#include <config.h>

#include <gmerlin/utils.h>
#include <gmerlin/mdb.h>

#include <gmerlin/httpserver.h>
#include <gmerlin/plug_handler.h>
#include <gmerlin/upnp/upnputils.h>
// #include <upnp/didl.h>

#include <gmerlin/translation.h>
#include <gmerlin/log.h>
#include <gmerlin/bgplug.h>

#define LOG_DOMAIN "lpcmhandler"

#include <httpserver_priv.h>

#define PLUG_PATH "/plug"

// http://host:port/plug<id>?track=2&...

struct bg_plug_handler_s
  {
  bg_http_server_t * srv;
  };

static void thread_func_plug(bg_http_connection_t * conn, void * priv)
  {
  int result = 0;
  char * id = NULL;
  gavl_dictionary_t track;
  bg_plug_handler_t * h = priv;
  
  int uri_idx = 0;

  const gavl_dictionary_t * src;
  const char * location;
  const gavl_dictionary_t * m;

  //  bg_plug_t * in_plug;
  //  bg_plug_t * out_plug;
  
  gavl_dictionary_init(&track);
  
  /* Get object */

  //  fprintf(stderr, "Client thread\n");
  
  id = gavl_strdup(conn->path);
  gavl_url_get_vars(id, NULL);

  if(strcmp(conn->method, "GET") && strcmp(conn->method, "HEAD"))
    {
    /* Method not allowed */
    bg_http_connection_init_res(conn, BG_PLUG_PROTOCOL, 
                                405, "Method Not Allowed");
    goto fail;
    }
  
  if(!bg_mdb_browse_object_sync(bg_mdb_get_controllable(h->srv->mdb), &track, id, 10000))
    {
    bg_http_connection_init_res(conn, BG_PLUG_PROTOCOL, 404, "Not Found");
    goto fail;
    }

  if(!(m = gavl_track_get_metadata(&track)))
    {
    bg_http_connection_init_res(conn, BG_PLUG_PROTOCOL, 500, "Internal Server Error");
    goto fail;
    }
  
  //  fprintf(stderr, "Got object\n");
  //  gavl_dictionary_dump(&track, 2);
  
  if(!(src = bg_plugin_registry_get_src(bg_plugin_reg, &track, &uri_idx)) ||
     !(location = gavl_strdup(gavl_dictionary_get_string(src, GAVL_META_URI))))
    {
    bg_http_connection_init_res(conn, BG_PLUG_PROTOCOL, 404, "Not Found");
    goto fail;
    }

  /* Make command */

  
  
  /* Open Plug */
  
  
  
  /* TODO: Seek */

  
  /* Send reply */

  bg_http_connection_init_res(conn, BG_PLUG_PROTOCOL, 200, "OK");

  gavl_dictionary_set_string_nocopy(&conn->res, "Server", bg_upnp_make_server_string());
  bg_http_header_set_date(&conn->res, "Date");
  
  //  gavl_dictionary_set_string(&conn->res, "Content-Type", bg_plug_mimetype);
  
  bg_http_connection_write_res(conn);
  
  /* Send data (skipped for HEAD request) */

  if(!strcmp(conn->method, "GET"))
    {
    while(1)
      {
      
      }
    }
  
  result = 1;
  
  fail:
  
  if(!result)
    {
    /* Send response (if not already done) */
    bg_http_server_write_res(h->srv, conn);
    }
  
  
  gavl_dictionary_free(&track);
  if(id)
    free(id);
  
  bg_http_server_put_connection(h->srv, conn);
  bg_http_connection_free(conn);
  }

static int handle_http_plug(bg_http_connection_t * conn, void * data)
  {
  bg_plug_handler_t * h = data;

  if(*conn->path != '/')
    return 0;
  
  //  fprintf(stderr, "handle_http_lpcm: %s\n", conn->path);
  //  gavl_dictionary_dump(&conn->url_vars, 2);

  bg_http_server_create_client_thread(h->srv, thread_func_plug, NULL, conn, data);
  
  return 0;
  }

bg_plug_handler_t * bg_plug_handler_create(bg_http_server_t * srv)
  {
  bg_plug_handler_t * ret;
  ret = calloc(1, sizeof(*ret));
  ret->srv = srv;
  bg_http_server_add_handler(srv, handle_http_plug, BG_HTTP_PROTO_BGPLUG, PLUG_PATH, // E.g. /static/ can be NULL
                             ret);
  return ret;
  }

void bg_plug_handler_destroy(bg_plug_handler_t * h)
  {
  free(h);
  }


void bg_plug_handler_add_uris(bg_plug_handler_t * h, gavl_dictionary_t * track)
  {
  const char * id;
  const char * klass;
  const char * location;
  const char * root_uri;
  const char * pos;
  char * uri;
  
  //  gavl_dictionary_t * src;
  gavl_dictionary_t * m;

  
  m = gavl_track_get_metadata_nc(track);

  /*
     src = 
       gavl_dictionary_t *
       gavl_metadata_add_src(gavl_dictionary_t * m, const char * key,
                          const char * mimetype, const char * location)
   */
  
  if(!(klass = gavl_dictionary_get_string(m, GAVL_META_CLASS)))
    return;

  //  fprintf(stderr, "bg_plug_handler_add_uris: class: %s\n", klass);
  
  if(!gavl_metadata_get_src(m, GAVL_META_SRC, 0,
                              NULL, &location) ||
     !location)
    return;
  
  if(!gavl_string_starts_with(location, "cda://") &&
     !gavl_string_starts_with(location, "dvd://") &&
     !gavl_string_starts_with(location, "dvb://") &&
     !gavl_string_starts_with(location, "vcd://"))
    return;
  
  if(!(id = gavl_track_get_id(track)))
    return;

  
  
  if(!(root_uri = bg_http_server_get_root_url(h->srv)) ||
     !(pos = strstr(root_uri, "://")))
    return;
    
  uri      = bg_sprintf("gavf-tcp%s"PLUG_PATH"%s", pos, id);
  
  gavl_metadata_add_src(m, GAVL_META_SRC, NULL, uri);
  
  free(uri);
  
  //  fprintf(stderr, "bg_plug_handler_add_uris\n");
  //  gavl_dictionary_dump(m, 2);
  //  fprintf(stderr, "\n");
  }

