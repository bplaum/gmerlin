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

/* Communication between the visualizer and the slave */

// BG_MSG_NS_VISUALIZER

#define BG_VIS_MSG_IMAGE_SIZE     3
#define BG_VIS_MSG_FPS            4
#define BG_VIS_MSG_QUIT           5
#define BG_VIS_MSG_VIS_PARAM      6
#define BG_VIS_MSG_OV_PARAM       7
#define BG_VIS_MSG_GAIN           8
#define BG_VIS_MSG_START          9
#define BG_VIS_MSG_METADATA      10

/* Messages from the visualizer to the application */

#define BG_VIS_SLAVE_MSG_FPS     1

/* The following are callbacks from the ov plugin */

/*
 * gmerlin_visualize_slave -s socket_address
 *   [-w "window_id"|-o "output_module"]
 *   -p "plugin_module"
 */
