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



#ifndef BGLADSPA_PRIV_H_INCLUDED
#define BGLADSPA_PRIV_H_INCLUDED


/* Make gmerlin audio filters from ladspa plugins.
 * See http://www.ladspa.org for information about ladspa.
 *
 * For plugins, go to
 * http://tap-plugins.sourceforge.net
 * http://plugin.org.uk/
 */

bg_plugin_info_t * bg_ladspa_get_info(void * dll_handle, const char * filename);

int bg_ladspa_load(bg_plugin_handle_t * ret,
                   const bg_plugin_info_t * info);

void bg_ladspa_unload(bg_plugin_handle_t *);

#endif // BGLADSPA_PRIV_H_INCLUDED
