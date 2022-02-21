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
#include <gtk/gtk.h>

#include <gmerlin/cfg_registry.h>
#include <gui_gtk/button.h>
#include <gui_gtk/gtkutils.h>
#include <gmerlin/utils.h>
#include <gmerlin/xmlutils.h>

struct bg_gtk_button_s
  {
  int x, y; /* Coordinates relative to parent window */
    
  /* Pixbufs */
  
  cairo_surface_t * pixbuf_normal;
  cairo_surface_t * pixbuf_highlight;
  cairo_surface_t * pixbuf_pressed;

  /* Widget */
    
  GtkWidget * widget;
  
  /* Callback stuff */
    
  void (*callback)(bg_gtk_button_t*, void*);
  void * callback_data;

  void (*callback_2)(bg_gtk_button_t*, void*);
  void * callback_2_data;
  
  GtkWidget * menu;

  int mouse_inside;
  };

static gboolean button_press_callback(GtkWidget * w, GdkEventButton * evt,
                                      gpointer data)
  {
  bg_gtk_button_t * b;
  b = (bg_gtk_button_t *)data;

  bg_gtk_set_widget_bg_pixmap(b->widget, b->pixbuf_pressed);

  // fprintf(stderr, "Button press\n");
  
  if(b->menu)
    {
    gtk_menu_popup_at_pointer(GTK_MENU(b->menu), (GdkEvent*)evt);
    }
  
  return TRUE;
  }

//static void 

static gboolean button_release_callback(GtkWidget * w, GdkEventButton * evt,
                                    gpointer data)
  {
  bg_gtk_button_t * b;
  b = (bg_gtk_button_t *)data;

  //  fprintf(stderr, "Button release\n");

  if(b->mouse_inside)
    {
    bg_gtk_set_widget_bg_pixmap(b->widget, b->pixbuf_highlight);
    if(b->callback_2 && (evt->button == 3))
      b->callback_2(b, b->callback_2_data);
    else if(b->callback)
      b->callback(b, b->callback_data);
    }
  return TRUE;
  }

static gboolean enter_notify_callback(GtkWidget *widget,
                                      GdkEventCrossing *event,
                                      gpointer data)
  {
  bg_gtk_button_t * b;
  b = (bg_gtk_button_t *)data;
  bg_gtk_set_widget_bg_pixmap(b->widget, b->pixbuf_highlight);
  b->mouse_inside = 1;
  return TRUE;
  }

static gboolean leave_notify_callback(GtkWidget *widget,
                                      GdkEventCrossing *event,
                                      gpointer data)
  {
  bg_gtk_button_t * b;
  b = (bg_gtk_button_t *)data;
  bg_gtk_set_widget_bg_pixmap(b->widget, b->pixbuf_normal);
  b->mouse_inside = 0;
  return TRUE;
  }

static gboolean motion_callback(GtkWidget * w, GdkEventMotion * evt,
                                gpointer data)
  {
  return TRUE; // Prevent propagation
  }

static void set_shape(bg_gtk_button_t * b)
  {
  int width, height;
  cairo_region_t * mask = NULL;

  width = cairo_image_surface_get_width(b->pixbuf_normal);
  height = cairo_image_surface_get_height(b->pixbuf_normal);
  
  gtk_widget_set_size_request(b->widget, width, height);
  mask = gdk_cairo_region_create_from_surface(b->pixbuf_normal);
    
  if(mask)
    {
    gtk_widget_shape_combine_region(b->widget, mask);
    cairo_region_destroy(mask);
    }
  
  }

static void realize_callback(GtkWidget * w,
                             gpointer data)
  {
  bg_gtk_button_t * b = data;
  
  if(b->pixbuf_normal)
    set_shape(b);
  }

bg_gtk_button_t * bg_gtk_button_create()
  {
  bg_gtk_button_t * ret;

  ret = calloc(1, sizeof(*ret));

  /* Create objects */
  
  ret->widget = gtk_drawing_area_new();

  /* Set attributes */

  gtk_widget_add_events(ret->widget,
                        GDK_EXPOSURE_MASK |
                        GDK_BUTTON_PRESS_MASK |
                        GDK_BUTTON_RELEASE_MASK |
                        GDK_ENTER_NOTIFY_MASK |
                        GDK_LEAVE_NOTIFY_MASK |
                        GDK_BUTTON_MOTION_MASK);
  
  /* Set callbacks */

  g_signal_connect(G_OBJECT(ret->widget),
                   "button_press_event",
                   G_CALLBACK (button_press_callback),
                   ret);

  g_signal_connect(G_OBJECT(ret->widget),
                   "button_release_event",
                   G_CALLBACK (button_release_callback),
                   ret);

  g_signal_connect(G_OBJECT(ret->widget),
                   "enter_notify_event",
                   G_CALLBACK (enter_notify_callback),
                   ret);

  g_signal_connect(G_OBJECT(ret->widget),
                   "leave_notify_event",
                   G_CALLBACK (leave_notify_callback),
                   ret);
  
  g_signal_connect(G_OBJECT(ret->widget),
                   "realize",
                   G_CALLBACK (realize_callback),
                   ret);
 
  g_signal_connect(G_OBJECT(ret->widget), "motion-notify-event",
                   G_CALLBACK(motion_callback), (gpointer*)ret);
 
  /* Pack everything */
  gtk_widget_show(ret->widget);
    
  return ret;
  }

void bg_gtk_button_destroy(bg_gtk_button_t * b)
  {

  }

/* Set Attributes */

static cairo_surface_t * make_pixbuf(cairo_surface_t * old,
                                     const char * filename)
  {
  cairo_surface_t * ret;
  
  if(old)
    cairo_surface_destroy(old);

  ret = cairo_image_surface_create_from_png(filename);
  
  return ret;
  }

void bg_gtk_button_set_skin(bg_gtk_button_t * b,
                            bg_gtk_button_skin_t * s,
                            const char * directory)
  {
  char * tmp_string = NULL;
  
  b->x = s->x;
  b->y = s->y;

  
  tmp_string = bg_sprintf("%s/%s", directory, s->pixmap_normal);
  b->pixbuf_normal = make_pixbuf(b->pixbuf_normal, tmp_string);
  free(tmp_string);

  tmp_string = bg_sprintf("%s/%s", directory, s->pixmap_highlight);
  b->pixbuf_highlight = make_pixbuf(b->pixbuf_highlight, tmp_string);
  free(tmp_string);

  tmp_string = bg_sprintf("%s/%s", directory, s->pixmap_pressed);
  b->pixbuf_pressed = make_pixbuf(b->pixbuf_pressed, tmp_string);
  free(tmp_string);

  bg_gtk_set_widget_bg_pixmap(b->widget, b->pixbuf_normal);
  if(gtk_widget_get_realized(b->widget))
    set_shape(b);
  }

/* A button can have either a callback or a menu */

void bg_gtk_button_set_callback(bg_gtk_button_t * b,
                                void (*callback)(bg_gtk_button_t *,
                                                 void *),
                                void * callback_data)
  {
  b->callback      = callback;
  b->callback_data = callback_data;
  }

void bg_gtk_button_set_callback_2(bg_gtk_button_t * b,
                                  void (*callback_2)(bg_gtk_button_t *,
                                                   void *),
                                  void * callback_2_data)
  {
  b->callback_2      = callback_2;
  b->callback_2_data = callback_2_data;
  }

void bg_gtk_button_set_menu(bg_gtk_button_t * b, GtkWidget * menu)
  {
  b->menu = menu;
  }

/* Get Stuff */

void bg_gtk_button_get_coords(bg_gtk_button_t * b, int * x, int * y)
  {
  *x = b->x;
  *y = b->y;
  }

GtkWidget * bg_gtk_button_get_widget(bg_gtk_button_t * b)
  {
  return b->widget;
  }

#define FREE(ptr) if(ptr)free(ptr);

void bg_gtk_button_skin_free(bg_gtk_button_skin_t * s)
  {
  FREE(s->pixmap_normal);
  FREE(s->pixmap_highlight);
  FREE(s->pixmap_pressed);

  }

void bg_gtk_button_skin_load(bg_gtk_button_skin_t * s,
                             xmlDocPtr doc, xmlNodePtr node)
  {
  char * tmp_string;
  node = node->children;
  
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
    else if(!BG_XML_STRCMP(node->name, "NORMAL"))
      s->pixmap_normal = gavl_strrep(s->pixmap_normal, tmp_string);
    else if(!BG_XML_STRCMP(node->name, "HIGHLIGHT"))
      s->pixmap_highlight = gavl_strrep(s->pixmap_highlight, tmp_string);
    else if(!BG_XML_STRCMP(node->name, "PRESSED"))
      s->pixmap_pressed = gavl_strrep(s->pixmap_pressed, tmp_string);
    node = node->next;
    xmlFree(tmp_string);
    }
  }
