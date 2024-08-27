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



#ifndef BG_UPNP_DEVICE_DESC_H_INCLUDED
#define BG_UPNP_DEVICE_DESC_H_INCLUDED

#include <uuid/uuid.h>
#include <gmerlin/xmlutils.h>
#include <gavl/gavlsocket.h>


// #include <gmerlin/upnp/device.h>


// #include <gmerlin/.h>

xmlDocPtr bg_upnp_device_description_create(const char * type, int version);

/*
  Required. Short description for end user. Should be localized (cf. ACCEPT-LANGUAGE and CONTENT-
  LANGUAGE headers). Specified by UPnP vendor. String. Should be < 64 characters.
*/

void bg_upnp_device_description_set_name(xmlDocPtr ptr, const char * name);

/*
  Required. Manufacturer's name. May be localized (cf. ACCEPT-LANGUAGE and CONTENT-LANGUAGE headers).
  Specified by UPnP vendor. String. Should be < 64 characters.
*/

void bg_upnp_device_description_set_manufacturer(xmlDocPtr ptr, const char * name);

/*
  Optional. Web site for Manufacturer. May be localized (cf. ACCEPT-LANGUAGE and CONTENT-LANGUAGE
  headers). May be relative to base URL. Specified by UPnP vendor. Single URL.
*/

void bg_upnp_device_description_set_manufacturer_url(xmlDocPtr ptr, const char * name);

/*
  Recommended. Long description for end user. Should be localized (cf. ACCEPT-LANGUAGE and CONTENT-
  LANGUAGE headers). Specified by UPnP vendor. String. Should be < 128 characters.
 */

void bg_upnp_device_description_set_model_description(xmlDocPtr ptr, const char * name);

/*
  Required. Model name. May be localized (cf. ACCEPT-LANGUAGE and CONTENT-LANGUAGE headers).
  Specified by UPnP vendor. String. Should be < 32 characters.
 */

void bg_upnp_device_description_set_model_name(xmlDocPtr ptr, const char * name);

/*
  Recommended. Model number. May be localized (cf. ACCEPT-LANGUAGE and CONTENT-LANGUAGE headers).
  Specified by UPnP vendor. String. Should be < 32 characters.
 */

void bg_upnp_device_description_set_model_number(xmlDocPtr ptr, const char * name);

/*
  Optional. Web site for model. May be localized (cf. ACCEPT-LANGUAGE and CONTENT-LANGUAGE headers).
  May be relative to base URL. Specified by UPnP vendor. Single URL.
*/

void bg_upnp_device_description_set_model_url(xmlDocPtr ptr, const char * name);

/*
  Recommended. Serial number. May be localized (cf. ACCEPT-LANGUAGE and CONTENT-LANGUAGE headers).
  Specified by UPnP vendor. String. Should be < 64 characters.
*/

void bg_upnp_device_description_set_serial_number(xmlDocPtr ptr, const char * name);

/*
  Required. Unique Device Name. Universally-unique identifier for the device, whether root or
  embedded. Must be the same over time for a specific device instance (i.e., must survive reboots).
  Must match the value of the NT header in device discovery messages. Must match the prefix of the
  USN header in all discovery messages. (The section on Discovery explains the NT and USN headers.)
  Must begin with uuid: followed by a UUID suffix
  specified by a UPnP vendor. Single URI.
 */

void bg_upnp_device_description_set_uuid(xmlDocPtr ptr, uuid_t uuid);

/*
  Optional. Universal Product Code. 12-digit, all-numeric code that identifies the consumer
  package. Managed by the Uniform Code Council. Specified by UPnP vendor. Single UPC.
 */

void bg_upnp_device_description_set_upc(xmlDocPtr ptr, const char * name);

void bg_upnp_device_description_add_icon(xmlDocPtr ptr,
                                         const char * mimetype,
                                         int width, int height, int depth, const char * url);




char *
bg_upnp_device_description_get_url_base(const char * desc_url,
                                        xmlDocPtr doc);

char *
bg_upnp_device_description_make_url(const char * url, const char * base);

xmlNodePtr
bg_upnp_device_description_get_device_node(xmlDocPtr doc,
                                           const char * device,
                                           int version);

xmlNodePtr
bg_upnp_device_description_get_service_node(xmlNodePtr node,
                                            const char * service,
                                            int version);

int
bg_upnp_device_description_is_gmerlin(xmlNodePtr node);

char *
bg_upnp_device_description_get_icon_url(xmlNodePtr dev_node, int size, const char * url_base);

void 
bg_upnp_device_description_get_icon_urls(xmlNodePtr dev_node, gavl_array_t * ret, const char * url_base);

const char *
bg_upnp_device_description_get_label(xmlNodePtr dev_node);


char *
bg_upnp_device_description_get_control_url(xmlNodePtr service_node,
                                           const char * url_base);




char *
bg_upnp_device_description_get_event_url(xmlNodePtr service_node,
                                         const char * url_base);

char *
bg_upnp_device_description_get_service_description(xmlNodePtr service_node,
                                                   const char * url_base);

xmlNodePtr
bg_upnp_service_description_get_action(xmlDocPtr desc, const char * action);

xmlNodePtr
bg_upnp_service_description_get_state_variable(xmlDocPtr desc, const char * var);

int bg_upnp_service_description_get_variable_range(xmlNodePtr node,
                                                   int * min, int * max, int * step);

int bg_upnp_service_description_value_allowed(xmlNodePtr node, const char * value);

char * bg_upnp_device_description_get_url_base(const char * desc_url, xmlDocPtr doc);

xmlNodePtr bg_upnp_device_description_get_device_node(xmlDocPtr doc, const char * device, int version);

const char *
bg_upnp_device_description_get_label(xmlNodePtr dev_node);

void 
bg_upnp_device_description_get_icon_urls(xmlNodePtr dev_node, gavl_array_t * ret, const char * url_base);

void bg_upnp_device_get_info(gavl_dictionary_t * dev, const char * uri_base, xmlNodePtr dev_node);


int bg_upnp_device_get_node_info(gavl_dictionary_t * dev, const char * device, int version);


#endif // BG_UPNP_DEVICE_DESC_H_INCLUDED

