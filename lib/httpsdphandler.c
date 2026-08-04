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

#include <string.h>

#include <gmerlin/httpserver.h>
#include <gmerlin/mdb.h>
#include <gmerlin/upnp/upnputils.h>

#include <gavl/utils.h>
#include <gavl/http.h>

static int handle_sdp(bg_http_connection_t * conn, void * data)
  {
  const char * end;
  char * sdp_enc;
  gavl_buffer_t sdp_dec;

  bg_http_server_t * srv = data;

  gavl_buffer_init(&sdp_dec);
  
  if(!(end = strchr(conn->path, '/')))
    end = conn->path + strlen(conn->path);

  sdp_enc = gavl_strndup(conn->path, end);

  
  
  gavl_base64_decode_data_urlsafe(sdp_enc, &sdp_dec);

  bg_http_connection_init_res(conn, "HTTP/1.1", 200, "OK");

  gavl_dictionary_set_string_nocopy(&conn->res, "Server", bg_upnp_make_server_string());
  gavl_http_header_set_date(&conn->res, "Date");
  //  gavl_dictionary_set_string(&conn->res, "Accept-Ranges", "bytes");
  gavl_dictionary_set_string(&conn->res, "Content-Type", "application/sdp");
  gavl_dictionary_set_long(&conn->res, "Content-Length", sdp_dec.len-1);

  bg_http_connection_check_keepalive(conn);

  if(!bg_http_server_write_res(srv, conn))
    goto end;

  if(!strcmp(conn->method, "HEAD"))
    goto end;

  gavl_socket_write_data(conn->fd, sdp_dec.buf, sdp_dec.len - 1);
  
  end:
  
  free(sdp_enc);
  gavl_buffer_free(&sdp_dec);
  
  
  return 1;
  }

void bg_http_server_init_sdp_handler(bg_http_server_t * srv)
  {
  bg_http_server_add_handler(srv, handle_sdp, BG_HTTP_PROTO_HTTP, "/sdp/", srv);
  }
