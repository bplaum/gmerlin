/*****************************************************************
 * Gmerlin - a general purpose multimedia framework and applications
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

#define BG_BACKEND_PATH   "/remotedev/"
#define BG_BACKEND_ID_LEN 32 // md5 in hex

#define BG_BACKEND_PROTOCOL       "Protocol"
#define BG_BACKEND_ID             "BackendID"

/* Integer 1 if the device belongs to ourselfes */

#define BG_BACKEND_LOCAL         "Local"

// #define BG_BACKEND_REAL_PROTOCOL "RealProtocol"

// #define BG_BACKEND_TYPE         "Type"
// #define BG_BACKEND_REAL_URI "RealURI"
#define BG_BACKEND_ROOT_URI "RootURI"

#define BG_BACKEND_URI_SCHEME_GMERLIN_RENDERER "gmerlin-renderer"
#define BG_BACKEND_URI_SCHEME_GMERLIN_MDB      "gmerlin-mdb"

#define BG_BACKEND_URI_SCHEME_UPNP_RENDERER "upnp-renderer"
#define BG_BACKEND_URI_SCHEME_UPNP_SERVER   "upnp-server"


/* The registry for backends is a dictionary with the following fields:

   GAVL_META_LABEL:  Label for display
   GAVL_META_URI:    Address
   GAVL_META_ID:     bg_get_filename_hash() of GAVL_META_URI
   BG_BACKEND_TYPE:  BG_BACKEND_MEDIASERVER or BG_BACKEND_RENDERER (see bgmsg.h)
   BG_BACKEND_LOCAL: Optional, 1 if backend is local to the process

   BG_APP_NETWORK_ICONS: Array of icon URLs

   or

   BG_APP_NETWORK_ICON: Name of an application icon
   
 */

#if 0
void bg_msg_set_backend_info(gavl_msg_t * msg,
                             uint32_t id, 
                             const gavl_dictionary_t * info);

void bg_msg_get_backend_info(gavl_msg_t * msg,
                             gavl_dictionary_t * info);
#endif


/* */
typedef struct bg_backend_handle_s bg_backend_handle_t;

bg_backend_handle_t * bg_backend_handle_create(const gavl_dictionary_t * dev,
                                               const char * url_root);

int bg_backend_needs_http(const char * uri);

/* Passed to http server */
int bg_backend_handle_handle(bg_http_connection_t * conn,
                             void * data);

void bg_backend_handle_destroy(bg_backend_handle_t *);
void bg_backend_handle_stop(bg_backend_handle_t *);

bg_controllable_t * bg_backend_handle_get_controllable(bg_backend_handle_t *);

void bg_backend_handle_start(bg_backend_handle_t * d);

int bg_backend_handle_ping(bg_backend_handle_t * d);

const gavl_dictionary_t * bg_backend_handle_get_info(bg_backend_handle_t * d);

/* */
typedef struct bg_backend_registry_s bg_backend_registry_t;


bg_backend_registry_t * bg_get_backend_registry(void);
void bg_backend_registry_rescan();


void bg_backend_registry_set_proxy_prefix(const char * proxy_prefix);

/* Returned arrays must be destroyed with bg_backend_info_destroy_array() */
gavl_array_t * bg_backend_registry_get(void);

bg_msg_hub_t * bg_backend_registry_get_evt_hub();

gavl_array_t * bg_backends_scan(gavl_time_t timeout);

/* Get the node info (gmerlin backends only) */

int bg_backend_get_node_info(gavl_dictionary_t * ret);

void bg_backend_register_local(const gavl_dictionary_t * dev);

char * bg_make_backend_id(const char * klass, char id[BG_BACKEND_ID_LEN+1]);

#endif //  BG_REMOTEDEV_H_INCLUDED

