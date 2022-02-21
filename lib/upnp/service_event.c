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
#include <upnp/devicepriv.h>

#include <gmerlin/upnp/upnputils.h>


#include <gmerlin/utils.h>
#include <gmerlin/http.h>
#include <gmerlin/log.h>
#define LOG_DOMAIN "upnp.event"

#include <string.h>
#include <unistd.h> // close

#define CLIENT_TIMEOUT 500

static int do_send_event(bg_upnp_sv_t * sv, gavl_time_t current_time, int force)
  {
  if(force)
    return 1;

  if(!(sv->flags & BG_UPNP_SV_EVENT) ||
     !(sv->flags & BG_UPNP_SV_CHANGED))
    return 0;
  
  if(sv->interval &&
     (sv->last_event != GAVL_TIME_UNDEFINED) &&
     (current_time < sv->last_event + sv->interval))
    return 0;
  
  return 1;
  }

static char * create_event(bg_upnp_service_t * s, int * len, int force, gavl_time_t current_time)
  {
  char * ret;
  int i;
  bg_upnp_sv_t * last_change = NULL;
  xmlNodePtr propset;
  xmlNodePtr node;
  xmlNsPtr ns;
  xmlDocPtr doc;
  char * buf;
  int do_send;
  
  /* Indirect events */
  
  if(!force)
    {
    do_send = 0;
    for(i = 0; i < s->desc.num_sv; i++)
      {
      if((s->desc.sv[i].flags & BG_UPNP_SV_EVENT_MOD) &&
         (s->desc.sv[i].flags & BG_UPNP_SV_CHANGED))
        {
        do_send = 1;
        break;
        }
      }
    }
  else
    do_send = 1;
  
  if(do_send)
    {
    //    bg_upnp_sv_val_t val;

    for(i = 0; i < s->desc.num_sv; i++)
      {
      if(!strcmp(s->desc.sv[i].name, "LastChange"))
        last_change = &s->desc.sv[i];
      }

    if(last_change)
      {
      xmlNodePtr root;
      doc = xmlNewDoc((xmlChar*)"1.0");

      root = xmlNewDocRawNode(doc, NULL, (xmlChar*)"Event", NULL);
      xmlDocSetRootElement(doc, root);

      root = xmlNewChild(root, NULL, (xmlChar*)"InstanceID", NULL);
      BG_XML_SET_PROP(root, "val", "0");
      
      for(i = 0; i < s->desc.num_sv; i++)
        {
        if(!(s->desc.sv[i].flags & BG_UPNP_SV_EVENT_MOD))
          continue;

        if(!force && !(s->desc.sv[i].flags & BG_UPNP_SV_CHANGED))
          continue;

        buf = bg_upnp_val_to_string(s->desc.sv[i].type,
                                    &s->desc.sv[i].value);
        
        node = xmlNewChild(root, NULL, (xmlChar*)s->desc.sv[i].name, NULL);
        BG_XML_SET_PROP(node, "val", buf);
        free(buf);

        if(!force)
          bg_upnp_sv_clear_changed(&s->desc.sv[i]);
        }
      buf = bg_xml_save_to_memory(doc);
      xmlFreeDoc(doc);
      //      val.s = buf;
      bg_upnp_sv_set_string(last_change, buf);
      //   fprintf(stderr, "LastChange: %s\n", buf);
      free(buf);
      }
    }
  
  /* Check if we should send and event at all */
  if(!force)
    {
    do_send = 0;
    for(i = 0; i < s->desc.num_sv; i++)
      {
      if(do_send_event(&s->desc.sv[i], current_time, force))
        {
        do_send = 1;
        break;
        }
      }
    }
  else
    do_send = 1;
  
  if(!do_send)
    return NULL;

  /* Send an event for *all* variables */
  doc = xmlNewDoc((xmlChar*)"1.0");
  propset = xmlNewDocRawNode(doc, NULL, (xmlChar*)"propertyset", NULL);
  xmlDocSetRootElement(doc, propset);
  ns = xmlNewNs(propset,
                (xmlChar*)"urn:schemas-upnp-org:event-1-0",
                (xmlChar*)"e");
  xmlSetNs(propset, ns);
  
  for(i = 0; i < s->desc.num_sv; i++)
    {
    if(!do_send_event(&s->desc.sv[i], current_time, force))
      continue;
    
    if(!force)
      {
      s->desc.sv[i].last_event = current_time;
      bg_upnp_sv_clear_changed(&s->desc.sv[i]);
      }
    
    node = xmlNewChild(propset, ns, (xmlChar*)"property", NULL);
    switch(s->desc.sv[i].type)
      {
      case BG_UPNP_SV_INT4:
      case BG_UPNP_SV_INT2:
        {
        char buf[16];
        snprintf(buf, 16, "%d", s->desc.sv[i].value.i);
        node = xmlNewChild(node, NULL, (xmlChar*)s->desc.sv[i].name, (xmlChar*)buf);
        xmlSetNs(node, NULL);
        }
        break;
      case BG_UPNP_SV_UINT4:
      case BG_UPNP_SV_UINT2:
      case BG_UPNP_SV_BOOLEAN:
        {
        char buf[16];
        snprintf(buf, 16, "%u", s->desc.sv[i].value.ui);
        node = xmlNewChild(node, NULL, (xmlChar*)s->desc.sv[i].name, (xmlChar*)buf);
        xmlSetNs(node, NULL);
        }
        break;
      case BG_UPNP_SV_STRING:
        node = xmlNewChild(node, NULL, (xmlChar*)s->desc.sv[i].name,
                    (xmlChar*)(s->desc.sv[i].value.s ?
                               s->desc.sv[i].value.s : ""));
        xmlSetNs(node, NULL);
        break;
      }
    }
  ret = bg_xml_save_to_memory(doc);
  *len = strlen(ret);
  xmlFreeDoc(doc);
  return ret;
  }

static int send_event(bg_upnp_event_subscriber_t * es,
                      char * event, int len)
  {
  gavl_dictionary_t m;
  bg_socket_address_t * addr = NULL;
  char * path = NULL;
  char * host = NULL;
  int port;
  int fd = -1;
  char * tmp_string;
  int result = 0;
  
  gavl_dictionary_init(&m);
  
  /* Parse URL */
  if(!bg_url_split(es->url,
                   NULL, NULL, NULL,
                   &host, &port, &path))
    goto fail;
  
  addr = bg_socket_address_create();

  if(!bg_socket_address_set(addr, host, port, SOCK_STREAM))
    goto fail;
  
  if((fd = bg_socket_connect_inet(addr, 500)) < 0)
    goto fail;

  if(path)
    bg_http_request_init(&m, "NOTIFY", path, "HTTP/1.1");
  else
    bg_http_request_init(&m, "NOTIFY", "/", "HTTP/1.1");
    
  tmp_string = bg_sprintf("%s:%d", host, port);
  gavl_dictionary_set_string(&m, "HOST", tmp_string);
  free(tmp_string);
  
  gavl_dictionary_set_string(&m, "CONTENT-TYPE", "text/xml");
  gavl_dictionary_set_int(&m, "CONTENT-LENGTH", len);
  gavl_dictionary_set_string(&m, "NT", "upnp:event");
  gavl_dictionary_set_string(&m, "NTS", "upnp:propchange");
  tmp_string = bg_sprintf("uuid:%s", es->uuid);  
  gavl_dictionary_set_string(&m, "SID", tmp_string);
  free(tmp_string);
  gavl_dictionary_set_long(&m, "SEQ", es->key++);

  //  fprintf(stderr, "Sending event: %s (%d %d)\n", es->url, len, strlen(event));
  //  gavl_dictionary_dump(&m, 0);
  //  fprintf(stderr, "%s\n", event);
  
  if(!bg_http_request_write(fd, &m))
    {
    goto fail;
    }
  if(!bg_socket_write_data(fd, (uint8_t*)event, len))
    {
    goto fail;
    }
  gavl_dictionary_free(&m);
  gavl_dictionary_init(&m);
  
  if(!bg_http_response_read(fd, &m, CLIENT_TIMEOUT))
    {
    // Some weird clients send no reply
    result = 1;
    goto fail;
    }
  //  fprintf(stderr, "Got response:\n");
  //  gavl_dictionary_dump(&m, 0);

  if(bg_http_response_get_status_int(&m) != 200)
    goto fail;
  result = 1;
  fail:

  gavl_dictionary_free(&m);
  if(path)
    free(path);
  if(host)
    free(host);

  if(fd >= 0)
    gavl_socket_close(fd);
  
  if(addr)
    bg_socket_address_destroy(addr);
  
  return result;
  }

static int get_timeout_seconds(const char * timeout, int * ret)
  {
  if(!strncmp(timeout, "Second-", 7))
    {
    *ret = atoi(timeout + 7);
    return 1;
    }
  else if(!strcmp(timeout, "infinite"))
    {
    *ret = 1800; // We don't like infinite subscriptions
    return 1;
    }
  return 0;
  }

static int add_subscription(bg_upnp_service_t * s, bg_http_connection_t * conn,
                            const char * callback, const char * timeout)
  {
  const char * start, *end;
  uuid_t uuid;
  bg_upnp_event_subscriber_t * es;
  int seconds;
  int result = 0;
  char * event = NULL;
  int len;

  if(s->num_es + 1 > s->es_alloc)
    {
    s->es_alloc += 8;
    s->es = realloc(s->es, s->es_alloc * sizeof(*s->es));
    memset(s->es + s->num_es, 0, (s->es_alloc - s->num_es) * sizeof(*s->es));
    }
  es = s->es + s->num_es;
  memset(es, 0, sizeof(*es));
  
  /* UUID */
  uuid_generate(uuid);
  uuid_unparse(uuid, es->uuid);
  
  /* Timeout */

  if(!get_timeout_seconds(timeout, &seconds))
    {
    bg_http_connection_init_res(conn, "HTTP/1.1", 400, "Bad Request");
    goto fail;
    }
  es->expire_time = gavl_timer_get(s->dev->timer) + seconds * GAVL_TIME_SCALE;
  
  /* Callback */
  start = strchr(callback, '<');
  if(!start)
    {
    bg_http_connection_init_res(conn, "HTTP/1.1", 412, "Precondition Failed");
    goto fail;
    }
  start++;
  end = strchr(callback, '>');
  if(!end)
    {
    bg_http_connection_init_res(conn, "HTTP/1.1", 412, "Precondition Failed");
    goto fail;
    }
  es->url = gavl_strndup(start, end);

  bg_http_connection_init_res(conn, "HTTP/1.1", 200, "OK");
  gavl_dictionary_set_string(&conn->res, "SERVER", bg_upnp_get_server_string());
  gavl_dictionary_set_string_nocopy(&conn->res, "SID", bg_sprintf("uuid:%s", es->uuid));
  gavl_dictionary_set_string_nocopy(&conn->res, "TIMEOUT", bg_sprintf("Second-%d", seconds));
  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Got new event subscription: uuid: %s, url: %s, timeout: %d",
         es->uuid, es->url, seconds);

  bg_http_connection_write_res(conn);

  event = create_event(s, &len, 1, GAVL_TIME_UNDEFINED);
  //  fprintf(stderr, "%s", event);

  send_event(es, event, len);

  result = 1;
  
  fail:

  if(!result)
    {
    if(es->url)
      free(es->url);
    bg_http_connection_write_res(conn);
    }
  else
    s->num_es++;

  if(event)
    free(event);
  return result;
  }

static int find_subscription(bg_upnp_service_t * s, const char * sid)
  {
  int i;
  for(i = 0; i < s->num_es; i++)
    {
    if(!strcmp(s->es[i].uuid, sid + 5))
      return i;
    }
  return -1;
  }

static void delete_subscription(bg_upnp_service_t * s, int idx)
  {
  if(s->es[idx].url)
    free(s->es[idx].url);
  if(idx < s->num_es - 1)
    memmove(s->es + idx, s->es + idx + 1, (s->num_es - 1 - idx) * sizeof(*s->es));
  s->num_es--;
  }

int
bg_upnp_service_handle_event_request(bg_upnp_service_t * s,
                                     bg_http_connection_t * conn)
  {
  const char * nt;
  const char * callback;
  const char * timeout;
  const char * sid;
  
  nt =       gavl_dictionary_get_string_i(&conn->req, "NT");
  callback = gavl_dictionary_get_string_i(&conn->req, "CALLBACK");
  timeout =  gavl_dictionary_get_string_i(&conn->req, "TIMEOUT");
  sid =      gavl_dictionary_get_string_i(&conn->req, "SID");
  
  if(!strcmp(conn->method, "SUBSCRIBE"))
    {
    if(nt && callback && !sid) // New subscription
      {
      if(!strcmp(nt, "upnp:event"))
        {
        add_subscription(s, conn, callback, timeout);
        return 1;
        }
      else
        {
        bg_http_connection_init_res(conn, "HTTP/1.1", 412, "Precondition Failed");
        }
      }
    else if(!nt && !callback && sid) // Renew subscription
      {
      int seconds = 1800;
      int idx = find_subscription(s, sid);

      if(idx < 0)
        {
        gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Subscription %s not found", sid);
        bg_http_connection_init_res(conn, "HTTP/1.1", 412, "Precondition Failed");
        }
      else if(timeout && !get_timeout_seconds(timeout, &seconds))
        bg_http_connection_init_res(conn, "HTTP/1.1", 400, "Bad Request");
      else
        {
        bg_http_connection_init_res(conn, "HTTP/1.1", 200, "Ok");
        s->es[idx].expire_time = gavl_timer_get(s->dev->timer) + seconds * GAVL_TIME_SCALE;
        gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Renewing subscription: uuid: %s, url: %s",
               s->es[idx].uuid, s->es[idx].url);
        }
      }
    else
      {
      bg_http_connection_init_res(conn, "HTTP/1.1", 400, "Bad Request");
      }
    }
  else if(!strcmp(conn->method, "UNSUBSCRIBE"))
    {
    if(sid)
      {
      int idx = find_subscription(s, sid);
      if(idx >= 0)
        {
        gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Got unsubscription: uuid: %s, url: %s",
               s->es[idx].uuid, s->es[idx].url);
        delete_subscription(s, idx);
        bg_http_connection_init_res(conn, "HTTP/1.1", 200, "Ok");
        }
      else
        bg_http_connection_init_res(conn, "HTTP/1.1", 412, "Precondition Failed");
      }
    else
      bg_http_connection_init_res(conn, "HTTP/1.1", 400, "Bad Request");
    }
  else
    bg_http_connection_init_res(conn, "HTTP/1.1", 400, "Bad Request");
  
  bg_http_connection_write_res(conn);
  return 1;
  }

int
bg_upnp_service_ping(bg_upnp_service_t * s,
                     gavl_time_t current_time)
  {
  int i = 0;
  int ret = 0;
  char * event;
  int event_len = 0;
  
  while(i < s->num_es)
    {
    //    fprintf(stderr, "Checking subscription: %"PRId64" %"PRId64"\n",
    //            s->es[i].expire_time, current_time);
    if(s->es[i].expire_time < current_time)
      {
      gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Removing expired subscription %s",
             s->es[i].uuid);
      delete_subscription(s, i);    
      ret++;
      }
    else
      i++;
    }

  /* Check wether to send events */
  
  if((event = create_event(s, &event_len, 0, current_time)))
    {
    i = 0;

    while(i < s->num_es)
      {
      if(!send_event(&s->es[i], event, event_len))
        {
        gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Deleting subscription %s", s->es[i].uuid);
        delete_subscription(s, i);
        }
      else
        i++;
      }
    
    free(event);
    ret++;
    }
  return ret;
  }
