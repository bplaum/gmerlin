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

#include <gavl/gavl.h>
#include <gavl/metatags.h>

#include <gmerlin/http.h>
#include <gmerlin/bgsocket.h>
#include <gmerlin/utils.h>
#include <gmerlin/log.h>
#include <gmerlin/bgplug.h>

#define LOG_DOMAIN "gmerlin.http"


#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>


#define META_PROTOCOL   "$PROTOCOL"
#define META_PATH       "$PATH"
#define META_METHOD     "$METHOD"
#define META_STATUS_INT "$STATUS_INT"
#define META_STATUS_STR "$STATUS_STR"
#define META_EMPTY      "$EMPTY"

// Keepalive timeout
#define KA_TIMEOUT (10*GAVL_TIME_SCALE) // 10 Secs

int bg_http_request_write(int fd, gavl_dictionary_t * req)
  {
  int result, len = 0;
  char * line;
  
  line = gavl_http_request_to_string(req, &len);
  
  result = gavl_socket_write_data(fd, (const uint8_t*)line, len);
  free(line);
  return result;
  }

int bg_http_response_write(int fd, gavl_dictionary_t * req)
  {
  int result, len = 0;
  char * line;
  
  line = gavl_http_response_to_string(req, &len);
  
  result = gavl_socket_write_data(fd, (const uint8_t*)line, len);

  if(!result)
    {
    fprintf(stderr, "Writing response failed: %d %d %s\n", len, result, strerror(errno));
    }

  free(line);
  return result;
  }

void bg_http_header_set_empty_var(gavl_dictionary_t * h, const char * name)
  {
  gavl_dictionary_set_string(h, name, META_EMPTY);
  }

void bg_http_header_set_date(gavl_dictionary_t * h, const char * name)
  {
  char date[30];
  time_t curtime = time(NULL);
  struct tm tm;
  
  strftime(date, 30,"%a, %d %b %Y %H:%M:%S GMT", gmtime_r(&curtime, &tm));
  gavl_dictionary_set_string(h, name, date);
  }

int bg_http_response_check_keepalive(gavl_dictionary_t * res)
  {
  const char * protocol;
  const char * var;
  
  protocol = gavl_http_response_get_protocol(res);

  /* Under HTTP/1.1, connections are keep-alive by default */
  if(!strcmp(protocol, "HTTP/1.1"))
    {
    if((var = gavl_dictionary_get_string(res, "Connection")) &&
       !strcasecmp(var, "close"))
      return 0;
    else
      return 1;
    }
  else
    {
    if((var = gavl_dictionary_get_string(res, "Connection")) &&
       !strcasecmp(var, "keep-alive"))
      return 1;
    else
      return 0;
    }
  return 0;
  }


/* Asynchronous I/O */
int bg_http_read_body_start(gavf_io_t * io, const gavl_dictionary_t * res,
                            int * total_size,
                            int * chunked)
  {
  const char * var;
  
  var = gavl_dictionary_get_string_i(res, "Transfer-Encoding");
  if(var && !strcasecmp(var, "chunked"))
    {
    *chunked = 1;
    *total_size = 0;
    }
  else
    {
    *chunked = 0;
    
    if(!gavl_dictionary_get_int_i(res, "Content-Length", total_size))
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "No length given in http response");
      return 0;
      }
    }
  return 1;
  }

/* Return: >= 0: Bytes read, -1: Error (EOF) */

int bg_http_read_body_update(gavf_io_t * io,
                             gavl_buffer_t * buf,
                             int * total_size,
                             int chunked)
  {
  int result;
  int ret = -1;

  if(chunked)
    {
    int chunk_len;
    uint8_t crlf[2];

    char * length_str = NULL;
    int length_alloc = 0;
    int bytes_read = 0;
    
    /* Here we check once if we can read data. If it succeeds, we read a whole chunk */
    
    while(1)
      {
      result = 0;
      
      if(!gavf_io_can_read(io, 0))
        {
        ret = bytes_read;
        goto chunkend;
        }
      
      /* Read length */
      if(!gavf_io_read_line(io, &length_str,
                            &length_alloc, 10000) ||
         (sscanf(length_str, "%x", &chunk_len) < 1))
        {
        gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Reading chunk length failed");
        goto chunkend;
        }
      //      fprintf(stderr, "Chunk length: %d", chunk_len);
      if(chunk_len > 0)
        {
        if((buf->len) + chunk_len > 10 * 1024 * 1024) // Never download more then 10 MB
          {
          gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Length %d outside allowed range", (buf->len) + chunk_len);
          goto chunkend;
          }

        gavl_buffer_alloc(buf, buf->len + chunk_len + 1);
        
        if(chunk_len > 0)
          result = gavf_io_read_data(io, buf->buf + buf->len, chunk_len);
        
        if(result < chunk_len)
          {
          gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Reading %d bytes failed (got %d)", chunk_len, result);
          goto chunkend;
          }

        buf->len += chunk_len;
        bytes_read += chunk_len;
        }
        
      /* Read trailing \r\n */
      if((gavf_io_read_data(io, crlf, 2) < 2) ||
         (crlf[0] != '\r') ||
         (crlf[1] != '\n'))
        goto chunkend;

      if(chunk_len <= 0)
        {
        /* Signal end */
        *total_size = buf->len;
        
        //        fprintf(stderr, "Downloaded chunked http %d bytes\n", *len);
        // gavl_hexdump(ret, *len, 16);
        break;
        }
      }
    
    ret = bytes_read;
    
    chunkend:
    
    if(length_str)
      free(length_str);

    return ret;
    
    //    gavl_buffer_alloc(buf, 
    }
  else
    {
    if(!gavf_io_can_read(io, 0))
      return 0;

    if(!buf->buf)
      {
      gavl_buffer_alloc(buf, *total_size);
      }
    
    result = gavf_io_read_data_nonblock(io, buf->buf + buf->len, *total_size - buf->len);
    
    if(!result)
      {
      fprintf(stderr, "gavf_io_read_data_nonblock returned 0, wanted: %d\n", *total_size - buf->len);

      return -1;
      }
    if(result > 0)
      {
      buf->len += result;
      return result;
      }
    }
  
  return -1;
  }


int bg_http_read_body(gavf_io_t * io, const gavl_dictionary_t * res, gavl_buffer_t * buf)
  {
  char * length_str = NULL;
  int length_alloc = 0;

  int success = 0;
  const char * var;
  int chunked = 0;
  int result;
  
  
  //  gavl_dictionary_dump(res, 0);

  var = gavl_dictionary_get_string_i(res, "Transfer-Encoding");
  if(var && !strcasecmp(var, "chunked"))
    chunked = 1;
  else
    {
    if(!gavl_dictionary_get_int_i(res, "Content-Length", &buf->len))
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "No length given in http response");
      goto fail;
      }
    }
  
  if(chunked)
    {
    int chunk_len;
    uint8_t crlf[2];
    while(1)
      {
      /* Read length */
      if(!gavf_io_read_line(io, &length_str,
                            &length_alloc, 10000) ||
         (sscanf(length_str, "%x", &chunk_len) < 1))
        {
        gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Reading chunk length failed");
        goto fail;
        }
      //      fprintf(stderr, "Chunk length: %d", chunk_len);
      if(chunk_len > 0)
        {
        if((buf->len) + chunk_len > 10 * 1024 * 1024) // Never download more then 10 MB
          {
          gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Length %d outside allowed range", (buf->len) + chunk_len);
          goto fail;
          }

        gavl_buffer_alloc(buf, buf->len + chunk_len + 1);
        
        result = gavf_io_read_data(io, buf->buf + buf->len, chunk_len);
        
        if(result < chunk_len)
          {
          gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Reading %d bytes failed (got %d)", chunk_len, result);
          goto fail;
          }

        buf->len += chunk_len;
        }
        
      /* Read trailing \r\n */
      if((gavf_io_read_data(io, crlf, 2) < 2) ||
         (crlf[0] != '\r') ||
         (crlf[1] != '\n'))
        goto fail;

      if(chunk_len <= 0)
        {
        //        fprintf(stderr, "Downloaded chunked http %d bytes\n", *len);
        // gavl_hexdump(ret, *len, 16);
        break;
        }
      }
    }
  else
    {
    if((buf->len <= 0) || (buf->len > 10 * 1024 * 1024)) // Never download more then 10 MB
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Length %d outside allowed range", buf->len);
      goto fail;
      }

    gavl_buffer_alloc(buf, buf->len+1);
    
    result = gavf_io_read_data(io, buf->buf, buf->len);
    
    if(result < buf->len)
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Reading %d bytes failed (got %d)", buf->len, result);
      goto fail;
      }
    }
    
  buf->buf[buf->len] = '\0'; // Become string friendly
  // Success :)
  success = 1;

  fail:
  
  if(length_str)
    free(length_str);

  if(!success)
    {
    gavl_buffer_free(buf);
    /* Prevent double free later on */
    gavl_buffer_init(buf);
    }
  return success;
  }

static gavf_io_t * open_io(const char * protocol, const char * host, int port)
  {
  gavl_socket_address_t * addr = NULL;
  int use_tls = 0;
  int fd;
  gavf_io_t * ret = NULL;
  
  if(!strcasecmp(protocol, "https"))
    {
    if(port == -1)
      port = 443;
    
    use_tls = 1;
    }
  else if(strcasecmp(protocol, "http"))
    {
    goto fail;
    }

  if(port == -1)
    port = 80;

  addr = gavl_socket_address_create();
  
  if(!gavl_socket_address_set(addr, host, port, SOCK_STREAM))
    goto fail;
  
  
  if((fd = gavl_socket_connect_inet(addr, 5000)) < 0)
    goto fail;
  
  //  fprintf(stderr, "Request:\n");
  //  gavl_dictionary_dump(&req, 2);

  /* Wrap https */
  
  if(use_tls)
    {
    ret = gavf_io_create_tls_client(fd, host, GAVF_IO_SOCKET_DO_CLOSE);
    }
  else
    {
    ret = gavf_io_create_socket(fd, 30000, GAVF_IO_SOCKET_DO_CLOSE);
    }

  fail:
  if(addr)
    gavl_socket_address_destroy(addr);
  
  return ret;
  }

int bg_http_send_request(const char * url, 
                         int head, const gavl_dictionary_t * vars, gavf_io_t ** io_p)
  {
  gavl_dictionary_t req;

  int ret = 0;
  char * host = NULL;
  char * path = NULL;
  char * protocol = NULL;
  char * request_host = NULL;
  int port = -1;
  //  int status = -1;
  
  gavf_io_t * io = NULL;
  
  gavl_dictionary_init(&req);

  //  fprintf(stderr, "http_get: %s\n", url);
  
  if(!bg_url_split(url,
                   &protocol,
                   NULL,
                   NULL,
                   &host,
                   &port,
                   &path))
    goto fail;

  /* Keepalive socket */
  if(*io_p)
    io = *io_p;
  else
    io = open_io(protocol, host, port);

  if(!io)
    goto fail;
  
  request_host = bg_url_get_host(host, port);
  
  if(head)
    gavl_http_request_init(&req, "HEAD", path, "HTTP/1.1");
  else
    gavl_http_request_init(&req, "GET", path, "HTTP/1.1");

  gavl_dictionary_set_string(&req, "Host", request_host);
  gavl_dictionary_set_string(&req, "Connection", "Close");

  if(vars)
    gavl_dictionary_merge2(&req, vars);
  
  if(!gavl_http_request_write(io, &req))
    goto fail;

#if 0  
  if(!gavl_http_response_read(io, res))
    goto fail;

  
  
  //  fprintf(stderr, "Response:\n");
  //  gavl_dictionary_dump(res, 2);
  
  status = gavl_http_response_get_status_int(res);

  if(status == 200) // Success
    {
    if(!gavl_dictionary_get_string_i(res, "Content-Type")) // No mimetype
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "No mimetype given for %s", url);
      goto fail;
      }
    /* Read body */
    if(io_p)
      {
      *io_p = io;
      io = NULL;
      }
    ret = 1;
    }
  else if((status >= 300) && (status < 400)) // Redirection
    {
    *redirect = gavl_strdup(gavl_dictionary_get_string_i(res, "Location"));
    if(!(*redirect))
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN,
             "No location for redirection for %s", url);
      goto fail;
      }
    }
  else // Failure
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Downloading %s failed: %d %s", url, 
           gavl_http_response_get_status_int(res),
           gavl_http_response_get_status_str(res));
    }

  fail:

  if(fd >= 0)
    gavl_socket_close(fd);
  
  if(host)
    free(host);
  if(request_host)
    free(request_host);
  if(path)
    free(path);
  if(protocol)
    free(protocol);

  if(io)
    gavf_io_destroy(io);
  
  gavl_dictionary_free(&req);
  
  if(addr)
    gavl_socket_address_destroy(addr);

  return ret;
#else

  if(io_p)
    {
    *io_p = io;
    io = NULL;
    }
  
  ret = 1;
  
  fail:

  if(io)
    gavf_io_destroy(io);
  
  return ret;
  
#endif

  
  }

int bg_http_read_response(gavf_io_t * io,
                          char ** redirect,
                          gavl_dictionary_t * res)
  {
  int ret = 0;
  int status;
  

  if(!gavl_http_response_read(io, res))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Reading response failed");
    goto fail;
    }
  
  status = gavl_http_response_get_status_int(res);

  if(status == 200) // Success
    {
    if(!gavl_dictionary_get_string_i(res, "Content-Type")) // No mimetype
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "No mimetype given");
      goto fail;
      }
    ret = 1;
    }
  else if((status >= 300) && (status < 400)) // Redirection
    {
    *redirect = gavl_strdup(gavl_dictionary_get_string_i(res, "Location"));
    if(!(*redirect))
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN,
             "No location for redirection");
      goto fail;
      }
    ret = 1;
    }
  else // Failure
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Downloading failed: %d %s",  
           gavl_http_response_get_status_int(res),
           gavl_http_response_get_status_str(res));
    }

  
  fail:
  

  return ret;
  
  }
                      


/* Simple function implementing GET for (small) files */ 

static int http_get(const char * url,
                    gavl_dictionary_t * res, char ** redirect,
                    int head, gavl_buffer_t * buf, const gavl_dictionary_t * vars)
  {

  int ret = 0;

  
  gavf_io_t * io = NULL;

  gavl_log(GAVL_LOG_DEBUG, LOG_DOMAIN, "Downloading %s", url);
  
  if(!bg_http_send_request(url, 0, vars, &io))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Downloading %s failed: Coudln't send request", url);
    goto fail;
    }

  if(!io)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Downloading %s failed: Got no I/O handle", url);
    goto fail;
    }

  //  if(!bg_http_read_response(io, 3000, redirect, res))

  if(!gavf_io_can_read(io, 10000))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Downloading %s failed: Got timeout", url);
    goto fail;
    }
  
  if(!bg_http_read_response(io, redirect, res))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Downloading %s failed: Reading response failed", url);
    goto fail;
    }
  
  if(!(*redirect))
    ret = bg_http_read_body(io, res, buf);
  else
    gavl_log(GAVL_LOG_DEBUG, LOG_DOMAIN, "Got redirected to %s", *redirect);
  
  //  if(!ret)
  //    fprintf(stderr, "result %d\n", ret);

  fail:
  
  
  return ret;
  }

int bg_http_get_range(const char * url, gavl_buffer_t * ret, gavl_dictionary_t * dict,
                      int64_t offset, int64_t size)
  {
  int i;
  char * redirect = NULL;
  char * real_url = gavl_strdup(url);
  gavl_dictionary_t res;
  gavl_dictionary_t vars;

  int result = 0;
  
  gavl_dictionary_init(&res);
  gavl_dictionary_init(&vars);

  if((offset > 0) || (size > 0))
    {
    gavl_dictionary_set_string_nocopy(&vars, "Range",
                                      bg_sprintf("bytes=%"PRId64"-%"PRId64, offset, offset + size - 1));
    }
  
  for(i = 0; i < 5; i++)
    {
    if(http_get(real_url, &res, &redirect, 0, ret, NULL))
      {
      if(dict)
        gavl_dictionary_set_string(dict, GAVL_META_MIMETYPE,
                                   gavl_dictionary_get_string_i(&res, "Content-Type"));
      result = 1;
      break;
      }
    else if(redirect)
      {
      free(real_url);
      real_url = redirect;
      redirect = NULL;
      }
    else
      break; // Error
    
    gavl_dictionary_free(&res);
    gavl_dictionary_init(&res);
    }
  gavl_dictionary_free(&res);
  gavl_dictionary_free(&vars);
  free(real_url);
  return result;
  }

int bg_http_get(const char * url, gavl_buffer_t * buf, gavl_dictionary_t * dict)
  {
  return bg_http_get_range(url, buf, dict, 0, 0);
  }

#if 0
int bg_http_get_start(int * fd, const char * url, gavl_buffer_t * ret, gavl_dictionary_t * dict,
                      int64_t offset, int64_t size)
  {
  
  }
#endif

int bg_http_head(const char * url, gavl_dictionary_t * res)
  {
  int i;
  int result = 0;
  char * redirect = NULL;
  char * real_url = gavl_strdup(url);
  gavl_dictionary_init(res);
  
  for(i = 0; i < 5; i++)
    {
    if((result = http_get(real_url, res, &redirect, 1, NULL, NULL)))
      break;
    else if(redirect)
      {
      free(real_url);
      real_url = redirect;
      redirect = NULL;
      gavl_dictionary_free(res);
      gavl_dictionary_init(res);
      }
    else
      break; // Error
    }
  free(real_url);
  return result;
  }

int bg_http_write_data(gavf_io_t * io, const uint8_t * data, int len, int chunked)
  {
  if(chunked)
    {
    char buf[128];
    int slen;
    int result;

    /* Length in hex + \r\n */
    snprintf(buf, 128, "%x\r\n", len);
    slen = strlen(buf);
    if(gavf_io_write_data(io, (uint8_t*)buf, slen) < slen)
      return 0;
    
    /* Chunk */
    if((result = gavf_io_write_data(io, data, len) < len))
      return 0;

    /* Trailing \r\n */
    strncpy(buf, "\r\n", 128);
    if(gavf_io_write_data(io, (uint8_t*)buf, 2) < 2)
      return 0;
    return result;
    }
  else
    return gavf_io_write_data(io, data, len);
  }

void bg_http_flush(gavf_io_t * io, int chunked)
  {
  if(chunked)
    {
    char buf[128];
    strncpy(buf, "0\r\n\r\n", 128);
    gavf_io_write_data(io, (uint8_t*)buf, 5);
    }
  }

struct bg_http_keepalive_s
  {
  struct
    {
    int fd;
    gavl_time_t last_active;
    } * sockets;

  int num_sockets;
  int max_sockets;
  pthread_mutex_t mutex;
  };

bg_http_keepalive_t * bg_http_keepalive_create(int max_sockets)
  {
  bg_http_keepalive_t * ret = calloc(1, sizeof(*ret));
  ret->sockets = malloc(max_sockets * sizeof(*ret->sockets));
  pthread_mutex_init(&ret->mutex, NULL);
  ret->max_sockets = max_sockets;
  return ret;
  }

void bg_http_keepalive_destroy(bg_http_keepalive_t * ka)
  {
  int i;
  for(i = 0; i < ka->num_sockets; i++)
    close(ka->sockets[i].fd);
  
  if(ka->sockets)
    free(ka->sockets);

  pthread_mutex_destroy(&ka->mutex);
  
  free(ka);
  }

void bg_http_keepalive_push(bg_http_keepalive_t * ka, int fd,
                            gavl_time_t current_time)
  {
  //  fprintf(stderr, "keepalive_push %d %d %d\n", ka->num_sockets, ka->max_sockets, fd);

  pthread_mutex_lock(&ka->mutex);

  if(ka->num_sockets == ka->max_sockets)
    {
    close(ka->sockets[ka->num_sockets-1].fd);
    ka->num_sockets--;
    }

  if(ka->num_sockets)
    memmove(ka->sockets + 1,  ka->sockets, sizeof(*ka->sockets) * ka->num_sockets);
  
  ka->sockets[0].fd = fd;
  ka->sockets[0].last_active = current_time;
  ka->num_sockets++;
  
  pthread_mutex_unlock(&ka->mutex);
  }

int bg_http_keepalive_accept
(bg_http_keepalive_t * ka, gavl_time_t current_time, int * idx)
  {
  int i;
  int ret = -1;

  pthread_mutex_lock(&ka->mutex);
    
  while(*idx < ka->num_sockets)
    {
    //    
    if(gavl_socket_can_read(ka->sockets[*idx].fd, 0))
      {
      ret = ka->sockets[*idx].fd;

      if(*idx < ka->num_sockets - 1)
        memmove(ka->sockets + (*idx), ka->sockets + (*idx) + 1,
                sizeof(*ka->sockets) * (ka->num_sockets - 1 - (*idx)));
      ka->num_sockets--;
      break;
      }
    (*idx)++;
    }

  /* Remove outdated sockets */
  i = ka->num_sockets - 1;

  while(i >= 0)
    {
    if(current_time - ka->sockets[i].last_active > KA_TIMEOUT)
      {
      close(ka->sockets[i].fd);
      ka->num_sockets--;
      }      
    else
      break;
    i--;
    }
  
  pthread_mutex_unlock(&ka->mutex);
  return ret;
  }

