#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <config.h>

#include <gavl/log.h>
#define LOG_DOMAIN "res_ssdp"

#include <gmerlin/translation.h>
#include <gmerlin/plugin.h>
#include <gmerlin/backend.h>
#include <gmerlin/resourcemanager.h>
#include <gmerlin/upnp/devicedesc.h>
#include <gmerlin/upnp/upnputils.h>

#include <gmerlin/utils.h>

#include <gavl/gavlsocket.h>
#include <gavl/http.h>

#include <sys/socket.h>
#include <netinet/in.h>

#define UDP_BUFFER_SIZE 2048

#define QUEUE_TIME "t" // Time when this should be sent
#define QUEUE_ADDR "a" // Socket address (as string)
#define QUEUE_MSG  "m" // Message (as string)

#define UPNP_SERVER_NT_PREFIX      "urn:schemas-upnp-org:device:MediaServer:"
#define UPNP_RENDERER_NT_PREFIX    "urn:schemas-upnp-org:device:MediaRenderer:"

#define UPNP_SERVER_NT      UPNP_SERVER_NT_PREFIX"1"
#define UPNP_RENDERER_NT    UPNP_RENDERER_NT_PREFIX"1"

#define GMERLIN_SERVER_NT   "urn:gmerlin-sourceforge-net:device:MediaServer:"
#define GMERLIN_RENDERER_NT "urn:gmerlin-sourceforge-net:device:MediaRenderer:"

#define NEXT_NOTIFY_TIME "NNT"
#define NOTIFY_COUNT     "NC"


// Log level for ssdp messages (typically GAVL_LOG_DEBUG because they 
// are quite noisy)
#define LOG_LEVEL_MSG GAVL_LOG_DEBUG
#define META_ID "GMERLIN-ID"

#define MAX_AGE          1800


typedef struct
  {
  bg_controllable_t ctrl;
  
  int mcast_fd;
  int ucast_fd;

  gavl_socket_address_t * ucast_addr;
  gavl_socket_address_t * mcast_addr;
  
  gavl_array_t ucast_queue;
  gavl_array_t mcast_queue;

  uint8_t buf[UDP_BUFFER_SIZE];

  gavl_time_t cur;

  /* Reduce the frequency of multicast packets */
  gavl_time_t next_mcast_time;
  
  gavl_time_t next_search_time;

  int search_count;
  
  } ssdp_t;

/* Forward declarations */
static int do_search(ssdp_t * s, int force);
static int rand_long(int64_t min, int64_t max);
static void notify_dev(ssdp_t * ssdp, const gavl_dictionary_t * dev, int alive);


static const char * ssdp_protocols[] =
  {
    BG_BACKEND_URI_SCHEME_UPNP_RENDERER,
    BG_BACKEND_URI_SCHEME_UPNP_SERVER,
#if 0
    BG_BACKEND_URI_SCHEME_GMERLIN_RENDERER,
    BG_BACKEND_URI_SCHEME_GMERLIN_MDB,
#endif
    NULL
  };

static int is_ssdp_local(const gavl_dictionary_t * dev)
  {
  int idx;
  const char * uri;

  if(!(uri = gavl_dictionary_get_string(dev, GAVL_META_URI)))
    return 0;

  idx = 0;
  while(ssdp_protocols[idx])
    {
    if(gavl_string_starts_with(uri, ssdp_protocols[idx]))
      return 1;
    idx++;
    }
  
  return 0;
  }
  
static int handle_msg(void * priv, gavl_msg_t * msg)
  {
  ssdp_t * ssdp = priv;

  switch(msg->NS)
    {
    case GAVL_MSG_NS_GENERIC:
      
      switch(msg->ID)
        {
        case GAVL_MSG_RESOURCE_ADDED:
          {
          const char * id;
          gavl_dictionary_t * dict;
          
          if(!(id = gavl_dictionary_get_string(&msg->header, GAVL_MSG_CONTEXT_ID)))
            return 1;
          
          if(!(dict = bg_resource_get_by_id(1, id)))
            return 1;

          if(!is_ssdp_local(dict))
            return 1;

          gavl_dictionary_set_long(dict, NEXT_NOTIFY_TIME, ssdp->cur +
                                   rand_long(GAVL_TIME_SCALE / 100, GAVL_TIME_SCALE / 10));
          
          }
          break;
        case GAVL_MSG_RESOURCE_DELETED:
          fprintf(stderr, "Got resource to unpublish\n");
          break;
        }
      break;
    }
  
  

  return 1;
  }

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

static int flush_mcast(ssdp_t * ssdp, int force)
  {
  int ret = 0;
  const gavl_dictionary_t * msg;
  char * msg_str = NULL;
  int msg_len = 0;
  
  const gavl_dictionary_t * dict;

  //  fprintf(stderr, "Flush multicast 1 %d %"PRId64" %"PRId64"\n", ssdp->multicast_queue.num_entries,
  //          gavl_timer_get(ssdp->timer), ssdp->next_mcast_time);
  
  if(!ssdp->mcast_queue.num_entries  ||
     (!force && (ssdp->cur < ssdp->next_mcast_time)))
    return 0;

  //  fprintf(stderr, "Flush multicast 2\n");
  
  
  if(!(dict = gavl_value_get_dictionary(&ssdp->mcast_queue.entries[0])))
    {
    gavl_array_splice_val(&ssdp->mcast_queue, 0, 1, NULL);
    return 1;
    }

  // fprintf(stderr, "Flush multicast 3\n");
  
  msg = gavl_dictionary_get_dictionary(dict, QUEUE_MSG);
  msg_str = gavl_http_request_to_string(msg, &msg_len);
                              
  gavl_udp_socket_send(ssdp->ucast_fd, (uint8_t*)msg_str, msg_len, ssdp->mcast_addr);

  //  fprintf(stderr, "Flush multicast:\n%s\n", msg_str);
  //  gavl_hexdump((uint8_t*)msg_str, msg_len, 16);
  
  free(msg_str);
  
  
  gavl_array_splice_val(&ssdp->mcast_queue, 0, 1, NULL);
  ret = 1;

  if(!force)
    ssdp->next_mcast_time = ssdp->cur + rand_long(GAVL_TIME_SCALE / 100, GAVL_TIME_SCALE / 10);
  return ret;
  }

static void flush_multicast_force(ssdp_t * ssdp)
  {
  while(ssdp->mcast_queue.num_entries)
    flush_mcast(ssdp, 1);
  
  }

static int flush_ucast(ssdp_t * ssdp)
  {
  int ret = 0;
  int i = 0;
  gavl_time_t t = 0;
  const gavl_dictionary_t * dict;

  while(i < ssdp->ucast_queue.num_entries)
    {
    const char * addr;
    const gavl_dictionary_t * msg;
    
    if(!(dict = gavl_value_get_dictionary(&ssdp->ucast_queue.entries[i])))
      {
      gavl_array_splice_val(&ssdp->ucast_queue, i, 1, NULL);
      continue;
      }
    
    if((!gavl_dictionary_get_long(dict, QUEUE_TIME, &t) ||
        (ssdp->cur >= t)) &&
        (msg = gavl_dictionary_get_dictionary(dict, QUEUE_MSG)) &&
       (addr = gavl_dictionary_get_string(dict, QUEUE_ADDR)) &&
       gavl_socket_address_from_string(ssdp->ucast_addr, addr))
      {
      char * msg_str;
      int msg_len;
      msg_str = gavl_http_response_to_string(msg, &msg_len);

      //      fprintf(stderr, "Flush unicast %s\n%s", addr, msg_str);
      
      if(gavl_udp_socket_send(ssdp->ucast_fd, (uint8_t*)msg_str, msg_len, ssdp->ucast_addr) < 0)
        gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Flush unicast failed: %s", strerror(errno));
      
      gavl_array_splice_val(&ssdp->ucast_queue, i, 1, NULL);
      ret++;
      free(msg_str);
      }
    else
      i++;
    
    }
  return ret;
  }

static void queue_ucast(ssdp_t * ssdp,
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
    gavl_dictionary_set_long(dict, QUEUE_TIME, ssdp->cur + delay);

  gavl_array_splice_val_nocopy(&ssdp->ucast_queue, -1, 0, &val);
  
  } 

static void queue_mcast(ssdp_t * ssdp, const gavl_dictionary_t * msg)
  {
  gavl_dictionary_t * dict;
  gavl_value_t val;
  gavl_value_init(&val);
  dict = gavl_value_set_dictionary(&val);
  gavl_dictionary_set_dictionary(dict, QUEUE_MSG, msg);
  gavl_array_splice_val_nocopy(&ssdp->mcast_queue, -1, 0, &val);
  }


static void * create_ssdp()
  {
  int ttl;
  ssdp_t * ret = calloc(1, sizeof(*ret));

  ret->ucast_addr = gavl_socket_address_create();
  ret->mcast_addr = gavl_socket_address_create();

  /* TODO: For now we just use the first non-local network interface. The better
     way would be to have separate sockets for each interface and also track changes
     of the interfaces... */
  
  gavl_socket_address_set(ret->mcast_addr, "239.255.255.250", 1900, SOCK_DGRAM);
  gavl_socket_address_set_local(ret->ucast_addr, 0, NULL);

  
  ret->mcast_fd = gavl_udp_socket_create_multicast(ret->mcast_addr, ret->ucast_addr);
  ret->ucast_fd = gavl_udp_socket_create(ret->ucast_addr);

  /* Set multicast TTL */
  ttl = 4;
  setsockopt(ret->ucast_fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
  
  /* Set multicast interface for *sending* multicast datagrams */
  gavl_udp_socket_set_multicast_interface(ret->ucast_fd, ret->ucast_addr);
  
  bg_controllable_init(&ret->ctrl,
                       bg_msg_sink_create(handle_msg, ret, 1),
                       bg_msg_hub_create(1));

  ret->cur = gavl_time_get_monotonic();
  ret->next_search_time = ret->cur;
  ret->next_mcast_time = ret->cur + rand_long(GAVL_TIME_SCALE / 10, GAVL_TIME_SCALE / 5);
  
  return ret;
  }

static void destroy_ssdp(void * priv)
  {
  const gavl_dictionary_t * dev;
  ssdp_t * s = priv;

  int idx = 0;

  //  fprintf(stderr, "destroy_ssdp\n");

  while((dev = bg_resource_get_by_idx(1, idx)))
    {
    //    fprintf(stderr, "destroy_ssdp: %s\n", gavl_dictionary_get_string(dev, GAVL_META_URI));
    
    if(gavl_dictionary_get(dev, NEXT_NOTIFY_TIME))
      notify_dev(s, dev, 0);
    idx++;
    }
  
  flush_multicast_force(s);
  
  if(s->mcast_fd > 0)
    gavl_socket_close(s->mcast_fd);
  if(s->ucast_fd > 0)
    gavl_socket_close(s->ucast_fd);

  if(s->mcast_addr)
    gavl_socket_address_destroy(s->mcast_addr);
  if(s->ucast_addr)
    gavl_socket_address_destroy(s->ucast_addr);

  gavl_array_free(&s->ucast_queue);
  gavl_array_free(&s->mcast_queue);

  bg_controllable_cleanup(&s->ctrl);
  
  free(s);
  }

static void update_remote_device(ssdp_t * s, int alive, const gavl_dictionary_t * header)
  {
  gavl_msg_t * msg;
  gavl_dictionary_t * dict;
  
  char * real_uri = NULL;
  int is_upnp = 0;
  int max_age = 0;
  const char * nt;
  const char * uri;
  gavl_value_t new_val;
  char ** str = NULL;
  const char * klass = NULL;
  
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
    klass = GAVL_META_CLASS_BACKEND_MDB;
    gavl_log(LOG_LEVEL_MSG, LOG_DOMAIN, "Got %s for upnp server: %s", (alive ? "alive" : "bye"), real_uri);
    }
  else if(gavl_string_starts_with_i(nt, UPNP_RENDERER_NT_PREFIX))
    {
    real_uri = gavl_sprintf("%s%s", BG_BACKEND_URI_SCHEME_UPNP_RENDERER, strstr(uri, "://"));
    klass = GAVL_META_CLASS_BACKEND_RENDERER;

    is_upnp = 1;
    gavl_log(LOG_LEVEL_MSG, LOG_DOMAIN, "Got %s for upnp renderer: %s", (alive ? "alive" : "bye"), real_uri);
    }
  else if(!strcasecmp(nt, GMERLIN_SERVER_NT))
    {
    real_uri = gavl_strdup(uri);
    klass = GAVL_META_CLASS_BACKEND_MDB;
    gavl_log(LOG_LEVEL_MSG, LOG_DOMAIN, "Got %s for gmerlin server: %s", (alive ? "alive" : "bye"), real_uri);
    }
  else if(!strcasecmp(nt, GMERLIN_RENDERER_NT))
    {
    real_uri = gavl_strdup(uri);
    klass = GAVL_META_CLASS_BACKEND_RENDERER;
    gavl_log(LOG_LEVEL_MSG, LOG_DOMAIN, "Got %s for gmerlin renderer: %s", (alive ? "alive" : "bye"), real_uri);
    }
  else
    goto end;

  if(bg_resource_get_by_id(1, real_uri))
    goto end;

  dict = bg_resource_get_by_id(0, real_uri);
  
  if(!alive)
    {

    if(!dict)
      goto end;
      
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Removing %s: Got ssdp bye", real_uri);

    msg = bg_msg_sink_get(s->ctrl.evt_sink);
  
    gavl_msg_set_id_ns(msg, GAVL_MSG_RESOURCE_DELETED, GAVL_MSG_NS_GENERIC);
    gavl_dictionary_set_string(&msg->header, GAVL_MSG_CONTEXT_ID, real_uri);
    bg_msg_sink_put(s->ctrl.evt_sink);
    
    goto end;
    }
  
  if(!dict)
    {
    //    fprintf(stderr, "New device:\n");
    //    gavl_dictionary_dump(header, 2);

    /* Create new */
    dict = gavl_value_set_dictionary(&new_val);
    gavl_dictionary_set_string(dict, GAVL_META_URI, real_uri);
    
    gavl_dictionary_set_string(dict, GAVL_META_CLASS, klass);
    gavl_dictionary_set_string(dict, GAVL_META_HASH, gavl_dictionary_get_string(header, META_ID));
    
    if(is_upnp)
      {
      gavl_dictionary_set_int(dict, BG_RESOURCE_PRIORITY, BG_RESOURCE_PRIORITY_DEFAULT);
      }
    else
      {
      gavl_dictionary_set_int(dict, BG_RESOURCE_PRIORITY, BG_RESOURCE_PRIORITY_MAX);
      }
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

    gavl_dictionary_set_long(dict, BG_RESOURCE_EXPIRE_TIME,
                             s->cur + max_age * GAVL_TIME_SCALE);
    
    /* Got new Device */
    msg = bg_msg_sink_get(s->ctrl.evt_sink);
  
    gavl_msg_set_id_ns(msg, GAVL_MSG_RESOURCE_ADDED, GAVL_MSG_NS_GENERIC);
    gavl_dictionary_set_string(&msg->header, GAVL_MSG_CONTEXT_ID, real_uri);
    gavl_msg_set_arg_dictionary(msg, 0, dict);
    
    bg_msg_sink_put(s->ctrl.evt_sink);
    
    }
  else
    {
    gavl_dictionary_set_long(dict, BG_RESOURCE_EXPIRE_TIME,
                             s->cur + max_age * GAVL_TIME_SCALE);
    }
  end:

  if(real_uri)
    free(real_uri);
  
  if(str)
    gavl_strbreak_free(str);

  gavl_value_free(&new_val);
  }

static void handle_search_dev(ssdp_t * ssdp, const char * st, int mx, const gavl_dictionary_t * dev,
                              gavl_socket_address_t * sender)
  {
  gavl_dictionary_t m;
  const char * uri;
  const char * klass;
  char uuid[37];
  int is_upnp = 0;
  
  gavl_dictionary_init(&m);

  gavl_http_response_init(&m, "HTTP/1.1", 200, "OK");
  gavl_dictionary_set_string(&m, "CACHE-CONTROL", gavl_sprintf("max-age=%d", MAX_AGE));
  //  gavl_http_header_set_date(&m, "DATE");

  
  if(!(uri = gavl_dictionary_get_string(dev, GAVL_META_URI)))
    return;

  bg_uri_to_uuid(uri, uuid);

  klass = gavl_dictionary_get_string(dev, GAVL_META_CLASS);

  /* Upnp */
  if(gavl_string_starts_with(uri, BG_BACKEND_URI_SCHEME_UPNP_RENDERER) ||
     gavl_string_starts_with(uri, BG_BACKEND_URI_SCHEME_UPNP_SERVER))
    is_upnp = 1;

  gavl_http_header_set_empty_var(&m, "EXT");
  
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
      queue_ucast(ssdp, &m, sender, rand_long(GAVL_TIME_SCALE/100, mx * GAVL_TIME_SCALE));

      gavl_dictionary_set_string_nocopy(&m, "ST", gavl_sprintf("uuid:%s", uuid));
      gavl_dictionary_set_string_nocopy(&m, "USN", gavl_sprintf("uuid:%s", uuid));
      queue_ucast(ssdp, &m, sender, rand_long(GAVL_TIME_SCALE/100, mx * GAVL_TIME_SCALE));

      if(!strcmp(klass, GAVL_META_CLASS_BACKEND_MDB))
        {
        gavl_dictionary_set_string(&m, "ST", "urn:schemas-upnp-org:device:MediaServer:1");
        gavl_dictionary_set_string_nocopy(&m, "USN", gavl_sprintf("uuid:%s::urn:schemas-upnp-org:device:MediaServer:1", uuid));
        gavl_dictionary_set_string(&m, META_ID, gavl_dictionary_get_string(dev, GAVL_META_HASH));
        queue_ucast(ssdp, &m, sender, rand_long(GAVL_TIME_SCALE/100, mx * GAVL_TIME_SCALE));
        gavl_dictionary_set_string(&m, META_ID, NULL);
          
        gavl_dictionary_set_string(&m, "ST", "urn:schemas-upnp-org:service:ContentDirectory:1");
        gavl_dictionary_set_string_nocopy(&m, "USN", gavl_sprintf("uuid:%s::urn:schemas-upnp-org:service:ContentDirectory:1", uuid));
        queue_ucast(ssdp, &m, sender, rand_long(GAVL_TIME_SCALE/100, mx * GAVL_TIME_SCALE));
        
        gavl_dictionary_set_string(&m, "ST", "urn:schemas-upnp-org:service:ConnectionManager:1");
        gavl_dictionary_set_string_nocopy(&m, "USN", gavl_sprintf("uuid:%s::urn:schemas-upnp-org:service:ConnectionManager:1", uuid));
        queue_ucast(ssdp, &m, sender, rand_long(GAVL_TIME_SCALE/100, mx * GAVL_TIME_SCALE));
        
        }
      else if(!strcmp(klass, GAVL_META_CLASS_BACKEND_RENDERER))
        {
        gavl_dictionary_set_string(&m, "ST", "urn:schemas-upnp-org:device:MediaRenderer:1");
        gavl_dictionary_set_string_nocopy(&m, "USN", gavl_sprintf("uuid:%s::urn:schemas-upnp-org:device:MediaRenderer:1", uuid));
        gavl_dictionary_set_string(&m, META_ID, gavl_dictionary_get_string(dev, GAVL_META_HASH));
        queue_ucast(ssdp, &m, sender, rand_long(GAVL_TIME_SCALE/100, mx * GAVL_TIME_SCALE));
        gavl_dictionary_set_string(&m, META_ID, NULL);
          
        gavl_dictionary_set_string(&m, "ST", "urn:schemas-upnp-org:service:ConnectionManager:1");
        gavl_dictionary_set_string_nocopy(&m, "USN", gavl_sprintf("uuid:%s::urn:schemas-upnp-org:service:ConnectionManager:1", uuid));
        queue_ucast(ssdp, &m, sender, rand_long(GAVL_TIME_SCALE/100, mx * GAVL_TIME_SCALE));

        gavl_dictionary_set_string(&m, "ST", "urn:schemas-upnp-org:service:RenderingControl:1");
        gavl_dictionary_set_string_nocopy(&m, "USN", gavl_sprintf("uuid:%s::urn:schemas-upnp-org:service:RenderingControl:1", uuid));
        queue_ucast(ssdp, &m, sender, rand_long(GAVL_TIME_SCALE/100, mx * GAVL_TIME_SCALE));

        gavl_dictionary_set_string(&m, "ST", "urn:schemas-upnp-org:service:AVTransport:1");
        gavl_dictionary_set_string_nocopy(&m, "USN", gavl_sprintf("uuid:%s::urn:schemas-upnp-org:service:AVTransport:1", uuid));
        queue_ucast(ssdp, &m, sender, rand_long(GAVL_TIME_SCALE/100, mx * GAVL_TIME_SCALE));
        
        }
      
      }
    else
      {
      gavl_dictionary_set_string(&m, "LOCATION", uri);

      if(!strcmp(klass, GAVL_META_CLASS_BACKEND_MDB))
        {
        gavl_dictionary_set_string(&m, "ST", GMERLIN_SERVER_NT);
        gavl_dictionary_set_string_nocopy(&m, "USN", gavl_sprintf("uuid:%s::%s", uuid, GMERLIN_SERVER_NT));
        gavl_dictionary_set_string(&m, META_ID, gavl_dictionary_get_string(dev, GAVL_META_HASH));
        queue_ucast(ssdp, &m, sender, rand_long(GAVL_TIME_SCALE/100, mx * GAVL_TIME_SCALE));
        
        }
      else if(!strcmp(klass, GAVL_META_CLASS_BACKEND_RENDERER))
        {
        gavl_dictionary_set_string(&m, "ST", GMERLIN_RENDERER_NT);
        gavl_dictionary_set_string_nocopy(&m, "USN", gavl_sprintf("uuid:%s::%s", uuid, GMERLIN_RENDERER_NT));
        gavl_dictionary_set_string(&m, META_ID, gavl_dictionary_get_string(dev, GAVL_META_HASH));
        queue_ucast(ssdp, &m, sender, rand_long(GAVL_TIME_SCALE/100, mx * GAVL_TIME_SCALE));
        
        }
      }
    }
  else if(!strcasecmp(st, "upnp:rootdevice"))
    {
    if(is_upnp)
      {
      gavl_dictionary_set_string(&m, "ST", st);
      gavl_dictionary_set_string_nocopy(&m, "USN", gavl_sprintf("uuid:%s::upnp:rootdevice", uuid));
      queue_ucast(ssdp, &m, sender, rand_long(GAVL_TIME_SCALE/100, mx * GAVL_TIME_SCALE));
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
      queue_ucast(ssdp, &m, sender, rand_long(GAVL_TIME_SCALE/100, mx * GAVL_TIME_SCALE));
      }
    }
  else if(gavl_string_starts_with_i(st, "urn:schemas-upnp-org:device:"))
    {
    if(is_upnp)
      {
      char ** str;
      str = gavl_strbreak(st, ':');

      if(!strcmp(klass, GAVL_META_CLASS_BACKEND_MDB))
        {
        gavl_dictionary_set_string(&m, "ST", st);
        gavl_dictionary_set_string_nocopy(&m, "USN", gavl_sprintf("uuid:%s::%s", uuid, st));
        queue_ucast(ssdp, &m, sender, rand_long(GAVL_TIME_SCALE/100, mx * GAVL_TIME_SCALE));
        
        }
      else if(!strcmp(klass, GAVL_META_CLASS_BACKEND_RENDERER))
        {
        gavl_dictionary_set_string(&m, "ST", st);
        gavl_dictionary_set_string_nocopy(&m, "USN", gavl_sprintf("uuid:%s::%s", uuid, st));
        queue_ucast(ssdp, &m, sender, rand_long(GAVL_TIME_SCALE/100, mx * GAVL_TIME_SCALE));
        
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

      if(!strcmp(klass, GAVL_META_CLASS_BACKEND_MDB))
        {
        gavl_dictionary_set_string(&m, "ST", st);
        gavl_dictionary_set_string_nocopy(&m, "USN", gavl_sprintf("uuid:%s::%s", uuid, st));
        queue_ucast(ssdp, &m, sender, rand_long(GAVL_TIME_SCALE/100, mx * GAVL_TIME_SCALE));
        }
      else if(!strcmp(klass, GAVL_META_CLASS_BACKEND_RENDERER))
        {
        gavl_dictionary_set_string(&m, "ST", st);
        gavl_dictionary_set_string_nocopy(&m, "USN", gavl_sprintf("uuid:%s::%s", uuid, st));
        queue_ucast(ssdp, &m, sender, rand_long(GAVL_TIME_SCALE/100, mx * GAVL_TIME_SCALE));
        }
      
      gavl_strbreak_free(str);
      }
    }
  gavl_dictionary_free(&m);
  }

static void handle_search(ssdp_t * s, const char * st, int mx, gavl_socket_address_t * sender)
  {
  int i = 0;
  const gavl_dictionary_t * dev;
  
  //  fprintf(stderr, "handle_search: %s\n", st);

  while((dev = bg_resource_get_by_idx(1, i)))
    {
    if(is_ssdp_local(dev))
      handle_search_dev(s, st, mx, dev, sender);
    i++;
    }
  }


/* Handle messages from multicast socket */
static int handle_mcast(ssdp_t * s)
  {
  int ret = 0;
  int len;
  gavl_dictionary_t h;
  const char * method;
  while(gavl_fd_can_read(s->mcast_fd, 0))
    {
    len = gavl_udp_socket_receive(s->mcast_fd, s->buf, UDP_BUFFER_SIZE, s->ucast_addr);
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

      handle_search(s, st, atoi(mx), s->ucast_addr);
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

/* Handle messages from unicast socket. These are only search replies. */

static int handle_ucast(ssdp_t * s)
  {

  int ret = 0;
  int len;
  gavl_dictionary_t m;
  const char * uri;
  const char * st;
  
  gavl_dictionary_init(&m);
  
  while(gavl_fd_can_read(s->ucast_fd, 0))
    {
    len = gavl_udp_socket_receive(s->ucast_fd, s->buf, UDP_BUFFER_SIZE, s->ucast_addr);
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

static int do_search(ssdp_t * s, int force)
  {
  if((s->next_search_time <= s->cur) || force)
    {
    /* Send search packet */
    
    gavl_dictionary_t h;
    gavl_dictionary_init(&h);
    gavl_http_request_init(&h, "M-SEARCH", "*", "HTTP/1.1");
    gavl_dictionary_set_string(&h, "HOST", "239.255.255.250:1900");
    gavl_dictionary_set_string(&h, "MAN", "\"ssdp:discover\"");
    gavl_dictionary_set_string(&h, "MX", "3");
    gavl_dictionary_set_string(&h, "ST", "ssdp:all");
    queue_mcast(s, &h);
    gavl_log(LOG_LEVEL_MSG, LOG_DOMAIN, "Sent search packet");

    if(s->search_count < 3)
      {
      s->next_search_time = s->cur +
        rand_long(GAVL_TIME_SCALE * 1, GAVL_TIME_SCALE * 3);
      s->search_count++;
      }
    else
      {
      s->next_search_time = s->cur +
        rand_long(500 * GAVL_TIME_SCALE, 1000 * GAVL_TIME_SCALE);
      s->search_count = 0;
      }

    gavl_dictionary_free(&h);
    return 1;
    }
  else
    return 0;
  }

/* Notify */

static void notify_dev(ssdp_t * ssdp, const gavl_dictionary_t * dev, int alive)
  {
  
  gavl_dictionary_t m;
  const char * uri;
  const char * klass;
  char uuid[37];
  const char * id = gavl_dictionary_get_string(dev, GAVL_META_HASH);
  
  gavl_dictionary_init(&m);
  uri = gavl_dictionary_get_string(dev, GAVL_META_URI);

  gavl_log(LOG_LEVEL_MSG, LOG_DOMAIN, "Sending notification for %s (%d)", uri, alive);

  if(!alive)
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Sending bye for %s", uri);
  
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

  bg_uri_to_uuid(uri, uuid);
  
  klass = gavl_dictionary_get_string(dev, GAVL_META_CLASS);
  
  if(gavl_string_starts_with(uri, BG_BACKEND_URI_SCHEME_UPNP_RENDERER) ||
     gavl_string_starts_with(uri, BG_BACKEND_URI_SCHEME_UPNP_SERVER))
    {
    gavl_dictionary_set_string_nocopy(&m, "LOCATION", gavl_sprintf("http%s", strstr(uri, "://")));

    gavl_dictionary_set_string(&m, "NT", "upnp:rootdevice");
    gavl_dictionary_set_string_nocopy(&m, "USN", gavl_sprintf("uuid:%s::upnp:rootdevice", uuid));
    queue_mcast(ssdp, &m);

    gavl_dictionary_set_string_nocopy(&m, "NT", gavl_sprintf("uuid:%s", uuid));
    gavl_dictionary_set_string_nocopy(&m, "USN", gavl_sprintf("uuid:%s", uuid));
    queue_mcast(ssdp, &m);
    

    if(!strcmp(klass, GAVL_META_CLASS_BACKEND_MDB))
      {
      gavl_dictionary_set_string(&m, "NT", "urn:schemas-upnp-org:device:MediaServer:1");
      gavl_dictionary_set_string_nocopy(&m, "USN", gavl_sprintf("uuid:%s::urn:schemas-upnp-org:device:MediaServer:1", uuid));
      gavl_dictionary_set_string(&m, META_ID, id);
      queue_mcast(ssdp, &m);
      gavl_dictionary_set_string(&m, META_ID, NULL);
        
      gavl_dictionary_set_string(&m, "NT", "urn:schemas-upnp-org:service:ContentDirectory:1");
      gavl_dictionary_set_string_nocopy(&m, "USN", gavl_sprintf("uuid:%s::urn:schemas-upnp-org:service:ContentDirectory:1", uuid));
      queue_mcast(ssdp, &m);
        
      gavl_dictionary_set_string(&m, "NT", "urn:schemas-upnp-org:service:ConnectionManager:1");
      gavl_dictionary_set_string_nocopy(&m, "USN", gavl_sprintf("uuid:%s::urn:schemas-upnp-org:service:ConnectionManager:1", uuid));
      queue_mcast(ssdp, &m);
          
      }
    else if(!strcmp(klass, GAVL_META_CLASS_BACKEND_RENDERER))
      {
      gavl_dictionary_set_string(&m, "NT", "urn:schemas-upnp-org:device:MediaRenderer:1");
      gavl_dictionary_set_string_nocopy(&m, "USN", gavl_sprintf("uuid:%s::urn:schemas-upnp-org:device:MediaRenderer:1", uuid));
      gavl_dictionary_set_string(&m, META_ID, id);
      queue_mcast(ssdp, &m);
      gavl_dictionary_set_string(&m, META_ID, NULL);
        
      gavl_dictionary_set_string(&m, "NT", "urn:schemas-upnp-org:service:ConnectionManager:1");
      gavl_dictionary_set_string_nocopy(&m, "USN", gavl_sprintf("uuid:%s::urn:schemas-upnp-org:service:ConnectionManager:1", uuid));
      queue_mcast(ssdp, &m);

      gavl_dictionary_set_string(&m, "NT", "urn:schemas-upnp-org:service:RenderingControl:1");
      gavl_dictionary_set_string_nocopy(&m, "USN", gavl_sprintf("uuid:%s::urn:schemas-upnp-org:service:RenderingControl:1", uuid));
      queue_mcast(ssdp, &m);

      gavl_dictionary_set_string(&m, "NT", "urn:schemas-upnp-org:service:AVTransport:1");
      gavl_dictionary_set_string_nocopy(&m, "USN", gavl_sprintf("uuid:%s::urn:schemas-upnp-org:service:AVTransport:1", uuid));
      queue_mcast(ssdp, &m);
      }
        
    }
  else
    {
    gavl_dictionary_set_string(&m, "LOCATION", uri);

    if(!strcmp(klass, GAVL_META_CLASS_BACKEND_MDB))
      {
      gavl_dictionary_set_string(&m, "NT", GMERLIN_SERVER_NT);
      gavl_dictionary_set_string_nocopy(&m, "USN", gavl_sprintf("uuid:%s::%s", uuid, GMERLIN_SERVER_NT));
      gavl_dictionary_set_string(&m, META_ID, id);
      queue_mcast(ssdp, &m);

      }
    else if(!strcmp(klass, GAVL_META_CLASS_BACKEND_RENDERER))
      {
      gavl_dictionary_set_string(&m, "NT", GMERLIN_RENDERER_NT);
      gavl_dictionary_set_string_nocopy(&m, "USN", gavl_sprintf("uuid:%s::%s", uuid, GMERLIN_RENDERER_NT));
      gavl_dictionary_set_string(&m, META_ID, id);
      queue_mcast(ssdp, &m);
      }
    }
  gavl_dictionary_free(&m);
  }

static int notify(ssdp_t * ssdp)
  {
  int i;
  int ret = 0;
  gavl_dictionary_t * dev;
  int64_t t = 0;
  i = 0;

  while((dev = bg_resource_get_by_idx(1, i)))
    {
    if(!gavl_dictionary_get_long(dev, NEXT_NOTIFY_TIME, &t))
      {
      i++;
      continue;
      }

    if(t < ssdp->cur)
      {
      int count = 0;
      gavl_dictionary_get_int(dev, NOTIFY_COUNT, &count);

      if(count < 3)
        {
        gavl_dictionary_set_long(dev, NEXT_NOTIFY_TIME, ssdp->cur +
                                 rand_long(GAVL_TIME_SCALE * 1, GAVL_TIME_SCALE * 3));
        count++;
        }
      else
        {
        gavl_dictionary_set_long(dev, NEXT_NOTIFY_TIME, ssdp->cur +
                                 rand_long(GAVL_TIME_SCALE * 100, GAVL_TIME_SCALE * 900));
        count = 0;
        }
      notify_dev(ssdp, dev, 1);
      
      gavl_dictionary_set_int(dev, NOTIFY_COUNT, count);
      ret++;
      }
    i++;
    }
  return ret;
  }


static int update_ssdp(void * priv)
  {
  int ret = 0;
  ssdp_t * s = priv;

  s->cur = gavl_time_get_monotonic();

  ret += handle_mcast(s);
  ret += handle_ucast(s);
  
  ret += flush_mcast(s, 0);
  ret += flush_ucast(s);
  ret += do_search(s, 0);
  ret += notify(s);
  
  return ret;
  }



static bg_controllable_t * get_controllable_ssdp(void * priv)
  {
  ssdp_t * p = priv;
  return &p->ctrl;
  }

bg_controllable_plugin_t the_plugin =
  {
    .common =
    {
      BG_LOCALE,
      .name =      "res_ssdp",
      .long_name = TRS("SSDP"),
      .description = TRS("Publishes and detects Upnp devices via SSDP"),
      .type =     BG_PLUGIN_RESOURCE_DETECTOR,
      .flags =    0,
      .create =   create_ssdp,
      .destroy =   destroy_ssdp,
      .get_controllable =   get_controllable_ssdp,
      .priority =         1,
    },
    .update = update_ssdp,

  };

/* Include this into all plugin modules exactly once
   to let the plugin loader obtain the API version */
BG_GET_PLUGIN_API_VERSION;
