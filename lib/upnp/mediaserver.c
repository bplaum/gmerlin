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

#include <upnp/devicepriv.h>
#include <upnp/mediaserver.h>
#include <stdlib.h>

static void destroy_func(void * data)
  {
  bg_mediaserver_t * priv = data;
  free(priv);
  }

static const bg_upnp_device_info_t info =
  {
    .upnp_type = "MediaServer",
    .version = 1,
    .model_description = "Gmerlin media server",
    .model_name        = "Gmerlin media server",
    .num_services      = 2,
    .type              = BG_REMOTE_DEVICE_MEDIASERVER,
  };


bg_upnp_device_t *
bg_upnp_create_media_server(bg_http_server_t * srv,
                            uuid_t uuid,
                            const char * name,
                            const bg_upnp_icon_t * icons,
                            bg_db_t * db)
  {
  bg_mediaserver_t * priv;
  bg_upnp_device_t * ret;
  ret = calloc(1, sizeof(*ret));

  priv = calloc(1, sizeof(*priv));

  ret->destroy = destroy_func;
  ret->priv = priv;
  priv->db = db;  

  bg_upnp_device_init(ret, srv, uuid, name, &info, icons);

  bg_upnp_service_init_content_directory(&ret->services[0], db);
  bg_upnp_service_init_connection_manager(&ret->services[1], 0, 0);
  
  bg_upnp_device_create_common(ret);
  return ret;
  }

