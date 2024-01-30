
#include <string.h>

#include <config.h>
#include <gavl/log.h>
#define LOG_DOMAIN "be_server_upnp"

#include <gmerlin/translation.h>
#include <gmerlin/plugin.h>

#include <gmerlin/bgmsg.h>
#include <gmerlin/mdb.h>
#include <gmerlin/upnp/upnputils.h>

#include <gmerlin/upnp/soap.h>
#include <gmerlin/upnp/devicedesc.h>
#include <gmerlin/upnp/didl.h>
#include <gmerlin/backend.h>


typedef struct
  {
  bg_controllable_t ctrl;
  char * cd_control_url;
  gavf_io_t * control_io;
  
  gavl_dictionary_t dev;
  } server_t;

static char * id_gmerlin_to_upnp(const char * id)
  {
  if(!strcmp(id, "/"))
    {
    return gavl_strdup("0");
    }
  else
    {
    gavl_buffer_t buf;
    uint8_t zero = 0x00;
    
    const char * pos = strrchr(id, '/');
    if(!pos)
      return NULL;

    gavl_buffer_init(&buf);
    gavl_base64_decode_data_urlsafe(pos + 1, &buf);
    gavl_buffer_append_data(&buf, &zero, 0);
    return (char*)buf.buf;
    }
  }

static char * id_upnp_to_gmerlin(const char * id, const char * parent_id_gmerlin)
  {
  char * ret;
  char * tmp_string = gavl_base64_encode_data_urlsafe(id, strlen(id));

  if(!strcmp(parent_id_gmerlin, "/"))
    ret = gavl_sprintf("%s%s", parent_id_gmerlin, tmp_string);
  else
    ret = gavl_sprintf("%s/%s", parent_id_gmerlin, tmp_string);
  free(tmp_string);
  return ret;
  }


static int handle_msg(void * data, gavl_msg_t * msg)
  {
  server_t * server = data;

  //  fprintf(stderr, "handle_msg_server %d %d\n", msg->NS, msg->ID);

  switch(msg->NS)
    {
    case BG_MSG_NS_DB: // 114
      {
      switch(msg->ID)
        {
        case BG_FUNC_DB_BROWSE_OBJECT:
          {
          const char * Result;
          gavl_dictionary_t s;
          gavl_dictionary_t * args_in;
          const gavl_dictionary_t * args_out;
          
          const char * id;
          char * id_upnp;
          id = gavl_dictionary_get_string(&msg->header, GAVL_MSG_CONTEXT_ID);

          //          fprintf(stderr, "BG_FUNC_DB_BROWSE_OBJECT: %s\n", id);

          id_upnp = id_gmerlin_to_upnp(id);
          
          bg_soap_request_init(&s, server->cd_control_url, "ContentDirectory", 1, "Browse");
          args_in = gavl_dictionary_get_dictionary_nc(&s, BG_SOAP_META_ARGS_IN);

          gavl_dictionary_set_string(args_in, "ObjectID", id_upnp);
          gavl_dictionary_set_string(args_in, "BrowseFlag", "BrowseMetadata");
          gavl_dictionary_set_string(args_in, "Filter", "*");
          gavl_dictionary_set_string(args_in, "StartingIndex", "0");
          gavl_dictionary_set_string(args_in, "RequestedCount", "0");
          gavl_dictionary_set_string(args_in, "SortCriteria", BG_SOAP_ARG_EMPTY);

          if(bg_soap_request(&s, &server->control_io) &&
             (args_out = gavl_dictionary_get_dictionary(&s, BG_SOAP_META_ARGS_OUT)) &&
             (Result = gavl_dictionary_get_string(args_out, "Result")))
            {
            xmlDocPtr doc;
            xmlNodePtr node;
            gavl_dictionary_t dict;
            gavl_dictionary_t * m;

            gavl_dictionary_init(&dict);
            m = gavl_dictionary_get_dictionary_create(&dict, GAVL_META_METADATA);
            
            //            fprintf(stderr, "Got result:\n%s\n", Result);

            //            gavl_dictionary_dump(&be->dev, 2);
            
            if((doc = xmlParseMemory(Result, strlen(Result))) &&
               (node = bg_xml_find_doc_child(doc, "DIDL-Lite")) &&
               (node = bg_xml_find_next_node_child(node, NULL)) &&
               (!strcmp((const char*)node->name, "container") || !strcmp((const char*)node->name, "item")))
              {
              gavl_msg_t * res;
              
              bg_track_from_didl(&dict, node);
              /* upnp ID -> gmerlin id */
              gavl_track_set_id(&dict, id);

              if(!strcmp(id, "/"))
                {
                /* Merge device info */
                gavl_dictionary_copy_value(m, &server->dev, GAVL_META_LABEL);
                gavl_dictionary_copy_value(m, &server->dev, GAVL_META_ICON_URL);
                }

              res = bg_msg_sink_get(server->ctrl.evt_sink);
              bg_mdb_set_browse_obj_response(res, &dict, msg, -1, -1);
              bg_msg_sink_put(server->ctrl.evt_sink);
              // gavl_dictionary_dump(&dict, 2);
              gavl_dictionary_free(&dict);
              
              }
            if(doc)
              xmlFreeDoc(doc);
            }
          
          gavl_dictionary_free(&s);
          }
          break;
        case BG_FUNC_DB_BROWSE_CHILDREN:
          {
          int start = 0, num = 0, one_answer = 0;
          const char * id = NULL;
          const char * Result;
          gavl_dictionary_t s;
          gavl_dictionary_t * args_in;
          const gavl_dictionary_t * args_out;

          
          bg_mdb_get_browse_children_request(msg, &id, &start, &num, &one_answer);
          
          //          fprintf(stderr, "BG_FUNC_DB_BROWSE_CHILDREN %s %d %d %d\n", id, start, num, one_answer);
          
          bg_soap_request_init(&s, server->cd_control_url, "ContentDirectory", 1, "Browse");
          args_in = gavl_dictionary_get_dictionary_nc(&s, BG_SOAP_META_ARGS_IN);

          if(num < 0)
            num = 0;
          
          gavl_dictionary_set_string_nocopy(args_in, "ObjectID", id_gmerlin_to_upnp(id));
          gavl_dictionary_set_string(args_in, "BrowseFlag", "BrowseDirectChildren");
          gavl_dictionary_set_string(args_in, "Filter", "*");
          gavl_dictionary_set_string_nocopy(args_in, "StartingIndex", gavl_sprintf("%d", start));
          gavl_dictionary_set_string_nocopy(args_in, "RequestedCount", gavl_sprintf("%d", num));
          gavl_dictionary_set_string(args_in, "SortCriteria", BG_SOAP_ARG_EMPTY);

          if(bg_soap_request(&s, &server->control_io) &&
             (args_out = gavl_dictionary_get_dictionary(&s, BG_SOAP_META_ARGS_OUT)) &&
             (Result = gavl_dictionary_get_string(args_out, "Result")))
            {
            int total_matches = -1;
            xmlDocPtr doc;
            xmlNodePtr node;
            int num_ret = 0;
            int total = 0;

            gavl_dictionary_get_int(args_out, "NumberReturned", &num_ret);
            gavl_dictionary_get_int(args_out, "TotalMatches",   &total);
            
            //            fprintf(stderr, "Got result: %s\n", Result);
            //            gavl_dictionary_dump(track, 2);

            if((doc = xmlParseMemory(Result, strlen(Result))) &&
               (node = bg_xml_find_doc_child(doc, "DIDL-Lite")))
              {
              gavl_array_t arr;
              xmlNodePtr child = NULL;

              gavl_array_init(&arr);
              
              while((child = bg_xml_find_next_node_child(node, child)))
                {
                char * child_id;
                gavl_value_t track_val;
                gavl_dictionary_t * track;
                
                if(strcmp((const char*)child->name, "container") &&
                   strcmp((const char*)child->name, "item"))
                  continue;

                gavl_value_init(&track_val);
                track = gavl_value_set_dictionary(&track_val);

                bg_track_from_didl(track, child);

                /* Translate ID */
                child_id = id_upnp_to_gmerlin(gavl_track_get_id(track), id);
                gavl_track_set_id_nocopy(track, child_id);
                
                //                fprintf(stderr, "Got track:\n");
                //                gavl_dictionary_dump(track, 2);

                gavl_array_splice_val_nocopy(&arr, -1, 0, &track_val);
                
                }

              if(arr.num_entries != num_ret)
                {
                gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Number of objects mismatch: %d != %d",
                         arr.num_entries, num_ret);
                }
              
              if(arr.num_entries > 0)
                {
                gavl_msg_t * res;

                //                fprintf(stderr, "Got tracks:\n");
                //                gavl_array_dump(&arr, 2);
                
                res = bg_msg_sink_get(server->ctrl.evt_sink);
                bg_mdb_set_browse_children_response(res, &arr, msg, &start, 1, total_matches);
                bg_msg_sink_put(server->ctrl.evt_sink);
                }
              gavl_array_free(&arr);
              }
            }
          }
          break;
        }
      }
    }
  
  return 1;

  
  }

static bg_controllable_t * get_controllable_upnp_server(void * priv)
  {
  server_t * s = priv;
  return &s->ctrl;
  }

static void * create_upnp_server()
  {
  server_t * ret;
  ret = calloc(1, sizeof(*ret));

  bg_controllable_init(&ret->ctrl,
                       bg_msg_sink_create(handle_msg, ret, 0),
                       bg_msg_hub_create(1));
  
  return ret;
  }

static void destroy_upnp_server(void * priv)
  {
  server_t * s = priv;

  bg_controllable_cleanup(&s->ctrl);
  gavl_dictionary_free(&s->dev);
  
  free(s);
  }

static int open_upnp_server(void * priv, const char * uri_1)
  {
  
  const char * pos;
  char * uri;

  const char * var;
  //  const char * label;
  
  xmlDocPtr dev_desc = NULL;
  xmlNodePtr service_node = NULL;
  xmlNodePtr dev_node = NULL;
  xmlNodePtr node;
  char * url_base      = NULL;
  int ret = 0;
  server_t * server = priv;
  
  if((pos = strstr(uri_1, "://")))
    uri = gavl_sprintf("http%s", pos);
  else
    uri = gavl_strdup(uri_1);
  
  if(!(dev_desc = bg_xml_from_url(uri, NULL)))
    goto fail;
  
  /* URL Base */
  url_base = bg_upnp_device_description_get_url_base(uri, dev_desc);
  
  /* Get control- and event URLs */

  if(!(dev_node = bg_upnp_device_description_get_device_node(dev_desc, "MediaServer", 1)))
    goto fail;
  
  bg_upnp_device_get_info(&server->dev, url_base, dev_node);
  
  /* ContentDirectory */
  if(!(service_node = bg_upnp_device_description_get_service_node(dev_node, "ContentDirectory", 1)))
    goto fail;

  if(!(node = bg_xml_find_node_child(service_node, "controlURL")) ||
     !(var = bg_xml_node_get_text_content(node)))
    goto fail;
  server->cd_control_url = bg_upnp_device_description_make_url(var, url_base);

  //  fprintf(stderr, "Got control URI: %s\n", server->cd_control_url);
  
  ret = 1;
  
  fail:

  if(dev_desc)
    xmlFreeDoc(dev_desc);
  
  return ret;
  
  return 0;
  }

static int update_upnp_server(void * priv)
  {
  server_t * server = priv;

  bg_msg_sink_iteration(server->ctrl.cmd_sink);
  
  return bg_msg_sink_get_num(server->ctrl.cmd_sink);
  }

bg_backend_plugin_t the_plugin =
  {
    .common =
    {
      BG_LOCALE,
      .name =      "be_server_upnp",
      .long_name = TRS("Upnp server backend"),
      .description = TRS("Upnp media servers"),
      .type =     BG_PLUGIN_BACKEND_MDB,
      .flags =    0,
      .create =   create_upnp_server,
      .destroy =   destroy_upnp_server,
      .get_controllable =   get_controllable_upnp_server,
      .priority =         1,
    },
    .protocol = BG_BACKEND_URI_SCHEME_UPNP_SERVER,
    .update = update_upnp_server,
    .open = open_upnp_server,
  };

/* Include this into all plugin modules exactly once
   to let the plugin loader obtain the API version */
BG_GET_PLUGIN_API_VERSION;
