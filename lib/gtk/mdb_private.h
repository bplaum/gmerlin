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

typedef struct album_s album_t;

#define LIST_ICON_WIDTH 48

enum
{
  LIST_COLUMN_ICON,
  LIST_COLUMN_HAS_ICON,
  LIST_COLUMN_PIXBUF,
  LIST_COLUMN_HAS_PIXBUF,
  LIST_COLUMN_LABEL,
  LIST_COLUMN_COLOR,
  LIST_COLUMN_ID,
  LIST_COLUMN_SEARCH_STRING,
  LIST_COLUMN_CURRENT,
  NUM_LIST_COLUMNS
};


/* Menus for tree and list */

typedef struct
  {
  GtkWidget * menu;

  /* Always there */
  GtkWidget * save_item;
  GtkWidget * info_item;

  /* Only when items are available */
  GtkWidget * add_item;
  GtkWidget * play_item;
  
  /* Only when editable */
  GtkWidget * sort_item;
  GtkWidget * load_files_item;
  GtkWidget * load_url_item;

  GtkWidget * new_playlist_item;
  GtkWidget * new_container_item;
  GtkWidget * new_stream_source_item;
  GtkWidget * add_directory_item;
  GtkWidget * delete_item;
  
  } album_menu_t;

typedef struct
  {
  GtkWidget * menu;

  /* Always there */
  GtkWidget * info_item;
  GtkWidget * copy_item;
  GtkWidget * save_item;
  
  GtkWidget * add_item;
  GtkWidget * play_item;
  GtkWidget * favorites_item;
  
  /* Only when editable */
  //  GtkWidget * cut_item;
  GtkWidget * paste_item;
  GtkWidget * delete_item;

  /* Only for podcast episodes */
  GtkWidget * download_item;
  } track_menu_t;

typedef struct
  {
  GtkWidget * menu;
  album_menu_t album_menu;
  track_menu_t track_menu;
  
  GtkWidget * album_item;
  GtkWidget * track_item;
  
  } menu_t;

/* List widget */

typedef struct
  {
  /* List view */
  GtkWidget * listview;
  GtkWidget * widget;

  /* Tab label */
  GtkWidget * close_button;
  GtkWidget * tab;

  GtkWidget * tab_label;
  GtkWidget * tab_label_cur;
  GtkWidget * tab_icon;
  GtkWidget * tab_image;
  GtkWidget * menu_label;
  
  /* Window */
  GtkWidget * window;
  
  album_t * a;

  const char * klass;
  
  int drag_pos;
  guint drop_time;
  int move;        // Move active
  
  int last_mouse_x;
  int last_mouse_y;
  GtkTreePath * last_path;

  int select_on_release;
  } list_t;

struct album_s
  {
  /* list widget (NULL for expanded albums) */
  list_t * list;
  gavl_dictionary_t * a;
  char * id;
  bg_gtk_mdb_tree_t * t;
  
  int local; // e.g. playerqueue
  };

typedef struct
  {
  album_t ** albums;
  int num_albums;
  int albums_alloc;
  } album_array_t;

struct bg_gtk_mdb_tree_s
  {
  GtkWidget * treeview;
  GtkWidget * box;
  guint idle_tag;
  guint row_expanded_tag;
  
  bg_control_t ctrl;
  bg_control_t player_ctrl;
  bg_controllable_t * player_ctrl_p;
  bg_controllable_t * mdb_ctrl_p;
  
  GtkWidget * notebook;

  album_array_t tab_albums;
  album_array_t win_albums;
  album_array_t exp_albums;
  
  album_t playqueue;
  
  menu_t menu;
  
  //  album_t * menu_album;
  gavl_dictionary_t * list_clipboard;

  gavl_array_t browse_object_requests;
  gavl_array_t browse_children_requests;

  gavl_array_t icons_to_load;
  gavl_array_t list_icons_to_load;
  
  char * playback_id;
  
  int icons_loading; // Keeps track on how many icons are loaded in the background right now

  int have_children;
  
  int local_folders; // Playqueue etc
  
  struct
    {
    album_t * a;
    GtkWidget * widget; // Widget, which caught the event
    int num_selected;
    
    const gavl_dictionary_t * item;  // If only one item is selected
    const gavl_dictionary_t * album; // Container
    const gavl_dictionary_t * parent; // Parent album
    int tree; // Menu was fired from the tree view
    } menu_ctx;
    
  };

int bg_gtk_mdb_list_id_to_iter(GtkTreeView *treeview, GtkTreeIter * iter,
                               const char * id);

void bg_gtk_mdb_list_set_pixbuf(bg_gtk_mdb_tree_t * tree, const char * id, GdkPixbuf * pb);

int bg_gtk_mdb_array_get_flag_str(const gavl_array_t * arr, const char * id);
void bg_gtk_mdb_array_set_flag_str(gavl_array_t * arr, const char * id, int flag);

list_t * bg_gtk_mdb_list_create(album_t * a);
void bg_gtk_mdb_list_destroy(list_t * l);

gboolean
bg_gtk_mdb_search_equal_func(GtkTreeModel *model, gint column,
                             const gchar *key, GtkTreeIter *iter,
                             gpointer search_data);

void bg_gtk_mdb_tree_update_node(bg_gtk_mdb_tree_t * t, const char * id,
                                 const gavl_dictionary_t * dict);

album_t * bg_gtk_mdb_album_is_open(bg_gtk_mdb_tree_t * tree, const char * id);

void bg_gtk_mdb_album_update_track(album_t * a, const char * id,
                                   const gavl_dictionary_t * dict);

void bg_gtk_mdb_tree_close_tab_album(bg_gtk_mdb_tree_t * t, int idx);
void bg_gtk_mdb_tree_close_window_album(bg_gtk_mdb_tree_t * t, int idx);

void bg_gtk_mdb_tree_delete_selected_album(bg_gtk_mdb_tree_t * t);

char * bg_gtk_mdb_tree_create_markup(const gavl_dictionary_t * m, const char * parent_class);

void bg_gdk_mdb_list_set_obj(list_t * l, const gavl_dictionary_t * dict);

void bg_gtk_mdb_list_splice_children(list_t * l, int idx, int del, const gavl_value_t * add, int update_dict);

void bg_gtk_mdb_menu_init(menu_t * m, bg_gtk_mdb_tree_t * tree);

void bg_mdb_tree_close_tab_album(bg_gtk_mdb_tree_t * t, int idx);

void bg_gtk_mdb_browse_children(bg_gtk_mdb_tree_t * t, const char * id);
void bg_gtk_mdb_browse_object(bg_gtk_mdb_tree_t * t, const char * id);

void bg_gtk_mdb_popup_menu(bg_gtk_mdb_tree_t * t, const GdkEvent *trigger_event);
