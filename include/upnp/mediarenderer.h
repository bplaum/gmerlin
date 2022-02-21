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

typedef struct
  {
  int can_seek;
  int can_pause;
  gavl_dictionary_t metadata;

  int state;
  gavl_time_t time;

  int do_audio;
  int do_video;
  char * control_id;
  
  } bg_mediarenderer_t;

#define SERVICE_CONNECTION_MANAGER 0
#define SERVICE_AVTRANSPORT        1
#define SERVICE_RENDERINGCONTROL   2

#define TIME_STRING_LEN 10 // 000:00:00
char * bg_upnp_avtransport_print_time(char * buf, gavl_time_t t);

void bg_upnp_rendering_control_set_volume(bg_upnp_service_t * src,
                                          float volume);

