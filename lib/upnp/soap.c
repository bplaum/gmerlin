#include <string.h>
#include <unistd.h>

#include <config.h>

#include <gmerlin/translation.h>
#include <gmerlin/log.h>

#include <gmerlin/upnp/soap.h>
#include <gmerlin/upnp/upnputils.h>
#include <gmerlin/utils.h>
#include <gmerlin/http.h>

#define LOG_DOMAIN "soap"

// #define DUMP_SOAP_CLIENT

/* NEW API */

xmlNodePtr bg_soap_request_add_argument(xmlDocPtr doc, const char * name,
                                        const char * value)
  {
  xmlNodePtr ret;
  xmlNodePtr node = bg_soap_get_function(doc);
  ret = bg_xml_append_child_node(node, name, value);
  xmlSetNs(ret, NULL);
  return ret;
  }

static xmlNodePtr get_body(xmlDocPtr doc)
  {
  xmlNodePtr node;

  if(!(node = bg_xml_find_doc_child(doc, "Envelope")) ||
     !(node = bg_xml_find_node_child(node, "Body")))
    return NULL;
  return node;  
  }

static xmlDocPtr soap_create_common()
  {
  xmlNsPtr soap_ns;
  xmlDocPtr  xml_doc;
  xmlNodePtr xml_env;
  
  xml_doc = xmlNewDoc((xmlChar*)"1.0");
  xml_env = xmlNewDocRawNode(xml_doc, NULL, (xmlChar*)"Envelope", NULL);
  xmlDocSetRootElement(xml_doc, xml_env);
  soap_ns =
    xmlNewNs(xml_env,
             (xmlChar*)"http://schemas.xmlsoap.org/soap/envelope/",
             (xmlChar*)"s");
  xmlSetNs(xml_env, soap_ns);

  xmlSetNsProp(xml_env, soap_ns, (xmlChar*)"encodingStyle", 
               (xmlChar*)"http://schemas.xmlsoap.org/soap/encoding/");

  xmlNewChild(xml_env, soap_ns, (xmlChar*)"Body", NULL);
  return xml_doc;
  }

static xmlDocPtr bg_soap_create_error(int code_i, const char * code_str)
  {
  char buf[64];
  
  xmlNsPtr error_ns;

  xmlDocPtr  xml_doc;
  xmlNodePtr xml_body;
  xmlNodePtr xml_fault;
  
  xmlNodePtr node;
  
  xml_doc = soap_create_common();
  xml_body = get_body(xml_doc);

  xml_fault = xmlNewChild(xml_body, xml_body->ns, (const xmlChar*)"Fault", NULL);

  node = xmlNewChild(xml_fault, NULL, (const xmlChar*)"faultcode", (const xmlChar*)"s:Client");
  xmlSetNs(node, NULL);
  node = xmlNewChild(xml_fault, NULL, (const xmlChar*)"faultstring", (const xmlChar*)"UPnPError");
  xmlSetNs(node, NULL);
  node = xmlNewChild(xml_fault, NULL, (const xmlChar*)"detail", NULL);
  xmlSetNs(node, NULL);
  node = xmlNewChild(node, NULL, (const xmlChar*)"UPnPError", NULL);
  
  error_ns = xmlNewNs(node,
                      (const xmlChar*)"urn:schemas-upnp-org:control-1-0",
                      NULL);

  xmlSetNs(node, error_ns);

  snprintf(buf, 64, "%d", code_i);

  xmlNewChild(node, NULL, (const xmlChar*)"errorCode", (const xmlChar*)buf);
  xmlNewChild(node, NULL, (const xmlChar*)"errorDescription", (const xmlChar*)code_str);

  return xml_doc;
  }


static xmlDocPtr soap_create(const char * function, const char * service,
                             int version, int response)
  {
  char * tmp_string;
  xmlDocPtr  xml_doc;
  xmlNodePtr xml_body;
  xmlNodePtr xml_action;

  xmlNsPtr upnp_ns;

  xml_doc = soap_create_common();
  xml_body = get_body(xml_doc);
    
  if(response)
    {
    tmp_string = bg_sprintf("%sResponse", function);
    xml_action = xmlNewChild(xml_body, NULL, (xmlChar*)tmp_string, NULL);
    free(tmp_string);
    }
  else
    xml_action = xmlNewChild(xml_body, NULL, (xmlChar*)function, NULL);

  tmp_string = bg_sprintf("urn:schemas-upnp-org:service:%s:%d",
                          service, version);

  upnp_ns = xmlNewNs(xml_action, (xmlChar*)tmp_string,  (xmlChar*)"u");
  free(tmp_string);

  xmlSetNs(xml_action, upnp_ns);

  return xml_doc;
  }


xmlNodePtr bg_soap_request_next_argument(xmlNodePtr function, xmlNodePtr arg)
  {
  return bg_xml_find_next_node_child(function, arg);
  }

xmlNodePtr bg_soap_get_function(xmlDocPtr doc)
  {
  xmlNodePtr node;

  if(!(node = bg_xml_find_doc_child(doc, "Envelope")) ||
     !(node = bg_xml_find_node_child(node, "Body")) ||
     !(node = bg_xml_find_next_node_child(node, NULL)))
    return NULL;
  return node;  
  }

int bg_soap_request_read_req(gavl_dictionary_t * s, bg_http_connection_t * conn)
  {
  xmlDocPtr doc;
  xmlNodePtr function;
  xmlNodePtr arg;
  gavl_dictionary_t * args_in;
  gavl_buffer_t buf;
  const char * soapaction;
  const char * pos;
  gavl_io_t * io = gavl_io_create_socket(conn->fd, 3000, 0);

  gavl_buffer_init(&buf);
  
  if(!gavl_http_read_body(io, &conn->req, &buf))
    {
    gavl_io_destroy(io);
    return 0;
    }
  
  gavl_io_destroy(io);
  
#ifdef DUMP_SOAP
  fprintf(stderr, "Got SOAP Request:\n");
  gavl_dictionary_dump(&conn->req, 2);
  
  fprintf(stderr, "\n%s\n", (char*)buf.buf);
#endif
  
  doc = xmlParseMemory((char*)buf.buf, buf.len);
  if(!doc || !(function = bg_soap_get_function(doc)))
    {
    /* Error */
    if(doc)
      xmlFreeDoc(doc);
    return 0;
    }

  arg = NULL;

  //  fprintf(stderr, "Got SOAP Function: %s\n", function->name);

  gavl_dictionary_set_string(s, BG_SOAP_META_FUNCTION, (char*)function->name);
  args_in = gavl_dictionary_get_dictionary_create(s, BG_SOAP_META_ARGS_IN);

  gavl_dictionary_get_dictionary_create(s, BG_SOAP_META_ARGS_OUT);
  
  while((arg = bg_soap_request_next_argument(function, arg)))
    {
    const char * str = bg_xml_node_get_text_content(arg);

    if(*str == '\0')
      gavl_dictionary_set_string(args_in, (char*)arg->name, BG_SOAP_ARG_EMPTY);
    else
      gavl_dictionary_set_string(args_in, (char*)arg->name, str);
    }
  /* Get service and version */
  if((soapaction = gavl_dictionary_get_string_i(&conn->req, "SOAPACTION")) &&
     (pos = strstr(soapaction, "urn:schemas-upnp-org:service:")))
    {
    soapaction = pos + 29;

    if((pos = strchr(soapaction, ':')))
      {
      gavl_dictionary_set_string_nocopy(s, BG_SOAP_META_SERVICE, gavl_strndup(soapaction, pos));
      pos++;
      gavl_dictionary_set_int(s, BG_SOAP_META_VERSION, atoi(pos));
      }
    
    }

  if(doc)
    xmlFreeDoc(doc);
  
  gavl_buffer_free(&buf);
  return 1;
  }

static void write_arg_foreach(void * priv, const char * name, const gavl_value_t * val)
  {
  const char * str;
  if((str = gavl_value_get_string(val)))
    {
    if(!strcmp(str, BG_SOAP_ARG_EMPTY))
      bg_soap_request_add_argument(priv, name, "");
    else
      bg_soap_request_add_argument(priv, name, str);
    }
  }

void bg_soap_request_set_error(gavl_dictionary_t * s,
                               int error_code, const char * error_string)
  {
  gavl_dictionary_set_int(s, BG_SOAP_ERROR_CODE, error_code);
  gavl_dictionary_set_string(s, BG_SOAP_ERROR_STRING, error_string);
  }

int bg_soap_request_write_res(gavl_dictionary_t * s, bg_http_connection_t * conn)
  {
  xmlDocPtr res = NULL;
  const char * function;
  const char * service;
  int version = 0;
  char * res_xml;
  int len;
  int error_code = 0;
  const char * error_string;
  
  function  = gavl_dictionary_get_string(s, BG_SOAP_META_FUNCTION);
  service   = gavl_dictionary_get_string(s, BG_SOAP_META_SERVICE);
  gavl_dictionary_get_int(s, BG_SOAP_META_VERSION, &version);

  if(gavl_dictionary_get_int(s, BG_SOAP_ERROR_CODE, &error_code) && error_code &&
     (error_string = gavl_dictionary_get_string(s, BG_SOAP_ERROR_STRING)))
    {
    res = bg_soap_create_error(error_code, error_string);
    }
  else
    {
    res = soap_create(function, service, version, 1);
    gavl_dictionary_foreach(gavl_dictionary_get_dictionary(s, BG_SOAP_META_ARGS_OUT),
                            write_arg_foreach, res);
    }
  
  res_xml = bg_xml_save_to_memory(res);
  len = strlen(res_xml);

  if(error_code)
    bg_http_connection_init_res(conn, "HTTP/1.1", 500, "Internal Server Error");
  else
    bg_http_connection_init_res(conn, "HTTP/1.1", 200, "OK");

  gavl_dictionary_set_int(&conn->res, "CONTENT-LENGTH", len);
  gavl_dictionary_set_string(&conn->res, "CONTENT-TYPE", "text/xml; charset=\"utf-8\"");
  
  bg_http_connection_check_keepalive(conn);
  bg_http_header_set_date(&conn->res, "DATE");
  bg_http_header_set_empty_var(&conn->res, "EXT");
  gavl_dictionary_set_string_nocopy(&conn->res, "SERVER", bg_upnp_make_server_string());

#ifdef DUMP_SOAP
  fprintf(stderr, "Sending SOAP response\n");
  gavl_dictionary_dump(&conn->res, 0);
  fprintf(stderr, "%s\n", res_xml);
#endif
  if(bg_http_connection_write_res(conn))
    gavl_socket_write_data(conn->fd, (uint8_t*)res_xml, len);
  
  if(res)
    xmlFreeDoc(res);

  if(res_xml)
    free(res_xml);
  
  return 0;
  }

int bg_soap_request_init(gavl_dictionary_t * s, const char * control_uri,
                         const char * service, int version, const char * function)
  {
  int ret = 0;
  char * tmp_string;
  char * path = NULL;
  char * host = NULL;
  int port = -1;
  char * request_host = NULL;

  gavl_dictionary_t * req_vars;
  gavl_dictionary_init(s);
  
  gavl_dictionary_set_string(s, BG_SOAP_META_FUNCTION, function);
  gavl_dictionary_set_string(s, BG_SOAP_META_SERVICE,  service);
  gavl_dictionary_set_int(s, BG_SOAP_META_VERSION,     version);
  
  /* */

  gavl_dictionary_get_dictionary_create(s, BG_SOAP_META_ARGS_IN);
  gavl_dictionary_get_dictionary_create(s, BG_SOAP_META_ARGS_OUT);
  gavl_dictionary_get_dictionary_create(s, BG_SOAP_META_RES_HDR);

  gavl_dictionary_set_string(s, GAVL_META_URI, control_uri);
  
  req_vars = gavl_dictionary_get_dictionary_create(s, BG_SOAP_META_REQ_VARS);
  
  if(!gavl_url_split(control_uri, NULL, NULL, NULL, &host, &port, &path))
    goto fail;

  gavl_dictionary_set_string_nocopy(s, BG_SOAP_META_HOSTNAME, host);
  gavl_dictionary_set_int(s, BG_SOAP_META_PORT, port);
  
  if(port == -1)
    {
    request_host = gavl_strdup(host);
    port = 80;
    }
  else
    request_host = bg_sprintf("%s:%d", host, port);
  
  gavl_dictionary_set_string(req_vars, "HOST", request_host);
  gavl_dictionary_set_string(req_vars, "Connection", "keep-alive");
  
  tmp_string = bg_sprintf("\"urn:schemas-upnp-org:service:%s:%d#%s\"",
                          service, version, function);

  gavl_dictionary_set_string_nocopy(req_vars, "SOAPACTION", tmp_string);
  gavl_dictionary_set_string(req_vars, "Content-Type", "text/xml; charset=\"utf-8\"");

  ret = 1;
  fail:
  
  if(path)
    free(path);
  
  if(request_host)
    free(request_host);
  
  return ret;
  }

int bg_soap_request(gavl_dictionary_t * s, gavl_io_t ** io_p)
  {
  int ret = 0;
  gavl_io_t * io = *io_p;
  const char * function;
  const char * service;
  gavl_buffer_t req_buf;
  gavl_buffer_t res_buf;
  int version = 0;
  xmlDocPtr req;
  
  function = gavl_dictionary_get_string(s, BG_SOAP_META_FUNCTION);
  service = gavl_dictionary_get_string(s, BG_SOAP_META_SERVICE);
  gavl_dictionary_get_int(s, BG_SOAP_META_VERSION, &version);
  
  req = soap_create(function, service, version, 0);
  gavl_dictionary_foreach(gavl_dictionary_get_dictionary(s, BG_SOAP_META_ARGS_IN),
                          write_arg_foreach, req);

  gavl_buffer_init(&req_buf);
  bg_xml_save_to_buffer(req, &req_buf);
  
  if(!io)
    {
    io = gavl_http_client_create();
    *io_p = io;
    }

  gavl_http_client_set_req_vars(io, gavl_dictionary_get_dictionary(s, BG_SOAP_META_REQ_VARS));
  gavl_http_client_set_request_body(io, &req_buf);

  gavl_buffer_init(&res_buf);
  gavl_http_client_set_response_body(io, &res_buf);

  ret = gavl_http_client_open(io, "POST", gavl_dictionary_get_string(s, GAVL_META_URI));

  //  fprintf(stderr, "gavl_http_client_open %d %d\n", ret, res_buf.len);
  
  /* Parse body also in case of an error */
  if(res_buf.len)
    {
    xmlDocPtr doc;
    
    if(!(doc = xmlParseMemory((char*)res_buf.buf, res_buf.len)))
      goto fail;
    
    if(!ret)
      {
      xmlNodePtr node;
      const gavl_dictionary_t * res;
      int status = 0;
      
      res = gavl_http_client_get_response(io);
      status = gavl_http_response_get_status_int(res);
      
      if(doc && (node = get_body(doc)))
        {
        xmlNodePtr err_code;
        xmlNodePtr err_str;
        
        if(!(node = bg_xml_find_node_child(node, "Fault")))
          goto fail;
        if(!(node = bg_xml_find_node_child(node, "detail")))
          goto fail;
        if(!(node = bg_xml_find_node_child(node, "UPnPError")))
          goto fail;

        if(!(err_code = bg_xml_find_node_child(node, "errorCode")))
          goto fail;
        if(!(err_str = bg_xml_find_node_child(node, "errorDescription")))
          goto fail;
      
        gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "SOAP request failed. HTTP code: %d %s UPNP code: %s %s", 
                 status, gavl_http_response_get_status_str(res),
                 bg_xml_node_get_text_content(err_code),
                 bg_xml_node_get_text_content(err_str));
        }
      else
        gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "SOAP request failed. HTTP code: %d %s", 
                 status, gavl_http_response_get_status_str(res));
      }
    else
      {
      xmlNodePtr func_xml;
      xmlNodePtr arg = NULL;
      gavl_dictionary_t * args;
      
      if(!(func_xml = bg_soap_get_function(doc)))
        goto fail;
    
      args = gavl_dictionary_get_dictionary_create(s, BG_SOAP_META_ARGS_OUT);
      
      while((arg = bg_soap_request_next_argument(func_xml, arg)))
        {
        const char * str = bg_xml_node_get_text_content(arg);

        if(!str || (*str == '\0'))
          gavl_dictionary_set_string(args, (char*)arg->name, BG_SOAP_ARG_EMPTY);
        else
          gavl_dictionary_set_string(args, (char*)arg->name, str);
        }

      //      fprintf(stderr, "Got return args\n");
      //      gavl_dictionary_dump(args, 2);
      }
    
    }
  
  fail:

  gavl_buffer_free(&req_buf);
  gavl_buffer_free(&res_buf);
  
  
  return ret;
  
  }
