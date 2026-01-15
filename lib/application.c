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


#include <gavl/gavl.h>
#include <gavl/value.h>
#include <gavl/metatags.h>
#include <gavl/log.h>
#define LOG_DOMAIN "application"

#include <gmerlin/application.h>
#include <gmerlin/utils.h>

// #define WINDOW_ICON "window_icon"

/* Application support */

gavl_dictionary_t bg_app_vars = { 0 };

void bg_app_init(const char * app_name,
                 const char * app_label,
                 const char * icon_name)
  {
  gavl_dictionary_init(&bg_app_vars);

  gavl_dictionary_set_string(&bg_app_vars, BG_APP_NAME, app_name);
  gavl_dictionary_set_string(&bg_app_vars, BG_APP_LABEL, app_label);
  gavl_dictionary_set_string(&bg_app_vars, BG_APP_ICON_NAME, icon_name);
  }

const char * bg_app_get_name()
  {
  return gavl_dictionary_get_string(&bg_app_vars, BG_APP_NAME);
  }

const char * bg_app_get_label()
  {
  return gavl_dictionary_get_string(&bg_app_vars, BG_APP_LABEL);
  }

const char * bg_app_get_icon_name()
  {
  return gavl_dictionary_get_string(&bg_app_vars, BG_APP_ICON_NAME);
  }

char * bg_app_get_icon_file()
  {
  char * ret;
  char * tmp_string;
  const char * name = bg_app_get_icon_name();

  if(!name)
    return NULL;
  
  /* /web/icons/<name>_48.png */

  tmp_string = gavl_sprintf("%s_48.png", name);
  ret = bg_search_file_read("web/icons", tmp_string);
  free(tmp_string);

  if(!ret)
    {
    /* /icons/<name>_icon.png */
  
    tmp_string = gavl_sprintf("%s_icon.png", name);
    ret = bg_search_file_read("icons", tmp_string);
    free(tmp_string);
    }

  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Got icon file: %s", ret);
  return ret;
  }

#if 0
const char * config_dir_default = "generic";

const char * bg_app_get_config_dir()
  {
  const char * ret;
  if((ret = gavl_dictionary_get_string(&bg_app_vars, BG_APP_CFG_DIR)))
    return ret;
  else
    return config_dir_default;
  }

void bg_app_set_config_dir(const char * p)
  {
  gavl_dictionary_set_string(&bg_app_vars, BG_APP_CFG_DIR, p);
  }
#endif

char * bg_app_get_config_file_name(void)
  {
  const char * app;
  char * ret;

  if(!(app = bg_app_get_name()))
    app = "generic";
  
  if(!(ret = gavl_search_config_dir(PACKAGE, app, NULL)))
    return NULL;

  return gavl_strcat(ret, "/config.xml");
  }

static void add_application_icon(gavl_array_t * arr,
                                 char * uri,
                                 int size,
                                 const char * mimetype)
  {
  gavl_dictionary_t * dict;
  
  gavl_value_t val;
  gavl_value_init(&val);

  dict = gavl_value_set_dictionary(&val);
  gavl_dictionary_set_string_nocopy(dict, GAVL_META_URI, uri);
  gavl_dictionary_set_string(dict, GAVL_META_MIMETYPE, mimetype);

  gavl_dictionary_set_int(dict, GAVL_META_WIDTH,  size);
  gavl_dictionary_set_int(dict, GAVL_META_HEIGHT, size);
  gavl_array_splice_val_nocopy(arr, -1, 0, &val);
  }


void bg_array_add_application_icons(gavl_array_t * arr, const char * prefix, const char * name)
  {
  char * slash;
  
  if(!gavl_string_ends_with(prefix, "/"))
    slash = "/";
  else
    slash = "";
  
  add_application_icon(arr, gavl_sprintf("%s%s%s_48.png", prefix, slash, name), 48, "image/png");
  add_application_icon(arr, gavl_sprintf("%s%s%s_48.jpg", prefix, slash, name), 48, "image/jpeg");
  add_application_icon(arr, gavl_sprintf("%s%s%s_96.png", prefix, slash, name), 96, "image/png");
  add_application_icon(arr, gavl_sprintf("%s%s%s_96.jpg", prefix, slash, name), 96, "image/jpeg");
  }


void bg_dictionary_add_application_icons(gavl_dictionary_t * m,
                                         const char * prefix,
                                         const char * name)
  {
  gavl_value_t val;
  gavl_array_t * arr;
  
  gavl_value_init(&val);

  arr = gavl_value_set_array(&val);

  bg_array_add_application_icons(arr, prefix, name);
  
  gavl_dictionary_set_nocopy(m, GAVL_META_ICON_URL, &val);
  }

