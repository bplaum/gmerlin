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



#include <gavl/gavl.h>


#include <gmerlin/parameter.h>
#include <gmerlin/http.h>
#include <gmerlin/httpserver.h>
#include <gmerlin/websocket.h>
#include <gmerlin/mdb.h>
#include <gmerlin/frontend.h>

typedef struct
  {
  char * label; // Visible in the network
  char * dbpath; // Database path (parent dir of gmerlin-mdb)

  bg_http_server_t * srv;
  bg_mdb_t * mdb;

  bg_frontend_t ** frontends;
  int num_frontends;
  
  bg_parameter_info_t * parameters;

  char * vardir;

  bg_server_storage_t * storage;
  int max_client_ids;

  gavl_dictionary_t state;
  char * state_file;
  } server_t;

int server_init(server_t * s, gavl_array_t * fe_arr);


void server_cleanup(server_t * s);
int server_iteration(server_t * s);

void server_set_parameter(void * priv, const char * name,
                          const gavl_value_t * val);
