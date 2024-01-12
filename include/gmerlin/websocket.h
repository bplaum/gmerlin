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

#ifndef BG_WEBSOCKET_H_INCLUDED
#define BG_WEBSOCKET_H_INCLUDED

/* Websocket support */

#include <pthread.h>

#include <gmerlin/parameter.h>
#include <gmerlin/bgmsg.h>
#include <gmerlin/http.h>
#include <gmerlin/httpserver.h>
#include <gmerlin/backend.h>

#define BG_REMOTE_PORT_BASE 10100


typedef struct bg_websocket_context_s bg_websocket_context_t;
typedef struct bg_websocket_connection_s bg_websocket_connection_t;

char * bg_websocket_make_path(const char * klass);

/* Server side part of the websocket implementation */

/* Each websocket context has a read-thread, which reads commands from the
   clients (i.e. the frontends) and queues them in the controllable.
   Sending events goes also via the controllable but you need to call
   bg_websocket_context_iteration() regularly to actually send the events to the clients. 
*/

/* If srv or path are NULL, you need to call bg_websocket_context_handle_request
   from your own http handler */
bg_websocket_context_t *
bg_websocket_context_create(const char * klass,
                            const char * path,
                            bg_controllable_t * ctrl);

/* Can be passed to bg_http_server_add_handler */
int bg_websocket_context_handle_request(bg_http_connection_t * c, void * data);

/* Co through all client connections and send out queued events */
int bg_websocket_context_iteration(bg_websocket_context_t * ctx);

void bg_websocket_context_destroy(bg_websocket_context_t * ctx);

int bg_websocket_context_num_clients(bg_websocket_context_t * ctx);

/* Connection (client) */

bg_websocket_connection_t *
bg_websocket_connection_create(const char * url, int timeout,
                               const char * origin);

// void
// bg_websocket_connection_start(bg_websocket_connection_t * conn);

/*
  Call this regularly. This fuction never waits for network aktivity.
*/

int
bg_websocket_connection_iteration(bg_websocket_connection_t * conn);

bg_controllable_t * 
bg_websocket_connection_get_controllable(bg_websocket_connection_t *);

void
bg_websocket_connection_destroy(bg_websocket_connection_t*);

#endif // BG_WEBSOCKET_H_INCLUDED

