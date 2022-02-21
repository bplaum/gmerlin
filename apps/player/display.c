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

#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include "gmerlin.h"

#include <gavl/metatags.h>


#include <gmerlin/utils.h>
#include <gmerlin/xmlutils.h>
#include <gmerlin/bgcairo.h>
#include <gmerlin/iconfont.h>
#include <gmerlin/log.h>

#include <config.h>

#include <gmerlin/translation.h>

#include <gui_gtk/display.h>
#include <gui_gtk/gtkutils.h>
#include <gui_gtk/scrolltext.h>

#define STATE_STOPPED     0
#define STATE_PLAYING     1
#define STATE_PAUSED      2
#define STATE_CHANGING    3
#define STATE_SEEKING     4
#define STATE_BUFFERING_1 5
#define STATE_BUFFERING_2 6
#define STATE_BUFFERING_3 7
#define STATE_BUFFERING_4 8
#define STATE_BUFFERING_5 9
#define STATE_ERROR       10
#define STATE_MUTE        11
#define NUM_STATES        12

#define DIGIT_HEIGHT      32
#define DIGIT_WIDTH       20

#define DELAY_TIME 10

#define MODE_AREA_WIDTH   40
#define MODE_AREA_HEIGHT  16

typedef enum
  {
    DISPLAY_MODE_NONE,
    DISPLAY_MODE_ALL,
    DISPLAY_MODE_REM,
    DISPLAY_MODE_ALL_REM,
    NUM_DISPLAY_MODES
  } display_mode_t; /* Mode for the time display */

static const bg_parameter_info_t parameters[] =
  {
    {
      .name = "get_colors_from_skin",
      .long_name = TRS("Get colors from skin"),
      .type = BG_PARAMETER_CHECKBUTTON,
      .val_default = GAVL_VALUE_INIT_INT(1),
      .help_string = TRS("Take the display colors from the skin definition")
    },
    {
      .name =      "background",
      .long_name = TRS("Background"),
      .type = BG_PARAMETER_COLOR_RGB,
      .val_default = GAVL_VALUE_INIT_COLOR_RGB(0.0, 0.0, 0.0),
    },
    {
      .name =      "foreground_normal",
      .long_name = TRS("Normal foreground"),
      .type = BG_PARAMETER_COLOR_RGB,
      .val_default = GAVL_VALUE_INIT_COLOR_RGB(1.0, 0.5, 0.0),
    },
    {
      .name =      "foreground_error",
      .long_name = TRS("Error foreground"),
      .type = BG_PARAMETER_COLOR_RGB,
      .val_default = GAVL_VALUE_INIT_COLOR_RGB(1.0, 0.0, 0.0),
    },
    {
      .name =      "display_mode",
      .long_name = TRS("Display mode"),
      .type = BG_PARAMETER_INT,
      .flags =       BG_PARAMETER_HIDE_DIALOG,
      .val_min =     GAVL_VALUE_INIT_INT(DISPLAY_MODE_NONE),
      .val_max =     GAVL_VALUE_INIT_INT(NUM_DISPLAY_MODES - 1),
      .val_default = GAVL_VALUE_INIT_INT(DISPLAY_MODE_NONE),
    },
    {
      .name =      "play_mode",
      .long_name = TRS("Play mode"),
      .type = BG_PARAMETER_INT,
      .flags =       BG_PARAMETER_HIDE_DIALOG,
      .val_min =     GAVL_VALUE_INIT_INT(0),
      .val_max =     GAVL_VALUE_INIT_INT(BG_PLAYER_MODE_MAX - 1),
      .val_default = GAVL_VALUE_INIT_INT(BG_PLAYER_MODE_NORMAL),
    },
    {
      .name =      "fontname",
      .long_name = TRS("Font"),
      .type = BG_PARAMETER_FONT,
      .val_default = GAVL_VALUE_INIT_STRING("Sans 10"),
    },
    { /* End of parameters */ }
  };

const bg_parameter_info_t * display_get_parameters(display_t * display)
  {
  return parameters;
  }

int pixbufs_loaded = 0;

cairo_surface_t * state_pixbufs[NUM_STATES];
cairo_surface_t * display_pixbufs[NUM_DISPLAY_MODES];

static cairo_surface_t * load_pixbuf(const char * filename)
  {
  char * tmp_string;
  cairo_surface_t * ret;

  tmp_string = bg_search_file_read("icons", filename);
  ret = cairo_image_surface_create_from_png(tmp_string);
  free(tmp_string);
  return ret;
  }

static void load_pixbufs()
  {
  if(pixbufs_loaded)
    return;

  state_pixbufs[STATE_STOPPED]     = load_pixbuf("state_stopped.png");
  state_pixbufs[STATE_PLAYING]     = load_pixbuf("state_playing.png");
  state_pixbufs[STATE_PAUSED]      = load_pixbuf("state_paused.png");
  state_pixbufs[STATE_CHANGING]    = load_pixbuf("state_changing.png");
  state_pixbufs[STATE_SEEKING]     = load_pixbuf("state_seeking.png");
  state_pixbufs[STATE_BUFFERING_1] = load_pixbuf("state_buffering_1.png");
  state_pixbufs[STATE_BUFFERING_2] = load_pixbuf("state_buffering_2.png");
  state_pixbufs[STATE_BUFFERING_3] = load_pixbuf("state_buffering_3.png");
  state_pixbufs[STATE_BUFFERING_4] = load_pixbuf("state_buffering_4.png");
  state_pixbufs[STATE_BUFFERING_5] = load_pixbuf("state_buffering_5.png");
  state_pixbufs[STATE_ERROR]       = load_pixbuf("state_error.png");
  state_pixbufs[STATE_MUTE]        = load_pixbuf("state_mute.png");
  
  display_pixbufs[DISPLAY_MODE_NONE] = load_pixbuf("display_mode_none.png");
  display_pixbufs[DISPLAY_MODE_REM]  = load_pixbuf("display_mode_rem.png");
  display_pixbufs[DISPLAY_MODE_ALL]  = load_pixbuf("display_mode_all.png");
  display_pixbufs[DISPLAY_MODE_ALL_REM] = load_pixbuf("display_mode_all_rem.png");

  pixbufs_loaded = 1;
  }

struct display_s
  {
  bg_control_t player_ctrl;
  
  bg_gtk_time_display_t * time_display;
  bg_gtk_scrolltext_t * scrolltext;

  GtkWidget * widget;
  GtkWidget * state_area;
  GtkWidget * mode_area;
  GtkWidget * display_area;
  
  cairo_surface_t * state_pixbufs[NUM_STATES];
  cairo_surface_t * display_pixbufs[NUM_DISPLAY_MODES];

  float foreground_error[3];
  float foreground_normal[3];
  float background[3];

  /* From the config file */
  
  float foreground_error_cfg[3];
  float foreground_normal_cfg[3];
  float background_cfg[3];

  int get_colors_from_skin;
    
  gmerlin_t * gmerlin;
  display_mode_t display_mode;
  int state_index;

  int state;
  int last_state;
  
  display_skin_t * skin;

  int error_active;

  char * track_name;

  gavl_time_t time;
  gavl_time_t time_all;
  gavl_time_t time_rem;
  gavl_time_t time_rem_all;
  
  guint32 last_click_time;

  int mute;
  char * error_msg;

  int play_mode;

  bg_msg_sink_t * log_sink;
  };

static void set_track_name(display_t * d, const char * name)
  {
  d->track_name = gavl_strrep(d->track_name, name);
  d->error_active = 0;
  
  bg_gtk_scrolltext_set_text(d->scrolltext,
                             d->track_name,
                             d->foreground_normal, d->background);
  }

static void update_background(display_t * d)
  {
  bg_gtk_widget_queue_redraw(d->widget);
  }

static void update_colors(display_t * d)
  {
  int i;
  
  for(i = 0; i < NUM_STATES; i++)
    {
    if(d->state_pixbufs[i])
      cairo_surface_destroy(d->state_pixbufs[i]);
    d->state_pixbufs[i] = bg_gtk_pixbuf_scale_alpha(state_pixbufs[i],
                                                    20, 32,
                                                    d->foreground_normal,
                                                    d->background);
    }
  for(i = 0; i < NUM_DISPLAY_MODES; i++)
    {
    if(d->display_pixbufs[i])
      cairo_surface_destroy(d->display_pixbufs[i]);
    d->display_pixbufs[i] = bg_gtk_pixbuf_scale_alpha(display_pixbufs[i],
                                                      40, 16,
                                                      d->foreground_normal,
                                                      d->background);
    }
  
  if(d->error_active)
    bg_gtk_scrolltext_set_colors(d->scrolltext,
                                 d->foreground_error, d->background);
  else
    bg_gtk_scrolltext_set_colors(d->scrolltext,
                                 d->foreground_normal, d->background);

  bg_gtk_time_display_set_colors(d->time_display,
                                 d->foreground_normal,
                                 d->background);
  update_background(d);
  }

static gboolean draw_callback(GtkWidget * w, cairo_t * cr,
                              gpointer data)
  {
  display_t * d = data;
  
  if(w == d->widget)
    {
    cairo_set_source_rgb(cr, d->background[0],
                         d->background[1],
                         d->background[2]);
    cairo_paint(cr);
    }
  if(w == d->state_area)
    {
    bg_cairo_paint_image(cr, d->state_pixbufs[d->state_index],
                         0.0, 0.0, -1.0, -1.0);
    }
  if(w == d->mode_area)
    {
    PangoRectangle rect;
    PangoLayout * layout;

    char * markup =
      bg_sprintf("<markup><span font_family=\"" BG_ICON_FONT_FAMILY "\" >%s</span></markup>",
                 bg_play_mode_to_icon(d->play_mode));
    layout = pango_cairo_create_layout(cr);

    pango_layout_set_markup(layout, markup, -1);
    pango_cairo_update_layout(cr, layout);
    
    pango_layout_get_extents(layout, NULL, &rect);

    /* Clear background */
    cairo_set_source_rgb(cr, d->background[0],
                         d->background[1],
                         d->background[2]);
    cairo_paint(cr);

    /* Draw layout */
    cairo_set_source_rgb(cr, d->foreground_normal[0],
                         d->foreground_normal[1],
                         d->foreground_normal[2]);
    
    cairo_move_to(cr,
                  ((float)MODE_AREA_WIDTH  - (float)rect.width / PANGO_SCALE)*0.5,
                  ((float)MODE_AREA_HEIGHT - (float)rect.height / PANGO_SCALE)*0.5);
    pango_cairo_show_layout(cr, layout);
    
    g_object_unref(layout);
    free(markup);
    
    //    bg_cairo_paint_image(cr, d->repeat_pixbufs[d->repeat_mode],
    //                         0.0, 0.0, -1.0, -1.0);
    }
  if(w == d->display_area)
    {
    bg_cairo_paint_image(cr, d->display_pixbufs[d->display_mode],
                         0.0, 0.0, -1.0, -1.0);
    }
  return FALSE;
  }

static void set_display_mode(display_t * d)
  {
  gavl_time_t t = GAVL_TIME_UNDEFINED;
  
  if(d->display_mode >= NUM_DISPLAY_MODES)
    d->display_mode = 0;

  switch(d->display_mode)
    {
    case DISPLAY_MODE_NONE:
      t = d->time;
      break;
    case DISPLAY_MODE_ALL:
      t = d->time_all;
      break;
    case DISPLAY_MODE_REM:
      t = d->time_rem;
      break;
    case DISPLAY_MODE_ALL_REM:
      t = d->time_rem_all;
      break;
    default:
      break;
    }
  bg_gtk_time_display_update(d->time_display, t, BG_GTK_DISPLAY_MODE_HMS);
  bg_gtk_widget_queue_redraw(d->display_area);
  }

void display_set_parameter(void * data, const char * name,
                           const gavl_value_t * v)
  {
  display_t * d = data;
  if(!name)
    {
    if(d->get_colors_from_skin && d->skin)
      {
      memcpy(d->foreground_normal, d->skin->foreground_normal,
             3 * sizeof(float));
      memcpy(d->foreground_error, d->skin->foreground_error,
             3 * sizeof(float));
      memcpy(d->background, d->skin->background,
             3 * sizeof(float));
      update_colors(d);
      }
    else
      {
      memcpy(d->foreground_normal, d->foreground_normal_cfg,
             3 * sizeof(float));
      memcpy(d->foreground_error, d->foreground_error_cfg,
             3 * sizeof(float));
      memcpy(d->background, d->background_cfg,
             3 * sizeof(float));
      update_colors(d);
      }
    }
  else if(!strcmp(name, "get_colors_from_skin"))
    d->get_colors_from_skin = v->v.i;
  else if(!strcmp(name, "foreground_error"))
    {
    d->foreground_error_cfg[0] = v->v.color[0];
    d->foreground_error_cfg[1] = v->v.color[1];
    d->foreground_error_cfg[2] = v->v.color[2];
    }
  else if(!strcmp(name, "foreground_normal"))
    {
    d->foreground_normal_cfg[0] = v->v.color[0];
    d->foreground_normal_cfg[1] = v->v.color[1];
    d->foreground_normal_cfg[2] = v->v.color[2];
    }
  else if(!strcmp(name, "background"))
    {
    d->background_cfg[0] = v->v.color[0];
    d->background_cfg[1] = v->v.color[1];
    d->background_cfg[2] = v->v.color[2];
    }
  else if(!strcmp(name, "display_mode"))
    {
    d->display_mode = v->v.i;
    set_display_mode(d);
    }
  else if(!strcmp(name, "fontname"))
    {
    bg_gtk_scrolltext_set_font(d->scrolltext, v->v.str);
    }
  }

int display_get_parameter(void * data, const char * name,
                           gavl_value_t * v)
  {
  display_t * d = data;


  if(!strcmp(name, "display_mode"))
    {
    v->v.i = d->display_mode;
    return 1;
    }
  return 0;
  }

static void realize_callback(GtkWidget * w, gpointer data)
  {
  display_t * d = data;
  update_background(d);
  }

static gboolean button_press_callback(GtkWidget * w, GdkEventButton * evt,
                                      gpointer data)
  {
  display_t * d = data;

  if(evt->time == d->last_click_time)
    return FALSE;
  
  if(w == d->mode_area)
    {
    gavl_value_t val;
    gavl_msg_t * msg;

    gavl_value_init(&val);
    gavl_value_set_int(&val, 1); // Toggle
    
    msg = bg_msg_sink_get(d->player_ctrl.cmd_sink);

    bg_msg_set_state(msg,
                     BG_CMD_SET_STATE_REL,
                     1,
                     BG_PLAYER_STATE_CTX,
                     BG_PLAYER_STATE_MODE,
                     &val);
    
    bg_msg_sink_put(d->player_ctrl.cmd_sink, msg);
    
    d->last_click_time = evt->time;
    return TRUE;
    }
  else if(w == d->display_area)
    {
    d->display_mode++;
    set_display_mode(d);
    d->last_click_time = evt->time;
    return TRUE;
    }
  return FALSE;
  }

static void update_state(display_t * d)
  {
  switch(d->state)
    {
    case BG_PLAYER_STATUS_STOPPED:
    case BG_PLAYER_STATUS_INIT:
      d->state_index = STATE_STOPPED;
      set_track_name(d, "Gmerlin player (version "VERSION")");

      break;
    case BG_PLAYER_STATUS_SEEKING:
      d->state_index = STATE_SEEKING;
      break;
    case BG_PLAYER_STATUS_CHANGING:
    case BG_PLAYER_STATUS_STARTING:
      d->state_index = STATE_CHANGING;
      break;
    case BG_PLAYER_STATUS_PAUSED:
      d->state_index = STATE_PAUSED;
      break;
    case BG_PLAYER_STATUS_ERROR:
      d->state_index = STATE_ERROR;
      bg_gtk_scrolltext_set_text(d->scrolltext,
                                 d->error_msg,
                                 d->foreground_error, d->background);
      
      break;
    default: /* BG_PLAYER_STATUS_PLAYING */
      if(d->state_index != STATE_PLAYING)
        bg_gtk_scrolltext_set_text(d->scrolltext,
                                   d->track_name,
                                   d->foreground_normal, d->background);
      if(d->mute)
        d->state_index = STATE_MUTE;
      else
        d->state_index = STATE_PLAYING;
      break;
    }
  bg_gtk_widget_queue_redraw(d->state_area);
  }


static int display_handle_message(void * data, gavl_msg_t * msg)
  {
  display_t *  d = data;

  switch(msg->NS)
    {
    case BG_MSG_NS_STATE:
      {
      switch(msg->ID)
        {
        case BG_MSG_STATE_CHANGED:
          {
          gavl_value_t val;
          const char * ctx;
          const char * var;
          
          gavl_value_init(&val);

          bg_msg_get_state(msg,
                           NULL,
                           &ctx,
                           &var,
                           &val, NULL);

          if(!strcmp(ctx, BG_PLAYER_STATE_CTX))
            {
            if(!strcmp(var, BG_PLAYER_STATE_CURRENT_TIME))          // long
              {
              const gavl_dictionary_t * dict;

              if(!(dict = gavl_value_get_dictionary(&val)))
                return 1;

              gavl_dictionary_get_long(dict, BG_PLAYER_TIME, &d->time);
              gavl_dictionary_get_long(dict, BG_PLAYER_TIME_REM, &d->time_rem);
              gavl_dictionary_get_long(dict, BG_PLAYER_TIME_ABS, &d->time_all);
              gavl_dictionary_get_long(dict, BG_PLAYER_TIME_REM_ABS, &d->time_rem_all);
              
              if(d->display_mode == DISPLAY_MODE_NONE)
                bg_gtk_time_display_update(d->time_display, d->time, BG_GTK_DISPLAY_MODE_HMS);
              else if(d->display_mode == DISPLAY_MODE_REM)
                bg_gtk_time_display_update(d->time_display, d->time_rem, BG_GTK_DISPLAY_MODE_HMS);
              else if(d->display_mode == DISPLAY_MODE_ALL_REM)
                bg_gtk_time_display_update(d->time_display, d->time_rem_all, BG_GTK_DISPLAY_MODE_HMS);
              else if(d->display_mode == DISPLAY_MODE_ALL)
                bg_gtk_time_display_update(d->time_display, d->time_all, BG_GTK_DISPLAY_MODE_HMS);
              
              }
            else if(!strcmp(var, BG_PLAYER_STATE_VOLUME))     // float
              {
              }
            else if(!strcmp(var, BG_PLAYER_STATE_STATUS))       // int
              {
              if(!gavl_value_get_int(&val, &d->state))
                return 1;
              update_state(d);
              }
            else if(!strcmp(var, BG_PLAYER_STATE_CURRENT_TRACK))         // dictionary
              {
              const gavl_dictionary_t * track;
              const gavl_dictionary_t * m;

              if(!(track = gavl_value_get_dictionary(&val)) ||
                 !(m = gavl_track_get_metadata(track)))
                return 1;
              
              set_track_name(d, gavl_dictionary_get_string(m, GAVL_META_LABEL));
              }
            else if(!strcmp(var, BG_PLAYER_STATE_MODE))          // int
              {
              //              fprintf(stderr, "Mode changed %d\n", val.v.i);
              if(!gavl_value_get_int(&val, &d->play_mode))
                return 1;
              bg_gtk_widget_queue_redraw(d->widget);
              }
            else if(!strcmp(var, BG_PLAYER_STATE_MUTE))          // int
              {
              }
            }
          gavl_value_free(&val);
          }
          break;
        }
      }
      break;
    case BG_MSG_NS_PLAYER:
      {
      }
      break;
    case GAVL_MSG_NS_SRC:
      {
      switch(msg->ID)
        {
        case GAVL_MSG_SRC_BUFFERING:
          break;
        }
      }
    case GAVL_MSG_NS_LOG:
      {
      switch(msg->ID)
        {
        case GAVL_LOG_ERROR:
          if(d->error_msg)
            free(d->error_msg);
          d->error_msg = gavl_strdup(gavl_msg_get_arg_string_c(msg, 1));
          break;
        }
      
      }
    }
  
  return 1;
  }

static int display_handle_log_message(void * data, gavl_msg_t * msg)
  {
  display_t *  d = data;

  switch(msg->NS)
    {
    case GAVL_MSG_NS_LOG:
      {
      switch(msg->ID)
        {
        case GAVL_LOG_ERROR:
          if(d->error_msg)
            free(d->error_msg);
          d->error_msg = gavl_strdup(gavl_msg_get_arg_string_c(msg, 1));
          break;
        }
      
      }
    }
  
  return 1;
  }


static gboolean idle_callback(gpointer data)
  {
  int ret;
  display_t * d = data;

  ret = bg_msg_sink_iteration(d->player_ctrl.evt_sink);
  ret |= bg_msg_sink_iteration(d->log_sink);
  
  return ret ? TRUE : FALSE;
  }

void display_connect(display_t * d)
  {
  bg_controllable_connect(d->gmerlin->player_ctrl, &d->player_ctrl);
  }

void display_disconnect(display_t * d)
  {
  bg_controllable_disconnect(d->gmerlin->player_ctrl, &d->player_ctrl);
  }



display_t * display_create(gmerlin_t * gmerlin)
  {
  display_t * ret;
  load_pixbufs();
  ret = calloc(1, sizeof(*ret));
    
  /* Create objects */
  ret->gmerlin = gmerlin;
  
  bg_control_init(&ret->player_ctrl, bg_msg_sink_create(display_handle_message, ret, 0));

  display_connect(ret);
  
  /* Need also to collect log messages */

  ret->log_sink = bg_msg_sink_create(display_handle_log_message, ret, 0);
  bg_log_add_dest(ret->log_sink);
  
  ret->widget = gtk_layout_new(NULL, NULL);
  g_signal_connect(G_OBJECT(ret->widget),
                   "draw", G_CALLBACK(draw_callback),
                   (gpointer)ret);

  g_signal_connect(G_OBJECT(ret->widget),
                   "realize", G_CALLBACK(realize_callback),
                   (gpointer)ret);
  
  ret->state_index = STATE_STOPPED;
  
  ret->scrolltext = bg_gtk_scrolltext_create(226, 18);

  /* State area */
  
  ret->state_area = gtk_drawing_area_new();
  gtk_widget_set_size_request(ret->state_area, 20, 32);

  g_signal_connect(G_OBJECT(ret->state_area),
                   "draw", G_CALLBACK(draw_callback),
                   (gpointer)ret);

  
  gtk_widget_show(ret->state_area);

  /* Repeat area */
  
  ret->mode_area = gtk_drawing_area_new();
  gtk_widget_set_size_request(ret->mode_area, MODE_AREA_WIDTH, MODE_AREA_HEIGHT);

  gtk_widget_set_events(ret->mode_area,
                        GDK_BUTTON_PRESS_MASK | GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK);

  bg_gtk_tooltips_set_tip(ret->mode_area,
                          "Repeat mode\nClick to change",
                          PACKAGE);
  
  g_signal_connect(G_OBJECT(ret->mode_area),
                   "button_press_event",
                   G_CALLBACK (button_press_callback),
                   ret);

  g_signal_connect(G_OBJECT(ret->mode_area),
                   "draw", G_CALLBACK(draw_callback),
                   (gpointer)ret);
  
  gtk_widget_show(ret->mode_area);

  /* Display mode area */
  
  ret->display_area = gtk_drawing_area_new();
  gtk_widget_set_size_request(ret->display_area, 40, 16);

  gtk_widget_set_events(ret->display_area,
                        GDK_EXPOSURE_MASK |
                        GDK_BUTTON_PRESS_MASK |
                        GDK_ENTER_NOTIFY_MASK |
                        GDK_LEAVE_NOTIFY_MASK);
  bg_gtk_tooltips_set_tip(ret->display_area,
                          "Time display mode\nClick to change",
                          PACKAGE);
  
  
  g_signal_connect(G_OBJECT(ret->display_area),
                   "button_press_event",
                   G_CALLBACK (button_press_callback),
                   ret);

  g_signal_connect(G_OBJECT(ret->display_area),
                   "draw", G_CALLBACK(draw_callback),
                   (gpointer)ret);
  
  gtk_widget_show(ret->display_area);

  /* Scrolltext */
  
  bg_gtk_scrolltext_set_font(ret->scrolltext, "Sans Bold 10");

  ret->time_display =
    bg_gtk_time_display_create(BG_GTK_DISPLAY_SIZE_NORMAL, 0,
                               BG_GTK_DISPLAY_MODE_HMS);

  
  /* Set attributes */

  gtk_widget_set_size_request(ret->widget, DISPLAY_WIDTH, DISPLAY_HEIGHT);
  
  /* Set Callbacks */

  /* Pack */

  gtk_layout_put(GTK_LAYOUT(ret->widget),
                 bg_gtk_scrolltext_get_widget(ret->scrolltext),
                 3, 38);

  gtk_layout_put(GTK_LAYOUT(ret->widget),
                 ret->state_area,
                 3, 3);

  gtk_layout_put(GTK_LAYOUT(ret->widget),
                 ret->display_area,
                 189, 3);

  gtk_layout_put(GTK_LAYOUT(ret->widget),
                 ret->mode_area,
                 189, 19);

  gtk_layout_put(GTK_LAYOUT(ret->widget),
                 bg_gtk_time_display_get_widget(ret->time_display),
                 26, 3);
  
  gtk_widget_show(ret->widget);

  g_timeout_add(DELAY_TIME, idle_callback, (gpointer)ret);

  return ret;
  }

GtkWidget * display_get_widget(display_t * d)
  {
  return d->widget;
  }

void display_destroy(display_t * d)
  {
  if(d->error_msg)
    free(d->error_msg);

  bg_log_remove_dest(d->log_sink);
  bg_msg_sink_destroy(d->log_sink);

  bg_gtk_time_display_destroy(d->time_display);
  bg_gtk_scrolltext_destroy(d->scrolltext);
  free(d);
  }

#if 0
void display_set_state(display_t * d, int state,
                       const void * arg)
  {
  switch(state)
    {
    case BG_PLAYER_STATUS_ERROR:
      d->error_msg = gavl_strrep(d->error_msg, (char*)arg);
      break;
    default: /* BG_PLAYER_STATUS_PLAYING */
      break;
    }
  d->state = state;
  update_state(d);
  }
#endif

void display_set_mute(display_t * d, int mute)
  {
  d->mute = mute;
  update_state(d);
  }

void display_skin_load(display_skin_t * s,
                       xmlDocPtr doc, xmlNodePtr node)
  {
  char * tmp_string;
  char * rest;
  char * pos;
  char * old_locale;
  
  node = node->children;
  old_locale = setlocale(LC_NUMERIC, "C");
  while(node)
    {
    if(!node->name)
      {
      node = node->next;
      continue;
      }
    tmp_string = (char*)xmlNodeListGetString(doc, node->children, 1);

    if(!BG_XML_STRCMP(node->name, "X"))
      s->x = atoi(tmp_string);
    else if(!BG_XML_STRCMP(node->name, "Y"))
      s->y = atoi(tmp_string);
    else if(!BG_XML_STRCMP(node->name, "BACKGROUND"))
      {
      pos = tmp_string;
      s->background[0] = strtod(pos, &rest);
      pos = rest;
      s->background[1] = strtod(pos, &rest);
      pos = rest;
      s->background[2] = strtod(pos, &rest);
      
      }
    else if(!BG_XML_STRCMP(node->name, "FOREGROUND_NORMAL"))
      {
      pos = tmp_string;
      s->foreground_normal[0] = strtod(pos, &rest);
      pos = rest;
      s->foreground_normal[1] = strtod(pos, &rest);
      pos = rest;
      s->foreground_normal[2] = strtod(pos, &rest);
      }
    else if(!BG_XML_STRCMP(node->name, "FOREGROUND_ERROR"))
      {
      pos = tmp_string;
      s->foreground_error[0] = strtod(pos, &rest);
      pos = rest;
      s->foreground_error[1] = strtod(pos, &rest);
      pos = rest;
      s->foreground_error[2] = strtod(pos, &rest);
      }
    node = node->next;
    xmlFree(tmp_string);
    }
  setlocale(LC_NUMERIC, old_locale);
  }

void display_set_skin(display_t * d,
                      display_skin_t * s)
  {
  d->skin = s;

  if(d->get_colors_from_skin)
    {
    memcpy(d->foreground_normal, d->skin->foreground_normal,
           3 * sizeof(float));
    memcpy(d->foreground_error, d->skin->foreground_error,
           3 * sizeof(float));
    memcpy(d->background, d->skin->background,
           3 * sizeof(float));
    update_colors(d);
    }

  }

void display_get_coords(display_t * d, int * x, int * y)
  {
  *x = d->skin->x;
  *y = d->skin->y;
  }


void display_set_error_msg(display_t * d, char * msg)
  {
  d->error_active = 1;
  bg_gtk_scrolltext_set_text(d->scrolltext,
                             msg,
                             d->foreground_error, d->background);
  
  }

void display_set_buffering(display_t * d, float perc)
  {
  if((d->state_index != STATE_BUFFERING_5) &&
     (d->state_index != STATE_BUFFERING_4) &&
     (d->state_index != STATE_BUFFERING_3) &&
     (d->state_index != STATE_BUFFERING_2) &&
     (d->state_index != STATE_BUFFERING_1))
    {
    bg_gtk_scrolltext_set_text(d->scrolltext,
                               "Buffering...",
                               d->foreground_normal, d->background);
    
    d->last_state = d->state_index;
    }
  if(perc < 0.0)
    d->state_index = d->last_state;
  if(perc > 0.8)
    d->state_index = STATE_BUFFERING_5;
  else if(perc > 0.6)
    d->state_index = STATE_BUFFERING_4;
  else if(perc > 0.4)
    d->state_index = STATE_BUFFERING_3;
  else if(perc > 0.2)
    d->state_index = STATE_BUFFERING_2;
  else
    d->state_index = STATE_BUFFERING_1;
  
  bg_gtk_widget_queue_redraw(d->state_area);
  }
