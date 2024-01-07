/*****************************************************************
 * Gmerlin - a general purpose multimedia framework and applications
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

#ifndef BACKEND_PRIV_H_INCLUDED
#define BACKEND_PRIV_H_INCLUDED
#include <config.h>

#include <gmerlin/upnp/ssdp.h>
#include <pthread.h>

typedef struct bg_remote_dev_backend_s bg_remote_dev_backend_t;

// extern bg_backend_registry_t * bg_backend_reg;

#define MPRIS2_NAME_PREFIX     "org.mpris.MediaPlayer2."
#define MPRIS2_NAME_PREFIX_LEN 23

/* Hide backends, which are exported by the same process */
#define SHADOW_LOCAL_BACKENDS

struct bg_backend_handle_s
  {
  void * priv;
  const bg_remote_dev_backend_t * b;

  /* Set by registry */
  gavl_dictionary_t dev;
  
  bg_controllable_t ctrl_int;
  char * root_url;

  bg_controllable_t * ctrl_ext;

  bg_controllable_t * ctrl;
  
  gavl_timer_t * timer;

  pthread_t th;
  int thread_running;

  char id[GAVL_MD5_LENGTH];
  
  };

struct bg_remote_dev_backend_s
  {
  const char * name;

  /* Prefix for uris, including ://" */
  const char * uri_prefix;
  const char * klass;
  
  /* Functions for a remote device */
  int (*handle_msg)(void * priv, // Must be bg_backend_handle_t
                    gavl_msg_t * msg);
  int (*handle_http)(bg_backend_handle_t * dev, bg_http_connection_t * conn);
  int (*ping)(bg_backend_handle_t * dev);
  int (*create)(bg_backend_handle_t * dev, const char * addr, const char * root_url);
  //  void (*stop)(bg_backend_handle_t * dev);
  void (*destroy)(bg_backend_handle_t * dev);
  };

#ifdef HAVE_DBUS
/* Dbus detector (mpris2 and mpd via avahi) */
typedef struct bg_dbus_detector_s bg_dbus_detector_t;

bg_dbus_detector_t * bg_dbus_detector_create();
void bg_dbus_detector_destroy(bg_dbus_detector_t *);

int bg_dbus_detector_update(bg_dbus_detector_t *);
#endif

/* Backend ID */
#define BACKEND_ID "BackendID"

/*
 *  Remote device registry
 */

struct bg_backend_registry_s
  {
  int do_stop;
  int do_rescan;

  gavl_array_t devs;
  gavl_array_t local_devs;
  
  bg_msg_sink_t * evt_sink;
  bg_msg_hub_t * evt_hub;
  
  pthread_mutex_t mutex;
  pthread_t th;
  
  gavl_array_t arr;
  bg_ssdp_t * ssdp;
#ifdef HAVE_DBUS
  bg_dbus_detector_t * dbus;
#endif  
  };


gavl_dictionary_t * bg_backend_by_str(const char * key, const char * label, int local, int * idx);

void bg_backend_add_remote(const gavl_dictionary_t * b);
void bg_backend_del_remote(const char * uri);



#endif // BACKEND_PRIV_H_INCLUDED
