/*****************************************************************
 * gmerlin - a general purpose multimedia framework and applications
 *
 * Copright (c) 2001 - 2012 Members of the Gmerlin project
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
#include <unistd.h>
#include <errno.h>
#include <fnmatch.h>
#include <ctype.h>

#include <config.h>

#include <gavl/metatags.h>

#include <gmerlin/mdb.h>
#include <gmerlin/utils.h>
#include <gmerlin/translation.h>
#include <gmerlin/log.h>
#include <gmerlin/bggavl.h>
#include <gmerlin/state.h>

#include <gmerlin/httpserver.h>
#include <gmerlin/cfgctx.h>

#include <httpserver_priv.h>

#define LOG_DOMAIN "mdb"

#include <mdb_private.h>

#define MDB_DIR    "gmerlin-mdb"
#define STATE_FILE "state.xml"
#define STATE_ROOT "mdb_state"

#define GENERAL_CTX "$general"

static const struct
  {
  void (*create_func)(bg_mdb_backend_t * b);
  const char * name;
  const char * long_name;
  }
backends[] =
  {
    { bg_mdb_create_filesystem,    MDB_BACKEND_FILESYSTEM,    "Filesystem"   },
    { bg_mdb_create_sqlite,        MDB_BACKEND_SQLITE,        "SQLite"       },
    { bg_mdb_create_xml,           MDB_BACKEND_XML,           "XML"          },
    { bg_mdb_create_remote,        MDB_BACKEND_REMOTE,        "Remote"       },
    //    { bg_mdb_create_radio_browser, MDB_BACKEND_RADIO_BROWSER, "Radiobrowser" },
    { bg_mdb_create_podcasts,      MDB_BACKEND_PODCASTS,      "Podcasts"     },
    { bg_mdb_create_streams,       MDB_BACKEND_STREAMS,       "Streams"      },
  };

#define num_backends (sizeof(backends)/sizeof(backends[0]))

static int get_klass_idx(const char * klass);
static void finalize(gavl_dictionary_t * dict, int idx, int total);


static bg_mdb_backend_t * be_from_name(bg_mdb_t * db, const char * name)
  {
  int i;
  for(i = 0; i < num_backends; i++)
    {
    if(!strcmp(name, backends[i].name))
      {
      return &db->backends[i];
      break;
      }
    }
  return NULL;
  }
  
/* Handle command from frontend */

static gavl_dictionary_t * child_by_id(gavl_dictionary_t * parent,
                                       const char * id, const char * id_end, const char ** id_real)
  {
  int i, num;
  
  num = gavl_get_num_tracks(parent);

  for(i = 0; i < num; i++)
    {
    gavl_dictionary_t * child;
    const gavl_dictionary_t * m;
    const char * track_id;

    if((child = gavl_get_track_nc(parent, i)) &&
       (m = gavl_track_get_metadata(child)) &&
       (track_id = gavl_dictionary_get_string(m, GAVL_META_ID)) &&
       (strlen(track_id) == (id_end - id)) &&
       !strncmp(track_id, id, id_end - id))
      {
      *id_real = track_id;
      return child;
      }
    }
  return NULL;
  }

static gavl_dictionary_t *
container_by_id(bg_mdb_t * db, const char * id, const char ** id_real)
  {
  const char * end;
  const char * pos;
  gavl_dictionary_t * ret;

  ret = &db->root;

  *id_real = NULL;
  
  if(!strcmp(id, "/"))
    {
    *id_real = id;
    return ret;
    }
  end = id;

  while(1)
    {
    pos = strchr(end+1, '/');

    /* Leaf element */
    if(!pos)
      pos = end + strlen(end);

    end = pos;
    
    ret = child_by_id(ret, id, end, id_real);

    if(!ret)
      return NULL;
    
    if((*end == '\0') || bg_mdb_container_get_backend(ret))
      break;
    }
  
  return ret;
  }



void bg_mdb_set_browse_obj_response(gavl_msg_t * res, const gavl_dictionary_t * obj,
                                    const gavl_msg_t * cmd, int idx, int total)
  {
  gavl_value_t val;
  gavl_dictionary_t * dict;
  
  gavl_msg_set_id_ns(res, BG_RESP_DB_BROWSE_OBJECT, BG_MSG_NS_DB);
  
  gavl_value_init(&val);
  dict = gavl_value_set_dictionary(&val);
  gavl_dictionary_copy(dict, obj);
  
  finalize(dict, idx, total);
  gavl_msg_set_arg_nocopy(res, 0, &val);
  gavl_value_free(&val);

  gavl_msg_set_resp_for_req(res, cmd);
  
  }

void bg_mdb_set_browse_children_response(gavl_msg_t * res, const gavl_array_t * children,
                                         const gavl_msg_t * cmd, int * idx, int last, int total)
  {
  int i;
  gavl_value_t val;
  gavl_array_t * arr;
  gavl_dictionary_t * dict;
  
  gavl_value_init(&val);
  arr = gavl_value_set_array(&val);
  gavl_array_copy(arr, children);

  //  fprintf(stderr, "bg_mdb_set_browse_children_response %d\n", total);
  //  gavl_array_dump(children, 2);
  
  for(i = 0; i < arr->num_entries; i++)
    {
    if((dict = gavl_value_get_dictionary_nc(&arr->entries[i])))
      {
      finalize(dict, i + (*idx), total);
      }
    }

  gavl_msg_set_splice_children_nocopy(res, BG_MSG_NS_DB, BG_RESP_DB_BROWSE_CHILDREN,
                                      NULL,
                                      last, *idx, 0, &val);
  
  (*idx) += children->num_entries;

  gavl_msg_set_resp_for_req(res, cmd);
  }

void bg_mdb_set_browse_children_request(gavl_msg_t * msg, const char * id,
                                        int start, int num, int one_answer)
  {
  gavl_msg_set_id_ns(msg, BG_FUNC_DB_BROWSE_CHILDREN, BG_MSG_NS_DB);
  
  gavl_dictionary_set_string(&msg->header, GAVL_MSG_CONTEXT_ID, id);

  if((start > 0) || (num >= 0) ||  one_answer)
    {
    gavl_msg_set_arg_int(msg, 0, start);

    if((num >= 0) || one_answer)
      gavl_msg_set_arg_int(msg, 1, num);

    if(one_answer)
      gavl_msg_set_arg_int(msg, 2, 1);
    }
  }

void bg_mdb_get_browse_children_request(const gavl_msg_t * req, const char ** id,
                                        int * start, int * num, int * one_answer)
  {
  if(id)
    *id = gavl_dictionary_get_string(&req->header, GAVL_MSG_CONTEXT_ID);

  if(start)
    *start = 0;

  if(num)
    *num = -1;

  if(one_answer)
    *one_answer = 0;
  
  if((req->num_args > 0) && start)
    {
    *start = gavl_msg_get_arg_int(req, 0);
    if(*start < 0)
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Start index negative");
      gavl_msg_dump(req, 2);
      *start = 0;
      }
    }
  if((req->num_args > 1) && num)
    *num = gavl_msg_get_arg_int(req, 1);

  if((req->num_args > 2) && one_answer)
    *one_answer = gavl_msg_get_arg_int(req, 2);
  
  }


static void clear_gui_state(gavl_value_t * val)
  {
  if(!val)
    return;
  switch(val->type)
    {
    case GAVL_TYPE_DICTIONARY:
      gavl_track_clear_gui_state(gavl_value_get_dictionary_nc(val));
      break;
    case GAVL_TYPE_ARRAY:
      {
      int i;
      gavl_dictionary_t * d;
      gavl_array_t * a;
      a = gavl_value_get_array_nc(val);
      for(i = 0; i < a->num_entries; i++)
        {
        if((d = gavl_value_get_dictionary_nc(&a->entries[i])))
          gavl_track_clear_gui_state(d);
        }
      }
      break;
    default:
      break;
    }
  }

static bg_parameter_info_t parameters[] =
  {
    {
      .name = GENERAL_CTX,
      .long_name = TRS("General"),
      .type = BG_PARAMETER_SECTION,
    },
    {
      .name = "rescan",
      .long_name = TRS("Rescan"),
      .type = BG_PARAMETER_BUTTON,
      
    },
    { /* End */ }
  };

bg_cfg_ctx_t * bg_mdb_get_cfg(bg_mdb_t * db)
  {
  return db->cfg_ext;
  }

static void add_volume_func(void * priv, const char * name, const gavl_value_t * val)
  {
  const gavl_dictionary_t * dict;
  int i;
  gavl_msg_t msg;
  bg_mdb_t * db = priv;

  if(!(dict = gavl_value_get_dictionary(val)))
    return;
  
  gavl_msg_init(&msg);
  
  gavl_msg_set_id_ns(&msg, BG_MSG_ID_VOLUME_ADDED, BG_MSG_NS_VOLUMEMANAGER);
  gavl_msg_set_arg_string(&msg, 0, name);
  gavl_msg_set_arg_dictionary(&msg, 1, dict);
  
  /* Forward to backends */

      //   fprintf(stderr, "Got remote msg\n");
              
  for(i = 0; i < num_backends; i++)
    {
    if(db->backends[i].flags & BE_FLAG_VOLUMES)
      bg_msg_sink_put_copy(db->backends[i].ctrl.cmd_sink, &msg);
    }
  gavl_msg_free(&msg);
  }

static int is_us(bg_mdb_t * db, const char * url)
  {
  int ret = 0;
  const char * root_url;

  int port1 = 0;
  int port2 = 0;
  char * host1 = NULL;
  char * host2 = NULL;
  
  if(!db->srv || !(root_url = bg_http_server_get_root_url(db->srv)))
    return 0;

  // fprintf(stderr, "is us: %s %s\n", url, root_url);
  
  if(bg_url_split(url, NULL, NULL, NULL, &host1, &port1, NULL) &&
     bg_url_split(root_url, NULL, NULL, NULL, &host2, &port2, NULL) &&
     !strcmp(host1, host2) &&
     (port1 == port2))
    ret = 1;
  
  if(host1)
    free(host1);
  if(host2)
    free(host2);
  
  return ret;
  }

static void update_remote_devs_state(bg_mdb_t * db)
  {
  int i;
  const char * protocol;

  const gavl_dictionary_t * dict;
  
  gavl_value_t state_val;
  gavl_array_t * devs;

  /* Update state */
  devs = bg_backend_registry_get();

  i = 0;
  
  /* Remove non-gmerlin devs */
  while(i < devs->num_entries)
    {
    if((dict = gavl_value_get_dictionary(&devs->entries[i])) &&
       (protocol = gavl_dictionary_get_string(dict, BG_BACKEND_PROTOCOL)) &&
       strcmp(protocol, "gmerlin"))
      gavl_array_splice_val(devs, i, 1, NULL);
    else
      i++;
    }
    
  gavl_value_init(&state_val);
  gavl_value_set_array_nocopy(&state_val, devs);

  //  fprintf(stderr, "update_remote_devs_state %p\n", db->ctrl.evt_sink);
  
  bg_state_set(NULL, 1, "mdb", "remotedevs", &state_val, db->ctrl.evt_sink, BG_MSG_STATE_CHANGED);
  gavl_value_free(&state_val);


  }

static int handle_cmd(void * priv, gavl_msg_t * msg)
  {
  const char * be_name;
  const gavl_dictionary_t * obj;
  
  bg_mdb_t * db = priv;
  bg_mdb_backend_t * be = NULL;

  const char * real_id;

  //  fprintf(stderr, "mdb: Got command:\n");
  //  gavl_msg_dump(msg, 2);

#if 0  
  /* messages sent directly to the backends */
  if((be_name = bg_mdb_msg_get_backend(msg)) &&
     (be = be_from_name(db, be_name)))
    {
    bg_msg_sink_put_copy(be->ctrl.cmd_sink, msg);
    return 1;
    }
#endif
  
  /* Messages to be handled by the core */
  switch(msg->NS)
    {
    case BG_MSG_NS_DB:
      {
      switch(msg->ID)
        {
        case BG_FUNC_DB_RESCAN:
          {
          int i;

          if(db->rescan_func)
            {
            gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Re-scan already in process");
            return 1;
            }

          db->rescan_func = gavl_msg_create();
          gavl_msg_copy(db->rescan_func, msg);
          
          for(i = 0; i < num_backends; i++)
            {
            if(db->backends[i].flags & BE_FLAG_RESCAN)
              {
              bg_msg_sink_put_copy(db->backends[i].ctrl.cmd_sink, msg);
              db->num_rescan++;
              //          fprintf(stderr, "Put remote msg %d\n", i);
              }
            }
          bg_mdb_purge_thumbnails(db);
          
          }
          break;
        case BG_FUNC_DB_ADD_SQL_DIR:
        case BG_FUNC_DB_DEL_SQL_DIR:
          /* Forward to sql backend */
          be = be_from_name(db, MDB_BACKEND_SQLITE);
          break;
        case BG_CMD_DB_SAVE_LOCAL:
          {
          const char * ctx_id;
          
          if(!(ctx_id = gavl_dictionary_get_string(&msg->header, GAVL_MSG_CONTEXT_ID)))
            return 1;

          if(!(obj = container_by_id(db, ctx_id, &real_id)))
            gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Object %s not found", ctx_id);
          else
            {
            if((be_name = bg_mdb_container_get_backend(obj)))
              be = be_from_name(db, be_name);
            }
          
          }
          break;
        case BG_CMD_DB_SPLICE_CHILDREN:
          {
          const char * ctx_id;
          
          if(!(ctx_id = gavl_dictionary_get_string(&msg->header, GAVL_MSG_CONTEXT_ID)))
            return 1;
          
          /* Clear GUI State */
          clear_gui_state(gavl_msg_get_arg_nc(msg, 3));
          if(!(obj = container_by_id(db, ctx_id, &real_id)))
            gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Object %s not found", ctx_id);
          else
            {
            if((be_name = bg_mdb_container_get_backend(obj)))
              be = be_from_name(db, be_name);
            }
          }
          break;
        case BG_CMD_DB_SORT:
          {
          const char * ctx_id;

          if(!(ctx_id = gavl_dictionary_get_string(&msg->header, GAVL_MSG_CONTEXT_ID)))
            return 1;
          
          if(!(obj = container_by_id(db, ctx_id, &real_id)))
            gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Object %s not found", ctx_id);
          else
            {
            if((be_name = bg_mdb_container_get_backend(obj)))
              be = be_from_name(db, be_name);
            
            //  fprintf(stderr, "mdb sort %s %s\n", ctx_id, be_name);
            }
          }
          break;
        case BG_FUNC_DB_BROWSE_CHILDREN:
          {
          /* Look for cached object */
          const char * ctx_id;
          int start, num, one_answer;

          //          fprintf(stderr, "mdb: BG_FUNC_DB_BROWSE_CHILDREN\n");
          
          bg_mdb_get_browse_children_request(msg, &ctx_id, &start, &num, &one_answer);
          
          if(!ctx_id)
            return 1;
          
          //          fprintf(stderr, "mdb: BG_FUNC_DB_BROWSE_CHILDREN: %s\n", ctx_id);
          //          gavl_msg_dump(msg, 2);
          
          if(!(obj = container_by_id(db, ctx_id, &real_id)))
            {
            gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Object %s not found", ctx_id);
            }
          else if(strcmp(ctx_id, real_id))
            {
            if((be_name = bg_mdb_container_get_backend(obj)))
              be = be_from_name(db, be_name);

            if(!be)
              gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Object %s not found", ctx_id);
            }
          else
            {
            if((be_name = bg_mdb_container_get_backend(obj)))
              be = be_from_name(db, be_name);

            if(!be) // Children are in the root tree 
              {
              const gavl_array_t * arr = gavl_get_tracks(obj);
              gavl_msg_t * res;

              //              fprintf(stderr, "mdb: Got no backend\n");
              //              gavl_dictionary_dump(obj, 2);
              
              if(!bg_mdb_adjust_num(start, &num, arr->num_entries))
                return 1;

              res = bg_msg_sink_get(db->ctrl.evt_sink);
              
              if(num < arr->num_entries)
                {
                int i;
                gavl_array_t tmp_arr;
                gavl_array_init(&tmp_arr);
                
                /* Range */
                
                for(i = 0; i < num; i++)
                  gavl_array_splice_val(&tmp_arr, i, 0, &arr->entries[i+start]);
                
                bg_mdb_set_browse_children_response(res, &tmp_arr, msg, &start, 1, arr->num_entries);
                gavl_array_free(&tmp_arr);
                }
              else
                bg_mdb_set_browse_children_response(res, arr, msg, &start, 1, arr->num_entries);
              
              bg_msg_sink_put(db->ctrl.evt_sink);
              }
            }
          }
          break;
        case BG_FUNC_DB_BROWSE_OBJECT:
          {
          const char * ctx_id;
          /* Look for cached object */

          //          fprintf(stderr, "mdb: BG_FUNC_DB_BROWSE_OBJECT\n");
          
          if(!(ctx_id = gavl_dictionary_get_string(&msg->header, GAVL_MSG_CONTEXT_ID)))
            return 1;

          //          fprintf(stderr, "mdb: BG_FUNC_DB_BROWSE_OBJECT %s\n", ctx_id);
          
          if(!(obj = container_by_id(db, ctx_id, &real_id)))
            {
            gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Object %s not found", ctx_id);
            }
          else if(strcmp(ctx_id, real_id))
            {
            be_name = bg_mdb_container_get_backend(obj);
            be = be_from_name(db, be_name);
            }
          else
            {
            gavl_msg_t * res = bg_msg_sink_get(db->ctrl.evt_sink);
            bg_mdb_set_browse_obj_response(res, obj, msg, -1, -1);

            // fprintf(stderr, "Sending response %d %d:\n", res->ID, res->NS);
            //         gavl_msg_dump(res, 2);

            bg_msg_sink_put(db->ctrl.evt_sink);
            
            }
          }
          break;
        }
      break;
    case BG_MSG_NS_BACKEND:
      {
      int i;
      /* Forward to backends */
      switch(msg->ID)
        {
        case BG_MSG_ADD_BACKEND:
          {
          const char * var;
          const gavl_value_t * val;
          const gavl_dictionary_t * dict;
          int type;
          update_remote_devs_state(db);
          if((val = gavl_msg_get_arg_c(msg, 0)) &&
             (dict = gavl_value_get_dictionary(val)) &&
             gavl_dictionary_get_int(dict, BG_BACKEND_TYPE, &type) &&
             (type == BG_BACKEND_MEDIASERVER) &&
             (var = gavl_dictionary_get_string(dict, GAVL_META_URI)) &&
             is_us(db, var))
            {
            //  fprintf(stderr, "Not adding %s\n", uri);
            return 1;
            }
          break;
          }
        case BG_MSG_DEL_BACKEND:
          update_remote_devs_state(db);
          break;
        case BG_MSG_BACKENDS_RESCAN:
          bg_backend_registry_rescan();
          break;
        }
      
      //   fprintf(stderr, "Got remote msg\n");
              
      for(i = 0; i < num_backends; i++)
        {
        if(db->backends[i].flags & BE_FLAG_REMOTE)
          bg_msg_sink_put_copy(db->backends[i].ctrl.cmd_sink, msg);
        }
      break;
      }
    case BG_MSG_NS_VOLUMEMANAGER:
      {
      int i;
      /* Forward to backends */

      //   fprintf(stderr, "Got remote msg\n");
              
      for(i = 0; i < num_backends; i++)
        {
        if(db->backends[i].flags & BE_FLAG_VOLUMES)
          {
          bg_msg_sink_put_copy(db->backends[i].ctrl.cmd_sink, msg);
          //          fprintf(stderr, "Put remote msg %d\n", i);
          }
        }
      //      be->flags & BE_FLAG_REMOTE
      
      break;
      }
    case BG_MSG_NS_STATE:
      {
      //      fprintf(stderr, "State changed\n");
      //      gavl_msg_dump(msg, 2);
      
      switch(msg->ID)
        {
        case BG_MSG_STATE_CHANGED:
          {
          int last;
          const char * ctx;
          const char * var;
          gavl_value_t val;
          
          gavl_value_init(&val);
          gavl_msg_get_state(msg, &last, &ctx, &var, &val, NULL);
      
          if(!strcmp(ctx, "volumemanager") && !strcmp(var, "volumes") && !db->volumes_added)
            {
            const gavl_dictionary_t * dict;

            db->volumes_added = 1;
            
            if((dict = gavl_value_get_dictionary(&val)))
              gavl_dictionary_foreach(dict, add_volume_func, db);
            
            }
          gavl_value_free(&val);
          }
        }
      }
      break;
    case GAVL_MSG_NS_GENERIC:
      switch(msg->ID)
        {
        case GAVL_CMD_QUIT:
          return 0;
        }
      break;
    case BG_MSG_NS_PARAMETER:
      switch(msg->ID)
        {
        case BG_MSG_SET_PARAMETER_CTX:
          {
          const bg_parameter_info_t * info;
          const char * ctx = NULL;
          const char * name = NULL;
          gavl_value_t val;
          bg_cfg_ctx_t * c = NULL;
          
          gavl_value_init(&val);
          
          bg_msg_get_parameter_ctx(msg, &ctx, &name, &val);
          
          //          fprintf(stderr, "mdb: BG_MSG_SET_PARAMETER_CTX %s %s\n", ctx, name);
          //          gavl_value_dump(&val, 2);
          //          fprintf(stderr, "\n");

          if(!ctx)
            return 1;
          
          if(!strcmp(ctx, GENERAL_CTX))
            {
            
            if(!name)
              return 1;

            if(!strcmp(name, "rescan"))
              {
              //              fprintf(stderr, "** Rescan **\n");
              bg_mdb_rescan(&db->ctrl);
              }
            else
              return 1;
            
            /* Store value */
            
            if((info = bg_cfg_ctx_find_parameter(db->cfg, ctx, name, &c)) &&
               (c->s))
              {
              bg_cfg_section_set_parameter(c->s, info, &val);
              db->cfg_save_time = gavl_timer_get(db->timer) + 3 * GAVL_TIME_SCALE;
              }
            
            }
          else
            {
            if((be = be_from_name(db, ctx)))
              {
              gavl_msg_t * msg1 = bg_msg_sink_get(be->ctrl.cmd_sink);
              gavl_msg_set_id_ns(msg1, BG_MSG_SET_PARAMETER, BG_MSG_NS_PARAMETER);
              bg_msg_set_parameter(msg1, name, &val);
              bg_msg_sink_put(be->ctrl.cmd_sink);
              be = NULL;
              }
            }
          gavl_value_free(&val);
          }
        }
      break;
      }
    }
  
  if(be)
    bg_msg_sink_put_copy(be->ctrl.cmd_sink, msg);
  
  return 1;
  }

static int get_root_index(bg_mdb_t * db, const gavl_dictionary_t * dict)
  {
  int i;
  const gavl_dictionary_t * track;
  const gavl_dictionary_t * m;
  const gavl_dictionary_t * test_m;

  int num;
  int idx;
  int test_idx;
  const char * klass;
  const char * test_klass;

  const char * label;
  const char * test_label;
  
  num = gavl_get_num_tracks(&db->root);

  if(!(m = gavl_track_get_metadata(dict)) ||
     !(klass = gavl_dictionary_get_string(m, GAVL_META_MEDIA_CLASS)) ||
     ((idx = get_klass_idx(klass)) < 0) ||
     !(label = gavl_dictionary_get_string(m, GAVL_META_LABEL)))
    {
    return -1;
    }
  
  for(i = 0; i < num; i++)
    {
    if((track = gavl_get_track(&db->root, i)) &&
       (test_m = gavl_track_get_metadata(track)) &&
       (test_klass = gavl_dictionary_get_string(test_m, GAVL_META_MEDIA_CLASS)) &&
       ((test_idx = get_klass_idx(test_klass)) >= 0))
      {
      if(test_idx < idx)
        continue;
      
      else if(test_idx > idx)
        return i;

      else // test_idx == idx
        {
        /* Compare labels */
        test_label = gavl_dictionary_get_string(test_m, GAVL_META_LABEL);

        if(strcmp(test_label, label) > 0)
          return i;
        }
      }
    }
  
  return i;
  }

/* Handle message from backend */

static int handle_be_msg(void * priv, gavl_msg_t * msg)
  {
  int i;
  gavl_value_t * arg_val;
  gavl_array_t * arg_arr;
  gavl_dictionary_t * arg_dict;
  
  bg_mdb_t * db = priv;

  int do_forward = 1;
  
#if 0
  fprintf(stderr, "handle_be_msg\n");
  gavl_msg_dump(msg, 2);
#endif
  
  switch(msg->NS)
    {
    case BG_MSG_NS_DB:
      {
      /* Store changes in the root directories */
      switch(msg->ID)
        {
        case BG_MSG_DB_CREATION_DONE:
          db->num_create--;
          if(!db->num_create)
            {
            bg_msg_sink_put_copy(db->ctrl.evt_sink, msg);
            gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Creation done");
            }
          break;
        case BG_RESP_DB_RESCAN:
          db->num_rescan--;

          /* Send to the outer world if this was the last one */
          if(!db->num_rescan)
             {
             gavl_msg_t * res;
             res = bg_msg_sink_get(db->ctrl.evt_sink);
             gavl_msg_set_id_ns(res, BG_RESP_DB_RESCAN, BG_MSG_NS_DB);

             gavl_msg_set_resp_for_req(res, db->rescan_func);
             gavl_msg_destroy(db->rescan_func);
             db->rescan_func = NULL;
             
             bg_msg_sink_put(db->ctrl.evt_sink);
             }
          do_forward = 0;
          break;
        case BG_MSG_DB_OBJECT_CHANGED:
          {
          gavl_dictionary_t * dict_root;
          
          const char * id;
          const char * id_real = NULL;
          
          id = gavl_dictionary_get_string(&msg->header, GAVL_MSG_CONTEXT_ID);
          
          if((dict_root = container_by_id(db, id, &id_real)) &&
             id_real &&
             !strcmp(id, id_real))
            {
            gavl_dictionary_t dict_arg;
            gavl_dictionary_t * m_arg;
            gavl_dictionary_t * m_root;

            gavl_dictionary_init(&dict_arg);
            gavl_msg_get_arg_dictionary_c(msg, 0, &dict_arg);

            if((m_root = gavl_track_get_metadata_nc(dict_root)) &&
               (m_arg = gavl_track_get_metadata_nc(&dict_arg)))
              {
              gavl_dictionary_merge2(m_arg, m_root);
              gavl_dictionary_reset(m_root);
              gavl_dictionary_move(m_root, m_arg);
              }
            gavl_dictionary_free(&dict_arg);
            }
          }
          break;
        case BG_MSG_DB_SPLICE_CHILDREN:
        case BG_RESP_DB_BROWSE_CHILDREN:
          /*
           *  Compatible with splice for simpler frontends
           *
           *  ContextID: album_id
           *  arg0: idx        (int)
           *  arg1: num_delete (int) (always zero)
           *  arg2: new_tracks (array)
           */
          if(db->srv &&
             (arg_val = gavl_msg_get_arg_nc(msg, 2)) &&
             (arg_arr = gavl_value_get_array_nc(arg_val)) &&
             arg_arr->num_entries > 0)
            {
            for(i = 0; i < arg_arr->num_entries; i++)
              {
              if((arg_dict = gavl_value_get_dictionary_nc(&arg_arr->entries[i])))
                bg_http_server_add_playlist_uris(db->srv, arg_dict);
              }
            }
          
          break;
        case BG_RESP_DB_BROWSE_OBJECT:
          /*
           *  ContextID: album_id
           *  arg0: metadata   (dictionary)
           */
          if(db->srv &&
             (arg_val = gavl_msg_get_arg_nc(msg, 0)) &&
             (arg_dict = gavl_value_get_dictionary_nc(arg_val)))
            {
            bg_http_server_add_playlist_uris(db->srv, arg_dict);
            }
          break;
        }
      }
      break;
    case BG_MSG_NS_MDB_PRIVATE:
      {
      switch(msg->ID)
        {
        case BG_CMD_MDB_ADD_ROOT_ELEMENT:
          {
          int idx;
          gavl_value_t val;
          gavl_dictionary_t * d;
          
          //          gavl_dictionary_t el;
          gavl_msg_t * res;

          gavl_value_init(&val);
          d = gavl_value_set_dictionary(&val);
          gavl_msg_get_arg_dictionary(msg, 0, d);
          
          //          fprintf(stderr, "ADD ROOT ELEMENT\n");
          //          gavl_dictionary_dump(d, 2);
          //          fprintf(stderr, "\n");

          idx = get_root_index(db, d);

          res = bg_msg_sink_get(db->ctrl.evt_sink);
          bg_msg_set_splice_children(res, BG_MSG_DB_SPLICE_CHILDREN, "/", 1, idx, 0, &val);
          bg_msg_sink_put(db->ctrl.evt_sink);

          gavl_track_splice_children_nocopy(&db->root, idx, 0, &val);
          }
          break;
        case BG_CMD_MDB_DEL_ROOT_ELEMENT:
          {
          int idx;
          gavl_msg_t * res;
          const char * id = gavl_msg_get_arg_string_c(msg, 0);
          idx = gavl_get_track_idx_by_id(&db->root, id);

          //          fprintf(stderr, "DEL ROOT ELEMENT %s %d\n", id, idx);

          res = bg_msg_sink_get(db->ctrl.evt_sink);
          bg_msg_set_splice_children(res, BG_MSG_DB_SPLICE_CHILDREN, "/", 1, idx, 1, NULL);
          bg_msg_sink_put(db->ctrl.evt_sink);
          
          gavl_track_splice_children(&db->root, idx, 1, NULL);
          }
          break;
        case BG_CMD_MDB_ADD_MEDIA_DIR:
          {
          const char * dir;

          if(!db->dirs)
            return 1;
            
          dir = gavl_msg_get_arg_string_c(msg, 0);
          bg_media_dirs_add_path(db->dirs, dir);
          }
          break;
        case BG_CMD_MDB_DEL_MEDIA_DIR:
          {
          const char * dir;

          if(!db->dirs)
            return 1;
          
          dir = gavl_msg_get_arg_string_c(msg, 0);
          bg_media_dirs_del_path(db->dirs, dir);
          }
          break;
        }
      return 1; /* Don't pass to frontends */
      break;
      }
    case BG_MSG_NS_PARAMETER:
      {
      switch(msg->ID)
        {
        case BG_MSG_PARAMETER_CHANGED_CTX:
          {
          const char * ctx = NULL;
          const char * name = NULL;
          gavl_value_t val;
          bg_cfg_ctx_t * c;
          const bg_parameter_info_t * info;
          
          gavl_value_init(&val);

          bg_msg_get_parameter_ctx(msg, &ctx, &name, &val);
          
          // fprintf(stderr, "Parameter changed: %s %s\n", ctx, name);
          // gavl_value_dump(&val, 2);

          /* Store value. We assume a flat structure in the backends. */

          if((info = bg_cfg_ctx_find_parameter(db->cfg, ctx, name, &c)) &&
             (c->s))
            {
            bg_cfg_section_set_parameter(c->s, info, &val);
            db->cfg_save_time = gavl_timer_get(db->timer) + 3 * GAVL_TIME_SCALE;
            }
          
          // if(!strcmp(ctx->name, "$general"))
          // fprintf(stderr, "set_parameter frontend %p %s.%s\n", ctx->sink, ctx->name, name);
          
          gavl_value_free(&val);
          }
        }
      }
      
    }

  if(do_forward)
    bg_msg_sink_put_copy(db->ctrl.evt_sink, msg);
  
  return 1;
  }

/* Called by backends */
void bg_mdb_export_media_directory(bg_msg_sink_t * sink, const char * path)
  {
  gavl_msg_t * msg;
  msg = bg_msg_sink_get(sink);
  gavl_msg_set_id_ns(msg, BG_CMD_MDB_ADD_MEDIA_DIR, BG_MSG_NS_MDB_PRIVATE);
  gavl_msg_set_arg_string(msg, 0, path);
  bg_msg_sink_put(sink);
  }

void bg_mdb_unexport_media_directory(bg_msg_sink_t * sink, const char * path)
  {
  gavl_msg_t * msg;

  msg = bg_msg_sink_get(sink);
  gavl_msg_set_id_ns(msg, BG_CMD_MDB_DEL_MEDIA_DIR, BG_MSG_NS_MDB_PRIVATE);
  gavl_msg_set_arg_string(msg, 0, path);
  bg_msg_sink_put(sink);
  }


/* Backend thread */

static void * backend_thread(void * data)
  {
  int ops;
  gavl_time_t delay_time = GAVL_TIME_SCALE / 20; // 50 ms
  bg_mdb_backend_t * be = data;

  //  fprintf(stderr, "Backend thread\n");
  
  while(1)
    {
    ops = 0;

    /* Ping func must come first so we can do some (blocking) initializtion
       before the first messages are processed */

    if(be->ping_func)
      ops += be->ping_func(be);
    
    /* Check for stop */
    if(!bg_msg_sink_iteration(be->ctrl.cmd_sink))
      break;
    
    ops += bg_msg_sink_get_num(be->ctrl.cmd_sink);
    
    if(!ops)
      gavl_time_delay(&delay_time);
    }

  //  fprintf(stderr, "Backend thread finished\n");
    
  return NULL;
  }

static void * mdb_thread(void * data)
  {
  int ops;
  gavl_time_t delay_time = GAVL_TIME_SCALE / 20; // 50 ms
  bg_mdb_t * mdb = data;
  
  
  while(1)
    {
    ops = 0;
    
    /* Check for stop */
    if(!bg_msg_sink_iteration(mdb->ctrl.cmd_sink))
      break;

    ops += bg_msg_sink_get_num(mdb->ctrl.cmd_sink);
    
    bg_msg_sink_iteration(mdb->be_evt_sink);
    ops += bg_msg_sink_get_num(mdb->be_evt_sink);

    /* Check whether to save the config */
    if((mdb->cfg_save_time != GAVL_TIME_UNDEFINED) &&
       (gavl_timer_get(mdb->timer) >= mdb->cfg_save_time))
      {
      gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Saving config file %s", mdb->config_file);
      bg_cfg_registry_save_to(mdb->cfg_reg, mdb->config_file);
      mdb->cfg_save_time = GAVL_TIME_UNDEFINED;
      ops++;
      }
    
    if(!ops)
      gavl_time_delay(&delay_time);
    }
  return NULL;
  }

void bg_mdb_merge_root_metadata(bg_mdb_t * db, const gavl_dictionary_t * m)
  {
  gavl_dictionary_t m_new;
  gavl_dictionary_t * m_root;

  gavl_dictionary_init(&m_new);
  
  m_root = gavl_dictionary_get_dictionary_nc(&db->root, GAVL_META_METADATA);
  gavl_dictionary_merge(&m_new, m, m_root);
  gavl_dictionary_free(m_root);
  gavl_dictionary_move(m_root, &m_new);
  }

/* Set mimetypes state */
static void set_state_mimetypes(bg_mdb_t * db)
  {
  int i;
  gavl_value_t val;
  gavl_array_t * arr;

  gavl_value_t sub_val;
  gavl_dictionary_t * dict;
  
  gavl_value_init(&val);

  arr = gavl_value_set_array(&val);

  i = 0;
  while(bg_mimetypes[i].mimetype)
    {
    gavl_value_init(&sub_val);
    dict = gavl_value_set_dictionary(&sub_val);

    gavl_dictionary_set_string(dict, "ext",      bg_mimetypes[i].ext);
    gavl_dictionary_set_string(dict, GAVL_META_MIMETYPE, bg_mimetypes[i].mimetype);
    gavl_dictionary_set_string(dict, GAVL_META_LABEL,    bg_mimetypes[i].name);
    gavl_array_splice_val_nocopy(arr, -1, 0, &sub_val);
    i++;
    }
  
  bg_state_set(NULL, 1, "mdb", "mimetypes", &val, db->ctrl.evt_sink, BG_MSG_STATE_CHANGED);
  gavl_value_reset(&val);

  }

static int handle_creation_event(void * priv, gavl_msg_t * msg)
  {
  if((msg->NS == BG_MSG_NS_DB) && (msg->ID == BG_MSG_DB_CREATION_DONE))
    {
    int * done = priv;
    *done = 1;
    }
  return 1;
  }


bg_mdb_t * bg_mdb_create(const char * path,
                         int do_create, bg_http_server_t * srv)
  {
  int i;
  int idx;
  bg_mdb_t * ret = calloc(1, sizeof(*ret));
  char * tmp_string;
  int has_new_cfg_reg = 0;
  gavl_dictionary_t * m;
  bg_msg_sink_t * sink = NULL;
  int done = 0;
  
  ret->cfg_save_time = GAVL_TIME_UNDEFINED;
  
  ret->backends = calloc(num_backends, sizeof(*ret->backends));

  ret->volman = bg_volume_manager_create();

  
  ret->timer = gavl_timer_create();
  
  ret->srv = srv;
  
  if(ret->srv)
    ret->dirs = bg_http_server_get_media_dirs(ret->srv);
  
  gavl_timer_start(ret->timer);
  
  if(path)
    {
    char * pos;
    ret->path     = bg_sprintf("%s/%s", path, MDB_DIR);

    /* Remove trailing /  */
    pos = ret->path + (strlen(ret->path) - 1);
    
    if(*pos == '/')
      *pos = '\0';
    
    /* Check if path already exists */
    
    if(do_create)
      {
      if(!access(ret->path, R_OK|W_OK|X_OK))
        {
        gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN,
                 "Won't create database: Directory %s already exists",
                 ret->path);
        goto fail;
        }
      bg_ensure_directory(ret->path, 0);
      }
    }
  else
    {
    if(do_create)
      {
      char * tmp_string;
      if((tmp_string = bg_search_file_write_nocreate(MDB_DIR, NULL)))
        {
        gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN,
                 "Won't create database: Directory %s already exists",
                 tmp_string);
        free(tmp_string);
        goto fail;
        }
      
      ret->path     = bg_search_file_write(MDB_DIR, NULL);
      }
    else
      {
      if(!(ret->path     = bg_search_file_write_nocreate(MDB_DIR, NULL)))
        {
        gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Database not found");
        goto fail;
        }
      }
    }

  /* Try to lock the directory */
  if(!(ret->dirlock = bg_lock_directory(ret->path)))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Couldn't lock database");
    goto fail;
    }
  
  /* Root element */
  m = gavl_dictionary_get_dictionary_create(&ret->root, GAVL_META_METADATA);

  gavl_dictionary_set_string(m, GAVL_META_LABEL, "Root");
  gavl_dictionary_set_string(m, GAVL_META_ID, "/");
  gavl_dictionary_set_string(m, GAVL_META_MEDIA_CLASS, GAVL_META_MEDIA_CLASS_ROOT);
  
  /* Cache */
  tmp_string = bg_sprintf("%s/cache", ret->path);
  ret->cache = bg_object_cache_create(10000, 200, tmp_string);
  free(tmp_string);
  
  /* Thumbnails */
  
  bg_mdb_init_thumbnails(ret);

  /* Controllable */
  bg_controllable_init(&ret->ctrl,
                       bg_msg_sink_create(handle_cmd, ret, 0),
                       bg_msg_hub_create(1));
  
  bg_msg_hub_connect_sink(bg_backend_registry_get_evt_hub() , ret->ctrl.cmd_sink);
  
  ret->be_evt_sink = bg_msg_sink_create(handle_be_msg, ret, 0);
  /* Also need to set the client ID */
  bg_msg_sink_set_id(ret->be_evt_sink, "*");
  
  /* Backends */
  for(i = 0; i < num_backends; i++)
    {
    ret->backends[i].db = ret;
    backends[i].create_func(&ret->backends[i]);
    
    if(do_create && (ret->backends[i].flags & BE_FLAG_CREATION_DONE))
      ret->num_create++;
    
    ret->backends[i].name = backends[i].name;
    ret->backends[i].long_name = backends[i].long_name;
    
    
    /* Get events from backend */
    bg_msg_hub_connect_sink(ret->backends[i].ctrl.evt_hub,
                            ret->be_evt_sink);
    }

  /* Create configuration contexts */
  
  idx = 0;
  i = 0;

  ret->cfg = calloc(num_backends+2, sizeof(*ret->cfg));
  ret->cfg_ext = calloc(num_backends+2, sizeof(*ret->cfg));
  
  bg_cfg_ctx_init(&ret->cfg[idx++],
                  parameters,
                  GENERAL_CTX,
                  TR("General"),
                  NULL, NULL);

  for(i = 0; i < num_backends; i++)
    {
    if(ret->backends[i].parameters)
      bg_cfg_ctx_init(&ret->cfg[idx++],
                      ret->backends[i].parameters,
                      ret->backends[i].name,
                      ret->backends[i].long_name,
                      NULL, NULL);
    }
  
  ret->config_file = bg_sprintf("%s/config.xml", ret->path);
  
  ret->cfg_reg = gavl_dictionary_create();
  
  if(access(ret->config_file, R_OK) ||
     !bg_cfg_registry_load(ret->cfg_reg, ret->config_file))
    {
    /* Create empty registry */
    has_new_cfg_reg = 1;
    }
  
  ret->section = bg_cfg_registry_find_section(ret->cfg_reg, "mdb");

  bg_cfg_ctx_array_create_sections(ret->cfg, ret->section);
  bg_cfg_ctx_set_sink_array(ret->cfg, ret->ctrl.cmd_sink);

  if(has_new_cfg_reg)
    bg_cfg_registry_save_to(ret->cfg_reg, ret->config_file);
  
  /* Create external config */
  memcpy(ret->cfg_ext, ret->cfg, (num_backends+1)*sizeof(*ret->cfg_ext));

  gavl_dictionary_copy(&ret->cfg_section_ext, ret->section);
  
  bg_cfg_ctx_array_create_sections(ret->cfg_ext, &ret->cfg_section_ext);

  
  /* Apply config */
  bg_cfg_ctx_apply_array(ret->cfg);
  
  /* Connect volumemanager. Must be done after applying the config. */
  bg_msg_hub_connect_sink(bg_volume_manager_get_msg_hub(ret->volman), ret->ctrl.cmd_sink);

  /* Set some state variables */

  set_state_mimetypes(ret);

  if(do_create)
    {
    /* Wait until the creation is complete */
    sink = bg_msg_sink_create(handle_creation_event, &done, 0);
    bg_msg_hub_connect_sink(ret->ctrl.evt_hub, sink);
    }
  
  for(i = 0; i < num_backends; i++)
    pthread_create(&ret->backends[i].th, NULL, backend_thread, &ret->backends[i]);
  
  pthread_create(&ret->th, NULL, mdb_thread, ret);

  bg_mdb_export_media_directory(ret->be_evt_sink, ret->thumbs_dir);

  if(do_create)
    {
    gavl_time_t delay_time = GAVL_TIME_SCALE / 10;
    /* Wait until the creation is complete */
    while(1)
      {
      bg_msg_sink_iteration(sink);
      if(!done)
        gavl_time_delay(&delay_time);
      else
        {
        gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Creation completed");
        break;
        }
      }
    }

  if(sink)
    {
    bg_msg_hub_disconnect_sink(ret->ctrl.evt_hub, sink);
    bg_msg_sink_destroy(sink);
    }
  
  return ret;
  
  fail:
  if(ret)
    bg_mdb_destroy(ret);

  return NULL;
  }

void bg_mdb_set_root_name(bg_mdb_t * db, const char * root)
  {
  gavl_dictionary_t * m = gavl_track_get_metadata_nc(&db->root);
  gavl_dictionary_set_string(m, GAVL_META_LABEL, root);
  }

void bg_mdb_stop(bg_mdb_t * db)
  {
  int i;
  gavl_msg_t * msg;

  
  /* Join threads */  
  if(db->backends)
    {
    for(i = 0; i < num_backends; i++)
      {
      msg = bg_msg_sink_get(db->backends[i].ctrl.cmd_sink);
  
      gavl_msg_set_id_ns(msg, GAVL_CMD_QUIT, GAVL_MSG_NS_GENERIC);
      bg_msg_sink_put(db->backends[i].ctrl.cmd_sink);
      pthread_join(db->backends[i].th, NULL);

      if(db->backends[i].stop)
        db->backends[i].stop(&db->backends[i]);
      
      }
    }
  msg = bg_msg_sink_get(db->ctrl.cmd_sink);
  gavl_msg_set_id_ns(msg, GAVL_CMD_QUIT, GAVL_MSG_NS_GENERIC);
  bg_msg_sink_put(db->ctrl.cmd_sink);
  pthread_join(db->th, NULL);
  
  }

void bg_mdb_destroy(bg_mdb_t * db)
  {
  int i;

  if(db->cfg_save_time != GAVL_TIME_UNDEFINED)
    {
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Saving config file %s", db->config_file);
    bg_cfg_registry_save_to(db->cfg_reg, db->config_file);
    }
  
  /* Free data */  
  
  if(db->backends)
    {
    for(i = 0; i < num_backends; i++)
      {
      if(db->backends[i].destroy)
        db->backends[i].destroy(&db->backends[i]);
      bg_controllable_cleanup(&db->backends[i].ctrl);
      }
    free(db->backends);
    }

  if(db->ctrl.cmd_sink)
    bg_msg_hub_disconnect_sink(bg_backend_registry_get_evt_hub(), db->ctrl.cmd_sink);
  
  bg_controllable_cleanup(&db->ctrl);

  if(db->timer)
    gavl_timer_destroy(db->timer);
  
  if(db->be_evt_sink)
    bg_msg_sink_destroy(db->be_evt_sink);

  if(db->thumbs_dir)
    free(db->thumbs_dir);
  
  
  if(db->cache)
    bg_object_cache_destroy(db->cache);

  bg_unlock_directory(db->dirlock, db->path);
  
  if(db->path)
    free(db->path);

  if(db->config_file)
    free(db->config_file);
  
  gavl_dictionary_free(&db->root);

  bg_volume_manager_destroy(db->volman);
  
  bg_mdb_cleanup_thumbnails(db);

  /* Clear parameter infos to avoid double freeing */

  if(db->cfg_ext)
    {
    i = 0;
    while(db->cfg_ext[i].p)
      {
      db->cfg_ext[i].p = NULL;
      i++;
      }
    bg_cfg_ctx_destroy_array(db->cfg_ext);
    }
  
  if(db->cfg)
    bg_cfg_ctx_destroy_array(db->cfg);
  
  gavl_dictionary_free(&db->cfg_section_ext);

  if(db->cfg_reg)
    gavl_dictionary_destroy(db->cfg_reg);

  if(db->rescan_func)
    gavl_msg_destroy(db->rescan_func);
  
  free(db);
  }

void bg_mdb_rescan(bg_controllable_t * db)
  {
  gavl_msg_t * msg = bg_msg_sink_get(db->cmd_sink);
  gavl_msg_set_id_ns(msg, BG_FUNC_DB_RESCAN, BG_MSG_NS_DB);
  bg_msg_sink_put(db->cmd_sink);
  }

#if 1

void bg_mdb_rescan_sync(bg_controllable_t * db)
  {
  gavl_msg_t msg;
  gavl_msg_init(&msg);
  
  gavl_msg_set_id_ns(&msg, BG_FUNC_DB_RESCAN, BG_MSG_NS_DB);
  
  bg_controllable_call_function(db, &msg, NULL, NULL, 1000*2*3600);
  gavl_msg_free(&msg);
  }

#else
static int handle_message_rescan(void * data, gavl_msg_t * msg)
  {
  int * ret = data;

  if((msg->NS == BG_MSG_NS_DB) && (msg->ID == BG_MSG_DB_RESCAN_DONE))
    *ret = 1;
  
  return 1;
  }

void bg_mdb_rescan_sync(bg_controllable_t * db)
  {
  gavl_time_t delay_time = GAVL_TIME_SCALE/20; // 50 ms
  int done = 0;
  bg_msg_sink_t * sink = bg_msg_sink_create(handle_message_rescan, &done, 0);

  bg_msg_hub_connect_sink(db->evt_hub, sink);

  bg_mdb_rescan(db);
  
  while(1)
    {
    bg_msg_sink_iteration(sink);

    if(done)
      break;
    
    if(!bg_msg_sink_get_num(sink))
      gavl_time_delay(&delay_time);
    }
  
  bg_msg_hub_disconnect_sink(db->evt_hub, sink);
  bg_msg_sink_destroy(sink);
  }
#endif


static const struct
  {
  const char * klass;
  const char * label;
  const char * id;
  int idx;
  }
root_folders[] =
  {
    { GAVL_META_MEDIA_CLASS_ROOT_FAVORITES,          "Favorites",    BG_MDB_ID_FAVORITES,    1 },
    { GAVL_META_MEDIA_CLASS_ROOT_LIBRARY,            "Library",      BG_MDB_ID_LIBRARY,      2 },
    { GAVL_META_MEDIA_CLASS_ROOT_MUSICALBUMS,        "Music albums", BG_MDB_ID_MUSICALBUMS,  3 },
    { GAVL_META_MEDIA_CLASS_ROOT_SONGS,              "Songs",        BG_MDB_ID_SONGS,        4 },
    { GAVL_META_MEDIA_CLASS_ROOT_STREAMS,            "Streams",      BG_MDB_ID_STREAMS,      6 },
    { GAVL_META_MEDIA_CLASS_ROOT_PODCASTS,           "Podcasts",     BG_MDB_ID_PODCASTS,     7 },
    { GAVL_META_MEDIA_CLASS_ROOT_MOVIES,             "Movies",       BG_MDB_ID_MOVIES,       8 },
    { GAVL_META_MEDIA_CLASS_ROOT_TV_SHOWS,           "TV Shows",     BG_MDB_ID_TV_SHOWS,     9 },
    { GAVL_META_MEDIA_CLASS_ROOT_PHOTOS,             "Photos",       BG_MDB_ID_PHOTOS,      10 },
    { GAVL_META_MEDIA_CLASS_ROOT_DIRECTORIES,        "Directories",  BG_MDB_ID_DIRECTORIES, 11 },

    { GAVL_META_MEDIA_CLASS_ROOT_REMOVABLE,          "Removable",    NULL,         20 },
    { GAVL_META_MEDIA_CLASS_ROOT_REMOVABLE_AUDIOCD,  "Audio CD",     NULL,         21 },
    { GAVL_META_MEDIA_CLASS_ROOT_REMOVABLE_VCD,      "VCD",          NULL,         22 },
    { GAVL_META_MEDIA_CLASS_ROOT_REMOVABLE_SVCD,     "SVCD",         NULL,         23 },
    { GAVL_META_MEDIA_CLASS_ROOT_REMOVABLE_VIDEODVD, "DVD",          NULL,         24 },

    { GAVL_META_MEDIA_CLASS_ROOT_REMOVABLE_FILESYSTEM,            "Filesystem", NULL, 25 },
    { GAVL_META_MEDIA_CLASS_ROOT_REMOVABLE_FILESYSTEM_HDD,        "Filesystem", NULL, 26 },
    { GAVL_META_MEDIA_CLASS_ROOT_REMOVABLE_FILESYSTEM_PENDRIVE,   "Filesystem", NULL, 27 },
    { GAVL_META_MEDIA_CLASS_ROOT_REMOVABLE_FILESYSTEM_MEMORYCARD, "Filesystem", NULL, 28 },
    { GAVL_META_MEDIA_CLASS_ROOT_REMOVABLE_FILESYSTEM_MOBILE,     "Filesystem", NULL, 29 },
    { GAVL_META_MEDIA_CLASS_ROOT_REMOVABLE_FILESYSTEM_CD,         "Filesystem", NULL, 30 },
    { GAVL_META_MEDIA_CLASS_ROOT_REMOVABLE_FILESYSTEM_DVD,        "Filesystem", NULL, 31 },


#if 0
    { GAVL_META_MEDIA_CLASS_ROOT_BOOKMARKS,   "Bookmarks",    "/bookmarks", 0 },
#endif
    /* MUST be last */
    { GAVL_META_MEDIA_CLASS_ROOT_SERVER,      "Network",      NULL,         100 },
    { /* End */ },
  };

static int get_klass_idx(const char * klass)
  {
  int i = 0;

  while(root_folders[i].klass)
    {
    if(!strcmp(klass, root_folders[i].klass))
      return root_folders[i].idx;
    i++;
    }
  return -1;
  }

static const char * get_klass_label(const char * klass)
  {
  int i = 0;

  while(root_folders[i].klass)
    {
    if(!strcmp(klass, root_folders[i].klass))
      return root_folders[i].label;
    i++;
    }
  return NULL;
  }

const char * bg_mdb_get_klass_id(const char * klass)
  {
  int i = 0;

  while(root_folders[i].klass)
    {
    if(!strcmp(klass, root_folders[i].klass))
      return root_folders[i].id;
    i++;
    }
  return NULL;
  }

const char * bg_mdb_get_klass_from_id(const char * id)
  {
  int i = 0;

  while(root_folders[i].klass)
    {
    if(root_folders[i].id && !strcmp(id, root_folders[i].id))
      return root_folders[i].klass;
    i++;
    }
  return NULL;
  }


void bg_mdb_add_root_container(bg_msg_sink_t * sink, const gavl_dictionary_t * dict)
  {
  gavl_msg_t * msg;
  msg = bg_msg_sink_get(sink);
  gavl_msg_set_id_ns(msg, BG_CMD_MDB_ADD_ROOT_ELEMENT, BG_MSG_NS_MDB_PRIVATE);
  gavl_msg_set_arg_dictionary(msg, 0, dict);
  bg_msg_sink_put(sink);
  }

void bg_mdb_delete_root_container(bg_msg_sink_t * sink, const char * id)
  {
  gavl_msg_t * msg;
  msg = bg_msg_sink_get(sink);
  gavl_msg_set_id_ns(msg, BG_CMD_MDB_DEL_ROOT_ELEMENT, BG_MSG_NS_MDB_PRIVATE);
  gavl_msg_set_arg_string(msg, 0, id);
  bg_msg_sink_put(sink);
  }


void bg_mdb_init_root_container(gavl_dictionary_t * dict, const char * media_class)
  {
  gavl_dictionary_t * m;

  m = gavl_dictionary_get_dictionary_create(dict, GAVL_META_METADATA);
  
  gavl_dictionary_set_string(m, GAVL_META_MEDIA_CLASS, media_class);
  gavl_dictionary_set_string(m, GAVL_META_LABEL, get_klass_label(media_class));
  gavl_dictionary_set_string(m, GAVL_META_ID, bg_mdb_get_klass_id(media_class));
  
  
  }

gavl_dictionary_t * bg_mdb_get_root_container(bg_mdb_t * db, const char * media_class)
  {
  int idx = -1;
  gavl_array_t * arr;
  int i;
  gavl_dictionary_t * ret = NULL;
  gavl_dictionary_t * t;
  const gavl_dictionary_t * mc;
  const char * klass;
  
  int klass_idx;
  int test_idx;
  
  /* Check if the container is already here */
  
  arr = gavl_get_tracks_nc(&db->root);

  if(!arr->num_entries)
    {
    ret = gavl_append_track(&db->root, NULL);
    }
  else
    {
    for(i = 0; i < arr->num_entries; i++)
      {
      if((t = gavl_get_track_nc(&db->root, i)) &&
         (mc = gavl_track_get_metadata(t)) &&
         (klass = gavl_dictionary_get_string(mc, GAVL_META_MEDIA_CLASS)) &&
         !strcmp(klass, media_class))
        return t;
      }
    }
  
  if(!ret)
    {
    gavl_value_t val;
    
    /* Create */

    if((klass_idx = get_klass_idx(media_class)) >= 0)
      {
      for(i = 0; i < arr->num_entries; i++)
        {
        if((t = gavl_get_track_nc(&db->root, i)) &&
           (mc = gavl_track_get_metadata(t)) &&
           (klass = gavl_dictionary_get_string(mc, GAVL_META_MEDIA_CLASS)))
          {
          test_idx = get_klass_idx(klass);

          if((test_idx < 0) ||
             ((test_idx >= 0) && (test_idx > klass_idx)))
            {
            idx = i;
            break;
            }
          }
        }
      }
    
    gavl_value_init(&val);
    ret = gavl_value_set_dictionary(&val);
    gavl_array_splice_val_nocopy(arr, idx, 0, &val);
    }

  bg_mdb_init_root_container(ret, media_class);
  
  gavl_track_update_children(&db->root);
  
  return ret;
  }

const char * bg_mdb_container_get_backend(const gavl_dictionary_t * track)
  {
  const gavl_dictionary_t * m;

  if((m = gavl_dictionary_get_dictionary(track, BG_MDB_DICT)))
    return gavl_dictionary_get_string(m, MDB_BACKEND_TAG);
  return NULL;
  }

void bg_mdb_container_set_backend(gavl_dictionary_t * track, const char * be)
  {
  gavl_dictionary_t * m;
  
  m = gavl_dictionary_get_dictionary_create(track, BG_MDB_DICT);
  gavl_dictionary_set_string(m, MDB_BACKEND_TAG, be);
  }


bg_controllable_t * bg_mdb_get_controllable(bg_mdb_t * db)
  {
  return &db->ctrl;
  }

char * bg_mdb_get_parent_id(const char * id)
  {
  char * end;

  if(!strcmp(id, "/"))
    return NULL;
  
  if(!(end = strrchr(id, '/')))
    return NULL;
  
  if(end == id)
    end++;
  
  return gavl_strndup(id, end);
  }

int bg_mdb_is_ancestor(const char * ancestor, const char * descendant)
  {
  int ancestor_len;

  if(!strcmp(ancestor, "/"))
    return 1;
  
  if(!gavl_string_starts_with(descendant, ancestor))
    return 0;
  
  ancestor_len = strlen(ancestor);
  
  if((descendant[ancestor_len] != '\0') &&
     (descendant[ancestor_len] != '/'))
    return 0;
  
  return 1;
  }

/* Caching utilities */
#if 0
const gavl_dictionary_t * bg_mdb_cache_get_object_max_age(bg_mdb_t * mdb, const char * id, int64_t max_age)
  {
  char * parent_id;
  const gavl_value_t * val;

  if((val = bg_object_cache_get_max_age(mdb->cache, id, max_age)))
    return gavl_value_get_dictionary(val);

  if((parent_id = bg_mdb_get_parent_id(id)))
    {
    const gavl_dictionary_t * dict = NULL;
    
    if((val = bg_object_cache_get_max_age(mdb->cache, parent_id, max_age)) &&
       (dict = gavl_value_get_dictionary(val)))
      dict = gavl_get_track_by_id(dict, id);
    
    free(parent_id);

    return dict;
    }
  return NULL;
  }

const gavl_dictionary_t * bg_mdb_cache_get_object_min_mtime(bg_mdb_t * mdb, const char * id, int64_t min_mtime)
  {
  char * parent_id;
  const gavl_value_t * val;

  if((val = bg_object_cache_get_min_mtime(mdb->cache, id, min_mtime)))
    return gavl_value_get_dictionary(val);

  if((parent_id = bg_mdb_get_parent_id(id)))
    {
    const gavl_dictionary_t * dict = NULL;
    
    if((val = bg_object_cache_get_min_mtime(mdb->cache, parent_id, min_mtime)) &&
       (dict = gavl_value_get_dictionary(val)))
      dict = gavl_get_track_by_id(dict, id);
    
    free(parent_id);

    return dict;
    }
  return NULL;
  }

const gavl_array_t * bg_mdb_cache_get_children_max_age(bg_mdb_t * mdb, const char * id, int64_t max_age)
  {
  const gavl_value_t * val;
  const gavl_dictionary_t * dict;

  if((val = bg_object_cache_get_max_age(mdb->cache, id, max_age)) &&
     (dict = gavl_value_get_dictionary(val)))
    return gavl_get_tracks(dict);
  else
    return NULL;
  }

const gavl_array_t * bg_mdb_cache_get_children_min_mtime(bg_mdb_t * mdb, const char * id, int64_t min_mtime)
  {
  const gavl_value_t * val;
  const gavl_dictionary_t * dict;

  if((val = bg_object_cache_get_min_mtime(mdb->cache, id, min_mtime)) &&
     (dict = gavl_value_get_dictionary(val)))
    return gavl_get_tracks(dict);
  else
    return NULL;
  }
#endif

/* Editable flags */

#define META_EDITABLE "Editable"
#define META_CHILD_CLASSES "ChildClasses"

void bg_mdb_set_editable(gavl_dictionary_t * dict)
  {
  gavl_dictionary_t * m = gavl_dictionary_get_dictionary_create(dict, GAVL_META_METADATA);
  gavl_dictionary_set_int(m, META_EDITABLE, 1);
  }

void bg_mdb_add_can_add(gavl_dictionary_t * dict, const char * child_class)
  {
  gavl_dictionary_t * m = gavl_dictionary_get_dictionary_create(dict, GAVL_META_METADATA);
  gavl_dictionary_append_string_array(m, META_CHILD_CLASSES, child_class);
  }

void bg_mdb_clear_editable(gavl_dictionary_t * dict)
  {
  gavl_dictionary_t * m = gavl_dictionary_get_dictionary_create(dict, GAVL_META_METADATA);
  gavl_dictionary_set(m, META_EDITABLE,      NULL);
  gavl_dictionary_set(m, META_CHILD_CLASSES, NULL);
  }

int bg_mdb_is_editable(const gavl_dictionary_t * dict)
  {
  const gavl_dictionary_t * m;
  int val = 0;
  
  if((m = gavl_track_get_metadata(dict)) && 
     gavl_dictionary_get_int(m, META_EDITABLE, &val) &&
     (val > 0))
    return 1;
  else
    return 0;
  }

int bg_mdb_can_add(const gavl_dictionary_t * dict, const char * child_class)
  {
  int idx;
  const char * str;
  const char * klass;
  const gavl_dictionary_t * m;

  //  fprintf(stderr, "bg_mdb_can_add\n");
  //  gavl_dictionary_dump(dict, 2);
  //  fprintf(stderr, "\n");
  
  if(!bg_mdb_is_editable(dict))
    return 0;

  if(!(m = gavl_track_get_metadata(dict)) ||
     !(klass = gavl_dictionary_get_string(m, GAVL_META_MEDIA_CLASS)))
    return 0;
  
  /* Directories can only be added to
     GAVL_META_MEDIA_CLASS_ROOT_PHOTOS and GAVL_META_MEDIA_CLASS_ROOT_DIRECTORIES */

  if(!strcmp(child_class, GAVL_META_MEDIA_CLASS_DIRECTORY))
    {
    if(!strcmp(klass, GAVL_META_MEDIA_CLASS_ROOT_PHOTOS) ||
       !strcmp(klass, GAVL_META_MEDIA_CLASS_ROOT_DIRECTORIES))
      return 1;
    else
      return 0;
    }
  
  if(!gavl_dictionary_get(m, META_CHILD_CLASSES))
    return 1;
  
  idx = 0;
  while((str = gavl_dictionary_get_string_array(m, META_CHILD_CLASSES, idx)))
    {
    if(!fnmatch(str, child_class, 0))
      return 1;
    idx++;
    }
  return 0;
  }

void bg_mdb_object_changed(gavl_dictionary_t * dst, const gavl_dictionary_t * src)
  {
  gavl_dictionary_t dict;
  const gavl_value_t * v;
  gavl_dictionary_t * dst_m;
  const gavl_dictionary_t * src_m;
  
  if((v  = gavl_dictionary_get(src, GAVL_META_STREAMS)))
    gavl_dictionary_set(dst, GAVL_META_STREAMS, v);
  
  if((src_m = gavl_track_get_metadata(src)))
    {
    dst_m = gavl_dictionary_get_dictionary_create(dst, GAVL_META_METADATA);

    gavl_dictionary_init(&dict);
    gavl_dictionary_merge(&dict, src_m, dst_m);
    gavl_dictionary_free(dst_m);
    gavl_dictionary_move(dst_m, &dict);
    }

  gavl_track_copy_gui_state(dst, src);
  }

static void add_http_uris(bg_mdb_t * mdb, gavl_dictionary_t * dict, const char * name)
  {
  int i = 0;
  const gavl_dictionary_t * local_dict = NULL;
  const gavl_value_t * local_val;
  
  const char * local_uri;

  char * http_uri;

  int num = gavl_dictionary_get_num_items(dict, name);
  
  for(i = 0; i < num; i++)
    {
    http_uri = NULL;
    local_uri = NULL;
    
    if((local_val = gavl_dictionary_get_item(dict, name, i)) &&
       (local_dict = gavl_value_get_dictionary(local_val)) &&
       (local_uri = gavl_dictionary_get_string(local_dict, GAVL_META_URI)) &&
       (local_uri[0] == '/') &&
       (http_uri = bg_media_dirs_local_to_http_uri(mdb->dirs, local_uri)))
      {
      gavl_dictionary_t tmp_dict;
      gavl_dictionary_t * http_dict;

      gavl_dictionary_init(&tmp_dict);
      gavl_dictionary_copy(&tmp_dict, local_dict);

      if((http_dict = gavl_metadata_add_src(dict, name, NULL, http_uri)))
        {
        gavl_dictionary_merge2(http_dict, &tmp_dict);
        
        /* Remove filesystem specific stuff */
        gavl_dictionary_set(http_dict, GAVL_META_MTIME, NULL);
        }
      gavl_dictionary_free(&tmp_dict);
      }
#if 0
    else
      {
      fprintf(stderr, "Won't add http uris: %s\n", local_uri);
      gavl_dictionary_dump(local_dict, 2);
      }
#endif
    if(http_uri)
      free(http_uri);
    }
  }

/* Add / delete http translations of radiobrowser URIs */


static void rb_add_uri(gavl_dictionary_t * m)
  {
  const char * location = NULL;
  char * uri;
  
  if(!gavl_metadata_get_src(m, GAVL_META_SRC, 0, NULL, &location))
    return;
  
#if 0  
  if(!location)
    {
    fprintf(stderr, "Scheisse\n");
    gavl_dictionary_dump(m, 2);
    return;
    }
#endif
  if(!bg_rb_check_uri(location))
    return;
  
  if((uri = bg_rb_resolve_uri(location)))
    {
    gavl_metadata_add_src(m, GAVL_META_SRC,
                          "application/mpegurl", uri);
    free(uri);
    }
  
  }

void bg_mdb_add_http_uris(bg_mdb_t * mdb, gavl_dictionary_t * dict)
  {
  gavl_dictionary_t * m;
  gavl_dictionary_t * part;
  int num_parts, i;
  
  if(mdb->srv && mdb->srv->plughandler)
    bg_plug_handler_add_uris(mdb->srv->plughandler, dict);
  
  if(mdb->srv && mdb->srv->lpcmhandler)
    bg_lpcm_handler_add_uris(mdb->srv->lpcmhandler, dict);

  bg_mdb_get_thumbnails(mdb, dict);
  
  if(!(m = gavl_track_get_metadata_nc(dict)) || !mdb->dirs)
    return;
  
  rb_add_uri(m);
  
  //  fprintf(stderr, "Add http uris 1\n");
  //  gavl_dictionary_dump(m, 2);
  
  add_http_uris(mdb, m, GAVL_META_SRC);
  add_http_uris(mdb, m, GAVL_META_COVER_URL);
  add_http_uris(mdb, m, GAVL_META_POSTER_URL);
  add_http_uris(mdb, m, GAVL_META_WALLPAPER_URL);
  add_http_uris(mdb, m, GAVL_META_ICON_URL);

  //  fprintf(stderr, "Add http uris 2\n");
  //  gavl_dictionary_dump(dict, 2);

  num_parts = gavl_track_get_num_parts(dict);

  for(i = 0; i < num_parts; i++)
    {
    part = gavl_track_get_part_nc(dict, i);
    bg_mdb_add_http_uris(mdb, part);
    }
  
  }

void bg_mdb_add_http_uris_arr(bg_mdb_t * mdb, gavl_array_t * arr)
  {
  int i;
  gavl_dictionary_t * dict;
  for(i = 0; i < arr->num_entries; i++)
    {
    if((dict = gavl_value_get_dictionary_nc(&arr->entries[i])))
      bg_mdb_add_http_uris(mdb, dict);
    }
  }

static void delete_http_uris(gavl_dictionary_t * dict, const char * name)
  {
  const gavl_dictionary_t * local_dict;
  const gavl_value_t * local_val;
  const char * local_uri;

  /* If the first uri is a regular file, remove all higher uris */
  if((local_val = gavl_dictionary_get_item(dict, name, 0)) &&
     (local_dict = gavl_value_get_dictionary(local_val)) &&
     (local_uri = gavl_dictionary_get_string(local_dict, GAVL_META_URI)) &&
     ((local_uri[0] == '/') || bg_rb_check_uri(local_uri)))
    {
    int num = gavl_dictionary_get_num_items(dict, name);
    while(num > 1)
      {
      gavl_dictionary_delete_item(dict, name, 1);
      num--;
      }
    }
  }

void bg_mdb_delete_http_uris(gavl_dictionary_t * dict)
  {
  if(!(dict = gavl_track_get_metadata_nc(dict)))
    return;

  delete_http_uris(dict, GAVL_META_SRC);
  delete_http_uris(dict, GAVL_META_COVER_URL);
  delete_http_uris(dict, GAVL_META_POSTER_URL);
  delete_http_uris(dict, GAVL_META_WALLPAPER_URL);
  }

int bg_mdb_is_parent_id(const char * child, const char * parent)
  {
  int parent_len = strlen(parent);
  
  if(gavl_string_starts_with(child, parent) &&
     (child[parent_len] == '/') &&
     !strchr(child + (parent_len+1), '/'))
    return 1;
  else
    return 0;
  }

#if 0
typedef struct
  {
  const char * function_tag;
  gavl_dictionary_t * ret;
  } browse_context_t;
#endif

static int handle_msg_browse_object_sync(void * data, gavl_msg_t * msg)
  {
  gavl_msg_get_arg_dictionary(msg, 0, data);
  return 1;
  }

#if 1
int bg_mdb_browse_object_sync(bg_controllable_t * mdb,
                              gavl_dictionary_t * ret,
                              const char * id, int timeout)
  {
  int result = 0;
  gavl_msg_t msg;
  gavl_msg_init(&msg);
  
  gavl_msg_set_id_ns(&msg, BG_FUNC_DB_BROWSE_OBJECT, BG_MSG_NS_DB);

  gavl_dictionary_set_string(&msg.header, GAVL_MSG_CONTEXT_ID, id);
  
  result = bg_controllable_call_function(mdb, &msg,
                                         handle_msg_browse_object_sync, ret, timeout);
  gavl_msg_free(&msg);
  return result;
  }
#endif

static int handle_msg_browse_children_sync(void * data, gavl_msg_t * msg)
  {
  gavl_msg_splice_children(msg, data);
  return 1;
  }

int bg_mdb_browse_children_sync(bg_controllable_t * mdb, gavl_dictionary_t * ret, const char * id, int timeout)
  {
  int result = 0;
  gavl_msg_t msg;
  gavl_msg_init(&msg);

  bg_mdb_set_browse_children_request(&msg, id, 0, -1, 1);
  bg_msg_add_function_tag(&msg);
  
  result = bg_controllable_call_function(mdb, &msg,
                                         handle_msg_browse_children_sync,
                                         ret, timeout);
  gavl_msg_free(&msg);
  return result;
  }

static const struct
  {
  const char * klass;
  const char * child_class;
  const char * tooltip;
  }
classes[] =
  {
  
  /* Container values */
  
  //  { GAVL_META_MEDIA_CLASS_CONTAINER },
  { GAVL_META_MEDIA_CLASS_MUSICALBUM, GAVL_META_MEDIA_CLASS_SONG }, 
  //  { GAVL_META_MEDIA_CLASS_PLAYLIST },
  //  { GAVL_META_MEDIA_CLASS_CONTAINER_ACTOR },
  //  { GAVL_META_MEDIA_CLASS_CONTAINER_DIRECTOR },
  //  { GAVL_META_MEDIA_CLASS_CONTAINER_ARTIST },
  //  { GAVL_META_MEDIA_CLASS_CONTAINER_COUNTRY },
  //  { GAVL_META_MEDIA_CLASS_CONTAINER_GENRE },
  //  { GAVL_META_MEDIA_CLASS_CONTAINER_LANGUAGE },
  //  { GAVL_META_MEDIA_CLASS_CONTAINER_TAG },
  //  { GAVL_META_MEDIA_CLASS_CONTAINER_YEAR },
  { GAVL_META_MEDIA_CLASS_TV_SEASON, GAVL_META_MEDIA_CLASS_TV_EPISODE },
  { GAVL_META_MEDIA_CLASS_TV_SHOW, GAVL_META_MEDIA_CLASS_TV_SEASON },
  //  { GAVL_META_MEDIA_CLASS_DIRECTORY },

  //  { GAVL_META_MEDIA_CLASS_MULTITRACK_FILE },

  /* Root Containers */
  { GAVL_META_MEDIA_CLASS_ROOT },
  { GAVL_META_MEDIA_CLASS_ROOT_MUSICALBUMS, GAVL_META_MEDIA_CLASS_CONTAINER },
  { GAVL_META_MEDIA_CLASS_ROOT_SONGS,       GAVL_META_MEDIA_CLASS_CONTAINER  },
  { GAVL_META_MEDIA_CLASS_ROOT_MOVIES,      GAVL_META_MEDIA_CLASS_CONTAINER },
  { GAVL_META_MEDIA_CLASS_ROOT_TV_SHOWS,    GAVL_META_MEDIA_CLASS_CONTAINER },
  { GAVL_META_MEDIA_CLASS_ROOT_STREAMS,     GAVL_META_MEDIA_CLASS_CONTAINER, "Add http(s) urls for Radio- or TV channels in m3u format.\nUse radiobrowser:// to import the database from radio-browser.info.\nUse iptv-org:// to import the database from https://iptv-org.github.io/" },
  { GAVL_META_MEDIA_CLASS_ROOT_DIRECTORIES, GAVL_META_MEDIA_CLASS_DIRECTORY, "Add directories, which will be scanned recursively for media files" },
  { GAVL_META_MEDIA_CLASS_ROOT_PODCASTS,    GAVL_META_MEDIA_CLASS_PODCAST,
    "Add urls for podcast feeds (in RSS xml format)" },
  //  { GAVL_META_MEDIA_CLASS_PODCAST,  },
  
  { GAVL_META_MEDIA_CLASS_ROOT_PHOTOS, NULL, "Add directories, which will be scanned recursively for photo albums" },

  //  { GAVL_META_MEDIA_CLASS_ROOT_INCOMING },
  //  { GAVL_META_MEDIA_CLASS_ROOT_FAVORITES },
  //  { GAVL_META_MEDIA_CLASS_ROOT_BOOKMARKS  },
  { GAVL_META_MEDIA_CLASS_ROOT_LIBRARY, NULL, "Add generic containers, playlists or TV- or Radio channel lists" },
  //  { GAVL_META_MEDIA_CLASS_ROOT_NETWORK },
 
  //  { GAVL_META_MEDIA_CLASS_ROOT_REMOVABLE },
  { GAVL_META_MEDIA_CLASS_ROOT_REMOVABLE_AUDIOCD, GAVL_META_MEDIA_CLASS_SONG },
  //  { GAVL_META_MEDIA_CLASS_ROOT_REMOVABLE_VCD },
  //  { GAVL_META_MEDIA_CLASS_ROOT_REMOVABLE_SVCD },
  //  { GAVL_META_MEDIA_CLASS_ROOT_REMOVABLE_VIDEODVD },

  //    { GAVL_META_MEDIA_CLASS_ROOT_REMOVABLE_FILESYSTEM },
  //    { GAVL_META_MEDIA_CLASS_ROOT_REMOVABLE_FILESYSTEM_HDD },
  //    { GAVL_META_MEDIA_CLASS_ROOT_REMOVABLE_FILESYSTEM_PENDRIVE },
  //    { GAVL_META_MEDIA_CLASS_ROOT_REMOVABLE_FILESYSTEM_MEMORYCARD },
  //    { GAVL_META_MEDIA_CLASS_ROOT_REMOVABLE_FILESYSTEM_MOBILE },
  //    { GAVL_META_MEDIA_CLASS_ROOT_REMOVABLE_FILESYSTEM_CD },
  //    { GAVL_META_MEDIA_CLASS_ROOT_REMOVABLE_FILESYSTEM_DVD },

  { /* End */ }
  };

const char * bg_mdb_get_child_class(const gavl_dictionary_t * dict)
  {
  const char * klass;
  int idx = 0;
  
  dict = gavl_track_get_metadata(dict);
  
  if((klass = gavl_dictionary_get_string(dict, GAVL_META_CHILD_CLASS)))
    return klass;

  if(!(klass = gavl_dictionary_get_string(dict, GAVL_META_MEDIA_CLASS)))
    return NULL;
  
  while(classes[idx].klass)
    {
    if(!strcmp(classes[idx].klass, klass))
      return classes[idx].child_class;
    idx++;
    }
  return NULL;
  }

  
static void finalize(gavl_dictionary_t * track, int idx, int total)
  {
  const char * klass;
  int i = 0;
  gavl_dictionary_t * m;

  //  fprintf(stderr, "Finalize track\n");
  //  gavl_dictionary_dump(track, 2);
  
  gavl_dictionary_set(track, GAVL_META_CHILDREN, NULL);
  gavl_dictionary_set(track, GAVL_META_STREAMS, NULL);
  gavl_dictionary_set(track, "astreams", NULL);
  gavl_dictionary_set(track, "vstreams", NULL);
  gavl_dictionary_set(track, "tstreams", NULL);
  
  gavl_track_finalize(track);
  
  if(!(m = gavl_track_get_metadata_nc(track)))
    return;

  if(!(klass = gavl_dictionary_get_string(m, GAVL_META_MEDIA_CLASS)))
    return;
  
  while(classes[i].klass)
    {
    if(!strcmp(classes[i].klass, klass))
      {
      gavl_dictionary_set_string(m, GAVL_META_CHILD_CLASS, classes[i].child_class);
      gavl_dictionary_set_string(m, GAVL_META_TOOLTIP, classes[i].tooltip);
      break;
      }
    i++;
    }
  
  if(idx >= 0)
    gavl_dictionary_set_int(m, GAVL_META_IDX, idx);
  
  if(total > 0)
    gavl_dictionary_set_int(m, GAVL_META_TOTAL, total);
  
  //  fprintf(stderr, "Finalized track\n");
  //  gavl_dictionary_dump(track, 2);
  
  }

/* Grouping */


const bg_mdb_group_t bg_mdb_groups[] =
  {
    { "0-9",    BG_MDB_GROUP_PREFIX"0-9" },
    { "A",      BG_MDB_GROUP_PREFIX"a" },
    { "B",      BG_MDB_GROUP_PREFIX"b" },
    { "C",      BG_MDB_GROUP_PREFIX"c" },
    { "D",      BG_MDB_GROUP_PREFIX"d" },
    { "E",      BG_MDB_GROUP_PREFIX"e" },
    { "F",      BG_MDB_GROUP_PREFIX"f" },
    { "G",      BG_MDB_GROUP_PREFIX"g" },
    { "H",      BG_MDB_GROUP_PREFIX"h" },
    { "I",      BG_MDB_GROUP_PREFIX"i" },
    { "J",      BG_MDB_GROUP_PREFIX"j" },
    { "K",      BG_MDB_GROUP_PREFIX"k" },
    { "L",      BG_MDB_GROUP_PREFIX"l" },
    { "M",      BG_MDB_GROUP_PREFIX"m" },
    { "N",      BG_MDB_GROUP_PREFIX"n" },
    { "O",      BG_MDB_GROUP_PREFIX"o" },
    { "P",      BG_MDB_GROUP_PREFIX"p" },
    { "Q",      BG_MDB_GROUP_PREFIX"q" },
    { "R",      BG_MDB_GROUP_PREFIX"r" },
    { "S",      BG_MDB_GROUP_PREFIX"s" },
    { "T",      BG_MDB_GROUP_PREFIX"t" },
    { "U",      BG_MDB_GROUP_PREFIX"u" },
    { "V",      BG_MDB_GROUP_PREFIX"v" },
    { "W",      BG_MDB_GROUP_PREFIX"w" },
    { "X",      BG_MDB_GROUP_PREFIX"x" },
    { "Y",      BG_MDB_GROUP_PREFIX"y" },
    { "Z",      BG_MDB_GROUP_PREFIX"z" },
    { "Others", BG_MDB_GROUP_PREFIX"others" },
  };

const int bg_mdb_num_groups = sizeof(bg_mdb_groups) / sizeof(bg_mdb_groups[0]);

int bg_mdb_test_group_condition(const char * id, const char * str)
  {
  id += BG_MDB_GROUP_PREFIX_LEN;
  if(!strcmp(id, "0-9"))
    {
    if(isdigit(*str))
      return 1;
    else
      return 0;
    }
  else if(!strcmp(id, "others"))
    {
    if(!isdigit(*str) &&
       !((*str >= 'a') && (*str <= 'z')) &&
       !((*str >= 'A') && (*str <= 'Z')))
      return 1;
    else
      return 0;
    }
  else if((*id >= 'a') && (*id <= 'z'))
    {
    if(tolower(*str) == *id)
      return 1;
    return 0;
    }
  return 0;
  }

const char * bg_mdb_get_group_label(const char * id)
  {
  int i;

  for(i = 0; i < bg_mdb_num_groups; i++)
    {
    if(!strcmp(bg_mdb_groups[i].id, id))
      return bg_mdb_groups[i].label;
    }
  return NULL;
  }

const char * bg_mdb_get_group_id(const char * str)
  {
  int i;

  for(i = 0; i < bg_mdb_num_groups; i++)
    {
    if(bg_mdb_test_group_condition(bg_mdb_groups[i].id, str))
      return bg_mdb_groups[i].id;
    }
  return NULL;
  }

int bg_mdb_get_num_groups(gavl_array_t * arr)
  {
  int ret = 0;
  int i, j;
  
  for(i = 0; i < bg_mdb_num_groups; i++)
    {
    for(j = 0; j < arr->num_entries; j++)
      {
      if(bg_mdb_test_group_condition(bg_mdb_groups[i].id, gavl_value_get_string(&arr->entries[j])))
        {
        ret++;
        break;
        }
      }
    }
  return ret;
  }

int bg_mdb_get_group_size(gavl_array_t * arr, const char * id)
  {
  int j;
  int ret = 0;
  
  for(j = 0; j < arr->num_entries; j++)
    {
    if(bg_mdb_test_group_condition(id, gavl_value_get_string(&arr->entries[j])))
      ret++;
    }
  return ret;
  }

int bg_mdb_adjust_num(int start, int * num, int total)
  {
  if(start >= total)
    *num = 0;

  if((*num < 1) || (*num + start > total))
    *num = total - start;
  
  return !!(*num);
  }


void bg_mdb_set_idx_total(gavl_array_t * arr, int idx, int total)
  {
  int i;

  gavl_dictionary_t * dict;

  for(i = 0; i < arr->num_entries; i++)
    {
    if((dict = gavl_value_get_dictionary_nc(&arr->entries[i])) &&
       (dict = gavl_track_get_metadata_nc(dict)))
      {
      gavl_dictionary_set_int(dict, GAVL_META_IDX, idx + i);
      gavl_dictionary_set_int(dict, GAVL_META_TOTAL, total);
      }
    }
  
    
  }

void bg_mdb_set_next_previous(gavl_array_t * arr)
  {
  int i;

  gavl_dictionary_t * dict;
  
  gavl_dictionary_t * m;
  gavl_dictionary_t * m_p = NULL;
  gavl_dictionary_t * m_n;

  if(!arr->num_entries)
    return;
  
  dict = gavl_value_get_dictionary_nc(&arr->entries[0]);
  m = gavl_track_get_metadata_nc(dict);
  
  for(i = 0; i < arr->num_entries; i++)
    {
    if(i < arr->num_entries-1)
      {
      dict = gavl_value_get_dictionary_nc(&arr->entries[i+1]);
      m_n = gavl_track_get_metadata_nc(dict);
      }
    else
      m_n = NULL;

    if(m)
      {
      if(m_n)
        gavl_dictionary_set_string(m, GAVL_META_NEXT_ID, gavl_dictionary_get_string(m_n, GAVL_META_ID));
      else
        gavl_dictionary_set_string(m, GAVL_META_NEXT_ID, NULL);
    
      if(m_p)
        gavl_dictionary_set_string(m, GAVL_META_PREVIOUS_ID, gavl_dictionary_get_string(m_p, GAVL_META_ID));
      else
        gavl_dictionary_set_string(m, GAVL_META_PREVIOUS_ID, NULL);
      }
    
    m_p = m;
    m = m_n;
    }
  
  }

static char * save_tags[] =
  {
    GAVL_META_MEDIA_CLASS,
    GAVL_META_CHILD_CLASS,
    GAVL_META_ID,
    GAVL_META_NEXT_ID,
    GAVL_META_PREVIOUS_ID,
    GAVL_META_IDX,
    GAVL_META_TOTAL,
    GAVL_META_LABEL,
    GAVL_META_TITLE,
    GAVL_META_ARTIST,
    GAVL_META_ALBUMARTIST,
    GAVL_META_AUTHOR,
    GAVL_META_ALBUM,
    GAVL_META_GENRE,
    GAVL_META_TRACKNUMBER,
    GAVL_META_DATE,
    GAVL_META_DIRECTOR,
    GAVL_META_ACTOR,
    GAVL_META_COUNTRY,
    GAVL_META_PLOT,
    GAVL_META_DESCRIPTION,
    GAVL_META_STATION,
    GAVL_META_STATION_URL,
    GAVL_META_APPROX_DURATION,
    GAVL_META_AUDIO_LANGUAGES,
    GAVL_META_SUBTITLE_LANGUAGES,
    GAVL_META_SRC,
    GAVL_META_COVER_URL,
    GAVL_META_POSTER_URL,
    GAVL_META_ICON_URL,
    GAVL_META_AUDIO_CHANNELS,
    GAVL_META_AUDIO_SAMPLERATE,
    GAVL_META_WIDTH,
    GAVL_META_HEIGHT,
    GAVL_META_NUM_CHILDREN,
    GAVL_META_NUM_ITEM_CHILDREN,
    GAVL_META_NUM_CONTAINER_CHILDREN,
    NULL /* End */
  };

void bg_mdb_object_cleanup(gavl_dictionary_t * dict)
  {
  int i;
  gavl_dictionary_t m_dst;
  const gavl_dictionary_t * m_src;
  gavl_dictionary_t * m_new;
  
  gavl_dictionary_init(&m_dst);

  m_src = gavl_track_get_metadata(dict);

  i = 0;
  while(save_tags[i])
    {
    gavl_dictionary_set(&m_dst, save_tags[i], gavl_dictionary_get(m_src, save_tags[i]));
    i++;
    }
  gavl_dictionary_reset(dict);
  
  m_new = gavl_dictionary_get_dictionary_create(dict, GAVL_META_METADATA);
  gavl_dictionary_move(m_new, &m_dst);
  }

void bg_mdb_track_lock(bg_mdb_backend_t * b, int lock, gavl_dictionary_t * obj)
  {
  gavl_msg_t * msg;

  if(!obj)
    return;
  
  gavl_track_set_lock(obj, lock);
  msg = bg_msg_sink_get(b->ctrl.evt_sink);
  gavl_msg_set_id_ns(msg, BG_MSG_DB_OBJECT_CHANGED, BG_MSG_NS_DB);
  
  gavl_dictionary_set_string(&msg->header, GAVL_MSG_CONTEXT_ID, gavl_track_get_id(obj));
  gavl_msg_set_arg_dictionary(msg, 0, obj);
  bg_msg_sink_put(b->ctrl.evt_sink);
  }


void bg_mdb_set_load_uri(gavl_msg_t * msg, const char * id, int idx, const char * uri)
  {
  gavl_value_t add_val;
  gavl_dictionary_t * dict;
  
  gavl_value_init(&add_val);
  dict = gavl_value_set_dictionary(&add_val);

  gavl_track_from_location(dict, uri);
  
  gavl_msg_set_splice_children_nocopy(msg, BG_MSG_NS_DB, BG_CMD_DB_SPLICE_CHILDREN,
                                      id, 1, idx, 0, &add_val);
  
  }

void bg_mdb_set_load_uris(gavl_msg_t * msg, const char * id, int idx, const gavl_array_t * arr)
  {
  int i;
  gavl_value_t add_val;
  gavl_array_t * add_arr;

  //  fprintf(stderr, "bg_mdb_set_load_uris\n");
  
  gavl_value_init(&add_val);
  add_arr = gavl_value_set_array(&add_val);

  for(i = 0; i < arr->num_entries; i++)
    {
    gavl_value_t dict_val;
    gavl_dictionary_t * dict;
    
    gavl_value_init(&dict_val);
    dict = gavl_value_set_dictionary(&dict_val);

    gavl_track_from_location(dict, gavl_string_array_get(arr, i));
    gavl_array_splice_val_nocopy(add_arr, -1, 0, &dict_val);
    }
  //  fprintf(stderr, "bg_mdb_set_load_uris 1\n");
  //  gavl_array_dump(add_arr, 2);
  bg_msg_set_splice_children(msg, BG_CMD_DB_SPLICE_CHILDREN, id, 1, idx, 0, &add_val);
  gavl_value_free(&add_val);
  }

