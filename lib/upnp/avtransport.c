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
#include <ctype.h>

#include <config.h>

#include <upnp/devicepriv.h>
// #include <upnp/servicepriv.h>
#include <upnp/mediarenderer.h>
#include <upnp/didl.h>

#include <gavl/metatags.h>


#include <gmerlin/http.h>
#include <gmerlin/player.h>
#include <gmerlin/playermsg.h>
#include <gmerlin/utils.h>

#include <gmerlin/log.h>
#define LOG_DOMAIN "avtransport"

#include <gavl/utils.h>

#define MAX_TRACKS 999

/* Service actions */
static const char * xml_namespace = "urn:schemas-upnp-org:metadata-1-0/AVT/";

// SetAVTransportURI
#define ARG_InstanceID             1
#define ARG_CurrentURI             2
#define ARG_CurrentURIMetaData     3

// SetNextAVTransportURI
#define ARG_NextURI                4
#define ARG_NextURIMetaData        5

// GetMediaInfo
#define ARG_NrTracks               6
#define ARG_MediaDuration          7
// #define ARG_CurrentURI             8
// #define ARG_CurrentURIMetaData     9
// #define ARG_NextURI               10
// #define ARG_NextURIMetaData       11
#define ARG_PlayMedium            12
#define ARG_RecordMedium          13
#define ARG_WriteStatus           14

// GetTransportInfo
#define ARG_CurrentTransportState  15
#define ARG_CurrentTransportStatus 16
#define ARG_CurrentSpeed           17

// GetPositionInfo
#define ARG_Track                  18
#define ARG_TrackDuration          19
#define ARG_TrackMetaData          20
#define ARG_TrackURI               21
#define ARG_RelTime                22
#define ARG_AbsTime                23
#define ARG_RelCount               24
#define ARG_AbsCount               25

// GetDeviceCapabilities
#define ARG_PlayMedia              26
#define ARG_RecMedia               27
#define ARG_RecQualityModes        28

// GetTransportSettings
#define ARG_PlayMode               29
#define ARG_RecQualityMode         30
 
// Play
#define ARG_Speed                  31

// Seek
#define ARG_Unit                   32
#define ARG_Target                 33

// SetPlayMode
#define ARG_NewPlayMode            34

// GetCurrentTransportActions
#define ARG_Actions                35

#define TIME_STRING_LEN 10 // 000:00:00

char * bg_upnp_avtransport_print_time(char * buf, gavl_time_t t)
  {
  int seconds;
  int minutes;
  int hours;

  t /= GAVL_TIME_SCALE;
  seconds = t % 60;
  t /= 60;

  minutes = t % 60;
  t /= 60;

  hours = t % 1000;

  snprintf(buf, TIME_STRING_LEN, "%d:%02d:%02d", hours, minutes, seconds);
  return buf;
  }

/* Utilities */

static char * get_cd_urls(const char * url, char ** id)
  {
  char * ret;
  int port = 0;
  char * host = NULL;
  char * protocol = NULL;
  char * path = NULL;
  
  if(!bg_url_split(url, &protocol, NULL, NULL, &host, &port, &path))
    return NULL;

  if(path && id)
    {
    const char * pos;
    
    pos = path;

    while((*pos != '\0'))
      {
      if((*pos == '/') && isdigit(pos[1]))
        {
        pos++;
        break;
        }
      pos++;
      }
    
    if(*pos != '\0')
      *id = bg_sprintf("%"PRId64, (int64_t)strtoll(pos, NULL, 10));
    }
  
  if(port > 0)
    ret = bg_sprintf("%s://%s:%d/upnp/cd/control", protocol, host, port);
  else
    ret = bg_sprintf("%s://%s/upnp/cd/control", protocol, host);

  if(host)
    free(host);
  if(protocol)
    free(protocol);
  if(path)
    free(path);
  
  return ret;
  }

static int check_url(char ** url_p,
                     char ** metadata_str,
                     gavl_dictionary_t * metadata)
  {
  char * cd_url = NULL;
  char * id = NULL;
  char * didl = NULL;
  
  char * url = *url_p;
  
  if(!strncmp(url, "http://", 7))
    {
    const char * var;
    gavl_dictionary_t res;
    gavl_dictionary_init(&res);
    /* Do a HEAD request to check if the media is accessible */
    if(!bg_http_head(url, &res))
      {
      gavl_dictionary_free(&res);
      return 0;
      }
    /* Check for a gmerlin-server instance */
  
    //    fprintf(stderr, "Got media header\n");
    //    gavl_dictionary_dump(&res, 2);

    if((var = gavl_dictionary_get_string_i(&res, "Server")) &&
       strstr(var, "gmerlin") &&
       (cd_url = get_cd_urls(url, &id)) && id)
      {
      fprintf(stderr, "Detected gmerlin server, getting full metadata for ID %s\n", id);
      didl = bg_upnp_contentdirectory_browse_str(cd_url, id, 1);
      fprintf(stderr, "Got full metadata:\n%s\n", didl);
      }
    gavl_dictionary_free(&res);
    }
  
  if(didl && (!metadata_str || !(*metadata_str) || (strlen(didl) > strlen(*metadata_str))))
    {
    if(metadata_str && *metadata_str)
      free(*metadata_str);
    *metadata_str = didl;
    didl = NULL;
    }

  if(metadata_str && *metadata_str)
    {
    /* didl -> metadata */
    xmlDocPtr doc = NULL;
    xmlNodePtr child;
    
    if((doc = xmlParseMemory(*metadata_str, strlen(*metadata_str))) &&
       (child = bg_xml_find_doc_child(doc, "DIDL-Lite")) &&
       (child = bg_xml_find_node_child(child, "item")))
      bg_metadata_from_didl(metadata, child);

    if(doc)
      xmlFreeDoc(doc);
    }
  
  if(!gavl_metadata_has_src(metadata, GAVL_META_SRC, *url_p))
    gavl_metadata_add_src(metadata, GAVL_META_SRC, NULL, *url_p);
  
  if(didl)
    free(didl);
  if(cd_url)
    free(cd_url);
  if(id)
    free(id);
  
  return 1;
  }

// TODO
static int SetAVTransportURI(bg_upnp_service_t * s)
  {
  xmlDocPtr didl = NULL;
  int ret = 0;
  
  gavl_dictionary_t m;
  bg_upnp_sv_t * sv;
  bg_mediarenderer_t * priv = s->dev->priv;

  //  uint32_t InstanceID =
  //    bg_upnp_service_get_arg_in_uint(&s->req, ARG_InstanceID);

  char * CurrentURI =
    gavl_strdup(bg_upnp_service_get_arg_in_string(&s->req, ARG_CurrentURI));

  char * CurrentURIMetaData =
    gavl_strdup(bg_upnp_service_get_arg_in_string(&s->req, ARG_CurrentURIMetaData));
  
  gavl_dictionary_init(&m);

  if(!check_url(&CurrentURI, &CurrentURIMetaData, &m))
    goto fail;
  
  bg_player_set_location(s->dev->ctrl.cmd_sink, &m, priv->control_id);
  
  /* Save state variables */
  sv = bg_upnp_service_desc_sv_by_name(&s->desc, "AVTransportURI");
  bg_upnp_sv_set_string(sv, CurrentURI);

  sv = bg_upnp_service_desc_sv_by_name(&s->desc, "AVTransportURIMetaData");
  bg_upnp_sv_set_string(sv, CurrentURIMetaData);
  
  ret = 1;

  fail:

  gavl_dictionary_free(&m);
  if(didl)
    xmlFreeDoc(didl);

  if(CurrentURI)
    free(CurrentURI);
  if(CurrentURIMetaData)
    free(CurrentURIMetaData);
  
  return ret;
  }

// SetNextAVTransportURI
static int SetNextAVTransportURI(bg_upnp_service_t * s)
  {
  gavl_dictionary_t m;
  bg_upnp_sv_t * sv;
  char * metadata = NULL;
  xmlDocPtr didl = NULL;
  int ret = 0;
  bg_mediarenderer_t * priv = s->dev->priv;
  
  //  uint32_t InstanceID =
  //    bg_upnp_service_get_arg_in_uint(&s->req, ARG_InstanceID);

  char * NextURI =
    gavl_strdup(bg_upnp_service_get_arg_in_string(&s->req, ARG_NextURI));

  char * NextURIMetaData =
    gavl_strdup(bg_upnp_service_get_arg_in_string(&s->req, ARG_NextURIMetaData));
  
  gavl_dictionary_init(&m);

  // fprintf(stderr, "SetNextAVTransportURI:\nInstance: %u\nNextURI: %s\nNextURIMetaData: %s\n",
  //        InstanceID, NextURI, NextURIMetaData);

  if(!check_url(&NextURI, &NextURIMetaData, &m))
    goto fail;

  bg_player_set_next_location(s->dev->ctrl.cmd_sink, &m, priv->control_id);
  
  /* Save state variables */
  sv = bg_upnp_service_desc_sv_by_name(&s->desc, "NextAVTransportURI");
  bg_upnp_sv_set_string(sv, NextURI);

  sv = bg_upnp_service_desc_sv_by_name(&s->desc, "NextAVTransportURIMetaData");
  bg_upnp_sv_set_string(sv, NextURIMetaData);

  
  ret = 1;

  fail:
  gavl_dictionary_free(&m);

  if(didl)
    xmlFreeDoc(didl);
  if(metadata)
    free(metadata);

  if(NextURI)
    free(NextURI);
  if(NextURIMetaData)
    free(NextURIMetaData);
  
  return ret;
  
  }

// GetMediaInfo
static int GetMediaInfo(bg_upnp_service_t * s)
  {
  // TODO: Check instance ID

  //  fprintf(stderr, "GetMediaInfo\n");
  
  bg_upnp_service_set_arg_out(&s->req, ARG_NrTracks);
  bg_upnp_service_set_arg_out(&s->req, ARG_MediaDuration);
  bg_upnp_service_set_arg_out(&s->req, ARG_CurrentURI);
  bg_upnp_service_set_arg_out(&s->req, ARG_CurrentURIMetaData);
  bg_upnp_service_set_arg_out(&s->req, ARG_NextURI);
  bg_upnp_service_set_arg_out(&s->req, ARG_NextURIMetaData);
  bg_upnp_service_set_arg_out(&s->req, ARG_PlayMedium);
  bg_upnp_service_set_arg_out(&s->req, ARG_RecordMedium);
  bg_upnp_service_set_arg_out(&s->req, ARG_WriteStatus);
  return 1;
  }

// GetTransportInfo
static int GetTransportInfo(bg_upnp_service_t * s)
  {
  // TODO: Check instance ID

  //  fprintf(stderr, "GetTransportInfo\n");

  bg_upnp_service_set_arg_out(&s->req, ARG_CurrentTransportState);
  bg_upnp_service_set_arg_out(&s->req, ARG_CurrentTransportStatus);
  bg_upnp_service_set_arg_out(&s->req, ARG_CurrentSpeed);
  return 1;
  }

// GetPositionInfo
static int GetPositionInfo(bg_upnp_service_t * s)
  {
  // TODO: Check instance ID
  //  fprintf(stderr, "GetPositionInfo\n");
  bg_upnp_service_set_arg_out(&s->req, ARG_Track);
  bg_upnp_service_set_arg_out(&s->req, ARG_TrackDuration);
  bg_upnp_service_set_arg_out(&s->req, ARG_TrackMetaData);
  bg_upnp_service_set_arg_out(&s->req, ARG_TrackURI);
  bg_upnp_service_set_arg_out(&s->req, ARG_RelTime);
  bg_upnp_service_set_arg_out(&s->req, ARG_AbsTime);
  bg_upnp_service_set_arg_out(&s->req, ARG_RelCount);
  bg_upnp_service_set_arg_out(&s->req, ARG_AbsCount);

  

  return 1;
  }

// GetDeviceCapabilities
static int GetDeviceCapabilities(bg_upnp_service_t * s)
  {
  // TODO: Check instance ID
  //  fprintf(stderr, "GetDeviceCapabilities\n");
  bg_upnp_service_set_arg_out(&s->req, ARG_PlayMedia);
  bg_upnp_service_set_arg_out(&s->req, ARG_RecMedia);
  bg_upnp_service_set_arg_out(&s->req, ARG_RecQualityModes);
  return 1;
  }

// GetTransportSettings
static int GetTransportSettings(bg_upnp_service_t * s)
  {
  //  fprintf(stderr, "GetTransportSettings\n");
  // TODO: Check instance ID
  bg_upnp_service_set_arg_out(&s->req, ARG_PlayMode);
  bg_upnp_service_set_arg_out(&s->req, ARG_RecQualityMode);
  return 1;
  }

// Stop
static int Stop(bg_upnp_service_t * s)
  {
  bg_player_stop(s->dev->ctrl.cmd_sink);
  return 1;
  }

// TODO
static int Play(bg_upnp_service_t * s)
  {
  bg_player_play(s->dev->ctrl.cmd_sink);
  return 1;
  }

static int Pause(bg_upnp_service_t * s)
  {
  bg_player_pause(s->dev->ctrl.cmd_sink);
  return 1;
  }

// TODO
static int Seek(bg_upnp_service_t * s)
  {
  gavl_time_t t;
  const char * unit;
  const char * target;
  //  bg_mediarenderer_t * priv = s->dev->priv;

  unit = bg_upnp_service_get_arg_in_string(&s->req, ARG_Unit);
  target = bg_upnp_service_get_arg_in_string(&s->req, ARG_Target);

  fprintf(stderr, "Seek %s %s\n", unit, target);
  
  if(!strcmp(unit, "ABS_TIME"))
    {
    gavl_time_parse(target, &t);
    bg_player_seek(s->dev->ctrl.cmd_sink, t, GAVL_TIME_SCALE);
    }
  else if(!strcmp(unit, "REL_TIME"))
    {
    gavl_time_parse(target, &t);
    bg_player_seek(s->dev->ctrl.cmd_sink, t, GAVL_TIME_SCALE);
    }
  return 1;
  }

// TODO
static int Next(bg_upnp_service_t * s)
  {
  //  fprintf(stderr, "Next\n");
  return 0;
  }

// TODO
static int Previous(bg_upnp_service_t * s)
  {
  //  fprintf(stderr, "Previous\n");
  return 0;
  }

// TODO
static int SetPlayMode(bg_upnp_service_t * s)
  {
  //  fprintf(stderr, "SetPlayMode\n");
  return 0;
  }

// GetCurrentTransportActions
static int GetCurrentTransportActions(bg_upnp_service_t * s)
  {
  //  fprintf(stderr, "GetCurrentTransportActions\n");
  bg_upnp_service_set_arg_out(&s->req, ARG_Actions);
  return 1;
  }

/* Initialize service description */

static void init_service_desc(bg_upnp_service_desc_t * d)
  {
  bg_upnp_sv_val_t  val;
  bg_upnp_sv_t * sv;
  bg_upnp_sa_desc_t * sa;

  bg_upnp_sv_range_t range;
  
  /* State variables */

  // TransportState
  sv = bg_upnp_service_desc_add_sv(d, "TransportState",
                                   BG_UPNP_SV_EVENT_MOD, BG_UPNP_SV_STRING);
  
  val.s = "STOPPED";
  bg_upnp_sv_add_allowed(sv, &val);
  val.s = "PLAYING";
  bg_upnp_sv_add_allowed(sv, &val);
  val.s = "TRANSITIONING";
  bg_upnp_sv_add_allowed(sv, &val);
  val.s = "PAUSED_PLAYBACK";
  bg_upnp_sv_add_allowed(sv, &val);
  val.s = "NO_MEDIA_PRESENT";
  bg_upnp_sv_add_allowed(sv, &val);

  val.s = "NO_MEDIA_PRESENT";
  bg_upnp_sv_init(sv, &val);
  
  // TransportStatus
  sv = bg_upnp_service_desc_add_sv(d, "TransportStatus",
                                   BG_UPNP_SV_EVENT_MOD, BG_UPNP_SV_STRING);
  
  val.s = "OK";
  bg_upnp_sv_add_allowed(sv, &val);
  val.s = "ERROR_OCCURRED";
  bg_upnp_sv_add_allowed(sv, &val);

  val.s = "OK";
  bg_upnp_sv_init(sv, &val);
  
  // PlaybackStorageMedium
  sv = bg_upnp_service_desc_add_sv(d, "PlaybackStorageMedium",
                                   BG_UPNP_SV_EVENT_MOD, BG_UPNP_SV_STRING);
  
  val.s = "NETWORK";
  bg_upnp_sv_add_allowed(sv, &val);
    
  // RecordStorageMedium
  sv = bg_upnp_service_desc_add_sv(d, "RecordStorageMedium",
                                   BG_UPNP_SV_EVENT_MOD|BG_UPNP_SV_NOTIMPL, BG_UPNP_SV_STRING);
  
  // PossiblePlaybackStorageMedia
  sv = bg_upnp_service_desc_add_sv(d, "PossiblePlaybackStorageMedia",
                                   BG_UPNP_SV_EVENT_MOD, BG_UPNP_SV_STRING);
  
  // PossibleRecordStorageMedia
  sv = bg_upnp_service_desc_add_sv(d, "PossibleRecordStorageMedia",
                                   BG_UPNP_SV_EVENT_MOD|BG_UPNP_SV_NOTIMPL, BG_UPNP_SV_STRING);
  // CurrentPlayMode
  sv = bg_upnp_service_desc_add_sv(d, "CurrentPlayMode",
                                   BG_UPNP_SV_EVENT_MOD, BG_UPNP_SV_STRING);
  val.s = "NORMAL";
  bg_upnp_sv_set_default(sv, &val);

  val.s = "NORMAL";
  bg_upnp_sv_add_allowed(sv, &val);
  val.s = "RANDOM";
  bg_upnp_sv_add_allowed(sv, &val);
  val.s = "REPEAT_ONE";
  bg_upnp_sv_add_allowed(sv, &val);
  val.s = "REPEAT_ALL";
  bg_upnp_sv_add_allowed(sv, &val);
  
  // TransportPlaySpeed
  sv = bg_upnp_service_desc_add_sv(d, "TransportPlaySpeed",
                                   BG_UPNP_SV_EVENT_MOD, BG_UPNP_SV_STRING);
  val.s = "1";
  bg_upnp_sv_add_allowed(sv, &val);
  
  // RecordMediumWriteStatus
  sv = bg_upnp_service_desc_add_sv(d, "RecordMediumWriteStatus",
                                   BG_UPNP_SV_EVENT_MOD|BG_UPNP_SV_NOTIMPL, BG_UPNP_SV_STRING);
  
  // CurrentRecordQualityMode
  sv = bg_upnp_service_desc_add_sv(d, "CurrentRecordQualityMode",
                                   BG_UPNP_SV_EVENT_MOD|BG_UPNP_SV_NOTIMPL, BG_UPNP_SV_STRING);
  
  // PossibleRecordQualityModes
  sv = bg_upnp_service_desc_add_sv(d, "PossibleRecordQualityModes",
                                   BG_UPNP_SV_EVENT_MOD|BG_UPNP_SV_NOTIMPL, BG_UPNP_SV_STRING);
  
  // NumberOfTracks
  sv = bg_upnp_service_desc_add_sv(d, "NumberOfTracks",
                                   BG_UPNP_SV_EVENT_MOD, BG_UPNP_SV_INT4);

  val.i = 1;
  bg_upnp_sv_init(sv, &val);
  
  range.min.i = 0;
  range.max.i = MAX_TRACKS;
  range.step.i = 1;
  bg_upnp_sv_set_range(sv, &range);
  
  // CurrentTrack
  sv = bg_upnp_service_desc_add_sv(d, "CurrentTrack",
                                   BG_UPNP_SV_EVENT_MOD, BG_UPNP_SV_INT4);

  
  range.min.i = 0;
  range.max.i = MAX_TRACKS;
  range.step.i = 1;
  bg_upnp_sv_set_range(sv, &range);
  
  val.i = 0;
  bg_upnp_sv_init(sv, &val);
  
  // CurrentTrackDuration
  sv = bg_upnp_service_desc_add_sv(d, "CurrentTrackDuration",
                                   BG_UPNP_SV_EVENT_MOD, BG_UPNP_SV_STRING);
  val.s = "0:00:00";
  bg_upnp_sv_init(sv, &val);
  
  // CurrentMediaDuration
  sv = bg_upnp_service_desc_add_sv(d, "CurrentMediaDuration",
                                   BG_UPNP_SV_EVENT_MOD, BG_UPNP_SV_STRING);
  val.s = "0:00:00";
  bg_upnp_sv_init(sv, &val);
  
  // CurrentTrackMetaData
  sv = bg_upnp_service_desc_add_sv(d, "CurrentTrackMetaData",
                                   BG_UPNP_SV_EVENT_MOD, BG_UPNP_SV_STRING);

  // CurrentTrackURI
  sv = bg_upnp_service_desc_add_sv(d, "CurrentTrackURI",
                                   BG_UPNP_SV_EVENT_MOD, BG_UPNP_SV_STRING);
  // AVTransportURI
  sv = bg_upnp_service_desc_add_sv(d, "AVTransportURI",
                                   BG_UPNP_SV_EVENT_MOD, BG_UPNP_SV_STRING);
  // AVTransportURIMetaData
  sv = bg_upnp_service_desc_add_sv(d, "AVTransportURIMetaData",
                                   BG_UPNP_SV_EVENT_MOD, BG_UPNP_SV_STRING);
  // NextAVTransportURI
  sv = bg_upnp_service_desc_add_sv(d, "NextAVTransportURI",
                                   BG_UPNP_SV_EVENT_MOD, BG_UPNP_SV_STRING);
  // NextAVTransportURIMetaData
  sv = bg_upnp_service_desc_add_sv(d, "NextAVTransportURIMetaData",
                                   BG_UPNP_SV_EVENT_MOD, BG_UPNP_SV_STRING);
  // RelativeTimePosition
  sv = bg_upnp_service_desc_add_sv(d, "RelativeTimePosition",
                                   0, BG_UPNP_SV_STRING);
  val.s = "0:00:00";
  bg_upnp_sv_init(sv, &val);
  
  // AbsoluteTimePosition
  sv = bg_upnp_service_desc_add_sv(d, "AbsoluteTimePosition",
                                   0, BG_UPNP_SV_STRING);
  val.s = "0:00:00";
  bg_upnp_sv_init(sv, &val);

  // RelativeCounterPosition
  sv = bg_upnp_service_desc_add_sv(d, "RelativeCounterPosition",
                                   BG_UPNP_SV_NOTIMPL, BG_UPNP_SV_INT4);
  // AbsoluteCounterPosition
  sv = bg_upnp_service_desc_add_sv(d, "AbsoluteCounterPosition",
                                   BG_UPNP_SV_NOTIMPL, BG_UPNP_SV_INT4);
  // CurrentTransportActions
  sv = bg_upnp_service_desc_add_sv(d, "CurrentTransportActions",
                                   BG_UPNP_SV_EVENT_MOD, BG_UPNP_SV_STRING);
  val.s = "";
  bg_upnp_sv_init(sv, &val);

  // LastChange
  sv = bg_upnp_service_desc_add_sv(d, "LastChange",
                                   BG_UPNP_SV_EVENT, BG_UPNP_SV_STRING);
  // Event frequency
  sv->interval = GAVL_TIME_SCALE / 5;
  sv->ns = xml_namespace;
  
  // A_ARG_TYPE_SeekMode
  sv = bg_upnp_service_desc_add_sv(d, "A_ARG_TYPE_SeekMode",
                                   BG_UPNP_SV_ARG_ONLY, BG_UPNP_SV_STRING);

  val.s = "TRACK_NR";
  bg_upnp_sv_add_allowed(sv, &val);

  //  val.s = "ABS_TIME";
  //  bg_upnp_sv_add_allowed(sv, &val);

  val.s = "REL_TIME";
  bg_upnp_sv_add_allowed(sv, &val);
  
  // A_ARG_TYPE_SeekTarget 
  sv = bg_upnp_service_desc_add_sv(d, "A_ARG_TYPE_SeekTarget",
                                   BG_UPNP_SV_ARG_ONLY, BG_UPNP_SV_STRING);
  
  // A_ARG_TYPE_InstanceID
  sv = bg_upnp_service_desc_add_sv(d, "A_ARG_TYPE_InstanceID",
                                   BG_UPNP_SV_ARG_ONLY, BG_UPNP_SV_UINT4);
  
  
  /* Actions */

  // SetAVTransportURI
  sa = bg_upnp_service_desc_add_action(d, "SetAVTransportURI", SetAVTransportURI);

  bg_upnp_sa_desc_add_arg_in(sa, "InstanceID",
                             "A_ARG_TYPE_InstanceID", 0,
                             ARG_InstanceID);

  bg_upnp_sa_desc_add_arg_in(sa, "CurrentURI",
                             "AVTransportURI", 0,
                             ARG_CurrentURI);

  bg_upnp_sa_desc_add_arg_in(sa, "CurrentURIMetaData",
                             "AVTransportURIMetaData", 0,
                             ARG_CurrentURIMetaData);

  // SetNextAVTransportURI
  sa = bg_upnp_service_desc_add_action(d, "SetNextAVTransportURI", SetNextAVTransportURI);

  bg_upnp_sa_desc_add_arg_in(sa, "InstanceID",
                             "A_ARG_TYPE_InstanceID", 0,
                             ARG_InstanceID);

  bg_upnp_sa_desc_add_arg_in(sa, "NextURI",
                             "NextAVTransportURI", 0,
                             ARG_NextURI);

  bg_upnp_sa_desc_add_arg_in(sa, "URIMetaData",
                             "NextAVTransportURIMetaData", 0,
                             ARG_NextURIMetaData);

  
  // GetMediaInfo
  sa = bg_upnp_service_desc_add_action(d, "GetMediaInfo", GetMediaInfo);
  bg_upnp_sa_desc_add_arg_in(sa, "InstanceID",
                             "A_ARG_TYPE_InstanceID", 0,
                             ARG_InstanceID);

  bg_upnp_sa_desc_add_arg_out(sa, "NrTracks", "NumberOfTracks", 0, ARG_NrTracks);
  bg_upnp_sa_desc_add_arg_out(sa, "MediaDuration", "CurrentMediaDuration", 0, ARG_MediaDuration);
  bg_upnp_sa_desc_add_arg_out(sa, "CurrentURI", "AVTransportURI", 0, ARG_CurrentURI);
  bg_upnp_sa_desc_add_arg_out(sa, "CurrentURIMetaData", "AVTransportURIMetaData", 0, ARG_CurrentURIMetaData);
  bg_upnp_sa_desc_add_arg_out(sa, "NextURI", "NextAVTransportURI", 0, ARG_NextURI);
  bg_upnp_sa_desc_add_arg_out(sa, "NextURIMetaData", "NextAVTransportURIMetaData", 0, ARG_NextURIMetaData); 
  bg_upnp_sa_desc_add_arg_out(sa, "PlayMedium", "PlaybackStorageMedium", 0, ARG_PlayMedium);
  bg_upnp_sa_desc_add_arg_out(sa, "RecordMedium", "RecordStorageMedium", 0, ARG_RecordMedium);
  bg_upnp_sa_desc_add_arg_out(sa, "WriteStatus", "RecordMediumWriteStatus", 0, ARG_WriteStatus);
  
  // GetTransportInfo
  sa = bg_upnp_service_desc_add_action(d, "GetTransportInfo", GetTransportInfo);
  bg_upnp_sa_desc_add_arg_in(sa, "InstanceID",
                             "A_ARG_TYPE_InstanceID", 0,
                             ARG_InstanceID);

  bg_upnp_sa_desc_add_arg_out(sa, "CurrentTransportState", "TransportState", 0, ARG_CurrentTransportState);
  bg_upnp_sa_desc_add_arg_out(sa, "CurrentTransportStatus", "TransportStatus", 0, ARG_CurrentTransportStatus);
  bg_upnp_sa_desc_add_arg_out(sa, "CurrentSpeed", "TransportPlaySpeed", 0, ARG_CurrentSpeed);

  // GetPositionInfo
  sa = bg_upnp_service_desc_add_action(d, "GetPositionInfo", GetPositionInfo);
  bg_upnp_sa_desc_add_arg_in(sa, "InstanceID",
                             "A_ARG_TYPE_InstanceID", 0,
                             ARG_InstanceID);

  bg_upnp_sa_desc_add_arg_out(sa, "Track", "CurrentTrack", 0, ARG_Track);
  bg_upnp_sa_desc_add_arg_out(sa, "TrackDuration", "CurrentTrackDuration", 0, ARG_TrackDuration);
  bg_upnp_sa_desc_add_arg_out(sa, "TrackMetaData", "CurrentTrackMetaData", 0, ARG_TrackMetaData);
  bg_upnp_sa_desc_add_arg_out(sa, "TrackURI", "CurrentTrackURI", 0, ARG_TrackURI);
  bg_upnp_sa_desc_add_arg_out(sa, "RelTime", "RelativeTimePosition", 0, ARG_RelTime);
  bg_upnp_sa_desc_add_arg_out(sa, "AbsTime", "AbsoluteTimePosition", 0, ARG_AbsTime);
  bg_upnp_sa_desc_add_arg_out(sa, "RelCount", "RelativeCounterPosition", 0, ARG_RelCount);
  bg_upnp_sa_desc_add_arg_out(sa, "AbsCount", "AbsoluteCounterPosition", 0, ARG_AbsCount);

  // GetDeviceCapabilities
  sa = bg_upnp_service_desc_add_action(d, "GetDeviceCapabilities", GetDeviceCapabilities);
  bg_upnp_sa_desc_add_arg_in(sa, "InstanceID",
                             "A_ARG_TYPE_InstanceID", 0,
                             ARG_InstanceID);

  bg_upnp_sa_desc_add_arg_out(sa, "PlayMedia", "PossiblePlaybackStorageMedia", 0, ARG_PlayMedia);
  bg_upnp_sa_desc_add_arg_out(sa, "RecMedia", "PossibleRecordStorageMedia", 0, ARG_RecMedia);
  bg_upnp_sa_desc_add_arg_out(sa, "RecQualityModes", "PossibleRecordQualityModes", 0, ARG_RecQualityModes);

  // GetTransportSettings
  sa = bg_upnp_service_desc_add_action(d, "GetTransportSettings", GetTransportSettings);
  bg_upnp_sa_desc_add_arg_in(sa, "InstanceID",
                             "A_ARG_TYPE_InstanceID", 0,
                             ARG_InstanceID);

  bg_upnp_sa_desc_add_arg_out(sa, "PlayMode", "CurrentPlayMode", 0, ARG_PlayMode);
  bg_upnp_sa_desc_add_arg_out(sa, "RecQualityMode", "CurrentRecordQualityMode", 0, ARG_RecQualityMode);

  // Stop
  sa = bg_upnp_service_desc_add_action(d, "Stop", Stop);
  bg_upnp_sa_desc_add_arg_in(sa, "InstanceID",
                             "A_ARG_TYPE_InstanceID", 0,
                             ARG_InstanceID);

  // Play
  sa = bg_upnp_service_desc_add_action(d, "Play", Play);
  bg_upnp_sa_desc_add_arg_in(sa, "InstanceID",
                             "A_ARG_TYPE_InstanceID", 0,
                             ARG_InstanceID);
  bg_upnp_sa_desc_add_arg_in(sa, "Speed", "TransportPlaySpeed", 0, ARG_Speed);

  // Pause
  sa = bg_upnp_service_desc_add_action(d, "Pause", Pause);
  bg_upnp_sa_desc_add_arg_in(sa, "InstanceID",
                             "A_ARG_TYPE_InstanceID", 0,
                             ARG_InstanceID);


  // Seek
  sa = bg_upnp_service_desc_add_action(d, "Seek", Seek);
  bg_upnp_sa_desc_add_arg_in(sa, "InstanceID",
                             "A_ARG_TYPE_InstanceID", 0,
                             ARG_InstanceID);
  bg_upnp_sa_desc_add_arg_in(sa, "Unit", "A_ARG_TYPE_SeekMode", 0, ARG_Unit);
  bg_upnp_sa_desc_add_arg_in(sa, "Target", "A_ARG_TYPE_SeekTarget", 0, ARG_Target);
 
  // Next
  sa = bg_upnp_service_desc_add_action(d, "Next", Next);
  bg_upnp_sa_desc_add_arg_in(sa, "InstanceID",
                             "A_ARG_TYPE_InstanceID", 0,
                             ARG_InstanceID);

  // Previous
  sa = bg_upnp_service_desc_add_action(d, "Previous", Previous);
  bg_upnp_sa_desc_add_arg_in(sa, "InstanceID",
                             "A_ARG_TYPE_InstanceID", 0,
                             ARG_InstanceID);

  // SetPlayMode
  sa = bg_upnp_service_desc_add_action(d, "SetPlayMode", SetPlayMode);
  bg_upnp_sa_desc_add_arg_in(sa, "InstanceID",
                             "A_ARG_TYPE_InstanceID", 0,
                             ARG_InstanceID);
  bg_upnp_sa_desc_add_arg_in(sa, "NewPlayMode", "CurrentPlayMode", 0,
                             ARG_NewPlayMode);

  // GetCurrentTransportActions
  sa = bg_upnp_service_desc_add_action(d, "GetCurrentTransportActions", GetCurrentTransportActions);
  bg_upnp_sa_desc_add_arg_in(sa, "InstanceID",
                             "A_ARG_TYPE_InstanceID", 0,
                             ARG_InstanceID);
  bg_upnp_sa_desc_add_arg_out(sa, "Actions", "CurrentTransportActions", 0, ARG_Actions);

  
  // 
  
  }

void bg_upnp_service_init_avtransport(bg_upnp_service_t * ret)
  {
  bg_upnp_service_init(ret, BG_UPNP_SERVICE_ID_AVT, "AVTransport", 1);
  init_service_desc(&ret->desc);
  bg_upnp_service_start(ret);
  }
