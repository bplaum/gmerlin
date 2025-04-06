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

/* Radiobrowser URIs */
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include <config.h>

#include <gmerlin/translation.h>
#include <gmerlin/utils.h>
#include <gmerlin/http.h>
#include <gmerlin/plugin.h>
#include <gmerlin/pluginregistry.h>

#include <gavl/trackinfo.h>
#include <gavl/gavlsocket.h>
#include <gavl/log.h>
#include <gavl/metatags.h>
#define LOG_DOMAIN "radiobrowser"

#define RB_PROTOCOL "gmerlin-radiobrowser"

static pthread_mutex_t rb_server_mutex = PTHREAD_MUTEX_INITIALIZER;
static char * rb_server = NULL;

static int check_rb_server(const char * host)
  {
  int ret = 0;
  gavl_buffer_t buf;
  char * uri;

  /* Try do download the list of codecs */
  
  gavl_buffer_init(&buf);
  uri = gavl_sprintf("https://%s/json/codecs", host);

  ret = bg_http_get(uri, &buf, NULL);

  free(uri);
  gavl_buffer_free(&buf);
  return ret;
  }

static char * lookup_rb_server()
  {
  int i = 0;
  char * ret;
  gavl_socket_address_t ** addr;
  
  /* Full DNS lookup */
  addr = gavl_lookup_hostname_full("all.api.radio-browser.info", SOCK_STREAM);
  if(!addr)
    return NULL;
  
  /* Reverse DNS lookups */
  while(addr[i])
    {
    if((ret = gavl_socket_address_get_hostname(addr[i])) &&
       check_rb_server(ret))
      break;
    else
      ret = NULL;
    i++;
    }
  
  /* Free */
  gavl_socket_address_free_array(addr);
  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Looked up radiobrowser server: %s", ret);
  
  return ret;
  }

static const char * get_rb_server()
  {
  /* TODO: Re-read server every hour or so */
  if(!rb_server)
    rb_server = lookup_rb_server();
  return rb_server;
  }

char *bg_get_rb_server()
  {
  char * ret = NULL;
  pthread_mutex_lock(&rb_server_mutex);
  ret = gavl_strdup(get_rb_server());
  pthread_mutex_unlock(&rb_server_mutex);
  return ret;
  }

char * bg_rb_make_uri(const char * station_uuid)
  {
  return gavl_sprintf(RB_PROTOCOL":///%s", station_uuid);
  }

int bg_rb_check_uri(const char * uri)
  {
  return gavl_string_starts_with(uri, RB_PROTOCOL":///");
  }

char * bg_rb_resolve_uri(const char * uri)
  {
  char * ret;
  const char * srv;
  
  if(!bg_rb_check_uri(uri))
    return NULL;

  uri += strlen(RB_PROTOCOL":///");
  
  pthread_mutex_lock(&rb_server_mutex);

  //  https://de1.api.radio-browser.info/m3u/url/492a6362-66d5-11ea-be63-52543be04c81

  if(!(srv = get_rb_server()))
    return NULL;
  
  ret = gavl_sprintf("https://%s/m3u/url/%s", srv, uri);
  
  pthread_mutex_unlock(&rb_server_mutex);
  return ret;
  }


/* Radiobrowser plugin: Resolves URLs and redirects to an actual server */

typedef struct
  {
  gavl_dictionary_t mi;
  } rb_plugin_t;

static void close_rb(void * priv)
  {
  rb_plugin_t * p = priv;
  gavl_dictionary_reset(&p->mi);
  }


static int open_rb(void * priv, const char * uri)
  {
  char * real_uri = NULL;
  
  gavl_dictionary_t * track;
  gavl_dictionary_t * m;
  rb_plugin_t * p = priv;

  real_uri = bg_rb_resolve_uri(uri);

  if(!real_uri)
    return 0;
  
  track = gavl_append_track(&p->mi, NULL);
  m = gavl_track_get_metadata_nc(track);
  
  gavl_dictionary_set_string(m, GAVL_META_CLASS, GAVL_META_CLASS_LOCATION);
  
  gavl_metadata_add_src(m, GAVL_META_SRC, NULL, real_uri);
  free(real_uri);
  return 1;
  }

static void destroy_rb(void * priv)
  {
  rb_plugin_t * p = priv;
  close_rb(priv);
  free(p);
  }

static gavl_dictionary_t * get_media_info_rb(void * priv)
  {
  rb_plugin_t * p = priv;
  return &p->mi;
  }


static const bg_input_plugin_t rb_plugin =
  {
    .common =
    {
      BG_LOCALE,
      .name =           "i_rb",
      .long_name =      TRS("Radiobrowser resolver"),
      .description =    TRS("Resolve radiobbrowser URIs"),
      .type =           BG_PLUGIN_INPUT,
      .flags =          0,
      .priority =       1,
      .create =         NULL,
      .destroy =        destroy_rb,

      //      .get_controllable = get_controllable_rb,
      //      .get_parameters = get_parameters_edl,
      //      .set_parameter =  set_parameter_edl
    },
    .open = open_rb,
    .get_media_info = get_media_info_rb,
    
    //    .get_src           = get_src_edl,
    
    /* Read one video frame (returns FALSE on EOF) */
    
    /*
     *  Do percentage seeking (can be NULL)
     *  Media streams are supposed to be seekable, if this
     *  function is non-NULL AND the duration field of the track info
     *  is > 0
     */
    //    .seek = seek_edl,
    /* Stop playback, close all decoders */
    //    .stop = stop_edl,
    .close = close_rb,
  };

const bg_plugin_common_t * bg_rb_plugin_get()
  {
  return (bg_plugin_common_t*)(&rb_plugin);
  }


bg_plugin_info_t * bg_rb_plugin_get_info()
  {
  bg_plugin_info_t * ret;
  ret = bg_plugin_info_create(&rb_plugin.common);
  ret->protocols = gavl_value_set_array(&ret->protocols_val);
  gavl_string_array_add(ret->protocols, RB_PROTOCOL);
  return ret;
  }

void * bg_rb_plugin_create()
  {
  rb_plugin_t * priv = calloc(1, sizeof(*priv));
  return priv;
  }

