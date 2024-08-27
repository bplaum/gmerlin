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


#include <string.h>
#include <sys/utsname.h>
#include <unistd.h>



#include <config.h>

#include <gavl/gavl.h>
#include <gavl/metatags.h>
#include <gavl/value.h>
#include <gavl/trackinfo.h>
#include <gavl/log.h>
#define LOG_DOMAIN "frontend_mdb_upnp"

#include <gmerlin/translation.h>
#include <gmerlin/parameter.h>
#include <gmerlin/application.h>

#include <gmerlin/upnp/soap.h>
#include <gmerlin/upnp/upnputils.h>
#include <gmerlin/upnp/event.h>

#include <gmerlin/upnp/didl.h>

// #include <upnp/event.h>

#include <gmerlin/frontend.h>
#include <gmerlin/resourcemanager.h>

#include <gmerlin/utils.h>

#include <gmerlin/mdb.h>

/* Emulate a media server */

/* upnp frontend for the media server */

static const char * dev_desc;

/* ContentDirectory */
static const char * cd_desc;
/* ConnectionManager */
static const char * cm_desc;

typedef struct 
  {
  char * desc;

  gavl_dictionary_t cm_evt;
  gavl_dictionary_t cd_evt;

  gavl_array_t requests;

  gavl_dictionary_t state;

  bg_control_t control;
  
  } bg_mdb_frontend_upnp_t;


static int handle_http_mdb_upnp(bg_http_connection_t * c, void * data)
  {
  bg_mdb_frontend_upnp_t * priv = data;

  bg_http_server_t * srv = bg_http_server_get();
  
  if(!strcmp(c->method, "GET") || !strcmp(c->method, "HEAD"))    
    {
    if(!strcmp(c->path, "desc.xml"))
      {
      bg_upnp_send_description(c, priv->desc);
      return 1;
      }
    else if(!strcmp(c->path, "cm/desc.xml"))
      {
      bg_upnp_send_description(c, cm_desc);
      return 1;
      }
    else if(!strcmp(c->path, "cd/desc.xml"))
      {
      bg_upnp_send_description(c, cd_desc);
      return 1;
      }
    }
  
  else if(!strcmp(c->path, "cd/ctrl"))
    {
    const char * func;
    const gavl_dictionary_t * args_in;
    gavl_dictionary_t * args_out;
    gavl_dictionary_t soap;
    gavl_dictionary_init(&soap);

    if(!bg_soap_request_read_req(&soap, c))
      {
      gavl_socket_close(c->fd);
      c->fd = -1;
      gavl_dictionary_free(&soap);
      return 1;
      }

    //    fprintf(stderr, "Got request:\n");
    //    gavl_dictionary_dump(&soap, 2);
    
    
    /* Handle */

    func = gavl_dictionary_get_string(&soap, BG_SOAP_META_FUNCTION);
    
    args_in  = gavl_dictionary_get_dictionary(&soap, BG_SOAP_META_ARGS_IN);
    args_out = gavl_dictionary_get_dictionary_nc(&soap, BG_SOAP_META_ARGS_OUT);
    
    if(!strcmp(func, "GetSearchCapabilities"))
      {
      //      fprintf(stderr, "Get Search Capabilities\n");
      gavl_dictionary_set_string(args_out, "SearchCaps", BG_SOAP_ARG_EMPTY);
      bg_upnp_finish_soap_request(&soap, c, srv);
      }
    else if(!strcmp(func, "GetSortCapabilities"))
      {
      //      fprintf(stderr, "Get Sort Capabilities\n");
      gavl_dictionary_set_string(args_out, "SortCaps", BG_SOAP_ARG_EMPTY);
      bg_upnp_finish_soap_request(&soap, c, srv);
      }
    else if(!strcmp(func, "Browse"))
      {
      gavl_dictionary_t * req;
      gavl_dictionary_t * conn_dict;
      
      gavl_msg_t * msg;
      
      const char * ObjectID;
      const char * BrowseFlag;
      
      if(!(ObjectID   = gavl_dictionary_get_string(args_in, "ObjectID")) ||
         !(BrowseFlag = gavl_dictionary_get_string(args_in, "BrowseFlag")))
        {
        gavl_dictionary_free(&soap);
        return 0;
        }
      
      //      fprintf(stderr, "Browse(%s %s)\n", ObjectID, BrowseFlag);
      
      /* 1. Generate request message */
      msg = bg_msg_sink_get(priv->control.cmd_sink);

      if(!strcmp(BrowseFlag, "BrowseMetadata"))
        {
        gavl_msg_set_id_ns(msg, BG_FUNC_DB_BROWSE_OBJECT, BG_MSG_NS_DB);
        gavl_dictionary_set_string_nocopy(&msg->header, GAVL_MSG_CONTEXT_ID, bg_upnp_id_from_upnp(ObjectID));
        }
      else
        {
        int start;
        int num;

        char * id;
        
        const char * StartingIndex;
        const char * RequestedCount;
        
        StartingIndex = gavl_dictionary_get_string(args_in, "StartingIndex");
        RequestedCount = gavl_dictionary_get_string(args_in, "RequestedCount");

        start = atoi(StartingIndex);
        num = atoi(RequestedCount);

        id = bg_upnp_id_from_upnp(ObjectID);
        
        //        fprintf(stderr, "UPNP: browse children %s %d %d\n", id, start, num);

        //bg_mdb_set_browse_children_request
        bg_mdb_set_browse_children_request(msg, id, start, num, 1);
        }
      
      /* 2. Store request */

      req = bg_function_push(&priv->requests, msg);
      conn_dict = gavl_dictionary_get_dictionary_create(req, "conn");
      
      bg_http_connection_to_dict_nocopy(c, conn_dict);
      c->fd = -1;

      gavl_dictionary_copy(gavl_dictionary_get_dictionary_create(req, "soap"), &soap);
      
      //gavl_dictionary_set_dictionary_nocopy(req, "soap", &soap);
      
      //      fprintf(stderr, "Got browse request\n");
      //      gavl_dictionary_dump(req, 2);
      
      /* 3. Send request message */
      bg_msg_sink_put(priv->control.cmd_sink);
      }
    gavl_dictionary_free(&soap);
    }

  else if(!strcmp(c->path, "cm/ctrl"))
    {
    //    const char * func;
    gavl_dictionary_t soap;
    gavl_dictionary_init(&soap);
    
    if(!bg_soap_request_read_req(&soap, c))
      {
      gavl_socket_close(c->fd);
      c->fd = -1;
      return 1;
      
      }
    
    /* TODO: Handle */
    //    func = gavl_dictionary_get_string(&soap, BG_SOAP_META_FUNCTION);

    
    
    gavl_dictionary_free(&soap);
    }
  
  return 1;
  }

static int handle_mdb_message_upnp(void * priv, gavl_msg_t * msg)
  {
  bg_mdb_frontend_upnp_t * p = priv;
  
  switch(msg->NS)
    {
    case BG_MSG_NS_DB:
      switch(msg->ID)
        {
        case BG_RESP_DB_BROWSE_OBJECT:
        case BG_RESP_DB_BROWSE_CHILDREN:
          {
          int idx = -1;
          gavl_dictionary_t * req;
          gavl_dictionary_t * conn_dict;
          gavl_dictionary_t * soap;
          const gavl_dictionary_t * args_in;
          gavl_dictionary_t * args_out;
          xmlDocPtr didl;
          char * didl_str;
          bg_http_connection_t conn;
          
          /* Find request */
          
          if(!(req = bg_function_get(&p->requests, msg, &idx)) ||
             !(soap = gavl_dictionary_get_dictionary_nc(req, "soap")) ||
             !(conn_dict = gavl_dictionary_get_dictionary_nc(req, "conn")))
            return 1;

          if(msg->ID == BG_RESP_DB_BROWSE_OBJECT)
            {
            const char * Filter;
            char ** filter_el;
            gavl_dictionary_t dict;

            args_in  = gavl_dictionary_get_dictionary(soap, BG_SOAP_META_ARGS_IN);
            args_out = gavl_dictionary_get_dictionary_nc(soap, BG_SOAP_META_ARGS_OUT);
            
            Filter = gavl_dictionary_get_string(args_in, "Filter");
            
            if(strcmp(Filter, "*"))
              filter_el = bg_didl_create_filter(Filter);
            else
              filter_el = NULL;
            
            /* Load connection */
            bg_http_connection_from_dict_nocopy(&conn, conn_dict);
                        
            args_in  = gavl_dictionary_get_dictionary(soap, BG_SOAP_META_ARGS_IN);
            args_out = gavl_dictionary_get_dictionary_nc(soap, BG_SOAP_META_ARGS_OUT);

            gavl_dictionary_init(&dict);
            gavl_msg_get_arg_dictionary(msg, 0, &dict);
            
            didl = bg_didl_create();
            bg_track_to_didl(didl, &dict, filter_el);
            didl_str = bg_xml_save_to_memory_opt(didl, XML_SAVE_NO_DECL);

            //            fprintf(stderr, "Got browse object reply, Filter: %s\nDidl: %s", Filter, didl_str);
            
            gavl_dictionary_set_string_nocopy(args_out, "Result", didl_str);
            gavl_dictionary_set_string(args_out, "NumberReturned", "1");
            gavl_dictionary_set_string(args_out, "TotalMatches", "1");
            gavl_dictionary_set_string(args_out, "UpdateID", "0");
            
            if(filter_el)
              gavl_strbreak_free(filter_el);

            xmlFreeDoc(didl);
            gavl_dictionary_free(&dict);
            
            bg_upnp_finish_soap_request(soap, &conn, bg_http_server_get());
            gavl_array_splice_val(&p->requests, idx, 1, NULL);
            }
          else // BG_RESP_DB_BROWSE_CHILDREN
            {
            int last;
            int idx;
            int del;
            gavl_value_t val;
            const char * Filter;
            gavl_array_t * children = NULL;
            
            gavl_value_init(&val);
            
            gavl_msg_get_splice_children(msg, &last, &idx, &del, &val);

#if 0 // Should never happen            
            if(!last)
              {
              children = gavl_dictionary_get_array_create(req, "children");
              gavl_array_splice_array(children, -1, 0, gavl_value_get_array(&val));
              }
#endif
            
            if(last)
              {
              int num_returned = 0;
              

              int total = 0;
              int i;
              char ** filter_el;
              
              children = gavl_value_get_array_nc(&val);
              
              /* Generate Browse children reply */
#if 0
              fprintf(stderr, "Got req:\n");
              gavl_dictionary_dump(req, 2);

              fprintf(stderr, "Got soap:\n");
              gavl_dictionary_dump(soap, 2);

              fprintf(stderr, "Got conn:\n");
              gavl_dictionary_dump(conn_dict, 2);
#endif
              /* Load connection */
              bg_http_connection_from_dict_nocopy(&conn, conn_dict);
              
              args_in  = gavl_dictionary_get_dictionary(soap, BG_SOAP_META_ARGS_IN);
              args_out = gavl_dictionary_get_dictionary_nc(soap, BG_SOAP_META_ARGS_OUT);

              Filter = gavl_dictionary_get_string(args_in, "Filter");
              
              //  fprintf(stderr, "Got browse children reply, Filter: %s\n", Filter);
              
              if(strcmp(Filter, "*"))
                filter_el = bg_didl_create_filter(Filter);
              else
                filter_el = NULL;
              
              didl = bg_didl_create();
              
              for(i = 0; i < children->num_entries; i++)
                {
                const gavl_dictionary_t * track;
                
                if((track = gavl_value_get_dictionary(&children->entries[i])))
                  {
                  if(!total)
                    {
                    const gavl_dictionary_t * m;
                    m = gavl_track_get_metadata(track);
                    gavl_dictionary_get_int(m, GAVL_META_TOTAL, &total);
                    }

                  bg_track_to_didl(didl, track, filter_el);
                  num_returned++;
                  }
                }
              
              didl_str = bg_xml_save_to_memory_opt(didl, XML_SAVE_NO_DECL);
              
              //       fprintf(stderr, "DIDL: %s\n", didl_str);
              
              gavl_dictionary_set_string_nocopy(args_out, "Result", didl_str);
              gavl_dictionary_set_string_nocopy(args_out, "NumberReturned", bg_sprintf("%d", num_returned));
              gavl_dictionary_set_string_nocopy(args_out, "TotalMatches", bg_sprintf("%d", total));
              gavl_dictionary_set_string(args_out, "UpdateID", "0");

              //              fprintf(stderr, "Browse children response");
              //              gavl_dictionary_dump(args_out, 2);
              
              bg_upnp_finish_soap_request(soap, &conn, bg_http_server_get());
              gavl_array_splice_val(&p->requests, idx, 1, NULL);
              
              xmlFreeDoc(didl);
              
              if(filter_el)
                gavl_strbreak_free(filter_el);
              }
            gavl_value_free(&val); 
            }
          
          }
          break;
        }
      break;
    case BG_MSG_NS_STATE:
      {
      switch(msg->ID)
        {
        case BG_MSG_STATE_CHANGED:
          {
          int last;
          const char * ctx = NULL;
          const char * var = NULL;

          gavl_value_t val;
          gavl_value_init(&val);
          
          //          bg_msg_get_state(msg, &last, &ctx, &var, NULL, &p->state);
          gavl_msg_get_state(msg, &last, &ctx, &var, &val, &p->state);
          
          gavl_value_free(&val);
          
          
          }
          break;
        }
             
      }
    }
  
  
  return 1;
  }

static void destroy_mdb_upnp(void * priv)
  {
  bg_mdb_frontend_upnp_t * p = priv;

  if(p->desc)
    free(p->desc);
  
  gavl_dictionary_free(&p->cd_evt);
  gavl_dictionary_free(&p->cm_evt);
  gavl_array_free(&p->requests);

  gavl_dictionary_free(&p->state);
  
  free(p);
  }

/* New Functions */

static int open_mdb_upnp(void * data, bg_controllable_t * ctrl)
  {
  bg_http_server_t * srv;
  bg_mdb_frontend_upnp_t * priv = data;

  char * icons;
  gavl_dictionary_t local_dev;
    
  const char * server_label;
  const char * icon_name;
  const char * root_uri;
    
  char uuid_str[37];
    
  char * uri;

  
  if(!(srv = bg_http_server_get()))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "No http server present");
    return 0;
    }

  bg_control_init(&priv->control, bg_msg_sink_create(handle_mdb_message_upnp, priv, 0));
  bg_controllable_connect(ctrl, &priv->control);
  
  //  priv->controllable = ctrl;

  /* Add the event handlers first */
  bg_upnp_event_context_init_server(&priv->cm_evt, "/upnp/server/cm/evt");
  bg_upnp_event_context_init_server(&priv->cd_evt, "/upnp/server/cd/evt");
  
  bg_http_server_add_handler(srv, handle_http_mdb_upnp,
                             BG_HTTP_PROTO_HTTP, "/upnp/server/", // E.g. /static/ can be NULL
                             priv);
  
  /* Publish device */

  root_uri = bg_http_server_get_root_url(bg_http_server_get());
    
  uri = bg_sprintf("%s/upnp/server/desc.xml", root_uri);

  gavl_dictionary_init(&local_dev);
    
  if(!(server_label = bg_app_get_label()))
    return 0;

  if((icon_name = bg_app_get_icon_name()))
    {
    gavl_array_t icon_arr;
    char * prefix = gavl_sprintf("%s/static/icons/", root_uri);
    gavl_array_init(&icon_arr);
    bg_array_add_application_icons(&icon_arr, prefix, icon_name);
    icons = bg_upnp_create_icon_list(&icon_arr);
    gavl_array_free(&icon_arr);
    free(prefix);
    }
  else
    icons = gavl_strdup("");
    
  /* Register local device */

  gavl_dictionary_set_string_nocopy(&local_dev, GAVL_META_URI,
                                    bg_sprintf("%s://%s", BG_BACKEND_URI_SCHEME_UPNP_SERVER, uri + 7));
    
  gavl_dictionary_set_string(&local_dev, GAVL_META_CLASS, GAVL_META_CLASS_BACKEND_MDB);
  gavl_dictionary_set_string(&local_dev, GAVL_META_LABEL, server_label);
    
  bg_uri_to_uuid(gavl_dictionary_get_string(&local_dev, GAVL_META_URI), uuid_str);
    
  priv->desc = bg_sprintf(dev_desc, uuid_str, server_label, icons);

  bg_resourcemanager_publish(gavl_dictionary_get_string(&local_dev, GAVL_META_URI), &local_dev);

  free(icons);
  free(uri);

  gavl_dictionary_free(&local_dev);
  return 1;
  }

static void * create_mdb_upnp()
  {
  bg_mdb_frontend_upnp_t * priv;
  priv = calloc(1, sizeof(*priv));

  
  
  return priv;
  }

static int ping_mdb_upnp(void * data)
  {
  int ret = 0;
  bg_mdb_frontend_upnp_t * priv = data;
  bg_msg_sink_iteration(priv->control.evt_sink);
  ret += bg_msg_sink_get_num(priv->control.evt_sink);
  return ret;
  }


/* Plugin */

bg_frontend_plugin_t the_plugin =
  {
    .common =
    {
      BG_LOCALE,
      .name =      "fe_mdb_upnp",
      .long_name = TRS("Upnp dlna access"),
      .description = TRS("Launch an Upnp media server for accessing the media DB."),
      .type =     BG_PLUGIN_FRONTEND_MDB,
      .flags =    BG_PLUGIN_NEEDS_HTTP_SERVER,
      .create =   create_mdb_upnp,
      .destroy =   destroy_mdb_upnp,
      .priority =         1,
    },
    //    .handle_message = handle_mdb_message,
    .update = ping_mdb_upnp,
    .open = open_mdb_upnp,
  };

/* Include this into all plugin modules exactly once
   to let the plugin loader obtain the API version */
BG_GET_PLUGIN_API_VERSION;




/* Descriptions */

static const char * dev_desc =
"<?xml version=\"1.0\" encoding=\"utf-8\"?>"
"<root xmlns=\"urn:schemas-upnp-org:device-1-0\" "
"      xmlns:dlna=\"urn:schemas-dlna-org:device-1-0\" "
"      xmlns:sec=\"http://www.sec.co.kr/dlna\">"
"  <specVersion>"
"    <major>1</major>"
"    <minor>0</minor>"
"  </specVersion>"
"  <device>"
"    <UDN>uuid:%s</UDN>"
"    <friendlyName>%s</friendlyName>"
"    <deviceType>urn:schemas-upnp-org:device:MediaServer:1</deviceType>"
"    <manufacturer>Gmerlin project</manufacturer>"
"    <manufacturerURL>http://gmerlin.sourceforge.net</manufacturerURL>"
"    <modelName>Gmerlin Media Server</modelName>"
"    <modelDescription></modelDescription>"
"    <modelNumber>"VERSION"</modelNumber>"
"    <modelURL>http://gmerlin.sourceforge.net</modelURL>"
"    <serialNumber>"VERSION"</serialNumber>"
"%s"  
"    <serviceList>"
"      <service>"
"        <serviceType>urn:schemas-upnp-org:service:ContentDirectory:1</serviceType>"
"        <serviceId>urn:upnp-org:serviceId:ContentDirectory</serviceId>"
"        <SCPDURL>/upnp/server/cd/desc.xml</SCPDURL>"
"        <controlURL>/upnp/server/cd/ctrl</controlURL>"
"        <eventSubURL>/upnp/server/cd/evt</eventSubURL>"
"      </service>"
"      <service>"
"        <serviceType>urn:schemas-upnp-org:service:ConnectionManager:1</serviceType>"
"        <serviceId>urn:upnp-org:serviceId:ConnectionManager</serviceId>"
"        <SCPDURL>/upnp/server/cm/desc.xml</SCPDURL>"
"        <controlURL>/upnp/server/cm/ctrl</controlURL>"
"        <eventSubURL>/upnp/server/cm/evt</eventSubURL>"
"      </service>"
"    </serviceList>"
"  </device>"
"</root>";

/* Content Directory */
static const char * cd_desc = 
"<?xml version=\"1.0\" encoding=\"utf-8\"?>"
"<scpd xmlns=\"urn:schemas-upnp-org:service-1-0\">"
"  <specVersion>"
"    <major>1</major>"
"    <minor>0</minor>"
"  </specVersion>"
"  <actionList>"
"    <action>"
"      <name>GetSystemUpdateID</name>"
"      <argumentList>"
"        <argument>"
"          <name>Id</name>"
"          <direction>out</direction>"
"          <relatedStateVariable>SystemUpdateID</relatedStateVariable>"
"        </argument>"
"      </argumentList>"
"    </action>"
"    <action>"
"      <name>GetSearchCapabilities</name>"
"      <argumentList>"
"        <argument>"
"          <name>SearchCaps</name>"
"          <direction>out</direction>"
"          <relatedStateVariable>SearchCapabilities</relatedStateVariable>"
"        </argument>"
"      </argumentList>"
"    </action>"
"    <action>"
"      <name>GetSortCapabilities</name>"
"      <argumentList>"
"        <argument>"
"          <name>SortCaps</name>"
"          <direction>out</direction>"
"          <relatedStateVariable>SortCapabilities</relatedStateVariable>"
"        </argument>"
"      </argumentList>"
"    </action>"
"    <action>"
"      <name>Browse</name>"
"      <argumentList>"
"        <argument>"
"          <name>ObjectID</name>"
"          <direction>in</direction>"
"          <relatedStateVariable>A_ARG_TYPE_ObjectID</relatedStateVariable>"
"        </argument>"
"        <argument>"
"          <name>BrowseFlag</name>"
"          <direction>in</direction>"
"          <relatedStateVariable>A_ARG_TYPE_BrowseFlag</relatedStateVariable>"
"        </argument>"
"        <argument>"
"          <name>Filter</name>"
"          <direction>in</direction>"
"          <relatedStateVariable>A_ARG_TYPE_Filter</relatedStateVariable>"
"        </argument>"
"        <argument>"
"          <name>StartingIndex</name>"
"          <direction>in</direction>"
"          <relatedStateVariable>A_ARG_TYPE_Index</relatedStateVariable>"
"        </argument>"
"        <argument>"
"          <name>RequestedCount</name>"
"          <direction>in</direction>"
"          <relatedStateVariable>A_ARG_TYPE_Count</relatedStateVariable>"
"        </argument>"
"        <argument>"
"          <name>SortCriteria</name>"
"          <direction>in</direction>"
"          <relatedStateVariable>A_ARG_TYPE_SortCriteria</relatedStateVariable>"
"        </argument>"
"        <argument>"
"          <name>Result</name>"
"          <direction>out</direction>"
"          <relatedStateVariable>A_ARG_TYPE_Result</relatedStateVariable>"
"        </argument>"
"        <argument>"
"          <name>NumberReturned</name>"
"          <direction>out</direction>"
"          <relatedStateVariable>A_ARG_TYPE_Count</relatedStateVariable>"
"        </argument>"
"        <argument>"
"          <name>TotalMatches</name>"
"          <direction>out</direction>"
"          <relatedStateVariable>A_ARG_TYPE_Count</relatedStateVariable>"
"        </argument>"
"        <argument>"
"          <name>UpdateID</name>"
"          <direction>out</direction>"
"          <relatedStateVariable>A_ARG_TYPE_UpdateID</relatedStateVariable>"
"        </argument>"
"      </argumentList>"
"    </action>"
"  </actionList>"
"  <serviceStateTable>"
"    <stateVariable sendEvents=\"no\">"
"      <name>A_ARG_TYPE_SortCriteria</name>"
"      <dataType>string</dataType>"
"    </stateVariable>"
"    <stateVariable sendEvents=\"no\">"
"      <name>A_ARG_TYPE_UpdateID</name>"
"      <dataType>ui4</dataType>"
"    </stateVariable>"
"    <stateVariable sendEvents=\"no\">"
"      <name>A_ARG_TYPE_SearchCriteria</name>"
"      <dataType>string</dataType>"
"    </stateVariable>"
"    <stateVariable sendEvents=\"no\">"
"      <name>A_ARG_TYPE_Filter</name>"
"      <dataType>string</dataType>"
"    </stateVariable>"
"    <stateVariable sendEvents=\"no\">"
"      <name>A_ARG_TYPE_Result</name>"
"      <dataType>string</dataType>"
"    </stateVariable>"
"    <stateVariable sendEvents=\"no\">"
"      <name>A_ARG_TYPE_Index</name>"
"      <dataType>ui4</dataType>"
"    </stateVariable>"
"    <stateVariable sendEvents=\"no\">"
"      <name>A_ARG_TYPE_ObjectID</name>"
"      <dataType>string</dataType>"
"    </stateVariable>"
"    <stateVariable sendEvents=\"no\">"
"      <name>SortCapabilities</name>"
"      <dataType>string</dataType>"
"    </stateVariable>"
"    <stateVariable sendEvents=\"no\">"
"      <name>SearchCapabilities</name>"
"      <dataType>string</dataType>"
"    </stateVariable>"
"    <stateVariable sendEvents=\"no\">"
"      <name>A_ARG_TYPE_Count</name>"
"      <dataType>ui4</dataType>"
"    </stateVariable>"
"    <stateVariable sendEvents=\"no\">"
"      <name>A_ARG_TYPE_BrowseFlag</name>"
"      <dataType>string</dataType>"
"      <allowedValueList>"
"        <allowedValue>BrowseMetadata</allowedValue>"
"        <allowedValue>BrowseDirectChildren</allowedValue>"
"      </allowedValueList>"
"    </stateVariable>"
"    <stateVariable sendEvents=\"yes\">"
"      <name>SystemUpdateID</name>"
"      <dataType>ui4</dataType>"
"    </stateVariable>"
"    <stateVariable sendEvents=\"no\">"
"      <name>A_ARG_TYPE_BrowseLetter</name>"
"      <dataType>string</dataType>"
"    </stateVariable>"
"    <stateVariable sendEvents=\"no\">"
"      <name>A_ARG_TYPE_CategoryType</name>"
"      <dataType>ui4</dataType>"
"      <defaultValue />"
"    </stateVariable>"
"    <stateVariable sendEvents=\"no\">"
"      <name>A_ARG_TYPE_RID</name>"
"      <dataType>ui4</dataType>"
"      <defaultValue />"
"    </stateVariable>"
"    <stateVariable sendEvents=\"no\">"
"      <name>A_ARG_TYPE_PosSec</name>"
"      <dataType>ui4</dataType>"
"      <defaultValue />"
"    </stateVariable>"
"    <stateVariable sendEvents=\"no\">"
"      <name>A_ARG_TYPE_Featurelist</name>"
"      <dataType>string</dataType>"
"      <defaultValue />"
"    </stateVariable>"
"  </serviceStateTable>"
"</scpd>";

static const char * cm_desc =
  "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
"<scpd xmlns=\"urn:schemas-upnp-org:service-1-0\">"
"  <specVersion>"
"    <major>1</major>"
"    <minor>0</minor>"
"  </specVersion>"
"  <actionList>"
"    <action>"
"      <name>GetCurrentConnectionInfo</name>"
"      <argumentList>"
"        <argument>"
"          <name>ConnectionID</name>"
"          <direction>in</direction>"
"          <relatedStateVariable>A_ARG_TYPE_ConnectionID</relatedStateVariable>"
"        </argument>"
"        <argument>"
"          <name>RcsID</name>"
"          <direction>out</direction>"
"          <relatedStateVariable>A_ARG_TYPE_RcsID</relatedStateVariable>"
"        </argument>"
"        <argument>"
"          <name>AVTransportID</name>"
"          <direction>out</direction>"
"          <relatedStateVariable>A_ARG_TYPE_AVTransportID</relatedStateVariable>"
"        </argument>"
"        <argument>"
"          <name>ProtocolInfo</name>"
"          <direction>out</direction>"
"          <relatedStateVariable>A_ARG_TYPE_ProtocolInfo</relatedStateVariable>"
"        </argument>"
"        <argument>"
"          <name>PeerConnectionManager</name>"
"          <direction>out</direction>"
"          <relatedStateVariable>A_ARG_TYPE_ConnectionManager</relatedStateVariable>"
"        </argument>"
"        <argument>"
"          <name>PeerConnectionID</name>"
"          <direction>out</direction>"
"          <relatedStateVariable>A_ARG_TYPE_ConnectionID</relatedStateVariable>"
"        </argument>"
"        <argument>"
"          <name>Direction</name>"
"          <direction>out</direction>"
"          <relatedStateVariable>A_ARG_TYPE_Direction</relatedStateVariable>"
"        </argument>"
"        <argument>"
"          <name>Status</name>"
"          <direction>out</direction>"
"          <relatedStateVariable>A_ARG_TYPE_ConnectionStatus</relatedStateVariable>"
"        </argument>"
"      </argumentList>"
"    </action>"
"    <action>"
"      <name>GetProtocolInfo</name>"
"      <argumentList>"
"        <argument>"
"          <name>Source</name>"
"          <direction>out</direction>"
"          <relatedStateVariable>SourceProtocolInfo</relatedStateVariable>"
"        </argument>"
"        <argument>"
"          <name>Sink</name>"
"          <direction>out</direction>"
"          <relatedStateVariable>SinkProtocolInfo</relatedStateVariable>"
"        </argument>"
"      </argumentList>"
"    </action>"
"    <action>"
"      <name>GetCurrentConnectionIDs</name>"
"      <argumentList>"
"        <argument>"
"          <name>ConnectionIDs</name>"
"          <direction>out</direction>"
"          <relatedStateVariable>CurrentConnectionIDs</relatedStateVariable>"
"        </argument>"
"      </argumentList>"
"    </action>"
"  </actionList>"
"  <serviceStateTable>"
"    <stateVariable sendEvents=\"no\">"
"      <name>A_ARG_TYPE_ProtocolInfo</name>"
"      <dataType>string</dataType>"
"    </stateVariable>"
"    <stateVariable sendEvents=\"no\">"
"      <name>A_ARG_TYPE_ConnectionStatus</name>"
"      <dataType>string</dataType>"
"      <allowedValueList>"
"        <allowedValue>OK</allowedValue>"
"        <allowedValue>ContentFormatMismatch</allowedValue>"
"        <allowedValue>InsufficientBandwidth</allowedValue>"
"        <allowedValue>UnreliableChannel</allowedValue>"
"        <allowedValue>Unknown</allowedValue>"
"      </allowedValueList>"
"    </stateVariable>"
"    <stateVariable sendEvents=\"no\">"
"      <name>A_ARG_TYPE_AVTransportID</name>"
"      <dataType>i4</dataType>"
"      <defaultValue>0</defaultValue>"
"    </stateVariable>"
"    <stateVariable sendEvents=\"no\">"
"      <name>A_ARG_TYPE_RcsID</name>"
"      <dataType>i4</dataType>"
"      <defaultValue>0</defaultValue>"
"    </stateVariable>"
"    <stateVariable sendEvents=\"no\">"
"      <name>A_ARG_TYPE_ConnectionID</name>"
"      <dataType>i4</dataType>"
"      <defaultValue>0</defaultValue>"
"    </stateVariable>"
"    <stateVariable sendEvents=\"no\">"
"      <name>A_ARG_TYPE_ConnectionManager</name>"
"      <dataType>string</dataType>"
"    </stateVariable>"
"    <stateVariable sendEvents=\"yes\">"
"      <name>SourceProtocolInfo</name>"
"      <dataType>string</dataType>"
"    </stateVariable>"
"    <stateVariable sendEvents=\"yes\">"
"      <name>SinkProtocolInfo</name>"
"      <dataType>string</dataType>"
"    </stateVariable>"
"    <stateVariable sendEvents=\"no\">"
"      <name>A_ARG_TYPE_Direction</name>"
"      <dataType>string</dataType>"
"      <allowedValueList>"
"        <allowedValue>Input</allowedValue>"
"        <allowedValue>Output</allowedValue>"
"      </allowedValueList>"
"    </stateVariable>"
"    <stateVariable sendEvents=\"yes\">"
"      <name>CurrentConnectionIDs</name>"
"      <dataType>string</dataType>"
"      <defaultValue>0</defaultValue>"
"    </stateVariable>"
"  </serviceStateTable>"
"</scpd>";
