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

#include <pthread.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include <config.h>

#include <gtk/gtk.h>
#include <gui_gtk/audio.h>
#include <gui_gtk/gtkutils.h>

#include <gmerlin/utils.h>
#include <gmerlin/bgcairo.h>

#include <gavl/peakdetector.h>

/* Some constants */

#define TIC_LENGTH    7
#define METER_WIDTH  14

static const struct
  {
  float val;
  char * label;
  }
scale_tics[] =
  {
    { -60.0, "60"},
    { -50.0, "50"},
    { -40.0, "40"},
    { -30.0, "30"},
    { -20.0, "20"},
    { -10.0, "10"},
    {  -6.0,  "6"},
    {  -3.0,  "3"},
    {  -1.0,  "1"},
    {   0.0,  "0"},
  };

#define NUM_TICS sizeof(scale_tics)/sizeof(scale_tics[0])

#define LEVEL_MIN (float)(scale_tics[0].val)
#define LEVEL_MAX (float)(scale_tics[NUM_TICS-1].val)

/* Yellow color will be at the -6 dB mark */
#define YELLOW_LEVEL(len) (len*2)/3


struct bg_gtk_vumeter_s
  {
  GtkWidget * layout;
  gavl_peak_detector_t * pd;

  struct
    {
    GtkWidget * l;
    GdkRectangle coords;
    } labels[NUM_TICS];
  
  cairo_surface_t * pixbuf_on;
  cairo_surface_t * pixbuf_off;
  
  int width;
  int height;

  int min_width;
  int min_height;
  
  int pixmap_width, pixmap_height;
  
  int num_channels;
  
  int vertical;
  
  pthread_mutex_t analysis_mutex;

  /* Analysis stuff, level values are in dB */

  struct
    {
    float level;
    float peak;
    
    int64_t peak_age;
    GdkRectangle coords;
    cairo_surface_t * pixmap;
    }
    meters[GAVL_MAX_CHANNELS];
  
  int max_peak_age;
  int redraw_pixbufs;
  int pixmaps_valid;
  };

/* Golor functions */

static void get_color(int i, int len,
                      uint32_t * r, uint32_t * g, uint32_t * b)
  {
  int yellow_level = YELLOW_LEVEL(len);
  
  if(i < yellow_level)
    {
    /* Green -> Yellow */
    *r = (i * 0xff)/yellow_level; // Red
    *g = 0xff;                    // Green
    *b = 0x00;                    // Blue
    }
  else
    {
    /* Yellow -> Red */
    *r = 0xff;                    // Red
    *g = ((len - i) * 0xff)/(len - yellow_level); // Green
    *b = 0x00;                 // Blue
    }
  }

static void get_color_on(int i, int len, uint32_t * color)
  {
  uint32_t r, g, b;

  get_color(i, len, &r, &g, &b);
  
  *color =
    0xFF000000 |
    r << 16    |
    g << 8     |
    b;
  }

static void get_color_off(int i, int len, uint32_t * color)
  {
  uint32_t r, g, b;

  get_color(i, len, &r, &g, &b);

  r >>= 1;
  g >>= 1;
  b >>= 1;
  
  *color =
    0xFF000000 |
    r << 16    |
    g << 8     |
    b;
  }

static float level_2_dB(float level)
  {
  float ret;
  
  if(level == 0.0)
    return LEVEL_MIN;
  
  ret = log10(level) * 20;
  if(ret > LEVEL_MAX)
    ret = LEVEL_MAX;
  if(ret < LEVEL_MIN)
    ret = LEVEL_MIN;
  return ret;
  }

static void set_coords_horizontal(bg_gtk_vumeter_t * m, int *min_width,
                                  int *min_height)
  {
  int i;
  int meter_height;
  int max_label_w = 0;
  int max_label_h = 0;

  int total_width = 0;

  /* Get maximum size of the labels */
  for(i = 0; i < NUM_TICS; i++)
    {
    if(m->labels[i].coords.width > max_label_w)
      max_label_w = m->labels[i].coords.width;
    if(m->labels[i].coords.height > max_label_h)
      max_label_h = m->labels[i].coords.height;
    total_width += m->labels[i].coords.width;
    //    gtk_layout_move(GTK_LAYOUT(m->layout), m->labels[i].l, 100, 100);
    }
  
  /* Calculate meter coordinates */

  meter_height =
    (m->height - m->num_channels * TIC_LENGTH - max_label_h - 1)/m->num_channels;
  
  for(i = 0; i < m->num_channels; i++)
    {
    m->meters[i].coords.x     = m->labels[i].coords.width / 2;
    m->meters[i].coords.width =
      m->width - m->labels[NUM_TICS-1].coords.width / 2 -
      m->labels[i].coords.width / 2;
    }

  for(i = 0; i < m->num_channels; i++)
    {
    m->meters[i].coords.y = TIC_LENGTH + max_label_h +
      i * (meter_height + TIC_LENGTH);
    m->meters[i].coords.height = meter_height;
    }
  *min_width  = NUM_TICS * max_label_w + (NUM_TICS-1) * 5;
  *min_height = max_label_h +
    m->num_channels * (TIC_LENGTH + 10);
  }

static void set_coords_vertical(bg_gtk_vumeter_t * m, int *min_width,
                                int *min_height)
  {
  int i;
  int meter_width;
  int max_label_w = 0;
  int max_label_h = 0;

  int total_height = 0;

  /* Get maximum size of the labels */
  for(i = 0; i < NUM_TICS; i++)
    {
    if(m->labels[i].coords.width > max_label_w)
      max_label_w = m->labels[i].coords.width;
    if(m->labels[i].coords.height > max_label_h)
      max_label_h = m->labels[i].coords.height;
    total_height += m->labels[i].coords.height;
    //    gtk_layout_move(GTK_LAYOUT(m->layout), m->labels[i].l, 100, 100);
    }
  
  /* Calculate meter coordinates */

  meter_width =
    (m->width - m->num_channels * TIC_LENGTH - max_label_w - 1)/m->num_channels;
    
  for(i = 0; i < m->num_channels; i++)
    {
    m->meters[i].coords.y     = m->labels[i].coords.height / 2;
    m->meters[i].coords.height =
      m->height - m->labels[NUM_TICS-1].coords.height / 2 -
      m->labels[i].coords.height / 2;
    }

  for(i = 0; i < m->num_channels; i++)
    {
    m->meters[i].coords.x = TIC_LENGTH + max_label_w +
      i * (meter_width + TIC_LENGTH);
    m->meters[i].coords.width = meter_width;
    }
  *min_height  = NUM_TICS * max_label_h + (NUM_TICS-1) * 5;
  *min_width = max_label_w +
    m->num_channels * (TIC_LENGTH + 10);
  }

static void set_coords(bg_gtk_vumeter_t * m)
  {
  int min_width, min_height;
  if(m->vertical)
    set_coords_vertical(m, &min_width, &min_height);
  else
    set_coords_horizontal(m, &min_width, &min_height);

  if((m->min_width != min_width) ||
     (m->min_height != min_height))
    {
    m->min_width = min_width;
    m->min_height = min_height;
    gtk_widget_set_size_request(m->layout, min_width, min_height);
    }
     
  }

static void draw_static_horizontal(bg_gtk_vumeter_t * m, cairo_t * cr)
  {
  int i, j;
  int label_x, label_y;
  GdkRGBA color;
  GtkStyleContext *context = gtk_widget_get_style_context(m->layout);

  gtk_style_context_get_color(context,
                              GTK_STATE_FLAG_NORMAL,
                              &color);
  
  cairo_set_source_rgb(cr, color.red, color.green, color.blue);
  cairo_set_line_width(cr, 1);
  
  /* Print meter shadows */
  for(i = 0; i < m->num_channels; i++)
    {
    cairo_rectangle(cr, m->meters[i].coords.x-1, m->meters[i].coords.y-1,
                    m->meters[i].coords.width+2, m->meters[i].coords.height+2);
    cairo_stroke(cr);
    }

  label_y = 0;
  
  for(i = 0; i < NUM_TICS; i++)
    {
    label_x =
      m->meters[0].coords.x + (m->meters[0].coords.width * i)/(NUM_TICS-1);
    
    if((m->labels[i].coords.x != label_x - m->labels[i].coords.width/2) ||
       (m->labels[i].coords.y != label_y))
      {
      m->labels[i].coords.x = label_x - m->labels[i].coords.width/2;
      m->labels[i].coords.y = label_y;
      gtk_layout_move(GTK_LAYOUT(m->layout),
                      m->labels[i].l,
                      m->labels[i].coords.x,
                      m->labels[i].coords.y);
      }

    for(j = 0; j < m->num_channels; j++)
      {
      bg_cairo_draw_vline(cr,
                          m->meters[j].coords.y - TIC_LENGTH,
                          m->meters[j].coords.y,
                          label_x);
      }
    }

  }

static void draw_static_vertical(bg_gtk_vumeter_t * m, cairo_t * cr)
  {
  int i, j;
  int label_x, label_y;
  GdkRGBA color;
  GtkStyleContext *context = gtk_widget_get_style_context(m->layout);

  gtk_style_context_get_color(context,
                              GTK_STATE_FLAG_NORMAL,
                              &color);
  
  cairo_set_source_rgb(cr, color.red, color.green, color.blue);
  cairo_set_line_width(cr, 1);
  
  /* Print meter shadows */
  for(i = 0; i < m->num_channels; i++)
    {
    cairo_rectangle(cr, m->meters[i].coords.x-1, m->meters[i].coords.y-1,
                    m->meters[i].coords.width+2, m->meters[i].coords.height+2);
    cairo_stroke(cr);
    }

  label_x = 0;
  
  for(i = 0; i < NUM_TICS; i++)
    {
    label_y =
      m->meters[0].coords.y + (m->meters[0].coords.height * i)/(NUM_TICS-1);
            
    if((m->labels[i].coords.x != label_x) ||
       (m->labels[i].coords.y != label_y - m->labels[i].coords.height/2))
      {
      m->labels[i].coords.x = label_x;
      m->labels[i].coords.y = label_y - m->labels[i].coords.height/2;
      gtk_layout_move(GTK_LAYOUT(m->layout),
                      m->labels[i].l,
                      m->labels[i].coords.x,
                      m->labels[i].coords.y);
      }

    for(j = 0; j < m->num_channels; j++)
      {
      bg_cairo_draw_hline(cr,
                          m->meters[j].coords.x - TIC_LENGTH,
                          m->meters[j].coords.x,
                          label_y);
      }
    }
  
  }

static void draw_static(bg_gtk_vumeter_t * m, cairo_t * cr)
  {
  if(m->vertical)
    draw_static_vertical(m, cr);
  else
    draw_static_horizontal(m, cr);
  }

static int interpolate_tics(float val, int len)
  {
  int i;
  int pos_1, pos_2;
  
  if(val <= scale_tics[0].val)
    return 0;
  if(val >= scale_tics[NUM_TICS-1].val)
    return len;

  for(i = 1; i < NUM_TICS; i++)
    {
    if(scale_tics[i].val > val)
      {
      pos_1 = (len * (i-1)) / (NUM_TICS-1);
      pos_2 = (len * i) / (NUM_TICS-1);
      return pos_1 +
        (int)((pos_2 - pos_1) * (val - scale_tics[i-1].val)/
              (scale_tics[i].val - scale_tics[i-1].val)+0.5);
      }
    }
  return 0;
  }

static int level_2_pos_horizontal(bg_gtk_vumeter_t * m, float level)
  {
  return interpolate_tics(level_2_dB(level),
                          m->meters[0].coords.width);
  }

static int level_2_pos_vertical(bg_gtk_vumeter_t * m, float level)
  {
  return interpolate_tics(level_2_dB(level),
                          m->meters[0].coords.height);
  }

static void draw_pixbufs_horizontal(bg_gtk_vumeter_t * m)
  {
  int i, j;
  uint32_t on_color;
  uint32_t off_color;

  uint32_t * on_pixels;
  uint32_t * off_pixels;

  uint32_t * on_ptr;
  uint32_t * off_ptr;
  
  int on_stride;

  on_pixels = (uint32_t*)cairo_image_surface_get_data(m->pixbuf_on);
  off_pixels = (uint32_t*)cairo_image_surface_get_data(m->pixbuf_off);
  
  on_stride  = cairo_image_surface_get_stride(m->pixbuf_on) / 4;
  
  for(i = 0; i < m->meters[0].coords.width; i++)
    {
    get_color_on(i, m->meters[0].coords.width, &on_color);
    get_color_off(i, m->meters[0].coords.width, &off_color);
    
    on_ptr  = on_pixels  + i;
    off_ptr = off_pixels + i;
    
    for(j = 0; j < m->meters[0].coords.height; j++)
      {
      *on_ptr = on_color;
      *off_ptr = off_color;
      
      on_ptr += on_stride;
      off_ptr += on_stride;
      }
    }
  }

static void draw_pixbufs_vertical(bg_gtk_vumeter_t * m)
  {
  int i, j;
  uint32_t on_color;
  uint32_t off_color;
  
  uint32_t * on_pixels;
  uint32_t * off_pixels;

  uint32_t * on_ptr;
  uint32_t * off_ptr;
  
  int on_stride;

  on_pixels = (uint32_t*)cairo_image_surface_get_data(m->pixbuf_on);
  off_pixels = (uint32_t*)cairo_image_surface_get_data(m->pixbuf_off);

  on_stride  = cairo_image_surface_get_stride(m->pixbuf_on) / 4;
  
  for(i = 0; i < m->meters[0].coords.height; i++)
    {
    get_color_on(m->meters[0].coords.height - 1 - i,
                 m->meters[0].coords.height, &on_color);
    get_color_off(m->meters[0].coords.height - 1 - i,
                  m->meters[0].coords.height, &off_color);
    
    on_ptr = on_pixels + on_stride * i;
    off_ptr = off_pixels + on_stride * i;
    
    for(j = 0; j < m->meters[0].coords.width; j++)
      {
      *on_ptr  = on_color;
      *off_ptr = off_color;
      on_ptr++;
      off_ptr++;
      }
    
    }
  
  }

static void update_pixmap_horizontal(bg_gtk_vumeter_t * m, int channel)
  {
  int level_pos, peak_pos;
  
  level_pos = level_2_pos_horizontal(m, m->meters[channel].level);
  peak_pos = level_2_pos_horizontal(m, m->meters[channel].peak);
  
  if(level_pos)
    {
    bg_cairo_img_surface_copy(m->meters[channel].pixmap,
                              m->pixbuf_on,
                              0, /* gint src_x */
                              0, /* gint src_y */
                              0, /* gint dest_x */
                              0, /* gint dest_y */
                              level_pos, /* gint width */
                              m->meters[channel].coords.height);  /* gint height */
    }
  
  bg_cairo_img_surface_copy(m->meters[channel].pixmap,
                            m->pixbuf_off,
                            level_pos, /* gint src_x */
                            0, /* gint src_y */
                            level_pos, /* gint dest_x */
                            0, /* gint dest_y */
                            m->meters[channel].coords.width - level_pos, /* gint width */
                            m->meters[channel].coords.height);  /* gint height */

  if(peak_pos + 2 >= m->meters[channel].coords.width)
    peak_pos = m->meters[channel].coords.width - 2;
  
  if(peak_pos)
    {
    bg_cairo_img_surface_copy(m->meters[channel].pixmap,
                              m->pixbuf_on,
                              peak_pos, /* gint src_x */
                              0, /* gint src_y */
                              peak_pos, /* gint dest_x */
                              0, /* gint dest_y */
                              2, /* gint width */
                              m->meters[channel].coords.height);  /* gint height */
    }
  
  }

static void update_pixmap_vertical(bg_gtk_vumeter_t * m, int channel)
  {
  int level_pos, peak_pos;
  
  level_pos = level_2_pos_vertical(m, m->meters[channel].level);
  peak_pos = level_2_pos_vertical(m, m->meters[channel].peak);
#if 1
  
  if(level_pos)
    {
    bg_cairo_img_surface_copy(m->meters[channel].pixmap,
                              m->pixbuf_on,
                              0, /* gint src_x */
                              m->meters[channel].coords.height - level_pos, /* gint src_y */
                              0, /* gint dest_x */
                              m->meters[channel].coords.height - level_pos, /* gint dest_y */
                              m->meters[channel].coords.width, /* gint width */
                              level_pos);  /* gint height */
    }
#endif
  
  bg_cairo_img_surface_copy(m->meters[channel].pixmap,
                            m->pixbuf_off,
                            0, /* gint src_x */
                            0, /* gint src_y */
                            0, /* gint dest_x */
                            0, /* gint dest_y */
                            m->meters[channel].coords.width, /* gint width */
                            m->meters[channel].coords.height - level_pos);  /* gint height */
#if 1
  if(peak_pos + 2 >= m->meters[channel].coords.height)
    peak_pos = m->meters[channel].coords.height - 2;
  
  if(peak_pos)
    {
    bg_cairo_img_surface_copy(m->meters[channel].pixmap,
                              m->pixbuf_on,
                              0, /* gint src_x */
                              m->meters[channel].coords.height - peak_pos, /* gint src_y */
                              0, /* gint dest_x */
                              m->meters[channel].coords.height - peak_pos, /* gint dest_y */
                              m->meters[channel].coords.width, /* gint width */
                              2);  /* gint height */
    }
#endif  
  
  }

static void update_pixmaps(bg_gtk_vumeter_t * m)
  {
  int i;
  
  if(!m->pixbuf_on)
    {
    m->pixbuf_on = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                              m->pixmap_width,
                                              m->pixmap_height);
    m->pixbuf_off = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                               m->pixmap_width,
                                               m->pixmap_height);
    if(m->vertical)
      draw_pixbufs_vertical(m);
    else
      draw_pixbufs_horizontal(m);
    m->redraw_pixbufs = 0;
    }
  else if(m->redraw_pixbufs)
    {
    if(m->vertical)
      draw_pixbufs_vertical(m);
    else
      draw_pixbufs_horizontal(m);
    m->redraw_pixbufs = 0;
    }

  for(i = 0; i < m->num_channels; i++)
    {
    if(!m->meters[i].pixmap)
      {
      m->meters[i].pixmap =
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                   m->pixmap_width,
                                   m->pixmap_height);
      }
    }
  if(m->vertical)
    {
    for(i = 0; i < m->num_channels; i++)
      update_pixmap_vertical(m, i);
    }
  else
    {
    for(i = 0; i < m->num_channels; i++)
      update_pixmap_horizontal(m, i);
    }
  m->pixmaps_valid = 1;
  }

static void draw_dynamic(bg_gtk_vumeter_t * m, cairo_t * cr1)
  {
  GdkDrawingContext * ctx = NULL;
  cairo_t * cr;
  int i;
  cairo_region_t * region = NULL;
  GdkWindow* window;
  
  if(!bg_gtk_widget_is_realized(m->layout) ||
     !m->pixmap_width || !m->pixmap_height)
    return;
  
  window = gtk_widget_get_window(m->layout);
  
  if(!cr1)
    {
    region = cairo_region_create();
    ctx = gdk_window_begin_draw_frame(window,
                                      region);
    
    cr = gdk_drawing_context_get_cairo_context(ctx);
    //    cr = gdk_cairo_create();
    
    }
  else
    cr = cr1;
  
  if(!m->pixmaps_valid)
    update_pixmaps(m);

  for(i = 0; i < m->num_channels; i++)
    {
    bg_cairo_paint_image(cr, m->meters[i].pixmap,
                         m->meters[i].coords.x,
                         m->meters[i].coords.y,
                         m->meters[i].coords.width,
                         m->meters[i].coords.height);
    }
    
  if(ctx)
    gdk_window_end_draw_frame(window,ctx);
  
  if(region)
    cairo_region_destroy(region);
  
  }

static void flash(bg_gtk_vumeter_t * m)
  {
  
  }

static gboolean expose_callback(GtkWidget * w, cairo_t * cr,
                                gpointer data)
  {
  bg_gtk_vumeter_t * m = (bg_gtk_vumeter_t *)data;
  draw_static(m, cr);
  draw_dynamic(m, cr);
  return FALSE;
  }


static void label_size_allocate_callback(GtkWidget * w,
                                         GdkRectangle *requisition,
                                         gpointer data)
  {
  bg_gtk_vumeter_t * v;
  int i; 
  v = (bg_gtk_vumeter_t *)data;

  for(i = 0; i < NUM_TICS; i++)
    {
    if(v->labels[i].l == w)
      {
      v->labels[i].coords.width  = requisition->width;
      v->labels[i].coords.height = requisition->height;
      }
    }
  }


static void size_allocate_callback(GtkWidget * w,
                                   GtkAllocation * allocation,
                                   gpointer data)
  {
  int i;
  bg_gtk_vumeter_t * v;
  v = (bg_gtk_vumeter_t *)data;
  v->width  = allocation->width;
  v->height = allocation->height;
  
  //  gtk_layout_move(

  gtk_layout_set_size(GTK_LAYOUT(v->layout), allocation->width, allocation->height);
  set_coords(v);

  if((v->meters[0].coords.width > v->pixmap_width) || 
     (v->meters[0].coords.height > v->pixmap_height))
    {
    /* Check if the pixmaps must be enlarged */

    v->pixmap_width = (v->meters[0].coords.width > v->pixmap_width) ?
      v->meters[0].coords.width + 128 : v->pixmap_width;
    v->pixmap_height = (v->meters[0].coords.height > v->pixmap_height) ?
      v->meters[0].coords.height + 128 : v->pixmap_height;

    if(v->pixbuf_on)
      {
      cairo_surface_destroy(v->pixbuf_on);
      v->pixbuf_on = NULL;
      }
    if(v->pixbuf_off)
      {
      cairo_surface_destroy(v->pixbuf_off);
      v->pixbuf_off = NULL;
      }
    
    for(i = 0; i < GAVL_MAX_CHANNELS; i++)
      {
      if(!v->meters[i].pixmap)
        break;
      cairo_surface_destroy(v->meters[i].pixmap);
      v->meters[i].pixmap = NULL;
      }
    v->pixmaps_valid = 0;
    }
  else
    {
    v->redraw_pixbufs = 1;
    v->pixmaps_valid = 0;
    }
  }

bg_gtk_vumeter_t *
bg_gtk_vumeter_create(int num_channels, int vertical)
  {
  int i;
  
  bg_gtk_vumeter_t * ret = calloc(1, sizeof(*ret));
  ret->num_channels = num_channels;

  ret->layout = gtk_layout_new(NULL, NULL);
  
  ret->vertical = vertical;
  
  gtk_widget_set_events(ret->layout, GDK_EXPOSURE_MASK);
  
  g_signal_connect(G_OBJECT(ret->layout), "draw",
                   G_CALLBACK(expose_callback), (gpointer)ret);
  g_signal_connect(G_OBJECT(ret->layout), "size-allocate",
                   G_CALLBACK(size_allocate_callback), (gpointer)ret);
  
  gtk_widget_show(ret->layout);

  if(vertical)
    gtk_layout_set_size(GTK_LAYOUT(ret->layout), 40, 100);
  else
    gtk_layout_set_size(GTK_LAYOUT(ret->layout), 100, 40);
    
  
  /* Create labels */
  for(i = 0; i < NUM_TICS; i++)
    {
    if(vertical) /* Swap y */
      ret->labels[i].l = gtk_label_new(scale_tics[NUM_TICS - 1 - i].label);
    else
      ret->labels[i].l = gtk_label_new(scale_tics[i].label);
    
    
    g_signal_connect(G_OBJECT(ret->labels[i].l),
                     "size-allocate", G_CALLBACK(label_size_allocate_callback),
                     (gpointer)ret);
    gtk_widget_show(ret->labels[i].l);

    gtk_layout_put(GTK_LAYOUT(ret->layout), ret->labels[i].l, 0, 0);
    }
  
  gtk_widget_show(ret->layout);
  
  ret->pd = gavl_peak_detector_create();
  
  ret->max_peak_age = 44100;
  
  pthread_mutex_init(&ret->analysis_mutex, NULL);
  return ret;
  }

GtkWidget *
bg_gtk_vumeter_get_widget(bg_gtk_vumeter_t * m)
  {
  return m->layout;
  }

void bg_gtk_vumeter_set_format(bg_gtk_vumeter_t * m,
                               gavl_audio_format_t * format)
  {
  gavl_peak_detector_set_format(m->pd, format);
  m->num_channels = format->num_channels;
  }

void bg_gtk_vumeter_update_peak(bg_gtk_vumeter_t * m,
                                double * ampl, int samples)
  {
  //  cairo_t * cr = NULL;
  int i;
  
  for(i = 0; i < m->num_channels; i++)
    {
    if(m->meters[i].level < ampl[i])
      {
      m->meters[i].level = ampl[i];
      if(m->meters[i].peak < ampl[i])
        {
        m->meters[i].peak = ampl[i];
        m->meters[i].peak_age = 0;
        }
      else if(m->meters[i].peak_age > m->max_peak_age)
        {
        m->meters[i].peak = ampl[i];
        m->meters[i].peak_age = 0;
        }
      else
        m->meters[i].peak_age += samples;
      }
    else /* Lowpass */
      {
      m->meters[i].level = 0.90 * m->meters[i].level + 0.10 * ampl[i];

      if(m->meters[i].peak_age > m->max_peak_age)
        {
        m->meters[i].peak = m->meters[i].level;
        m->meters[i].peak_age = 0;
        }
      else
        m->meters[i].peak_age += samples;
      }
    }
  
  m->pixmaps_valid = 0;
  draw_dynamic(m, NULL);
  }

void bg_gtk_vumeter_update(bg_gtk_vumeter_t * m,
                           gavl_audio_frame_t * frame)
  {
  double ampl[GAVL_MAX_CHANNELS];
  gavl_peak_detector_reset(m->pd);
  gavl_peak_detector_update(m->pd, frame);
  gavl_peak_detector_get_peaks(m->pd, NULL, NULL, ampl);
  bg_gtk_vumeter_update_peak(m, ampl, frame->valid_samples);
  }

void bg_gtk_vumeter_draw(bg_gtk_vumeter_t * m)
  {
  pthread_mutex_lock(&m->analysis_mutex);
  draw_dynamic(m, NULL);
  pthread_mutex_unlock(&m->analysis_mutex);
  flash(m);
  }

void bg_gtk_vumeter_destroy(bg_gtk_vumeter_t * m)
  {
  
  }
