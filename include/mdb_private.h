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
#define MDB_BACKEND_REMOVABLE      "removable"
#define MDB_BACKEND_RECORDER       "recorder"

/* Special dictionary to store MDB specific data in
   gavl tracks. Will be removed before passed to the outer world */
  
#define BG_MDB_DICT                "$mdb"

/* Backend flags */
#define BE_FLAG_DO_CACHE (1<<0)
#define BE_FLAG_RESOURCES (1<<2) // We want volumes and drives

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

void bg_mdb_create_fs(bg_mdb_backend_t * b);

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

void bg_mdb_create_removable(bg_mdb_backend_t * b);

void bg_mdb_create_recorder(bg_mdb_backend_t * b);


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




void bg_mdb_set_next_previous(gavl_array_t * arr);
void bg_mdb_set_idx_total(gavl_array_t * arr, int idx, int total);

void bg_mdb_tracks_sort(gavl_array_t * arr);

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
  
  gavl_dictionary_t root;
  
  pthread_t th;

  bg_media_dirs_t * dirs;

  bg_http_server_t * srv;
  
  bg_cfg_registry_t * cfg_reg;
  gavl_dictionary_t * section; // mdb

  bg_parameter_info_t * parameters;

  bg_cfg_ctx_t * cfg;
  bg_cfg_ctx_t * cfg_ext;
  gavl_dictionary_t cfg_section_ext;
  
  FILE * dirlock;
  int num_rescan;
  int num_create;
  
  gavl_msg_t * rescan_func;
  
  char * thumbs_dir;
  
  gavl_array_t renderers;
  
  };

/* Cleanup object for saving to file */
void bg_mdb_object_cleanup(gavl_dictionary_t * dict);

typedef struct
  {
  sqlite3 * db;
  sqlite3_stmt *select_object;
  sqlite3_stmt *select_children;
  sqlite3_stmt *count_children;
  sqlite3_stmt *insert;
  sqlite3_stmt *scan_dir;
  sqlite3_stmt *delete_entry;

  char * current_path;
  gavl_array_t current_dir;
  int current_mask;
  
  } bg_mdb_fs_cache_t;

#define BG_MDB_FS_MASK_AUDIO      (1<<0)
#define BG_MDB_FS_MASK_VIDEO      (1<<1)
#define BG_MDB_FS_MASK_IMAGE      (1<<2)
#define BG_MDB_FS_MASK_LOCATION   (1<<3)
#define BG_MDB_FS_MASK_OTHER      (1<<4)

#define BG_MDB_FS_MASK_DIRECTORY  (1<<8)
#define BG_MDB_FS_MASK_MULTITRACK (1<<9)

#define BG_MDB_FS_TYPE_DIRECTORY  1
#define BG_MDB_FS_TYPE_MULTITRACK 2
#define BG_MDB_FS_TYPE_FILE       3

#define BG_MDB_FS_ITEM_MASK          0x00ff
#define BG_MDB_FS_CONTAINER_MASK     0xff00



int bg_mdb_fs_cache_init(bg_mdb_fs_cache_t * c, const char * path);
void bg_mdb_fs_cache_cleanup(bg_mdb_fs_cache_t * c);

int bg_mdb_fs_browse_object(bg_mdb_fs_cache_t * c, const char * path,
                            gavl_dictionary_t * ret,
                            int type_mask);

int bg_mdb_fs_browse_children(bg_mdb_fs_cache_t * c, const char * path,
                              gavl_array_t * ret, int start, int num, int type_mask, int * total_entries);

int bg_mdb_fs_count_children(bg_mdb_fs_cache_t * c, const char * path, int type_mask,
                             int * num_containers, int * num_items);

//int bg_mdb_load_track_fs(bg_mdb_fs_cache_t * c, gavl_dictionary_t * ret,
//                         const char * filename, time_t mtime);

int bg_mdb_fs_cache_get(bg_mdb_fs_cache_t * c, const char * filename,
                        time_t mtime, gavl_dictionary_t * m);

int bg_mdb_fs_cache_put(bg_mdb_fs_cache_t * c, const char * filename,
                        time_t mtime, const gavl_dictionary_t * m);



#endif // MDB_PRIVATE_H_INCLUDED
