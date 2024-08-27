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



/*
 * This is the replacement for the old xml-based media tree. It's used for
 * all albums, which are user editable: Favorites and
 * "Library" (a completely user defined area)
 *
 * The implementation is straightforward:
 * 
 * Every item is assigned a GUID. The filename is the same as the GUID
 * The database ID will be like:
 *
 * /favorites/c918035c-1710-487f-a871-560fb63d0321
 *
 * Or:
 * /library/c918035c-1710-487f-a871-560fb63d0321/1a12a147-bae7-4dcc-a362-b34eb24f08fd
 *
 * Containers are represented as directories (name == GUID) which contain
 * the files. The container metadata (label, class, child IDs) are in a file called INDEX
 * in the directory
 *
 * Non-container items are regular files are the gavl track elemenents encoded as xml
 *
 * 
 *
 */

#include <config.h>
#include <unistd.h>
#include <string.h>
#include <uuid/uuid.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>

#include <gmerlin/mdb.h>
#include <gmerlin/utils.h>
#include <gmerlin/bggavl.h>

#include <mdb_private.h>
#include <gavl/metatags.h>

#include <gmerlin/translation.h>
#include <gmerlin/log.h>
#define LOG_DOMAIN "mdb.xml"

#define META_CHILD_IDS "ChildIDs"

/* Maximum number of children to transmit at once (so clients can update) */
#define MAX_CHILDREN 20

typedef struct
  {
  gavl_dictionary_t * favorites;
  gavl_dictionary_t * library;
  
  const char * favorites_id;
  const char * library_id;
  } xml_t;

static void save_dir_index(bg_mdb_backend_t * be, const char * dir, gavl_dictionary_t * d);
static gavl_dictionary_t * browse_object(bg_mdb_backend_t * b, const char * id);


static const char * path_to_id(bg_mdb_backend_t * be, const char * path)
  {
  return path + (strlen(be->db->path) + 4 /* /xml */);
  }

static void set_editable(const char * id, gavl_dictionary_t * dict)
  {
  const char * klass;
  gavl_dictionary_t * m;

  if(!(m = gavl_track_get_metadata_nc(dict)) ||
     !(klass = gavl_dictionary_get_string(m, GAVL_META_CLASS)) ||
     !gavl_string_starts_with(klass, "container"))
    return;

  bg_mdb_set_editable(dict);
  
  if(!strcmp(id, bg_mdb_get_klass_id(GAVL_META_CLASS_ROOT_FAVORITES)))
    {
    bg_mdb_add_can_add(dict, "item.audio*");
    bg_mdb_add_can_add(dict, "item.video*");
    bg_mdb_add_can_add(dict, "item.image*");
    bg_mdb_add_can_add(dict, GAVL_META_CLASS_LOCATION);
    bg_mdb_add_can_add(dict, GAVL_META_CLASS_BROADCAST);
    }
  else if(!strcmp(klass, GAVL_META_CLASS_CONTAINER_TV))
    {
    bg_mdb_add_can_add(dict, GAVL_META_CLASS_VIDEO_BROADCAST);
    bg_mdb_add_can_add(dict, GAVL_META_CLASS_LOCATION);
    bg_mdb_add_can_add(dict, GAVL_META_CLASS_BROADCAST);
    }
  else if(!strcmp(klass, GAVL_META_CLASS_CONTAINER_RADIO))
    {
    bg_mdb_add_can_add(dict, GAVL_META_CLASS_AUDIO_BROADCAST);
    bg_mdb_add_can_add(dict, GAVL_META_CLASS_LOCATION);
    bg_mdb_add_can_add(dict, GAVL_META_CLASS_BROADCAST);
    }
  else if(!strcmp(klass, GAVL_META_CLASS_PLAYLIST))
    {
    bg_mdb_add_can_add(dict, GAVL_META_CLASS_SONG);
    }
  
  }

static void item_to_storage(bg_mdb_backend_t * b, gavl_dictionary_t * dict)
  {
  //  const char * klass;
  gavl_dictionary_t * m;
  gavl_dictionary_get_dictionary_create(dict, GAVL_META_METADATA);
  bg_mdb_delete_http_uris(dict);
  bg_mdb_clear_thumbnail_uris(dict);
  bg_mdb_clear_editable(dict);

  m = gavl_track_get_metadata_nc(dict);
  
  gavl_dictionary_set(m, GAVL_META_NEXT_ID, NULL);
  gavl_dictionary_set(m, GAVL_META_PREVIOUS_ID, NULL);
  //  gavl_dictionary_set(m, GAVL_META_NUM_CHILDREN, NULL);
  //  gavl_dictionary_set(m, GAVL_META_NUM_ITEM_CHILDREN, NULL);
  //  gavl_dictionary_set(m, GAVL_META_NUM_CONTAINER_CHILDREN, NULL);

  }

static void item_from_storage(bg_mdb_backend_t * b,
                              gavl_dictionary_t * dict, const char * parent_id)
  {
  const char * klass;
  char * id;
  gavl_dictionary_t * m;
  

  /* Might be left from earlier versions */
  bg_mdb_clear_editable(dict);
  
  if(!(m = gavl_track_get_metadata_nc(dict)))
    return;

  if((klass = gavl_dictionary_get_string(m, GAVL_META_CLASS)))
    {
    /* Set label for song */
    if(!strcmp(klass, GAVL_META_CLASS_SONG))
      {
      char * artist = gavl_metadata_join_arr(m, GAVL_META_ARTIST, ", ");

      gavl_dictionary_set_string_nocopy(m, GAVL_META_LABEL,
                                        bg_sprintf("%s: %s", artist, gavl_dictionary_get_string(m, GAVL_META_TITLE)));
      free(artist);
      }
    }
  
  if(!strcmp(parent_id, "/"))
    id = bg_sprintf("%s%s", parent_id, gavl_dictionary_get_string(m, GAVL_META_ID));
  else
    id = bg_sprintf("%s/%s", parent_id, gavl_dictionary_get_string(m, GAVL_META_ID));
  set_editable(id, dict);
  gavl_dictionary_set_string_nocopy(m, GAVL_META_ID, id);

  bg_mdb_add_http_uris(b->db, dict);

  //  fprintf(stderr, "Added http uris\n");
  //  gavl_dictionary_dump(dict, 2);
  }


static gavl_dictionary_t * load_dir_index(bg_mdb_backend_t * be, const char * dir)
  {
  gavl_dictionary_t * ret;
  char * filename = bg_sprintf("%s/INDEX", dir);
  
  ret = gavl_dictionary_create();
  if(access(filename, R_OK) || !bg_dictionary_load_xml(ret, filename, "INDEX"))
    {
    gavl_dictionary_destroy(ret);
    ret = NULL;
    }

  if(ret)
    {
    const char * id;
    char * parent_id;
    id = path_to_id(be, dir);
    parent_id = bg_mdb_get_parent_id(id);

    id += strlen(parent_id);

    if(*id == '/')
      id++;

    gavl_track_set_id(ret, id);
    
    item_from_storage(be, ret, parent_id);

    /* Update durations automatically */
#if 0
    if(!gavl_dictionary_get(metadata, GAVL_META_APPROX_DURATION))
      {
      // update_container_duration(ret, dir);
      save_dir_index(be, dir, ret);
      }
#endif
    
    free(parent_id);
    }
  
  free(filename);
  return ret;
  }

static void arr_get_num_children(const gavl_array_t * arr, const char * directory,
                                 int * containers, int * items, gavl_time_t * duration)
  {
  int i;
  char * tmp_string;
  struct stat st;
  gavl_time_t track_duration;
  
  *duration = 0;
  
  for(i = 0; i < arr->num_entries; i++)
    {
    tmp_string = bg_sprintf("%s/%s", directory, gavl_string_array_get(arr, i));

    if(!stat(tmp_string, &st))
      {
      if(S_ISDIR(st.st_mode))
        {
        (*containers)++;
        *duration = GAVL_TIME_UNDEFINED;
        }
      else if(S_ISREG(st.st_mode))
        {
        (*items)++;

        if(*duration != GAVL_TIME_UNDEFINED)
          {
          gavl_dictionary_t track;
          const gavl_dictionary_t * metadata;

          gavl_dictionary_init(&track);
          
          if(!bg_dictionary_load_xml(&track, tmp_string, "TRACK"))
            {
            gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Couldn't load %s", tmp_string);
            }

          metadata = gavl_dictionary_get_dictionary_create(&track, GAVL_META_METADATA);

          track_duration = GAVL_TIME_UNDEFINED;

          if(!gavl_dictionary_get_long(metadata, GAVL_META_APPROX_DURATION, &track_duration) ||
             (track_duration == GAVL_TIME_UNDEFINED))
            *duration = GAVL_TIME_UNDEFINED;
          else
            *duration += track_duration;
          
          gavl_dictionary_reset(&track);
          }

        }
      }
    free(tmp_string);
    }
  
  }

static void get_num_children(const gavl_dictionary_t * dict, const char * directory, int * containers, int * items,
                             gavl_time_t * duration)
  {
  const gavl_array_t * arr;

  *containers = 0;
  *items = 0;
  
  if(!(arr = gavl_dictionary_get_array(dict, META_CHILD_IDS)))
    return;

  arr_get_num_children(arr, directory, containers, items, duration);
  }

static void save_dir_index(bg_mdb_backend_t * be, const char * dir, gavl_dictionary_t * d)
  {
  char * filename = bg_sprintf("%s/INDEX", dir);

  item_to_storage(be, d);
  bg_dictionary_save_xml(d, filename, "INDEX");
  free(filename);
  }

static char * id_to_path(bg_mdb_backend_t * be, const char * id)
  {
  return bg_sprintf("%s/xml%s", be->db->path, id);
  }


static int do_add(bg_mdb_backend_t * b,
                  gavl_dictionary_t * dir_idx,
                  int idx,
                  gavl_dictionary_t * add,
                  const char * parent_id)
  {
  gavl_array_t * child_ids;
  const char * klass;
  char * filename;
  gavl_dictionary_t * m;
  char uuid_str[37];
  uuid_t uuid;
  char * dir = NULL;

  int no_duplicates = 0;
  
  dir = id_to_path(b, parent_id);

  if(!strcmp(parent_id, bg_mdb_get_klass_id(GAVL_META_CLASS_ROOT_FAVORITES)))
    {
    const char * uri;
    const gavl_dictionary_t * m;
    no_duplicates = 1;

    if((m = gavl_track_get_metadata(add)) &&
       gavl_metadata_get_src(m, GAVL_META_SRC, 0, NULL, &uri))
      {
      bg_get_filename_hash(uri, uuid_str);
      }
    else
      {
      uuid_generate(uuid);
      uuid_unparse(uuid, uuid_str);
      }
    }
  else
    {
    uuid_generate(uuid);
    uuid_unparse(uuid, uuid_str);
    }
  
  item_to_storage(b, add);

  gavl_track_set_id(add, uuid_str);
  
  child_ids = gavl_dictionary_get_array_nc(dir_idx, META_CHILD_IDS);

  m = gavl_track_get_metadata_nc(add);

  
  if(!(klass = gavl_dictionary_get_string(m, GAVL_META_CLASS)) ||
     !bg_mdb_can_add(dir_idx, klass))
    {
    gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Cannot add item, unknown or unsupported media class (%s)",
           klass);
    return 0;
    }
  
  if(no_duplicates && gavl_string_array_indexof(child_ids, uuid_str) >= 0)
    {
    gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Skipping duplicate entry for %s", parent_id);
    return 0;
    }
  
  gavl_string_array_insert_at(child_ids, idx, uuid_str);
  
  if(gavl_string_starts_with(klass, "container"))
    {
    gavl_value_t val;
    gavl_dictionary_t * tmp_dict;
    char * id;

    id = bg_sprintf("%s/%s", parent_id, uuid_str);
    
    filename = bg_sprintf("%s/%s", dir, uuid_str);
    gavl_ensure_directory(filename, 0);
    free(filename);

    tmp_dict = gavl_dictionary_create();
    gavl_dictionary_copy(tmp_dict, add);
    gavl_value_init(&val);
    gavl_value_set_array(&val);
    gavl_dictionary_set_nocopy(tmp_dict, META_CHILD_IDS, &val);

    set_editable(id, tmp_dict);
    free(id);
    
    filename = bg_sprintf("%s/%s/INDEX", dir, uuid_str);
    bg_dictionary_save_xml(tmp_dict, filename, "INDEX");
    
    gavl_dictionary_destroy(tmp_dict);
    free(filename);
    }
  else
    {
    filename = bg_sprintf("%s/%s", dir, uuid_str);
    bg_dictionary_save_xml(add, filename, "TRACK");
    free(filename);
    }

  free(dir);

  /* Need to set the correct stuff for BG_MSG_DB_SPLICE_CHILDREN */
  item_from_storage(b, add, parent_id);
  
  return 1;
  }


static int splice(bg_mdb_backend_t * b, const char * ctx_id, int last, int idx, int del, gavl_array_t * add_arr)
  {
  int i;
  gavl_dictionary_t * dir_idx = NULL;
  char * parent_directory = NULL;

  int ret = 0;
  gavl_value_t val;
  gavl_array_t * child_ids;
  char * tmp_string;
  gavl_msg_t * res;
  int idx_res;
  gavl_dictionary_t dict;
  gavl_dictionary_t * root_dict = NULL ;
  xml_t * xml = b->priv;
  const char * klass;
  int num_added = 0;
  int containers = 0;
  int items = 0;
  gavl_time_t duration;

  gavl_dictionary_t * metadata;
  gavl_dictionary_t * track;
  
  gavl_dictionary_init(&dict);

  if(!ctx_id)
    goto fail;
  
  //  fprintf(stderr, "splice: %s\n", ctx_id);
  
  parent_directory = id_to_path(b, ctx_id); 

  if((klass = bg_mdb_get_klass_from_id(ctx_id)))
    {
    if(!strcmp(klass, GAVL_META_CLASS_ROOT_LIBRARY))
      {
      root_dict =  xml->library;
      }
    else if(!strcmp(klass, GAVL_META_CLASS_ROOT_FAVORITES))
      {
      root_dict =  xml->favorites;
      }
    }
  
  if((dir_idx = load_dir_index(b, parent_directory)))
    {
    child_ids = gavl_dictionary_get_array_nc(dir_idx, META_CHILD_IDS);
    }
  else
    {
    gavl_value_init(&val);
    dir_idx = gavl_dictionary_create();
    gavl_dictionary_get_dictionary_create(dir_idx, GAVL_META_METADATA);
    child_ids = gavl_value_set_array(&val);
    gavl_dictionary_set_nocopy(dir_idx, META_CHILD_IDS, &val);
    }

  /* Set Media class */

  if(root_dict)
    {
    //    fprintf(stderr, "Root dict:\n");
    //    gavl_dictionary_dump(root_dict, 2);

    gavl_dictionary_merge2(gavl_track_get_metadata_nc(dir_idx),
                           gavl_track_get_metadata(root_dict));
    }
  set_editable(ctx_id, dir_idx);
  
  if((idx < 0) || (idx > child_ids->num_entries)) // Append
    idx = child_ids->num_entries;
  
  if(del)
    {
    if(del < 0)
      del = child_ids->num_entries - idx;
    
    if(idx + del > child_ids->num_entries)
      del = child_ids->num_entries - idx;
    }

  idx_res = idx;
  
  /* Delete entries, folders are deleted recursively */
  
  if(del > 0)
    {
    for(i = 0; i < del; i++)
      {
      tmp_string = bg_sprintf("%s/%s", parent_directory,
                              gavl_value_get_string(&child_ids->entries[idx + i]));
      bg_remove_file(tmp_string);
      free(tmp_string);
      }
    gavl_array_splice_val(child_ids, idx, del, NULL);
    }

  for(i = 0; i < add_arr->num_entries; i++)
    {
    if((track = gavl_value_get_dictionary_nc(&add_arr->entries[i])))
      {
      if(do_add(b, dir_idx, idx, track, ctx_id))
        {
        idx++;
        num_added++;
        }
      }
    }
  
  /* Send splice event */
  /* TODO: Don't send this when an error occurred */
  
  if(!num_added && !del)
    goto fail;
  
  res = bg_msg_sink_get(b->ctrl.evt_sink);
  gavl_msg_set_id_ns(res, BG_MSG_DB_SPLICE_CHILDREN, BG_MSG_NS_DB);
  gavl_dictionary_set_string(&res->header, GAVL_MSG_CONTEXT_ID, ctx_id);

  gavl_msg_set_last(res, 1);
  
  gavl_msg_set_arg_int(res, 0, idx_res); // idx
  gavl_msg_set_arg_int(res, 1, del); // del

  if(!num_added)
    gavl_msg_set_arg(res, 2, NULL);
  else
    gavl_msg_set_arg_array(res, 2, add_arr);
  
  bg_msg_sink_put(b->ctrl.evt_sink);

  /* Send update event for parent */
  res = bg_msg_sink_get(b->ctrl.evt_sink);
  gavl_msg_set_id_ns(res, BG_MSG_DB_OBJECT_CHANGED, BG_MSG_NS_DB);

  gavl_dictionary_set_string(&res->header, GAVL_MSG_CONTEXT_ID, ctx_id);
  
  metadata = gavl_dictionary_get_dictionary_create(&dict, GAVL_META_METADATA);
  duration = GAVL_TIME_UNDEFINED;
  
  arr_get_num_children(child_ids, parent_directory, &containers, &items, &duration);

  if(duration != GAVL_TIME_UNDEFINED)
    gavl_dictionary_set_long(metadata, GAVL_META_APPROX_DURATION, duration);
  
  gavl_track_set_num_children(&dict, containers, items);
  
  gavl_dictionary_update_fields(gavl_track_get_metadata_nc(dir_idx), metadata);
  
  gavl_track_set_num_children(dir_idx, containers, items);
  
  gavl_msg_set_arg_dictionary(res, 0, &dict);
  bg_msg_sink_put(b->ctrl.evt_sink);
  
  ret = 1;
  fail:

  if(num_added || del)
    save_dir_index(b, parent_directory, dir_idx);
  
  if(parent_directory)
    free(parent_directory);

  gavl_dictionary_free(&dict);

  if(dir_idx)
    gavl_dictionary_destroy(dir_idx);
  
  return ret;
  }

static void destroy_xml(bg_mdb_backend_t * b)
  {
  xml_t * p = b->priv;
  free(p);
  }


static gavl_dictionary_t * browse_object(bg_mdb_backend_t * b, const char * id)
  {
  int ret = 0;
  char * path = NULL;  
  struct stat st;
  char * parent_id = NULL;
  
  gavl_dictionary_t * dict = NULL;
  
  path = id_to_path(b, id);
  
  if(stat(path, &st))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Couldn't stat %s: %s", path, strerror(errno));
    goto fail;
    }
  if(S_ISDIR(st.st_mode))
    {
    if(!(dict = load_dir_index(b, path)))
      goto fail;

    gavl_dictionary_set(dict, META_CHILD_IDS, NULL);
    
    }
  else
    {
    const char * pos;
    
    dict = gavl_dictionary_create();
    if(!bg_dictionary_load_xml(dict, path, "TRACK"))
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Couldn't load %s", path);
      goto fail;
      }
    if((pos = strrchr(id, '/')))
      gavl_track_set_id(dict, pos+1);
    
    parent_id = bg_mdb_get_parent_id(id);
    item_from_storage(b, dict, parent_id);
    }
    
  ret = 1;
  fail:

  if(path)
    free(path);
  if(parent_id)
    free(parent_id);

  if(!ret && dict)
    {
    gavl_dictionary_destroy(dict);
    dict = NULL;
    }

  
  return dict;
  }

static int browse_children(bg_mdb_backend_t * b, const char * id, gavl_array_t * ret,
                           int start, int num, int * total)
  {
  int result = 0;
  int i;
  gavl_array_t * child_ids;
  char * path = NULL;
  const char * str;
  char * child_id;
  int changed = 0;
  gavl_dictionary_t * child_dict = NULL;
  gavl_dictionary_t * child_m = NULL;
  gavl_dictionary_t * parent_dict = NULL;

  path = id_to_path(b, id);

  //  fprintf(stderr, "XML Browse children %s\n", id);
  
  if(!(parent_dict = load_dir_index(b, path)) ||
     !(child_ids = gavl_dictionary_get_array_nc(parent_dict, META_CHILD_IDS)))
    goto fail;

  if(total)
    *total = child_ids->num_entries;

  if(!bg_mdb_adjust_num(start, &num, child_ids->num_entries))
    goto fail;

  i = 0;

  while(i < num)
    //  for(i = 0; i < num; i++)
    {
    gavl_value_t val;

    //    gavl_dictionary_t * last_child_m = NULL;
    //    char * last_id = NULL;
    
    if((str = gavl_value_get_string(&child_ids->entries[i + start])))
      {
      child_id = bg_sprintf("%s/%s", id, str);

      if(!(child_dict = browse_object(b, child_id)))
        {
        changed++;
        gavl_array_splice_val(child_ids, i, 1, NULL);
        num--;
        continue;
        }
      

#if 0      
      if(last_child_m)
        gavl_dictionary_set_string(last_child_m, GAVL_META_NEXT_ID, child_id);

      if(last_id)
        {
        gavl_dictionary_set_string_nocopy(child_m, GAVL_META_PREVIOUS_ID, last_id);
        last_id = NULL;
        }
      fprintf(stderr, "browse_children xml\n");
      gavl_dictionary_dump(child_dict, 2);
      fprintf(stderr, "\n");
#endif
      
      gavl_value_init(&val);
      gavl_value_set_dictionary_nocopy(&val, child_dict);
      gavl_array_splice_val_nocopy(ret, -1, 0, &val);

      
      child_id = NULL;

      }
    i++;
    }

  /* Set next and previous */
  for(i = 0; i < ret->num_entries; i++)
    {
    child_dict = gavl_value_get_dictionary_nc(&ret->entries[i]);
    child_m = gavl_track_get_metadata_nc(child_dict);
    
    if(i + start > 0)
      {
      gavl_dictionary_set_string_nocopy(child_m, GAVL_META_PREVIOUS_ID,
                                        bg_sprintf("%s/%s", id, gavl_string_array_get(child_ids, i-1)));
      }

    if(i  + start < ret->num_entries - 1)
      {
      gavl_dictionary_set_string_nocopy(child_m, GAVL_META_NEXT_ID,
                                        bg_sprintf("%s/%s", id, gavl_string_array_get(child_ids, i+1)));
                                        
      }
    

    }
    
  
  result = 1;
  
  if(changed)
    save_dir_index(b, path, parent_dict);
  
  fail:

  if(parent_dict)
    gavl_dictionary_destroy(parent_dict);
  
  if(path)
    free(path);

  return result;
  
  }

static int handle_msg_xml(void * priv, gavl_msg_t * msg)
  {
  bg_mdb_backend_t * b = priv;
  
  // fprintf(stderr, "handle_msg_xml %d %d\n", msg->ID, msg->NS);
  
  switch(msg->NS)
    {
    case BG_MSG_NS_DB:
      {
      switch(msg->ID)
        {
        case BG_FUNC_DB_BROWSE_OBJECT:
          {
          const char * pos;

          const char * ctx_id;
          gavl_dictionary_t * dict;
          gavl_dictionary_t * m;
          gavl_msg_t * res;
          gavl_value_t val;
          int ix;
          char * parent_id = NULL;
          char * parent_path = NULL;
          int item_idx = -1, total = -1;
          
          ctx_id = gavl_dictionary_get_string(&msg->header, GAVL_MSG_CONTEXT_ID);
          dict = browse_object(b, ctx_id);
          m = gavl_track_get_metadata_nc(dict);
          
          /* Set next and previous */
          
          parent_id = bg_mdb_get_parent_id(ctx_id);
          
          if(strcmp(parent_id, "/"))
            {
            gavl_dictionary_t * idx;
            const gavl_array_t * child_ids;
            
            parent_path = id_to_path(b, parent_id); 
            
            idx = load_dir_index(b, parent_path);

            pos = strrchr(ctx_id, '/');
            
            if((child_ids = gavl_dictionary_get_array_nc(idx, META_CHILD_IDS)) &&
               ((ix = gavl_string_array_indexof(child_ids, pos + 1)) >= 0))
              {
              if(ix > 0)
                {
                gavl_dictionary_set_string_nocopy(m, GAVL_META_PREVIOUS_ID,
                                                  bg_sprintf("%s/%s", parent_id, gavl_string_array_get(child_ids, ix-1)));
                }
              if(ix < child_ids->num_entries - 1)
                {
                gavl_dictionary_set_string_nocopy(m, GAVL_META_NEXT_ID,
                                                  bg_sprintf("%s/%s", parent_id, gavl_string_array_get(child_ids, ix+1)));
                }
              item_idx = ix;
              total = child_ids->num_entries;
              }
            
            gavl_dictionary_destroy(idx);

            if(parent_path)
              free(parent_path);
            
            }
          
          if(parent_id)
            free(parent_id);
          
          
          //          fprintf(stderr, "Browse object:\n");
          //          gavl_dictionary_dump(dict, 2);
          
          gavl_value_init(&val);
          gavl_value_set_dictionary_nocopy(&val, dict);
          
          res = bg_msg_sink_get(b->ctrl.evt_sink);

          bg_mdb_set_browse_obj_response(res, dict, msg, item_idx, total);
          bg_msg_sink_put(b->ctrl.evt_sink);
          }
          break;
        case BG_FUNC_DB_BROWSE_CHILDREN:
          {
          gavl_array_t arr;
          /* Flush messages */
          gavl_msg_t * res;
          const char * ctx_id;

          int start, num, total = 0;
          int one_answer;
          
          gavl_array_init(&arr);
          ctx_id = gavl_dictionary_get_string(&msg->header, GAVL_MSG_CONTEXT_ID);

          bg_mdb_get_browse_children_request(msg, &ctx_id, &start, &num, &one_answer);

          //   fprintf(stderr, "XML Browse children %s %d %d %d\n", ctx_id, start, num, one_answer);
          
          if(browse_children(b, ctx_id, &arr, start, num, &total))
            {
            if(one_answer)
              {
              res = bg_msg_sink_get(b->ctrl.evt_sink);
              bg_mdb_set_browse_children_response(res, &arr, msg, &start, 1, total);
              bg_msg_sink_put(b->ctrl.evt_sink);
              }
            else
              {
              int end, last = 0;
              gavl_array_t arr_sub;
              int start1 = start;
              
              end = start + arr.num_entries;
              
              gavl_array_init(&arr_sub);
              
              while(!last)
                {
                gavl_array_copy_sub(&arr_sub, &arr, start - start1, MAX_CHILDREN);
                //fprintf(stderr, "XML Browse children 1 %d %d\n", start, arr_sub.num_entries);
                if(start + arr_sub.num_entries == end)
                  last = 1;
                
                res = bg_msg_sink_get(b->ctrl.evt_sink);
                bg_mdb_set_browse_children_response(res, &arr_sub, msg, &start, last, total);
                bg_msg_sink_put(b->ctrl.evt_sink);
                
                gavl_array_reset(&arr_sub);
                }
              
              }

            }
          
          gavl_array_free(&arr);
          }
          break;
        case BG_CMD_DB_SPLICE_CHILDREN:
          {
          int last = 0;
          int idx = 0;
          int del = 0;
          gavl_value_t add;
          gavl_array_t add_arr;
          gavl_value_init(&add);
          gavl_array_init(&add_arr);
          
          gavl_msg_get_splice_children(msg, &last, &idx, &del, &add);

          bg_tracks_resolve_locations(&add, &add_arr, BG_INPUT_FLAG_GET_FORMAT);

          //          fprintf(stderr, "Resolve children\n");
          //          gavl_array_dump(&add_arr, 2 );
          
          splice(b, gavl_dictionary_get_string(&msg->header, GAVL_MSG_CONTEXT_ID), last, idx, del, &add_arr);
          gavl_value_free(&add);
          gavl_array_free(&add_arr);
          }
          break;
        case BG_CMD_DB_SORT:
          {
          const char * ctx_id;
          gavl_dictionary_t * dir_idx = NULL;
          gavl_array_t * child_ids;
          char * parent_directory = NULL;
          
          ctx_id = gavl_dictionary_get_string(&msg->header, GAVL_MSG_CONTEXT_ID);

          //          fprintf(stderr, "xml sort %s\n", ctx_id);

          parent_directory = id_to_path(b, ctx_id);
          
          if((dir_idx = load_dir_index(b, parent_directory)) &&
             (child_ids = gavl_dictionary_get_array_nc(dir_idx, META_CHILD_IDS)))
            {
            int i;
            gavl_msg_t * msg;
            const gavl_dictionary_t * dict;
            const char * id;
            const char * pos;
            gavl_array_t children;

            //            fprintf(stderr, "xml sort 1\n");
            
            gavl_array_init(&children);
            browse_children(b, ctx_id, &children, 0, -1, NULL);

            /* We use the dir_idx as parent for sorting the tracks */
            bg_mdb_tracks_sort(&children);
            
            /* Extract child IDs */

            gavl_array_splice_val(child_ids, 0, -1, NULL);
            for(i = 0; i < children.num_entries; i++)
              {
              if((dict = gavl_value_get_dictionary(&children.entries[i])) &&
                 (id = gavl_track_get_id(dict)))
                {
                if((pos = strrchr(id, '/')))
                  pos++;
                else
                  pos = id;

                gavl_string_array_add(child_ids, pos);
                }
              }
            
            save_dir_index(b, parent_directory, dir_idx);

            msg = bg_msg_sink_get(b->ctrl.evt_sink);
            gavl_msg_set_id_ns(msg, BG_MSG_DB_SPLICE_CHILDREN, BG_MSG_NS_DB);
            gavl_dictionary_set_string(&msg->header, GAVL_MSG_CONTEXT_ID, ctx_id);
            gavl_msg_set_last(msg, 1);
            
            gavl_msg_set_arg_int(msg, 0, 0); // idx
            gavl_msg_set_arg_int(msg, 1, children.num_entries); // del
            gavl_msg_set_arg_array(msg, 2, &children);
            bg_msg_sink_put(b->ctrl.evt_sink);
            
            gavl_array_free(&children);
            }
          }
          break;
        }
      break;
      }
    case GAVL_MSG_NS_GENERIC:
      switch(msg->ID)
        {
        case GAVL_CMD_QUIT:
          return 0;
        }
      break;
    }
  
  return 1;
  }

static gavl_dictionary_t * create_root_folder(bg_mdb_backend_t * b, const char * media_class,
                                              const char ** id)
  {
  char * directory;
  gavl_dictionary_t * container;
  gavl_dictionary_t * m;
  gavl_dictionary_t * idx;
  int num_items = 0;
  int num_containers = 0;
  gavl_time_t duration = GAVL_TIME_UNDEFINED;
  
  container = bg_mdb_get_root_container(b->db, media_class);
  bg_mdb_container_set_backend(container, MDB_BACKEND_XML);

  m = gavl_track_get_metadata_nc(container);
                        
  *id = gavl_dictionary_get_string(m, GAVL_META_ID);
  
  directory = id_to_path(b, *id);
  
  gavl_ensure_directory(directory, 0);

  if((idx = load_dir_index(b, directory)))
    get_num_children(idx, directory, &num_containers, &num_items, &duration);

  gavl_track_set_num_children(container, num_containers, num_items);
  
  if(idx)
    gavl_dictionary_destroy(idx);
  
  free(directory);
  set_editable(*id, container);
  return container;
  }

void bg_mdb_create_xml(bg_mdb_backend_t * b)
  {
  xml_t * priv;
  
  priv = calloc(1, sizeof(*priv));

  priv->favorites = create_root_folder(b, GAVL_META_CLASS_ROOT_FAVORITES, &priv->favorites_id);

  //  fprintf(stderr, "Created favorites: %p\n", priv->favorites);

  priv->library   = create_root_folder(b, GAVL_META_CLASS_ROOT_LIBRARY,   &priv->library_id);
  
  bg_controllable_init(&b->ctrl,
                       bg_msg_sink_create(handle_msg_xml, b, 0),
                       bg_msg_hub_create(1));
  
  b->destroy = destroy_xml;
  b->priv = priv;
  }
