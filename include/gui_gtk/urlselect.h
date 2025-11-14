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



#ifndef BG_GTK_URLSELECT_H_INCLUDED
#define BG_GTK_URLSELECT_H_INCLUDED

/* Create urlselector with callback */

// BG_MSG_DIALOG_ADD_LOCATIONS

void
bg_gtk_urlsel_show(const char * title,
                   bg_msg_sink_t * sink,
                   GtkWidget * parent_window);

#endif // BG_GTK_URLSELECT_H_INCLUDED


