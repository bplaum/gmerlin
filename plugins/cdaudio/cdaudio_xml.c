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

#include <string.h>
#include <gmerlin/utils.h>
#include <gmerlin/xmlutils.h>
#include <gmerlin/bggavl.h>

#include "cdaudio.h"

int bg_cdaudio_load(gavl_dictionary_t * mi, const char * filename)
  {
  int index;
  xmlDocPtr xml_doc;
  xmlNodePtr node;
  
  gavl_dictionary_t * m;
  
  xml_doc = xmlParseFile(filename);
  if(!xml_doc)
    return 0;

  node = xml_doc->children;

  if(BG_XML_STRCMP(node->name, "CD"))
    {
    xmlFreeDoc(xml_doc);
    return 0;
    }

  node = node->children;

  index = 0;

  while(node)
    {
    if(!node->name)
      {
      node = node->next;
      continue;
      }
    
    if(!BG_XML_STRCMP(node->name, "METADATA"))
      bg_xml_2_dictionary(node, gavl_track_get_metadata_nc(mi));
    else if(!BG_XML_STRCMP(node->name, "TRACK"))
      {
      m = gavl_get_track_nc(mi, index);
      bg_xml_2_dictionary(node, gavl_track_get_metadata_nc(m));
      index++;
      }
    node = node->next;
    }
  return 1;
  }

void bg_cdaudio_save(gavl_dictionary_t * mi,
                     const char * filename)
  {
  xmlDocPtr  xml_doc;
  int i;

  xmlNodePtr xml_cd, child;

  int num_tracks = gavl_get_num_tracks(mi);
  
  xml_doc = xmlNewDoc((xmlChar*)"1.0");
  xml_cd = xmlNewDocRawNode(xml_doc, NULL, (xmlChar*)"CD", NULL);
  xmlDocSetRootElement(xml_doc, xml_cd);
  xmlAddChild(xml_cd, BG_XML_NEW_TEXT("\n"));

  child = xmlNewTextChild(xml_cd, NULL, (xmlChar*)"METADATA", NULL);
  xmlAddChild(child, BG_XML_NEW_TEXT("\n"));
  bg_dictionary_2_xml(child, gavl_track_get_metadata(mi), 0);
  
  for(i = 0; i < num_tracks; i++)
    {
    child = xmlNewTextChild(xml_cd, NULL, (xmlChar*)"TRACK", NULL);
    xmlAddChild(child, BG_XML_NEW_TEXT("\n"));

    bg_dictionary_2_xml(child, gavl_track_get_metadata(gavl_get_track(mi, i)), 1);
    
    xmlAddChild(xml_cd, BG_XML_NEW_TEXT("\n"));
    }

  xmlSaveFile(filename, xml_doc);
  xmlFreeDoc(xml_doc);
  }
