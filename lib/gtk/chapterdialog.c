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


#include <gmerlin/cfg_dialog.h>
#include <gmerlin/streaminfo.h>
#include <gui_gtk/chapterdialog.h>
#include <gui_gtk/gtkutils.h>
#include <gmerlin/utils.h>
#include <gmerlin/iconfont.h>


enum
{
  COLUMN_NAME,
  COLUMN_TIME,
  NUM_COLUMNS
};

typedef struct
  {
  GtkWidget * window;
  GtkWidget * add_button;
  GtkWidget * delete_button;
  GtkWidget * edit_button;
  GtkWidget * list;

  GtkWidget * ok_button;
  GtkWidget * cancel_button;
    
  gavl_chapter_list_t * cl;

  int selected;
  int edited;
  
  int is_ok;

  guint select_id;
  gavl_time_t duration;
  } bg_gtk_chapter_dialog_t;

static void select_row_callback(GtkTreeSelection * sel,
                                gpointer data)
  {
  int i;
  bg_gtk_chapter_dialog_t * win;
  GtkTreeModel * model;
  GtkTreeSelection * selection;
  GtkTreeIter iter;
  int num;
  
  win = data;
  model = gtk_tree_view_get_model(GTK_TREE_VIEW(win->list));
  selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(win->list));

  num = gavl_chapter_list_get_num(win->cl);
  
  win->selected = -1;

  if(!gtk_tree_model_get_iter_first(model, &iter))
    return;
  
  for(i = 0; i < num; i++)
    {
    if(gtk_tree_selection_iter_is_selected(selection, &iter))
      {
      win->selected = i;
      break;
      }
    if(!gtk_tree_model_iter_next(model, &iter))
      break;
    }

  if(win->selected < 0)
    {
    gtk_widget_set_sensitive(win->edit_button, 0);
    gtk_widget_set_sensitive(win->delete_button, 0);
    }
  else
    {
    gtk_widget_set_sensitive(win->edit_button, 1);
    gtk_widget_set_sensitive(win->delete_button, 1);
    }
  }


static void update_list(bg_gtk_chapter_dialog_t * win)
  {
  int i;
  char time_string[GAVL_TIME_STRING_LEN];
  GtkTreeModel * model;
  GtkTreeSelection * selection;
  GtkTreeIter iter;
  int num;
  int timescale;
  gavl_time_t t;
  const char * label;
  
  selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(win->list));

  num = gavl_chapter_list_get_num(win->cl);

  g_signal_handler_block(G_OBJECT(selection), win->select_id);
    
  model = gtk_tree_view_get_model(GTK_TREE_VIEW(win->list));
  gtk_list_store_clear(GTK_LIST_STORE(model));
  
  if(win->cl && gavl_chapter_list_is_valid(win->cl))
    {
    num = gavl_chapter_list_get_num(win->cl);
    timescale = gavl_chapter_list_get_timescale(win->cl);
    
    for(i = 0; i < num; i++)
      {
      gtk_list_store_append(GTK_LIST_STORE(model), &iter);

      t = gavl_time_unscale(timescale, gavl_chapter_list_get_time(win->cl, i));
      
      gavl_time_prettyprint(t, time_string);
      gtk_list_store_set(GTK_LIST_STORE(model), &iter,
                         COLUMN_TIME,
                         time_string,
                         -1);

      if((label = gavl_chapter_list_get_label(win->cl, i)))
        gtk_list_store_set(GTK_LIST_STORE(model), &iter,
                           COLUMN_NAME,
                           label,
                           -1);
      
      if(win->selected == i)
        gtk_tree_selection_select_iter(selection, &iter);
      }
    }
  if((win->selected < 0) || (!win->cl))
    {
    gtk_widget_set_sensitive(win->edit_button, 0);
    gtk_widget_set_sensitive(win->delete_button, 0);
    }
  else
    {
    gtk_widget_set_sensitive(win->edit_button, 1);
    gtk_widget_set_sensitive(win->delete_button, 1);
    }

  g_signal_handler_unblock(G_OBJECT(selection), win->select_id);
  }

static void set_parameter(void * data, const char * name,
                          const gavl_value_t * val)
  {
  bg_gtk_chapter_dialog_t * win;
  gavl_dictionary_t * chapter;
  win = data;
  
  if(!name)
    {
    win->is_ok = 1;
    return;
    }

  chapter = gavl_chapter_list_get_nc(win->cl, win->edited);

  
  if(!strcmp(name, GAVL_CHAPTERLIST_TIME))
    {
    int timescale = gavl_chapter_list_get_timescale(win->cl);
    gavl_dictionary_set_long(chapter, GAVL_CHAPTERLIST_TIME, gavl_time_scale(timescale, val->v.l));
    }
  else
    gavl_dictionary_set(chapter, name, val);
  }


static int edit_chapter(bg_gtk_chapter_dialog_t * win)
  {
  int64_t time;
  int timescale;
  int num;
  
  bg_dialog_t * dialog;
  
  bg_parameter_info_t chapter_parameters[3];

  timescale = gavl_chapter_list_get_timescale(win->cl);
  num = gavl_chapter_list_get_num(win->cl);
  
  /* Set up parameters */
  memset(chapter_parameters, 0, sizeof(chapter_parameters));
  
  chapter_parameters[0].name      = GAVL_META_LABEL;
  chapter_parameters[0].long_name = TRS("Name");
  chapter_parameters[0].type = BG_PARAMETER_STRING;
  gavl_value_set_string(&chapter_parameters[0].val_default,
                        gavl_chapter_list_get_label(win->cl, win->edited));
  
  /* Time can only be changed if this isn't the first chapter */
  if(win->edited)
    {
    chapter_parameters[1].name      = GAVL_CHAPTERLIST_TIME;
    chapter_parameters[1].long_name = TRS("Time");
    chapter_parameters[1].type = BG_PARAMETER_TIME;

    time = gavl_chapter_list_get_time(win->cl, win->edited);
    
    gavl_value_set_long(&chapter_parameters[1].val_default,
                        gavl_time_unscale(timescale, time));
    
    /* We set the min-max values of the time such that the
       resulting list will always be ordered */
    time = gavl_chapter_list_get_time(win->cl, win->edited-1);
    
    gavl_value_set_long(&chapter_parameters[1].val_min,
                        gavl_time_unscale(timescale, time) + GAVL_TIME_SCALE / 1000);
    
    if(win->edited == num - 1)
      gavl_value_set_long(&chapter_parameters[1].val_max, win->duration -
                          GAVL_TIME_SCALE / 1000);
    else
      {
      time = gavl_chapter_list_get_time(win->cl, win->edited+1);
      
      gavl_value_set_long(&chapter_parameters[1].val_max, 
                          gavl_time_unscale(timescale, time) - GAVL_TIME_SCALE / 1000);
      }
    
    if(chapter_parameters[1].val_default.v.l < chapter_parameters[1].val_min.v.l)
      chapter_parameters[1].val_default.v.l = chapter_parameters[1].val_min.v.l;
    }

  dialog = bg_dialog_create(NULL,
                            set_parameter,
                            win,
                            chapter_parameters,
                            TR("Edit chapter"));
  
  bg_dialog_show(dialog, win->window);
  bg_dialog_destroy(dialog);
  
  gavl_value_free(&chapter_parameters[0].val_default);

  return 1;
  }

static void button_callback(GtkWidget * w, gpointer data)
  {
  bg_gtk_chapter_dialog_t * win;
  win = (bg_gtk_chapter_dialog_t*)data;

  if(w == win->ok_button)
    {
    win->is_ok = 1;
    gtk_main_quit();
    gtk_widget_hide(win->window);
    }
  else if((w == win->cancel_button) || (w == win->window))
    {
    gtk_main_quit();
    gtk_widget_hide(win->window);
    win->is_ok = 0;
    }
  else if(w == win->add_button)
    {
    if(!win->cl)
      {
      win->cl = calloc(1, sizeof(*win->cl));
      gavl_chapter_list_set_timescale(win->cl, GAVL_TIME_SCALE);
      win->selected = 0;
      win->edited = 0;
      }
    else
      win->edited = win->selected + 1;
    
    gavl_chapter_list_insert(win->cl, win->edited,
                           0,NULL);
    win->is_ok = 0;
    edit_chapter(win);
    
    if(!win->is_ok)
      gavl_chapter_list_delete(win->cl, win->edited);
    else
      {
      win->selected = win->edited;
      update_list(win);
      }
    }
  else if(w == win->delete_button)
    {
    gavl_chapter_list_delete(win->cl, win->selected);
    update_list(win);
    }
  else if(w == win->edit_button)
    {
    win->edited = win->selected;
    edit_chapter(win);
    update_list(win);
    }
  }

static gboolean delete_callback(GtkWidget * w, GdkEventAny * evt, gpointer data)
  {
  button_callback(w, data);
  return TRUE;
  }

static GtkWidget * create_window_icon_button(bg_gtk_chapter_dialog_t * win,
                                             const char * icon,
                                             const char * tooltip)
  {
  GtkWidget * button;

  button = bg_gtk_create_icon_button(icon);
  
  g_signal_connect(G_OBJECT(button), "clicked",
                   G_CALLBACK(button_callback), win);

  
  gtk_widget_show(button);

  bg_gtk_tooltips_set_tip(button, tooltip, PACKAGE);
  
  return button;
  }

static bg_gtk_chapter_dialog_t * create_dialog(gavl_chapter_list_t * list,
                                               gavl_time_t duration)
  {
  GtkListStore *store;
  GtkCellRenderer *renderer;
  GtkTreeViewColumn *column;
  GtkTreeSelection * selection;

  GtkWidget * scrolled;
  GtkWidget * table;
  GtkWidget * box;
    
  bg_gtk_chapter_dialog_t * ret;
  ret = calloc(1, sizeof(*ret));
  
  ret->cl = calloc(1, sizeof(*ret->cl));
  gavl_dictionary_copy(ret->cl, list);
  ret->duration = duration;
  
  /* Create objects */
  ret->window = bg_gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_position(GTK_WINDOW(ret->window), GTK_WIN_POS_CENTER_ON_PARENT);
  
  gtk_window_set_modal(GTK_WINDOW(ret->window), 1);
  gtk_window_set_title(GTK_WINDOW(ret->window), TR("Edit chapters"));
  g_signal_connect(G_OBJECT(ret->window), "delete_event",
                   G_CALLBACK(delete_callback),
                   ret);
  
  ret->ok_button = gtk_button_new_with_mnemonic("_OK");
  ret->cancel_button = gtk_button_new_with_mnemonic("_Cancel");

  g_signal_connect(G_OBJECT(ret->ok_button), "clicked",
                   G_CALLBACK(button_callback), ret);
  g_signal_connect(G_OBJECT(ret->cancel_button), "clicked",
                   G_CALLBACK(button_callback), ret);

  gtk_widget_show(ret->ok_button);
  gtk_widget_show(ret->cancel_button);

  ret->add_button =
    create_window_icon_button(ret, BG_ICON_ADD, TRS("Add new chapter"));
  ret->edit_button =
    create_window_icon_button(ret, BG_ICON_CONFIG, TRS("Edit chapter"));
  ret->delete_button =
    create_window_icon_button(ret, BG_ICON_TRASH, TRS("Delete chapter"));
  
  
  /* Create treeview */
  store = gtk_list_store_new(NUM_COLUMNS,
                             G_TYPE_STRING,
                             G_TYPE_STRING,
                             G_TYPE_STRING);

  ret->list = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));

  selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(ret->list));
  
  ret->select_id =
    g_signal_connect(G_OBJECT(selection), "changed",
                     G_CALLBACK(select_row_callback), (gpointer)ret);
  
  renderer = gtk_cell_renderer_text_new ();
  column = gtk_tree_view_column_new_with_attributes ("Name",
                                                     renderer,
                                                     "text",
                                                     COLUMN_NAME,
                                                     NULL);

  gtk_tree_view_append_column (GTK_TREE_VIEW(ret->list), column);

  renderer = gtk_cell_renderer_text_new ();
  column = gtk_tree_view_column_new_with_attributes ("Time",
                                                     renderer,
                                                     "text",
                                                     COLUMN_TIME,
                                                     NULL);

  gtk_tree_view_append_column (GTK_TREE_VIEW(ret->list), column);

  gtk_widget_show(ret->list);
  
  /* Pack objects */
  table = gtk_grid_new();

  gtk_grid_set_row_spacing(GTK_GRID(table), 5);
  gtk_grid_set_column_spacing(GTK_GRID(table), 5);
  gtk_container_set_border_width(GTK_CONTAINER(table), 5);
  
  scrolled =
    gtk_scrolled_window_new(gtk_scrollable_get_hadjustment(GTK_SCROLLABLE(ret->list)),
                            gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(ret->list)));
  
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                 GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
  gtk_container_add(GTK_CONTAINER(scrolled), ret->list);
  gtk_widget_show(scrolled);
  
  bg_gtk_table_attach_defaults(table, scrolled, 0, 1, 0, 3);
  bg_gtk_table_attach(table, ret->add_button, 1, 2, 0, 1, 0, 0);
  bg_gtk_table_attach(table, ret->edit_button, 1, 2, 1, 2, 0, 0);
  bg_gtk_table_attach(table, ret->delete_button, 1, 2, 2, 3, 0, 0);
  
  box = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_box_set_spacing(GTK_BOX(box), 5);
  gtk_container_add(GTK_CONTAINER(box), ret->ok_button);
  gtk_container_add(GTK_CONTAINER(box), ret->cancel_button);
  gtk_widget_show(box);

  bg_gtk_table_attach(table, box, 0, 2, 3, 4, 0, 0);
  gtk_widget_show(table);
  gtk_container_add(GTK_CONTAINER(ret->window), table);

  update_list(ret);

  return ret;
  }

static void destroy_dialog(bg_gtk_chapter_dialog_t * dlg)
  {
  gtk_widget_destroy(dlg->window);

  if(dlg->cl)
    {
    gavl_dictionary_free(dlg->cl);
    free(dlg->cl);
    }
  free(dlg);
  }

void bg_gtk_chapter_dialog_show(gavl_chapter_list_t ** list,
                                gavl_time_t duration, GtkWidget * parent)
  {
  bg_gtk_chapter_dialog_t * dlg;
  dlg = create_dialog(*list, duration);
  
  parent = bg_gtk_get_toplevel(parent);
  if(parent)
    gtk_window_set_transient_for(GTK_WINDOW(dlg->window),
                                 GTK_WINDOW(parent));
  
  gtk_widget_show(dlg->window);
  gtk_main();

  if(dlg->is_ok)
    {
    if(*list)
      {
      gavl_dictionary_free(*list);
      gavl_dictionary_init(*list);
      }
    else
      *list = calloc(1, sizeof(**list));
    
    gavl_dictionary_copy(*list, dlg->cl);
    }
  destroy_dialog(dlg);
  }

