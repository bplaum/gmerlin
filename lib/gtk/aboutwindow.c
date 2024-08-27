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




#include <config.h>

#include <stdlib.h>


#include <gtk/gtk.h>
#include <gui_gtk/aboutwindow.h>

#include <gmerlin/utils.h>
#include <gmerlin/application.h>

static gboolean delete_callback(GtkWidget * w, GdkEventAny * event,
                                gpointer data)
  {
  gtk_widget_hide(w);
  return TRUE;
  }

static void response_callback(GtkWidget * w, int id,
                              gpointer data)
  {
  gtk_widget_hide(w);
  }


GtkWidget * bg_gtk_about_window_create()
  {
  GtkWidget * ret;

  ret = gtk_about_dialog_new();

  gtk_about_dialog_set_program_name(GTK_ABOUT_DIALOG(ret), bg_app_get_label());
  gtk_about_dialog_set_version(GTK_ABOUT_DIALOG(ret), VERSION);

  gtk_about_dialog_set_license_type(GTK_ABOUT_DIALOG(ret), GTK_LICENSE_GPL_3_0);

  /* TODO: Enter new website once it is available */
  gtk_about_dialog_set_website(GTK_ABOUT_DIALOG(ret), "http://gmerlin.sourceforge.net");

  g_signal_connect(G_OBJECT(ret), "delete_event",
                   G_CALLBACK(delete_callback), NULL);
  g_signal_connect(G_OBJECT(ret), "response",
                   G_CALLBACK(response_callback), NULL);
  
  return ret;
  
  }

