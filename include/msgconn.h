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


/* Private stuff shared among msg handling modules */

#define ROUTING_TABLE_SIZE 32

typedef struct
  {
  gavl_msg_t ** buf;
  int len;
  int alloc;
  } msg_buf_t;


typedef struct
  {
  msg_buf_t queue;
  msg_buf_t pool;

  } msg_queue_t;

struct bg_msg_sink_s
  {
  msg_queue_t * queue;
  
  //  bg_msg_queue_t * q;
  gavl_msg_t * m;
  gavl_msg_t * m_priv;

  gavl_handle_msg_func cb;
  void * cb_data;

  char id_buf[37]; // uuid
  char * id;
  
  /*
   *  Number of messages processed in the last call of
   *  bg_msg_sink_iteration
   */
  
  int num_msg;

  /* Routing table */
  int routing_table_size;
  struct
    {
    uuid_t id;
    } routing_table[ROUTING_TABLE_SIZE];

  pthread_mutex_t rm;          // routing table mutex
  pthread_mutex_t write_mutex; // Write active mutex
  //  pthread_mutex_t queue_mutex; // Queue mutex
  };

int bg_msg_routing_table_get(bg_msg_sink_t * sink, const char * id);
void bg_msg_routing_table_put(bg_msg_sink_t * sink, const char * id);
