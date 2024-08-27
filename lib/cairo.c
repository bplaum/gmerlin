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



#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>


#include <gmerlin/bgcairo.h>

#define DEGREES (M_PI/180.0)


/* Functions for letting cairo render into gavl frames */

gavl_video_frame_t * bg_cairo_frame_create(gavl_video_format_t * fmt)
  {
  gavl_video_frame_t * ret;
  cairo_surface_t * s = 
    cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                               fmt->image_width,
                               fmt->image_height);

  // cairo_surface_set_device_scale(s, (float)(fmt->pixel_height)/(float)(fmt->pixel_width), 1.0);
  
  fmt->pixelformat = GAVL_RGBA_32;
  ret = gavl_video_frame_create(NULL);
  ret->storage = s;
  return ret;
  }

void bg_cairo_frame_destroy(gavl_video_frame_t * frame)
  {
  cairo_surface_t * s = frame->storage;
  
  cairo_surface_destroy(s);
  gavl_video_frame_null(frame);
  gavl_video_frame_destroy(frame);
  
  }

cairo_t *
bg_cairo_create(const gavl_video_format_t * fmt,
                gavl_video_frame_t * frame)
  {
  cairo_surface_t * s = frame->storage;
  return cairo_create(s);
  }

void bg_cairo_frame_done(const gavl_video_format_t * fmt,
                         gavl_video_frame_t * frame)
  {
  int i, j;
  
  uint32_t *src_ptr;
  uint8_t  *dst_ptr;
    
  cairo_surface_t * s = frame->storage;

  cairo_surface_flush(s);
  frame->planes[0] = cairo_image_surface_get_data(s);
  frame->strides[0] = cairo_image_surface_get_stride(s);
    
  /* de-multiply */
  for(i = 0; i < fmt->image_height; i++)
    {
    int r, g, b, a;
    uint32_t src;
    
    dst_ptr = frame->planes[0] + i * frame->strides[0];
    src_ptr = (uint32_t*)dst_ptr;

    for(j = 0; j < fmt->image_width; j++)
      {
      src = *src_ptr;
      a = (src >> 24) & 0xff;
      r = (src >> 16) & 0xff; 
      g = (src >> 8) & 0xff;
      b = (src) & 0xff;

      if(!a)
        {
        /* Set to half level */
        r = 0x80;
        g = 0x80;
        b = 0x80;
        }
      else
        {
        r = (r * 255) / a;
        g = (g * 255) / a;
        b = (b * 255) / a;
        }
      dst_ptr[0] = r;
      dst_ptr[1] = g;
      dst_ptr[2] = b;
      dst_ptr[3] = a;
      dst_ptr+=4;
      src_ptr++;
      }
    }
  }

void bg_cairo_img_surface_copy(cairo_surface_t * dst,
                               cairo_surface_t * src,
                               int src_x, int src_y,
                               int dst_x, int dst_y,
                               int w, int h)
  {
  int i;
  int src_w, src_h, dst_w, dst_h, src_stride, dst_stride;
  int bytes_per_pixel = 4;
  int bytes_per_line;

  const unsigned char * src_ptr;
  unsigned char * dst_ptr;
  
  cairo_surface_flush(src);
  cairo_surface_flush(dst);
  
  src_w      = cairo_image_surface_get_width(src);
  src_h      = cairo_image_surface_get_height(src);
  src_stride = cairo_image_surface_get_stride(src);
  src_ptr    = cairo_image_surface_get_data(src);
  
  dst_w      = cairo_image_surface_get_width(dst);
  dst_h      = cairo_image_surface_get_height(dst);
  dst_stride = cairo_image_surface_get_stride(dst);
  dst_ptr    = cairo_image_surface_get_data(dst);
  
  /* Prevent crashes */
  if((src_x >= src_w) ||
     (src_y >= src_h) ||
     (dst_x >= dst_w) ||
     (dst_y >= dst_h))
    return;

  if(src_x + w > src_w)
    w = src_w - src_x;

  if(src_y + h > src_h)
    h = src_h - src_y;

  if(dst_x + w > dst_w)
    w = dst_w - dst_x;

  if(dst_y + h > dst_h)
    h = dst_h - dst_y;

  bytes_per_line = w * bytes_per_pixel;

  src_ptr += src_y * src_stride + src_x * bytes_per_pixel;
  dst_ptr += dst_y * dst_stride + dst_x * bytes_per_pixel;

  for(i = 0; i < h; i++)
    {
    memcpy(dst_ptr, src_ptr, bytes_per_line);
    dst_ptr += dst_stride;
    src_ptr += src_stride;
    }
  cairo_surface_mark_dirty(dst);
  }

void bg_cairo_surface_fill_rgb(cairo_surface_t * s, cairo_t * cr1, float * rgb)
  {
  cairo_t * cr;
  if(!cr1)
    cr = cairo_create(s);
  else
    {
    cr = cr1;
    cairo_save(cr);
    }

  cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
  cairo_set_source_rgb(cr, rgb[0], rgb[1], rgb[2]);
  cairo_paint(cr);
  
  if(!cr1)
    cairo_destroy(cr);
  else
    cairo_restore(cr);
  }

void bg_cairo_surface_fill_rgba(cairo_surface_t * s,
                                cairo_t * cr1, float * rgba)
  {
  cairo_t * cr;
  if(!cr1)
    cr = cairo_create(s);
  else
    {
    cr = cr1;
    cairo_save(cr);
    }
  
  cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
  cairo_set_source_rgba(cr, rgba[0], rgba[1], rgba[2], rgba[3]);
  cairo_paint(cr);
  
  if(!cr1)
    cairo_destroy(cr);
  else
    cairo_restore(cr);
  }

void bg_cairo_draw_hline(cairo_t * cr,
                         double x1, double x2, double y)
  {
  cairo_move_to(cr, x1, y);
  cairo_line_to(cr, x2, y);
  cairo_stroke(cr);
  }

void bg_cairo_draw_vline(cairo_t * cr,
                         double y1, double y2, double x)
  {
  cairo_move_to(cr, x, y1);
  cairo_line_to(cr, x, y2);
  cairo_stroke(cr);
  }

void bg_cairo_paint_image(cairo_t * cr, cairo_surface_t * s,
                          double x, double y,
                          double width, double height)
  {
  if(width < 0.0)
    width = cairo_image_surface_get_width(s);
  if(height < 0.0)
    height = cairo_image_surface_get_height(s);
  
  cairo_set_source_surface(cr, s, x, y);
  cairo_rectangle(cr, x, y, width, height);
  cairo_fill(cr);
  }

void
bg_cairo_make_rounded_box(cairo_t * cr,
                          const gavl_rectangle_f_t * r,
                          double radius)
  {
  //  fprintf(stderr, "bg_cairo_make_rounded_box %f %f %f %f %f\n", r->x, r->y, r->w, r->h, radius);
  
  if(radius > 0.0)
    {
    cairo_new_sub_path (cr);
    cairo_arc(cr, r->x + r->w - radius,
              r->y + radius, radius,
              -90 * DEGREES,
              0 * DEGREES);
    cairo_arc(cr, r->x + r->w - radius,
              r->y + r->h - radius,
              radius, 0 * DEGREES, 90 * DEGREES);
    cairo_arc(cr, r->x + radius,
              r->y + r->h - radius, radius,
              90 * DEGREES, 180 * DEGREES);
    cairo_arc(cr, r->x + radius,
              r->y + radius, radius,
              180 * DEGREES, 270 * DEGREES);
    cairo_close_path(cr);
    }
  else
    cairo_rectangle(cr, r->x, r->y, r->w, r->h);
  }
