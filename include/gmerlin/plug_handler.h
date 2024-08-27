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



#ifndef BG_PLUG_HANDLER_H_INCLUDED
#define BG_PLUG_HANDLER_H_INCLUDED

typedef struct bg_plug_handler_s bg_plug_handler_t;

bg_plug_handler_t * bg_plug_handler_create(bg_http_server_t * srv);

void bg_plug_handler_destroy(bg_plug_handler_t * h);

void bg_plug_handler_add_uris(bg_plug_handler_t *, gavl_dictionary_t * track);

#endif // BG_PLUG_HANDLER_H_INCLUDED

