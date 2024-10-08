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

#include "gtk_dialog.h"
#include <gui_gtk/fileentry.h>
#include <gui_gtk/gtkutils.h>

typedef struct
  {
  bg_gtk_file_entry_t * fe;
  GtkWidget * label;
  } file_t;

static void get_value(bg_gtk_widget_t * w)
  {
  file_t * priv;
  priv = (file_t*)(w->priv);

  if(!w->value.v.str || (*w->value.v.str == '\0'))
    {
    bg_gtk_file_entry_set_filename(priv->fe, "");
    return;
    }
  bg_gtk_file_entry_set_filename(priv->fe, w->value.v.str);
  }

static void set_value(bg_gtk_widget_t * w)
  {
  file_t * priv;
  const char * filename;
  
  priv = (file_t*)(w->priv);

  filename = bg_gtk_file_entry_get_filename(priv->fe);

  gavl_value_free(&w->value);
  gavl_value_init(&w->value);
  
  if(*filename != '\0')
    gavl_value_set_string(&w->value, filename);
  }


static void destroy(bg_gtk_widget_t * w)
  {
  file_t * priv = (file_t*)w->priv;

  bg_gtk_file_entry_destroy(priv->fe);
  free(priv);
  }

static void attach(void * priv, GtkWidget * table,
                   int * row)
  {
  file_t * f = (file_t*)priv;
  bg_gtk_table_attach(table, f->label, 0, 1, *row, *row+1, 0, 0);

  bg_gtk_table_attach(table,
                      bg_gtk_file_entry_get_entry(f->fe),
                      1, 2, *row, *row+1, 1, 0);
  
  bg_gtk_table_attach(table,
                      bg_gtk_file_entry_get_button(f->fe),
                      2, 3, *row, *row+1, 0, 0);
  
  (*row)++;
  }

static const gtk_widget_funcs_t funcs =
  {
    .get_value = get_value,
    .set_value = set_value,
    .destroy =   destroy,
    .attach =    attach
  };


void bg_gtk_create_file(bg_gtk_widget_t * w,
                        const char * translation_domain)
  {
  file_t * priv = calloc(1, sizeof(*priv));
  
  priv->fe = bg_gtk_file_entry_create((w->info->type == BG_PARAMETER_DIRECTORY) ? 1 : 0,
                                      NULL, NULL,
                                      w->info->help_string, translation_domain);
  
  priv->label = gtk_label_new(TR_DOM(w->info->long_name));

  gtk_widget_set_halign(priv->label, GTK_ALIGN_START);
  gtk_widget_set_valign(priv->label, GTK_ALIGN_CENTER);
  gtk_widget_show(priv->label);
  
  w->funcs = &funcs;
  w->priv = priv;
  }

void bg_gtk_create_directory(bg_gtk_widget_t * w, const char * translation_domain)
  {
  bg_gtk_create_file(w, translation_domain);
  }
