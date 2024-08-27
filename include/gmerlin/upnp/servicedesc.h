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



#ifndef BG_UPNP_SERVICEDESC_H_INCLUDED
#define BG_UPNP_SERVICEDESC_H_INCLUDED

#include <gmerlin/xmlutils.h>

xmlDocPtr bg_upnp_service_description_create();
xmlNodePtr bg_upnp_service_description_add_action(xmlDocPtr doc, const char * name);

void bg_upnp_service_action_add_argument(xmlNodePtr node,
                                         const char * name, int out, int retval,
                                         const char * related_statevar);

xmlNodePtr
bg_upnp_service_description_add_statevar(xmlDocPtr doc,
                                         const char * name,
                                         int events,
                                         char * data_type);

void
bg_upnp_service_statevar_add_allowed_value(xmlNodePtr node,
                                           const char * name);

#endif // BG_UPNP_SERVICEDESC_H_INCLUDED
