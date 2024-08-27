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



#ifndef BGTHREAD_H_INCLUDED
#define BGTHREAD_H_INCLUDED


typedef struct bg_thread_common_s bg_thread_common_t;
typedef struct bg_thread_s        bg_thread_t;


bg_thread_common_t *
bg_thread_common_create();

void bg_thread_common_destroy(bg_thread_common_t * com);

bg_thread_t *
bg_thread_create(bg_thread_common_t * com);

void bg_thread_destroy(bg_thread_t * th);

void bg_thread_set_func(bg_thread_t * th,
                        void * (*func)(void*), void * arg);

void bg_threads_init(bg_thread_t ** th, int num);

void bg_threads_start(bg_thread_t ** th, int num);

/* Pause all threads, resume with bg_threads_start */
void bg_threads_pause(bg_thread_t ** th, int num);

/* Stop and join all threads, must call bg_threads_init() after */
void bg_threads_join(bg_thread_t ** th, int num);

/* Returns 1 if the thread should continue, 0 else.
   If the thread was paused, this function blocks until we restart */
   
int bg_thread_check(bg_thread_t * th);
int bg_thread_wait_for_start(bg_thread_t * th);
void bg_thread_exit(bg_thread_t * th);


#endif // BGTHREAD_H_INCLUDED
