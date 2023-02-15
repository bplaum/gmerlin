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
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <gavl/gavl.h>
#include <gavl/metadata.h>
#include <gavl/metatags.h>
#include <gavl/gavlsocket.h>

#include <gmerlin/upnp/ssdp.h>
#include <gmerlin/upnp/upnputils.h>

#include <gmerlin/translation.h>
#include <gmerlin/http.h>

#include <gmerlin/log.h>
#define LOG_DOMAIN "ssdp"

#include <gmerlin/utils.h>

#include <unistd.h>

// #define DUMP_UDP
// #define DUMP_DEVS
// #define DUMP_HEADERS

#define UDP_BUFFER_SIZE 2048

#define QUEUE_SIZE 128

// #define META_METHOD "$METHOD"
// #define META_STATUS "$STATUS"

#define NOTIFY_INTERVAL_LONG  (900*GAVL_TIME_SCALE) // max-age / 2
#define NOTIFY_INTERVAL_SHORT (GAVL_TIME_SCALE/2)
#define NOTIFY_NUM 5 // Number of notify messages sent in shorter intervals

#define SEARCH_INTERVAL (GAVL_TIME_SCALE/2)
#define SEARCH_NUM      5

typedef struct
  {
  char * st;
  gavl_socket_address_t * addr;
  gavl_time_t time; // GAVL_TIME_UNDEFINED means empty
  //  int times_sent;
  } queue_element_t;

struct bg_ssdp_s
  {
  int mcast_fd;
  int ucast_fd;
  gavl_timer_t * timer;
  gavl_socket_address_t * sender_addr;
  gavl_socket_address_t * local_addr;
  gavl_socket_address_t * mcast_addr;
  uint8_t buf[UDP_BUFFER_SIZE];

  const bg_ssdp_root_device_t * local_dev;

  gavl_array_t remote_devs;
  
  
  queue_element_t queue[QUEUE_SIZE];

#ifdef DUMP_DEVS
  gavl_time_t last_dump_time;
#endif

  gavl_time_t last_notify_time;
  int notify_count;

  gavl_time_t last_search_time;
  int search_count;
  
  bg_msg_hub_t * event_hub;
  
  //  bg_ssdp_callback_t cb;
  //  void * cb_data;
  };

bg_msg_hub_t * bg_ssdp_get_event_hub(bg_ssdp_t * s)
  {
  return s->event_hub;
  }

void bg_ssdp_msg_set_add(gavl_msg_t * msg,
                         const char * protocol,
                         const char * type,
                         int version,
                         const char * desc_url)
  {
  const char * pos;
  char * real_url = NULL;

  //  fprintf(stderr, "bg_ssdp_msg_set_add: %s %s %d %s\n", protocol, type, version, desc_url);
  
  gavl_msg_set_id_ns(msg, BG_SSDP_MSG_ADD_DEVICE, BG_MSG_NS_SSDP);

  if(!strcmp(protocol, "upnp") && (pos = strstr(desc_url, "://")))
    {
    if(!strcmp(type, "MediaServer"))
      {
      real_url = bg_sprintf(BG_BACKEND_URI_SCHEME_UPNP_SERVER"%s", pos);
      }
    else if(!strcmp(type, "MediaRenderer"))
      {
      real_url = bg_sprintf(BG_BACKEND_URI_SCHEME_UPNP_RENDERER"%s", pos);
      }
    }

  if(real_url)
    desc_url = real_url;

  //  fprintf(stderr, "URL %s\n", desc_url);
  
  gavl_msg_set_arg_string(msg, 0, protocol);
  gavl_msg_set_arg_string(msg, 1, type);
  gavl_msg_set_arg_int(msg, 2, version);
  gavl_msg_set_arg_string(msg, 3, desc_url);

  if(real_url)
    free(real_url);
  
  }

void bg_ssdp_msg_get_add(const gavl_msg_t * msg,
                         const char ** protocol,
                         const char ** type,
                         int * version,
                         const char ** desc_url)
  {
  if(protocol)
    *protocol = gavl_msg_get_arg_string_c(msg, 0);
  
  if(type)
    *type = gavl_msg_get_arg_string_c(msg, 1);
  if(version)
    *version = gavl_msg_get_arg_int(msg, 2);
  if(desc_url)
    *desc_url = gavl_msg_get_arg_string_c(msg, 3);
  }

void bg_ssdp_msg_set_del(gavl_msg_t * msg,
                         const char * desc_url)
  {
  gavl_msg_set_id_ns(msg, BG_SSDP_MSG_DEL_DEVICE, BG_MSG_NS_SSDP);
  gavl_msg_set_arg_string(msg, 0, desc_url);
  }

void bg_ssdp_msg_get_del(gavl_msg_t * msg,
                         const char ** desc_url)
  {
  if(desc_url)
    *desc_url = gavl_msg_get_arg_string_c(msg, 0);
  }
  

// TYPE:VERSION

static int extract_type_version(const char * str, gavl_dictionary_t * ret)
  {
  const char * pos;
  
  pos = strchr(str, ':');
  if(!pos)
    return 0;
  
  gavl_dictionary_set_string_nocopy(ret, BG_SSDP_META_TYPE, gavl_strndup(str, pos));
  
  pos++;
  if(*pos == '\0')
    return 0;
  
  gavl_dictionary_set_int(ret, BG_SSDP_META_VERSION, atoi(pos));
  return 1;
  }

static int 
find_root_dev_by_location(bg_ssdp_t * s, const char * loc)
  {
  int i;
  const char * str;
  const gavl_dictionary_t * dev;
  
  for(i = 0; i < s->remote_devs.num_entries; i++)
    {
    if((dev = gavl_value_get_dictionary(&s->remote_devs.entries[i])) &&
       (str = gavl_dictionary_get_string(dev, GAVL_META_URI)) &&
       !strcmp(str, loc))
      return i;
    }
  return -1;
  }

static int 
find_embedded_dev_by_uuid(const bg_ssdp_root_device_t * dev, const char * uuid)
  {
  int i;
  const char * str;
  const gavl_dictionary_t * embedded_dev;
  const gavl_array_t * arr = gavl_dictionary_get_array(dev, BG_SSDP_META_DEVICES);

  if(!arr)
    return -1;
  
  for(i = 0; i < arr->num_entries; i++)
    {
    if((embedded_dev = gavl_value_get_dictionary(&arr->entries[i])) &&
       (str = gavl_dictionary_get_string(embedded_dev, BG_SSDP_META_UUID)) &&
       !strcmp(str, uuid))
      return i;
    }
  return -1;
  }

static const char * is_device_type(const char * str)
  {
  if(gavl_string_starts_with_i(str, "urn:schemas-upnp-org:device:"))
    return str + 28;
  return NULL;
  }

static const char * is_service_type(const char * str)
  {
  if(gavl_string_starts_with_i(str, "urn:schemas-upnp-org:service:"))
    return str + 29;
  return NULL;
  }

static const bg_ssdp_service_t * find_service(const gavl_array_t * arr, const char * type)
  {
  int i;
  int len;
  const char * pos;
  const char * str;
  const gavl_dictionary_t * dict;
  
  if((pos = strchr(type, ':')))
    len = pos - type;
  else
    len = strlen(type);

  for(i = 0; i < arr->num_entries; i++)
    {
    if((dict = gavl_value_get_dictionary(&arr->entries[i])) &&
       (str = gavl_dictionary_get_string(dict, BG_SSDP_META_TYPE)) &&
       (strlen(str) == len) &&
       !strncmp(str, type, len))
      return dict;
    }
  return NULL;
  }

static const bg_ssdp_service_t * device_find_service(const gavl_dictionary_t * dev, const char * type)
  {
  const gavl_array_t * services;
  
  if(!(services = gavl_dictionary_get_array(dev, BG_SSDP_META_SERVICES)))
    return NULL;
  
  return find_service(services, type);
  }

static bg_ssdp_service_t * device_add_service(bg_ssdp_device_t * dev)
  {
  gavl_value_t val;
  gavl_array_t * services;
  
  services = gavl_dictionary_get_array_create(dev, BG_SSDP_META_SERVICES);
  
  gavl_value_init(&val);
  gavl_value_set_dictionary(&val);
  gavl_array_splice_val_nocopy(services, -1, 0, &val);
  return gavl_value_get_dictionary_nc(&services->entries[services->num_entries-1]);
  }


static bg_ssdp_root_device_t *
add_root_dev(bg_ssdp_t * s, const char * loc, const char * uuid)
  {
  gavl_value_t val;
  gavl_dictionary_t * dev;
  
  //  bg_ssdp_root_device_t * ret;

  gavl_value_init(&val);
  dev = gavl_value_set_dictionary(&val);
  gavl_dictionary_set_string(dev, GAVL_META_URI, loc);
  gavl_dictionary_set_string(dev, GAVL_META_UUID, uuid);

  gavl_array_splice_val_nocopy(&s->remote_devs, -1, 0, &val);
  return gavl_value_get_dictionary_nc(&s->remote_devs.entries[s->remote_devs.num_entries-1]);
  }

static void
del_remote_dev(bg_ssdp_t * s, int idx)
  {
  int i;
  const char * uri;
  const char * protocol;
  const char * type;
  
  gavl_dictionary_t * dev = gavl_value_get_dictionary_nc(&s->remote_devs.entries[idx]);

  //  fprintf(stderr, "SSDP: Deleting remote device: %d\n", idx);
    
  if((uri = gavl_dictionary_get_string(dev, GAVL_META_URI)) &&
     (protocol = gavl_dictionary_get_string(dev, BG_SSDP_META_PROTOCOL)) &&
     (type = gavl_dictionary_get_string(dev, BG_SSDP_META_TYPE)))
    {
    char * real_url = NULL;
    char * pos = NULL;
    const gavl_array_t * embedded_devs;
    const gavl_dictionary_t * embedded_dev;
    gavl_msg_t * msg;
    bg_msg_sink_t * sink = bg_msg_hub_get_sink(s->event_hub);

    //    fprintf(stderr, "SSDP: Deleting remote device: %s\n", uri);
    //    gavl_dictionary_dump(dev, 2);

    if(!strcmp(protocol, "upnp") && (pos = strstr(uri, "://")))
      {
      if(!strcmp(type, "MediaServer"))
        {
        real_url = bg_sprintf(BG_BACKEND_URI_SCHEME_UPNP_SERVER"%s", pos);
        }
      else if(!strcmp(type, "MediaRenderer"))
        {
        real_url = bg_sprintf(BG_BACKEND_URI_SCHEME_UPNP_RENDERER"%s", pos);
        }
      }

    if(real_url)
      uri = real_url;
    
    if((embedded_devs = gavl_dictionary_get_array(dev, BG_SSDP_META_DEVICES)))
      {
      for(i = 0; i < embedded_devs->num_entries; i++)
        {

        if(!(embedded_dev = gavl_value_get_dictionary(&embedded_devs->entries[i])))
          continue;

        //        fprintf(stderr, "Got embedded dev\n");
        //        gavl_dictionary_dump(embedded_dev, 2);
        
        msg = bg_msg_sink_get(sink);

        
        bg_ssdp_msg_set_del(msg, uri);
        
        bg_msg_sink_put(sink, msg);
        }
      
      }

    msg = bg_msg_sink_get(sink);

    
    
    bg_ssdp_msg_set_del(msg, uri);
    
    bg_msg_sink_put(sink, msg);

    if(real_url)
      free(real_url);
      
    }
#if 0
  else
    {
    fprintf(stderr, "Couldn't delete device\n");
    gavl_dictionary_dump(dev, 2);
    }
#endif
  
  gavl_array_splice_val(&s->remote_devs, idx, 1, NULL);
  }

static char * search_string =
"M-SEARCH * HTTP/1.1\r\n"
"Host:239.255.255.250:1900\r\n"
"ST: ssdp:all\r\n"
"Man:\"ssdp:discover\"\r\n"
"MX:3\r\n\r\n";

bg_ssdp_t * bg_ssdp_create(bg_ssdp_root_device_t * local_dev)
  {
  const char * local_uri;
  char addr_str[GAVL_SOCKET_ADDR_STR_LEN];
  bg_ssdp_t * ret = calloc(1, sizeof(*ret));

  ret->event_hub = bg_msg_hub_create(1);
  
  ret->local_dev = local_dev;
  
  ret->last_notify_time = GAVL_TIME_UNDEFINED;
  ret->last_search_time = GAVL_TIME_UNDEFINED;
  
#ifdef DUMP_DEVS
  if(ret->local_dev)
    {
    gavl_dprintf("Local root device:\n");
    gavl_dictionary_dump(ret->local_dev, 2);
    }
#endif
  
  ret->sender_addr = gavl_socket_address_create();
  ret->local_addr  = gavl_socket_address_create();
  ret->mcast_addr  = gavl_socket_address_create();

  if(ret->local_dev && (local_uri = gavl_dictionary_get_string(ret->local_dev, GAVL_META_URI)))
    {
    /* If we advertise a device, we need to bind onto the same interface */
    char * host = NULL;
    bg_url_split(local_uri,
                 NULL,
                 NULL,
                 NULL,
                 &host,
                 NULL,
                 NULL);
    gavl_socket_address_set(ret->local_addr, host, 0, SOCK_DGRAM);
    free(host);
    }
  else if(!gavl_socket_address_set_local(ret->local_addr, 0, NULL))
    goto fail;

  if(!gavl_socket_address_set(ret->mcast_addr, "239.255.255.250", 1900, SOCK_DGRAM))
    goto fail;

  ret->mcast_fd = gavl_udp_socket_create_multicast(ret->mcast_addr);
  ret->ucast_fd = gavl_udp_socket_create(ret->local_addr);
  
  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Unicast socket bound at %s", 
         gavl_socket_address_to_string(ret->local_addr, addr_str));
  

  ret->timer = gavl_timer_create(ret->timer);
  gavl_timer_start(ret->timer);

  /* Create send queue */
  if(ret->local_dev)
    {
    int i;
    for(i = 0; i < QUEUE_SIZE; i++)
      {
      ret->queue[i].addr = gavl_socket_address_create();
      ret->queue[i].time = GAVL_TIME_UNDEFINED;
      }
    }
  return ret;

  fail:
  bg_ssdp_destroy(ret);
  return NULL;
  }

static char * uuid_from_usn(const char * usn)
  {
  const char * pos;
  if(gavl_string_starts_with(usn, "uuid:"))
    {
    pos = strchr(usn+5, ':');
    if(!pos)
      pos = usn + 5 + strlen(usn + 5);
    return gavl_strndup(usn+5, pos);
    }
  return NULL;
  }

static void update_device(bg_ssdp_t * s, const char * type,
                          gavl_dictionary_t * m)
  {
  int idx;
  int max_age = 0;
  const char * loc;
  const char * usn;
  const char * cc; 
  const char * pos;
  char * uuid;
  const char * type_version;
  const char * dev_uuid;
  
  bg_ssdp_root_device_t * dev;
  bg_ssdp_device_t * edev;

  bg_backend_type_t bt;
  
  //  fprintf(stderr, "Got ssdp packet:\n");
  //  gavl_dictionary_dump(m, 2);
  
  loc = gavl_dictionary_get_string_i(m, "LOCATION");
  if(!loc)
    return;

  /* Get max age */
  cc = gavl_dictionary_get_string_i(m, "CACHE-CONTROL");
  if(!cc || 
     !gavl_string_starts_with(cc, "max-age") ||
     !(pos = strchr(cc, '=')))
    return;
  pos++;
  max_age = atoi(pos);

  /* Get UUID */
  usn = gavl_dictionary_get_string_i(m, "USN");
  if(!usn)
    return;

  if(!(uuid = uuid_from_usn(usn)))
    return;
  

  /* For gmerlin it is quite simple */

  if((bt = bg_ssdp_is_gmerlin_nt(type)))
    {
    //    fprintf(stderr, "Found gmerlin %s at %s\n", bg_backend_type_to_string(bt),
    //            loc);
    
    idx = find_root_dev_by_location(s, loc);
    
    if(idx >= 0) // Update expire time
      {
      
      dev = gavl_value_get_dictionary_nc(&s->remote_devs.entries[idx]);
      
      gavl_dictionary_set_long(dev, BG_SSDP_META_EXPIRE_TIME,
                               gavl_timer_get(s->timer) + GAVL_TIME_SCALE * max_age);
      }
    else
      {
      gavl_msg_t * msg;
      bg_msg_sink_t * sink = bg_msg_hub_get_sink(s->event_hub);
      
      //      gavl_array_dump(&s->remote_devs, 2);

      type_version = bg_backend_type_to_string(bt);
      
      
      dev = add_root_dev(s, loc, uuid);

      gavl_dictionary_set_string(dev, BG_SSDP_META_PROTOCOL, "gmerlin");
      gavl_dictionary_set_string(dev, BG_SSDP_META_TYPE,     "type_version");
      
      gavl_dictionary_set_long(dev, BG_SSDP_META_EXPIRE_TIME,
                               gavl_timer_get(s->timer) + GAVL_TIME_SCALE * max_age);
      
      msg = bg_msg_sink_get(sink);
      
      bg_ssdp_msg_set_add(msg, "gmerlin", type_version, 0, loc);
      
      bg_msg_sink_put(sink, msg);
      
      //      fprintf(stderr, "New Array\n");
      //      gavl_array_dump(&s->remote_devs, 2);
      
      //      gavl_dictionary_dump(dev, 2);
      
      }
    goto end;
    }

  idx = find_root_dev_by_location(s, loc);
  
  if(idx >= 0)
    dev = gavl_value_get_dictionary_nc(&s->remote_devs.entries[idx]);
  else
    {
    dev = add_root_dev(s, loc, uuid);
    gavl_dictionary_set_string(dev, BG_SSDP_META_PROTOCOL, "upnp");
    }
  
  gavl_dictionary_set_long(dev, BG_SSDP_META_EXPIRE_TIME,
                           gavl_timer_get(s->timer) + GAVL_TIME_SCALE * max_age);

  dev_uuid = gavl_dictionary_get_string(dev, BG_SSDP_META_UUID);

  
  if(!strcasecmp(type, "upnp:rootdevice"))
    {
    /* Set root uuid if not known already */
    if(!dev_uuid)
      {
      gavl_dictionary_set_string(dev, BG_SSDP_META_UUID, uuid);
      dev_uuid = uuid;
      }
    goto end;
    }
  
  /* If we have no uuid of the root device yet, we can't do much more */
  if(!dev_uuid)
    goto end;

  if(gavl_string_starts_with_i(type, "uuid:"))
    {
    /* Can't do much here */
    goto end;
    }

  if(!strcmp(uuid, dev_uuid))
    {
    /* Message is for root device */
    edev = NULL;
    }
  else
    {
    idx = find_embedded_dev_by_uuid(dev, uuid);
    if(idx >= 0)
      {
      gavl_array_t * arr = gavl_dictionary_get_array_nc(dev, BG_SSDP_META_DEVICES);
      edev = gavl_value_get_dictionary_nc(&arr->entries[idx]);
      }
    else
      edev = bg_ssdp_device_add_device(dev, uuid);
    }

  if((type_version = is_device_type(type)))
    {
    int version = 0;
    gavl_msg_t * msg;
    bg_msg_sink_t * sink = bg_msg_hub_get_sink(s->event_hub);
    
    if(edev)
      {
      if(!gavl_dictionary_get(edev, BG_SSDP_META_TYPE))
        {
        extract_type_version(type_version, edev);
        gavl_dictionary_set_string(edev, BG_SSDP_META_PROTOCOL, "upnp");
        
        msg = bg_msg_sink_get(sink);
        gavl_dictionary_get_int(edev, BG_SSDP_META_VERSION, &version);
        
        bg_ssdp_msg_set_add(msg,
                            "upnp",
                            gavl_dictionary_get_string(edev, BG_SSDP_META_TYPE),
                            version,
                            loc);
        
        bg_msg_sink_put(sink, msg);
        }
      }
    else if(!gavl_dictionary_get(dev, BG_SSDP_META_TYPE))
      {
      extract_type_version(type_version, dev);
      gavl_dictionary_set_string(dev, BG_SSDP_META_PROTOCOL, "upnp");
      
      msg = bg_msg_sink_get(sink);
      gavl_dictionary_get_int(dev, BG_SSDP_META_VERSION, &version);
      
      bg_ssdp_msg_set_add(msg, "upnp",
                          gavl_dictionary_get_string(dev, BG_SSDP_META_TYPE),
                          version,
                          loc);
      
      bg_msg_sink_put(sink, msg);
      }
    }
  else if((type_version = is_service_type(type)))
    {
    bg_ssdp_service_t * s = NULL;
    
    if(edev)
      {
      if(!device_find_service(edev, type_version))
        s = device_add_service(edev);
      }
    else
      {
      if(!device_find_service(dev, type_version))
        s = device_add_service(dev);
      }    
    if(s)
      extract_type_version(type_version, s);
    }
  end:
  if(uuid)
    free(uuid);  
  }

static void schedule_reply(bg_ssdp_t * s, const char * st,
                           const gavl_socket_address_t * sender,
                           gavl_time_t current_time, int mx)
  {
  int i, idx = -1;
  for(i = 0; i < QUEUE_SIZE; i++)
    {
    if(!s->queue[i].st)
      {
      idx = i;
      break;
      }
    }
  if(idx == -1)
    {
    gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Cannot schedule search reply, queue full");
    return;
    }
  s->queue[idx].st = gavl_strdup(st);

  s->queue[idx].time = current_time;
  s->queue[idx].time += (int64_t)((double)rand() / (double)RAND_MAX * (double)(mx * GAVL_TIME_SCALE));
  gavl_socket_address_copy(s->queue[idx].addr, sender);
  }

// static void setup_header(bg_ssdp_root_device_t * dev, 

static void flush_reply_packet(bg_ssdp_t * s, const gavl_dictionary_t * h,
                               gavl_socket_address_t * sender)
  {
  int len = 0;
  char * str = gavl_http_response_to_string(h, &len);
  //  fprintf(stderr, "Sending search reply:\n%s\n", str);
  gavl_udp_socket_send(s->ucast_fd, (uint8_t*)str, len, sender);
  free(str);
  }
                               
static void flush_reply(bg_ssdp_t * s, queue_element_t * q)
  {
  const char * type_version;
  gavl_dictionary_t h;
  int i;
  gavl_dictionary_init(&h);
  gavl_http_response_init(&h, "HTTP/1.1", 200, "OK");
  gavl_dictionary_set_string(&h, "CACHE-CONTROL", "max-age=1800");
  bg_http_header_set_empty_var(&h, "EXT");
  gavl_dictionary_set_string(&h, "LOCATION", gavl_dictionary_get_string(s->local_dev, GAVL_META_URI));
  gavl_dictionary_set_string(&h, "SERVER", bg_upnp_get_server_string());
  
  if(!strcasecmp(q->st, "ssdp:all"))
    {
    const char * protocol;
    const gavl_array_t * arr;
    
    /*
     *  uuid:device-UUID::upnp:rootdevice
     *  uuid:device-UUID (for each root- and embedded dev)
     *  uuid:device-UUID::urn:schemas-upnp-org:device:deviceType:v (for each root- and embedded dev)
     *  uuid:device-UUID::urn:schemas-upnp-org:service:serviceType:v  (for each service)
     */

    protocol = gavl_dictionary_get_string(s->local_dev, BG_SSDP_META_PROTOCOL);
    
    /* Root device */
    gavl_dictionary_set_string_nocopy(&h, "USN", bg_ssdp_get_root_usn(s->local_dev));
    gavl_dictionary_set_string_nocopy(&h, "ST",  bg_ssdp_get_root_nt(s->local_dev));
    flush_reply_packet(s, &h, q->addr);

    if(!strcmp(protocol, "upnp"))
      {
      /* Device UUID */
      gavl_dictionary_set_string_nocopy(&h, "USN", bg_ssdp_get_device_uuid_usn(s->local_dev, -1));
      gavl_dictionary_set_string_nocopy(&h, "ST",  bg_ssdp_get_device_uuid_nt(s->local_dev, -1));
      flush_reply_packet(s, &h, q->addr);
  
      /* TODO: Embedded devices would come here */

      /* Device Type */
      gavl_dictionary_set_string_nocopy(&h, "USN", bg_ssdp_get_device_type_usn(s->local_dev, -1));
      gavl_dictionary_set_string_nocopy(&h, "ST",  bg_ssdp_get_device_type_nt(s->local_dev, -1));
      flush_reply_packet(s, &h, q->addr);
      /* TODO: Embedded devices would come here */

      if((arr = gavl_dictionary_get_array(s->local_dev, BG_SSDP_META_SERVICES)))
        {
        for(i = 0; i < arr->num_entries; i++)
          {
          gavl_dictionary_set_string_nocopy(&h, "USN", bg_ssdp_get_service_type_usn(s->local_dev, -1, i));
          gavl_dictionary_set_string_nocopy(&h, "ST",  bg_ssdp_get_service_type_nt(s->local_dev, -1, i));
          flush_reply_packet(s, &h, q->addr);
          }
        }
      }
    
    gavl_dictionary_free(&h);
    gavl_dictionary_init(&h);
    }
  else if(!strcasecmp(q->st, "upnp:rootdevice"))
    {
    /* Root device */
    gavl_dictionary_set_string_nocopy(&h, "USN", bg_ssdp_get_root_usn(s->local_dev));
    gavl_dictionary_set_string_nocopy(&h, "ST",  bg_ssdp_get_root_nt(s->local_dev));
    flush_reply_packet(s, &h, q->addr);
    }
  else if(gavl_string_starts_with_i(q->st, "uuid:"))
    {
    int dev = find_embedded_dev_by_uuid(s->local_dev, q->st+5);
    gavl_dictionary_set_string_nocopy(&h, "USN", bg_ssdp_get_device_uuid_usn(s->local_dev, dev));
    gavl_dictionary_set_string_nocopy(&h, "ST",  bg_ssdp_get_device_uuid_nt(s->local_dev, dev));
    flush_reply_packet(s, &h, q->addr);
    }
  else if((type_version = is_device_type(q->st)))
    {
    int dev;
    bg_ssdp_has_device_str(s->local_dev, type_version, &dev);
    gavl_dictionary_set_string_nocopy(&h, "USN", bg_ssdp_get_device_type_usn(s->local_dev, dev));
    gavl_dictionary_set_string_nocopy(&h, "ST",  bg_ssdp_get_device_type_nt(s->local_dev, dev));
    flush_reply_packet(s, &h, q->addr);
    }
  else if((type_version = is_service_type(q->st)))
    {
    int dev, srv;
    bg_ssdp_has_service_str(s->local_dev, type_version, &dev, &srv);
    gavl_dictionary_set_string_nocopy(&h, "USN", bg_ssdp_get_service_type_usn(s->local_dev, dev, srv));
    gavl_dictionary_set_string_nocopy(&h, "ST",  bg_ssdp_get_service_type_nt(s->local_dev, dev, srv));
    flush_reply_packet(s, &h, q->addr);
    }
  free(q->st);
  q->st = NULL;
  gavl_dictionary_free(&h);
  }

static int flush_queue(bg_ssdp_t * s, gavl_time_t current_time)
  {
  int ret = 0;
  int i;
  for(i = 0; i < QUEUE_SIZE; i++)
    {
    if(!s->queue[i].st || (s->queue[i].time > current_time))
      continue;

    flush_reply(s, &s->queue[i]);
    ret++;
    }
  return ret;
  }


static void handle_multicast(bg_ssdp_t * s, const char * buffer,
                             gavl_socket_address_t * sender, gavl_time_t current_time)
  {
  const char * var;
  gavl_dictionary_t m;
  gavl_dictionary_init(&m);
   
  if(!gavl_http_request_from_string(&m, buffer))
    goto fail;
#ifdef DUMP_HEADERS
  gavl_dprintf("handle_multicast\n"); 
  gavl_dictionary_dump(&m, 2);
#endif
  
  var = gavl_http_request_get_method(&m);
  
  if(!var)
    goto fail;

  if(!strcasecmp(var, "M-SEARCH"))
    {
    int mx;
    const char * type_version;
    
    /* Got search request */
    if(!s->local_dev)
      goto fail;

    //    fprintf(stderr, "Got search request\n");
    //    gavl_dictionary_dump(&m, 0);
    
    if(!gavl_dictionary_get_int_i(&m, "MX", &mx))
      goto fail;
    
    var = gavl_dictionary_get_string_i(&m, "ST");
    
    if(!strcasecmp(var, "ssdp:all"))
      {
      schedule_reply(s, var, sender,
                     current_time, mx);
      }
    else if(!strcasecmp(var, "upnp:rootdevice"))
      {
      schedule_reply(s, var, sender,
                     current_time, mx);
      }
    else if(gavl_string_starts_with_i(var, "uuid:"))
      {
      if(!strcmp(var + 5, gavl_dictionary_get_string(s->local_dev, BG_SSDP_META_UUID)) ||
         find_embedded_dev_by_uuid(s->local_dev, var + 5))
        schedule_reply(s, var, sender,
                       current_time, mx);
      }
    else if((type_version = is_device_type(var)))
      {
      if(bg_ssdp_has_device_str(s->local_dev, type_version, NULL))
        schedule_reply(s, var, sender,
                       current_time, mx);
      }
    else if((type_version = is_service_type(var)))
      {
      //      fprintf(stderr, "Got service search\n");
      if(bg_ssdp_has_service_str(s->local_dev, type_version, NULL, NULL))
        schedule_reply(s, var, sender,
                       current_time, mx);
      }
    
    /*
     *  Ignoring
     *  urn:domain-name:device:deviceType:v 
     *  and
     *  urn:domain-name:service:serviceType:v
     */
    
    }
  else if(!strcasecmp(var, "NOTIFY"))
    {
    const char * nt;
    const char * nts;
    /* Got notify request */
    
    nts = gavl_dictionary_get_string_i(&m, "NTS");
    
    if(!strcmp(nts, "ssdp:alive"))
      {
      if(!(nt = gavl_dictionary_get_string_i(&m, "NT")))
        goto fail;
      
      update_device(s, nt, &m);
      }
    else if(!strcmp(nts, "ssdp:byebye"))
      {
      /* We delete the entire root device */
      char * uuid;
      const char * usn;
      int i;

      //      fprintf(stderr, "Got byebye message\n");
      //      gavl_dictionary_dump(&m, 2);
      
      if(!(usn = gavl_dictionary_get_string(&m, "USN")) ||
         !(uuid = uuid_from_usn(usn)))
        {
        //        fprintf(stderr, "Got no uuid from usn %s\n", usn);
        goto fail;
        }

      //  fprintf(stderr, "Deleting uuid %s\n", uuid);
      
      i = 0; 
      
      while(i < s->remote_devs.num_entries)
        {
        const char * dev_uuid;
        const gavl_dictionary_t * remote_dev;

        //        fprintf(stderr, "Device: %s\n", gavl_dictionary_get_string(gavl_value_get_dictionary(&s->remote_devs.entries[i]), BG_SSDP_META_UUID));
        
        //        gavl_dictionary_dump(gavl_value_get_dictionary(&s->remote_devs.entries[i]), 2);
        
        if((remote_dev = gavl_value_get_dictionary(&s->remote_devs.entries[i])) &&
           (((dev_uuid = gavl_dictionary_get_string(remote_dev, BG_SSDP_META_UUID)) &&
             !strcmp(dev_uuid, uuid)) ||
            (find_embedded_dev_by_uuid(remote_dev, uuid) >= 0)))
          {
          del_remote_dev(s, i);
          break;
          }
        else
          i++;
        }
      
      free(uuid);
      }
    }
  
  fail:
  gavl_dictionary_free(&m);
  }

static void handle_unicast(bg_ssdp_t * s, const char * buffer,
                           gavl_socket_address_t * sender)
  {
  const char * st;
  gavl_dictionary_t m;
  gavl_dictionary_init(&m);

  if(!gavl_http_response_from_string(&m, buffer))
    goto fail;

#ifdef DUMP_HEADERS
  gavl_dprintf("handle_unicast\n");
  gavl_dictionary_dump(&m, 2);
#endif
  
  if(!(st = gavl_dictionary_get_string_i(&m, "ST")))
    goto fail;
  
  update_device(s, st, &m);

  
  fail:
  gavl_dictionary_free(&m);
  }

static void flush_notify(bg_ssdp_t * s, const gavl_dictionary_t * h)
  {
  int len = 0;
  char * str = gavl_http_request_to_string(h, &len);
  gavl_udp_socket_send(s->ucast_fd, (uint8_t*)str, len, s->mcast_addr);
  //  fprintf(stderr, "Notify:\n%s", str);
  free(str);
  }

static void notify(bg_ssdp_t * s, int alive)
  {
  int i;
  const gavl_array_t * arr;
  gavl_dictionary_t m;
  const char * protocol;
  
  gavl_dictionary_init(&m);

  protocol = gavl_dictionary_get_string(s->local_dev, BG_SSDP_META_PROTOCOL);
  
  /* Common fields */
  gavl_http_request_init(&m, "NOTIFY", "*", "HTTP/1.1");
  gavl_dictionary_set_string(&m, "HOST", "239.255.255.250:1900");

  if(alive)
    {
    gavl_dictionary_set_string(&m, "CACHE-CONTROL", "max-age=1800");
    gavl_dictionary_set_string(&m, "NTS", "ssdp:alive");
    }
  else
    {
    gavl_dictionary_set_string(&m, "NTS", "ssdp:byebye");
    }
  
  gavl_dictionary_set_string(&m, "SERVER", bg_upnp_get_server_string());
  gavl_dictionary_set_string(&m, "LOCATION", gavl_dictionary_get_string(s->local_dev, GAVL_META_URI));
  
  /* Root device */
  gavl_dictionary_set_string_nocopy(&m, "USN", bg_ssdp_get_root_usn(s->local_dev));
  gavl_dictionary_set_string_nocopy(&m, "NT",  bg_ssdp_get_root_nt(s->local_dev));
  flush_notify(s, &m);
  
  if(!strcmp(protocol, "upnp"))
    {
    
    /* Device UUID */
    gavl_dictionary_set_string_nocopy(&m, "USN", bg_ssdp_get_device_uuid_usn(s->local_dev, -1));
    gavl_dictionary_set_string_nocopy(&m, "NT",  bg_ssdp_get_device_uuid_nt(s->local_dev, -1));
    flush_notify(s, &m);
  
    /* TODO: Embedded devices would come here */

    /* Device Type */
    gavl_dictionary_set_string_nocopy(&m, "USN", bg_ssdp_get_device_type_usn(s->local_dev, -1));
    gavl_dictionary_set_string_nocopy(&m, "NT",  bg_ssdp_get_device_type_nt(s->local_dev, -1));
    flush_notify(s, &m);
    /* TODO: Embedded devices would come here */
  
    if((arr = gavl_dictionary_get_array(s->local_dev, BG_SSDP_META_SERVICES)))
      {
      for(i = 0; i < arr->num_entries; i++)
        {
        gavl_dictionary_set_string_nocopy(&m, "USN", bg_ssdp_get_service_type_usn(s->local_dev, -1, i));
        gavl_dictionary_set_string_nocopy(&m, "NT",  bg_ssdp_get_service_type_nt(s->local_dev, -1, i));
        flush_notify(s, &m);
        }
      }

    
    }
  
  gavl_dictionary_free(&m);
  }

int bg_ssdp_update(bg_ssdp_t * s)
  {
  int len;
  int i;
  gavl_time_t current_time;
#ifdef DUMP_UDP
  char addr_str[GAVL_SOCKET_ADDR_STR_LEN];
#endif
  int ret = 0;
  
  /* Delete expired devices */
  current_time = gavl_timer_get(s->timer);
  i = 0;  
  while(i < s->remote_devs.num_entries)
    {
    gavl_time_t expire_time;
    const gavl_dictionary_t * dev;

    if(!(dev = gavl_value_get_dictionary(&s->remote_devs.entries[i])) ||
       !gavl_dictionary_get_long(dev, BG_SSDP_META_EXPIRE_TIME, &expire_time) ||  
       (expire_time < current_time))
      {
      del_remote_dev(s, i);
      //      fprintf(stderr, "Expired\n");
      }
    else
      i++;
    }

  /* Read multicast messages */
  while(gavl_socket_can_read(s->mcast_fd, 0))
    {
    len = gavl_udp_socket_receive(s->mcast_fd, s->buf, UDP_BUFFER_SIZE, s->sender_addr);
    if(len <= 0)
      continue;
    s->buf[len] = '\0';
    handle_multicast(s, (const char *)s->buf, s->sender_addr, current_time);
#ifdef DUMP_UDP
    fprintf(stderr, "Got SSDP multicast message from %s\n%s",
            gavl_socket_address_to_string(s->sender_addr, addr_str), (char*)s->buf);
#endif
    ret++;
    }

  /* Read unicast messages */
  while(gavl_socket_can_read(s->ucast_fd, 0))
    {
    len = gavl_udp_socket_receive(s->ucast_fd, s->buf, UDP_BUFFER_SIZE, s->sender_addr);
    if(len <= 0)
      continue;
    s->buf[len] = '\0';
    handle_unicast(s, (const char *)s->buf, s->sender_addr);
#ifdef DUMP_UDP
    fprintf(stderr, "Got SSDP unicast message from %s\n%s",
            gavl_socket_address_to_string(s->sender_addr, addr_str), (char*)s->buf);
#endif
    ret++;
    }

#ifdef DUMP_DEVS
  if((current_time - s->last_dump_time > 10 * GAVL_TIME_SCALE) && s->discover_remote)
    {
    gavl_dprintf("Root devices: %d\n", s->num_remote_devs);
    for(i = 0; i < s->num_remote_devs; i++)
      bg_ssdp_root_device_dump(&s->remote_devs[i]);
    s->last_dump_time = current_time;
    }
#endif

  ret += flush_queue(s, current_time);

  if(s->local_dev)
    {
    if((s->last_notify_time == GAVL_TIME_UNDEFINED) ||
       (!s->notify_count && (current_time - s->last_notify_time > NOTIFY_INTERVAL_LONG)) ||
       (s->notify_count && (current_time - s->last_notify_time > NOTIFY_INTERVAL_SHORT)))
      {
      notify(s, 1);
      gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Sent notify packet");
      s->last_notify_time = current_time;
      
      s->notify_count++;
      if(s->notify_count > NOTIFY_NUM)
        s->notify_count = 0;
      }
 
    }

  if((s->last_notify_time == GAVL_TIME_UNDEFINED) ||
     ((s->search_count < SEARCH_NUM) && (current_time - s->last_notify_time >= SEARCH_INTERVAL)))
    {
    /* Send search packet */
    gavl_udp_socket_send(s->ucast_fd, (uint8_t*)search_string,
                         strlen(search_string), s->mcast_addr);
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Sent discovery packet");
    s->search_count++;
    }
  
  return ret;
  }

void bg_ssdp_destroy(bg_ssdp_t * s)
  {
  int i;
  /* If we advertised a local device, unadvertise it */
  if(s->local_dev)
    {
    notify(s, 0);
    }
  for(i = 0; i < QUEUE_SIZE; i++)
    {
    if(s->queue[i].addr)
      gavl_socket_address_destroy(s->queue[i].addr);
    if(s->queue[i].st)
      free(s->queue[i].st);
    }
  
  gavl_array_free(&s->remote_devs);
  
  gavl_socket_close(s->ucast_fd);
  gavl_socket_close(s->mcast_fd);
  
  if(s->local_addr) 
    gavl_socket_address_destroy(s->local_addr);
  if(s->mcast_addr) 
    gavl_socket_address_destroy(s->mcast_addr);
  if(s->sender_addr) 
    gavl_socket_address_destroy(s->sender_addr);

  if(s->timer)
    gavl_timer_destroy(s->timer);

  if(s->event_hub)
    bg_msg_hub_destroy(s->event_hub);
  
  free(s);
  }

void bg_ssdp_browse(bg_ssdp_t * s, bg_msg_sink_t * sink)
  {
  const char * type;
  const char * uri;
  gavl_msg_t * msg;
  int i, j;
  const gavl_array_t * arr;
  
  for(i = 0; i < s->remote_devs.num_entries; i++)
    {
    const gavl_dictionary_t * dev;

    if(!(dev = gavl_value_get_dictionary(&s->remote_devs.entries[i])))
      continue;

    uri  = gavl_dictionary_get_string(dev, GAVL_META_URI);

    
    
    if((type = gavl_dictionary_get_string(dev, BG_SSDP_META_TYPE)))
      {
      int version = 0;
      
      msg = bg_msg_sink_get(sink);

      gavl_dictionary_get_int(dev, BG_SSDP_META_VERSION, &version);
      
      bg_ssdp_msg_set_add(msg, "upnp",
                      type, version,
                      uri);
      
      bg_msg_sink_put(sink, msg);
      
      }

    if((arr = gavl_dictionary_get_array(dev, BG_SSDP_META_DEVICES)))
      {
      for(j = 0; j < arr->num_entries; j++)
        {
        if(!(dev = gavl_value_get_dictionary(&arr->entries[i])))
          continue;

        if((type = gavl_dictionary_get_string(dev, BG_SSDP_META_TYPE)))
          {
          int version = 0;
      
          msg = bg_msg_sink_get(sink);

          gavl_dictionary_get_int(dev, BG_SSDP_META_VERSION, &version);
        
          bg_ssdp_msg_set_add(msg, "upnp", 
                              type, version, uri);

          bg_msg_sink_put(sink, msg);
          }
        }

      }


    
    }
  }

/* Create device info */
void bg_create_ssdp_device(gavl_dictionary_t * desc, bg_backend_type_t type,
                           const char * uri, const char * protocol)
  {
  gavl_dictionary_t * service;

  char uuid[37];

  bg_uri_to_uuid(uri, uuid);
  
  /* Create ssdp device description */

  gavl_dictionary_set_string(desc, BG_SSDP_META_UUID, uuid);

  gavl_dictionary_set_string(desc, BG_SSDP_META_PROTOCOL, protocol);
  
  gavl_dictionary_set_string(desc, GAVL_META_URI, uri);

  if(!strcmp(protocol, "upnp"))
    {
    switch(type)
      {
      case BG_BACKEND_MEDIASERVER:
        gavl_dictionary_set_string(desc, BG_SSDP_META_TYPE, "MediaServer");
        gavl_dictionary_set_int(desc,    BG_SSDP_META_VERSION, 1);

        service = device_add_service(desc);
        gavl_dictionary_set_string(service, BG_SSDP_META_TYPE, "ContentDirectory");
        gavl_dictionary_set_int(service, BG_SSDP_META_VERSION, 1);

        service = device_add_service(desc);
        gavl_dictionary_set_string(service, BG_SSDP_META_TYPE, "ConnectionManager");
        gavl_dictionary_set_int(service, BG_SSDP_META_VERSION, 1);
      
        break;
      case BG_BACKEND_RENDERER:
        gavl_dictionary_set_string(desc, BG_SSDP_META_TYPE, "MediaRenderer");
        gavl_dictionary_set_int(desc,    BG_SSDP_META_VERSION, 1);
      
        service = device_add_service(desc);
        gavl_dictionary_set_string(service, BG_SSDP_META_TYPE, "ConnectionManager");
        gavl_dictionary_set_int(service, BG_SSDP_META_VERSION, 1);
      
        service = device_add_service(desc);
        gavl_dictionary_set_string(service, BG_SSDP_META_TYPE, "RenderingControl");
        gavl_dictionary_set_int(service, BG_SSDP_META_VERSION, 1);

        service = device_add_service(desc);
        gavl_dictionary_set_string(service, BG_SSDP_META_TYPE, "AVTransport");
        gavl_dictionary_set_int(service, BG_SSDP_META_VERSION, 1);

        break;
      case BG_BACKEND_NONE:
      case BG_BACKEND_STATE:
        break;
      }
    }
  else if(!strcmp(protocol, "gmerlin"))
    {
    gavl_dictionary_set_string(desc, BG_SSDP_META_TYPE, bg_backend_type_to_string(type));
    }
  }
