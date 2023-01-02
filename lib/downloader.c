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

#include <gmerlin/downloader.h>
#include <gavl/http.h>
#include <gmerlin/utils.h>
#include <gavl/log.h>

#define LOG_DOMAIN "downloader"


#define STATE_ACTIVE  (1<<0)
#define STATE_STARTED (1<<1)

typedef struct
  {
  char * uri;
  bg_downloader_callback_t cb;
  void * cb_data;
  int slot; // -1 if not started yet
  } queue_item_t;

typedef struct
  {
  char * uri;
  gavf_io_t * io;
  gavl_buffer_t buf;

  gavl_time_t start_time;

  int state;
  
  } download_t;

struct bg_downloader_s
  {
  download_t * downloads;
  int num_downloads;
  int downloads_alloc;
  
  queue_item_t * queue;
  int queue_len;
  int queue_alloc;
  
  gavl_timer_t * timer;

  char * cache_dir;
  };

static void remove_queue_item(bg_downloader_t * d, int j)
  {
  if(d->queue[j].uri)
    free(d->queue[j].uri);
  
  if(j < d->queue_len-1)
    memmove(d->queue + j, d->queue + j + 1, (d->queue_len-1 -j)*sizeof(*d->queue));

  d->queue_len--;
  }

static int get_download_slot(bg_downloader_t * d, const char * uri)
  {
  int i;

  for(i = 0; i < d->downloads_alloc; i++)
    {
    if((d->downloads[i].state & STATE_ACTIVE) &&
       !strcmp(d->downloads[i].uri, uri))
      return i;
    }
  return -1;
  }

static void finish_success(bg_downloader_t * d, const char * mimetype, int i)
  {
  int j;
  gavl_dictionary_t dict;
  gavl_dictionary_init(&dict);

  gavl_dictionary_set_string(&dict, GAVL_META_URI, d->downloads[i].uri);
  gavl_dictionary_set_string(&dict, GAVL_META_MIMETYPE, mimetype);

  j = 0;
  while(j < d->queue_len)
    {
    if(d->queue[j].slot == i)
      {
      d->queue[j].cb(d->queue[j].cb_data, &dict, &d->downloads[i].buf);
      remove_queue_item(d, j);
      }
    else
      j++;
    }
  
  gavl_dictionary_free(&dict);
  d->downloads[i].state = 0;
  gavl_buffer_reset(&d->downloads[i].buf);
  d->num_downloads--;
  }

static void finish_error(bg_downloader_t * d, int i)
  {
  int j;

  j = 0;
  while(j < d->queue_len)
    {
    if(d->queue[j].slot == i)
      {
      d->queue[j].cb(d->queue[j].cb_data, NULL, NULL);
      remove_queue_item(d, j);
      }
    else
      j++;
    }
  d->downloads[i].state = 0;
  gavl_buffer_reset(&d->downloads[i].buf);
  if(d->downloads[i].io)
    {
    gavf_io_destroy(d->downloads[i].io);
    d->downloads[i].io = NULL;
    }
  d->num_downloads--;
  }

static void init_download(bg_downloader_t * d, int i, const char * uri)
  {
  int j;
  
  /* Load download */
  d->downloads[i].state = STATE_ACTIVE;
  d->downloads[i].uri = gavl_strrep(d->downloads[i].uri, uri);

  for(j = 0; j < d->queue_len; j++)
    {
    if((d->queue[j].slot < 0) && !strcmp(d->queue[j].uri, uri))
      d->queue[j].slot = i;
    }
  d->num_downloads++;
  }

/* Disk cache */
static int load_cache_item(bg_downloader_t * d,
                           const char * uri,
                           const char ** mimetype,
                           gavl_buffer_t * buf)
  {
  char md5[33];
  bg_get_filename_hash(uri, md5);
  return bg_load_cache_item(d->cache_dir, md5, mimetype, buf);
  }

static int save_cache_item(bg_downloader_t * d,
                           const char * uri,
                           const char * mimetype,
                           const gavl_buffer_t * buf)
  {
  char md5[33];
  bg_get_filename_hash(uri, md5);
  bg_save_cache_item(d->cache_dir, md5, mimetype, buf);
  return 1;
  }


void bg_downloader_update(bg_downloader_t * d)
  {
  int i, j;
  gavl_time_t cur = gavl_timer_get(d->timer);

  //  fprintf(stderr, "bg_downloader_update\n");
  
  /* Check running downloads */
  if(d->num_downloads)
    {
    int result;
    for(i = 0; i < d->downloads_alloc; i++)
      {
      if(!(d->downloads[i].state & STATE_ACTIVE))
        continue;

      /* Check newly added "downloads" */
      if(!(d->downloads[i].state & STATE_STARTED))
        {
        const char * mimetype = NULL;
        
        /* Read local file */
        if(d->downloads[i].uri[0] == '/')
          {
          gavl_log(GAVL_LOG_DEBUG, LOG_DOMAIN, "Loading local file: %s", d->downloads[i].uri);

          if(!bg_read_file(d->downloads[i].uri, &d->downloads[i].buf))
            finish_error(d, i);         /* Error */
          else
            finish_success(d, bg_url_to_mimetype(d->downloads[i].uri), i);
          }
        /* Disk cache */
        else if(load_cache_item(d, d->downloads[i].uri, &mimetype, &d->downloads[i].buf))
          {
          gavl_log(GAVL_LOG_DEBUG, LOG_DOMAIN, "Got %s from disk cache", d->downloads[i].uri);
          finish_success(d, mimetype, i);
          }
        else
          {
          /* Download */
          if(!d->downloads[i].io)
            {
            d->downloads[i].io = gavl_http_client_create();
            gavl_http_client_set_response_body(d->downloads[i].io, &d->downloads[i].buf);
            }
          
          gavl_buffer_reset(&d->downloads[i].buf);
          gavl_http_client_run_async(d->downloads[i].io,
                                     "GET", d->downloads[i].uri);
          
          d->downloads[i].state |= STATE_STARTED;
          }
        continue;
        }
      
      /* Check if download is ready */

      result = gavl_http_client_run_async_done(d->downloads[i].io, 0);

      if(!result)
        {
        /* Check timeout */
        if(cur - d->downloads[i].start_time > 10 * GAVL_TIME_SCALE)
          {
          gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Download of %s timed out", d->downloads[i].uri);
          finish_error(d, i);
          }
        continue;
        }
      else if(result < 0)
        finish_error(d, i);         /* Error */
      else /* Download completed */
        {
        char * mimetype;
        char * pos;

        /* GAVL_META_MIMETYPE */
        const gavl_dictionary_t * resp = gavl_http_client_get_response(d->downloads[i].io);
        mimetype = gavl_strdup(gavl_dictionary_get_string_i(resp, "Content-Type"));
          
        if(mimetype && (pos = strchr(mimetype, ';')))
          *pos = '\0';

        save_cache_item(d, d->downloads[i].uri, mimetype, &d->downloads[i].buf);
        gavl_log(GAVL_LOG_DEBUG, LOG_DOMAIN, "Finished download %s", d->downloads[i].uri);
        finish_success(d, mimetype, i);
        if(mimetype)
          free(mimetype);
        }
      }
    } // Check running downloads

  i = 0;
  j = 0;
  
  /* Open new downloads */
  while(1)
    {
    /* Find download slot */
    
    while(i < d->downloads_alloc)
      {
      if(!(d->downloads[i].state & STATE_ACTIVE))
        break;
      i++;
      }
    if(i >= d->downloads_alloc)
      break;
    
    while(j < d->queue_len)
      {
      if(d->queue[j].slot < 0)
        break;
      j++;
      }
    if(j >= d->queue_len)
      break;

    init_download(d, i, d->queue[j].uri);
    d->downloads[i].start_time = cur;
    gavl_log(GAVL_LOG_DEBUG, LOG_DOMAIN, "Starting download %s", d->queue[j].uri);
    }
  
  }

bg_downloader_t * bg_downloader_create(int max_downloads)
  {
  bg_downloader_t * ret = calloc(1, sizeof(*ret));

  ret->downloads = calloc(max_downloads, sizeof(*ret->downloads));
  ret->downloads_alloc = max_downloads;

  ret->timer = gavl_timer_create();
  gavl_timer_start(ret->timer);

  ret->cache_dir = bg_search_cache_dir("http");
  
  return ret;
  }

void bg_downloader_destroy(bg_downloader_t * d)
  {
  int i;
  
  if(d->queue)
    {
    for(i = 0; i < d->queue_len; i++)
      {
      if(d->queue[i].uri)
        free(d->queue[i].uri);
      }
    free(d->queue);
    }
  
  if(d->downloads)
    {
    for(i = 0; i < d->downloads_alloc; i++)
      {
      if(d->downloads[i].uri)
        free(d->downloads[i].uri);
      
      if(d->downloads[i].io)
        gavf_io_destroy(d->downloads[i].io);
      
      gavl_buffer_free(&d->downloads[i].buf);
      }
    free(d->downloads);
    }
  
  free(d->cache_dir);
  gavl_timer_destroy(d->timer);
  free(d);
  }

void bg_downloader_add(bg_downloader_t * d, const char * uri,
                       bg_downloader_callback_t cb, void * cb_data)
  {
  queue_item_t * item;
  char * real_uri = NULL;
  
  //  gavl_log(GAVL_LOG_DEBUG, LOG_DOMAIN, "Adding download %s", uri);

  if(gavl_string_starts_with(uri, "appicon:"))
    {
    real_uri = bg_search_application_icon(uri + 8, 48);
    uri = real_uri;
    }
  
  if(d->queue_len == d->queue_alloc)
    {
    d->queue_alloc += 1024;
    d->queue = realloc(d->queue, d->queue_alloc * sizeof(*d->queue));
    memset(d->queue + d->queue_len, 0, (d->queue_alloc - d->queue_len) * sizeof(*d->queue));
    }
  item = d->queue + d->queue_len;
  
  item->uri = gavl_strdup(uri);
  item->cb = cb;
  item->cb_data = cb_data;

  item->slot = get_download_slot(d, uri);
  gavl_log(GAVL_LOG_DEBUG, LOG_DOMAIN, "Added download %s slot: %d", uri, item->slot);

  d->queue_len++;

  if(real_uri)
    free(real_uri);
  
  }
