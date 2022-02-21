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


#include <gmerlin/parameter.h>
#include <gmerlin/msgqueue.h>
#include <gmerlin/player.h>
#include <gmerlin/playermsg.h>
#include <upnp/devicepriv.h>
#include <upnp/mediarenderer.h>
#include <upnp/didl.h>

#include <gavl/utils.h>
#include <gavl/metatags.h>

#include <stdlib.h>
#include <string.h>


static void destroy_func(void * data)
  {
  bg_mediarenderer_t * priv = data;
  gavl_dictionary_free(&priv->metadata);

  if(priv->control_id)
    free(priv->control_id);
  
  free(priv);
  }

static int msg_callback(void * data, gavl_msg_t * msg)
  {
  bg_upnp_service_t * avt;
  bg_upnp_service_t * rc;
  bg_upnp_device_t * dev = data;
  bg_mediarenderer_t * priv = dev->priv;
  
  avt = &dev->services[SERVICE_AVTRANSPORT];
  rc = &dev->services[SERVICE_RENDERINGCONTROL];

  //  fprintf(stderr, "Got message from player\n"); 
  //  bg_msg_dump(msg, 2);
  
  /* Send 1:1 to websocket clients */
  //  bg_websocket_send_message(priv->ws, msg);

  switch(msg->NS)
    {
    case GAVL_MSG_NS_SRC:
      switch(msg->ID)
        {
        }
      break;
    case BG_MSG_NS_STATE:
      switch(gavl_msg_get_id(msg))
        {
        case BG_MSG_STATE_CHANGED:
          {
          gavl_value_t val;
          const char * ctx;
          const char * var;
          int last = 0;
          
          gavl_value_init(&val);

          bg_msg_get_state(msg,
                           &last,
                           &ctx,
                           &var,
                           &val, NULL);

          if(!strcmp(ctx, BG_PLAYER_STATE_CTX))
            {
            if(!strcmp(var, BG_PLAYER_STATE_VOLUME))     // float
              {
              double f;
              if(gavl_value_get_float(&val, &f))
                bg_upnp_rendering_control_set_volume(rc, f);
              }
            else if(!strcmp(var, BG_PLAYER_STATE_STATUS))       // int
              {
              bg_upnp_sv_t * tstate;  // Playing etc
              bg_upnp_sv_t * tstatus; // Ok/Error
              bg_upnp_sv_t * actions; // CurrentTransportActions

              if(!gavl_value_get_int(&val, &priv->state))
                return 1;
              
              tstate = bg_upnp_service_desc_sv_by_name(&avt->desc, "TransportState");
              tstatus = bg_upnp_service_desc_sv_by_name(&avt->desc, "TransportStatus");
              actions = bg_upnp_service_desc_sv_by_name(&avt->desc, "CurrentTransportActions");
      
              switch(priv->state)
                {
                case BG_PLAYER_STATUS_INIT:      //!< Initializing
                  fprintf(stderr, "Player init\n");
                  break;
                case BG_PLAYER_STATUS_STOPPED:   //!< Stopped, waiting for play command
                  fprintf(stderr, "Player stopped\n");

                  if(strcmp(tstate->value.s, "NO_MEDIA_PRESENT"))
                    bg_upnp_sv_set_string(tstate, "STOPPED");
          
                  bg_upnp_sv_set_string(tstatus, "OK");

                  break;
                case BG_PLAYER_STATUS_PLAYING:   //!< Playing
                  {
                  char * transport_actions;
          
                  //          fprintf(stderr, "Player playing\n");

                  bg_upnp_sv_set_string(tstate, "PLAYING");
                  bg_upnp_sv_set_string(tstatus, "OK");

                  transport_actions = gavl_strdup("Stop");
                  if(priv->can_pause)
                    transport_actions = gavl_strcat(transport_actions, ",Pause");
                  if(priv->can_seek)
                    transport_actions = gavl_strcat(transport_actions, ",Seek");

                  bg_upnp_sv_set_string(actions, transport_actions);
                  free(transport_actions);
                  }
          
                  break;
                case BG_PLAYER_STATUS_SEEKING:   //!< Seeking
                  bg_upnp_sv_set_string(tstate, "TRANSITIONING");
                  bg_upnp_sv_set_string(tstatus, "OK");
                  break;
                case BG_PLAYER_STATUS_CHANGING:  //!< Changing the track
                  bg_upnp_sv_set_string(tstate, "TRANSITIONING");
                  bg_upnp_sv_set_string(tstatus, "OK");
                  break;
                case BG_PLAYER_STATUS_PAUSED:    //!< Paused
                  bg_upnp_sv_set_string(tstate, "PAUSED_PLAYBACK");
                  bg_upnp_sv_set_string(tstatus, "OK");
                  break;
                case BG_PLAYER_STATUS_STARTING:  //!< Starting playback
                  bg_upnp_sv_set_string(tstate, "TRANSITIONING");
                  bg_upnp_sv_set_string(tstatus, "OK");
                  break;
                case BG_PLAYER_STATUS_ERROR:     //!< Error
                  bg_upnp_sv_set_string(tstatus, "ERROR_OCCURRED");
                  break;
                }
              }
            else if(!strcmp(var, BG_PLAYER_STATE_CURRENT_TRACK))         // dictionary
              {
              const gavl_dictionary_t * m;
              const gavl_dictionary_t * dict;
              bg_upnp_sv_t * sv;
              char * str;
              const char * location = NULL;
              int idx = 0;
              gavl_time_t t;
              char buf[TIME_STRING_LEN];

              if(!(dict = gavl_value_get_dictionary(&val)) ||
                 !(m = gavl_track_get_metadata(dict)))
                return 1;
              
              gavl_dictionary_free(&priv->metadata);
              gavl_dictionary_init(&priv->metadata);

              gavl_dictionary_copy(&priv->metadata, m);
              
              t = GAVL_TIME_UNDEFINED;
              gavl_dictionary_get_long(&priv->metadata, GAVL_META_APPROX_DURATION, &t);
              bg_upnp_avtransport_print_time(buf, t);
      
              sv = bg_upnp_service_desc_sv_by_name(&avt->desc, "CurrentTrackDuration");
              bg_upnp_sv_set_string(sv, buf);

              sv = bg_upnp_service_desc_sv_by_name(&avt->desc, "CurrentMediaDuration");
              bg_upnp_sv_set_string(sv, buf);

              priv->can_seek = 0;
              gavl_dictionary_get_int(&priv->metadata,
                                      GAVL_META_CAN_SEEK, &priv->can_seek);

              priv->can_pause = 0;
              gavl_dictionary_get_int(&priv->metadata, GAVL_META_CAN_PAUSE, &priv->can_pause);
          
              if(gavl_dictionary_get_int(&priv->metadata, GAVL_META_SRCIDX, &idx) && 
                 gavl_dictionary_get_src(&priv->metadata, GAVL_META_SRC, idx,
                                         NULL, &location))
                {
                sv = bg_upnp_service_desc_sv_by_name(&avt->desc, "CurrentTrackURI");
                bg_upnp_sv_set_string(sv, location);

                sv = bg_upnp_service_desc_sv_by_name(&avt->desc, "AVTransportURI");
                bg_upnp_sv_set_string(sv, location);
                }
          

              if(!priv->metadata.num_entries)
                {
                str = calloc(1, 1);
                }
              else
                {
                xmlDocPtr doc;
                doc = bg_didl_create();
                bg_metadata_to_didl(doc, &priv->metadata, NULL);
                
                str = bg_xml_save_to_memory_opt(doc, XML_SAVE_NO_DECL);
                fprintf(stderr, "CurrentTrackMetaData: %s\n", str);
                if(doc)
                  xmlFreeDoc(doc);
                }
      
              if(str)
                {
                sv = bg_upnp_service_desc_sv_by_name(&avt->desc, "CurrentTrackMetaData");
                bg_upnp_sv_set_string(sv, str);

                sv = bg_upnp_service_desc_sv_by_name(&avt->desc, "AVTransportURIMetaData");
                bg_upnp_sv_set_string(sv, str);

                free(str);
                }

              }
            else if(!strcmp(var, BG_PLAYER_STATE_TIME))          // long
              {
              bg_upnp_sv_t * sv;
              char buf[TIME_STRING_LEN];

              if(!gavl_value_get_long(&val, &priv->time))
                return 1;
              bg_upnp_avtransport_print_time(buf, priv->time);
              
              sv = bg_upnp_service_desc_sv_by_name(&avt->desc, "RelativeTimePosition");
              bg_upnp_sv_set_string(sv, buf);
              
              sv = bg_upnp_service_desc_sv_by_name(&avt->desc, "AbsoluteTimePosition");
              bg_upnp_sv_set_string(sv, buf);
              
              }
            else if(!strcmp(var, BG_PLAYER_STATE_TIME_REM))      // long
              {
              
              }
            else if(!strcmp(var, BG_PLAYER_STATE_TIME_REM_ABS))  // long
              {
              
              }
            else if(!strcmp(var, BG_PLAYER_STATE_TIME_ABS))      // long
              {
              
              }
            else if(!strcmp(var, BG_PLAYER_STATE_MODE))          // int
              {
              
              }
            else if(!strcmp(var, BG_PLAYER_STATE_MUTE))          // int
              {
              int mute;
              bg_upnp_sv_t * sv;

              if(!gavl_value_get_int(&val, &mute))
                return 1;
              sv = bg_upnp_service_desc_sv_by_name(&rc->desc, "Mute");
              bg_upnp_sv_set_uint(sv, mute);
              }
            

            }

          }
          break;
        }
      break; 
    case BG_MSG_NS_PLAYER:
        switch(gavl_msg_get_id(msg))
          {
#if 0
          case BG_PLAYER_MSG_VOLUME_CHANGED:
            break;
#endif
#if 0
          case BG_PLAYER_MSG_NEXT_LOCATION:
            {
            xmlDocPtr doc = NULL;
            bg_upnp_sv_t * sv;
            char * str;
            gavl_dictionary_t m;
            gavl_dictionary_init(&m);
      
            /* NextTransportURI */
            str = gavl_msg_get_arg_string(msg, 0);
            if(str)
              {
              sv = bg_upnp_service_desc_sv_by_name(&avt->desc, "NextAVTransportURI");
              bg_upnp_sv_set_string(sv, str);
              free(str);
              }

            /* NextAVTransportURIMetaData */

            gavl_msg_get_arg_dictionary(msg, 1, &m);
      
            doc = bg_didl_create(0);
            bg_metadata_to_didl(doc, &m, 0);
            str = bg_xml_save_to_memory_opt(doc, XML_SAVE_NO_DECL);
      
            if(str)
              {
              sv = bg_upnp_service_desc_sv_by_name(&avt->desc, "NextAVTransportURIMetaData");
              bg_upnp_sv_set_string(sv, str);
              free(str);
              }
            if(doc)
              xmlFreeDoc(doc);
            gavl_dictionary_free(&m);
            }
          break;
#endif            
          }
        break;
    }
  return 1;
  }

static int ping_func(bg_upnp_device_t * dev)
  {
  bg_msg_sink_iteration(dev->ctrl.evt_sink);
  return bg_msg_sink_get_num(dev->ctrl.evt_sink);
  }

static const bg_upnp_device_info_t info =
  {
    .upnp_type = "MediaRenderer",
    .version = 1,
    .model_description = "Gmerlin media renderer",
    .model_name        = "Gmerlin media renderer",
    .num_services      = 3,
    .type              = BG_REMOTE_DEVICE_RENDERER,
  };

bg_upnp_device_t *
bg_upnp_create_media_renderer(bg_http_server_t * srv,
                              uuid_t uuid,
                              const char * name,
                              const bg_upnp_icon_t * icons,
                              int do_audio, int do_video)
  {
  bg_mediarenderer_t * priv;
  bg_upnp_device_t * ret;
  char uuid_str[37];

  ret = calloc(1, sizeof(*ret));

  priv = calloc(1, sizeof(*priv));

  uuid_unparse(uuid, uuid_str);

  priv->control_id = gavl_strdup(uuid_str);
  
  bg_control_init(&ret->ctrl, bg_msg_sink_create(msg_callback, ret, 0));
  
  priv->do_audio = do_audio;
  priv->do_video = do_video;
  
  ret->destroy = destroy_func;
  ret->priv = priv;
  
  ret->ping = ping_func;
  
  bg_upnp_device_init(ret, srv, uuid, name, &info, icons);
  bg_upnp_service_init_connection_manager(&ret->services[SERVICE_CONNECTION_MANAGER],
                                          do_audio, do_video);
  bg_upnp_service_init_avtransport(&ret->services[SERVICE_AVTRANSPORT]);
  bg_upnp_service_init_rendering_control(&ret->services[SERVICE_RENDERINGCONTROL]);
  
  bg_upnp_device_create_common(ret);
  return ret;
  }

