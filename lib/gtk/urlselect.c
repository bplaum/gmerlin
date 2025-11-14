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



#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <gtk/gtk.h>
// #include <gui_gtk/question.h>

#include <config.h>

#include <gmerlin/pluginregistry.h>

#include <gui_gtk/urlselect.h>
#include <gui_gtk/gtkutils.h>

#include <gmerlin/utils.h>

struct bg_gtk_urlsel_s
  {
  GtkWidget * entry;
  GtkWidget * dialog;

  bg_msg_sink_t * sink;
  const char * ctx;
  };

static void
response_callback(GtkWidget *chooser,
                 gint       response_id,
                 gpointer data)
  {
  gavl_msg_t * msg;
  //  bg_gtk_urlsel_t * f = data;

  bg_msg_sink_t * sink = data;
  
  switch(response_id)
    {
    case GTK_RESPONSE_OK:
      {
      gavl_array_t filenames;
      const char * str;
      GtkWidget * entry = bg_gtk_find_widget_by_name(chooser, "entry");
      
      //      fprintf(stderr, "Apply\n");
      gavl_array_init(&filenames);

      str = gtk_entry_get_text(GTK_ENTRY(entry));
      gavl_string_array_add(&filenames, str);

      msg = bg_msg_sink_get(sink);
      gavl_msg_set_id_ns(msg, BG_MSG_DIALOG_ADD_LOCATIONS, BG_MSG_NS_DIALOG);
      gavl_msg_set_arg_array_nocopy(msg, 0, &filenames);
      bg_msg_sink_put(sink);
      gavl_array_free(&filenames);
      }
      break;
    }

  g_idle_add ((GSourceFunc) bg_gtk_destroy_widget, chooser);
  
  }

void
bg_gtk_urlsel_show(const char * title,
                   bg_msg_sink_t * sink,
                   GtkWidget * parent_window)
  {
  GtkWidget * label;
  GtkWidget * entry;
  GtkWidget * box;
  GtkDialogFlags flags;
  GtkWidget * content_area;

  GtkWidget * dlg;
  
  /* Create window */
  flags = GTK_DIALOG_DESTROY_WITH_PARENT;
  dlg = gtk_dialog_new_with_buttons("Load URL",
                                    GTK_WINDOW(parent_window),
                                    flags,
                                    TR("Add"),
                                    GTK_RESPONSE_OK,
                                    TR("Close"),
                                    GTK_RESPONSE_CLOSE,
                                    NULL);

  g_signal_connect(G_OBJECT(dlg), "response", G_CALLBACK(response_callback), sink);
  
  /* Create entry */
  
  entry = gtk_entry_new();
  gtk_widget_set_name(entry, "entry");
  gtk_widget_show(entry);
  
  /* Pack everything */
  
  box = bg_gtk_hbox_new(GTK_ORIENTATION_HORIZONTAL);
  
  label = gtk_label_new(TR("URL:"));
  gtk_widget_show(label);
  
  bg_gtk_box_pack_start(box, label, 0);
  bg_gtk_box_pack_start(box, entry, 1);
  gtk_widget_show(box);

  content_area = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
  gtk_container_add(GTK_CONTAINER(content_area), box);

  gtk_window_set_modal(GTK_WINDOW(dlg), 1);
  gtk_window_present(GTK_WINDOW(dlg));
  
  return;
  }
