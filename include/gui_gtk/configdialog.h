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

#ifndef CONFIGDIALOG_H_INCLUDED
#define CONFIGDIALOG_H_INCLUDED

#include <gtk/gtk.h>
#include <gmerlin/cfgctx.h>


#define BG_GTK_CONFIG_DIALOG_DESTROY   (1<<0)
#define BG_GTK_CONFIG_DIALOG_SYNC      (1<<1)

#define BG_GTK_CONFIG_DIALOG_OK_CANCEL (1<<2)
#define BG_GTK_CONFIG_DIALOG_ADD_CLOSE (1<<3)

// typedef struct bg_gtk_config_dialog_s bg_gtk_config_dialog_t;

GtkWidget *
bg_gtk_config_dialog_create_single(int flags, const char * title,
                                   GtkWidget * parent,
                                   bg_cfg_ctx_t * ctx);

GtkWidget *
bg_gtk_config_dialog_create_multi(int flags, const char * title,
                                  GtkWidget * parent);

/* Tree support */
void bg_gtk_config_dialog_add_container(GtkWidget *, const char * label,
                                        GtkTreeIter * parent, GtkTreeIter * ret);
  
void bg_gtk_config_dialog_add_section(GtkWidget *,
                                      bg_cfg_ctx_t * ctx,
                                      GtkTreeIter * parent);

// void bg_gtk_config_dialog_destroy(bg_gtk_config_dialog_t *);

#endif // CONFIGDIALOG_H_INCLUDED
