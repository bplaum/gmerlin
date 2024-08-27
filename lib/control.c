/*****************************************************************
 * gmerlin - a general purpose multimedia framework and applications
 *
 * Copyright (c) 2001 - 2024 Members of the Gmerlin project
 * http://github.com/bplaum
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

/* Handle a message of a control.
   Here, we need to do the routing */

static int handle_message_cmd(void * priv, gavl_msg_t * msg)
  {
  const char * client_id;
  bg_control_t * c = priv;

  if(!c->ctrl)
    return 1;
  
  if(!(client_id = gavl_msg_get_client_id(msg)))
    {
    if(c->cmd_sink->id)
      gavl_msg_set_client_id(msg, c->cmd_sink->id);
    }
  else
    {
    /* Update routing table */
    bg_msg_routing_table_put(c->evt_sink, client_id);
    }
  
  if(c->ctrl && c->ctrl->cmd_sink)
    {
    gavl_msg_t * msg1 = bg_msg_sink_get(c->ctrl->cmd_sink);
    gavl_msg_copy(msg1, msg);
    bg_msg_sink_put(c->ctrl->cmd_sink);
    }
  return 1;
  }

void bg_control_init(bg_control_t * c,
                     bg_msg_sink_t * evt_sink)
  {
  uuid_t u;
  
  memset(c, 0, sizeof(*c));

  uuid_generate(u);
  uuid_unparse(u, c->id);
  
  c->evt_sink = evt_sink;
  c->cmd_sink = bg_msg_sink_create(handle_message_cmd, c, 1);

  bg_msg_sink_set_id(c->evt_sink, c->id);
  bg_msg_sink_set_id(c->cmd_sink, c->id);
  }

void bg_control_cleanup(bg_control_t * c)
  {
  if(c->priv && c->cleanup)
    c->cleanup(c->priv);
  if(c->evt_sink)
    bg_msg_sink_destroy(c->evt_sink);
  if(c->cmd_sink)
    bg_msg_sink_destroy(c->cmd_sink);
  memset(c, 0, sizeof(*c));
  }
