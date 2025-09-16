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
#include <string.h>
// #include <locale.h>

#include <gmerlin/cfg_registry.h>
#include <registry_priv.h>
#include <gmerlin/utils.h>
#include <gmerlin/streaminfo.h>
#include <gmerlin/bggavl.h>
#include <gmerlin/application.h>

#include <gmerlin/xmlutils.h>
void bg_cfg_xml_2_section(xmlDocPtr xml_doc,
                          xmlNodePtr xml_section,
                          gavl_dictionary_t * cfg_section)
  {
  bg_xml_2_dictionary(xml_section, cfg_section);
  }


void bg_cfg_section_2_xml(const gavl_dictionary_t * section, xmlNodePtr xml_section)
  {
  /* Save items */
  bg_dictionary_2_xml(xml_section, section, 0);
  }

int bg_cfg_registry_load(bg_cfg_registry_t * r, const char * filename)
  {
  return bg_dictionary_load_xml(r, filename, "REGISTRY");
  }

void bg_cfg_registry_save_to(const bg_cfg_registry_t * r, const char * filename)
  {
  bg_dictionary_save_xml(r, filename, "REGISTRY");
  }

void bg_cfg_registry_save()
  {
  char * tmp_string;
  const char * dir = bg_app_get_config_dir();

  if((tmp_string = bg_search_file_write(dir, "cfg.xml")))
    {
    bg_cfg_registry_save_to(bg_cfg_registry, tmp_string);
    free(tmp_string);
    }
  
  }

void bg_cfg_section_dump(gavl_dictionary_t * section, const char * filename)
  {
  bg_dictionary_save_xml(section, filename, "SECTION");
  }

