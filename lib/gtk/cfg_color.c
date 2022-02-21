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

#include <stdio.h>
#include <string.h>

#include "gtk_dialog.h"
#include <gui_gtk/gtkutils.h>

#define HAVE_RGBA

typedef struct
  {
  GtkWidget * button;
  GtkWidget * label;
  int has_alpha;
  } color_t;



static void destroy(bg_gtk_widget_t * w)
  {
  color_t * priv = w->priv;
  free(priv);
  }

static void get_value(bg_gtk_widget_t * w)
  {
  color_t * priv;

#ifdef HAVE_RGBA
  GdkRGBA rgba;
  
  priv = w->priv;

  rgba.red   = w->value.v.color[0];
  rgba.green = w->value.v.color[1];
  rgba.blue  = w->value.v.color[2];
  
  if(priv->has_alpha)
    rgba.alpha = w->value.v.color[3];
  else
    rgba.alpha = 1.0;
  
  gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(priv->button), &rgba);
#else
  GdkColor col;
  priv = w->priv;
  col.red   = (guint16)(w->value.v.color[0]*65535.0);
  col.green = (guint16)(w->value.v.color[1]*65535.0);
  col.blue  = (guint16)(w->value.v.color[2]*65535.0);
  gtk_color_button_set_color(GTK_COLOR_BUTTON(priv->button), &col);
  
  if(priv->has_alpha)
    gtk_color_button_set_alpha(GTK_COLOR_BUTTON(priv->button),
                               (guint16)(w->value.val_color[3]*65535.0));
#endif
  }

static void set_value(bg_gtk_widget_t * w)
  {
  color_t * priv;

#ifdef HAVE_RGBA

  GdkRGBA rgba;
  double * col;

  priv = w->priv;

  if(priv->has_alpha)
    col = gavl_value_set_color_rgba(&w->value);
  else
    col = gavl_value_set_color_rgb(&w->value);
  

  gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(priv->button), &rgba);
  
  col[0] = rgba.red;
  col[1] = rgba.green;
  col[2] = rgba.blue;
  
  if(priv->has_alpha)
    col[3] = rgba.alpha;

#else
  GdkColor col;

  priv = w->priv;
  gtk_color_button_get_color(GTK_COLOR_BUTTON(priv->button), &col);

  w->value.v.color[0] = (float)col.red/65535.0;
  w->value.v.color[1] = (float)col.green/65535.0;
  w->value.v.color[2] = (float)col.blue/65535.0;
  
  if(priv->has_alpha)
    w->value.val_color[3] = (float)gtk_color_button_get_alpha(GTK_COLOR_BUTTON(priv->button))/65535.0;
    
#endif
  }

static void attach(void * priv, GtkWidget * table,
                   int * row)
  {
  color_t * c = (color_t*)priv;
  bg_gtk_table_attach(table, c->label,  0, 1, *row, *row+1, 0, 0);
  bg_gtk_table_attach(table, c->button, 1, 2, *row, *row+1, 1, 0);
  (*row)++;
  }

static const gtk_widget_funcs_t funcs =
  {
    .get_value = get_value,
    .set_value = set_value,
    .destroy =   destroy,
    .attach =    attach
  };

static void changed_callback(GtkWidget * w, gpointer data)
  {
  bg_gtk_widget_t * wid = data;
  bg_gtk_change_callback(NULL, wid);
  }

static void create(bg_gtk_widget_t * w,
                   const char * translation_domain, int alpha)
  {
  color_t * priv = calloc(1, sizeof(*priv));

  w->funcs = &funcs;
  
  w->value.v.color[0] = 0.0;
  w->value.v.color[1] = 0.0;
  w->value.v.color[2] = 0.0;
  w->value.v.color[3] = 1.0;
  
  priv->button = gtk_color_button_new();
  priv->has_alpha = alpha;
  
  if(w->info->help_string)
    {
    bg_gtk_tooltips_set_tip(priv->button, w->info->help_string,
                            translation_domain);
    }

  if(alpha)
    gtk_color_chooser_set_use_alpha(GTK_COLOR_CHOOSER(priv->button), 1);

  g_signal_connect(priv->button, "color-set", G_CALLBACK(changed_callback), w);
  
  gtk_widget_show(priv->button);

  priv->label = gtk_label_new(TR_DOM(w->info->long_name));

  gtk_widget_set_halign(priv->label, GTK_ALIGN_START);
  gtk_widget_set_valign(priv->label, GTK_ALIGN_CENTER);
  
  
  
  gtk_widget_show(priv->label);
  
  w->priv = priv;
  }
  
void bg_gtk_create_color_rgba(bg_gtk_widget_t * w,
                              const char * translation_domain)
  {
  create(w, translation_domain, 1);
  }

void bg_gtk_create_color_rgb(bg_gtk_widget_t * w,
                             const char * translation_domain)
  {
  create(w, translation_domain, 0);
  }
