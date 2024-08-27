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


#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>


// /* According to POSIX.1-2001, POSIX.1-2008 */
// #include <sys/select.h>

#include <config.h>

#include <gavl/gavl.h>
#include <gavl/msg.h>
#include <gavl/keycodes.h>

#include <gmerlin/translation.h>
#include <gmerlin/log.h>

#define LOG_DOMAIN "ncurses"

#include <gmerlin/frontend.h>
#include <bgncurses.h>

static pthread_mutex_t ncurses_mutex = PTHREAD_MUTEX_INITIALIZER;

static int ncurses_running = 0;
static int window_size_reported = 0;

typedef struct
  {
  int nc;
  int gavl_code;
  int gavl_mask;
  }
keymap_t;

static keymap_t keymap[];

static int old_stderr = -1;

static void suppress_stderr()
  {
  int new_stderr;
  
  if(!isatty(STDERR_FILENO))
    return;

  if((new_stderr = open("/dev/null", 0)) < 0)
    return;

  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Redirecting error output");
  
  old_stderr = dup(STDERR_FILENO);
  dup2(new_stderr, STDERR_FILENO);
  
  }

static void restore_stderr()
  {
  if(old_stderr < 0)
    return;

  dup2(old_stderr, STDERR_FILENO);
  close(old_stderr);
  
  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Restored error output");
  }

int bg_ncurses_init()
  {
  int result = 1;
  
  pthread_mutex_lock(&ncurses_mutex);

  if(ncurses_running)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN,
           "Cannot initialize ncurses, other instance running");
    goto fail;
    }

  if(!isatty(STDIN_FILENO))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN,
           "Cannot initialize ncurses: stdin is no tty");
    goto fail;
    }
  if(!isatty(STDOUT_FILENO))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN,
           "Cannot initialize ncurses: stdout is no tty");
    goto fail;
    }

  suppress_stderr();
  
  /* Do actual initalization */

  initscr();                 /* Initialize ncurses             */
  cbreak();                  /* Don't wait for newline         */
  noecho();                  /* Turn off echo                  */
  nonl();                    /* Make the return key detectable */
  //  intrflush(stdscr, FALSE);
  keypad(stdscr, TRUE);      /* Enable Arrow keys              */
  
  curs_set(0);               /* Hide cursor                    */
  nodelay(stdscr, TRUE);     /* make getch non-blocking        */
  
  ncurses_running = 1;

  window_size_reported = 0;
  
  start_color();
  init_pair(1, COLOR_WHITE, COLOR_BLUE);
  init_pair(2, COLOR_BLACK, COLOR_WHITE);
  
  bkgd(COLOR_PAIR(1));

  mousemask(ALL_MOUSE_EVENTS, NULL);   /* The events you want to listen to */
  
  result = 1;
  
  fail:
  
  pthread_mutex_unlock(&ncurses_mutex);
  return result;
  
  }

int bg_ncurses_cleanup()
  {
  int result = 0;
  
  pthread_mutex_lock(&ncurses_mutex);

  if(!ncurses_running)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN,
           "Cannot cleanup ncurses, not running");
    goto fail;
    }
  ncurses_running = 0;

  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN,
         "Shutting down ncurses");
  endwin();

  restore_stderr();
    
  result = 1;
  fail:
  
  pthread_mutex_unlock(&ncurses_mutex);
  
  return result;
  }

static void report_window_size(bg_msg_sink_t * sink)
  {
  int w, h;
  gavl_msg_t * msg;

  getmaxyx(stdscr, h, w);
  
  msg = bg_msg_sink_get(sink);
  gavl_msg_set_id_ns(msg, GAVL_MSG_GUI_WINDOW_COORDS, GAVL_MSG_NS_GUI);
      
  gavl_msg_set_arg_int(msg, 0, 0);
  gavl_msg_set_arg_int(msg, 1, 0);
  gavl_msg_set_arg_int(msg, 2, w);
  gavl_msg_set_arg_int(msg, 3, h);
  bg_msg_sink_put(sink);
  }

static int button_mask(MEVENT * event)
  {
  int mask = 0;

  if(event->bstate & BUTTON_CTRL)
    mask |= GAVL_KEY_CONTROL_MASK;
  if(event->bstate & BUTTON_SHIFT)
    mask |= GAVL_KEY_SHIFT_MASK;
  if(event->bstate & BUTTON_ALT)
    mask |= GAVL_KEY_ALT_MASK;
  
  return mask;
  }


static void report_button_press(bg_msg_sink_t * sink, MEVENT * event, int button)
  {
  int w, h;
  gavl_msg_t * msg;
  double * pos;
  gavl_value_t pos_val;
  gavl_value_init(&pos_val);
  pos = gavl_value_set_position(&pos_val);
  getmaxyx(stdscr,h,w);
  pos[0] = (double)event->x / (w-1);
  pos[1] = (double)event->y / (h-1);


  msg = bg_msg_sink_get(sink);

  gavl_msg_set_id_ns(msg, GAVL_MSG_GUI_BUTTON_PRESS, GAVL_MSG_NS_GUI);
  gavl_msg_set_arg_int(msg, 0,   button); // Button
  gavl_msg_set_arg_int(msg, 1,   button_mask(event)); // mask
  gavl_msg_set_arg_int(msg, 2,   event->x);
  gavl_msg_set_arg_int(msg, 3,   event->y);
  gavl_msg_set_arg_nocopy(msg, 4, &pos_val);
  
  bg_msg_sink_put(sink);
  }

static void report_button_release(bg_msg_sink_t * sink, MEVENT * event, int button)
  {
  int w, h;
  gavl_msg_t * msg;

  double * pos;
  gavl_value_t pos_val;
  gavl_value_init(&pos_val);
  pos = gavl_value_set_position(&pos_val);
  getmaxyx(stdscr,h,w);
  pos[0] = (double)event->x / (w-1);
  pos[1] = (double)event->y / (h-1);
  

  msg = bg_msg_sink_get(sink);

  gavl_msg_set_id_ns(msg, GAVL_MSG_GUI_BUTTON_RELEASE, GAVL_MSG_NS_GUI);
  gavl_msg_set_arg_int(msg, 0,   button); // Button
  gavl_msg_set_arg_int(msg, 1,   button_mask(event)); // mask
  gavl_msg_set_arg_int(msg, 2,   event->x);
  gavl_msg_set_arg_int(msg, 3,   event->y);
  gavl_msg_set_arg_nocopy(msg, 4, &pos_val);

  pos[0] = (double)event->x / (w-1);
  pos[1] = (double)event->y / (h-1);
  
  
  bg_msg_sink_put(sink);
  
  }

static void report_button_doubleclick(bg_msg_sink_t * sink, MEVENT * event, int button)
  {
  int w, h;
  gavl_msg_t * msg;
  double * pos;
  gavl_value_t pos_val;
  gavl_value_init(&pos_val);
  pos = gavl_value_set_position(&pos_val);
  getmaxyx(stdscr,h,w);
  pos[0] = (double)event->x / (w-1);
  pos[1] = (double)event->y / (h-1);


  msg = bg_msg_sink_get(sink);

  gavl_msg_set_id_ns(msg, GAVL_MSG_GUI_BUTTON_DOUBLECLICK, GAVL_MSG_NS_GUI);
  gavl_msg_set_arg_int(msg, 0,   button); // Button
  gavl_msg_set_arg_int(msg, 1,   button_mask(event)); // mask
  gavl_msg_set_arg_int(msg, 2,   event->x);
  gavl_msg_set_arg_int(msg, 3,   event->y);
  gavl_msg_set_arg_nocopy(msg, 4, &pos_val);
  bg_msg_sink_put(sink);
  }

void bg_ncurses_process_events(bg_msg_sink_t * sink)
  {
  int key; 
  int idx = 0;
  gavl_msg_t * msg;

  pthread_mutex_lock(&ncurses_mutex);

  if(!window_size_reported)
    {
    report_window_size(sink);
    window_size_reported = 1;
    pthread_mutex_unlock(&ncurses_mutex);
    return;
    }
  
  while((key = getch()) != ERR)
    {
    if(key == KEY_RESIZE)
      {
      report_window_size(sink);
      }
    else if(key == KEY_MOUSE)
      {
      MEVENT event;

      if(getmouse(&event) != OK)
        break;
      
      if(event.bstate & BUTTON1_PRESSED)          /* mouse button 1 down           */
        {
        //        fprintf(stderr, "Button 1 pressed\n");
        report_button_press(sink, &event, 1);
        }
      else if(event.bstate & BUTTON1_RELEASED)         /* mouse button 1 up             */
        {
        //        fprintf(stderr, "Button 1 released\n");
        report_button_release(sink, &event, 1);
        }
      else if(event.bstate & BUTTON1_CLICKED)          /* mouse button 1 clicked        */
        {
        //        fprintf(stderr, "Button 1 clicked\n");
        report_button_press(sink, &event, 1);
        report_button_release(sink, &event, 1);
        }
      else if(event.bstate & BUTTON1_DOUBLE_CLICKED)   /* mouse button 1 double clicked */
        {
        //        fprintf(stderr, "Button 1 doubleclicked\n");
        report_button_doubleclick(sink, &event, 1);
        }
      else if(event.bstate & BUTTON1_TRIPLE_CLICKED)   /* mouse button 1 triple clicked */
        {
        //        fprintf(stderr, "Button 1 tripleclicked\n");
        }
      else if(event.bstate & BUTTON2_PRESSED)          /* mouse button 2 down           */
        {
        report_button_press(sink, &event, 2);
        }
      else if(event.bstate & BUTTON2_RELEASED)         /* mouse button 2 up             */
        {
        report_button_release(sink, &event, 2);
        
        }
      else if(event.bstate & BUTTON2_CLICKED)          /* mouse button 2 clicked        */
        {
        report_button_press(sink, &event, 2);
        report_button_release(sink, &event, 2);
        
        }
      else if(event.bstate & BUTTON2_DOUBLE_CLICKED)   /* mouse button 2 double clicked */
        {
        report_button_doubleclick(sink, &event, 2);
        }
      else if(event.bstate & BUTTON2_TRIPLE_CLICKED)   /* mouse button 2 triple clicked */
        {
        
        }
      else if(event.bstate & BUTTON3_PRESSED)          /* mouse button 3 down           */
        {
        report_button_press(sink, &event, 3);
        }
      else if(event.bstate & BUTTON3_RELEASED)         /* mouse button 3 up             */
        {
        report_button_release(sink, &event, 3);
        }
      else if(event.bstate & BUTTON3_CLICKED)          /* mouse button 3 clicked        */
        {
        report_button_press(sink, &event, 3);
        report_button_release(sink, &event, 3);
        }
      else if(event.bstate & BUTTON3_DOUBLE_CLICKED)   /* mouse button 3 double clicked */
        {
        report_button_doubleclick(sink, &event, 3);
        }
      else if(event.bstate & BUTTON3_TRIPLE_CLICKED)   /* mouse button 3 triple clicked */
        {
        
        }
      else if(event.bstate & BUTTON4_PRESSED)          /* mouse button 4 down           */
        {
        report_button_press(sink, &event, 4);
        }
      else if(event.bstate & BUTTON4_RELEASED)         /* mouse button 4 up             */
        {
        report_button_release(sink, &event, 4);
        }
      else if(event.bstate & BUTTON4_CLICKED)          /* mouse button 4 clicked        */
        {
        report_button_press(sink, &event, 4);
        report_button_release(sink, &event, 4);
        }
      else if(event.bstate & BUTTON4_DOUBLE_CLICKED)   /* mouse button 4 double clicked */
        {
        report_button_doubleclick(sink, &event, 4);
        }
      else if(event.bstate & BUTTON4_TRIPLE_CLICKED)   /* mouse button 4 triple clicked */
        {
        
        }
#if 0
      else if(event.bstate & BUTTON5_PRESSED)          /* mouse button 4 down           */
        {
        int w, h;

        getmaxyx(stdscr,h,w);
        msg = bg_msg_sink_get(sink);

        gavl_msg_set_id_ns(msg, GAVL_MSG_GUI_BUTTON_PRESS, GAVL_MSG_NS_GUI);
        gavl_msg_set_arg_int(msg, 0,   4); // Button
        gavl_msg_set_arg_int(msg, 1,   mask); // mask
        gavl_msg_set_arg_int(msg, 2,   event.x);
        gavl_msg_set_arg_int(msg, 3,   event.y);
        gavl_msg_set_arg_float(msg, 4, (double)event.x / w);
        gavl_msg_set_arg_float(msg, 5, (double)event.y / h);
        bg_msg_sink_put(sink);
        
        }
      else if(event.bstate & BUTTON5_RELEASED)         /* mouse button 4 up             */
        {
        
        }
      else if(event.bstate & BUTTON5_CLICKED)          /* mouse button 4 clicked        */
        {
        
        }
      else if(event.bstate & BUTTON5_DOUBLE_CLICKED)   /* mouse button 4 double clicked */
        {
        
        }
      else if(event.bstate & BUTTON5_TRIPLE_CLICKED)   /* mouse button 4 triple clicked */
        {
        
        }
#endif
      /* 
      msg = bg_msg_sink_get(sink);
      
      
      bg_msg_sink_put(sink);
      */
      }
    else
      {
      while(keymap[idx].nc != ERR)
        {
        if(key == keymap[idx].nc)
          {
          msg = bg_msg_sink_get(sink);

          gavl_msg_set_id_ns(msg, GAVL_MSG_GUI_KEY_PRESS, GAVL_MSG_NS_GUI);
          
          gavl_msg_set_arg_int(msg, 0, keymap[idx].gavl_code);
          gavl_msg_set_arg_int(msg, 1, keymap[idx].gavl_mask);
          
          bg_msg_sink_put(sink);
          break;
          }
        
        idx++;
        }
      }
    }

  pthread_mutex_unlock(&ncurses_mutex);
  
  }

static keymap_t keymap[] =
  {

    { 'A', GAVL_KEY_A },
    { 'B', GAVL_KEY_B },
    { 'C', GAVL_KEY_C },
    { 'D', GAVL_KEY_D },
    { 'E', GAVL_KEY_E },
    { 'F', GAVL_KEY_F },
    { 'G', GAVL_KEY_G },
    { 'H', GAVL_KEY_H },
    { 'I', GAVL_KEY_I },
    { 'J', GAVL_KEY_J },
    { 'K', GAVL_KEY_K },
    { 'L', GAVL_KEY_L },
    { 'M', GAVL_KEY_M },
    { 'N', GAVL_KEY_N },
    { 'O', GAVL_KEY_O },
    { 'P', GAVL_KEY_P },
    { 'Q', GAVL_KEY_Q },
    { 'R', GAVL_KEY_R },
    { 'S', GAVL_KEY_S },
    { 'T', GAVL_KEY_T },
    { 'U', GAVL_KEY_U },
    { 'V', GAVL_KEY_V },
    { 'W', GAVL_KEY_W },
    { 'X', GAVL_KEY_X },
    { 'Y', GAVL_KEY_Y },
    { 'Z', GAVL_KEY_Z },
    
    { 'a', GAVL_KEY_a },
    { 'b', GAVL_KEY_b },
    { 'c', GAVL_KEY_c },
    { 'd', GAVL_KEY_d },
    { 'e', GAVL_KEY_e },
    { 'f', GAVL_KEY_f },
    { 'g', GAVL_KEY_g },
    { 'h', GAVL_KEY_h },
    { 'i', GAVL_KEY_i },
    { 'j', GAVL_KEY_j },
    { 'k', GAVL_KEY_k },
    { 'l', GAVL_KEY_l },
    { 'm', GAVL_KEY_m },
    { 'n', GAVL_KEY_n },
    { 'o', GAVL_KEY_o },
    { 'p', GAVL_KEY_p },
    { 'q', GAVL_KEY_q },
    { 'r', GAVL_KEY_r },
    { 's', GAVL_KEY_s },
    { 't', GAVL_KEY_t },
    { 'u', GAVL_KEY_u },
    { 'v', GAVL_KEY_v },
    { 'w', GAVL_KEY_w },
    { 'x', GAVL_KEY_x },
    { 'y', GAVL_KEY_y },
    { 'z', GAVL_KEY_z },

    { '0', GAVL_KEY_0 },
    { '1', GAVL_KEY_1 },
    { '2', GAVL_KEY_2 },
    { '3', GAVL_KEY_3 },
    { '4', GAVL_KEY_4 },
    { '5', GAVL_KEY_5 },
    { '6', GAVL_KEY_6 },
    { '7', GAVL_KEY_7 },
    { '8', GAVL_KEY_8 },
    { '9', GAVL_KEY_9 },

    { '?',           GAVL_KEY_QUESTION   }, //!< ?
    { '!',           GAVL_KEY_EXCLAM     }, //!< !
    { '"',           GAVL_KEY_QUOTEDBL   }, //!< "
    { '$',           GAVL_KEY_DOLLAR     }, //!< $
    { '%',           GAVL_KEY_PERCENT    }, //!< %
    { '&',           GAVL_KEY_APMERSAND  }, //!< &
    { '/',           GAVL_KEY_SLASH      }, //!< /
    { '(',           GAVL_KEY_LEFTPAREN  }, //!< (
    { ')',           GAVL_KEY_RIGHTPAREN }, //!< )
    { '=',           GAVL_KEY_EQUAL      }, //!< =
    { '\\',          GAVL_KEY_BACKSLASH  }, //!< :-)
    { KEY_BACKSPACE, GAVL_KEY_BACKSPACE  }, //!< :-)

    
    { KEY_F(1),  GAVL_KEY_F1 },
    { KEY_F(2),  GAVL_KEY_F2 },
    { KEY_F(3),  GAVL_KEY_F3 },
    { KEY_F(4),  GAVL_KEY_F4 },
    { KEY_F(5),  GAVL_KEY_F5 },
    { KEY_F(6),  GAVL_KEY_F6 },
    { KEY_F(7),  GAVL_KEY_F7 },
    { KEY_F(8),  GAVL_KEY_F8 },
    { KEY_F(9),  GAVL_KEY_F9 },
    { KEY_F(10), GAVL_KEY_F10 },
    { KEY_F(11), GAVL_KEY_F11 },
    { KEY_F(12), GAVL_KEY_F12 },
    
    { KEY_UP,     GAVL_KEY_UP        },
    { KEY_DOWN,   GAVL_KEY_DOWN      },
    { KEY_LEFT,   GAVL_KEY_LEFT      },
    { KEY_RIGHT,  GAVL_KEY_RIGHT     },
    { KEY_SLEFT,  GAVL_KEY_LEFT,     GAVL_KEY_SHIFT_MASK },
    { KEY_SRIGHT, GAVL_KEY_RIGHT,    GAVL_KEY_SHIFT_MASK },
    { KEY_NPAGE,  GAVL_KEY_PAGE_DOWN },
    { KEY_PPAGE,  GAVL_KEY_PAGE_UP   },
    { KEY_END,    GAVL_KEY_END  },
    { KEY_HOME,   GAVL_KEY_HOME },
    { '\r',       GAVL_KEY_RETURN },
    { ' ',        GAVL_KEY_SPACE },
    
    { ERR },
  };
