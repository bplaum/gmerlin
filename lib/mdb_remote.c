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

#include <gmerlin/mdb.h>
#include <gmerlin/utils.h>
#include <gmerlin/bggavl.h>

#include <mdb_private.h>
#include <gavl/metatags.h>

#include <gmerlin/translation.h>
#include <gmerlin/log.h>
#define LOG_DOMAIN "mdb.remote"

#include <gmerlin/backend.h>

static int handle_remote_msg(void * priv, gavl_msg_t * msg);

#define SERVER_FLAG_HAS_ROOT_METADATA (1<<0)
#define SERVER_FLAG_HAS_ROOT_CHILDREN (1<<1)

/*
 *  IDs of the servers are /remote-n with n being a 64 bit counter continuoulsy increased every time a
 *  remote device is found
 */

/* Remote gmerlin server backend */
/* Actually this just keeps track of remote devices and routes messages */

typedef struct
  {
  bg_backend_handle_t * bh;
  
  gavl_dictionary_t dev; // Device
  bg_control_t ctrl;
  
  bg_mdb_backend_t * be;
  
  int flags;

  /* We cache the root and it's immediate children
     because we need to filter out the remote folders of the remote server */
  gavl_dictionary_t root;
  
  } remote_server_t; 

typedef struct
  {
  remote_server_t * servers;
  int num_servers;
  int servers_alloc;
  
  int64_t server_counter;
  } remote_priv_t;

#if 0
static void dump_servers(remote_priv_t * priv)
  {
  int i;
  fprintf(stderr, "SERVERS\n");
  for(i = 0; i < priv->num_servers; i++)
    {
    fprintf(stderr, "  DEV\n");
    gavl_dictionary_dump(&priv->servers[i].dev, 2);
    fprintf(stderr, "  ROOT\n");
    gavl_dictionary_dump(&priv->servers[i].root, 2);
    }
  }
#endif

#if 0
static const char * get_server_label(remote_server_t * srv)
  {
  //  gavl_dictionary_dump(&srv->root, 2);
  
  return gavl_dictionary_get_string(gavl_track_get_metadata(&srv->root), GAVL_META_LABEL);
  }
#endif

static char * make_id(remote_priv_t * priv)
  {
  return bg_sprintf("/remote-%"PRId64, ++priv->server_counter);
  }

static char * id_local_to_remote(remote_priv_t * priv, int * server_idx, const char * local_id)
  {
  int i;
  const char * var;
  int len;
  for(i = 0; i < priv->num_servers; i++)
    {
    if((var = gavl_dictionary_get_string(&priv->servers[i].dev, GAVL_META_ID)) &&
       (len = strlen(var)) &&
       gavl_string_starts_with(local_id, var))
      {
      if(local_id[len] == '/')
        {
        if(server_idx)
          *server_idx = i;
        return gavl_strdup(local_id + len);
        }
      else if(local_id[len] == '\0')
        {
        if(server_idx)
          *server_idx = i;
        return gavl_strdup("/");
        }
      }
    }
  return NULL;
  }

static char * id_remote_to_local(const remote_server_t * s, const char * remote_id)
  {
  const char * var;

  if(!(var = gavl_dictionary_get_string(&s->dev, GAVL_META_ID)))
    return NULL;
  else if(!strcmp(remote_id, "/"))
    return gavl_strdup(var);
  else
    return bg_sprintf("%s%s", var, remote_id);
  }

/* remote_to_local_array */

typedef struct
  {
  gavl_dictionary_t * dict;
  const remote_server_t * s;
  
  } remote_to_local_array_t;

static void foreach_func_arr(void * priv, const char * name,
                             const gavl_value_t * val)
  {
  char * tmp_name;
  remote_to_local_array_t * a = priv;

  if(gavl_string_ends_with(name, "Container"))
    return;

  tmp_name = bg_sprintf("%sContainer", name);

  if(gavl_dictionary_get(a->dict, tmp_name))
    {
    int i;
    gavl_value_t * val;
    const char * id;
    char * new_id;
    
    i = 0;

    while((val = gavl_dictionary_get_item_nc(a->dict, tmp_name, i)))
      {
      if((id = gavl_value_get_string(val)))
        {
        new_id = id_remote_to_local(a->s, id);
        gavl_value_set_string_nocopy(val, new_id);
        }
      
      i++;
      }
    }
  
  free(tmp_name);

  return;
  }

static void value_remote_to_local(const remote_server_t * s, gavl_value_t * val)
  {
  switch(val->type)
    {
    case GAVL_TYPE_DICTIONARY:
      {
      const char * remote_id;
      gavl_dictionary_t * dict = gavl_value_get_dictionary_nc(val);

     
      if((dict = gavl_track_get_metadata_nc(dict)))
        {
        remote_to_local_array_t a;
        
        a.dict = dict;
        a.s = s;
        
        if((remote_id = gavl_dictionary_get_string(dict, GAVL_META_ID)))
          gavl_dictionary_set_string_nocopy(dict, GAVL_META_ID, id_remote_to_local(s, remote_id));

        if((remote_id = gavl_dictionary_get_string(dict, GAVL_META_NEXT_ID)))
          gavl_dictionary_set_string_nocopy(dict, GAVL_META_NEXT_ID, id_remote_to_local(s, remote_id));

        if((remote_id = gavl_dictionary_get_string(dict, GAVL_META_PREVIOUS_ID)))
          gavl_dictionary_set_string_nocopy(dict, GAVL_META_PREVIOUS_ID, id_remote_to_local(s, remote_id));


        gavl_dictionary_foreach(dict, foreach_func_arr, &a);
        

        }
      }
      break;
    case GAVL_TYPE_ARRAY:
      {
      int i;
      gavl_array_t * arr = gavl_value_get_array_nc(val);
      for(i = 0; i < arr->num_entries; i++)
        value_remote_to_local(s, &arr->entries[i]);
      }
      break;
    default:
      break;
    }
  }

static void value_local_to_remote(const remote_server_t * s, gavl_value_t * val)
  {
  switch(val->type)
    {
    case GAVL_TYPE_DICTIONARY:
      {
      const char * local_id;
      gavl_dictionary_t * dict = gavl_value_get_dictionary_nc(val);
      
      if((dict = gavl_track_get_metadata_nc(dict)))
        {
        if((local_id = gavl_dictionary_get_string(dict, GAVL_META_ID)))
          gavl_dictionary_set_string_nocopy(dict, GAVL_META_ID,
                                            id_local_to_remote(s->be->priv, NULL, local_id));
        
        if((local_id = gavl_dictionary_get_string(dict, GAVL_META_NEXT_ID)))
          gavl_dictionary_set_string_nocopy(dict, GAVL_META_NEXT_ID,
                                            id_local_to_remote(s->be->priv, NULL, local_id));

        if((local_id = gavl_dictionary_get_string(dict, GAVL_META_PREVIOUS_ID)))
          gavl_dictionary_set_string_nocopy(dict, GAVL_META_PREVIOUS_ID,
                                            id_local_to_remote(s->be->priv, NULL, local_id));
        
        }
      }
      break;
    case GAVL_TYPE_ARRAY:
      {
      int i;
      gavl_array_t * arr = gavl_value_get_array_nc(val);
      for(i = 0; i < arr->num_entries; i++)
        value_local_to_remote(s, &arr->entries[i]);
      }
      break;
    default:
      break;
    }
  }
  
static void msg_remote_to_local(const remote_server_t * s,
                                gavl_msg_t * msg)
  {
  int i;
  for(i = 0; i < msg->num_args; i++)
    value_remote_to_local(s, &msg->args[i]);
  }


static void msg_local_to_remote(const remote_server_t * s,
                                gavl_msg_t * msg)
  {
  int i;
  for(i = 0; i < msg->num_args; i++)
    value_local_to_remote(s, &msg->args[i]);
  }

static void server_free(remote_server_t * s)
  {
  gavl_dictionary_free(&s->dev);

  if(s->bh)
    bg_backend_handle_destroy(s->bh);
  
  bg_control_cleanup(&s->ctrl);
  gavl_dictionary_free(&s->root);

  memset(s, 0, sizeof(*s));
  
  }

static void remove_remote_children(gavl_dictionary_t * root)
  {
  int i, num;
  const gavl_dictionary_t * dict;
  const char * klass;
  
  num = gavl_get_num_tracks(root);

  for(i = 0; i < num; i++)
    {
    if((dict = gavl_get_track(root, i)) &&
       (dict = gavl_track_get_metadata(dict)) &&
       (klass = gavl_dictionary_get_string(dict, GAVL_META_MEDIA_CLASS)))
      {
      if(!strcmp(klass, GAVL_META_MEDIA_CLASS_ROOT_SERVER))
        {
        /* This and remaining entries need to be deleted */
        gavl_track_splice_children(root, i, num - i, NULL);
        break;
        }
      }
    }
  }

static int handle_remote_msg(void * priv, gavl_msg_t * msg)
  {
  remote_server_t * s = priv;
  gavl_msg_t * msg1;
  const char * remote_id;

  //  fprintf(stderr, "Handle remote message\n");
  //  gavl_msg_dump(msg, 2);
  
#if 0  
  if((msg->NS == BG_MSG_NS_DB) &&
     ((msg->ID == BG_RESP_DB_BROWSE_OBJECT)))
    {
    fprintf(stderr, "remote BG_RESP_DB_BROWSE_OBJECT\n");
    gavl_msg_dump(msg, 2);
    }
  
  if((msg->NS == BG_MSG_NS_DB) &&
     ((msg->ID == BG_RESP_DB_BROWSE_CHILDREN)))
    {
    fprintf(stderr, "remote BG_RESP_DB_BROWSE_CHILDREN\n");
    gavl_msg_dump(msg, 2);
    }
#endif
  
  switch(msg->NS)
    {
    case BG_MSG_NS_DB:
      {
      switch(msg->ID)
        {
        case BG_MSG_DB_SPLICE_CHILDREN:
          {
          int num_children;
          const char * klass;
          const gavl_dictionary_t * dict;

          //          fprintf(stderr, "remote BG_MSG_DB_SPLICE_CHILDREN MSG\n");
          //          gavl_msg_dump(msg, 2);
          
          remote_id = gavl_dictionary_get_string(&msg->header, GAVL_MSG_CONTEXT_ID);

          /* Remote objects of remote objects are ignored */
          if(remote_id && gavl_string_starts_with(remote_id, "/remote"))
            return 1;
          
          if(!strcmp(remote_id, "/"))
            {
            int last;
            int idx = 0;
            int del = 0;
            gavl_value_t add;
            char * local_id;
            
            gavl_value_init(&add);
            
            gavl_msg_get_splice_children(msg, &last, &idx, &del, &add);
            value_remote_to_local(s, &add);

            
            num_children = gavl_get_num_tracks(&s->root);
          
            /* Affects only remote servers */
            if(idx > num_children)
              return 1;
            else if(idx + del >= num_children)
              del = num_children - idx;

            if(add.type == GAVL_TYPE_DICTIONARY)
              {
              if((dict = gavl_value_get_dictionary(&add)) &&
                 (dict = gavl_track_get_metadata(dict)) &&
                 (klass = gavl_dictionary_get_string(dict, GAVL_META_MEDIA_CLASS)) &&
                 !strcmp(klass, GAVL_META_MEDIA_CLASS_ROOT_SERVER))
                {
                gavl_value_reset(&add);
                }
              }
            else if(add.type == GAVL_TYPE_ARRAY)
              {
              int i;
              gavl_array_t * arr = gavl_value_get_array_nc(&add);

              for(i = 0; i < arr->num_entries; i++)
                {
                if((dict = gavl_value_get_dictionary(&arr->entries[i])) &&
                   (dict = gavl_track_get_metadata(dict)) &&
                   (klass = gavl_dictionary_get_string(dict, GAVL_META_MEDIA_CLASS)) &&
                   !strcmp(klass, GAVL_META_MEDIA_CLASS_ROOT_SERVER))
                  {
                  /* Discard rest of the array */
                  gavl_array_splice_val(arr, i, -1, NULL);
                  break;
                  }
                }
              if(!arr->num_entries)
                gavl_value_reset(&add);
              }
          
            if(!del && !add.type)
              return 1;

            // fprintf(stderr, "remote BG_MSG_DB_SPLICE_CHILDREN ROOT 2 (%d %d %d)\n", idx, del, add.type);
            // gavl_dictionary_dump(&s->root, 2);
            
            gavl_track_splice_children(&s->root, idx, del, &add);

            // fprintf(stderr, "remote BG_MSG_DB_SPLICE_CHILDREN ROOT 3\n");
            // gavl_dictionary_dump(&s->root, 2);
            
            /* Send message to core */
            msg1 = bg_msg_sink_get(s->be->ctrl.evt_sink);

            local_id = id_remote_to_local(s, remote_id);
            
            bg_msg_set_splice_children(msg1, BG_MSG_DB_SPLICE_CHILDREN, local_id, last, idx, del, &add);
            bg_msg_sink_put(s->be->ctrl.evt_sink, msg1);
            gavl_value_free(&add);

            free(local_id);
            
            return 1;
            }
          
          }
          break;
        case BG_MSG_DB_OBJECT_CHANGED:
          {
          const char * id = gavl_dictionary_get_string(&msg->header, GAVL_MSG_CONTEXT_ID);
          /* Filter out remote objects */
          if(id && gavl_string_starts_with(id, "/remote"))
            return 1;
          }
          break;
        case BG_RESP_DB_BROWSE_OBJECT:
          
          if(!(s->flags & SERVER_FLAG_HAS_ROOT_METADATA))
            {
            gavl_value_t * arg_val;
            
            gavl_dictionary_t * root_m;

            if((arg_val = gavl_msg_get_arg_nc(msg, 0)))
              value_remote_to_local(s, arg_val);
            
            if(!gavl_msg_get_arg_dictionary_c(msg, 0, &s->root) ||
               !(root_m = gavl_track_get_metadata_nc(&s->root)))
              return 1;

            // fprintf(stderr, "mdb_remote: BG_RESP_DB_BROWSE_OBJECT %s\n", gavl_dictionary_get_string(root_m, GAVL_META_LABEL) );
            //         gavl_dictionary_dump(root_m, 2);
            
            /* Correct some fields */
            gavl_dictionary_set_string(root_m,
                                       GAVL_META_MEDIA_CLASS,
                                       GAVL_META_MEDIA_CLASS_ROOT_SERVER);
            gavl_dictionary_set(root_m, GAVL_META_ID, gavl_dictionary_get(&s->dev,GAVL_META_ID));
            bg_mdb_container_set_backend(&s->root, MDB_BACKEND_REMOTE);
            
            /* Initial root metadata */
            s->flags |= SERVER_FLAG_HAS_ROOT_METADATA;

            /* Browse root children */
            msg1 = bg_msg_sink_get(s->ctrl.cmd_sink);
            gavl_msg_set_id_ns(msg1, BG_FUNC_DB_BROWSE_CHILDREN, BG_MSG_NS_DB);
            gavl_dictionary_set_string(&msg1->header, GAVL_MSG_CONTEXT_ID, "/");
            bg_msg_sink_put(s->ctrl.cmd_sink, msg1);

            
            
            /* Don't forward */
            return 1;
            }
          break;
        case BG_RESP_DB_BROWSE_CHILDREN:

          //          fprintf(stderr, "mdb_remote: BG_RESP_DB_BROWSE_CHILDREN %s\n", get_server_label(s));
          
          if(!(s->flags & SERVER_FLAG_HAS_ROOT_CHILDREN))
            {
            int last;
            int idx;
            int del;
            gavl_value_t add;
            gavl_value_init(&add);
            
            gavl_msg_get_splice_children(msg, &last, &idx, &del, &add);
            
            value_remote_to_local(s, &add);
            gavl_track_splice_children(&s->root, idx, del, &add);
            
            if(last)
              {
              gavl_dictionary_t obj;
              s->flags |= SERVER_FLAG_HAS_ROOT_CHILDREN;
              
              /*
               *  Remove remote servers of the remote server.
               *  We use the fact, that remote servers ALWAYS appear at the end of the root children
               */
              
              remove_remote_children(&s->root);

              
              /* Send message to core */
              
              gavl_dictionary_init(&obj);
              gavl_dictionary_copy(&obj, &s->root);
              gavl_dictionary_set(&obj, GAVL_META_CHILDREN, NULL);

#if 0
              fprintf(stderr, "Got remote server %s\n",
                      gavl_dictionary_get_string(gavl_track_get_metadata(&s->root), 
                                                 GAVL_META_LABEL));
              
              //  gavl_dictionary_dump(&s->root, 2);
              
              //   gavl_dictionary_dump(&s->root, 2);

              //              fprintf(stderr, "Got remote server 2:\n");
              //              gavl_dictionary_dump(&obj, 2);
#endif
              bg_mdb_add_root_container(s->be->ctrl.evt_sink, &obj);
                
              gavl_dictionary_free(&obj);

              }
            gavl_value_free(&add);
            
            return 1;
            }
          break;
        }

      /* Forward to frontend with changed ID */

      msg1 = bg_msg_sink_get(s->be->ctrl.evt_sink);

      gavl_msg_copy(msg1, msg);

      if((remote_id = gavl_dictionary_get_string(&msg->header, GAVL_MSG_CONTEXT_ID)))
        gavl_dictionary_set_string_nocopy(&msg1->header, GAVL_MSG_CONTEXT_ID,
                                          id_remote_to_local(s, remote_id));
      
      msg_remote_to_local(s, msg1);

      // fprintf(stderr, "Got remote message 2:\n");
      // gavl_msg_dump(res, 2);
      
      bg_msg_sink_put(s->be->ctrl.evt_sink, msg1);
      }
      break;
    }
  
  return 1;
  }

static remote_server_t * add_server(bg_mdb_backend_t * be, const gavl_dictionary_t * dict)
  {
  gavl_msg_t * msg;
  
  const char * http_root_uri = NULL;
  remote_server_t * ret;
  remote_priv_t * p = be->priv;
  bg_backend_handle_t * bh;

  //  fprintf(stderr, "Adding server\n");
  //  gavl_dictionary_dump(dict, 2);
    
  if(!(bh = bg_backend_handle_create(dict, http_root_uri)))
    return NULL;
  
  if(p->num_servers >= p->servers_alloc)
    {
    p->servers_alloc = p->num_servers + 16;
    p->servers = realloc(p->servers, p->servers_alloc * sizeof(*p->servers));
    memset(p->servers + p->num_servers, 0,
           sizeof(*p->servers) * (p->servers_alloc - p->num_servers));
    }
  
  ret = p->servers + p->num_servers;
  
  /* Initialize */
  gavl_dictionary_copy(&ret->dev, dict);
  
  /* Generate ID */
  gavl_dictionary_set_string_nocopy(&ret->dev, GAVL_META_ID, make_id(p));
  
  ret->bh = bh;
  ret->be = be;

  bg_control_init(&ret->ctrl,
                  bg_msg_sink_create(handle_remote_msg, ret, 0));
  
  bg_controllable_connect(bg_backend_handle_get_controllable(ret->bh), &ret->ctrl);
  
  /* Request root metadata */
  msg = bg_msg_sink_get(ret->ctrl.cmd_sink);
  gavl_msg_set_id_ns(msg, BG_FUNC_DB_BROWSE_OBJECT, BG_MSG_NS_DB);
  gavl_dictionary_set_string(&msg->header, GAVL_MSG_CONTEXT_ID, "/");
  bg_msg_sink_put(ret->ctrl.cmd_sink, msg);
  
  p->num_servers++;
  return ret;
  }

static void remove_server_by_idx(bg_mdb_backend_t * be, int i)
  {
  remote_priv_t * p = be->priv;
  
  /* Send message */
    
  bg_mdb_delete_root_container(be->ctrl.evt_sink, gavl_dictionary_get_string(&p->servers[i].dev, GAVL_META_ID));
    
  /* Remove locally */

  server_free(&p->servers[i]);
    
  if(i < p->num_servers - 1)
    memmove(p->servers + i, p->servers + (i + 1),
            sizeof(*p->servers) * (p->num_servers - 1 - i));
  
  p->num_servers--;
  memset(&p->servers[p->num_servers], 0, sizeof(p->servers[p->num_servers]));
  }

static void remove_server(bg_mdb_backend_t * be, const char * url)
  {
  int i;
  const char * var;
  remote_priv_t * p = be->priv;
  
  if(!url)
    return;
  
  for(i = 0; i < p->num_servers; i++)
    {
    if(!(var = gavl_dictionary_get_string(&p->servers[i].dev, GAVL_META_URI)) ||
       strcmp(var, url))
      continue;
    
    /* Remove ith server */

    remove_server_by_idx(be, i);
    
    return;
    }
  }

static int handle_local_msg(void * priv, gavl_msg_t * msg)
  {
  bg_mdb_backend_t * be = priv;
  remote_priv_t * p = be->priv;

#if 0
  if((msg->NS == BG_MSG_NS_DB) &&
     ((msg->ID == BG_FUNC_DB_BROWSE_OBJECT)))
    {
    fprintf(stderr, "local BG_FUNC_DB_BROWSE_OBJECT\n");
    gavl_msg_dump(msg, 2);
    }
  
  if((msg->NS == BG_MSG_NS_DB) &&
     ((msg->ID == BG_FUNC_DB_BROWSE_CHILDREN)))
    {
    fprintf(stderr, "local BG_FUNC_DB_BROWSE_CHILDREN\n");
    gavl_msg_dump(msg, 2);
    }
#endif
  
  switch(msg->NS)
    {
    case BG_MSG_NS_DB:
      {
      switch(msg->ID)
        {
        case BG_FUNC_DB_BROWSE_OBJECT:
        case BG_FUNC_DB_BROWSE_CHILDREN:
        case BG_CMD_DB_SPLICE_CHILDREN:
        case BG_CMD_DB_SORT:
        case BG_CMD_DB_SAVE_LOCAL:
          /* Forward to remote server */
          {
          int server_idx = -1;
          char * remote_id = NULL;
          const char * local_id;
          
          if((local_id = gavl_dictionary_get_string(&msg->header, GAVL_MSG_CONTEXT_ID)) &&
             (remote_id = id_local_to_remote(p, &server_idx, local_id)))
            {
            gavl_msg_t * msg1;
            
            //            if(msg->ID == BG_CMD_DB_SAVE_LOCAL)
            //              fprintf(stderr, "BG_CMD_DB_SAVE_LOCAL l: %s r: %s\n", local_id, remote_id);
            
            //            if(msg->ID == BG_FUNC_DB_BROWSE_CHILDREN)
            //              fprintf(stderr, "BG_FUNC_DB_BROWSE_CHILDREN l: %s r: %s\n", local_id, remote_id);
            
            if((msg->ID == BG_FUNC_DB_BROWSE_CHILDREN) &&
               !strcmp(remote_id, "/"))
              {
              int start, num, one_answer;
              
              const gavl_array_t * arr = gavl_get_tracks(&p->servers[server_idx].root);

              bg_mdb_get_browse_children_request(msg, NULL, &start, &num, &one_answer);
              
              if(!bg_mdb_adjust_num(start, &num, arr->num_entries))
                return 1;

              msg1 = bg_msg_sink_get(be->ctrl.evt_sink);           
              
              if(num < arr->num_entries)
                {
                int i;
                gavl_array_t tmp_arr;
                gavl_array_init(&tmp_arr);
                
                /* Range */
                
                for(i = 0; i < num; i++)
                  gavl_array_splice_val(&tmp_arr, i, 0, &arr->entries[i+start]);
                
                bg_mdb_set_browse_children_response(msg1, &tmp_arr, msg, &start, 1, arr->num_entries);
                gavl_array_free(&tmp_arr);
                }
              else
                bg_mdb_set_browse_children_response(msg1, arr, msg, &start, 1, arr->num_entries);
              
              //              gavl_msg_dump(msg1, 2);

              //              fprintf(stderr, "Sending root response 2 %d:\n", server_idx);
              //              gavl_dictionary_dump(&p->servers[server_idx].root, 2);
              
              bg_msg_sink_put(be->ctrl.evt_sink, msg1);
              }
            else
              {
              msg1 = bg_msg_sink_get(p->servers[server_idx].ctrl.cmd_sink);
              gavl_msg_copy(msg1, msg);
              gavl_dictionary_set_string(&msg1->header, GAVL_MSG_CONTEXT_ID, remote_id);
              msg_local_to_remote(&p->servers[server_idx], msg1);
              bg_msg_sink_put(p->servers[server_idx].ctrl.cmd_sink, msg1);
              }
            
            }
          //          else
          //            fprintf(stderr, "BG_FUNC_DB_BROWSE_CHILDREN failed: local: %s r: %s\n",
          //                    local_id, remote_id);
          
          if(remote_id)
            free(remote_id);
          }
          break;
        }
      }
      break;
    case BG_MSG_NS_BACKEND:
      {
      switch(msg->ID)
        {
        case BG_MSG_ADD_BACKEND:
          {
          int type = BG_BACKEND_NONE;
          gavl_dictionary_t dict;
          gavl_dictionary_init(&dict);
          gavl_msg_get_arg_dictionary(msg, 0, &dict);
          
          if(gavl_dictionary_get_int(&dict, BG_BACKEND_TYPE, &type) &&
             (type == BG_BACKEND_MEDIASERVER))
            //            (protocol = gavl_dictionary_get_string(&dict, BG_BACKEND_PROTOCOL)) &&
            //              !strcmp(protocol, "gmerlin"))
            {
            //            fprintf(stderr, "Add remote device %s\n", gavl_dictionary_get_string(&dict, GAVL_META_LABEL));
            add_server(be, &dict);
            }
          gavl_dictionary_free(&dict);
          }
          break;
        case BG_MSG_DEL_BACKEND:
          //          fprintf(stderr, "Delete remote device %s\n", gavl_msg_get_arg_string_c(msg, 0));
          remove_server(be, gavl_msg_get_arg_string_c(msg, 0));
          break;
        }
      }
      break;
    case GAVL_MSG_NS_GENERIC:
      {
      switch(msg->ID)
        {
        case GAVL_CMD_QUIT:
          return 0;
          break;
        }
      break;
      }
    }
  return 1;
  }

static int ping_func_remote(bg_mdb_backend_t * b)
  {
  int i = 0;
  int ret = 0;
  int result;

  remote_priv_t * priv = b->priv;

  while(i < priv->num_servers)
    {

    bg_msg_sink_iteration(priv->servers[i].ctrl.evt_sink);
    ret += bg_msg_sink_get_num(priv->servers[i].ctrl.evt_sink);

    if(priv->servers[i].bh)
      {
      result = bg_backend_handle_ping(priv->servers[i].bh);

      // fprintf(stderr, "bg_backend_handle_ping: %d\n", result);
      
      if(result < 0)
        {
        //  fprintf(stderr, "Removing server\n");
        
        remove_server_by_idx(b, i);
        ret++;
        }
      else
        {
        ret += result;
        i++;
        }
      }
    else
      i++;
    
    //    fprintf(stderr, "Sink it %d %d\n", i, ret);
    
    }
  
  //  fprintf(stderr, "ping_func_remote %d\n", ret);
  return ret;
  }

static void destroy_func_remote(bg_mdb_backend_t * b)
  {
  int i;
  
  remote_priv_t * priv = b->priv;

  if(priv->servers)
    {
    for(i = 0; i < priv->num_servers; i++)
      server_free(&priv->servers[i]);
    free(priv->servers);
    }
  free(priv);
  }

void bg_mdb_create_remote(bg_mdb_backend_t * b)
  {
  remote_priv_t * priv;
  
  b->flags |= BE_FLAG_REMOTE;
  b->ping_func    = ping_func_remote;
  b->destroy = destroy_func_remote;
  
  priv = calloc(1, sizeof(*priv));
  b->priv = priv;
  
  bg_controllable_init(&b->ctrl,
                       bg_msg_sink_create(handle_local_msg, b, 0),
                       bg_msg_hub_create(1));

  
  
  }
