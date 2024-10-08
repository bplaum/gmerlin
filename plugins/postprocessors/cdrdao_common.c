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



#include <string.h>
#include <pthread.h>
#include <signal.h>


#include <config.h>
#include <gmerlin/plugin.h>
#include <gmerlin/utils.h>
#include <gmerlin/log.h>
#include <gmerlin/subprocess.h>
#include "cdrdao_common.h"

#define LOG_DOMAIN "cdrdao"

struct bg_cdrdao_s
  {
  int run;
  char * device;
  char * driver;
  int eject;
  int simulate;
  int speed;
  int nopause;
  bg_e_pp_callbacks_t * callbacks;
  pthread_mutex_t stop_mutex;
  int do_stop;
  };

bg_cdrdao_t * bg_cdrdao_create()
  {
  bg_cdrdao_t * ret;
  ret = calloc(1, sizeof(*ret));
  pthread_mutex_init(&ret->stop_mutex, NULL);
  return ret;
  }

void bg_cdrdao_destroy(bg_cdrdao_t * cdrdao)
  {
  if(cdrdao->device)
    free(cdrdao->device);
  free(cdrdao);
  }

void bg_cdrdao_set_parameter(void * data, const char * name,
                             const gavl_value_t * val)
  {
  bg_cdrdao_t * c;
  if(!name)
    return;
  c = data;
  if(!strcmp(name, "cdrdao_run"))
    c->run = val->v.i;
  else if(!strcmp(name, "cdrdao_device"))
    c->device = gavl_strrep(c->device, val->v.str);
  else if(!strcmp(name, "cdrdao_driver"))
    c->driver = gavl_strrep(c->driver, val->v.str);
  else if(!strcmp(name, "cdrdao_eject"))
    c->eject = val->v.i;
  else if(!strcmp(name, "cdrdao_simulate"))
    c->simulate = val->v.i;
  else if(!strcmp(name, "cdrdao_speed"))
    c->speed = val->v.i;
  else if(!strcmp(name, "cdrdao_nopause"))
    c->nopause = val->v.i;
  }

static int check_stop(bg_cdrdao_t * c)
  {
  int ret;
  pthread_mutex_lock(&c->stop_mutex);
  ret = c->do_stop;
  c->do_stop = 0;
  pthread_mutex_unlock(&c->stop_mutex);
  return ret;
  }

int bg_cdrdao_run(bg_cdrdao_t * c, const char * toc_file)
  {
  bg_subprocess_t * cdrdao;
  char * str;
  char * commandline = NULL;

  char * line = NULL;
  int line_alloc = 0;
  
  int mb_written, mb_total;
  
  if(!c->run)
    {
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Not running cdrdao (disabled by user)");
    return 0;
    }
  if(!bg_search_file_exec("cdrdao", &commandline))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "cdrdao executable not found");
    return 0;
    }
  commandline = gavl_strcat(commandline, " write");
  
  /* Device */
  if(c->device)
    {
    str = gavl_sprintf(" --device %s", c->device);
    commandline = gavl_strcat(commandline, str);
    free(str);
    }
  /* Driver */
  if(c->driver)
    {
    str = gavl_sprintf(" --driver %s", c->driver);
    commandline = gavl_strcat(commandline, str);
    free(str);
    }
  /* Eject */
  if(c->eject)
    commandline = gavl_strcat(commandline, " --eject");
  /* Skip pause */
  if(c->nopause)
    commandline = gavl_strcat(commandline, " -n");

  /* Simulate */
  if(c->simulate)
    commandline = gavl_strcat(commandline, " --simulate");

  /* Speed */
  if(c->speed > 0)
    {
    str = gavl_sprintf(" --speed %d", c->speed);
    commandline = gavl_strcat(commandline, str);
    free(str);
    }
  
  /* TOC-File and stderr redirection */
  str = gavl_sprintf(" \"%s\"", toc_file);
  commandline = gavl_strcat(commandline, str);
  free(str);
  
  if(check_stop(c))
    {
    free(commandline);
    return 0;
    }

  /* Launching command (cdrdao sends everything to stderr) */
  cdrdao = bg_subprocess_create(commandline, 0, 0, 1);
  free(commandline);
  /* Read lines */

  while(bg_subprocess_read_line(cdrdao->stderr_fd, &line, &line_alloc, -1))
    {
    if(check_stop(c))
      {
      bg_subprocess_kill(cdrdao, SIGQUIT);
      bg_subprocess_close(cdrdao);
      return 0;
      }

    if(!strncmp(line, "ERROR", 5))
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "%s", line);	   
      //      break;
      }
    else if(!strncmp(line, "WARNING", 7))
      {
      gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "%s", line);	   
      //      break;
      }
    else if(!strncmp(line, "Writing", 7))
      {
      if(c->callbacks && c->callbacks->action_callback)
        c->callbacks->action_callback(c->callbacks->data,
                                      line);
      gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "%s", line);

      if(c->callbacks && c->callbacks->progress_callback)
        {
        if(!strncmp(line, "Writing track 01", 16) ||
           strncmp(line, "Writing track", 13))
          c->callbacks->progress_callback(c->callbacks->data, 0.0);
        }
      }
    else if(sscanf(line, "Wrote %d of %d", &mb_written, &mb_total) == 2)
      {
      if(c->callbacks && c->callbacks->progress_callback)
        c->callbacks->progress_callback(c->callbacks->data,
                                        (float)mb_written/(float)mb_total);
      else
        gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "%s", line);
      }
    else
      gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "%s", line);
    }
  bg_subprocess_close(cdrdao);

  if(c->simulate)
    return 0;
  else
    return 1;
  }

void bg_cdrdao_set_callbacks(bg_cdrdao_t * c, bg_e_pp_callbacks_t * callbacks)
  {
  c->callbacks = callbacks;
  }

void bg_cdrdao_stop(bg_cdrdao_t * c)
  {
  pthread_mutex_lock(&c->stop_mutex);
  c->do_stop = 1;
  pthread_mutex_unlock(&c->stop_mutex);
  
  }
