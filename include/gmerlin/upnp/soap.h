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

#ifndef __BG_UPNP_SOAP_H_
#define __BG_UPNP_SOAP_H_

#include <gmerlin/httpserver.h>

#include <gmerlin/xmlutils.h>

#if 0

xmlDocPtr bg_soap_create_request(const char * function, const char * service, int version);
// xmlDocPtr bg_soap_create_response(const char * function, const char * service, int version);

/* return the first named node within the Body element */


const char *
bg_soap_response_get_argument(xmlDocPtr doc, const char * name);


xmlDocPtr bg_soap_create_error(int code_i, const char * code_str);

/* Soap context (for clients) */

#if 1
typedef struct
  {
  gavl_dictionary_t req_hdr;
  gavl_dictionary_t res_hdr;
  
  xmlDocPtr req;
  xmlDocPtr res;

  xmlNodePtr res_node;
  
  char * host;
  int port;
  
  const char * function;
  } bg_soap_t;
#endif

int bg_soap_init(bg_soap_t * s, const char * url, const char * service,
                 int version, const char * function);

int bg_soap_init_request(gavl_dictionary_t * s, const char * url, const char * service,
                         int version, const char * function);



/* Write request and get answer */
int bg_soap_request(bg_soap_t * s);
void bg_soap_free(bg_soap_t * s);

#endif

/* New API */

#define BG_SOAP_META_FUNCTION  "function"  // String
#define BG_SOAP_META_SERVICE   "service"   // String
#define BG_SOAP_META_VERSION   "version"   // int


#define BG_SOAP_META_ARGS_IN   "args_in"   // Dictionary
#define BG_SOAP_META_ARGS_OUT  "args_out"  // Dictionary

#define BG_SOAP_META_REQ_HDR   "req_hdr"   // Dictionary
#define BG_SOAP_META_RES_HDR   "res_hdr"   // Dictionary

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
int bg_soap_request_init(gavl_dictionary_t * s, const char * control_uri,
                         const char * service, int version, const char * function);

int bg_soap_request_write_req(gavl_dictionary_t * s, int * fd);
int bg_soap_request_read_res(gavl_dictionary_t * s, int * fd);

int bg_soap_request(gavl_dictionary_t * s, int * fd);

#endif // __BG_UPNP_SOAP_H_
