/*****************************************************************
 * gmerlin - a general purpose multimedia framework and applications
 *
 * Copyright (c) 2001 - 2024 Members of the Gmerlin project
 * http://github.com/bplaum
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

#include <string.h>


#include <config.h>

#include <gavl/log.h>
#define LOG_DOMAIN "res_sap"

#include <gmerlin/translation.h>
#include <gmerlin/plugin.h>
#include <gmerlin/resourcemanager.h>

#include <gavl/gavlsocket.h>
#include <gavl/utils.h>
#include <gavl/sap.h>

#define UDP_BUFFER_SIZE 8192

#define MAX_AGE (15*60*GAVL_TIME_SCALE)

typedef struct
  {
  bg_controllable_t ctrl;
  
  int mcast_fd;

  gavl_buffer_t buf;
  
  }  sap_t;

static int handle_msg(void * priv, gavl_msg_t * msg)
  {
  return 1;
  }


static void * create_sap()
  {
  sap_t * ret = calloc(1, sizeof(*ret));

  gavl_socket_address_t * addr = gavl_socket_address_create();

  gavl_socket_address_set(addr, "239.255.255.255", 9875, SOCK_DGRAM);

  gavl_buffer_alloc(&ret->buf, UDP_BUFFER_SIZE);
  
  ret->mcast_fd =
    gavl_udp_socket_create_multicast(addr, NULL);

#if 0
  gavl_socket_address_set(addr, "224.2.127.254", 9875, SOCK_DGRAM);
  /* Join second group */
  gavl_udp_socket_join_multicast(ret->mcast_fd, addr, NULL);
#endif
  
  bg_controllable_init(&ret->ctrl,
                       bg_msg_sink_create(handle_msg, ret, 1),
                       bg_msg_hub_create(1));
  
  return ret;
  }

static bg_controllable_t * get_controllable_sap(void * priv)
  {
  sap_t * s = priv;
  return &s->ctrl;
  }

#if 0
static int parse_sap_header(gavl_buffer_t * buf, int *del,
                            char ** sdp, char ** id)
  {
  int flags;
  int auth_len;
  int addr_len;
  const char * str;
  const char * end;
  const char * pos;
  const uint8_t * data = buf->buf;
  
  flags = data[0];

  if(((flags >> 5) & 0x07) != 1) // Version
    {
    gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Version mismatch");
    return 0;
    }
  
  if(flags & (1<<4)) // Originating address IPV4 or IPV6?
    addr_len = 16;
  else
    addr_len = 4;

  if(flags & (1<<2))
    *del = 1;
  else
    *del = 0;

  if(flags & (1<<1))
    {
    gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Got encrypted SAP packet");
    return 0;
    }

  if(flags & (1<<0))
    {
    gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Got compressed SAP packet");
    return 0;
    }
  
  auth_len = data[1] * 4;
  
  str = (const char*)(data + (4 + addr_len + auth_len));
  end = (const char*)(data + buf->len);

  //  fprintf(stderr, "Got str: %s\n", str);

  if((pos = memchr(str, '\0', end - str)) &&
     (pos != end) &&
     !strcasecmp(str, "application/sdp"))
    {
    pos++;
    *sdp = gavl_strdup(pos);

    *id = malloc(GAVL_MD5_LENGTH);
    gavl_md5_buffer_str(data + 2, 2 + addr_len, *id);
    return 1;
    }
  return 0;
  }
#endif

static int update_sap(void * priv)
  {
  int ret = 0;
  sap_t * s = priv;
  int del;

  const char * id;
  const char * sdp;
  gavl_msg_t * msg;
  gavl_value_t val_new;
  gavl_dictionary_t * dict;
  const char * pos;
  const char * end;
  gavl_dictionary_t sap_packet;
  
  gavl_value_init(&val_new);
  gavl_dictionary_init(&sap_packet);
  
  while(gavl_fd_can_read(s->mcast_fd, 0))
    {
    s->buf.len = gavl_udp_socket_receive(s->mcast_fd, s->buf.buf, UDP_BUFFER_SIZE-1, NULL);
    if(s->buf.len <= 0)
      continue;
    s->buf.buf[s->buf.len] = '\0';
    ret++;
    
    //    fprintf(stderr, "Got SAP:\n");
    //    gavl_hexdump(s->buf, len, 16);

    /* Parse SAP packet */
    if(gavl_sap_decode(&s->buf, &del, &sap_packet) &&
       (id = gavl_dictionary_get_string(&sap_packet, GAVL_META_ID)) &&
       (sdp = gavl_dictionary_get_string(&sap_packet, GAVL_SAP_SDP)))
      {
      dict = bg_resource_get_by_id(0, id);
      
      //      fprintf(stderr, "Got id: %s, sdp:\n%s\n", id, sdp);

      if(del)
        {
        msg = bg_msg_sink_get(s->ctrl.evt_sink);
        gavl_msg_set_id_ns(msg, GAVL_MSG_RESOURCE_DELETED, GAVL_MSG_NS_GENERIC);
        gavl_dictionary_set_string(&msg->header, GAVL_MSG_CONTEXT_ID, id);
        bg_msg_sink_put(s->ctrl.evt_sink);
        }
      else
        {
        if(!dict)
          {
          dict = gavl_value_set_dictionary(&val_new);
          
          if(strstr(sdp, "m=video "))
            gavl_dictionary_set_string(dict, GAVL_META_CLASS, GAVL_META_CLASS_VIDEO_BROADCAST);
          else if(strstr(sdp, "m=audio "))
            gavl_dictionary_set_string(dict, GAVL_META_CLASS, GAVL_META_CLASS_AUDIO_BROADCAST);
          else
            gavl_dictionary_set_string(dict, GAVL_META_CLASS, GAVL_META_CLASS_BROADCAST);
          
          if((pos = strstr(sdp, "s=")))
            {
            pos += 2;
            if((end = strchr(pos, '\r')) || (end = strchr(pos, '\n')))
              gavl_dictionary_set_string(dict, GAVL_META_LABEL, gavl_strndup(pos, end));
            }
          
          gavl_dictionary_set_long(dict, BG_RESOURCE_EXPIRE_TIME,
                                   gavl_time_get_monotonic() + MAX_AGE);
          
          gavl_dictionary_set_string_nocopy(dict, GAVL_META_URI, gavl_sdp_to_uri(sdp));

          //          fprintf(stderr, "Got source:\n");
          //          gavl_dictionary_dump(dict, 2);
          
          msg = bg_msg_sink_get(s->ctrl.evt_sink);
          gavl_msg_set_id_ns(msg, GAVL_MSG_RESOURCE_ADDED, GAVL_MSG_NS_GENERIC);
          gavl_dictionary_set_string(&msg->header, GAVL_MSG_CONTEXT_ID, id);
          gavl_msg_set_arg_nocopy(msg, 0, &val_new);
          bg_msg_sink_put(s->ctrl.evt_sink);
          }
        else
          {
          gavl_dictionary_set_long(dict, BG_RESOURCE_EXPIRE_TIME,
                                   gavl_time_get_monotonic() + MAX_AGE);
          }
        }
      }
    gavl_dictionary_reset(&sap_packet);
    }
  
  return ret;
  }

static void destroy_sap(void * priv)
  {
  sap_t * s = priv;
  if(s->mcast_fd > 0)
    gavl_socket_close(s->mcast_fd);

  bg_controllable_cleanup(&s->ctrl);
  free(s);
  }

bg_controllable_plugin_t the_plugin =
  {
    .common =
    {
      BG_LOCALE,
      .name =      "res_sap",
      .long_name = TRS("SAP"),
      .description = TRS("Publishes and detects RTP streams via SAP"),
      .type =     BG_PLUGIN_RESOURCE_DETECTOR,
      .flags =    0,
      .create =   create_sap,
      .destroy =   destroy_sap,
      .get_controllable =   get_controllable_sap,
      .priority =         1,
    },
    .update = update_sap,

  };

/* Include this into all plugin modules exactly once
   to let the plugin loader obtain the API version */
BG_GET_PLUGIN_API_VERSION;
