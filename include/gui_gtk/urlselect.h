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

#ifndef BG_GTK_URLSELECT_H_INCLUDED
#define BG_GTK_URLSELECT_H_INCLUDED

typedef struct bg_gtk_urlsel_s bg_gtk_urlsel_t;

/* Create urlselector with callback */

bg_gtk_urlsel_t *
bg_gtk_urlsel_create(const char * title,
                     void (*add_url)(char ** urls, 
                                     void * data),
                     void (*close_notify)(bg_gtk_urlsel_t *,
                                          void * data),
                     void * user_data,
                     GtkWidget * parent_window,
                     bg_plugin_registry_t * plugin_reg,
                     int type_mask, int flag_mask);

/* Destroy urlselector */

void bg_gtk_urlsel_destroy(bg_gtk_urlsel_t * urlsel);

/* Show the window */

/* A non modal window will destroy itself when it's closed */

void bg_gtk_urlsel_run(bg_gtk_urlsel_t * urlsel, int modal, GtkWidget * parent);

#endif // BG_GTK_URLSELECT_H_INCLUDED


