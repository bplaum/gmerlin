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

#include <string.h>
#include <uuid/uuid.h>

#include <config.h>

#include "server.h"

#include <gmerlin/translation.h>
#include <gmerlin/log.h>
#include <gmerlin/state.h>
#include <gmerlin/bggavl.h>
#include <gmerlin/application.h>

#include <gmerlin/utils.h>
#include <gavl/metatags.h>

#include <gmerlin/translation.h>
#include <gmerlin/log.h>
#define LOG_DOMAIN "server"

#define VAR_PREFIX "server"

const char * storage_vars[] =
  {
    "cfg",
    NULL,
  };

static const bg_parameter_info_t parameters[] =
  {
    {
      .name =      "loglevel",
      .type = BG_PARAMETER_INT,
      .long_name =  TRS("Log level"),
      .val_min = GAVL_VALUE_INIT_INT(0),
      .val_max = GAVL_VALUE_INIT_INT(4),
      .val_default = GAVL_VALUE_INIT_INT(3),
    },
    {
      .name =      "syslog",
      .long_name = TRS("Syslog name"),
      .type = BG_PARAMETER_STRING,
    },
    {
      .name =      "label",
      .long_name = TRS("Label"),
      .type = BG_PARAMETER_STRING,
      .val_default = GAVL_VALUE_INIT_STRING("Gmerlin server"),
    },
    {
      .name =      "db",
      .long_name = TRS("Database path"),
      .type = BG_PARAMETER_DIRECTORY,
      .val_default = GAVL_VALUE_INIT_STRING("."),
    },
    {
      .name =      "vardir",
      .type = BG_PARAMETER_DIRECTORY,
      .long_name =  TRS("Storage directory"),
    },
    {
      .name =      "state_file",
      .type = BG_PARAMETER_FILE,
      .long_name =  TRS("State file"),
    },
    {
      .name =      "max_client_ids",
      .type = BG_PARAMETER_INT,
      .flags = BG_PARAMETER_HIDE_DIALOG,
      .long_name =  TRS("Maximum number of clients which can store data"),
      .val_default = GAVL_VALUE_INIT_INT(16),
      .val_min     = GAVL_VALUE_INIT_INT(0),
      .val_max     = GAVL_VALUE_INIT_INT(65535),
    },
    { /* End */ },
  };

void server_set_parameter(void * priv, const char * name,
                          const gavl_value_t * val)
  {
  server_t * s = priv;
  if(!name)
    {
    bg_http_server_set_parameter(s->srv, NULL, NULL);
    return;
    }
  if(!strcmp(name, "db"))
    s->dbpath = gavl_strrep(s->dbpath, val->v.str);
  else if(!strcmp(name, "vardir"))
    s->vardir = gavl_strrep(s->vardir, val->v.str);
  else if(!strcmp(name, "state_file"))
    s->state_file = gavl_strrep(s->state_file, val->v.str);
  else if(!strcmp(name, "label"))
    {
    s->label = gavl_strrep(s->label, val->v.str);

    if(s->mdb)
      bg_mdb_set_root_name(s->mdb, s->label);

    // fprintf(stderr, "Server label: %s\n", s->label);
    }
  else if(!strcmp(name, "max_client_ids"))
    s->max_client_ids = val->v.i;
  else if(!strcmp(name, "syslog"))
    {
    if(val->v.str)
      bg_log_syslog_init(val->v.str);
    }
  else if(!strcmp(name, "loglevel"))
    gavl_set_log_verbose(val->v.i);
  else
    bg_http_server_set_parameter(s->srv, name, val);
  }

static const bg_parameter_info_t * server_get_parameters(server_t * s)
  {
  if(!s->parameters)
    {
    const bg_parameter_info_t * arr[4];
    arr[0] = bg_http_server_get_parameters(s->srv);
    arr[1] = bg_media_dirs_get_parameters();
    arr[2] = parameters;
    arr[3] = NULL;
    s->parameters = bg_parameter_info_concat_arrays(arr);
    }
  return s->parameters;
  }

/* Initialize an array of known mimetypes as state variable */


static const char * manifest_file =
  "{\n"
    "\"short_name\": \"%s\",\n"
    "\"name\": \"%s\",\n"
    "\"display\": \"standalone\",\n"
    "\"icons\": [ \n"
      "{\n"
      "\"src\": \"static/icons/server_16.png\",\n"
      "\"sizes\": \"16x16\",\n"
      "\"type\": \"image/png\",\n"
      "\"density\": 1.0\n"
      "},\n"
      "{\n"
      "\"src\": \"static/icons/server_48.png\",\n"
      "\"sizes\": \"48x48\",\n"
      "\"type\": \"image/png\",\n"
      "\"density\": 1.0\n"
      "},\n"
      "{\n"
      "\"src\": \"static/icons/server_96.png\",\n"
      "\"sizes\": \"96x96\",\n"
      "\"type\": \"image/png\",\n"
      "\"density\": 1.0\n"
      "} ],\n"
  "\"start_url\": \"%s\"\n"
  "}\n";

static int server_handle_manifest(bg_http_connection_t * conn, void * data)
  {
  const char * var;
  int result = 0;
  server_t * s = data;
  
  if(!strcmp(conn->path, "/manifest.json"))
    {
    char * start_url = NULL;
    
    if((var = gavl_dictionary_get_string(&conn->req, "Referer")))
      {
      gavl_dictionary_t url_vars;
      char * protocol = NULL;
      char * host = NULL;
      int port = 0;
      const char * cid;
      char * m = NULL;
      int len = 0;
      
      bg_http_connection_check_keepalive(conn);
      
      gavl_dictionary_init(&url_vars);
      //      fprintf(stderr, "Referer: %s\n", var);
      bg_url_split(var, &protocol, NULL, NULL, &host, &port, NULL);

      gavl_url_get_vars_c(var, &url_vars);
      if((cid = gavl_dictionary_get_string(&url_vars, BG_URL_VAR_CLIENT_ID)))
        {
        start_url = bg_sprintf("%s://%s:%d/?%s=%s", protocol, host, port,
                               BG_URL_VAR_CLIENT_ID, cid);
        m = bg_sprintf(manifest_file, s->label, s->label, start_url);
        len = strlen(m);
        
        bg_http_connection_init_res(conn, conn->protocol, 200, "OK");
        gavl_dictionary_set_string(&conn->res, "Content-Type", "application/manifest+json");
        gavl_dictionary_set_long(&conn->res, "Content-Length", len);

        if(!bg_http_connection_write_res(conn))
          {
          bg_http_connection_clear_keepalive(conn);
          goto cleanup;
          }
        result = 1;
        }

      if(!result)
        {
        bg_http_connection_init_res(conn, conn->protocol, 400, "Bad Request");
        if(!bg_http_connection_write_res(conn))
          {
          bg_http_connection_clear_keepalive(conn);
          goto cleanup;
          }
        }
      
      if(result && !gavl_socket_write_data(conn->fd, (const uint8_t*)m, len))
        bg_http_connection_clear_keepalive(conn);
      
      cleanup:

      if(protocol)
        free(protocol);
      if(host)
        free(host);
      
      gavl_dictionary_free(&url_vars);

      //      fprintf(stderr, "Got manifest:\n%s\n", m);
      
      if(m)
        free(m);
      
      }
    
    return result;
    }
  return 0;
  }


int server_init(server_t * s)
  {
  char * tmp_string;
  bg_controllable_t * mdb_ctrl;
  gavl_dictionary_t root_metadata;
  gavl_dictionary_t * section;
  const gavl_value_t * uuid_val;
  const char * uuid = NULL;

  gavl_dictionary_init(&root_metadata);
  
  memset(s, 0, sizeof(*s));
  
  s->srv = bg_http_server_create();
  bg_http_server_get_media_dirs(s->srv);
  
  /* Set parameters */
  
  section = bg_cfg_registry_find_section(bg_cfg_registry, "server");
  bg_cfg_section_create_items(section, server_get_parameters(s));

  //  fprintf(stderr, "Applying server section\n");
  //  gavl_dictionary_dump(section, 2);

  bg_cfg_section_apply(section, server_get_parameters(s), server_set_parameter, s);

  /* Create vardir */
  if(!s->vardir)
    s->vardir  = bg_search_var_dir(VAR_PREFIX);

  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Using data directory: %s", s->vardir);
  
  /* Load state */

  if(!s->state_file)
    s->state_file = bg_sprintf("%s/state.xml", s->vardir);

  if(!bg_dictionary_load_xml(&s->state, s->state_file, "state") ||
     !(uuid_val =  bg_state_get(&s->state, "server", "uuid")) ||
     !(uuid = gavl_value_get_string(uuid_val)))
    {
    uuid_t u;
    char uuid_str[37];
    gavl_value_t v;
    
    uuid_generate(u);
    uuid_unparse(u, uuid_str);

    gavl_value_init(&v);
    gavl_value_set_string(&v, uuid_str);

    bg_state_set(&s->state, 1, "server", "uuid", &v, NULL, 0);
    
    uuid_val =  bg_state_get(&s->state, "server", "uuid");
    uuid = gavl_value_get_string(uuid_val);
    
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Generated uuid: %s", uuid);

    
    }
  else
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Using saved uuid: %s", uuid);
  
  if(!(s->mdb = bg_mdb_create(s->dbpath, 0, s->srv)))
    return 0;
  
  bg_http_server_set_mdb(s->srv, s->mdb);

  mdb_ctrl = bg_mdb_get_controllable(s->mdb);
  
  
  /* Add handlers */

  bg_http_server_set_generate_client_ids(s->srv);
  bg_http_server_set_root_file(s->srv, "/static/app.html");
  
  bg_http_server_add_handler(s->srv, server_handle_manifest, BG_HTTP_PROTO_HTTP, NULL, s);

  bg_http_server_set_static_path(s->srv, "/static");

  /* Set root metadata */
  
  if(!s->label)
    s->label = gavl_strdup("Gmerlin Server");
  
  bg_mdb_set_root_name(s->mdb, s->label);
  
  /* Create frontends */
  s->fe_upnp    = bg_frontend_create_mdb_upnp(mdb_ctrl);
  s->fe_gmerlin = bg_frontend_create_mdb_gmerlin(mdb_ctrl);

  /* Create server side storage */
  
  tmp_string = bg_sprintf("%s/storage", s->vardir);
  s->storage = bg_server_storage_create(tmp_string, s->max_client_ids, storage_vars);
  free(tmp_string);

  bg_http_server_add_handler(s->srv, bg_server_storage_handle_http,
                             BG_HTTP_PROTO_HTTP, "/storage/",
                             s->storage);
  
  /* Start http part */
  bg_http_server_start(s->srv);

  /* TODO: Maybe add PID and hostname */

  /* Add icons with absolute URLs */
  
  tmp_string = bg_sprintf("%s/static/icons/", bg_http_server_get_root_url(s->srv));
  gavl_dictionary_set(&root_metadata, GAVL_META_ICON_URL, NULL);
  bg_dictionary_add_application_icons(&root_metadata, tmp_string, "server");
  
  free(tmp_string);
  
  bg_mdb_merge_root_metadata(s->mdb, &root_metadata);
  
  bg_set_network_node_info(s->label, gavl_dictionary_get_array(&root_metadata, GAVL_META_ICON_URL),
                           NULL, mdb_ctrl->evt_sink);
  
  gavl_dictionary_free(&root_metadata);
  
  return 1;
  }

void server_cleanup(server_t * s)
  {
  if(s->mdb)
    bg_mdb_stop(s->mdb);

  
  if(s->fe_upnp)
    bg_frontend_destroy(s->fe_upnp);
  if(s->fe_gmerlin)
    bg_frontend_destroy(s->fe_gmerlin);

  if(s->mdb)
    bg_mdb_destroy(s->mdb);

  if(s->srv)
    bg_http_server_destroy(s->srv);


  //  if(s->remotedev_sink)
  //    bg_msg_sink_destroy(s->remotedev_sink);

  if(s->dbpath)
    free(s->dbpath);
  if(s->label)
    free(s->label);
  
  if(s->parameters)
    bg_parameter_info_destroy_array(s->parameters);
  
  if(s->vardir)
    free(s->vardir);

  if(s->storage)
    bg_server_storage_destroy(s->storage);
  
  if(s->state_file)
    {
    bg_dictionary_save_xml(&s->state, s->state_file, "state");
    free(s->state_file);
    }
  gavl_dictionary_free(&s->state);
  }
  
int server_iteration(server_t * s)
  {
  int ret = 0;

  gavl_time_t t = bg_http_server_get_time(s->srv);
  
  ret += bg_http_server_iteration(s->srv);
  
  if(s->fe_upnp)
    ret += bg_frontend_ping(s->fe_upnp, t);

  if(s->fe_gmerlin)
    ret += bg_frontend_ping(s->fe_gmerlin, t);
  
  return ret;
  }

