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




#include <stdio.h>
#include <string.h>

#include "gtk_dialog.h"
#include <gmerlin/utils.h>
#include <gmerlin/iconfont.h>
#include <gui_gtk/multiinfo.h>
#include <gui_gtk/gtkutils.h>

#include <gui_gtk/gtkutils.h>

enum
  {
    COLUMN_LABEL,
    NUM_COLUMNS
  };

typedef struct list_priv_s list_priv_t;


struct list_priv_s
  {
  GtkWidget * treeview;

#if 0  
  GtkWidget * top_button;
  GtkWidget * bottom_button;
  GtkWidget * up_button;
  GtkWidget * down_button;
#endif
  
  GtkWidget * add_button;
  GtkWidget * remove_button;

  GtkWidget * scrolled;
  GtkWidget * label;

  void * data;
  int selected;
  
  gavl_array_t arr;
  //  int array_changed;
  
  const char * translation_domain;
  int is_dir;
  };

static void set_value(bg_gtk_widget_t * w)
  {
  gavl_array_t * arr;
  list_priv_t * priv = w->priv;

  // fprintf(stderr, "dirlist: set_value\n");
  
  arr = gavl_value_set_array(&w->value);
  gavl_array_copy(arr, &priv->arr);
  }

static void splice(bg_gtk_widget_t * w, int idx, int num_delete, int num_add)
  {
  int i;
  GtkTreeIter iter;
  GtkTreeModel * model;
  int num;
  list_priv_t * priv = w->priv;

  //  fprintf(stderr, "splice %d %d %d\n", idx, num_delete, num_add);
  
  model = gtk_tree_view_get_model(GTK_TREE_VIEW(priv->treeview));
  
  num = gtk_tree_model_iter_n_children(model, NULL);
  
  /* Delete */
  if(num_delete)
    {
    if(num_delete < 0)
      num_delete = num - idx;
    
    if(idx == num)
      num_delete = 0;
    
    if(num_delete && !gtk_tree_model_iter_nth_child(model, &iter, NULL, idx))
      return;
    
    for(i = 0; i < num_delete; i++)
      {
      if(!gtk_list_store_remove(GTK_LIST_STORE(model), &iter))
        break;
      }
    }
  
  num -= num_delete;

  //  fprintf(stderr, "splice 2\n");
    
  /* Add */
  for(i = 0; i < num_add; i++)
    {
    GtkTreeIter new_iter;
    memset(&new_iter, 0, sizeof(new_iter));
    if(!i)
      {
      if(idx == num)
        gtk_list_store_insert_before(GTK_LIST_STORE(model), &new_iter, NULL);
      else
        {
        if(!gtk_tree_model_iter_nth_child(model, &iter, NULL, idx))
          {
          //          fprintf(stderr, "splice error 2\n");
          return;
          }
        gtk_list_store_insert_before(GTK_LIST_STORE(model), &new_iter, &iter);
        }
      }
    else
      {
      memcpy(&iter, &new_iter, sizeof(iter));
      gtk_list_store_insert_after(GTK_LIST_STORE(model), &new_iter, &iter);
      }
    gtk_list_store_set(GTK_LIST_STORE(model), &new_iter, COLUMN_LABEL,
                       gavl_string_array_get(&priv->arr, idx + i), -1);
    }

  //  fprintf(stderr, "splice 3\n");

  }

static void get_value(bg_gtk_widget_t * w)
  {
  const gavl_array_t * arr;
  list_priv_t * priv = w->priv;

  
  if(!(arr = gavl_value_get_array(&w->value)))
    arr = gavl_value_set_array(&w->value);
  
  gavl_array_copy(&priv->arr, arr);

  //  fprintf(stderr, "dirlist: get_value\n");
  //  gavl_value_dump(&w->value, 2);
  
  splice(w, 0, -1, priv->arr.num_entries);
  }

static void
filesel_callback(GtkWidget *chooser,
                 gint       response_id,
                 gpointer data)
  {
  char * tmp_string;
  list_priv_t * list;
  bg_gtk_widget_t * w;
  int old_num;

  w = data;
  list = w->priv;

  old_num = list->arr.num_entries;
  
  if(response_id == GTK_RESPONSE_OK)
    {
    if(list->is_dir)
      tmp_string = gtk_file_chooser_get_current_folder(GTK_FILE_CHOOSER(chooser));
    else
      tmp_string = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
    
    if((gavl_string_array_indexof(&list->arr, tmp_string) < 0))
      {
      gavl_string_array_add(&list->arr, tmp_string);
      splice(w, old_num, 0, 1);
      
      if(w->info->flags & BG_PARAMETER_SYNC)
        bg_gtk_change_callback(NULL, w);
      }
    

    
    
    g_free(tmp_string);
    }
  gtk_widget_hide(chooser);
  
  gtk_main_quit();
  }
                             
static gboolean delete_callback(GtkWidget * w, GdkEventAny * event,
                                gpointer data)
  {
  filesel_callback(w, GTK_RESPONSE_CANCEL, data);
  return TRUE;
  }

static void attach(void * p, GtkWidget * table,
                   int * row)
  {
  list_priv_t * e = p;
  GtkWidget * vbox;
  GtkWidget * hbox;
  
  vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  
  bg_gtk_box_pack_start(hbox, e->scrolled, 1);
  
  if(e->add_button)
    bg_gtk_box_pack_start(vbox, e->add_button, 0);

  if(e->remove_button)
    bg_gtk_box_pack_start(vbox, e->remove_button, 0);

#if 0  
  if(e->top_button)
    bg_gtk_box_pack_start(vbox, e->top_button, 0);

  if(e->up_button)
    bg_gtk_box_pack_start(vbox, e->up_button, 0);

  if(e->down_button)
    bg_gtk_box_pack_start(vbox, e->down_button, 0);

  if(e->bottom_button)
    bg_gtk_box_pack_start(vbox, e->bottom_button, 0);
#endif
  
  gtk_widget_show(vbox);
  bg_gtk_box_pack_start(hbox, vbox, 0);
  gtk_widget_show(hbox);

  bg_gtk_table_attach(table, e->label, 0, 3, *row, *row+1, 1, 0);
  
  bg_gtk_table_attach(table, hbox, 0, 3, *row+1, *row+2, 1, 1);
  (*row)+=2;
  }

static void destroy(bg_gtk_widget_t * w)
  {
  list_priv_t * priv = w->priv;
  gavl_array_free(&priv->arr);
  free(priv);
  }

static const gtk_widget_funcs_t funcs =
  {
    .get_value = get_value,
    .set_value = set_value,
    .destroy =   destroy,
    .attach =    attach,
  };

static void select_row_callback(GtkTreeSelection * s, gpointer data)
  {
  bg_gtk_widget_t * w;
  list_priv_t * priv;
  GtkTreeIter iter;
  GtkTreeModel * model;

  w = data;
  priv = w->priv;

  if(!gtk_tree_selection_get_selected(s, &model, &iter))
    priv->selected = -1;
  else
    {
    priv->selected = 0;
    gtk_tree_model_get_iter_first(model, &iter);
    while(1)
      {
      if(gtk_tree_selection_iter_is_selected(s, &iter))
        break;
      priv->selected++;
      gtk_tree_model_iter_next(model, &iter);
      }
    }

  // fprintf(stderr, "select_row_callback: %d\n", priv->selected);
  
  if(priv->selected < 0)
    {
#if 0
    if(priv->top_button)
      gtk_widget_set_sensitive(priv->top_button, 0);
    if(priv->bottom_button)
      gtk_widget_set_sensitive(priv->bottom_button, 0);
    if(priv->up_button)
      gtk_widget_set_sensitive(priv->up_button, 0);
    if(priv->down_button)
      gtk_widget_set_sensitive(priv->down_button, 0);
#endif
    if(priv->remove_button)
      gtk_widget_set_sensitive(priv->remove_button, 0);
    }
  else
    {
#if 0
    if(priv->selected > 0)
      {
      if(priv->top_button)
        gtk_widget_set_sensitive(priv->top_button, 1);
      if(priv->up_button)
        gtk_widget_set_sensitive(priv->up_button, 1);
      }
    else
      {
      if(priv->top_button)
        gtk_widget_set_sensitive(priv->top_button, 0);
      if(priv->up_button)
        gtk_widget_set_sensitive(priv->up_button, 0);
      }

    if(priv->selected < priv->arr.num_entries - 1)
      {
      if(priv->bottom_button)
        gtk_widget_set_sensitive(priv->bottom_button, 1);
      if(priv->down_button)
        gtk_widget_set_sensitive(priv->down_button, 1);
      }
    else
      {
      if(priv->bottom_button)
        gtk_widget_set_sensitive(priv->bottom_button, 0);
      if(priv->down_button)
        gtk_widget_set_sensitive(priv->down_button, 0);
      }
#endif
    if(priv->remove_button)
      gtk_widget_set_sensitive(priv->remove_button, 1);
    }
  }

#if 0
static void move_selected(bg_gtk_widget_t * w, int new_pos)
  {
  int i;
  list_priv_t * priv;


  GtkTreeSelection * selection;
  GtkTreeModel * model;
  GtkTreePath      * path;

  GtkTreeIter iter;
  GtkTreeIter iter_before;

  priv = (list_priv_t *)(w->priv);
  selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(priv->treeview));
  gtk_tree_selection_get_selected(selection, &model, &iter);

  if(!new_pos)
    {
    gtk_list_store_move_after(GTK_LIST_STORE(model),
                              &iter,
                              NULL);

    }
  else
    {
    gtk_tree_model_get_iter_first(model, &iter_before);

    for(i = 0; i < new_pos-1; i++)
      gtk_tree_model_iter_next(model, &iter_before);

    if(new_pos > priv->selected)
      gtk_tree_model_iter_next(model, &iter_before);
        
    gtk_list_store_move_after(GTK_LIST_STORE(model),
                              &iter,
                              &iter_before);
    }
  //  gtk_tree_selection_select_iter(selection, &iter);
  
  path = gtk_tree_model_get_path(model, &iter);
  gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(priv->treeview),
                               path,
                               NULL,
                               0, 0.0, 0.0);
  gtk_tree_path_free(path);

  /* Move array element */
  
  if(priv->is_chain)
    gavl_array_move_entry(&priv->arr,
                          priv->selected, new_pos);
  
  /* Apply parameter and subsections. It's easier to do it here. */
  
  if(w->info->flags & BG_PARAMETER_SYNC)
    bg_gtk_change_callback(NULL, w);
  
  priv->selected = new_pos;
  
  if(!priv->selected)
    {
    if(priv->top_button)
      gtk_widget_set_sensitive(priv->top_button, 0);
    if(priv->up_button)
      gtk_widget_set_sensitive(priv->up_button, 0);
    }
  else
    {
    if(priv->top_button)
      gtk_widget_set_sensitive(priv->top_button, 1);
    if(priv->up_button)
      gtk_widget_set_sensitive(priv->up_button, 1);
    }

  if(priv->selected >= priv->arr.num_entries - 1)
    {
    if(priv->down_button)
      gtk_widget_set_sensitive(priv->down_button, 0);
    if(priv->bottom_button)
      gtk_widget_set_sensitive(priv->bottom_button, 0);
    }
  else
    {
    if(priv->down_button)
      gtk_widget_set_sensitive(priv->down_button, 1);
    if(priv->bottom_button)
      gtk_widget_set_sensitive(priv->bottom_button, 1);
    }
  }
#endif

static void button_callback(GtkWidget * wid, gpointer data)
  {
  bg_gtk_widget_t * w;
  list_priv_t * priv;
  
  //  bg_cfg_section_t * subsubsection;

  w = data;
  priv = w->priv;
  
  if(wid == priv->add_button)
    {
    GtkWidget * toplevel;
    GtkWidget * chooser;
    
    toplevel = bg_gtk_get_toplevel(wid);
    
    if(priv->is_dir)
      {
      chooser = 
        gtk_file_chooser_dialog_new(TR("Select a directory"),
                                    GTK_WINDOW(toplevel),
                                    GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
                                    TR("_Cancel"),
                                    GTK_RESPONSE_CANCEL,
                                    TR("_OK"),
                                    GTK_RESPONSE_OK,
                                    NULL);
      }
    else
      {
      chooser = 
        gtk_file_chooser_dialog_new(TR("Select a file"),
                                    GTK_WINDOW(toplevel),
                                    GTK_FILE_CHOOSER_ACTION_SAVE,
                                    TR("_Cancel"),
                                    GTK_RESPONSE_CANCEL,
                                    TR("_OK"),
                                    GTK_RESPONSE_OK,
                                    NULL);
      }
    
    gtk_window_set_modal(GTK_WINDOW(chooser), TRUE);

    g_signal_connect(chooser, "response",
                     G_CALLBACK(filesel_callback),
                     data);
    
    g_signal_connect(chooser,
                     "delete_event", G_CALLBACK(delete_callback),
                     data);

    gtk_widget_show(chooser);
    gtk_main();
    
    
    }
  else if(wid == priv->remove_button)
    {
    gavl_array_splice_val(&priv->arr, priv->selected, 1, NULL);
    splice(w, priv->selected, 1, 0);
    
    if(w->info->flags & BG_PARAMETER_SYNC)
      bg_gtk_change_callback(NULL, w);
    }
#if 0

  if(wid == priv->top_button)
    {
    if(priv->selected == 0)
      return;
    move_selected(w, 0);
    }
  else if(wid == priv->up_button)
    {
    if(priv->selected == 0)
      return;
    move_selected(w, priv->selected - 1);
    }
  else if(wid == priv->down_button)
    {
    if(priv->selected >= priv->arr.num_entries - 1)
      return;
    move_selected(w, priv->selected+1);
    }
  
  else if(wid == priv->bottom_button)
    {
    if(priv->selected >= priv->arr.num_entries-1)
      return;

    move_selected(w, priv->arr.num_entries-1);
    }
 
#endif

  
  }


static void create_list_common(bg_gtk_widget_t * w, const bg_parameter_info_t * info,
                               const char * translation_domain, int is_dir)
  {
  GtkListStore *store;
  GtkTreeViewColumn *column;
  GtkCellRenderer *renderer;
  GtkTreeSelection * selection;

  list_priv_t * priv = calloc(1, sizeof(*priv));

  priv->is_dir = is_dir;
  priv->translation_domain = translation_domain;

  
  w->funcs = &funcs;
  w->priv = priv;
  
  /* Create objects */

  priv->add_button = bg_gtk_create_icon_button(BG_ICON_ADD);
  priv->remove_button = bg_gtk_create_icon_button(BG_ICON_TRASH);

  g_signal_connect(G_OBJECT(priv->add_button),
                   "clicked", G_CALLBACK(button_callback),
                   (gpointer)w);
  g_signal_connect(G_OBJECT(priv->remove_button),
                   "clicked", G_CALLBACK(button_callback),
                   (gpointer)w);
  gtk_widget_show(priv->add_button);
  gtk_widget_show(priv->remove_button);
  gtk_widget_set_sensitive(priv->remove_button, 0);
  
  store = gtk_list_store_new(NUM_COLUMNS, G_TYPE_STRING);

  priv->treeview = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));

  priv->label = gtk_label_new(TR(info->long_name));
  
  if(info->help_string)
    {
    bg_gtk_tooltips_set_tip(priv->treeview,
                            info->help_string, translation_domain);
    }

  
  gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(priv->treeview), FALSE);
  selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(priv->treeview));
  
  g_signal_connect(G_OBJECT(selection),
                   "changed", G_CALLBACK(select_row_callback),
                   (gpointer)w);

  renderer = gtk_cell_renderer_text_new ();
  column =
    gtk_tree_view_column_new_with_attributes("",
                                             renderer,
                                             "text",
                                             COLUMN_LABEL,
                                             NULL);
  
  gtk_tree_view_append_column (GTK_TREE_VIEW(priv->treeview), column);
  
  gtk_widget_show(priv->treeview);

  priv->scrolled =
    gtk_scrolled_window_new(gtk_scrollable_get_hadjustment(GTK_SCROLLABLE(priv->treeview)),
                            gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(priv->treeview)));
  
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(priv->scrolled),
                                 GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
  gtk_container_add(GTK_CONTAINER(priv->scrolled), priv->treeview);
  gtk_widget_show(priv->scrolled);
  gtk_widget_show(priv->label);
  }

void
bg_gtk_create_dirlist(bg_gtk_widget_t * w,
                      const char * translation_domain)
  {
  create_list_common(w, w->info, translation_domain, 1);
  }

void
bg_gtk_create_filelist(bg_gtk_widget_t * w,
                       const char * translation_domain)
  {
  create_list_common(w, w->info, translation_domain, 0);
  }
