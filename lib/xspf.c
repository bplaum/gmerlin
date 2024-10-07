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
#include <stdlib.h>


#include <gavl/metatags.h>

#include <gmerlin/xmlutils.h>
#include <gmerlin/xspf.h>
#include <gmerlin/utils.h>

#define GMERLIN_PREFIX     "http://gmerlin.sourceforge.net/metadata/"
#define GMERLIN_PREFIX_LEN 40

xmlDocPtr bg_xspf_create()
  {
  xmlNsPtr ns;
  xmlNodePtr root;
  xmlDocPtr ret;
  
  ret = xmlNewDoc((xmlChar*)"1.0");

  root = xmlNewDocRawNode(ret, NULL, (xmlChar*)"playlist", NULL);
  xmlDocSetRootElement(ret, root);

  ns =
    xmlNewNs(root,
             (xmlChar*)"http://xspf.org/ns/0/",
             NULL);
  xmlSetNs(root, ns);
  xmlAddChild(root, BG_XML_NEW_TEXT("\n"));

  BG_XML_SET_PROP(root, "version", "1");
  bg_xml_append_child_node(root, "trackList", NULL);

  return ret; 
  }

xmlNodePtr bg_xspf_get_playlist(xmlDocPtr xspf)
  {
  return bg_xml_find_next_doc_child(xspf, NULL);
  }

xmlNodePtr bg_xspf_get_tracklist(xmlDocPtr xspf)
  {
  xmlNodePtr ret;

  if(!(ret = bg_xspf_get_playlist(xspf)) ||
     !(ret = bg_xml_find_node_child(ret, "trackList")))
    return NULL;
  
  return ret;
  }

static struct
  {
  const char * gavl_name;
  const char * xspf_name;
  }
xml_tags[] =
  {
    { GAVL_META_TITLE,       "title"    },
    { GAVL_META_ARTIST,      "creator"  },
    { GAVL_META_ALBUM,       "album"    },
    { GAVL_META_TRACKNUMBER, "trackNum" },
    { /* End */ }
  };

/* To be added as meta elements */
static const char * meta_tags[] =
  {
    GAVL_META_COVER_URL,
    GAVL_META_LABEL,
    NULL,
  };

xmlNodePtr bg_xspf_add_track(xmlDocPtr xspf, const gavl_dictionary_t * m)
  {
  const char * location;
  gavl_time_t duration;
  int i, j, num;
  char * tmp_string;
  const char * var;
  xmlNodePtr ret;
  xmlNodePtr child;
  xmlNodePtr parent = bg_xspf_get_tracklist(xspf);
  
  if(!parent)
    return NULL;
  ret = bg_xml_append_child_node(parent, "track", NULL);

  if(m)
    {
    i = 0;
    
    if(gavl_metadata_get_src(m, GAVL_META_SRC, 0, NULL, &location))
      bg_xml_append_child_node(ret, "location", location);
    
    while(xml_tags[i].gavl_name)
      {
      if((var = gavl_dictionary_get_string(m, xml_tags[i].gavl_name)))
        bg_xml_append_child_node(ret, xml_tags[i].xspf_name, var);
      i++;
      }

    if(gavl_dictionary_get_long(m, GAVL_META_APPROX_DURATION, &duration))
      {
      char str[32];
      snprintf(str, 32, "%"PRId64, duration / (GAVL_TIME_SCALE/1000));
      bg_xml_append_child_node(ret, "duration", str);
      }
      
    i = 0;
    
    while(meta_tags[i])
      {
      num = gavl_dictionary_get_arr_len(m, meta_tags[i]);

      for(j = 0; j < num; j++)
        {
        var = gavl_dictionary_get_arr(m, meta_tags[i], j);

        child = bg_xml_append_child_node(ret, "meta", var);
        
        tmp_string = gavl_sprintf("%s%s", GMERLIN_PREFIX, meta_tags[i]);
        BG_XML_SET_PROP(child, "rel", tmp_string);
        free(tmp_string);
        }
      
      i++;
      }

    if((var = gavl_dictionary_get_string_image_max(m,
                                          GAVL_META_COVER_URL,
                                          600, 600, NULL)))
      bg_xml_append_child_node(ret, "image", var);
    }
  return ret;
  }

xmlNodePtr bg_xspf_get_track(xmlDocPtr xspf, xmlNodePtr node,
                             gavl_dictionary_t * m)
  {
  char * rel;
  int i;
  xmlNodePtr child;
  xmlNodePtr parent = bg_xspf_get_tracklist(xspf);
  if(!parent)
    return NULL;

  if(!(node = bg_xml_find_next_node_child_by_name(parent, node, "track")))
    return NULL;

  if(m)
    {
    i = 0;

    if((child = bg_xml_find_node_child(node, "location")))
      {
      gavl_metadata_add_src(m, GAVL_META_SRC,
                            NULL,
                            bg_xml_node_get_text_content(child));
      }

    while(xml_tags[i].xspf_name)
      {
      if((child = bg_xml_find_node_child(node, xml_tags[i].xspf_name)))
        {
        gavl_dictionary_set_string(m, xml_tags[i].gavl_name,
                          bg_xml_node_get_text_content(child));
        }
      i++;
      }

    if((child = bg_xml_find_node_child(node, "duration")))
      {
      gavl_dictionary_set_long(m, GAVL_META_APPROX_DURATION,
                             (int64_t)1000 * strtoll(bg_xml_node_get_text_content(child), NULL, 10));
      }
    
    child = NULL;
    while((child = bg_xml_find_next_node_child_by_name(node, child, "meta")))
      {
      if((rel = BG_XML_GET_PROP(child, "rel")) &&
         !strncmp(rel, GMERLIN_PREFIX, GMERLIN_PREFIX_LEN))
        {
        gavl_metadata_append(m, rel + GMERLIN_PREFIX_LEN,
                             bg_xml_node_get_text_content(child));
        }
      }
    }
  
  return node;
  }
