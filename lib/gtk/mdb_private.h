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

#define LIST_ICON_WIDTH  48
#define LIST_ICON_HEIGHT 72

#define TREE_ICON_WIDTH  48
#define TREE_ICON_HEIGHT 72

enum
{
  LIST_COLUMN_ICON,
  LIST_COLUMN_HAS_ICON,
  LIST_COLUMN_PIXBUF,
  LIST_COLUMN_HAS_PIXBUF,
  LIST_COLUMN_LABEL,
  LIST_COLUMN_ID,
  LIST_COLUMN_SEARCH_STRING,
  LIST_COLUMN_HASH,
  LIST_COLUMN_CURRENT,
  NUM_LIST_COLUMNS
};

enum
{
  TREE_COLUMN_ICON,
  TREE_COLUMN_HAS_ICON,
  TREE_COLUMN_PIXBUF,
  TREE_COLUMN_HAS_PIXBUF,
  TREE_COLUMN_LABEL,
  TREE_COLUMN_ID,
  TREE_COLUMN_SEARCH_STRING,
  TREE_COLUMN_TOOLTIP,
  TREE_COLUMN_FLAGS,
  //  TREE_COLUMN_CAN_EXPAND,
  //  TREE_COLUMN_CAN_OPEN,
  //  TREE_COLUMN_EDITABLE,
  NUM_TREE_COLUMNS
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

  } track_menu_t;

typedef struct
  {
  GtkWidget * menu;
  album_menu_t album_menu;
  track_menu_t track_menu;
  
  GtkWidget * album_item;
  GtkWidget * track_item;
  
  } menu_t;

/* Tree flags (stored in the model along with the edit flags) */

#define TREE_CAN_EXPAND        (1<<0)
#define TREE_CAN_OPEN          (1<<1)
#define TREE_HAS_DUMMY         (1<<2)

/* List widget flags (stored in list_t along with the edit flags) */

#define LIST_SELECT_ON_RELEASE (1<<1)

/* Is in the flags for list_t and tree */
#define ALBUM_EDITABLE               (1<<8)
#define ALBUM_CAN_ADD_SONG           (1<<9)
#define ALBUM_CAN_ADD_CONTAINER     (1<<10)
#define ALBUM_CAN_ADD_DIRECTORY     (1<<11)
#define ALBUM_CAN_ADD_STREAM_SOURCE (1<<12)
#define ALBUM_CAN_ADD_URL           (1<<13)

typedef struct
  {
  char * id;
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
  
  char * klass;
  
  int drag_pos;
  guint drop_time;
  //  int move;        // Move active
  
  int last_mouse_x;
  int last_mouse_y;
  GtkTreePath * last_path;

  //  int select_on_release;

  bg_gtk_mdb_tree_t * tree;

  int flags;
  
  } list_t;

struct bg_gtk_mdb_tree_s
  {
  GtkWidget * treeview;
  GtkWidget * box;
  guint idle_tag;
  guint row_expanded_tag;
  
  bg_control_t ctrl;
  bg_control_t player_ctrl;
  bg_control_t cache_ctrl;
  
  bg_controllable_t * player_ctrl_p;
  bg_controllable_t * mdb_ctrl_p;
  
  GtkWidget * notebook;

  GList * lists;
  

  bg_mdb_cache_t * cache;
  
  menu_t menu;
  
  gavl_dictionary_t * list_clipboard;

  gavl_array_t tree_icons;
  gavl_array_t list_icons;
  
  char * playback_id;
  char * cur; // Current track as hash
  
  int icons_loading; // Keeps track on how many icons are loaded in the background right now
  
  struct
    {
    char * id;
    GtkWidget * widget; // Widget, which caught the event
    int num_selected;
    
    list_t * list; /* Currently selected list */
    } menu_ctx;
  
  bg_msg_sink_t * dlg_sink;
  gavl_dictionary_t dlg_dict;
  };

#define MSG_ADD_STREAM      "add-stream"
#define MSG_ADD_CONTAINER   "add-container"
#define MSG_SAVE_ALBUM      "save-album"
#define MSG_SAVE_SELECTED   "save-selected"
#define MSG_LOAD_FILES      "load-files"

void bg_gtk_mdb_create_container_generic(bg_gtk_mdb_tree_t * tree,
                                         const char * label,
                                         const char * klass,
                                         const char * uri);

void bg_gtk_mdb_add_stream_source(bg_gtk_mdb_tree_t * tree,
                                  const char * label,
                                  const char * uri);

int bg_gtk_mdb_list_id_to_iter(GtkTreeView *treeview, GtkTreeIter * iter,
                               const char * id);

int bg_gtk_mdb_tree_id_to_iter(GtkTreeView *treeview, const char * id, GtkTreeIter * ret);

void bg_gtk_mdb_list_get_selected_ids(list_t * list,
                                      gavl_array_t * selected);

void bg_gtk_mdb_list_set_pixbuf(bg_gtk_mdb_tree_t * tree, const char * id, GdkPixbuf * pb);

gavl_dictionary_t * bg_gtk_mdb_list_extract_selected(list_t * list);

list_t * bg_gtk_mdb_list_create(bg_gtk_mdb_tree_t * tree, const char * id);
void bg_gtk_mdb_list_destroy(list_t * l);

void
bg_gtk_mdb_list_add_entries(list_t * l, const gavl_array_t * entries, const char * sibling_before);

void
bg_gtk_mdb_list_delete_entries(list_t * l, const gavl_array_t * ids);

void
bg_gtk_mdb_list_update_entry(list_t * l, const char * id, const gavl_dictionary_t * dict);


void bg_gtk_mdb_list_set_current(list_t * list, const char * hash);

gboolean
bg_gtk_mdb_search_equal_func(GtkTreeModel *model, gint column,
                             const gchar *key, GtkTreeIter *iter,
                             gpointer search_data);

list_t * bg_gtk_mdb_tree_find_list(bg_gtk_mdb_tree_t * t,
                                   const char * id);

void bg_gtk_mdb_tree_delete_selected_album(bg_gtk_mdb_tree_t * t);

char * bg_gtk_mdb_tree_create_markup(const gavl_dictionary_t * m, const char * parent_class);

void bg_gdk_mdb_list_set_obj(list_t * l, const gavl_dictionary_t * dict);

void bg_gtk_mdb_menu_init(menu_t * m, bg_gtk_mdb_tree_t * tree);

void bg_mdb_tree_close_tab_album(bg_gtk_mdb_tree_t * t, int idx);


void bg_gtk_mdb_popup_menu(bg_gtk_mdb_tree_t * t, const GdkEvent *trigger_event);

int bg_gtk_mdb_get_edit_flags(const gavl_dictionary_t * track);
  
void bg_gtk_mdb_load_list_icon(list_t * list, const gavl_dictionary_t * track);
void bg_gtk_mdb_load_tree_icon(bg_gtk_mdb_tree_t * tree, const gavl_dictionary_t * track);
