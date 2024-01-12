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

#include <poll.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <uuid/uuid.h>
#include <sys/socket.h>
#include <netinet/tcp.h>

#include <config.h>

#include <gavl/numptr.h>
#include <gavl/gavl.h>
#include <gavl/connectors.h>
#include <gavl/gavlsocket.h>
#include <gavl/metatags.h>


// #include <gmerlin/http.h>
#include <gmerlin/utils.h>

#include <gmerlin/websocket.h>
#include <gmerlin/translation.h>
#include <gmerlin/log.h>
#include <gmerlin/bgplug.h>
#include <gmerlin/application.h>
#include <gmerlin/bggavl.h>

#define LOG_DOMAIN_CLIENT "websocket-client"
#define LOG_DOMAIN_SERVER "websocket-server"
#define LOG_DOMAIN "websocket"

#define BG_WEBSOCKET_MAX_CONNECTIONS 10
#define PING_PAYLOAD_LEN 8

#define PING_INTERVAL  (2*GAVL_TIME_SCALE)
#define PING_TIMEOUT  (10*GAVL_TIME_SCALE)

// #define STATE_CLOSED    0 // Must be 0 for initialization
// #define STATE_RUNNING   1
// #define STATE_FINISHED  2

//  #define DUMP_SERVER_READ
// #define DUMP_SERVER_WRITE

// #define DUMP_CLIENT_READ
// #define DUMP_CLIENT_WRITE


/**************************************************************
 * Common for server and client
 **************************************************************/
#if 0
typedef struct
  {
  gavl_buffer_t b;
  int type;
  } bg_websocket_msg_t;
#endif

typedef struct
  {
  gavl_buffer_t buf;
  uint8_t head[4+4+4+2];
  int head_len;
  int head_written;

  uint8_t * mask;
  } msg_write_t;


struct bg_websocket_connection_s
  {
  gavf_io_t * io;
  
  int is_client;

  /* handled by the thread of the context */

  gavl_time_t last_ping_time;
  
  int ping_sent;
  
  /* Control stuff */
  bg_controllable_t ctrl_client;
  bg_control_t      ctrl_server;

  gavl_timer_t * timer;
  
  struct
    {
    gavl_buffer_t buf;
    uint8_t * mask;

    uint8_t head[4+4+4+2];
    int head_len;
    int head_read; // Header bytes read so far
    
    uint64_t payload_len;
    uint64_t payload_read;
    
    } read_msg;
  
  msg_write_t * write_msg;
  
  int num_write_msg;
  int write_msg_alloc;
  
  };

static void msg_write_reset(msg_write_t * msg)
  {
  //  fprintf(stderr, "msg_write_reset\n");
  gavl_buffer_reset(&msg->buf);
  msg->head_len = 0;
  msg->head_written = 0;
  msg->mask = NULL;
  }

static void conn_reset_read_msg_segment(bg_websocket_connection_t * conn)
  {
  conn->read_msg.mask = NULL;
  conn->read_msg.head_len = 0;
  conn->read_msg.head_read = 0;
  conn->read_msg.payload_len = 0;
  conn->read_msg.payload_read = 0;
  }

static void conn_reset_read_msg(bg_websocket_connection_t * conn)
  {
  conn_reset_read_msg_segment(conn);
  gavl_buffer_reset(&conn->read_msg.buf);
  }


static void create_msg_header(bg_websocket_connection_t * conn, msg_write_t * msg, uint64_t len, int type)
  {
  uint8_t * ptr = &(msg->head[0]);
  ptr[0] = 0;
 
  ptr[0] |= (type & 0x0f); // OPCode
  ptr[0] |= 0x80; // FIN

  ptr[1] = 0;
  
  if(conn->is_client)
    ptr[1] |= 0x80; // Mask bit
  
  if(len < 126) // 7 bit
    {
    ptr[1] |= len;
    ptr += 2;
    }
  else if(len < 0x10000) // 16 bit
    {
    ptr[1] |= 126;
    ptr += 2;
    GAVL_16BE_2_PTR(len, ptr);
    ptr += 2;
    }
  else // 64 bit
    {
    ptr[1] |= 127;
    ptr += 2;
    GAVL_64BE_2_PTR(len, ptr);
    ptr += 8;
    }

  if(conn->is_client)
    {
    msg->mask = ptr;
    msg->mask[0] = rand();
    msg->mask[1] = rand();
    msg->mask[2] = rand();
    msg->mask[3] = rand();
    ptr += 4;
    }
  
  msg->head_len = ptr - &(msg->head[0]);
  }

static msg_write_t * get_msg_write(bg_websocket_connection_t * conn)
  {
  msg_write_t * ret;
  if(conn->num_write_msg == conn->write_msg_alloc)
    {
    conn->write_msg_alloc += 8;
    conn->write_msg = realloc(conn->write_msg, conn->write_msg_alloc * sizeof(*conn->write_msg));
    memset(conn->write_msg + conn->num_write_msg, 0,
           sizeof(*conn->write_msg) * (conn->write_msg_alloc - conn->num_write_msg));
    }
  ret = conn->write_msg + conn->num_write_msg;
  msg_write_reset(ret);
  conn->num_write_msg++;
  return ret;
  }

static void
msg_write(bg_websocket_connection_t * conn, const void * msg, uint64_t len, int type)
  {
  msg_write_t * write_msg;
  
  write_msg = get_msg_write(conn);

  
  create_msg_header(conn, write_msg, len, type);

  
  gavl_buffer_alloc(&write_msg->buf, len);
  memcpy(write_msg->buf.buf, msg, len);
  write_msg->buf.len = len;

#ifdef DUMP_SERVER_WRITE
  if(!conn->is_client)
    {
    fprintf(stderr, "\nmsg_write Bytes: %"PRId64"\n", len);
    fprintf(stderr, "header:\n");
    gavl_hexdump(write_msg->head, write_msg->head_len, 16);
    }
#endif

#ifdef DUMP_CLIENT_WRITE
  if(conn->is_client)
    {
    fprintf(stderr, "\nmsg_write Bytes: %"PRId64" encoding mask:\n", len);
    gavl_hexdump(write_msg->mask, 4, 4);
    fprintf(stderr, "header:\n");
    gavl_hexdump(write_msg->head, write_msg->head_len, 16);
    }
#endif

  
  if(write_msg->mask)
    {
    int i;
    
    //    gavl_hexdump(write_msg->buf.buf, len, 16);
    for(i = 0; i < len; i++)
      write_msg->buf.buf[i] ^= write_msg->mask[i % 4];
    }
  }

#define RETURN_RES \
  if(result < 0) \
    { \
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Reading data failed"); \
    return GAVL_SOURCE_EOF;                         \
    } \
  else if(!result) \
    { \
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Other side disconnected"); \
    return GAVL_SOURCE_EOF; \
    }

static int decode_msg_header(bg_websocket_connection_t * conn)
  {
  int buf_len;
  uint8_t * ptr;

  /* Decode full header */

  ptr = conn->read_msg.head;
  ptr++;

  buf_len = *ptr & 0x7f;

  ptr++;
    
  if(buf_len == 126)
    {
    conn->read_msg.payload_len = GAVL_PTR_2_16BE(ptr);
    ptr+=2;
    }
  else if(buf_len == 127)
    {
    conn->read_msg.payload_len = GAVL_PTR_2_64BE(ptr);
    ptr+=8;
    }
  else
    {
    conn->read_msg.payload_len = buf_len;
    }

  /* Masking key */
  conn->read_msg.mask = NULL;
  if(conn->read_msg.head[1] & 0x80)
    {
    conn->read_msg.mask = ptr;
    ptr += 4;
    }
  else if(!conn->is_client)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Got unmasked data");
    return 0;
    }

  gavl_buffer_alloc(&conn->read_msg.buf, conn->read_msg.buf.len + conn->read_msg.payload_len + 1);


#ifdef DUMP_CLIENT_READ
  if(conn->is_client)
    {
    fprintf(stderr, "\nmsg_read Bytes: %"PRId64"\n", conn->read_msg.payload_len);
    if(conn->read_msg.mask)
      {
      fprintf(stderr, "mask: ");
      gavl_hexdump(conn->read_msg.mask, 4, 4);
      }
    fprintf(stderr, "header:\n");
    gavl_hexdump(conn->read_msg.head, conn->read_msg.head_len, 16);
    }
#endif
    
#ifdef DUMP_SERVER_READ
  if(!conn->is_client)
    {
    fprintf(stderr, "\nmsg_read Bytes: %"PRId64"\n", conn->read_msg.payload_len);
    if(conn->read_msg.mask)
      {
      fprintf(stderr, "mask: ");
      gavl_hexdump(conn->read_msg.mask, 4, 4);
      }
    fprintf(stderr, "header:\n");
    gavl_hexdump(conn->read_msg.head, conn->read_msg.head_len, 16);
    }
#endif
  return 1;
  }

static gavl_source_status_t
msg_read(bg_websocket_connection_t * conn)
  {
  int result;
  
  if(conn->read_msg.head_read < 2)
    {
    int buf_len;

    if(!gavf_io_can_read(conn->io, 0))
      return GAVL_SOURCE_AGAIN;
    
    result = gavf_io_read_data_nonblock(conn->io, &conn->read_msg.head[conn->read_msg.head_read],
                                        2 - conn->read_msg.head_read);
    RETURN_RES;

    conn->read_msg.head_read += result;

    if(conn->read_msg.head_read < 2)
      return GAVL_SOURCE_AGAIN;

    /* Parse header */

    if(conn->read_msg.head[0] & 0x70) //  RSV1 RSV2 RSV3
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Got reserved bits");
      return GAVL_SOURCE_EOF;
      }

    conn->read_msg.head_len = 2;
    
    buf_len = conn->read_msg.head[1] & 0x7f;

    //  fprintf(stderr, "buf_len: %"PRId64"\n", buf_len);
  
    if(buf_len == 126)
      {
      conn->read_msg.head_len += 2;
      }
    else if(buf_len == 127)
      {
      conn->read_msg.head_len += 8;
      }

    if(conn->read_msg.head[1] & 0x80)
      {
      conn->read_msg.head_len += 4;
      }

    if(conn->read_msg.head_len == 2)
      {
      if(!decode_msg_header(conn))
        return GAVL_SOURCE_AGAIN;
      }
    }
  
  if(conn->read_msg.head_read < conn->read_msg.head_len)
    {
    if(!gavf_io_can_read(conn->io, 0))
      return GAVL_SOURCE_AGAIN;
    
    result = gavf_io_read_data_nonblock(conn->io, &conn->read_msg.head[conn->read_msg.head_read],
                                        conn->read_msg.head_len - conn->read_msg.head_read);
    RETURN_RES;

    conn->read_msg.head_read += result;

    if(conn->read_msg.head_read < conn->read_msg.head_len)
      return GAVL_SOURCE_AGAIN;

    if(!decode_msg_header(conn))
      return GAVL_SOURCE_AGAIN;
    }
  
  if(conn->read_msg.payload_read < conn->read_msg.payload_len)
    {
    if(!gavf_io_can_read(conn->io, 0))
      return GAVL_SOURCE_AGAIN;
    
    result = gavf_io_read_data_nonblock(conn->io,
                                        conn->read_msg.buf.buf + conn->read_msg.buf.len + conn->read_msg.payload_read,
                                        conn->read_msg.payload_len - conn->read_msg.payload_read);
    RETURN_RES;

    conn->read_msg.payload_read += result;

    if(conn->read_msg.payload_read < conn->read_msg.payload_len)
      return GAVL_SOURCE_AGAIN;

    if(conn->read_msg.mask)
      {
      int i;
      for(i = 0; i < conn->read_msg.payload_len; i++)
        conn->read_msg.buf.buf[conn->read_msg.buf.len + i] ^= conn->read_msg.mask[i%4];
      }
    
    /* Read message */
    conn->read_msg.buf.len += conn->read_msg.payload_len;
    conn->read_msg.buf.buf[conn->read_msg.buf.len] = '\0';
    }
    
  /* Check opcode */
  switch(conn->read_msg.head[0] & 0x0f)
    {
    case 0x0: // continuation frame
    case 0x1: // text frame
    case 0x2: // binary frame
      if(conn->read_msg.head[0] & 0x80) // FIN
        return GAVL_SOURCE_OK;
      else
        {
        /* Read another segment */
        conn_reset_read_msg_segment(conn);
        return GAVL_SOURCE_AGAIN;
        }
      break;
    case 0x8: // Close
      msg_write(conn, conn->read_msg.buf.buf, conn->read_msg.buf.len, 0x8);
      gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Got opcode close");
      return GAVL_SOURCE_EOF;
      break;
    case 0x9: // Ping
      /* Send pong */
      //      fprintf(stderr, "Sending pong\n");
      msg_write(conn, conn->read_msg.buf.buf, conn->read_msg.buf.len, 0xA);
      conn_reset_read_msg(conn);
      return GAVL_SOURCE_AGAIN;
      break;
    case 0xA: // Pong
      /* If we are a server, check if the pong belongs to the last ping */

      if(conn->ping_sent && (conn->read_msg.buf.len == PING_PAYLOAD_LEN))
        {
        gavl_time_t payload = GAVL_PTR_2_64BE(conn->read_msg.buf.buf);
        if(payload == conn->last_ping_time)
          {
          //          gavl_log(GAVL_LOG_DEBUG, LOG_DOMAIN, "Got pong");
          conn->ping_sent = 0;
          }
        }
      conn_reset_read_msg(conn);
      return GAVL_SOURCE_AGAIN;
      break;
    default:
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Unknown opcode: %x", conn->read_msg.head[0] & 0x0f);
      return GAVL_SOURCE_EOF;
      break;
    }
  
  gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Whoops %x", conn->read_msg.head[0] & 0x0f);
  return GAVL_SOURCE_EOF;
  }

static int msg_write_cb(void * data, gavl_msg_t * msg)
  {
  char * str = NULL;
  uint64_t len = 0;
  bg_websocket_connection_t * conn = data;
  
  str = bg_msg_to_json_str(msg);
  len = strlen(str);
  
  msg_write(conn, str, len, 0x1 /* text frame */ );
  
  if(str)
    free(str);

  /* Close if we get a QUIT command */
  if((msg->NS == GAVL_MSG_NS_GENERIC) &&
     (msg->ID == GAVL_CMD_QUIT))
    {
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Shutting down websocket %d (Got quit command)", conn->is_client);
    return 0;
    }
  
  return 1;
  }

static void ping_func(void * data)
  {
  bg_websocket_connection_iteration(data);
  }

static void conn_init(bg_websocket_connection_t * conn, int is_client)
  {
  conn->is_client = is_client;

  conn->timer = gavl_timer_create();
  
  if(is_client)
    {
    bg_controllable_init(&conn->ctrl_client,
                         bg_msg_sink_create(msg_write_cb, conn, 0),
                         bg_msg_hub_create(1));   // Owned
    conn->ctrl_client.ping_func = ping_func;
    conn->ctrl_client.ping_data = conn;
    }
  else
    {
    bg_control_init(&conn->ctrl_server,
                    bg_msg_sink_create(msg_write_cb, conn, 0));
    }
  }


static void conn_close(bg_websocket_connection_t * conn)
  {
  int i;
  if(conn->io)
    {
    gavf_io_destroy(conn->io);
    conn->io = NULL;
    }
  conn->num_write_msg = 0;
  conn_reset_read_msg(conn);

  for(i = 0; i < conn->write_msg_alloc; i++)
    msg_write_reset(&conn->write_msg[i]);
  }

static void conn_free(bg_websocket_connection_t * conn)
  {
  int i;
  gavl_timer_destroy(conn->timer);
  conn_close(conn);
  bg_control_cleanup(&conn->ctrl_server);
  bg_controllable_cleanup(&conn->ctrl_client);

  gavl_buffer_free(&conn->read_msg.buf);

  for(i = 0; i < conn->write_msg_alloc; i++)
    {
    gavl_buffer_free(&conn->write_msg[i].buf);
    }
  if(conn->write_msg)
    free(conn->write_msg);
  }


static char * websocket_key_generate()
  {
  uuid_t uuid;
  char * str_enc = NULL;
  int str_enc_len = 0;
  uuid_generate(uuid); // Lazy way to generate a 16 byte nonce
  bg_base64_encode(uuid, 16, &str_enc, &str_enc_len);
  return str_enc;
  }

static char * websocket_key_to_response(const char * key)
  {
  char * str;
  char * str_enc = NULL;
  int str_enc_len = 0;
  uint8_t sha1[20];
  
  str = bg_sprintf("%s258EAFA5-E914-47DA-95CA-C5AB0DC85B11", key);

  bg_base64_encode(bg_sha1_buffer(str, strlen(str), sha1),
                   20, &str_enc, &str_enc_len);
  
  free(str);
  return str_enc;
  }

/**************************************************************
 * Client
 **************************************************************/

/* Create CLIENT connection */

bg_websocket_connection_t *
bg_websocket_connection_create(const char * url, int timeout,
                               const char * origin)
  {
  gavl_dictionary_t req;
  gavl_dictionary_t res;
  
  char * host = NULL;
  char * path = NULL;
  char * protocol = NULL;
  
  char * key = NULL;
  char * key_res = NULL;
  
  int port = 0;
  //  int one = 1;
  int fd;
  bg_websocket_connection_t * ret = NULL;
  gavl_socket_address_t * addr = NULL;
  const char * var;
  gavf_io_t * io = NULL;
  
  gavl_dictionary_init(&req);
  gavl_dictionary_init(&res);
  
  if(!bg_url_split(url,
                   &protocol,
                   NULL, NULL,
                   &host,
                   &port,
                   &path))
    goto fail;

  if(!path)
    path = gavl_strdup("/");
  
  if(strcmp(protocol, "ws") &&
     strcmp(protocol, BG_BACKEND_URI_SCHEME_GMERLIN_RENDERER) &&
     strcmp(protocol, BG_BACKEND_URI_SCHEME_GMERLIN_MDB))
    goto fail;
  
  addr = gavl_socket_address_create();

  if(!gavl_socket_address_set(addr, host,
                            port, SOCK_STREAM))
    goto fail;

  gavl_http_request_init(&req, "GET", path, "HTTP/1.1");

  gavl_dictionary_set_string_nocopy(&req, "Host", bg_url_get_host(host, port));

  gavl_dictionary_set_string(&req, "Upgrade",                "websocket");
  gavl_dictionary_set_string(&req, "Connection",                "Upgrade");
  
  key = websocket_key_generate();
  key_res = websocket_key_to_response(key);
  
  gavl_dictionary_set_string(&req, "Sec-WebSocket-Key", key);
  
  if(origin)
    gavl_dictionary_set_string(&req, "Origin", origin);
  else
    gavl_dictionary_set_string_nocopy(&req, "Origin",
                            bg_sprintf("http://%s", gavl_dictionary_get_string(&req, "Host")));

  gavl_dictionary_set_string(&req, "Sec-WebSocket-Protocol", "json");
  gavl_dictionary_set_string(&req, "Sec-WebSocket-Version",  "13");

  if((fd = gavl_socket_connect_inet(addr, timeout)) < 0)
    goto fail;

  //  setsockopt(fd, SOL_TCP, TCP_NODELAY, &one, sizeof(one));
  
  io = gavf_io_create_socket(fd, 30000, GAVF_IO_SOCKET_DO_CLOSE);
  
  if(!gavl_http_request_write(io, &req))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Cannot write http request");
    goto fail;
    }
  if(!gavl_http_response_read(io, &res))
    goto fail;
  
  /* Check response */
  if((gavl_http_response_get_status_int(&res) != 101) ||
     !(var = gavl_dictionary_get_string(&res, "Connection")) ||
     strcasecmp(var, "Upgrade") ||
     !(var = gavl_dictionary_get_string(&res, "Upgrade")) ||
     strcasecmp(var, "websocket") ||
     !(var = gavl_dictionary_get_string(&res, "Sec-WebSocket-Accept")) ||
     strcasecmp(var, key_res) ||
     !(var = gavl_dictionary_get_string(&res, "Sec-WebSocket-Protocol")) ||
     strcasecmp(var, "json"))
    goto fail;

  /* From here on, we assume things to go smoothly */
  
  ret = calloc(1, sizeof(*ret));

  conn_init(ret, 1);
  ret->io = io;
  
  fail:

  if(host)
    free(host);
  if(path)
    free(path);
  if(protocol)
    free(protocol);
  if(key)
    free(key);
  if(key_res)
    free(key_res);
  
  gavl_dictionary_free(&req);
  gavl_dictionary_free(&res);

  if(addr)
    gavl_socket_address_destroy(addr);

  if(ret && !ret->io)
    {
    conn_free(ret);
    free(ret);
    ret = NULL;
    }
  
  return ret;
  }

/*
*/


int
bg_websocket_connection_iteration(bg_websocket_connection_t * conn)
  {
  int result;
  gavl_source_status_t st;
  
  /* Check ping */
  //  fprintf(stderr, "Websocket iteration\n");
  if(!conn->is_client)
    {
    gavl_time_t cur = gavl_timer_get(conn->timer);
    
    if(!conn->ping_sent &&
       ((conn->last_ping_time == GAVL_TIME_UNDEFINED) ||
        (cur - conn->last_ping_time > PING_INTERVAL)))
      {
      uint8_t payload[PING_PAYLOAD_LEN];

      conn->last_ping_time = cur;
      GAVL_64BE_2_PTR(cur, payload);
        
      /* Send ping */
      //        gavl_log(GAVL_LOG_DEBUG, LOG_DOMAIN, "Sending ping %d", gavf_io_get_socket(conn->io));
        
      msg_write(conn, payload, PING_PAYLOAD_LEN, 0x9);
      conn->ping_sent = 1;
      }
      
    // Check for ping timeout
    if(conn->ping_sent && ((cur - conn->last_ping_time) > PING_TIMEOUT))
      {
      /* Ping timeout */
      gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Ping timeout %d", gavf_io_get_socket(conn->io));
      return 0;
      }
    }
  else
    {
    bg_msg_sink_iteration(conn->ctrl_client.cmd_sink);
    }
  
  /* Write messages */
  while(conn->num_write_msg > 0)
    {
    
    if(conn->write_msg[0].head_written < conn->write_msg[0].head_len)
      {
      result = gavf_io_write_data_nonblock(conn->io,
                                           &conn->write_msg[0].head[conn->write_msg[0].head_written],
                                           conn->write_msg[0].head_len - conn->write_msg[0].head_written);
      if(result < 0)
        {
        gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Writing %d bytes failed",
                 conn->write_msg[0].head_len - conn->write_msg[0].head_written);
        return 0;
        }
      else if(!result)
        goto read;
      
      conn->write_msg[0].head_written += result;

      if(conn->write_msg[0].head_written < conn->write_msg[0].head_len)
        return 1;
      }
    
    if(conn->write_msg[0].buf.pos < conn->write_msg[0].buf.len)
      {
      result = gavf_io_write_data_nonblock(conn->io,
                                           conn->write_msg[0].buf.buf + conn->write_msg[0].buf.pos,
                                           conn->write_msg[0].buf.len - conn->write_msg[0].buf.pos);
      if(result < 0)
        {
        gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Writing %d bytes failed",
                 conn->write_msg[0].buf.len - conn->write_msg[0].buf.pos);
        return 0;
        }
      else if(!result)
        goto read;
      
      conn->write_msg[0].buf.pos += result;
      
      if(conn->write_msg[0].buf.pos < conn->write_msg[0].buf.len)
        goto read;
      }

    /* Message finished */
    //    fprintf(stderr, "Message finished\n");
    msg_write_reset(&conn->write_msg[0]);
    conn->num_write_msg--;
    
    if(conn->num_write_msg > 0)
      {
      msg_write_t swp;
      memcpy(&swp, &conn->write_msg[0], sizeof(swp));
      memmove(conn->write_msg, conn->write_msg + 1, sizeof(conn->write_msg[0]) * conn->num_write_msg);
      memcpy(&conn->write_msg[conn->num_write_msg], &swp, sizeof(swp));
      }
    
    }

  read:
    
  /* Read messages */
  
  while(1)
    {
    gavl_msg_t msg;

    st = msg_read(conn);
    //    fprintf(stderr, "msg_read returned: %d\n", st);
    if(st == GAVL_SOURCE_EOF)
      return 0;
    
    else if(st == GAVL_SOURCE_AGAIN)
      break;
    
    gavl_msg_init(&msg);
    
    /* Handle message */

    if(!bg_msg_from_json_str(&msg, (const char*)conn->read_msg.buf.buf))
      {
      fprintf(stderr, "bg_msg_from_json_str failed (got %d bytes), mask: %p\n",
              conn->read_msg.buf.len, conn->read_msg.mask);

      gavl_hexdump(conn->read_msg.mask, 4, 4);

      //      gavl_hexdump(conn->read_msg.buf.buf, conn->read_msg.buf.len, 16);
      return 0;
      }

    /* Check for quit */
    if((msg.NS == GAVL_MSG_NS_GENERIC) &&
       (msg.ID == GAVL_CMD_QUIT))
      {
      gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Got quit message");
      return 0;
      }
    
    if(conn->is_client)
      {
      //      fprintf(stderr, "Websocket: Got message:\n");
      //      gavl_msg_dump(&msg, 2);
      bg_msg_sink_put_copy(conn->ctrl_client.evt_sink, &msg);
      }
    else
      bg_msg_sink_put_copy(conn->ctrl_server.cmd_sink, &msg);

    /*
        struct
    {
    gavl_buffer_t buf;
    uint8_t * mask;

    uint8_t head[4+4+4+2];
    int head_len;
    int head_read; // Header bytes read so far
    
    uint64_t payload_len;
    uint64_t payload_read;
    
    } read_msg;
    */
    
    conn_reset_read_msg(conn);
    gavl_msg_free(&msg);
    }
  
  return 1;
  }  



void
bg_websocket_connection_destroy(bg_websocket_connection_t * conn)
  {
  conn_free(conn);
  free(conn);
  }

bg_controllable_t * 
bg_websocket_connection_get_controllable(bg_websocket_connection_t * conn)
  {
  return &conn->ctrl_client;
  }

/**************************************************************
 * Server
 **************************************************************/

/* Ping status */

struct bg_websocket_context_s
  {
  bg_websocket_connection_t conn[BG_WEBSOCKET_MAX_CONNECTIONS];
  bg_controllable_t * ctrl;
  
  char * path;
  char * info_json;
  };

/* Connection */

static int conn_start_server(bg_websocket_connection_t * conn,
                             bg_http_connection_t * c,
                             bg_websocket_context_t * ctx)
  {
  int result;
  char * str = NULL;
  int ret = 0;
  //  int one = 1;
  
  const char * key;
  const char * var;

  //  fprintf(stderr, "conn_start_server\n");
  
  /* Sanity check */
  if(!(var = gavl_dictionary_get_string(&c->req, "Upgrade")) ||
     strcmp(var, "websocket"))
    goto fail;
   
  if(!(var = gavl_dictionary_get_string(&c->req, "Connection")) ||
     !strstr(var, "Upgrade"))
    goto fail;

  if(!(key = gavl_dictionary_get_string(&c->req, "Sec-WebSocket-Key")))
    goto fail;

  if(!(var = gavl_dictionary_get_string(&c->req, "Sec-WebSocket-Protocol")) ||
     !strstr(var, "json"))
    goto fail;
  
  /* Do handshake */

  gavl_dictionary_init(&c->res);

  bg_http_connection_init_res(c, "HTTP/1.1", 101, "Switching Protocols");
  
  gavl_dictionary_set_string(&c->res, "Upgrade", "websocket");
  gavl_dictionary_set_string(&c->res, "Connection" , "Upgrade");

  gavl_dictionary_set_string(&c->res, "Sec-WebSocket-Protocol", "json");
  
  /* Key */
  
  gavl_dictionary_set_string_nocopy(&c->res, "Sec-WebSocket-Accept", websocket_key_to_response(key));
  
  /* Send response */

  c->flags |= BG_HTTP_REQ_WEBSOCKET;
  result = bg_http_connection_write_res(c);

  if(!result)
    {
    // fprintf(stderr, "conn_start_server: Writing response failed\n");
    goto fail;
    }

  //  pthread_mutex_lock(&conn->fd_mutex);

  //  setsockopt(c->fd, SOL_TCP, TCP_NODELAY, &one, sizeof(one));
  
  if(!(conn->io = gavf_io_create_socket(c->fd, 5000, GAVF_IO_SOCKET_DO_CLOSE)))
    goto fail;
  
  c->fd = -1;
  
  bg_controllable_connect(ctx->ctrl, &conn->ctrl_server);
  
  ret = 1;
  
  fail:
  
  //  fprintf(stderr, "conn_start_server finished %d\n", ret);
  
  if(str)
    free(str);
  
  return ret;
  }

/* Context */

int bg_websocket_context_handle_request(bg_http_connection_t * c, void * data)
  {
  int i;
  bg_websocket_connection_t * conn = NULL;
  bg_websocket_context_t * ctx = data;

  //  fprintf(stderr, "bg_websocket_context_handle_request %s", c->path);
  
  if(!strcmp(c->path, "/info"))
    {
    int len;
    //    fprintf(stderr, "Node info requested\n");

    if(!ctx->info_json)
      {
      const gavl_array_t * icons;
      const char * str;
      gavl_dictionary_t node;
      struct json_object * json = json_object_new_object();
      
      gavl_dictionary_init(&node);

      if((icons = bg_app_get_application_icons()))
        gavl_dictionary_set_array(&node, GAVL_META_ICON_URL, icons);

      if((str = bg_app_get_label()))
        gavl_dictionary_set_string(&node, GAVL_META_LABEL, str);

      bg_dictionary_to_json(&node, json);
      
      ctx->info_json = gavl_strdup(json_object_to_json_string_ext(json, 0));
      json_object_put(json);

      //   fprintf(stderr, "Node info requested %s\n", ctx->info_json);
      }

    len = strlen(ctx->info_json);
    
    bg_http_connection_init_res(c, c->protocol, 200, "OK");
    gavl_dictionary_set_string(&c->res, "Content-Type", "application/json");
    gavl_dictionary_set_int(&c->res, "Content-Length", len);
    if(!bg_http_connection_write_res(c) ||
       (gavl_socket_write_data(c->fd, ctx->info_json, len) < len))
      {
      bg_http_connection_clear_keepalive(c);
      return 0;
      }
    
    return 1;
    }
  else if(*c->path != '\0')
    {
    /* 404 */
    return 0;
    }
  
  /* Check for free connection */

  for(i = 0; i < BG_WEBSOCKET_MAX_CONNECTIONS; i++)
    {
    if(ctx->conn[i].io)
      continue;
    
    conn = &ctx->conn[i];
    break;
    }

  if(!conn)
    {
    bg_http_connection_init_res(c, c->protocol, 503, "Service Unavailable");
    return 1;
    }
  
  conn_start_server(conn, c, ctx);
  return 1;
  }

char * bg_websocket_make_path(const char * klass)
  {
  return bg_sprintf("/ws/%s", klass);
  }

bg_websocket_context_t *
bg_websocket_context_create(const char * klass,
                            const char * path,
                            bg_controllable_t * ctrl)
  {
  int i;
  bg_http_server_t * srv;
  bg_websocket_context_t * ctx = calloc(1, sizeof(*ctx));
  
  for(i = 0; i < BG_WEBSOCKET_MAX_CONNECTIONS; i++)
    conn_init(&ctx->conn[i], 0);

  ctx->ctrl = ctrl;

  if(path)
    ctx->path = gavl_strdup(path);
  else
    ctx->path = bg_websocket_make_path(klass);
  
  if((srv = bg_http_server_get()))
    {
    bg_http_server_add_handler(srv,
                               bg_websocket_context_handle_request,
                               BG_HTTP_PROTO_HTTP,
                               ctx->path, // E.g. /static/ can be NULL
                               ctx);
    }
  
  return ctx;
  }

int bg_websocket_context_iteration(bg_websocket_context_t * ctx)
  {
  int i;
  int ret = 0;
  
  for(i = 0; i < BG_WEBSOCKET_MAX_CONNECTIONS; i++)
    {
    if(ctx->conn[i].io)
      {
      if(!bg_websocket_connection_iteration(&ctx->conn[i]))
        {
        gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Client disconnected");
        bg_controllable_disconnect(ctx->ctrl, &ctx->conn[i].ctrl_server);
        conn_close(&ctx->conn[i]);
        ret++;
        continue;
        }
      bg_msg_sink_iteration(ctx->conn[i].ctrl_server.evt_sink);
      ret += bg_msg_sink_get_num(ctx->conn[i].ctrl_server.evt_sink);
      }
    }
  return ret;
  }

void bg_websocket_context_destroy(bg_websocket_context_t * ctx)
  {
  int i;

  for(i = 0; i < BG_WEBSOCKET_MAX_CONNECTIONS; i++)
    conn_free(&ctx->conn[i]);

  if(ctx->path)
    free(ctx->path);
  if(ctx->info_json)
    free(ctx->info_json);
  
  free(ctx);
  }

int bg_websocket_context_num_clients(bg_websocket_context_t * ctx)
  {
  int ret = 0;
  int i;

  for(i = 0; i < BG_WEBSOCKET_MAX_CONNECTIONS; i++)
    {
    if(ctx->conn[i].io)
      ret++;
    }
  return ret;
  }

