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

#ifndef BG_GLVIDEO_H_INCLUDED
#define BG_GLVIDEO_H_INCLUDED


/* GL (ES) based video rendering engine. */

#include <EGL/egl.h>

#include <gavl/gavl.h>
#include <gmerlin/bgmsg.h>


typedef struct bg_glvideo_s bg_glvideo_t;

bg_glvideo_t * bg_glvideo_create(int type_mask, void * native_display, EGLSurface window_surface);
void bg_glvideo_destroy(bg_glvideo_t *);

gavl_video_sink_t * bg_glvideo_open(bg_glvideo_t *, gavl_video_format_t * fmt, int src_flags);
gavl_video_sink_t * bg_glvideo_add_overlay_stream(bg_glvideo_t *, gavl_video_format_t * fmt, int src_flags);

bg_controllable_t * bg_glvideo_get_controllable(bg_glvideo_t *);

void bg_glvideo_set_window_size(bg_glvideo_t *, int w, int h);

void bg_glvideo_window_coords_to_position(bg_glvideo_t * g, int x, int y, double * ret);

int bg_glvideo_handle_message(bg_glvideo_t * g, gavl_msg_t * msg);

/* re-draw last frame. This command is a noop if not in still
   image mode */
void bg_glvideo_redraw(bg_glvideo_t * g);

void bg_glvideo_close(bg_glvideo_t * g);

gavl_hw_context_t * bg_glvideo_get_hwctx(bg_glvideo_t * g);

#endif // BG_GLVIDEO_H_INCLUDED
