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

#include <gmerlin/cfg_registry.h>
#include <gmerlin/preset.h>
#include <gmerlin/xmlutils.h>

bg_cfg_section_t * bg_preset_load(bg_preset_t * p)
  {
  xmlNodePtr node;
  xmlDocPtr xml_doc;
  bg_cfg_section_t * ret;
  
  xml_doc = bg_xml_parse_file(p->file, 1);

  if(!xml_doc)
    return NULL;
  
  node = xml_doc->children;
  
  if(BG_XML_STRCMP(node->name, "PRESET"))
    {
    xmlFreeDoc(xml_doc);
    return NULL;
    }
  
  ret = bg_cfg_section_create(NULL);
  bg_cfg_xml_2_section(xml_doc, node, ret);
  xmlFreeDoc(xml_doc);
  return ret;
  }

void bg_preset_save(bg_preset_t * p, const bg_cfg_section_t * s)
  {
  xmlDocPtr  xml_doc;
  xmlNodePtr node;
  xml_doc = xmlNewDoc((xmlChar*)"1.0");
  node = xmlNewDocRawNode(xml_doc, NULL, (xmlChar*)"PRESET", NULL);
  
  xmlDocSetRootElement(xml_doc, node);

  bg_cfg_section_2_xml(s, node);

  bg_xml_save_file(xml_doc, p->file, 1);
  xmlFreeDoc(xml_doc);
  
  }

