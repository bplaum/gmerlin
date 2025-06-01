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



#include <config.h>
#include <stdio.h>
#include <string.h>

#include <stdlib.h>

#include <time.h>
#include <sys/time.h>

#include <X11/Xatom.h>
#include <gavl/metatags.h>

#include <gavl/keycodes.h>
#include <gmerlin/translation.h>
#include <gmerlin/utils.h>
#include <gmerlin/log.h>
#define LOG_DOMAIN "x11"
#include <gmerlin/state.h>
#include <gmerlin/plugin.h>
#include <gmerlin/pluginregistry.h>
#include <gmerlin/application.h>

#include <gmerlin/ov.h>

#include <x11/x11.h>
#include <x11/x11_window_private.h>


#define _NET_WM_STATE_REMOVE        0    /* remove/unset property */
#define _NET_WM_STATE_ADD           1    /* add/set property */

// #define FULLSCREEN_MODE_OLD    0
#define FULLSCREEN_MODE_NET_FULLSCREEN   (1<<0)
#define FULLSCREEN_MODE_NET_ABOVE        (1<<1)
#define FULLSCREEN_MODE_NET_STAYS_ON_TOP (1<<2)

#define FULLSCREEN_MODE_WIN_LAYER      (1<<2)

#define HAVE_XSHM


static char bm_no_data[] = { 0,0,0,0, 0,0,0,0 };

/* since it doesn't seem to be defined on some platforms */
int XShmGetEventBase (Display *);


static const bg_accelerator_t accels[] =
  {
    { GAVL_KEY_HOME,   GAVL_KEY_CONTROL_MASK, ACCEL_RESET_ZOOMSQUEEZE },
    { GAVL_KEY_HOME,                     0, ACCEL_FIT_WINDOW        },
    { GAVL_KEY_HOME,     GAVL_KEY_SHIFT_MASK, ACCEL_SHRINK_WINDOW     },
    { GAVL_KEY_PLUS,   GAVL_KEY_CONTROL_MASK, ACCEL_INC_SQUEEZE       },
    { GAVL_KEY_MINUS,  GAVL_KEY_CONTROL_MASK, ACCEL_DEC_SQUEEZE       },
    { GAVL_KEY_PLUS,       GAVL_KEY_ALT_MASK, ACCEL_INC_ZOOM          },
    { GAVL_KEY_MINUS,      GAVL_KEY_ALT_MASK, ACCEL_DEC_ZOOM          },

    { GAVL_KEY_b,        GAVL_KEY_CONTROL_MASK,                     ACCEL_DEC_BRIGHTNESS    },
    { GAVL_KEY_B,        GAVL_KEY_CONTROL_MASK|GAVL_KEY_SHIFT_MASK, ACCEL_INC_BRIGHTNESS    },
    { GAVL_KEY_s,        GAVL_KEY_CONTROL_MASK,                     ACCEL_DEC_SATURATION    },
    { GAVL_KEY_S,        GAVL_KEY_CONTROL_MASK|GAVL_KEY_SHIFT_MASK, ACCEL_INC_SATURATION    },
    { GAVL_KEY_c,        GAVL_KEY_CONTROL_MASK,                     ACCEL_DEC_CONTRAST      },
    { GAVL_KEY_C,        GAVL_KEY_CONTROL_MASK|GAVL_KEY_SHIFT_MASK, ACCEL_INC_CONTRAST      },
    { GAVL_KEY_NONE,                     0,  0                      },
  };

/* Callbacks */

void x11_window_accel_pressed(bg_x11_window_t * win, int id)
  {
  gavl_value_t delta, val;
  
  switch(id)
    {
    case ACCEL_RESET_ZOOMSQUEEZE:

      gavl_value_init(&delta);
      gavl_value_set_float(&delta, 100.0);
      
      bg_state_set(&win->state, 0, BG_STATE_CTX_OV, BG_STATE_OV_ZOOM, 
                   &delta, win->ctrl.evt_sink, BG_MSG_STATE_CHANGED);

      gavl_value_set_float(&delta, 0.0);
      
      bg_state_set(&win->state, 0, BG_STATE_CTX_OV, BG_STATE_OV_SQUEEZE, 
                   &delta, win->ctrl.evt_sink, BG_MSG_STATE_CHANGED);
      
      bg_x11_window_set_drawing_coords(win);

      return;
      break;
    case ACCEL_INC_ZOOM:
    case ACCEL_DEC_ZOOM:

      gavl_value_init(&delta);
      gavl_value_init(&val);

      if(id == ACCEL_INC_ZOOM)
        gavl_value_set_float(&delta, BG_OV_ZOOM_DELTA);
      else
        gavl_value_set_float(&delta, -BG_OV_ZOOM_DELTA);
      
      bg_state_add_value(&win->state, BG_STATE_CTX_OV, BG_STATE_OV_ZOOM, &delta, &val);
      bg_state_set(&win->state, 1, BG_STATE_CTX_OV, BG_STATE_OV_ZOOM,
                   &val, win->ctrl.evt_sink, BG_MSG_STATE_CHANGED);
      
      bg_x11_window_set_drawing_coords(win);
      
      return;
      break;
    case ACCEL_INC_SQUEEZE:
    case ACCEL_DEC_SQUEEZE:

      gavl_value_init(&delta);
      gavl_value_init(&val);

      if(id == ACCEL_INC_SQUEEZE)
        gavl_value_set_float(&delta, BG_OV_SQUEEZE_DELTA);
      else
        gavl_value_set_float(&delta, -BG_OV_SQUEEZE_DELTA);
      
      bg_state_add_value(&win->state, BG_STATE_CTX_OV, BG_STATE_OV_SQUEEZE, &delta, &val);
      bg_state_set(&win->state, 1, BG_STATE_CTX_OV, BG_STATE_OV_SQUEEZE,
                   &val, win->ctrl.evt_sink, BG_MSG_STATE_CHANGED);
      
      bg_x11_window_set_drawing_coords(win);
      return;
      break;
    case ACCEL_INC_BRIGHTNESS:
    case ACCEL_DEC_BRIGHTNESS:
      if(!TEST_FLAG(win, FLAG_VIDEO_OPEN) ||
         !(win->current_driver->flags & DRIVER_FLAG_BRIGHTNESS))
        return;

      gavl_value_init(&delta);
      gavl_value_init(&val);

      if(id == ACCEL_INC_BRIGHTNESS)
        gavl_value_set_float(&delta, BG_BRIGHTNESS_DELTA);
      else
        gavl_value_set_float(&delta, -BG_BRIGHTNESS_DELTA);
      
      bg_state_add_value(&win->state, BG_STATE_CTX_OV, BG_STATE_OV_BRIGHTNESS, &delta, &val);
      bg_state_set(&win->state, 1, BG_STATE_CTX_OV, BG_STATE_OV_BRIGHTNESS,
                   &val, win->ctrl.evt_sink, BG_MSG_STATE_CHANGED);
      
      bg_x11_window_set_brightness(win);
      break;
    case ACCEL_INC_SATURATION:
    case ACCEL_DEC_SATURATION:
      if(!TEST_FLAG(win, FLAG_VIDEO_OPEN) ||
         !(win->current_driver->flags & DRIVER_FLAG_SATURATION))
        return;

      gavl_value_init(&delta);
      gavl_value_init(&val);

      if(id == ACCEL_INC_SATURATION)
        gavl_value_set_float(&delta, BG_SATURATION_DELTA);
      else
        gavl_value_set_float(&delta, -BG_SATURATION_DELTA);
      
      bg_state_add_value(&win->state, BG_STATE_CTX_OV, BG_STATE_OV_SATURATION, &delta, &val);
      bg_state_set(&win->state, 1, BG_STATE_CTX_OV, BG_STATE_OV_SATURATION,
                   &val, win->ctrl.evt_sink, BG_MSG_STATE_CHANGED);
      
      bg_x11_window_set_saturation(win);
      break;
    case ACCEL_INC_CONTRAST:
    case ACCEL_DEC_CONTRAST:
      if(!TEST_FLAG(win, FLAG_VIDEO_OPEN) ||
         !(win->current_driver->flags & DRIVER_FLAG_CONTRAST))
        return;

      gavl_value_init(&delta);
      gavl_value_init(&val);

      if(id == ACCEL_INC_CONTRAST)
        gavl_value_set_float(&delta, BG_CONTRAST_DELTA);
      else
        gavl_value_set_float(&delta, -BG_CONTRAST_DELTA);
      
      bg_state_add_value(&win->state, BG_STATE_CTX_OV, BG_STATE_OV_CONTRAST, &delta, &val);
      bg_state_set(&win->state, 1, BG_STATE_CTX_OV, BG_STATE_OV_CONTRAST,
                   &val, win->ctrl.evt_sink, BG_MSG_STATE_CHANGED);
      
      bg_x11_window_set_contrast(win);
      break;

    case ACCEL_FIT_WINDOW:
      if(TEST_FLAG(win, FLAG_VIDEO_OPEN))
        {
        bg_x11_window_resize(win,
                             (win->video_format.image_width *
                              win->video_format.pixel_width) /
                             win->video_format.pixel_height,
                             win->video_format.image_height);
        }
      break;
    case ACCEL_SHRINK_WINDOW:
      if(TEST_FLAG(win, FLAG_VIDEO_OPEN))
        {
        float video_aspect;
        float window_aspect;
        
        video_aspect =
          (float)(win->video_format.image_width *
                  win->video_format.pixel_width) /
          (float)(win->video_format.image_height *
                  win->video_format.pixel_height);

        window_aspect = (float)(win->window_rect.w) /
          (float)(win->window_rect.h);

        if(window_aspect > video_aspect)
          {
          /* Remove black borders left and right */
          bg_x11_window_resize(win,
                               (int)(win->window_rect.h * video_aspect + 0.5),
                               win->window_rect.h);
          }
        else
          {
          /* Remove black borders top and bottom */
          bg_x11_window_resize(win,
                               win->window_rect.w,
                               (int)((float)win->window_rect.w /
                                     video_aspect + 0.5));
          }
        }
      break;
    default:
      {
      gavl_msg_t * msg;
      msg = bg_msg_sink_get(win->ctrl.evt_sink);
      gavl_msg_set_id_ns(msg, GAVL_MSG_GUI_ACCEL, GAVL_MSG_NS_GUI);
      gavl_msg_set_arg_int(msg, 0, id);
      bg_msg_sink_put(win->ctrl.evt_sink);
      }
      break;
    }
  }

/* Screensaver detection */

static int check_disable_screensaver(bg_x11_window_t * w)
  {
  if(!w->current)
    return 0;
  
  if(!TEST_FLAG(w, FLAG_IS_FULLSCREEN))
    return !!TEST_FLAG(w, FLAG_DISABLE_SCREENSAVER_NORMAL);
  else
    return !!TEST_FLAG(w, FLAG_DISABLE_SCREENSAVER_FULLSCREEN);
  }

void bg_x11_window_ping_screensaver(bg_x11_window_t * w)
  {
  }



static int
wm_check_capability(Display *dpy, Window root, Atom list, Atom wanted)
  {
  Atom            type;
  int             format;
  unsigned int    i;
  unsigned long   nitems, bytesafter;
  unsigned char   *args;
  unsigned long   *ldata;
  int             retval = 0;
  
  if (Success != XGetWindowProperty
      (dpy, root, list, 0, 16384, False,
       AnyPropertyType, &type, &format, &nitems, &bytesafter, &args))
    return 0;
  if (type != XA_ATOM)
    return 0;
  ldata = (unsigned long*)args;
  for (i = 0; i < nitems; i++)
    {
    if (ldata[i] == wanted)
      retval = 1;

    }
  XFree(ldata);
  return retval;
  }

void
bg_x11_window_set_netwm_state(Display * dpy, Window win, Window root, int action, Atom state)
  {
  /* Setting _NET_WM_STATE by XSendEvent works only, if the window
     is already mapped!! */
  
  XEvent e;
  memset(&e,0,sizeof(e));
  e.xclient.type = ClientMessage;
  e.xclient.message_type = XInternAtom(dpy, "_NET_WM_STATE", False);
  e.xclient.window = win;
  e.xclient.send_event = True;
  e.xclient.format = 32;
  e.xclient.data.l[0] = action;
  e.xclient.data.l[1] = state;
  
  XSendEvent(dpy, root, False,
             SubstructureRedirectMask | SubstructureNotifyMask, &e);
  }

static int get_fullscreen_mode(bg_x11_window_t * w)
  {
  int ret = 0;

  Atom            type;
  int             format;
  unsigned int    i;
  unsigned long   nitems, bytesafter;
  unsigned char   *args;
  unsigned long   *ldata;
  if (Success != XGetWindowProperty
      (w->dpy, w->root, w->_NET_SUPPORTED, 0, (65536 / sizeof(long)), False,
       AnyPropertyType, &type, &format, &nitems, &bytesafter, &args))
    return 0;
  if (type != XA_ATOM)
    return 0;
  ldata = (unsigned long*)args;
  for (i = 0; i < nitems; i++)
    {
    if (ldata[i] == w->_NET_WM_STATE_FULLSCREEN)
      {
      ret |= FULLSCREEN_MODE_NET_FULLSCREEN;
      }
    if (ldata[i] == w->_NET_WM_STATE_ABOVE)
      {
      ret |= FULLSCREEN_MODE_NET_ABOVE;
      }
    if (ldata[i] == w->_NET_WM_STATE_STAYS_ON_TOP)
      {
      ret |= FULLSCREEN_MODE_NET_STAYS_ON_TOP;
      }
    }
  XFree(ldata);
  
  if(wm_check_capability(w->dpy, w->root, w->WIN_PROTOCOLS,
                         w->WIN_LAYER))
    {
    ret |= FULLSCREEN_MODE_WIN_LAYER;
    }
  
  return ret;
  }

#define INIT_ATOM(a) w->a = XInternAtom(w->dpy, #a, False);

static void init_atoms(bg_x11_window_t * w)
  {
  INIT_ATOM(WM_DELETE_WINDOW);
  INIT_ATOM(WM_TAKE_FOCUS);
  INIT_ATOM(WIN_PROTOCOLS);
  INIT_ATOM(WM_PROTOCOLS);
  INIT_ATOM(WIN_LAYER);
  INIT_ATOM(_NET_SUPPORTED);
  INIT_ATOM(_NET_WM_STATE);
  INIT_ATOM(_NET_WM_STATE_FULLSCREEN);
  INIT_ATOM(_NET_WM_STATE_ABOVE);
  INIT_ATOM(_NET_WM_STATE_STAYS_ON_TOP);
  INIT_ATOM(_NET_MOVERESIZE_WINDOW);
  INIT_ATOM(WM_CLASS);
  INIT_ATOM(STRING);
  }

void bg_x11_window_set_fullscreen_mapped(bg_x11_window_t * win,
                                         window_t * w)
  {
  //  int x, y, wi, h;
  //  bg_x11_window_get_coords(win->dpy, w->win, &x, &y, &wi, &h);
  //  fprintf(stderr, "*** bg_x11_window_set_fullscreen_mapped\n");

  //  XMoveResizeWindow(win->dpy, win->current->win, -1, -1,
  //                    win->window_width+2, win->window_height+2);
  
  if(win->fullscreen_mode & FULLSCREEN_MODE_NET_ABOVE)
    {
    bg_x11_window_set_netwm_state(win->dpy, w->win, win->root,
                                  _NET_WM_STATE_ADD, win->_NET_WM_STATE_ABOVE);
    }
  else if(win->fullscreen_mode & FULLSCREEN_MODE_NET_STAYS_ON_TOP)
    {
    bg_x11_window_set_netwm_state(win->dpy, w->win, win->root,
                                  _NET_WM_STATE_ADD, win->_NET_WM_STATE_STAYS_ON_TOP);
    }
  if(win->fullscreen_mode & FULLSCREEN_MODE_NET_FULLSCREEN)
    {
    bg_x11_window_set_netwm_state(win->dpy, w->win, win->root,
                                  _NET_WM_STATE_ADD, win->_NET_WM_STATE_FULLSCREEN);
    }
  }

static void show_window(bg_x11_window_t * win, window_t * w, int show)
  {
  if(!w)
    return;

  //  fprintf(stderr, "show_window: %ld\n", w->win);
  
  if(!show)
    {
    
    if(w->win == None)
      return;
    
    if(w->win == w->toplevel)
      {
      XUnmapWindow(win->dpy, w->win);
      XWithdrawWindow(win->dpy, w->win,
                      DefaultScreen(win->dpy));
      }
    else
      XUnmapWindow(win->dpy, w->win);
    XSync(win->dpy, False);
    return;
    }
  

  CLEAR_FLAG(win, FLAG_TRACK_RESIZE);
  
  /* If the window was already mapped, raise it */
  if(w->mapped)
    XRaiseWindow(win->dpy, w->win);
  else
    {
    XMapWindow(win->dpy, w->win);

    //    fprintf(stderr, "XMapWindow %ld\n", w->win);

    //    XFlush(win->dpy);
    XSync(win->dpy, False);
    /* Wait until the window is actually mapped */
    if(!win->display_string_parent)
      {
      while(!w->mapped)
        bg_x11_window_handle_events(win, 100);
      }
    }
  
  SET_FLAG(win, FLAG_TRACK_RESIZE);
  }

void bg_x11_window_clear(bg_x11_window_t * win)
  {
  //  fprintf(stderr, "***** bg_x11_window_clear\n");
  //  XSetForeground(win->dpy, win->gc, win->black);
//  XFillRectangle(win->dpy, win->normal_window, win->gc, 0, 0, win->window_width, 
//                 win->window_height);  
  if(win->normal.win != None)
    {
    //    XClearArea(win->dpy, win->normal.win, 0, 0,
    //               win->window_width, win->window_height, False);
    XClearWindow(win->dpy, win->normal.win);
    }
  

  if(win->fullscreen.win != None)
    {
    //    XClearArea(win->dpy, win->fullscreen.win, -1, -1,
    //               win->window_width+2, win->window_height+2, False);
    XClearWindow(win->dpy, win->fullscreen.win);
    }
  //   XSync(win->dpy, False);
  }

void bg_x11_window_clear_border(bg_x11_window_t * w)
  {
  //  fprintf(stderr, "bg_x11_window_clear_border\n");
  XDrawRectangle(w->dpy, w->current->win,
                 w->gc, 0, 0, w->window_rect.w, w->window_rect.h);
  }

/* MWM decorations */

#define MWM_HINTS_DECORATIONS (1L << 1)
#define MWM_HINTS_FUNCTIONS     (1L << 0)

#define MWM_FUNC_ALL                 (1L<<0)
#define PROP_MOTIF_WM_HINTS_ELEMENTS 5
typedef struct
  {
  CARD32 flags;
  CARD32 functions;
  CARD32 decorations;
  INT32 inputMode;
    CARD32 status;
  } PropMotifWmHints;

static
int mwm_set_decorations(bg_x11_window_t * w, Window win, int set)
  {
  PropMotifWmHints motif_hints;
  Atom hintsatom;
  
  /* setup the property */
  motif_hints.flags = MWM_HINTS_DECORATIONS | MWM_HINTS_FUNCTIONS;
  motif_hints.decorations = set;
  motif_hints.functions   = set ? MWM_FUNC_ALL : 0;
  
  /* get the atom for the property */
  hintsatom = XInternAtom(w->dpy, "_MOTIF_WM_HINTS", False);
  
  XChangeProperty(w->dpy, win, hintsatom, hintsatom, 32, PropModeReplace,
                  (unsigned char *) &motif_hints, PROP_MOTIF_WM_HINTS_ELEMENTS);
  return 1;
  }

static void set_fullscreen(bg_x11_window_t * w, Window win)
  {
  if(w->fullscreen_mode & FULLSCREEN_MODE_NET_FULLSCREEN)
    {
    
    }
  else
    mwm_set_decorations(w, win, 0);
  }

static void set_min_size(bg_x11_window_t * w, Window win, int width, int height)
  {
  XSizeHints * h;
  h = XAllocSizeHints();

  h->flags = PMinSize;
  h->min_width = width;
  h->min_height = height;

  XSetWMNormalHints(w->dpy, win, h);
  
  XFree(h);
  }

static int open_display(bg_x11_window_t * w)
  {
  char * normal_id;
  char * fullscreen_id;
#ifdef HAVE_LIBXINERAMA
  int foo,bar;
#endif
  
  /*
   *  Display string is in the form
   *  <XDisplayName(DisplayString(dpy)>:<normal_id>:<fullscreen_id>
   *  It can be NULL. Also, <fullscreen_id> can be missing
   */
  
  if(!w->display_string_parent)
    {
    w->dpy = XOpenDisplay(NULL);
    if(!w->dpy)
      return 0;
    w->normal.parent = None;
    w->fullscreen.parent = None;
    }
  else
    {
    fullscreen_id = strrchr(w->display_string_parent, ':');
    if(!fullscreen_id)
      {
      return 0;
      }
    *fullscreen_id = '\0';
    fullscreen_id++;
    
    normal_id = strrchr(w->display_string_parent, ':');
    if(!normal_id)
      {
      return 0;
      }
    *normal_id = '\0';
    normal_id++;
    
    w->dpy = XOpenDisplay(w->display_string_parent);
    if(!w->dpy)
      return 0;
    
    w->normal.parent = strtoul(normal_id, NULL, 16);
    
    if(!(*fullscreen_id))
      w->fullscreen.parent = None;
    else
      w->fullscreen.parent = strtoul(fullscreen_id, NULL, 16);

    //    fprintf(stderr, "Initialized windows: %ld %ld\n",
    //            w->normal.parent, w->fullscreen.parent);
    }

  w->screen = DefaultScreen(w->dpy);
  w->root =   RootWindow(w->dpy, w->screen);

  if(w->normal.parent == None)
    w->normal.parent = w->root;
  if(w->fullscreen.parent == None)
    w->fullscreen.parent = w->root;
  
  w->normal.child = None;
  w->fullscreen.child = None;
  
  init_atoms(w);
  
  /* Check, which fullscreen modes we have */
  
  w->fullscreen_mode = get_fullscreen_mode(w);

  bg_x11_screensaver_init(&w->scr, w->dpy);
  
  /* Get xinerama screens */

#ifdef HAVE_LIBXINERAMA
  if (XineramaQueryExtension(w->dpy,&foo,&bar) &&
      XineramaIsActive(w->dpy))
    {
    w->xinerama = XineramaQueryScreens(w->dpy,&w->nxinerama);
    }
#endif

  return 1;
  }

#if 0
void bg_x11_window_get_coords(Display * dpy,
                              Window win,
                              int * x, int * y, int * width,
                              int * height)
  {
  Window root_return;
  Window parent_return;
  Window * children_return = NULL;
  unsigned int nchildren_return = 0;
  int x_return, y_return;
  unsigned int width_return, height_return;
  unsigned int border_width_return;
  unsigned int depth_return;
  int32_t * frame;
  
  //  Window child_return;

  fprintf(stderr, "Get coords 1\n");
  
  XGetGeometry(dpy, win, &root_return, &x_return, &y_return,
               &width_return, &height_return,
               &border_width_return, &depth_return);
  
  XQueryTree(dpy, win, &root_return, &parent_return,
             &children_return, &nchildren_return);

  fprintf(stderr, "Get coords 2 %ld %ld\n", root_return, parent_return);
  
  if(nchildren_return)
    XFree(children_return);
  
  if(x) *x = x_return;
  if(y) *y = y_return;
  
  if(width)  *width  = width_return;
  if(height) *height = height_return;
  
  if((x || y) && (parent_return != root_return))
    {
    fprintf(stderr, "Get coords 3\n");

    XGetGeometry(dpy, parent_return, &root_return,
                 &x_return, &y_return,
                 &width_return, &height_return,
                 &border_width_return, &depth_return);

    XQueryTree(dpy, parent_return, &root_return, &parent_return,
               &children_return, &nchildren_return);

    if(nchildren_return)
      XFree(children_return);
        
    fprintf(stderr, "Get coords 4 %ld %ld\n", root_return, parent_return);
    
    
    if(x) *x = x_return;
    if(y) *y = y_return;
    }
  }
#else

void bg_x11_window_get_coords(Display * dpy,
                              Window win,
                              int * x, int * y, int * width,
                              int * height)
  {
  Window root_return;
  Window child_return;
  int x_return, y_return;
  unsigned int width_return, height_return;
  unsigned int border_width_return;
  unsigned int depth_return;
  long * frame;

  Atom actual_type;
  int actual_format;
  unsigned long nitems, bytes_after;
  unsigned char *data = NULL;
  int result;
    
  XGetGeometry(dpy, win, &root_return, &x_return, &y_return,
               &width_return, &height_return,
               &border_width_return, &depth_return);

  XTranslateCoordinates(dpy, win, root_return, x_return, y_return,
                        &x_return, &y_return, &child_return);
  
  //  fprintf(stderr, "x: %d y: %d w: %d h: %d\n",
  //          x_return, y_return,
  //          width_return, height_return);

  if(x)
    *x = x_return;

  if(y)
    *y = y_return;

  if(width)
    *width = width_return;

  if(height)
    *height = height_return;
  
  result = XGetWindowProperty(dpy, win, XInternAtom(dpy, "_NET_FRAME_EXTENTS", False),
                              0, 4, False, AnyPropertyType, 
                              &actual_type, &actual_format, 
                              &nitems, &bytes_after, &data);
  
  if(result == Success)
    {
    if ((nitems == 4) && (bytes_after == 0))
      {
      frame = (long*)data;
      //      fprintf(stderr, "Frame: %ld %ld %ld %ld\n",
      //              frame[0], frame[1], frame[2], frame[3]);
      
      x_return -= frame[0];
      y_return -= frame[2];
      }
    
    XFree(data);
    }

  if(x)
    *x = x_return;

  if(y)
    *y = y_return;
 
  //  fprintf(stderr, "return: x: %d y: %d w: %d h: %d\n",
  //          *x, *y, *width, *height);

  }
#endif



static int window_is_viewable(Display * dpy, Window w)
  {
  XWindowAttributes attr;

  if(w == None)
    return 0;
  
  XGetWindowAttributes(dpy, w, &attr);
  if(attr.map_state == IsViewable)
    return 1;
  return 0;
  }


void bg_x11_window_size_changed(bg_x11_window_t * w)
  {
  /* Frame size remains unchanged, to let set_rectangles
     find out, if the image must be reallocated */

  /* Nothing changed actually. */
  if((w->window_format.image_width == w->window_rect.w) &&
     (w->window_format.image_height == w->window_rect.h))
    return;
  
  w->window_format.image_width  = w->window_rect.w;
  w->window_format.image_height = w->window_rect.h;
  
  bg_x11_window_set_drawing_coords(w);
  }

void bg_x11_window_init(bg_x11_window_t * w)
  {
  /* Decide current window */
  if((w->fullscreen.parent != w->root) &&
     window_is_viewable(w->dpy, w->fullscreen.parent))
    {
    //    fprintf(stderr, "Is fullscreen\n");
    
    w->current = &w->fullscreen;
    SET_FLAG(w, FLAG_IS_FULLSCREEN);
    }
  else
    {
    w->current = &w->normal;
    CLEAR_FLAG(w, FLAG_IS_FULLSCREEN);
    }

#if 1
  if(w->current->parent != w->root)
    {
    //    fprintf(stderr, "bg_x11_window_init %ld %ld\n", w->current->win,
    //            w->current->parent);
    bg_x11_window_get_coords(w->dpy, w->current->parent,
                             &w->window_rect.x, &w->window_rect.y,
                             &w->window_rect.w, &w->window_rect.h);

    fprintf(stderr, "XMoveResizeWindow 2 %d %d %d %d\n", 0, 0,
            w->window_rect.w, w->window_rect.h);

    XMoveResizeWindow(w->dpy, w->current->win, 0, 0,
                      w->window_rect.w, w->window_rect.h);
    }
  else
    bg_x11_window_get_coords(w->dpy, w->current->win,
                             &w->window_rect.x, &w->window_rect.y,
                             &w->window_rect.w, &w->window_rect.h);
#endif
  //  fprintf(stderr, "Window size: %dx%d+%d+%d\n", w->window_width, w->window_height, x, y);
  
  bg_x11_window_size_changed(w);

  
  }

static int create_window(bg_x11_window_t * w,
                         int width, int height)
  {
  unsigned long event_mask;
  XColor black;
  Atom wm_protocols[2];
  XWMHints * wmhints;
  
  //  int i;
  /* Stuff for making the cursor */
  
  XSetWindowAttributes attr;
  unsigned long attr_flags;

  const char * title;
  const gavl_value_t * title_val;
  char * icon_file;
  
  if((!w->dpy) && !open_display(w))
    return 0;

  
  wmhints = XAllocWMHints();
  
  wmhints->input = True;
  wmhints->initial_state = NormalState;
  wmhints->flags |= InputHint|StateHint;

  /* Creating windows without colormap results in a BadMatch error */
  w->black = BlackPixel(w->dpy, w->screen);
  w->colormap = XCreateColormap(w->dpy, RootWindow(w->dpy, w->screen),
                                w->visual,
                                AllocNone);
  
  /* Setup event mask */

  event_mask =
    StructureNotifyMask |
    PointerMotionMask |
    ExposureMask |
    ButtonPressMask |
    ButtonReleaseMask |
    PropertyChangeMask |
    KeyPressMask |
    KeyReleaseMask |
    FocusChangeMask;
  
  /* Create normal window */

  memset(&attr, 0, sizeof(attr));
  
  attr.backing_store = NotUseful;
  attr.border_pixel = 0;
  attr.background_pixel = 0;
  attr.event_mask = event_mask;
  attr.colormap = w->colormap;
  attr_flags = (CWBackingStore | CWEventMask | CWBorderPixel |
                CWBackPixel | CWColormap);
  
  wm_protocols[0] = w->WM_DELETE_WINDOW;
  wm_protocols[1] = w->WM_TAKE_FOCUS;
  
  /* Create normal window */

  //  fprintf(stderr, "Create normal window %d %d\n", width, height);
  w->normal.win = XCreateWindow(w->dpy, w->normal.parent,
                                0 /* x */,
                                0 /* y */,
                                width, height,
                                0 /* border_width */, w->depth,
                                InputOutput,
                                w->visual,
                                attr_flags,
                                &attr);

  /* Create GC */
  
  w->gc = XCreateGC(w->dpy, w->normal.win, 0, NULL);
  XSetForeground(w->dpy, w->gc, w->black);
  XSetForeground(w->dpy, w->gc, w->black);
  XSetLineAttributes(w->dpy, w->gc, 3, LineSolid, CapButt, JoinBevel);

  /* Icon */
  if((icon_file = bg_app_get_icon_file()))
    {
    gavl_video_frame_t * icon = NULL;
    gavl_video_format_t icon_format;
    
    if((icon = 
        bg_plugin_registry_load_image(bg_plugin_reg, icon_file, &icon_format, NULL)))
      {
      bg_x11_window_make_icon(w, icon, &icon_format, &w->icon, &w->icon_mask);
    
      wmhints->icon_pixmap = w->icon;
      wmhints->icon_mask   = w->icon_mask;
    
      if(wmhints->icon_pixmap != None)
        wmhints->flags |= IconPixmapHint;
    
      if(wmhints->icon_mask != None)
        wmhints->flags |= IconMaskHint;
      
      gavl_video_frame_destroy(icon);
      }
    else
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Couldn't load icon file %s", icon_file);

    free(icon_file);
    }
  else
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Got no application icon");
    //    gavl_dictionary_dump(&bg_app_vars, 2);
    }

  
  if(w->normal.parent == w->root)
    {
    //    set_decorations(w, w->normal.win, 1);
    XSetWMProtocols(w->dpy, w->normal.win, wm_protocols, 2);
    
    if(w->min_width && w->min_height)
      set_min_size(w, w->normal.win, w->min_width, w->min_height);

    w->normal.toplevel = w->normal.win;

    w->normal.focus_child = XCreateSimpleWindow(w->dpy, w->normal.win,
                                                  -1, -1, 1, 1, 0,
                                                  0, 0);
    XSelectInput(w->dpy, w->normal.focus_child,
                 KeyPressMask | KeyReleaseMask | FocusChangeMask);
    
    XSetWMHints(w->dpy, w->normal.win, wmhints);
    XMapWindow(w->dpy, w->normal.focus_child);
    
    }
  
  /* The fullscreen window will be created with the same size for now */
  
  w->fullscreen.win = XCreateWindow (w->dpy, w->fullscreen.parent,
                                     0 /* x */,
                                     0 /* y */,
                                     width, height,
                                     0 /* border_width */, w->depth,
                                     InputOutput, w->visual,
                                     attr_flags,
                                     &attr);

  w->fullscreen.fullscreen = 1;
  
  if(w->fullscreen.parent == w->root)
    {
    /* Setup protocols */
    
    set_fullscreen(w, w->fullscreen.win);
    XSetWMProtocols(w->dpy, w->fullscreen.win, wm_protocols, 2);
    w->fullscreen.toplevel = w->fullscreen.win;
    w->fullscreen.focus_child = XCreateSimpleWindow(w->dpy,
                                                    w->fullscreen.win,
                                                    -1, -1, 1, 1, 0,
                                                    0, 0);
    XSelectInput(w->dpy, w->fullscreen.focus_child,
                 KeyPressMask | KeyReleaseMask | FocusChangeMask);
    
    XSetWMHints(w->dpy, w->fullscreen.win, wmhints);
    XMapWindow(w->dpy, w->fullscreen.focus_child);
    }

  /* Set the final event masks now. We blocked SubstructureNotifyMask
   * before because we don't want our focus children as
   * embeded clients..
   */
  
  XSync(w->dpy, False);
  XSelectInput(w->dpy, w->normal.win,
               event_mask | SubstructureNotifyMask);
  XSelectInput(w->dpy, w->fullscreen.win,
               event_mask | SubstructureNotifyMask);

  XSetWindowBackground(w->dpy, w->normal.win, w->black);
  XSetWindowBackground(w->dpy, w->fullscreen.win, w->black);
  
  
  /* Create colormap and fullscreen cursor */
  
  black.pixel = BlackPixel(w->dpy, w->screen);
  XQueryColor(w->dpy, DefaultColormap(w->dpy, w->screen), &black);
  
  w->fullscreen_cursor_pixmap =
    XCreateBitmapFromData(w->dpy, w->fullscreen.win,
                          bm_no_data, 8, 8);

  
  w->fullscreen_cursor=
    XCreatePixmapCursor(w->dpy, w->fullscreen_cursor_pixmap,
                        w->fullscreen_cursor_pixmap,
                        &black, &black, 0, 0);
  
  
  XFree(wmhints);
  
  /* Determine if we are in fullscreen mode */
  bg_x11_window_init(w);

  if((title_val = bg_state_get(&w->state,
                               BG_STATE_CTX_OV,
                               BG_STATE_OV_TITLE)) &&
     (title = gavl_value_get_string(title_val)))
    bg_x11_window_set_title(w, title);
  
  return 1;
  }


static void get_fullscreen_coords(bg_x11_window_t * w,
                                  int * x, int * y, int * width, int * height)
  {
#ifdef HAVE_LIBXINERAMA
  int x_return, y_return;
  int i;
  Window child;
  /* Get the coordinates of the normal window */

  *x = 0;
  *y = 0;
  *width  = DisplayWidth(w->dpy, w->screen);
  *height = DisplayHeight(w->dpy, w->screen);
  
  if(w->nxinerama)
    {
    XTranslateCoordinates(w->dpy, w->normal.win, w->root, 0, 0,
                          &x_return,
                          &y_return,
                          &child);
    
    /* Get the xinerama screen we are on */
    
    for(i = 0; i < w->nxinerama; i++)
      {
      if((x_return >= w->xinerama[i].x_org) &&
         (y_return >= w->xinerama[i].y_org) &&
         (x_return < w->xinerama[i].x_org + w->xinerama[i].width) &&
         (y_return < w->xinerama[i].y_org + w->xinerama[i].height))
        {
        *x = w->xinerama[i].x_org;
        *y = w->xinerama[i].y_org;
        *width = w->xinerama[i].width;
        *height = w->xinerama[i].height;
        break;
        }
      }
    }
#else
  *x = 0;
  *y = 0;
  *width  = DisplayWidth(w->dpy, w->screen);
  *height = DisplayHeight(w->dpy, w->screen);
#endif
  }

int bg_x11_window_set_fullscreen(bg_x11_window_t * w,int fullscreen)
  {
  int ret = 0;
  int width;
  int height;
  int x;
  int y;

  //  fprintf(stderr, "bg_x11_window_set_fullscreen %d\n", fullscreen);
  
  /* Toggle */
  if((fullscreen != 0) && (fullscreen != 1))
    {
    if(TEST_FLAG(w, FLAG_IS_FULLSCREEN))
      fullscreen = 0;
    else
      fullscreen = 1;
    }
    
  /* Return early if there is nothing to do */
  if(!!fullscreen == !!TEST_FLAG(w, FLAG_IS_FULLSCREEN))
    return 0;
  
  /* Normal->fullscreen */
  
  if(fullscreen)
    {
    if(w->normal.parent != w->root)
      return 0;
    get_fullscreen_coords(w, &x, &y, &width, &height);

    w->normal_width = w->window_rect.w;
    w->normal_height = w->window_rect.h;
    
    w->window_rect.w = width;
    w->window_rect.h = height;
    w->window_rect.x = 0;
    w->window_rect.y = 0;
    
    w->current = &w->fullscreen;

    SET_FLAG(w, FLAG_IS_FULLSCREEN);
    SET_FLAG(w, FLAG_NEED_FULLSCREEN);
    bg_x11_window_show(w, 1);
    
    bg_x11_window_clear(w);
    
    /* Hide old window */
    if(w->normal.win == w->normal.toplevel)
      XWithdrawWindow(w->dpy, w->normal.toplevel, w->screen);
    
    XFlush(w->dpy);
    ret = 1;
    
    }
  else
    {
    if(w->fullscreen.parent != w->root)
      return 0;
    /* Unmap fullscreen window */
#if 1
    bg_x11_window_set_netwm_state(w->dpy, w->fullscreen.win, w->root,
                                  _NET_WM_STATE_REMOVE, w->_NET_WM_STATE_FULLSCREEN);

    bg_x11_window_set_netwm_state(w->dpy, w->fullscreen.win, w->root,
                                  _NET_WM_STATE_REMOVE, w->_NET_WM_STATE_ABOVE);
    XUnmapWindow(w->dpy, w->fullscreen.win);
#endif
    XWithdrawWindow(w->dpy, w->fullscreen.win, w->screen);
    
    /* Map normal window */
    w->current = &w->normal;
    CLEAR_FLAG(w, FLAG_IS_FULLSCREEN);

    bg_x11_window_get_coords(w->dpy,
                             w->normal.win,
                             &w->window_rect.x,
                             &w->window_rect.y,
                             &w->window_rect.w,
                             &w->window_rect.h);
    
    w->window_rect.w  = w->normal_width;
    w->window_rect.h = w->normal_height;
    
    bg_x11_window_show(w, 1);
    
    bg_x11_window_clear(w);
    XFlush(w->dpy);
    ret = 1;
    
    }

  if(ret)
    {
    if(check_disable_screensaver(w))
      bg_x11_screensaver_disable(&w->scr);
    else
      bg_x11_screensaver_enable(&w->scr);
    }
  bg_x11_window_set_drawing_coords(w);
  
  return ret;
  }

void bg_x11_window_resize(bg_x11_window_t * win,
                          int width, int height)
  {
  win->normal_width = width;
  win->normal_height = height;
  if(!TEST_FLAG(win, FLAG_IS_FULLSCREEN) &&
     (win->normal.parent == win->root))
    {
    win->window_rect.w = width;
    win->window_rect.h = height;

    XResizeWindow(win->dpy, win->normal.win, width, height);

    
#if 0
    if(win->normal.mapped)
      
      XResizeWindow(win->dpy, win->normal.win, width, height);
      {
      XWindowChanges xwc;
      memset(&xwc, 0, sizeof(xwc));
      xwc.width = width;
      xwc.height = height;
      XConfigureWindow(win->dpy, win->normal.win, CWWidth|CWHeight, &xwc);
      XSync(win->dpy, False);
      //      fprintf(stderr, "Resized mapped window: %d %d\n",
      //              win->window_rect.w, win->window_rect.h);

      }
    else
      SET_FLAG(win, FLAG_RESIZE_PENDING);
#endif
    }
  }

void bg_x11_window_get_size(bg_x11_window_t * win, int * width, int * height)
  {
  *width = win->window_rect.w;
  *height = win->window_rect.h;
  }

static int handle_message(void * priv, gavl_msg_t * msg)
  {
  bg_x11_window_t * win = priv;

  switch(msg->NS)
    {
    case BG_MSG_NS_STATE:
      switch(msg->ID)
        {
        case BG_CMD_SET_STATE:
          {
          const char * var;
          const char * ctx;
          gavl_value_t val;
          int last = 0;
          gavl_value_init(&val);
          
          gavl_msg_get_state(msg,
                           &last,
                           &ctx,
                           &var,
                           &val,
                           NULL);
#if 0
          fprintf(stderr, "x11_window_set_state: %s %s %d ", ctx, var, last);
          gavl_value_dump(&val, 0);
          fprintf(stderr, "\n");
#endif     
          if(!strcmp(ctx, BG_STATE_CTX_OV))
            {
            
            /* Handle toggle */
            if(!strcmp(var, BG_STATE_OV_FULLSCREEN))
              {
              int fs = 0;
              if(!gavl_value_get_int(&val, &fs))
                {
                gavl_value_free(&val);
                return 1;
                }

              fs &= 1;
              gavl_value_set_int(&val, fs);
              }
            else if(!strcmp(var, BG_STATE_OV_VISIBLE))
              {
              if(!win->have_state_ov)
                gavl_value_set_int(&val, 0);
              }
            
            /* Store locally and broadcast */
            bg_state_set(&win->state,
                         last, ctx, var, &val,
                         win->ctrl.evt_sink, BG_MSG_STATE_CHANGED);
            
            if(!strcmp(var, BG_STATE_OV_FULLSCREEN))
              {
              int fs = 0;
              if(gavl_value_get_int(&val, &fs))
                {
                int visible = 0;
                const gavl_value_t * delt;
                
                if(win->dpy &&
                   (delt = bg_state_get(&win->state, BG_STATE_CTX_OV, BG_STATE_OV_VISIBLE)) &&
                   gavl_value_get_int(delt, &visible) &&
                   visible)
                  bg_x11_window_set_fullscreen(win, fs);
                }
              }
            else if(!strcmp(var, BG_STATE_OV_CONTRAST))
              {
              bg_x11_window_set_contrast(win);
              }
            else if(!strcmp(var, BG_STATE_OV_BRIGHTNESS))
              {
              bg_x11_window_set_brightness(win);
              }
            else if(!strcmp(var, BG_STATE_OV_SATURATION))
              {
              bg_x11_window_set_saturation(win);
              }
            else if(!strcmp(var, BG_STATE_OV_ZOOM))
              {
              bg_x11_window_set_drawing_coords(win);
              }
            else if(!strcmp(var, BG_STATE_OV_SQUEEZE))
              {
              bg_x11_window_set_drawing_coords(win);
              }
            else if(!strcmp(var, BG_STATE_OV_TITLE))
              {
              const char * title;
              if((title = gavl_value_get_string(&val)) && win->dpy)
                bg_x11_window_set_title(win, title);
              }
            else if(!strcmp(var, BG_STATE_OV_VISIBLE))
              {
              int fs = 0;
              const gavl_value_t * val1;
              
              int visible = 0;
              if(gavl_value_get_int(&val, &visible) && win->dpy)
                {
                bg_x11_window_show(win, visible);
                
                if(visible &&
                   (val1 = bg_state_get(&win->state, BG_STATE_CTX_OV, BG_STATE_OV_FULLSCREEN)) &&
                   gavl_value_get_int(val1, &fs))
                  bg_x11_window_set_fullscreen(win, fs);
                }
              }

            if(last && !win->have_state_ov)
              {
              //              fprintf(stderr, "Got state\n");
              win->have_state_ov = 1;
              }
            }
          
          gavl_value_free(&val);
          }
          break;
        }
      break;
    }
  return 1;
  }

/* Public methods */

bg_x11_window_t * bg_x11_window_create(const char * display_string,
                                       const bg_accelerator_map_t * accel_map)
  {
  bg_x11_window_t * ret;
  ret = calloc(1, sizeof(*ret));
  ret->display_string_parent = gavl_strrep(ret->display_string_parent, display_string);

  ret->accel_map = bg_accelerator_map_create();

  if(accel_map)
    bg_accelerator_map_append_array(ret->accel_map, bg_accelerator_map_get_accels(accel_map));
  
  bg_accelerator_map_append_array(ret->accel_map, accels);
  
  ret->icon      = None;
  ret->icon_mask = None;

  bg_controllable_init(&ret->ctrl,
                       bg_msg_sink_create(handle_message, ret, 1),
                       bg_msg_hub_create(1));

  bg_state_init_ctx(&ret->state, BG_STATE_CTX_OV, bg_ov_state_vars);
  
  return ret;
  }

bg_controllable_t * bg_x11_window_get_controllable(bg_x11_window_t * w)
  {
  return &w->ctrl;
  }


void bg_x11_window_destroy(bg_x11_window_t * w)
  {
  bg_x11_window_cleanup_video(w);
  
  if(w->colormap != None)
    XFreeColormap(w->dpy, w->colormap);
  
  if(w->normal.win != None)
    XDestroyWindow(w->dpy, w->normal.win);
  
  if(w->fullscreen.win != None)
    XDestroyWindow(w->dpy, w->fullscreen.win);
  
  if(w->fullscreen_cursor != None)
    XFreeCursor(w->dpy, w->fullscreen_cursor);
  
  if(w->fullscreen_cursor_pixmap != None)
    XFreePixmap(w->dpy, w->fullscreen_cursor_pixmap);

  if(w->gc != None)
    XFreeGC(w->dpy, w->gc);

  
#ifdef HAVE_LIBXINERAMA
  if(w->xinerama)
    XFree(w->xinerama);
#endif

  
  if(w->icon != None)
    XFreePixmap(w->dpy, w->icon);
  
  if(w->icon_mask != None)
    XFreePixmap(w->dpy, w->icon_mask);
  
  if(w->dpy)
    {
    XCloseDisplay(w->dpy);
    bg_x11_screensaver_cleanup(&w->scr);
    }
  if(w->display_string_parent)
    free(w->display_string_parent);

  bg_controllable_cleanup(&w->ctrl);

  gavl_dictionary_free(&w->state);

  if(w->accel_map)
    bg_accelerator_map_destroy(w->accel_map);
  
  free(w);
  }

static const bg_parameter_info_t common_parameters[] =
  {
    {
      BG_LOCALE,
      .name =        "window",
      .long_name =   TRS("General"),
    },
    {
      .name =        "auto_resize",
      .long_name =   TRS("Auto resize window"),
      .type =        BG_PARAMETER_CHECKBUTTON,
      .val_default = GAVL_VALUE_INIT_INT(1)
    },
    {
      .name =        "window_width",
      .long_name =   "Window width",
      .type =        BG_PARAMETER_INT,
      .flags =       BG_PARAMETER_HIDE_DIALOG,
      .val_default = GAVL_VALUE_INIT_INT(320)
    },
    {
      .name =        "window_height",
      .long_name =   "Window height",
      .type =        BG_PARAMETER_INT,
      .flags =       BG_PARAMETER_HIDE_DIALOG,
      .val_default = GAVL_VALUE_INIT_INT(240)
    },
    {
      .name =        "window_x",
      .long_name =   "Window x",
      .type =        BG_PARAMETER_INT,
      .flags =       BG_PARAMETER_HIDE_DIALOG,
      .val_default = GAVL_VALUE_INIT_INT(100)
    },
    {
      .name =        "window_y",
      .long_name =   "Window y",
      .type =        BG_PARAMETER_INT,
      .flags =       BG_PARAMETER_HIDE_DIALOG,
      .val_default = GAVL_VALUE_INIT_INT(100)
    },
    {
      .name =        "disable_xscreensaver_normal",
      .long_name =   TRS("Disable Screensaver for normal playback"),
      .type =        BG_PARAMETER_CHECKBUTTON,
      .val_default = GAVL_VALUE_INIT_INT(0)
    },
    {
      .name =        "disable_xscreensaver_fullscreen",
      .long_name =   TRS("Disable Screensaver for fullscreen playback"),
      .type =        BG_PARAMETER_CHECKBUTTON,
      .val_default = GAVL_VALUE_INIT_INT(1)
    },
#ifdef HAVE_EGL
    {
      .name =        "background_color",
      .long_name =   TRS("Background color"),
      .type =        BG_PARAMETER_COLOR_RGB,
      .flags =       BG_PARAMETER_SYNC,
      .help_string = TRS("Specify the background color for videos with alpha channel. This is only used by the OpenGL driver."),

    },
#endif
    { /* End of parameters */ },
  };


const bg_parameter_info_t * bg_x11_window_get_parameters(bg_x11_window_t * win)
  {
  return common_parameters;
  }

void
bg_x11_window_set_parameter(void * data, const char * name,
                            const gavl_value_t * val)
  {
  bg_x11_window_t * win;
  if(!name)
    return;
  win = (bg_x11_window_t *)data;

  if(!strcmp(name, "auto_resize"))
    {
    if(val->v.i)
      SET_FLAG(win, FLAG_AUTO_RESIZE);
    else
      CLEAR_FLAG(win, FLAG_AUTO_RESIZE);
    }
  else if(!strcmp(name, "disable_xscreensaver_normal"))
    {
    if(val->v.i)
      SET_FLAG(win, FLAG_DISABLE_SCREENSAVER_NORMAL);
    else
      CLEAR_FLAG(win, FLAG_DISABLE_SCREENSAVER_NORMAL);
    }
  else if(!strcmp(name, "disable_xscreensaver_fullscreen"))
    {
    if(val->v.i)
      SET_FLAG(win, FLAG_DISABLE_SCREENSAVER_FULLSCREEN);
    else
      CLEAR_FLAG(win, FLAG_DISABLE_SCREENSAVER_FULLSCREEN);
    }
#ifdef HAVE_EGL
  else if(!strcmp(name, "background_color"))
    {
    memcpy(win->background_color, val->v.color, 3 * sizeof(val->v.color[0]));
    }
#endif
  
  else if(!strcmp(name, "window_width"))
    win->window_rect.w = val->v.i;
  else if(!strcmp(name, "window_height"))
    win->window_rect.h = val->v.i;
  
  
  
  }


void bg_x11_window_set_size(bg_x11_window_t * win, int width, int height)
  {
  win->window_rect.w = width;
  win->window_rect.h = height;
  }


int bg_x11_window_realize(bg_x11_window_t * win)
  {
  int ret;
  
  if(!win->dpy && !open_display(win))
    return 0;
  
  win->visual = DefaultVisual(win->dpy, win->screen);
  win->depth = DefaultDepth(win->dpy, win->screen);
  
  gavl_log(GAVL_LOG_DEBUG, LOG_DOMAIN, "Got Visual 0x%lx depth %d",
         win->visual->visualid, win->depth);

  /* TODO: Zero size windows aren't mapped at all */
  //  win->window_rect.w = 100;
  //  win->window_rect.h = 100;
  
  ret = create_window(win, win->window_rect.w, win->window_rect.h);
  //  bg_x11_window_init_gl(win);
  return ret;
  }


void bg_x11_window_set_title(bg_x11_window_t * w, const char * title)
  {
  if(w->normal.parent == w->root)
    XmbSetWMProperties(w->dpy, w->normal.win, title,
                       title, NULL, 0, NULL, NULL, NULL);
  if(w->fullscreen.parent == w->root)
    XmbSetWMProperties(w->dpy, w->fullscreen.win, title,
                       title, NULL, 0, NULL, NULL, NULL);
  }

void bg_x11_window_set_options(bg_x11_window_t * w,
                               const char * name, 
                               const char * klass,
                               const gavl_video_frame_t * icon,
                               const gavl_video_format_t * icon_format)
  {

  /* Set Class hints */
  if(name && klass)
    {
    XClassHint xclasshint;

    /* const madness */
    xclasshint.res_name = gavl_strdup(name);
    xclasshint.res_class = gavl_strdup(klass);

    if(w->normal.parent == w->root)
      XSetClassHint(w->dpy, w->normal.win, &xclasshint);

    if(w->fullscreen.parent == w->root)
      XSetClassHint(w->dpy, w->fullscreen.win, &xclasshint);

    free(xclasshint.res_name);
    free(xclasshint.res_class);
    }

  /* Set Icon */
  if(icon && icon_format)
    {
    XWMHints xwmhints;
    memset(&xwmhints, 0, sizeof(xwmhints));

    if((w->normal.parent == w->root) ||
       (w->fullscreen.parent == w->root))
      {
      if(w->icon != None)
        {
        XFreePixmap(w->dpy, w->icon);
        w->icon = None; 
        }
      if(w->icon_mask != None)
        {
        XFreePixmap(w->dpy, w->icon_mask);
        w->icon_mask = None; 
        }
      
      bg_x11_window_make_icon(w,
                              icon,
                              icon_format,
                              &w->icon,
                              &w->icon_mask);
      
      xwmhints.icon_pixmap = w->icon;
      xwmhints.icon_mask   = w->icon_mask;
      
      if(xwmhints.icon_pixmap != None)
        xwmhints.flags |= IconPixmapHint;
      
      if(xwmhints.icon_mask != None)
        xwmhints.flags |= IconMaskHint;
      
      if(w->normal.parent == w->root)
        XSetWMHints(w->dpy, w->normal.win, &xwmhints);
      if(w->fullscreen.parent == w->root)
        XSetWMHints(w->dpy, w->fullscreen.win, &xwmhints);
      }
    }
  }

void bg_x11_window_show(bg_x11_window_t * win, int show)
  {
  if(show && (win->current->win == win->fullscreen.win))
    SET_FLAG(win, FLAG_NEED_FULLSCREEN);
  
  show_window(win, win->current, show);
  
  if(show && check_disable_screensaver(win))
    bg_x11_screensaver_disable(&win->scr);
  else
    bg_x11_screensaver_enable(&win->scr);
  
  if(win->dpy)
    {
    XSync(win->dpy, False);
    bg_x11_window_handle_events(win, 0);
    }
  }

Window bg_x11_window_get_toplevel(bg_x11_window_t * w, Window win)
  {
  Window *children_return;
  Window root_return;
  Window parent_return;
  Atom     type_ret;
  int      format_ret;
  unsigned long   nitems_ret;
  unsigned long   bytes_after_ret;
  unsigned char  *prop_ret;
  
  unsigned int nchildren_return;
  while(1)
    {
    XGetWindowProperty(w->dpy, win,
                       w->WM_CLASS, 0L, 0L, 0,
                       w->STRING,
                       &type_ret,&format_ret,&nitems_ret,
                       &bytes_after_ret,&prop_ret);
    if(type_ret!=None)
      {
      XFree(prop_ret);
      return win;
      }
    XQueryTree(w->dpy, win, &root_return, &parent_return,
               &children_return, &nchildren_return);
    if(nchildren_return)
      XFree(children_return);
    if(parent_return == root_return)
      break;
    win = parent_return;
    }
  return win;
  }

gavl_pixelformat_t 
bg_x11_window_get_pixelformat(Display * dpy, Visual * visual, int depth)
  {
  int bpp;
  XPixmapFormatValues * pf;
  int i;
  int num_pf;
  gavl_pixelformat_t ret = GAVL_PIXELFORMAT_NONE;
  
  bpp = 0;
  pf = XListPixmapFormats(dpy, &num_pf);
  for(i = 0; i < num_pf; i++)
    {
    if(pf[i].depth == depth)
      bpp = pf[i].bits_per_pixel;
    }
  XFree(pf);
  
  ret = GAVL_PIXELFORMAT_NONE;
  switch(bpp)
    {
    case 16:
      if((visual->red_mask == 63488) &&
         (visual->green_mask == 2016) &&
         (visual->blue_mask == 31))
        ret = GAVL_RGB_16;
      else if((visual->blue_mask == 63488) &&
              (visual->green_mask == 2016) &&
              (visual->red_mask == 31))
        ret = GAVL_BGR_16;
      break;
    case 24:
      if((visual->red_mask == 0xff) && 
         (visual->green_mask == 0xff00) &&
         (visual->blue_mask == 0xff0000))
        ret = GAVL_RGB_24;
      else if((visual->red_mask == 0xff0000) && 
         (visual->green_mask == 0xff00) &&
         (visual->blue_mask == 0xff))
        ret = GAVL_BGR_24;
      break;
    case 32:
      if((visual->red_mask == 0xff) && 
         (visual->green_mask == 0xff00) &&
         (visual->blue_mask == 0xff0000))
        ret = GAVL_RGB_32;
      else if((visual->red_mask == 0xff0000) && 
         (visual->green_mask == 0xff00) &&
         (visual->blue_mask == 0xff))
        ret = GAVL_BGR_32;
      break;
    }
  return ret;
  }

void bg_x11_window_set_drawing_coords(bg_x11_window_t * win)
  {
  double f_tmp;
  const gavl_value_t * val;
  float zoom_factor;
  float squeeze_factor;
  
  gavl_rectangle_f_t src_rect_f;
  gavl_rectangle_i_t dst_rect;

  if(!TEST_FLAG(win, FLAG_VIDEO_OPEN))
    return;
     
  if((val = bg_state_get(&win->state, BG_STATE_CTX_OV, BG_STATE_OV_ZOOM)) &&
     gavl_value_get_float(val, &f_tmp))
    zoom_factor = f_tmp * 0.01;
  else
    zoom_factor = 1.0;
  
  if((val = bg_state_get(&win->state, BG_STATE_CTX_OV, BG_STATE_OV_SQUEEZE)) &&
     gavl_value_get_float(val, &f_tmp))
    squeeze_factor = f_tmp;
  else
    squeeze_factor = 0.0;

  win->window_format.image_width = win->window_rect.w;
  win->window_format.image_height = win->window_rect.h;
  gavl_video_format_set_frame_size(&win->window_format, 0, 0);
  
  gavl_rectangle_f_set_all(&src_rect_f, &win->video_format_n);

#if 0  
  if(priv->keep_aspect)
    {
#endif 
    gavl_rectangle_fit_aspect(&dst_rect,
                              &win->video_format_n,
                              &src_rect_f,
                              &win->window_format,
                              zoom_factor, squeeze_factor);
#if 0  
    }
  else
    {
    gavl_rectangle_i_set_all(&priv->dst_rect, &priv->window_format);
    }
#endif 
  
  gavl_rectangle_crop_to_format_scale(&src_rect_f,
                                      &dst_rect,
                                      &win->video_format_n,
                                      &win->window_format);
  
  bg_x11_window_set_rectangles(win, &src_rect_f, &dst_rect);

  SET_FLAG(win, FLAG_NEED_REDRAW);
  
  }
