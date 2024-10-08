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



typedef struct transcoder_window_s transcoder_window_t;

transcoder_window_t * transcoder_window_create();

void transcoder_window_destroy(transcoder_window_t*);
void transcoder_window_run(transcoder_window_t *);
transcoder_window_t * transcoder_window_create();

void transcoder_window_destroy(transcoder_window_t*);
void transcoder_window_run(transcoder_window_t *);

void transcoder_window_load_profile(transcoder_window_t *,
                                    const char * file);
