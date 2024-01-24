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
#include <pthread.h>
#include <uuid/uuid.h>

#include <gmerlin/parameter.h>
#include <gmerlin/bgmsg.h>
#include <gmerlin/state.h>

#include <gmerlin/translation.h>
#include <gmerlin/log.h>
#define LOG_DOMAIN "msghub"

#include <msgconn.h>

/* msg hub: Sink, which broadcasts to other sinks. */

struct bg_msg_hub_s
  {
  bg_msg_sink_t ** sinks;
  int num_sinks;
  int sinks_alloc;

  bg_msg_sink_t * sink;
  
  pthread_mutex_t mutex;
  void (*connect_cb)(bg_msg_sink_t * s, void * data);
  void * connect_cb_data;
  
  gavl_dictionary_t state;
  };

const gavl_dictionary_t * bg_msg_hub_get_state(bg_msg_hub_t * h)
  {
  return &h->state;
  }


void bg_msg_hub_set_connect_cb(bg_msg_hub_t * h,
                               void (*cb)(bg_msg_sink_t * s,
                                          void * data),
                               void * data)
  {
  h->connect_cb      = cb;
  h->connect_cb_data = data;
  }

static int put_msg_hub(void * data, gavl_msg_t * msg)
  {
  int i;
  bg_msg_hub_t * h = data;
  const char * id;
  
  if((msg->NS == BG_MSG_NS_STATE) &&
     (msg->ID == BG_MSG_STATE_CHANGED))
    gavl_msg_get_state(msg, NULL, NULL, NULL, NULL, &h->state);

  id = gavl_msg_get_client_id(msg);
  
  pthread_mutex_lock(&h->mutex);

  for(i = 0; i < h->num_sinks; i++)
    {
    if(!id || bg_msg_sink_has_id(h->sinks[i], id))
      {
      bg_msg_sink_put_copy(h->sinks[i], msg);

      if(id)
        break; // Assuming unique clients
      }
    }
  pthread_mutex_unlock(&h->mutex);
  return 1;
  }

bg_msg_hub_t * bg_msg_hub_create(int sync)
  {
  bg_msg_hub_t * ret = calloc(1, sizeof(*ret));
  ret->sink = bg_msg_sink_create(put_msg_hub, ret, sync);
  pthread_mutex_init(&ret->mutex, NULL);

  return ret;
  }

void
bg_msg_hub_destroy(bg_msg_hub_t * h)
  {
  pthread_mutex_destroy(&h->mutex);
  if(h->sink)
    bg_msg_sink_destroy(h->sink);

  if(h->sinks)
    free(h->sinks);

  gavl_dictionary_free(&h->state);
  
  free(h);
  }

void bg_msg_hub_connect_sink(bg_msg_hub_t * h, bg_msg_sink_t * sink)
  {
  pthread_mutex_lock(&h->mutex);

  if(h->num_sinks + 1 > h->sinks_alloc)
    {
    h->sinks_alloc += 16;
    h->sinks = realloc(h->sinks, sizeof(*h->sinks) * h->sinks_alloc);
    }
  h->sinks[h->num_sinks] = sink;
  h->num_sinks++;

  /* Send initial messages */
  if(h->state.num_entries > 0)
    bg_state_apply(&h->state, sink, BG_MSG_STATE_CHANGED);
  
  if(h->connect_cb)
    h->connect_cb(sink, h->connect_cb_data);
  
  pthread_mutex_unlock(&h->mutex);
  }


void bg_msg_hub_disconnect_sink(bg_msg_hub_t * h, bg_msg_sink_t * sink)
  {
  int i;

  //  fprintf(stderr, "bg_msg_hub_disconnect_sink: %p %p\n", h, sink);
  
  pthread_mutex_lock(&h->mutex);

  for(i = 0; i < h->num_sinks; i++)
    {
    if(h->sinks[i] == sink)
      {
      break;
      }
    }
  if(i < h->num_sinks)
    {
    if(i < h->num_sinks - 1)
      memmove(h->sinks + i, h->sinks + i + 1,
              sizeof(*h->sinks) * (h->num_sinks - 1 - i));
    h->num_sinks--;
    }
  else
    fprintf(stderr, "bg_msg_hub_disconnect_sink: No such sink %p\n", sink);
  pthread_mutex_unlock(&h->mutex);
  }

bg_msg_sink_t * bg_msg_hub_get_sink(bg_msg_hub_t * h)
  {
  return h->sink;
  }

void bg_msg_hub_send_cb(bg_msg_hub_t * h,
                        void (*set_message)(gavl_msg_t * message,
                                            const void * data),
                        const void * data)
  {
  int i;
  gavl_msg_t * msg;
  pthread_mutex_lock(&h->mutex);
  for(i = 0; i < h->num_sinks; i++)
    {
    msg = bg_msg_sink_get(h->sinks[i]);
    set_message(msg, data);
    bg_msg_sink_put(h->sinks[i]);
    }
  pthread_mutex_unlock(&h->mutex);
  }
