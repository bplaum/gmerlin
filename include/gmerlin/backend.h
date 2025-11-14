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



#ifndef BG_REMOTEDEV_H_INCLUDED
#define BG_REMOTEDEV_H_INCLUDED

#include <gavl/gavlsocket.h>

#include <gmerlin/parameter.h>
#include <gmerlin/bgmsg.h>

#include <gmerlin/httpserver.h>


/* Proxy URL scheme
 *
 * http://host:8888/devproxy/<MD5 of original URL>
 */

/* Integer 1 if the device belongs to ourselfes */

#define BG_BACKEND_URI_SCHEME_GMERLIN_RENDERER "gmerlin-renderer"
#define BG_BACKEND_URI_SCHEME_GMERLIN_MDB      "gmerlin-mdb"


/* */

bg_plugin_handle_t * bg_backend_handle_create(const gavl_dictionary_t * dict);

// int bg_backend_needs_http(const char * uri);

/* Passed to http server */
//int bg_backend_handle_handle(bg_http_connection_t * conn,
//                             void * data);

// void bg_backend_handle_destroy(bg_backend_handle_t *);
// void bg_backend_handle_stop(bg_backend_handle_t *);

bg_controllable_t * bg_backend_handle_get_controllable(bg_plugin_handle_t *);
// void bg_backend_handle_start(bg_backend_handle_t * d);

int bg_backend_handle_ping(bg_plugin_handle_t * d);

/* */

/* Get the node info (gmerlin backends only) */

int bg_backend_get_node_info(gavl_dictionary_t * ret);

char * bg_make_backend_id(const char * klass);

/* Gmerlin backends */

void * bg_backend_gmerlin_create(void);
void bg_backend_gmerlin_destroy(void *);
int bg_backend_gmerlin_open(void *, const char * uri);
bg_controllable_t * bg_backend_gmerlin_get_controllable(void *);
int bg_backend_gmerlin_update(void *);

#endif //  BG_REMOTEDEV_H_INCLUDED

