#include <stdlib.h>
#include <string.h>


#include <config.h>
#include <gmerlin/translation.h>

#include <gmerlin/utils.h>

#include <gmerlin/plugin.h>
#include <gmerlin/bgdbus.h>
#include <gmerlin/backend.h>
#include <gmerlin/resourcemanager.h>

#define FLAG_INITIAL (1<<0)

#define MPRIS2_NAME_PREFIX     "org.mpris.MediaPlayer2."
#define MPRIS2_NAME_PREFIX_LEN 23

#define MSG_ID_NAME_OWNER_CHANGED 1

typedef struct
  {
  bg_controllable_t ctrl;

  bg_dbus_connection_t * conn;
  bg_msg_sink_t * dbus_sink;

  int flags;
  } mpris_t;

static void add_dev(mpris_t * m, const char * addr, const char * name,
                    const char * klass)
  {
  gavl_dictionary_t info;
  char * addr_priv = NULL;
  char * str;
  char * real_name = NULL;
  char * uri = NULL;
  gavl_msg_t * msg;
  
  memset(&info, 0, sizeof(info));
  
  gavl_dictionary_set_string(&info, GAVL_META_MEDIA_CLASS, klass);

  //  fprintf(stderr, "add_dev %s %s %s %d\n", addr, name, protocol, type);
  

  if(gavl_string_starts_with(name, "gmerlin-"))
    {
    const char * pos = strrchr(name, '-');
    if(pos && (strlen(pos + 1) == BG_BACKEND_ID_LEN))
      gavl_dictionary_set_string(&info, GAVL_META_ID, pos+1);
    }
    
  real_name = gavl_sprintf("%s%s", MPRIS2_NAME_PREFIX, name);

  uri = gavl_sprintf("%s://%s", BG_DBUS_MPRIS_URI_SCHEME, real_name + MPRIS2_NAME_PREFIX_LEN);

  if(!addr)
    {
    addr_priv = bg_dbus_get_name_owner(m->conn, real_name);
    addr = addr_priv;
    }
  
  gavl_dictionary_set_string_nocopy(&info, GAVL_META_URI,
                                    gavl_sprintf("%s://%s", BG_DBUS_MPRIS_URI_SCHEME, name));
      
  str = bg_dbus_get_string_property(m->conn,
                                    addr,
                                    "/org/mpris/MediaPlayer2",
                                    "org.mpris.MediaPlayer2",
                                    "Identity");
    
  if(str)
    gavl_dictionary_set_string_nocopy(&info, GAVL_META_LABEL, str);

  str = bg_dbus_get_string_property(m->conn,
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
  
  gavl_dictionary_set_string(&info, GAVL_META_MEDIA_CLASS, GAVL_META_MEDIA_CLASS_BACKEND_RENDERER);

  msg = bg_msg_sink_get(m->ctrl.evt_sink);

  gavl_msg_set_id_ns(msg, GAVL_MSG_RESOURCE_ADDED, GAVL_MSG_NS_GENERIC);
  gavl_dictionary_set_string_nocopy(&msg->header, GAVL_MSG_CONTEXT_ID, gavl_sprintf("mpris-%s", addr));
  gavl_msg_set_arg_dictionary(msg, 0, &info);
  bg_msg_sink_put(m->ctrl.evt_sink);
  
  if(addr_priv)
    free(addr_priv);
    
  if(real_name)
    free(real_name);

  if(uri)
    free(uri);
  
  gavl_dictionary_free(&info);
  return;
  }

static void del_dev(mpris_t * m, const char * addr)
  {
  gavl_msg_t * msg;
  msg = bg_msg_sink_get(m->ctrl.evt_sink);

  gavl_msg_set_id_ns(msg, GAVL_MSG_RESOURCE_DELETED, GAVL_MSG_NS_GENERIC);
  gavl_dictionary_set_string_nocopy(&msg->header, GAVL_MSG_CONTEXT_ID, gavl_sprintf("mpris-%s", addr));
  
  bg_msg_sink_put(m->ctrl.evt_sink);

  }


static int handle_msg_dbus(void * priv, gavl_msg_t * msg)
  {
  mpris_t * m = priv;

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

          if(!m->conn)
            break;
          
          name = gavl_msg_get_arg_string_c(msg, 0);
          o_old = gavl_msg_get_arg_string_c(msg, 1);
          o_new = gavl_msg_get_arg_string_c(msg, 2);
          
          if(o_new && !o_old)
            {
            /* Added name */
      
            if(gavl_string_starts_with(name, MPRIS2_NAME_PREFIX))
              add_dev(m, o_new, name + MPRIS2_NAME_PREFIX_LEN, GAVL_META_MEDIA_CLASS_BACKEND_RENDERER);
            
            }
          else if(!o_new && o_old)
            {
            //            char * addr = NULL;

            if(gavl_string_starts_with(name, MPRIS2_NAME_PREFIX))
              {
              // addr = gavl_sprintf("%s://%s", BG_DBUS_MPRIS_URI_SCHEME, name + MPRIS2_NAME_PREFIX_LEN);
              del_dev(m, o_old);
              //              free(addr);
              }
            
            /* Deleted name */
            }
          }
          break;
        }
      break;
    }
  return 1;
  }



static int update_mpris(void * priv)
  {
  int ret = 0;
  mpris_t * m = priv;

  if(!(m->flags & FLAG_INITIAL))
    {
    ret++;
    m->flags |= FLAG_INITIAL;
    }

  bg_msg_sink_iteration(m->dbus_sink);
  ret += bg_msg_sink_get_num(m->dbus_sink);
  return ret;
  }

static void destroy_mpris(void * priv)
  {
  mpris_t * m = priv;
  
  free(m);
  }

static int handle_msg(void * priv, gavl_msg_t * msg)
  {
  return 1;
  }

static void * create_mpris()
  {
  mpris_t * m;

  m = calloc(1, sizeof(*m));

  bg_controllable_init(&m->ctrl,
                       bg_msg_sink_create(handle_msg, m, 1),
                       bg_msg_hub_create(1));

  if(!(m->conn = bg_dbus_connection_get(DBUS_BUS_SESSION)))
    return m;

  m->dbus_sink = bg_msg_sink_create(handle_msg_dbus, m, 0);
  
  bg_dbus_connection_add_listener(m->conn,
                                  "interface='org.freedesktop.DBus',"
                                  "type='signal',"
                                  "member='NameOwnerChanged',"
                                  "arg0namespace='org.mpris.MediaPlayer2'",
                                  m->dbus_sink,
                                  BG_MSG_NS_PRIVATE, MSG_ID_NAME_OWNER_CHANGED);
  
  return m;
  }

static bg_controllable_t * get_controllable_mpris(void * priv)
  {
  mpris_t * m = priv;
  return &m->ctrl;
  }


bg_controllable_plugin_t the_plugin =
  {
    .common =
    {
      BG_LOCALE,
      .name =      "res_mpris",
      .long_name = TRS("Mpris2 detector"),
      .description = TRS("Detector for Mpris2 based media players"),
      .type =     BG_PLUGIN_RESOURCE_DETECTOR,
      .flags =    0,
      .create =   create_mpris,
      .destroy =   destroy_mpris,
      .get_controllable =   get_controllable_mpris,
      .priority =         1,
    },
    .update = update_mpris,

  };

/* Include this into all plugin modules exactly once
   to let the plugin loader obtain the API version */
BG_GET_PLUGIN_API_VERSION;
