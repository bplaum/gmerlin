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
#include <gtk/gtk.h>

#include <config.h>

#include <gmerlin/parameter.h>
#include <gmerlin/utils.h>
#include <gui_gtk/gtkutils.h>
#include <gui_gtk/multiinfo.h>
#include <gui_gtk/textview.h>

typedef struct 
  {
  GtkWidget * window;
  GtkWidget * close_button;
  bg_gtk_textview_t * textview1;
  bg_gtk_textview_t * textview2;
  } multiwindow_t;

static void button_callback(GtkWidget * w, gpointer data)
  {
  multiwindow_t * win;
  win = (multiwindow_t*)data;
  bg_gtk_textview_destroy(win->textview1);
  bg_gtk_textview_destroy(win->textview2);
  gtk_widget_hide(win->window);
  gtk_widget_destroy(win->window);
  free(win);
  } 

static gboolean delete_callback(GtkWidget * w, GdkEventAny * event,
                                gpointer data)
  {
  button_callback(w, data);
  return TRUE;
  }

static multiwindow_t *
multiwindow_create(const char * title, const char * properties, const char * description)
  {
  GtkWidget * table;
  GtkWidget * frame;

  multiwindow_t * ret;
  ret = calloc(1, sizeof(*ret));

  ret->window = bg_gtk_window_new(GTK_WINDOW_TOPLEVEL);
  
  gtk_window_set_position(GTK_WINDOW(ret->window), GTK_WIN_POS_CENTER_ON_PARENT);
  g_signal_connect(G_OBJECT(ret->window), "delete_event",
                   G_CALLBACK(delete_callback), (gpointer)ret);

  gtk_window_set_title(GTK_WINDOW(ret->window), title);

  /* Create close button */

  ret->close_button = gtk_button_new_with_mnemonic("_Close");
  bg_gtk_widget_set_can_default(ret->close_button, TRUE);

  g_signal_connect(G_OBJECT(ret->close_button), "clicked",
                   G_CALLBACK(button_callback), (gpointer)ret);
  gtk_widget_show(ret->close_button);
  
  /* Create texts */
  
  ret->textview1 = bg_gtk_textview_create();
  bg_gtk_textview_update(ret->textview1, properties);
  
  ret->textview2 = bg_gtk_textview_create();
  bg_gtk_textview_update(ret->textview2, description);

  table = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(table), 5);
  gtk_grid_set_column_spacing(GTK_GRID(table), 5);
  gtk_container_set_border_width(GTK_CONTAINER(table), 5);


  frame = gtk_frame_new("Properties");
  gtk_container_add(GTK_CONTAINER(frame),
                    bg_gtk_textview_get_widget(ret->textview1));
  gtk_widget_show(frame);
  
  bg_gtk_table_attach_defaults(table,
                               frame, 0, 1, 0, 1);

  frame = gtk_frame_new("Description");
  gtk_container_add(GTK_CONTAINER(frame),
                    bg_gtk_textview_get_widget(ret->textview2));
  gtk_widget_show(frame);
  
  bg_gtk_table_attach_defaults(table,
                            frame, 0, 1, 1, 2);
  
  bg_gtk_table_attach(table, ret->close_button, 0, 1, 2, 3, 0, 0);
  gtk_widget_show(table);
  gtk_container_add(GTK_CONTAINER(ret->window), table);
    
  return ret;
  }

static void multiwindow_show(multiwindow_t * w, int modal)
  {
  gtk_window_set_modal(GTK_WINDOW(w->window), modal);

  gtk_widget_grab_default(w->close_button);
  gtk_widget_show(w->window);
  }


void bg_gtk_multi_info_show(const bg_parameter_info_t * info, int i,
                            const char * translation_domain, GtkWidget * parent)
  {
  char * text;
  multiwindow_t * win;
  
  text = gavl_sprintf(TR("Name:\t %s\nLabel:\t %s"),
                    info->multi_names[i],
                    (info->multi_labels?TR_DOM(info->multi_labels[i]):info->multi_names[i]));
  

  win = multiwindow_create(info->long_name,
                           text, info->multi_descriptions ? info->multi_descriptions[i] :
                           TR("Not available"));
  
  free(text);

  parent = bg_gtk_get_toplevel(parent);
  if(parent)
    gtk_window_set_transient_for(GTK_WINDOW(win->window), GTK_WINDOW(parent));
  
  multiwindow_show(win, 1);
  }


