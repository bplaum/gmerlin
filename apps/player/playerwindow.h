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



#include <gui_gtk/button.h>
#include <gui_gtk/slider.h>
#include <gmerlin/msgqueue.h>

typedef struct
  {
  char * background;
  char * background_highlight;
 
  bg_gtk_button_skin_t play_button;
  bg_gtk_button_skin_t pause_button;
  bg_gtk_button_skin_t prev_button;
  bg_gtk_button_skin_t next_button;
  bg_gtk_button_skin_t stop_button;
  bg_gtk_button_skin_t menu_button;
  bg_gtk_button_skin_t close_button;

  bg_gtk_slider_skin_t seek_slider;
  bg_gtk_slider_skin_t volume_slider;

  display_skin_t display;
  
  } player_window_skin_t;

void player_window_skin_load(player_window_skin_t *,
                            xmlDocPtr doc, xmlNodePtr node);

void player_window_skin_destroy(player_window_skin_t *);

/* Player window */

typedef struct player_window_s
  {
  gmerlin_t * gmerlin;
  
  /* Window stuff */
  
  GtkWidget * window;
  GtkWidget * layout;

  /* For moving the window */
  
  int mouse_x;
  int mouse_y;

  int window_x;
  int window_y;
    
  /* Background */
  
  cairo_surface_t * background_pixbuf;
  cairo_surface_t * background_pixbuf_highlight;
  int mouse_inside;
  
  /* GUI Elements */

  bg_gtk_button_t * play_button;
  bg_gtk_button_t * stop_button;
  bg_gtk_button_t * pause_button;
  bg_gtk_button_t * next_button;
  bg_gtk_button_t * prev_button;
  bg_gtk_button_t * close_button;
  bg_gtk_button_t * menu_button;

  bg_gtk_slider_t * seek_slider;
  bg_gtk_slider_t * volume_slider;
  
  display_t * display;
  gavl_time_t duration;

  int seek_active;
  
  float volume;

  /* For the player window only (NOT for album windows) */
  GtkAccelGroup *accel_group;

  /* For avoiding infinite recursions */
  guint enter_notify_id;
  guint leave_notify_id;

  int have_state;
  
  } player_window_t;

void player_window_create(gmerlin_t*);


void player_window_show(player_window_t * win);

void player_window_set_skin(player_window_t * win,
                            player_window_skin_t*,
                            const char * directory);

void player_window_destroy(player_window_t * win);

void player_window_init_state(gavl_dictionary_t * dict);
