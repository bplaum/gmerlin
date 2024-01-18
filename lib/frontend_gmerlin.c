
#include <string.h>

#include <gmerlin/frontend.h>
#include <gmerlin/websocket.h>
#include <gmerlin/utils.h>
#include <gmerlin/state.h>
#include <gmerlin/application.h>
#include <gmerlin/resourcemanager.h>

#include <gmerlin/upnp/ssdp.h>

#include <gavl/log.h>
#define LOG_DOMAIN "frontend_gmerlin"

#include <frontend_priv.h>

#define FLAG_REGISTERED (1<<0)

typedef struct
  {
  bg_websocket_context_t * ws;

  char * klass;
  
  int flags;

  gavl_dictionary_t state;

  } frontend_priv_t;

static int ping_func(bg_frontend_t * f, gavl_time_t current_time)
  {
  int ret = 0;
  frontend_priv_t * p = f->priv;

  if(!(p->flags & FLAG_REGISTERED))
    {
    gavl_dictionary_t local_dev;
    
    const char * uri_scheme = NULL;
    const char * server_label = NULL;
    char * uri;
    const char * root_uri;
    bg_http_server_t * srv;
    
    if(!strcmp(p->klass, GAVL_META_MEDIA_CLASS_BACKEND_RENDERER))
      uri_scheme = BG_BACKEND_URI_SCHEME_GMERLIN_RENDERER;
    else if(!strcmp(p->klass, GAVL_META_MEDIA_CLASS_BACKEND_SERVER))
      uri_scheme = BG_BACKEND_URI_SCHEME_GMERLIN_MDB;
    
    if(!uri_scheme)
      return 1;

    srv = bg_http_server_get();
    
    root_uri = bg_http_server_get_root_url(srv);
    uri = bg_sprintf("%s%s/ws/%s", uri_scheme, root_uri + 4 /* ://..." */, p->klass);
    
    /* Create local device */

    if(!(server_label = bg_app_get_label()))
      {
      free(uri);
      return 0;
      }
    
    gavl_dictionary_init(&local_dev);
    gavl_dictionary_set_string(&local_dev, GAVL_META_URI, uri);
    gavl_dictionary_set_string(&local_dev, GAVL_META_LABEL, server_label);
    gavl_dictionary_set_string(&local_dev, GAVL_META_MEDIA_CLASS, p->klass);
    
    
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


static bg_frontend_t * frontend_create_gmerlin(bg_controllable_t * ctrl,
                                               const char * klass)
  {
  bg_frontend_t * ret;

  frontend_priv_t * p;

  if(!bg_http_server_get())
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "No http server present");
    return NULL;
    }

  p = calloc(1, sizeof(*p));
  ret = bg_frontend_create(ctrl);

  ret->ping_func = ping_func;
  ret->cleanup_func = frontend_cleanup_gmerlin;
  
  p->ws = bg_websocket_context_create(klass, NULL, ctrl);
  
  p->klass = gavl_strdup(klass);
  
  
  ret->priv = p;

  
  bg_frontend_init(ret);
  
  return ret;
  }

bg_frontend_t * bg_frontend_create_mdb_gmerlin(bg_controllable_t * ctrl)
  {
  return frontend_create_gmerlin(ctrl, GAVL_META_MEDIA_CLASS_BACKEND_SERVER);
  }

bg_frontend_t * bg_frontend_create_player_gmerlin(bg_controllable_t * ctrl)
  {
  return frontend_create_gmerlin(ctrl, GAVL_META_MEDIA_CLASS_BACKEND_RENDERER);
  }
