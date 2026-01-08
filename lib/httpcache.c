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

#include <pthread.h>
#include <stdlib.h>

#include <config.h>

#include <gmerlin/http.h>
#include <gmerlin/application.h>
#include <gmerlin/utils.h>

#include <gavl/log.h>

#define LOG_DOMAIN "httpcache"

#include <bgsqlite.h>

typedef struct
  {
  char * path;

  sqlite3_stmt *query;
  sqlite3_stmt *insert;
  
  sqlite3 * db;
  } cache_t;

static cache_t * http_cache = NULL;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

void bg_http_cache_init(void)
  {
  char * tmp_string;
  const char * app;

  char * query_sql = "SELECT "GAVL_HTTP_ETAG", "GAVL_META_MTIME", "GAVL_HTTP_CACHE_TIME", "GAVL_HTTP_CACHE_MAXAGE", "GAVL_META_MIMETYPE
    " from items where "GAVL_META_HASH" = :"GAVL_META_HASH;

  const char *insert_sql =
    "INSERT OR REPLACE INTO items "
    "("GAVL_META_HASH",   "GAVL_HTTP_ETAG",  "GAVL_META_MTIME",  "GAVL_HTTP_CACHE_TIME",  "GAVL_HTTP_CACHE_MAXAGE",  "GAVL_META_MIMETYPE") "
    "VALUES "
    "(:"GAVL_META_HASH", :"GAVL_HTTP_ETAG", :"GAVL_META_MTIME", :"GAVL_HTTP_CACHE_TIME", :"GAVL_HTTP_CACHE_MAXAGE", :"GAVL_META_MIMETYPE");";
  
  if(!(app = bg_app_get_name()))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "bg_http_cache_init called before bg_app_init");
    return;
    }

  
  pthread_mutex_lock(&mutex);
  
  if(http_cache)
    {
    pthread_mutex_unlock(&mutex);
    return;
    }
  http_cache = calloc(1, sizeof(*http_cache));

  http_cache->path = gavl_search_cache_dir(PACKAGE, app, "http");

  tmp_string = gavl_sprintf("%s/db.sqlite", http_cache->path);
  
  if(sqlite3_open(tmp_string, &http_cache->db))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN,
             "Cannot open database %s: %s", tmp_string,
             sqlite3_errmsg(http_cache->db));
    free(tmp_string);
    goto fail;
    }
  
  free(tmp_string);
  
  bg_sqlite_exec(http_cache->db, "CREATE TABLE IF NOT EXISTS items("
                 GAVL_META_HASH" TEXT PRIMARY KEY, "
                 GAVL_HTTP_ETAG" TEXT, "
                 GAVL_META_MTIME" INTEGER, "
                 GAVL_HTTP_CACHE_TIME" INTEGER, "
                 GAVL_HTTP_CACHE_MAXAGE" INTEGER, "
                 GAVL_META_MIMETYPE" TEXT);", NULL, NULL);

  if(sqlite3_prepare_v2(http_cache->db, query_sql, -1, &http_cache->query, NULL) != SQLITE_OK)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN,
             "Setting up query statement failed: %s",
             sqlite3_errmsg(http_cache->db));
    goto fail;
    }

  if(sqlite3_prepare_v2(http_cache->db, insert_sql, -1, &http_cache->insert, NULL) != SQLITE_OK)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN,
             "Setting up insert statement failed: %s",
             sqlite3_errmsg(http_cache->db));
    goto fail;
    }
  
  pthread_mutex_unlock(&mutex);

  return;

  fail:
  
  bg_http_cache_cleanup();
  pthread_mutex_unlock(&mutex);
  
  }

void bg_http_cache_cleanup()
  {
  pthread_mutex_lock(&mutex);
  
  if(!http_cache)
    {
    pthread_mutex_unlock(&mutex);
    return;
    }

  /* TODO: Clean up really old entries */
  
  sqlite3_finalize(http_cache->query);
  sqlite3_finalize(http_cache->insert);
  sqlite3_close(http_cache->db);
  free(http_cache);
  http_cache = NULL;
  pthread_mutex_unlock(&mutex);
  }


int bg_http_cache_get(const char * uri, gavl_dictionary_t * dict)
  {
  char md5[33];
  char * dir;
  int result;

  gavl_dictionary_reset(dict);
  
  pthread_mutex_lock(&mutex);
  
  if(!http_cache)
    {
    pthread_mutex_unlock(&mutex);
    return 0;
    }

  bg_get_filename_hash(uri, md5);
  
  gavl_dictionary_set_string(dict, GAVL_META_HASH, md5);

  dir = gavl_sprintf("%s/%c/%c", http_cache->path, md5[0], md5[1]);
  gavl_ensure_directory(dir, 1);

  gavl_dictionary_set_string_nocopy(dict, GAVL_HTTP_CACHE_FILE,
                                    gavl_sprintf("%s/%s", dir, md5));

  sqlite3_bind_text(http_cache->query, sqlite3_bind_parameter_index(http_cache->query, ":"GAVL_META_HASH), md5, -1, SQLITE_STATIC);


  result = sqlite3_step(http_cache->query);

  if(result == SQLITE_ROW)
    {
    gavl_dictionary_set_string(dict, GAVL_HTTP_ETAG, (const char*)sqlite3_column_text(http_cache->query, 0));

    gavl_dictionary_set_long(dict, GAVL_META_MTIME, sqlite3_column_int64(http_cache->query, 1));
    gavl_dictionary_set_long(dict, GAVL_HTTP_CACHE_TIME, sqlite3_column_int64(http_cache->query, 2));
    gavl_dictionary_set_long(dict, GAVL_HTTP_CACHE_MAXAGE, sqlite3_column_int64(http_cache->query, 3));
    gavl_dictionary_set_string(dict, GAVL_META_MIMETYPE, (const char*)sqlite3_column_text(http_cache->query, 4));
    }

  //  fprintf(stderr, "Got cache entry\n");
  
  
  sqlite3_reset(http_cache->query);
  sqlite3_clear_bindings(http_cache->query);
  
  free(dir);
  
  pthread_mutex_unlock(&mutex);
  return 1;
  }

int bg_http_cache_put(const gavl_dictionary_t * dict)
  {
  int updated = 0;
  
  const char * hash;
  const char * etag;
  const char * mimetype;
  int64_t mtime = 0;
  int64_t ctime = 0;
  int64_t maxage = 0;
  
  pthread_mutex_lock(&mutex);
  
  if(!http_cache ||
     !gavl_dictionary_get_int(dict, GAVL_HTTP_CACHE_UPDATED, &updated) ||
     !updated)
    {
    pthread_mutex_unlock(&mutex);
    return 0;
    }

  hash = gavl_dictionary_get_string(dict, GAVL_META_HASH);
  etag = gavl_dictionary_get_string(dict, GAVL_HTTP_ETAG);
  mimetype = gavl_dictionary_get_string(dict, GAVL_META_MIMETYPE);
  
  gavl_dictionary_get_long(dict, GAVL_META_MTIME, &mtime);  
  gavl_dictionary_get_long(dict, GAVL_HTTP_CACHE_TIME, &ctime);
  gavl_dictionary_get_long(dict, GAVL_HTTP_CACHE_MAXAGE, &maxage);

  sqlite3_bind_text(http_cache->insert,  sqlite3_bind_parameter_index(http_cache->insert, ":"GAVL_META_HASH),         hash, -1, SQLITE_STATIC);
  sqlite3_bind_text(http_cache->insert,  sqlite3_bind_parameter_index(http_cache->insert, ":"GAVL_HTTP_ETAG),         etag, -1, SQLITE_STATIC);
  sqlite3_bind_text(http_cache->insert,  sqlite3_bind_parameter_index(http_cache->insert, ":"GAVL_META_MIMETYPE), mimetype, -1, SQLITE_STATIC);
  sqlite3_bind_int64(http_cache->insert, sqlite3_bind_parameter_index(http_cache->insert, ":"GAVL_META_MTIME), (sqlite3_int64)mtime);
  sqlite3_bind_int64(http_cache->insert, sqlite3_bind_parameter_index(http_cache->insert, ":"GAVL_HTTP_CACHE_TIME), (sqlite3_int64)ctime);
  sqlite3_bind_int64(http_cache->insert, sqlite3_bind_parameter_index(http_cache->insert, ":"GAVL_HTTP_CACHE_MAXAGE), (sqlite3_int64)maxage);
  
  sqlite3_step(http_cache->insert);
  sqlite3_reset(http_cache->insert);
  sqlite3_clear_bindings(http_cache->insert);
  
  pthread_mutex_unlock(&mutex);
  return 1;
  }
