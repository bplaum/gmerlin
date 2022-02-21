/*****************************************************************
 * Gmerlin - a general purpose multimedia framework and applications
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



#include <sys/utsname.h>
#include <string.h>

#include <config.h>


#include <gavl/gavl.h>
#include <gavl/utils.h>
#include <gavl/metatags.h>

#include <gmerlin/upnp/devicedesc.h>
#include <gmerlin/upnp/upnputils.h>
#include <gmerlin/upnp/soap.h>
#include <gmerlin/mdb.h>
#include <gmerlin/utils.h>

char * bg_upnp_id_from_upnp(const char * id)
  {
  if(!strcmp(id, "0"))
    return gavl_strdup("/");
  else
    return gavl_strdup(id);
  }

char * bg_upnp_id_to_upnp(const char * id)
  {
  if(!strcmp(id, "/"))
    return gavl_strdup("0");
  else
    return gavl_strdup(id);
  }

char * bg_upnp_parent_id_to_upnp(const char * id)
  {
  char * ret;
  
  if(!strcmp(id, "/"))
    return gavl_strdup("-1");
  
  ret = bg_mdb_get_parent_id(id);
  if(!strcmp(ret, "/"))
    {
    free(ret);
    return gavl_strdup("0");
    }
  return ret;
  }

static char * upnp_server_string = NULL;

static char * make_server_string(void)
  {
  struct utsname os_info;
  uname(&os_info);
  return bg_sprintf("%s/%s, UPnP/1.0, "PACKAGE"/"VERSION,
                    os_info.sysname, os_info.release);
  }

void bg_upnp_init_server_string()
  {
  upnp_server_string = make_server_string();
  }

const char * bg_upnp_get_server_string()
  {
  return upnp_server_string;
  }

void bg_upnp_free_server_string()
  {
  free(upnp_server_string);
  }

void bg_upnp_send_description(bg_http_connection_t * conn,
                              const char * desc1)
  {
  int len;

  len = strlen(desc1);
  bg_http_connection_init_res(conn, "HTTP/1.1", 200, "OK");
  gavl_dictionary_set_int(&conn->res, "CONTENT-LENGTH", len);
  gavl_dictionary_set_string(&conn->res, "CONTENT-TYPE", "text/xml; charset=UTF-8");
  gavl_dictionary_set_string(&conn->res, "SERVER", bg_upnp_get_server_string());

  bg_http_connection_check_keepalive(conn);
  //  gavl_dictionary_set_string(&res, "CONNECTION", "close");

#if 0  
  fprintf(stderr, "Send description\n");
  gavl_dictionary_dump(&res, 2);
  fprintf(stderr, "%s\n", desc);
#endif
  
  if(!bg_http_connection_write_res(conn))
    return;

  if(strcmp(conn->method, "HEAD"))
    gavl_socket_write_data(conn->fd, (const uint8_t*)desc1, len);
  }

void bg_upnp_finish_soap_request(gavl_dictionary_t * soap,
                                 bg_http_connection_t * conn,
                                 bg_http_server_t *srv)
  {
  bg_soap_request_write_res(soap, conn);
  bg_http_server_put_connection(srv, conn);
  bg_http_connection_free(conn);
  bg_http_connection_init(conn);
  }

char * bg_upnp_create_icon_list(const gavl_array_t * arr)
  {
  int i;
  char * ret = NULL;
  const gavl_dictionary_t * icon;
  char * tmp_string;
  int w, h, depth;
  const char * uri;
  
  const char * mimetype;
  
  if(!arr || !arr->num_entries)
    return NULL;
  
  for(i = 0; i < arr->num_entries; i++)
    {
    if((icon = gavl_value_get_dictionary(&arr->entries[i])) &&
       (uri = gavl_dictionary_get_string(icon, GAVL_META_URI)) &&
       gavl_dictionary_get_int(icon, GAVL_META_WIDTH, &w) &&
       gavl_dictionary_get_int(icon, GAVL_META_HEIGHT, &h) &&
       (mimetype = gavl_dictionary_get_string(icon, GAVL_META_MIMETYPE)))
      {
      if(!strcmp(mimetype, "image/png"))
        depth = 32;
      else
        depth = 24;

      
      tmp_string = bg_sprintf("      <icon>"
                              "<mimetype>%s</mimetype>"
                              "<width>%d</width>"
                              "<height>%d</height>"
                              "<depth>%d</depth>"
                              "<url>%s</url></icon>\n", mimetype, w, h, depth, uri);
      
      if(!ret)
        ret = gavl_strdup("    <iconList>\n");

      ret = gavl_strcat(ret, tmp_string);
      free(tmp_string);
      }
    }

  if(ret)
    ret = gavl_strcat(ret, "    </iconList>\n");
  
  return ret;
  }

int bg_upnp_parse_bool(const char * str)
  {
  if(str && (!strcmp(str, "1") || !strcasecmp(str, "yes") || !strcasecmp(str, "true")))
    return 1;
  else
    return 0;
  }
