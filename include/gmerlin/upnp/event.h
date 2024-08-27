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


#ifndef BG_UPNP_EVENT_H_INCLUDED
#define BG_UPNP_EVENT_H_INCLUDED

#include <gavl/gavl.h>
#include <gavl/value.h>

#include <gmerlin/httpserver.h>

/* Event context (server /device side)*/

void bg_upnp_event_context_init_server(gavl_dictionary_t * dict,
                                      const char * dir);

void bg_upnp_event_context_server_set_value(gavl_dictionary_t * dict, const char * name,
                                            const char * val,
                                            gavl_time_t update_interval);

const char * bg_upnp_event_context_server_get_value(const gavl_dictionary_t * dict, const char * name);

/* Send moderate events */

int bg_upnp_event_context_server_update(gavl_dictionary_t * dict);

/* Event context (client / control side) */

int bg_upnp_event_context_init_client(gavl_dictionary_t * dict,
                                      bg_http_server_t * srv,
                                      bg_msg_sink_t * sink);

#endif // BG_UPNP_EVENT_H_INCLUDED

