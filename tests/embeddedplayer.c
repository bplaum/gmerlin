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

#include <uuid/uuid.h>

#include <gavl/metatags.h>


#include <gmerlin/parameter.h>
#include <gmerlin/cfg_registry.h>

#include <gmerlin/embedplayer.h>
#include <gmerlin/player.h>

#include <gmerlin/playermsg.h>

static int handle_msg(void * priv, gavl_msg_t * msg)
  {
  fprintf(stderr, "Got message\n");
  gavl_msg_dump(msg, 2);
  return 1;
  }

int main(int argc, char ** argv)
  {
  bg_embed_player_t * p;
  gavl_dictionary_t m;
  bg_controllable_t * player_ctrl;
  bg_control_t ctrl;
  char uuid_str[37];
  uuid_t uuid;
  
  p = bg_embed_player_create();

  bg_control_init(&ctrl, bg_msg_sink_create(handle_msg, NULL, 0));
  
  gavl_dictionary_init(&m);
  
  bg_embed_player_run(p);

  player_ctrl = bg_embed_player_get_ctrl(p);

  bg_controllable_connect(player_ctrl, &ctrl);

  uuid_generate(uuid);
  uuid_unparse(uuid, uuid_str);
  
  bg_player_play_location(player_ctrl->cmd_sink, argv[1], &m, uuid_str);
  
  while(1)
    {
    bg_msg_sink_iteration(ctrl.evt_sink);
    }
  
  }
