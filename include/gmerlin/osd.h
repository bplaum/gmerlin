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



#ifndef BG_OSD_H_INCLUDED
#define BG_OSD_H_INCLUDED


typedef struct bg_osd_s bg_osd_t;
// typedef struct bg_player_s bg_player_t;

bg_osd_t * bg_osd_create(bg_controllable_t * ctrl);

// bg_msg_queue_t * bg_osd_get_msg_queue(bg_osd_t * osd);

void bg_osd_destroy(bg_osd_t*); 

const bg_parameter_info_t * bg_osd_get_parameters(bg_osd_t*);
void bg_osd_set_parameter(void * data, const char * name,
                          const gavl_value_t * val);

// void bg_osd_set_overlay(bg_osd_t*, gavl_overlay_t *);

gavl_video_source_t * bg_osd_init(bg_osd_t * osd,
                                  const gavl_video_format_t * frame_format);

void bg_osd_update(bg_osd_t * osd);

void bg_osd_set_sink(bg_osd_t * osd, gavl_video_sink_t * sink);


// int bg_osd_overlay_valid(bg_osd_t*);

// void bg_osd_set_volume_changed(bg_osd_t*, float val);
// void bg_osd_set_brightness_changed(bg_osd_t*, float val);
// void bg_osd_set_contrast_changed(bg_osd_t*, float val);
// void bg_osd_set_saturation_changed(bg_osd_t*, float val);

void bg_osd_show_info(bg_osd_t * osd);
void bg_osd_show_time(bg_osd_t * osd);

void bg_osd_show_audio_menu(bg_osd_t * osd);
void bg_osd_show_subtitle_menu(bg_osd_t * osd);
void bg_osd_show_chapter_menu(bg_osd_t * osd);

void bg_osd_handle_messages(bg_osd_t * osd);

int bg_osd_key_pressed(bg_osd_t * osd, int key, int mask);

int bg_osd_clear(bg_osd_t * osd);

#endif // BG_OSD_H_INCLUDED
