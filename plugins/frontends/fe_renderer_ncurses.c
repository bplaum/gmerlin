#include <config.h>

#include <string.h>

#include <gavl/keycodes.h>

#include <gmerlin/parameter.h>
#include <gmerlin/bgmsg.h>

#include <gmerlin/player.h>
#include <gmerlin/playermsg.h>
#include <gmerlin/frontend.h>
#include <gmerlin/translation.h>
#include <gmerlin/log.h>

#define LOG_DOMAIN "renderer_ncurses"

#include <gmerlin/utils.h>
#include <gmerlin/mdb.h>

#include <bgncurses.h>

#include <frontend_priv.h>

#define WIDGET_PLAYER  0
#define WIDGET_BROWSER 1

typedef struct 
  {
  int win_w;
  int win_h;
  
  int num_ncurses_msg;
  bg_msg_sink_t * ncurses_sink;

  int widget;

  WINDOW * player_win;
  WINDOW * tracks_win;
  
  gavl_rectangle_i_t tracks_win_rect;
  gavl_rectangle_i_t player_win_rect;
  gavl_rectangle_i_t tracks_frame_rect;
  
  gavl_time_t current_duration;
  
  gavl_dictionary_t current_track;
  
  double perc;
  gavl_time_t time;
  int status;
  
  gavl_dictionary_t tracks;
  
  int selected;
  int current;
  
  int scroll_offset;

  bg_control_t ctrl;
  bg_controllable_t * controllable;
  
  } bg_player_frontend_ncurses_t;

static void set_selected(bg_player_frontend_ncurses_t * p, int new_selected);
static void set_current(bg_player_frontend_ncurses_t * p, int new_current);
static void fill_space(WINDOW * win, int line, int col_start, int col_end);

static void update_current_track(bg_player_frontend_ncurses_t * p)
  {
  const gavl_dictionary_t * m;
  const char * var;
  int new_current;
  
  m = gavl_track_get_metadata(&p->current_track);
  
  /* Print label */
  
  if(m)
    {
    int x = 1, y = 1;
    char str[GAVL_TIME_STRING_LEN];
    
    if((var = gavl_dictionary_get_string(m, GAVL_META_LABEL)) ||
       (var = gavl_dictionary_get_string(m, GAVL_META_TITLE)))
      {
      //      fprintf(stderr, "update track: %s\n", var);
      mvwprintw(p->player_win, 1, 1, "%s", var);
      getyx(p->player_win, y, x);
      }
    else
      x = 1;
    
    fill_space(p->player_win, y, x, p->win_w - 1);
    
    p->current_duration = GAVL_TIME_UNDEFINED;

    gavl_dictionary_get_long(m, GAVL_META_APPROX_DURATION, &p->current_duration);

    gavl_time_prettyprint(p->current_duration, str);
    
    mvwprintw(p->player_win, 2, 11, "/%10s", str);

    if((var = gavl_track_get_id(&p->current_track)) &&
       ((new_current = gavl_get_track_idx_by_id(&p->tracks, var)) >= 0))
      set_current(p, new_current);
    
    }
    
  }

static void draw_seek_slider(bg_player_frontend_ncurses_t * p)
  {
  int i;
  int pos;
  int len;
  
  len = p->win_w - 2;

  pos = (int)(p->perc * len + 0.5);

  wattron(p->player_win, A_ALTCHARSET);
  for(i = 0; i < len; i++)
    {
    if(i == pos)
      mvwprintw(p->player_win, 3, i+1, "%c", ACS_DIAMOND);
    else
      mvwprintw(p->player_win, 3, i+1, "%c", ACS_HLINE);
    }
  wrefresh(p->player_win);

  wattroff(p->player_win, A_ALTCHARSET);
  
  }

static void draw_status(bg_player_frontend_ncurses_t * p)
  {
  const char * status_str = NULL;

  switch(p->status)
    {
    case BG_PLAYER_STATUS_STOPPED:
      status_str = " []";
      break;
    case BG_PLAYER_STATUS_PLAYING:
      status_str = " |>";
      break;
    case BG_PLAYER_STATUS_SEEKING:
    case BG_PLAYER_STATUS_CHANGING:
      status_str = "<->";
      break;
    case BG_PLAYER_STATUS_PAUSED:
      status_str = " ||";
      break;
    case BG_PLAYER_STATUS_ERROR:
      status_str = "/!\\";
      break;
    }

  if(status_str)
    {
    mvwprintw(p->player_win, 2, p->win_w - 4, "%s", status_str);
    wrefresh(p->player_win);
    }
  }

static void fill_space(WINDOW * win, int line, int col_start, int col_end)
  {
  int i;
  for(i = col_start; i < col_end; i++)
    mvwprintw(win, line, i, " ");
  }

static void draw_track(bg_player_frontend_ncurses_t * p, int idx, int selected, int current)
  {
  gavl_time_t duration = GAVL_TIME_UNDEFINED;
  const char * var;
  const gavl_dictionary_t * m;
  char str[GAVL_TIME_STRING_LEN];
  const gavl_dictionary_t * track;

  if(!(track = gavl_get_track(&p->tracks, idx)))
    {
    return;
    }
  
  m = gavl_track_get_metadata(track);

  if(selected)
    wattron(p->tracks_win, A_REVERSE);

  if(current)
    wattron(p->tracks_win, A_BOLD);
  
  idx -= p->scroll_offset;
  
  fill_space(p->tracks_win, p->tracks_win_rect.y + idx, p->tracks_win_rect.x, p->tracks_win_rect.w);
  
  if((var = gavl_dictionary_get_string(m, GAVL_META_LABEL)) ||
     (var = gavl_dictionary_get_string(m, GAVL_META_TITLE)))
    {
    mvwprintw(p->tracks_win, p->tracks_win_rect.y + idx, p->tracks_win_rect.x, "%s", var);
    }
  
  gavl_dictionary_get_long(m, GAVL_META_APPROX_DURATION, &duration);
  
  gavl_time_prettyprint(duration, str);
  mvwprintw(p->tracks_win, p->tracks_win_rect.y + idx, p->tracks_win_rect.x +
            p->tracks_win_rect.w - 10, "%10s", str);
  
  if(selected)
    wattroff(p->tracks_win, A_REVERSE);

  if(current)
    wattroff(p->tracks_win, A_BOLD);
  }

static void draw_tracks(bg_player_frontend_ncurses_t * p)
  {
  int i;
  int num_tracks = gavl_get_num_tracks(&p->tracks);
  
  for(i = p->scroll_offset; i < p->scroll_offset + p->tracks_win_rect.h; i++)
    {
    if(i >= num_tracks)
      break;
    
    /* Print label */
    draw_track(p, i, (i == p->selected), (i == p->current));

    // fprintf(stderr, "Draw track: %d %d\n", i, i - p->scroll_offset);
    
    // mvwprintw(p->player_win, 2, 11, "/%10s", str);
    }

  mvwprintw(p->tracks_win, p->tracks_frame_rect.h-1, 1, "[ %03d / %03d ]",
            p->selected + 1, num_tracks);
  
  refresh();
  wrefresh(p->tracks_win);
  }

static void redraw(bg_player_frontend_ncurses_t * p)
  {
  werase(stdscr);
  wrefresh(stdscr);

  p->player_win_rect.w = p->win_w;
  p->player_win_rect.h = 5;
  wresize(p->player_win, p->player_win_rect.h, p->player_win_rect.w);

  p->tracks_frame_rect.w = p->win_w;
  p->tracks_frame_rect.h = p->win_h - 5;
  wresize(p->tracks_win, p->tracks_frame_rect.h, p->tracks_frame_rect.w);

  p->tracks_win_rect.w = p->win_w - 2;
  p->tracks_win_rect.h = p->win_h - 7;
  
  werase(p->player_win);
  box(p->player_win, 0, 0); 
  update_current_track(p);

  wrefresh(p->player_win);

  draw_seek_slider(p);
  draw_status(p);

  /* Tracks win */
  werase(p->tracks_win);
  box(p->tracks_win, 0, 0); 
  wrefresh(p->tracks_win);
  
  /* Draw tracks */
  draw_tracks(p);
  }

static int ping_renderer_ncurses(void * data)
  {
  int ret = 0;
  bg_player_frontend_ncurses_t * p = data;

  /* Handle player message */
  bg_msg_sink_iteration(p->ctrl.evt_sink);
  ret += bg_msg_sink_get_num(p->ctrl.evt_sink);

  p->num_ncurses_msg = 0;

  bg_ncurses_process_events(p->ncurses_sink);
  
  ret += p->num_ncurses_msg;
  
  return ret;
  }

static void destroy_renderer_ncurses(void * priv)
  {
  bg_player_frontend_ncurses_t * p = priv;

  if(p->controllable)
    bg_ncurses_cleanup();
  
  if(p->ncurses_sink)
    bg_msg_sink_destroy(p->ncurses_sink);
  
  gavl_dictionary_free(&p->tracks);
  
  free(p);
  
  }

static int handle_player_message_ncurses(void * priv, gavl_msg_t * msg)
  {
  bg_player_frontend_ncurses_t * p = priv;
  
  switch(msg->NS)
    {
    case BG_MSG_NS_STATE:
      switch(msg->ID)
        {
        case BG_MSG_STATE_CHANGED:
          {
          gavl_value_t val;
          const char * ctx;
          const char * var;
          
          gavl_value_init(&val);

          gavl_msg_get_state(msg,
                           NULL,
                           &ctx,
                           &var,
                           &val, NULL);

          if(!strcmp(ctx, BG_PLAYER_STATE_CTX))
            {
            if(!strcmp(var, BG_PLAYER_STATE_TIME))          // long
              {
              char str[GAVL_TIME_STRING_LEN];
              p->time = GAVL_TIME_UNDEFINED;

              if(!gavl_value_get_long(&val, &p->time))
                return 1;
              
              gavl_time_prettyprint(p->time, str);
              
              mvwprintw(p->player_win, 2, 1, "%10s", str);
              wrefresh(p->player_win);

              // print_time(fe, t);
              }
            if(!strcmp(var, BG_PLAYER_STATE_TIME_PERC))          // float
              {
              if(gavl_value_get_float(&val, &p->perc) && (p->perc >= 0.0))
                draw_seek_slider(p);
              }
            else if(!strcmp(var, BG_PLAYER_STATE_VOLUME))     // float
              {
              }
            else if(!strcmp(var, BG_PLAYER_STATE_STATUS))       // int
              {
              if(!gavl_value_get_int(&val, &p->status))
                return 1;

              draw_status(p);

              
              
              break;
              }
            else if(!strcmp(var, BG_PLAYER_STATE_CURRENT_TRACK))         // dictionary
              {
              const gavl_dictionary_t * track;

              if(!(track = gavl_value_get_dictionary(&val)))
                return 1;
              
              gavl_dictionary_reset(&p->current_track);
              gavl_dictionary_copy(&p->current_track, track);
              
              update_current_track(p);
              wrefresh(p->player_win);
              
              // fprintf(stderr, "Track:\n");
              // gavl_dictionary_dump(track, 2);
              }
            else if(!strcmp(var, BG_PLAYER_STATE_MODE))          // int
              {
              }
            else if(!strcmp(var, BG_PLAYER_STATE_MUTE))          // int
              {
              }
            else if(!strcmp(var, BG_PLAYER_STATE_AUDIO_STREAM_CURRENT))          // int
              {
              int val_i;
              if(!gavl_value_get_int(&val, &val_i))
                return 1;
              //              fprintf(stderr, "Playing audio stream %d\n", val_i);
              }
            else if(!strcmp(var, BG_PLAYER_STATE_VIDEO_STREAM_CURRENT))          // int
              {
              int val_i;
              if(!gavl_value_get_int(&val, &val_i))
                return 1;
              //              fprintf(stderr, "Playing video stream %d\n", val_i);
              }
            else if(!strcmp(var, BG_PLAYER_STATE_SUBTITLE_STREAM_CURRENT))          // int
              {
              int val_i;
              if(!gavl_value_get_int(&val, &val_i))
                return 1;
              //              fprintf(stderr, "Playing subtitle stream %d\n", val_i);
              }
            }
          }
          break;
        }
      break; 

    
    case GAVL_MSG_NS_SRC:
      switch(msg->ID)
        {
        }
      break;
    case BG_MSG_NS_PLAYER:
      
      switch(msg->ID)
        {
        case BG_PLAYER_MSG_CLEANUP:
          //          gavl_log(GAVL_LOG_DEBUG, LOG_DOMAIN, "Player cleaned up");
          break;
        }

      
      break;
    case BG_MSG_NS_DB:
      gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "player: Got DB message %d", msg->ID);
      switch(msg->ID)
        {
        case BG_MSG_DB_SPLICE_CHILDREN:
        case BG_RESP_DB_BROWSE_CHILDREN:
          {
          int idx;
          int del;
          const gavl_value_t * val;
          int current;
          const char * id;
          
          // gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "player: Splice children");
          
          idx = gavl_msg_get_arg_int(msg, 0);
          del = gavl_msg_get_arg_int(msg, 1);
          val = gavl_msg_get_arg_c(msg, 2);
          gavl_track_splice_children(&p->tracks, idx, del, val);


          p->selected = 0; // Otherwise set_selected() returns early

          //          p->selected = -1; // Otherwise set_selected() returns early
          p->scroll_offset = 0;
          
          draw_tracks(p);

          if((id = gavl_track_get_id(&p->current_track)) &&
             ((current = gavl_get_track_idx_by_id(&p->tracks, id)) >= 0))
            set_current(p, current);
          
          set_selected(p, 0);
          }
          break;
        case BG_MSG_DB_OBJECT_CHANGED:
          gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "player: Object changed");
          break;
        }
      
    }

  
  return 1;
  }

static int scroll_to(bg_player_frontend_ncurses_t * p, int position)
  {
  int delta = 0;
  
  if(position - p->scroll_offset > p->tracks_win_rect.h - 1)
    {
    delta = position - p->scroll_offset - p->tracks_win_rect.h + 1;
    
    //    fprintf(stderr, "Scroll down %d %d\n", p->scroll_offset, delta);
    
    //    p->scroll_offset
      
    }
  else if(position < p->scroll_offset)
    {
    delta = position - p->scroll_offset;
    //    fprintf(stderr, "Scroll up %d\n", delta);
    }

  if(!delta)
    return 0;
  
  // wscrl(p->tracks_win, delta);
  p->scroll_offset += delta;
  draw_tracks(p);
  return 1;
  }

static void set_selected(bg_player_frontend_ncurses_t * p, int new_selected)
  {
  if(p->selected == new_selected)
    return;

  scroll_to(p, new_selected);
  
  draw_track(p, p->selected, 0, (p->current == p->selected));
  p->selected = new_selected;
  draw_track(p, p->selected, 1, (p->current == p->selected));
  
  mvwprintw(p->tracks_win, p->tracks_frame_rect.h-1, 1, "[ %03d / %03d ]",
            p->selected + 1, gavl_get_num_tracks(&p->tracks));
  
  wrefresh(p->tracks_win);
  }

static void set_current(bg_player_frontend_ncurses_t * p, int new_current)
  {
  if(p->current == new_current)
    return;

  draw_track(p, p->current, (p->current == p->selected), 0);

  p->current = new_current;
  draw_track(p, p->current, (p->current == p->selected), 1);
  wrefresh(p->tracks_win);

  mvwprintw(p->player_win, 2, p->player_win_rect.w-18, "[ %03d / %03d ]",
            p->current + 1, gavl_get_num_tracks(&p->tracks));
  
  }

static int contains(const gavl_rectangle_i_t * r, int x, int y)
  {
  if((x < r->x) ||
     (y < r->y) ||
     (x > r->x + r->w) ||
     (y > r->y + r->h))
    return 0;
  
  return 1;
  }

static int track_clicked(bg_player_frontend_ncurses_t * p, int x, int y)
  {
  if(!contains(&p->tracks_frame_rect, x, y))
    return -1;

  x -= p->tracks_frame_rect.x;
  y -= p->tracks_frame_rect.y;

  if(!contains(&p->tracks_win_rect, x, y))
    return -1;

  //  x -= p->tracks_win_rect.x;
  y -= p->tracks_win_rect.y;
  
  if(y - p->scroll_offset >= gavl_get_num_tracks(&p->tracks))
    return -1;
  
  return y - p->scroll_offset;
  }

static float seek_clicked(bg_player_frontend_ncurses_t * p, int x, int y)
  {
  if(!contains(&p->player_win_rect, x, y))
    return -1.0;

  x -= p->player_win_rect.x;
  y -= p->player_win_rect.y;

  if((x >= 1) && x < (p->player_win_rect.w-1) && (y == 3))
    return (double)(x - 1) / (p->player_win_rect.w-2);

  else
    return -1.0;
  
  }



static void play_track(bg_player_frontend_ncurses_t * p, int idx)
  {
  gavl_msg_t * msg;
  const gavl_dictionary_t * track = gavl_get_track(&p->tracks, idx);
              
  msg = bg_msg_sink_get(p->ctrl.cmd_sink);
  gavl_msg_set_id_ns(msg, BG_PLAYER_CMD_SET_CURRENT_TRACK, BG_MSG_NS_PLAYER);
  gavl_msg_set_arg_string(msg, 0, gavl_track_get_id(track)); // ID
  bg_msg_sink_put(p->ctrl.cmd_sink);

  msg = bg_msg_sink_get(p->ctrl.cmd_sink);
  gavl_msg_set_id_ns(msg, BG_PLAYER_CMD_PLAY, BG_MSG_NS_PLAYER);
  bg_msg_sink_put(p->ctrl.cmd_sink);
  
  }

static int handle_ncurses_message(void * priv, gavl_msg_t * msg)
  {
  bg_player_frontend_ncurses_t * p = priv;

  p->num_ncurses_msg++;
  
  switch(msg->NS)
    {
    case GAVL_MSG_NS_GUI:
      {
      switch(msg->ID)
        {
        case GAVL_MSG_GUI_KEY_PRESS:
          {
          int key;
          int mask;
          int accel_id = 0;
          
          key = gavl_msg_get_arg_int(msg, 0);
          mask = gavl_msg_get_arg_int(msg, 1);

          if(bg_accelerator_array_has_accel(bg_player_accels,
                                            key, mask, &accel_id))
            {
            bg_player_accel_pressed(p->controllable, accel_id);
            return 1;
            }
          switch(key)
            {
            case GAVL_KEY_UP:
              {
              int new_selected = p->selected - 1;

              if(new_selected >= 0)
                set_selected(p, new_selected);
              }
              break;
            case GAVL_KEY_DOWN:
              {
              int new_selected = p->selected + 1;
              int num_tracks = gavl_get_num_tracks(&p->tracks);
              
              if(new_selected < num_tracks)
                set_selected(p, new_selected);
              }
              break;
            case GAVL_KEY_PAGE_UP:
              {
              int new_selected = p->selected - (p->tracks_win_rect.h-1);
              if(new_selected < 0)
                new_selected = 0;
              set_selected(p, new_selected);
              }
              break;
            case GAVL_KEY_PAGE_DOWN:
              {
              int new_selected = p->selected + p->tracks_win_rect.h-1;
              int num_tracks = gavl_get_num_tracks(&p->tracks);
              
              if(new_selected >= num_tracks)
                new_selected = num_tracks - 1;

              set_selected(p, new_selected);
              }
              break;
            case GAVL_KEY_HOME:
              set_selected(p, 0);
              break;
            case GAVL_KEY_END:
              set_selected(p, gavl_get_num_tracks(&p->tracks) -1);
              break;
            case GAVL_KEY_RETURN:
              play_track(p, p->selected);
              break;
            case GAVL_KEY_q:
              /* Quit (send artifical sigint) */
              bg_sigint_raise();
            }
          }
          break;
        case GAVL_MSG_GUI_WINDOW_COORDS:
          {
          p->win_w = gavl_msg_get_arg_int(msg, 2);
          p->win_h = gavl_msg_get_arg_int(msg, 3);
          redraw(p);
          }
          break;
        case GAVL_MSG_GUI_BUTTON_PRESS:
          {
          int idx;
          int x, y, button, mask;
          double perc;
          
          button = gavl_msg_get_arg_int(msg, 0);
          mask   = gavl_msg_get_arg_int(msg, 1);
          x      = gavl_msg_get_arg_int(msg, 2);
          y      = gavl_msg_get_arg_int(msg, 3);

          if(((idx = track_clicked(p, x, y)) >= 0) &&
             (button == 1) &&
             !mask)
            {
            //            fprintf(stderr, "Clicked track %d\n", idx);
            set_selected(p, idx);
            }

          else if((perc = seek_clicked(p, x, y)) && 
                  (button == 1) && !mask)
            {
            bg_player_seek_perc(p->ctrl.cmd_sink, perc);
            }
          }
          break;
        case GAVL_MSG_GUI_BUTTON_DOUBLECLICK:
          {
          int idx;
          int x, y, button, mask;

          button = gavl_msg_get_arg_int(msg, 0);
          mask   = gavl_msg_get_arg_int(msg, 1);
          x      = gavl_msg_get_arg_int(msg, 2);
          y      = gavl_msg_get_arg_int(msg, 3);

          if(((idx = track_clicked(p, x, y)) >= 0) &&
             (button == 1) &&
             !mask)
            {
            //            fprintf(stderr, "Doubleclicked track %d\n", idx);
            set_selected(p, idx);
            play_track(p, p->selected);
            }
          
          }
          break;
        }
      }
      break;
    }

  return 1;
  }

static void *
create_renderer_ncurses(void)
  {
  bg_player_frontend_ncurses_t * priv;
  priv = calloc(1, sizeof(*priv));
  return priv;
  }

static int
open_renderer_ncurses(void * data, bg_controllable_t * ctrl)
  {
  gavl_msg_t * msg;
  bg_player_frontend_ncurses_t * priv = data;
  
  if(!bg_ncurses_init())
    return 0;
  
  priv->player_win_rect.x = 0;
  priv->player_win_rect.y = 0;
  
  priv->player_win = newwin(5, 10, priv->player_win_rect.y, priv->player_win_rect.x);

  priv->tracks_frame_rect.x = 0;
  priv->tracks_frame_rect.y = 5;
  
  wbkgdset(priv->player_win, COLOR_PAIR(2));

  //  priv->tracks_win = derwin(priv->tracks_frame, 5, 10, 1, 1);

  priv->tracks_win_rect.x = 1;
  priv->tracks_win_rect.y = 1;
  
  priv->tracks_win = newwin(5, 10, priv->tracks_frame_rect.y, priv->tracks_frame_rect.x);
  
  wbkgdset(priv->tracks_win, COLOR_PAIR(1));
  
  priv->ncurses_sink = bg_msg_sink_create(handle_ncurses_message, priv, 1);

  bg_control_init(&priv->ctrl, bg_msg_sink_create(handle_player_message_ncurses, priv, 0));
    
  bg_controllable_connect(ctrl, &priv->ctrl);
  priv->controllable = ctrl;
  
  /* Get track list */
  msg = bg_msg_sink_get(priv->ctrl.cmd_sink);

  bg_mdb_set_browse_children_request(msg, BG_PLAYQUEUE_ID, 0, -1, 0);
  //  gavl_dictionary_set_string(&msg->header, GAVL_MSG_CONTEXT_ID, BG_PLAYQUEUE_ID);
  
  bg_msg_sink_put(priv->ctrl.cmd_sink);
  
  return 1;
  }

bg_frontend_plugin_t the_plugin =
  {
    .common =
    {
      BG_LOCALE,
      .name =      "fe_renderer_ncurses",
      .long_name = TRS("ncurses frontend"),
      .description = TRS("ncurses based player fronntend"),
      .type =     BG_PLUGIN_FRONTEND_RENDERER,
      .flags =    BG_PLUGIN_NEEDS_TERMINAL,
      .create =   create_renderer_ncurses,
      .destroy =   destroy_renderer_ncurses,
      .priority =  1,
    },
    .update = ping_renderer_ncurses,
    .open = open_renderer_ncurses,
  };

/* Include this into all plugin modules exactly once
   to let the plugin loader obtain the API version */
BG_GET_PLUGIN_API_VERSION;
