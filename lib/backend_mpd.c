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

#include <gmerlin/translation.h>
#include <gmerlin/log.h>
#define LOG_DOMAIN "mpd"

#include <backend_priv.h>

#define STATUS_REPEAT          "repeat"
#define STATUS_RANDOM          "random"
#define STATUS_SINGLE          "single"
#define STATUS_SONG_IDX        "song"
#define STATUS_SONG_ID         "songid"
#define STATUS_STATE           "state"
#define STATUS_TIME            "elapsed"
#define STATUS_DURATION        "duration"
#define STATUS_PLAYLIST_ID     "playlist"
#define STATUS_PLAYLIST_LENGTH "playlistlength"
#define STATUS_VOLUME          "volume"

#define FLAG_MODE_CHANGED  (1<<0)
#define FLAG_TRACK_CHANGED (1<<1)

#define FLAG_OUR_PLAYBACK  (1<<2)


static const struct
  {
  const char * name;
  gavl_type_t type;
  }
status_vars[] =
  {
    { STATUS_REPEAT,          GAVL_TYPE_INT    },
    { STATUS_RANDOM,          GAVL_TYPE_INT    },
    { STATUS_SINGLE,          GAVL_TYPE_INT    },
    { STATUS_SONG_IDX,        GAVL_TYPE_INT    },
    { STATUS_SONG_ID,         GAVL_TYPE_INT    },
    { STATUS_STATE,           GAVL_TYPE_STRING },
    { STATUS_TIME,            GAVL_TYPE_FLOAT  },
    { STATUS_DURATION,        GAVL_TYPE_FLOAT  },
    { STATUS_PLAYLIST_ID,     GAVL_TYPE_INT    },
    { STATUS_PLAYLIST_LENGTH, GAVL_TYPE_INT    },
    { STATUS_VOLUME,          GAVL_TYPE_INT    },
    { /* End */                                }
  };

typedef struct
  {
  gavf_io_t * io;

  int line_alloc;
  char * line;

  int got_error;
  gavl_dictionary_t gmerlin_state;
  gavl_dictionary_t mpd_state;

  gavl_timer_t * timer;
  gavl_time_t last_poll_time;

  bg_player_tracklist_t tl;

  bg_msg_sink_t * tl_sink;
  
  int our_playback;

  gavl_array_t idmap;

  int flags;
  int current_mpd_id;

  gavl_time_t duration;
  gavl_time_t current_time;
  
  } mpd_t;

#if 0
static void add_logo(gavl_dictionary_t * dev)
  {
  
  }

#endif


static void store_ids(mpd_t * m, const char * gmerlin, int mpd, int idx)
  {
  gavl_value_t val;
  gavl_dictionary_t * dict;

  gavl_value_init(&val);
  dict = gavl_value_set_dictionary(&val);
  gavl_dictionary_set_string(dict, "g", gmerlin);
  gavl_dictionary_set_int(dict, "m", mpd);

  gavl_array_splice_val_nocopy(&m->idmap, idx, 0, &val);
  }

static int id_gmerlin_to_mpd(mpd_t * m, const char * gmerlin) 
  {
  const char * gmerlin_test;

  const gavl_dictionary_t * dict;
  int i;
  for(i = 0; i < m->idmap.num_entries; i++)
    {
    if(!(dict = gavl_value_get_dictionary(&m->idmap.entries[i])))
      continue;

    if((gmerlin_test = gavl_dictionary_get_string(dict, "g")) &&
       !strcmp(gmerlin_test, gmerlin))
      {
      int ret = -1;
      gavl_dictionary_get_int(dict, "m", &ret);
      return ret;
      }
    }
  return -1;
  }

static const char * id_mpd_to_gmerlin(mpd_t * m, int mpd)
  {
  const gavl_dictionary_t * dict;
  int mpd_test = -1;
  
  int i;
  for(i = 0; i < m->idmap.num_entries; i++)
    {
    if(!(dict = gavl_value_get_dictionary(&m->idmap.entries[i])))
      continue;
    
    if(gavl_dictionary_get_int(dict, "m", &mpd_test) &&
       (mpd_test == mpd))
      return gavl_dictionary_get_string(dict, "g");
    }
  return NULL;
  }

static char * response_line(mpd_t * m)
  {
  if(!gavf_io_read_line(m->io, &m->line, &m->line_alloc, 1024))
    return NULL;

  if(!strcmp(m->line, "OK"))
    {
    m->got_error = 0;
    return NULL;
    }
  else if(gavl_string_starts_with(m->line, "ACK "))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "MPD error: %s", m->line + 4);
    m->got_error = 1;
    return NULL;
    }
  else
    return m->line;
  }

/* Command *must* include the trailing "\n" */
static int send_command(bg_backend_handle_t * dev, const char * cmd, int flush_reply)
  {
  char * res;
  mpd_t * priv = dev->priv;

  if(strcmp(cmd, "status\n"))
    {
    gavl_log(GAVL_LOG_DEBUG, LOG_DOMAIN, "Sending command: %s", cmd);
    }
  
  if(!gavf_io_write_data(priv->io, (const uint8_t*)cmd, strlen(cmd)))
    return 0;

  if(flush_reply)
    {
    while((res = response_line(priv)))
      {
      gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Ignoring response line %s", res);
      }
    }

  return 1;
  }

static void update_foreign_track(bg_backend_handle_t * dev)
  {
  int idx = -1;
  gavl_value_t val;
  char * cmd;
  gavl_dictionary_t * dict;
  gavl_dictionary_t * metadata;
  mpd_t * m = dev->priv;
  char * line;
  double duration = -1.0;

  const char * title;
  
  if(!gavl_dictionary_get_int(&m->mpd_state, STATUS_SONG_IDX, &idx) ||
     (idx < 0))
    return;

  gavl_value_init(&val);
  dict = gavl_value_set_dictionary(&val);
  metadata = gavl_dictionary_get_dictionary_create(dict, GAVL_META_METADATA);
    
  cmd = bg_sprintf("playlistinfo %d\n", idx);
  send_command(dev, cmd, 0);

#define SET_STRING(mpd_var, gavl_var) \
  if(gavl_string_starts_with(line, mpd_var ": ")) \
    {                                             \
    gavl_dictionary_append_string_array(metadata, gavl_var, line + strlen(mpd_var ": "));\
    continue; \
    }

  while((line = response_line(m)))
    {
    SET_STRING("Artist", GAVL_META_ARTIST);
    SET_STRING("Album", GAVL_META_ALBUM);
    SET_STRING("AlbumArtist", GAVL_META_ALBUMARTIST);
    SET_STRING("Title", GAVL_META_TITLE);
    SET_STRING("Genre", GAVL_META_GENRE);

    if(gavl_string_starts_with(line, "Date: "))
      gavl_dictionary_set_date(metadata, GAVL_META_DATE, atoi(line + 6), 99, 99);
    
    }

#undef SET_STRING
  
  /* Set Label */
  if((title = gavl_dictionary_get_string(metadata, GAVL_META_TITLE)) &&
     (line = gavl_metadata_join_arr(metadata, GAVL_META_ARTIST, ", ")))
    {
    gavl_dictionary_set_string_nocopy(metadata, GAVL_META_LABEL,
                                      bg_sprintf("%s - %s", line, title));
    free(line);
    }

  /* Set duration */

  if(gavl_dictionary_get_float(&m->mpd_state, STATUS_DURATION, &duration) && (duration > 0.0))
    {
    gavl_dictionary_set_long(metadata, GAVL_META_APPROX_DURATION, gavl_seconds_to_time(duration));
    
    gavl_dictionary_set_int(metadata, GAVL_META_CAN_PAUSE, 1);
    gavl_dictionary_set_int(metadata, GAVL_META_CAN_SEEK, 1);
    }
  
  bg_state_set(&m->gmerlin_state, 1, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_CURRENT_TRACK,
               &val, dev->ctrl.evt_sink, BG_MSG_STATE_CHANGED);
  gavl_value_free(&val);
  
  }

static int get_mpd_status(bg_backend_handle_t * dev, gavl_dictionary_t * dict)
  {
  int idx;
  char * line;
  char * val;
  
  mpd_t * priv = dev->priv;

  send_command(dev, "status\n", 0);

  while((line = response_line(priv)))
    {
    if((val = strstr(line, ": ")))
      {
      *val = '\0';
      val += 2;
      }

    if(val)
      {
      idx = 0;

      while(status_vars[idx].name)
        {
        if(!strcmp(status_vars[idx].name, line))
          {
          switch(status_vars[idx].type)
            {
            case GAVL_TYPE_INT:
              gavl_dictionary_set_int(dict, status_vars[idx].name, atoi(val));
              break;
            case GAVL_TYPE_FLOAT:
              gavl_dictionary_set_float(dict, status_vars[idx].name, strtod(val, NULL));
              break;
            case GAVL_TYPE_STRING:
              gavl_dictionary_set_string(dict, status_vars[idx].name, val);
              break;
            default:
              break;
            }
          break;
          }
        
        idx++;
        }
      }
    }
  
  if(priv->got_error)
    return 0;
  else
    return 1;
  }

static int get_gmerlin_status(const gavl_value_t * val)
  {
  const char * str;
  
  if(!val || !(str = gavl_value_get_string(val)))
    return BG_PLAYER_STATUS_INIT;

  if(!strcmp(str, "play"))
    return BG_PLAYER_STATUS_PLAYING;
  
  if(!strcmp(str, "stop"))
    return BG_PLAYER_STATUS_STOPPED;
  if(!strcmp(str, "pause"))
    return BG_PLAYER_STATUS_PAUSED;

  return BG_PLAYER_STATUS_STOPPED;
  }

static void set_time(bg_backend_handle_t * dev, gavl_time_t t)
  {
  mpd_t * m;
  gavl_value_t val;
  gavl_dictionary_t * dict;
  
  m = dev->priv;

  m->current_time = t;
  
  gavl_value_init(&val);
  dict = gavl_value_set_dictionary(&val);
  
  if(m->flags & FLAG_OUR_PLAYBACK)
    {
    gavl_time_t t_abs;
    gavl_time_t t_rem;
    gavl_time_t t_rem_abs;
    double percentage;
    
    bg_player_tracklist_get_times(&m->tl, m->current_time, &t_abs, &t_rem, &t_rem_abs, &percentage);
    
    gavl_dictionary_set_long(dict, BG_PLAYER_TIME, m->current_time);
    gavl_dictionary_set_long(dict, BG_PLAYER_TIME_ABS, t_abs);
    gavl_dictionary_set_long(dict, BG_PLAYER_TIME_REM, t_rem);
    gavl_dictionary_set_long(dict, BG_PLAYER_TIME_REM_ABS, t_rem_abs);
    gavl_dictionary_set_float(dict, BG_PLAYER_TIME_PERC, percentage);
    }
  else
    {
    gavl_dictionary_set_long(dict, BG_PLAYER_TIME, m->current_time);
    gavl_dictionary_set_long(dict, BG_PLAYER_TIME_ABS, m->current_time);
    
    gavl_dictionary_set_long(dict, BG_PLAYER_TIME_REM, m->duration - m->current_time);
    gavl_dictionary_set_long(dict, BG_PLAYER_TIME_REM_ABS, m->duration - m->current_time);
    gavl_dictionary_set_float(dict, BG_PLAYER_TIME_PERC, (double)m->current_time / (double)m->duration);
    }
  
  bg_state_set(&m->gmerlin_state, 1, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_CURRENT_TIME,
               &val, dev->ctrl.evt_sink, BG_MSG_STATE_CHANGED);

  gavl_value_free(&val);
  
  }

static void update_mode(bg_backend_handle_t * dev)
  {
  gavl_value_t val;
  mpd_t * m;
  int gmerlin_mode = BG_PLAYER_MODE_NORMAL;
  int single = 0, repeat = 0, random = 0;
  
  m = dev->priv;

  gavl_value_init(&val);
  
  if(!gavl_dictionary_get_int(&m->mpd_state, STATUS_RANDOM, &random) ||
     !gavl_dictionary_get_int(&m->mpd_state, STATUS_REPEAT, &repeat) ||
     !gavl_dictionary_get_int(&m->mpd_state, STATUS_SINGLE, &single))
    gmerlin_mode = BG_PLAYER_MODE_NORMAL;

  else
    {
    if(single)
      {
      if(repeat)
        gmerlin_mode = BG_PLAYER_MODE_LOOP;
      else
        gmerlin_mode = BG_PLAYER_MODE_ONE;
      }
    else if(random)
      gmerlin_mode = BG_PLAYER_MODE_SHUFFLE;
    else if(repeat)
      gmerlin_mode = BG_PLAYER_MODE_REPEAT;
    else
      gmerlin_mode = BG_PLAYER_MODE_NORMAL;
    }
  
  gavl_value_set_int(&val, gmerlin_mode);
  
  bg_state_set(&m->gmerlin_state, 1, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_MODE,
               &val, dev->ctrl.evt_sink, BG_MSG_STATE_CHANGED);

  gavl_value_free(&val);

  m->flags &= ~FLAG_MODE_CHANGED;
  
  }

static void update_current_track(bg_backend_handle_t * dev)
  {
  mpd_t * m;

  gavl_value_t val;

  const gavl_value_t * val_c;
  int status = -1;
    
  m = dev->priv;

  gavl_value_init(&val);
          
  if(!(val_c = bg_state_get(&m->gmerlin_state,
                            BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_STATUS)) ||
     !gavl_value_get_int(val_c, &status))
    return;
  
  if(status == BG_PLAYER_STATUS_STOPPED)
    {
    gavl_dictionary_t * track;
    
    track = gavl_value_set_dictionary(&val);
    
    gavl_dictionary_get_dictionary_create(track, GAVL_META_METADATA);
    
    bg_state_set(&m->gmerlin_state, 0, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_CURRENT_TRACK,
                 &val, dev->ctrl.evt_sink, BG_MSG_STATE_CHANGED);
    
    gavl_value_free(&val);
    return;
    }
  
  if(m->flags & FLAG_OUR_PLAYBACK)
    {
    int mpd_id = -1;
    const char * gmerlin_id;
    const gavl_dictionary_t * dict;
    
    if(!gavl_dictionary_get_int(&m->mpd_state, STATUS_SONG_ID, &mpd_id) ||
       (mpd_id < 0))
      {
      
      }
    
    gmerlin_id = id_mpd_to_gmerlin(m, mpd_id);
    
    if(gmerlin_id) // Track from us
      {
      
      bg_player_tracklist_set_current_by_id(&m->tl, gmerlin_id);
          
      dict = bg_player_tracklist_get_current_track(&m->tl);

      if(dict)
        {
        gavl_time_t duration = GAVL_TIME_UNDEFINED;
        gavl_dictionary_t * metadata;

        gavl_dictionary_copy(gavl_value_set_dictionary(&val), dict);
            
        /* Set can_pause and can_seek */
        metadata = gavl_value_get_dictionary_nc(&val);
        metadata = gavl_track_get_metadata_nc(metadata); 

        /* If the track has a duration we assume we can seek and pause */
        if(gavl_dictionary_get_long(metadata, GAVL_META_APPROX_DURATION, &duration) &&
           (duration > 0))
          {
          gavl_dictionary_set_int(metadata, GAVL_META_CAN_PAUSE, 1);
          gavl_dictionary_set_int(metadata, GAVL_META_CAN_SEEK, 1);
          }
            
        bg_state_set(&m->gmerlin_state, 1, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_CURRENT_TRACK,
                     &val, dev->ctrl.evt_sink, BG_MSG_STATE_CHANGED);

        }
      }
    else
      m->flags &= ~FLAG_OUR_PLAYBACK;
    }
  
  if(!(m->flags & FLAG_OUR_PLAYBACK))
    update_foreign_track(dev);
  
  m->flags &= ~FLAG_TRACK_CHANGED;
  
  }

static void update_status_variable(bg_backend_handle_t * dev, const char * name,
                                   const gavl_value_t * var_old, const gavl_value_t * var_new)
  {
  mpd_t * m;

  gavl_value_t val;
  
  m = dev->priv;

  gavl_value_init(&val);
  
  if(!strcmp(name, STATUS_REPEAT))
    {
    m->flags |= FLAG_MODE_CHANGED;
    }
  else if(!strcmp(name, STATUS_RANDOM))
    {
    m->flags |= FLAG_MODE_CHANGED;
    }
  else if(!strcmp(name, STATUS_SINGLE))
    {
    m->flags |= FLAG_MODE_CHANGED;
    }
  else if(!strcmp(name, STATUS_SONG_IDX))
    {

    
    }
  else if(!strcmp(name, STATUS_SONG_ID))
    {
    int id_new;

    if(var_new)
      gavl_value_get_int(var_new, &id_new);
    else
      id_new = -1;
    
    if(m->current_mpd_id != id_new)
      {
      m->current_mpd_id = id_new;
      
      if(id_new >= 0)
        {
        m->flags |= FLAG_TRACK_CHANGED;
        }
      }
    }
  else if(!strcmp(name, STATUS_STATE))
    {
    //    int old_status = get_gmerlin_status(var_old);
    int new_status = get_gmerlin_status(var_new);
    
    if(new_status == BG_PLAYER_STATUS_STOPPED)
      {
      gavl_dictionary_t * track;
      /* Sut down playback */
      set_time(dev, 0);

      track = gavl_value_set_dictionary(&val);
      
      gavl_dictionary_get_dictionary_create(track, GAVL_META_METADATA);

      bg_state_set(&m->gmerlin_state, 0, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_CURRENT_TRACK,
                   &val, dev->ctrl.evt_sink, BG_MSG_STATE_CHANGED);
      
      gavl_value_reset(&val);
      }

    gavl_value_set_int(&val, new_status);

    bg_state_set(&m->gmerlin_state, 1, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_STATUS,
                 &val, dev->ctrl.evt_sink, BG_MSG_STATE_CHANGED);
    }
  else if(!strcmp(name, STATUS_VOLUME))
    {
    int val_i;

    if(var_new && gavl_value_get_int(var_new, &val_i) && (val_i >= 0) && (val_i <= 100))
      {
      gavl_value_set_float(&val, (float)val_i / 100.0);
      bg_state_set(&m->gmerlin_state, 1, BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_VOLUME,
                   &val, dev->ctrl.evt_sink, BG_MSG_STATE_CHANGED);
      }
    }
  else if(!strcmp(name, STATUS_TIME))
    {
    gavl_time_t t;
    double val_f;
    
    if(var_new && gavl_value_get_float(var_new, &val_f) && (val_f >= 0.0))
      {
      t = gavl_seconds_to_time(val_f);
      
      if(t / GAVL_TIME_SCALE != m->current_time / GAVL_TIME_SCALE)
        set_time(dev, t);
      }
      
    }
  else if(!strcmp(name, STATUS_DURATION))
    {
    double val_f;
    
    if(var_new && gavl_value_get_float(var_new, &val_f) && (val_f > 0.0))
      m->duration = gavl_seconds_to_time(val_f);
    else
      m->duration = GAVL_TIME_UNDEFINED;
    
    if(m->duration != GAVL_TIME_UNDEFINED)
      bg_state_set_range_long(&m->gmerlin_state,
                              BG_PLAYER_STATE_CTX "/" BG_PLAYER_STATE_CURRENT_TIME, BG_PLAYER_TIME,
                              0, m->duration);
    else
      bg_state_set_range_long(&m->gmerlin_state,
                              BG_PLAYER_STATE_CTX "/" BG_PLAYER_STATE_CURRENT_TIME, BG_PLAYER_TIME,
                              0, 0);
    }
  else if(!strcmp(name, STATUS_PLAYLIST_ID))
    {
    }
  else if(!strcmp(name, STATUS_PLAYLIST_LENGTH))
    {
    }
  
  gavl_value_free(&val);
  
  }

static void check_status_variable(bg_backend_handle_t * dev, gavl_dictionary_t * dict, const char * name)
  {
  const gavl_value_t * var_old;
  const gavl_value_t * var_new;
  mpd_t * m = dev->priv;

  var_new = gavl_dictionary_get(dict, name);
  var_old = gavl_dictionary_get(&m->mpd_state, name);
  
  if(gavl_value_compare(var_old, var_new))
    {
    update_status_variable(dev, name, var_old, var_new);
    gavl_dictionary_set(&m->mpd_state, name, var_new);
    }
  
  }

static void update_mpd_status(bg_backend_handle_t * dev, gavl_dictionary_t * dict)
  {
  int idx = 0;
  mpd_t * m = dev->priv;

  while(status_vars[idx].name)
    {
    check_status_variable(dev, dict, status_vars[idx].name);
    idx++;
    }

  if(m->flags & FLAG_MODE_CHANGED)
    {
    update_mode(dev);
    }

  if(m->flags & FLAG_TRACK_CHANGED)
    {
    update_current_track(dev);
    }
  
  }

static void init_mpd_status(bg_backend_handle_t * dev)
  {
  int idx = 0;
  const gavl_value_t * var_new;

  mpd_t * m = dev->priv;
  
  while(status_vars[idx].name)
    {
    if((var_new = gavl_dictionary_get(&m->mpd_state, status_vars[idx].name)))
      update_status_variable(dev, status_vars[idx].name, NULL, var_new);
    idx++;
    }
  update_current_track(dev);
  }

#if 0
static void set_node_info(bg_backend_handle_t * dev, const gavl_dictionary_t * info)
  {
  gavl_array_t icons;
  const char * label;

  gavl_value_t val;
  gavl_dictionary_t * dict;
  
  label = gavl_dictionary_get_string(&dev->dev, GAVL_META_LABEL);

  
  
  bg_set_network_node_info(gavl_dictionary_get_string(&dev->dev, GAVL_META_LABEL),
                           gavl_dictionary_get_string(&dev->dev, GAVL_META_ICON_URL)
                           &icons, NULL, dev->ctrl.evt_sink);
  }
#endif


static void add_tag_id(bg_backend_handle_t * dev,
                       const gavl_dictionary_t * dict,
                       int song_id,
                       const char * mpd_tag, const char * gmerlin_tag)
  {
  char * cmd;

  int idx = 0;
  const char * str;
  const gavl_value_t * val;
  
  while((val = gavl_dictionary_get_item(dict, gmerlin_tag, idx)) &&
        (str = gavl_value_get_string(val)))
    {
    cmd = bg_sprintf("addtagid %d \"%s\" \"%s\"\n", song_id, mpd_tag, str);
    send_command(dev, cmd, 1);
    free(cmd);
    idx++;
    }
  }

static int add_file(bg_backend_handle_t * dev, const gavl_value_t * val, int idx)
  {
  const char * uri_c;
  char * uri;
  char * cmd;
  mpd_t * m;
  char * str;
  int mpd_id = -1;
  int year;
  const char * id;
  //  gavl_time_t duration = GAVL_TIME_UNDEFINED;
  const gavl_dictionary_t * dict;
  const gavl_dictionary_t * metadata;
  
  m = dev->priv;

  /* Clear playlist */
  
  if(!(m->flags & FLAG_OUR_PLAYBACK))
    {
    send_command(dev, "clear\n", 1);
    m->flags |= FLAG_OUR_PLAYBACK;
    }
  
  if(!(dict = gavl_value_get_dictionary(val)) ||
     !(uri_c = bg_player_track_get_uri(&m->gmerlin_state, dict)) ||
     !(id = gavl_track_get_id(dict)))
    return 0;
  
  uri = gavl_escape_string(gavl_strdup(uri_c), "\"");

  cmd = bg_sprintf("addid \"%s\" %d\n", uri, idx);
  send_command(dev, cmd, 0);
  free(cmd);
  
  while((str = response_line(m)))
    {
    if(gavl_string_starts_with(str, "Id: "))
      mpd_id = atoi(str + 4);
    }

  if(m->got_error || mpd_id < 0)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Lading %s failed", uri);
    return 0;
    }
    
  store_ids(m, id, mpd_id, idx);

  metadata = gavl_track_get_metadata(dict);
  
  add_tag_id(dev, metadata, mpd_id, "artist", GAVL_META_ARTIST);
  add_tag_id(dev, metadata, mpd_id, "album", GAVL_META_ALBUM);
  add_tag_id(dev, metadata, mpd_id, "albumartist", GAVL_META_ALBUMARTIST);
  add_tag_id(dev, metadata, mpd_id, "title", GAVL_META_TITLE);
  add_tag_id(dev, metadata, mpd_id, "genre", GAVL_META_GENRE);

  if((year = bg_metadata_get_year(metadata)) > 0)
    {
    cmd = bg_sprintf("addtagid %d \"%s\" \"%d\"\n", mpd_id, "date", year);
    send_command(dev, cmd, 1);
    free(cmd);
    }

#if 0  
  if(gavl_dictionary_get_long(metadata, GAVL_META_APPROX_DURATION, &duration) &&
     (duration > 0))
    {
    cmd = bg_sprintf("addtagid %d \"%s\" \"%f\"\n", mpd_id, "duration", gavl_time_to_seconds(duration));
    send_command(dev, cmd, 1);
    free(cmd);
    }
#endif
  
  free(uri);

  if(mpd_id < 0)
    return 0;
  else
    return 1;
  }

static int handle_tl_msg(void * priv, gavl_msg_t * msg)
  {
  mpd_t * m;
  bg_backend_handle_t * dev = priv;
  
  m = dev->priv;
  
  if((msg->NS == BG_MSG_NS_DB) &&
     (msg->ID == BG_MSG_DB_SPLICE_CHILDREN))
    {
    int idx, del;
    gavl_value_t val;
    
    gavl_value_init(&val);
          
    idx = gavl_msg_get_arg_int(msg, 0);
    del = gavl_msg_get_arg_int(msg, 1);
    gavl_msg_get_arg(msg, 2, &val);

    /* Delete tracks */
    if(del > 0)
      {
      char * cmd = bg_sprintf("delete %d:%d\n", idx, idx + del);
      send_command(dev, cmd, 1);
      free(cmd);
      gavl_array_splice_val(&m->idmap, idx, del, NULL);
      }
    
    switch(val.type)
      {
      case GAVL_TYPE_DICTIONARY:
        add_file(dev, &val, idx);
        break;
      case GAVL_TYPE_ARRAY:
        {
        int i;
        const gavl_array_t * arr =
          gavl_value_get_array(&val);
        
        for(i = 0; i < arr->num_entries; i++)
          {
          add_file(dev, &arr->entries[i], idx + i);
          }
        }
        break;
      default:
        break;
      }
    
    }
  return 1;
  }

static void add_logo(gavl_dictionary_t * dev)
  {
  gavl_array_t * arr;
  gavl_value_t val;
  gavl_dictionary_t * dict;

  if(gavl_dictionary_get(dev, GAVL_META_ICON_URL))
    return;
  
  gavl_value_init(&val);
  
  dict = gavl_value_set_dictionary(&val);
  
  gavl_dictionary_set_string(dict, GAVL_META_URI, "https://www.musicpd.org/logo.png");
  gavl_dictionary_set_string(dict, GAVL_META_MIMETYPE, "image/png");

  //  gavl_dictionary_set(dev, GAVL_META_ICON_URL, NULL);
  arr = gavl_dictionary_get_array_create(dev, GAVL_META_ICON_URL);
  gavl_array_splice_val_nocopy(arr, 0, 0, &val);
  }

static int create_mpd(bg_backend_handle_t * dev, const char * uri, const char * root_url)
  {
  int ret = 0;
  int port;
  char * host = NULL;
  gavl_socket_address_t * addr = gavl_socket_address_create();
  int fd;
  char * line;
  mpd_t * priv;
  
  gavl_array_t mimetypes;
  gavl_array_t protocols;

  const char * label;
  char * label_priv = NULL;
  
  priv = calloc(1, sizeof(*priv));
  dev->priv = priv;

  priv->timer = gavl_timer_create();
  gavl_timer_start(priv->timer);
    
  gavl_array_init(&mimetypes);
  gavl_array_init(&protocols);

  bg_player_tracklist_init(&priv->tl, dev->ctrl.evt_sink);

  if(!bg_url_split(uri, NULL, NULL, NULL, &host, &port, NULL))
    goto fail;

  if(!gavl_socket_address_set(addr, host, port, SOCK_STREAM))
    goto fail;

  if((fd = gavl_socket_connect_inet(addr, 2000)) < 0)
    goto fail;
  
  priv->io = gavf_io_create_socket(fd, 2000, GAVF_IO_SOCKET_DO_CLOSE);
  
  if(!gavf_io_read_line(priv->io, &priv->line, &priv->line_alloc, 1024))
    goto fail;
  
  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Got Server line: %s", priv->line);

  /* Get mimetypes */
  
  send_command(dev, "decoders\n", 0);

  while((line = response_line(priv)))
    {
    if(gavl_string_starts_with(line, "mime_type: "))
      {
      gavl_string_array_add(&mimetypes, line + 11);
      }
    
    }

  gavl_string_array_add(&protocols, "http");

  label = gavl_dictionary_get_string(&dev->dev, GAVL_META_LABEL);

  if(!label)
    {
    label_priv = bg_sprintf("mpd@%s", host);
    label = label_priv;
    }
  
  bg_player_state_init(&priv->gmerlin_state,
                       label,
                       &protocols, &mimetypes);

  priv->tl.application_state = &priv->gmerlin_state;
  
  if(!get_mpd_status(dev, &priv->mpd_state))
    goto fail;

  priv->last_poll_time = gavl_timer_get(priv->timer);
  
  init_mpd_status(dev);
  update_mode(dev);
  
  bg_state_apply(&priv->gmerlin_state, dev->ctrl.evt_sink, BG_MSG_STATE_CHANGED);

  add_logo(&dev->dev);
  
  bg_set_network_node_info(label,
                           gavl_dictionary_get_array(&dev->dev, GAVL_META_ICON_URL),
                           NULL, dev->ctrl.evt_sink);
  
  ret = 1;

  priv->tl_sink = bg_msg_sink_create(handle_tl_msg, dev, 1);
  bg_msg_hub_connect_sink(priv->tl.hub, priv->tl_sink);

  
  
  fail:

  gavl_array_free(&mimetypes);
  gavl_array_free(&protocols);
  
  if(host)
    free(host);

  if(label_priv)
    free(label_priv);
  
  if(addr)
    gavl_socket_address_destroy(addr);
  
  return ret;
  }

static void destroy_mpd(bg_backend_handle_t * dev)
  {
  mpd_t * priv = dev->priv;

  if(!priv)
    return;
  
  if(priv->io)
    gavf_io_destroy(priv->io);

  if(priv->timer)
    gavl_timer_destroy(priv->timer);

  if(priv->line)
    free(priv->line);

  
  gavl_dictionary_free(&priv->gmerlin_state);
  gavl_dictionary_free(&priv->mpd_state);

  bg_player_tracklist_free(&priv->tl);

  if(priv->tl_sink)
    bg_msg_sink_destroy(priv->tl_sink);
  
  free(priv);
  }

static int ping_mpd(bg_backend_handle_t * be)
  {
  int ret = 0;
  gavl_time_t cur;
  
  mpd_t * priv = be->priv;

  cur = gavl_timer_get(priv->timer);

  if(cur - priv->last_poll_time > GAVL_TIME_SCALE)
    {
    gavl_dictionary_t state;

    ret++;
    gavl_dictionary_init(&state);
    get_mpd_status(be, &state);
    /* TODO: Compare with priv->mdb_state  */
    update_mpd_status(be, &state);
    
    gavl_dictionary_free(&state);
    
    priv->last_poll_time = cur;
    }

  return ret;
  }

static void do_seek(bg_backend_handle_t * be, gavl_time_t t)
  {
  char * cmd;
  
  cmd = bg_sprintf("seekcur %f\n", gavl_time_to_seconds(t));
  send_command(be, cmd, 1);
  free(cmd);
  
  }

static void do_play(bg_backend_handle_t * be)
  {
  int mpd_id = -1;
  char * cmd;
  const gavl_dictionary_t * dict = NULL;
  const char * id = NULL;
  mpd_t * m = be->priv;
  
  if(!(dict = bg_player_tracklist_get_current_track(&m->tl)) ||
     !(id = gavl_track_get_id(dict)) ||
     ((mpd_id = id_gmerlin_to_mpd(m, id)) < 0))
    {
    return;
    }
  
  cmd = bg_sprintf("playid %d\n", mpd_id);
  send_command(be, cmd, 1);
  free(cmd);
  }


static void set_volume(bg_backend_handle_t * be, float val_f)
  {
  char * cmd;
  int val_i;
  
  val_i = (val_f * 100.0 + 0.5);

  if(val_i < 0)
    val_i = 0;
  if(val_i > 100)
    val_i = 100;
              
  cmd = bg_sprintf("setvol %d\n", val_i);
  send_command(be, cmd, 1);
  free(cmd);
  
  }

static int handle_msg_mpd(void * priv, // Must be bg_backend_handle_t
                          gavl_msg_t * msg)
  {
  bg_backend_handle_t * be = priv;
  mpd_t * m = be->priv;

  if(bg_player_tracklist_handle_message(&m->tl, msg))
    {
#if 0
    if(m->tl.list_changed || m->tl.current_changed)
      r->flags &= ~RENDERER_HAS_NEXT;
    
    if(r->tl.list_changed)
      r->tl.list_changed = 0;

    if(r->tl.current_changed)
      {
      r->tl.current_changed = 0;

      if((r->flags & RENDERER_OUR_PLAYBACK) &&
         ((r->player_status == BG_PLAYER_STATUS_PLAYING) ||
          (r->player_status == BG_PLAYER_STATUS_SEEKING) ||
          (r->player_status == BG_PLAYER_STATUS_PAUSED)))
        do_stop(be);
      }
#endif
    return 1;
    }

  
  switch(msg->NS)
    {
    case BG_MSG_NS_STATE:

      switch(msg->ID)
        {
        case BG_CMD_SET_STATE:
          {
          gavl_value_t val;
          const char * ctx;
          const char * var;
          
          int last = 0;
          
          gavl_value_init(&val);

          bg_msg_get_state(msg, &last, &ctx, &var, &val, NULL);

          if(!strcmp(ctx, BG_PLAYER_STATE_CTX"/"BG_PLAYER_STATE_CURRENT_TIME))          // dictionary
            {
            
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
                t = (int64_t)(perc * m->duration + 0.5);
                do_seek(be, t);
                }
              }
            }
          else if(!strcmp(ctx, BG_PLAYER_STATE_CTX))
            {
            if(!strcmp(var, BG_PLAYER_STATE_VOLUME))     // float
              {
              double val_f = 0.0;
              
              if(!gavl_value_get_float(&val, &val_f))
                break;
              set_volume(be, val_f);
              }
            else if(!strcmp(var, BG_PLAYER_STATE_MUTE))          // int
              {
              int val_i;
              
              if(!gavl_value_get_int(&val, &val_i))
                break;

              val_i &= 1;
              gavl_value_set_int(&val, val_i);
              
              if(!val_i) /* No mute means normal volume */
                {
                const gavl_value_t * volume_val;
                double volume_f;
                if(!(volume_val = bg_state_get(&m->gmerlin_state,
                                               BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_VOLUME)) ||
                   !gavl_value_get_float(volume_val, &volume_f))
                  break;
                set_volume(be, volume_f);
                }
              else
                {
                send_command(be, "setvol 0\n", 1);
                }
              
              }
            else if(!strcmp(var, BG_PLAYER_STATE_MODE))          // int
              {
              int val_i;
              if(!gavl_value_get_int(&val, &val_i))
                break;

              while(val_i >= BG_PLAYER_MODE_MAX)
                val_i -= BG_PLAYER_MODE_MAX;
              while(val_i < 0)
                val_i += BG_PLAYER_MODE_MAX;
              
              gavl_value_set_int(&val, val_i);
              
              switch(val_i)
                {
                case BG_PLAYER_MODE_NORMAL: //!< Normal playback
                  {
                  send_command(be, "random 0\n", 1);
                  send_command(be, "repeat 0\n", 1);
                  send_command(be, "single 0\n", 1);
                  }
                  break;
                case BG_PLAYER_MODE_REPEAT: //!< Repeat current album
                  {
                  send_command(be, "random 0\n", 1);
                  send_command(be, "repeat 1\n", 1);
                  send_command(be, "single 0\n", 1);
                  }
                  break;
                case BG_PLAYER_MODE_SHUFFLE: //!< Shuffle (implies repeat)
                  {
                  send_command(be, "random 1\n", 1);
                  send_command(be, "repeat 1\n", 1);
                  send_command(be, "single 0\n", 1);
                  }
                  break;
                case BG_PLAYER_MODE_ONE: //!< Play one track and stop
                  {
                  send_command(be, "random 0\n", 1);
                  send_command(be, "repeat 0\n", 1);
                  send_command(be, "single 1\n", 1);
                  }
                  break;
                case BG_PLAYER_MODE_LOOP: //!< Loop current track
                  {
                  send_command(be, "random 0\n", 1);
                  send_command(be, "repeat 1\n", 1);
                  send_command(be, "single 1\n", 1);
                  }
                  break;
                }
              
              }
            else
              {
#if 0
              gavl_dprintf("BG_CMD_SET_STATE %s %s ", ctx, var);
              gavl_value_dump(&val, 2);
              gavl_dprintf("\n");
#endif
              }
            }
          else
            {
#if 0
            gavl_dprintf("BG_CMD_SET_STATE %s %s ", ctx, var);
            gavl_value_dump(&val, 2);
            gavl_dprintf("\n");
#endif
            }
          
          bg_state_set(&m->gmerlin_state, last, ctx, var,
                       &val, be->ctrl.evt_sink, BG_MSG_STATE_CHANGED);
          
          gavl_value_free(&val);
          
          }
          break;
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

          bg_state_add_value(&m->gmerlin_state, ctx, var, &add, &val);
          
          gavl_msg_init(&cmd);
          bg_msg_set_state(&cmd, BG_CMD_SET_STATE, last, ctx, var, &val);
          handle_msg_mpd(priv, &cmd);
          gavl_msg_free(&cmd);
          
          gavl_value_free(&val);
          gavl_value_free(&add);
          
          //          gavl_dprintf("BG_CMD_SET_STATE_REL");
          //          gavl_msg_dump(msg, 2);

          }

          break;
        }
      
      break;
    case BG_MSG_NS_PLAYER:

      switch(msg->ID)
        {
        case BG_PLAYER_CMD_STOP:
          send_command(be, "stop\n", 1);
          break;
        case BG_PLAYER_CMD_PLAY:
          {
          const gavl_value_t * val;
          int val_i = -1;
          
          if(!(val = bg_state_get(&m->gmerlin_state,
                                 BG_PLAYER_STATE_CTX, BG_PLAYER_STATE_STATUS)) ||
             !gavl_value_get_int(val, &val_i))
            break;
          
          switch(val_i)
            {
            case BG_PLAYER_STATUS_PAUSED:
              send_command(be, "pause 0\n", 1);
              break;
            case BG_PLAYER_STATUS_PLAYING:
              break;
            case BG_PLAYER_STATUS_STOPPED:
              do_play(be);
              break;
            }
          
          }

          break;
        case BG_PLAYER_CMD_NEXT:
          send_command(be, "next\n", 1);
          break;
        case BG_PLAYER_CMD_PREV:
          send_command(be, "previous\n", 1);
          break;
        case BG_PLAYER_CMD_SET_CURRENT_TRACK:
          {
          const char * id = gavl_msg_get_arg_string_c(msg, 0);
          bg_player_tracklist_set_current_by_id(&m->tl, id);
          break;
          }
        case BG_PLAYER_CMD_PLAY_BY_ID:
          {
          const char * id = gavl_msg_get_arg_string_c(msg, 0);
          bg_player_tracklist_set_current_by_id(&m->tl, id);

          
          do_play(be);
          }
          break;
        case BG_PLAYER_CMD_PAUSE:
          send_command(be, "pause\n", 1);
          break;
        case BG_PLAYER_CMD_SET_TRACK:
          break;
        case BG_PLAYER_CMD_SET_LOCATION:
          break;
        }
      
      break;
    case BG_MSG_NS_DB:

      switch(msg->ID)
        {
        
        }
      
      break;
    case GAVL_MSG_NS_GENERIC:

      switch(msg->ID)
        {
        case GAVL_CMD_QUIT:
          return 0;
          break;
        }
      
    }
  
  return 1;
  }

const bg_remote_dev_backend_t bg_remote_dev_backend_mpd_player =
  {
    .name = "mpd player",
    .uri_prefix = BG_MPD_URI_SCHEME"://",
    .type = BG_BACKEND_RENDERER,
    
    .handle_msg = handle_msg_mpd,
    .ping    = ping_mpd,
    .create    = create_mpd,
    .destroy   = destroy_mpd,
  };
