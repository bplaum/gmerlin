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

//#include <upnp_service.h>
#include <upnp/devicepriv.h>

#include <gmerlin/utils.h>
#include <gmerlin/upnp/upnputils.h>
#include <string.h>

int
bg_upnp_service_start(bg_upnp_service_t * s)
  {
  int i;
  
  /* Get the related state variables of all action arguments */
  if(!bg_upnp_service_desc_resolve_refs(&s->desc))
    return 0;
  
  /* Create XML Description */
  s->description = bg_upnp_service_desc_2_xml(&s->desc);

  /* Get the maximum number of input- and output args so we can
     allocate our request structure */

  for(i = 0; i < s->desc.num_sa; i++)
    {
    if(s->req.args_in_alloc < s->desc.sa[i].num_args_in)
      s->req.args_in_alloc = s->desc.sa[i].num_args_in;
    
    if(s->req.args_out_alloc < s->desc.sa[i].num_args_out)
      s->req.args_out_alloc = s->desc.sa[i].num_args_out;
    }
  
  s->req.args_in  = calloc(s->req.args_in_alloc, sizeof(*s->req.args_in));
  s->req.args_out = calloc(s->req.args_out_alloc, sizeof(*s->req.args_out));
  
  return 1;
  }

void bg_upnp_service_init(bg_upnp_service_t * ret,
                          const char * name, const char * type, int version)
  {
  ret->name = gavl_strdup(name);
  ret->type = gavl_strdup(type);
  ret->version = version;
  }

int
bg_upnp_service_handle_request(bg_upnp_service_t * s, 
                               bg_http_connection_t * conn)
  {
  if(!strcmp(conn->path, "desc.xml"))
    {
    /* Send service description */
    bg_upnp_send_description(conn, s->description);
    return 1;
    }
  else if(!strcmp(conn->path, "control"))
    {
    /* Service control */
    bg_upnp_service_handle_action_request(s, conn);
    return 1;
    }
  else if(!strcmp(conn->path, "event"))
    {
    /* Service events */
    bg_upnp_service_handle_event_request(s, conn);
    return 1;
    }
  return 0; // 404
  }

void bg_upnp_service_free(bg_upnp_service_t * s)
  {
  int i;
  if(s->name)
    free(s->name);
  if(s->type)
    free(s->type);
  bg_upnp_service_desc_free(&s->desc);
  if(s->description)
    free(s->description);

  for(i = 0; i < s->num_es; i++)
    {
    if(s->es[i].url)
      free(s->es[i].url);
    }

  if(s->es)
    free(s->es);

  if(s->req.args_in)
    free(s->req.args_in);
  if(s->req.args_out)
    free(s->req.args_out);
  }
