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

#include <string.h>
#include <config.h>
#include <pthread.h>


#include <gavl/metatags.h>

#include <gmerlin/backend.h>

#include <gmerlin/utils.h>
#include <gmerlin/msgqueue.h>
#include <gmerlin/player.h>
#include <gmerlin/playermsg.h>
#include <gmerlin/translation.h>
#include <gmerlin/log.h>
#include <gmerlin/httpserver.h>

#define LOG_DOMAIN "backendregistry"

#include <backend_priv.h>

extern bg_remote_dev_detector_t bg_remote_dev_detector_upnp;

#ifdef HAVE_DBUS
extern bg_remote_dev_detector_t bg_remote_dev_detector_dbus;
#endif

static const bg_remote_dev_detector_t *  
remote_dev_detectors[] =
  {
    &bg_remote_dev_detector_upnp,
#ifdef HAVE_DBUS
    &bg_remote_dev_detector_dbus,
#endif
  };

#define NUM_DEV_DETECTORS (sizeof(remote_dev_detectors)/sizeof(remote_dev_detectors[0]))

bg_backend_registry_t * bg_backend_reg = NULL;

static void backend_registry_lock()
  {
  pthread_mutex_lock(&bg_backend_reg->mutex);
  }

static void backend_registry_unlock()
  {
  pthread_mutex_unlock(&bg_backend_reg->mutex);
  }

bg_msg_hub_t * bg_backend_registry_get_evt_hub()
  {
  bg_backend_registry_t * reg = bg_get_backend_registry();
  return reg->evt_hub;
  }

static int dev_by_url(const bg_backend_registry_t * reg,
                      const char * url)
  {
  int i;
  const gavl_dictionary_t * dict;
  const char * str;
  
  for(i = 0; i < reg->devs.num_entries; i++)
    {
    if((dict = gavl_value_get_dictionary(&reg->devs.entries[i])) &&
       (str = gavl_dictionary_get_string(dict, GAVL_META_URI)) &&
       !strcmp(url, str))
      return i;
    }
  
  return -1;
  }

/* We connect to our own events to maintain the registry in memory */

static int handle_message(void * data, gavl_msg_t * msg)
  {
  bg_backend_registry_t * reg = data;
  
  switch(msg->NS)
    {
    case BG_MSG_NS_BACKEND:
      {
      switch(msg->ID)
        {
        case BG_MSG_ADD_BACKEND:
          {
          const char * uri;
          int idx = -1;
          
          gavl_value_t val;
          gavl_dictionary_t * dict;

          gavl_value_init(&val);
          dict = gavl_value_set_dictionary(&val);
          
          bg_msg_get_backend_info(msg, dict);

          /* No URI given or entry already there */
             
          if(!(uri = gavl_dictionary_get_string(dict, GAVL_META_URI)) ||
             ((idx = dev_by_url(reg, uri)) >= 0))
            {
            gavl_value_free(&val);
            break;
            }
          
          /* Add to array */
          gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Adding device %s [%s]", uri,
                 gavl_dictionary_get_string(dict, BG_BACKEND_PROTOCOL));
          
          gavl_array_splice_val_nocopy(&reg->devs, -1, 0, &val);
          }
          break;
        case BG_MSG_DEL_BACKEND:
          {
          int idx = -1;
          
          const char * uri;
          if(!(uri = gavl_msg_get_arg_string_c(msg, 0)))
            break;
          
          if((idx = dev_by_url(reg, uri)) >= 0)
            {
            gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Deleting device %s", uri);
            gavl_array_splice_val(&reg->devs, idx, 1, NULL);
            }
          else
            {
            gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Couldn't delete device %s", uri);
            }
          }
          break;
        }
      }
      break;
    }
  return 1;
  }

gavl_array_t *
bg_backend_registry_get()
  {
  gavl_array_t * ret;

  bg_backend_registry_t * reg = bg_get_backend_registry();
  
  ret = gavl_array_create();

  backend_registry_lock();
  gavl_array_splice_array(ret, 0, 0, &reg->devs);
  backend_registry_unlock();
  
  return ret;
  }

static void bg_backend_registry_init(bg_backend_registry_t * reg)
  {
  int i;

  backend_registry_lock();
  for(i = 0; i < NUM_DEV_DETECTORS; i++)
    {
    if(reg->detectors[i].d->init)
      reg->detectors[i].d->init(reg->detectors[i].priv);
    }

  backend_registry_unlock();
  }


static int bg_backend_registry_update(bg_backend_registry_t * reg)
  {
  int i;
  int result;
  int ret = 0;
  backend_registry_lock();
  for(i = 0; i < NUM_DEV_DETECTORS; i++)
    {
    if(reg->detectors[i].d->update)
      {
      result = reg->detectors[i].d->update(reg->detectors[i].priv);
      ret += result;
      }
    }
  backend_registry_unlock();

  return ret;
  }

gavl_array_t *
bg_backend_registry_get_type(bg_backend_type_t type)
  {
  int i;
  gavl_array_t * ret;
  const gavl_dictionary_t * dict;
  int t = 0;

  bg_backend_registry_t * reg = bg_get_backend_registry();
  
  ret = gavl_array_create();

  backend_registry_lock();
  
  for(i = 0; i < reg->devs.num_entries; i++)
    {
    if((dict = gavl_value_get_dictionary(&reg->devs.entries[i])) &&
       (gavl_dictionary_get_int(dict, BG_BACKEND_TYPE, &t)) &&
       (t == type))
      gavl_array_splice_val(ret, -1, 0, &reg->devs.entries[i]);
    }
  backend_registry_unlock();
  return ret;
  }

gavl_dictionary_t * bg_backend_registry_get_by_id(const char * id)
  {
  int i;
  const char * var;
  gavl_dictionary_t * ret = NULL;
  const gavl_dictionary_t * dict = NULL;

  bg_backend_registry_t * reg = bg_get_backend_registry();
  
  backend_registry_lock();

  for(i = 0; i < reg->devs.num_entries; i++)
    {
    if((dict = gavl_value_get_dictionary(&reg->devs.entries[i])) &&
       (var = gavl_dictionary_get_string(dict, GAVL_META_ID)) &&
       !strcmp(id, var))
      {
      ret = gavl_dictionary_create();
      gavl_dictionary_copy(ret, dict);
      break;
      }
    }
  backend_registry_unlock();
  
  return ret;
  }

static void * thread_func(void * data)
  {
  gavl_time_t delay_time = GAVL_TIME_SCALE / 10; // 100 ms
  bg_backend_registry_t * reg = data;

  //  bg_backend_registry_init(reg);
  
  while(1)
    {
    backend_registry_lock();
    if(reg->do_stop)
      {
      backend_registry_unlock();
      return NULL;
      }
    backend_registry_unlock();
    
    if(!bg_backend_registry_update(reg))
      gavl_time_delay(&delay_time);
    }

  return NULL;
  }


static void bg_backend_registry_start(bg_backend_registry_t * ret)
  {
  pthread_create(&ret->th, NULL, thread_func, ret);  
  }
  
static void bg_backend_registry_stop(bg_backend_registry_t * reg)
  {
  backend_registry_lock();
  
  if(reg->do_stop)
    {
    backend_registry_unlock();
    return;
    }
  
  reg->do_stop = 1;

  backend_registry_unlock();
  
  pthread_join(reg->th, NULL);
  }


static void 
bg_backend_registry_create1(void)
  {
  int i;
  
  bg_backend_reg = calloc(1, sizeof(*bg_backend_reg));
      
  pthread_mutex_init(&bg_backend_reg->mutex, NULL);
  
  bg_backend_reg->evt_hub = bg_msg_hub_create(1);
  bg_backend_reg->evt_sink = bg_msg_hub_get_sink(bg_backend_reg->evt_hub);
  bg_backend_reg->evt_handler = bg_msg_sink_create(handle_message, bg_backend_reg, 1);

  bg_msg_hub_connect_sink(bg_backend_reg->evt_hub, bg_backend_reg->evt_handler);

  bg_backend_reg->detectors = calloc(NUM_DEV_DETECTORS, sizeof(*bg_backend_reg->detectors));
 
  /* Initialize backends */
  for(i = 0; i < NUM_DEV_DETECTORS; i++)
    {
    bg_backend_reg->detectors[i].d    = remote_dev_detectors[i];
    bg_backend_reg->detectors[i].priv = bg_backend_reg->detectors[i].d->create();
    }

  /* Initialize */
  bg_backend_registry_init(bg_backend_reg);
  
  bg_backend_registry_start(bg_backend_reg);
  

  }


static void bg_backend_registry_destroy(bg_backend_registry_t * reg)
  {
  int i;

  bg_backend_registry_stop(reg);
  
  bg_msg_hub_destroy(reg->evt_hub);
  bg_msg_sink_destroy(reg->evt_handler);
  gavl_array_free(&reg->devs);
  gavl_array_free(&reg->local_devs);


  pthread_mutex_destroy(&reg->mutex);

  
  
  /* Cleanup backends */
  for(i = 0; i < NUM_DEV_DETECTORS; i++)
    {
    if(reg->detectors[i].priv)
      reg->detectors[i].d->destroy(reg->detectors[i].priv);
    }
  free(reg->detectors);
  
  
  free(reg);
  }

bg_backend_registry_t * bg_get_backend_registry(void)
  {
  if(!bg_backend_reg)
    bg_backend_registry_create1(); 
  return bg_backend_reg;
  }

gavl_array_t * bg_backends_scan(gavl_time_t timeout)
  {
  gavl_array_t * ret;
  
  bg_get_backend_registry();
    
  if(timeout == GAVL_TIME_UNDEFINED)
    timeout = GAVL_TIME_SCALE;
  
  gavl_time_delay(&timeout);
  ret = bg_backend_registry_get();
  
  return ret;
  }
void bg_backend_register_local(const gavl_dictionary_t * dev)
  {
  gavl_dictionary_t * dict;
  gavl_value_t val;

  bg_backend_registry_t * reg;

  gavl_value_init(&val);
  dict = gavl_value_set_dictionary(&val);
  gavl_dictionary_copy(dict, dev);
  
  gavl_dictionary_set_int(dict, BG_BACKEND_LOCAL, 1);
  bg_backend_info_init(dict);
  
  reg = bg_get_backend_registry();

  backend_registry_lock();
  gavl_array_splice_val_nocopy(&reg->local_devs, -1, 0, &val);
  pthread_mutex_unlock(&reg->mutex);
  }

int bg_backend_is_local(const char * uri,
                        gavl_dictionary_t * dev)
  {
  int ret = 0;
  int i;
  
  const char * var;
  const gavl_dictionary_t * d;
  bg_backend_registry_t * reg;
  
  reg = bg_get_backend_registry();
  
  //  pthread_mutex_lock(&reg->mutex);

  for(i = 0; i < reg->local_devs.num_entries; i++)
    {
    if((d = gavl_value_get_dictionary(&reg->local_devs.entries[i])) &&
       (var = gavl_dictionary_get_string(d, GAVL_META_URI)) &&
       !strcmp(var, uri))
      {
      ret = 1;
      gavl_dictionary_copy(dev, d);
      break;
      }
    }
  
  //  pthread_mutex_unlock(&reg->mutex);

  return ret;
  }


#if defined(__GNUC__)

static void cleanup_backend() __attribute__ ((destructor));

static void cleanup_backend()
  {
  if(bg_backend_reg)
    bg_backend_registry_destroy(bg_backend_reg);
  }

#endif
