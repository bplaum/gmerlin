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

#ifndef MDB_PRIVATE_H_INCLUDED
#define MDB_PRIVATE_H_INCLUDED


/* Backends: */

#include <gmerlin/objectcache.h>
#include <gmerlin/pluginregistry.h>
#include <gmerlin/volumemanager.h>
#include <gmerlin/backend.h>
#include <gmerlin/cfgctx.h>

#include <bgsqlite.h>

typedef struct bg_mdb_backend_s bg_mdb_backend_t;

#define MDB_BACKEND_TAG "DBBackend"

#define MDB_BACKEND_FILESYSTEM     "filesystem"
#define MDB_BACKEND_SQLITE         "sqlite"
#define MDB_BACKEND_RADIO_BROWSER  "radiobrowser"
#define MDB_BACKEND_XML            "xml"
#define MDB_BACKEND_REMOTE         "remote"
#define MDB_BACKEND_PODCASTS       "podcasts"
#define MDB_BACKEND_STREAMS        "streams"

/* Special dictionary to store MDB specific data in
   gavl tracks. Will be removed before passed to the outer world */
  
#define BG_MDB_DICT                "$mdb"

/* Backend flags */
#define BE_FLAG_DO_CACHE (1<<0)
#define BE_FLAG_REMOTE   (1<<1) // We want remote devices
#define BE_FLAG_VOLUMES  (1<<2) // We want volumes and drives
#define BE_FLAG_RESCAN   (1<<3) // We want rescan commands
#define BE_FLAG_CREATION_DONE   (1<<4) // We send CREATION_DONE events

/* BG_MSG_NS_MDB_PRIVATE */

/* arg0: dictionary */
#define BG_CMD_MDB_ADD_ROOT_ELEMENT 1

/* arg0: id */
#define BG_CMD_MDB_DEL_ROOT_ELEMENT 2

/* arg0: local path */
#define BG_CMD_MDB_ADD_MEDIA_DIR    3

/* arg0: local path */
#define BG_CMD_MDB_DEL_MEDIA_DIR    4

/* Called by backends */


void bg_mdb_export_media_directory(bg_msg_sink_t * sink, const char * path);
void bg_mdb_unexport_media_directory(bg_msg_sink_t * sink, const char * path);

const char * bg_mdb_container_get_backend(const gavl_dictionary_t * track);
void bg_mdb_container_set_backend(gavl_dictionary_t * track, const char * be);

void bg_mdb_track_lock(bg_mdb_backend_t * b, int lock, gavl_dictionary_t * obj);

/* upnp */

// void bg_mdb_create_upnp(bg_mdb_backend_t * b);

/* remote */
void bg_mdb_create_upnp(bg_mdb_backend_t * b);

/* Filesystem (scanned while opening) */

void bg_mdb_create_filesystem(bg_mdb_backend_t * b);

/* xml */
void bg_mdb_create_xml(bg_mdb_backend_t * b);

/* sqlite */
void bg_mdb_create_sqlite(bg_mdb_backend_t * b);

/* user (editable, saved per album as xml) */

// void bg_mdb_create_user(bg_mdb_backend_t * b);

/* Removable drives */

/* Tuner */

/* Icecast */
// void bg_mdb_create_icecast(bg_mdb_backend_t * b);

/* radio-browser.info */
void bg_mdb_create_radio_browser(bg_mdb_backend_t * b);

/* Standard folders (Bookmarks, Favorites, Incoming) */
void bg_mdb_create_standard(bg_mdb_backend_t * b);

/* Remote gmerlin servers */
void bg_mdb_create_remote(bg_mdb_backend_t * b);

void bg_mdb_create_podcasts(bg_mdb_backend_t * b);

void bg_mdb_create_streams(bg_mdb_backend_t * b);

/* id is e.g.: /upnp-2/upnp_id */

/* mdb_thumbnail.c */

void bg_mdb_init_thumbnails(bg_mdb_t * mdb);
void bg_mdb_cleanup_thumbnails(bg_mdb_t * mdb);
void bg_mdb_clear_thumbnail_uris(gavl_dictionary_t * track);

/* Deprecated API */
// void bg_mdb_create_thumbnails(bg_mdb_t * mdb, gavl_dictionary_t * track);

/* New version */
void bg_mdb_make_thumbnails(bg_mdb_t * mdb, const char * filename);
void bg_mdb_purge_thumbnails(bg_mdb_t * mdb);


/* Add / delete root containers during runtime */
void bg_mdb_add_root_container(bg_msg_sink_t * sink, const gavl_dictionary_t * dict);
void bg_mdb_delete_root_container(bg_msg_sink_t * sink, const char * id);

gavl_dictionary_t * bg_mdb_get_root_container(bg_mdb_t * db, const char * media_class);

void bg_mdb_init_root_container(gavl_dictionary_t * dict, const char * media_class);

// gavl_dictionary_t * bg_mdb_add_dir_array(gavl_array_t * arr, const char * dir, const char * parent_id);
// void bg_mdb_del_dir_array(gavl_array_t * arr, const char * dir);

// int bg_mdb_has_dir_array(gavl_array_t * arr, const char * dir);
// const gavl_dictionary_t * bg_mdb_dir_array_get(const gavl_array_t * arr, int idx);
// const gavl_dictionary_t * bg_mdb_dir_array_get_by_id(const gavl_array_t * arr, const char * id);




const char * bg_mdb_get_klass_id(const char * klass);
const char * bg_mdb_get_klass_from_id(const char * id);


void bg_mdb_add_http_uris(bg_mdb_t * mdb, gavl_dictionary_t * dict);
void bg_mdb_add_http_uris_arr(bg_mdb_t * mdb, gavl_array_t * arr);

void bg_mdb_delete_http_uris(gavl_dictionary_t * dict);


const gavl_dictionary_t * bg_mdb_cache_get_object_max_age(bg_mdb_t * mdb, const char * id, int64_t max_age);
const gavl_dictionary_t * bg_mdb_cache_get_object_min_mtime(bg_mdb_t * mdb, const char * id, int64_t min_mtime);

const gavl_array_t * bg_mdb_cache_get_children_max_age(bg_mdb_t * mdb, const char * id, int64_t max_age);
const gavl_array_t * bg_mdb_cache_get_children_min_mtime(bg_mdb_t * mdb, const char * id, int64_t min_mtime);


void bg_mdb_set_next_previous(gavl_array_t * arr);
void bg_mdb_set_idx_total(gavl_array_t * arr, int idx, int total);

/* Push / pop a browse request */

gavl_dictionary_t * bg_mdb_push_browse_request(gavl_array_t * arr, int msg_id, const char * id);
gavl_dictionary_t * bg_mdb_pop_browse_request(gavl_array_t * arr, int msg_id, const char * id);

struct bg_mdb_backend_s
  {
  void (*destroy)(bg_mdb_backend_t * b);
  void (*stop)(bg_mdb_backend_t * b);
  
  int (*ping_func)(bg_mdb_backend_t * b);

  void (*create_db)(bg_mdb_backend_t * b);
  
  bg_controllable_t ctrl;
  void * priv;
  // char * prefix;
  
  bg_mdb_t * db;
  pthread_t th;

  int flags;

  const bg_parameter_info_t * parameters;

  const char * name;
  const char * long_name;
  
  };

struct bg_mdb_s
  {
  sqlite3 * thumbnail_db;
  pthread_mutex_t thumbnail_mutex;
  
  gavl_timer_t * timer;

  gavl_time_t cfg_save_time;
    
  char * path;
  char * config_file;

  bg_controllable_t ctrl;
  
  bg_msg_sink_t * be_evt_sink;
  
  bg_mdb_backend_t * backends;

  bg_volume_manager_t * volman;
  int volumes_added;

  bg_object_cache_t * cache;

  gavl_dictionary_t root;
  
  pthread_t th;

  bg_media_dirs_t * dirs;

  bg_http_server_t * srv;
  
  bg_cfg_registry_t * cfg_reg;
  bg_cfg_section_t * section; // mdb

  bg_parameter_info_t * parameters;

  bg_cfg_ctx_t * cfg;
  bg_cfg_ctx_t * cfg_ext;
  gavl_dictionary_t cfg_section_ext;
  
  FILE * dirlock;
  int num_rescan;
  int num_create;
  
  gavl_msg_t * rescan_func;
  
  //  int page_size;

  char * thumbs_dir;
  
  };

/* Cleanup object for saving to file */
void bg_mdb_object_cleanup(gavl_dictionary_t * dict);

#if 0
typedef struct
  {
  int start;
  int end;
  
  char * orig_id;
  char * translated_id;
  
  } bg_mdb_page_t;

void bg_mdb_page_init_browse_object(bg_mdb_t * mdb, bg_mdb_page_t * page, const char ** id);
void bg_mdb_page_init_browse_children(bg_mdb_t * mdb, bg_mdb_page_t * page, const char ** id, int * start, int * num);

void bg_mdb_page_apply_object(bg_mdb_t * mdb, const bg_mdb_page_t * page, gavl_dictionary_t * dict);

void bg_mdb_page_apply_children(bg_mdb_t * mdb, const bg_mdb_page_t * page, gavl_dictionary_t * array);

int bg_mdb_container_create_pages(bg_mdb_t * mdb,
                                  gavl_dictionary_t * arr, int start,
                                  int num, int total_children, const char * child_class,
                                  const char * parent_id);

void bg_mdb_page_free(bg_mdb_page_t * page);
  
#endif

#endif // MDB_PRIVATE_H_INCLUDED
