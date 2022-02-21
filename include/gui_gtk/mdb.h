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

#include <gmerlin/bggavl.h>
#include <gmerlin/mdb.h>

#define bg_gtk_atom_tracks_name bg_tracks_mimetype

typedef struct bg_gtk_mdb_tree_s bg_gtk_mdb_tree_t;

typedef struct bg_gtk_mdb_container_s bg_gtk_mdb_container_t;

bg_gtk_mdb_tree_t * bg_gtk_mdb_tree_create(bg_controllable_t * mdb_ctrl);

void bg_gtk_mdb_tree_destroy(bg_gtk_mdb_tree_t *);

void bg_gtk_mdb_tree_set_player_ctrl(bg_gtk_mdb_tree_t *, bg_controllable_t * player_ctrl);
void bg_gtk_mdb_tree_unset_player_ctrl(bg_gtk_mdb_tree_t * t);

GtkWidget * bg_gtk_mdb_tree_get_widget(bg_gtk_mdb_tree_t * w);

void bg_gtk_trackinfo_show(const gavl_dictionary_t * m,
                           GtkWidget * parent);
