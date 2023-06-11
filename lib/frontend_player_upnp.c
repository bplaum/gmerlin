#include <string.h>
#include <sys/utsname.h>
#include <unistd.h>
#include <uuid/uuid.h>
#include <limits.h>

#include <config.h>

#include <gavl/metatags.h>

#include <gmerlin/parameter.h>
#include <gmerlin/upnp/soap.h>
#include <gmerlin/upnp/upnputils.h>
#include <gmerlin/upnp/ssdp.h>
#include <gmerlin/player.h>
#include <gmerlin/playermsg.h>

#include <gmerlin/upnp/didl.h>

#include <gmerlin/upnp/event.h>

#include <gmerlin/frontend.h>
#include <gmerlin/application.h>

#include <gmerlin/utils.h>

#include <frontend_priv.h>

/* https://stackoverflow.com/questions/5459868/c-preprocessor-concatenate-int-to-string */
#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)



#define EVT_INTERVAL (GAVL_TIME_SCALE/5)

/*

  AVTransport:

  GetCurrentTransportActions
  in:  InstanceID
  out: Actions

  GetDeviceCapabilities
  in:  InstanceID
  out: PlayMedia, RecMedia

  GetMediaInfo
  in:  InstanceID
  out: NrTracks, MediaDuration, CurrentURI, CurrentURIMetaData, NextURI, NextURIMetaData, PlayMedium, RecordMedium, WriteStatus

  GetPositionInfo
  in:  InstanceID
  out: Track, TrackDuration, TrackMetaData, TrackURI, RelTime, AbsTime, RelCount, AbsCount

  GetTransportInfo
  in:  InstanceID
  out: CurrentTransportState, CurrentTransportStatus, CurrentSpeed

  GetTransportSettings
  in:  InstanceID
  out: PlayMode, RecQualityMode

  Next
  in:  InstanceID
  out: 

  Pause
  in:  InstanceID
  out:

  Play
  in:  InstanceID, Speed 
  out:

  Previous
  in:  InstanceID
  out:

  Seek
  in:  InstanceID, Unit, Target
  out: 

  SetAVTransportURI
  in:  InstanceID, CurrentURI, CurrentURIMetaData
  out:

  SetNextAVTransportURI
  in:  InstanceID, NextURI, NextURIMetaData
  out:

  SetPlayMode
  in:  InstanceID, NewPlayMode
  out:

  Stop
  in:  InstanceID
  out:
  
  cm:
  
  GetCurrentConnectionIDs
  in:  
  out: ConnectionIDs

  GetCurrentConnectionInfo
  in:  
  out: ConnectionID, RcsID, AVTransportID, ProtocolInfo, PeerConnectionManager, PeerConnectionID, Direction, Status
  
  GetProtocolInfo
  in:  
  out: Source, Sink
 

  rc:
  
  GetMute
  in:  InstanceID, Channel
  out: CurrentMute

  GetVolume
  in:  InstanceID, Channel
  out: CurrentVolume

  ListPresets
  in:  InstanceID
  out: CurrentPresetNameList

  SelectPreset
  in:  InstanceID
  out: PresetName

  SetMute
  in:  InstanceID, Channel, DesiredMute
  out:

  SetVolume
  in:  InstanceID, Channel, DesiredVolume
  out: 

  
*/

static const char * dev_desc;
static const char * avt_desc;
static const char * cm_desc;
static const char * rc_desc;

#define FLAG_HAVE_NODE  (1<<0)
#define FLAG_REGISTERED (1<<1)

typedef struct 
  {
  char * desc;
  char * protocol_info;
  
  gavl_dictionary_t cm_evt;
  gavl_dictionary_t rc_evt;
  gavl_dictionary_t avt_evt;
  bg_http_server_t * srv;
  gavl_dictionary_t state;
  char control_id[37];

  char * next_uri;

  int flags;
  
  } bg_renderer_frontend_upnp_t;

static int ping_player_upnp(bg_frontend_t * fe, gavl_time_t current_time)
  {
  int ret = 0;
  bg_renderer_frontend_upnp_t * p = fe->priv;

  //  fprintf(stderr, "ping_player_upnp %s\n", p->protocol_info);
  
  if((p->flags & FLAG_HAVE_NODE) && !(p->flags & FLAG_REGISTERED) && p->protocol_info)
    {
    gavl_dictionary_t local_dev;
    
    const gavl_value_t * val;
    const char * server_label;
    char uuid_str[37];

    const gavl_array_t * icon_arr;
    char * icons;
    
    char * uri = bg_sprintf("%s/upnp/renderer/desc.xml", bg_http_server_get_root_url(p->srv));

    gavl_dictionary_init(&local_dev);
    gavl_dictionary_set_string_nocopy(&local_dev, GAVL_META_URI,
                                      bg_sprintf("%s://%s", BG_BACKEND_URI_SCHEME_UPNP_RENDERER, uri + 7));

    gavl_dictionary_set_int(&local_dev, BG_BACKEND_TYPE, BG_BACKEND_RENDERER);
    
    if(!(val = bg_state_get(&p->state, BG_APP_STATE_NETWORK_NODE, GAVL_META_LABEL)) ||
       !(server_label = gavl_value_get_string(val)))
      return 0;
    
    gavl_dictionary_set_string(&local_dev, GAVL_META_LABEL, server_label);

    gavl_dictionary_set_string(&local_dev, BG_BACKEND_PROTOCOL, "upnp");
    
    if((val = bg_state_get(&p->state, BG_APP_STATE_NETWORK_NODE, GAVL_META_ICON_URL)) &&
       (icon_arr = gavl_value_get_array(val)))
      {
      icons = bg_upnp_create_icon_list(icon_arr);
      gavl_dictionary_set_array(&local_dev, GAVL_META_ICON_URL, icon_arr);
      }
    else
      icons = gavl_strdup("");

    /* Register local device before creating the ssdp */
    
    bg_backend_register_local(&local_dev);
    
    bg_uri_to_uuid(gavl_dictionary_get_string(&local_dev, GAVL_META_URI), uuid_str);
    
    p->desc = bg_sprintf(dev_desc, uuid_str, server_label, icons);
    
    free(uri);
    free(icons);
    ret++;

    gavl_dictionary_free(&local_dev);
    p->flags |= FLAG_REGISTERED;
    }
  
  bg_msg_sink_iteration(fe->ctrl.evt_sink);
  
  ret += bg_msg_sink_get_num(fe->ctrl.evt_sink);
  
  ret += bg_upnp_event_context_server_update(&p->cm_evt, current_time);
  ret += bg_upnp_event_context_server_update(&p->rc_evt, current_time);
  ret += bg_upnp_event_context_server_update(&p->avt_evt, current_time);
  
  return ret;
  }

static void cleanup_player_upnp(void * priv)
  {
  bg_renderer_frontend_upnp_t * p = priv;

  if(p->desc)
    free(p->desc);
  
  gavl_dictionary_free(&p->rc_evt);
  gavl_dictionary_free(&p->cm_evt);
  gavl_dictionary_free(&p->avt_evt);
  gavl_dictionary_free(&p->state);

  if(p->protocol_info)
    free(p->protocol_info);
  

  free(p);
  }

static const struct
  {
  int gmerlin_status;
  const char * transport_state;
  const char * transport_status;
  }
player_statuses[] =
  {
    { BG_PLAYER_STATUS_INIT,         "NO_MEDIA_PRESENT",  "OK"             },
    { BG_PLAYER_STATUS_STOPPED,      "STOPPED"         ,  "OK"             },
    { BG_PLAYER_STATUS_PLAYING,      "PLAYING"         ,  "OK"             },
    { BG_PLAYER_STATUS_SEEKING,      "TRANSITIONING"   ,  "OK"             },
    { BG_PLAYER_STATUS_CHANGING,     "TRANSITIONING"   ,  "OK"             },
    { BG_PLAYER_STATUS_INTERRUPTED,  "PAUSED_PLAYBACK" ,  "OK"             },
    { BG_PLAYER_STATUS_PAUSED,       "PAUSED_PLAYBACK" ,  "OK"             },
    { BG_PLAYER_STATUS_STARTING,     "TRANSITIONING"   ,  "OK"             },
    { BG_PLAYER_STATUS_ERROR,        "STOPPED"         ,  "ERROR_OCCURRED" },
    { /* */ },
    
  };

static const char * status_to_transport_state(int status)
  {
  int i = 0;

  while(player_statuses[i].transport_state)
    {
    if(player_statuses[i].gmerlin_status == status)
      return player_statuses[i].transport_state;
    i++;
    }
  return NULL;
  }

static const char * status_to_transport_status(int status)
  {
  int i = 0;

  while(player_statuses[i].transport_state)
    {
    if(player_statuses[i].gmerlin_status == status)
      return player_statuses[i].transport_status;
    i++;
    }
  return NULL;
  }

static const struct
  {
  int gmerlin_mode;
  const char * upnp_mode;
  }
player_modes[] =
  {
    { BG_PLAYER_MODE_NORMAL,  "NORMAL"     },
    { BG_PLAYER_MODE_REPEAT,  "REPEAT_ALL" },
    { BG_PLAYER_MODE_SHUFFLE, "SHUFFLE"    },
    { BG_PLAYER_MODE_ONE,     "DIRECT_1"   },
    { BG_PLAYER_MODE_LOOP,    "REPEAT_ONE" },
    { /* End */ },
  };


static const char * player_mode_to_upnp(int gmerlin_mode)
  {
  int i = 0;

  while(player_modes[i].upnp_mode)
    {
    if(player_modes[i].gmerlin_mode == gmerlin_mode)
      return player_modes[i].upnp_mode;
    
    i++;
    }
  return NULL;
  }

static int player_mode_to_gmerlin(const char * upnp_mode)
  {
  int i = 0;

  while(player_modes[i].upnp_mode)
    {
    if(!strcmp(player_modes[i].upnp_mode, upnp_mode))
      return player_modes[i].gmerlin_mode;
    i++;
    }
  return -1;
  }

static int handle_player_message(void * priv, gavl_msg_t * msg)
  {
  bg_frontend_t * fe = priv;
  bg_renderer_frontend_upnp_t * p = fe->priv;
  
  switch(msg->NS)
    {
    case BG_MSG_NS_STATE:
      switch(msg->ID)
        {
        case BG_MSG_STATE_CHANGED:
          {
          int last;
          const char * ctx = NULL;
          const char * var = NULL;
          gavl_value_t val;
          gavl_value_init(&val);
          
          /* Store state locally */
          bg_msg_get_state(msg, &last, &ctx, &var, &val, &p->state);

          if(!strcmp(ctx, BG_PLAYER_STATE_CTX))
            {
            if(!strcmp(var, BG_PLAYER_STATE_VOLUME))
              {
              double volume = 0.0;
              int volume_DB;
              if(gavl_value_get_float(&val, &volume))
                {
                char * str = bg_sprintf("%d", (int)(volume * BG_PLAYER_VOLUME_INT_MAX + 0.5));
                bg_upnp_event_context_server_set_value(&p->rc_evt, "Volume",
                                                       str, EVT_INTERVAL);
                free(str);

                /* VolumeDB */
                
                volume_DB = (int)(bg_player_volume_to_dB((int)(volume *
                                                               BG_PLAYER_VOLUME_INT_MAX +
                                                               0.5)) * 256.0 + 0.5);
                if(volume_DB < -32767)
                  volume_DB = -32767;
                else if(volume_DB > 0)
                  volume_DB = 0;
                
                str = bg_sprintf("%d", volume_DB);
                bg_upnp_event_context_server_set_value(&p->rc_evt, "VolumeDB",
                                                       str, EVT_INTERVAL);
                free(str);
                }
              }
            else if(!strcmp(var, BG_PLAYER_STATE_STATUS))
              {
              
              int status;
              char * CurrentTransportActions = NULL;
              
              if(gavl_value_get_int(&val, &status))
                {
                /* CurrentTransportActions */
                switch(status)
                  {
                  case BG_PLAYER_STATUS_INIT:        //!< Initializing
                    CurrentTransportActions = gavl_strdup("Play");
                    break;
                  case BG_PLAYER_STATUS_STOPPED:     //!< Stopped, waiting for play command
                    CurrentTransportActions = gavl_strdup("Play");
                    break;
                  case BG_PLAYER_STATUS_PLAYING:     //!< Playing
                    {
                    const gavl_dictionary_t * dict;
                    const gavl_value_t      * state_val;
                    
                    CurrentTransportActions = gavl_strdup("Stop");

                    if((state_val = bg_state_get(&p->state,
                                                 BG_PLAYER_STATE_CTX,
                                                 BG_PLAYER_STATE_CURRENT_TRACK)) &&
                       (dict = gavl_value_get_dictionary(state_val)) &&
                       (dict = gavl_track_get_metadata(dict)))
                      {
                      int val_i;

                      val_i = 0;
                      if(gavl_dictionary_get_int(dict, GAVL_META_CAN_SEEK, &val_i) && val_i)
                        CurrentTransportActions = gavl_strcat(CurrentTransportActions, "Seek");

                      if(gavl_dictionary_get_int(dict, GAVL_META_CAN_PAUSE, &val_i) && val_i)
                        CurrentTransportActions = gavl_strcat(CurrentTransportActions, "Pause");
                      
                      
                      }
                    }
                    break;
                  case BG_PLAYER_STATUS_PAUSED:      //!< Paused
                    CurrentTransportActions = gavl_strdup("Stop,Play");
                    break;
                  case BG_PLAYER_STATUS_ERROR:       //!< Error
                    CurrentTransportActions = gavl_strdup("Play");
                    break;

                  case BG_PLAYER_STATUS_CHANGING:    //!< Changing the track
                  case BG_PLAYER_STATUS_SEEKING:     //!< Seeking
                  case BG_PLAYER_STATUS_INTERRUPTED: //!< Playback interrupted (due to parameter- or stream change)
                  case BG_PLAYER_STATUS_STARTING:    //!< Starting playback
                  default: 
                    CurrentTransportActions = gavl_strdup("Stop");
                    break;
                  }
                
                bg_upnp_event_context_server_set_value(&p->avt_evt, "CurrentTransportActions",
                                                       CurrentTransportActions, EVT_INTERVAL);
                
                bg_upnp_event_context_server_set_value(&p->avt_evt, "TransportState",
                                                       status_to_transport_state(status), EVT_INTERVAL);
                
                bg_upnp_event_context_server_set_value(&p->avt_evt, "TransportStatus",
                                                       status_to_transport_status(status), EVT_INTERVAL);

                free(CurrentTransportActions);
                }
              
              }
            else if(!strcmp(var, BG_PLAYER_STATE_CURRENT_TRACK))
              {
              const gavl_dictionary_t * dict;
              gavl_time_t duration;
              char time_str[GAVL_TIME_STRING_LEN_MS];
              
              if((dict = gavl_value_get_dictionary(&val)))
                {
                char * str;
                const char * uri;
                xmlDocPtr didl = bg_didl_create();
                bg_track_to_didl(didl, dict, NULL);

                str = bg_xml_save_to_memory_opt(didl, XML_SAVE_NO_DECL);
                xmlFreeDoc(didl);
                
                bg_upnp_event_context_server_set_value(&p->avt_evt, "CurrentTrackMetaData", str, EVT_INTERVAL);
                free(str);
                
                if(gavl_dictionary_get_long(dict, GAVL_META_APPROX_DURATION, &duration) &&
                   (duration != GAVL_TIME_UNDEFINED))
                  gavl_time_prettyprint_ms_full(duration, time_str);
                else
                  strncpy(time_str, "0:00.000", GAVL_TIME_STRING_LEN_MS);
                
                bg_upnp_event_context_server_set_value(&p->avt_evt, "CurrentTrackDuration", time_str, EVT_INTERVAL);

                // gavl_dictionary_get_int(dict, GAVL_META_SRCIDX, &srcidx);

                if((uri = bg_track_get_current_location(dict)))
                  {
                  //                  fprintf(stderr, "upnp: Got uri %s\n", uri);

                  bg_upnp_event_context_server_set_value(&p->avt_evt, "CurrentTrackURI", uri, EVT_INTERVAL);
                  }
                else
                  bg_upnp_event_context_server_set_value(&p->avt_evt, "CurrentTrackURI", BG_SOAP_ARG_EMPTY, EVT_INTERVAL);
                
                // CurrentTrackURI
                
                }

              }
            else if(!strcmp(var, BG_PLAYER_STATE_CURRENT_TIME))
              {
              const gavl_dictionary_t * dict;

              if((dict = gavl_value_get_dictionary(&val)))
                {
                
                }
              }
            else if(!strcmp(var, BG_PLAYER_STATE_MODE))
              {
              int mode;

              if(gavl_value_get_int(&val, &mode))
                {
                const char * upnp_mode = player_mode_to_upnp(mode);
                bg_upnp_event_context_server_set_value(&p->avt_evt, "CurrentPlayMode", upnp_mode, EVT_INTERVAL);
                }
              }
            else if(!strcmp(var, BG_PLAYER_STATE_MUTE))
              {
              int mute;
              
              if(gavl_value_get_int(&val, &mute))
                {
                char * str = bg_sprintf("%d", mute);
                bg_upnp_event_context_server_set_value(&p->rc_evt, "Mute",
                                                       str, EVT_INTERVAL);
                free(str);
                }
              }
            else if(!strcmp(var, BG_PLAYER_STATE_MIMETYPES))
              {
              if(!p->protocol_info)
                {
                char * tmp_string;
                int i;
                const gavl_array_t * arr = gavl_value_get_array(&val);

                for(i = 0; i < arr->num_entries; i++)
                  {
                  if(i)
                    p->protocol_info = gavl_strcat(p->protocol_info, ",");

                  tmp_string = bg_sprintf("http-get:*:%s:*", gavl_string_array_get(arr, i));
                  p->protocol_info = gavl_strcat(p->protocol_info, tmp_string);
                  free(tmp_string);
                  }
                
                }
              
              }
            }

          else if(!strcmp(ctx, BG_APP_STATE_NETWORK_NODE) && (!var || last))
            {
            p->flags |= FLAG_HAVE_NODE;
            }
            
          /* Send events */
          //          bg_upnp_event_context_server_set_value(gavl_dictionary_t * dict, const char * name,
          //                                                 const char * val,
          //                                                 gavl_time_t update_interval)          
            
          gavl_value_free(&val);
          }
          break;
        }
      break;
    }
  
  return 1;
  }


static void make_track(gavl_dictionary_t * track,
                       const char * uri,
                       const char * metadata)
  {
  xmlDocPtr doc = NULL;
  xmlNodePtr child;
  
  gavl_dictionary_t * m;

  if((doc = xmlParseMemory(metadata, strlen(metadata))) &&
     (child = bg_xml_find_doc_child(doc, "DIDL-Lite")) &&
     (child = bg_xml_find_node_child(child, "item")))
    bg_track_from_didl(track, child);
  
  m = gavl_track_get_metadata_nc(track);
  
  if(doc)
    xmlFreeDoc(doc);
  
  if(!gavl_metadata_has_src(m, GAVL_META_SRC, uri))
    gavl_metadata_add_src(m, GAVL_META_SRC, NULL, uri);
  }

#define CHECK_INSTANCE_ID(code)                                            \
  if(!(InstanceID = gavl_dictionary_get_string(args_in, "InstanceID")) || \
     strcmp(InstanceID, "0")) \
    { \
    bg_soap_request_set_error(soap, code, "Invalid InstanceID"); \
    bg_upnp_finish_soap_request(soap, c, priv->srv);          \
    return 1;  \
    }


static int handle_http_request(bg_http_connection_t * c, void * data)
  {
  const char * InstanceID;
  
  bg_frontend_t * fe = data;

  bg_renderer_frontend_upnp_t * priv = fe->priv;
  
  if(!strcmp(c->method, "GET") || !strcmp(c->method, "HEAD"))    
    {
    if(!strcmp(c->path, "desc.xml"))
      {
      bg_upnp_send_description(c, priv->desc);
      return 1;
      }
    else if(!strcmp(c->path, "cm/desc.xml"))
      {
      bg_upnp_send_description(c, cm_desc);
      return 1;
      }
    else if(!strcmp(c->path, "rc/desc.xml"))
      {
      bg_upnp_send_description(c, rc_desc);
      return 1;
      }
    else if(!strcmp(c->path, "avt/desc.xml"))
      {
      bg_upnp_send_description(c, avt_desc);
      return 1;
      }
    }
  
  else if(!strcmp(c->path, "rc/ctrl"))
    {
    const char * func;
    gavl_dictionary_t * soap;
    const gavl_dictionary_t * args_in;
    gavl_dictionary_t * args_out;
    
    soap = gavl_dictionary_create();

    if(!bg_soap_request_read_req(soap, c))
      {
      gavl_socket_close(c->fd);
      c->fd = -1;
      return 1;
      }

    func = gavl_dictionary_get_string(soap, BG_SOAP_META_FUNCTION);
    args_in  = gavl_dictionary_get_dictionary(soap, BG_SOAP_META_ARGS_IN);
    args_out = gavl_dictionary_get_dictionary_nc(soap, BG_SOAP_META_ARGS_OUT);

    /* Handle functions */

    /*
      GetMute
      in:  InstanceID, Channel
      out: CurrentMute
    */
    if(!strcmp(func, "GetMute"))
      {
      CHECK_INSTANCE_ID(702);
      
      gavl_dictionary_set_string(args_out, "CurrentMute", bg_upnp_event_context_server_get_value(&priv->rc_evt, "Mute"));
      bg_upnp_finish_soap_request(soap, c, priv->srv);
      }
    
    /*
      GetVolume
      in:  InstanceID, Channel
      out: CurrentVolume
    */
    else if(!strcmp(func, "GetVolume"))
      {
      CHECK_INSTANCE_ID(702);
      gavl_dictionary_set_string(args_out, "CurrentVolume",
                                 bg_upnp_event_context_server_get_value(&priv->rc_evt, "Volume"));
      bg_upnp_finish_soap_request(soap, c, priv->srv);
      return 1;
      }

    /*
      GetVolumeDB
      in:  InstanceID, Channel
      out: CurrentVolume
    */
    else if(!strcmp(func, "GetVolumeDB"))
      {
      CHECK_INSTANCE_ID(702);
      gavl_dictionary_set_string(args_out, "CurrentVolume",
                                 bg_upnp_event_context_server_get_value(&priv->rc_evt, "VolumeDB"));
      bg_upnp_finish_soap_request(soap, c, priv->srv);
      return 1;
      }
    
    
    /*
      ListPresets
      in:  InstanceID
      out: CurrentPresetNameList
    */
    else if(!strcmp(func, "ListPresets"))
      {
      CHECK_INSTANCE_ID(702);
      gavl_dictionary_set_string(args_out, "CurrentPresetNameList", "FactoryDefaults");
      bg_upnp_finish_soap_request(soap, c, priv->srv);
      }
    
    /*
      SelectPreset
      in:  InstanceID
      in: PresetName
    */
    else if(!strcmp(func, "SelectPreset"))
      {
      CHECK_INSTANCE_ID(702);

      /* Does nothing for now */
      
      bg_upnp_finish_soap_request(soap, c, priv->srv);
      }

    /*
      SetMute
      in:  InstanceID, Channel, DesiredMute
      out:
    */
    else if(!strcmp(func, "SetMute"))
      {
      gavl_value_t val;
      const char * Channel;
      const char * DesiredMute;
      
      CHECK_INSTANCE_ID(702);

      Channel     = gavl_dictionary_get_string(args_in, "Channel");
      DesiredMute = gavl_dictionary_get_string(args_in, "DesiredMute");

      if(!Channel || strcmp(Channel, "Master"))
        {
        bg_soap_request_set_error(soap, 402, "Invalid Args");
        bg_upnp_finish_soap_request(soap, c, priv->srv);
        return 1;
        }

      gavl_value_init(&val);
      gavl_value_set_int(&val, bg_upnp_parse_bool(DesiredMute));
      
      bg_state_set(NULL, 1, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_MUTE,
                   &val, fe->ctrl.cmd_sink, BG_CMD_SET_STATE);
      bg_upnp_finish_soap_request(soap, c, priv->srv);
      return 1;
      }

    /*
      SetVolume
      in:  InstanceID, Channel, DesiredVolume
      out: 
    */
    else if(!strcmp(func, "SetVolume"))
      {
      const char * Channel;
      const char * DesiredVolume;

      CHECK_INSTANCE_ID(702);

      Channel       = gavl_dictionary_get_string(args_in, "Channel");
      DesiredVolume = gavl_dictionary_get_string(args_in, "DesiredVolume");

      //      fprintf(stderr, "Set volume %s %s\n", Channel, DesiredVolume);
      
      if(!Channel || strcmp(Channel, "Master") ||
         !DesiredVolume)
        {
        bg_soap_request_set_error(soap, 402, "Invalid Args");
        bg_upnp_finish_soap_request(soap, c, priv->srv);
        return 1;
        }

      bg_player_set_volume(fe->ctrl.cmd_sink, (double)atoi(DesiredVolume) / (double)BG_PLAYER_VOLUME_INT_MAX);
      bg_upnp_finish_soap_request(soap, c, priv->srv);
      }
    else if(!strcmp(func, "SetVolumeDB"))
      {
      const char * Channel;
      const char * DesiredVolume;
      
      CHECK_INSTANCE_ID(702);

      Channel       = gavl_dictionary_get_string(args_in, "Channel");
      DesiredVolume = gavl_dictionary_get_string(args_in, "DesiredVolume");

      if(!Channel || strcmp(Channel, "Master") ||
         !DesiredVolume)
        {
        bg_soap_request_set_error(soap, 402, "Invalid Args");
        bg_upnp_finish_soap_request(soap, c, priv->srv);
        return 1;
        }
      
      bg_player_set_volume(fe->ctrl.cmd_sink, (double)bg_player_volume_from_dB((double)atoi(DesiredVolume)/256.0));
      bg_upnp_finish_soap_request(soap, c, priv->srv);

      bg_upnp_event_context_server_set_value(&priv->rc_evt, "VolumeDB", DesiredVolume, EVT_INTERVAL);
      }
    
    gavl_dictionary_destroy(soap);
    }
  else if(!strcmp(c->path, "cm/ctrl"))
    {
    const char * func;
    gavl_dictionary_t * soap;
    //    const gavl_dictionary_t * args_in;
    gavl_dictionary_t * args_out;
    
    soap = gavl_dictionary_create();

    if(!bg_soap_request_read_req(soap, c))
      {
      gavl_socket_close(c->fd);
      c->fd = -1;
      return 1;
      }

    func = gavl_dictionary_get_string(soap, BG_SOAP_META_FUNCTION);
    //    args_in  = gavl_dictionary_get_dictionary(soap, BG_SOAP_META_ARGS_IN);
    args_out = gavl_dictionary_get_dictionary_nc(soap, BG_SOAP_META_ARGS_OUT);

    /* Handle functions */

    /* 
       GetCurrentConnectionIDs
       in:  
       out: ConnectionIDs
    */
    if(!strcmp(func, "GetCurrentConnectionIDs"))
      {
      gavl_dictionary_set_string(args_out, "ConnectionIDs", BG_SOAP_ARG_EMPTY);
      bg_upnp_finish_soap_request(soap, c, priv->srv);
      }
    
    /*
      GetCurrentConnectionInfo
      in:  ConnectionID
      out: RcsID, AVTransportID, ProtocolInfo, PeerConnectionManager, PeerConnectionID, Direction, Status
    */
    // TODO
    else if(!strcmp(func, "GetCurrentConnectionInfo"))
      {
      return 0;
      }
    
    /*
      GetProtocolInfo
      in:  
      out: Source, Sink
    */
    else if(!strcmp(func, "GetProtocolInfo"))
      {
      gavl_dictionary_set_string(args_out, "Source", BG_SOAP_ARG_EMPTY);
      gavl_dictionary_set_string(args_out, "Sink", priv->protocol_info);
      
      bg_upnp_finish_soap_request(soap, c, priv->srv);
      }
    
    gavl_dictionary_destroy(soap);
    
    }
  else if(!strcmp(c->path, "avt/ctrl"))
    {
    const char * func;
    gavl_dictionary_t * soap;
    const gavl_dictionary_t * args_in;
    gavl_dictionary_t * args_out;
    
    soap = gavl_dictionary_create();

    if(!bg_soap_request_read_req(soap, c))
      {
      gavl_socket_close(c->fd);
      c->fd = -1;
      return 1;
      }

    func = gavl_dictionary_get_string(soap, BG_SOAP_META_FUNCTION);
    args_in  = gavl_dictionary_get_dictionary(soap, BG_SOAP_META_ARGS_IN);
    args_out = gavl_dictionary_get_dictionary_nc(soap, BG_SOAP_META_ARGS_OUT);

    /* Handle functions */

    /*
      GetCurrentTransportActions
      in:  InstanceID
      out: Actions
    */
    if(!strcmp(func, "GetCurrentTransportActions"))
      {
      CHECK_INSTANCE_ID(718);
      
      gavl_dictionary_set_string(args_out, "Actions",
                                 bg_upnp_event_context_server_get_value(&priv->avt_evt, "CurrentTransportActions"));
      
      bg_upnp_finish_soap_request(soap, c, priv->srv);
      }

    /*
      GetDeviceCapabilities
      in:  InstanceID
      out: PlayMedia, RecMedia, RecQualityModes
    */
    else if(!strcmp(func, "GetDeviceCapabilities"))
      {
      CHECK_INSTANCE_ID(718);

      gavl_dictionary_set_string(args_out, "PlayMedia",      "NONE,NETWORK");
      gavl_dictionary_set_string(args_out, "RecMedia",       "NOT_IMPLEMENTED");
      gavl_dictionary_set_string(args_out, "RecQualityModes","NOT_IMPLEMENTED");
      bg_upnp_finish_soap_request(soap, c, priv->srv);
      }

    /*
      GetMediaInfo
      in:  InstanceID
      out: NrTracks, MediaDuration, CurrentURI, CurrentURIMetaData, NextURI, NextURIMetaData, PlayMedium, RecordMedium, WriteStatus
    */
    // TODO
    else if(!strcmp(func, "GetMediaInfo"))
      {
      CHECK_INSTANCE_ID(718);
      
      gavl_dictionary_set_string(args_out, "NrTracks",      bg_upnp_event_context_server_get_value(&priv->avt_evt, "NumberOfTracks"));
      gavl_dictionary_set_string(args_out, "MediaDuration", "NOT_IMPLEMENTED");

      gavl_dictionary_set_string(args_out, "CurrentURI",         bg_upnp_event_context_server_get_value(&priv->avt_evt, "AVTransportURI"));
      gavl_dictionary_set_string(args_out, "CurrentURIMetaData", bg_upnp_event_context_server_get_value(&priv->avt_evt, "AVTransportURIMetaData"));

      gavl_dictionary_set_string(args_out, "NextURI",            bg_upnp_event_context_server_get_value(&priv->avt_evt, "NextAVTransportURI"));
      gavl_dictionary_set_string(args_out, "NextURIMetaData",    bg_upnp_event_context_server_get_value(&priv->avt_evt, "NextAVTransportURIMetaData"));
      
      gavl_dictionary_set_string(args_out, "PlayMedium",   "NETWORK");
      gavl_dictionary_set_string(args_out, "RecordMedium", "NOT_IMPLEMENTED");
      gavl_dictionary_set_string(args_out, "WriteStatus",  "NOT_IMPLEMENTED");
      
      }

    /*
      GetPositionInfo
      in:  InstanceID
      out: Track, TrackDuration, TrackMetaData, TrackURI, RelTime, AbsTime, RelCount, AbsCount
    */
    // TODO
    else if(!strcmp(func, "GetPositionInfo"))
      {
      const gavl_value_t * val;
      const gavl_dictionary_t * dict;
      gavl_time_t t;
      char time_str[GAVL_TIME_STRING_LEN_MS];
      
      
      CHECK_INSTANCE_ID(718);

      gavl_dictionary_set_string(args_out, "Track", "0"); // TODO
      
      gavl_dictionary_set_string(args_out, "TrackDuration", bg_upnp_event_context_server_get_value(&priv->avt_evt, "CurrentTrackDuration")); // TODO
      gavl_dictionary_set_string(args_out, "TrackMetaData", bg_upnp_event_context_server_get_value(&priv->avt_evt, "CurrentTrackMetaData"));
      gavl_dictionary_set_string(args_out, "TrackURI",      bg_upnp_event_context_server_get_value(&priv->avt_evt, "CurrentTrackURI")); // TODO
      
      if((val = bg_state_get(&priv->state,
                             BG_PLAYER_STATE_CTX,
                             BG_PLAYER_STATE_CURRENT_TIME)) &&
         (dict = gavl_value_get_dictionary(val)) &&
         gavl_dictionary_get_long(dict, BG_PLAYER_TIME, &t))
        gavl_time_prettyprint_ms_full(t, time_str);
      else
        strncpy(time_str, "0:00:00.000", GAVL_TIME_STRING_LEN_MS);

      // fprintf(stderr, "Got time: %s\n", time_str);
      
      gavl_dictionary_set_string(args_out, "RelTime", time_str); // TODO
      gavl_dictionary_set_string(args_out, "AbsTime", time_str);
      gavl_dictionary_set_string(args_out, "RelCount", "2147483647");
      gavl_dictionary_set_string(args_out, "AbsCount", "2147483647");
      bg_upnp_finish_soap_request(soap, c, priv->srv);
      
      }

    /*
      GetTransportInfo
      in:  InstanceID
      out: CurrentTransportState, CurrentTransportStatus, CurrentSpeed
    */
    else if(!strcmp(func, "GetTransportInfo"))
      {
      CHECK_INSTANCE_ID(718);

      gavl_dictionary_set_string(args_out, "CurrentTransportState",  bg_upnp_event_context_server_get_value(&priv->avt_evt, "TransportState"));
      gavl_dictionary_set_string(args_out, "CurrentTransportStatus", bg_upnp_event_context_server_get_value(&priv->avt_evt, "TransportStatus"));
      gavl_dictionary_set_string(args_out, "CurrentSpeed", "1");
      bg_upnp_finish_soap_request(soap, c, priv->srv);
      }
    
    /*
      GetTransportSettings
      in:  InstanceID
      out: PlayMode, RecQualityMode
    */
    else if(!strcmp(func, "GetTransportSettings"))
      {
      CHECK_INSTANCE_ID(718);

      gavl_dictionary_set_string(args_out, "PlayMode", bg_upnp_event_context_server_get_value(&priv->avt_evt, "CurrentPlayMode"));
      gavl_dictionary_set_string(args_out, "RecQualityMode", "NOT_IMPLEMENTED");
      bg_upnp_finish_soap_request(soap, c, priv->srv);
      }
    
    /*
      Next
      in:  InstanceID
      out: 
    */
    else if(!strcmp(func, "Next"))
      {
      CHECK_INSTANCE_ID(718);
      bg_player_next(fe->ctrl.cmd_sink);
      bg_upnp_finish_soap_request(soap, c, priv->srv);
      }
    
    /*
      Pause
      in:  InstanceID
      out:
    */
    else if(!strcmp(func, "Pause"))
      {
      CHECK_INSTANCE_ID(718);
      bg_player_pause(fe->ctrl.cmd_sink);
      bg_upnp_finish_soap_request(soap, c, priv->srv);
      }

    /*
      Play
      in:  InstanceID, Speed 
      out:
    */
    else if(!strcmp(func, "Play"))
      {
      CHECK_INSTANCE_ID(718);
      bg_player_play(fe->ctrl.cmd_sink);
      bg_upnp_finish_soap_request(soap, c, priv->srv);
      }

    /*
      Previous
      in:  InstanceID
      out:
    */
    else if(!strcmp(func, "Previous"))
      {
      CHECK_INSTANCE_ID(718);
      bg_player_prev(fe->ctrl.cmd_sink);
      bg_upnp_finish_soap_request(soap, c, priv->srv);
      }

    /*
      Seek
      in:  InstanceID, Unit, Target
      out: 
    */
    // TODO
    else if(!strcmp(func, "Seek"))
      {
      const char * Unit;
      const char * Target;
      gavl_time_t t;
      
      CHECK_INSTANCE_ID(718);

      Unit    = gavl_dictionary_get_string(args_in, "Unit");
      Target  = gavl_dictionary_get_string(args_in, "Target");

      //      fprintf(stderr, "Seek: %s %s\n", Unit, Target);
      
      if(!strcmp(Unit, "REL_TIME"))
        {
        gavl_time_parse(Target, &t);

        //        bg_player_seek(bg_msg_sink_t * sink, gavl_time_t time, int scale)
        
        bg_player_seek(fe->ctrl.cmd_sink, t, GAVL_TIME_SCALE);
        bg_upnp_finish_soap_request(soap, c, priv->srv);
        }
      }
    
    /*
      SetAVTransportURI
      in:  InstanceID, CurrentURI, CurrentURIMetaData
      out:
    */
    else if(!strcmp(func, "SetAVTransportURI"))
      {
      gavl_dictionary_t track;
      const char * CurrentURI;
      const char * CurrentURIMetaData;
      
      CHECK_INSTANCE_ID(718);
      
      CurrentURI         = gavl_dictionary_get_string(args_in, "CurrentURI");
      CurrentURIMetaData = gavl_dictionary_get_string(args_in, "CurrentURIMetaData");

      gavl_dictionary_init(&track);
      make_track(&track, CurrentURI, CurrentURIMetaData);
      bg_player_set_track(fe->ctrl.cmd_sink, &track);
      gavl_dictionary_free(&track);
      
      bg_upnp_event_context_server_set_value(&priv->avt_evt, "AVTransportURI", CurrentURI, EVT_INTERVAL);
      bg_upnp_event_context_server_set_value(&priv->avt_evt, "AVTransportURIMetaData", CurrentURIMetaData, EVT_INTERVAL);
      bg_upnp_finish_soap_request(soap, c, priv->srv);
      }
    
    /*
      SetNextAVTransportURI
      in:  InstanceID, NextURI, NextURIMetaData
      out:
    */
    else if(!strcmp(func, "SetNextAVTransportURI"))
      {
      gavl_dictionary_t track;
      const char * NextURI;
      const char * NextURIMetaData;

      CHECK_INSTANCE_ID(718);
      
      NextURI         = gavl_dictionary_get_string(args_in, "NextURI");
      NextURIMetaData = gavl_dictionary_get_string(args_in, "NextURIMetaData");

      // fprintf(stderr, "SetNextAVTransportURI %s\n%s\n", NextURI, NextURIMetaData);
      
      gavl_dictionary_init(&track);
      make_track(&track, NextURI, NextURIMetaData);
      bg_player_set_next_track(fe->ctrl.cmd_sink, &track);
      gavl_dictionary_free(&track);

      bg_upnp_event_context_server_set_value(&priv->avt_evt, "NextAVTransportURI", NextURI, EVT_INTERVAL);
      bg_upnp_event_context_server_set_value(&priv->avt_evt, "NextAVTransportURIMetaData", NextURIMetaData, EVT_INTERVAL);
      bg_upnp_finish_soap_request(soap, c, priv->srv);
      }

    /*
      SetPlayMode
      in:  InstanceID, NewPlayMode
      out:
    */
    else if(!strcmp(func, "SetPlayMode"))
      {
      int mode;
      const char * NewPlayMode;
      CHECK_INSTANCE_ID(718);

      NewPlayMode = gavl_dictionary_get_string(args_in, "NewPlayMode");
      if((mode = player_mode_to_gmerlin(NewPlayMode)) >= 0)
        {
        gavl_value_t val;
        gavl_value_init(&val);
        gavl_value_set_int(&val, mode);
        
        bg_state_set(NULL, 1, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_MUTE,
                     &val, fe->ctrl.cmd_sink, BG_CMD_SET_STATE);
        bg_upnp_finish_soap_request(soap, c, priv->srv);
        }
      
      }

    /*
      Stop
      in:  InstanceID
      out:
    */
    else if(!strcmp(func, "Stop"))
      {
      CHECK_INSTANCE_ID(718);
      bg_player_stop(fe->ctrl.cmd_sink);
      bg_upnp_finish_soap_request(soap, c, priv->srv);
      
      }
    
    gavl_dictionary_destroy(soap);
    
    }
  return 0; // 404
  }



bg_frontend_t *
bg_frontend_create_player_upnp(bg_http_server_t * srv,
                               bg_controllable_t * ctrl)
  {
  uuid_t control_uuid;
  
  bg_renderer_frontend_upnp_t * priv;
  
  bg_frontend_t * ret = bg_frontend_create(ctrl);

  ret->ping_func    =    ping_player_upnp;
  ret->cleanup_func = cleanup_player_upnp;

  priv = calloc(1, sizeof(*priv));
  
  priv->srv = srv;
  ret->priv = priv;

  /* Initialize state variables */
  
  bg_upnp_event_context_server_set_value(&priv->avt_evt, "TransportState",               "NO_MEDIA_PRESENT", EVT_INTERVAL);
  bg_upnp_event_context_server_set_value(&priv->avt_evt, "TransportStatus",              "OK",               EVT_INTERVAL);
  bg_upnp_event_context_server_set_value(&priv->avt_evt, "PlaybackStorageMedium",        "NONE",             EVT_INTERVAL);
  bg_upnp_event_context_server_set_value(&priv->avt_evt, "RecordStorageMedium",          "NOT_IMPLEMENTED",  EVT_INTERVAL);
  bg_upnp_event_context_server_set_value(&priv->avt_evt, "PossiblePlaybackStorageMedia", "NONE,NETWORK",     EVT_INTERVAL);
  bg_upnp_event_context_server_set_value(&priv->avt_evt, "PossibleRecordStorageMedia",   "NOT_IMPLEMENTED",  EVT_INTERVAL);
  bg_upnp_event_context_server_set_value(&priv->avt_evt, "CurrentPlayMode",              "NORMAL",           EVT_INTERVAL);
  bg_upnp_event_context_server_set_value(&priv->avt_evt, "TransportPlaySpeed",           "1",                EVT_INTERVAL);
  bg_upnp_event_context_server_set_value(&priv->avt_evt, "RecordMediumWriteStatus",      "NOT_IMPLEMENTED",  EVT_INTERVAL);
  bg_upnp_event_context_server_set_value(&priv->avt_evt, "CurrentRecordQualityMode",     "NOT_IMPLEMENTED",  EVT_INTERVAL);
  bg_upnp_event_context_server_set_value(&priv->avt_evt, "PossibleRecordQualityModes",   "NOT_IMPLEMENTED",  EVT_INTERVAL);
  bg_upnp_event_context_server_set_value(&priv->avt_evt, "NumberOfTracks",               "0",                EVT_INTERVAL);
  bg_upnp_event_context_server_set_value(&priv->avt_evt, "CurrentTrack",                 "0",                EVT_INTERVAL);
  bg_upnp_event_context_server_set_value(&priv->avt_evt, "CurrentTrackDuration",         "00:00.000",        EVT_INTERVAL);
  bg_upnp_event_context_server_set_value(&priv->avt_evt, "CurrentMediaDuration",         "00:00.000",        EVT_INTERVAL);
  bg_upnp_event_context_server_set_value(&priv->avt_evt, "CurrentTrackMetaData",         BG_SOAP_ARG_EMPTY,  EVT_INTERVAL);
  bg_upnp_event_context_server_set_value(&priv->avt_evt, "CurrentTrackURI",              BG_SOAP_ARG_EMPTY,  EVT_INTERVAL);
  bg_upnp_event_context_server_set_value(&priv->avt_evt, "AVTransportURI",               BG_SOAP_ARG_EMPTY,  EVT_INTERVAL);
  bg_upnp_event_context_server_set_value(&priv->avt_evt, "AVTransportURIMetaData",       BG_SOAP_ARG_EMPTY,  EVT_INTERVAL);
  bg_upnp_event_context_server_set_value(&priv->avt_evt, "NextAVTransportURI",           BG_SOAP_ARG_EMPTY,  EVT_INTERVAL);
  bg_upnp_event_context_server_set_value(&priv->avt_evt, "NextAVTransportURIMetaData",   BG_SOAP_ARG_EMPTY,  EVT_INTERVAL);
  bg_upnp_event_context_server_set_value(&priv->avt_evt, "CurrentTransportActions",      BG_SOAP_ARG_EMPTY,  EVT_INTERVAL);

  bg_upnp_event_context_server_set_value(&priv->rc_evt,  "Mute",                         "0",                EVT_INTERVAL);
  bg_upnp_event_context_server_set_value(&priv->rc_evt,  "Volume",                       "0",                EVT_INTERVAL);
  bg_upnp_event_context_server_set_value(&priv->rc_evt,  "VolumeDB",                     "-32767",           EVT_INTERVAL);
  
  uuid_generate(control_uuid);
  uuid_unparse(control_uuid, priv->control_id);
  
  bg_control_init(&ret->ctrl, bg_msg_sink_create(handle_player_message, ret, 0));

  /* Add the event handlers first */

  bg_upnp_event_context_init_server(&priv->cm_evt, "/upnp/renderer/cm/evt", srv);
  bg_upnp_event_context_init_server(&priv->rc_evt, "/upnp/renderer/rc/evt", srv);
  bg_upnp_event_context_init_server(&priv->avt_evt, "/upnp/renderer/avt/evt", srv);
  
  bg_http_server_add_handler(srv, handle_http_request, BG_HTTP_PROTO_HTTP, "/upnp/renderer/", // E.g. /static/ can be NULL
                             ret);

  bg_frontend_init(ret);

  bg_controllable_connect(ctrl, &ret->ctrl);
  
  return ret;
  }

/* Device description */

static const char * dev_desc =
"<?xml version=\"1.0\" encoding=\"utf-8\"?>"
"<root xmlns=\"urn:schemas-upnp-org:device-1-0\" "
"      xmlns:dlna=\"urn:schemas-dlna-org:device-1-0\" "
"      xmlns:sec=\"http://www.sec.co.kr/dlna\">"
"  <specVersion>"
"    <major>1</major>"
"    <minor>0</minor>"
"  </specVersion>"
"  <device>"
"    <UDN>uuid:%s</UDN>"
"    <friendlyName>%s</friendlyName>"
"    <deviceType>urn:schemas-upnp-org:device:MediaRenderer:1</deviceType>"
"    <manufacturer>Gmerlin project</manufacturer>"
"    <manufacturerURL>http://gmerlin.sourceforge.net</manufacturerURL>"
"    <modelName>Gmerlin Media Renderer</modelName>"
"    <modelDescription></modelDescription>"
"    <modelNumber>"VERSION"</modelNumber>"
"    <modelURL>http://gmerlin.sourceforge.net</modelURL>"
"    <serialNumber>"VERSION"</serialNumber>"
"%s"
"    <serviceList>"
"      <service>"
"        <serviceType>urn:schemas-upnp-org:service:RenderingControl:1</serviceType>"
"        <serviceId>urn:upnp-org:serviceId:RenderingControl</serviceId>"
"        <SCPDURL>/upnp/renderer/rc/desc.xml</SCPDURL>"
"        <controlURL>/upnp/renderer/rc/ctrl</controlURL>"
"        <eventSubURL>/upnp/renderer/rc/evt</eventSubURL>"
"      </service>"
"      <service>"
"        <serviceType>urn:schemas-upnp-org:service:ConnectionManager:1</serviceType>"
"        <serviceId>urn:upnp-org:serviceId:ConnectionManager</serviceId>"
"        <SCPDURL>/upnp/renderer/cm/desc.xml</SCPDURL>"
"        <controlURL>/upnp/renderer/cm/ctrl</controlURL>"
"        <eventSubURL>/upnp/renderer/cm/evt</eventSubURL>"
"      </service>"
"      <service>"
"        <serviceType>urn:schemas-upnp-org:service:AVTransport:1</serviceType>"
"        <serviceId>urn:upnp-org:serviceId:AVTransport</serviceId>"
"        <SCPDURL>/upnp/renderer/avt/desc.xml</SCPDURL>"
"        <controlURL>/upnp/renderer/avt/ctrl</controlURL>"
"        <eventSubURL>/upnp/renderer/avt/evt</eventSubURL>"
"      </service>"
"    </serviceList>"
"  </device>"
"</root>";

/* Service descriptions */

static const char * avt_desc =
"<?xml version=\"1.0\" encoding=\"utf-8\"?>"
"<scpd xmlns=\"urn:schemas-upnp-org:service-1-0\">"
  "<specVersion>"
    "<major>1</major>"
    "<minor>0</minor>"
"  </specVersion>"
"  <actionList>"
    "<action>"
"      <name>GetCurrentTransportActions</name>"
"      <argumentList>"
"        <argument>"
"          <name>InstanceID</name>"
"          <direction>in</direction>"
"          <relatedStateVariable>A_ARG_TYPE_InstanceID</relatedStateVariable>"
"        </argument>"
"        <argument>"
"          <name>Actions</name>"
"          <direction>out</direction>"
"          <relatedStateVariable>CurrentTransportActions</relatedStateVariable>"
"        </argument>"
"      </argumentList>"
"    </action>"
"    <action>"
"      <name>GetDeviceCapabilities</name>"
"      <argumentList>"
"        <argument>"
"          <name>InstanceID</name>"
"          <direction>in</direction>"
"          <relatedStateVariable>A_ARG_TYPE_InstanceID</relatedStateVariable>"
"        </argument>"
"        <argument>"
"          <name>PlayMedia</name>"
"          <direction>out</direction>"
"          <relatedStateVariable>PossiblePlaybackStorageMedia</relatedStateVariable>"
"        </argument>"
"        <argument>"
"          <name>RecMedia</name>"
"          <direction>out</direction>"
"          <relatedStateVariable>PossibleRecordStorageMedia</relatedStateVariable>"
"        </argument>"
"        <argument>"
"          <name>RecQualityModes</name>"
"          <direction>out</direction>"
"          <relatedStateVariable>PossibleRecordQualityModes</relatedStateVariable>"
"        </argument>"
"      </argumentList>"
"    </action>"
"    <action>"
"      <name>GetMediaInfo</name>"
"      <argumentList>"
"        <argument>"
"          <name>InstanceID</name>"
"          <direction>in</direction>"
"          <relatedStateVariable>A_ARG_TYPE_InstanceID</relatedStateVariable>"
"        </argument>"
"        <argument>"
"          <name>NrTracks</name>"
"          <direction>out</direction>"
"          <relatedStateVariable>NumberOfTracks</relatedStateVariable>"
"        </argument>"
"        <argument>"
"          <name>MediaDuration</name>"
"          <direction>out</direction>"
"          <relatedStateVariable>CurrentMediaDuration</relatedStateVariable>"
"        </argument>"
"        <argument>"
"          <name>CurrentURI</name>"
"          <direction>out</direction>"
"          <relatedStateVariable>AVTransportURI</relatedStateVariable>"
"        </argument>"
"        <argument>"
"          <name>CurrentURIMetaData</name>"
"          <direction>out</direction>"
"          <relatedStateVariable>AVTransportURIMetaData</relatedStateVariable>"
"        </argument>"
"        <argument>"
"          <name>NextURI</name>"
"          <direction>out</direction>"
"          <relatedStateVariable>NextAVTransportURI</relatedStateVariable>"
"        </argument>"
"        <argument>"
"          <name>NextURIMetaData</name>"
"          <direction>out</direction>"
"          <relatedStateVariable>NextAVTransportURIMetaData</relatedStateVariable>"
"        </argument>"
"        <argument>"
"          <name>PlayMedium</name>"
"          <direction>out</direction>"
"          <relatedStateVariable>PlaybackStorageMedium</relatedStateVariable>"
"        </argument>"
"        <argument>"
"          <name>RecordMedium</name>"
"          <direction>out</direction>"
"          <relatedStateVariable>RecordStorageMedium</relatedStateVariable>"
"        </argument>"
"        <argument>"
"          <name>WriteStatus</name>"
"          <direction>out</direction>"
"          <relatedStateVariable>RecordMediumWriteStatus</relatedStateVariable>"
"        </argument>"
"      </argumentList>"
"    </action>"
"    <action>"
"      <name>GetPositionInfo</name>"
"      <argumentList>"
"        <argument>"
"          <name>InstanceID</name>"
"          <direction>in</direction>"
"          <relatedStateVariable>A_ARG_TYPE_InstanceID</relatedStateVariable>"
"        </argument>"
"        <argument>"
"          <name>Track</name>"
"          <direction>out</direction>"
"          <relatedStateVariable>CurrentTrack</relatedStateVariable>"
"        </argument>"
"        <argument>"
"          <name>TrackDuration</name>"
"          <direction>out</direction>"
"          <relatedStateVariable>CurrentTrackDuration</relatedStateVariable>"
"        </argument>"
"        <argument>"
"          <name>TrackMetaData</name>"
"          <direction>out</direction>"
"          <relatedStateVariable>CurrentTrackMetaData</relatedStateVariable>"
"        </argument>"
"        <argument>"
"          <name>TrackURI</name>"
"          <direction>out</direction>"
"          <relatedStateVariable>CurrentTrackURI</relatedStateVariable>"
"        </argument>"
"        <argument>"
"          <name>RelTime</name>"
"          <direction>out</direction>"
"          <relatedStateVariable>RelativeTimePosition</relatedStateVariable>"
"        </argument>"
"        <argument>"
"          <name>AbsTime</name>"
"          <direction>out</direction>"
"          <relatedStateVariable>AbsoluteTimePosition</relatedStateVariable>"
"        </argument>"
"        <argument>"
"          <name>RelCount</name>"
"          <direction>out</direction>"
"          <relatedStateVariable>RelativeCounterPosition</relatedStateVariable>"
"        </argument>"
"        <argument>"
"          <name>AbsCount</name>"
"          <direction>out</direction>"
"          <relatedStateVariable>AbsoluteCounterPosition</relatedStateVariable>"
"        </argument>"
"      </argumentList>"
"    </action>"
"    <action>"
"      <name>GetTransportInfo</name>"
"      <argumentList>"
"        <argument>"
"          <name>InstanceID</name>"
"          <direction>in</direction>"
"          <relatedStateVariable>A_ARG_TYPE_InstanceID</relatedStateVariable>"
"        </argument>"
"        <argument>"
"          <name>CurrentTransportState</name>"
"          <direction>out</direction>"
"          <relatedStateVariable>TransportState</relatedStateVariable>"
"        </argument>"
"        <argument>"
"          <name>CurrentTransportStatus</name>"
"          <direction>out</direction>"
"          <relatedStateVariable>TransportStatus</relatedStateVariable>"
"        </argument>"
"        <argument>"
"          <name>CurrentSpeed</name>"
"          <direction>out</direction>"
"          <relatedStateVariable>TransportPlaySpeed</relatedStateVariable>"
"        </argument>"
"      </argumentList>"
"    </action>"
"    <action>"
"      <name>GetTransportSettings</name>"
"      <argumentList>"
"        <argument>"
"          <name>InstanceID</name>"
"          <direction>in</direction>"
"          <relatedStateVariable>A_ARG_TYPE_InstanceID</relatedStateVariable>"
"        </argument>"
"        <argument>"
"          <name>PlayMode</name>"
"          <direction>out</direction>"
"          <relatedStateVariable>CurrentPlayMode</relatedStateVariable>"
"        </argument>"
"        <argument>"
"          <name>RecQualityMode</name>"
"          <direction>out</direction>"
"          <relatedStateVariable>CurrentRecordQualityMode</relatedStateVariable>"
"        </argument>"
"      </argumentList>"
"    </action>"
"    <action>"
"      <name>Next</name>"
"      <argumentList>"
"        <argument>"
"          <name>InstanceID</name>"
"          <direction>in</direction>"
"          <relatedStateVariable>A_ARG_TYPE_InstanceID</relatedStateVariable>"
"        </argument>"
"      </argumentList>"
"    </action>"
"    <action>"
"      <name>Pause</name>"
"      <argumentList>"
"        <argument>"
"          <name>InstanceID</name>"
"          <direction>in</direction>"
"          <relatedStateVariable>A_ARG_TYPE_InstanceID</relatedStateVariable>"
"        </argument>"
"      </argumentList>"
"    </action>"
"    <action>"
"      <name>Play</name>"
"      <argumentList>"
"        <argument>"
"          <name>InstanceID</name>"
"          <direction>in</direction>"
"          <relatedStateVariable>A_ARG_TYPE_InstanceID</relatedStateVariable>"
"        </argument>"
"        <argument>"
"          <name>Speed</name>"
"          <direction>in</direction>"
"          <relatedStateVariable>TransportPlaySpeed</relatedStateVariable>"
"        </argument>"
"      </argumentList>"
"    </action>"
"    <action>"
"      <name>Previous</name>"
"      <argumentList>"
"        <argument>"
"          <name>InstanceID</name>"
"          <direction>in</direction>"
"          <relatedStateVariable>A_ARG_TYPE_InstanceID</relatedStateVariable>"
"        </argument>"
"      </argumentList>"
"    </action>"
"    <action>"
"      <name>Seek</name>"
"      <argumentList>"
"        <argument>"
"          <name>InstanceID</name>"
"          <direction>in</direction>"
"          <relatedStateVariable>A_ARG_TYPE_InstanceID</relatedStateVariable>"
"        </argument>"
"        <argument>"
"          <name>Unit</name>"
"          <direction>in</direction>"
"          <relatedStateVariable>A_ARG_TYPE_SeekMode</relatedStateVariable>"
"        </argument>"
"        <argument>"
"          <name>Target</name>"
"          <direction>in</direction>"
"          <relatedStateVariable>A_ARG_TYPE_SeekTarget</relatedStateVariable>"
"        </argument>"
"      </argumentList>"
"    </action>"
"    <action>"
"      <name>SetAVTransportURI</name>"
"      <argumentList>"
"        <argument>"
"          <name>InstanceID</name>"
"          <direction>in</direction>"
"          <relatedStateVariable>A_ARG_TYPE_InstanceID</relatedStateVariable>"
"        </argument>"
"        <argument>"
"          <name>CurrentURI</name>"
"          <direction>in</direction>"
"          <relatedStateVariable>AVTransportURI</relatedStateVariable>"
"        </argument>"
"        <argument>"
"          <name>CurrentURIMetaData</name>"
"          <direction>in</direction>"
"          <relatedStateVariable>AVTransportURIMetaData</relatedStateVariable>"
"        </argument>"
"      </argumentList>"
"    </action>"
"    <action>"
"      <name>SetNextAVTransportURI</name>"
"      <argumentList>"
"        <argument>"
"          <name>InstanceID</name>"
"          <direction>in</direction>"
"          <relatedStateVariable>A_ARG_TYPE_InstanceID</relatedStateVariable>"
"        </argument>"
"        <argument>"
"          <name>NextURI</name>"
"          <direction>in</direction>"
"          <relatedStateVariable>NextAVTransportURI</relatedStateVariable>"
"        </argument>"
"        <argument>"
"          <name>NextURIMetaData</name>"
"          <direction>in</direction>"
"          <relatedStateVariable>NextAVTransportURIMetaData</relatedStateVariable>"
"        </argument>"
"      </argumentList>"
"    </action>"
"    <action>"
"      <name>SetPlayMode</name>"
"      <argumentList>"
"        <argument>"
"          <name>InstanceID</name>"
"          <direction>in</direction>"
"          <relatedStateVariable>A_ARG_TYPE_InstanceID</relatedStateVariable>"
"        </argument>"
"        <argument>"
"          <name>NewPlayMode</name>"
"          <direction>in</direction>"
"          <relatedStateVariable>CurrentPlayMode</relatedStateVariable>"
"        </argument>"
"      </argumentList>"
"    </action>"
"    <action>"
"      <name>Stop</name>"
"      <argumentList>"
"        <argument>"
"          <name>InstanceID</name>"
"          <direction>in</direction>"
"          <relatedStateVariable>A_ARG_TYPE_InstanceID</relatedStateVariable>"
"        </argument>"
"      </argumentList>"
"    </action>"
"  </actionList>"
"  <serviceStateTable>"
"    <stateVariable sendEvents=\"no\">"
"      <name>CurrentPlayMode</name>"
"      <dataType>string</dataType>"
"      <defaultValue>NORMAL</defaultValue>"
"      <allowedValueList>"
"        <allowedValue>NORMAL</allowedValue>"
"        <allowedValue>SHUFFLE</allowedValue>"
"        <allowedValue>REPEAT_ONE</allowedValue>"
"        <allowedValue>REPEAT_ALL</allowedValue>"
"        <allowedValue>DIRECT_1</allowedValue>"
"      </allowedValueList>"
"    </stateVariable>"
"    <stateVariable sendEvents=\"no\">"
"      <name>RecordStorageMedium</name>"
"      <dataType>string</dataType>"
"      <defaultValue>NOT_IMPLEMENTED</defaultValue>"
"      <allowedValueList>"
"        <allowedValue>NOT_IMPLEMENTED</allowedValue>"
"      </allowedValueList>"
"    </stateVariable>"
"    <stateVariable sendEvents=\"yes\">"
"      <name>LastChange</name>"
"      <dataType>string</dataType>"
"    </stateVariable>"
"    <stateVariable sendEvents=\"no\">"
"      <name>RelativeTimePosition</name>"
"      <dataType>string</dataType>"
"    </stateVariable>"
"    <stateVariable sendEvents=\"no\">"
"      <name>CurrentTrackURI</name>"
"      <dataType>string</dataType>"
"    </stateVariable>"
"    <stateVariable sendEvents=\"no\">"
"      <name>CurrentTrackDuration</name>"
"      <dataType>string</dataType>"
"    </stateVariable>"
"    <stateVariable sendEvents=\"no\">"
"      <name>CurrentRecordQualityMode</name>"
"      <dataType>string</dataType>"
"      <defaultValue>NOT_IMPLEMENTED</defaultValue>"
"      <allowedValueList>"
"        <allowedValue>NOT_IMPLEMENTED</allowedValue>"
"      </allowedValueList>"
"    </stateVariable>"
"    <stateVariable sendEvents=\"no\">"
"      <name>CurrentMediaDuration</name>"
"      <dataType>string</dataType>"
"    </stateVariable>"
"    <stateVariable sendEvents=\"no\">"
"      <name>AbsoluteCounterPosition</name>"
"      <dataType>i4</dataType>"
"    </stateVariable>"
"    <stateVariable sendEvents=\"no\">"
"      <name>RelativeCounterPosition</name>"
"      <dataType>i4</dataType>"
"    </stateVariable>"
"    <stateVariable sendEvents=\"no\">"
"      <name>A_ARG_TYPE_InstanceID</name>"
"      <dataType>ui4</dataType>"
"      <defaultValue>0</defaultValue>"
"    </stateVariable>"
"    <stateVariable sendEvents=\"no\">"
"      <name>AVTransportURI</name>"
"      <dataType>string</dataType>"
"    </stateVariable>"
"    <stateVariable sendEvents=\"no\">"
"      <name>CurrentTrackMetaData</name>"
"      <dataType>string</dataType>"
"    </stateVariable>"
"    <stateVariable sendEvents=\"no\">"
"      <name>AVTransportURIMetaData</name>"
"      <dataType>string</dataType>"
"    </stateVariable>"
"    <stateVariable sendEvents=\"no\">"
"      <name>TransportPlaySpeed</name>"
"      <dataType>string</dataType>"
"      <defaultValue>1</defaultValue>"
"      <allowedValueList>"
"        <allowedValue>1</allowedValue>"
"      </allowedValueList>"
"    </stateVariable>"
"    <stateVariable sendEvents=\"no\">"
"      <name>NextAVTransportURI</name>"
"      <dataType>string</dataType>"
"    </stateVariable>"
"    <stateVariable sendEvents=\"no\">"
"      <name>PossibleRecordQualityModes</name>"
"      <dataType>string</dataType>"
"      <defaultValue>NOT_IMPLEMENTED</defaultValue>"
"      <allowedValueList>"
"        <allowedValue>NOT_IMPLEMENTED</allowedValue>"
"      </allowedValueList>"
"    </stateVariable>"
"    <stateVariable sendEvents=\"no\">"
"      <name>PossibleRecordStorageMedia</name>"
"      <dataType>string</dataType>"
"      <defaultValue>NOT_IMPLEMENTED</defaultValue>"
"      <allowedValueList>"
"        <allowedValue>NOT_IMPLEMENTED</allowedValue>"
"      </allowedValueList>"
"    </stateVariable>"
"    <stateVariable sendEvents=\"no\">"
"      <name>AbsoluteTimePosition</name>"
"      <dataType>string</dataType>"
"    </stateVariable>"
"    <stateVariable sendEvents=\"no\">"
"      <name>NextAVTransportURIMetaData</name>"
"      <dataType>string</dataType>"
"    </stateVariable>"
"    <stateVariable sendEvents=\"no\">"
"      <name>PlaybackStorageMedium</name>"
"      <dataType>string</dataType>"
"      <defaultValue>NETWORK</defaultValue>"
"      <allowedValueList>"
"        <allowedValue>NETWORK</allowedValue>"
"      </allowedValueList>"
"    </stateVariable>"
"    <stateVariable sendEvents=\"no\">"
"      <name>CurrentTransportActions</name>"
"      <dataType>string</dataType>"
"      <allowedValueList>"
"        <allowedValue>PLAY</allowedValue>"
"        <allowedValue>STOP</allowedValue>"
"        <allowedValue>PAUSE</allowedValue>"
"        <allowedValue>SEEK</allowedValue>"
"        <allowedValue>NEXT</allowedValue>"
"        <allowedValue>PREVIOUS</allowedValue>"
"      </allowedValueList>"
"    </stateVariable>"
"    <stateVariable sendEvents=\"no\">"
"      <name>RecordMediumWriteStatus</name>"
"      <dataType>string</dataType>"
"      <defaultValue>NOT_IMPLEMENTED</defaultValue>"
"      <allowedValueList>"
"        <allowedValue>NOT_IMPLEMENTED</allowedValue>"
"      </allowedValueList>"
"    </stateVariable>"
"    <stateVariable sendEvents=\"no\">"
"      <name>PossiblePlaybackStorageMedia</name>"
"      <dataType>string</dataType>"
"      <defaultValue>NOT_IMPLEMENTED</defaultValue>"
"      <allowedValueList>"
"        <allowedValue>NOT_IMPLEMENTED</allowedValue>"
"      </allowedValueList>"
"    </stateVariable>"
"    <stateVariable sendEvents=\"no\">"
"      <name>TransportState</name>"
"      <dataType>string</dataType>"
"      <defaultValue>NO_MEDIA_PRESENT</defaultValue>"
"      <allowedValueList>"
"        <allowedValue>STOPPED</allowedValue>"
"        <allowedValue>PLAYING</allowedValue>"
"        <allowedValue>TRANSITIONING</allowedValue>"
"        <allowedValue>PAUSED_PLAYBACK</allowedValue>"
"        <allowedValue>NO_MEDIA_PRESENT</allowedValue>"
"      </allowedValueList>"
"    </stateVariable>"
"    <stateVariable sendEvents=\"no\">"
"      <name>NumberOfTracks</name>"
"      <dataType>ui4</dataType>"
"      <allowedValueRange>"
"        <minimum>0</minimum>"
"        <maximum>1</maximum>"
"      </allowedValueRange>"
"      <defaultValue>0</defaultValue>"
"    </stateVariable>"
"    <stateVariable sendEvents=\"no\">"
"      <name>A_ARG_TYPE_SeekMode</name>"
"      <dataType>string</dataType>"
"      <allowedValueList>"
"        <allowedValue>REL_TIME</allowedValue>"
"      </allowedValueList>"
"    </stateVariable>"
"    <stateVariable sendEvents=\"no\">"
"      <name>CurrentTrack</name>"
"      <dataType>ui4</dataType>"
"      <allowedValueRange>"
"        <minimum>0</minimum>"
"        <maximum>1</maximum>"
"        <step>1</step>"
"      </allowedValueRange>"
"      <defaultValue>0</defaultValue>"
"    </stateVariable>"
"    <stateVariable sendEvents=\"no\">"
"      <name>TransportStatus</name>"
"      <dataType>string</dataType>"
"      <defaultValue>OK</defaultValue>"
"      <allowedValueList>"
"        <allowedValue>OK</allowedValue>"
"        <allowedValue>ERROR_OCCURRED</allowedValue>"
"      </allowedValueList>"
"    </stateVariable>"
"    <stateVariable sendEvents=\"no\">"
"      <name>A_ARG_TYPE_SeekTarget</name>"
"      <dataType>string</dataType>"
"    </stateVariable>"
"  </serviceStateTable>"
"</scpd>";

static const char * cm_desc =
"  <?xml version=\"1.0\" encoding=\"utf-8\"?>"
"<scpd xmlns=\"urn:schemas-upnp-org:service-1-0\">"
"  <specVersion>"
"    <major>1</major>"
"    <minor>0</minor>"
"  </specVersion>"
"  <actionList>"
"    <action>"
"      <name>GetCurrentConnectionIDs</name>"
"      <argumentList>"
"        <argument>"
"          <name>ConnectionIDs</name>"
"          <direction>out</direction>"
"          <relatedStateVariable>CurrentConnectionIDs</relatedStateVariable>"
"        </argument>"
"      </argumentList>"
"    </action>"
"    <action>"
"      <name>GetCurrentConnectionInfo</name>"
"      <argumentList>"
"        <argument>"
"          <name>ConnectionID</name>"
"          <direction>in</direction>"
"          <relatedStateVariable>A_ARG_TYPE_ConnectionID</relatedStateVariable>"
"        </argument>"
"        <argument>"
"          <name>RcsID</name>"
"          <direction>out</direction>"
"          <relatedStateVariable>A_ARG_TYPE_RcsID</relatedStateVariable>"
"        </argument>"
"        <argument>"
"          <name>AVTransportID</name>"
"          <direction>out</direction>"
"          <relatedStateVariable>A_ARG_TYPE_AVTransportID</relatedStateVariable>"
"        </argument>"
"        <argument>"
"          <name>ProtocolInfo</name>"
"          <direction>out</direction>"
"          <relatedStateVariable>A_ARG_TYPE_ProtocolInfo</relatedStateVariable>"
"        </argument>"
"        <argument>"
"          <name>PeerConnectionManager</name>"
"          <direction>out</direction>"
"          <relatedStateVariable>A_ARG_TYPE_ConnectionManager</relatedStateVariable>"
"        </argument>"
"        <argument>"
"          <name>PeerConnectionID</name>"
"          <direction>out</direction>"
"          <relatedStateVariable>A_ARG_TYPE_ConnectionID</relatedStateVariable>"
"        </argument>"
"        <argument>"
"          <name>Direction</name>"
"          <direction>out</direction>"
"          <relatedStateVariable>A_ARG_TYPE_Direction</relatedStateVariable>"
"        </argument>"
"        <argument>"
"          <name>Status</name>"
"          <direction>out</direction>"
"          <relatedStateVariable>A_ARG_TYPE_ConnectionStatus</relatedStateVariable>"
"        </argument>"
"      </argumentList>"
"    </action>"
"    <action>"
"      <name>GetProtocolInfo</name>"
"      <argumentList>"
"        <argument>"
"          <name>Source</name>"
"          <direction>out</direction>"
"          <relatedStateVariable>SourceProtocolInfo</relatedStateVariable>"
"        </argument>"
"        <argument>"
"          <name>Sink</name>"
"          <direction>out</direction>"
"          <relatedStateVariable>SinkProtocolInfo</relatedStateVariable>"
"        </argument>"
"      </argumentList>"
"    </action>"
"  </actionList>"
"  <serviceStateTable>"
"    <stateVariable sendEvents=\"no\">"
"      <name>A_ARG_TYPE_ProtocolInfo</name>"
"      <dataType>string</dataType>"
"    </stateVariable>"
"    <stateVariable sendEvents=\"no\">"
"      <name>A_ARG_TYPE_ConnectionStatus</name>"
"      <dataType>string</dataType>"
"      <allowedValueList>"
"        <allowedValue>OK</allowedValue>"
"        <allowedValue>ContentFormatMismatch</allowedValue>"
"        <allowedValue>InsufficientBandwidth</allowedValue>"
"        <allowedValue>UnreliableChannel</allowedValue>"
"        <allowedValue>Unknown</allowedValue>"
"      </allowedValueList>"
"    </stateVariable>"
"    <stateVariable sendEvents=\"no\">"
"      <name>A_ARG_TYPE_AVTransportID</name>"
"      <dataType>i4</dataType>"
"      <defaultValue>-1</defaultValue>"
"    </stateVariable>"
"    <stateVariable sendEvents=\"no\">"
"      <name>A_ARG_TYPE_RcsID</name>"
"      <dataType>i4</dataType>"
"      <defaultValue>-1</defaultValue>"
"    </stateVariable>"
"    <stateVariable sendEvents=\"no\">"
"      <name>A_ARG_TYPE_ConnectionID</name>"
"      <dataType>i4</dataType>"
"      <defaultValue>0</defaultValue>"
"    </stateVariable>"
"    <stateVariable sendEvents=\"no\">"
"      <name>A_ARG_TYPE_ConnectionManager</name>"
"      <dataType>string</dataType>"
"    </stateVariable>"
"    <stateVariable sendEvents=\"yes\">"
"      <name>SourceProtocolInfo</name>"
"      <dataType>string</dataType>"
"    </stateVariable>"
"    <stateVariable sendEvents=\"yes\">"
"      <name>SinkProtocolInfo</name>"
"      <dataType>string</dataType>"
"    </stateVariable>"
"    <stateVariable sendEvents=\"no\">"
"      <name>A_ARG_TYPE_Direction</name>"
"      <dataType>string</dataType>"
"      <allowedValueList>"
"        <allowedValue>Output</allowedValue>"
"        <allowedValue>Input</allowedValue>"
"      </allowedValueList>"
"    </stateVariable>"
"    <stateVariable sendEvents=\"yes\">"
"      <name>CurrentConnectionIDs</name>"
"      <dataType>string</dataType>"
"    </stateVariable>"
"  </serviceStateTable>"
"</scpd>";

static const char * rc_desc =
"<?xml version=\"1.0\" encoding=\"utf-8\"?>"
"<scpd xmlns=\"urn:schemas-upnp-org:service-1-0\">"
"  <specVersion>"
"    <major>1</major>"
"    <minor>0</minor>"
"  </specVersion>"
"  <actionList>"
"    <action>"
"      <name>GetMute</name>"
"      <argumentList>"
"        <argument>"
"          <name>InstanceID</name>"
"          <direction>in</direction>"
"          <relatedStateVariable>A_ARG_TYPE_InstanceID</relatedStateVariable>"
"        </argument>"
"        <argument>"
"          <name>Channel</name>"
"          <direction>in</direction>"
"          <relatedStateVariable>A_ARG_TYPE_Channel</relatedStateVariable>"
"        </argument>"
"        <argument>"
"          <name>CurrentMute</name>"
"          <direction>out</direction>"
"          <relatedStateVariable>Mute</relatedStateVariable>"
"        </argument>"
"      </argumentList>"
"    </action>"
"    <action>"
"      <name>GetVolume</name>"
"      <argumentList>"
"        <argument>"
"          <name>InstanceID</name>"
"          <direction>in</direction>"
"          <relatedStateVariable>A_ARG_TYPE_InstanceID</relatedStateVariable>"
"        </argument>"
"        <argument>"
"          <name>Channel</name>"
"          <direction>in</direction>"
"          <relatedStateVariable>A_ARG_TYPE_Channel</relatedStateVariable>"
"        </argument>"
"        <argument>"
"          <name>CurrentVolume</name>"
"          <direction>out</direction>"
"          <relatedStateVariable>Volume</relatedStateVariable>"
"        </argument>"
"      </argumentList>"
"    </action>"
"    <action>"
"      <name>GetVolumeDB</name>"
"      <argumentList>"
"        <argument>"
"          <name>InstanceID</name>"
"          <direction>in</direction>"
"          <relatedStateVariable>A_ARG_TYPE_InstanceID</relatedStateVariable>"
"        </argument>"
"        <argument>"
"          <name>Channel</name>"
"          <direction>in</direction>"
"          <relatedStateVariable>A_ARG_TYPE_Channel</relatedStateVariable>"
"        </argument>"
"        <argument>"
"          <name>CurrentVolume</name>"
"          <direction>out</direction>"
"          <relatedStateVariable>VolumeDB</relatedStateVariable>"
"        </argument>"
"      </argumentList>"
"    </action>"
"    <action>"
"      <name>ListPresets</name>"
"      <argumentList>"
"        <argument>"
"          <name>InstanceID</name>"
"          <direction>in</direction>"
"          <relatedStateVariable>A_ARG_TYPE_InstanceID</relatedStateVariable>"
"        </argument>"
"        <argument>"
"          <name>CurrentPresetNameList</name>"
"          <direction>out</direction>"
"          <relatedStateVariable>PresetNameList</relatedStateVariable>"
"        </argument>"
"      </argumentList>"
"    </action>"
"    <action>"
"      <name>SelectPreset</name>"
"      <argumentList>"
"        <argument>"
"          <name>InstanceID</name>"
"          <direction>in</direction>"
"          <relatedStateVariable>A_ARG_TYPE_InstanceID</relatedStateVariable>"
"        </argument>"
"        <argument>"
"          <name>PresetName</name>"
"          <direction>in</direction>"
"          <relatedStateVariable>A_ARG_TYPE_PresetName</relatedStateVariable>"
"        </argument>"
"      </argumentList>"
"    </action>"
"    <action>"
"      <name>SetMute</name>"
"      <argumentList>"
"        <argument>"
"          <name>InstanceID</name>"
"          <direction>in</direction>"
"          <relatedStateVariable>A_ARG_TYPE_InstanceID</relatedStateVariable>"
"        </argument>"
"        <argument>"
"          <name>Channel</name>"
"          <direction>in</direction>"
"          <relatedStateVariable>A_ARG_TYPE_Channel</relatedStateVariable>"
"        </argument>"
"        <argument>"
"          <name>DesiredMute</name>"
"          <direction>in</direction>"
"          <relatedStateVariable>Mute</relatedStateVariable>"
"        </argument>"
"      </argumentList>"
"    </action>"
"    <action>"
"      <name>SetVolume</name>"
"      <argumentList>"
"        <argument>"
"          <name>InstanceID</name>"
"          <direction>in</direction>"
"          <relatedStateVariable>A_ARG_TYPE_InstanceID</relatedStateVariable>"
"        </argument>"
"        <argument>"
"          <name>Channel</name>"
"          <direction>in</direction>"
"          <relatedStateVariable>A_ARG_TYPE_Channel</relatedStateVariable>"
"        </argument>"
"        <argument>"
"          <name>DesiredVolume</name>"
"          <direction>in</direction>"
"          <relatedStateVariable>Volume</relatedStateVariable>"
"        </argument>"
"      </argumentList>"
"    </action>"
"    <action>"
"      <name>SetVolumeDB</name>"
"      <argumentList>"
"        <argument>"
"          <name>InstanceID</name>"
"          <direction>in</direction>"
"          <relatedStateVariable>A_ARG_TYPE_InstanceID</relatedStateVariable>"
"        </argument>"
"        <argument>"
"          <name>Channel</name>"
"          <direction>in</direction>"
"          <relatedStateVariable>A_ARG_TYPE_Channel</relatedStateVariable>"
"        </argument>"
"        <argument>"
"          <name>DesiredVolume</name>"
"          <direction>in</direction>"
"          <relatedStateVariable>VolumeDB</relatedStateVariable>"
"        </argument>"
"      </argumentList>"
"    </action>"
"  </actionList>"
"  <serviceStateTable>"
"    <stateVariable sendEvents=\"no\">"
"      <name>A_ARG_TYPE_PresetName</name>"
"      <dataType>string</dataType>"
"      <defaultValue>FactoryDefaults</defaultValue>"
"      <allowedValueList>"
"        <allowedValue>FactoryDefaults</allowedValue>"
"      </allowedValueList>"
"    </stateVariable>"
"    <stateVariable sendEvents=\"no\">"
"      <name>A_ARG_TYPE_InstanceID</name>"
"      <dataType>ui4</dataType>"
"      <defaultValue>0</defaultValue>"
"    </stateVariable>"
"    <stateVariable sendEvents=\"no\">"
"      <name>A_ARG_TYPE_Channel</name>"
"      <dataType>string</dataType>"
"      <defaultValue>Master</defaultValue>"
"      <allowedValueList>"
"        <allowedValue>Master</allowedValue>"
"      </allowedValueList>"
"    </stateVariable>"
"    <stateVariable sendEvents=\"no\">"
"      <name>PresetNameList</name>"
"      <dataType>string</dataType>"
"    </stateVariable>"
"    <stateVariable sendEvents=\"no\">"
"      <name>Mute</name>"
"      <dataType>boolean</dataType>"
"      <defaultValue>0</defaultValue>"
"    </stateVariable>"
"    <stateVariable sendEvents=\"yes\">"
"      <name>LastChange</name>"
"      <dataType>string</dataType>"
"    </stateVariable>"
"    <stateVariable sendEvents=\"no\">"
"      <name>Volume</name>"
"      <dataType>ui2</dataType>"
"      <allowedValueRange>"
"        <minimum>0</minimum>"
"        <maximum>" STR(BG_PLAYER_VOLUME_INT_MAX)"</maximum>"
"        <step>1</step>"
"      </allowedValueRange>"
"    </stateVariable>"
"    <stateVariable sendEvents=\"no\">"
"      <name>VolumeDB</name>"
"      <dataType>i2</dataType>"
"      <allowedValueRange>"
"        <minimum>-32767</minimum>"
"        <maximum>0</maximum>"
"        <step>1</step>"
"      </allowedValueRange>"
"    </stateVariable>"
"  </serviceStateTable>"
"</scpd>";
