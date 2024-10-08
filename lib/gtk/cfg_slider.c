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




#include <stdio.h>

#include "gtk_dialog.h"
#include <gui_gtk/gtkutils.h>


typedef struct
  {
  GtkWidget * label;
  GtkWidget * slider;
  } slider_t;

/*
  typedef enum
{
  GTK_EXPAND = 1 << 0,
  GTK_SHRINK = 1 << 1,
  GTK_FILL   = 1 << 2
} GtkAttachOptions;
*/

static void destroy(bg_gtk_widget_t * w)
  {
  slider_t * priv = w->priv;
  free(priv);
  }

static void get_value_int(bg_gtk_widget_t * w)
  {
  slider_t * priv;
  priv = w->priv;
  gtk_range_set_value(GTK_RANGE(priv->slider), (gdouble)w->value.v.i);
  }

static void set_value_int(bg_gtk_widget_t * w)
  {
  slider_t * priv = w->priv;
  gavl_value_set_int(&w->value, (int)(gtk_range_get_value(GTK_RANGE(priv->slider))));
  }

static void get_value_float(bg_gtk_widget_t * w)
  {
  slider_t * priv;
  priv = w->priv;
  gtk_range_set_value(GTK_RANGE(priv->slider), w->value.v.d);
  }

static void set_value_float(bg_gtk_widget_t * w)
  {
  slider_t * priv = w->priv;
  gavl_value_set_float(&w->value, gtk_range_get_value(GTK_RANGE(priv->slider)));
  }

static gboolean button_callback(GtkWidget * wid, GdkEventButton * evt,
                                gpointer data)
  {
  bg_gtk_widget_t * w;
    
  w = data;
  
  if(evt->type == GDK_2BUTTON_PRESS)
    {
    gavl_value_copy(&w->value, &w->info->val_default);

    if(w->info->type == BG_PARAMETER_SLIDER_FLOAT)
      get_value_float(w);
    else if(w->info->type == BG_PARAMETER_SLIDER_INT)
      get_value_int(w);
    
    return TRUE;
    }
  return FALSE;
  }

static void attach(void * priv, GtkWidget * table, int * row)
  {
  slider_t * s = priv;
  bg_gtk_table_attach(table, s->label,
                      0, 1, *row, *row+1, 0, 0);
  
  bg_gtk_table_attach(table, s->slider,
                      1, 2, *row, *row+1, 1, 0);
  *row += 1;
  }

static const gtk_widget_funcs_t int_funcs =
  {
    .get_value = get_value_int,
    .set_value = set_value_int,
    .destroy = destroy,
    .attach =  attach
  };

static const gtk_widget_funcs_t float_funcs =
  {
    .get_value = get_value_float,
    .set_value = set_value_float,
    .destroy = destroy,
    .attach =  attach
  };


static void create_common(bg_gtk_widget_t * w,
                          const bg_parameter_info_t * info,
                          float min_value,
                          float max_value,
                          const char * translation_domain)
  {
  float step;
  slider_t * s = calloc(1, sizeof(*s));
  int i;
  s->label = gtk_label_new(TR_DOM(info->long_name));
  step = 1.0;
  for(i = 0; i < info->num_digits; i++)
    step /= 10.0;

  gtk_widget_set_halign(s->label, GTK_ALIGN_START);
  gtk_widget_set_valign(s->label, GTK_ALIGN_CENTER);
  gtk_widget_show(s->label);

  s->slider =
    gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL,
                             min_value, max_value,  step);

  if(info->help_string)
    {
    bg_gtk_tooltips_set_tip(s->slider,
                            info->help_string, translation_domain);
    }

  
  if(info->flags & BG_PARAMETER_SYNC)
    {
    w->callback_id =
      g_signal_connect(G_OBJECT(s->slider), "value-changed",
                       G_CALLBACK(bg_gtk_change_callback), (gpointer)w);
    w->callback_widget = s->slider;
    }
  gtk_scale_set_value_pos(GTK_SCALE(s->slider), GTK_POS_LEFT);
  gtk_widget_set_events(s->slider, GDK_BUTTON_PRESS_MASK);

  g_signal_connect(G_OBJECT(s->slider), "button-press-event",
                   G_CALLBACK(button_callback), (gpointer)w);
  
  gtk_widget_show(s->slider);
  gtk_widget_show(s->label);
  w->priv = s;
  }

void 
bg_gtk_create_slider_int(bg_gtk_widget_t * w,
                         const char * translation_domain)
  {
  float min_value;
  float max_value;
  
  slider_t * s;

  min_value = (float)w->info->val_min.v.i;
  max_value = (float)w->info->val_max.v.i;

  if(min_value >= max_value)
    {
    min_value = 0.0;
    max_value = 100000.0;
    }
  
  create_common(w, w->info, min_value, max_value, translation_domain);
  s = w->priv;
  w->funcs = &int_funcs;
  gtk_scale_set_digits(GTK_SCALE(s->slider), 0);
  }

void 
bg_gtk_create_slider_float(bg_gtk_widget_t * w,
                           const char * translation_domain)
  {
  float min_value;
  float max_value;
  
  slider_t * s;

  min_value = w->info->val_min.v.d;
  max_value = w->info->val_max.v.d;
  
  if(min_value >= max_value)
    {
    min_value = 0.0;
    max_value = 100000.0;
    }
  
  create_common(w, w->info, min_value, max_value, translation_domain);
  s = w->priv;

  gtk_scale_set_digits(GTK_SCALE(s->slider),
                       w->info->num_digits);

  w->funcs = &float_funcs;
  }
