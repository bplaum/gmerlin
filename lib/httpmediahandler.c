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


#include <errno.h>
#include <string.h>
#include <unistd.h>


#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>


#include <config.h>
#include <gavl/gavl.h>
#include <gavl/metatags.h>
#include <gavl/numptr.h>

#include <gmerlin/utils.h>

#include <gmerlin/http.h>
#include <gmerlin/httpserver.h>
#include <gmerlin/upnp/upnputils.h>

#include <gmerlin/translation.h>
#include <gmerlin/log.h>

#include <gmerlin/mdb.h>

#include <gmerlin/upnp/didl.h>

#include <httpserver_priv.h>

#define LOG_DOMAIN "httpmediahandler"

#define THREAD_THRESHOLD (1024*1024) // 1 M

/* id3 and flac tag handling */

typedef struct
  {
  char * filename;
  int64_t off;
  int64_t len;
  bg_http_server_t * s;
  
  header_t h;
  
  } media_handler_t;

static header_t * header_get(bg_http_server_t * srv, const gavl_dictionary_t * m, char * filename);

static void thread_func_media(bg_http_connection_t * conn, void * priv)
  {
  int64_t off;
  int64_t len;
  
  media_handler_t * handler = priv;

  //  fprintf(stderr, "bg_socket_send_file %s\n", handler->filename);

  /* Send header */
  
  off = handler->off;
  
  if((handler->h.buf.len > 0) && (off < handler->h.buf.len))
    {
    len = handler->h.buf.len - off;

    if(len > handler->len)
      len = handler->len;
    
    gavl_socket_write_data(conn->fd, handler->h.buf.buf + off, len);

    handler->off += len;
    handler->len -= len;
    }
  
  /* Send file */

  off = handler->off - handler->h.buf.len + handler->h.offset;
  len = handler->len;

  if(len > 0)
    {
    if(!gavl_socket_send_file(conn->fd, handler->filename, off, len))
      bg_http_connection_clear_keepalive(conn);
    }
  
  bg_http_server_put_connection(handler->s, conn);

  bg_http_connection_free(conn);
  
  //  fprintf(stderr, "gavl_socket_send_file done\n");
  }

static void free_data(media_handler_t * data)
  {
  free(data->filename);
  }

static void cleanup(void * data)
  {
  free_data(data);
  free(data);
  }

static int handle_http_mediafile(bg_http_connection_t * conn, void * data)
  {
  int result = 0;
  char * local_path = NULL;
  char * local_path_enc = NULL;
  char * pos;
  const char * mimetype;
  const char * klass = NULL;
  const char * transferMode = NULL;
  const char * fragment = NULL;
  bg_http_server_t * s = data;

  media_handler_t mh;

  media_handler_t * mhp;
  int64_t total_bytes;
  const char * range;
  const char * var;
  header_t * h = NULL;
  
  int64_t start_byte = 0;
  int64_t end_byte = 0;
  struct stat st;
  gavl_dictionary_t * mi = NULL;
  gavl_dictionary_t * metadata = NULL;
  gavl_dictionary_t * track = NULL;
  gavl_time_t duration = GAVL_TIME_UNDEFINED;
  
  if(!(local_path_enc = bg_media_dirs_http_to_local(s->dirs, conn->path)))
    return 0; // Not our business
  
  //  fprintf(stderr, "Got media path: %s\n", local_path);
  
  /* Remove fragment part */
  if((pos = strrchr(local_path_enc, '#')))
    {
    fragment = pos+1; // After '#'
    *pos = '\0';
    gavl_log(GAVL_LOG_DEBUG, LOG_DOMAIN, "Got fragment: #%s", fragment);
    }
  
  local_path = bg_uri_to_string(local_path_enc, -1);
  
  /* Reject invalid methods */

  if(strcmp(conn->method, "GET") && strcmp(conn->method, "HEAD"))
    {
    /* Method not allowed */
    bg_http_connection_init_res(conn, "HTTP/1.1", 
                                405, "Method Not Allowed");
    goto go_on;
    }
  
  /* Check if we have a valid extension and mime-type */
  if(!(pos = strrchr(local_path, '.')))
    {
    bg_http_connection_init_res(conn, "HTTP/1.1", 404, "Not Found");
    goto go_on;
    }
  if(!(mimetype = bg_ext_to_mimetype(pos + 1)))
    {
    bg_http_connection_init_res(conn, "HTTP/1.1", 401, "Forbidden");
    goto go_on;
    }
  
  /* Check if the file exists and we can access it */

  if(stat(local_path, &st))
    {
    if(errno == EACCES)
      bg_http_connection_init_res(conn, "HTTP/1.1", 401, "Forbidden");
    else if(errno == ENOENT)
      bg_http_connection_init_res(conn, "HTTP/1.1", 404, "Not Found");
    else
      bg_http_connection_init_res(conn, "HTTP/1.1", 400, "Bad Request");
    goto go_on;
    }

  if(!(st.st_mode & S_IROTH))
    {
    bg_http_connection_init_res(conn, "HTTP/1.1", 401, "Forbidden");
    goto go_on;
    }
  
  /* Load media info */
  mi = bg_plugin_registry_load_media_info(bg_plugin_reg, local_path, 0);
  
  if(mi)
    {
    if((gavl_get_num_tracks(mi) == 1) &&
       (track = gavl_get_track_nc(mi, 0)) &&
       (metadata = gavl_track_get_metadata_nc(track)))
      {
      gavl_dictionary_get_long(metadata, GAVL_META_APPROX_DURATION, &duration);
      if(!duration)
        duration = GAVL_TIME_UNDEFINED;
      
      klass = gavl_dictionary_get_string(metadata, GAVL_META_CLASS);
      
      /* Cover */

      if(gavl_string_starts_with(klass, GAVL_META_CLASS_AUDIO_FILE) &&
         gavl_dictionary_get(metadata, GAVL_META_COVER_URL) &&
         !gavl_dictionary_get(metadata, GAVL_META_COVER_EMBEDDED))
        {
        if(s->mdb)
          bg_mdb_get_thumbnails(s->mdb, track);

        /* Get header including cover */
        h = header_get(s, metadata, local_path);
        }
      
      //      fprintf(stderr, "Got media info:\n");
      //      gavl_dictionary_dump(mi, 2);
      //      fprintf(stderr, "\n");
      }
    }
  
  total_bytes = st.st_size;

  if(h)
    {
    total_bytes -= h->offset;
    total_bytes += h->buf.len;
    }
  
  /* Check transfer mode */

  if((var = gavl_dictionary_get_string(&conn->req, "transferMode.dlna.org")))
    {
    if(klass)
      {
      if(!strcmp(klass, GAVL_META_CLASS_AUDIO_BROADCAST) ||
         !strcmp(klass, GAVL_META_CLASS_VIDEO_BROADCAST))
        {
        /* Only streaming */
        if(!strcmp(var, "Streaming"))
          transferMode = var;
        
        }
      else if(gavl_string_starts_with(klass, GAVL_META_CLASS_AUDIO_FILE) ||
              gavl_string_starts_with(klass, GAVL_META_CLASS_VIDEO_FILE))
        {
        /* Streaming and background */

        if(!strcmp(var, "Streaming") || !strcmp(var, "Background"))
          transferMode = var;
        
        }
      else // Images
        {
        /* Always interactive */
        if(!strcmp(var, "Interactive"))
          transferMode = var;
        }

      if(!transferMode)
        {
        bg_http_connection_init_res(conn, "HTTP/1.1", 406, "Not Acceptable");
        goto go_on;
        }
      }
    }
  
  /* Check for bytes range */

  memset(&mh, 0, sizeof(mh));
  mh.filename = gavl_strdup(local_path);
  mh.s = s;
  
  if((range = gavl_dictionary_get_string_i(&conn->req, "Range")))
    {
    if(!gavl_string_starts_with(range, "bytes="))
      {
      bg_http_connection_init_res(conn, "HTTP/1.1", 400, "Bad Request");
      goto go_on;
      }

    range += 6;

    if(sscanf(range, "%"PRId64"-%"PRId64, &start_byte, &end_byte) < 2)
      {
      if(sscanf(range, "%"PRId64"-", &start_byte) == 1)
        end_byte = total_bytes - 1;
      else
        {
        bg_http_connection_init_res(conn, "HTTP/1.1", 400, "Bad Request");
        goto go_on;
        }
      }

    if((start_byte < 0) ||
       (end_byte < 0) ||
       (end_byte < start_byte) ||
       (end_byte > total_bytes - 1))
      {
      bg_http_connection_init_res(conn, "HTTP/1.1", 
                                  416, "Requested range not satisfiable");
      goto go_on;
      }
    mh.off = start_byte;
    mh.len = end_byte - start_byte + 1;
    }
  else
    {
    mh.off = 0;
    mh.len = total_bytes;
    }

  if(h)
    {
    gavl_buffer_copy(&mh.h.buf, &h->buf);
    mh.h.offset = h->offset;
    }
  
  /* Send response header */

  if(range)
    bg_http_connection_init_res(conn, "HTTP/1.1", 206, "Partial Content");
  else
    bg_http_connection_init_res(conn, "HTTP/1.1", 200, "OK");

  /* dlna content features */
  
  if(metadata && (var = gavl_dictionary_get_string(&conn->req, "getcontentFeatures.dlna.org")) &&
     (atoi(var) == 1))
    {
    gavl_dictionary_t * http_uri;
    char * content_features = NULL;

    /* Add a temporary http uri so we can obtain the content features. */
    http_uri = gavl_metadata_add_src(metadata, GAVL_META_SRC, NULL, NULL); 
    
    gavl_dictionary_copy(http_uri, gavl_metadata_get_src(metadata, GAVL_META_SRC, 0, NULL, NULL));
    gavl_dictionary_set_string_nocopy(http_uri, GAVL_META_URI, bg_sprintf("%s%s", s->root_url, conn->path));
    
    if((content_features = bg_get_dlna_content_features(track, http_uri, 1, 0)))
      gavl_dictionary_set_string_nocopy(&conn->res, "contentFeatures.dlna.org", content_features);
    }

  if(transferMode)
    gavl_dictionary_set_string(&conn->res, "transferMode.dlna.org", transferMode);
  
  if(duration != GAVL_TIME_UNDEFINED)
    gavl_dictionary_set_string_nocopy(&conn->res, "X-Content-Duration",
                                      bg_sprintf("%f", gavl_time_to_seconds(duration)));
  
  gavl_dictionary_set_string_nocopy(&conn->res, "Server", bg_upnp_make_server_string());
  bg_http_header_set_date(&conn->res, "Date");
  gavl_dictionary_set_string(&conn->res, "Accept-Ranges", "bytes");
  gavl_dictionary_set_string(&conn->res, "Content-Type", mimetype);
  
  if(range)
    {
    gavl_dictionary_set_long(&conn->res, "Content-Length", end_byte - start_byte + 1);
    
    gavl_dictionary_set_string_nocopy(&conn->res, "Content-Range",
                                      bg_sprintf("bytes %"PRId64"-%"PRId64"/%"PRId64, start_byte, end_byte,
                                                 total_bytes));
    }
  else
    gavl_dictionary_set_long(&conn->res, "Content-Length", total_bytes);

  bg_http_connection_check_keepalive(conn);
  
  //  gavl_dictionary_set_string(&conn->res, "Cache-control", "no-cache");
  
  if(!bg_http_server_write_res(s, conn))
    goto go_on;

  if(!strcmp(conn->method, "HEAD"))
    {
    result = 1;
    goto go_on; // Actually this isn't a fail condition
    }
  
  /* Decide whether to launch a thread */
  if(gavl_string_starts_with(mimetype, "image/") &&
     (total_bytes <= THREAD_THRESHOLD))
    {
    thread_func_media(conn, &mh);
    free_data(&mh);
    }
  else
    {
    mhp = calloc(1, sizeof(*mhp));
    memcpy(mhp, &mh, sizeof(mh));
    bg_http_server_create_client_thread(s, thread_func_media, cleanup, conn, mhp);
    }
    
  result = 1;
  go_on:

  if(!result)
    bg_http_server_write_res(s, conn);
  
  if(local_path)
    free(local_path);
  
  if(local_path_enc)
    free(local_path_enc);

  if(mi)
    gavl_dictionary_destroy(mi);
  
  return 1;
  }

void bg_http_server_init_mediafile_handler(bg_http_server_t * s)
  {
  if(!bg_plugin_reg)
    {
    fprintf(stderr,"BUG: bg_http_server_init_mediafile_handler called without plugin_reg)");
    return;
    }

  bg_http_server_add_handler(s,handle_http_mediafile, BG_HTTP_PROTO_HTTP,
                             NULL, // E.g. /static/ can be NULL
                             s);
  }

static int header_get_flac(header_t * ret, const gavl_dictionary_t * m, char * filename)
  {
  uint8_t buf[4];
  int result = 0;
  FILE * file = NULL;
  int64_t last_tag;
  gavl_io_t * output = NULL;
  int len;
  const gavl_dictionary_t * image_uri;
  gavl_buffer_t buffer;
  
  image_uri = gavl_dictionary_get_image_max(m, GAVL_META_COVER_URL,
                                            600, 600, "image/jpeg");

  gavl_buffer_init(&buffer);
  
  if(!image_uri)
    goto fail;

  output = gavl_io_create_buffer_write(&buffer);
  if(!bg_flac_cover_tag_write(output, image_uri, 1))
    goto fail;
  
  /* Obtain size */
  file = fopen(filename, "rb");
  
  // "flac"
  // 8 bit type (0x80 = last)
  // 24 bit length

  fseek(file, 4, SEEK_SET); // "flac"
  
  while(1)
    {
    last_tag = ftell(file);
    
    if(fread(buf, 1, 4, file) < 4)
      {
      fclose(file);
      break;
      }
    
    len = GAVL_PTR_2_24BE(&buf[1]);
    
    fseek(file, len, SEEK_CUR);

    if((buf[0] & 0x7f) == 0x06) // Cover already included
      goto fail;
    
    if(buf[0] & 0x80)
      break;
    }
  ret->offset = ftell(file);

  gavl_buffer_alloc(&ret->buf, ret->offset);

  fseek(file, 0, SEEK_SET);
  if(fread(ret->buf.buf, 1, ret->offset, file) < ret->offset)
    goto fail;

  ret->buf.buf[last_tag] &= ~0x80;

  ret->buf.len = ret->offset;
  
  gavl_buffer_append(&ret->buf, &buffer);
  
  result = 1;
  fail:
  
  if(file)
    fclose(file);
  
  if(output)
    gavl_io_destroy(output);

  gavl_buffer_free(&buffer);
  
  return result;
  }

static int header_get_mp3(header_t * ret, const gavl_dictionary_t * m, char * filename)
  {
  uint8_t buf[10];
  bg_id3v2_t * id3 = NULL;
  gavl_io_t * output = NULL;
  FILE * file = NULL;
  int result = 0;
  
  file = fopen(filename, "rb");

  if(fread(buf, 1, 10, file) < 10)
    {
    return 0;
    }
  
  if((buf[0] == 'I') &&
     (buf[1] == 'D') &&
     (buf[2] == '3'))
    {
    ret->offset = (uint32_t)buf[6] << 24;
    ret->offset >>= 1;
    ret->offset |= (uint32_t)buf[7] << 16;
    ret->offset >>= 1;
    ret->offset |= (uint32_t)buf[8] << 8;
    ret->offset >>= 1;
    ret->offset |= (uint32_t)buf[9];
    ret->offset += 10;
    }
  
  if(!(id3 = bg_id3v2_create(m, 1)))
    goto fail;

  output = gavl_io_create_buffer_write(&ret->buf);
  
  if(!bg_id3v2_write(output, id3, BG_ID3_ENCODING_UTF8))
    goto fail;
  
  result = 1;
  
  fail:
  
  if(id3)
    bg_id3v2_destroy(id3);
  
  if(file)
    fclose(file);
  
  if(output)
    gavl_io_destroy(output);
  
  return result; 
  }

static header_t * header_get(bg_http_server_t * srv, const gavl_dictionary_t * m, char * filename)
  {
  const char * format;
  const gavl_dictionary_t * dict;
  int i;
  header_t h;

  memset(&h, 0, sizeof(h));
  
  for(i = 0; i < srv->num_headers; i++)
    {
    if(!strcmp(srv->headers[i].uri, filename))
      {
      /* Move to first place in the array */

      if(i > 0)
        {
        header_t tmp;
        memcpy(&tmp, &srv->headers[i], sizeof(tmp));
        
        if(i < srv->num_headers - 1)
          memmove(&srv->headers[i], &srv->headers[i+1], (srv->num_headers - 1 - i) * sizeof(tmp));
        
        memmove(&srv->headers[1], &srv->headers[0], (srv->num_headers - 1) * sizeof(tmp));
        memcpy(&srv->headers[0], &tmp, sizeof(tmp));
        }
      
      return &srv->headers[0];
      }
    }

  /* Load header from file */
  
  if((dict = gavl_metadata_get_src(m, GAVL_META_SRC, 0, NULL, NULL)) &&
     (format = gavl_dictionary_get_string(dict, GAVL_META_FORMAT)))
    {
    if(!strcmp(format, GAVL_META_FORMAT_MP3))
      {
      if(!header_get_mp3(&h, m, filename))
        return NULL;

      h.uri = gavl_strdup(filename);
      }
    else if(!strcmp(format, GAVL_META_FORMAT_FLAC))
      {
      if(!header_get_flac(&h, m, filename))
        return NULL;

      h.uri = gavl_strdup(filename);
      }
    else
      return NULL;
    }
  else
    return NULL;
  
  /* Delete last */
  
  if(srv->num_headers == NUM_HEADERS)
    {
    bg_http_server_free_header(&srv->headers[srv->num_headers-1]);
    srv->num_headers--;
    }
  memmove(&srv->headers[1], &srv->headers[0], srv->num_headers * sizeof(h));
  memcpy(&srv->headers[0], &h, sizeof(h));
  return &srv->headers[0];
  }

