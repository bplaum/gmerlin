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

#ifndef BGPLUG_H_INCLUDED
#define BGPLUG_H_INCLUDED

#include <gavl/gavf.h>
#include <gavl/connectors.h>

#include <gmerlin/parameter.h>
#include <gmerlin/pluginregistry.h>
#include <gmerlin/mediaconnector.h>

#define BG_PLUG_PROTOCOL "BGPLUG/1.0"
#define BG_PLUG_MULTI_HEADER "PLUGMDHR"

/* The plug protocol is as follows:

   From the source to the destination, the media data are transferred
   in terms of a gavf stream.

   Control messages from source to destination are transported in the
   special stream ID "GAVL_META_STREAM_ID_MSG_DEMUXER".

   Control messages from destination to source (e.g. seek, select_track) are
   send in a backchannel as raw messages (as in gavl_msg_write/gavl_msg_read)

   If the connection is a socket, the backchannel is sent via the same socket.
   If the connection is a pipe or a fifo, the *src* opens a Unix-domain socket
   and passes it's address to the destination in the variable
   GAVL_META_MSG_BACK_CHANNEL_ADDRESS in the topmost metadata.

   Semantically, a plug can work in two ways:

   1. In singletrack mode, the source sends a chunk of type "GAVFPHDR". It is a
      simple gavf stream, which can be passed directly to the decoder. The
      command GAVL_MSG_NS_SRC:GAVL_CMD_SRC_SELECT_TRACK can be used but has no
      effect.

      open()
      wr -> rd: "GAVFPHDR"
      wr -> rd: GAVFPKTS

      wr -> rd: GAVFSYNC
      wr -> rd: PACKET
      wr <- rd: PACKET_ACK
      wr -> rd: PACKET
      wr <- rd: PACKET_ACK

      The stream is closed by anything but a packet- or a sync header
   
   2. In multitrack mode, the first data chunk has the type: "PLUGMHDR", which
      contains the a dictionary which you can use with gavl_get_num_tracks() and
      gavl_get_track() and friends. After opening, the source plug waits for the
      command GAVL_MSG_NS_SRC:GAVL_CMD_SRC_SELECT_TRACK. Immediately after that,
      the source sends a gavf stream as in the singletrack mode.

      open()
      wr -> rd: "PLUGMHDR"
      wr <- rd: GAVL_MSG_NS_SRC:GAVL_CMD_SRC_SELECT_TRACK

      wr -> rd: "GAVFPHDR"
      wr -> rd: GAVFPKTS

      wr -> rd: GAVFSYNC
      wr -> rd: PACKET
      wr <- rd: PACKET_ACK
      wr -> rd: PACKET
      wr <- rd: PACKET_ACK
      
*/

/* Reader */

typedef struct bg_plug_s bg_plug_t;

bg_plug_t * bg_plug_create_reader(void);
bg_plug_t * bg_plug_create_writer(void);

void bg_plug_destroy(bg_plug_t *);

int bg_plug_ping(bg_plug_t *);
int bg_plug_set_media_info(bg_plug_t * p, const gavl_dictionary_t * mi);


// int bg_plug_next_backchannel_msg(bg_plug_t * plug);

const bg_parameter_info_t *
bg_plug_get_input_parameters();

const bg_parameter_info_t *
bg_plug_get_output_parameters();



void bg_plug_set_parameter(void * data, const char * name,
                           const gavl_value_t * val);

// int bg_plug_open(bg_plug_t *, gavl_io_t * io, int io_flags);

//  int bg_plug_set_multitrack(bg_plug_t *, const gavl_dictionary_t * mi);

bg_controllable_t *
bg_plug_get_controllable(bg_plug_t *);

void bg_plug_finish_program(bg_plug_t * p, int discard);

/* Convenience fuction */
void bg_plug_select_track(bg_plug_t * p, int track);

int bg_plug_set_from_track(bg_plug_t *,
                           const gavl_dictionary_t * track);

int bg_plug_open_location(bg_plug_t * p, const char * location);

#if 0
int bg_plug_select_track(bg_plug_t * p, const gavl_msg_t * msg);
int bg_plug_select_track_by_idx(bg_plug_t * p, int track);


int bg_plug_seek(bg_plug_t * p, const gavl_msg_t * msg);


void bg_plug_write_resync(bg_plug_t * plug,
                          int64_t time, int scale, int discard, int discont);
gavl_io_t * bg_plug_get_io(bg_plug_t*);
#endif

gavf_t * bg_plug_get_gavf(bg_plug_t*);


const gavl_dictionary_t * bg_plug_get_metadata(bg_plug_t*);

/* These must be called *before* the plug is openend to
   set up the message passing */
// bg_msg_hub_t * bg_plug_get_msg_hub(bg_plug_t*);
// bg_msg_sink_t * bg_plug_get_msg_sink(bg_plug_t*);

/* Initialization function for readers and writers */

int bg_plug_add_mediaconnector_stream(bg_plug_t * p,
                                      bg_mediaconnector_stream_t * s);

int bg_plug_connect_mediaconnector_stream(bg_mediaconnector_stream_t * s,
                                          bg_media_sink_stream_t * ms);

/* Set up a writer from a media connector */
// int bg_plug_setup_writer(bg_plug_t*, bg_mediaconnector_t * conn);

int bg_plug_start_program(bg_plug_t*, const bg_media_source_t * src);

/* Set up a media connector from a reader */
int bg_plug_setup_reader(bg_plug_t*, bg_mediaconnector_t * conn);


/* Needs to be called before any I/O is done */
int bg_plug_start(bg_plug_t * p);


/* Add streams for encoding, on the fly compression might get applied */

int bg_plug_add_stream(bg_plug_t * p, const gavl_dictionary_t * dict, gavl_stream_type_t type);

int bg_plug_add_msg_stream(bg_plug_t * p,
                           const gavl_dictionary_t * s, int id);

bg_media_source_t * bg_plug_get_source(bg_plug_t * p);
bg_media_sink_t * bg_plug_get_sink(bg_plug_t * p);


gavl_sink_status_t bg_plug_put_packet(bg_plug_t * p,
                                      gavl_packet_t * pkt);

gavl_dictionary_t * bg_plug_get_media_info(bg_plug_t * p);
gavl_dictionary_t * bg_plug_get_current_track(bg_plug_t * p);

// int bg_plug_got_error(bg_plug_t * p);


/* I/O Stuff */

#define BG_PLUG_IO_IS_LOCAL   (1<<0)
#define BG_PLUG_IO_IS_REGULAR (1<<1)
#define BG_PLUG_IO_IS_SOCKET  (1<<2)
#define BG_PLUG_IO_IS_PIPE    (1<<3) // Will be redirected to Unix-Socket

#define BG_PLUG_IO_METHOD_READ  0
#define BG_PLUG_IO_METHOD_WRITE 1
#define BG_PLUG_IO_METHOD_HEAD  2 // Use only in the http protocol

#define BG_PLUG_IO_STATUS_100 100 // Continue
#define BG_PLUG_IO_STATUS_200 200 // OK
#define BG_PLUG_IO_STATUS_400 400 // Bad Request
#define BG_PLUG_IO_STATUS_404 404 // Not found
#define BG_PLUG_IO_STATUS_405 405 // Method Not Allowed
#define BG_PLUG_IO_STATUS_415 415 // Unsupported Media Type
#define BG_PLUG_IO_STATUS_417 417 // Expectation Failed

#define BG_PLUG_MSG_SRC_METADATA_CHANGED 1
#define BG_PLUG_MSG_SRC_ 1

#if 0
#define BG_PLUG_SCHEMA_UNIX "gavf-unix"
#define BG_PLUG_PREFIX_UNIX BG_PLUG_SCHEMA_UNIX"://"

#define BG_PLUG_SCHEMA_UNIXSERV "gavf-unixserv"
#define BG_PLUG_PREFIX_UNIXSERV BG_PLUG_SCHEMA_UNIXSERV"://"

#define BG_PLUG_SCHEMA_TCP "gavf-tcp"
#define BG_PLUG_PREFIX_TCP BG_PLUG_SCHEMA_TCP"://"

#define BG_PLUG_SCHEMA_TCPSERV "gavf-tcpserv"
#define BG_PLUG_PREFIX_TCPSERV BG_PLUG_SCHEMA_TCPSERV"://"

#define BG_PLUG_SCHEMA "gavf"
#define BG_PLUG_PREFIX BG_PLUG_SCHEMA"://"
#endif

extern const char * bg_plug_app_id; // User-Agent and Server

// extern char const * const bg_plug_mimetype;

/* Called by bg_plug_open_location */

gavl_io_t *
bg_plug_io_open_location(const char * location,
                         int method, int * flags, int timeout);

int bg_plug_io_server_handshake(gavl_io_t *, int method, const gavl_dictionary_t * req, const char * path);

#if 0
gavl_io_t *
bg_plug_io_open_socket(int fd, int * flags, int timeout);

gavl_io_t *
bg_plug_io_open_socket_noclose(int fd, int method, int * flags, int timeout);
#endif


/* bgplug network protocol primitives */

int 
bg_plug_request_get_method(const gavl_dictionary_t * req, int * metod);

bg_plugin_handle_t * bg_input_plugin_create_plug(void);

bg_plugin_info_t * bg_plug_input_get_info();

extern char const * const bg_plug_plugin_protocols;

#endif // BGPLUG_H_INCLUDED
