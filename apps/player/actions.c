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

#include "gmerlin.h"
#include <config.h>

#include <gmerlin/translation.h>


struct menu_s
  {
  GMenu * audio_stream_menu;
  GMenu * video_stream_menu;
  GMenu * subtitle_stream_menu;
  
  GMenu * audio_sink_menu;
  GMenu * video_sink_menu;

  GMenu * player_backend_menu;
  GMenu * mdb_backend_menu;

  bg_controllable_t * player_ctrl;

  GMenu *menu;
  
  };

static void on_accel(GSimpleAction *action, GVariant *parameter, gpointer user_data)
  {
  gint32 accel;
  bg_controllable_t * player_ctrl = user_data;
  accel = g_variant_get_int32(parameter);
  bg_player_accel_pressed(player_ctrl, accel);
  }

static void preferences(GSimpleAction *action, GVariant *parameter, gpointer user_data)
  {
  
  }



void create_window_actions(GActionMap * map, bg_controllable_t * player_ctrl)
  {
  
  }

void create_application_actions(GActionMap * map, bg_controllable_t * player_ctrl)
  {
  GSimpleAction * act = g_simple_action_new("accel", G_VARIANT_TYPE_INT32);
  g_signal_connect(act, "activate", G_CALLBACK(on_accel), player_ctrl);
  g_action_map_add_action(map, G_ACTION(act));
  g_object_unref(act);


  //  act = g_simple_action_new("preferences", G_VARIANT_TYPE_INT32);
  
  }

static void create_accel_item(GMenu * menu, int accel, const char * label)
  {
  char * tmp_string = gavl_sprintf("app.accel(%d)", accel);
  g_menu_append(menu, label, tmp_string);
  free(tmp_string);
  }

menu_t * menu_create(bg_controllable_t * player_ctrl)
  {
  menu_t * ret;
  GMenu * menu;
  GMenu * submenu;
  
  ret = calloc(1, sizeof(*ret));
  ret->menu = g_menu_new();
  
  ret->audio_stream_menu = g_menu_new();
  ret->video_stream_menu = g_menu_new();
  ret->audio_sink_menu = g_menu_new();
  ret->video_sink_menu = g_menu_new();

  ret->subtitle_stream_menu = g_menu_new();

  menu = g_menu_new();
  g_menu_append_submenu(menu, "Streams", G_MENU_MODEL(ret->audio_stream_menu));
  g_menu_append_submenu(menu, "Sinks", G_MENU_MODEL(ret->audio_sink_menu));
  
  g_menu_append_submenu(ret->menu, "Audio", G_MENU_MODEL(menu));

  menu = g_menu_new();
  g_menu_append_submenu(menu, "Streams", G_MENU_MODEL(ret->video_stream_menu));
  g_menu_append_submenu(menu, "Sinks", G_MENU_MODEL(ret->video_sink_menu));
  g_menu_append_submenu(ret->menu, "Audio", G_MENU_MODEL(menu));
  
  g_menu_append_submenu(ret->menu, "Subtitle", G_MENU_MODEL(ret->subtitle_stream_menu));

  menu = g_menu_new();
  create_accel_item(menu, BG_PLAYER_ACCEL_VOLUME_UP, TR("Increase volume"));
  create_accel_item(menu, BG_PLAYER_ACCEL_VOLUME_DOWN, TR("Decrease volume"));
  create_accel_item(menu, BG_PLAYER_ACCEL_MUTE, TR("Toggle mute"));
  create_accel_item(menu, BG_PLAYER_ACCEL_NEXT, TR("Next track"));
  create_accel_item(menu, BG_PLAYER_ACCEL_PREV, TR("Previous track"));
  create_accel_item(menu, BG_PLAYER_ACCEL_NEXT_CHAPTER, TR("Next chapter"));
  create_accel_item(menu, BG_PLAYER_ACCEL_PREV_CHAPTER, TR("Previous chapter"));
  
  create_accel_item(menu, BG_PLAYER_ACCEL_STOP, TR("Stop"));
  create_accel_item(menu, BG_PLAYER_ACCEL_PLAY, TR("Play"));
  create_accel_item(menu, BG_PLAYER_ACCEL_NEXT_VISUALIZATION, TR("Next visualization"));
  
  
  submenu = g_menu_new();
  create_accel_item(submenu, BG_PLAYER_ACCEL_SEEK_START, TR("Seek to start"));
  create_accel_item(submenu, BG_PLAYER_ACCEL_SEEK_10, TR("Seek to 10%"));
  create_accel_item(submenu, BG_PLAYER_ACCEL_SEEK_20, TR("Seek to 20%"));
  create_accel_item(submenu, BG_PLAYER_ACCEL_SEEK_30, TR("Seek to 30%"));
  create_accel_item(submenu, BG_PLAYER_ACCEL_SEEK_40, TR("Seek to 40%"));
  create_accel_item(submenu, BG_PLAYER_ACCEL_SEEK_50, TR("Seek to 50%"));
  create_accel_item(submenu, BG_PLAYER_ACCEL_SEEK_60, TR("Seek to 60%"));
  create_accel_item(submenu, BG_PLAYER_ACCEL_SEEK_70, TR("Seek to 70%"));
  create_accel_item(submenu, BG_PLAYER_ACCEL_SEEK_80, TR("Seek to 80%"));
  create_accel_item(submenu, BG_PLAYER_ACCEL_SEEK_90, TR("Seek to 90%"));
  create_accel_item(submenu, BG_PLAYER_ACCEL_SEEK_BACKWARD, TR("Seek backward"));
  create_accel_item(submenu, BG_PLAYER_ACCEL_SEEK_FORWARD, TR("Seek forward"));
  create_accel_item(submenu, BG_PLAYER_ACCEL_SEEK_BACKWARD_FAST, TR("Seek backward fast"));
  create_accel_item(submenu, BG_PLAYER_ACCEL_SEEK_FORWARD_FAST, TR("Seek forward fast"));

  g_menu_append_submenu(menu, TR("Seek"), G_MENU_MODEL(submenu));
  
  
  g_menu_append_submenu(ret->menu, "Commands", G_MENU_MODEL(menu));
  

  ret->player_backend_menu = g_menu_new();
  ret->mdb_backend_menu = g_menu_new();

  ret->player_ctrl = player_ctrl;
  
  return ret;
  }


#if 0
static void action_quit(GSimpleAction *action, GVariant *param, gpointer app)
  {
  
  }

static void action_inc_volume(GSimpleAction *action, GVariant *param, gpointer app)
  {
  
  }

static void action_dec_volume(GSimpleAction *action, GVariant *param, gpointer app)
  {
  
  }

static void action_about(GSimpleAction *action, GVariant *param, gpointer user_data)
  {
  
  }

static const GActionEntry app_actions[] =
  {
    { "quit",  action_quit,  NULL, NULL, NULL },
    { "about", action_about, NULL, NULL, NULL },
    { "dec_volume", action_dec_volume, NULL, NULL, NULL },
    { "inc_volume", action_inc_volume, NULL, NULL, NULL },
  };

#endif
