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



#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/types.h>


#include <limits.h>

#include <config.h>
#include <gmerlin/utils.h>
#include <gmerlin/subprocess.h>

#include <gmerlin/log.h>
#define LOG_DOMAIN "utils"

char * bg_search_var_dir(const char * directory)
  {
  char * home_dir;
  char * testdir;

  if(!(home_dir = getenv("HOME")))
    return NULL;
  
  testdir  = gavl_sprintf("%s/.%s/%s", home_dir, PACKAGE, directory);
  if(!gavl_ensure_directory(testdir, 1))
    {
    free(testdir);
    return NULL;
    }
  return testdir;
  }



char * bg_search_file_read(const char * directory, const char * file)
  {
  char * testpath;
  char * test_file_name;
  FILE * testfile;

  if(!file)
    return NULL;
  
  /* First step: Try home directory */
  
  if((testpath = bg_search_var_dir(directory)))
    {
    test_file_name = gavl_sprintf("%s/%s", testpath, file);
    testfile = fopen(test_file_name, "r");
    if(testfile)
      {
      free(testpath);
      fclose(testfile);
      return test_file_name;
      }
    free(test_file_name);
    free(testpath);
    }
  
  /* Second step: Try Data directory */
  test_file_name = gavl_sprintf("%s/%s/%s", DATA_DIR, directory, file);

  testfile = fopen(test_file_name, "r");
  if(testfile)
    {
    fclose(testfile);
    return test_file_name;
    }
  free(test_file_name);
  return NULL;
  }

static char * search_file_write(const char * directory, const char * file, int do_create)
  {
  char * testpath;
  char * testdir;
  
  FILE * testfile;

  //  if(!file)
  //    return NULL;
  
  /* Try to open the file */

  testdir = bg_search_var_dir(directory);
  
  //   gavl_sprintf("%s/.%s/%s", home_dir, PACKAGE, directory);

  if(!do_create && access(testdir, R_OK|W_OK|X_OK))
    {
    free(testdir);
    return NULL;
    }
  
  if(!gavl_ensure_directory(testdir, 1))
    {
    free(testdir);
    return NULL;
    }
  
  if(!file)
    {
    /* Make sure it's writable */
    if(!access(testdir, R_OK|W_OK|X_OK))
      return testdir;
    else
      return NULL;
    }
  
  testpath = gavl_sprintf("%s/%s", testdir, file);
  
  testfile = fopen(testpath, "a");
  if(testfile)
    {
    fclose(testfile);
    free(testdir);
    return testpath;
    }
  else
    {
    free(testpath);
    free(testdir);
    return NULL;
    }
  }

char * bg_search_file_write_nocreate(const char * directory, const char * file)
  {
  return search_file_write(directory, file, 0);
  }

char * bg_search_file_write(const char * directory, const char * file)
  {
  return search_file_write(directory, file, 1);
  }

// S_IRUSR|S_IWUSR|S_IXUSR
// S_IRUSR|S_IWUSR|S_IXUSR|


int bg_search_file_exec(const char * file, char ** _path)
  {
  int i;
  char * path;
  char ** searchpaths;
  char * test_filename;
  
  struct stat st;

  /* Try the dependencies path first */
  test_filename = gavl_sprintf("/opt/gmerlin/bin/%s", file);
  if(!stat(test_filename, &st) && (st.st_mode & S_IXOTH))
    {
    if(_path)
      *_path = test_filename;
    else
      free(test_filename);
    return 1;
    }
  free(test_filename);
  
  path = getenv("PATH");
  if(!path)
    return 0;

  searchpaths = gavl_strbreak(path, ':');
  i = 0;
  while(searchpaths[i])
    {
    test_filename = gavl_sprintf("%s/%s", searchpaths[i], file);
    if(!stat(test_filename, &st) && (st.st_mode & S_IXOTH))
      {
      if(_path)
        *_path = test_filename;
      else
        free(test_filename);
      gavl_strbreak_free(searchpaths);
      return 1;
      }
    free(test_filename);
    i++;
    }
  gavl_strbreak_free(searchpaths);
  return 0;
  }

static const struct
  {
  char * command;
  char * template;
  }
webbrowsers[] =
  {
    { "firefox", "firefox %s" },
    { "mozilla", "mozilla %s" },
  };

char * bg_search_desktop_file(const char * name)
  {
  const char * home_dir;
  char * file = NULL;

  if((home_dir = getenv("HOME")))
    {
    file = gavl_sprintf("%s/.local/share/applications/%s.desktop", home_dir, name);

    if(access(file, R_OK))
      {
      free(file);
      file = NULL;
      }
    }

  if(file)
    return file;

  file = gavl_sprintf("/usr/local/share/applications/%s.desktop", name);

  if(access(file, R_OK))
    {
    free(file);
    file = NULL;
    }

  if(file)
    return file;

  file = gavl_sprintf("/usr/share/applications/%s.desktop", name);

  if(access(file, R_OK))
    {
    free(file);
    file = NULL;
    }

  return file;
  }

#if 0
int bg_search_icons(const char * file, gavl_dictionary_t * ret, const char * string)
  {

  if(home)
    {
    char * dir;
    
    }
  
  if(xdg_dirs_var)
    xdg_dirs = gavl_strbreak(xdg_dirs_var, ':');

  
  }

#endif

static char * search_application_icon_internal(const char * dir, int size, const char * file)
  {
  char * ret = gavl_sprintf("%s/hicolor/%dx%d/apps/%s.png", dir, size, size, file);

  if(access(ret, R_OK))
    {
    free(ret);
    return NULL;
    }
  return ret;
  }

char * bg_search_application_icon(const char * file, int size)
  {
  char * tmp_string;
  char * ret = NULL;
  
  char * xdg_dirs_var = getenv("XDG_DATA_DIRS");
  char * home = getenv("HOME");
  
  if(size <= 0)
    size = 48;
  
  if(home)
    {
    tmp_string = gavl_sprintf("%s/.icons", home);
    ret = search_application_icon_internal(tmp_string, size, file);
    free(tmp_string);
    
    if(ret)
      return ret;
    }

  if(xdg_dirs_var)
    {
    int i = 0;

    char ** xdg_dirs = NULL;

    if((xdg_dirs = gavl_strbreak(xdg_dirs_var, ':')))
      {
      while(xdg_dirs[i])
        {
        if((ret = search_application_icon_internal(xdg_dirs[i], size, file)))
          {
          gavl_strbreak_free(xdg_dirs);
          return ret;
          }
        i++;
        }
      gavl_strbreak_free(xdg_dirs);
      }
    }

  /* Fallback */
  
  if((ret = search_application_icon_internal("/usr/local/share/icons", size, file)))
    return ret;

  if((ret = search_application_icon_internal("/usr/share/icons", size, file)))
    return ret;

  return NULL;
  }

char * bg_find_url_launcher()
  {
  int i;
  char * ret = NULL;
  int ret_alloc = 0;
  bg_subprocess_t * proc;
  /* Try to get the default url handler from gnome */
  
  if(bg_search_file_exec("gconftool-2", NULL))
    {
    proc =
      bg_subprocess_create("gconftool-2 -g /desktop/gnome/url-handlers/http/command",
                           0, 1, 0);
    
    if(bg_subprocess_read_line(proc->stdout_fd, &ret, &ret_alloc, -1))
      {
      bg_subprocess_close(proc);
      return ret;
      }
    else if(ret)
      free(ret);
    bg_subprocess_close(proc);
    }
  for(i = 0; i < sizeof(webbrowsers)/sizeof(webbrowsers[0]); i++)
    {
    if(bg_search_file_exec(webbrowsers[i].command, NULL))
      {
      return gavl_strdup(webbrowsers[i].template);
      }
    }
  return NULL;
  }

void bg_display_html_help(const char * path)
  {
  char * url_launcher;
  char * complete_path;
  char * command;
  url_launcher = bg_find_url_launcher();
  if(!url_launcher)
    return;
  
  complete_path = gavl_sprintf("file://%s/%s", DOC_DIR, path);
  command = gavl_sprintf(url_launcher, complete_path);
  command = gavl_strcat(command, " &");
  bg_system(command);
  free(command);
  free(url_launcher);
  free(complete_path);
  }

const char * bg_tempdir()
  {
  char * ret = getenv("TMPDIR");

  if(!ret)
    ret = getenv("TEMP");

  if(!ret)
    ret = getenv("TMP");

  if(ret)
    return ret;

  if(!access("/tmp", R_OK|W_OK))
    return "/tmp";
  return ".";
  }
