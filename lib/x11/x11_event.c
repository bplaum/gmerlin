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

#include <string.h>
#include <gavl/metatags.h>


#include <gmerlin/parameter.h>
#include <gmerlin/msgqueue.h>
#include <gmerlin/state.h>

#include <x11/x11.h>
#include <x11/x11_window_private.h>

#include <gavl/keycodes.h>
#include <gmerlin/translation.h>
#include <gmerlin/log.h>
#define LOG_DOMAIN "x11_event"

#include <X11/keysym.h>
#include <sys/select.h>

#define IDLE_MAX 50

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

static KeySym key_code_to_keysym(int bg_code)
  {
  int i;

  for(i = 0; i < sizeof(keysyms)/sizeof(keysyms[0]); i++)
    {
    if(bg_code == keysyms[i].bg)
      return keysyms[i].x11;
    }
  return XK_VoidSymbol;
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

#if 1
static int key_mask_to_xembed(int bg_mask)
  {
  int ret = 0;

  if(bg_mask & GAVL_KEY_SHIFT_MASK)
    ret |= XEMBED_MODIFIER_SHIFT;
  if(bg_mask & GAVL_KEY_CONTROL_MASK)
    ret |= XEMBED_MODIFIER_CONTROL;
  if(bg_mask & GAVL_KEY_ALT_MASK)
    ret |= XEMBED_MODIFIER_ALT;
  if(bg_mask & GAVL_KEY_SUPER_MASK)
    ret |= XEMBED_MODIFIER_SUPER;
  // XEMBED_MODIFIER_HYPER ??
  return ret;
  
  }
#endif

static int xembed_to_key_mask(int xembed_mask)
  {
  int ret = 0;

  if(xembed_mask & XEMBED_MODIFIER_SHIFT)
    ret |= GAVL_KEY_SHIFT_MASK;
  if(xembed_mask & XEMBED_MODIFIER_CONTROL)
    ret |= GAVL_KEY_CONTROL_MASK;
  if(xembed_mask & XEMBED_MODIFIER_ALT)
    ret |= GAVL_KEY_ALT_MASK;
  if(xembed_mask & XEMBED_MODIFIER_SUPER)
    ret |= GAVL_KEY_SUPER_MASK;
  // XEMBED_MODIFIER_HYPER ??
  return ret;
  
  }

static int x11_window_next_event(bg_x11_window_t * w,
                                 XEvent * evt,
                                 int milliseconds)
  {
  int fd;
  struct timeval timeout;
  fd_set read_fds;
  if(milliseconds < 0) /* Block */
    {
    XNextEvent(w->dpy, evt);
    return 1;
    }
  else if(!milliseconds)
    {
    if(!XPending(w->dpy))
      return 0;
    else
      {
      XNextEvent(w->dpy, evt);
      return 1;
      }
    }
  else /* Use timeout */
    {
    fd = ConnectionNumber(w->dpy);
    FD_ZERO (&read_fds);
    FD_SET (fd, &read_fds);

    timeout.tv_sec = milliseconds / 1000;
    timeout.tv_usec = 1000 * (milliseconds % 1000);
    if(!select(fd+1, &read_fds, NULL,NULL,&timeout))
      return 0;
    else
      {
      XNextEvent(w->dpy, evt);
      return 1;
      }
    }
  
  }

static int window_is_mapped(Display * dpy, Window w)
  {
  XWindowAttributes attr;
  if(w == None)
    return 0;

  XGetWindowAttributes(dpy, w, &attr);
  if(attr.map_state != IsUnmapped)
    return 1;
  return 0;
  }

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

static void register_xembed_accelerators(bg_x11_window_t * w,
                                         window_t * win)
  {
  int i;
  const bg_accelerator_t * accels;
  
  if(!w->accel_map)
    return;
  accels = bg_accelerator_map_get_accels(w->accel_map);
  i = 0;
  while(accels[i].key != GAVL_KEY_NONE)
    {
    bg_x11_window_send_xembed_message(w, win->parent,
                                      CurrentTime,
                                      XEMBED_REGISTER_ACCELERATOR,
                                      accels[i].id,
                                      key_code_to_keysym(accels[i].key),
                                      key_mask_to_xembed(accels[i].mask));
    i++;
    }
  }
#if 0
static void unregister_xembed_accelerators(bg_x11_window_t * w,
                                           window_t * win)
  {
  int i;
  const bg_accelerator_t * accels;
  if(!w->callbacks || !w->callbacks->accel_map)
    return;
  accels = bg_accelerator_map_get_accels(w->callbacks->accel_map);
  i = 0;
  while(accels[i].key != GAVL_KEY_NONE)
    {
    bg_x11_window_send_xembed_message(w, win->win,
                                      CurrentTime,
                                      XEMBED_UNREGISTER_ACCELERATOR,
                                      accels[i].id, 0, 0);
    i++;
    }
  }
#endif
/* Transform coordinates if we playback video */

static void transform_coords(bg_x11_window_t * w, int x_raw, int y_raw, gavl_value_t * ret)
  {
  double * coords;

  gavl_value_init(ret);
  coords = gavl_value_set_position(ret);
  
  if(!TEST_FLAG(w, FLAG_VIDEO_OPEN))
    {
    coords[0] = (double)(x_raw) / w->window_rect.w;
    coords[1] = (double)(y_raw) / w->window_rect.h;
    }
  else
    {
    coords[0] = (double)(x_raw - w->dst_rect.x) / w->dst_rect.w;
    coords[1] = (double)(y_raw - w->dst_rect.y) / w->dst_rect.h;
    }
  }

void bg_x11_window_handle_event(bg_x11_window_t * w, XEvent * evt)
  {
  KeySym keysym;
  char key_char;
  int  key_code;
  int  key_mask;
  int  accel_id;
  int  button_number = 0;
  window_t * cur;
  int still_shown = 0;
  
  gavl_msg_t * msg;

  gavl_value_t val;
  
  if(TEST_FLAG(w, FLAG_OVERLAY_CHANGED) &&
     TEST_FLAG(w, FLAG_STILL_MODE))
    {
    bg_x11_window_put_frame_internal(w, w->still_frame);
    still_shown = 1;
    }
  
  CLEAR_FLAG(w, FLAG_DO_DELETE);
  
  if(!evt || (evt->type != MotionNotify))
    {
    w->idle_counter++;
    if(w->idle_counter >= IDLE_MAX)
      {
      if(!TEST_FLAG(w, FLAG_POINTER_HIDDEN))
        {
        if(w->current->child == None)
          XDefineCursor(w->dpy, w->current->win, w->fullscreen_cursor);
        XFlush(w->dpy);
        SET_FLAG(w, FLAG_POINTER_HIDDEN);
        }
      w->idle_counter = 0;
      }
    }

  bg_x11_screensaver_ping(&w->scr);
  
  if(TEST_FLAG(w, FLAG_NEED_FOCUS) &&
     window_is_viewable(w->dpy, w->current->focus_child))
    {
    XSetInputFocus(w->dpy, w->current->focus_child,
                   RevertToParent, w->focus_time);
    CLEAR_FLAG(w, FLAG_NEED_FOCUS);
    }

  if(TEST_FLAG(w, FLAG_NEED_FULLSCREEN) &&
     window_is_viewable(w->dpy, w->fullscreen.win))
    {
    bg_x11_window_set_fullscreen_mapped(w, &w->fullscreen);

    CLEAR_FLAG(w, FLAG_NEED_FULLSCREEN);
    }
  
  if(!evt)
    return;

  if(evt->type == w->shm_completion_type)
    {
    CLEAR_FLAG(w, FLAG_WAIT_FOR_COMPLETION);
    return;
    }

  
  switch(evt->type)
    {
    case Expose:
      if(TEST_FLAG(w, FLAG_STILL_MODE) && !still_shown && w->still_frame)
        {
        bg_x11_window_put_frame_internal(w, w->still_frame);
        still_shown = 1;
        }
      break;
    case PropertyNotify:
      if(evt->xproperty.atom == w->_XEMBED_INFO)
        {
        
        if(evt->xproperty.window == w->normal.child)
          {
          bg_x11_window_check_embed_property(w,
                                             &w->normal);
          }
        else if(evt->xproperty.window == w->fullscreen.child)
          {
          bg_x11_window_check_embed_property(w,
                                             &w->fullscreen);
          }
        }
      break;
    case ClientMessage:
      if(evt->xclient.message_type == w->WM_PROTOCOLS)
        {
        if(evt->xclient.data.l[0] == w->WM_DELETE_WINDOW)
          {
          SET_FLAG(w, FLAG_DO_DELETE);
          return;
          }
        else if(evt->xclient.data.l[0] == w->WM_TAKE_FOCUS)
          {
          if(window_is_viewable(w->dpy, w->current->focus_child))
            XSetInputFocus(w->dpy, w->current->focus_child,
                           RevertToParent, evt->xclient.data.l[1]);
          else
            {
            SET_FLAG(w, FLAG_NEED_FOCUS);
            w->focus_time = evt->xclient.data.l[1];
            }
          }
        }
      if(evt->xclient.message_type == w->_XEMBED)
        {

        if((evt->xclient.window == w->normal.win) ||
           (evt->xclient.window == w->normal.child))
          cur = &w->normal;
        else if((evt->xclient.window == w->fullscreen.win) ||
                (evt->xclient.window == w->fullscreen.child))
          cur = &w->fullscreen;
        else
          return;
        
        switch(evt->xclient.data.l[1])
          {
          /* XEMBED messages */
          case XEMBED_EMBEDDED_NOTIFY:
            if(evt->xclient.window == cur->win)
              {
              cur->parent_xembed = 1;
              /*
               *  In gtk this isn't necessary, didn't try other
               *  toolkits. Strange that this is sometimes called with
               *  None as parent window, so we're better off ignoring this here
               */
              // fprintf(stderr, "XEMBED_EMBEDDED_NOTIFY %08lx %08lx\n",
              // cur->parent, evt->xclient.data.l[3]);
              // cur->parent = evt->xclient.data.l[3];
              
              if(window_is_mapped(w->dpy, cur->parent))
                {
                unsigned long buffer[2];
                                
                buffer[0] = 0; // Version
                buffer[1] = XEMBED_MAPPED; 
                
                XChangeProperty(w->dpy,
                                cur->win,
                                w->_XEMBED_INFO,
                                w->_XEMBED_INFO, 32,
                                PropModeReplace,
                                (unsigned char *)buffer, 2);
                XSync(w->dpy, False);
                }

              /* Register our accelerators */
              register_xembed_accelerators(w, cur);
              }
            break;
          case XEMBED_WINDOW_ACTIVATE:
            if((evt->xclient.window == cur->win) &&
               (cur->child_xembed))
              {
              /* Redirect to child */
              bg_x11_window_send_xembed_message(w, cur->child,
                                                XEMBED_WINDOW_ACTIVATE,
                                                evt->xclient.data.l[0],
                                                0, 0, 0);
              }
            break;
          case XEMBED_WINDOW_DEACTIVATE:
            if((evt->xclient.window == cur->win) &&
               (cur->child_xembed))
              {
              /* Redirect to child */
              bg_x11_window_send_xembed_message(w, cur->child,
                                                XEMBED_WINDOW_DEACTIVATE,
                                                evt->xclient.data.l[0],
                                                0, 0, 0);
              }
            break;
          case XEMBED_REQUEST_FOCUS:
            break;
          case XEMBED_FOCUS_IN:
            if((evt->xclient.window == cur->win) &&
               (cur->child_xembed))
              {
              /* Redirect to child */
              bg_x11_window_send_xembed_message(w, cur->child,
                                                evt->xclient.data.l[0],
                                                XEMBED_FOCUS_IN,
                                                evt->xclient.data.l[2], 0, 0);
              }
            break;
          case XEMBED_FOCUS_OUT:
            if((evt->xclient.window == cur->win) &&
               (cur->child_xembed))
              {
              /* Redirect to child */
              bg_x11_window_send_xembed_message(w, cur->child,
                                                evt->xclient.data.l[0],
                                                XEMBED_FOCUS_OUT,
                                                0, 0, 0);
              }

            break;
          case XEMBED_FOCUS_NEXT:
            break;
          case XEMBED_FOCUS_PREV:
/* 8-9 were used for XEMBED_GRAB_KEY/XEMBED_UNGRAB_KEY */
            break;
          case XEMBED_MODALITY_ON:
            if((evt->xclient.window == cur->win) &&
               (cur->child_xembed))
              {
              /* Redirect to child */
              bg_x11_window_send_xembed_message(w, cur->child,
                                                evt->xclient.data.l[0],
                                                XEMBED_MODALITY_ON,
                                                0, 0, 0);
              }
            cur->modality = 1;
            break;
          case XEMBED_MODALITY_OFF:
            if((evt->xclient.window == cur->win) &&
               (cur->child_xembed))
              {
              /* Redirect to child */
              bg_x11_window_send_xembed_message(w, cur->child,
                                                evt->xclient.data.l[0],
                                                XEMBED_MODALITY_OFF,
                                                0, 0, 0);
              }
            cur->modality = 0;
            break;
          case XEMBED_REGISTER_ACCELERATOR:
            /* Don't process our own accelerators */
            
            
            /* Child wants to register an accelerator */
            /*
              detail	accelerator_id
              data1	X key symbol
              data2	bit field of modifier values
            */
            if(!cur->parent_xembed)
              {
              int code;
              if((code = keysym_to_key_code(evt->xclient.data.l[3])) != GAVL_KEY_NONE)
                /* Add to out accel map */
                bg_accelerator_map_append(cur->child_accel_map,
                                          code,
                                          xembed_to_key_mask(evt->xclient.data.l[4]),
                                          evt->xclient.data.l[2]);
              }
            else
              {
              /* Propagate */
              bg_x11_window_send_xembed_message(w, cur->parent,
                                                evt->xclient.data.l[0],
                                                XEMBED_REGISTER_ACCELERATOR,
                                                evt->xclient.data.l[2],
                                                evt->xclient.data.l[3],
                                                evt->xclient.data.l[4]);
              }
            //
            break;
          case XEMBED_UNREGISTER_ACCELERATOR:
            /* Child wants to unregister an accelerator */
            if(!cur->parent_xembed)
              {
              /* Remove from our accel map */
              bg_accelerator_map_remove(cur->child_accel_map,
                                        evt->xclient.data.l[2]);
              }
            else
              {
              /* Propagate */
              bg_x11_window_send_xembed_message(w, cur->parent,
                                                evt->xclient.data.l[0],
                                                XEMBED_UNREGISTER_ACCELERATOR,
                                                evt->xclient.data.l[2], 0, 0);
              }
            break;
          case XEMBED_ACTIVATE_ACCELERATOR:
            /* Check if we have the accelerator */
            //  fprintf(stderr, "Activate accelerator\n");
            if(w->accel_map &&
               bg_accelerator_map_has_accel_with_id(w->accel_map,
                                                    evt->xclient.data.l[2]))
              {
              x11_window_accel_pressed(w, evt->xclient.data.l[2]);
              return;
              }
            else
              {
              /* Propagate to child */
              bg_x11_window_send_xembed_message(w, cur->parent,
                                                evt->xclient.data.l[0],
                                                XEMBED_ACTIVATE_ACCELERATOR,
                                                evt->xclient.data.l[2],
                                                evt->xclient.data.l[3], 0);
              }
            break;
          }
        return;
        }
      break;
    case CreateNotify:
      if(evt->xcreatewindow.parent == w->normal.win)
        cur = &w->normal;
      else if(evt->xcreatewindow.parent == w->fullscreen.win)
        cur = &w->fullscreen;
      else
        break;
      
      if((evt->xcreatewindow.window != cur->focus_child) &&
         (evt->xcreatewindow.window != cur->subwin))
        {
        cur->child = evt->xcreatewindow.window;
        //        fprintf(stderr, "Embed child %ld\n",
        //        evt->xcreatewindow.window);
        bg_x11_window_embed_child(w, cur);
        //        fprintf(stderr, "Embed child done\n");
        }
      break;
    case DestroyNotify:
      if(evt->xdestroywindow.event == w->normal.win)
        cur = &w->normal;
      else if(evt->xdestroywindow.event == w->fullscreen.win)
        cur = &w->fullscreen;
      else
        break;

      if(evt->xdestroywindow.window == cur->child)
        {
        //        fprintf(stderr, "UnEmbed child\n");
        cur->child = None;
        cur->child_xembed = 0;
        if(cur->child_accel_map)
          bg_accelerator_map_clear(cur->child_accel_map);
        //        fprintf(stderr, "Unembed child done\n");
        }
      break;
#if 1
    case FocusIn:
    case FocusOut:
      bg_x11_window_clear_border(w);
      break;
#endif
    case ConfigureNotify:
      {
      if(w->current->win == w->normal.win)
        {
        if((evt->xconfigure.window == w->normal.win) && w->normal.mapped)
          {
          int x,y, width, height;

#if 1
          bg_x11_window_get_coords(w->dpy,
                                   evt->xconfigure.window,
                                   &x, &y, &width, &height);
#else
          x = evt->xconfigure.x;
          y = evt->xconfigure.y;
          width  = evt->xconfigure.width;
          height = evt->xconfigure.height;
#endif
          //          fprintf(stderr, "Configure Notify\n");
          //          fprintf(stderr, "Got coords: %d %d %d %d\n", x, y, width, height);
          
          w->window_rect.w = width;
          w->window_rect.h = height;
          w->window_rect.x = x;
          w->window_rect.y = y;
#if 0          
          if(w->normal.parent == w->root)
            {
            }
#endif
          
          /* Send GAVL_MSG_GUI_WINDOW_COORDS */
          msg = bg_msg_sink_get(w->ctrl.evt_sink);
          gavl_msg_set_id_ns(msg, GAVL_MSG_GUI_WINDOW_COORDS, GAVL_MSG_NS_GUI);
          gavl_msg_set_arg_int(msg, 0, x);
          gavl_msg_set_arg_int(msg, 1, y);
          gavl_msg_set_arg_int(msg, 2, width);
          gavl_msg_set_arg_int(msg, 3, height);
          bg_msg_sink_put(w->ctrl.evt_sink, msg);

          /* Store state */
          gavl_value_init(&val);

          if(TEST_FLAG(w, FLAG_TRACK_RESIZE))
            {
            bg_x11_window_size_changed(w);
            }
          
          if(w->normal.subwin != None)
            {
            //            fprintf(stderr, "XResizeWindow 2 %d %d\n", width, height);
          
            XResizeWindow(w->dpy,
                          w->normal.subwin,
                          width,
                          height);
            
            }
          
          bg_x11_window_set_drawing_coords(w);
          }
        else if(evt->xconfigure.window == w->normal.parent)
          {
          //          fprintf(stderr, "XResizeWindow 1 %d %d\n", 
          //                  evt->xconfigure.width,
          //                  evt->xconfigure.height);
          
          XResizeWindow(w->dpy,
                        w->normal.win,
                        evt->xconfigure.width,
                        evt->xconfigure.height);
          }
        }
      else
        {
        if(evt->xconfigure.window == w->fullscreen.win)
          {
          w->window_rect.x = evt->xconfigure.x;
          w->window_rect.y = evt->xconfigure.y;
          w->window_rect.w  = evt->xconfigure.width;
          w->window_rect.h = evt->xconfigure.height;
          
          
          msg = bg_msg_sink_get(w->ctrl.evt_sink);
          gavl_msg_set_id_ns(msg, GAVL_MSG_GUI_WINDOW_COORDS, GAVL_MSG_NS_GUI);
          gavl_msg_set_arg_int(msg, 0, evt->xconfigure.x);
          gavl_msg_set_arg_int(msg, 1, evt->xconfigure.y);
          gavl_msg_set_arg_int(msg, 2, evt->xconfigure.width);
          gavl_msg_set_arg_int(msg, 3, evt->xconfigure.height);
          bg_msg_sink_put(w->ctrl.evt_sink, msg);
          
          bg_x11_window_size_changed(w);

          gavl_log(GAVL_LOG_DEBUG, LOG_DOMAIN, "Got window size: %d %d",
                 evt->xconfigure.width, evt->xconfigure.height);
                    
          if(w->fullscreen.subwin != None)
            {
            XResizeWindow(w->dpy,
                          w->fullscreen.subwin,
                          evt->xconfigure.width,
                          evt->xconfigure.height);
            }
          }
        else if(evt->xconfigure.window == w->fullscreen.parent)
          {
          XResizeWindow(w->dpy,
                        w->fullscreen.win,
                        evt->xconfigure.width,
                        evt->xconfigure.height);
          }
        }
      SET_FLAG(w, FLAG_DRAWING_COORDS_CHANGED);
      }
      break;
    case MotionNotify:

      /* Check if we had a fake motion event from the screensaver */
      if(w->scr.fake_motion > 0)
        {
        w->scr.fake_motion--;
        return;
        }
      
      w->idle_counter = 0;
      if(TEST_FLAG(w, FLAG_POINTER_HIDDEN))
        {
        XDefineCursor(w->dpy, w->normal.win, None);
        XDefineCursor(w->dpy, w->fullscreen.win, None);
        XFlush(w->dpy);
        CLEAR_FLAG(w, FLAG_POINTER_HIDDEN);
        }

      if((evt->xmotion.window == w->normal.win) ||
         (evt->xmotion.window == w->normal.subwin))
        cur = &w->normal;
      else if((evt->xmotion.window == w->fullscreen.win) ||
              (evt->xmotion.window == w->fullscreen.subwin))
        cur = &w->fullscreen;
      else
        return;
      
      if(cur->modality)
        return;
      
      transform_coords(w, evt->xmotion.x, evt->xmotion.y, &val);
      key_mask = x11_to_key_mask(evt->xmotion.state);

      msg = bg_msg_sink_get(w->ctrl.evt_sink);
      
      gavl_msg_set_gui_motion(msg, 
                              key_mask, evt->xmotion.x, evt->xmotion.y, gavl_value_get_position(&val));
      bg_msg_sink_put(w->ctrl.evt_sink, msg);
      break;
    case UnmapNotify:
      if(evt->xunmap.window == w->normal.win)
        w->normal.mapped = 0;
      else if(evt->xunmap.window == w->fullscreen.win)
        w->fullscreen.mapped = 0;
      else if(evt->xunmap.window == w->fullscreen.toplevel)
        bg_x11_window_init(w);
      break;
    case MapNotify:
      if(evt->xmap.window == w->normal.win)
        {
        w->normal.mapped = 1;
        /* Kindly ask for keyboard focus */
        if(w->normal.parent_xembed)
          bg_x11_window_send_xembed_message(w, w->normal.parent,
                                            CurrentTime,
                                            XEMBED_REQUEST_FOCUS,
                                            0, 0, 0);

        XDefineCursor(w->dpy, w->normal.win, None);
        w->idle_counter = 0;
        CLEAR_FLAG(w, FLAG_POINTER_HIDDEN);

#if 0
        if(TEST_FLAG(w, FLAG_RESIZE_PENDING))
          {
          //          XResizeWindow(w->dpy, w->normal.win, w->window_rect.w, w->window_rect.h);

          XWindowChanges xwc;
          memset(&xwc, 0, sizeof(xwc));
          xwc.width  = w->window_rect.w;
          xwc.height = w->window_rect.h;
          XConfigureWindow(w->dpy, w->normal.win, CWWidth|CWHeight, &xwc);
          XSync(w->dpy, False);
          CLEAR_FLAG(w, FLAG_RESIZE_PENDING);
          fprintf(stderr, "Resized window after map: %d %d\n",
                  w->window_rect.w, w->window_rect.h);
          }
#endif
        }
      else if(evt->xmap.window == w->fullscreen.win)
        {
        w->fullscreen.mapped = 1;
        /* Kindly ask for keyboard focus */
        if(w->fullscreen.parent_xembed)
          bg_x11_window_send_xembed_message(w, w->fullscreen.parent,
                                            CurrentTime,
                                            XEMBED_REQUEST_FOCUS,
                                            0, 0, 0);

        XDefineCursor(w->dpy, w->fullscreen.win, None);
        w->idle_counter = 0;
        CLEAR_FLAG(w, FLAG_POINTER_HIDDEN);
        
        //        bg_x11_window_set_fullscreen_mapped(w, &w->fullscreen);
        }
      else if(evt->xmap.window == w->fullscreen.toplevel)
        {
        bg_x11_window_init(w);
        bg_x11_window_show(w, 1);
        }
      break;
    case KeyPress:
    case KeyRelease:
      keysym = XK_VoidSymbol;
      XLookupString(&evt->xkey, &key_char, 1, &keysym, NULL);
      evt->xkey.state &= STATE_IGNORE;
      
      if((evt->xkey.window == w->normal.win) ||
         (evt->xkey.window == w->normal.focus_child) ||
         (evt->xkey.window == w->normal.subwin))
        cur = &w->normal;
      else if((evt->xkey.window == w->fullscreen.win) ||
              (evt->xkey.window == w->fullscreen.focus_child) ||
              (evt->xkey.window == w->fullscreen.subwin))
        cur = &w->fullscreen;
      else
        {
        return;
        }

      
      if((key_code = keysym_to_key_code(keysym)) == GAVL_KEY_NONE)
        key_code = x11_keycode_to_bg(evt->xkey.keycode);

      // fprintf(stderr, "BG key code: %d, X11 Keycode: %d\n", key_code, evt->xkey.keycode);
      
      key_mask = x11_to_key_mask(evt->xkey.state);
      if(key_code != GAVL_KEY_NONE)
        {
        if(evt->type == KeyPress)
          {
          /* Try if it's our accel */
          if(w->accel_map &&
             bg_accelerator_map_has_accel(w->accel_map,
                                          key_code,
                                          key_mask, &accel_id))
            {
            x11_window_accel_pressed(w, accel_id);
            return;
            }
          /* Check if the child wants tht shortcut */
          if(cur->child_accel_map &&
             cur->child &&
             cur->child_xembed &&
             bg_accelerator_map_has_accel(cur->child_accel_map,
                                          key_code,
                                          key_mask, &accel_id))
            {
            bg_x11_window_send_xembed_message(w, cur->child,
                                              evt->xkey.time,
                                              XEMBED_ACTIVATE_ACCELERATOR,
                                              accel_id, 0, 0);
            return;
            }
          
          /* Here, we see why generic key callbacks are bad:
             One key callback is ok, but if we have more than one
             (i.e. in embedded or embedding windows), one might always
             eat up all events. At least callbacks should return zero
             to notify, that the event should be propagated */
          transform_coords(w, evt->xkey.x, evt->xkey.y, &val);

          msg = bg_msg_sink_get(w->ctrl.evt_sink);
          gavl_msg_set_gui_key_press(msg, key_code, key_mask,
                                     evt->xkey.x, evt->xkey.y, gavl_value_get_position(&val));
          bg_msg_sink_put(w->ctrl.evt_sink, msg);
          }
        else // KeyRelease
          {
          transform_coords(w, evt->xkey.x, evt->xkey.y, &val);
          
          msg = bg_msg_sink_get(w->ctrl.evt_sink);
          gavl_msg_set_gui_key_release(msg, key_code, key_mask,
                                       evt->xkey.x, evt->xkey.y, gavl_value_get_position(&val));
          bg_msg_sink_put(w->ctrl.evt_sink, msg);
          }
        }
      
      if(cur->child != None)
        {
        XKeyEvent key_event;
        /* Send event to child window */
        memset(&key_event, 0, sizeof(key_event));
        key_event.display = w->dpy;
        key_event.window = cur->child;
        key_event.root = w->root;
        key_event.subwindow = None;
        key_event.time = evt->xkey.time;
        key_event.x = 0;
        key_event.y = 0;
        key_event.x_root = 0;
        key_event.y_root = 0;
        key_event.same_screen = True;
        
        key_event.type = evt->type;
        key_event.keycode = evt->xkey.keycode;
        key_event.state = evt->xkey.state;

        XSendEvent(key_event.display,
                   key_event.window,
                   False, NoEventMask, (XEvent *)(&key_event));
        XFlush(w->dpy);
        }
      break;
    case ButtonPress:
    case ButtonRelease:
      
      if((evt->xbutton.window == w->normal.win) ||
         (evt->xbutton.window == w->normal.subwin))
        cur = &w->normal;
      else if((evt->xbutton.window == w->fullscreen.win) ||
              (evt->xbutton.window == w->fullscreen.subwin))
        cur = &w->fullscreen;
      else
        return;
      
      if(cur->modality)
        return;
      
      transform_coords(w, evt->xbutton.x, evt->xbutton.y, &val);
      evt->xkey.state &= STATE_IGNORE;

      switch(evt->xbutton.button)
        {
        case Button1:
          button_number = 1;
          break;
        case Button2:
          button_number = 2;
          break;
        case Button3:
          button_number = 3;
          break;
        case Button4:
          button_number = 4;
          break;
        case Button5:
          button_number = 5;
          break;
        }

      key_mask = x11_to_key_mask(evt->xbutton.state);

      /* TODO: Zoom, Squeeze */

      if(button_number == 4)
        {
        /* Increase Zoom */
        if((key_mask & GAVL_KEY_ALT_MASK) == GAVL_KEY_ALT_MASK)
          {
          x11_window_accel_pressed(w, ACCEL_INC_ZOOM);
          return;
          }
          
        /* Increase Squeeze */
        else if((key_mask & GAVL_KEY_CONTROL_MASK) == GAVL_KEY_CONTROL_MASK)
          {
          x11_window_accel_pressed(w, ACCEL_INC_SQUEEZE);
          return;
          }
        }
      else if(button_number == 5)
        {
        /* Increase Zoom */
        if((key_mask & GAVL_KEY_ALT_MASK) == GAVL_KEY_ALT_MASK)
          {
          x11_window_accel_pressed(w, ACCEL_DEC_ZOOM);
          return;
          }

        /* Increase Squeeze */
        else if((key_mask & GAVL_KEY_CONTROL_MASK) == GAVL_KEY_CONTROL_MASK)
          {
          x11_window_accel_pressed(w, ACCEL_DEC_SQUEEZE);
          return;
          }
        }

      /* Propagate */ 

      msg = bg_msg_sink_get(w->ctrl.evt_sink);

      if(evt->type == ButtonPress)
        gavl_msg_set_gui_button_press(msg, button_number, key_mask, 
                                      evt->xbutton.x, evt->xbutton.y, gavl_value_get_position(&val));
      else
        gavl_msg_set_gui_button_release(msg, button_number, key_mask, 
                                        evt->xbutton.x, evt->xbutton.y, gavl_value_get_position(&val));
        
      bg_msg_sink_put(w->ctrl.evt_sink, msg);
        
      /* Also send to parent */
      if(w->current->parent != w->root)
        {
        XButtonEvent button_event;
        memset(&button_event, 0, sizeof(button_event));
        button_event.type = evt->type;
        button_event.display = w->dpy;
        button_event.window = w->current->parent;
        button_event.root = w->root;
        button_event.time = CurrentTime;
        button_event.x = evt->xbutton.x;
        button_event.y = evt->xbutton.y;
        button_event.x_root = evt->xbutton.x_root;
        button_event.y_root = evt->xbutton.y_root;
        button_event.state  = evt->xbutton.state;
        button_event.button = evt->xbutton.button;
        button_event.same_screen = evt->xbutton.same_screen;

        XSendEvent(button_event.display,
                   button_event.window,
                   False, NoEventMask, (XEvent *)(&button_event));
        //        XFlush(w->dpy);
        }
    }

  }

void bg_x11_window_handle_events(bg_x11_window_t * win, int milliseconds)
  {
  XEvent evt;
  
  if(TEST_FLAG(win, FLAG_WAIT_FOR_COMPLETION))
    {
    while(TEST_FLAG(win, FLAG_WAIT_FOR_COMPLETION))
      {
      x11_window_next_event(win, &evt, -1);
      bg_x11_window_handle_event(win, &evt);
      }
    }
  else
    {
    while(1)
      {
      if(!x11_window_next_event(win, &evt, milliseconds))
        {
        /* Still need to hide the mouse cursor and ping the screensaver */
        bg_x11_window_handle_event(win, NULL);
        return;
        }
      bg_x11_window_handle_event(win, &evt);
      }
    }
  }
