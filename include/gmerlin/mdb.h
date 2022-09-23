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

#ifndef BG_MDB_H_INCLUDED
#define BG_MDB_H_INCLUDED


#include <gmerlin/httpserver.h>
#include <gmerlin/msgqueue.h>
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
#define BG_MDB_ID_PLAYQUEUE         BG_PLAYQUEUE_ID

      
typedef struct bg_mdb_s bg_mdb_t;

bg_mdb_t * bg_mdb_create(const char * path,
                         int create, bg_http_server_t * srv);

void bg_mdb_merge_root_metadata(bg_mdb_t * db, const gavl_dictionary_t * m);

void bg_mdb_destroy(bg_mdb_t * db);
void bg_mdb_stop(bg_mdb_t * db);

bg_controllable_t * bg_mdb_get_controllable(bg_mdb_t * db);

void bg_mdb_rescan(bg_mdb_t * db);
void bg_mdb_rescan_sync(bg_mdb_t * db);


void bg_mdb_add_uris(bg_mdb_t * mdb, const char * parent_id, int idx,
                     const gavl_array_t * uris);

void bg_mdb_get_thumbnails(bg_mdb_t * mdb, gavl_dictionary_t * track);

bg_cfg_ctx_t * bg_mdb_get_cfg(bg_mdb_t * db);


/* Browse */

int bg_mdb_browse_object_sync(bg_mdb_t * mdb, gavl_dictionary_t * ret, const char * id, int timeout);
int bg_mdb_browse_children_sync(bg_mdb_t * mdb, gavl_dictionary_t * ret, const char * id, int timeout);

void bg_mdb_set_browse_children_response(gavl_msg_t * res, const gavl_array_t * children,
                                         const gavl_msg_t * cmd, int * idx, int last, int total);

void bg_mdb_get_browse_children_request(const gavl_msg_t * req, const char ** id,
                                        int * start, int * num, int * one_answer);

void bg_mdb_set_browse_children_request(gavl_msg_t * req, const char * id,
                                        int start, int num, int one_answer);


/* Utilities */

char * bg_mdb_get_parent_id(const char * id);

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

