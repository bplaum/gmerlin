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



#ifndef BG_X11_WINDOW_PRIVATE_H_INCLUDED
#define BG_X11_WINDOW_PRIVATE_H_INCLUDED

#include <config.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#ifdef HAVE_LIBXINERAMA
#include <X11/extensions/Xinerama.h>
#endif

#ifdef HAVE_XDPMS
#include <X11/extensions/dpms.h>
#endif

#define SCREENSAVER_MODE_XLIB  0 // MUST be 0 (fallback)
#define SCREENSAVER_MODE_GNOME 1
#define SCREENSAVER_MODE_KDE   2
#define SCREENSAVER_MODE_XTEST 3

#ifdef HAVE_GLX
#include <GL/glx.h>
#endif

#ifdef HAVE_EGL
#include <EGL/egl.h>
#endif


// #include <X11/extensions/XShm.h>


/* Screensaver module */

typedef struct
  {
  Display * dpy;
  int mode;
  int disabled;
  int was_enabled;
  int saved_timeout;
  int64_t last_ping_time;

  int fake_motion;
  
  gavl_timer_t * timer;

#ifdef HAVE_XDPMS
  int dpms_disabled;
#endif
  } bg_x11_screensaver_t;

void
bg_x11_screensaver_init(bg_x11_screensaver_t *, Display * dpy);

void
bg_x11_screensaver_enable(bg_x11_screensaver_t *);

void
bg_x11_screensaver_disable(bg_x11_screensaver_t *);

void
bg_x11_screensaver_ping(bg_x11_screensaver_t *);

void
bg_x11_screensaver_cleanup(bg_x11_screensaver_t *);

/* Overlay stream */

typedef struct
  {
  gavl_overlay_t * ovl;
  gavl_overlay_t * ovl_hw;
  
  gavl_video_format_t format;
  bg_x11_window_t * win;
  gavl_video_sink_t * sink;
  int active;
  } overlay_stream_t;

typedef struct video_driver_s video_driver_t;

#define DRIVER_FLAG_BRIGHTNESS (1<<0)
#define DRIVER_FLAG_SATURATION (1<<1)
#define DRIVER_FLAG_CONTRAST   (1<<2)

typedef struct
  {
  int flags;
  const video_driver_t * driver;
  gavl_pixelformat_t * img_formats;
  gavl_pixelformat_t * ovl_formats;
  void * priv;
  int can_scale;
  bg_x11_window_t * win;
  
  
  /* Selected pixelformat (used by the core only) */
  gavl_pixelformat_t pixelformat;
  int penalty;
  } driver_data_t;

#ifdef HAVE_LIBVA
extern const video_driver_t vaapi_driver;
#endif


extern const video_driver_t ximage_driver;

#ifdef HAVE_LIBXV
extern const video_driver_t xv_driver;
#endif

#if defined(HAVE_GLX) || defined(HAVE_EGL)
extern const video_driver_t gl_driver;
extern const video_driver_t gles_driver;
#endif

#ifdef HAVE_MMAL
extern const video_driver_t mmal_driver;
#endif

#define MAX_DRIVERS 5

struct video_driver_s
  {
  const char * name;
  
  int can_scale;
  int (*init)(driver_data_t* data);
  int (*open)(driver_data_t* data);
  
  void (*init_overlay_stream)(driver_data_t* data, overlay_stream_t * str);
  
  int (*supports_hw)(driver_data_t* data, gavl_hw_context_t * ctx);
  
  gavl_video_frame_t * (*create_overlay)(driver_data_t* d, overlay_stream_t * str);
  void (*destroy_overlay)(driver_data_t* data, overlay_stream_t * str,
                          gavl_video_frame_t*);

  void (*set_overlay)(driver_data_t* d, overlay_stream_t * str);
  void (*unset_overlay)(driver_data_t* d, overlay_stream_t * str);
  
  gavl_video_frame_t * (*create_frame)(driver_data_t* data);
  
  void (*destroy_frame)(driver_data_t* data, gavl_video_frame_t *);
  
  void (*set_frame)(driver_data_t* data, gavl_video_frame_t * frame);
  void (*put_frame)(driver_data_t* data);

  void (*set_brightness)(driver_data_t* data,float brightness);
  void (*set_saturation)(driver_data_t* data,float saturation);
  void (*set_contrast)(driver_data_t* data,float contrast);
  
  void (*close)(driver_data_t* data);
  void (*cleanup)(driver_data_t* data);
  };

/*
 *  We have 2 windows: One normal window and one
 *  fullscreen window. We optionally talk to both the
 *  children and parents through the XEmbed protocol
 */

typedef struct
  {
  Window win;      /* Actual window */
  Window parent;   /* Parent (if non-root we are embedded) */
  Window child;    /* Child window */

  /* Toplevel window we are inside (e.g. window manager decoration) */
  Window toplevel; 
  
  /* Subwindow (created with another visual than the default) */
  Window subwin; 
  
  Window focus_child;  /* Focus proxy */
#ifdef HAVE_GLX
  GLXWindow glx_win;
#endif

#ifdef HAVE_EGL
  EGLSurface egl_surface;
#endif
  
  int parent_xembed;
  int child_xembed;
  int mapped;
  int fullscreen;
  bg_accelerator_map_t * child_accel_map;
  int modality;
  } window_t;

/* Keyboard accelerators */

#define ACCEL_RESET_ZOOMSQUEEZE  3<<8
#define ACCEL_INC_ZOOM           4<<8
#define ACCEL_DEC_ZOOM           5<<8
#define ACCEL_INC_SQUEEZE        6<<8
#define ACCEL_DEC_SQUEEZE        7<<8
#define ACCEL_INC_BRIGHTNESS     8<<8
#define ACCEL_DEC_BRIGHTNESS     9<<8
#define ACCEL_INC_SATURATION    10<<8
#define ACCEL_DEC_SATURATION    11<<8
#define ACCEL_INC_CONTRAST      12<<8
#define ACCEL_DEC_CONTRAST      13<<8
#define ACCEL_FIT_WINDOW        16<<8
#define ACCEL_SHRINK_WINDOW     17<<8


#define FLAG_IS_FULLSCREEN                  (1<<0)
#define FLAG_DO_DELETE                      (1<<1)
#define FLAG_POINTER_HIDDEN                 (1<<2)
#define FLAG_AUTO_RESIZE                    (1<<3)
#define FLAG_DISABLE_SCREENSAVER_NORMAL     (1<<5)
#define FLAG_DISABLE_SCREENSAVER_FULLSCREEN (1<<6)
#define FLAG_VIDEO_OPEN                     (1<<9)
#define FLAG_DRIVERS_INITIALIZED            (1<<10)
#define FLAG_NEED_FOCUS                     (1<<11)
#define FLAG_NEED_FULLSCREEN                (1<<12)
#define FLAG_OVERLAY_CHANGED                (1<<15)
#define FLAG_NO_GET_FRAME                   (1<<16)
#define FLAG_CLEAR_BORDER                   (1<<17)
#define FLAG_TRACK_RESIZE                   (1<<18)
#define FLAG_ZEROCOPY                       (1<<19)
#define FLAG_NEED_REDRAW                    (1<<21)

#define SET_FLAG(w, flag) w->flags |= (flag)
#define CLEAR_FLAG(w, flag) w->flags &= ~(flag)
#define TEST_FLAG(w, flag) (w->flags & (flag))

struct bg_x11_window_s
  {
  int flags;

  /* User settable stuff (initialized before x11_window_create) */
  
  int min_width;
  int min_height;
  
#ifdef HAVE_LIBXINERAMA
  XineramaScreenInfo *xinerama;
  int                nxinerama;
#endif
  
  unsigned long black;  
  Display * dpy;
  GC gc;

  window_t normal;
  window_t fullscreen;
  window_t * current;
  
  Window root;

  gavl_rectangle_i_t window_rect;
  
  int normal_width, normal_height;
  int screen;

  gavl_dictionary_t state;
  int have_state_ov;
  int have_state_x11;
  
  /* Fullscreen stuff */

  int fullscreen_mode;
  Pixmap fullscreen_cursor_pixmap;
  Cursor fullscreen_cursor;

  Atom WM_DELETE_WINDOW;
  Atom WM_TAKE_FOCUS;
  Atom _NET_SUPPORTED;
  Atom _NET_WM_STATE;
  Atom _NET_WM_STATE_FULLSCREEN;
  Atom _NET_WM_STATE_STAYS_ON_TOP;
  Atom _NET_WM_STATE_ABOVE;
  Atom _NET_MOVERESIZE_WINDOW;
  Atom WIN_PROTOCOLS;
  Atom WM_PROTOCOLS;
  Atom WIN_LAYER;
  Atom _XEMBED_INFO;
  Atom _XEMBED;
  Atom STRING;
  Atom WM_CLASS;
  
  /* For hiding the mouse pointer */
  int idle_counter;
  
  /* Screensaver stuff */
    
  char * display_string_parent;
  char * display_string_child;

  Colormap colormap;
  Colormap sub_colormap;
  
  bg_accelerator_map_t * accel_map;
  
  Visual * visual;
  int depth;

  /* OpenGL stuff */  
#if defined(HAVE_GLX) || defined(HAVE_EGL)
  float background_color[3];
#endif

  gavl_video_format_t video_format;
  /* Video format with normalized orientation */
  gavl_video_format_t video_format_n;
  
  gavl_video_format_t window_format;
  
  gavl_rectangle_f_t src_rect;
  gavl_rectangle_i_t dst_rect;
  
  driver_data_t drivers[MAX_DRIVERS];
  
  driver_data_t * current_driver;
  
  /* For asynchronous focus grabbing */
  Time focus_time;
  
  /* Overlay stuff */
  int num_overlay_streams;
  overlay_stream_t ** overlay_streams;

  gavl_video_sink_t * sink;
  
  float brightness;
  float saturation;
  float contrast;
  
  Pixmap icon;
  Pixmap icon_mask;

  /* Screensaver */
  bg_x11_screensaver_t scr;

  /* Frame from the sink */
  gavl_video_frame_t * frame;
  
  bg_controllable_t ctrl;

#if 0  
  /* Zoom/Squeeze */
  float zoom;
  float squeeze;
  float hue;
  float brightness;
  float saturation;
  float contrast;
#endif

  
  };

void bg_x11_window_put_frame_internal(bg_x11_window_t * win,
                                      gavl_video_frame_t * frame);

/* Private functions */

void bg_x11_window_handle_event(bg_x11_window_t * win, XEvent * evt);
void bg_x11_window_ping_screensaver(bg_x11_window_t * w);
void bg_x11_window_get_coords(Display * dpy,
                              Window win,
                              int * x, int * y, int * width,
                              int * height);
void bg_x11_window_init(bg_x11_window_t * w);

gavl_pixelformat_t
bg_x11_window_get_pixelformat(Display * dpy, Visual * visual, int depth);

void bg_x11_window_make_icon(bg_x11_window_t * win,
                             const gavl_video_frame_t * icon,
                             const gavl_video_format_t * icon_format,
                             Pixmap * icon_ret, Pixmap * mask_ret);


int bg_x11_window_check_shm(Display * dpy, int * completion_type);


void bg_x11_window_size_changed(bg_x11_window_t * w);

void bg_x11_window_cleanup_video(bg_x11_window_t * w);

Window bg_x11_window_get_toplevel(bg_x11_window_t * w, Window win);

void bg_x11_window_send_xembed_message(bg_x11_window_t * w, Window win, long time,
                                       int type, int detail, int data1, int data2);

void bg_x11_window_embed_parent(bg_x11_window_t * win,
                                window_t * w);
void bg_x11_window_embed_child(bg_x11_window_t * win,
                               window_t * w);

int bg_x11_window_check_embed_property(bg_x11_window_t * win,
                                       window_t * w);

void bg_x11_window_set_fullscreen_mapped(bg_x11_window_t * win,
                                         window_t * w);

void
bg_x11_window_set_netwm_state(Display * dpy, Window win, Window root, int action, Atom state);


void bg_x11_window_create_subwins(bg_x11_window_t * w,
                                  int depth, Visual * v);

void bg_x11_window_destroy_subwins(bg_x11_window_t * w);
void bg_x11_window_clear_border(bg_x11_window_t * w);

void x11_window_accel_pressed(bg_x11_window_t * win, int id);

/* For OpenGL support */

// int bg_x11_window_init_gl(bg_x11_window_t *);

#define XEMBED_MAPPED                   (1 << 0)

/* XEMBED messages */
#define XEMBED_EMBEDDED_NOTIFY		0
#define XEMBED_WINDOW_ACTIVATE  	1
#define XEMBED_WINDOW_DEACTIVATE  	2
#define XEMBED_REQUEST_FOCUS	 	3
#define XEMBED_FOCUS_IN 	 	4
#define XEMBED_FOCUS_OUT  		5
#define XEMBED_FOCUS_NEXT 		6
#define XEMBED_FOCUS_PREV 		7
/* 8-9 were used for XEMBED_GRAB_KEY/XEMBED_UNGRAB_KEY */
#define XEMBED_MODALITY_ON 		10
#define XEMBED_MODALITY_OFF 		11
#define XEMBED_REGISTER_ACCELERATOR     12
#define XEMBED_UNREGISTER_ACCELERATOR   13
#define XEMBED_ACTIVATE_ACCELERATOR     14

/* Modifiers field for XEMBED_REGISTER_ACCELERATOR */
#define XEMBED_MODIFIER_SHIFT    (1 << 0)
#define XEMBED_MODIFIER_CONTROL  (1 << 1)
#define XEMBED_MODIFIER_ALT      (1 << 2)
#define XEMBED_MODIFIER_SUPER    (1 << 3)
#define XEMBED_MODIFIER_HYPER    (1 << 4)


#endif // BG_X11_WINDOW_PRIVATE_H_INCLUDED
