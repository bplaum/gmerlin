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



#ifndef BGLV_PRIV_H_INCLUDED
#define BGLV_PRIV_H_INCLUDED


/* Interface for libvisual plugins */

bg_plugin_info_t * bg_lv_get_info(const char * filename);

int bg_lv_load(bg_plugin_handle_t * ret,
               const char * name, int plugin_flags,
               const char * window_id);

void bg_lv_unload(bg_plugin_handle_t *);

#endif // BGLV_PRIV_H_INCLUDED
