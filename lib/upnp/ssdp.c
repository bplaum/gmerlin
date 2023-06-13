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
#include <errno.h>

#include <gavl/gavl.h>
#include <gavl/metadata.h>
#include <gavl/metatags.h>
#include <gavl/gavlsocket.h>

#include <gmerlin/upnp/ssdp.h>
#include <gmerlin/upnp/upnputils.h>
#include <gmerlin/upnp/devicedesc.h>

#include <gmerlin/translation.h>
#include <gmerlin/http.h>
#include <backend_priv.h>

#include <gmerlin/log.h>
#define LOG_DOMAIN "ssdp"

#include <gmerlin/utils.h>

#include <unistd.h>

#define UDP_BUFFER_SIZE 2048

#define META_ID "GMERLIN-ID"

#define QUEUE_TIME "t" // Time when this should be sent
#define QUEUE_ADDR "a" // Socket address (as string)
#define QUEUE_MSG  "m" // Message (as string)

/* Added to the device description */

#define NEXT_NOTIFY_TIME "NNT"
#define NOTIFY_COUNT     "NC"

#define EXPIRE_TIME      "ET"
#define MAX_AGE          1800

#define UPNP_SERVER_NT_PREFIX      "urn:schemas-upnp-org:device:MediaServer:"
#define UPNP_RENDERER_NT_PREFIX    "urn:schemas-upnp-org:device:MediaRenderer:"

#define UPNP_SERVER_NT      UPNP_SERVER_NT_PREFIX"1"
#define UPNP_RENDERER_NT    UPNP_RENDERER_NT_PREFIX"1"

#define GMERLIN_SERVER_NT   "urn:gmerlin-sourceforge-net:device:MediaServer:"
#define GMERLIN_RENDERER_NT "urn:gmerlin-sourceforge-net:device:MediaRenderer:"


struct  bg_ssdp_s
  {
  int mcast_fd;
  int ucast_fd;
  gavl_timer_t * timer;
  gavl_socket_address_t * addr;
  gavl_socket_address_t * mcast_addr;
  
  uint8_t buf[UDP_BUFFER_SIZE];

  gavl_array_t unicast_queue;
  gavl_array_t multicast_queue;

  /* Updated once each call to bg_ssdp_update() */
  gavl_time_t cur_time;
  gavl_time_t next_search_time;

  /* Reduce the frequency of multicast packets */
  gavl_time_t next_mcast_time;

  int search_count;
  
  };

static void notify_dev(bg_ssdp_t * ssdp, const gavl_dictionary_t * dev, int alive);


static int get_max_age(const gavl_dictionary_t * m)
  {
  const char * str;
  if(!(str = gavl_dictionary_get_string_i(m, "CACHE-CONTROL")) ||
     !gavl_string_starts_with(str, "max-age") ||
     !(str = strchr(str, '=')))
    return 0;
  str++;
  return atoi(str);
  }

static int rand_long(int64_t min, int64_t max)
  {
  return min + (int64_t)((double)rand() / (double)RAND_MAX * (double)(max-min));
  }

static int flush_multicast(bg_ssdp_t * ssdp, int force)
  {
  int ret = 0;
  const gavl_dictionary_t * msg;
  char * msg_str = NULL;
  int msg_len = 0;
  
  const gavl_dictionary_t * dict;

  //  fprintf(stderr, "Flush multicast 1 %d %"PRId64" %"PRId64"\n", ssdp->multicast_queue.num_entries,
  //          gavl_timer_get(ssdp->timer), ssdp->next_mcast_time);
  
  if(!ssdp->multicast_queue.num_entries  ||
     (!force && (gavl_timer_get(ssdp->timer) < ssdp->next_mcast_time)))
    return 0;

  //  fprintf(stderr, "Flush multicast 2\n");
  
  
  if(!(dict = gavl_value_get_dictionary(&ssdp->multicast_queue.entries[0])))
    {
    gavl_array_splice_val(&ssdp->multicast_queue, 0, 1, NULL);
    return 1;
    }

  // fprintf(stderr, "Flush multicast 3\n");
  
  msg = gavl_dictionary_get_dictionary(dict, QUEUE_MSG);
  msg_str = gavl_http_request_to_string(msg, &msg_len);
                              
  gavl_udp_socket_send(ssdp->ucast_fd, (uint8_t*)msg_str, msg_len, ssdp->mcast_addr);

  //  fprintf(stderr, "Flush multicast:\n%s\n", msg_str);
  //  gavl_hexdump((uint8_t*)msg_str, msg_len, 16);
  
  free(msg_str);
  
  
  gavl_array_splice_val(&ssdp->multicast_queue, 0, 1, NULL);
  ret = 1;

  if(!force)
    ssdp->next_mcast_time = gavl_timer_get(ssdp->timer) + rand_long(GAVL_TIME_SCALE / 100, GAVL_TIME_SCALE / 10);
  return ret;
  }

static void flush_multicast_force(bg_ssdp_t * ssdp)
  {
  while(ssdp->multicast_queue.num_entries)
    flush_multicast(ssdp, 1);
  
  }

static int flush_unicast(bg_ssdp_t * ssdp)
  {
  int ret = 0;
  int i = 0;
  gavl_time_t t = 0;
  const gavl_dictionary_t * dict;

  while(i < ssdp->unicast_queue.num_entries)
    {
    const char * addr;
    const gavl_dictionary_t * msg;
    
    if(!(dict = gavl_value_get_dictionary(&ssdp->unicast_queue.entries[i])))
      {
      gavl_array_splice_val(&ssdp->unicast_queue, i, 1, NULL);
      continue;
      }
    
    if((!gavl_dictionary_get_long(dict, QUEUE_TIME, &t) ||
        (ssdp->cur_time >= t)) &&
        (msg = gavl_dictionary_get_dictionary(dict, QUEUE_MSG)) &&
       (addr = gavl_dictionary_get_string(dict, QUEUE_ADDR)) &&
       gavl_socket_address_from_string(ssdp->addr, addr))
      {
      char * msg_str;
      int msg_len;
      msg_str = gavl_http_response_to_string(msg, &msg_len);

      //      fprintf(stderr, "Flush unicast %s\n%s", addr, msg_str);
      
      if(gavl_udp_socket_send(ssdp->mcast_fd, (uint8_t*)msg_str, msg_len, ssdp->addr) < 0)
        gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Flush unicast failed: %s", strerror(errno));
      
      gavl_array_splice_val(&ssdp->unicast_queue, i, 1, NULL);
      ret++;
      free(msg_str);
      }
    else
      i++;
    
    }
  return ret;
  }

static void queue_unicast(bg_ssdp_t * ssdp,
                          const gavl_dictionary_t * msg,
                          const gavl_socket_address_t * addr,
                          gavl_time_t delay)
  {
  char addr_str[GAVL_SOCKET_ADDR_STR_LEN];

  gavl_dictionary_t * dict;
  gavl_value_t val;
  gavl_value_init(&val);
  dict = gavl_value_set_dictionary(&val);

  gavl_dictionary_set_dictionary(dict, QUEUE_MSG, msg);
  
  gavl_socket_address_to_string(addr, addr_str);
  gavl_dictionary_set_string(dict, QUEUE_ADDR, addr_str);
  
  if(delay != GAVL_TIME_UNDEFINED)
    gavl_dictionary_set_long(dict, QUEUE_TIME, ssdp->cur_time + delay);

  gavl_array_splice_val_nocopy(&ssdp->unicast_queue, -1, 0, &val);
  
  } 

static void queue_multicast(bg_ssdp_t * ssdp, const gavl_dictionary_t * msg)
  {
  gavl_dictionary_t * dict;
  gavl_value_t val;
  gavl_value_init(&val);
  dict = gavl_value_set_dictionary(&val);
  gavl_dictionary_set_dictionary(dict, QUEUE_MSG, msg);
  gavl_array_splice_val_nocopy(&ssdp->multicast_queue, -1, 0, &val);
  }


bg_ssdp_t * bg_ssdp_create(void)
  {
  char addr_str[GAVL_SOCKET_ADDR_STR_LEN];
  
  bg_ssdp_t * ret = calloc(1, sizeof(*ret));

  ret->timer = gavl_timer_create();
  gavl_timer_start(ret->timer);
  ret->addr = gavl_socket_address_create();
  ret->mcast_addr = gavl_socket_address_create();
  
  /* Create multicast socket */
  gavl_socket_address_set(ret->mcast_addr, "239.255.255.250", 1900, SOCK_DGRAM);
  ret->mcast_fd = gavl_udp_socket_create_multicast(ret->mcast_addr);

  gavl_socket_address_set(ret->addr, "0.0.0.0", 0, SOCK_DGRAM);
  
  ret->ucast_fd = gavl_udp_socket_create(ret->addr);
  
  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Unicast socket bound at %s", 
           gavl_socket_address_to_string(ret->addr, addr_str));
  
  ret->next_search_time = gavl_timer_get(ret->timer);
  
  ret->next_mcast_time = gavl_timer_get(ret->timer) + rand_long(GAVL_TIME_SCALE / 10, GAVL_TIME_SCALE / 5);
  
  return ret;
  }

static gavl_dictionary_t * get_next_dev(int * idx, int local)
  {
  const char * protocol;
  gavl_dictionary_t * dict;
  bg_backend_registry_t * reg = bg_get_backend_registry();
  gavl_array_t * arr = local ? &reg->local_devs : &reg->devs;
  
  if(*idx >= arr->num_entries)
    return NULL;

  while(*idx < arr->num_entries)
    {
    if((dict = gavl_value_get_dictionary_nc(&arr->entries[*idx])) &&
       (protocol = gavl_dictionary_get_string(dict, BG_BACKEND_PROTOCOL)) &&
       (!strcmp(protocol, "gmerlin") ||
        !strcmp(protocol, "upnp")))
      {
      return dict;
      }
    (*idx)++;
    }
  
  return NULL;
  }

void bg_ssdp_destroy(bg_ssdp_t * s)
  {
  /* Send final messages */
  int i = 0;
  const gavl_dictionary_t * dev;

  gavl_array_reset(&s->multicast_queue);

  while((dev = get_next_dev(&i, 1)))
    {
    notify_dev(s, dev, 0);
    i++;
    }
  flush_multicast_force(s);
  
  gavl_array_free(&s->unicast_queue);
  gavl_array_free(&s->multicast_queue);
  
  
  gavl_timer_destroy(s->timer);
  gavl_socket_address_destroy(s->addr);
  gavl_socket_address_destroy(s->mcast_addr);

  if(s->mcast_fd > 0)
    gavl_socket_close(s->mcast_fd);
  if(s->ucast_fd > 0)
    gavl_socket_close(s->ucast_fd);

  
  
    
  
  free(s);
  }


static void update_remote_device(bg_ssdp_t * s, int alive, const gavl_dictionary_t * header)
  {
  gavl_dictionary_t * dict;
  
  char * real_uri = NULL;
  int is_upnp = 0;
  int max_age = 0;
  const char * nt;
  const char * uri;
  gavl_value_t new_val;
  char ** str = NULL;
  int idx = 0;
  int type = BG_BACKEND_NONE;
  
  gavl_value_init(&new_val);
  
  if(!(nt = gavl_dictionary_get_string_i(header, "NT")) &&
     !(nt = gavl_dictionary_get_string_i(header, "ST")))
    return;

  if(!(uri = gavl_dictionary_get_string_i(header, "LOCATION")))
    return;

  if(alive && ((max_age = get_max_age(header)) <= 0))
    return;
  
  if(gavl_string_starts_with_i(nt, UPNP_SERVER_NT_PREFIX))
    {
    is_upnp = 1;
    real_uri = gavl_sprintf("%s%s", BG_BACKEND_URI_SCHEME_UPNP_SERVER, strstr(uri, "://"));
    type = BG_BACKEND_MEDIASERVER;
    }
  else if(gavl_string_starts_with_i(nt, UPNP_RENDERER_NT_PREFIX))
    {
    real_uri = gavl_sprintf("%s%s", BG_BACKEND_URI_SCHEME_UPNP_RENDERER, strstr(uri, "://"));
    type = BG_BACKEND_RENDERER;

    is_upnp = 1;
    }
  else if(!strcasecmp(nt, GMERLIN_SERVER_NT))
    {
    real_uri = gavl_strdup(uri);
    type = BG_BACKEND_MEDIASERVER;
    }
  else if(!strcasecmp(nt, GMERLIN_RENDERER_NT))
    {
    real_uri = gavl_strdup(uri);
    type = BG_BACKEND_RENDERER;
    }
  else
    goto end;
  
  /* Ignore local devices */
  if(bg_backend_by_str(GAVL_META_URI, real_uri, 1, NULL))
    goto end;

  dict = bg_backend_by_str(GAVL_META_URI, real_uri, 0, &idx);
  
  if(!alive)
    {
    if(!dict)
      goto end;
      
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Removing %s: Got ssdp bye", real_uri);
    bg_backend_del_remote(real_uri);
    goto end;
    }
  
  if(!dict)
    {
    //    fprintf(stderr, "New device:\n");
    //    gavl_dictionary_dump(header, 2);

    /* Create new */
    dict = gavl_value_set_dictionary(&new_val);
    gavl_dictionary_set_string(dict, GAVL_META_URI, real_uri);
    
    gavl_dictionary_set_int(dict, BG_BACKEND_TYPE, type);
    gavl_dictionary_set_string(dict, GAVL_META_ID, gavl_dictionary_get_string(header, META_ID));
    
    if(is_upnp)
      gavl_dictionary_set_string(dict, BG_BACKEND_PROTOCOL, "upnp");
    else
      gavl_dictionary_set_string(dict, BG_BACKEND_PROTOCOL, "gmerlin");
    
    if(is_upnp)
      {
      // "urn:schemas-upnp-org:device:MediaServer:1" 
      
      str = gavl_strbreak(nt, ':');

      if(str[4] && !bg_upnp_device_get_node_info(dict, str[3], atoi(str[4])))
        goto end;
      
      }
    else // gmerlin
      {
      if(!bg_backend_get_node_info(dict))
        goto end;
      
      }

    gavl_dictionary_set_long(dict, EXPIRE_TIME,
                             gavl_timer_get(s->timer) + max_age * GAVL_TIME_SCALE);
    
    /* Got new Device */
    bg_backend_add_remote(dict);
    }
  else
    {
    gavl_dictionary_set_long(dict, EXPIRE_TIME,
                             gavl_timer_get(s->timer) + max_age * GAVL_TIME_SCALE);
    }
  end:

  if(real_uri)
    free(real_uri);
  
  if(str)
    gavl_strbreak_free(str);

  gavl_value_free(&new_val);
  }



/* Notify */


static void notify_dev(bg_ssdp_t * ssdp, const gavl_dictionary_t * dev, int alive)
  {
  int type = 0;
  
  gavl_dictionary_t m;
  const char * uri;
  char uuid[37];
  const char * id = gavl_dictionary_get_string(dev, GAVL_META_ID);
  
  gavl_dictionary_init(&m);

  
  /* Common fields */
  gavl_http_request_init(&m, "NOTIFY", "*", "HTTP/1.1");
  gavl_dictionary_set_string(&m, "HOST", "239.255.255.250:1900");
  
  if(alive)
    {
    gavl_dictionary_set_string_nocopy(&m, "CACHE-CONTROL", gavl_sprintf("max-age=%d", MAX_AGE));
    gavl_dictionary_set_string(&m, "NTS", "ssdp:alive");
    }
  else
    {
    gavl_dictionary_set_string(&m, "NTS", "ssdp:byebye");
    }
  
  gavl_dictionary_set_string_nocopy(&m, "SERVER", bg_upnp_make_server_string());

  uri = gavl_dictionary_get_string(dev, GAVL_META_URI);
  bg_uri_to_uuid(uri, uuid);
  
  gavl_dictionary_get_int(dev, BG_BACKEND_TYPE, &type);
  
  if(gavl_string_starts_with(uri, BG_BACKEND_URI_SCHEME_UPNP_RENDERER) ||
     gavl_string_starts_with(uri, BG_BACKEND_URI_SCHEME_UPNP_SERVER))
    {
    gavl_dictionary_set_string_nocopy(&m, "LOCATION", gavl_sprintf("http%s", strstr(uri, "://")));

    gavl_dictionary_set_string(&m, "NT", "upnp:rootdevice");
    gavl_dictionary_set_string_nocopy(&m, "USN", gavl_sprintf("uuid:%s::upnp:rootdevice", uuid));
    queue_multicast(ssdp, &m);

    gavl_dictionary_set_string_nocopy(&m, "NT", gavl_sprintf("uuid:%s", uuid));
    gavl_dictionary_set_string_nocopy(&m, "USN", gavl_sprintf("uuid:%s", uuid));
    queue_multicast(ssdp, &m);
    
    switch(type)
      {
      case BG_BACKEND_MEDIASERVER:
        gavl_dictionary_set_string(&m, "NT", "urn:schemas-upnp-org:device:MediaServer:1");
        gavl_dictionary_set_string_nocopy(&m, "USN", gavl_sprintf("uuid:%s::urn:schemas-upnp-org:device:MediaServer:1", uuid));
        gavl_dictionary_set_string(&m, META_ID, id);
        queue_multicast(ssdp, &m);
        gavl_dictionary_set_string(&m, META_ID, NULL);
        
        gavl_dictionary_set_string(&m, "NT", "urn:schemas-upnp-org:service:ContentDirectory:1");
        gavl_dictionary_set_string_nocopy(&m, "USN", gavl_sprintf("uuid:%s::urn:schemas-upnp-org:service:ContentDirectory:1", uuid));
        queue_multicast(ssdp, &m);
        
        gavl_dictionary_set_string(&m, "NT", "urn:schemas-upnp-org:service:ConnectionManager:1");
        gavl_dictionary_set_string_nocopy(&m, "USN", gavl_sprintf("uuid:%s::urn:schemas-upnp-org:service:ConnectionManager:1", uuid));
        queue_multicast(ssdp, &m);
        
        break;
      case BG_BACKEND_RENDERER:
        gavl_dictionary_set_string(&m, "NT", "urn:schemas-upnp-org:device:MediaRenderer:1");
        gavl_dictionary_set_string_nocopy(&m, "USN", gavl_sprintf("uuid:%s::urn:schemas-upnp-org:device:MediaRenderer:1", uuid));
        gavl_dictionary_set_string(&m, META_ID, id);
        queue_multicast(ssdp, &m);
        gavl_dictionary_set_string(&m, META_ID, NULL);
        
        gavl_dictionary_set_string(&m, "NT", "urn:schemas-upnp-org:service:ConnectionManager:1");
        gavl_dictionary_set_string_nocopy(&m, "USN", gavl_sprintf("uuid:%s::urn:schemas-upnp-org:service:ConnectionManager:1", uuid));
        queue_multicast(ssdp, &m);

        gavl_dictionary_set_string(&m, "NT", "urn:schemas-upnp-org:service:RenderingControl:1");
        gavl_dictionary_set_string_nocopy(&m, "USN", gavl_sprintf("uuid:%s::urn:schemas-upnp-org:service:RenderingControl:1", uuid));
        queue_multicast(ssdp, &m);

        gavl_dictionary_set_string(&m, "NT", "urn:schemas-upnp-org:service:AVTransport:1");
        gavl_dictionary_set_string_nocopy(&m, "USN", gavl_sprintf("uuid:%s::urn:schemas-upnp-org:service:AVTransport:1", uuid));
        queue_multicast(ssdp, &m);
        
        break;
      }

    }
  else
    {
    gavl_dictionary_set_string(&m, "LOCATION", uri);
    
    switch(type)
      {
      case BG_BACKEND_MEDIASERVER:
        gavl_dictionary_set_string(&m, "NT", GMERLIN_SERVER_NT);
        gavl_dictionary_set_string_nocopy(&m, "USN", gavl_sprintf("uuid:%s::%s", uuid, GMERLIN_SERVER_NT));
        gavl_dictionary_set_string(&m, META_ID, id);
        queue_multicast(ssdp, &m);
        break;
      case BG_BACKEND_RENDERER:
        gavl_dictionary_set_string(&m, "NT", GMERLIN_RENDERER_NT);
        gavl_dictionary_set_string_nocopy(&m, "USN", gavl_sprintf("uuid:%s::%s", uuid, GMERLIN_RENDERER_NT));
        gavl_dictionary_set_string(&m, META_ID, id);
        queue_multicast(ssdp, &m);
        break;
      }

    }
  gavl_dictionary_free(&m);
  }

static int notify(bg_ssdp_t * ssdp)
  {
  int i;
  int ret = 0;
  gavl_dictionary_t * dev;
  int64_t t = 0;
  i = 0;

  while((dev = get_next_dev(&i, 1)))
    {
    if(!gavl_dictionary_get_long(dev, NEXT_NOTIFY_TIME, &t))
      {
      gavl_dictionary_set_long(dev, NEXT_NOTIFY_TIME, gavl_timer_get(ssdp->timer) +
                               rand_long(GAVL_TIME_SCALE / 100, GAVL_TIME_SCALE / 10));
      i++;
      continue;
      }
    else if(t < ssdp->cur_time)
      {
      int count = 0;
      gavl_dictionary_get_int(dev, NOTIFY_COUNT, &count);

      if(count < 3)
        {
        gavl_dictionary_set_long(dev, NEXT_NOTIFY_TIME, gavl_timer_get(ssdp->timer) +
                                 rand_long(GAVL_TIME_SCALE * 1, GAVL_TIME_SCALE * 3));
        count++;
        }
      else
        {
        gavl_dictionary_set_long(dev, NEXT_NOTIFY_TIME, gavl_timer_get(ssdp->timer) +
                                 rand_long(GAVL_TIME_SCALE * 100, GAVL_TIME_SCALE * 900));
        count = 0;
        }
      
      gavl_dictionary_set_int(dev, NOTIFY_COUNT, count);
      
      ret++;
      }
    i++;
    }
  return ret;
  }

static void handle_search_dev(bg_ssdp_t * ssdp, const char * st, int mx, const gavl_dictionary_t * dev,
                              gavl_socket_address_t * sender)
  {
  gavl_dictionary_t m;
  const char * uri;
  char uuid[37];
  int type = 0;
  int is_upnp = 0;
  
  gavl_dictionary_init(&m);

  gavl_http_response_init(&m, "HTTP/1.1", 200, "OK");
  gavl_dictionary_set_string(&m, "CACHE-CONTROL", gavl_sprintf("max-age=%d", MAX_AGE));
  //  gavl_http_header_set_date(&m, "DATE");

  
  if(!(uri = gavl_dictionary_get_string(dev, GAVL_META_URI)))
    return;

  bg_uri_to_uuid(uri, uuid);

  gavl_dictionary_get_int(dev, BG_BACKEND_TYPE, &type);

  /* Upnp */
  if(gavl_string_starts_with(uri, BG_BACKEND_URI_SCHEME_UPNP_RENDERER) ||
     gavl_string_starts_with(uri, BG_BACKEND_URI_SCHEME_UPNP_SERVER))
    is_upnp = 1;

  bg_http_header_set_empty_var(&m, "EXT");
  
  if(is_upnp)
    gavl_dictionary_set_string_nocopy(&m, "LOCATION", gavl_sprintf("http%s", strstr(uri, "://")));
  else
    gavl_dictionary_set_string(&m, "LOCATION", uri);
  
  gavl_dictionary_set_string_nocopy(&m, "SERVER", bg_upnp_make_server_string());
  
  if(!strcasecmp(st, "ssdp:all"))
    {
    if(is_upnp)
      {
      gavl_dictionary_set_string(&m, "ST", "upnp:rootdevice");
      gavl_dictionary_set_string_nocopy(&m, "USN", gavl_sprintf("uuid:%s::upnp:rootdevice", uuid));
      queue_unicast(ssdp, &m, sender, rand_long(GAVL_TIME_SCALE/100, mx * GAVL_TIME_SCALE));

      gavl_dictionary_set_string_nocopy(&m, "ST", gavl_sprintf("uuid:%s", uuid));
      gavl_dictionary_set_string_nocopy(&m, "USN", gavl_sprintf("uuid:%s", uuid));
      queue_unicast(ssdp, &m, sender, rand_long(GAVL_TIME_SCALE/100, mx * GAVL_TIME_SCALE));
      
      switch(type)
        {
        case BG_BACKEND_MEDIASERVER:
          gavl_dictionary_set_string(&m, "ST", "urn:schemas-upnp-org:device:MediaServer:1");
          gavl_dictionary_set_string_nocopy(&m, "USN", gavl_sprintf("uuid:%s::urn:schemas-upnp-org:device:MediaServer:1", uuid));
          gavl_dictionary_set_string(&m, META_ID, gavl_dictionary_get_string(dev, GAVL_META_ID));
          queue_unicast(ssdp, &m, sender, rand_long(GAVL_TIME_SCALE/100, mx * GAVL_TIME_SCALE));
          gavl_dictionary_set_string(&m, META_ID, NULL);
          
          gavl_dictionary_set_string(&m, "ST", "urn:schemas-upnp-org:service:ContentDirectory:1");
          gavl_dictionary_set_string_nocopy(&m, "USN", gavl_sprintf("uuid:%s::urn:schemas-upnp-org:service:ContentDirectory:1", uuid));
          queue_unicast(ssdp, &m, sender, rand_long(GAVL_TIME_SCALE/100, mx * GAVL_TIME_SCALE));
        
          gavl_dictionary_set_string(&m, "ST", "urn:schemas-upnp-org:service:ConnectionManager:1");
          gavl_dictionary_set_string_nocopy(&m, "USN", gavl_sprintf("uuid:%s::urn:schemas-upnp-org:service:ConnectionManager:1", uuid));
          queue_unicast(ssdp, &m, sender, rand_long(GAVL_TIME_SCALE/100, mx * GAVL_TIME_SCALE));
        
          break;
        case BG_BACKEND_RENDERER:
          gavl_dictionary_set_string(&m, "ST", "urn:schemas-upnp-org:device:MediaRenderer:1");
          gavl_dictionary_set_string_nocopy(&m, "USN", gavl_sprintf("uuid:%s::urn:schemas-upnp-org:device:MediaRenderer:1", uuid));
          gavl_dictionary_set_string(&m, META_ID, gavl_dictionary_get_string(dev, GAVL_META_ID));
          queue_unicast(ssdp, &m, sender, rand_long(GAVL_TIME_SCALE/100, mx * GAVL_TIME_SCALE));
          gavl_dictionary_set_string(&m, META_ID, NULL);
          
          gavl_dictionary_set_string(&m, "ST", "urn:schemas-upnp-org:service:ConnectionManager:1");
          gavl_dictionary_set_string_nocopy(&m, "USN", gavl_sprintf("uuid:%s::urn:schemas-upnp-org:service:ConnectionManager:1", uuid));
          queue_unicast(ssdp, &m, sender, rand_long(GAVL_TIME_SCALE/100, mx * GAVL_TIME_SCALE));

          gavl_dictionary_set_string(&m, "ST", "urn:schemas-upnp-org:service:RenderingControl:1");
          gavl_dictionary_set_string_nocopy(&m, "USN", gavl_sprintf("uuid:%s::urn:schemas-upnp-org:service:RenderingControl:1", uuid));
          queue_unicast(ssdp, &m, sender, rand_long(GAVL_TIME_SCALE/100, mx * GAVL_TIME_SCALE));

          gavl_dictionary_set_string(&m, "ST", "urn:schemas-upnp-org:service:AVTransport:1");
          gavl_dictionary_set_string_nocopy(&m, "USN", gavl_sprintf("uuid:%s::urn:schemas-upnp-org:service:AVTransport:1", uuid));
          queue_unicast(ssdp, &m, sender, rand_long(GAVL_TIME_SCALE/100, mx * GAVL_TIME_SCALE));
          
          break;
        }
      
      }
    else
      {
      gavl_dictionary_set_string(&m, "LOCATION", uri);
      switch(type)
        {
        case BG_BACKEND_MEDIASERVER:
          gavl_dictionary_set_string(&m, "ST", GMERLIN_SERVER_NT);
          gavl_dictionary_set_string_nocopy(&m, "USN", gavl_sprintf("uuid:%s::%s", uuid, GMERLIN_SERVER_NT));
          gavl_dictionary_set_string(&m, META_ID, gavl_dictionary_get_string(dev, GAVL_META_ID));
          queue_unicast(ssdp, &m, sender, rand_long(GAVL_TIME_SCALE/100, mx * GAVL_TIME_SCALE));
          break;
        case BG_BACKEND_RENDERER:
          gavl_dictionary_set_string(&m, "ST", GMERLIN_RENDERER_NT);
          gavl_dictionary_set_string_nocopy(&m, "USN", gavl_sprintf("uuid:%s::%s", uuid, GMERLIN_RENDERER_NT));
          gavl_dictionary_set_string(&m, META_ID, gavl_dictionary_get_string(dev, GAVL_META_ID));
          queue_unicast(ssdp, &m, sender, rand_long(GAVL_TIME_SCALE/100, mx * GAVL_TIME_SCALE));
          break;
        }
      }
    }
  else if(!strcasecmp(st, "upnp:rootdevice"))
    {
    if(is_upnp)
      {
      gavl_dictionary_set_string(&m, "ST", st);
      gavl_dictionary_set_string_nocopy(&m, "USN", gavl_sprintf("uuid:%s::upnp:rootdevice", uuid));
      queue_unicast(ssdp, &m, sender, rand_long(GAVL_TIME_SCALE/100, mx * GAVL_TIME_SCALE));
      //      fprintf(stderr, "handling upnp:rootdevice");
      //      gavl_dictionary_dump(&m, 2);
      }
    }
  else if(gavl_string_starts_with_i(st, "uuid:"))
    {
    if(is_upnp && !strcmp(st + 5, uuid))
      {
      gavl_dictionary_set_string(&m, "ST", st);
      gavl_dictionary_set_string_nocopy(&m, "USN", gavl_sprintf("uuid:%s", uuid));
      queue_unicast(ssdp, &m, sender, rand_long(GAVL_TIME_SCALE/100, mx * GAVL_TIME_SCALE));
      }
    }
  else if(gavl_string_starts_with_i(st, "urn:schemas-upnp-org:device:"))
    {
    if(is_upnp)
      {
      char ** str;
      str = gavl_strbreak(st, ':');

      switch(type)
        {
        case BG_BACKEND_MEDIASERVER:
          if(str[3] && !strcmp(str[3], "MediaServer"))
            {
            gavl_dictionary_set_string(&m, "ST", st);
            gavl_dictionary_set_string_nocopy(&m, "USN", gavl_sprintf("uuid:%s::%s", uuid, st));
            queue_unicast(ssdp, &m, sender, rand_long(GAVL_TIME_SCALE/100, mx * GAVL_TIME_SCALE));
            }
          break;
        case BG_BACKEND_RENDERER:
          if(str[3] && !strcmp(str[3], "MediaRenderer"))
            {
            gavl_dictionary_set_string(&m, "ST", st);
            gavl_dictionary_set_string_nocopy(&m, "USN", gavl_sprintf("uuid:%s::%s", uuid, st));
            queue_unicast(ssdp, &m, sender, rand_long(GAVL_TIME_SCALE/100, mx * GAVL_TIME_SCALE));
            }
          break;
        }
      
      gavl_strbreak_free(str);
      }
    }
  else if(gavl_string_starts_with_i(st, "urn:schemas-upnp-org:service:"))
    {
    if(is_upnp)
      {
      char ** str;
      str = gavl_strbreak(st, ':');

      switch(type)
        {
        case BG_BACKEND_MEDIASERVER:
          if(str[3] && (!strcmp(str[3], "ContentDirectory") || !strcmp(str[3], "ConnectionManager")))
            {
            gavl_dictionary_set_string(&m, "ST", st);
            gavl_dictionary_set_string_nocopy(&m, "USN", gavl_sprintf("uuid:%s::%s", uuid, st));
            queue_unicast(ssdp, &m, sender, rand_long(GAVL_TIME_SCALE/100, mx * GAVL_TIME_SCALE));
            }
          break;
        case BG_BACKEND_RENDERER:
          if(str[3] && (!strcmp(str[3], "RenderingControl") || !strcmp(str[3], "ConnectionManager") || !strcmp(str[3], "AVTransport")))
            {
            gavl_dictionary_set_string(&m, "ST", st);
            gavl_dictionary_set_string_nocopy(&m, "USN", gavl_sprintf("uuid:%s::%s", uuid, st));
            queue_unicast(ssdp, &m, sender, rand_long(GAVL_TIME_SCALE/100, mx * GAVL_TIME_SCALE));
            }
          break;
        }
      
      gavl_strbreak_free(str);
      }
    }
  gavl_dictionary_free(&m);
  }

static void handle_search(bg_ssdp_t * s, const char * st, int mx, gavl_socket_address_t * sender)
  {
  int i = 0;
  const gavl_dictionary_t * dev;
  
  //  fprintf(stderr, "handle_search: %s\n", st);
  
  while((dev = get_next_dev(&i, 1)))
    {
    handle_search_dev(s, st, mx, dev, sender);
    i++;
    }
  }

static int handle_multicast(bg_ssdp_t * s)
  {
  int ret = 0;
  int len;
  gavl_dictionary_t h;
  const char * method;
  while(gavl_socket_can_read(s->mcast_fd, 0))
    {
    len = gavl_udp_socket_receive(s->mcast_fd, s->buf, UDP_BUFFER_SIZE, s->addr);
    if(len <= 0)
      continue;
    s->buf[len] = '\0';

    gavl_dictionary_init(&h);
    
    if(!gavl_http_request_from_string(&h, (const char*)s->buf))
      {
      gavl_dictionary_free(&h);
      continue;
      }

    method = gavl_http_request_get_method(&h);
  
    if(!method)
      {
      gavl_dictionary_free(&h);
      continue;
      }
    
    if(!strcasecmp(method, "M-SEARCH"))
      {
      const char * st;
      const char * mx;
      
      if(!(st = gavl_dictionary_get_string_i(&h, "ST"))||
         !(mx = gavl_dictionary_get_string_i(&h, "MX")))
        {
        gavl_dictionary_free(&h);
        continue;
        }

      handle_search(s, st, atoi(mx), s->addr);
      ret++;
      }
    else if(!strcasecmp(method, "NOTIFY"))
      {
      /* We ignore everything except the device type specific ones */
      const char * nt;
      const char * uri;
      const char * nts;
      int alive = 0;
      
      if(!(nt = gavl_dictionary_get_string_i(&h, "NT")) ||
         !(uri = gavl_dictionary_get_string_i(&h, "LOCATION")) ||
         !(nts = gavl_dictionary_get_string_i(&h, "NTS")))
        {
        gavl_dictionary_free(&h);
        continue;
        }

      if(!strcmp(nts, "ssdp:alive"))
        alive = 1;
      
      update_remote_device(s, alive, &h);
      ret++;
      }
    
    gavl_dictionary_free(&h);
    }
  return ret;
  }

static int handle_unicast(bg_ssdp_t * s)
  {
  int ret = 0;
  int len;
  gavl_dictionary_t m;
  const char * uri;
  const char * st;
  
  gavl_dictionary_init(&m);
  
  while(gavl_socket_can_read(s->ucast_fd, 0))
    {
    len = gavl_udp_socket_receive(s->ucast_fd, s->buf, UDP_BUFFER_SIZE, s->addr);
    if(len <= 0)
      continue;
    s->buf[len] = '\0';

    if(!gavl_http_response_from_string(&m, (char*)s->buf))
      continue;

    //    gavl_dprintf("handle_unicast\n");
    //    gavl_dictionary_dump(&m, 2);
    
    if((gavl_http_response_get_status_int(&m) != 200) ||
       !(st = gavl_dictionary_get_string_i(&m, "ST")) ||
       !(uri = gavl_dictionary_get_string_i(&m, "LOCATION")))
      {
      gavl_dictionary_reset(&m);
      continue;
      }
    
    update_remote_device(s, 1, &m);
    ret++;
    gavl_dictionary_reset(&m);
    }
  return ret;
  
  }

static int check_expired(bg_ssdp_t * s)
  {
  int ret = 0;
  int i = 0;
  const gavl_dictionary_t * dict;
  gavl_time_t expire_time;
  const char * uri;

  while((dict = get_next_dev(&i, 0)))
    {
    if(gavl_dictionary_get_long(dict, EXPIRE_TIME, &expire_time) &&
       (expire_time < s->cur_time))
      {
      uri = gavl_dictionary_get_string(dict, GAVL_META_URI);
      gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Removing %s: Expired", uri);
      bg_backend_del_remote(uri);
      ret++;
      }
    else
      i++;
    }
  
  return ret;
  }

static int do_search(bg_ssdp_t * s)
  {
  if(s->next_search_time <= s->cur_time)
    {
    /* Send search packet */
    
    gavl_dictionary_t h;
    gavl_dictionary_init(&h);
    gavl_http_request_init(&h, "M-SEARCH", "*", "HTTP/1.1");
    gavl_dictionary_set_string(&h, "HOST", "239.255.255.250:1900");
    gavl_dictionary_set_string(&h, "MAN", "\"ssdp:discover\"");
    gavl_dictionary_set_string(&h, "MX", "3");
    gavl_dictionary_set_string(&h, "ST", "ssdp:all");
    queue_multicast(s, &h);
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Sent search packet");

    if(s->search_count < 3)
      {
      s->next_search_time = gavl_timer_get(s->timer) +
        rand_long(GAVL_TIME_SCALE * 1, GAVL_TIME_SCALE * 3);
      s->search_count++;
      }
    else
      {
      s->next_search_time = gavl_timer_get(s->timer) +
        rand_long(500 * GAVL_TIME_SCALE, 1000 * GAVL_TIME_SCALE);
      s->search_count = 0;
      }

    gavl_dictionary_free(&h);
    return 1;
    }
  else
    return 0;
  }

int bg_ssdp_update(bg_ssdp_t * s)
  {
  int ret = 0;
  
  s->cur_time = gavl_timer_get(s->timer);
  
  /* Handle multicast messages */
  ret += handle_multicast(s);
  ret += handle_unicast(s);
  ret += flush_multicast(s, 0);
  ret += flush_unicast(s);
  ret += check_expired(s);
  ret += do_search(s);
  
  ret += notify(s);

  //  fprintf(stderr, "bg_ssdp_update %d\n", ret);
  
  return ret;
  }

