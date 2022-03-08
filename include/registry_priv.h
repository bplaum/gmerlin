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

#ifndef REGISTRY_PRIV_H_INCLUDED
#define REGISTRY_PRIV_H_INCLUDED


#define bg_cfg_item_t gavl_value_t

/* Create an empty item */

bg_cfg_item_t * bg_cfg_section_find_item(bg_cfg_section_t * section,
                                         const bg_parameter_info_t * info);

#define BG_CFG_REGISTRY_INFO "$INFO"
#define BG_CFG_REGISTRY_DIRECTORY "$DIR"

#endif // REGISTRY_PRIV_H_INCLUDED
