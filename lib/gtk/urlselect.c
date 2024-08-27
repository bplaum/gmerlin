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
  GtkWidget * window;
  GtkWidget * add_button;
  GtkWidget * close_button;
  GtkWidget * entry;
    
  void (*add_files)(char ** files,
                    void * data);

  void (*close_notify)(bg_gtk_urlsel_t * f, void * data);
  
  void * callback_data;

  int is_modal;
  };

static void button_callback(GtkWidget * w, gpointer data)
  {
  bg_gtk_urlsel_t * f;

  char * urls[2];
  
  f = (bg_gtk_urlsel_t *)data;

  if(w == f->add_button)
    {
    gavl_dictionary_t vars;
    gavl_dictionary_init(&vars);
    
    urls[0] = bg_url_append_vars(gavl_strdup(gtk_entry_get_text(GTK_ENTRY(f->entry))), &vars);
    urls[1] = NULL;
    
    f->add_files(urls, f->callback_data);
    free(urls[0]);
    gavl_dictionary_free(&vars);
    }
  
  else if((w == f->window) || (w == f->close_button))
    {
    if(f->close_notify)
      f->close_notify(f, f->callback_data);
    
    gtk_widget_hide(f->window);
    if(f->is_modal)
      gtk_main_quit();
    bg_gtk_urlsel_destroy(f);
    }

  }

static gboolean delete_callback(GtkWidget * w, GdkEventAny * event,
                                gpointer data)
  {
  button_callback(w, data);
  return TRUE;
  }

static gboolean destroy_callback(GtkWidget * w, GdkEvent * event,
                                  gpointer data)
  {
  button_callback(w, data);
  return TRUE;
  }

bg_gtk_urlsel_t *
bg_gtk_urlsel_create(const char * title,
                     void (*add_files)(char ** files, void * data),
                     void (*close_notify)(bg_gtk_urlsel_t *,
                                          void * data),
                     void * user_data,
                     GtkWidget * parent_window,
                     bg_plugin_registry_t * plugin_reg, int type_mask,
                     int flag_mask)
  {
  bg_gtk_urlsel_t * ret;
  GtkWidget * box;
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
    g_signal_connect(G_OBJECT(ret->window), "destroy-event",
                     G_CALLBACK(destroy_callback), ret);
    }

  /* Create entry */
  
  ret->entry = gtk_entry_new();
  gtk_widget_show(ret->entry);
  
  
  /* Create Buttons */

  ret->add_button = gtk_button_new_with_mnemonic("_Add");
  ret->close_button = gtk_button_new_with_mnemonic("_Close");

  bg_gtk_widget_set_can_default(ret->close_button, TRUE);
  bg_gtk_widget_set_can_default(ret->add_button, TRUE);
    
  /* Set callbacks */

  g_signal_connect(G_OBJECT(ret->window), "delete-event",
                   G_CALLBACK(delete_callback), ret);
  g_signal_connect(G_OBJECT(ret->add_button),
                   "clicked", G_CALLBACK(button_callback), ret);
  g_signal_connect(G_OBJECT(ret->close_button),
                   "clicked", G_CALLBACK(button_callback), ret);

  /* Show Buttons */

  gtk_widget_show(ret->add_button);
  gtk_widget_show(ret->close_button);
  
  /* Pack everything */

  mainbox = bg_gtk_vbox_new(5);
  box = bg_gtk_hbox_new(GTK_ORIENTATION_HORIZONTAL);
  
  label = gtk_label_new(TR("URL:"));
  gtk_widget_show(label);
  
  bg_gtk_box_pack_start(box, label, 0);
  bg_gtk_box_pack_start(box, ret->entry, 1);
  gtk_widget_show(box);
  bg_gtk_box_pack_start(mainbox, box, 1);


  box = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_container_add(GTK_CONTAINER(box), ret->close_button);
  gtk_container_add(GTK_CONTAINER(box), ret->add_button);
  gtk_widget_show(box);
  
  bg_gtk_box_pack_start(mainbox, box, 1);
  
  gtk_widget_show(mainbox);
  gtk_container_add(GTK_CONTAINER(ret->window), mainbox);
  
  /* Set pointers */
  
  ret->add_files = add_files;
  ret->close_notify = close_notify;
  ret->callback_data = user_data;
  
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
  parent = bg_gtk_get_toplevel(parent);
  if(parent)
    gtk_window_set_transient_for(GTK_WINDOW(urlsel->window),
                                 GTK_WINDOW(parent));


  gtk_window_set_modal(GTK_WINDOW(urlsel->window), modal);
  gtk_widget_show(urlsel->window);

  gtk_widget_grab_default(urlsel->close_button);
  gtk_widget_grab_focus(urlsel->close_button);
  
  urlsel->is_modal = modal;
  if(modal)
    gtk_main();
  
  }
