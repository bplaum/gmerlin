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

#include <errno.h>


#include <stdlib.h>

#include <string.h>
#include <stdio.h>
#include <pthread.h>


#include <gmerlin/bg_sem.h>
#include <gmerlin/parameter.h>
#include <gmerlin/msgqueue.h>

#include <gmerlin/utils.h>
#include <gmerlin/translation.h>
#include <gmerlin/log.h>
#define LOG_DOMAIN "msgqueue"

typedef struct msg_s msg_t;

struct msg_s
  {
  gavl_msg_t * m;

  sem_t produced;
  msg_t * prev;
  msg_t * next;
  };

static msg_t * msg_create()
  {
  msg_t * ret = calloc(1, sizeof(*ret));
  sem_init(&ret->produced, 0, 0);
  ret->m = gavl_msg_create();
  return ret;
  };

static void msg_destroy(msg_t * m)
  {
  gavl_msg_destroy(m->m);
  sem_destroy(&m->produced);
  free(m);
  }

struct bg_msg_queue_s
  {
  msg_t * msg_input;
  msg_t * msg_last_input;
  msg_t * msg_output;
  msg_t * msg_last;

  pthread_mutex_t chain_mutex;
  pthread_mutex_t write_mutex;
  
  int num_messages; /* Number of total messages */
  };

bg_msg_queue_t * bg_msg_queue_create()
  {
  bg_msg_queue_t * ret;
  ret = calloc(1, sizeof(*ret));
  
  /* Allocate at least 2 messages */
  
  ret->msg_output       = msg_create();
  ret->msg_output->next = msg_create();
  
  /* Set in- and output messages */

  ret->msg_input = ret->msg_output;
  ret->msg_last  = ret->msg_output->next;
  
  /* Initialize chain mutex */

  pthread_mutex_init(&ret->chain_mutex, NULL);
  pthread_mutex_init(&ret->write_mutex, NULL);
  
  return ret;
  }

void bg_msg_queue_destroy(bg_msg_queue_t * m)
  {
  msg_t * tmp_message;
  while(m->msg_output)
    {
    tmp_message = m->msg_output->next;
    msg_destroy(m->msg_output);
    m->msg_output = tmp_message;
    }
  free(m);
  }

/* Lock message queue for reading, block until something arrives */

gavl_msg_t * bg_msg_queue_lock_read(bg_msg_queue_t * m)
  {
  while(sem_wait(&m->msg_output->produced) == -1)
    {
    if(errno != EINTR)
      return NULL;
    }
  return m->msg_output->m;
  
  // sem_ret->msg_output
  }

gavl_msg_t * bg_msg_queue_try_lock_read(bg_msg_queue_t * m)
  {
  if(!sem_trywait(&m->msg_output->produced))
    return m->msg_output->m;
  else
    return NULL;
  }

int bg_msg_queue_peek(bg_msg_queue_t * m, uint32_t * id, uint32_t * ns)
  {
  int sem_val;
  sem_getvalue(&m->msg_output->produced, &sem_val);
  if(sem_val)
    {
    if(id)
      *id = m->msg_output->m->ID;
    if(ns)
      *ns = m->msg_output->m->NS;
    return 1;
    }
  else
    return 0;
  }

void bg_msg_queue_unlock_read(bg_msg_queue_t * m)
  {
  msg_t * old_out_message;

  pthread_mutex_lock(&m->chain_mutex);
  old_out_message = m->msg_output;
  
  gavl_msg_free(old_out_message->m);
  
  m->msg_output = m->msg_output->next;
  m->msg_last->next = old_out_message;
  m->msg_last = m->msg_last->next;
  m->msg_last->next = NULL;

  pthread_mutex_unlock(&m->chain_mutex);
  }

/*
 *  Lock queue for writing
 */

gavl_msg_t * bg_msg_queue_lock_write(bg_msg_queue_t * m)
  {
  pthread_mutex_lock(&m->write_mutex);
  return m->msg_input->m;
  }

void bg_msg_queue_unlock_write(bg_msg_queue_t * m)
  {
  pthread_mutex_lock(&m->chain_mutex);

  if(!m->msg_last_input ||
     (m->msg_last_input == m->msg_output) ||
     !bg_msg_merge(m->msg_last_input->m, m->msg_input->m))
    {
    if(!m->msg_input->next)
      {
      m->msg_input->next = msg_create();
      m->msg_last = m->msg_input->next;
      }
    m->msg_last_input = m->msg_input;
    m->msg_input = m->msg_input->next;
    sem_post(&m->msg_last_input->produced);
    }
  else
    {
    gavl_msg_free(m->msg_input->m);
    gavl_msg_init(m->msg_input->m);
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Merged messages");
    }
  pthread_mutex_unlock(&m->chain_mutex);
  pthread_mutex_unlock(&m->write_mutex);
  }
