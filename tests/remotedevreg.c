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



#include <string.h>
#include <signal.h>

#include <config.h>
#include <gmerlin/backend.h>
#include <gmerlin/translation.h>
#include <gmerlin/log.h>
#define LOG_DOMAIN "backendreg"

#include <gmerlin/utils.h>

static int handle_msg(void * data, gavl_msg_t * msg)
  {
  fprintf(stderr, "Handle backend message\n");
  gavl_msg_dump(msg, 2);
  return 1;
  }

int main(int argc, char ** argv)
  {
  gavl_array_t * info;
  gavl_time_t delay_time = GAVL_TIME_SCALE / 50;
  int i;

  bg_msg_sink_t * sink;
  
  bg_handle_sigint();

  sink = bg_msg_sink_create(handle_msg, NULL, 1);

  bg_msg_hub_connect_sink(bg_backend_registry_get_evt_hub(), sink);
  
  for(i = 0; i < 20 * 50; i++)
    {
    if(bg_got_sigint())
      break;
    gavl_time_delay(&delay_time);
    }

  bg_msg_hub_disconnect_sink(bg_backend_registry_get_evt_hub(), sink);
    
  info = bg_backend_registry_get();
  gavl_array_dump(info, 0);
  gavl_array_destroy(info);

  bg_msg_sink_destroy(sink);
  
  return 0;
  }
