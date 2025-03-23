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


#ifndef BG_RESOURCEMANAGER_H_INCLUDED
#define BG_RESOURCEMANAGER_H_INCLUDED


#include <gmerlin/bgmsg.h>

#define BG_RESOURCE_EXPIRE_TIME  "ExpireTime"
#define BG_RESOURCE_PRIORITY     "Priority"
#define BG_RESOURCE_PLUGIN       "Plugin"

#define BG_RESOURCE_PRIORITY_MIN     1
#define BG_RESOURCE_PRIORITY_DEFAULT 2
#define BG_RESOURCE_PRIORITY_MAX     3

/*
 *  Unified resource manager for:
    - Removable HDDs and disks
    - Sources and sinks for audio and video
    - Remote backends (servers or renderers)
    - 
*/

/* Get the process wide resource manager, create a new one if necessary */
  

bg_controllable_t * bg_resourcemanager_get_controllable();

gavl_array_t * bg_resourcemanager_get(const char * klass);

void bg_resourcemanager_publish(const char * id, const gavl_dictionary_t * dict);
void bg_resourcemanager_unpublish(const char * id);

int bg_resource_idx_for_label(const gavl_array_t * arr, const char * label, int off);

void bg_resource_get_by_class(const char * klass, int full_match, gavl_time_t timeout, gavl_array_t * arr);
void bg_resource_get_by_protocol(const char * protocol, int full_match, gavl_time_t timeout, gavl_array_t * arr);

void bg_resource_list_by_class(const char * klass, int full_match, gavl_time_t timeout);
void bg_resource_list_by_protocol(const char * protocol, int full_match, gavl_time_t timeout);

/* List resources */
void bg_opt_list_recording_sources(void * data, int * argc, char *** _argv, int arg);

/* Clean up resource manager (usually you don't need to call this) */
void bg_resourcemanager_cleanup();


/* To be used by resource plugins (i.e. from the resource detector thread) only */

gavl_dictionary_t * bg_resource_get_by_id(int local, const char * id);
gavl_dictionary_t * bg_resource_get_by_idx(int local, int idx);

/* List recording devices */
void bg_opt_list_recording_sources(void * data, int * argc,
                                   char *** _argv, int arg);
void bg_opt_list_audio_sinks(void * data, int * argc,
                             char *** _argv, int arg);
void bg_opt_list_video_sinks(void * data, int * argc,
                             char *** _argv, int arg);


#define BG_OPT_LIST_RECORDERS                  \
  { \
  .arg =         "-list-rec-src", \
  .help_string = TRS("List recording sources"), \
  .callback =    bg_opt_list_recording_sources, \
  }

#define BG_OPT_LIST_OA                   \
  { \
  .arg =         "-list-oa", \
  .help_string = TRS("List available audio sinks"), \
  .callback =    bg_opt_list_audio_sinks, \
  }

#define BG_OPT_LIST_OV \
  { \
  .arg =         "-list-ov", \
  .help_string = TRS("List available video sinks"), \
  .callback =    bg_opt_list_video_sinks, \
  }

#endif // BG_RESOURCEMANAGER_H_INCLUDED
