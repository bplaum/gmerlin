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


#include <unistd.h>
#include <string.h>

#include <gmerlin/mdb.h>
#include <gmerlin/utils.h>

#include <gmerlin/translation.h>
#include <gmerlin/log.h>

#define LOG_DOMAIN "mdb.streams"

// #include <gmerlin/xmlutils.h>

#include <gmerlin/bggavl.h>

#include <mdb_private.h>
#include <gavl/metatags.h>
#include <gavl/utils.h>

/*
 * /streams/
 *   /1
 *     /by-tag
 *       /j
 *         /jazz
 *           Blabla
 *     /by-country
 *       /de
 *         BlaBla
 *     /by-language
 *       /ger
 *         BlaBla
 *
 *   If a source has just one category to choose from, the order is:
 *   
 * /streams/
 *   /2-by-tag
 *     /j
 *       /jazz
 *         Blabla
 */

/*
 * Icecast streams:
 * http://dir.xiph.org/yp.xml
 *
 * Radio-Browser:
 * https://de1.api.radio-browser.info/
 *
 */

#define META_DB_ID       "DBID"

#define STREAM_TYPE_UNKOWN 0
#define STREAM_TYPE_RADIO  1
#define STREAM_TYPE_TV     2

#define SOURCE_TYPE_RADIOBROWSER 0
#define SOURCE_TYPE_ICECAST      1
#define SOURCE_TYPE_M3U          2

/* Containers with more than this will be split into alphabetical pages */
#define GROUP_THRESHOLD 300

#define BROWSE_NONE        0
#define BROWSE_ALL         1
#define BROWSE_BY_TAG      2
#define BROWSE_BY_COUNTRY  3
#define BROWSE_BY_LANGUAGE 4
#define BROWSE_BY_CATEGORY 5


/*
  tables:

  sources( ID, LABEL, NUM_LANGUAGES, NUM_TAGS, NUM_COUNTRIES, NUM_CATEGORIES)
  
  stations( DBID, LABEL, LOGO_URI, HOMEPAGE_URI, STREAM_URI, TYPE, SOURCE_ID )

  categories( ID, NAME )
  station_categories( ID, DBID, CATEGORY_ID )

  tags( ID, NAME )
  station_tags( ID, DBID, TAG_ID )

  countries( ID, NAME, CODE )
  station_countries( ID, DBID, COUNTRY_ID )

  languages( ID, NAME, CODE )
  station_languages( ID, DBID, LANGUAGE_ID )
  
*/

typedef struct
  {
  int64_t id;
  const char * name;
  const char * stream_uri;
  const char * station_uri; // Homepage
  const char * logo_uri;
  int type;
  int64_t source_id;

  gavl_array_t categories;
  gavl_array_t tags;
  gavl_array_t countries; // Labels
  gavl_array_t languages; // Labels
  
  } station_t;


typedef struct
  {
  sqlite3 * db;

  int64_t num_stations;
  int64_t num_sources;

  gavl_dictionary_t * root_container;

  gavl_array_t sources;
  
  } streams_t;

static struct
  {
  int mode;
  const char * table;
  }
browse_tables[] =
  {
    { BROWSE_BY_TAG,      "tags"     },
    { BROWSE_BY_LANGUAGE, "languages" },
    { BROWSE_BY_CATEGORY, "categories" },
    { BROWSE_BY_COUNTRY,   "countries" },
    { /* End */ },
  };

static void init_sources(bg_mdb_backend_t * b);


static const char * get_browse_table(int browse_mode)
  {
  int i = 0;
  while(browse_tables[i].table)
    {
    if(browse_tables[i].mode == browse_mode)
      return browse_tables[i].table;
    i++;
    }
  return NULL;
  }


  
static const gavl_dictionary_t * get_source_by_id(streams_t * p, int64_t id)
  {
  int i;

  const gavl_dictionary_t * ret;
  const gavl_dictionary_t * mdb;
  int64_t test_id;
  
  for(i = 0; i < p->sources.num_entries; i++)
    {
    if((ret = gavl_value_get_dictionary(&p->sources.entries[i])) &&
       (mdb = gavl_dictionary_get_dictionary(ret, BG_MDB_DICT)) &&
       gavl_dictionary_get_long(mdb, META_DB_ID, &test_id) &&
       (test_id == id))
      return ret;
    }
  return NULL;
  }

static int64_t get_source_count(streams_t * p, int64_t id, const char * var)
  {
  int i;
  const gavl_dictionary_t * dict;
  int64_t ret;
  int64_t test_id;
  
  for(i = 0; i < p->sources.num_entries; i++)
    {
    if((dict = gavl_value_get_dictionary(&p->sources.entries[i])) &&
       (dict = gavl_dictionary_get_dictionary(dict, BG_MDB_DICT)) &&
       gavl_dictionary_get_long(dict, META_DB_ID, &test_id) &&
       (test_id == id))
      {
      if(gavl_dictionary_get_long(dict, var, &ret))
        return ret;
      else
        return -1;
      }
    }
  
  return -1;
  }

// static void import_icecast(bg_mdb_backend_t * b);
static int import_radiobrowser(bg_mdb_backend_t * b);
static void import_m3u(bg_mdb_backend_t * b);

static void station_init(station_t * s)
  {
  memset(s, 0, sizeof(*s));
  }

static void station_reset(station_t * s)
  {
  gavl_array_free(&s->categories);
  gavl_array_free(&s->tags);
  gavl_array_free(&s->countries);
  gavl_array_free(&s->languages);
  station_init(s);
  }


typedef struct
  {
  gavl_dictionary_t * dict;
  gavl_dictionary_t * m;
  } browse_station_t;

static int
browse_station_callback(void * data, int argc, char **argv, char **azColName)
  {
  int i;
  browse_station_t * b = data;

  for(i = 0; i < argc; i++)
    {
    if(!strcmp(azColName[i], META_DB_ID))
      {
      
      }
    else if(!strcmp(azColName[i], GAVL_META_LABEL))
      {
      gavl_dictionary_set_string(b->m, GAVL_META_LABEL, argv[i]);
      }
    else if(!strcmp(azColName[i], GAVL_META_URI))
      {
      gavl_metadata_add_src(b->m, GAVL_META_SRC, NULL, argv[i]);
      }
    else if(!strcmp(azColName[i], GAVL_META_STATION_URL))
      {
      gavl_dictionary_set_string(b->m, GAVL_META_STATION_URL, argv[i]);
      }
    else if(!strcmp(azColName[i], GAVL_META_LOGO_URL))
      {
      gavl_dictionary_set_string(b->m, GAVL_META_LOGO_URL, argv[i]);
      }
    else if(!strcmp(azColName[i], "TYPE"))
      {
      int type = atoi(argv[i]);
      if(type == STREAM_TYPE_RADIO)
        gavl_dictionary_set_string(b->m, GAVL_META_MEDIA_CLASS,
                                   GAVL_META_MEDIA_CLASS_AUDIO_BROADCAST);
      else if(type == STREAM_TYPE_TV)
        gavl_dictionary_set_string(b->m, GAVL_META_MEDIA_CLASS,
                                   GAVL_META_MEDIA_CLASS_VIDEO_BROADCAST);
      else
        gavl_dictionary_set_string(b->m, GAVL_META_MEDIA_CLASS,
                                   GAVL_META_MEDIA_CLASS_LOCATION);
      }
    }
  
  return 0;
  }


static int
browse_array_callback(void * data, int argc, char **argv, char **azColName)
  {
  gavl_string_array_add(data, argv[0]);
  return 0;
  }

static void browse_array(bg_mdb_backend_t * b,
                         gavl_dictionary_t * metadata, const char * table_name, const char * dict_key, int64_t id)
  {
  char * sql;
  gavl_array_t * arr;
  streams_t * p = b->priv;
  
  arr = gavl_dictionary_get_array_create(metadata, dict_key);
  
  sql = gavl_sprintf("select NAME from station_%s inner join %s on ATTR_ID = %s.ID where STATION_ID = %"PRId64";",
                     table_name, table_name, table_name, id);
  bg_sqlite_exec(p->db, sql, browse_array_callback, arr);
  free(sql);
  
  if(!arr->num_entries)
    gavl_dictionary_set(metadata, dict_key, NULL);
  
  }

static int browse_station(bg_mdb_backend_t * b,
                           gavl_dictionary_t * ret, int64_t id)
  {
  int result;
  char * sql;
  browse_station_t bs;
  streams_t * p = b->priv;

  bs.dict = ret;
  bs.m = gavl_dictionary_get_dictionary_create(ret, GAVL_META_METADATA);
  
  sql = gavl_sprintf("select * from stations where "META_DB_ID" = %"PRId64";", id);
  result = bg_sqlite_exec(p->db, sql, browse_station_callback, &bs);
  
  free(sql);

  if(!result)
    return 0;
  
  browse_array(b, bs.m, "tags", GAVL_META_TAG, id); 
  browse_array(b, bs.m, "countries", GAVL_META_COUNTRY, id); 
  browse_array(b, bs.m, "languages", GAVL_META_AUDIO_LANGUAGES, id); 
  browse_array(b, bs.m, "categories", GAVL_META_CATEGORY, id); 

  return 1;

  }

static void add_array(bg_mdb_backend_t * b, int64_t station_id, int64_t source_id,
                      const gavl_array_t * arr, const char * attr_table, const char * arr_table)
  {
  int i;
  char * sql;
  const char * attr;
  int64_t attr_id;
  int result;
  
  streams_t * p = b->priv;
  
  for(i = 0; i < arr->num_entries; i++)
    {
    attr = gavl_string_array_get(arr, i);

    attr_id = bg_sqlite_string_to_id_add(p->db, attr_table, "ID", "NAME", attr);

    sql = sqlite3_mprintf("INSERT INTO %s (ATTR_ID, STATION_ID, SOURCE_ID) "
                          "VALUES"
                          " (%"PRId64", %"PRId64", %"PRId64"); ",
                          arr_table, attr_id, station_id, source_id);

    result = bg_sqlite_exec(p->db, sql, NULL, NULL);
    sqlite3_free(sql);
    if(!result)
      return;
    }
  }

static const char * search_string_skip_chars = "+-~_ .,;\"'[({*@#:";

static const char * get_search_name(const char * name)
  {
  const char * name_orig = name;

  while(strchr(search_string_skip_chars, *name))
    name++;

  if(*name == '\0')
    return name_orig;
  else
    return name;
  }

static void add_station(bg_mdb_backend_t * b, station_t * s)
  {
  char * sql;
  int result;
  char * name = gavl_strdup(s->name);
  streams_t * p = b->priv;

  name = gavl_strip_space(name);
  
  s->id = ++p->num_stations;


  
  if(strchr(s->name, '\n'))
    {
    }
  
  sql = sqlite3_mprintf("INSERT INTO stations ("META_DB_ID", "GAVL_META_LABEL", "GAVL_META_SEARCH_TITLE", "GAVL_META_URI", "GAVL_META_STATION_URL", "GAVL_META_LOGO_URL", TYPE, SOURCE_ID) "
                        "VALUES"
                        " (%"PRId64", %Q, %Q, %Q, %Q, %Q, %d, %"PRId64"); ",
                        s->id, name, get_search_name(name), s->stream_uri, s->station_uri, s->logo_uri, s->type, s->source_id);

  free(name);
  
  result = bg_sqlite_exec(p->db, sql, NULL, NULL);
  sqlite3_free(sql);
  if(!result)
    return;

  add_array(b, s->id, s->source_id, &s->categories, "categories", "station_categories");
  add_array(b, s->id, s->source_id, &s->languages, "languages", "station_languages");
  add_array(b, s->id, s->source_id, &s->countries, "countries", "station_countries");
  add_array(b, s->id, s->source_id, &s->tags, "tags", "station_tags");
  
  }

static void add_source(bg_mdb_backend_t * b, const char * label, int64_t * source_id)
  {
  char * sql;
  int result;

  streams_t * p = b->priv;
  *source_id = ++p->num_sources;

  sql = sqlite3_mprintf("INSERT INTO sources ("META_DB_ID", "GAVL_META_LABEL") "
                        "VALUES"
                        " (%"PRId64", %Q);",
                        *source_id, label);
  
  result = bg_sqlite_exec(p->db, sql, NULL, NULL);
  sqlite3_free(sql);
  if(!result)
    return;
  
  
  }

static void destroy_streams(bg_mdb_backend_t * b)
  {
  streams_t * p;
  p = b->priv;
  sqlite3_close(p->db);
  gavl_array_free(&p->sources);
  
  free(p);
  }

static void rescan(bg_mdb_backend_t * be)
  {
  int64_t num_languages;
  int64_t num_countries;
  int64_t num_tags;
  int64_t num_categories;
  int64_t num_stations;
  char * sql;
  int i;
  bg_sqlite_id_tab_t tab;
  gavl_msg_t * evt;
  streams_t * p = be->priv;
    
  bg_mdb_track_lock(be, 1, p->root_container);
  
  p->num_stations = 0;
  p->num_sources = 0;
  
  bg_sqlite_start_transaction(p->db);
  
  /* We re-build the whole database */

  bg_sqlite_exec(p->db, "DELETE FROM sources", NULL, NULL);
  
  bg_sqlite_exec(p->db, "DELETE FROM stations", NULL, NULL);
  bg_sqlite_exec(p->db, "DELETE FROM categories", NULL, NULL);
  bg_sqlite_exec(p->db, "DELETE FROM station_categories", NULL, NULL);

  bg_sqlite_exec(p->db, "DELETE FROM languages", NULL, NULL);
  bg_sqlite_exec(p->db, "DELETE FROM station_languages", NULL, NULL);

  bg_sqlite_exec(p->db, "DELETE FROM countries", NULL, NULL);
  bg_sqlite_exec(p->db, "DELETE FROM station_countries", NULL, NULL);

  bg_sqlite_exec(p->db, "DELETE FROM tags", NULL, NULL);
  bg_sqlite_exec(p->db, "DELETE FROM station_tags", NULL, NULL);

  //  import_icecast(be);
  import_radiobrowser(be);
  import_m3u(be);

  /* Get summary */
  bg_sqlite_id_tab_init(&tab);

  bg_sqlite_exec(p->db,                              /* An open database */
                 "SELECT "META_DB_ID" from sources;",                           /* SQL to be evaluated */
                 bg_sqlite_append_id_callback,  /* Callback function */
                 &tab);                               /* 1st argument to callback */

  fprintf(stderr, "Got %d sources\n", tab.num_val);
  
  for(i = 0; i < tab.num_val; i++)
    {
    sql = sqlite3_mprintf("select count(distinct attr_id) from station_countries where SOURCE_ID = %"PRId64";", tab.val[i]);
    num_countries = bg_sqlite_get_int(p->db, sql);
    sqlite3_free(sql);

    sql = sqlite3_mprintf("select count(distinct attr_id) from station_languages where SOURCE_ID = %"PRId64";", tab.val[i]);
    num_languages = bg_sqlite_get_int(p->db, sql);
    sqlite3_free(sql);

    sql = sqlite3_mprintf("select count(distinct attr_id) from station_categories where SOURCE_ID = %"PRId64";", tab.val[i]);
    num_categories = bg_sqlite_get_int(p->db, sql);
    sqlite3_free(sql);

    sql = sqlite3_mprintf("select count(distinct attr_id) from station_tags where SOURCE_ID = %"PRId64";", tab.val[i]);
    num_tags = bg_sqlite_get_int(p->db, sql);
    sqlite3_free(sql);

    sql = sqlite3_mprintf("select count("META_DB_ID") from stations where SOURCE_ID = %"PRId64";", tab.val[i]);
    num_stations = bg_sqlite_get_int(p->db, sql);
    sqlite3_free(sql);

    sql = sqlite3_mprintf("UPDATE SOURCES set NUM_COUNTRIES = %"PRId64", NUM_LANGUAGES = %"PRId64", NUM_CATEGORIES = %"PRId64", NUM_TAGS = %"PRId64", NUM_STATIONS = %"PRId64" where "META_DB_ID" = %"PRId64";", num_countries, num_languages, num_categories, num_tags, num_stations, tab.val[i]);

    fprintf(stderr, "SQL: %s\n", sql);
    
    bg_sqlite_exec(p->db, sql, NULL, NULL);
    sqlite3_free(sql);
    }
  
  bg_sqlite_id_tab_free(&tab);
  
  bg_sqlite_end_transaction(p->db);

  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Finished rescannig streams");
  bg_mdb_track_lock(be, 0, p->root_container);

  /* Update root folder */
  init_sources(be);

  evt = bg_msg_sink_get(be->ctrl.evt_sink);
  gavl_msg_set_id_ns(evt, BG_MSG_DB_OBJECT_CHANGED, BG_MSG_NS_DB);
  
  gavl_dictionary_set_string(&evt->header, GAVL_MSG_CONTEXT_ID, BG_MDB_ID_STREAMS);
  
  gavl_msg_set_arg_dictionary(evt, 0, p->root_container);
  bg_msg_sink_put(be->ctrl.evt_sink, evt);

  
  }


static void create_tables(bg_mdb_backend_t * be)
  {
  streams_t * priv = be->priv;

  /* Object table */  
  if(!bg_sqlite_exec(priv->db,
                     "CREATE TABLE IF NOT EXISTS sources("META_DB_ID" INTEGER PRIMARY KEY, "
                     GAVL_META_LABEL" TEXT, NUM_LANGUAGES INTEGER, NUM_TAGS INTEGER, NUM_COUNTRIES INTEGER, NUM_CATEGORIES INTEGER, NUM_STATIONS INTEGER);",
                     NULL, NULL) ||
     !bg_sqlite_exec(priv->db,
                     "CREATE TABLE IF NOT EXISTS stations("META_DB_ID" INTEGER PRIMARY KEY, "
                     GAVL_META_LABEL" TEXT, "GAVL_META_SEARCH_TITLE" TEXT, "GAVL_META_URI" TEXT, "GAVL_META_STATION_URL" TEXT, "GAVL_META_LOGO_URL" TEXT,"
                     "TYPE INTEGER, SOURCE_ID INTEGER);",
                     NULL, NULL) ||
     !bg_sqlite_exec(priv->db,
                     "CREATE TABLE IF NOT EXISTS tags(ID INTEGER PRIMARY KEY, NAME TEXT);", NULL, NULL) ||
     !bg_sqlite_exec(priv->db,
                     "CREATE TABLE IF NOT EXISTS station_tags(ID INTEGER PRIMARY KEY, ATTR_ID INTEGER, STATION_ID INTEGER, SOURCE_ID INTEGER);", NULL, NULL) ||
     !bg_sqlite_exec(priv->db,
                     "CREATE TABLE IF NOT EXISTS categories(ID INTEGER PRIMARY KEY, NAME TEXT);", NULL, NULL) ||
     !bg_sqlite_exec(priv->db,
                     "CREATE TABLE IF NOT EXISTS station_categories(ID INTEGER PRIMARY KEY, ATTR_ID INTEGER, STATION_ID INTEGER, SOURCE_ID INTEGER);", NULL, NULL) ||
     !bg_sqlite_exec(priv->db,
                     "CREATE TABLE IF NOT EXISTS countries(ID INTEGER PRIMARY KEY, NAME TEXT, CODE TEXT);", NULL, NULL) ||
     !bg_sqlite_exec(priv->db,
                     "CREATE TABLE IF NOT EXISTS station_countries(ID INTEGER PRIMARY KEY, ATTR_ID INTEGER, STATION_ID INTEGER, SOURCE_ID INTEGER);", NULL, NULL) ||
     !bg_sqlite_exec(priv->db,
                     "CREATE TABLE IF NOT EXISTS languages(ID INTEGER PRIMARY KEY, NAME TEXT, CODE TEXT);", NULL, NULL) ||
     !bg_sqlite_exec(priv->db,
                     "CREATE TABLE IF NOT EXISTS station_languages(ID INTEGER PRIMARY KEY, ATTR_ID INTEGER, STATION_ID INTEGER, SOURCE_ID INTEGER);", NULL, NULL))
    return;
  
  }


static int get_source_callback(void * data, int argc, char **argv, char **azColName)
  {
  int i;
  gavl_value_t val;
  gavl_value_t browse_modes_val;
  gavl_value_t browse_mode_val;
  gavl_dictionary_t * dict;
  gavl_dictionary_t * m;
  gavl_dictionary_t * mdb;
  gavl_array_t * browse_modes;
  
  int64_t id;
  int num_items = 0;
  int num_containers = 0;
  int num_stations = 0;
  int num_tags = 0, num_languages = 0, num_categories = 0, num_countries = 0;
  int num = 0;
  bg_mdb_backend_t * be = data;
  streams_t * p = be->priv;
  
  
  gavl_value_init(&val);
  dict = gavl_value_set_dictionary(&val);
  
  m = gavl_dictionary_get_dictionary_create(dict, GAVL_META_METADATA);
  mdb = gavl_dictionary_get_dictionary_create(dict, BG_MDB_DICT);
  
  for(i = 0; i < argc; i++)
    {
    if(!strcmp(azColName[i], META_DB_ID))
      {
      id = strtoll(argv[i], NULL, 10);
      }
    else if(!strcmp(azColName[i], GAVL_META_LABEL))
      {
      gavl_dictionary_set_string(m, GAVL_META_LABEL, argv[i]);
      }
    else if(!strcmp(azColName[i], "NUM_TAGS") && argv[i])
      {
      num_tags = atoi(argv[i]);
      }
    else if(!strcmp(azColName[i], "NUM_LANGUAGES") && argv[i])
      {
      num_languages = atoi(argv[i]);
      }
    else if(!strcmp(azColName[i], "NUM_COUNTRIES") && argv[i])
      {
      num_countries = atoi(argv[i]);
      }
    else if(!strcmp(azColName[i], "NUM_CATEGORIES") && argv[i])
      {
      num_categories = atoi(argv[i]);
      }
    else if(!strcmp(azColName[i], "NUM_STATIONS") && argv[i])
      {
      num_stations = atoi(argv[i]);
      }
    }
  
  gavl_dictionary_set_string(m, GAVL_META_MEDIA_CLASS, GAVL_META_MEDIA_CLASS_CONTAINER);

  //  get_source_children(s, &browse_mode, &num_containers, &num_items);

  gavl_dictionary_set_string_nocopy(m, GAVL_META_ID, gavl_sprintf("%s/%"PRId64, BG_MDB_ID_STREAMS, id));

  gavl_value_init(&browse_modes_val);
  gavl_value_init(&browse_mode_val);
  browse_modes = gavl_value_set_array(&browse_modes_val);
  
  if(num_tags)
    {
    num++;

    gavl_value_set_int(&browse_mode_val, BROWSE_BY_TAG);
    gavl_array_splice_val_nocopy(browse_modes, -1, 0, &browse_mode_val);
    }

  if(num_categories)
    {
    num++;
    gavl_value_set_int(&browse_mode_val, BROWSE_BY_CATEGORY);
    gavl_array_splice_val_nocopy(browse_modes, -1, 0, &browse_mode_val);
    }
  
  if(num_countries)
    {
    num++;
    gavl_value_set_int(&browse_mode_val, BROWSE_BY_COUNTRY);
    gavl_array_splice_val_nocopy(browse_modes, -1, 0, &browse_mode_val);
    }
  
  if(num_languages)
    {
    num++;
    gavl_value_set_int(&browse_mode_val, BROWSE_BY_LANGUAGE);
    gavl_array_splice_val_nocopy(browse_modes, -1, 0, &browse_mode_val);
    }

  gavl_value_set_int(&browse_mode_val, BROWSE_ALL);
  gavl_array_splice_val_nocopy(browse_modes, -1, 0, &browse_mode_val);
  
  if(!num)
    {
    //    browse_mode = BROWSE_ALL;
    
    num_items = get_source_count(p, id, "num_stations");
    
    if(num_items > GROUP_THRESHOLD)
      {
      /* TODO: Make groups */
      }
    
    }
  else
    num_containers = num+1;

  gavl_dictionary_set_long(mdb, META_DB_ID, id);
  
  gavl_dictionary_set_nocopy(mdb, "browse_modes", &browse_modes_val);

  gavl_dictionary_set_int(mdb, "num_stations", num_stations);
  gavl_dictionary_set_int(mdb, "num_languages", num_languages);
  gavl_dictionary_set_int(mdb, "num_tags", num_tags);
  gavl_dictionary_set_int(mdb, "num_categories", num_categories);
  gavl_dictionary_set_int(mdb, "num_countries", num_countries);
  
  gavl_track_set_num_children(dict , num_containers, num_items);
  
  gavl_array_splice_val_nocopy(&p->sources, -1, 0, &val);
  return 0;
  }


static int browse_object(bg_mdb_backend_t * be, const char * id, gavl_dictionary_t * ret,
                         int * idx, int * total)
  {
  fprintf(stderr, "Streams: browse_object %s\n", id);

  
  
  /* TODO: IDX + total */
  
  return 0;
  }

#define APPEND_WHERE() \
  if(!have_where) \
    { \
    sql = gavl_strcat(sql, "where "); \
    have_where = 1; \
    } \
  else \
    { \
    sql = gavl_strcat(sql, "and "); \
    }

static int query_children(bg_mdb_backend_t * be, bg_sqlite_id_tab_t * tab,
                          int browse_mode,
                          int64_t source_id,
                          int64_t attribute,
                          const char * group)
  {
  char * sql = NULL;
  char * tmp_string;
  int ret = 0;
  const char * attr_table = NULL;
  int have_where = 0;
  streams_t * priv;
  
  priv = be->priv;

  if(browse_mode == BROWSE_NONE)
    return 0;
  
  if(tab)
    {
    sql = gavl_strcat(sql, "select "META_DB_ID" from stations ");
    }
  else
    {
    sql = gavl_strcat(sql, "select count("META_DB_ID") from stations ");
    }

  attr_table = get_browse_table(browse_mode);
  
  if(attr_table)
    {
    tmp_string = gavl_sprintf("inner join station_%s on stations.DBID = station_%s.STATION_ID where ATTR_ID = %"PRId64" ",
                              attr_table, attr_table, attribute);
    sql = gavl_strcat(sql, tmp_string);
    free(tmp_string);
    have_where = 1;
    }

  if(source_id > 0)
    {
    APPEND_WHERE();
    tmp_string = gavl_sprintf("stations.SOURCE_ID = %"PRId64" ", source_id);
    sql = gavl_strcat(sql, tmp_string);
    free(tmp_string);
    }
  
  if(group)
    {
    char * group_condition;
    APPEND_WHERE();
    
    group_condition = bg_sqlite_make_group_condition(group);

    tmp_string = gavl_sprintf(" "GAVL_META_SEARCH_TITLE" %s ", group_condition);
    sql = gavl_strcat(sql, tmp_string);
    
    free(tmp_string);
    free(group_condition);
    }


  if(tab)
    sql = gavl_strcat(sql, "order by "GAVL_META_SEARCH_TITLE" COLLATE strcoll;");
  else
    sql = gavl_strcat(sql, ";");
  
  
  if(tab)
    {
    ret = bg_sqlite_exec(priv->db, sql, bg_sqlite_append_id_callback, tab);

    if(ret)
      ret = tab->num_val;
    }
  else
    {
    ret = bg_sqlite_get_int(priv->db, sql);
    if(ret < 0)
      ret = 0;
    }
  
  free(sql);
  return ret;
  }

static void browse_leafs(bg_mdb_backend_t * be, gavl_msg_t * msg,
                         int browse_mode,
                         int64_t source_id,
                         int64_t attribute,
                         const char * group,
                         const char * parent_id,
                         int start, int num, int one_answer)
  {
  int i;
  int num_sent = 0;
  int idx;
  bg_sqlite_id_tab_t tab;
  //  const char * attr_table;
  //  streams_t * priv;
  gavl_array_t arr;
  gavl_msg_t * res;
  gavl_time_t time_msg = 0;
  gavl_time_t current_time = 0;
  gavl_time_t start_time = gavl_timer_get(be->db->timer);
  
  bg_sqlite_id_tab_init(&tab);
  gavl_array_init(&arr);
  
  //  priv = be->priv;

  if(!query_children(be, &tab,
                     browse_mode,
                     source_id,
                     attribute,
                     group))
    goto fail;
  
  if(!bg_mdb_adjust_num(start, &num, tab.num_val))
    goto fail;

  idx = start;
  
  for(i = 0; i < num; i++)
    {
    gavl_value_t val;
    gavl_dictionary_t * dict;
    
    gavl_value_init(&val);
    dict = gavl_value_set_dictionary(&val);
    
    if(!browse_station(be, dict, tab.val[i+start]))
      goto fail;

    gavl_track_set_id_nocopy(dict, gavl_sprintf("%s/%"PRId64, parent_id, tab.val[i+start]));
    
    gavl_array_splice_val_nocopy(&arr, -1, 0, &val);

    /* TODO: Send partial answers */
    
    if(!one_answer)
      {
      current_time = gavl_timer_get(be->db->timer) - start_time;
    
      if((current_time - time_msg > GAVL_TIME_SCALE) && (arr.num_entries > 0))
        {
        int last = 0;
        /* Flush objects */
        gavl_msg_t * res = bg_msg_sink_get(be->ctrl.evt_sink);

        if(num_sent + arr.num_entries == num)
          last = 1;
        
        bg_mdb_set_browse_children_response(res, &arr, msg, &idx, last, tab.num_val);
        bg_msg_sink_put(be->ctrl.evt_sink, res);
        time_msg = current_time;
        num_sent += arr.num_entries;
        gavl_array_reset(&arr);
        }

      }
    }

  if(arr.num_entries)
    {
    res = bg_msg_sink_get(be->ctrl.evt_sink);
    bg_mdb_set_browse_children_response(res, &arr, msg, &idx, 1, tab.num_val);
    bg_msg_sink_put(be->ctrl.evt_sink, res);
    }
  
  fail:
  
  bg_sqlite_id_tab_free(&tab);
  }

typedef struct
  {
  gavl_array_t * ret;
  gavl_dictionary_t m_tmpl;
  const char * parent_id;

  int browse_mode;
  int64_t source_id;

  const char * attr_table;
  
  streams_t * priv;
  } browse_containers_t;

static int
browse_containers_callback(void * data, int argc, char **argv, char **azColName)
  {
  int i;
  browse_containers_t * b = data;
  gavl_value_t val;
  gavl_dictionary_t * dict;
  gavl_dictionary_t * m;
  char * sql = NULL;
  int64_t id;
  int num_children;

  
  gavl_value_init(&val);
  dict = gavl_value_set_dictionary(&val);
  m = gavl_dictionary_get_dictionary_create(dict, GAVL_META_METADATA);
  
  gavl_dictionary_copy(m, &b->m_tmpl);
  
  for(i = 0; i < argc; i++)
    {
    if(!strcmp(azColName[i], "ID"))
      {
      gavl_track_set_id_nocopy(dict, gavl_sprintf("%s/%s", b->parent_id, argv[i]));
      id = strtoll(argv[i], NULL, 10);
      }
    else if(!strcmp(azColName[i], "NAME"))
      gavl_dictionary_set_string(m, GAVL_META_LABEL, argv[i]);
    }

  sql = gavl_sprintf("select count(id) from station_%s where source_id = %"PRId64" and attr_id = %"PRId64";", b->attr_table, b->source_id, id);
  
  num_children = bg_sqlite_get_int(b->priv->db, sql);
  
  if(num_children > GROUP_THRESHOLD)
    {
    sql = gavl_sprintf("select exists(select 1 from stations inner join station_%s on stations."META_DB_ID" = station_%s.station_id where stations.SOURCE_ID = %"PRId64" AND ATTR_ID = %"PRId64" AND "GAVL_META_SEARCH_TITLE" %%s);", b->attr_table, b->attr_table, b->source_id, id);
    
    num_children = bg_sqlite_count_groups(b->priv->db, sql);
    free(sql);
    gavl_track_set_num_children(dict, num_children, 0);
    }
  else 
    gavl_track_set_num_children(dict, 0, num_children);

  gavl_array_splice_val_nocopy(b->ret, -1, 0, &val);
  
  return 0;
  }

static void browse_containers(bg_mdb_backend_t * be,
                              const char * parent_id,
                              int browse_mode,
                              int64_t source_id,
                              const char * group,
                              gavl_array_t * ret,
                              int start, int num)
  {
  char *sql;
  char *tmp_string;
  streams_t * p = be->priv;

  browse_containers_t b;

  b.ret = ret;
  b.parent_id = parent_id;
  b.browse_mode = browse_mode;
  b.source_id = source_id;
  b.priv = be->priv;
  b.attr_table = get_browse_table(browse_mode);
  
  gavl_dictionary_init(&b.m_tmpl);
  
  switch(browse_mode)
    {
    case BROWSE_BY_TAG:
      gavl_dictionary_set_string(&b.m_tmpl, GAVL_META_MEDIA_CLASS, GAVL_META_MEDIA_CLASS_CONTAINER_TAG);
      break; 
    case BROWSE_BY_COUNTRY:
      gavl_dictionary_set_string(&b.m_tmpl, GAVL_META_MEDIA_CLASS, GAVL_META_MEDIA_CLASS_CONTAINER_COUNTRY);
      break; 
    case BROWSE_BY_LANGUAGE:
      gavl_dictionary_set_string(&b.m_tmpl, GAVL_META_MEDIA_CLASS, GAVL_META_MEDIA_CLASS_CONTAINER_LANGUAGE);
      break; 
    case BROWSE_BY_CATEGORY:
      gavl_dictionary_set_string(&b.m_tmpl, GAVL_META_MEDIA_CLASS, GAVL_META_MEDIA_CLASS_CONTAINER);
      break; 
    }

  sql = gavl_sprintf("select distinct name, %s.id from %s inner join station_%s on %s.id = station_%s.attr_id where source_id = %"PRId64,
                     b.attr_table, b.attr_table, b.attr_table, b.attr_table, b.attr_table, source_id);

  if(group)
    {
    sql = gavl_strcat(sql, " and name ");
    tmp_string = bg_sqlite_make_group_condition(group);
    sql = gavl_strcat(sql, tmp_string);
    free(tmp_string);
    }

  sql = gavl_strcat(sql, " order by name");

  if(start > 0)
    {
    tmp_string = gavl_sprintf(" offset %d", start);
    sql = gavl_strcat(sql, tmp_string);
    free(tmp_string);
    }
  if(num > 0)
    {
    tmp_string = gavl_sprintf(" limit %d", num);
    sql = gavl_strcat(sql, tmp_string);
    free(tmp_string);
    }

  sql = gavl_strcat(sql, ";");

  bg_sqlite_exec(p->db, sql, browse_containers_callback, &b);
  
  gavl_dictionary_free(&b.m_tmpl);
  
  }
                              
                              

static void browse_children(bg_mdb_backend_t * be, gavl_msg_t * msg, const char * ctx_id,
                            int start, int num, int one_answer)
  {
  char * sql;
  streams_t * priv;
  //  int result;
  bg_sqlite_id_tab_t tab;
  gavl_array_t arr;

  int i;
  gavl_msg_t * res;
  int total = 0;
  const char * ctx_id_orig = ctx_id;
  int64_t source_id = -1;
  int64_t attribute = -1;
  int browse_mode = 0;
  const char * group = NULL;
  const char * attr_table = NULL;
  char ** path;
  int path_idx;

  int num_items = 0;
  int num_containers = 0;

  const gavl_dictionary_t * mdb;
  const gavl_dictionary_t * source;
  const gavl_array_t * browse_modes;
  
  priv = be->priv;

  bg_sqlite_id_tab_init(&tab);
  
  fprintf(stderr, "Streams: browse_children %s\n", ctx_id);
  
  path = gavl_strbreak(ctx_id+1, '/');
  
  gavl_array_init(&arr);

  path_idx = 1;
  if(!path[path_idx]) // /streams
    {
    /* Get sources as containers */
    
    if(!bg_mdb_adjust_num(start, &num, priv->sources.num_entries))
      goto fail;
    
    for(i = 0; i < num; i++)
      {
      gavl_array_splice_val(&arr, -1, 0, &priv->sources.entries[i+start]);
      }
    total = priv->sources.num_entries;
    goto end;
    }

  source_id = strtoll(path[path_idx], NULL, 10);

  if(!(source = get_source_by_id(priv, source_id)) ||
     !(mdb = gavl_dictionary_get_dictionary(source, BG_MDB_DICT)) ||
     !(browse_modes = gavl_dictionary_get_array(mdb, "browse_modes")))
    goto fail;

  if(browse_modes->num_entries == 1)
    {
    browse_mode = BROWSE_ALL;
    }
  
  path_idx++;
  
  if(!path[path_idx])  // /streams/1
    {
    if(browse_modes->num_entries > 1)
      {
      if(!bg_mdb_adjust_num(start, &num, browse_modes->num_entries))
        goto fail;

      for(i = 0; i < num; i++)
        {
        int mode;
        const char * id = NULL;
        const char * klass = NULL;
        const char * child_class = NULL;
        const char * label = NULL;

        num_items = 0;
        num_containers = 0;
        
        if(!gavl_value_get_int(&browse_modes->entries[i+start], &mode))
          continue;

        attr_table = get_browse_table(mode);
        
        switch(mode)
          {
          case BROWSE_ALL:
            {
            id = "all";
            label = "All";
            klass = GAVL_META_MEDIA_CLASS_CONTAINER;

            num_items = get_source_count(priv, source_id, "num_stations");
              
            if(num_items > GROUP_THRESHOLD)
              {
              num_items = 0;
              sql = gavl_sprintf("select exists(select 1 from stations where SOURCE_ID = %"PRId64" AND "GAVL_META_SEARCH_TITLE" %%s);", source_id);
              num_containers = bg_sqlite_count_groups(priv->db, sql);
              free(sql);
              child_class = GAVL_META_MEDIA_CLASS_CONTAINER;
              }
            }
            break;
          case BROWSE_BY_TAG:
            {
            id = "tag";
            label = "Tags";
            klass = GAVL_META_MEDIA_CLASS_CONTAINER;

            num_containers = get_source_count(priv, source_id, "num_tags");
              
            child_class = GAVL_META_MEDIA_CLASS_CONTAINER_TAG;
            
            }
            break;
          case BROWSE_BY_COUNTRY:
            {
            id = "country";
            label = "Countries";
            klass = GAVL_META_MEDIA_CLASS_CONTAINER;
            
            num_containers = get_source_count(priv, source_id, "num_countries");
            child_class = GAVL_META_MEDIA_CLASS_CONTAINER_COUNTRY;
            }
            break;
          case BROWSE_BY_LANGUAGE:
            {
            id = "language";
            label = "Languages";
            klass = GAVL_META_MEDIA_CLASS_CONTAINER;
            
            num_containers = get_source_count(priv, source_id, "num_languages");
            child_class = GAVL_META_MEDIA_CLASS_CONTAINER_LANGUAGE;
            }
            break;
          case BROWSE_BY_CATEGORY:
            {
            id = "category";
            label = "Categories";
            klass = GAVL_META_MEDIA_CLASS_CONTAINER;
            child_class = GAVL_META_MEDIA_CLASS_CONTAINER;
            num_containers = get_source_count(priv, source_id, "num_categories");
            }
            break;
          }

        if(attr_table && (num_containers > GROUP_THRESHOLD))
          {
          sql = gavl_sprintf("select exists(select 1 from station_%s inner join %s on station_%s.attr_id = %s.id where SOURCE_ID = %"PRId64" and NAME %%s);", attr_table, attr_table, attr_table, attr_table, source_id);
          
          num_containers = bg_sqlite_count_groups(priv->db, sql);
          free(sql);
          child_class = GAVL_META_MEDIA_CLASS_CONTAINER;
          }
        
        if(label)
          {
          gavl_value_t val;
          gavl_dictionary_t * dict;
          gavl_dictionary_t * m;
            
          gavl_value_init(&val);
          dict = gavl_value_set_dictionary(&val);
          m = gavl_dictionary_get_dictionary_create(dict, GAVL_META_METADATA);
          gavl_dictionary_set_string(m, GAVL_META_LABEL, label);
          gavl_dictionary_set_string_nocopy(m, GAVL_META_ID, gavl_sprintf("%s/%s", ctx_id_orig, id));
          gavl_dictionary_set_string(m, GAVL_META_MEDIA_CLASS, klass);
          if(child_class)
            gavl_dictionary_set_string(m, GAVL_META_CHILD_CLASS, child_class);
            
          gavl_track_set_num_children(dict, num_containers, num_items);
          gavl_array_splice_val_nocopy(&arr, -1, 0, &val);
          }

        }
        
      }
    else
      {
      /* TODO: Just "all" available */
        
      }
    goto end;
    }
  // /streams/1/language, /streams/1/country, /streams/1/category, /streams/1/tags 

  if(browse_modes->num_entries > 1)
    {
    if(!strcmp(path[path_idx], "language"))
      {
      browse_mode = BROWSE_BY_LANGUAGE;
      path_idx++;
      }
    else if(!strcmp(path[path_idx], "country"))
      {
      browse_mode = BROWSE_BY_COUNTRY;
      path_idx++;
      }
    else if(!strcmp(path[path_idx], "tag"))
      {
      browse_mode = BROWSE_BY_TAG;
      path_idx++;
      }
    else if(!strcmp(path[path_idx], "category"))
      {
      browse_mode = BROWSE_BY_CATEGORY;
      path_idx++;
      }
    else if(!strcmp(path[path_idx], "all"))
      {
      browse_mode = BROWSE_ALL;
      path_idx++;
      }
    else
      goto fail;
    }

  attr_table = get_browse_table(browse_mode);
  
  if(path[path_idx] && gavl_string_starts_with(path[path_idx], BG_MDB_GROUP_PREFIX))
    {
    group = path[path_idx];
    path_idx++;
    }
  
  if(!path[path_idx])
    {
    switch(browse_mode)
      {
      case BROWSE_BY_LANGUAGE: // /streams/1/language
        num_containers = get_source_count(priv, source_id, "num_languages");

        if(!group && (num_containers > GROUP_THRESHOLD))
          {
          gavl_dictionary_t m_tmpl;
          gavl_dictionary_init(&m_tmpl);
          gavl_dictionary_set_string(&m_tmpl, GAVL_META_CHILD_CLASS, GAVL_META_MEDIA_CLASS_CONTAINER_COUNTRY);
          
          sql = gavl_sprintf("select count(distinct languages.id) from languages inner join station_languages on languages.ID = station_languages.ATTR_ID where SOURCE_ID = %"PRId64" and NAME %%s", source_id);
          
          bg_sqlite_add_groups(priv->db, &arr, ctx_id_orig, sql, &m_tmpl, start, num);
          gavl_dictionary_free(&m_tmpl);
          free(sql);
          
          }
        else
          browse_containers(be, ctx_id_orig, browse_mode, source_id, group, &arr, start, num);
        
        break;
      case BROWSE_BY_COUNTRY:
        num_containers = get_source_count(priv, source_id, "num_countries");

        if(!group && (num_containers > GROUP_THRESHOLD))
          {
          gavl_dictionary_t m_tmpl;
          gavl_dictionary_init(&m_tmpl);
          gavl_dictionary_set_string(&m_tmpl, GAVL_META_CHILD_CLASS, GAVL_META_MEDIA_CLASS_CONTAINER_COUNTRY);
          
          sql = gavl_sprintf("select count(distinct countries.id) from countries inner join station_countries on countries.ID = station_countries.ATTR_ID where SOURCE_ID = %"PRId64" and NAME %%s", source_id);
          
          bg_sqlite_add_groups(priv->db, &arr, ctx_id_orig, sql, &m_tmpl, start, num);
          gavl_dictionary_free(&m_tmpl);
          free(sql);
          }
        else
          browse_containers(be, ctx_id_orig, browse_mode, source_id, group, &arr, start, num);
        break;
      case BROWSE_BY_TAG:
        num_containers = get_source_count(priv, source_id, "num_tags");

        if(!group && (num_containers > GROUP_THRESHOLD))
          {
          gavl_dictionary_t m_tmpl;
          gavl_dictionary_init(&m_tmpl);
          gavl_dictionary_set_string(&m_tmpl, GAVL_META_CHILD_CLASS, GAVL_META_MEDIA_CLASS_CONTAINER_TAG);
          
          sql = gavl_sprintf("select count(distinct tags.id) from tags inner join station_tags on tags.ID = station_tags.ATTR_ID where SOURCE_ID = %"PRId64" and NAME %%s", source_id);
          
          bg_sqlite_add_groups(priv->db, &arr, ctx_id_orig, sql, &m_tmpl, start, num);
          gavl_dictionary_free(&m_tmpl);
          free(sql);
          
          }
        else
          browse_containers(be, ctx_id_orig, browse_mode, source_id, group, &arr, start, num);
        break;
      case BROWSE_BY_CATEGORY:
        num_containers = get_source_count(priv, source_id, "num_categories");

        if(!group && (num_containers > GROUP_THRESHOLD))
          {
          gavl_dictionary_t m_tmpl;
          gavl_dictionary_init(&m_tmpl);
          gavl_dictionary_set_string(&m_tmpl, GAVL_META_CHILD_CLASS, GAVL_META_MEDIA_CLASS_CONTAINER_TAG);
          
          sql = gavl_sprintf("select count(distinct categories.id) from categories inner join station_categories on categories.ID = station_categories.ATTR_ID where SOURCE_ID = %"PRId64" and NAME %%s", source_id);
          
          bg_sqlite_add_groups(priv->db, &arr, ctx_id_orig, sql, &m_tmpl, start, num);
          gavl_dictionary_free(&m_tmpl);
          free(sql);
          
          }
        else
          browse_containers(be, ctx_id_orig, browse_mode, source_id, group, &arr, start, num);
        
        break;
      case BROWSE_ALL:
        num_items = get_source_count(priv, source_id, "num_stations");
        
        if(!group && (num_items > GROUP_THRESHOLD))
          {
          gavl_dictionary_t m_tmpl;
          gavl_dictionary_init(&m_tmpl);

          sql = gavl_sprintf("select count("META_DB_ID") from stations where SOURCE_ID = %"PRId64" and "GAVL_META_SEARCH_TITLE" %%s", source_id);
          
          bg_sqlite_add_groups(priv->db, &arr, ctx_id_orig, sql, &m_tmpl, start, num);
          gavl_dictionary_free(&m_tmpl);
          free(sql);
          }
        else
          browse_leafs(be, msg, browse_mode, source_id, -1,group, ctx_id_orig, start, num, one_answer);
        
        break;
      }
    goto end;
    }

  attribute = strtoll(path[path_idx], NULL, 10);
  
  group = NULL;
  
  if(attribute < 1)
    goto fail;

  path_idx++;
  
  if(path[path_idx] && gavl_string_starts_with(path[path_idx], BG_MDB_GROUP_PREFIX))
    {
    group = path[path_idx];
    path_idx++;
    }

  if(path[path_idx])
    goto fail;
  
  /* Leaf */

  if(group || (query_children(be, NULL, browse_mode, source_id, attribute, group) <= GROUP_THRESHOLD))
    {
    browse_leafs(be, msg, browse_mode, source_id, attribute, group, ctx_id_orig, start, num, one_answer);
    }
  else
    {
    /* Append groups */

    gavl_dictionary_t m_tmpl;
    gavl_dictionary_init(&m_tmpl);
    
    sql = gavl_sprintf("select count("META_DB_ID") from stations inner join station_%s on stations."META_DB_ID" = station_%s.STATION_ID where stations.SOURCE_ID = %"PRId64" and station_%s.attr_id = %"PRId64" and "GAVL_META_SEARCH_TITLE" %%s;", attr_table, attr_table, source_id, attr_table, attribute);
    
    bg_sqlite_add_groups(priv->db, &arr, ctx_id_orig, sql, &m_tmpl, start, num);
    gavl_dictionary_free(&m_tmpl);
    free(sql);
    }
  
  
  //  sql = 
  
  end:

  if(arr.num_entries > 0)
    {
    res = bg_msg_sink_get(be->ctrl.evt_sink);
    bg_mdb_set_browse_children_response(res, &arr, msg, &start, 1, total);
    bg_msg_sink_put(be->ctrl.evt_sink, res);
    }
  

  fail:
  gavl_array_free(&arr);
  bg_sqlite_id_tab_free(&tab);
  gavl_strbreak_free(path);
    
  return;
  
  }

static int handle_msg(void * priv, gavl_msg_t * msg)
  {
  bg_mdb_backend_t * be = priv;
  //  streams_t * s = be->priv;
  switch(msg->NS)
    {
    case BG_MSG_NS_DB:
      {
      switch(msg->ID)
        {
        case BG_FUNC_DB_BROWSE_OBJECT:
          {
          int idx = -1, total = -1;
          gavl_dictionary_t ret;
          const char * ctx_id = gavl_dictionary_get_string(&msg->header,
                                                           GAVL_MSG_CONTEXT_ID);
          
          gavl_dictionary_init(&ret);
          if(browse_object(be, ctx_id, &ret, &idx, &total))
            {
            gavl_msg_t * res = bg_msg_sink_get(be->ctrl.evt_sink);

            bg_mdb_set_browse_obj_response(res, &ret, msg, idx, total);
            bg_msg_sink_put(be->ctrl.evt_sink, res);
            }
          gavl_dictionary_free(&ret);
          }
          break;
        case BG_FUNC_DB_BROWSE_CHILDREN:
          {
          const char * ctx_id;
          int start, num, one_answer;
          
          //          gavl_msg_dump(msg, 2);

          bg_mdb_get_browse_children_request(msg, &ctx_id, &start, &num, &one_answer);
          browse_children(be, msg, ctx_id, start, num, one_answer);
          }
          break;
        case BG_CMD_DB_RESCAN:
          {
#if 0
        case BG_CMD_DB_RESCAN:
          {
          int i;
          gavl_array_t sql_dirs;
          gavl_msg_t * res;
          
          gavl_array_init(&sql_dirs);
          bg_sqlite_get_string_array(s->db, "scandirs", "PATH", &sql_dirs);
          
          /* Rescan */
          lock_root_containers(be, 1);

          for(i = 0; i < sql_dirs.num_entries; i++)
            add_directory(be, gavl_string_array_get(&sql_dirs, i));

          /* Update thumbnails */
          make_thumbnails(be);
          
          lock_root_containers(be, 0);
          
          gavl_array_free(&sql_dirs);

          /* Send done event */
          
          res = bg_msg_sink_get(be->ctrl.evt_sink);
          gavl_msg_set_id_ns(res, BG_MSG_DB_RESCAN_DONE, BG_MSG_NS_DB);
          bg_msg_sink_put(be->ctrl.evt_sink, res);
          }
          break;
          
#endif       
          
          gavl_msg_t * res;
          rescan(be);
          /* Send done event */
          
          res = bg_msg_sink_get(be->ctrl.evt_sink);
          gavl_msg_set_id_ns(res, BG_MSG_DB_RESCAN_DONE, BG_MSG_NS_DB);
          bg_msg_sink_put(be->ctrl.evt_sink, res);

          }
          break;
        }
      break;
      }
    }
  return 1;
  }

static int ping_streams(bg_mdb_backend_t * b)
  {
  rescan(b);
  
  b->ping_func = NULL;
  return 1;
  }

static void init_sources(bg_mdb_backend_t * b)
  {
  int result;
  char * sql;
    
  streams_t * priv = b->priv;
  
  if((result = bg_sqlite_get_int(priv->db, "SELECT count("META_DB_ID") from sources;" )) > 0)
    {
    gavl_track_set_num_children(priv->root_container, result, 0);
    }
  else
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Got no sources");

  /* Initialize sources */

  /* Get sources as containers */

  sql = sqlite3_mprintf("select * from SOURCES order by "GAVL_META_LABEL" COLLATE strcoll;");
  bg_sqlite_exec(priv->db, sql, get_source_callback, b);
  sqlite3_free(sql);

  }

void bg_mdb_create_streams(bg_mdb_backend_t * b)
  {
  streams_t * priv;
  //  gavl_dictionary_t * container;
  //  gavl_dictionary_t * child;
  //  gavl_dictionary_t * child_m;
  //  const gavl_dictionary_t * container_m;
  char * filename;
  int new_file = 0;

  priv = calloc(1, sizeof(*priv));
  b->priv = priv;

  b->flags |= BE_FLAG_RESCAN;
  
  filename = gavl_sprintf("%s/streams.sqlite", b->db->path);

  if(access(filename, R_OK))
    new_file = 1;
  
  sqlite3_open(filename, &priv->db);
  
  bg_sqlite_init_strcoll(priv->db);
  
  create_tables(b);
  
  priv->root_container = bg_mdb_get_root_container(b->db, GAVL_META_MEDIA_CLASS_ROOT_STREAMS);

  bg_mdb_container_set_backend(priv->root_container, MDB_BACKEND_STREAMS);

  init_sources(b);
  
  bg_controllable_init(&b->ctrl,
                       bg_msg_sink_create(handle_msg, b, 0),
                       bg_msg_hub_create(1));
  
  b->destroy = destroy_streams;

  if(new_file)
    b->ping_func = ping_streams;
  }

#if 0
static void import_icecast(bg_mdb_backend_t * b)
  {
  int stations_added = 0;
  //  char * mimetype;
  xmlNodePtr directory;
  xmlNodePtr entry;
  xmlDocPtr doc;
  station_t s;
  const char * genre;
  int source_added = 0;
  int64_t source_id = -1;

  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Importing icecast streams");
  
  if(!(doc = bg_xml_from_url("http://dir.xiph.org/yp.xml", NULL)))
    {
    gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Failed to download icecast streams");
    goto fail;
    }

  if(!(directory = bg_xml_find_doc_child(doc, "directory")))
    {
    gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "No root element \"directory\" in icecast xml file");
    goto fail;
    }

  station_init(&s);
  
  entry = NULL;
  while(1)
    {
    if(!(entry = bg_xml_find_next_node_child_by_name(directory, entry, "entry")))
      break;

    s.name       = bg_xml_node_get_child_content(entry, "server_name");
    s.stream_uri = bg_xml_node_get_child_content(entry, "listen_url");
    
    genre = bg_xml_node_get_child_content(entry, "genre");
    
    if(s.name && s.stream_uri)
      {
      if(!source_added)
        {
        add_source(b, "Icecast", &source_id);
        source_added = 1;
        }

      s.source_id = source_id;
      
      if(genre && strlen(genre))
        gavl_string_array_add(&s.categories, genre);

      /* TODO: get MIME type from server_type */

      add_station(b, &s);
      }
    stations_added++;
    station_reset(&s);
    }

  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Imported %d icecast streams", stations_added);

  
  fail:

  if(doc)
    xmlFreeDoc(doc);
  
  }
#endif

#define NUM_RB_STATIONS 5000 // Stations to query at once

static const char * rb_servers[] =
  {
    "https://de1.api.radio-browser.info",
    "https://nl1.api.radio-browser.info",
    "https://fr1.api.radio-browser.info",
    NULL
  };

static const char * rb_dict_get_string(json_object * obj, const char * tag)
  {
  json_object * child;
  const char * ret;

  if(!json_object_object_get_ex(obj, tag, &child) ||
     !json_object_is_type(child, json_type_string) ||
     !(ret = json_object_get_string(child)))
    return NULL;
  return ret;
  }

static void rb_set_array(json_object * child, gavl_array_t * ret, const char * name, int language, int country)
  {
  char ** arr;
  const char * var;
  int idx;
  
  if((var = rb_dict_get_string(child, name)) &&
     (arr = gavl_strbreak(var, ',')))
    {
    idx = 0;
    while(arr[idx])
      {
      if(language)
        {
        const char * v = gavl_language_get_label_from_code(arr[idx]);
        if(v)
          gavl_string_array_add(ret, v);
        }
      else if(country)
        {
        const char * v = gavl_get_country_label(arr[idx]);
        if(v)
          gavl_string_array_add(ret, v);
        }
      else
        gavl_string_array_add(ret, arr[idx]);
      
      idx++;
      }
    }
  
  }

static int import_radiobrowser_sub(bg_mdb_backend_t * b, int start, int64_t source_id)
  {
  int ret = 0;
  station_t s;
  
  int uri_idx = 0;
  char * uri;
  json_object * obj;
  json_object * child;
  
  int num;
  int i;
  //  char ** arr;
  
  while(rb_servers[uri_idx])
    {
    uri = gavl_sprintf("%s/json/stations?order=name&offset=%d&limit=%d&hidebroken=true",
                       rb_servers[uri_idx], start, NUM_RB_STATIONS);
    
    //    gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Downloading %s", uri);
    
    if((obj = bg_json_from_url(uri, NULL)) &&
       json_object_is_type(obj, json_type_array))
      {
      //    gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Downloaded %s", uri);
      free(uri);
      break;
      }

    if(obj)
      json_object_put(obj);
    
    free(uri);
    uri_idx++;
    }

  if(!obj)
    {
    gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Failed to download radiobrowser streams");
    goto fail;
    }

  num = json_object_array_length(obj);

  station_init(&s);

  for(i = 0; i < num; i++)
    {
    const char * var;

    if(!(child = json_object_array_get_idx(obj, i)))
      {
      gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Invalid array element");
      continue;
      }
    
    if(!(var = rb_dict_get_string(child, "stationuuid")))
      {
      gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Station has no ID");
      continue;
      }
    uri = gavl_sprintf("%s/m3u/url/%s", rb_servers[uri_idx], var);
    
    s.name        = rb_dict_get_string(child, "name");
    s.logo_uri    = rb_dict_get_string(child, "favicon");
    s.station_uri = rb_dict_get_string(child, "homepage");
    s.stream_uri  = uri;
    s.source_id   = source_id;
    
    rb_set_array(child, &s.tags, "tags", 0, 0);
    rb_set_array(child, &s.languages, "languagecodes", 1, 0);
    rb_set_array(child, &s.countries, "countrycode", 0, 1);

    add_station(b, &s);
    station_reset(&s);
    free(uri);
    ret++;
    }
  
  fail:

  if(obj)
    json_object_put(obj);
  
  return ret;
  }

static int import_radiobrowser(bg_mdb_backend_t * b)
  {
  int start = 0;
  int stations_added = 0;
  int result;
  int64_t source_id = -1;

  gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Importing streams from radio-browser.info");
  
  add_source(b, "radio-browser.info", &source_id);
  
  while((result = import_radiobrowser_sub(b, start, source_id)))
    {
    stations_added += result;
    start += NUM_RB_STATIONS;
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Got %d streams from radio-browser.info", result);
    }
  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Imported %d streams from radio-browser.info", stations_added);
  return stations_added;
  }

static void import_m3u_array(gavl_array_t * ret, const gavl_dictionary_t * m, const char * key)
  {
  int i = 0;
  const gavl_value_t * val;
  const char * s;
  while((val = gavl_dictionary_get_item(m, key, i)))
    {
    if((s = gavl_value_get_string(val)))
      gavl_string_array_add(ret, s);
    i++;
    }
  }

static void import_m3u_sub(bg_mdb_backend_t * b, const char * uri, const char * label)
  {
  int i, num;
  station_t s;
  const gavl_dictionary_t * dict;
  const gavl_dictionary_t * m;
  int64_t source_id = -1;
  int stations_added = 0;
  gavl_dictionary_t * mi = bg_plugin_registry_load_media_info(bg_plugin_reg, uri, 0);

  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Importing stations from %s", uri);
  
  if(!mi)
    return;
  
  num = gavl_get_num_tracks(mi);
  
  
#if 0
  
typedef struct
  {
  int64_t id;
  const char * name;
  const char * stream_uri;
  const char * station_uri; // Homepage
  const char * logo_uri;
  int type;
  int64_t source_id;

  gavl_array_t categories;
  gavl_array_t tags;
  gavl_array_t countries; // Labels
  gavl_array_t languages; // Labels
  
  } station_t;

#endif

  add_source(b, label, &source_id);

  for(i = 0; i < num; i++)
    {
    station_init(&s);

    dict = gavl_get_track(mi, i);
    m = gavl_dictionary_get_dictionary(dict, GAVL_META_METADATA);
    
    s.name = gavl_dictionary_get_string(m, GAVL_META_LABEL);
    s.logo_uri = gavl_dictionary_get_string(m, GAVL_META_LOGO_URL);
    s.station_uri = gavl_dictionary_get_string(m, GAVL_META_STATION_URL);
    s.source_id = source_id;
    
    gavl_dictionary_get_src(m, GAVL_META_SRC, 0, NULL, &s.stream_uri);

    import_m3u_array(&s.categories, m, GAVL_META_CATEGORY);
    import_m3u_array(&s.languages, m, GAVL_META_AUDIO_LANGUAGES);
    import_m3u_array(&s.countries, m, GAVL_META_COUNTRY);
    add_station(b, &s);
    station_reset(&s);
    stations_added++;
    }
  
  gavl_dictionary_destroy(mi);

  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Imported %d stations from %s", stations_added, uri);

  }

static void import_m3u(bg_mdb_backend_t * b)
  {
  import_m3u_sub(b, "https://iptv-org.github.io/iptv/index.m3u", "iptv-org");
  import_m3u_sub(b, "http://bit.ly/kn-kodi-tv", "Kodinerds IPTV");
  import_m3u_sub(b, "http://bit.ly/kn-kodi-radio", "Kodinerds Radio");
  }
