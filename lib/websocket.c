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

#include <gmerlin/bgsocket.h>
// #include <gmerlin/http.h>
#include <gmerlin/utils.h>

#include <gmerlin/websocket.h>
#include <gmerlin/translation.h>
#include <gmerlin/log.h>
#include <gmerlin/bgplug.h>

#define LOG_DOMAIN_CLIENT "websocket-client"
#define LOG_DOMAIN_SERVER "websocket-server"
#define LOG_DOMAIN "websocket"

#define BG_WEBSOCKET_MAX_CONNECTIONS 10
#define PING_PAYLOAD_LEN 8

#define PING_INTERVAL  (2*GAVL_TIME_SCALE)
#define PING_TIMEOUT  (10*GAVL_TIME_SCALE)

#define STATE_CLOSED    0 // Must be 0 for initialization
#define STATE_RUNNING   1
#define STATE_FINISHED  2

/**************************************************************
 * Common for server and client
 **************************************************************/

typedef struct
  {
  gavl_buffer_t b;
  int type;
  } bg_websocket_msg_t;

struct bg_websocket_connection_s
  {
  gavf_io_t * io;
  
  pthread_mutex_t write_mutex;
  bg_websocket_msg_t msg;
  int is_client;

  /* handled by the thread of the context */

  pthread_mutex_t state_mutex;
  gavl_time_t last_ping_time;
  
  int ping_sent;
  int state;

  pthread_t th;

  /* Control stuff */
  bg_controllable_t ctrl_client;
  bg_control_t      ctrl_server;

  gavl_timer_t * timer;
  
  };


static int conn_get_state(bg_websocket_connection_t * conn)
  {
  int ret;
  pthread_mutex_lock(&conn->state_mutex);
  ret = conn->state;
  pthread_mutex_unlock(&conn->state_mutex);
  return ret;
  }

static void conn_set_state(bg_websocket_connection_t * conn, int s)
  {
  pthread_mutex_lock(&conn->state_mutex);
  conn->state = s;
  pthread_mutex_unlock(&conn->state_mutex);
  }



static int conn_lock_write(bg_websocket_connection_t * conn)
  {
  pthread_mutex_lock(&conn->write_mutex);
  if(!conn->io)
    {
    pthread_mutex_unlock(&conn->write_mutex);
    return 0;
    }
  return 1;
  }

static void conn_unlock_write(bg_websocket_connection_t * conn)
  {
  pthread_mutex_unlock(&conn->write_mutex);
  }


static void msg_free(bg_websocket_msg_t * m)
  {
  gavl_buffer_free(&m->b);
  }

static int
msg_write(bg_websocket_connection_t * conn, const void * msg1, uint64_t len, int type)
  {
  int result;
  uint8_t head[4+4+4+2];
  uint8_t * mask_buffer = NULL;

  uint8_t * ptr = &(head[0]);
  const uint8_t * msg;

  ptr[0] = 0;
  
  ptr[0] |= (type & 0x0f); // OPCode
  ptr[0] |= 0x80; // FIN

  ptr[1] = 0;
  
  if(conn->is_client)
    ptr[1] = 0x80;
  
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

  /* No masking key (we are server) */
  msg = msg1;
  
  if(conn->is_client && (len > 0))
    {
    int i;

    ptr[0] = rand();
    ptr[1] = rand();
    ptr[2] = rand();
    ptr[3] = rand();
    
    mask_buffer = malloc(len);

    for(i = 0; i < len; i++)
      {
      mask_buffer[i] = msg[i] ^ ptr[i % 4];
      }
    msg = mask_buffer;
    ptr += 4;
    }
  
  result = 1;
  
  //  gavl_hexdump(head, ptr - &(head[0]), 16);

  conn_lock_write(conn);
  if(gavf_io_write_data(conn->io, head, ptr - &(head[0])) < ptr - &(head[0]))
    result = 0;
  else if((len > 0) && (gavf_io_write_data(conn->io, msg, len) < len))
    result = 0;

  gavf_io_flush(conn->io);
  
  conn_unlock_write(conn);
  
  if(mask_buffer)
    free(mask_buffer);
  
  return result;
  }


static gavl_source_status_t
msg_read(bg_websocket_connection_t * conn)
  {
  int result;
  uint8_t masking_key[4];
  uint8_t head[2];
  uint8_t len[8];
  uint64_t buf_len;
  gavl_source_status_t ret = GAVL_SOURCE_EOF;

  if((result = gavf_io_read_data(conn->io, head, 2)) < 2)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Reading first two bytes failed");
    goto end;
    }

  if(head[0] & 0x70) //  RSV1 RSV2 RSV3
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Got reserved bits");
    goto end;
    }
  
  buf_len = head[1] & 0x7f;

  //  fprintf(stderr, "buf_len: %"PRId64"\n", buf_len);
  
  if(buf_len == 126)
    {
    if(gavf_io_read_data(conn->io, len, 2) < 2)
      return GAVL_SOURCE_EOF;
    buf_len = GAVL_PTR_2_16BE(len);
    }
  else if(buf_len == 127)
    {
    if(gavf_io_read_data(conn->io, len, 8) < 8)
      goto end;
    buf_len = GAVL_PTR_2_64BE(len);
    }

  /* Masking key */
  if(head[1] & 0x80)
    {
    if(gavf_io_read_data(conn->io, masking_key, 4) < 4)
      goto end;
    }
  else if(!conn->is_client)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Got unmasked data");
    goto end;
    }

  //  fprintf(stderr, "head: %d fin: %d len: %"PRId64"\n", head[0] & 0x0f, head[0] & 0x80, buf_len);
  
  switch(head[0] & 0x0f)
    {
    case 0x0: // continuation frame
      break;
    case 0x1: // text frame
      gavl_buffer_reset(&conn->msg.b);
      conn->msg.type = BG_WEBSOCKET_TYPE_TEXT;
      break;
    case 0x2: // binary frame
      gavl_buffer_reset(&conn->msg.b);
      conn->msg.type = BG_WEBSOCKET_TYPE_BINARY;
      break;
    case 0x8: // Close
      gavl_buffer_reset(&conn->msg.b);
      break;
    case 0x9: // Ping
      gavl_buffer_reset(&conn->msg.b);
      break;
    case 0xA: // Pong
      gavl_buffer_reset(&conn->msg.b);
      break;
    default:
      break;
    }

  /* Payload */
  if(buf_len)
    {
    uint64_t i;
    gavl_buffer_alloc(&conn->msg.b, conn->msg.b.len + buf_len + 1);
//    msg_alloc(&conn->msg, conn->msg.buf_len + buf_len + 1);
    if(gavf_io_read_data(conn->io, conn->msg.b.buf + conn->msg.b.len, buf_len) < buf_len)
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Reading payload failed: %s", strerror(errno));
      goto end;
      }
    /* Unmask */
    if(head[1] & 0x80)
      {
      i = 0;

      while(i < buf_len)
        {
        conn->msg.b.buf[conn->msg.b.len + i] ^= masking_key[i % 4];
        i++;
        }
      }
    
    conn->msg.b.len += buf_len;
    conn->msg.b.buf[conn->msg.b.len] = '\0';
    }
  else
    return GAVL_SOURCE_EOF;
    
  switch(head[0] & 0x0f)
    {
    case 0x0: // continuation frame
    case 0x1: // text frame
    case 0x2: // binary frame
      if(head[0] & 0x80) // FIN
        ret = GAVL_SOURCE_OK;
      else
        ret = GAVL_SOURCE_AGAIN;
      break;
    case 0x8: // Close
      msg_write(conn, conn->msg.b.buf, conn->msg.b.len, 0x8);
      break;
    case 0x9: // Ping
      /* Send pong */
      //      fprintf(stderr, "Sending pong\n");
      result = msg_write(conn, conn->msg.b.buf, conn->msg.b.len, 0xA);
      if(result)
        ret = GAVL_SOURCE_AGAIN;
      else
      break;
    case 0xA: // Pong
      /* If we are a server, check if the pong belongs to the last ping */

      if(conn->ping_sent && (conn->msg.b.len == PING_PAYLOAD_LEN))
        {
        gavl_time_t payload = GAVL_PTR_2_64BE(conn->msg.b.buf);
        if(payload == conn->last_ping_time)
          {
          //          gavl_log(GAVL_LOG_DEBUG, LOG_DOMAIN, "Got pong");
          conn->ping_sent = 0;
          }
        }
      ret = GAVL_SOURCE_AGAIN;
      break;
    default:
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Unknown opcode: %x", head[0] & 0x0f);
      break;
    }

  end:

    
  return ret;
  }


static gavl_source_status_t
bg_websocket_msg_read(bg_websocket_connection_t * conn, gavl_msg_t * msg)
  {
  gavl_source_status_t st;

  st = msg_read(conn);
  
  if(st == GAVL_SOURCE_OK)
    {
    /* Handle message */
    if(!bg_msg_from_json_str(msg, (const char*)conn->msg.b.buf))
      {
      st = GAVL_SOURCE_EOF;
      fprintf(stderr, "bg_msg_from_json_str failed (got %d bytes)\n", conn->msg.b.len);
      gavl_hexdump(conn->msg.b.buf, conn->msg.b.len, 16);
      }
    
    }
  return st;
  }

static int bg_websocket_msg_write(bg_websocket_connection_t * conn, gavl_msg_t * msg)
  {
  char * str = NULL;
  uint64_t len = 0;
  int ret;

  /* Close if we get a QUIT command */
#if 0
  if((msg->NS == GAVL_MSG_NS_GENERIC) &&
     (msg->ID == GAVL_CMD_QUIT))
    {
    shutdown(gavf_io_get_socket(conn->io), SHUT_RD);
    return 0;
    }
#endif
  
  str = bg_msg_to_json_str(msg);
  len = strlen(str);
  
  if(!(ret = msg_write(conn, str, len, 0x1 /* text frame */ )))
    shutdown(gavf_io_get_socket(conn->io), SHUT_RD);
  
  if(str)
    free(str);

  if((msg->NS == GAVL_MSG_NS_GENERIC) &&
     (msg->ID == GAVL_CMD_QUIT))
    {
    conn_set_state(conn, STATE_FINISHED);
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Shutting down websocket %d", conn->is_client);
    shutdown(gavf_io_get_socket(conn->io), SHUT_RD);
    return 0;
    }
  
  if(!ret)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Writing message failed");
    gavl_msg_dump(msg, 2);
    }
  
  return ret;
  }

static int msg_write_cb(void * data, gavl_msg_t * msg)
  {
  return bg_websocket_msg_write(data, msg);
  }
  

static void conn_init(bg_websocket_connection_t * conn, int is_client)
  {
  conn->is_client = is_client;
  pthread_mutex_init(&conn->write_mutex, NULL);
  pthread_mutex_init(&conn->state_mutex, NULL);

  conn->timer = gavl_timer_create();
  
  if(is_client)
    {
    bg_controllable_init(&conn->ctrl_client,
                         bg_msg_sink_create(msg_write_cb, conn, 1),
                         bg_msg_hub_create(1));   // Owned
    }
  else
    {
    bg_control_init(&conn->ctrl_server,
                    bg_msg_sink_create(msg_write_cb, conn, 0));
    }
  }

static void * conn_thread(void * data)
  {
  gavl_source_status_t st;
  gavl_time_t cur;
  gavl_msg_t msg;
  int num = 0;
  int do_break = 0;
  bg_websocket_connection_t * conn = data;

  conn->last_ping_time = GAVL_TIME_UNDEFINED;
  gavl_timer_start(conn->timer);

  
  while(1)
    {
    num = 0;
    
    gavl_msg_init(&msg);
    
    /* Read messages */
    while(gavf_io_can_read(conn->io, 50))
      {
      if(conn_get_state(conn) == STATE_FINISHED)
        {
        do_break = 1;
        break;
        }
      
      if((st = bg_websocket_msg_read(conn, &msg)) == GAVL_SOURCE_OK)
        {
        //        fprintf(stderr, "Got message:\n");
        //        gavl_msg_dump(&msg, 2);

        /* Check for quit */
        if((msg.NS == GAVL_MSG_NS_GENERIC) &&
           (msg.ID == GAVL_CMD_QUIT))
          {
          gavl_log(GAVL_LOG_DEBUG, LOG_DOMAIN, "Got quit message");
          goto end;
          }
        
        if(conn->is_client)
          bg_msg_sink_put(conn->ctrl_client.evt_sink, &msg);
        else
          bg_msg_sink_put(conn->ctrl_server.cmd_sink, &msg);
        }
      else if(st == GAVL_SOURCE_EOF)
        {
        gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Couldn't read message %d", conn->is_client);
        gavl_msg_free(&msg);
        goto end;
        }
      
      gavl_msg_free(&msg);
      gavl_msg_init(&msg);
      num++;
      }
    
    
    gavl_msg_free(&msg);

    if(do_break)
      break;
    
    /* Check for ping */

    if(!conn->is_client)
      {
      cur = gavl_timer_get(conn->timer);
    
      if(!conn->ping_sent &&
         ((conn->last_ping_time == GAVL_TIME_UNDEFINED) ||
          (cur - conn->last_ping_time > PING_INTERVAL)))
        {
        uint8_t payload[PING_PAYLOAD_LEN];

        conn->last_ping_time = cur;
        GAVL_64BE_2_PTR(cur, payload);
        
        /* Send ping */
        //        gavl_log(GAVL_LOG_DEBUG, LOG_DOMAIN, "Sending ping %d", gavf_io_get_socket(conn->io));
        
        if(!msg_write(conn, payload, PING_PAYLOAD_LEN, 0x9))
          break;
        conn->ping_sent = 1;
        }
      
      // Check for ping timeout
      if(conn->ping_sent && ((cur - conn->last_ping_time) > PING_TIMEOUT))
        {
        /* Ping timeout */
        gavl_log(GAVL_LOG_DEBUG, LOG_DOMAIN, "Ping timeout %d", gavf_io_get_socket(conn->io));
        break;
        }
      }
    
    }
  
  end:


  gavl_log(GAVL_LOG_DEBUG, LOG_DOMAIN, "Closed websocket connection");
  
  gavl_timer_stop(conn->timer);
  conn_set_state(conn, STATE_FINISHED);
  return NULL;
  }

static void conn_free(bg_websocket_connection_t * conn)
  {
  pthread_mutex_destroy(&conn->write_mutex);
  pthread_mutex_destroy(&conn->state_mutex);
  gavl_timer_destroy(conn->timer);
  msg_free(&conn->msg);

  bg_control_cleanup(&conn->ctrl_server);
  bg_controllable_cleanup(&conn->ctrl_client);
  
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
  
  if(strcmp(protocol, "ws"))
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
  
  io = gavf_io_create_socket_1(fd, 30000, GAVF_IO_SOCKET_DO_CLOSE);
  
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
    if(ret->io)
      gavf_io_destroy(ret->io);
    
    conn_free(ret);
    free(ret);
    ret = NULL;
    }
  
  return ret;
  }

void
bg_websocket_connection_start(bg_websocket_connection_t * conn)
  {
  conn_set_state(conn, STATE_RUNNING);
  pthread_create(&conn->th, NULL, conn_thread, conn);
  }

void
bg_websocket_connection_stop(bg_websocket_connection_t * conn)
  {
  int state = conn_get_state(conn);

  if(state == STATE_RUNNING)
    {
    /* Send quit signal */
    
    gavl_msg_t msg;
    gavl_msg_init(&msg);
    gavl_msg_set_id_ns(&msg, GAVL_CMD_QUIT, GAVL_MSG_NS_GENERIC);

    bg_websocket_msg_write(conn, &msg);
    gavl_msg_free(&msg);

    conn_set_state(conn, STATE_FINISHED);
        
    shutdown(gavf_io_get_socket(conn->io), SHUT_RD);
    pthread_join(conn->th, NULL);
    }
  }

void
bg_websocket_connection_destroy(bg_websocket_connection_t * conn)
  {
  bg_websocket_connection_stop(conn);

  if(conn->io)
    gavf_io_destroy(conn->io);
  
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
  
  if(!(conn->io = gavf_io_create_socket_1(c->fd, 5000, GAVF_IO_SOCKET_DO_CLOSE)))
    goto fail;
  
  c->fd = -1;
  
  bg_controllable_connect(ctx->ctrl, &conn->ctrl_server);

  /* Start thread */
  bg_websocket_connection_start(conn);
  
  ret = 1;
  
  fail:
  
  //  fprintf(stderr, "conn_start_server finished %d\n", ret);
  
  if(str)
    free(str);

  if(!ret)
    conn_set_state(conn,  STATE_CLOSED);
  
  return ret;
  }

/* Context */

int bg_websocket_context_handle_request(bg_http_connection_t * c, void * data)
  {
  int i;
  bg_websocket_connection_t * conn = NULL;
  bg_websocket_context_t * ctx = data;
  
  /* Check for free connection */

  for(i = 0; i < BG_WEBSOCKET_MAX_CONNECTIONS; i++)
    {
    if(conn_get_state(&ctx->conn[i]) != STATE_CLOSED)
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

char * bg_websocket_make_path(bg_backend_type_t type)
  {
  const char * label = bg_backend_type_to_string(type);
  if(label)
    return bg_sprintf("/ws/%s", label);
  else
    return gavl_strdup("/ws");
  }

bg_websocket_context_t *
bg_websocket_context_create(bg_backend_type_t type,
                            bg_http_server_t * srv,
                            const char * path,
                            bg_controllable_t * ctrl)
  {
  int i;
  bg_websocket_context_t * ctx = calloc(1, sizeof(*ctx));
  
  for(i = 0; i < BG_WEBSOCKET_MAX_CONNECTIONS; i++)
    conn_init(&ctx->conn[i], 0);

  ctx->ctrl = ctrl;

  if(srv)
    {
    if(path)
      {
      bg_http_server_add_handler(srv,
                                 bg_websocket_context_handle_request,
                                 BG_HTTP_PROTO_HTTP,
                                 path, // E.g. /static/ can be NULL
                                 ctx);
      }
    else
      {
      char * path1 = bg_websocket_make_path(type);
      bg_http_server_add_handler(srv,
                                 bg_websocket_context_handle_request,
                                 BG_HTTP_PROTO_HTTP,
                                 path1, // E.g. /static/ can be NULL
                                 ctx);
      free(path1);
      }
    }
  
  return ctx;
  }

int bg_websocket_context_iteration(bg_websocket_context_t * ctx)
  {
  int i;
  int ret = 0;
  int state;
  
  for(i = 0; i < BG_WEBSOCKET_MAX_CONNECTIONS; i++)
    {
    state = conn_get_state(&ctx->conn[i]);
    if(state == STATE_RUNNING)
      {
      bg_msg_sink_iteration(ctx->conn[i].ctrl_server.evt_sink);
      ret += bg_msg_sink_get_num(ctx->conn[i].ctrl_server.evt_sink);
      }
    /* Join finished threads */
    else if(state == STATE_FINISHED)
      {
      pthread_join(ctx->conn[i].th, NULL);
      bg_controllable_disconnect(ctx->ctrl, &ctx->conn[i].ctrl_server);
      
      conn_set_state(&ctx->conn[i], STATE_CLOSED); // Can be reused

      if(ctx->conn[i].io)
        {
        gavf_io_destroy(ctx->conn[i].io);
        ctx->conn[i].io = NULL;
        }
      
      ret++;
      }
    }
  return ret;
  }

void bg_websocket_context_destroy(bg_websocket_context_t * ctx)
  {
  int i;

  for(i = 0; i < BG_WEBSOCKET_MAX_CONNECTIONS; i++)
    {
    if(conn_get_state(&ctx->conn[i]) != STATE_CLOSED)
      {
      if(ctx->conn[i].io)
        shutdown(gavf_io_get_socket(ctx->conn[i].io), SHUT_RDWR);
      
      pthread_join(ctx->conn[i].th, NULL);
      bg_controllable_disconnect(ctx->ctrl, &ctx->conn[i].ctrl_server);
      }
    
    conn_free(&ctx->conn[i]);
    }
  free(ctx);
  }

int bg_websocket_context_num_clients(bg_websocket_context_t * ctx)
  {
  int ret = 0;
  int i;

  for(i = 0; i < BG_WEBSOCKET_MAX_CONNECTIONS; i++)
    {
    if(conn_get_state(&ctx->conn[i]) != STATE_CLOSED)
      ret++;
    }
  return ret;
  }

