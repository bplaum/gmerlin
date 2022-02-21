
#include <string.h>

#include <gmerlin/frontend.h>
#include <gmerlin/websocket.h>
#include <gmerlin/utils.h>
#include <gmerlin/state.h>
#include <gmerlin/application.h>

#include <gmerlin/upnp/ssdp.h>

#include <frontend_priv.h>

typedef struct
  {
  bg_ssdp_t * ssdp;
  gavl_dictionary_t ssdp_dev;
  
  bg_websocket_context_t * ws;

  bg_http_server_t * srv;
  bg_backend_type_t type;
  
  bg_msg_sink_t * msink;

  int have_node;

  gavl_dictionary_t state;

  } frontend_priv_t;

static int ping_func(bg_frontend_t * f, gavl_time_t current_time)
  {
  int ret = 0;
  frontend_priv_t * p = f->priv;

  if(p->msink)
    bg_msg_sink_iteration(p->msink);
  
  if(!p->ssdp && p->have_node)
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
    
    switch(p->type)
      {
      case BG_BACKEND_RENDERER:
        uri_scheme = BG_BACKEND_URI_SCHEME_GMERLIN_RENDERER;
        break;
      case BG_BACKEND_MEDIASERVER:
        uri_scheme = BG_BACKEND_URI_SCHEME_GMERLIN_MDB;
        break;
      case BG_BACKEND_STATE:
      case BG_BACKEND_NONE:
        break;
      }

    if(!uri_scheme)
      return 1;
    
    root_uri = bg_http_server_get_root_url(p->srv);
    uri = bg_sprintf("%s%s/ws/%s", uri_scheme, root_uri + 4 /* ://..." */, bg_backend_type_to_string(p->type));
    
    bg_create_ssdp_device(&p->ssdp_dev, p->type, uri, "gmerlin");

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

    gavl_dictionary_set_int(&local_dev, BG_BACKEND_TYPE, p->type);
    gavl_dictionary_set_string(&local_dev, BG_BACKEND_PROTOCOL, "gmerlin");

    if((val = bg_state_get(&p->state, BG_APP_STATE_NETWORK_NODE, GAVL_META_ICON_URL)) &&
       (icon_array = gavl_value_get_array(val)))
      {
      gavl_dictionary_set_array(&local_dev, GAVL_META_ICON_URL, icon_array);
      }

    bg_backend_register_local(&local_dev);
    
    p->ssdp = bg_ssdp_create(&p->ssdp_dev);

    gavl_dictionary_free(&local_dev);
    
    ret++;
    
    free(uri);
    }
  
  ret += bg_websocket_context_iteration(p->ws);

  if(p->ssdp)
    ret += bg_ssdp_update(p->ssdp);
  
  return ret;
  }

static void frontend_cleanup_gmerlin(void * priv)
  {
  frontend_priv_t * p = priv;

  if(p->ssdp)
    bg_ssdp_destroy(p->ssdp);
    
  bg_websocket_context_destroy(p->ws);

  gavl_dictionary_free(&p->state);

  gavl_dictionary_free(&p->ssdp_dev);
  
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
          bg_msg_get_state(msg, &last, &ctx, &var, &val, &p->state);
          
          gavl_value_free(&val);
          
          if(!strcmp(ctx, BG_APP_STATE_NETWORK_NODE) && (!var || last))
            p->have_node = 1;
          
          }
          break;
        }
      }
    }
  return 1;
  }

static bg_frontend_t * frontend_create_gmerlin(bg_http_server_t * srv, bg_controllable_t * ctrl,
                                               bg_backend_type_t type)
  {
  bg_frontend_t * ret;

  frontend_priv_t * p = calloc(1, sizeof(*p));
  
  ret = bg_frontend_create(ctrl);

  ret->ping_func = ping_func;
  ret->cleanup_func = frontend_cleanup_gmerlin;
  
  p->ws = bg_websocket_context_create(type, srv, NULL, ctrl);
  p->srv = srv;
  p->type = type;

  p->msink = bg_msg_sink_create(handle_message, p, 0);

  bg_msg_hub_connect_sink(ctrl->evt_hub, p->msink);
  
  ret->priv = p;

  
  bg_frontend_init(ret);
  
  return ret;
  }

bg_frontend_t * bg_frontend_create_mdb_gmerlin(bg_http_server_t * srv, bg_controllable_t * ctrl)
  {
  return frontend_create_gmerlin(srv, ctrl, BG_BACKEND_MEDIASERVER);
  }

bg_frontend_t * bg_frontend_create_player_gmerlin(bg_http_server_t * srv, bg_controllable_t * ctrl)
  {
  return frontend_create_gmerlin(srv, ctrl, BG_BACKEND_RENDERER);
  }

