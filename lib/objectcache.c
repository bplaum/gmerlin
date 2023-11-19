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


#include <string.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

#include <config.h>


#include <gavl/gavl.h>
#include <gavl/metatags.h>
#include <gmerlin/bggavl.h>

#include <gmerlin/objectcache.h>
#include <gmerlin/utils.h>

#include <gmerlin/translation.h>
#include <gmerlin/log.h>
#define LOG_DOMAIN "objcache"

#include <md5.h>

#define ROOT_NAME_ENTRY "cacheentry"
#define ROOT_NAME_INDEX "cacheindex"
#define INDEX_FILENAME  "INDEX"

typedef struct
  {
  uint32_t md5[4];
  gavl_value_t val;
  char * id;
  } mem_cache_entry_t;

typedef struct
  {
  uint32_t md5[4];
  } disk_cache_entry_t;

struct bg_object_cache_s
  {
  int max_disk_cache_size;
  int max_memory_cache_size;
  
  int disk_cache_size;
  int memory_cache_size;
  
  mem_cache_entry_t * memory_cache;
  disk_cache_entry_t * disk_cache;

  char * directory;

  };

static gavl_value_t * object_cache_put_nocopy(bg_object_cache_t * cache,
                                              const char * id, gavl_value_t * val);

static gavl_value_t * object_cache_put(bg_object_cache_t * cache,
                                       const char * id, const gavl_value_t * val);

static gavl_value_t * object_cache_prepend_nocopy(bg_object_cache_t * cache,
                                                  const char * id, gavl_value_t * val);


static void move_to_front_mem(mem_cache_entry_t * arr, int num, int idx)
  {
  mem_cache_entry_t sav;
  memcpy(&sav, arr + idx, sizeof(sav));

  if(idx < num - 1)
    memmove(arr + idx, arr + (idx + 1), (num - 1 - idx) * sizeof(*arr));

  if(num - 1 > 0)
    memmove(arr + 1, arr, (num - 1) * sizeof(*arr));
  
  memcpy(arr, &sav, sizeof(sav));
  }

static void move_to_front_disk(disk_cache_entry_t * arr, int num, int idx)
  {
  disk_cache_entry_t sav;
  memcpy(&sav, arr + idx, sizeof(sav));
  
  if(idx < num - 1)
    memmove(arr + idx, arr + (idx + 1), (num - 1 - idx) * sizeof(*arr));

  if(num - 1 > 0)
    memmove(arr + 1, arr, (num - 1) * sizeof(*arr));
  
  memcpy(arr, &sav, sizeof(sav));
  }

/* Utils */

/* md5 stuff */
static void id_2_md5(const char * id, void * ret)
  {
  bg_md5_buffer(id, strlen(id), ret);
  }

static char * create_filename(const bg_object_cache_t * cache, const uint32_t * md5)
  {
  char str[MD5STRING_LEN];
  bg_md5_2_string(md5, str);
  return bg_sprintf("%s/%s", cache->directory, str);
  }

static void load_disk_index(bg_object_cache_t * cache)
  {
  char * filename;
  const gavl_array_t * arr;
  gavl_value_t val;

  gavl_value_init(&val);
  
  filename = bg_sprintf("%s/%s", cache->directory, INDEX_FILENAME);
  if(!access(filename, R_OK))
    bg_value_load_xml(&val, filename, ROOT_NAME_INDEX);
  free(filename);

  cache->disk_cache_size = 0;
  
  if((arr = gavl_value_get_array(&val)))
    {
    while((cache->disk_cache_size < cache->max_disk_cache_size) &&
          (cache->disk_cache_size < arr->num_entries))
      {
      if(!bg_string_2_md5(gavl_value_get_string(&arr->entries[cache->disk_cache_size]),
                       cache->disk_cache[cache->disk_cache_size].md5))
        break;
      cache->disk_cache_size++;
      }
    }
  gavl_value_free(&val);
  }

static void save_disk_index(const bg_object_cache_t * cache)
  {
  int i;
  char * filename;
  gavl_array_t * arr;
  gavl_value_t val;
  gavl_value_t el;

  char md5_string[MD5STRING_LEN];
  
  gavl_value_init(&val);
  filename = bg_sprintf("%s/%s", cache->directory, INDEX_FILENAME);

  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Saving disk index %s", filename);
  
  arr = gavl_value_set_array(&val);

  for(i = 0; i < cache->disk_cache_size; i++)
    {
    gavl_value_init(&el);
    bg_md5_2_string(cache->disk_cache[i].md5, md5_string);
    gavl_value_set_string(&el, md5_string);
    gavl_array_splice_val_nocopy(arr, -1, 0, &el);
    }
  
  bg_value_save_xml(&val, filename, ROOT_NAME_INDEX);
  
  gavl_value_free(&val);
  free(filename);
  }

static void delete_memory_cache_idx(bg_object_cache_t * cache,
                                    int idx)
  {
  gavl_value_reset(&cache->memory_cache[idx].val);
  
  if(idx < cache->memory_cache_size-1)
    {
    memmove(cache->memory_cache + idx,
            cache->memory_cache + (idx + 1),
            sizeof(*cache->memory_cache) * (cache->memory_cache_size-1-idx));
    }
  cache->memory_cache_size--;
  }

static void delete_disk_cache_idx(bg_object_cache_t * cache,
                                  int idx)
  {
  char * str = create_filename(cache, cache->disk_cache[idx].md5);
  remove(str);
  free(str);
    
  if(idx < cache->disk_cache_size-1)
    {
    memmove(cache->disk_cache + idx,
            cache->disk_cache + (idx + 1),
            sizeof(*cache->disk_cache) * (cache->disk_cache_size-1-idx));
    }
  cache->disk_cache_size--;
  }


/* object cache */

static int mem_cache_index(const bg_object_cache_t * cache, const uint32_t * md5)
  {
  int i;
  for(i = 0; i < cache->memory_cache_size; i++)
    {
    if((cache->memory_cache[i].md5[0] == md5[0]) &&
       (cache->memory_cache[i].md5[1] == md5[1]) &&
       (cache->memory_cache[i].md5[2] == md5[2]) &&
       (cache->memory_cache[i].md5[3] == md5[3]))
      {
      return i;
      }
    }
  return -1;
  }

static int disk_cache_index(const bg_object_cache_t * cache, const uint32_t * md5)
  {
  int i;

  //  fprintf(stderr, "disk_cache_index\n");
  //  gavl_hexdump((const uint8_t *)md5, 16, 16);
  
  for(i = 0; i < cache->disk_cache_size; i++)
    {
    //    gavl_hexdump((const uint8_t *)cache->disk_cache[i].md5, 16, 16);
    
    if((cache->disk_cache[i].md5[0] == md5[0]) &&
       (cache->disk_cache[i].md5[1] == md5[1]) &&
       (cache->disk_cache[i].md5[2] == md5[2]) &&
       (cache->disk_cache[i].md5[3] == md5[3]))
      {
      return i;
      }
    }
  return -1;
  }

gavl_value_t * bg_object_cache_get(bg_object_cache_t * cache, const char * id)
  {
  int idx;
  int done = 0;
  gavl_value_t * ret = NULL;
  gavl_value_t * val = NULL;
  
  uint32_t md5[4];
  
  /* Calculate md5 of id */
  id_2_md5(id, md5);
  
  /* 1. Try memory cache */
  if((idx = mem_cache_index(cache, md5)) >= 0)
    {
    move_to_front_mem(cache->memory_cache, cache->memory_cache_size, idx);
    done = 1;
    ret = &cache->memory_cache[0].val;
    }
 
  /* 2. Try disk cache */
  if(!done)
    {
    if((idx = disk_cache_index(cache, md5)) >= 0)
      {
      gavl_dictionary_t dict;
      char * filename = create_filename(cache, md5);
      /* Load value from cache */
      gavl_dictionary_init(&dict);
      
      if((done = bg_dictionary_load_xml(&dict, filename, ROOT_NAME_ENTRY)))
        {
        val = gavl_dictionary_get_nc(&dict, "v");
        ret = object_cache_prepend_nocopy(cache, id, val);
        
        move_to_front_disk(cache->disk_cache, cache->disk_cache_size, idx);
        }
      free(filename);
      gavl_dictionary_free(&dict);
      }
    }
  
  return ret;
  }

static void delete_memory_cache(bg_object_cache_t * cache,
                                uint32_t * md5)
  {
  int idx;
  if((idx = mem_cache_index(cache, md5)) >= 0)
    delete_memory_cache_idx(cache, idx);
  }


static void delete_disk_cache(bg_object_cache_t * cache,
                              uint32_t * md5)
  {
  int idx;
  if((idx = disk_cache_index(cache, md5)) >= 0)
    delete_disk_cache_idx(cache, idx);
  }

void bg_object_cache_delete(bg_object_cache_t * cache,
                            const char * id)
  {
  uint32_t md5[4];
  id_2_md5(id, md5);
  delete_memory_cache(cache, md5);
  delete_disk_cache(cache, md5);
  }

static void put_disk_cache_nocopy(bg_object_cache_t * cache, const uint32_t * md5,
                                  gavl_value_t * val, const char * id)
  {
  int idx;
  char * filename;
  gavl_dictionary_t dict;
  
  /* If entry is there, we don't rewrite it. Just move it to the front in the index */
  if((idx = disk_cache_index(cache, md5)) >= 0)
    {
    disk_cache_entry_t tmp;
    memcpy(&tmp, &cache->disk_cache[idx], sizeof(tmp));
    if(idx < cache->disk_cache_size-1)
      {
      memmove(cache->disk_cache + idx,
              cache->disk_cache + (idx + 1),
              sizeof(*cache->disk_cache) * (cache->disk_cache_size-1-idx));
      }

    if(cache->disk_cache_size > 1)
      memmove(&cache->disk_cache[1], &cache->disk_cache[0], (cache->disk_cache_size-1) * sizeof(*cache->disk_cache));

    memcpy(&cache->disk_cache[0], &tmp, sizeof(tmp));
    gavl_value_free(val);
    return;
    }
  
  /* Make space in disk cache */
  if(cache->disk_cache_size == cache->max_disk_cache_size)
    delete_disk_cache_idx(cache, cache->disk_cache_size-1);

  if(cache->disk_cache_size > 0)
    memmove(&cache->disk_cache[1], &cache->disk_cache[0], cache->disk_cache_size * sizeof(*cache->disk_cache));
  
  memcpy(cache->disk_cache[0].md5, md5, 4*sizeof(*md5));
  cache->disk_cache_size++;
  
  /* Save value */

  gavl_dictionary_init(&dict);
  gavl_dictionary_set_nocopy(&dict, "v", val);
  
  filename = create_filename(cache, md5);

  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Saving disk cache entry %s", filename);
  bg_dictionary_save_xml(&dict, filename, ROOT_NAME_ENTRY);
  free(filename);
  gavl_dictionary_free(&dict);
  }

static gavl_value_t * object_cache_prepend_nocopy(bg_object_cache_t * cache,
                                                        const char * id, gavl_value_t * val)
  {
  /* Make space in memory cache */
  if(cache->memory_cache_size == cache->max_memory_cache_size)
    {
    mem_cache_entry_t * e = &cache->memory_cache[cache->max_memory_cache_size-1];
    put_disk_cache_nocopy(cache, e->md5, &e->val, e->id);
    cache->memory_cache_size--;
    }
  
  /* Move in front of the memory cache */
  if(cache->memory_cache_size)
    memmove(cache->memory_cache+1, cache->memory_cache, sizeof(*cache->memory_cache)*cache->memory_cache_size);
  
  gavl_value_move(&cache->memory_cache[0].val, val);


  bg_md5_buffer(id, strlen(id), cache->memory_cache[0].md5);
  
  cache->memory_cache_size++;

  return &cache->memory_cache[0].val;
  
  }

static gavl_value_t * object_cache_put_nocopy(bg_object_cache_t * cache,
                                                    const char * id, gavl_value_t * val)
  {
  bg_object_cache_delete(cache, id);
  return object_cache_prepend_nocopy(cache, id, val);
  }

static gavl_value_t * object_cache_put(bg_object_cache_t * cache,
                                       const char * id, const gavl_value_t * val)
  {
  gavl_value_t v;
  gavl_value_init(&v);
  gavl_value_copy(&v, val);
  return object_cache_put_nocopy(cache, id, &v);
  }

gavl_value_t * bg_object_cache_put(bg_object_cache_t * cache,
                                          const char * id, const gavl_value_t * val)
  {
  gavl_value_t * ret;
  ret = object_cache_put(cache, id, val);
  return ret;
  }

gavl_value_t * bg_object_cache_put_nocopy(bg_object_cache_t * cache,
                                                const char * id, gavl_value_t * val)
  {
  gavl_value_t * ret;
  ret = object_cache_put_nocopy(cache, id, val);
  return ret;
  }

bg_object_cache_t * bg_object_cache_create(int max_disk_cache_size,
                                           int max_memory_cache_size,
                                           const char * directory)
  {
  bg_object_cache_t * ret = calloc(1, sizeof(*ret));

  ret->max_disk_cache_size = max_disk_cache_size;
  ret->max_memory_cache_size = max_memory_cache_size;

  ret->disk_cache = calloc(ret->max_disk_cache_size, sizeof(*ret->disk_cache));
  ret->memory_cache = calloc(ret->max_memory_cache_size, sizeof(*ret->memory_cache));
  
  ret->directory = gavl_strdup(directory);

  bg_ensure_directory(ret->directory, 1);
  
  load_disk_index(ret);
  return ret;
  }

void bg_object_cache_destroy(bg_object_cache_t * cache)
  {
  /* Move memory cache to disk.
     We do this backwards such that the first entry in the memory cache becomes the
     first entry in the disk cache */

  int i;
  for(i = cache->memory_cache_size-1; i >= 0; i--)
    {
    mem_cache_entry_t * e = cache->memory_cache + i;
    put_disk_cache_nocopy(cache, e->md5, &e->val, e->id);
    }
  
  save_disk_index(cache);

  /* Free stuff */
  free(cache->memory_cache);
  free(cache->disk_cache);
  free(cache->directory);
  
  free(cache);
  }

