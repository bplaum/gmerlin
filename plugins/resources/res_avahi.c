#include <string.h>
// #include <glob.h>
#include <unistd.h>

#include <config.h>
#include <gmerlin/translation.h>

#include <gmerlin/plugin.h>
#include <gmerlin/bgdbus.h>
#include <gmerlin/backend.h>
#include <gmerlin/resourcemanager.h>

#include <gavl/log.h>
#define LOG_DOMAIN "avahi"

#include <gavl/utils.h>


#define MSG_ID_AVAHI_SERVICE_ADDED   1
#define MSG_ID_AVAHI_SERVICE_REMOVED 2

/* txt fields for gmerlin resources */
#define TXT_CLASS    "class"
#define TXT_PATH     "path"
#define TXT_PROTOCOL "protocol"
#define TXT_HASH     "hash"


 enum {
     AVAHI_PROTO_INET = 0,     
     AVAHI_PROTO_INET6 = 1,   
     AVAHI_PROTO_UNSPEC = -1  
 };

#define TYPE_MPD               "_mpd._tcp"
// #define TYPE_PULSEAUDIO_SINK   "_pulse-sink._tcp"
#define TYPE_GMERLIN           "_gmerlin._tcp"

//#define TYPE_PULSEAUDIO_SOURCE "_pulse-source._tcp"

typedef struct
  {
  bg_controllable_t ctrl;

  bg_dbus_connection_t * conn;
  bg_msg_sink_t * dbus_sink;

  char * avahi_addr;
  
  int initialized;

  char * mpd_browser;
  char * gmerlin_browser;

  gavl_dictionary_t entry_groups;
  
  } avahi_t;

static void publish_local(avahi_t * a, const char * id, const gavl_dictionary_t * dict);

static int handle_msg(void * priv, gavl_msg_t * msg)
  {
  avahi_t * a = priv;
  switch(msg->NS)
    {
    case GAVL_MSG_NS_GENERIC:
      
      switch(msg->ID)
        {
        case GAVL_MSG_RESOURCE_ADDED:
          {
          const char * id;
          gavl_dictionary_t dict;
          // fprintf(stderr, "Got resource to publish\n");
          
          id = gavl_dictionary_get_string(&msg->header, GAVL_MSG_CONTEXT_ID);
          gavl_dictionary_init(&dict);
          gavl_msg_get_arg_dictionary(msg, 0, &dict);
          publish_local(a, id, &dict);
          gavl_dictionary_free(&dict);
          }
          break;
        case GAVL_MSG_RESOURCE_DELETED:
          {
          const char * entry_group;
          const char * id;
          DBusMessage * req;
          gavl_msg_t * res;
          
          id = gavl_dictionary_get_string(&msg->header, GAVL_MSG_CONTEXT_ID);

          if(!(entry_group = gavl_dictionary_get_string(&a->entry_groups, id)))
            return 1;
          
          fprintf(stderr, "Got resource to unpublish\n");

          req = dbus_message_new_method_call(a->avahi_addr, entry_group, "org.freedesktop.Avahi.EntryGroup",
                                             "Free");
          res = bg_dbus_connection_call_method(a->conn, req);
          dbus_message_unref(req);
          gavl_msg_destroy(res);
          }
          break;
        }
      break;
    }
  
  return 1;
  }


static int dbus_callback_avahi(void * priv, gavl_msg_t * msg)
  {
  avahi_t * a = priv;
  
  
  switch(msg->NS)
    {
    case BG_MSG_NS_PRIVATE:
      switch(msg->ID)
        {
        case MSG_ID_AVAHI_SERVICE_ADDED:
          {
          gavl_dictionary_t info;
          DBusMessage * req;
          gavl_msg_t * res;
          gavl_msg_t * msg1;
          
          int32_t protocol;
          int32_t interface;
          const char * name;
          const char * type;
          const char * domain;
          uint32_t flags = 0;
          int32_t aprotocol = AVAHI_PROTO_UNSPEC;
          char * uri = NULL;
          const char * addr;
          int port;

          gavl_array_t txt_arr;
          gavl_dictionary_t txt_dict;
          gavl_array_init(&txt_arr);
          gavl_dictionary_init(&txt_dict);
          
          //          fprintf(stderr, "Service added\n");
          //          gavl_msg_dump(msg, 2);
          
          interface = gavl_msg_get_arg_int(msg, 0);
          protocol = gavl_msg_get_arg_int(msg, 1);

          name = gavl_msg_get_arg_string_c(msg, 2);
          type = gavl_msg_get_arg_string_c(msg, 3);
          domain = gavl_msg_get_arg_string_c(msg, 4);
          
          gavl_dictionary_init(&info);
          
          req = dbus_message_new_method_call(a->avahi_addr,
                                             "/",
                                             "org.freedesktop.Avahi.Server",
                                             "ResolveService");

          dbus_message_append_args(req,
                                   DBUS_TYPE_INT32, &interface, // Interface
                                   DBUS_TYPE_INT32, &protocol, // Protocol
                                   DBUS_TYPE_STRING, &name,
                                   DBUS_TYPE_STRING, &type,
                                   DBUS_TYPE_STRING, &domain,
                                   DBUS_TYPE_INT32, &aprotocol, // Protocol
                                   DBUS_TYPE_UINT32, &flags,
                                   DBUS_TYPE_INVALID);

          if(!(res = bg_dbus_connection_call_method(a->conn, req)))
            {
            dbus_message_unref(req);
            break;
            }
          dbus_message_unref(req);

          //          fprintf(stderr, "Service resolved\n");
          //          gavl_msg_dump(res, 2);
          
          addr = gavl_msg_get_arg_string_c(res, 7);
          port = gavl_msg_get_arg_int(res, 8);

          if(gavl_msg_get_arg_array(res, 9, &txt_arr))
            {
            const char * str;
            char ** arr;

            int i;
            for(i = 0; i < txt_arr.num_entries; i++)
              {
              if((str = gavl_value_get_string(&txt_arr.entries[i])))
                {
                arr = gavl_strbreak(str, '=');
                if(arr && arr[0])
                  gavl_dictionary_set_string(&txt_dict, arr[0], arr[1]);
                gavl_strbreak_free(arr);
                }
              }

            //            fprintf(stderr, "Got txt:\n");
            //            gavl_dictionary_dump(&txt_dict, 2);
            }
          
          //            gavl_dictionary_set_string(&info, BG_BACKEND_PROTOCOL, "mpd");

          if(!strcmp(type, TYPE_MPD))
            {
            switch(protocol)
              {
              case AVAHI_PROTO_INET: // IPV4
                uri = gavl_sprintf("%s://%s:%d", BG_MPD_URI_SCHEME, addr, port);
                break;
              case AVAHI_PROTO_INET6: // IPV6
                uri = gavl_sprintf("%s://[%s]:%d", BG_MPD_URI_SCHEME, addr, port);
                break;
              }
          
            if(!uri)
              {
              break;
              }
            
            gavl_dictionary_set_string(&info, GAVL_META_CLASS, GAVL_META_CLASS_BACKEND_RENDERER);
            gavl_metadata_add_image_uri(&info,
                                        GAVL_META_ICON_URL,
                                        -1, -1,
                                        "image/png",
                                        "https://www.musicpd.org/logo.png");
            gavl_dictionary_set_string_nocopy(&info, GAVL_META_URI, uri);

            gavl_dictionary_set_int(&info, BG_RESOURCE_PRIORITY, BG_RESOURCE_PRIORITY_DEFAULT);
            gavl_dictionary_set_string(&info, GAVL_META_LABEL, name);
            }
          else if(!strcmp(type, TYPE_GMERLIN))
            {
            const char * proto_str;
            const char * path;
            
            gavl_dictionary_set_string(&info, GAVL_META_CLASS,
                                       gavl_dictionary_get_string(&txt_dict, TXT_CLASS) );

            proto_str = gavl_dictionary_get_string(&txt_dict, TXT_PROTOCOL);
            path = gavl_dictionary_get_string(&txt_dict, TXT_PATH);

            gavl_dictionary_set(&info, GAVL_META_HASH, gavl_dictionary_get(&txt_dict, TXT_HASH));
            
            if(proto_str && path)
              {
              switch(protocol)
                {
                case AVAHI_PROTO_INET: // IPV4
                  uri = gavl_sprintf("%s://%s:%d%s", proto_str, addr, port, path);
                  break;
                case AVAHI_PROTO_INET6: // IPV6
                  uri = gavl_sprintf("%s://[%s]:%d%s", proto_str, addr, port, path);
                  break;
                }
              }
            gavl_dictionary_set_int(&info, BG_RESOURCE_PRIORITY, BG_RESOURCE_PRIORITY_MAX);
            gavl_dictionary_set_string_nocopy(&info, GAVL_META_URI, uri);
            bg_backend_get_node_info(&info);
            }
          
          if(uri)
            {
            msg1 = bg_msg_sink_get(a->ctrl.evt_sink);
          
            gavl_msg_set_id_ns(msg1, GAVL_MSG_RESOURCE_ADDED, GAVL_MSG_NS_GENERIC);
            gavl_dictionary_set_string_nocopy(&msg1->header, GAVL_MSG_CONTEXT_ID, gavl_sprintf("avahi:%s", name));
            gavl_msg_set_arg_dictionary(msg1, 0, &info);
          
            bg_msg_sink_put(a->ctrl.evt_sink);
            }
          
          gavl_dictionary_free(&info);
          gavl_array_free(&txt_arr);
          gavl_dictionary_free(&txt_dict);
          
          gavl_msg_destroy(res);
          
          }
          break;
        case MSG_ID_AVAHI_SERVICE_REMOVED:
          {
          gavl_msg_t * msg1;
          const char * name = gavl_msg_get_arg_string_c(msg, 2);

          msg1 = bg_msg_sink_get(a->ctrl.evt_sink);
          
          gavl_msg_set_id_ns(msg1, GAVL_MSG_RESOURCE_DELETED, GAVL_MSG_NS_GENERIC);
          gavl_dictionary_set_string_nocopy(&msg1->header, GAVL_MSG_CONTEXT_ID, gavl_sprintf("avahi:%s", name));
          bg_msg_sink_put(a->ctrl.evt_sink);
          
          }
          break;
        }
      break;
      
    }
  return 1;
  }

static char * create_service_browser(avahi_t * a, const char * type)
  {
  char * ret = NULL;
  DBusMessage * req;
  gavl_msg_t * res;

  const char * domain = "local";
  int32_t interface = -1;
  int32_t protocol = AVAHI_PROTO_UNSPEC;
  uint32_t flags = 0;
  const char * var;
    
  /* Create browser for mpd */
  req = dbus_message_new_method_call(a->avahi_addr, "/", "org.freedesktop.Avahi.Server",
                                     "ServiceBrowserNew");
    
  dbus_message_append_args(req,
                           DBUS_TYPE_INT32, &interface, // Interface
                           DBUS_TYPE_INT32, &protocol, // Protocol
                           DBUS_TYPE_STRING, &type,
                           DBUS_TYPE_STRING, &domain,
                           DBUS_TYPE_UINT32, &flags,
                           DBUS_TYPE_INVALID);
    
  res = bg_dbus_connection_call_method(a->conn, req);
  dbus_message_unref(req);
  
  if((var = gavl_msg_get_arg_string_c(res, 0)))
    {
    char * rule;
    char * rule_common = gavl_sprintf("sender='%s',type='signal',path='%s',interface='org.freedesktop.Avahi.ServiceBrowser'",
                                      a->avahi_addr, var);
      
    ret = gavl_strdup(var);
    gavl_log(GAVL_LOG_DEBUG, LOG_DOMAIN, "Created service browser %s", ret);
      
    rule = gavl_sprintf("%s,member='ItemNew'", rule_common);
    bg_dbus_connection_add_listener(a->conn,
                                    rule,
                                    a->dbus_sink,
                                    BG_MSG_NS_PRIVATE, MSG_ID_AVAHI_SERVICE_ADDED);
    //    fprintf(stderr, "Added listener: %s\n", rule);
    free(rule);

    rule = gavl_sprintf("%s,member='ItemRemove'", rule_common);
    bg_dbus_connection_add_listener(a->conn,
                                    rule,
                                    a->dbus_sink,
                                    BG_MSG_NS_PRIVATE, MSG_ID_AVAHI_SERVICE_REMOVED);
    //    fprintf(stderr, "Added listener: %s\n", rule);
    free(rule);
    free(rule_common);
    }
    
  gavl_msg_destroy(res);

  return ret;
  }

static void append_txt_tag(DBusMessageIter * iter, const char * tag, const char * val)
  {
  DBusMessageIter subiter;
  char * str = gavl_sprintf("%s=%s", tag, val);

  dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY, "y", &subiter);
  dbus_message_iter_append_fixed_array(&subiter, DBUS_TYPE_BYTE, &str, strlen(str));
  dbus_message_iter_close_container(iter, &subiter);
  free(str);
  }

static void publish_local(avahi_t * a, const char * id, const gavl_dictionary_t * dict)
  {
  char * protocol = NULL;
  char hostname_buf[HOST_NAME_MAX+1];
  char * path = NULL;
  int port = -1;
  DBusMessage * req;
  gavl_msg_t * res;
  char * entry_group = NULL;
  char * host = NULL;
  const char * uri;
  const char * klass;
  const char * type = TYPE_GMERLIN;
  const char * domain = "local";
  gavl_socket_address_t * addr;
  int32_t avahi_protocol;
  int gavl_protocol;
  int32_t interface;
  uint32_t flags = 0;
  char * name = NULL;
  DBusMessageIter iter, subiter;
  char * hostname = NULL;
  
  /* Warning: This breaks if the MDNS host differs from the host name */
  gethostname(hostname_buf, HOST_NAME_MAX+1);
  
  if(!(uri = gavl_dictionary_get_string(dict, GAVL_META_URI)))
    return;

  if(!(klass = gavl_dictionary_get_string(dict, GAVL_META_CLASS)))
    return;

  if(!gavl_url_split(uri, &protocol, NULL, NULL, &host, &port, &path))
    goto fail;
  
  /* We set a name to be unique. Applications will display a nice label instead */
  
  name = gavl_sprintf("gmerlin-%s", gavl_dictionary_get_string(dict, GAVL_META_HASH));
  
  /* We publish only selected protocols */
  if(strcmp(protocol, BG_BACKEND_URI_SCHEME_GMERLIN_RENDERER) &&
     strcmp(protocol, BG_BACKEND_URI_SCHEME_GMERLIN_MDB))
    goto fail;

  /* Obtain interface name */
  addr = gavl_socket_address_create();

  gavl_socket_address_set(addr, host, 0, SOCK_STREAM);
  interface = gavl_interface_index_from_address(addr);

  gavl_protocol = gavl_socket_address_get_address_family(addr);
  switch(gavl_protocol)
    {
    case AF_INET:
      avahi_protocol = AVAHI_PROTO_INET;
      break;
    case AF_INET6:
      avahi_protocol = AVAHI_PROTO_INET6;
      break;
    default:
      goto fail;
    }
  
  gavl_socket_address_destroy(addr);
  
  req = dbus_message_new_method_call(a->avahi_addr, "/", "org.freedesktop.Avahi.Server",
                                     "EntryGroupNew");
  
  
  res = bg_dbus_connection_call_method(a->conn, req);
  dbus_message_unref(req);

  if(!res)
    goto fail;

  entry_group = gavl_strdup(gavl_msg_get_arg_string_c(res, 0));

  gavl_dictionary_set_string(&a->entry_groups, id, entry_group);

  
  gavl_msg_destroy(res);
  
  
  //  fprintf(stderr, "Got entry group: %s\n", entry_group);
  //  fprintf(stderr, "Got interface index: %d\n", interface);

  hostname = gavl_sprintf("%s.local", hostname_buf);
  
  //   = NULL;
  
  //  fprintf(stderr, "Got hostname: %s\n", hostname);
  
  req = dbus_message_new_method_call(a->avahi_addr, entry_group, "org.freedesktop.Avahi.EntryGroup",
                                     "AddService");

  dbus_message_append_args(req,
                           DBUS_TYPE_INT32, &interface,      // interface
                           DBUS_TYPE_INT32, &avahi_protocol, // protocol
                           DBUS_TYPE_UINT32, &flags,         // flags
                           DBUS_TYPE_STRING, &name,          // name
                           DBUS_TYPE_STRING, &type,          // type
                           DBUS_TYPE_STRING, &domain,        // domain
                           DBUS_TYPE_STRING, &hostname,      // host
                           DBUS_TYPE_UINT16, &port,
                           DBUS_TYPE_INVALID);

  /* txt */
    
  dbus_message_iter_init_append(req, &iter);
  dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "ay", &subiter);

  append_txt_tag(&subiter, TXT_CLASS, klass);
  append_txt_tag(&subiter, TXT_PROTOCOL, protocol);
  append_txt_tag(&subiter, TXT_PATH, path);
  append_txt_tag(&subiter, TXT_HASH, gavl_dictionary_get_string(dict, GAVL_META_HASH));
  
  dbus_message_iter_close_container(&iter, &subiter);
  
  res = bg_dbus_connection_call_method(a->conn, req);
  dbus_message_unref(req);

  if(!res)
    goto fail;
  
  gavl_msg_destroy(res);

  //  fprintf(stderr, "Added service\n");
  
  req = dbus_message_new_method_call(a->avahi_addr, entry_group, "org.freedesktop.Avahi.EntryGroup",
                                     "Commit");
  res = bg_dbus_connection_call_method(a->conn, req);
  dbus_message_unref(req);

  if(!res)
    goto fail;

  //  fprintf(stderr, "Committed service\n");

  gavl_msg_destroy(res);
    
  fail:
  if(protocol)
    free(protocol);
  if(name)
    free(name);
  if(host)
    free(host);
  if(hostname)
    free(hostname);
  if(path)
    free(path);

  if(entry_group)
    free(entry_group);
  
  }
  
static void * create_avahi()
  {
  avahi_t * a;

  a = calloc(1, sizeof(*a));
  
  if(!(a->conn =  bg_dbus_connection_get(DBUS_BUS_SYSTEM)) ||
     !(a->avahi_addr = bg_dbus_get_name_owner(a->conn, "org.freedesktop.Avahi")))
    {
    a->conn = NULL;
    return a;
    }
  gavl_log(GAVL_LOG_DEBUG, LOG_DOMAIN, "Found avahi daemon at %s", a->avahi_addr);
  
  a->dbus_sink = bg_msg_sink_create(dbus_callback_avahi, a, 0);

  a->mpd_browser = create_service_browser(a, TYPE_MPD);
  a->gmerlin_browser = create_service_browser(a, TYPE_GMERLIN);
  
  bg_controllable_init(&a->ctrl,
                       bg_msg_sink_create(handle_msg, a, 1),
                       bg_msg_hub_create(1));
  
  return a;
  }

static void destroy_avahi(void * priv)
  {
  avahi_t * a = priv;

  gavl_dictionary_free(&a->entry_groups);
  bg_controllable_cleanup(&a->ctrl);

  bg_dbus_connection_del_listeners(a->conn, a->dbus_sink);
    
  if(a->dbus_sink)
    bg_msg_sink_destroy(a->dbus_sink);

  if(a->mpd_browser)
    free(a->mpd_browser);
  if(a->gmerlin_browser)
    free(a->gmerlin_browser);
  if(a->avahi_addr)
    free(a->avahi_addr);
  
  free(a);
  }
  
static int update_avahi(void * priv)
  {
  int ret = 0;
  avahi_t * a = priv;
  if(!a->conn)
    return 0;
  
  bg_msg_sink_iteration(a->dbus_sink);
  ret += bg_msg_sink_get_num(a->dbus_sink);
  
  return ret;
  }

static bg_controllable_t * get_controllable_avahi(void * priv)
  {
  avahi_t * a = priv;
  return &a->ctrl;
  }


bg_controllable_plugin_t the_plugin =
  {
    .common =
    {
      BG_LOCALE,
      .name =      "res_avahi",
      .long_name = TRS("avahi resource detector"),
      .description = TRS("Retects remote resources via the zeroconf protocol"),
      .type =     BG_PLUGIN_RESOURCE_DETECTOR,
      .flags =    0,
      .create =   create_avahi,
      .destroy =   destroy_avahi,
      .get_controllable =   get_controllable_avahi,
      .priority =         1,
    },
    .update = update_avahi,

  };

/* Include this into all plugin modules exactly once
   to let the plugin loader obtain the API version */
BG_GET_PLUGIN_API_VERSION;
