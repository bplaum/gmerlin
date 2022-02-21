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

#include <gmerlin/parameter.h>
#include <gmerlin/accelerator.h>
#include <gmerlin/msgqueue.h>

#include <gavl/gavl.h>
#include <gavl/connectors.h>

/* OpenGL display attributes (see bg_x11_window_set_gl_attribute()) */

#if 0
#define BG_GL_ATTRIBUTE_BUFFER_SIZE       0 /**< Depth of the color buffer. */
#define BG_GL_ATTRIBUTE_LEVEL             1 /**< Level in plane stacking. */
#define BG_GL_ATTRIBUTE_RGBA              2 /**< True if RGBA mode. */
#define BG_GL_ATTRIBUTE_DOUBLEBUFFER      3 /**< Double buffering supported. */
#define BG_GL_ATTRIBUTE_STEREO            4 /**< Stereo buffering supported. */
#define BG_GL_ATTRIBUTE_AUX_BUFFERS       5 /**< Number of aux buffers. */
#define BG_GL_ATTRIBUTE_RED_SIZE          6 /**< Number of red component bits. */
#define BG_GL_ATTRIBUTE_GREEN_SIZE        7 /**< Number of green component bits. */
#define BG_GL_ATTRIBUTE_BLUE_SIZE         8 /**< Number of blue component bits. */
#define BG_GL_ATTRIBUTE_ALPHA_SIZE        9 /**< Number of alpha component bits. */
#define BG_GL_ATTRIBUTE_DEPTH_SIZE       10 /**< Number of depth bits. */
#define BG_GL_ATTRIBUTE_STENCIL_SIZE     11 /**< Number of stencil bits. */
#define BG_GL_ATTRIBUTE_ACCUM_RED_SIZE   12 /**< Number of red accum bits. */
#define BG_GL_ATTRIBUTE_ACCUM_GREEN_SIZE 13 /**< Number of green accum bits. */
#define BG_GL_ATTRIBUTE_ACCUM_BLUE_SIZE  14 /**< Number of blue accum bits. */
#define BG_GL_ATTRIBUTE_ACCUM_ALPHA_SIZE 15 /**< Number of alpha accum bits. */
#define BG_GL_ATTRIBUTE_NUM              16 /* Must be last and highest */
#endif

typedef struct bg_x11_window_s bg_x11_window_t;

bg_x11_window_t * bg_x11_window_create(const char * display_string,
                                       const bg_accelerator_map_t * accel_map);

/* For attribute artgument see BG_GL_ATTRIBUTE_ above */
//void bg_x11_window_set_gl_attribute(bg_x11_window_t * win, int attribute, int value);

const char * bg_x11_window_get_display_string(bg_x11_window_t * w);

void bg_x11_window_destroy(bg_x11_window_t *);

const bg_parameter_info_t * bg_x11_window_get_parameters(bg_x11_window_t *);

void
bg_x11_window_set_parameter(void * data, const char * name,
                            const gavl_value_t * val);
int
bg_x11_window_get_parameter(void * data, const char * name,
                            gavl_value_t * val);

void bg_x11_window_set_drawing_coords(bg_x11_window_t * win);

void bg_x11_window_set_size(bg_x11_window_t *, int width, int height);

void bg_x11_window_clear(bg_x11_window_t *);

int bg_x11_window_realize(bg_x11_window_t *);

/* Handle X11 events, callbacks are called from here */

void bg_x11_window_set_state(bg_x11_window_t * win,
                        gavl_dictionary_t * state,
                        const char * state_ctx);

void bg_x11_window_handle_events(bg_x11_window_t*, int milliseconds);

int bg_x11_window_set_fullscreen(bg_x11_window_t * w,int fullscreen);
void bg_x11_window_set_title(bg_x11_window_t * w, const char * title);
void bg_x11_window_set_options(bg_x11_window_t * w,
                               const char * name, const char * klass,
                               const gavl_video_frame_t * icon,
                               const gavl_video_format_t * icon_format);

void bg_x11_window_show(bg_x11_window_t * w, int show);

void bg_x11_window_resize(bg_x11_window_t * win, int width, int height);
void bg_x11_window_get_size(bg_x11_window_t * win, int * width, int * height);


/*
 *   All opengl calls must be enclosed by x11_window_set_gl() and
 *   x11_window_unset_gl()
 */

// void bg_x11_window_set_gl(bg_x11_window_t *);
// void bg_x11_window_unset_gl(bg_x11_window_t *);

// int bg_x11_window_start_gl(bg_x11_window_t * win);
// void bg_x11_window_stop_gl(bg_x11_window_t * win);


/*
 *  Swap buffers and make your rendered work visible
 */
void bg_x11_window_swap_gl(bg_x11_window_t *);

// void bg_x11_window_cleanup_gl(bg_x11_window_t *);

/* For Video output */

int bg_x11_window_open_video(bg_x11_window_t*, gavl_video_format_t * format);

gavl_video_sink_t *
bg_x11_window_add_overlay_stream(bg_x11_window_t*,
                                 gavl_video_format_t * format);


gavl_video_sink_t * bg_x11_window_get_sink(bg_x11_window_t*);

void bg_x11_window_set_rectangles(bg_x11_window_t * w,
                                  gavl_rectangle_f_t * src_rect,
                                  gavl_rectangle_i_t * dst_rect);

void bg_x11_window_set_brightness(bg_x11_window_t*);
void bg_x11_window_set_saturation(bg_x11_window_t*);
void bg_x11_window_set_contrast(bg_x11_window_t*);

void bg_x11_window_put_frame(bg_x11_window_t*, gavl_video_frame_t * frame);
void bg_x11_window_put_still(bg_x11_window_t*, gavl_video_frame_t * frame);

void bg_x11_window_close_video(bg_x11_window_t*);

bg_controllable_t * bg_x11_window_get_controllable(bg_x11_window_t*);

/* Grab window */

typedef struct bg_x11_grab_window_s bg_x11_grab_window_t;

const bg_parameter_info_t *
bg_x11_grab_window_get_parameters(bg_x11_grab_window_t * win);

void bg_x11_grab_window_set_parameter(void * data, const char * name,
                                      const gavl_value_t * val);

int bg_x11_grab_window_get_parameter(void * data, const char * name,
                                     gavl_value_t * val);


bg_x11_grab_window_t * bg_x11_grab_window_create();
void bg_x11_grab_window_destroy(bg_x11_grab_window_t *);

int bg_x11_grab_window_init(bg_x11_grab_window_t *, gavl_video_format_t * format);

gavl_source_status_t
bg_x11_grab_window_grab(void *, gavl_video_frame_t ** frame);

void bg_x11_grab_window_close(bg_x11_grab_window_t * win);
