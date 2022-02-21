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

#ifndef __BG_REMOTE_H_
#define __BG_REMOTE_H_

#include <gmerlin/parameter.h>
#include <gmerlin/msgqueue.h>
#include <gmerlin/bgsocket.h>
#include <gmerlin/http.h>
#include <gmerlin/httpserver.h>

#define BG_REMOTE_PORT_BASE 10100

/* Remote server */

/*
 * http://server:10101/player
 *
 * Open Websocket connection (read/write)
 * GET /player/ws
 * Get messages (supports Keep-Alive)
 * GET /player/json
 * Send messages (supports Keep-Alive)
 * PUT /player/json
 */

#if 0


typedef struct bg_remote_server_s bg_remote_server_t;

bg_remote_server_t * bg_remote_server_create(bg_http_server_t * server,
                                             char * path);

const bg_parameter_info_t * bg_remote_server_get_parameters(bg_remote_server_t *);

void bg_remote_server_set_parameter(void * data,
                                    const char * name,
                                    const gavl_value_t * v);

int bg_remote_server_init(bg_remote_server_t *);

bg_msg_t * bg_remote_server_get_msg(bg_remote_server_t *);
void bg_remote_server_put_msg(bg_remote_server_t *, bg_msg_t *);

bg_msg_t * bg_remote_server_lock_read(bg_remote_server_t * s);
void bg_remote_server_unlock_read(bg_remote_server_t * s);
#else
void bg_remote_server_set_cmd_sink(bg_remote_server_t * s, bg_msg_sink_t * sink);
bg_msg_sink_t * bg_remote_server_get_evt_sink(bg_remote_server_t * s);
void bg_remote_server_destroy(bg_remote_server_t *);

#endif




#endif // __BG_REMOTE_H_
