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
#include <unistd.h>
#include <errno.h>
#include <uuid/uuid.h>

#include <gavl/gavlsocket.h>

#include <gmerlin/parameter.h>
#include <gmerlin/msgqueue.h>
#include <gmerlin/state.h>

#include <gmerlin/translation.h>
#include <gmerlin/log.h>
#define LOG_DOMAIN "msgconn"

/* And entry in the routing table needs to survive
   only from the FUNC msg up to the last RESP msg */

#define ROUTING_TABLE_SIZE 32

struct bg_msg_sink_s
  {
  bg_msg_queue_t * q;
  gavl_msg_t * m;
  gavl_msg_t * m_priv;

  gavl_handle_msg_func cb;
  void * cb_data;

  char id_buf[37]; // uuid
  char * id;
  
  /*
   *  Number of messages processed in the last call of
   *  bg_msg_sink_iteration
   */
  
  int num_msg;

  /* Routing table */
  int routing_table_size;
  struct
    {
    uuid_t id;
    } routing_table[ROUTING_TABLE_SIZE];

  pthread_mutex_t rm;

  pthread_mutex_t write_mutex;
  };

static int routing_table_get(bg_msg_sink_t * sink, const char * id)
  {
  int i;
  int ret = 0;
  uuid_t uuid;
  
  if(uuid_parse(id, uuid))
    return 0;
  
  pthread_mutex_lock(&sink->rm);

  for(i = 0; i < sink->routing_table_size; i++)
    {
    if(!uuid_compare(uuid, sink->routing_table[i].id))
      {
      ret = 1;
      break;
      }
    }
  
  pthread_mutex_unlock(&sink->rm);
  return ret;
  }

static void routing_table_put(bg_msg_sink_t * sink, const char * id)
  {
  int idx;
  uuid_t uuid;
  int i;
  
  if(uuid_parse(id, uuid))
    return;
  
  pthread_mutex_lock(&sink->rm);

  idx = -1;

  for(i = 0; i < sink->routing_table_size; i++)
    {
    if(!uuid_compare(uuid, sink->routing_table[i].id))
      {
      idx = i;
      break;
      }
    }

  if(idx >= 0)
    {
    /* Remove */

    if(idx < sink->routing_table_size-1)
      {
      memmove(&sink->routing_table[idx],
              &sink->routing_table[idx+1],
              (sink->routing_table_size-1-idx) * sizeof(&sink->routing_table[0]));
      }
    sink->routing_table_size--;
    }

  if(sink->routing_table_size == ROUTING_TABLE_SIZE)
    sink->routing_table_size--;

  memmove(&sink->routing_table[0],
          &sink->routing_table[1],
          (sink->routing_table_size) * sizeof(&sink->routing_table[0]));
  
  uuid_copy(sink->routing_table[0].id, uuid);
  sink->routing_table_size++;
  
  pthread_mutex_unlock(&sink->rm);
  }
  
gavl_msg_t * bg_msg_sink_get(bg_msg_sink_t * sink)
  {
  if(sink->q)
    sink->m = bg_msg_queue_lock_write(sink->q);
  else
    {
    pthread_mutex_lock(&sink->write_mutex);

    if(!sink->m_priv)
      sink->m_priv = gavl_msg_create();

    gavl_msg_free(sink->m_priv);
    sink->m = sink->m_priv;
    }
  
  return sink->m;
  }

int bg_msg_sink_get_num(bg_msg_sink_t * sink)
  {
  return sink->num_msg;
  }

void bg_msg_sink_put(bg_msg_sink_t * sink, gavl_msg_t * msg)
  {
  if(sink->q)
    {
    if(!sink->m)
      bg_msg_sink_get(sink);
    
    if(msg != sink->m)
      gavl_msg_copy(sink->m, msg);
    bg_msg_queue_unlock_write(sink->q);
    }
  else
    {
    if(sink->cb)
      sink->cb(sink->cb_data, msg);
    
    pthread_mutex_unlock(&sink->write_mutex);
    }
  sink->m = NULL;
  }
  
bg_msg_sink_t * bg_msg_sink_create(gavl_handle_msg_func cb, void * cb_data, int sync)
  {
  bg_msg_sink_t * ret = calloc(1, sizeof(*ret));

  ret->cb      = cb;
  ret->cb_data = cb_data;
  if(!sync)
    ret->q = bg_msg_queue_create();

  pthread_mutex_init(&ret->rm, NULL);
  pthread_mutex_init(&ret->write_mutex, NULL);

  return ret;
  }

void bg_msg_sink_destroy(bg_msg_sink_t * sink)
  {
  if(sink->q)
    bg_msg_queue_destroy(sink->q);
  if(sink->m_priv)
    gavl_msg_destroy(sink->m_priv);

  pthread_mutex_destroy(&sink->rm);
  pthread_mutex_destroy(&sink->write_mutex);
  
  free(sink);
  }

int bg_msg_sink_handle(void * sink, gavl_msg_t * msg)
  {
  bg_msg_sink_t * s = sink;
  bg_msg_sink_put(s, msg);
  return 1;
  }


/* For asynchronous sinks */
int bg_msg_sink_iteration(bg_msg_sink_t * sink)
  {
  gavl_msg_t * m;
  int result;
  sink->num_msg = 0;

  if(!sink->q)
    {
    // gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "bg_msg_sink_iteration called for synchronous sink");
    /* Do nothing */
    return 1;
    }
  
  while((m = bg_msg_queue_try_lock_read(sink->q)))
    {
    if((m->NS == GAVL_MSG_NS_GENERIC) &&
       (m->ID == GAVL_CMD_QUIT))
      {
      gavl_log(GAVL_LOG_DEBUG, LOG_DOMAIN,
             "bg_msg_sink_iteration: Got quit command");
      bg_msg_queue_unlock_read(sink->q);
      return 0;
      }
    if(sink->cb)
      {
      gavl_msg_t m1;
      gavl_msg_init(&m1);
      gavl_msg_copy(&m1, m);
      bg_msg_queue_unlock_read(sink->q);
      result = sink->cb(sink->cb_data, &m1);
      gavl_msg_free(&m1);
      }
    else
      bg_msg_queue_unlock_read(sink->q);

    sink->num_msg++;
    
    if(!result)
      return 0;
    }
  return 1;
  }

int bg_msg_sink_peek(bg_msg_sink_t * sink, uint32_t * id, uint32_t * ns)
  {
  if(!sink->q) // Return gracefully
    return 0;
  return bg_msg_queue_peek(sink->q, id, ns);
  }

gavl_msg_t * bg_msg_sink_get_read_block(bg_msg_sink_t * sink)
  {
  if(!sink->q) // Return gracefully
    return NULL;
  return bg_msg_queue_lock_read(sink->q);
  }

gavl_msg_t * bg_msg_sink_get_read(bg_msg_sink_t * sink)
  {
  if(!sink->q) // Return gracefully
    return NULL;
  return bg_msg_queue_try_lock_read(sink->q);
  }

void bg_msg_sink_done_read(bg_msg_sink_t * sink)
  {
  if(!sink->q) // Return gracefully
    return;
  bg_msg_queue_unlock_read(sink->q);
  }

void bg_msg_sink_set_id(bg_msg_sink_t * sink, const char * id)
  {
  memcpy(sink->id_buf, id, 37);
  sink->id = sink->id_buf;
  }

int bg_msg_sink_has_id(bg_msg_sink_t * sink, const char * id)
  {
  if(!sink->id || !id || !strcmp(sink->id, "*") || !strcmp(id, "*") || (sink->id && !strcmp(id, sink->id)))
    return 1;
  
  return routing_table_get(sink, id);
  }


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

static int put_msg(void * data, gavl_msg_t * msg)
  {
  int i;
  bg_msg_hub_t * h = data;
  const char * id;
  
  if((msg->NS == BG_MSG_NS_STATE) &&
     (msg->ID == BG_MSG_STATE_CHANGED))
    bg_msg_get_state(msg, NULL, NULL, NULL, NULL, &h->state);

  id = gavl_msg_get_client_id(msg);
  
  pthread_mutex_lock(&h->mutex);

  for(i = 0; i < h->num_sinks; i++)
    {
    if(!id || bg_msg_sink_has_id(h->sinks[i], id))
      {
      gavl_msg_t * m1 = bg_msg_sink_get(h->sinks[i]);
      gavl_msg_copy(m1, msg);
      bg_msg_sink_put(h->sinks[i], m1);

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
  ret->sink = bg_msg_sink_create(put_msg, ret, sync);
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
  //  fprintf(stderr, "bg_msg_hub_connect_sink: %p %p\n", h, sink);
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
    bg_msg_sink_put(h->sinks[i], msg);
    }
  pthread_mutex_unlock(&h->mutex);
  }

/* Controllable and control */

void
bg_controllable_init(bg_controllable_t * ctrl,
                     bg_msg_sink_t * cmd_sink, // Owned
                     bg_msg_hub_t * evt_hub)   // Owned
  {
  memset(ctrl, 0, sizeof(*ctrl));
  ctrl->cmd_sink = cmd_sink;
  ctrl->evt_hub  = evt_hub;
  ctrl->evt_sink = bg_msg_hub_get_sink(ctrl->evt_hub);
  }

void
bg_controllable_cleanup(bg_controllable_t * ctrl)   // Owned
  {
  if(ctrl->priv && ctrl->cleanup)
    ctrl->cleanup(ctrl->priv);

  if(ctrl->cmd_sink)
    bg_msg_sink_destroy(ctrl->cmd_sink);
  if(ctrl->evt_hub)
    bg_msg_hub_destroy(ctrl->evt_hub);
  memset(ctrl, 0, sizeof(*ctrl));
  }

void
bg_controllable_connect(bg_controllable_t * ctrl,
                        bg_control_t * c)
  {
  if(c->evt_sink)
    bg_msg_hub_connect_sink(ctrl->evt_hub, c->evt_sink);
  c->ctrl = ctrl;
  }


void
bg_controllable_disconnect(bg_controllable_t * ctrl,
                           bg_control_t * c)
  {
  if(c->evt_sink)
    bg_msg_hub_disconnect_sink(ctrl->evt_hub, c->evt_sink);
  c->ctrl = NULL;
  }

/* Handle a message of a control.
   Here, we need to do the routing */

static int handle_message_cmd(void * priv, gavl_msg_t * msg)
  {
  const char * client_id;
  bg_control_t * c = priv;

  if(!c->ctrl)
    return 1;
  
  if(!(client_id = gavl_msg_get_client_id(msg)))
    {
    if(c->cmd_sink->id)
      gavl_msg_set_client_id(msg, c->cmd_sink->id);
    }
  else
    {
    /* Update routing table */
    routing_table_put(c->evt_sink, client_id);
    }
  
  if(c->ctrl && c->ctrl->cmd_sink)
    {
    gavl_msg_t * msg1 = bg_msg_sink_get(c->ctrl->cmd_sink);
    gavl_msg_copy(msg1, msg);
    bg_msg_sink_put(c->ctrl->cmd_sink, msg1);
    }
  return 1;
  }

void bg_control_init(bg_control_t * c,
                     bg_msg_sink_t * evt_sink)
  {
  uuid_t u;
  
  memset(c, 0, sizeof(*c));

  uuid_generate(u);
  uuid_unparse(u, c->id);
  
  c->evt_sink = evt_sink;
  c->cmd_sink = bg_msg_sink_create(handle_message_cmd, c, 1);

  bg_msg_sink_set_id(c->evt_sink, c->id);
  bg_msg_sink_set_id(c->cmd_sink, c->id);
  }

void bg_control_cleanup(bg_control_t * c)
  {
  if(c->priv && c->cleanup)
    c->cleanup(c->priv);
  if(c->evt_sink)
    bg_msg_sink_destroy(c->evt_sink);
  if(c->cmd_sink)
    bg_msg_sink_destroy(c->cmd_sink);
  memset(c, 0, sizeof(*c));
  }


int bg_controllable_call_function(bg_controllable_t * c, gavl_msg_t * func,
                                  gavl_handle_msg_func cb, void * data, int timeout)
  {
  int result = 0;
  gavl_time_t delay_time = GAVL_TIME_SCALE / 50; // 20ms
  bg_control_t ctrl;
  gavl_timer_t * timer = gavl_timer_create();

  memset(&ctrl, 0, sizeof(ctrl));

  bg_msg_add_function_tag(func);

  bg_control_init(&ctrl, bg_msg_sink_create(cb, data, 0));
  bg_controllable_connect(c, &ctrl);
  
  bg_msg_sink_put(ctrl.cmd_sink, func);
  
  gavl_timer_start(timer);
  while(1)
    {
    if(!bg_msg_sink_iteration(ctrl.evt_sink))
      {
      result = 1;
      break;
      }
    if((gavl_timer_get(timer)*1000) / GAVL_TIME_SCALE > timeout)
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Timeout expired when waiting for function result");
      break;
      }
    gavl_time_delay(&delay_time);
    }
  
  gavl_timer_destroy(timer);

  bg_controllable_disconnect(c, &ctrl);
  bg_control_cleanup(&ctrl);
  
  return result;
  }

/* */

void bg_msg_add_function_tag(gavl_msg_t * msg)
  {
  char uuid_str[37];
  uuid_t uuid;

  if(gavl_dictionary_get(&msg->header, BG_FUNCTION_TAG))
    return;
  
  uuid_generate(uuid);
  uuid_unparse(uuid, uuid_str);
  gavl_dictionary_set_string(&msg->header, BG_FUNCTION_TAG, uuid_str);
  }

gavl_dictionary_t * 
bg_function_push(gavl_array_t * arr, gavl_msg_t * msg)
  {
  gavl_value_t val;
  
  
  gavl_dictionary_t * ret;

  gavl_dictionary_t * dict;
  
  bg_msg_add_function_tag(msg);
  
  gavl_value_init(&val);
  dict = gavl_value_set_dictionary(&val);
  gavl_dictionary_set_string(dict, BG_FUNCTION_TAG, gavl_dictionary_get_string(&msg->header, BG_FUNCTION_TAG) );
  ret = gavl_dictionary_get_dictionary_create(dict, "data");
  gavl_array_splice_val_nocopy(arr, -1, 0, &val);
  return ret;
  }

gavl_dictionary_t *
bg_function_get(gavl_array_t * arr, const gavl_msg_t * msg, int * idxp)
  {
  int i;
  gavl_dictionary_t * func;
  const char * ft;
  const char * functag = gavl_dictionary_get_string(&msg->header, BG_FUNCTION_TAG);
  if(!functag)
    return NULL;
  
  for(i = 0; i < arr->num_entries; i++)
    {
    if((func = gavl_value_get_dictionary_nc(&arr->entries[i])))
      {
      if(!(ft = gavl_dictionary_get_string(func, BG_FUNCTION_TAG)) ||
         strcmp(ft, functag))
        continue;
      
      if(idxp)
        *idxp = i;
      
      return gavl_dictionary_get_dictionary_nc(func, "data");
      }
    }

  if(idxp)
    *idxp = -1;
  return NULL;
  }
