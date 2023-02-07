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

#ifndef BG_UPNP_SOAP_H_INCLUDED
#define BG_UPNP_SOAP_H_INCLUDED

#include <gmerlin/httpserver.h>

#include <gmerlin/xmlutils.h>

#define BG_SOAP_META_FUNCTION  "function"  // String
#define BG_SOAP_META_SERVICE   "service"   // String
#define BG_SOAP_META_VERSION   "version"   // int


#define BG_SOAP_META_ARGS_IN   "args_in"   // Dictionary
#define BG_SOAP_META_ARGS_OUT  "args_out"  // Dictionary

#define BG_SOAP_META_REQ_VARS   "req_vars"   // Dictionary
#define BG_SOAP_META_RES_HDR    "res_hdr"   // Dictionary

#define BG_SOAP_ARG_EMPTY      "*NULL*"    // Empty string

#define BG_SOAP_ERROR_CODE     "ErrorCode"
#define BG_SOAP_ERROR_STRING   "ErrorString"

/* For clients */
#define BG_SOAP_META_HOSTNAME  "hostname"  // string
#define BG_SOAP_META_PORT      "port"      // int

xmlNodePtr bg_soap_get_function(xmlDocPtr);
xmlNodePtr bg_soap_request_next_argument(xmlNodePtr function, xmlNodePtr arg);

void bg_soap_request_set_error(gavl_dictionary_t * s, int error_code, const char * error_string);


xmlNodePtr bg_soap_request_add_argument(xmlDocPtr doc, const char * name,
                                        const char * value);


/* Server side */
int bg_soap_request_read_req(gavl_dictionary_t * s, bg_http_connection_t * conn);
int bg_soap_request_write_res(gavl_dictionary_t * s, bg_http_connection_t * conn);

/* Client side */
typedef struct
  {
  gavl_dictionary_t * s;
  gavf_io_t * io; // http client
  gavl_buffer_t req_buf;
  gavl_buffer_t res_buf;
  } bg_soap_client_request_t;

int bg_soap_request_init(gavl_dictionary_t * s, const char * control_uri,
                         const char * service, int version, const char * function);

// int bg_soap_request_write_req(gavl_dictionary_t * s, int * fd);
// int bg_soap_request_read_res(gavl_dictionary_t * s, int * fd);

int bg_soap_request(gavl_dictionary_t * s, gavf_io_t ** io);

/* Start an asynchronous request. io MUST be an gavl http client. Check for
   completion with gavl_http_client_run_async_done() */
int bg_soap_request_start_async(gavl_dictionary_t * s, gavf_io_t * io);


#endif // BG_UPNP_SOAP_H_INCLUDED

