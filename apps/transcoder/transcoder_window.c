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
#include <string.h>
#include <stdio.h>
#include <gtk/gtk.h>

#include <config.h>

#include <gavl/gavlsocket.h>
#include <gavl/log.h>
#define LOG_DOMAIN "transcoder_window"
#include <gmerlin/translation.h>

#include <gmerlin/pluginregistry.h>
#include <gmerlin/utils.h>

#include <gmerlin/transcoder_track.h>
#include <gmerlin/bgmsg.h>
#include <gmerlin/transcoder.h>
#include <gmerlin/transcodermsg.h>

#include <gmerlin/http.h>
#include <gmerlin/httpserver.h>
#include <gmerlin/websocket.h>
#include <gmerlin/iconfont.h>


#include <gmerlin/textrenderer.h>

#include <gmerlin/filters.h>

// #include <gui_gtk/display.h>
// #include <gui_gtk/scrolltext.h>
#include <gui_gtk/gtkutils.h>
#include <gui_gtk/logwindow.h>
#include <gui_gtk/aboutwindow.h>
#include <gui_gtk/fileselect.h>
#include <gui_gtk/configdialog.h>

#include "transcoder_window.h"

#include "tracklist.h"

static const uint32_t stream_flags = GAVL_STREAM_AUDIO |
GAVL_STREAM_VIDEO |
GAVL_STREAM_TEXT |
GAVL_STREAM_OVERLAY;

static const uint32_t plugin_flags = BG_PLUGIN_FILE;

#define CTX_TRACKLIST "tracklist"
#define CTX_PROFILE   "profile"

#define GET_CFG_STRING(name) \
  gavl_dictionary_get_string(bg_cfg_registry_find_section(bg_cfg_registry, \
                                                          "transcoder_window"), \
                             name)

#define SET_CFG_STRING(name, val)                                           \
  gavl_dictionary_set_string(bg_cfg_registry_find_section(bg_cfg_registry, \
                                                          "transcoder_window"), \
                             name, val)

#define SET_CFG_STRING_NOCOPY(name, val)                                       \
  gavl_dictionary_set_string_nocopy(bg_cfg_registry_find_section(bg_cfg_registry, \
                                                                  "transcoder_window"), \
                             name, val)

struct transcoder_window_s
  {
  GtkWidget * win;

  track_list_t * tracklist;

  GtkWidget * run_button;
  GtkWidget * stop_button;
  GtkWidget * properties_button;
  GtkWidget * quit_button;
  GtkWidget * load_button;
  GtkWidget * save_button;

  GtkWidget * statusbar;

  guint status_ctx_id;
  int status_active;
  
  bg_gtk_log_window_t * logwindow;
  
  GtkWidget * progress_bar;
  /* Configuration stuff */

  char * output_directory;
  int delete_incomplete;

  gavl_dictionary_t * track_defaults_section;

  bg_parameter_info_t * encoder_parameters;
  gavl_dictionary_t * encoder_section;
  gavl_dictionary_t encoder_params;
  
  /* The actual transcoder */

  bg_transcoder_t * transcoder;
  bg_transcoder_track_t * transcoder_track;
  
  /* Load/Save stuff */
  GtkWidget * filesel;
  GtkWidget * menubar;

  struct
    {
    GtkWidget * load_item;
    GtkWidget * save_item;
    GtkWidget * quit_item;
    GtkWidget * menu;
    } file_menu;

  struct
    {
    GtkWidget * config_item;
    GtkWidget * load_item;
    GtkWidget * save_item;
    GtkWidget * menu;
    } options_menu;

  struct
    {
    GtkWidget * run_item;
    GtkWidget * stop_item;
    GtkWidget * menu;
    } actions_menu;

  struct
    {
    GtkWidget * log_item;
    GtkWidget * menu;
    } windows_menu;

  struct
    {
    GtkWidget * about_item;
    //    GtkWidget * help_item;
    GtkWidget * menu;
    } help_menu;

  GtkWidget * about_window;

  bg_msg_sink_t * msg_sink;
  };


static void transcoder_window_save_profile(transcoder_window_t * win,
                                           const char * file);


static int start_transcode(transcoder_window_t * win);

static const bg_parameter_info_t transcoder_window_parameters[] =
  {
    {
      .name =        "task_path",
      .long_name =   "Task path",
      .type =        BG_PARAMETER_DIRECTORY,
      .flags =       BG_PARAMETER_HIDE_DIALOG,
      .val_default = GAVL_VALUE_INIT_STRING("."),
    },
    {
      .name =        "profile_path",
      .long_name =   "Profile path",
      .type =        BG_PARAMETER_DIRECTORY,
      .flags =       BG_PARAMETER_HIDE_DIALOG,
      .val_default = GAVL_VALUE_INIT_STRING("."),
    },
    {
      .name =        "show_logwindow",
      .long_name =   "Show log window",
      .type =        BG_PARAMETER_CHECKBUTTON,
      .flags =       BG_PARAMETER_HIDE_DIALOG,
      .val_default = GAVL_VALUE_INIT_INT(0),
    },
    {
      .name =        "gui",
      .long_name =   TRS("GUI"),
      .type =        BG_PARAMETER_SECTION,
    },
    {
      .name =        "show_tooltips",
      .long_name =   TRS("Show Tooltips"),
      .type =        BG_PARAMETER_CHECKBUTTON,
      .val_default = GAVL_VALUE_INIT_INT(1),
    },
    { /* End of parameters */ }
  };

static void set_status_message(transcoder_window_t * win, const char * msg)
  {
  if(win->status_active)
    gtk_statusbar_pop(GTK_STATUSBAR(win->statusbar), win->status_ctx_id);
  
  gtk_statusbar_push(GTK_STATUSBAR(win->statusbar), win->status_ctx_id, msg);
  win->status_active = 1;
  }

static void clear_status_message(transcoder_window_t * win)
  {
  if(win->status_active)
    {
    gtk_statusbar_pop(GTK_STATUSBAR(win->statusbar), win->status_ctx_id);
    win->status_active = 0;
    }
  }

static void
set_transcoder_window_parameter(void * data, const char * name,
                                const gavl_value_t * val)
  {
  if(!name)
    return;
  
  if(!strcmp(name, "show_tooltips"))
    {
    bg_gtk_set_tooltips(val->v.i);
    }
  }


  
static void
transcoder_window_preferences(transcoder_window_t * win);

static void finish_transcoding(transcoder_window_t * win)
  {
  if(win->transcoder)
    {
    bg_transcoder_finish(win->transcoder);
    bg_transcoder_destroy(win->transcoder);
    win->transcoder = NULL;
    
    if(win->transcoder_track)
      {
      gavl_dictionary_destroy(win->transcoder_track);
      win->transcoder_track = NULL;
      }
    }
  /* Flush message queue */
  bg_msg_sink_iteration(win->msg_sink);
  }

static int handle_msg(void * data, gavl_msg_t * msg)
  {
  float percentage_done;
  gavl_time_t remaining_time;
  const char * arg_str;
  transcoder_window_t * win = data;

  switch(msg->NS)
    {
    case BG_MSG_NS_PARAMETER:
      switch(msg->ID)
        {
        case BG_CMD_SET_PARAMETER:
          {
          const char * name;
          const char * ctx;
          gavl_value_t val;

          //       const char * subsection;
          
          gavl_value_init(&val);
          bg_msg_get_parameter(msg, &name, &val);
          ctx = gavl_dictionary_get_string(&msg->header, GAVL_MSG_CONTEXT_ID);

          //          subsection = gavl_dictionary_get_string(&msg->header, BG_MSG_PARAMETER_SUBSECTION);

          if(name && 
             (!strcmp(ctx, BG_TRANSCODER_TRACK_DEFAULT_GENERAL) ||
              !strcmp(ctx, BG_TRANSCODER_TRACK_DEFAULT_AUDIO) ||
              !strcmp(ctx, BG_TRANSCODER_TRACK_DEFAULT_VIDEO) ||
              !strcmp(ctx, BG_TRANSCODER_TRACK_DEFAULT_TEXT) ||
              !strcmp(ctx, BG_TRANSCODER_TRACK_DEFAULT_TEXTRENDERER) || 
              !strcmp(ctx, BG_TRANSCODER_TRACK_DEFAULT_OVERLAY)))
            {
            gavl_dictionary_t * cfg_section =
              bg_cfg_section_find_subsection(win->track_defaults_section, ctx);
            
            gavl_dictionary_set_nocopy(cfg_section, name, &val);
            }
          else if(!strcmp(ctx, "transcoder_window"))
            {
            gavl_dictionary_t * cfg_section =
              bg_cfg_registry_find_section(bg_cfg_registry, ctx);
            
            set_transcoder_window_parameter(win, name, &val);

            if(name)
              gavl_dictionary_set_nocopy(cfg_section, name, &val);
            }
          else if(!strcmp(ctx, "logwindow"))
            {
            gavl_dictionary_t * cfg_section =
              bg_cfg_registry_find_section(bg_cfg_registry, ctx);

            bg_gtk_log_window_set_parameter(win->logwindow, name, &val);
            
            if(name)
              gavl_dictionary_set_nocopy(cfg_section, name, &val);
            }
          else if(!strcmp(ctx, "encoders"))
            {
            if(name)
              {
              fprintf(stderr, "Encoder settings: %s\n", name);
              gavl_value_dump(&val, 2);
              fprintf(stderr, "\n");
              gavl_dictionary_set_nocopy(win->encoder_section, name, &val);
              }
            }
          else if(!strcmp(ctx, "output"))
            {
            if(name)
              {
              gavl_dictionary_t * cfg_section = bg_cfg_registry_find_section(bg_cfg_registry, "output");
              gavl_dictionary_set_nocopy(cfg_section, name, &val);
              }
            }
          }
        }
      break;
    case GAVL_MSG_NS_GUI:
      switch(msg->ID)
        {
        case BG_MSG_DIALOG_FILE_LOAD:
          {
          const char * ctx = gavl_dictionary_get_string(&msg->header, GAVL_MSG_CONTEXT_ID);
          const char * uri = gavl_msg_get_arg_string_c(msg, 0);
          if(!ctx || !uri)
            return 1;
          
          if(!strcmp(ctx, CTX_PROFILE))
            {
            transcoder_window_load_profile(win, uri);
            SET_CFG_STRING_NOCOPY("profile_path", gavl_filename_get_dir(uri));
            }
          else if(!strcmp(ctx, CTX_TRACKLIST))
            {
            track_list_load(win->tracklist, uri);
            SET_CFG_STRING_NOCOPY("task_path", gavl_filename_get_dir(uri));
            }
          
          }
          break;
        case BG_MSG_DIALOG_FILE_SAVE:
          {
          const char * ctx = gavl_dictionary_get_string(&msg->header, GAVL_MSG_CONTEXT_ID);
          const char * uri = gavl_msg_get_arg_string_c(msg, 0);
          if(!ctx || !uri)
            return 1;

          if(!strcmp(ctx, CTX_PROFILE))
            {
            transcoder_window_save_profile(win, uri);
            SET_CFG_STRING_NOCOPY("profile_path", gavl_filename_get_dir(uri));
            }
          else if(!strcmp(ctx, CTX_TRACKLIST))
            {
            track_list_save(win->tracklist, uri);
            SET_CFG_STRING_NOCOPY("task_path", gavl_filename_get_dir(uri));
            }
          
          }
          break;
        }
      break;
    
    case BG_MSG_NS_TRANSCODER:
      
      switch(msg->ID)
        {
        case BG_TRANSCODER_MSG_START:
          arg_str = gavl_msg_get_arg_string_c(msg, 0);
          set_status_message(win, arg_str);
          break;
        case BG_TRANSCODER_MSG_NUM_AUDIO_STREAMS:
          break;
        case BG_TRANSCODER_MSG_AUDIO_FORMAT:
          break;
        case BG_TRANSCODER_MSG_NUM_VIDEO_STREAMS:
          break;
        case BG_TRANSCODER_MSG_VIDEO_FORMAT:
          break;
        case BG_TRANSCODER_MSG_PROGRESS:
          {
          char * status_msg;
          char time_str[GAVL_TIME_STRING_LEN];
          
          percentage_done = gavl_msg_get_arg_float(msg, 0);
          remaining_time = gavl_msg_get_arg_long(msg, 1);

          gavl_time_prettyprint(remaining_time, time_str);

          status_msg = gavl_sprintf("Remaining time: %s", time_str);
          
          gtk_progress_bar_set_text(GTK_PROGRESS_BAR(win->progress_bar), status_msg);
          free(status_msg);
          
          gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(win->progress_bar),
                                        percentage_done);
          }
          break;
        case BG_TRANSCODER_MSG_FINISHED:

          finish_transcoding(win);

          gtk_progress_bar_set_text(GTK_PROGRESS_BAR(win->progress_bar), "");
          
          gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(win->progress_bar), 0.0);
        
        
          if(!start_transcode(win))
            {
            gtk_widget_set_sensitive(win->run_button, 1);
            gtk_widget_set_sensitive(win->actions_menu.run_item, 1);
          
            gtk_widget_set_sensitive(win->stop_button, 0);
            gtk_widget_set_sensitive(win->actions_menu.stop_item, 0);
            return TRUE;
            }
          else
            {
            return TRUE;
            }
          break;

        case BG_TRANSCODER_MSG_ERROR:
          arg_str = gavl_msg_get_arg_string_c(msg, 0);

          set_status_message(win, bg_gtk_log_window_last_error(win->logwindow));
          
          if(win->transcoder_track)
            {
            track_list_prepend_track(win->tracklist, win->transcoder_track);
            win->transcoder_track = NULL;
            }

          finish_transcoding(win);
        
          gtk_progress_bar_set_text(GTK_PROGRESS_BAR(win->progress_bar), "");
          gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(win->progress_bar), 0.0);
        
          win->transcoder = NULL;
        
          gtk_widget_set_sensitive(win->run_button, 1);
          gtk_widget_set_sensitive(win->actions_menu.run_item, 1);
        
          gtk_widget_set_sensitive(win->stop_button, 0);
          gtk_widget_set_sensitive(win->actions_menu.stop_item, 0);
          return TRUE;
        }
      
      break;
    }
  return 1;
  }


static gboolean idle_callback(gpointer data)
  {
  transcoder_window_t * win = data;

  /* If the transcoder isn't there, it means that we were interrupted */

  bg_msg_sink_iteration(win->msg_sink);
  
  
  return TRUE;
  }

static int start_transcode(transcoder_window_t * win)
  {
  gavl_dictionary_t * cfg_section;

  
  cfg_section = bg_cfg_registry_find_section(bg_cfg_registry, "output");

  win->transcoder_track      = track_list_get_track(win->tracklist);
  if(!win->transcoder_track)
    {
    clear_status_message(win);
    return 0;
    }
  win->transcoder            = bg_transcoder_create();
  bg_cfg_section_apply(cfg_section, bg_transcoder_get_parameters(),
                       bg_transcoder_set_parameter, win->transcoder);
  
  bg_transcoder_add_message_sink(win->transcoder,
                                 win->msg_sink);
  

  if(!bg_transcoder_init(win->transcoder,
                         win->transcoder_track))
    {
    bg_gtk_log_window_flush(win->logwindow);
    
    set_status_message(win, bg_gtk_log_window_last_error(win->logwindow));
        
    
    if(win->transcoder_track)
      track_list_prepend_track(win->tracklist, win->transcoder_track);
    win->transcoder_track = NULL;

    bg_transcoder_destroy(win->transcoder);
    win->transcoder = NULL;
    return 0;
    }
  
  gtk_widget_set_sensitive(win->run_button, 0);
  gtk_widget_set_sensitive(win->actions_menu.run_item, 0);

  gtk_widget_set_sensitive(win->stop_button, 1);
  gtk_widget_set_sensitive(win->actions_menu.stop_item, 1);

  bg_transcoder_run(win->transcoder);
  
  return 1;
  }

void transcoder_window_load_profile(transcoder_window_t * win,
                                    const char * file)
  {
  gavl_dictionary_t * s;
  bg_cfg_registry_load(bg_cfg_registry, file);
  s = bg_cfg_section_create_from_parameters("encoders", win->encoder_parameters);
  bg_cfg_section_transfer(s, win->encoder_section);
  bg_cfg_section_destroy(s);
  }

static void transcoder_window_save_profile(transcoder_window_t * win,
                                           const char * file)
  {
  bg_cfg_registry_save_to(bg_cfg_registry, file);
  }

static void button_callback(GtkWidget * w, gpointer data)
  {
  transcoder_window_t * win = data;
  
  if((w == win->run_button) || (w == win->actions_menu.run_item))
    {
    start_transcode(win);
    }
  else if((w == win->stop_button) || (w == win->actions_menu.stop_item))
    {
    if(win->transcoder_track)
      track_list_prepend_track(win->tracklist, win->transcoder_track);
    win->transcoder_track = NULL;
    if(win->transcoder)
      bg_transcoder_stop(win->transcoder);
    
    finish_transcoding(win);
    gtk_widget_set_sensitive(win->run_button, 1);
    gtk_widget_set_sensitive(win->actions_menu.run_item, 1);

    gtk_widget_set_sensitive(win->stop_button, 0);
    gtk_widget_set_sensitive(win->actions_menu.stop_item, 0);

    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(win->progress_bar), "");

    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(win->progress_bar), 0.0);

    clear_status_message(win);
    }
  else if((w == win->load_button) || (w == win->file_menu.load_item))
    {
    bg_gtk_get_filename_read(TR("Load task list"), CTX_TRACKLIST,
                             GET_CFG_STRING("task_path"), 
                             win->load_button, win->msg_sink);
    }
  else if((w == win->save_button) || (w == win->file_menu.save_item))
    {
    bg_gtk_get_filename_write(TR("Save task list"), CTX_TRACKLIST,
                              GET_CFG_STRING("task_path"), 1,
                              win->save_button, win->msg_sink, NULL);
    }
  else if(w == win->options_menu.load_item)
    {
    bg_gtk_get_filename_read(TR("Load profile"), CTX_PROFILE,
                             GET_CFG_STRING("profile_path"), win->win, win->msg_sink);
    }
  else if(w == win->options_menu.save_item)
    {
    bg_gtk_get_filename_write(TR("Save profile"), CTX_PROFILE,
                              GET_CFG_STRING("profile_path"), 1,
                              win->win, win->msg_sink, NULL);
    }
  else if((w == win->quit_button) || (w == win->file_menu.quit_item))
    {
    gtk_widget_hide(win->win);
    bg_gtk_quit();
    }
  else if((w == win->properties_button) || (w == win->options_menu.config_item))
    {
    transcoder_window_preferences(win);
    }
  else if(w == win->windows_menu.log_item)
    {
    if(gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(w)))
      {
      gtk_widget_show(bg_gtk_log_window_get_widget(win->logwindow));

      gavl_dictionary_set_int(bg_cfg_registry_find_section(bg_cfg_registry, "transcoder_window"),
                              "show_logwindow", 1);
      }
    else
      {
      gtk_widget_hide(bg_gtk_log_window_get_widget(win->logwindow));
      gavl_dictionary_set_int(bg_cfg_registry_find_section(bg_cfg_registry, "transcoder_window"),
                              "show_logwindow", 0);
      }
    }

  else if(w == win->help_menu.about_item)
    {
    if(!win->about_window)
      win->about_window = bg_gtk_about_window_create();

    gtk_widget_show(win->about_window);
    }
  }



static GtkWidget * create_icon_button(transcoder_window_t * win,
                                      const char * icon,
                                      const char * tooltip)
  {
  GtkWidget * ret = bg_gtk_create_icon_button(icon);
  
  g_signal_connect(G_OBJECT(ret), "clicked", G_CALLBACK(button_callback),
                   win);
  gtk_widget_show(ret);

  bg_gtk_tooltips_set_tip(ret, tooltip, PACKAGE);

  return ret;
  }

static gboolean delete_callback(GtkWidget * w, GdkEvent * evt,
                                gpointer data)
  {
  transcoder_window_t * win = data;
  gtk_widget_hide(win->win);
  bg_gtk_quit();
  return TRUE;
  }


static GtkWidget *
create_icon_item(transcoder_window_t * w, GtkWidget * parent,
                 const char * label, const char * icon)
  {
  GtkWidget * ret;
  ret = bg_gtk_icon_menu_item_new(label, icon);
  
  g_signal_connect(G_OBJECT(ret), "activate", G_CALLBACK(button_callback),
                   (gpointer)w);
  gtk_widget_show(ret);
  gtk_menu_shell_append(GTK_MENU_SHELL(parent), ret);
  return ret;
  }


static GtkWidget *
create_toggle_item(transcoder_window_t * w, GtkWidget * parent,
                   const char * label)
  {
  GtkWidget * ret;
  ret = gtk_check_menu_item_new_with_label(label);
  
  g_signal_connect(G_OBJECT(ret), "toggled", G_CALLBACK(button_callback),
                   (gpointer)w);
  gtk_widget_show(ret);
  gtk_menu_shell_append(GTK_MENU_SHELL(parent), ret);
  return ret;
  }

#if 0
  struct
    {
    GtkWidget * load_item;
    GtkWidget * save_item;
    GtkWidget * quit_item;
    GtkWidget * menu;
    } file_menu;

  struct
    {
    GtkWidget * config_item;
    GtkWidget * load_item;
    GtkWidget * save_item;
    } options_menu;
#endif

static void init_menus(transcoder_window_t * w)
  {
  w->file_menu.menu = gtk_menu_new();
  w->file_menu.load_item = create_icon_item(w, w->file_menu.menu, TR("Load tasklist..."), BG_ICON_FOLDER_OPEN);
  w->file_menu.save_item = create_icon_item(w, w->file_menu.menu, TR("Save tasklist..."), BG_ICON_SAVE);
  w->file_menu.quit_item = create_icon_item(w, w->file_menu.menu, TR("Quit"), BG_ICON_LEAVE);
  gtk_widget_show(w->file_menu.menu);

  w->options_menu.menu = gtk_menu_new();
  w->options_menu.config_item = create_icon_item(w, w->options_menu.menu, TR("Preferences..."), BG_ICON_CONFIG);
  w->options_menu.load_item = create_icon_item(w, w->options_menu.menu, TR("Load profile..."), BG_ICON_FOLDER_OPEN);
  w->options_menu.save_item = create_icon_item(w, w->options_menu.menu, TR("Save profile..."), BG_ICON_SAVE);
  gtk_widget_show(w->options_menu.menu);

  w->actions_menu.menu = gtk_menu_new();
  w->actions_menu.run_item = create_icon_item(w, w->actions_menu.menu, TR("Start transcoding"), BG_ICON_RUN);
  w->actions_menu.stop_item = create_icon_item(w, w->actions_menu.menu, TR("Stop transcoding"), BG_ICON_X);
  gtk_widget_set_sensitive(w->actions_menu.stop_item, 0);

  w->windows_menu.menu = gtk_menu_new();
  w->windows_menu.log_item = create_toggle_item(w, w->windows_menu.menu, TR("Log messages"));
  gtk_widget_show(w->windows_menu.menu);

  w->help_menu.menu = gtk_menu_new();
  w->help_menu.about_item = create_icon_item(w, w->help_menu.menu, TR("About..."), BG_ICON_INFO);
  //  w->help_menu.help_item = create_item(w, w->help_menu.menu, TR("Userguide"), "help_16.png");
  gtk_widget_show(w->help_menu.menu);
  
  }

static void logwindow_close_callback(GtkWidget * w, void*data)
  {
  transcoder_window_t * win = data;
  gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(win->windows_menu.log_item), 0);

  gavl_dictionary_set_int(bg_cfg_registry_find_section(bg_cfg_registry, "transcoder_window"),
                          "show_logwindow", 0);
  }

transcoder_window_t * transcoder_window_create()
  {
  GtkWidget * menuitem;
  
  GtkWidget * main_table;
  GtkWidget * frame;
  GtkWidget * box;
  transcoder_window_t * ret;
  gavl_dictionary_t * cfg_section;

  gavl_dictionary_t * enc_section;
  
  ret = calloc(1, sizeof(*ret));

  ret->msg_sink = bg_msg_sink_create(handle_msg, ret, 0);

  g_timeout_add(200, idle_callback, ret);

  /* Create window */
  
  ret->win = bg_gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_position(GTK_WINDOW(ret->win), GTK_WIN_POS_CENTER);
  gtk_window_set_title(GTK_WINDOW(ret->win),
                       "Gmerlin transcoder "VERSION);
  g_signal_connect(G_OBJECT(ret->win), DELETE_EVENT,
                   G_CALLBACK(delete_callback),
                   ret);
  

  /* Create encoding parameters */

  ret->encoder_parameters =
    bg_plugin_registry_create_encoder_parameters(bg_plugin_reg,
                                                 stream_flags, plugin_flags, 1);

  gavl_parameter_info_append_static(&ret->encoder_params,
                                    ret->encoder_parameters);
  
  //  bg_parameters_dump(ret->encoder_parameters, "encoder_params.xml");
  
  ret->encoder_section = bg_cfg_registry_find_section(bg_cfg_registry, "encoders");

  //  bg_cfg_section_dump(ret->encoder_section, "encoders_1.xml");
    
  enc_section = bg_cfg_section_create(NULL);

  //  bg_cfg_section_create_items(enc_section, ret->encoder_parameters);
  bg_cfg_section_set_from_params(enc_section, &ret->encoder_params);
  
  fprintf(stderr, "enc_section:\n");
  gavl_dictionary_dump(enc_section, 2);
  fprintf(stderr, "\n");
    
  //  bg_cfg_section_dump(enc_section, "encoders_2.xml");
  
  gavl_dictionary_merge2(ret->encoder_section, enc_section);
  
  fprintf(stderr, "\n");
  
  
  bg_cfg_section_destroy(enc_section);
  
  //  bg_cfg_section_dump(ret->encoder_section, "encoders_3.xml");
  
  /* Create track list */

  ret->track_defaults_section = bg_cfg_registry_find_section(bg_cfg_registry, "track_defaults");
  ret->tracklist = track_list_create(ret->track_defaults_section,
                                     ret->encoder_parameters, ret->encoder_section);
  
  gtk_window_add_accel_group(GTK_WINDOW(ret->win), track_list_get_accel_group(ret->tracklist));
  

  /* Create log window */

  ret->logwindow = bg_gtk_log_window_create(TR("Gmerlin transcoder"));

  g_signal_connect(G_OBJECT(bg_gtk_log_window_get_widget(ret->logwindow)),
                   "hide", G_CALLBACK(logwindow_close_callback),
                   (gpointer)ret);
  
  cfg_section = bg_cfg_registry_find_section(bg_cfg_registry, "logwindow");
  bg_cfg_section_apply(cfg_section, bg_gtk_log_window_get_parameters(ret->logwindow),
                       bg_gtk_log_window_set_parameter, ret->logwindow);
  
  /* Create buttons */

  ret->run_button  = create_icon_button(ret, BG_ICON_RUN,
                                          TRS("Start transcoding"));

  ret->stop_button = create_icon_button(ret, BG_ICON_X,
                                          TRS("Stop transcoding"));
  
  ret->properties_button = create_icon_button(ret,
                                               BG_ICON_CONFIG, TRS("Set global options and track defaults"));
  ret->quit_button = create_icon_button(ret, BG_ICON_LEAVE, TRS("Quit program"));
  
  ret->load_button  = create_icon_button(ret, BG_ICON_FOLDER_OPEN, TRS("Load task list"));
  ret->save_button  = create_icon_button(ret, BG_ICON_SAVE, TRS("Save task list"));

  gtk_widget_set_sensitive(ret->stop_button, 0);
  
  /* Progress bar */
  ret->progress_bar = gtk_progress_bar_new();
  gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(ret->progress_bar), TRUE);
  gtk_progress_bar_set_text(GTK_PROGRESS_BAR(ret->progress_bar), "");
    
  gtk_widget_show(ret->progress_bar);

  ret->statusbar = gtk_statusbar_new();
  ret->status_ctx_id = gtk_statusbar_get_context_id(GTK_STATUSBAR(ret->statusbar), "Messages");
  gtk_statusbar_push(GTK_STATUSBAR(ret->statusbar), ret->status_ctx_id, "Gmerlin transcoder version "VERSION);
  
  gtk_widget_show(ret->statusbar);

  
  /* Menubar */

  init_menus(ret);
  
  ret->menubar = gtk_menu_bar_new();

  menuitem = gtk_menu_item_new_with_label(TR("File"));
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(menuitem), ret->file_menu.menu);
  gtk_widget_show(menuitem);
  gtk_menu_shell_append(GTK_MENU_SHELL(ret->menubar), menuitem);

  menuitem = gtk_menu_item_new_with_label(TR("Options"));
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(menuitem), ret->options_menu.menu);
  gtk_widget_show(menuitem);
  gtk_menu_shell_append(GTK_MENU_SHELL(ret->menubar), menuitem);

  menuitem = gtk_menu_item_new_with_label(TR("Actions"));
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(menuitem), ret->actions_menu.menu);
  gtk_widget_show(menuitem);
  gtk_menu_shell_append(GTK_MENU_SHELL(ret->menubar), menuitem);

  menuitem = gtk_menu_item_new_with_label(TR("Tasklist"));
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(menuitem), track_list_get_menu(ret->tracklist));
  gtk_widget_show(menuitem);
  gtk_menu_shell_append(GTK_MENU_SHELL(ret->menubar), menuitem);

  menuitem = gtk_menu_item_new_with_label(TR("Windows"));
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(menuitem), ret->windows_menu.menu);
  gtk_widget_show(menuitem);
  gtk_menu_shell_append(GTK_MENU_SHELL(ret->menubar), menuitem);

  menuitem = gtk_menu_item_new_with_label(TR("Help"));
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(menuitem), ret->help_menu.menu);
  gtk_widget_show(menuitem);
  gtk_menu_shell_append(GTK_MENU_SHELL(ret->menubar), menuitem);
  
  gtk_widget_show(ret->menubar);
    
  /* Pack everything */
  
  main_table = gtk_grid_new();
  gtk_container_set_border_width(GTK_CONTAINER(main_table), 5);
  gtk_grid_set_row_spacing(GTK_GRID(main_table), 5);
  gtk_grid_set_column_spacing(GTK_GRID(main_table), 5);

  bg_gtk_table_attach(main_table,
                      ret->menubar,
                      0, 1, 0, 1, 0, 0);
  
  box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  
  gtk_box_pack_start(GTK_BOX(box), ret->load_button, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(box), ret->save_button, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(box), ret->run_button, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(box), ret->stop_button, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(box), ret->properties_button, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(box), ret->quit_button, FALSE, FALSE, 0);
  gtk_widget_show(box);
  bg_gtk_table_attach(main_table,
                   box,
                   0, 1, 1, 2, 0, 0);

  
  bg_gtk_table_attach(main_table,
                      ret->progress_bar,
                      0, 1, 2, 3, 0, 0);
  
  frame = gtk_frame_new(TR("Tasklist"));
  gtk_container_add(GTK_CONTAINER(frame),
                    track_list_get_widget(ret->tracklist));

  gtk_widget_show(frame);
  bg_gtk_table_attach_defaults(main_table,
                               frame,
                               0, 1, 3, 4);
  
  bg_gtk_table_attach(main_table,
                      ret->statusbar,
                      0, 1, 4, 5, 0, 0);
  
  
  gtk_widget_show(main_table);
  gtk_container_add(GTK_CONTAINER(ret->win), main_table);
  
  /* Apply config stuff */

  cfg_section = bg_cfg_registry_find_section(bg_cfg_registry, "transcoder_window");
  bg_cfg_section_apply(cfg_section, transcoder_window_parameters,
                       set_transcoder_window_parameter, ret);

  
  return ret;
  }

void transcoder_window_destroy(transcoder_window_t* w)
  {
  bg_parameter_info_destroy_array(w->encoder_parameters);
  
  track_list_destroy(w->tracklist);
  
  bg_cfg_registry_save();
  
  /* Destroy after saving the logwindow state */
  bg_gtk_log_window_destroy(w->logwindow);
  
  
  bg_msg_sink_destroy(w->msg_sink);

  free(w);
  }

static gboolean remote_callback(gpointer data)
  {
  /* TODO: Flush remote commands */
  return TRUE;
  }

void transcoder_window_run(transcoder_window_t * w)
  {
  int show_logwindow = 0;
  gtk_widget_show(w->win);
  
  gavl_dictionary_get_int(bg_cfg_registry_find_section(bg_cfg_registry, "transcoder_window"),
                          "show_logwindow", &show_logwindow);
  
  if(show_logwindow)
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(w->windows_menu.log_item), 1);

  remote_callback(w);
  
  g_timeout_add(50, remote_callback, w);
  
  gtk_main();
  }

#if 0
static const bg_parameter_info_t input_plugin_parameters[] =
  {
    {
      .name = "input_plugins",
      .long_name = "Input plugins",
      .flags = BG_PARAMETER_PLUGIN,
    },
    { /* */ },
  };

static const bg_parameter_info_t image_reader_parameters[] =
  {
    {
      .name = "image_readers",
      .long_name = "Image readers",
      .flags = BG_PARAMETER_PLUGIN,
    },
    { /* */ },
  };
#endif

/* Configuration stuff */


static void transcoder_window_preferences(transcoder_window_t * w)
  {
  GtkWidget * dlg;
  GtkTreeIter it;
  bg_cfg_ctx_t ctx;
  
  dlg = bg_gtk_config_dialog_create_multi(BG_GTK_CONFIG_DIALOG_DESTROY,
                                          TR("Transcoder configuration"),
                                          w->win);

  /*
   *  Build dialog 
   *
   */

  bg_cfg_ctx_init(&ctx, NULL,
                  "output",
                  TR("Output options"),
                  NULL, NULL);
  ctx.s = bg_cfg_registry_find_section(bg_cfg_registry, "output");
  ctx.parameters = bg_transcoder_get_parameters();
  ctx.sink = w->msg_sink;
  bg_gtk_config_dialog_add_section(dlg, &ctx,
                                   NULL);
  bg_cfg_ctx_free(&ctx);

  bg_cfg_ctx_init(&ctx, NULL,
                  BG_TRANSCODER_TRACK_DEFAULT_GENERAL,
                  TR("Track defaults"),
                  NULL, NULL);
  ctx.s = bg_cfg_section_find_subsection(w->track_defaults_section,
                                         ctx.name);
  ctx.parameters = bg_transcoder_track_get_general_parameters();
  ctx.sink = w->msg_sink;
  bg_gtk_config_dialog_add_section(dlg, &ctx, NULL);
  bg_cfg_ctx_free(&ctx);
  
  bg_gtk_config_dialog_add_container(dlg, TR("Audio defaults"),
                                     NULL, &it);

  bg_cfg_ctx_init(&ctx, NULL,
                  BG_TRANSCODER_TRACK_DEFAULT_AUDIO,
                  TR("General"),
                  NULL, NULL);
  ctx.parameters = bg_transcoder_track_audio_get_general_parameters();
  ctx.sink = w->msg_sink;
  ctx.s = bg_cfg_section_find_subsection(w->track_defaults_section,
                                         ctx.name);

  bg_gtk_config_dialog_add_section(dlg, &ctx, &it);
  bg_cfg_ctx_free(&ctx);

  bg_gtk_config_dialog_add_section(dlg, bg_plugin_config_get_ctx(BG_PLUGIN_FILTER_AUDIO), &it);
  
  //  bg_gtk_config_dialog_add_section(dlg, &ctx, &it);

  bg_gtk_config_dialog_add_container(dlg, TR("Video defaults"),
                                     NULL, &it);

  bg_cfg_ctx_init(&ctx, NULL,
                  BG_TRANSCODER_TRACK_DEFAULT_VIDEO,
                  TR("General"),
                  NULL, NULL);
  ctx.parameters = bg_transcoder_track_video_get_general_parameters();
  ctx.sink = w->msg_sink;
  ctx.s = bg_cfg_section_find_subsection(w->track_defaults_section,
                                         ctx.name);

  bg_gtk_config_dialog_add_section(dlg, &ctx, &it);
  bg_cfg_ctx_free(&ctx);
  
  bg_gtk_config_dialog_add_section(dlg, bg_plugin_config_get_ctx(BG_PLUGIN_FILTER_VIDEO), &it);

  bg_gtk_config_dialog_add_container(dlg, TR("Text subtitle defaults"),
                                     NULL, &it);
  bg_cfg_ctx_init(&ctx, NULL,
                  BG_TRANSCODER_TRACK_DEFAULT_TEXT,
                  TR("General"),
                  NULL, NULL);
  ctx.parameters = bg_transcoder_track_text_get_general_parameters();
  ctx.sink = w->msg_sink;
  ctx.s = bg_cfg_section_find_subsection(w->track_defaults_section,
                                         ctx.name);

  bg_gtk_config_dialog_add_section(dlg, &ctx, &it);
  bg_cfg_ctx_free(&ctx);

  bg_cfg_ctx_init(&ctx, NULL,
                  BG_TRANSCODER_TRACK_DEFAULT_TEXTRENDERER,
                  TR("Textrenderer"),
                  NULL, NULL);
  ctx.parameters = bg_text_renderer_get_parameters();
  ctx.sink = w->msg_sink;
  ctx.s = bg_cfg_section_find_subsection(w->track_defaults_section,
                                         ctx.name);

  bg_gtk_config_dialog_add_section(dlg, &ctx, &it);
  bg_cfg_ctx_free(&ctx);

  /* overlay */
  
  bg_cfg_ctx_init(&ctx, NULL,
                  BG_TRANSCODER_TRACK_DEFAULT_OVERLAY,
                  TR("Overlay subtitle defaults"),
                  NULL, NULL);
  ctx.parameters = bg_transcoder_track_overlay_get_general_parameters();
  ctx.s = bg_cfg_section_find_subsection(w->track_defaults_section,
                                         ctx.name);
  ctx.sink = w->msg_sink;

  bg_gtk_config_dialog_add_section(dlg, &ctx, NULL);
  bg_cfg_ctx_free(&ctx);

  /* encoders */

  bg_cfg_ctx_init(&ctx, NULL,
                  "encoders",
                  TR("Encoders"),
                  NULL, NULL);
  ctx.parameters = w->encoder_parameters;
  ctx.s = w->encoder_section;
  ctx.sink = w->msg_sink;
  
  bg_gtk_config_dialog_add_section(dlg, &ctx, NULL);
  bg_cfg_ctx_free(&ctx);

  /* Input plugins */

  bg_gtk_config_dialog_add_section(dlg, bg_plugin_config_get_ctx(BG_PLUGIN_INPUT), NULL);
  bg_gtk_config_dialog_add_section(dlg, bg_plugin_config_get_ctx(BG_PLUGIN_IMAGE_READER), NULL);
  
  bg_cfg_ctx_init(&ctx, NULL,
                  "transcoder_window",
                  TR("Window"),
                  NULL, NULL);
  ctx.parameters = transcoder_window_parameters;
  ctx.s = bg_cfg_section_find_subsection(bg_cfg_registry,
                                         ctx.name);
  ctx.sink = w->msg_sink;
  
  bg_gtk_config_dialog_add_section(dlg, &ctx, NULL);
  bg_cfg_ctx_free(&ctx);

  bg_cfg_ctx_init(&ctx, NULL,
                  "logwindow",
                  TR("Log window"),
                  NULL, NULL);
  ctx.s = bg_cfg_section_find_subsection(bg_cfg_registry,
                                         ctx.name);
  ctx.sink = w->msg_sink;
  
  ctx.parameters = bg_gtk_log_window_get_parameters(w->logwindow);
  bg_gtk_config_dialog_add_section(dlg, &ctx, NULL);
  bg_cfg_ctx_free(&ctx);
  
  /*
   *  End build dialog
   */
  
  gtk_window_present(GTK_WINDOW(dlg));
  
  }
