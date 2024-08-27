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



#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <config.h>

#include <libintl.h>

#include <stdarg.h>
#include <stdio.h>
#include <pthread.h>
#include <syslog.h>

#ifdef HAVE_ISATTY
#include <unistd.h>
#endif

#include <gmerlin/log.h>
#include <gmerlin/utils.h>

// #define DUMP_LOG

static bg_msg_sink_t * log_queue = NULL;
static bg_msg_hub_t * log_hub = NULL;
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

static bg_msg_sink_t * stderr_sink = NULL;
static bg_msg_sink_t * syslog_sink = NULL;


void bg_log_add_dest(bg_msg_sink_t * q)
  {
  pthread_mutex_lock(&log_mutex);
  
  if(!log_hub)
    {
    log_hub = bg_msg_hub_create(1);
    log_queue = bg_msg_hub_get_sink(log_hub);
    gavl_set_log_callback(bg_msg_sink_handle, log_queue);
    }
  
  bg_msg_hub_connect_sink(log_hub, q);
  
  pthread_mutex_unlock(&log_mutex);
  }

void bg_log_remove_dest(bg_msg_sink_t * q)
  {
  bg_msg_hub_disconnect_sink(log_hub, q);
  }

/* Syslog stuff */


static struct
  {
  int gmerlin_level;
  int syslog_level;
  }
loglevels[] =
  {
    { GAVL_LOG_ERROR,   LOG_ERR },
    { GAVL_LOG_WARNING, LOG_WARNING },
    { GAVL_LOG_INFO,    LOG_INFO },
    { GAVL_LOG_DEBUG,   LOG_DEBUG },
  };

static int log_syslog(void * data, gavl_msg_t * msg)
  {
  int i;
  int syslog_level = LOG_INFO;
  gavl_log_level_t level;
  const char * domain = NULL;
  const char * message = NULL;
  
  if(!gavl_log_msg_get(msg, &level, &domain, &message))
    return 1;
  
  for(i = 0; i < sizeof(loglevels) / sizeof(loglevels[0]); i++)
    {
    if(loglevels[i].gmerlin_level == level)
      {
      syslog_level = loglevels[i].syslog_level;
      break;
      }
    }
  syslog(syslog_level, "%s: %s", domain, message);
  return 1;
  }

void bg_log_stderr_init()
  {
  stderr_sink = bg_msg_sink_create(gavl_log_stderr, NULL, 1);
  bg_log_add_dest(stderr_sink);
  }


static char * syslog_name = NULL;

void bg_log_syslog_init(const char * name)
  {
  
  /* Initialize Logging */
  syslog_name = gavl_strrep(syslog_name, name);
  openlog(syslog_name, LOG_PID, LOG_USER);
  syslog_sink = bg_msg_sink_create(log_syslog, NULL, 1);
  bg_log_add_dest(syslog_sink);
  }

const char * bg_log_syslog_name()
  {
  return syslog_name;
  }

void bg_log_cleanup()
  {
  if(syslog_name)
    free(syslog_name);

  if(log_hub)
    bg_msg_hub_destroy(log_hub);

  if(stderr_sink)
    bg_msg_sink_destroy(stderr_sink);

  if(syslog_sink)
    {
    bg_msg_sink_destroy(syslog_sink);
    closelog();
    }
  }
