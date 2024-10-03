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

#include <gmerlin/cfg_dialog.h>
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

#include "transcoder_window.h"

#include "tracklist.h"

static const uint32_t stream_flags = GAVL_STREAM_AUDIO |
GAVL_STREAM_VIDEO |
GAVL_STREAM_TEXT |
GAVL_STREAM_OVERLAY;

static const uint32_t plugin_flags = BG_PLUGIN_FILE;

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
  int show_logwindow;
  
  GtkWidget * progress_bar;
  //  bg_gtk_time_display_t * time_remaining;
  //  bg_gtk_scrolltext_t   * scrolltext;
  
  /* Configuration stuff */

  char * output_directory;
  int delete_incomplete;

  bg_cfg_section_t * track_defaults_section;

  bg_parameter_info_t * encoder_parameters;
  bg_cfg_section_t * encoder_section;
    
  /* The actual transcoder */

  bg_transcoder_t * transcoder;
  bg_transcoder_track_t * transcoder_track;
  
  /* Load/Save stuff */
  
  char * task_path;
  char * profile_path;
  
  GtkWidget * filesel;
  char * filesel_file;
  char * filesel_path;
  
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
  transcoder_window_t * win = data;

  if(!name)
    return;
  
  if(!strcmp(name, "task_path"))
    {
    win->task_path = gavl_strrep(win->task_path, val->v.str);
    }
  else if(!strcmp(name, "profile_path"))
    {
    win->profile_path = gavl_strrep(win->profile_path, val->v.str);
    }
  else if(!strcmp(name, "show_logwindow"))
    {
    win->show_logwindow = val->v.i;
    }
  else if(!strcmp(name, "show_tooltips"))
    {
    bg_gtk_set_tooltips(val->v.i);
    }
  }

static int
get_transcoder_window_parameter(void * data, const char * name,
                                gavl_value_t * val)
  {
  transcoder_window_t * win = data;

  if(!name)
    return 1;

  if(!strcmp(name, "task_path"))
    {
    val->v.str = gavl_strrep(val->v.str, win->task_path);
    return 1;
    }
  else if(!strcmp(name, "profile_path"))
    {
    val->v.str = gavl_strrep(val->v.str, win->profile_path);
    return 1;
    }
  else if(!strcmp(name, "show_logwindow"))
    {
    val->v.i = win->show_logwindow;
    return 1;
    }
  return 0;
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

          status_msg = bg_sprintf("Remaining time: %s", time_str);
          
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
  bg_cfg_section_t * cfg_section;

  
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
  bg_cfg_section_t * s;
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
  char * tmp_string;
  
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
    tmp_string = bg_gtk_get_filename_read(TR("Load task list"),
                                          &win->task_path, win->load_button);
    if(tmp_string)
      {
      track_list_load(win->tracklist, tmp_string);
      free(tmp_string);
      }
    }
  else if((w == win->save_button) || (w == win->file_menu.save_item))
    {
    tmp_string = bg_gtk_get_filename_write(TR("Save task list"),
                                           &win->task_path, 1,
                                           win->save_button, NULL);
    if(tmp_string)
      {
      track_list_save(win->tracklist, tmp_string);
      free(tmp_string);
      }
    }
  else if(w == win->options_menu.load_item)
    {
    tmp_string = bg_gtk_get_filename_read(TR("Load profile"),
                                          &win->profile_path, win->win);
    if(tmp_string)
      {
      transcoder_window_load_profile(win, tmp_string);
      free(tmp_string);
      }
    }
  else if(w == win->options_menu.save_item)
    {
    tmp_string = bg_gtk_get_filename_write(TR("Save profile"),
                                           &win->profile_path, 1,
                                           win->win, NULL);
    if(tmp_string)
      {
      transcoder_window_save_profile(win, tmp_string);
      free(tmp_string);
      }
    }
  else if((w == win->quit_button) || (w == win->file_menu.quit_item))
    {
    gtk_widget_hide(win->win);
    gtk_main_quit();
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
      win->show_logwindow = 1;
      }
    else
      {
      gtk_widget_hide(bg_gtk_log_window_get_widget(win->logwindow));
      win->show_logwindow = 0;
      }
    }

  else if(w == win->help_menu.about_item)
    {
    if(!win->about_window)
      win->about_window = bg_gtk_about_window_create();

    gtk_widget_show(win->about_window);
    }
#if 0
  else if(w == win->help_menu.help_item)
    {
    bg_display_html_help("userguide/GUI-Transcoder.html");
    }
#endif
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
  gtk_main_quit();
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

  fprintf(stderr, "Logwindow close\n");
  
  gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(win->windows_menu.log_item), 0);
  win->show_logwindow = 0;
  }

static void remove_missing_encoders_sub(gavl_dictionary_t * dst, const gavl_dictionary_t * src, const char * name)
  {
  int i, num;
  const char * str;
  const gavl_value_t * src_val;
  gavl_value_t * dst_val;
  
  if(!(dst_val = gavl_dictionary_get_nc(dst, name)) ||
     !(src_val = gavl_dictionary_get(src, name)))
    return;

  num = bg_multi_menu_get_num(dst_val);
  i = 0;
  while(i < num)
    {
    if((str = bg_multi_menu_get_name(dst_val, i)) &&
       !bg_multi_menu_has_name(src_val, str))
      {
      gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Removing entry %s", str);
      bg_multi_menu_remove(dst_val, i);
      num--;
      }
    else
      i++;
    }
  
  }
  
static void remove_missing_encoders(gavl_dictionary_t * dst, const gavl_dictionary_t * src)
  {
  remove_missing_encoders_sub(dst, src, "ae");
  remove_missing_encoders_sub(dst, src, "te");
  remove_missing_encoders_sub(dst, src, "oe");
  remove_missing_encoders_sub(dst, src, "ve");
  }

transcoder_window_t * transcoder_window_create()
  {
  GtkWidget * menuitem;
  
  GtkWidget * main_table;
  GtkWidget * frame;
  GtkWidget * box;
  transcoder_window_t * ret;
  bg_cfg_section_t * cfg_section;

  gavl_dictionary_t * enc_section;
  
  ret = calloc(1, sizeof(*ret));

  ret->msg_sink = bg_msg_sink_create(handle_msg, ret, 0);

  g_timeout_add(200, idle_callback, ret);

  /* Create window */
  
  ret->win = bg_gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_position(GTK_WINDOW(ret->win), GTK_WIN_POS_CENTER);
  gtk_window_set_title(GTK_WINDOW(ret->win),
                       "Gmerlin transcoder "VERSION);
  g_signal_connect(G_OBJECT(ret->win), "delete_event",
                   G_CALLBACK(delete_callback),
                   ret);
  

  /* Create encoding parameters */

  ret->encoder_parameters =
    bg_plugin_registry_create_encoder_parameters(bg_plugin_reg,
                                                 stream_flags, plugin_flags, 1);

  // bg_parameters_dump(ret->encoder_parameters, "encoder_params.xml");
  
  ret->encoder_section = bg_cfg_registry_find_section(bg_cfg_registry, "encoders");

  //  bg_cfg_section_dump(ret->encoder_section, "encoders_1.xml");
    
  enc_section = bg_cfg_section_create(NULL);
  bg_cfg_section_create_items(enc_section, ret->encoder_parameters);

  //  bg_cfg_section_dump(enc_section, "encoders_2.xml");
  
  gavl_dictionary_merge2(ret->encoder_section, enc_section);

  /* remove unsupported */
  remove_missing_encoders(ret->encoder_section, enc_section);
  

  bg_cfg_section_destroy(enc_section);
  
  //  bg_cfg_section_dump(ret->encoder_section, "encoders_3.xml");
  
  /* Create track list */

  ret->track_defaults_section = bg_cfg_registry_find_section(bg_cfg_registry, "track_defaults");
  ret->tracklist = track_list_create(ret->track_defaults_section,
                                     ret->encoder_parameters, ret->encoder_section);
  
  gtk_window_add_accel_group(GTK_WINDOW(ret->win), track_list_get_accel_group(ret->tracklist));
  
  cfg_section = bg_cfg_registry_find_section(bg_cfg_registry, "track_list");
  bg_cfg_section_apply(cfg_section, track_list_get_parameters(ret->tracklist),
                       track_list_set_parameter, ret->tracklist);


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
  bg_cfg_section_t * cfg_section;
  
  bg_parameter_info_destroy_array(w->encoder_parameters);
  
  cfg_section = bg_cfg_registry_find_section(bg_cfg_registry, "transcoder_window");
  bg_cfg_section_get(cfg_section, transcoder_window_parameters,
                       get_transcoder_window_parameter, w);
  

  cfg_section = bg_cfg_registry_find_section(bg_cfg_registry, "track_list");
  bg_cfg_section_get(cfg_section, track_list_get_parameters(w->tracklist),
                     track_list_get_parameter, w->tracklist);

  track_list_destroy(w->tracklist);
  
  bg_gtk_log_window_destroy(w->logwindow);

  bg_cfg_registry_save();
  
  if(w->task_path)
    free(w->task_path);
  if(w->profile_path)
    free(w->profile_path);
  
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
  gtk_widget_show(w->win);

  if(w->show_logwindow)
    {
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(w->windows_menu.log_item), 1);
    }
  //    bg_gtk_log_window_show(w->logwindow);

  remote_callback(w);

  g_timeout_add(50, remote_callback, w);
  
  gtk_main();
  }

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

/* Configuration stuff */

static void transcoder_window_preferences(transcoder_window_t * w)
  {
  bg_dialog_t * dlg;
  bg_cfg_section_t * cfg_section;
  void * parent;

  bg_audio_filter_chain_t * ac;
  bg_video_filter_chain_t * vc;

  bg_gavl_audio_options_t ao;
  bg_gavl_video_options_t vo;

  bg_parameter_info_t * params_i;
  bg_parameter_info_t * params_ir;

  params_i = bg_parameter_info_copy_array(input_plugin_parameters);
  params_ir = bg_parameter_info_copy_array(image_reader_parameters);

  bg_plugin_registry_set_parameter_info_input(bg_plugin_reg,
                                              BG_PLUGIN_INPUT,
                                              0,
                                              params_i);
  bg_plugin_registry_set_parameter_info_input(bg_plugin_reg,
                                              BG_PLUGIN_IMAGE_READER,
                                              0,
                                              params_ir);
  
  memset(&ao, 0, sizeof(ao));
  memset(&vo, 0, sizeof(vo));

  bg_gavl_audio_options_init(&ao);
  bg_gavl_video_options_init(&vo);
  
  ac = bg_audio_filter_chain_create(&ao);
  vc = bg_video_filter_chain_create(&vo);
  
  dlg = bg_dialog_create_multi(TR("Transcoder configuration"));

  cfg_section     = bg_cfg_registry_find_section(bg_cfg_registry, "output");

  bg_dialog_add(dlg,
                TR("Output options"),
                cfg_section,
                NULL,
                NULL,
                bg_transcoder_get_parameters());

  cfg_section = bg_cfg_section_find_subsection(w->track_defaults_section,
                                               "general");
  
  bg_dialog_add(dlg,
                TR("Track defaults"),
                cfg_section,
                NULL,
                NULL,
                bg_transcoder_track_get_general_parameters());
  
  parent = bg_dialog_add_parent(dlg, NULL,
                                TR("Audio defaults"));

  cfg_section = bg_cfg_section_find_subsection(w->track_defaults_section,
                                               "audio");
  bg_dialog_add_child(dlg, parent,
                      TR("General"),
                      cfg_section,
                      NULL,
                      NULL,
                      bg_transcoder_track_audio_get_general_parameters());
  
  cfg_section = bg_cfg_section_find_subsection(w->track_defaults_section,
                                               "audiofilters");
  bg_dialog_add_child(dlg, parent,
                TR("Filters"),
                cfg_section,
                NULL,
                NULL,
                bg_audio_filter_chain_get_parameters(ac));

  
  
  cfg_section = bg_cfg_section_find_subsection(w->track_defaults_section,
                                               "video");

  parent = bg_dialog_add_parent(dlg, NULL,
                                TR("Video defaults"));

  
  bg_dialog_add_child(dlg, parent, TR("General"),
                      cfg_section,
                      NULL,
                      NULL,
                      bg_transcoder_track_video_get_general_parameters());

  cfg_section = bg_cfg_section_find_subsection(w->track_defaults_section,
                                               "videofilters");
  bg_dialog_add_child(dlg, parent,
                      TR("Filters"),
                      cfg_section,
                      NULL,
                      NULL,
                      bg_video_filter_chain_get_parameters(vc));
  
  parent = bg_dialog_add_parent(dlg, NULL,
                                TR("Text subtitle defaults"));
    
  cfg_section = bg_cfg_section_find_subsection(w->track_defaults_section,
                                               "text");

  bg_dialog_add_child(dlg, parent, TR("General"),
                      cfg_section,
                      NULL,
                      NULL,
                      bg_transcoder_track_text_get_general_parameters());

  cfg_section = bg_cfg_section_find_subsection(w->track_defaults_section,
                                               "textrenderer");

  bg_dialog_add_child(dlg, parent, TR("Textrenderer"),
                      cfg_section,
                      NULL,
                      NULL,
                      bg_text_renderer_get_parameters());
  
  cfg_section = bg_cfg_section_find_subsection(w->track_defaults_section,
                                               "overlay");

  bg_dialog_add(dlg,
                TR("Overlay subtitle defaults"),
                cfg_section,
                NULL,
                NULL,
                bg_transcoder_track_overlay_get_general_parameters());

  bg_dialog_add(dlg,
                TR("Encoders"),
                w->encoder_section,
                NULL,
                NULL,
                w->encoder_parameters);
  
  bg_dialog_add(dlg,
                TR("Input plugins"),
                NULL,
                bg_plugin_registry_set_parameter_input,
                bg_plugin_reg,
                params_i);

  bg_dialog_add(dlg,
                TR("Image readers"),
                NULL,
                bg_plugin_registry_set_parameter_input,
                bg_plugin_reg,
                params_ir);
  
  cfg_section = bg_cfg_registry_find_section(bg_cfg_registry,
                                             "transcoder_window");

  bg_dialog_add(dlg,
                TR("Window"),
                cfg_section,
                set_transcoder_window_parameter,
                w,
                transcoder_window_parameters);


  cfg_section = bg_cfg_registry_find_section(bg_cfg_registry,
                                             "logwindow");

  bg_dialog_add(dlg,
                TR("Log window"),
                cfg_section,
                bg_gtk_log_window_set_parameter,
                w->logwindow,
                bg_gtk_log_window_get_parameters(w->logwindow));

  
  bg_dialog_show(dlg, w->win);
  bg_dialog_destroy(dlg);

  bg_audio_filter_chain_destroy(ac);
  bg_video_filter_chain_destroy(vc);

  bg_gavl_audio_options_free(&ao);
  bg_gavl_video_options_free(&vo);
    
  }

