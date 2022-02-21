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
#include <glob.h>
#include <glob.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>


#include <inttypes.h>
#include <limits.h>
#include <gtk/gtk.h>
#include <stdio.h>
#include <ctype.h>

#include <config.h>

#include <gui_gtk/gtkutils.h>

#include <gavl/trackinfo.h>
#include <gavl/metatags.h>
#include <md5.h>


#include <fontconfig/fontconfig.h>

#include <pango/pangofc-fontmap.h>

#include <gmerlin/iconfont.h>
#include <gmerlin/downloader.h>

#include <gmerlin/utils.h>
#include <gmerlin/http.h>
#include <gmerlin/log.h>
#define LOG_DOMAIN "gtkutils"

cairo_surface_t * bg_gtk_pixbuf_scale_alpha(cairo_surface_t * src,
                                            int dest_width,
                                            int dest_height,
                                            float * foreground,
                                            float * background)
  {
  int i, j;
  cairo_surface_t * ret;
  int rowstride;
  uint8_t * data;
  uint32_t * ptr;
  int old_width, old_height;
  cairo_t *cr;
  uint32_t r, g, b, a;
  int background_i[3];
  int foreground_i[3];

  for(i = 0; i < 3; i++)
    {
    background_i[i] = (int)(255.0 * background[i]);
    foreground_i[i] = (int)(255.0 * foreground[i]);
    }

  old_width  = cairo_image_surface_get_width(src);
  old_height = cairo_image_surface_get_height(src);

  // http://lists.cairographics.org/archives/cairo/2006-January/006178.html
  
  ret = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, dest_width, dest_height);
  cr = cairo_create(ret);

  cairo_scale (cr, (double)dest_width / old_width, (double)dest_height / old_height);
  cairo_set_source_surface (cr, src, 0, 0);

  cairo_pattern_set_extend (cairo_get_source(cr), CAIRO_EXTEND_PAD); 
  cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
  cairo_paint(cr);
  cairo_destroy(cr);

  /* Correct colors */

  rowstride = cairo_image_surface_get_stride(ret);
  data      = cairo_image_surface_get_data(ret);

  for(i = 0; i < dest_height; i++)
    {
    ptr = (uint32_t*)data;
    
    for(j = 0; j < dest_width; j++)
      {
      a = (*ptr & 0xff000000) >> 24;

      r = (a * foreground_i[0] + (0xFF - a) * background_i[0]) >> 8;
      g = (a * foreground_i[1] + (0xFF - a) * background_i[1]) >> 8;
      b = (a * foreground_i[2] + (0xFF - a) * background_i[2]) >> 8;

      a = 0xff;

      *ptr =
        (a << 24) |
        (r << 16) |
        (g << 8) |
        (b);
      ptr++;
      }
    
    data += rowstride;
    }
  return ret;
  }

GdkPixbuf * bg_gtk_window_icon_pixbuf = NULL;

static char * default_window_name = NULL;
static char * default_window_class = NULL;

static void set_default_window_icon(const char * icon)
  {
  char * tmp;
  tmp = bg_search_file_read("icons", icon);
  if(tmp)
    {
    if(bg_gtk_window_icon_pixbuf)
      g_object_unref(bg_gtk_window_icon_pixbuf);
    bg_gtk_window_icon_pixbuf = gdk_pixbuf_new_from_file(tmp, NULL);
    free(tmp);
    }
  }

GtkWidget * bg_gtk_window_new(GtkWindowType type)
  {
  GtkWidget * ret = gtk_window_new(type);
  if(bg_gtk_window_icon_pixbuf)
    gtk_window_set_icon(GTK_WINDOW(ret), bg_gtk_window_icon_pixbuf);

#if 0
  if(default_window_name && default_window_class)
    {
    gtk_window_set_wmclass(GTK_WINDOW(ret), 
                           default_window_name, 
                           default_window_class);
    }
#endif
  
  return ret;
  }


void bg_gtk_init(int * argc, char *** argv, 
                 const char * default_window_icon,
                 const char * win_name, const char * win_class)
  {
  gtk_init(argc, argv);

  /* No, we don't like commas as decimal separators */
  setlocale(LC_NUMERIC, "C");

  /* Set the default window icon */
  set_default_window_icon(default_window_icon);

  /* Set default class hints */
  default_window_name = gavl_strrep(default_window_name, win_name);
  default_window_class = gavl_strrep(default_window_class, win_class);

  }

cairo_surface_t * bg_gdk_pixbuf_render_pixmap_and_mask(GdkPixbuf *pixbuf)
  {
  return gdk_cairo_surface_create_from_pixbuf(pixbuf, 0, NULL);
  }

static gboolean bg_pixmap_expose_callback(GtkWidget * w,
                                          cairo_t *cr, gpointer data)
  {
  cairo_surface_t * pixmap = data;

  //  fprintf(stderr, "Expose %p\n", w);
  /* Do the drawing */

//  cairo_mask_surface(cr, pixmap, 0, 0);
  
//  cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.0);
//  cairo_paint(cr);

  cairo_set_source_surface(cr, pixmap, 0, 0);
//  cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
  cairo_paint(cr);

  
  return FALSE;
  }

void bg_gtk_set_widget_bg_pixmap(GtkWidget * widget, cairo_surface_t * surface)
  {
  /* Disconnect old signal handler */
  g_signal_handlers_disconnect_matched(G_OBJECT(widget),
                                       G_SIGNAL_MATCH_FUNC, 0, 0, NULL,
                                       G_CALLBACK(bg_pixmap_expose_callback),
                                       NULL);
  g_signal_connect(widget, "draw",
                   G_CALLBACK(bg_pixmap_expose_callback),
                   surface);
  bg_gtk_widget_queue_redraw(widget);
  }

void bg_gtk_widget_queue_redraw(GtkWidget * widget)
  {
  GdkWindow * window;
  window = gtk_widget_get_window(widget);
  if(window)
    gdk_window_invalidate_rect (window, NULL, TRUE);
  }

static int show_tooltips = 1;


static GQuark tooltip_quark = 0;

static gboolean tooltip_callback(GtkWidget  *widget,
                                 gint        x,
                                 gint        y,
                                 gboolean    keyboard_mode,
                                 GtkTooltip *tooltip,
                                 gpointer    user_data)
  {
  char * str;
  if(show_tooltips)
    {
    str = g_object_get_qdata(G_OBJECT(widget), tooltip_quark);
    gtk_tooltip_set_text(tooltip, str);
    return TRUE;
    }
  else
    return FALSE;
  }


void bg_gtk_tooltips_set_tip(GtkWidget * w, const char * str,
                             const char * translation_domain)
  {
  GValue val = { 0 };
  
  str = TR_DOM(str);
  //  gtk_widget_set_tooltip_text(w, str);

  if(!tooltip_quark)
    tooltip_quark = g_quark_from_string("gmerlin-tooltip");
  
  g_object_set_qdata_full(G_OBJECT(w), tooltip_quark, g_strdup(str), g_free);

  g_value_init(&val, G_TYPE_BOOLEAN);
  g_value_set_boolean(&val, 1);

  g_object_set_property(G_OBJECT(w), "has-tooltip", &val);
  g_signal_connect(G_OBJECT(w), "query-tooltip",
                   G_CALLBACK(tooltip_callback),
                   NULL);
  }

void bg_gtk_set_tooltips(int enable)
  {
  show_tooltips = enable;
  }

int bg_gtk_get_tooltips()
  {
  return show_tooltips;
  }

GtkWidget * bg_gtk_get_toplevel(GtkWidget * w)
  {
  GtkWidget * toplevel;

  if(!w)
    return NULL;
  
  toplevel = gtk_widget_get_toplevel(w);
  if(!bg_gtk_widget_is_toplevel(toplevel))
    toplevel = NULL;
  return toplevel;
  }

static void pixbuf_destroy_notify(guchar *pixels,
                           gpointer data)
  {
  gavl_video_frame_destroy(data);
  }


GdkPixbuf * bg_gtk_pixbuf_from_frame(gavl_video_format_t * format,
                                     gavl_video_frame_t * frame)
  {
  if(format->pixelformat == GAVL_RGB_24)
    {
    return gdk_pixbuf_new_from_data(frame->planes[0],
                                    GDK_COLORSPACE_RGB,
                                    FALSE,
                                    8,
                                    format->image_width,
                                    format->image_height,
                                    frame->strides[0],
                                    pixbuf_destroy_notify,
                                    frame);
    }
  else if(format->pixelformat == GAVL_RGBA_32)
    {
    return gdk_pixbuf_new_from_data(frame->planes[0],
                                    GDK_COLORSPACE_RGB,
                                    TRUE,
                                    8,
                                    format->image_width,
                                    format->image_height,
                                    frame->strides[0],
                                    pixbuf_destroy_notify,
                                    frame);
    
    }
  else
    return NULL;
  }

int bg_gtk_widget_is_realized(GtkWidget * w)
  {
#if GTK_CHECK_VERSION(2,20,0)
  return gtk_widget_get_realized(w);
#else
  return GTK_WIDGET_REALIZED(w);
#endif
  }

int bg_gtk_widget_is_toplevel(GtkWidget * w)
  {
#if GTK_CHECK_VERSION(2,18,0)
  return gtk_widget_is_toplevel(w);
#else
  return GTK_WIDGET_TOPLEVEL(w);
#endif
  }

void bg_gtk_widget_set_can_default(GtkWidget *w, gboolean can_default)
  {
#if GTK_CHECK_VERSION(2,18,0)
  gtk_widget_set_can_default(w, can_default);
#else
  if(can_default)
    GTK_WIDGET_SET_FLAGS(w, GTK_CAN_DEFAULT);
  else
    GTK_WIDGET_UNSET_FLAGS(w, GTK_CAN_DEFAULT);
#endif
  }

void bg_gtk_widget_set_can_focus(GtkWidget *w, gboolean can_focus)
  {
#if GTK_CHECK_VERSION(2,18,0)
  gtk_widget_set_can_focus(w, can_focus);
#else
  if(can_focus)
    GTK_WIDGET_SET_FLAGS(w, GTK_CAN_FOCUS);
  else
    GTK_WIDGET_UNSET_FLAGS(w, GTK_CAN_FOCUS);
#endif
  }


GtkWidget * bg_gtk_combo_box_new_text()
  {
#if GTK_CHECK_VERSION(2,24,0)
  return gtk_combo_box_text_new();
#else
  return gtk_combo_box_new_text();
#endif
  }

void bg_gtk_combo_box_append_text(GtkWidget *combo_box, const gchar *text)
  {
#if GTK_CHECK_VERSION(2,24,0)
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_box), text);
#else
  gtk_combo_box_append_text(GTK_COMBO_BOX(combo_box), text);
#endif
  }

void bg_gtk_combo_box_remove_text(GtkWidget * b, int index)
  {
#if GTK_CHECK_VERSION(2,24,0)
  int i;
  GtkTreeIter it;
  GtkTreeModel * model;
  model = gtk_combo_box_get_model(GTK_COMBO_BOX(b));

  if(!gtk_tree_model_get_iter_first(model, &it))
    return;
  for(i = 0; i < index; i++)
    {
    if(!gtk_tree_model_iter_next(model, &it))
      return;
    }
  gtk_list_store_remove(GTK_LIST_STORE(model), &it);
#else
  gtk_combo_box_remove_text(GTK_COMBO_BOX(b), index);
#endif
  }

typedef struct
  {
  bg_gtk_pixbuf_from_uri_callback cb;
  void * cb_data;

  int max_width;
  int max_height;

  GdkPixbuf * ret;
  
  } load_gtk_image_t;

static void size_prepared_callback(GdkPixbufLoader * loader, int width, int height, gpointer data)
  {
  load_gtk_image_t * m = data;
  int new_width, new_height;
  double ext_x, ext_y, ar;

  ext_x = m->max_width > 0 ? ((double)width / (double)m->max_width) : 1.0;
  ext_y = m->max_height > 0 ? ((double)height / (double)m->max_height) : 1.0;

  //  fprintf(stderr, "size_prepared_callback %d %d\n", width, height);
  
  if((ext_x > 1.0) || (ext_y > 1.0))
    {
    ar = (double)width / (double)height;

    if(ext_x > ext_y) // Fit to max_width
      {
      new_width  = m->max_width;
      new_height = (int)((double)m->max_width / ar + 0.5);
      }
    else // Fit to max_height
      {
      new_height  = m->max_height;
      new_width = (int)((double)m->max_height * ar + 0.5);
      }
    gdk_pixbuf_loader_set_size(loader, new_width, new_height);
    }
  }

static void area_prepared_callback(GdkPixbufLoader * loader, gpointer data)
  {
  load_gtk_image_t * m = data;
  m->ret = gdk_pixbuf_loader_get_pixbuf(loader); 
  g_object_ref(m->ret);
  }

static void pixbuf_from_buffer(load_gtk_image_t * d, const gavl_buffer_t * buf)
  {
  GdkPixbufLoader * loader = gdk_pixbuf_loader_new();

  g_signal_connect(G_OBJECT(loader), "size-prepared", G_CALLBACK(size_prepared_callback), d);
  g_signal_connect(G_OBJECT(loader), "area-prepared", G_CALLBACK(area_prepared_callback), d);
  
  if(!gdk_pixbuf_loader_write(loader, buf->buf, buf->len, NULL))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Sending data failed");
    goto fail;
    }
  if(!gdk_pixbuf_loader_close(loader, NULL))
    goto fail;
  
  fail:
  
  if(loader)
    g_object_unref(loader);
  

  }

GdkPixbuf * bg_gtk_pixbuf_from_buffer(const gavl_buffer_t * buf, int max_width, int max_height)
  {
  load_gtk_image_t d;
  memset(&d, 0, sizeof(d));

  d.max_width = max_width;
  d.max_height = max_height;
  
  pixbuf_from_buffer(&d, buf);
  
  return d.ret;
  
  }
  
GdkPixbuf * bg_gtk_pixbuf_from_uri(const char * url, int max_width, int max_height, int use_cache)
  {
  GdkPixbuf * ret = NULL;
  gavl_dictionary_t dict;
  gavl_buffer_t buf;

  gavl_buffer_init(&buf);
  gavl_dictionary_init(&dict);
  
  if(bg_read_location(url, &buf, 0, 0, &dict))
    {
    ret = bg_gtk_pixbuf_from_buffer(&buf, max_width, max_height);
    }
  
  gavl_buffer_free(&buf);
  gavl_dictionary_free(&dict);
  
  return ret;
  }

static bg_downloader_t * pixbuf_downloader = NULL;

/* Callback called from downloader */

static void image_downloader_callback(void * data,
                                      int64_t id,
                                      const gavl_dictionary_t * dict,
                                      const gavl_buffer_t * buffer)
  {
  GdkPixbuf * pb = NULL;
  
  load_gtk_image_t * d = data;

  if(buffer && buffer->len)
    {
    pb = bg_gtk_pixbuf_from_buffer(buffer, d->max_width, d->max_height);
    }

  if(d->cb)
    {
    d->cb(d->cb_data, pb);
    }
  
  if(pb)
    g_object_unref(pb);
  
  free(d);
  }

static gboolean image_downloader_idle_callback(gpointer data)
  {
  bg_downloader_update(pixbuf_downloader);
  return G_SOURCE_CONTINUE;
  }

void
bg_gtk_pixbuf_from_uri_async(bg_gtk_pixbuf_from_uri_callback cb,
                             void * cb_data,
                             const char * url, int max_width, int max_height)
  {
  load_gtk_image_t * d = calloc(1, sizeof(*d));
  
  if(!pixbuf_downloader)
    {
    pixbuf_downloader = bg_downloader_create(10);
//    g_idle_add(image_downloader_idle_callback, NULL);
    g_timeout_add(20, image_downloader_idle_callback, NULL);
    }
  
  
  d->max_width = max_width;
  d->max_height = max_height;

  d->cb = cb;
  d->cb_data = cb_data;
  
  bg_downloader_add(pixbuf_downloader, url, image_downloader_callback, d);
  }


#if defined(__GNUC__)

static void cleanup_images() __attribute__ ((destructor));

static void cleanup_images()
  {
  if(pixbuf_downloader)
    bg_downloader_destroy(pixbuf_downloader);
  }

#endif


/* 
   typedef void (*bg_gtk_pixbuf_from_uri_callback)(void * data, GtkPixbuf * pb);
*/

const char * bg_gtk_get_track_image_uri(const gavl_dictionary_t * dict, int max_width, int max_height)
  {
  const char * klass;
  const gavl_dictionary_t * m;
  const gavl_dictionary_t * img;

  //  fprintf(stderr, "bg_gtk_get_track_image_uri: \n");
  //  gavl_dictionary_dump(dict, 2);
  //  fprintf(stderr, "\n");
  
    
  if(!(m = gavl_track_get_metadata(dict)) ||
     !(klass = gavl_dictionary_get_string(m, GAVL_META_MEDIA_CLASS)))
    return NULL;

  if(!strcmp(klass, GAVL_META_MEDIA_CLASS_AUDIO_FILE) ||
     !strcmp(klass, GAVL_META_MEDIA_CLASS_SONG) ||
     !strcmp(klass, GAVL_META_MEDIA_CLASS_MUSICALBUM) ||
     !strcmp(klass, GAVL_META_MEDIA_CLASS_ROOT_REMOVABLE_AUDIOCD))
     
    {
    if((img = gavl_dictionary_get_image_max(m,
                                            GAVL_META_COVER_URL, max_width, max_height, NULL)))
      {
      return  gavl_dictionary_get_string(img, GAVL_META_URI);
      }
    }
  else if(!strcmp(klass, GAVL_META_MEDIA_CLASS_CONTAINER))
    {
    if((img = gavl_dictionary_get_image_max(m,
                                            GAVL_META_POSTER_URL, max_width, max_height, NULL)))
      {
      return  gavl_dictionary_get_string(img, GAVL_META_URI);
      }
    else if((img = gavl_dictionary_get_image_max(m,
                                                 GAVL_META_COVER_URL, max_width, max_height, NULL)))
      {
      return  gavl_dictionary_get_string(img, GAVL_META_URI);
      }
    }
  else if(!strcmp(klass, GAVL_META_MEDIA_CLASS_AUDIO_BROADCAST) ||
          !strcmp(klass, GAVL_META_MEDIA_CLASS_PODCAST) ||
          !strcmp(klass, GAVL_META_MEDIA_CLASS_AUDIO_PODCAST_EPISODE) ||
          !strcmp(klass, GAVL_META_MEDIA_CLASS_VIDEO_PODCAST_EPISODE))
    {
    return  gavl_dictionary_get_string(m, GAVL_META_LOGO_URL);
    }
  else if(!strcmp(klass, GAVL_META_MEDIA_CLASS_VIDEO_FILE) ||
          !strcmp(klass, GAVL_META_MEDIA_CLASS_MOVIE) ||
          !strcmp(klass, GAVL_META_MEDIA_CLASS_MOVIE_PART) ||
          !strcmp(klass, GAVL_META_MEDIA_CLASS_MOVIE_MULTIPART) ||
          !strcmp(klass, GAVL_META_MEDIA_CLASS_TV_SHOW) ||
          !strcmp(klass, GAVL_META_MEDIA_CLASS_TV_SEASON) ||
          !strcmp(klass, GAVL_META_MEDIA_CLASS_TV_EPISODE))
    {
    if((img = gavl_dictionary_get_image_max(m,
                                            GAVL_META_POSTER_URL, max_width, max_height, NULL)))
      return  gavl_dictionary_get_string(img, GAVL_META_URI);
    }
  else if(gavl_string_starts_with(klass, GAVL_META_MEDIA_CLASS_IMAGE))
    {
    if((img = gavl_dictionary_get_image_max(m,
                                            GAVL_META_SRC, max_width, max_height, NULL)))
      return gavl_dictionary_get_string(img, GAVL_META_URI);
    }
  /* Icon is a fallback */ 
  else if((img = gavl_dictionary_get_image_max(m, GAVL_META_ICON_URL, max_width, max_height, NULL)))
    {
    return  gavl_dictionary_get_string(img, GAVL_META_URI);
    }
  return NULL;
  }

GdkPixbuf * bg_gtk_load_track_image(const gavl_dictionary_t * dict, int max_width, int max_height)
  {
  const char * uri = NULL;
  int use_cache = 0;

  if(!(uri = bg_gtk_get_track_image_uri(dict, max_width, max_height)))
    return NULL;

  if(gavl_string_starts_with(uri, "http://") ||
     gavl_string_starts_with(uri, "https://"))
    use_cache = 1;
  
  // fprintf(stderr, "Got uri: %s\n", uri);
  
  return bg_gtk_pixbuf_from_uri(uri, max_width, max_height, use_cache);
  }

int bg_gtk_load_track_image_async(bg_gtk_pixbuf_from_uri_callback cb,
                                  void * cb_data,
                                  const gavl_dictionary_t * track, int max_width, int max_height)
  {
  const char * uri = NULL;
  if(!(uri = bg_gtk_get_track_image_uri(track, max_width, max_height)))
    {
    cb(cb_data, NULL);
    return 0;
    }
  /* Don't load local files asynchronously */
  if(*uri == '/') 
    {
    GdkPixbuf * pb = bg_gtk_pixbuf_from_uri(uri, max_width, max_height, 0);
    if(!pb)
      {
      cb(cb_data, NULL);
      return 0;
      }
    cb(cb_data, pb);
    g_object_unref(pb);
    }
  else
    bg_gtk_pixbuf_from_uri_async(cb, cb_data, uri, max_width, max_height);
  return 1;
  }


void bg_gtk_table_attach(GtkWidget *w, GtkWidget * child,
                         int left, int right, int top, int bottom, int hexpand, int vexpand)
  {
  gtk_widget_set_hexpand(child, !!hexpand);
  gtk_widget_set_vexpand(child, !!vexpand);
  gtk_grid_attach(GTK_GRID(w), child, left, top, right - left, bottom - top);
  }

void bg_gtk_table_attach_defaults(GtkWidget *w, GtkWidget * child, int left, int right, int top, int bottom)
  {
  bg_gtk_table_attach(w, child, left, right, top, bottom, 1, 1);
  }

GtkWidget * bg_gtk_hbox_new(int spacing)
  {
  GtkWidget * ret = gtk_grid_new();
  gtk_grid_set_column_spacing(GTK_GRID(ret), spacing);
  gtk_orientable_set_orientation(GTK_ORIENTABLE(ret), GTK_ORIENTATION_HORIZONTAL);
  return ret;
  }

GtkWidget * bg_gtk_vbox_new(int spacing)
  {
  GtkWidget * ret = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(ret), spacing);
  gtk_orientable_set_orientation(GTK_ORIENTABLE(ret), GTK_ORIENTATION_VERTICAL);
  return ret;
  }

void bg_gtk_box_pack_start(GtkWidget * box, GtkWidget * child, int expand)
  {
  if(gtk_orientable_get_orientation(GTK_ORIENTABLE(box)) == GTK_ORIENTATION_VERTICAL)
    gtk_widget_set_vexpand(child, !!expand);
  else
    gtk_widget_set_hexpand(child, !!expand);
  
  gtk_container_add(GTK_CONTAINER(box), child);
  }

void bg_gtk_box_pack_end(GtkWidget * box, GtkWidget * child, int expand)
  {
  if(gtk_orientable_get_orientation(GTK_ORIENTABLE(box)) == GTK_ORIENTATION_VERTICAL)
    gtk_widget_set_vexpand(child, !!expand);
  else
    gtk_widget_set_hexpand(child, !!expand);

  gtk_grid_attach_next_to (GTK_GRID(box), child, NULL, GTK_POS_RIGHT, 1, 1);
  
  //  gtk_container_add(GTK_CONTAINER(box), child);
  }

GtkWidget * bg_gtk_image_menu_item_new_full(GtkWidget * ret,
                                            const char * label, const char * icon_file,
                                            GdkPixbuf * pixbuf)
  {
  GtkWidget * box;
  GtkWidget * lab;
  GtkWidget * alab;
  GtkWidget * image = NULL;
  
  if(!ret)
    ret = gtk_menu_item_new();
  
  if(icon_file)
    {
    if(gavl_string_starts_with(icon_file, "icon:"))
      {
      char * markup = g_markup_printf_escaped("<span size=\"32000\" font_family=\"%s\" weight=\"normal\">%s</span>",
                                              BG_ICON_FONT_FAMILY, icon_file + 5);
      image = gtk_label_new(NULL);
      gtk_label_set_markup(GTK_LABEL(image), markup);
      g_free(markup);
      }
    else
      {
      GdkPixbuf * pbuf;
      if(!(pbuf = bg_gtk_pixbuf_from_uri(icon_file, 48, 48, 0)))
        gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Couldn't load %s", icon_file);
      else
        {
        if(gdk_pixbuf_get_width(pbuf) > 48)
          {
          GdkPixbuf * tmp = gdk_pixbuf_scale_simple(pbuf,
                                                    48, 48, GDK_INTERP_BILINEAR);
        
          g_object_unref(pbuf);
          pbuf = tmp;
          }
      
        image = gtk_image_new_from_pixbuf(pbuf);
        g_object_unref(pbuf);
        }
      }
    }
  else if(pixbuf)
    image = gtk_image_new_from_pixbuf(pixbuf);
  
  alab = gtk_accel_label_new("");
  gtk_accel_label_set_accel_widget(GTK_ACCEL_LABEL(alab), ret);
  
  lab = gtk_label_new("");
  gtk_label_set_markup(GTK_LABEL(lab), label);
  
  //  gtk_accel_label_set_accel(GTK_ACCEL_LABEL(lab),
  //                            GDK_KEY_F10,
  //                            GDK_SHIFT_MASK);
  
  box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
  gtk_widget_set_halign(lab, GTK_ALIGN_START);
  
  gtk_widget_show(lab);
  gtk_widget_show(alab);

  if(image)
    {
    gtk_widget_show(image);
    gtk_box_pack_start(GTK_BOX(box), image, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), lab, TRUE, TRUE, 0);
    }
  else
    gtk_box_pack_start(GTK_BOX(box), lab, TRUE, TRUE, 21);
  
  gtk_box_pack_end(GTK_BOX(box), alab, TRUE, TRUE, 0);
  
  gtk_widget_show(box);
  gtk_container_add(GTK_CONTAINER(ret), box);
  
  return ret;
  }

GtkWidget * bg_gtk_image_menu_item_new(const char * label, const char * icon_file)
  {
  return bg_gtk_image_menu_item_new_full(NULL, label, icon_file, NULL);
  }

GtkWidget * bg_gtk_icon_menu_item_new(const char * label, const char * icon)
  {
  GtkWidget * ret;
  GtkWidget * image = NULL;
  GtkWidget * lab;
  GtkWidget * box;

  ret = gtk_menu_item_new();
  
  if(icon)
    {
    char * markup = g_markup_printf_escaped("<span size=\"16000\" font_family=\"%s\" weight=\"normal\">%s</span>",
                                            BG_ICON_FONT_FAMILY, icon);
    image = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(image), markup);
    g_free(markup);
    }

  lab = gtk_label_new(label);

  box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
  gtk_widget_set_halign(lab, GTK_ALIGN_START);
  
  gtk_widget_show(lab);


  if(image)
    {
    gtk_widget_show(image);
    gtk_box_pack_start(GTK_BOX(box), image, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), lab, TRUE, TRUE, 0);
    }
  else
    gtk_box_pack_start(GTK_BOX(box), lab, TRUE, TRUE, 21);
  
  gtk_widget_show(box);
  gtk_container_add(GTK_CONTAINER(ret), box);
  
  return ret;
  }

void bg_gtk_decorated_window_move_resize_window(GtkWidget* wid, int x, int y, int w, int h)
  {
  GdkWindow * win;

  if((win = gtk_widget_get_window(wid)))
    gdk_window_move_resize(win, x, y, w, h);
  }

/* Callback struct */

typedef struct
  {
  GtkWidget * grid;
  int * rows;
  int * cols;
  } get_dimensions_t;

static void get_dimensions_callback(GtkWidget * w, gpointer data)
  {
  get_dimensions_t * d = data;
  GValue gv = G_VALUE_INIT;
  gint x;
  
  g_value_init(&gv, G_TYPE_INT);
  
  if(d->rows)
    {
    gtk_container_child_get_property(GTK_CONTAINER(d->grid), w,
                                     "top-attach", &gv);
    x = g_value_get_int(&gv);

    gtk_container_child_get_property(GTK_CONTAINER(d->grid), w,
                                     "height", &gv);
    x += g_value_get_int(&gv);

    if(x > *d->rows)
      *d->rows = x;
    }
  
  if(d->cols)
    {
    gtk_container_child_get_property(GTK_CONTAINER(d->grid), w,
                                     "left-attach", &gv);
    x = g_value_get_int(&gv);

    gtk_container_child_get_property(GTK_CONTAINER(d->grid), w,
                                     "width", &gv);
    x += g_value_get_int(&gv);
    
    if(x > *d->cols)
      *d->cols = x;
    }
  }

void bg_gtk_grid_get_dimensions(GtkWidget* grid, int * rows, int * cols)
  {
  get_dimensions_t dim;
  dim.grid = grid;
  dim.rows = rows;
  dim.cols = cols;

  if(rows)
    *rows = 0;
  if(cols)
    *cols = 0;
  
  gtk_container_foreach(GTK_CONTAINER(grid), get_dimensions_callback, &dim);
  }


void bg_gtk_get_text_extents(PangoFontDescription * font_desc,
                             char * str, PangoRectangle * logical_rect)
  {
  cairo_surface_t * s;
  cairo_t * cr;
  PangoLayout * layout;
  PangoRectangle ink_rect;

  if(!str)
    {
    logical_rect->width = 0;
    logical_rect->height = 0;
    logical_rect->x = 0;
    logical_rect->y = 0;
    return;
    }
  
  s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 10, 10);
  cr = cairo_create(s);
  layout = pango_cairo_create_layout(cr);
  
  if(font_desc)
    pango_layout_set_font_description(layout, font_desc);

  pango_layout_set_text(layout, str, -1);

  pango_layout_get_extents(layout, &ink_rect, logical_rect);
  
  g_object_unref(layout);
  cairo_destroy(cr);
  cairo_surface_destroy(s);
  }

GtkWidget * bg_gtk_create_icon_button(const char * icon)
  {
  GtkWidget * label;
  GtkWidget * ret;
  char * markup = g_markup_printf_escaped("<span size=\"16000\" font_family=\"%s\" weight=\"normal\">%s</span>",
                                          BG_ICON_FONT_FAMILY, icon);
  label = gtk_label_new(NULL);
  gtk_label_set_markup(GTK_LABEL(label), markup);
  g_free(markup);

  gtk_widget_show(label);
  
  ret = gtk_button_new();
  gtk_container_add(GTK_CONTAINER(ret), label);
  
  return ret;
  }
