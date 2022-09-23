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

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <uuid/uuid.h>

#include <gavl/gavl.h>
#include <gavl/metatags.h>
#include <gavl/http.h>
#include <gmerlin/parameter.h>
#include <gmerlin/streaminfo.h>
#include <gmerlin/utils.h>
#include <gmerlin/iconfont.h>

#include <gmerlin/pluginregistry.h>

void bg_set_track_name_default(gavl_dictionary_t * info,
                               const char * location)
  {
  char * name;
  const char * start_pos;
  const char * end_pos;
  const char * var;
  gavl_dictionary_t * m = gavl_track_get_metadata_nc(info);
  
  if(gavl_dictionary_get_string(m, GAVL_META_LABEL))
    return;

  if((var = gavl_dictionary_get_string(m, GAVL_META_STATION)))
    name = gavl_strdup(var);
  else if(bg_string_is_url(location))
    name = gavl_strdup(location);
  else
    {
    start_pos = strrchr(location, '/');
    if(start_pos)
      start_pos++;
    else
      start_pos = location;
    end_pos = strrchr(start_pos, '.');
    if(!end_pos)
      end_pos = &start_pos[strlen(start_pos)];
    name = gavl_strndup( start_pos, end_pos);
    }
  gavl_dictionary_set_string_nocopy(m, GAVL_META_LABEL, name);
  }

char * bg_get_track_name_default(const char * location)
  {
  const char * start_pos;
  const char * end_pos;
  char * tmp_string, *ret;
  gavl_dictionary_t url_vars;
  int track = 0;
  
  if(bg_string_is_url(location))
    {
    tmp_string = gavl_strdup(location);
    }
  else
    {
    start_pos = strrchr(location, '/');
    if(start_pos)
      start_pos++;
    else
      start_pos = location;
    end_pos = strrchr(start_pos, '.');
    if(!end_pos)
      end_pos = &start_pos[strlen(start_pos)];
    tmp_string = bg_system_to_utf8(start_pos, end_pos - start_pos);
    }

  gavl_dictionary_init(&url_vars);
  gavl_url_get_vars_c(location, &url_vars);
  
  if(gavl_dictionary_get_int(&url_vars, BG_URL_VAR_TRACK, &track))
    {
    ret = bg_sprintf("%s [%d]", tmp_string, track);
    free(tmp_string);
    return ret;
    }
  else
    return tmp_string;
  }

static const struct
  {
  const char * klass;
  const char * icon;
  }
icons[] =
  {
/* Value for class */
    { GAVL_META_MEDIA_CLASS_AUDIO_FILE,             BG_ICON_MUSIC     },
    { GAVL_META_MEDIA_CLASS_VIDEO_FILE,             BG_ICON_FILM      },
    { GAVL_META_MEDIA_CLASS_AUDIO_PODCAST_EPISODE,  BG_ICON_MUSIC     },
    { GAVL_META_MEDIA_CLASS_VIDEO_PODCAST_EPISODE,  BG_ICON_FILM      },

    { GAVL_META_MEDIA_CLASS_SONG,               BG_ICON_MUSIC         },
    { GAVL_META_MEDIA_CLASS_MOVIE,              BG_ICON_FILM          },
    { GAVL_META_MEDIA_CLASS_MOVIE_PART,         BG_ICON_FILM          },
    { GAVL_META_MEDIA_CLASS_MOVIE_MULTIPART,    BG_ICON_FILM          },
    { GAVL_META_MEDIA_CLASS_TV_EPISODE,         BG_ICON_TV            },
    { GAVL_META_MEDIA_CLASS_AUDIO_BROADCAST,    BG_ICON_RADIO },
    { GAVL_META_MEDIA_CLASS_VIDEO_BROADCAST,    BG_ICON_RADIO_STATION },
    //    { GAVL_META_MEDIA_CLASS_VIDEO_BROADCAST,  },
    { GAVL_META_MEDIA_CLASS_IMAGE,              BG_ICON_IMAGE,        },
    { GAVL_META_MEDIA_CLASS_FILE,               BG_ICON_FILE,         },
    { GAVL_META_MEDIA_CLASS_LOCATION,           BG_ICON_GLOBE,        },
    { GAVL_META_MEDIA_CLASS_AUDIO_DISK_TRACK,   BG_ICON_MUSIC         },
    { GAVL_META_MEDIA_CLASS_VIDEO_DISK_TRACK,   BG_ICON_FILM          },
    
/* Container values */
//    { GAVL_META_MEDIA_CLASS_CONTAINER,  },
    { GAVL_META_MEDIA_CLASS_MUSICALBUM,         BG_ICON_MUSIC_ALBUM },
    { GAVL_META_MEDIA_CLASS_PHOTOALBUM,         BG_ICON_PHOTO       },
    { GAVL_META_MEDIA_CLASS_PLAYLIST,           BG_ICON_PLAYLIST    },
    { GAVL_META_MEDIA_CLASS_CONTAINER_ACTOR,    BG_ICON_PERSON      }, 
    { GAVL_META_MEDIA_CLASS_CONTAINER_DIRECTOR, BG_ICON_MOVIE_MAKER }, 
    { GAVL_META_MEDIA_CLASS_CONTAINER_ARTIST,   BG_ICON_MICROPHONE  },
    { GAVL_META_MEDIA_CLASS_CONTAINER_COUNTRY,  BG_ICON_FLAG        },
    { GAVL_META_MEDIA_CLASS_CONTAINER_GENRE,    BG_ICON_MASKS       },
    { GAVL_META_MEDIA_CLASS_CONTAINER_LANGUAGE, BG_ICON_TALK        },
    { GAVL_META_MEDIA_CLASS_CONTAINER_TAG,      BG_ICON_TAG         },
    { GAVL_META_MEDIA_CLASS_CONTAINER_YEAR,     BG_ICON_CALENDAR    },
    { GAVL_META_MEDIA_CLASS_CONTAINER_RADIO,    BG_ICON_RADIO       },
    { GAVL_META_MEDIA_CLASS_CONTAINER_TV,       BG_ICON_TV          },

    { GAVL_META_MEDIA_CLASS_PODCAST,            BG_ICON_RSS   },

    { GAVL_META_MEDIA_CLASS_TV_SEASON,          BG_ICON_TV          },
    { GAVL_META_MEDIA_CLASS_TV_SHOW,            BG_ICON_TV          },
    { GAVL_META_MEDIA_CLASS_AUDIO_RECORDER,     BG_ICON_MICROPHONE  },
    { GAVL_META_MEDIA_CLASS_VIDEO_RECORDER,     BG_ICON_VIDEOCAMERA },
    //    { GAVL_META_MEDIA_CLASS_DIRECTORY },
    
/* Root Containers */
//    { GAVL_META_MEDIA_CLASS_ROOT  },
    { GAVL_META_MEDIA_CLASS_ROOT_PLAYQUEUE,         BG_ICON_PLAYER   }, 
    { GAVL_META_MEDIA_CLASS_ROOT_MUSICALBUMS,       BG_ICON_MUSIC_ALBUM   }, 
    { GAVL_META_MEDIA_CLASS_ROOT_SONGS,             BG_ICON_MUSIC         },
    { GAVL_META_MEDIA_CLASS_ROOT_MOVIES,            BG_ICON_FILM          },
    { GAVL_META_MEDIA_CLASS_ROOT_TV_SHOWS,          BG_ICON_TV            },
    { GAVL_META_MEDIA_CLASS_ROOT_STREAMS,           BG_ICON_NETWORK },
    { GAVL_META_MEDIA_CLASS_ROOT_PODCASTS,          BG_ICON_RSS   },

    { GAVL_META_MEDIA_CLASS_ROOT_FAVORITES,         BG_ICON_HEART         }, 
    { GAVL_META_MEDIA_CLASS_ROOT_BOOKMARKS,         BG_ICON_BOOKMARK      }, 
    { GAVL_META_MEDIA_CLASS_ROOT_LIBRARY,           BG_ICON_LIBRARY       },
    { GAVL_META_MEDIA_CLASS_ROOT_SERVER,            BG_ICON_SERVER      },
    
    { GAVL_META_MEDIA_CLASS_ROOT_DIRECTORIES,       BG_ICON_HDD           },
    { GAVL_META_MEDIA_CLASS_ROOT_PHOTOS,            BG_ICON_PHOTO         },

    { GAVL_META_MEDIA_CLASS_ROOT_RECORDERS,         BG_ICON_VIDEOCAMERA    },

    
    { GAVL_META_MEDIA_CLASS_ROOT_REMOVABLE,     BG_ICON_HDD           },
    { GAVL_META_MEDIA_CLASS_ROOT_REMOVABLE_AUDIOCD, BG_ICON_CDROM     },
    { GAVL_META_MEDIA_CLASS_ROOT_REMOVABLE_VCD, BG_ICON_CDROM     },
    { GAVL_META_MEDIA_CLASS_ROOT_REMOVABLE_SVCD,BG_ICON_CDROM     },
    { GAVL_META_MEDIA_CLASS_ROOT_REMOVABLE_VIDEODVD,BG_ICON_CDROM     },

    { GAVL_META_MEDIA_CLASS_ROOT_REMOVABLE_FILESYSTEM,     BG_ICON_HDD           },
    { GAVL_META_MEDIA_CLASS_ROOT_REMOVABLE_FILESYSTEM_HDD, BG_ICON_HDD           },
    { GAVL_META_MEDIA_CLASS_ROOT_REMOVABLE_FILESYSTEM_PENDRIVE, BG_ICON_PENDRIVE },
    { GAVL_META_MEDIA_CLASS_ROOT_REMOVABLE_FILESYSTEM_MEMORYCARD, BG_ICON_MEMORYCARD },
    { GAVL_META_MEDIA_CLASS_ROOT_REMOVABLE_FILESYSTEM_MOBILE, BG_ICON_MOBILE },
    { GAVL_META_MEDIA_CLASS_ROOT_REMOVABLE_FILESYSTEM_CD, BG_ICON_CDROM },
    { GAVL_META_MEDIA_CLASS_ROOT_REMOVABLE_FILESYSTEM_DVD, BG_ICON_CDROM },
    { /* End */ }
  };
  
const char * bg_get_type_icon(const char * media_class)
  {
  int i = 0;

  while(icons[i].klass)
    {
    if(!strcmp(media_class, icons[i].klass))
      return icons[i].icon;
    i++;
    }

  if(gavl_string_starts_with(media_class, "container"))
    return BG_ICON_FOLDER;
  
  return NULL;
  }

const char * bg_dictionary_get_id(gavl_dictionary_t * m)
  {
  uuid_t u;
  char * id;
  const char * val;
  if((val = gavl_dictionary_get_string(m, GAVL_META_ID)))
    return val;
  
  uuid_generate(u);
  id = calloc(37, 1);
  uuid_unparse(u, id);
  gavl_dictionary_set_string_nocopy(m, GAVL_META_ID, id);
  return gavl_dictionary_get_string(m, GAVL_META_ID);
  }

void bg_msg_set_splice_children(gavl_msg_t * msg, int msg_id, const char * album_id,
                                int last, int idx, int del, const gavl_value_t * add)
  {
  gavl_msg_set_splice_children(msg, BG_MSG_NS_DB, msg_id,
                               album_id,
                               last, idx, del, add);
  }


static int handle_delete_command(void * priv, gavl_msg_t * msg)
  {
  gavl_dictionary_t * dict = priv;
  if((msg->NS == BG_MSG_NS_DB) && (msg->ID == BG_CMD_DB_SPLICE_CHILDREN))
    gavl_msg_splice_children(msg, dict);
  return 1;
  }

void bg_dictionary_delete_children_nc(gavl_dictionary_t * container,
                                      bg_test_child_func func,
                                      void * data)
  {
  bg_msg_sink_t * sink = bg_msg_sink_create(handle_delete_command, container, 1);
  bg_dictionary_delete_children(container, func, data, sink, BG_CMD_DB_SPLICE_CHILDREN);
  bg_msg_sink_destroy(sink);
  }

void bg_dictionary_delete_children(const gavl_dictionary_t * container,
                                   bg_test_child_func func,
                                   void * data, bg_msg_sink_t * sink, int msg_id)
  {
  int num_deleted = 0;
  int i;
  
  uint8_t * masks;

  int num_tracks = gavl_get_num_tracks(container);

  if(!num_tracks)
    return;
  
  masks = malloc(num_tracks);
  
  for(i = 0; i < num_tracks; i++)
    {
    const gavl_dictionary_t * track = gavl_get_track(container, i);
    masks[i] = func(track, data);
    }

  i = 0;

  while(i < num_tracks)
    {
    if(masks[i])
      {
      const gavl_dictionary_t * m;
      int last = 1;
      int off = 1;
      int num_delete = 1;
      gavl_msg_t * msg;
      const char * id = NULL;
      
      while((i + num_delete < num_tracks) && masks[i + num_delete])
        num_delete++;
      
      off = num_delete;

      while(i + off < num_tracks)
        {
        if(masks[i + off])
          {
          last = 0;
          break;
          }
        off++;
        }

      if((m = gavl_track_get_metadata(container)))
        id = gavl_dictionary_get_string(m, GAVL_META_ID);
      
      msg = bg_msg_sink_get(sink);
      bg_msg_set_splice_children(msg, msg_id, id,
                                 last, i - num_deleted, num_delete, NULL);
      
      bg_msg_sink_put(sink, msg);

      i += num_delete;
      num_deleted += num_delete;
      }
    else
      i++;
    }
  
  free(masks);
  }

static int test_child_func(const gavl_dictionary_t * track, void * data)
  {
  return gavl_track_get_gui_state(track, data);
  }

void bg_dictionary_delete_children_by_flag_nc(gavl_dictionary_t * container,
                                              const char * flag)
  {
  char * flag1 = gavl_strdup(flag);
  bg_dictionary_delete_children_nc(container,
                                   test_child_func,
                                   flag1);
  free(flag1);
  }


void bg_dictionary_delete_children_by_flag(const gavl_dictionary_t * container,
                                           const char * flag,
                                           bg_msg_sink_t * sink, int msg_id)
  {
  char * flag1 = gavl_strdup(flag);
  bg_dictionary_delete_children(container, test_child_func, flag1, sink, msg_id);
  free(flag1);
  }

gavl_array_t * bg_dictionary_extract_children(const gavl_dictionary_t * container,
                                              bg_test_child_func func,
                                              void * data)
  {
  int i;
  gavl_array_t * ret = NULL;
  int num_tracks = gavl_get_num_tracks(container);
  
  if(!num_tracks)
    return NULL;

  for(i = 0; i < num_tracks; i++)
    {
    const gavl_dictionary_t * track = gavl_get_track(container, i);
    if(func(track, data))
      {
      gavl_value_t val;
      gavl_dictionary_t * d;
      
      if(!ret)
        ret = gavl_array_create();
      
      gavl_value_init(&val);
      d = gavl_value_set_dictionary(&val);
      gavl_dictionary_copy(d, track);
      gavl_array_splice_val_nocopy(ret, -1, 0, &val);
      }
    
    }
  return ret;
  }

gavl_array_t * bg_dictionary_extract_children_by_flag(const gavl_dictionary_t * container,
                                                      const char * flag)
  {
  gavl_array_t * ret;
  char * flag1 = gavl_strdup(flag);
  ret = bg_dictionary_extract_children(container, test_child_func, flag1);
  free(flag1);
  return ret;
  }



