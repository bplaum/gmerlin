/*****************************************************************
 * Gmerlin - a general purpose multimedia framework and applications
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
#include <upnp/devicepriv.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

#include <gmerlin/mdb.h>

#include <gmerlin/http.h>
#include <gmerlin/utils.h>
#include <gmerlin/upnp/devicedesc.h>
#include <gmerlin/upnp/upnputils.h>
#include <sys/utsname.h>
#include <gmerlin/log.h>
#define LOG_DOMAIN "upnpdevice"


int
bg_upnp_device_handle_request(bg_upnp_device_t * dev, 
                              bg_http_connection_t * conn)
  {
  int i;
  const char * pos;
  
  /* Check for description */

  if(!strcmp(conn->path, "desc.xml"))
    {
    bg_upnp_send_description(conn,dev->description);
    return 1;
    }

  pos = strchr(conn->path, '/');
  if(!pos)
    return 0;
  
  for(i = 0; i < dev->info->num_services; i++)
    {
    if((strlen(dev->services[i].name) == (pos - conn->path)) &&
       (!strncmp(dev->services[i].name, conn->path, pos - conn->path)))
      {
      /* Found service */
      conn->path = pos + 1;
      return bg_upnp_service_handle_request(&dev->services[i], conn);
      }
    }
  return 0;
  }

void
bg_upnp_device_destroy(bg_upnp_device_t * dev)
  {
  int i;

  for(i = 0; i < dev->info->num_services; i++)
    bg_upnp_service_free(&dev->services[i]);
  if(dev->services)
    free(dev->services);

  if(dev->url_base)
    free(dev->url_base);
  if(dev->name)
    free(dev->name);
  if(dev->path)
    free(dev->path);
  if(dev->timer)
    gavl_timer_destroy(dev->timer);
  if(dev->description)
    free(dev->description);

  if(dev->destroy)
    dev->destroy(dev->priv);

  gavl_dictionary_free(&dev->ssdp_dev);

  free(dev);
  }

#if 0
char * bg_upnp_make_server_string(void)
  {
  struct utsname os_info;
  uname(&os_info);
  return bg_sprintf("%s/%s, UPnP/1.0, "PACKAGE"/"VERSION,
                    os_info.sysname, os_info.release);
  }
#endif

static int handle_upnp(bg_http_connection_t * conn, void * data)
  {
  return bg_upnp_device_handle_request(data, conn);
  }

void bg_upnp_device_init(bg_upnp_device_t * ret, bg_http_server_t * srv,
                         uuid_t uuid, const char * name,
                         const bg_upnp_device_info_t * info,
                         const bg_upnp_icon_t * icons)
  {
  char addr_str[BG_SOCKET_ADDR_STR_LEN];

  ret->info = info;
  ret->services = calloc(ret->info->num_services, sizeof(*ret->services));

  ret->srv = srv;
  ret->path = bg_sprintf("/upnp/%s/", bg_remote_device_type_to_string(ret->info->type));
  bg_http_server_add_handler(ret->srv, handle_upnp, BG_HTTP_PROTO_HTTP, ret->path, ret);
  
  ret->sa = bg_http_server_get_address(ret->srv);

  ret->url_base = bg_sprintf("http://%s/",
                             bg_socket_address_to_string(ret->sa, addr_str));
  
  uuid_copy(ret->uuid, uuid);
  ret->name = gavl_strdup(name);
  ret->icons = icons;
  
  ret->timer = gavl_timer_create();
  gavl_timer_start(ret->timer);
  
  }

static void create_description(bg_upnp_device_t * dev)
  {
  int i;
  xmlDocPtr doc;
  char * tmp_string;
  char hostname[HOST_NAME_MAX];
  
  doc = bg_upnp_device_description_create(dev->info->upnp_type, dev->info->version);

  gethostname(hostname, HOST_NAME_MAX);

  tmp_string = bg_sprintf("%s (%s)", dev->name, hostname);
  bg_upnp_device_description_set_name(doc, tmp_string);
  free(tmp_string);

  bg_upnp_device_description_set_manufacturer(doc, "Gmerlin Project");
  bg_upnp_device_description_set_manufacturer_url(doc, "http://gmerlin.sourceforge.net");
  bg_upnp_device_description_set_model_description(doc, dev->info->model_description);
  bg_upnp_device_description_set_model_name(doc, dev->info->model_name);
  bg_upnp_device_description_set_model_number(doc, VERSION);
  bg_upnp_device_description_set_model_url(doc, "http://gmerlin.sourceforge.net");
  bg_upnp_device_description_set_serial_number(doc, "1");
  bg_upnp_device_description_set_uuid(doc, dev->uuid);

  if(dev->icons)
    {
    i = 0;
    while(dev->icons[i].location)
      {
      bg_upnp_device_description_add_icon(doc, 
                                          dev->icons[i].mimetype, 
                                          dev->icons[i].width, dev->icons[i].height, 
                                          dev->icons[i].depth, dev->icons[i].location);
      i++;
      }
    }

  for(i = 0; i < dev->info->num_services; i++)
    {
    bg_upnp_device_description_add_service(doc, dev,
                                           dev->services[i].type, 
                                           dev->services[i].version, 
                                           dev->services[i].name);
    }
  dev->description = bg_xml_save_to_memory(doc);
  xmlFreeDoc(doc);
  
  //  fprintf(stderr, "Created device description:\n%s\n", dev->description);
  
  }

int
bg_upnp_device_create_common(bg_upnp_device_t * dev)
  {
  int i;
  /* Set the device pointers in the servives */
  for(i = 0; i < dev->info->num_services; i++)
    dev->services[i].dev = dev;
  
  create_description(dev);
  return 1;
  }

int
bg_upnp_device_ping(bg_upnp_device_t * dev)
  {
  int i;
  int ret = 0;
  gavl_time_t current_time = gavl_timer_get(dev->timer);

  if(dev->ping)
    ret += dev->ping(dev);

  for(i = 0; i < dev->info->num_services; i++)
    ret += bg_upnp_service_ping(&dev->services[i], current_time);
  
  return ret;
  }

bg_http_server_t * bg_upnp_device_get_server(bg_upnp_device_t * d)
  {
  return d->srv;
  }


bg_control_t *  bg_upnp_device_get_control(bg_upnp_device_t * dev)
  {
  return &dev->ctrl;
  }

const gavl_audio_format_t bg_upnp_playlist_stream_afmt =
  {
    .num_channels = 2,
    .samplerate   = 44100,
    .sample_format = GAVL_SAMPLE_FLOAT,
    .interleave_mode = GAVL_INTERLEAVE_ALL,
    .channel_locations = { GAVL_CHID_FRONT_LEFT, GAVL_CHID_FRONT_RIGHT },
  }; 
