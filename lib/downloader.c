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


#include <pthread.h>
#include <stdlib.h>
// #include <poll.h>
#include <glob.h>

#include <string.h>


#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <gavl/gavl.h>
#include <gavl/utils.h>
#include <gavl/value.h>

#include <gmerlin/downloader.h>
#include <gmerlin/msgqueue.h>
#include <gmerlin/utils.h>
#include <gmerlin/log.h>
#include <gmerlin/http.h>

#define LOG_DOMAIN "downloader"

#define META_MD5 "md5"

// #define USE_STATS

typedef struct
  {
  gavf_io_t * io;

  gavl_value_t buf_val;
  gavl_buffer_t * buf;

  int total_bytes;
  int chunked;
  
  /* GAVL_META_URI      */
  /* GAVL_META_MIMETYPE */
  /* GAVL_META_ID       */

  gavl_value_t dict_val;
  gavl_dictionary_t * dict;
  
  int64_t id;

  /* Asynchronous header reading */
  gavl_dictionary_t res;
  int got_header;
  int num_redirections;
  } download_internal_t;

typedef struct
  {
  bg_downloader_callback_t cb;
  void * cb_data;
  int64_t id;
  int running;
  char * url;

  /* ID of the actual download */
  int64_t ref_id;
  
  }  download_external_t;

struct bg_downloader_s
  {
  /* Internal downloads */
  int downloads_running_e;
  
  int downloads_running_i;
  int max_downloads;

  int cache_size;
  
  download_internal_t * downloads_i;
  
  /* External downloads */
  int downloads_e_alloc;
  int num_downloads_e;
  download_external_t * downloads_e;
    
  //  struct pollfd *pollfds;
  
  bg_controllable_t ctrl;

  pthread_t thread; 

  char * cache_dir;

  int64_t cur_id;

  bg_msg_sink_t * sink;
  
  gavl_array_t mem_cache;

  /* Statistics */

#ifdef USE_STATS
  int num_calls;
  int num_mem_cache_hits;
  int num_disk_cache_hits;
  int num_downloads;
  int num_downloads_ref;
  int num_errors;
  int num_local;
#endif

  
  
  };

static void free_download_i(bg_downloader_t * dl, int i)
  {
  download_internal_t * d = &dl->downloads_i[i];
  
  if(d->io)
    gavf_io_destroy(d->io);

  gavl_value_free(&d->dict_val);
  gavl_value_free(&d->buf_val);
  memset(d, 0, sizeof(*d));

  if(i < dl->downloads_running_i - 1)
    {
    memmove(d,
            d+1,
            (dl->downloads_running_i - 1 - i)*sizeof(*d) );
    }
  
  d = &dl->downloads_i[dl->downloads_running_i - 1];
  memset(d, 0, sizeof(*d));
  
  dl->downloads_running_i--;
  }

static void free_download_e(bg_downloader_t * dl, int i)
  {
  download_external_t * d = &dl->downloads_e[i];

  if(d->url)
    free(d->url);
  
  memset(d, 0, sizeof(*d));
  
  if(i < dl->num_downloads_e - 1)
    {
    memmove(d,
            d+1,
            (dl->num_downloads_e - 1 - i)*sizeof(*d) );
    }
  d = &dl->downloads_e[dl->num_downloads_e - 1];
  memset(d, 0, sizeof(*d));
  
  dl->num_downloads_e--;

  }

static void download_done_e(bg_downloader_t * d,
                            int64_t id,
                            const gavl_dictionary_t * dict,
                            const gavl_buffer_t * data)
  {
  int i = 0;
  int do_cb = 0;
  
  while(i < d->num_downloads_e)
    {
    do_cb = 0;
    
    if((d->downloads_e[i].id == id))
      {
      do_cb = 1;
      }

    if((d->downloads_e[i].ref_id == id))
      {
#ifdef USE_STATS
      d->num_downloads_ref++;
#endif
      do_cb = 1;
      }
    
    if(do_cb)
      {
      if(d->downloads_e[i].cb)
        d->downloads_e[i].cb(d->downloads_e[i].cb_data, id, dict, data);
      free_download_e(d, i);
      }
    
    else
      i++;
    }
  
  }

static int handle_msg_ext(void * data, gavl_msg_t * msg)
  {
  bg_downloader_t * d = data;

  switch(msg->NS)
    {
    case BG_MSG_NS_DOWNLOADER:
      switch(msg->ID)
        {
        case BG_MSG_DOWNLOADER_DOWNLOADED:
          {
          int64_t id;
          const gavl_dictionary_t * dict = NULL;
          const gavl_value_t * dict_val = NULL;

          const gavl_buffer_t * buf = NULL;
          const gavl_value_t * buf_val = NULL;
          
          if((id = gavl_msg_get_arg_long(msg, 0)) > 0)
            {
            if((dict_val = gavl_msg_get_arg_c(msg, 1)))
              dict = gavl_value_get_dictionary(dict_val);
            if((buf_val = gavl_msg_get_arg_c(msg, 2)))
              buf = gavl_value_get_binary(buf_val);
            
            download_done_e(d, id, dict, buf);

            d->downloads_running_e--;
            }

          break;
          }
        }
    }
  
  return 1;
  }

static int load_cache_item(bg_downloader_t * d,
                           const char * md5,
                           gavl_dictionary_t * dict,
                           gavl_buffer_t * buf)
  {
  int ret = 0;
  const char * mimetype = NULL;
  
  if((ret = bg_load_cache_item(d->cache_dir, md5, &mimetype, buf)))
    {
    gavl_dictionary_set_string(dict, GAVL_META_MIMETYPE, 
                               mimetype);
    gavl_dictionary_set_string(dict, META_MD5, md5);
    }
  
  return ret;
  }

static int save_cache_item(bg_downloader_t * d,
                           download_internal_t * dl)
  {
  
  bg_save_cache_item(d->cache_dir,
                     gavl_dictionary_get_string(dl->dict, META_MD5),
                     gavl_dictionary_get_string(dl->dict, GAVL_META_MIMETYPE),
                     dl->buf);
  
  return 1;
  }

static void send_download_done(bg_downloader_t * d,
                               int64_t id,
                               const gavl_value_t * dict_val,
                               const gavl_value_t * buf_val)
  {
  gavl_msg_t * evt;

  evt = bg_msg_sink_get(d->ctrl.evt_sink);

  gavl_msg_set_id_ns(evt, BG_MSG_DOWNLOADER_DOWNLOADED, BG_MSG_NS_DOWNLOADER);

  gavl_msg_set_arg_long(evt, 0, id);
  
  gavl_msg_set_arg(evt, 1, dict_val);
  gavl_msg_set_arg(evt, 2, buf_val);
  bg_msg_sink_put(d->ctrl.evt_sink, evt);
  }

static void put_memory_cache(bg_downloader_t * d,
                             gavl_value_t * dict_val,
                             gavl_value_t * buf_val)
  {
  gavl_value_t item_val;
  gavl_dictionary_t * item_dict;
  
  if(d->mem_cache.num_entries >= d->cache_size)
    {
    gavl_array_splice_val(&d->mem_cache, d->cache_size - 1, -1, NULL);
    }

  gavl_value_init(&item_val);
  item_dict = gavl_value_set_dictionary(&item_val);

  gavl_dictionary_set_nocopy(item_dict, "dict", dict_val);
  gavl_dictionary_set_nocopy(item_dict, "buf", buf_val);
  }

static int get_memory_cache(bg_downloader_t * d,
                            const char * md5,
                            const gavl_value_t ** dict_val_ret,
                            const gavl_value_t ** buf_val_ret)
  {
  int i;

  const gavl_dictionary_t * item;
  const gavl_dictionary_t * dict;
  const gavl_value_t * dict_val;

  const char * test_md5;
  
  
  for(i = 0; i < d->mem_cache.num_entries; i++)
    {
    if((item = gavl_value_get_dictionary(&d->mem_cache.entries[i])) &&
       (dict_val = gavl_dictionary_get(item, "dict")) &&
       (dict = gavl_value_get_dictionary(dict_val)) &&
       (test_md5 = gavl_dictionary_get_string(dict, META_MD5)) &&
       !strcmp(md5, test_md5))
      {
      
      /* Move item to front of array */
      if(i)
        {
        gavl_value_t sav;
        gavl_value_init(&sav);
        gavl_value_move(&sav, &d->mem_cache.entries[i]);
        gavl_array_splice_val(&d->mem_cache, i, 1, NULL);
        gavl_array_splice_val_nocopy(&d->mem_cache, 0, 0, &sav);
        }
      
      *dict_val_ret = dict_val;
      *buf_val_ret = gavl_dictionary_get(item, "buf");
      
      return 1;
      }
    }
  return 0;
  
  }

#if 0
static void download_done_i(bg_downloader_t * d,
                            download_internal_t * dl)
  {
  send_download_done(d, dl->id, &dl->dict_val,
                     &dl->buf_val);
  put_memory_cache(d, &dl->dict_val,
                   &dl->buf_val);
  
  }
#endif

static int start_download_i(bg_downloader_t * d,
                            const char * uri, 
                            int64_t id,
                            download_internal_t * dl)
  {
  char md5[33];
  
  //  int j;
  //  gavl_dictionary_t res;

  const gavl_value_t * dict_val_cache = NULL;
  const gavl_value_t * buf_val_cache = NULL;
  
  gavl_log(GAVL_LOG_DEBUG, LOG_DOMAIN, "start_download_i: %"PRId64", %s", id, uri);
  
  dl->dict = gavl_value_set_dictionary(&dl->dict_val);
  dl->buf = gavl_value_set_binary(&dl->buf_val);
  dl->id = id;
  
  bg_get_filename_hash(uri, md5);
  
  /* Check if we have a cached item */

  gavl_dictionary_set_string(dl->dict, GAVL_META_URI, uri);
  gavl_dictionary_set_string(dl->dict, META_MD5, md5);

  /* Check for memory cache */
  
  if(get_memory_cache(d, md5, &dict_val_cache,
                      &buf_val_cache))
    {
#ifdef USE_STATS
    d->num_mem_cache_hits++;
#endif

    gavl_log(GAVL_LOG_DEBUG, LOG_DOMAIN, "Got download %s from memory cache", uri);
    
    send_download_done(d, id, dict_val_cache, buf_val_cache);
    return 1;
    }
  
  if(load_cache_item(d, md5, dl->dict, dl->buf))
    {
    gavl_log(GAVL_LOG_DEBUG, LOG_DOMAIN, "Got download %s from disk cache", uri);
    
#ifdef USE_STATS
    d->num_disk_cache_hits++;
#endif
    
    send_download_done(d, dl->id, &dl->dict_val,
                       &dl->buf_val);

    put_memory_cache(d, &dl->dict_val, &dl->buf_val);
    
    return 1;
    }

  /* Check for local files */

  if(*uri == '/')
    {
    gavl_log(GAVL_LOG_DEBUG, LOG_DOMAIN, "Loading local file: %s", uri);
    
    if(!bg_read_file(uri, dl->buf))
      return 0;

    gavl_dictionary_set_string(dl->dict, GAVL_META_MIMETYPE, 
                               bg_url_to_mimetype(uri));


    send_download_done(d, dl->id, &dl->dict_val,
                       &dl->buf_val);

    put_memory_cache(d, &dl->dict_val,
                     &dl->buf_val);

#ifdef USE_STATS
    d->num_local++;
#endif
    
    
    return 1;
    }
  /* Start download */
  // gavl_dictionary_set_string(

  
  gavl_log(GAVL_LOG_DEBUG, LOG_DOMAIN, "Starting download of %s", uri);
  
  //  gavl_dictionary_init(&res);

  if(!bg_http_send_request(uri, 0, NULL, &dl->io) || !dl->io)
    return 0;

  

  return 1;
  }


static int handle_msg_int(void * data, gavl_msg_t * msg)
  {
  bg_downloader_t * d = data;
  
  switch(msg->NS)
    {
    case BG_MSG_NS_DOWNLOADER:
      
      switch(msg->ID)
        {
        case BG_CMD_DOWNLOADER_ADD:
          {
          const char * uri = gavl_msg_get_arg_string_c(msg, 0);
          int64_t id = gavl_msg_get_arg_long(msg, 1);
          
          gavl_log(GAVL_LOG_DEBUG, LOG_DOMAIN, "Adding download %s running: %d max: %d\n",
                   uri, d->downloads_running_i, d->max_downloads);
          
          if(d->downloads_running_i == d->max_downloads)
            {
            // fprintf(stderr, "Bug %d %d\n", d->downloads_running_i, d->max_downloads);
            /* Should't happen */
            return 1;
            }

          start_download_i(d, uri, id,
                           &d->downloads_i[d->downloads_running_i]);
          
          d->downloads_running_i++;
          
          /* Error occurred or already finished */
          if(!d->downloads_i[d->downloads_running_i-1].io)
            {
            free_download_i(d, d->downloads_running_i-1);
            }
          }
          break;
        case BG_CMD_DOWNLOADER_DELETE:
          {
          int i;
          
          int64_t id = gavl_msg_get_arg_long(msg, 0);
          
          for(i = 0; i < d->downloads_running_i; i++)
            {
            if(d->downloads_i[i].id == id)
              {
              free_download_i(d, i);
              break;
              }
            }

          //          gavl_string_array_delete(&d->queue, uri);
          }
          break;
        }
    
    }

  return 1;
  }




static void * thread_func(void * data)
  {
  int i;
  int result;
  int actions;
  gavl_time_t delay_time = GAVL_TIME_SCALE / 20;
  bg_downloader_t * d = data;
  gavl_timer_t * timer;

  gavl_time_t current_time;

  gavl_time_t last_cleanup_time = GAVL_TIME_UNDEFINED;
  
  timer = gavl_timer_create();
  gavl_timer_start(timer);
  
  while(1)
    {
    actions = 0;

    current_time = gavl_timer_get(timer);

    if((last_cleanup_time == GAVL_TIME_UNDEFINED) ||
       (current_time - last_cleanup_time) > 3600 * (gavl_time_t)GAVL_TIME_SCALE)
      {
      if(bg_cache_directory_cleanup(d->cache_dir))
        actions++;
      
      last_cleanup_time = current_time;
      }
    
    /* Read messages */
    if(!bg_msg_sink_iteration(d->ctrl.cmd_sink))
      break;

    i = 0;
    
    while(i < d->downloads_running_i)
      {
      /* Read header in one run */

      download_internal_t * dl = &d->downloads_i[i];
      
      if(!dl->got_header)
        {
        char * redirect = NULL;
        const char * mimetype;
        const char * pos;

        if(!gavf_io_can_read(dl->io, 0))
          {
          i++;
          continue;
          }
        
        if(bg_http_read_response(dl->io,
                                 &redirect,
                                 &dl->res))
          {
          if(redirect)
            {
            if(dl->num_redirections > 5)
              {
              /* Too many redirections */
              gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Too many redirections for %s",
                       gavl_dictionary_get_string(dl->dict, GAVL_META_URI));
              
              send_download_done(d, dl->id, NULL, NULL);

              free_download_i(d, i);
#ifdef USE_STATS
              d->num_errors++;
#endif
              free(redirect);
              continue;
              }
            else
              {
              dl->num_redirections++;
              gavf_io_destroy(dl->io);
              dl->io = NULL;
              
              if(!bg_http_send_request(redirect, 0, NULL, &dl->io) || !dl->io)
                {
                gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Redirection to %s failed for %s",
                         redirect,
                         gavl_dictionary_get_string(dl->dict, GAVL_META_URI));
                
                send_download_done(d, dl->id, NULL, NULL);

                free_download_i(d, i);
#ifdef USE_STATS
                d->num_errors++;
#endif
                free(redirect);
                }
              }
            }
          else
            dl->got_header = 1;
          }
        else
          {
          gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Error downloading %s",
                   gavl_dictionary_get_string(dl->dict, GAVL_META_URI));
          
          send_download_done(d, dl->id, NULL, NULL);
          free_download_i(d, i);
#ifdef USE_STATS
          d->num_errors++;
#endif
          continue;
          }
        
        if(!dl->got_header)
          {
          i++;
          continue;
          }
        
        //        gavl_dprintf("Got HTTP answer\n");
        //        gavl_dictionary_dump(&dl->res, 2);
        
        mimetype = gavl_dictionary_get_string_i(&dl->res, "Content-Type");

        if((pos = strchr(mimetype, ';')))
          {
          gavl_dictionary_set_string_nocopy(dl->dict, GAVL_META_MIMETYPE, gavl_strndup(mimetype, pos));
          }
        else
          gavl_dictionary_set_string(dl->dict, GAVL_META_MIMETYPE, mimetype);
        
        if(!bg_http_read_body_start(dl->io, &dl->res,
                                    &dl->total_bytes,
                                    &dl->chunked))
          {
          send_download_done(d, dl->id, NULL, NULL);
          
          //          gavf_io_destroy(dl->io);
          
#ifdef USE_STATS
          d->num_errors++;
#endif
          gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Initialization of body reading failed for %s",
                   gavl_dictionary_get_string(dl->dict, GAVL_META_URI));

          send_download_done(d, d->downloads_i[i].id, NULL, NULL);
          free_download_i(d, i);
          actions++;
          continue;
          }
        else
          gavl_log(GAVL_LOG_DEBUG, LOG_DOMAIN, "Download started successfully %d bytes", dl->total_bytes);
        }
      
      result = bg_http_read_body_update(d->downloads_i[i].io,
                                        d->downloads_i[i].buf,
                                        &d->downloads_i[i].total_bytes,
                                        d->downloads_i[i].chunked);

      if(result < 0) // Error
        {
        gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Read error");
        
        send_download_done(d, d->downloads_i[i].id, NULL, NULL);
        free_download_i(d, i);
#ifdef USE_STATS
        d->num_errors++;
#endif
        actions++;
        continue;
        }
      else if(result > 0)
        actions++;
      
      if(d->downloads_i[i].io &&
         (d->downloads_i[i].total_bytes > 0) &&
         (d->downloads_i[i].buf->len == d->downloads_i[i].total_bytes))
        {
        save_cache_item(d, &d->downloads_i[i]);
        
        /* Download finished */
        

        send_download_done(d, d->downloads_i[i].id, &d->downloads_i[i].dict_val,
                           &d->downloads_i[i].buf_val);

        put_memory_cache(d, &d->downloads_i[i].dict_val,
                         &d->downloads_i[i].buf_val);
        
        free_download_i(d, i);
        
        //        fprintf(stderr, "Download finished %d\n", d->downloads_running_i);
        }
      else
        i++;
      }
    
    if(!actions)
      {
      gavl_time_delay(&delay_time);
      }
    
    }

  gavl_timer_destroy(timer);
  
  
  return NULL;
  }



void bg_downloader_update(bg_downloader_t * d)
  {
  int i;
  int done = 0;
  
  bg_msg_sink_iteration(d->sink);

  //  fprintf(stderr, "bg_downloader_update %d %d %d %d\n",
  //          d->downloads_running_e, d->max_downloads, 
  // d->num_downloads_e, d->downloads_running_e);
          
  while((d->downloads_running_e < d->max_downloads) &&
        (d->num_downloads_e > d->downloads_running_e))
    {
    done = 1;
    
    for(i = 0; i < d->num_downloads_e; i++)
      {
      if((d->downloads_e[i].id > 0) &&
         !d->downloads_e[i].running &&
         d->downloads_e[i].url)
        {
        gavl_msg_t * msg;
        
        msg = bg_msg_sink_get(d->ctrl.cmd_sink);

        gavl_msg_set_id_ns(msg, BG_CMD_DOWNLOADER_ADD, BG_MSG_NS_DOWNLOADER);

        gavl_msg_set_arg_string(msg, 0, d->downloads_e[i].url);
        gavl_msg_set_arg_long(msg, 1, d->downloads_e[i].id);
        
        bg_msg_sink_put(d->ctrl.cmd_sink, msg);
        d->downloads_running_e++;
        d->downloads_e[i].running = 1;

        if(d->downloads_running_e < d->max_downloads)
          done = 0;
        else
          break;
        }
      }

    if(done)
      break;
    }
  
  }

bg_downloader_t * bg_downloader_create(int max_downloads)
  {
  bg_downloader_t * ret;

  ret = calloc(1, sizeof(*ret));
  
  ret->max_downloads = max_downloads;
  ret->cache_size = max_downloads * 2;
  
  ret->downloads_i = calloc(ret->max_downloads, sizeof(*ret->downloads_i));
  
  ret->cache_dir = bg_search_cache_dir("http");
  
  bg_controllable_init(&ret->ctrl,
                       bg_msg_sink_create(handle_msg_int, ret, 0),
                       bg_msg_hub_create(1));
  
  ret->sink = bg_msg_sink_create(handle_msg_ext, ret, 0);

  bg_msg_hub_connect_sink(ret->ctrl.evt_hub, ret->sink);
  
  pthread_create(&ret->thread, NULL, thread_func, ret);
  
  return ret;
  }

void bg_downloader_destroy(bg_downloader_t * d)
  {
  /* Quit */
  gavl_msg_t * msg;
  msg = bg_msg_sink_get(d->ctrl.cmd_sink);
  gavl_msg_set_id_ns(msg, GAVL_CMD_QUIT, GAVL_MSG_NS_GENERIC);
  bg_msg_sink_put(d->ctrl.cmd_sink, msg);
  
  pthread_join(d->thread, NULL);
  

  if(d->downloads_i)
    {
    while(d->downloads_running_i)
      free_download_i(d, 0);
    
    free(d->downloads_i);
    }

  if(d->downloads_e)
    {
    while(d->num_downloads_e)
      free_download_e(d, 0);
    
    free(d->downloads_e);
    }
  
  bg_controllable_cleanup(&d->ctrl);
  bg_msg_sink_destroy(d->sink);

  
  free(d->cache_dir);

  gavl_array_free(&d->mem_cache);

#ifdef USE_STATS
  gavl_dprintf("Destroying downloader\n");
  gavl_dprintf("  num_calls:           %d\n", d->num_calls);
  gavl_dprintf("  num_mem_cache_hits:  %d\n", d->num_mem_cache_hits);
  gavl_dprintf("  num_disk_cache_hits: %d\n", d->num_disk_cache_hits);
  gavl_dprintf("  num_downloads:       %d\n", d->num_downloads);
  gavl_dprintf("  num_downloads_ref:   %d\n", d->num_downloads_ref);
  gavl_dprintf("  num_errrors:         %d\n", d->num_errors);
  gavl_dprintf("  num_local:           %d\n", d->num_local);
#endif
  
  free(d);
  
  }

int64_t bg_downloader_add(bg_downloader_t * d, const char * uri, bg_downloader_callback_t cb, void * cb_data)
  {
  int i;
  int64_t id = ++d->cur_id;
  
  if(d->num_downloads_e == d->downloads_e_alloc)
    {
    d->downloads_e_alloc += 64;
    d->downloads_e = realloc(d->downloads_e, sizeof(*d->downloads_e)*d->downloads_e_alloc);

    memset(d->downloads_e + d->num_downloads_e, 0,
           (d->downloads_e_alloc - d->num_downloads_e) * sizeof(*d->downloads_e));
    }

  
  
  d->downloads_e[d->num_downloads_e].cb      = cb;
  d->downloads_e[d->num_downloads_e].cb_data = cb_data;
  d->downloads_e[d->num_downloads_e].id = id;

  /* Check if we have this download queued already */
  
  for(i = 0; i < d->num_downloads_e; i++)
    {
    if(d->downloads_e[i].url &&
       !strcmp(d->downloads_e[i].url, uri))
      {
      d->downloads_e[d->num_downloads_e].ref_id = d->downloads_e[i].id;
      break;
      }
    }

  if(!d->downloads_e[d->num_downloads_e].ref_id)
    {
    d->downloads_e[d->num_downloads_e].url = gavl_strdup(uri);
    }
  
  d->num_downloads_e++;

#ifdef USE_STATS
  d->num_calls++;
#endif
  
  return id;
  }

void bg_downloader_delete(bg_downloader_t * d, int64_t id)
  {
  gavl_msg_t * msg;
  
  msg = bg_msg_sink_get(d->ctrl.evt_sink);
  gavl_msg_set_id_ns(msg, BG_CMD_DOWNLOADER_DELETE, BG_MSG_NS_DOWNLOADER);
  gavl_msg_set_arg_long(msg, 0, id);
  bg_msg_sink_put(d->ctrl.evt_sink, msg);
  
  }

