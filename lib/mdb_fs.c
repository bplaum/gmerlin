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

#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <glob.h>

/* */

#include <config.h>

#include <gmerlin/translation.h>
#include <gmerlin/log.h>
#define LOG_DOMAIN "mdb.fs"

#include <gmerlin/mdb.h>
#include <gavl/utils.h>
#include <gmerlin/utils.h>
#include <gmerlin/bggavl.h>

#include <mdb_private.h>

#define DIRS_ROOT "dirs"
#define DIRS_FILE "fs_dirs.xml"

#define NUM_ROOT 2

typedef struct
  {
  const char * klass;
  const char * id;
  int flags;
  
  gavl_dictionary_t * container;
  gavl_array_t * dirs;
  gavl_array_t hashes;
  } fs_root_t;

typedef struct
  {
  bg_mdb_fs_cache_t c;
  gavl_dictionary_t config;
  fs_root_t root[NUM_ROOT];
  } fs_t;

static void finalize_func(bg_mdb_backend_t * be, gavl_dictionary_t * track, const char * id_prefix)
  {
  const char * var;
  const char * klass;
  const char * uri = NULL;

  gavl_dictionary_t * m;

  m = gavl_track_get_metadata_nc(track);
  
  /* Create thumbnails */
  if((klass = gavl_dictionary_get_string(m, GAVL_META_CLASS)))
    {
    if(!strcmp(klass, GAVL_META_CLASS_IMAGE) &&
       gavl_track_get_src(track, GAVL_META_SRC, 0, NULL, &uri))
      bg_mdb_make_thumbnails(be->db, uri);
    
    if(!strcmp(klass, GAVL_META_CLASS_PHOTOALBUM) &&
       gavl_track_get_src(track, GAVL_META_ICON_URL, 0, NULL, &uri))
      bg_mdb_make_thumbnails(be->db, uri);
    }
  
  bg_mdb_add_http_uris(be->db, track);
  
  if((var = gavl_dictionary_get_string(m, GAVL_META_ID)))
    gavl_dictionary_set_string_nocopy(m, GAVL_META_ID, gavl_sprintf("%s/%s", id_prefix, var));
  
  if((var = gavl_dictionary_get_string(m, GAVL_META_PREVIOUS_ID)))
    gavl_dictionary_set_string_nocopy(m, GAVL_META_PREVIOUS_ID, gavl_sprintf("%s/%s", id_prefix, var));
  
  if((var = gavl_dictionary_get_string(m, GAVL_META_NEXT_ID)))
    gavl_dictionary_set_string_nocopy(m, GAVL_META_NEXT_ID, gavl_sprintf("%s/%s", id_prefix, var));
  

  }

static void update_hashes(fs_root_t * root)
  {
  int i;
  char md5[33];

  gavl_array_reset(&root->hashes);

  for(i = 0; i < root->dirs->num_entries; i++)
    {
    bg_get_filename_hash(gavl_string_array_get(root->dirs, i), md5);
    gavl_string_array_add(&root->hashes, md5);
    }
  }


static void init_root(bg_mdb_backend_t * b, fs_root_t * root,
                      const char * klass, int flags)
  {
  fs_t * priv = b->priv;
  root->container = bg_mdb_get_root_container(b->db, klass);
  root->klass = klass;
  root->dirs = gavl_dictionary_get_array_create(&priv->config, klass);
  root->id = bg_mdb_get_klass_id(klass);
  root->flags = flags;
  
  bg_mdb_container_set_backend(root->container, MDB_BACKEND_FILESYSTEM);
  bg_mdb_add_can_add(root->container, GAVL_META_CLASS_DIRECTORY);
  bg_mdb_set_editable(root->container);

  gavl_track_set_num_children(root->container,
                              root->dirs->num_entries,
                              0);
  
  update_hashes(root);
  }

static fs_root_t * get_root(bg_mdb_backend_t * be, const char * id)
  {
  int i;
  fs_t * fs = be->priv;

  for(i = 0; i < NUM_ROOT; i++)
    {
    if(gavl_string_starts_with(id, fs->root[i].id))
      return &fs->root[i];
    }
  return NULL;
  }


static int compare_toplevel_dirs(const void * v1, const void * v2, void * priv)
  {
  const char * pos;
  
  const char * s1;
  const char * s2;

  s1 = gavl_value_get_string(v1);
  s2 = gavl_value_get_string(v2);

  if((pos = strrchr(s1, '/')))
    s1 = pos;
  if((pos = strrchr(s2, '/')))
    s2 = pos;

  return strcoll(s1, s2);
  }

static void set_root_child_internal(bg_mdb_backend_t * be,
                                    fs_root_t * root,
                                    const char * hash,
                                    const char * path,
                                    gavl_dictionary_t * ret)
  {
  const char * pos;
  int num_containers = 0;
  int num_items = 0;
  gavl_dictionary_t * m;
  fs_t * p = be->priv;
  
  m = gavl_dictionary_get_dictionary_create(ret, GAVL_META_METADATA);

  gavl_dictionary_set_string_nocopy(m, GAVL_META_ID,
                                    gavl_sprintf("%s/%s", root->id, hash));

  if((pos = strrchr(path, '/')))
    gavl_dictionary_set_string(m, GAVL_META_LABEL, pos+1);
  else
    gavl_dictionary_set_string(m, GAVL_META_LABEL, path);

  gavl_dictionary_set_string(m, GAVL_META_CLASS, GAVL_META_CLASS_DIRECTORY);

  bg_mdb_fs_count_children(&p->c, path, root->flags,
                           &num_containers, &num_items);
  gavl_track_set_num_children(ret, num_containers, num_items);

  }

static void set_root_child(bg_mdb_backend_t * be,
                           fs_root_t * root,
                           int idx,
                           gavl_dictionary_t * ret)
  {
  const char * hash;
  const char * path;
  
  
  hash = gavl_string_array_get(&root->hashes, idx);
  path = gavl_string_array_get(root->dirs, idx);
  set_root_child_internal(be, root, hash, path, ret);
  }

#if 1
static char * path_from_id(fs_root_t * root, const char * ctx_id)
  {
  int i;
  const char * hash;
  
  for(i = 0; i < root->hashes.num_entries; i++)
    {
    hash = gavl_string_array_get(&root->hashes, i);
    if(gavl_string_starts_with(ctx_id, hash))
      {
      ctx_id += strlen(hash);

      if(*ctx_id == '\0')
        return gavl_strdup(gavl_string_array_get(root->dirs, i));
      else
        return gavl_sprintf("%s%s", gavl_string_array_get(root->dirs, i),
                            ctx_id);
      }
    }
  return NULL;
  }
#endif

static int browse_object(bg_mdb_backend_t * be, gavl_msg_t * msg)
  {
  const char * pos;
  gavl_msg_t * res;
  gavl_dictionary_t dict;
  fs_root_t * root;
  const char * ctx_id =
    gavl_dictionary_get_string(&msg->header, GAVL_MSG_CONTEXT_ID);

  if(!(root = get_root(be, ctx_id)))
    return 0;
  
  ctx_id += strlen(root->id);

  if(*ctx_id != '/')
    return 0;

  gavl_dictionary_init(&dict);
  
  ctx_id++;

  if(!(pos = strchr(ctx_id, '/')))
    {
    int idx;
    /* Root element */
    idx = gavl_string_array_indexof(&root->hashes, ctx_id);
    
    set_root_child(be, root, idx, &dict);
    
    res = bg_msg_sink_get(be->ctrl.evt_sink);
    bg_mdb_set_browse_obj_response(res, &dict,
                                   msg, idx, root->dirs->num_entries);
    bg_msg_sink_put(be->ctrl.evt_sink);
    
    }
  else
    {
    /* TODO: Browse object below root child */
    }

  gavl_dictionary_free(&dict);
  
  //  fprintf(stderr, "browse_object: %s\n", ctx_id);
  return 1;
  }

static int browse_children(bg_mdb_backend_t * be, gavl_msg_t * msg)
  {
  int i;
  gavl_array_t arr;
  const char * ctx_id = NULL;
  const char * ctx_id_orig = NULL;
  int start = 0, num = 0, one_answer = 0;
  gavl_msg_t * res;
  fs_t * p = be->priv;

  fs_root_t * root;
  
  bg_mdb_get_browse_children_request(msg, &ctx_id_orig, &start, &num, &one_answer);

  ctx_id = ctx_id_orig;
  
  if(!(root = get_root(be, ctx_id)))
    return 0;
  
  //  fprintf(stderr, "browse_children: %s\n", ctx_id);

  gavl_array_init(&arr);

  
  ctx_id += strlen(root->id);

  if(*ctx_id == '\0')
    {
    gavl_dictionary_t * dict;
    
    /* Browse root elements */
    //    fprintf(stderr, "Browse root elements\n");

    if(!bg_mdb_adjust_num(start, &num, root->dirs->num_entries))
      return 1;
    
    for(i = 0; i < num; i++)
      {
      dict = gavl_array_append_dictionary(&arr);
      set_root_child(be, root, i + start, dict);
      }
    
    res = bg_msg_sink_get(be->ctrl.evt_sink);
    bg_mdb_set_browse_children_response(res, &arr, msg, &start, 1, root->dirs->num_entries);
    bg_msg_sink_put(be->ctrl.evt_sink);
    }
  else
    {
    int total_entries = 0;
    char * path;
    int i;
    
    ctx_id++; // Skip '/'
    
    path = path_from_id(root, ctx_id);
    
    // fprintf(stderr, "Browse children: %s -> %s\n", ctx_id, path);

    bg_mdb_fs_browse_children(&p->c, path, &arr, 
                              start, num, root->flags, &total_entries);

    for(i = 0; i < arr.num_entries; i++)
      {
      finalize_func(be, gavl_value_get_dictionary_nc(&arr.entries[i]),
                    ctx_id_orig);
      }
    
    res = bg_msg_sink_get(be->ctrl.evt_sink);
    bg_mdb_set_browse_children_response(res, &arr, msg, &start, 1, total_entries);
    bg_msg_sink_put(be->ctrl.evt_sink);
    
    free(path);
    }
  return 1;
  }

static int ping_fs(bg_mdb_backend_t * be)
  {
  int ret = 0;
  fs_t * p = be->priv;
  int i, j;

  for(i = 0; i < NUM_ROOT; i++)
    {
    for(j = 0; j < p->root[i].dirs->num_entries; j++)
      {
      bg_mdb_export_media_directory(be->ctrl.evt_sink, gavl_string_array_get(p->root[i].dirs, j));
      }
    }
  
  be->ping_func = NULL;
  
  return ret;
  }

static int handle_msg_fs(void * priv, gavl_msg_t * msg)
  {
  bg_mdb_backend_t * be = priv;
  fs_t * fs = be->priv;

  switch(msg->NS)
    {
    case BG_MSG_NS_DB:
      {
      switch(msg->ID)
        {
        case BG_FUNC_DB_BROWSE_OBJECT:
          browse_object(be, msg);
          break;
        case BG_FUNC_DB_BROWSE_CHILDREN:
          browse_children(be, msg);
          break;
        case BG_CMD_DB_SPLICE_CHILDREN:
          {
          int changed = 0;
          int last = 0;
          int idx = 0;
          int del = 0;
          const char * ctx_id;
          gavl_value_t add;
          const gavl_dictionary_t * dict;
          const char * dir = NULL;
          fs_root_t * root;
          
          gavl_value_init(&add);
          
          gavl_msg_get_splice_children(msg, &last, &idx, &del, &add);

          ctx_id = gavl_dictionary_get_string(&msg->header, GAVL_MSG_CONTEXT_ID);

          if(!(root = get_root(be, ctx_id)))
            return 0;
          
          if(idx < 0)
            idx = root->dirs->num_entries;

          if(del < 0)
            del = root->dirs->num_entries - idx;
          
          if(del)
            {
            gavl_array_splice_val(root->dirs, idx, del, NULL);
            changed = 1;
            }
          
          if((dict = gavl_value_get_dictionary(&add)) &&
             (dict = gavl_track_get_metadata(dict)) &&
             (dir = gavl_dictionary_get_string(dict, GAVL_META_URI)))
            {
            if(gavl_string_array_indexof(root->dirs, dir) >= 0)
              gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Won't add %s: Already there", dir);
            else
              {
              gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Adding %s", dir);
              gavl_string_array_add(root->dirs, dir);
              changed = 1;
              }
            }

          if(changed)
            {
            gavl_msg_t * evt;
            char * tmp_string;
            const char * id = gavl_track_get_id(root->container);
            gavl_array_sort(root->dirs, compare_toplevel_dirs, NULL);
            update_hashes(root);
            gavl_track_set_num_children(root->container, root->dirs->num_entries, 0);

            /* Send delete event */
            if(del)
              {
              evt = bg_msg_sink_get(be->ctrl.evt_sink);
              gavl_msg_set_splice_children(evt, BG_MSG_NS_DB,
                                           BG_MSG_DB_SPLICE_CHILDREN,
                                           id, 1, idx, del, NULL);
              bg_msg_sink_put(be->ctrl.evt_sink);
              }
            
            /* Send splice event for root children */            

            if(dir)
              {
              gavl_value_t val;
              gavl_dictionary_t * dict = gavl_value_set_dictionary(&val);

              evt = bg_msg_sink_get(be->ctrl.evt_sink);

              idx = gavl_string_array_indexof(root->dirs, dir);
              
              set_root_child(be, root, idx, dict);

              gavl_msg_set_splice_children(evt, BG_MSG_NS_DB,
                                           BG_MSG_DB_SPLICE_CHILDREN,
                                           id, 1, idx, 0, &val);
              gavl_value_free(&val);

              bg_msg_sink_put(be->ctrl.evt_sink);
              }
            
            
            /* Send changed event for root object */            
            evt = bg_msg_sink_get(be->ctrl.evt_sink);
            gavl_msg_set_id_ns(evt, BG_MSG_DB_OBJECT_CHANGED, BG_MSG_NS_DB);
            gavl_dictionary_set_string(&evt->header, GAVL_MSG_CONTEXT_ID, id);
            gavl_msg_set_arg_dictionary(evt, 0, root->container);

            bg_msg_sink_put(be->ctrl.evt_sink);
            
            /* */            
            tmp_string = gavl_sprintf("%s/%s", be->db->path, DIRS_FILE);
            bg_dictionary_save_xml(&fs->config, tmp_string, DIRS_ROOT);
            free(tmp_string);
            }

          
          
          gavl_value_free(&add);
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
          break;
        }
      break;
    }  
  
  return 1;
  }

static void destroy_fs(bg_mdb_backend_t * b)
  {
  int i;
  fs_t * fs = b->priv;
  
  gavl_dictionary_free(&fs->config);
  bg_mdb_fs_cache_cleanup(&fs->c);

  for(i = 0; i < NUM_ROOT; i++)
    {
    gavl_array_free(&fs->root[i].hashes);
    }
  

  }

void bg_mdb_create_fs(bg_mdb_backend_t * b)
  {
  char * tmp_string;
  fs_t * priv;
  priv = calloc(1, sizeof(*priv));
  b->priv = priv;

  b->destroy = destroy_fs;
  b->ping_func = ping_fs;
  
  bg_controllable_init(&b->ctrl,
                       bg_msg_sink_create(handle_msg_fs, b, 0),
                       bg_msg_hub_create(1));


  /* Create cache */

  tmp_string = gavl_sprintf("%s/fs_cache.sqlite", b->db->path);
  bg_mdb_fs_cache_init(&priv->c, tmp_string);
  free(tmp_string);
  
  /* Load dirs */
  
  tmp_string = gavl_sprintf("%s/%s", b->db->path, DIRS_FILE);
  if(!access(tmp_string, R_OK))
    bg_dictionary_load_xml(&priv->config, tmp_string, DIRS_ROOT);
  free(tmp_string);
  
  init_root(b, &priv->root[0], GAVL_META_CLASS_ROOT_DIRECTORIES,
            BG_MDB_FS_MASK_AUDIO |
            BG_MDB_FS_MASK_VIDEO |
            BG_MDB_FS_MASK_IMAGE |
            BG_MDB_FS_MASK_DIRECTORY |
            BG_MDB_FS_MASK_MULTITRACK);

  init_root(b, &priv->root[1], GAVL_META_CLASS_ROOT_PHOTOS,
            BG_MDB_FS_MASK_IMAGE |
            BG_MDB_FS_MASK_DIRECTORY);
  
  /* Add children */

  
  }
