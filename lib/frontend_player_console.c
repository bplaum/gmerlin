#include <string.h>

#include <config.h>


#include <gmerlin/parameter.h>
#include <gmerlin/msgqueue.h>

#include <gmerlin/player.h>
#include <gmerlin/playermsg.h>
#include <gmerlin/frontend.h>

#include <gmerlin/bggavl.h>

#include <frontend_priv.h>

#include <gmerlin/translation.h>
#include <gmerlin/log.h>
#define LOG_DOMAIN "consoleplayer"

typedef struct 
  {
  int display_time;
  int time_active;
  gavl_time_t total_time;
  
  } bg_player_frontend_console_t;

#if 1
static void print_time(bg_frontend_t * fe, gavl_time_t time)
  {
  bg_player_frontend_console_t * priv = fe->priv;
  
  if(!priv->display_time)
    return;
  bg_print_time(stderr, time, priv->total_time);
  }
#endif

static int handle_player_message(void * data,
                                 gavl_msg_t * msg)
  {
  bg_frontend_t * fe = data;
  bg_player_frontend_console_t * priv = fe->priv;
  
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

          bg_msg_get_state(msg,
                           NULL,
                           &ctx,
                           &var,
                           &val, NULL);

          if(!strcmp(ctx, BG_PLAYER_STATE_CTX))
            {
            if(!strcmp(var, BG_PLAYER_STATE_CURRENT_TIME))          // long
              {
              const gavl_dictionary_t * dict;
              gavl_time_t t = GAVL_TIME_UNDEFINED;
              
              if(!(dict = gavl_value_get_dictionary(&val)) ||
                 !gavl_dictionary_get_long(dict, BG_PLAYER_TIME, &t))
                return 1;
              print_time(fe, t);
              }
            else if(!strcmp(var, BG_PLAYER_STATE_VOLUME))     // float
              {
              }
            else if(!strcmp(var, BG_PLAYER_STATE_STATUS))       // int
              {
              int status;
              
              if(!gavl_value_get_int(&val, &status))
                return 1;

              switch(status)
                {
                case BG_PLAYER_STATUS_STOPPED:
                  break;
                case BG_PLAYER_STATUS_PLAYING:
                  gavl_log(GAVL_LOG_DEBUG, LOG_DOMAIN, "Player now playing");
                  break;
                case BG_PLAYER_STATUS_SEEKING:
                  if(priv->time_active) { putc('\n', stderr); priv->time_active = 0; }
                  gavl_log(GAVL_LOG_DEBUG, LOG_DOMAIN, "Player now seeking");
                  break;
                case BG_PLAYER_STATUS_CHANGING:
                  gavl_log(GAVL_LOG_DEBUG, LOG_DOMAIN, "Player now changing");
                  if(priv->time_active) { putc('\n', stderr); priv->time_active = 0; }
                  break;
                case BG_PLAYER_STATUS_PAUSED:
                  break;
                case BG_PLAYER_STATUS_QUIT:
                  fe->flags |= BG_FRONTEND_FLAG_FINISHED;
                  break;
                }
              
              break;
              }
            else if(!strcmp(var, BG_PLAYER_STATE_CURRENT_TRACK))         // dictionary
              {
              const gavl_dictionary_t * m;
              const gavl_dictionary_t * track;
              
              if(!(track = gavl_value_get_dictionary(&val)) ||
                 !(m = gavl_track_get_metadata(track)))
                return 1;
              
              priv->total_time = GAVL_TIME_UNDEFINED;
              gavl_dictionary_get_long(m, GAVL_META_APPROX_DURATION, &priv->total_time);
              
              //              fprintf(stderr, "Track:\n");
              //              gavl_dictionary_dump(track, 2);
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
              fprintf(stderr, "Playing audio stream %d\n", val_i);
              }
            else if(!strcmp(var, BG_PLAYER_STATE_VIDEO_STREAM_CURRENT))          // int
              {
              int val_i;
              if(!gavl_value_get_int(&val, &val_i))
                return 1;
              fprintf(stderr, "Playing video stream %d\n", val_i);
              }
            else if(!strcmp(var, BG_PLAYER_STATE_SUBTITLE_STREAM_CURRENT))          // int
              {
              int val_i;
              if(!gavl_value_get_int(&val, &val_i))
                return 1;
              fprintf(stderr, "Playing subtitle stream %d\n", val_i);
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
          gavl_log(GAVL_LOG_DEBUG, LOG_DOMAIN, "Player cleaned up");
          break;
        }

      
      break;
    }
  
  
  return 1;
  }

static int ping_player_console(bg_frontend_t * fe, gavl_time_t current_time)
  {
  int ret = 0;
  //  bg_player_frontend_console_t * p = fe->priv;

  /* Handle player message */
  bg_msg_sink_iteration(fe->ctrl.evt_sink);
  ret += bg_msg_sink_get_num(fe->ctrl.evt_sink);
  
  return ret;
  }

static void cleanup_player_console(void * priv)
  {
  bg_player_frontend_console_t * p = priv;
  free(p);
  }


bg_frontend_t *
bg_frontend_create_player_console(bg_controllable_t * ctrl, int display_time)
  {
  bg_player_frontend_console_t * priv;
  
  bg_frontend_t * ret = bg_frontend_create(ctrl);

  ret->ping_func    =    ping_player_console;
  ret->cleanup_func = cleanup_player_console;
  
  priv = calloc(1, sizeof(*priv));

  priv->display_time = display_time;
  
  ret->priv = priv;

  /* Initialize state variables */
  
  bg_control_init(&ret->ctrl, bg_msg_sink_create(handle_player_message, ret, 0));
  
  bg_frontend_init(ret);

  bg_controllable_connect(ctrl, &ret->ctrl);
  
  return ret;
  }
