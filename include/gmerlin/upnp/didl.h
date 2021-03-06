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

#ifndef BG_UPNP_H_INCLUDED
#define BG_UPNP_H_INCLUDED

const char * bg_didl_get_dlna_image_profile(const char * mimetype, int width, int height);

char * bg_get_dlna_content_features(const gavl_dictionary_t * track, const gavl_dictionary_t * uri,
                                    int can_seek_http, int can_seek_dlna);

xmlDocPtr bg_didl_create(void);

xmlNodePtr bg_didl_add_item(xmlDocPtr doc);

xmlNodePtr bg_didl_add_container(xmlDocPtr doc);

xmlNodePtr bg_didl_add_element(xmlDocPtr doc,
                               xmlNodePtr node,
                               const char * name,
                               const char * value);

void bg_didl_set_class(xmlDocPtr doc,
                       xmlNodePtr node,
                       const char * klass);

void bg_didl_set_title(xmlDocPtr doc,
                       xmlNodePtr node,
                       const char * title);

int bg_didl_filter_element(const char * name, char ** filter);

int bg_didl_filter_attribute(const char * element, const char * attribute, char ** filter);

xmlNodePtr bg_didl_add_element_string(xmlDocPtr doc,
                                      xmlNodePtr node,
                                      const char * name,
                                      const char * content, char ** filter);

xmlNodePtr bg_didl_add_element_int(xmlDocPtr doc,
                                   xmlNodePtr node,
                                   const char * name,
                                   int64_t content, char ** filter);

/* Filtering must be done by the caller!! */
void bg_didl_set_attribute_int(xmlNodePtr node, const char * name, int64_t val);


void bg_metadata_from_didl(gavl_dictionary_t * m, xmlNodePtr didl);

xmlNodePtr bg_track_to_didl(xmlDocPtr ret, const gavl_dictionary_t * track, char ** filter);

char ** bg_didl_create_filter(const char * Filter);


// char * bg_didl_get_location(xmlNodePtr didl, gavl_time_t * duration);

#endif // BG_UPNP_H_INCLUDED
