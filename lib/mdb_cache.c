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

#include <stdlib.h>
#include <string.h>

#include <config.h>

#include <gmerlin/mdb.h>
#include <gavl/utils.h>
#include <gavl/log.h>
#define LOG_DOMAIN "mdb.cache"


#define CONTAINER_EXPANDED        (1<<0)
#define CONTAINER_OPEN            (1<<1)

#define CONTAINER_ROOT            (1<<8)
#define CONTAINER_COMPLETE        (1<<9)

/* Never delete these */
#define CONTAINER_PERSISTENT      (1<<10)

#define STATE_INIT     0
#define STATE_OPEN     1
#define STATE_UNUSED   2

#define CLEANUP_INTERVAL GAVL_TIME_SCALE
#define UNUSED_TIME      (60*GAVL_TIME_SCALE)

typedef struct
  {
  char * id;
  gavl_array_t children;
  int flags;
  gavl_time_t last_used;

  int state;
  
  } container_t;

struct bg_mdb_cache_s
  {
  gavl_timer_t * timer;

  container_t * containers;
  int num_containers;
  int containers_alloc;
  
  bg_controllable_t ctrl;

  bg_control_t player_ctrl;
  bg_control_t mdb_ctrl;

  bg_controllable_t * player_ctrl_p;
  bg_controllable_t * mdb_ctrl_p;

  gavl_time_t last_cleanup;
  
  };

static void container_free(container_t * cnt)
  {
  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Freeing container: %s", cnt->id);
  
  if(cnt->id)
    free(cnt->id);
  gavl_array_free(&cnt->children);
  }

static void container_init(container_t * cnt)
  {
  memset(cnt, 0, sizeof(*cnt));
  }

static void browse_children(bg_mdb_cache_t * cache, container_t * cnt)
  {
  gavl_msg_t * cmd;
  int is_playqueue = 0;
  if(!strcmp(cnt->id, BG_PLAYQUEUE_ID))
    {
    is_playqueue = 1;
    cmd = bg_msg_sink_get(cache->player_ctrl.cmd_sink);
    }
  else
    cmd = bg_msg_sink_get(cache->mdb_ctrl.cmd_sink);

  /* */
  bg_mdb_set_browse_children_request(cmd, cnt->id, 0, -1, 0);
  
  if(is_playqueue)
    bg_msg_sink_put(cache->player_ctrl.cmd_sink);
  else
    bg_msg_sink_put(cache->mdb_ctrl.cmd_sink);

  
  }

#if 1
static void send_children(bg_mdb_cache_t * cache, container_t * cnt, int start, int num, int flags)
  {
  int i;
  const char * container_before = NULL;
  const char * item_before = NULL;
  const gavl_dictionary_t * dict;
  const gavl_dictionary_t * m;
  const char * klass = NULL;
  gavl_msg_t * msg;

  gavl_array_t item_children;
  gavl_array_t container_children;

  gavl_array_init(&item_children);
  gavl_array_init(&container_children);
  
  /* Get siblings before */
  for(i = 0; i < start; i++)
    {
    if((dict = gavl_value_get_dictionary(&cnt->children.entries[i])) &&
       (m = gavl_track_get_metadata(dict)) &&
       (klass = gavl_dictionary_get_string(m, GAVL_META_CLASS)))
      {
      if(gavl_string_starts_with(klass, "item"))
        item_before = gavl_dictionary_get_string(m, GAVL_META_ID);
      else if(gavl_string_starts_with(klass, "container"))
        container_before = gavl_dictionary_get_string(m, GAVL_META_ID);
      }
    }
  
  for(i = 0; i < num; i++)
    {
    const char * id = NULL;
    
    if((dict = gavl_value_get_dictionary(&cnt->children.entries[i+start])) &&
       (m = gavl_track_get_metadata(dict)) &&
       (klass = gavl_dictionary_get_string(m, GAVL_META_CLASS)) &&
       (id = gavl_dictionary_get_string(m, GAVL_META_ID)))
      {
      if(gavl_string_starts_with(klass, "item") && (flags & CONTAINER_OPEN))
        {
        gavl_array_splice_val(&item_children, -1, 0, &cnt->children.entries[i+start]);
        }
      else if(gavl_string_starts_with(klass, "container") && (flags & CONTAINER_EXPANDED))
        {
        gavl_array_splice_val(&container_children, -1, 0, &cnt->children.entries[i+start]);
        }
      }
    }
  
  if(item_children.num_entries)
    {
    msg = bg_msg_sink_get(cache->ctrl.evt_sink);
    gavl_msg_set_id_ns(msg, BG_MSG_DB_CACHE_ADD_LIST_ITEMS, BG_MSG_NS_DB_CACHE);
    gavl_dictionary_set_string(&msg->header, GAVL_MSG_CONTEXT_ID, cnt->id);
    gavl_msg_set_arg_string(msg, 0, item_before);
    gavl_msg_set_arg_array_nocopy(msg, 1, &item_children);
    bg_msg_sink_put(cache->ctrl.evt_sink);
    }

  if(container_children.num_entries)
    {
    msg = bg_msg_sink_get(cache->ctrl.evt_sink);
    gavl_msg_set_id_ns(msg, BG_MSG_DB_CACHE_ADD_TREE_ITEMS, BG_MSG_NS_DB_CACHE);
    gavl_dictionary_set_string(&msg->header, GAVL_MSG_CONTEXT_ID, cnt->id);
    gavl_msg_set_arg_string(msg, 0, container_before);
    gavl_msg_set_arg_array_nocopy(msg, 1, &container_children);
    bg_msg_sink_put(cache->ctrl.evt_sink);
    }
  
  gavl_array_free(&item_children);
  gavl_array_free(&container_children);
  }

#endif

static container_t * has_container(bg_mdb_cache_t * cache, const char * id)
  {
  int i;
  
  for(i = 0; i < cache->num_containers; i++)
    {
    if(!strcmp(id, cache->containers[i].id))
      return &cache->containers[i];
    }
  return NULL;
  }

static container_t * get_container(bg_mdb_cache_t * cache, const char * id)
  {
  container_t * ret;

  if((ret = has_container(cache, id)))
    return ret;
  
  if(cache->num_containers == cache->containers_alloc)
    {
    cache->containers_alloc += 32;
    cache->containers =
      realloc(cache->containers, sizeof(*cache->containers)*cache->containers_alloc);
    memset(cache->containers + cache->num_containers, 0,
           (cache->containers_alloc-cache->num_containers)*sizeof(*cache->containers));
    }
  ret = cache->containers + cache->num_containers;
  ret->id = gavl_strdup(id);
  cache->num_containers++;
  return ret;
  }

static container_t * has_parent(bg_mdb_cache_t * cache, const char * id)
  {
  container_t * ret;
  char * parent_id = bg_mdb_get_parent_id(id);

  ret = has_container(cache, parent_id);
  
  free(parent_id);
  return ret;
  }

static int handle_mdb_message(void * data, gavl_msg_t * msg)
  {
  const char *id;
  bg_mdb_cache_t * c = data;
  container_t * cnt;
  
  switch(msg->NS)
    {
    case BG_MSG_NS_DB:

      switch(msg->ID)
        {
        case BG_MSG_DB_SPLICE_CHILDREN:
        case BG_RESP_DB_BROWSE_CHILDREN:
          {
          int i;
          int last, idx, del;
          int num_added = 0;
          gavl_value_t add;
          gavl_value_init(&add);
          
          /* Check if we are meant */
          if(!(id = gavl_dictionary_get_string(&msg->header, GAVL_MSG_CONTEXT_ID)) ||
             !(cnt = has_container(c, id)))
            {
            return 1;
            }
          
          if((msg->ID == BG_MSG_DB_SPLICE_CHILDREN) &&
             !(cnt->flags & CONTAINER_COMPLETE))
            return 1;

          if((msg->ID == BG_RESP_DB_BROWSE_CHILDREN) &&
             (cnt->flags & CONTAINER_COMPLETE))
            return 1;
          
          gavl_msg_get_splice_children(msg, &last, &idx, &del, &add);

          /*
            fprintf(stderr, "Splice children:\n");
            gavl_value_dump(&add, 2);
            fprintf(stderr, "\n");
          */
          
          /* Skip player queue */
          if(cnt->flags & CONTAINER_ROOT)
            idx++;
          
          if((msg->ID == BG_RESP_DB_BROWSE_CHILDREN) && last)
            cnt->flags |= CONTAINER_COMPLETE;
          
          if(del > 0)
            {
            gavl_array_t del_items;
            gavl_array_t del_containers;
            const gavl_dictionary_t * dict;
            const char * klass;
            
            gavl_array_init(&del_items);
            gavl_array_init(&del_containers);

            for(i = 0; i < del; i++)
              {
              if((dict = gavl_value_get_dictionary(&cnt->children.entries[i+idx])) &&
                 (dict = gavl_track_get_metadata(dict)) &&
                 (klass = gavl_dictionary_get_string(dict, GAVL_META_CLASS)))
                {
                if(gavl_string_starts_with(klass, "item") && (cnt->flags & CONTAINER_OPEN))
                  gavl_string_array_add(&del_items, gavl_dictionary_get_string(dict, GAVL_META_ID));

                else if(gavl_string_starts_with(klass, "container") && (cnt->flags & CONTAINER_EXPANDED))
                  gavl_string_array_add(&del_containers, gavl_dictionary_get_string(dict, GAVL_META_ID));
                }
              }

            if(del_items.num_entries)
              {
              msg = bg_msg_sink_get(c->ctrl.evt_sink);
              gavl_msg_set_id_ns(msg, BG_MSG_DB_CACHE_DELETE_LIST_ITEMS, BG_MSG_NS_DB_CACHE);
              gavl_dictionary_set_string(&msg->header, GAVL_MSG_CONTEXT_ID, id);
              gavl_msg_set_arg_array_nocopy(msg, 0, &del_items);
              bg_msg_sink_put(c->ctrl.evt_sink);
              }
            if(del_containers.num_entries)
              {
              msg = bg_msg_sink_get(c->ctrl.evt_sink);
              gavl_msg_set_id_ns(msg, BG_MSG_DB_CACHE_DELETE_TREE_ITEMS, BG_MSG_NS_DB_CACHE);
              gavl_dictionary_set_string(&msg->header, GAVL_MSG_CONTEXT_ID, id);
              gavl_msg_set_arg_array_nocopy(msg, 0, &del_containers);
              bg_msg_sink_put(c->ctrl.evt_sink);
              }
            
            gavl_array_free(&del_items);
            gavl_array_free(&del_containers);

            gavl_array_splice_val(&cnt->children, idx, del, NULL);
            }
          
          /* Store locally */
          if(add.type == GAVL_TYPE_ARRAY)
            {
            gavl_array_t * arr = gavl_value_get_array_nc(&add);
            num_added = arr->num_entries;
            gavl_array_splice_array_nocopy(&cnt->children, idx, 0, arr);
            }
          else if(add.type == GAVL_TYPE_DICTIONARY)
            {
            gavl_array_splice_val_nocopy(&cnt->children, idx, 0, &add);
            num_added = 1;
            }
          
          /* Send message(s) */
          if(num_added)
            send_children(c, cnt, idx, num_added, cnt->flags);
          
          gavl_value_free(&add);
          }
          break;
        case BG_MSG_DB_OBJECT_CHANGED:
        case BG_RESP_DB_BROWSE_OBJECT:
          {
          gavl_dictionary_t * dict;
          const gavl_dictionary_t * m;
          gavl_dictionary_t changed;
          const char * klass;
          gavl_msg_t * evt;
          
          gavl_dictionary_init(&changed);
          gavl_msg_get_arg_dictionary(msg, 0, &changed);
          
          /* Check if we are meant */
          if(!(id = gavl_dictionary_get_string(&msg->header, GAVL_MSG_CONTEXT_ID)) ||
             !(cnt = has_parent(c, id)) ||
             !(dict = gavl_get_track_by_id_arr_nc(&cnt->children, id)))
            return 1;          
          
          /* Store locally */
          bg_mdb_object_changed(dict, &changed);
          gavl_dictionary_free(&changed);

          //          fprintf(stderr, "Object changed\n");
          //          gavl_dictionary_dump(dict, 2);
          
          /* Send message */
          if((m = gavl_track_get_metadata(dict)) &&
             (klass = gavl_dictionary_get_string(m, GAVL_META_CLASS)))
            {
            if(gavl_string_starts_with(klass, "container") && (cnt->flags & CONTAINER_EXPANDED))
              {
              evt = bg_msg_sink_get(c->ctrl.evt_sink);
              gavl_msg_set_id_ns(evt, BG_MSG_DB_CACHE_UPDATE_TREE_ITEM, BG_MSG_NS_DB_CACHE);
              gavl_dictionary_set_string(&evt->header, GAVL_MSG_CONTEXT_ID, id);
              gavl_msg_set_arg_dictionary(evt, 0, dict);
              bg_msg_sink_put(c->ctrl.evt_sink);
              }
            else if(gavl_string_starts_with(klass, "item") && (cnt->flags & CONTAINER_OPEN))
              {
              evt = bg_msg_sink_get(c->ctrl.evt_sink);
              gavl_msg_set_id_ns(evt, BG_MSG_DB_CACHE_UPDATE_LIST_ITEM, BG_MSG_NS_DB_CACHE);
              gavl_dictionary_set_string(&evt->header, GAVL_MSG_CONTEXT_ID, id);
              gavl_msg_set_arg_dictionary(evt, 0, dict);
              bg_msg_sink_put(c->ctrl.evt_sink);
              }
            }
          }
          break;
        }
      
      break;
    }
  return 1;                      
  }

static void init_player_queue(gavl_dictionary_t * dict)
  {
  gavl_dictionary_t * m;
  m = gavl_dictionary_get_dictionary_create(dict, GAVL_META_METADATA);
  
  gavl_dictionary_set_string(m, GAVL_META_ID, BG_PLAYQUEUE_ID);
  gavl_dictionary_set_string(m, GAVL_META_CLASS, GAVL_META_CLASS_ROOT_PLAYQUEUE);
  gavl_dictionary_set_string(m, GAVL_META_LABEL, "No player available");
  }
  
static container_t * init_root_container(bg_mdb_cache_t * c)
  {
  gavl_value_t track_val;
  gavl_dictionary_t * track;
  
  container_t * cnt = get_container(c, "/");
  
  cnt->flags |= (CONTAINER_ROOT | CONTAINER_PERSISTENT);
  
  gavl_value_init(&track_val);
  track = gavl_value_set_dictionary(&track_val);
  init_player_queue(track);
  gavl_array_splice_val_nocopy(&cnt->children, 0, 0, &track_val);

  browse_children(c, cnt);
  cnt->state = STATE_OPEN;
  
  return cnt;
  }

static int handle_gui_message(void * data, gavl_msg_t * msg)
  {
  const char * id;
          
  bg_mdb_cache_t * c = data;
  container_t * cnt;

  switch(msg->NS)
    {
    case BG_MSG_NS_DB_CACHE:

      switch(msg->ID)
        {
        case BG_CMD_DB_CACHE_CONTAINER_EXPAND:
          id = gavl_dictionary_get_string(&msg->header, GAVL_MSG_CONTEXT_ID);

          //  fprintf(stderr, "Expand: %s\n", id);
          
          cnt = get_container(c, id);

          if(cnt->state == STATE_UNUSED)
            cnt->state = STATE_OPEN;
          
          if(cnt->state == STATE_INIT)
            {
            browse_children(c, cnt);
            cnt->state = STATE_OPEN;
            }
          else if(cnt->state == STATE_OPEN)
            send_children(c, cnt, 0, cnt->children.num_entries, CONTAINER_EXPANDED);
          
          cnt->flags |= CONTAINER_EXPANDED;
          
          break;
        case BG_CMD_DB_CACHE_CONTAINER_COLLAPSE:
          if((id = gavl_dictionary_get_string(&msg->header, GAVL_MSG_CONTEXT_ID)) &&
             (cnt = has_container(c, id)))
            {
            cnt->flags &= ~CONTAINER_EXPANDED; // Will get kicked out

            if(!(cnt->flags & (CONTAINER_EXPANDED|CONTAINER_OPEN)))
              {
              cnt->last_used = gavl_timer_get(c->timer);
              cnt->state = STATE_UNUSED;
              }
            }
          break;

        case BG_CMD_DB_CACHE_CONTAINER_OPEN:
          {
          gavl_msg_t * evt;
          gavl_dictionary_t * dict;
          id = gavl_dictionary_get_string(&msg->header, GAVL_MSG_CONTEXT_ID);
          cnt = get_container(c, id);

          cnt->flags |= CONTAINER_OPEN;
          
          if(cnt->state == STATE_UNUSED)
            cnt->state = STATE_OPEN;

          dict = bg_mdb_cache_get_object(c, cnt->id);
          
          /* Send container info */
          evt = bg_msg_sink_get(c->ctrl.evt_sink);
          gavl_msg_set_id_ns(evt, BG_MSG_DB_CACHE_CONTAINER_INFO, BG_MSG_NS_DB_CACHE);
          gavl_dictionary_set_string(&evt->header, GAVL_MSG_CONTEXT_ID, cnt->id);
          gavl_msg_set_arg_dictionary(evt, 0, dict);
          bg_msg_sink_put(c->ctrl.evt_sink);

          /* No children -> Container is already complete */
          if(!gavl_track_get_num_children(dict))
            cnt->flags |= CONTAINER_COMPLETE;
          
          if(cnt->state == STATE_INIT)
            {
            browse_children(c, cnt);
            cnt->state = STATE_OPEN;
            }
          else if(cnt->state == STATE_OPEN)
            send_children(c, cnt, 0, cnt->children.num_entries, CONTAINER_OPEN);
          }
          break;
        case BG_CMD_DB_CACHE_CONTAINER_CLOSE:
          if((id = gavl_dictionary_get_string(&msg->header, GAVL_MSG_CONTEXT_ID)) &&
             (cnt = has_container(c, id)))
            {
            cnt->flags &= ~CONTAINER_OPEN; // Will get kicked out

            if(!(cnt->flags & (CONTAINER_EXPANDED|CONTAINER_OPEN)))
              {
              cnt->last_used = gavl_timer_get(c->timer);
              cnt->state = STATE_UNUSED;
              }
            }
          break;
        case BG_CMD_DB_CACHE_DELETE_ITEMS:
        case BG_CMD_DB_CACHE_DELETE_CONTAINERS:
          if((id = gavl_dictionary_get_string(&msg->header, GAVL_MSG_CONTEXT_ID)) &&
             (cnt = has_container(c, id)))
            {
            int i, idx;
            char * delete_map;
            gavl_array_t arr;
            int num_deleted = 0;
            const char * delete_start;
            const char * delete_end;
            gavl_msg_t * cmd;
            bg_msg_sink_t * sink;
            const char * parent_id;
            
            gavl_array_init(&arr);

            parent_id = gavl_dictionary_get_string(&msg->header, GAVL_MSG_CONTEXT_ID);
          
            gavl_msg_get_arg_array(msg, 0, &arr);
          
            /* Translate indices and pass to backend */
            fprintf(stderr, "Delete Items %s\n", id);
            gavl_array_dump(&arr, 2);

            delete_map = calloc(cnt->children.num_entries, 1);

            if(!strcmp(id, BG_PLAYQUEUE_ID))
              sink = c->player_ctrl.cmd_sink;
            else
              sink = c->mdb_ctrl.cmd_sink;
            
            for(i = 0; i < cnt->children.num_entries; i++)
              {
              const gavl_dictionary_t * dict;
              
              if((dict = gavl_value_get_dictionary(&cnt->children.entries[i])) &&
                 (id = gavl_track_get_id(dict)) &&
                 (idx = gavl_string_array_indexof(&arr, id) >= 0))
                delete_map[i] = 1;
              }

            /* Delete children */
            delete_start = delete_map;
            idx = 0;
            
            while(1)
              {
              if(!(delete_start = memchr(delete_start, 1, cnt->children.num_entries - (delete_start - delete_map) )))
                break;

              if(!(delete_end = memchr(delete_start, 0, cnt->children.num_entries - (delete_start - delete_map) )))
                delete_end = delete_map + cnt->children.num_entries;

              cmd = bg_msg_sink_get(sink);

              bg_msg_set_splice_children(cmd, BG_CMD_DB_SPLICE_CHILDREN, parent_id, 1,
                                         (delete_start - delete_map) - num_deleted,
                                         delete_end - delete_start, NULL);

              bg_msg_sink_put(sink);
              
              num_deleted += (delete_end - delete_start);
              
              if(delete_end - delete_map >= cnt->children.num_entries)
                break;
              
              delete_start = delete_end;
              }
            
            free(delete_map);
            gavl_array_free(&arr);
            }
          break;
        }
      

      break;
    }
  return 1;
  }


bg_mdb_cache_t * bg_mdb_cache_create(bg_controllable_t * mdb_ctrl, 
                                     bg_controllable_t * player_ctrl)
  {
  bg_mdb_cache_t * ret;

  ret = calloc(1, sizeof(*ret));

  ret->timer = gavl_timer_create();
  gavl_timer_start(ret->timer);

  ret->player_ctrl_p = player_ctrl;
  ret->mdb_ctrl_p    = mdb_ctrl;

  bg_control_init(&ret->player_ctrl, bg_msg_sink_create(handle_mdb_message, ret, 0));
  bg_control_init(&ret->mdb_ctrl, bg_msg_sink_create(handle_mdb_message, ret, 0));

  if(mdb_ctrl)
    {
    ret->mdb_ctrl_p = mdb_ctrl;
    bg_controllable_connect(ret->mdb_ctrl_p, &ret->mdb_ctrl);
    }

  if(player_ctrl)
    {
    ret->player_ctrl_p = player_ctrl;
    bg_controllable_connect(ret->player_ctrl_p, &ret->player_ctrl);
    }
  
  bg_controllable_init(&ret->ctrl, 
                       bg_msg_sink_create(handle_gui_message, ret, 1),
                       bg_msg_hub_create(1));

  init_root_container(ret);
  
  return ret;
  }

bg_controllable_t * bg_mdb_cache_get_controllable(bg_mdb_cache_t * cache)
  {
  return &cache->ctrl;
  }

void bg_mdb_cache_set_mdb(bg_mdb_cache_t * cache, bg_controllable_t * mdb_ctrl)
  {
  /* TODO: */
  
  }


void bg_mdb_cache_set_player(bg_mdb_cache_t * cache, bg_controllable_t * player_ctrl)
  {
  gavl_msg_t * msg;

  if(cache->player_ctrl_p)
    {
    bg_controllable_disconnect(cache->player_ctrl_p, &cache->player_ctrl);
    cache->player_ctrl_p = NULL;

    if(!player_ctrl)
      {
      /* TODO: */
      }
    
    }

  if(player_ctrl)
    {
    cache->player_ctrl_p = player_ctrl;
    bg_controllable_connect(cache->player_ctrl_p, &cache->player_ctrl);
    cache->player_ctrl_p = NULL;

    /* Browse object */
    msg = bg_msg_sink_get(cache->player_ctrl.cmd_sink);
  
    gavl_msg_set_id_ns(msg, BG_FUNC_DB_BROWSE_OBJECT, BG_MSG_NS_DB);
    gavl_dictionary_set_string(&msg->header, GAVL_MSG_CONTEXT_ID, BG_PLAYQUEUE_ID);

    bg_msg_sink_put(cache->player_ctrl.cmd_sink);
    
    }
  }


void bg_mdb_cache_destroy(bg_mdb_cache_t * cache)
  {
  int i;
  
  if(cache->mdb_ctrl_p)
    bg_controllable_disconnect(cache->mdb_ctrl_p, &cache->mdb_ctrl);

  if(cache->player_ctrl_p)
    bg_controllable_disconnect(cache->player_ctrl_p, &cache->player_ctrl);
  
  bg_control_cleanup(&cache->player_ctrl);
  bg_control_cleanup(&cache->mdb_ctrl);
  
  bg_controllable_cleanup(&cache->ctrl);

  for(i = 0; i < cache->containers_alloc; i++)
    container_free(&cache->containers[i]);

  if(cache->containers)
    free(cache->containers);
  
  free(cache);
  }

void bg_mdb_cache_ping(bg_mdb_cache_t * cache)
  {
  gavl_time_t cur = gavl_timer_get(cache->timer);
  
  bg_msg_sink_iteration(cache->player_ctrl.evt_sink);
  bg_msg_sink_iteration(cache->mdb_ctrl.evt_sink);

  /* Cleanup unused entries */

  if(cur - cache->last_cleanup > CLEANUP_INTERVAL)
    {
    int i;
    
    i = 0;
    while(i < cache->num_containers)
      {
      if(!(cache->containers[i].flags & CONTAINER_PERSISTENT) &&
         (cache->containers[i].state == STATE_UNUSED) &&
         (cur - cache->containers[i].last_used > UNUSED_TIME))
        {
        container_free(&cache->containers[i]);
        
        if(i < cache->num_containers-1)
          {
          memmove(cache->containers+i,
                  cache->containers+i+1,
                  sizeof(*cache->containers)*(cache->num_containers-i-1));
          }
        cache->num_containers--;
        container_init(&cache->containers[cache->num_containers]);
        }
      else
        i++;
      }
    cache->last_cleanup = cur;
    }
  
  }

const gavl_dictionary_t * bg_mdb_cache_get_object(bg_mdb_cache_t * cache,
                                                  const char * id)
  {
  container_t * cnt;
  char * parent_id;
  const gavl_dictionary_t * ret = NULL;

  parent_id = bg_mdb_get_parent_id(id);

  if((cnt = has_container(cache, parent_id)))
    ret = gavl_get_track_by_id_arr(&cnt->children, id);
  free(parent_id);
  return ret;
  }

gavl_dictionary_t * bg_mdb_cache_get_container(bg_mdb_cache_t * cache,
                                               const char * parent_id, gavl_array_t * ids)
  {
  int i;
  container_t * cnt;
  gavl_dictionary_t * ret = NULL;
  gavl_array_t * tracks;

  if((cnt = has_container(cache, parent_id)))
    {
    ret = gavl_dictionary_create();

    tracks = gavl_get_tracks_nc(ret);
    
    if(ids)
      {
      int idx;
      const char * id;
      
      for(i = 0; i < ids->num_entries; i++)
        {
        if((id = gavl_string_array_get(ids, i)) &&
           ((idx = gavl_get_track_idx_by_id_arr(&cnt->children, id)) >= 0))
          gavl_array_splice_val(tracks, -1, 0, &cnt->children.entries[idx]);
        }
      }
    else
      {
      gavl_array_splice_array(tracks, 0, 0, &cnt->children);
      }
    gavl_track_update_children(ret);
    }
  
  return ret;
  }
