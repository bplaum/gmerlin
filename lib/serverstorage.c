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

#include <config.h>
#include <gavl/gavl.h>
#include <gavl/utils.h>

#include <gmerlin/utils.h>
#include <gmerlin/http.h>
#include <gmerlin/bgsocket.h>
#include <gmerlin/translation.h>
#include <gmerlin/log.h>
#include <gmerlin/httpserver.h>
#include <gmerlin/bgplug.h>

#define LOG_DOMAIN "storage"

#define ID_NAME_MAX 1024

struct bg_server_storage_s
  {
  int max_clients;
  int num_clients;
  char ** client_ids;
  char * path;
  const char ** vars;
  };

static char * index_filename(bg_server_storage_t * s)
  {
  return bg_sprintf("%s/INDEX", s->path);
  }

static void read_index(bg_server_storage_t * s)
  {
  char * pos;
  char * name;
  FILE * f;
  char buf[ID_NAME_MAX];
  
  name = index_filename(s);
  f = fopen(name, "r");
  free(name);
  if(!f)
    return;
  
  while(1)
    {
    if(!fgets(buf, ID_NAME_MAX, f))
      break;

    if((pos = strchr(buf, '\n')))
      *pos = '\0';
    s->client_ids[s->num_clients] = gavl_strdup(buf);
    s->num_clients++;

    if(s->num_clients == s->max_clients)
      break;
    }
  fclose(f);
  }

static void write_index(bg_server_storage_t * s)
  {
  int i;
  char * name;
  FILE * f;

  name = index_filename(s);
  f = fopen(name, "w");
  free(name);
  if(!f)
    return;
  
  for(i = 0; i < s->num_clients; i++)
    fprintf(f, "%s\n", s->client_ids[i]);
  
  fclose(f);
  } 

bg_server_storage_t *
bg_server_storage_create(const char * local_path,
                         int max_clients,
                         const char ** vars)
  {
  bg_server_storage_t * ret;
  ret = calloc(1, sizeof(*ret));

  ret->max_clients = max_clients;
  ret->client_ids = calloc(ret->max_clients + 1, sizeof(*ret->client_ids));
  ret->vars = vars;
  ret->path = gavl_strdup(local_path);
  read_index(ret);
  return ret;
  }

void
bg_server_storage_destroy(bg_server_storage_t * s)
  {
  int i;
  write_index(s);
  
  for(i = 0; i < s->num_clients; i++)
    free(s->client_ids[i]);

  if(s->client_ids)
    free(s->client_ids);

  free(s->path);
  free(s);
  }


static char * make_path(bg_server_storage_t * s,
                        const char * id,
                        const char * var, int wr)
  {
  int i;
  
  /* Check if var is allowed */
  if(!s->vars)
    return NULL;

  i = 0;
  while(s->vars[i])
    {
    if(!strcmp(var, s->vars[i]))
      break;
    i++;
    }
  if(!s->vars[i])
    return NULL;

  /* Check if client ID is available */
  if(strlen(id) > ID_NAME_MAX - 1)
    return NULL;

  i = 0;
  
  for(i = 0; i < s->num_clients; i++)
    {
    if(!strcmp(s->client_ids[i], id))
      break;
    }

  if(s->client_ids[i]) // Id available, move to front
    {
    if(i)
      {
      char * id_save = s->client_ids[i];

      memmove(&s->client_ids[1],
              &s->client_ids[0],
              i * sizeof(s->client_ids[0]));
      s->client_ids[0] = id_save;
      }
    }
  else if(wr) // New client ID
    {
    char * path;
    if(s->num_clients == s->max_clients)
      {
      char * last_id = s->client_ids[s->num_clients-1];
      
      i = 0;
      while(s->vars[i])
        {
        path = bg_sprintf("%s/%s/%s", s->path, last_id, s->vars[i]);
        gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Deleting %s", path);
        remove(path);
        free(path);
        }
      path = bg_sprintf("%s/%s", s->path, last_id);
      gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Deleting %s", path);
      remove(path);
      free(path);
      free(last_id);
      s->num_clients--;
      }

    if(s->num_clients)
      memmove(&s->client_ids[1],
              &s->client_ids[0],
              (s->num_clients) * sizeof(s->client_ids[0]));

    s->client_ids[0] = gavl_strdup(id);
    
    /* Make directory */
    path = bg_sprintf("%s/%s", s->path, id);
    bg_ensure_directory(path, 1);
    free(path);
    }
  
  return bg_sprintf("%s/%s/%s", s->path, id, var);
  }

void * bg_server_storage_get(bg_server_storage_t * s,
                             const char * client_id,
                             const char * var, int * len)
  {
  gavl_buffer_t buf;
  char * path = make_path(s, client_id, var, 0);

  gavl_buffer_init(&buf);
  
  if(!path)
    return NULL;

  if(!bg_read_file(path, &buf))
    return NULL;
  
  free(path);

  if(len)
    *len = buf.len;
  return buf.buf;
  }

int bg_server_storage_put(bg_server_storage_t * s,
                          const char * client_id,
                          const char * var,
                          void * data, int len)
  {
  char * path = make_path(s, client_id, var, 1);

  if(!path)
    return 0;

  bg_write_file(path, data, len);

  free(path);
  return 1;
  }

#if 1
int bg_server_storage_handle_http(bg_http_connection_t * conn, void * data)
  {
  const char * id;
  bg_server_storage_t * s = data;
  void * buf = NULL;
  int len;
  char * real_path = real_path;
  
  if(!(id = gavl_dictionary_get_string(&conn->url_vars, BG_URL_VAR_CLIENT_ID)))
    {
    bg_http_connection_init_res(conn, conn->protocol, 404, "Not Found");
    gavl_dictionary_set_string(&conn->res, "Content-Type", "text/plain");
    gavl_dictionary_set_string(&conn->res, "Content-Length", "0");
    goto fail;
    }

  real_path = gavl_strdup(conn->path);
  bg_url_get_vars(real_path, NULL);
  
  if(!strcmp(conn->method, "GET"))
    {
    bg_http_connection_check_keepalive(conn);

    if((buf = bg_server_storage_get(s, id, real_path, &len)))
      {
      bg_http_connection_init_res(conn, conn->protocol, 200, "OK");
      gavl_dictionary_set_string(&conn->res, "Content-Type", "text/plain");
      gavl_dictionary_set_int(&conn->res, "Content-Length", len);
      if(!bg_http_connection_write_res(conn) ||
         (gavl_socket_write_data(conn->fd, buf, len) < len))
        {
        bg_http_connection_clear_keepalive(conn);
        goto fail;
        }
      else
        bg_http_connection_init_res(conn, conn->protocol, 200, "OK");
      }
    else
      {
      bg_http_connection_init_res(conn, conn->protocol, 404, "Not Found");
      gavl_dictionary_set_string(&conn->res, "Content-Type", "text/plain");
      gavl_dictionary_set_string(&conn->res, "Content-Length", "0");
      }
    }
  else if(!strcmp(conn->method, "PUT"))
    {
    gavf_io_t * io = NULL;
    gavl_buffer_t buffer;
    gavl_buffer_init(&buffer);
    
    bg_http_connection_check_keepalive(conn);

    if(!(io = gavf_io_create_socket(conn->fd, 10000, 0)) ||
       !bg_http_read_body(io, &conn->req, &buffer) ||
       !bg_server_storage_put(s, id, real_path, buffer.buf, buffer.len))
      bg_http_connection_init_res(conn, conn->protocol, 400, "Bad Request");
    else
      {
      bg_http_connection_init_res(conn, conn->protocol, 200, "OK");
      gavl_dictionary_set_string(&conn->res, "Content-Type", "text/plain");
      gavl_dictionary_set_string(&conn->res, "Content-Length", "0");
      }
    
    if(io)
      gavf_io_destroy(io);
    
    gavl_buffer_free(&buffer);
    }
  
  fail:
 
  if(buf)
    free(buf);
  if(real_path)
    free(real_path);
  
  return 1;
  }
#endif
