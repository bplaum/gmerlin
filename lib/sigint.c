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



#include <signal.h>
#include <pthread.h>

#include <config.h>
#include <gmerlin/translation.h>
#include <gmerlin/log.h>
#define LOG_DOMAIN "sigint"

#include <gmerlin/utils.h>

static pthread_mutex_t sigint_mutex = PTHREAD_MUTEX_INITIALIZER;

static int got_sigint = 0;
static struct sigaction old_int_sigaction;
static struct sigaction old_term_sigaction;

static void sigint_handler(int sig)
  {
  pthread_mutex_lock(&sigint_mutex);
  got_sigint = 1;
  sigaction(SIGINT, &old_int_sigaction, 0);
  sigaction(SIGTERM, &old_term_sigaction, 0);
  
  switch(sig)
    {
    case SIGINT:
      gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Caught SIGINT, terminating");
      break;
    case SIGTERM:
      gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Caught SIGTERM, terminating");
      break;
    }
  pthread_mutex_unlock(&sigint_mutex);
  }

void bg_handle_sigint()
  {
  struct sigaction sa;
  sa.sa_flags = 0;
  sigemptyset(&sa.sa_mask);
  sa.sa_handler = sigint_handler;
  if (sigaction(SIGINT, &sa, &old_int_sigaction) == -1)
    fprintf(stderr, "sigaction failed\n");
  if (sigaction(SIGTERM, &sa, &old_term_sigaction) == -1)
    fprintf(stderr, "sigaction failed\n");
  }

/* We don't really raise the signal */
void bg_sigint_raise()
  {
  pthread_mutex_lock(&sigint_mutex);
  got_sigint = 1;
  pthread_mutex_unlock(&sigint_mutex);
  }

int bg_got_sigint()
  {
  int ret;
  
  pthread_mutex_lock(&sigint_mutex);
  ret = got_sigint;
  pthread_mutex_unlock(&sigint_mutex);
  
  return ret;
  }
  
