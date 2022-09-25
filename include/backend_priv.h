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

#include <pthread.h>

typedef struct bg_remote_dev_backend_s bg_remote_dev_backend_t;
typedef struct bg_remote_dev_detector_s bg_remote_dev_detector_t;

extern bg_backend_registry_t * bg_backend_reg;

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

  char id[BG_BACKEND_ID_LEN+1];
  
  };

struct bg_remote_dev_backend_s
  {
  const char * name;

  /* Prefix for uris, including ://" */
  const char * uri_prefix;
  
  
  bg_backend_type_t type;
  
  /* Functions for a remote device */
  int (*handle_msg)(void * priv, // Must be bg_backend_handle_t
                    gavl_msg_t * msg);
  int (*handle_http)(bg_backend_handle_t * dev, bg_http_connection_t * conn);
  int (*ping)(bg_backend_handle_t * dev);
  int (*create)(bg_backend_handle_t * dev, const char * addr, const char * root_url);
  //  void (*stop)(bg_backend_handle_t * dev);
  void (*destroy)(bg_backend_handle_t * dev);
  };

struct bg_remote_dev_detector_s
  {
  /* Detector for remote devices (e.g. ssdp) */
  void * (*create)(void);
  void (*destroy)(void*);
  int  (*update)(void*);
  void (*init)(void*); // Called just once but from the main thread
  };

/*
 *  Remote device registry
 */

struct bg_backend_registry_s
  {
  int do_stop;

  gavl_array_t devs;
  gavl_array_t local_devs;
  
  bg_msg_sink_t * evt_handler;

  bg_msg_sink_t * evt_sink;
  bg_msg_hub_t * evt_hub;
  
  pthread_mutex_t mutex;
  pthread_t th;
  
  gavl_array_t arr;
  
  struct
    {
    void * priv;
    const bg_remote_dev_detector_t * d;
    } * detectors;
  
  };

int bg_backend_is_local(const char * uri,
                        gavl_dictionary_t * dev);

#endif // BACKEND_PRIV_H_INCLUDED
