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



#include "gmerlin.h"
#include "player_remote.h"

void gmerlin_handle_remote(gmerlin_t * g, gavl_msg_t * msg)
  {
  int           id;
  const char        * arg_str;
  const char * locations[2];
  
  id = gavl_msg_get_id(msg);

  fprintf(stderr, "gmerlin_handle_remote %d\n", id);

  switch(id)
    {
    case PLAYER_COMMAND_ADD_LOCATION:
      arg_str = gavl_msg_get_arg_string_c(msg, 0);
      locations[0] = arg_str;
      locations[1] = NULL;
      gmerlin_add_locations(g, locations);
      break;
    case PLAYER_COMMAND_PLAY_LOCATION:
      arg_str = gavl_msg_get_arg_string_c(msg, 0);
      locations[0] = arg_str;
      locations[1] = NULL;
      gmerlin_play_locations(g, locations);
      break;
    case PLAYER_COMMAND_OPEN_DEVICE:
      arg_str = gavl_msg_get_arg_string_c(msg, 0);
      gmerlin_open_device(g, arg_str);
      break;
    case PLAYER_COMMAND_PLAY_DEVICE:
      arg_str = gavl_msg_get_arg_string_c(msg, 0);
      gmerlin_play_device(g, arg_str);
      break;
      
    default:
      {
      /* Send to player */
      bg_msg_sink_t * s = g->player_ctrl->cmd_sink;
      bg_msg_sink_put_copy(s, msg);
      }
    }
  }
