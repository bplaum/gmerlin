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

#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <errno.h>

#include <gavl/metatags.h>


#include <gmerlin/utils.h>
#include <gmerlin/xmlutils.h>
#include <gmerlin/http.h>

#define BLOCK_SIZE 2048

#include <gmerlin/log.h>
#define LOG_DOMAIN "xmlutils"

#if 0
typedef struct
  {
  int bytes_written;
  int bytes_allocated;
  char * buffer;
  } bg_xml_output_mem_t;
#endif

static int mem_write_callback(void * context, const char * buffer,
                              int len)
  {
  gavl_buffer_append_data(context, (const uint8_t*)buffer, len);
  return len;
  }

static int mem_close_callback(void * context)
  {
  uint8_t zero = 0x00;
  gavl_buffer_append_data(context, &zero, 1);
  return 0;
  }


void bg_xml_save_to_buffer(xmlDocPtr doc, gavl_buffer_t * buf)
  {
  xmlOutputBufferPtr b = xmlOutputBufferCreateIO (mem_write_callback,
                                                  mem_close_callback,
                                                  buf,
                                                  NULL);
  /*
   *  From the libxml documentation of xmlSaveFileTo:
   *  Warning ! This call xmlOutputBufferClose() on buf which is not available after this call.
   */
  xmlSaveFileTo(b, doc, NULL);
  }

char * bg_xml_save_to_memory(xmlDocPtr doc)
  {
  gavl_buffer_t buffer;
  gavl_buffer_init(&buffer);
  bg_xml_save_to_buffer(doc, &buffer);
  return (char*)buffer.buf;
  }


void bg_xml_save_to_buffer_opt(xmlDocPtr doc, int opt, gavl_buffer_t * buf)
  {
  xmlSaveCtxtPtr xtc;
  
  xtc = xmlSaveToIO(mem_write_callback, 
                    mem_close_callback, 
                    buf, NULL, opt);
  xmlSaveDoc(xtc, doc);
  xmlSaveClose(xtc);
  }

char * bg_xml_save_to_memory_opt(xmlDocPtr doc, int opt)
  {
  gavl_buffer_t buf;

  gavl_buffer_init(&buf);
  bg_xml_save_to_buffer_opt(doc, opt, &buf);
  return (char*)buf.buf;
  }

static int FILE_write_callback(void * context, const char * buffer,
                               int len)
  {
  return fwrite(buffer, 1, len, context);
  }

static int FILE_read_callback(void * context, char * buffer,
                               int len)
  {
  return fread(buffer, 1, len, context);
  }

static int gavl_io_write_callback(void * context, const char * buffer,
                                  int len)
  {
  return gavl_io_write_data(context, (uint8_t*)buffer, len);
  }

static int gavl_io_read_callback(void * context, char * buffer,
                                 int len)
  {
  return gavl_io_read_data(context, (uint8_t*)buffer, len);
  }


xmlDocPtr bg_xml_load_FILE(FILE * f)
  {
  return xmlReadIO(FILE_read_callback, NULL, f, NULL, NULL, 0);
  }

void bg_xml_save_FILE(xmlDocPtr doc, FILE * f)
  {
  xmlOutputBufferPtr b;

  b = xmlOutputBufferCreateIO (FILE_write_callback,
                               NULL, f, NULL);
  xmlSaveFormatFileTo(b, doc, NULL, 1);
  }

xmlDocPtr bg_xml_load_gavf_io(gavl_io_t* io)
  {
  return xmlReadIO(gavl_io_read_callback, NULL, io, NULL, NULL, 0);
  }

void bg_xml_save_io(xmlDocPtr doc, gavl_io_t* io)
  {
  xmlOutputBufferPtr b;

  b = xmlOutputBufferCreateIO (gavl_io_write_callback,
                               NULL, io, NULL);
  xmlSaveFileTo(b, doc, NULL);
  }



xmlDocPtr bg_xml_parse_file(const char * filename, int lock)
  {
  xmlDocPtr ret = NULL;
  FILE * f = fopen(filename, "r");
  if(!f)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Cannot open %s: %s",
           filename, strerror(errno));
    return NULL;
    }

  if(lock)
    {
    if(!bg_lock_file(f, 0))
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Cannot lock file %s: %s",
             filename, strerror(errno));
    }

  if(bg_file_size(f))
    ret = bg_xml_load_FILE(f);
  
  if(lock)
    {
    if(!bg_unlock_file(f))
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Cannot unlock file %s: %s",
             filename, strerror(errno));
    }
  
  fclose(f);
  return ret;
  }

void bg_xml_save_file(xmlDocPtr doc, const char * filename, int lock)
  {
  FILE * f = fopen(filename, "w");
  if(!f)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Cannot open %s: %s",
           filename, strerror(errno));
    return;
    }
  
  if(lock)
    {
    if(!bg_lock_file(f, 1))
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Cannot lock file %s: %s",
             filename, strerror(errno));
    }
  
  bg_xml_save_FILE(doc, f);
  fflush(f);
  
  if(lock)
    {
    if(!bg_unlock_file(f))
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Cannot unlock file %s: %s",
             filename, strerror(errno));
    }
  fclose(f);
  }

static xmlNodePtr find_children(xmlNodePtr node, const char * child)
  {
  while(node && (!node->name || BG_XML_STRCMP(node->name, child)))
    node = node->next;
  return node;
  }

xmlNodePtr
bg_xml_find_node_child(xmlNodePtr parent, const char * child)
  {
  return find_children(parent->children, child);
  }

xmlNodePtr
bg_xml_find_doc_child(xmlDocPtr parent, const char * child)
  {
  return find_children(parent->children, child);
  }

xmlNodePtr
bg_xml_find_next_node_child(xmlNodePtr parent, xmlNodePtr node)
  {
  if(!node)
    node = parent->children;
  else
    node = node->next;

  while(node && (node->type != XML_ELEMENT_NODE))
    node = node->next;
  return node;
  }

xmlNodePtr
bg_xml_find_next_node_child_by_name(xmlNodePtr parent, xmlNodePtr node,
                                    const char * name)
  {
  while(1)
    {
    node = bg_xml_find_next_node_child(parent, node);
    if(!node || (node->name && !strcmp((char*)node->name, name)))
      break;
    }
  return node;
  }


xmlNodePtr
bg_xml_find_next_doc_child(xmlDocPtr parent, xmlNodePtr node)
  {
  if(!node)
    node = parent->children;
  else
    node = node->next;
  while(node && (node->type != XML_ELEMENT_NODE))
    node = node->next;
  return node;
  }



xmlNodePtr
bg_xml_append_child_node(xmlNodePtr parent, const char * name,
                         const char * content)
  {
  xmlNodePtr node;

  node = xmlNewTextChild(parent, NULL, (xmlChar*)name, NULL);
  if(content)
    xmlAddChild(node, BG_XML_NEW_TEXT(content));
  else
    xmlAddChild(node, BG_XML_NEW_TEXT("\n"));
  xmlAddChild(parent, BG_XML_NEW_TEXT("\n"));
  return node;
  }

const char empty_string = '\0';

const char * bg_xml_node_get_text_content(xmlNodePtr parent)
  {
  xmlNodePtr node = parent->children;

  if(!node)
    return &empty_string;
  
  if((node->type != XML_TEXT_NODE) || node->next)
    return NULL;
  return (char*)node->content;
  }

xmlDocPtr bg_xml_from_url(const char * url, char ** mimetype_ptr)
  {
  xmlDocPtr ret = NULL;
  gavl_dictionary_t dict;
  gavl_buffer_t buf;
  const char * var;
  
  gavl_dictionary_init(&dict);
  gavl_buffer_init(&buf);
  
  if(!bg_http_get(url, &buf, &dict))
    goto fail;

  if(!mimetype_ptr)
    {
    if(!(var = gavl_dictionary_get_string(&dict, GAVL_META_MIMETYPE)) ||
       (strncasecmp(var, "text/xml", 8) &&
        strncasecmp(var, "application/xml", 15)))
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Invalid mimetype %s for xml parsing from %s",
             var, url);
      goto fail;
      }
    }
  else
    *mimetype_ptr = gavl_strdup(gavl_dictionary_get_string(&dict, GAVL_META_MIMETYPE));

  //  fprintf(stderr, "bg_xml_from_url %s\n", url);
  
  //  ret = xmlParseMemory((char*)buf.buf, buf.len);

  ret = xmlReadMemory((char*)buf.buf, buf.len, NULL, NULL,
                      XML_PARSE_RECOVER |
                      XML_PARSE_NOERROR |
                      XML_PARSE_NOWARNING);
  
  //  fprintf(stderr, "bg_xml_from_url done %p\n", ret);
  
  fail:

  gavl_buffer_free(&buf);
  gavl_dictionary_free(&dict);
  
  return ret;
  }

const char * bg_xml_node_get_child_content(xmlNodePtr parent,
                                           const char * child_name)
  {
  xmlNodePtr child = bg_xml_find_next_node_child_by_name(parent, NULL,
                                                         child_name);
  if(!child)
    return NULL;
  return bg_xml_node_get_text_content(child);
  }
