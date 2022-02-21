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

#include <gmerlin/upnp/ssdp.h>
#include <gmerlin/upnp/device.h>

#include <upnp/servicepriv.h>

typedef struct
  {
  /* upnp type and version */
  const char * upnp_type;
  int version;
  const char * model_description;
  const char * model_name;
  int num_services;
  bg_remote_device_type_t type;
  } bg_upnp_device_info_t;

struct bg_upnp_device_s
  {
  const bg_upnp_device_info_t * info;

  char * description; // xml device description
  
  bg_upnp_service_t * services;
  
  void (*destroy)(void*priv);
  void * priv;

  int (*ping)(bg_upnp_device_t * dev);
  
  bg_ssdp_root_device_t ssdp_dev;
  
  const bg_socket_address_t * sa;
  uuid_t uuid;
  char * name;
  char * path;
  
  const bg_upnp_icon_t * icons;

  char * url_base;
    
  gavl_timer_t * timer;

  bg_http_server_t * srv;
  
  bg_control_t ctrl;
  };

void bg_upnp_device_init(bg_upnp_device_t * dev,
                         bg_http_server_t * srv,
                         uuid_t uuid, const char * name,
                         const bg_upnp_device_info_t * info,
                         const bg_upnp_icon_t * icons);

int
bg_upnp_device_create_common(bg_upnp_device_t * dev);

