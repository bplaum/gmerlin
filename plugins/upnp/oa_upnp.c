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
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>

#include <config.h>
#include <gmerlin/translation.h>

#include <gmerlin/plugin.h>
#include <gmerlin/utils.h>
#include <gmerlin/http.h>
#include <gmerlin/bgsocket.h>
#include <gmerlin/httpserver.h>

#include <gmerlin/xmlutils.h>
#include <gmerlin/upnp/devicedesc.h>
#include <gmerlin/upnp/soap.h>
#include <gmerlin/upnp/eventlistener.h>


// #define WORDS_BIGENDIAN

#define STREAM_PATH "/stream.lpcm"

typedef struct
  {
  char * dev_url;
  int stream_fd;
  bg_http_server_t * server;
  
  char * stream_url;
  char * stream_mimetype;
  
  char * avt_url;
  char * cm_url;

  char * avt_evturl;
  
  char * instance_id;
  
  gavl_audio_sink_t * sink;

  pthread_t listen_thread;
  int thread_running;
  
  gavl_audio_format_t format;
  gavl_audio_frame_t * frame;

#ifndef WORDS_BIGENDIAN
  gavl_dsp_context_t * dsp;
#endif

  bg_upnp_event_listener_t * el;

  pthread_mutex_t fd_mutex;
  pthread_cond_t  fd_cond;

  int got_stream;
  int playing;

  } upnp_t;

static void close_upnp(void * p);


static void set_http_header(upnp_t * e, bg_http_connection_t * conn)
  {
  /* TODO: Set DLNA Spefific stuff */  
  bg_http_connection_init_res(conn, "HTTP/1.1", 200, "OK"); 
  gavl_dictionary_set_string(&conn->res, "Content-Type", e->stream_mimetype);
  gavl_dictionary_set_string(&conn->res, "transferMode.dlna.org", "Streaming");
  // gavl_dictionary_set_string(res, "Connection", "Close");
  //  gavl_dictionary_set_string(res, "contentFeatures.dlna.org",
  //                    "DLNA.ORG_PN=LPCM;DLNA.ORG_CI=1;DLNA.ORG_FLAGS=01700000000000000000000000000000");
  }

static const bg_parameter_info_t parameters[] =
  {
    {
      .name =      "device_url",
      .long_name = TRS("Device URL"),
      .type =      BG_PARAMETER_STRING,
    },
    { /* End of parameters */ }
  };

static void set_parameter_upnp(void * data, const char * name,
                              const gavl_value_t * val)
  {
  upnp_t * e = data;

  if(!name)
    return;
  
  if(!strcmp(name, "device_url"))
    e->dev_url = gavl_strrep(e->dev_url, val->v.str);
  }

static void * create_upnp()
  {
  upnp_t * ret = calloc(1, sizeof(*ret));

#ifndef WORDS_BIGENDIAN
  ret->dsp = gavl_dsp_context_create();
#endif

  pthread_mutex_init(&ret->fd_mutex, NULL);
  pthread_cond_init(&ret->fd_cond, NULL);
  
  return ret;
  }

static void destroy_upnp(void *data)
  {
  upnp_t * e = data;

  close_upnp(data);
  
#ifndef WORDS_BIGENDIAN
  gavl_dsp_context_destroy(e->dsp);
#endif
  
  if(e->dev_url)
    free(e->dev_url);

  pthread_mutex_destroy(&e->fd_mutex);
  pthread_cond_destroy(&e->fd_cond);
  free(e);
  }

static gavl_audio_frame_t * get_frame_func(void * p)
  {
  upnp_t * e = p;
  e->frame->valid_samples = 0;
  return e->frame;
  }

static gavl_sink_status_t
write_func_upnp(void * p, gavl_audio_frame_t * f)
  {
  upnp_t * e = p;
  int bytes, result;
  
#ifndef WORDS_BIGENDIAN
  gavl_dsp_audio_frame_swap_endian(e->dsp, e->frame, &e->format);
#endif

  bytes = e->frame->valid_samples * 2 * e->format.num_channels;
  
  result = bg_socket_write_data(e->stream_fd, e->frame->samples.u_8, bytes);

  //  fprintf(stderr, "write: %d %d %d\n", e->frame->valid_samples, bytes, result);
  
  if(result < bytes)
    return GAVL_SINK_ERROR;

  bg_upnp_event_listener_ping(e->el);
  bg_http_server_iteration(e->server);
  
  // if(write(e->fd, f->channels.s_8[0], f->valid_samples * e->block_align) < 0)
  // return GAVL_SINK_ERROR;
  return GAVL_SINK_OK;
  }

static int handle_http(bg_http_connection_t * conn, void * data)
  {
  upnp_t * e = data;

  if(bg_upnp_event_listener_handle(e->el, conn))
    {
    const char * state;
      
    state = bg_upnp_event_listener_get_var(e->el, "TransportState");
    
    fprintf(stderr, "*** Got event, state: %s\n", state);
    
    if(!e->playing && !strcmp(state, "PLAYING"))
      e->playing = 1;
    else if(e->playing && !strcmp(state, "STOPPED"))
      e->playing = 0;
    }
  else if(!e->got_stream && !strcmp(bg_http_request_get_path(&conn->req),
                                    STREAM_PATH))
    {
    // fprintf(stderr, "*** Got stream request\n");
      
    /* Write response */
    set_http_header(e, conn);
    
    if(!bg_http_connection_write_res(conn, 0))
      {
      pthread_mutex_lock(&e->fd_mutex);
      e->got_stream = 1;
      pthread_cond_broadcast(&e->fd_cond);
      pthread_mutex_unlock(&e->fd_mutex);
      return 1;
      }

    /* Check for keepalive */
    if(!strcmp(conn->method, "HEAD"))
      bg_http_connection_check_keepalive(conn);
    
    else if(!strcmp(conn->method, "GET"))
      {
      pthread_mutex_lock(&e->fd_mutex);
      e->stream_fd = conn->fd;
      conn->fd = -1;
      e->got_stream = 1;
      pthread_cond_broadcast(&e->fd_cond);
      pthread_mutex_unlock(&e->fd_mutex);
      }
    }
  else
    return 0; // 404

  return 1;
  }

/* Thread func */
static void * listen_func(void * data)
  {
  upnp_t * e = data;
  fprintf(stderr, "Listen thread started\n");
  while(1)
    {
    bg_upnp_event_listener_ping(e->el);

    if(!bg_http_server_iteration(e->server))
      {
      gavl_time_t delay = GAVL_TIME_SCALE/20; // 50 ms
      gavl_time_delay(&delay);
      continue;
      }
    if(e->got_stream) // End of thread
      return NULL;
    }
  fprintf(stderr, "Listen func exiting\n");
  
  return NULL;
  }

static const int samplerates[] =
  {
    44100,
    48000,
    /* End */
    0
  };
  
static int open_upnp(void * data, gavl_audio_format_t * format)
  {
  xmlDocPtr desc;
  
  char * tmp_string;
  xmlDocPtr service_desc;
  
  upnp_t * e = data;
  xmlNodePtr dev_node;
  xmlNodePtr service_node;
  char * url_base = NULL;
  int ret = 0;
  bg_soap_t soap;
  char ** protocol_info = NULL;
  int i;
  int supported = 0;
  
  memset(&soap, 0, sizeof(soap));
  
  /* Get device description */

  desc = bg_xml_from_url(e->dev_url, NULL);
  
  if(!desc)
    goto fail;
  
  fprintf(stderr, "Got description\n");

  url_base =
    bg_upnp_device_description_get_url_base(e->dev_url, desc);
  
  dev_node =
    bg_upnp_device_description_get_device_node(desc, "MediaRenderer", 1);
  
  if(!dev_node)
    goto fail;

  service_node =
    bg_upnp_device_description_get_service_node(dev_node,
                                                "ConnectionManager",
                                                1);
  
  if(!service_node)
    goto fail;

  tmp_string = bg_upnp_device_description_get_service_description(service_node,
                                                                  url_base);

  service_desc = bg_xml_from_url(tmp_string, NULL);
  free(tmp_string);

  if(!service_desc)
    goto fail;
  
  e->cm_url = bg_upnp_device_description_get_control_url(service_node,
                                                         url_base);
  if(!e->cm_url)
    goto fail;

  service_node =
    bg_upnp_device_description_get_service_node(dev_node,
                                                "AVTransport",
                                                1);

  if(!service_node)
    goto fail;

  e->avt_url = bg_upnp_device_description_get_control_url(service_node,
                                                          url_base);
  if(!e->avt_url)
    goto fail;

  e->avt_evturl = bg_upnp_device_description_get_event_url(service_node,
                                                           url_base);
  if(!e->avt_evturl)
    goto fail;
  
  //  fprintf(stderr, "Got Connection manager: %s\n", e->cm_url);
  //  fprintf(stderr, "Got AVTransport:        %s\n", e->avt_url);
  //  fprintf(stderr, "Got AVTransport events: %s\n", e->avt_evturl);
  
  /* Get protocol info */

  if(!bg_upnp_service_description_has_action(service_desc, "GetProtocolInfo"))
    {
    fprintf(stderr, "No action GetProtocolInfo\n");
    goto fail;
    }
  
  if(!bg_soap_init(&soap, e->cm_url, "ConnectionManager", 1, "GetProtocolInfo") ||
     !bg_soap_request(&soap))
    {
    bg_soap_free( &soap);
    goto fail;
    }
  
  protocol_info = bg_strbreak(bg_soap_response_get_argument(soap.res, "Sink"),
                              ',');

  bg_soap_free(&soap);
  
  //  fprintf(stderr, "Sink protocol info:\n");

  i = 0;

  while(protocol_info[i])
    {
    /* We assume, that if the Renderer supports one DLNA LPCM format,
       it supports all 4 formats, which are demanded by the DLNA spec:
       1/2 channels and 44100/48000 Hz samplerate.
    */
    
    if(strstr(protocol_info[i], "http-get:*:audio/L16;"))
      supported = 1;
    
#if 0    
    if(strstr(protocol_info[i], "http-get:*:audio/L16;") &&
       strstr(protocol_info[i], "DLNA.ORG_PN=LPCM"))
      supported = 1;
#endif
    
    fprintf(stderr, "  %s\n", protocol_info[i]);
    i++;
    }

  if(!supported)
    {
    fprintf(stderr, "No supported format\n");
    goto fail;
    }
  
  /* Open server fd */

  e->server = bg_http_server_create();

  if(!bg_http_server_start(e->server))
    goto fail;

  bg_http_server_add_handler(e->server,
                             handle_http,
                             BG_HTTP_PROTO_HTTP,
                             NULL, // E.g. /static/ can be NULL
                             e);
  
  e->stream_url = bg_sprintf("%s%s",
                             bg_http_server_get_root_url(e->server),
                             STREAM_PATH);
  
  tmp_string = bg_sprintf("%s/event",
                          bg_http_server_get_root_url(e->server));
  
  e->el = bg_upnp_event_listener_create(e->avt_evturl, tmp_string);
  
  fprintf(stderr, "Stream URL: %s\n", e->stream_url);
  fprintf(stderr, "Event URL: %s\n", tmp_string);
  
  free(tmp_string);
  
  /* Decide format */

  switch(format->num_channels)
    {
    case 1:
      format->num_channels = 1;
      format->channel_locations[0] = GAVL_CHID_FRONT_CENTER;
      break;
    default:
      format->num_channels = 2;
      format->channel_locations[0] = GAVL_CHID_FRONT_LEFT;
      format->channel_locations[1] = GAVL_CHID_FRONT_RIGHT;
      break;
    }
  
  format->samplerate = gavl_nearest_samplerate(format->samplerate,
                                               samplerates);
  format->sample_format = GAVL_SAMPLE_S16;
  format->interleave_mode = GAVL_INTERLEAVE_ALL;
  format->samples_per_frame = 1024;
  gavl_audio_format_copy(&e->format, format);
  
  e->stream_mimetype = bg_sprintf("audio/L16;rate=%d;channels=%d",
                                  e->format.samplerate,
                                  e->format.num_channels);
  
  e->frame = gavl_audio_frame_create(&e->format);
  
  /* From this point on, we need to expect client connections */

  e->stream_fd = -1;
  pthread_create(&e->listen_thread, NULL, listen_func, e); 
  e->thread_running = 1;
  
  /* Check, if there is a PrepareForConnection action implemented */

  if(bg_upnp_service_description_has_action(service_desc, "PrepareForConnection"))
    fprintf(stderr, "Need to call PrepareForConnection!!!\n");
  else
    e->instance_id = gavl_strdup("0");

  /* Call SetAVTransportURI */

  if(!bg_soap_init(&soap, e->avt_url, "AVTransport", 1, "SetAVTransportURI"))
    goto fail;

  bg_soap_request_add_argument(soap.req, "InstanceID", e->instance_id);
  bg_soap_request_add_argument(soap.req, "CurrentURI", e->stream_url);
  
  tmp_string =
    bg_sprintf("<DIDL-Lite xmlns:dc=\"http://purl.org/dc/elements/1.1/\" "
               "xmlns:upnp=\"urn:schemas-upnp-org:metadata-1-0/upnp/\" "
               "xmlns=\"urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/\">"
               "<item id=\"1\" parentID=\"0\" restricted=\"false\">"
               "<dc:title>Gmerlin output</dc:title>"
               "<upnp:class>object.item.audioItem.audioBroadcast</upnp:class>"
               "<res protocolInfo=\"http-get:*:%s:DLNA.ORG_PN=LPCM\">%s</res>"
               "</item>"
               "</DIDL-Lite>", e->stream_mimetype, e->stream_url);

  // fprintf(stderr, "DIDL:\n%s\n", tmp_string);
  
  bg_soap_request_add_argument(soap.req, "CurrentURIMetaData", tmp_string);
  free(tmp_string);
  
  if(!bg_soap_request(&soap))
    {
    bg_soap_free( &soap);
    goto fail;
    }
  bg_soap_free(&soap);
  
  /* */
  
  e->sink = gavl_audio_sink_create(get_frame_func, write_func_upnp, e, format);
  ret = 1;
  fail:

  if(protocol_info)
    bg_strbreak_free(protocol_info);
  
  if(url_base)
    free(url_base);
  if(desc)
    xmlFreeDoc(desc);
  if(service_desc)
    xmlFreeDoc(service_desc);
  
  return ret;
  }

static gavl_audio_sink_t * get_sink_upnp(void * p)
  {
  upnp_t * e = p;
  return e->sink;
  }

// static int stream_count = 0;

static int start_upnp(void * p)
  {
  upnp_t * e = p;
  bg_soap_t soap;
  
  /* Send "play" method and wait for connection */

  if(!bg_soap_init(&soap, e->avt_url, "AVTransport", 1, "Play"))
    return 0;
    
  bg_soap_request_add_argument(soap.req, "InstanceID", e->instance_id);
  bg_soap_request_add_argument(soap.req, "Speed", "1");
  
  if(!bg_soap_request(&soap))
    {
    bg_soap_free( &soap);
    return 0;
    }
  bg_soap_free(&soap);

  /* Join the connect thread */
  pthread_mutex_lock(&e->fd_mutex);
  if(e->stream_fd < 0)
    pthread_cond_wait(&e->fd_cond, &e->fd_mutex);
  pthread_mutex_unlock(&e->fd_mutex);
  
  if(e->stream_fd < 0)
    {
    fprintf(stderr, "Got no connection\n");
    return 0;
    }
  return 1;
  }

#define MY_FREE(ptr) if(ptr) { free(ptr); ptr = NULL; }

static void close_upnp(void * p)
  {
  upnp_t * e = p;

  if(e->stream_fd >= 0)
    {
    close(e->stream_fd);
    e->stream_fd = -1;
    }

  if(e->thread_running)
    {
    //  fprintf(stderr, "pthread_join\n");
    pthread_join(e->listen_thread, NULL);
    //  fprintf(stderr, "pthread_join done\n");
    e->thread_running = 0;
    }
  
  if(e->sink)
    {
    gavl_audio_sink_destroy(e->sink);
    e->sink = NULL;
    }

  if(e->server)
    {
    bg_http_server_destroy(e->server);
    e->server = NULL;
    }

  if(e->el)
    {
    bg_upnp_event_listener_destroy(e->el);
    e->el = NULL;
    }

  MY_FREE(e->instance_id);
  MY_FREE(e->stream_mimetype);
  MY_FREE(e->stream_url);
  
  MY_FREE(e->avt_url);
  MY_FREE(e->cm_url);
  MY_FREE(e->avt_evturl);
  
  if(e->frame)
    {
    gavl_audio_frame_destroy(e->frame);
    e->frame = NULL;
    }
  
  }

static void stop_upnp(void * p)
  {
  bg_soap_t soap;

  upnp_t * e = p;

  if(!bg_soap_init(&soap, e->avt_url, "AVTransport", 1, "Stop"))
    return;
  
  bg_soap_request_add_argument(soap.req, "InstanceID", e->instance_id);
  
  bg_soap_request(&soap);
  bg_soap_free(&soap);
  }

static const bg_parameter_info_t *
get_parameters_upnp(void * priv)
  {
  return parameters;
  }

const bg_oa_plugin_t the_plugin =
  {
    .common =
    {
      BG_LOCALE,
      .name =          "oa_upnp",
      .long_name =     TRS("Upnp audio output driver"),
      .description =   TRS("Stream audio to upnp capable devices"),
      .type =          BG_PLUGIN_OUTPUT_AUDIO,
      .flags =         BG_PLUGIN_PLAYBACK,
      .priority =      BG_PLUGIN_PRIORITY_MIN,
      .create =        create_upnp,
      .destroy =       destroy_upnp,

      .get_parameters = get_parameters_upnp,
      .set_parameter =  set_parameter_upnp
    },
    .open =          open_upnp,
    .start =         start_upnp,
    .stop =          stop_upnp,
    .close =         close_upnp,
    .get_sink =      get_sink_upnp,
  };

/* Include this into all plugin modules exactly once
   to let the plugin loader obtain the API version */
BG_GET_PLUGIN_API_VERSION;
