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



#include <stdlib.h>
#include <config.h>

#include <gtk/gtk.h>

#include <gmerlin/iconfont.h>

#include <gui_gtk/gtkutils.h>
#include <gavl/utils.h>


enum
  {
  COLUMN_1,
  COLUMN_2,
  NUM_COLUMNS
  };



struct bg_gtk_dict_view_s
  {
  GtkWidget * w;

  struct
    {
    GtkWidget * copy_all;
    GtkWidget * copy_selected;
    GtkWidget * menu;
    } menu;

  /* Clipboard */
  char * clipboard;
  int clipboard_len;
    
  };

static void set_dict_internal(GtkTreeModel * model,
                              GtkTreeIter * parent,
                              const gavl_dictionary_t * dict);


/* Clipboard */

#define TARGET_TEXT_PLAIN 1

static const GtkTargetEntry copy_paste_entries[] =
  {
    { "STRING", 0, TARGET_TEXT_PLAIN },
  };

/* Callback functions for the clipboard */

static void clipboard_get_func(GtkClipboard *clipboard,
                               GtkSelectionData *selection_data,
                               guint info,
                               gpointer data)
  {
  GdkAtom type_atom;
  bg_gtk_dict_view_t * w = data;

  type_atom = gdk_atom_intern("STRING", FALSE);
  if(!type_atom)
    return;
  
  gtk_selection_data_set(selection_data, type_atom, 8, (uint8_t*)w->clipboard,
                         w->clipboard_len);
  }

static void clipboard_clear_func(GtkClipboard *clipboard,
                                 gpointer data)
  {
  bg_gtk_dict_view_t * w = data;
  if(w->clipboard)
    {
    free(w->clipboard);
    w->clipboard_len = 0;
    w->clipboard = NULL;
    }
  }

static char * iter_to_string(bg_gtk_dict_view_t * w, char * ret,
                             int depth,
                             GtkTreeIter * iter, int append_children)
  {
  int i;
  int num_children;
  GtkTreeModel * model;
  char * str = NULL;
  GtkTreeIter child;
  
  /* */
  model = gtk_tree_view_get_model(GTK_TREE_VIEW(w->w));

  if(iter)
    {
    /*
     *  Ugly, that's right, but the code isn't meant to be run
     *  for each video pixel :)
     */

    for(i = 0; i < depth; i++)
      ret = gavl_strcat(ret, "  ");

    /* First column */
    gtk_tree_model_get(model, iter, COLUMN_1, &str, -1);

    if(str && *str)
      ret = gavl_strcat(ret, str);
    else
      {
      if(str)
        g_free(str);
      return ret;
      }
    
    g_free(str);

    str = NULL;
    /* Second column */
    gtk_tree_model_get(model, iter, COLUMN_2, &str, -1);

    if(str && *str)
      {
      ret = gavl_strcat(ret, "\t");
      ret = gavl_strcat(ret, str);
      }

    if(str)
      g_free(str);
    
    ret = gavl_strcat(ret, "\n");
    }

  if(!append_children)
    return ret;
  
  num_children = gtk_tree_model_iter_n_children(model, iter);

  if(!num_children)
    return ret;
  
  gtk_tree_model_iter_children(model, &child, iter);
  
  for(i = 0; i < num_children; i++)
    {
    ret = iter_to_string(w, ret, depth + !!(iter),
                         &child, append_children);
    gtk_tree_model_iter_next(model, &child);
    }
  
  return ret;
  
  }


static void copy_selected(bg_gtk_dict_view_t * w)
  {
  GtkTreeIter iter;
  GtkTreeSelection * selection;
  GtkClipboard *clipboard;
  
  GtkTreeModel * model;
  char * str = NULL;
  
  /* */
  model = gtk_tree_view_get_model(GTK_TREE_VIEW(w->w));
  clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
  
  selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(w->w));
  
  gtk_clipboard_set_with_data(clipboard,
                              copy_paste_entries,
                              sizeof(copy_paste_entries)/
                              sizeof(copy_paste_entries[0]),
                              clipboard_get_func,
                              clipboard_clear_func,
                              (gpointer)w);

  
  
  if(w->clipboard)
    free(w->clipboard);

  gtk_tree_selection_get_selected(selection,
                                  NULL,
                                  &iter);
  
  gtk_tree_model_get(model, &iter, COLUMN_2, &str, -1);

  w->clipboard = gavl_strdup(str);
  if(str)
    free(str);
  
  if(w->clipboard)
    w->clipboard_len = strlen(w->clipboard);
  else
    w->clipboard_len = 0;
  }



static void copy_all(bg_gtk_dict_view_t * w)
  {
  GtkClipboard *clipboard;
  clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
  gtk_clipboard_set_with_data(clipboard,
                              copy_paste_entries,
                              sizeof(copy_paste_entries)/
                              sizeof(copy_paste_entries[0]),
                              clipboard_get_func,
                              clipboard_clear_func,
                              (gpointer)w);
  
  if(w->clipboard)
    free(w->clipboard);
  w->clipboard = iter_to_string(w, NULL, 0, NULL, 1);

  if(w->clipboard)
    {
    w->clipboard_len = strlen(w->clipboard) + 1;
    }
  else
    w->clipboard_len = 0;
  }

static void menu_callback(GtkWidget * wid, gpointer data)
  {
  bg_gtk_dict_view_t * w = data;
  
  /* Add files */
  
  if(wid == w->menu.copy_all)
    copy_all(w);
  else if(wid == w->menu.copy_selected)
    copy_selected(w);
  }

static void pressed_callback(GtkGestureMultiPress *gesture,
                             int                   n_press,
                             double                x,
                             double                y,
                             gpointer              data)
  {
  bg_gtk_dict_view_t * w = data;
  gtk_menu_popup_at_pointer(GTK_MENU(w->menu.menu), NULL);
  }

static GtkWidget *
create_item(bg_gtk_dict_view_t * w, GtkWidget * parent,
            const char * label, const char * icon)
  {
  GtkWidget * ret;
  ret = bg_gtk_icon_menu_item_new(label, icon);
  
  g_signal_connect(G_OBJECT(ret), "activate", G_CALLBACK(menu_callback),
                   (gpointer)w);
  gtk_widget_show(ret);
  gtk_menu_shell_append(GTK_MENU_SHELL(parent), ret);
  return ret;
  }

static void init_menu(bg_gtk_dict_view_t * w)
  {
  w->menu.menu = gtk_menu_new();
  w->menu.copy_selected =
    create_item(w, w->menu.menu, TR("Copy selected value"), BG_ICON_COPY);
  w->menu.copy_all =
    create_item(w, w->menu.menu, TR("Copy all"), BG_ICON_COPY);
  
  }


bg_gtk_dict_view_t * bg_gtk_dict_view_create()
  {
  GtkTreeStore * store;
  GtkCellRenderer * text_renderer;
  GtkTreeViewColumn *column;
  GtkTreeSelection  *selection;
  GtkGesture *gesture;

  bg_gtk_dict_view_t * ret = calloc(1, sizeof(*ret));
  
  store = gtk_tree_store_new(NUM_COLUMNS, G_TYPE_STRING,
                             G_TYPE_STRING);

  
  
  ret->w = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
  gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(ret->w), 0);

  gesture = gtk_gesture_multi_press_new(ret->w);
  gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), GDK_BUTTON_SECONDARY);
  g_signal_connect(G_OBJECT(gesture), "pressed", G_CALLBACK(pressed_callback), ret);
  
  
  /* Column 1 */
  text_renderer = gtk_cell_renderer_text_new();
  column = gtk_tree_view_column_new();
  gtk_tree_view_column_pack_start(column, text_renderer, TRUE);
  gtk_tree_view_column_add_attribute(column, text_renderer, "text", COLUMN_1);

  gtk_tree_view_append_column (GTK_TREE_VIEW(ret->w), column);

  /* Column 2 */
  text_renderer = gtk_cell_renderer_text_new();
  column = gtk_tree_view_column_new();
  gtk_tree_view_column_pack_start(column, text_renderer, TRUE);
  gtk_tree_view_column_add_attribute(column, text_renderer, "text", COLUMN_2);
  gtk_tree_view_append_column (GTK_TREE_VIEW(ret->w), column);
  
  gtk_widget_show(ret->w);
  
  /* Selection mode */
  
  selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(ret->w));
  gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);

  init_menu(ret);
  
  return ret;
  }

GtkWidget * bg_gtk_dict_view_get_widget(bg_gtk_dict_view_t * w)
  {
  return w->w;
  }

static void set_value(GtkTreeModel * model,
                      GtkTreeIter * iter, const gavl_value_t * val)
  {
  char * tmp_string = NULL;
  switch(val->type)
    {
    case GAVL_TYPE_UNDEFINED:
      break;
    case GAVL_TYPE_INT:
    case GAVL_TYPE_LONG:
    case GAVL_TYPE_FLOAT:
    case GAVL_TYPE_STRING:
      tmp_string = gavl_value_to_string(val);
      break;
    case GAVL_TYPE_AUDIOFORMAT:
      {
      gavl_dictionary_t tmp;
      gavl_dictionary_init(&tmp);
      gavl_audio_format_to_dictionary(val->v.audioformat, &tmp);
      set_dict_internal(model, iter, &tmp);
      gavl_dictionary_free(&tmp);
      }
      break;
    case GAVL_TYPE_VIDEOFORMAT:
      {
      gavl_dictionary_t tmp;
      gavl_dictionary_init(&tmp);
      gavl_video_format_to_dictionary(val->v.videoformat, &tmp);
      set_dict_internal(model, iter, &tmp);
      gavl_dictionary_free(&tmp);
      }
      break;
    case GAVL_TYPE_COLOR_RGB:
      tmp_string = gavl_sprintf("%f %f %f",
                              val->v.color[0],
                              val->v.color[1],
                              val->v.color[2]);
      break;
    case GAVL_TYPE_COLOR_RGBA:
      tmp_string = gavl_sprintf("%f %f %f %f",
                              val->v.color[0],
                              val->v.color[1],
                              val->v.color[2],
                              val->v.color[3]);
      break;
    case GAVL_TYPE_POSITION:
      tmp_string = gavl_sprintf("%f %f",
                              val->v.color[0],
                              val->v.color[1]);
      break;
    case GAVL_TYPE_DICTIONARY:
      {
      set_dict_internal(model, iter,  val->v.dictionary);
      }
      break;
    case GAVL_TYPE_ARRAY:
      {
      GtkTreeIter child;
      const gavl_array_t * arr;
      int i;

      arr = val->v.array;

      for(i = 0; i < arr->num_entries; i++)
        {
        gtk_tree_store_append(GTK_TREE_STORE(model), &child, iter);
        tmp_string = gavl_sprintf("#%d", i+1);
        gtk_tree_store_set(GTK_TREE_STORE(model), &child, COLUMN_1,
                           tmp_string, -1);
        free(tmp_string);
        tmp_string = NULL;
        set_value(model, &child, &arr->entries[i]);
        
        }
      }
      break;
    case GAVL_TYPE_BINARY:
      {
      tmp_string = gavl_sprintf("Binary data (%d bytes)", val->v.buffer->len);
      }
      
    }

  if(tmp_string)
    {
    gtk_tree_store_set(GTK_TREE_STORE(model), iter, COLUMN_2, tmp_string, -1);
    free(tmp_string);
    }
  
  }

static void set_dict_internal(GtkTreeModel * model,
                              GtkTreeIter * parent,
                              const gavl_dictionary_t * dict)
  {
  int i;
  GtkTreeIter item;

  //  fprintf(stderr, "set_dict_internal:\n");
  //  gavl_dictionary_dump(dict, 2);
  
  for(i = 0; i < dict->num_entries; i++)
    {
    gtk_tree_store_append(GTK_TREE_STORE(model), &item, parent);
    gtk_tree_store_set(GTK_TREE_STORE(model), &item, COLUMN_1,
                       dict->entries[i].name, -1);

    set_value(model, &item, &dict->entries[i].v);
    }
  }


void bg_gtk_dict_view_set_dict(bg_gtk_dict_view_t * w, const gavl_dictionary_t * dict)
  {
  GtkTreeModel * model;
  
  model = gtk_tree_view_get_model(GTK_TREE_VIEW(w->w));
  gtk_tree_store_clear(GTK_TREE_STORE(model));
  set_dict_internal(model, NULL, dict);
  }

void bg_gtk_dict_view_destroy(bg_gtk_dict_view_t * w)
  {
  clipboard_clear_func(NULL, w);
  free(w);
  }
