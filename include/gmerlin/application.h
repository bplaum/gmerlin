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


/*
 *  Application wide variables
 *
 *  They are set once from main() and are considered read-only
 *  later on
 */

#ifndef BG_APPLICATION_H_INCLUDED
#define BG_APPLICATION_H_INCLUDED

#include <gavl/gavl.h>
#include <gavl/value.h>
#include <gavl/metatags.h>

#include <gmerlin/bgmsg.h>
#include <gmerlin/state.h>

extern gavl_dictionary_t bg_app_vars;

#define BG_APP_NAME      "Name"
#define BG_APP_LABEL     GAVL_META_LABEL
// #define BG_APP_CFG_DIR   "configdir"
#define BG_APP_ICON_NAME "IconName"
#define BG_APP_CFG_FILE  "configfile"


void bg_app_init(const char * name,
                 const char * label,
                 const char * icon_name);

const char * bg_app_get_name(void);
const char * bg_app_get_label(void);
// const char * bg_app_get_window_icon(void);

const char * bg_app_get_icon_name(void);

char * bg_app_get_icon_file(void);

char * bg_app_get_config_file_name(void);


//const char * bg_app_get_config_dir(void);
// void bg_app_set_config_dir(const char * p);
// void bg_app_set_window_icon(const char * icon);


void bg_array_add_application_icons(gavl_array_t * arr, const char * prefix, const char * name);


// const gavl_array_t * bg_app_get_application_icons(void);
// void bg_app_add_application_icons(const char * prefix, const char * name);

void bg_dictionary_add_application_icons(gavl_dictionary_t * dict,
                                         const char * prefix,
                                         const char * name);


#endif // BG_APPLICATION_H_INCLUDED
