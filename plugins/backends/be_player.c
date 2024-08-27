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


/*
 *  This is a template using remote
 *  players as backends for gmerlin
 *
 *  Use this, if you want to control upnp- or mpris players
 *  (or others) as player backends
 *
 */

#include <string.h>

#include <config.h>


#include <gavl/log.h>
#define LOG_DOMAIN "be_player"
#include <gmerlin/translation.h>

#include <gmerlin/plugin.h>
#include <gmerlin/player.h>


typedef struct
  {
  bg_controllable_t ctrl;
  gavl_dictionary_t gmerlin_state;
  bg_player_tracklist_t tl;
  } renderer_t;

static int handle_msg(void * data, gavl_msg_t * msg)
  {
  renderer_t * r = data;
  
  if(bg_player_tracklist_handle_message(&r->tl, msg))
    {
    
    if(r->tl.current_changed)
      {
      r->tl.current_changed = 0;
      /* Stop and propably restart */
      }
    return 1;
    }
  
  switch(msg->NS)
    {
    case BG_MSG_NS_STATE:
      bg_state_handle_set_rel(&r->gmerlin_state, msg);
      
      switch(msg->ID)
        {
        case BG_CMD_SET_STATE:
          {
          gavl_value_t val;
          const char * ctx;
          const char * var;

          int last = 0;
          
          gavl_value_init(&val);

          gavl_msg_get_state(msg, &last, &ctx, &var, &val, NULL);
          if(!strcmp(ctx, BG_PLAYER_STATE_CTX))
            {
            
            if(!strcmp(var, BG_PLAYER_STATE_TIME))
              {
              /* Seek */
              gavl_time_t t = GAVL_TIME_UNDEFINED;

              /* Seek absolute */
              if(gavl_value_get_long(&val, &t) &&
                 (t != GAVL_TIME_UNDEFINED))
                {
                /* Do seek */
                }
              }
            else if(!strcmp(var, BG_PLAYER_STATE_TIME_PERC))
              {
              /* Seek percentage */
              double percentage;
              if(gavl_value_get_float(&val, &percentage))
                {
                /* Do seek */
                
                }
              
              }
            else if(!strcmp(var, BG_PLAYER_STATE_VOLUME))     // float
              {
              double volume;
              if(gavl_value_get_float(&val, &volume))
                ;
              
              }
            else if(!strcmp(var, BG_PLAYER_STATE_MODE))          // int
              {
              if(val.type != GAVL_TYPE_INT)
                return 1;
              bg_player_tracklist_set_mode(&r->tl, &val.v.i);
              }
            else if(!strcmp(var, BG_PLAYER_STATE_MUTE))          // int
              {
              if(val.type != GAVL_TYPE_INT)
                return 1;
              val.v.i &= 1;
              /* Mute */
              }
            
            }
          }
          break;
          
        }
      
      break;

    case BG_MSG_NS_PLAYER:
      {
      switch(msg->ID)
        {
        case BG_PLAYER_CMD_NEXT:
        case BG_PLAYER_CMD_PREV:
          {
          if(msg->ID == BG_PLAYER_CMD_NEXT)
            {
            if(!bg_player_tracklist_advance(&r->tl, 1))
              break;
            }
          else if(msg->ID == BG_PLAYER_CMD_PREV)
            {
            if(!bg_player_tracklist_back(&r->tl))
              break;
            }
          /* Play or not */
          }
          break;
        case BG_PLAYER_CMD_PLAY:
          {
          
          }
          break;
        case BG_PLAYER_CMD_STOP:
          {
          
          }
          break;
        case BG_PLAYER_CMD_PAUSE:
          {
          
          }
          break;
        }
      }
      break;
    case GAVL_MSG_NS_GENERIC:
      switch(msg->ID)
        {
        case GAVL_CMD_QUIT:
          {
          return 0;
          }
          break;
        }
      break;
    }
  return 1;
  }

static int update_renderer(void * priv)
  {
  renderer_t * r = priv;
  bg_msg_sink_iteration(r->ctrl.cmd_sink);
  return bg_msg_sink_get_num(r->ctrl.cmd_sink);
  }

static void * create_renderer()
  {
  renderer_t * r;

  r = calloc(1, sizeof(*r));

  bg_controllable_init(&r->ctrl,
                       bg_msg_sink_create(handle_msg, r, 0),
                       bg_msg_hub_create(1));
  
  return  r;
  }

static int open_renderer(void * priv, const char * uri)
  {
  renderer_t * r = priv;
  return 0;
  }

static void destroy_renderer(void * priv)
  {
  renderer_t * r = priv;
  
  bg_controllable_cleanup(&r->ctrl);
  gavl_dictionary_free(&r->gmerlin_state);

  free(r);
  }

static bg_controllable_t * get_controllable_renderer(void * priv)
  {
  renderer_t * r = priv;
  return &r->ctrl;
  }

bg_backend_plugin_t the_plugin =
  {
    .common =
    {
      BG_LOCALE,
      .name =      "be_player",
      .long_name = TRS("Player"),
      .description = TRS("Remote player backends (template)"),
      .type =     BG_PLUGIN_BACKEND_RENDERER,
      .flags =    0,
      .create =   create_renderer,
      .destroy =   destroy_renderer,
      .get_controllable =   get_controllable_renderer,
      .priority =         1,
    },

    .update = update_renderer,
    .open = open_renderer,
  };

/* Include this into all plugin modules exactly once
   to let the plugin loader obtain the API version */
BG_GET_PLUGIN_API_VERSION;
