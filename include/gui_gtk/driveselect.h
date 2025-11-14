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



#ifndef BG_GTK_DRIVESEL_H_INCLUDED
#define BG_GTK_DRIVESEL_H_INCLUDED

/* Create driveselector with callback */

void
bg_gtk_drivesel_show(const char * title,
                     bg_msg_sink_t * sink,
                     GtkWidget * parent_window);

#if 0
bg_gtk_drivesel_t *
bg_gtk_drivesel_create(const char * title,
                       bg_msg_sink_t * sink,
                       const char * ctx,
                       GtkWidget * parent_window);

/* Destroy driveselector */

void bg_gtk_drivesel_destroy(bg_gtk_drivesel_t * drivesel);

/* Show the window */

void bg_gtk_drivesel_run(bg_gtk_drivesel_t * drivesel, int modal);
#endif


#endif // BG_GTK_DRIVESEL_H_INCLUDED

