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

#include <gmerlin/mdb.h>
#include <gmerlin/utils.h>

#include <gmerlin/translation.h>
#include <gmerlin/log.h>
#define LOG_DOMAIN "mdb.sqlite"

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

  sources( ID, LABEL, SOURCE_TYPE, SOURCE_URI, NUM_LANGUAGES, NUM_TAGS, NUM_COUTRIES, NUM_GENRES)
  
  stations( DBID, LABEL, LOGO_URI, STREAM_URI, TYPE, SOURCE_ID )

  genres( ID, NAME )
  station_genres( ID, DBID, GENRE_ID )

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
  char * name;
  char * stream_url;
  char * logo_url;
  int type;
  int64_t source_id;

  gavl_array_t genres;
  gavl_array_t tags;
  gavl_array_t countries;
  gavl_array_t languages;
  
  } station_t;

typedef struct
  {
  sqlite3 * db;
  
  } streams_t;

static void import_icecast(bg_mdb_backend_t * b, const char * uri);
static void import_radiobrowser(bg_mdb_backend_t * b, const char * uri);
static void import_m3u(bg_mdb_backend_t * b, const char * uri, const char * label);

static int
get_station_callback(void * data, int argc, char **argv, char **azColName)
  {
  return 0;
  }

static void browse_station(bg_mdb_backend_t * b, station_t * ret, int64_t * id)
  {
  
  }

static void add_station(bg_mdb_backend_t * b, station_t * ret, int64_t * id)
  {
  
  }

static void destroy_streams(bg_mdb_backend_t * b)
  {
  
  }

static int ping_streams(bg_mdb_backend_t * be)
  {
  return 0;
  
  }

static void rescan(bg_mdb_backend_t * be)
  {
  
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

static void create_tables(bg_mdb_backend_t * be)
  {
  streams_t * priv = be->priv;

  /* Object table */  
  if(!bg_sqlite_exec(priv->db,
                     "CREATE TABLE IF NOT EXISTS stations("META_DB_ID" INTEGER PRIMARY KEY, "
                     GAVL_META_LABEL" TEXT, "GAVL_META_URI" TEXT, "GAVL_META_LOGO_URL" TEXT,"
                     "TYPE INTEGER, SOURCE_ID INTEGER);",
                     NULL, NULL) ||
     !bg_sqlite_exec(priv->db,
                     "CREATE TABLE IF NOT EXISTS tags(ID INTEGER PRIMARY KEY, "GAVL_META_TAG" TEXT);", NULL, NULL) ||
     !bg_sqlite_exec(priv->db,
                     "CREATE TABLE IF NOT EXISTS station_tags(ID INTEGER PRIMARY KEY, ATTR_ID INTEGER, STATION_ID INTEGER);", NULL, NULL) ||
     !bg_sqlite_exec(priv->db,
                     "CREATE TABLE IF NOT EXISTS genres(ID INTEGER PRIMARY KEY, "GAVL_META_GENRE" TEXT);", NULL, NULL) ||
     !bg_sqlite_exec(priv->db,
                     "CREATE TABLE IF NOT EXISTS station_genres(ID INTEGER PRIMARY KEY, ATTR_ID INTEGER, STATION_ID INTEGER);", NULL, NULL) ||
     !bg_sqlite_exec(priv->db,
                     "CREATE TABLE IF NOT EXISTS countries(ID INTEGER PRIMARY KEY, "GAVL_META_COUNTRY" TEXT);", NULL, NULL) ||
     !bg_sqlite_exec(priv->db,
                     "CREATE TABLE IF NOT EXISTS station_countries(ID INTEGER PRIMARY KEY, ATTR_ID INTEGER, STATION_ID INTEGER);", NULL, NULL) ||
     !bg_sqlite_exec(priv->db,
                     "CREATE TABLE IF NOT EXISTS languages(ID INTEGER PRIMARY KEY, "GAVL_META_LANGUAGE" TEXT);", NULL, NULL) ||
     !bg_sqlite_exec(priv->db,
                     "CREATE TABLE IF NOT EXISTS station_languages(ID INTEGER PRIMARY KEY, ATTR_ID INTEGER, STATION_ID INTEGER);", NULL, NULL))
    return;
  
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
  int exists = 0;
  
  priv = calloc(1, sizeof(*priv));
  
  filename = gavl_sprintf("%s/streams.sqlite", b->db->path);

  if(!access(filename, R_OK | W_OK))
    exists = 1;
  
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
  
  bg_controllable_init(&b->ctrl,
                       bg_msg_sink_create(handle_msg, b, 0),
                       bg_msg_hub_create(1));

#endif
  
  b->priv = priv;
  b->destroy = destroy_streams;
  b->ping_func = ping_streams;
  }
