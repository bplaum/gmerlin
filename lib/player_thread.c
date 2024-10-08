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



#include <gmerlin/bgthread.h>

#include <pthread.h>

#include <gmerlin/log.h>
#define LOG_DOMAIN "player.thread"

#include <stdlib.h>
#include <stdio.h>

/* Binary semaphores: Like POSIX semaphores but the
   value can only be 1 or 0 */

typedef struct
  {
  int count;
  int nwaiting;
  pthread_mutex_t lock;
  pthread_cond_t cond;
  } bin_sem_t;

static void bin_sem_init(bin_sem_t * s)
  {
  pthread_mutex_init(&s->lock, NULL);
  pthread_cond_init(&s->cond, NULL);
  }

static void bin_sem_destroy(bin_sem_t * s)
  {
  pthread_mutex_destroy(&s->lock);
  pthread_cond_destroy(&s->cond);
  }

static void bin_sem_wait(bin_sem_t * s)
  {
  pthread_mutex_lock(&s->lock);
  if(!s->count)
    {
    s->nwaiting++;
    pthread_cond_wait(&s->cond, &s->lock);
    s->nwaiting--;
    }
  s->count = 0;
  pthread_mutex_unlock(&s->lock);
  }

static void bin_sem_post(bin_sem_t * s)
  {
  pthread_mutex_lock(&s->lock);

  s->count = 1;
  if(s->nwaiting)
    pthread_cond_broadcast(&s->cond);
  pthread_mutex_unlock(&s->lock);
  }

static void bin_sem_reset(bin_sem_t * s)
  {
  pthread_mutex_lock(&s->lock);
  s->count = 0;
  pthread_mutex_unlock(&s->lock);
  }

struct bg_thread_common_s
  {
  pthread_cond_t start_cond;
  pthread_mutex_t start_mutex;
  };

struct bg_thread_s
  {
  bg_thread_common_t * com;
  
  pthread_t thread;
  bin_sem_t sem;

  void * (*func)(void*);
  void * arg;
  
  int do_stop;
  int do_pause;
  pthread_mutex_t mutex;
  
  };


bg_thread_common_t * bg_thread_common_create()
  {
  bg_thread_common_t * com = calloc(1, sizeof(*com));
  pthread_cond_init(&com->start_cond, NULL);
  pthread_mutex_init(&com->start_mutex, NULL);
  return com;
  }

void bg_thread_common_destroy(bg_thread_common_t * com)
  {
  pthread_cond_destroy(&com->start_cond);
  pthread_mutex_destroy(&com->start_mutex);
  free(com);
  }

bg_thread_t *
bg_thread_create(bg_thread_common_t * com)
  {
  bg_thread_t * th = calloc(1, sizeof(*th));
  th->com = com;
  bin_sem_init(&th->sem);
  pthread_mutex_init(&th->mutex, NULL);
  return th;
  }

void bg_thread_destroy(bg_thread_t * th)
  {
  bin_sem_destroy(&th->sem);
  pthread_mutex_destroy(&th->mutex);
  free(th);
  }

void bg_thread_set_func(bg_thread_t * th,
                        void * (*func)(void*), void * arg)
  {
  th->func = func;
  th->arg = arg;
  th->do_pause = 0;
  th->do_stop = 0;
  }

void bg_threads_init(bg_thread_t ** th, int num)
  {
  int i;

  for(i = 0; i < num; i++)
    {
    if(th[i]->func)
      {
      // fprintf(stderr, "Starting thread...\n");
      pthread_create(&th[i]->thread, NULL, th[i]->func, th[i]->arg);
      // fprintf(stderr, "Starting thread done\n");
      }
    }
  /* Wait until all threads are started */
  for(i = 0; i < num; i++)
    {
    if(th[i]->func)
      {
      // fprintf(stderr, "Sem wait...");
      bin_sem_wait(&th[i]->sem);
      // fprintf(stderr, "done ret: %d, val: %d\n", ret, val);
      }
    }
  
  }

void bg_threads_start(bg_thread_t ** th, int num)
  {
  int i;

  //  fprintf(stderr, "bg_threads_start\n");

  /* Lock the global mutex. This will succeed after all
     threads wait for the start condition */

  if(!num)
    return;
  
  pthread_mutex_lock(&th[0]->com->start_mutex);
  pthread_cond_broadcast(&th[0]->com->start_cond);
  pthread_mutex_unlock(&th[0]->com->start_mutex);

  /* Wait until all threads woke up */
  for(i = 0; i < num; i++)
    {
    if(th[i]->func)
      {
      bin_sem_wait(&th[i]->sem);
      // fprintf(stderr, "done ret: %d, val: %d\n", ret, val);
      }
    }

  //  fprintf(stderr, "bg_threads_start done\n");
  }

void bg_threads_pause(bg_thread_t ** th, int num)
  {
  int i;
  //  fprintf(stderr, "bg_threads_pause\n");

  /* Set pause flag */
  for(i = 0; i < num; i++)
    {
    if(th[i]->func)
      {
      pthread_mutex_lock(&th[i]->mutex);
      th[i]->do_pause = 1;
      pthread_mutex_unlock(&th[i]->mutex);
      }
    }
  
  /* Wait until all threads are paused */
  for(i = 0; i < num; i++)
    {
    if(th[i]->func)
      bin_sem_wait(&th[i]->sem);
    }
  //  fprintf(stderr, "bg_threads_pause done\n");
  }

void bg_threads_join(bg_thread_t ** th, int num)
  {
  int i;
  /* Set stop flag */
  for(i = 0; i < num; i++)
    {
    if(th[i]->func)
      {
      pthread_mutex_lock(&th[i]->mutex);
      th[i]->do_stop = 1;
      pthread_mutex_unlock(&th[i]->mutex);
      }
    }

  /* Start the threads if they where paused.
     If not paused, this call does no harm */
  //  fprintf(stderr, "bg_threads_start %d\n", getpid());
  bg_threads_start(th, num);
  //  fprintf(stderr, "bg_threads_start done %d\n", getpid());
  
  for(i = 0; i < num; i++)
    {
    if(th[i]->func)
      {
      // fprintf(stderr, "Joining thread...\n");
      pthread_join(th[i]->thread, NULL);
      // fprintf(stderr, "Joining thread done, sem\n");

      bin_sem_reset(&th[i]->sem);
      //      bin_sem_init(&th[i]->sem, 0, 0);
      
      }
    }
  }
/* called from within the thread */

int bg_thread_wait_for_start(bg_thread_t * th)
  {
  int ret = 1;
  pthread_mutex_lock(&th->com->start_mutex);
  // fprintf(stderr, "Sem post...\n");
  bin_sem_post(&th->sem);
  // fprintf(stderr, "Sem post done\n");
  
  pthread_cond_wait(&th->com->start_cond, &th->com->start_mutex);
  pthread_mutex_unlock(&th->com->start_mutex);

  pthread_mutex_lock(&th->mutex);
  th->do_pause = 0;
  if(th->do_stop)
    ret = 0;
  
  pthread_mutex_unlock(&th->mutex);
  bin_sem_post(&th->sem);
  return ret;
  }

void bg_thread_exit(bg_thread_t * th)
  {
  /* bg_threads_start() (called by bg_threads_join())
     needs this */
  
  bin_sem_post(&th->sem);
  }

int bg_thread_check(bg_thread_t * th)
  {
  int do_pause;
  
  pthread_mutex_lock(&th->mutex);
  if(th->do_stop)
    {
    pthread_mutex_unlock(&th->mutex);
    bin_sem_post(&th->sem);
    return 0;
    }
  do_pause = th->do_pause;
  pthread_mutex_unlock(&th->mutex);
  
  if(do_pause)
    {
    pthread_mutex_lock(&th->mutex);
    th->do_pause = 0;
    pthread_mutex_unlock(&th->mutex);
    
    return bg_thread_wait_for_start(th);
    }
  return 1;
  }
