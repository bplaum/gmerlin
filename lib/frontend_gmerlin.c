
#include <string.h>

#include <gmerlin/frontend.h>
#include <gmerlin/websocket.h>
#include <gmerlin/utils.h>
#include <gmerlin/state.h>
#include <gmerlin/application.h>
#include <gmerlin/resourcemanager.h>

#include <gmerlin/upnp/ssdp.h>

#include <frontend_priv.h>

#define FLAG_HAVE_NODE  (1<<0)
#define FLAG_REGISTERED (1<<1)

typedef struct
  {
  bg_websocket_context_t * ws;

  bg_http_server_t * srv;
  char * klass;
  
  bg_msg_sink_t * msink;

  int flags;

  gavl_dictionary_t state;

  } frontend_priv_t;

static int ping_func(bg_frontend_t * f, gavl_time_t current_time)
  {
  int ret = 0;
  frontend_priv_t * p = f->priv;

  if(p->msink)
    bg_msg_sink_iteration(p->msink);
  
  if((p->flags & FLAG_HAVE_NODE) && !(p->flags & FLAG_REGISTERED))
    {
    gavl_dictionary_t local_dev;
    
    const gavl_value_t * val;
    const gavl_array_t * icon_array;
    
    const char * uri_scheme = NULL;
    const char * server_label = NULL;
    char * uri;
    const char * root_uri;

    bg_msg_hub_disconnect_sink(f->controllable->evt_hub, p->msink);
    bg_msg_sink_destroy(p->msink);
    p->msink = NULL;

    if(!strcmp(p->klass, GAVL_META_MEDIA_CLASS_BACKEND_RENDERER))
      uri_scheme = BG_BACKEND_URI_SCHEME_GMERLIN_RENDERER;
    else if(!strcmp(p->klass, GAVL_META_MEDIA_CLASS_BACKEND_SERVER))
      uri_scheme = BG_BACKEND_URI_SCHEME_GMERLIN_MDB;
    
    if(!uri_scheme)
      return 1;
    
    root_uri = bg_http_server_get_root_url(p->srv);
    uri = bg_sprintf("%s%s/ws/%s", uri_scheme, root_uri + 4 /* ://..." */, p->klass);
    
    /* Create local device */

    if(!(val = bg_state_get(&p->state, BG_APP_STATE_NETWORK_NODE, GAVL_META_LABEL)) ||
       !(server_label = gavl_value_get_string(val)))
      {
      free(uri);
      return 0;
      }
    
    gavl_dictionary_init(&local_dev);
    gavl_dictionary_set_string(&local_dev, GAVL_META_URI, uri);
    gavl_dictionary_set_string(&local_dev, GAVL_META_LABEL, server_label);

    gavl_dictionary_set_string(&local_dev, GAVL_META_MEDIA_CLASS, p->klass);
    gavl_dictionary_set_string(&local_dev, BG_BACKEND_PROTOCOL, "gmerlin");

    if((val = bg_state_get(&p->state, BG_APP_STATE_NETWORK_NODE, GAVL_META_ICON_URL)) &&
       (icon_array = gavl_value_get_array(val)))
      {
      gavl_dictionary_set_array(&local_dev, GAVL_META_ICON_URL, icon_array);
      }
    
    bg_resourcemanager_publish(uri, &local_dev);
    
    gavl_dictionary_free(&local_dev);
    
    ret++;
    
    free(uri);
    p->flags |= FLAG_REGISTERED;
    
    }
  
  ret += bg_websocket_context_iteration(p->ws);

  return ret;
  }

static void frontend_cleanup_gmerlin(void * priv)
  {
  frontend_priv_t * p = priv;

  bg_websocket_context_destroy(p->ws);

  gavl_dictionary_free(&p->state);

  if(p->klass)
    free(p->klass);
  
  free(p);
  }

static int handle_message(void * priv, gavl_msg_t * msg)
  {
  frontend_priv_t * p = priv;
  
  switch(msg->NS)
    {
    case BG_MSG_NS_STATE:
      {
      switch(msg->ID)
        {
        case BG_MSG_STATE_CHANGED:
          {
          int last;
          const char * ctx = NULL;
          const char * var = NULL;

          gavl_value_t val;
          gavl_value_init(&val);
          
          //          bg_msg_get_state(msg, &last, &ctx, &var, NULL, &p->state);
          gavl_msg_get_state(msg, &last, &ctx, &var, &val, &p->state);
          
          if(!strcmp(ctx, BG_APP_STATE_NETWORK_NODE) && (!var || last))
            {
            p->flags = FLAG_HAVE_NODE;
            //            fprintf(stderr, "Got network node:\n");
            //            gavl_value_dump(&val, 2);
            }
          gavl_value_free(&val);
          
          }
          break;
        }
      }
    }
  return 1;
  }

static bg_frontend_t * frontend_create_gmerlin(bg_http_server_t * srv, bg_controllable_t * ctrl,
                                               const char * klass)
  {
  bg_frontend_t * ret;

  frontend_priv_t * p = calloc(1, sizeof(*p));
  
  ret = bg_frontend_create(ctrl);

  ret->ping_func = ping_func;
  ret->cleanup_func = frontend_cleanup_gmerlin;
  
  p->ws = bg_websocket_context_create(klass, srv, NULL, ctrl);
  p->srv = srv;
  p->klass = gavl_strdup(klass);
  
  p->msink = bg_msg_sink_create(handle_message, p, 0);

  bg_msg_hub_connect_sink(ctrl->evt_hub, p->msink);
  
  ret->priv = p;

  
  bg_frontend_init(ret);
  
  return ret;
  }

bg_frontend_t * bg_frontend_create_mdb_gmerlin(bg_http_server_t * srv, bg_controllable_t * ctrl)
  {
  return frontend_create_gmerlin(srv, ctrl, GAVL_META_MEDIA_CLASS_BACKEND_SERVER);
  }

bg_frontend_t * bg_frontend_create_player_gmerlin(bg_http_server_t * srv, bg_controllable_t * ctrl)
  {
  return frontend_create_gmerlin(srv, ctrl, GAVL_META_MEDIA_CLASS_BACKEND_RENDERER);
  }
