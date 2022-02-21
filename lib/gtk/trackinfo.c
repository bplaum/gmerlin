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

#include <gavl/metatags.h>

#include <config.h>
#include <gmerlin/mdb.h>
#include <gmerlin/utils.h>
#include <gmerlin/iconfont.h>
#include <gui_gtk/albumentry.h>
#include <gui_gtk/textview.h>
#include <gui_gtk/gtkutils.h>
#include <gui_gtk/mdb.h>

#define POSTER_WIDTH 200
#define POSTER_HEIGHT ((POSTER_WIDTH / 2) * 3)

#define COVER_WIDTH 300
#define COVER_HEIGHT COVER_WIDTH

/* Box for displaying static text */

static const char * css =
  "GtkLinkButton { "
  "padding: 0px; "
  "border-width: 0px;"
  "margin: 0px;"
  "border-style: none;"
  "border-image: none;"
  "background-image: none;"
  " }";

typedef struct bg_gtk_trackinfo_s
  {
  GtkWidget * window;
  GtkWidget * close_button;

  GtkWidget * textview;
  GtkWidget * scrolledwindow;
  
  GtkWidget * cover;

  GtkCssProvider * css;

  } bg_gtk_trackinfo_t;

static void button_callback(GtkWidget * w, gpointer data)
  {
  bg_gtk_trackinfo_t * win;
  win = (bg_gtk_trackinfo_t*)data;
  gtk_widget_hide(win->window);
  gtk_widget_destroy(win->window);
  free(win);
  } 

static gboolean delete_callback(GtkWidget * w, GdkEventAny * event,
                                gpointer data)
  {
  button_callback(w, data);
  return TRUE;
  }

static char * append_row_noescape(char * str, const char * icon, const char * val)
  {
  const char * nl = "";
  char * tmp_string;

  if(str && (strlen(str) > 8))
    nl = "\n";
  
  tmp_string = bg_sprintf("%s<span font_family=\"%s\" weight=\"normal\">%s</span>\t%s",
                          nl, BG_ICON_FONT_FAMILY, icon, val);
  
  str = gavl_strcat(str, tmp_string);
  free(tmp_string);
  return str;
  }


static char * append_row(char * str, const char * icon, const char * val)
  {
  gchar * text_escaped;

  text_escaped = g_markup_escape_text(val, -1);
  str = append_row_noescape(str, icon, text_escaped);
  g_free(text_escaped);
  return str;
  }

#if 0
static const gavl_pixelformat_t pfmts[] =
  {
    GAVL_RGB_24,
    GAVL_RGBA_32,
    GAVL_PIXELFORMAT_NONE,
  };
#endif

static void pixbuf_from_uri_callback(void * data, GdkPixbuf * pb)
  {
  GtkWidget * image = data;
  gtk_image_set_from_pixbuf(GTK_IMAGE(image), pb);
  }

static GtkWidget * create_cover(const gavl_dictionary_t * dict)
  {
  GtkWidget * ret = NULL;
  const char * klass;
  int max_width;
  int max_height;
  const gavl_dictionary_t * m;
    
  if(!(m = gavl_track_get_metadata(dict)) ||
     !(klass = gavl_dictionary_get_string(m, GAVL_META_MEDIA_CLASS)))
    return NULL;
  
  if(gavl_string_starts_with(klass, "item.audio"))
    {
    max_width  = COVER_WIDTH;
    max_height = COVER_HEIGHT;
    }
  else
    {
    max_width  = POSTER_WIDTH;
    max_height = POSTER_HEIGHT;
    }

  // fprintf(stderr, "Loading image\n");
  
  ret = gtk_image_new();

  bg_gtk_load_track_image_async(pixbuf_from_uri_callback,
                                ret,
                                dict, max_width, max_height);
  
  gtk_widget_set_hexpand(ret, FALSE);
  gtk_widget_set_vexpand(ret, FALSE);

  gtk_widget_set_valign(ret, GTK_ALIGN_START);
  gtk_widget_set_halign(ret, GTK_ALIGN_START);
  
  gtk_widget_show(ret);
  
  return ret;
  }

static void append_link(bg_gtk_trackinfo_t * info,
                        GtkTextIter * iter,
                        const char * href, const char * label)
  
  {
  GtkWidget * link;
  GtkTextChildAnchor * a;
  
  GtkTextBuffer * buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(info->textview));

  if(!info->css)
    {
    info->css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(info->css,
                                    css,
                                    -1, NULL);
    }
  
  a = gtk_text_buffer_create_child_anchor(buf, iter);

  if(href[0] == '/')
    {
    char * tmp_string = bg_sprintf("file://%s", href);
    link = gtk_link_button_new_with_label(tmp_string, label);
    free(tmp_string);
    }
  else
    link = gtk_link_button_new_with_label(href, label);

  gtk_style_context_add_provider(gtk_widget_get_style_context(link),
                                 GTK_STYLE_PROVIDER(info->css),
                                 GTK_STYLE_PROVIDER_PRIORITY_USER);

  gtk_widget_set_valign(link, 1.0);
  
  gtk_widget_show(link);
  
  gtk_text_view_add_child_at_anchor(GTK_TEXT_VIEW(info->textview),
                                    link, a);
  }

static void create_markup(bg_gtk_trackinfo_t * info, const gavl_dictionary_t * dict)
  {
  int64_t duration;

  char * markup = NULL;
  char * tmp_string = NULL;
  
  //  const char * klass;
  char * var;
  const char * var_c;
  GtkTextBuffer * buf;
  GtkTextIter iter;
  const char * location;
  const char * mimetype;
  const char * format;
  int idx = 0;
  int num_links = 0;
  
  
  buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(info->textview));

  gtk_text_buffer_get_start_iter(buf, &iter);
  
  //  klass = gavl_dictionary_get_string(dict, GAVL_META_MEDIA_CLASS);

  if(!(var_c = gavl_dictionary_get_string(dict, GAVL_META_TITLE)) &&
     !(var_c = gavl_dictionary_get_string(dict, GAVL_META_LABEL)))
    return;

  tmp_string = g_markup_escape_text(var_c, -1);
  markup = bg_sprintf("<span weight=\"bold\" size=\"xx-large\">%s</span>", tmp_string);
  g_free(tmp_string);
  
  /* TODO Add more */

  if((var = gavl_metadata_join_arr(dict,
                                   GAVL_META_ARTIST, ", ")))
    {
    markup = append_row(markup, BG_ICON_MICROPHONE, var);
    free(var);
    }

  if((var = gavl_metadata_join_arr(dict,
                                   GAVL_META_COUNTRY, ", ")))
    {
    markup = append_row(markup, BG_ICON_FLAG, var);
    free(var);
    }

  if((var = gavl_metadata_join_arr(dict,
                                   GAVL_META_TAG, ", ")))
    {
    markup = append_row(markup, BG_ICON_TAGS, var);
    free(var);
    }
  
  if((var = gavl_metadata_join_arr(dict,
                                   GAVL_META_DIRECTOR, ", ")))
    {
    markup = append_row(markup, BG_ICON_MOVIE_MAKER, var);
    free(var);
    }
  
  if((var = gavl_metadata_join_arr(dict,
                                   GAVL_META_ACTOR, ", ")))
    {
    markup = append_row(markup, BG_ICON_PERSON, var);
    free(var);
    }

  
  if((var_c = gavl_dictionary_get_string(dict,
                                         GAVL_META_ALBUM)))
    markup = append_row(markup, BG_ICON_MUSIC_ALBUM, var_c);
  
  if((var = gavl_metadata_join_arr(dict,
                                   GAVL_META_GENRE, ", ")))
    {
    markup = append_row(markup, BG_ICON_MASKS, var);
    free(var);
    }

  if((var_c = gavl_dictionary_get_string(dict, GAVL_META_DATE)) &&
     !gavl_string_starts_with(var_c, "9999"))
    {
    if(gavl_string_ends_with(var_c, "99-99"))
      {
      tmp_string = gavl_strndup(var_c, var_c + 4);
      markup = append_row(markup, BG_ICON_CALENDAR, tmp_string);
      free(tmp_string);
      }
    else
      markup = append_row(markup, BG_ICON_CALENDAR, var_c);
    }
  else if((var_c = gavl_dictionary_get_string(dict, GAVL_META_YEAR)) &&
          !gavl_string_starts_with(var_c, "9999"))
    {
    markup = append_row(markup, BG_ICON_CALENDAR, var_c);
    }
  
  if(gavl_dictionary_get_long(dict, GAVL_META_APPROX_DURATION,
                              &duration) &&
     (duration != GAVL_TIME_UNDEFINED))
    {
    char time_str[GAVL_TIME_STRING_LEN];
    gavl_time_prettyprint(duration, time_str);
    markup = append_row(markup, BG_ICON_CLOCK, time_str);
    }

  if((var_c = gavl_dictionary_get_string(dict, GAVL_META_ID)))
    markup = append_row(markup, BG_ICON_TAG, var_c);
  
  gtk_text_buffer_insert_markup(buf,
                                &iter,
                                markup,
                                -1);

  free(markup);
  markup = NULL;
  
  while(gavl_dictionary_get_src(dict, GAVL_META_SRC, idx++,
                                &mimetype, &location))
    {
    //    fprintf(stderr, "Append link %s %s\n", mimetype, location);
    
    if(!mimetype || !(format = bg_mimetype_to_name(mimetype)))
      continue;
    
    if(!num_links)
      {
      markup = gavl_strdup("\n");
      markup = append_row(markup, BG_ICON_DOWNLOAD, "");
      gtk_text_buffer_insert_markup(buf, &iter, markup, -1);
      }
    else
      gtk_text_buffer_insert_markup(buf, &iter, " ", -1);
    
    append_link(info, &iter, location, format);
    num_links++;
    }
  
  //  fprintf(stderr, "Markup: %s\n", markup);

  // append_link(info, &iter, "http://nas:8888/?cid=test", "nas");
  
  
  if((var_c = gavl_dictionary_get_string(dict,
                                         GAVL_META_PLOT)))
    {
    gtk_text_buffer_insert(buf, &iter, "\n\n", -1);
    gtk_text_buffer_insert(buf, &iter, var_c, -1);
    }
  
  }

static bg_gtk_trackinfo_t *
bg_gtk_trackinfo_create(const gavl_dictionary_t * dictp)
  {
  PangoTabArray * tabs;
  
  GtkWidget * box;
  GtkWidget * table;
  bg_gtk_trackinfo_t * ret;

  const gavl_dictionary_t * m;

  //  fprintf(stderr, "bg_gtk_trackinfo_create\n");
  //  gavl_dictionary_dump(dictp, 2);
  
  if(!(m = gavl_track_get_metadata(dictp)))
    return NULL;
  
  ret = calloc(1, sizeof(*ret));

  ret->window = bg_gtk_window_new(GTK_WINDOW_TOPLEVEL);
  
  gtk_window_set_default_size(GTK_WINDOW(ret->window),
                              500, 250);
  
  gtk_window_set_type_hint(GTK_WINDOW(ret->window),
                           GDK_WINDOW_TYPE_HINT_DIALOG);
  
  gtk_window_set_position(GTK_WINDOW(ret->window),
                          GTK_WIN_POS_CENTER);
  
  g_signal_connect(G_OBJECT(ret->window), "delete_event",
                   G_CALLBACK(delete_callback), (gpointer)ret);

  gtk_window_set_title(GTK_WINDOW(ret->window), "Track info");
  
  /* Create close button */

  ret->close_button = gtk_button_new_with_mnemonic("_Close");
  
  bg_gtk_widget_set_can_default(ret->close_button, TRUE);

  g_signal_connect(G_OBJECT(ret->close_button), "clicked",
                   G_CALLBACK(button_callback), (gpointer)ret);
  gtk_widget_show(ret->close_button);

  /* Button box */
  box = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_container_add(GTK_CONTAINER(box), ret->close_button);
  gtk_widget_show(box);
  
  table = gtk_grid_new();

  gtk_grid_set_row_spacing(GTK_GRID(table), 5);
  gtk_grid_set_column_spacing(GTK_GRID(table), 5);
  gtk_container_set_border_width(GTK_CONTAINER(table), 5);

  /* Create cover */
  ret->cover = create_cover(dictp);
  
  /* Create text */
  
  ret->textview = gtk_text_view_new();

  /* Set tabs */
  tabs = pango_tab_array_new_with_positions(1, TRUE, PANGO_TAB_LEFT, 20);
  gtk_text_view_set_tabs(GTK_TEXT_VIEW(ret->textview), tabs);
  pango_tab_array_free(tabs);

  gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(ret->textview), GTK_WRAP_WORD);

  ret->scrolledwindow =
    gtk_scrolled_window_new(gtk_scrollable_get_hadjustment(GTK_SCROLLABLE(ret->textview)),
                            gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(ret->textview)));

  
  gtk_widget_set_hexpand(ret->textview, TRUE);
  gtk_widget_set_vexpand(ret->textview, TRUE);

  //  gtk_widget_set_halign(ret->textview, GTK_ALIGN_START);
  //  gtk_widget_set_valign(ret->textview, GTK_ALIGN_START);
  
  gtk_widget_set_hexpand(ret->scrolledwindow, TRUE);
  gtk_widget_set_vexpand(ret->scrolledwindow, TRUE);

  //  gtk_widget_set_halign(ret->scrolledwindow, GTK_ALIGN_START);
  //  gtk_widget_set_valign(ret->scrolledwindow, GTK_ALIGN_START);
    
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(ret->scrolledwindow),
                                 GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_container_add(GTK_CONTAINER(ret->scrolledwindow), ret->textview);
  gtk_widget_show(ret->scrolledwindow);
  
  create_markup(ret, m);
  
  gtk_widget_set_can_focus(ret->textview, FALSE);
  
  gtk_widget_set_valign(ret->textview, GTK_ALIGN_START);
  
  gtk_widget_show(ret->textview);

  if(ret->cover)
    {
    gtk_grid_attach(GTK_GRID(table), ret->cover, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(table), ret->scrolledwindow, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(table), box, 0, 1, 2, 1);
    }
  else
    {
    gtk_grid_attach(GTK_GRID(table), ret->scrolledwindow, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(table), box, 0, 1, 1, 1);
    }
  
  gtk_widget_show(table);
  gtk_container_add(GTK_CONTAINER(ret->window), table);
    
  return ret;
  }

static void trackinfo_show(bg_gtk_trackinfo_t * w, int modal, GtkWidget * parent)
  {
  parent = bg_gtk_get_toplevel(parent);
  if(parent)
    {
    gtk_window_set_transient_for(GTK_WINDOW(w->window),
                                 GTK_WINDOW(parent));
    }
  gtk_window_set_modal(GTK_WINDOW(w->window), modal);

  gtk_widget_grab_default(w->close_button);
  gtk_widget_show(w->window);
  }

/* This pops up a which shows all informations about a selected track */

#define S(s) (s?s:"(NULL)")

void bg_gtk_trackinfo_show(const gavl_dictionary_t * dict,
                           GtkWidget * parent)
  {
  bg_gtk_trackinfo_t * win;
  win = bg_gtk_trackinfo_create(dict);
  trackinfo_show(win, 0, parent);
  }


