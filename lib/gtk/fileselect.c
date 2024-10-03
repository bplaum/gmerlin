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
  int is_modal;
  
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

  if(response_id == GTK_RESPONSE_OK)
    {
    add_files(f);
    }
  else
    {
    gavl_msg_t * msg;
    gtk_widget_hide(f->filesel);
    if(f->is_modal)
      gtk_main_quit();

    msg = bg_msg_sink_get(f->sink);
    gavl_msg_set_id_ns(msg, BG_MSG_DIALOG_CLOSED, BG_MSG_NS_DIALOG);
    gavl_dictionary_set_string(&msg->header, GAVL_MSG_CONTEXT_ID, f->ctx); 
    bg_msg_sink_put(f->sink);
    bg_gtk_filesel_destroy(f);
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
                                TR("_Cancel"),
                                GTK_RESPONSE_CANCEL,
                                TR("_OK"), GTK_RESPONSE_OK,
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
  //  g_object_unref(G_OBJECT(filesel));
  free(filesel);
  }

/* Show the window */

void bg_gtk_filesel_run(bg_gtk_filesel_t * filesel, int modal)
  {
  gtk_window_set_modal(GTK_WINDOW(filesel->filesel), modal);
  
  gtk_widget_show(filesel->filesel);
  filesel->is_modal = modal;
  if(modal)
    gtk_main();
  
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

typedef struct
  {
  GtkWidget * w;
  int answer;
  } filesel_write_struct;

static void
write_callback(GtkWidget *chooser,
               gint       response_id,
               gpointer data)
  {
  filesel_write_struct * ws;
  
  ws = (filesel_write_struct*)data;
  
  if(response_id == GTK_RESPONSE_OK)
    ws->answer = 1;
  
  gtk_widget_hide(ws->w);
  gtk_main_quit();
  }

static gboolean write_delete_callback(GtkWidget * w,
                                      GdkEventAny * evt,
                                      gpointer data)
  {
  write_callback(w, GTK_RESPONSE_CANCEL, data);
  return TRUE;
  }

char * bg_gtk_get_filename_write(const char * title,
                                 char ** directory,
                                 int ask_overwrite, GtkWidget * parent,
                                 GtkWidget * extra)
  {
  char * ret;
  char * tmp_string;
  filesel_write_struct f;

  ret = NULL;
  
  parent = bg_gtk_get_toplevel(parent);
  
  f.w =     
    gtk_file_chooser_dialog_new(title,
                                GTK_WINDOW(parent),
                                GTK_FILE_CHOOSER_ACTION_SAVE,
                                TR("_Cancel"),
                                GTK_RESPONSE_CANCEL,
                                TR("_OK"),
                                GTK_RESPONSE_OK,
                                NULL);
  if(extra)
    gtk_file_chooser_set_extra_widget(GTK_FILE_CHOOSER(f.w), extra);
  
  if(ask_overwrite)
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(f.w),
                                                   TRUE);
  /* Set attributes */
  
  gtk_window_set_modal(GTK_WINDOW(f.w), 1);
  f.answer = 0;
  
  /* Set callbacks */
  
  g_signal_connect(G_OBJECT(f.w), "delete_event",
                   G_CALLBACK(write_delete_callback),
                   (gpointer)(&f));
  g_signal_connect(G_OBJECT(f.w), "response",
                   G_CALLBACK(write_callback),
                   (gpointer)(&f));

  
  /* Set the current directory */
  
  if(directory && *directory)
    gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(f.w),
                                        *directory);
  
  /* Run the widget */
  
  gtk_widget_show(f.w);
  gtk_main();
  
  /* Fetch the answer */
  
  if(!f.answer)
    {
    gtk_widget_destroy(f.w);
    return NULL;
    }
  
  tmp_string = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(f.w));
  ret = gavl_strdup(tmp_string);
  g_free(tmp_string);
  
  /* Update current directory */
    
  if(directory)
    {
    tmp_string = gtk_file_chooser_get_current_folder(GTK_FILE_CHOOSER(f.w));
    *directory = gavl_strrep(*directory, tmp_string);
    g_free(tmp_string);
    }

  g_object_unref(f.w);
  
  return ret;
  }

char * bg_gtk_get_filename_read(const char * title,
                                char ** directory, GtkWidget * parent)
  {
  char * ret;
  char * tmp_string;
  filesel_write_struct f;

  ret = NULL;

  parent = bg_gtk_get_toplevel(parent);
  
  f.w =     
    gtk_file_chooser_dialog_new(title,
                                GTK_WINDOW(parent),
                                GTK_FILE_CHOOSER_ACTION_OPEN,
                                TR("_Cancel"),
                                GTK_RESPONSE_CANCEL,
                                TR("_OK"), GTK_RESPONSE_OK,
                                NULL);
  
  /* Set attributes */
  
  gtk_window_set_modal(GTK_WINDOW(f.w), 1);
  f.answer = 0;
  
  /* Set callbacks */
  
  g_signal_connect(G_OBJECT(f.w), "delete_event",
                   G_CALLBACK(write_delete_callback),
                   (gpointer)(&f));
  g_signal_connect(G_OBJECT(f.w), "response",
                   G_CALLBACK(write_callback),
                   (gpointer)(&f));

  /* Set the current directory */
  
  if(directory && *directory)
    gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(f.w),
                                        *directory);
  
  /* Run the widget */
  
  gtk_widget_show(f.w);
  gtk_main();
  
  /* Fetch the answer */
  
  if(!f.answer)
    {
    gtk_widget_destroy(f.w);
    return NULL;
    }
  
  tmp_string = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(f.w));
  ret = gavl_strdup(tmp_string);
  g_free(tmp_string);
  
  /* Update current directory */
    
  if(directory)
    {
    tmp_string = gtk_file_chooser_get_current_folder(GTK_FILE_CHOOSER(f.w));
    *directory = gavl_strrep(*directory, tmp_string);
    g_free(tmp_string);
    }
  
  return ret;
  }

char * bg_gtk_get_directory(const char * title,
                            GtkWidget * parent)
  {
  char * ret;
  char * tmp_string;
  filesel_write_struct f;

  ret = NULL;

  parent = bg_gtk_get_toplevel(parent);
  
  f.w =     
    gtk_file_chooser_dialog_new(title,
                                GTK_WINDOW(parent),
                                GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
                                TR("_Cancel"),
                                GTK_RESPONSE_CANCEL,
                                TR("_OK"), GTK_RESPONSE_OK,
                                NULL);
  
  /* Set attributes */
  
  gtk_window_set_modal(GTK_WINDOW(f.w), 1);
  f.answer = 0;
  
  /* Set callbacks */
  
  g_signal_connect(G_OBJECT(f.w), "delete_event",
                   G_CALLBACK(write_delete_callback),
                   (gpointer)(&f));
  g_signal_connect(G_OBJECT(f.w), "response",
                   G_CALLBACK(write_callback),
                   (gpointer)(&f));
  
  /* Run the widget */
  
  gtk_widget_show(f.w);
  gtk_main();
  
  /* Fetch the answer */
  
  if(!f.answer)
    {
    gtk_widget_destroy(f.w);
    return NULL;
    }
  
  tmp_string = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(f.w));
  ret = gavl_strdup(tmp_string);
  g_free(tmp_string);
  
  return ret;
  }
