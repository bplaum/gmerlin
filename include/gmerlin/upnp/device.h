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

#ifndef __BG_UPNP_DEVICE_H_
#define __BG_UPNP_DEVICE_H_

#include <uuid/uuid.h>
#include <gmerlin/xmlutils.h>
#include <gmerlin/bgsocket.h>
#include <gmerlin/embedplayer.h>
#include <gmerlin/httpserver.h>
#include <gmerlin/websocket.h>

#include <gmerlin/upnp/ssdp.h>

typedef struct
  {
  const char * mimetype;
  int width;
  int height;
  int depth;
  const char * location;
  } 
bg_upnp_icon_t;

typedef struct bg_upnp_device_s bg_upnp_device_t;

int
bg_upnp_device_handle_request(bg_upnp_device_t * dev, 
                              bg_http_connection_t * conn);

void
bg_upnp_device_destroy(bg_upnp_device_t * dev);

/* Call this regularly to keep things up to date */

int
bg_upnp_device_ping(bg_upnp_device_t * dev);

gavl_dictionary_t * bg_upnp_device_get_ssdp_dev(bg_upnp_device_t * dev);

/*
 *  Transcoding (on-the-fly format conversion of local files)
 */

typedef struct
  {
  const char * name;
  const char * in_mimetype;
  const char * out_mimetype;
  const char * ext;
  
  int supported;

  int (*is_supported)(bg_plugin_registry_t * plugin_reg);

  char * (*make_command)(const char * source_file, double seek_time);
  char * (*make_protocol_info)(bg_db_object_t * obj, const char * mimetype);
  void (*set_header)(bg_db_object_t * obj,
                     const gavl_dictionary_t * req,
                     gavl_dictionary_t * res, const char * mimetype);
  
  int (*get_bitrate)(bg_db_object_t * obj);
  } bg_upnp_transcoder_t;

extern const bg_upnp_transcoder_t bg_upnp_transcoders[];

const bg_upnp_transcoder_t *
bg_upnp_transcoder_by_name(const char * name);

int64_t bg_upnp_transcoder_get_size(const bg_upnp_transcoder_t * t,
                                    bg_db_object_t * obj);

char * bg_upnp_transcoder_make_protocol_info(const bg_upnp_transcoder_t * t,
                                             bg_db_object_t * obj, const char * mimetype);


/*
 * Recompressing (on-the-fly recompression of life streams)
 */

typedef struct
  {
  const char * name;
  const char * out_mimetype;
  const char * in_mimetype;
  const char * ext;
  int supported;

  int (*is_supported)(bg_plugin_registry_t * plugin_reg);
  char * (*make_command)();
  char * (*make_protocol_info)(bg_db_object_t * obj, const char * mimetype);

  int (*get_bitrate)(bg_db_object_t * obj);
  } bg_upnp_recompressor_t;

extern const bg_upnp_recompressor_t bg_upnp_recompressors[];

const bg_upnp_recompressor_t *
bg_upnp_recompressor_by_name(const char * name);

char * bg_upnp_recompressor_make_protocol_info(const bg_upnp_recompressor_t * c,
                                               bg_db_object_t * obj, const char * mimetype);

int bg_upnp_recompressor_get_bitrate(const bg_upnp_recompressor_t * c,
                                     bg_db_object_t * obj);


/* Creation routines for device types */

bg_upnp_device_t *
bg_upnp_create_media_server(bg_http_server_t * srv,
                            uuid_t uuid,
                            const char * name,
                            const bg_upnp_icon_t * icons,
                            bg_db_t * db);

bg_upnp_device_t *
bg_upnp_create_media_renderer(bg_http_server_t * srv,
                              uuid_t uuid,
                              const char * name,
                              const bg_upnp_icon_t * icons,
                              int do_audio, int do_video);

bg_control_t * bg_upnp_device_get_control(bg_upnp_device_t *);


bg_http_server_t * bg_upnp_device_get_server(bg_upnp_device_t *);

/* Client info for media servers */

/* Client understands multiple <res> elements */
#define BG_UPNP_CLIENT_MULTIPLE_RES   (1<<0)

/* Client wants the (non-standard) original location */
#define BG_UPNP_CLIENT_ORIG_LOCATION  (1<<1)  

typedef struct
  {
  const char * gmerlin;
  const char * client;
  } bg_upnp_mimetype_translation_t;

typedef struct
  {
  int (*check)(const gavl_dictionary_t * m);
  const char ** mimetypes;

  bg_upnp_mimetype_translation_t * mt;
  
  int flags;
  
  int album_thumbnail_width;
  int movie_thumbnail_width;
  } bg_upnp_client_t;

const bg_upnp_client_t * bg_upnp_detect_client(const gavl_dictionary_t * m);
int bg_upnp_client_supports_mimetype(const bg_upnp_client_t * cl,
                                     const char * mimetype);
const char *
bg_upnp_client_translate_mimetype(const bg_upnp_client_t * cl,
                                  const char * mimetype);


  
// static char * bg_upnp_id_from_upnp(const char * id)

  
extern const gavl_audio_format_t bg_upnp_playlist_stream_afmt;

char * bg_upnp_contentdirectory_browse_str(const char * url, const char * id, int self);
xmlDocPtr bg_upnp_contentdirectory_browse_xml(const char * url, const char * id, int self);


#endif // __BG_UPNP_DEVICE_H_
