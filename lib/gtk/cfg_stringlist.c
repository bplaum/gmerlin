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
#include <string.h>
#include <gmerlin/utils.h>

#include "gtk_dialog.h"

#include <gui_gtk/gtkutils.h>

typedef struct
  {
  GtkWidget * label;
  GtkWidget * combo;
  int selected;
  const char * translation_domain;
  } stringlist_t;

static void get_value(bg_gtk_widget_t * w)
  {
  stringlist_t * priv = w->priv;
  priv->selected = bg_parameter_get_selected(w->info, &w->value);
  gtk_combo_box_set_active(GTK_COMBO_BOX(priv->combo), priv->selected);
  }

static void set_value(bg_gtk_widget_t * w)
  {
  stringlist_t * priv = w->priv;
  gavl_value_set_string(&w->value, w->info->multi_names[priv->selected]);
  }

static void destroy(bg_gtk_widget_t * w)
  {
  stringlist_t * priv = w->priv;
  free(priv);
  }

static void attach(void * priv, GtkWidget * table,
                   int * row)
  {
  stringlist_t * s = priv;

  bg_gtk_table_attach(table, s->label, 0, 1, *row, *row+1, 0, 0);
  bg_gtk_table_attach(table, s->combo, 1, 2, *row, *row+1, 1, 0);
  (*row)++;
  }

static const gtk_widget_funcs_t funcs =
  {
    .get_value = get_value,
    .set_value = set_value,
    .destroy =   destroy,
    .attach =    attach
  };

static void change_callback(GtkWidget * wid, gpointer data)
  {
  bg_gtk_widget_t * w;
  stringlist_t * priv;

  w = data;
  priv = w->priv;
  
  priv->selected = gtk_combo_box_get_active(GTK_COMBO_BOX(priv->combo));

  if(w->info->flags & BG_PARAMETER_SYNC)
    bg_gtk_change_callback(wid, data);
  
  }


/*
 *  Really dirty trick to get tooltips for a GtkComboBox working:
 *  loop through all container children and set the tooltip for
 *  the child, which is a button
 */

static void
set_combo_tooltip(GtkWidget *widget, gpointer   data)
  {
  bg_gtk_widget_t * w = data;
  stringlist_t * priv;
  priv = w->priv;
  
  if(GTK_IS_BUTTON (widget))
    bg_gtk_tooltips_set_tip(widget,
                            w->info->help_string,
                            priv->translation_domain);
  }

static void
realize_combo(GtkWidget *combo, gpointer   data)
  {
  bg_gtk_widget_t * w = (bg_gtk_widget_t *)data;
  
  gtk_container_forall (GTK_CONTAINER (combo),
                        set_combo_tooltip,
                        w);
  }

void bg_gtk_create_stringlist(bg_gtk_widget_t * w,
                              const char * translation_domain)
  {
  int i;
  stringlist_t * priv = calloc(1, sizeof(*priv));

  w->funcs = &funcs;
  w->priv = priv;
  
  priv->translation_domain = translation_domain;
  
  priv->combo = bg_gtk_combo_box_new_text();
  i = 0;

  if(w->info->help_string)
    {
    g_signal_connect (priv->combo, "realize",
                      G_CALLBACK (realize_combo), w);
    }
  
  if(w->info->multi_labels)
    {
    while(w->info->multi_labels[i])
      {
      bg_gtk_combo_box_append_text(priv->combo,
                                   TR_DOM(w->info->multi_labels[i]));
      i++;
      }
    }
  else
    {
    while(w->info->multi_names[i])
      {
      bg_gtk_combo_box_append_text(priv->combo,
                                   w->info->multi_names[i]);
      i++;
      }
    }
  
  w->callback_widget = priv->combo;
  w->callback_id = g_signal_connect(G_OBJECT(w->callback_widget),
                                      "changed", G_CALLBACK(change_callback),
                                      (gpointer)w);
  
  //  GTK_WIDGET_UNSET_FLAGS(priv->combo, GTK_CAN_DEFAULT);

  gtk_widget_show(priv->combo);

  priv->label = gtk_label_new(TR_DOM(w->info->long_name));

  gtk_widget_set_halign(priv->label, GTK_ALIGN_START);
  gtk_widget_set_valign(priv->label, GTK_ALIGN_CENTER);
  
  gtk_widget_show(priv->label);
    
  }
