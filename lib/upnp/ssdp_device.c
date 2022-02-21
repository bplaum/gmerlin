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

#include <config.h>
#include <string.h>
#include <ctype.h>

#include <gavl/gavl.h>
#include <gavl/metadata.h>

#include <gmerlin/upnp/ssdp.h>

#include <gmerlin/log.h>
#define LOG_DOMAIN "ssdpdevice"

#include <gmerlin/utils.h>

static int match_type_version(const char * type_req, int ver_req,
                              const gavl_dictionary_t * dict)
  {
  int ver_avail = 0;
  const char * type_avail = gavl_dictionary_get_string(dict, BG_SSDP_META_TYPE);
  gavl_dictionary_get_int(dict, BG_SSDP_META_VERSION, &ver_avail);
  
  return !strcmp(type_req, type_avail) && (ver_avail >= ver_req);
  }

static int match_type_version_str(const char * req,
                                  const gavl_dictionary_t * dict)
  {
  const char * pos;
  int ver_avail = 0;
  const char * type_avail = gavl_dictionary_get_string(dict, BG_SSDP_META_TYPE);
  gavl_dictionary_get_int(dict, BG_SSDP_META_VERSION, &ver_avail);


  pos = strchr(req, ':');
  if(!pos)
    return 0;

  if((strlen(type_avail) != pos - req) ||
     strncmp(req, type_avail, pos - req))
    return 0;

  pos++;
  if(atoi(pos) > ver_avail)
    return 0;
  return 1;
  }

int
bg_ssdp_has_device(const bg_ssdp_root_device_t * dev, const char * type, int version, int * dev_index)
  {
  int i;
  const gavl_array_t * arr;
  
  if(match_type_version(type, version, dev))
    {
    if(dev_index)
      *dev_index = -1;
    return 1;
    }

  if((arr = gavl_dictionary_get_array(dev, BG_SSDP_META_DEVICES)))
    {
    for(i = 0; i < arr->num_entries; i++)
      {
      const gavl_dictionary_t * edev;
      
      if((edev = gavl_value_get_dictionary(&arr->entries[i])) &&
         match_type_version(type, version, edev))
        {
        if(dev_index)
          *dev_index = i;
        return 1;
        }
      }

    
    }
     
  
  return 0;
  }

int
bg_ssdp_has_device_str(const bg_ssdp_root_device_t * dev, const char * type_version, int * dev_index)
  {
  int i;
  const gavl_array_t * arr;

  if(match_type_version_str(type_version, dev))
    {
    if(dev_index)
      *dev_index = -1;
    return 1;
    }

  if((arr = gavl_dictionary_get_array(dev, BG_SSDP_META_DEVICES)))
    {

    for(i = 0; i < arr->num_entries; i++)
      {
      const gavl_dictionary_t * edev;

      if((edev = gavl_value_get_dictionary(&arr->entries[i])) &&
         match_type_version_str(type_version, edev))
        {
        if(dev_index)
          *dev_index = i;
        return 1;
        }
      }
    }
  return 0;
  }

static int lookup_service(const gavl_dictionary_t * s, const char * type, int version)
  {
  int i;
  const gavl_array_t * arr;
  const gavl_dictionary_t * srv;
  
  if(!(arr = gavl_dictionary_get_array(s, BG_SSDP_META_SERVICES)))
    return -1;
  
  for(i = 0; i < arr->num_entries; i++)
    {
    if((srv = gavl_value_get_dictionary(&arr->entries[i])) &&
       match_type_version(type, version, srv))
      return i;
    }
  return -1;
  }

static int lookup_service_str(const gavl_dictionary_t * s, const char * type_version)
  {
  int i;
  const gavl_array_t * arr;
  const gavl_dictionary_t * srv;

  if(!(arr = gavl_dictionary_get_array(s, BG_SSDP_META_SERVICES)))
    return -1;
  
  for(i = 0; i < arr->num_entries; i++)
    {
    if((srv = gavl_value_get_dictionary(&arr->entries[i])) &&
       match_type_version_str(type_version, srv))
      return i;
    }
  return -1;
  }

int
bg_ssdp_has_service(const bg_ssdp_root_device_t * dev, const char * type,
                    int version, int * dev_index, int * srv_index)
  {
  int idx, i;
  const gavl_array_t * arr;
  const gavl_dictionary_t * edev;

  idx = lookup_service(dev, type, version);

  if(idx >= 0)
    {
    if(dev_index)
      *dev_index = -1;

    if(srv_index)
      *srv_index = idx;
    return 1;
    }

  if(!(arr = gavl_dictionary_get_array(dev, BG_SSDP_META_DEVICES)))
    return 0;
  
  for(i = 0; i < arr->num_entries; i++)
    {
    if((edev = gavl_value_get_dictionary(&arr->entries[i])) &&
       ((idx = lookup_service(edev, type, version)) >= 0))
      {
      if(dev_index)
        *dev_index = i;
      
      if(srv_index)
        *srv_index = idx;
      return 1;
      }
    }
  return 0;
  }

int
bg_ssdp_has_service_str(const bg_ssdp_root_device_t * dev,
                        const char * type_version, int * dev_index, int * srv_index)
  {
  int idx, i;
  const gavl_array_t * arr;
  const gavl_dictionary_t * edev;

  idx = lookup_service_str(dev, type_version);

  if(idx >= 0)
    {
    if(dev_index)
      *dev_index = -1;

    if(srv_index)
      *srv_index = idx;
    return 1;
    }

  if(!(arr = gavl_dictionary_get_array(dev, BG_SSDP_META_DEVICES)))
    return 0;

    
  for(i = 0; i < arr->num_entries; i++)
    {
    if((edev = gavl_value_get_dictionary(&arr->entries[i])) &&
       ((idx = lookup_service_str(edev, type_version)) >= 0))
      {
      if(dev_index)
        *dev_index = i;
      
      if(srv_index)
        *srv_index = idx;
      return 1;
      }
    }
    
  /*
  
  for(i = 0; i < dev->num_devices; i++)
    {
    idx = lookup_service_str(dev->devices[i].services, dev->devices[i].num_services, type_version);

    if(idx >= 0)
      {
      if(dev_index)
        *dev_index = i;
      
      if(srv_index)
        *srv_index = idx;
      return 1;
      }
    }
  */

  
  return 0;
  }

bg_ssdp_device_t *
bg_ssdp_device_add_device(bg_ssdp_root_device_t * dev, const char * uuid)
  {
  gavl_array_t * arr;

  gavl_value_t val;
  gavl_dictionary_t * dict;
  
  arr = gavl_dictionary_get_array_create(dev, BG_SSDP_META_DEVICES);

  gavl_value_init(&val);
  dict = gavl_value_set_dictionary(&val);
  gavl_dictionary_set_string(dict, BG_SSDP_META_UUID, uuid);
  gavl_array_splice_val_nocopy(arr, -1, 0, &val);
  return gavl_value_get_dictionary_nc(&arr->entries[arr->num_entries-1]);
  }

/* Get USN / NT */

char * bg_ssdp_get_service_type_usn(const bg_ssdp_root_device_t * d, int dev, int serv)
  {
  int version = 0;
  const gavl_array_t * services;
  const gavl_dictionary_t * service;
  
  if(dev >= 0)
    {
    const gavl_array_t * arr;
    arr = gavl_dictionary_get_array(d, BG_SSDP_META_DEVICES);
    d = gavl_value_get_dictionary(&arr->entries[dev]);
    }
  
  services = gavl_dictionary_get_array(d, BG_SSDP_META_SERVICES);
  service = gavl_value_get_dictionary(&services->entries[serv]);

  gavl_dictionary_get_int(service, BG_SSDP_META_VERSION, &version);

  return bg_sprintf("uuid:%s::urn:schemas-upnp-org:service:%s:%d",
                    gavl_dictionary_get_string(d, BG_SSDP_META_UUID),
                    gavl_dictionary_get_string(service, BG_SSDP_META_TYPE),
                    version);
  
  }

char * bg_ssdp_get_device_type_usn(const bg_ssdp_root_device_t * d, int dev)
  {
  int version = 0;
  
  if(dev >= 0)
    {
    const gavl_array_t * arr;
    arr = gavl_dictionary_get_array(d, BG_SSDP_META_DEVICES);
    d = gavl_value_get_dictionary(&arr->entries[dev]);
    }

  gavl_dictionary_get_int(d, BG_SSDP_META_VERSION, &version);

  return bg_sprintf("uuid:%s::urn:schemas-upnp-org:device:%s:%d",
                    gavl_dictionary_get_string(d, BG_SSDP_META_UUID),
                    gavl_dictionary_get_string(d, BG_SSDP_META_TYPE),
                    version);
  }

char * bg_ssdp_get_device_uuid_usn(const bg_ssdp_root_device_t * d, int dev)
  {
  if(dev >= 0)
    {
    const gavl_array_t * arr;
    arr = gavl_dictionary_get_array(d, BG_SSDP_META_DEVICES);
    d = gavl_value_get_dictionary(&arr->entries[dev]);
    }
  
  return bg_sprintf("uuid:%s", gavl_dictionary_get_string(d, BG_SSDP_META_UUID));
  }

char * bg_ssdp_get_service_type_nt(const bg_ssdp_root_device_t * d, int dev, int serv)
  {
  int version = 0;
  const gavl_array_t * services;
  const gavl_dictionary_t * service;
  
  if(dev >= 0)
    {
    const gavl_array_t * arr;
    arr = gavl_dictionary_get_array(d, BG_SSDP_META_DEVICES);
    d = gavl_value_get_dictionary(&arr->entries[dev]);
    }
  
  services = gavl_dictionary_get_array(d, BG_SSDP_META_SERVICES);
  service = gavl_value_get_dictionary(&services->entries[serv]);

  gavl_dictionary_get_int(service, BG_SSDP_META_VERSION, &version);
  
  return bg_sprintf("urn:schemas-upnp-org:service:%s:%d",
                    gavl_dictionary_get_string(service, BG_SSDP_META_TYPE),
                    version);
  }

char * bg_ssdp_get_device_type_nt(const bg_ssdp_root_device_t * d, int dev)
  {
  int version = 0;
  
  if(dev >= 0)
    {
    const gavl_array_t * arr;
    arr = gavl_dictionary_get_array(d, BG_SSDP_META_DEVICES);
    d = gavl_value_get_dictionary(&arr->entries[dev]);
    }

  gavl_dictionary_get_int(d, BG_SSDP_META_VERSION, &version);

  return bg_sprintf("urn:schemas-upnp-org:device:%s:%d",
                    gavl_dictionary_get_string(d, BG_SSDP_META_TYPE),
                    version);
  }

char * bg_ssdp_get_device_uuid_nt(const bg_ssdp_root_device_t * d, int dev)
  {
  if(dev >= 0)
    {
    const gavl_array_t * arr;
    arr = gavl_dictionary_get_array(d, BG_SSDP_META_DEVICES);
    d = gavl_value_get_dictionary(&arr->entries[dev]);
    }

  return bg_sprintf("uuid:%s", gavl_dictionary_get_string(d, BG_SSDP_META_UUID));
  }

static char * make_gmerlin_nt(const bg_ssdp_root_device_t * d)
  {
  return bg_sprintf("urn:gmerlin-sourceforge-net:ws:%s", gavl_dictionary_get_string(d, BG_SSDP_META_TYPE));
  }

bg_backend_type_t bg_ssdp_is_gmerlin_nt(const char * nt)
  {
  bg_backend_type_t type;
  
  if(gavl_string_starts_with(nt, "urn:gmerlin-sourceforge-net:ws:") &&
     ((type = bg_backend_type_from_string(nt + 31)) != BG_BACKEND_NONE))
    return type;

  return BG_BACKEND_NONE;
  }

char * bg_ssdp_get_root_usn(const bg_ssdp_root_device_t * d)
  {
  const char * protocol;

  //  fprintf(stderr, "bg_ssdp_get_root_usn\n");
  //  gavl_dictionary_dump(d, 2);
  
  protocol = gavl_dictionary_get_string(d, BG_SSDP_META_PROTOCOL);

  if(!strcmp(protocol, "upnp"))
    return bg_sprintf("uuid:%s::upnp:rootdevice", gavl_dictionary_get_string(d, BG_SSDP_META_UUID));
  else if(!strcmp(protocol, "gmerlin"))
    {
    char * ret;
    char * tmp_string;
    
    tmp_string = make_gmerlin_nt(d);
    ret = bg_sprintf("uuid:%s::%s", gavl_dictionary_get_string(d, BG_SSDP_META_UUID), tmp_string);
    free(tmp_string);
    return ret; 
    }
  return NULL;
  }
  
char * bg_ssdp_get_root_nt(const bg_ssdp_root_device_t * d)
  {
  const char * protocol = gavl_dictionary_get_string(d, BG_SSDP_META_PROTOCOL);

  if(!strcmp(protocol, "upnp"))
    return gavl_strdup("upnp:rootdevice");
  else if(!strcmp(protocol, "gmerlin"))
    {
    return make_gmerlin_nt(d);
    }
  return NULL;
  }
