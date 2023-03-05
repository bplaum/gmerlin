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
#include <config.h>

#include <gavl/metatags.h>

#include <gmerlin/bgdbus.h>
#include <gmerlin/backend.h>
#include <gmerlin/utils.h>
#include <gmerlin/state.h>
#include <gmerlin/playermsg.h>
#include <gmerlin/player.h>
#include <gmerlin/application.h>
#include <gmerlin/mdb.h>

#include <gmerlin/translation.h>
#include <gmerlin/log.h>
#define LOG_DOMAIN "dbus.detector"

#include <backend_priv.h>

/* Player */
#define RENDERER_OUR_PLAYBACK      (1<<0)
#define RENDERER_CLEAR_TRACKLIST   (1<<1) // Clear tracklist from all tracks except the current one
#define RENDERER_FINISHING         (1<<2)
#define RENDERER_CAN_PAUSE         (1<<3) 
#define RENDERER_CAN_SEEK          (1<<4) 
#define RENDERER_TRACKLIST_CHANGED (1<<5)
#define RENDERER_LOADED_FILE       (1<<6)

#undef LOG_DOMAIN

#define LOG_DOMAIN "mpris2"
#define META_MPRIS_TRACK_ID "MprisTrackID"

#define MSG_PLAYER_PROPERTY_CHANGED  1
#define MSG_TRACKLIST_CHANGED        2



/*
 *  Using the TrackList interface was given up
 *  since vlc supports it poorly
 *
 *  * Tracklist only available while the player is playing
 *  * TrackAdded and TrackRemoved events are never sent
 *
 *
 * Furthermore the PlaybackStatus is not evented properly. Thus, we detect the
 * stop <-> playing and playing <-> pause transitions by observing the player time.
 * 
 */

typedef struct
  {
  bg_msg_sink_t * dbus_sink;
  bg_dbus_connection_t * conn;
  
  gavl_dictionary_t gmerlin_state;
  
  gavl_dictionary_t root_properties;
  gavl_dictionary_t player_properties;

  gavl_timer_t * timer;
  
  int player_state;
  gavl_time_t duration;

  char * addr;

  gavl_time_t last_poll_time;
  gavl_time_t last_player_time;
  gavl_time_t max_player_time; // EOF detection logic

  int flags;
  
  bg_player_tracklist_t tl;
  
  gavl_dictionary_t ti;

  char * uri;
  } mpris2_player_t;

static void set_player_status(bg_backend_handle_t * dev, const char * str);
static void set_current_track(bg_backend_handle_t * dev, const gavl_dictionary_t * track);


static const char * get_track_id(const gavl_dictionary_t * dict)
  {
  if((dict = gavl_track_get_metadata(dict)))
    return gavl_dictionary_get_string(dict, META_MPRIS_TRACK_ID);
  return NULL;
  }

static void set_player_time(bg_backend_handle_t * dev, gavl_time_t cur)
  {
  gavl_dictionary_t * t;
  gavl_value_t val;
  
  /* Generate gmerlin compatible time stamps */
  gavl_time_t time_rem;
  gavl_time_t time_abs;
  gavl_time_t time_rem_abs;
  mpris2_player_t * p;
  double percentage;
  
  p = dev->priv;

  gavl_value_init(&val);
  t = gavl_value_set_dictionary(&val);
  
  if(p->flags & RENDERER_OUR_PLAYBACK)
    {
    bg_player_tracklist_get_times(&p->tl,
                                  cur, &time_abs, &time_rem, &time_rem_abs,
                                  &percentage);
    }
  else
    {
    if(p->duration == GAVL_TIME_UNDEFINED)
      {
      time_rem = GAVL_TIME_UNDEFINED;
      percentage = -1.0; 
      }
    else
      {
      time_rem = p->duration - cur;
      percentage = (double)cur / (double)p->duration; 
      }
    time_abs = cur;
    time_rem_abs = time_rem;
    }
  
  gavl_dictionary_set_long(t, BG_PLAYER_TIME, cur);
  gavl_dictionary_set_long(t, BG_PLAYER_TIME_ABS, time_abs);
  gavl_dictionary_set_long(t, BG_PLAYER_TIME_REM, time_rem);
  gavl_dictionary_set_long(t, BG_PLAYER_TIME_REM_ABS, time_rem_abs);
  gavl_dictionary_set_float(t, BG_PLAYER_TIME_PERC, percentage);
  
  bg_state_set(&p->gmerlin_state, 1, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_CURRENT_TIME,
               &val, dev->ctrl_int.evt_sink, BG_MSG_STATE_CHANGED);
  }

static void poll_player_state(bg_backend_handle_t * dev)
  {
  mpris2_player_t * p;
  char * str;

  p = dev->priv;
  str = bg_dbus_get_string_property(p->conn, p->addr, "/org/mpris/MediaPlayer2",
                                    "org.mpris.MediaPlayer2.Player",
                                    "PlaybackStatus");
  set_player_status(dev, str);
  free(str);
  }

static void poll_mpris2(bg_backend_handle_t * dev)
  {
  gavl_time_t cur;

  mpris2_player_t * p;
  p = dev->priv;

  cur = bg_dbus_get_long_property(p->conn,
                                  p->addr,
                                  "/org/mpris/MediaPlayer2",
                                  "org.mpris.MediaPlayer2.Player",
                                  "Position");
  
  set_player_time(dev, cur);
  
  if(p->last_player_time == GAVL_TIME_UNDEFINED)
    p->last_player_time = cur;

  /* Detect stop -> play transition */
  else if(p->player_state == BG_PLAYER_STATUS_PLAYING)
    {
    if(p->last_player_time == cur)
      poll_player_state(dev);
    }
  else if((p->player_state == BG_PLAYER_STATUS_PAUSED) ||
          (p->player_state == BG_PLAYER_STATUS_STOPPED) ||
          (p->player_state == BG_PLAYER_STATUS_CHANGING))
    {
    if(p->last_player_time != cur)
      poll_player_state(dev);
    }

  if(p->max_player_time < cur)
    p->max_player_time = cur;
  }

static void do_stop(bg_backend_handle_t * dev)
  {
  mpris2_player_t * p;
  DBusMessage * req;
  gavl_msg_t * res;
  p = dev->priv;
  
  req = dbus_message_new_method_call(p->addr, "/org/mpris/MediaPlayer2", "org.mpris.MediaPlayer2.Player",
                                     "Stop");
  
  res = bg_dbus_connection_call_method(p->conn, req);
  
  dbus_message_unref(req);

  if(!res)
    gavl_msg_destroy(res);
  
  }


static void remove_track(bg_backend_handle_t * dev, const char * mpris_id)
  {
  DBusMessage * req;
  gavl_msg_t * res;
  mpris2_player_t * p;
  
  p = dev->priv;

  req = dbus_message_new_method_call(p->addr, "/org/mpris/MediaPlayer2", "org.mpris.MediaPlayer2.TrackList",
                                     "RemoveTrack");

  dbus_message_append_args(req, DBUS_TYPE_OBJECT_PATH, &mpris_id, DBUS_TYPE_INVALID);
  
  res = bg_dbus_connection_call_method(p->conn, req);
  
  dbus_message_unref(req);
  
  if(res)
    gavl_msg_destroy(res);
  
  }

/* Returns 0 if the list is not writable */
static int clear_list(bg_backend_handle_t * dev)
  {
  int can_edit = 0;
  mpris2_player_t * p;
  gavl_dictionary_t * props = NULL;
  int ret = 0;
  const gavl_array_t * tracks;
  const char * id;

  p = dev->priv;
  id = get_track_id(&p->ti);
  
  if(!(props = 
       bg_dbus_get_properties(p->conn, p->addr,
                              "/org/mpris/MediaPlayer2",
                              "org.mpris.MediaPlayer2.TrackList")) ||
     !(gavl_dictionary_get_int(props, "CanEditTracks", &can_edit)) ||
     !can_edit)
    goto fail;

  if((tracks = gavl_dictionary_get_array(props, "Tracks")))
    {
    int i;
    const char * str;
    
    if(!tracks->num_entries)
      return 0;
    
    for(i = 0; i < tracks->num_entries; i++)
      {
      str = gavl_value_get_string(&tracks->entries[i]);
      
      if(!id || strcmp(str, id))
        remove_track(dev, str);
      }
    }
  
  ret = 1;

  fail:

  if(props)
    gavl_dictionary_destroy(props);

  return ret;
  }


#if 0

static int load_track_sync(bg_backend_handle_t * dev, gavl_dictionary_t * track)
  {
  int ret = 0;
  int num;
  mpris2_player_t * r = dev->priv;
  while(1)
    {
    bg_msg_sink_iteration(r->dbus_sink);

    num = bg_msg_sink_get_num(r->dbus_sink);
    
    /* Check if we need to synchronize the tracklist */

    if(r->flags & RENDERER_TRACKLIST_CHANGED)
      {
      /* TODO: Synchronize playlist */
      r->flags &= ~RENDERER_TRACKLIST_CHANGED;
      }
    }
  return ret;
  }
#endif

static void do_play(bg_backend_handle_t * dev)
  {
  DBusMessage * req;
  gavl_msg_t * res;

  mpris2_player_t * p = dev->priv;
  
  const char * location = NULL;
  gavl_dictionary_t * m;
  
  if(!(m = bg_player_tracklist_get_current_track(&p->tl)) ||
     !(m = gavl_track_get_metadata_nc(m)))
    return;

  /* TODO: Supported locations might be just higher ones */
  gavl_metadata_get_src(m, GAVL_META_SRC, 0, NULL, &location);
  
  if(!location)
    return;
  
  if(p->uri)
    free(p->uri);
  p->uri = bg_string_to_uri(location, -1);
  p->flags |= RENDERER_LOADED_FILE;
  
  req = dbus_message_new_method_call(p->addr, "/org/mpris/MediaPlayer2", "org.mpris.MediaPlayer2.Player",
                                     "OpenUri");

  dbus_message_append_args(req, DBUS_TYPE_STRING, &p->uri, DBUS_TYPE_INVALID);
  
  res = bg_dbus_connection_call_method(p->conn, req);
  
  dbus_message_unref(req);

  if(!res)
    gavl_msg_destroy(res);
  }

static void mpris2_set_root_property(void * priv, const char * name, const gavl_value_t * val)
  {
  mpris2_player_t * p;
  bg_backend_handle_t * dev = priv;
  p = dev->priv;
  gavl_dictionary_set(&p->root_properties, name, val);
  }

static void set_metadata_field_mpris2_to_gavl(void * priv, const char * name, const gavl_value_t * val)
  {
  if(!strcmp(name, "mpris:trackid"))
    {
    gavl_dictionary_set(priv, META_MPRIS_TRACK_ID, val);
    }
  else if(!strcmp(name, "xesam:url"))
    {
    char * location = bg_uri_to_string(gavl_value_get_string(val), -1);
    gavl_metadata_add_src(priv, GAVL_META_SRC, NULL, location);
    free(location);
    }
  else if(!strcmp(name, "xesam:title"))
    {
    gavl_dictionary_set(priv, GAVL_META_TITLE, val);
    }
  else if(!strcmp(name, "xesam:artist"))
    {
    gavl_dictionary_set(priv, GAVL_META_ARTIST, val);
    }
  else if(!strcmp(name, "xesam:album"))
    {
    gavl_dictionary_set(priv, GAVL_META_ALBUM, val);
    }
  else if(!strcmp(name, "xesam:tracknumber"))
    {
    const char * str;
    
    if((str = gavl_value_get_string(val)))
      gavl_dictionary_set_int(priv, GAVL_META_TRACKNUMBER, atoi(str));
    
    }
  else if(!strcmp(name, "mpris:length"))
    {
    gavl_dictionary_set(priv, GAVL_META_APPROX_DURATION, val);
    }
  else if(!strcmp(name, "xesam:genre"))
    {
    gavl_dictionary_set(priv, GAVL_META_GENRE, val);
    }
  else if(!strcmp(name, "xesam:contentCreated"))
    {
    gavl_dictionary_set(priv, GAVL_META_YEAR, val);
    }
  else if(!strcmp(name, "mpris:artUrl"))
    {
  
    }
  }


static void set_player_status(bg_backend_handle_t * dev, const char * str)
  {
  gavl_value_t v;
  mpris2_player_t * p = dev->priv;

  int last_state = p->player_state;
  int gmerlin_status = BG_PLAYER_STATUS_STOPPED;
  
  if(!strcmp(str, "Paused"))
    gmerlin_status = BG_PLAYER_STATUS_PAUSED;
  else if(!strcmp(str, "Playing"))
    gmerlin_status = BG_PLAYER_STATUS_PLAYING;
  
  
  /* Detect EOF */
  
  if((p->flags & RENDERER_OUR_PLAYBACK) &&
     (gmerlin_status == BG_PLAYER_STATUS_STOPPED) &&
     (last_state == BG_PLAYER_STATUS_PLAYING) &&
     (p->duration > 0) &&
     (abs(p->duration - p->max_player_time) < 10 * GAVL_TIME_SCALE))
    {
    if(bg_player_tracklist_advance(&p->tl, 0))
      {
      gmerlin_status = BG_PLAYER_STATUS_CHANGING;
      do_play(dev);
      }
    }
  
  p->player_state = gmerlin_status;
  
  gavl_value_init(&v);
  gavl_value_set_int(&v, gmerlin_status);
  
  bg_state_set(&p->gmerlin_state, 1, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_STATUS,
               &v, dev->ctrl_int.evt_sink, BG_MSG_STATE_CHANGED);
  

  
  if(p->player_state == BG_PLAYER_STATUS_STOPPED)
    set_player_time(dev, 0);

  if((last_state == BG_PLAYER_STATUS_STOPPED) &&
     (p->player_state == BG_PLAYER_STATUS_PLAYING))
    p->max_player_time = 0;

  }


static void set_current_track(bg_backend_handle_t * dev, const gavl_dictionary_t * track)
  {
  mpris2_player_t * p;
  gavl_value_t v;
  const char * old_id;
  const char * new_id;
  
  p = dev->priv;

  old_id = get_track_id(&p->ti);
  new_id = get_track_id(track);

  if(old_id && new_id && !strcmp(old_id, new_id))
    return;
  
  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Detected track change");

  if(p->flags & RENDERER_OUR_PLAYBACK)
    p->flags |= RENDERER_CLEAR_TRACKLIST;
  
  gavl_dictionary_reset(&p->ti);
  gavl_dictionary_copy(&p->ti, track);
  
  gavl_value_init(&v);
  gavl_dictionary_copy(gavl_value_set_dictionary(&v), track);
  
  bg_state_set(&p->gmerlin_state, 1, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_CURRENT_TRACK,
               &v, dev->ctrl_int.evt_sink, BG_MSG_STATE_CHANGED);
  
  gavl_value_free(&v);
  }

static void mpris2_set_player_property(void * priv, const char * name, const gavl_value_t * val)
  {
  mpris2_player_t * p;
  bg_backend_handle_t * dev = priv;
  p = dev->priv;
  gavl_dictionary_set(&p->player_properties, name, val);
  
  if(!strcmp(name, "PlaybackStatus"))
    {
    const char * str;
    if((str = gavl_value_get_string(val)))
      {
      set_player_status(dev, str);
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
                                BG_PLAYER_STATE_CTX "/" BG_PLAYER_STATE_CURRENT_TIME, BG_PLAYER_TIME,
                                0, p->duration);
      else
        bg_state_set_range_long(&p->gmerlin_state,
                                BG_PLAYER_STATE_CTX "/" BG_PLAYER_STATE_CURRENT_TIME, BG_PLAYER_TIME,
                                0, 0);
      
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

static int handle_dbus_msg_player(void * priv, // Must be bg_backend_handle_t
                                  gavl_msg_t * msg)
  {
  mpris2_player_t * p;
  bg_backend_handle_t * dev = priv;
  p = dev->priv;
  
  switch(msg->NS)
    {
    case BG_MSG_NS_PRIVATE:
      switch(msg->ID)
        {
        case MSG_PLAYER_PROPERTY_CHANGED:
          {
          const gavl_value_t * val;
          const gavl_dictionary_t * dict;
          const gavl_array_t * arr;
          
          const char * interface = gavl_msg_get_arg_string_c(msg, 0);
          
          if(!strcmp(interface, "org.mpris.MediaPlayer2"))
            {
            if((val = gavl_msg_get_arg_c(msg, 1)) &&
               (dict = gavl_value_get_dictionary(val)))
              gavl_dictionary_foreach(dict, mpris2_set_root_property, dev);
            }
          else if(!strcmp(interface, "org.mpris.MediaPlayer2.Player"))
            {
            
            if((val = gavl_msg_get_arg_c(msg, 1)) &&
               (dict = gavl_value_get_dictionary(val)))
              {
              gavl_dictionary_foreach(dict, mpris2_set_player_property, dev);
              }
            }
          else if(!strcmp(interface, "org.mpris.MediaPlayer2.TrackList"))
            {
            
            if((val = gavl_msg_get_arg_c(msg, 1)) &&
               (dict = gavl_value_get_dictionary(val)) &&
               gavl_dictionary_get(dict, "Tracks"))
              {
              p->flags |= RENDERER_TRACKLIST_CHANGED;
              }
            else if((val = gavl_msg_get_arg_c(msg, 2)) &&
                    (arr = gavl_value_get_array(val)) &&
                    (gavl_string_array_indexof(arr, "Tracks") >= 0))
              {
              p->flags |= RENDERER_TRACKLIST_CHANGED;
              }
            }
          }
          break;
        case MSG_TRACKLIST_CHANGED:
          break;
        }
      break;
      
    }
  return 1;
  }

static void add_mimetype(gavl_array_t * arr, const char * type)
  {
  if(gavl_string_array_indexof(arr, type) < 0)
    gavl_string_array_add(arr, type);
  }

static int create_mpris2(bg_backend_handle_t * dev, const char * uri, const char * root_url)
  {
  const char * pos;
  const char * var;
  
  char * rule;
  mpris2_player_t * p;

  gavl_dictionary_t * dict;

  char * icon = NULL;
  char * real_name = NULL;

  const char * identity;
  gavl_array_t * mimetypes;
  
  if((pos = strstr(uri, "://")))
    uri = pos + 3;
  
  p = calloc(1, sizeof(*p));

  p->dbus_sink = bg_msg_sink_create(handle_dbus_msg_player, dev, 0);
  p->timer = gavl_timer_create();
  gavl_timer_start(p->timer);

  // gavl_dictionary_get_dictionary_create(p->cnt, GAVL_META_METADATA);
  
  p->conn = bg_dbus_connection_get(DBUS_BUS_SESSION);

  real_name = bg_sprintf("%s%s", MPRIS2_NAME_PREFIX, uri);
  
  p->addr = bg_dbus_get_name_owner(p->conn, real_name);
  free(real_name);
  
  dev->priv = p;
  
  /* Player properties */
  rule = bg_sprintf("sender='%s',type='signal',path='/org/mpris/MediaPlayer2',interface='org.freedesktop.DBus.Properties'", p->addr);
  
  bg_dbus_connection_add_listener(p->conn,
                                  rule,
                                  p->dbus_sink, BG_MSG_NS_PRIVATE, MSG_PLAYER_PROPERTY_CHANGED);
  free(rule);

#if 1
  rule = bg_sprintf("sender='%s',type='signal',path='/org/mpris/MediaPlayer2',interface='org.mpris.MediaPlayer2.TrackList'", p->addr);
  //  rule = bg_sprintf("sender='%s',type='signal'", addr);
  bg_dbus_connection_add_listener(p->conn,
                                  rule,
                                  p->dbus_sink, BG_MSG_NS_PRIVATE, MSG_TRACKLIST_CHANGED);
  free(rule);
#endif
  
  dict = bg_dbus_get_properties(p->conn, p->addr, "/org/mpris/MediaPlayer2", "org.mpris.MediaPlayer2");

  gavl_dictionary_foreach(dict, mpris2_set_root_property, dev);

  identity = gavl_dictionary_get_string(dict, "Identity");
  mimetypes = gavl_dictionary_get_array_nc(dict, "SupportedMimeTypes");

  if(gavl_string_starts_with(identity, "VLC"))
    {
    add_mimetype(mimetypes, "audio/flac");
    }
  
  bg_player_state_init(&p->gmerlin_state,
                       identity,
                       gavl_dictionary_get_array(dict, "SupportedUriSchemes"),
                       mimetypes);
  
  
  if((var = gavl_dictionary_get_string(dict, "DesktopEntry")))
    {
    char * desktop_file = bg_search_desktop_file(var);

    if(desktop_file)
      {
      const gavl_dictionary_t * s;
      
      gavl_dictionary_t df;
      gavl_dictionary_init(&df);
      bg_read_desktop_file(desktop_file, &df);
        
      if((s = gavl_dictionary_get_dictionary(&df, "Desktop Entry")))
        icon = gavl_strdup(gavl_dictionary_get_string(s, "Icon"));
      
      free(desktop_file);
      gavl_dictionary_free(&df);
      }
    }
  
  bg_set_network_node_info(gavl_dictionary_get_string(dict, "Identity"), NULL, icon, dev->ctrl_int.evt_sink);
  
  dict = bg_dbus_get_properties(p->conn, p->addr, "/org/mpris/MediaPlayer2", "org.mpris.MediaPlayer2.Player");
  
  gavl_dictionary_foreach(dict, mpris2_set_player_property, dev);
  
  /* Tracklist */
  bg_player_tracklist_init(&p->tl, dev->ctrl_int.evt_sink);
  
  
  bg_state_apply(&p->gmerlin_state, dev->ctrl_int.evt_sink, BG_MSG_STATE_CHANGED);

  gavl_dictionary_destroy(dict);

  if(icon)
    free(icon);
  
  return 1;
  }

static void destroy_mpris2(bg_backend_handle_t * dev)
  {
  mpris2_player_t * p = dev->priv;

  bg_dbus_connection_del_listeners(p->conn,
                                   p->dbus_sink);

  gavl_timer_destroy(p->timer);
  
  if(p->conn)
    bg_dbus_connection_unref(p->conn);

  if(p->dbus_sink)
    bg_msg_sink_destroy(p->dbus_sink);

  if(p->addr)
    free(p->addr);

  if(p->uri)
    free(p->uri);
  
  gavl_dictionary_free(&p->gmerlin_state);
  gavl_dictionary_free(&p->player_properties);
  gavl_dictionary_free(&p->root_properties);
  bg_player_tracklist_free(&p->tl);
  
  free(p);
  }

static int ping_mpris2(bg_backend_handle_t * be)
  {
  int ret = 0;
  mpris2_player_t * r;
  gavl_time_t cur;
  
  r = be->priv;

  cur = gavl_timer_get(r->timer);
  
  bg_msg_sink_iteration(r->dbus_sink);
  ret += bg_msg_sink_get_num(r->dbus_sink);

  /* Poll Interval: 0.5 s */
  if(cur - r->last_poll_time > GAVL_TIME_SCALE / 2)
    {
    poll_mpris2(be);
    r->last_poll_time = cur;
    ret++;
    }
  
  /* */

  if(r->flags & RENDERER_CLEAR_TRACKLIST)
    {
    if(clear_list(be))
      r->flags &= ~RENDERER_CLEAR_TRACKLIST;
    }
  return ret;
  }

static void do_seek(bg_backend_handle_t * be, gavl_time_t pos)
  {
  mpris2_player_t * p;
  DBusMessage * req;
  gavl_msg_t * res;
  gavl_time_t cur;
  
  p = be->priv;

  cur = bg_dbus_get_long_property(p->conn, p->addr,
                                  "/org/mpris/MediaPlayer2", "org.mpris.MediaPlayer2.Player",
                                  "Position");

  /* Seek are relative */
  pos -= cur;
  
  req = dbus_message_new_method_call(p->addr, "/org/mpris/MediaPlayer2",
                                     "org.mpris.MediaPlayer2.Player", "Seek");

  dbus_message_append_args(req, DBUS_TYPE_INT64, &pos, DBUS_TYPE_INVALID);
  
  res = bg_dbus_connection_call_method(p->conn, req);
  
  dbus_message_unref(req);

  if(!res)
    gavl_msg_destroy(res);
  }

static int handle_msg_mpris2(void * priv, // Must be bg_backend_handle_t
                             gavl_msg_t * msg)
  {
  mpris2_player_t * r;
  bg_backend_handle_t * be = priv;

  const char * client_id = gavl_msg_get_client_id(msg);
  
  r = be->priv;

#if 1
  if(bg_player_tracklist_handle_message(&r->tl, msg))
    {
    //    if(r->tl.list_changed || r->tl.current_changed)
    //      r->flags &= ~RENDERER_HAS_NEXT;
    
    if(r->tl.list_changed)
      r->tl.list_changed = 0;

    if(r->tl.current_changed)
      {
      r->tl.current_changed = 0;

      if((r->flags & RENDERER_OUR_PLAYBACK) &&
         ((r->player_state == BG_PLAYER_STATUS_PLAYING) ||
          (r->player_state == BG_PLAYER_STATUS_SEEKING) ||
          (r->player_state == BG_PLAYER_STATUS_PAUSED)))
        do_stop(be);
      }
    return 1;
    }
#endif

  switch(msg->NS)
    {
    case BG_MSG_NS_STATE:
      {
      switch(msg->ID)
        {
        case BG_CMD_SET_STATE_REL:
          {
          gavl_msg_t cmd;
          
          gavl_value_t val;
          gavl_value_t add;

          const char * ctx;
          const char * var;
          
          int last = 0;
          
          gavl_value_init(&val);
          gavl_value_init(&add);
          
          bg_msg_get_state(msg, &last, &ctx, &var, &add, NULL);
          
          /* Add (and clamp) value */

          bg_state_add_value(&r->gmerlin_state, ctx, var, &add, &val);
          
          gavl_msg_init(&cmd);
          bg_msg_set_state(&cmd, BG_CMD_SET_STATE, last, ctx, var, &val);
          handle_msg_mpris2(priv, &cmd);
          gavl_msg_free(&cmd);
          
          gavl_value_free(&val);
          gavl_value_free(&add);
          
          //          gavl_dprintf("BG_CMD_SET_STATE_REL");
          //          gavl_msg_dump(msg, 2);
          }
          break;
        case BG_CMD_SET_STATE:
          {
          gavl_value_t val;
          const char * ctx;
          const char * var;
          
          int last = 0;
          
          int player_ctx_len = strlen(BG_PLAYER_STATE_CTX);

          gavl_value_init(&val);

          bg_msg_get_state(msg, &last, &ctx, &var, &val, NULL);
          
          
          if(gavl_string_starts_with(ctx, BG_PLAYER_STATE_CTX) &&
             ((ctx[player_ctx_len] == '/') ||
              (ctx[player_ctx_len] == '\0')))
            {
            if(!strcmp(ctx, BG_PLAYER_STATE_CTX"/"BG_PLAYER_STATE_CURRENT_TIME))          // dictionary
              {
              if(!(r->flags & RENDERER_CAN_SEEK))
                break;
              
              /* Seek */
              if(!strcmp(var, BG_PLAYER_TIME))
                {
                int64_t t = GAVL_TIME_UNDEFINED;
                
                if(gavl_value_get_long(&val, &t))                
                  do_seek(be, t);
                }
              else if(!strcmp(var, BG_PLAYER_TIME_PERC))
                {
                double perc;
                gavl_time_t t;

                if(gavl_value_get_float(&val, &perc))                
                  {
                  t = (int64_t)(perc * ((double)(r->duration)) + 0.5);
                  do_seek(be, t);
                  }
                }
              }
            else if(!strcmp(var, BG_PLAYER_STATE_VOLUME))     // float
              {
              double val_f;
              
              if(!gavl_value_get_float(&val, &val_f))
                break;

              bg_dbus_set_double_property(r->conn,
                                          r->addr,
                                          "/org/mpris/MediaPlayer2",
                                          "org.mpris.MediaPlayer2.Player",
                                          "Volume", val_f);
              
              }
            else if(!strcmp(var, BG_PLAYER_STATE_MODE))          // int
              {
              bg_player_tracklist_set_mode(&r->tl, &val.v.i);
              bg_state_set(&r->gmerlin_state, 1, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_MODE,
                           &val, be->ctrl_int.evt_sink, BG_MSG_STATE_CHANGED);
              }
#if 0
            else if(!strcmp(var, BG_PLAYER_STATE_MUTE))          // int
              {
              gavl_dprintf("BG_CMD_SET_STATE %s %s ", ctx, var);
              gavl_value_dump(&val, 2);
              gavl_dprintf("\n");
              }
            else
              {
              gavl_dprintf("BG_CMD_SET_STATE %s %s ", ctx, var);
              gavl_value_dump(&val, 2);
              gavl_dprintf("\n");
              }
#endif
            }
          else
            {
            
            }

          gavl_value_free(&val);
          }
          break;
        }
      }
      break;
    case BG_MSG_NS_PLAYER:
      switch(msg->ID)
        {
        case BG_PLAYER_CMD_STOP:
          do_stop(be);
          break;
        case BG_PLAYER_CMD_PLAY:
          {
          // gavl_dprintf("BG_PLAYER_CMD_PLAY\n");
          do_play(be);
          }
          break;
        case BG_PLAYER_CMD_PLAY_BY_ID:
          {
          const char * id = gavl_msg_get_arg_string_c(msg, 0);
          if(!bg_player_tracklist_set_current_by_id(&r->tl, id))
            break;
          do_play(be);
          }
          break;


        case BG_PLAYER_CMD_NEXT:
          {
          /* Next() */

          if(r->flags & RENDERER_OUR_PLAYBACK)
            {
            if(!bg_player_tracklist_advance(&r->tl, 1))
              break;
              
            if(r->player_state == BG_PLAYER_STATUS_PLAYING)
              do_play(be);
            
            }
          else if(bg_dbus_get_int_property(r->conn, r->addr, "/org/mpris/MediaPlayer2",
                                           "org.mpris.MediaPlayer2.Player",
                                           "CanGoNext"))
            {
            
            DBusMessage * req;
            gavl_msg_t * res;
            
            req = dbus_message_new_method_call(r->addr, "/org/mpris/MediaPlayer2", "org.mpris.MediaPlayer2.Player",
                                               "Next");
  
            res = bg_dbus_connection_call_method(r->conn, req);
  
            dbus_message_unref(req);

            if(!res)
              gavl_msg_destroy(res);


            }
          
          }
          break;
        case BG_PLAYER_CMD_PREV:

          {
          if(r->flags & RENDERER_OUR_PLAYBACK)
            {
            if(!bg_player_tracklist_back(&r->tl))
              break;
              
            if(r->player_state == BG_PLAYER_STATUS_PLAYING)
              do_play(be);
            
            }
          else if(bg_dbus_get_int_property(r->conn, r->addr, "/org/mpris/MediaPlayer2",
                                           "org.mpris.MediaPlayer2.Player",
                                           "CanGoPrevious"))
            {
            
            DBusMessage * req;
            gavl_msg_t * res;
            
            req = dbus_message_new_method_call(r->addr, "/org/mpris/MediaPlayer2", "org.mpris.MediaPlayer2.Player",
                                               "Previous");
            
            res = bg_dbus_connection_call_method(r->conn, req);
  
            dbus_message_unref(req);

            if(!res)
              gavl_msg_destroy(res);


            }
          }
          break;
        case BG_PLAYER_CMD_PAUSE:
          if(!(r->flags & RENDERER_CAN_PAUSE))
            break;
          
          if(r->player_state == BG_PLAYER_STATUS_PLAYING)
            {
            DBusMessage * req;
            gavl_msg_t * res;
  
            req = dbus_message_new_method_call(r->addr, "/org/mpris/MediaPlayer2", "org.mpris.MediaPlayer2.Player",
                                               "Pause");
  
            res = bg_dbus_connection_call_method(r->conn, req);
  
            dbus_message_unref(req);

            if(!res)
              gavl_msg_destroy(res);
            }
          else if(r->player_state == BG_PLAYER_STATUS_PAUSED)
            {
            DBusMessage * req;
            gavl_msg_t * res;
  
            req = dbus_message_new_method_call(r->addr, "/org/mpris/MediaPlayer2", "org.mpris.MediaPlayer2.Player",
                                               "Play");
  
            res = bg_dbus_connection_call_method(r->conn, req);
  
            dbus_message_unref(req);

            if(!res)
              gavl_msg_destroy(res);
            }
          break;
        case BG_PLAYER_CMD_SET_TRACK:
          {
          gavl_value_t val;
          gavl_value_init(&val);
          
          gavl_msg_get_arg(msg, 0, &val);
          
          do_stop(be);
          
          bg_player_tracklist_splice(&r->tl, 0, -1, &val, client_id);
          gavl_value_free(&val);
          
          /* Set current track */
          bg_player_tracklist_set_current_by_idx(&r->tl, 0);
          }
          break;
        case BG_PLAYER_CMD_SET_LOCATION:
          {
          char * id;
          gavl_msg_t msg1;
          
          do_stop(be);
          
          gavl_msg_init(&msg1);

          /* After the last track */

          bg_mdb_set_load_uri(&msg1, BG_PLAYQUEUE_ID, -1, gavl_msg_get_arg_string_c(msg, 0));

          bg_player_tracklist_handle_message(&r->tl, &msg1);

          id = bg_player_tracklist_id_from_uri(NULL, gavl_msg_get_arg_string_c(msg, 0));
          bg_player_tracklist_set_current_by_id(&r->tl, id);
          free(id);
          
          gavl_msg_free(&msg1);
          
          if(gavl_msg_get_arg_int(msg, 1))
            do_play(be);
          }
          break;
        }
      break;

    case GAVL_MSG_NS_GENERIC:

      switch(msg->ID)
        {
        case GAVL_CMD_QUIT:
          return 0;
          break;
        }

    default:
      gavl_dprintf("Unknown msg namespace");
      gavl_msg_dump(msg, 2);
      break;
    }

  return 1;
  }

const bg_remote_dev_backend_t bg_remote_dev_backend_mpris2_player =
  {
    .name = "mpris2 player",
    .uri_prefix = BG_DBUS_MPRIS_URI_SCHEME"://",
    .type = BG_BACKEND_RENDERER,
    
    .handle_msg = handle_msg_mpris2,
    .ping    = ping_mpris2,
    .create    = create_mpris2,
    .destroy   = destroy_mpris2,
  };
