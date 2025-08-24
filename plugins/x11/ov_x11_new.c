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

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <string.h>
#include <errno.h>

#include <config.h>
#include <gmerlin/translation.h>


#include <gmerlin/plugin.h>
#include <gmerlin/glvideo.h>
#include <gmerlin/state.h>

#include <gavl/log.h>
#define LOG_DOMAIN "ov_x11"

#include <gavl/utils.h>
#include <gavl/state.h>
#include <gavl/keycodes.h>
#include <gavl/msg.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>

#define FLAG_MAPPED        (1<<0)
#define FLAG_CURSOR_HIDDEN (1<<1)

typedef struct
  {
  Display * dpy;
  Window win;
  bg_glvideo_t * g;
  gavl_video_sink_t * sink;
  int flags;

  bg_controllable_t ctrl;
  gavl_dictionary_t state;

  Cursor cur_invisible;

  gavl_time_t last_active_time;
  gavl_timer_t * timer;
  
  } x11_t;

static void handle_events_x11(void * priv);

static Cursor create_invisible_cursor(Display *display, Window window)
  {
  Pixmap pixmap;
  XColor black;
  Cursor invisible_cursor;
  static char nodata[] = { 0,0,0,0,0,0,0,0 };
    
  // Schwarze Farbe definieren
  black.red = black.green = black.blue = 0;
    
  // 1x1 Pixmap erstellen
  pixmap = XCreateBitmapFromData(display, window, nodata, 8, 8);
    
  // Unsichtbaren Cursor erstellen
  invisible_cursor = XCreatePixmapCursor(display, pixmap, pixmap, 
                                                &black, &black, 0, 0);
    
  // Pixmap freigeben
  XFreePixmap(display, pixmap);
    
  return invisible_cursor;
  }

static void show_cursor(x11_t * x11)
  {
  
  if(!(x11->flags & FLAG_CURSOR_HIDDEN))
    return;

  x11->flags &= ~FLAG_CURSOR_HIDDEN;
  
  XUndefineCursor(x11->dpy, x11->win);
  }

static void hide_cursor(x11_t * x11)
  {
  if(x11->flags & FLAG_CURSOR_HIDDEN)
    return;

  x11->flags |= FLAG_CURSOR_HIDDEN;

  if(x11->cur_invisible == None)
    {
    x11->cur_invisible = create_invisible_cursor(x11->dpy, x11->win);
    }

  XDefineCursor(x11->dpy, x11->win, x11->cur_invisible);
  }


static int ensure_window(void * priv)
  {
  x11_t * x11 = priv;
  int screen;
  Window root;
  
  if(x11->dpy)
    return 1;

  if(!(x11->dpy = XOpenDisplay(NULL)))
    {
    if(errno != 0)
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Cannot connect to X-Server: %s", strerror(errno));
    else
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Cannot connect to X-Server: Unknown error");
    return 0;
    }
  screen = DefaultScreen(x11->dpy);
  root = RootWindow(x11->dpy, screen);

  x11->win = XCreateSimpleWindow(x11->dpy, root, 
                                 100, 100, 800, 600, 1,
                                 BlackPixel(x11->dpy, screen),
                                 BlackPixel(x11->dpy, screen));

  //  XStoreName(display, window, "Xlib Vollbild-Demo");
  
  // Select events
  XSelectInput(x11->dpy, x11->win, KeyPressMask | ExposureMask | StructureNotifyMask | PointerMotionMask);
  
  x11->g = bg_glvideo_create(EGL_PLATFORM_X11_KHR,
                             x11->dpy, &x11->win);
  
  return 1;
  }

static void map_sync(x11_t * x11)
  {
  XMapWindow(x11->dpy, x11->win);
  XSync(x11->dpy, False);

  while(!(x11->flags & FLAG_MAPPED))
    {
    if(gavl_fd_can_read(ConnectionNumber(x11->dpy), 100))
      handle_events_x11(x11);
    }
  }

static int handle_cmd(void * priv, gavl_msg_t * cmd)
  {
  x11_t * x11 = priv;

  if(x11->g && bg_glvideo_handle_message(x11->g, cmd))
    return 1;
  
  switch(cmd->NS)
    {
    case GAVL_MSG_NS_SINK:
      switch(cmd->ID)
        {
        case GAVL_MSG_SINK_RESYNC:
          if(x11->g)
            bg_glvideo_resync(x11->g);
          break;
        }
      break;
    case GAVL_MSG_NS_STATE:
      {
      switch(cmd->ID)
        {
        case GAVL_CMD_SET_STATE:
          {
          int last = 0;
          const char * ctx = NULL;
          const char * var = NULL;
          gavl_value_t val;
          gavl_value_init(&val);

          gavl_msg_get_state(cmd,
                             &last,
                             &ctx, &var, &val, NULL);

          
          if(!strcmp(ctx, BG_STATE_CTX_OV))
            {
            if(!strcmp(var, BG_STATE_OV_FULLSCREEN))
              {
              int fs = 0;
              Atom wm_state;
              Atom fullscreen;
              XEvent event;
              
              if(!gavl_value_get_int(&val, &fs) ||
                 !x11->dpy)
                return 1;

              wm_state = XInternAtom(x11->dpy, "_NET_WM_STATE", False);
              fullscreen = XInternAtom(x11->dpy, "_NET_WM_STATE_FULLSCREEN", False);
              
              
              memset(&event, 0, sizeof(event));
              
              event.type = ClientMessage;
              event.xclient.window = x11->win;
              event.xclient.message_type = wm_state;
              event.xclient.format = 32;
              event.xclient.data.l[0] = 2;    // _NET_WM_STATE_TOGGLE
              event.xclient.data.l[1] = fullscreen;
              event.xclient.data.l[2] = 0;
              
              XSendEvent(x11->dpy, DefaultRootWindow(x11->dpy), False,
                         SubstructureNotifyMask | SubstructureRedirectMask,
                         &event);
              }
            else if(!strcmp(var, BG_STATE_OV_TITLE))
              {
              const char * str;
              
              if(x11->dpy && (str = gavl_value_get_string(&val)))
                XStoreName(x11->dpy, x11->win, str);
              }
            else if(!strcmp(var, BG_STATE_OV_VISIBLE))
              {
              int visible = 0;

              if(!x11->dpy)
                return 1;
              
              if(gavl_value_get_int(&val, &visible) && visible)
                {
                map_sync(x11);
                }
              else
                {
                XUnmapWindow(x11->dpy, x11->win);
                XSync(x11->dpy, False);
                handle_events_x11(x11);
                }
              }
            else
              {
#if 0
              fprintf(stderr, "X11 set state %s %s\n", ctx, var);
              gavl_value_dump(&val, 2);
              fprintf(stderr, "\n");
#endif
              }
            }
          gavl_value_free(&val);
          }
          break;
        }
      }
    }
  return 1;
  }

static void * create_x11()
  {
  x11_t * x11 = calloc(1, sizeof(*x11));

  x11->cur_invisible = None; // Unneccesary but well..
  
  bg_controllable_init(&x11->ctrl,
                       bg_msg_sink_create(handle_cmd, x11, 1),
                       bg_msg_hub_create(1));

  x11->timer = gavl_timer_create();
  gavl_timer_start(x11->timer);
  
  return x11;
  }

static void destroy_x11(void * priv)
  {
  x11_t * x11 = priv;

  
  if(x11->g)
    bg_glvideo_destroy(x11->g);
  
  if(x11->dpy)
    {
    if(x11->cur_invisible != None)
      XFreeCursor(x11->dpy, x11->cur_invisible);
    
    XDestroyWindow(x11->dpy, x11->win);
    XCloseDisplay(x11->dpy);
    }

  bg_controllable_cleanup(&x11->ctrl);
  
  free(x11);
  
  }

static gavl_hw_context_t * get_hw_context_x11(void * priv)
  {
  x11_t * x11 = priv;
  if(!ensure_window(x11))
    return NULL;
  return bg_glvideo_get_hwctx(x11->g);
  }


static gavl_video_sink_t *
add_overlay_stream_x11(void * priv, gavl_video_format_t * format)
  {
  x11_t * x11 = priv;
  return bg_glvideo_add_overlay_stream(x11->g, format, 0);
  }

static gavl_video_sink_t * get_sink_x11(void * priv)
  {
  x11_t * x11 = priv;
  return x11->sink;
  }

/* Key mappings */

#define STATE_IGNORE ~(Mod2Mask)

static const struct
  {
  KeySym x11;
  int bg;
  }
keysyms[] = 
  {
    { XK_0, GAVL_KEY_0 },
    { XK_1, GAVL_KEY_1 },
    { XK_2, GAVL_KEY_2 },
    { XK_3, GAVL_KEY_3 },
    { XK_4, GAVL_KEY_4 },
    { XK_5, GAVL_KEY_5 },
    { XK_6, GAVL_KEY_6 },
    { XK_7, GAVL_KEY_7 },
    { XK_8, GAVL_KEY_8 },
    { XK_9, GAVL_KEY_9 },
    { XK_space,  GAVL_KEY_SPACE },
    { XK_Return, GAVL_KEY_RETURN },
    { XK_Left,   GAVL_KEY_LEFT },
    { XK_Right,  GAVL_KEY_RIGHT },
    { XK_Up,     GAVL_KEY_UP },
    { XK_Down,   GAVL_KEY_DOWN },
    { XK_Page_Up,  GAVL_KEY_PAGE_UP },
    { XK_Page_Down, GAVL_KEY_PAGE_DOWN },
    { XK_Home,  GAVL_KEY_HOME },
    { XK_plus,  GAVL_KEY_PLUS },
    { XK_minus, GAVL_KEY_MINUS },
    { XK_Tab,   GAVL_KEY_TAB },
    { XK_Escape,   GAVL_KEY_ESCAPE },
    { XK_Menu,     GAVL_KEY_MENU },
    
    { XK_question,   GAVL_KEY_QUESTION }, //!< ?
    { XK_exclam,     GAVL_KEY_EXCLAM    }, //!< !
    { XK_quotedbl,   GAVL_KEY_QUOTEDBL,   }, //!< "
    { XK_dollar,     GAVL_KEY_DOLLAR,     }, //!< $
    { XK_percent,    GAVL_KEY_PERCENT,    }, //!< %
    { XK_ampersand,  GAVL_KEY_APMERSAND,  }, //!< &
    { XK_slash,      GAVL_KEY_SLASH,      }, //!< /
    { XK_parenleft,  GAVL_KEY_LEFTPAREN,  }, //!< (
    { XK_parenright, GAVL_KEY_RIGHTPAREN, }, //!< )
    { XK_equal,      GAVL_KEY_EQUAL,      }, //!< =
    { XK_backslash,  GAVL_KEY_BACKSLASH,  }, //!< :-)
    { XK_BackSpace,  GAVL_KEY_BACKSPACE,  }, //!< :-)
    { XK_a,        GAVL_KEY_a },
    { XK_b,        GAVL_KEY_b },
    { XK_c,        GAVL_KEY_c },
    { XK_d,        GAVL_KEY_d },
    { XK_e,        GAVL_KEY_e },
    { XK_f,        GAVL_KEY_f },
    { XK_g,        GAVL_KEY_g },
    { XK_h,        GAVL_KEY_h },
    { XK_i,        GAVL_KEY_i },
    { XK_j,        GAVL_KEY_j },
    { XK_k,        GAVL_KEY_k },
    { XK_l,        GAVL_KEY_l },
    { XK_m,        GAVL_KEY_m },
    { XK_n,        GAVL_KEY_n },
    { XK_o,        GAVL_KEY_o },
    { XK_p,        GAVL_KEY_p },
    { XK_q,        GAVL_KEY_q },
    { XK_r,        GAVL_KEY_r },
    { XK_s,        GAVL_KEY_s },
    { XK_t,        GAVL_KEY_t },
    { XK_u,        GAVL_KEY_u },
    { XK_v,        GAVL_KEY_v },
    { XK_w,        GAVL_KEY_w },
    { XK_x,        GAVL_KEY_x },
    { XK_y,        GAVL_KEY_y },
    { XK_z,        GAVL_KEY_z },

    { XK_A,        GAVL_KEY_A },
    { XK_B,        GAVL_KEY_B },
    { XK_C,        GAVL_KEY_C },
    { XK_D,        GAVL_KEY_D },
    { XK_E,        GAVL_KEY_E },
    { XK_F,        GAVL_KEY_F },
    { XK_G,        GAVL_KEY_G },
    { XK_H,        GAVL_KEY_H },
    { XK_I,        GAVL_KEY_I },
    { XK_J,        GAVL_KEY_J },
    { XK_K,        GAVL_KEY_K },
    { XK_L,        GAVL_KEY_L },
    { XK_M,        GAVL_KEY_M },
    { XK_N,        GAVL_KEY_N },
    { XK_O,        GAVL_KEY_O },
    { XK_P,        GAVL_KEY_P },
    { XK_Q,        GAVL_KEY_Q },
    { XK_R,        GAVL_KEY_R },
    { XK_S,        GAVL_KEY_S },
    { XK_T,        GAVL_KEY_T },
    { XK_U,        GAVL_KEY_U },
    { XK_V,        GAVL_KEY_V },
    { XK_W,        GAVL_KEY_W },
    { XK_X,        GAVL_KEY_X },
    { XK_Y,        GAVL_KEY_Y },
    { XK_Z,        GAVL_KEY_Z },
    { XK_F1,       GAVL_KEY_F1 },
    { XK_F2,       GAVL_KEY_F2 },
    { XK_F3,       GAVL_KEY_F3 },
    { XK_F4,       GAVL_KEY_F4 },
    { XK_F5,       GAVL_KEY_F5 },
    { XK_F6,       GAVL_KEY_F6 },
    { XK_F7,       GAVL_KEY_F7 },
    { XK_F8,       GAVL_KEY_F8 },
    { XK_F9,       GAVL_KEY_F9 },
    { XK_F10,      GAVL_KEY_F10 },
    { XK_F11,      GAVL_KEY_F11 },
    { XK_F12,      GAVL_KEY_F12 },
  };

#if 1
static const struct
  {
  int x11;
  int bg;
  }
keycodes[] = 
  {
    { 121, GAVL_KEY_MUTE         }, // = XF86AudioMute
    { 122, GAVL_KEY_VOLUME_MINUS }, // = XF86AudioLowerVolume
    { 123, GAVL_KEY_VOLUME_PLUS  }, // = XF86AudioRaiseVolume
    { 171, GAVL_KEY_NEXT         }, // = XF86AudioNext
    { 172, GAVL_KEY_PLAY         }, // = XF86AudioPlay
    { 173, GAVL_KEY_PREV         }, // = XF86AudioPrev
    { 174, GAVL_KEY_STOP         }, // = XF86AudioStop
    { 209, GAVL_KEY_PAUSE        }, // = F86AudioPause
    { 176, GAVL_KEY_REWIND       }, // = F86AudioRewind
    { 216, GAVL_KEY_FORWARD      }, // = F86AudioForward
  };

#endif


static int keysym_to_key_code(KeySym x11)
  {
  int i;

  for(i = 0; i < sizeof(keysyms)/sizeof(keysyms[0]); i++)
    {
    if(x11 == keysyms[i].x11)
      return keysyms[i].bg;
    }
  return GAVL_KEY_NONE;
  }

static int x11_keycode_to_bg(int x11_code)
  {
  int i;

  for(i = 0; i < sizeof(keycodes)/sizeof(keycodes[0]); i++)
    {
    if(x11_code == keycodes[i].x11)
      return keycodes[i].bg;
    }
  return GAVL_KEY_NONE;
  }


static int x11_to_key_mask(int x11_mask)
  {
  int ret = 0;

  if(x11_mask & ShiftMask)
    ret |= GAVL_KEY_SHIFT_MASK;
  if(x11_mask & ControlMask)
    ret |= GAVL_KEY_CONTROL_MASK;
  if(x11_mask & Mod1Mask)
    ret |= GAVL_KEY_ALT_MASK;
  if(x11_mask & Mod4Mask)
    ret |= GAVL_KEY_SUPER_MASK;
  if(x11_mask & Button1Mask)
    ret |= GAVL_KEY_BUTTON1_MASK;
  if(x11_mask & Button2Mask)
    ret |= GAVL_KEY_BUTTON2_MASK;
  if(x11_mask & Button3Mask)
    ret |= GAVL_KEY_BUTTON3_MASK;
  if(x11_mask & Button4Mask)
    ret |= GAVL_KEY_BUTTON4_MASK;
  if(x11_mask & Button5Mask)
    ret |= GAVL_KEY_BUTTON5_MASK;
  return ret;
  }

static void handle_events_x11(void * priv)
  {
  XEvent evt;
  x11_t * x11 = priv;
  gavl_time_t current_time;
  
  /* Check whether to hide the cursor */
  current_time = gavl_timer_get(x11->timer);

  while(XPending(x11->dpy))
    {
    XNextEvent(x11->dpy, &evt);

    switch(evt.type)
      {
      case KeyPress:
        {
        int gavl_code;
        int gavl_mask;

        KeySym keysym;

        evt.xkey.state &= STATE_IGNORE;
        
        keysym = XLookupKeysym(&evt.xkey, 0);
        
        if((gavl_code = keysym_to_key_code(keysym)) == GAVL_KEY_NONE)
          gavl_code = x11_keycode_to_bg(evt.xkey.keycode);

        if(gavl_code != GAVL_KEY_NONE)
          {
          double pos[2];
          gavl_msg_t * msg = bg_msg_sink_get(x11->ctrl.evt_sink);

          gavl_mask = x11_to_key_mask(evt.xkey.state);

          bg_glvideo_window_coords_to_position(x11->g, evt.xkey.x, evt.xkey.y, pos);
          
          gavl_msg_set_gui_key_press(msg, gavl_code, gavl_mask,
                                     evt.xkey.x, evt.xkey.y, pos);
          bg_msg_sink_put(x11->ctrl.evt_sink);
          }
        
        show_cursor(x11);
        x11->last_active_time = current_time;
        }
        break;
      case ConfigureNotify:
        {
        //        gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Window size changed %d %d", evt.xconfigure.width, evt.xconfigure.height);
  
        if(x11->g)
          bg_glvideo_set_window_size(x11->g, evt.xconfigure.width, evt.xconfigure.height);
        }
      case Expose:
        if(!(x11->flags & FLAG_MAPPED))
          gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Expose");
        x11->flags |= FLAG_MAPPED;
        if(x11->g)
          bg_glvideo_redraw(x11->g);
        break;
      case MotionNotify:
        show_cursor(x11);
        x11->last_active_time = current_time;
        break;
      }
    
    }

  if(current_time - x11->last_active_time > 3 * GAVL_TIME_SCALE)
    hide_cursor(x11);
  
  }

static int open_x11(void * priv, const char * uri,
                    gavl_video_format_t * format,
                    int src_flags)
  {
  x11_t * x11 = priv;

  if(!ensure_window(priv))
    return 0;

  map_sync(x11);
  
  x11->sink = 
    bg_glvideo_open(x11->g, format, src_flags);

  show_cursor(x11);
  x11->last_active_time = gavl_timer_get(x11->timer);
  
  return 1;
  }


static void close_x11(void * priv)
  {
  x11_t * x11 = priv;
  bg_glvideo_close(x11->g);
  }

static bg_controllable_t * get_controllable_x11(void * data)
  {
  x11_t * priv = data;
  return &priv->ctrl;
  }

static char const * const protocols = "x11-sink";

static const char * get_protocols_x11(void * priv)
  {
  return protocols;
  }


const bg_ov_plugin_t the_plugin =
  {
    .common =
    {
      BG_LOCALE,
      .name =          "ov_x11",
      .long_name =     TRS("X11"),
      .description =   TRS("X11 display driver supporting OpenGL and direct rendering to DMA buffers"),
      .type =          BG_PLUGIN_OUTPUT_VIDEO,
      .flags =         BG_PLUGIN_OV_STILL,
      .priority =      BG_PLUGIN_PRIORITY_MAX,
      .create =        create_x11,
      .destroy =       destroy_x11,

      //      .get_parameters   = get_parameters_x11,
      //      .set_parameter    = set_parameter_x11,
      .get_controllable = get_controllable_x11,
      .get_protocols = get_protocols_x11,
    },

    .get_hw_context     = get_hw_context_x11,
    .open               = open_x11,
    .get_sink           = get_sink_x11,
    
    .add_overlay_stream = add_overlay_stream_x11,

    .handle_events      = handle_events_x11,
    .close              = close_x11,
    
  };

/* Include this into all plugin modules exactly once
   to let the plugin loader obtain the API version */
BG_GET_PLUGIN_API_VERSION;
