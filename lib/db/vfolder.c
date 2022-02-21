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

#include <mediadb_private.h>
#include <gmerlin/log.h>
#include <gmerlin/utils.h>
#include <string.h>

#define LOG_DOMAIN "db.vfolder"

typedef struct
  {
  const char * name;
  bg_db_object_type_t type;
  bg_db_category_t cats[BG_DB_VFOLDER_MAX_DEPTH + 1];
  } vfolder_type_t;

static const vfolder_type_t vfolder_types[] =
  {
#if 1
    {
      .type = BG_DB_OBJECT_AUDIO_ALBUM,
      .name = "Genre-Artist",
      .cats = { BG_DB_CAT_GENRE,
                BG_DB_CAT_ARTIST,
              },
    },
    {
      .type = BG_DB_OBJECT_AUDIO_ALBUM,
      .name = "Genre-Year",
      .cats = { BG_DB_CAT_GENRE,
                BG_DB_CAT_YEAR,
              },
    },
    {
      .type = BG_DB_OBJECT_AUDIO_ALBUM,
      .name = "Artist",
      .cats = { BG_DB_CAT_GROUP,
                BG_DB_CAT_ARTIST,
              },
    },
    {
      .type = BG_DB_OBJECT_AUDIO_FILE,
      .name = "Artist",
      .cats = { BG_DB_CAT_GROUP,
                BG_DB_CAT_ARTIST },
    },
    {
      .type = BG_DB_OBJECT_AUDIO_FILE,
      .name = "Genre",
      .cats = { BG_DB_CAT_GENRE,
                BG_DB_CAT_GROUP },
    },
    {
      .type = BG_DB_OBJECT_AUDIO_FILE,
      .name = "Genre-Artist",
      .cats = { BG_DB_CAT_GENRE,
                BG_DB_CAT_ARTIST },
    },
    {
      .type = BG_DB_OBJECT_AUDIO_FILE,
      .name = "Genre-Year",
      .cats = { BG_DB_CAT_GENRE,
                BG_DB_CAT_YEAR },
    },
#endif
    {
      .type = BG_DB_OBJECT_MOVIE,
      .name = "Genre",
      .cats = { BG_DB_CAT_GENRE },
    },
#if 1
    {
      .type = BG_DB_OBJECT_MOVIE,
      .name = "Actor",
      .cats = { BG_DB_CAT_GROUP,
                BG_DB_CAT_ACTOR },
    },
    {
      .type = BG_DB_OBJECT_MOVIE,
      .name = "Director",
      .cats = { BG_DB_CAT_GROUP, 
                BG_DB_CAT_DIRECTOR },
    },
    {
      .type = BG_DB_OBJECT_MOVIE,
      .name = "Year",
      .cats = { BG_DB_CAT_YEAR },
    },
    {
      .type = BG_DB_OBJECT_MOVIE,
      .name = "Country",
      .cats = { BG_DB_CAT_COUNTRY },
    },
    {
      .type = BG_DB_OBJECT_MOVIE,
      .name = "All",
      .cats = { },
    },
#endif
    {
      .type = BG_DB_OBJECT_TVSERIES,
      .name = "All",
      .cats = { },
    },
    {
      .type = BG_DB_OBJECT_TVSERIES,
      .name = "Genre",
      .cats = { BG_DB_CAT_GENRE },
    },
    { /* End */ }
  };

static int get_num_cats(const vfolder_type_t * t)
  {
  int ret = 0;
  while(t->cats[ret])
    ret++;
  return ret;
  }

static void dump_vfolder(void * obj)
  {
  int i;
  bg_db_vfolder_t * f = obj;

  gavl_diprintf(2, "Type:    %d\n", f->type);
  gavl_diprintf(2, "Depth:   %d\n", f->depth);
  for(i = 0; i < BG_DB_VFOLDER_MAX_DEPTH; i++)
    {
    if(!f->path[i].cat)
      break;
    gavl_diprintf(4, "Cat %d: %"PRId64"\n", f->path[i].cat, f->path[i].val);
    }
  }

static void del_vfolder(bg_db_t * db, bg_db_object_t * obj) // Delete from db
  {
  bg_sqlite_delete_by_id(db->db, "VFOLDERS", obj->id);
  }

static int vfolder_query_callback(void * data, int argc,
                                  char **argv, char **azColName)
  {
  int i;
  bg_db_vfolder_t * ret = data;
  
  for(i = 0; i < argc; i++)
    {
    BG_DB_SET_QUERY_INT("TYPE",      type);
    BG_DB_SET_QUERY_INT("DEPTH",     depth);

    if(!strncmp(azColName[i], "CAT_", 4))
      ret->path[atoi(azColName[i] + 4)-1].cat = atoi(argv[i]);
    if(!strncmp(azColName[i], "VAL_", 4))
      ret->path[atoi(azColName[i] + 4)-1].val = strtoll(argv[i], NULL, 10);
    }
  ret->obj.found = 1;
  return 0;
  }

static int query_vfolder(bg_db_t * db, void * obj)
  {
  char * sql;
  int result;
  bg_db_vfolder_t * f = obj;
  
  f->obj.found = 0;
  sql =
    sqlite3_mprintf("select * from VFOLDERS where ID = %"PRId64";",
                    bg_db_object_get_id(f));
  result = bg_sqlite_exec(db->db, sql, vfolder_query_callback, f);
  sqlite3_free(sql);
  if(!result || !f->obj.found)
    return 0;
  return 1;
  }

const bg_db_object_class_t bg_db_vfolder_class =
  {
  .name = "Virtual folder",
  .del = del_vfolder,
  .query = query_vfolder,
  .dump = &dump_vfolder,
  .parent = NULL,
  };

/* Utilities */

static int has_cat(bg_db_vfolder_t * f, bg_db_category_t cat)
  {
  int i = 0;
  for(i = 0; i < BG_DB_VFOLDER_MAX_DEPTH; i++)
    {
    if(!f->path[i].cat)
      return 0;
    if(f->path[i].cat == cat)
      return 1;
    }
  return 0;
  }

static int compare_vfolder_by_path(const bg_db_object_t * obj,
                                   const void * data)
  {
  const bg_db_vfolder_t * f1 = data;
  const bg_db_vfolder_t * f2;

  if((obj->type == BG_DB_OBJECT_VFOLDER) ||
     (obj->type == BG_DB_OBJECT_VFOLDER_LEAF))
    {
    f2 = (bg_db_vfolder_t*)obj;

    // fprintf(stderr, "Compare vfolder\n");
    // dump_vfolder(f1);
    // dump_vfolder(f2);
    
    if((f1->depth == f2->depth) &&
       (f1->type == f2->type) &&
       !memcmp(f1->path, f2->path,
               BG_DB_VFOLDER_MAX_DEPTH * sizeof(f1->path[0])))
      return 1;
    //    else
    //      fprintf(stderr, "Result: 0\n");
    }
  return 0;
  }

static void 
vfolder_set(bg_db_t * db,
            bg_db_vfolder_t * f, int type, int depth,
            bg_db_vfolder_path_t * path)
  {
  char * sql;
  int i;
  char tmp_string[256];
  f->type = type;
  f->depth = depth;
  memcpy(&f->path[0], path, BG_DB_VFOLDER_MAX_DEPTH * sizeof(path[0]));

  sql = gavl_strdup("INSERT INTO VFOLDERS ( ID, TYPE, DEPTH");
  
  for(i = 0; i < BG_DB_VFOLDER_MAX_DEPTH; i++)
    {
    snprintf(tmp_string, 256, ", CAT_%d, VAL_%d", i+1, i+1);
    sql = gavl_strcat(sql, tmp_string);
    }
  snprintf(tmp_string, 256, ") VALUES ( %"PRId64", %d, %d", 
           bg_db_object_get_id(f), f->type, f->depth);
  sql = gavl_strcat(sql, tmp_string);

  for(i = 0; i < BG_DB_VFOLDER_MAX_DEPTH; i++)
    {
    snprintf(tmp_string, 256, ", %d, %"PRId64"", f->path[i].cat, f->path[i].val);
    sql = gavl_strcat(sql, tmp_string);
    }
  sql = gavl_strcat(sql, ");");
  bg_sqlite_exec(db->db, sql, NULL, NULL);
  free(sql);

  if(bg_db_object_get_type(f) == BG_DB_OBJECT_VFOLDER_LEAF)
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Created vfolder leaf %s",
           bg_db_object_get_label(f));
  else
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Created vfolder %s",
           bg_db_object_get_label(f));
  
  //  fprintf(stderr, "Created vfolder\n");
  //  bg_db_object_dump(f);
  
  }


static int64_t
vfolder_by_path(bg_db_t * db, int type, int depth, 
                bg_db_vfolder_path_t * path)
  {
  int64_t id;
  char * sql;
  char tmp_string[64];
  int i;  

  bg_db_vfolder_t f;
  memset(&f, 0, sizeof(f));
  f.depth = depth;
  f.type = type;
  memcpy(f.path, path, BG_DB_VFOLDER_MAX_DEPTH * sizeof(path[0]));
	
  /* Look in cache */
  id = bg_db_cache_search(db, compare_vfolder_by_path, &f);
  if(id > 0)
    return id;

  /* Do a real lookup */
  id = -1;

  sql = bg_sprintf("SELECT ID FROM VFOLDERS WHERE (TYPE = %d) & (DEPTH = %d)", type, depth);
  for(i = 0; i < BG_DB_VFOLDER_MAX_DEPTH; i++)
    {
    snprintf(tmp_string, 64, "& (CAT_%d = %d) & (VAL_%d =  %"PRId64")",
             i+1, path[i].cat, i+1, path[i].val);
    sql = gavl_strcat(sql, tmp_string);
    }
  sql = gavl_strcat(sql, ";");
  bg_sqlite_exec(db->db, sql, bg_sqlite_int_callback, &id);
  free(sql);
  return id;  
  }

static void *
get_root_vfolder(bg_db_t * db, const vfolder_type_t * t)
  {
  bg_db_vfolder_t * parent;
  bg_db_vfolder_t * child;
  int64_t id;
  int type;
  int depth;
  int i;
  bg_db_vfolder_path_t path[BG_DB_VFOLDER_MAX_DEPTH];

  memset(path, 0, sizeof(path));
  depth = 0;
  type = t->type;
  
  /* New version */
  parent = bg_db_rootfolder_get(db, t->type);
  
  if(!t->cats[0])
    return parent;
  
  /* Folder for this specific path */
  
  i = 0;
  while(t->cats[i])
    {
    path[i].cat = t->cats[i];
    i++;
    }
  
  id = vfolder_by_path(db, type, depth, path);
  if(id < 0)
    {
    child = bg_db_object_create(db);
    if(!i)
      bg_db_object_set_type(child, BG_DB_OBJECT_VFOLDER_LEAF);
    else
      bg_db_object_set_type(child, BG_DB_OBJECT_VFOLDER);
    bg_db_object_set_label(child, t->name);
    bg_db_object_set_parent(db, child, parent);
    vfolder_set(db, child, type, depth, path);
    }
  else
    child = bg_db_object_query(db, id);

  bg_db_object_unref(parent);
  return child;
  }

static void * get_vfolder(bg_db_t * db,
                          void * parent, const char * label,
                          bg_db_object_type_t folder_type,
                          bg_db_object_type_t object_type,
                          bg_db_vfolder_path_t * path,
                          int depth)
  {
  int64_t id;
  bg_db_object_t * child;
  
  id = vfolder_by_path(db, object_type, depth, path);
  if(id > 0)
    return bg_db_object_query(db, id);

  child = bg_db_object_create(db);
  bg_db_object_set_label(child, label);
  bg_db_object_set_type(child, folder_type);
  vfolder_set(db, (bg_db_vfolder_t*)child, object_type, depth, path);
  bg_db_object_set_parent(db, child, parent);
  return child;
  }

/* Functions for vfolders */

typedef struct
  {
  bg_db_object_type_t type;
  void (*create_vfolders)(bg_db_t * db, const vfolder_type_t * t, void * obj);
  void (*get_children)(bg_db_t * db, bg_db_vfolder_t * vf,
                       bg_sqlite_id_tab_t * tab);
  
  } vfolder_funcs_t;


/* Audio file */

static void create_vfolders_audio_file(bg_db_t * db, const vfolder_type_t * t,
                                       void * obj)
  {
  int num_cats, i;
  bg_db_audio_file_t * f = obj;
  bg_db_object_t * folder;
  bg_db_object_t * parent;
  bg_db_vfolder_path_t path[BG_DB_VFOLDER_MAX_DEPTH];
  const char * label;
  bg_db_object_type_t folder_type = BG_DB_OBJECT_VFOLDER;
  
  if(t->type != BG_DB_OBJECT_AUDIO_FILE)
    return;

  if(!f->artist || !f->title)
    return;
  
  memset(path, 0, sizeof(path));
  num_cats = get_num_cats(t);
  parent = get_root_vfolder(db, t);

  for(i = 0; i < num_cats; i++)
    path[i].cat = t->cats[i];
    
  i = 0;
  
  while(i < num_cats)
    {
    if(i == num_cats - 1)
      folder_type = BG_DB_OBJECT_VFOLDER_LEAF;
    
    switch(t->cats[i])
      {
      case BG_DB_CAT_YEAR:
        {
        char year_str[16];
        path[i].val = f->date.year;
          
        if(f->date.year == 9999)
          label = "Unknown";
        else
          {
          sprintf(year_str, "%04d", f->date.year);
          label = year_str;
          }
        folder = get_vfolder(db, parent, label,
                             folder_type,
                             BG_DB_OBJECT_AUDIO_FILE,
                             path, i+1);
        }
        break;
      case BG_DB_CAT_ARTIST:
        path[i].val = f->artist_id;
        folder = get_vfolder(db, parent, f->artist,
                             folder_type,
                             BG_DB_OBJECT_AUDIO_FILE,
                             path, i+1);
        break;
      case BG_DB_CAT_GENRE:
        path[i].val = f->genre_id;
        folder = get_vfolder(db, parent, f->genre,
                             folder_type,
                             BG_DB_OBJECT_AUDIO_FILE,
                             path, i+1);
        break;
      case BG_DB_CAT_GROUP:
        if(i == num_cats - 1)
          {
          label = bg_db_get_group(f->search_title, &path[i].val);
          folder = get_vfolder(db, parent, label, folder_type,
                               BG_DB_OBJECT_AUDIO_FILE,
                               path, i+1);
          }
        else
          {
          switch(t->cats[i+1])
            {
            case BG_DB_CAT_ARTIST:
              label = bg_db_get_group(f->artist, &path[i].val);
              folder = get_vfolder(db, parent, label, folder_type,
                                   BG_DB_OBJECT_AUDIO_FILE,
                                   path, i+1);
              break;
            default:
              gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN,
                     "Unsupported category for audio file");
              break;
            }
          }
        break;
      default:
        gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Unsupported category for audio file");
        break;
      }
    i++;

    bg_db_object_unref(parent);
    parent = folder;
    }
  bg_db_object_add_child(db, parent, obj);
  bg_db_object_unref(parent);
  }

static void get_children_audio_file(bg_db_t * db, bg_db_vfolder_t * vf,
                                    bg_sqlite_id_tab_t * tab)
  {
  char * sql;
  char * cond;
  int i;
  int num_conditions = 0;
  sql = bg_sprintf("SELECT ID FROM AUDIO_FILES WHERE ");

  for(i = 0; i < vf->depth; i++)
    {
    cond = NULL;
    switch(vf->path[i].cat)
      {
      case BG_DB_CAT_YEAR:
        cond = bg_sprintf("(SUBSTR(DATE, 1, 4) = '%"PRId64"')",
                          vf->path[i].val);
        break;
      case BG_DB_CAT_GENRE:
        cond = bg_sprintf("(GENRE = %"PRId64")",
                          vf->path[i].val);
        break;
      case BG_DB_CAT_ARTIST:
        cond = bg_sprintf("(ARTIST = %"PRId64")",
                          vf->path[i].val);
        break;
      case BG_DB_CAT_GROUP:
        if(i == vf->depth - 1)
          cond = bg_db_get_group_condition("SEARCH_TITLE",
                                           vf->path[i].val);
        break;
      default:
        break;
      }

    if(cond)
      {
      if(num_conditions)
        sql = gavl_strcat(sql, "&");
      sql = gavl_strcat(sql, cond);
      num_conditions++;
      free(cond);
      }
    }
  
  sql = gavl_strcat(sql, " ORDER BY SEARCH_TITLE COLLATE NOCASE;");
  bg_sqlite_exec(db->db, sql, bg_sqlite_append_id_callback, tab);
  free(sql);
  }

/* Audio album  */

static void create_vfolders_audio_album(bg_db_t * db, const vfolder_type_t * t,
                                        void * obj)
  {
  int num_cats, i;
  bg_db_audio_album_t * f = obj;
  bg_db_object_t * folder;
  bg_db_object_t * parent;
  bg_db_vfolder_path_t path[BG_DB_VFOLDER_MAX_DEPTH];
  const char * label;
  bg_db_object_type_t folder_type = BG_DB_OBJECT_VFOLDER;

  if(t->type != BG_DB_OBJECT_AUDIO_ALBUM)
    return;

  memset(path, 0, sizeof(path));
  num_cats = get_num_cats(t);

  parent = get_root_vfolder(db, t);

  for(i = 0; i < num_cats; i++)
    path[i].cat = t->cats[i];

  i = 0;
  while(i < num_cats)
    {
    if(i == num_cats - 1)
      folder_type = BG_DB_OBJECT_VFOLDER_LEAF;

    switch(t->cats[i])
      {
      case BG_DB_CAT_YEAR:
        {
        char year_str[16];
        path[i].val = f->date.year;

        if(f->date.year == 9999)
          label = "Unknown";
        else
          {
          sprintf(year_str, "%04d", f->date.year);
          label = year_str;
          }
        folder = get_vfolder(db, parent, label,
                             folder_type,
                             BG_DB_OBJECT_AUDIO_ALBUM,
                             path, i+1);
        }
        break;
      case BG_DB_CAT_ARTIST:
        path[i].val = f->artist_id;
        folder = get_vfolder(db, parent, f->artist,
                             folder_type,
                             BG_DB_OBJECT_AUDIO_ALBUM,
                             path, i+1);
        break;
      case BG_DB_CAT_GENRE:
        path[i].val = f->genre_id;
        folder = get_vfolder(db, parent, f->genre,
                             folder_type,
                             BG_DB_OBJECT_AUDIO_ALBUM,
                             path, i+1);
        break;
      case BG_DB_CAT_GROUP:
        if(i == num_cats - 1)
          {
          label = bg_db_get_group(f->search_title, &path[i].val);
          folder = get_vfolder(db, parent, label, folder_type,
                               BG_DB_OBJECT_AUDIO_ALBUM,
                               path, i+1);
          }
        else
          {
          switch(t->cats[i+1])
            {
            case BG_DB_CAT_ARTIST:
              label = bg_db_get_group(f->artist, &path[i].val);
              folder = get_vfolder(db, parent, label, folder_type,
                                   BG_DB_OBJECT_AUDIO_ALBUM,
                                   path, i+1);
              break;
            default:
              gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN,
                     "Unsupported category for audio album");
              break;
            }
          }
        break;
      default:
        gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN,
               "Unsupported category for audio album");
        break;
      }
    i++;

    bg_db_object_unref(parent);
    parent = folder;
    }
  bg_db_object_add_child(db, parent, obj);
  bg_db_object_unref(parent);
  }

static void get_children_audio_album(bg_db_t * db, bg_db_vfolder_t * vf,
                                     bg_sqlite_id_tab_t * tab)
  {
  char * sql;
  char * cond;
  int i;
  int num_conditions = 0;
  sql = bg_sprintf("SELECT ID FROM AUDIO_ALBUMS WHERE ");

  for(i = 0; i < vf->depth; i++)
    {
    cond = NULL;
    switch(vf->path[i].cat)
      {
      case BG_DB_CAT_YEAR:
        cond = bg_sprintf("(SUBSTR(DATE, 1, 4) = '%"PRId64"')",
                          vf->path[i].val);
        break;
      case BG_DB_CAT_GENRE:
        cond = bg_sprintf("(GENRE = %"PRId64")",
                          vf->path[i].val);
        break;
      case BG_DB_CAT_ARTIST:
        cond = bg_sprintf("(ARTIST = %"PRId64")",
                          vf->path[i].val);
        break;
      case BG_DB_CAT_GROUP:
        if(i == vf->depth - 1)
          cond = bg_db_get_group_condition("SEARCH_TITLE",
                                           vf->path[i].val);
        break;
      default:
        break;
      }

    if(cond)
      {
      if(num_conditions)
        sql = gavl_strcat(sql, "&");
      sql = gavl_strcat(sql, cond);
      num_conditions++;
      free(cond);
      }
    }

  if(has_cat(vf, BG_DB_CAT_ARTIST))
    sql = gavl_strcat(sql, " ORDER BY DATE, SEARCH_TITLE COLLATE NOCASE");
  else
    sql = gavl_strcat(sql, " ORDER BY SEARCH_TITLE COLLATE NOCASE");
  
  bg_sqlite_exec(db->db, sql, bg_sqlite_append_id_callback, tab);
  free(sql);
  
  }

/* Movie */

static void create_vfolders_movie_internal(bg_db_t * db,
                                           const vfolder_type_t * t,
                                           void * obj, 
                                           int depth, bg_db_object_t * parent)
  {
  int num_cats, i, j;
  bg_db_video_info_t * info;
  bg_db_object_t * folder;
  const char * label;
  bg_db_vfolder_path_t path[BG_DB_VFOLDER_MAX_DEPTH];
  bg_db_object_type_t folder_type = BG_DB_OBJECT_VFOLDER;
  
  if(t->type != BG_DB_OBJECT_MOVIE)
    return;
  num_cats = get_num_cats(t);

  //  if(num_cats == 0)
  //    fprintf(stderr, "num_cats = 0\n");

  memset(path, 0, sizeof(path));

  info = bg_db_object_get_video_info(obj);
  
  for(i = 0; i < num_cats; i++)
    path[i].cat = t->cats[i];

  if(!parent)
    {
    parent = get_root_vfolder(db, t);
    i = 0;
    }
  else
    {
    bg_db_vfolder_t * vf = (bg_db_vfolder_t *)parent;
    
    for(i = 0; i < depth; i++)
      path[i].val = vf->path[i].val;

    /* Set i for later loop */
    i = depth;
    bg_db_object_ref(parent);
    } 

  if(!num_cats) // Movies/All needs special treatment
    {
    folder = get_vfolder(db, parent, t->name,
                         BG_DB_OBJECT_VFOLDER_LEAF,
                         BG_DB_OBJECT_MOVIE,
                         path, 0);
    bg_db_object_unref(parent);
    parent = folder;
    }
  
  while(i < num_cats)
    {
    if(i == num_cats - 1)
      folder_type = BG_DB_OBJECT_VFOLDER_LEAF;

    switch(t->cats[i])
      {
      case BG_DB_CAT_YEAR:
        {
        char year_str[16];
        path[i].val = info->date.year;

        if(info->date.year == 9999)
          label = "Unknown";
        else
          {
          sprintf(year_str, "%04d", info->date.year);
          label = year_str;
          }
        folder = get_vfolder(db, parent, label,
                             folder_type,
                             BG_DB_OBJECT_MOVIE,
                             path, i+1);
        }
        break;
      case BG_DB_CAT_GENRE:
        j = 0;

        if(!info->genres)
          {
          bg_db_object_unref(parent);
          return;
          }
        
        while(info->genres[j])
          {
          path[i].val = info->genre_ids[j];
          folder = get_vfolder(db, parent, info->genres[j],
                               folder_type,
                               BG_DB_OBJECT_MOVIE,
                               path, i+1);
          create_vfolders_movie_internal(db, t, obj, i+1, folder);
          bg_db_object_unref(folder);
          j++;
          }
        bg_db_object_unref(parent);
        return;
        break;
      case BG_DB_CAT_ACTOR:
        break;
      case BG_DB_CAT_DIRECTOR:
        break;
      case BG_DB_CAT_COUNTRY:
        j = 0;

        if(!info->countries)
          {
          bg_db_object_unref(parent);
          return;
          }

        while(info->countries[j])
          {
          path[i].val = info->country_ids[j];
          folder = get_vfolder(db, parent, info->countries[j],
                               folder_type,
                               BG_DB_OBJECT_MOVIE,
                               path, i+1);

          if(i == num_cats - 1) // Done here
            bg_db_object_add_child(db, folder, obj);
          else
            create_vfolders_movie_internal(db, t, obj, i+1, folder); // Subcategories
          bg_db_object_unref(folder);
          j++;
          }
        bg_db_object_unref(parent);
        return;
        break;
      case BG_DB_CAT_GROUP:
        if(i == num_cats - 1)
          {
          label = bg_db_get_group(info->search_title, &path[i].val);
          folder = get_vfolder(db, parent, label, folder_type,
                               BG_DB_OBJECT_MOVIE,
                               path, i+1);
          }
        else
          {
          bg_db_object_t * child;
          switch(t->cats[i+1])
            {
            case BG_DB_CAT_ACTOR:
              j = 0;

              if(!info->actors)
                {
                bg_db_object_unref(parent);
                return;
                }

              while(info->actors[j])
                {
                label = bg_db_get_group(info->actors[j], &path[i].val);
                path[i+1].val = 0;
                folder = get_vfolder(db, parent, label, folder_type,
                                     BG_DB_OBJECT_MOVIE,
                                     path, i+1);
                path[i+1].val = info->actor_ids[j];

                if(i == num_cats - 2)
                  {
                  child = get_vfolder(db, folder, info->actors[j],
                                      BG_DB_OBJECT_VFOLDER_LEAF,
                                      BG_DB_OBJECT_MOVIE,
                                      path, i+2);
                  bg_db_object_add_child(db, child, obj);
                  }
                else
                  {
                  child = get_vfolder(db, folder, info->actors[j],
                                      BG_DB_OBJECT_VFOLDER,
                                      BG_DB_OBJECT_MOVIE,
                                      path, i+2);
                  create_vfolders_movie_internal(db, t, obj, i+2, child); // Subcategories
                  }
                bg_db_object_unref(child);
                bg_db_object_unref(folder);
                j++;
                }
              bg_db_object_unref(parent);
              return;
              break;
            case BG_DB_CAT_DIRECTOR:

              if(!info->directors)
                {
                bg_db_object_unref(parent);
                return;
                }

              j = 0;
              while(info->directors[j])
                {
                label = bg_db_get_group(info->directors[j], &path[i].val);
                path[i+1].val = 0;
                folder = get_vfolder(db, parent, label, folder_type,
                                     BG_DB_OBJECT_MOVIE,
                                     path, i+1);
                path[i+1].val = info->director_ids[j];
                if(i == num_cats - 2)
                  {
                  child = get_vfolder(db, folder, info->directors[j],
                                      BG_DB_OBJECT_VFOLDER_LEAF,
                                      BG_DB_OBJECT_MOVIE,
                                      path, i+2);
                  bg_db_object_add_child(db, child, obj);
                  }
                else
                  {
                  child = get_vfolder(db, folder, info->directors[j],
                                      BG_DB_OBJECT_VFOLDER,
                                      BG_DB_OBJECT_MOVIE,
                                      path, i+2);
                  create_vfolders_movie_internal(db, t, obj, i+2, child); // Subcategories
                  }
                bg_db_object_unref(child);
                bg_db_object_unref(folder);
                j++;
                }
              bg_db_object_unref(parent);
              return;
              break;
            default:
              gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Unsupported category for movie");
            }
          }
        break;
      default:
        gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Unsupported category for movie");
        break;
      }
    
    i++;
    bg_db_object_unref(parent);
    parent = folder;
    }

  bg_db_object_add_child(db, parent, obj);
  bg_db_object_unref(parent);

  
  }

static void create_vfolders_movie(bg_db_t * db, const vfolder_type_t * t,
                                  void * obj)
  {
  create_vfolders_movie_internal(db, t, obj,  0, NULL);
  }

static void get_children_movie(bg_db_t * db, bg_db_vfolder_t * vf,
                               bg_sqlite_id_tab_t * tab)
  {
  char * sql;
  char * cond;
  int i;
  sql = bg_sprintf("SELECT ID FROM VIDEO_INFOS WHERE "
                   "((VIDEO_TYPE = %d) | (VIDEO_TYPE = %d))",
                   BG_DB_OBJECT_MOVIE, BG_DB_OBJECT_MOVIE_MULTIPART);
  
  for(i = 0; i < vf->depth; i++)
    {
    cond = NULL;
    switch(vf->path[i].cat)
      {
      case BG_DB_CAT_YEAR:
        cond = bg_sprintf("& (SUBSTR(DATE, 1, 4) = '%"PRId64"')",
                          vf->path[i].val);
        break;
      case BG_DB_CAT_GENRE:
        cond = bg_sprintf("& (ID IN (SELECT VIDEO_ID FROM VIDEO_GENRES_VIDEOS WHERE GENRE_ID = %"PRId64"))",
                          vf->path[i].val);
        break;
      case BG_DB_CAT_ACTOR:
        cond = bg_sprintf("& (ID IN (SELECT VIDEO_ID FROM VIDEO_ACTORS_VIDEOS WHERE PERSON_ID = %"PRId64"))",
                          vf->path[i].val);
        break;
      case BG_DB_CAT_DIRECTOR:
        cond = bg_sprintf("& (ID IN (SELECT VIDEO_ID FROM VIDEO_DIRECTORS_VIDEOS WHERE PERSON_ID = %"PRId64"))",
                          vf->path[i].val);
        break;
      case BG_DB_CAT_GROUP:
        if(i == vf->depth - 1)
          {
          sql = gavl_strcat(sql, "&");
          cond = bg_db_get_group_condition("SEARCH_TITLE",
                                           vf->path[i].val);
          }
        break;
      case BG_DB_CAT_COUNTRY:
        cond = bg_sprintf("& (ID IN (SELECT VIDEO_ID FROM VIDEO_COUNTRIES_VIDEOS WHERE COUNTRY_ID = %"PRId64"))",
                          vf->path[i].val);
        break;
        
      default:
        break;
      }

    if(cond)
      {
      sql = gavl_strcat(sql, cond);
      free(cond);
      }
    }

  if(has_cat(vf, BG_DB_CAT_DIRECTOR))
    sql = gavl_strcat(sql, " ORDER BY DATE, SEARCH_TITLE COLLATE NOCASE;");
  else
    sql = gavl_strcat(sql, " ORDER BY SEARCH_TITLE COLLATE NOCASE;");

  // fprintf(stderr, "SQL: %s\n", sql);

  bg_sqlite_exec(db->db, sql, bg_sqlite_append_id_callback, tab);
  free(sql);
  }

// TV Show

static void create_vfolders_tvseries_internal(bg_db_t * db,
                                           const vfolder_type_t * t,
                                           void * obj, 
                                           int depth, bg_db_object_t * parent)
  {
  int num_cats, i, j;
  bg_db_video_info_t * info;
  bg_db_object_t * folder;
  const char * label;
  bg_db_video_container_t * f;
  bg_db_vfolder_path_t path[BG_DB_VFOLDER_MAX_DEPTH];
  bg_db_object_type_t folder_type = BG_DB_OBJECT_VFOLDER;
  
  if(t->type != BG_DB_OBJECT_TVSERIES)
    return;
  num_cats = get_num_cats(t);
  memset(path, 0, sizeof(path));

  f = obj;
  info = &f->info;
  
  for(i = 0; i < num_cats; i++)
    path[i].cat = t->cats[i];

  if(!parent)
    {
    parent = get_root_vfolder(db, t);
    i = 0;
    }
  else
    {
    bg_db_vfolder_t * vf = (bg_db_vfolder_t *)parent;
    
    for(i = 0; i < depth; i++)
      path[i].val = vf->path[i].val;

    /* Set i for later loop */
    i = depth;
    bg_db_object_ref(parent);
    } 

  if(!num_cats) // TVSeries/All needs special treatment
    {
    folder = get_vfolder(db, parent, t->name,
                         BG_DB_OBJECT_VFOLDER_LEAF,
                         BG_DB_OBJECT_TVSERIES,
                         path, 0);
    bg_db_object_unref(parent);
    parent = folder;
    }

  
  while(i < num_cats)
    {
    if(i == num_cats - 1)
      folder_type = BG_DB_OBJECT_VFOLDER_LEAF;

    switch(t->cats[i])
      {
      case BG_DB_CAT_YEAR:
        {
        char year_str[16];
        path[i].val = info->date.year;

        if(info->date.year == 9999)
          label = "Unknown";
        else
          {
          sprintf(year_str, "%04d", info->date.year);
          label = year_str;
          }
        folder = get_vfolder(db, parent, label,
                             folder_type,
                             BG_DB_OBJECT_TVSERIES,
                             path, i+1);
        }
        break;
      case BG_DB_CAT_GENRE:
        j = 0;

        if(!info->genres)
          {
          bg_db_object_unref(parent);
          return;
          }
        
        while(info->genres[j])
          {
          path[i].val = info->genre_ids[j];
          folder = get_vfolder(db, parent, info->genres[j],
                               folder_type,
                               BG_DB_OBJECT_TVSERIES,
                               path, i+1);
          create_vfolders_tvseries_internal(db, t, obj, i+1, folder);
          bg_db_object_unref(folder);
          j++;
          }
        bg_db_object_unref(parent);
        return;
        break;
      case BG_DB_CAT_ACTOR:
        break;
      case BG_DB_CAT_DIRECTOR:
        break;
      case BG_DB_CAT_COUNTRY:
        j = 0;

        if(!info->countries)
          {
          bg_db_object_unref(parent);
          return;
          }

        while(info->countries[j])
          {
          path[i].val = info->country_ids[j];
          folder = get_vfolder(db, parent, info->countries[j],
                               folder_type,
                               BG_DB_OBJECT_TVSERIES,
                               path, i+1);

          if(i == num_cats - 1) // Done here
            bg_db_object_add_child(db, folder, obj);
          else
            create_vfolders_tvseries_internal(db, t, obj, i+1, folder); // Subcategories
          bg_db_object_unref(folder);
          j++;
          }
        bg_db_object_unref(parent);
        return;
        break;
      case BG_DB_CAT_GROUP:
        if(i == num_cats - 1)
          {
          label = bg_db_get_group(info->search_title, &path[i].val);
          folder = get_vfolder(db, parent, label, folder_type,
                               BG_DB_OBJECT_TVSERIES,
                               path, i+1);
          }
        else
          {
          bg_db_object_t * child;
          switch(t->cats[i+1])
            {
            case BG_DB_CAT_ACTOR:
              j = 0;

              if(!info->actors)
                {
                bg_db_object_unref(parent);
                return;
                }

              while(info->actors[j])
                {
                label = bg_db_get_group(info->actors[j], &path[i].val);
                path[i+1].val = 0;
                folder = get_vfolder(db, parent, label, folder_type,
                                     BG_DB_OBJECT_TVSERIES,
                                     path, i+1);
                path[i+1].val = info->actor_ids[j];

                if(i == num_cats - 2)
                  {
                  child = get_vfolder(db, folder, info->actors[j],
                                      BG_DB_OBJECT_VFOLDER_LEAF,
                                      BG_DB_OBJECT_TVSERIES,
                                      path, i+2);
                  bg_db_object_add_child(db, child, obj);
                  }
                else
                  {
                  child = get_vfolder(db, folder, info->actors[j],
                                      BG_DB_OBJECT_VFOLDER,
                                      BG_DB_OBJECT_TVSERIES,
                                      path, i+2);
                  create_vfolders_tvseries_internal(db, t, obj, i+2, child); // Subcategories
                  }
                bg_db_object_unref(child);
                bg_db_object_unref(folder);
                j++;
                }
              bg_db_object_unref(parent);
              return;
              break;
            case BG_DB_CAT_DIRECTOR:

              if(!info->directors)
                {
                bg_db_object_unref(parent);
                return;
                }

              j = 0;
              while(info->directors[j])
                {
                label = bg_db_get_group(info->directors[j], &path[i].val);
                path[i+1].val = 0;
                folder = get_vfolder(db, parent, label, folder_type,
                                     BG_DB_OBJECT_TVSERIES,
                                     path, i+1);
                path[i+1].val = info->director_ids[j];
                if(i == num_cats - 2)
                  {
                  child = get_vfolder(db, folder, info->directors[j],
                                      BG_DB_OBJECT_VFOLDER_LEAF,
                                      BG_DB_OBJECT_TVSERIES,
                                      path, i+2);
                  bg_db_object_add_child(db, child, obj);
                  }
                else
                  {
                  child = get_vfolder(db, folder, info->directors[j],
                                      BG_DB_OBJECT_VFOLDER,
                                      BG_DB_OBJECT_TVSERIES,
                                      path, i+2);
                  create_vfolders_tvseries_internal(db, t, obj, i+2, child); // Subcategories
                  }
                bg_db_object_unref(child);
                bg_db_object_unref(folder);
                j++;
                }
              bg_db_object_unref(parent);
              return;
              break;
            default:
              gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Unsupported category for tvseries");
            }
          }
        break;
      default:
        gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Unsupported category for tvseries");
        break;
      }
    
    i++;
    bg_db_object_unref(parent);
    parent = folder;
    }

  bg_db_object_add_child(db, parent, obj);
  bg_db_object_unref(parent);

  
  }

static void create_vfolders_tvseries(bg_db_t * db, const vfolder_type_t * t,
                                  void * obj)
  {
  create_vfolders_tvseries_internal(db, t, obj,  0, NULL);
  }

static void get_children_tvseries(bg_db_t * db, bg_db_vfolder_t * vf,
                                  bg_sqlite_id_tab_t * tab)
  {
  char * sql;
  char * cond;
  int i;
  sql = bg_sprintf("SELECT ID FROM VIDEO_INFOS WHERE "
                   "(VIDEO_TYPE = %d)", BG_DB_OBJECT_TVSERIES);
  
  for(i = 0; i < vf->depth; i++)
    {
    cond = NULL;
    switch(vf->path[i].cat)
      {
      case BG_DB_CAT_GENRE:
        cond = bg_sprintf("& (ID IN (SELECT VIDEO_ID FROM VIDEO_GENRES_VIDEOS WHERE GENRE_ID = %"PRId64"))",
                          vf->path[i].val);
        break;
      case BG_DB_CAT_GROUP:
        if(i == vf->depth - 1)
          {
          sql = gavl_strcat(sql, "&");
          cond = bg_db_get_group_condition("SEARCH_TITLE",
                                           vf->path[i].val);
          }
        break;
      default:
        break;
      }

    if(cond)
      {
      sql = gavl_strcat(sql, cond);
      free(cond);
      }
    }

  sql = gavl_strcat(sql, " ORDER BY SEARCH_TITLE COLLATE NOCASE;");
  
  // fprintf(stderr, "SQL: %s\n", sql);

  bg_sqlite_exec(db->db, sql, bg_sqlite_append_id_callback, tab);
  free(sql);
  }

/* End */

static vfolder_funcs_t funcs[] =
  {
    {
      .type = BG_DB_OBJECT_AUDIO_FILE,
      .create_vfolders = create_vfolders_audio_file,
      .get_children = get_children_audio_file,
    },
    {
      .type = BG_DB_OBJECT_AUDIO_ALBUM,
      .create_vfolders = create_vfolders_audio_album,
      .get_children = get_children_audio_album,
    },
    {
      .type = BG_DB_OBJECT_MOVIE,
      .create_vfolders = create_vfolders_movie,
      .get_children = get_children_movie,
    },
    {
      .type = BG_DB_OBJECT_MOVIE_MULTIPART,
      .create_vfolders = create_vfolders_movie,
      .get_children = get_children_movie,
    },
    {
      .type = BG_DB_OBJECT_TVSERIES,
      .create_vfolders = create_vfolders_tvseries,
      .get_children = get_children_tvseries,
    },
    { /* End */ }
    
  };

static vfolder_funcs_t * get_funcs(bg_db_object_type_t t)
  {
  int i = 0;
  while(funcs[i].create_vfolders)
    {
    if(funcs[i].type == t)
      return &funcs[i];
    i++;
    }
  return NULL;
  }

/*
 *  Query children of a vfolder leaf
 */

/* We assume that columns for the same category are names identically
   in different tables */

static void get_children_vfolder_leaf(bg_db_t * db, void * obj,
                                      bg_sqlite_id_tab_t * tab)
  {
  bg_db_vfolder_t * vf = obj;
  vfolder_funcs_t * funcs = get_funcs(vf->type);
  if(funcs)
    funcs->get_children(db, vf, tab);
  }


const bg_db_object_class_t bg_db_vfolder_leaf_class =
  {
  .name = "Virtual folder (leaf)",
  .del =   del_vfolder,
  .query = query_vfolder,
  .dump =  dump_vfolder,
  .get_children = get_children_vfolder_leaf,
  .parent = NULL,
  };

void
bg_db_create_vfolders(bg_db_t * db, void * obj)
  {
  int i = 0;
  vfolder_funcs_t * funcs = get_funcs(bg_db_object_get_type(obj));
  
  if(!funcs)
    return;
    
  while(vfolder_types[i].type)
    {
    funcs->create_vfolders(db, &vfolder_types[i], obj);
    i++;
    }

  }

void
bg_db_cleanup_vfolders(bg_db_t * db)
  {
  int i;
  char * sql = NULL;
  int result;
  bg_db_object_t * vfolder;
  bg_sqlite_id_tab_t vfolders_tab;
  bg_sqlite_id_tab_t children_tab;

  bg_sqlite_id_tab_init(&children_tab);
  bg_sqlite_id_tab_init(&vfolders_tab);
  
  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Cleaning up vfolders");

  sql = sqlite3_mprintf("select ID from OBJECTS where TYPE = %d;", BG_DB_OBJECT_VFOLDER_LEAF);
  result = bg_sqlite_exec(db->db, sql, bg_sqlite_append_id_callback, &vfolders_tab);
  sqlite3_free(sql);

  if(!result)
    goto end;

  for(i = 0; i < vfolders_tab.num_val; i++)
    {
    vfolder = bg_db_object_query(db, vfolders_tab.val[i]);
    get_children_vfolder_leaf(db, vfolder, &children_tab);

    if(children_tab.num_val)
      {
      vfolder->children = children_tab.num_val;
      bg_db_object_unref(vfolder);
      }
    else
      {
      gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Deleting empty vfolder %s", vfolder->label);
      bg_db_object_delete(db, vfolder);
      // bg_db_object_unref(vfolder);
      }
    bg_sqlite_id_tab_reset(&children_tab);
    }
  
  end:
  bg_sqlite_id_tab_free(&children_tab);
  bg_sqlite_id_tab_free(&vfolders_tab);
  }

void
bg_db_create_tables_vfolders(bg_db_t * db)
  {
  int i;
  char * str = gavl_strdup("CREATE TABLE VFOLDERS ( ID INTEGER PRIMARY KEY,"
                           "TYPE INTEGER, "
                           "DEPTH INTEGER" );
  char tmp_string[32];
  
  for(i = 0; i < BG_DB_VFOLDER_MAX_DEPTH; i++)
    {
    snprintf(tmp_string, 32, ", CAT_%d INTEGER", i+1);
    str = gavl_strcat(str, tmp_string);
    snprintf(tmp_string, 32, ", VAL_%d INTEGER", i+1);
    str = gavl_strcat(str, tmp_string);
    }

  str = gavl_strcat(str, ");");
  bg_sqlite_exec(db->db, str, NULL, NULL);
  free(str);
  }

