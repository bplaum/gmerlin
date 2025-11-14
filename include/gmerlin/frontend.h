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


#ifndef BG_FRONTEND_H_INCLUDED
#define BG_FRONTEND_H_INCLUDED

#include <gmerlin/parameter.h>
#include <gmerlin/httpserver.h>

typedef struct bg_frontend_s bg_frontend_t;


void bg_frontend_destroy(bg_frontend_t *);
int bg_frontend_ping(bg_frontend_t *);

int bg_frontend_set_option(gavl_array_t * frontends, const char * opt);

bg_frontend_t ** bg_frontends_create(bg_controllable_t * ctrl,
                                     int type_mask, gavl_array_t * frontends, int * num);

int bg_frontends_ping(bg_frontend_t **, int num_frontends);
void bg_frontends_destroy(bg_frontend_t **, int num_frontends);

/* gmerlin frontend */
int bg_frontend_gmerlin_ping(void * data);
void * bg_frontend_gmerlin_create(void);
void bg_frontend_gmerlin_destroy(void * priv);
int bg_frontend_gmerlin_open_mdb(void * data, bg_controllable_t * ctrl);
int bg_frontend_gmerlin_open_renderer(void * data, bg_controllable_t * ctrl);

#endif // BG_FRONTEND_H_INCLUDED
