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

#ifndef BG_CAIRO_H_INCLUDED
#define BG_CAIRO_H_INCLUDED

#include <gavl/gavl.h>
#include <cairo/cairo.h>

gavl_video_frame_t * bg_cairo_frame_create(gavl_video_format_t * fmt);

cairo_t * bg_cairo_create(const gavl_video_format_t * fmt,
                          gavl_video_frame_t * frame);

void bg_cairo_frame_done(const gavl_video_format_t * fmt,
                         gavl_video_frame_t * frame);

void bg_cairo_frame_destroy(gavl_video_frame_t * frame);

/* Copy one image surface to another */

void bg_cairo_img_surface_copy(cairo_surface_t * dst,
                               cairo_surface_t * src,
                               int src_x,
                               int src_y,
                               int dst_x,
                               int dst_y,
                               int w,
                               int h);

void bg_cairo_surface_fill_rgb(cairo_surface_t * s,
                               cairo_t * cr1, float * rgb);

void bg_cairo_surface_fill_rgba(cairo_surface_t * s,
                                cairo_t * cr1, float * rgba);

void bg_cairo_draw_hline(cairo_t * cr, double x1, double x2, double y);
void bg_cairo_draw_vline(cairo_t * cr, double y1, double y2, double x);

void
bg_cairo_paint_image(cairo_t * cr, cairo_surface_t * s,
                     double x, double y,
                     double width, double height);

void
bg_cairo_make_rounded_box(cairo_t * cr,
                          const gavl_rectangle_f_t * r,
                          double radius);

#endif // BG_CAIRO_H_INCLUDED

