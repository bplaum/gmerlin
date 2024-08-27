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
  GtkWidget * button;
  } button_t;

static void destroy(bg_gtk_widget_t * w)
  {
  button_t * priv = w->priv;
  free(priv);
  }

static void attach(void * priv, GtkWidget * table,
                   int * row)
  {
  button_t * b = priv;
  bg_gtk_table_attach(table, b->button, 0, 2, *row, *row+1, 1, 0);
  (*row)++;
  }

static const gtk_widget_funcs_t funcs =
  {
    .destroy =   destroy,
    .attach =    attach
  };

static void callback(GtkWidget * w, gpointer data)
  {
  bg_gtk_widget_t * wid = data;
  if(wid->change_callback)
    wid->change_callback(wid->change_callback_data,
                         wid->info->name, NULL);
  }

void bg_gtk_create_button(bg_gtk_widget_t * w,
                          const char * translation_domain)
  {
  button_t * priv = calloc(1, sizeof(*priv));
  priv->button = gtk_button_new_with_label(TR_DOM(w->info->long_name));

  g_signal_connect(G_OBJECT(priv->button), "clicked",
                   G_CALLBACK(callback), w);
  
  if(w->info->help_string)
    {
    bg_gtk_tooltips_set_tip(priv->button,
                            w->info->help_string, translation_domain);
    }
  
  gtk_widget_show(priv->button);
 
  w->funcs = &funcs;
  w->priv = priv;
  }
