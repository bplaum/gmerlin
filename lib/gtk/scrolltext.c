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
#include <stdio.h>
#include <string.h>

#include <gtk/gtk.h>
#include <gui_gtk/scrolltext.h>
#include <gui_gtk/gtkutils.h>

#include <gmerlin/utils.h>
#include <gmerlin/bgcairo.h>

#define TIMEOUT_INTERVAL 30
#define SCROLL_ADVANCE   1

struct bg_gtk_scrolltext_s
  {
  int width;
  int height;
  int offset;

  int text_width;
  
  int is_realized;
  char * text;
  float foreground_color[3];
  float background_color[3];

  int do_scroll;
  gulong timeout_tag;
    
  PangoFontDescription * font_desc;
    
  GtkWidget * drawingarea;

  cairo_surface_t * pixmap_string; /* Pixmap for the whole string */
  cairo_surface_t * pixmap_da; /* Pixmap for the drawingarea  */

  // GdkGC * gc;

  int pixmap_width;
  int pixmap_height;
  
  };

static void create_text_pixmap(bg_gtk_scrolltext_t * st);

static gboolean expose_callback(GtkWidget * w, cairo_t * cr,
                                gpointer data)
  {
  bg_gtk_scrolltext_t * st = data;
  if(st->pixmap_da)
    {
    cairo_set_source_surface(cr, st->pixmap_da, 0, 0);
    cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint(cr);
    }
  return TRUE;
  }

static void size_allocate_callback(GtkWidget * w, GtkAllocation * evt,
                                   gpointer data)
     //static gboolean configure_callback(GtkWidget * w, GdkEventConfigure * evt,
     //                                   gpointer data)
  {
  bg_gtk_scrolltext_t * st = (bg_gtk_scrolltext_t *)data;

  if((st->width == evt->width) && (st->height == evt->height) &&
     (st->pixmap_da))
    return;
  
  st->width = evt->width;
  st->height = evt->height;

  if(!st->is_realized)
    return;

  if(st->pixmap_da)
    {
    if((st->pixmap_width < evt->width) || (st->pixmap_height < evt->height))
      {
      /* Enlarge pixmap */
      
      st->pixmap_width  = evt->width + 10;
      st->pixmap_height = evt->height + 10;
      
      cairo_surface_destroy(st->pixmap_da);

      st->pixmap_da = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                                 st->pixmap_width,
                                                 st->pixmap_height);
      }
    /* Put pixmap_string onto pixmap_da if we won't scroll */
    if(st->width >= st->text_width)
      {
      cairo_t * cr = cairo_create(st->pixmap_da);
      bg_cairo_surface_fill_rgb(st->pixmap_da, cr, st->background_color);
      cairo_destroy(cr);
      
      if(st->pixmap_string)
        bg_cairo_img_surface_copy(st->pixmap_da,
                                  st->pixmap_string,
                                  0, 0,
                                  (st->width - st->text_width)/2, 0,
                                  st->text_width, st->height);
      }
    }
  else
    {
    st->pixmap_width  = evt->width + 10;
    st->pixmap_height = evt->height + 10;

    st->pixmap_da = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                               st->pixmap_width,
                                               st->pixmap_height);
    if(st->text)
      create_text_pixmap(st);
    }
  
  if(((st->width < st->text_width) && !st->do_scroll) ||
     ((st->width >= st->text_width) && st->do_scroll) ||
     !st->pixmap_string)
    {
    create_text_pixmap(st);
    }
  
  //  expose_callback(w, NULL, data);
  return;
  }

static gboolean timeout_func(gpointer data)
  {
  bg_gtk_scrolltext_t * st = (bg_gtk_scrolltext_t*)data;
  
  if(!st->do_scroll)
    return FALSE;
  
  st->offset += SCROLL_ADVANCE;

  if(st->offset > st->text_width)
    st->offset = 0;

  if(st->text_width - st->offset < st->width)
    {
    bg_cairo_img_surface_copy(st->pixmap_da, st->pixmap_string,
                              st->offset, 0, 0, 0,
                              st->text_width - st->offset,
                              st->height);

    bg_cairo_img_surface_copy(st->pixmap_da, st->pixmap_string,
                              0, 0, st->text_width - st->offset, 0,
                              st->width - (st->text_width - st->offset), st->height);
    }
  else
    {
    bg_cairo_img_surface_copy(st->pixmap_da, st->pixmap_string,
                              st->offset, 0, 0, 0, st->width, st->height);
    }
  bg_gtk_widget_queue_redraw(st->drawingarea);
  return TRUE;
  }


static void create_text_pixmap(bg_gtk_scrolltext_t * st)
  {
  char * tmp_string = NULL;
  PangoLayout * layout;
  cairo_t * cr;
  
  PangoRectangle logical_rect;
  
  int height;

  if(!st->is_realized || !st->width || !st->height || !st->text)
    return;
  
  /* Create pango layout */
  
  layout = gtk_widget_create_pango_layout(st->drawingarea,
                                          st->text);

  if(st->font_desc)
    pango_layout_set_font_description(layout,
                                      st->font_desc);

  /* Set Colors */

  //  set_color(st, st->foreground_color, &fg);
  //  set_color(st, st->background_color, &bg);

  /* Remove previous timeout callbacks */

  if(st->do_scroll)
    {
    g_source_remove(st->timeout_tag);
    st->do_scroll = 0;
    st->timeout_tag = 0;
    }
  
  /* Set up pixmap */

  bg_gtk_get_text_extents(st->font_desc, st->text, &logical_rect);
  
  st->text_width =  logical_rect.width  / PANGO_SCALE;
  height = logical_rect.height / PANGO_SCALE;

  /* Change string if necessary */

  if(st->text_width > st->width)
    {
    st->do_scroll = 1;
    tmp_string = bg_sprintf("%s * * * ", st->text);
    
    bg_gtk_get_text_extents(st->font_desc, tmp_string, &logical_rect);
    
    st->text_width = logical_rect.width  / PANGO_SCALE;
    height         = logical_rect.height / PANGO_SCALE;
    }
  else
    st->do_scroll = 0;
  
  /* Set up Pixmap */
  
  if(st->pixmap_string)
    cairo_surface_destroy(st->pixmap_string);
  
  st->pixmap_string = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, st->text_width, st->height);
  cr = cairo_create(st->pixmap_string);
  
  bg_cairo_surface_fill_rgb(st->pixmap_string, cr, st->background_color);  

  layout = pango_cairo_create_layout(cr);
  
  if(st->font_desc)
    pango_layout_set_font_description(layout, st->font_desc);

  if(tmp_string)
    pango_layout_set_text(layout, tmp_string, -1);
  else
    pango_layout_set_text(layout, st->text, -1);

  cairo_set_source_rgb(cr, st->foreground_color[0], st->foreground_color[1], st->foreground_color[2]);
  //  cairo_move_to(cr, 0.0, (double)(st->height - height)*0.5/st->height);
  cairo_move_to(cr, 0.0, (double)(st->height - height)*0.5);
  pango_cairo_show_layout(cr, layout);
  
  g_object_unref(layout);
  cairo_destroy(cr);
  if(tmp_string)
    free(tmp_string);

  /* */
  
  if(!st->do_scroll)
    {
    bg_cairo_surface_fill_rgb(st->pixmap_da, NULL, st->background_color);
    bg_cairo_img_surface_copy(st->pixmap_da,
                              st->pixmap_string,
                              0,
                              0,
                              (st->width - st->text_width)/2,
                              0,
                              st->text_width, st->height);
    }
  else
    {
    st->timeout_tag = g_timeout_add(TIMEOUT_INTERVAL,
                                    timeout_func,
                                    st);
    }
  bg_gtk_widget_queue_redraw(st->drawingarea);
  }

static void realize_callback(GtkWidget * w, gpointer data)
  {
  bg_gtk_scrolltext_t * st = (bg_gtk_scrolltext_t *)data;
  st->is_realized = 1;

  if(!st->pixmap_da)
    {
    GtkAllocation a;
    a.width  = st->width;
    a.height = st->height;
    size_allocate_callback(w, &a, data);
    }
  
  }

bg_gtk_scrolltext_t * bg_gtk_scrolltext_create(int width, int height)
  {
  bg_gtk_scrolltext_t * ret;

  ret = calloc(1, sizeof(*ret));

  ret->drawingarea = gtk_drawing_area_new();

  if((width >= 0) && (height >= 0))
    {
    //    ret->width = width;
    //    ret->height = height;
    gtk_widget_set_size_request(ret->drawingarea,
                                width, height);
    }
  else
    {
    gtk_widget_set_size_request(ret->drawingarea, 16, 16);
    }
  
  g_signal_connect(G_OBJECT(ret->drawingarea),
                   "realize", G_CALLBACK(realize_callback),
                   ret);
  g_signal_connect(G_OBJECT(ret->drawingarea),
                   "draw", G_CALLBACK(expose_callback),
                   ret);

  //  g_signal_connect(G_OBJECT(ret->drawingarea),
  //                   "configure-event", G_CALLBACK(configure_callback),
  //                   ret);

  g_signal_connect(G_OBJECT(ret->drawingarea),
                   "size-allocate", G_CALLBACK(size_allocate_callback),
                   ret);
  
  gtk_widget_show(ret->drawingarea);
  
  return ret;
  }

void bg_gtk_scrolltext_set_text(bg_gtk_scrolltext_t * d, const char * text,
                                const float * foreground_color,
                                const float * background_color)
  {
  d->text = gavl_strrep(d->text, text);

  memcpy(d->foreground_color, foreground_color, 3 * sizeof(float));
  memcpy(d->background_color, background_color, 3 * sizeof(float));
  create_text_pixmap(d);
  }

void bg_gtk_scrolltext_set_colors(bg_gtk_scrolltext_t * d,
                                  const float * fg_color, const float * bg_color)
  {
  memcpy(d->foreground_color, fg_color, 3 * sizeof(float));
  memcpy(d->background_color, bg_color, 3 * sizeof(float));
  create_text_pixmap(d);
  }

void bg_gtk_scrolltext_set_font(bg_gtk_scrolltext_t * d, const char * font)
  {
  if(d->font_desc)
    pango_font_description_free(d->font_desc);
  d->font_desc = pango_font_description_from_string(font);
  }

void bg_gtk_scrolltext_destroy(bg_gtk_scrolltext_t * d)
  {
  if(d->timeout_tag)
    g_source_remove(d->timeout_tag);
  if(d->font_desc)
    pango_font_description_free(d->font_desc);
  if(d->text)
    free(d->text);

  if(d->pixmap_string)
    cairo_surface_destroy(d->pixmap_string);
  if(d->pixmap_da)
    cairo_surface_destroy(d->pixmap_da);
  free(d);
  }

GtkWidget * bg_gtk_scrolltext_get_widget(bg_gtk_scrolltext_t * d)
  {
  return d->drawingarea;
  }
