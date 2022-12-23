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

#include <ctype.h>
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
#define LOG_DOMAIN "mdb.filesystem"

#include <gmerlin/mdb.h>
#include <gmerlin/backend.h>
#include <gmerlin/bggavl.h>

#include <mdb_private.h>

#include <gmerlin/utils.h>

#include <gavl/metatags.h>
#include <gavl/http.h>

#define EXTFS_ID_PREFIX "/extfs"

/* ID From the volume manager */

#define VOLUME_ID        "volume_id"
#define VOLUME_CONTAINER "volume_container"

#define META_DIR "dir"

#define FS_DIRS_NAME "fs_dirs"
#define FS_PHOTO_NAME "photo_dirs"

/*
 * Caching:
 * We keep an own object cache, where we store the
 * directory entries
 *
 * Each cache entry contains the children of one
 * directory in an array (GAVL_META_CHILDREN),
 * the path and the mtime.
 * {
 * GAVL_META_URI:      path (string)
 * GAVL_META_MTIME:    mtime of the directory entry (long)
 * GAVL_META_CHILDREN: parsed children of the directory (array)
 * META_DIR:           raw directory as returned by 
 * }
 *
 * When BROWSE_OBJECT is called, we take the children
 * of the parent container from the cache.
 *
 * The directory entries are scanned on demand: Every
 * entry is set to 
 */

typedef struct
  {
  //  gavl_dictionary_t * dict;

  gavl_array_t dirs;
  gavl_array_t image_dirs;

  gavl_array_t extfs;
  int64_t extfs_counter;
  
  gavl_dictionary_t * container;
  gavl_dictionary_t * image_container;

  /* Direct children of container and image container. Everything below is handled
     by the caching mechanism */

  //  gavl_array_t * container_children;
  //  gavl_array_t * image_container_children;
  
  const char * root_id;
  const char * image_root_id;
  int have_params;

  int mount_removable;
  int mount_audiocd;
  int mount_videodisk;
  
  bg_object_cache_t * cache;
  
  } fs_t;

/* Dir array functions */
#if 0
static int has_id(const gavl_array_t * arr, const char * id)
  {
  int i;
  const gavl_dictionary_t * dict;
  const char * str;
  for(i = 0; i < arr->num_entries; i++)
    {
    if((dict = gavl_value_get_dictionary(&arr->entries[i])) &&
       (str = gavl_dictionary_get_string(dict, GAVL_META_ID)) &&
       !strcmp(str, id))
      return 1;
    }
  return 0;
  }
#endif

static int has_uri(const gavl_array_t * arr, const char * id)
  {
  int i;
  const gavl_dictionary_t * dict;
  const char * str;
  for(i = 0; i < arr->num_entries; i++)
    {
    if((dict = gavl_value_get_dictionary(&arr->entries[i])) &&
       (str = gavl_dictionary_get_string(dict, GAVL_META_URI)) &&
       !strcmp(str, id))
      return 1;
    }
  return 0;
  }

static int has_label(const gavl_array_t * arr, const char * id)
  {
  int i;
  const gavl_dictionary_t * dict;
  const char * str;
  for(i = 0; i < arr->num_entries; i++)
    {
    if((dict = gavl_value_get_dictionary(&arr->entries[i])) &&
       (str = gavl_dictionary_get_string(dict, GAVL_META_LABEL)) &&
       !strcmp(str, id))
      return 1;
    }
  return 0;
  }

#if 0
static gavl_dictionary_t * add_dir_array(gavl_array_t * arr, const char * dir, const char * parent_id, int idx)
  {
  int i;
  gavl_value_t val;
  gavl_dictionary_t * dict;
  const char * pos;
  char * id;
  
  gavl_value_init(&val);
  dict = gavl_value_set_dictionary(&val);

  gavl_dictionary_set_string(dict, GAVL_META_URI, dir);
  
  if((pos = strrchr(dir, '/')))
    pos++;
  else
    pos = dir;

  id = bg_sprintf("%s/%s", parent_id, pos);
  
  if(!has_id(arr, id))
    {
    gavl_dictionary_set_string_nocopy(dict, GAVL_META_ID, id);
    }
  else
    {
    free(id);
    
    for(i = 0; i < 1000; i++)
      {
      id = bg_sprintf("%s/%s-%d", parent_id, pos, i+1);

      if(!has_id(arr, id))
        {
        gavl_dictionary_set_string_nocopy(dict, GAVL_META_ID, id);
        break;
        }
      else
        free(id);
      }
    }

  gavl_array_splice_val_nocopy(arr, idx, 0, &val);
  return gavl_value_get_dictionary_nc(&arr->entries[arr->num_entries-1]);
  }

static const gavl_dictionary_t * dir_array_get(const gavl_array_t * arr, int idx)
  {
  const gavl_dictionary_t * dir;

  if((idx < 0) || (idx >= arr->num_entries) ||
     !(dir = gavl_value_get_dictionary(&arr->entries[idx])))
    return NULL;

  return dir;
  }
#endif

static const gavl_dictionary_t * bg_mdb_dir_array_get_by_id(const gavl_array_t * arr, const char * id)
  {
  int i;
  const gavl_dictionary_t * d;
  const char * str;
  
  for(i = 0; i < arr->num_entries; i++)
    {
    if((d = gavl_value_get_dictionary(&arr->entries[i])) &&
       (str = gavl_dictionary_get_string(d, GAVL_META_ID)) &&
       !strcmp(id, str))
      {
      return d;
      }
    }
  return NULL;
  }

/* */

static const gavl_value_t * cache_put(bg_mdb_backend_t * be, const char * id, gavl_value_t * val)
  {
  int i;
  gavl_dictionary_t * cache_entry;
  gavl_dictionary_t * child;
  gavl_array_t * children;
  
  fs_t * fs = be->priv;

  cache_entry = gavl_value_get_dictionary_nc(val);
  children = gavl_dictionary_get_array_nc(cache_entry, GAVL_META_CHILDREN);

  for(i = 0; i < children->num_entries; i++)
    {
    if((child = gavl_value_get_dictionary_nc(&children->entries[i])))
      bg_mdb_object_cleanup(child); // Remove uncessesary crap
    }
  
  return bg_object_cache_put_nocopy(fs->cache, id, val);
  }

static void browse_dir_list(bg_mdb_backend_t * be,
                            gavl_msg_t * msg,
                            const gavl_array_t * dirs, const char * root_id,
                            int photo_mode, int start, int num);

static void destroy_filesystem(bg_mdb_backend_t * b)
  {
  fs_t * fs = b->priv;
  gavl_array_free(&fs->extfs);

  gavl_array_free(&fs->dirs);
  gavl_array_free(&fs->image_dirs);
  
  bg_object_cache_destroy(fs->cache);
  free(fs);
  }

/* Scan a directory and create dummy entries */

static int scan_directory_entry(const char * path, gavl_dictionary_t * ret)
  {
  struct stat st;
  if(stat(path, &st))
    return 0;
    
  gavl_dictionary_set_long(ret, GAVL_META_MTIME, st.st_mtime);
  gavl_dictionary_set_string(ret, GAVL_META_URI, path);

  if(S_ISDIR(st.st_mode))
    {
    gavl_dictionary_set_string(ret, GAVL_META_MEDIA_CLASS, GAVL_META_MEDIA_CLASS_DIRECTORY);
    }
  else
    {
    gavl_dictionary_set_string(ret, GAVL_META_MEDIA_CLASS, GAVL_META_MEDIA_CLASS_LOCATION);
    }
  return 1;
  }

static void scan_directory(const char * path, gavl_array_t * ret)
  {
  glob_t g;
  char * pattern;
  int i;
  gavl_value_t val;
  gavl_dictionary_t * dict;
  
  pattern = bg_sprintf("%s/*", path);
  pattern = gavl_escape_string(pattern, "[]?");
  
  glob(pattern, 0, NULL /* errfunc */, &g);
  
  for(i = 0; i < g.gl_pathc; i++)
    {
    gavl_value_init(&val);
    dict = gavl_value_set_dictionary(&val);
    if(scan_directory_entry(g.gl_pathv[i], dict))
      gavl_array_splice_val_nocopy(ret, -1, 0, &val);
    else
      gavl_value_free(&val);
    }
  globfree(&g);
  }

static int is_image(gavl_dictionary_t * mi)
  {
  /* In Photo mode we count only the images */
  const gavl_dictionary_t * dict;
  const char * str;

  if((dict = gavl_get_track(mi, 0)) &&
     (dict = gavl_track_get_metadata(dict)) &&
     (str = gavl_dictionary_get_string(dict, GAVL_META_MEDIA_CLASS)) &&
     gavl_string_starts_with(str, GAVL_META_MEDIA_CLASS_IMAGE))
    return 1;
  else
    return 0;
  }

static void read_container_info(const bg_mdb_backend_t * be,
                                gavl_dictionary_t * ret,
                                const gavl_array_t * arr, int photo_mode)
  {
  int i;
  
  int num_containers  = 0;
  int num_items       = 0;

  gavl_dictionary_t * m;
  gavl_dictionary_t * first_image = NULL;
  
  for(i = 0; i < arr->num_entries; i++)
    {
    const gavl_dictionary_t * dict;
    const char * klass;
    const char * uri;

    if(!(dict = gavl_value_get_dictionary_nc(&arr->entries[i])) ||
       !(dict = gavl_track_get_metadata(dict)) ||
       !(klass = gavl_dictionary_get_string(dict, GAVL_META_MEDIA_CLASS)))
      {
      continue;
      }
    
    if(gavl_string_starts_with(klass, "container"))
      num_containers++;
    else
      {
      gavl_dictionary_t * mi;
      int num_tracks;
      
      uri = gavl_dictionary_get_string(dict, GAVL_META_URI);
      
      if(photo_mode) // Ignore everything except images
        {
        if(!bg_plugin_find_by_filename(bg_plugin_reg, uri, BG_PLUGIN_IMAGE_READER))
          continue;
        }
      else // Directory mode: We open files only to detect multitrack plugins
        {
        if(!bg_file_supports_multitrack(uri))
          {
          num_items++;
          continue;
          }
        }
      /* Open location */

      mi = bg_plugin_registry_load_media_info(bg_plugin_reg, uri, 0);
      
      if(!mi)
        {
        if(!photo_mode)
          num_items++;
        }
      else
        {
        num_tracks = gavl_get_num_tracks(mi);

        if(num_tracks > 1)
          {
          if(!photo_mode)
            num_containers++;
          }
        else
          {
          if(!photo_mode || is_image(mi))
            {
            num_items++;

            if(photo_mode && !first_image)
              {
              first_image = gavl_dictionary_create();
              gavl_dictionary_copy(first_image, gavl_get_track(mi, 0));
              }
            }
          }
        }
      }
    }
  
  m = gavl_dictionary_get_dictionary_create(ret, GAVL_META_METADATA);
  gavl_track_set_num_children(ret, num_containers, num_items);

  fprintf(stderr, "Read container info %d %d\n", num_containers, num_items);
  
  if(photo_mode)
    {
    if(num_items && !num_containers)
      {
      gavl_dictionary_set_string(m, GAVL_META_MEDIA_CLASS, GAVL_META_MEDIA_CLASS_PHOTOALBUM);
      gavl_dictionary_set_string(m, GAVL_META_CHILD_CLASS, GAVL_META_MEDIA_CLASS_IMAGE);

      if(first_image)
        {
        const char * str = NULL;
        const gavl_dictionary_t * m1;

        if((m1 = gavl_track_get_metadata(first_image)) &&
           (gavl_metadata_get_src(m1, GAVL_META_SRC, 0,  NULL, &str)) &&
           str)
          {
          bg_mdb_make_thumbnails(be->db, str);
          bg_mdb_get_thumbnails(be->db, first_image);
          gavl_dictionary_set(m, GAVL_META_ICON_URL,
                              gavl_dictionary_get(gavl_track_get_metadata(first_image),
                                                  GAVL_META_ICON_URL));
          }
        
        gavl_dictionary_destroy(first_image);
        }
      }
    else
      gavl_dictionary_set_string(m, GAVL_META_MEDIA_CLASS, GAVL_META_MEDIA_CLASS_CONTAINER);
    }
  else
    gavl_dictionary_set_string(m, GAVL_META_MEDIA_CLASS, GAVL_META_MEDIA_CLASS_DIRECTORY);
  
  }


/*
 *   For fixed directories, the DB id
 *   /dirs/<dir-label>/<rel_path>
 *
 *   becomes
 *   <dir-path>/<rel_path>
 *
 *   For fixed photo albums, the DB id
 *   /photos/<dir-label>/<rel_path>
 *
 *   becomes
 *   <dir-path>/<rel_path>
 *
 *
 *   For removable directories,
 *   /extfs-1/<rel_path>
 *
 *   becomes
 *   <extfs-mountpoint>/<rel_path>
 *
 *
 *   Multitrack files are reported as containers
 *   where the ID of the tracks is:
 *   <parent_id>/?track=1
 *   
 */

static char * id_to_path_dirlist(const gavl_array_t * list, const char * id)
  {
  int i;
  const gavl_dictionary_t * d;
  for(i = 0; i < list->num_entries; i++)
    {
    int len;
    const char * test_id;
      
    if((d = gavl_value_get_dictionary(&list->entries[i])) &&
       (test_id = gavl_dictionary_get_string(d, GAVL_META_ID)) &&
       (test_id = strrchr(test_id, '/')) &&
       (test_id++) && 
       (len = strlen(test_id)) &&
       !strncmp(test_id, id, len) &&
       ((id[len] == '/') || (id[len] == '\0')))
      {
      if(id[len] == '/')
        return bg_sprintf("%s/%s",
                          gavl_dictionary_get_string(d, GAVL_META_URI),
                          id + len + 1);
      else if(id[len] == '\0')
        return gavl_strdup(gavl_dictionary_get_string(d, GAVL_META_URI));
      }
    }
  return NULL;
  }

static char * id_to_path(const bg_mdb_backend_t * be, const char * id)
  {
  int i;
  const gavl_dictionary_t * d;
    
  fs_t * p = be->priv;

  /* configured directories */
  if(gavl_string_starts_with(id, p->root_id))
    {
    id += strlen(p->root_id);

    if(*id != '/')
      return NULL;

    id++;
    return id_to_path_dirlist(&p->dirs, id);
    }
  else if(gavl_string_starts_with(id, p->image_root_id))
    {
    id += strlen(p->image_root_id);

    if(*id != '/')
      return NULL;

    id++;
    return id_to_path_dirlist(&p->image_dirs, id);
    }
  
  /* Removable drive or disk */
  else if(gavl_string_starts_with(id, EXTFS_ID_PREFIX))
    {
    int volume_id_len;
    const char * volume_id;
    const char * uri;
    
    //    fprintf(stderr, "id_to_path %s\n", id);
    for(i = 0; i < p->extfs.num_entries; i++)
      {
      if((d = gavl_value_get_dictionary(&p->extfs.entries[i])) &&
         (volume_id = gavl_dictionary_get_string(d, GAVL_META_ID)) &&
         (volume_id_len = strlen(volume_id)) &&
         !strncmp(volume_id, id, volume_id_len) &&
         (uri = gavl_dictionary_get_string(d, GAVL_META_URI)) &&
         ((id[volume_id_len] == '/') || (id[volume_id_len] == '\0')))
        {
        if(id[volume_id_len] == '/')
          {
          if(strstr(uri, "://")) // Removable
            {
            int track;
            char * ret;

            if(isdigit(id[volume_id_len+1]) && ((track = atoi(id + volume_id_len + 1)) > 0))
              {
              gavl_dictionary_t vars;
              gavl_dictionary_init(&vars);

              gavl_dictionary_set_int(&vars, BG_URL_VAR_TRACK, track);
              ret = bg_url_append_vars(gavl_strdup(uri), &vars);
              gavl_dictionary_free(&vars);
              return ret;
              }
            }
          else
            return bg_sprintf("%s/%s",
                            gavl_dictionary_get_string(d, GAVL_META_URI),
                            id + volume_id_len + 1);
          }
        else if(id[volume_id_len] == '\0')
          return gavl_strdup(uri);
        }
      }
    }
  return NULL;
  }

static char * path_to_id_dirlist(const bg_mdb_backend_t * be, const gavl_array_t * dirs,
                                 const char * path, const char * root_id)
  {
  const gavl_dictionary_t * d;
  int dir_uri_len;
  const char * dir_uri;
  int i;
  
  for(i = 0; i < dirs->num_entries; i++)
    {
    if((d = gavl_value_get_dictionary(&dirs->entries[i])) &&
       (dir_uri = gavl_dictionary_get_string(d, GAVL_META_URI)) &&
       (dir_uri_len = strlen(dir_uri)) &&
       !strncmp(dir_uri, path, dir_uri_len))
      {
      if(path[dir_uri_len] == '/')
        return bg_sprintf("%s/%s/%s",
                          root_id,
                          gavl_dictionary_get_string(d, GAVL_META_LABEL),
                          path + (dir_uri_len + 1));
      else if(path[dir_uri_len] == '\0')
        return bg_sprintf("%s/%s", root_id,
                          gavl_dictionary_get_string(d, GAVL_META_LABEL));
      }
    }

  return NULL;
  }

static char * path_to_id(const bg_mdb_backend_t * be, const char * path)
  {
  int i;
  const gavl_dictionary_t * d;
  int dir_uri_len;
  const char * dir_uri;
  
  fs_t * p = be->priv;

  char * ret;

  if((ret = path_to_id_dirlist(be, &p->image_dirs, path, p->image_root_id)))
    return ret;

  if((ret = path_to_id_dirlist(be, &p->dirs, path, p->root_id)))
    return ret;
  
  for(i = 0; i < p->extfs.num_entries; i++)
    {
    if((d = gavl_value_get_dictionary(&p->extfs.entries[i])) &&
       (dir_uri = gavl_dictionary_get_string(d, GAVL_META_URI)) &&
       (dir_uri_len = strlen(dir_uri)) &&
       !strncmp(dir_uri, path, dir_uri_len))
      {
      if(strstr(dir_uri, "://"))
        {
        int track = 0;
        char * ret = NULL;
        gavl_dictionary_t url_vars;

        gavl_dictionary_init(&url_vars);

        gavl_url_get_vars_c(path, &url_vars);
        if(gavl_dictionary_get_int(&url_vars, BG_URL_VAR_TRACK, &track) && (track > 0))
          ret = bg_sprintf("%s/%d", gavl_dictionary_get_string(d, GAVL_META_ID), track);
        else
          ret = gavl_strdup(gavl_dictionary_get_string(d, GAVL_META_ID));
        
        gavl_dictionary_free(&url_vars);
        return ret;
        }
      
      if(path[dir_uri_len] == '/')
        return bg_sprintf("%s/%s",
                          gavl_dictionary_get_string(d, GAVL_META_ID),
                          path + (dir_uri_len + 1));
      else if(path[dir_uri_len] == '\0')
        return gavl_strdup(gavl_dictionary_get_string(d, GAVL_META_ID));
      }
    }
  return NULL;
  }

static gavl_dictionary_t * get_volume_container(bg_mdb_backend_t * b, gavl_dictionary_t * volume)
  {
  int i, num;
  gavl_dictionary_t * ret;
  gavl_dictionary_t * ret_m;
  gavl_dictionary_t * dict;
  gavl_dictionary_t * m;
  const char * uri;
  char * id;
  
  ret = gavl_dictionary_get_dictionary_nc(volume, VOLUME_CONTAINER);

  if(ret)
    return ret;

  uri = gavl_dictionary_get_string(volume, GAVL_META_URI);
  
  ret = bg_plugin_registry_load_media_info(bg_plugin_reg, uri, 0);

  if(!ret)
    return NULL;
  
  id = path_to_id(b, uri);

  ret_m = gavl_dictionary_get_dictionary_create(ret, GAVL_META_METADATA);
  gavl_dictionary_set_string(ret_m, GAVL_META_ID, id);
  
  num = gavl_get_num_tracks(ret);

  for(i = 0; i < num; i++)
    {
    if((dict = gavl_get_track_nc(ret, i)) &&
       (m = gavl_track_get_metadata_nc(dict)))
      {
      gavl_dictionary_set_string_nocopy(m, GAVL_META_ID, bg_sprintf("%s/?track=%d", id, i+1));
      bg_mdb_add_http_uris(b->db, dict);
      }
    }
#if 0  
  fprintf(stderr, "Loaded disk\n");
  gavl_dictionary_dump(ret, 2);
  fprintf(stderr, "\n");
#endif
  gavl_dictionary_set_dictionary_nocopy(volume, VOLUME_CONTAINER, ret);

  free(id);
  
  return gavl_dictionary_get_dictionary_nc(volume, VOLUME_CONTAINER);
  }

static gavl_dictionary_t * volume_by_uri(bg_mdb_backend_t * be, const char * uri)
  {
  int i;
  const char * volume_uri;
  gavl_dictionary_t * d;
  fs_t * p = be->priv;
  
  for(i = 0; i < p->extfs.num_entries; i++)
    {
    if((d = gavl_value_get_dictionary_nc(&p->extfs.entries[i])) &&
       (volume_uri = gavl_dictionary_get_string(d, GAVL_META_URI)) &&
       !strcmp(volume_uri, uri))
      {
      return d;
      }
    }
  return NULL;
  }

static int browse_object_internal(bg_mdb_backend_t * be,
                                  gavl_dictionary_t * ret,
                                  const gavl_dictionary_t * entry, int photo_mode)
  {
  const char * pos;
  int result = 0;
  gavl_dictionary_t * m;
  
  struct stat st;
  char * path_priv = NULL;

  const char * path;
  //  const char * klass;
  
  int track = -1;
  gavl_dictionary_t * first_image = NULL;
  
  
  path = gavl_dictionary_get_string(entry, GAVL_META_URI);

  if(strstr(path, "://")) /* Device */
    {
    int track = 0;
    char * base_path = NULL;
    gavl_dictionary_t * volume;
    gavl_dictionary_t * container;
    gavl_dictionary_t url_vars;

    gavl_dictionary_init(&url_vars);
    base_path = gavl_strdup(path);
    gavl_url_get_vars(base_path, &url_vars);

    if(!(volume = volume_by_uri(be, base_path)) ||
       !(container = get_volume_container(be, volume)))
      goto end;
    
    /* Find volume */
    
    if(gavl_dictionary_get_int(&url_vars, BG_URL_VAR_TRACK, &track))
      {
      track--;
      gavl_dictionary_copy(ret, gavl_get_track(container, track));
      }
    else
      {
      gavl_dictionary_copy(ret, container);
      gavl_dictionary_set(ret, GAVL_META_CHILDREN, NULL);
      }
    free(base_path);
    gavl_dictionary_free(&url_vars);
    // result = 1;
    goto end;
    }
  
  /* Multitrack file */
  
  if((pos = strrchr(path, '/')) &&
     gavl_string_starts_with(pos, "/?track=") &&
     ((track = atoi(pos + 8)) > 0))
    {
    /* Multi track file */
    path_priv = gavl_strdup(path);
    path_priv[pos - path] = '\0';
    path = path_priv;
    }

  if(stat(path, &st))
    goto end;
  
  if(S_ISDIR(st.st_mode))
    {
    gavl_array_t dir;

    gavl_array_init(&dir);
    scan_directory(path, &dir);

    m = gavl_dictionary_get_dictionary_create(ret, GAVL_META_METADATA);

    /* Create return value */
    read_container_info(be, ret, &dir, photo_mode);
    
    if((pos = strrchr(path, '/')))
      gavl_dictionary_set_string(m, GAVL_META_LABEL, pos + 1);
    else
      fprintf(stderr, "No label for path %s\n", path);
    
    gavl_dictionary_set_string_nocopy(m, GAVL_META_ID, path_to_id(be, path));
    
    gavl_dictionary_set_long(m, GAVL_META_MTIME, st.st_mtime);
    }
  else if(S_ISREG(st.st_mode))
    {
    gavl_dictionary_t * mi;
    int num_tracks;
    mi = bg_plugin_registry_load_media_info(bg_plugin_reg, path, 0);
    
    // gavl_get_track(const gavl_dictionary_t * dict, int idx);
    
    if(!mi)
      {
      if(photo_mode)
        goto end;
      
      /* File couldn't be opened */
      m = gavl_dictionary_get_dictionary_create(ret, GAVL_META_METADATA);
      gavl_dictionary_set_string(m, GAVL_META_MEDIA_CLASS,
                                 GAVL_META_MEDIA_CLASS_FILE);
      
      if((pos = strrchr(path, '/')))
        gavl_dictionary_set_string(m, GAVL_META_LABEL, pos + 1);
      
      gavl_dictionary_set_string_nocopy(m, GAVL_META_ID, path_to_id(be, path));
      result = 1;
      goto end;
      }
    
    num_tracks = gavl_get_num_tracks(mi);
    
    if(num_tracks == 1)
      {
      if(photo_mode && !is_image(mi))
        goto end;
      
      gavl_dictionary_copy(ret, gavl_get_track(mi, 0));
      if(photo_mode)
        {
        bg_mdb_make_thumbnails(be->db, path);
        }
      }
    else if(num_tracks > 1)
      {
      /* Multitrack photos? Maybe multipage TIFFs or so... */
      if(photo_mode)
        goto end;
      
      if(track > 0)
        {
        gavl_dictionary_copy(ret, gavl_get_track(mi, track-1));
        }
      else
        {
        gavl_dictionary_copy(ret, mi);
        gavl_dictionary_set(ret, GAVL_META_CHILDREN, NULL);
        }
      }
    
    m = gavl_track_get_metadata_nc(ret);

    if(!m)
      {
      fprintf(stderr, "Got no metadata\nEntry:\n");
      gavl_dictionary_dump(entry, 2);
      fprintf(stderr, "Ret:\n");
      gavl_dictionary_dump(ret, 2);
      fprintf(stderr, "mi:\n");
      gavl_dictionary_dump(mi, 2);
      }
    gavl_dictionary_set_string_nocopy(m, GAVL_META_ID, path_to_id(be, path));
    
    if(mi)
      gavl_dictionary_destroy(mi);
    }
  
  result = 1;
  end:

  if(first_image)
    gavl_dictionary_destroy(first_image);
  
  if(path_priv)
    free(path_priv);
  
  return result;
  }

/* Scan a full directory. */
static void scan_directory_full(bg_mdb_backend_t * be, const char * ctx_id, gavl_array_t * ret,
                                const gavl_array_t * dir)
  {
  int i;
  fs_t * fs = be->priv;
  
  for(i = 0; i < dir->num_entries; i++)
    {
    int photo_mode = 0;
    
    gavl_dictionary_t * child;
    const gavl_dictionary_t * dirent;
    gavl_value_t child_val;

    if(gavl_string_starts_with(ctx_id, fs->image_root_id))
      photo_mode = 1;

    gavl_value_init(&child_val);
    child = gavl_value_set_dictionary(&child_val);
    
    if((dirent = gavl_value_get_dictionary(&dir->entries[i])) &&
       browse_object_internal(be, child, dirent, photo_mode))
      {
      /* New entry */
      const char * var;
      const char * pos;
      
      gavl_dictionary_t * m = gavl_dictionary_get_dictionary_create(child, GAVL_META_METADATA);
      gavl_array_splice_val_nocopy(ret, -1, 0, &child_val);

      if((var = gavl_dictionary_get_string(dirent, GAVL_META_URI)) &&
         (pos = strrchr(var, '/')))
        gavl_dictionary_set_string_nocopy(m, GAVL_META_ID, bg_sprintf("%s%s", ctx_id, pos));
      }
    else
      gavl_value_free(&child_val);
    }

  if(!ret->num_entries)
    return;

  bg_mdb_set_idx_total(ret, 0, ret->num_entries);
  bg_mdb_set_next_previous(ret);
  }

static int dirs_equal(const gavl_array_t * dir1, 
                      const gavl_array_t * dir2)
  {
  int found;
  int i, j;
  
  if(dir1->num_entries != dir2->num_entries)
    return 0;

  for(i = 0; i < dir1->num_entries; i++)
    {
    found = 0;
    for(j = 0; j < dir1->num_entries; j++)
      {
      if(!gavl_value_compare(&dir1->entries[i], &dir2->entries[j]))
        {
        found = 1;
        break;
        }
      }
    if(!found)
      return 0;
    }
  return 1;
  }

static const gavl_array_t * ensure_directory(bg_mdb_backend_t * be,
                                             const char * path,
                                             const char * ctx_id)
  {
  gavl_array_t dir;
  const gavl_value_t * cache_entry_val;
  const gavl_dictionary_t * cache_entry;
  
  const gavl_array_t * children = NULL;
  const gavl_array_t * cache_dir;
  
  
  fs_t * fs = be->priv;

  gavl_array_init(&dir);
    
  scan_directory(path, &dir);

  /*
   *  Try to read directory from cache
   */
  
  if(!(cache_entry_val = bg_object_cache_get(fs->cache, ctx_id)))
    {
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "(re-)scanning %s, cache object missing", path);
    goto rescan;
    }

  if(!(cache_entry = gavl_value_get_dictionary(cache_entry_val)))
    {
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "re-scanning %s, cache object is no dictionary", path);
    goto rescan;
    }
  if(!(cache_dir = gavl_dictionary_get_array(cache_entry, META_DIR)))
    {
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "re-scanning %s, cache object contains no Element "META_DIR, path);
    goto rescan;
    }
  if(!dirs_equal(cache_dir, &dir))
    {
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "re-scanning %s, cache expired %d %d", path,
             cache_dir->num_entries, dir.num_entries);

    /* Save arrays to file */
    //    fprintf(stderr, "Saving arrays\n");
    //    bg_array_save_xml(cache_dir, "cache_dir.xml", "dir");
    //    bg_array_save_xml(&dir, "dir.xml", "dir");
    
    goto rescan;
    }

  if(!(children = gavl_dictionary_get_array(cache_entry, GAVL_META_CHILDREN)))
    goto rescan;

  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Got %s from cache", path);
  
  goto end;
  
  rescan:

    {
    gavl_value_t new_cache_entry_val;
    gavl_dictionary_t * new_cache_entry = NULL;
    gavl_array_t * new_children = NULL;

    gavl_value_init(&new_cache_entry_val);
    
    /* Re-scan directory */
    new_cache_entry = gavl_value_set_dictionary(&new_cache_entry_val);
    new_children = gavl_dictionary_get_array_create(new_cache_entry, GAVL_META_CHILDREN);
    gavl_dictionary_set_array(new_cache_entry, META_DIR, &dir);

    scan_directory_full(be, ctx_id, new_children, &dir);
    children = new_children;

    cache_entry_val = cache_put(be, ctx_id, &new_cache_entry_val);
    cache_entry = gavl_value_get_dictionary(cache_entry_val);
    children = gavl_dictionary_get_array(cache_entry, GAVL_META_CHILDREN);
    }

  
  
  end:
  
  gavl_array_free(&dir);
  
  return children;
  }

static void browse_children(bg_mdb_backend_t * be, gavl_msg_t * msg, const char * ctx_id,
                            int start, int num, int one_answer)
  {
  struct stat st;
  gavl_msg_t * res;
  
  fs_t * fs = be->priv;
  char * path = NULL;
  
  //  fprintf(stderr, "browse_children %s\n", path);

  /* TODO: Do this generically? */
  if(!strcmp(ctx_id, fs->root_id))
    {
    /* /photos */
    browse_dir_list(be, msg, &fs->dirs, fs->root_id, 0, start, num);
    return;
    }
  else if(!strcmp(ctx_id, fs->image_root_id))
    {
    /* */
    browse_dir_list(be, msg, &fs->image_dirs, fs->image_root_id, 1, start, num);
    return;
    }

  path = id_to_path(be, ctx_id);
  
  if(strstr(path, "://"))
    {
    gavl_dictionary_t * volume;
    gavl_dictionary_t * container;
    char * base_path = NULL;

    gavl_dictionary_t url_vars;

    gavl_array_t * arr;

    gavl_dictionary_init(&url_vars);
    base_path = gavl_strdup(path);
    gavl_url_get_vars(base_path, &url_vars);
    
    /* Find volume */
    if(!(volume = volume_by_uri(be, base_path)) ||
       !(container = get_volume_container(be, volume)) ||
       !(arr = gavl_get_tracks_nc(container)))
      return;

    if(!bg_mdb_adjust_num(start, &num, arr->num_entries))
      return;
    
    res = bg_msg_sink_get(be->ctrl.evt_sink);

    if(num < arr->num_entries)
      {
      int i;
      gavl_array_t tmp_arr;
      gavl_array_init(&tmp_arr);
      
      /* Range */

      for(i = 0; i < num; i++)
        {
        gavl_dictionary_t * d;
        d = gavl_value_get_dictionary_nc(&arr->entries[i+start]);
        
        bg_mdb_get_thumbnails(be->db, d);
        bg_mdb_add_http_uris(be->db, d);
        gavl_array_splice_val(&tmp_arr, i, 0, &arr->entries[i+start]);
        }
      bg_mdb_set_browse_children_response(res, &tmp_arr, msg, &start, 1, arr->num_entries);
      gavl_array_free(&tmp_arr);
      }
    else
      bg_mdb_set_browse_children_response(res, arr, msg, &start, 1, arr->num_entries);
    
    bg_msg_sink_put(be->ctrl.evt_sink, res);
    
    gavl_dictionary_free(&url_vars);
    free(base_path);
    return;
    }

    
  if(stat(path, &st))
    return;

  if(S_ISDIR(st.st_mode))
    {
    int i;
    gavl_array_t arr;
    
    const gavl_array_t * children;

    children = ensure_directory(be, path, ctx_id);

    /* Return */
    fprintf(stderr, "browse_children %s %d\n", ctx_id, children->num_entries);
    
    if(!bg_mdb_adjust_num(start, &num, children->num_entries))
      return;

    fprintf(stderr, "browse_children 2 %d %d\n", num, children->num_entries);

    
    gavl_array_init(&arr);
      
    for(i = 0; i < num; i++)
      {
      gavl_dictionary_t * dict;
      gavl_array_splice_val(&arr, i, 0, &children->entries[i + start]);

      if((dict = gavl_value_get_dictionary_nc(&arr.entries[i])))
        {
        //        bg_mdb_create_thumbnails(be->db, dict);
        bg_mdb_get_thumbnails(be->db, dict);
        bg_mdb_add_http_uris(be->db, dict);
        }
      }

    res = bg_msg_sink_get(be->ctrl.evt_sink);
    bg_mdb_set_browse_children_response(res, &arr, msg, &start, 1, children->num_entries);
    bg_msg_sink_put(be->ctrl.evt_sink, res);
    
    gavl_array_free(&arr);
    }
  
  // TODO!
  else if(S_ISREG(st.st_mode))     /* Multitrack file */
    {
    gavl_dictionary_t * mi;
    gavl_msg_t * res;
    int i;
    char * parent_id;

    gavl_array_t arr;
    const gavl_array_t * children;
    
    mi = bg_plugin_registry_load_media_info(bg_plugin_reg, path, 0);

    children = gavl_get_tracks(mi);
    
    parent_id = path_to_id(be, path);

    if(!bg_mdb_adjust_num(start, &num, children->num_entries))
      {
      gavl_dictionary_destroy(mi);
      return;
      }
    
    gavl_array_init(&arr);
    
    for(i = 0; i < num; i++)
      {
      gavl_dictionary_t * dict = gavl_value_get_dictionary_nc(&children->entries[i + start]);
      
      gavl_track_set_id_nocopy(dict,
                               bg_sprintf("%s/?track=%d", parent_id, i+1+start));
      
      bg_mdb_add_http_uris(be->db, dict);
      gavl_array_splice_val(&arr, -1, 0, &children->entries[i + start]);
      }
    free(parent_id);
    
    res = bg_msg_sink_get(be->ctrl.evt_sink);
    bg_mdb_set_browse_children_response(res, &arr, msg, &start, 1, children->num_entries);
    bg_msg_sink_put(be->ctrl.evt_sink, res);
    gavl_dictionary_destroy(mi);
    gavl_array_free(&arr);
    }
  }

#if 0
static void set_directories(bg_mdb_backend_t * be,
                            gavl_array_t * dst, const gavl_array_t * arr,
                            gavl_dictionary_t * container, int photo_mode)
  {
  int num_added = 0;
  int num_deleted = 0;
  gavl_msg_t * resp;
  gavl_dictionary_t * entry;
  
  const char * dir;
  int i;
  const gavl_dictionary_t * dict;
  int send_msg = 0;
  fs_t * fs = be->priv;

  gavl_value_t val_add;
  gavl_value_t child_val_add;
  
  gavl_array_t * arr_add;
  gavl_dictionary_t * child_dict_add;
  const char * id;
  
  if(fs->have_params) // Parameters are already set
    {
    send_msg = 1;

    if(dst->num_entries)
      num_deleted = dst->num_entries;

    for(i = 0; i < num_deleted; i++)
      {
      if((dict = gavl_value_get_dictionary(&dst->entries[i])) &&
         (dir = gavl_dictionary_get_string(dict, GAVL_META_URI)))
        {
        bg_mdb_unexport_media_directory(be->ctrl.evt_sink, dir);
        }
      }
    gavl_array_splice_val(dst, 0, -1, NULL);
    }

  
  for(i = 0; i < arr->num_entries; i++)
    {
    gavl_dictionary_t * m;
    //    gavl_dictionary_t * d;
    
    gavl_dictionary_t dent;
    gavl_dictionary_t cont;
    const char * dir;
    const char * pos;
    
    dir = gavl_string_array_get(arr, i);

    if(!bg_is_directory(dir, 0))
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Cannot add directory \"%s\" (not a directory)", dir);
      continue;
      }
    
    gavl_dictionary_init(&cont);
    gavl_dictionary_init(&dent);
    
    scan_directory_entry(dir, &dent);

    if(!browse_object_internal(be, &cont, &dent, photo_mode))
      {
      gavl_dictionary_free(&cont);
      gavl_dictionary_free(&dent);
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Cannot add directory \"%s\"", dir);
      continue;
      }

    num_added++;
    
    entry = add_dir_array(dst, dir, gavl_track_get_id(container), -1);
    m = gavl_dictionary_get_dictionary_create(&cont, GAVL_META_METADATA);

    id = gavl_dictionary_get_string(entry, GAVL_META_ID);
    
    gavl_dictionary_set_string(m, GAVL_META_ID, id);

    if((pos = strrchr(id, '/')))
      gavl_dictionary_set_string(m, GAVL_META_LABEL, pos+1);
    
    gavl_dictionary_set_dictionary(entry, "obj", &cont);
    
    //    d = gavl_dictionary_get_dictionary(entry, "obj");
    //    fprintf(stderr, "OBJ: %p\n", d);
    
    gavl_dictionary_free(&cont);
    gavl_dictionary_free(&dent);

    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Add directory \"%s\"", dir);
    
    //    fprintf(stderr, "Added directory %s\n", dir);
    //    gavl_dictionary_dump(entry, 2);
    
    bg_mdb_export_media_directory(be->ctrl.evt_sink, dir);
    }


  /* Update children of the container */
  gavl_track_set_num_children(container, dst->num_entries, 0);
    
  if(!send_msg || (!num_added && !num_deleted))
    return;

  /* Splice */

  gavl_value_init(&val_add);
  arr_add = gavl_value_set_array(&val_add);
  
  for(i = 0; i < dst->num_entries; i++)
    {
    const gavl_dictionary_t * d;
    gavl_dictionary_t * m;
    
    gavl_value_init(&child_val_add);
    child_dict_add = gavl_value_set_dictionary(&child_val_add);

    d = dir_array_get(dst, i);
    // d = gavl_dictionary_get_dictionary(d, "obj");

    m = gavl_dictionary_get_dictionary_create(child_dict_add, GAVL_META_METADATA);

    gavl_dictionary_set(m, GAVL_META_LABEL, gavl_dictionary_get(d, GAVL_META_LABEL));
    gavl_dictionary_set(m, GAVL_META_ID, gavl_dictionary_get(d, GAVL_META_ID));
    
    //  gavl_dictionary_copy(child_dict_add, d);

    gavl_array_splice_val_nocopy(arr_add, -1, 0, &child_val_add);
    }
  
  resp = bg_msg_sink_get(be->ctrl.evt_sink);

  gavl_msg_set_splice_children_nocopy(resp, BG_MSG_NS_DB, BG_MSG_DB_SPLICE_CHILDREN,
                                      gavl_track_get_id(container),
                                      1, 0, num_deleted, &val_add);
  bg_msg_sink_put(be->ctrl.evt_sink, resp);
  
  /* Update parent */

  resp = bg_msg_sink_get(be->ctrl.evt_sink);
  gavl_msg_set_id_ns(resp, BG_MSG_DB_OBJECT_CHANGED, BG_MSG_NS_DB);
  gavl_dictionary_set_string(&resp->header, GAVL_MSG_CONTEXT_ID, gavl_track_get_id(container));
  gavl_msg_set_arg_dictionary(resp, 0, container);
  bg_msg_sink_put(be->ctrl.evt_sink, resp);
  }
#endif

static void browse_dir_list(bg_mdb_backend_t * be,
                            gavl_msg_t * msg,
                            const gavl_array_t * dirs, const char * root_id,
                            int photo_mode, int start, int num)
  {
  const gavl_dictionary_t * dir;
  int i;
  gavl_array_t arr;
  
  gavl_array_init(&arr);
  
  if(!bg_mdb_adjust_num(start, &num, dirs->num_entries))
    return;
            
  for(i = 0; i < num; i++)
    {
    if((dir = gavl_value_get_dictionary(&dirs->entries[start + i])))
      {
      gavl_value_t val;
      gavl_dictionary_t * track;
      gavl_dictionary_t * m;
      const gavl_array_t * children;
      
      gavl_value_init(&val);
      track = gavl_value_set_dictionary(&val);

      m = gavl_dictionary_get_dictionary_create(track, GAVL_META_METADATA);
      
      gavl_dictionary_set(m, GAVL_META_LABEL, gavl_dictionary_get(dir, GAVL_META_LABEL));
      gavl_dictionary_set(m, GAVL_META_ID, gavl_dictionary_get(dir, GAVL_META_ID));
      gavl_dictionary_set_string(m, GAVL_META_MEDIA_CLASS, GAVL_META_MEDIA_CLASS_DIRECTORY);

      children = ensure_directory(be, gavl_dictionary_get_string(dir, GAVL_META_URI),
                                  gavl_dictionary_get_string(dir, GAVL_META_ID));

      fprintf(stderr, "Got children:\n");
      gavl_array_dump(children, 2);
      
      read_container_info(be, track, children, photo_mode);
      
      gavl_array_splice_val_nocopy(&arr, i, 0, &val);
      }
    }

  fprintf(stderr, "Browse dir list:\n");
  gavl_array_dump(&arr, 2);
  
  if(arr.num_entries)
    {
    gavl_msg_t * res;

    bg_mdb_set_idx_total(&arr, 0, arr.num_entries);
    bg_mdb_set_next_previous(&arr);
    
    res = bg_msg_sink_get(be->ctrl.evt_sink);

    bg_mdb_set_browse_children_response(res, &arr, msg, &start, 1, dirs->num_entries);

    //    fprintf(stderr, "Browse children response:\n");
    //    gavl_msg_dump(res, 2);

    bg_msg_sink_put(be->ctrl.evt_sink, res);
    }
  gavl_array_free(&arr);
  
  }

static const gavl_dictionary_t * cache_dirent_by_path(const gavl_dictionary_t * cache_entry, const char * path)
  {
  int i;
  const gavl_dictionary_t * ret;
  const gavl_array_t * cache_dir;
  const char * var;
  
  if(!(cache_dir = gavl_dictionary_get_array(cache_entry, META_DIR)))
    return NULL;

  for(i = 0; i < cache_dir->num_entries; i++)
    {
    if((ret = gavl_value_get_dictionary(&cache_dir->entries[i])) &&
       (var = gavl_dictionary_get_string(ret, GAVL_META_URI)) &&
       !strcmp(path, var))
      return ret;
    }
  return 0;
  }

static const gavl_dictionary_t *
cache_object_by_id(const gavl_dictionary_t * cache_entry, const char * id, int * idx, int * total)
  {
  int i;
  const gavl_array_t * children;
  const gavl_dictionary_t * ret;
  const char * var;

  if(!(children = gavl_dictionary_get_array(cache_entry, GAVL_META_CHILDREN)))
    return NULL;

  for(i = 0; i < children->num_entries; i++)
    {
    if((ret = gavl_value_get_dictionary(&children->entries[i])) &&
       (var = gavl_track_get_id(ret)) &&
       !strcmp(id, var))
      {
      *idx = i;
      *total = children->num_entries;
      return ret;
      }
    }
  return NULL;
  }

static int browse_object(bg_mdb_backend_t * be, const char * id, gavl_dictionary_t * ret,
                         int * idx, int * total)
  {
  const gavl_value_t * cache_entry_val;
  const gavl_dictionary_t * cache_entry;
  const gavl_dictionary_t * dict;
  const gavl_dictionary_t * obj;
  
  gavl_value_t new_cache_entry_val;
  gavl_dictionary_t * new_cache_entry;
  gavl_array_t * new_children;
  gavl_array_t * new_dir;
  
  int64_t mtime;
  fs_t * fs = be->priv;
  char * parent_id = NULL;
  char * path = NULL;
  struct stat st;
  int result = 0;

  if(gavl_string_starts_with(id, fs->root_id) && (dict = bg_mdb_dir_array_get_by_id(&fs->dirs, id)))
    {
    obj = gavl_dictionary_get_dictionary(dict, "obj");

    if(!obj)
      {
      fprintf(stderr, "Dict contains no item \"obj\"\n");
      }
    
    gavl_dictionary_copy(ret, obj);
    result = 1;
    goto end;
    }
  else if(gavl_string_starts_with(id, fs->image_root_id) &&
          (dict = bg_mdb_dir_array_get_by_id(&fs->image_dirs, id)))
    {
    obj = gavl_dictionary_get_dictionary(dict, "obj");

    
    gavl_dictionary_copy(ret, obj);

    bg_mdb_get_thumbnails(be->db, ret);
    bg_mdb_add_http_uris(be->db, ret);

    result = 1;
    goto end;
    }
  
  parent_id = bg_mdb_get_parent_id(id);
  path = id_to_path(be, id);
  
  if(stat(path, &st))
    return 0;
  
  if(!(cache_entry_val = bg_object_cache_get(fs->cache, parent_id)) ||
     !(cache_entry = gavl_value_get_dictionary(cache_entry_val)))
    {
    gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Found no cache entry for ID %s", parent_id);
    goto rescan;
    }

  if(!(dict = cache_dirent_by_path(cache_entry, path)))
    {
    gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Found no dirent for path %s in cache entry for ID %s", path, id);
    goto rescan;
    }
  
  if(!gavl_dictionary_get_long(dict, GAVL_META_MTIME, &mtime))
    {
    gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Cache entry for path %s contains no MTIME", path);
    goto rescan;
    }
    
  if(mtime != st.st_mtime)
    {
    gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "MTIME mismatch");
    goto rescan;
    }
  
  if(!(dict = cache_object_by_id(cache_entry, id, idx, total)))
    {
    gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Cache object missing");
    goto rescan;
    }
  
  gavl_dictionary_copy(ret, dict);
  bg_mdb_get_thumbnails(be->db, ret);
  bg_mdb_add_http_uris(be->db, ret);

  result = 1;
  goto end;
  
  rescan:

  if(path)
    free(path);

  path = id_to_path(be, parent_id);
  
  /* Re-scan directory */
  gavl_value_init(&new_cache_entry_val);
  new_cache_entry = gavl_value_set_dictionary(&new_cache_entry_val);
  new_children = gavl_dictionary_get_array_create(new_cache_entry, GAVL_META_CHILDREN);
  new_dir = gavl_dictionary_get_array_create(new_cache_entry, META_DIR);
  
  scan_directory(path, new_dir);
    
  scan_directory_full(be, parent_id, new_children, new_dir);

  if((dict = cache_object_by_id(new_cache_entry, id, idx, total)))
    {
    gavl_dictionary_copy(ret, dict);
    result = 1;
    }

  /* Save cache entry */
  cache_put(be, id, &new_cache_entry_val);
  
  end:
  
  if(path)
    free(path);
  
  if(parent_id)
    free(parent_id);
  
  return result;
  }

static void rescan_directory(bg_mdb_backend_t * be, const char * id)
  {
  int i;
  gavl_array_t arr_static;
  char * path;
  const gavl_array_t * arr;

  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Rescanning directory %s", id);
  
  path = id_to_path(be, id);
  
  arr = ensure_directory(be, path, id);
  
  gavl_array_init(&arr_static);
  gavl_array_copy(&arr_static, arr);

  for(i = 0; i < arr_static.num_entries; i++)
    {
    const gavl_dictionary_t * dict = NULL;
    const char * sub_id = NULL;
    const char * klass = NULL;
    const char * uri = NULL;
    
    if((dict = gavl_value_get_dictionary(&arr_static.entries[i])) &&
       (dict = gavl_track_get_metadata(dict)) &&
       (klass = gavl_dictionary_get_string(dict, GAVL_META_MEDIA_CLASS)))
       {
       if((sub_id = gavl_dictionary_get_string(dict, GAVL_META_ID)) &&
          ( !strcmp(klass, GAVL_META_MEDIA_CLASS_PHOTOALBUM) ||
            !strcmp(klass, GAVL_META_MEDIA_CLASS_DIRECTORY) ||
            !strcmp(klass, GAVL_META_MEDIA_CLASS_CONTAINER) ) )
         {
         rescan_directory(be, sub_id);
         }
       else if(!strcmp(klass, GAVL_META_MEDIA_CLASS_IMAGE) &&
               gavl_metadata_get_src(dict, GAVL_META_SRC, 0,
                                       NULL, &uri))
         {
         bg_mdb_make_thumbnails(be->db, uri);
         }
       }
    
    //    gavl_dictionary_dump(dict, 2);
    //    fprintf(stderr, "%s %s\n", sub_id, klass);
    
    }

  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Finished rescanning directory %s", id);
  
  free(path);
  gavl_array_free(&arr_static);
  }

static void rescan(bg_mdb_backend_t * be)
  {
  int i;
  const gavl_dictionary_t * d;
  const char * id;
  
  fs_t * fs = be->priv;
  for(i = 0; i < fs->image_dirs.num_entries; i++)
    {
    d = gavl_value_get_dictionary(&fs->image_dirs.entries[i]);
    id = gavl_dictionary_get_string(d, GAVL_META_ID);
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Rescanning photo directory %s", id);
    rescan_directory(be, id);
    }
  }

static void add_directory(bg_mdb_backend_t * b,
                          const char * ctx_id,
                          gavl_dictionary_t * ret,
                          gavl_array_t * dirs,
                          int idx,
                          const gavl_dictionary_t * add)
  {
  const gavl_array_t * dir_entries;
  
  gavl_value_t dir_val;
  gavl_dictionary_t * dir_dict;
  char url_md5[33];
  const char * uri;
  const char * pos;
  char * label;
  int photo_mode = 0;
  fs_t * fs = b->priv;
  const gavl_dictionary_t * m = gavl_track_get_metadata(add);

  if(!(uri = gavl_dictionary_get_string(m, GAVL_META_URI)))
    {
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "No directory to add");
    return;
    }
  if(has_uri(dirs, uri))
    {
    /* Directory already there */
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Directory already added");
    return;
    }

  fprintf(stderr, "Adding directory: %s\n", uri);

  gavl_value_init(&dir_val);
  dir_dict = gavl_value_set_dictionary(&dir_val);

  gavl_dictionary_set_string(dir_dict, GAVL_META_URI, uri);
  
  bg_get_filename_hash(uri, url_md5);
  gavl_dictionary_set_string_nocopy(dir_dict, GAVL_META_ID, gavl_sprintf("%s/%s", ctx_id, url_md5));

  pos = strrchr(uri, '/');
  if(pos)
    {
    int idx = 1;
    pos++;

    label = gavl_strdup(pos);

    while(has_label(dirs, label))
      {
      free(label);
      label = gavl_sprintf("%s (%d)", pos, idx);
      idx++;
      }
    gavl_dictionary_set_string_nocopy(dir_dict, GAVL_META_LABEL, label);
    }

  gavl_array_splice_val_nocopy(dirs, idx, 0, &dir_val);
  
  dir_entries = ensure_directory(b, uri, gavl_dictionary_get_string(dir_dict, GAVL_META_ID));
  
  if(!strcmp(ctx_id, fs->image_root_id))
    photo_mode = 1;
  
  read_container_info(b, ret, dir_entries, photo_mode);
  }

static void load_dir_list(bg_mdb_backend_t * be, gavl_array_t * arr)
  {
  char * tmp_string;
  int photo_mode = 0;
  
  fs_t * fs = be->priv;

  if(arr == &fs->dirs)
    tmp_string = bg_sprintf("%s/"FS_DIRS_NAME, be->db->path);
  else
    {
    tmp_string = bg_sprintf("%s/"FS_PHOTO_NAME, be->db->path);
    photo_mode = 1;
    }
  
  bg_array_load_xml(arr, tmp_string, "dirs");
  free(tmp_string);

  if(photo_mode)
    gavl_track_set_num_children(fs->image_container, fs->image_dirs.num_entries, 0);
  else
    gavl_track_set_num_children(fs->container, fs->dirs.num_entries, 0);
  
  }

static void save_dir_list(bg_mdb_backend_t * be, gavl_array_t * arr)
  {
  char * tmp_string;
  fs_t * fs = be->priv;

  if(arr == &fs->dirs)
    tmp_string = bg_sprintf("%s/"FS_DIRS_NAME, be->db->path);
  else
    tmp_string = bg_sprintf("%s/"FS_PHOTO_NAME, be->db->path);

  bg_array_save_xml(arr, tmp_string, "dirs");
  
  free(tmp_string);
  }


static void splice(bg_mdb_backend_t * b, const char * ctx_id, int last, int idx, int del, gavl_value_t * add, int sendmsg)
  {
  int num_added = 0;
  gavl_array_t * dirs;
  gavl_dictionary_t * container;
  fs_t * fs = b->priv;
  gavl_array_t children;
  gavl_msg_t * resp;
  gavl_value_t val_add;

  gavl_value_t child_val;
  gavl_dictionary_t * child;
    
  fprintf(stderr, "splice %s %d %d %d\n", ctx_id, last, idx, del);
  gavl_value_dump(add, 2);
  gavl_array_init(&children);

  gavl_value_init(&val_add);
  
  if(!strcmp(ctx_id, fs->root_id))
    {
    dirs = &fs->dirs;
    container = fs->container;
    }
  else if(!strcmp(ctx_id, fs->image_root_id))
    {
    dirs = &fs->image_dirs;
    container = fs->image_container;
    }
  else
    {
    /* Children are not editable */
    goto end;
    }

  if(idx < 0)
    idx = dirs->num_entries;
  
  if(del < 0)
    del = dirs->num_entries - idx;
  
  if(del)
    gavl_array_splice_val_nocopy(dirs, idx, del, NULL);
  
  if(!add)
    goto end;

  if(add->type == GAVL_TYPE_DICTIONARY)
    {
    gavl_dictionary_t * dict_add = gavl_value_set_dictionary(&val_add);
    
    add_directory(b,
                  ctx_id,
                  dict_add,
                  dirs,
                  idx,
                  gavl_value_get_dictionary(add));
    
    num_added = 1;
    }
  else if(add->type == GAVL_TYPE_ARRAY)
    {
    int i;
    const gavl_array_t * arr = gavl_value_get_array(add);
    gavl_array_t * arr_add = gavl_value_set_array(&val_add);
    
    for(i = 0; i < arr->num_entries; i++)
      {
      gavl_value_init(&child_val);
      child = gavl_value_set_dictionary(&child_val);

      add_directory(b,
                    ctx_id,
                    child,
                    dirs,
                    idx + i,
                    gavl_value_get_dictionary(&arr->entries[i]));
      
      gavl_array_splice_val_nocopy(arr_add, -1, 0, &child_val);
      num_added++;
      }
    
    }
  
  end:

  fprintf(stderr, "Splice 1\n");
  gavl_array_dump(dirs, 2);
  
  if((del || num_added))
    {
    gavl_track_set_num_children(container, dirs->num_entries, 0);

    if(sendmsg)
      {
      /* Save dir array */
      save_dir_list(b, dirs);

      /* Send splice signal */
      resp = bg_msg_sink_get(b->ctrl.evt_sink);

      gavl_msg_set_splice_children_nocopy(resp, BG_MSG_NS_DB, BG_MSG_DB_SPLICE_CHILDREN,
                                          ctx_id,
                                          1, idx, del, &val_add);
      bg_msg_sink_put(b->ctrl.evt_sink, resp);
      
      }
    /* Update parent */

    
    
    resp = bg_msg_sink_get(b->ctrl.evt_sink);
    gavl_msg_set_id_ns(resp, BG_MSG_DB_OBJECT_CHANGED, BG_MSG_NS_DB);
    gavl_dictionary_set_string(&resp->header, GAVL_MSG_CONTEXT_ID, ctx_id);
    gavl_msg_set_arg_dictionary(resp, 0, container);
    bg_msg_sink_put(b->ctrl.evt_sink, resp);
    
    }
  
  return;
  }


static int handle_msg(void * priv, gavl_msg_t * msg)
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
          {
          int idx = -1, total = -1;
          gavl_dictionary_t ret;
          const char * ctx_id = gavl_dictionary_get_string(&msg->header,
                                                           GAVL_MSG_CONTEXT_ID);
          
          gavl_dictionary_init(&ret);
          if(browse_object(be, ctx_id, &ret, &idx, &total))
            {
            gavl_msg_t * res = bg_msg_sink_get(be->ctrl.evt_sink);

            bg_mdb_set_browse_obj_response(res, &ret, msg, idx, total);
            bg_msg_sink_put(be->ctrl.evt_sink, res);
            }
          gavl_dictionary_free(&ret);
          }
          break;
        case BG_FUNC_DB_BROWSE_CHILDREN:
          {
          const char * ctx_id;
          int start, num, one_answer;
          
          //          gavl_msg_dump(msg, 2);

          bg_mdb_get_browse_children_request(msg, &ctx_id, &start, &num, &one_answer);
          browse_children(be, msg, ctx_id, start, num, one_answer);
          }
          break;
        case BG_CMD_DB_RESCAN:
          {
          gavl_msg_t * res;
          rescan(be);
          /* Send done event */
          
          res = bg_msg_sink_get(be->ctrl.evt_sink);
          gavl_msg_set_id_ns(res, BG_MSG_DB_RESCAN_DONE, BG_MSG_NS_DB);
          bg_msg_sink_put(be->ctrl.evt_sink, res);

          }
          break;
        case BG_CMD_DB_SPLICE_CHILDREN:
          {
          int last = 0;
          int idx = 0;
          int del = 0;
          gavl_value_t add;
          gavl_value_init(&add);
          
          gavl_msg_get_splice_children(msg, &last, &idx, &del, &add);

          splice(be, gavl_dictionary_get_string(&msg->header, GAVL_MSG_CONTEXT_ID), last, idx, del, &add, 1);
          gavl_value_free(&add);
          }
          break;
        }
      break;
      }
    case BG_MSG_NS_VOLUMEMANAGER:
      switch(msg->ID)
        {
        case BG_MSG_ID_VOLUME_ADDED:
          {
          gavl_value_t vol_store_val;
          gavl_dictionary_t * vol_store;
          
          gavl_dictionary_t obj;
          gavl_dictionary_t vol;
          gavl_dictionary_t * m;
          const char * id;
          const char * uri;
          const char * klass;
          
          gavl_dictionary_init(&vol);
          gavl_dictionary_init(&obj);
          
          id = gavl_msg_get_arg_string_c(msg, 0);
          gavl_msg_get_arg_dictionary(msg, 1, &vol);

          if(!(klass = gavl_dictionary_get_string(&vol, GAVL_META_MEDIA_CLASS)))
            return 1;

          uri = gavl_dictionary_get_string(&vol, GAVL_META_URI);
          
          if(gavl_string_starts_with(klass, GAVL_META_MEDIA_CLASS_ROOT_REMOVABLE_FILESYSTEM) &&
             (!fs->mount_removable || !gavl_string_starts_with(uri, "/media")))
            {
            return 1;
            }
          if(!strcmp(klass, GAVL_META_MEDIA_CLASS_ROOT_REMOVABLE_AUDIOCD) &&
             !fs->mount_audiocd)
            {
            return 1;
            }
          if((!strcmp(klass, GAVL_META_MEDIA_CLASS_ROOT_REMOVABLE_VCD) ||
              !strcmp(klass, GAVL_META_MEDIA_CLASS_ROOT_REMOVABLE_SVCD) ||
              !strcmp(klass, GAVL_META_MEDIA_CLASS_ROOT_REMOVABLE_VIDEODVD)) &&
             !fs->mount_videodisk)
            return 1;
          
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


          if(*uri == '/')
            bg_mdb_export_media_directory(be->ctrl.evt_sink, gavl_dictionary_get_string(&vol, GAVL_META_URI));
          
          gavl_dictionary_free(&obj);
          gavl_dictionary_free(&vol);
          }
          break;
        case BG_MSG_ID_VOLUME_REMOVED:
          {
          int i;
          const char * test_id;
          const char * id;
          const gavl_dictionary_t * d;
          
          id = gavl_msg_get_arg_string_c(msg, 0);
          
          for(i = 0; i < fs->extfs.num_entries; i++)
            {
            if((d = gavl_value_get_dictionary(&fs->extfs.entries[i])) &&
               (test_id = gavl_dictionary_get_string(d, VOLUME_ID)) &&
               !strcmp(test_id, id))
              {
              gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Volume removed %s", id); 
              
              /* Send message */
              bg_mdb_delete_root_container(be->ctrl.evt_sink, gavl_dictionary_get_string(d, GAVL_META_ID));
              
              bg_mdb_unexport_media_directory(be->ctrl.evt_sink, gavl_dictionary_get_string(d, GAVL_META_URI));
              
              /* Remove locally */
              gavl_array_splice_val(&fs->extfs, i, 1, NULL);
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
            fs->have_params = 1;
            return 1;
            }
#if 0          
          if(!strcmp(name, "dirs"))
            {
            set_directories(be, &fs->dirs, gavl_value_get_array(&val), fs->container, 0);
            }
          else if(!strcmp(name, "image_dirs"))
            {
            set_directories(be, &fs->image_dirs, gavl_value_get_array(&val), fs->image_container, 1);
            }
#endif
          else if(!strcmp(name, "mount_removable"))
            {
            fs->mount_removable = val.v.i;
            //   fprintf(stderr, "mount removable: %d\n", fs->mount_removable);
            }
          else if(!strcmp(name, "mount_audiocd"))
            {
            fs->mount_audiocd = val.v.i;
            }
          else if(!strcmp(name, "mount_videodisk"))
            {
            fs->mount_videodisk = val.v.i;
            }
          
          
          /* Pass to core to store it in the config registry */
          if(fs->have_params)
            {
            resp = bg_msg_sink_get(be->ctrl.evt_sink);
            bg_msg_set_parameter_ctx(resp, BG_MSG_PARAMETER_CHANGED_CTX, MDB_BACKEND_FILESYSTEM, name, &val);
            bg_msg_sink_put(be->ctrl.evt_sink, resp);
          
            gavl_value_free(&val);
            }
          }
          break;
        }
    }  
  
  return 1;
  }

static const bg_parameter_info_t parameters[] =
  {
    {
      .name = "mount_removable",
      .long_name = TRS("Mount removable storage media"),
      .help_string = TRS("Mount removable storage media like pendrives, mobile phones."),
      .type = BG_PARAMETER_CHECKBUTTON,
    },
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
#if 1
    {
      .name = "dirs",
      .long_name = TRS("Directories"),
      .help_string = TRS("Folder which can be browsed like a file manager"),
      .type = BG_PARAMETER_DIRLIST,
    },
    {
      .name = "image_dirs",
      .long_name = TRS("Folder containing photoalbums"),
      .type = BG_PARAMETER_DIRLIST,
    },
#endif
    { /* End */ },
  };

void bg_mdb_create_filesystem(bg_mdb_backend_t * b)
  {
  fs_t * priv;
  char * tmp_string;
  priv = calloc(1, sizeof(*priv));
  b->priv = priv;

  b->parameters = parameters;
  
  b->flags |= (BE_FLAG_VOLUMES | BE_FLAG_RESCAN);
  
  priv->root_id = bg_mdb_get_klass_id(GAVL_META_MEDIA_CLASS_ROOT_DIRECTORIES);
  priv->image_root_id = bg_mdb_get_klass_id(GAVL_META_MEDIA_CLASS_ROOT_PHOTOS);
  
  b->destroy = destroy_filesystem;

  bg_controllable_init(&b->ctrl,
                       bg_msg_sink_create(handle_msg, b, 0),
                       bg_msg_hub_create(1));
  
  //  fprintf(stderr, "bg_mdb_create_filesystem\n");
  //  gavl_array_dump(priv->dirs, 2);
  
  priv->container = bg_mdb_get_root_container(b->db, GAVL_META_MEDIA_CLASS_ROOT_DIRECTORIES);
  bg_mdb_container_set_backend(priv->container, MDB_BACKEND_FILESYSTEM);
  bg_mdb_add_can_add(priv->container, GAVL_META_MEDIA_CLASS_DIRECTORY);
  bg_mdb_set_editable(priv->container);
  
  /* Add children */

  priv->image_container = bg_mdb_get_root_container(b->db, GAVL_META_MEDIA_CLASS_ROOT_PHOTOS);
  
  bg_mdb_container_set_backend(priv->image_container, MDB_BACKEND_FILESYSTEM);
  bg_mdb_add_can_add(priv->image_container, GAVL_META_MEDIA_CLASS_DIRECTORY);
  bg_mdb_set_editable(priv->image_container);
  
  /* Create cache */  
  tmp_string = bg_sprintf("%s/fs_cache", b->db->path);
  priv->cache = bg_object_cache_create(1024, 32, tmp_string);
  free(tmp_string);

  /* Load directories */

  load_dir_list(b, &priv->image_dirs);
  load_dir_list(b, &priv->dirs);
  
  }
