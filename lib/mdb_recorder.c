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
#include <gmerlin/recordingdevice.h>

#include <gavl/metatags.h>

#include <mdb_private.h>

typedef struct
  {
  gavl_array_t devices;
  bg_recording_device_registry_t * reg;
  
  gavl_dictionary_t * root;
  
  } recorder_priv_t;

static int is_before(const gavl_value_t * val1, const gavl_dictionary_t * dev)
  {
  const char * str1;
  const char * str2;
  
  const gavl_dictionary_t * dict1;
  dict1 = gavl_value_get_dictionary(val1);
  dict1 = gavl_track_get_metadata(dict1);
  
  str1 = gavl_dictionary_get_string(dict1, GAVL_META_MEDIA_CLASS);
  str2 = gavl_dictionary_get_string(dev, GAVL_META_MEDIA_CLASS);

  if(!strcmp(str1, str2))
    {
    /* Same class, compare by label */
    str1 = gavl_dictionary_get_string(dict1, GAVL_META_LABEL);
    str2 = gavl_dictionary_get_string(dev, GAVL_META_LABEL);

    if(strcmp(str1, str2) < 0)
      return 1;
    else
      return 0;
    }
  else if(!strcmp(str1, GAVL_META_MEDIA_CLASS_VIDEO_RECORDER) &&
          !strcmp(str2, GAVL_META_MEDIA_CLASS_AUDIO_RECORDER))
    return 1;
  else
    return 0;
  }

static void update_parent(bg_mdb_backend_t * b)
  {
  gavl_msg_t * evt;
  recorder_priv_t * priv = b->priv;

  /* Send parent changed */
  
  gavl_track_set_num_children(priv->root, 0, priv->devices.num_entries);
  
  evt = bg_msg_sink_get(b->ctrl.evt_sink);
  gavl_msg_set_id_ns(evt, BG_MSG_DB_OBJECT_CHANGED, BG_MSG_NS_DB);
  gavl_dictionary_set_string(&evt->header, GAVL_MSG_CONTEXT_ID, BG_MDB_ID_RECORDERS);
  
  gavl_msg_set_arg_dictionary(evt, 0, priv->root);
  bg_msg_sink_put(b->ctrl.evt_sink);
  
  }

static void add_dev(bg_mdb_backend_t * b, const gavl_dictionary_t * dev)
  {
  int idx;
  gavl_msg_t * evt;
  gavl_value_t new_val;
  gavl_dictionary_t * new_dict;
  gavl_dictionary_t * new_m;
  recorder_priv_t * priv = b->priv;
  
  //  fprintf(stderr, "add_dev\n");
  //  gavl_dictionary_dump(dev, 2);
  //  fprintf(stderr, "\n");
  
  gavl_value_init(&new_val);
  new_dict = gavl_value_set_dictionary(&new_val);
  new_m = gavl_dictionary_get_dictionary_create(new_dict, GAVL_META_METADATA);
  gavl_dictionary_copy(new_m, dev);

  gavl_dictionary_set_string_nocopy(new_m, GAVL_META_ID,
                                    gavl_sprintf("%s/%s", BG_MDB_ID_RECORDERS,
                                                 gavl_dictionary_get_string(dev, GAVL_META_ID)));
  gavl_metadata_add_src(new_m, GAVL_META_SRC, NULL, gavl_dictionary_get_string(dev, GAVL_META_URI));
  gavl_dictionary_set(new_m, GAVL_META_URI, NULL);


  if(!priv->devices.num_entries)
    idx = 0;
  else if(priv->devices.num_entries == 1)
    {
    if(is_before(&priv->devices.entries[0], dev))
      idx = 1;
    else
      idx = 0;
    }
  else
    {
    int i;
    
    idx= priv->devices.num_entries;
    
    for(i = 0; i < priv->devices.num_entries; i++)
      {
      if(!is_before(&priv->devices.entries[i], dev))
        {
        idx = i;
        break;
        }
      }
    }

  gavl_array_splice_val_nocopy(&priv->devices, idx, 0, &new_val);

  /* Send splice children */
  evt = bg_msg_sink_get(b->ctrl.evt_sink);
  bg_msg_set_splice_children(evt, BG_MSG_DB_SPLICE_CHILDREN,
                             BG_MDB_ID_RECORDERS, 1, idx, 0, &priv->devices.entries[idx]);
  bg_msg_sink_put(b->ctrl.evt_sink);

  update_parent(b);
  }

static int handle_local_msg(void * data, gavl_msg_t * msg)
  {
  gavl_msg_t * res;
  bg_mdb_backend_t * b = data;
  recorder_priv_t * priv = b->priv;
  
  switch(msg->NS)
    {
    case BG_MSG_NS_DB:
      {
      switch(msg->ID)
        {
        case BG_FUNC_DB_BROWSE_OBJECT:
          {
          int idx;
          const char * ctx_id = gavl_dictionary_get_string(&msg->header, GAVL_MSG_CONTEXT_ID);

          if((idx = gavl_get_track_idx_by_id_arr(&priv->devices, ctx_id)) >= 0)
            {
            res = bg_msg_sink_get(b->ctrl.evt_sink);
            bg_mdb_set_browse_obj_response(res, gavl_value_get_dictionary(&priv->devices.entries[idx]),
                                           msg, idx, priv->devices.num_entries);
            bg_msg_sink_put(b->ctrl.evt_sink);
            }
          
          }
          break;
        case BG_FUNC_DB_BROWSE_CHILDREN:
          {
          const char * ctx_id;
          int start, num, one_answer;
          bg_mdb_get_browse_children_request(msg, &ctx_id, &start, &num, &one_answer);
          
          if(!bg_mdb_adjust_num(start, &num, priv->devices.num_entries))
            return 1;

          if(num < priv->devices.num_entries)
            {
            int i;
            gavl_array_t tmp_array;
            gavl_array_init(&tmp_array);
            for(i = 0; i < num; i++)
              gavl_array_splice_val(&tmp_array, -1, 0, &priv->devices.entries[i+start]);

            res = bg_msg_sink_get(b->ctrl.evt_sink);
            bg_mdb_set_browse_children_response(res, &tmp_array, msg, &start, 1, priv->devices.num_entries);
            bg_msg_sink_put(b->ctrl.evt_sink);
            }
          else
            {
            res = bg_msg_sink_get(b->ctrl.evt_sink);
            bg_mdb_set_browse_children_response(res, &priv->devices, msg, &start, 1, priv->devices.num_entries);
            bg_msg_sink_put(b->ctrl.evt_sink);
            }
          }
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
        case GAVL_MSG_RESOURCE_ADDED:
          {
          const char * id;
          gavl_dictionary_t dev;
          gavl_dictionary_init(&dev);
          id = gavl_dictionary_get_string(&msg->header, GAVL_MSG_CONTEXT_ID);

          if(gavl_get_track_idx_by_id_arr(&priv->devices, id) >= 0)
            return 1;

          //          fprintf(stderr, "Resource added %s\n", id);
          gavl_msg_get_arg_dictionary(msg, 0, &dev);
          add_dev(b, &dev);
          gavl_dictionary_free(&dev);
          break;
          }
        case GAVL_MSG_RESOURCE_DELETED:
          {
          char * id;
          int idx;
          
          id = gavl_sprintf("%s/%s", BG_MDB_ID_RECORDERS,
                            gavl_dictionary_get_string(&msg->header, GAVL_MSG_CONTEXT_ID));

          // fprintf(stderr, "Resource deleted %s\n", id);

          if((idx = gavl_get_track_idx_by_id_arr(&priv->devices, id)) >= 0)
            {
            res = bg_msg_sink_get(b->ctrl.evt_sink);
            bg_msg_set_splice_children(res, BG_MSG_DB_SPLICE_CHILDREN,
                                       BG_MDB_ID_RECORDERS, 1, idx, 1, NULL);
            bg_msg_sink_put(b->ctrl.evt_sink);
            
            gavl_array_splice_val_nocopy(&priv->devices, idx, 1, NULL);
            update_parent(b);
            }
          free(id);
          break;
          }
        }
      break;
      }
    }
  return 1;
  }

static int ping_func_recorder(bg_mdb_backend_t * b)
  {
  recorder_priv_t * priv = b->priv;
  return bg_recording_device_registry_update(priv->reg);
  }

static void destroy_func_recorder(bg_mdb_backend_t * b)
  {
  recorder_priv_t * priv = b->priv;
  gavl_array_free(&priv->devices);
  bg_recording_device_registry_destroy(priv->reg);
  free(priv);
  }

void bg_mdb_create_recorder(bg_mdb_backend_t * b)
  {
  recorder_priv_t * priv;

  priv = calloc(1, sizeof(*priv));
  b->priv = priv;
  b->destroy = destroy_func_recorder;
  b->ping_func = ping_func_recorder;

  priv->root = bg_mdb_get_root_container(b->db, GAVL_META_MEDIA_CLASS_ROOT_RECORDERS);
  bg_mdb_container_set_backend(priv->root, MDB_BACKEND_RECORDER);

  priv->reg = bg_recording_device_registry_create();

  bg_controllable_init(&b->ctrl,
                       bg_msg_sink_create(handle_local_msg, b, 0),
                       bg_msg_hub_create(1));
  
  bg_msg_hub_connect_sink(bg_recording_device_registry_get_msg_hub(priv->reg),
                          b->ctrl.cmd_sink);
  
  } 
