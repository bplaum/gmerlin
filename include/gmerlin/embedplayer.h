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



#ifndef BG_EMBEDPLAYER_H_INCLUDED
#define BG_EMBEDPLAYER_H_INCLUDED

#include <gmerlin/parameter.h>
#include <gmerlin/bgmsg.h>
#include <gmerlin/cfgctx.h>

typedef struct bg_embed_player_s bg_embed_player_t;

bg_embed_player_t * bg_embed_player_create();
void bg_embed_player_destroy(bg_embed_player_t *);

void bg_embed_player_set_display_string(bg_embed_player_t *,
                                          const char * display_string);

void bg_embed_player_set_state_file(bg_embed_player_t * p,
                                    const char * f);

bg_controllable_t * bg_embed_player_get_ctrl(bg_embed_player_t *);


/* Start subprocess, can send messages then */
int bg_embed_player_run(bg_embed_player_t *);

int bg_embed_player_iteration(bg_embed_player_t * p);

bg_cfg_ctx_t * bg_embed_player_get_cfg(bg_embed_player_t * p);

void bg_embed_player_set_window_options(bg_embed_player_t * p,
                                        const char * window_name,
                                        const char * window_class,
                                        const char * icon_file);

int bg_embed_player_get_error(bg_embed_player_t * p);


#endif // BG_EMBEDPLAYER_H_INCLUDED

