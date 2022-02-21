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

//#include <upnp/servicepriv.h>

#include <upnp/devicepriv.h>
#include <upnp/mediaserver.h>
#include <upnp/mediarenderer.h>
#include <gmerlin/utils.h>




#include <gavl/utils.h>

/* Service actions */

#define ARG_Source                1
#define ARG_Sink                  2
#define ARG_ConnectionIDs         3
#define ARG_ConnectionID          4

#define ARG_RcsID                 5
#define ARG_AVTransportID         6
#define ARG_ProtocolInfo          7
#define ARG_PeerConnectionManager 8
#define ARG_PeerConnectionID      9
#define ARG_Direction             10
#define ARG_Status                11

static const char * sink_protocol_info_audio = 
  "http-get:*:audio/L16;rate=44100;channels=1:DLNA.ORG_PN=LPCM,"
  "http-get:*:audio/L16;rate=44100;channels=2:DLNA.ORG_PN=LPCM,"
  "http-get:*:audio/L16;rate=48000;channels=1:DLNA.ORG_PN=LPCM,"
  "http-get:*:audio/L16;rate=48000;channels=2:DLNA.ORG_PN=LPCM,"
  "http-get:*:audio/x-ms-wma:DLNA.ORG_PN=WMABASE,"
  "http-get:*:audio/x-ms-wma:DLNA.ORG_PN=WMAFULL,"
  "http-get:*:audio/mpeg:DLNA.ORG_PN=MP3,"
  "http-get:*:audio/mpeg:DLNA.ORG_PN=MP3X,"
  "http-get:*:audio/L16:*,"
  "http-get:*:audio/L16;rate=11000;channels=1:*,"
  "http-get:*:audio/L16;rate=11000;channels=2:*,"
  "http-get:*:audio/L16;rate=11025;channels=1:*,"
  "http-get:*:audio/L16;rate=11025;channels=2:*,"
  "http-get:*:audio/L16;rate=22000;channels=1:*,"
  "http-get:*:audio/L16;rate=22000;channels=2:*,"
  "http-get:*:audio/L16;rate=22050;channels=1:*,"
  "http-get:*:audio/L16;rate=22050;channels=2:*,"
  "http-get:*:audio/L16;rate=44100;channels=4:*,"
  "http-get:*:audio/L16;rate=44100;channels=6:*,"
  "http-get:*:audio/L16;rate=44100;channels=8:*,"
  "http-get:*:audio/L16;rate=8000;channels=1:*,"
  "http-get:*:audio/L16;rate=8000;channels=2:*,"
  "http-get:*:audio/L8:*,"
  "http-get:*:audio/L8;rate=11000;channels=1:*,"
  "http-get:*:audio/L8;rate=11000;channels=2:*,"
  "http-get:*:audio/L8;rate=11025;channels=1:*,"
  "http-get:*:audio/L8;rate=11025;channels=2:*,"
  "http-get:*:audio/L8;rate=22000;channels=1:*,"
  "http-get:*:audio/L8;rate=22000;channels=2:*,"
  "http-get:*:audio/L8;rate=22050;channels=1:*,"
  "http-get:*:audio/L8;rate=22050;channels=2:*,"
  "http-get:*:audio/L8;rate=44100;channels=1:*,"
  "http-get:*:audio/L8;rate=44100;channels=2:*,"
  "http-get:*:audio/L8;rate=48000;channels=1:*,"
  "http-get:*:audio/L8;rate=48000;channels=2:*,"
  "http-get:*:audio/L8;rate=8000;channels=1:*,"
  "http-get:*:audio/L8;rate=8000;channels=2:*,"
  "http-get:*:audio/flac:*,"
  "http-get:*:audio/x-flac:*,"
  "http-get:*:audio/wav:*,"
  "http-get:*:audio/x-wav:*,"
  "http-get:*:audio/x-ms-wma:*,"
  "http-get:*:audio/mpeg:*,"
  "http-get:*:audio/mp4:*,"
  "http-get:*:audio/m4a:*,"
  "http-get:*:audio/x-m4a:*,"
  "http-get:*:audio/m4b:*,"
  "http-get:*:audio/x-m4b:*,"
  "http-get:*:audio/x-aac:*,"
  "http-get:*:audio/x-aacp:*,"
  "http-get:*:audio/ogg:*,"
  "http-get:*:audio/ogg; codecs=vorbis:*,"
  "http-get:*:audio/ogg; codecs=flac:*,"
  "http-get:*:audio/gavf:*,"
  "http-get:*:audio/3gpp:*,"
  "http-get:*:audio/3gpp2:*";

static const char * sink_protocol_info_video =
  "http-get:*:video/MP2P:*,"
  "http-get:*:video/mp4:*,"
  "http-get:*:video/mpeg:*,"
  "http-get:*:video/quicktime:*,"
  "http-get:*:video/x-flv:*,"
  "http-get:*:video/x-matroska:*,"
  "http-get:*:video/x-mkv:*,"
  "http-get:*:video/x-msvideo:*,"
  "http-get:*:video/x-ms-wmv:*,"
  "http-get:*:video/x-ms-asf:*,"
  "http-get:*:video/avi:*,"
  "http-get:*:video/x-avi:,*"
  "http-get:*:video/x-xvid:,*"
  "http-get:*:video/vnd.dlna.mpeg-tts:*,"
  "http-get:*:video/webm:*,"
  "http-get:*:video/3gpp:*,"
  "http-get:*:video/3gpp2:*,"
  "http-get:*:video/gavf:*";

/* GetProtocolInfo (media server variant) */
static int GetProtocolInfo(bg_upnp_service_t * s)
  {
  bg_upnp_service_set_arg_out(&s->req, ARG_Source);
  bg_upnp_service_set_arg_out(&s->req, ARG_Sink);
  return 1;
  }

static int GetCurrentConnectionIDs(bg_upnp_service_t * s)
  {
  bg_upnp_service_set_arg_out_string(&s->req, ARG_ConnectionIDs, calloc(1, 1));
  return 1;
  }

static int GetCurrentConnectionInfo(bg_upnp_service_t * s)
  {
  bg_upnp_service_set_arg_out_string(&s->req, ARG_RcsID, gavl_strdup("0"));
  bg_upnp_service_set_arg_out_string(&s->req, ARG_AVTransportID, gavl_strdup("0"));
  bg_upnp_service_set_arg_out_string(&s->req, ARG_ProtocolInfo, calloc(1, 1));
  bg_upnp_service_set_arg_out_string(&s->req, ARG_PeerConnectionManager, calloc(1, 1));
  bg_upnp_service_set_arg_out_string(&s->req, ARG_PeerConnectionManager, gavl_strdup("-1"));
  bg_upnp_service_set_arg_out_string(&s->req, ARG_Direction, gavl_strdup("Input"));
  bg_upnp_service_set_arg_out_string(&s->req, ARG_Status, gavl_strdup("Unknown"));
  return 1;
  }

/* Initialize service description */

static void init_service_desc(bg_upnp_service_desc_t * d, int do_audio, int do_video)
  {
  bg_upnp_sv_val_t  val;
  bg_upnp_sv_t * sv;
  bg_upnp_sa_desc_t * sa;

  /* State variables */
  
  sv = bg_upnp_service_desc_add_sv(d, "SourceProtocolInfo",
                                   BG_UPNP_SV_EVENT, BG_UPNP_SV_STRING);
  bg_upnp_sv_set_string(sv, "");
  
  sv = bg_upnp_service_desc_add_sv(d, "SinkProtocolInfo",
                                   BG_UPNP_SV_EVENT, BG_UPNP_SV_STRING);

  if(do_audio && do_video)
    {
    char * tmp_string = bg_sprintf("%s,%s", sink_protocol_info_audio,
                                   sink_protocol_info_video);
    bg_upnp_sv_set_string(sv, tmp_string);
    free(tmp_string);
    }
  else if(do_audio)
    bg_upnp_sv_set_string(sv, sink_protocol_info_audio);
  else if(do_video)
    bg_upnp_sv_set_string(sv, sink_protocol_info_video);
  else
    bg_upnp_sv_set_string(sv, "");
  
  bg_upnp_service_desc_add_sv(d, "CurrentConnectionIDs",
                              BG_UPNP_SV_EVENT, BG_UPNP_SV_STRING);
  
  sv = bg_upnp_service_desc_add_sv(d, "A_ARG_TYPE_ConnectionStatus",
                                   BG_UPNP_SV_ARG_ONLY,
                                   BG_UPNP_SV_STRING);
  
  val.s = "OK";
  bg_upnp_sv_add_allowed(sv, &val);
  val.s = "ContentFormatMismatch";
  bg_upnp_sv_add_allowed(sv, &val);
  val.s = "InsufficientBandwidth";
  bg_upnp_sv_add_allowed(sv, &val);
  val.s = "UnreliableChannel";
  bg_upnp_sv_add_allowed(sv, &val);
  val.s = "Unknown";
  bg_upnp_sv_add_allowed(sv, &val);

  bg_upnp_service_desc_add_sv(d, "A_ARG_TYPE_ConnectionManager",
                              BG_UPNP_SV_ARG_ONLY,
                              BG_UPNP_SV_STRING);
  
  sv = bg_upnp_service_desc_add_sv(d, "A_ARG_TYPE_Direction",
                                   BG_UPNP_SV_ARG_ONLY,
                                   BG_UPNP_SV_STRING);
  
  val.s = "Output";
  bg_upnp_sv_add_allowed(sv, &val);
  val.s = "Input";
  bg_upnp_sv_add_allowed(sv, &val);

  bg_upnp_service_desc_add_sv(d, "A_ARG_TYPE_ProtocolInfo",
                              BG_UPNP_SV_ARG_ONLY,
                              BG_UPNP_SV_STRING);
  
  bg_upnp_service_desc_add_sv(d, "A_ARG_TYPE_ConnectionID",
                              BG_UPNP_SV_ARG_ONLY,
                              BG_UPNP_SV_INT4);
  bg_upnp_service_desc_add_sv(d, "A_ARG_TYPE_AVTransportID",
                              BG_UPNP_SV_ARG_ONLY,
                              BG_UPNP_SV_INT4);
  bg_upnp_service_desc_add_sv(d, "A_ARG_TYPE_RcsID",
                              BG_UPNP_SV_ARG_ONLY,
                              BG_UPNP_SV_INT4);

  /* Actions */

  sa = bg_upnp_service_desc_add_action(d, "GetProtocolInfo", GetProtocolInfo);
  
  bg_upnp_sa_desc_add_arg_out(sa, "Source",
                              "SourceProtocolInfo", 0, ARG_Source);
  bg_upnp_sa_desc_add_arg_out(sa, "Sink",
                              "SinkProtocolInfo", 0, ARG_Sink);
  
  /*
  sa = bg_upnp_service_desc_add_action(d, "PrepareForConnection");
  sa = bg_upnp_service_desc_add_action(d, "ConnectionComplete");
  */

  sa = bg_upnp_service_desc_add_action(d, "GetCurrentConnectionIDs",
                                       GetCurrentConnectionIDs);
  bg_upnp_sa_desc_add_arg_out(sa, "ConnectionIDs",
                              "CurrentConnectionIDs", 0,
                              ARG_ConnectionIDs);
  
  sa = bg_upnp_service_desc_add_action(d, "GetCurrentConnectionInfo",
                                       GetCurrentConnectionInfo);
  bg_upnp_sa_desc_add_arg_in(sa, "ConnectionID",
                             "A_ARG_TYPE_ConnectionID", 0,
                             ARG_ConnectionID);

  bg_upnp_sa_desc_add_arg_out(sa, "RcsID",
                              "A_ARG_TYPE_RcsID", 0,
                              ARG_RcsID);
  
  bg_upnp_sa_desc_add_arg_out(sa, "AVTransportID",
                              "A_ARG_TYPE_AVTransportID", 0,
                              ARG_AVTransportID);
  
  bg_upnp_sa_desc_add_arg_out(sa, "ProtocolInfo",
                              "A_ARG_TYPE_ProtocolInfo", 0,
                              ARG_ProtocolInfo);

  bg_upnp_sa_desc_add_arg_out(sa, "PeerConnectionManager",
                              "A_ARG_TYPE_ConnectionManager", 0,
                              ARG_PeerConnectionManager);
  
  bg_upnp_sa_desc_add_arg_out(sa, "PeerConnectionID",
                              "A_ARG_TYPE_ConnectionID", 0,
                              ARG_PeerConnectionID);

  bg_upnp_sa_desc_add_arg_out(sa, "Direction",
                              "A_ARG_TYPE_Direction", 0,
                              ARG_Direction);

  bg_upnp_sa_desc_add_arg_out(sa, "Status",
                              "A_ARG_TYPE_ConnectionStatus", 0,
                              ARG_Status);
  }

void bg_upnp_service_init_connection_manager(bg_upnp_service_t * ret,
                                             int do_audio, int do_video)
  {
  bg_upnp_service_init(ret, BG_UPNP_SERVICE_ID_CM, "ConnectionManager", 1);
  init_service_desc(&ret->desc, do_audio, do_video);
  bg_upnp_service_start(ret);
  }
