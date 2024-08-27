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

#define BG_MSG_ID_UPNP_EVENT 1

typedef struct bg_upnp_event_listener_s bg_upnp_event_listener_t;

void bg_upnp_event_to_msg(gavl_msg_t * msg,
                          const char * service, const char * variable, const char * value);

void bg_upnp_event_from_msg(gavl_msg_t * msg,
                            const char ** service, const char ** variable, const char ** value);


bg_upnp_event_listener_t *
bg_upnp_event_listener_create(const char * event_url_remote,
                              const char * url_local,  // Full URL plus common path for this device
                              const char * name,       // Short service name
                              bg_msg_sink_t * dst);

void bg_upnp_event_listener_destroy(bg_upnp_event_listener_t *);

int bg_upnp_event_listener_ping(bg_upnp_event_listener_t *);

// int
//bg_upnp_event_listener_handle(bg_upnp_event_listener_t *,
//                              bg_http_connection_t * conn);

#endif
