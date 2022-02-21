#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <inttypes.h>

#include <dbus/dbus.h>

#include <gavl/gavl.h>
#include <gavl/value.h>
#include <gavl/utils.h>

/* gcc -Wall `pkg-config --cflags dbus-1` `pkg-config --cflags gavl` -o dbus dbus.c `pkg-config --libs dbus-1` `pkg-config --libs gavl` */

static void do_indent(int num)
  {
  int i;
  for(i = 0; i < num; i++)
    fprintf(stderr, " ");
  }

static void dump_value(DBusMessageIter * iter, int indent)
  {
  DBusBasicValue value;
  int arg_type = dbus_message_iter_get_arg_type(iter);

  switch(arg_type)
    {
    case DBUS_TYPE_BYTE:
      do_indent(indent);
      fprintf(stderr, "byte ");
      dbus_message_iter_get_basic(iter, &value);
      fprintf(stderr, "%02x\n", value.byt);
      break;
    case DBUS_TYPE_BOOLEAN:
      do_indent(indent);
      fprintf(stderr, "boolean ");
      dbus_message_iter_get_basic(iter, &value);
      fprintf(stderr, "%d\n", value.bool_val);
      break;
    case DBUS_TYPE_INT16:
      do_indent(indent);
      fprintf(stderr, "int16 ");
      dbus_message_iter_get_basic(iter, &value);
      fprintf(stderr, "%d\n", value.i16);
      break;
    case DBUS_TYPE_UINT16:
      do_indent(indent);
      fprintf(stderr, "uint16 ");
      dbus_message_iter_get_basic(iter, &value);
      fprintf(stderr, "%d\n", value.u16);
      break;
    case DBUS_TYPE_INT32:
      do_indent(indent);
      fprintf(stderr, "int32 ");
      dbus_message_iter_get_basic(iter, &value);
      fprintf(stderr, "%d\n", value.i32);
      break;
    case DBUS_TYPE_UINT32:
      do_indent(indent);
      fprintf(stderr, "uint32 ");
      dbus_message_iter_get_basic(iter, &value);
      fprintf(stderr, "%d\n", value.u32);
      break;
    case DBUS_TYPE_INT64:
      do_indent(indent);
      fprintf(stderr, "int64 ");
      dbus_message_iter_get_basic(iter, &value);
      fprintf(stderr, "%"PRId64"\n", value.i64);
      break;
    case DBUS_TYPE_UINT64:
      do_indent(indent);
      fprintf(stderr, "uint64 ");
      dbus_message_iter_get_basic(iter, &value);
      fprintf(stderr, "%"PRId64"\n", value.u64);
      break;
    case DBUS_TYPE_DOUBLE:
      do_indent(indent);
      fprintf(stderr, "double ");
      dbus_message_iter_get_basic(iter, &value);
      fprintf(stderr, "%f\n", value.dbl);
      break;
    case DBUS_TYPE_STRING:
      do_indent(indent);
      fprintf(stderr, "string ");
      dbus_message_iter_get_basic(iter, &value);
      fprintf(stderr, "%s\n", value.str);
      break;
    case DBUS_TYPE_ARRAY:
      {       
      int type;
      DBusMessageIter sub;
      do_indent(indent);
      fprintf(stderr, "array\n");
      
      dbus_message_iter_recurse(iter, &sub);

      do_indent(indent + 2);
      fprintf(stderr, "[\n");
      while ((type = dbus_message_iter_get_arg_type(&sub)) != DBUS_TYPE_INVALID)
        { 
        dump_value(&sub, indent + 2);
        dbus_message_iter_next(&sub);
        }
      do_indent(indent + 2);
      fprintf(stderr, "]\n");

      }
      break;
   case DBUS_TYPE_DICT_ENTRY:
      {
      int type;
      DBusMessageIter sub;
      do_indent(indent);
      fprintf(stderr, "dict entry\n");

      dbus_message_iter_recurse(iter, &sub);

      do_indent(indent + 2);
      fprintf(stderr, "[\n");

      while ((type = dbus_message_iter_get_arg_type(&sub)) != DBUS_TYPE_INVALID)
        {
        dump_value(&sub, indent + 2);
        dbus_message_iter_next(&sub);
        }
      do_indent(indent + 2);
      fprintf(stderr, "]\n");
      }
      break;


    case DBUS_TYPE_VARIANT:
      {
      int type;
      DBusMessageIter sub;
      do_indent(indent);
      fprintf(stderr, "variant\n");

      dbus_message_iter_recurse(iter, &sub);

      do_indent(indent + 2);
      fprintf(stderr, "[\n");

      while ((type = dbus_message_iter_get_arg_type(&sub)) != DBUS_TYPE_INVALID)
        {
        dump_value(&sub, indent + 2);
        dbus_message_iter_next(&sub);
        }
      do_indent(indent + 2);
      fprintf(stderr, "]\n");
      }
      break;
    case DBUS_TYPE_STRUCT:
      do_indent(indent);
      fprintf(stderr, "struct\n");
      break;
    case DBUS_TYPE_OBJECT_PATH:
      do_indent(indent);
      fprintf(stderr, "object path ");
      dbus_message_iter_get_basic(iter, &value);
      fprintf(stderr, "%s\n", value.str);
      break;
    default:
      do_indent(indent);
      fprintf(stderr, "Unknown (%c)\n", arg_type);
    }
  

  }

static void dump_message(DBusMessage * msg)
  {
  int arg_type;
  int arg_idx;
  DBusMessageIter iter;

  int type = dbus_message_get_type(msg);

  fprintf(stderr, "  Type:      ");

  switch(type)
    {
    case DBUS_MESSAGE_TYPE_METHOD_CALL:
      fprintf(stderr, "Method call\n");
      break;
    case DBUS_MESSAGE_TYPE_METHOD_RETURN:
      fprintf(stderr, "Method return\n");
      break;
    case DBUS_MESSAGE_TYPE_ERROR:
      fprintf(stderr, "Error\n");
      break;
    case DBUS_MESSAGE_TYPE_SIGNAL:
      fprintf(stderr, "Signal\n");
      break;
    }

  fprintf(stderr, "  Path:      %s\n", dbus_message_get_path(msg));
  fprintf(stderr, "  Interface: %s\n", dbus_message_get_interface(msg));
  fprintf(stderr, "  member:    %s\n", dbus_message_get_member(msg));
  fprintf(stderr, "  Sender:    %s\n", dbus_message_get_sender(msg));

  arg_idx = 0;

  dbus_message_iter_init (msg, &iter);
  while ((arg_type = dbus_message_iter_get_arg_type (&iter)) != DBUS_TYPE_INVALID)
    {
    fprintf(stderr, "  arg %d\n", arg_idx);

    dump_value(&iter, 4);

    dbus_message_iter_next(&iter);
    arg_idx++;
    }

  }

static char * get_name_owner(DBusConnection * conn, char * name)
  {
  DBusError err;
  DBusMessage * req = NULL;
  DBusMessage * res = NULL;
  char * ret = NULL;
  const char * ret_c = NULL;
  dbus_error_init(&err);

  req = dbus_message_new_method_call("org.freedesktop.DBus", "/", "org.freedesktop.DBus", "GetNameOwner"); 	
  dbus_message_append_args(req, DBUS_TYPE_STRING, &name, DBUS_TYPE_INVALID);

  res = dbus_connection_send_with_reply_and_block(conn, req, 5000, &err);

  if(!res)
    goto fail; 

  dbus_message_get_args(res, &err, DBUS_TYPE_STRING, &ret_c, DBUS_TYPE_INVALID);

  ret = strdup(ret_c);

  fail:

  if(req)
    dbus_message_unref(req);
  if(res)
    dbus_message_unref(res);

  dbus_error_free(&err); 	

  return ret;

  }

typedef struct
  {
  char * udisks_name;
  gavl_array_t drives;
  gavl_array_t disks;
  gavl_array_t filesystems;
 
  } removables_t;

static int removables_init(removables_t * r, DBusConnection * conn)
  {
  dbus_bus_add_match(conn, "sender='org.freedesktop.UDisks2'", NULL);

  memset(r, 0, sizeof(*r));
  if(!(r->udisks_name = get_name_owner(conn, "org.freedesktop.UDisks2")))
    return 0;
  return 1;
  }

static char * iter_get_string(DBusMessageIter * iter)
  {
  DBusBasicValue value;
  int arg_type = dbus_message_iter_get_arg_type(iter);
  switch(arg_type)
    {
    case DBUS_TYPE_STRING:
    case DBUS_TYPE_OBJECT_PATH:
      dbus_message_iter_get_basic(iter, &value);
      return gavl_strdup(value.str);
      break;
    }
  return NULL;
  }

static int iter_get_dictionary(DBusMessageIter * iter, gavl_dictionary_t * ret)
  {
  int i, num;
  char * name;
  DBusMessageIter sub_iter;    // Elements
  DBusMessageIter subsub_iter; // Element
  DBusMessageIter subsubsub_iter; // Variant
  DBusBasicValue value;

  if((dbus_message_iter_get_arg_type(iter) != DBUS_TYPE_ARRAY) ||
     (dbus_message_iter_get_element_type(iter) != DBUS_TYPE_DICT_ENTRY))
    return 0;

  dbus_message_iter_recurse(iter, &sub_iter);
  num = dbus_message_iter_get_element_count(iter);

  for(i = 0; i < num; i++)
    {
    dbus_message_iter_recurse(&sub_iter, &subsub_iter);

    /* Name */
    name = iter_get_string(&subsub_iter);

    dbus_message_iter_next(&subsub_iter);
      
    dbus_message_iter_recurse(&subsub_iter, &subsubsub_iter);

    switch(dbus_message_iter_get_arg_type(&subsubsub_iter))
      {
      case DBUS_TYPE_STRING:
      case DBUS_TYPE_OBJECT_PATH:
        {
        char * val_str = iter_get_string(&subsubsub_iter);
        if(val_str && (*val_str != '\0'))
          gavl_dictionary_set_string_nocopy(ret, name, val_str);
        else if(val_str)
          free(val_str);
        }
        break;
      case DBUS_TYPE_BYTE:
        dbus_message_iter_get_basic(&subsubsub_iter, &value);
        gavl_dictionary_set_int(ret, name, value.byt);
        break;
      case DBUS_TYPE_BOOLEAN:
        dbus_message_iter_get_basic(&subsubsub_iter, &value);
        gavl_dictionary_set_int(ret, name, value.bool_val);
        break;
      case DBUS_TYPE_INT16:
        dbus_message_iter_get_basic(&subsubsub_iter, &value);
        gavl_dictionary_set_int(ret, name, value.i16);
        break;
      case DBUS_TYPE_UINT16:
        dbus_message_iter_get_basic(&subsubsub_iter, &value);
        gavl_dictionary_set_int(ret, name, value.u16);
        break;
      case DBUS_TYPE_INT32:
        dbus_message_iter_get_basic(&subsubsub_iter, &value);
        gavl_dictionary_set_int(ret, name, value.i32);
        break;
      case DBUS_TYPE_UINT32:
        dbus_message_iter_get_basic(&subsubsub_iter, &value);
        gavl_dictionary_set_int(ret, name, value.u32);
        break;
      case DBUS_TYPE_INT64:
        dbus_message_iter_get_basic(&subsubsub_iter, &value);
        gavl_dictionary_set_long(ret, name, value.i64);
        break;
      case DBUS_TYPE_UINT64:
        dbus_message_iter_get_basic(&subsubsub_iter, &value);
        gavl_dictionary_set_long(ret, name, value.u64);
        break;
      default:
        break;
      }

    /* Next element */
    dbus_message_iter_next(&sub_iter);

    free(name);
    }  


  return 1;
  }

static void handle_add_interface(removables_t * r, DBusMessage * msg)
  {
  char * path;
  char * interface;
  DBusMessageIter iter;
  DBusMessageIter sub_iter;
  DBusMessageIter subsub_iter;
  gavl_dictionary_t properties;


  fprintf(stderr, "** Interface added\n");

  dbus_message_iter_init(msg, &iter);
  
  if(!(path = iter_get_string(&iter)))
    return;

  fprintf(stderr, "  path: %s\n", path);

  if(!dbus_message_iter_next(&iter) ||
     (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_ARRAY))
    return;

  dbus_message_iter_recurse(&iter, &sub_iter);  

  while(1) // Loop over added interfaces
    {
    if(dbus_message_iter_get_arg_type(&sub_iter) != DBUS_TYPE_DICT_ENTRY)
      return;

    dbus_message_iter_recurse(&sub_iter, &subsub_iter);      
      
    interface = iter_get_string(&subsub_iter);
    
    fprintf(stderr, "  interface: %s\n", interface);

    if(!dbus_message_iter_next(&subsub_iter))
      break;
  
    gavl_dictionary_init(&properties);
    if(!iter_get_dictionary(&subsub_iter, &properties))
      break;

    fprintf(stderr, "  properties\n");
    gavl_dictionary_dump(&properties, 4);
    fprintf(stderr, "\n");
    gavl_dictionary_free(&properties);
   
    free(interface);

    if(!dbus_message_iter_next(&sub_iter))
      break;
    }

  free(path);

  }

static void handle_remove_interface(removables_t * r, DBusMessage * msg)
  {
  fprintf(stderr, "** Interface removed\n");
  }

static void handle_properties_changed(removables_t * r, DBusMessage * msg)
  {
  fprintf(stderr, "** Properties changed\n");
  }

static void handle_job_completed(removables_t * r, DBusMessage * msg)
  {
  fprintf(stderr, "** Job completed\n");
  }


static void handle_message(removables_t * r, DBusMessage * msg)
  { 
  const char * sender    = NULL; 
  const char * interface = NULL;
  const char * member    = NULL;
  const char * path      = NULL;
  int type; 
  
  if(!(sender = dbus_message_get_sender(msg)) ||
     strcmp(sender, r->udisks_name))
    return;

  type      = dbus_message_get_type(msg);
  interface = dbus_message_get_interface(msg);  
  member    = dbus_message_get_member(msg);

  if(type != DBUS_MESSAGE_TYPE_SIGNAL) // We only handle signals here
    return;

  if(interface && member)
    {
    if(!strcmp(interface, "org.freedesktop.DBus.ObjectManager"))
      {
      if(!strcmp(member, "InterfacesAdded"))
        {
        handle_add_interface(r, msg);      
        return;
        }
      else if(!strcmp(member, "InterfacesRemoved"))
        {
        handle_remove_interface(r, msg);
        return;
        }
      }
    else if(!strcmp(interface, "org.freedesktop.DBus.Properties"))
      {
      if(!strcmp(member, "PropertiesChanged"))
        {
        handle_properties_changed(r, msg);
//        return;
        }
      }
    else if(!strcmp(interface, "org.freedesktop.UDisks2.Job"))
      {
      if(!strcmp(member, "Completed"))
        {
        handle_job_completed(r, msg);
        return;
        }
      }

    }

  fprintf(stderr, "Got message\n");
  dump_message(msg);

  }

int main(int argc, char ** argv)
  {
  DBusConnection * conn;
  removables_t r;
  DBusError err;
  DBusMessage * msg;

  dbus_error_init(&err);

  /* Create a privately managed connection (i.e. not reference counted by libdbus */
//  conn = dbus_bus_get_private(DBUS_BUS_SESSION, &err);
  conn = dbus_bus_get_private(DBUS_BUS_SYSTEM, &err);

  if(dbus_error_is_set(&err))
    {
    fprintf(stderr, "Connection Error: %s", err.message);
    return EXIT_FAILURE;
    }

  if(!removables_init(&r, conn))
    return EXIT_FAILURE;

//  dbus_bus_add_match(conn, "interface='org.gtk.Private.RemoteVolumeMonitor'", NULL);

  while(1)
    {
    dbus_connection_read_write(conn, 0);
    if(!(msg = dbus_connection_pop_message(conn)))
      {
      usleep(1000 * 20);            
      continue;
      }
    handle_message(&r, msg);
    }

  return EXIT_SUCCESS;

  }

