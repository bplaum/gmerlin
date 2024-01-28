#include <uuid/uuid.h>

#include <unistd.h>
#include <string.h>

#include <config.h>

#include <gmerlin/upnp/event.h>

#include <gmerlin/xmlutils.h>
#include <gmerlin/utils.h>
#include <gmerlin/http.h>
#include <gmerlin/bgplug.h>

#include <gmerlin/upnp/upnputils.h>
#include <gmerlin/upnp/soap.h>

#include <gmerlin/translation.h>
#include <gmerlin/log.h>
#define LOG_DOMAIN "upnp:event"

#include <gavl/metatags.h>

/* Common routines */

/*
   dict
     {
     vars
       { 
       name1
         {
         val:                  "5" // Always string
         
         // The following are optional for moderated variables
         changed:               1;
         last_event:     94358974;
         event_interval:   100000;
         }
       name2
         {

         }
       name3
         {
         
         }
       }

     subscriptions
       [
         {
         GAVL_META_URL: return_url
         GAVL_META_ID:  uuid
         counter:       189
         expire_time:   983470247;
         }
       
       ]
       
     }

*/

#define CLIENT_TIMEOUT 500

#define SERVER_VARS           "vars"
#define SERVER_SUBSCRIPTIONS  "subscriptions"

#define SERVER_LAST_EVENT     "last_event"
#define SERVER_EVENT_INTERVAL "event_interval"
#define SERVER_VALUE          "value"
#define SERVER_VALUE_CHANGED  "changed"
#define SERVER_COUNTER        "counter"
#define SERVER_EXPIRE_TIME    "expire_time"

static gavl_dictionary_t * server_get_vars_nc(gavl_dictionary_t * dict)
  {
  return gavl_dictionary_get_dictionary_create(dict, SERVER_VARS);
  }

static const gavl_dictionary_t * server_get_vars(const gavl_dictionary_t * dict)
  {
  return gavl_dictionary_get_dictionary(dict, SERVER_VARS);
  }

static gavl_array_t * server_get_subscriptions(gavl_dictionary_t * dict)
  {
  return gavl_dictionary_get_array_create(dict, SERVER_SUBSCRIPTIONS);
  }

static int do_send_event(const gavl_dictionary_t * var,
                         gavl_time_t current_time, int force,
                         int * last_change)
  {
  gavl_time_t last_event = GAVL_TIME_UNDEFINED;
  gavl_time_t interval = GAVL_TIME_UNDEFINED;
  
  int changed = 0;
  
  if(last_change)
    *last_change = 0;
  
  if(!force && (!gavl_dictionary_get_int(var, SERVER_VALUE_CHANGED, &changed) || !changed))
    return 0;
  
  if(gavl_dictionary_get_long(var, SERVER_EVENT_INTERVAL, &interval))
    {
    if(!force && (gavl_dictionary_get_long(var, SERVER_LAST_EVENT, &last_event) &&
                  (current_time < last_event + interval)))
      return 0;
    
    if(last_change)
      *last_change = 1;
    }
  
  return 1;
  }

/* Create xml string for the event to send out */

static char * create_event(gavl_dictionary_t * dict, int * len, int force, gavl_time_t current_time)
  {
  char * ret;
  int i;
  int last_change = 0;
  xmlNodePtr propset;
  xmlNodePtr node;
  xmlNsPtr ns;
  xmlDocPtr doc;
  char * buf = NULL;
  int do_send = 0;
  int do_send_last_change = 0;

  gavl_dictionary_t * vars = server_get_vars_nc(dict);
  gavl_dictionary_t * var;
  const char * val;

  //  if(force)
  //    fprintf(stderr, "create_event: %d %d dict: %p vars: %p\n", force, vars->num_entries, dict, vars);
  
  /* Indirect events */

  for(i = 0; i < vars->num_entries; i++)
    {
    if(!(var = gavl_value_get_dictionary_nc(&vars->entries[i].v)))
      continue;
    
    if(do_send_event(var, current_time, force, &last_change))
      {
      do_send = 1;
      if(last_change)
        {
        do_send_last_change = 1;
        break;
        }

      }
    }

  if(!do_send)
    return NULL;
  
  //    bg_upnp_sv_val_t val;
  
  if(do_send_last_change)
    {
    xmlNodePtr root;
    doc = xmlNewDoc((xmlChar*)"1.0");

    root = xmlNewDocRawNode(doc, NULL, (xmlChar*)"Event", NULL);
    xmlDocSetRootElement(doc, root);

    root = xmlNewChild(root, NULL, (xmlChar*)"InstanceID", NULL);
    BG_XML_SET_PROP(root, "val", "0");
      
    for(i = 0; i < vars->num_entries; i++)
      {
      if(!(var = gavl_value_get_dictionary_nc(&vars->entries[i].v)))
        continue;
      
      if(do_send_event(var, current_time, force, &last_change) &&
         last_change &&
         (val = gavl_dictionary_get_string(var, SERVER_VALUE)))
        {
        //   fprintf(stderr, "Variable changed: %s %s\n", vars->entries[i].name, val);
        
        node = xmlNewChild(root, NULL, (xmlChar*)vars->entries[i].name, NULL);

        if(!strcmp(val, BG_SOAP_ARG_EMPTY))
          BG_XML_SET_PROP(node, "val", "");
        else
          BG_XML_SET_PROP(node, "val", val);
        
        if(!force)
          {
          gavl_dictionary_set_int(var, SERVER_VALUE_CHANGED, 0);
          gavl_dictionary_set_long(var, SERVER_LAST_EVENT, current_time);
          }
        }
      }
    buf = bg_xml_save_to_memory(doc);
    xmlFreeDoc(doc);
    
    /* Set LastChange variable */

    var = gavl_dictionary_get_dictionary_create(vars, "LastChange");
    gavl_dictionary_set_string(var, SERVER_VALUE, buf);
    gavl_dictionary_set_int(var, SERVER_VALUE_CHANGED, 1);

    // fprintf(stderr, "Got last change: %s\n", buf);
    
    free(buf);
    }
  
  /* Send an event for *all* variables */
  doc = xmlNewDoc((xmlChar*)"1.0");
  propset = xmlNewDocRawNode(doc, NULL, (xmlChar*)"propertyset", NULL);
  xmlDocSetRootElement(doc, propset);
  ns = xmlNewNs(propset,
                (xmlChar*)"urn:schemas-upnp-org:event-1-0",
                (xmlChar*)"e");
  xmlSetNs(propset, ns);
  
  for(i = 0; i < vars->num_entries; i++)
    {
    if(!(var = gavl_value_get_dictionary_nc(&vars->entries[i].v)))
      continue;
    
    if(!do_send_event(var, current_time, force, &last_change) || last_change)
      continue;
    if(!force)
      {
      gavl_dictionary_set_int(var, SERVER_VALUE_CHANGED, 0);
      gavl_dictionary_set_long(var, SERVER_LAST_EVENT, current_time);
      }
    
    node = xmlNewChild(propset, ns, (xmlChar*)"property", NULL);
    
    val = gavl_dictionary_get_string(var, SERVER_VALUE);
    
    if(!strcmp(val, BG_SOAP_ARG_EMPTY))
      val = NULL;
    
    node = xmlNewChild(node, NULL, (xmlChar*)vars->entries[i].name,
                       (xmlChar*)(val ? val : ""));
    xmlSetNs(node, NULL);
    }
  ret = bg_xml_save_to_memory(doc);
  *len = strlen(ret);
  xmlFreeDoc(doc);
  return ret;
  }

static int send_event(gavl_dictionary_t * es,
                      char * event, int len)
  {
  gavl_dictionary_t m;
  gavl_socket_address_t * addr = NULL;
  char * path = NULL;
  char * host = NULL;
  int port;
  int fd = -1;
  char * tmp_string;
  int result = 0;
  const char * uri;
  const char * uuid;
  int64_t key = 0;

  gavf_io_t * io = NULL;
  
  gavl_dictionary_init(&m);

  uri = gavl_dictionary_get_string(es, GAVL_META_URI);
  uuid = gavl_dictionary_get_string(es, GAVL_META_ID );
  gavl_dictionary_get_long(es, SERVER_COUNTER, &key);
  
  /* Parse URL */
  if(!bg_url_split(uri,
                   NULL, NULL, NULL,
                   &host, &port, &path))
    goto fail;
  
  addr = gavl_socket_address_create();

  if(!gavl_socket_address_set(addr, host, port, SOCK_STREAM))
    goto fail;
  
  if((fd = gavl_socket_connect_inet(addr, 500)) < 0)
    goto fail;

  io = gavf_io_create_socket(fd, CLIENT_TIMEOUT, GAVF_IO_SOCKET_DO_CLOSE);
  fd = -1;
  
  if(path)
    gavl_http_request_init(&m, "NOTIFY", path, "HTTP/1.1");
  else
    gavl_http_request_init(&m, "NOTIFY", "/", "HTTP/1.1");
    
  tmp_string = bg_sprintf("%s:%d", host, port);
  gavl_dictionary_set_string(&m, "HOST", tmp_string);
  free(tmp_string);
  
  gavl_dictionary_set_string(&m, "CONTENT-TYPE", "text/xml");
  gavl_dictionary_set_int(&m, "CONTENT-LENGTH", len);
  gavl_dictionary_set_string(&m, "NT", "upnp:event");
  gavl_dictionary_set_string(&m, "NTS", "upnp:propchange");
  tmp_string = bg_sprintf("uuid:%s", uuid);  
  gavl_dictionary_set_string(&m, "SID", tmp_string);
  free(tmp_string);
  gavl_dictionary_set_long(&m, "SEQ", key++);

  if(key > 0x100000000LL)
    key = 0;

  gavl_dictionary_set_long(es, SERVER_COUNTER, key);
  
  //  fprintf(stderr, "Sending event: %s (%d %d)\n", es->url, len, strlen(event));
  //  gavl_dictionary_dump(&m, 0);
  //  fprintf(stderr, "%s\n", event);
  
  if(!gavl_http_request_write(io, &m))
    {
    goto fail;
    }
  if(!gavf_io_write_data(io, (uint8_t*)event, len))
    {
    goto fail;
    }
  gavl_dictionary_reset(&m);
  
  if(!gavl_http_response_read(io, &m))
    {
    // Some weird clients send no reply
    result = 1;
    goto fail;
    }
  //  fprintf(stderr, "Got response:\n");
  //  gavl_dictionary_dump(&m, 0);

  if(gavl_http_response_get_status_int(&m) != 200)
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
    gavl_socket_address_destroy(addr);

  if(io)
    gavf_io_destroy(io);
  
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


/* Add subscription */

static int add_subscription(gavl_dictionary_t * dict,
                            bg_http_connection_t * conn,
                            const char * callback, const char * timeout)
  {
  const char * start, *end;
  uuid_t uuid;
  int seconds;
  int result = 0;
  char * event = NULL;
  int len;
  gavl_value_t val;
  gavl_array_t * arr;
  char uuid_str[37];
  gavl_dictionary_t * s;
  gavl_time_t current_time = gavl_time_get_monotonic();
  arr = server_get_subscriptions(dict);

  gavl_value_init(&val);
  s = gavl_value_set_dictionary(&val);
  
  /* UUID */
  uuid_generate(uuid);
  uuid_unparse(uuid, uuid_str);

  gavl_dictionary_set_string(s, GAVL_META_ID, uuid_str);
  
  /* Timeout */

  if(!get_timeout_seconds(timeout, &seconds))
    {
    bg_http_connection_init_res(conn, "HTTP/1.1", 400, "Bad Request");
    goto fail;
    }
  
  gavl_dictionary_set_long(s, SERVER_EXPIRE_TIME, current_time + seconds * GAVL_TIME_SCALE);
  
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

  gavl_dictionary_set_string_nocopy(s, GAVL_META_URI, gavl_strndup(start, end));
  
  bg_http_connection_init_res(conn, "HTTP/1.1", 200, "OK");
  gavl_dictionary_set_string_nocopy(&conn->res, "SERVER", bg_upnp_make_server_string());
  gavl_dictionary_set_string_nocopy(&conn->res, "SID", bg_sprintf("uuid:%s", uuid_str));
  gavl_dictionary_set_string_nocopy(&conn->res, "TIMEOUT", bg_sprintf("Second-%d", seconds));

  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Got new event subscription: uuid: %s, url: %s, timeout: %d",
         uuid_str, gavl_dictionary_get_string(s, GAVL_META_URI), seconds);

  bg_http_connection_write_res(conn);

  len = 0;

  //  fprintf(stderr, "Add subscription\n");
  //  gavl_dictionary_dump(s, 2);
  
  if((event = create_event(dict, &len, 1, GAVL_TIME_UNDEFINED)))
    send_event(s, event, len);
  
  result = 1;
  
  fail:

  if(!result)
    {
    gavl_value_free(&val);
    bg_http_connection_write_res(conn);
    }
  else
    gavl_array_splice_val_nocopy(arr, -1, 0, &val);
  
  if(event)
    free(event);
  return result;
  }

static int find_subscription(gavl_dictionary_t * dict, const char * sid)
  {
  int i;
  gavl_array_t * arr;
  const char * val;
  
  arr = server_get_subscriptions(dict);
  
  for(i = 0; i < arr->num_entries; i++)
    {
    if((dict = gavl_value_get_dictionary_nc(&arr->entries[i])) &&
       (val = gavl_dictionary_get_string(dict, GAVL_META_ID)) &&
       !strcmp(val, sid + 5))
      return i;
    }
  return -1;
  }

static int server_handle_http(bg_http_connection_t * conn, void * data)
  {
  gavl_dictionary_t * dict = data;

  const char * nt;
  const char * callback;
  const char * timeout;
  const char * sid;
  
  gavl_array_t * arr = server_get_subscriptions(dict);
  
  nt =       gavl_dictionary_get_string_i(&conn->req, "NT");
  callback = gavl_dictionary_get_string_i(&conn->req, "CALLBACK");
  timeout =  gavl_dictionary_get_string_i(&conn->req, "TIMEOUT");
  sid =      gavl_dictionary_get_string_i(&conn->req, "SID");

  /* Handle missing timeout gracefully */
  if(!timeout)
    timeout = "Second-1800";
  
  if(!strcmp(conn->method, "SUBSCRIBE"))
    {
    if(nt && callback && !sid) // New subscription
      {
      if(!strcmp(nt, "upnp:event"))
        {
        
        add_subscription(dict, conn, callback, timeout);
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
      int idx = find_subscription(dict, sid);
      
      if(idx < 0)
        {
        gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Subscription %s not found", sid);
        bg_http_connection_init_res(conn, "HTTP/1.1", 412, "Precondition Failed");
        }
      else if(timeout && !get_timeout_seconds(timeout, &seconds))
        bg_http_connection_init_res(conn, "HTTP/1.1", 400, "Bad Request");
      else
        {
        gavl_dictionary_t * es;

        bg_http_connection_init_res(conn, "HTTP/1.1", 200, "Ok"); 
        
        es = gavl_value_get_dictionary_nc(&arr->entries[idx]);
        gavl_dictionary_set_long(es, SERVER_EXPIRE_TIME,
                                 conn->current_time + seconds * GAVL_TIME_SCALE);
        gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Renewing subscription: uuid: %s, url: %s",
               gavl_dictionary_get_string(es, GAVL_META_ID),
               gavl_dictionary_get_string(es, GAVL_META_URI));
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
      int idx = find_subscription(dict, sid);
      if(idx >= 0)
        {
        gavl_dictionary_t * es;
        es = gavl_value_get_dictionary_nc(&arr->entries[idx]);
        
        gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Got unsubscription: uuid: %s, url: %s",
               gavl_dictionary_get_string(es, GAVL_META_ID),
               gavl_dictionary_get_string(es, GAVL_META_URI));

        gavl_array_splice_val(arr, idx, 1, NULL);
        
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
  
void bg_upnp_event_context_init_server(gavl_dictionary_t * dict,
                                      const char * path)
  {
  server_get_vars_nc(dict);
  server_get_subscriptions(dict);

  bg_http_server_add_handler(bg_http_server_get(), server_handle_http, BG_HTTP_PROTO_HTTP, path, dict);
  }

void bg_upnp_event_context_server_set_value(gavl_dictionary_t * dict, const char * name,
                                            const char * val,
                                            gavl_time_t update_interval)
  {
  const char  * old_value;
  gavl_dictionary_t * vars;
  gavl_dictionary_t * var;

  
  vars = server_get_vars_nc(dict);
  var = gavl_dictionary_get_dictionary_create(vars, name);
  
  if((old_value = gavl_dictionary_get_string(var, SERVER_VALUE)) &&
     !strcmp(old_value, val))
    return;

  gavl_dictionary_set_string(var, SERVER_VALUE, val);
  gavl_dictionary_set_int(var, SERVER_VALUE_CHANGED, 1);
  
  if(update_interval != GAVL_TIME_UNDEFINED)
    {
    /* Send events not immediately */
    gavl_dictionary_set_long(var, SERVER_EVENT_INTERVAL, update_interval);
    }
  
  }

const char * bg_upnp_event_context_server_get_value(const gavl_dictionary_t * dict, const char * name)
  {
  const gavl_dictionary_t * vars;
  const gavl_dictionary_t * var;
  
  if((vars = server_get_vars(dict)) &&
     (var = gavl_dictionary_get_dictionary(vars, name)))
    return gavl_dictionary_get_string(var, SERVER_VALUE);
  else
    return NULL;
  }

/* Send moderate events */

int bg_upnp_event_context_server_update(gavl_dictionary_t * dict)


  {
  int i = 0;
  int ret = 0;
  char * event;
  int event_len = 0;
  gavl_array_t * arr;
  gavl_dictionary_t * es;

  gavl_time_t expire_time;

  gavl_time_t current_time = gavl_time_get_monotonic();
  
  arr = server_get_subscriptions(dict);
  
  while(i < arr->num_entries)
    {
    //    fprintf(stderr, "Checking subscription: %"PRId64" %"PRId64"\n",
    //            s->es[i].expire_time, current_time);

    es = gavl_value_get_dictionary_nc(&arr->entries[i]);

    if(gavl_dictionary_get_long(es, SERVER_EXPIRE_TIME, &expire_time) &&
       (expire_time < current_time))
      {
      gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Removing expired subscription %s",
             gavl_dictionary_get_string(es, GAVL_META_ID));
      gavl_array_splice_val(arr, i, 1, NULL);
      ret++;
      }
    else
      i++;
    }

  /* Check wether to send events */
  
  if((event = create_event(dict, &event_len, 0, current_time)))
    {
    i = 0;

    //    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Sending event");
    
    //    fprintf(stderr, "Sending event: %s\n", event);
    
    while(i < arr->num_entries)
      {
      es = gavl_value_get_dictionary_nc(&arr->entries[i]);
      
      if(!send_event(es, event, event_len))
        {
        gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Deleting subscription %s",
               gavl_dictionary_get_string(es, GAVL_META_ID));
        
        gavl_array_splice_val(arr, i, 1, NULL);
        }
      else
        i++;
      }
    
    free(event);
    ret++;
    }
  return ret;
  
  }


/*
 *
 */

