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

#ifndef HTTPSERVER_PRIV_H_INCLUDED
#define HTTPSERVER_PRIV_H_INCLUDED


#include <gmerlin/websocket.h>
#include <gmerlin/lpcm_handler.h>
#include <gmerlin/plug_handler.h>

/* Playlist handler */

typedef struct bg_http_playlist_handler_s bg_http_playlist_handler_t;

typedef struct bg_backend_proxy_s bg_backend_proxy_t;

typedef struct
  {
  bg_http_handler_t func;
  int protocols;
  char * path;
  void * data;
  } http_handler_t;

typedef struct client_thread_s
  {
  bg_http_connection_t conn;

  void * priv;
  pthread_t th;

  void (*cleanup)(void * priv);
  void (*thread_func)(bg_http_connection_t * conn, void * priv);

  int stopped;
  pthread_mutex_t stopped_mutex;
  } client_thread_t;

#define NUM_HEADERS 16

typedef struct
  {
  char * uri;
  int64_t offset;
  gavl_buffer_t buf;
  } header_t;

struct bg_http_server_s
  {
  bg_http_connection_t req; // Used for all requests

  /* Config stuff */
  int max_ka_sockets;
  int port;
  char * bind_addr;
  
  /* */

  bg_http_keepalive_t * ka;

  gavl_socket_address_t * addr;
  gavl_socket_address_t * remote_addr;

  char * root_url;
  char * root_file;
  
  int fd;

  http_handler_t * handlers;
  int num_handlers;
  int handlers_alloc;
  pthread_mutex_t handlers_mutex;
  
  const char * server_string;

  gavl_timer_t * timer;
  bg_parameter_info_t * params;

  bg_media_dirs_t * dirs;

  /* Client threads */
  
  client_thread_t ** threads;
  int threads_alloc;
  int num_threads;

  pthread_mutex_t threads_mutex;
  bg_mdb_t * mdb;

  header_t headers[NUM_HEADERS];
  int num_headers;

  bg_http_playlist_handler_t * playlist_handler;
  
  //  bg_backend_proxy_t ** backend_proxies;
  //  int num_backend_proxies;
  //  int backend_proxies_alloc;

  bg_lpcm_handler_t * lpcmhandler;
  bg_plug_handler_t * plughandler;
  gavl_array_t static_dirs;

  
  };

//int bg_http_playlist_handler_ping(bg_http_playlist_handler_t * h);
void bg_http_server_init_playlist_handler(bg_http_server_t * srv);

void bg_http_playlist_handler_destroy(bg_http_playlist_handler_t * h);

void bg_http_server_init_mediafile_handler(bg_http_server_t * s);

void bg_http_server_free_header(header_t *);


#endif // HTTPSERVER_PRIV_H_INCLUDED
