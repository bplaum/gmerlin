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

/* Gmerlin includes */

#include <string.h>
#include <unistd.h>

#include <gavl/metatags.h>
#include <config.h>
#include <gmerlin/translation.h>

#include <gmerlin/tree.h>
#include <treeprivate.h>
#include <gmerlin/utils.h>
#include <gmerlin/log.h>
#include <gmerlin/xmlutils.h>
#include <gmerlin/bgsocket.h>
#include <gmerlin/http.h>

#include <gmerlin/upnp/device.h>
#include <gmerlin/upnp/devicedesc.h>
#include <upnp/didl.h>

#define LOG_DOMAIN "tree_upnp"

#define DUMP_DIDL

#if 0
static void add_upnp_server(bg_media_tree_t * t, const char * desc_url)
  {
  xmlNodePtr dev_node = NULL;
  xmlNodePtr service_node = NULL; 
  xmlNodePtr node;
  
  const char * control_url_c = NULL;
  char * icon_url      = NULL;
  const char * server_name   = NULL;
  char * url_base      = NULL;
  char * control_url = NULL;

  bg_album_t * a;
  bg_album_t * tmp;
  xmlDocPtr doc = bg_xml_from_url(desc_url, NULL);
  
  if(!doc)
    goto fail;
  
  /* URL Base */

  url_base = bg_upnp_device_description_get_url_base(desc_url, doc);

  /* Find the device node belonging to the media server */
  
  if(!(dev_node = bg_upnp_device_description_get_device_node(doc, "MediaServer", 1)))
    goto fail;
  
  /* Name */

  server_name = bg_upnp_device_description_get_label(dev_node);
  
  /* Get control URL */
  if(!(service_node = bg_upnp_device_description_get_service_node(dev_node, "ContentDirectory", 1)))
    goto fail;
  
  node = bg_xml_find_node_child(service_node, "controlURL");
  if(!node)
    goto fail;

  control_url_c = bg_xml_node_get_text_content(node);
  
  /* Create root album for this server */

  control_url =  bg_upnp_device_description_make_url(control_url_c, url_base);
  icon_url = bg_upnp_device_description_get_icon_url(dev_node, 16, url_base);

  //  fprintf(stderr, "Got UPNP Server. Name: %s, control URL: %s icon URL: %s\n",
  //          server_name, control_url, icon_url);

  /* Create album */
  a = bg_album_create(&t->com, BG_ALBUM_TYPE_UPNP_ROOT, NULL);
  a->name = gavl_strdup(server_name);

  gavl_dictionary_set_string_nocopy(&a->m, BG_ALBUM_CONTROL_URL, control_url);
  gavl_dictionary_set_string_nocopy(&a->m, BG_ALBUM_ICON_URL, icon_url);
  gavl_dictionary_set_string(&a->m, BG_ALBUM_DESC_URL, desc_url);
  gavl_dictionary_set_string(&a->m, BG_ALBUM_UPNP_ID, "0");
  
  control_url = NULL;
  icon_url = NULL;
  
  /* Put into chain */
  tmp = t->children;
  if(!tmp)
    t->children = a;
  else
    {
    while(1)
      {
      if((!tmp->next) || (tmp->next->type == BG_ALBUM_TYPE_PLUGIN))
        break;
      tmp = tmp->next;
      }
    a->next = tmp->next;
    tmp->next = a;
    }

  if(t->change_callback)
    t->change_callback(t, t->change_callback_data);
  
  //  browse_upnp(control_url, "0", 0);
    
  fail:
  if(doc)
    xmlFreeDoc(doc);
  if(control_url)
    free(control_url);
  if(icon_url)
    free(icon_url);
  if(url_base)
    free(url_base);
  }

static void remove_upnp_server(bg_media_tree_t * t, const char * desc_url)
  {
  const char * url;
  bg_album_t * a = t->children;
  bg_album_t * before = NULL;

  while(a)
    {
    if((a->type == BG_ALBUM_TYPE_UPNP_ROOT) &&
       (url = gavl_dictionary_get_string(&a->m, BG_ALBUM_DESC_URL)) &&
       !strcmp(url, desc_url))
      break;
    before = a;
    a = a->next;
    }

  if(!a)
    return;

  if(before)
    before->next = a->next;
  else
    t->children = a->next;

  a->next = NULL;
  bg_album_destroy(a);

  if(t->change_callback)
    t->change_callback(t, t->change_callback_data);
  
  }

static void ssdp_callback(void * priv, int add, const char * type,
                          int version, const char * desc_url, const char * uuid)
  {
  bg_media_tree_t * t = priv;

  if(!type || !desc_url || strcmp(type, "MediaServer") || (version < 1))
    return;
  
  if(add)
    {
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Adding upnp media server, URL: %s",
           desc_url);
    add_upnp_server(t, desc_url);
    }
  else
    {
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Removing upnp media server, URL: %s",
           desc_url);
    remove_upnp_server(t, desc_url);
    }
  }
#endif

void bg_media_tree_upnp_create(bg_media_tree_t * t)
  {
  t->ssdp = bg_ssdp_create(NULL);
  //  if(t->ssdp)
  //    bg_ssdp_set_callback(t->ssdp, ssdp_callback, t);
  }

void bg_media_tree_upnp_update(bg_media_tree_t * t)
  {
  if(t->ssdp)
    bg_ssdp_update(t->ssdp);
  }

void bg_media_tree_upnp_destroy(bg_media_tree_t * t)
  {
  if(t->ssdp)
    bg_ssdp_destroy(t->ssdp);
  }

static int get_year(xmlNodePtr node)
  {
  const char * str;
  int dummy1, dummy2, ret;

  str = bg_xml_node_get_child_content(node, "date");

  if(str && (sscanf(str, "%d-%d-%d", &ret, &dummy1, &dummy2) == 3))
    return ret;
  else
    return -1;
  }

/* Load didl */

static void load_container(bg_album_t * a, xmlNodePtr node)
  {
  const char * klass;
  const char * title;
  const char * str;

  int year;
  bg_album_t * child;
  
  klass = bg_xml_node_get_child_content(node, "class");
  if(!klass)
    return;

  child = bg_album_create(a->com,
                          BG_ALBUM_TYPE_UPNP,
                          a);
  
  title = bg_xml_node_get_child_content(node, "title");

  if(!strcmp(klass, "object.container.album.musicAlbum"))
    {
    year = get_year(node);
    if(year > 0)
      child->name = bg_sprintf("%s (%d)", title, year);
    }
  
  if(!child->name) 
    child->name = gavl_strdup(title);
  
  //  fprintf(stderr, "Loading album\n");
  
  str = BG_XML_GET_PROP(node, "id");
  if(str)
    gavl_dictionary_set_string(&child->m, BG_ALBUM_UPNP_ID, str);
  
  /* Add to list */
  a->children = bg_album_append_to_list(a->children, child);
  }


static void load_item(bg_album_t * a, xmlNodePtr node)
  {
  bg_album_entry_t * e;
  
  e = bg_album_entry_create();
  e->parent = a;
  
  /* Get locations */
  // gavl_dictionary_set_string_nocpy(&e->m, GAVL_META_LOCATION, bg_didl_get_location(node, &e->duration));
  
  /* Get metadata */
  bg_metadata_from_didl(&e->m, node);
  
  if(!gavl_dictionary_get_string(&e->m, GAVL_META_MEDIA_CLASS))
    bg_album_entry_destroy(e);
  else
    bg_album_insert_entries_before(a, e, NULL);
  }

static int load_didl(bg_album_t * a)
  {
  xmlDocPtr didl_doc = NULL;
  xmlNodePtr didl_node;
  xmlNodePtr di;
  
  bg_album_t * tmp;
  const char * control_url = NULL;
  const char * id = NULL;
  int ret = 0;
  //  fprintf(stderr, "Load didl\n");
  
  /* Get control URL */
  tmp = a;
  while(tmp)
    {
    if(tmp->type == BG_ALBUM_TYPE_UPNP_ROOT)
      {
      control_url = gavl_dictionary_get_string(&tmp->m,
                                      BG_ALBUM_CONTROL_URL);
      break;
      }
    tmp = tmp->parent;
    }

  if(!control_url)
    goto fail;

  id = gavl_dictionary_get_string(&a->m, BG_ALBUM_UPNP_ID);
  if(!id)
    goto fail;
  
  /* Load didl */
  didl_doc = bg_upnp_contentdirectory_browse_xml(control_url, id, 0);
  if(!didl_doc)
    goto fail;

  /* Browse through the items */

  didl_node = bg_xml_find_next_doc_child(didl_doc, NULL);
  
  di = NULL;
  
  while((di = bg_xml_find_next_node_child(didl_node, di)))
    {
    if(!BG_XML_STRCMP(di->name, "item"))
      load_item(a, di);
    else if(!BG_XML_STRCMP(di->name, "container"))
      load_container(a, di);
    }
  
  a->flags |= BG_ALBUM_HAS_DIDL;

  ret = 1;
  fail:
  if(didl_doc)
    xmlFreeDoc(didl_doc);
  
  return ret;
  }

int bg_album_open_upnp(bg_album_t * a)
  {
  if(!(a->flags & BG_ALBUM_HAS_DIDL))
    return load_didl(a);
  return 1;
  }

void bg_album_close_upnp(bg_album_t * a)
  {

  }

void bg_album_expand_upnp(bg_album_t * a)
  {
  if(!(a->flags & BG_ALBUM_HAS_DIDL))
    load_didl(a);
  }

void bg_album_collapse(bg_album_t * a)
  {
  
  }
