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

#include <stdlib.h>
#include <stdio.h>

#include <gavl/gavl.h>
#include <gavl/metadata.h>
#include <gmerlin/upnp/ssdp.h>

static int handle_ssdp_msg(void * priv, gavl_msg_t * evt)
  {
  const char * upnp_type;
  int upnp_version = 0;
  const char * desc_url;
  const char * protocol;
  
  switch(evt->NS)
    {
    case BG_MSG_NS_SSDP:
      switch(evt->ID)
        {
        case BG_SSDP_MSG_ADD_DEVICE: // 1
          {
          bg_ssdp_msg_get_add(evt, &protocol, &upnp_type, &upnp_version, &desc_url);
          fprintf(stderr, "Added device: %s.%d [%s]\n", upnp_type, upnp_version, desc_url);
          }
          break;
        case BG_SSDP_MSG_DEL_DEVICE: // 2
          bg_ssdp_msg_get_del(evt, &desc_url);
          fprintf(stderr, "Removed device: [%s]\n", desc_url);
          break;
        }
      break;
    }
  
  return 1;
  }

int main(int argc, char ** argv)
  {
  bg_msg_sink_t * sink;
  
  gavl_time_t delay_time = GAVL_TIME_SCALE / 100;
  bg_ssdp_t * s = bg_ssdp_create(NULL);

  sink = bg_msg_sink_create(handle_ssdp_msg, NULL, 1);
  bg_msg_hub_connect_sink(bg_ssdp_get_event_hub(s), sink);
  
  while(1)
    {
    bg_ssdp_update(s);
    gavl_time_delay(&delay_time);
    }
  }
