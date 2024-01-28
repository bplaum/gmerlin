#include <string.h>

#include <config.h>


#include <gmerlin/parameter.h>
#include <gmerlin/bgmsg.h>

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

  bg_control_t ctrl;
  
  } bg_player_frontend_console_t;

#if 1
static void print_time(bg_player_frontend_console_t * priv, gavl_time_t time)
  {
  if(!priv->display_time)
    return;
  bg_print_time(stderr, time, priv->total_time);
  }
#endif

static int handle_player_message_console(void * data,
                                         gavl_msg_t * msg)
  {
  bg_player_frontend_console_t * priv = data;
  
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
              gavl_time_t t = GAVL_TIME_UNDEFINED;
              if(!gavl_value_get_long(&val, &t))
                return 1;
              print_time(priv, t);
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
              
              fprintf(stderr, "Track changed:\n");
              gavl_dictionary_dump(m, 2);
              fprintf(stderr, "\n");
              }
            else if(!strcmp(var, BG_PLAYER_STATE_MODE))          // int
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

static int ping_renderer_console(void * data)
  {
  int ret = 0;
  bg_player_frontend_console_t * p = data;

  /* Handle player message */
  bg_msg_sink_iteration(p->ctrl.evt_sink);
  ret += bg_msg_sink_get_num(p->ctrl.evt_sink);
  
  return ret;
  }

static void destroy_renderer_console(void * priv)
  {
  bg_player_frontend_console_t * p = priv;
  bg_control_cleanup(&p->ctrl);
  free(p);
  }


static int open_renderer_console(void * data,
                                 bg_controllable_t * ctrl)
  {
  bg_player_frontend_console_t * priv = data;
  
  bg_control_init(&priv->ctrl, bg_msg_sink_create(handle_player_message_console, priv, 0));
  bg_controllable_connect(ctrl, &priv->ctrl);

  return 1;

  }

static void * create_renderer_console()
  {
  bg_player_frontend_console_t * priv;
  priv = calloc(1, sizeof(*priv));
  priv->display_time = 1;
  return priv;
  }

bg_frontend_plugin_t the_plugin =
  {
    .common =
    {
      BG_LOCALE,
      .name =      "fe_renderer_console",
      .long_name = TRS("Console frontend"),
      .description = TRS("Console frontend for commandline applications"),
      .type =     BG_PLUGIN_FRONTEND_RENDERER,
      .flags =    BG_PLUGIN_NEEDS_TERMINAL,
      .create =   create_renderer_console,
      .destroy =   destroy_renderer_console,
      .priority =         1,
    },
    .update = ping_renderer_console,
    .open = open_renderer_console,
  };

/* Include this into all plugin modules exactly once
   to let the plugin loader obtain the API version */
BG_GET_PLUGIN_API_VERSION;
