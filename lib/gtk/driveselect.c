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
#include <stdlib.h>
#include <stdio.h>
#include <gtk/gtk.h>

#include <config.h>

#include <gmerlin/pluginregistry.h>

#include <gui_gtk/driveselect.h>
#include <gui_gtk/gtkutils.h>

#include <gmerlin/utils.h>

struct bg_gtk_drivesel_s
  {
  GtkWidget * window;
  GtkWidget * add_button;
  GtkWidget * close_button;
  };


bg_gtk_drivesel_t *
bg_gtk_drivesel_create(const char * title,
                       void (*add_files)(char ** files, void * data),
                       void (*close_notify)(bg_gtk_drivesel_t *,
                                            void * data),
                       void * user_data,
                       GtkWidget * parent_window,
                       bg_plugin_registry_t * plugin_reg,
                       int type_mask, int flag_mask)
  {
  bg_gtk_drivesel_t * ret;
  GtkWidget * box;
  GtkWidget * table;
  GtkWidget * mainbox;
  GtkWidget * label;
  
  ret = calloc(1, sizeof(*ret));
  
  /* Create window */

  ret->window = bg_gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title(GTK_WINDOW(ret->window), title);
  gtk_window_set_position(GTK_WINDOW(ret->window), GTK_WIN_POS_CENTER_ON_PARENT);
  gtk_container_set_border_width(GTK_CONTAINER(ret->window), 5);
    
  if(parent_window)
    {
    gtk_window_set_transient_for(GTK_WINDOW(ret->window),
                                 GTK_WINDOW(parent_window));
    gtk_window_set_destroy_with_parent(GTK_WINDOW(ret->window), TRUE);
    }

  /* Create Buttons */

  ret->add_button = gtk_button_new_with_mnemonic("_Add");
  ret->close_button = gtk_button_new_with_mnemonic("_Close");

  bg_gtk_widget_set_can_default(ret->close_button, TRUE);
  bg_gtk_widget_set_can_default(ret->add_button, TRUE);
  

  /* Show Buttons */

  gtk_widget_show(ret->add_button);
  gtk_widget_show(ret->close_button);
  
  /* Pack everything */

  mainbox = bg_gtk_vbox_new(5);

  table = gtk_grid_new();

  gtk_grid_set_column_spacing(GTK_GRID(table), 5);
  gtk_grid_set_row_spacing(GTK_GRID(table), 5);
  
  label = gtk_label_new(TR("Drive:"));
  gtk_widget_show(label);

  bg_gtk_table_attach(table, label, 0, 1, 1, 2, 0, 0);
  
  gtk_widget_show(table);
  bg_gtk_box_pack_start(mainbox, table, 1);
  
  box = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);

  gtk_container_add(GTK_CONTAINER(box), ret->close_button);
  gtk_container_add(GTK_CONTAINER(box), ret->add_button);
  gtk_widget_show(box);
  bg_gtk_box_pack_start(mainbox, box, 1);
  
  gtk_widget_show(mainbox);
  gtk_container_add(GTK_CONTAINER(ret->window), mainbox);
  
  return ret;
  }

/* Destroy driveselector */

void bg_gtk_drivesel_destroy(bg_gtk_drivesel_t * drivesel)
  {
  free(drivesel);
  }

/* Show the window */

void bg_gtk_drivesel_run(bg_gtk_drivesel_t * drivesel, int modal,
                         GtkWidget * parent)
  {
  if(modal)
    {
    parent = bg_gtk_get_toplevel(parent);
    if(parent)
      gtk_window_set_transient_for(GTK_WINDOW(drivesel->window),
                                   GTK_WINDOW(parent));
    
    }
  
  gtk_window_set_modal(GTK_WINDOW(drivesel->window), modal);
  gtk_widget_show(drivesel->window);

  gtk_widget_grab_focus(drivesel->close_button);
  gtk_widget_grab_default(drivesel->close_button);
  
  if(modal)
    gtk_main();
  
  }
