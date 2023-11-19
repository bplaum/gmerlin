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

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>

#include <gmerlin/translation.h>

#include <gmerlin/log.h>

#define LOG_DOMAIN "mdb_thumbnail"

#include <gmerlin/mdb.h>

#include <gmerlin/utils.h>

#include <mdb_private.h>

#include <gavl/metatags.h>

#define META_MIMETYPE_ID "MIMETYPE_ID"

/* Create thumbs */

static int make_thumbnail(bg_mdb_t * mdb, int64_t image_id,
                          const char * filename,
                          gavl_video_format_t * format,
                          gavl_video_frame_t ** f,
                          gavl_dictionary_t * metadata,
                          int width, int height)
  {
  char * tn_file = NULL;
  char * tn_path = NULL;
  char * real_path = NULL;
  int64_t thumbnail_id = -1;
  int64_t mimetype_id = -1;
  char * sql;
  int result = 0;
  const char * mimetype = "image/jpeg";
  
  /* Next upscale images */
  if((format->image_width <= width) ||
     (format->image_height <= height))
    goto end;

  /* Make sure the directory exists */
  tn_path = bg_sprintf("%s/thumbnails/%dx%d", mdb->path, width, height);
  bg_ensure_directory(tn_path, 0);
  free(tn_path);
  tn_path = NULL;

  /* */
  
  tn_file = bg_sprintf("%dx%d/%"PRId64".%s",
                       width, height, image_id,
                       bg_mimetype_to_ext(mimetype));
  
  tn_path = bg_sprintf("%s/thumbnails/%s",
                       mdb->path, tn_file);

  /* Check if the thumbnail already exists. We just check for the existence of the file since the
   mtime check is done by bg_mdb_purge_thumbnails() */
  
  if(!access(tn_path, R_OK))
    goto end;

  /* Load the file if necessary */

  if(!(*f))
    {
    *f = bg_plugin_registry_load_image(bg_plugin_reg,
                                           filename,
                                           format, metadata);
    
    if(!(*f))
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Loading %s failed", filename);
      goto end;
      }
    }
  
  real_path = bg_make_thumbnail(*f,
                                format,
                                &width, &height,
                                tn_path,
                                mimetype, metadata);

  if(!real_path)
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Creating thumbnail failed");
  else
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Created thumbnail %s", real_path);

  mimetype_id = bg_sqlite_string_to_id_add(mdb->thumbnail_db,
                                           "mimetypes",
                                           "ID",
                                           GAVL_META_MIMETYPE,
                                           mimetype);

  thumbnail_id = bg_sqlite_get_max_int(mdb->thumbnail_db, "thumbnails", "ID")+1;
  
  sql = sqlite3_mprintf("INSERT INTO thumbnails "
                        "(ID, PARENT, "GAVL_META_WIDTH", "GAVL_META_HEIGHT", "META_MIMETYPE_ID", "GAVL_META_URI") VALUES "
                        "(%"PRId64", %"PRId64", %d, %d, %"PRId64", %Q);", thumbnail_id, image_id, width, height, mimetype_id, tn_file);
  
  bg_sqlite_exec(mdb->thumbnail_db, sql, NULL, NULL);
  sqlite3_free(sql);

  result = 1;
  
  end:

  if(tn_file)
    free(tn_file);

  if(tn_path)
    free(tn_path);

  if(real_path)
    free(real_path);

  return result;
  }
  
void bg_mdb_make_thumbnails(bg_mdb_t * mdb, const char * filename)
  {
  int64_t image_id;
  gavl_video_frame_t * in_frame = NULL;
  gavl_video_format_t in_format;
  gavl_dictionary_t metadata;

  gavl_dictionary_init(&metadata);

  if(!strncasecmp(filename, "file://", 7))
    filename += 7;

  if(strstr(filename, "://"))
    return;
  
  pthread_mutex_lock(&mdb->thumbnail_mutex);
  bg_sqlite_start_transaction(mdb->thumbnail_db);
    
  /* Check if the file is already there. */

  image_id = bg_sqlite_string_to_id(mdb->thumbnail_db,
                                    "images",
                                    "ID",
                                    GAVL_META_URI,
                                    filename);

  
  if(image_id < 0)
    {
    char * sql;
    struct stat st;

    if(stat(filename, &st))
      goto end;
    
    /* Create entry */
    
    image_id = bg_sqlite_get_max_int(mdb->thumbnail_db, "images", "ID") + 1;

    sql = sqlite3_mprintf("INSERT INTO images (ID, "GAVL_META_URI", "GAVL_META_MTIME") VALUES "
                          "(%"PRId64", %Q, %"PRId64");", image_id, filename, (int64_t)st.st_mtime);
    bg_sqlite_exec(mdb->thumbnail_db, sql, NULL, NULL);
    sqlite3_free(sql);
    }
    
  if(!make_thumbnail(mdb, image_id,
                     filename,
                     &in_format, // const gavl_video_format_t * format,
                     &in_frame, // const gavl_video_frame_t * fr,
                     &metadata,
                     160, 160))
    goto end;

  if(!make_thumbnail(mdb, image_id,
                     filename,
                     &in_format, // const gavl_video_format_t * format,
                     &in_frame, // const gavl_video_frame_t * fr,
                     &metadata,
                     320, 320))
    goto end;

  if(!make_thumbnail(mdb, image_id,
                     filename,
                     &in_format, // const gavl_video_format_t * format,
                     &in_frame, // const gavl_video_frame_t * fr,
                     &metadata,
                     600, 600))
    goto end;

  
  /*
   *  We *don't check against mtime here since this is done by
   *  bg_mdb_purge_thumbnails()
   */

  end:
  
  if(in_frame)
    gavl_video_frame_destroy(in_frame);
  
  bg_sqlite_end_transaction(mdb->thumbnail_db);
  pthread_mutex_unlock(&mdb->thumbnail_mutex);

  gavl_dictionary_free(&metadata);
  
  }


typedef struct
  {
  const bg_mdb_t * mdb;
  gavl_dictionary_t * m;
  const char * tag;

  //  int64_t parent;
  
  } get_tn_t;

static int
get_thumbnail_callback(void * data, int argc, char **argv, char **azColName)
  {
  int mimetype_id;
  int width  = -1;
  int height = -1;
  int i = 0;
  char * mimetype = NULL;
  char * uri = NULL;
  

  get_tn_t * tn = data;
  
  for(i = 0; i < argc; i++)
    {
    if(!strcmp(azColName[i], GAVL_META_WIDTH))
      width = atoi(argv[i]);
    if(!strcmp(azColName[i], GAVL_META_HEIGHT))
      height = atoi(argv[i]);
    if(!strcmp(azColName[i], META_MIMETYPE_ID))
      {
      mimetype_id = atoi(argv[i]);
      mimetype = bg_sqlite_id_to_string(tn->mdb->thumbnail_db,
                                        "mimetypes",
                                        GAVL_META_MIMETYPE,
                                        "ID", mimetype_id);
      }
    if(!strcmp(azColName[i], GAVL_META_URI))
      uri = bg_sprintf("%s/thumbnails/%s", tn->mdb->path, argv[i]);
    }
  
  gavl_metadata_add_image_uri(tn->m, tn->tag, width, height, mimetype, uri);
  
  if(uri)
    free(uri);
  if(mimetype)
    free(mimetype);
  return 0;
  }

/* Get thumbnails for a particular track */
void bg_mdb_get_thumbnails(bg_mdb_t * mdb, gavl_dictionary_t * track)
  {
  int64_t id;
  char * sql;
  const char * path;
  const char * var;
  get_tn_t tn;
  
  memset(&tn, 0, sizeof(tn));
  tn.m = gavl_track_get_metadata_nc(track);
  tn.mdb = mdb;
  
  if(!tn.m)
    return;
  
  pthread_mutex_lock(&mdb->thumbnail_mutex);
  /* Image */  
  if((var = gavl_dictionary_get_string(tn.m, GAVL_META_MEDIA_CLASS)) &&
     gavl_string_starts_with(var, GAVL_META_MEDIA_CLASS_IMAGE) &&
     (path = gavl_dictionary_get_string_image_uri(tn.m, GAVL_META_SRC, 0, NULL, NULL, NULL)) &&
     ((id = bg_sqlite_string_to_id(mdb->thumbnail_db, "images", "ID", GAVL_META_URI, path)) > 0))
    {
    sql = bg_sprintf("SELECT * FROM thumbnails WHERE PARENT = %"PRId64";", id);
    tn.tag = GAVL_META_ICON_URL;
    bg_sqlite_exec(mdb->thumbnail_db, sql, get_thumbnail_callback, &tn);
    free(sql);
    }
  else if((path = gavl_dictionary_get_string_image_uri(tn.m, GAVL_META_ICON_URL, 0, NULL, NULL, NULL)) &&
     ((id = bg_sqlite_string_to_id(mdb->thumbnail_db, "images", "ID", GAVL_META_URI, path)) > 0))
    {
    sql = bg_sprintf("SELECT * FROM thumbnails WHERE PARENT = %"PRId64";", id);
    tn.tag = GAVL_META_ICON_URL;
    bg_sqlite_exec(mdb->thumbnail_db, sql, get_thumbnail_callback, &tn);
    free(sql);
    }
  
  /* Album cover */  
  if((path = gavl_dictionary_get_string_image_uri(tn.m, GAVL_META_COVER_URL, 0, NULL, NULL, NULL)) &&
     ((id = bg_sqlite_string_to_id(mdb->thumbnail_db, "images", "ID", GAVL_META_URI, path)) > 0))
    {
    sql = bg_sprintf("SELECT * FROM thumbnails WHERE PARENT = %"PRId64";", id);
    tn.tag = GAVL_META_COVER_URL;
    bg_sqlite_exec(mdb->thumbnail_db, sql, get_thumbnail_callback, &tn);
    free(sql);
    }
  
  /* Video poster */  
  if((path = gavl_dictionary_get_string_image_uri(tn.m, GAVL_META_POSTER_URL, 0, NULL, NULL, NULL)) &&
     ((id = bg_sqlite_string_to_id(mdb->thumbnail_db, "images", "ID", GAVL_META_URI, path)) > 0))
    {
    sql = bg_sprintf("SELECT * FROM thumbnails WHERE PARENT = %"PRId64";", id);
    tn.tag = GAVL_META_POSTER_URL;
    bg_sqlite_exec(mdb->thumbnail_db, sql, get_thumbnail_callback, &tn);
    free(sql);
    }
  
  pthread_mutex_unlock(&mdb->thumbnail_mutex);
  }

void bg_mdb_clear_thumbnail_uris(gavl_dictionary_t * track)
  {
  gavl_array_t * arr;
  gavl_dictionary_t * m = gavl_track_get_metadata_nc(track);

  if(!m)
    return;

  if((arr = gavl_dictionary_get_array_nc(m, GAVL_META_COVER_URL)))
    gavl_array_splice_val(arr, 1, -1, NULL);
  if((arr = gavl_dictionary_get_array_nc(m, GAVL_META_POSTER_URL)))
    gavl_array_splice_val(arr, 1, -1, NULL);
  //  if((arr = gavl_dictionary_get_array_nc(m, GAVL_META_ICON_URL)))
  //    gavl_array_splice_val(arr, 1, -1, NULL);
  }

/* Global funcs */

/* Delete thumbnails for a particular file */
void bg_mdb_cleanup_thumbnails(bg_mdb_t * mdb)
  {
  pthread_mutex_destroy(&mdb->thumbnail_mutex);
  sqlite3_close(mdb->thumbnail_db);
  }

static int create_tables(bg_mdb_t * mdb)
  {
  /* Object table */  
  if(!bg_sqlite_exec(mdb->thumbnail_db,
                     "CREATE TABLE IF NOT EXISTS thumbnails(ID INTEGER PRIMARY KEY, "
                     "PARENT INTEGER, "GAVL_META_WIDTH" INTEGER, "GAVL_META_HEIGHT" INTEGER, "META_MIMETYPE_ID" INTEGER, "GAVL_META_URI" TEXT);",
                     NULL, NULL) ||
     !bg_sqlite_exec(mdb->thumbnail_db,
                     "CREATE TABLE IF NOT EXISTS mimetypes(ID INTEGER PRIMARY KEY, "GAVL_META_MIMETYPE" TEXT);", NULL, NULL) ||
     !bg_sqlite_exec(mdb->thumbnail_db,
                     "CREATE TABLE IF NOT EXISTS images(ID INTEGER PRIMARY KEY, "GAVL_META_URI" TEXT, "GAVL_META_MTIME" INTEGER);", NULL, NULL))
    return 0;
  return 1;
  }

/* Delete thumbnails for a particular file */
void bg_mdb_init_thumbnails(bg_mdb_t * mdb)
  {
  char * filename;
  int result;
  pthread_mutex_init(&mdb->thumbnail_mutex, NULL);

  mdb->thumbs_dir = bg_sprintf("%s/thumbnails", mdb->path);
  bg_ensure_directory(mdb->thumbs_dir, 0);

  filename = bg_sprintf("%s/thumbnails/thumbs.sqlite", mdb->path);
  result = sqlite3_open(filename, &mdb->thumbnail_db);

  if(result)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Cannot open database %s: %s", filename,
           sqlite3_errmsg(mdb->thumbnail_db));
    sqlite3_close(mdb->thumbnail_db);
    mdb->thumbnail_db = NULL;
    free(filename);
    return;
    }
  free(filename);

  if(!create_tables(mdb))
    return; // Should not happen if the path is writeable
  }

/* Purge thumbnails */

static int
exists_callback(void * data, int argc, char **argv, char **azColName)
  {
  struct stat st;
  int i = 0;
  int64_t id = -1;
  int64_t mtime = -1;
  
  const char * uri = NULL;
  bg_sqlite_id_tab_t * tab = data;

  memset(&st, 0, sizeof(st));
  
  for(i = 0; i < argc; i++)
    {
    if(!strcmp(azColName[i], "ID"))
      id = strtoll(argv[i], NULL, 10);
    else if(!strcmp(azColName[i], GAVL_META_MTIME))
      mtime = strtoll(argv[i], NULL, 10);
    else if(!strcmp(azColName[i], GAVL_META_URI))
      uri = argv[i];
    }
  
  if(!uri)
    return 0;
  
  if((id > 0) && (mtime > 0) && !stat(uri, &st) && (mtime == st.st_mtime))
    return 0;

  fprintf(stderr, "File %s disappeared: %"PRId64" %"PRId64"\n",
          uri, (int64_t)mtime, (int64_t)st.st_mtime);
  
  bg_sqlite_id_tab_push(tab, id);
  
  return 0;
  }

static int
remove_thumb_callback(void * data, int argc, char **argv, char **azColName)
  {
  char * tmp_string = NULL;
  
  if(!strcmp(azColName[0], GAVL_META_URI))
    {
    tmp_string = bg_sprintf("%s/%s", (char*)data, argv[0]);
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Removing %s", tmp_string);
    remove(tmp_string);
    free(tmp_string);
    }
  return 0;
  }

void bg_mdb_purge_thumbnails(bg_mdb_t * mdb)
  {
  int i;
  char * sql;
  /* Clear thumbnails where the parent is deleted or newer */
  bg_sqlite_id_tab_t tab;

  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Purging thumbnails");
  
  pthread_mutex_lock(&mdb->thumbnail_mutex);

  bg_sqlite_id_tab_init(&tab);

  sql = bg_sprintf("SELECT * FROM images;");
  
  bg_sqlite_exec(mdb->thumbnail_db, sql, exists_callback, &tab);
  
  free(sql);
  
  for(i = 0; i < tab.num_val; i++)
    {
    /* Remove thumbnails for non-existing files */
    sql = sqlite3_mprintf("DELETE FROM images WHERE ID = %"PRId64";", tab.val[i]);
    bg_sqlite_exec(mdb->thumbnail_db, sql, NULL, NULL);
    sqlite3_free(sql);
    
    sql = sqlite3_mprintf("SELECT "GAVL_META_URI" FROM thumbnails WHERE PARENT = %"PRId64";", tab.val[i]);
    bg_sqlite_exec(mdb->thumbnail_db, sql, remove_thumb_callback, mdb->thumbs_dir);
    sqlite3_free(sql);
    
    sql = sqlite3_mprintf("DELETE FROM thumbnails WHERE PARENT = %"PRId64";", tab.val[i]);
    bg_sqlite_exec(mdb->thumbnail_db, sql, NULL, NULL);
    sqlite3_free(sql);
    }

  bg_sqlite_id_tab_free(&tab);

  pthread_mutex_unlock(&mdb->thumbnail_mutex);

  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Purging thumbnails done");
  
  }
