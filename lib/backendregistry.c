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

#define REMOVE_DUPLICATES

bg_backend_registry_t * bg_backend_reg = NULL;

static void backend_registry_lock()
  {
  //  fprintf(stderr, "backend_registry_lock...");
  pthread_mutex_lock(&bg_backend_reg->mutex);
  //  fprintf(stderr, "done\n");
  }

static void backend_registry_unlock()
  {
  //  fprintf(stderr, "backend_registry_unlock\n");
  pthread_mutex_unlock(&bg_backend_reg->mutex);
  }

bg_msg_hub_t * bg_backend_registry_get_evt_hub()
  {
  bg_backend_registry_t * reg = bg_get_backend_registry();
  return reg->evt_hub;
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

static int bg_backend_registry_update(bg_backend_registry_t * reg)
  {
  int ret = 0;

  ret += bg_ssdp_update(reg->ssdp);
#ifdef HAVE_DBUS
  ret += bg_dbus_detector_update(reg->dbus);
#endif
  return ret;
  }


static void * thread_func(void * data)
  {
  int result;
  gavl_time_t delay_time = GAVL_TIME_SCALE / 100; // 10 ms
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
    result = bg_backend_registry_update(reg);
    backend_registry_unlock();
    
    if(!result) // idle
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
  bg_backend_reg = calloc(1, sizeof(*bg_backend_reg));
      
  pthread_mutex_init(&bg_backend_reg->mutex, NULL);
  
  bg_backend_reg->evt_hub = bg_msg_hub_create(1);
  bg_backend_reg->evt_sink = bg_msg_hub_get_sink(bg_backend_reg->evt_hub);

  bg_backend_reg->ssdp = bg_ssdp_create();

#ifdef HAVE_DBUS
  bg_backend_reg->dbus = bg_dbus_detector_create();
#endif  
  /* Start thread */
  bg_backend_registry_start(bg_backend_reg);
  

  }


static void bg_backend_registry_destroy(bg_backend_registry_t * reg)
  {
  bg_backend_registry_stop(reg);

  bg_ssdp_destroy(reg->ssdp);

#ifdef HAVE_DBUS
  bg_dbus_detector_destroy(reg->dbus);
#endif
  
  bg_msg_hub_destroy(reg->evt_hub);
  gavl_array_free(&reg->devs);
  gavl_array_free(&reg->local_devs);
  
  pthread_mutex_destroy(&reg->mutex);
  
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

  //  fprintf(stderr, "bg_backend_register_local:\n");
  //  gavl_dictionary_dump(dev, 2);
  
  reg = bg_get_backend_registry();
  
  gavl_value_init(&val);
  dict = gavl_value_set_dictionary(&val);
  gavl_dictionary_copy(dict, dev);
  
  gavl_dictionary_set_int(dict, BG_BACKEND_LOCAL, 1);
  bg_set_backend_id(dict);
  
  //  gavl_dictionary_dump(dict, 2);
  
  backend_registry_lock();
  gavl_array_splice_val_nocopy(&reg->local_devs, -1, 0, &val);
  backend_registry_unlock();
  }

gavl_dictionary_t * bg_backend_by_str(const char * key, const char * str, int local, int * idx)
  {
  gavl_array_t * arr = local ? &bg_backend_reg->local_devs : &bg_backend_reg->devs;

  int i;
  gavl_dictionary_t * ret = 0;
  const char * test_str;
  for(i = 0; i < arr->num_entries; i++)
    {
    if((ret = gavl_value_get_dictionary_nc(&arr->entries[i])) &&
       (test_str = gavl_dictionary_get_string(ret, key)) &&
       !strcmp(test_str, str))
      {
      if(idx)
        *idx = i;
      return ret;
      }
    }
  if(idx)
    *idx = -1;
  return NULL;
  }

/* TODO?: Update state */

void bg_backend_add_remote(const gavl_dictionary_t * b)
  {
  gavl_value_t new_val;
  gavl_msg_t * msg;
  const char * uri;
#ifdef REMOVE_DUPLICATES
  const char * id;
  const char * protocol;
  const gavl_dictionary_t * dev;
#endif
  
  if(!(uri = gavl_dictionary_get_string(b, GAVL_META_URI)))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Remote backed has no URI");
    return;
    }
  
  if(bg_backend_by_str(GAVL_META_URI, uri, 0, NULL))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Backend %s already there", uri);
    return;
    }
#ifdef REMOVE_DUPLICATES
  id       = gavl_dictionary_get_string(b, GAVL_META_ID);
  protocol = gavl_dictionary_get_string(b, BG_BACKEND_PROTOCOL);

  //  fprintf(stderr, "remove_duplicates %s %s\n", id, protocol);
  
  if(id && protocol)
    {
    if(!strcmp(protocol, "gmerlin"))
      {
      while((dev = bg_backend_by_str(GAVL_META_ID, id, 0, NULL)))
        bg_backend_del_remote(gavl_dictionary_get_string(dev, GAVL_META_URI));
      }
    else
      {
      /* Ignore this if a gmerlin device is already here */
      if((dev = bg_backend_by_str(GAVL_META_ID, id, 0, NULL)) &&
         (protocol = gavl_dictionary_get_string(dev, BG_BACKEND_PROTOCOL)) &&
         !strcmp(protocol, "gmerlin"))
        return;
      }
    }
  
#endif
  
  gavl_value_init(&new_val);
  gavl_dictionary_copy(gavl_value_set_dictionary(&new_val), b);
  gavl_array_splice_val_nocopy(&bg_backend_reg->devs, -1, 0, &new_val);

  msg = bg_msg_sink_get(bg_backend_reg->evt_sink);
  gavl_msg_set_id_ns(msg, BG_MSG_ADD_BACKEND, BG_MSG_NS_BACKEND);
  gavl_msg_set_arg_dictionary(msg, 0, b);
  bg_msg_sink_put(bg_backend_reg->evt_sink, msg);
  
  }

void bg_backend_del_remote(const char * uri)
  {
  int idx = 0;
  gavl_msg_t * msg;
  
  if(!bg_backend_by_str(GAVL_META_URI, uri, 0, &idx))
    return;

  msg = bg_msg_sink_get(bg_backend_reg->evt_sink);
  gavl_msg_set_id_ns(msg, BG_MSG_DEL_BACKEND, BG_MSG_NS_BACKEND);
  gavl_msg_set_arg_string(msg, 0, uri);
  bg_msg_sink_put(bg_backend_reg->evt_sink, msg);
  
  gavl_array_splice_val(&bg_backend_reg->devs, idx, 1, NULL);
  }




#if defined(__GNUC__)

static void cleanup_backend() __attribute__ ((destructor));

static void cleanup_backend()
  {
  if(bg_backend_reg)
    bg_backend_registry_destroy(bg_backend_reg);
  }

#endif
