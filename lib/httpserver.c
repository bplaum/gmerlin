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


#include <unistd.h>
#include <string.h>

#include <poll.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <pthread.h>
#include <uuid/uuid.h>


#include <config.h>

#include <gavl/gavl.h>
#include <gavl/metadata.h>
#include <gavl/utils.h>
#include <gavl/numptr.h>

#include <gmerlin/translation.h>
#include <gmerlin/bgsocket.h>

#include <gmerlin/parameter.h>
#include <gmerlin/httpserver.h>
#include <gmerlin/http.h>
#include <gmerlin/utils.h>
#include <gmerlin/mdb.h>

// #include <gmerlin/upnp/device.h>

#include <httpserver_priv.h>


#include <gmerlin/log.h>
#define LOG_DOMAIN "httpserver"

#define TIMEOUT GAVL_TIME_SCALE/2000


/* Server */

static const bg_parameter_info_t parameters[] =
  {
    {
      .name =      "port",
      .type = BG_PARAMETER_INT,
      .long_name =  TRS("Listen port"),
      .val_default = GAVL_VALUE_INIT_INT(0),
      .val_min     = GAVL_VALUE_INIT_INT(0),
      .val_max     = GAVL_VALUE_INIT_INT(65535),
    },
    {
      .name =      "addr",
      .type = BG_PARAMETER_STRING,
      .long_name =  TRS("Address to bind to"),
    },
    {
      .name =      "max_keepalive_sockets",
      .type = BG_PARAMETER_INT,
      .long_name =  TRS("Maximum number of keep-alive sockets"),
      .val_default = GAVL_VALUE_INIT_INT(16),
      .val_min     = GAVL_VALUE_INIT_INT(0),
      .val_max     = GAVL_VALUE_INIT_INT(65535),
    },
    {
      .name =      "max_client_ids",
      .type = BG_PARAMETER_INT,
      .flags = BG_PARAMETER_HIDE_DIALOG,
      .long_name =  TRS("Maximum number of clients which can store data"),
      .val_default = GAVL_VALUE_INIT_INT(16),
      .val_min     = GAVL_VALUE_INIT_INT(0),
      .val_max     = GAVL_VALUE_INIT_INT(65535),
    },
    { /* End */ },
  };



/* Client thread stuff */

static void * thread_func(void * priv)
  {
  client_thread_t * th = priv;

  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Client thread started");
  
  th->thread_func(&th->conn, th->priv);

  /* Signal that we are done */
  pthread_mutex_lock(&th->stopped_mutex);
  th->stopped = 1;
  pthread_mutex_unlock(&th->stopped_mutex);
  
  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Client thread finished");
  return NULL;
  }

void bg_http_server_create_client_thread(bg_http_server_t * s,
                                         bg_http_server_thread_func tf,
                                         bg_http_server_thread_cleanup cf,
                                         bg_http_connection_t * conn, void * priv)
  {
  client_thread_t * th = calloc(1, sizeof(*th));
  th->cleanup = cf;
  th->thread_func = tf;
  th->priv = priv;
  
  memcpy(&th->conn, conn, sizeof(th->conn));
  bg_http_connection_init(conn);
  
  pthread_mutex_lock(&s->threads_mutex);

  if(s->num_threads + 1 > s->threads_alloc)
    {
    s->threads_alloc += 16;
    s->threads = realloc(s->threads, s->threads_alloc * sizeof(*s->threads));
    }
  s->threads[s->num_threads] = th;
  s->num_threads++;
  pthread_mutex_unlock(&s->threads_mutex);
  
  pthread_create(&th->th, NULL, thread_func, th);
  }

static int client_thread_finished(client_thread_t * th)
  {
  int ret;

  pthread_mutex_lock(&th->stopped_mutex);
  ret = th->stopped;
  pthread_mutex_unlock(&th->stopped_mutex);
  return ret;
  }

static void client_thread_destroy(client_thread_t * th)
  {
  pthread_join(th->th, NULL);

  if(th->cleanup)
    th->cleanup(th->priv);
  pthread_mutex_destroy(&th->stopped_mutex);

  bg_http_connection_free(&th->conn);
  
  free(th);
  }

/* */

void bg_http_server_put_connection(bg_http_server_t * s, bg_http_connection_t * conn)
  {
  if((conn->flags & BG_HTTP_REQ_KEEPALIVE) && (conn->fd > -1))
    {
    bg_http_keepalive_push(s->ka, conn->fd, conn->current_time);
    conn->fd = -1;
    }
  }

static int handle_static(bg_http_connection_t * conn, void * data)
  {
  bg_http_connection_send_static_file(conn);
  return 1;
  }

void bg_http_server_set_static_path(bg_http_server_t * s, const char * path)
  {
  bg_http_server_add_handler(s, handle_static, BG_HTTP_PROTO_HTTP, path, s);
  }

/*
 *  Application icons: http://host:8888/appicons/<name>.png
 */

static int handle_appicon(bg_http_connection_t * conn, void * data)
  {
  char * file;
  char * real_file;
  char * pos;
  
  file = gavl_strdup(conn->path);
  if((pos = strrchr(file, '.')))
    *pos = '\0';

  real_file = bg_search_application_icon(file, -1);

  //  fprintf(stderr, "handle_appicon %s %s %s\n", conn->path, file, real_file);
  
  free(file);

  
  if(real_file)
    {
    bg_http_connection_send_file(conn, real_file);
    free(real_file);
    return 1;
    }

  return 0;
  }

void bg_http_server_enable_appicons(bg_http_server_t * s)
  {
  bg_http_server_add_handler(s, handle_appicon, BG_HTTP_PROTO_HTTP, BG_HTTP_APPICON_PATH, s);
  }

static int generate_client_id(bg_http_connection_t * conn, void * data)
  {
  if(strcmp(conn->path, "/") && strncmp(conn->path, "/?", 2))
    return 0; 
  
  /* Check for a client ID. If we don't have one, generate one. */
  if(!gavl_dictionary_get_string(&conn->url_vars, BG_URL_VAR_CLIENT_ID))
    {
    char * location;
    
    uuid_t cid;
    char cid_str[37];
    uuid_generate(cid);
    uuid_unparse(cid, cid_str);
    gavl_dictionary_set_string(&conn->url_vars, BG_URL_VAR_CLIENT_ID, cid_str);
    
    location = bg_sprintf("http://%s/", gavl_dictionary_get_string_i(&conn->req, "Host"));
    location = bg_url_append_vars(location, &conn->url_vars);

    bg_http_connection_init_res(conn, conn->protocol, 303, "See Other");
    gavl_dictionary_set_string_nocopy(&conn->res, "Location", location);
    return 1;
    }  
  return 0;
  }

void bg_http_server_set_generate_client_ids(bg_http_server_t * s)
  {
  bg_http_server_add_handler(s, generate_client_id, BG_HTTP_PROTO_HTTP, NULL, NULL);
  }

static int handle_root(bg_http_connection_t * conn, void * data)
  {
  char * new_path = NULL;
  const char * url_vars;
  bg_http_server_t * s = data;
  
  if(strcmp(conn->path, "/") && strncmp(conn->path, "/?", 2))
    return 0; 
  
  if(!(url_vars = strchr(conn->path, '?')))
    url_vars = "";
  
  new_path = bg_sprintf("%s%s", s->root_file, url_vars);
  
  gavl_http_request_set_path(&conn->req, new_path);
  conn->path = gavl_http_request_get_path(&conn->req);
  free(new_path);
  
  return 0;
  }

/* Path to a file (html) to load at the root path "/" */
void bg_http_server_set_root_file(bg_http_server_t * s, const char * path)
  {
  s->root_file = gavl_strdup(path);
  bg_http_server_add_handler(s, handle_root, BG_HTTP_PROTO_HTTP, NULL, s);
  }

bg_http_server_t * bg_http_server_create(void)
  {
  bg_http_server_t * ret;
  ret = calloc(1, sizeof(*ret));
  ret->max_ka_sockets = 10;
  ret->timer = gavl_timer_create();
  return ret;
  }

void bg_http_server_destroy(bg_http_server_t * s)
  {
  int i;

  if(s->dirs)
    bg_media_dirs_destroy(s->dirs);
  
  if(s->bind_addr)
    free(s->bind_addr);
  if(s->addr)
    gavl_socket_address_destroy(s->addr);
  if(s->remote_addr)
    gavl_socket_address_destroy(s->remote_addr);
  if(s->root_url)
    free(s->root_url);
  if(s->root_file)
    free(s->root_file);
  
  if(s->ka)
    bg_http_keepalive_destroy(s->ka);

  for(i = 0; i < s->num_handlers; i++)
    {
    if(s->handlers[i].path)
      free(s->handlers[i].path);
    }
  if(s->handlers)
    free(s->handlers);
  
  if(s->timer)
    gavl_timer_destroy(s->timer);
  
  if(s->fd > 0)
    close(s->fd);

  if(s->playlist_handler)
    bg_http_playlist_handler_destroy(s->playlist_handler);

  if(s->lpcmhandler)
    bg_lpcm_handler_destroy(s->lpcmhandler);

  if(s->plughandler)
    bg_plug_handler_destroy(s->plughandler);
  
  for(i = 0; i < s->num_headers; i++)
    bg_http_server_free_header(&s->headers[i]);

  if(s->params)
    bg_parameter_info_destroy_array(s->params);

  free(s);
  }

int bg_http_server_write_res(bg_http_server_t * s, bg_http_connection_t * req)
  {
  if(s->server_string)
    gavl_dictionary_set_string(&req->res, "Server", s->server_string);
  
  return bg_http_connection_write_res(req);  
  }

const char * bg_http_server_get_root_url(bg_http_server_t * s)
  {
  return s->root_url;
  }

const gavl_socket_address_t * bg_http_server_get_address(bg_http_server_t * s)
  {
  return s->addr;
  }

void bg_http_server_set_server_string(bg_http_server_t * s, const char * str)
  {
  s->server_string = str;
  }

const bg_parameter_info_t * bg_http_server_get_parameters(bg_http_server_t * s)
  {
  if(!s->params)
    s->params = bg_parameter_info_copy_array(parameters);
  return s->params;
  }

void bg_http_server_set_default_port(bg_http_server_t * s, int port)
  {
  int idx;
  if(!s->params)
    s->params = bg_parameter_info_copy_array(parameters);

  idx = 0;
  while(s->params[idx].name)
    {
    if(!strcmp(s->params[idx].name, "port"))
      {
      s->params[idx].val_default.v.i = port;
      break;
      }
    idx++;
    }

  if(s->port < 1)
    s->port = port;
  }

int bg_http_server_has_path(bg_http_server_t * s, const char * path)
  {
  int i;
  for(i = 0; i < s->num_handlers; i++)
    {
    if(s->handlers[i].path && !strncmp(path, s->handlers[i].path, strlen(s->handlers[i].path)))
      return 1;
    }
  return 0;
  }

void bg_http_server_set_parameter(void * sp, const char * name, const gavl_value_t * val)
  {
  bg_http_server_t * s = sp;
  
  if(!name)
    {
    if(s->dirs)
      bg_media_dirs_set_parameter(s->dirs, NULL, NULL);
    return;
    }
  if(s->dirs && bg_media_dirs_set_parameter(s->dirs, name, val))
    return;
  else if(!strcmp(name, "port"))
    s->port = val->v.i;
  else if(!strcmp(name, "addr"))
    s->bind_addr = gavl_strrep(s->bind_addr, val->v.str);
  else if(!strcmp(name, "max_keepalive_sockets"))
    s->max_ka_sockets = val->v.i;
  }

int bg_http_server_get_parameter(void * sp, const char * name, gavl_value_t * val)
  {
  bg_http_server_t * s = sp;
  if(!name)
    return 0;
  else if(!strcmp(name, "port"))
    {
    val->v.i = s->port;
    return 1;
    }
  return 0;
  }

void bg_http_server_add_handler(bg_http_server_t * s,
                                bg_http_handler_t h,
                                int protocols,
                                const char * path, // E.g. /static/ can be NULL
                                void * data)
  {
  if(s->num_handlers + 1 > s->handlers_alloc)
    {
    s->handlers_alloc += 16;
    s->handlers = realloc(s->handlers, s->handlers_alloc * sizeof(*s->handlers));
    memset(s->handlers + s->num_handlers, 0, (s->handlers_alloc - s->num_handlers) * sizeof(*s->handlers));
    }
  s->handlers[s->num_handlers].func = h;
  s->handlers[s->num_handlers].protocols = protocols;
  s->handlers[s->num_handlers].path = gavl_strdup(path);
  s->handlers[s->num_handlers].data = data;
  s->num_handlers++;
  }

void bg_http_server_remove_handler(bg_http_server_t * s,
                                   const char * path, // E.g. /static/ can be NULL
                                   void * data)
  {
  int idx = -1;
  int i = 0;
  while(s->handlers[i].func)
    {
    if((s->handlers[i].data == data) ||
       (s->handlers[i].path && path && !strcmp(s->handlers[i].path, path)))
      {
      idx = i;
      break;
      }
    }

  if(idx < 0)
    return;

  if(s->handlers[idx].path)
    free(s->handlers[idx].path);

  if(idx < s->num_handlers-1)
    memmove(s->handlers + idx,
            s->handlers + idx + 1,
            sizeof(*s->handlers) * (s->num_handlers-1 - idx));
  s->num_handlers--;
  
  }

int bg_http_server_start(bg_http_server_t * s)
  {
  const char * addr;
  char addr_str[GAVL_SOCKET_ADDR_STR_LEN];
  
  s->ka = bg_http_keepalive_create(s->max_ka_sockets);

  s->addr = gavl_socket_address_create();
  s->remote_addr = gavl_socket_address_create();
  
  if(s->bind_addr)
    addr = s->bind_addr;
  else
    addr = "0.0.0.0";

  if(!gavl_socket_address_set(s->addr, addr,
                            s->port, SOCK_STREAM))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Invalid interface address %s",
           addr);
    return 0;
    }
  
  s->fd = gavl_listen_socket_create_inet(s->addr, 0 /* Port */,
                                       10 /* queue_size */,
                                       GAVL_SOCKET_REUSEADDR /* flags */);
  
  if(s->fd < 0)
    return 0;
  
  if(!gavl_socket_get_address(s->fd, s->addr, NULL))
    return 0;
  
  /* get_parameter() might need that */
  s->port = gavl_socket_address_get_port(s->addr);

  /* Special case for 0.0.0.0: Set address to a network wide reachable one */
  if(!strcmp(addr, "0.0.0.0") || !strcmp(addr, "::"))
    {
    if(!gavl_socket_address_set_local(s->addr, s->port, addr))
      return 0;
    }
  
  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Socket listening on %s:%d", addr, s->port);
  
  gavl_socket_address_to_string(s->addr, addr_str);
  s->root_url = bg_sprintf("http://%s", addr_str);

  if(s->dirs)
    bg_media_dirs_set_root_uri(s->dirs, s->root_url);
  
  gavl_timer_start(s->timer);
  
  return 1;
  }

static void handle_client_connection(bg_http_server_t * s, int fd)
  {
  int i;
  int result = 0;
  
  if(!bg_http_connection_read_req(&s->req, fd, TIMEOUT))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Couldn't read request");
    goto fail;
    }

  gavl_log(GAVL_LOG_DEBUG, LOG_DOMAIN, "Got request: %s %s %s", s->req.method, s->req.path, s->req.protocol);
  
  s->req.current_time = gavl_timer_get(s->timer);
  
  for(i = 0; i < s->num_handlers; i++)
    {
    // Wrong protocol
    if(!(s->handlers[i].protocols & s->req.protocol_i)) 
      continue;
    
    if(s->handlers[i].path)
      {
      int len = strlen(s->handlers[i].path);
      
      if(strncmp(s->handlers[i].path, s->req.path, len))
        continue;

      s->req.path+=len;

      result = s->handlers[i].func(&s->req, s->handlers[i].data);
      break;
      }
    else
      {
      if((result = s->handlers[i].func(&s->req, s->handlers[i].data)))
        break;
      }
    }
  
  if(!result) // 404
    gavl_http_response_init(&s->req.res, s->req.protocol, 404, "Not Found");

  /* Send response (if not already done) */
  bg_http_server_write_res(s, &s->req);
  bg_http_server_put_connection(s, &s->req);
  
  fail:
  
  /* Cleanup */
  bg_http_connection_free(&s->req);
  
  }

/* 
 *  Integrate this into your main loop. Return value is zero if nothing happened.
 *  In this case the thread should sleep a short time before calling this function again.
 */

int bg_http_server_iteration(bg_http_server_t * s)
  {
  int i;
  int fd;
  int ret = 0;
  int ka_idx;
  char addr_str[GAVL_SOCKET_ADDR_STR_LEN];

  /* Remove finished clients */
  pthread_mutex_lock(&s->threads_mutex);

  i = 0;
  
  while(i < s->num_threads)
    {
    if(client_thread_finished(s->threads[i]))
      {
      client_thread_destroy(s->threads[i]);
      if(i < s->num_threads-1)
        {
        memmove(s->threads + i, s->threads + i + 1,
                (s->num_threads - 1 - i) * sizeof(*s->threads));
        }
      s->num_threads--;
      }
    else
      i++;
    }

  pthread_mutex_unlock(&s->threads_mutex);
  
  ka_idx = 0;
  
  while((fd = bg_http_keepalive_accept(s->ka, gavl_timer_get(s->timer),
                                       &ka_idx)) >= 0)
    {
    gavl_log(GAVL_LOG_DEBUG, LOG_DOMAIN, "Reusing keep-alive connection");
    handle_client_connection(s, fd);
    ret++;
    }
  while((fd = gavl_listen_socket_accept(s->fd, 0, s->remote_addr)) >= 0)
    {
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Got connection from %s fd: %d",
           gavl_socket_address_to_string(s->remote_addr, addr_str), fd);
    handle_client_connection(s, fd);
    ret++;
    }

  if(s->playlist_handler)
    ret += bg_http_playlist_handler_ping(s->playlist_handler);
  
  return ret;
  }

gavl_time_t bg_http_server_get_time(bg_http_server_t * s)
  {
  return gavl_timer_get(s->timer);
  }

bg_media_dirs_t * bg_http_server_get_media_dirs(bg_http_server_t * s)
  {
  if(!s->dirs)
    {
    s->dirs = bg_media_dirs_create();
    /* Add media handler */
    bg_http_server_init_mediafile_handler(s);
    }
  
  return s->dirs;
  }

void bg_http_server_set_mdb(bg_http_server_t * s, bg_mdb_t * mdb)
  {
  s->mdb = mdb;
  bg_http_server_init_playlist_handler(s);

  if(bg_plugin_reg && !s->lpcmhandler)
    s->lpcmhandler = bg_lpcm_handler_create(mdb, s);

  if(bg_plugin_reg && !s->plughandler)
    s->plughandler = bg_plug_handler_create(s);
  
  }

void bg_http_server_free_header(header_t * h)
  {
  if(h->uri)
    free(h->uri);
  gavl_buffer_free(&h->buf);
  memset(h, 0, sizeof(*h));
  }
