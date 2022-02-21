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

#include <mediadb_private.h>
#include <gmerlin/log.h>
#include <gmerlin/utils.h>
#include <string.h>

#define LOG_DOMAIN "db.rootfolder"

static void del_rootfolder(bg_db_t * db, bg_db_object_t * obj) // Delete from db
  {
  bg_sqlite_delete_by_id(db->db, "ROOTFOLDERS", obj->id);
  }

#define CHILD_TYPE_COL    1

static int query_rootfolder(bg_db_t * db, void * obj)
  {
  int result;
  int found = 0;
  bg_db_rootfolder_t * f = obj;

  sqlite3_stmt * st = db->q_rootfolders;

  sqlite3_bind_int64(st, 1, f->obj.id);
  
  if((result = sqlite3_step(st)) == SQLITE_ROW)
    {
    BG_DB_GET_COL_INT(CHILD_TYPE_COL, f->child_type);
    found = 1;
    }
  sqlite3_reset(st);
  sqlite3_clear_bindings(st);
  
  if(!found)
    return 0;
  return 1;
  }

const bg_db_object_class_t bg_db_rootfolder_class =
  {
  .name = "Root folder",
  .del =   del_rootfolder,
  .query = query_rootfolder,
  // .dump =  dump_rootfolder,
  .parent = NULL,
  };

static const struct
  {
  bg_db_object_type_t type;
  const char * label;
  int idx;
  }
labels[] =
  {
    {BG_DB_OBJECT_AUDIO_ALBUM, "Music albums", 1 },
    {BG_DB_OBJECT_AUDIO_FILE,  "Songs"       , 2 },
    {BG_DB_OBJECT_PLAYLIST,    "Playlists"   , 3 },
    {BG_DB_OBJECT_RADIO_URL,   "Webradio"    , 4 },
    {BG_DB_OBJECT_MOVIE,       "Movies"      , 5 },
    {BG_DB_OBJECT_TVSERIES,    "TV Shows"    , 6 },
    {BG_DB_OBJECT_PHOTO,       "Photos"      , 7 },
    {BG_DB_OBJECT_DIRECTORY,   "Directories" , 8 },
    { /* End */ }
  };
  
void * bg_db_rootfolder_get(bg_db_t * db, bg_db_object_type_t type)
  {
  int64_t id;
  int i;
  bg_db_object_t * ret;
  bg_db_rootfolder_t * rf;
  char * sql;
  
  switch(type)
    {
    /* Movies */
    case BG_DB_OBJECT_MOVIE:
    case BG_DB_OBJECT_MOVIE_MULTIPART:
      type = BG_DB_OBJECT_MOVIE;
      break;
      /* Albums */
    case BG_DB_OBJECT_AUDIO_ALBUM:
      /* Songs */
    case BG_DB_OBJECT_AUDIO_FILE:
    /* Playlists */
    case BG_DB_OBJECT_PLAYLIST:
    /* TV Shows */
    case BG_DB_OBJECT_TVSERIES:
    /* Webradio */
    case BG_DB_OBJECT_RADIO_URL:
    /* Photos (Untested) */
    case BG_DB_OBJECT_PHOTO:
    /* Directories */
    case BG_DB_OBJECT_DIRECTORY:
      break;
    default:
      return NULL;
    }

  id = bg_sqlite_id_to_id(db->db, "ROOTFOLDERS",
                          "ID", "TYPE", type);
  if(id >= 0)
    return bg_db_object_query(db, id);

  /* Create Folder */
  i = 0;
  while(labels[i].label)
    {
    if(labels[i].type == type)
      break;
    i++;
    }
  if(!labels[i].label)
    return NULL;
  
  ret = bg_db_object_create(db);
  bg_db_object_set_label(ret, labels[i].label);
  bg_db_object_set_type(ret, BG_DB_OBJECT_ROOTFOLDER);
  bg_db_object_set_parent_id(db, ret, 0);

  rf = (bg_db_rootfolder_t *)ret;
  rf->child_type = type;

  /* Add to db */
  sql = sqlite3_mprintf("INSERT INTO ROOTFOLDERS ( ID, TYPE, IX ) VALUES ( %"PRId64", %d, %d);",
                        bg_db_object_get_id(ret), rf->child_type, labels[i].idx);
  bg_sqlite_exec(db->db, sql, NULL, NULL);
  sqlite3_free(sql);
  
  return ret;
  }

