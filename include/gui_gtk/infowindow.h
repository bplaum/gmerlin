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


#ifndef __BG_GTK_INFOWINDOW_H_
#define __BG_GTK_INFOWINDOW_H_

typedef struct bg_gtk_info_window_s bg_gtk_info_window_t;

bg_gtk_info_window_t *
bg_gtk_info_window_create(void);

GtkWidget * bg_gtk_info_window_get_widget(bg_gtk_info_window_t *);
void bg_gtk_info_window_destroy(bg_gtk_info_window_t *);

void bg_gtk_info_window_set(bg_gtk_info_window_t *, const gavl_dictionary_t * dict);


#endif // __BG_GTK_INFOWINDOW_H_
