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
#include <stdio.h>
#include <gtk/gtk.h>

#include <config.h>

#include <gui_gtk/message.h>
#include <gui_gtk/gtkutils.h>

typedef struct
  {
  GtkWidget * window;
  GtkWidget * ok_button;
  GtkWidget * label;
  } message_t;

static void button_callback(GtkWidget * w, gpointer * data)
  {

  gtk_main_quit();
  }

static gboolean delete_callback(GtkWidget * w, GdkEventAny * evt,
                            gpointer data)
  {
  button_callback(w, data);
  return TRUE;
  }

void bg_gtk_message(const char * message, int type, GtkWidget * parent)
  {
  GtkWidget * buttonbox;
  message_t * q;
  GtkWidget * label;

  GtkWidget * mainbox;
  GtkWidget * hbox;
  GtkWidget * image = NULL;
    
  q = calloc(1, sizeof(*q));
    
  /* Create objects */
  
  q->window = bg_gtk_window_new(GTK_WINDOW_TOPLEVEL);

  gtk_window_set_position(GTK_WINDOW(q->window), GTK_WIN_POS_CENTER_ON_PARENT);

  parent = bg_gtk_get_toplevel(parent);
  if(parent)
    gtk_window_set_transient_for(GTK_WINDOW(q->window),
                                 GTK_WINDOW(parent));
  
  q->ok_button = gtk_button_new_with_mnemonic("_OK");
  label = gtk_label_new(message);

  if(type == BG_GTK_MESSAGE_INFO)
    image = gtk_image_new_from_icon_name("dialog-information",
                                         GTK_ICON_SIZE_DIALOG);
  else if(type == BG_GTK_MESSAGE_ERROR)
    image = gtk_image_new_from_icon_name("dialog-error",
                                         GTK_ICON_SIZE_DIALOG);
  
  /* Set attributes */

  gtk_window_set_modal(GTK_WINDOW(q->window), 1);
  gtk_window_set_title(GTK_WINDOW(q->window), TR("Message"));
  gtk_window_set_position(GTK_WINDOW(q->window), GTK_WIN_POS_CENTER_ON_PARENT);
  bg_gtk_widget_set_can_default(q->ok_button, TRUE);

  gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_CENTER);
  
  /* Set callbacks */

  g_signal_connect(G_OBJECT(q->ok_button), "clicked",
                   G_CALLBACK(button_callback),
                   (gpointer)q);


  g_signal_connect(G_OBJECT(q->window), "delete_event",
                   G_CALLBACK(delete_callback),
                   (gpointer)q);

  /* Show everything */

  gtk_widget_show(label);
  gtk_widget_show(image);
  gtk_widget_show(q->ok_button);
  
  /* Pack the objects */

  mainbox = bg_gtk_vbox_new(5);
  hbox    = bg_gtk_hbox_new(5);
    
  gtk_container_set_border_width(GTK_CONTAINER(mainbox), 5);
  
  bg_gtk_box_pack_start(hbox, image, 1);
  bg_gtk_box_pack_start(hbox, label, 1);
  gtk_widget_show(hbox);
  bg_gtk_box_pack_start(mainbox, hbox, 1);
  
  buttonbox = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);

  gtk_box_set_spacing(GTK_BOX(buttonbox), 10);
  
  gtk_container_add(GTK_CONTAINER(buttonbox), q->ok_button);

  gtk_widget_show(buttonbox);

  bg_gtk_box_pack_start(mainbox, buttonbox, 1);
  
  gtk_widget_show(mainbox);
  gtk_container_add(GTK_CONTAINER(q->window), mainbox);
  
  gtk_widget_show(q->window);
  
  gtk_main();

  /* Window has gone, fetch the answer */


  /* Destroy everything */
  gtk_widget_hide(q->window);
  gtk_widget_destroy(q->window);
  free(q);
  }
