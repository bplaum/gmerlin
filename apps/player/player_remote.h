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



#define PLAYER_REMOTE_PORT (BG_REMOTE_PORT_BASE+1)
#define PLAYER_REMOTE_PATH "/ws/renderer"
#define PLAYER_REMOTE_ENV "GMERLIN_PLAYER_REMOTE_PORT"

/* Player commands */


/* Add or play a location (arg1: Location) */

#define PLAYER_COMMAND_ADD_LOCATION   (BG_PLAYER_MSG_MAX+6)
#define PLAYER_COMMAND_PLAY_LOCATION  (BG_PLAYER_MSG_MAX+7)

/* Open devices */

#define PLAYER_COMMAND_OPEN_DEVICE    (BG_PLAYER_MSG_MAX+12)
#define PLAYER_COMMAND_PLAY_DEVICE    (BG_PLAYER_MSG_MAX+13)

