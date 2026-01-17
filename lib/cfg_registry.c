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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <gmerlin/cfg_registry.h>
#include <registry_priv.h>
#include <gmerlin/utils.h>
#include <gmerlin/application.h>

#include <gmerlin/cmdline.h>

bg_cfg_registry_t * bg_cfg_registry = NULL;
#define SAVE_CONFIG_KEY "$SAVE_CONFIG"

void bg_cfg_registry_init(void)
  {
  char * tmp_path;

  bg_cfg_registry = gavl_dictionary_create();
  
  if((tmp_path = bg_app_get_config_file_name()) &&
     !access(tmp_path, R_OK))
    {
    bg_cfg_registry_load(bg_cfg_registry, tmp_path);
    free(tmp_path);
    }
  }

void bg_cfg_registry_cleanup()
  {
  if(bg_cfg_registry)
    {
    gavl_dictionary_destroy(bg_cfg_registry);
    bg_cfg_registry = NULL;
    }
  }


/*
 *  Path looks like "section:subsection:subsubsection"
 */

// static void 

gavl_dictionary_t * bg_cfg_registry_find_section(bg_cfg_registry_t * r,
                                                const char * path)
  {
  int i;
  char ** tmp_sections;
  
  tmp_sections = gavl_strbreak(path, ':');
  
  i = 0;
  while(tmp_sections[i])
    {
    r = bg_cfg_section_find_subsection(r, tmp_sections[i]);
    i++;
    }
  
  gavl_strbreak_free(tmp_sections);
  
  return r;
  }

void bg_cfg_registry_opt_c(void * data, int * argc, char *** _argv, int arg)
  {
  if(arg >= *argc)
    {
    fprintf(stderr, "Option -c requires an argument\n");
    exit(-1);
    }

  if(!bg_cfg_registry)
    {
    bg_cfg_registry = gavl_dictionary_create();
    bg_cfg_registry_load(bg_cfg_registry, (*_argv)[arg]);
    }
  else
    {
    gavl_dictionary_t file_reg;
    gavl_dictionary_t tmp;

    const gavl_dictionary_t * src1;
    const gavl_dictionary_t * src2;
    gavl_dictionary_t * dst;
    
    gavl_dictionary_init(&file_reg);
    gavl_dictionary_init(&tmp);
    bg_cfg_registry_load(&file_reg, (*_argv)[arg]);
    
    src1 = gavl_dictionary_get_dictionary_create(&file_reg, BG_CFG_TAG_CHILDREN);
    src2 = gavl_dictionary_get_dictionary_create(bg_cfg_registry, BG_CFG_TAG_CHILDREN);

    gavl_dictionary_copy(&tmp, bg_cfg_registry);
    gavl_dictionary_set(&tmp, BG_CFG_TAG_CHILDREN, NULL);
    
    dst = gavl_dictionary_get_dictionary_create(&tmp, BG_CFG_TAG_CHILDREN);
    
    gavl_dictionary_merge(dst, src1, src2);
    
    gavl_dictionary_free(bg_cfg_registry);
    gavl_dictionary_move(bg_cfg_registry, &tmp);
    
    gavl_dictionary_free(&file_reg);
    }

  //  fprintf(stderr, "Loaded registry:\n");
  //  gavl_dictionary_dump(bg_cfg_registry, 2);
  
  bg_cmdline_remove_arg(argc, _argv, arg);
  }

void bg_cfg_registry_opt_sc(void * data, int * argc, char *** _argv, int arg)
  {
  if(arg >= *argc)
    {
    fprintf(stderr, "Option -sc requires an argument\n");
    exit(-1);
    }
  
  gavl_dictionary_set_string(bg_cfg_registry, SAVE_CONFIG_KEY, (*_argv)[arg]);
  bg_cmdline_remove_arg(argc, _argv, arg);
  }

void bg_cfg_registry_save_config()
  {
  char * path;
  gavl_dictionary_t tmp;

  if(!(path = gavl_strdup(gavl_dictionary_get_string(bg_cfg_registry, SAVE_CONFIG_KEY))))
    return;
  
  gavl_dictionary_set(bg_cfg_registry, SAVE_CONFIG_KEY, NULL);
  
  gavl_dictionary_init(&tmp);
  gavl_dictionary_copy(&tmp, bg_cfg_registry);
  bg_cfg_section_delete_subsection_by_name(&tmp, "plugins");
  
  bg_cfg_registry_save_to(&tmp, path);
  free(path);
  gavl_dictionary_free(&tmp);
  }
  

