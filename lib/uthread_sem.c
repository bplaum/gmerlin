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




/* Semaphore implementation taken from some BSD, original copyright below */

/*
 * $Id: uthread_sem.c,v 1.2 2010-09-02 13:07:13 gmerlin Exp $
 *
 * Copyright (C) 2000 Jason Evans <jasone@freebsd.org>.
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice(s), this list of conditions and the following disclaimer as
 *    the first lines of this file unmodified other than the possible
 *    addition of one or more copyright notices.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice(s), this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER(S) ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER(S) BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD: src/lib/libc_r/uthread/uthread_sem.c,v 1.3.2.1 2000/07/18 02:05:57 jasone Exp $
 */

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <inttypes.h>
#include <gmerlin/bg_sem.h>
// #include "pthread_private.h"

#define _SEM_CHECK_VALIDITY(sem)                \
        if ((*(sem))->magic != SEM_MAGIC) {     \
                errno = EINVAL;                 \
                retval = -1;                    \
                goto RETURN;                    \
        }

int
sem_init(sem_t *sem, int pshared, unsigned int value)
{
	int	retval;

        //	if (!THREAD_SAFE())
        //		THREAD_INIT();

	/*
	 * Range check the arguments.
	 */
	if (pshared != 0) {
		/*
		 * The user wants a semaphore that can be shared among
		 * processes, which this implementation can't do.  Sounds like a
		 * permissions problem to me (yeah right).
		 */
		errno = EPERM;
		retval = -1;
		goto RETURN;
	}

	if (value > SEM_VALUE_MAX) {
		errno = EINVAL;
		retval = -1;
		goto RETURN;
	}

	*sem = (sem_t)malloc(sizeof(struct sem));
	if (*sem == NULL) {
		errno = ENOSPC;
		retval = -1;
		goto RETURN;
	}

	/*
	 * Initialize the semaphore.
	 */
	if (pthread_mutex_init(&(*sem)->lock, NULL) != 0) {
		free(*sem);
		errno = ENOSPC;
		retval = -1;
		goto RETURN;
	}

	if (pthread_cond_init(&(*sem)->gtzero, NULL) != 0) {
		pthread_mutex_destroy(&(*sem)->lock);
		free(*sem);
		errno = ENOSPC;
		retval = -1;
		goto RETURN;
	}
	
	(*sem)->count = (u_int32_t)value;
	(*sem)->nwaiters = 0;
	(*sem)->magic = SEM_MAGIC;

	retval = 0;
  RETURN:
	return retval;
}

int
sem_destroy(sem_t *sem)
{
	int	retval;
	
	_SEM_CHECK_VALIDITY(sem);

	/* Make sure there are no waiters. */
	pthread_mutex_lock(&(*sem)->lock);
	if ((*sem)->nwaiters > 0) {
		pthread_mutex_unlock(&(*sem)->lock);
		errno = EBUSY;
		retval = -1;
		goto RETURN;
	}
	pthread_mutex_unlock(&(*sem)->lock);
	
	pthread_mutex_destroy(&(*sem)->lock);
	pthread_cond_destroy(&(*sem)->gtzero);
	(*sem)->magic = 0;

	free(*sem);

	retval = 0;
  RETURN:
	return retval;
}

sem_t *
sem_open(const char *name, int oflag, ...)
{
	errno = ENOSYS;
	return SEM_FAILED;
}

int
sem_close(sem_t *sem)
{
	errno = ENOSYS;
	return -1;
}

int
sem_unlink(const char *name)
{
	errno = ENOSYS;
	return -1;
}

int
sem_wait(sem_t *sem)
{
	int	retval;

	pthread_testcancel();
	
	_SEM_CHECK_VALIDITY(sem);

	pthread_mutex_lock(&(*sem)->lock);

	while ((*sem)->count == 0) {
		(*sem)->nwaiters++;
		pthread_cond_wait(&(*sem)->gtzero, &(*sem)->lock);
		(*sem)->nwaiters--;
	}
	(*sem)->count--;

	pthread_mutex_unlock(&(*sem)->lock);

	retval = 0;
  RETURN:

	pthread_testcancel();
	return retval;
}

int
sem_trywait(sem_t *sem)
{
	int	retval;

	_SEM_CHECK_VALIDITY(sem);

	pthread_mutex_lock(&(*sem)->lock);

	if ((*sem)->count > 0) {
		(*sem)->count--;
		retval = 0;
	} else {
		errno = EAGAIN;
		retval = -1;
	}
	
	pthread_mutex_unlock(&(*sem)->lock);

  RETURN:
	return retval;
}

int
sem_post(sem_t *sem)
{
	int	retval;

	_SEM_CHECK_VALIDITY(sem);

	pthread_mutex_lock(&(*sem)->lock);

	(*sem)->count++;
	if ((*sem)->nwaiters > 0) {
		/*
		 * We must use pthread_cond_broadcast() rather than
		 * pthread_cond_signal() in order to assure that the highest
		 * priority thread is run by the scheduler, since
		 * pthread_cond_signal() signals waiting threads in FIFO order.
		 */
		pthread_cond_broadcast(&(*sem)->gtzero);
	}

	pthread_mutex_unlock(&(*sem)->lock);

	retval = 0;
  RETURN:
	return retval;
}

int
sem_getvalue(sem_t *sem, int *sval)
{
	int	retval;

	_SEM_CHECK_VALIDITY(sem);

	pthread_mutex_lock(&(*sem)->lock);
	*sval = (int)(*sem)->count;
	pthread_mutex_unlock(&(*sem)->lock);

	retval = 0;
  RETURN:
	return retval;
}
