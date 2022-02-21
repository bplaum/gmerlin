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

#include <gui_gtk/slider.h>
#include <gui_gtk/gtkutils.h>

#include <gmerlin/utils.h>
#include <gmerlin/xmlutils.h>
#include <gmerlin/bgcairo.h>

#define USE_GTK_FIXED

#ifdef USE_GTK_FIXED
#define MOVE(layout, child, x, y) gtk_fixed_move(GTK_FIXED(layout), child, x, y)
#else
#define MOVE(layout, child, x, y) gtk_layout_move(GTK_LAYOUT(layout), child, x, y)
#endif



struct bg_gtk_slider_s
  {
  cairo_surface_t * pixbuf_background;
  cairo_surface_t * pixbuf_background_l;
  cairo_surface_t * pixbuf_background_r;
  cairo_surface_t * pixbuf_background_total;
  
  cairo_surface_t * pixbuf_normal;
  cairo_surface_t * pixbuf_highlight;
  cairo_surface_t * pixbuf_pressed;
  cairo_surface_t * pixbuf_inactive;
  int x, y;
  int width, height;
  int vertical;
  
  /* State information */

  int action;
  int mouse_inside;
  bg_gtk_slider_state_t state;
  int mouse_root;
  
  int total_size;
  int slider_size;
  
  int pos;
    
  void (*change_callback)(bg_gtk_slider_t*, float, void*);
  void * change_callback_data;
  
  void (*release_callback)(bg_gtk_slider_t*, float, void*);
  void * release_callback_data;

  void (*scroll_callback)(bg_gtk_slider_t*, int up, void*);
  void * scroll_callback_data;
  
  /* Widget stuff */

  GtkWidget * background_layout;
  GtkWidget * slider_eventbox;

  // GtkWidget * slider_image;
  };

/* Callbacks */

static gboolean
button_press_callback(GtkWidget * w, GdkEventButton * evt,
                      gpointer data)
  {
  bg_gtk_slider_t * s;
  s = (bg_gtk_slider_t *)data;

  if(s->state != BG_GTK_SLIDER_ACTIVE)
    return TRUE;

  bg_gtk_set_widget_bg_pixmap(s->slider_eventbox, s->pixbuf_pressed);
    
  if(s->vertical)
    s->mouse_root = (int)(evt->y_root);
  else
    s->mouse_root = (int)(evt->x_root);
  
  s->action = 1;

  return TRUE;
  }

//static void 

static gboolean
button_release_callback(GtkWidget * w, GdkEventButton * evt,
                        gpointer data)
  {
  bg_gtk_slider_t * s;
  int mouse_root_1;
  s = (bg_gtk_slider_t *)data;

  if(s->state != BG_GTK_SLIDER_ACTIVE)
    return TRUE;

  if(s->mouse_inside)
    bg_gtk_set_widget_bg_pixmap(s->slider_eventbox, s->pixbuf_highlight);
  else
    bg_gtk_set_widget_bg_pixmap(s->slider_eventbox, s->pixbuf_normal);
  
  s->action = 0;
  
  if(s->release_callback)
    {
    if(s->vertical)
      mouse_root_1 = (int)(evt->y_root);
    else
      mouse_root_1 = (int)(evt->x_root);
    
    s->pos += (mouse_root_1 - s->mouse_root);
    if(s->pos > s->total_size - s->slider_size)
      s->pos = s->total_size - s->slider_size;
    else if(s->pos < 0)
      s->pos = 0;
    
    if(s->vertical)
      MOVE(s->background_layout, s->slider_eventbox, 0, s->pos);
    else
      MOVE(s->background_layout, s->slider_eventbox, s->pos, 0);
    
    if(s->vertical)
      s->release_callback(s,
                          1.0 - (float)(s->pos)/(float)(s->total_size -
                                                        s->slider_size),
                          s->release_callback_data);
    else
      s->release_callback(s,
                          (float)(s->pos)/(float)(s->total_size -
                                                  s->slider_size),
                          s->release_callback_data);
    }

  return TRUE;
  }

static gboolean
scroll_callback(GtkWidget * w, GdkEvent * evt,
                gpointer data)
  {
  gdouble delta_x;
  gdouble delta_y;
  GdkScrollDirection dir;
  bg_gtk_slider_t * s = data;
  
  if((s->state != BG_GTK_SLIDER_ACTIVE) || !s->scroll_callback)
    return FALSE;

  gdk_event_get_scroll_deltas(evt, &delta_x, &delta_y);
  
  if(gdk_event_get_scroll_direction(evt, &dir) || (delta_y == 0.0))
    return FALSE;

  if(delta_y < 0)
    dir = GDK_SCROLL_UP;
  else
    dir = GDK_SCROLL_DOWN;
  
  if((dir != GDK_SCROLL_UP) && (dir != GDK_SCROLL_DOWN))
    return FALSE;
  
  s->scroll_callback(s, (dir == GDK_SCROLL_UP),
                     s->scroll_callback_data);
  
  return FALSE;
  }

static gboolean enter_notify_callback(GtkWidget *widget,
                                      GdkEventCrossing *event,
                                      gpointer data)
  {
  bg_gtk_slider_t * s;
  s = (bg_gtk_slider_t *)data;

  if(s->state != BG_GTK_SLIDER_ACTIVE)
    return FALSE;

  s->mouse_inside = 1;
  if(!s->action)
    bg_gtk_set_widget_bg_pixmap(s->slider_eventbox, s->pixbuf_highlight);
  return FALSE;
  }

static gboolean leave_notify_callback(GtkWidget *widget,
                                      GdkEventCrossing *event,
                                      gpointer data)
  {
  bg_gtk_slider_t * s;
  s = (bg_gtk_slider_t *)data;
  if(s->state != BG_GTK_SLIDER_ACTIVE)
    return FALSE;

  s->mouse_inside = 0;
  if(!s->action)
    bg_gtk_set_widget_bg_pixmap(s->slider_eventbox, s->pixbuf_normal);
  return FALSE;
  }

static gboolean motion_callback(GtkWidget * w, GdkEventMotion * evt,
                                gpointer data)
  {
  bg_gtk_slider_t * s;
  int mouse_root_1;
  s = (bg_gtk_slider_t *)data;

  /* Buggy (newer) gtk versions send motion events even if no button
     is pressed */
  if(!(evt->state & (GDK_BUTTON1_MASK|GDK_BUTTON2_MASK|GDK_BUTTON3_MASK)))
    return TRUE;
  
  if(s->state != BG_GTK_SLIDER_ACTIVE)
    return TRUE;
  
  if(s->vertical)
    mouse_root_1 = (int)(evt->y_root);
  else
    mouse_root_1 = (int)(evt->x_root);

  s->pos += (mouse_root_1 - s->mouse_root);
  if(s->pos > s->total_size - s->slider_size)
    s->pos = s->total_size - s->slider_size;
  else if(s->pos < 0)
    s->pos = 0;

  if(s->vertical)
    {
    MOVE(s->background_layout, s->slider_eventbox, 0, s->pos);

    if(s->change_callback)
      s->change_callback(s,
                         1.0 - (float)(s->pos)/(float)(s->total_size - s->slider_size),
                         s->change_callback_data);
    }
  else
    {
    MOVE(s->background_layout, s->slider_eventbox, s->pos, 0);
    
    if(s->change_callback)
      s->change_callback(s,
                         (float)(s->pos)/(float)(s->total_size - s->slider_size),
                         s->change_callback_data);
    }
  s->mouse_root = mouse_root_1;
  return TRUE;
  }

/* Set background image. We don't do this before the
   widget is realized */

static void set_background(bg_gtk_slider_t * s)
  {
  if(!s->pixbuf_background_l ||
     !s->pixbuf_background_r)
    {
    s->width  = cairo_image_surface_get_width(s->pixbuf_background);
    s->height = cairo_image_surface_get_height(s->pixbuf_background);
    bg_gtk_set_widget_bg_pixmap(s->background_layout, s->pixbuf_background);
    }
  else
    {
    int width, height;
    
    if(!s->width || !s->height)
      return;

    if(s->pixbuf_background_total)
      cairo_surface_destroy(s->pixbuf_background_total);

    s->pixbuf_background_total =
      cairo_image_surface_create(cairo_image_surface_get_format(s->pixbuf_background),
                                 s->width,
                                 s->height);
    
    if(s->vertical)
      {
      int dest_y = 0, dest_y_end;
      width = cairo_image_surface_get_width(s->pixbuf_background);
      height = cairo_image_surface_get_height(s->pixbuf_background_l);

      /* Draw top */

      bg_cairo_img_surface_copy(s->pixbuf_background_total,
                                s->pixbuf_background_l,
                                0, // int src_x,
                                0, // int src_y,
                                0, // int dst_x,
                                dest_y, // dst_y,
                                width,
                                height);
      
      dest_y += height;
      
      dest_y_end = s->height - cairo_image_surface_get_height(s->pixbuf_background_r);
      height = cairo_image_surface_get_height(s->pixbuf_background);
      
      while(dest_y < dest_y_end)
        {
        if(dest_y + height > dest_y_end)
          height = dest_y_end - dest_y;

        bg_cairo_img_surface_copy(s->pixbuf_background_total,
                                  s->pixbuf_background,
                                  0, // int src_x,
                                  0, // int src_y,
                                  0, // int dst_x,
                                  dest_y, // dst_y,
                                  width,
                                  height);
        
        dest_y += height;
        }

      height = cairo_image_surface_get_height(s->pixbuf_background_r);
      
      bg_cairo_img_surface_copy(s->pixbuf_background_total,
                                s->pixbuf_background_r,
                                0, // int src_x,
                                0, // int src_y,
                                0, // int dst_x,
                                dest_y, // dst_y,
                                width,
                                height);
      }
    else
      {
      int dest_x = 0, dest_x_end;
      height = cairo_image_surface_get_height(s->pixbuf_background);
      width = cairo_image_surface_get_width(s->pixbuf_background_l);

      bg_cairo_img_surface_copy(s->pixbuf_background_total,
                                s->pixbuf_background_l,
                                0, // int src_x,
                                0, // int src_y,
                                dest_x, // int dst_x,
                                0, // dst_y,
                                width,
                                height);
      
      dest_x += width;

      dest_x_end = s->width - cairo_image_surface_get_width(s->pixbuf_background_r);
      width = cairo_image_surface_get_width(s->pixbuf_background);

      while(dest_x < dest_x_end)
        {
        if(dest_x + width > dest_x_end)
          width = dest_x_end - dest_x;
        
        bg_cairo_img_surface_copy(s->pixbuf_background_total,
                                  s->pixbuf_background,
                                  0, // int src_x,
                                  0, // int src_y,
                                  dest_x, // int dst_x,
                                  0, // dst_y,
                                  width,
                                  height);
        dest_x += width;
        }

      width = cairo_image_surface_get_width(s->pixbuf_background_r);

      bg_cairo_img_surface_copy(s->pixbuf_background_total,
                                s->pixbuf_background_r,
                                0, // int src_x,
                                0, // int src_y,
                                dest_x, // int dst_x,
                                0, // dst_y,
                                width,
                                height);
      }
    
    bg_gtk_set_widget_bg_pixmap(s->background_layout, s->pixbuf_background_total);
    }
  
  if(s->vertical)
    s->total_size = s->height;
  else
    s->total_size = s->width;
  
  }

static void set_shape(bg_gtk_slider_t * s)
  {
#if 1
  cairo_region_t * mask;
  mask = gdk_cairo_region_create_from_surface(s->pixbuf_normal);
//  fprintf(stderr, "set_shape %d\n", cairo_region_num_rectangles(mask));
  if(mask)
    {
    //    gtk_widget_shape_combine_region(s->slider_eventbox, mask);
    gdk_window_shape_combine_region(gtk_widget_get_window(s->slider_eventbox), mask, 0, 0);
    cairo_region_destroy(mask);
    }
//  fprintf(stderr, "set_shape: %d\n", gdk_window_is_shaped(gtk_widget_get_window(s->slider_eventbox)));
#endif
  }

static void realize_callback(GtkWidget * w,
                             gpointer data)
  {
  bg_gtk_slider_t * s;
  s = (bg_gtk_slider_t *)data;
  
  if((w == s->background_layout) && (s->pixbuf_background))
    set_background(s);
  else if((w == s->slider_eventbox) && (s->pixbuf_normal))
    set_shape(s);
  }

static void size_allocate_callback(GtkWidget * w, GtkAllocation * evt,
                                   gpointer data)
  {
  double old_pos;
  bg_gtk_slider_t * s;
  s = (bg_gtk_slider_t *)data;
  
  if((s->width == evt->width) && (s->height == evt->height))
    return;

  if(s->total_size)
    old_pos = (float)(s->pos)/(float)(s->total_size - s->slider_size);
  else
    old_pos = 0.0;
  
  s->width = evt->width;
  s->height = evt->height;
  
  if(s->pixbuf_background)
    set_background(s);

  if(s->vertical)
    s->total_size = evt->height;
  else 
    s->total_size = evt->width;

  if(old_pos != 0.0)
    bg_gtk_slider_set_pos(s, old_pos);
  }


bg_gtk_slider_t * bg_gtk_slider_create()
  {
  bg_gtk_slider_t * ret = calloc(1, sizeof(*ret));
  //  ret->vertical = vertical;

  /* Create objects */
#ifdef USE_GTK_FIXED
  ret->background_layout = gtk_fixed_new();
#else
  ret->background_layout = gtk_layout_new(NULL, NULL);
#endif
  
  ret->slider_eventbox = gtk_drawing_area_new();
  //  gtk_widget_set_double_buffered (ret->slider_eventbox, FALSE);
  
  //  ret->slider_image  = gtk_image_new_from_pixbuf(NULL);
    
  /* Set Attributes */

  gtk_widget_add_events(ret->slider_eventbox,
                        GDK_EXPOSURE_MASK|
                        GDK_BUTTON1_MOTION_MASK|
                        GDK_BUTTON2_MOTION_MASK|
                        GDK_BUTTON3_MOTION_MASK|
                        GDK_BUTTON_PRESS_MASK|
                        GDK_BUTTON_RELEASE_MASK|
                        GDK_ENTER_NOTIFY_MASK|
                        GDK_LEAVE_NOTIFY_MASK);
    
  /* Set Callbacks */

  g_signal_connect(G_OBJECT(ret->slider_eventbox),
                   "button_press_event",
                   G_CALLBACK (button_press_callback),
                   ret);

#if 0
  g_signal_connect(G_OBJECT(ret->background_layout),
                   "configure-event", G_CALLBACK(configure_callback),
                   ret);
#endif
  
  g_signal_connect(G_OBJECT(ret->background_layout),
                   "size-allocate",
                   G_CALLBACK (size_allocate_callback),
                   ret);

  g_signal_connect(G_OBJECT(ret->background_layout),
                   "scroll_event",
                   G_CALLBACK (scroll_callback),
                   ret);

  g_signal_connect(G_OBJECT(ret->slider_eventbox),
                   "scroll_event",
                   G_CALLBACK (scroll_callback),
                   ret);
  
  g_signal_connect(G_OBJECT(ret->slider_eventbox),
                   "button_release_event",
                   G_CALLBACK (button_release_callback),
                   ret);

  g_signal_connect(G_OBJECT(ret->slider_eventbox),
                   "enter_notify_event",
                   G_CALLBACK (enter_notify_callback),
                   ret);

  g_signal_connect(G_OBJECT(ret->slider_eventbox),
                   "leave_notify_event",
                   G_CALLBACK (leave_notify_callback),
                   ret);

  g_signal_connect(G_OBJECT(ret->slider_eventbox),
                   "motion_notify_event",
                   G_CALLBACK (motion_callback),
                   ret);
  
  g_signal_connect(G_OBJECT(ret->slider_eventbox),
                   "realize",
                   G_CALLBACK (realize_callback),
                   ret);

  g_signal_connect(G_OBJECT(ret->background_layout),
                   "realize",
                   G_CALLBACK (realize_callback),
                   ret);
  
  /* Pack */

  //  gtk_widget_show(ret->slider_image);
  //  gtk_container_add(GTK_CONTAINER(ret->slider_eventbox), ret->slider_image);

  //  gtk_widget_show(ret->slider_eventbox);

#ifdef USE_GTK_FIXED
  gtk_fixed_put(GTK_FIXED(ret->background_layout), ret->slider_eventbox, 0, 0);
#else
  gtk_layout_put(GTK_LAYOUT(ret->background_layout), ret->slider_eventbox, 0, 0);
#endif
  
  gtk_widget_show(ret->background_layout);

  bg_gtk_slider_set_state(ret, BG_GTK_SLIDER_ACTIVE);
  
  return ret;
  }

void bg_gtk_slider_set_state(bg_gtk_slider_t * s,
                             bg_gtk_slider_state_t state)
  {
  s->state = state;

  switch(state)
    {
    case BG_GTK_SLIDER_ACTIVE:
      if(s->mouse_inside)
        bg_gtk_set_widget_bg_pixmap(s->slider_eventbox, s->pixbuf_highlight);
      else
        bg_gtk_set_widget_bg_pixmap(s->slider_eventbox, s->pixbuf_normal);
      gtk_widget_show(s->slider_eventbox);
      break;
    case BG_GTK_SLIDER_INACTIVE:
      bg_gtk_set_widget_bg_pixmap(s->slider_eventbox, s->pixbuf_inactive);
      gtk_widget_show(s->slider_eventbox);
      break;
    case BG_GTK_SLIDER_HIDDEN:
      gtk_widget_hide(s->slider_eventbox);
      break;
    }
  }

void bg_gtk_slider_destroy(bg_gtk_slider_t * s)
  {
  cairo_surface_destroy(s->pixbuf_background);
  if(s->pixbuf_background_l)
    cairo_surface_destroy(s->pixbuf_background_l);
  if(s->pixbuf_background_r)
    cairo_surface_destroy(s->pixbuf_background_r);
    
  cairo_surface_destroy(s->pixbuf_normal);
  cairo_surface_destroy(s->pixbuf_highlight);
  cairo_surface_destroy(s->pixbuf_pressed);
  cairo_surface_destroy(s->pixbuf_inactive);
  free(s);
  }

/* Set attributes */

void bg_gtk_slider_set_change_callback(bg_gtk_slider_t * s,
                                       void (*func)(bg_gtk_slider_t *,
                                                    float perc,
                                                    void * data),
                                       void * data)
  {
  s->change_callback = func;
  s->change_callback_data = data;
  
  }

void bg_gtk_slider_set_release_callback(bg_gtk_slider_t * s,
                                        void (*func)(bg_gtk_slider_t *,
                                                     float perc,
                                                     void * data),
                                        void * data)
  {
  s->release_callback = func;
  s->release_callback_data = data;
  }

static cairo_surface_t * make_pixbuf(cairo_surface_t * old,
                                     const char * filename)
  {
  cairo_surface_t * ret;
  
  if(old)
    cairo_surface_destroy(old);
  ret =  cairo_image_surface_create_from_png(filename);
  return ret;
  }

void bg_gtk_slider_set_skin(bg_gtk_slider_t * s,
                            bg_gtk_slider_skin_t * skin,
                            const char * directory)
  {
  char * tmp_string = NULL;
  
  s->x = skin->x;
  s->y = skin->y;
  
  tmp_string = bg_sprintf("%s/%s", directory, skin->pixmap_normal);
  s->pixbuf_normal = make_pixbuf(s->pixbuf_normal, tmp_string);
  free(tmp_string);

  tmp_string = bg_sprintf("%s/%s", directory, skin->pixmap_highlight);
  s->pixbuf_highlight = make_pixbuf(s->pixbuf_highlight, tmp_string);
  free(tmp_string);

  tmp_string = bg_sprintf("%s/%s", directory, skin->pixmap_pressed);
  s->pixbuf_pressed = make_pixbuf(s->pixbuf_pressed, tmp_string);
  free(tmp_string);

  tmp_string = bg_sprintf("%s/%s", directory, skin->pixmap_inactive);
  s->pixbuf_inactive = make_pixbuf(s->pixbuf_inactive, tmp_string);
  free(tmp_string);

  tmp_string = bg_sprintf("%s/%s", directory, skin->pixmap_background);
  s->pixbuf_background = make_pixbuf(s->pixbuf_background, tmp_string);
  free(tmp_string);

  if(skin->pixmap_background_l)
    {
    tmp_string = bg_sprintf("%s/%s", directory, skin->pixmap_background_l);
    s->pixbuf_background_l = make_pixbuf(s->pixbuf_background_l, tmp_string);
    free(tmp_string);
    }
  if(skin->pixmap_background_r)
    {
    tmp_string = bg_sprintf("%s/%s", directory, skin->pixmap_background_r);
    s->pixbuf_background_r = make_pixbuf(s->pixbuf_background_r, tmp_string);
    free(tmp_string);
    }
    
  if(gtk_widget_get_realized(s->background_layout))
    set_background(s);
  if(gtk_widget_get_realized(s->slider_eventbox))
    set_shape(s);
  
  //  gtk_image_set_from_surface(GTK_IMAGE(s->slider_image), s->pixbuf_normal);
  bg_gtk_set_widget_bg_pixmap(s->slider_eventbox, s->pixbuf_normal);
  /* Determine if it's a vertical or horizontal layout */

  if(cairo_image_surface_get_width(s->pixbuf_background) ==
     cairo_image_surface_get_width(s->pixbuf_normal))
    {
    s->vertical = 1;
    s->total_size  = cairo_image_surface_get_height(s->pixbuf_background);
    s->slider_size = cairo_image_surface_get_height(s->pixbuf_normal);
    }
  else
    {
    s->vertical = 0;
    s->total_size  = cairo_image_surface_get_width(s->pixbuf_background);
    s->slider_size = cairo_image_surface_get_width(s->pixbuf_normal);
    }

  gtk_widget_set_size_request(s->slider_eventbox,
                              cairo_image_surface_get_width(s->pixbuf_normal),
                              cairo_image_surface_get_height(s->pixbuf_normal));
  
  /* Request minimum size */
  if(s->pixbuf_background_l && s->pixbuf_background_r)
    {
    if(s->vertical)
      {
      gtk_widget_set_size_request(s->background_layout,
                                  cairo_image_surface_get_width(s->pixbuf_background),
                                  cairo_image_surface_get_height(s->pixbuf_background_l)+
                                  cairo_image_surface_get_height(s->pixbuf_background_r));
      }
    else
      {
      gtk_widget_set_size_request(s->background_layout,
                                  cairo_image_surface_get_width(s->pixbuf_background_l)+
                                  cairo_image_surface_get_width(s->pixbuf_background_r),
                                  cairo_image_surface_get_height(s->pixbuf_background));
      }
    }
  else
    gtk_widget_set_size_request(s->background_layout,
                                cairo_image_surface_get_width(s->pixbuf_background),
                                cairo_image_surface_get_height(s->pixbuf_background));

  }

/* Get stuff */

void bg_gtk_slider_get_coords(bg_gtk_slider_t * s, int * x, int * y)
  {
  *x = s->x;
  *y = s->y;
  }

GtkWidget * bg_gtk_slider_get_widget(bg_gtk_slider_t * s)
  {
  return s->background_layout;  
  }

GtkWidget * bg_gtk_slider_get_slider_widget(bg_gtk_slider_t * s)
  {
  return s->slider_eventbox;
  }

void bg_gtk_slider_skin_load(bg_gtk_slider_skin_t * s,
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
    else if(!BG_XML_STRCMP(node->name, "INACTIVE"))
      s->pixmap_inactive = gavl_strrep(s->pixmap_inactive, tmp_string);
    else if(!BG_XML_STRCMP(node->name, "BACKGROUND"))
      s->pixmap_background = gavl_strrep(s->pixmap_background, tmp_string);
    else if(!BG_XML_STRCMP(node->name, "BACKGROUND_L"))
      s->pixmap_background_l = gavl_strrep(s->pixmap_background_l, tmp_string);
    else if(!BG_XML_STRCMP(node->name, "BACKGROUND_R"))
      s->pixmap_background_r = gavl_strrep(s->pixmap_background_r, tmp_string);
    node = node->next;
    xmlFree(tmp_string);
    }

  }

#define MY_FREE(ptr) if(ptr){free(ptr);ptr=NULL;}

void bg_gtk_slider_skin_free(bg_gtk_slider_skin_t * s)
  {
  MY_FREE(s->pixmap_normal);
  MY_FREE(s->pixmap_highlight);
  MY_FREE(s->pixmap_pressed);
  MY_FREE(s->pixmap_inactive);
  MY_FREE(s->pixmap_background);
  MY_FREE(s->pixmap_background_l);
  MY_FREE(s->pixmap_background_r);
  }
#undef MY_FREE

void bg_gtk_slider_set_pos(bg_gtk_slider_t * s, float position)
  {
  if(s->action)
    return;

  if(s->vertical)
    s->pos = (int)((1.0 - position) * (float)(s->total_size - s->slider_size) + 0.5);
  else
    s->pos = (int)(position * (float)(s->total_size - s->slider_size) + 0.5);
  if(s->pos < 0)
    s->pos = 0;
  else if(s->pos > s->total_size - s->slider_size)
    s->pos = s->total_size - s->slider_size;
  
  if(s->vertical)
    MOVE(s->background_layout, s->slider_eventbox, 0, s->pos);
  else
    MOVE(s->background_layout, s->slider_eventbox, s->pos, 0);
  }

void
bg_gtk_slider_set_scroll_callback(bg_gtk_slider_t * s,
                                  void (*func)(bg_gtk_slider_t*, int up, void*),
                                  void* data)
  {
  s->scroll_callback      = func;
  s->scroll_callback_data = data;
  }


