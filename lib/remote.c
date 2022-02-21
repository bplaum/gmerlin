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

#include <config.h>
#include <gmerlin/translation.h>

#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#else
#include <sys/time.h>
#include <sys/types.h>
#endif

#include <unistd.h>
#include <string.h>

#include <gmerlin/remote.h>
#include <gmerlin/bgsocket.h>
#include <gmerlin/utils.h>
#include <gmerlin/log.h>

#include <gmerlin/websocket.h>


/* For INADDR_ Macros */

#include <netinet/in.h>

#define LOG_DOMAIN_SERVER "remote.server"
#define LOG_DOMAIN_CLIENT "remote.client"

/*
 *  Server
 */

struct bg_remote_server_s
  {
  bg_msg_t * msg;
  bg_websocket_context_t * ws;
  bg_msg_queue_t * queue;
  bg_http_server_t * srv;
  };

static void websocket_callback(bg_msg_t * msg, void * data)
  {
  bg_msg_t * msg1;
  bg_remote_server_t * s = data;

  msg1 = bg_msg_queue_lock_write(s->queue);
  bg_msg_copy(msg1, msg);
  bg_msg_queue_unlock_write(s->queue);
  }

bg_remote_server_t * bg_remote_server_create(bg_http_server_t * server,
                                             char * path)
  {
  bg_remote_server_t * ret;
  
  ret = calloc(1, sizeof(*ret));
  
  ret->srv = server;
  ret->ws = bg_websocket_context_create(websocket_callback, ret, NULL, NULL);

  bg_http_server_add_handler(server, bg_websocket_handle_request,
                             BG_HTTP_PROTO_HTTP, path, ret->ws);
  ret->msg = bg_msg_create();

  ret->queue = bg_msg_queue_create();
  return ret;
  }

int bg_remote_server_init(bg_remote_server_t * s)
  {
  return 1;
  }

void bg_remote_server_put_msg(bg_remote_server_t * s, bg_msg_t * m)
  {
  bg_websocket_send_message(s->ws, m);
  }

bg_msg_t * bg_remote_server_lock_read(bg_remote_server_t * s)
  {
  return bg_msg_queue_try_lock_read(s->queue);
  }

void bg_remote_server_unlock_read(bg_remote_server_t * s)
  {
  bg_msg_queue_unlock_read(s->queue);
  }

void bg_remote_server_destroy(bg_remote_server_t * s)
  {
  if(s->msg)
    bg_msg_destroy(s->msg);

  bg_msg_queue_destroy(s->queue);
  
  free(s);
  }

