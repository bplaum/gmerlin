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
#include <gmerlin/bgmsg.h>
#include <gmerlin/state.h>

#include <gmerlin/translation.h>
#include <gmerlin/log.h>
#define LOG_DOMAIN "msgsink"

#include <msgconn.h>

/* 
typedef struct
  {
  gavl_msg_t ** buf;
  int len;
  int alloc;
  } msg_buf_t;
*/
  
static void msg_buf_push(msg_buf_t * buf, gavl_msg_t * msg)
  {
  if(buf->len + 1 >= buf->alloc)
    {
    buf->alloc = buf->len + 32;
    buf->buf = realloc(buf->buf, buf->alloc * sizeof(*buf->buf));
    memset(buf->buf + buf->len, 0, (buf->alloc - buf->len) * sizeof(*buf->buf));
    }
  buf->buf[buf->len] = msg;
  buf->len++;
  }

static gavl_msg_t * msg_buf_shift(msg_buf_t * buf)
  {
  gavl_msg_t * ret;
  if(!buf->len)
    return NULL;

  ret = buf->buf[0];
  buf->len--;

  if(buf->len > 0)
    memmove(buf->buf, buf->buf + 1, buf->len * sizeof(*buf->buf));
  
  buf->buf[buf->len] = NULL;
  
  return ret;
  }

static gavl_msg_t * msg_buf_pop(msg_buf_t * buf)
  {
  gavl_msg_t * ret;
  if(!buf->len)
    return NULL;
  
  buf->len--;

  ret = buf->buf[buf->len];
  buf->buf[buf->len] = NULL;

  return ret;
  }

static void msg_buf_free(msg_buf_t * buf)
  {
  int i;
  for(i = 0; i < buf->len; i++)
    gavl_msg_destroy(buf->buf[i]);
  if(buf->buf)
    free(buf->buf);
  }

static gavl_msg_t * queue_get_write(msg_queue_t * q)
  {
  gavl_msg_t * ret;
  
  if((ret = msg_buf_pop(&q->pool)))
    gavl_msg_free(ret);
  else
    ret = gavl_msg_create();
  
  return ret;
  }

static void queue_done_write(msg_queue_t * q, gavl_msg_t * msg)
  {
  if(q->queue.len && bg_msg_merge(q->queue.buf[q->queue.len-1], msg))
    {
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Merged messages");
    msg_buf_push(&q->pool, msg);
    }
  else
    msg_buf_push(&q->queue, msg);
  }

static gavl_msg_t * queue_get_read(msg_queue_t * q)
  {
  return msg_buf_shift(&q->queue);
  }

static void queue_done_read(msg_queue_t * q, gavl_msg_t * msg)
  {
  return msg_buf_push(&q->pool, msg);
  }

static void queue_destroy(msg_queue_t * q)
  {
  msg_buf_free(&q->queue);
  msg_buf_free(&q->pool);
  free(q);
  }

static msg_queue_t * queue_create()
  {
  msg_queue_t * ret = calloc(1, sizeof(*ret));
  return ret;
  }

/* And entry in the routing table needs to survive
   only from the FUNC msg up to the last RESP msg */

int bg_msg_routing_table_get(bg_msg_sink_t * sink, const char * id)
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

void bg_msg_routing_table_put(bg_msg_sink_t * sink, const char * id)
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

int bg_msg_sink_get_num(bg_msg_sink_t * sink)
  {
  return sink->num_msg;
  }

gavl_msg_t * bg_msg_sink_get(bg_msg_sink_t * sink)
  {
  pthread_mutex_lock(&sink->write_mutex);
  if(sink->queue)
    sink->m = queue_get_write(sink->queue);
  else
    {
    if(!sink->m_priv)
      sink->m_priv = gavl_msg_create();

    gavl_msg_free(sink->m_priv);
    sink->m = sink->m_priv;
    }
  return sink->m;
  }


void bg_msg_sink_put(bg_msg_sink_t * sink)
  {
  if(!sink->m)
    {
    /* Error */
    fprintf(stderr, "Buuug: bg_msg_sink_put called without calling bg_msg_sink_get before");
    return;
    }
  
  if(sink->queue)
    {
    queue_done_write(sink->queue, sink->m);
    }
  else
    {
    if(sink->cb)
      sink->cb(sink->cb_data, sink->m);
    }
  sink->m = NULL;
  pthread_mutex_unlock(&sink->write_mutex);
  }

void bg_msg_sink_put_copy(bg_msg_sink_t * sink, const gavl_msg_t * msg)
  {
  gavl_msg_t * msg1 = bg_msg_sink_get(sink);
  gavl_msg_copy(msg1, msg);
  bg_msg_sink_put(sink);
  }

bg_msg_sink_t * bg_msg_sink_create(gavl_handle_msg_func cb, void * cb_data, int sync)
  {
  bg_msg_sink_t * ret = calloc(1, sizeof(*ret));

  ret->cb      = cb;
  ret->cb_data = cb_data;
  if(!sync)
    ret->queue = queue_create();
    //    ret->q = bg_msg_queue_create();

  pthread_mutex_init(&ret->rm, NULL);
  pthread_mutex_init(&ret->write_mutex, NULL);

  return ret;
  }

void bg_msg_sink_destroy(bg_msg_sink_t * sink)
  {
  if(sink->queue)
    queue_destroy(sink->queue);
  if(sink->m_priv)
    gavl_msg_destroy(sink->m_priv);

  pthread_mutex_destroy(&sink->rm);
  pthread_mutex_destroy(&sink->write_mutex);
  
  free(sink);
  }

/* Interface for the old callback based method */
// gavl_handle_msg_func

int bg_msg_sink_handle(void * sink, gavl_msg_t * msg)
  {
  bg_msg_sink_t * s = sink;
  bg_msg_sink_put_copy(s, msg);
  return 1;
  }

gavl_msg_t * bg_msg_sink_get_read(bg_msg_sink_t * sink)
  {
  gavl_msg_t * ret;
  
  if(!sink->queue)
    {
    gavl_dprintf("BUG: bg_msg_sink_get_read called for synchronous sink\n");
    return NULL;
    }

  pthread_mutex_lock(&sink->write_mutex);
  ret = queue_get_read(sink->queue);
  pthread_mutex_unlock(&sink->write_mutex);

  return ret;
  }

void bg_msg_sink_done_read(bg_msg_sink_t * sink, gavl_msg_t * m)
  {
  pthread_mutex_lock(&sink->write_mutex);
  queue_done_read(sink->queue, m); // Prevent memory leak
  pthread_mutex_unlock(&sink->write_mutex);
  }


/* For asynchronous sinks */
int bg_msg_sink_iteration(bg_msg_sink_t * sink)
  {
  gavl_msg_t * m;
  int result = 1;
  
  sink->num_msg = 0;

  /* Do nothing for synchronous queues */
  if(!sink->queue)
    return 1;
  
  while(1)
    {
    pthread_mutex_lock(&sink->write_mutex);
    m = queue_get_read(sink->queue);
    pthread_mutex_unlock(&sink->write_mutex);

    if(!m)
      break;
    
    /* Check for quit command */
    if((m->NS == GAVL_MSG_NS_GENERIC) &&
       (m->ID == GAVL_CMD_QUIT))
      {
      gavl_log(GAVL_LOG_DEBUG, LOG_DOMAIN,
               "bg_msg_sink_iteration: Got quit command");

      pthread_mutex_lock(&sink->write_mutex);
      queue_done_read(sink->queue, m); // Prevent memory leak
      pthread_mutex_unlock(&sink->write_mutex);

      sink->num_msg++;

      result = 0;
      
      break;
      }
    
    /* Call callback function */

    if(result && sink->cb)
      result = sink->cb(sink->cb_data, m);

    pthread_mutex_lock(&sink->write_mutex);
    queue_done_read(sink->queue, m);
    pthread_mutex_unlock(&sink->write_mutex);
    
    if(!result)
      break;

    sink->num_msg++;
    }
  
  return result;
#if 0
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
#endif
  }

void bg_msg_sink_set_id(bg_msg_sink_t * sink, const char * id)
  {
  memcpy(sink->id_buf, id, 37);
  sink->id = sink->id_buf;
  }

int bg_msg_sink_has_id(bg_msg_sink_t * sink, const char * id)
  {
  if(!id)
    return 1;
  
  if(sink->id && (!strcmp(id, sink->id) || !strcmp(sink->id, "*")))
    return 1;
  
  return bg_msg_routing_table_get(sink, id);
  }
