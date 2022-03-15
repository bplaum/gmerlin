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

#ifndef BG_GTK_LOGWINDOW_H_INCLUDED
#define BG_GTK_LOGWINDOW_H_INCLUDED

typedef struct bg_gtk_log_window_s bg_gtk_log_window_t;

/* State */
#define BG_GTK_LOGWINDOW_STATE_CTX "logwindow"
#define BG_GTK_LOGWINDOW_VISIBLE   "visible"

bg_gtk_log_window_t *
bg_gtk_log_window_create(const char * app_name);

void bg_gtk_log_window_destroy(bg_gtk_log_window_t *);

GtkWidget * bg_gtk_log_window_get_widget(bg_gtk_log_window_t * w);

const bg_parameter_info_t *
bg_gtk_log_window_get_parameters(bg_gtk_log_window_t *);

void bg_gtk_log_window_set_parameter(void * data, const char * name,
                                     const gavl_value_t * v);

void bg_gtk_log_window_flush(bg_gtk_log_window_t *);

const char * bg_gtk_log_window_last_error(bg_gtk_log_window_t *);

#endif // BG_GTK_LOGWINDOW_H_INCLUDED

