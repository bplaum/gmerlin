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

#include <mediadb_private.h>
#include <gmerlin/log.h>
#include <gmerlin/utils.h>
#include <string.h>

#include <sys/types.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#define LOG_DOMAIN "db.file"

#define PATH_COL        1
#define MTIME_COL       2
#define MIMETYPE_COL    3
#define SCAN_DIR_ID_COL 4

static int query_file(bg_db_t * db, void * file1)
  {
  int result;
  bg_db_file_t * f = file1;
  int found = 0;

  sqlite3_stmt * st = db->q_files;

  sqlite3_bind_int64(st, 1, f->obj.id);
  
  if((result = sqlite3_step(st)) == SQLITE_ROW)
    {
    BG_DB_GET_COL_STRING(PATH_COL, f->path);
    BG_DB_GET_COL_MTIME(MTIME_COL, f->mtime);
    BG_DB_GET_COL_INT(MIMETYPE_COL, f->mimetype_id);
    BG_DB_GET_COL_INT(SCAN_DIR_ID_COL, f->scan_dir_id);
    found = 1;
    }
  sqlite3_reset(st);
  sqlite3_clear_bindings(st);
  
  if(!found)
    return 0;
  
  f->path = bg_db_filename_to_abs(db, f->path);
  
  f->mimetype = bg_db_string_cache_get(db->mimetypes, db->db,
                                       f->mimetype_id);
  return 1;
  }

static void del_file(bg_db_t * db, bg_db_object_t * obj) // Delete from db
  {
  bg_sqlite_delete_by_id(db->db, "FILES", obj->id);
  }

static void free_file(void * obj)
  {
  bg_db_file_t * f = obj;
  if(f->path)
    free(f->path);
  if(f->mimetype)
    free(f->mimetype);
  }

static void dump_file(void * obj)
  {
  bg_db_file_t * f = obj;
  gavl_diprintf(2, "Path:        %s\n", f->path);
  gavl_diprintf(2, "Mtime:       %"PRId64"\n", (int64_t)f->mtime);
  gavl_diprintf(2, "Mimetype:    %s\n", f->mimetype);
  gavl_diprintf(2, "Scan dir ID: %"PRId64"\n", f->scan_dir_id);
  }

const bg_db_object_class_t bg_db_file_class =
  {
  .name = "File",
  .dump = dump_file,
  .del = del_file,
  .free = free_file,
  .query = query_file,
  .parent = NULL,  /* Object */
  };

static int file_add(bg_db_t * db, bg_db_file_t * f)
  {
  char * sql;
  int result;
  char mtime_str[BG_DB_TIME_STRING_LEN];
  
  /* Mimetype */
  if(f->mimetype)
    {
    f->mimetype_id = 
      bg_sqlite_string_to_id_add(db->db, "MIMETYPES",
                                 "ID", "NAME", f->mimetype);
    }
  /* Mtime */
  bg_db_time_to_string(f->mtime, mtime_str);

#if 1
  sql = sqlite3_mprintf("INSERT INTO FILES ( ID, PATH, MTIME, MIMETYPE, SCAN_DIR_ID ) VALUES ( %"PRId64", %Q, %Q, %"PRId64", %"PRId64" );",
                        f->obj.id, bg_db_filename_to_rel(db, f->path), mtime_str, f->mimetype_id, f->scan_dir_id);

  result = bg_sqlite_exec(db->db, sql, NULL, NULL);
  sqlite3_free(sql);
#endif
  return result;
  }

static int compare_file_by_path(const bg_db_object_t * obj, const void * data)
  {
  const bg_db_file_t * file;
  if(obj->type & BG_DB_FLAG_FILE)
    {
    file = (const bg_db_file_t*)obj;
    if(!strcmp(file->path, data))
      return 1;
    }
  return 0;
  } 

int64_t bg_db_file_by_path(bg_db_t * db, const char * path)
  {
  int64_t ret;

  ret = bg_db_cache_search(db, compare_file_by_path,
                           path);
  if(ret > 0)
    return ret;

  
  return bg_sqlite_string_to_id(db->db, "FILES", "ID", "PATH",
                                bg_db_filename_to_rel(db, path));
  }

static const struct
  {
  const char * ext;
  const char * mimetype;
  }
text_mimetypes[] =
  {
    { ".nfo", "text/x-nfo" },
    { ".txt", "text/plain" },
    { ".htm .html",  "text/html" },
    { ".srt",  "text/plain" },
    { ".sub",  "text/plain" },
    { ".meta",  "text/x-gmerlin-metadata" },
    { },
  };
  
  
/* Open the file, load metadata and so on */
bg_db_file_t *
bg_db_file_create_from_object(bg_db_t * db, bg_db_object_t * obj, int scan_flags,
                              bg_db_scan_item_t * item,
                              int64_t scan_dir_id)
  {
  int i;
  gavl_dictionary_t * ti;
  bg_plugin_handle_t * h = NULL;
  bg_input_plugin_t * plugin = NULL;
  bg_db_file_t * file;
  bg_db_object_type_t type;
  const char * ext;
  int num_audio_streams;
  int num_video_streams;
  int num_text_streams;
  int num_overlay_streams;

  const gavl_dictionary_t * m;
  gavl_time_t duration;

  const char * mimetype_ptr;
  
  bg_db_object_set_type(obj, BG_DB_OBJECT_FILE);

  file = (bg_db_file_t *)obj;
  file->path = item->path;
  item->path = NULL;

  file->mtime = item->mtime;
  file->scan_dir_id = scan_dir_id;
  
  bg_db_object_update_size(db, file, item->size);
  bg_db_object_set_label_nocpy(file, bg_path_to_label(file->path));

  /* Detect text files */

  if((ext = strrchr(file->path, '.')))
    {
    i = 0;
    while(text_mimetypes[i].ext)
      {
      if(bg_string_match(ext, text_mimetypes[i].ext))
        {
        file->mimetype = gavl_strdup(text_mimetypes[i].mimetype);
        file_add(db, file);
        return file;
        }
      i++;
      }
    }
  
  
  /* Load all infos */  
  if(!bg_input_plugin_load(db->plugin_reg, file->path, &h, NULL))
    goto fail;
  plugin = (bg_input_plugin_t *)h->plugin;
  
  /* Only one track supported */
  if(bg_input_plugin_get_num_tracks(h) < 1)
    goto fail;

  ti = bg_input_plugin_get_track_info(h, 0);
  
  num_audio_streams = gavl_track_get_num_audio_streams(ti);
  num_video_streams = gavl_track_get_num_video_streams(ti);
  num_text_streams = gavl_track_get_num_text_streams(ti);
  num_overlay_streams = gavl_track_get_num_overlay_streams(ti);
  m = gavl_track_get_metadata(ti);
  
  /* Detect file type */
  if((num_audio_streams == 1) && !num_video_streams)
    type = BG_DB_OBJECT_AUDIO_FILE;
  else if(num_video_streams == 1)
    {
    const gavl_video_format_t * fmt;
    fmt = gavl_track_get_video_format(ti, 0);

    if(!num_audio_streams && (num_video_streams == 1) &&
       (fmt->framerate_mode == GAVL_FRAMERATE_STILL))
      type = BG_DB_OBJECT_IMAGE_FILE;
    else
      type = BG_DB_OBJECT_VIDEO_FILE;
    }

  gavl_dictionary_get_src(m, GAVL_META_SRC, 0, &mimetype_ptr, NULL);
  file->mimetype = gavl_strdup(mimetype_ptr);

  /* Check if we want to add this file */
  switch(type)
    {
    case BG_DB_OBJECT_AUDIO_FILE:
      if(!(scan_flags & BG_DB_SCAN_AUDIO))
        goto fail;
      break;
    case BG_DB_OBJECT_VIDEO_FILE:
      if(!(scan_flags & BG_DB_SCAN_VIDEO))
        goto fail;
      break;
    case BG_DB_OBJECT_IMAGE_FILE: // Images can also be movie poster and album covers
      break;
    default:
      break;
    }

  /* */
  
  if(!bg_input_plugin_set_track(h, 0))
    goto fail;
  
  /* Start everything */
  if(plugin->set_audio_stream)
    {
    for(i = 0; i < num_audio_streams; i++)
      plugin->set_audio_stream(h->priv, i, BG_STREAM_ACTION_DECODE);
    }
  if(plugin->set_video_stream)
    {
    for(i = 0; i < num_video_streams; i++)
      plugin->set_video_stream(h->priv, i, BG_STREAM_ACTION_DECODE);
    }
  if(plugin->set_text_stream)
    {
    for(i = 0; i < num_text_streams; i++)
      plugin->set_text_stream(h->priv, i, BG_STREAM_ACTION_DECODE);
    }
  if(plugin->set_overlay_stream)
    {
    for(i = 0; i < num_overlay_streams; i++)
      plugin->set_overlay_stream(h->priv, i, BG_STREAM_ACTION_DECODE);
    }
  if(plugin->start && !plugin->start(h->priv))
    goto fail;

  if(gavl_dictionary_get_long(m, GAVL_META_APPROX_DURATION,
                             &duration))
    bg_db_object_update_duration(db, file, duration);
  
  /* Create derived type */
  switch(type)
    {
    case BG_DB_OBJECT_AUDIO_FILE:
      bg_db_audio_file_create(db, file, ti);
      break;
    case BG_DB_OBJECT_VIDEO_FILE:
      bg_db_video_file_create(db, file, ti);
      break;
    case BG_DB_OBJECT_IMAGE_FILE:
      bg_db_image_file_create_from_ti(db, file, ti);
      break;
    default:
      break;
    }
  
  fail:

  if(plugin)
    plugin->close(h->priv);

  if(h)
    bg_plugin_unref(h);

  file_add(db, file);
  return file; 
  }

void bg_db_file_create(bg_db_t * db, int scan_flags,
                       bg_db_scan_item_t * item,
                       bg_db_dir_t ** dir, int64_t scan_dir_id)
  {
  bg_db_object_t * obj;
  bg_db_file_t * file;
    
  /* Make sure we have the right parent directoy */
  if(!(*dir = bg_db_dir_ensure_parent(db, *dir, item->path)))
    return;
  
  obj = bg_db_object_create(db);
  file = bg_db_file_create_from_object(db, obj, scan_flags, item, scan_dir_id);

  bg_db_object_set_parent(db, file, *dir);
  bg_db_object_unref(file);
  }

bg_db_file_t * bg_db_file_create_internal(bg_db_t * db, const char * path_rel)
  {
  bg_db_file_t * file;
  bg_db_object_t * obj;

  char * path_abs = gavl_strdup(path_rel);
  bg_db_scan_item_t item;
  
  memset(&item, 0, sizeof(item));
  path_abs = bg_db_filename_to_abs(db, path_abs);
  if(!bg_db_scan_item_set(&item, path_abs))
    return NULL;
  obj = bg_db_object_create(db);
  if((file = bg_db_file_create_from_object(db, obj, ~0, &item, -1)))
    bg_db_object_set_parent_id(db, file, -1);
  bg_db_scan_item_free(&item);
  return file;
  }

void bg_db_add_files(bg_db_t * db, bg_db_scan_item_t * files, int num,
                     int scan_flags, int64_t scan_dir_id)
  {
  int i;
  
  bg_db_dir_t * dir = NULL;
  
  for(i = 0; i < num; i++)
    {
    if(files[i].done)
      continue;
    
    switch(files[i].type)
      {
      case BG_DB_DIRENT_DIRECTORY:
        {
        bg_db_dir_create(db, scan_flags, &files[i],
                         &dir, &scan_dir_id);
        }
        break;
      case BG_DB_DIRENT_FILE:
        {
        bg_db_file_create(db, scan_flags, &files[i],
                          &dir, scan_dir_id);
        }
        break;
      }
    }
  if(dir)
    bg_db_object_unref(dir);
  }

static bg_db_scan_item_t * find_by_path(bg_db_scan_item_t * files, int num, 
                                        char * path, bg_db_dirent_type_t type)
  {
  int i;
  for(i = 0; i < num; i++)
    {
    if((files[i].type == type) && !strcmp(files[i].path, path))
      return &files[i];
    }
  return NULL;
  }

void bg_db_update_files(bg_db_t * db, bg_db_scan_item_t * files, 
                        int num, int scan_flags,
                        int64_t scan_dir_id, const char * scan_dir)
  {
  char * sql;
  int result;
  bg_db_file_t * file;
  bg_db_dir_t * dir;
  
  int i;
  bg_db_scan_item_t * si;
  
  bg_sqlite_id_tab_t tab;
  bg_sqlite_id_tab_init(&tab);
  
  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Getting directories from database");
  sql =
    sqlite3_mprintf("select ID from DIRECTORIES where SCAN_DIR_ID = %"PRId64";", scan_dir_id);
  result = bg_sqlite_exec(db->db, sql, bg_sqlite_append_id_callback, &tab);
  sqlite3_free(sql);

  //  fprintf(stderr, "SQL: %s\n", sql);
  
  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Found %d directories in database", tab.num_val);

  for(i = 0; i < tab.num_val; i++)
    {
    dir = bg_db_object_query(db, tab.val[i]);

    if(!dir) // dir can be gone already if the parent directory vanished
      continue;
    
    if(!strcmp(dir->path, scan_dir))
      {
      bg_db_object_unref(dir);
      continue;
      }
    
    si = find_by_path(files, num, dir->path, BG_DB_DIRENT_DIRECTORY);
    if(!si)
      {
      gavl_log(GAVL_LOG_INFO, LOG_DOMAIN,
             "Directory %s disappeared, removing from database", dir->path);
      bg_db_object_delete(db, dir);
      }
    else
      {
      bg_db_object_unref(dir);
      si->done = 1;
      }
    }

  bg_sqlite_id_tab_reset(&tab);
  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Getting files from database");
  
  sql =
    sqlite3_mprintf("select ID from FILES where SCAN_DIR_ID = %"PRId64";", scan_dir_id);
  result = bg_sqlite_exec(db->db, sql, bg_sqlite_append_id_callback, &tab);
  sqlite3_free(sql);
  
  if(!result)
    goto fail;
  
  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Found %d files in database", tab.num_val);

  for(i = 0; i < tab.num_val; i++)
    {
    file = bg_db_object_query(db, tab.val[i]);
    
    si = find_by_path(files, num, file->path, BG_DB_DIRENT_FILE);
    if(!si)
      {
      gavl_log(GAVL_LOG_INFO, LOG_DOMAIN,
             "File %s disappeared, removing from database", file->path);
      bg_db_object_delete(db, file);
      }
    else if(si->mtime != file->mtime)
      {
      gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, 
             "File %s changed on disk, removing from database for re-adding later", file->path);
      bg_db_object_delete(db, file);
      }
    else
      {
      si->done = 1;
      bg_db_object_unref(file);
      }
    }

  bg_db_add_files(db, files, num, scan_flags, scan_dir_id);
  
  fail:
  bg_sqlite_id_tab_free(&tab);
  
  }
