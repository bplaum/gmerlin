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
#include <stdio.h>

#include <config.h>

#include <gavl/metatags.h>


#include "gmerlin.h"
#include <gmerlin/utils.h>
#include <gmerlin/xmlutils.h>
#include <gavl/keycodes.h>

#include <gui_gtk/gtkutils.h>

#define DELAY_TIME 10

#define PLAYERWINDOW_STATE_CTX "playerwindow"

#define BACKGROUND_WINDOW GTK_LAYOUT(win->layout)->bin_window
// #define MASK_WINDOW       win->window->window

#define BACKGROUND_WIDGET win->layout
#define MASK_WIDGET       win->window

static const bg_state_var_desc_t state_vars[] =
  {
    { GAVL_META_X,              GAVL_TYPE_INT, GAVL_VALUE_INIT_INT(10)  },
    { GAVL_META_Y,              GAVL_TYPE_INT, GAVL_VALUE_INIT_INT(10)  },
    { /* End */ },
  };


static void set_background(player_window_t * win)
  {
  cairo_region_t * mask = NULL;
  
  int width, height;
  cairo_surface_t * pixbuf;
  
  if(win->mouse_inside)
    pixbuf = win->background_pixbuf_highlight;
  else
    pixbuf = win->background_pixbuf;

  if(!pixbuf)
    return;
  
  width = cairo_image_surface_get_width(pixbuf);
  height = cairo_image_surface_get_height(pixbuf);

  gtk_widget_set_size_request(win->window, width, height);
  //  gtk_window_resize(GTK_WINDOW(win->window), width, height);

  mask = gdk_cairo_region_create_from_surface(pixbuf);
  
  if(mask)
    {
    gtk_widget_shape_combine_region(win->window, mask);
    cairo_region_destroy(mask);
    }
  
  bg_gtk_set_widget_bg_pixmap(win->layout, pixbuf);
  bg_gtk_widget_queue_redraw(win->window);

  }

void player_window_set_skin(player_window_t * win,
                            player_window_skin_t * s,
                            const char * directory)
  {
  int x, y;
  char * tmp_path;
  
  if(win->background_pixbuf)
    cairo_surface_destroy(win->background_pixbuf);
  
  tmp_path = bg_sprintf("%s/%s", directory, s->background);
  win->background_pixbuf = cairo_image_surface_create_from_png(tmp_path);
  free(tmp_path);

  if(s->background_highlight)
    {
    tmp_path = bg_sprintf("%s/%s", directory, s->background_highlight);
    win->background_pixbuf_highlight =
      cairo_image_surface_create_from_png(tmp_path);
    free(tmp_path);
    }

  set_background(win);

  /* Apply the button skins */

  bg_gtk_button_set_skin(win->play_button, &s->play_button, directory);
  bg_gtk_button_get_coords(win->play_button, &x, &y);
  gtk_layout_move(GTK_LAYOUT(win->layout),
                  bg_gtk_button_get_widget(win->play_button),
                  x, y);

  bg_gtk_button_set_skin(win->stop_button, &s->stop_button, directory);
  bg_gtk_button_get_coords(win->stop_button, &x, &y);
  gtk_layout_move(GTK_LAYOUT(win->layout),
                 bg_gtk_button_get_widget(win->stop_button),
                 x, y);

  bg_gtk_button_set_skin(win->pause_button, &s->pause_button, directory);
  bg_gtk_button_get_coords(win->pause_button, &x, &y);
  gtk_layout_move(GTK_LAYOUT(win->layout),
                 bg_gtk_button_get_widget(win->pause_button),
                 x, y);
  
  bg_gtk_button_set_skin(win->next_button, &s->next_button, directory);
  bg_gtk_button_get_coords(win->next_button, &x, &y);
  gtk_layout_move(GTK_LAYOUT(win->layout),
                 bg_gtk_button_get_widget(win->next_button),
                 x, y);
  
  bg_gtk_button_set_skin(win->prev_button, &s->prev_button, directory);
  bg_gtk_button_get_coords(win->prev_button, &x, &y);
  gtk_layout_move(GTK_LAYOUT(win->layout),
                 bg_gtk_button_get_widget(win->prev_button),
                 x, y);

  bg_gtk_button_set_skin(win->close_button, &s->close_button, directory);
  bg_gtk_button_get_coords(win->close_button, &x, &y);
  gtk_layout_move(GTK_LAYOUT(win->layout),
                 bg_gtk_button_get_widget(win->close_button),
                 x, y);

  bg_gtk_button_set_skin(win->menu_button, &s->menu_button, directory);
  bg_gtk_button_get_coords(win->menu_button, &x, &y);
  gtk_layout_move(GTK_LAYOUT(win->layout),
                 bg_gtk_button_get_widget(win->menu_button),
                 x, y);

  /* Apply slider skins */
  
  bg_gtk_slider_set_skin(win->seek_slider, &s->seek_slider, directory);
  bg_gtk_slider_get_coords(win->seek_slider, &x, &y);
  gtk_layout_move(GTK_LAYOUT(win->layout),
                 bg_gtk_slider_get_widget(win->seek_slider),
                 x, y);

  bg_gtk_slider_set_skin(win->volume_slider, &s->volume_slider, directory);
  bg_gtk_slider_get_coords(win->volume_slider, &x, &y);
  gtk_layout_move(GTK_LAYOUT(win->layout),
                  bg_gtk_slider_get_widget(win->volume_slider),
                  x, y);

  /* Apply display skin */
  
  display_set_skin(win->display, &s->display);
  display_get_coords(win->display, &x, &y);
  
  gtk_layout_move(GTK_LAYOUT(win->layout),
                  display_get_widget(win->display),
                  x, y);

  /* Update slider positions */

  bg_gtk_slider_set_pos(win->volume_slider,
                        win->volume);
  
  }

/* Gtk Callbacks */

static void realize_callback(GtkWidget * w, gpointer data)
  {
  
  player_window_t * win;
  
  win = data;
  
  set_background(win);
  }

static gboolean button_press_callback(GtkWidget * w, GdkEventButton * evt,
                                      gpointer data)
  {
  player_window_t * win;
  
  win = data;
  
  win->mouse_x = (int)(evt->x);
  win->mouse_y = (int)(evt->y);
  
  return TRUE;
  }


static gboolean motion_callback(GtkWidget * w, GdkEventMotion * evt,
                                gpointer data)
  {
  player_window_t * win;
  gavl_value_t val;
  gavl_value_init(&val);
  
  /* Buggy (newer) gtk versions send motion events even if no button
     is pressed */
  if(!(evt->state & (GDK_BUTTON1_MASK|GDK_BUTTON2_MASK|GDK_BUTTON3_MASK)))
    return TRUE;

  win = data;
 
//  fprintf(stderr, "gtk_window_move 1 %d %d\n", (int)(evt->x_root) - win->mouse_x, (int)(evt->y_root) - win->mouse_y);
 
  gtk_window_move(GTK_WINDOW(win->window),
                  (int)(evt->x_root) - win->mouse_x,
                  (int)(evt->y_root) - win->mouse_y);

  win->window_x = (int)(evt->x_root - evt->x);
  win->window_y = (int)(evt->y_root - evt->y);

  gavl_value_set_int(&val, win->window_x);
  bg_state_set(NULL, 0, PLAYERWINDOW_STATE_CTX, GAVL_META_X, &val, win->gmerlin->player_ctrl->cmd_sink, BG_CMD_SET_STATE);
  
  gavl_value_set_int(&val, win->window_y);
  bg_state_set(NULL, 1, PLAYERWINDOW_STATE_CTX, GAVL_META_Y, &val, win->gmerlin->player_ctrl->cmd_sink, BG_CMD_SET_STATE);
  
  return TRUE;
  }

/* Gmerlin callbacks */

static void seek_change_callback(bg_gtk_slider_t * slider, float perc,
                                 void * data)
  {
  gavl_time_t time;
  player_window_t * win = (player_window_t *)data;
  
  time = (gavl_time_t)(perc * (double)win->duration);

  if(!win->seek_active)
    {
    if(win->gmerlin->player_state == BG_PLAYER_STATUS_PAUSED)
      win->seek_active = 2;
    else
      {
      win->seek_active = 1;
      bg_player_pause(win->gmerlin->ctrl.cmd_sink);
      }
    }
  
  //  player_window_t * win = (player_window_t *)data;

  bg_player_seek(win->gmerlin->ctrl.cmd_sink, time, GAVL_TIME_SCALE);
  }

static void seek_release_callback(bg_gtk_slider_t * slider, float perc,
                                  void * data)
  {
  gavl_time_t time;
  player_window_t * win = (player_window_t *)data;

  time = (gavl_time_t)(perc * (double)win->duration);
  
  //  player_window_t * win = (player_window_t *)data;
  bg_player_seek(win->gmerlin->ctrl.cmd_sink, time, GAVL_TIME_SCALE);

  if(win->seek_active == 1)
    bg_player_pause(win->gmerlin->ctrl.cmd_sink);
  }

static void
slider_scroll_callback(bg_gtk_slider_t * slider, int up, void * data)
  {
  player_window_t * win = (player_window_t *)data;

  if(slider == win->volume_slider)
    {
    if(up)
      bg_player_set_volume_rel(win->gmerlin->ctrl.cmd_sink, 0.02);
    else
      bg_player_set_volume_rel(win->gmerlin->ctrl.cmd_sink, -0.02);
    }
  else if(slider == win->seek_slider)
    {
    if(up)
      bg_player_seek_rel(win->gmerlin->ctrl.cmd_sink, 2 * GAVL_TIME_SCALE);
    else
      bg_player_seek_rel(win->gmerlin->ctrl.cmd_sink, -2 * GAVL_TIME_SCALE);
    }
  
  }

static void volume_change_callback(bg_gtk_slider_t * slider, float perc,
                                   void * data)
  {
  player_window_t * win = (player_window_t *)data;
  bg_player_set_volume(win->gmerlin->ctrl.cmd_sink, perc);
  win->volume = perc;
  }

static void gmerlin_button_callback_2(bg_gtk_button_t * b, void * data)
  {
  player_window_t * win = (player_window_t *)data;

  if(b == win->next_button)
    {
    bg_player_next_chapter(win->gmerlin->ctrl.cmd_sink);
    }
  else if(b == win->prev_button)
    {
    bg_player_prev_chapter(win->gmerlin->ctrl.cmd_sink);
    }
  }

static void gmerlin_button_callback(bg_gtk_button_t * b, void * data)
  {
  player_window_t * win = (player_window_t *)data;
  if(b == win->play_button)
    {
    bg_player_play(win->gmerlin->ctrl.cmd_sink);
    }
  else if(b == win->pause_button)
    {
    gmerlin_pause(win->gmerlin);
    }
  else if(b == win->stop_button)
    {
    bg_player_stop(win->gmerlin->ctrl.cmd_sink);
    }
  else if(b == win->next_button)
    {
    bg_player_next(win->gmerlin->ctrl.cmd_sink);
    }
  else if(b == win->prev_button)
    {
    bg_player_prev(win->gmerlin->ctrl.cmd_sink);
    }
  else if(b == win->close_button)
    {
    gtk_main_quit();
    }
  }


int gmerlin_handle_message(void * data, gavl_msg_t * msg)
  {
  int arg_i_1;

  gmerlin_t * g = data;
  player_window_t * win = g->player_window;

  //  fprintf(stderr, "Got message %d %d\n", msg->ns, msg->id);
  
  switch(msg->NS)
    {
    }
  return 1;
  }

static gboolean idle_callback(gpointer data)
  {
  player_window_t * w = (player_window_t *)data;

  bg_msg_sink_iteration(w->gmerlin->ctrl.evt_sink);
  
  
  return TRUE;
  }

static gboolean crossing_callback(GtkWidget *widget,
                                  GdkEventCrossing *event,
                                  gpointer data)
  {
  player_window_t * w = (player_window_t *)data;
  if(event->detail == GDK_NOTIFY_INFERIOR)
    return FALSE;

  //  fprintf(stderr, "crossing callback %d %d %d\n",
  //          event->detail, event->type, w->mouse_inside);
 
 
  w->mouse_inside = (event->type == GDK_ENTER_NOTIFY) ? 1 : 0;
  //  fprintf(stderr, "Set background...");

  g_signal_handler_block(w->window, w->enter_notify_id);
  g_signal_handler_block(w->window, w->leave_notify_id); 

  set_background(w);

  g_signal_handler_unblock(w->window, w->enter_notify_id);   
  g_signal_handler_unblock(w->window, w->leave_notify_id); 

  //  fprintf(stderr, "Done\n");
  return FALSE;
  }

void player_window_create(gmerlin_t * g)
  {
  player_window_t * ret;

  ret = calloc(1, sizeof(*ret));
  ret->gmerlin = g;

  g->player_window = ret;
  
  g_timeout_add(DELAY_TIME, idle_callback, (gpointer)ret);
  
  /* Create objects */
  
  ret->window = bg_gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_decorated(GTK_WINDOW(ret->window), FALSE);

  ret->accel_group = gtk_accel_group_new();
  
  gtk_window_add_accel_group (GTK_WINDOW(ret->window), ret->gmerlin->accel_group);
  gtk_window_add_accel_group (GTK_WINDOW(ret->window), ret->accel_group);
  
  ret->layout = gtk_layout_new(NULL, NULL);
  
  /* Set attributes */

  gtk_widget_set_events(ret->window,
                        GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK);
    
  gtk_widget_set_events(ret->layout,
                        GDK_BUTTON1_MOTION_MASK|
                        GDK_BUTTON2_MOTION_MASK|
                        GDK_BUTTON3_MOTION_MASK|
                        GDK_BUTTON_PRESS_MASK);
  /* Set Callbacks */

  g_signal_connect(G_OBJECT(ret->window), "realize",
                   G_CALLBACK(realize_callback), (gpointer*)ret);

  ret->enter_notify_id =
    g_signal_connect(G_OBJECT(ret->window), "enter-notify-event",
                     G_CALLBACK(crossing_callback), (gpointer*)ret);
  ret->leave_notify_id =
    g_signal_connect(G_OBJECT(ret->window), "leave-notify-event",
                     G_CALLBACK(crossing_callback), (gpointer*)ret);
  

  
  g_signal_connect(G_OBJECT(ret->layout), "realize",
                   G_CALLBACK(realize_callback), (gpointer*)ret);

  g_signal_connect(G_OBJECT(ret->layout), "motion-notify-event",
                   G_CALLBACK(motion_callback), (gpointer*)ret);
  
  g_signal_connect(G_OBJECT(ret->layout), "button-press-event",
                   G_CALLBACK(button_press_callback), (gpointer*)ret);

  /* Create child objects */

  ret->play_button = bg_gtk_button_create();
  ret->stop_button = bg_gtk_button_create();
  ret->next_button = bg_gtk_button_create();
  ret->prev_button = bg_gtk_button_create();
  ret->pause_button = bg_gtk_button_create();
  ret->menu_button = bg_gtk_button_create();
  ret->close_button = bg_gtk_button_create();

  ret->seek_slider = bg_gtk_slider_create();
  ret->volume_slider = bg_gtk_slider_create();
  
  ret->display = display_create(g);
  
  /* Set callbacks */

  bg_gtk_slider_set_change_callback(ret->seek_slider,
                                    seek_change_callback, ret);
  
  bg_gtk_slider_set_release_callback(ret->seek_slider,
                                     seek_release_callback, ret);

  bg_gtk_slider_set_change_callback(ret->volume_slider,
                                    volume_change_callback, ret);

  bg_gtk_slider_set_scroll_callback(ret->volume_slider,
                                    slider_scroll_callback, ret);
  bg_gtk_slider_set_scroll_callback(ret->seek_slider,
                                    slider_scroll_callback, ret);

  
  bg_gtk_button_set_callback(ret->play_button, gmerlin_button_callback, ret);
  bg_gtk_button_set_callback(ret->stop_button, gmerlin_button_callback, ret);
  bg_gtk_button_set_callback(ret->pause_button, gmerlin_button_callback, ret);
  bg_gtk_button_set_callback(ret->next_button, gmerlin_button_callback, ret);
  bg_gtk_button_set_callback(ret->prev_button, gmerlin_button_callback, ret);
  bg_gtk_button_set_callback(ret->close_button, gmerlin_button_callback, ret);

  bg_gtk_button_set_callback_2(ret->next_button, gmerlin_button_callback_2, ret);
  bg_gtk_button_set_callback_2(ret->prev_button, gmerlin_button_callback_2, ret);
  
  /* Set tooltips */
  
  bg_gtk_tooltips_set_tip(bg_gtk_button_get_widget(ret->play_button),
                          "Play", PACKAGE);
  bg_gtk_tooltips_set_tip(bg_gtk_button_get_widget(ret->stop_button),
                       "Stop", PACKAGE);
  bg_gtk_tooltips_set_tip(bg_gtk_button_get_widget(ret->pause_button),
                       "Pause", PACKAGE);
  bg_gtk_tooltips_set_tip(bg_gtk_button_get_widget(ret->next_button),
                       "Left button: Next track\nRight button: Next chapter",
                       PACKAGE);
  bg_gtk_tooltips_set_tip(bg_gtk_button_get_widget(ret->prev_button),
                       "Left button: Previous track\nRight button: Previous chapter",
                       PACKAGE);
  
  bg_gtk_tooltips_set_tip(bg_gtk_button_get_widget(ret->menu_button),
                          "Main menu", PACKAGE);
  bg_gtk_tooltips_set_tip(bg_gtk_button_get_widget(ret->close_button),
                          "Quit program", PACKAGE);

  bg_gtk_tooltips_set_tip(bg_gtk_slider_get_slider_widget(ret->volume_slider),
                          "Volume", PACKAGE);

  bg_gtk_tooltips_set_tip(bg_gtk_slider_get_slider_widget(ret->seek_slider),
                          "Seek", PACKAGE);
  
  /* Pack Objects */

  gtk_layout_put(GTK_LAYOUT(ret->layout),
                 bg_gtk_button_get_widget(ret->play_button),
                 0, 0);
  gtk_layout_put(GTK_LAYOUT(ret->layout),
                 bg_gtk_button_get_widget(ret->stop_button),
                 0, 0);
  gtk_layout_put(GTK_LAYOUT(ret->layout),
                 bg_gtk_button_get_widget(ret->pause_button),
                 0, 0);
  gtk_layout_put(GTK_LAYOUT(ret->layout),
                 bg_gtk_button_get_widget(ret->next_button),
                 0, 0);
  gtk_layout_put(GTK_LAYOUT(ret->layout),
                 bg_gtk_button_get_widget(ret->prev_button),
                 0, 0);
  gtk_layout_put(GTK_LAYOUT(ret->layout),
                 bg_gtk_button_get_widget(ret->close_button),
                 0, 0);
  gtk_layout_put(GTK_LAYOUT(ret->layout),
                 bg_gtk_button_get_widget(ret->menu_button),
                 0, 0);
  gtk_layout_put(GTK_LAYOUT(ret->layout),
                 bg_gtk_slider_get_widget(ret->seek_slider),
                 0, 0);
  gtk_layout_put(GTK_LAYOUT(ret->layout),
                 bg_gtk_slider_get_widget(ret->volume_slider),
                 0, 0);

  gtk_layout_put(GTK_LAYOUT(ret->layout),
                 display_get_widget(ret->display),
                 0, 0);
  
  gtk_widget_show(ret->layout);
  gtk_container_add(GTK_CONTAINER(ret->window), ret->layout);
    
  }

void player_window_show(player_window_t * win)
  {
//  fprintf(stderr, "gtk_window_move 2 %d %d\n", win->window_x, win->window_y);

  gtk_window_move(GTK_WINDOW(win->window),
                  win->window_x,
                  win->window_y);
  gtk_widget_show(win->window);
  }

void player_window_destroy(player_window_t * win)
  {
  /* Fetch parameters */
  
  bg_cfg_section_get(win->gmerlin->display_section,
                     display_get_parameters(win->display),
                     display_get_parameter, (void*)(win->display));

  bg_gtk_slider_destroy(win->seek_slider);
  bg_gtk_slider_destroy(win->volume_slider);

  if(win->background_pixbuf)
    cairo_surface_destroy(win->background_pixbuf);
  if(win->background_pixbuf_highlight)
    cairo_surface_destroy(win->background_pixbuf_highlight);
  
  free(win);
  }

void player_window_skin_load(player_window_skin_t * s,
                             xmlDocPtr doc, xmlNodePtr node)
  {
  xmlNodePtr child;
  char * tmp_string;
  child = node->children;
  while(child)
    {
    if(!child->name)
      {
      child = child->next;
      continue;
      }
    else if(!BG_XML_STRCMP(child->name, "BACKGROUND"))
      {
      tmp_string = (char*)xmlNodeListGetString(doc, child->children, 1);
      s->background = gavl_strrep(s->background, tmp_string);
      xmlFree(tmp_string);
      }
    else if(!BG_XML_STRCMP(child->name, "BACKGROUND_HIGHLIGHT"))
      {
      tmp_string = (char*)xmlNodeListGetString(doc, child->children, 1);
      s->background_highlight = gavl_strrep(s->background_highlight, tmp_string);
      xmlFree(tmp_string);
      }

    else if(!BG_XML_STRCMP(child->name, "DISPLAY"))
      display_skin_load(&s->display, doc, child);
    else if(!BG_XML_STRCMP(child->name, "PLAYBUTTON"))
      bg_gtk_button_skin_load(&s->play_button, doc, child);
    else if(!BG_XML_STRCMP(child->name, "PAUSEBUTTON"))
      bg_gtk_button_skin_load(&s->pause_button, doc, child);
    else if(!BG_XML_STRCMP(child->name, "NEXTBUTTON"))
      bg_gtk_button_skin_load(&s->next_button, doc, child);
    else if(!BG_XML_STRCMP(child->name, "PREVBUTTON"))
      bg_gtk_button_skin_load(&s->prev_button, doc, child);
    else if(!BG_XML_STRCMP(child->name, "STOPBUTTON"))
      bg_gtk_button_skin_load(&s->stop_button, doc, child);
    else if(!BG_XML_STRCMP(child->name, "MENUBUTTON"))
      bg_gtk_button_skin_load(&s->menu_button, doc, child);
    else if(!BG_XML_STRCMP(child->name, "CLOSEBUTTON"))
      bg_gtk_button_skin_load(&s->close_button, doc, child);
    else if(!BG_XML_STRCMP(child->name, "SEEKSLIDER"))
      bg_gtk_slider_skin_load(&s->seek_slider, doc, child);
    else if(!BG_XML_STRCMP(child->name, "VOLUMESLIDER"))
      bg_gtk_slider_skin_load(&s->volume_slider, doc, child);
    else if(!BG_XML_STRCMP(child->name, "DISPLAY"))
      display_skin_load(&s->display, doc, child);
    child = child->next;
    }
  }

void player_window_skin_destroy(player_window_skin_t * s)
  {
  if(s->background)
    free(s->background);
  if(s->background_highlight)
    free(s->background_highlight);
  
  bg_gtk_button_skin_free(&s->play_button);
  bg_gtk_button_skin_free(&s->stop_button);
  bg_gtk_button_skin_free(&s->pause_button);
  bg_gtk_button_skin_free(&s->next_button);
  bg_gtk_button_skin_free(&s->prev_button);
  bg_gtk_button_skin_free(&s->close_button);
  bg_gtk_button_skin_free(&s->menu_button);
  bg_gtk_slider_skin_free(&s->seek_slider);
  bg_gtk_slider_skin_free(&s->volume_slider);
  
  }

void player_window_init_state(gavl_dictionary_t * dict)
  {
  bg_state_init_ctx(dict, PLAYERWINDOW_STATE_CTX, state_vars);
  }
