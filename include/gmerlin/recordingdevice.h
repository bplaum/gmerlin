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



#ifndef BG_RECORDINGDEVICE_H_INCLUDED
#define BG_RECORDINGDEVICE_H_INCLUDED

#include <gmerlin/bgmsg.h>

typedef struct bg_recording_device_registry_s bg_recording_device_registry_t;

bg_recording_device_registry_t * bg_recording_device_registry_create();
int bg_recording_device_registry_update(bg_recording_device_registry_t * reg);
bg_msg_hub_t * bg_recording_device_registry_get_msg_hub(bg_recording_device_registry_t * reg);
void bg_recording_device_registry_destroy(bg_recording_device_registry_t * reg);

gavl_array_t * bg_get_recording_devices(int timeout);

void bg_list_recording_devices(int timeout);
void bg_opt_list_recording_sources(void * data, int * argc,
                                   char *** _argv, int arg);

#define BG_OPT_LIST_RECORDERS                  \
  { \
  .arg =         "-list-rec-src", \
  .help_string = TRS("List recording sources"), \
  .callback =    bg_opt_list_recording_sources, \
  }




#endif // BG_RECORDINGDEVICE_H_INCLUDED
