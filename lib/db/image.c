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

#define LOG_DOMAIN "db.image"

static int image_query_callback(void * data, int argc, char **argv, char **azColName)
  {
  int i;
  bg_db_image_file_t * ret = data;

  for(i = 0; i < argc; i++)
    {
    BG_DB_SET_QUERY_INT("WIDTH",   width);
    BG_DB_SET_QUERY_INT("HEIGHT",  height);
    BG_DB_SET_QUERY_DATE("DATE",   date);
    }
  ret->file.obj.found = 1;
  return 0;
  }

static int query_image(bg_db_t * db, void * a1)
  {
  char * sql;
  int result;
  bg_db_object_t * a = a1;
  
  a->found = 0;
  sql = sqlite3_mprintf("select * from IMAGE_FILES where ID = %"PRId64";", bg_db_object_get_id(a));
  result = bg_sqlite_exec(db->db, sql, image_query_callback, a);
  sqlite3_free(sql);
  if(!result || !a->found)
    return 0;
  return 1;
  }

static void update_image(bg_db_t * db, void * obj)
  {
  bg_db_image_file_t * a = obj;
  char * sql;
  char date_string[BG_DB_DATE_STRING_LEN];
  
  bg_db_date_to_string(&a->date, date_string);
  
  sql = sqlite3_mprintf("UPDATE IMAGE_FILES SET DATE = %Q, WIDTH = %d, HEIGHT = %d WHERE ID = %"PRId64";",
                        date_string, a->width, a->height, bg_db_object_get_id(a));
  bg_sqlite_exec(db->db, sql, NULL, NULL);
  sqlite3_free(sql);
  return;
  }


static void del_image(bg_db_t * db, bg_db_object_t * obj) // Delete from db
  {
  char * sql;
  int result;
  bg_sqlite_id_tab_t tab;
  bg_sqlite_id_tab_init(&tab);

  /* Delete associated thumbnails */
  
  sql = sqlite3_mprintf("SELECT ID FROM OBJECTS WHERE (TYPE = %d) & (REF_ID = %"PRId64");",
                        BG_DB_OBJECT_THUMBNAIL, obj->id);
  result = bg_sqlite_exec(db->db, sql, bg_sqlite_append_id_callback, &tab);
  sqlite3_free(sql);

  if(result)
    {
    int i;
    for(i = 0; i < tab.num_val; i++)
      {
      bg_db_object_t * image = bg_db_object_query(db, tab.val[i]);
      bg_db_object_delete(db, image);
      }
    }
  bg_sqlite_id_tab_free(&tab);
  bg_sqlite_delete_by_id(db->db, "IMAGE_FILES", obj->id);
  }


static void dump_image(void * obj)
  {
  bg_db_image_file_t*a = obj;
  gavl_diprintf(2, "Size:   %dx%d\n", a->width, a->height);
  gavl_diprintf(2, "Date:   %04d-%02d-%02d\n", a->date.year, a->date.month, a->date.day);
  }

const bg_db_object_class_t bg_db_image_file_class =
  {
    .name = "Image file",
    .del = del_image,
    .query = query_image,
    .update = update_image,
    .dump = dump_image,
    .parent = &bg_db_file_class, // Object
  };

static void del_thumbnail(bg_db_t * db, bg_db_object_t * obj) 
  {
  /* Delete thumbnail file */
  bg_db_file_t * f = (bg_db_file_t *)obj;
  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Removing %s", f->path);
  remove(f->path);
  }


const bg_db_object_class_t bg_db_thumbnail_class =
  {
    .name = "Thumbnail",
    .del = del_thumbnail,
    .parent = &bg_db_image_file_class, // Object
  };

static void del_album_cover(bg_db_t * db, bg_db_object_t * obj)
  {
  char * sql;
  int result, i;
  bg_sqlite_id_tab_t tab;
  bg_sqlite_id_tab_init(&tab);
  
  sql = sqlite3_mprintf("SELECT ID FROM AUDIO_ALBUMS WHERE COVER = %"PRId64";", obj->id);
  result = bg_sqlite_exec(db->db, sql, bg_sqlite_append_id_callback, &tab);
  sqlite3_free(sql);
  if(!result)
    return;
  
  for(i = 0; i < tab.num_val; i++)
    {
    bg_db_audio_album_t * a = bg_db_object_query(db, tab.val[i]);
    if(a)
      {
      a->cover_id = -1;
      bg_db_object_unref(a);
      }
    }
  bg_sqlite_id_tab_free(&tab);
  }


const bg_db_object_class_t bg_db_album_cover_class =
  {
    .name = "Album cover",
    .del = del_album_cover,
    .parent = &bg_db_image_file_class, // Object
  };

static void del_movie_art(bg_db_t * db, bg_db_object_t * obj)
  {
  char * sql;
  int result, i;
  bg_sqlite_id_tab_t tab;
  bg_sqlite_id_tab_init(&tab);

  sql = sqlite3_mprintf("SELECT ID FROM VIDEO_INFOS WHERE "
                        "(POSTER = %"PRId64") | (FANART = %"PRId64");", obj->id, obj->id);
  result = bg_sqlite_exec(db->db, sql, bg_sqlite_append_id_callback, &tab);
  sqlite3_free(sql);
  if(!result)
    return;

  for(i = 0; i < tab.num_val; i++)
    {
    bg_db_object_t * a = bg_db_object_query(db, tab.val[i]);
    if(a)
      {
      bg_db_video_info_t * vi = bg_db_object_get_video_info(a);
      if(vi->poster_id == obj->id)
        vi->poster_id = -1;
      if(vi->fanart_id == obj->id)
        vi->fanart_id = -1;

      bg_db_object_unref(a);
      }
    }
  bg_sqlite_id_tab_free(&tab);
  }


const bg_db_object_class_t bg_db_movie_art_class =
  {
    .name = "Movie art",
    .del = del_movie_art,
    .parent = &bg_db_image_file_class, // Object
  };


void bg_db_image_file_create_from_ti(bg_db_t * db, void * obj, gavl_dictionary_t * ti)
  {
  char * sql;
  char date_string[BG_DB_DATE_STRING_LEN];
  const gavl_video_format_t * fmt;
  const gavl_dictionary_t * m;
  
  bg_db_image_file_t * f = obj;
  bg_db_object_set_type(obj, BG_DB_OBJECT_IMAGE_FILE);
  
  fmt = gavl_track_get_video_format(ti, 0);

  m = gavl_track_get_metadata(ti);
  
  f->width  = fmt->image_width;
  f->height = fmt->image_height;

  /* Date */
  bg_db_date_to_string(&f->date, date_string);

  /* Creation date (comes from exit data) */
  if(!gavl_dictionary_get_date(m, GAVL_META_DATE_CREATE, &f->date.year, &f->date.month, &f->date.day))
    bg_db_date_set_invalid(&f->date);

  sql = sqlite3_mprintf("INSERT INTO IMAGE_FILES ( ID, WIDTH, HEIGHT, DATE ) VALUES ( %"PRId64", %d, %d, %Q);",
                          bg_db_object_get_id(f), f->width, f->height, date_string);
  bg_sqlite_exec(db->db, sql, NULL, NULL);
  sqlite3_free(sql);
  }

static int detect_album_cover(bg_db_t * db, bg_db_image_file_t * f)
  {
  int result = 0;
  int found = 0;
  char * sql;
  int id = -1;
  const char * basename;
  bg_db_audio_file_t * song = NULL;
  bg_db_audio_album_t * album = NULL;

  /* Check if there are music files in the same directory */
  sql = sqlite3_mprintf("SELECT ID FROM OBJECTS WHERE TYPE = %d and PARENT_ID =  %"PRId64";",
                        BG_DB_OBJECT_AUDIO_FILE, f->file.obj.parent_id);
  result = bg_sqlite_exec(db->db, sql, bg_sqlite_int_callback, &id);
  sqlite3_free(sql);
  if(!result || (id <= 0))
    goto end;

  /* Check if the file has a valid album */
  song = bg_db_object_query(db, id);
  if(!song || (song->album_id <= 0))
    goto end;

  album = bg_db_object_query(db, song->album_id);
  if(album->cover_id > 0)
    goto end;

  basename = bg_db_object_get_label(f);
  if(!strcasecmp(basename, "cover") ||
     !strcasecmp(basename, "folder") ||
     !strcasecmp(basename, album->title))
    found = 1;

  end:

  if(found)
    {
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Using %s as cover for album %s",
           f->file.path, album->title);
    album->cover_id = bg_db_object_get_id(f);
    bg_db_object_set_type(f, BG_DB_OBJECT_ALBUM_COVER);
    
    bg_db_make_thumbnail(db, f, 600, 600, "image/jpeg"); // "large"
    bg_db_make_thumbnail(db, f, 248, 248, "image/jpeg"); // NMJ
    bg_db_make_thumbnail(db, f, 160, 160, "image/jpeg"); // Web, DLNA JPEG_TN
    }
  if(song)
    bg_db_object_unref(song);
  if(album)
    bg_db_object_unref(album);
  return found;
  }

static int detect_movie_art(bg_db_t * db, bg_db_image_file_t * f)
  {
  int is_fanart = 0;
  char * pos;
  char * sql;
  int result;
  int ret = 0;
  bg_sqlite_id_tab_t tab;
  bg_db_object_t * obj;
  bg_db_video_info_t * vi;
  char * label = gavl_strdup(f->file.obj.label);
  bg_sqlite_id_tab_init(&tab);  

  if((pos = strrchr(label, '.')) && !strncasecmp(pos, ".fanart", 7))
    {
    is_fanart = 1;
    *pos = '\0';
    }

  sql = sqlite3_mprintf("select ID from OBJECTS where (LABEL = %Q) & "
                          "((TYPE = %d) | "
                          "(TYPE = %d) | "
                          "(TYPE = %d) | "
                          "(TYPE = %d) | "
                          "(TYPE = %d));",
                          label, 
                          BG_DB_OBJECT_MOVIE, 
                          BG_DB_OBJECT_TVSERIES, 
                          BG_DB_OBJECT_SEASON, 
                          BG_DB_OBJECT_VIDEO_EPISODE,
                          BG_DB_OBJECT_MOVIE_MULTIPART);

   result = bg_sqlite_exec(db->db, sql, bg_sqlite_append_id_callback, &tab);
   sqlite3_free(sql);
   if(result)
     {
     if(tab.num_val > 1)
       gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "File %s matches more than one movie, taking first one", f->file.path);
     
     if(tab.num_val > 0)
       {
       if((obj = bg_db_object_query(db, tab.val[0])) &&
          (vi = bg_db_object_get_video_info(obj)))
         {
         if(is_fanart)
           {
           vi->fanart_id = bg_db_object_get_id(f);
           gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Using %s as fanart", f->file.path);
           bg_db_make_thumbnail(db, f, 1280, 720, "image/jpeg"); // NMT Wallpaper
           }
         else
           {
           vi->poster_id = bg_db_object_get_id(f);
           gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Using %s as poster", f->file.path);
           bg_db_make_thumbnail(db, f, 124, 185, "image/jpeg"); // NMT Thumbnail
           bg_db_make_thumbnail(db, f, 220, 330, "image/jpeg"); // NMT Poster
           }
         ret = 1;
         bg_db_object_set_type(f, BG_DB_OBJECT_MOVIE_ART);
         }
       if(obj)
         bg_db_object_unref(obj);
       }
     }
  
   free(label);
   bg_sqlite_id_tab_free(&tab);
   return ret;
   }

void bg_db_identify_images(bg_db_t * db, int64_t scan_dir_id, int scan_flags)
  {
  int i;
  int result;
  char * sql;
  bg_db_image_file_t * f;

  bg_sqlite_id_tab_t tab;
  bg_sqlite_id_tab_init(&tab);

  /* Get all unidentified images from the scan directory */  

  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Finding unidentifies images");

  sql = sqlite3_mprintf("select ID from OBJECTS where TYPE = %d;", BG_DB_OBJECT_IMAGE_FILE);
  result = bg_sqlite_exec(db->db, sql, bg_sqlite_append_id_callback, &tab);
  sqlite3_free(sql);

  if(!result)
    return;

  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Found %d unidentifies images", tab.num_val);

  for(i = 0; i < tab.num_val; i++)
    {
    f = bg_db_object_query(db, tab.val[i]);

    if(f->file.scan_dir_id != scan_dir_id)
      {
      bg_db_object_unref(f);
      continue;
      }
    
    if((scan_flags & BG_DB_SCAN_AUDIO) && detect_album_cover(db, f))
      {
      bg_db_object_unref(f);
      continue;
      }
    if((scan_flags & BG_DB_SCAN_VIDEO) && detect_movie_art(db, f))
      {
      bg_db_object_unref(f);
      continue;
      }


    bg_db_object_unref(f);
    }
  bg_sqlite_id_tab_free(&tab);
  }

