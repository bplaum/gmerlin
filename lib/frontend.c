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



#include <string.h>


#include <config.h>
#include <gavl/log.h>
#define LOG_DOMAIN "frontend"

#include <gmerlin/frontend.h>

#include <gavl/utils.h>

struct bg_frontend_s
  {
  bg_plugin_handle_t * handle;

  };

static bg_frontend_t * bg_frontend_create(bg_controllable_t * controllable, int type_mask, const char * plugin_name)
  {
  char * real_name = NULL;
  bg_frontend_plugin_t * plugin;

  const bg_plugin_info_t * info;
  bg_frontend_t * ret = calloc(1, sizeof(*ret));

  if(!gavl_string_starts_with(plugin_name, "fe_"))
    {
    real_name = gavl_sprintf("fe_%s", plugin_name);
    plugin_name = real_name;
    }
  
  if(!(info = bg_plugin_find_by_name(plugin_name)))
    goto fail;

  if(!(info->type & type_mask))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Plugin %s has wrong type", bg_plugin_info_get_long_name(info));
    goto fail;
    }
  if(!(ret->handle = bg_plugin_load(info)))
    goto fail;
  
  plugin = (bg_frontend_plugin_t *)ret->handle->plugin;

  if(!plugin->open(ret->handle->priv, controllable))
    goto fail;

  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Loaded frontend %s", bg_plugin_info_get_name(info));

  
  if(real_name)
    free(real_name);
  
  return ret;

  fail:

  if(real_name)
    free(real_name);
  bg_frontend_destroy(ret);
  return NULL;
  }

void bg_frontend_destroy(bg_frontend_t * f)
  {
  if(f->handle)
    bg_plugin_unref(f->handle);
  free(f);
  }

int bg_frontend_ping(bg_frontend_t * f)
  {
  int ret = 0;

  bg_frontend_plugin_t * plugin;
  plugin = (bg_frontend_plugin_t *)f->handle->plugin;

  if(plugin->update)
    ret += plugin->update(f->handle->priv);

  
  return ret;
  }


/* Array of frontends */

int bg_frontends_ping(bg_frontend_t ** f, int num_frontends)
  {
  int i;
  int ret = 0;
  for(i = 0; i < num_frontends; i++)
    ret += bg_frontend_ping(f[i]);
  return ret;
  }

void bg_frontends_destroy(bg_frontend_t ** f, int num_frontends)
  {
  int i;
  for(i = 0; i < num_frontends; i++)
    bg_frontend_destroy(f[i]);
  free(f);
  }

bg_frontend_t ** bg_frontends_create(bg_controllable_t * ctrl,
                                     int type_mask, gavl_array_t * frontends, int * num)
  {
  bg_frontend_t ** ret;
  int idx = 0;
  int i;

  if(!frontends->num_entries)
    return NULL;
  
  ret = calloc(frontends->num_entries, sizeof(*ret));
  
  for(i = 0; i < frontends->num_entries; i++)
    {
    if((ret[idx] = bg_frontend_create(ctrl, type_mask, gavl_string_array_get(frontends, i))))
      idx++;
    }
  *num = idx;
  return ret;
  }

int bg_frontend_set_option(gavl_array_t * frontends, const char * opt)
  {
  int idx = 0;
  int ret = 0;
  char * real_opt = NULL;

  char ** str;

  gavl_array_reset(frontends);

  if(!strcmp(opt, "none"))
    return 1;
  
  if(!(str = gavl_strbreak(opt, ',')))
    goto fail;

  while(str[idx])
    {
    gavl_string_array_add(frontends, str[idx]);
    idx++;
    }
  
  ret = 1;
  
  fail:

  if(str)
    gavl_strbreak_free(str);
  
  if(real_opt)
    free(real_opt);
  return ret;
  }

