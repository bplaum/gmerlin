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

#include <gavl/metatags.h>

#include <gmerlin/upnp/devicedesc.h>


#include <gmerlin/utils.h>

// #include <upnp/devicepriv.h>


// #define URL_BASE_MARKER "@URL_BASE@"

xmlDocPtr bg_upnp_device_description_create(const char * type, int version)
  {
  xmlDocPtr ret;
  xmlNodePtr root;
  xmlNodePtr node;
  xmlNsPtr ns;
  char * tmp_string;
  
  ret = xmlNewDoc((xmlChar*)"1.0");

  root = xmlNewDocRawNode(ret, NULL, (xmlChar*)"root", NULL);
  xmlDocSetRootElement(ret, root);

  ns =
    xmlNewNs(root,
             (xmlChar*)"urn:schemas-upnp-org:device-1-0",
             NULL);
  xmlSetNs(root, ns);
  xmlAddChild(root, BG_XML_NEW_TEXT("\n"));
  
  node = bg_xml_append_child_node(root, "specVersion", NULL);
  bg_xml_append_child_node(node, "major", "1");
  bg_xml_append_child_node(node, "minor", "0");
  
  //  bg_xml_append_child_node(root, "URLBase", URL_BASE_MARKER);
  
  node = bg_xml_append_child_node(root, "device", NULL);
  
  tmp_string = bg_sprintf("urn:schemas-upnp-org:device:%s:%d", type, version);
  bg_xml_append_child_node(node, "deviceType", tmp_string);
  free(tmp_string);
  bg_xml_append_child_node(node, "presentationURL", "/");
  return ret;
  }

static xmlNodePtr get_device_node(xmlDocPtr ptr)
  {
  xmlNodePtr node;

  if(!(node = bg_xml_find_doc_child(ptr, "root")) ||
     !(node = bg_xml_find_node_child(node, "device")))
    return NULL;
  return node;
  }

void bg_upnp_device_description_set_name(xmlDocPtr ptr, const char * name)
  {
  xmlNodePtr node = get_device_node(ptr);
  bg_xml_append_child_node(node, "friendlyName", name);
  }

void bg_upnp_device_description_set_manufacturer(xmlDocPtr ptr, const char * name)
  {
  xmlNodePtr node = get_device_node(ptr);
  bg_xml_append_child_node(node, "manufacturer", name);
  }

void bg_upnp_device_description_set_manufacturer_url(xmlDocPtr ptr, const char * name)
  {
  xmlNodePtr node = get_device_node(ptr);
  bg_xml_append_child_node(node, "manufacturerURL", name);
  }

void bg_upnp_device_description_set_model_description(xmlDocPtr ptr, const char * name)
  {
  xmlNodePtr node = get_device_node(ptr);
  bg_xml_append_child_node(node, "modelDescription", name);
  }

void bg_upnp_device_description_set_model_name(xmlDocPtr ptr, const char * name)
  {
  xmlNodePtr node = get_device_node(ptr);
  bg_xml_append_child_node(node, "modelName", name);
  }

void bg_upnp_device_description_set_model_number(xmlDocPtr ptr, const char * name)
  {
  xmlNodePtr node = get_device_node(ptr);
  bg_xml_append_child_node(node, "modelNumber", name);
  }

void bg_upnp_device_description_set_model_url(xmlDocPtr ptr, const char * name)
  {
  xmlNodePtr node = get_device_node(ptr);
  bg_xml_append_child_node(node, "modelURL", name);
  }

void bg_upnp_device_description_set_serial_number(xmlDocPtr ptr, const char * name)
  {
  xmlNodePtr node = get_device_node(ptr);
  bg_xml_append_child_node(node, "serialNumber", name);
  }

void bg_upnp_device_description_set_uuid(xmlDocPtr ptr, uuid_t uuid)
  {
  char uuid_str[37];
  char * tmp_string;
  xmlNodePtr node = get_device_node(ptr);

  uuid_unparse_lower(uuid, uuid_str);
  tmp_string = bg_sprintf("uuid:%s", uuid_str);
  bg_xml_append_child_node(node, "UDN", tmp_string);
  free(tmp_string);
  }

void bg_upnp_device_description_set_upc(xmlDocPtr ptr, const char * name)
  {
  xmlNodePtr node = get_device_node(ptr);
  bg_xml_append_child_node(node, "UPC", name);
  }

#if 0
void bg_upnp_device_description_add_service(xmlDocPtr ptr,
                                            bg_upnp_device_t * dev,
                                            const char * type, int version, const char * name)
  {
  char * tmp_string;
  xmlNodePtr servicelist;
  xmlNodePtr service;
  
  xmlNodePtr node = get_device_node(ptr);

  servicelist = bg_xml_find_node_child(node, "serviceList");
  
  if(!servicelist)
    servicelist = bg_xml_append_child_node(node, "serviceList", NULL);
  
  service = bg_xml_append_child_node(servicelist, "service", NULL);

  tmp_string = bg_sprintf("urn:schemas-upnp-org:service:%s:%d", type, version);
  bg_xml_append_child_node(service, "serviceType", tmp_string);
  free(tmp_string);
  
  tmp_string = bg_sprintf("urn:upnp-org:serviceId:%s", type);
  bg_xml_append_child_node(service, "serviceId", tmp_string);
  free(tmp_string);

  tmp_string = bg_sprintf("%s%s/desc.xml", dev->path, name);
  bg_xml_append_child_node(service, "SCPDURL", tmp_string);
  free(tmp_string);
  
  tmp_string = bg_sprintf("%s%s/control", dev->path, name);
  bg_xml_append_child_node(service, "controlURL", tmp_string);
  free(tmp_string);

  tmp_string = bg_sprintf("%s%s/event", dev->path, name);
  bg_xml_append_child_node(service, "eventSubURL", tmp_string);
  free(tmp_string);
  
  }
#endif

void bg_upnp_device_description_add_icon(xmlDocPtr ptr,
                                         const char * mimetype,
                                         int width, int height, int depth, const char * url)
  {
  char * tmp_string;
  xmlNodePtr iconlist;
  xmlNodePtr icon;
  xmlNodePtr node = get_device_node(ptr);

  iconlist = bg_xml_find_node_child(node, "iconList");
  if(!iconlist)
    iconlist = xmlNewTextChild(node, NULL, (xmlChar*)"iconList", NULL);

  icon = xmlNewTextChild(iconlist, NULL, (xmlChar*)"icon", NULL);
  bg_xml_append_child_node(icon, "mimetype", mimetype);

  tmp_string = bg_sprintf("%d", width);
  bg_xml_append_child_node(icon, "width", tmp_string);
  free(tmp_string);

  tmp_string = bg_sprintf("%d", height);
  bg_xml_append_child_node(icon, "height", tmp_string);
  free(tmp_string);

  tmp_string = bg_sprintf("%d", depth);
  bg_xml_append_child_node(icon, "depth", tmp_string);
  free(tmp_string);
  
  bg_xml_append_child_node(icon, "url", url);
  }

/* Functions for upnp Clients */

static char * get_url_base(const char * desc_url)
  {
  char * ret;
  char * host;
  int port = -1;
  if(!bg_url_split(desc_url,
                   NULL,
                   NULL,
                   NULL,
                   &host,
                   &port,
                   NULL))
    {
    return NULL;
    }
  if(port == -1)
    ret = bg_sprintf("http://%s", host);
  else
    ret = bg_sprintf("http://%s:%d", host, port);
  free(host);
  return ret;
  }

char * bg_upnp_device_description_get_url_base(const char * desc_url, xmlDocPtr doc)
  {
  xmlNodePtr root;
  xmlNodePtr node;

  root = bg_xml_find_doc_child(doc, "root");
  if(!root)
    goto fail;

  node = bg_xml_find_node_child(root, "URLBase");
  if(node)
    return gavl_strdup(bg_xml_node_get_text_content(node));
  else
    return get_url_base(desc_url);
  
  
  fail:
  return NULL;
  }

char * bg_upnp_device_description_make_url(const char * url, const char * base)
  {
  if(!strncasecmp(url, "http://", 7))
    return gavl_strdup(url);
  else if((base[strlen(base)-1] == '/') && (url[0] == '/'))
    return bg_sprintf("%s%s", base, url+1);
  else if((base[strlen(base)-1] == '/') || (url[0] == '/'))
    return bg_sprintf("%s%s", base, url);
  else
    return bg_sprintf("%s/%s", base, url);
  }


static int check_device(xmlNodePtr node, const char * device, int version)
  {
  xmlNodePtr type_node;
  int device_len;
  const char * type;

  device_len = strlen(device);
  
  type_node = bg_xml_find_node_child(node, "deviceType");
  if(!type_node)
    return 0;

  type = bg_xml_node_get_text_content(type_node);
  if(!type)
    return 0;

  if(strncmp(type, "urn:schemas-upnp-org:device:", 28))
    return 0;

  type += 28;

  if(strncmp(type, device, device_len))
    return 0;

  type += device_len;

  if(*type != ':')
    return 0;
  
  if(atoi(type + 1) < version)
    return 0;

  return 1;
  }

xmlNodePtr bg_upnp_device_description_get_device_node(xmlDocPtr doc, const char * device, int version)
  {
  xmlNodePtr root;
  xmlNodePtr dev_node = NULL;
  xmlNodePtr list_node;
  
  root = bg_xml_find_doc_child(doc, "root");
  if(!root)
    goto fail;
  
  dev_node = bg_xml_find_node_child(root, "device");
  
  if(dev_node)
    {
    if(!check_device(dev_node, device, version))
      dev_node = NULL;
    }
  
  if(!dev_node)
    {
    /* Look for an embedded device */
    list_node = bg_xml_find_node_child(root, "deviceList");
    if(!list_node)
      goto fail;

    while(1)
      {
      dev_node = bg_xml_find_next_node_child_by_name(list_node,
                                                     dev_node, "device");
      if(!dev_node || check_device(dev_node, device, version))
        break;
      }
    }
  
  fail:
  return dev_node;
  }

static int check_service(xmlNodePtr node, const char * service, int version)
  {
  xmlNodePtr type_node;
  int service_len;
  const char * type;

  service_len = strlen(service);
  
  type_node = bg_xml_find_node_child(node, "serviceType");
  if(!type_node)
    return 0;

  type = bg_xml_node_get_text_content(type_node);
  if(!type)
    return 0;

  if(strncmp(type, "urn:schemas-upnp-org:service:", 29))
    return 0;

  type += 29;

  if(strncmp(type, service, service_len))
    return 0;

  type += service_len;

  if(*type != ':')
    return 0;
  
  if(atoi(type + 1) < version)
    return 0;

  return 1;
  }

xmlNodePtr bg_upnp_device_description_get_service_node(xmlNodePtr dev_node, const char * service, int version)
  {
  xmlNodePtr list_node;
  xmlNodePtr service_node = NULL;
  
  if(!(list_node = bg_xml_find_node_child(dev_node, "serviceList")))
    goto fail;
  
  while(1)
    {
    service_node = bg_xml_find_next_node_child_by_name(list_node, service_node, "service");
    if(!service_node || check_service(service_node, service, version))
      break;
    }
  
  fail:
  return service_node;
  }

static char * make_url(const char * url, const char * base)
  {
  if(!strncasecmp(url, "http://", 7))
    return gavl_strdup(url);
  else if((base[strlen(base)-1] == '/') && (url[0] == '/'))
    return bg_sprintf("%s%s", base, url+1);
  else if((base[strlen(base)-1] == '/') || (url[0] == '/'))
    return bg_sprintf("%s%s", base, url);
  else
    return bg_sprintf("%s/%s", base, url);
  }

char *
bg_upnp_device_description_get_control_url(xmlNodePtr service_node,
                                           const char * url_base)
  {
  const char * control_url_c;
  xmlNodePtr node;

  node = bg_xml_find_node_child(service_node, "controlURL");
  if(!node)
    return NULL;

  control_url_c = bg_xml_node_get_text_content(node);
  return make_url(control_url_c, url_base);
  }

char *
bg_upnp_device_description_get_event_url(xmlNodePtr service_node,
                                         const char * url_base)
  {
  const char * event_url_c;
  xmlNodePtr node;

  node = bg_xml_find_node_child(service_node, "eventSubURL");
  if(!node)
    return NULL;

  event_url_c = bg_xml_node_get_text_content(node);
  return make_url(event_url_c, url_base);
  }

char *
bg_upnp_device_description_get_service_description(xmlNodePtr service_node,
                                                   const char * url_base)
  {
  const char * control_url_c;
  xmlNodePtr node;

  node = bg_xml_find_node_child(service_node, "SCPDURL");
  if(!node)
    return NULL;

  control_url_c = bg_xml_node_get_text_content(node);
  return make_url(control_url_c, url_base);
  }

xmlNodePtr
bg_upnp_service_description_get_action(xmlDocPtr desc, const char * action)
  {
  xmlNodePtr action_list;
  xmlNodePtr node = NULL;
  xmlNodePtr name;

  if(!(action_list = bg_xml_find_doc_child(desc, "scpd")) ||
     !(action_list = bg_xml_find_node_child(action_list, "actionList")))
    return NULL;
  
  while((node = bg_xml_find_next_node_child_by_name(action_list, node, "action")))
    {
    if((name = bg_xml_find_node_child(node, "name")) &&
       !strcmp(action, bg_xml_node_get_text_content(name)))
      return node;
    }
  return NULL;
  }

xmlNodePtr
bg_upnp_service_description_get_state_variable(xmlDocPtr desc, const char * var)
  {
  xmlNodePtr state_table;
  xmlNodePtr node = NULL;
  xmlNodePtr name;

  if(!(state_table = bg_xml_find_doc_child(desc, "scpd")) ||
     !(state_table = bg_xml_find_node_child(state_table, "serviceStateTable")))
    return NULL;
  
  while((node = bg_xml_find_next_node_child_by_name(state_table, node, "stateVariable")))
    {
    if((name = bg_xml_find_node_child(node, "name")) &&
       !strcmp(var, bg_xml_node_get_text_content(name)))
      return node;
    }
  return NULL;
  }

int bg_upnp_service_description_get_variable_range(xmlNodePtr node,
                                                   int * min, int * max, int * step)
  {
  xmlNodePtr child;

  if(!(node = bg_xml_find_node_child(node, "allowedValueRange")))
    return 0;
    
  if(min)
    {
    if(!(child = bg_xml_find_node_child(node, "minimum")))
      return 0;
    
    *min = atoi(bg_xml_node_get_text_content(child));
    }

  if(max)
    {
    if(!(child = bg_xml_find_node_child(node, "maximum")))
      return 0;
    
    *max = atoi(bg_xml_node_get_text_content(child));
    }

  if(step)
    {
    if(!(child = bg_xml_find_node_child(node, "step")))
      return 0;
    
    *step = atoi(bg_xml_node_get_text_content(child));
    }
  
  return 1;
  }

int bg_upnp_service_description_value_allowed(xmlNodePtr node, const char * value)
  {
  xmlNodePtr child = NULL;
  
  if(!(node = bg_xml_find_node_child(node, "allowedValueList")))
    return 0;

  while((child = bg_xml_find_next_node_child_by_name(node, child, "allowedValue")))
    {
    if(!strcmp(value, bg_xml_node_get_text_content(child)))
      return 1;
    }
  return 0;
  }

int
bg_upnp_device_description_is_gmerlin(xmlNodePtr node)
  {
  const char * str;
  
  if((node = bg_xml_find_node_child(node, "manufacturer")) &&
     (str = bg_xml_node_get_text_content(node)) &&
     strstr(str, "Gmerlin"))
    return 1;
  else
    return 0;
  }

#define MAX_ERROR 0x7FFFFFFF

static int check_icon(xmlNodePtr icon_node, int size)
  {
  const char * str;
  xmlNodePtr node;
  int width, height, depth;
  const char * mimetype;
  int ret = 0;
  
  node = bg_xml_find_node_child(icon_node, "width");
  if(!node || !(str = bg_xml_node_get_text_content(node)))
    return MAX_ERROR;
  width = atoi(str);
  
  node = bg_xml_find_node_child(icon_node, "height");
  if(!node || !(str = bg_xml_node_get_text_content(node)))
    return MAX_ERROR;
  height = atoi(str);

  node = bg_xml_find_node_child(icon_node, "depth");
  if(!node || !(str = bg_xml_node_get_text_content(node)))
    return MAX_ERROR;
  depth = atoi(str);
  
  node = bg_xml_find_node_child(icon_node, "depth");
  if(!node || !(mimetype = bg_xml_node_get_text_content(node)))
    return MAX_ERROR;

  ret += abs(width - size);
  ret += abs(height - size);
  ret += 10 * abs(depth - 32);

  if(strcmp(mimetype, "image/png"))
    {
    if(!strcmp(mimetype, "image/bmp") ||
       !strcmp(mimetype, "image/x-ms-bmp"))
      ret += 10;
    else if(!strcmp(mimetype, "image/jpeg"))
      ret += 20;
    else
      ret += 100;
    }
  return ret;
  }

char *
bg_upnp_device_description_get_icon_url(xmlNodePtr dev_node, int size, const char * url_base)
  {
  int icon_error;
  int icon_error_min = -1;
  xmlNodePtr node;

  xmlNodePtr icon_node = NULL;
  const char * icon_url_c = NULL;
  xmlNodePtr list_node = bg_xml_find_node_child(dev_node, "iconList");
  
  if(list_node)
    {
    while(1)
      {
      icon_node = bg_xml_find_next_node_child_by_name(list_node, icon_node, "icon");
      if(!icon_node)
        break;

      icon_error = check_icon(icon_node, size);

      if((icon_error_min == -1) || (icon_error < icon_error_min))
        {
        node = bg_xml_find_node_child(icon_node, "url");
        if(node && (icon_url_c = bg_xml_node_get_text_content(node)))
          icon_error_min = icon_error;
        }
      }
    }

  if(icon_url_c)
    return bg_upnp_device_description_make_url(icon_url_c, url_base);
  else
    return NULL;
  
  }

const char *
bg_upnp_device_description_get_label(xmlNodePtr dev_node)
  {
  xmlNodePtr node = bg_xml_find_node_child(dev_node, "friendlyName");
  if(!node)
    return NULL;
  return bg_xml_node_get_text_content(node);
  }

void 
bg_upnp_device_description_get_icon_urls(xmlNodePtr dev_node, gavl_array_t * ret, const char * url_base)
  {
  xmlNodePtr list_node;
  xmlNodePtr node;
  xmlNodePtr icon_node = NULL;

  if(!(list_node = bg_xml_find_node_child(dev_node, "iconList")))
    return;
  
  while((icon_node = bg_xml_find_next_node_child_by_name(list_node, icon_node, "icon")))
    {
    gavl_value_t val;
    gavl_dictionary_t * dict;

    gavl_value_init(&val);
    dict = gavl_value_set_dictionary(&val);

    if((node = bg_xml_find_node_child(icon_node, "url")))
      gavl_dictionary_set_string_nocopy(dict, GAVL_META_URI,
                                        bg_upnp_device_description_make_url(bg_xml_node_get_text_content(node),
                                                                            url_base));
    else
      goto fail;

    if((node = bg_xml_find_node_child(icon_node, "mimetype")))
      gavl_dictionary_set_string(dict, GAVL_META_MIMETYPE, bg_xml_node_get_text_content(node));
    else
      goto fail;
    
    if((node = bg_xml_find_node_child(icon_node, "width")))
      gavl_dictionary_set_int(dict, GAVL_META_WIDTH,
                              atoi(bg_xml_node_get_text_content(node)));
    else
      goto fail;

    if((node = bg_xml_find_node_child(icon_node, "height")))
      gavl_dictionary_set_int(dict, GAVL_META_HEIGHT,
                              atoi(bg_xml_node_get_text_content(node)));
    else
      goto fail;
    
    gavl_array_splice_val_nocopy(ret, -1, 0, &val);
    
    fail:

    gavl_value_free(&val);
    
    }
  
  
  
  }
