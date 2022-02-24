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

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <unistd.h>
#include <ctype.h>

#include <gmerlin/bgplug.h>
#include <gmerlin/subprocess.h>
#include <gmerlin/bgsocket.h>
#include <gmerlin/utils.h>
#include <gmerlin/http.h>

#include <gmerlin/translation.h>
#include <gmerlin/log.h>
#define LOG_DOMAIN "plug_io"

// #define TIMEOUT 5000

#define DUMP_HEADERS

/* stdin / stdout */

/* IO Protocols */

/*
 *  socket interface: We *always* Use a http like request. The protocol name used in URLs *must* be
 *  bgplug or bgplugerv. For Unix-domain sockets, the path *must* be "*" for http sockets
 */

/*
 * Client reads

  C->S
  GET <path> BGPLUG/1.0

  S->C
  BGPLUG/1.0 200 OK

  S->C
  <program-header>

  C->S
  <program-header (backchannel)>

  S->C AV+msg Packets
  * duplex with *
  C->S command Packets
  
  TODO: 
  Synchronization and source control schemes
  
  <socket closes>
  
*/

/*
 * Client writes

  C->S
  PUT <path> BGPLUG/1.0
  
  S->C
  BGPLUG/1.0 100 Continue
  
  C->S
  <program-header>

  S->C
  <program-header (backchannel)>

  C->S AV+msg Packets
  * duplex with *
  S->C command Packets
  
  TODO:
  Synchronization and source control schemes
  
  <socket closes>
  
*/

/* Operation via a pipe */

/*
  
  S->D Program header (with msg stream and back address)
   
  D->S Connects to back address
  D->S Sends messages
  
  Source accepts connection and reads in a separate threads, such that
  non-gmerlin applications are handled gracefully

*/

static gavf_io_t * bg_plug_io_open_socket(int fd, int * flags, int timeout);


static gavf_io_t * open_dash(int method, int * flags)
  {
  struct stat st;
  int fd;
  FILE * f;
  
  /* Stdin/stdout */
  if(method == BG_PLUG_IO_METHOD_WRITE)
    f = stdout;
  else
    f = stdin;
    
  fd = fileno(f);

  if(isatty(fd))
    {
    if(method == BG_PLUG_IO_METHOD_WRITE)
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Not writing to a TTY");
    else
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Not reading from a TTY");
    return NULL;
    }
    
  if(fstat(fd, &st))
    {
    if(method == BG_PLUG_IO_METHOD_WRITE)
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Cannot stat stdout");
    else
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Cannot stat stdin");
    return 0;
    }
  if(S_ISFIFO(st.st_mode)) /* Pipe: Use local connection */
    {
    *flags |= (BG_PLUG_IO_IS_LOCAL|BG_PLUG_IO_IS_PIPE);
    }
  else if(S_ISREG(st.st_mode))
    *flags |= BG_PLUG_IO_IS_REGULAR;

  if(method == BG_PLUG_IO_METHOD_WRITE)
    fprintf(stderr, "Opened stdout\n");
  else
    fprintf(stderr, "Opened stdin %d\n", !!(*flags & BG_PLUG_IO_IS_PIPE));
  
  return gavf_io_create_file(f, method == BG_PLUG_IO_METHOD_WRITE, 0, 0);
  }

/* TCP client */

/* Unix domain client */

/* Pipe */

static int read_pipe(void * priv, uint8_t * data, int len)
  {
  bg_subprocess_t * sp = priv;
  return bg_subprocess_read_data(sp->stdout_fd, data, len);
  }

static int write_pipe(void * priv, const uint8_t * data, int len)
  {
  bg_subprocess_t * sp = priv;
  return write(sp->stdin_fd, data, len);
  }

static void close_pipe(void * priv)
  {
  bg_subprocess_t * sp = priv;
  bg_subprocess_close(sp);
  }

static gavf_io_t * open_pipe(const char * location, int wr)
  {
  const char * pos;
  bg_subprocess_t * sp;
  
  pos = location;

  if(!wr && (*pos == '|'))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Read pipes must start with '<'");
    return NULL;
    }
  else if(wr && (*pos == '<'))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Write pipes must start with '|'");
    return NULL;
    }
  
  pos++;
  while(isspace(*pos) && (*pos != '\0'))
    pos++;

  if(*pos == '\0')
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Invalid pipe: %s", location);
    return NULL;
    }
  
  sp = bg_subprocess_create(pos,
                            wr ? 1 : 0, // Do stdin
                            wr ? 0 : 1, // Do stdout
                            0);         // Do stderr
  
  if(!sp)
    return NULL;
  
  return gavf_io_create(wr ? NULL : read_pipe,
                        wr ? write_pipe : NULL,
                        NULL, // seek
                        close_pipe,
                        NULL, // flush
                        sp);
  }

/* Socket */

/* Handshake */

/*
 * Protocol
 *
 * Client to server:
 * METHOD location protocol
 *
 * METHOD can be GET or PUT
 *
 * Server: PROTOCOL STATUS STATUS_STRING
 *
 * If the method was read: Server starts to write
 * If the method was write: Server starts to read
 *
 * Supported status codes:
 *
 * 200 OK
 * 400 Bad Request
 * 405 Method Not Allowed
 * 505 Protocol Version Not Supported
 * 503 Service Unavailable
 */

// const char * bg_plug_app_id = "bgplug-"VERSION;

int 
bg_plug_request_get_method(const gavl_dictionary_t * req, int * method)
  {
  const char * val;

  
  if(!(val = gavl_http_request_get_method(req)))
    return 0;
  
  if(!strcmp(val, "PUT"))
    {
    *method = BG_PLUG_IO_METHOD_WRITE;
    return 1;
    }
  else if(!strcmp(val, "GET"))
    {
    *method = BG_PLUG_IO_METHOD_READ;
    return 1;
    }
  else if(!strcmp(val, "HEAD"))
    {
    *method = BG_PLUG_IO_METHOD_HEAD;
    return 1;
    }
  else
    return 0;
  }

int bg_plug_io_server_handshake(gavf_io_t * io, int method, const gavl_dictionary_t * req, const char * path)
  {
  int ret = 0;
  int status = 0;
  gavl_dictionary_t res;
  int request_method;
  const char * var;
  const char * status_str = NULL;
  
  gavl_dictionary_init(&res);

#if 0
  if(!bg_http_request_read(fd, &req, timeout))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Reading request failed");
    goto fail;
    }
#endif
  
  if(!(var = gavl_http_request_get_protocol(req)) || strcmp(var, BG_PLUG_PROTOCOL))
    {
    status = BG_PLUG_IO_STATUS_400;
    status_str = "Bad Request";
    goto fail;
    }

  if(path && (!(var = gavl_http_request_get_path(req)) || strcmp(var, path)))
    {
    status = BG_PLUG_IO_STATUS_404;
    status_str = "Not Found";
    goto fail;
    }
  
  if(!bg_plug_request_get_method(req, &request_method))
    {
    // 400 Bad Request
    status = BG_PLUG_IO_STATUS_400;
    status_str = "Bad Request";
    goto fail;
    }

  else if(method == request_method)
    {
    status = BG_PLUG_IO_STATUS_405;
    status_str = "Method Not Allowed";
    goto fail;
    }
  
  if(!status)
    {
    switch(request_method)
      {
      case BG_PLUG_IO_METHOD_WRITE:  // PUT
        status = BG_PLUG_IO_STATUS_100;
        status_str = "Continue";
        ret = 1;
        break;
      case BG_PLUG_IO_METHOD_READ: // GET
      case BG_PLUG_IO_METHOD_HEAD:
        status = BG_PLUG_IO_STATUS_200;
        status_str = "OK";
        ret = 1;
        break;
      default:
        status = BG_PLUG_IO_STATUS_405;
        status_str = "Unsupported Media Type";
        break;
      }
    }

  fail:

  gavl_http_response_init(&res, BG_PLUG_PROTOCOL, status, status_str);
  
  /* Write response */
    /* Set common fields */
    //    gavl_dictionary_set_string(&res, "Server", bg_plug_app_id);
    
    //    fprintf(stderr, "Sending response:\n");
    //    gavl_dictionary_dump(&res, 0);
    
  if(!gavl_http_response_write(io, &res))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Writing response failed");
    }
  
  if(request_method == BG_PLUG_IO_METHOD_HEAD)
    ret = 0;
  
  gavl_dictionary_free(&res);
  return ret;
  }

static int client_handshake(gavf_io_t * io, int method, const char * path, int timeout)
  {
  int ret = 0;
  int status = 0;
  gavl_dictionary_t req;
  gavl_dictionary_t res;
  const char * method_string;
  const char * var;
  
  gavl_dictionary_init(&req);
  gavl_dictionary_init(&res);

  if(!path)
    path = "/";

  if(method == BG_PLUG_IO_METHOD_WRITE)
    method_string = "PUT";
  else
    method_string = "GET";
  
  gavl_http_request_init(&req, method_string, path, BG_PLUG_PROTOCOL);
  
  /* Set common fields */
  
#ifdef DUMP_HEADERS
  fprintf(stderr, "Sending request\n");
  gavl_dictionary_dump(&req, 2);
#endif
  
  if(!gavl_http_request_write(io, &req))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Writing request failed");
    goto fail;
    }
  if(!gavl_http_response_read(io, &res))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Reading response failed");
    goto fail;
    }

#ifdef DUMP_HEADERS
  fprintf(stderr, "Got response\n");
  gavl_dictionary_dump(&res, 2);
#endif

  
  if(!(status = gavl_http_response_get_status_int(&res)))
    goto fail;

  if(!(var = gavl_http_request_get_protocol(&req)) || strcmp(var, BG_PLUG_PROTOCOL))
    goto fail;
  
  if(((method == BG_PLUG_IO_METHOD_READ) && (status != BG_PLUG_IO_STATUS_200)) ||
     ((method == BG_PLUG_IO_METHOD_WRITE) && (status != BG_PLUG_IO_STATUS_100)))
    {
    const char * status_str;
    
    if(!(status_str = gavl_http_response_get_status_str(&res)))
      status_str = "??";
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Got status: %d %s", status, status_str);
    goto fail;
    }
  ret = 1;
  
  fail:
  gavl_dictionary_free(&req);
  gavl_dictionary_free(&res);
  return ret;
  }

static gavf_io_t * open_tcp(const char * location,
                            int method, int * flags, int timeout)
  {
  /* Remote TCP socket */
  char * host = NULL;
  char * path = NULL;
  gavf_io_t * ret = NULL;
  int port;
  int fd;
  gavl_socket_address_t * addr = NULL;
  
  if(!bg_url_split(location,
                   NULL,
                   NULL,
                   NULL,
                   &host,
                   &port,
                   &path))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN,
           "Invalid TCP address %s", location);
    goto fail;
    }
  
  if(port < 0)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN,
           "Port missing in address %s", location);
    goto fail;
    }

  addr = gavl_socket_address_create();
  if(!gavl_socket_address_set(addr, host, port, SOCK_STREAM))
    goto fail;
  
  fd = gavl_socket_connect_inet(addr, timeout);

  if(fd < 0)
    goto fail;

  /* Handshake */
  
  ret = bg_plug_io_open_socket(fd, flags, timeout);
  
  if(!client_handshake(ret, method, path, timeout))
    {
    gavf_io_destroy(ret);
    goto fail;
    }
  /* Return */

  
  fail:
  if(host)
    free(host);
  if(path)
    free(path);
  if(addr)
    gavl_socket_address_destroy(addr);
  return ret;
  }

static gavf_io_t * open_unix(const char * addr, int method, int * flags, int timeout)
  {
  char * path = NULL;
  
  int fd;
  gavf_io_t * ret = NULL;
  gavl_dictionary_t vars;
  gavl_dictionary_init(&vars);

  if(!bg_url_split(addr,
                   NULL,
                   NULL,
                   NULL,
                   NULL,
                   NULL,
                   &path))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Invalid Unix address %s", addr);
    goto fail;
    }
  
  bg_url_get_vars(path, &vars);
  
  fd = gavl_socket_connect_unix(path);
  
  if(fd < 0)
    goto fail;

  /* Handshake */

  ret = bg_plug_io_open_socket(fd, flags, timeout);
  
  if(!client_handshake(ret, method, NULL, timeout))
    {
    gavf_io_destroy(ret);
    goto fail;
    }
  /* Return */
  
  
  fail:
  if(path)
    free(path);
  
  gavl_dictionary_free(&vars);
  
  return ret;
  }

static gavf_io_t * open_tcpserv(const char * addr,
                                int method, int * flags, int timeout)
  {
  gavl_socket_address_t * a = NULL;
  int port;
  int server_fd, fd;
  gavl_dictionary_t req;
  
  char * host = NULL;
  gavf_io_t * ret = NULL;

  if(!bg_url_split(addr,
                   NULL,
                   NULL,
                   NULL,
                   &host,
                   &port,
                   NULL))
    {
    if(host)
      free(host);
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN,
           "Invalid TCP address %s", addr);
    goto fail;
    }

  a = gavl_socket_address_create();
  if(!gavl_socket_address_set(a, host,
                          port, SOCK_STREAM))
    goto fail;

  server_fd = gavl_listen_socket_create_inet(a, 0, 1, 0);
  
  if(server_fd < 0)
    {
    return NULL;
    }
  while(1)
    {
    gavl_dictionary_init(&req);
    
    fd = gavl_listen_socket_accept(server_fd, -1, NULL);
    
    if(fd < 0)
      break;

    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Got connection");

    ret = bg_plug_io_open_socket(fd, flags, timeout);
    
    if(!gavl_http_request_read(ret, &req))
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Reading request failed");
      gavl_dictionary_free(&req);

      gavf_io_destroy(ret);
      ret = NULL;

      continue;
      }
    
    if(bg_plug_io_server_handshake(ret, method, &req, "/"))
      {
      gavl_dictionary_free(&req);
      break;
      }
    
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Handshake failed");

    gavf_io_destroy(ret);
    ret = NULL;
    
    gavl_dictionary_free(&req);
    }
  
  gavl_listen_socket_destroy(server_fd);
  
  fail:

  if(a)
    gavl_socket_address_destroy(a);
  
  return ret;
  }

static gavf_io_t * open_unixserv(const char * addr, int method, int * flags, int timeout)
  {
  gavl_dictionary_t req;
  int server_fd, fd;
  char * name = NULL;
  gavf_io_t * ret = NULL;

  if(!bg_url_split(addr,
                   NULL,
                   NULL,
                   NULL,
                   &name,
                   NULL,
                   NULL))
    {
    if(name)
      free(name);
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN,
           "Invalid UNIX domain address address %s", addr);
    return NULL;
    }
  server_fd = gavl_listen_socket_create_unix(name, 1);
    
  if(server_fd < 0)
    {
    free(name);
    return NULL;
    }
  while(1)
    {
    gavl_dictionary_init(&req);
    
    fd = gavl_listen_socket_accept(server_fd, -1, NULL);
    
    if(fd < 0)
      break;
    
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN,
           "Got connection");

    ret = bg_plug_io_open_socket(fd, flags, timeout);
    
    if(!gavl_http_request_read(ret, &req))
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Reading request failed");
      gavl_dictionary_free(&req);

      gavf_io_destroy(ret);
      ret = NULL;
      
      continue;
      }
    
    if(bg_plug_io_server_handshake(ret, method, &req, "/"))
      {
      gavl_dictionary_free(&req);
      break;
      }

    gavf_io_destroy(ret);
    ret = NULL;
    
    
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Handshake failed");
    gavl_dictionary_free(&req);
    }
  
  gavl_listen_socket_destroy(server_fd);
  
  
  free(name);
  
  return ret;
  }

/* File */

static gavf_io_t * open_file(const char * file, int wr, int * flags)
  {
  FILE * f;
  struct stat st;
  
  if(stat(file, &st))
    {
    if(!wr)
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN,
             "Cannot stat %s: %s", file, strerror(errno));
      return NULL;
      }
    }

  if(flags)
    {
    if(S_ISFIFO(st.st_mode)) /* Pipe: Use local connection */
      *flags |= BG_PLUG_IO_IS_LOCAL | BG_PLUG_IO_IS_PIPE;
    else if(S_ISREG(st.st_mode))
      *flags |= BG_PLUG_IO_IS_REGULAR;
    }
  
  
  f = fopen(file, (wr ? "w" : "r"));
  if(!f)
    return NULL;
  return gavf_io_create_file(f, wr, !!(S_ISREG(st.st_mode)), 1);
  }

static gavf_io_t * plug_io_init_pipe(gavf_io_t * old, int * flags, int method, int timeout)
  {
  /* Pipe "handshake": The sending process sends the address of a Unix-domain socket
     in the form gavf-unixserv:///path/to/socket
  */

  char * uri = NULL;
  
  gavf_io_t * ret = NULL;
  gavl_dictionary_t req;
  gavl_dictionary_init(&req);

  //  fprintf(stderr, "plug_io_init_pipe %d\n", method);
  
  if(method == BG_PLUG_IO_METHOD_WRITE)
    {
    char * name = NULL;
    int listen_fd;
    int client_fd;
    
    listen_fd = gavl_unix_socket_create(&name, 1);
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Redirecting pipe to %s", name);

    uri = bg_sprintf(BG_PLUG_PREFIX_UNIX"%s\n", name);

    /* Write URI to stdout */
    gavf_io_write_data(old, (uint8_t*)uri, strlen(uri));
    gavf_io_flush(old);
    
    /* Accept listen socket */
    
    client_fd = gavl_listen_socket_accept(listen_fd, 5000 /* 5 sec */, NULL);
    
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Accepted backchannel connection: %d", client_fd);
    
    /* Remove socket */
    gavl_listen_socket_destroy(listen_fd);
    
    if(client_fd < 0)
      goto fail;

    ret = bg_plug_io_open_socket(client_fd, flags, timeout);
      
    if(!gavl_http_request_read(ret, &req))
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Reading request failed");
      gavf_io_destroy(ret);
      ret = NULL;
      goto fail;
      }
    
    if(!bg_plug_io_server_handshake(ret, method, &req, "/"))
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Server handshake failed in pipe redirection");
      gavf_io_destroy(ret);
      ret = NULL;
      goto fail;
      }
    
    free(name);
    }
  else // Read
    {
    int uri_alloc = 0;

    if(!gavf_io_read_line(old, &uri, &uri_alloc, 1024))
      goto fail;
    
    if(!gavl_string_starts_with(uri, BG_PLUG_PREFIX_UNIX))
      goto fail;

    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Got pipe redirection to %s", uri);
    
    ret = bg_plug_io_open_location(uri, method, flags, timeout);
    }

  fail:

  if(uri)
    free(uri);
  
  gavl_dictionary_free(&req);
  gavf_io_destroy(old);
  return ret;
  }

gavf_io_t * bg_plug_io_open_location(const char * location,
                                     int method, int * flags, int timeout)
  {
  gavf_io_t * ret = NULL;

  if(flags)
    *flags = 0;

  if(!location)
    location = "-";

  /* Support gavf://./fifo1 */
  if(gavl_string_starts_with(location, BG_PLUG_PREFIX))
    location += strlen(BG_PLUG_PREFIX);
  
  if(!strcmp(location, "-"))
    ret = open_dash(method, flags);
  else if(gavl_string_starts_with(location, BG_PLUG_PREFIX_TCP))
    ret = open_tcp(location, method, flags, timeout);
  else if(gavl_string_starts_with(location, BG_PLUG_PREFIX_UNIX))
    {
    if(flags)
      *flags |= (BG_PLUG_IO_IS_LOCAL | BG_PLUG_IO_IS_SOCKET);
    
    /* Local UNIX domain socket */
    ret = open_unix(location, method, flags, timeout);
    }
  else if(gavl_string_starts_with(location, BG_PLUG_PREFIX_TCPSERV))
    {
    ret = open_tcpserv(location, method, flags, timeout);
    }
  else if(gavl_string_starts_with(location, BG_PLUG_PREFIX_UNIXSERV))
    {
    if(flags)
      *flags |= BG_PLUG_IO_IS_LOCAL | BG_PLUG_IO_IS_SOCKET;

    ret = open_unixserv(location, method, flags, timeout);
    }
  else if((location[0] == '|') ||
          (location[0] == '<'))
    {
    /* Pipe */
    if(flags)
      *flags |= BG_PLUG_IO_IS_LOCAL | BG_PLUG_IO_IS_PIPE;
    ret = open_pipe(location, method);
    }
  else
    {
    /* Regular file */
    ret = open_file(location, method, flags);
    }

  if(*flags & BG_PLUG_IO_IS_PIPE)
    {
    *flags = 0;
    ret = plug_io_init_pipe(ret, flags, method, timeout);
    }
  
  return ret;
  }

static gavf_io_t * bg_plug_io_open_socket(int fd, int * flags, int timeout)
  {
  if(flags)
    {
    if(gavl_socket_is_local(fd))
      *flags |= BG_PLUG_IO_IS_LOCAL;
    *flags |= BG_PLUG_IO_IS_SOCKET;
    }
  
  return gavf_io_create_socket(fd, timeout, GAVF_IO_SOCKET_DO_CLOSE);
  }


