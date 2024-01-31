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

#ifndef BG_XMLUTILS_H_INCLUDED
#define BG_XMLUTILS_H_INCLUDED

/* xml utilities */

#include <libxml/tree.h>
#include <libxml/parser.h>
#include <libxml/xmlsave.h>
#include <gavl/gavf.h>


/*  Macro, which calls strcmp, but casts the first argument to char*
 *  This is needed because libxml strings are uint8_t*
 */

#define BG_XML_STRCMP(a, b) strcmp((const char*)a, b)
#define BG_XML_GET_PROP(a, b) (char*)xmlGetProp(a, (xmlChar*)b)
#define BG_XML_SET_PROP(a, b, c) xmlSetProp(a, (xmlChar*)b, (xmlChar*)c)
#define BG_XML_NEW_TEXT(a) xmlNewText((xmlChar*)a)

/* 
 * memory writer for libxml
 */

char * bg_xml_save_to_memory(xmlDocPtr doc);
void bg_xml_save_to_buffer(xmlDocPtr doc, gavl_buffer_t * buf);


/* Opt can be a combination of
   XML_SAVE_FORMAT = 1 : format save output
   XML_SAVE_NO_DECL = 2 : drop the xml declaration
   XML_SAVE_NO_EMPTY = 4 : no empty tags
   XML_SAVE_NO_XHTML = 8 : disable XHTML1 specific rules
   XML_SAVE_XHTML = 16 : force XHTML1 specific rules
   XML_SAVE_AS_XML = 32 : force XML serialization on HTML doc
   XML_SAVE_AS_HTML = 64 : force HTML serialization on XML doc
   XML_SAVE_WSNONSIG = 128 : format with non-significant whitespace
*/

char * bg_xml_save_to_memory_opt(xmlDocPtr doc, int opt);
void bg_xml_save_to_buffer_opt(xmlDocPtr doc, int opt, gavl_buffer_t * buf);

xmlDocPtr bg_xml_load_FILE(FILE * f);
void bg_xml_save_FILE(xmlDocPtr doc, FILE * f);

xmlDocPtr bg_xml_load_gavf_io(gavl_io_t* io);
void bg_xml_save_io(xmlDocPtr doc, gavl_io_t* io);

xmlDocPtr bg_xml_parse_file(const char * filename, int lock);
void bg_xml_save_file(xmlDocPtr doc, const char * filename, int lock);

xmlNodePtr bg_xml_find_node_child(xmlNodePtr parent, const char * child);
xmlNodePtr bg_xml_find_doc_child(xmlDocPtr parent, const char * child);

xmlNodePtr bg_xml_find_next_node_child(xmlNodePtr parent, xmlNodePtr child);
xmlNodePtr bg_xml_find_next_node_child_by_name(xmlNodePtr parent, xmlNodePtr node,
                                               const char * name);


xmlNodePtr bg_xml_find_next_doc_child(xmlDocPtr parent, xmlNodePtr child);

xmlNodePtr bg_xml_append_child_node(xmlNodePtr parent, const char * name,
                                    const char * content);

/* Get the contents of node like <node>content</node>.
   Returns an empty string if the node has no children or
   NULL
 */

const char * bg_xml_node_get_text_content(xmlNodePtr parent);

/* Search all children of parent for one with the name child_name.
   Return it's text content */

const char * bg_xml_node_get_child_content(xmlNodePtr parent,
                                           const char * child_name);

/* Load an xml document from a http URL */
xmlDocPtr bg_xml_from_url(const char * url, char ** mimetype_ptr);

#endif // BG_XMLUTILS_H_INCLUDED
