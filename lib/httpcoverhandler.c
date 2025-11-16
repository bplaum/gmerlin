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

#include <errno.h>
#include <string.h>
#include <unistd.h>


#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>


#include <config.h>
#include <gavl/gavl.h>
#include <gavl/metatags.h>
#include <gavl/numptr.h>

#include <gmerlin/utils.h>

#include <gmerlin/http.h>
#include <gmerlin/httpserver.h>
#include <gmerlin/upnp/upnputils.h>

#include <gmerlin/translation.h>
#include <gmerlin/log.h>

#include <gmerlin/mdb.h>

#include <gmerlin/upnp/didl.h>

#include <httpserver_priv.h>

#define LOG_DOMAIN "httpcoverhandler"

#define THREAD_THRESHOLD (1024*1024) // 1 M

typedef struct
  {
  char * filename;
  int64_t off;
  int64_t len;
  bg_http_server_t * s;
  
  } media_handler_t;

#if 0
static header_t * header_get(bg_http_server_t * srv, const gavl_dictionary_t * m, char * filename);

static void thread_func_media(bg_http_connection_t * conn, void * priv)
  {
  int64_t off;
  int64_t len;
  
  media_handler_t * handler = priv;

  //  fprintf(stderr, "bg_socket_send_file %s\n", handler->filename);

  /* Send header */
  
  off = handler->off;
  
  /* Send file */

  off = handler->off - handler->h.buf.len + handler->h.offset;
  len = handler->len;

  if(len > 0)
    {
    if(!gavl_socket_send_file(conn->fd, handler->filename, off, len))
      bg_http_connection_clear_keepalive(conn);
    }
  
  bg_http_server_put_connection(handler->s, conn);

  bg_http_connection_free(conn);
  
  //  fprintf(stderr, "gavl_socket_send_file done\n");
  }

static void free_data(media_handler_t * data)
  {
  free(data->filename);
  }

static void cleanup(void * data)
  {
  free_data(data);
  free(data);
  }
#endif

static int handle_http_cover(bg_http_connection_t * conn, void * data)
  {
  int result = 0;
  char * local_path = NULL;
  char * local_path_enc = NULL;
  const char * mimetype;
  const char * ext;
  bg_http_server_t * s = data;

  const char * range;
  
  int64_t start_byte = 0;
  int64_t end_byte = 0;
  struct stat st;
  gavl_dictionary_t dict;
  
  int64_t off = 0;
  int64_t len = 0;

  gavl_buffer_t buf;

  char * path = gavl_sprintf(BG_HTTP_MEDIA_PATH"%s", conn->path);
  
  if(!(local_path_enc = bg_media_dirs_http_to_local(s->dirs, path)))
    {
    free(path);
    return 0; // Not our business
    }

  free(path);
  
  //  fprintf(stderr, "Got media path: %s\n", local_path);
  
  local_path = bg_uri_to_string(local_path_enc, -1);

  /* Reject invalid methods */

  if(strcmp(conn->method, "GET") && strcmp(conn->method, "HEAD"))
    {
    /* Method not allowed */
    bg_http_connection_init_res(conn, "HTTP/1.1", 
                                405, "Method Not Allowed");
    goto go_on;
    }

  gavl_buffer_init(&buf);
  gavl_dictionary_init(&dict);

  if(stat(local_path, &st))
    {
    if(errno == EACCES)
      bg_http_connection_init_res(conn, "HTTP/1.1", 401, "Forbidden");
    else if(errno == ENOENT)
      bg_http_connection_init_res(conn, "HTTP/1.1", 404, "Not Found");
    else
      bg_http_connection_init_res(conn, "HTTP/1.1", 400, "Bad Request");
    goto go_on;
    }
    
  if(!(st.st_mode & S_IROTH))
    {
    bg_http_connection_init_res(conn, "HTTP/1.1", 401, "Forbidden");
    goto go_on;
    }
  
  if(!bg_plugin_registry_extract_embedded_cover(local_path, &buf, &dict))
    {
    bg_http_connection_init_res(conn, "HTTP/1.1", 404, "Not Found");
    goto go_on;
    }

  if(!(mimetype = gavl_dictionary_get_string(&dict, GAVL_META_MIMETYPE)) ||
     !(ext = bg_mimetype_to_ext(mimetype)))
    {
    bg_http_connection_init_res(conn, "HTTP/1.1", 401, "Forbidden");
    goto go_on;
    }
  
  /* Check for bytes range */
  
  if((range = gavl_dictionary_get_string_i(&conn->req, "Range")))
    {
    if(!gavl_string_starts_with(range, "bytes="))
      {
      bg_http_connection_init_res(conn, "HTTP/1.1", 400, "Bad Request");
      goto go_on;
      }

    range += 6;

    if(sscanf(range, "%"PRId64"-%"PRId64, &start_byte, &end_byte) < 2)
      {
      if(sscanf(range, "%"PRId64"-", &start_byte) == 1)
        end_byte = buf.len - 1;
      else
        {
        bg_http_connection_init_res(conn, "HTTP/1.1", 400, "Bad Request");
        goto go_on;
        }
      }

    if((start_byte < 0) ||
       (end_byte < 0) ||
       (end_byte < start_byte) ||
       (end_byte > buf.len - 1))
      {
      bg_http_connection_init_res(conn, "HTTP/1.1", 
                                  416, "Requested range not satisfiable");
      goto go_on;
      }
    off = start_byte;
    len = end_byte - start_byte + 1;
    }
  else
    {
    off = 0;
    len = buf.len;
    }
  
  /* Send response header */

  if(range)
    bg_http_connection_init_res(conn, "HTTP/1.1", 206, "Partial Content");
  else
    bg_http_connection_init_res(conn, "HTTP/1.1", 200, "OK");

  /* dlna content features */
  
  gavl_dictionary_set_string_nocopy(&conn->res, "Server", bg_upnp_make_server_string());
  bg_http_header_set_date(&conn->res, "Date");
  gavl_dictionary_set_string(&conn->res, "Accept-Ranges", "bytes");
  gavl_dictionary_set_string(&conn->res, "Content-Type", mimetype);
  
  if(range)
    {
    gavl_dictionary_set_long(&conn->res, "Content-Length", end_byte - start_byte + 1);
    gavl_dictionary_set_string_nocopy(&conn->res, "Content-Range",
                                      gavl_sprintf("bytes %"PRId64"-%"PRId64"/%d", start_byte, end_byte,
                                                   buf.len));
    }
  else
    gavl_dictionary_set_long(&conn->res, "Content-Length", buf.len);

  bg_http_connection_check_keepalive(conn);
  
  //  gavl_dictionary_set_string(&conn->res, "Cache-control", "no-cache");
  
  if(!bg_http_server_write_res(s, conn))
    goto go_on;

  if(!strcmp(conn->method, "HEAD"))
    {
    result = 1;
    goto go_on; // Actually this isn't a fail condition
    }

  if(gavl_socket_write_data(conn->fd, buf.buf + off, len) < len)
    bg_http_connection_clear_keepalive(conn);
  
  result = 1;
  go_on:

  if(!result)
    bg_http_server_write_res(s, conn);
  
  if(local_path)
    free(local_path);
  
  if(local_path_enc)
    free(local_path_enc);

  gavl_buffer_free(&buf);
  gavl_dictionary_free(&dict);
  
  return 1;
  }

void bg_http_server_init_cover_handler(bg_http_server_t * s)
  {
  if(!bg_plugin_reg)
    {
    fprintf(stderr,"BUG: bg_http_server_init_cover_handler called without plugin_reg)");
    return;
    }
  
  bg_http_server_add_handler(s,handle_http_cover, BG_HTTP_PROTO_HTTP,
                             BG_HTTP_MEDIACOVER_PATH, // E.g. /static/ can be NULL
                             s);
  }

