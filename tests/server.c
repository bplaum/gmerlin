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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/poll.h>
#include <unistd.h>

#include <gavl/gavlsocket.h>


#include <gmerlin/utils.h>

#include <netinet/in.h>

#define MAX_CONNECTIONS 1024

#define INET_PORT 1122
#define UNIX_NAME "blubberplatsch"

typedef struct connection_s
  {
  int fd;
  struct connection_s * next;
  } connection_t;

static connection_t * add_connection(connection_t * list, int fd)
  {
  connection_t * new_connection;
  
  new_connection = calloc(1, sizeof(*new_connection));
  new_connection->fd = fd;
  new_connection->next = list;
  return new_connection;
  }

static connection_t * remove_connection(connection_t * list, connection_t * c)
  {
  connection_t * before;

  if(c == list)
    list = list->next;
  else
    {
    before = list;

    while(before->next != c)
      before = before->next;

    before->next = c->next;
    }
  free(c);
  return list;
  }

int main(int argc, char ** argv)
  {
  connection_t * connections = NULL;
  connection_t * con_ptr;
  connection_t * con_tmp;
  int new_fd;
  int i;
  struct pollfd * pollfds = NULL;
  int num_pollfds = 0;
  int num_connections = 0;
  int unix_socket;
  int tcp_socket;
  int result;

  char * buffer = NULL;
  int buffer_size = 0;
  int keep_going = 1;
  
  /* Create unix listener */

  tcp_socket = gavl_listen_socket_create_inet(NULL,
                                            1122, 10, INADDR_LOOPBACK);
  if(tcp_socket == -1)
    fprintf(stderr, "Cannot create TCP Socket\n");

  unix_socket = gavl_listen_socket_create_unix(UNIX_NAME, 10);
  if(unix_socket == -1)
    fprintf(stderr, "Cannot create UNIX Socket\n");

  while(keep_going)
    {
    if((new_fd = gavl_listen_socket_accept(tcp_socket, 0, NULL)) != -1)
      {
      fprintf(stderr, "TCP Connection coming in: %d\n", new_fd);
      connections = add_connection(connections, new_fd);
      num_connections++;
      }

    if((new_fd = gavl_listen_socket_accept(unix_socket, 0, NULL)) != -1)
      {
      fprintf(stderr, "Local connection coming in: %d\n", new_fd);
      connections = add_connection(connections, new_fd);
      num_connections++;
      }
    
    /* Now, poll for messages */

    if(num_connections)
      {
      if(num_pollfds < num_connections)
        {
        num_pollfds = num_connections + 10;
        pollfds = realloc(pollfds, num_pollfds * sizeof(*pollfds));
        }
      con_ptr = connections;

      for(i = 0; i < num_connections; i++)
        {
        pollfds[i].fd     = con_ptr->fd;
        pollfds[i].events = POLLIN | POLLPRI;
        pollfds[i].revents = 0;
        con_ptr = con_ptr->next;
        }

      /* Now, do the poll */

      result = poll(pollfds, num_connections, 1000);

      //      fprintf(stderr, "Poll: %d\n", result);
      
      if(result > 0)
        {
        con_ptr = connections;
        
        for(i = 0; i < num_connections; i++)
          {
          if(pollfds[i].revents & (POLLHUP | POLLERR | POLLNVAL))
            {
            /* Connection closed */
            close(pollfds[i].fd);
            con_tmp = con_ptr->next;
            connections = remove_connection(connections, con_ptr);
            num_connections--;
            con_ptr = con_tmp;
            fprintf(stderr, "Connection %d closed (fd: %d)\n", i+1,
                    pollfds[i].fd);
            }
          else if(pollfds[i].revents & (POLLIN | POLLPRI))
            {
            /* Read message */
            
            if(!gavl_socket_read_line(pollfds[i].fd, &buffer, &buffer_size, -1))
              {
              con_tmp = con_ptr->next;
              connections = remove_connection(connections, con_ptr);
              num_connections--;

              fprintf(stderr, "Connection %d closed (fd: %d)\n", i+1,
                      pollfds[i].fd);
              
              con_ptr = con_tmp;
              }
            else
              {
              fprintf(stderr, "Message from connection %d: %s\n",
                      i+1, buffer);
              if(!strcmp(buffer, "quit"))
                {
                fprintf(stderr, "Exiting\n");
                keep_going = 0;
                }
              }
            }
                    else
            con_ptr = con_ptr->next;
          }
        
        }
        

      }
    }

  /* Close all sockets */
  close(unix_socket);
  close(tcp_socket);
  
  for(i = 0; i < num_connections; i++)
    close(pollfds[i].fd);
  
  return 0;
  }

