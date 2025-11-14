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


static void send_response(GtkWidget *chooser, bg_msg_sink_t * sink, int id)
  {
  gavl_msg_t * msg;
  char * str;
  GtkFileFilter * filter;
  int format = -1;
  const bg_gtk_file_filter_t * filters;

  msg = bg_msg_sink_get(sink);
  
  gavl_msg_set_id_ns(msg, id, GAVL_MSG_NS_GUI);

  gavl_dictionary_set_string(&msg->header, GAVL_MSG_CONTEXT_ID,
                             g_object_get_data(G_OBJECT(chooser),
                                               GAVL_MSG_CONTEXT_ID));
  
  str = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
 
  if((filters = (const bg_gtk_file_filter_t*)g_object_get_data(G_OBJECT(chooser), "gmerlin-filters")) &&
     (filter = gtk_file_chooser_get_filter(GTK_FILE_CHOOSER(chooser))))
    {
    /* Ensure extension */
    int idx = 0;
    const char * filter_name = gtk_file_filter_get_name(filter);
    
    while(filters[idx].label)
      {
      if(!strcmp(filters[idx].label, filter_name))
        {
        format = filters[idx].type;

        /* Enforce extension */
        if(!gavl_string_ends_with(str, filters[idx].extension))
          {
          gchar * tmp = g_strconcat(str, filters[idx].extension, NULL);
          free(str);
          str = tmp;
          break;
          }
        }
      idx++;
      }
    
    }
  
  gavl_msg_set_arg_string(msg, 0, str);
  
  if(format >= 0)
    gavl_msg_set_arg_int(msg, 1, format);
  
  g_free(str);
    
  bg_msg_sink_put(sink);
    
  }


static void
write_callback(GtkWidget *chooser,
               gint       response_id,
               gpointer data)
  {
  if(response_id == GTK_RESPONSE_OK)
    {
    send_response(chooser, data, BG_MSG_DIALOG_FILE_SAVE);
    }
  g_idle_add ((GSourceFunc) bg_gtk_destroy_widget, chooser);
  }

static void
read_callback(GtkWidget *chooser,
               gint       response_id,
               gpointer data)
  {
  if(response_id == GTK_RESPONSE_OK)
    send_response(chooser, data, BG_MSG_DIALOG_FILE_LOAD);
  
  g_idle_add ((GSourceFunc) bg_gtk_destroy_widget, chooser);
  }

static void
directory_callback(GtkWidget *chooser,
               gint       response_id,
               gpointer data)
  {
  if(response_id == GTK_RESPONSE_OK)
    send_response(chooser, data, BG_MSG_DIALOG_DIRECTORY);
  
  g_idle_add ((GSourceFunc) bg_gtk_destroy_widget, chooser);
  }

void bg_gtk_get_filename_write(const char * title, const char * context_id,
                               const char * directory,
                               int ask_overwrite, GtkWidget * parent, bg_msg_sink_t * sink,
                               const bg_gtk_file_filter_t * filter)
  {
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

  if(filter)
    {
    int idx = 0;

    while(filter[idx].label)
      {
      GtkFileFilter * ff;
      char * pattern;
      
      ff = gtk_file_filter_new();

      pattern = gavl_sprintf("*%s", filter[idx].extension);
      gtk_file_filter_add_pattern(ff, pattern);
      free(pattern);
      
      gtk_file_filter_set_name(ff, filter[idx].label);
      gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(w), ff);
      idx++;
      }
    
    g_object_set_data(G_OBJECT(w), "gmerlin-filters", (gpointer)filter);
    }
  
  /* Set attributes */
  
  gtk_window_set_modal(GTK_WINDOW(w), 1);
  
  /* Set callbacks */
  
  g_signal_connect(G_OBJECT(w), "response", G_CALLBACK(write_callback), sink);
  
  /* Set the current directory */
  
  if(directory)
    gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(w), directory);
  
  /* Run the widget */

  gtk_window_present(GTK_WINDOW(w));
  }

void bg_gtk_get_filename_read(const char * title, const char * context_id,
                              const char * directory, GtkWidget * parent, bg_msg_sink_t * sink)
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
  
  if(directory)
    gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(w),
                                        directory);
  
  /* Run the widget */
  
  gtk_window_present(GTK_WINDOW(w));
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
  
  gtk_window_present(GTK_WINDOW(w));
  
  }

static void
fileselect_callback_new(GtkWidget *chooser,
                        gint       response_id,
                        gpointer data)
  {
  if(response_id == GTK_RESPONSE_OK)
    {
    GSList * file_list;
    GSList * tmp;
    int num, i;
    gavl_array_t filenames;
    gavl_msg_t * msg;
    bg_msg_sink_t * sink = data;
    
    gavl_array_init(&filenames);
  
    file_list =
      gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(chooser));
    
    num = g_slist_length(file_list);
  
    tmp = file_list;
  
    for(i = 0; i < num; i++)
      {
      gavl_string_array_add(&filenames, (char*)tmp->data);
      tmp = tmp->next;
      }
  
    msg = bg_msg_sink_get(sink);
    gavl_msg_set_id_ns(msg, BG_MSG_DIALOG_ADD_LOCATIONS, BG_MSG_NS_DIALOG);
    gavl_msg_set_arg_array_nocopy(msg, 0, &filenames);
    bg_msg_sink_put(sink);
    
    g_slist_foreach(file_list, (GFunc)g_free, NULL);
    g_slist_free(file_list);
    
    }
  
  g_idle_add ((GSourceFunc) bg_gtk_destroy_widget, chooser);
  
  }

void bg_gtk_load_media_files(const char * title,
                             const char * directory,
                             GtkWidget * parent,
                             bg_msg_sink_t * sink)
  {
  GtkWidget * filesel;
  
  parent = bg_gtk_get_toplevel(parent);
  
  /* Create fileselection */
  
  filesel =
    gtk_file_chooser_dialog_new(title,
                                GTK_WINDOW(parent),
                                GTK_FILE_CHOOSER_ACTION_OPEN,
                                TR("OK"),
                                GTK_RESPONSE_OK,
                                TR("Cancel"),
                                GTK_RESPONSE_CANCEL,
                                NULL);
  
  gtk_window_set_modal(GTK_WINDOW(filesel), 1);
  gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(filesel),
                                       TRUE);
  
  /* Set callbacks */
  g_signal_connect(filesel, "response",
                   G_CALLBACK(fileselect_callback_new),
                   (gpointer)sink);
  
  gtk_window_present(GTK_WINDOW(filesel));
  }
