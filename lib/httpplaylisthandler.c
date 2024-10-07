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


#include <gavl/gavl.h>
#include <gavl/metatags.h>

#include <gmerlin/bgmsg.h>
#include <gmerlin/utils.h>
#include <gmerlin/bggavl.h>
#include <gmerlin/http.h>
#include <gmerlin/httpserver.h>
#include <gmerlin/mdb.h>


#include <httpserver_priv.h>

#define FORMAT_IDX "fmt"
#define LOCAL      "local"
#define OBJECT     "object"

/*
 *  container/pls/<id>/<title>.pls
 *  container/m3u/<id>/<title>.m3u
 *  container/sxpf/<id>/<title>.sxpf
 *  container/gmerlin/<id>/<title>.gmerlin
 */

static const struct
  {
  const char * name; // == extension
  const char * mimetype;
  int format;
  }
formats[] =
  {
    { "pls",     "audio/x-scpls",        BG_TRACK_FORMAT_PLS },
    { "m3u",     "audio/x-mpegurl",      BG_TRACK_FORMAT_M3U },
    { "xspf",    "application/xspf+xml", BG_TRACK_FORMAT_XSPF },
    // { "gmerlin", bg_tracks_mimetype,     BG_TRACK_FORMAT_GMERLIN },
    { /* End                                   */ }
  };

struct bg_http_playlist_handler_s
  {
  bg_http_server_t * srv;
  
  bg_control_t ctrl;
  gavl_array_t requests;
  
  };

void bg_http_playlist_handler_destroy(bg_http_playlist_handler_t * h)
  {
  //  fprintf(stderr, "bg_http_playlist_handler_destroy %p\n", h->ctrl.evt_sink);
  
  bg_control_cleanup(&h->ctrl);
  gavl_array_free(&h->requests);
  free(h);
  }

static void send_playlist(bg_http_connection_t * conn,
                          const gavl_dictionary_t * dict,
                          int local, int fmtidx)
  {
  char * str;
  int len;
  
  bg_http_connection_init_res(conn, "HTTP/1.1", 200, "OK");

  str = bg_tracks_to_string(dict, formats[fmtidx].format, local);
  
  len = strlen(str);
  
  gavl_dictionary_set_int(&conn->res, "Content-Length", len);
  gavl_dictionary_set_string(&conn->res, "Content-Type", formats[fmtidx].mimetype);

  //  fprintf(stderr, "Got playlist:\n");
  //  gavl_dictionary_dump(&conn->res, 2);
  //  fprintf(stderr, "\n%s\n", str);
  
  bg_http_connection_write_res(conn);
  
  if(!strcmp(conn->method, "HEAD"))
    return;

  if(gavl_socket_write_data(conn->fd, str, len) < len)
    {
    
    }
  
  free(str);
  }

static int handle_http_playlist(bg_http_connection_t * conn, void * data)
  {
  int format_idx;
  int len;
  char * id;
  char * pos;
  bg_http_playlist_handler_t * h = data;
  const char * var;
  
  gavl_dictionary_t ret;
  int local;
  
  gavl_dictionary_init(&ret);
  
  format_idx = 0;

  while(formats[format_idx].name)
    {
    len = strlen(formats[format_idx].name);
    if(!strncmp(conn->path, formats[format_idx].name, len) &&
       (conn->path[len] == '/'))
      break;
    format_idx++;
    }

  if(!formats[format_idx].name)
    return 0; // 404

  id = gavl_strdup(conn->path + len);
  
  pos = strrchr(id, '/');
  *pos = '\0';
  
  if((var = gavl_dictionary_get_string(&conn->url_vars, LOCAL)) &&
     (atoi(var) != 0))
    local = 1;
  
  if(bg_mdb_browse_children_sync(bg_mdb_get_controllable(h->srv->mdb),
                                 &ret, id, 20000))
    send_playlist(conn, &ret, local, format_idx);    
  else
    bg_http_connection_init_res(conn, "HTTP/1.1", 500, "Internal Server Error");
  
  gavl_dictionary_free(&ret);
  
  return 1;
  }

void bg_http_server_init_playlist_handler(bg_http_server_t * srv)
  {
  bg_http_playlist_handler_t * priv;

  priv = calloc(1, sizeof(*priv));
  priv->srv = srv;

  srv->playlist_handler = priv;
  
  bg_http_server_add_handler(srv, handle_http_playlist, BG_HTTP_PROTO_HTTP, "/container/", priv);
  }

void bg_http_server_add_playlist_uris(bg_http_server_t * srv, gavl_dictionary_t * container)
  {
  const char * root_uri = NULL;
  const char * id = NULL;
  const char * klass = NULL;
  const char * title = NULL;
  gavl_dictionary_t * m = NULL;

  if(!(m = gavl_track_get_metadata_nc(container)) ||
     !(klass = gavl_dictionary_get_string(m, GAVL_META_CLASS)) ||
     !gavl_string_starts_with(klass, "container.") ||
     !(id = gavl_dictionary_get_string(m, GAVL_META_ID)) ||
     (!(title = gavl_dictionary_get_string(m, GAVL_META_TITLE)) &&
      !(title = gavl_dictionary_get_string(m, GAVL_META_LABEL))))
    return;
  
  root_uri = bg_http_server_get_root_url(srv);
  
  if(!strcmp(klass, GAVL_META_CLASS_MUSICALBUM) ||
     !strcmp(klass, GAVL_META_CLASS_PLAYLIST) ||
     !strcmp(klass, GAVL_META_CLASS_TV_SEASON))
    {
    int idx = 0;
    char * uri;
    
    while(formats[idx].name)
      {
      uri = gavl_sprintf("%s/container/%s%s/%s.%s",
                       root_uri, formats[idx].name, id, title, formats[idx].name);

      gavl_metadata_add_src(m, GAVL_META_SRC, formats[idx].mimetype, uri);
      free(uri);
      idx++;
      }
    }
  }
