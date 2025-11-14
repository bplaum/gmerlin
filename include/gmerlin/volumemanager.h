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



#ifndef BG_VOLUMEMANAGER_H_INCLUDED
#define BG_VOLUMEMANAGER_H_INCLUDED

#include <gmerlin/bgmsg.h>

typedef struct bg_volume_manager_s bg_volume_manager_t;

/* Array of strings (state variable) */
#define BG_VOLUMEMANAGER_STATE_VOLUMES "volumes"


bg_volume_manager_t * bg_volume_manager_create(void);
void bg_volume_manager_destroy(bg_volume_manager_t *);

bg_msg_hub_t * bg_volume_manager_get_msg_hub(bg_volume_manager_t *);

#endif // BG_VOLUMEMANAGER_H_INCLUDED


