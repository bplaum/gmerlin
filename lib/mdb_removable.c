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
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <glob.h>
#include <errno.h>

#include <config.h>

#include <gmerlin/translation.h>
#include <gmerlin/log.h>
#define LOG_DOMAIN "mdb.removable"

#include <gmerlin/mdb.h>

#include <mdb_private.h>

#include <gmerlin/utils.h>

#include <gavl/metatags.h>

#define REMOVABLE_ID_PREFIX "/removable"
#define VOLUME_ID "VolumeID"

static const bg_parameter_info_t parameters[] =
  {
    {
      .name = "mount_audiocd",
      .long_name = TRS("Mount audio CDs"),
      .type = BG_PARAMETER_CHECKBUTTON,
    },
    {
      .name = "mount_videodisk",
      .long_name = TRS("Mount video disks"),
      .type = BG_PARAMETER_CHECKBUTTON,
    },
    { /* End */ },
  };

typedef struct
  {
  gavl_array_t removables;
  int64_t removable_counter;
  
  int mount_audiocd;
  int mount_videodisk;
  int have_params;
  
  } removable_t;

static const gavl_dictionary_t * get_removable(bg_mdb_backend_t * b, const char * id, int * sub)
  {
  int i;
  int id_len;
  const char * removable_id;
  const gavl_dictionary_t * ret;
  removable_t * r = b->priv;
  
  for(i = 0; i < r->removables.num_entries; i++)
    {
    if(!(ret = gavl_value_get_dictionary(&r->removables.entries[i])))
      continue;

    if(!(removable_id = gavl_track_get_id(ret)))
      continue;

    if(!gavl_string_starts_with(removable_id, id))
      continue;

    id_len = strlen(removable_id);

    if(id[id_len] == '\0')
      *sub = 0;
    else if(id[id_len] != '/')
      *sub = 1;
    else
      continue;
    
    return ret;
    }
  return NULL;
  }

static void add(bg_mdb_backend_t * b, const char * uri, const char * klass, const char * volume_id)
  {
  int i;
  char * id;
  gavl_dictionary_t * ret;
  gavl_dictionary_t * ret_m;

  gavl_dictionary_t * child;
  gavl_dictionary_t * child_m;

  gavl_value_t add_val;
  gavl_dictionary_t * add_dict;
  
  int num;
  removable_t * r = b->priv;

  id = gavl_sprintf("/removable-%"PRId64, ++r->removable_counter);
  ret = bg_plugin_registry_load_media_info(bg_plugin_reg, uri, 0);
  bg_mdb_container_set_backend(ret, MDB_BACKEND_REMOVABLE);
  
  ret_m = gavl_dictionary_get_dictionary_create(ret, GAVL_META_METADATA);
  gavl_dictionary_set_string(ret_m, GAVL_META_ID, id);
  gavl_dictionary_set_string(ret_m, VOLUME_ID, volume_id);
  
  num = gavl_get_num_tracks(ret);

  for(i = 0; i < num; i++)
    {
    if((child = gavl_get_track_nc(ret, i)) &&
       (child_m = gavl_track_get_metadata_nc(child)))
      {
      gavl_dictionary_set_string_nocopy(child_m, GAVL_META_ID, bg_sprintf("%s/?track=%d", id, i+1));
      bg_mdb_add_http_uris(b->db, child);
      }
    }

  bg_mdb_set_next_previous(gavl_get_tracks_nc(ret));
  
  gavl_value_init(&add_val);
  add_dict = gavl_value_set_dictionary(&add_val);

  gavl_dictionary_copy(add_dict, ret);
  gavl_array_splice_val_nocopy(&r->removables, -1, 0, &add_val);
  gavl_dictionary_set(ret, GAVL_META_CHILDREN, NULL);
  bg_mdb_add_root_container(b->ctrl.evt_sink, ret);
  gavl_dictionary_destroy(ret);
  }


static int handle_msg(void * priv, gavl_msg_t * msg)
  {
  bg_mdb_backend_t * be = priv;
  removable_t * r = be->priv;

  switch(msg->NS)
    {
    case BG_MSG_NS_DB:
      {
      switch(msg->ID)
        {
        case BG_FUNC_DB_BROWSE_OBJECT:
          {
          int sub = 0;
          int idx, total;
          const gavl_dictionary_t * ret;
          gavl_msg_t * res;
          const char * ctx_id = gavl_dictionary_get_string(&msg->header,
                                                           GAVL_MSG_CONTEXT_ID);
          
          if(!(ret = get_removable(be, ctx_id, &sub)))
            {
            return 1;
            }

          if(!sub)
            {
            /* Should be handled by the core */
            return 1;
            }
          
          idx = gavl_get_track_idx_by_id(ret, ctx_id);
          total = gavl_get_num_tracks(ret);
          ret = gavl_get_track(ret, idx);
          
          res = bg_msg_sink_get(be->ctrl.evt_sink);
          bg_mdb_set_browse_obj_response(res, ret, msg, idx, total);
          bg_msg_sink_put(be->ctrl.evt_sink);
          }
          break;
        case BG_FUNC_DB_BROWSE_CHILDREN:
          {
          const char * ctx_id;
          int start, num, one_answer, total;
          int sub;
          const gavl_dictionary_t * dict;
          gavl_msg_t * res;
          
          bg_mdb_get_browse_children_request(msg, &ctx_id, &start, &num, &one_answer);

          dict = get_removable(be, ctx_id, &sub);
          
          if(!dict || sub)
            return 1;

          total = gavl_get_num_tracks(dict);
          
          if(!bg_mdb_adjust_num(start, &num, total))
            return 1;

          res = bg_msg_sink_get(be->ctrl.evt_sink);
          
          if(num < total)
            {
            gavl_array_t arr;
            gavl_array_init(&arr);
            gavl_array_copy_sub(&arr, gavl_get_tracks(dict), start, num);
            bg_mdb_set_browse_children_response(res, &arr, msg, &start, 1, total);
            gavl_array_free(&arr);
            }
          else
            {
            bg_mdb_set_browse_children_response(res, gavl_get_tracks(dict), msg, &start, 1, total);
            }
          
          bg_msg_sink_put(be->ctrl.evt_sink);
          }
          break;
        }
      }
    case BG_MSG_NS_VOLUMEMANAGER:
      switch(msg->ID)
        {
        case BG_MSG_ID_VOLUME_ADDED:
          {
          //          const char * id;
          const char * uri;
          const char * klass;
          const char * pos;
          const char * volume_id;
          char * protocol;
          gavl_dictionary_t vol;
          gavl_dictionary_init(&vol);

          volume_id = gavl_msg_get_arg_string_c(msg, 0);
          
          gavl_msg_get_arg_dictionary(msg, 1, &vol);

          if(!(klass = gavl_dictionary_get_string(&vol, GAVL_META_MEDIA_CLASS)))
            return 1;

          uri = gavl_dictionary_get_string(&vol, GAVL_META_URI);

          if(!(pos = strstr(uri, "://")))
            return 1;
          
          if(!strcmp(klass, GAVL_META_MEDIA_CLASS_ROOT_REMOVABLE_AUDIOCD) &&
             !r->mount_audiocd)
            {
            return 1;
            }
          if((!strcmp(klass, GAVL_META_MEDIA_CLASS_ROOT_REMOVABLE_VCD) ||
              !strcmp(klass, GAVL_META_MEDIA_CLASS_ROOT_REMOVABLE_SVCD) ||
              !strcmp(klass, GAVL_META_MEDIA_CLASS_ROOT_REMOVABLE_VIDEODVD)) &&
             !r->mount_videodisk)
            return 1;
          
          protocol = gavl_strndup(uri, pos);

          if(!bg_plugin_find_by_protocol(protocol))
            {
            gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "No plugin for protocol %s", protocol);
            free(protocol);
            return 1;
            }
          free(protocol);
          
          add(be, uri, klass, volume_id);
          gavl_dictionary_free(&vol);
          
#if 0
          /* Store locally */
          gavl_value_init(&vol_store_val);
          vol_store = gavl_value_set_dictionary(&vol_store_val);
          
          gavl_dictionary_set_string_nocopy(vol_store, GAVL_META_ID, bg_sprintf(EXTFS_ID_PREFIX"-%"PRId64,
                                                                                ++fs->extfs_counter));
          
          gavl_dictionary_set_string(vol_store, GAVL_META_URI, uri);
          gavl_dictionary_set_string(vol_store, VOLUME_ID, id);
          
          gavl_array_splice_val_nocopy(&fs->extfs, -1, 0, &vol_store_val);
          /* */

          browse_object_internal(be, &obj, &vol, 0);
          
          m = gavl_dictionary_get_dictionary_create(&obj, GAVL_META_METADATA);

          /* Keep the label */
          gavl_dictionary_set(&vol, GAVL_META_LABEL, NULL);
          gavl_dictionary_update_fields(m, &vol);
          
          bg_mdb_container_set_backend(&obj, MDB_BACKEND_FILESYSTEM);
          
          //          gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Volume added %s", id); 

          bg_mdb_add_root_container(be->ctrl.evt_sink, &obj);
          
          gavl_dictionary_free(&obj);
          gavl_dictionary_free(&vol);
#endif
          }
          break;
        case BG_MSG_ID_VOLUME_REMOVED:
          {
          int i;
          const char * test_id;
          const char * volume_id;
          const gavl_dictionary_t * d;
          const gavl_dictionary_t * m;
          
          volume_id = gavl_msg_get_arg_string_c(msg, 0);
          
          for(i = 0; i < r->removables.num_entries; i++)
            {
            if((d = gavl_value_get_dictionary(&r->removables.entries[i])) &&
               (m = gavl_track_get_metadata(d)) &&
               (test_id = gavl_dictionary_get_string(m, VOLUME_ID)) &&
               !strcmp(test_id, volume_id))
              {
              gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Volume removed %s", volume_id);
              
              /* Send message */
              bg_mdb_delete_root_container(be->ctrl.evt_sink, gavl_dictionary_get_string(m, GAVL_META_ID));
              
              /* Remove locally */
              gavl_array_splice_val(&r->removables, i, 1, NULL);
              break;
              }
            }
          break;
          }
        }
      break;
    case GAVL_MSG_NS_GENERIC:
      switch(msg->ID)
        {
        case GAVL_CMD_QUIT:
          return 0;
          break;
        }
      break;
    case BG_MSG_NS_PARAMETER:
      switch(msg->ID)
        {
        case BG_MSG_SET_PARAMETER:
          {
          gavl_msg_t * resp;
          const char * name = NULL;
          gavl_value_t val;
          
          gavl_value_init(&val);

          bg_msg_get_parameter(msg, &name, &val);
          
          if(!name)
            {
            r->have_params = 1;
            return 1;
            }
          if(!strcmp(name, "mount_audiocd"))
            {
            r->mount_audiocd = val.v.i;
            }
          else if(!strcmp(name, "mount_videodisk"))
            {
            r->mount_videodisk = val.v.i;
            }
          
          /* Pass to core to store it in the config registry */
          if(r->have_params)
            {
            resp = bg_msg_sink_get(be->ctrl.evt_sink);
            bg_msg_set_parameter_ctx(resp, BG_MSG_PARAMETER_CHANGED_CTX, MDB_BACKEND_REMOVABLE, name, &val);
            bg_msg_sink_put(be->ctrl.evt_sink);
            
            gavl_value_free(&val);
            }
          }
          break;
        }
    }  
  
  return 1;
  }

static void destroy_removable(bg_mdb_backend_t * b)
  {
  removable_t * r = b->priv;
  gavl_array_free(&r->removables);
  free(r);
  }


void bg_mdb_create_removable(bg_mdb_backend_t * b)
  {
  removable_t * priv;
  priv = calloc(1, sizeof(*priv));
  b->priv = priv;

  b->parameters = parameters;
  b->flags |= (BE_FLAG_VOLUMES);
  
  b->destroy = destroy_removable;

  bg_controllable_init(&b->ctrl,
                       bg_msg_sink_create(handle_msg, b, 0),
                       bg_msg_hub_create(1));
  }
