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

#include <config.h>
#include <upnp/devicepriv.h>
#include <string.h>

#include <gmerlin/upnp/soap.h>
#include <gmerlin/upnp/upnputils.h>

#include <gmerlin/utils.h>
#include <gmerlin/http.h>
#include <gmerlin/log.h>
#define LOG_DOMAIN "upnp.action"

// #define DUMP_SOAP

static bg_upnp_soap_arg_t *
get_in_arg_by_id(bg_upnp_soap_request_t * req,
                 int id)
  {
  int i;
  for(i = 0; i < req->num_args_in; i++)
    {
    if(req->args_in[i].desc->id == id)
      return &req->args_in[i];
    }
  gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "No input argument for ID %d", id);
  return NULL;
  }

int
bg_upnp_service_get_arg_in_int(bg_upnp_soap_request_t * req,
                               int id)
  {
  const bg_upnp_soap_arg_t * arg = get_in_arg_by_id(req, id);
  if(arg)
    return arg->val.i;
  return 0;
  }

unsigned int
bg_upnp_service_get_arg_in_uint(bg_upnp_soap_request_t * req,
                                int id)
  {
  const bg_upnp_soap_arg_t * arg = get_in_arg_by_id(req, id);
  if(arg)
    return arg->val.ui;
  return 0;
  }

const char *
bg_upnp_service_get_arg_in_string(bg_upnp_soap_request_t * req,
                                  int id)
  {
  const bg_upnp_soap_arg_t * arg = get_in_arg_by_id(req, id);
  if(arg)
    return arg->val.s;
  return NULL;
  }

void
bg_upnp_service_get_arg_in(bg_upnp_soap_request_t * req, int id)
  {
  bg_upnp_soap_arg_t * arg = get_in_arg_by_id(req, id);
  
  if(!arg)
    return;
  bg_upnp_sv_set(arg->desc->rsv, &arg->val);
  }


static bg_upnp_soap_arg_t *
get_out_arg_by_id(bg_upnp_soap_request_t * req,
                 int id)
  {
  int i;
  for(i = 0; i < req->num_args_out; i++)
    {
    if(req->args_out[i].desc->id == id)
      return &req->args_out[i];
    }
  gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "No output argument for ID %d", id);
  return NULL;
  }


void
bg_upnp_service_set_arg_out_int(bg_upnp_soap_request_t * req,
                                int id, int val)
  {
  bg_upnp_soap_arg_t * arg = get_out_arg_by_id(req, id);
  if(arg)
    arg->val.i = val;
  }

void
bg_upnp_service_set_arg_out_uint(bg_upnp_soap_request_t * req,
                                 int id, unsigned int val)
  {
  bg_upnp_soap_arg_t * arg = get_out_arg_by_id(req, id);
  if(arg)
    arg->val.ui = val;
  }

void
bg_upnp_service_set_arg_out_string(bg_upnp_soap_request_t * req,
                                   int id, char * val)
  {
  bg_upnp_soap_arg_t * arg = get_out_arg_by_id(req, id);
  if(arg)
    arg->val.s = val;
  }

void
bg_upnp_service_set_arg_out(bg_upnp_soap_request_t * req, int id)
  {
  bg_upnp_soap_arg_t * arg = get_out_arg_by_id(req, id);
  
  if(!arg)
    return;
  
  bg_upnp_sv_val_copy(arg->desc->rsv->type,
                      &arg->val,
                      &arg->desc->rsv->value);
  }

#define TIMEOUT 500

static void cleanup_string(uint8_t * data,  int *len1)
  {
  int i = 0;
  int len = *len1;
  
  while(i < len)
    {
    if((data[i] < 0x20) &&
       (data[i] != '\r') &&
       (data[i] != '\n') &&
       (data[i] != '\0'))
      {
      if(i < len - 1)
        memmove(data + i, data + i + 1, len - 1 - i);
      len--;
      }
    else
      i++;
    }
  *len1 = len;
  }

int
bg_upnp_service_handle_action_request(bg_upnp_service_t * s, 
                                      bg_http_connection_t * conn)
  {
  int content_length = -1;
  char * buf = NULL;
  int ret = 0;
  int len;
  
  if(!strcmp(conn->method, "POST"))
    {
    if(!gavl_dictionary_get_int_i(&conn->req , "CONTENT-LENGTH", &content_length))
      {
      /* Error */
      goto fail;
      }

    buf = malloc(content_length + 1);
    if(bg_socket_read_data(conn->fd, (uint8_t*)buf,
                           content_length, TIMEOUT) < content_length)
      {
      /* Error */
      goto fail;
      }
    buf[content_length] = '\0';

    len = content_length + 1;
    cleanup_string((uint8_t*)buf, &len);
    content_length = len - 1;
    
#ifdef DUMP_SOAP
    fprintf(stderr, "Got SOAP request\n");
    gavl_dictionary_dump(&conn->req, 0);
    fprintf(stderr, "%s\n", buf);
    //    gavl_hexdump((uint8_t*)buf, content_length, 16);
#endif
    if(!bg_upnp_soap_request_from_xml(s, buf, content_length, &conn->req))
      {
      /* Error */
      goto fail;
      }
    
    if(!s->req.error_int &&
       !s->req.action->func(s))
      {
      /* Error */
      goto fail;
      }
    
    free(buf);
    buf = bg_upnp_soap_response_to_xml(s, &content_length);

    if(!s->req.error_int)
      {
      /* Everything ok */
      bg_http_connection_init_res(conn, "HTTP/1.1", 200, "OK");
      }
    else
      {
      bg_http_connection_init_res(conn, "HTTP/1.1", 500, "Internal Server Error");
      }

    gavl_dictionary_set_int(&conn->res, "CONTENT-LENGTH", content_length);
    gavl_dictionary_set_string(&conn->res, "CONTENT-TYPE", "text/xml; charset=\"utf-8\"");
    
    bg_http_connection_check_keepalive(conn);
    bg_http_header_set_date(&conn->res, "DATE");
    bg_http_header_set_empty_var(&conn->res, "EXT");
    gavl_dictionary_set_string(&conn->res, "SERVER", bg_upnp_get_server_string());
    

#ifdef DUMP_SOAP
    fprintf(stderr, "Sending SOAP response\n");
    gavl_dictionary_dump(&conn->res, 0);
    fprintf(stderr, "%s\n", buf);
#endif
    if(bg_http_connection_write_res(conn))
      bg_socket_write_data(conn->fd, (uint8_t*)buf, content_length);
    }
  else
    {
    
    }

  ret = 1;
  
  fail:
  
  bg_upnp_soap_request_cleanup(&s->req);
  
  if(buf)
    free(buf);
  
  return ret;
  }
