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

#include <string.h>

#include <gtk/gtk.h>

#include <config.h>

#include <gavl/metatags.h>

#include <gmerlin/msgqueue.h>
#include <gmerlin/iconfont.h>
#include <gmerlin/utils.h>
#include <gmerlin/bggavl.h>
#include <gmerlin/state.h>
#include <gmerlin/playermsg.h>
#include <gmerlin/player.h>

#include <gmerlin/translation.h>
#include <gmerlin/log.h>
#define LOG_DOMAIN "mdb.tree"

#include <gui_gtk/mdb.h>
#include <gui_gtk/gtkutils.h>
#include <gavl/metatags.h>

#include <gmerlin/cfg_dialog.h>

#include "mdb_private.h"



/* Forward declarations */

static void save_tracks(bg_gtk_mdb_tree_t * tree);
static void save_selected(bg_gtk_mdb_tree_t * tree);

static void load_files(bg_gtk_mdb_tree_t * tree);
static void create_playlist(bg_gtk_mdb_tree_t * tree);
static void create_folder(bg_gtk_mdb_tree_t * tree);  
static void load_uri(bg_gtk_mdb_tree_t * tree);

static void insert_selection_data(album_t * a, GtkSelectionData *data,
                                  int idx);


static gavl_dictionary_t * list_extract_selected(album_t * album);

static void clipboard_received_func(GtkClipboard *clipboard,
                                    GtkSelectionData *selection_data,
                                    gpointer data);



/* Drag & Drop */

static const GtkTargetEntry list_src_entries[] = 
  {
    { bg_gtk_atom_tracks_name, 0, BG_TRACK_FORMAT_GMERLIN },
    { "application/xspf+xml",  0, BG_TRACK_FORMAT_XSPF    },
    { "audio/x-mpegurl",       0, BG_TRACK_FORMAT_M3U     },
    { "audio/x-scpls",         0, BG_TRACK_FORMAT_PLS     },
    { "text/uri-list",         0, BG_TRACK_FORMAT_URILIST },
  };

static const GtkTargetEntry list_dst_entries[] = 
  {
    { bg_gtk_atom_tracks_name, 0, BG_TRACK_FORMAT_GMERLIN },
    { "text/uri-list",         0, BG_TRACK_FORMAT_URILIST },
    { "text/plain",            0, BG_TRACK_FORMAT_URILIST },
  };

static const int num_list_src_entries = sizeof(list_src_entries)/sizeof(list_src_entries[0]);
static const int num_list_dst_entries = sizeof(list_dst_entries)/sizeof(list_dst_entries[0]);


static int dst_track_format_from_atom(GdkAtom target)
  {
  int i;
  char * name = gdk_atom_name(target);

  for(i = 0; i < num_list_dst_entries; i++)
    {
    if(!strcmp(list_dst_entries[i].target, name))
      {
      //      fprintf(stderr, "Got dst format: %s %d\n",
      //              name, list_dst_entries[i].info);
      g_free(name);
      return list_dst_entries[i].info;
      }
    
    }
  g_free(name);
  return -1;
  }

static char * iter_to_id_list(GtkTreeView *treeview, GtkTreeIter * iter)
  {
  GtkTreeModel * model;
  GValue value={0,};
  char * ret;
  
  model = gtk_tree_view_get_model(treeview);
  gtk_tree_model_get_value(model, iter, LIST_COLUMN_ID, &value);
  
  ret = gavl_strdup(g_value_get_string(&value));
  g_value_unset(&value);
  return ret;
  }

int bg_gtk_mdb_list_id_to_iter(GtkTreeView *treeview, GtkTreeIter * iter,
                               const char * id)
  {
  const char * iter_id;
  
  GtkTreeModel * model;
  GValue value={0,};
  
  model = gtk_tree_view_get_model(treeview);

  if(!gtk_tree_model_get_iter_first(model, iter))
    return 0;

  while(1)
    {
    gtk_tree_model_get_value(model, iter, LIST_COLUMN_ID, &value);

    if(!(iter_id = g_value_get_string(&value)))
      return 0;
    
    if(!strcmp(iter_id, id))
      {
      g_value_unset(&value);
      return 1;
      }
    g_value_unset(&value);

    if(!gtk_tree_model_iter_next(model, iter))
      break;
    
    }
  return 0;
  }

void bg_gtk_mdb_list_set_pixbuf(load_image_t * d, GdkPixbuf * pb)
  {
  album_t * a;
  GtkTreeIter iter;
  char * parent_id = NULL;
  GtkTreeModel * model;
  
  if(!d->id || !(parent_id = bg_mdb_get_parent_id(d->id)))
    goto fail;
    
  if(!(a = bg_gtk_mdb_album_is_open(d->tree, parent_id)))
    goto fail;

  if(!bg_gtk_mdb_list_id_to_iter(GTK_TREE_VIEW(a->list->listview), &iter, d->id))
    goto fail;

  model = gtk_tree_view_get_model(GTK_TREE_VIEW(a->list->listview));

  if(pb)
    {
    gtk_list_store_set(GTK_LIST_STORE(model), &iter,
                       LIST_COLUMN_HAS_ICON, FALSE,
                       LIST_COLUMN_HAS_PIXBUF, TRUE,
                       LIST_COLUMN_PIXBUF, pb, -1);
    }
  else
    {
    gtk_list_store_set(GTK_LIST_STORE(model), &iter,
                       LIST_COLUMN_HAS_PIXBUF, FALSE,
                       LIST_COLUMN_HAS_ICON, TRUE,
                       -1);
    }
  
  fail:
  
  if(parent_id)
    free(parent_id);
  return;
  }


/* */

static int is_item(const gavl_value_t * val)
  {
  const gavl_dictionary_t * dict;
  const gavl_dictionary_t * m;
  const char * klass;

  if((dict = gavl_value_get_dictionary(val)) &&
     (m = gavl_track_get_metadata(dict)) &&
     (klass = gavl_dictionary_get_string(m, GAVL_META_MEDIA_CLASS)) &&
     gavl_string_starts_with(klass, "item"))
    return 1;
  
  return 0;
  }


/* List Functions */

/* Callback functions for the clipboard */

static void list_clipboard_get_func(GtkClipboard *clipboard,
                                    GtkSelectionData *selection_data,
                                    guint info,
                                    gpointer data)
  {
  char * str;

  bg_gtk_mdb_tree_t * tree = data;
  GdkAtom type_atom = gdk_atom_intern("STRING", FALSE);

  if(!type_atom)
    return;
  
  str = bg_tracks_to_string(tree->list_clipboard, info, 1, 0);

  if(str)
    {
    gtk_selection_data_set(selection_data, type_atom, 8, (uint8_t*)str,
                           strlen(str)+1);
    free(str);
    }
  else
    gtk_selection_data_set(selection_data, type_atom, 8, (uint8_t*)"", 1);
    
  }

static void list_clipboard_clear_func(GtkClipboard *clipboard,
                                 gpointer data)
  {
  bg_gtk_mdb_tree_t * tree = data;

  //  fprintf(stderr, "Clipboard destroy\n");
  
  if(tree->list_clipboard)
    {
    gavl_dictionary_destroy(tree->list_clipboard);
    tree->list_clipboard = NULL;
    }
  }

static gavl_dictionary_t * list_extract_selected(album_t * album)
  {
  int              i;
  int              num;
  GtkTreeIter      iter;
  GtkTreeModel     * model;
  GtkTreeSelection * selection;
  gavl_dictionary_t * ret = gavl_dictionary_create();
  
  model = gtk_tree_view_get_model(GTK_TREE_VIEW(album->list->listview));
  selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(album->list->listview));

  if(!album->a)
    return ret;

  gtk_tree_model_get_iter_first(model, &iter);

  num = gavl_get_num_tracks(album->a);
  
  for(i = 0; i < num; i++)
    {
    if(gtk_tree_selection_iter_is_selected(selection, &iter))
      {
      gavl_value_t val;
      gavl_dictionary_t * track;
      gavl_value_init(&val);
      track = gavl_value_set_dictionary(&val);
      gavl_dictionary_copy(track, gavl_get_track(album->a, i));
      gavl_track_clear_gui_state(track);
      gavl_track_splice_children_nocopy(ret, -1, 0, &val);
      }
    if(!gtk_tree_model_iter_next(model, &iter))
      break;
    }
  return ret;
  }

static void delete_items(bg_msg_sink_t * sink, const char * parent, int idx, int del)
  {
  gavl_msg_t * msg = bg_msg_sink_get(sink);
        
  gavl_msg_set_id_ns(msg, BG_CMD_DB_SPLICE_CHILDREN, BG_MSG_NS_DB);
        
  gavl_dictionary_set_string(&msg->header, GAVL_MSG_CONTEXT_ID, parent);
  
  gavl_msg_set_arg_int(msg, 0, idx);
  gavl_msg_set_arg_int(msg, 1, del);
  bg_msg_sink_put(sink, msg);
  }

/* Internal to GUI index */
static int list_transform_idx(const gavl_dictionary_t * dict, int idx)
  {
  int i;
  const gavl_dictionary_t * d = NULL;
  const char * klass = NULL;
  int ret = 0;

  if(!(d = gavl_get_track(dict, idx)) ||
     !(d = gavl_track_get_metadata(d)) ||
     !(klass = gavl_dictionary_get_string(d, GAVL_META_MEDIA_CLASS)) ||
     !gavl_string_starts_with(klass, "item"))
    {
    return -1;
    }
  for(i = 0; i < idx; i++)
    {
    if((d = gavl_get_track(dict, i)) &&
       (d = gavl_track_get_metadata(d)) &&
       (klass = gavl_dictionary_get_string(d, GAVL_META_MEDIA_CLASS)) &&
       gavl_string_starts_with(klass, "item"))
      ret++;
    }
  return ret;
  }

static void list_delete_selected(album_t * album)
  {
  int              i;
  GtkTreeIter      iter;
  GtkTreeModel     * model;
  GtkTreeSelection * selection;
  int idx;
  int del;
  int j;
  int num;
  
  const char * id;

  bg_msg_sink_t * sink = album->t->ctrl.cmd_sink;
  
  if(!album->a || !bg_mdb_is_editable(album->a))
    return;

  model = gtk_tree_view_get_model(GTK_TREE_VIEW(album->list->listview));
  selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(album->list->listview));

  id = gavl_track_get_id(album->a);

  if(!strcmp(id, BG_PLAYQUEUE_ID))
    sink = album->t->player_ctrl.cmd_sink;
  
  gtk_tree_model_get_iter_first(model, &iter);
  
  del = 0;
  idx = 0;

  // album->a will be updated just after this function call
  num = gavl_get_num_tracks(album->a);

  //   num = gtk_tree_model_iter_n_children(model, NULL);
  
  for(i = 0; i < num; i++)
    {
    if(((j = list_transform_idx(album->a, i)) >= 0) &&
       gtk_tree_model_iter_nth_child(model, &iter, NULL, j) &&
       gtk_tree_selection_iter_is_selected(selection, &iter))
      {
      del++;
      }
    else 
      {
      if(del)
        {
        delete_items(sink, id, idx, del);
        del = 0;
        }
      idx++;
      }
    
    }

  if(del)
    delete_items(sink, id, idx, del);
  }

static void list_copy(album_t * a)
  {
  GtkClipboard *clipboard;
  GdkAtom clipboard_atom;

  fprintf(stderr, "List copy\n");

  //  if(tree->list_clipboard)
  //    gavl_dictionary_destroy(tree->list_clipboard);
    
  clipboard_atom = gdk_atom_intern ("CLIPBOARD", FALSE);   
  clipboard = gtk_clipboard_get(clipboard_atom);
  
  gtk_clipboard_set_with_data(clipboard,
                              list_src_entries,
                              num_list_src_entries,
                              list_clipboard_get_func,
                              list_clipboard_clear_func,
                              (gpointer)a->t);

  a->t->list_clipboard = list_extract_selected(a);
  }

static void list_paste(album_t * a)
  {
  GtkClipboard *clipboard;
  GdkAtom clipboard_atom;
  GdkAtom target;
  
  if(!bg_mdb_is_editable(a->a))
    {
    //  fprintf(stderr, "Don't paste in read only album\n");
    return;
    }
  
  //    clipboard_atom = gdk_atom_intern ("PRIMARY", FALSE);
  clipboard_atom = gdk_atom_intern ("CLIPBOARD", FALSE);   
  clipboard = gtk_clipboard_get(clipboard_atom);
    
  target = gdk_atom_intern(bg_gtk_atom_tracks_name, FALSE);
    
  gtk_clipboard_request_contents(clipboard,
                                 target,
                                 clipboard_received_func, a);
  
  }

static void list_favorites(album_t * a)
  {
  const char * id;

  gavl_dictionary_t * sel;

  if(!(id = gavl_track_get_id(a->a)) ||
     !strcmp(id, BG_MDB_ID_FAVORITES))
    {
    return;
    }

  sel = list_extract_selected(a);
      
  if(gavl_track_get_num_children(sel) > 0)
    {
    /* Copy to favorites */
    gavl_msg_t * msg = bg_msg_sink_get(a->t->ctrl.cmd_sink);

    gavl_msg_set_id_ns(msg, BG_CMD_DB_SPLICE_CHILDREN, BG_MSG_NS_DB);
        
    gavl_dictionary_set_string(&msg->header, GAVL_MSG_CONTEXT_ID,
                               BG_MDB_ID_FAVORITES);
  
    gavl_msg_set_arg_int(msg, 0, -1);
    gavl_msg_set_arg_int(msg, 1, 0);
    gavl_msg_set_arg(msg, 2, gavl_dictionary_get(sel, GAVL_META_CHILDREN));
    bg_msg_sink_put(a->t->ctrl.cmd_sink, msg);
    }
      
  gavl_dictionary_destroy(sel);
  }

static void set_entry_list(list_t * l,
                           const gavl_dictionary_t * dict,
                           GtkTreeIter * iter)
  {
  const char * klass;
  const char * id;
  const char * var;
  GtkTreeModel * model;
  char * markup;
  const gavl_dictionary_t * m = gavl_track_get_metadata(dict);

  
  model = gtk_tree_view_get_model(GTK_TREE_VIEW(l->listview));

  if(!m)
    return;
  
  if((klass = gavl_dictionary_get_string(m, GAVL_META_MEDIA_CLASS)))
    {
    gtk_list_store_set(GTK_LIST_STORE(model), iter,
                       LIST_COLUMN_ICON, bg_get_type_icon(klass),
                       LIST_COLUMN_HAS_ICON, FALSE, -1);
    }

  markup = bg_gtk_mdb_tree_create_markup(dict, l->klass);
  gtk_list_store_set(GTK_LIST_STORE(model), iter, LIST_COLUMN_LABEL, markup, -1);
  free(markup);
  
  gtk_list_store_set(GTK_LIST_STORE(model), iter, LIST_COLUMN_COLOR, "#000000", -1);

  if((var = gavl_dictionary_get_string(m, GAVL_META_SEARCH_TITLE)) ||
     (var = gavl_dictionary_get_string(m, GAVL_META_TITLE)) ||
     (var = gavl_dictionary_get_string(m, GAVL_META_LABEL)))
    gtk_list_store_set(GTK_LIST_STORE(model), iter, LIST_COLUMN_SEARCH_STRING, var, -1);
  
  id = gavl_dictionary_get_string(m, GAVL_META_ID);
  gtk_list_store_set(GTK_LIST_STORE(model), iter, LIST_COLUMN_ID, id, -1);

  if(!l->klass ||
     (strcmp(l->klass, GAVL_META_MEDIA_CLASS_MUSICALBUM) &&
      strcmp(l->klass, GAVL_META_MEDIA_CLASS_TV_SEASON) &&
      strcmp(l->klass, GAVL_META_MEDIA_CLASS_ROOT_REMOVABLE_AUDIOCD)))
    bg_gtk_mdb_array_set_flag_str(&l->a->t->list_icons_to_load, id, 1);
  else
    {
    gtk_list_store_set(GTK_LIST_STORE(model), iter, LIST_COLUMN_HAS_PIXBUF, FALSE, -1);
    gtk_list_store_set(GTK_LIST_STORE(model), iter, LIST_COLUMN_HAS_ICON, TRUE, -1);
    }
  if(gavl_track_get_gui_state(dict, GAVL_META_GUI_CURRENT))
    gtk_list_store_set(GTK_LIST_STORE(model), iter, LIST_COLUMN_CURRENT, BG_ICON_PLAY, -1);
  else
    gtk_list_store_set(GTK_LIST_STORE(model), iter, LIST_COLUMN_CURRENT, "", -1);

  //  if(!l->idle_tag)
  //    l->idle_tag = g_idle_add(idle_callback, l);
  }

void bg_gtk_mdb_album_update_track(album_t * a, const char * id,
                                   const gavl_dictionary_t * new_dict)
  {
  GtkTreeIter iter;
  
  //          fprintf(stderr, "Object changed %s\n", ctx_id);
  //          gavl_dictionary_dump(&new_dict, 2);
  //          fprintf(stderr, "\n");

  if(!a->list)
    return;
  
  if(bg_gtk_mdb_list_id_to_iter(GTK_TREE_VIEW(a->list->listview), &iter, id))
    {
    gavl_dictionary_t * dict;
    
    if(a->a && (dict = gavl_get_track_by_id_nc(a->a, id)))
      {
      if(new_dict)
        bg_mdb_object_changed(dict, new_dict); 
      set_entry_list(a->list, dict, &iter);
      }
    }
  }


void bg_gdk_mdb_list_set_obj(list_t * l, const gavl_dictionary_t * dict)
  {
  const char * icon;
  const gavl_dictionary_t * m;
  char * markup;
  GdkPixbuf * buf;
  
  if(!(m = gavl_track_get_metadata(dict)))
    {
    fprintf(stderr, "Buug: Need to create metadata\n");
    return;
    }

  l->klass = NULL;
  
  if((l->klass = gavl_dictionary_get_string(m, GAVL_META_MEDIA_CLASS)) &&
     (icon = bg_get_type_icon(l->klass)))
    {
    markup = g_markup_printf_escaped("<span size=\"large\" font_family=\"%s\" weight=\"normal\">%s</span> %s",
                                     BG_ICON_FONT_FAMILY, icon,
                                     gavl_dictionary_get_string(m, GAVL_META_LABEL));
    }
  else
    markup = bg_sprintf("%s", gavl_dictionary_get_string(m, GAVL_META_LABEL));

  gtk_label_set_markup(GTK_LABEL(l->menu_label), markup);
  g_free(markup);


  if((buf = bg_gtk_load_track_image(dict, 48, -1)))
    {
    gtk_image_set_from_pixbuf(GTK_IMAGE(l->tab_image), buf);
    g_object_unref(buf);
    gtk_widget_show(l->tab_image);
    gtk_widget_hide(l->tab_icon);
    }
  else if(icon)
    {
    markup = bg_sprintf("<span size=\"large\" font_family=\"%s\" weight=\"normal\">%s</span>",
                        BG_ICON_FONT_FAMILY, icon);
    gtk_label_set_markup(GTK_LABEL(l->tab_icon), markup);
    free(markup);

    gtk_widget_show(l->tab_icon);
    gtk_widget_hide(l->tab_image);
    }
  else
    {
    gtk_widget_hide(l->tab_icon);
    gtk_widget_hide(l->tab_image);
    }
  
  markup = bg_gtk_mdb_tree_create_markup(dict, NULL);
  gtk_label_set_markup(GTK_LABEL(l->tab_label), markup);
  free(markup);

  gtk_drag_dest_unset(l->listview);
  
  if(bg_mdb_is_editable(dict))
    {
    gtk_drag_dest_set(l->listview,
                      GTK_DEST_DEFAULT_HIGHLIGHT | 
                      GTK_DEST_DEFAULT_MOTION,
                      list_dst_entries,
                      num_list_dst_entries,
                      GDK_ACTION_COPY);
    }
  
  }

void bg_gtk_mdb_list_destroy(list_t * l)
  {

  if(l->tab_icon)
    g_object_ref(G_OBJECT(l->tab_icon));

  if(l->tab_image)
    g_object_ref(G_OBJECT(l->tab_image));

  if(l->window)
    gtk_widget_destroy(l->window);

  /* widgets are destroyed when they are removed from the notebook */
  //  else if(l->widget)
  //    gtk_widget_destroy(l->widget);
  
  
  free(l);
  }

static void close_button_callback(GtkWidget * wid, gpointer data)
  {
  int i;
  bg_gtk_mdb_tree_t * tree = data;

  for(i = 0; i < tree->tab_albums.num_albums; i++)
    {
    if(tree->tab_albums.albums[i]->list->close_button == wid)
      {
      bg_gtk_mdb_tree_close_tab_album(tree, i);
      return;
      }
    }

  for(i = 0; i < tree->win_albums.num_albums; i++)
    {
    if(tree->win_albums.albums[i]->list->close_button == wid)
      {
      bg_gtk_mdb_tree_close_window_album(tree, i);
      return;
      }
    }
  }

#if 0
static const char * close_button_css =
  "GtkButton {"
  " -GtkButton-default-border: 0;"
  " -GtkButton-default-outside-border: 0;"
  " -GtkButton-inner-border: 0;"
  " -GtkWidget-focus-padding: 0;"
  " -GtkWidget-focus-line-width: 0;"
  " margin: 0;"
  " padding: 0; }";
#endif

static GtkWidget * make_close_button(bg_gtk_mdb_tree_t * tree)
  {
  GtkWidget * ret;
  GtkWidget * image;
  GtkCssProvider * provider = gtk_css_provider_new();

  //  gtk_css_provider_load_from_data(provider, close_button_css, -1, NULL);
  
  image = gtk_image_new_from_icon_name("window-close", GTK_ICON_SIZE_MENU);

  ret = gtk_button_new();
  gtk_button_set_image(GTK_BUTTON(ret), image);
  gtk_button_set_relief(GTK_BUTTON(ret),
                        GTK_RELIEF_NONE);
  
  //  gtk_widget_set_focus_on_click
  gtk_widget_set_focus_on_click(ret, FALSE);
  
  g_signal_connect(G_OBJECT(ret), "clicked",
                   G_CALLBACK(close_button_callback), tree);

  gtk_style_context_add_provider(gtk_widget_get_style_context(ret),
                                 GTK_STYLE_PROVIDER (provider),
                                 GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref(provider);
  
  gtk_widget_show(ret);
  return ret;
  }

void bg_gtk_mdb_popup_menu(bg_gtk_mdb_tree_t * t, const GdkEvent *trigger_event)
  {
  /* Show/hide items */
  gtk_widget_hide(t->menu.track_menu.paste_item);
  gtk_widget_hide(t->menu.track_menu.delete_item);

  gtk_widget_hide(t->menu.album_menu.sort_item);
  gtk_widget_hide(t->menu.album_menu.load_files_item);
  gtk_widget_hide(t->menu.album_menu.load_url_item);
  gtk_widget_hide(t->menu.album_menu.new_playlist_item);
  gtk_widget_hide(t->menu.album_menu.new_container_item);
  gtk_widget_hide(t->menu.album_menu.delete_item);
  
  
  if(t->menu_ctx.tree)
    {
    gtk_widget_hide(t->menu.track_item);

    /* Add and play only work for opened albums */
    gtk_widget_hide(t->menu.album_menu.add_item);
    gtk_widget_hide(t->menu.album_menu.play_item);
    }
  else 
    {
    gtk_widget_show(t->menu.track_item);
    gtk_widget_show(t->menu.album_menu.add_item);
    gtk_widget_show(t->menu.album_menu.play_item);
    }
  
  //  fprintf(stderr, "bg_gtk_mdb_popup_menu %p %p %d\n",
  //          t->menu_ctx.album,
  //          t->menu_ctx.parent, bg_mdb_is_editable(t->menu_ctx.parent));
  
  if(t->menu_ctx.album)
    {
    if(t->menu_ctx.tree && t->menu_ctx.parent && bg_mdb_is_editable(t->menu_ctx.parent))
      gtk_widget_show(t->menu.album_menu.delete_item);
    
    if(bg_mdb_is_editable(t->menu_ctx.album))
      {
      gtk_widget_show(t->menu.track_menu.paste_item);
 
      if(!t->menu_ctx.tree)
        gtk_widget_show(t->menu.track_menu.delete_item);
      else if(t->menu_ctx.parent && bg_mdb_is_editable(t->menu_ctx.parent))
        gtk_widget_show(t->menu.album_menu.delete_item);

      gtk_widget_show(t->menu.album_menu.sort_item);

      if(bg_mdb_can_add(t->menu_ctx.album, GAVL_META_MEDIA_CLASS_SONG))
        gtk_widget_show(t->menu.album_menu.load_files_item);

      if(bg_mdb_can_add(t->menu_ctx.album, GAVL_META_MEDIA_CLASS_CONTAINER))
        gtk_widget_show(t->menu.album_menu.new_container_item);
      else if(bg_mdb_can_add(t->menu_ctx.album, GAVL_META_MEDIA_CLASS_PLAYLIST))
        gtk_widget_show(t->menu.album_menu.new_playlist_item);
      
      if(bg_mdb_can_add(t->menu_ctx.album, GAVL_META_MEDIA_CLASS_AUDIO_BROADCAST) ||
         bg_mdb_can_add(t->menu_ctx.album, GAVL_META_MEDIA_CLASS_VIDEO_BROADCAST) ||
         bg_mdb_can_add(t->menu_ctx.album, GAVL_META_MEDIA_CLASS_LOCATION))
        gtk_widget_show(t->menu.album_menu.load_url_item);
      }

    if(!strcmp(gavl_track_get_id(t->menu_ctx.album), BG_PLAYQUEUE_ID))
      {
      gtk_widget_hide(t->menu.album_menu.add_item);
      gtk_widget_hide(t->menu.album_menu.play_item);
      gtk_widget_hide(t->menu.track_menu.add_item);
      }
    else
      {
      gtk_widget_show(t->menu.album_menu.add_item);
      gtk_widget_show(t->menu.album_menu.play_item);
      gtk_widget_show(t->menu.track_menu.add_item);
      }

    if(!strcmp(gavl_track_get_id(t->menu_ctx.album), BG_MDB_ID_FAVORITES) ||
       (t->menu_ctx.num_selected < 1))
      gtk_widget_hide(t->menu.track_menu.favorites_item);
    else
      gtk_widget_show(t->menu.track_menu.favorites_item);
    
    if(!t->menu_ctx.num_selected)
      {
      gtk_widget_hide(t->menu.track_item);
      }
    else if(t->menu_ctx.num_selected == 1)
      {
      gtk_widget_show(t->menu.track_menu.info_item);
      }
    else // > 1
      {
      gtk_widget_hide(t->menu.track_menu.info_item);
      }
    
    }
  
  
  gtk_menu_popup_at_pointer(GTK_MENU(t->menu.menu), trigger_event);
  }


static void do_play(bg_gtk_mdb_tree_t * t, const char * id)
  {
  gavl_msg_t * msg;
  char * queue_id = bg_player_tracklist_make_id(t->player_ctrl.id, id);
  
  /* Set current track */

  msg = bg_msg_sink_get(t->player_ctrl.cmd_sink);
  gavl_msg_set_id_ns(msg, BG_PLAYER_CMD_PLAY_BY_ID, BG_MSG_NS_PLAYER);
  gavl_msg_set_arg_string(msg, 0, queue_id);
  bg_msg_sink_put(t->player_ctrl.cmd_sink, msg);
  free(queue_id);
  }

static void album_add(bg_gtk_mdb_tree_t * t, gavl_dictionary_t * album, int replace, int play)
  {
  const gavl_array_t * arr;
  gavl_msg_t * msg;
  const char * id;
  
  const char * play_id = NULL;

  if(!t->player_ctrl.cmd_sink)
    return;

  if((id = gavl_track_get_id(album)) && !strcmp(id, BG_PLAYQUEUE_ID))
    {
    return;
    }
  
  if(play)
    {
    const gavl_dictionary_t * track;
    
    if((track = gavl_get_track(album, 0)))
      play_id = gavl_track_get_id(track);
    }
  if((arr = gavl_get_tracks(album)) &&
     arr->num_entries)
    {
    /* Upload what we have now */
    
    msg = bg_msg_sink_get(t->player_ctrl.cmd_sink);
    
    gavl_msg_set_id_ns(msg, BG_CMD_DB_SPLICE_CHILDREN, BG_MSG_NS_DB);
    
    gavl_dictionary_set_string(&msg->header, GAVL_MSG_CONTEXT_ID, BG_PLAYQUEUE_ID);

    if(replace)
      {
      gavl_msg_set_arg_int(msg, 0, 0);  // idx
      gavl_msg_set_arg_int(msg, 1, -1); // del
      }
    else
      {
      gavl_msg_set_arg_int(msg, 0, -1); // idx
      gavl_msg_set_arg_int(msg, 1, 0);  // del
      }
    
    gavl_msg_set_arg_array(msg, 2, arr);
    
    bg_msg_sink_put(t->player_ctrl.cmd_sink, msg);
    }

  if(play_id)
    do_play(t, play_id);
  }

static void selected_add(bg_gtk_mdb_tree_t * t, album_t * a, int replace, int play)
  {
  gavl_dictionary_t * album;

  if(!t->player_ctrl.cmd_sink)
    return;
  
  album = list_extract_selected(a);
  album_add(t, album, replace, play);
  gavl_dictionary_destroy(album);
  }

#if 0
static void entry_add(bg_gtk_mdb_tree_t * t, album_t * a, const char * id, int replace, int play)
  {
  gavl_msg_t * msg;
  const gavl_dictionary_t * track;
  const char * play_id = NULL;

  if(!t->player_ctrl.cmd_sink)
    return;
  
  if(gavl_string_starts_with(id, BG_PLAYQUEUE_ID))
    {
    gavl_msg_t * msg;
    if(!play)
      return;

    /* Set current track */
    
    msg = bg_msg_sink_get(t->player_ctrl.cmd_sink);
    gavl_msg_set_id_ns(msg, BG_PLAYER_CMD_PLAY_BY_ID, BG_MSG_NS_PLAYER);
    gavl_msg_set_arg_string(msg, 0, id);
    bg_msg_sink_put(t->player_ctrl.cmd_sink, msg);
    return;
    }
  
  if(!(track = gavl_get_track_by_id(a->a, id)))
    return;

  if(play)
    {
    play_id = gavl_track_get_id(track);
    }
  /* Add track */
  msg = bg_msg_sink_get(t->player_ctrl.cmd_sink);
    
  gavl_msg_set_id_ns(msg, BG_CMD_DB_SPLICE_CHILDREN, BG_MSG_NS_DB);
    
  gavl_dictionary_set_string(&msg->header, GAVL_MSG_CONTEXT_ID, BG_PLAYQUEUE_ID);
  
  if(replace)
    {
    gavl_msg_set_arg_int(msg, 0, 0);
    gavl_msg_set_arg_int(msg, 1, -1);
    }
  else
    {
    gavl_msg_set_arg_int(msg, 0, -1);
    gavl_msg_set_arg_int(msg, 1, 0);
    }
  
  gavl_msg_set_arg_dictionary(msg, 2, track);
  bg_msg_sink_put(t->player_ctrl.cmd_sink, msg);

  if(play_id)
    do_play(t, play_id);
  
  }
#endif

static gboolean list_button_press_callback(GtkWidget * w, GdkEventButton * evt,
                                           gpointer data)
  {
  GtkTreeModel * model;
  GtkTreePath * path;
  GtkTreeIter iter;

  album_t * a = data;
  bg_gtk_mdb_tree_t * t = a->t;
  
  model = gtk_tree_view_get_model(GTK_TREE_VIEW(w));

  if(gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(w),
                                   evt->x, evt->y, &path,
                                   NULL,
                                   NULL,
                                   NULL))
    gtk_tree_model_get_iter(model, &iter, path);
  else
    path = NULL;
  
  if((evt->button == 3) && (evt->type == GDK_BUTTON_PRESS))
    {
    memset(&t->menu_ctx, 0, sizeof(t->menu_ctx));

    t->menu_ctx.num_selected =
      gtk_tree_selection_count_selected_rows(gtk_tree_view_get_selection(GTK_TREE_VIEW(w)));
    
    /* Set the menu album */
    t->menu_ctx.a = a;
    t->menu_ctx.album = t->menu_ctx.a->a;
    
    /* Set selected item */
    if((t->menu_ctx.num_selected == 1) && t->menu_ctx.album)
      {
      GtkTreeIter iter;
      char * id;
      GtkTreeSelection * selection;

      selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(w));
      
      if(gtk_tree_model_get_iter_first(model, &iter))
        {
        while(1)
          {
          if(gtk_tree_selection_iter_is_selected(selection, &iter))
            {
            id = iter_to_id_list(GTK_TREE_VIEW(w), &iter);

            t->menu_ctx.item = gavl_get_track_by_id(t->menu_ctx.album, id);
            
            free(id);
            break; 
            }
          if(!gtk_tree_model_iter_next(model, &iter))
            break;
          }
        }
      //      gtk_widget_set_sensitive(w, sensitive);
      }
    
    t->menu_ctx.widget = w;
    
    bg_gtk_mdb_popup_menu(t, (const GdkEvent*)evt);
    return TRUE;
    }

  else if((evt->button == 1) && (evt->type == GDK_BUTTON_PRESS))
    {
    a->list->mouse_x = (int)evt->x;
    a->list->mouse_y = (int)evt->y;
    }
  else if((evt->button == 1) && (evt->type == GDK_2BUTTON_PRESS) && path)
    {
    char * id;
    
    id = iter_to_id_list(GTK_TREE_VIEW(w), &iter);

    if(!gavl_string_starts_with(id, BG_PLAYQUEUE_ID))
      {
      album_add(t, a->a, 1, 0);
      }
    
    do_play(t, id);
    
    //    entry_add(t, a, id, 0, 1);
    
    free(id);
    return FALSE;
    }

  if(path)
    gtk_tree_path_free(path);

  
  
  return FALSE;
  }

static gboolean list_key_press_callback(GtkWidget * w, GdkEventKey * evt,
                                        gpointer data)
  {
  album_t * a = data;

  switch(evt->keyval)
    {
    case GDK_KEY_c:
      {
      if(evt->state & GDK_CONTROL_MASK)
        {
        list_copy(a);
        }
      }
      break;
    case GDK_KEY_v:
      {
      if(evt->state & GDK_CONTROL_MASK)
        {
        list_paste(a);
        }
      }
      break;
    case GDK_KEY_Delete:
      {
      list_delete_selected(a);
      }
      break;
    case GDK_KEY_f:
      list_favorites(a);
      break;
    case GDK_KEY_a:
      {
#if 0
      if(evt->state & GDK_SHIFT_MASK)
        {
        album_add(a->t, a->a, 1, 0);
        }
      else
#endif
        {
        selected_add(a->t, a, 0, 0);
        }
      }
      break;
    case GDK_KEY_A:
      {
      album_add(a->t, a->a, 1, 0);
      }
      break;
    case GDK_KEY_Return:
      {
      if(evt->state & GDK_SHIFT_MASK)
        {
        album_add(a->t, a->a, 1, 1);
        }
      else
        {
        selected_add(a->t, a, 0, 1);
        }
      }
      break;
    case GDK_KEY_Escape:
      break;
    }
  return FALSE;
  }

/* Search equal func */

gboolean
bg_gtk_mdb_search_equal_func(GtkTreeModel *model, gint column,
                             const gchar *key, GtkTreeIter *iter,
                             gpointer search_data)
  {
  gboolean ret = TRUE; // TRUE means non-matched
  char *iter_string = NULL;

#if 0 // Substring search
  
  int key_len;
  int str_len;
  int i;
  
  key_len = strlen(key);
  
  
  gtk_tree_model_get(model, iter, column, &iter_string, -1);

  str_len = strlen(iter_string);

  for(i = 0; i < str_len - key_len; i++)
    {
    if(!strncasecmp(iter_string + i, key, key_len))
      {
      ret = FALSE;
      break;
      }
    }
#else
  gtk_tree_model_get(model, iter, column, &iter_string, -1);

  if(!strncasecmp(iter_string, key, strlen(key)))
    ret = FALSE;
#endif
  
  g_free(iter_string);
  return ret;
  }

/* List menu */

static void clipboard_received_func(GtkClipboard *clipboard,
                                    GtkSelectionData *selection_data,
                                    gpointer data)
  {
  album_t * a = data;
  insert_selection_data(a, selection_data, -1);
  }

static void list_menu_callback(GtkWidget * item, gpointer data)
  {
  //  const char * id;
  bg_gtk_mdb_tree_t * tree = data;
  
  //  fprintf(stderr, "list_menu_callback %s\n");

  if(item == tree->menu.album_menu.save_item)
    {
    //    fprintf(stderr, "Save album\n");
    save_tracks(tree);
    }
  else if(item == tree->menu.album_menu.info_item)
    {
    //  fprintf(stderr, "Album info %p\n", tree->menu_ctx.album);
    if(tree->menu_ctx.album)
      bg_gtk_trackinfo_show(tree->menu_ctx.album, tree->menu_ctx.widget);
    
    }
  else if(item == tree->menu.album_menu.sort_item)
    {
    fprintf(stderr, "Sort %p\n", tree->menu_ctx.item);
    
    if(tree->menu_ctx.item)
      {
      const char * id;
      gavl_msg_t * msg;
      bg_msg_sink_t * sink;
      
      if(tree->menu_ctx.a)
        id = tree->menu_ctx.a->id;
      else if(tree->menu_ctx.item)
        id = gavl_track_get_id(tree->menu_ctx.item);

      sink = tree->ctrl.cmd_sink;

      if(!strcmp(id, BG_PLAYQUEUE_ID))
        sink = tree->player_ctrl.cmd_sink;
      
      msg = bg_msg_sink_get(sink);
      gavl_msg_set_id_ns(msg, BG_CMD_DB_SORT, BG_MSG_NS_DB);

      if(tree->menu_ctx.a)
        gavl_dictionary_set_string(&msg->header, GAVL_MSG_CONTEXT_ID, tree->menu_ctx.a->id);
      else if(tree->menu_ctx.item)
        gavl_dictionary_set_string(&msg->header, GAVL_MSG_CONTEXT_ID, gavl_track_get_id(tree->menu_ctx.item));
      
      bg_msg_sink_put(sink, msg);
      }
    }
  else if(item == tree->menu.album_menu.add_item)
    {
    //    fprintf(stderr, "Add album\n");
    if(tree->menu_ctx.a)
      album_add(tree, tree->menu_ctx.a->a, 1, 0);
    }
  else if(item == tree->menu.album_menu.play_item)
    {
    //    fprintf(stderr, "Play album\n");
    if(tree->menu_ctx.a)
      album_add(tree, tree->menu_ctx.a->a, 1, 1);
    }
  else if(item == tree->menu.track_menu.add_item)
    {
    if(tree->menu_ctx.a)
      selected_add(tree, tree->menu_ctx.a, 0, 0);
    }
  else if(item == tree->menu.track_menu.play_item)
    {
    if(tree->menu_ctx.a)
      selected_add(tree, tree->menu_ctx.a, 0, 1);
    }
  else if(item == tree->menu.track_menu.favorites_item)
    {
    if(tree->menu_ctx.a)
      list_favorites(tree->menu_ctx.a);
    }
  else if(item == tree->menu.album_menu.load_files_item)
    {
    //    fprintf(stderr, "Load files\n");
    load_files(tree);
    }
  else if(item == tree->menu.album_menu.load_url_item)
    {
    //    fprintf(stderr, "Load URL\n");
    load_uri(tree);
    }
  else if(item == tree->menu.album_menu.new_playlist_item)
    {
    create_playlist(tree);
    }
  else if(item == tree->menu.album_menu.new_container_item)
    {
    create_folder(tree);
    }
  else if(item == tree->menu.album_menu.delete_item)
    {
    fprintf(stderr, "Delete album\n");
    bg_gtk_mdb_tree_delete_selected_album(tree);
    }
  else if((item == tree->menu.track_menu.info_item))
    {
    fprintf(stderr, "Info\n");

    //    void bg_gtk_trackinfo_show(const gavl_dictionary_t * m,
    //                               GtkWidget * parent)
    
    if(tree->menu_ctx.item)
      bg_gtk_trackinfo_show(tree->menu_ctx.item, tree->menu_ctx.widget);
    }
  else if((item == tree->menu.track_menu.copy_item))
    {
    list_copy(tree->menu_ctx.a);
    }
  else if((item == tree->menu.track_menu.save_item))
    {
    // fprintf(stderr, "Save\n");
    save_selected(tree);
    
    }
  else if((item == tree->menu.track_menu.paste_item))
    {
    list_paste(tree->menu_ctx.a);
    }
  else if((item == tree->menu.track_menu.delete_item))
    {
    //    fprintf(stderr, "Delete\n");

    if(tree->menu_ctx.a)
      list_delete_selected(tree->menu_ctx.a);
    }
  
  }

static GtkWidget *
create_list_menu_item(bg_gtk_mdb_tree_t * tree, GtkWidget * parent, const char * icon,
                      const char * label, guint accelerator_key, GdkModifierType accelerator_mods)
  {
  GtkWidget * ret;
  GtkWidget *child;

  gchar * text_escaped;
  gchar * markup;

  ret = gtk_menu_item_new_with_label("");

  g_signal_connect(G_OBJECT(ret), "activate", G_CALLBACK(list_menu_callback), tree);
  
  text_escaped = g_markup_escape_text(label, -1);

  if(icon)
    markup = (gchar*)bg_sprintf("<span font_family=\"%s\" weight=\"normal\">%s</span>  %s",
                                BG_ICON_FONT_FAMILY, icon, text_escaped);
  else
    markup = gavl_strdup(text_escaped);
  
  child = gtk_bin_get_child(GTK_BIN(ret));
  gtk_label_set_markup(GTK_LABEL(child), markup);

  g_free(text_escaped);
  free(markup);
  
  gtk_accel_label_set_accel(GTK_ACCEL_LABEL(child), accelerator_key, accelerator_mods);

  gtk_menu_shell_append(GTK_MENU_SHELL(parent), ret);
  gtk_widget_show(ret);
  return ret;
  }

void bg_gtk_mdb_menu_init(menu_t * m, bg_gtk_mdb_tree_t * tree)
  {
  m->menu = gtk_menu_new();
  
  m->album_menu.menu = gtk_menu_new();
  m->album_menu.add_item = create_list_menu_item(tree, m->album_menu.menu, BG_ICON_ADD,   "Enqueue", GDK_KEY_a, GDK_SHIFT_MASK);
  m->album_menu.play_item = create_list_menu_item(tree, m->album_menu.menu, BG_ICON_PLAY, "Play", GDK_KEY_Return, GDK_SHIFT_MASK);
  
  m->album_menu.save_item = create_list_menu_item(tree, m->album_menu.menu, BG_ICON_SAVE, "Save as...", 0, 0);
  m->album_menu.info_item = create_list_menu_item(tree, m->album_menu.menu, BG_ICON_INFO, "Info", 0, 0);
  
  m->album_menu.sort_item = create_list_menu_item(tree, m->album_menu.menu, BG_ICON_SORT, "Sort", 0, 0);
  m->album_menu.load_files_item = create_list_menu_item(tree, m->album_menu.menu, BG_ICON_FOLDER_OPEN, "Add file(s)", 0, 0);
  m->album_menu.load_url_item = create_list_menu_item(tree, m->album_menu.menu, BG_ICON_GLOBE, "Add URL", 0, 0);
  m->album_menu.new_playlist_item = create_list_menu_item(tree, m->album_menu.menu, BG_ICON_PLAYLIST, "New playlist...", 0, 0);
  m->album_menu.new_container_item = create_list_menu_item(tree, m->album_menu.menu, BG_ICON_FOLDER, "New folder...", 0, 0);
  m->album_menu.delete_item = create_list_menu_item(tree, m->album_menu.menu, BG_ICON_TRASH, "Delete", 0, 0);
  
  gtk_widget_show(m->album_menu.menu);
  
  m->track_menu.menu = gtk_menu_new();
  m->track_menu.add_item = create_list_menu_item(tree, m->track_menu.menu, BG_ICON_ADD, "Enqueue", GDK_KEY_a, 0);
  m->track_menu.play_item = create_list_menu_item(tree, m->track_menu.menu, BG_ICON_PLAY, "Play", GDK_KEY_Return, 0);
  m->track_menu.favorites_item = create_list_menu_item(tree, m->track_menu.menu, BG_ICON_HEART, "Copy to favorites", GDK_KEY_f, 0);
  
  m->track_menu.info_item = create_list_menu_item(tree, m->track_menu.menu, BG_ICON_INFO, "Info", 0, 0);
  m->track_menu.copy_item = create_list_menu_item(tree, m->track_menu.menu, BG_ICON_COPY, "Copy", GDK_KEY_c, GDK_CONTROL_MASK);
  m->track_menu.paste_item = create_list_menu_item(tree, m->track_menu.menu, BG_ICON_PASTE, "Paste", GDK_KEY_v, GDK_CONTROL_MASK);
  m->track_menu.save_item = create_list_menu_item(tree, m->track_menu.menu, BG_ICON_SAVE, "Save as...", 0, 0);
  m->track_menu.delete_item = create_list_menu_item(tree, m->track_menu.menu, BG_ICON_TRASH, "Delete...", GDK_KEY_Delete, 0);
  gtk_widget_show(m->track_menu.menu);

  m->album_item = create_list_menu_item(tree, m->menu, NULL, "Album...", 0, 0);
  m->track_item = create_list_menu_item(tree, m->menu, NULL, "Track(s)...", 0, 0);
  
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(m->album_item), m->album_menu.menu);
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(m->track_item), m->track_menu.menu);

  
  }


static gboolean
motion_callback(GtkWidget * w, GdkEventMotion * evt,
                gpointer user_data)
  {
  //  GdkDragContext* ctx;
  GtkTargetList * tl;
  GtkTreeSelection * selection;
  int num_selected;

  album_t * a = user_data;
  
  selection =
    gtk_tree_view_get_selection(GTK_TREE_VIEW(a->list->listview));
  
  num_selected = gtk_tree_selection_count_selected_rows(selection);

  tl = gtk_target_list_new(list_src_entries, num_list_src_entries);
  
  if(evt->state & GDK_BUTTON1_MASK)
    {
    if((abs((int)(evt->x) - a->list->mouse_x) + abs((int)(evt->y) - a->list->mouse_y) < 10) ||
       (!num_selected))
      return FALSE;

    // ctx = 
    gtk_drag_begin_with_coordinates(w, tl, GDK_ACTION_COPY, 1, (GdkEvent*)evt, -1, -1);
    
    }

  gtk_target_list_unref(tl);
  
  return TRUE;
  }


static void insert_selection_data(album_t * a, GtkSelectionData *data,
                                  int idx)
  {
  const guchar * str = NULL;
  gchar * target_name = NULL;
  bg_msg_sink_t * sink = a->t->ctrl.cmd_sink;
  
  if(!(target_name = gdk_atom_name(gtk_selection_data_get_target(data))))
    goto fail;

  if(a == &a->t->playqueue)
    sink = a->t->player_ctrl.cmd_sink;
  
  if(!strcmp(target_name, "text/uri-list") ||
     !strcmp(target_name, "text/plain"))
    {
    int i;
    char ** list;
    gavl_array_t arr;
    gavl_msg_t * msg;
    int len = gtk_selection_data_get_length(data);
    
    if(!(str = gtk_selection_data_get_data(data)))
      goto fail;

    list = bg_urilist_decode((char*)str, len);

    gavl_array_init(&arr);
    i = 0;

    while(list[i])
      {
      gavl_value_t val;
      gavl_value_init(&val);
      gavl_value_set_string(&val, list[i]);
      gavl_array_splice_val_nocopy(&arr, i, 0, &val);

      // fprintf(stderr, "Insert selection data: %s\n", list[i]);
      
      i++;
      }

    msg = bg_msg_sink_get(sink);
    
    gavl_msg_set_id_ns(msg, BG_CMD_DB_LOAD_URIS, BG_MSG_NS_DB);
        
    gavl_dictionary_set_string(&msg->header, GAVL_MSG_CONTEXT_ID,
                               gavl_track_get_id(a->a));
    
    gavl_msg_set_arg_int(msg, 0, idx);
    gavl_msg_set_arg_array(msg, 1, &arr);
    bg_msg_sink_put(sink, msg);
    
    gavl_array_free(&arr);
    bg_urilist_free(list);
    }
  else if(!strcmp(target_name, bg_gtk_atom_tracks_name))
    {
    gavl_msg_t * msg;
    gavl_dictionary_t dict;

    int len = gtk_selection_data_get_length(data);

    if(!(str = gtk_selection_data_get_data(data)))
      goto fail;

    gavl_dictionary_init(&dict);
    bg_tracks_from_string(&dict, BG_TRACK_FORMAT_GMERLIN, (char*)str, len);

    /* Send splice message */
    msg = bg_msg_sink_get(sink);

    gavl_msg_set_id_ns(msg, BG_CMD_DB_SPLICE_CHILDREN, BG_MSG_NS_DB);

    gavl_dictionary_set_string(&msg->header, GAVL_MSG_CONTEXT_ID,
                               gavl_track_get_id(a->a));

    gavl_msg_set_arg_int(msg, 0, idx);
    gavl_msg_set_arg_int(msg, 1, 0);
    gavl_msg_set_arg_array(msg, 2, gavl_get_tracks(&dict));
    bg_msg_sink_put(sink, msg);
    
    /* Cleanup */
    gavl_dictionary_free(&dict);
    }
  
  fail:
  
  if(target_name)
    g_free(target_name);
  }

static gboolean drag_motion_callback(GtkWidget *widget,
                                     GdkDragContext *drag_context,
                                     gint x,
                                     gint y,
                                     guint time,
                                     gpointer d)
  {
  GtkTreePath * path = NULL;
  GtkTreeViewDropPosition pos;
  gint * indices;
  album_t * a = d;
  
  gdk_drag_status(drag_context, gdk_drag_context_get_suggested_action(drag_context), time);
  
  if(gtk_tree_view_get_dest_row_at_pos(GTK_TREE_VIEW(a->list->listview),
                                       x, y, &path,
                                       &pos))
    {
    if(pos == GTK_TREE_VIEW_DROP_INTO_OR_BEFORE)
      pos = GTK_TREE_VIEW_DROP_BEFORE;
    else if(pos == GTK_TREE_VIEW_DROP_INTO_OR_AFTER)
      pos = GTK_TREE_VIEW_DROP_AFTER;

    indices = gtk_tree_path_get_indices(path);
    if(pos == GTK_TREE_VIEW_DROP_BEFORE)
      a->list->drag_pos = indices[0];
    else
      a->list->drag_pos = indices[0]+1;
    
    gtk_tree_view_set_drag_dest_row(GTK_TREE_VIEW(a->list->listview),
                                    path, pos);

    

    }
  
  if(path)
    gtk_tree_path_free(path);
  
  return TRUE;
  }

static gboolean drag_drop_callback(GtkWidget *widget,
                                   GdkDragContext *drag_context,
                                   gint x,
                                   gint y,
                                   guint time,
                                   gpointer d)
  {
  GdkAtom target_type = GDK_NONE;
  GList * targets;
  int fmt = -1;
  album_t * a = d;
  
  a->list->drop_time = time;

  if((targets = gdk_drag_context_list_targets(drag_context)))
    {
    gpointer ptr;
    int i = 0;
    int fmt_test;
    GdkAtom target_type_test;
    
    while(1)
      {
      if(!(ptr = g_list_nth_data(targets, i)))
        break;

      target_type_test = GDK_POINTER_TO_ATOM(ptr);
      fmt_test = dst_track_format_from_atom(target_type_test);

      if((fmt_test > 0) && ((fmt < 0) || (fmt_test < fmt)))
        {
        target_type = target_type_test;
        fmt = fmt_test;
        }
      i++;
      }
    }
  
  if(target_type != GDK_NONE)
    {
    gtk_drag_get_data(widget,         /* will receive 'drag-data-received' signal */
                      drag_context,   /* represents the current state of the DnD */
                      target_type,    /* the target type we want */
                      time);          /* time stamp */
    return TRUE;
    }
  else
    return FALSE;
  }

static void drag_received_callback(GtkWidget *widget,
                                   GdkDragContext *drag_context,
                                   gint x,
                                   gint y,
                                   GtkSelectionData *data,
                                   guint info,
                                   guint time,
                                   gpointer d)
  {
  int do_delete = 0;
  album_t * a = d;
  drag_motion_callback(widget, drag_context, x, y, time, d);
  insert_selection_data(a, data, a->list->drag_pos);
  a->list->drag_pos = -1;

  /* Tell source we are ready */
  gtk_drag_finish(drag_context,
                  TRUE, /* Success */
                  do_delete, /* Delete */
                  a->list->drop_time);

  if(gtk_drag_get_source_widget(drag_context) == widget)
    a->list->move = 1;
  }


static void drag_delete_callback(GtkWidget *widget,
                                 GdkDragContext *drag_context,
                                 gpointer user_data)
  {
  //  album_t * a = user_data;
  
  fprintf(stderr, "Drag delete\n");
  
  }

static void drag_end_callback(GtkWidget *widget,
                              GdkDragContext *drag_context,
                              gpointer user_data)
  {
  //  album_t * a = user_data;

  //  fprintf(stderr, "Drag end\n");
  }

static void drag_get_callback(GtkWidget *widget,
                              GdkDragContext *drag_context,
                              GtkSelectionData *data,
                              guint info,
                              guint time,
                              gpointer user_data)
  {
  char * str;
  GdkAtom type_atom;
  gchar * target_name = NULL;
  gavl_dictionary_t * sel = NULL;
  album_t * a = user_data;
  
  type_atom = gdk_atom_intern("STRING", FALSE);
  if(!type_atom)
    return;
  
  if(!(target_name = gdk_atom_name(gtk_selection_data_get_target(data))))
    goto fail;
  
  sel = list_extract_selected(a);
  
  str = bg_tracks_to_string(sel, info, 1, 0);

  if(str)
    {
    gtk_selection_data_set(data, type_atom, 8, (uint8_t*)str, strlen(str));
    free(str);
    }
  else
    gtk_selection_data_set(data, type_atom, 8, NULL, 0);
  
  fail:

  if(target_name)
    g_free(target_name);

  if(sel)
    gavl_dictionary_destroy(sel);

  }

list_t * bg_gtk_mdb_list_create(album_t * a)
  {
  GtkWidget * scrolledwindow;
  GtkListStore * store;
  GtkCellRenderer   *text_renderer;
  GtkCellRenderer   *pixmap_renderer;
  GtkTreeViewColumn *column;
  char * tmp_string;
  list_t * l = calloc(1, sizeof(*l));
  
  l->a = a;
  l->drag_pos = -1;
  
  /* Close button */
  l->close_button = make_close_button(a->t);
  
  /* Tab label */

  l->tab_label     = gtk_label_new(NULL);
  l->tab_icon      = gtk_label_new(NULL);
  l->tab_image     = gtk_image_new_from_pixbuf(NULL);
  l->tab_label_cur = gtk_label_new(NULL);

  tmp_string = bg_sprintf("<span size=\"xx-large\" font_family=\"%s\" weight=\"normal\">%s</span>",
                          BG_ICON_FONT_FAMILY, BG_ICON_PLAY);
  
  gtk_label_set_markup(GTK_LABEL(l->tab_label_cur), tmp_string);
  free(tmp_string);
  
  g_object_ref(G_OBJECT(l->tab_icon));
  g_object_ref(G_OBJECT(l->tab_image));
  
  gtk_widget_show(l->tab_icon);

  /* Menu label */

  l->menu_label = gtk_label_new(NULL);
  gtk_widget_show(l->menu_label);
  
  gtk_widget_show(l->tab_label);

  /* Tab widget */
  l->tab = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);

  gtk_container_add(GTK_CONTAINER(l->tab), l->tab_icon);
  gtk_container_add(GTK_CONTAINER(l->tab), l->tab_image);
  
  gtk_container_add(GTK_CONTAINER(l->tab), l->tab_label);
  gtk_container_add(GTK_CONTAINER(l->tab), l->tab_label_cur);
  gtk_container_add(GTK_CONTAINER(l->tab), l->close_button);
  gtk_widget_show(l->tab);
  
  store = gtk_list_store_new(NUM_LIST_COLUMNS,
                             G_TYPE_STRING,  // icon
                             G_TYPE_BOOLEAN, // has_icon
                             GDK_TYPE_PIXBUF,// pixbuf
                             G_TYPE_BOOLEAN, // has_pixbuf
                             G_TYPE_STRING,  // label
                             G_TYPE_STRING,  // color
                             G_TYPE_STRING,  // id
                             G_TYPE_STRING,  // search_string
                             G_TYPE_STRING   // current
                             );

  l->listview = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));

  gtk_widget_add_events(l->listview, GDK_KEY_PRESS_MASK |
                        GDK_BUTTON_PRESS_MASK | GDK_BUTTON1_MOTION_MASK);
  
  g_signal_connect(G_OBJECT(l->listview), "button-press-event",
                   G_CALLBACK(list_button_press_callback), a);

  g_signal_connect(G_OBJECT(l->listview), "key-press-event",
                   G_CALLBACK(list_key_press_callback), a);

  g_signal_connect(G_OBJECT(l->listview), "drag-data-received",
                   G_CALLBACK(drag_received_callback),
                   (gpointer)a);
  g_signal_connect(G_OBJECT(l->listview), "drag-drop",
                   G_CALLBACK(drag_drop_callback),
                   (gpointer)a);
  g_signal_connect(G_OBJECT(l->listview), "drag-motion",
                   G_CALLBACK(drag_motion_callback),
                   (gpointer)a);
  g_signal_connect(G_OBJECT(l->listview), "drag-data-get",
                   G_CALLBACK(drag_get_callback),
                   (gpointer)a);
  g_signal_connect(G_OBJECT(l->listview), "drag-data-delete",
                   G_CALLBACK(drag_delete_callback),
                   (gpointer)a);
  g_signal_connect(G_OBJECT(l->listview), "drag-end",
                   G_CALLBACK(drag_end_callback),
                   (gpointer)a);
  
  g_signal_connect(G_OBJECT(l->listview), "motion-notify-event",
                   G_CALLBACK(motion_callback), (gpointer)a);
  
  gtk_tree_view_set_search_column(GTK_TREE_VIEW(l->listview), LIST_COLUMN_SEARCH_STRING);
  gtk_tree_view_set_search_equal_func(GTK_TREE_VIEW(l->listview), bg_gtk_mdb_search_equal_func,
                                      NULL, NULL);


  gtk_tree_view_set_enable_search(GTK_TREE_VIEW(l->listview), FALSE);
  
  /* Icon */
  column = gtk_tree_view_column_new();

  gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
  
  text_renderer = gtk_cell_renderer_text_new();
  g_object_set(G_OBJECT(text_renderer), "family", BG_ICON_FONT_FAMILY, NULL);
  
  //  g_object_set(G_OBJECT(text_renderer), "scale", 1.5, NULL);

  g_object_set(G_OBJECT(text_renderer), "size-points", 24.0, NULL);
  g_object_set(G_OBJECT(text_renderer), "xalign", 0.5, NULL);
  g_object_set(G_OBJECT(text_renderer), "yalign", 0.5, NULL);
  
  gtk_tree_view_column_pack_start(column, text_renderer, FALSE);
  
  gtk_tree_view_column_add_attribute(column, text_renderer,
                                     "foreground", LIST_COLUMN_COLOR);
  gtk_tree_view_column_add_attribute(column, text_renderer,
                                     "text", LIST_COLUMN_ICON);
  gtk_tree_view_column_add_attribute(column, text_renderer,
                                     "visible", LIST_COLUMN_HAS_ICON);
  
  /* Icon (pixmap) */
  pixmap_renderer = gtk_cell_renderer_pixbuf_new();
  gtk_tree_view_column_pack_start(column, pixmap_renderer, FALSE);
  gtk_tree_view_column_add_attribute(column, pixmap_renderer, "pixbuf", LIST_COLUMN_PIXBUF);
  gtk_tree_view_column_add_attribute(column, pixmap_renderer, "visible", LIST_COLUMN_HAS_PIXBUF);

  g_object_set(G_OBJECT(pixmap_renderer), "xalign", 0.5, NULL);
  g_object_set(G_OBJECT(pixmap_renderer), "yalign", 0.5, NULL);
  //  g_object_set(G_OBJECT(pixmap_renderer), "xpad", 1, NULL);
  g_object_set(G_OBJECT(pixmap_renderer), "ypad", 1, NULL);
  
  gtk_tree_view_column_set_fixed_width(GTK_TREE_VIEW_COLUMN(column), LIST_ICON_WIDTH+2);
  
  gtk_tree_view_append_column (GTK_TREE_VIEW(l->listview), column);

  
  
  
  /* Label */
  column = gtk_tree_view_column_new();

  text_renderer = gtk_cell_renderer_text_new();

  //  gtk_cell_renderer_text_set_fixed_height_from_font(text_renderer, 3);
  
  g_object_set(G_OBJECT(text_renderer), "ellipsize", PANGO_ELLIPSIZE_END, NULL);
  g_object_set(G_OBJECT(text_renderer), "yalign", 0.5, NULL);
  
  gtk_tree_view_column_pack_start(column, text_renderer, FALSE);

  gtk_tree_view_column_add_attribute(column, text_renderer,
                                     "foreground", LIST_COLUMN_COLOR);
  gtk_tree_view_column_add_attribute(column, text_renderer,
                                     "markup", LIST_COLUMN_LABEL);

  gtk_tree_view_column_set_expand(column, TRUE);
  
  gtk_tree_view_append_column (GTK_TREE_VIEW(l->listview), column);
  
  /* Playing */
  column = gtk_tree_view_column_new();
  text_renderer = gtk_cell_renderer_text_new();
  
  gtk_tree_view_column_pack_start(column, text_renderer, FALSE);
  
  //  gtk_tree_view_column_add_attribute(column, text_renderer, "foreground", LIST_COLUMN_COLOR);
  gtk_tree_view_column_add_attribute(column, text_renderer, "text", LIST_COLUMN_CURRENT);
  g_object_set(G_OBJECT(text_renderer), "family", BG_ICON_FONT_FAMILY, NULL);
  g_object_set(G_OBJECT(text_renderer), "scale", 2.0, NULL);
  
  gtk_tree_view_append_column (GTK_TREE_VIEW(l->listview), column);

  /* Columns done */
  
  gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(l->listview), FALSE);
  gtk_tree_selection_set_mode(gtk_tree_view_get_selection(GTK_TREE_VIEW(l->listview)),
                              GTK_SELECTION_MULTIPLE);

  
  /* Pack and show */
  gtk_widget_show(l->listview);

  scrolledwindow =
    gtk_scrolled_window_new(gtk_scrollable_get_hadjustment(GTK_SCROLLABLE(l->listview)),
                            gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(l->listview)));

  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolledwindow),
                                 GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);
  gtk_container_add(GTK_CONTAINER(scrolledwindow), l->listview);
  gtk_widget_show(scrolledwindow);

  l->widget = scrolledwindow;

  if(a->a)
    bg_gdk_mdb_list_set_obj(l, a->a);
  
  if(!a->a)
    bg_gtk_mdb_browse_object(a->t, a->id);
  else
    {
    int num;
    int total;

    num = gavl_get_num_tracks_loaded(a->a, &total);
    
    if(total)
      {
      if(!num)
        bg_gtk_mdb_browse_children(a->t, a->id);
      else
        bg_gtk_mdb_list_splice_children(l, 0, 0, gavl_dictionary_get(a->a, GAVL_META_TRACKS), 0);
      }
    }

  /* */
  
  return l;
  }



static void selection_foreach_func(GtkTreeModel *model,
                                   GtkTreePath *path,
                                   GtkTreeIter *iter,
                                   gpointer data)
  {
  gavl_value_t val;
  gchar * id = NULL;
  gavl_array_t * arr = data;
  
  gtk_tree_model_get(model, iter, LIST_COLUMN_ID, &id, -1);

  gavl_value_init(&val);
  gavl_value_set_string(&val, id);
  gavl_array_splice_val_nocopy(arr, -1, 0, &val);
  g_free(id);
  }

void bg_gtk_mdb_list_splice_children(list_t * l, int idx, int del, const gavl_value_t * add,
                                     int splice_internal)
  {
  gavl_array_t selected_rows;

  GtkTreeIter iter;
  GtkTreeModel * model;
  GtkTreeSelection * selection;

  int i;
  int num_added = 0;
  int real_idx;
  
  const gavl_array_t * arr;
  
  selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(l->listview)); 
  model = gtk_tree_view_get_model(GTK_TREE_VIEW(l->listview));

  /* Save selection */
  gavl_array_init(&selected_rows);
  gtk_tree_selection_selected_foreach(selection, selection_foreach_func,
                                      &selected_rows);  
  

  /* Calculate base index */

  real_idx = 0;
  
  arr = gavl_get_tracks(l->a->a);

  if(arr)
    {
    for(i = 0; i < idx; i++)
      {
      if(is_item(&arr->entries[i]))
        real_idx++;
      }
    }
  
  /* Remove entries */

  if(del > 0)
    {
    if(gtk_tree_model_iter_nth_child(model, &iter, NULL, real_idx))
      {
      for(i = 0; i < del; i++)
        {
        if(is_item(&arr->entries[i+idx]))
          {
          if(!gtk_list_store_remove(GTK_LIST_STORE(model), &iter))
            break;
          }
        }
      
      }

    }
  else if(del < 0)
    {
    if(gtk_tree_model_iter_nth_child(model, &iter, NULL, real_idx))
      {
      while(gtk_list_store_remove(GTK_LIST_STORE(model), &iter))
        ;
      }
    }
  
  /* Add entries */

  if(add)
    {
    if((arr = gavl_value_get_array(add)))
      {
      for(i = 0; i < arr->num_entries; i++)
        {
        if(is_item(arr->entries + i))
          {
          gtk_list_store_insert(GTK_LIST_STORE(model), &iter, real_idx + num_added);
          set_entry_list(l, gavl_value_get_dictionary(arr->entries + i), &iter);
          num_added++;
          }
        }
      }
    else if(is_item(add))
      {
      gtk_list_store_insert(GTK_LIST_STORE(model), &iter, real_idx);
      set_entry_list(l, gavl_value_get_dictionary(add), &iter);
      num_added = 1;
      }

    }
  
  
  /* Restore selection */
  gtk_tree_selection_unselect_all(selection);
  for(i = 0; i < selected_rows.num_entries; i++)
    {
    if(bg_gtk_mdb_list_id_to_iter(GTK_TREE_VIEW(l->listview), &iter, gavl_value_get_string(&selected_rows.entries[i])))
      gtk_tree_selection_select_iter(selection, &iter);
    }

  /* Must be called exactly here */

  //  num = gavl_get_num_tracks_loaded(l->a->a, &total);
  //  if(num < total)

  if(splice_internal)
    gavl_track_splice_children(l->a->a, idx, del, add);
  
  if(l->move && (num_added > 0))
    {
    GtkTreePath * start;
    GtkTreePath * end;
    list_delete_selected(l->a);
    
//     gtk_tree_selection_select_all(selection);

    start = gtk_tree_path_new_from_indices(idx, -1);
    end   = gtk_tree_path_new_from_indices(idx + num_added - 1, -1);

    // fprintf(stderr, "Select range: %d %d\n", idx, idx + num_added);
    
    gtk_tree_selection_select_range(selection, start, end);
    
    gtk_tree_path_free(start);
    gtk_tree_path_free(end);
    
    l->move = 0;
    }
   
  gavl_array_free(&selected_rows); 
  
  bg_gtk_widget_queue_redraw(l->listview);
  }


static const struct
  {
  const char * label;
  const char * pattern;
  int type;
  }
save_formats[] = 
  {
    { "Gmerlin", "*.tracks", BG_TRACK_FORMAT_GMERLIN },
    { "XSPF",    "*.xspf",   BG_TRACK_FORMAT_XSPF    },
    { "M3U",     "*.m3u",    BG_TRACK_FORMAT_M3U     },
    { "PLS",     "*.pls",    BG_TRACK_FORMAT_PLS     },
    { /* End */                                           }
  };

static int filesel_get_filter(GtkFileChooser *filechooser)
  {
  int i;
  const char * filter_name;
  GtkFileFilter * filter = gtk_file_chooser_get_filter(filechooser);
  
  i = 0;
  filter_name = gtk_file_filter_get_name(filter);

  while(save_formats[i].label)
    {
    if(!strcmp(save_formats[i].label, filter_name))
      return i;
    i++;
    }
  return -1;  
  }

static void filesel_activate_callback(GtkWidget *button,
                                      void * data)
  {
  int idx;
  GtkFileChooser *filechooser = data;
  gchar *name = gtk_file_chooser_get_current_name(filechooser);

  if((idx = filesel_get_filter(filechooser)) < 0)
    return;
  
  if (!g_str_has_suffix (name, save_formats[idx].pattern + 1))
    {
    gchar *new_name = g_strconcat(name, save_formats[idx].pattern + 1, NULL);
    gtk_file_chooser_set_current_name(filechooser, new_name);
    g_free(new_name);
    }
  g_free(name);
  }

static char * get_save_filename(bg_gtk_mdb_tree_t * tree,
                                const char * dialog_title, int * type)
  {
  int i;
  int result;
  GtkWidget * filechooser;
  GtkWidget * button;
  char * filename = NULL;
  
  filechooser = gtk_file_chooser_dialog_new(dialog_title,
                                            GTK_WINDOW(bg_gtk_get_toplevel(tree->menu_ctx.widget)),
                                            GTK_FILE_CHOOSER_ACTION_SAVE,
                                            "_Cancel",
                                            GTK_RESPONSE_CANCEL,
                                            "_Save",
                                            GTK_RESPONSE_ACCEPT,
                                            NULL);
  
  gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(filechooser), TRUE);

  i = 0;

  while(save_formats[i].label)
    {
    GtkFileFilter * filter;

    filter = gtk_file_filter_new();
    gtk_file_filter_add_pattern(filter, save_formats[i].pattern);
    gtk_file_filter_set_name(filter,    save_formats[i].label);

    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(filechooser), filter);
    i++;
    }

  button = gtk_dialog_get_widget_for_response(GTK_DIALOG(filechooser), GTK_RESPONSE_ACCEPT);
  g_signal_connect(button, "activate", G_CALLBACK(filesel_activate_callback), filechooser);
  
  result = gtk_dialog_run(GTK_DIALOG(filechooser));

  if(result == GTK_RESPONSE_ACCEPT)
    {
    if((i = filesel_get_filter(GTK_FILE_CHOOSER(filechooser))) < 0)
      return NULL;

    filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(filechooser));

    *type = save_formats[i].type;
    }
  
  gtk_widget_destroy(filechooser);
  return filename;
  }

static void save_selected(bg_gtk_mdb_tree_t * tree)
  {
  int type;
  char * str;
  char * filename;
  gavl_dictionary_t * selected;
  
  if(!(filename = get_save_filename(tree, TR("Save album"), &type)))
    return;

  selected = list_extract_selected(tree->menu_ctx.a);
  
  str = bg_tracks_to_string(selected, type, 1, 0);

  gavl_dictionary_destroy(selected);

  bg_write_file(filename, str, strlen(str));
  free(str);
  g_free(filename);
  }

static void save_tracks(bg_gtk_mdb_tree_t * tree)
  {
  int type;
  char * str;
  char * filename;

  if(!(filename = get_save_filename(tree, TR("Save album"), &type)))
    return;
  
  str = bg_tracks_to_string(tree->menu_ctx.album, type, 1, 0);
  bg_write_file(filename, str, strlen(str));
  free(str);
  g_free(filename);
  }

static void load_files(bg_gtk_mdb_tree_t * tree)
  {
  int result;
  GtkWidget * filechooser;

  filechooser = gtk_file_chooser_dialog_new("Load files",
                                            GTK_WINDOW(bg_gtk_get_toplevel(tree->menu_ctx.widget)),
                                            GTK_FILE_CHOOSER_ACTION_OPEN,
                                            "_Cancel",
                                            GTK_RESPONSE_CANCEL,
                                            "_Load",
                                            GTK_RESPONSE_ACCEPT,
                                            NULL);

  gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(filechooser), TRUE);

  result = gtk_dialog_run(GTK_DIALOG(filechooser));

  if(result == GTK_RESPONSE_ACCEPT)
    {
    int i = 0;
    char * filename;
    gavl_array_t arr;
    
    GSList * filenames = gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(filechooser));

    gavl_array_init(&arr);
    while((filename = g_slist_nth_data(filenames, i)))
      {
      // fprintf(stderr, "Loading %s into %s\n", filename, gavl_track_get_id(tree->menu_ctx.album));
      gavl_string_array_add(&arr, filename);
      g_free(filename);
      i++;
      }
    g_slist_free(filenames);

#if 1
    if((arr.num_entries > 0) && tree->menu_ctx.album)
      {
      bg_msg_sink_t * sink;
      gavl_msg_t * msg;
      const char * id;
      
      sink = tree->ctrl.cmd_sink;

      id = gavl_track_get_id(tree->menu_ctx.album);

      if(!strcmp(id, BG_PLAYQUEUE_ID))
        sink = tree->player_ctrl.cmd_sink;
      
      msg = bg_msg_sink_get(sink);
      
      gavl_msg_set_id_ns(msg, BG_CMD_DB_LOAD_URIS, BG_MSG_NS_DB);
      
      gavl_dictionary_set_string(&msg->header, GAVL_MSG_CONTEXT_ID,
                                 id);
      
      gavl_msg_set_arg_int(msg, 0, -1);
      gavl_msg_set_arg_array(msg, 1, &arr);
      bg_msg_sink_put(sink, msg);
      
      //      bg_mdb_add_uris(bg_mdb_t * mdb, const char * parent_id, int idx,
      //                      gavl_array_t * uris)
      }
#endif
    gavl_array_free(&arr);
    }
  gtk_widget_destroy(filechooser); 
  }

static void set_parameter_ask_string(void * data, const char * name, const gavl_value_t * val)
  {
  char **ret = data;
  if(!name)
    return;
  *ret = gavl_strdup(gavl_value_get_string(val));
  }

static char * ask_string(GtkWidget * w, const char * title, const char * label)
  {
  bg_dialog_t * dlg;
  bg_parameter_info_t info[2];
  char * str = NULL;

  memset(&info[0], 0, sizeof(info));

  info[0].name = "val";
  info[0].long_name = (char*)label;
  info[0].type = BG_PARAMETER_STRING;
  
  dlg = bg_dialog_create(NULL, set_parameter_ask_string, &str, info, title);
  bg_dialog_show(dlg, GTK_WINDOW(bg_gtk_get_toplevel(w)));
  bg_dialog_destroy(dlg);
  return str;
  }

static void create_container(bg_gtk_mdb_tree_t * tree,
                             char * label,
                             const char * klass)
  {
  gavl_msg_t * msg;
  gavl_dictionary_t * c;
  gavl_dictionary_t * m;

  c = gavl_dictionary_create();
  m = gavl_dictionary_get_dictionary_create(c, GAVL_META_METADATA);

  gavl_dictionary_set_string(m, GAVL_META_LABEL, label);
  gavl_dictionary_set_string_nocopy(m, GAVL_META_TITLE, label);
  gavl_dictionary_set_string(m, GAVL_META_MEDIA_CLASS, klass);
  gavl_dictionary_set_int(m, GAVL_META_NUM_CHILDREN, 0);

  msg = bg_msg_sink_get(tree->ctrl.cmd_sink);
  
  gavl_msg_set_id_ns(msg, BG_CMD_DB_SPLICE_CHILDREN, BG_MSG_NS_DB);
  gavl_dictionary_set_string(&msg->header, GAVL_MSG_CONTEXT_ID, gavl_track_get_id(tree->menu_ctx.album));
  gavl_msg_set_arg_int(msg, 0, -1);
  gavl_msg_set_arg_int(msg, 1, 0);
  gavl_msg_set_arg_dictionary_nocopy(msg, 2, c);

  fprintf(stderr, "Create container\n");
  gavl_msg_dump(msg, 0);
  
  bg_msg_sink_put(tree->ctrl.cmd_sink, msg);
  }

static void create_playlist(bg_gtk_mdb_tree_t * tree)
  {
  char * str;
  
  str = ask_string(tree->menu_ctx.widget, "Create playlist", "Name");
  if(!str)
    return;

  create_container(tree, str, GAVL_META_MEDIA_CLASS_PLAYLIST);
  }

static void create_folder(bg_gtk_mdb_tree_t * tree)
  {
  char * str;
  
  str = ask_string(tree->menu_ctx.widget, "Create playlist", "Name");
  if(!str)
    return;

  create_container(tree, str, GAVL_META_MEDIA_CLASS_CONTAINER);
  }

static void load_uri(bg_gtk_mdb_tree_t * tree)
  {
  char * str;
  gavl_msg_t * msg;
  const char * id;
  bg_msg_sink_t * sink = tree->ctrl.cmd_sink;

  id = gavl_track_get_id(tree->menu_ctx.album);

  str = ask_string(tree->menu_ctx.widget, "Load URI", "URI");
  if(!str)
    return;
  
  //  fprintf(stderr, "Load URI %s\n", str);

  if(!strcmp(id, BG_PLAYQUEUE_ID))
    sink = tree->player_ctrl.cmd_sink;
  
  msg = bg_msg_sink_get(sink);
  
  gavl_msg_set_id_ns(msg, BG_CMD_DB_LOAD_URIS, BG_MSG_NS_DB);
  
  gavl_dictionary_set_string(&msg->header, GAVL_MSG_CONTEXT_ID,
                             id);
  
  gavl_msg_set_arg_int(msg, 0, -1);
  gavl_msg_set_arg_string(msg, 1, str);
  bg_msg_sink_put(sink, msg);
  
  free(str);
  }

