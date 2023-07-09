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
#include <config.h>

#include <gavl/metatags.h>

#include <gmerlin/bgdbus.h>
#include <gmerlin/backend.h>
#include <gmerlin/utils.h>
#include <gmerlin/state.h>
#include <gmerlin/playermsg.h>
#include <gmerlin/player.h>

#include <gmerlin/translation.h>
#include <gmerlin/log.h>
#define LOG_DOMAIN "dbus_detector"

#include <backend_priv.h>


#define MSG_ID_NAME_OWNER_CHANGED    1
#define MSG_ID_AVAHI_SERVICE_ADDED   2
#define MSG_ID_AVAHI_SERVICE_REMOVED 3

#define TYPE_MPD "_mpd._tcp"

// #define MSG_ID_NAME_OWNER_CHANGED 1

 enum {
     AVAHI_PROTO_INET = 0,     
     AVAHI_PROTO_INET6 = 1,   
     AVAHI_PROTO_UNSPEC = -1  
 };

struct bg_dbus_detector_s
  {
  bg_msg_sink_t * sink;
  
  bg_msg_sink_t * dbus_sink;
  bg_dbus_connection_t * session_conn;
  bg_dbus_connection_t * system_conn;
  
  char * avahi_addr;
  char * mpd_browser;

  /* Keep track of the MPD labels */
  gavl_array_t mpd_labels;
  gavl_array_t mpd_uris;
  
  } ;

static void add_dev(bg_dbus_detector_t * d, const char * addr, const char * name,
                    const char * protocol, bg_backend_type_t type)
  {
  gavl_dictionary_t info;
  
  memset(&info, 0, sizeof(info));
  
  gavl_dictionary_set_string(&info, BG_BACKEND_PROTOCOL, protocol);
  gavl_dictionary_set_int(&info, BG_BACKEND_TYPE, type);

  //  fprintf(stderr, "add_dev %s %s %s %d\n", addr, name, protocol, type);
  
  if(!strcmp(protocol, "mpris2"))
    {
    char * addr_priv = NULL;
    char * str;
    char * real_name = NULL;
    char * uri = NULL;

    if(gavl_string_starts_with(name, "gmerlin-"))
      {
      const char * pos = strrchr(name, '-');
      if(pos && (strlen(pos + 1) == BG_BACKEND_ID_LEN))
        gavl_dictionary_set_string(&info, GAVL_META_ID, pos+1);
      }
    
    real_name = bg_sprintf("%s%s", MPRIS2_NAME_PREFIX, name);

    uri = bg_sprintf("%s://%s", BG_DBUS_MPRIS_URI_SCHEME, real_name + MPRIS2_NAME_PREFIX_LEN);

    if(!bg_backend_by_str(GAVL_META_URI, uri, 1, NULL))
      {
      if(!addr)
        {
        addr_priv = bg_dbus_get_name_owner(d->session_conn, real_name);
        addr = addr_priv;
        }
          
      gavl_dictionary_set_string_nocopy(&info, GAVL_META_URI,
                                        bg_sprintf("%s://%s", BG_DBUS_MPRIS_URI_SCHEME, name));
      
      str = bg_dbus_get_string_property(d->session_conn,
                                        addr,
                                        "/org/mpris/MediaPlayer2",
                                        "org.mpris.MediaPlayer2",
                                        "Identity");
    
      if(str)
        gavl_dictionary_set_string_nocopy(&info, GAVL_META_LABEL, str);

      str = bg_dbus_get_string_property(d->session_conn,
                                        addr,
                                        "/org/mpris/MediaPlayer2",
                                        "org.mpris.MediaPlayer2",
                                        "DesktopEntry");

      if(str)
        {
        char * desktop_file = bg_search_desktop_file(str);

        if(desktop_file)
          {
          const gavl_dictionary_t * s;
          const char * icon;
        
          gavl_dictionary_t dict;
          gavl_dictionary_init(&dict);
          bg_read_desktop_file(desktop_file, &dict);
        
          if((s = gavl_dictionary_get_dictionary(&dict, "Desktop Entry")) &&
             (icon = gavl_dictionary_get_string(s, "Icon")))
            {
            gavl_dictionary_set_string(&info, GAVL_META_ICON_NAME, icon);
            }
          free(desktop_file);
          gavl_dictionary_free(&dict);
          }
        free(str);
        }
      
      gavl_dictionary_set_int(&info, BG_BACKEND_TYPE, BG_BACKEND_RENDERER);
      bg_backend_add_remote(&info);
      }
    
    if(addr_priv)
      free(addr_priv);
    
    if(real_name)
      free(real_name);

    if(uri)
      free(uri);
    
    }
  
  gavl_dictionary_free(&info);
  return;
  }

static void del_dev(bg_dbus_detector_t * d, const char * addr)
  {
  gavl_msg_t * msg;
  msg = bg_msg_sink_get(d->sink);

  gavl_msg_set_id_ns(msg, BG_MSG_DEL_BACKEND, BG_MSG_NS_BACKEND);
  gavl_msg_set_arg_string(msg, 0, addr);
  
  bg_msg_sink_put(d->sink, msg);

  }

static int msg_callback_detector(void * priv, gavl_msg_t * msg)
  {
  bg_dbus_detector_t * d = priv;

  switch(msg->NS)
    {
    case BG_MSG_NS_PRIVATE:
      switch(msg->ID)
        {
        case MSG_ID_NAME_OWNER_CHANGED:
          {
          const char * name = NULL;
          const char * o_old = NULL;
          const char * o_new = NULL;

          if(!d->session_conn)
            break;
          
          name = gavl_msg_get_arg_string_c(msg, 0);
          o_old = gavl_msg_get_arg_string_c(msg, 1);
          o_new = gavl_msg_get_arg_string_c(msg, 2);
          
          if(o_new && !o_old)
            {
            /* Added name */
      
            if(gavl_string_starts_with(name, MPRIS2_NAME_PREFIX))
              add_dev(d, o_new, name + MPRIS2_NAME_PREFIX_LEN, "mpris2", BG_BACKEND_RENDERER);
            
            }
          else if(!o_new && o_old)
            {
            char * addr = NULL;

            if(gavl_string_starts_with(name, MPRIS2_NAME_PREFIX))
              {
              addr = bg_sprintf("%s://%s", BG_DBUS_MPRIS_URI_SCHEME, name + MPRIS2_NAME_PREFIX_LEN);
              del_dev(d, addr);
              free(addr);
              }
            
            /* Deleted name */
            }
          }
          break;
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
          
          interface = gavl_msg_get_arg_int(msg, 0);
          protocol = gavl_msg_get_arg_int(msg, 1);

          name = gavl_msg_get_arg_string_c(msg, 2);
          type = gavl_msg_get_arg_string_c(msg, 3);
          domain = gavl_msg_get_arg_string_c(msg, 4);

          /* Label already there */
          if(!strcmp(type, TYPE_MPD))
            {
            if(gavl_string_array_indexof(&d->mpd_labels, name) >= 0)
              break;
            }
          
          gavl_dictionary_init(&info);
          
          
          req = dbus_message_new_method_call(d->avahi_addr,
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

          if(!(res = bg_dbus_connection_call_method(d->system_conn, req)))
            {
            dbus_message_unref(req);
            break;
            }
          dbus_message_unref(req);
          
          if(!strcmp(type, TYPE_MPD))
            {
            char * uri = NULL;
            const char * addr;
            int port;
            
            addr = gavl_msg_get_arg_string_c(res, 7);
            port = gavl_msg_get_arg_int(res, 8);

            switch(protocol)
              {
              case AVAHI_PROTO_INET: // IPV4
                uri = bg_sprintf("%s://%s:%d", BG_MPD_URI_SCHEME, addr, port);
                break;
              case AVAHI_PROTO_INET6: // IPV6
                uri = bg_sprintf("%s://[%s]:%d", BG_MPD_URI_SCHEME, addr, port);
                break;
              }
            
            if(!uri)
              {
              break;
              }
            
            gavl_string_array_add(&d->mpd_labels, name);
            gavl_string_array_add(&d->mpd_uris, uri);

            gavl_dictionary_set_string(&info, GAVL_META_LABEL, name);
            gavl_dictionary_set_string_nocopy(&info, GAVL_META_URI, uri);
            
            gavl_dictionary_set_string(&info, BG_BACKEND_PROTOCOL, "mpd");
            gavl_dictionary_set_int(&info, BG_BACKEND_TYPE, BG_BACKEND_RENDERER);

            gavl_metadata_add_image_uri(&info,
                                        GAVL_META_ICON_URL,
                                        -1, -1,
                                        "image/png",
                                        "https://www.musicpd.org/logo.png");
            
            msg1 = bg_msg_sink_get(d->sink);

            bg_msg_set_backend_info(msg1, BG_MSG_ADD_BACKEND, &info);
            bg_msg_sink_put(d->sink, msg1);
  
            gavl_dictionary_free(&info);
            }

          
          
          //          gavl_dictionary_set_string(&info, BG_BACKEND_PROTOCOL, protocol);
          // gavl_dictionary_set_int(&info, BG_BACKEND_TYPE, type);
          
          
          }
          break;
        case MSG_ID_AVAHI_SERVICE_REMOVED:
          {
          int idx;
          const char * name = gavl_msg_get_arg_string_c(msg, 2);
          
          
          if((idx = gavl_string_array_indexof(&d->mpd_labels, name)) >= 0)
            {
            del_dev(d, gavl_string_array_get(&d->mpd_uris, idx));

            gavl_array_splice_val(&d->mpd_uris, idx, 1, NULL);
            gavl_array_splice_val(&d->mpd_labels, idx, 1, NULL);
            
            }
          }
          break;
        }
      break;
      
    }
  return 1;
  }

static void detector_init_dbus(bg_dbus_detector_t * ret)
  {
  DBusMessage * req;
  gavl_msg_t * res1;
  const gavl_value_t * val_c;
  const gavl_array_t * arr;
  int i;
  const char * str;
  
  if(ret->session_conn)
    {
    
    /* Get bus names */
    /* name:   org.freedesktop.DBus
     * iface:  org.freedesktop.DBus
     * path:   /
     * method: ListNames
     */

    req = dbus_message_new_method_call("org.freedesktop.DBus",
                                       "/",
                                       "org.freedesktop.DBus",
                                       "ListNames");
  
    res1 = bg_dbus_connection_call_method(ret->session_conn, req);
    dbus_message_unref(req);
  
    if(res1 && (val_c = gavl_msg_get_arg_c(res1, 0)) &&
       (arr = gavl_value_get_array(val_c)))
      {
      for(i = 0; i < arr->num_entries; i++)
        {
        if((str = gavl_value_get_string(&arr->entries[i])))
          {
          if(gavl_string_starts_with(str, MPRIS2_NAME_PREFIX))
            {
            add_dev(ret, NULL, str + MPRIS2_NAME_PREFIX_LEN, "mpris2", BG_BACKEND_RENDERER);
            }
          }
      
        }
    
      gavl_msg_destroy(res1);
    
      }
    }
  
  if((ret->avahi_addr = bg_dbus_get_name_owner(ret->system_conn, "org.freedesktop.Avahi")))
    {
    DBusMessage * req;
    gavl_msg_t * res;

    const char * type = TYPE_MPD;
    const char * domain = "local";
    int32_t interface = -1;
    int32_t protocol = AVAHI_PROTO_UNSPEC;
    uint32_t flags = 0;
    const char * var;
    
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Found avahi daemon at %s", ret->avahi_addr);
    
    /* Create browser for mpd */    
    req = dbus_message_new_method_call(ret->avahi_addr, "/", "org.freedesktop.Avahi.Server",
                                       "ServiceBrowserNew");
    
    dbus_message_append_args(req,
                             DBUS_TYPE_INT32, &interface, // Interface
                             DBUS_TYPE_INT32, &protocol, // Protocol
                             DBUS_TYPE_STRING, &type,
                             DBUS_TYPE_STRING, &domain,
                             DBUS_TYPE_UINT32, &flags,
                             DBUS_TYPE_INVALID);
    
    res = bg_dbus_connection_call_method(ret->system_conn, req);

    if((var = gavl_msg_get_arg_string_c(res, 0)))
      {
      char * rule;
      char * rule_common = bg_sprintf("sender='%s',type='signal',path='%s',interface='org.freedesktop.Avahi.ServiceBrowser'",
                                      ret->avahi_addr, var);
      
      ret->mpd_browser = gavl_strdup(var);
      gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Created MPD service browser %s", ret->mpd_browser);
      
      rule = bg_sprintf("%s,member='ItemNew'", rule_common);
      bg_dbus_connection_add_listener(ret->system_conn,
                                      rule,
                                      ret->dbus_sink,
                                      BG_MSG_NS_PRIVATE, MSG_ID_AVAHI_SERVICE_ADDED);
      free(rule);

      rule = bg_sprintf("%s,member='ItemRemove'", rule_common);
      bg_dbus_connection_add_listener(ret->system_conn,
                                      rule,
                                      ret->dbus_sink,
                                      BG_MSG_NS_PRIVATE, MSG_ID_AVAHI_SERVICE_REMOVED);
      free(rule);
      }
    
    gavl_msg_destroy(res);
    }
    
  
  }

int bg_dbus_detector_update(bg_dbus_detector_t * d)
  {
  int ret;
  bg_msg_sink_iteration(d->dbus_sink);
  ret = bg_msg_sink_get_num(d->dbus_sink);
  return ret;
  }

bg_dbus_detector_t * bg_dbus_detector_create()
  {
  
  bg_dbus_detector_t * ret;
  
  ret = calloc(1, sizeof(*ret));
  ret->sink = bg_backend_reg->evt_sink;

  if(!(ret->system_conn = bg_dbus_connection_get(DBUS_BUS_SYSTEM)))
    return ret;
  
  ret->session_conn = bg_dbus_connection_get(DBUS_BUS_SESSION);
  
  ret->dbus_sink = bg_msg_sink_create(msg_callback_detector, ret, 0);

  if(ret->session_conn)
    bg_dbus_connection_add_listener(ret->session_conn,
                                    "interface='org.freedesktop.DBus',"
                                    "type='signal',"
                                    "member='NameOwnerChanged',"
                                    "arg0namespace='org.mpris.MediaPlayer2'",
                                    ret->dbus_sink,
                                    BG_MSG_NS_PRIVATE, MSG_ID_NAME_OWNER_CHANGED);

  detector_init_dbus(ret);
  
  return ret;
  }


void bg_dbus_detector_destroy(bg_dbus_detector_t * d)
  {
  DBusMessage * req;
  gavl_msg_t * res;
    
  /* Destroy detector */

  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Freeing MPD Browser");
  req = dbus_message_new_method_call(d->avahi_addr,
                                     d->mpd_browser,
                                     "org.freedesktop.Avahi.ServiceBrowser",
                                     "Free");
  res = bg_dbus_connection_call_method(d->system_conn, req);
  dbus_message_unref(req);
  if(res)
    gavl_msg_destroy(res);
  

  if(d->dbus_sink)
    {
    if(d->session_conn)
      bg_dbus_connection_del_listeners(d->session_conn, d->dbus_sink);
    if(d->system_conn)
      bg_dbus_connection_del_listeners(d->system_conn, d->dbus_sink);
    }
  
  if(d->dbus_sink)
    bg_msg_sink_destroy(d->dbus_sink);

  if(d->avahi_addr)
    free(d->avahi_addr);

  if(d->mpd_browser)
    free(d->mpd_browser);
  
  gavl_array_free(&d->mpd_labels);
  gavl_array_free(&d->mpd_uris);
  
  free(d);
  }

#if 0
bg_remote_dev_detector_t bg_remote_dev_detector_dbus = 
  {
    .create  = detector_create_dbus,
    .destroy = detector_destroy_dbus,
    .init    = detector_init_dbus,

  };
#endif

