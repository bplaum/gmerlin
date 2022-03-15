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

#ifndef BG_GTKUTILS_H_INCLUDED
#define BG_GTKUTILS_H_INCLUDED

#include <gmerlin/translation.h>
#include <gavl/gavl.h>
#include <gavl/value.h>

extern GdkPixbuf * bg_gtk_window_icon_pixbuf;

cairo_surface_t * bg_gtk_pixbuf_scale_alpha(cairo_surface_t * src,
                                            int dest_width,
                                            int dest_height,
                                            float * foreground,
                                            float * background);

GdkPixbuf * bg_gtk_pixbuf_from_frame(gavl_video_format_t * format,
                                     gavl_video_frame_t * frame);

void bg_gtk_init(int * argc, char *** argv, 
                 const char * default_window_icon);

cairo_surface_t * bg_gdk_pixbuf_render_pixmap_and_mask(GdkPixbuf *pixbuf);

void bg_gtk_set_widget_bg_pixmap(GtkWidget * w, cairo_surface_t *);

GtkWidget * bg_gtk_window_new(GtkWindowType type);

void bg_gtk_tooltips_set_tip(GtkWidget * w, const char * str,
                             const char * translation_domain);

void bg_gtk_set_tooltips(int enable);
int bg_gtk_get_tooltips();

GtkWidget * bg_gtk_get_toplevel(GtkWidget * w);

// #define bg_gtk_box_pack_start_defaults(b, cy 
//  gtk_box_pack_start(b, c, TRUE, TRUE, 0)

int bg_gtk_widget_is_realized(GtkWidget * w);
int bg_gtk_widget_is_toplevel(GtkWidget * w);

void bg_gtk_widget_set_can_default(GtkWidget *w, gboolean can_default);
void bg_gtk_widget_set_can_focus(GtkWidget *w, gboolean can_focus);

GtkWidget * bg_gtk_combo_box_new_text();
void bg_gtk_combo_box_append_text(GtkWidget *combo_box, const gchar *text);
void bg_gtk_combo_box_remove_text(GtkWidget * b, int index);

GdkPixbuf * bg_gtk_pixbuf_from_buffer(const gavl_buffer_t * buf, int max_width, int max_height);

GdkPixbuf * bg_gtk_pixbuf_from_uri(const char * url, int max_width, int max_height, int use_cache);

typedef void (*bg_gtk_pixbuf_from_uri_callback)(void * data, GdkPixbuf * pb);

void bg_gtk_pixbuf_from_uri_async(bg_gtk_pixbuf_from_uri_callback cb,
                                  void * cb_data,
                                  const char * url, int max_width, int max_height);

const char * bg_gtk_get_track_image_uri(const gavl_dictionary_t * dict, int max_width, int max_height);

/* GtkTable -> GtkGrid translator */

void bg_gtk_table_attach_defaults(GtkWidget *w, GtkWidget * child,
                                  int left, int right, int top, int bottom);

void bg_gtk_table_attach(GtkWidget *w, GtkWidget * child,
                         int left, int right, int top, int bottom, int hexpand, int vexpand);

/* GtkBox -> GtkGrid translator */

GtkWidget * bg_gtk_hbox_new(int spacing);
GtkWidget * bg_gtk_vbox_new(int spacing);

void bg_gtk_box_pack_start(GtkWidget * w, GtkWidget * child, int expand);
void bg_gtk_box_pack_end(GtkWidget * w, GtkWidget * child, int expand);

/* GtkImageMenuItem */

GtkWidget *
bg_gtk_image_menu_item_new(const char * label, const char * image_path);

GtkWidget * bg_gtk_image_menu_item_new_full(GtkWidget * ret,
                                            const char * label, const char * icon_file,
                                            GdkPixbuf * pixbuf);


GtkWidget * bg_gtk_icon_menu_item_new(const char * label, const char * icon);
GtkWidget * bg_gtk_create_icon_button(const char * icon);

void bg_gtk_widget_queue_redraw(GtkWidget * widget);

void bg_gtk_decorated_window_move_resize_window(GtkWidget* wid,
                                                int x, int y, int w, int h);

void bg_gtk_grid_get_dimensions(GtkWidget* grid, int * rows, int * cols);

void bg_gtk_get_text_extents(PangoFontDescription * font_desc,
                             char * str, PangoRectangle * logical_rect);

GdkPixbuf * bg_gtk_load_track_image(const gavl_dictionary_t * dict, int max_width, int max_height);

int bg_gtk_load_track_image_async(bg_gtk_pixbuf_from_uri_callback cb,
                                  void * cb_data,
                                  const gavl_dictionary_t * track, int max_width, int max_height);

#endif // BG_GTKUTILS_H_INCLUDED

