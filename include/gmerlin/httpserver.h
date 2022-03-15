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

#ifndef BG_HTTPSERVER_H_INCLUDED
#define BG_HTTPSERVER_H_INCLUDED

#include <gavl/gavlsocket.h>

#include <gmerlin/msgqueue.h>
#include <gmerlin/parameter.h>

#include <gmerlin/pluginregistry.h>



/* Flags for the request */
#define BG_HTTP_REQ_KEEPALIVE  (1<<0) // Socket will do keepalive
#define BG_HTTP_REQ_RES_SENT   (1<<1) // Response header sent already
#define BG_HTTP_REQ_HAS_STATUS (1<<2) // Response header initialized already
#define BG_HTTP_REQ_WEBSOCKET  (1<<3) // Websocket connection

/* Protocols */
#define BG_HTTP_PROTO_HTTP    (1<<0)
#define BG_HTTP_PROTO_RTSP    (1<<1)
#define BG_HTTP_PROTO_BGPLUG  (1<<2)

/* Pre-defined paths */

#define BG_HTTP_APPICON_PATH      "/appicons/"
// #define BG_HTTP_BACKENDPROXY_PATH "/backendproxy/"


// #include <gmerlin/mdb.h>
// #include <gmerlin/remotedev.h>

typedef struct bg_http_server_s bg_http_server_t;
typedef struct bg_http_connection_s bg_http_connection_t;

struct bg_http_connection_s
  {
  const char * method;
  const char * protocol;
  const char * path;
  int protocol_i;
 
  int fd;

  gavl_dictionary_t url_vars;
  
  gavl_dictionary_t req;
  gavl_dictionary_t res;

  int flags; 

  gavl_time_t current_time;
  };

void bg_http_connection_free(bg_http_connection_t * conn);

void bg_http_connection_init(bg_http_connection_t * conn);

int bg_http_connection_read_req(bg_http_connection_t * conn, int fd,
                                int timeout);

void bg_http_connection_init_res(bg_http_connection_t * conn,
                                 const char * protocol,
                                 int status_i, const char * status);

int bg_http_connection_write_res(bg_http_connection_t * conn);

void bg_http_connection_send_static_file(bg_http_connection_t * conn);
void bg_http_connection_send_file(bg_http_connection_t * conn, const char * real_file);

int bg_http_connection_check_keepalive(bg_http_connection_t * c);
void bg_http_connection_clear_keepalive(bg_http_connection_t * c);

void bg_http_connection_to_dict_nocopy(bg_http_connection_t * c,
                                       gavl_dictionary_t * dict);

void bg_http_connection_from_dict_nocopy(bg_http_connection_t * c,
                                         gavl_dictionary_t * dict);



/* Server-side storage */

typedef struct bg_server_storage_s bg_server_storage_t;

bg_server_storage_t *
bg_server_storage_create(const char * local_path,
                         int max_clients,
                         const char ** vars);

void * bg_server_storage_get(bg_server_storage_t * s,
                             const char * client_id,
                             const char * var, int * len);

int bg_server_storage_put(bg_server_storage_t * s,
                          const char * client_id,
                          const char * var,
                          void * data, int len);

void
bg_server_storage_destroy(bg_server_storage_t * s);

int bg_server_storage_handle_http(bg_http_connection_t * conn, void * data);

/* Client thread */

typedef void (*bg_http_server_thread_func)(bg_http_connection_t * conn, void * data);
typedef void (*bg_http_server_thread_cleanup)(void * priv);

/* Server */


typedef int (*bg_http_handler_t)(bg_http_connection_t * conn, void * data);

bg_http_server_t * bg_http_server_create(void);
void bg_http_server_destroy(bg_http_server_t*);

const bg_parameter_info_t * bg_http_server_get_parameters(bg_http_server_t *);
void bg_http_server_set_parameter(void * s, const char * name, const gavl_value_t * val);
int bg_http_server_get_parameter(void * s, const char * name, gavl_value_t * val);

int bg_http_server_write_res(bg_http_server_t * s, bg_http_connection_t * req);

void bg_http_server_set_server_string(bg_http_server_t * s, const char * str);

int bg_http_server_has_path(bg_http_server_t * s, const char * path);

void bg_http_server_add_handler(bg_http_server_t * s,
                                bg_http_handler_t h,
                                int protocol,
                                const char * path, // E.g. /static/ can be NULL
                                void * data);

void bg_http_server_remove_handler(bg_http_server_t * s,
                                   const char * path, // E.g. /static/ can be NULL
                                   void * data);

int bg_http_server_start(bg_http_server_t * s);

const char * bg_http_server_get_root_url(bg_http_server_t * s);

void bg_http_server_set_default_port(bg_http_server_t * s, int port);

const gavl_socket_address_t * bg_http_server_get_address(bg_http_server_t * s);

/* 
 *  Integrate this into your main loop. Return value is zero if nothing happened.
 *  In this case the thread should sleep a short time before calling this function again.
 */

void bg_http_server_create_client_thread(bg_http_server_t * s,
                                         bg_http_server_thread_func tf,
                                         bg_http_server_thread_cleanup cf,
                                         bg_http_connection_t * conn, void * priv);

int bg_http_server_iteration(bg_http_server_t * s);

gavl_time_t bg_http_server_get_time(bg_http_server_t * s);

void bg_http_server_set_static_path(bg_http_server_t * s, const char * path);

void bg_http_server_enable_appicons(bg_http_server_t * s);

void bg_http_server_set_generate_client_ids(bg_http_server_t * s);
void bg_http_server_set_root_file(bg_http_server_t * s, const char * path);

/* Put a connection here after the request was handled. The server will
   close the socket or re-use it if the BG_HTTP_REQ_KEEPALIVE is set */

void bg_http_server_put_connection(bg_http_server_t * s, bg_http_connection_t * conn);

void bg_http_server_init_playlist_handler(bg_http_server_t * srv);
void bg_http_server_add_playlist_uris(bg_http_server_t * srv, gavl_dictionary_t * container);

/*
 *   Media directories
 *
 *   This needs to be shared between the mdb (for generating http uris) and the
 *   server (for handing media requests).
 */

typedef struct bg_media_dirs_s bg_media_dirs_t;

bg_media_dirs_t * bg_media_dirs_create();

void bg_media_dirs_destroy(bg_media_dirs_t *);

void bg_media_dirs_add_path(bg_media_dirs_t *, const char * path);
void bg_media_dirs_del_path(bg_media_dirs_t *, const char * path);
void bg_media_dirs_set_root_uri(bg_media_dirs_t *, const char * uri);

char * bg_media_dirs_local_to_http(bg_media_dirs_t *, const char * );
char * bg_media_dirs_local_to_http_uri(bg_media_dirs_t *, const char *);
char * bg_media_dirs_http_to_local(bg_media_dirs_t *, const char * );

const bg_parameter_info_t * bg_media_dirs_get_parameters(void);

int bg_media_dirs_set_parameter(void * dirs, const char * name,
                                 const gavl_value_t * val);

bg_media_dirs_t * bg_http_server_get_media_dirs(bg_http_server_t * s);
int bg_is_http_media_uri(const char * uri);



#endif // BG_HTTPSERVER_H_INCLUDED


