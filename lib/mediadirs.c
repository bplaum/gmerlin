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

#include <pthread.h>
#include <string.h>


#include <gmerlin/httpserver.h>
#include <gmerlin/utils.h>

#include <gmerlin/translation.h>


#define LOG_DOMAIN "mediadirs"
#include <gmerlin/log.h>

#include <gavl/utils.h>
#include <gavl/http.h>



#define LOCAL_PATH  "l"
#define HTTP_PATH "r"

#define PATH_PREFIX "/media/"

/*
 *  Access to media files
 *  
 *
 *  A media file under /path/to_media_files/subdir/path
 *
 *  will be available as
 *
 *  http://example.com:8888/media/403fd4c2a411d838a8121692a7730da5/subdir/path
 *
 *  where "403fd4c2a411d838a8121692a7730da5" is the md5 sum of "/path/to_media_files/"
 *
 *
 *
 */


static const bg_parameter_info_t parameters[] =
  {
    {
      .name =      "export_media_dirs",
      .type = BG_PARAMETER_CHECKBUTTON,
      .long_name   =  TRS("Export media files"),
      .help_string =  TRS("Make media files available in the LAN. You can in- and exclude specific directories below."),
      
    },
    {
      .name =      "export_dirs",
      .type = BG_PARAMETER_DIRLIST,
      .long_name   =  TRS("Directories to export"),
      .help_string =  TRS("Files under these directories are exported in the LAN."),
    },
    {
      .name =      "restrict_dirs",
      .type = BG_PARAMETER_DIRLIST,
      .long_name   =  TRS("Restricted directories"),
      .help_string =  TRS("Files under these directories are *not* exported in the LAN."),
    },
    { /* */ },
  };

struct bg_media_dirs_s
  {
  char * root_uri;

  
  pthread_mutex_t mutex;
  
  int do_export;
  gavl_array_t export_dirs;
  gavl_array_t restricted_dirs;
  
  gavl_array_t dirs;
  };

/* Do all access checking here */

static int find_by_local_path(bg_media_dirs_t * d, const char * path);

static int array_contains(gavl_array_t * arr, const char * dir)
  {
  int i;
  const char * arr_dir;

  if(!dir)
    return 0;
  
  for(i = 0; i < arr->num_entries; i++)
    {
    if((arr_dir = gavl_string_array_get(arr, i)) &&
       (!strcmp(arr_dir, dir) ||
        (gavl_string_starts_with(dir, arr_dir) &&
         (dir[strlen(arr_dir)] == '/'))))
      {
      return 1;
      }
    }
  return 0;
  }

static int do_export(bg_media_dirs_t * dirs, const char * path)
  {
  int ret = 0;
  
  char * cpath;

  //  fprintf(stderr, "do export: %d %s\n", dirs->do_export, path);
  
  if(!dirs->do_export)
    return 0;

  //  if(gavl_string_starts_with(path, "/nas"))
  //    fprintf(stderr, "Blupp\n");
  
  cpath = bg_canonical_filename(path);
  
  /* Check if it is allowed */
  if((path && find_by_local_path(dirs, path) >= 0) ||
     (cpath && find_by_local_path(dirs, cpath) >= 0))
    ret = 1;
  else if(array_contains(&dirs->export_dirs, path) ||
          array_contains(&dirs->export_dirs, cpath))
    ret = 1;
  
  /* Check if it is forbidden */
  
  if(ret &&
     (array_contains(&dirs->restricted_dirs, path) ||
      array_contains(&dirs->restricted_dirs, cpath)))
    ret = 0;
  
  free(cpath);

  if(!ret)
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Not exporting %s", path);
  
  return ret;
  
  }

bg_media_dirs_t * bg_media_dirs_create()
  {
  bg_media_dirs_t * ret;
  ret = calloc(1, sizeof(*ret));
  pthread_mutex_init(&ret->mutex, NULL);
  return ret;
  }

/* paths are transformed like:
   /media_path/dir/file -> http://example.com/media/2/dir/file
*/
   
void bg_media_dirs_destroy(bg_media_dirs_t * d)
  {
  pthread_mutex_destroy(&d->mutex);
  gavl_array_free(&d->dirs);
  if(d->root_uri)
    free(d->root_uri);
  free(d);
  }

static int find_by_local_path(bg_media_dirs_t * d, const char * path)
  {
  int i;
  const gavl_dictionary_t * dict;
  const char * var;
  
  for(i = 0; i < d->dirs.num_entries; i++)
    {
    if((dict = gavl_value_get_dictionary(&d->dirs.entries[i])) &&
       (var = gavl_dictionary_get_string(dict, LOCAL_PATH)) &&
       gavl_string_starts_with(path, var))
      return i;
    }
  return -1;
  }


static int find_by_http_path(bg_media_dirs_t * d, const char * path)
  {
  int i;
  const gavl_dictionary_t * dict;
  const char * var;
  
  for(i = 0; i < d->dirs.num_entries; i++)
    {
    if((dict = gavl_value_get_dictionary(&d->dirs.entries[i])) &&
       (var = gavl_dictionary_get_string(dict, HTTP_PATH)) &&
       gavl_string_starts_with(path, var))
      return i;
    }
  return -1;
  }

void bg_media_dirs_add_path(bg_media_dirs_t * d, const char * path)
  {
  char md5[33];
  gavl_value_t val;
  gavl_dictionary_t * dict;

  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Exporting media dir %s", path);
  
  gavl_value_init(&val);
  dict = gavl_value_set_dictionary(&val);

  /* We need the trailing / so we can efficiently test using gavl_string_starts_with */
  if(path[strlen(path)-1] == '/')
    gavl_dictionary_set_string(dict, LOCAL_PATH, path);
  else
    gavl_dictionary_set_string_nocopy(dict, LOCAL_PATH, bg_sprintf("%s/", path));

  bg_get_filename_hash(gavl_dictionary_get_string(dict, LOCAL_PATH), md5);
    
  gavl_dictionary_set_string_nocopy(dict, HTTP_PATH, bg_sprintf(PATH_PREFIX"%s/", md5));

  pthread_mutex_lock(&d->mutex);
  gavl_array_splice_val_nocopy(&d->dirs, -1, 0, &val);
  pthread_mutex_unlock(&d->mutex);
  }

int bg_is_http_media_uri(const char * uri)
  {
  char * protocol = NULL;
  char * path = NULL;
  int ret = 0;

  // fprintf(stderr, "bg_is_http_media_uri %s\n", uri);
  
  if(bg_url_split(uri,
                  &protocol,
                  NULL,
                  NULL,
                  NULL,
                  NULL,
                  &path) &&
     gavl_string_starts_with(path, PATH_PREFIX) &&
     protocol &&
     !strcmp(protocol, "http"))
    ret = 1;
  
  if(protocol)
    free(protocol);
  if(path)
    free(path);
  return ret;
  }

void bg_media_dirs_del_path(bg_media_dirs_t * d, const char * path)
  {
  int idx;
  
  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Unexporting media dir %s", path);
  
  pthread_mutex_lock(&d->mutex);

  if((idx = find_by_local_path(d, path)) >= 0)
    gavl_array_splice_val(&d->dirs, idx, 1, NULL);
  
  pthread_mutex_unlock(&d->mutex);
  }

void bg_media_dirs_set_root_uri(bg_media_dirs_t * d, const char * uri)
  {
  d->root_uri = gavl_strrep(d->root_uri, uri);
  }

char * bg_media_dirs_local_to_http(bg_media_dirs_t * d, const char * path)
  {
  int idx = -2;
  const gavl_dictionary_t * dict = NULL;
  const char * str_http = NULL;
  const char * str_local = NULL;
  
  char * ret = NULL;
  
  pthread_mutex_lock(&d->mutex);

  if(do_export(d, path) &&
     ((idx = find_by_local_path(d, path)) >= 0) &&
     (dict = gavl_value_get_dictionary(&d->dirs.entries[idx])) &&
     (str_local = gavl_dictionary_get_string(dict, LOCAL_PATH)) &&
     (str_http = gavl_dictionary_get_string(dict, HTTP_PATH)))
    {
    ret = bg_sprintf("%s%s", str_http, path + strlen(str_local));
    }
  else
    {
    fprintf(stderr, "bg_media_dirs_local_to_http failed: idx: %d, dict: %p, str_local: %s, str_http: %s\n",
            idx, dict, str_local, str_http);
    }
  
  pthread_mutex_unlock(&d->mutex);
  
  return ret;
  }

char * bg_media_dirs_local_to_http_uri(bg_media_dirs_t * d, const char * path)
  {
  int idx = -2;
  const gavl_dictionary_t * dict = NULL;
  const char * str_http;
  const char * str_local = NULL;
  
  char * ret = NULL;

  /* Don't serve multitrack files or edl related stuff over http */
  gavl_dictionary_t vars;

  gavl_dictionary_init(&vars);
  gavl_url_get_vars_c(path, &vars);

  if(gavl_dictionary_get(&vars, BG_URL_VAR_TRACK) ||
     gavl_dictionary_get(&vars, BG_URL_VAR_EDL))
    {
    gavl_dictionary_free(&vars);
    return NULL;
    }

  gavl_dictionary_free(&vars);
  
  pthread_mutex_lock(&d->mutex);

  if(do_export(d, path) &&
     ((idx = find_by_local_path(d, path)) >= 0) &&
     (dict = gavl_value_get_dictionary(&d->dirs.entries[idx])) &&
     (str_local = gavl_dictionary_get_string(dict, LOCAL_PATH)) &&
     (str_http = gavl_dictionary_get_string(dict, HTTP_PATH)))
    {
    char * path_enc = bg_string_to_uri(path + strlen(str_local), -1);
    
    ret = bg_sprintf("%s%s%s", d->root_uri, str_http, path_enc);
    free(path_enc);
    }
  else
    {
#if 0
    fprintf(stderr, "bg_media_dirs_local_to_http_uri failed 1: idx: %d, dict: %p\n",
            idx, dict);
    fprintf(stderr, "bg_media_dirs_local_to_http_uri failed 2: str_local: %s\n",
            str_local);
#endif
    }
  
  pthread_mutex_unlock(&d->mutex);
  
  return ret;
  }

char * bg_media_dirs_http_to_local(bg_media_dirs_t * d, const char *  path)
  {
  int idx;
  const gavl_dictionary_t * dict;
  const char * str_http;
  const char * str_local;
  
  char * ret = NULL;
  
  pthread_mutex_lock(&d->mutex);

  if(((idx = find_by_http_path(d, path)) >= 0) &&
     (dict = gavl_value_get_dictionary(&d->dirs.entries[idx])) &&
     (str_local = gavl_dictionary_get_string(dict, LOCAL_PATH)) &&
     (str_http = gavl_dictionary_get_string(dict, HTTP_PATH)))
    {
    ret = bg_sprintf("%s%s", str_local, path + strlen(str_http));
    }

  if(ret && !do_export(d, ret))
    {
    free(ret);
    ret = NULL;
    }
  
  pthread_mutex_unlock(&d->mutex);
  
  return ret;
  }

const bg_parameter_info_t * bg_media_dirs_get_parameters(void)
  {
  return parameters;
  }

int bg_media_dirs_set_parameter(void * data, const char * name,
                                const gavl_value_t * val)
  {
  int ret = 1;
  bg_media_dirs_t * dirs = data;

  if(!name)
    return 0;

  pthread_mutex_lock(&dirs->mutex);
  
  if(!strcmp(name, "export_media_dirs"))
    {
    dirs->do_export = val->v.i;
    }
  else if(!strcmp(name, "export_dirs"))
    {
    const gavl_array_t * arr;
    gavl_array_reset(&dirs->export_dirs);

    if((arr = gavl_value_get_array(val)))
      gavl_array_copy(&dirs->export_dirs, arr);
    
    }
  else if(!strcmp(name, "restrict_dirs"))
    {
    const gavl_array_t * arr;
    gavl_array_reset(&dirs->restricted_dirs);

    if((arr = gavl_value_get_array(val)))
      gavl_array_copy(&dirs->restricted_dirs, arr);

    
    }
  else
    ret = 0;
  
  pthread_mutex_unlock(&dirs->mutex);

  return ret;
  }
