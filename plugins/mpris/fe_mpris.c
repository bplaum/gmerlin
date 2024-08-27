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
#define LOG_DOMAIN "fe_mpris"


#include <gmerlin/translation.h>

#include <gmerlin/frontend.h>
#include <gmerlin/bgdbus.h>
#include <gmerlin/state.h>
#include <gmerlin/playermsg.h>
#include <gmerlin/utils.h>
#include <gmerlin/player.h>
#include <gmerlin/application.h>
#include <gmerlin/backend.h>
#include <gmerlin/mdb.h>
#include <gmerlin/resourcemanager.h>

#include <gavl/metatags.h>

#define MSG_ID_MPRIS2 1

#define OBJ_PATH "/org/mpris/MediaPlayer2"

#define MPRIS2_NAME_PREFIX     "org.mpris.MediaPlayer2."
#define MPRIS2_NAME_PREFIX_LEN 23

static const char * introspect_xml;

/* TODO:
 *
 * Interface MediaPlayer2.Player
 *
 * Methods:
 * OpenUri 	(s: Uri)
 *
 * Signals:
 * Seeked 	(x: Position)
 * 
 * Properties:
 * CanGoNext 	 b
 * CanGoPrevious b
 *
 * Interface MediaPlayer2
 *
 * Methods:
 * Raise 	()
 * Quit 	()
 *
 * Properties:
 * Fullscreen 	b 
 *
 * org.mpris.MediaPlayer2.TrackList
 *
 * Methods:
 * AddTrack(s: Uri, o: AfterTrack, b: SetAsCurrent) 	-> 	nothing 	
 * RemoveTrack(o: TrackId) 	-> 	nothing 	
 * GoTo(o: TrackId) 	-> 	nothing 	
 *
 * Signals:
 * TrackListReplaced 	(ao: Tracks, o: CurrentTrack) 	
 * TrackAdded 	(a{sv}: Metadata, o: AfterTrack) 	
 * TrackRemoved 	(o: TrackId) 	
 * TrackMetadataChanged 	(o: TrackId, a{sv}: Metadata) 	
 */

static const bg_dbus_property_t 
metadata_map[] =
  {
    { "mpris:trackid",         "o" },
    { "mpris:length",          "x" },
    { "mpris:artUrl",          "s" },
    { "xesam:album",           "s" },
    { "xesam:albumArtist",    "as" },
    { "xesam:artist",         "as" },
    { "xesam:asText",          "s" },
    { "xesam:audioBPM",        "u" },
    { "xesam:autoRating",      "f" },
    { "xesam:comment",        "as" },
    { "xesam:composer",       "as" },
    { "xesam:contentCreated",  "s" },
    { "xesam:discNumber",      "u" },
    { "xesam:url"              "s" },
    { "xesam:useCount"         "s" },
    { "xesam:userRating",      "s" },
    { "xesam:firstUsed",       "s" },
    { "xesam:genre",          "as" },
    { "xesam:lastUsed",        "s" },
    { "xesam:lyricist",       "as" },
    { "xesam:title",           "s" },
    { "xesam:trackNumber",     "u" },
    { "xesam:uri",             "s" },
    { "xesam:useCount",        "u" },
    { "xesam:userRating",      "f" },
    { /* End */                    }
  };

static void val_to_variant_metadata(const gavl_value_t * val, DBusMessageIter *iter)
  {
  int i;
  const gavl_dictionary_t * dict;
  const gavl_value_t * v;

  DBusMessageIter subiter;
  DBusMessageIter subsubiter;

  //  fprintf(stderr, "val_to_variant_metadata\n");
  //  gavl_value_dump(val, 2);
  
  if((dict = gavl_value_get_dictionary(val)))
    {
    i = 0;

    dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY, "{sv}", &subiter);
    
    while(metadata_map[i].name)
      {
      if((v = gavl_dictionary_get(dict, metadata_map[i].name)))
        {
        dbus_message_iter_open_container(&subiter, DBUS_TYPE_DICT_ENTRY, NULL, &subsubiter);
        dbus_message_iter_append_basic(&subsubiter, DBUS_TYPE_STRING, &metadata_map[i].name);
        bg_value_to_dbus_variant(metadata_map[i].dbus_type, v, &subsubiter, metadata_map[i].val_to_variant);
        dbus_message_iter_close_container(&subiter, &subsubiter);
        }
      i++;
      }
    dbus_message_iter_close_container(iter, &subiter);
    }
  }

static const bg_dbus_property_t root_properties[] =
  {
    { "CanQuit",              "b", 0, GAVL_VALUE_INIT_INT(0) }, 		
    { "Fullscreen",           "b", 1, GAVL_VALUE_INIT_INT(0) },
    { "CanSetFullscreen",     "b", 0, GAVL_VALUE_INIT_INT(1) },
    { "CanRaise",             "b", 0, GAVL_VALUE_INIT_INT(0) },
    { "HasTrackList", 	      "b", 0, GAVL_VALUE_INIT_INT(1) },
    { "Identity", 	      "s", 0                         },
    { "DesktopEntry",         "s", 0                         },
    { "SupportedUriSchemes", "as", 0                         },
    { "SupportedMimeTypes",  "as", 0                         },
    { /* End */ }
  };

static const bg_dbus_property_t player_properties[] =
  {
    { "PlaybackStatus", "s",     0, GAVL_VALUE_INIT_STRING("Stopped")                         },
    { "LoopStatus",     "s",     1, GAVL_VALUE_INIT_STRING("None")                            },
    { "Rate",           "d",     1, GAVL_VALUE_INIT_FLOAT(1.0)                                },
    { "Shuffle",        "b", 	 1, GAVL_VALUE_INIT_INT(0)                                    },
    { "Metadata",       "a{sv}", 0, {},                               val_to_variant_metadata },
    { "Volume",         "d",     1, GAVL_VALUE_INIT_FLOAT(0.0)                                }, 
    { "Position",       "x",     0, GAVL_VALUE_INIT_LONG(1)                                   }, 
    { "MinimumRate",    "d",     0, GAVL_VALUE_INIT_FLOAT(1.0)                                },
    { "MaximumRate",    "d",     0, GAVL_VALUE_INIT_FLOAT(1.0)                                },
    { "CanGoNext",      "b",     0, GAVL_VALUE_INIT_INT(1)                                    },
    { "CanGoPrevious",  "b",     0, GAVL_VALUE_INIT_INT(1)                                    },
    { "CanPlay",        "b",     0, GAVL_VALUE_INIT_INT(1)                                    },
    { "CanPause",       "b",     0, GAVL_VALUE_INIT_INT(1)                                    },
    { "CanSeek",        "b",     0, GAVL_VALUE_INIT_INT(1)                                    },
    { "CanControl",     "b",     0, GAVL_VALUE_INIT_INT(1)                                    },
    { /* End */ }
  };

static const bg_dbus_property_t tracklist_properties[] =
  {
    { "Tracks",         "ao",    0, {},                                                       },
    { "CanEditTracks",  "b",     0, GAVL_VALUE_INIT_INT(1)                                    },
    { /* */ },
  };

typedef struct
  {
  bg_dbus_connection_t * conn;
  bg_msg_sink_t * dbus_sink;

  gavl_dictionary_t root_prop;
  gavl_dictionary_t player_prop;
  gavl_dictionary_t tracklist_prop;
  gavl_dictionary_t state;

  gavl_array_t tracks;
  
  int track_counter;

  char * bus_name;

  //  bg_controllable_t * ctrl;

  bg_control_t control;
  
  } mpris2_t;


static const char * mode_to_loop_status(int gmerlin_mode)
  {
  switch(gmerlin_mode)
    {
    case BG_PLAYER_MODE_REPEAT:  //!< Repeat current album
    case BG_PLAYER_MODE_SHUFFLE: //!< Shuffle (implies repeat)
      return "Playlist";
      break;
    case BG_PLAYER_MODE_LOOP:    //!< Loop current track
      return "Track";
      break;
    }
  return "None";
  }

static int mode_to_shuffle(int gmerlin_mode)
  {
  if(gmerlin_mode == BG_PLAYER_MODE_SHUFFLE)
    return 1;
  else
    return 0;
  }

static int get_gmerlin_mode(int shuffle, const char * loop_status)
  {
  if(shuffle)
    return BG_PLAYER_MODE_SHUFFLE;
  
  if(!strcmp(loop_status, "Playlist"))
    return BG_PLAYER_MODE_REPEAT;
  else if(!strcmp(loop_status, "Track"))
    return BG_PLAYER_MODE_LOOP;
  else
    return BG_PLAYER_MODE_NORMAL;
  }

static void send_ack(mpris2_t * p, gavl_msg_t * msg)
  {
  DBusMessage * reply;
      
  reply = bg_dbus_msg_new_reply(msg);
  bg_dbus_send_msg(p->conn, reply);
  dbus_message_unref(reply);
  
  }

static int handle_msg_mpris2(void * priv, gavl_msg_t * msg)
  {
  const char * interface;
  const char * member;
  mpris2_t * p;
  char * error_msg = NULL;
  
  p = priv;

  //  fprintf(stderr, "Handle dbus message\n");
  //  gavl_msg_dump(msg, 2);
  
  interface = gavl_dictionary_get_string(&msg->header, BG_DBUS_META_INTERFACE);
  member = gavl_dictionary_get_string(&msg->header, BG_DBUS_META_MEMBER);
  
  if(!strcmp(interface, "org.freedesktop.DBus.Properties"))
    {
    const char * prop_name = NULL;
    const char * prop_iface = gavl_msg_get_arg_string_c(msg, 0);
    
    if(!strcmp(member, "Get"))
      {
      prop_name = gavl_msg_get_arg_string_c(msg, 1);

      if(!strcmp(prop_iface, "org.mpris.MediaPlayer2"))
        bg_dbus_property_get(root_properties, &p->root_prop, prop_name, msg, p->conn);
      else if(!strcmp(prop_iface, "org.mpris.MediaPlayer2.TrackList"))
        bg_dbus_property_get(tracklist_properties, &p->tracklist_prop, prop_name, msg, p->conn);
      else if(!strcmp(prop_iface, "org.mpris.MediaPlayer2.Player"))
        bg_dbus_property_get(player_properties, &p->player_prop, prop_name, msg, p->conn);
      else
        {
        char * error_msg =
          bg_sprintf("org.freedesktop.DBus.Properties.Get failed: No such interface %s", prop_iface);
        
        bg_dbus_send_error(p->conn, msg, NULL, error_msg);
        free(error_msg);
        }
      }
    else if(!strcmp(member, "Set"))
      {
      prop_name = gavl_msg_get_arg_string_c(msg, 1);

      if(!strcmp(prop_iface, "org.mpris.MediaPlayer2"))
        {
        if(!strcmp(prop_name, "Fullscreen"))
          {
          
          }
        else
          {
          error_msg =
            bg_sprintf("org.freedesktop.DBus.Properties.Set failed: Property %s in interface %s doesn't exist or is read-only",
                       prop_name, prop_iface);
        
          bg_dbus_send_error(p->conn, msg, NULL, error_msg);
          free(error_msg);
          }
        }
      else if(!strcmp(prop_iface, "org.mpris.MediaPlayer2.Player"))
        {
        if(!strcmp(prop_name, "LoopStatus"))
          {
          gavl_msg_t * msg1;
          const char * loop_status;
          int shuffle = 0;
          gavl_value_t v;
          
          gavl_dictionary_get_int(&p->player_prop, "Shuffle", &shuffle);
          loop_status = gavl_msg_get_arg_string_c(msg, 2);

          if(!loop_status)
            loop_status = "None";

          msg1 = bg_msg_sink_get(p->control.cmd_sink);

          gavl_value_init(&v);
          gavl_value_set_int(&v, get_gmerlin_mode(shuffle, loop_status));

          gavl_msg_set_state(msg1,
                           BG_CMD_SET_STATE, 1,
                           BG_PLAYER_STATE_CTX,
                           BG_PLAYER_STATE_MODE,
                           &v);
          
          bg_msg_sink_put(p->control.cmd_sink);
          }
        else if(!strcmp(prop_name, "Shuffle"))
          {
          gavl_msg_t * msg1;
          const char * loop_status;
          int shuffle = 0;
          gavl_value_t v;
          
          loop_status = gavl_dictionary_get_string(&p->player_prop, "LoopStatus");
          shuffle = gavl_msg_get_arg_int(msg, 2);

          if(!loop_status)
            loop_status = "None";

          msg1 = bg_msg_sink_get(p->control.cmd_sink);

          gavl_value_init(&v);
          gavl_value_set_int(&v, get_gmerlin_mode(shuffle, loop_status));

          gavl_msg_set_state(msg1,
                           BG_CMD_SET_STATE, 1,
                           BG_PLAYER_STATE_CTX,
                           BG_PLAYER_STATE_MODE,
                           &v);
          
          bg_msg_sink_put(p->control.cmd_sink);
          }
#if 0 // Nop
        else if(!strcmp(prop_name, "Rate"))
          {
          
          }
#endif
        else if(!strcmp(prop_name, "Volume"))
          {
          bg_player_set_volume(p->control.cmd_sink, gavl_msg_get_arg_float(msg, 2));
          }
        else
          {
          error_msg =
            bg_sprintf("org.freedesktop.DBus.Properties.Set failed: Property %s in interface %s doesn't exist or is read-only",
                       prop_name, prop_iface);
        
          bg_dbus_send_error(p->conn, msg, NULL, error_msg);
          free(error_msg);
          }
        }
      else
        {
        error_msg =
          bg_sprintf("org.freedesktop.DBus.Properties.Get failed: No such interface %s", prop_iface);
        
        bg_dbus_send_error(p->conn, msg, NULL, error_msg);
        free(error_msg);
        }
      send_ack(p, msg);
      }
    else if(!strcmp(member, "GetAll"))
      {
      if(!strcmp(prop_iface, "org.mpris.MediaPlayer2"))
        bg_dbus_properties_get(root_properties, &p->root_prop, msg, p->conn);
      else if(!strcmp(prop_iface, "org.mpris.MediaPlayer2.Player"))
        bg_dbus_properties_get(player_properties, &p->player_prop, msg, p->conn);
      else if(!strcmp(prop_iface, "org.mpris.MediaPlayer2.TrackList"))
        bg_dbus_properties_get(tracklist_properties, &p->tracklist_prop, msg, p->conn);
      else if(!strcmp(prop_iface, "org.freedesktop.DBus.Properties"))
        bg_dbus_properties_get(NULL, NULL, msg, p->conn);
      else
        {
        error_msg =
          bg_sprintf("org.freedesktop.DBus.Properties.GetAll failed: No such interface %s", prop_iface);
        
        bg_dbus_send_error(p->conn, msg, NULL, error_msg);
        free(error_msg);
        }

      }
    else
      {
      error_msg =
        bg_sprintf("No such function %s in interface %s", member, interface);
      bg_dbus_send_error(p->conn, msg, NULL, error_msg);
      free(error_msg);
      }
    }
  else if(!strcmp(interface, "org.mpris.MediaPlayer2"))
    {
    if(!strcmp(member, "Raise"))
      {
      send_ack(p, msg);
      }
    else if(!strcmp(member, "Quit"))
      {
      send_ack(p, msg);
      }
    else
      {
      error_msg =
        bg_sprintf("No such function %s in interface %s", member, interface);
      bg_dbus_send_error(p->conn, msg, NULL, error_msg);
      free(error_msg);
      }

    }
  else if(!strcmp(interface, "org.mpris.MediaPlayer2.Player"))
    {
    if(!strcmp(member, "Next"))
      {
      bg_player_next(p->control.cmd_sink);
      send_ack(p, msg);
      }
    else if(!strcmp(member, "Previous"))
      {
      bg_player_prev(p->control.cmd_sink);
      send_ack(p, msg);
      }
    else if(!strcmp(member, "Pause"))
      {
      const char * var;
      if((var = gavl_dictionary_get_string(&p->player_prop, "PlaybackStatus")) &&
         !strcmp(var, "Playing"))
        bg_player_pause(p->control.cmd_sink);
      send_ack(p, msg);
      }
    else if(!strcmp(member, "PlayPause"))
      {
      bg_player_pause(p->control.cmd_sink);
      send_ack(p, msg);
      }
    else if(!strcmp(member, "Stop"))
      {
      bg_player_stop(p->control.cmd_sink);
      send_ack(p, msg);
      }
    else if(!strcmp(member, "Play"))
      {
      bg_player_play(p->control.cmd_sink);
      send_ack(p, msg);
      }
    else if(!strcmp(member, "Seek"))
      {
      gavl_time_t t;
      t = gavl_msg_get_arg_long(msg, 0);
      
      fprintf(stderr, "Seek %"PRId64"\n", t);
      send_ack(p, msg);
      
      //      bg_player_seek(fe->ctrl.cmd_sink, gavl_time_t time, GAVL_TIME_SCALE);
      }
    else if(!strcmp(member, "SetPosition"))
      {
      //      const char * str;
      gavl_time_t t;
      //      str = gavl_msg_get_arg_string(msg, 0);
      t   = gavl_msg_get_arg_long(msg, 1);
      
      //      fprintf(stderr, "SetPosition %s %"PRId64"\n", str, t);
      bg_player_seek(p->control.cmd_sink, t, GAVL_TIME_SCALE);
      send_ack(p, msg);
      }
    else if(!strcmp(member, "OpenUri"))
      {
      /* TODO */
      send_ack(p, msg);
      }
    else
      {
      error_msg =
        bg_sprintf("No such function %s in interface %s", member, interface);
      bg_dbus_send_error(p->conn, msg, NULL, error_msg);
      free(error_msg);
      }
    }
  else if(!strcmp(interface, "org.mpris.MediaPlayer2.TrackList"))
    {
    if(!strcmp(member, "GetTracksMetadata"))
      {
      const char * str;
      int i;
      int idx;
      gavl_array_t ids;
      const gavl_array_t * track_ids;
      
      /* */

      DBusMessage * reply;
      DBusMessageIter iter;
      DBusMessageIter subiter;
      
      gavl_array_init(&ids);

      reply = bg_dbus_msg_new_reply(msg);
      dbus_message_iter_init_append(reply, &iter);

      dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "a{sv}", &subiter);
      
      /* */
      
      track_ids = gavl_dictionary_get_array(&p->tracklist_prop, "Tracks");
      
      gavl_msg_get_arg_array(msg, 0, &ids);
      
      /* (ao: TrackIds) -> aa{sv}: Metadata */
      for(i = 0; i < ids.num_entries; i++)
        {
        if((str = gavl_string_array_get(&ids, i)) &&
           ((idx = gavl_string_array_indexof(track_ids, str)) >= 0) &&
           (p->tracks.entries[idx].type == GAVL_TYPE_DICTIONARY))
          {
          val_to_variant_metadata(&p->tracks.entries[idx], &subiter);
          }
        }
      
      dbus_message_iter_close_container(&iter, &subiter);
      
      bg_dbus_send_msg(p->conn, reply);
      dbus_message_unref(reply);
      }
    else if(!strcmp(member, "AddTrack"))
      {
      /* (s: Uri, o: AfterTrack, b: SetAsCurrent) */
      send_ack(p, msg);
      
      }
    else if(!strcmp(member, "RemoveTrack"))
      {
      /* (o: TrackId) */
      send_ack(p, msg);
      
      }
    else if(!strcmp(member, "GoTo"))
      {
      /* (o: TrackId) */
      send_ack(p, msg);
      
      }
    else
      {
      error_msg =
        bg_sprintf("No such function %s in interface %s", member, interface);
      bg_dbus_send_error(p->conn, msg, NULL, error_msg);
      free(error_msg);
      }
    
    }
  else
    {
    error_msg =
      bg_sprintf("Unhandled interface %s", interface);
    bg_dbus_send_error(p->conn, msg, NULL, error_msg);
    free(error_msg);
    }
  
  return 1;
  }

/* Create mpris metadata from gmerlin track */

static void track_to_metadata(const gavl_dictionary_t * src, gavl_dictionary_t * dst)
  {
  const char * var = NULL;
  const gavl_value_t * val = NULL;

  
  if((var = bg_track_get_current_location(src)))
    gavl_dictionary_set_string_nocopy(dst, "xesam:url", bg_string_to_uri(var, -1));

  //  fprintf(stderr, "mpris2: Got uri %s\n", var);
  
  if(!(src = gavl_track_get_metadata(src)))
    return;
  
  if((var = gavl_dictionary_get_string(src, GAVL_META_TITLE)))
    gavl_dictionary_set_string(dst, "xesam:title", var);

  if((val = gavl_dictionary_get(src, GAVL_META_GENRE)))
    gavl_dictionary_set(dst, "xesam:genre", val);

  if((val = gavl_dictionary_get(src, GAVL_META_APPROX_DURATION)))
    gavl_dictionary_set(dst, "mpris:length", val);
    
  if((val = gavl_dictionary_get(src, GAVL_META_ALBUMARTIST)))
    gavl_dictionary_set(dst, "xesam:albumArtist", val);

  if((val = gavl_dictionary_get(src, GAVL_META_ARTIST)))
    gavl_dictionary_set(dst, "xesam:artist", val);

  if((var = gavl_dictionary_get_string(src, GAVL_META_ALBUM)))
    gavl_dictionary_set_string(dst, "xesam:album", var);

  if(gavl_metadata_get_src(src, GAVL_META_COVER_URL, 0, NULL, &var))
    gavl_dictionary_set_string_nocopy(dst, "mpris:artUrl", bg_string_to_uri(var, -1));
  }

/* Handle message from player */

static void add_track(void * priv, const gavl_dictionary_t * track, int idx)
  {
  char * mpris_id;
  gavl_value_t metadata_val;
  gavl_dictionary_t * metadata;
  const char * before = NULL;
  
  gavl_array_t * track_ids;
  
  DBusMessageIter iter;
  DBusMessage * msg;
  mpris2_t * p = priv;

  track_ids = gavl_dictionary_get_array_nc(&p->tracklist_prop, "Tracks");
  
  /* Create ID */
  mpris_id = bg_sprintf("/net/sourceforge/gmerlin/player/tracklist/%d", ++p->track_counter);
    
  /* Add to metadata array */
  gavl_value_init(&metadata_val);
  metadata = gavl_value_set_dictionary(&metadata_val);
  track_to_metadata(track, metadata);

  gavl_dictionary_set_string(metadata, "mpris:trackid", mpris_id);
  
  gavl_array_splice_val(&p->tracks, idx, 0, &metadata_val);
  gavl_string_array_insert_at(track_ids, idx, mpris_id);
  
  
  /* Send event */
  
  msg = dbus_message_new_signal(OBJ_PATH, "org.mpris.MediaPlayer2.TrackList", "TrackAdded");
  
  dbus_message_iter_init_append(msg, &iter);

  if(idx > 0)
    before = gavl_string_array_get(track_ids, idx-1);
  
  if(!before)
    before = "/org/mpris/MediaPlayer2/TrackList/NoTrack";
  
  val_to_variant_metadata(&metadata_val, &iter);

  dbus_message_iter_append_basic(&iter, DBUS_TYPE_OBJECT_PATH, &before);

  //  fprintf(stderr, "Track added:\n");
  //  bg_dbus_msg_dump(msg);
  
  bg_dbus_send_msg(p->conn, msg);
  dbus_message_unref(msg);
  }


static int handle_player_message_mpris(void * priv, gavl_msg_t * msg)
  {
  mpris2_t * p = priv;

  //  fprintf(stderr, "handle_player_message_mpris\n");
  
  switch(msg->NS)
    {
    case BG_MSG_NS_STATE:
      switch(msg->ID)
        {
        case BG_MSG_STATE_CHANGED:
          {
          int last;
          const char * ctx;
          const char * var;
          gavl_value_t val;
          gavl_value_init(&val);

          gavl_msg_get_state(msg, &last, &ctx, &var, &val, &p->state);

          //          fprintf(stderr, "Get state: %s %s\n", ctx, var);
          
          if(!p->dbus_sink)
            {
            gavl_value_free(&val);
            break;
            }
          if(!strcmp(ctx, BG_PLAYER_STATE_CTX))
            {
            if(!strcmp(var, BG_PLAYER_STATE_VOLUME))
              {
              bg_dbus_set_property_local(p->conn, OBJ_PATH,
                                         "org.mpris.MediaPlayer2.Player",
                                         "Volume", &val, player_properties,
                                         &p->player_prop, 1);
              
              }
            else if(!strcmp(var, BG_PLAYER_STATE_STATUS))
              {
              int status;
              if(gavl_value_get_int(&val, &status))
                {
                gavl_value_t prop_val;
                gavl_value_init(&prop_val);
                
                switch(status)
                  {
                  case BG_PLAYER_STATUS_PLAYING:     //!< Playing
                    gavl_value_set_string(&prop_val, "Playing");
                    break;
                  case BG_PLAYER_STATUS_SEEKING:     //!< Seeking
                  case BG_PLAYER_STATUS_INTERRUPTED: //!< Playback interrupted (due to parameter- or stream change)
                  case BG_PLAYER_STATUS_PAUSED:      //!< Paused
                    gavl_value_set_string(&prop_val, "Paused");
                    break;
                  default: 
                    gavl_value_set_string(&prop_val, "Stopped");
                  }
                bg_dbus_set_property_local(p->conn, OBJ_PATH,
                                           "org.mpris.MediaPlayer2.Player",
                                           "PlaybackStatus", &prop_val, player_properties,
                                           &p->player_prop, 1);
                }
              }
            else if(!strcmp(var, BG_PLAYER_STATE_CURRENT_TRACK))
              {
              const gavl_dictionary_t * dict;
              
              const gavl_value_t * var;

              gavl_value_t v;
              gavl_dictionary_t * d;

              gavl_value_init(&v);
              d = gavl_value_set_dictionary(&v);
              
              if((dict = gavl_value_get_dictionary(&val)))
                track_to_metadata(dict, d);
              
              //              fprintf(stderr, "Got track:\n");
              //              gavl_dictionary_dump(d, 2);
              //              fprintf(stderr, "\n");
              
              /* Send event */
              bg_dbus_set_property_local(p->conn, OBJ_PATH,
                                         "org.mpris.MediaPlayer2.Player",
                                         "Metadata", &v, player_properties,
                                         &p->player_prop, 1);
              gavl_value_free(&v);

              /* Handle CanPause and CanSeek */
              if((var = gavl_dictionary_get(dict, GAVL_META_CAN_PAUSE)))
                bg_dbus_set_property_local(p->conn, OBJ_PATH,
                                           "org.mpris.MediaPlayer2.Player",
                                           "CanPause", var, player_properties,
                                           &p->player_prop, 1);

              if((var = gavl_dictionary_get(dict, GAVL_META_CAN_SEEK)))
                bg_dbus_set_property_local(p->conn, OBJ_PATH,
                                           "org.mpris.MediaPlayer2.Player",
                                           "CanSeek", var, player_properties,
                                           &p->player_prop, 1);
              

              }
            else if(!strcmp(var, BG_PLAYER_STATE_TIME))
              {
              gavl_time_t t;
              
              if((gavl_value_get_long(&val, &t)) &&
                 (t != GAVL_TIME_UNDEFINED))
                {
                gavl_value_t v;
                gavl_value_init(&v);
                gavl_value_set_long(&v, t);
                bg_dbus_set_property_local(p->conn, OBJ_PATH,
                                           "org.mpris.MediaPlayer2.Player",
                                           "Position", &v, player_properties,
                                           &p->player_prop, 0);
                }
              }
            else if(!strcmp(var, BG_PLAYER_STATE_MODE))
              {
              int mode;
              gavl_value_t v;
              
              if(gavl_value_get_int(&val, &mode))
                {
                gavl_value_init(&v);
                gavl_value_set_int(&v, mode_to_shuffle(mode));
                bg_dbus_set_property_local(p->conn, OBJ_PATH,
                                           "org.mpris.MediaPlayer2.Player",
                                           "Shuffle", &v, player_properties,
                                           &p->player_prop, 1);

                gavl_value_reset(&v);

                gavl_value_set_string(&v, mode_to_loop_status(mode));
                bg_dbus_set_property_local(p->conn, OBJ_PATH,
                                           "org.mpris.MediaPlayer2.Player",
                                           "LoopStatus", &v, player_properties,
                                           &p->player_prop, 1);
                gavl_value_free(&v);
                
                }
              }
            else if(!strcmp(var, BG_PLAYER_STATE_MUTE))
              {
              int mute;
              
              if(gavl_value_get_int(&val, &mute))
                {
                }
              }
            }
          
          /* Send events */
          //          bg_upnp_event_context_server_set_value(gavl_dictionary_t * dict, const char * name,
          //                                                 const char * val,
          //                                                 gavl_time_t update_interval)          
            
          gavl_value_free(&val);
          }
          break;
        }
      break;
      
    case BG_MSG_NS_DB:

      switch(msg->ID)
        {
        case BG_MSG_DB_SPLICE_CHILDREN:
        case BG_RESP_DB_BROWSE_CHILDREN:
#if 1
          {
          gavl_array_t * track_ids;
          
          /* Update tracks */
          int idx;
          int del;
          gavl_value_t add;
          int last;
          const char * id;
          int i;
          
          gavl_value_init(&add);
          gavl_msg_get_splice_children(msg, &last, &idx, &del, &add);

          track_ids = gavl_dictionary_get_array_create(&p->tracklist_prop, "Tracks");

          if(del)
            {
            for(i = 0; i < del; i++)
              {
              DBusMessageIter iter;
              DBusMessage * msg;
              
              msg = dbus_message_new_signal(OBJ_PATH, "org.mpris.MediaPlayer2.TrackList", "TrackRemoved");
              
              dbus_message_iter_init_append(msg, &iter);

              id = gavl_string_array_get(track_ids, idx + i);
              dbus_message_iter_append_basic(&iter, DBUS_TYPE_OBJECT_PATH, &id);
              bg_dbus_send_msg(p->conn, msg);
              dbus_message_unref(msg);
              }
            
            gavl_array_splice_val(track_ids, idx, del, NULL);
            gavl_array_splice_val(&p->tracks, idx, del, NULL);
            }
          
          /* Add tracks */
          if(add.type == GAVL_TYPE_DICTIONARY)
            {
            add_track(priv, gavl_value_get_dictionary(&add), idx);
            }
          if(add.type == GAVL_TYPE_ARRAY)
            {
            int i;
            int added = 0;
            const gavl_dictionary_t * dict;
            const gavl_array_t * arr = gavl_value_get_array(&add);
            
            for(i = 0; i < arr->num_entries; i++)
              {
              if((dict = gavl_value_get_dictionary(&arr->entries[i])))
                {
                add_track(priv, dict, idx + added);
                added++;
                }
              }
            }

          /* Send property change event for the tracks */

          /*
            void bg_dbus_set_property_local(bg_dbus_connection_t * conn,
                                            const char * path,
                                            const char * interface,
                                            const char * name,
                                            const gavl_value_t * val,
                                            const bg_dbus_property_t * properties,
                                            gavl_dictionary_t * dict,
                                            int send_msg);
            */

          // track_ids = gavl_dictionary_get_array(&p->tracklist_prop, "Tracks");
          
          bg_dbus_set_property_local(p->conn,
                                     OBJ_PATH,
                                     "org.mpris.MediaPlayer2.TrackList",
                                     "Tracks",
                                     gavl_dictionary_get(&p->tracklist_prop, "Tracks"),
                                     tracklist_properties,
                                     NULL,
                                     1);
          }
#else
          break;
#endif
        }
      
      break;
    }
  
  return 1;
  }

static int ping_mpris2(void * data)
  {
  int ret = 0;
  mpris2_t * priv = data;
  
  bg_msg_sink_iteration(priv->dbus_sink);
  ret += bg_msg_sink_get_num(priv->dbus_sink);
  
  bg_msg_sink_iteration(priv->control.evt_sink);
  ret += bg_msg_sink_get_num(priv->control.evt_sink);
  
  return ret;
  }

static void destroy_mpris2(void * data)
  {
  mpris2_t * priv = data;
  
  if(priv->conn)
    bg_dbus_unregister_object(priv->conn, OBJ_PATH, priv->dbus_sink);
  
  if(priv->dbus_sink)
    bg_msg_sink_destroy(priv->dbus_sink);

  gavl_dictionary_free(&priv->root_prop);
  gavl_dictionary_free(&priv->player_prop);
  gavl_dictionary_free(&priv->tracklist_prop);
  gavl_dictionary_free(&priv->state);

  if(priv->bus_name)
    free(priv->bus_name);
  
  free(priv);
  }


static void * create_mpris2()
  {
  gavl_array_t * arr;
  mpris2_t * priv;
  const char * name;
  priv = calloc(1, sizeof(*priv));
  
  if(!(priv->conn = bg_dbus_connection_get(DBUS_BUS_SESSION)))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Could not connect to session bus"); 
    free(priv);
    return NULL;
    }
  
  name = bg_app_get_name();
  priv->bus_name = gavl_sprintf("org.mpris.MediaPlayer2.%s", name);
  
  
  bg_dbus_properties_init(root_properties, &priv->root_prop);
  bg_dbus_properties_init(player_properties, &priv->player_prop);
  bg_dbus_properties_init(tracklist_properties, &priv->tracklist_prop);

  gavl_dictionary_get_array_create(&priv->tracklist_prop, "Tracks");
  
  gavl_dictionary_set_string(&priv->root_prop, "DesktopEntry", name);

  /* Protocols */
  arr = gavl_dictionary_get_array_create(&priv->root_prop, "SupportedUriSchemes");
  
  gavl_string_array_add(arr, "file");
  gavl_string_array_add(arr, "http");

  /* mimetypes */

  arr = gavl_dictionary_get_array_create(&priv->root_prop, "SupportedMimeTypes");
  gavl_array_copy(arr, bg_plugin_registry_get_input_mimetypes());
  
  //  free(id);
  //  free(bus_name);

  return priv;
  }

static int open_mpris2(void * data, bg_controllable_t * ctrl)
  {
  const char * label;
  gavl_dictionary_t local_dev;
  char * bus_name_real;
  mpris2_t * priv = data;

  label = bg_app_get_label();
  
  gavl_dictionary_init(&local_dev);
    
  gavl_dictionary_set_string(&priv->root_prop, "Identity", label);
  
  priv->dbus_sink = bg_msg_sink_create(handle_msg_mpris2, priv, 0);

  bg_control_init(&priv->control, bg_msg_sink_create(handle_player_message_mpris, priv, 0));
  bg_controllable_connect(ctrl, &priv->control);
  
#if 0    
  fprintf(stderr, "Creating dbus object: %s\n", server_label);
  gavl_dictionary_dump(&priv->root_prop, 2);
  fprintf(stderr, "State\n");
  gavl_dictionary_dump(&priv->state, 2);
    
  fprintf(stderr, "registering object\n");
#endif
    
  /* Register object */
  bg_dbus_register_object(priv->conn, OBJ_PATH,
                          priv->dbus_sink, introspect_xml);
    
    
  /* Request name: From now on we are visible */

  bg_dbus_connection_lock(priv->conn);
    
  bus_name_real = bg_dbus_connection_request_name(priv->conn, priv->bus_name);
  
  /* TODO: Register local device. Must be done before releasing the lock */

  gavl_dictionary_set_string_nocopy(&local_dev, GAVL_META_URI,
                                    bg_sprintf("%s://%s", BG_DBUS_MPRIS_URI_SCHEME,
                                               bus_name_real + MPRIS2_NAME_PREFIX_LEN));

  gavl_dictionary_set_string(&local_dev, GAVL_META_CLASS, GAVL_META_CLASS_BACKEND_RENDERER);
  gavl_dictionary_set_string(&local_dev, GAVL_META_LABEL, label);


  bg_resourcemanager_publish(gavl_dictionary_get_string(&local_dev, GAVL_META_URI), &local_dev);
    
  gavl_dictionary_free(&local_dev);
    
  bg_dbus_connection_unlock(priv->conn);
    

  //    fprintf(stderr, "registering object done\n");

    
  //  fprintf(stderr, "Got bus name: %s\n", bus_name_real);
  if(bus_name_real)
    free(bus_name_real);

  
  return 1;
  }

bg_frontend_plugin_t the_plugin =
  {
    .common =
    {
      BG_LOCALE,
      .name =      "fe_mpris",
      .long_name = TRS("Mpris2 frontend"),
      .description = TRS("Makes gmerlin controllable via Mpris2"),
      .type =     BG_PLUGIN_FRONTEND_RENDERER,
      .flags =    0,
      .create =   create_mpris2,
      .destroy =   destroy_mpris2,
      .priority =         1,
    },
    .update = ping_mpris2,
    .open = open_mpris2,
  };

/* Include this into all plugin modules exactly once
   to let the plugin loader obtain the API version */
BG_GET_PLUGIN_API_VERSION;


static const char * introspect_xml =
"<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\" \"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">"
"<node>"
"  <interface name=\"org.freedesktop.DBus.Introspectable\">"
"    <method name=\"Introspect\">"
"      <arg name=\"data\" direction=\"out\" type=\"s\"/>"
"    </method>"
"  </interface>"
"  <interface name=\"org.freedesktop.DBus.Properties\">"
"    <method name=\"Get\">"
"      <arg direction=\"in\" type=\"s\"/>"
"      <arg direction=\"in\" type=\"s\"/>"
"      <arg direction=\"out\" type=\"v\"/>"
"    </method>"
"    <method name=\"Set\">"
"      <arg direction=\"in\" type=\"s\"/>"
"      <arg direction=\"in\" type=\"s\"/>"
"      <arg direction=\"in\" type=\"v\"/>"
"    </method>"
"    <method name=\"GetAll\">"
"      <arg direction=\"in\" type=\"s\"/>"
"      <arg direction=\"out\" type=\"a{sv}\"/>"
"    </method>"
"    <signal name=\"PropertiesChanged\">"
"      <arg type=\"s\"/>"
"      <arg type=\"a{sv}\"/>"
"      <arg type=\"as\"/>"
"    </signal>"
"  </interface>"
"  <interface name=\"org.mpris.MediaPlayer2\">"
"    <property name=\"Identity\" type=\"s\" access=\"read\" />"
"    <property name=\"DesktopEntry\" type=\"s\" access=\"read\" />"
"    <property name=\"SupportedMimeTypes\" type=\"as\" access=\"read\" />"
"    <property name=\"SupportedUriSchemes\" type=\"as\" access=\"read\" />"
"    <property name=\"HasTrackList\" type=\"b\" access=\"read\" />"
"    <property name=\"CanQuit\" type=\"b\" access=\"read\" />"
"    <property name=\"CanSetFullscreen\" type=\"b\" access=\"read\" />"
"    <property name=\"Fullscreen\" type=\"b\" access=\"readwrite\" />"
"    <property name=\"CanRaise\" type=\"b\" access=\"read\" />"
"    <method name=\"Quit\" />"
"    <method name=\"Raise\" />"
"  </interface>"
"  <interface name=\"org.mpris.MediaPlayer2.Player\">"
"    <property name=\"Metadata\" type=\"a{sv}\" access=\"read\" />"
"    <property name=\"PlaybackStatus\" type=\"s\" access=\"read\" />"
"    <property name=\"LoopStatus\" type=\"s\" access=\"readwrite\" />"
"    <property name=\"Volume\" type=\"d\" access=\"readwrite\" />"
"    <property name=\"Shuffle\" type=\"d\" access=\"readwrite\" />"
"    <property name=\"Position\" type=\"i\" access=\"read\" />"
"    <property name=\"Rate\" type=\"d\" access=\"readwrite\" />"
"    <property name=\"MinimumRate\" type=\"d\" access=\"readwrite\" />"
"    <property name=\"MaximumRate\" type=\"d\" access=\"readwrite\" />"
"    <property name=\"CanControl\" type=\"b\" access=\"read\" />"
"    <property name=\"CanPlay\" type=\"b\" access=\"read\" />"
"    <property name=\"CanPause\" type=\"b\" access=\"read\" />"
"    <property name=\"CanSeek\" type=\"b\" access=\"read\" />"
"    <method name=\"Previous\" />"
"    <method name=\"Next\" />"
"    <method name=\"Stop\" />"
"    <method name=\"Play\" />"
"    <method name=\"Pause\" />"
"    <method name=\"PlayPause\" />"
"    <method name=\"Seek\">"
"      <arg type=\"x\" direction=\"in\" />"
"    </method>    <method name=\"OpenUri\">"
"      <arg type=\"s\" direction=\"in\" />"
"    </method>"
"    <method name=\"SetPosition\">"
"      <arg type=\"o\" direction=\"in\" />"
"      <arg type=\"x\" direction=\"in\" />"
"    </method>"
"  </interface>"
"  <interface name=\"org.mpris.MediaPlayer2.TrackList\">"
"    <property name=\"Tracks\" type=\"ao\" access=\"read\" />"
"    <property name=\"CanEditTracks\" type=\"b\" access=\"read\" />"
"    <method name=\"GetTracksMetadata\">"
"      <arg type=\"ao\" direction=\"in\" />"
"      <arg type=\"aa{sv}\" direction=\"out\" />"
"    </method>"
"    <method name=\"AddTrack\">"
"      <arg type=\"s\" direction=\"in\" />"
"      <arg type=\"o\" direction=\"in\" />"
"      <arg type=\"b\" direction=\"in\" />"
"    </method>"
"    <method name=\"RemoveTrack\">"
"      <arg type=\"o\" direction=\"in\" />"
"    </method>"
"    <method name=\"GoTo\">"
"      <arg type=\"o\" direction=\"in\" />"
"    </method>"
"    <signal name=\"TrackListReplaced\">"
"      <arg type=\"ao\" />"
"      <arg type=\"o\" />"
"    </signal>"
"    <signal name=\"TrackAdded\">"
"      <arg type=\"a{sv}\" />"
"      <arg type=\"o\" />"
"    </signal>"
"    <signal name=\"TrackRemoved\">"
"      <arg type=\"o\" />"
"    </signal>"
"    <signal name=\"TrackMetadataChanged\">"
"      <arg type=\"o\" />"
"      <arg type=\"a{sv}\" />"
"    </signal>"
"  </interface>"
"</node>";
