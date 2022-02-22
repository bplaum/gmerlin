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

#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>
#include <gavl/metatags.h>

#include <config.h>

#include <gmerlin/log.h>
#include <gmerlin/utils.h>
#include <gmerlin/state.h>

#include <gui_gtk/logwindow.h>
#include <gui_gtk/gtkutils.h>


/* Delay between update calls in Milliseconds */
#define DELAY_TIME 50


struct bg_gtk_log_window_s
  {
  GtkWidget * window;
  GtkWidget * textview;
  GtkTextBuffer * buffer;
  GtkWidget * scrolledwindow;
  
  GtkTextTagTable * tag_table;
  GtkTextTag      * info_tag;
  GtkTextTag      * debug_tag;
  GtkTextTag      * error_tag;
  GtkTextTag      * warning_tag;

  int num_messages;
  int max_messages;
  
  char * last_error;
  
  bg_msg_sink_t * log_sink;
  };


static void delete_first_line(bg_gtk_log_window_t * win)
  {
  GtkTextIter start_iter, end_iter;

  gtk_text_buffer_get_iter_at_line(win->buffer, &start_iter, 0);
  gtk_text_buffer_get_iter_at_line(win->buffer, &end_iter, 1);
  gtk_text_buffer_delete(win->buffer, &start_iter, &end_iter);
  win->num_messages--;
  }

static void changed_callback(gpointer data)
  {
  bg_gtk_log_window_t * w;
  GtkTextIter iter;
  GtkTextMark * mark = NULL;
  w = data;
 
  gtk_text_buffer_get_end_iter(w->buffer, &iter);

  mark = gtk_text_buffer_create_mark(w->buffer, NULL, &iter, FALSE);
  gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(w->textview), mark);
  gtk_text_buffer_delete_mark(w->buffer, mark);
  }

static int handle_log_message(void * data, gavl_msg_t * msg)
  {
  bg_gtk_log_window_t * w = data;

  
  switch(msg->NS)
    {
    case GAVL_MSG_NS_LOG:
      {
      gavl_log_level_t level;
      const char * domain = NULL;
      const char * message = NULL;
      GtkTextTag * tag = NULL;
      char * str;
      GtkTextIter iter;
      int i;
      char ** lines;
      int got_message = 0;

      while(w->num_messages > w->max_messages - 1)
        delete_first_line(w);

      if(!gavl_log_msg_get(msg, &level,
                          &domain, &message))
        return 1;
      
      
      switch(level)
        {
        case GAVL_LOG_DEBUG:
          tag = w->debug_tag;
          break;
        case GAVL_LOG_WARNING:
          tag = w->warning_tag;
          break;
        case GAVL_LOG_ERROR:
          tag = w->error_tag;
          w->last_error = gavl_strrep(w->last_error, message);
          break;
        case GAVL_LOG_INFO:
          tag = w->info_tag;
          break;
        }

      gtk_text_buffer_get_end_iter(w->buffer, &iter);

      if(*message == '\0') /* Empty string */
        {
        str = bg_sprintf("[%s]\n", domain);
        gtk_text_buffer_insert_with_tags(w->buffer,
                                         &iter,
                                         str, -1, tag, NULL);
        }
      else
        {
        lines = gavl_strbreak(message, '\n');
        i = 0;
        while(lines[i])
          {
          str = bg_sprintf("[%s]: %s\n", domain, lines[i]);
          gtk_text_buffer_insert_with_tags(w->buffer,
                                           &iter,
                                           str, -1, tag, NULL);
          free(str);
          i++;
          }
        gavl_strbreak_free(lines);
        }
      w->num_messages++;
      got_message = 1;
      
      if(got_message)
        changed_callback(w);
      }
      break;
    }

  return 1;
  }


static gboolean idle_callback(gpointer data)
  {
  bg_gtk_log_window_t * w = data;
  bg_msg_sink_iteration(w->log_sink);
  return TRUE;
  }


bg_gtk_log_window_t * bg_gtk_log_window_create(const char * app_name)
  {
  char * tmp_string;
  bg_gtk_log_window_t * ret;
  ret = calloc(1, sizeof(*ret));

  /* Create window */
  ret->window = bg_gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_default_size(GTK_WINDOW(ret->window), 500, 300);
  
  tmp_string = bg_sprintf(TR("%s messages"), app_name);
  gtk_window_set_title(GTK_WINDOW(ret->window), tmp_string);
  free(tmp_string);
  
  ret->log_sink = bg_msg_sink_create(handle_log_message, ret, 0);
  bg_log_add_dest(ret->log_sink);
  
  /* Create tag table */
  ret->tag_table   = gtk_text_tag_table_new();
  ret->info_tag    = gtk_text_tag_new(NULL);
  ret->debug_tag   = gtk_text_tag_new(NULL);
  ret->error_tag   = gtk_text_tag_new(NULL);
  ret->warning_tag = gtk_text_tag_new(NULL);

  gtk_text_tag_table_add(ret->tag_table, ret->info_tag);
  gtk_text_tag_table_add(ret->tag_table, ret->debug_tag);
  gtk_text_tag_table_add(ret->tag_table, ret->error_tag);
  gtk_text_tag_table_add(ret->tag_table, ret->warning_tag);

  /* Create textbuffer */

  ret->buffer = gtk_text_buffer_new(ret->tag_table);
  
  /* Create textview */  
  
  ret->textview = gtk_text_view_new_with_buffer(ret->buffer);
  gtk_text_view_set_editable(GTK_TEXT_VIEW(ret->textview), FALSE);
  gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(ret->textview), FALSE);
  gtk_widget_set_size_request(ret->textview, 300, 100);
  
  gtk_widget_show(ret->textview);

  /* Create scrolledwindow */
  ret->scrolledwindow = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(ret->scrolledwindow),
                                 GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  
  gtk_container_add(GTK_CONTAINER(ret->scrolledwindow), ret->textview);
  gtk_widget_show(ret->scrolledwindow);
  gtk_container_add(GTK_CONTAINER(ret->window), ret->scrolledwindow);
  
  
  /* Add idle callback */
  g_timeout_add(DELAY_TIME, idle_callback, (gpointer)ret);
  
  return ret;
  }

void bg_gtk_log_window_destroy(bg_gtk_log_window_t * win)
  {
  if(win->last_error)
    free(win->last_error);
  gtk_widget_destroy(win->window);

  bg_log_remove_dest(win->log_sink);
  bg_msg_sink_destroy(win->log_sink);
  
  free(win);
  }

GtkWidget * bg_gtk_log_window_get_widget(bg_gtk_log_window_t * w)
  {
  return w->window;
  }


/* Configuration stuff */

static const bg_parameter_info_t parameters[] =
  {
    {
      .name =        "max_messages",
      .long_name =   TRS("Number of messages"),
      .type =        BG_PARAMETER_INT,
      .val_default = GAVL_VALUE_INIT_INT(200),
      .help_string = TRS("Maximum number of messages hold in the window")
    },
    {
      .name =        "info_color",
      .long_name =   TRS("Info foreground"),
      .type =        BG_PARAMETER_COLOR_RGB,
      .val_default = GAVL_VALUE_INIT_COLOR_RGB(0.0, 0.0, 0.0),
      .help_string = TRS("Color for info messages"),
    },
    {
      .name =        "warning_color",
      .long_name =   TRS("Warning foreground"),
      .type =        BG_PARAMETER_COLOR_RGB,
      .val_default = GAVL_VALUE_INIT_COLOR_RGB(1.0, 0.5, 0.0),
      .help_string = TRS("Color for warning messages"),
    },
    {
      .name =        "error_color",
      .long_name =   TRS("Error foreground"),
      .type =        BG_PARAMETER_COLOR_RGB,
      .val_default = GAVL_VALUE_INIT_COLOR_RGB(1.0, 0.0, 0.0),
      .help_string = TRS("Color for error messages"),
    },
    {
      .name =        "debug_color",
      .long_name =   TRS("Debug foreground"),
      .type =        BG_PARAMETER_COLOR_RGB,
      .val_default = GAVL_VALUE_INIT_COLOR_RGB(0.0, 0.0, 1.0),
      .help_string = TRS("Color for debug messages"),
    },
    {
      .name = "x",
      .long_name = "X",
      .flags = BG_PARAMETER_HIDE_DIALOG,
      .type = BG_PARAMETER_INT,
      .val_default = GAVL_VALUE_INIT_INT(100)
    },
    {
      .name = "y",
      .long_name = "Y",
      .flags = BG_PARAMETER_HIDE_DIALOG,
      .type = BG_PARAMETER_INT,
      .val_default = GAVL_VALUE_INIT_INT(100)
    },
    {
      .name = "width",
      .long_name = "Width",
      .flags = BG_PARAMETER_HIDE_DIALOG,
      .type = BG_PARAMETER_INT,
      .val_default = GAVL_VALUE_INIT_INT(0)
    },
    {
      .name = "height",
      .long_name = "Height",
      .flags = BG_PARAMETER_HIDE_DIALOG,
      .type = BG_PARAMETER_INT,
      .val_default = GAVL_VALUE_INIT_INT(0)
    },
    { /* */ }
  };

const bg_parameter_info_t *
bg_gtk_log_window_get_parameters(bg_gtk_log_window_t * w)
  {
  return parameters;
  }

void bg_gtk_log_window_set_parameter(void * data, const char * name,
                                     const gavl_value_t * v)
  {
  GdkColor color;
  bg_gtk_log_window_t * win;
  if(!name)
    return;

  win = (bg_gtk_log_window_t *)data;

  if(!strcmp(name, "max_messages"))
    {
    win->max_messages = v->v.i;
    while(win->num_messages > win->max_messages)
      delete_first_line(win);
    }
  
  else if(!strcmp(name, "info_color"))
    {
    color.red   = (guint16)(v->v.color[0] * 65535.0);
    color.green = (guint16)(v->v.color[1] * 65535.0);
    color.blue  = (guint16)(v->v.color[2] * 65535.0);
    g_object_set(win->info_tag, "foreground-gdk", &color, NULL);
    }
  else if(!strcmp(name, "warning_color"))
    {
    color.red   = (guint16)(v->v.color[0] * 65535.0);
    color.green = (guint16)(v->v.color[1] * 65535.0);
    color.blue  = (guint16)(v->v.color[2] * 65535.0);
    g_object_set(win->warning_tag, "foreground-gdk", &color, NULL);
    }
  else if(!strcmp(name, "error_color"))
    {
    color.red   = (guint16)(v->v.color[0] * 65535.0);
    color.green = (guint16)(v->v.color[1] * 65535.0);
    color.blue  = (guint16)(v->v.color[2] * 65535.0);
    g_object_set(win->error_tag, "foreground-gdk", &color, NULL);
    }
  else if(!strcmp(name, "debug_color"))
    {
    color.red   = (guint16)(v->v.color[0] * 65535.0);
    color.green = (guint16)(v->v.color[1] * 65535.0);
    color.blue  = (guint16)(v->v.color[2] * 65535.0);
    g_object_set(win->debug_tag, "foreground-gdk", &color, NULL);
    }
  }

void bg_gtk_log_window_flush(bg_gtk_log_window_t * win)
  {
  idle_callback(win);
  }

const char * bg_gtk_log_window_last_error(bg_gtk_log_window_t * win)
  {
  return win->last_error;
  }

  
