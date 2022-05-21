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


// #include <unistd.h>
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
 * If we have a single source:
 *
 * /streams/radio-browser.info
 *   /by-tag
 *     /movies
 *       Blabla
 *   /by-country
 *     /de
 *       BlaBla
 *   /by-language
 *     /German
 *       BlaBla
 *
 * Multiple sources
 *
 * /streams/iptv-1
 * ...
 *
 * /streams/iptv-2
 * ..
 *
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

/*
  tables:

  sources( ID, LABEL, NUM_LANGUAGES, NUM_TAGS, NUM_COUNTRIES, NUM_CATEGORIES)
  
  stations( DBID, LABEL, LOGO_URI, HOMEPAGE_URI, STREAM_URI, TYPE, SOURCE_ID )

  categories( ID, NAME )
  station_categories( ID, DBID, CATEGORY_ID )

  tags( ID, NAME )
  station_tags( ID, DBID, TAG_ID )

  countries( ID, NAME )
  station_countries( ID, DBID, COUNTRY_ID )

  languages( ID, NAME )
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
  gavl_array_t countries; // 2 character codes
  gavl_array_t languages; // 3 character codes
  
  } station_t;

typedef struct
  {
  sqlite3 * db;

  int64_t num_stations;
  int64_t num_sources;
  
  } streams_t;

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

static int
get_station_callback(void * data, int argc, char **argv, char **azColName)
  {
  return 0;
  }

static void browse_station(bg_mdb_backend_t * b,
                           gavl_dictionary_t * ret, int64_t id)
  {
  
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

static void add_station(bg_mdb_backend_t * b, station_t * s)
  {
  char * sql;
  int result;
  streams_t * p = b->priv;

  s->id = ++p->num_stations;
  
  sql = sqlite3_mprintf("INSERT INTO stations ("META_DB_ID", "GAVL_META_LABEL", "GAVL_META_URI", "GAVL_META_STATION_URL", "GAVL_META_LOGO_URL", TYPE, SOURCE_ID) "
                        "VALUES"
                        " (%"PRId64", %Q, %Q, %Q, %Q, %d, %"PRId64"); ",
                        s->id, s->name, s->stream_uri, s->station_uri, s->logo_uri, s->type, s->source_id);
  
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
  
  }

static void rescan(bg_mdb_backend_t * be)
  {
  streams_t * p = be->priv;

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
  
  bg_sqlite_end_transaction(p->db);
  }


static int ping_streams(bg_mdb_backend_t * be)
  {
  /* TODO: Don't do this not more often than every 24 hours */
  
  rescan(be);
  be->ping_func = NULL;
  return 1;
  }


static void create_tables(bg_mdb_backend_t * be)
  {
  streams_t * priv = be->priv;

  /* Object table */  
  if(!bg_sqlite_exec(priv->db,
                     "CREATE TABLE IF NOT EXISTS sources("META_DB_ID" INTEGER PRIMARY KEY, "
                     GAVL_META_LABEL" TEXT, NUM_LANGUAGES INTEGER, NUM_TAGS INTEGER, NUM_COUNTRIES INTEGER, NUM_CATEGORIES INTEGER);",
                     NULL, NULL) ||
     !bg_sqlite_exec(priv->db,
                     "CREATE TABLE IF NOT EXISTS stations("META_DB_ID" INTEGER PRIMARY KEY, "
                     GAVL_META_LABEL" TEXT, "GAVL_META_URI" TEXT, "GAVL_META_STATION_URL" TEXT, "GAVL_META_LOGO_URL" TEXT,"
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
                     "CREATE TABLE IF NOT EXISTS countries(ID INTEGER PRIMARY KEY, NAME TEXT);", NULL, NULL) ||
     !bg_sqlite_exec(priv->db,
                     "CREATE TABLE IF NOT EXISTS station_countries(ID INTEGER PRIMARY KEY, ATTR_ID INTEGER, STATION_ID INTEGER, SOURCE_ID INTEGER);", NULL, NULL) ||
     !bg_sqlite_exec(priv->db,
                     "CREATE TABLE IF NOT EXISTS languages(ID INTEGER PRIMARY KEY, NAME TEXT);", NULL, NULL) ||
     !bg_sqlite_exec(priv->db,
                     "CREATE TABLE IF NOT EXISTS station_languages(ID INTEGER PRIMARY KEY, ATTR_ID INTEGER, STATION_ID INTEGER, SOURCE_ID INTEGER);", NULL, NULL))
    return;
  
  }

static int browse_object(bg_mdb_backend_t * be, const char * id, gavl_dictionary_t * ret,
                         int * idx, int * total)
  {
  return 0;
  
  }

static void browse_children(bg_mdb_backend_t * be, gavl_msg_t * msg, const char * ctx_id,
                            int start, int num, int one_answer)
  {
  return;
  
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
        }
      break;
      }
    }
  return 1;
  }


void bg_mdb_create_streams(bg_mdb_backend_t * b)
  {
  streams_t * priv;
  gavl_dictionary_t * container;
  gavl_dictionary_t * child;
  gavl_dictionary_t * child_m;
  const gavl_dictionary_t * container_m;
  char * filename;
  int result;
  
  priv = calloc(1, sizeof(*priv));
  b->priv = priv;
  
  filename = gavl_sprintf("%s/streams.sqlite", b->db->path);
  
  result = sqlite3_open(filename, &priv->db);

  create_tables(b);

  
  
  container = bg_mdb_get_root_container(b->db, GAVL_META_MEDIA_CLASS_ROOT_STREAMS);

#if 0
  
  child = gavl_append_track(container, NULL);
  
  child_m = gavl_track_get_metadata_nc(child);
  
  container_m = gavl_track_get_metadata(container);
  
  gavl_dictionary_set_string(child_m, GAVL_META_LABEL, "radio-browser.info");
  gavl_dictionary_set_string(child_m, GAVL_META_MEDIA_CLASS, GAVL_META_MEDIA_CLASS_CONTAINER);

  gavl_dictionary_set_string_nocopy(child_m, GAVL_META_ID,
                                    bg_sprintf("%s/radiobrowser",
                                               gavl_dictionary_get_string(container_m, GAVL_META_ID)));

  /* Update container children. Must be done after setting the media class */
  gavl_track_update_children(container);
  
  /* languaue, country, tag */
  gavl_track_set_num_children(child, 3, 0);
  
  priv->root_id     = gavl_strdup(gavl_dictionary_get_string(child_m, GAVL_META_ID));
  priv->root_id_len = strlen(priv->root_id);
  
  bg_mdb_container_set_backend(child, MDB_BACKEND_RADIO_BROWSER);
  

#endif
  bg_controllable_init(&b->ctrl,
                       bg_msg_sink_create(handle_msg, b, 0),
                       bg_msg_hub_create(1));
  
  b->destroy = destroy_streams;
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

static void rb_set_array(json_object * child, gavl_array_t * ret, const char * name, int language)
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
        const char * v = gavl_language_get_iso639_2_b_from_code(arr[idx]);
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
  char ** arr;
  
  while(rb_servers[uri_idx])
    {
    uri = gavl_sprintf("%s/json/stations?order=name&offset=%d&limit=%d",
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
    
    rb_set_array(child, &s.tags, "tags", 0);
    rb_set_array(child, &s.languages, "languagecodes", 1);
    rb_set_array(child, &s.countries, "countrycode", 0);

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
    //  gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Got %d streams from radio-browser.info", result);
    }

  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Imported %d streams from radio-browser.info", stations_added);
  
  }

static void import_m3u(bg_mdb_backend_t * b)
  {
  
  }
