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

typedef struct
  {
  GtkWidget * entry;
  GtkWidget * label;
  } string_t;

static void get_value(bg_gtk_widget_t * w)
  {
  char * str;
  string_t * priv;

  priv = w->priv;

  str = gavl_value_join_arr(&w->value, "; ");

  if(str)
    gtk_entry_set_text(GTK_ENTRY(priv->entry), str);
  else
    gtk_entry_set_text(GTK_ENTRY(priv->entry), "");
  free(str);
  }

static void set_value(bg_gtk_widget_t * w)
  {
  string_t * priv = w->priv;
  gavl_value_set_string(&w->value, gtk_entry_get_text(GTK_ENTRY(priv->entry)));
  }

static void destroy(bg_gtk_widget_t * w)
  {
  string_t * priv = w->priv;
  free(priv);
  }

static void attach(void * priv, GtkWidget * table,
                   int * row)
  {
  string_t * b = priv;
  bg_gtk_table_attach(table, b->label, 0, 1, *row, *row+1, 0, 0);
  bg_gtk_table_attach(table, b->entry, 1, 2, *row, *row+1, 1, 0);

  (*row)++;
  }

static const gtk_widget_funcs_t funcs =
  {
    .get_value = get_value,
    .set_value = set_value,
    .destroy =   destroy,
    .attach =    attach
  };

void bg_gtk_create_string(bg_gtk_widget_t * w, const char * translation_domain)
  {
  string_t * priv = calloc(1, sizeof(*priv));

  priv->entry = gtk_entry_new();

  if(w->info->help_string)
    {
    bg_gtk_tooltips_set_tip(priv->entry,
                            w->info->help_string, translation_domain);
    }

  if(w->info->type == BG_PARAMETER_STRING_HIDDEN)
    gtk_entry_set_visibility(GTK_ENTRY(priv->entry), FALSE);
  
  gtk_widget_show(priv->entry);
  
  priv->label = gtk_label_new(TR_DOM(w->info->long_name));

  gtk_widget_set_halign(priv->label, GTK_ALIGN_START);
  gtk_widget_set_valign(priv->label, GTK_ALIGN_CENTER);

  gtk_widget_show(priv->label);
  
  
  w->funcs = &funcs;
  w->priv = priv;
  }
