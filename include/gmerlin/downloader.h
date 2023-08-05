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

#ifndef BG_DOWNLOADER_H_INCLUDED
#define BG_DOWNLOADER_H_INCLUDED

#include <gmerlin/bgmsg.h>

typedef struct bg_downloader_s bg_downloader_t;

/* Message format. Namespace is BG_MSG_NS_DOWNLOADER */

/*
 * arg0: uri (string)
 * arg1: id (string)
 */

#define BG_CMD_DOWNLOADER_ADD    1

/*
 * arg0: id (string)
 */

#define BG_CMD_DOWNLOADER_DELETE 2

/*
 * header[GAVL_MSG_CONTEXT_ID]: id
 * arg0: id (int64)
 * arg1: dictionary
 * arg2: binary
 */

#define BG_MSG_DOWNLOADER_DOWNLOADED 10

/*
 * header[GAVL_MSG_CONTEXT_ID]: id
 */

#define BG_MSG_DOWNLOADER_ERROR      11

/* Buffer can be NULL in which case the download failed */

typedef void (*bg_downloader_callback_t)(void * data,
                                         const gavl_dictionary_t * dict,
                                         const gavl_buffer_t * buffer);

void bg_downloader_update(bg_downloader_t * d);

bg_downloader_t * bg_downloader_create(int max_downloads);

void bg_downloader_destroy(bg_downloader_t * d);

void bg_downloader_add(bg_downloader_t * d, const char * uri,
                       bg_downloader_callback_t cb, void * cb_data);

#endif // G_DOWNLOADER_H_INCLUDED

