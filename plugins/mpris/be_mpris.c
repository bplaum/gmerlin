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




#include <string.h>

#include <config.h>


#include <gavl/log.h>
#define LOG_DOMAIN "bg_mpd"
#include <gmerlin/translation.h>

#include <gmerlin/plugin.h>
#include <gmerlin/player.h>
#include <gmerlin/bgdbus.h>

#include "mpris.h"

#define MSG_PLAYER_PROPERTY_CHANGED  1


typedef struct
  {
  bg_controllable_t ctrl;
  gavl_dictionary_t gmerlin_state;
  bg_player_tracklist_t tl;

  bg_dbus_connection_t * conn;

  } renderer_t;

static void destroy_renderer(void * priv);


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

static void set_track_metadata_int(renderer_t * r, const char * tag, int val)
  {
  
  }

static void set_player_status(renderer_t * r, const char * val)
  {
  fprintf(stderr, "Set player status %s\n", val);
  }

static void mpris2_set_player_property(void * priv, const char * name, const gavl_value_t * val)
  {
  renderer_t * r = priv;

    if(!strcmp(name, "PlaybackStatus"))
    {
    const char * str;
    if((str = gavl_value_get_string(val)))
      {
      set_player_status(r, str);
      }
    
    }
  else if(!strcmp(name, "LoopStatus"))
    {
    
    }
  else if(!strcmp(name, "Rate"))
    {
    
    }
  else if(!strcmp(name, "Shuffle"))
    {
    
    }
  else if(!strcmp(name, "Metadata"))
    {
    const gavl_dictionary_t * dict_c;
    
    if((dict_c = gavl_value_get_dictionary(val)))
      {
      gavl_dictionary_t dict;
      const char * title;
      const char * str;
      gavl_dictionary_t * m;
      
      if((p->flags & RENDERER_LOADED_FILE) &&
         p->uri &&
         (str = gavl_dictionary_get_string(dict_c, "xesam:url")) &&
         !strcmp(p->uri, str))
        {
        p->flags |= RENDERER_OUR_PLAYBACK;
        gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Have playback control");
        }
      else
        {
        p->flags &= ~RENDERER_LOADED_FILE;
        p->flags &= ~RENDERER_OUR_PLAYBACK;
        gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Have NO playback control");
        }
      
      gavl_dictionary_init(&dict);

      if(p->flags & RENDERER_OUR_PLAYBACK)
        {
        gavl_dictionary_copy(&dict, bg_player_tracklist_get_current_track(&p->tl));
        m = gavl_dictionary_get_dictionary_create(&dict, GAVL_META_METADATA);
        gavl_dictionary_set(m, META_MPRIS_TRACK_ID, gavl_dictionary_get(dict_c, "mpris:trackid") );
        }
      else
        {
        m = gavl_dictionary_get_dictionary_create(&dict, GAVL_META_METADATA);
        gavl_dictionary_foreach(dict_c, set_metadata_field_mpris2_to_gavl, m);

        if((title = gavl_dictionary_get_string(m, GAVL_META_TITLE)))
          {
          char * artist;
          if((artist = gavl_metadata_join_arr(m, GAVL_META_ARTIST, ", ")))
            {
            gavl_dictionary_set_string_nocopy(m, GAVL_META_LABEL, bg_sprintf("%s - %s", artist, title));
            free(artist);
            }
          else
            gavl_dictionary_set_string(m, GAVL_META_LABEL, title);
          }
        }
      
      /* Set CanSeek and CanPause */

      p->flags &= ~(RENDERER_CAN_PAUSE|RENDERER_CAN_SEEK);
      
      if(bg_dbus_get_int_property(p->conn, p->addr, "/org/mpris/MediaPlayer2",
                                  "org.mpris.MediaPlayer2.Player",
                                  "CanSeek"))
        {
        p->flags |= RENDERER_CAN_SEEK;
        gavl_dictionary_set_int(m, GAVL_META_CAN_SEEK, 1);
        }
      else
        gavl_dictionary_set_int(m, GAVL_META_CAN_SEEK, 0);

      if(bg_dbus_get_int_property(p->conn, p->addr, "/org/mpris/MediaPlayer2",
                                  "org.mpris.MediaPlayer2.Player",
                                  "CanPause"))
        {
        p->flags |= RENDERER_CAN_PAUSE;
        gavl_dictionary_set_int(m, GAVL_META_CAN_PAUSE, 1);
        }
      else
        gavl_dictionary_set_int(m, GAVL_META_CAN_PAUSE, 0);
      
      p->duration = GAVL_TIME_UNDEFINED;
      gavl_dictionary_get_long(m, GAVL_META_APPROX_DURATION, &p->duration);
      
      /* Set duration range */
      if(p->duration != GAVL_TIME_UNDEFINED)
        bg_state_set_range_long(&p->gmerlin_state,
                                BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_TIME, 0, p->duration);
      else
        bg_state_set_range_long(&p->gmerlin_state,
                                BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_TIME, 0, 0);
      
      set_current_track(dev, &dict);
      
      gavl_dictionary_free(&dict);
      }
    
    }
  else if(!strcmp(name, "Volume"))
    {
    bg_state_set(&p->gmerlin_state, 1, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_VOLUME,
                 val, dev->ctrl_int.evt_sink, BG_MSG_STATE_CHANGED);
    }
  else if(!strcmp(name, "Position"))
    {
    gavl_time_t cur = GAVL_TIME_UNDEFINED;
    if(gavl_value_get_long(val, &cur))
      set_player_time(dev, cur);
    }
  else if(!strcmp(name, "MinimumRate"))
    {
    
    }
  else if(!strcmp(name, "MaximumRate"))
    {
    
    }
  else if(!strcmp(name, "CanGoNext"))
    {
    
    }
  else if(!strcmp(name, "CanGoPrevious"))
    {
    
    }
  else if(!strcmp(name, "CanPlay"))
    {
    
    }
  else if(!strcmp(name, "CanPause"))
    {
    int val_i;
    const gavl_value_t * val_c;
    const gavl_dictionary_t * track_c;
    
    if((val_c = bg_state_get(&p->gmerlin_state, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_CURRENT_TRACK)) &&
       (track_c = gavl_value_get_dictionary(val_c)))
      {
      gavl_value_t v;
      gavl_dictionary_t * track;
      gavl_dictionary_t * m;
      
      gavl_value_init(&v);
      track = gavl_value_set_dictionary(&v);
      gavl_dictionary_copy(track, track_c);

      m = gavl_dictionary_get_dictionary_create(track, GAVL_META_METADATA);
      gavl_dictionary_set(m, GAVL_META_CAN_PAUSE, val);
      bg_state_set(&p->gmerlin_state, 1, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_CURRENT_TRACK,
                   &v, dev->ctrl_int.evt_sink, BG_MSG_STATE_CHANGED);
      gavl_value_free(&v);
      }

    val_i = 0;
    if(gavl_value_get_int(val, &val_i) && val_i)
      p->flags |= RENDERER_CAN_PAUSE;
      
    }
  else if(!strcmp(name, "CanSeek"))
    {
    const gavl_value_t * val_c;
    const gavl_dictionary_t * track_c;
    int val_i;
    
    if((val_c = bg_state_get(&p->gmerlin_state, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_CURRENT_TRACK)) &&
       (track_c = gavl_value_get_dictionary(val_c)))
      {
      gavl_value_t v;
      gavl_dictionary_t * track;
      gavl_dictionary_t * m;
      
      gavl_value_init(&v);
      track = gavl_value_set_dictionary(&v);
      gavl_dictionary_copy(track, track_c);

      m = gavl_dictionary_get_dictionary_create(track, GAVL_META_METADATA);
      gavl_dictionary_set(m, GAVL_META_CAN_SEEK, val);
      bg_state_set(&p->gmerlin_state, 1, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_CURRENT_TRACK,
                   &v, dev->ctrl_int.evt_sink, BG_MSG_STATE_CHANGED);
      gavl_value_free(&v);
      }
    val_i = 0;
    if(gavl_value_get_int(val, &val_i) && val_i)
      p->flags |= RENDERER_CAN_SEEK;
    
    }
  else if(!strcmp(name, "CanControl"))
    {
    
    }

  
  
  }


static int handle_dbus_msg(void * data, gavl_msg_t * msg)
  {
  switch(msg->NS)
    {
    case BG_MSG_NS_PRIVATE:

      switch(msg->ID)
        {
        case MSG_PLAYER_PROPERTY_CHANGED:
          break;
        }
      
      break;
    }
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

  if(!(r->conn = bg_dbus_connection_get(DBUS_BUS_SESSION)))
    {
    destroy_renderer(r);
    return NULL;
    }

  r->dbus_sink = bg_msg_sink_create(handle_dbus_msg, r, 0);
  
  return  r;
  }

static int open_renderer(void * priv, const char * uri)
  {
  renderer_t * r = priv;
  const char * pos;
  char * rule;

  /* Find peer address */
  if((pos = strstr(uri, "://")))
    uri = pos + 3;

  real_name = bg_sprintf("%s%s", MPRIS2_NAME_PREFIX, uri);

  r->addr = bg_dbus_get_name_owner(r->conn, real_name);
  
  if(!r->addr)
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "%s not found");

  free(real_name);
  if(!r->addr)
    return 0;

  /* Connect property changed callback */
  rule = bg_sprintf("sender='%s',type='signal',path='/org/mpris/MediaPlayer2',interface='org.freedesktop.DBus.Properties'",
                    r->addr);
  
  bg_dbus_connection_add_listener(r->conn,
                                  rule,
                                  r->dbus_sink, BG_MSG_NS_PRIVATE, MSG_PLAYER_PROPERTY_CHANGED);
  free(rule);
  
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
      .name =      "be_mpd",
      .long_name = TRS("MPD"),
      .description = TRS("Playback via MPDs (music player daemons)"),
      .type =     BG_PLUGIN_BACKEND_RENDERER,
      .flags =    0,
      .create =   create_renderer,
      .destroy =   destroy_renderer,
      .get_controllable =   get_controllable_renderer,
      .priority =         1,
    },
    .protocol = MPRIS_URI_SCHEME,
    .update = update_renderer,
    .open = open_renderer,
  };

/* Include this into all plugin modules exactly once
   to let the plugin loader obtain the API version */
BG_GET_PLUGIN_API_VERSION;
