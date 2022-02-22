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
#include <string.h>

#include <gmerlin/translation.h>

#include "gmerlin.h"
#include <gmerlin/utils.h>
#include <gmerlin/subprocess.h>
#include <gmerlin/iconfont.h>

#include <gdk/gdkkeysyms.h>

#include <gui_gtk/aboutwindow.h>
#include <gui_gtk/plugin.h>
#include <gui_gtk/gtkutils.h>
#include <gui_gtk/backendmenu.h>


typedef struct stream_menu_s
  {
  GSList * group;
  GtkWidget ** stream_items;
  guint * ids;

  GtkWidget * off_item;
  guint off_id;

  int num_streams;
  int streams_alloc;
  
  GtkWidget * menu;
  
  int idx;
  } stream_menu_t;

typedef struct chapter_menu_s
  {
  int timescale;
  int num_chapters;
  int chapters_alloc;
  guint * ids;
  GSList * group;
  GtkWidget ** chapter_items;
  GtkWidget * menu;
  } chapter_menu_t;


struct windows_menu_s
  {
  GtkWidget * infowindow;
  guint       infowindow_id;
  GtkWidget * logwindow;
  guint       logwindow_id;

  GtkWidget * mdbwindow;
  guint       mdbwindow_id;
  
  GtkWidget * menu;
  };

struct help_menu_s
  {
  GtkWidget * about;
  //  GtkWidget * help;
  GtkWidget * menu;
  };

struct seek_menu_s
  {
  GtkWidget * seek_forward;
  GtkWidget * seek_backward;
  GtkWidget * seek_forward_fast;
  GtkWidget * seek_backward_fast;
  
  GtkWidget * seek_00;
  GtkWidget * seek_10;
  GtkWidget * seek_20;
  GtkWidget * seek_30;
  GtkWidget * seek_40;
  GtkWidget * seek_50;
  GtkWidget * seek_60;
  GtkWidget * seek_70;
  GtkWidget * seek_80;
  GtkWidget * seek_90;
  GtkWidget * menu;
  };

struct command_menu_s
  {
  GtkWidget * inc_volume;
  GtkWidget * dec_volume;

  GtkWidget * mute;

  GtkWidget * next;
  GtkWidget * previous;

  GtkWidget * next_chapter;
  GtkWidget * previous_chapter;
  GtkWidget * current_to_favourites;
  GtkWidget * goto_current;

  GtkWidget * next_visualization;
  
  GtkWidget * seek_item;
  GtkWidget * play;
  GtkWidget * pause;
  GtkWidget * stop;
  
  GtkWidget * quit;

  struct seek_menu_s       seek_menu;
  
  GtkWidget * menu;
  };


struct main_menu_s
  {
  struct windows_menu_s       windows_menu;
  struct help_menu_s          help_menu;
  struct command_menu_s       command_menu;
  struct stream_menu_s        audio_stream_menu;
  struct stream_menu_s        video_stream_menu;
  struct stream_menu_s        subtitle_stream_menu;
  struct chapter_menu_s       chapter_menu;
  
  GtkWidget * windows_item;
  GtkWidget * help_item;
  GtkWidget * preferences_item;
  GtkWidget * command_item;
  
  GtkWidget * audio_stream_item;
  GtkWidget * video_stream_item;
  GtkWidget * subtitle_stream_item;
  GtkWidget * chapter_item;
  GtkWidget * visualization_item;
  
  GtkWidget * player_backend_item;
  GtkWidget * mdb_backend_item;
  
  GtkWidget * menu;
  gmerlin_t * g;

  bg_gtk_backend_menu_t * player_backend_menu;
  bg_gtk_backend_menu_t * mdb_backend_menu;
  
  };

static void stream_menu_set_index(stream_menu_t * s, int index);

static GtkWidget * create_menu()
  {
  GtkWidget * ret;
  //  GtkWidget * tearoff_item;

  ret = gtk_menu_new();
#if 0
  tearoff_item = gtk_tearoff_menu_item_new();
  gtk_widget_show(tearoff_item);
  gtk_menu_shell_append(GTK_MENU_SHELL(ret), tearoff_item);
#endif
  return ret;
  }

static int stream_menu_has_widget(stream_menu_t * s,
                                  GtkWidget * w, int * index)
  {
  int i;
  if((w == s->off_item) &&
     gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(s->off_item)))
    {
    *index = -1;
    return 1;
    }
  for(i = 0; i < s->num_streams; i++)
    {
    if((w == s->stream_items[i]) &&
       gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(s->stream_items[i])))
      {
      *index = i;
      return 1;
      }
    }
  return 0;
  }

static int chapter_menu_has_widget(chapter_menu_t * s,
                                   GtkWidget * w, int * index)
  {
  int i;
  for(i = 0; i < s->num_chapters; i++)
    {
    if((w == s->chapter_items[i]) &&
       gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(s->chapter_items[i])))
      {
      *index = i;
      return 1;
      }
    }
  return 0;
  }


static void menu_callback(GtkWidget * w, gpointer data)
  {
  int i;
  gmerlin_t * g;
  main_menu_t * the_menu;

  g = (gmerlin_t*)data;
  the_menu = g->main_menu;
  
  if(w == the_menu->preferences_item)
    gmerlin_configure(g);
  else if(w == the_menu->windows_menu.infowindow)
    {
    if(gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(the_menu->windows_menu.infowindow)))
      gtk_widget_show(bg_gtk_info_window_get_widget(g->info_window));
    else
      gtk_widget_hide(bg_gtk_info_window_get_widget(g->info_window));
    }
  else if(w == the_menu->windows_menu.logwindow)
    {
    if(gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(the_menu->windows_menu.logwindow)))
      gtk_widget_show(bg_gtk_log_window_get_widget(g->log_window));
    else
      gtk_widget_hide(bg_gtk_log_window_get_widget(g->log_window));
    }
  else if(w == the_menu->windows_menu.mdbwindow)
    {
    if(gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(the_menu->windows_menu.mdbwindow)))
      gtk_widget_show(g->mdb_window);
    else
      gtk_widget_hide(g->mdb_window);
    }

  else if(w == the_menu->help_menu.about)
    {
    if(!g->about_window)
      g->about_window = bg_gtk_about_window_create();

    gtk_widget_show(g->about_window);
    
    }
#if 0
  else if(w == the_menu->help_menu.help)
    {
    // bg_display_html_help("userguide/GUI-Player.html");
    }
#endif
  
  /* Commands (== accelerators) */
  else if(w == the_menu->command_menu.inc_volume)
    bg_player_accel_pressed(g->player_ctrl, BG_PLAYER_ACCEL_VOLUME_UP);
  else if(w == the_menu->command_menu.dec_volume)
    bg_player_accel_pressed(g->player_ctrl, BG_PLAYER_ACCEL_VOLUME_DOWN);
  else if(w == the_menu->command_menu.pause)
    bg_player_accel_pressed(g->player_ctrl, BG_PLAYER_ACCEL_PAUSE);
  else if(w == the_menu->command_menu.play)
    bg_player_accel_pressed(g->player_ctrl, BG_PLAYER_ACCEL_PLAY);
  else if(w == the_menu->command_menu.mute)
    bg_player_accel_pressed(g->player_ctrl, BG_PLAYER_ACCEL_MUTE);
  else if(w == the_menu->command_menu.next_chapter)
    bg_player_accel_pressed(g->player_ctrl, BG_PLAYER_ACCEL_NEXT_CHAPTER);
  else if(w == the_menu->command_menu.previous_chapter)
    bg_player_accel_pressed(g->player_ctrl, BG_PLAYER_ACCEL_PREV_CHAPTER);
  else if(w == the_menu->command_menu.next)
    bg_player_accel_pressed(g->player_ctrl, BG_PLAYER_ACCEL_NEXT);
  else if(w == the_menu->command_menu.previous)
    bg_player_accel_pressed(g->player_ctrl, BG_PLAYER_ACCEL_PREV);
  else if(w == the_menu->command_menu.stop)
    bg_player_accel_pressed(g->player_ctrl, BG_PLAYER_ACCEL_STOP);
  else if(w == the_menu->command_menu.current_to_favourites)
    bg_player_accel_pressed(g->player_ctrl, ACCEL_CURRENT_TO_FAVOURITES);
  else if(w == the_menu->command_menu.quit)
    bg_player_accel_pressed(g->player_ctrl, ACCEL_QUIT);
  else if(w == the_menu->command_menu.next_visualization)
    bg_player_accel_pressed(g->player_ctrl, BG_PLAYER_ACCEL_NEXT_VISUALIZATION);
  
  else if(w == the_menu->command_menu.seek_menu.seek_00)
    bg_player_accel_pressed(g->player_ctrl, BG_PLAYER_ACCEL_SEEK_START);
  else if(w == the_menu->command_menu.seek_menu.seek_10)
    bg_player_accel_pressed(g->player_ctrl, BG_PLAYER_ACCEL_SEEK_10);
  else if(w == the_menu->command_menu.seek_menu.seek_20)
    bg_player_accel_pressed(g->player_ctrl, BG_PLAYER_ACCEL_SEEK_20);
  else if(w == the_menu->command_menu.seek_menu.seek_30)
    bg_player_accel_pressed(g->player_ctrl, BG_PLAYER_ACCEL_SEEK_30);
  else if(w == the_menu->command_menu.seek_menu.seek_40)
    bg_player_accel_pressed(g->player_ctrl, BG_PLAYER_ACCEL_SEEK_40);
  else if(w == the_menu->command_menu.seek_menu.seek_50)
    bg_player_accel_pressed(g->player_ctrl, BG_PLAYER_ACCEL_SEEK_50);
  else if(w == the_menu->command_menu.seek_menu.seek_60)
    bg_player_accel_pressed(g->player_ctrl, BG_PLAYER_ACCEL_SEEK_60);
  else if(w == the_menu->command_menu.seek_menu.seek_70)
    bg_player_accel_pressed(g->player_ctrl, BG_PLAYER_ACCEL_SEEK_70);
  else if(w == the_menu->command_menu.seek_menu.seek_80)
    bg_player_accel_pressed(g->player_ctrl, BG_PLAYER_ACCEL_SEEK_80);
  else if(w == the_menu->command_menu.seek_menu.seek_90)
    bg_player_accel_pressed(g->player_ctrl, BG_PLAYER_ACCEL_SEEK_90);
  else if(w == the_menu->command_menu.seek_menu.seek_backward)
    bg_player_accel_pressed(g->player_ctrl, BG_PLAYER_ACCEL_SEEK_BACKWARD);
  else if(w == the_menu->command_menu.seek_menu.seek_forward)
    bg_player_accel_pressed(g->player_ctrl, BG_PLAYER_ACCEL_SEEK_FORWARD);
  else if(w == the_menu->command_menu.seek_menu.seek_backward_fast)
    bg_player_accel_pressed(g->player_ctrl, BG_PLAYER_ACCEL_SEEK_BACKWARD_FAST);
  else if(w == the_menu->command_menu.seek_menu.seek_forward_fast)
    bg_player_accel_pressed(g->player_ctrl, BG_PLAYER_ACCEL_SEEK_FORWARD_FAST);
  
  /* Stream selection */
  else if(stream_menu_has_widget(&the_menu->audio_stream_menu, w, &i))
    bg_player_set_audio_stream(g->mainwin.player_ctrl.cmd_sink, i);
  else if(stream_menu_has_widget(&the_menu->video_stream_menu, w, &i))
    bg_player_set_video_stream(g->mainwin.player_ctrl.cmd_sink, i);
  else if(stream_menu_has_widget(&the_menu->subtitle_stream_menu, w, &i))
    bg_player_set_subtitle_stream(g->mainwin.player_ctrl.cmd_sink, i);
  /* Chapters */
  else if(chapter_menu_has_widget(&the_menu->chapter_menu, w, &i))
    bg_player_set_chapter(g->mainwin.player_ctrl.cmd_sink, i);
  }


static GtkWidget * create_item(const char * label, const char * icon,
                               gmerlin_t * gmerlin,
                               GtkWidget * menu)
  {
  GtkWidget * ret;

  if(icon)
    ret = bg_gtk_icon_menu_item_new(label, icon);
  else
    ret = gtk_menu_item_new_with_label(label);
  g_signal_connect(G_OBJECT(ret), "activate",
                   G_CALLBACK(menu_callback),
                   gmerlin);
  gtk_widget_show(ret);
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), ret);
  return ret;
  }

static GtkWidget * create_toggle_item(const char * label,
                                      gmerlin_t * gmerlin,
                                      GtkWidget * menu, guint * id)
  {
  guint32 handler_id;
  GtkWidget * ret;
  ret = gtk_check_menu_item_new_with_label(label);
  handler_id = g_signal_connect(G_OBJECT(ret), "toggled",
                   G_CALLBACK(menu_callback),
                   gmerlin);
  if(id)
    *id = handler_id;
  gtk_widget_show(ret);
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), ret);
  return ret;
  }

static GtkWidget * create_stream_item(gmerlin_t * gmerlin,
                                      stream_menu_t * m,
                                      guint * id)
  {
  GtkWidget * ret;
  ret = gtk_radio_menu_item_new_with_label(m->group, "");
  m->group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(ret));
  
  *id = g_signal_connect(G_OBJECT(ret), "activate",
                         G_CALLBACK(menu_callback),
                         gmerlin);
  gtk_menu_shell_insert(GTK_MENU_SHELL(m->menu), ret, (int)(id - m->ids) + 2);
  return ret;
  }

static GtkWidget * create_chapter_item(gmerlin_t * gmerlin,
                                       chapter_menu_t * m,
                                       guint * id)
  {
  GtkWidget * ret;
  ret = gtk_radio_menu_item_new_with_label(m->group, "");
  m->group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(ret));
  
  *id = g_signal_connect(G_OBJECT(ret), "activate",
                         G_CALLBACK(menu_callback),
                         gmerlin);
  gtk_menu_shell_append(GTK_MENU_SHELL(m->menu), ret);
  return ret;
  }

static GtkWidget * create_submenu_item(const char * label, const char * icon,
                                       GtkWidget * child_menu,
                                       GtkWidget * parent_menu)
  {
  GtkWidget * ret;

  ret = bg_gtk_icon_menu_item_new(label, icon);
  //   ret = gtk_menu_item_new_with_label(label);

  gtk_menu_item_set_submenu(GTK_MENU_ITEM(ret), child_menu);
  gtk_widget_show(ret);

  gtk_menu_shell_append(GTK_MENU_SHELL(parent_menu), ret);
  return ret;
  }


static void stream_menu_init(stream_menu_t * s, gmerlin_t * gmerlin,
                             int has_plugins, int has_filters,
                             bg_plugin_type_t plugin_type)
  {
  GtkWidget * separator;
  s->menu = create_menu();
  s->off_item = gtk_radio_menu_item_new_with_label(NULL, TR("Off"));
  
  s->off_id = g_signal_connect(G_OBJECT(s->off_item), "activate",
                               G_CALLBACK(menu_callback),
                               gmerlin);
  
  s->group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(s->off_item));
  gtk_widget_show(s->off_item);
  gtk_menu_shell_append(GTK_MENU_SHELL(s->menu), s->off_item);

  separator = gtk_separator_menu_item_new();
  gtk_widget_show(separator);
  gtk_menu_shell_append(GTK_MENU_SHELL(s->menu), separator);

  }

static void stream_menu_free(stream_menu_t * s)
  {
  if(s->stream_items)
    free(s->stream_items);
  if(s->ids)
    free(s->ids);
  }

static void stream_menu_set_num(gmerlin_t * g, stream_menu_t * s, int num)
  {
  int i;
  if(num > s->streams_alloc)
    {
    s->stream_items = realloc(s->stream_items, num * sizeof(*s->stream_items));
    s->ids = realloc(s->ids, num * sizeof(*s->ids));

    for(i = s->streams_alloc; i < num; i++)
      s->stream_items[i] = create_stream_item(g, s, &s->ids[i]);
    stream_menu_set_index(s, s->idx);
    s->streams_alloc = num;
    }
  for(i = 0; i < num; i++)
    gtk_widget_show(s->stream_items[i]);
  for(i = num; i < s->streams_alloc; i++)
    gtk_widget_hide(s->stream_items[i]);
  s->num_streams = num;
  }

static void stream_menu_set_index(stream_menu_t * s, int index)
  {
  int i;
  /* Block event handlers */
  g_signal_handler_block(G_OBJECT(s->off_item), s->off_id);
  for(i = 0; i < s->streams_alloc; i++)
    g_signal_handler_block(G_OBJECT(s->stream_items[i]), s->ids[i]);

  /* Select item */
  
  if(index == -1)
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(s->off_item), 1);
  else if(index < s->streams_alloc)
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(s->stream_items[index]), 1);
  
  /* Unblock event handlers */
  g_signal_handler_unblock(G_OBJECT(s->off_item), s->off_id);
  for(i = 0; i < s->streams_alloc; i++)
    g_signal_handler_unblock(G_OBJECT(s->stream_items[i]), s->ids[i]);

  s->idx = index;
  
  }

static void chapter_menu_set_num(gmerlin_t * g, chapter_menu_t * s,
                                 int num, int timescale)
  {
  int i;
  if(num > s->chapters_alloc)
    {
    s->chapter_items = realloc(s->chapter_items,
                               num * sizeof(*s->chapter_items));
    s->ids = realloc(s->ids, num * sizeof(*s->ids));

    for(i = s->chapters_alloc; i < num; i++)
      s->chapter_items[i] = create_chapter_item(g, s, &s->ids[i]);
    s->chapters_alloc = num;
    }
  s->timescale = timescale;
  for(i = 0; i < num; i++)
    gtk_widget_show(s->chapter_items[i]);
  for(i = num; i < s->chapters_alloc; i++)
    gtk_widget_hide(s->chapter_items[i]);
  s->num_chapters = num;
  }



void
main_menu_set_audio_index(main_menu_t * m, int index)
  {
  stream_menu_set_index(&m->audio_stream_menu, index);
  }

void
main_menu_set_video_index(main_menu_t * m, int index)
  {
  stream_menu_set_index(&m->video_stream_menu, index);
  }

void
main_menu_set_subtitle_index(main_menu_t * m, int index)
  {
  stream_menu_set_index(&m->subtitle_stream_menu, index);
  }


void main_menu_set_num_streams(main_menu_t * m,
                               int audio_streams,
                               int video_streams,
                               int subtitle_streams)
  {
  stream_menu_set_num(m->g, &m->audio_stream_menu, audio_streams);
  stream_menu_set_num(m->g, &m->video_stream_menu, video_streams);
  stream_menu_set_num(m->g, &m->subtitle_stream_menu, subtitle_streams);
  }

void main_menu_set_chapters(main_menu_t * m, const gavl_dictionary_t * list)
  {
  int i;
  int num;
  int timescale;
  char * label;
  GtkWidget * w;
  
  if(!gavl_chapter_list_is_valid(list))
    {
    gtk_widget_set_sensitive(m->chapter_item, 0);
    return;
    }
  else
    {
    num = gavl_chapter_list_get_num(list);
    timescale = gavl_chapter_list_get_timescale(list);

    gtk_widget_set_sensitive(m->chapter_item, 1);
    chapter_menu_set_num(m->g, &m->chapter_menu,
                         num,
                         timescale);

    for(i = 0; i < num; i++)
      {
      label = bg_get_chapter_label(i, gavl_chapter_list_get_time(list, i),
                                   timescale, gavl_chapter_list_get_label(list, i));
      w = m->chapter_menu.chapter_items[i];
      gtk_label_set_text(GTK_LABEL(gtk_bin_get_child(GTK_BIN(w))), label);
      free(label);
      }
    
    }
  }


void main_menu_chapter_changed(main_menu_t * m, int chapter)
  {
  GtkWidget * w;
  if(chapter >= m->chapter_menu.num_chapters)
    return;

  w = m->chapter_menu.chapter_items[chapter];
  g_signal_handler_block(G_OBJECT(w), m->chapter_menu.ids[chapter]);
  gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(w), 1);
  g_signal_handler_unblock(G_OBJECT(w), m->chapter_menu.ids[chapter]);
  }

void main_menu_set_audio_info(main_menu_t * m, int stream,
                              const gavl_dictionary_t * metadata)
  {
  char * label;
  GtkWidget * w;
  label = bg_get_stream_label(stream, metadata);
  w = m->audio_stream_menu.stream_items[stream];
  gtk_label_set_text(GTK_LABEL(gtk_bin_get_child(GTK_BIN(w))), label);
  free(label);
  }

void main_menu_set_video_info(main_menu_t * m, int stream,
                              const gavl_dictionary_t * metadata)
  {
  char * label;
  GtkWidget * w;
  label = bg_get_stream_label(stream, metadata);
  w = m->video_stream_menu.stream_items[stream];
  gtk_label_set_text(GTK_LABEL(gtk_bin_get_child(GTK_BIN(w))), label);

  free(label);

  }

void main_menu_set_subtitle_info(main_menu_t * m, int stream,
                                 const gavl_dictionary_t * metadata)
  {
  char * label;
  GtkWidget * w;

  
  label = bg_get_stream_label(stream, metadata);

  // fprintf(stderr, "main_menu_set_subtitle_info %d %s\n", stream, label);
  
  w = m->subtitle_stream_menu.stream_items[stream];
  gtk_label_set_text(GTK_LABEL(gtk_bin_get_child(GTK_BIN(w))), label);
  free(label);
  
  }

main_menu_t * main_menu_create(gmerlin_t * gmerlin)
  {
  main_menu_t * ret;
  ret = calloc(1, sizeof(*ret));
  ret->g = gmerlin;
  /* Windows */
    
  ret->windows_menu.menu = create_menu();

  ret->windows_menu.mdbwindow =
    create_toggle_item(TR("Media DB"), gmerlin, ret->windows_menu.menu,
                       &ret->windows_menu.mdbwindow_id);
  ret->windows_menu.infowindow =
    create_toggle_item(TR("Info window"), gmerlin, ret->windows_menu.menu,
                       &ret->windows_menu.infowindow_id);
  ret->windows_menu.logwindow =
    create_toggle_item(TR("Log window"), gmerlin, ret->windows_menu.menu,
                       &ret->windows_menu.logwindow_id);


  gtk_widget_show(ret->windows_menu.menu);

  /* Help */
  
  ret->help_menu.menu = create_menu();
  ret->help_menu.about = create_item(TR("About..."), BG_ICON_INFO,
                                            gmerlin, ret->help_menu.menu);
  //  ret->help_menu.help = create_pixmap_item(TR("Userguide"), "help_16.png",
  //                                           gmerlin, ret->help_menu.menu);
  
  gtk_widget_show(ret->help_menu.menu);
    
  /* Streams */

  stream_menu_init(&ret->audio_stream_menu, gmerlin, 1, 1, BG_PLUGIN_OUTPUT_AUDIO);
  stream_menu_init(&ret->video_stream_menu, gmerlin, 1, 1, BG_PLUGIN_OUTPUT_VIDEO);
  stream_menu_init(&ret->subtitle_stream_menu, gmerlin, 0, 0, BG_PLUGIN_NONE);

  /* Chapters */
  ret->chapter_menu.menu = create_menu();
  
  /* Commands */
  ret->command_menu.menu = create_menu();
  
  ret->command_menu.inc_volume =
    create_item(TR("Increase volume"), NULL, gmerlin, ret->command_menu.menu);
  gtk_widget_add_accelerator(ret->command_menu.inc_volume, "activate",
                             ret->g->accel_group,
                             GDK_KEY_AudioRaiseVolume, 0, GTK_ACCEL_VISIBLE);


  ret->command_menu.dec_volume =
    create_item(TR("Decrease volume"), NULL, gmerlin, ret->command_menu.menu);
  gtk_widget_add_accelerator(ret->command_menu.dec_volume, "activate",
                             ret->g->accel_group,
                             GDK_KEY_AudioLowerVolume, 0, GTK_ACCEL_VISIBLE);

  ret->command_menu.mute =
    create_item(TR("Toggle mute"), NULL, gmerlin, ret->command_menu.menu);
  gtk_widget_add_accelerator(ret->command_menu.mute, "activate",
                             ret->g->accel_group,
                             GDK_KEY_M, 0, GTK_ACCEL_VISIBLE);
  
  ret->command_menu.next =
    create_item(TR("Next track"), NULL, gmerlin, ret->command_menu.menu);
  gtk_widget_add_accelerator(ret->command_menu.next, "activate", ret->g->accel_group,
                             GDK_KEY_AudioNext, 0, GTK_ACCEL_VISIBLE);

  ret->command_menu.previous =
    create_item(TR("Previous track"), NULL, gmerlin, ret->command_menu.menu);
  gtk_widget_add_accelerator(ret->command_menu.previous, "activate", ret->g->accel_group,
                             GDK_KEY_AudioPrev, 0, GTK_ACCEL_VISIBLE);

  ret->command_menu.next_chapter =
    create_item(TR("Next chapter"), NULL, gmerlin, ret->command_menu.menu);
  gtk_widget_add_accelerator(ret->command_menu.next_chapter, "activate", ret->g->accel_group,
                             GDK_KEY_AudioNext,  GDK_CONTROL_MASK,
                             GTK_ACCEL_VISIBLE);

  ret->command_menu.previous_chapter =
    create_item(TR("Previous chapter"), NULL, gmerlin, ret->command_menu.menu);
  gtk_widget_add_accelerator(ret->command_menu.previous_chapter, "activate", ret->g->accel_group,
                             GDK_KEY_AudioPrev, GDK_CONTROL_MASK,
                             GTK_ACCEL_VISIBLE);

  ret->command_menu.stop =
    create_item(TR("Stop"), NULL, gmerlin, ret->command_menu.menu);
  gtk_widget_add_accelerator(ret->command_menu.stop, "activate", ret->g->accel_group,
                             GDK_KEY_BackSpace, 0,
                             GTK_ACCEL_VISIBLE);
  
  ret->command_menu.goto_current = create_item(TR("Goto current track"),
                                               NULL, 
                                               gmerlin, ret->command_menu.menu);
  gtk_widget_add_accelerator(ret->command_menu.goto_current, "activate",
                             ret->g->accel_group,
                             GDK_KEY_g, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);

  ret->command_menu.next_visualization = create_item(TR("Next visualization"),
                                                     NULL, 
                                                     gmerlin, ret->command_menu.menu);

  gtk_widget_add_accelerator(ret->command_menu.next_visualization, "activate",
                             ret->g->accel_group,
                             GDK_KEY_v, 0, GTK_ACCEL_VISIBLE);

  
  ret->command_menu.current_to_favourites =
    create_item(TR("Copy current track to favourites"), NULL, gmerlin, ret->command_menu.menu);
  gtk_widget_add_accelerator(ret->command_menu.current_to_favourites,
                             "activate", ret->g->accel_group,
                             GDK_KEY_F9, 0, GTK_ACCEL_VISIBLE);

  ret->command_menu.play =
    create_item(TR("Play"), NULL, gmerlin, ret->command_menu.menu);
  gtk_widget_add_accelerator(ret->command_menu.play, "activate", ret->g->accel_group,
                             GDK_KEY_AudioPlay, 0, GTK_ACCEL_VISIBLE);
  
  ret->command_menu.pause =
    create_item(TR("Pause"), NULL, gmerlin, ret->command_menu.menu);
  gtk_widget_add_accelerator(ret->command_menu.pause, "activate", ret->g->accel_group,
                             GDK_KEY_space, 0, GTK_ACCEL_VISIBLE);
  
  ret->command_menu.quit =
    create_item(TR("Quit gmerlin"), NULL, gmerlin, ret->command_menu.menu);
  gtk_widget_add_accelerator(ret->command_menu.quit, "activate",
                             ret->g->accel_group,
                             GDK_KEY_q, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);

  /* Command -> Seek */

  ret->command_menu.seek_menu.menu = create_menu();


  ret->command_menu.seek_menu.seek_forward =
    create_item(TR("Seek forward"), NULL, gmerlin, ret->command_menu.menu);
  gtk_widget_add_accelerator(ret->command_menu.seek_menu.seek_forward, "activate",
                             ret->g->accel_group,
                             GDK_KEY_Right, 0, GTK_ACCEL_VISIBLE);
  
  
  ret->command_menu.seek_menu.seek_backward =
    create_item(TR("Seek backward"), NULL, gmerlin, ret->command_menu.menu);
  gtk_widget_add_accelerator(ret->command_menu.seek_menu.seek_backward, "activate",
                             ret->g->accel_group,
                             GDK_KEY_Left, 0, GTK_ACCEL_VISIBLE);

  ret->command_menu.seek_menu.seek_forward_fast =
    create_item(TR("Seek forward fast"), NULL, gmerlin, ret->command_menu.menu);
  gtk_widget_add_accelerator(ret->command_menu.seek_menu.seek_forward, "activate",
                             ret->g->accel_group,
                             GDK_KEY_Right, GDK_SHIFT_MASK, GTK_ACCEL_VISIBLE);
  
  
  ret->command_menu.seek_menu.seek_backward_fast =
    create_item(TR("Seek backward fast"), NULL, gmerlin, ret->command_menu.menu);
  gtk_widget_add_accelerator(ret->command_menu.seek_menu.seek_backward, "activate",
                             ret->g->accel_group,
                             GDK_KEY_Left, GDK_SHIFT_MASK, GTK_ACCEL_VISIBLE);
  
  ret->command_menu.seek_menu.seek_00 =
    create_item(TR("Seek to start"), NULL, gmerlin, ret->command_menu.seek_menu.menu);
  gtk_widget_add_accelerator(ret->command_menu.seek_menu.seek_00,
                             "activate", ret->g->accel_group,
                             GDK_KEY_0, 0, GTK_ACCEL_VISIBLE);

  ret->command_menu.seek_menu.seek_10 =
    create_item(TR("Seek to 10%"), NULL, gmerlin, ret->command_menu.seek_menu.menu);
  gtk_widget_add_accelerator(ret->command_menu.seek_menu.seek_10,
                             "activate", ret->g->accel_group,
                             GDK_KEY_1, 0, GTK_ACCEL_VISIBLE);
  ret->command_menu.seek_menu.seek_20 =
    create_item(TR("Seek to 20%"), NULL, gmerlin, ret->command_menu.seek_menu.menu);
  gtk_widget_add_accelerator(ret->command_menu.seek_menu.seek_20,
                             "activate", ret->g->accel_group,
                             GDK_KEY_2, 0, GTK_ACCEL_VISIBLE);
  ret->command_menu.seek_menu.seek_30 =
    create_item(TR("Seek to 30%"), NULL, gmerlin, ret->command_menu.seek_menu.menu);
  gtk_widget_add_accelerator(ret->command_menu.seek_menu.seek_30,
                             "activate", ret->g->accel_group,
                             GDK_KEY_3, 0, GTK_ACCEL_VISIBLE);
  ret->command_menu.seek_menu.seek_40 =
    create_item(TR("Seek to 40%"), NULL, gmerlin, ret->command_menu.seek_menu.menu);
  gtk_widget_add_accelerator(ret->command_menu.seek_menu.seek_40,
                             "activate", ret->g->accel_group,
                             GDK_KEY_4, 0, GTK_ACCEL_VISIBLE);
  ret->command_menu.seek_menu.seek_50 =
    create_item(TR("Seek to 50%"), NULL, gmerlin, ret->command_menu.seek_menu.menu);
  gtk_widget_add_accelerator(ret->command_menu.seek_menu.seek_50,
                             "activate", ret->g->accel_group,
                             GDK_KEY_5, 0, GTK_ACCEL_VISIBLE);
  ret->command_menu.seek_menu.seek_60 =
    create_item(TR("Seek to 60%"), NULL, gmerlin, ret->command_menu.seek_menu.menu);
  gtk_widget_add_accelerator(ret->command_menu.seek_menu.seek_60,
                             "activate", ret->g->accel_group,
                             GDK_KEY_6, 0, GTK_ACCEL_VISIBLE);
  ret->command_menu.seek_menu.seek_70 =
    create_item(TR("Seek to 70%"), NULL, gmerlin, ret->command_menu.seek_menu.menu);
  gtk_widget_add_accelerator(ret->command_menu.seek_menu.seek_70,
                             "activate", ret->g->accel_group,
                             GDK_KEY_7, 0, GTK_ACCEL_VISIBLE);
  ret->command_menu.seek_menu.seek_80 =
    create_item(TR("Seek to 80%"), NULL, gmerlin, ret->command_menu.seek_menu.menu);
  gtk_widget_add_accelerator(ret->command_menu.seek_menu.seek_80,
                             "activate", ret->g->accel_group,
                             GDK_KEY_8, 0, GTK_ACCEL_VISIBLE);
  ret->command_menu.seek_menu.seek_90 =
    create_item(TR("Seek to 90%"), NULL, gmerlin, ret->command_menu.seek_menu.menu);
  gtk_widget_add_accelerator(ret->command_menu.seek_menu.seek_90,
                             "activate", ret->g->accel_group,
                             GDK_KEY_9, 0, GTK_ACCEL_VISIBLE);

  ret->command_menu.seek_item =
    create_submenu_item(TR("Seek..."), NULL,
                        ret->command_menu.seek_menu.menu,
                        ret->command_menu.menu);
 

  /* Backend menus */

  ret->player_backend_menu =
    bg_gtk_backend_menu_create(BG_BACKEND_RENDERER,
                               1,
                               /* Will send BG_MSG_SET_BACKEND events */
                               gmerlin->mainwin.player_ctrl.evt_sink);
  
  ret->mdb_backend_menu =
    bg_gtk_backend_menu_create(BG_BACKEND_MEDIASERVER,
                               1,
                               /* Will send BG_MSG_SET_BACKEND events */
                               gmerlin->mainwin.player_ctrl.evt_sink);
  
  /* Main menu */
    
  ret->menu = create_menu();

  ret->audio_stream_item = create_submenu_item(TR("Audio..."), BG_ICON_MUSIC,
                                               ret->audio_stream_menu.menu,
                                               ret->menu);

  ret->video_stream_item = create_submenu_item(TR("Video..."), BG_ICON_FILM,
                                               ret->video_stream_menu.menu,
                                               ret->menu);

  ret->subtitle_stream_item = create_submenu_item(TR("Subtitles..."), BG_ICON_SUBTITLE,
                                                  ret->subtitle_stream_menu.menu,
                                                  ret->menu);
  
  ret->chapter_item = create_submenu_item(TR("Chapters..."),BG_ICON_CHAPTERS,
                                          ret->chapter_menu.menu,
                                          ret->menu);

  ret->preferences_item =
    create_item(TR("Preferences..."), BG_ICON_CONFIG, gmerlin, ret->menu);

  gtk_widget_add_accelerator(ret->preferences_item, "activate", ret->g->accel_group,
                             GDK_KEY_o, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);


  
  ret->windows_item = create_submenu_item(TR("Windows..."), NULL,
                                          ret->windows_menu.menu,
                                          ret->menu);


  ret->command_item = create_submenu_item(TR("Commands..."), NULL,
                                           ret->command_menu.menu,
                                           ret->menu);
  
  ret->player_backend_item = create_submenu_item(TR("Player backend"), BG_ICON_PLAYER,
                                                 bg_gtk_backend_menu_get_widget(ret->player_backend_menu),
                                                 ret->menu);

  ret->mdb_backend_item = create_submenu_item(TR("Database backend"), BG_ICON_SERVER,
                                              bg_gtk_backend_menu_get_widget(ret->mdb_backend_menu),
                                              ret->menu);
  
  ret->help_item = create_submenu_item(TR("Help..."),BG_ICON_HELP,
                                       ret->help_menu.menu,
                                       ret->menu);
  
  gtk_widget_show(ret->menu);

  
  return ret;
  }


void main_menu_destroy(main_menu_t * m)
  {
  stream_menu_free(&m->audio_stream_menu);
  stream_menu_free(&m->video_stream_menu);
  stream_menu_free(&m->subtitle_stream_menu);
  
  free(m);
  }

GtkWidget * main_menu_get_widget(main_menu_t * m)
  {
  return m->menu;
  }


void main_menu_set_info_window_item(main_menu_t * m, int state)
  {
  g_signal_handler_block(G_OBJECT(m->windows_menu.infowindow),
                         m->windows_menu.infowindow_id);
  gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(m->windows_menu.infowindow), state);
  g_signal_handler_unblock(G_OBJECT(m->windows_menu.infowindow),
                           m->windows_menu.infowindow_id);
  }

void main_menu_set_mdb_window_item(main_menu_t * m, int state)
  {
  g_signal_handler_block(G_OBJECT(m->windows_menu.mdbwindow),
                         m->windows_menu.mdbwindow_id);
  gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(m->windows_menu.mdbwindow), state);
  g_signal_handler_unblock(G_OBJECT(m->windows_menu.mdbwindow),
                           m->windows_menu.mdbwindow_id);
  }

void main_menu_set_log_window_item(main_menu_t * m, int state)
  {
  g_signal_handler_block(G_OBJECT(m->windows_menu.logwindow),
                         m->windows_menu.logwindow_id);
  gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(m->windows_menu.logwindow), state);
  g_signal_handler_unblock(G_OBJECT(m->windows_menu.logwindow),
                           m->windows_menu.logwindow_id);
  }


void main_menu_ping(main_menu_t * m)
  {
  bg_gtk_backend_menu_ping(m->player_backend_menu);
  bg_gtk_backend_menu_ping(m->mdb_backend_menu);
  }

