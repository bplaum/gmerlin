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



#ifndef BG_UPNP_H_INCLUDED
#define BG_UPNP_H_INCLUDED


xmlDocPtr bg_didl_create(void);

// #if 0
xmlNodePtr bg_didl_add_item(xmlDocPtr doc);

xmlNodePtr bg_didl_add_container(xmlDocPtr doc);

xmlNodePtr bg_didl_add_element(xmlDocPtr doc,
                               xmlNodePtr node,
                               const char * name,
                               const char * value);
#if 0

void bg_didl_set_class(xmlDocPtr doc,
                       xmlNodePtr node,
                       const char * klass);

void bg_didl_set_title(xmlDocPtr doc,
                       xmlNodePtr node,
                       const char * title);
#endif

int bg_didl_filter_element(const char * name, char ** filter);

int bg_didl_filter_attribute(const char * element, const char * attribute, char ** filter);

#if 0
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
#endif


void bg_track_from_didl(gavl_dictionary_t * track, xmlNodePtr didl);

xmlNodePtr bg_track_to_didl(xmlDocPtr ret, const gavl_dictionary_t * track, char ** filter);

char ** bg_didl_create_filter(const char * Filter);


// char * bg_didl_get_location(xmlNodePtr didl, gavl_time_t * duration);

#endif // BG_UPNP_H_INCLUDED
