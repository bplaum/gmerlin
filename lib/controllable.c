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
#include <string.h>
#include <pthread.h>
#include <uuid/uuid.h>

#include <gmerlin/parameter.h>
#include <gmerlin/bgmsg.h>
#include <gmerlin/state.h>

#include <gmerlin/translation.h>
#include <gmerlin/log.h>
#define LOG_DOMAIN "controllable"

#include <msgconn.h>

/* Controllable */

void
bg_controllable_init(bg_controllable_t * ctrl,
                     bg_msg_sink_t * cmd_sink, // Owned
                     bg_msg_hub_t * evt_hub)   // Owned
  {
  memset(ctrl, 0, sizeof(*ctrl));
  ctrl->cmd_sink = cmd_sink;
  ctrl->evt_hub  = evt_hub;
  ctrl->evt_sink = bg_msg_hub_get_sink(ctrl->evt_hub);
  }

void
bg_controllable_cleanup(bg_controllable_t * ctrl)   // Owned
  {
  if(ctrl->priv && ctrl->cleanup)
    ctrl->cleanup(ctrl->priv);

  if(ctrl->cmd_sink)
    bg_msg_sink_destroy(ctrl->cmd_sink);
  if(ctrl->evt_hub)
    bg_msg_hub_destroy(ctrl->evt_hub);
  memset(ctrl, 0, sizeof(*ctrl));
  }

void
bg_controllable_connect(bg_controllable_t * ctrl,
                        bg_control_t * c)
  {
  if(c->evt_sink)
    bg_msg_hub_connect_sink(ctrl->evt_hub, c->evt_sink);
  c->ctrl = ctrl;
  }


void
bg_controllable_disconnect(bg_controllable_t * ctrl,
                           bg_control_t * c)
  {
  if(c->evt_sink)
    bg_msg_hub_disconnect_sink(ctrl->evt_hub, c->evt_sink);
  c->ctrl = NULL;
  }

typedef struct
  {
  gavl_msg_t * req;
  gavl_handle_msg_func cb;
  void * data;
  } function_context_t;

static int handle_msg_function(void * data, gavl_msg_t * msg)
  {
  const char * var;
  
  function_context_t * ctx = data;

  if(!(var = gavl_dictionary_get_string(&msg->header, BG_FUNCTION_TAG)) ||
     strcmp(var, gavl_dictionary_get_string(&ctx->req->header, BG_FUNCTION_TAG)))
    return 1;

  if(ctx->cb)
    ctx->cb(ctx->data, msg);

  if(gavl_msg_get_last(msg))
    return 0;
  else
    return 1;
  }

int bg_controllable_call_function(bg_controllable_t * c, gavl_msg_t * func,
                                  gavl_handle_msg_func cb, void * data, int timeout)
  {
  int result = 0;
  gavl_time_t delay_time = GAVL_TIME_SCALE / 50; // 20ms
  bg_control_t ctrl;
  gavl_timer_t * timer = gavl_timer_create();

  memset(&ctrl, 0, sizeof(ctrl));
  
  bg_msg_add_function_tag(func);

  bg_control_init(&ctrl, bg_msg_sink_create(handle_msg_function, data, 0));
  bg_controllable_connect(c, &ctrl);
  
  bg_msg_sink_put_copy(ctrl.cmd_sink, func);
  
  gavl_timer_start(timer);
  while(1)
    {
    if(c->ping_func)
      c->ping_func(c->ping_data);
    
    if(!bg_msg_sink_iteration(ctrl.evt_sink))
      {
      result = 1;
      break;
      }
    if((gavl_timer_get(timer)*1000) / GAVL_TIME_SCALE > timeout)
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Timeout expired when waiting for function result");
      break;
      }
    gavl_time_delay(&delay_time);
    }
  
  gavl_timer_destroy(timer);

  bg_controllable_disconnect(c, &ctrl);
  bg_control_cleanup(&ctrl);
  
  return result;
  }
