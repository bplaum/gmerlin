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



#ifndef BG_EDL_H_INCLUDED
#define BG_EDL_H_INCLUDED

#include <gmerlin/parameter.h>
#include <gmerlin/streaminfo.h>
#include <gavl/edl.h>

/** \defgroup edl EDL support
 *  \ingroup plugin_i
 *  \brief EDL support
 *
 *  Most media files contain one or more A/V streams. In addition however, there
 *  can be additional instructions, how the media should be played back. Basically
 *  you can have "logical" streams, where the EDL tells how they are composed from
 *  phyiscal streams.
 *
 *  To use EDLs with gmerlin, note the following:
 *
 *  - If you do nothing, the streams are decoded as they are found in the file
 *  - If a media file contains an EDL, it is returned by the get_edl() method of
 *    the input plugin.
 *  - The EDL references streams either in the file you opened, or in external
 *    files.
 *  - Some files contain only the EDL (with external references) but no actual media
 *    streams. In this case, the get_num_tracks() method of the input plugin will return 0.
 *  - The gmerlin library contains a builtin EDL decoder plugin, which opens the
 *    elementary streams and decodes the EDL as if it was a simple file. It can be used
 *    by calling \ref bg_input_plugin_load. It will fire up an EDL decoder for files, which
 *    contain only EDL data and no media. For other files, the behaviour is controlled by the
 *    prefer_edl argument.
 * @{
 */

/** \brief Save an EDL to an xml file
    \param e An EDL
    \param filename Name of the file
 */


void bg_edl_save(const gavl_edl_t * e, const char * filename);

/** \brief Load an EDL from an xml file
    \param filename Name of the file
    \returns The EDL or NULL.
*/

gavl_edl_t * bg_edl_load(const char * filename);

/**
 * @}
 */


#endif // BG_EDL_H_INCLUDED
