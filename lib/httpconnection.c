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




#include <unistd.h>
#include <string.h>

#include <poll.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>

#include <config.h>

#include <gavl/gavl.h>
#include <gavl/metadata.h>
#include <gavl/utils.h>
#include <gavl/numptr.h>
#include <gavl/gavlsocket.h>

#include <gmerlin/translation.h>

#include <gmerlin/parameter.h>
#include <gmerlin/httpserver.h>
#include <gmerlin/http.h>
#include <gmerlin/utils.h>
#include <gmerlin/upnp/upnputils.h>


// #include <gmerlin/upnp/device.h>

#include <gmerlin/log.h>
#define LOG_DOMAIN "httpconnection"

/* Keys for connection -> dictionary */

#define SAVE_INT(member)    gavl_dictionary_set_int(dict,         #member, c->member)
#define SAVE_LONG(member)   gavl_dictionary_set_long(dict,         #member, c->member)

#define SAVE_DICT(member)   { gavl_dictionary_t * tmp = gavl_dictionary_create(); \
                              gavl_dictionary_move(tmp, &c->member); \
                              gavl_dictionary_set_dictionary_nocopy(dict, #member, tmp); \
                            }

#define SAVE_STRING(member) gavl_dictionary_set_string(dict, #member, c->member)

#define LOAD_INT(member)    gavl_dictionary_get_int(dict,  #member, &c->member)
#define LOAD_LONG(member)   gavl_dictionary_get_long(dict, #member, &c->member)
#define LOAD_DICT(member)   \
  {                         \
  gavl_dictionary_t * d = gavl_dictionary_get_dictionary_nc(dict, #member); \
  if(d) \
    gavl_dictionary_move(&c->member, d);         \
  }

void bg_http_connection_to_dict_nocopy(bg_http_connection_t * c,
                                       gavl_dictionary_t * dict)
  {
  SAVE_DICT(url_vars);
  SAVE_DICT(req);
  SAVE_DICT(res);
  SAVE_INT(protocol_i);
  SAVE_INT(flags);
  SAVE_INT(fd);
  SAVE_LONG(current_time);
  }

void bg_http_connection_from_dict_nocopy(bg_http_connection_t * c,
                                         gavl_dictionary_t * dict)
  {
  LOAD_DICT(url_vars);
  LOAD_DICT(req);
  LOAD_DICT(res);
  LOAD_INT(protocol_i);
  LOAD_INT(flags);
  LOAD_INT(fd);
  LOAD_LONG(current_time);
  
  c->method   = gavl_http_request_get_method(&c->req);
  c->path     = gavl_http_request_get_path(&c->req);
  c->protocol = gavl_http_request_get_protocol(&c->req);
  }

void bg_http_connection_free(bg_http_connection_t * req)
  {
  gavl_dictionary_free(&req->req);
  gavl_dictionary_free(&req->res);
  gavl_dictionary_free(&req->url_vars);

  if(req->fd > 0)
    {
    gavl_socket_close(req->fd);
    req->fd = -1;
    }
  bg_http_connection_init(req);
  }

void bg_http_connection_init(bg_http_connection_t * req)
  {
  memset(req, 0, sizeof(*req));
  req->fd = -1;
  }


int bg_http_connection_check_keepalive(bg_http_connection_t * c)
  {
  const char * var;
  
  if(!strcmp(c->protocol, "HTTP/1.1"))
    {
    if((var = gavl_dictionary_get_string_i(&c->req, "Connection")) &&
       !strcasecmp(var, "close"))
      return 0;
    
    c->flags |= BG_HTTP_REQ_KEEPALIVE;
    return 1;
    }
  
  if((var = gavl_dictionary_get_string_i(&c->req, "Connection")) &&
     !strcasecmp(var, "Keep-Alive"))
    {
    c->flags |= BG_HTTP_REQ_KEEPALIVE;
    return 1;
    }
  return 0;
  }

void bg_http_connection_clear_keepalive(bg_http_connection_t * c)
  {
  c->flags &= ~BG_HTTP_REQ_KEEPALIVE;
  }

int bg_http_connection_read_req(bg_http_connection_t * req, int fd, int timeout)
  {
  gavl_io_t * io;

  /* Return silently for connect() floods or closed keepalive sockets */
  if(gavl_socket_is_disconnected(fd, timeout))
    {
    //  gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Socket disconnected");
    gavl_socket_close(fd);
    return 0;
    }
  io = gavl_io_create_socket(fd, timeout, 0);

  
  
  req->fd = fd;

  if(!gavl_http_request_read(io, &req->req) ||
     !(req->method   = gavl_http_request_get_method(&req->req)) ||
     !(req->path     = gavl_http_request_get_path(&req->req)) ||
     !(req->protocol = gavl_http_request_get_protocol(&req->req)))
    {
    gavl_io_destroy(io);
    bg_http_connection_free(req);
    return 0;
    }

  gavl_io_destroy(io);
  
  if(!strncmp(req->protocol, "HTTP/", 5))
    req->protocol_i = BG_HTTP_PROTO_HTTP;
  else if(!strncmp(req->protocol, "RTSP/", 5))
    req->protocol_i = BG_HTTP_PROTO_RTSP;
#if 0
  else if(!strcmp(req->protocol, BG_PLUG_PROTOCOL))
    req->protocol_i = BG_HTTP_PROTO_BGPLUG;
#endif
  if(req->protocol_i == BG_HTTP_PROTO_RTSP)
    {
    const char * pos;
    if(!strncasecmp(req->path, "rtsp://", 7))
      {
      if((pos = strchr(req->path + 7, '/')))
        req->path = pos;
      }
    }
  
  /* Can happen */
  while(!strncmp(req->path, "//", 2))
    req->path++;

  gavl_url_get_vars_c(req->path, &req->url_vars);
  
  return 1;
  }

int bg_http_connection_write_res(bg_http_connection_t * req)
  {
  int result;
  
  if((req->flags & BG_HTTP_REQ_RES_SENT) || (req->fd <= 0))
    return 1;

  if(!(req->flags & BG_HTTP_REQ_HAS_STATUS))
    bg_http_connection_init_res(req, req->protocol, 404, "Not Found");
  
  if(req->flags & BG_HTTP_REQ_KEEPALIVE)
    gavl_dictionary_set_string(&req->res, "Connection", "keep-alive");
  else if(!(req->flags & BG_HTTP_REQ_WEBSOCKET))
    gavl_dictionary_set_string(&req->res, "Connection", "close");
  
  result = bg_http_response_write(req->fd, &req->res);
  req->flags |= BG_HTTP_REQ_RES_SENT;
  
  if(!strcmp(req->method, "HEAD"))
    result = 0;
  
  return result;
  }

void bg_http_connection_init_res(bg_http_connection_t * conn,
                                 const char * protocol,
                                 int status_i, const char * status)
  {
  gavl_http_response_init(&conn->res, protocol, status_i, status);
  conn->flags |= BG_HTTP_REQ_HAS_STATUS;
  }

int bg_http_connection_not_modified(bg_http_connection_t * conn,
                                    time_t mtime)
  {
  time_t last_mtime;
  
  /* Check for caching */
  if((last_mtime = gavl_http_header_get_time(&conn->req, "If-Modified-Since")) &&
     (mtime <= last_mtime))
    {
    bg_http_connection_init_res(conn, "HTTP/1.1", 304, "Not Modified");

    gavl_http_header_set_date(&conn->res, "Date");
    
    gavl_http_header_set_time(&conn->res, "Last-Modified", mtime);
    gavl_dictionary_set_string_nocopy(&conn->res, "Cache-Control",
                                      gavl_sprintf("max-age=%d", BG_HTTP_CACHE_AGE));
    
    bg_http_connection_check_keepalive(conn);
    return 1;
    }
  return 0;
  }

void bg_http_connection_send_file(bg_http_connection_t * conn, const char * real_file)
  {
  struct stat st;
  char * ext;
  const char * mimetype;
  const bg_upnp_client_t * cl;
  int result = 0;
  
  if(strcmp(conn->method, "GET") && strcmp(conn->method, "HEAD"))
    {
    /* Method not allowed */
    bg_http_connection_init_res(conn, "HTTP/1.1", 
                          405, "Method Not Allowed");
    goto go_on;
    }

  if(!real_file)
    {
    bg_http_connection_init_res(conn, "HTTP/1.1", 404, "Not Found");
    goto go_on;
    }
  
  if(stat(real_file, &st))
    {
    if(errno == EACCES)
      {
      bg_http_connection_init_res(conn, "HTTP/1.1", 
                            401, "Forbidden");
      goto go_on;
      }
    else
      {
      bg_http_connection_init_res(conn, "HTTP/1.1", 
                            404, "Not Found");
      goto go_on;
      }
    }

  /* Check if the file is world wide readable */
  if(!(st.st_mode & S_IROTH))
    {
    bg_http_connection_init_res(conn, "HTTP/1.1", 
                          401, "Forbidden");
    goto go_on;
    }

  /* Check if the file is regular */
  if(!(S_ISREG(st.st_mode)))
    {
    bg_http_connection_init_res(conn, "HTTP/1.1", 
                          401, "Forbidden");
    goto go_on;
    }

  if(bg_http_connection_not_modified(conn, st.st_mtime))
    goto go_on;
  
  
  bg_http_connection_init_res(conn, "HTTP/1.1", 200, "OK");

  bg_http_connection_check_keepalive(conn);
  gavl_dictionary_set_long(&conn->res, "Content-Length", st.st_size);

  gavl_http_header_set_time(&conn->res, "Last-Modified", st.st_mtime);
  gavl_dictionary_set_string_nocopy(&conn->res, "Cache-Control",
                                    gavl_sprintf("max-age=%d", BG_HTTP_CACHE_AGE));
  
  ext = strrchr(real_file, '.');
  if(ext)
    {
    ext++;

    mimetype = bg_ext_to_mimetype(ext);
    
    if(mimetype)
      {
      cl = bg_upnp_detect_client(&conn->req);
      gavl_dictionary_set_string(&conn->res, "Content-Type",
                        bg_upnp_client_translate_mimetype(cl, mimetype));
      }
    }

  result = 1;
  
  go_on:

  if(!bg_http_connection_write_res(conn))
    return;
    
  if(result && !gavl_socket_send_file(conn->fd, real_file, 0, 0))
    bg_http_connection_clear_keepalive(conn);
  
  }

