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
#include <unistd.h>
#include <math.h>

#include <mediadb_private.h>
#include <gmerlin/log.h>
#include <gmerlin/utils.h>
#include <string.h>

static void free_video_container(void * obj)
  {
  bg_db_video_container_t * c = obj;
  bg_db_video_info_free(&c->info);
  }

static int query_video_container(bg_db_t * db, void * obj)
  {
  bg_db_video_container_t * c = obj;
  bg_db_video_info_query(db, &c->info, obj);
  return 1;
  }

static void dump_video_container(void * obj)
  {
  bg_db_video_container_t * c = obj;
  bg_db_video_info_dump(&c->info);
  }

static void del_video_container(bg_db_t * db, bg_db_object_t * obj) // Delete from db
  {
  bg_db_video_info_delete(db, obj);
  }

static void update_video_container(bg_db_t * db, void * obj)
  {
  bg_db_video_container_t * c = obj;
  bg_db_video_info_update(db, &c->info, obj);
  }

static void get_children_video_container(bg_db_t * db, void * obj, bg_sqlite_id_tab_t * tab)
  {
  char * sql;
  sql = sqlite3_mprintf("SELECT ID FROM VIDEO_FILES WHERE COLLECTION = %"PRId64" ORDER BY IDX;",
                        bg_db_object_get_id(obj));
  bg_sqlite_exec(db->db, sql, bg_sqlite_append_id_callback, tab);
  sqlite3_free(sql);
  }


const bg_db_object_class_t bg_db_series_class =
  {
  .name = "Series",
  .del = del_video_container,
  .free = free_video_container,
  .query = query_video_container,
  .dump = dump_video_container,
  .update = update_video_container,
  .parent = NULL,
  };

const bg_db_object_class_t bg_db_season_class =
  {
  .name = "Season",
  .del = del_video_container,
  .free = free_video_container,
  .query = query_video_container,
  .dump = dump_video_container,
  .update = update_video_container,
  .get_children = get_children_video_container,
  .parent = NULL,
  };

const bg_db_object_class_t bg_db_multipart_movie_class =
  {
  .name = "Multipart movie",
  .del = del_video_container,
  .free = free_video_container,
  .query = query_video_container,
  .dump = dump_video_container,
  .update = update_video_container,
  .get_children = get_children_video_container,
  .parent = NULL,
  };

static int compare_show(const bg_db_object_t * obj, const void * data)
  {
  if((obj->type == BG_DB_OBJECT_TVSERIES) && !strcmp(obj->label, data))
    return 1;
  return 0;
  }


bg_db_object_t * bg_db_tvseries_query(bg_db_t * db, const char * label)
  {
  int64_t ret;
  char * sql;
  bg_db_video_container_t * obj;

  /* Query cache */
  ret = bg_db_cache_search(db, compare_show, label);
  if(ret > 0)
    return bg_db_object_query(db, ret);

  /* Query db */
  ret = -1;
  sql = sqlite3_mprintf("SELECT ID FROM OBJECTS WHERE (TYPE = %d) & (LABEL = %Q);",
                        BG_DB_OBJECT_TVSERIES, label);
  bg_sqlite_exec(db->db, sql, bg_sqlite_int_callback, &ret);
  sqlite3_free(sql);

  if(ret > 0)
    return bg_db_object_query(db, ret);

  /* Create object */
  obj = bg_db_object_create(db);
  bg_db_object_set_type(obj, BG_DB_OBJECT_TVSERIES);
  bg_db_object_set_parent_id(db, obj, -1);
  bg_db_object_set_label(obj, label);
  bg_db_video_info_init(&obj->info);
  bg_db_video_info_set_title(&obj->info, label);
  bg_db_video_info_insert(db, &obj->info, obj);

  return (bg_db_object_t *)obj;
  }

static int compare_season(const bg_db_object_t * obj, const void * data)
  {
  if((obj->type == BG_DB_OBJECT_SEASON) && !strcmp(obj->label, data))
    return 1;
  return 0;
  }


bg_db_object_t * bg_db_season_query(bg_db_t * db,
                                    bg_db_object_t * show,
                                    const char * label, int idx)
  {
  int64_t ret;
  char * sql;
  bg_db_video_container_t * obj;

  /* Query cache */
  ret = bg_db_cache_search(db, compare_season, label);
  if(ret > 0)
    return bg_db_object_query(db, ret);

  /* Query db */
  ret = -1;
  sql = sqlite3_mprintf("SELECT ID FROM OBJECTS WHERE (TYPE = %d) & (LABEL = %Q);",
                        BG_DB_OBJECT_SEASON, label);
  bg_sqlite_exec(db->db, sql, bg_sqlite_int_callback, &ret);
  sqlite3_free(sql);
  
  if(ret > 0)
    return bg_db_object_query(db, ret);

  /* Create object */
  obj = bg_db_object_create(db);
  bg_db_object_set_type(obj, BG_DB_OBJECT_SEASON);
  bg_db_object_set_parent(db, obj, show);
  bg_db_object_set_label(obj, label);

  /* Set friendly title */
  bg_db_video_info_set_title_nocpy(&obj->info, bg_sprintf("Season %d", idx));
  
  bg_db_video_info_insert(db, &obj->info, obj);
  return (bg_db_object_t*)obj;
  }

static int compare_multipart(const bg_db_object_t * obj, const void * data)
  {
  if((obj->type == BG_DB_OBJECT_MOVIE_MULTIPART) && !strcmp(obj->label, data))
    return 1;
  return 0;
  }


bg_db_object_t *
bg_db_movie_multipart_query(bg_db_t * db, const char * label,
                            const char * title)
  {
  int64_t ret;
  char * sql;
  
  /* Query cache */
  ret = bg_db_cache_search(db, compare_multipart, label);
  if(ret > 0)
    return bg_db_object_query(db, ret);

  /* Query db */
  ret = -1;
  sql = sqlite3_mprintf("SELECT ID FROM OBJECTS WHERE (TYPE = %d) & (LABEL = %Q);",
                        BG_DB_OBJECT_MOVIE_MULTIPART, label);
  bg_sqlite_exec(db->db, sql, bg_sqlite_int_callback, &ret);
  sqlite3_free(sql);

  if(ret > 0)
    return bg_db_object_query(db, ret);

  return NULL;
  }

