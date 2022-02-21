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
#include <gmerlin/xmlutils.h>

#include <string.h>

#define LOG_DOMAIN "db.videoinfo"

static void del_nfo(bg_db_t * db, bg_db_object_t * obj) // Delete from db
  {
  /* Get associated object and free it's info */
  bg_db_object_t * vi;
  char * sql;
  int result;
  int64_t id = -1;
  sql = sqlite3_mprintf("SELECT ID FROM VIDEO_INFOS WHERE NFO_FILE = %"PRId64";",
                        bg_db_object_get_id(obj));
  result = bg_sqlite_exec(db->db, sql, bg_sqlite_int_callback, &id);
  sqlite3_free(sql);

  if(!result || (id <= 0))
    return;

  vi = bg_db_object_query(db, id);
  if(!vi)
    return;  

  bg_db_video_info_clear(db, bg_db_object_get_video_info(vi), vi);
  bg_db_object_unref(vi);
  }

const bg_db_object_class_t bg_db_nfo_class =
  {
    .name = "NFO",
    .del = del_nfo,
    .parent = &bg_db_file_class,
  };


#define MY_FREE(ptr) if(ptr) free(ptr)
#define MY_FREE_ARR(arr) \
  if(arr)           \
    {               \
    i = 0;          \
    while(arr[i])   \
      {             \
      free(arr[i]); \
      i++;          \
      }             \
    free(arr);      \
    }           


void bg_db_video_info_free(bg_db_video_info_t* info)
  {
  int i;
  MY_FREE(info->title);
  MY_FREE(info->search_title);
  MY_FREE(info->title_orig);
  
  MY_FREE(info->rating);
  MY_FREE(info->parental_control);
  MY_FREE(info->director_ids);
  MY_FREE(info->actor_ids);
  MY_FREE(info->genre_ids);
  MY_FREE(info->country_ids);
  MY_FREE(info->audio_language_ids);
  MY_FREE(info->subtitle_language_ids);
  
  MY_FREE_ARR(info->directors);
  MY_FREE_ARR(info->actors);
  MY_FREE_ARR(info->genres);
  MY_FREE_ARR(info->countries);
  MY_FREE_ARR(info->audio_languages);
  MY_FREE_ARR(info->subtitle_languages);
  
  MY_FREE(info->plot);
  }

#define TITLE_COL            1
#define SEARCH_TITLE_COL     2
#define TITLE_ORIG_COL       3
#define RATING_COL           4
#define PARENTAL_CONTROL_COL 5
#define DATE_COL             6
#define PLOT_COL             7
#define POSTER_COL           8
#define FANART_COL           9
#define NFO_FILE_COL         10

static int64_t * copy_ids(bg_sqlite_id_tab_t * tab)
  {
  int64_t * ret = malloc((tab->num_val+1) * sizeof(*ret));
  memcpy(ret, tab->val, tab->num_val * sizeof(*ret));
  ret[tab->num_val] = -1;
  return ret;
  }

static char ** ids_2_strings(bg_db_t * db, bg_sqlite_id_tab_t * tab, bg_db_string_cache_t * c)
  {
  int i;
  char ** ret = calloc(tab->num_val+1, sizeof(*ret));
  for(i = 0; i < tab->num_val; i++)
    ret[i] = bg_db_string_cache_get(c, db->db, tab->val[i]);
  return ret;
  }

void bg_db_video_info_dump(bg_db_video_info_t * info)
  {

  }

void bg_db_video_info_init(bg_db_video_info_t * info)
  {
  bg_db_date_set_invalid(&info->date);
  }

void bg_db_video_info_query(bg_db_t * db, bg_db_video_info_t * ret,
                            bg_db_object_t * obj)
  {
  int result;
  int found = 0;
  char * sql;
  bg_sqlite_id_tab_t tab;
  sqlite3_stmt * st = db->q_video_infos;

  bg_sqlite_id_tab_init(&tab);
  
  sqlite3_bind_int64(st, 1, obj->id);

  if((result = sqlite3_step(st)) == SQLITE_ROW)
    {
    BG_DB_GET_COL_STRING(TITLE_COL, ret->title);
    BG_DB_GET_COL_STRING(SEARCH_TITLE_COL, ret->search_title);
    BG_DB_GET_COL_STRING(TITLE_ORIG_COL, ret->title_orig);
    BG_DB_GET_COL_STRING(RATING_COL, ret->rating);
    BG_DB_GET_COL_STRING(PARENTAL_CONTROL_COL, ret->parental_control);
    BG_DB_GET_COL_DATE(DATE_COL, ret->date);
    BG_DB_GET_COL_STRING(PLOT_COL, ret->plot);
    BG_DB_GET_COL_INT(POSTER_COL, ret->poster_id);
    BG_DB_GET_COL_INT(FANART_COL, ret->fanart_id);
    BG_DB_GET_COL_INT(NFO_FILE_COL, ret->nfo_file);
    found = 1;
    }

  sqlite3_reset(st);
  sqlite3_clear_bindings(st);
  
  if(!found)
    return;

  /* Directors */
  sql = sqlite3_mprintf("SELECT PERSON_ID FROM VIDEO_DIRECTORS_VIDEOS WHERE VIDEO_ID = %"PRId64" ORDER BY ID;",
                        bg_db_object_get_id(obj));
  result = bg_sqlite_exec(db->db, sql, bg_sqlite_append_id_callback, &tab);
  sqlite3_free(sql);

  if(result)
    {
    ret->director_ids = copy_ids(&tab);
    ret->directors = ids_2_strings(db, &tab, db->video_persons);
    }
  bg_sqlite_id_tab_reset(&tab);

  /* Actors */
  
  sql = sqlite3_mprintf("SELECT PERSON_ID FROM VIDEO_ACTORS_VIDEOS WHERE VIDEO_ID = %"PRId64" ORDER BY ID;",
                        bg_db_object_get_id(obj));
  result = bg_sqlite_exec(db->db, sql, bg_sqlite_append_id_callback, &tab);
  sqlite3_free(sql);

  if(result)
    {
    ret->actor_ids = copy_ids(&tab);
    ret->actors = ids_2_strings(db, &tab, db->video_persons);
    }
  bg_sqlite_id_tab_reset(&tab);

  /* Genres */
  sql = sqlite3_mprintf("SELECT GENRE_ID FROM VIDEO_GENRES_VIDEOS WHERE VIDEO_ID = %"PRId64" ORDER BY ID;",
                        bg_db_object_get_id(obj));
  result = bg_sqlite_exec(db->db, sql, bg_sqlite_append_id_callback, &tab);
  sqlite3_free(sql);

  if(result)
    {
    ret->genre_ids = copy_ids(&tab);
    ret->genres = ids_2_strings(db, &tab, db->video_genres);
    }
  bg_sqlite_id_tab_reset(&tab);

  /* Countries */
  sql = sqlite3_mprintf("SELECT COUNTRY_ID FROM VIDEO_COUNTRIES_VIDEOS WHERE VIDEO_ID = %"PRId64" ORDER BY ID;",
                        bg_db_object_get_id(obj));
  result = bg_sqlite_exec(db->db, sql, bg_sqlite_append_id_callback, &tab);
  sqlite3_free(sql);

  if(result)
    {
    ret->country_ids = copy_ids(&tab);
    ret->countries = ids_2_strings(db, &tab, db->video_countries);
    }
  bg_sqlite_id_tab_reset(&tab);

  /* Audio languages */
  sql = sqlite3_mprintf("SELECT LANGUAGE_ID FROM VIDEO_AUDIO_LANGUAGES_VIDEOS WHERE VIDEO_ID = %"PRId64" ORDER BY ID;",
                        bg_db_object_get_id(obj));
  result = bg_sqlite_exec(db->db, sql, bg_sqlite_append_id_callback, &tab);
  sqlite3_free(sql);

  if(result)
    {
    ret->audio_language_ids = copy_ids(&tab);
    ret->audio_languages = ids_2_strings(db, &tab, db->video_languages);
    }
  bg_sqlite_id_tab_reset(&tab);

  /* Subtitle languages */
  sql = sqlite3_mprintf("SELECT LANGUAGE_ID FROM VIDEO_SUBTITLE_LANGUAGES_VIDEOS WHERE VIDEO_ID = %"PRId64" ORDER BY ID;",
                        bg_db_object_get_id(obj));
  result = bg_sqlite_exec(db->db, sql, bg_sqlite_append_id_callback, &tab);
  sqlite3_free(sql);
  
  if(result)
    {
    ret->subtitle_language_ids = copy_ids(&tab);
    ret->subtitle_languages = ids_2_strings(db, &tab, db->video_languages);
    }
  
  bg_sqlite_id_tab_free(&tab);
  }

static void delete_arrays(bg_db_t * db, bg_db_object_t * obj)
  {
  char * sql;
  sql = sqlite3_mprintf("DELETE FROM VIDEO_DIRECTORS_VIDEOS WHERE VIDEO_ID = %"PRId64";", obj->id);
  bg_sqlite_exec(db->db, sql, NULL, NULL);
  sqlite3_free(sql);

  sql = sqlite3_mprintf("DELETE FROM VIDEO_ACTORS_VIDEOS WHERE VIDEO_ID = %"PRId64";", obj->id);
  bg_sqlite_exec(db->db, sql, NULL, NULL);
  sqlite3_free(sql);

  sql = sqlite3_mprintf("DELETE FROM VIDEO_GENRES_VIDEOS WHERE VIDEO_ID = %"PRId64";", obj->id);
  bg_sqlite_exec(db->db, sql, NULL, NULL);
  sqlite3_free(sql);
  
  sql = sqlite3_mprintf("DELETE FROM VIDEO_COUNTRIES_VIDEOS WHERE VIDEO_ID = %"PRId64";", obj->id);
  bg_sqlite_exec(db->db, sql, NULL, NULL);
  sqlite3_free(sql);

  sql = sqlite3_mprintf("DELETE FROM VIDEO_AUDIO_LANGUAGES_VIDEOS WHERE VIDEO_ID = %"PRId64";", obj->id);
  bg_sqlite_exec(db->db, sql, NULL, NULL);
  sqlite3_free(sql);

  sql = sqlite3_mprintf("DELETE FROM VIDEO_SUBTITLE_LANGUAGES_VIDEOS WHERE VIDEO_ID = %"PRId64";", obj->id);
  bg_sqlite_exec(db->db, sql, NULL, NULL);
  sqlite3_free(sql);
  }

static void add_arrays(bg_db_t * db, bg_db_video_info_t * info,
                       bg_db_object_t * obj)
  {
  int i;
  int64_t id;
  char * sql;
  int result;
  /* Add directors */
  if(info->director_ids)
    {
    i = 0;
    id = bg_sqlite_get_next_id(db->db, "VIDEO_DIRECTORS_VIDEOS");
    while(info->director_ids[i] > 0)
      {
      sql = sqlite3_mprintf("INSERT OR IGNORE INTO VIDEO_DIRECTORS_VIDEOS ("
                            "ID, VIDEO_ID, PERSON_ID"
                            ") VALUES ( "
                            "%"PRId64", %"PRId64", %"PRId64");",
                            id, bg_db_object_get_id(obj), info->director_ids[i]);

      result = bg_sqlite_exec(db->db, sql, NULL, NULL);
      sqlite3_free(sql);

      if(!result)
        return;
      i++;
      id++;
      }

    }

  if(info->actor_ids)
    {
    /* Add actors */
    i = 0;
    id = bg_sqlite_get_next_id(db->db, "VIDEO_ACTORS_VIDEOS");
    while(info->actor_ids[i] > 0)
      {
      sql = sqlite3_mprintf("INSERT OR IGNORE INTO VIDEO_ACTORS_VIDEOS ("
                            " ID, VIDEO_ID, PERSON_ID"
                            ") VALUES ( "
                            "%"PRId64", %"PRId64", %"PRId64");",
                            id, bg_db_object_get_id(obj),
                            info->actor_ids[i]);

      result = bg_sqlite_exec(db->db, sql, NULL, NULL);
      sqlite3_free(sql);

      if(!result)
        return;
      i++;
      id++;
      }
    }
  
  if(info->genre_ids)
    {
    /* Add genres */
    i = 0;
    id = bg_sqlite_get_next_id(db->db, "VIDEO_GENRES_VIDEOS");
    while(info->genre_ids[i] > 0)
      {
      sql = sqlite3_mprintf("INSERT OR IGNORE INTO VIDEO_GENRES_VIDEOS ( "
                            "ID, VIDEO_ID, GENRE_ID"
                            ") VALUES ( "
                            "%"PRId64", %"PRId64", %"PRId64");",
                            id, bg_db_object_get_id(obj),
                            info->genre_ids[i]);
      result = bg_sqlite_exec(db->db, sql, NULL, NULL);
      sqlite3_free(sql);

      if(!result)
        return;
      i++;
      id++;
      }
    }

  if(info->country_ids)
    {
    /* Add countries */
    i = 0;
    id = bg_sqlite_get_next_id(db->db, "VIDEO_COUNTRIES_VIDEOS");
    while(info->country_ids[i] > 0)
      {
      sql = sqlite3_mprintf("INSERT OR IGNORE INTO VIDEO_COUNTRIES_VIDEOS ( "
                            "ID, VIDEO_ID, COUNTRY_ID"
                            ") VALUES ( "
                            "%"PRId64", %"PRId64", %"PRId64");",
                            id, bg_db_object_get_id(obj), info->country_ids[i]);
      result = bg_sqlite_exec(db->db, sql, NULL, NULL);
      sqlite3_free(sql);
    
      if(!result)
        return;
      i++;
      id++;
      }
    }

  if(info->audio_language_ids)
    {
    /* Add audio languages */
    i = 0;
    id = bg_sqlite_get_next_id(db->db, "VIDEO_AUDIO_LANGUAGES_VIDEOS");
    while(info->audio_language_ids[i] > 0)
      {
      sql = sqlite3_mprintf("INSERT OR IGNORE INTO VIDEO_AUDIO_LANGUAGES_VIDEOS ( "
                            "ID, VIDEO_ID, LANGUAGE_ID"
                            ") VALUES ( "
                            "%"PRId64", %"PRId64", %"PRId64");",
                            id, bg_db_object_get_id(obj), info->audio_language_ids[i]);
      result = bg_sqlite_exec(db->db, sql, NULL, NULL);
      sqlite3_free(sql);
    
      if(!result)
        return;
      i++;
      id++;
      }
    }

  if(info->subtitle_language_ids)
    {
    /* Add aubtitle languages */
    i = 0;
    id = bg_sqlite_get_next_id(db->db, "VIDEO_SUBTITLE_LANGUAGES_VIDEOS");
    while(info->subtitle_language_ids[i] > 0)
      {
      sql = sqlite3_mprintf("INSERT OR IGNORE INTO VIDEO_SUBTITLE_LANGUAGES_VIDEOS ( "
                            "ID, VIDEO_ID, LANGUAGE_ID"
                            ") VALUES ( "
                            "%"PRId64", %"PRId64", %"PRId64");",
                            id, bg_db_object_get_id(obj), info->subtitle_language_ids[i]);
      result = bg_sqlite_exec(db->db, sql, NULL, NULL);
      sqlite3_free(sql);
    
      if(!result)
        return;
      i++;
      id++;
      }
    }

  }

void bg_db_video_info_delete(bg_db_t * db,
                             bg_db_object_t * obj)
  {
  bg_db_video_info_t * vi;
  bg_db_object_t * file;
  vi = bg_db_object_get_video_info(obj);
  if((vi->nfo_file > 0) && (file = bg_db_object_query(db, vi->nfo_file)))
    {
    bg_db_object_set_type(file, BG_DB_OBJECT_FILE);
    bg_db_object_unref(file);
    }
  if((vi->poster_id > 0) && (file = bg_db_object_query(db, vi->poster_id)))
    {
    bg_db_object_set_type(file, BG_DB_OBJECT_IMAGE_FILE);
    bg_db_object_unref(file);
    }
  if((vi->fanart_id > 0) && (file = bg_db_object_query(db, vi->fanart_id)))
    {
    bg_db_object_set_type(file, BG_DB_OBJECT_IMAGE_FILE);
    bg_db_object_unref(file);
    }

  bg_sqlite_delete_by_id(db->db, "VIDEO_INFOS", obj->id);
  delete_arrays(db, obj);

  }

void bg_db_video_info_clear(bg_db_t * db, bg_db_video_info_t * info,
                            void * obj)
  {
  int64_t fanart = info->fanart_id;
  int64_t poster = info->poster_id;
  char * title = info->title;
  char * search_title = info->search_title;

  /* Prevent these from being free()d */
  info->title = NULL;
  info->search_title = NULL;
  
  bg_db_video_info_free(info);
  memset(info, 0, sizeof(*info));

  info->fanart_id = fanart;
  info->poster_id = poster;
  info->title = title;
  info->search_title = search_title;

  delete_arrays(db, obj);  
  }

void bg_db_video_info_insert(bg_db_t * db, bg_db_video_info_t * info,
                             void * obj)
  {
  int result;
  char * sql;
  
  char date_string[BG_DB_DATE_STRING_LEN];
  
  sql = sqlite3_mprintf("INSERT INTO VIDEO_INFOS ( "
                        "ID, "
                        "TITLE, "
                        "SEARCH_TITLE, "
                        "TITLE_ORIG, "
                        "RATING, "
                        "PARENTAL_CONTROL, "
                        "DATE, "
                        "PLOT, "
                        "POSTER, "
                        "FANART, "
                        "NFO_FILE, "
                        "VIDEO_TYPE "
                        ") VALUES ( "
                        "%"PRId64", "
                        "%Q, "
                        "%Q, "
                        "%Q, "
                        "%Q, "
                        "%Q, "
                        "%Q, "
                        "%Q, "
                        "%"PRId64", "
                        "%"PRId64", "
                        "%"PRId64", "
                        "%d"
                        ");",
                        bg_db_object_get_id(obj),
                        info->title,
                        info->search_title,
                        info->title_orig,
                        info->rating,
                        info->parental_control,
                        bg_db_date_to_string(&info->date, date_string),
                        info->plot,
                        info->poster_id,
                        info->fanart_id,
                        info->nfo_file,
                        bg_db_object_get_type(obj)
                        );

  result = bg_sqlite_exec(db->db, sql, NULL, NULL);
  sqlite3_free(sql);

  add_arrays(db, info, obj);
  
  if(!result)
    return;

  }

void bg_db_video_info_update(bg_db_t * db, bg_db_video_info_t * info,
                             void * obj)
  {
  char * sql;
  char date_string[BG_DB_DATE_STRING_LEN];
  sql = sqlite3_mprintf("UPDATE VIDEO_INFOS SET "
                        "TITLE = %Q, "
                        "SEARCH_TITLE = %Q, "
                        "TITLE_ORIG = %Q, "
                        "RATING = %Q, "
                        "PARENTAL_CONTROL = %Q, "
                        "DATE = %Q, "
                        "PLOT = %Q,"
                        "POSTER = %"PRId64","
                        "FANART = %"PRId64", "
                        "NFO_FILE = %"PRId64" "
                        "WHERE ID = %"PRId64";",
                        info->title,
                        info->search_title,
                        info->title_orig,
                        info->rating,
                        info->parental_control,
                        bg_db_date_to_string(&info->date, date_string),
                        info->plot,
                        info->poster_id,
                        info->fanart_id,
                        info->nfo_file,
                        bg_db_object_get_id(obj));

  // fprintf(stderr, "%s\n", sql);
  
  bg_sqlite_exec(db->db, sql, NULL, NULL);
  sqlite3_free(sql);

  delete_arrays(db, obj);
  add_arrays(db, info, obj);
  }

/* */

bg_db_video_info_t * bg_db_object_get_video_info(void * obj)
  {
  switch(bg_db_object_get_type(obj))
    {
    case BG_DB_OBJECT_TVSERIES:
    case BG_DB_OBJECT_SEASON:
    case BG_DB_OBJECT_MOVIE_MULTIPART:
      {
      bg_db_video_container_t * c = obj;
      return &c->info;
      }
      break;
    case BG_DB_OBJECT_VIDEO_EPISODE:
    case BG_DB_OBJECT_MOVIE:
      {
      bg_db_movie_t * m = obj;
      return &m->info;
      }
      break;
    default:
      break;
    }
  return NULL;
  }

static char * normalize_genre(char * genre)
  {
  char * pos = genre;

  while(*pos != '\0')
    {
    if(*pos == '-')
      *pos = ' ';
    pos++;
    }
  return bg_capitalize(genre);
  }

/* Read a video info from an xml file */
int bg_db_video_info_load(bg_db_t * db, bg_db_video_info_t * info,
                          bg_db_object_t * obj, bg_db_file_t * nfo_file)
  {
  FILE * in = NULL;
  xmlDocPtr doc = NULL;
  xmlNodePtr root;
  xmlNodePtr node;
  int num_countries = 0;
  int num_actors    = 0;
  int num_directors = 0;
  int ret = 0;
  int i;
  int num = 0;
  
  in = fopen(nfo_file->path, "r");
  if(!in)
    goto fail;

  doc = bg_xml_load_FILE(in);
  if(!doc)
    goto fail;
  
  root = bg_xml_find_doc_child(doc, "movie");
  if(!root)
    root = bg_xml_find_doc_child(doc, "tvshow");

  if(!root)
    goto fail;
  
  node = root->children;

  while(node)
    {
    if(!node->name)
      {
      node = node->next;
      continue;
      }
    if(!BG_XML_STRCMP(node->name, "originaltitle"))
      info->title_orig = gavl_strdup(bg_xml_node_get_text_content(node));
    else if(!BG_XML_STRCMP(node->name, "title"))
      {
      bg_db_video_info_set_title(info, bg_xml_node_get_text_content(node));
      }
    else if(!BG_XML_STRCMP(node->name, "rating"))
      {
      char * pos;
      info->rating = gavl_strdup(bg_xml_node_get_text_content(node));
      if((pos = strchr(info->rating, ',')))
        *pos = '.';
      }
    else if(!BG_XML_STRCMP(node->name, "outline") || !BG_XML_STRCMP(node->name, "plot"))
      info->plot = gavl_strrep(info->plot, bg_xml_node_get_text_content(node));
    else if(!BG_XML_STRCMP(node->name, "mpaa"))
      info->parental_control = gavl_strdup(bg_xml_node_get_text_content(node));
    else if(!BG_XML_STRCMP(node->name, "year"))
      {
      info->date.year = atoi(bg_xml_node_get_text_content(node));
      info->date.month = 1;
      info->date.day   = 1;
      }
    else if(!BG_XML_STRCMP(node->name, "genre"))
      {
      char ** genres;

      genres = bg_strbreak(bg_xml_node_get_text_content(node), ',');

      if(genres)
        {
        num = 0;
        while(genres[num])
          num++;

        info->genres = calloc(num+1, sizeof(*genres));
        for(i = 0; i < num; i++)
          info->genres[i] = normalize_genre(genres[i]);
        bg_strbreak_free(genres);
        }

      }
    else if(!BG_XML_STRCMP(node->name, "director"))
      {
      info->directors = realloc(info->directors,
                               (num_directors+2)*sizeof(*info->directors));
      info->directors[num_directors] =
        bg_strip_space(gavl_strdup(bg_xml_node_get_text_content(node)));
      num_directors++;
      info->directors[num_directors] = NULL;
      }
    else if(!BG_XML_STRCMP(node->name, "actor"))
      {
      xmlNodePtr child = bg_xml_find_node_child(node, "name");
      if(child)
        {
        info->actors = realloc(info->actors,
                              (num_actors+2)*sizeof(*info->actors));
        info->actors[num_actors] =
          bg_strip_space(gavl_strdup(bg_xml_node_get_text_content(child)));
        num_actors++;
        info->actors[num_actors] = NULL;
        }
      }
    else if(!BG_XML_STRCMP(node->name, "country"))
      {
      info->countries = realloc(info->countries,
                               (num_countries+2)*sizeof(*info->countries));
      info->countries[num_countries] = gavl_strdup(bg_xml_node_get_text_content(node));
      num_countries++;
      info->countries[num_countries] = NULL;
      }

    node = node->next;
    }

  /* Get IDs */

  if(info->directors)
    {
    i = 0;
    num = 0;
    while(info->directors[num])
      num++;
    info->director_ids = malloc((num+1)*sizeof(*info->director_ids));

    for(i = 0; i < num; i++)
      info->director_ids[i] =
        bg_sqlite_string_to_id_add(db->db, "VIDEO_PERSONS",
                                   "ID", "NAME", info->directors[i]);
    info->director_ids[num] = -1;
    }

  if(info->actors)
    {
    i = 0;
    num = 0;
    while(info->actors[num])
      num++;

    info->actor_ids = malloc((num+1)*sizeof(*info->actor_ids));

    for(i = 0; i < num; i++)
      info->actor_ids[i] =
        bg_sqlite_string_to_id_add(db->db, "VIDEO_PERSONS",
                                   "ID", "NAME", info->actors[i]);
    info->actor_ids[num] = -1;
    }
  
  if(info->genres)
    {
    i = 0;
    num = 0;
    while(info->genres[num])
      num++;

    info->genre_ids = malloc((num+1)*sizeof(*info->genre_ids));

    for(i = 0; i < num; i++)
      info->genre_ids[i] =
        bg_sqlite_string_to_id_add(db->db, "VIDEO_GENRES",
                                   "ID", "NAME", info->genres[i]);

    info->genre_ids[num] = -1;
    }

  if(info->countries)
    {
    i = 0;
    num = 0;
    while(info->countries[num])
      num++;
    info->country_ids = malloc((num+1)*sizeof(*info->country_ids));

    for(i = 0; i < num; i++)
      info->country_ids[i] =
        bg_sqlite_string_to_id_add(db->db, "VIDEO_COUNTRIES",
                                   "ID", "NAME", info->countries[i]);
    info->country_ids[num] = -1;
    }
 
  info->nfo_file = bg_db_object_get_id(nfo_file);
  bg_db_create_vfolders(db, obj);
  
  ret = 1;
  fail:
  if(in)
    fclose(in);
  if(doc)
    xmlFreeDoc(doc);
  return ret;  
  }

void bg_db_video_info_set_title_nocpy(bg_db_video_info_t* info, char * title)
  {
  info->title = title;
  info->search_title = gavl_strdup(bg_db_get_search_string(info->title));
  }

void bg_db_video_info_set_title(bg_db_video_info_t* info, const char * title)
  {
  bg_db_video_info_set_title_nocpy(info, gavl_strdup(title));
  }


void bg_db_identify_nfo(bg_db_t * db, int64_t scan_dir_id)
  {
  int result;
  char * sql;
  int64_t mimetype;
  void * obj;
  int i;
  bg_db_file_t * f;
  bg_sqlite_id_tab_t tab;
  bg_sqlite_id_tab_t tab1;
  bg_sqlite_id_tab_init(&tab);
  bg_sqlite_id_tab_init(&tab1);

  mimetype = bg_sqlite_string_to_id(db->db, "MIMETYPES", "ID", "NAME", "text/x-nfo");

  sql = sqlite3_mprintf("select ID from OBJECTS where TYPE = %d;", BG_DB_OBJECT_FILE);
  result = bg_sqlite_exec(db->db, sql, bg_sqlite_append_id_callback, &tab);
  sqlite3_free(sql);

  if(!result)
    return;

  for(i = 0; i < tab.num_val; i++)
    {
    bg_sqlite_id_tab_reset(&tab1);

    f = bg_db_object_query(db, tab.val[i]);

    if((f->scan_dir_id != scan_dir_id) || (f->mimetype_id != mimetype))
      {
      bg_db_object_unref(f);
      continue;
      }

    sql = sqlite3_mprintf("select ID from OBJECTS where (LABEL = %Q) & "
                          "((TYPE = %d) | "
                          "(TYPE = %d) | "
                          "(TYPE = %d) | "
                          "(TYPE = %d) | "
                          "(TYPE = %d));",
                          f->obj.label, 
                          BG_DB_OBJECT_MOVIE, 
                          BG_DB_OBJECT_TVSERIES, 
                          BG_DB_OBJECT_SEASON, 
                          BG_DB_OBJECT_VIDEO_EPISODE,
                          BG_DB_OBJECT_MOVIE_MULTIPART);
 
    result = bg_sqlite_exec(db->db, sql, bg_sqlite_append_id_callback, &tab1);
    sqlite3_free(sql);
    if(!result)
      continue;

    if(tab1.num_val > 1)
      gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "nfo file %s matches for more than one object, taking only the first one", f->path);
    
    if(tab1.num_val > 0)
      {
      obj = bg_db_object_query(db, tab1.val[0]);
      if(bg_db_video_info_load(db, bg_db_object_get_video_info(obj), obj, f))
        {
        bg_db_object_set_type(f, BG_DB_OBJECT_VIDEO_NFO);
        gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Using %s as nfo", f->path);
        }
      bg_db_object_unref(obj);
      }

    
    bg_db_object_unref(f);
    }
  
  bg_sqlite_id_tab_free(&tab1);
  bg_sqlite_id_tab_free(&tab);

  }
