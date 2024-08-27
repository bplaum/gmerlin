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

#include <gavl/gavl.h>


#include <gmerlin/parameter.h>
#include <gmerlin/http.h>
#include <gmerlin/httpserver.h>
#include <gmerlin/websocket.h>
#include <gmerlin/frontend.h>
#include <gmerlin/player.h>

#ifdef HAVE_DBUS
#include <gmerlin/bgdbus.h>
#endif

typedef struct
  {
  bg_player_t * player;
  bg_cfg_ctx_t * player_cfg;
  
  //  char * label; // Visible in the network

  bg_http_server_t * srv;
  
  bg_frontend_t ** frontends;
  int num_frontends;
  
  bg_parameter_info_t * parameters;

  char * vardir;
  
  gavl_dictionary_t state;
  char * state_file;
  } renderer_t;

void renderer_init(renderer_t * s, gavl_array_t * fe_arr);


void renderer_cleanup(renderer_t * s);
int renderer_iteration(renderer_t * s);

void renderer_set_parameter(void * priv, const char * name,
                            const gavl_value_t * val);
