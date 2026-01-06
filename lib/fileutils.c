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



#include <config.h>
#include <stdio.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <time.h>
#include <glob.h>
#include <gmerlin/pluginregistry.h>

#include <gmerlin/utils.h>
#include <gmerlin/log.h>
#define LOG_DOMAIN "fileutils"

#include <gmerlin/http.h>
#include <gmerlin/bggavl.h>

#include <unistd.h>
#include <fcntl.h>

static int lock_file(FILE * f, int wr, int wait)
  {
  struct flock fl;

  fl.l_type   = wr ? F_WRLCK : F_RDLCK;  /* F_RDLCK, F_WRLCK, F_UNLCK    */
  fl.l_whence = SEEK_SET; /* SEEK_SET, SEEK_CUR, SEEK_END */
  fl.l_start  = 0;        /* Offset from l_whence         */
  fl.l_len    = 0;        /* length, 0 = to EOF           */
  fl.l_pid    = getpid(); /* our PID                      */

  if(wait)
    {
    if(fcntl(fileno(f), F_SETLKW, &fl))
      return 0;
    }
  else
    {
    if(fcntl(fileno(f), F_SETLK, &fl))
      return 0;
    }
  return 1;
  }

int bg_lock_file(FILE * f, int wr)
  {
  return lock_file(f, wr, 1);
  }

int bg_lock_file_nowait(FILE * f, int wr)
  {
  return lock_file(f, wr, 0);
  }

int bg_unlock_file(FILE * f)
  {
  struct flock fl;

  fl.l_type   = F_UNLCK;  /* F_RDLCK, F_WRLCK, F_UNLCK    */
  fl.l_whence = SEEK_SET; /* SEEK_SET, SEEK_CUR, SEEK_END */
  fl.l_start  = 0;        /* Offset from l_whence         */
  fl.l_len    = 0;        /* length, 0 = to EOF           */
  fl.l_pid    = getpid(); /* our PID                      */

  if(fcntl(fileno(f), F_SETLK, &fl))
    return 0;
  
  return 1;
  }

FILE * bg_lock_directory(const char * directory)
  {
  char * tmp_string;
  FILE * ret;

  tmp_string = gavl_sprintf("%s/lock", directory);

  if(!(ret = fopen(tmp_string, "w")))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Couldn't open lock file %s: %s", tmp_string, strerror(errno));
    return NULL;
    }
  
  if(!bg_lock_file_nowait(ret, 1))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Couldn't get write lock for %s: %s", tmp_string, strerror(errno));
    fclose(ret);
    ret = NULL;
    }
  free(tmp_string);
  return ret;
  }

void bg_unlock_directory(FILE * lockfile, const char * directory)
  {
  char * tmp_string;

  if(!lockfile)
    return;

  bg_unlock_file(lockfile);
  tmp_string = gavl_sprintf("%s/lock", directory);
  remove(tmp_string);
  free(tmp_string);
  }

size_t bg_file_size(FILE * f)
  {
  size_t ret;
  size_t oldpos;

  oldpos = ftell(f);

  fseek(f, 0, SEEK_END);
  ret = ftell(f);
  fseek(f, oldpos, SEEK_SET);
  return ret;
  }

int bg_read_file_range(const char * filename, gavl_buffer_t * buf, int64_t start, int64_t len)
  {
  FILE * file;
  
  file = fopen(filename, "r");
  if(!file)
    return 0;

  if(len < 1)
    len = bg_file_size(file) - start;

  gavl_buffer_alloc(buf, len + 1);
  
  if(start > 0)
    fseek(file, start, SEEK_SET);
  
  if(fread(buf->buf, 1, len, file) < len)
    {
    fclose(file);
    gavl_buffer_free(buf);
    return 0;
    }
  buf->len = len;
  buf->buf[buf->len] = '\0';
  fclose(file);
  return 1;
  }


int bg_read_file(const char * filename, gavl_buffer_t * buf)
  {
  return bg_read_file_range(filename, buf, 0, 0);
  }

int bg_write_file(const char * filename, void * data, int len)
  {
  FILE * file;
  
  file = fopen(filename, "w");
  if(!file)
    return 0;
  
  if(fwrite(data, 1, len, file) < len)
    {
    fclose(file);
    return 0;
    }
  fclose(file);
  return 1;
  }

#if 1
int bg_read_location(const char * location_orig,
                     gavl_buffer_t * ret,
                     int64_t start, int64_t size,
                     gavl_dictionary_t * dict)
  {
  int result = 0;
  
  const char * var;
  gavl_dictionary_t vars;
  char * location;

  if(gavl_string_starts_with(location_orig, "appicon:"))
    location = bg_search_application_icon(location_orig + 8, 48);
  else if(gavl_string_starts_with(location_orig, BG_EMBEDDED_COVER_SCHEME"://"))
    {
    return bg_plugin_registry_extract_embedded_cover(location_orig + strlen(BG_EMBEDDED_COVER_SCHEME"://"),
                                                     ret, dict);
    }
  else
    location = gavl_strdup(location_orig);
  
  // fprintf(stderr, "bg_read_location %s\n", location_orig);
  
  gavl_dictionary_init(&vars);
  gavl_url_get_vars(location, &vars);

  if((var = gavl_dictionary_get_string(&vars, "byterange")))
    {
    const char * pos = strchr(var, '-');
    start = strtoll(var, NULL, 10);

    if(pos)
      {
      pos++;
      size = strtoll(pos, NULL, 10);
      size -= start;
      }
    gavl_dictionary_set(&vars, "byterange", NULL);
    }

  location = bg_url_append_vars(location, &vars);
  
  if(!strncasecmp(location, "http://", 7) || !strncasecmp(location, "https://", 8))
    result = bg_http_get_range(location, ret, dict, start, size);
  else
    {
    const char * pos;
    result =  bg_read_file_range(location, ret, start, size);
    
    if(dict && (pos = strrchr(location, '.')))
      {
      pos++;
      gavl_dictionary_set_string(dict, GAVL_META_MIMETYPE, bg_ext_to_mimetype(pos));
      }
    
    }
  if(location)
    free(location);
  
  return result;

  }
#endif

/* Remove a regular file, directories are removed recursively */

int bg_remove_file(const char * file)
  {
  struct stat st;

  if(lstat(file, &st))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Couldn't stat file %s: %s", file, strerror(errno));
    return 0;
    }

  if(S_ISDIR(st.st_mode))
    {
    /* Delete subdirectories */
    struct dirent * dent_ptr;
    DIR * d;

    char * filename;
    d = opendir(file);
    
    while((dent_ptr = readdir(d)))
      {
      if(!dent_ptr)
        break;

      if(!strcmp(dent_ptr->d_name, "..") ||
         !strcmp(dent_ptr->d_name, "."))
        continue;
      
      filename = gavl_sprintf("%s/%s", file, dent_ptr->d_name);

      if(!bg_remove_file(filename))
        return 0;
      }
    closedir(d);
    rmdir(file);
    }
  else if(unlink(file))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Couldn't unlink %s: %s", file, strerror(errno));
    return 0;
    }
  
  return 1;
  }

/* Read .desktop file into a dictionary */

int bg_read_desktop_file(const char * file, gavl_dictionary_t * ret)
  {
  int i;
  char ** lines;
  char * pos;
  char * end;
  gavl_buffer_t buf;
  gavl_dictionary_t * s = NULL;
  
  gavl_buffer_init(&buf);

  if(!bg_read_file(file, &buf))
    return 0;
  
  lines = gavl_strbreak((const char*)buf.buf, '\n');

  i = 0;

  while(lines[i])
    {
    char * tmp_string = gavl_strtrim(lines[i]);

    if((*tmp_string == '#') || (*tmp_string == '\0'))
      {
      i++;
      continue;
      }

    /* Section */
    if(*tmp_string == '[')
      {
      pos = tmp_string + 1;

      if((end = strchr(pos, ']')))
        {
        char * section;
        section = gavl_strtrim(gavl_strndup(pos, end));
        s = gavl_dictionary_get_dictionary_create(ret, section);
        free(section);
        }
      
      }
    else if((end = strchr(tmp_string, '=')))
      {
      if(s)
        {
        char * var;
        char * val;
        var = gavl_strtrim(gavl_strndup(tmp_string, end));
        val = gavl_strtrim(gavl_strdup(end+1));
        gavl_dictionary_set_string(s, var, val);
        free(var);
        free(val);
        }
      }
    else
      {
      /* Unknown line */
      }

    i++;
    
    continue;
    }
  
  gavl_strbreak_free(lines);
  gavl_buffer_free(&buf);
  return 1;
  }

/* Cache access. Also handles cleaning the cache directory */

#define MAX_CACHE_AGE (3600*24)


#define LASTCLEANUP_NAME "lastcleanup"

#define CLEANUP_INTERVAL (3600*24)

/* Call this regularly */

int bg_cache_directory_cleanup(const char * cache_dir)
  {
  glob_t g;
  int i;
  int ret = 0;
  struct stat st;
  FILE * f = NULL;
  char * tmp_string;
  int locked = 0;
  time_t cur;
  
  tmp_string = gavl_sprintf("%s/" LASTCLEANUP_NAME, cache_dir);
  
  if(!stat(tmp_string, &st))
    {
    double diff;
    /* If lastcleanup file exists and the MTIME is less than CLEANUP_INTERVAL ago, return */
    
    if((diff = difftime(time(NULL), st.st_mtime)) < CLEANUP_INTERVAL)
      goto fail;
    }

  f = fopen(tmp_string, "w");
  free(tmp_string);
  tmp_string = NULL;
  
  if(!bg_lock_file_nowait(f, 1))
    goto fail;

  locked = 1;

  tmp_string = gavl_sprintf("%s/*.*", cache_dir);
  glob(tmp_string, 0, NULL /* errfunc */, &g);

  free(tmp_string);
  tmp_string = NULL;

  cur = time(NULL);
  
  for(i = 0; i < g.gl_pathc; i++)
    {
    if(stat(g.gl_pathv[i], &st))
      continue;

    if(difftime(cur, st.st_mtime) > MAX_CACHE_AGE)
      {
      gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Removing expired cache item: %s", g.gl_pathv[i]);
      remove(g.gl_pathv[i]);
      }
    
    }

  ret = 1;
  
  fail:

  if(f)
    {
    if(locked)
      bg_unlock_file(f);
    
    fclose(f);
    }
  
  if(tmp_string)
    free(tmp_string);
  
  return ret;
  
  
  }

gavl_io_t * bg_cache_item_load_metadata(const char * cache_dir,
                                        const char * md5,
                                        gavl_dictionary_t * metadata)
  {
  int done = 0;
  
  gavl_buffer_t buf;
  
  gavl_io_t * io = NULL;
  char * filename = gavl_sprintf("%s/%s", cache_dir, md5);
  
  if(access(filename, R_OK))
    goto fail;

  if(!(io = gavl_io_from_filename(filename, 0)))
    goto fail;
  
  if(!gavl_io_read_buffer(io, &buf))
    goto fail;
  
  if(!bg_dictionary_load_xml_string(metadata, (const char*)buf.buf, -1, GAVL_META_METADATA))
    goto fail;
  
  done = 1;

  fail:

  if(!done)
    {
    gavl_io_destroy(io);
    io = NULL;
    }

  free(filename);
  
  return io;
  }

int bg_cache_item_load_data(gavl_io_t * io, gavl_buffer_t * buf)
  {
  int ret;
  ret = gavl_io_read_buffer(io, buf);
  gavl_io_destroy(io);
  return ret;
  }

#if 0
int bg_load_cache_item(const char * cache_dir,
                       const char * md5,
                       gavl_dictionary_t * metadata,
                       gavl_buffer_t * buf)
  {
  int ret = 0;
  glob_t g;
  int i;
  char * template;
  struct stat st;
  
  
  template = gavl_sprintf("%s/%s.*", cache_dir, md5);

  glob(template, 0, NULL /* errfunc */, &g);

  for(i = 0; i < g.gl_pathc; i++)
    {
    if(stat(g.gl_pathv[i], &st))
      continue;

    if(difftime(time(NULL), st.st_mtime) > MAX_CACHE_AGE)
      break;
    
    if(!bg_read_file(g.gl_pathv[i], buf))
      continue;

    if(metadata)
      {
      
      } 
    
    ret = 1;
    break;
    }
  
  globfree(&g);
  free(template);
  
  return ret;

  }
#endif

void bg_save_cache_item(const char * cache_dir,
                        const char * md5,
                        const gavl_dictionary_t * metadata,
                        const gavl_buffer_t * buf)
  {
  char * xml;
  char * filename;
  gavl_io_t * io;
  gavl_buffer_t header;
  
  filename = gavl_sprintf("%s/%s", cache_dir, md5);
  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Saving cache item: %s", filename);
  
  xml = bg_dictionary_save_xml_string(metadata, "metadata");
  
  io = gavl_io_from_filename(filename, 1);

  gavl_buffer_init_static(&header, (uint8_t*)xml, strlen(xml)+1);
  gavl_io_write_buffer(io, &header);
  gavl_buffer_free(&header);
  
  gavl_io_write_buffer(io, buf);

  /* Cleanup */
  free(filename);
  free(xml);
  gavl_io_destroy(io);
  
  }

static int compare_func(const void * p1, const void * p2, void * data)
  {
  const char * s1;
  const char * s2;
  
  const gavl_dictionary_t * dict1;
  const gavl_dictionary_t * dict2;

  if(!(dict1 = gavl_value_get_dictionary(p1)) ||
     !(dict2 = gavl_value_get_dictionary(p2)) ||
     !(s1 = gavl_dictionary_get_string(dict1, GAVL_META_URI)) ||
     !(s2 = gavl_dictionary_get_string(dict2, GAVL_META_URI)))
    return 0;
  
  return strcoll(s1, s2);
  }

void bg_sort_directory(gavl_array_t * ret)
  {
  gavl_array_sort(ret, compare_func, NULL);
  }
  

int bg_scan_directory(const char * path,
                      gavl_array_t * ret)
  {
  int i;
  glob_t g;
  char * pattern;

  fprintf(stderr, "load_directory_contents %s\n", path);
    
  pattern = gavl_sprintf("%s/*", path);
  pattern = gavl_escape_string(pattern, "[]?");
  
  if(glob(pattern, GLOB_NOSORT, NULL /* errfunc */, &g))
    return 0;
  
  for(i = 0; i < g.gl_pathc; i++)
    {
    gavl_value_t val;
    gavl_dictionary_t * dict;
    struct stat st;

    if(stat(g.gl_pathv[i], &st))
      continue;
    
    gavl_value_init(&val);
    dict = gavl_value_set_dictionary(&val);

    if(S_ISDIR(st.st_mode))
      gavl_dictionary_set_string(dict, GAVL_META_CLASS, GAVL_META_CLASS_DIRECTORY);
    else
      gavl_dictionary_set_string(dict, GAVL_META_CLASS, GAVL_META_CLASS_LOCATION);
    
    gavl_dictionary_set_long(dict, GAVL_META_MTIME, st.st_mtime);
    gavl_dictionary_set_string(dict, GAVL_META_URI, g.gl_pathv[i]);
    gavl_array_splice_val_nocopy(ret, -1, 0, &val);
    }
  
  globfree(&g);
    
  
  return 1;
  }
