/*****************************************************************
 * Gmerlin - a general purpose multimedia framework and applications
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

#include <config.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

/* getpid() */
#include <sys/types.h>
#include <unistd.h>

#include <gmerlin/translation.h>
#include <gmerlin/log.h>
#define LOG_DOMAIN "dbus"

#include <gmerlin/frontend.h>
#include <gmerlin/bgdbus.h>
#include <gmerlin/utils.h>


/* Original rule passed to bg_dbus_connection_add_listener */
#define BG_DBUS_META_RULE      "rule"

#define BG_DBUS_META_INTROSPECT "$INTROSPECT"

/* Backtrace stuff */

#if 0
#include <execinfo.h>

void
print_trace(void)
  {
  void *array[50];
  size_t size;
  char **strings;
  size_t i;

  size = backtrace(array, 50);
  strings = backtrace_symbols(array, size);

  fprintf(stderr, "Obtained %zd stack frames.\n", size);
  
  for (i = 0; i < size; i++)
    fprintf(stderr, "%s\n", strings[i]);

  free (strings);
  }
#endif

/* */


static const struct
  {
  int type;
  const char * name;
  }
msg_type_names[] =
  {
    {  DBUS_MESSAGE_TYPE_SIGNAL,        "signal"         },
    {  DBUS_MESSAGE_TYPE_METHOD_CALL,   "method_call",   },
    {  DBUS_MESSAGE_TYPE_METHOD_RETURN, "method_return", },
    {  DBUS_MESSAGE_TYPE_ERROR,         "error",         },
    { /* End */                                          }
  };

static void msg_dbus_to_gavl(DBusMessage * msg, gavl_msg_t * ret);


static const char * get_msg_type_name(int type)
  {
  int i = 0;

  while(msg_type_names[i].name)
    {
    if(msg_type_names[i].type == type)
      return msg_type_names[i].name;
    i++;
    }
  return NULL;
  }

#if 1
void bg_dbus_msg_dump(DBusMessage * msg)
  {
  gavl_msg_t m;
  gavl_msg_init(&m);
  
  msg_dbus_to_gavl(msg, &m);
  gavl_msg_dump(&m, 2);
  gavl_msg_free(&m);
  }
#endif

typedef struct
  {
  /* For constructing the gavl message */
  int ns;
  int id;
  gavl_dictionary_t m;
  bg_msg_sink_t * sink;
  } match_t;

struct bg_dbus_connection_s
  {
  DBusConnection * conn;
  match_t * listeners;
  int num_listeners;
  int listeners_alloc;

  pthread_mutex_t mutex;

  gavl_dictionary_t obj_tree;
  };

static bg_dbus_connection_t * session_conn = NULL;
static bg_dbus_connection_t * system_conn = NULL;

static pthread_t dbus_thread;
static int do_join = 0;

static pthread_mutex_t conn_mutex = PTHREAD_MUTEX_INITIALIZER;

static void dbus_lock()
  {
  //  fprintf(stderr, "LOCK\n");
  //  print_trace();

  pthread_mutex_lock(&conn_mutex);
  }

static void dbus_unlock()
  {
  //  fprintf(stderr, "UNLOCK\n");
  pthread_mutex_unlock(&conn_mutex);
  }

void bg_dbus_connection_lock(bg_dbus_connection_t * conn)
  {
  //  fprintf(stderr, "CONN LOCK %p\n", conn);
  //  print_trace();
  
  pthread_mutex_lock(&conn->mutex);
  }

void bg_dbus_connection_unlock(bg_dbus_connection_t * conn)
  {
  //  fprintf(stderr, "CONN UNLOCK %p\n", conn);
  pthread_mutex_unlock(&conn->mutex);
  }

static int dbus_iter_to_val(DBusMessageIter * iter, gavl_value_t * val)
  {
  DBusBasicValue value;
  
  int arg_type = dbus_message_iter_get_arg_type(iter);

  switch(arg_type)
    {
    case DBUS_TYPE_BYTE:
      dbus_message_iter_get_basic(iter, &value);
      gavl_value_set_int(val, value.byt);
      break;
    case DBUS_TYPE_BOOLEAN:
      dbus_message_iter_get_basic(iter, &value);
      gavl_value_set_int(val, value.bool_val);
      break;
    case DBUS_TYPE_INT16:
      dbus_message_iter_get_basic(iter, &value);
      gavl_value_set_int(val, value.i16);
      break;
    case DBUS_TYPE_UINT16:
      dbus_message_iter_get_basic(iter, &value);
      gavl_value_set_int(val, value.u16);
      break;
    case DBUS_TYPE_INT32:
      dbus_message_iter_get_basic(iter, &value);
      gavl_value_set_int(val, value.i32);
      break;
    case DBUS_TYPE_UINT32:
      dbus_message_iter_get_basic(iter, &value);
      gavl_value_set_int(val, value.u32);
      break;
    case DBUS_TYPE_INT64:
      dbus_message_iter_get_basic(iter, &value);
      gavl_value_set_long(val, value.i64);
      break;
    case DBUS_TYPE_UINT64:
      dbus_message_iter_get_basic(iter, &value);
      gavl_value_set_long(val, value.u64);
      break;
    case DBUS_TYPE_DOUBLE:
      dbus_message_iter_get_basic(iter, &value);
      gavl_value_set_float(val, value.dbl);
      break;
    case DBUS_TYPE_STRING:
    case DBUS_TYPE_OBJECT_PATH:
      dbus_message_iter_get_basic(iter, &value);
      gavl_value_set_string(val, value.str);
      break;
    case DBUS_TYPE_ARRAY:
      {
      int sub_type;
      DBusMessageIter sub;
      
      sub_type = dbus_message_iter_get_element_type(iter);

      dbus_message_iter_recurse(iter, &sub);
      
      if(sub_type == DBUS_TYPE_DICT_ENTRY)
        {
        const char * name;
        gavl_value_t subname;
        gavl_value_t subvalue;
        DBusMessageIter subsub;
        gavl_dictionary_t * dict = NULL;

        dict = gavl_value_set_dictionary(val);
        
        while((sub_type = dbus_message_iter_get_arg_type(&sub)) != DBUS_TYPE_INVALID)
          {
          dbus_message_iter_recurse(&sub, &subsub);
        
          gavl_value_init(&subname);
          gavl_value_init(&subvalue);

          if(dbus_iter_to_val(&subsub, &subname) &&
             dbus_message_iter_next(&subsub) &&
             dbus_iter_to_val(&subsub, &subvalue) &&
             (name = gavl_value_get_string(&subname)))
            {
            gavl_dictionary_set(dict, name, &subvalue);
            }
          else
            {
            fprintf(stderr, "Invalid dictionary entry");
            return 0;
            }
          gavl_value_free(&subvalue);
          gavl_value_free(&subname);
          dbus_message_iter_next(&sub);
          }
        }
      else if(sub_type == DBUS_TYPE_BYTE)
        {
        int num, i;
        gavl_buffer_t buf;
        gavl_buffer_init(&buf);
        
        /* byte arrays are mostly string actually */
        num = dbus_message_iter_get_element_count(iter);

        gavl_buffer_alloc(&buf, num);

        for(i = 0; i < num; i++)
          {
          dbus_message_iter_get_basic(&sub, &value);
          buf.buf[i] = value.byt;
          dbus_message_iter_next(&sub);
          }
        buf.len = num;
        if(memchr(buf.buf, 0x00, buf.len) == (buf.buf + (buf.len-1)))
          gavl_value_set_string(val, (const char*)buf.buf);
        else
          fprintf(stderr, "Invalid byte buffer\n");
        
        gavl_buffer_free(&buf);
        }
      else
        {
        gavl_array_t * arr = NULL;
        
        arr = gavl_value_set_array(val);

        while((sub_type = dbus_message_iter_get_arg_type(&sub)) != DBUS_TYPE_INVALID)
          {
          gavl_value_t subvalue;
          gavl_value_init(&subvalue);
          dbus_iter_to_val(&sub, &subvalue);
          gavl_array_splice_val_nocopy(arr, -1, 0, &subvalue);
          dbus_message_iter_next(&sub);
          }
        }
      
      }
      break;
    case DBUS_TYPE_VARIANT:
      {
      DBusMessageIter sub;
      dbus_message_iter_recurse(iter, &sub);
      return dbus_iter_to_val(&sub, val);
      }
      break;
#if 1
    case DBUS_TYPE_STRUCT:
      {
      DBusMessageIter sub;
      int sub_type;

      gavl_array_t * arr = NULL;
      
      arr = gavl_value_set_array(val);

      dbus_message_iter_recurse(iter, &sub);
      
      while((sub_type = dbus_message_iter_get_arg_type(&sub)) != DBUS_TYPE_INVALID)
        {
        gavl_value_t subvalue;
        gavl_value_init(&subvalue);
        dbus_iter_to_val(&sub, &subvalue);
        gavl_array_splice_val_nocopy(arr, -1, 0, &subvalue);
        dbus_message_iter_next(&sub);
        }

      //      fprintf(stderr, "Read DBUS struct\n");
      //      gavl_array_dump(arr, 2);
      
      }
      break;
#endif
    default:
      fprintf(stderr, "Don't know how to convert type %d to gavl\n", arg_type);
      return 0;
    }
  return 1;
  }



static void match_free(match_t * m)
  {
  gavl_dictionary_free(&m->m);
  memset(m, 0, sizeof(*m));
  }

static void bg_dbus_connection_destroy(bg_dbus_connection_t * conn)
  {
  int i;

  //  fprintf(stderr, "bg_dbus_connection_destroy\n");

  if(conn->listeners)
    {
    for(i = 0; i < conn->num_listeners; i++)
      match_free(conn->listeners + i);
    free(conn->listeners);
    }

  gavl_dictionary_free(&conn->obj_tree);
  
  if(conn->conn)
    {
    dbus_connection_close(conn->conn);
    dbus_connection_unref(conn->conn);
    }
  pthread_mutex_destroy(&conn->mutex);

  free(conn);
  }

static bg_dbus_connection_t * bg_dbus_connection_create(DBusBusType type)
  {
  DBusError err; 
  
  bg_dbus_connection_t * ret = calloc(1, sizeof(*ret));
  dbus_error_init(&err);
  pthread_mutex_init(&ret->mutex, NULL);
  
  ret->conn = dbus_bus_get_private(type, &err);
  
  if(dbus_error_is_set(&err))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Canntot connect to %s bus: %s",
           ((type == DBUS_BUS_SESSION) ? "session" : "system"),
           err.message); 
    goto fail;
    }
  dbus_error_free(&err); 

  dbus_bus_add_match(ret->conn,
                     "member='Introspect',interface='org.freedesktop.DBus.Introspectable'", NULL);
  
  return ret;

  fail:
  dbus_error_free(&err); 
  bg_dbus_connection_destroy(ret);
  return NULL;
  }

static void msg_dbus_to_gavl(DBusMessage * msg, gavl_msg_t * ret)
  {
  gavl_value_t arg_val;
  int arg_idx = 0;
  int arg_type;
  uint32_t serial;
  DBusMessageIter iter;
  
  gavl_msg_set_id_ns(ret, 0, 0);
  
  gavl_dictionary_set_string(&ret->header, BG_DBUS_META_SENDER,      dbus_message_get_sender(msg));
  gavl_dictionary_set_string(&ret->header, BG_DBUS_META_INTERFACE,   dbus_message_get_interface(msg));
  gavl_dictionary_set_string(&ret->header, BG_DBUS_META_MEMBER,      dbus_message_get_member(msg));
  gavl_dictionary_set_string(&ret->header, BG_DBUS_META_PATH,        dbus_message_get_path(msg));
  gavl_dictionary_set_string(&ret->header, BG_DBUS_META_TYPE,        get_msg_type_name(dbus_message_get_type(msg)));
  gavl_dictionary_set_string(&ret->header, BG_DBUS_META_DESTINATION, dbus_message_get_destination(msg));
  
  if((serial = dbus_message_get_serial(msg)))
    gavl_dictionary_set_long(&ret->header, BG_DBUS_META_SERIAL, serial);

  if((serial = dbus_message_get_reply_serial(msg)))
    gavl_dictionary_set_long(&ret->header, BG_DBUS_META_REPLY_SERIAL, serial);
  
  dbus_message_iter_init(msg, &iter);

  while((arg_type = dbus_message_iter_get_arg_type (&iter)) != DBUS_TYPE_INVALID)
    {
    gavl_value_init(&arg_val);
    dbus_iter_to_val(&iter, &arg_val);
    gavl_msg_set_arg_nocopy(ret, arg_idx, &arg_val);
    dbus_message_iter_next(&iter);
    arg_idx++;
    }
  }

typedef struct
  {
  int ret;
  const gavl_msg_t * msg;
  } match_foreach_t;

static void match_foreach_func(void * priv, const char * name, const gavl_value_t * val)
  {
  const char * str;
  const char * var_str;

  match_foreach_t * f = priv;
  
  if(!f->ret)
    return;
  
  if(!strcmp(name, "arg0namespace"))
    {
    if(!(str = gavl_msg_get_arg_string_c(f->msg, 0)) ||
       !(var_str = gavl_value_get_string(val)) ||
       !gavl_string_starts_with(str, var_str))
      f->ret = 0;
    }
  else if(strcmp(name, BG_DBUS_META_RULE))
    {
    if(!(var_str = gavl_value_get_string(val)) ||
       !(str = gavl_dictionary_get_string(&f->msg->header, name)) ||
       strcmp(var_str, str))
      f->ret = 0;
    }
  }

static int msg_matches(gavl_msg_t * msg, const gavl_dictionary_t * matches)
  {
  match_foreach_t f;
  f.ret = 1;
  f.msg = msg;
  
  gavl_dictionary_foreach(matches, match_foreach_func, &f);
  return f.ret;
  }

/* Needs lock */
char * bg_dbus_connection_request_name(bg_dbus_connection_t * conn, const char * name)
  {
  int result;
  char * ret = NULL;
  DBusError err; 
  dbus_error_init(&err);
  
  //  bg_dbus_connection_lock(conn);
  
  result = dbus_bus_request_name(conn->conn, name, 0, &err);

  switch(result)
    {
    case DBUS_REQUEST_NAME_REPLY_IN_QUEUE:
      ret = bg_sprintf("%s.pid%d", name, getpid());
      
      if(dbus_bus_request_name(conn->conn, ret, 0, &err) != 
         DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER)
        {
        free(ret);
        ret = NULL;
        }
      break;
    case DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER:
    case DBUS_REQUEST_NAME_REPLY_ALREADY_OWNER:
      ret = gavl_strdup(name);
      break;
    }
  //  bg_dbus_connection_unlock(conn);
  return ret;
  }

static const gavl_dictionary_t * get_obj_el(const gavl_dictionary_t * tree, const char * path)
  {
  int i;
  char ** path_broken;

  if(!strcmp(path, "/"))
    return tree;
  
  path_broken = gavl_strbreak(path+1, '/');

  
  
  i = 0;
  while(path_broken[i])
    {
    if(!(tree = gavl_dictionary_get_dictionary(tree, path_broken[i])))
      {
      gavl_strbreak_free(path_broken);
      return NULL;
      }
    i++;
    }
  
  gavl_strbreak_free(path_broken);
  return tree;
  }

static void handle_introspect(bg_dbus_connection_t * conn, const char * path, DBusMessage * msg)
  {
  const char * str;
  DBusMessage * reply;
  const gavl_dictionary_t * el;
  DBusMessageIter iter;
    
  fprintf(stderr, "Handle introspect: %s\n", path);
  
  if(!(el = get_obj_el(&conn->obj_tree, path)))
    {
    /* TODO: Trigger error */
    }
  else if((str = gavl_dictionary_get_string(el, BG_DBUS_META_INTROSPECT)))
    {
    reply = dbus_message_new_method_return(msg);

    dbus_message_iter_init_append(reply, &iter);
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &str);
    dbus_connection_send(conn->conn, reply, NULL);
    dbus_connection_flush(conn->conn);

    dbus_message_unref(reply);
    }
  else // Return children
    {
    char * xml;
    char * tmp_string;
    int i;

    xml = gavl_strdup("<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\"\n"
                      "\"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">\n"
                      "<node>\n");

    for(i = 0; i < el->num_entries; i++)
      {
      tmp_string = bg_sprintf("  <node name=\"%s\"/>\n", el->entries[i].name);
      xml = gavl_strcat(xml, tmp_string);
      free(tmp_string);
      }
    xml = gavl_strcat(xml, "</node>\n");
    
    reply = dbus_message_new_method_return(msg);

    dbus_message_iter_init_append(reply, &iter);
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &xml);
    dbus_connection_send(conn->conn, reply, NULL);
    dbus_connection_flush(conn->conn);

    dbus_message_unref(reply);
    
    free(xml);
    }
  
  }

static int bg_dbus_connection_update(bg_dbus_connection_t * conn)
  {
  int i;
  DBusMessage * msg;
  int ret = 0;
  gavl_msg_t gavl_msg;
  const char * str;
  
  
  while(1)
    {
    bg_dbus_connection_lock(conn);
    
    dbus_connection_read_write(conn->conn, 0);
    
    if(!(msg = dbus_connection_pop_message(conn->conn)))
      {
      bg_dbus_connection_unlock(conn);
      break;
      }
    
    bg_dbus_connection_unlock(conn);
    
    if((str = dbus_message_get_interface(msg)) && !strcmp(str, "org.freedesktop.DBus.Introspectable") &&
       (str = dbus_message_get_member(msg)) && !strcmp(str, "Introspect") &&
       (str = dbus_message_get_path(msg)))
      {
      handle_introspect(conn, str, msg);
      continue;
      }
    
    gavl_msg_init(&gavl_msg);
    msg_dbus_to_gavl(msg, &gavl_msg);

    //    fprintf(stderr, "Got message:\n");
    //    gavl_msg_dump(&gavl_msg, 2);
        
    /* Handle message */
    for(i = 0; i < conn->num_listeners; i++)
      {
      if(msg_matches(&gavl_msg, &conn->listeners[i].m))
        {
        gavl_msg_t * msg1;

        msg1 = bg_msg_sink_get(conn->listeners[i].sink);
        gavl_msg_copy(msg1, &gavl_msg);
        gavl_dictionary_set_int(&msg1->header, GAVL_MSG_NS, conn->listeners[i].ns);
        msg1->NS = conn->listeners[i].ns;

        gavl_dictionary_set_int(&msg1->header, GAVL_MSG_ID, conn->listeners[i].id);
        msg1->ID = conn->listeners[i].id;
        bg_msg_sink_put(conn->listeners[i].sink);
        }
      }
    
    gavl_msg_free(&gavl_msg);
    
    /* Free */
    dbus_message_unref(msg);
    
    ret++;
    }

  
  return ret;
  }


static void * dbus_thread_func(void * data)
  {
  int ret = 0;
  bg_dbus_connection_t * system_conn_local;
  bg_dbus_connection_t * session_conn_local;
  
  gavl_time_t delay_time = GAVL_TIME_SCALE / 50; // 20 ms
  
  while(1)
    {
    ret = 0;

    //    fprintf(stderr, "dbus_thread_func 1\n");
    
    dbus_lock();
    system_conn_local = system_conn;
    session_conn_local = session_conn;

    if(do_join)
      {
      dbus_unlock();
      break;
      }

    dbus_unlock();
    
    //    fprintf(stderr, "dbus_thread_func 2\n");

    if(system_conn_local)
      ret += bg_dbus_connection_update(system_conn_local);

    //    fprintf(stderr, "dbus_thread_func 3\n");
    
    if(session_conn_local)
      ret += bg_dbus_connection_update(session_conn_local);
    
    //    fprintf(stderr, "dbus_thread_func 4\n");
    
    if(!session_conn_local && !system_conn_local)
      break;
    
    //    fprintf(stderr, "dbus_thread_func 5\n");
    
    if(!ret)
      gavl_time_delay(&delay_time);
    
    }
  return NULL;
  }

const char * bg_dbus_connection_get_addr(DBusBusType type)
  {
  bg_dbus_connection_t * c = bg_dbus_connection_get(type);
  
  return dbus_bus_get_unique_name(c->conn);
  }

bg_dbus_connection_t * bg_dbus_connection_get(DBusBusType type)
  {
  int no_connections = 0;

  bg_dbus_connection_t * ret = NULL;
  
  dbus_lock();
  
  if(!session_conn && !system_conn)
    no_connections = 1;
  
  switch(type)
    {
    case DBUS_BUS_SESSION:
      if(!session_conn)
        session_conn = bg_dbus_connection_create(DBUS_BUS_SESSION);
      ret = session_conn;
      break;
    case DBUS_BUS_SYSTEM:
      if(!system_conn)
        system_conn = bg_dbus_connection_create(DBUS_BUS_SYSTEM);
      ret = system_conn;
      break;
    case DBUS_BUS_STARTER:
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "DBUS_BUS_STARTER not supported"); 
      break;
    }

  dbus_unlock();
  
  /* Start thread */
  if(no_connections && ret)
    {
    //    fprintf(stderr, "Starting dbus thread\n");
    pthread_create(&dbus_thread, NULL, dbus_thread_func, NULL);  
    }

  
  return ret;
  }

void
bg_dbus_connection_add_listener(bg_dbus_connection_t * conn,
                                const char * rule,
                                bg_msg_sink_t * sink, int ns, int id)
  {
  char ** rules;
  char ** r;
  int i;
  match_t * m;
  int val_len;

  //  fprintf(stderr, "bg_dbus_connection_add_listener %s %p\n",  rule, sink);
  
  bg_dbus_connection_lock(conn);
  
  if(conn->num_listeners + 1 > conn->listeners_alloc)
    {
    conn->listeners_alloc += 16;
    conn->listeners = realloc(conn->listeners,
                              conn->listeners_alloc * sizeof(*conn->listeners));
    memset(conn->listeners + conn->num_listeners, 0,
           (conn->listeners_alloc - conn->num_listeners) * sizeof(*conn->listeners));
    }
  
  m = conn->listeners + conn->num_listeners;
  
  m->ns = ns;
  m->id = id;
  m->sink = sink;

  dbus_bus_add_match(conn->conn, rule, NULL);
  
  gavl_dictionary_set_string(&m->m, BG_DBUS_META_RULE, rule);
  
  /* Disassemble rule */
  rules = gavl_strbreak(rule, ',');

  i = 0;
  
  while(rules[i])
    {
    r = gavl_strbreak(rules[i], '=');
    if(r && r[0] && r[1])
      {
      /* remove leading and trailing ' */
      val_len = strlen(r[1]);
      
      if((r[1][0] == '\'') && (r[1][val_len - 1] == '\''))
        {
        r[1][val_len - 1] = '\0';
        val_len--;
        
        /*
         *  one less because leading ' is skipped, one more because \0 is included
         *  -> val_len bytes are moved
         */
        memmove(r[1], r[1]+1, val_len);
        }
      gavl_dictionary_set_string(&m->m, r[0], r[1]);
      }
    gavl_strbreak_free(r);
    i++;
    }
  
  gavl_strbreak_free(rules);
  
  conn->num_listeners++;

  bg_dbus_connection_unlock(conn);
  
  }

void
bg_dbus_connection_del_listeners(bg_dbus_connection_t * conn,
                                 bg_msg_sink_t * sink)
  {
  int i;
  int num_deleted = 0;

  pthread_mutex_lock(&conn->mutex);

  i = 0;
  
  while(i < conn->num_listeners)
    {
    if(conn->listeners[i].sink == sink)
      {
      dbus_bus_remove_match(conn->conn, gavl_dictionary_get_string(&conn->listeners[i].m, BG_DBUS_META_RULE), NULL);
      
      match_free(&conn->listeners[i]);
      
      if(i < conn->num_listeners - 1)
        {
        memmove(conn->listeners + i, conn->listeners + (i + 1), 
                (conn->num_listeners - 1 - i) * sizeof(*conn->listeners));
        }
      conn->num_listeners--;
      num_deleted++;
      }
    else
      i++;
    }

  /* Clear memory of deleted matches */
  if(num_deleted > 0)
    memset(conn->listeners + conn->num_listeners, 0, num_deleted * sizeof(*conn->listeners));
  
  bg_dbus_connection_unlock(conn);
  }

gavl_msg_t * bg_dbus_connection_call_method(bg_dbus_connection_t * conn,
                                            DBusMessage * req)
  {
  gavl_msg_t * msg = NULL;
  DBusMessage * ret;
  DBusError err; 
  dbus_error_init(&err);

  bg_dbus_connection_lock(conn);
  
  ret = dbus_connection_send_with_reply_and_block(conn->conn, req, 1000, &err);

  if(dbus_error_is_set(&err))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Message call failed: %s", err.message);
    fprintf(stderr, "Message call failed: %s\n", err.message);
    dbus_error_free(&err); 
    bg_dbus_connection_unlock(conn);
    return NULL;
    }
  
  msg = gavl_msg_create();
  msg_dbus_to_gavl(ret, msg);
  dbus_message_unref(ret);
  bg_dbus_connection_unlock(conn);
  return msg;
  }

char * bg_dbus_get_name_owner(bg_dbus_connection_t * conn,
                              const char * name)
  {
  char * ret = NULL;
  
  DBusMessage * req;
  gavl_msg_t * res;
        
  req = dbus_message_new_method_call("org.freedesktop.DBus",
                                     "/",
                                     "org.freedesktop.DBus",
                                     "GetNameOwner");

  dbus_message_append_args(req, DBUS_TYPE_STRING, &name, DBUS_TYPE_INVALID);
        
  res = bg_dbus_connection_call_method(conn, req);
  dbus_message_unref(req);

  /* */

  if(res)
    {
    ret = gavl_strdup(gavl_msg_get_arg_string_c(res, 0));
    gavl_msg_destroy(res);
    }
  
  return ret;
  }



/* Get properties */

static gavl_msg_t * get_property(bg_dbus_connection_t * conn,
                                 const char * addr,
                                 const char * path,
                                 const char * interface,
                                 const char * name)
  {
  DBusMessage * req;
  gavl_msg_t * res;

  // fprintf(stderr, "get property: %s %s %s %s\n", addr, path, interface, name);
  
  req = dbus_message_new_method_call(addr, path, "org.freedesktop.DBus.Properties", "Get");

  dbus_message_append_args(req, DBUS_TYPE_STRING, &interface,
                           DBUS_TYPE_STRING, &name, DBUS_TYPE_INVALID);

  res = bg_dbus_connection_call_method(conn, req);

  dbus_message_unref(req);
  
  return res;
  }
  
char * bg_dbus_get_string_property(bg_dbus_connection_t * conn,
                                   const char * addr,
                                   const char * path,
                                   const char * interface,
                                   const char * name)
  {
  gavl_msg_t * res;
  char * ret;
  
  if(!(res = get_property(conn, addr, path, interface, name)))
    return NULL;
  
  ret = gavl_strdup(gavl_msg_get_arg_string_c(res, 0));
  
  gavl_msg_destroy(res);
  return ret;
  }

int64_t bg_dbus_get_long_property(bg_dbus_connection_t * conn,
                                  const char * addr,
                                  const char * path,
                                  const char * interface,
                                  const char * name)
  {
  gavl_msg_t * res;
  int64_t ret;

  if(!(res = get_property(conn, addr, path, interface, name)))
    return GAVL_TIME_UNDEFINED;
  
  ret = gavl_msg_get_arg_long(res, 0);
  
  gavl_msg_destroy(res);
  return ret;
  }

int bg_dbus_get_int_property(bg_dbus_connection_t * conn,
                             const char * addr,
                             const char * path,
                             const char * interface,
                             const char * name)
  {
  gavl_msg_t * res;
  int64_t ret;

  if(!(res = get_property(conn, addr, path, interface, name)))
    return 0;
  
  ret = gavl_msg_get_arg_int(res, 0);
  
  gavl_msg_destroy(res);
  return ret;
  }

gavl_array_t * bg_dbus_get_array_property(bg_dbus_connection_t * conn,
                                          const char * addr,
                                          const char * path,
                                          const char * interface,
                                          const char * name)
  {
  gavl_msg_t * res;
  gavl_array_t * ret;

  if(!(res = get_property(conn, addr, path, interface, name)))
    return NULL;

  //  fprintf(stderr, "bg_dbus_get_array_property result:\n");
  //  gavl_msg_dump(res, 2);
  //  fprintf(stderr, "\n");
  
  ret = gavl_array_create();

  gavl_msg_get_arg_array(res, 0, ret);
  
  gavl_msg_destroy(res);
  return ret;
  
  }

void bg_dbus_set_double_property(bg_dbus_connection_t * conn,
                                 const char * addr,
                                 const char * path,
                                 const char * interface,
                                 const char * name, double val)
  {
  DBusMessage * req;
  gavl_msg_t * res;
  DBusMessageIter iter;
  DBusMessageIter subiter;
  
  req = dbus_message_new_method_call(addr, path, "org.freedesktop.DBus.Properties",
                                     "Set");

  dbus_message_append_args(req, DBUS_TYPE_STRING, &interface,
                           DBUS_TYPE_STRING, &name, DBUS_TYPE_INVALID);

  dbus_message_iter_init_append(req, &iter);
 
  dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "d", &subiter);

  dbus_message_iter_append_basic(&subiter, DBUS_TYPE_DOUBLE, &val);
  
  dbus_message_iter_close_container(&iter, &subiter);
  
  res = bg_dbus_connection_call_method(conn, req);
  
  dbus_message_unref(req);

  if(res)
    gavl_msg_destroy(res);
  }

gavl_dictionary_t *
bg_dbus_get_properties(bg_dbus_connection_t * conn,
                       const char * addr,
                       const char * path,
                       const char * interface)
  {
  DBusMessage * req;
  gavl_msg_t * res;
  gavl_dictionary_t * ret;
  
  const gavl_value_t * val;
  const gavl_dictionary_t * dict;
  
  req = dbus_message_new_method_call(addr, path, "org.freedesktop.DBus.Properties",
                                     "GetAll");

  dbus_message_append_args(req, DBUS_TYPE_STRING, &interface, DBUS_TYPE_INVALID);

  if(!(res = bg_dbus_connection_call_method(conn, req)) ||
     !(val = gavl_msg_get_arg_c(res, 0)) ||
     !(dict = gavl_value_get_dictionary(val)))
    return NULL;
  
  ret = gavl_dictionary_create();
  gavl_dictionary_copy(ret, dict);
  
  dbus_message_unref(req);
  gavl_msg_destroy(res);
  return ret;
  }

DBusMessage * bg_dbus_msg_new_reply(const gavl_msg_t * msg)
  {
  int64_t serial;
  DBusMessage * ret;
  const char *sender;
    
  if(!gavl_dictionary_get_long(&msg->header, BG_DBUS_META_SERIAL, &serial))
    return NULL;

  sender = gavl_dictionary_get_string(&msg->header, BG_DBUS_META_SENDER);
  
  ret = dbus_message_new(DBUS_MESSAGE_TYPE_METHOD_RETURN);

  dbus_message_set_no_reply(ret, TRUE);
    
  dbus_message_set_destination(ret, sender);
  dbus_message_set_reply_serial(ret, serial);
  
  //  fprintf(stderr, "bg_dbus_msg_new_reply:\n");
  //  bg_dbus_msg_dump(ret);
  
  return ret;
  }

void bg_dbus_send_error(bg_dbus_connection_t * conn,
                                 const gavl_msg_t * msg,
                                 const char * error_name, const char * error_msg)
  {
  int64_t serial;
  DBusMessage * ret;
  const char *sender;
  DBusMessageIter iter;
  
  if(!gavl_dictionary_get_long(&msg->header, BG_DBUS_META_SERIAL, &serial))
    return;
  
  sender = gavl_dictionary_get_string(&msg->header, BG_DBUS_META_SENDER);

  
  ret = dbus_message_new(DBUS_MESSAGE_TYPE_ERROR);
  
  dbus_message_set_no_reply(ret, TRUE);
    
  dbus_message_set_destination(ret, sender);
  dbus_message_set_reply_serial(ret, serial);

  if(!error_name)
    error_name = DBUS_ERROR_FAILED;

  gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Sending error to %s: %s %s", sender, error_name, error_msg);
  
  dbus_message_set_error_name(ret, error_name);

  if(error_msg)
    {
    dbus_message_iter_init_append(ret, &iter);
    dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &error_msg);
    }
  
  //  fprintf(stderr, "bg_dbus_msg_new_reply:\n");
  //  bg_dbus_msg_dump(ret);

  bg_dbus_send_msg(conn, ret);
  
  dbus_message_unref(ret);
  }

void bg_value_to_dbus_variant(const char * sig,
                              const gavl_value_t * val,
                              DBusMessageIter *iter,
                              void (*val_to_variant)(const gavl_value_t*, DBusMessageIter*))
  {
  DBusMessageIter subiter;

     
  dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, sig, &subiter);

  if(val_to_variant)
    {
    val_to_variant(val, &subiter);
    }
  else
    {
    if(!strcmp(sig, "b"))
      {
      dbus_message_iter_append_basic(&subiter, DBUS_TYPE_BOOLEAN, &val->v.i);
      }
    else if(!strcmp(sig, "d"))
      {
      dbus_message_iter_append_basic(&subiter, DBUS_TYPE_DOUBLE, &val->v.d);
      }
    else if(!strcmp(sig, "s"))
      {
      //      fprintf(stderr, "Blupp 1\n");
      //      gavl_value_dump(val, 2);
      //      fprintf(stderr, "\n");
      dbus_message_iter_append_basic(&subiter, DBUS_TYPE_STRING, &val->v.str);
      //      fprintf(stderr, "Blupp 2\n");
      }
    else if(!strcmp(sig, "o"))
      {
      //      fprintf(stderr, "Blupp 1\n");
      //      gavl_value_dump(val, 2);
      //      fprintf(stderr, "\n");
      dbus_message_iter_append_basic(&subiter, DBUS_TYPE_OBJECT_PATH, &val->v.str);
      //      fprintf(stderr, "Blupp 2\n");
      }
    else if(!strcmp(sig, "x"))
      {
      dbus_message_iter_append_basic(&subiter, DBUS_TYPE_INT64, &val->v.l);
      }
    else if(!strcmp(sig, "as"))
      {
      /* String array */
      int i;
      DBusMessageIter subsubiter;
      const char * str;
      const gavl_value_t * item;
    
      int num;
    
      dbus_message_iter_open_container(&subiter, DBUS_TYPE_ARRAY, "s", &subsubiter);

      num = gavl_value_get_num_items(val);
    
      for(i = 0; i < num; i++)
        {
        if((item = gavl_value_get_item(val, i)) &&
           (str = gavl_value_get_string(item)))
          dbus_message_iter_append_basic(&subsubiter, DBUS_TYPE_STRING, &str);
        }
      dbus_message_iter_close_container(&subiter, &subsubiter);
      }
    else if(!strcmp(sig, "ao"))
      {
      /* String array */
      int i;
      DBusMessageIter subsubiter;
      const char * str;
      const gavl_value_t * item;
    
      int num;
    
      dbus_message_iter_open_container(&subiter, DBUS_TYPE_ARRAY, "s", &subsubiter);

      num = gavl_value_get_num_items(val);
    
      for(i = 0; i < num; i++)
        {
        if((item = gavl_value_get_item(val, i)) &&
           (str = gavl_value_get_string(item)))
          dbus_message_iter_append_basic(&subsubiter, DBUS_TYPE_OBJECT_PATH, &str);
        }
      dbus_message_iter_close_container(&subiter, &subsubiter);
      }
    }
  dbus_message_iter_close_container(iter, &subiter);
  }

static const bg_dbus_property_t * get_property_desc(const bg_dbus_property_t * desc,
                                                    const char * name)
  {
  int i = 0;

  while(desc[i].name)
    {
    if(!strcmp(name, desc[i].name))
      return &desc[i];
    i++;
    }
  return NULL;
  }

void bg_dbus_property_get(const bg_dbus_property_t * desc, gavl_dictionary_t * prop,
                          const char * name,
                          const gavl_msg_t * req, bg_dbus_connection_t * conn)
  {
  DBusMessage * reply;
  DBusMessageIter iter;
  const gavl_value_t * val;
  
  if(!(desc = get_property_desc(desc, name)))
    return; // TODO: Send error

  if(!(val = gavl_dictionary_get(prop, name)))
    return; // TODO: Send error
  
  reply = bg_dbus_msg_new_reply(req);
  
  /* Add argument */
  dbus_message_iter_init_append(reply, &iter);
  bg_value_to_dbus_variant(desc->dbus_type, val, &iter, desc->val_to_variant);
  bg_dbus_send_msg(conn, reply);

  //  fprintf(stderr, "property_get\n");
  //  bg_dbus_msg_dump(reply);
  //  fprintf(stderr, "\n");
  
  dbus_message_unref(reply);
  }

void bg_dbus_properties_get(const bg_dbus_property_t * desc, const gavl_dictionary_t * prop,
                            const gavl_msg_t * req, bg_dbus_connection_t * conn)
  {
  int i = 0;
  DBusMessage * reply;
  const gavl_value_t * val;
  DBusMessageIter iter;
  DBusMessageIter subiter;
  DBusMessageIter subsubiter;
  
  reply = bg_dbus_msg_new_reply(req);
  
  dbus_message_iter_init_append(reply, &iter);
  
  /* Add argument */

  dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "{sv}", &subiter);

  if(desc && prop)
    {
    while(desc[i].name)
      {
      if((val = gavl_dictionary_get(prop, desc[i].name)))
        {
        dbus_message_iter_open_container(&subiter, DBUS_TYPE_DICT_ENTRY, NULL, &subsubiter);
        dbus_message_iter_append_basic(&subsubiter, DBUS_TYPE_STRING, &desc[i].name);
        bg_value_to_dbus_variant(desc[i].dbus_type, val, &subsubiter, desc[i].val_to_variant);
        dbus_message_iter_close_container(&subiter, &subsubiter);
        }
    
      i++;
      }
    }
  
  dbus_message_iter_close_container(&iter, &subiter);

  //  fprintf(stderr, "bg_dbus_properties_get\n");
  //  bg_dbus_msg_dump(reply);
  
  bg_dbus_send_msg(conn, reply);
  dbus_message_unref(reply);
  }

void bg_dbus_properties_init(const bg_dbus_property_t * desc, gavl_dictionary_t * prop)
  {
  int i = 0;

  while(desc[i].name)
    {
    if(desc[i].val_default.type != GAVL_TYPE_UNDEFINED)
      gavl_dictionary_set(prop, desc[i].name, &desc[i].val_default);
    i++;
    }
  }

void bg_dbus_send_msg(bg_dbus_connection_t * conn, DBusMessage * msg)
  {
  //  fprintf(stderr, "bg_dbus_send_msg:\n");
  //  bg_dbus_msg_dump(msg);
  
  bg_dbus_connection_lock(conn);
  dbus_connection_send(conn->conn, msg, NULL);
  dbus_connection_flush(conn->conn);

  bg_dbus_connection_unlock(conn);
  }

#if 0
static DBusHandlerResult msg_func(DBusConnection *connection, DBusMessage *message, void *user_data)
  {
  gavl_msg_t * gavl_msg;
  bg_msg_sink_t * sink = user_data;
  gavl_msg = bg_msg_sink_get(sink);
  msg_dbus_to_gavl(message, gavl_msg); 
  bg_msg_sink_put(sink);
  return DBUS_HANDLER_RESULT_HANDLED;
  }
#endif


void bg_dbus_register_object(bg_dbus_connection_t * conn, const char * path,
                             bg_msg_sink_t * sink, const char * introspection_xml)
  {
  int i;
  char ** path_broken;
  char * match;
  gavl_dictionary_t * tree_el = &conn->obj_tree;
  
  path_broken = gavl_strbreak(path+1, '/');
  
  bg_dbus_connection_lock(conn);

  i = 0;
  while(path_broken[i])
    {
    tree_el = gavl_dictionary_get_dictionary_create(tree_el, path_broken[i]);
    i++;
    }
  gavl_strbreak_free(path_broken);
  
  gavl_dictionary_set_string(tree_el, BG_DBUS_META_INTROSPECT, introspection_xml);
  
  bg_dbus_connection_unlock(conn);
  
  match = bg_sprintf("type='method_call',path='%s'", path);
  bg_dbus_connection_add_listener(conn, match, sink, BG_MSG_NS_PRIVATE, 1);
  free(match);
  }

void bg_dbus_unregister_object(bg_dbus_connection_t * conn, const char * path, bg_msg_sink_t * sink)
  {
  bg_dbus_connection_del_listeners(conn, sink);
  }
  
void bg_dbus_set_property_local(bg_dbus_connection_t * conn,
                                const char * path,
                                const char * interface,
                                const char * name, const gavl_value_t * val,
                                const bg_dbus_property_t * properties,
                                gavl_dictionary_t * dict,
                                int send_msg)
  {
  if(dict && val)
    gavl_dictionary_set(dict, name, val);

  if(send_msg)
    {
    int i;
    DBusMessageIter iter;
    DBusMessageIter subiter;
    DBusMessageIter subsubiter;


    i = 0;
    while(properties[i].name)
      {
      if(!strcmp(name, properties[i].name))
        {
        DBusMessage * msg;
        msg = dbus_message_new_signal(path, "org.freedesktop.DBus.Properties",
                                      "PropertiesChanged");
        
        dbus_message_iter_init_append(msg, &iter);

        dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &interface);
        
        /* Changed properties */
        dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "{sv}", &subiter);

        if(val)
          {
          dbus_message_iter_open_container(&subiter, DBUS_TYPE_DICT_ENTRY, NULL, &subsubiter);
          dbus_message_iter_append_basic(&subsubiter, DBUS_TYPE_STRING, &properties[i].name);
          bg_value_to_dbus_variant(properties[i].dbus_type, val, &subsubiter, properties[i].val_to_variant);
          dbus_message_iter_close_container(&subiter, &subsubiter);
          }
        
        dbus_message_iter_close_container(&iter, &subiter);

        /* Invalidated properties */
        dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "s", &subiter);

        if(!val)
          dbus_message_iter_append_basic(&subiter, DBUS_TYPE_STRING, &properties[i].name);
        
        dbus_message_iter_close_container(&iter, &subiter);

        //        fprintf(stderr, "Property changed\n");
        //        bg_dbus_msg_dump(msg);
        bg_dbus_send_msg(conn, msg);
        dbus_message_unref(msg);
        return;
        }
      i++;
      }
    
    }

  }

#if defined(__GNUC__)

static void cleanup_dbus() __attribute__ ((destructor));

static void cleanup_dbus()
  {
  dbus_lock();
  do_join = 1;
  dbus_unlock();
  
  pthread_join(dbus_thread, NULL);

  if(session_conn)
    bg_dbus_connection_destroy(session_conn);
  
  if(system_conn)
    bg_dbus_connection_destroy(system_conn);
  
  }

#endif

