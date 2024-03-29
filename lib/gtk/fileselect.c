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
#include <gui_gtk/question.h>
#include <gui_gtk/gtkutils.h>

#include <gmerlin/utils.h>

struct bg_gtk_filesel_s
  {
  GtkWidget * filesel;
  GtkWidget * plugin_menu;
  void (*add_files)(char ** files, void * data);

  void (*add_dir)(char * dir,
                  const char * plugin,
                  void * data);
  
  void (*close_notify)(bg_gtk_filesel_t * f, void * data);
  
  void * callback_data;

  char * cwd;
  int is_modal;
  
  int unsensitive;
  };

static void add_files(bg_gtk_filesel_t * f)
  {
  char ** filenames;
  GSList * file_list;
  GSList * tmp;
  gavl_dictionary_t vars;
  
  int num, i;

  gavl_dictionary_init(&vars);
  
  file_list =
    gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(f->filesel));
  
  num = g_slist_length(file_list);
  
  filenames = calloc(num+1, sizeof(*filenames));

  tmp = file_list;
  
  for(i = 0; i < num; i++)
    {
    filenames[i] = bg_url_append_vars(gavl_strdup((char*)tmp->data), &vars);
    tmp = tmp->next;
    }
  
  f->unsensitive = 1;
  gtk_widget_set_sensitive(f->filesel, 0);
  f->add_files(filenames, f->callback_data);
  gtk_widget_set_sensitive(f->filesel, 1);
  f->unsensitive = 0;
  
  g_slist_foreach(file_list, (GFunc)g_free, NULL);
  g_slist_free(file_list);

  for(i = 0; i < num; i++)
    free(filenames[i]);
  
  gavl_dictionary_free(&vars);
  free(filenames);
  }

static void add_dir(bg_gtk_filesel_t * f)
  {
  const char * plugin = NULL;
  char * tmp =
    gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(f->filesel));
  

  f->unsensitive = 1;
  gtk_widget_set_sensitive(f->filesel, 0);
  f->add_dir(tmp,
             plugin,
             f->callback_data);
  gtk_widget_set_sensitive(f->filesel, 1);
  f->unsensitive = 0;
  g_free(tmp);
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
    if(f->add_files)
      add_files(f);
    else if(f->add_dir)
      add_dir(f);
    }
  else
    {
    gtk_widget_hide(f->filesel);
    if(f->is_modal)
      gtk_main_quit();
  
    if(f->close_notify)
      f->close_notify(f, f->callback_data);
    bg_gtk_filesel_destroy(f);
    }
  }

#if 0
static gboolean delete_callback(GtkWidget * w, GdkEventAny * event,
                                gpointer data)
  {
  fileselect_callback(w, GTK_RESPONSE_CANCEL, data);
  return TRUE;
  }

static gboolean destroy_callback(GtkWidget * w, GdkEvent * event,
                                  gpointer data)
  {
  fileselect_callback(w, GTK_RESPONSE_CANCEL, data);
  return TRUE;
  }
#endif

static bg_gtk_filesel_t *
filesel_create(const char * title,
               void (*add_files)(char ** files, void * data),
               void (*close_notify)(bg_gtk_filesel_t *,
                                    void * data),
               void * user_data,
               GtkWidget * parent_window, bg_plugin_registry_t * plugin_reg,
               int type_mask, int flag_mask)
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
  
  ret->add_files     = add_files;
  ret->close_notify  = close_notify;
  ret->callback_data = user_data;

  return ret;
  }

bg_gtk_filesel_t *
bg_gtk_filesel_create(const char * title,
                      void (*add_file)(char ** files, void * data),
                      void (*close_notify)(bg_gtk_filesel_t *,
                                           void * data),
                      void * user_data,
                      GtkWidget * parent_window,
                      int type_mask, int flag_mask)
  {
  return filesel_create(title,
                        add_file,
                        close_notify,
                        user_data,
                        parent_window, bg_plugin_reg, type_mask, flag_mask);
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
