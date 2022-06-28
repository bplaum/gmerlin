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
  
  stations( DBID, LABEL, LOGO_URI, HOMEPAGE_URI, STREAM_URI, MIMETYPE, TYPE, SOURCE_ID )

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
  const char * mimetype;
  
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
  int64_t next_id;
  
  gavl_dictionary_t * root_container;

  gavl_array_t sources;
  int new_file;
  
  } streams_t;

static struct
  {
  int mode;
  const char * table;
  const char * id;
  const char * label;
  }
browse_tables[] =
  {
    { BROWSE_BY_TAG,      "tags",       "tag"      },
    { BROWSE_BY_LANGUAGE, "languages",  "language" },
    { BROWSE_BY_CATEGORY, "categories", "category" },
    { BROWSE_BY_COUNTRY,   "countries", "country"  },
    { BROWSE_ALL,          NULL,        "all"      },
    { /* End */ },
  };

static void init_sources(bg_mdb_backend_t * b);

static int query_children(bg_mdb_backend_t * be,
                          bg_sqlite_id_tab_t * tab,
                          int browse_mode,
                          int64_t source_id,
                          int64_t attribute,
                          const char * group);



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

static const char * get_browse_mode_id(int browse_mode)
  {
  int i = 0;
  while(browse_tables[i].id)
    {
    if(browse_tables[i].mode == browse_mode)
      return browse_tables[i].id;
    i++;
    }
  return NULL;
  }

static int get_browse_mode_by_id(const char * id)
  {
  int i = 0;
  while(browse_tables[i].id)
    {
    if(!strcmp(browse_tables[i].id, id))
      return browse_tables[i].mode;
    i++;
    }
  return -1;
  }

#if 0
static int get_browse_mode_info(const char * id, int * mode, const char ** table, const char ** label)
  {
  int i = 0;
  while(browse_tables[i].id)
    {
    if(!strcmp(browse_tables[i].id, id))
      {
      *mode = browse_tables[i].mode;
      *table = browse_tables[i].table;
      
      return 1;
      }
    i++;
    }
  
  }
#endif

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
static int import_radiobrowser(bg_mdb_backend_t * b, int64_t source_id);

// static void import_m3u(bg_mdb_backend_t * b);

static int import_source(bg_mdb_backend_t * b, int64_t id, const char * uri);
static int import_m3u(bg_mdb_backend_t * b, int64_t id, const char * uri);
    

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
  int64_t mimetype_id;
  } browse_station_t;

static int
browse_station_callback(void * data, int argc, char **argv, char **azColName)
  {
  int i;
  browse_station_t * b = data;
  b->mimetype_id = -1;
  
  for(i = 0; i < argc; i++)
    {
    if(!strcmp(azColName[i], META_DB_ID))
      {
      
      }
    else if(!strcmp(azColName[i], "MIMETYPE_ID"))
      {
      b->mimetype_id = strtoll(argv[i], NULL, 10);
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

  if(bs.mimetype_id >= 0)
    {
    gavl_dictionary_t * src =  gavl_dictionary_get_src_nc(bs.m, GAVL_META_SRC, 0);
    gavl_dictionary_set_string_nocopy(src, GAVL_META_MIMETYPE,
                                      bg_sqlite_id_to_string(p->db,
                                                             "mimetypes",
                                                             "NAME",
                                                             "ID",
                                                             bs.mimetype_id));
    }
  
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
  int64_t mimetype_id = -1;
  char * name = gavl_strdup(s->name);
  streams_t * p = b->priv;

  name = gavl_strip_space(name);
  
  s->id = ++p->next_id;
  
  if(!s->mimetype)
    {
    char *pos;
    char *uri = gavl_strdup(s->stream_uri);
    if((pos = strrchr(uri, '?')))
      *pos = '\0';

    if((pos = strchr(uri, '#')))
      *pos = '\0';

    if((pos = strrchr(uri, '.')))
      {
      pos++;
      s->mimetype = bg_ext_to_mimetype(pos);
      }
    free(uri);
    }
  
  if(s->mimetype)
    {
    mimetype_id = bg_sqlite_string_to_id_add(p->db,
                                             "mimetypes",
                                             "ID",
                                             "NAME",
                                             s->mimetype);
    }
  
  if(strchr(s->name, '\n'))
    {
    }
  
  sql = sqlite3_mprintf("INSERT INTO stations ("META_DB_ID", "GAVL_META_LABEL", "GAVL_META_SEARCH_TITLE", "GAVL_META_URI", "GAVL_META_STATION_URL", "GAVL_META_LOGO_URL", TYPE, SOURCE_ID, MIMETYPE_ID) "
                        "VALUES"
                        " (%"PRId64", %Q, %Q, %Q, %Q, %Q, %d, %"PRId64", %"PRId64"); ",
                        s->id, name, get_search_name(name), s->stream_uri, s->station_uri, s->logo_uri, s->type, s->source_id, mimetype_id);
  
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

static void add_source(bg_mdb_backend_t * b, const char * label, const char * uri, int64_t * source_id)
  {
  char * sql;
  int result;

  streams_t * p = b->priv;
  *source_id = bg_sqlite_get_max_int(p->db, "sources", META_DB_ID) + 1;
  
  sql = sqlite3_mprintf("INSERT INTO sources ("META_DB_ID", "GAVL_META_LABEL", "GAVL_META_URI") "
                        "VALUES"
                        " (%"PRId64", %Q, %Q);",
                        *source_id, label, uri);
  
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

static void broadcast_root_folder(bg_mdb_backend_t * be)
  {
  gavl_msg_t * evt;
  streams_t * p = be->priv;

  evt = bg_msg_sink_get(be->ctrl.evt_sink);
  gavl_msg_set_id_ns(evt, BG_MSG_DB_OBJECT_CHANGED, BG_MSG_NS_DB);
  
  gavl_dictionary_set_string(&evt->header, GAVL_MSG_CONTEXT_ID, BG_MDB_ID_STREAMS);
  
  gavl_msg_set_arg_dictionary(evt, 0, p->root_container);
  bg_msg_sink_put(be->ctrl.evt_sink, evt);
  
  }

static void rescan(bg_mdb_backend_t * be)
  {
  int i;
  bg_sqlite_id_tab_t tab;
  streams_t * p = be->priv;

  if(p->new_file)
    {
    p->new_file = 0;
    //    goto end;
    return;
    }
  
  bg_mdb_track_lock(be, 1, p->root_container);
  
  bg_sqlite_start_transaction(p->db);
  
  /* We re-build the whole database */

  //  bg_sqlite_exec(p->db, "DELETE FROM sources", NULL, NULL);
  
  bg_sqlite_exec(p->db, "DELETE FROM stations", NULL, NULL);
  bg_sqlite_exec(p->db, "DELETE FROM categories", NULL, NULL);
  bg_sqlite_exec(p->db, "DELETE FROM station_categories", NULL, NULL);

  bg_sqlite_exec(p->db, "DELETE FROM languages", NULL, NULL);
  bg_sqlite_exec(p->db, "DELETE FROM station_languages", NULL, NULL);

  bg_sqlite_exec(p->db, "DELETE FROM countries", NULL, NULL);
  bg_sqlite_exec(p->db, "DELETE FROM station_countries", NULL, NULL);

  bg_sqlite_exec(p->db, "DELETE FROM tags", NULL, NULL);
  bg_sqlite_exec(p->db, "DELETE FROM station_tags", NULL, NULL);

  bg_sqlite_id_tab_init(&tab);
  bg_sqlite_exec(p->db, "SELECT "META_DB_ID" FROM sources", bg_sqlite_append_id_callback, &tab);

  for(i = 0; i < tab.num_val; i++)
    {
    char * uri = bg_sqlite_id_to_string(p->db,
                                        "sources",
                                        GAVL_META_URI,
                                        META_DB_ID,
                                        tab.val[i]);
    if(uri)
      {
      import_source(be, tab.val[i], uri);
      free(uri);
      }
    }

  bg_sqlite_id_tab_free(&tab);
  
  //  import_icecast(be);
  //  import_radiobrowser(be);

  bg_sqlite_end_transaction(p->db);

  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Finished rescannig streams");
  bg_mdb_track_lock(be, 0, p->root_container);

  /* Update root folder */
  init_sources(be);
  broadcast_root_folder(be);
  
  //  end:
  
  
  }

static int import_source(bg_mdb_backend_t * be, int64_t id, const char * uri)
  {
  if(!strcmp(uri, "radiobrowser://"))
    {
    return import_radiobrowser(be, id);
    }
  else if(gavl_string_starts_with_i(uri, "http://") ||
          gavl_string_starts_with_i(uri, "https://") ||
          gavl_string_starts_with(uri, "/"))
    {
    return import_m3u(be, id, uri);
    }
  else
    return 0;
  }

static void create_tables(bg_mdb_backend_t * be)
  {
  streams_t * priv = be->priv;

  /* Object table */  
  if(!bg_sqlite_exec(priv->db,
                     "CREATE TABLE IF NOT EXISTS sources("META_DB_ID" INTEGER PRIMARY KEY, "
                     GAVL_META_LABEL" TEXT, "GAVL_META_URI" TEXT);",
                     NULL, NULL) ||
     !bg_sqlite_exec(priv->db,
                     "CREATE TABLE IF NOT EXISTS stations("META_DB_ID" INTEGER PRIMARY KEY, "
                     GAVL_META_LABEL" TEXT, "GAVL_META_SEARCH_TITLE" TEXT, MIMETYPE_ID INTEGER, "GAVL_META_URI" TEXT, "GAVL_META_STATION_URL" TEXT, "GAVL_META_LOGO_URL" TEXT,"
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
                     "CREATE TABLE IF NOT EXISTS station_languages(ID INTEGER PRIMARY KEY, ATTR_ID INTEGER, STATION_ID INTEGER, SOURCE_ID INTEGER);", NULL, NULL) ||
     !bg_sqlite_exec(priv->db,
                     "CREATE TABLE IF NOT EXISTS mimetypes(ID INTEGER PRIMARY KEY, NAME TEXT);", NULL, NULL)
     )
    return;
  
  }


static int get_source_callback(void * data, int argc, char **argv, char **azColName)
  {
  int i;
  gavl_value_t val;
  //  gavl_value_t browse_modes_val;
  //  gavl_value_t browse_mode_val;
  gavl_dictionary_t * dict;
  gavl_dictionary_t * m;
  gavl_dictionary_t * mdb;
  //  gavl_array_t * browse_modes;
  
  int64_t id;
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
      gavl_dictionary_set_long(mdb, META_DB_ID, id);
      }
    else if(!strcmp(azColName[i], GAVL_META_LABEL))
      {
      gavl_dictionary_set_string(m, GAVL_META_LABEL, argv[i]);
      }
    else if(!strcmp(azColName[i], GAVL_META_URI))
      {
      gavl_dictionary_set_string(mdb, GAVL_META_URI, argv[i]);
      }
    }
  
  gavl_dictionary_set_string(m, GAVL_META_MEDIA_CLASS, GAVL_META_MEDIA_CLASS_CONTAINER);

  //  get_source_children(s, &browse_mode, &num_containers, &num_items);

  gavl_dictionary_set_string_nocopy(m, GAVL_META_ID, gavl_sprintf("%s/%"PRId64, BG_MDB_ID_STREAMS, id));

  
  gavl_array_splice_val_nocopy(&p->sources, -1, 0, &val);
  return 0;
  }

static int browse_source_child(bg_mdb_backend_t * be, int browse_mode, int64_t source_id, gavl_dictionary_t * dict)
  {
  const char * klass = NULL;
  const char * child_class = NULL;
  const char * label = NULL;
  const char * attr_table = NULL;
  streams_t * priv;
  int num_items;
  int num_containers;
  char * sql;
  
  priv = be->priv;
  
  num_items = 0;
  num_containers = 0;
  
  attr_table = get_browse_table(browse_mode);
  
  switch(browse_mode)
    {
    case BROWSE_ALL:
      {
      //      id = "all";
      label = "All";
      klass = GAVL_META_MEDIA_CLASS_CONTAINER;

      num_items = get_source_count(priv, source_id, "num_stations");
      child_class = GAVL_META_MEDIA_CLASS_LOCATION;
      
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
      //      id = "tag";
      label = "Tags";
      klass = GAVL_META_MEDIA_CLASS_CONTAINER;

      num_containers = get_source_count(priv, source_id, "num_tags");
              
      child_class = GAVL_META_MEDIA_CLASS_CONTAINER_TAG;
            
      }
      break;
    case BROWSE_BY_COUNTRY:
      {
      //      id = "country";
      label = "Countries";
      klass = GAVL_META_MEDIA_CLASS_CONTAINER;
            
      num_containers = get_source_count(priv, source_id, "num_countries");
      child_class = GAVL_META_MEDIA_CLASS_CONTAINER_COUNTRY;
      }
      break;
    case BROWSE_BY_LANGUAGE:
      {
      //      id = "language";
      label = "Languages";
      klass = GAVL_META_MEDIA_CLASS_CONTAINER;
            
      num_containers = get_source_count(priv, source_id, "num_languages");
      child_class = GAVL_META_MEDIA_CLASS_CONTAINER_LANGUAGE;
      }
      break;
    case BROWSE_BY_CATEGORY:
      {
      //      id = "category";
      label = "Categories";
      klass = GAVL_META_MEDIA_CLASS_CONTAINER;
      child_class = GAVL_META_MEDIA_CLASS_CONTAINER;
      num_containers = get_source_count(priv, source_id, "num_categories");
      }
      break;
    }

  //  fprintf(stderr, "Bro
  
  if(attr_table && (num_containers > GROUP_THRESHOLD))
    {
    sql = gavl_sprintf("select exists(select 1 from station_%s inner join %s on station_%s.attr_id = %s.id where SOURCE_ID = %"PRId64" and NAME %%s);", attr_table, attr_table, attr_table, attr_table, source_id);
          
    num_containers = bg_sqlite_count_groups(priv->db, sql);
    free(sql);
    child_class = GAVL_META_MEDIA_CLASS_CONTAINER;
    }
        
  if(label)
    {
    gavl_dictionary_t * m;
            
    m = gavl_dictionary_get_dictionary_create(dict, GAVL_META_METADATA);
    gavl_dictionary_set_string(m, GAVL_META_LABEL, label);
    gavl_dictionary_set_string(m, GAVL_META_MEDIA_CLASS, klass);
    if(child_class)
      gavl_dictionary_set_string(m, GAVL_META_CHILD_CLASS, child_class);
    else if(!num_containers && num_items)
      gavl_dictionary_set_string(m, GAVL_META_CHILD_CLASS, GAVL_META_MEDIA_CLASS_LOCATION);
    
    gavl_track_set_num_children(dict, num_containers, num_items);
    return 1;
    }
  
  return 0;
  }

static int browse_object(bg_mdb_backend_t * be, const char * id, gavl_dictionary_t * ret,
                         int * idx, int * total)
  {
  const gavl_dictionary_t * source;
  char ** path;
  int64_t source_id;
  int64_t attr_id = -1;
  int path_idx = 1;
  streams_t * p = be->priv;
  int result = 0;
  int browse_mode = 0;
  gavl_array_t siblings;
  bg_sqlite_id_tab_t sibling_ids;
  //  const char * group = NULL;
  gavl_dictionary_t * m = NULL;
  int i;
  int num_items = 0;
  int num_containers = 0;
  const gavl_dictionary_t * mdb;
  const gavl_array_t * browse_modes;
  char * sql;
  const char * group = NULL;
  
  gavl_array_init(&siblings);
  bg_sqlite_id_tab_init(&sibling_ids);
  
  
  
  //  fprintf(stderr, "Streams: browse_object %s\n", id);
  
  path = gavl_strbreak(id+1, '/');
  
  if(!path[path_idx])
    goto end;
  
  source_id = strtoll(path[path_idx], NULL, 10);

  if(!(source = get_source_by_id(p, source_id)))
    {
    fprintf(stderr, "Got no source for ID %"PRId64"\n", source_id);
    goto end;
    }

  if(!(mdb = gavl_dictionary_get_dictionary(source, BG_MDB_DICT)) ||
     !(browse_modes = gavl_dictionary_get_array(mdb, "browse_modes")))
    {
    fprintf(stderr, "Got no browse modes for source ID %"PRId64"\n", source_id);
    goto end;
    }
  
  path_idx++;

  if(!path[path_idx])
    {
    /* /streams/1 */
    gavl_dictionary_copy(ret, source);

    for(i = 0; i < p->sources.num_entries; i++)
      {
      gavl_string_array_add(&siblings,
                            gavl_track_get_id(gavl_value_get_dictionary(&p->sources.entries[i])));
      }
    
    result = 1;
    goto end;
    }
  
  browse_mode = get_browse_mode_by_id(path[path_idx]);
  m = gavl_dictionary_get_dictionary_create(ret, GAVL_META_METADATA);
  
  if(browse_mode < 0)
    {
    /* All */

    if(gavl_string_starts_with(path[path_idx], BG_MDB_GROUP_PREFIX))
      {
      group = path[path_idx];
      path_idx++;

      if(!path[path_idx])
        {
        sql = gavl_sprintf("select count("META_DB_ID") from stations where SOURCE_ID = %"PRId64" and "GAVL_META_SEARCH_TITLE" %%s;",
                           source_id);
        
        if(bg_sqlite_set_group_container(p->db, ret, id, sql, GAVL_META_MEDIA_CLASS_LOCATION, idx, total))
          result = 1;
        if(sql)
          free(sql);
        goto end;
        }
      }

    
    /* /streams/1/1039 */
    /* /streams/1/~group~A/1039 */
    
    }
  
  path_idx++;

  
  if(!path[path_idx])
    {
    char * parent_id;
    int mode;
    /* /streams/1/tag */
    
    if(!browse_source_child(be, browse_mode, source_id, ret))
      goto end;
    
    result = 1;
    
    parent_id = bg_mdb_get_parent_id(id);

    for(i = 0; i < browse_modes->num_entries; i++)
      {
      gavl_value_get_int(&browse_modes->entries[i], &mode);

      gavl_string_array_add_nocopy(&siblings,
                                   gavl_sprintf("%s/%s", parent_id, get_browse_mode_id(mode)));
      }
    free(parent_id);
    goto end;
    }

  /* /streams/1/tags */
  
  if(gavl_string_starts_with(path[path_idx], BG_MDB_GROUP_PREFIX))
    {
    group = path[path_idx];
    /* /streams/1/tag/~group~A */
    //    group = path[path_idx];
    path_idx++;

    if(!path[path_idx])
      {
      const char * child_class = NULL;
      /* /streams/1/tag/~group~A */

      switch(browse_mode)
        {
        case BROWSE_ALL:
          child_class = GAVL_META_MEDIA_CLASS_LOCATION;
          sql = gavl_sprintf("select count("META_DB_ID") from stations where SOURCE_ID = %"PRId64" and "GAVL_META_SEARCH_TITLE" %%s;", source_id);
          break;
        case BROWSE_BY_TAG:
          child_class = GAVL_META_MEDIA_CLASS_CONTAINER_TAG;

          sql = gavl_sprintf("select count(distinct tags.id) from tags inner join station_tags on tags.ID = station_tags.ATTR_ID where SOURCE_ID = %"PRId64" and NAME %%s;", source_id);

          break;
        case BROWSE_BY_COUNTRY:
          child_class = GAVL_META_MEDIA_CLASS_CONTAINER_COUNTRY;
          
          sql = gavl_sprintf("select count(distinct countries.id) from countries inner join station_countries on countries.ID = station_countries.ATTR_ID where SOURCE_ID = %"PRId64" and NAME %%s;", source_id);
          
          break;
        case BROWSE_BY_LANGUAGE:
          child_class = GAVL_META_MEDIA_CLASS_CONTAINER_LANGUAGE;
          sql = gavl_sprintf("select count(distinct languages.id) from languages inner join station_languages on languages.ID = station_languages.ATTR_ID where SOURCE_ID = %"PRId64" and NAME %%s;", source_id);
          
          break;
        case BROWSE_BY_CATEGORY:
          child_class = GAVL_META_MEDIA_CLASS_CONTAINER;
          sql = gavl_sprintf("select count(distinct categories.id) from categories inner join station_categories on categories.ID = station_categories.ATTR_ID where SOURCE_ID = %"PRId64" and NAME %%s;", source_id);
          break;
        }

      /* Next, previous, idx, total */

      if(bg_sqlite_set_group_container(p->db, ret, id, sql, child_class, idx, total))
        result = 1;
      if(sql)
        free(sql);
      goto end;
      }
    //    else
    //      group = NULL;
    }
  
  /* /streams/1/tags/[~group~A/]1 */

  attr_id = strtoll(path[path_idx], NULL, 10);

  path_idx++;

  if(!path[path_idx])
    {
    char * label;
    const char * attr_table = NULL;
    // /streams/1/tags/[~group~A/]1
    
    switch(browse_mode)
      {
#if 0 // TODO: Handle leafs
      case BROWSE_ALL:
        gavl_dictionary_set_string(m, GAVL_META_MEDIA_CLASS, GAVL_META_MEDIA_CLASS_LOCATION);
        sql = gavl_sprintf("select count(id) from stations where source_id = %"PRId64";",
                           source_id, attr_id);
        num_items = bg_sqlite_get_int(p->db, sql);
        free(sql);
        break;
#endif
      case BROWSE_BY_TAG:
        gavl_dictionary_set_string(m, GAVL_META_MEDIA_CLASS, GAVL_META_MEDIA_CLASS_CONTAINER_TAG);
        attr_table = "tags";
        
        /* Children */
                           
        sql = gavl_sprintf("select count(id) from station_tags where source_id = %"PRId64" and attr_id = %"PRId64";",
                           source_id, attr_id);
        num_items = bg_sqlite_get_int(p->db, sql);
        free(sql);

        break;
      case BROWSE_BY_COUNTRY:
        attr_table = "countries";

        gavl_dictionary_set_string(m, GAVL_META_MEDIA_CLASS, GAVL_META_MEDIA_CLASS_CONTAINER_COUNTRY);

        sql = gavl_sprintf("select count(id) from station_countries where source_id = %"PRId64" and attr_id = %"PRId64";",
                           source_id, attr_id);
        num_items = bg_sqlite_get_int(p->db, sql);
        free(sql);


        break;
      case BROWSE_BY_LANGUAGE:
        attr_table = "languages";

        gavl_dictionary_set_string(m, GAVL_META_MEDIA_CLASS, GAVL_META_MEDIA_CLASS_CONTAINER_LANGUAGE);

        sql = gavl_sprintf("select count(id) from station_languages where source_id = %"PRId64" and attr_id = %"PRId64";",
                           source_id, attr_id);
        num_items = bg_sqlite_get_int(p->db, sql);
        free(sql);

        break;
      case BROWSE_BY_CATEGORY:
        attr_table = "categories";
        
        gavl_dictionary_set_string(m, GAVL_META_MEDIA_CLASS, GAVL_META_MEDIA_CLASS_CONTAINER);

        sql = gavl_sprintf("select count(id) from station_categories where source_id = %"PRId64" and attr_id = %"PRId64";",
                           source_id, attr_id);
        num_items = bg_sqlite_get_int(p->db, sql);
        free(sql);

        break;
      }
    
    if(num_items > GROUP_THRESHOLD)
      {
      num_items = 0;
      sql = gavl_sprintf("select exists(select 1 from station_%s inner join %s on station_%s.attr_id = %s.id where SOURCE_ID = %"PRId64" and NAME %%s);", attr_table, attr_table, attr_table, attr_table, source_id);
      
      num_containers = bg_sqlite_count_groups(p->db, sql);
      free(sql);
      
      //          num_containers = 
      gavl_dictionary_set_string(m, GAVL_META_CHILD_CLASS, GAVL_META_MEDIA_CLASS_CONTAINER);
      }
    else if(num_items > 0)
      {
      gavl_dictionary_set_string(m, GAVL_META_CHILD_CLASS, GAVL_META_MEDIA_CLASS_LOCATION);
      }

    if(attr_table && (label = bg_sqlite_id_to_string(p->db,
                                                     attr_table,
                                                     "NAME",
                                                     "ID",
                                                     attr_id)))
      {
      if(group)
        {
        char * cond;
        cond = bg_sqlite_make_group_condition(group);
        /* Siblings */
        sql = gavl_sprintf("select %s.id from station_%s inner join %s on ATTR_ID = %s.ID where SOURCE_ID = %"PRId64" and NAME %s ORDER by NAME COLLATE strcoll;", attr_table, attr_table, attr_table, attr_table, source_id, cond);
        free(cond);
        }
      else
        sql = gavl_sprintf("select %s.id from station_%s inner join %s on ATTR_ID = %s.ID where SOURCE_ID = %"PRId64" ORDER by NAME COLLATE strcoll;", attr_table, attr_table, attr_table, attr_table, source_id);

      bg_sqlite_exec(p->db,                              /* An open database */
                     sql,
                     bg_sqlite_append_id_callback,  /* Callback function */
                     &sibling_ids);                               /* 1st argument to callback */
      
      free(sql);
      
      gavl_dictionary_set_string_nocopy(m, GAVL_META_LABEL, label);
      
      
      


      }
    result = 1;
    goto end;
    }

  group = NULL;
  
  if(gavl_string_starts_with(path[path_idx], BG_MDB_GROUP_PREFIX))
    {
    //    group = path[path_idx];
    path_idx++;

    /* ..../~group~A */
    if(!path[path_idx])
      {
      const char  * attr_table = NULL;
      
      sql = NULL;
      
      switch(browse_mode)
        {
        case BROWSE_ALL:
          sql = gavl_sprintf("select count(id) from stations where SOURCE_ID = %"PRId64" and "GAVL_META_SEARCH_TITLE" %%s;",
                             source_id);
          break;
        case BROWSE_BY_TAG:
          attr_table = "tags";
          break;
        case BROWSE_BY_COUNTRY:
          attr_table = "countries";
          break;
        case BROWSE_BY_LANGUAGE:
          attr_table = "languages";
          break;
        case BROWSE_BY_CATEGORY:
          attr_table = "categories";
          break;
        }

      if(attr_table)
        {
        sql = gavl_sprintf("select count(stations."META_DB_ID") from station_%s inner join stations on station_%s.STATION_ID = stations."META_DB_ID" where stations.SOURCE_ID = %"PRId64" and ATTR_ID = %"PRId64" and "GAVL_META_SEARCH_TITLE" %%s;", attr_table, attr_table, source_id, attr_id);
        }
      
      if(sql && bg_sqlite_set_group_container(p->db, ret, id, sql,
                                              GAVL_META_MEDIA_CLASS_LOCATION, idx, total))
        result = 1;
      
      if(sql)
        free(sql);
      goto end;
      }
    
    }

  
  
  /* TODO: IDX + total, next, previous */
  
  end:

  
  if(result)
    {

    if(!siblings.num_entries && sibling_ids.num_val)
      {
      char * parent_id = bg_mdb_get_parent_id(id);
      
      for(i = 0; i < sibling_ids.num_val; i++)
        gavl_string_array_add_nocopy(&siblings, gavl_sprintf("%s/%"PRId64, parent_id, sibling_ids.val[i]));
      }

    if(siblings.num_entries)
      {
      if(!m)
        m = gavl_track_get_metadata_nc(ret);
      
      *idx = gavl_string_array_indexof(&siblings, id);
      *total = siblings.num_entries;

      if(*idx >= 0)
        {
        if(*idx > 0)
          gavl_dictionary_set_string(m, GAVL_META_PREVIOUS_ID, gavl_string_array_get(&siblings, (*idx) - 1));
        if(*idx < siblings.num_entries-1)
          gavl_dictionary_set_string(m, GAVL_META_NEXT_ID, gavl_string_array_get(&siblings, (*idx) + 1));
        }
      
      }
    
    gavl_track_set_id(ret, id);
    }

  if(num_containers || num_items)
    {
    gavl_track_set_num_children(ret, num_containers, num_items);

    if(!num_containers)
      gavl_dictionary_set_string(m, GAVL_META_CHILD_CLASS, GAVL_META_MEDIA_CLASS_LOCATION);
    }
  gavl_array_free(&siblings);
  bg_sqlite_id_tab_free(&sibling_ids);
  
  gavl_strbreak_free(path);
  
  return result;
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
    gavl_dictionary_t * m;
    
    gavl_value_init(&val);
    dict = gavl_value_set_dictionary(&val);
    
    if(!browse_station(be, dict, tab.val[i+start]))
      goto fail;

    m = gavl_track_get_metadata_nc(dict);
    
    if(i+start > 0)
      {
      gavl_dictionary_set_string_nocopy(m, GAVL_META_PREVIOUS_ID,
                                        gavl_sprintf("%s/%"PRId64, parent_id,
                                                     tab.val[i+start-1]));
      }
    if(i < tab.num_val - 1)
      {
      gavl_dictionary_set_string_nocopy(m, GAVL_META_NEXT_ID,
                                        gavl_sprintf("%s/%"PRId64, parent_id,
                                                     tab.val[i+start+1]));
      }
    
    gavl_track_set_id_nocopy(dict, gavl_sprintf("%s/%"PRId64, parent_id, tab.val[i+start]));
    
    gavl_array_splice_val_nocopy(&arr, -1, 0, &val);

    /* Send partial answers */
    
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

  int idx;
  int start;
  int num;

  char * last_id;
  gavl_dictionary_t * last_m;
  
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

  if((b->start > 0) && (b->idx < b->start))
    {
    if(b->idx == b->start-1)
      {
      for(i = 0; i < argc; i++)
        {
        if(!strcmp(azColName[i], "ID"))
          {
          b->last_id = gavl_sprintf("%s/%s", b->parent_id, argv[i]);
          }
        }
      }
    b->idx++;
    return 0;
    }
  
  if((b->num > 0) && (b->idx + b->start >= b->num))
    {
    if(b->last_m)
      {
      for(i = 0; i < argc; i++)
        {
        if(!strcmp(azColName[i], "ID"))
          {
          gavl_dictionary_set_string_nocopy(b->last_m, GAVL_META_NEXT_ID, gavl_sprintf("%s/%s", b->parent_id, argv[i]));
          b->last_m = NULL;
          }
        }
      }
    b->idx++;
    return 0;
    }
  
  
  gavl_value_init(&val);
  dict = gavl_value_set_dictionary(&val);
  m = gavl_dictionary_get_dictionary_create(dict, GAVL_META_METADATA);
  
  gavl_dictionary_copy(m, &b->m_tmpl);

  if(b->last_id)
    {
    gavl_dictionary_set_string_nocopy(m, GAVL_META_PREVIOUS_ID, b->last_id);
    b->last_id = NULL;
    }
  
  for(i = 0; i < argc; i++)
    {
    if(!strcmp(azColName[i], "ID"))
      {
      b->last_id = gavl_sprintf("%s/%s", b->parent_id, argv[i]);

      gavl_track_set_id(dict, b->last_id);

      id = strtoll(argv[i], NULL, 10);

      if(b->last_m)
        {
        gavl_dictionary_set_string(b->last_m, GAVL_META_NEXT_ID, b->last_id);
        b->last_m = NULL;
        }
      }
    else if(!strcmp(azColName[i], "NAME"))
      gavl_dictionary_set_string(m, GAVL_META_LABEL, argv[i]);
    }

  sql = gavl_sprintf("select count(id) from station_%s where source_id = %"PRId64" and attr_id = %"PRId64";", b->attr_table, b->source_id, id);
  num_children = bg_sqlite_get_int(b->priv->db, sql);
  free(sql);
  
  if(num_children > GROUP_THRESHOLD)
    {
    sql = gavl_sprintf("select exists(select 1 from stations inner join station_%s on stations."META_DB_ID" = station_%s.station_id where stations.SOURCE_ID = %"PRId64" AND ATTR_ID = %"PRId64" AND "GAVL_META_SEARCH_TITLE" %%s);", b->attr_table, b->attr_table, b->source_id, id);
    
    num_children = bg_sqlite_count_groups(b->priv->db, sql);
    free(sql);
    gavl_track_set_num_children(dict, num_children, 0);
    }
  else 
    {
    gavl_track_set_num_children(dict, 0, num_children);

    gavl_dictionary_set_string(m, GAVL_META_CHILD_CLASS, GAVL_META_MEDIA_CLASS_LOCATION);

    }
  gavl_array_splice_val_nocopy(b->ret, -1, 0, &val);

  b->last_m = m;
  
  
  b->idx++;
  
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

  b.idx = 0;
  b.start = start;
  b.num = num;

  b.last_m = NULL;
  b.last_id = NULL;
  
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
  
  // fprintf(stderr, "Streams: browse_children %s\n", ctx_id);
  
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
        const char * id;
        gavl_value_t val;
        gavl_dictionary_t * dict;
        gavl_dictionary_t * m;
        
        num_items = 0;
        num_containers = 0;
        
        if(!gavl_value_get_int(&browse_modes->entries[i+start], &mode))
          continue;

        
        
        id = get_browse_mode_id(mode);
        
        attr_table = get_browse_table(mode);

        gavl_value_init(&val);
        dict = gavl_value_set_dictionary(&val);
        
        if(browse_source_child(be, mode, source_id, dict))
          {
          gavl_track_set_id_nocopy(dict, gavl_sprintf("%s/%s", ctx_id_orig, id));
          gavl_array_splice_val_nocopy(&arr, -1, 0, &val);
          }
        else
          gavl_value_free(&val);

        m = gavl_track_get_metadata_nc(dict);
        
        if(i+start > 0)
          {
          if(gavl_value_get_int(&browse_modes->entries[i+start-1], &mode) &&
             (id = get_browse_mode_id(mode)))
            {
            gavl_dictionary_set_string_nocopy(m, GAVL_META_PREVIOUS_ID, gavl_sprintf("%s/%s", ctx_id_orig, id));
            }
          }
        
        if(i+start < browse_modes->num_entries-1)
          {
          if(gavl_value_get_int(&browse_modes->entries[i+start+1], &mode) &&
             (id = get_browse_mode_id(mode)))
            {
            gavl_dictionary_set_string_nocopy(m, GAVL_META_NEXT_ID, gavl_sprintf("%s/%s", ctx_id_orig, id));
            }
          }
        
        }
        
      }
    else
      {
      
      /* Make groups */
      if(get_source_count(priv, source_id, "num_stations") > GROUP_THRESHOLD)
        {
        gavl_dictionary_t m_tmpl;
        gavl_dictionary_init(&m_tmpl);
        gavl_dictionary_set_string(&m_tmpl, GAVL_META_CHILD_CLASS, GAVL_META_MEDIA_CLASS_LOCATION);
        
        sql = gavl_sprintf("select count("META_DB_ID") from stations where SOURCE_ID = %"PRId64" and "GAVL_META_SEARCH_TITLE" %%s",
                           source_id);
        
        bg_sqlite_add_groups(priv->db, &arr, ctx_id_orig, sql, &m_tmpl, start, num);
        gavl_dictionary_free(&m_tmpl);
        free(sql);
        }
      else
        browse_leafs(be, msg,
                     BROWSE_ALL,
                     source_id,
                     -1,
                     NULL,
                     ctx_id_orig,
                     start, num, one_answer);
      
      }
    goto end;
    }
  // /streams/1/language, /streams/1/country, /streams/1/category, /streams/1/tag 

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
          gavl_dictionary_set_string(&m_tmpl, GAVL_META_CHILD_CLASS, GAVL_META_MEDIA_CLASS_CONTAINER_LANGUAGE);
          
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

static int delete_source(bg_mdb_backend_t * be, int idx)
  {
  int64_t id = -1;
  const gavl_dictionary_t * src;
  const gavl_dictionary_t * mdb;
  char * sql;
  streams_t * s = be->priv;

  if((idx < 0) || (idx > s->sources.num_entries))
    return 0;

  if(!(src = gavl_value_get_dictionary(&s->sources.entries[idx])))
    return 0;

  if(!(mdb = gavl_dictionary_get_dictionary(src, BG_MDB_DICT)))
    return 0;
  
  if(!gavl_dictionary_get_long(mdb, META_DB_ID, &id))
    return 0;

  /* Delete from database */

  sql = gavl_sprintf("DELETE FROM sources WHERE "META_DB_ID" = %"PRId64";",
                     id);
  bg_sqlite_exec(s->db, sql, NULL, NULL);
  free(sql);

  sql = gavl_sprintf("DELETE FROM stations WHERE SOURCE_ID = %"PRId64";",
                     id);
  bg_sqlite_exec(s->db, sql, NULL, NULL);
  free(sql);

  
  sql = gavl_sprintf("DELETE FROM station_categories WHERE SOURCE_ID = %"PRId64";",
                     id);
  bg_sqlite_exec(s->db, sql, NULL, NULL);
  free(sql);

  sql = gavl_sprintf("DELETE FROM station_tags WHERE SOURCE_ID = %"PRId64";",
                     id);
  bg_sqlite_exec(s->db, sql, NULL, NULL);
  free(sql);

  sql = gavl_sprintf("DELETE FROM station_languages WHERE SOURCE_ID = %"PRId64";",
                     id);
  bg_sqlite_exec(s->db, sql, NULL, NULL);
  free(sql);

  sql = gavl_sprintf("DELETE FROM station_countries WHERE SOURCE_ID = %"PRId64";",
                     id);
  bg_sqlite_exec(s->db, sql, NULL, NULL);
  free(sql);

  gavl_array_splice_val(&s->sources, idx, 1, NULL);
  
  return 1;
  }

static int add_source_dict(bg_mdb_backend_t * be, const gavl_dictionary_t * dict)
  {
  const char * uri = NULL;
  const char * label = NULL;
  int64_t source_id = -1;
  streams_t * s = be->priv;

  if(!(dict = gavl_track_get_metadata(dict)) ||
     !gavl_dictionary_get_src(dict, GAVL_META_SRC, 0,
                              NULL, &uri) ||
     !(label = gavl_dictionary_get_string(dict, GAVL_META_LABEL)))
    return 0;

  /* Check if the source was already added */
  if(bg_sqlite_string_to_id(s->db,
                            "sources",
                            META_DB_ID,
                            GAVL_META_URI,
                            uri) > 0)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Stream source %s is already in the database", uri);
    return 0;
    }

  add_source(be, label, uri, &source_id);
  return import_source(be, source_id, uri);
  }

static int add_source_str(bg_mdb_backend_t * be, const char * label, const char * uri)
  {
  int ret;
  gavl_dictionary_t dict;
  gavl_dictionary_t * m;

  gavl_dictionary_init(&dict);
  m = gavl_dictionary_get_dictionary_create(&dict, GAVL_META_METADATA);

  gavl_dictionary_set_string(m, GAVL_META_LABEL, label);
  gavl_metadata_add_src(m, GAVL_META_SRC, NULL, uri);

  ret = add_source_dict(be, &dict);
  gavl_dictionary_free(&dict);
  return ret;
  }


static int handle_msg(void * priv, gavl_msg_t * msg)
  {
  bg_mdb_backend_t * be = priv;
  streams_t * s = be->priv;
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
          gavl_msg_t * res;
          rescan(be);
          /* Send done event */
          
          res = bg_msg_sink_get(be->ctrl.evt_sink);
          gavl_msg_set_id_ns(res, BG_MSG_DB_RESCAN_DONE, BG_MSG_NS_DB);
          bg_msg_sink_put(be->ctrl.evt_sink, res);

          }
          break;
        case BG_CMD_DB_SPLICE_CHILDREN:
          {
          int i;
          int last = 0;
          int idx = 0;
          int del = 0;
          gavl_value_t add;
          const char * ctx_id;
          int num_added = 0;
          int old_num = s->sources.num_entries;
          
          ctx_id = gavl_dictionary_get_string(&msg->header, GAVL_MSG_CONTEXT_ID);

          if(strcmp(ctx_id, BG_MDB_ID_STREAMS))
            break;

          gavl_value_init(&add);
          gavl_msg_get_splice_children(msg, &last, &idx, &del, &add);

          //          fprintf(stderr, "Splice: %d %d %d\n", idx, del, last);
          //          gavl_value_dump(&add, 2);

          if(idx < 0)
            idx = s->sources.num_entries;

          if(del < 0)
            del = s->sources.num_entries - idx;

          if(del + idx >= s->sources.num_entries)
            del = s->sources.num_entries - idx;

          bg_sqlite_start_transaction(s->db);
          
          for(i = 0; i < del; i++)
            delete_source(be, idx);
          
          if(add.type == GAVL_TYPE_ARRAY)
            {
            gavl_dictionary_t * dict;
    //    const char * uri;

            gavl_array_t * add_arr = gavl_value_get_array_nc(&add);
            
            if(!add_arr->num_entries)
              {
              /* Nothing to add */
              }
            else if((dict = gavl_value_get_dictionary_nc(&add_arr->entries[0])))
              {
              for(i = 0; i < add_arr->num_entries; i++)
                {
                if((dict = gavl_value_get_dictionary_nc(&add_arr->entries[i])))
                  {
                  if(add_source_dict(be, dict))
                    num_added++;
                  }
                }
              }
            }
          else if(add.type == GAVL_TYPE_DICTIONARY)
            {
            gavl_dictionary_t * dict;
            dict = gavl_value_get_dictionary_nc(&add);

            if(add_source_dict(be, dict))
              num_added++;
            }
          if(del || num_added)
            {
            gavl_msg_t * res;
            
            init_sources(be);

            res = bg_msg_sink_get(be->ctrl.evt_sink);
            gavl_msg_set_id_ns(res, BG_MSG_DB_SPLICE_CHILDREN, BG_MSG_NS_DB);

            gavl_dictionary_set_string(&res->header,
                                       GAVL_MSG_CONTEXT_ID, BG_MDB_ID_STREAMS);           
            gavl_msg_set_last(res, 1);
            
            gavl_msg_set_arg_int(res, 0, 0); // idx
            gavl_msg_set_arg_int(res, 1, old_num); // del
            gavl_msg_set_arg_array(res, 2, &s->sources);
            bg_msg_sink_put(be->ctrl.evt_sink, res);
            broadcast_root_folder(be);
            }
          bg_sqlite_end_transaction(s->db);
          }
        }
      break;
      }
    }
  return 1;
  }

static void init_sources(bg_mdb_backend_t * b)
  {
  int result;
  char * sql;
  int i;
  streams_t * priv = b->priv;

  priv->next_id = bg_sqlite_get_max_int(priv->db, "stations", META_DB_ID);
  
  /* Initialize sources */
  gavl_array_reset(&priv->sources);
  
  if((result = bg_sqlite_get_int(priv->db, "SELECT count("META_DB_ID") from sources;" )) > 0)
    gavl_track_set_num_children(priv->root_container, result, 0);
  else
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Got no sources");
    return;
    }
  
  /* Get sources as containers */

  sql = sqlite3_mprintf("select * from SOURCES order by "GAVL_META_LABEL" COLLATE strcoll;");
  bg_sqlite_exec(priv->db, sql, get_source_callback, b);
  sqlite3_free(sql);

  /* next/previous, idx, total */

  for(i = 0; i < priv->sources.num_entries; i++)
    {
    int num_stations = 0;
    int num_tags = 0, num_languages = 0, num_categories = 0, num_countries = 0;
    const char * next_id;
    const char * prev_id;

    gavl_value_t browse_modes_val;
    gavl_value_t browse_mode_val;
    gavl_dictionary_t * m;
    gavl_dictionary_t * mdb;
    gavl_array_t * browse_modes;
    gavl_dictionary_t * dict;
    int num = 0;
    int num_items = 0;
    int num_containers = 0;
    int64_t id = -1;
    
    next_id = NULL;
    prev_id = NULL;
    
    if((i > 0) && (dict = gavl_value_get_dictionary_nc(&priv->sources.entries[i-1])))       
      prev_id = gavl_track_get_id(dict);

    if((i < priv->sources.num_entries-1) && (dict = gavl_value_get_dictionary_nc(&priv->sources.entries[i+1])))
      next_id = gavl_track_get_id(dict);
    
    if((dict = gavl_value_get_dictionary_nc(&priv->sources.entries[i])))
      {
      m = gavl_track_get_metadata_nc(dict);
      
      if(next_id)
        gavl_dictionary_set_string(m, GAVL_META_NEXT_ID, next_id);
      if(prev_id)
        gavl_dictionary_set_string(m, GAVL_META_PREVIOUS_ID, prev_id);
      
      mdb = gavl_dictionary_get_dictionary_nc(dict, BG_MDB_DICT);
      gavl_dictionary_get_long(mdb, META_DB_ID, &id);
      }
      
    /* Children */

    sql = sqlite3_mprintf("select count(distinct attr_id) from station_countries where SOURCE_ID = %"PRId64";", id);
    num_countries = bg_sqlite_get_int(priv->db, sql);
    sqlite3_free(sql);

    sql = sqlite3_mprintf("select count(distinct attr_id) from station_languages where SOURCE_ID = %"PRId64";", id);
    num_languages = bg_sqlite_get_int(priv->db, sql);
    sqlite3_free(sql);

    sql = sqlite3_mprintf("select count(distinct attr_id) from station_categories where SOURCE_ID = %"PRId64";", id);
    num_categories = bg_sqlite_get_int(priv->db, sql);
    sqlite3_free(sql);

    sql = sqlite3_mprintf("select count(distinct attr_id) from station_tags where SOURCE_ID = %"PRId64";", id);
    num_tags = bg_sqlite_get_int(priv->db, sql);
    sqlite3_free(sql);

    sql = sqlite3_mprintf("select count("META_DB_ID") from stations where SOURCE_ID = %"PRId64";", id);
    num_stations = bg_sqlite_get_int(priv->db, sql);
    sqlite3_free(sql);

    gavl_value_init(&browse_modes_val);
    gavl_value_init(&browse_mode_val);
    browse_modes = gavl_value_set_array(&browse_modes_val);
  
    if(num_tags > 1)
      {
      num++;
      gavl_value_set_int(&browse_mode_val, BROWSE_BY_TAG);
      gavl_array_splice_val_nocopy(browse_modes, -1, 0, &browse_mode_val);
      }

    if(num_categories > 1)
      {
      num++;
      gavl_value_set_int(&browse_mode_val, BROWSE_BY_CATEGORY);
      gavl_array_splice_val_nocopy(browse_modes, -1, 0, &browse_mode_val);
      }
  
    if(num_countries > 1)
      {
      num++;
      gavl_value_set_int(&browse_mode_val, BROWSE_BY_COUNTRY);
      gavl_array_splice_val_nocopy(browse_modes, -1, 0, &browse_mode_val);
      }
  
    if(num_languages > 1)
      {
      num++;
      gavl_value_set_int(&browse_mode_val, BROWSE_BY_LANGUAGE);
      gavl_array_splice_val_nocopy(browse_modes, -1, 0, &browse_mode_val);
      }

    gavl_value_set_int(&browse_mode_val, BROWSE_ALL);
    gavl_array_splice_val_nocopy(browse_modes, -1, 0, &browse_mode_val);
  
    if(!num)
      {
      num_items = num_stations;
    
      if(num_items > GROUP_THRESHOLD)
        {
        /* Make groups */
        num_items = 0;
        sql = gavl_sprintf("select exists(select 1 from stations where SOURCE_ID = %"PRId64" AND "GAVL_META_SEARCH_TITLE" %%s);", id);
        num_containers = bg_sqlite_count_groups(priv->db, sql);
        free(sql);
        gavl_dictionary_set_string(m, GAVL_META_CHILD_CLASS, GAVL_META_MEDIA_CLASS_CONTAINER);
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
    if(!num_containers && num_items)
      {
      gavl_dictionary_set_string(m, GAVL_META_CHILD_CLASS, GAVL_META_MEDIA_CLASS_LOCATION);
      }

    
    }
  
  }

static int ping_streams(bg_mdb_backend_t * be)
  {
  streams_t * priv = be->priv;
  //  rescan(b);

  bg_mdb_track_lock(be, 1, priv->root_container);

  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Creating default database");

  bg_sqlite_start_transaction(priv->db);
  
  add_source_str(be, "radio-browser.info", "radiobrowser://");
  add_source_str(be, "iptv-org",        "https://iptv-org.github.io/iptv/index.m3u");
  add_source_str(be, "Kodinerds IPTV",  "http://bit.ly/kn-kodi-tv");
  add_source_str(be, "Kodinerds Radio", "http://bit.ly/kn-kodi-radio");

  bg_sqlite_end_transaction(priv->db);

  init_sources(be);
  
  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Default database created");
  
  bg_mdb_track_lock(be, 0, priv->root_container);
  gavl_track_set_num_children(priv->root_container,
                              bg_sqlite_get_int(priv->db, "select count("META_DB_ID") from sources;"), 0);

  broadcast_root_folder(be);
  
  be->ping_func = NULL;
  return 1;
  }


void bg_mdb_create_streams(bg_mdb_backend_t * b)
  {
  streams_t * priv;
  //  gavl_dictionary_t * container;
  //  gavl_dictionary_t * child;
  //  gavl_dictionary_t * child_m;
  //  const gavl_dictionary_t * container_m;
  char * filename;

  priv = calloc(1, sizeof(*priv));
  b->priv = priv;

  b->flags |= BE_FLAG_RESCAN;
  
  filename = gavl_sprintf("%s/streams.sqlite", b->db->path);

  if(access(filename, R_OK))
    priv->new_file = 1;
  
  sqlite3_open(filename, &priv->db);
  
  bg_sqlite_init_strcoll(priv->db);
  
  create_tables(b);
  
  priv->root_container =
    bg_mdb_get_root_container(b->db, GAVL_META_MEDIA_CLASS_ROOT_STREAMS);

  bg_mdb_set_editable(priv->root_container);
  //  bg_mdb_add_can_add(priv->root_container, GAVL_META_MEDIA_CLASS_LOCATION);
  
  bg_mdb_container_set_backend(priv->root_container, MDB_BACKEND_STREAMS);
  
  if(!priv->new_file)
    init_sources(b);
  
  bg_controllable_init(&b->ctrl,
                       bg_msg_sink_create(handle_msg, b, 0),
                       bg_msg_hub_create(1));
  
  b->destroy = destroy_streams;

  if(priv->new_file)
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
    s.mimetype = "application/mpegurl";
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

static int import_radiobrowser(bg_mdb_backend_t * b, int64_t source_id)
  {
  int start = 0;
  int stations_added = 0;
  int result;

  gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Importing streams from radio-browser.info");
  
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

static int import_m3u(bg_mdb_backend_t * b, int64_t source_id, const char * uri)
  {
  int i, num;
  station_t s;
  const gavl_dictionary_t * dict;
  const gavl_dictionary_t * m;
  
  int stations_added = 0;
  gavl_dictionary_t * mi = bg_plugin_registry_load_media_info(bg_plugin_reg, uri, 0);

  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Importing stations from %s", uri);
  
  if(!mi)
    return 0;
  
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

  return stations_added;
  }

#if 0
static void import_m3u(bg_mdb_backend_t * b)
  {
  import_m3u_sub(b, "https://iptv-org.github.io/iptv/index.m3u", "iptv-org");
  import_m3u_sub(b, "http://bit.ly/kn-kodi-tv", "Kodinerds IPTV");
  import_m3u_sub(b, "http://bit.ly/kn-kodi-radio", "Kodinerds Radio");
  }
#endif
