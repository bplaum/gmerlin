/*****************************************************************
 * gmerlin - a general purpose multimedia framework and applications
 *
 * Copyright (c) 2001 - 2024 Members of the Gmerlin project
 * http://github.com/bplaum
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



#ifndef BG_MDB_H_INCLUDED
#define BG_MDB_H_INCLUDED


#include <gmerlin/httpserver.h>
#include <gmerlin/bgmsg.h>
#include <gmerlin/pluginregistry.h>
#include <gmerlin/backend.h>

#include <gmerlin/cfgctx.h>
#include <gmerlin/playermsg.h>

/* IDs of the root elements */      

#define BG_MDB_ID_FAVORITES         "/favorites"
#define BG_MDB_ID_MUSICALBUMS       "/albums"
#define BG_MDB_ID_SONGS             "/songs"
#define BG_MDB_ID_PLAYLISTS         "/playlists"
#define BG_MDB_ID_STREAMS           "/streams"
#define BG_MDB_ID_MOVIES            "/movies"
#define BG_MDB_ID_TV_SHOWS          "/series"
#define BG_MDB_ID_LIBRARY           "/library"
#define BG_MDB_ID_DIRECTORIES       "/dirs"
#define BG_MDB_ID_PODCASTS          "/podcasts"
#define BG_MDB_ID_PHOTOS            "/photos"
#define BG_MDB_ID_RECORDERS         "/recorder"

#define BG_MDB_ID_PLAYQUEUE         BG_PLAYQUEUE_ID

/* Messages for namespace BG_MSG_NS_DB */

/*
 *  ContextID: album_id
    arg0: idx        (int)
    arg1: num_delete (int)
    arg2: new_tracks (array or dictionary)
 */

#define BG_CMD_DB_SPLICE_CHILDREN         1

/*
 * ContextID: album_id
 * arg0: idx        (int)
 * arg1: uris       (array or string)
 *
 */

// #define BG_CMD_DB_LOAD_URIS               2

/*
 *  Sort by *label*
 *  Only supported by writable backends
 *
 *  ContextID: album_id
 */

#define BG_CMD_DB_SORT                    4


/*
 *  Make a local copy of an item (currently only supported for
 *  podcast episodes)
 */



#define BG_CMD_DB_SAVE_LOCAL             6


/*
 *  ContextID: album_id
    arg0: idx        (int)
    arg1: num_delete (int)
    arg2: new_tracks (array or dictionary)
 */

#define BG_MSG_DB_SPLICE_CHILDREN         100

/*  ContextID: object_id
    arg0: track
 */

#define BG_MSG_DB_OBJECT_CHANGED          101

/*  */

#define BG_MSG_DB_CREATION_DONE           102

/*
 *  ContextID: album_id
 */

#define BG_FUNC_DB_BROWSE_OBJECT          200

/*
 *  ContextID: album_id
 *
 *  arg0 (optional): start, default 0
 *  arg1 (optional): num, default: -1
 *
 *  num = -1 return all children up to the end, but allow them to be
 *           sent in separate replies (gmerlin default)
 *  num = 0  return all children up to the end in one reply (upnp way)
 */

#define BG_FUNC_DB_BROWSE_CHILDREN        201

#define BG_FUNC_DB_RESCAN                 202

/*
 *  arg0: path    (string)
 */

#define BG_FUNC_DB_ADD_SQL_DIR            203

/*
 *  arg0: path    (string)
 */

#define BG_FUNC_DB_DEL_SQL_DIR            204

/*
 *  ContextID: album_id
 *  arg0: metadata   (dictionary)
 */

#define BG_RESP_DB_BROWSE_OBJECT          300

/*
 *  Compatible with splice for simpler frontends
 *
 *  ContextID: album_id
 *  arg0: last       (int) Last operation in sequence
 *  arg1: idx        (int)
 *  arg2: num_delete (int) (always zero)
 *  arg3: new_tracks (array)
 */

#define BG_RESP_DB_BROWSE_CHILDREN        301

#define BG_RESP_DB_RESCAN                 302

#define BG_RESP_ADD_SQL_DIR               303
#define BG_RESP_DEL_SQL_DIR               304
      
typedef struct bg_mdb_s bg_mdb_t;

bg_mdb_t * bg_mdb_create(const char * path, int create, int * locked);

void bg_mdb_merge_root_metadata(bg_mdb_t * db, const gavl_dictionary_t * m);

/* To be called after the root uri of the http server is known */
void bg_mdb_set_root_icons(bg_mdb_t * db);


void bg_mdb_destroy(bg_mdb_t * db);
void bg_mdb_stop(bg_mdb_t * db);

bg_controllable_t * bg_mdb_get_controllable(bg_mdb_t * db);

void bg_mdb_rescan(bg_controllable_t * db);
void bg_mdb_rescan_sync(bg_controllable_t * db);

void bg_mdb_get_thumbnails(bg_mdb_t * mdb, gavl_dictionary_t * track);

void bg_mdb_add_sql_directory(bg_controllable_t * db, const char * dir);
void bg_mdb_del_sql_directory(bg_controllable_t * db, const char * dir);

void bg_mdb_add_sql_directory_sync(bg_controllable_t * db, const char * dir);
void bg_mdb_del_sql_directory_sync(bg_controllable_t * db, const char * dir);


bg_cfg_ctx_t * bg_mdb_get_cfg(bg_mdb_t * db);


/* Browse */

int bg_mdb_browse_object_sync(bg_controllable_t * mdb, gavl_dictionary_t * ret, const char * id, int timeout);
int bg_mdb_browse_children_sync(bg_controllable_t * mdb, gavl_dictionary_t * ret, const char * id, int timeout);

void bg_mdb_set_browse_obj_response(gavl_msg_t * msg, const gavl_dictionary_t * obj,
                                    const gavl_msg_t * cmd, int idx, int total);

void bg_mdb_set_browse_children_response(gavl_msg_t * res, const gavl_array_t * children,
                                         const gavl_msg_t * cmd, int * idx, int last, int total);

void bg_mdb_get_browse_children_request(const gavl_msg_t * req, const char ** id,
                                        int * start, int * num, int * one_answer);

void bg_mdb_set_browse_children_request(gavl_msg_t * req, const char * id,
                                        int start, int num, int one_answer);


/* Utilities */

void bg_mdb_set_load_uri(gavl_msg_t * msg, const char * id, int idx, const char * uri);
void bg_mdb_set_load_uris(gavl_msg_t * msg, const char * id, int idx, const gavl_array_t * arr);

char * bg_mdb_get_parent_id(const char * id);

/* Return the "depth" of an ID
   "/" is depth 0
   "/dir" is depth 1
   "/dir/subdir" is depth 2
   and so o */
int bg_mdb_id_get_depth(const char * id);

const char * bg_mdb_get_child_class(const gavl_dictionary_t * track);

int bg_mdb_is_parent_id(const char * child, const char * parent);

int bg_mdb_is_ancestor(const char * ancestor, const char * descendant);

/* Determine if s track is editable */
int bg_mdb_is_editable(const gavl_dictionary_t * dict);
int bg_mdb_can_add(const gavl_dictionary_t * dict, const char * child_class);

void bg_mdb_set_editable(gavl_dictionary_t * dict);
void bg_mdb_add_can_add(gavl_dictionary_t * dict, const char * child_class);
void bg_mdb_clear_editable(gavl_dictionary_t * dict);


void bg_mdb_object_changed(gavl_dictionary_t * dst, const gavl_dictionary_t * src);

void bg_http_server_set_mdb(bg_http_server_t * s, bg_mdb_t * mdb);

void bg_mdb_set_root_name(bg_mdb_t * db, const char * root);

/* Group handling */

typedef struct
  {
  const char * label;
  const char * id;
  } bg_mdb_group_t;

#define BG_MDB_GROUP_PREFIX     "~group~"
#define BG_MDB_GROUP_PREFIX_LEN 7

extern const bg_mdb_group_t bg_mdb_groups[];
extern const int bg_mdb_num_groups;

const char * bg_mdb_get_group_label(const char * id);
int bg_mdb_test_group_condition(const char * id, const char * str);
const char * bg_mdb_get_group_id(const char * str);
int bg_mdb_get_num_groups(gavl_array_t * arr);
int bg_mdb_get_group_size(gavl_array_t * arr, const char * id);

int bg_mdb_adjust_num(int start, int * num, int total);



#endif // BG_MDB_H_INCLUDED

