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

#include <gmerlin/pluginregistry.h>
#include <gmerlin/utils.h>
#include <gmerlin/cfgctx.h>
#include <gmerlin/cmdline.h>
#include <gmerlin/translation.h>

bg_plugin_registry_t * plugin_reg = NULL;

const bg_cmdline_app_data_t app_data =
  {
    .package =  PACKAGE,
    .version =  VERSION,
    .name =     "gmerlin-savemeta",
    .long_name = TRS("Save metadata to an xml file"),
    .synopsis = TRS("[options] file ...\n"),
    .help_before = TRS("Metadata saver\n"),
    .args = (bg_cmdline_arg_array_t[]) {
    // { TRS("Options"), global_options },
                                       {  } },
    .files = (bg_cmdline_ext_doc_t[])
    { { "~/.gmerlin/plugins.xml",
        TRS("Cache of the plugin registry (shared by all applications)") },
      { "~/.gmerlin/generic/cfg.xml",
        TRS("Default plugin parameters are read from there. Use gmerlin_plugincfg to change them.") },
      { /* End */ }
    },
    
  };

int main(int argc, char ** argv)
  {
  bg_cfg_registry_t * cfg_reg;
  bg_cfg_section_t * cfg_section;
  char * tmp_path;
  char ** files;
  int i;
  bg_plugin_handle_t * h = NULL;
  
  /* Create registries */
  
  cfg_reg = bg_cfg_registry_create();
  tmp_path =  bg_search_file_read("generic", "cfg.xml");
  bg_cfg_registry_load(cfg_reg, tmp_path);
  if(tmp_path)
    free(tmp_path);

  cfg_section = bg_cfg_registry_find_section(cfg_reg, "plugins");
  plugin_reg = bg_plugin_registry_create(cfg_section);

  /* Handle commandline options */

  bg_cmdline_init(&app_data);
  //  bg_cmdline_parse(global_options, &argc, &argv, NULL);
  
  files = bg_cmdline_get_locations_from_args(&argc, &argv);
  if(!files)
    return -1;
  i = 0;
  
  while(files[i])
    {
    if(!bg_input_plugin_load(plugin_reg, files[i], &h, NULL))
      {
      i++;
      continue;
      }

    /* Save metadata */
    bg_plugin_save_metadata(h);
    bg_plugin_unref(h);
    
    i++;
    }
  
  return 0;
  }
