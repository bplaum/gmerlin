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
    COLUMN_CFG,   // gavl_dictionary_t
    NUM_COLUMNS
  };

typedef struct list_priv_s list_priv_t;


struct list_priv_s
  {
  GtkWidget * treeview;
  GtkWidget * config_button;
  GtkWidget * info_button;

  GtkWidget * top_button;
  GtkWidget * bottom_button;
  GtkWidget * up_button;
  GtkWidget * down_button;
  
  GtkWidget * add_button;
  GtkWidget * remove_button;

  GtkWidget * scrolled;

  const char * translation_domain;
  bg_set_parameter_func_t  set_param;
  void * data;
  int selected;
  int param_selected;
  int is_chain;

  char ** multi_labels;

  gavl_array_t arr;
  //  int array_changed;
  };

static void update_cfg(bg_gtk_widget_t * w)
  {
  int i;
  list_priv_t * priv;
  GtkTreeModel * model;
  GtkTreeIter iter;

  priv = w->priv;

  model = gtk_tree_view_get_model(GTK_TREE_VIEW(priv->treeview));
  if(!gtk_tree_model_get_iter_first(model, &iter))
    return;
  
  for(i = 0; i < priv->arr.num_entries; i++)
    {
    //  fprintf(stderr, "gtk_list_store_set: %p\n", priv->arr.entries[i].v.dictionary);
    gtk_list_store_set(GTK_LIST_STORE(model), &iter, COLUMN_CFG,
                       priv->arr.entries[i].v.dictionary, -1);
    if(!gtk_tree_model_iter_next(model, &iter))
      break;
    }
  }

static void translate_labels(bg_gtk_widget_t * w)
  {
  list_priv_t * priv;
  int i;
  priv = w->priv;
  i = 0;
  while(w->info->multi_labels[i])
    i++;
  priv->multi_labels = calloc(i+1, sizeof(*priv->multi_labels));
  i = 0;
  while(w->info->multi_labels[i])
    {
    priv->multi_labels[i] =
      gavl_strdup(TRD(w->info->multi_labels[i], priv->translation_domain));
    i++;
    }
  
  }

static void set_sub_param(void * priv, const char * name,
                          const gavl_value_t * val)
  {
  bg_msg_sink_t * sink;
  char * tmp_string;
  list_priv_t * list;
  bg_gtk_widget_t * w;
  w = priv;
  list = w->priv;

  fprintf(stderr, "set_sub_param %s\n", name);

  if(!list->set_param)
    return;
  
  if(!name)
    tmp_string = NULL;
  else if(list->is_chain)
    tmp_string = gavl_sprintf("%s.%d.%s", w->info->name, list->selected,
                            name);
  else if(list->param_selected < 0)
    return;
  else
    {
    tmp_string = gavl_sprintf("%s.%s.%s", w->info->name,
                            w->info->multi_names[list->param_selected],
                            name);
    }
  list->set_param(list->data, tmp_string, val);
  if(tmp_string)
    free(tmp_string);

  if(name && (sink = bg_dialog_get_sink(w->dialog)) && w->section->ctx)
    {
    gavl_msg_t * msg = bg_msg_sink_get(sink);
    if(list->is_chain) 
      bg_msg_set_chain_parameter_ctx(msg, w->section->ctx, w->info->name, list->selected, name, val);
    else
      bg_msg_set_multi_parameter_ctx(msg, w->section->ctx, w->info->name,
                                     w->info->multi_names[list->param_selected], name, val);
    bg_msg_sink_put(sink);
    }
  }

/* Get the index in the multi_* arrays of the selected item */
static int get_selected_index(bg_gtk_widget_t * w)
  {
  GtkTreeModel * model;
  GtkTreeIter iter;
  const char * name;
  int ret;
  gavl_dictionary_t * cfg = NULL;
  list_priv_t * priv = w->priv;
  
  model = gtk_tree_view_get_model(GTK_TREE_VIEW(priv->treeview));
  if(!gtk_tree_model_iter_nth_child(model, &iter,
                                    NULL,
                                    priv->selected))
    {
    return 0;
    }

  gtk_tree_model_get(model, &iter, COLUMN_CFG, &cfg, -1);
  
  if(!cfg)
    return 0;

  if(!(name = gavl_dictionary_get_string(cfg, BG_CFG_TAG_NAME)))
    return 0;
  
  ret = 0;
  while(strcmp(w->info->multi_names[ret], name))
    ret++;
  
  return ret;
  }

static void set_value(bg_gtk_widget_t * w)
  {
  gavl_array_t * arr;
  list_priv_t * priv = w->priv;
  arr = gavl_value_set_array(&w->value);
  gavl_array_copy(arr, &priv->arr);
  }

static const char * get_label(bg_gtk_widget_t * w, const gavl_dictionary_t * cfg)
  {
  int i;
  int idx;
  const char * var;
  list_priv_t * priv = w->priv;
  
  if(!(var = gavl_dictionary_get_string(cfg, BG_CFG_TAG_NAME)))
    return NULL;
  
  idx = -1;
  i = 0;
  while(w->info->multi_names[i])
    {
    if(!strcmp(w->info->multi_names[i], var))
      {
      idx = i;
      break;
      }
    i++;
    }
  
  if(priv->multi_labels)
    return priv->multi_labels[idx];
  else
    return w->info->multi_names[idx];
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
  
  /* create translated labels */
  if(!priv->multi_labels && w->info->multi_labels)
    translate_labels(w);

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
        return;
      }
    }
  
  num -= num_delete;
  
  /* Add */
  for(i = 0; i < num_add; i++)
    {
    GtkTreeIter new_iter;
    const gavl_dictionary_t * dict;
    dict = gavl_value_get_dictionary(&priv->arr.entries[idx + i]);

    if(!i)
      {
      if(idx == num)
        gtk_list_store_insert_before(GTK_LIST_STORE(model), &new_iter, NULL);
      else
        {
        if(!gtk_tree_model_iter_nth_child(model, &iter, NULL, idx))
          return;
        gtk_list_store_insert_before(GTK_LIST_STORE(model), &new_iter, &iter);
        }
      }
    else
      {
      memcpy(&iter, &new_iter, sizeof(iter));
      gtk_list_store_insert_after(GTK_LIST_STORE(model), &new_iter, &iter);
      }
    gtk_list_store_set(GTK_LIST_STORE(model), &new_iter, COLUMN_LABEL, get_label(w, dict), -1);
    }
  update_cfg(w);
  }

static void get_value(bg_gtk_widget_t * w)
  {
  const gavl_array_t * arr;
  list_priv_t * priv = w->priv;
  
  if(!(arr = gavl_value_get_array(&w->value)))
    arr = gavl_value_set_array(&w->value);
  
  gavl_array_copy(&priv->arr, arr);
  
  splice(w, 0, -1, arr->num_entries);
  }

static void add_func(void * priv, const char * name,
                     const gavl_value_t * val)
  {
  list_priv_t * list;
  bg_gtk_widget_t * w;
  int old_num;
  gavl_value_t v;
  w = priv;
  list = w->priv;
  
  if(!name)
    return;
  if(strcmp(name, w->info->name))
    return;
  
  /* */
  old_num = list->arr.num_entries;

  gavl_value_init(&v);
  gavl_dictionary_copy(gavl_value_set_dictionary(&v), bg_multi_menu_get_selected(val));
  
  gavl_array_splice_val_nocopy(&list->arr, list->arr.num_entries, 0, &v);
  splice(w, old_num, 0, 1);
  
  if(w->info->flags & BG_PARAMETER_SYNC)
    bg_gtk_change_callback(NULL, w);
  
  }

static void attach(void * p, GtkWidget * table,
                   int * row)
  {
  list_priv_t * e = (list_priv_t*)(p);
  GtkWidget * vbox;
  GtkWidget * hbox;
  
  int num_rows = 3; // Add an extra row

  vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  
  if(e->is_chain)
    num_rows += 2;
  if(e->top_button)
    num_rows+=4;

  bg_gtk_box_pack_start(hbox, e->scrolled, 1);
  
  if(e->add_button)
    bg_gtk_box_pack_start(vbox, e->add_button, 0);

  if(e->remove_button)
    bg_gtk_box_pack_start(vbox, e->remove_button, 0);

  if(e->config_button)
    bg_gtk_box_pack_start(vbox, e->config_button, 0);

  if(e->info_button)
    bg_gtk_box_pack_start(vbox, e->info_button, 0);

  if(e->top_button)
    bg_gtk_box_pack_start(vbox, e->top_button, 0);

  if(e->up_button)
    bg_gtk_box_pack_start(vbox, e->up_button, 0);

  if(e->down_button)
    bg_gtk_box_pack_start(vbox, e->down_button, 0);

  if(e->bottom_button)
    bg_gtk_box_pack_start(vbox, e->bottom_button, 0);

  gtk_widget_show(vbox);
  bg_gtk_box_pack_start(hbox, vbox, 0);
  gtk_widget_show(hbox);
  
  bg_gtk_table_attach(table, hbox, 0, 3, *row, *row+1, 1, 1);
  (*row)++;
  
  }

static void destroy(bg_gtk_widget_t * w)
  {
  list_priv_t * priv = (list_priv_t*)(w->priv);
  if(priv->multi_labels)
    {
    int i = 0;
    while(priv->multi_labels[i])
      free(priv->multi_labels[i++]);
    free(priv->multi_labels);
    }
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

  fprintf(stderr, "select_row_callback: %d\n", priv->selected);
  
  if(priv->selected < 0)
    {
    gtk_widget_set_sensitive(priv->info_button, 0);
    gtk_widget_set_sensitive(priv->config_button, 0);

    if(priv->top_button)
      gtk_widget_set_sensitive(priv->top_button, 0);
    if(priv->bottom_button)
      gtk_widget_set_sensitive(priv->bottom_button, 0);
    if(priv->up_button)
      gtk_widget_set_sensitive(priv->up_button, 0);
    if(priv->down_button)
      gtk_widget_set_sensitive(priv->down_button, 0);
    if(priv->remove_button)
      gtk_widget_set_sensitive(priv->remove_button, 0);
    priv->param_selected = priv->selected;
    }
  else
    {
    const char * name;
    gavl_dictionary_t * cfg = NULL;
    gtk_tree_model_get(model, &iter, COLUMN_CFG, &cfg, -1);
    fprintf(stderr, "get_cfg %p\n", cfg);
    gavl_dictionary_dump(cfg, 2);
    
    if(!cfg || !(name = gavl_dictionary_get_string(cfg, BG_CFG_TAG_NAME)))
      return;
    
    priv->param_selected = 0;
    while(strcmp(w->info->multi_names[priv->param_selected], name))
      priv->param_selected++;
    
    if(w->info->multi_descriptions &&
       w->info->multi_descriptions[priv->param_selected])
      gtk_widget_set_sensitive(priv->info_button, 1);
    else
      gtk_widget_set_sensitive(priv->info_button, 0);
    
    if(w->info->multi_parameters &&
       w->info->multi_parameters[priv->param_selected])
      gtk_widget_set_sensitive(priv->config_button, 1);
    else
      gtk_widget_set_sensitive(priv->config_button, 0);

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
    
    if(priv->remove_button)
      gtk_widget_set_sensitive(priv->remove_button, 1);
    }
  }

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

static void button_callback(GtkWidget * wid, gpointer data)
  {
  bg_gtk_widget_t * w;
  list_priv_t * priv;
  bg_dialog_t * dialog;

  const char * label;
  bg_cfg_section_t * subsection;
  //  bg_cfg_section_t * subsubsection;

  w = data;
  priv = w->priv;
  
  if(wid == priv->config_button)
    {
    subsection = priv->arr.entries[priv->selected].v.dictionary;
    
    if(w->info->multi_labels && w->info->multi_labels[priv->param_selected])
      label = TRD(w->info->multi_labels[priv->param_selected],
                  priv->translation_domain);
    else
      label = w->info->multi_names[priv->param_selected];
    
    dialog = bg_dialog_create(subsection, set_sub_param, w,
                              w->info->multi_parameters[priv->param_selected],
                              label);
    fprintf(stderr, "Config_button: %p\n", bg_dialog_get_sink(w->dialog));
    bg_dialog_set_sink(dialog, bg_dialog_get_sink(w->dialog));
    bg_dialog_show(dialog, priv->treeview);
    }
  else if(wid == priv->info_button)
    {
    bg_gtk_multi_info_show(w->info, get_selected_index(w),
                           priv->translation_domain, priv->info_button);
    }
  else if(wid == priv->top_button)
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
  else if(wid == priv->add_button)
    {
    bg_parameter_info_t params[2];
    char * tmp_string;
    memset(params, 0, sizeof(params));
    params[0].name               = w->info->name;
    params[0].long_name          = w->info->long_name;
    params[0].type               = BG_PARAMETER_MULTI_MENU;
    params[0].gettext_domain     = gavl_strrep(params[0].gettext_domain,
                                             priv->translation_domain);
    params[0].multi_names        = w->info->multi_names;
    params[0].multi_labels       = w->info->multi_labels;
    params[0].multi_descriptions = w->info->multi_descriptions;
    params[0].help_string        = w->info->help_string;
    params[0].multi_parameters   = w->info->multi_parameters;
    
    tmp_string = gavl_sprintf(TR("Add %s"),
                            TRD(w->info->long_name, priv->translation_domain));
    
    dialog = bg_dialog_create(NULL, add_func, w, params, tmp_string);
    
    bg_dialog_set_sink(dialog, bg_dialog_get_sink(w->dialog));
    
    free(tmp_string);
    bg_dialog_show(dialog, priv->treeview);
    free(params[0].gettext_domain);
    }
  else if(wid == priv->remove_button)
    {
    bg_msg_sink_t * sink;
    gavl_array_splice_val(&priv->arr, priv->selected, 1, NULL);
    splice(w, priv->selected, 1, 0);
    
    if(w->info->flags & BG_PARAMETER_SYNC)
      bg_gtk_change_callback(NULL, w);
    
    if((sink = bg_dialog_get_sink(w->dialog)))
      bg_msg_set_parameter_ctx_term(sink);
    }
  
  }

#if 0
static GtkWidget * create_pixmap_button(const char * filename)
  {
  GtkWidget * button;
  GtkWidget * image;
  char * path;
  path = bg_search_file_read("icons", filename);
  if(path)
    {
    image = gtk_image_new_from_file(path);
    free(path);
    }
  else
    image = gtk_image_new();

  gtk_widget_show(image);
  button = gtk_button_new();
  gtk_container_add(GTK_CONTAINER(button), image);
  return button;
  }
#endif

static void create_list_common(bg_gtk_widget_t * w, const bg_parameter_info_t * info,
                               bg_set_parameter_func_t set_param,
                               void * data, const char * translation_domain,
                               int is_chain)
  {
  GtkListStore *store;
  GtkTreeViewColumn *column;
  GtkCellRenderer *renderer;
  GtkTreeSelection * selection;

  list_priv_t * priv = calloc(1, sizeof(*priv));

  priv->set_param   = set_param;
  priv->data        = data;
  priv->translation_domain = translation_domain;

  priv->is_chain = is_chain;

  w->funcs = &funcs;
  w->priv = priv;
  
  /* Create objects */

  priv->info_button = bg_gtk_create_icon_button(BG_ICON_INFO);
  priv->config_button = bg_gtk_create_icon_button(BG_ICON_CONFIG);
  
  g_signal_connect(G_OBJECT(priv->info_button),
                   "clicked", G_CALLBACK(button_callback),
                   (gpointer)w);
  g_signal_connect(G_OBJECT(priv->config_button),
                   "clicked", G_CALLBACK(button_callback),
                   (gpointer)w);
  gtk_widget_show(priv->info_button);
  gtk_widget_show(priv->config_button);

  gtk_widget_set_sensitive(priv->info_button, 0);
  gtk_widget_set_sensitive(priv->config_button, 0);
  
  if(priv->is_chain)
    {
    priv->top_button = bg_gtk_create_icon_button(BG_ICON_CHEVRON2_UP);
    priv->bottom_button = bg_gtk_create_icon_button(BG_ICON_CHEVRON2_DOWN);
    priv->up_button = bg_gtk_create_icon_button(BG_ICON_CHEVRON_UP);
    priv->down_button = bg_gtk_create_icon_button(BG_ICON_CHEVRON_DOWN);
    
    g_signal_connect(G_OBJECT(priv->top_button),
                     "clicked", G_CALLBACK(button_callback),
                     (gpointer)w);
    g_signal_connect(G_OBJECT(priv->bottom_button),
                     "clicked", G_CALLBACK(button_callback),
                     (gpointer)w);
    g_signal_connect(G_OBJECT(priv->up_button),
                     "clicked", G_CALLBACK(button_callback),
                     (gpointer)w);
    g_signal_connect(G_OBJECT(priv->down_button),
                     "clicked", G_CALLBACK(button_callback),
                     (gpointer)w);
    gtk_widget_show(priv->top_button);
    gtk_widget_show(priv->bottom_button);
    gtk_widget_show(priv->up_button);
    gtk_widget_show(priv->down_button);

    gtk_widget_set_sensitive(priv->top_button, 0);
    gtk_widget_set_sensitive(priv->bottom_button, 0);
    gtk_widget_set_sensitive(priv->up_button, 0);
    gtk_widget_set_sensitive(priv->down_button, 0);


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
    }
  
  store = gtk_list_store_new(NUM_COLUMNS, G_TYPE_STRING, G_TYPE_POINTER);

  priv->treeview = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));

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
  }

void
bg_gtk_create_multi_list(bg_gtk_widget_t * w,
                         bg_set_parameter_func_t set_param,
                         void * data, const char * translation_domain)
  {
  create_list_common(w, w->info, set_param, data, translation_domain, 0);

  /* Create default value */

  }

void
bg_gtk_create_multi_chain(bg_gtk_widget_t * w,
                          bg_set_parameter_func_t set_param,
                          void * data, const char * translation_domain)
  {
  create_list_common(w, w->info, set_param, data, translation_domain, 1);
  }
