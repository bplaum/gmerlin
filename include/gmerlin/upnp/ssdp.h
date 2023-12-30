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

bg_ssdp_device_t *
bg_ssdp_device_add_device(bg_ssdp_root_device_t*, const char * uuid);

typedef struct bg_ssdp_s bg_ssdp_t;

bg_ssdp_t *
bg_ssdp_create(void);

void bg_ssdp_force_search(bg_ssdp_t * s);


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

const char * bg_ssdp_is_gmerlin_nt(const char * nt);

int bg_ssdp_update(bg_ssdp_t *);
void bg_ssdp_destroy(bg_ssdp_t *);

// void bg_ssdp_browse(bg_ssdp_t *, bg_msg_sink_t * sink);

void bg_create_ssdp_device(gavl_dictionary_t * desc, const char * klass,
                           const char * uri, const char * protocol);


#endif // BG_UPNP_SSDP_H_INCLUDED

