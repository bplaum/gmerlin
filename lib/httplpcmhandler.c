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


#include <config.h>

#include <gavl/numptr.h>


#include <gmerlin/utils.h>
#include <gmerlin/mdb.h>

#include <gmerlin/httpserver.h>
#include <gmerlin/lpcm_handler.h>
#include <gmerlin/upnp/upnputils.h>
#include <gmerlin/upnp/didl.h>

#include <gmerlin/translation.h>
#include <gmerlin/log.h>

#define LOG_DOMAIN "lpcmhandler"

#include <httpserver_priv.h>

#define LPCM_PATH "/lcpm"
#define WAV_PATH "/wav"

#define LPCM_VAR_SAMPLERATE "rate"
#define LPCM_VAR_CHANNELS   "channels"

#define WAV_HEADER_LEN 44
#define HEADER_MAX_LEN WAV_HEADER_LEN


#define FORMAT_WAV  1
#define FORMAT_LPCM 2

// http://host:port/lpcm<id>?channels=2&rate=48000

// http://host:port/wav<id>.wav

struct bg_lpcm_handler_s
  {
  bg_http_server_t * srv;
  };


#define SET_FOURCC(dst, c) memcpy(dst, c, 4); dst+=4

static int build_wav_header(uint8_t * ret, uint32_t total_samples, int num_channels, int samplerate)
  {
  uint32_t u;
  int block_align = num_channels * 2;
  
  SET_FOURCC(ret, "RIFF");

  if(!total_samples)
    {
    GAVL_32LE_2_PTR(0, ret); ret+= 4;
    }
  else
    {
    uint32_t len = WAV_HEADER_LEN + total_samples * block_align - 8;
    GAVL_32LE_2_PTR(len, ret); ret+= 4;
    }
  SET_FOURCC(ret, "WAVE");
  SET_FOURCC(ret, "fmt ");
   
  GAVL_32LE_2_PTR(16, ret); ret+= 4;
  GAVL_16LE_2_PTR(0x0001, ret); ret+= 2;
  GAVL_16LE_2_PTR(num_channels, ret); ret+= 2;
  GAVL_32LE_2_PTR(samplerate, ret); ret+= 4;

  u = block_align * samplerate;

  GAVL_32LE_2_PTR(u, ret); ret+= 4;
  GAVL_16LE_2_PTR(block_align, ret); ret+= 2;
  GAVL_16LE_2_PTR(16, ret); ret+= 2;
  
  SET_FOURCC(ret, "data");

  if(!total_samples)
    {
    GAVL_32LE_2_PTR(0, ret);
    }
  else
    {
    uint32_t len = total_samples * block_align;
    GAVL_32LE_2_PTR(len, ret);
    }
  return 1;
  }

static void thread_func(bg_http_connection_t * conn, void * priv, int format)
  {
  int result = 0;
  char * id = NULL;
  gavl_dictionary_t track;
  gavl_dictionary_t url_vars;
  
  bg_lpcm_handler_t * h = priv;  

  gavl_source_status_t st;
  
  bg_plugin_handle_t * handle = NULL;
  gavl_audio_source_t * asrc;
  gavl_audio_frame_t * f;
  int uri_idx = 0;

  const gavl_dictionary_t * src;
  const char * location;
  const char * var;
  gavl_audio_format_t fmt;
  const gavl_dictionary_t * m;
  const gavl_dictionary_t * src_track;
  const gavl_dictionary_t * src_m;

  int has_byte_range = 0;
  //  int has_ntp_range = 0;
  
  gavl_time_t duration;

  /* Byte range */
  int64_t byte_offset = 0;
  int64_t byte_len    = -1;
  int64_t bytes_written = 0;
  
  int64_t stream_duration = 0;
  int block_align = 0;
  int sample_accurate = 0;
  
  //  int in_rate;
  int out_rate = 0;
  int channels = 0;

  uint8_t header[HEADER_MAX_LEN];
  int header_len = 0;
  
  int num_bytes;
  gavl_dsp_context_t * dsp = NULL;

  
  gavl_dictionary_init(&track);
  
  /* Get object */

  //  fprintf(stderr, "Client thread\n");

  gavl_dictionary_init(&url_vars);

  /* Remove lpcm specifig URL-variables to get the right ID */
  id = bg_uri_to_string(conn->path, -1);
  gavl_url_get_vars(id, &url_vars);
  gavl_dictionary_set(&url_vars, LPCM_VAR_SAMPLERATE, NULL);
  gavl_dictionary_set(&url_vars, LPCM_VAR_CHANNELS, NULL);
  id = bg_url_append_vars(id, &url_vars);
  
  if(strcmp(conn->method, "GET") && strcmp(conn->method, "HEAD"))
    {
    /* Method not allowed */
    bg_http_connection_init_res(conn, "HTTP/1.1", 
                                405, "Method Not Allowed");
    goto fail;
    }

  if(format == FORMAT_LPCM)
    {
    if(!gavl_dictionary_get_int(&conn->url_vars, LPCM_VAR_SAMPLERATE, &out_rate) ||
       (out_rate <= 0) ||
       !gavl_dictionary_get_int(&conn->url_vars, LPCM_VAR_CHANNELS, &channels) ||
       (channels <= 0))
      {
      gavl_http_response_init(&conn->res, conn->protocol, 400, "Bad Request");
      goto fail;
      }

#ifndef WORDS_BIGENDIAN
    dsp = gavl_dsp_context_create();
#endif
    }

  if(format == FORMAT_WAV)
    {
    char * pos;
    if((pos = strrchr(id, '/')))
      *pos = '\0';
    
    header_len = WAV_HEADER_LEN;

#ifdef WORDS_BIGENDIAN
    dsp = gavl_dsp_context_create();
#endif
    }
  
  //  fprintf(stderr, "LPCM got ID: %s\n", id);
  
  if(!bg_mdb_browse_object_sync(bg_mdb_get_controllable(h->srv->mdb),
                                &track, id, 10000))
    {
    gavl_http_response_init(&conn->res, conn->protocol, 404, "Not Found");
    goto fail;
    }

  if(!(m = gavl_track_get_metadata(&track)))
    {
    gavl_http_response_init(&conn->res, conn->protocol, 500, "Internal Server Error");
    goto fail;
    }

  if((var = gavl_dictionary_get_string(&conn->req, "TimeSeekRange.dlna.org")))
    fprintf(stderr, "Got TimeSeekRange.dlna.org: %s\n", var);
  
  //  fprintf(stderr, "Got object\n");
  //  gavl_dictionary_dump(&track, 2);

  /* We don't load anything except the primary location */
  
  if(!(src = bg_plugin_registry_get_src(bg_plugin_reg, &track, &uri_idx)) ||
     (uri_idx > 0) ||
     !(location = gavl_strdup(gavl_dictionary_get_string(src, GAVL_META_URI))))
    {
    gavl_http_response_init(&conn->res, conn->protocol, 404, "Not Found");
    goto fail;
    }
  
  /* Open Object */
  handle = NULL;
  
  if(!(handle = bg_input_plugin_load_full(location)))
    {
    gavl_http_response_init(&conn->res, conn->protocol, 500, "Internal Server Error");
    goto fail;
    }
  
  bg_media_source_set_audio_action(handle->src, 0, BG_STREAM_ACTION_DECODE);

  bg_input_plugin_start(handle);
  
  asrc = bg_input_plugin_get_audio_source(handle, 0);

  if(format == FORMAT_WAV)
    {
    const gavl_audio_format_t * afmt;

    afmt = gavl_audio_source_get_src_format(asrc);

    out_rate = afmt->samplerate;
    channels = afmt->num_channels;

    if(channels > 2)
      channels = 2;

    if((out_rate != 44100) &&
       (out_rate != 48000) &&
       (out_rate != 32000))
      out_rate = 48000;
    }
  
  src_track = bg_input_plugin_get_track_info(handle, -1);
  src_m = gavl_track_get_metadata(src_track);
  
  gavl_dictionary_get_int(src_m, GAVL_META_SAMPLE_ACCURATE, &sample_accurate);
  
  if(sample_accurate)
    {
    const gavl_dictionary_t * audio_metadata = gavl_track_get_audio_metadata(src_track, 0);

    if(audio_metadata)
      gavl_dictionary_get_long(audio_metadata, GAVL_META_STREAM_DURATION, &stream_duration);
    }
  
  if(stream_duration <= 0)
    sample_accurate = 0;
  
  /* */
  
  memset(&fmt, 0, sizeof(fmt));
  
  gavl_audio_format_copy(&fmt, gavl_audio_source_get_src_format(asrc));

  if(fmt.samplerate != out_rate)
    {
    stream_duration = gavl_time_rescale(fmt.samplerate, out_rate, stream_duration);
    }
  
  fmt.sample_format   = GAVL_SAMPLE_S16;
  fmt.samplerate      = out_rate;
  fmt.num_channels    = channels;
  fmt.interleave_mode = GAVL_INTERLEAVE_ALL;
  
  /* 20 copy cycles per second */
  fmt.samples_per_frame = out_rate / 20;

  gavl_audio_source_set_dst(asrc, 0, &fmt);
  block_align = channels * 2;
  
  /* Seek */

  if(sample_accurate)
    {
    byte_offset = 0;
    byte_len = header_len + stream_duration * block_align;
    }
  
  if(sample_accurate && (var = gavl_dictionary_get_string(&conn->req, "Range")))
    {
    int64_t start_byte;
    int64_t end_byte;
    
    has_byte_range = 1;

    if(!gavl_string_starts_with(var, "bytes="))
      {
      bg_http_connection_init_res(conn, "HTTP/1.1", 400, "Bad Request");
      goto fail;
      }

    var += 6;

    if(sscanf(var, "%"PRId64"-%"PRId64, &start_byte, &end_byte) < 2)
      {
      if(sscanf(var, "%"PRId64"-", &start_byte) == 1)
        end_byte = stream_duration * block_align - 1;
      else
        {
        bg_http_connection_init_res(conn, "HTTP/1.1", 400, "Bad Request");
        goto fail;
        }
      }

    if((start_byte < 0) ||
       (end_byte < 0) ||
       (end_byte < start_byte) ||
       (end_byte > stream_duration * block_align - 1))
      {
      bg_http_connection_init_res(conn, "HTTP/1.1", 
                                  416, "Requested range not satisfiable");
      goto fail;
      }

    byte_offset = start_byte;
    byte_len = end_byte - start_byte + 1;
    
    //    mh.off = start_byte;
    //    mh.len = end_byte - start_byte + 1;
    
    }
  else if((var = gavl_dictionary_get_string(&conn->req, "TimeSeekRange.dlna.org")))
    {
    /* TODO: Parse byte range and convert to time range */
    
    //&    has_ntp_range = 1;
    
    }

  /* Seek source */

  if(byte_offset > header_len)
    {
    int64_t seek_time = (byte_offset-header_len) / block_align;

    //    fprintf(stderr, "LPCM seek 1: %"PRId64"\n", seek_time);
    
    bg_input_plugin_seek(handle, seek_time, out_rate);
    
    //    fprintf(stderr, "LPCM seek 2: %"PRId64"\n", seek_time);
    }
  
  /* Send reply */

  if(has_byte_range)
    {
    bg_http_connection_init_res(conn, "HTTP/1.1", 206, "Partial Content");
    }
  else
    {
    bg_http_connection_init_res(conn, "HTTP/1.1", 200, "OK");
    }
  
  gavl_dictionary_set_string_nocopy(&conn->res, "Server", bg_upnp_make_server_string());
  gavl_http_header_set_date(&conn->res, "Date");

  if(format == FORMAT_LPCM)
    gavl_dictionary_set_string_nocopy(&conn->res, "Content-Type",
                                      gavl_sprintf("audio/L16;%s=%d;%s=%d",
                                                 LPCM_VAR_SAMPLERATE, out_rate,
                                                 LPCM_VAR_CHANNELS, channels));
  else if(format == FORMAT_WAV)
    gavl_dictionary_set_string(&conn->res, "Content-Type", "audio/wav");

  
  if(sample_accurate)
    {
    if(has_byte_range)
      {
      gavl_dictionary_set_long(&conn->res, "Content-Length", byte_len);
      
      gavl_dictionary_set_string_nocopy(&conn->res, "Content-Range",
                                        gavl_sprintf("bytes %"PRId64"-%"PRId64"/%"PRId64,
                                                   byte_offset,
                                                   byte_offset + byte_len - 1,
                                                   stream_duration * block_align));
      }
    else
      gavl_dictionary_set_long(&conn->res, "Content-Length", header_len + stream_duration * block_align);

    gavl_dictionary_set_string(&conn->res, "Accept-Ranges", "bytes");
    }
  else
    gavl_dictionary_set_string(&conn->res, "Accept-Ranges", "none");
  
  if((var = gavl_dictionary_get_string_i(&conn->req, "getcontentFeatures.dlna.org")) &&
     (atoi(var) == 1))
    {
    gavl_dictionary_t uri;
    char * content_features;

    gavl_dictionary_set_string(&conn->res, "transferMode.dlna.org", "Streaming");
    gavl_dictionary_init(&uri);
    gavl_dictionary_set_string(&uri, GAVL_META_MIMETYPE,
                               gavl_dictionary_get_string(&conn->res, "Content-Type"));
    
    gavl_dictionary_set_string_nocopy(&uri, GAVL_META_URI, gavl_sprintf("http://localhost:123%s/%s", LPCM_PATH, conn->path));
    
    if((content_features = bg_get_dlna_content_features(src_track, &uri, sample_accurate, 0)))
      gavl_dictionary_set_string_nocopy(&conn->res, "contentFeatures.dlna.org", content_features);

    gavl_dictionary_free(&uri);
    }

  if(gavl_dictionary_get_long(m, GAVL_META_APPROX_DURATION, &duration) &&
     (duration != GAVL_TIME_UNDEFINED))
    {
    //    char * dur_str;
    //    char * range;
    
    gavl_dictionary_set_string_nocopy(&conn->res, "X-Content-Duration",
                                      gavl_sprintf("%f", gavl_time_to_seconds(duration)));

    //    range = bg_npt_range_to_string(0, duration-GAVL_TIME_SCALE/1000);
    //    gavl_dictionary_set_string_nocopy(&conn->res, "X-AvailableSeekRange",
    //                                      gavl_sprintf("1 ntp=%s", range));

    //    dur_str = bg_npt_to_string(duration);
    
    //    gavl_dictionary_set_string_nocopy(&conn->res, "TimeSeekRange.dlna.org",
    //                                      gavl_sprintf("ntp=%s/%s", range, dur_str));
    
    //    free(range);
    //    free(dur_str);
    }

#if 0  
  fprintf(stderr, "LPCM request:\n");
  gavl_dictionary_dump(&conn->req, 2);
  
  fprintf(stderr, "LPCM response:\n");
  gavl_dictionary_dump(&conn->res, 2);
#endif
  // TimeSeekRange.dlna.org npt=00:05:35.3-00:05:37.5 
  
  bg_http_connection_write_res(conn);
  
  /* Send data (skipped for HEAD request) */

  if(!strcmp(conn->method, "GET"))
    {
    /* Check whether to send the header */

    if(byte_offset < header_len)
      {
      if(format == FORMAT_WAV)
        {
        if(sample_accurate)
          build_wav_header(header, stream_duration, channels, out_rate);
        else
          build_wav_header(header, 0, channels, out_rate);
        }

      num_bytes = header_len - byte_offset;
      
      if((byte_len > 0) && (num_bytes > byte_len))
        num_bytes = byte_len;
      
      bytes_written = gavl_socket_write_data(conn->fd, header + byte_offset, num_bytes);
      
      if(bytes_written != num_bytes)
        goto fail;
      
      }
    
    while(1)
      {
      f = NULL;
    
      if((st = gavl_audio_source_read_frame(asrc, &f)) != GAVL_SOURCE_OK)
        break;
      
      num_bytes = f->valid_samples * block_align;

      if((byte_len > 0) && (bytes_written + num_bytes > byte_len))
        num_bytes = byte_len - bytes_written;

      if(dsp)
        gavl_dsp_audio_frame_swap_endian(dsp, f, &fmt);
      
      if(gavl_socket_write_data(conn->fd, f->samples.u_8, num_bytes) < num_bytes)
        break;

      bytes_written += num_bytes;

      if((byte_len > 0) && (bytes_written >= byte_len))
        break;
      }
    }
  
  result = 1;
  
  fail:

  if(dsp)
    gavl_dsp_context_destroy(dsp);
  
  if(!result)
    {
    /* Send response (if not already done) */
    bg_http_server_write_res(h->srv, conn);
    }
  
  if(handle)
    bg_plugin_unref(handle);
  
  gavl_dictionary_free(&track);
  if(id)
    free(id);
  
  bg_http_server_put_connection(h->srv, conn);
  bg_http_connection_free(conn);
  }

static void thread_func_lpcm(bg_http_connection_t * conn, void * priv)
  {
  thread_func(conn, priv, FORMAT_LPCM);
  }

static void thread_func_wav(bg_http_connection_t * conn, void * priv)
  {
  thread_func(conn, priv, FORMAT_WAV);
  }

static int handle_http_lpcm(bg_http_connection_t * conn, void * data)
  {
  bg_lpcm_handler_t * h = data;

  if(*conn->path != '/')
    return 0;
  
  //  fprintf(stderr, "handle_http_lpcm: %s\n", conn->path);
  //  gavl_dictionary_dump(&conn->url_vars, 2);

  bg_http_server_create_client_thread(h->srv, thread_func_lpcm, NULL, conn, data);
  
  return 0;
  }

static int handle_http_wav(bg_http_connection_t * conn, void * data)
  {
  bg_lpcm_handler_t * h = data;

  if(*conn->path != '/')
    return 0;
  
  //  fprintf(stderr, "handle_http_lpcm: %s\n", conn->path);
  //  gavl_dictionary_dump(&conn->url_vars, 2);

  bg_http_server_create_client_thread(h->srv, thread_func_wav, NULL, conn, data);
  
  return 0;
  }

bg_lpcm_handler_t * bg_lpcm_handler_create(bg_mdb_t * mdb, bg_http_server_t * srv)
  {
  bg_lpcm_handler_t * ret;
  ret = calloc(1, sizeof(*ret));
  ret->srv = srv;
  bg_http_server_add_handler(srv, handle_http_lpcm, BG_HTTP_PROTO_HTTP, LPCM_PATH, // E.g. /static/ can be NULL
                             ret);
  bg_http_server_add_handler(srv, handle_http_wav, BG_HTTP_PROTO_HTTP, WAV_PATH, // E.g. /static/ can be NULL
                             ret);
  return ret;
  }

void bg_lpcm_handler_destroy(bg_lpcm_handler_t * h)
  {
  free(h);
  }

static void add_uri_lpcm(gavl_dictionary_t * metadata,
                         bg_http_server_t * srv,
                         const char * id, int samplerate, int num_channels)
  {
  char * uri;
  char * mimetype;
  gavl_dictionary_t vars;
  gavl_dictionary_t * src;

  gavl_dictionary_init(&vars);
  
  //  gavl_dictionary_t * src;
  
  uri      = gavl_sprintf("%s%s%s", bg_http_server_get_root_url(srv), LPCM_PATH, id);
  
  gavl_url_get_vars(uri, &vars);
  gavl_dictionary_set_int(&vars, LPCM_VAR_SAMPLERATE, samplerate);
  gavl_dictionary_set_int(&vars, LPCM_VAR_CHANNELS, num_channels);
  uri = bg_url_append_vars(uri, &vars);
  gavl_dictionary_free(&vars);
  
  mimetype = gavl_sprintf("audio/L16;%s=%d;%s=%d", LPCM_VAR_SAMPLERATE, samplerate, LPCM_VAR_CHANNELS, num_channels);

  //  src =
  src = gavl_metadata_add_src(metadata, GAVL_META_SRC, mimetype, uri);
  gavl_dictionary_set_int(src, GAVL_META_TRANSCODED, 1);
  
  free(mimetype);
  free(uri);
  
  //  mimetype = 
  
  }

static void add_uri_wav(gavl_dictionary_t * metadata, bg_http_server_t * srv, const char * id)
  {
  char * uri;

  char * label = NULL;

  const char * location = NULL;
  const char * pos1;
  const char * pos2;

  gavl_dictionary_t * src;
  
  if(gavl_metadata_get_src(metadata, GAVL_META_SRC, 0, NULL, &location) &&
     location &&
     (pos1 = strrchr(location, '/')) &&
     (pos2 = strrchr(pos1, '.')))
    label = gavl_strndup(pos1+1, pos2);
  else
    label = gavl_strdup("file");
  
  uri = gavl_sprintf("%s%s%s/%s.wav", bg_http_server_get_root_url(srv), WAV_PATH, id, label);
  
  src = gavl_metadata_add_src(metadata, GAVL_META_SRC, "audio/wav", uri);
  gavl_dictionary_set_int(src, GAVL_META_TRANSCODED, 1);

  free(uri);
  }

void bg_lpcm_handler_add_uris(bg_lpcm_handler_t * h, gavl_dictionary_t * track)
  {
  int samplerate;
  int channels;
  const char * id;
  const char * klass;
  const char * mimetype = NULL;
  const char * uri = NULL;

  int do_lpcm = 0;
  int do_wav = 0;
  
  //  gavl_dictionary_t * src;
  gavl_dictionary_t * m = gavl_track_get_metadata_nc(track);
  
  /*
     src = 
       gavl_dictionary_t *
       gavl_metadata_add_src(gavl_dictionary_t * m, const char * key,
                          const char * mimetype, const char * location)
   */
  
  if(!(klass = gavl_dictionary_get_string(m, GAVL_META_CLASS)))
    return;
  
  if(strcmp(klass, GAVL_META_CLASS_SONG) &&
     strcmp(klass, GAVL_META_CLASS_AUDIO_BROADCAST))
    return;
  
  if(!gavl_metadata_get_src(m, GAVL_META_SRC, 0, &mimetype, &uri) ||
     !mimetype)
    return;
  
  if(!strcmp(mimetype, "audio/flac") ||
     !strcmp(mimetype, "application/x-cue"))
    {
    do_lpcm = 1;
    do_wav = 1;
    }
  else if(!strcmp(mimetype, "audio/ogg"))
    {
    do_lpcm = 1;
    return;
    }
  
  //  fprintf(stderr, "bg_lpcm_handler_add_uris\n");
  //  gavl_dictionary_dump(track, 2);
  
  if(!gavl_dictionary_get_int(m, GAVL_META_AUDIO_SAMPLERATE, &samplerate) ||
     !gavl_dictionary_get_int(m, GAVL_META_AUDIO_CHANNELS, &channels))
    return;
    
  if(!(id = gavl_track_get_id(track)))
    return;
  
  if(channels > 2)
    channels = 2;

  /* Everything except 44100 is converted to 48000 */
  if(samplerate != 44100)
    samplerate = 48000;


  if(do_wav)
    add_uri_wav(m, h->srv, id);

  if(do_lpcm)
    add_uri_lpcm(m, h->srv, id, samplerate, channels);
  
  //  fprintf(stderr, "bg_lpcm_handler_add_uris\n");
  //  gavl_dictionary_dump(m, 2);
  //  fprintf(stderr, "\n");
  }

