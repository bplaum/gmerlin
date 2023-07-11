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


#include <uuid/uuid.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>

#include <gmerlin/player.h>
#include <playerprivate.h>

#include <md5.h>

#include <gmerlin/log.h>
#include <gmerlin/mdb.h>
#include <gmerlin/application.h>

#include <mdb_private.h>

#include <gavl/metatags.h>

#define LOG_DOMAIN "playertracks"

static int * create_shuffle_list(int num_entries)
  {
  int i;
  int idx;
  int swp;
  int * ret;

  ret = malloc(num_entries * sizeof(*ret));

  for(i = 0; i < num_entries; i++)
    ret[i] = i;

  if(num_entries > 1)
    {
    for(i = 0; i < num_entries; i++)
      {
      idx = rand() % (num_entries - 1);

      if(idx >= i)
        idx++;
    
      swp = ret[i];
      ret[i] = ret[idx];
      ret[idx] = swp;
      }

    }
  
  
  return ret;
  }

static void delete_shuffle_list(bg_player_tracklist_t * l)
  {
  if(l->shuffle_list)
    {
    free(l->shuffle_list);
    l->shuffle_list = NULL;
    }
  }

static int next_track(bg_player_tracklist_t * tl, int advance, int wrap)
  {
  int idx = tl->idx;
  int real_idx = 0;

  gavl_array_t * list = gavl_get_tracks_nc(tl->cnt);
  
  if(!list || !list->num_entries)
    return -1;

  /* If next track was set, we ignore our play mode */
  if(tl->has_next)
    {
    real_idx = tl->idx_real + 1;
    
    if(advance)
      {
      tl->idx      = real_idx;
      tl->idx_real = real_idx;
      tl->has_next = 0;
      }
    return real_idx;
    }
  
  switch(tl->mode)
    {
    case BG_PLAYER_MODE_NORMAL:
    case BG_PLAYER_MODE_ONE: //!< Play one track and stop

      if((tl->mode == BG_PLAYER_MODE_ONE) && !wrap)
        {
        idx = -1;
        real_idx = idx;
        delete_shuffle_list(tl);
        break;
        }
      
      idx++;
      
      if(idx >= list->num_entries)
        {
        if(wrap)
          idx = 0;
        else
          idx = -1; // Finished
        }
      
      real_idx = idx;

      delete_shuffle_list(tl);
      break;
    case BG_PLAYER_MODE_REPEAT: //!< Repeat current album
      idx++;

      if(idx >= list->num_entries)
        idx = 0;
      
      real_idx = idx;
      delete_shuffle_list(tl);
      break;
    case BG_PLAYER_MODE_SHUFFLE: //!< Shuffle (implies repeat)

      if(!tl->shuffle_list)
        tl->shuffle_list = create_shuffle_list(list->num_entries);
      
      if(idx < 0)
        {
        int i;
        for(i = 0; i < list->num_entries; i++)
          {
          if(tl->shuffle_list[i] == tl->idx_real)
            {
            idx = i;
            break;
            }
          }
        }
      idx++;
      if(idx >= list->num_entries)
        idx = 0;
      
      real_idx = tl->shuffle_list[idx];
      
      break;
    case BG_PLAYER_MODE_LOOP:  //!< Loop current track
      real_idx = idx;
      delete_shuffle_list(tl);
      break;
    }

  if(advance)
    {
    tl->idx = idx;
    tl->idx_real = real_idx;
    }

  return real_idx;
  }

static int prev_track(int num_tracks, int mode, int * idx_p, int ** shuffle_list)
  {
  int real_idx = 0;
  int idx = *idx_p;

  //  fprintf(stderr, "prev_track: %d %d %d\n", num_tracks, mode, idx);
  
  if(!num_tracks)
    return -1;
  
  switch(mode)
    {
    case BG_PLAYER_MODE_NORMAL:
    case BG_PLAYER_MODE_REPEAT: //!< Repeat current album
    case BG_PLAYER_MODE_ONE: //!< Play one track and stop
      idx--;
      if(idx < 0)
        idx = num_tracks - 1;

      real_idx = idx;
      
      if(shuffle_list && *shuffle_list)
        {
        free(*shuffle_list);
        *shuffle_list = NULL;
        }

      break;
    case BG_PLAYER_MODE_SHUFFLE: //!< Shuffle (implies repeat)

      idx--;
      if(idx < 0)
        idx = num_tracks - 1;

      if(!(*shuffle_list))
        *shuffle_list = create_shuffle_list(num_tracks);
      
      real_idx = (*shuffle_list)[idx];
      
      break;
    case BG_PLAYER_MODE_LOOP:  //!< Loop current track
      if(shuffle_list && *shuffle_list)
        {
        free(*shuffle_list);
        *shuffle_list = NULL;
        }
      real_idx = idx;
      break;
    }

  //  fprintf(stderr, "prev_track 1: %d %d\n", idx, real_idx);

  if(idx_p)
    *idx_p = idx;
  
  return real_idx;
  }


static void position_changed(bg_player_tracklist_t * l)
  {
  gavl_value_t val;
  gavl_msg_t * evt;
  int i;
  gavl_time_t t;
  const gavl_dictionary_t * dict;
  gavl_array_t * list = gavl_get_tracks_nc(l->cnt);

  
  
  l->duration_before = 0;
  l->duration_after = 0;
  l->duration = GAVL_TIME_UNDEFINED;
  
  gavl_value_init(&val);
  gavl_value_set_int(&val, l->idx_real);
  
  
  evt = bg_msg_sink_get(l->evt_sink);
  
  bg_msg_set_state(evt, 
                   BG_MSG_STATE_CHANGED, 1,
                   BG_PLAYER_STATE_CTX,
                   BG_PLAYER_STATE_QUEUE_IDX,
                   &val);
  
  bg_msg_sink_put(l->evt_sink, evt);

  if(l->idx_real < 0)
    return;
  
  dict = gavl_track_get_metadata(list->entries[l->idx_real].v.dictionary);
  gavl_dictionary_get_long(dict, GAVL_META_APPROX_DURATION, &l->duration);
  
  for(i = 0; i < list->num_entries; i++)
    {
    if((i < l->idx_real) && (l->duration_before != GAVL_TIME_UNDEFINED))
      {
      dict = gavl_track_get_metadata(list->entries[i].v.dictionary);

      t = GAVL_TIME_UNDEFINED;
      
      if(gavl_dictionary_get_long(dict, GAVL_META_APPROX_DURATION, &t) &&
         (t != GAVL_TIME_UNDEFINED))
        l->duration_before += t;
      else
        l->duration_before = GAVL_TIME_UNDEFINED;
      }
    else if((i > l->idx_real) && (l->duration_after != GAVL_TIME_UNDEFINED))
      {
      dict = gavl_track_get_metadata(list->entries[i].v.dictionary);

      t = GAVL_TIME_UNDEFINED;
      
      if(gavl_dictionary_get_long(dict, GAVL_META_APPROX_DURATION, &t) &&
         (t != GAVL_TIME_UNDEFINED))
        l->duration_after += t;
      else
        {
        l->duration_after = GAVL_TIME_UNDEFINED;
        break;
        }
      }
    }
  //  fprintf(stderr, "position changed %"PRId64" %"PRId64" %"PRId64"\n", 
  //          l->duration_before, l->duration_after, l->duration);
  
  l->current_changed = 1;

  }

void bg_player_tracklist_get_times(bg_player_tracklist_t * l,
                                   gavl_time_t t, gavl_time_t * t_abs,
                                   gavl_time_t * t_rem,
                                   gavl_time_t * t_rem_abs,
                                   double * percentage)
  {
  
  if(l->duration_before == GAVL_TIME_UNDEFINED)
    *t_abs = GAVL_TIME_UNDEFINED;
  else
    *t_abs = t + l->duration_before;
  
  if(l->duration == GAVL_TIME_UNDEFINED)
    *t_rem = GAVL_TIME_UNDEFINED;
  else
    *t_rem = l->duration - t;

  if((l->duration == GAVL_TIME_UNDEFINED) ||
     (l->duration_after == GAVL_TIME_UNDEFINED))
    *t_rem_abs = GAVL_TIME_UNDEFINED;
  else
    *t_rem_abs = l->duration - t + l->duration_after;
 
  if(l->duration > 0)
    *percentage = (double)(t) / (double)l->duration;
  else
    *percentage = -1.0;
  
  }

/* Returns 0 if the track is already in the list */
static int set_id(bg_player_tracklist_t * l, gavl_value_t * track_val, const char * client_id)
  {
  const char * track_id;

  char * new_id;

  gavl_dictionary_t * track;
  gavl_dictionary_t * m;
  if(!(track = gavl_value_get_dictionary_nc(track_val)) ||
     !(m = gavl_track_get_metadata_nc(track)))
    return 0;

  //  fprintf(stderr, "set_id\n");
  //  gavl_dictionary_dump(track, 2);
  
  if(!(track_id = gavl_track_get_id(track)))
    {
    /* We generate the ID as MD5-Sum of the filename. This way we guarantee, that
       the same file is added just once */
    const char * location;

    if(gavl_metadata_get_src(m, GAVL_META_SRC, 0, NULL, &location))
      {
      char md5[MD5STRING_LEN];
      if(!location)
        {
        fprintf(stderr, "Track has a src but no location??\n");
        gavl_dictionary_dump(m, 2);
        return 0;
        }
      bg_get_filename_hash(location, md5);
      gavl_track_set_id(track, md5);
      client_id = NULL;
      }
    else
      {
      char uuid_str[38];
      uuid_t u;
      
      uuid_generate(u);
      uuid_unparse(u, uuid_str);
      gavl_track_set_id(track, uuid_str);
      }
    
    track_id = gavl_track_get_id(track);
    }

  /* Set original ID, client ID and new ID */

  new_id = bg_player_tracklist_make_id(client_id, track_id);
  
  gavl_dictionary_set_string(m, BG_PLAYER_META_CLIENT_ID, client_id);
  gavl_dictionary_set_string(m, BG_PLAYER_META_ORIGINAL_ID, track_id);
  gavl_dictionary_set_string_nocopy(m, GAVL_META_ID, new_id);
  return 1;
  }



static int can_add(bg_player_tracklist_t * l, gavl_value_t * val, int idx, int del, const char * client_id)
  {
  int idx1;
  const gavl_dictionary_t * dict;
  const gavl_dictionary_t * m;
  const char * id;
  const char * klass;

  if(!set_id(l, val, client_id))
    return 0;
  
  if(!(dict = gavl_value_get_dictionary(val)))
    return 0;
  else if(!(m = gavl_track_get_metadata(dict)))
    return 0;
  else if(!(id = gavl_dictionary_get_string(m, GAVL_META_ID)))
    return 0;
  else if(!(klass = gavl_dictionary_get_string(m, GAVL_META_MEDIA_CLASS)))
    return 0;
  else if(!gavl_string_starts_with(klass, "item"))
    return 0;
  /* Check if track is already present. Add it only, when it's deleted before */
  else if((idx1 = gavl_get_track_idx_by_id(l->cnt, id)) >= 0)
    {
    if(!del)
      return 0;
    else if((idx1 < idx) || (idx1 >= idx + del))
      return 0;
    }
  /* Check if track can be played back */

  if(l->application_state &&
     !bg_player_track_get_uri(l->application_state, dict))
    return 0;
  
  
  return 1;
  }

char * bg_player_tracklist_make_id(const char * client_id, const char * original_id)
  {
  char * ret;
  
  char * pos;
  char * track_id;
  track_id = gavl_strdup(original_id);

  if(gavl_string_starts_with(track_id, BG_PLAYQUEUE_ID))
    return track_id;
  
  pos = track_id;

  while(*pos != '\0')
    {
    if(*pos == '/')
      *pos = '~';
    pos++;
    }

  if(client_id)
    ret = bg_sprintf(BG_PLAYQUEUE_ID"/%s~%s", client_id, track_id);
  else
    ret = bg_sprintf(BG_PLAYQUEUE_ID"/%s", track_id);
  
  free(track_id);
  return ret;
  }

char * bg_player_tracklist_id_from_uri(const char * client_id, const char * location)
  {
  char md5[33];
  bg_get_filename_hash(location, md5);
  return bg_player_tracklist_make_id(client_id, md5);
  }

static void splice(bg_player_tracklist_t * l, int idx, int del, int last,
                   gavl_value_t * val, const char * client_id)
  {
  gavl_msg_t * evt;
  const gavl_dictionary_t * cur;
  char * cur_id = NULL;
  int last_num;
  gavl_value_t val1;
  
  gavl_array_t * list;

  gavl_dictionary_t tmp_dict;
  
  //  fprintf(stderr, "Splice %d %d %d %p\n", idx, del, last, val);
  //  gavl_value_dump(val, 2);
  
  list = gavl_get_tracks_nc(l->cnt);
  
  last_num = list->num_entries;
  
  if((cur = bg_player_tracklist_get_current_track(l)))
    cur_id = gavl_strdup(gavl_track_get_id(cur));

  if(idx < 0)
    idx = list->num_entries;
  
  if(del < 0)
    del = list->num_entries - idx;
  
  if(val && (val->type == GAVL_TYPE_ARRAY))
    {
    int i = 0;

    while(i < val->v.array->num_entries)
      {
      if(!can_add(l, &val->v.array->entries[i], idx, del, client_id))
        gavl_array_splice_val(val->v.array, i, 1, NULL);
      else
        i++;
      }
    if(!val->v.array->num_entries)
      {
      if(!del)
        return;
      else
        {
        gavl_array_splice_val(list, idx, del, NULL);
        val = NULL;
        }
      }
    else
      gavl_array_splice_array(list, idx, del, val->v.array);
    }
  else // GAVL_TYPE_ARRAY
    {
    if(!val || !can_add(l, val, idx, del, client_id))
      {
      if(!del)
        return;
      gavl_array_splice_val(list, idx, del, NULL);
      val = NULL;
      }
    else
      {
      gavl_array_splice_val(list, idx, del, val);
      }
    }

  //  fprintf(stderr, "Splice 2\n");
  
  evt = bg_msg_sink_get(l->evt_sink);
  gavl_msg_set_id_ns(evt, BG_MSG_DB_SPLICE_CHILDREN, BG_MSG_NS_DB);

  gavl_dictionary_set_string(&evt->header, GAVL_MSG_CONTEXT_ID,
                             BG_PLAYQUEUE_ID);
  gavl_msg_set_last(evt, last);
  
  gavl_msg_set_arg_int(evt, 0, idx);
  gavl_msg_set_arg_int(evt, 1, del);
  gavl_msg_set_arg(evt, 2, val);
  
  bg_msg_sink_put(l->evt_sink, evt);
  l->list_changed = 1;
  
  l->idx = -1;
  l->idx_real = -1;

#if 0  
  if(!last_num && (list->num_entries > 0))
    {
    l->idx = 0;
    l->idx_real = -1;
    }
  else
#endif
    if(cur_id)
    {
    int i;
    for(i = 0; i < list->num_entries; i++)
      {
      if(!strcmp(cur_id, bg_dictionary_get_id(gavl_track_get_metadata_nc(list->entries[i].v.dictionary))))
        {
        l->idx_real = i;
        break;
        }
      }
    free(cur_id);
    }

  if(l->idx_real < 0)
    l->current_changed = 1;
  
  delete_shuffle_list(l);

  if(last_num != list->num_entries)
    {
    gavl_value_init(&val1);

    gavl_value_set_int(&val1, list->num_entries);
    
    evt = bg_msg_sink_get(l->evt_sink);
    
    bg_msg_set_state(evt, 
                     BG_MSG_STATE_CHANGED, 1,
                     BG_PLAYER_STATE_CTX,
                     BG_PLAYER_STATE_QUEUE_LEN,
                     &val1);
    
    bg_msg_sink_put(l->evt_sink, evt);
 
    }

  gavl_value_init(&val1);
  gavl_value_set_int(&val1, l->idx_real);
  
  
  evt = bg_msg_sink_get(l->evt_sink);
  
  bg_msg_set_state(evt, 
                   BG_MSG_STATE_CHANGED, 1,
                   BG_PLAYER_STATE_CTX,
                   BG_PLAYER_STATE_QUEUE_IDX,
                   &val1);
  
  bg_msg_sink_put(l->evt_sink, evt);
  
  /* Update metadata for root object:
     
   */
  gavl_track_update_children(l->cnt);


  evt = bg_msg_sink_get(l->evt_sink);

  gavl_msg_set_id_ns(evt, BG_MSG_DB_OBJECT_CHANGED, BG_MSG_NS_DB);
  gavl_dictionary_set_string(&evt->header, GAVL_MSG_CONTEXT_ID, BG_PLAYQUEUE_ID);


  gavl_dictionary_init(&tmp_dict);
  gavl_dictionary_copy(&tmp_dict, l->cnt);
  
  gavl_dictionary_set(&tmp_dict, GAVL_META_CHILDREN, NULL);
  
  gavl_msg_set_arg_dictionary(evt, 0, &tmp_dict);
  bg_msg_sink_put(l->evt_sink, evt);
  
  gavl_dictionary_free(&tmp_dict);
  }

void bg_player_tracklist_clear(bg_player_tracklist_t * l)
  {
  int old_len;
  gavl_array_t * list;
  
  list = gavl_get_tracks_nc(l->cnt);
  old_len = list->num_entries;
  /* TODO: Stop if we are playing */
  gavl_array_splice_val(list, 0, -1, NULL);
  
  delete_shuffle_list(l);
  
  if(old_len > 0)
    {
    l->current_changed = 1;
    l->list_changed = 1;
    }
  l->idx = -1;
  l->idx_real = -1;
  }

void bg_player_tracklist_set_current_by_idx(bg_player_tracklist_t * l, int idx)
  {
  int idx_real_new;
  
  gavl_array_t * list;
  list = gavl_get_tracks_nc(l->cnt);
  
  /* Set track */
  idx_real_new = idx;
  l->idx = idx;

  if((idx < 0) || (idx >= list->num_entries))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "bg_player_tracklist_set_current_by_idx: idx %d out of range (0 %d)", idx, list->num_entries-1);
    return;
    }
  
  if(l->mode == BG_PLAYER_MODE_SHUFFLE)
    {
    int i;
    if(!l->shuffle_list)
      l->shuffle_list = create_shuffle_list(list->num_entries);
            
    for(i = 0; i < list->num_entries; i++)
      {
      if(l->shuffle_list[i] == l->idx_real)
        {
        l->idx = i;
        break;
        }
      }
    }
  else // No shuffle
    {
    delete_shuffle_list(l);
    l->idx = idx_real_new;
    }

  l->idx_real = idx_real_new;
  position_changed(l);
  }

int bg_player_tracklist_set_current_by_id(bg_player_tracklist_t * l, const char * id)
  {
  int idx;
          
  if((idx = gavl_get_track_idx_by_id(l->cnt, id)) < 0)
    {
    gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN,
           "Cannot set current track: No such track %s", id);
    return 0;
    }
  bg_player_tracklist_set_current_by_idx(l, idx);
  return 1;
  }


int bg_player_tracklist_handle_message(bg_player_tracklist_t * l,
                                       gavl_msg_t * msg)
  {
  const char * client_id;
  int ret = 0;
  
  //  fprintf(stderr, "bg_player_tracklist_handle_message\n");
  //  gavl_msg_dump(msg, 2);

  client_id = gavl_msg_get_client_id(msg);
  
  switch(msg->NS)
    {
    case BG_MSG_NS_PLAYER:
      switch(msg->ID)
        {
        case BG_PLAYER_CMD_SET_CURRENT_TRACK:
          {
          const char * id1;
          if(!(id1 = gavl_msg_get_arg_string_c(msg, 0)))
            {
            gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN,
                   "Cannot set current track: ID missing");
            break;
            }
          
          if(!bg_player_tracklist_set_current_by_id(l, id1))
            break;
          }
          break;
        case BG_PLAYER_CMD_SET_NEXT_TRACK:
          {
          gavl_value_t val;
          gavl_value_init(&val);
          // fprintf(stderr, "Set next location\n");
          if(l->idx_real < 0)
            return 1;

          /* Remove all entries before the index. This prevents the tracklist from
             growing infinitely from repeated calls to BG_PLAYER_CMD_SET_NEXT_LOCATION */
          if(l->idx_real > 0)
            {
            splice(l, 0, l->idx_real, 0, NULL, client_id);
            l->idx_real = 0;
            }
          gavl_msg_get_arg(msg, 0, &val);
          splice(l, l->idx_real+1, -1, 1, &val, client_id);
          l->has_next = 1;
          gavl_value_free(&val);
          }
          ret = 1;
          break;
        }
      break;
    case BG_MSG_NS_DB:
      
      switch(msg->ID)
        {
        case BG_CMD_DB_SPLICE_CHILDREN:
          {
          int idx, del, last;
          const char * id;
          gavl_value_t add;
          
          gavl_value_t add_arr_val;
          gavl_array_t * add_arr;
          gavl_value_init(&add);
          gavl_value_init(&add_arr_val);
          
          id = gavl_dictionary_get_string(&msg->header, GAVL_MSG_CONTEXT_ID);
          if(!id || strcmp(id, BG_PLAYQUEUE_ID))
            break;

          add_arr = gavl_value_set_array(&add_arr_val);
          
          gavl_msg_get_splice_children(msg, &last, &idx, &del, &add);

          //          fprintf(stderr, "SPLICE %d %d %d\n", idx, del, last);
          //          gavl_value_dump(&add, 2);
          
          bg_tracks_resolve_locations(&add, add_arr, BG_INPUT_FLAG_GET_FORMAT);
          
          splice(l, idx, del, last, &add_arr_val, client_id);
          
          
          l->has_next = 0;

          if(l->list_changed)
            delete_shuffle_list(l);
          ret = 1;
          gavl_value_free(&add);
          gavl_value_free(&add_arr_val);
          break;
          }
        case BG_FUNC_DB_BROWSE_CHILDREN:
          {
          gavl_msg_t * resp;
          const char * id;
          const gavl_array_t * arr;
          int start, num, one_answer;

          bg_mdb_get_browse_children_request(msg, &id, &start, &num, &one_answer);
          
          ret = 1;
          
          if(!id ||
             strcmp(id, BG_PLAYQUEUE_ID) ||
             !(arr = gavl_get_tracks_nc(l->cnt)) ||
             !arr->num_entries)
            break;
          
          if(!bg_mdb_adjust_num(start, &num, arr->num_entries))
            break;

          /* Range requested */
          if(num < arr->num_entries)
            {
            int i;
            gavl_array_t tmp_arr;
            gavl_array_init(&tmp_arr);
            resp = bg_msg_sink_get(l->evt_sink);
                
            /* Range */
            
            for(i = 0; i < num; i++)
              gavl_array_splice_val(&tmp_arr, i, 0, &arr->entries[i+start]);
                
            bg_mdb_set_browse_children_response(resp, &tmp_arr, msg, &start, 1, arr->num_entries);
            gavl_array_free(&tmp_arr);
            bg_msg_sink_put(l->evt_sink, resp);
            }
          else if(one_answer)
            {
            resp = bg_msg_sink_get(l->evt_sink);
            bg_mdb_set_browse_children_response(resp, arr, msg, &start, 1, arr->num_entries);
            bg_msg_sink_put(l->evt_sink, resp);
            }
          else // Multiple answers
            {
            int i;
            gavl_array_t tmp_arr;
            gavl_array_init(&tmp_arr);
            start = 0;
            
            for(i = 0; i < num; i++)
              {
              if(tmp_arr.num_entries >= 50)
                {
                resp = bg_msg_sink_get(l->evt_sink);
                bg_mdb_set_browse_children_response(resp, &tmp_arr, msg, &start, 0, arr->num_entries);
                bg_msg_sink_put(l->evt_sink, resp);
                gavl_array_reset(&tmp_arr);
                }
              gavl_array_splice_val(&tmp_arr, i, 0, &arr->entries[i]);
              }

            if(tmp_arr.num_entries > 0)
              {
              resp = bg_msg_sink_get(l->evt_sink);
              bg_mdb_set_browse_children_response(resp, &tmp_arr, msg, &start, 1, arr->num_entries);
              bg_msg_sink_put(l->evt_sink, resp);
              gavl_array_free(&tmp_arr);
              }
            }
          ret = 1;
          break;
          }
        case BG_FUNC_DB_BROWSE_OBJECT:
          {
          gavl_msg_t * res;
          const gavl_dictionary_t * child;
          
          const char * ctx_id = gavl_dictionary_get_string(&msg->header,
                                                           GAVL_MSG_CONTEXT_ID);
          

          //          fprintf(stderr, "BG_FUNC_DB_BROWSE_OBJECT %s\n", ctx_id);
          
          if(!strcmp(ctx_id, BG_PLAYQUEUE_ID))
            {
            gavl_dictionary_t tmp_dict;
            gavl_dictionary_init(&tmp_dict);
            gavl_dictionary_copy(&tmp_dict, l->cnt);
            gavl_dictionary_set(&tmp_dict, GAVL_META_CHILDREN, NULL);
            
            res = bg_msg_sink_get(l->evt_sink);
            bg_mdb_set_browse_obj_response(res, &tmp_dict, msg, -1, -1);
            bg_msg_sink_put(l->evt_sink, res);
            gavl_dictionary_free(&tmp_dict);
            }
          else if((child = gavl_get_track_by_id(l->cnt, ctx_id)))
            {
            res = bg_msg_sink_get(l->evt_sink);
            bg_mdb_set_browse_obj_response(res, child, msg,
                                           gavl_get_track_idx_by_id(l->cnt, ctx_id),
                                           gavl_track_get_num_children(l->cnt));
            bg_msg_sink_put(l->evt_sink, res);
            }
          ret = 1;
          }
        }
      break;
      
    }
  return ret;
  }

void bg_player_tracklist_splice(bg_player_tracklist_t * l, int idx, int del,
                                gavl_value_t * val, const char * client_id)
  {
  splice(l, idx, del, 1, val, client_id);
  l->has_next = 0;
  }

void bg_player_tracklist_free(bg_player_tracklist_t * l)
  {
  delete_shuffle_list(l);
  gavl_dictionary_destroy(l->cnt);

  if(l->hub)
    bg_msg_hub_destroy(l->hub);
  
  }

void bg_player_tracklist_init(bg_player_tracklist_t * l, bg_msg_sink_t * evt_sink)
  {
  gavl_value_t val; 
  gavl_dictionary_t * m;
  const char * label;
  
  memset(l, 0, sizeof(*l));
  l->idx      = -1;
  l->idx_real = -1;
  
  l->hub = bg_msg_hub_create(1);
  
  l->evt_sink = bg_msg_hub_get_sink(l->hub);

  bg_msg_hub_connect_sink(l->hub, evt_sink);
  
  l->cnt = gavl_dictionary_create();
  
  gavl_value_init(&val);
  gavl_value_set_array(&val);
  gavl_dictionary_set_nocopy(l->cnt, GAVL_META_CHILDREN, &val);
  
  m = gavl_dictionary_get_dictionary_create(l->cnt, GAVL_META_METADATA);

  if((label = bg_app_get_label()))
    gavl_dictionary_set_string_nocopy(m, GAVL_META_LABEL, bg_sprintf("%s queue", label));
  else
    gavl_dictionary_set_string(m, GAVL_META_LABEL, "Player queue");
  gavl_dictionary_set_string(m, GAVL_META_MEDIA_CLASS, GAVL_META_MEDIA_CLASS_ROOT_PLAYQUEUE);
  gavl_dictionary_set_string(m, GAVL_META_ID, BG_PLAYQUEUE_ID);

  bg_mdb_set_editable(l->cnt);
  bg_mdb_add_can_add(l->cnt, "item.audio*");
  bg_mdb_add_can_add(l->cnt, "item.video*");
  bg_mdb_add_can_add(l->cnt, "item.image*");
  bg_mdb_add_can_add(l->cnt, "item.location");
  
  
  }

int bg_player_tracklist_advance(bg_player_tracklist_t * l, int force)
  {
  int idx_real_new;
  
  idx_real_new = next_track(l, 1, force);

  if(idx_real_new < 0)
    return 0;
  l->idx_real = idx_real_new;
  position_changed(l);
  return 1;
  }

int bg_player_tracklist_back(bg_player_tracklist_t * l)
  {
  int idx_real_new;
  gavl_array_t * list = gavl_get_tracks_nc(l->cnt);
  idx_real_new = prev_track(list->num_entries, l->mode, &l->idx, &l->shuffle_list);
  if(idx_real_new < 0)
    return 0;
  
  l->idx_real = idx_real_new;
  position_changed(l);
  return 1;
  }

gavl_dictionary_t *
bg_player_tracklist_get_current_track(bg_player_tracklist_t * l)
  {
  gavl_array_t * list = gavl_get_tracks_nc(l->cnt);
  
  if(l->idx_real < 0)
    {
    if(!list->num_entries)
      return NULL;
    
    if(l->idx < 0)
      l->idx = 0;
    
    if(l->mode == BG_PLAYER_MODE_SHUFFLE)
      {
      if(!l->shuffle_list)
        l->shuffle_list = create_shuffle_list(list->num_entries);

      /* Start with random track */
      l->idx_real = l->shuffle_list[l->idx];
      }
    else
      l->idx_real = l->idx;
    }
  
  return list->entries[l->idx_real].v.dictionary;
  }
  
gavl_dictionary_t *
bg_player_tracklist_get_next(bg_player_tracklist_t * l)
  {
  int idx_real;  
  gavl_array_t * list = gavl_get_tracks_nc(l->cnt);
  
  idx_real = next_track(l, 0, 0);

  if(idx_real < 0)
    return NULL;
  else
    return list->entries[idx_real].v.dictionary;
  }

void bg_player_tracklist_set_mode(bg_player_tracklist_t * l, int * mode)
  {
  while(*mode >= BG_PLAYER_MODE_MAX)
    (*mode) -= BG_PLAYER_MODE_MAX;
  while(*mode < 0)
    (*mode) += BG_PLAYER_MODE_MAX;
  l->mode = *mode;
  }
