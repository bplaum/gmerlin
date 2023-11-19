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

#ifndef BG_OBJCACHE_H_INCLUDED
#define BG_OBJCACHE_H_INCLUDED

typedef struct bg_object_cache_s bg_object_cache_t;

/* Return 1 if a cache item is not expired yet */
typedef int (*bg_object_cache_check_func)(const gavl_value_t * val, const char * id, void * priv);

/* The value is only valid until the next call to any cache function */

gavl_value_t * bg_object_cache_get(bg_object_cache_t * cache, const char * id);

void bg_object_cache_delete(bg_object_cache_t * cache, const char * id);

gavl_value_t * bg_object_cache_put_nocopy(bg_object_cache_t * cache, const char * id, gavl_value_t * val);
gavl_value_t * bg_object_cache_put(bg_object_cache_t * cache, const char * id, const gavl_value_t * val);

bg_object_cache_t * bg_object_cache_create(int max_disk_cache_size,
                                           int max_memory_cache_size,
                                           const char * directory);

void bg_object_cache_destroy(bg_object_cache_t * cache);

// void bg_object_cache_cleanup(bg_object_cache_t * cache, bg_object_cache_check_func f, void * priv);

#if 0

/* TODO: Local store for long tracklists */

typedef struct bg_list_store_s bg_list_store_t;

bg_list_store_t * bg_array_store_create(const char * directory);

void bg_list_store_destroy(bg_list_store_t * st);
int bg_list_store_get_num(bg_list_store_t * st);
const gavl_dictionary_t * bg_list_store_get(bg_list_store_t * st, int idx);

/* val can be array or dictionary */
void bg_list_store_splice(bg_list_store_t * st, int idx, int del, gavl_value_t * val);
#endif


#endif // BG_OBJCACHE_H_INCLUDED
