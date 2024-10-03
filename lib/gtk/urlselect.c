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
  
  int is_modal;
  };

static void
response_callback(GtkWidget *chooser,
                 gint       response_id,
                 gpointer data)
  {
  gavl_msg_t * msg;
  bg_gtk_urlsel_t * f = data;
  
  switch(response_id)
    {
    case GTK_RESPONSE_APPLY:
      {
      gavl_array_t filenames;
      const char * str;
      fprintf(stderr, "Apply\n");
      gavl_array_init(&filenames);

      str = gtk_entry_get_text(GTK_ENTRY(f->entry));
      gavl_string_array_add(&filenames, str);

      msg = bg_msg_sink_get(f->sink);
      gavl_msg_set_id_ns(msg, BG_MSG_DIALOG_ADD_LOCATIONS, BG_MSG_NS_DIALOG);
      gavl_dictionary_set_string(&msg->header, GAVL_MSG_CONTEXT_ID, f->ctx); 
      gavl_msg_set_arg_array_nocopy(msg, 0, &filenames);
      bg_msg_sink_put(f->sink);
      gavl_array_free(&filenames);
      }
      break;
    default:
      {
      fprintf(stderr, "Close\n");
      gtk_widget_hide(f->dialog);

      msg = bg_msg_sink_get(f->sink);
      gavl_msg_set_id_ns(msg, BG_MSG_DIALOG_CLOSED, BG_MSG_NS_DIALOG);
      gavl_dictionary_set_string(&msg->header, GAVL_MSG_CONTEXT_ID, f->ctx); 
      bg_msg_sink_put(f->sink);
      }
      break;
    }
  
  //  gtk_main_quit();
  }



bg_gtk_urlsel_t *
bg_gtk_urlsel_create(const char * title,
                     bg_msg_sink_t * sink,
                     const char * ctx,
                     GtkWidget * parent_window)
  {
  bg_gtk_urlsel_t * ret;
  GtkWidget * label;
  GtkWidget * box;
  GtkDialogFlags flags;
  GtkWidget * content_area;
  
  
  ret = calloc(1, sizeof(*ret));
  ret->sink = sink;
  ret->ctx = ctx;
  
  /* Create window */
  flags = GTK_DIALOG_DESTROY_WITH_PARENT;
  ret->dialog = gtk_dialog_new_with_buttons("Message",
                                            GTK_WINDOW(parent_window),
                                            flags,
                                            TR("Add"),
                                            GTK_RESPONSE_APPLY,
                                            TR("Close"),
                                            GTK_RESPONSE_CLOSE,
                                            NULL);

  g_signal_connect(G_OBJECT(ret->dialog), "response", G_CALLBACK(response_callback), ret);
  
  /* Create entry */
  
  ret->entry = gtk_entry_new();
  gtk_widget_show(ret->entry);
  
  /* Pack everything */
  
  box = bg_gtk_hbox_new(GTK_ORIENTATION_HORIZONTAL);
  
  label = gtk_label_new(TR("URL:"));
  gtk_widget_show(label);
  
  bg_gtk_box_pack_start(box, label, 0);
  bg_gtk_box_pack_start(box, ret->entry, 1);
  gtk_widget_show(box);

  content_area = gtk_dialog_get_content_area(GTK_DIALOG(ret->dialog));
  gtk_container_add(GTK_CONTAINER(content_area), box);
  
  return ret;
  }

/* Destroy urlselector */

void bg_gtk_urlsel_destroy(bg_gtk_urlsel_t * urlsel)
  {
  //  g_object_unref(G_OBJECT(urlsel));
  free(urlsel);
  }

/* Show the window */

void bg_gtk_urlsel_run(bg_gtk_urlsel_t * urlsel, int modal, GtkWidget * parent)
  {
  gtk_widget_show(urlsel->dialog);
  
  urlsel->is_modal = modal;
  if(modal)
    gtk_main();
  
  }
