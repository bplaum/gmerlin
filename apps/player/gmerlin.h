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



#include <gtk/gtk.h>

#include <config.h>
#include <gavl/gavlsocket.h>

#include <gmerlin/player.h>
#include <gmerlin/playermsg.h>
#include <gmerlin/cfg_registry.h>
#include <gmerlin/pluginregistry.h>
// #include <gui_gtk/infowindow.h>
#include <gui_gtk/logwindow.h>
#include <gui_gtk/mdb.h>

#include <gmerlin/websocket.h>
#include <gmerlin/mdb.h>

#include <gmerlin/cfg_dialog.h>

#ifdef HAVE_DBUS
#include <gmerlin/bgdbus.h>
#endif


// #include <gmerlin/remote.h>

typedef struct gmerlin_s gmerlin_t;

/* Main menu */

typedef struct main_menu_s main_menu_t;

main_menu_t * main_menu_create(gmerlin_t * gmerlin);

void main_menu_destroy(main_menu_t *);

GtkWidget * main_menu_get_widget(main_menu_t *);

void main_menu_set_chapters(main_menu_t * m, const gavl_dictionary_t * list);

void main_menu_set_num_streams(main_menu_t *,
                               int audio_streams,
                               int video_streams,
                               int subtitle_streams);

void main_menu_chapter_changed(main_menu_t * m, int chapter);

void main_menu_set_audio_info(main_menu_t *, int stream,
                              const gavl_dictionary_t * m);

void main_menu_set_video_info(main_menu_t *, int stream,
                              const gavl_dictionary_t * m);

void main_menu_set_subtitle_info(main_menu_t *, int stream,
                                 const gavl_dictionary_t * m);

void
main_menu_update_streams(main_menu_t *,
                         int num_audio_streams,
                         int num_video_streams,
                         int num_subpicture_streams,
                         int num_programs);

void
main_menu_set_audio_index(main_menu_t *, int);

void
main_menu_set_video_index(main_menu_t *, int);

void
main_menu_set_subtitle_index(main_menu_t *, int);


void main_menu_set_info_window_item(main_menu_t * m, int state);
void main_menu_set_mdb_window_item(main_menu_t * m, int state);

void main_menu_set_log_window_item(main_menu_t * m, int state);

void
main_menu_set_oa_uri(main_menu_t *, const char *);

void
main_menu_set_ov_uri(main_menu_t *, const char *);


void main_menu_ping(main_menu_t * m);


/* New player window */
typedef struct
  {
  GtkWidget * win;
  GtkWidget * play_button;
  GtkWidget * stop_button;
  GtkWidget * next_button;
  GtkWidget * prev_button;
  GtkWidget * pause_button;
  GtkWidget * menu_button;
  GtkWidget * close_button;
  
  GtkWidget * status_label;
  GtkWidget * mode_label;
  GtkWidget * mode_box;

  GtkWidget * time_label;
  GtkWidget * time_box;

  GtkWidget * all_label;
  GtkWidget * rem_label;

  GtkWidget * track_image;
  GtkWidget * track_icon;
  GtkWidget * track_info;
  
  GtkWidget * seek_slider;
  GtkWidget * volume_slider;
  GtkWidget * volume_label;
  
  GtkCssProvider * skin_provider;
  GtkCssProvider * icon_provider;
  
  gmerlin_t * g;
  
  int volume_active;

  gulong seek_change_id;
  gulong volume_change_id;

  bg_control_t player_ctrl;

  int display_mode;

  int seek_flags;
  
  } main_window_t;

void main_window_init(main_window_t * ret, gmerlin_t * g);

void main_window_show(main_window_t * w);

void main_window_connect(main_window_t * w);
void main_window_disconnect(main_window_t * w);


/* Accelerators */

#define ACCEL_QUIT                   (BG_PLAYER_ACCEL_PRIV+1)
#define ACCEL_OPTIONS                (BG_PLAYER_ACCEL_PRIV+2)
#define ACCEL_GOTO_CURRENT           (BG_PLAYER_ACCEL_PRIV+3)
#define ACCEL_CURRENT_TO_FAVOURITES  (BG_PLAYER_ACCEL_PRIV+4)


#define PLAYBACK_SKIP_ERROR (1<<0)

struct gmerlin_s
  {
  main_window_t mainwin;
  
  int playback_flags;

  /* Core stuff */
  
  bg_player_t          * player;
  
  bg_controllable_t    * player_ctrl;
  bg_controllable_t    * mdb_ctrl;
  
  bg_mdb_t * mdb;
  
  /* GUI */
  
  bg_dialog_t * cfg_dialog;
  
  bg_gtk_mdb_tree_t * mdb_tree;
  GtkWidget * mdb_window;
  
  bg_gtk_trackinfo_t * info_window;
  bg_gtk_log_window_t * log_window;
  
  bg_cfg_ctx_t * cfg_player;
  bg_cfg_ctx_t * cfg_mdb;
  
  int tree_error;

  /* Configuration stuff */
  
  gavl_dictionary_t * display_section;
  gavl_dictionary_t * tree_section;
  gavl_dictionary_t * general_section;
  gavl_dictionary_t * lcdproc_section;
  gavl_dictionary_t * remote_section;
  gavl_dictionary_t * logwindow_section;
  gavl_dictionary_t * infowindow_section;

  bg_parameter_info_t * input_plugin_parameters;
  bg_parameter_info_t * image_reader_parameters;
  
  GtkWidget * about_window;
  
  //  bg_lcdproc_t * lcdproc;

  /* Remote control */
  bg_http_server_t   * srv;

  bg_server_storage_t * client_config;
  
  int player_state;

  /* For all windows */
  GtkAccelGroup *accel_group;

  gavl_dictionary_t state;

  bg_plugin_handle_t * player_backend;
  bg_plugin_handle_t * mdb_backend;

  bg_frontend_t ** renderer_frontends;
  int num_renderer_frontends;

  bg_frontend_t ** mdb_frontends;
  int num_mdb_frontends;
  
  
  pthread_mutex_t backend_mutex;
  
  int stop;
  int frontend_thread_running;
  pthread_mutex_t stop_mutex;
  pthread_t frontend_thread;

  main_menu_t * main_menu;
  
  };



gmerlin_t * gmerlin_create(const gavl_dictionary_t * saved_state, const char * db_path);

/* Right after creating, urls can be added */

void gmerlin_add_locations(gmerlin_t * g, const char ** locations);
void gmerlin_play_locations(gmerlin_t * g, const char ** locations);

void gmerlin_open_device(gmerlin_t * g, const char * device);
void gmerlin_play_device(gmerlin_t * g, const char * device);

void gmerlin_destroy(gmerlin_t*);

void gmerlin_run(gmerlin_t*, const char ** locations,
                           gavl_array_t * fe_arr_mdb, gavl_array_t * fe_arr_renderer);

// void gmerlin_set_next_track(gmerlin_t * g);

void gmerlin_connect_player(gmerlin_t * gmerlin);
void gmerlin_disconnect_player(gmerlin_t * gmerlin);

void gmerlin_connect_mdb(gmerlin_t * gmerlin);
void gmerlin_disconnect_mdb(gmerlin_t * gmerlin);


/* Skin stuff */

/* Load a skin from directory. Return the default dierectory if the
   skin could not be found */

/* Run the main config dialog */

void gmerlin_create_dialog(gmerlin_t * g);

void gmerlin_configure(gmerlin_t * g);


int gmerlin_handle_message(void * data, gavl_msg_t * msg);

/* This is called when the player signals that it wants a new
   track */

// void gmerlin_next_track(gmerlin_t * g);

const bg_parameter_info_t * gmerlin_get_parameters(gmerlin_t * g);

void gmerlin_set_parameter(void * data, const char * name,
                           const gavl_value_t * val);

/* Handle remote command */

void gmerlin_handle_remote(gmerlin_t * g, gavl_msg_t * msg);
