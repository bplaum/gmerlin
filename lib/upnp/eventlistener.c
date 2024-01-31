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

#include <string.h>
#include <unistd.h>
#include <ctype.h>


#include <config.h>


#include <gavl/gavl.h>
#include <gavl/metadata.h>
#include <gavl/utils.h>
#include <gavl/gavlsocket.h>

#include <gmerlin/parameter.h>
#include <gmerlin/http.h>
#include <gmerlin/httpserver.h>
#include <gmerlin/upnp/eventlistener.h>
#include <gmerlin/xmlutils.h>
#include <gmerlin/utils.h>
#include <gmerlin/bgplug.h>

#include <gmerlin/translation.h>
#include <gmerlin/log.h>
#define LOG_DOMAIN "upnp.event"

// #define DUMP_SUBSCRIPTON

static int
event_listener_handle_http(bg_http_connection_t * conn, void * priv);


struct bg_upnp_event_listener_s
  {
  char * path;

  char * remote_path;
  char * remote_host;
  
  char * remote_url;
  char * local_url;
  
  char * sid; // Subscription ID
  
  gavl_socket_address_t * remote_addr;

  gavl_timer_t * timer;
  gavl_time_t last_subscribe_time;
  gavl_time_t subscribe_duration;

  bg_msg_sink_t * sink;

  char * name;
  
  };

static int subscribe(bg_upnp_event_listener_t * l)
  {
  int is_new = 0;
  int fd = -1;
  int result = 0;
  const char * var;
  char * tmp_string;
  gavl_dictionary_t req;
  gavl_dictionary_t res;

  gavl_io_t * io = NULL;
  
  gavl_dictionary_init(&req);
  gavl_dictionary_init(&res);

  gavl_http_request_init(&req,
                       "SUBSCRIBE",
                       l->remote_path,
                       "HTTP/1.1");
  gavl_dictionary_set_string(&req, "HOST", l->remote_host);

  if(l->sid) // Renew
    {
    gavl_dictionary_set_string(&req, "SID", l->sid);
    }
  else // Initial
    {
    tmp_string = bg_sprintf("<%s>", l->local_url);
    gavl_dictionary_set_string(&req, "CALLBACK", tmp_string);
    free(tmp_string);
    gavl_dictionary_set_string(&req, "NT", "upnp:event");
    is_new = 1;
    }
  
  gavl_dictionary_set_string(&req, "TIMEOUT", "Second-1800"); // 30 min as recommended
  //  gavl_dictionary_set_string(&req, "CONNECTION", "close");
  bg_http_header_set_date(&req, "Date");
  gavl_dictionary_set_string(&req, "Content-Length", "0");

#ifdef DUMP_SUBSCRIPTON
  gavl_dprintf("Sending request:\n");
  gavl_dictionary_dump(&req, 2);
  gavl_dprintf("\n");
#endif
  
  l->last_subscribe_time = gavl_timer_get(l->timer);
  
  if(((fd = gavl_socket_connect_inet(l->remote_addr, 5000)) < 0) ||
     !(io = gavl_io_create_socket(fd, 5000, 0)) ||
     !gavl_http_request_write(io, &req) ||
     !gavl_http_response_read(io, &res) ||
     (gavl_http_response_get_status_int(&res) != 200))
    goto fail;
  
#ifdef DUMP_SUBSCRIPTON
  gavl_dprintf("Got response:\n");
  gavl_dictionary_dump(&res, 2);
  gavl_dprintf("\n");
#endif
  
  if(!l->sid && (var = gavl_dictionary_get_string_i(&res, "SID")))
    l->sid = gavl_strdup(var);

  if(is_new)
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Got subscription %s %s %s",
           l->sid, l->local_url, l->path);
  else
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Renewed subscription %s", l->sid);
  
  if((var = gavl_dictionary_get_string_i(&res, "TIMEOUT")) &&
     !strncasecmp(var, "Second-", 7) &&
     isdigit(var[7]))
    l->subscribe_duration = atoi(var + 7) * GAVL_TIME_SCALE;
  
  result = 1;
  fail:
  gavl_dictionary_free(&req);
  gavl_dictionary_free(&res);
  
  if(io)
    gavl_io_destroy(io);

  if(fd >= 0)
    gavl_socket_close(fd);
  
  return result;
  }

static void unsubscribe(bg_upnp_event_listener_t * l)
  {
  int fd = -1;
  gavl_dictionary_t req;
  gavl_dictionary_t res;
  gavl_io_t * io = NULL;
  
  gavl_dictionary_init(&req);
  gavl_dictionary_init(&res);
  
  gavl_http_request_init(&req,
                       "UNSUBSCRIBE",
                       l->remote_path,
                       "HTTP/1.1");

  gavl_dictionary_set_string(&req, "HOST", l->remote_host);

  if(l->sid) // Renew
    gavl_dictionary_set_string(&req, "SID", l->sid);
  else
    goto fail;

  if(((fd = gavl_socket_connect_inet(l->remote_addr, 5000)) < 0) ||
     !(io = gavl_io_create_socket(fd, 5000, 0)) ||
     !gavl_http_request_write(io, &req) ||
     !gavl_http_response_read(io, &res) ||
     (gavl_http_response_get_status_int(&res) != 200))
    goto fail;
  
  
  fail:

  
  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Unsubscribing %s %s", l->sid,
         ((gavl_http_response_get_status_int(&res) != 200) ? "failed" : "succeeded"));

  if(gavl_http_response_get_status_int(&res) != 200)
    gavl_dictionary_dump(&res, 2);
  
  if(fd >= 0)
    gavl_socket_close(fd);
  
  if(io)
    gavl_io_destroy(io);
  
  gavl_dictionary_free(&req);
  gavl_dictionary_free(&res);
  
  return;
  }

bg_upnp_event_listener_t *
bg_upnp_event_listener_create(const char * event_url_remote,
                              const char * url_local,
                              const char * name,
                              bg_msg_sink_t * dst)
  {
  int result = 0;
  bg_upnp_event_listener_t * ret;
  char * host = NULL;
  int port;
  
  ret = calloc(1, sizeof(*ret));
  ret->remote_url = gavl_strdup(event_url_remote);
  ret->remote_addr = gavl_socket_address_create();
  ret->name = gavl_strdup(name);

  if(gavl_string_ends_with(url_local, "/"))
    ret->path = bg_sprintf("evt/%s", name);
  else
    ret->path = bg_sprintf("/evt/%s", name);

  bg_http_server_add_handler(bg_http_server_get(),
                             event_listener_handle_http,
                             BG_HTTP_PROTO_HTTP,
                             ret->path, ret);
  
  ret->local_url = bg_sprintf("%s%s", url_local, ret->path);
  
  ret->timer = gavl_timer_create();

  gavl_timer_start(ret->timer);
  
  if(!bg_url_split(ret->remote_url,
                   NULL, NULL, NULL,
                   &host, &port, &ret->remote_path))
    {
    goto fail;
    }

  if(!gavl_socket_address_set(ret->remote_addr, host,
                            port, SOCK_STREAM))
    goto fail;
  
  ret->remote_host = bg_sprintf("%s:%d", host, port);

  /* Subscribe */
  
  if(!subscribe(ret))
    goto fail;

  ret->sink = dst;
  
  result = 1;
  
  fail:

  if(host)
    free(host);
  
  if(!result)
    {
    bg_upnp_event_listener_destroy(ret);
    return NULL;
    }
  else
    return ret;
  }

void bg_upnp_event_listener_destroy(bg_upnp_event_listener_t * l)
  {
  unsubscribe(l);

  bg_http_server_remove_handler(bg_http_server_get(),
                                l->path, l);
  
  if(l->remote_path)
    free(l->remote_path);
  if(l->remote_url)
    free(l->remote_url);
  if(l->remote_host)
    free(l->remote_host);
  if(l->local_url)
    free(l->local_url);
  if(l->path)
    free(l->path);
  if(l->sid)
    free(l->sid);
  if(l->name)
    free(l->name);

  if(l->remote_addr)
    gavl_socket_address_destroy(l->remote_addr);
  
  gavl_timer_destroy(l->timer);

  free(l);
  }

static void set_value(bg_upnp_event_listener_t * l, const char * name, const char * val)
  {
  if(name && val && strcmp(val, "NOT_IMPLEMENTED"))
    {
    if(l->sink)
      {
      gavl_msg_t * msg = bg_msg_sink_get(l->sink);
      bg_upnp_event_to_msg(msg, l->name, name, val);

      //      fprintf(stderr, "Got event:\n");
      //      gavl_msg_dump(msg, 2);

      bg_msg_sink_put(l->sink);
      }
    }
  }

static int handle_last_change(bg_upnp_event_listener_t * l, const char * str)
  {
  xmlNodePtr root;
  xmlNodePtr node;
  xmlDocPtr xml_doc;
  char * var;

  //  fprintf(stderr, "Last change: %s\n", str);
  
  if(!(xml_doc = xmlParseMemory(str, strlen(str))) ||
     !(root = bg_xml_find_doc_child(xml_doc, "Event")) ||
     !(root = bg_xml_find_next_node_child_by_name(root, NULL, "InstanceID")))
    return 0;

  node = NULL;
  while((node = bg_xml_find_next_node_child(root, node)))
    {
    var = BG_XML_GET_PROP(node, "val");

    set_value(l, (const char*)node->name, var);

    if(var)
      xmlFree(var);
    }
  xmlFreeDoc(xml_doc);
  return 1;
  }

int bg_upnp_event_listener_ping(bg_upnp_event_listener_t * l)
  {
  int ret = 0;
  gavl_time_t current_time;

  current_time = gavl_timer_get(l->timer);
  
  /* Renew subscription after 80% of the duration */
  if(current_time - l->last_subscribe_time >=
     (8 * l->subscribe_duration) / 10)
    {
    //    fprintf(stderr, "Subscribe..%"PRId64" - %"PRId64" >= %"PRId64"\n",
    //            current_time, l->last_subscribe_time,
    //        l->subscribe_duration);
    subscribe(l);
    // fprintf(stderr, "Subscribe....done\n");
    ret = 1;
    }
  return ret;
  }

static int
event_listener_handle_http(bg_http_connection_t * conn, void * priv)
  {
  xmlDocPtr xml_doc;
  xmlNodePtr root;
  xmlNodePtr node;
  xmlNodePtr child;
  const char * var;
  gavl_io_t * io;

  gavl_buffer_t buf;

  bg_upnp_event_listener_t * l = priv;
  
  gavl_buffer_init(&buf);
  
  if(strcmp(conn->method, "NOTIFY") || strcmp(conn->path, l->path))
    return 0;

  io = gavl_io_create_socket(conn->fd, 30000, 0);
  
  if(!gavl_http_read_body(io, &conn->req, &buf))
    {
    gavl_io_destroy(io);
    return 1;
    }

  gavl_io_destroy(io);
  
  if(!(xml_doc = xmlParseMemory((char*)buf.buf, buf.len)) ||
     !(root = bg_xml_find_doc_child(xml_doc, "propertyset")))
    {
    gavl_buffer_free(&buf);
    bg_http_connection_init_res(conn, "HTTP/1.1", 400, "Bad Request");
    goto fail;
    }
  
  node = NULL;
  
  while((node = bg_xml_find_next_node_child_by_name(root, node, "property")))
    {
    child = bg_xml_find_next_node_child(node, NULL);

    if(!child)
      continue;
    
    var = bg_xml_node_get_text_content(child);
    
    if(!strcmp((char*)child->name, "LastChange")) 
      {
      // Actual variables are wrapped in another XML layer
      handle_last_change(l, var);
      }
    else if(var && strcmp(var, "NOT_IMPLEMENTED"))
      {
      set_value(l, (char*)child->name, var);
      }
    }
  
  xmlFreeDoc(xml_doc);
  gavl_buffer_free(&buf);
  
  //  fprintf(stderr, "Got event\n");
  //  gavl_dictionary_dump(&l->vars, 2);

  bg_http_connection_check_keepalive(conn);
  
  bg_http_connection_init_res(conn, "HTTP/1.1", 200, "OK");
  gavl_dictionary_set_string(&conn->res, "Content-Length", "0");

  fail:
  
  bg_http_connection_write_res(conn);
  
  return 1;
  }

void bg_upnp_event_to_msg(gavl_msg_t * msg,
                          const char * service, const char * variable, const char * value)
  {
  gavl_msg_set_id_ns(msg, BG_MSG_ID_UPNP_EVENT, BG_MSG_NS_UPNP);
  gavl_msg_set_arg_string(msg, 0, service);
  gavl_msg_set_arg_string(msg, 1, variable);
  gavl_msg_set_arg_string(msg, 2, value);
  }

void bg_upnp_event_from_msg(gavl_msg_t * msg,
                            const char ** service, const char ** variable, const char ** value)
  {
  *service  = gavl_msg_get_arg_string_c(msg, 0);
  *variable = gavl_msg_get_arg_string_c(msg, 1);
  *value    = gavl_msg_get_arg_string_c(msg, 2);
  }
