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

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <gtk/gtk.h>
#include <gmerlin/pluginregistry.h>
#include <gui_gtk/fileselect.h>
#include <gui_gtk/gtkutils.h>

#include <gmerlin/utils.h>
#include <gmerlin/bgmsg.h>

struct bg_gtk_filesel_s
  {
  GtkWidget * filesel;
  GtkWidget * plugin_menu;

  bg_msg_sink_t * sink;
  const char * ctx;
  
  char * cwd;
  
  int unsensitive;
  };

static void add_files(bg_gtk_filesel_t * f)
  {
  GSList * file_list;
  GSList * tmp;
  int num, i;
  gavl_array_t filenames;
  gavl_msg_t * msg;
  
  gavl_array_init(&filenames);
  
  file_list =
    gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(f->filesel));
  
  num = g_slist_length(file_list);
  
  tmp = file_list;
  
  for(i = 0; i < num; i++)
    {
    gavl_string_array_add(&filenames, (char*)tmp->data);
    tmp = tmp->next;
    }
  
  f->unsensitive = 1;
  gtk_widget_set_sensitive(f->filesel, 0);

  msg = bg_msg_sink_get(f->sink);
  gavl_msg_set_id_ns(msg, BG_MSG_DIALOG_ADD_LOCATIONS, BG_MSG_NS_DIALOG);
  gavl_dictionary_set_string(&msg->header, GAVL_MSG_CONTEXT_ID, f->ctx); 
  gavl_msg_set_arg_array_nocopy(msg, 0, &filenames);
  bg_msg_sink_put(f->sink);
  
  gtk_widget_set_sensitive(f->filesel, 1);
  f->unsensitive = 0;
  
  g_slist_foreach(file_list, (GFunc)g_free, NULL);
  g_slist_free(file_list);
  }


static void
fileselect_callback(GtkWidget *chooser,
                    gint       response_id,
                    gpointer data)
  {
  bg_gtk_filesel_t * f;
  f = (bg_gtk_filesel_t *)data;
  if(f->unsensitive)
    return;

  if(response_id == GTK_RESPONSE_APPLY)
    {
    add_files(f);
    }
  else
    {
    gavl_msg_t * msg;
    gtk_widget_hide(f->filesel);

    msg = bg_msg_sink_get(f->sink);
    gavl_msg_set_id_ns(msg, BG_MSG_DIALOG_CLOSED, BG_MSG_NS_DIALOG);
    gavl_dictionary_set_string(&msg->header, GAVL_MSG_CONTEXT_ID, f->ctx); 
    bg_msg_sink_put(f->sink);
    }
  }

bg_gtk_filesel_t *
bg_gtk_filesel_create(const char * title,
                      bg_msg_sink_t * sink,
                      const char * ctx,
                      GtkWidget * parent_window)
  {
  bg_gtk_filesel_t * ret;
  
  ret = calloc(1, sizeof(*ret));
  
  parent_window = bg_gtk_get_toplevel(parent_window);
  
  /* Create fileselection */
  
  ret->filesel =
    gtk_file_chooser_dialog_new(title,
                                GTK_WINDOW(parent_window),
                                GTK_FILE_CHOOSER_ACTION_OPEN,
                                TR("Add"),
                                GTK_RESPONSE_APPLY,
                                TR("Close"),
                                GTK_RESPONSE_CLOSE,
                                NULL);
  gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(ret->filesel),
                                       TRUE);
  
  gtk_window_set_default_size(GTK_WINDOW(ret->filesel), 400, 400);
  
  /* Set callbacks */
  g_signal_connect(ret->filesel, "response",
                   G_CALLBACK(fileselect_callback),
                   (gpointer)ret);

  ret->sink = sink;
  ret->ctx = ctx;
  
  return ret;
  }



/* Destroy fileselector */

void bg_gtk_filesel_destroy(bg_gtk_filesel_t * filesel)
  {
  if(filesel->cwd)
    g_free(filesel->cwd);
  gtk_widget_destroy(filesel->filesel);
  free(filesel);
  }

/* Show the window */

void bg_gtk_filesel_run(bg_gtk_filesel_t * filesel, int modal)
  {
  gtk_window_set_modal(GTK_WINDOW(filesel->filesel), modal);
  
  gtk_widget_show(filesel->filesel);
  
  }

/* Get the current working directory */

void bg_gtk_filesel_set_directory(bg_gtk_filesel_t * filesel,
                                  const char * dir)
  {
  gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(filesel->filesel), dir);
  }

const char * bg_gtk_filesel_get_directory(bg_gtk_filesel_t * filesel)
  {
  if(filesel->cwd)
    g_free(filesel->cwd);
  filesel->cwd =
    gtk_file_chooser_get_current_folder(GTK_FILE_CHOOSER(filesel->filesel));
  return filesel->cwd;
  }

/*
 *  Create a temporary fileselector and ask
 *  for a file to save something
 *
 *  Return value should be freed with free();
 */

#if 0
typedef struct
  {
  GtkWidget * w;
  int answer;
  } filesel_write_struct;
#endif

static void send_response(GtkWidget *chooser, bg_msg_sink_t * sink, int id)
  {
  gavl_msg_t * msg;
  char * str;
    
  msg = bg_msg_sink_get(sink);
  
  gavl_msg_set_id_ns(msg, id, GAVL_MSG_NS_GUI);

  gavl_dictionary_set_string(&msg->header, GAVL_MSG_CONTEXT_ID, g_object_get_data(G_OBJECT(chooser), GAVL_MSG_CONTEXT_ID));

  str = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
  gavl_msg_set_arg_string(msg, 0, str);
  g_free(str);
    
  bg_msg_sink_put(sink);
    
  }

static gboolean destroy_widget(gpointer data)
  {
  gtk_widget_destroy(data);
  return G_SOURCE_REMOVE;
  }

static void
write_callback(GtkWidget *chooser,
               gint       response_id,
               gpointer data)
  {
  if(response_id == GTK_RESPONSE_OK)
    send_response(chooser, data, GAVL_MSG_GUI_FILE_SAVE);
  
  g_idle_add ((GSourceFunc) destroy_widget, chooser);
  }

static void
read_callback(GtkWidget *chooser,
               gint       response_id,
               gpointer data)
  {
  if(response_id == GTK_RESPONSE_OK)
    send_response(chooser, data, GAVL_MSG_GUI_FILE_LOAD);
  
  g_idle_add ((GSourceFunc) destroy_widget, chooser);
  }

static void
directory_callback(GtkWidget *chooser,
               gint       response_id,
               gpointer data)
  {
  if(response_id == GTK_RESPONSE_OK)
    send_response(chooser, data, GAVL_MSG_GUI_DIRECTORY);
  
  g_idle_add ((GSourceFunc) destroy_widget, chooser);
  }

void bg_gtk_get_filename_write(const char * title, const char * context_id,
                               char ** directory,
                               int ask_overwrite, GtkWidget * parent, bg_msg_sink_t * sink)
  {
  //  char * ret;
  //  char * tmp_string;
  GtkWidget * w;
  
  parent = bg_gtk_get_toplevel(parent);
  
  w =     
    gtk_file_chooser_dialog_new(title,
                                GTK_WINDOW(parent),
                                GTK_FILE_CHOOSER_ACTION_SAVE,
                                TR("_Cancel"),
                                GTK_RESPONSE_CANCEL,
                                TR("_OK"),
                                GTK_RESPONSE_OK,
                                NULL);

  if(context_id)
    g_object_set_data_full(G_OBJECT(w), GAVL_MSG_CONTEXT_ID, 
                           g_strdup(context_id), g_free);
  
  if(ask_overwrite)
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(w),
                                                   TRUE);
  /* Set attributes */
  
  gtk_window_set_modal(GTK_WINDOW(w), 1);
  
  /* Set callbacks */
  
  g_signal_connect(G_OBJECT(w), "response", G_CALLBACK(write_callback), sink);
  
  /* Set the current directory */
  
  if(directory && *directory)
    gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(w), *directory);
  
  /* Run the widget */
  
  gtk_widget_show(w);
  }

void bg_gtk_get_filename_read(const char * title, const char * context_id,
                              char ** directory, GtkWidget * parent, bg_msg_sink_t * sink)
  {
  GtkWidget * w;
  
  parent = bg_gtk_get_toplevel(parent);
  
  w =     
    gtk_file_chooser_dialog_new(title,
                                GTK_WINDOW(parent),
                                GTK_FILE_CHOOSER_ACTION_OPEN,
                                TR("_Cancel"),
                                GTK_RESPONSE_CANCEL,
                                TR("_OK"), GTK_RESPONSE_OK,
                                NULL);

  if(context_id)
    g_object_set_data_full(G_OBJECT(w), GAVL_MSG_CONTEXT_ID, 
                           g_strdup(context_id), g_free);
  
  /* Set attributes */
  
  gtk_window_set_modal(GTK_WINDOW(w), 1);
  
  /* Set callbacks */
  
  g_signal_connect(G_OBJECT(w), "response",
                   G_CALLBACK(read_callback),
                   sink);

  /* Set the current directory */
  
  if(directory && *directory)
    gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(w),
                                        *directory);
  
  /* Run the widget */
  
  gtk_widget_show(w);
  }

void bg_gtk_get_directory(const char * title, const char * context_id,
                          GtkWidget * parent, bg_msg_sink_t * sink)
  {
  GtkWidget * w;
  parent = bg_gtk_get_toplevel(parent);
  
  w =     
    gtk_file_chooser_dialog_new(title,
                                GTK_WINDOW(parent),
                                GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
                                TR("_Cancel"),
                                GTK_RESPONSE_CANCEL,
                                TR("_OK"), GTK_RESPONSE_OK,
                                NULL);

  if(context_id)
    g_object_set_data_full(G_OBJECT(w), GAVL_MSG_CONTEXT_ID, 
                           g_strdup(context_id), g_free);
  
  /* Set attributes */
  
  gtk_window_set_modal(GTK_WINDOW(w), 1);
  
  /* Set callbacks */
  
  g_signal_connect(G_OBJECT(w), "response",
                   G_CALLBACK(directory_callback),
                   sink);
  
  /* Run the widget */
  
  gtk_widget_show(w);
  
  }
