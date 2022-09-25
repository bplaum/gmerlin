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

#include <string.h>

#include <config.h>

#include <gavl/metatags.h>

#include <gmerlin/application.h>
#include <gmerlin/backend.h>
#include <gmerlin/player.h>
#include <gmerlin/playermsg.h>
#include <gmerlin/translation.h>
#include <gmerlin/log.h>
#include <gmerlin/websocket.h>

#include <backend_priv.h>


#include <gmerlin/upnp/ssdp.h>
#include <gmerlin/upnp/upnputils.h>
#include <gmerlin/upnp/eventlistener.h>
#include <gmerlin/upnp/soap.h>
#include <gmerlin/upnp/devicedesc.h>
#include <gmerlin/xmlutils.h>
#include <gmerlin/utils.h>

#include <gmerlin/upnp/didl.h>

#define LOG_DOMAIN "backend_upnp"


/* ssdp */

static const struct
  {
  const char * upnp_type;
  int upnp_version;
  bg_backend_type_t type;
  }
device_types[] = 
  {
    {
      .upnp_type    = "MediaRenderer",
      .upnp_version = 1,
      .type         = BG_BACKEND_RENDERER,
    },
#if 0 // We don't detect remote media servers yet
    {
      .upnp_type    = "MediaServer",
      .upnp_version = 1,
      .type         = BG_BACKEND_MEDIASERVER,
    },
#endif
    { /* End */ },
  };

// static void set_time(bg_backend_handle_t * dev, gavl_time_t current_time);

typedef struct
  {
  bg_msg_sink_t * sink;
  bg_ssdp_t * ssdp;
  bg_msg_sink_t * ssdp_sink;
  } ssdp_t;

static int handle_ssdp_msg(void * priv, gavl_msg_t * evt)
  {
  const char * upnp_type;
  int upnp_version = 0;
  const char * desc_url;
  const char * protocol;
  gavl_msg_t * msg;
  ssdp_t * s = priv;
  char * url_base = NULL;

  
  switch(evt->NS)
    {
    case BG_MSG_NS_SSDP:
      switch(evt->ID)
        {
        case BG_SSDP_MSG_ADD_DEVICE: // 1
          {
          gavl_dictionary_t dev;
          bg_backend_type_t type = BG_BACKEND_NONE;
          
          bg_ssdp_msg_get_add(evt, &protocol, &upnp_type, &upnp_version, &desc_url);

          gavl_dictionary_init(&dev);
          
          if(!strcmp(protocol, "gmerlin"))
            {
            gavl_dictionary_set_string(&dev, GAVL_META_URI, desc_url);

            if(!(type = bg_backend_type_from_string(upnp_type)))
              break;
            
            gavl_dictionary_set_int(&dev, BG_BACKEND_TYPE, type);
#if 1
            if(!bg_backend_get_node_info(&dev))
              {
              gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Could not obtain node info for %s", desc_url);
              break;
              }
#endif
            gavl_dictionary_set_string(&dev, BG_BACKEND_PROTOCOL, "gmerlin");

            }
          else if(!strcmp(protocol, "upnp"))
            {
            if(!bg_backend_is_local(desc_url, &dev))
              {
              xmlDocPtr doc = NULL;
              int idx;
              xmlNodePtr dev_node;
              gavl_value_t val;
              gavl_array_t * icons;
              char * real_url;
              const char * pos;
              
              /* Determine type */
              idx = 0;
              while(device_types[idx].upnp_type)
                {
                if(!strcmp(device_types[idx].upnp_type, upnp_type) &&
                   (device_types[idx].upnp_version <= upnp_version))
                  {
                  type = device_types[idx].type;
                  gavl_dictionary_set_int(&dev, BG_BACKEND_TYPE, type);
                  break;
                  }
                idx++;
                }

              if((pos = strstr(desc_url, "://")))
                real_url = bg_sprintf("http%s", pos);
              else
                real_url = gavl_strdup(desc_url);
            
              /* Load device description */
              if(!device_types[idx].upnp_type ||
                 !(doc = bg_xml_from_url(real_url, NULL)))
                {
                gavl_dictionary_free(&dev);


                if(real_url)
                  free(real_url);
              
                break;
                }
              if(real_url)
                free(real_url);
            
              url_base = bg_upnp_device_description_get_url_base(desc_url, doc);
  
              /* Check device type */
              if(!(dev_node = bg_upnp_device_description_get_device_node(doc, upnp_type, upnp_version)))
                {
                gavl_dictionary_free(&dev);
                xmlFreeDoc(doc);
                break;
                }

              /* Ignore gmerlin devices */
              if(bg_upnp_device_description_is_gmerlin(dev_node))
                {
                gavl_dictionary_free(&dev);
                xmlFreeDoc(doc);
                break;
                }
            
              gavl_dictionary_set_string(&dev, GAVL_META_LABEL, bg_upnp_device_description_get_label(dev_node));

              /* */

              gavl_value_init(&val);
              icons = gavl_value_set_array(&val);
              bg_upnp_device_description_get_icon_urls(dev_node, icons, url_base);
              gavl_dictionary_set_nocopy(&dev, GAVL_META_ICON_URL, &val);

              gavl_dictionary_set_string(&dev, GAVL_META_URI, desc_url);
            
              gavl_dictionary_set_string(&dev, BG_BACKEND_PROTOCOL, "upnp");
              if(doc)
                xmlFreeDoc(doc);
              }
            }

          bg_backend_info_init(&dev);

          
          msg = bg_msg_sink_get(s->sink);
          gavl_msg_set_id_ns(msg, BG_MSG_ADD_BACKEND, BG_MSG_NS_BACKEND);
          gavl_msg_set_arg_dictionary(msg, 0, &dev);
          bg_msg_sink_put(s->sink, msg);
          
          gavl_dictionary_free(&dev);
          }
          break;
        case BG_SSDP_MSG_DEL_DEVICE: // 2
          {
          bg_ssdp_msg_get_del(evt, &desc_url);

          msg = bg_msg_sink_get(s->sink);
          gavl_msg_set_id_ns(msg, BG_MSG_DEL_BACKEND, BG_MSG_NS_BACKEND);
          
          gavl_msg_set_arg_string(msg, 0, desc_url);
          
          bg_msg_sink_put(s->sink, msg);
          }
          break;
        }
      break;
    }

  if(url_base)
    free(url_base);
  return 1;
  }

static void * detector_create_upnp()
  {
  ssdp_t * ret;
  ret = calloc(1, sizeof(*ret));
  ret->sink = bg_backend_reg->evt_sink;
  ret->ssdp = bg_ssdp_create(NULL);

  ret->ssdp_sink = bg_msg_sink_create(handle_ssdp_msg, ret, 1);

  if(!ret->ssdp)
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Creation of SSDP instance failed");

  bg_msg_hub_connect_sink(bg_ssdp_get_event_hub(ret->ssdp), ret->ssdp_sink);
  
  return ret;
  }

static void detector_destroy_upnp(void * priv)
  {
  ssdp_t * s = priv;
  bg_ssdp_destroy(s->ssdp);
  if(s->ssdp_sink)
    bg_msg_sink_destroy(s->ssdp_sink);
  free(s);
  }

static int detector_update_upnp(void * priv)
  {
  ssdp_t * s = priv;
  return bg_ssdp_update(s->ssdp);
  }

bg_remote_dev_detector_t bg_remote_dev_detector_upnp = 
  {
    .create  = detector_create_upnp,
    .destroy = detector_destroy_upnp,
    .update  = detector_update_upnp,
  };

/* Service description utils */

typedef struct
  {
  int min;
  int max;
  int step;
  } range_t;

static int range_float_to_int(const range_t * r, double val)
  {
  int ret;
  ret = r->min + (int)(val * (r->max - r->min) + 0.5);

  if(r->step > 1)
    {
    ret /= r->step;
    ret *= r->step;
    }
  return ret;
  }

static double range_int_to_float(const range_t * r, int val)
  {
  return (double)(val - r->min) / (r->max - r->min);
  }

static const char * get_xml_name(xmlNodePtr node)
  {
  xmlNodePtr child = NULL;

  if((child = bg_xml_find_next_node_child_by_name(node, child, "name")))
    return bg_xml_node_get_text_content(child);
  return NULL;
  }

static xmlNodePtr service_get_action(xmlDocPtr desc, const char * name)
  {
  const char * node_name;
  xmlNodePtr child;
  xmlNodePtr node;
  
  if((node = bg_xml_find_doc_child(desc, "scpd")) &&
     (node = bg_xml_find_next_node_child_by_name(node, NULL, "actionList")))
    {
    child = NULL;
    while((child = bg_xml_find_next_node_child_by_name(node, child, "action")))
      {
      if((node_name = get_xml_name(child)) && !strcmp(node_name, name))
        return child;
      }
    }
  return NULL;
  }

static xmlNodePtr service_get_state_var(xmlDocPtr desc, const char * name)
  {
  const char * node_name;
  xmlNodePtr child;
  xmlNodePtr node;
  
  if((node = bg_xml_find_doc_child(desc, "scpd")) &&
     (node = bg_xml_find_next_node_child_by_name(node, NULL, "serviceStateTable")))
    {
    child = NULL;
    while((child = bg_xml_find_next_node_child_by_name(node, child, "stateVariable")))
      {
      if((node_name = get_xml_name(child)) && !strcmp(node_name, name))
        return child;
      }
    }
  return NULL;
  }

static int get_int_range(xmlNodePtr node, range_t * ret)
  {
  xmlNodePtr child;
  if(!(node = bg_xml_find_next_node_child_by_name(node, NULL, "allowedValueRange")))
    return 0;

  if(!(child = bg_xml_find_next_node_child_by_name(node, NULL, "minimum")))
    return 0;
  ret->min = atoi(bg_xml_node_get_text_content(child));
  
  if(!(child = bg_xml_find_next_node_child_by_name(node, NULL, "maximum")))
    return 0;
  ret->max = atoi(bg_xml_node_get_text_content(child));
  
  if(!(child = bg_xml_find_next_node_child_by_name(node, NULL, "step")))
    return 0;
  ret->step = atoi(bg_xml_node_get_text_content(child));

  return 1;
  }

static int var_allows_value(xmlNodePtr node, const char * val)
  {
  const char * child_val;
  xmlNodePtr child = NULL;
  if(!(node = bg_xml_find_next_node_child_by_name(node, NULL, "allowedValueList")))
    return 0;

  while((child = bg_xml_find_next_node_child_by_name(node, child, "allowedValue")) &&
        (child_val = bg_xml_node_get_text_content(child)))
    {
    if(!strcmp(child_val, val))
      return 1;
    }
  return 0;
  }

#if 0
static void dump_int_range(const range_t * r)
  {
  gavl_dprintf("%d-%d step: %d", r->min, r->max, r->step);
  }
#endif

/* Upnp renderer */

#define RENDERER_HAS_SET_NEXT_AVTRANSPORTURI    (1<<0)
#define RENDERER_HAS_VOLUME                     (1<<1)
#define RENDERER_HAS_MUTE                       (1<<2)

#define RENDERER_HAS_NEXT                       (1<<3) // Net URI was set already
#define RENDERER_OUR_PLAYBACK                   (1<<4) 
#define RENDERER_HAS_CURRENT_TRANSPORT_ACTIONS  (1<<5) 
#define RENDERER_HAS_PAUSE                      (1<<6) 
#define RENDERER_HAS_SEEK                       (1<<7) 

#define RENDERER_CAN_PAUSE                      (1<<8) 
#define RENDERER_CAN_SEEK                       (1<<9) 
#define RENDERER_STOP_SENT                      (1<<10)
#define RENDERER_FINISHING                      (1<<11)
#define RENDERER_NEED_METADATA                  (1<<12)

#define SET_INSTANCE_ID(a) gavl_dictionary_set_string(a, "InstanceID", "0")

static void do_play(bg_backend_handle_t * be);

static const char * track_to_uri(bg_backend_handle_t * be, const gavl_dictionary_t * track);
static char * track_to_metadata(const gavl_dictionary_t * track_p, const char * current_uri);
    
typedef struct
  {
  gavl_dictionary_t upnp_state;
  gavl_dictionary_t gmerlin_state;

  bg_upnp_event_listener_t * rc_evt;
  bg_upnp_event_listener_t * avt_evt;

  bg_msg_sink_t * evt_sink;

  char * avt_control_url;
  char * rc_control_url;

  char * avt_event_url;
  char * rc_event_url;

  char * cm_control_url;

  char * AVTransportURI;
  char * NextAVTransportURI;

  /* Reported from device */
  char * NextAVTransportURI_dev;
  char * NextAVTransportURIMetaData_dev;
  
  
  // Updated by renderer_poll().
  // We use this to detect track changes and to decide if we "own" the playback.
  char * TrackURI; 

  // We use this to detect track changes if we don't own the playback
  char * TrackMetaData; 
  
  int control_fd;

  range_t Volume_range;
  
  int flags;
  //  gavl_array_t mimetypes;

  bg_player_tracklist_t tl;

  int player_status;

  gavl_timer_t * timer;
  gavl_time_t last_poll_time;
  
  gavl_time_t duration;
  gavl_time_t current_time;
  
  } renderer_t;

static void set_track_from_didl(bg_backend_handle_t * be, const char * str)
  {
  /* didl -> metadata */
  xmlDocPtr doc = NULL;
  xmlNodePtr child;

  gavl_value_t v;
  gavl_dictionary_t * dict;
  gavl_dictionary_t * m;
  gavl_time_t didl_duration = GAVL_TIME_UNDEFINED;
  renderer_t * r = be->priv;

  if(r->TrackMetaData && !strcmp(r->TrackMetaData, str))
    return;

  r->flags &= ~RENDERER_NEED_METADATA;
  
  r->TrackMetaData = gavl_strrep(r->TrackMetaData, str);

  gavl_value_init(&v);

  dict = gavl_value_set_dictionary(&v);
  m = gavl_dictionary_get_dictionary_create(dict, GAVL_META_METADATA);
                
  if((doc = xmlParseMemory(str, strlen(str))) &&
     (child = bg_xml_find_doc_child(doc, "DIDL-Lite")) &&
     (child = bg_xml_find_node_child(child, "item")))
    bg_metadata_from_didl(m, child);
  
  if(doc)
    xmlFreeDoc(doc);
                
  gavl_dictionary_get_long(m, GAVL_META_APPROX_DURATION, &didl_duration);
                
  if((r->duration == GAVL_TIME_UNDEFINED) &&
     (didl_duration != GAVL_TIME_UNDEFINED))
    r->duration = didl_duration;
  else if((didl_duration == GAVL_TIME_UNDEFINED) &&
          (r->duration != GAVL_TIME_UNDEFINED))
    gavl_dictionary_set_long(m, GAVL_META_APPROX_DURATION, r->duration);
  
  if(r->flags & RENDERER_CAN_PAUSE)
    gavl_dictionary_set_int(m, GAVL_META_CAN_PAUSE, 1);
  if(r->flags & RENDERER_CAN_SEEK)
    gavl_dictionary_set_int(m, GAVL_META_CAN_SEEK, 1);
  
  bg_state_set(&r->gmerlin_state, 1, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_CURRENT_TRACK,
               &v, be->ctrl_int.evt_sink, BG_MSG_STATE_CHANGED);
  gavl_value_reset(&v);
  
  }

static void detect_can_seek_pause(bg_backend_handle_t * be)
  {
  renderer_t * r = be->priv;

  r->flags &= ~(RENDERER_CAN_SEEK|RENDERER_CAN_PAUSE);
  
  if(!(r->flags & RENDERER_HAS_CURRENT_TRANSPORT_ACTIONS))
    {
    if(r->flags & RENDERER_HAS_SEEK)
      r->flags |= RENDERER_CAN_SEEK;

    if(r->flags & RENDERER_HAS_PAUSE)
      r->flags |= RENDERER_CAN_PAUSE;
    }
  else
    {
    gavl_dictionary_t s;
    gavl_dictionary_t * args_in;
    const gavl_dictionary_t * args_out;
    const char * actions;
    char ** actions_arr;
    int idx;
    
    bg_soap_request_init(&s, r->avt_control_url, "AVTransport", 1, "GetCurrentTransportActions");
    args_in = gavl_dictionary_get_dictionary_nc(&s, BG_SOAP_META_ARGS_IN);
    SET_INSTANCE_ID(args_in);
    
    if(bg_soap_request(&s, &r->control_fd) &&
       (args_out = gavl_dictionary_get_dictionary(&s, BG_SOAP_META_ARGS_OUT)) &&
       (actions = gavl_dictionary_get_string(args_out, "Actions")) &&
       (actions_arr = gavl_strbreak(actions, ',')))
      {
      idx = 0;
      while(actions_arr[idx])
        {
        if(!strcmp(actions_arr[idx], "Seek"))
          r->flags |= RENDERER_CAN_SEEK;
        else if(!strcmp(actions_arr[idx], "Pause"))
          r->flags |= RENDERER_CAN_PAUSE;
        idx++;
        }
      }
    
    gavl_dictionary_free(&s);
    }
  
  }
  
static void renderer_poll(bg_backend_handle_t * be)
  {
  gavl_dictionary_t s;
  gavl_dictionary_t * args_in;
  const gavl_dictionary_t * args_out;
  const char * var;
  gavl_value_t val;
  gavl_dictionary_t * dict;
  gavl_dictionary_t * m;

  renderer_t * r = be->priv;
  
  gavl_value_init(&val);
  
  bg_soap_request_init(&s, r->avt_control_url, "AVTransport", 1, "GetPositionInfo");
  args_in = gavl_dictionary_get_dictionary_nc(&s, BG_SOAP_META_ARGS_IN);
  SET_INSTANCE_ID(args_in);
  
  if(!bg_soap_request(&s, &r->control_fd))
    {
    gavl_dictionary_free(&s);
    return;
    }
  args_out = gavl_dictionary_get_dictionary(&s, BG_SOAP_META_ARGS_OUT);
  
  var = gavl_dictionary_get_string(args_out, "TrackURI");

  if(!r->TrackURI || strcmp(r->TrackURI, var))
    {
    detect_can_seek_pause(be);
    
    /* Track changed */

    r->TrackURI = gavl_strrep(r->TrackURI, var);
    
    if(r->AVTransportURI && !strcmp(var, r->AVTransportURI))
      r->flags |= RENDERER_OUR_PLAYBACK;
    /* Track switched */
    else if(r->NextAVTransportURI && !strcmp(var, r->NextAVTransportURI))
      {
      r->flags |= RENDERER_OUR_PLAYBACK;
      gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Detected gapless advance %s", r->NextAVTransportURI);
      
      if(r->AVTransportURI)
        free(r->AVTransportURI);

      r->AVTransportURI = r->NextAVTransportURI;
      r->NextAVTransportURI = NULL;
      
      bg_player_tracklist_advance(&r->tl, 0);
      }
    else
      r->flags &= ~RENDERER_OUR_PLAYBACK;

    
    if(r->flags & RENDERER_OUR_PLAYBACK)
      {
      gavl_dictionary_t * track = bg_player_tracklist_get_current_track(&r->tl);

      /* Current track */
      dict = gavl_value_set_dictionary(&val);
      
      gavl_dictionary_copy(dict, track);

      /* Set GAVL_META_CAN_PAUSE and GAVL_META_CAN_SEEK */
      m = gavl_track_get_metadata_nc(dict);
      
      r->duration = GAVL_TIME_UNDEFINED;
      gavl_dictionary_get_long(m, GAVL_META_APPROX_DURATION, &r->duration);

      if(r->flags & RENDERER_CAN_PAUSE)
        gavl_dictionary_set_int(m, GAVL_META_CAN_PAUSE, 1);
      if(r->flags & RENDERER_CAN_SEEK)
        gavl_dictionary_set_int(m, GAVL_META_CAN_SEEK, 1);
    
      bg_state_set(&r->gmerlin_state, 1, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_CURRENT_TRACK,
                   &val, be->ctrl_int.evt_sink, BG_MSG_STATE_CHANGED);
      gavl_value_reset(&val);
      }
    else if(r->TrackURI && r->NextAVTransportURI_dev && r->NextAVTransportURIMetaData_dev &&
            !strcmp(r->TrackURI, r->NextAVTransportURI_dev))
      {
      set_track_from_didl(be, r->NextAVTransportURIMetaData_dev);
      free(r->NextAVTransportURIMetaData_dev);
      r->NextAVTransportURIMetaData_dev = NULL;
      free(r->NextAVTransportURI_dev);
      r->NextAVTransportURI_dev = NULL;
      }
    else
      r->flags |= RENDERER_NEED_METADATA;
    
    /* Set duration range */
    if(r->duration != GAVL_TIME_UNDEFINED)
      bg_state_set_range_long(&r->gmerlin_state,
                              BG_PLAYER_STATE_CTX "/" BG_PLAYER_STATE_CURRENT_TIME, BG_PLAYER_TIME,
                              0, r->duration);
    else
      bg_state_set_range_long(&r->gmerlin_state,
                              BG_PLAYER_STATE_CTX "/" BG_PLAYER_STATE_CURRENT_TIME, BG_PLAYER_TIME,
                              0, 0);
    

    }

  /* Metadata changed */

  if((r->flags & RENDERER_NEED_METADATA) &&
     (var = gavl_dictionary_get_string(args_out, "TrackMetaData")))
    set_track_from_didl(be, var);
  
  var = gavl_dictionary_get_string(args_out, "RelTime");

  if(!gavl_time_parse(var, &r->current_time))
    r->current_time = GAVL_TIME_UNDEFINED;
  
  if((r->current_time != GAVL_TIME_UNDEFINED) &&
     (r->duration != GAVL_TIME_UNDEFINED))
    {
    /* SetNextAVTransportURI */
    if((r->flags & RENDERER_OUR_PLAYBACK) &&
       (r->flags & RENDERER_HAS_SET_NEXT_AVTRANSPORTURI) &&
       !r->NextAVTransportURI &&
       (r->duration - r->current_time < 10 * GAVL_TIME_SCALE))
      {
      const char * uri = NULL;
      char * uri_metadata = NULL;
      
      gavl_dictionary_t * next = bg_player_tracklist_get_next(&r->tl);

      if((uri = track_to_uri(be, next)) &&
         (uri_metadata = track_to_metadata(next, uri)))
        {
        /* SetNextAVTransportURI */
        bg_soap_request_init(&s, r->avt_control_url, "AVTransport", 1, "SetNextAVTransportURI");
  
        args_in = gavl_dictionary_get_dictionary_nc(&s, BG_SOAP_META_ARGS_IN);
        SET_INSTANCE_ID(args_in);
        
        r->NextAVTransportURI = bg_string_to_uri(uri, -1);
      
        gavl_dictionary_set_string(args_in, "NextURI", r->NextAVTransportURI);
        gavl_dictionary_set_string(args_in, "NextURIMetaData", uri_metadata);

        bg_soap_request(&s, &r->control_fd);

        gavl_dictionary_reset(&s);
        }
      }
    
    if(r->duration - r->current_time < 3 * GAVL_TIME_SCALE)
      r->flags |= RENDERER_FINISHING;
    }

     
  
  dict = gavl_value_set_dictionary(&val);
  
  if(r->flags & RENDERER_OUR_PLAYBACK)
    {
    gavl_time_t t_abs;
    gavl_time_t t_rem;
    gavl_time_t t_rem_abs;
    double percentage;
    
    bg_player_tracklist_get_times(&r->tl, r->current_time, &t_abs, &t_rem, &t_rem_abs, &percentage);
    
    gavl_dictionary_set_long(dict, BG_PLAYER_TIME, r->current_time);
    gavl_dictionary_set_long(dict, BG_PLAYER_TIME_ABS, t_abs);
    gavl_dictionary_set_long(dict, BG_PLAYER_TIME_REM, t_rem);
    gavl_dictionary_set_long(dict, BG_PLAYER_TIME_REM_ABS, t_rem_abs);
    gavl_dictionary_set_float(dict, BG_PLAYER_TIME_PERC, percentage);
    }
  else
    {
    gavl_time_t duration;
    
    gavl_dictionary_set_long(dict, BG_PLAYER_TIME, r->current_time);
    gavl_dictionary_set_long(dict, BG_PLAYER_TIME_ABS, r->current_time);

    var = gavl_dictionary_get_string(args_out, "TrackDuration");
    gavl_time_parse(var, &duration);
    
    gavl_dictionary_set_long(dict, BG_PLAYER_TIME_REM, duration - r->current_time);
    gavl_dictionary_set_long(dict, BG_PLAYER_TIME_REM_ABS, duration - r->current_time);
    gavl_dictionary_set_float(dict, BG_PLAYER_TIME_PERC, (double)r->current_time / (double)duration);
    }
  
  bg_state_set(&r->gmerlin_state, 1, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_CURRENT_TIME,
               &val, be->ctrl_int.evt_sink, BG_MSG_STATE_CHANGED);
  
  gavl_value_reset(&val);
  
  gavl_dictionary_reset(&s);

#if 0
  if(r->flags & RENDERER_NEED_METADATA)
    {
    
    bg_soap_request_init(&s, r->avt_control_url, "AVTransport", 1, "GetMediaInfo");
    args_in = gavl_dictionary_get_dictionary_nc(&s, BG_SOAP_META_ARGS_IN);
    SET_INSTANCE_ID(args_in);
  
    if(!bg_soap_request(&s, &r->control_fd))
      {
      gavl_dictionary_free(&s);
      return;
      }
    args_out = gavl_dictionary_get_dictionary(&s, BG_SOAP_META_ARGS_OUT);

    var = gavl_dictionary_get_string(args_out, "CurrentURIMetaData");
    
    if(var)
      set_track_from_didl(be, var);
    }
#endif
  
  }

static int handle_upnp_event(void * priv, gavl_msg_t * msg)
  {
  bg_backend_handle_t * be = priv;

  renderer_t * r = be->priv;

  switch(msg->NS)
    {
    case BG_MSG_NS_UPNP:
      switch(msg->ID)
        {
        case BG_MSG_ID_UPNP_EVENT:
          {
          const char * name;
          const char * var;
          const char * val;
          
          name = gavl_msg_get_arg_string_c(msg, 0);
          var = gavl_msg_get_arg_string_c(msg,  1);
          val = gavl_msg_get_arg_string_c(msg,  2);
          
          if(!strcmp(name, "rc"))
            {
            gavl_value_t v;
            gavl_value_init(&v);
            gavl_value_set_string(&v, val);
            bg_state_set(&r->upnp_state, 1, "rc", var, &v, NULL, 0);
            gavl_value_free(&v);
            
            if(!strcmp(var, "PresetNameList"))
              {
              
              }
            else if(!strcmp(var, "Volume"))
              {
              gavl_value_t volume;
              gavl_value_init(&volume);
              gavl_value_set_float(&volume, range_int_to_float(&r->Volume_range, atoi(val)) );
              bg_state_set(&r->gmerlin_state, 1, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_VOLUME,
                           &volume, be->ctrl_int.evt_sink, BG_MSG_STATE_CHANGED);
              }
            else if(!strcmp(var, "VolumeDB"))
              {
              
              }
            else if(!strcmp(var, "Mute"))
              {
              int val_i;
              gavl_value_t mute;
              gavl_value_init(&mute);
              val_i = atoi(val);
              gavl_value_set_int(&mute, val_i);
              bg_state_set(&r->gmerlin_state, 1, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_MUTE,
                           &mute, be->ctrl_int.evt_sink, BG_MSG_STATE_CHANGED);
              }
            else
              gavl_msg_dump(msg, 2);
            }
          else if(!strcmp(name, "avt"))
            {
            gavl_value_t v;
            gavl_value_init(&v);
            gavl_value_set_string(&v, val);
            bg_state_set(&r->upnp_state, 1, "avt", var, &v, NULL, 0);
            gavl_value_reset(&v);
            
            if(!strcmp(var, "TransportState"))
              {
              r->player_status = BG_PLAYER_STATUS_STOPPED;
              
              if(!strcmp(val, "STOPPED"))
                {
                gavl_dictionary_t * dict = NULL;
                if(r->flags & RENDERER_OUR_PLAYBACK)
                  {
                  gavl_time_t t_abs;
                  gavl_time_t t_rem;
                  gavl_time_t t_rem_abs;
                  double percentage;
                  
                  if(!(r->flags & RENDERER_STOP_SENT) &&
                     (r->flags & RENDERER_FINISHING))
                    {
                    /* Switch to next track */
                    if(bg_player_tracklist_advance(&r->tl, 0))
                      {
                      r->flags &= ~RENDERER_FINISHING;
                      do_play(be);
                      return 1;
                      }
                    }

                  dict = gavl_value_set_dictionary(&v);
                  
                  bg_player_tracklist_get_times(&r->tl, 0, &t_abs, &t_rem, &t_rem_abs, &percentage);
                  gavl_dictionary_set_long(dict, BG_PLAYER_TIME, 0);
                  gavl_dictionary_set_long(dict, BG_PLAYER_TIME_ABS, t_abs);
                  gavl_dictionary_set_long(dict, BG_PLAYER_TIME_REM, t_rem);
                  gavl_dictionary_set_long(dict, BG_PLAYER_TIME_REM_ABS, t_rem_abs);
                  gavl_dictionary_set_float(dict, BG_PLAYER_TIME_PERC, percentage);
                  
                  }
                else
                  {
                  dict = gavl_value_set_dictionary(&v);
                  
                  gavl_dictionary_set_long(dict, BG_PLAYER_TIME, 0);
                  gavl_dictionary_set_long(dict, BG_PLAYER_TIME_ABS, 0);
                  gavl_dictionary_set_long(dict, BG_PLAYER_TIME_REM, 0);
                  gavl_dictionary_set_long(dict, BG_PLAYER_TIME_REM_ABS, 0);
                  gavl_dictionary_set_float(dict, BG_PLAYER_TIME_PERC, 0.0);
                  }
                
                bg_state_set(&r->gmerlin_state, 1, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_CURRENT_TIME,
                             &v, be->ctrl_int.evt_sink, BG_MSG_STATE_CHANGED);
                gavl_value_reset(&v);
                
                r->player_status = BG_PLAYER_STATUS_STOPPED;

                if(r->TrackURI)
                  {
                  free(r->TrackURI);
                  r->TrackURI = NULL;
                  }
                if(r->TrackMetaData)
                  {
                  free(r->TrackMetaData);
                  r->TrackMetaData = NULL;
                  }

                /* Reset current track */
                dict = gavl_value_set_dictionary(&v);
                gavl_dictionary_get_dictionary_create(dict, GAVL_META_METADATA);
                bg_state_set(&r->gmerlin_state, 1, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_CURRENT_TRACK,
                             &v, be->ctrl_int.evt_sink, BG_MSG_STATE_CHANGED);
                
                r->flags &= ~(RENDERER_STOP_SENT|RENDERER_FINISHING);
                }
              else if(!strcmp(val, "PLAYING"))
                {
                r->player_status = BG_PLAYER_STATUS_PLAYING;
                /* trigger initial poll */
                r->last_poll_time = GAVL_TIME_UNDEFINED;
                }
              else if(!strcmp(val, "TRANSITIONING"))
                r->player_status = BG_PLAYER_STATUS_CHANGING;
              else if(!strcmp(val, "PAUSED_PLAYBACK"))
                r->player_status = BG_PLAYER_STATUS_PAUSED;
              else if(!strcmp(val, "NO_MEDIA_PRESENT"))
                r->player_status = BG_PLAYER_STATUS_INIT;

              gavl_value_set_int(&v, r->player_status);

              bg_state_set(&r->gmerlin_state, 1, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_STATUS,
                           &v, be->ctrl_int.evt_sink, BG_MSG_STATE_CHANGED);
              
              gavl_value_reset(&v);
              }
            else if(!strcmp(var, "TransportStatus"))
              {

              }
            else if(!strcmp(var, "PlaybackStorageMedium"))
              {

              }
            else if(!strcmp(var, "PossiblePlaybackStorageMedia"))
              {

              }
            else if(!strcmp(var, "CurrentPlayMode"))
              {

              }
            else if(!strcmp(var, "TransportPlaySpeed"))
              {

              }
            else if(!strcmp(var, "NumberOfTracks"))
              {

              }
            else if(!strcmp(var, "CurrentTrack"))
              {

              }
            else if(!strcmp(var, "CurrentTrackDuration"))
              {

              }
            else if(!strcmp(var, "CurrentMediaDuration"))
              {

              }
            else if(!strcmp(var, "CurrentTrackURI"))
              {
              /* TODO: Check r->NextAVTransportURI */
              
              }
            else if(!strcmp(var, "CurrentTrackMetaData"))
              {
#if 0
              if(!(r->flags & RENDERER_OUR_PLAYBACK))
                set_track_from_didl(be, val);
#endif         
              
              }
            else if(!strcmp(var, "AVTransportURI"))
              {
              //              fprintf(stderr, "AVTransportURI changed: %s\n", val);
              }
            else if(!strcmp(var, "AVTransportURIMetaData"))
              {
              //              fprintf(stderr, "AVTransportURIMetaData changed: %s\n", val);
              }
            else if(!strcmp(var, "NextAVTransportURI"))
              {
              //              fprintf(stderr, "Got NextAVTransportURI %s\n", val);

              r->NextAVTransportURI_dev =
                gavl_strrep(r->NextAVTransportURI_dev, val);
              }
            else if(!strcmp(var, "NextAVTransportURIMetaData"))
              {
              //              fprintf(stderr, "Got NextAVTransportURIMetaData %s\n", val);
              
              r->NextAVTransportURIMetaData_dev =
                gavl_strrep(r->NextAVTransportURIMetaData_dev, val);
              }
            else if(!strcmp(var, "RelativeTimePosition"))
              {

              }
            else if(!strcmp(var, "RelativeCountPosition"))
              {

              }
            else if(!strcmp(var, "AbsoluteCounterPosition"))
              {

              }
            else if(!strcmp(var, "CurrentTransportActions"))
              {
              //  fprintf(stderr, "Got CurrentTransportActions: %s\n", val);
              }
            else
              gavl_msg_dump(msg, 2);
            }
          else
            gavl_msg_dump(msg, 2);
          }
          break;
        }
    }
  
  return 1;
  }

static void set_protocol_info(renderer_t * r, const char * info, gavl_array_t * ret)
  {
  int i;
  
  char ** pinfos;
  char ** pinfo;

  pinfos = gavl_strbreak(info, ',');

  if(!pinfos)
    return;
  
  i = 0;

  while(pinfos[i])
    {
    pinfo = gavl_strbreak(pinfos[i], ':');

    if(!pinfo)
      break;
      
    if(!pinfo[0] || !pinfo[1] || !pinfo[1])
      {
      gavl_strbreak_free(pinfo);
      break;
      }
    
    if(!strcmp(pinfo[0], "http-get"))
      gavl_string_array_add(ret, pinfo[2]);
    
    gavl_strbreak_free(pinfo);
    
    i++;
    }
  
  gavl_strbreak_free(pinfos);

  //  gavl_array_dump(&r->mimetypes, 2);
  
  }


static int create_renderer(bg_backend_handle_t * dev, const char * uri_1, const char * root_url)
  {
  const char * pos;
  char * uri;

  const char * var;
  const char * label;
  
  xmlDocPtr dev_desc = NULL;
  xmlNodePtr service_node = NULL;
  xmlNodePtr dev_node = NULL;
  xmlNodePtr node;
  char * url_base      = NULL;
  renderer_t * r = calloc(1, sizeof(*r));
  int ret = 0;
  gavl_dictionary_t s;
  const gavl_dictionary_t * dict;
  char * service_desc_uri = NULL;
  xmlDocPtr service_desc = NULL;
  //  char * tmp_string;

  gavl_array_t protocols;
  gavl_array_t mimetypes;
  gavl_array_t icons;
  
  if((pos = strstr(uri_1, "://")))
    uri = bg_sprintf("http%s", pos);
  else
    uri = gavl_strdup(uri_1);
  
  if(!(dev_desc = bg_xml_from_url(uri, NULL)))
    {
    free(r);
    goto fail;
    }

  r->control_fd = -1;

  r->timer = gavl_timer_create();
  gavl_timer_start(r->timer);
  
  /* URL Base */
  url_base = bg_upnp_device_description_get_url_base(uri, dev_desc);
  
  /* Get control- and event URLs */

  if(!(dev_node = bg_upnp_device_description_get_device_node(dev_desc, "MediaRenderer", 1)))
    goto fail;

  /* AVTransport */
  if(!(service_node = bg_upnp_device_description_get_service_node(dev_node, "AVTransport", 1)))
    goto fail;

  if(!(node = bg_xml_find_node_child(service_node, "controlURL")) ||
     !(var = bg_xml_node_get_text_content(node)))
    goto fail;
  r->avt_control_url = bg_upnp_device_description_make_url(var, url_base);

  if(!(node = bg_xml_find_node_child(service_node, "eventSubURL")) ||
     !(var = bg_xml_node_get_text_content(node)))
    goto fail;
  r->avt_event_url = bg_upnp_device_description_make_url(var, url_base);

  /* Load service description */
  if(!(node = bg_xml_find_node_child(service_node, "SCPDURL")) ||
     !(var = bg_xml_node_get_text_content(node)))
    goto fail;

  if(!(service_desc_uri = bg_upnp_device_description_make_url(var, url_base)) ||
     !(service_desc = bg_xml_from_url(service_desc_uri, NULL)))
    goto fail;

  if(service_get_action(service_desc, "SetNextAVTransportURI"))
    {
    //    fprintf(stderr, "SetNextAVTransportURI\n");
    r->flags |= RENDERER_HAS_SET_NEXT_AVTRANSPORTURI;
    }
  //  else
  //    fprintf(stderr, "NO SetNextAVTransportURI\n");

  if(service_get_action(service_desc, "GetCurrentTransportActions"))
    r->flags |= RENDERER_HAS_CURRENT_TRANSPORT_ACTIONS;

  if(service_get_action(service_desc, "Pause"))
    r->flags |= RENDERER_HAS_PAUSE;

  if((node = service_get_state_var(service_desc, "A_ARG_TYPE_SeekMode")) &&
     (var_allows_value(node, "REL_TIME")))
    r->flags |= RENDERER_HAS_SEEK;
  
  xmlFreeDoc(service_desc);
  service_desc = NULL;
  
  free(service_desc_uri);
  service_desc_uri = NULL;
  
  /* RenderingControl */
  if(!(service_node = bg_upnp_device_description_get_service_node(dev_node, "RenderingControl", 1)))
    goto fail;

  if(!(node = bg_xml_find_node_child(service_node, "controlURL")) ||
     !(var = bg_xml_node_get_text_content(node)))
    goto fail;
  r->rc_control_url = bg_upnp_device_description_make_url(var, url_base);

  if(!(node = bg_xml_find_node_child(service_node, "eventSubURL")) ||
     !(var = bg_xml_node_get_text_content(node)))
    goto fail;
  r->rc_event_url = bg_upnp_device_description_make_url(var, url_base);
  
  /* Load service description */
  if(!(node = bg_xml_find_node_child(service_node, "SCPDURL")) ||
     !(var = bg_xml_node_get_text_content(node)) ||
     !(service_desc_uri = bg_upnp_device_description_make_url(var, url_base)) ||
     !(service_desc = bg_xml_from_url(service_desc_uri, NULL)))
    goto fail;

  /* Get Volume range */
  if((node = service_get_state_var(service_desc, "Volume")) &&
     get_int_range(node, &r->Volume_range) &&
     service_get_action(service_desc, "SetVolume"))
    {
    r->flags |= RENDERER_HAS_VOLUME;
    }
  /* Get Volume range */
  if((node = service_get_state_var(service_desc, "Mute")) &&
     service_get_action(service_desc, "SetMute"))
    {
    r->flags |= RENDERER_HAS_MUTE;
    }
 
  xmlFreeDoc(service_desc);
  service_desc = NULL;
  
  free(service_desc_uri);
  service_desc_uri = NULL;
  
  /* ConnectionManager */
  if(!(service_node = bg_upnp_device_description_get_service_node(dev_node, "ConnectionManager", 1)))
    goto fail;

  if(!(node = bg_xml_find_node_child(service_node, "controlURL")) ||
     !(var = bg_xml_node_get_text_content(node)))
    goto fail;
  r->cm_control_url = bg_upnp_device_description_make_url(var, url_base);

#if 0 // No events from ConnectionManager
  if(!(node = bg_xml_find_node_child(service_node, "eventSubURL")) ||
     !(uri = bg_xml_node_get_text_content(node)))
    goto fail;
  r->cm_event_url = bg_upnp_device_description_make_url(uri, url_base);
#endif
  /* */
  
  r->evt_sink = bg_msg_sink_create(handle_upnp_event, dev, 1);

#if 1
  r->rc_evt = bg_upnp_event_listener_create(r->rc_event_url,
                                            root_url,
                                            "rc",
                                            r->evt_sink);
  
  r->avt_evt = bg_upnp_event_listener_create(r->avt_event_url,
                                             root_url,
                                             "avt",
                                             r->evt_sink);
#endif  
  
  dev->priv = r;
  
  /* Get protocol info */
  bg_soap_request_init(&s, r->cm_control_url, "ConnectionManager", 1, "GetProtocolInfo");

  if(!bg_soap_request(&s, &r->control_fd) ||
     !(dict = gavl_dictionary_get_dictionary(&s, BG_SOAP_META_ARGS_OUT)))
    goto fail;

  gavl_array_init(&protocols);
  gavl_array_init(&mimetypes);
  
  set_protocol_info(r, gavl_dictionary_get_string(dict, "Sink"), &mimetypes);

  gavl_string_array_add(&protocols, "http");

  if(!(node = bg_xml_find_node_child(dev_node, "friendlyName")) ||
     !(label = bg_xml_node_get_text_content(node)))
    goto fail;

  bg_player_state_init(&r->gmerlin_state, label, &protocols, &mimetypes);

  gavl_array_init(&icons);
  
  bg_upnp_device_description_get_icon_urls(dev_node, &icons, url_base);

  /* Tracklist */
  bg_player_tracklist_init(&r->tl, dev->ctrl_int.evt_sink);
  

  //  set_protocol_info
  
  //  fprintf(stderr, "Initialized state:\n");
  //  gavl_dictionary_dump(&r->gmerlin_state, 2);
  //  fprintf(stderr, "\n");

  bg_state_apply(&r->gmerlin_state, dev->ctrl_int.evt_sink, BG_MSG_STATE_CHANGED);

  bg_set_network_node_info(label, &icons, NULL, dev->ctrl_int.evt_sink);
  gavl_array_free(&icons);
  
  ret = 1;

  fail:

  if(service_desc)
    xmlFreeDoc(service_desc);

  if(service_desc_uri)
    free(service_desc_uri);
  
  if(url_base)
    free(url_base);

  if(uri)
    free(uri);
  
  return ret;
  
  }

static void destroy_renderer(bg_backend_handle_t * dev)
  {
  renderer_t * r = dev->priv;
  gavl_dictionary_free(&r->upnp_state);
  gavl_dictionary_free(&r->gmerlin_state);

  if(r->rc_evt)
    bg_upnp_event_listener_destroy(r->rc_evt);

  if(r->avt_evt)
    bg_upnp_event_listener_destroy(r->avt_evt);
  
  if(r->avt_control_url) free(r->avt_control_url);
  if(r->rc_control_url)  free(r->rc_control_url);
  if(r->avt_event_url)   free(r->avt_event_url);
  if(r->rc_event_url)    free(r->rc_event_url);
  
  if(r->timer)
    gavl_timer_destroy(r->timer);

  if(r->AVTransportURI)     free(r->AVTransportURI);
  if(r->NextAVTransportURI) free(r->NextAVTransportURI);
  if(r->TrackURI)           free(r->TrackURI);

  if(r->NextAVTransportURI_dev) free(r->NextAVTransportURI_dev);
    
  if(r->NextAVTransportURIMetaData_dev) free(r->NextAVTransportURIMetaData_dev);
  
  free(r);
  }

static const char * track_to_uri(bg_backend_handle_t * be, const gavl_dictionary_t * track)
  {
  renderer_t * r;
#if 0
  const char * uri;
  const char * mimetype;
  int idx = 0;
  const gavl_dictionary_t * m = gavl_track_get_metadata(track);

  const gavl_array_t * mimetypes;
  const gavl_value_t * mimetypes_val;
  
  r = be->priv;

  if(!(mimetypes_val = bg_state_get(&r->gmerlin_state, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_MIMETYPES)) ||
     !(mimetypes = gavl_value_get_array(mimetypes_val)))
    return NULL;
  
  while(gavl_dictionary_get_src(m, GAVL_META_SRC, idx, &mimetype, &uri))
    {
    if((gavl_string_array_indexof(mimetypes, mimetype) >= 0) &&
       gavl_string_starts_with(uri, "http://"))
      return uri;
    idx++;
    }
  
  return NULL;
#else
  r = be->priv;

  return bg_player_track_get_uri(&r->gmerlin_state, track);
#endif
  
  }

static char * track_to_metadata(const gavl_dictionary_t * track_p, const char * current_uri)
  {
  
  gavl_dictionary_t track;
  gavl_dictionary_t * m;
  int idx = 0;
  const char * uri;
  const char * mimetype;
  xmlDocPtr didl;
  char * didl_str;
  
  gavl_dictionary_init(&track);
  gavl_dictionary_copy(&track, track_p);

  m = gavl_track_get_metadata_nc(&track);
  
  while(gavl_dictionary_get_src(m, GAVL_META_SRC, idx, &mimetype, &uri))
    {
    if(!idx && !strcmp(uri, current_uri))
      idx++;
    else
      gavl_dictionary_delete_item(m, GAVL_META_SRC, idx);
    }
  
  didl = bg_didl_create();

  bg_track_to_didl(didl, &track, NULL);
  didl_str = bg_xml_save_to_memory_opt(didl, XML_SAVE_NO_DECL);
  
  //  gavl_dictionary_delete_item(gavl_dictionary_t * d, const char * name, int item)
  
  gavl_dictionary_free(&track);
  xmlFreeDoc(didl);
  
  return didl_str;
  }

static void do_play(bg_backend_handle_t * be)
  {
  gavl_dictionary_t s;
  gavl_dictionary_t * args_in;
  gavl_dictionary_t * track;
  renderer_t * r;
  const char * uri = NULL;
  char * uri_metadata = NULL;
  
  gavl_dictionary_init(&s);
  
  r = be->priv;


#if 0
  fprintf(stderr, "Current track\n");
  gavl_dictionary_dump(track, 2);
  fprintf(stderr, "\n");
#endif
  
  if(!(track = bg_player_tracklist_get_current_track(&r->tl)))
    {
    fprintf(stderr, "Got no track\n");
    goto fail;
    }
    
  if(!(uri = track_to_uri(be, track)))
    {
    fprintf(stderr, "Track has no uri\n");
    goto fail;
    }
  
  if(!(uri_metadata = track_to_metadata(track, uri)))
    {
    fprintf(stderr, "Track has no metadat\n");
    goto fail;
    }
  //fprintf(stderr, "Got Metadata: %s\n", uri_metadata);

  /* SetAVTransportURI */
  bg_soap_request_init(&s, r->avt_control_url, "AVTransport", 1, "SetAVTransportURI");
  
  args_in = gavl_dictionary_get_dictionary_nc(&s, BG_SOAP_META_ARGS_IN);
  SET_INSTANCE_ID(args_in);

  if(r->AVTransportURI)
    {
    free(r->AVTransportURI);
    r->AVTransportURI = NULL;
    }

  if(r->NextAVTransportURI)
    {
    free(r->NextAVTransportURI);
    r->NextAVTransportURI = NULL;
    }
  
  r->AVTransportURI = bg_string_to_uri(uri, -1);
  
  gavl_dictionary_set_string(args_in, "CurrentURI", r->AVTransportURI);
  gavl_dictionary_set_string(args_in, "CurrentURIMetaData", uri_metadata);

  if(!bg_soap_request(&s, &r->control_fd))
    goto fail;
    
  gavl_dictionary_reset(&s);
    
  /* Play */
  bg_soap_request_init(&s, r->avt_control_url, "AVTransport", 1, "Play");
  
  args_in = gavl_dictionary_get_dictionary_nc(&s, BG_SOAP_META_ARGS_IN);
  SET_INSTANCE_ID(args_in);
  
  gavl_dictionary_set_string(args_in, "Speed", "1");
  
  if(!bg_soap_request(&s, &r->control_fd))
    goto fail;
    
  gavl_dictionary_reset(&s);
  
  fail:

  gavl_dictionary_free(&s);
  
  if(uri_metadata)
    free(uri_metadata);
  
  };
          
static void do_stop(bg_backend_handle_t * be)
  {
  gavl_dictionary_t s;
  renderer_t * r;
  gavl_dictionary_t * args_in;

  r = be->priv;

  bg_soap_request_init(&s, r->avt_control_url, "AVTransport", 1, "Stop");
  
  args_in = gavl_dictionary_get_dictionary_nc(&s, BG_SOAP_META_ARGS_IN);
  SET_INSTANCE_ID(args_in);
  
  bg_soap_request(&s, &r->control_fd);

  r->flags |= RENDERER_STOP_SENT;
  
  gavl_dictionary_free(&s);
  }

static void do_seek(bg_backend_handle_t * be, gavl_time_t time)
  {
  gavl_dictionary_t s;
  renderer_t * r;
  gavl_dictionary_t * args_in;

  char str[GAVL_TIME_STRING_LEN_MS];
  
  r = be->priv;

  bg_soap_request_init(&s, r->avt_control_url, "AVTransport", 1, "Seek");
  
  args_in = gavl_dictionary_get_dictionary_nc(&s, BG_SOAP_META_ARGS_IN);
  SET_INSTANCE_ID(args_in);
  
  gavl_dictionary_set_string(args_in, "Unit", "REL_TIME");
  
  gavl_time_prettyprint_ms_full(time, str);
  gavl_dictionary_set_string(args_in, "Target", str);
  
  bg_soap_request(&s, &r->control_fd);

  //  fprintf(stderr, "do_seek %s\n", str);
  //  gavl_dictionary_dump(&s, 2);
  
  gavl_dictionary_free(&s);
  }

static int handle_msg_renderer(void * priv, // Must be bg_backend_handle_t
                               gavl_msg_t * msg)
  {
  renderer_t * r;
  bg_backend_handle_t * be = priv;

  fprintf(stderr, "handle_msg_renderer\n");
  
  r = be->priv;
  
  if(bg_player_tracklist_handle_message(&r->tl, msg))
    {
    if(r->tl.list_changed || r->tl.current_changed)
      r->flags &= ~RENDERER_HAS_NEXT;
    
    if(r->tl.list_changed)
      r->tl.list_changed = 0;

    if(r->tl.current_changed)
      {
      r->tl.current_changed = 0;

      if((r->flags & RENDERER_OUR_PLAYBACK) &&
         ((r->player_status == BG_PLAYER_STATUS_PLAYING) ||
          (r->player_status == BG_PLAYER_STATUS_SEEKING) ||
          (r->player_status == BG_PLAYER_STATUS_PAUSED)))
        do_stop(be);
      }
    return 1;
    }
  
  switch(msg->NS)
    {
    case BG_MSG_NS_STATE:
      {
      switch(msg->ID)
        {
        case BG_CMD_SET_STATE_REL:
          {
          gavl_msg_t cmd;
          
          gavl_value_t val;
          gavl_value_t add;

          const char * ctx;
          const char * var;
          
          int last = 0;
          
          gavl_value_init(&val);
          gavl_value_init(&add);
          
          bg_msg_get_state(msg, &last, &ctx, &var, &add, NULL);
          
          /* Add (and clamp) value */

          bg_state_add_value(&r->gmerlin_state, ctx, var, &add, &val);
          
          gavl_msg_init(&cmd);
          bg_msg_set_state(&cmd, BG_CMD_SET_STATE, last, ctx, var, &val);
          handle_msg_renderer(priv, &cmd);
          gavl_msg_free(&cmd);
          
          gavl_value_free(&val);
          gavl_value_free(&add);
          
          //          gavl_dprintf("BG_CMD_SET_STATE_REL");
          //          gavl_msg_dump(msg, 2);
          }
          break;
        case BG_CMD_SET_STATE:
          {
          gavl_value_t val;
          const char * ctx;
          const char * var;
          
          int last = 0;
          
          int player_ctx_len = strlen(BG_PLAYER_STATE_CTX);

          gavl_value_init(&val);

          bg_msg_get_state(msg, &last, &ctx, &var, &val, NULL);
          
          if(gavl_string_starts_with(ctx, BG_PLAYER_STATE_CTX) &&
             ((ctx[player_ctx_len] == '/') ||
              (ctx[player_ctx_len] == '\0')))
            {
            if(!strcmp(ctx, BG_PLAYER_STATE_CTX"/"BG_PLAYER_STATE_CURRENT_TIME))          // dictionary
              {
              if(!(r->flags & RENDERER_CAN_SEEK))
                break;
              
              /* Seek */
              if(!strcmp(var, BG_PLAYER_TIME))
                {
                int64_t t = GAVL_TIME_UNDEFINED;
                
                if(gavl_value_get_long(&val, &t))                
                  do_seek(be, t);
                }
              else if(!strcmp(var, BG_PLAYER_TIME_PERC))
                {
                double perc;
                gavl_time_t t;

                if(gavl_value_get_float(&val, &perc))                
                  {
                  t = (int64_t)(perc * ((double)(r->duration)) + 0.5);
                  do_seek(be, t);
                  }
                }
              }
            }
          else if(strcmp(ctx, BG_PLAYER_STATE_CTX))
            {
            if(!strcmp(var, BG_PLAYER_STATE_VOLUME))     // float
              {
              int val_i;
              double val_f;
              gavl_dictionary_t s;
              gavl_dictionary_t * args_in;
              
              if(!(r->flags & RENDERER_HAS_VOLUME) ||
                 !gavl_value_get_float(&val, &val_f))
                break;

              val_i = range_float_to_int(&r->Volume_range, val_f);

              bg_soap_request_init(&s, r->rc_control_url, "RenderingControl", 1, "SetVolume");

              args_in = gavl_dictionary_get_dictionary_nc(&s, BG_SOAP_META_ARGS_IN);

              SET_INSTANCE_ID(args_in);

              gavl_dictionary_set_string(args_in, "Channel", "Master");
              gavl_dictionary_set_string_nocopy(args_in, "DesiredVolume", bg_sprintf("%d", val_i));
              bg_soap_request(&s, &r->control_fd);
              gavl_dictionary_free(&s);
              }
            else if(!strcmp(var, BG_PLAYER_STATE_MODE))          // int
              {
              bg_player_tracklist_set_mode(&r->tl, &val.v.i);

              bg_state_set(&r->gmerlin_state, 1, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_MODE,
                           &val, be->ctrl_int.evt_sink, BG_MSG_STATE_CHANGED);

              if(r->NextAVTransportURI)
                {
                free(r->NextAVTransportURI);
                r->NextAVTransportURI = NULL;
                }
              
              }
            else if(!strcmp(var, BG_PLAYER_STATE_MUTE))          // int
              {
              int val_i;
              gavl_dictionary_t s;
              gavl_dictionary_t * args_in;
              
              if(!(r->flags & RENDERER_HAS_MUTE) ||
                 !gavl_value_get_int(&val, &val_i))
                break;
              val_i &= 1;
              
              bg_soap_request_init(&s, r->rc_control_url, "RenderingControl", 1, "SetMute");

              args_in = gavl_dictionary_get_dictionary_nc(&s, BG_SOAP_META_ARGS_IN);
              SET_INSTANCE_ID(args_in);

              gavl_dictionary_set_string(args_in, "Channel", "Master");
              gavl_dictionary_set_string_nocopy(args_in, "DesiredMute", bg_sprintf("%d", val_i));
              bg_soap_request(&s, &r->control_fd);
              gavl_dictionary_free(&s);
              
              gavl_dprintf("BG_CMD_SET_STATE %s %s ", ctx, var);
              gavl_value_dump(&val, 2);
              gavl_dprintf("\n");
              }
            else
              {
              gavl_dprintf("BG_CMD_SET_STATE %s %s ", ctx, var);
              gavl_value_dump(&val, 2);
              gavl_dprintf("\n");
              }
            }
          else
            {
            gavl_dprintf("BG_CMD_SET_STATE %s %s ", ctx, var);
            gavl_value_dump(&val, 2);
            gavl_dprintf("\n");
            }
          
          gavl_value_free(&val);
          }
          break;
        }
      }
      break;
    case BG_MSG_NS_PLAYER:
      
      switch(msg->ID)
        {
        case BG_PLAYER_CMD_STOP:
          //     gavl_dprintf("BG_PLAYER_CMD_STOP\n");
          do_stop(be);
          break;
        case BG_PLAYER_CMD_PLAY:
          {
          //      gavl_dprintf("BG_PLAYER_CMD_PLAY\n");
          do_play(be);
          }
          break;
        case BG_PLAYER_CMD_NEXT:
          if(!bg_player_tracklist_advance(&r->tl, 1))
            break;

          if(r->player_status == BG_PLAYER_STATUS_PLAYING)
            do_play(be);
             
          break;
        case BG_PLAYER_CMD_PREV:
          if(!bg_player_tracklist_back(&r->tl))
            break;

          if(r->player_status == BG_PLAYER_STATUS_PLAYING)
            do_play(be);
          
          break;
        case BG_PLAYER_CMD_SET_CURRENT_TRACK:
          {
          const char * id = gavl_msg_get_arg_string_c(msg, 0);
          bg_player_tracklist_set_current_by_id(&r->tl, id);
          }
          break;
        case BG_PLAYER_CMD_PLAY_BY_ID:
          {
          const char * id = gavl_msg_get_arg_string_c(msg, 0);
          bg_player_tracklist_set_current_by_id(&r->tl, id);
          do_play(be);
          }
          break;
#if 0 // handled by track list
        case BG_PLAYER_CMD_SET_NEXT_LOCATION:
          gavl_dprintf("BG_PLAYER_CMD_SET_NEXT_LOCATION\n");
          break;
#endif
        case BG_PLAYER_CMD_PAUSE:
          
          if(!(r->flags & RENDERER_CAN_PAUSE))
            break;
          
          if(r->player_status == BG_PLAYER_STATUS_PLAYING)
            {
            gavl_dictionary_t s;
            gavl_dictionary_t * args_in;
            /* play -> pause */
            bg_soap_request_init(&s, r->avt_control_url, "AVTransport", 1, "Pause");
            args_in = gavl_dictionary_get_dictionary_nc(&s, BG_SOAP_META_ARGS_IN);
            SET_INSTANCE_ID(args_in);
            bg_soap_request(&s, &r->control_fd);
            gavl_dictionary_free(&s);
            }
          else if(r->player_status == BG_PLAYER_STATUS_PAUSED)
            {
            gavl_dictionary_t s;
            gavl_dictionary_t * args_in;
            /* pause -> play */
            bg_soap_request_init(&s, r->avt_control_url, "AVTransport", 1, "Play");
            args_in = gavl_dictionary_get_dictionary_nc(&s, BG_SOAP_META_ARGS_IN);
            SET_INSTANCE_ID(args_in);
            gavl_dictionary_set_string(args_in, "Speed", "1");
            bg_soap_request(&s, &r->control_fd);
            gavl_dictionary_free(&s);
            }
          
          //   gavl_dprintf("BG_PLAYER_CMD_PAUSE\n");
          break;
        case BG_PLAYER_CMD_SET_TRACK:
          {
          gavl_value_t val;
          gavl_value_init(&val);
          
          gavl_msg_get_arg(msg, 0, &val);
          
          do_stop(be);
          
          bg_player_tracklist_splice(&r->tl, 0, -1, &val, gavl_msg_get_client_id(msg));
          gavl_value_free(&val);
          
          /* Set current track */
          bg_player_tracklist_set_current_by_idx(&r->tl, 0);
          }
          break;
        case BG_PLAYER_CMD_SET_LOCATION:
          {
          int idx;
          gavl_msg_t msg1;
          
          do_stop(be);
          
          gavl_msg_init(&msg1);

          /* After the last track */
          
          gavl_msg_set_id_ns(&msg1, BG_CMD_DB_LOAD_URIS, BG_MSG_NS_DB);

          gavl_dictionary_set_string(&msg1.header, GAVL_MSG_CONTEXT_ID, BG_PLAYQUEUE_ID);

          idx = gavl_get_num_tracks(r->tl.cnt);
          
          gavl_msg_set_arg_int(&msg1, 0, idx);
          gavl_msg_set_arg(&msg1, 1, gavl_msg_get_arg_c(msg, 0));
          
          bg_player_tracklist_handle_message(&r->tl, &msg1);
          bg_player_tracklist_set_current_by_idx(&r->tl, idx);

          if(gavl_msg_get_arg_int(msg, 1))
            do_play(be);
          }
          break;


        }
      break;
    case BG_MSG_NS_DB:

      
      break;
    case GAVL_MSG_NS_GENERIC:

      switch(msg->ID)
        {
        case GAVL_CMD_QUIT:
          return 0;
          break;
        }
      
    default:
      gavl_dprintf("Unknown msg namespace");
      gavl_msg_dump(msg, 2);
      break;
    }
  return 1;
  }

static int ping_renderer(bg_backend_handle_t * be)
  {
  int ret = 0;
  gavl_time_t cur;
  renderer_t * r = be->priv;
  //  fprintf(stderr, "Ping_renderer");
  ret += bg_upnp_event_listener_ping(r->avt_evt);
  ret += bg_upnp_event_listener_ping(r->rc_evt);

  bg_msg_sink_iteration(r->evt_sink);
  
  cur = gavl_timer_get(r->timer);
  
  if(r->player_status == BG_PLAYER_STATUS_PLAYING)
    {
    /* Poll Interval: 1 s */
    if((r->last_poll_time == GAVL_TIME_UNDEFINED) ||
       (cur - r->last_poll_time > GAVL_TIME_SCALE))
      {
      renderer_poll(be);
      r->last_poll_time = cur;
      ret++;
      }
    }
  
  return ret;
  }

static int handle_http_renderer(bg_backend_handle_t * dev,
                                bg_http_connection_t * conn)
  {
  renderer_t * r = dev->priv;
  return bg_upnp_event_listener_handle(r->avt_evt, conn) +
    bg_upnp_event_listener_handle(r->rc_evt, conn);
  }

const bg_remote_dev_backend_t bg_remote_dev_backend_upnp_renderer =
  {
    .name = "upnp media renderer",
    
    .type = BG_BACKEND_RENDERER,

    .uri_prefix = BG_BACKEND_URI_SCHEME_UPNP_RENDERER"://",
    
    .ping        = ping_renderer,
    .handle_msg  = handle_msg_renderer,
    .handle_http = handle_http_renderer,
    
    .create      = create_renderer,
    .destroy     = destroy_renderer,
  };

#if 0

/* Upnp mediaserver */

const bg_remote_dev_backend_t bg_remote_dev_backend_upnp_mediaserver =
  {
    .name = "upnp media server",
    .protocol = "upnp",
    .type = BG_BACKEND_SERVER,
    
    //    .ping    = ping_gmerlin,
    .create    = create_server,
    .destroy   = destroy_server,
  };

#endif
