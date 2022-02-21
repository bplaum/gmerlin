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


#include <gavl/gavl.h>
#include <gavl/metatags.h>

#include <gmerlin/msgqueue.h>
#include <gmerlin/utils.h>
#include <gmerlin/bggavl.h>
#include <gmerlin/http.h>
#include <gmerlin/httpserver.h>
#include <gmerlin/mdb.h>


#include <httpserver_priv.h>

#define FORMAT_IDX "fmt"
#define LOCAL      "local"
#define DOWNLOAD   "download"
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
                          int local, int downloadable, int fmtidx)
  {
  char * str;
  int len;
  
  bg_http_connection_init_res(conn, "HTTP/1.1", 200, "OK");

  str = bg_tracks_to_string(dict, formats[fmtidx].format, local, downloadable);
  
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

static int handle_message(void * data, gavl_msg_t * msg)
  {
  bg_http_playlist_handler_t * h = data;
  
  switch(msg->NS)
    {
    case BG_MSG_NS_DB:
      switch(msg->ID)
        {
        case BG_RESP_DB_BROWSE_OBJECT:
        case BG_RESP_DB_BROWSE_CHILDREN:
          {
          int idx = -1;
          gavl_dictionary_t * req;
          gavl_dictionary_t * conn_dict;
          bg_http_connection_t conn;
          
          /* Find request */
          
          if(!(req = bg_function_get(&h->requests, msg, &idx)) ||
             !(conn_dict = gavl_dictionary_get_dictionary_nc(req, "conn")))
            return 1;

          memset(&conn, 0, sizeof(conn));
          bg_http_connection_from_dict_nocopy(&conn, conn_dict);
          
          if(msg->ID == BG_RESP_DB_BROWSE_OBJECT)
            {
            const gavl_dictionary_t * m;
            const char * var;
            gavl_dictionary_t dict;
            
            gavl_dictionary_init(&dict);
            
            gavl_msg_get_arg_dictionary(msg, 0, &dict);

            if(!(m = gavl_track_get_metadata(&dict)) ||
               !(var = gavl_dictionary_get_string(m, GAVL_META_MEDIA_CLASS)))
              {
              /* No usable track -> 404 */
              
              bg_http_connection_init_res(&conn, "HTTP/1.1", 404, "Not Found");
              bg_http_connection_check_keepalive(&conn);

              if(!bg_http_connection_write_res(&conn))
                bg_http_connection_free(&conn);
              
              bg_http_server_put_connection(h->srv, &conn);
              bg_http_connection_free(&conn);
              }
            
            if(!gavl_string_starts_with(var, GAVL_META_MEDIA_CLASS_CONTAINER))
              {
              /* Item -> create bogus container and generate playlist */
              int fmt = 0;
              int local = 0;
              int download = 0;
              gavl_dictionary_t * track;
              gavl_dictionary_t container;

              gavl_dictionary_init(&container);

              track = gavl_append_track(&container, NULL);
              gavl_dictionary_reset(track);
              gavl_dictionary_move(track, &dict);
              
              gavl_dictionary_get_int(req, FORMAT_IDX, &fmt);
              gavl_dictionary_get_int(req, LOCAL, &local);
              gavl_dictionary_get_int(req, DOWNLOAD, &download);
              
              send_playlist(&conn, &container, local, download, fmt);
              
              gavl_array_splice_val(&h->requests, idx, 1, NULL);
              bg_http_server_put_connection(h->srv, &conn);
              }
            else // Container -> get children
              {
              gavl_msg_t * request;
              gavl_dictionary_t * new_req;

              request = bg_msg_sink_get(h->ctrl.cmd_sink);

              bg_mdb_set_browse_children_request(request,
                                                 gavl_dictionary_get_string(req, GAVL_META_ID),
                                                 0, -1, 0);
              
              new_req = bg_function_push(&h->requests, request);

              conn_dict = gavl_dictionary_get_dictionary_create(new_req, "conn");
              bg_http_connection_to_dict_nocopy(&conn, conn_dict);
              conn.fd = -1;
              
              gavl_dictionary_set_dictionary(new_req, OBJECT, &dict);

              gavl_dictionary_set(new_req, LOCAL, gavl_dictionary_get(req, LOCAL));
              gavl_dictionary_set(new_req, FORMAT_IDX, gavl_dictionary_get(req, FORMAT_IDX));
              gavl_dictionary_set(new_req, DOWNLOAD, gavl_dictionary_get(req, DOWNLOAD));
              
              //              fprintf(stderr, "Getting children\n");
              //              gavl_dictionary_dump(new_req, 2);
              
              /* Send request message */
              bg_msg_sink_put(h->ctrl.cmd_sink, request);
              
              }

            gavl_dictionary_free(&dict);
            
            gavl_array_splice_val(&h->requests, idx, 1, NULL);
            }
          else // BG_RESP_DB_BROWSE_CHILDREN
            {
            int idx;
            int last;
            int del;
            gavl_value_t add;
            gavl_dictionary_t * dict;
            gavl_array_t * children;
            
            gavl_value_init(&add);
            
            gavl_msg_get_splice_children(msg, &last, &idx, &del, &add);

            /* Append to children */

            dict = gavl_dictionary_get_dictionary_nc(req, OBJECT);

            children = gavl_get_tracks_nc(dict);
            gavl_array_splice_array(children, -1, 0, gavl_value_get_array(&add));
            gavl_track_update_children(dict);
            
            /* Check if this is the last answer */
            if(last)
              {
              int fmt = 0;
              int local = 0;
              int download = 0;

              gavl_dictionary_get_int(req, FORMAT_IDX, &fmt);
              gavl_dictionary_get_int(req, LOCAL, &local);
              gavl_dictionary_get_int(req, DOWNLOAD, &download);
              
              // fprintf(stderr, "Got children %d %d\n", local, fmt);
              // gavl_dictionary_dump(req, 2);
              
              send_playlist(&conn, dict, local, download, fmt);
              gavl_array_splice_val(&h->requests, idx, 1, NULL);
              }
            
            gavl_array_splice_val(&h->requests, idx, 1, NULL);
            gavl_value_free(&add);
            }
          
          }
          break;
        }
      break;
    }
  return 1;
  }

int bg_http_playlist_handler_ping(bg_http_playlist_handler_t * h)
  {
  bg_msg_sink_iteration(h->ctrl.evt_sink);
  return bg_msg_sink_get_num(h->ctrl.evt_sink);
  }

static int handle_http_playlist(bg_http_connection_t * conn, void * data)
  {
  int format_idx;
  int len;
  char * id;
  char * pos;
  gavl_msg_t * msg;
  bg_http_playlist_handler_t * h = data;
  const char * var;
  
  gavl_dictionary_t * req;
  gavl_dictionary_t * conn_dict;
  
  format_idx = 0;

  while(formats[format_idx].name)
    {
    len = strlen(formats[format_idx].name);
    if(!strncmp(conn->path, formats[format_idx].name, len) &&
       (conn->path[len] == '/'))
      {
      break;
      }
    format_idx++;
    }

  if(!formats[format_idx].name)
    return 0; // 404

  id = gavl_strdup(conn->path + len);
  
  pos = strrchr(id, '/');
  *pos = '\0';
  
  /* 1. Generate request message */
  msg = bg_msg_sink_get(h->ctrl.cmd_sink);
  
  gavl_msg_set_id_ns(msg, BG_FUNC_DB_BROWSE_OBJECT, BG_MSG_NS_DB);
  gavl_dictionary_set_string_nocopy(&msg->header, GAVL_MSG_CONTEXT_ID, id);

  /* 2. Store request */

  req = bg_function_push(&h->requests, msg);

  gavl_dictionary_set_int(req, FORMAT_IDX, format_idx);

  /* TODO */
  if((var = gavl_dictionary_get_string(&conn->url_vars, LOCAL)) &&
     (atoi(var) != 0))
    gavl_dictionary_set_int(req, LOCAL, 1);
  else
    gavl_dictionary_set_int(req, LOCAL, 0);

  if((var = gavl_dictionary_get_string(&conn->url_vars, DOWNLOAD)) &&
     (atoi(var) != 0))
    gavl_dictionary_set_int(req, DOWNLOAD, 1);
  else
    gavl_dictionary_set_int(req, DOWNLOAD, 0);
  
  gavl_dictionary_set_string(req, GAVL_META_ID, id);
  
  conn_dict = gavl_dictionary_get_dictionary_create(req, "conn");
  
  bg_http_connection_to_dict_nocopy(conn, conn_dict);
  conn->fd = -1;
  
  fprintf(stderr, "Got playlist request\n");
  gavl_dictionary_dump(req, 2);
  
  /* 3. Send request message */
  bg_msg_sink_put(h->ctrl.cmd_sink, msg);
  
  return 1;
  }

void bg_http_server_init_playlist_handler(bg_http_server_t * srv)
  {
  bg_http_playlist_handler_t * priv;

  priv = calloc(1, sizeof(*priv));
  priv->srv = srv;

  srv->playlist_handler = priv;
  
  bg_control_init(&priv->ctrl, bg_msg_sink_create(handle_message, priv, 0));
  bg_controllable_connect(bg_mdb_get_controllable(srv->mdb), &priv->ctrl);
  
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
     !(klass = gavl_dictionary_get_string(m, GAVL_META_MEDIA_CLASS)) ||
     !gavl_string_starts_with(klass, "container.") ||
     !(id = gavl_dictionary_get_string(m, GAVL_META_ID)) ||
     (!(title = gavl_dictionary_get_string(m, GAVL_META_TITLE)) &&
      !(title = gavl_dictionary_get_string(m, GAVL_META_LABEL))))
    return;
  
  root_uri = bg_http_server_get_root_url(srv);
  
  if(!strcmp(klass, GAVL_META_MEDIA_CLASS_MUSICALBUM) ||
     !strcmp(klass, GAVL_META_MEDIA_CLASS_PLAYLIST) ||
     !strcmp(klass, GAVL_META_MEDIA_CLASS_TV_SEASON))
    {
    int idx = 0;
    char * uri;
    
    while(formats[idx].name)
      {
      uri = bg_sprintf("%s/container/%s%s/%s.%s",
                       root_uri, formats[idx].name, id, title, formats[idx].name);

      gavl_metadata_add_src(m, GAVL_META_SRC, formats[idx].mimetype, uri);
      free(uri);
      idx++;
      }
    }
  }
