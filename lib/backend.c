/*****************************************************************
 * Gmerlin - a general purpose multimedia framework and applications
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
#include <pthread.h>

#include <gavl/metatags.h>

#include <gmerlin/backend.h>

#include <gmerlin/utils.h>
#include <gmerlin/msgqueue.h>
#include <gmerlin/player.h>
#include <gmerlin/playermsg.h>
#include <gmerlin/translation.h>
#include <gmerlin/log.h>
#include <gmerlin/httpserver.h>

#define LOG_DOMAIN "backend"

#include <backend_priv.h>


#ifdef HAVE_DBUS
extern const bg_remote_dev_backend_t bg_remote_dev_backend_mpris2_player;
extern const bg_remote_dev_backend_t bg_remote_dev_backend_mpd_player;
#endif


extern const bg_remote_dev_backend_t bg_remote_dev_backend_gmerlin_renderer;
extern const bg_remote_dev_backend_t bg_remote_dev_backend_gmerlin_mediaserver;

extern const bg_remote_dev_backend_t bg_remote_dev_backend_upnp_renderer;


static const bg_remote_dev_backend_t *
remote_dev_backends[] =
  {
    &bg_remote_dev_backend_upnp_renderer,
    &bg_remote_dev_backend_gmerlin_mediaserver,
    &bg_remote_dev_backend_gmerlin_renderer,
#ifdef HAVE_DBUS
    &bg_remote_dev_backend_mpris2_player,
#endif
    &bg_remote_dev_backend_mpd_player,
    NULL,
  };

void bg_msg_set_backend_info(gavl_msg_t * msg,
                             uint32_t id, 
                             const gavl_dictionary_t * info)
  {
  gavl_msg_set_id_ns(msg, id, BG_MSG_NS_BACKEND);
  gavl_msg_set_arg_dictionary(msg, 0, info);
  }

void bg_msg_get_backend_info(gavl_msg_t * msg,
                                   gavl_dictionary_t * info)
  {
  gavl_msg_get_arg_dictionary(msg, 0, info);
  }

void bg_backend_info_get_id(const gavl_dictionary_t * info,
                            char * ret)
  {
  bg_get_filename_hash(gavl_dictionary_get_string(info, GAVL_META_URI), ret);
  }

void bg_backend_info_init(gavl_dictionary_t * info)
  {
  char buf[BG_BACKEND_ID_LEN+1];

  bg_get_filename_hash(gavl_dictionary_get_string(info, GAVL_META_URI), buf);
  gavl_dictionary_set_string(info, GAVL_META_ID, buf);

  }

static const struct
  {
  const char * name;
  bg_backend_type_t type;
  }
types[] =
  {
    {
    .name = "server",
    .type = BG_BACKEND_MEDIASERVER,
    },
    {
    .name = "renderer",
    .type = BG_BACKEND_RENDERER,
    },
    {
    .name = "state",
    .type = BG_BACKEND_STATE,
    },
    { /* End */ } 
  };

const char * bg_backend_type_to_string(bg_backend_type_t type)
  {
  int i = 0;

  while(types[i].name)
    {
    if(types[i].type == type)
      return types[i].name;
    i++;
    }
  return NULL;
  }

bg_backend_type_t bg_backend_type_from_string(const char * type)
  {
  int i = 0;

  while(types[i].name)
    {
    if(!strcmp(types[i].name, type))
      return types[i].type;
    i++;
    }
  return BG_BACKEND_NONE;
  }


/* remote device livecycle */

int bg_backend_needs_http(const char * uri)
  {
  int i;
  
  i = 0;
  
  while(remote_dev_backends[i])
    {
    if(gavl_string_starts_with(uri, remote_dev_backends[i]->uri_prefix))
      {
      if(remote_dev_backends[i]->handle_http)
        return 1;
      else
        return 0;
      }
    i++;
    }
  return 0;
  }

bg_backend_handle_t *
bg_backend_handle_create(const gavl_dictionary_t * dev, const char * root_url)
  {
  int i;
  bg_backend_handle_t * ret;

  const char * uri = gavl_dictionary_get_string(dev, GAVL_META_URI);

  if(!uri)
    return NULL;
  
  ret = calloc(1, sizeof(*ret));

  ret->root_url = gavl_strdup(root_url);
  
  gavl_dictionary_copy(&ret->dev, dev);
  
  bg_get_filename_hash(uri, ret->id);
  
  ret->timer = gavl_timer_create();
  gavl_timer_start(ret->timer);
  
  i = 0;
  
  while(remote_dev_backends[i])
    {
    if(gavl_string_starts_with(uri, remote_dev_backends[i]->uri_prefix))
      {
      ret->b = remote_dev_backends[i];
      break;
      }
    i++;
    }

  if(!ret->b)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "No backend found");
    goto fail;
    }

  /* Controls must exist before creation of the backend */

  if(ret->b->handle_msg)
    bg_controllable_init(&ret->ctrl,
                         bg_msg_sink_create(ret->b->handle_msg, ret, 0),
                         bg_msg_hub_create(1));
  
  if(ret->b->create && !ret->b->create(ret, uri, root_url))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Creation failed");
    goto fail;
    }
  
  return ret;
  
  fail:
  bg_backend_handle_destroy(ret);
  return NULL;

  }

int bg_backend_handle_handle(bg_http_connection_t * conn,
                             void * data)
  {
  bg_backend_handle_t * h = data;
  if(!h->b->handle_http)
    return 0;
  else
    return h->b->handle_http(h, conn);
  }

int bg_backend_handle_ping(bg_backend_handle_t * d)
  {
  int ret = 0;

  if(d->ctrl.cmd_sink)
    {
    if(!bg_msg_sink_iteration(d->ctrl.cmd_sink))
      {
      return -1;
      }
    ret = bg_msg_sink_get_num(d->ctrl.cmd_sink);
    }

#if 0
  
  if(d->ctrl_ext && d->ctrl_ext->cmd_sink)
    {
    if(!bg_msg_sink_iteration(d->ctrl_ext->cmd_sink))
      {
      return -1;
      }
    ret += bg_msg_sink_get_num(d->ctrl_ext->cmd_sink);
    }
#endif
  
  if(d->b->ping)
    ret += d->b->ping(d);

  if(d->ctrl.cmd_sink)
    {
    bg_msg_sink_iteration(d->ctrl.cmd_sink);
    ret += bg_msg_sink_get_num(d->ctrl.cmd_sink);
    }
  
  return ret;
  }

static void * backend_thread(void * data)
  {
  int ops;
  int res;
  bg_backend_handle_t * be = data;
  gavl_time_t delay_time = GAVL_TIME_SCALE / 20; // 50 ms
  
  
  while(1)
    {
    ops = 0;
    
    res = bg_backend_handle_ping(be);
    
    if(res < 0)
      break;
    
    ops += res;
    if(!ops)
      gavl_time_delay(&delay_time);
    }

  return NULL;
  }

void bg_backend_handle_stop(bg_backend_handle_t * be)
  {
  gavl_msg_t * msg = NULL;

  if(!be->thread_running)
    return;
  
  if(be->ctrl_ext)
    msg = bg_msg_sink_get(be->ctrl_ext->cmd_sink);
  else if(be->ctrl.cmd_sink)
    msg = bg_msg_sink_get(be->ctrl.cmd_sink);
  
  if(msg)
    {
    gavl_msg_set_id_ns(msg, GAVL_CMD_QUIT, GAVL_MSG_NS_GENERIC);
    if(be->ctrl_ext)
      bg_msg_sink_put(be->ctrl_ext->cmd_sink, msg);
    else if(be->ctrl.cmd_sink)
      bg_msg_sink_put(be->ctrl.cmd_sink, msg);
    }
  
  pthread_join(be->th, NULL);

  // if(be->b->stop)
  //    be->b->stop(be);

  
  be->thread_running = 0;
  
  }

void bg_backend_handle_destroy(bg_backend_handle_t * be)
  {
  bg_backend_handle_stop(be);
  
  if(be->b && be->b->destroy)
    be->b->destroy(be);
  
  if(be->root_url)
    free(be->root_url);
  
  bg_controllable_cleanup(&be->ctrl);

  gavl_timer_destroy(be->timer);
  
  free(be);
  }

bg_controllable_t * bg_backend_handle_get_controllable(bg_backend_handle_t * d)
  {
  if(d->ctrl_ext)
    return d->ctrl_ext;
  else
    return &d->ctrl;
  }

void bg_backend_handle_start(bg_backend_handle_t * be)
  {
  pthread_create(&be->th, NULL, backend_thread, be);
  be->thread_running = 1;
  }

