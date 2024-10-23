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
// #include <gavl/http.h>
#include <gavl/utils.h>
#include <gavl/trackinfo.h>

#define REMOVABLE_ID_PREFIX "/ext-fs"

/* ID From the volume manager */

#define VOLUME_ID        "volume_id"
// #define VOLUME_CONTAINER "volume_container"

#define PHOTO_DIRS_NAME "photo_dirs.xml"
#define LOCAL_DIRS_NAME "local_dirs.xml"
#define DIRS_ROOT "dirs"


/*
 * ID Structure:
 * /dirs
 * /dirs/<md5_of_directory>
 * /dirs/<md5_of_directory>/File1
 * /dirs/<md5_of_directory>/Dir1
 * 
 * /photos
 * /photos/<md5_of_directory>
 * /photos/<md5_of_directory>/File1
 * /photos/<md5_of_directory>/Dir1
 *
 * /volume-1/Dir1
 * /volume-1/Dir1/File1
 * 
 * /volume-2/Dir1
 * /volume-2/Dir1/File1
 *
 *  The toplevel directories are stored in
 *  local_dirs, photo_dirs and removables
 *
 *  They contain mdb objects, which can directly be passed to
 *  bg_mdb_set_browse_children_response() and
 *  bg_mdb_set_browse_obj_response()
 *  
 */



typedef enum
  {
  DIR_TYPE_UNKNOWN,
  DIR_TYPE_FS_LOCAL,
  DIR_TYPE_FS_REMOVABLE,
  DIR_TYPE_PHOTOALBUMS,
  } dir_type_t;

typedef enum
  {
  BROWSE_MODE_ROOT_CHILD,
  BROWSE_MODE_ROOT_DIRECTORY,
  BROWSE_MODE_ROOT_PATH,
  } browse_mode_t;

typedef struct
  {
  //  gavl_dictionary_t * dict;

  gavl_array_t local_dirs;
  gavl_array_t photo_dirs;

  gavl_array_t removables;
  int64_t removable_counter;

  gavl_array_t directory;
  char * directory_path;
  
  gavl_dictionary_t * local_container;
  gavl_dictionary_t * photo_container;
  
  int have_params;

  int mount_removable;
  
  bg_object_cache_t * cache;

  gavl_timer_t * timer;
  
  } fs_t;

static void save_root_children(bg_mdb_backend_t * be, dir_type_t type)
  {
  char * tmp_string;
  fs_t * fs = be->priv;
  
  switch(type)
    {
    case DIR_TYPE_FS_LOCAL:
      tmp_string = gavl_sprintf("%s/"LOCAL_DIRS_NAME, be->db->path);
      bg_array_save_xml(&fs->local_dirs, tmp_string, DIRS_ROOT);
      gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Saved %s", tmp_string);
      free(tmp_string);
      break;
    case DIR_TYPE_PHOTOALBUMS:
      tmp_string = gavl_sprintf("%s/"PHOTO_DIRS_NAME, be->db->path);
      bg_array_save_xml(&fs->photo_dirs, tmp_string, DIRS_ROOT);
      gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Saved %s", tmp_string);
      free(tmp_string);
      break;
    default:
      break;
    }
  }

static int load_dirent(gavl_dictionary_t * ret, const char * location)
  {
  struct stat st;
  if(stat(location, &st))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "stat failed for %s: %s", location, strerror(errno));
    return 0;
    }
  if(S_ISDIR(st.st_mode))
    gavl_dictionary_set_string(ret, GAVL_META_CLASS, GAVL_META_CLASS_DIRECTORY);
  else
    gavl_dictionary_set_string(ret, GAVL_META_CLASS, GAVL_META_CLASS_LOCATION);
  gavl_dictionary_set_long(ret, GAVL_META_MTIME, st.st_mtime);
  gavl_dictionary_set_string(ret, GAVL_META_URI, location);
  return 1;
  }

/* Load directory contents into the directory array (in terms of dirents) */

static void load_directory_contents(bg_mdb_backend_t * be, const char * path, gavl_array_t * ret)
  {
  glob_t g;
  char * pattern;
  int i;
  gavl_value_t val;
  gavl_dictionary_t * dict;

  fs_t * fs = be->priv;

  if(!ret)
    {
    if(fs->directory_path && !strcmp(fs->directory_path, path))
      return;

    fs->directory_path = gavl_strrep(fs->directory_path, path);
    gavl_array_reset(&fs->directory);
    ret = &fs->directory;
    }
  
  pattern = gavl_sprintf("%s/*", path);
  pattern = gavl_escape_string(pattern, "[]?");
  
  glob(pattern, 0, NULL /* errfunc */, &g);
  
  for(i = 0; i < g.gl_pathc; i++)
    {
    gavl_value_init(&val);
    dict = gavl_value_set_dictionary(&val);
    if(load_dirent(dict, g.gl_pathv[i]))
      gavl_array_splice_val_nocopy(ret, -1, 0, &val);
    else
      gavl_value_free(&val);
    }

  //  fprintf(stderr, "Got directory contents:\n");
  //  gavl_array_dump(&fs->directory, 2);
  
  globfree(&g);
  }

static int64_t obj_get_mtime(const gavl_dictionary_t * dict)
  {
  int64_t ret;

  //  fprintf(stderr, "obj_get_mtime\n");
  //  gavl_dictionary_dump(dict, 2);
  
  if((dict = gavl_dictionary_get_dictionary(dict, BG_MDB_DICT)) &&
     gavl_dictionary_get_long(dict, GAVL_META_MTIME, &ret))
    return ret;
  else
    return GAVL_TIME_UNDEFINED;
  }

static void obj_set_mtime(gavl_dictionary_t * dict, int64_t val)
  {
  dict = gavl_dictionary_get_dictionary_create(dict, BG_MDB_DICT);
  gavl_dictionary_set_long(dict, GAVL_META_MTIME, val);
  }

/*
 *  Load a directory and make sure that entries between start and num are
 *  fully loaded
 */

static int entry_supported(const gavl_value_t * dirent_val, dir_type_t type)
  {
  const gavl_dictionary_t * dirent;
  if(!(dirent = gavl_value_get_dictionary(dirent_val)))
    return 0;

  if(type == DIR_TYPE_PHOTOALBUMS)
    {
    const char * klass;
    const char * uri;

    if(!(klass = gavl_dictionary_get_string(dirent, GAVL_META_CLASS)))
      return 0;
    
    if(!strcmp(klass, GAVL_META_CLASS_DIRECTORY))
      return 1;
    else if(!strcmp(klass, GAVL_META_CLASS_LOCATION))
      {
      if(!(uri = gavl_dictionary_get_string(dirent, GAVL_META_URI)))
        return 0;

      if(bg_plugin_find_by_filename(uri, BG_PLUGIN_IMAGE_READER))
        return 1;
      else
        return 0;
      }
    else
      return 0;
    }
  else
    return 1;
  
  }

static void create_dummy_children(const gavl_array_t * dirents,
                                  gavl_array_t * children, dir_type_t type)
  {
  int i;
  gavl_value_t val;
  
  for(i = 0; i < dirents->num_entries; i++)
    {
    if(entry_supported(&dirents->entries[i], type))
      {
      gavl_value_init(&val);
      gavl_value_set_dictionary(&val);
      gavl_array_splice_val_nocopy(children, -1, 0, &val);
      }
    }
  }

/* Load the directory info */

static int load_directory_info(bg_mdb_backend_t * be,
                               gavl_dictionary_t * ret, const gavl_dictionary_t * dirent_p, dir_type_t type)
  {
  //  gavl_array_t arr;
  int num_items = 0;
  int num_containers = 0;
  const char * klass;
  char * uri;
  const char * child_uri;
  const gavl_dictionary_t * dict;
  gavl_dictionary_t dirent;
  int i;
  gavl_dictionary_t * m;
  gavl_dictionary_t * src;
  const char * pos;
  gavl_array_t arr;
  char * first_image = NULL;
  
  //  fs_t * fs = be->priv;

  gavl_dictionary_init(&dirent);
  gavl_dictionary_copy(&dirent, dirent_p);

  gavl_array_init(&arr);
  
  /* Local copy of URI because the dirent array will be reset */
  uri = gavl_strdup(gavl_dictionary_get_string(&dirent, GAVL_META_URI));
  
  load_directory_contents(be, uri, &arr);
  
  for(i = 0; i < arr.num_entries; i++)
    {
    if(!(dict = gavl_value_get_dictionary(&arr.entries[i])))
      continue;

    if(!(klass = gavl_dictionary_get_string(dict, GAVL_META_CLASS)))
      continue;

    if(!(child_uri = gavl_dictionary_get_string(dict, GAVL_META_URI)))
      continue;

    if(!strcmp(klass, GAVL_META_CLASS_LOCATION))
      {
      if(type == DIR_TYPE_PHOTOALBUMS)
        {
        if(bg_plugin_find_by_filename(child_uri, BG_PLUGIN_IMAGE_READER))
          {
          if(!first_image)
            first_image = gavl_strdup(child_uri);
          num_items++;
          }
        }
      else
        {
        if(!bg_file_supports_multitrack(child_uri))
          {
          num_items++;
          continue;
          }
        else
          {
          int num_tracks;
          /* Load track and check for multitrack */
          gavl_dictionary_t * mi = bg_plugin_registry_load_media_info(bg_plugin_reg, child_uri, 0);

          num_tracks = mi ? gavl_get_num_tracks(mi) : 0;
          
          //          fprintf(stderr, "Got multi track file %s %d\n", child_uri, num_tracks);
          if(num_tracks > 1)
            num_containers++;
          else
            num_items++;
          
          if(mi)
            gavl_dictionary_destroy(mi);
          }
        }
      
      }
    else if(!strcmp(klass, GAVL_META_CLASS_DIRECTORY))
      num_containers++;
    }

  m = gavl_dictionary_get_dictionary_create(ret, GAVL_META_METADATA);

  if((type == DIR_TYPE_PHOTOALBUMS) && num_items)
    {
    gavl_dictionary_set_string(m, GAVL_META_CLASS, GAVL_META_CLASS_PHOTOALBUM);
    if(first_image)
      {
      gavl_video_format_t fmt;
      gavl_dictionary_t img_m;
      memset(&fmt, 0, sizeof(fmt));
      gavl_dictionary_init(&img_m);

      if(bg_plugin_registry_probe_image(first_image, &fmt, &img_m, NULL))
        {
        gavl_metadata_add_image_uri(m,
                                    GAVL_META_ICON_URL,
                                    fmt.image_width, fmt.image_height,
                                    gavl_dictionary_get_string(&img_m, GAVL_META_MIMETYPE),
                                    first_image);
        }
      gavl_dictionary_free(&img_m);
      }
    }
  else 
    gavl_dictionary_set_string(m, GAVL_META_CLASS, GAVL_META_CLASS_DIRECTORY); 
  
  if((pos = strrchr(uri, '/')))
    gavl_dictionary_set_string(m, GAVL_META_LABEL, pos+1); 
  else
    gavl_dictionary_set_string(m, GAVL_META_LABEL, uri); 

  src = gavl_metadata_add_src(m, GAVL_META_SRC, NULL, uri);
  gavl_dictionary_copy_value(src, &dirent, GAVL_META_MTIME);
  gavl_track_set_num_children(ret, num_containers, num_items);

  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN,
           "Loaded %s, %d containers %d items", uri, num_containers, num_items);
  
  gavl_dictionary_free(&dirent);

  if(uri)
    free(uri);
  if(first_image)
    free(first_image);
  
  gavl_array_free(&arr);
  
  return 1;
  }

static dir_type_t id_to_type(const char * id)
  {
  if(gavl_string_starts_with(id, BG_MDB_ID_PHOTOS))
    return DIR_TYPE_PHOTOALBUMS;

  if(gavl_string_starts_with(id, BG_MDB_ID_DIRECTORIES))
    return DIR_TYPE_FS_LOCAL;

  if(gavl_string_starts_with(id, REMOVABLE_ID_PREFIX))
    return DIR_TYPE_FS_REMOVABLE;
  else
    return DIR_TYPE_UNKNOWN;
  }

static gavl_array_t * get_dir_array(bg_mdb_backend_t * be, dir_type_t type)
  {
  fs_t * fs = be->priv;
  switch(type)
    {
    case DIR_TYPE_FS_LOCAL:
      return &fs->local_dirs;
      break;
    case DIR_TYPE_FS_REMOVABLE:
      return &fs->removables;
      break;
    case DIR_TYPE_PHOTOALBUMS:
      return &fs->photo_dirs;
      break;
    default:
      return NULL;
    }
  }

static char * id_to_path(bg_mdb_backend_t * be, dir_type_t type, const char * id, int * track)
  {
  int i;
  char * ret = NULL;
  char * id_priv = NULL;
  //  fs_t * fs = be->priv;
  const char * pos;
  gavl_array_t * arr = NULL;

  if((pos = strstr(id, "/?track=")))
    {
    id_priv = gavl_strndup(id, pos);

    if(track)
      *track = atoi(pos + 8) - 1;
    id = id_priv;
    }
  
  arr = get_dir_array(be, type);
  
  for(i = 0; i < arr->num_entries; i++)
    {
    const gavl_dictionary_t * dict;
    const char * dir_id;
    const char * dir_uri;
    const char * pos;
    
    if((dict = gavl_value_get_dictionary(&arr->entries[i])) &&
       (dir_id = gavl_track_get_id(dict)) &&
       bg_mdb_is_ancestor(dir_id, id))
      {
      /*
       *  ID: /photos/<md5>
       *  URI: /media/archive/photos
       */
      gavl_track_get_src(dict, GAVL_META_SRC, 0, NULL, &dir_uri);
      pos = id + strlen(dir_id);
      ret = gavl_sprintf("%s%s", dir_uri, pos);
      break;
      }
    }

  if(id_priv)
    free(id_priv);
  
  return ret;
  }

static int load_item_info(bg_mdb_backend_t * be,
                          gavl_dictionary_t * ret,
                          const gavl_dictionary_t * dirent,
                          dir_type_t type)
  {
  int num_tracks;
  gavl_dictionary_t * mi;
  
  const char * uri = gavl_dictionary_get_string(dirent, GAVL_META_URI);
  const char * klass = gavl_dictionary_get_string(dirent, GAVL_META_CLASS);

  //  fprintf(stderr, "Load item info\n");
  //  gavl_dictionary_dump(dirent, 2);
  
  gavl_dictionary_reset(ret);
  
  /* Load directory */
  if(!strcmp(klass, GAVL_META_CLASS_DIRECTORY))
    {
    load_directory_info(be, ret, dirent, type);
    return 1;
    }
  
  if(!(mi = bg_plugin_registry_load_media_info(bg_plugin_reg, uri, 0)) ||
     !gavl_get_num_tracks(mi))
    {
    /* Unsupported file */
    const char * label;
    gavl_dictionary_t * m = gavl_dictionary_get_dictionary_create(ret, GAVL_META_METADATA);
    if((label = strrchr(uri, '/')))
      label++;
    else
      label = uri;
    gavl_dictionary_set_string(m, GAVL_META_LABEL, label);
    gavl_dictionary_set_string(m, GAVL_META_CLASS, GAVL_META_CLASS_FILE);

    if(mi)
      gavl_dictionary_destroy(mi);
    
    return 1;
    }

  num_tracks = gavl_get_num_tracks(mi);
  
  if(num_tracks > 1)
    {
    //    fprintf(stderr, "Got multitrack\n");
    //    gavl_dictionary_dump(mi, 2);
    
    gavl_dictionary_move(ret, mi);
    }
  else
    {
    gavl_dictionary_move(ret, gavl_get_track_nc(mi, 0));
    }
  
  if(mi)
    gavl_dictionary_destroy(mi);

  //  fprintf(stderr, "Load item info done\n");

  return 1;
  }


static int get_track_idx_by_uri(const gavl_array_t * arr, const char * uri)
  {
  int i;
  for(i = 0; i < arr->num_entries; i++)
    {
    const char * test_uri = NULL;
    const gavl_dictionary_t * dict;
    
    if((dict = gavl_value_get_dictionary(&arr->entries[i])) &&
       gavl_track_get_src(dict, GAVL_META_SRC, 0, NULL, &test_uri) &&
       !strcmp(test_uri, uri))
      return i;
    }
  return -1;
  }

static int get_dirent_idx_by_uri(const gavl_array_t * arr, const char * uri)
  {
  int i;
  for(i = 0; i < arr->num_entries; i++)
    {
    const char * test_uri = NULL;
    const gavl_dictionary_t * dict;
    
    if((dict = gavl_value_get_dictionary(&arr->entries[i])) &&
       (test_uri = gavl_dictionary_get_string(dict, GAVL_META_URI)) &&
       !strcmp(test_uri, uri))
      return i;
    }
  return -1;
  }

static char * path_to_id(const char * parent_id,
                         const char * path)
  {
  const char * pos;
  if((pos = strrchr(path, '/')))
    return gavl_sprintf("%s%s", parent_id, pos);
  else
    return gavl_sprintf("%s/%s", parent_id, path);
  }

static void refresh_children(bg_mdb_backend_t * be,
                             gavl_array_t * children,
                             const gavl_array_t * dirents,
                             dir_type_t type,
                             int start, int num, const char * parent_id)
  {
  int i;
  int idx = 0;
  int64_t dirent_mtime;
  int64_t child_mtime;
  gavl_dictionary_t * child;
  gavl_dictionary_t * child_m;
  const gavl_dictionary_t * dirent;
  const char * uri;
  int prev = -1;

  //  fprintf(stderr, "Refreshing children\n");
  
  for(i = 0; i < dirents->num_entries; i++)
    {
    if(!entry_supported(&dirents->entries[i], type))
      continue;

    if(idx < start)
      {
      idx++;
      continue;
      }

    dirent = gavl_value_get_dictionary(&dirents->entries[i]);
    child = gavl_value_get_dictionary_nc(&children->entries[idx]);
    
    // fprintf(stderr, "Got dirent:\n");
    // gavl_dictionary_dump(dirent, 2);

    child_mtime = obj_get_mtime(child);
    gavl_dictionary_get_long(dirent, GAVL_META_MTIME, &dirent_mtime);
    
    if((child_mtime == GAVL_TIME_UNDEFINED) ||
       (child_mtime != dirent_mtime))
      {
      gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Loading %s", gavl_dictionary_get_string(dirent, GAVL_META_URI));
      //      fprintf(stderr, "child_mtime:  %"PRId64"\n", child_mtime);
      //      fprintf(stderr, "dirent_mtime: %"PRId64"\n", dirent_mtime);
      /* Re-loading entry */
      gavl_dictionary_reset(child);
      load_item_info(be, child, dirent, type);
      /* load_item_info() frees the memory where dirent is in */

      obj_set_mtime(child, dirent_mtime);

      uri = gavl_dictionary_get_string(dirent, GAVL_META_URI);
      gavl_track_set_id_nocopy(child, path_to_id(parent_id, uri));
      }
    
    /* Set next/previous */

    child_m = gavl_dictionary_get_dictionary_create(child, GAVL_META_METADATA);
    
    if(prev >= 0)
      {
      dirent = gavl_value_get_dictionary(&dirents->entries[prev]);
      uri = gavl_dictionary_get_string(dirent, GAVL_META_URI);
      gavl_dictionary_set_string_nocopy(child_m, GAVL_META_PREVIOUS_ID, path_to_id(parent_id, uri));
      }
    if(i < dirents->num_entries-1)
      {
      int next = i + 1;

      while(next < dirents->num_entries)
        {
        if(!entry_supported(&dirents->entries[next], type))
          {
          next++;
          continue;
          }
        dirent = gavl_value_get_dictionary(&dirents->entries[next]);
        uri = gavl_dictionary_get_string(dirent, GAVL_META_URI);
        gavl_dictionary_set_string_nocopy(child_m, GAVL_META_NEXT_ID, path_to_id(parent_id, uri));
        break;
        }
      }
    
    prev = i;
    idx++;
    
    if(idx >= start + num)
      break;
    }
  
  }

static int refresh_root_children(bg_mdb_backend_t * be,
                                 gavl_array_t * children,
                                 dir_type_t type,
                                 int start, int num)
  {
  int ret = 0;
  const char * uri;
  gavl_dictionary_t * child;
  const gavl_dictionary_t * src;
  int64_t child_mtime;
  int i;
  struct stat st;
  int changed = 0;

  //  fs_t * fs = be->priv;
  
  for(i = 0; i < num; i++)
    {
    if(!(child = gavl_value_get_dictionary_nc(&children->entries[i + start])))
      continue;
    if(!(src = gavl_track_get_src(child, GAVL_META_SRC, 0, NULL, &uri)))
      continue;
    
    if(!gavl_dictionary_get_long(src, GAVL_META_MTIME, &child_mtime))
      continue;
    
    if(stat(uri, &st))
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "stat failed for %s: %s", uri, strerror(errno));
      continue;
      }

    if(child_mtime != st.st_mtime)
      {
      char hash[GAVL_MD5_LENGTH];
      gavl_dictionary_t dirent;
      gavl_dictionary_init(&dirent);
      
      gavl_log(GAVL_LOG_INFO, LOG_DOMAIN,
               "Reloading toplevel directory %s (mtime changed, %"PRId64" != %"PRId64")",
               uri, child_mtime, st.st_mtime);
      
      if(!load_dirent(&dirent, uri))
        {
        /* Error */
        gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN,
                 "Reloading toplevel directory %s failed", uri);
        }
      
      gavl_dictionary_reset(child);
      load_directory_info(be, child, &dirent, type);

      bg_get_filename_hash(uri, hash);

      if(type == DIR_TYPE_FS_LOCAL)
        gavl_track_set_id_nocopy(child, gavl_sprintf("%s/%s", BG_MDB_ID_DIRECTORIES, hash));
      else if(type == DIR_TYPE_PHOTOALBUMS)
        gavl_track_set_id_nocopy(child, gavl_sprintf("%s/%s", BG_MDB_ID_PHOTOS, hash));
      
      gavl_dictionary_free(&dirent);
      changed = 1;
      ret++;
      }
    }

  if(changed)
    save_root_children(be, type);
  
  return ret;
  }

/*
 *  Called if *some* of the directory entries were added or removed
 *  (i.e. if the directory mtime changed)
 */

static void reload_children(bg_mdb_backend_t * be,
                            gavl_array_t * children,
                            const gavl_array_t * dirents,
                            dir_type_t type)
  {
  gavl_array_t save;
  const gavl_dictionary_t * dirent;
  int i;
  const char * uri;
  int idx_save;
  gavl_value_t val_empty;

  gavl_value_init(&val_empty);
  gavl_value_set_dictionary(&val_empty);
  
  gavl_array_init(&save);
  gavl_array_move(&save, children);

  for(i = 0; i < dirents->num_entries; i++)
    {
    if(!entry_supported(&dirents->entries[i], type))
      continue;
    dirent = gavl_value_get_dictionary(&dirents->entries[i]);

    if((uri = gavl_dictionary_get_string(dirent, GAVL_META_URI)) &&
       ((idx_save = get_track_idx_by_uri(&save, uri)) >= 0))
      {
      gavl_array_splice_val_nocopy(children, -1, 0, &save.entries[idx_save]);
      }
    else
      {
      gavl_array_splice_val(children, -1, 0, &val_empty);
      }
    }
  gavl_array_free(&save);
  }

static void finalize_object(bg_mdb_backend_t * be, gavl_dictionary_t * track)
  {
  const char * klass;
  const char * uri = NULL;
  /* Create thumbnails */
  if((klass = gavl_track_get_media_class(track)))
    {
    if(!strcmp(klass, GAVL_META_CLASS_IMAGE) &&
       gavl_track_get_src(track, GAVL_META_SRC, 0, NULL, &uri))
      bg_mdb_make_thumbnails(be->db, uri);
    
    if(!strcmp(klass, GAVL_META_CLASS_PHOTOALBUM) &&
       gavl_track_get_src(track, GAVL_META_ICON_URL, 0, NULL, &uri))
      bg_mdb_make_thumbnails(be->db, uri);
    }
  
  bg_mdb_add_http_uris(be->db, track);
  }

static int browse_object(bg_mdb_backend_t * be, gavl_msg_t * msg)
  {
  gavl_array_t * arr;
  char * parent_id = NULL;
  char * parent_path = NULL;
  char * path = NULL;
  int num_tracks = 0;
  
  dir_type_t type;
  int ret = 0;
  int depth;
  const char * ctx_id;
  gavl_msg_t * res;
  gavl_dictionary_t dict;
  const gavl_dictionary_t * dict_p;
  fs_t * fs = be->priv;

  int idx = -1;
  
  gavl_dictionary_init(&dict);

  ctx_id = gavl_dictionary_get_string(&msg->header, GAVL_MSG_CONTEXT_ID);

  type = id_to_type(ctx_id);

  if((depth = bg_mdb_id_get_depth(ctx_id)) < 2)
    goto end;

  //  fprintf(stderr, "browse_object: id: %s, depth: %d\n", ctx_id, depth);
  
  if((type != DIR_TYPE_FS_REMOVABLE) && (depth == 2))
    {
    arr = get_dir_array(be, type);
    idx = gavl_get_track_idx_by_id_arr(arr, ctx_id);
    if(idx < 0)
      goto end;
    
    if(!(dict_p = gavl_value_get_dictionary(&arr->entries[idx])))
      goto end;

    res = bg_msg_sink_get(be->ctrl.evt_sink);
    bg_mdb_set_browse_obj_response(res, dict_p, msg, idx, arr->num_entries);
    bg_msg_sink_put(be->ctrl.evt_sink);
    ret = 1;
    goto end;
    }

  parent_id = bg_mdb_get_parent_id(ctx_id);
  parent_path = id_to_path(be, type, parent_id, NULL);
  path = id_to_path(be, type, ctx_id, &idx);

  /* Multi-track */
  if(idx >= 0)
    {
    gavl_dictionary_t * mi;
    gavl_dictionary_t * m;
    /* Multi track */

    if(!(mi = bg_plugin_registry_load_media_info(bg_plugin_reg, path, 0)))
      goto end;
    
    num_tracks = gavl_get_num_tracks(mi);
    
    gavl_dictionary_move(&dict, gavl_get_track_nc(mi, idx));
    
    m = gavl_track_get_metadata_nc(&dict);
    
    if(idx > 0)
      {
      /* idx + 1 - 1 */
      gavl_dictionary_set_string_nocopy(m, GAVL_META_PREVIOUS_ID, gavl_sprintf("%s/?track=%d", parent_id, idx));
      }
    
    if(idx < num_tracks - 1)
      {
      gavl_dictionary_set_string_nocopy(m, GAVL_META_NEXT_ID, gavl_sprintf("%s/?track=%d", parent_id, idx + 2));
      }
    
    }
  else
    {
    gavl_dictionary_t * m;
    
    /* No Multi-track (file or directory) */

    //  fprintf(stderr, "Got parent path: %s\n", parent_path);
    
    load_directory_contents(be, parent_path, NULL);
    
    if((idx = get_dirent_idx_by_uri(&fs->directory, path)) < 0)
      goto end;
    
    //  fprintf(stderr, "Index: %d\n", idx);
    
    if(!load_item_info(be, &dict, gavl_value_get_dictionary(&fs->directory.entries[idx]), type))
      goto end;

    /* next, previous */

    m = gavl_track_get_metadata_nc(&dict);
  
    if((idx > 0) &&
       (dict_p = gavl_value_get_dictionary(&fs->directory.entries[idx-1])))
      {
      /* Set previous */
      gavl_dictionary_set_string_nocopy(m, GAVL_META_PREVIOUS_ID,
                                        path_to_id(parent_id, gavl_dictionary_get_string(dict_p, GAVL_META_URI)));
      }
    if((idx < fs->directory.num_entries - 1) &&
       (dict_p = gavl_value_get_dictionary(&fs->directory.entries[idx+1])))
      {
      /* Set next */
      gavl_dictionary_set_string_nocopy(m, GAVL_META_NEXT_ID,
                                        path_to_id(parent_id, gavl_dictionary_get_string(dict_p, GAVL_META_URI)));
    
      }
    
    }
  
  gavl_track_set_id(&dict, ctx_id);
  finalize_object(be, &dict);
  
  res = bg_msg_sink_get(be->ctrl.evt_sink);
  bg_mdb_set_browse_obj_response(res, &dict, msg, idx, fs->directory.num_entries);
  bg_msg_sink_put(be->ctrl.evt_sink);
  
  ret = 1;

  end:

  gavl_dictionary_free(&dict);
  
  if(parent_id)
    free(parent_id);
  if(parent_path)
    free(parent_path);
  if(path)
    free(path);
  
  return ret;
  
  } 

static void browse_children_multitrack(bg_mdb_backend_t * be, gavl_msg_t * msg,
                                       const char * id,
                                       const char * path,
                                       int start, int num)
  {
  gavl_dictionary_t * mi;

  gavl_msg_t * res;
  int i, num_tracks;
  const gavl_array_t * tracks;
  
  mi = bg_plugin_registry_load_media_info(bg_plugin_reg, path, 0);
  tracks = gavl_get_tracks(mi);
  
  num_tracks = gavl_get_num_tracks(mi);

  if(!bg_mdb_adjust_num(start, &num, num_tracks))
    goto cleanup;
  
  for(i = 0; i < num_tracks; i++)
    gavl_track_set_id_nocopy(gavl_get_track_nc(mi, i), gavl_sprintf("%s/?track=%d", id, i+1));

  res = bg_msg_sink_get(be->ctrl.evt_sink);
  
  if(num < num_tracks)
    {
    int i;
    gavl_array_t arr_tmp;
    gavl_array_init(&arr_tmp);

    
    for(i = 0; i < num; i++)
      gavl_array_splice_val(&arr_tmp, i, 0, &tracks->entries[i+start]);
    
    bg_mdb_set_browse_children_response(res, &arr_tmp, msg, &start, 1, tracks->num_entries);
    gavl_array_free(&arr_tmp);
    }
  else
    {
    bg_mdb_set_browse_children_response(res, tracks, msg, &start, 1, tracks->num_entries);
    }
  
  bg_msg_sink_put(be->ctrl.evt_sink);

  cleanup:
  
  if(mi)
    gavl_dictionary_destroy(mi);
  }

static int browse_children(bg_mdb_backend_t * be, gavl_msg_t * msg)
  {
  int ret = 0;
  dir_type_t type;
  const char * ctx_id = NULL;
  int start = 0, num = 0, one_answer = 0;
  char * path = NULL;
  gavl_dictionary_t dirent;
  int depth;
  gavl_array_t * arr;
  gavl_array_t arr_static;
  gavl_msg_t * res;
  const char * klass;
  int i, idx;
  gavl_array_t arr_tmp;
  gavl_time_t last_flush_time, cur;
  fs_t * fs = be->priv;
  
  gavl_array_init(&arr_static);
  gavl_array_init(&arr_tmp);
  
  gavl_dictionary_init(&dirent);
  
  ctx_id = gavl_dictionary_get_string(&msg->header, GAVL_MSG_CONTEXT_ID);
  bg_mdb_get_browse_children_request(msg, &ctx_id, &start, &num, &one_answer);

  type = id_to_type(ctx_id);
  
  if((depth = bg_mdb_id_get_depth(ctx_id)) < 1)
    goto fail;
  
  /* Browse children */
  
  if(type == DIR_TYPE_FS_REMOVABLE)
    {
    /* /volume-1 */
    /* /volume-1/bla */
    path = id_to_path(be, type, ctx_id, NULL);

    if(!load_dirent(&dirent, path))
      goto fail;

    if(!(klass = gavl_dictionary_get_string(&dirent, GAVL_META_CLASS)))
      goto fail;
    
    if(!strcmp(klass, GAVL_META_CLASS_LOCATION))
      {
      browse_children_multitrack(be, msg, ctx_id, path, start, num);
      }
    
    arr = &arr_static;
    load_directory_contents(be, path, NULL);
    /* Load objects */
    create_dummy_children(&fs->directory, arr, type);

    if(!bg_mdb_adjust_num(start, &num, arr->num_entries))
      goto fail;
    
    refresh_children(be, arr, &fs->directory, type, start, num, ctx_id);
    
    }
  else
    {
    if(depth == 1)
      {
      /* /photos */
      arr = get_dir_array(be, type);
      if(!bg_mdb_adjust_num(start, &num, arr->num_entries))
        goto fail;
      
      refresh_root_children(be, arr, type, start, num);
      goto found;
      }
    else
      {
      int64_t mtime;
      int64_t dirent_mtime = 0;

      gavl_value_t * cache_val;
      gavl_dictionary_t * cache_dict;
      
      /* /photos/dir1 */
      path = id_to_path(be, type, ctx_id, NULL);
      /* Check cache */
      
      if(!load_dirent(&dirent, path))
        goto fail;

      if(!(klass = gavl_dictionary_get_string(&dirent, GAVL_META_CLASS)))
        goto fail;
    
      if(!strcmp(klass, GAVL_META_CLASS_LOCATION))
        browse_children_multitrack(be, msg, ctx_id, path, start, num);
      
      gavl_dictionary_get_long(&dirent, GAVL_META_MTIME, &dirent_mtime);
      
      if((cache_val = bg_object_cache_get(fs->cache, ctx_id)))
        {
        /* Got object from cache, let's check if it's up top date */
        cache_dict = gavl_value_get_dictionary_nc(cache_val);
        
        mtime = obj_get_mtime(cache_dict);
        arr = gavl_dictionary_get_array_nc(cache_dict, GAVL_META_CHILDREN);
        
        if(mtime != dirent_mtime)
          {
          gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Directory mtime changed, updating");
          fprintf(stderr, "Directory mtime changed, updating (%"PRId64" %"PRId64")\n", mtime, dirent_mtime);
          /* TODO */
          load_directory_contents(be, path, NULL);
          reload_children(be, arr, &fs->directory, type);
          
          obj_set_mtime(cache_dict, dirent_mtime);
          }
        else
          gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Got directory from cache");
        }
      else
        {
        gavl_value_t new_entry;
        
        /* Got no cache object, create new one */
        
        gavl_value_init(&new_entry);
        gavl_value_set_dictionary(&new_entry);
        
        cache_val = bg_object_cache_put_nocopy(fs->cache, ctx_id, &new_entry);
        cache_dict = gavl_value_get_dictionary_nc(cache_val);
        
        arr = gavl_dictionary_get_array_create(cache_dict, GAVL_META_CHILDREN);
        load_directory_contents(be, path, NULL);
        create_dummy_children(&fs->directory, arr, type);

        obj_set_mtime(cache_dict, dirent_mtime);
        }
      
      if(!bg_mdb_adjust_num(start, &num, arr->num_entries))
        goto fail;

      //  load_directory_contents(be, path);
      refresh_children(be, arr, &fs->directory, type, start, num, ctx_id);
      goto found;
      }
    }
  
  found:

  if(!arr)
    goto fail;

  /* Return children */
  
  last_flush_time = gavl_timer_get(fs->timer);

  idx = start;
  
  for(i = 0; i < num; i++)
    {
    gavl_dictionary_t * track;
    
    gavl_array_splice_val(&arr_tmp, -1, 0, &arr->entries[i+start]);
    track = gavl_value_get_dictionary_nc(&arr_tmp.entries[arr_tmp.num_entries-1]);
    finalize_object(be, track);
    
    cur = gavl_timer_get(fs->timer);
    
    if(!one_answer && (cur - last_flush_time > GAVL_TIME_SCALE))
      {
      /* Flush what we got so far */
      
      res = bg_msg_sink_get(be->ctrl.evt_sink);
      bg_mdb_set_browse_children_response(res, &arr_tmp, msg, &idx, (i == num-1), arr->num_entries);
      bg_msg_sink_put(be->ctrl.evt_sink);

      //      fprintf(stderr, "Flush 1\n");
      //      gavl_array_dump(&arr_tmp, 2);
      
      gavl_array_reset(&arr_tmp);

      
      last_flush_time = cur;
      }
    }

  if(arr_tmp.num_entries)
    {
    res = bg_msg_sink_get(be->ctrl.evt_sink);
    bg_mdb_set_browse_children_response(res, &arr_tmp, msg, &idx, 1, arr->num_entries);
    bg_msg_sink_put(be->ctrl.evt_sink);

    //    fprintf(stderr, "Flush 2\n");
    //    gavl_array_dump(&arr_tmp, 2);
    
    }
  
  ret = 1;


  fail:
  
  gavl_dictionary_free(&dirent);
  gavl_array_free(&arr_static);
  gavl_array_free(&arr_tmp);

  return ret;
  } 

static void destroy_filesystem(bg_mdb_backend_t * b)
  {
  fs_t * fs = b->priv;

  gavl_array_free(&fs->local_dirs);
  gavl_array_free(&fs->photo_dirs);
  gavl_array_free(&fs->removables);
  
  bg_object_cache_destroy(fs->cache);
  gavl_timer_destroy(fs->timer);
  free(fs);
  }

static int add_directory(bg_mdb_backend_t * b, dir_type_t type, int * idx, const char * location,
                         const char * volume_id)
  {
  int ret = 0;
  gavl_dictionary_t dirent;
  gavl_value_t add_val;
  gavl_dictionary_t * add_dict;
  gavl_array_t * arr = NULL;
  gavl_msg_t * evt;
  gavl_dictionary_t * m;
  char hash[GAVL_MD5_LENGTH];
  
  const char * parent_id = NULL;
  gavl_dictionary_t * parent = NULL;
  char * location_priv = NULL;
  int location_len;
  fs_t * fs = b->priv;

  fprintf(stderr, "Add directory %s\n", location);
  
  location_len = strlen(location);
  
  if((location_len > 1) && (location[location_len-1] == '/'))
    {
    location_priv = gavl_strndup(location, location + (location_len - 1));
    location = location_priv;
    }
  gavl_dictionary_init(&dirent);
  gavl_value_init(&add_val);
  add_dict = gavl_value_set_dictionary(&add_val);
  if(!load_dirent(&dirent, location))
    goto fail;
  
  arr = get_dir_array(b, type);

  if(get_track_idx_by_uri(arr, location) >= 0)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Directory %s is already added", location);
    goto fail;
    }
  
  if(*idx < 0)
    *idx = arr->num_entries;
  if(*idx > arr->num_entries)
    return 0;
  
  //  fprintf(stderr, "Got dirent:\n");
  //  gavl_dictionary_dump(&dirent, 2);
  load_directory_info(b, add_dict, &dirent, type);
  
  m = gavl_track_get_metadata_nc(add_dict);
  
  switch(type)
    {
    case DIR_TYPE_FS_LOCAL:
      parent_id = BG_MDB_ID_DIRECTORIES;
      bg_get_filename_hash(location, hash);
      gavl_dictionary_set_string_nocopy(m, GAVL_META_ID,
                                        gavl_sprintf("%s/%s", parent_id, hash));
      parent = fs->local_container;
      break;
    case DIR_TYPE_PHOTOALBUMS:
      parent_id = BG_MDB_ID_PHOTOS;
      bg_get_filename_hash(location, hash);
      gavl_dictionary_set_string_nocopy(m, GAVL_META_ID,
                                        gavl_sprintf("%s/%s", parent_id, hash));
      parent = fs->photo_container;
      break;
    case DIR_TYPE_FS_REMOVABLE:
      gavl_dictionary_set_string_nocopy(m, GAVL_META_ID, gavl_sprintf(REMOVABLE_ID_PREFIX"-%"PRId64,
                                                                    ++fs->removable_counter));
      gavl_dictionary_set_string(m, VOLUME_ID, volume_id);
      gavl_dictionary_set_string(m, GAVL_META_CLASS, GAVL_META_CLASS_ROOT_REMOVABLE_FILESYSTEM); 
      bg_mdb_container_set_backend(add_dict, MDB_BACKEND_FILESYSTEM);
      break;
    default:
      goto fail;
                                                      
    }

  //  fprintf(stderr, "Got directory: %p\n", parent);
  //  gavl_dictionary_dump(add_dict, 2);
  
  /* Splice array */
  
  gavl_array_splice_val_nocopy(arr, *idx, 0, &add_val);
  
  if(parent_id && parent)
    {
    bg_mdb_set_idx_total(arr, 0, arr->num_entries);
    bg_mdb_set_next_previous(arr);

    /* Send splice children */
    evt = bg_msg_sink_get(b->ctrl.evt_sink);
    bg_msg_set_splice_children(evt, BG_MSG_DB_SPLICE_CHILDREN,
                               parent_id, 1, *idx, 0, &arr->entries[arr->num_entries-1]);
    bg_msg_sink_put(b->ctrl.evt_sink);

    /* Send parent changed */

    gavl_track_set_num_children( parent, arr->num_entries, 0);
    
    evt = bg_msg_sink_get(b->ctrl.evt_sink);

    gavl_msg_set_id_ns(evt, BG_MSG_DB_OBJECT_CHANGED, BG_MSG_NS_DB);

    gavl_dictionary_set_string(&evt->header, GAVL_MSG_CONTEXT_ID, parent_id);
    
    gavl_msg_set_arg_dictionary(evt, 0, parent);
    bg_msg_sink_put(b->ctrl.evt_sink);
    }
  else
    {
    bg_mdb_add_root_container(b->ctrl.evt_sink, add_dict);
    //    fprintf(stderr, "Added filesystem\n");
    //    gavl_dictionary_dump(add_dict, 2);
    }
  (*idx)++;
  
  ret = 1;  
  fail:

  if(location_priv)
    free(location_priv);
  
  gavl_dictionary_free(&dirent);
  gavl_value_free(&add_val);
  
  return ret;
  }

static void do_splice(bg_mdb_backend_t * b, const char * ctx_id, int last, int idx,
                      int del, gavl_value_t * add, int sendmsg)
  {
  dir_type_t type;
  const gavl_dictionary_t * dict;
  const gavl_dictionary_t * m;
  fs_t * fs = b->priv;
  gavl_array_t * arr;
  
  //  fprintf(stderr, "Splice %s %d %d %d\nAdd:\n", ctx_id, last, idx, del);
  //  gavl_value_dump(add, 2);

  type = id_to_type(ctx_id);
  
  arr = get_dir_array(b, type);

  if(idx < 0)
    idx = arr->num_entries;
  
  if(del < 0)
    del = arr->num_entries - idx;
  
  /* Delete */
  if(del != 0)
    {
    gavl_msg_t * evt;
    gavl_dictionary_t * parent;
    
    gavl_array_splice_val(arr, idx, del, NULL);
    
    switch(type)
      {
      case DIR_TYPE_FS_LOCAL:
        parent = fs->local_container;
        break;
      case DIR_TYPE_PHOTOALBUMS:
        parent = fs->photo_container;
        break;
      default:
        return;
      }
    
    /* */
    
    bg_mdb_set_idx_total(arr, 0, arr->num_entries);
    bg_mdb_set_next_previous(arr);

    /* Send splice children */
    evt = bg_msg_sink_get(b->ctrl.evt_sink);
    bg_msg_set_splice_children(evt, BG_MSG_DB_SPLICE_CHILDREN,
                               ctx_id, 1, idx, del, NULL);
    bg_msg_sink_put(b->ctrl.evt_sink);
    
    /* Send parent changed */
    
    gavl_track_set_num_children(parent, arr->num_entries, 0);
    evt = bg_msg_sink_get(b->ctrl.evt_sink);
    gavl_msg_set_id_ns(evt, BG_MSG_DB_OBJECT_CHANGED, BG_MSG_NS_DB);
    gavl_dictionary_set_string(&evt->header, GAVL_MSG_CONTEXT_ID, ctx_id);
    gavl_msg_set_arg_dictionary(evt, 0, parent);
    bg_msg_sink_put(b->ctrl.evt_sink);
    

    /* */
    
    
    }

  
  /* ADD */
  if(add->type == GAVL_TYPE_DICTIONARY)
    {
    dict = gavl_value_get_dictionary(add);
    m = gavl_track_get_metadata(dict);
    add_directory(b, type, &idx, gavl_dictionary_get_string(m, GAVL_META_URI), NULL);
    }
  else if(add->type == GAVL_TYPE_ARRAY)
    {
    int i;
    const gavl_array_t * add_arr;

    add_arr = gavl_value_get_array(add);

    for(i = 0; i < arr->num_entries; i++)
      {
      dict = gavl_value_get_dictionary(&add_arr->entries[i]);
      m = gavl_track_get_metadata(dict);
      add_directory(b, type, &idx, gavl_dictionary_get_string(m, GAVL_META_URI), NULL);
      }
    
    }
  
  /* Save directory list */

  save_root_children(b, type);

#if 0  
  switch(type)
    {
    case DIR_TYPE_FS_LOCAL:
      tmp_string = gavl_sprintf("%s/"LOCAL_DIRS_NAME, b->db->path);
      bg_array_save_xml(&fs->local_dirs, tmp_string, DIRS_ROOT);
      gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Saved %s", tmp_string);
      free(tmp_string);
      break;
    case DIR_TYPE_PHOTOALBUMS:
      tmp_string = gavl_sprintf("%s/"PHOTO_DIRS_NAME, b->db->path);
      bg_array_save_xml(&fs->photo_dirs, tmp_string, DIRS_ROOT);
      gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Saved %s", tmp_string);
      free(tmp_string);
      break;
    default:
      break;
    }
#endif
  
  }

static int handle_msg_filesystem(void * priv, gavl_msg_t * msg)
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
          int last = 0;
          int idx = 0;
          int del = 0;
          gavl_value_t add;
          gavl_value_init(&add);
          
          gavl_msg_get_splice_children(msg, &last, &idx, &del, &add);

          /* TODO */
          do_splice(be, gavl_dictionary_get_string(&msg->header, GAVL_MSG_CONTEXT_ID), last, idx, del, &add, 1);
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
        case GAVL_MSG_RESOURCE_ADDED:
          {
          gavl_dictionary_t obj;
          gavl_dictionary_t vol;
          const char * volume_id;
          const char * uri;
          const char * klass;
          int idx = 0;
          //          const char * pos;

          gavl_dictionary_init(&vol);
          gavl_dictionary_init(&obj);

          if(!fs->mount_removable ||
             !gavl_msg_get_arg_dictionary(msg, 0, &vol) ||
             !(klass = gavl_dictionary_get_string(&vol, GAVL_META_CLASS)) ||
             !gavl_string_starts_with(klass, GAVL_META_CLASS_ROOT_REMOVABLE_FILESYSTEM))
            {
            gavl_dictionary_free(&vol);
            return 1;
            }
          
          volume_id = gavl_dictionary_get_string(&msg->header, GAVL_MSG_CONTEXT_ID);

          //          fprintf(stderr, "Volume added: %s\n", volume_id);
          //          gavl_dictionary_dump(&vol, 2);
          
          if(!(klass = gavl_dictionary_get_string(&vol, GAVL_META_CLASS)))
            return 1;

          uri = gavl_dictionary_get_string(&vol, GAVL_META_URI);
          
          if(!fs->mount_removable || !gavl_string_starts_with(uri, "/media"))
            {
            return 1;
            }

          add_directory(be, DIR_TYPE_FS_REMOVABLE, &idx, uri, volume_id);
          }
          break;
        case GAVL_MSG_RESOURCE_DELETED:
          {
          int i;
          const char * test_id;
          const char * id;
          const gavl_dictionary_t * d;
          const char * uri;
          
          id = gavl_dictionary_get_string(&msg->header, GAVL_MSG_CONTEXT_ID);

          for(i = 0; i < fs->removables.num_entries; i++)
            {
            if((d = gavl_value_get_dictionary(&fs->removables.entries[i])) &&
               (d = gavl_track_get_metadata(d)) &&
               (test_id = gavl_dictionary_get_string(d, VOLUME_ID)) &&
               !strcmp(test_id, id))
              {
              gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Volume removed %s", id); 

              gavl_metadata_get_src(d, GAVL_META_SRC, 0, NULL, &uri);
              
              /* Send message */
              bg_mdb_unexport_media_directory(be->ctrl.evt_sink, uri);
              bg_mdb_delete_root_container(be->ctrl.evt_sink, gavl_dictionary_get_string(d, GAVL_META_ID));
              /* Remove locally */
              gavl_array_splice_val(&fs->removables, i, 1, NULL);
              break;
              }
            }
          break;
          }





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
          else if(!strcmp(name, "mount_removable"))
            {
            fs->mount_removable = val.v.i;
            //   fprintf(stderr, "mount removable: %d\n", fs->mount_removable);
            }
          
          /* Pass to core to store it in the config registry */
          if(fs->have_params)
            {
            resp = bg_msg_sink_get(be->ctrl.evt_sink);
            bg_msg_set_parameter_ctx(resp, BG_MSG_PARAMETER_CHANGED_CTX, MDB_BACKEND_FILESYSTEM, name, &val);
            bg_msg_sink_put(be->ctrl.evt_sink);
          
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
    { /* End */ },
  };

void bg_mdb_create_filesystem(bg_mdb_backend_t * b)
  {
  fs_t * priv;
  char * tmp_string;
  priv = calloc(1, sizeof(*priv));
  b->priv = priv;

  b->parameters = parameters;
  
  b->flags |= BE_FLAG_RESOURCES;
  
  
  b->destroy = destroy_filesystem;

  bg_controllable_init(&b->ctrl,
                       bg_msg_sink_create(handle_msg_filesystem, b, 0),
                       bg_msg_hub_create(1));
  
  /* Add children */
  
  priv->local_container = bg_mdb_get_root_container(b->db, GAVL_META_CLASS_ROOT_DIRECTORIES);
  bg_mdb_container_set_backend(priv->local_container, MDB_BACKEND_FILESYSTEM);
  bg_mdb_add_can_add(priv->local_container, GAVL_META_CLASS_DIRECTORY);
  bg_mdb_set_editable(priv->local_container);

  priv->photo_container = bg_mdb_get_root_container(b->db, GAVL_META_CLASS_ROOT_PHOTOS);
  bg_mdb_container_set_backend(priv->photo_container, MDB_BACKEND_FILESYSTEM);
  bg_mdb_add_can_add(priv->photo_container, GAVL_META_CLASS_DIRECTORY);
  bg_mdb_set_editable(priv->photo_container);
  
  /* Create cache */  
  tmp_string = gavl_sprintf("%s/fs_cache", b->db->path);
  priv->cache = bg_object_cache_create(1024, 32, tmp_string);
  free(tmp_string);

  /* Load directories */

  tmp_string = gavl_sprintf("%s/"LOCAL_DIRS_NAME, b->db->path);
  if(!access(tmp_string, R_OK))
    bg_array_load_xml(&priv->local_dirs, tmp_string, DIRS_ROOT);
  gavl_track_set_num_children(priv->local_container, priv->local_dirs.num_entries, 0);
  free(tmp_string);
  
  tmp_string = gavl_sprintf("%s/"PHOTO_DIRS_NAME, b->db->path);
  if(!access(tmp_string, R_OK))
    bg_array_load_xml(&priv->photo_dirs, tmp_string, DIRS_ROOT);
  gavl_track_set_num_children(priv->photo_container, priv->photo_dirs.num_entries, 0);
  free(tmp_string);
  
  //  load_dir_list(b, &priv->photo_dirs);
  //  load_dir_list(b, &priv->local_dirs);

  priv->timer = gavl_timer_create();
  gavl_timer_start(priv->timer);
  
  }
