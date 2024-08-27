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




#include <inttypes.h> 
#include <stdlib.h> 
#include <stdio.h> 
#include <string.h> 
#include <ctype.h> 

#include <gavl/gavl.h>

#include <gtk/gtk.h>
#include <gui_gtk/display.h>
#include <gui_gtk/gtkutils.h>
#include <gmerlin/utils.h>
#include <gmerlin/bgcairo.h>

#define MAX_CHARS GAVL_TIME_STRING_LEN_MS // 15

struct bg_gtk_time_display_s
  {
  /*  */
  float foreground_color[3];
  float background_color[3];
  int height;
  GtkWidget * widget;

  int font_scale;
  
  int type_mask;
  int max_width;
  int border_width;
  
  PangoFontDescription * font_desc;

  int64_t time;
  int mode;
  };



static gboolean expose_callback(GtkWidget * w, cairo_t * cr,
                                gpointer data)
  {
  char buf[MAX_CHARS];
  PangoRectangle logical_rect;
  PangoLayout * layout;
  
  bg_gtk_time_display_t * d = data;

  layout = pango_cairo_create_layout(cr);
  
  cairo_set_source_rgb(cr,
                       d->background_color[0],
                       d->background_color[1],
                       d->background_color[2]);
  
  cairo_paint(cr);

  switch(d->mode)
    {
    case BG_GTK_DISPLAY_MODE_HMS:
      gavl_time_prettyprint(d->time, buf);
      break;
    case BG_GTK_DISPLAY_MODE_HMSMS:
      gavl_time_prettyprint_ms(d->time, buf);
      break;
    case BG_GTK_DISPLAY_MODE_TIMECODE:
      gavl_timecode_prettyprint_short(d->time, buf);
      break;
    case BG_GTK_DISPLAY_MODE_FRAMECOUNT:
      sprintf(buf, "%"PRId64, d->time);
      break;
    }
  
  cairo_set_source_rgb(cr,
                       d->foreground_color[0],
                       d->foreground_color[1],
                       d->foreground_color[2]);

  layout = pango_cairo_create_layout(cr);
  pango_layout_set_font_description(layout, d->font_desc);
  pango_layout_set_text(layout, buf, -1);

  pango_layout_get_extents(layout, NULL, &logical_rect);

  cairo_move_to(cr, d->max_width - logical_rect.width/PANGO_SCALE,
                (double)(d->height - logical_rect.height/PANGO_SCALE)*0.5);
  
  pango_cairo_show_layout(cr, layout);
  
  return TRUE;
  }


void bg_gtk_time_display_set_colors(bg_gtk_time_display_t * d,
                                    float * foreground,
                                    float * background)
  {
  memcpy(d->foreground_color, foreground, 3 * sizeof(float));
  memcpy(d->background_color, background, 3 * sizeof(float));

  // set_bg_color(d);
  bg_gtk_widget_queue_redraw(d->widget);
  }

void bg_gtk_time_display_set_font(bg_gtk_time_display_t * d, const char * font)
  {
  int size;
  if(d->font_desc)
    pango_font_description_free(d->font_desc);
  d->font_desc = pango_font_description_from_string(font);
  
  size = pango_font_description_get_size(d->font_desc);
  pango_font_description_set_size(d->font_desc, size * d->font_scale);
  }

void bg_gtk_time_display_update(bg_gtk_time_display_t * d,
                                int64_t time, int mode)
  {
  d->time = time;
  d->mode = mode;
  bg_gtk_widget_queue_redraw(d->widget);
  
  }

bg_gtk_time_display_t *
bg_gtk_time_display_create(BG_GTK_DISPLAY_SIZE size, int border_width,
                           int type_mask)
  {
  int digit_width = 0;
  int colon_width = 0;
  bg_gtk_time_display_t * ret;
  
  ret = calloc(1, sizeof(*ret));
  ret->border_width = border_width;
  ret->type_mask = type_mask;

  
  switch(size)
    {
    case BG_GTK_DISPLAY_SIZE_HUGE:   /* 480 x 96, 1/1 */
      ret->height       = 96;
      digit_width  = 60;
      colon_width  = 30;
      ret->font_scale   = 6;
      break;
    case BG_GTK_DISPLAY_SIZE_LARGE:  /* 240 x 48, 1/2 */
      ret->height       = 48;
      digit_width  = 30;
      colon_width  = 15;
      ret->font_scale   = 3;
      break;
    case BG_GTK_DISPLAY_SIZE_NORMAL: /* 160 x 32  1/3 */
      ret->height       = 32;
      digit_width  = 20;
      colon_width  = 10;
      ret->font_scale   = 2;
      break;
    case BG_GTK_DISPLAY_SIZE_SMALL:  /*  80 x 16  1/6 */
      ret->height       = 16;
      digit_width  = 10;
      colon_width  = 5;
      ret->font_scale   = 1;
      break;

    }
  ret->foreground_color[0] = 0.0;
  ret->foreground_color[1] = 1.0;
  ret->foreground_color[2] = 0.0;

  ret->background_color[0] = 0.0;
  ret->background_color[1] = 0.0;
  ret->background_color[2] = 0.0;

  ret->widget = gtk_drawing_area_new();

  g_signal_connect(G_OBJECT(ret->widget), "draw",
                   G_CALLBACK(expose_callback), (gpointer)ret);

  gtk_widget_set_events(ret->widget,
                        GDK_EXPOSURE_MASK |
                        GDK_ENTER_NOTIFY_MASK |
                        GDK_LEAVE_NOTIFY_MASK);
  
  ret->max_width = 2 * ret->border_width;
  
  if(ret->type_mask & BG_GTK_DISPLAY_MODE_HMSMS)
    { // -000:00:00.000
    ret->max_width += 3 * colon_width + 10 * digit_width;
    }
  else if(ret->type_mask & BG_GTK_DISPLAY_MODE_TIMECODE)
    { // -00:00:00:00
    ret->max_width += 3 * colon_width + 9 * digit_width;
    }
  else
    { // -000:00:00
    ret->max_width += 2 * colon_width + 7 * digit_width;
    }
  
  gtk_widget_set_size_request(ret->widget,
                              ret->max_width,
                              2 * ret->border_width + ret->height);
  
  gtk_widget_show(ret->widget);
  bg_gtk_time_display_set_font(ret, "Sans 10");
  
  return ret;
  }

GtkWidget * bg_gtk_time_display_get_widget(bg_gtk_time_display_t * d)
  {
  return d->widget;
  }

void bg_gtk_time_display_destroy(bg_gtk_time_display_t * d)
  {
  if(d->font_desc)
    pango_font_description_free(d->font_desc);

  free(d);
  }
