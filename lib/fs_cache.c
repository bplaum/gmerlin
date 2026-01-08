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
#include <sys/stat.h>
#include <errno.h>


#include <config.h>

#include <gavl/gavl.h>
#include <gavl/utils.h>
#include <gavl/metatags.h>
#include <gavl/log.h>
#define LOG_DOMAIN "fs_cache"

#include <gmerlin/mdb.h>
#include <gmerlin/bggavl.h>
#include <gmerlin/utils.h>

#include <gavl/utils.h>

#include <mdb_private.h>

#define META_NAME   "name"
#define META_PARENT "parent"
#define META_TYPE   "type"
#define META_FLAGS  "flags"


int bg_mdb_fs_cache_init(bg_mdb_fs_cache_t * c, const char * path)
  {
  const char *select_object_sql =
    "SELECT "GAVL_META_METADATA" FROM files WHERE "META_PARENT" = :"META_PARENT" AND "META_NAME" = :"META_NAME" AND "GAVL_META_MTIME" = :"GAVL_META_MTIME";";
  
  const char *select_children_sql = "SELECT "META_NAME", "GAVL_META_MTIME", "GAVL_META_METADATA" FROM "
    "files WHERE "META_PARENT" = :"META_PARENT" AND "META_FLAGS" & :"META_FLAGS" order by "META_TYPE", "META_NAME" COLLATE strcoll;";

  const char *scan_dir_sql = "SELECT "META_NAME", "GAVL_META_MTIME" FROM "
    "files WHERE "META_PARENT" = :"META_PARENT" order by "META_NAME" COLLATE strcoll;";

  const char *delete_entry_sql = "DELETE from files WHERE "META_PARENT" = :"META_PARENT" and "META_NAME" = :"META_NAME;

  const char *delete_children_sql = "DELETE from files WHERE "META_PARENT" = :"META_PARENT;
  
  char *count_children_sql = "SELECT count("META_NAME") FROM files WHERE "META_PARENT" = :"META_PARENT" AND "META_FLAGS" & :"META_FLAGS";";
  
  const char *insert_sql =
    "INSERT OR REPLACE INTO files ("META_PARENT", "META_NAME", "META_TYPE", "META_FLAGS", "GAVL_META_MTIME", "GAVL_META_METADATA") "
    "VALUES (:"META_PARENT", :"META_NAME", :"META_TYPE", :"META_FLAGS", :"GAVL_META_MTIME", :"GAVL_META_METADATA");";

  
  if(sqlite3_open(path, &c->db))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN,
             "Cannot open database %s: %s", path,
             sqlite3_errmsg(c->db));
    sqlite3_close(c->db);
    return 0;
    }
  
  bg_sqlite_init_strcoll(c->db);
  
  bg_sqlite_exec(c->db, "CREATE TABLE IF NOT EXISTS files("
                 META_PARENT" TEXT, "
                 META_NAME" TEXT, "
                 META_TYPE" INTEGER, "
                 META_FLAGS" INTEGER, "
                 GAVL_META_MTIME" INTEGER, "
                 GAVL_META_METADATA" TEXT, PRIMARY KEY ("META_PARENT", "META_NAME") );", NULL, NULL);
  
  if(sqlite3_prepare_v2(c->db, select_object_sql, -1, &c->select_object, NULL) != SQLITE_OK)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN,
             "Setting up select object statement failed: %s",
             sqlite3_errmsg(c->db));
    
    sqlite3_close(c->db);
    return 0;
    }
  if(sqlite3_prepare_v2(c->db, select_children_sql, -1, &c->select_children, NULL) != SQLITE_OK)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN,
             "Setting up select children statement failed: %s (%s)",
             sqlite3_errmsg(c->db), select_children_sql);
    
    sqlite3_close(c->db);
    return 0;
    }
  if(sqlite3_prepare_v2(c->db, count_children_sql, -1, &c->count_children, NULL) != SQLITE_OK)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN,
             "Setting up count children statement failed: %s (%s)",
             sqlite3_errmsg(c->db), count_children_sql);
    
    sqlite3_close(c->db);
    return 0;
    }
  if(sqlite3_prepare_v2(c->db, insert_sql, -1, &c->insert, NULL) != SQLITE_OK)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN,
             "Setting up insert statement failed: %s",
             sqlite3_errmsg(c->db));
    
    sqlite3_close(c->db);
    return 0;
    }
  if(sqlite3_prepare_v2(c->db, delete_entry_sql, -1, &c->delete_entry, NULL) != SQLITE_OK)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN,
             "Setting up delete_entry statement failed: %s",
             sqlite3_errmsg(c->db));
    
    sqlite3_close(c->db);
    return 0;
    }
  if(sqlite3_prepare_v2(c->db, delete_children_sql, -1, &c->delete_children, NULL) != SQLITE_OK)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN,
             "Setting up delete_children statement failed: %s",
             sqlite3_errmsg(c->db));
    
    sqlite3_close(c->db);
    return 0;
    }
  if(sqlite3_prepare_v2(c->db, scan_dir_sql, -1, &c->scan_dir, NULL) != SQLITE_OK)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN,
             "Setting up scan_dir statement failed: %s",
             sqlite3_errmsg(c->db));
    
    sqlite3_close(c->db);
    return 0;
    }
  return 1;
  }


void bg_mdb_fs_cache_cleanup(bg_mdb_fs_cache_t * c)
  {
  sqlite3_finalize(c->select_object);
  sqlite3_finalize(c->select_children);
  sqlite3_finalize(c->count_children);
  sqlite3_finalize(c->insert);
  sqlite3_finalize(c->delete_entry);
  sqlite3_finalize(c->delete_children);
  sqlite3_close(c->db);
  }

int bg_mdb_fs_cache_get(bg_mdb_fs_cache_t * c,
                        const char * filename, time_t mtime,
                        gavl_dictionary_t * m)
  {
  int result;
  
  const char * name;
  char * parent = NULL;
  
  if((name = strrchr(filename, '/')))
    {
    parent = gavl_strndup(filename, name);
    name++;
    }
  else
    name = filename;
  
  sqlite3_bind_text(c->select_object,  sqlite3_bind_parameter_index(c->select_object, ":"META_PARENT),     parent, -1, SQLITE_STATIC);
  sqlite3_bind_text(c->select_object,  sqlite3_bind_parameter_index(c->select_object, ":"META_NAME),       name, -1, SQLITE_STATIC);
  sqlite3_bind_int64(c->select_object, sqlite3_bind_parameter_index(c->select_object, ":"GAVL_META_MTIME), (sqlite3_int64)mtime);

  result = sqlite3_step(c->select_object);

  if(m && (result == SQLITE_ROW))
    {
    const char *json = (const char*)sqlite3_column_text(c->select_object, 0);
    bg_dictionary_from_json_string(m, json);
    }

  sqlite3_reset(c->select_object);
  sqlite3_clear_bindings(c->select_object);
  
  if(parent)
    free(parent);
  
  if(result == SQLITE_ROW)
    return 1;
  else
    return 0;
  }

/* Select filename from */

int bg_mdb_fs_cache_put(bg_mdb_fs_cache_t * c, const char * filename, time_t mtime, const gavl_dictionary_t * m)
  {
  json_object *obj;
  
  obj = json_object_new_object();
  bg_dictionary_to_json(m, obj);
  
  sqlite3_bind_text(c->insert,  sqlite3_bind_parameter_index(c->insert, ":"META_PARENT), filename, -1, SQLITE_STATIC);
  sqlite3_bind_int64(c->insert, sqlite3_bind_parameter_index(c->insert, ":"GAVL_META_MTIME), (sqlite3_int64)mtime);
  
  sqlite3_bind_text(c->insert,  sqlite3_bind_parameter_index(c->insert, ":"GAVL_META_METADATA), json_object_to_json_string(obj), -1, SQLITE_STATIC);

  if(sqlite3_step(c->insert) != SQLITE_DONE)
    {
    /* Error */
    return 0;
    }

  sqlite3_reset(c->insert);
  sqlite3_clear_bindings(c->insert);
  return 1;
  }


static void delete_from_db(bg_mdb_fs_cache_t * c, const char * path)
  {
  const char * filename;
  char * parent;

  filename = strrchr(path, '/');
  parent = gavl_strndup(path, filename);

  filename++;

  sqlite3_bind_text(c->delete_entry, sqlite3_bind_parameter_index(c->delete_entry, ":"META_PARENT), parent, -1, SQLITE_STATIC);
  sqlite3_bind_text(c->delete_entry, sqlite3_bind_parameter_index(c->delete_entry, ":"META_NAME), filename, -1, SQLITE_STATIC);

  sqlite3_step(c->delete_entry);

  sqlite3_reset(c->delete_entry);
  sqlite3_clear_bindings(c->delete_entry);
  
  }

static void add_to_db_sub(bg_mdb_fs_cache_t * c, const char * parent, const char * name, 
                          int type, int flags, time_t mtime, const gavl_dictionary_t * m)
  {
  json_object *obj;

  sqlite3_bind_text(c->insert, sqlite3_bind_parameter_index(c->insert, ":"META_PARENT), parent, -1, SQLITE_STATIC);
  sqlite3_bind_text(c->insert, sqlite3_bind_parameter_index(c->insert, ":"META_NAME),  name, -1, SQLITE_STATIC);
  sqlite3_bind_int(c->insert, sqlite3_bind_parameter_index(c->insert, ":"META_TYPE), type);
  sqlite3_bind_int(c->insert, sqlite3_bind_parameter_index(c->insert, ":"META_FLAGS), flags);

  obj = json_object_new_object();
  bg_dictionary_to_json(m, obj);
  
  sqlite3_bind_text(c->insert, sqlite3_bind_parameter_index(c->insert, ":"GAVL_META_METADATA),
                    json_object_to_json_string(obj), -1, SQLITE_STATIC);
  sqlite3_bind_int64(c->insert, sqlite3_bind_parameter_index(c->insert, ":"GAVL_META_MTIME), mtime);

  sqlite3_step(c->insert);

  sqlite3_reset(c->insert);
  sqlite3_clear_bindings(c->insert);
  
  json_object_put(obj);
  
  }

static void cleanup_metadata(gavl_dictionary_t * m)
  {
  gavl_value_t * val;
  int idx;
  
  /* TODO: Remove stuff? */
  gavl_dictionary_set(m, GAVL_META_IDX, NULL);
  gavl_dictionary_set(m, GAVL_META_TOTAL, NULL);
  gavl_dictionary_set(m, GAVL_META_CAN_SEEK, NULL);
  gavl_dictionary_set(m, GAVL_META_CAN_PAUSE, NULL);

  idx = 0;

  while((val = gavl_dictionary_get_item_nc(m, GAVL_META_COVER_EMBEDDED, idx)))
    {
    gavl_dictionary_t * img;
    if((img = gavl_value_get_dictionary_nc(val)))
      gavl_dictionary_set(img, GAVL_META_IMAGE_BUFFER, NULL);
    idx++;
    }
  
  }

static void add_to_db(bg_mdb_fs_cache_t * c, const char * path, const char * klass, time_t mtime)
  {
  const char * name;
  char * parent;
  const gavl_dictionary_t * src;
  gavl_dictionary_t m;
  int type = 0;
  int flags = 0;
  gavl_dictionary_t * mi = NULL;

  //  json_object *obj;
  
  gavl_dictionary_init(&m);
  
  if((name = strrchr(path, '/')))
    {
    parent = gavl_strndup(path, name);
    name++;
    }
  else
    {
    parent = NULL;
    name = path;
    }
  
  if(!strcmp(klass, GAVL_META_CLASS_DIRECTORY))
    {
    gavl_dictionary_set_string(&m, GAVL_META_CLASS, klass);
    gavl_dictionary_set_string(&m, GAVL_META_LABEL, name);
    type = BG_MDB_FS_TYPE_DIRECTORY;
    flags = BG_MDB_FS_MASK_DIRECTORY;
    }
  else
    {
    
    if(!(mi = bg_plugin_registry_load_media_info(bg_plugin_reg, path, 0)) ||
       !gavl_get_num_tracks(mi))
      {
      /* Unsupported file */
      const char * label;
    
      if((label = strrchr(path, '/')))
        label++;
      else
        label = path;

      gavl_dictionary_set_string(&m, GAVL_META_LABEL, label);
      gavl_dictionary_set_string(&m, GAVL_META_CLASS, GAVL_META_CLASS_FILE);

      type = BG_MDB_FS_TYPE_FILE;
      flags = BG_MDB_FS_MASK_OTHER;
      
      }
    else
      {
      const char * klass;
        
      if(gavl_get_num_tracks(mi) == 1)
        src = gavl_get_track(mi, 0);
      else
        src = mi;
        
      src = gavl_track_get_metadata(src);

      klass = gavl_dictionary_get_string(src, GAVL_META_CLASS);

      if(gavl_string_starts_with(klass, GAVL_META_CLASS_AUDIO_FILE))
        {
        type = BG_MDB_FS_TYPE_FILE;
        flags = BG_MDB_FS_MASK_AUDIO;
        }
      else if(gavl_string_starts_with(klass, GAVL_META_CLASS_VIDEO_FILE))
        {
        type = BG_MDB_FS_TYPE_FILE;
        flags = BG_MDB_FS_MASK_VIDEO;
        }
      else if(gavl_string_starts_with(klass, GAVL_META_CLASS_IMAGE))
        {
        type = BG_MDB_FS_TYPE_FILE;
        flags = BG_MDB_FS_MASK_IMAGE;
        }
      else if(!strcmp(klass, GAVL_META_CLASS_MULTITRACK_FILE))
        {
        type = BG_MDB_FS_TYPE_MULTITRACK;
        flags = BG_MDB_FS_MASK_MULTITRACK;
        }
      else
        {
        type = BG_MDB_FS_TYPE_FILE;
        flags = BG_MDB_FS_MASK_OTHER;
        }

      gavl_dictionary_copy(&m, src);
      cleanup_metadata(&m);
      }
      
    }

  add_to_db_sub(c, parent, name, type, flags, mtime, &m);
  gavl_dictionary_reset(&m);

  if(type == BG_MDB_FS_TYPE_MULTITRACK)
    {
    char * tmp_string;
    int i;
    gavl_array_t tracks;
    const char * label_1 = NULL;
    const char * label_2 = NULL;
    
    int num = gavl_get_num_tracks(mi);

    sqlite3_bind_text(c->delete_children, sqlite3_bind_parameter_index(c->delete_children, ":"META_PARENT), path, -1, SQLITE_STATIC);
    
    sqlite3_step(c->delete_children);
    
    sqlite3_reset(c->delete_children);
    sqlite3_clear_bindings(c->delete_children);
    
    gavl_array_init(&tracks);
    
    bg_tracks_resolve_locations(gavl_dictionary_get(mi, GAVL_META_CHILDREN),
                                &tracks, BG_INPUT_FLAG_GET_FORMAT);
    
    for(i = 0; i < tracks.num_entries; i++)
      {
      label_1 = NULL;

      if(num == tracks.num_entries)
        {
        if((src = gavl_get_track(mi, i)) && (src = gavl_track_get_metadata(src)))
          label_1 = gavl_dictionary_get_string(src, GAVL_META_LABEL);
        }
      
      if((src = gavl_value_get_dictionary(&tracks.entries[i])) &&
         (src = gavl_track_get_metadata(src)))
        {
        label_2 = gavl_dictionary_get_string(src, GAVL_META_LABEL);
        
        gavl_dictionary_copy(&m, src);

        /* Label might have gotten screwed up */
        if(label_1 && label_2 && strstr(label_2, "://") && !strstr(label_1, "://"))
          gavl_dictionary_set_string(&m, GAVL_META_LABEL, label_1);
        
        cleanup_metadata(&m);
        
        tmp_string = gavl_sprintf("%04d", i);
        
        add_to_db_sub(c, path, tmp_string, type, flags, mtime, &m);

        free(tmp_string);
        gavl_dictionary_reset(&m);
        
        } 
      }
    gavl_array_free(&tracks);
    }

  if(mi)
    gavl_dictionary_destroy(mi);
  
  }

static int scan_directory(bg_mdb_fs_cache_t * c, const char * path)
  {
  int result;
  const gavl_dictionary_t * db_dict = NULL;
  const gavl_dictionary_t * fs_dict = NULL;
  
  gavl_array_t arr_db;
  gavl_array_t arr_fs;
  int db_idx =  0;
  int fs_idx =  0;

  const char * db_name = NULL;
  const char * fs_name = NULL;

  struct stat st;

  if(stat(path, &st))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "stat failed for %s: %s", path, strerror(errno));
    return 0;
    }

  if(!S_ISDIR(st.st_mode))
    {
    /* Multitrack file is already loaded if parent directory is loaded */
    return 1;
    }
  
  /* (Re)load directory */

  gavl_array_init(&arr_db);
  gavl_array_init(&arr_fs);
  
  bg_scan_directory(path, &arr_fs);
  bg_sort_directory(&arr_fs);
  
  sqlite3_bind_text(c->scan_dir, sqlite3_bind_parameter_index(c->scan_dir, ":"META_PARENT), path, -1, SQLITE_STATIC);

  while((result = sqlite3_step(c->scan_dir)) == SQLITE_ROW)
    {
    gavl_dictionary_t * dict;
    const char *name = (const char*)sqlite3_column_text(c->scan_dir, 0);
    int64_t mtime = sqlite3_column_int64(c->scan_dir, 1);
    
    dict = gavl_array_append_dictionary(&arr_db);
    gavl_dictionary_set_string_nocopy(dict, GAVL_META_URI,
                                      gavl_sprintf("%s/%s", path, name));
    gavl_dictionary_set_long(dict, GAVL_META_MTIME, mtime);
    }

  sqlite3_reset(c->scan_dir);
  sqlite3_clear_bindings(c->scan_dir);
  
  /* Synchronize scanned directory with db. We do this in one run */
  
  while(1)
    {
    if(db_idx == arr_db.num_entries)
      {
      /* Load remainig fs entries */
      while(fs_idx < arr_fs.num_entries)
        {
        int64_t fs_mtime = 0;  
        
        fs_dict = gavl_value_get_dictionary(&arr_fs.entries[fs_idx]);

        gavl_dictionary_get_long(fs_dict, GAVL_META_MTIME, &fs_mtime);
          
        fs_name = gavl_dictionary_get_string(fs_dict, GAVL_META_URI);
        add_to_db(c, fs_name, gavl_dictionary_get_string(fs_dict, GAVL_META_CLASS), fs_mtime);
        fs_idx++;
        }
      break;
      }
    
    if(fs_idx == arr_fs.num_entries)
      {
      /* Delete remainig db entries */
      while(db_idx < arr_db.num_entries)
        {
        db_dict = gavl_value_get_dictionary(&arr_db.entries[db_idx]);
        db_name = gavl_dictionary_get_string(db_dict, GAVL_META_URI);
        delete_from_db(c, db_name);
        db_idx++;
        }
      break;
      }

    if(!db_dict)
      {
      db_dict = gavl_value_get_dictionary(&arr_db.entries[db_idx]);
      db_name = gavl_dictionary_get_string(db_dict, GAVL_META_URI);
      }
    if(!fs_dict)
      {
      fs_dict = gavl_value_get_dictionary(&arr_fs.entries[fs_idx]);
      fs_name = gavl_dictionary_get_string(fs_dict, GAVL_META_URI);
      }

    result = strcoll(db_name, fs_name);
    
    if(!result)
      {
      /* Same name, check mtime */
      int64_t fs_mtime = 0;  
      int64_t db_mtime = 0;  

      if(!gavl_dictionary_get_long(fs_dict, GAVL_META_MTIME, &fs_mtime) ||
         !gavl_dictionary_get_long(db_dict, GAVL_META_MTIME, &db_mtime) ||
         (fs_mtime != db_mtime))
        add_to_db(c, fs_name, gavl_dictionary_get_string(fs_dict, GAVL_META_CLASS), fs_mtime);
      
      fs_idx++;
      db_idx++;
      fs_dict = NULL;
      db_dict = NULL;
      continue;
      }
    else if(result < 0)
      {
      /* DB name < FS name -> Delete from DB */
      delete_from_db(c, db_name);
      db_dict = NULL;
      db_idx++;
      }
    else
      {
      int64_t fs_mtime = 0;  
      gavl_dictionary_get_long(fs_dict, GAVL_META_MTIME, &fs_mtime);
      
      /* DB name > FS name -> Add to DB */
      add_to_db(c, fs_name, gavl_dictionary_get_string(fs_dict, GAVL_META_CLASS), fs_mtime);
      fs_dict = NULL;
      fs_idx++;
      }
    }
  
  return 1;
  }

/* Make sure we have the directory loaded */
static int ensure_directory(bg_mdb_fs_cache_t * c, const char * path,
                            int type_mask)
  {
  int result;
  
  /*
   *  Check if it's in memory cache
   */
  
  if(c->current_path && !strcmp(c->current_path, path) && (type_mask == c->current_mask))
    return 1;

  c->current_path = gavl_strrep(c->current_path, path);
  c->current_mask = type_mask;
  gavl_array_reset(&c->current_dir);
  
  scan_directory(c, path);
  
  sqlite3_bind_text(c->select_children,  sqlite3_bind_parameter_index(c->select_children, ":"META_PARENT), path, -1, SQLITE_STATIC);
  sqlite3_bind_int64(c->select_children, sqlite3_bind_parameter_index(c->select_children, ":"META_FLAGS), c->current_mask);
  
  while((result = sqlite3_step(c->select_children)) == SQLITE_ROW)
    {
    gavl_dictionary_t * dict;
    gavl_dictionary_t * m;
    
    const char *json = (const char*)sqlite3_column_text(c->select_children, 2);
    
    dict = gavl_array_append_dictionary(&c->current_dir);
    m = gavl_dictionary_get_dictionary_create(dict, GAVL_META_METADATA);
    gavl_dictionary_set_string(m, GAVL_META_ID, (const char*)sqlite3_column_text(c->select_children, 0));
    bg_dictionary_from_json_string(m, json);
    }
  
  sqlite3_reset(c->select_children);
  sqlite3_clear_bindings(c->select_children);
  return 1;
  }

int bg_mdb_fs_count_children(bg_mdb_fs_cache_t * c, const char * path, int type_mask,
                             int * num_containers, int * num_items)
  {
  int result;

  scan_directory(c, path);

  sqlite3_bind_text(c->count_children, sqlite3_bind_parameter_index(c->count_children, ":"META_PARENT), path, -1, SQLITE_STATIC);
  sqlite3_bind_int(c->count_children, sqlite3_bind_parameter_index(c->count_children, ":"META_FLAGS), type_mask & BG_MDB_FS_ITEM_MASK);
  result = sqlite3_step(c->count_children);
    
  if(result == SQLITE_ROW)
    *num_items = sqlite3_column_int(c->count_children, 0);
    
  sqlite3_reset(c->count_children);

  sqlite3_bind_int(c->count_children, sqlite3_bind_parameter_index(c->count_children, ":"META_FLAGS), type_mask & BG_MDB_FS_CONTAINER_MASK);
  result = sqlite3_step(c->count_children);
    
  if(result == SQLITE_ROW)
    *num_containers = sqlite3_column_int(c->count_children, 0);
    
  sqlite3_reset(c->count_children);
  sqlite3_clear_bindings(c->count_children);

  return 1;
  }

static void finalize(bg_mdb_fs_cache_t * c, const char * parent, int idx, int flags)
  {
  gavl_dictionary_t * track;
  gavl_dictionary_t * m;
  const gavl_dictionary_t * sibling;
  const char * klass;
  int num_items = 0;
  int num_containers = 0;
  
  if(!(track = gavl_value_get_dictionary_nc(&c->current_dir.entries[idx])) ||
     !(m = gavl_track_get_metadata_nc(track)))
    return;

  gavl_dictionary_set_int(m, GAVL_META_IDX, idx);
  gavl_dictionary_set_int(m, GAVL_META_TOTAL, c->current_dir.num_entries);
  
  if((klass = gavl_dictionary_get_string(m, GAVL_META_CLASS)) &&
     !strcmp(klass, GAVL_META_CLASS_DIRECTORY) &&
     !gavl_dictionary_get_int(m, GAVL_META_NUM_CHILDREN, &num_items))
    {
    char * tmp_string;
    
    tmp_string = gavl_sprintf("%s/%s", parent, gavl_dictionary_get_string(m, GAVL_META_ID));

    /* */

    if(bg_mdb_fs_count_children(c, tmp_string, flags, &num_containers, &num_items))
      gavl_track_set_num_children(track, num_containers, num_items);
    
    free(tmp_string);

    
    }

  if((idx > 0) && !gavl_dictionary_get_string(m, GAVL_META_PREVIOUS_ID))
    {
    if((sibling = gavl_value_get_dictionary(&c->current_dir.entries[idx-1])))
      gavl_dictionary_set_string(m, GAVL_META_PREVIOUS_ID, gavl_track_get_id(sibling));
    }
  
  if((idx < c->current_dir.num_entries - 1) &&
     !gavl_dictionary_get_string(m, GAVL_META_NEXT_ID))
    {
    if((sibling = gavl_value_get_dictionary(&c->current_dir.entries[idx+1])))
      gavl_dictionary_set_string(m, GAVL_META_NEXT_ID, gavl_track_get_id(sibling));
    }

  }

int bg_mdb_fs_browse_object(bg_mdb_fs_cache_t * c, const char * path,
                            gavl_dictionary_t * ret,
                            int type_mask)
  {
  int idx;
  const char * pos;
  char * parent;

  if((pos = strrchr(path, '/')))
    {
    parent = gavl_strndup(path, pos);
    pos++;
    }
  else
    {
    parent = gavl_strdup("/");
    pos = path;
    }
  if(!ensure_directory(c, parent, type_mask))
    {
    free(parent);
    return 0;
    }

  if((idx = gavl_get_track_idx_by_id_arr(&c->current_dir, pos)) < 0)
    {
    free(parent);
    return 0;
    }

  finalize(c, parent, idx, type_mask);
  
  gavl_dictionary_copy(ret, gavl_value_get_dictionary(&c->current_dir.entries[idx]));

  free(parent);
  
  return 1;
  }

int bg_mdb_fs_browse_children(bg_mdb_fs_cache_t * c, const char * path,
                              gavl_array_t * ret,
                              int start, int num,
                              int type_mask, int * total_entries)
  {
  int i;
  if(!ensure_directory(c, path, type_mask))
    return 0;
  if(!bg_mdb_adjust_num(start, &num, c->current_dir.num_entries))
    return 0;

  for(i = 0; i < num; i++)
    {
    finalize(c, path, i+start, type_mask);
    gavl_array_splice_val(ret, -1, 0, &c->current_dir.entries[i+start]);
    }

  *total_entries = c->current_dir.num_entries;
  
  return 1;
  }
