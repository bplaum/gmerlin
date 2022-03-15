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

#ifndef BG_UPNP_SSDP_H_INCLUDED
#define BG_UPNP_SSDP_H_INCLUDED

#include <gavl/gavl.h>
#include <gmerlin/backend.h>

#define BG_SSDP_META_TYPE         "Type"
#define BG_SSDP_META_VERSION      "Version"
#define BG_SSDP_META_UUID         "UUID"
#define BG_SSDP_META_EXPIRE_TIME  "ExpireTime"
#define BG_SSDP_META_DEVICES      "Devices"
#define BG_SSDP_META_SERVICES     "Services"
#define BG_SSDP_META_PROTOCOL     "Protocol"

typedef gavl_dictionary_t bg_ssdp_service_t;
typedef gavl_dictionary_t bg_ssdp_device_t;
typedef gavl_dictionary_t bg_ssdp_root_device_t;

#if 0
typedef struct
  {
  char * type;
  int version;
  } bg_ssdp_service_t;


void
bg_ssdp_service_free(bg_ssdp_service_t * s);

typedef struct
  {
  char * type;
  char * uuid;
  int version;

  int num_services;
  bg_ssdp_service_t * services;
  } bg_ssdp_device_t;

void
bg_ssdp_device_free(bg_ssdp_device_t * dev);

typedef struct
  {
  char * uuid;
  char * url;

  char * type;
  int version;
  
  int num_devices;
  bg_ssdp_device_t * devices;

  int num_services;
  bg_ssdp_service_t * services;

  gavl_time_t expire_time;
  } bg_ssdp_root_device_t;

void
bg_ssdp_root_device_free(bg_ssdp_root_device_t*);

void
bg_ssdp_root_device_dump(const bg_ssdp_root_device_t*);

#endif

// BG_MSG_NS_SSDP

/*
 * ARGS (for both)
 *
 * arg0: type     (string)
 * arg1: version  (int)
 * arg2: desc_url (string)
 * arg3: uuid     (string)
 */
   
#define BG_SSDP_MSG_ADD_DEVICE 1
#define BG_SSDP_MSG_DEL_DEVICE 2

void bg_ssdp_msg_set_add(gavl_msg_t * msg,
                         const char * protocol,
                         const char * type,
                         int version,
                         const char * desc_url);

void bg_ssdp_msg_get_add(const gavl_msg_t * msg,
                         const char ** protocol,
                         const char ** type,
                         int * version,
                         const char ** desc_url);

void bg_ssdp_msg_set_del(gavl_msg_t * msg,
                         const char * desc_url);

void bg_ssdp_msg_get_del(gavl_msg_t * msg,
                         const char ** desc_url);




bg_ssdp_device_t *
bg_ssdp_device_add_device(bg_ssdp_root_device_t*, const char * uuid);

typedef struct bg_ssdp_s bg_ssdp_t;

#if 0
typedef void (*bg_ssdp_callback_t)(void * priv, int add, const char * type,
                                   int version,
                                   const char * desc_url,
                                   const char * uuid);
#endif

bg_ssdp_t *
bg_ssdp_create(bg_ssdp_root_device_t * local_dev);

bg_msg_hub_t * bg_ssdp_get_event_hub(bg_ssdp_t * s);

int
bg_ssdp_has_device(const bg_ssdp_root_device_t *,
                   const char * type, int version, int * dev_index);

int
bg_ssdp_has_service(const bg_ssdp_root_device_t *,
                    const char * type, int version, int * dev_index, int * srv_index);

int
bg_ssdp_has_device_str(const bg_ssdp_root_device_t *,
                       const char * type_version, int * dev_index);

int
bg_ssdp_has_service_str(const bg_ssdp_root_device_t *,
                        const char * type_version, int * dev_index, int * srv_index);


char * bg_ssdp_get_service_type_usn(const bg_ssdp_root_device_t * d, int dev, int serv);
char * bg_ssdp_get_device_type_usn(const bg_ssdp_root_device_t * d, int dev);
char * bg_ssdp_get_device_uuid_usn(const bg_ssdp_root_device_t * d, int dev);
char * bg_ssdp_get_root_usn(const bg_ssdp_root_device_t * d);

char * bg_ssdp_get_service_type_nt(const bg_ssdp_root_device_t * d, int dev, int serv);
char * bg_ssdp_get_device_type_nt(const bg_ssdp_root_device_t * d, int dev);
char * bg_ssdp_get_device_uuid_nt(const bg_ssdp_root_device_t * d, int dev);
char * bg_ssdp_get_root_nt(const bg_ssdp_root_device_t * d);

bg_backend_type_t bg_ssdp_is_gmerlin_nt(const char * nt);


int bg_ssdp_update(bg_ssdp_t *);
void bg_ssdp_destroy(bg_ssdp_t *);

void bg_ssdp_browse(bg_ssdp_t *, bg_msg_sink_t * sink);

void bg_create_ssdp_device(gavl_dictionary_t * desc, bg_backend_type_t type,
                           const char * uri, const char * protocol);


#endif // BG_UPNP_SSDP_H_INCLUDED

