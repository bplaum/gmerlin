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



#ifndef BG_HTTP_H_INCLUDED
#define BG_HTTP_H_INCLUDED

#include <gavl/gavl.h>
#include <gavl/metadata.h>
#include <gavl/utils.h>
#include <gavl/http.h>

#define BG_URL_VAR_CLIENT_ID "cid"

#define BG_HTTP_CACHE_AGE (60*60)

int bg_http_response_write(int socket, gavl_dictionary_t * req);

int bg_http_send_request(const char * url, int head,
                         const gavl_dictionary_t * vars, gavl_io_t ** io_p);

int bg_http_read_response(gavl_io_t * io,
                          char ** redirect,
                          gavl_dictionary_t * res);

/* Read a http request (server) */

int bg_http_request_read(int socket, gavl_dictionary_t * req, int timeout);

int bg_http_response_check_keepalive(gavl_dictionary_t * res);

/* Read a response (client) */


/* Generic utilities */

void bg_http_header_set_empty_var(gavl_dictionary_t * h, const char * name);

/* Get a file with redirection */
int bg_http_get(const char * url, gavl_buffer_t * ret, gavl_dictionary_t * dict);

char * bg_http_download(const char * url, const char * out_base);

/* Get a file with redirection and specific range*/
int bg_http_get_range(const char * url, gavl_buffer_t * ret, gavl_dictionary_t * dict,
                      int64_t offset, int64_t size);
                             
int bg_http_write_data(gavl_io_t * io, const uint8_t * data, int len, int chunked);
void bg_http_flush(gavl_io_t * io, int chunked);

/* Keepalive */

typedef struct bg_http_keepalive_s bg_http_keepalive_t;

bg_http_keepalive_t * bg_http_keepalive_create(int max_sockets);

void bg_http_keepalive_destroy(bg_http_keepalive_t * ka);

void bg_http_keepalive_push(bg_http_keepalive_t * ka, int fd,
                            gavl_time_t current_time);

int bg_http_keepalive_accept(bg_http_keepalive_t * ka,
                             gavl_time_t current_time,
                             int * idx);

/* Application wide http cache */

void bg_http_cache_init(void);
void bg_http_cache_cleanup(void);
int bg_http_cache_get(const char * uri, gavl_dictionary_t * dict);
int bg_http_cache_put(const gavl_dictionary_t * dict);

#endif // BG_HTTP_H_INCLUDED
