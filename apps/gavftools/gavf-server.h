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

#include "gavftools.h"

#define DUMP_HTTP_HEADERS

#define CLIENT_TIMEOUT 5000

/* Buffer */

#define BUFFER_TYPE_PACKET      0
#define BUFFER_TYPE_METADATA    1
#define BUFFER_TYPE_SYNC_HEADER 2

typedef struct buffer_element_s
  {
  int type;
  gavl_packet_t p;
  gavl_dictionary_t m;

  int64_t seq; // Sequence number

  struct buffer_element_s * next;
  } buffer_element_t;

void buffer_element_set_sync_header(buffer_element_t * el);

void buffer_element_set_packet(buffer_element_t * el,
                               const gavl_packet_t * p);

void buffer_element_set_metadata(buffer_element_t * el,
                                 const gavl_dictionary_t * m);

typedef struct buffer_s buffer_t;


buffer_t * buffer_create(int num_elements);

void buffer_destroy(buffer_t *);

buffer_element_t * buffer_get_write(buffer_t *);
void buffer_done_write(buffer_t *);

int buffer_get_read(buffer_t *, int64_t * seq, buffer_element_t ** el);

void buffer_wait(buffer_t * b);

// void buffer_done_read(buffer_t *, buffer_element_t *);

buffer_element_t * buffer_get_first(buffer_t *);

int64_t buffer_get_start_seq(buffer_t *);

void buffer_advance(buffer_t *);

void buffer_stop(buffer_t * b);

int buffer_get_free(buffer_t * b);

/* Filter: Outputs data to the client socket */

typedef struct
  {
  int is_supported;
  int (*supported)();
  
  int (*probe)(const gavf_program_header_t * ph,
               const gavl_dictionary_t * request);
  
  void * (*create)(const gavf_program_header_t * ph,
                   const gavl_dictionary_t * req,
                   const gavl_dictionary_t * vars,
                   const gavl_dictionary_t * inline_metadata,
                   gavl_dictionary_t * res);

  int (*start)(void * priv,
               const gavf_program_header_t * ph,
               const gavl_dictionary_t * req,
               const gavl_dictionary_t * vars,
               const gavl_dictionary_t * inline_metadata,
               gavf_io_t * io, int flags);
  
  int (*put_buffer)(void * priv, buffer_element_t *);
  void (*destroy)(void * priv);
  } filter_t;

void filter_init();

const filter_t * filter_find(const gavf_program_header_t * ph,
                             const gavl_dictionary_t * request);

/* Client (= listener) connection */

#define CLIENT_STATUS_STARTING     0
#define CLIENT_STATUS_WAIT_SYNC    1
#define CLIENT_STATUS_RUNNING      2
#define CLIENT_STATUS_DONE         3
#define CLIENT_STATUS_STOP         4

typedef struct
  {
  buffer_t * buf;

  pthread_mutex_t status_mutex;
  pthread_mutex_t seq_mutex;
  int64_t seq;
  
  int status;
  int fd;

  pthread_t thread;

  const filter_t * f;
  void * priv;
  
  } client_t;

client_t * client_create(int fd, const gavf_program_header_t * ph,
                         buffer_t * buf,
                         const gavl_dictionary_t * req,
                         gavl_dictionary_t * res,
                         const gavl_dictionary_t * url_vars,
                         const gavl_dictionary_t * inline_metadata);

void client_destroy(client_t * cl);
int client_iteration(client_t * cl);

int64_t client_get_seq(client_t * cl);
int client_get_status(client_t * cl);
void client_set_status(client_t * c, int status);

/* One program */

#define PROGRAM_STATUS_DONE    0
#define PROGRAM_STATUS_RUNNING 1
#define PROGRAM_STATUS_STOP    2

#define PROGRAM_HAVE_HEADER    (1<<0)

typedef struct
  {
  pthread_mutex_t client_mutex;
  pthread_mutex_t status_mutex;
  pthread_mutex_t metadata_mutex;
  gavl_dictionary_t m;
  int have_m; // 1 if we received a metadata update yet

  char * name;

  bg_plug_t * in_plug;
  bg_mediaconnector_t conn;
  bg_plug_t * conn_plug;
  
  int clients_alloc;
  int num_clients;
  client_t ** clients;
  
  
  int status;
  int flags;
  
  pthread_t thread;

  gavf_program_header_t ph;

  buffer_t * buf;

  gavl_timer_t * timer;
  
  } program_t;

program_t * program_create_from_socket(const char * name, int fd,
                                       const gavl_dictionary_t * url_vars);

program_t * program_create_from_plug(const char * name, bg_plug_t * plug,
                                     const gavl_dictionary_t * url_vars);

int program_get_status(program_t * p);

void program_destroy(program_t *);
void program_attach_client(program_t *, int fd,
                           const gavl_dictionary_t * req,
                           gavl_dictionary_t * res,
                           const gavl_dictionary_t * url_vars);

void program_run(program_t * p);


typedef struct
  {
  pthread_mutex_t program_mutex;

  program_t ** programs;
  int num_programs;
  int programs_alloc;
  
  int * listen_sockets;
  char ** listen_addresses;
  
  int num_listen_sockets;
    
  } server_t;

server_t * server_create(char ** listen_addresses,
                         int num_listen_addresses);

int server_iteration(server_t * s);

void server_destroy(server_t * s);

