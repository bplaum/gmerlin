#include <string.h>
#include <glob.h>


#include <config.h>
#include <gmerlin/translation.h>

#include <gmerlin/plugin.h>
#include <gmerlin/bgdbus.h>

#include <gavl/log.h>
#define LOG_DOMAIN "udisks2"

#define MSG_VOLUME_GENERIC 1 // Sent from the dbus to this plugin

typedef struct
  {
  bg_controllable_t ctrl;

  bg_dbus_connection_t * conn;
  bg_msg_sink_t * dbus_sink;

  char * daemon_addr;
  
  /* DBUS */
  gavl_dictionary_t drives;
  gavl_dictionary_t block_devices;

  int initialized;
  } udisks2_t;

static int handle_msg(void * priv, gavl_msg_t * msg)
  {
  return 1;
  }

static void add_volume(udisks2_t * u,
                       const char * id,
                       const char * label,
                       const char * location,
                       const char * media_class)
  {
  gavl_msg_t * msg;
  gavl_value_t val;
  gavl_dictionary_t * dict;

  //  fprintf(stderr, "add_volume %s %s %s %s\n", id, label, location, media_class);
  
  gavl_value_init(&val); 
  dict = gavl_value_set_dictionary(&val);

  gavl_dictionary_set_string(dict, GAVL_META_LABEL, label);
  gavl_dictionary_set_string(dict, GAVL_META_URI,   location);
  gavl_dictionary_set_string(dict, GAVL_META_MEDIA_CLASS, media_class);
  
  msg = bg_msg_sink_get(u->ctrl.evt_sink);

  gavl_msg_set_id_ns(msg, GAVL_MSG_RESOURCE_ADDED, GAVL_MSG_NS_GENERIC);
  gavl_dictionary_set_string(&msg->header, GAVL_MSG_CONTEXT_ID, id);
  gavl_msg_set_arg_dictionary_nocopy(msg, 0, dict);
  
  bg_msg_sink_put(u->ctrl.evt_sink);
  
  }

static void remove_volume(udisks2_t * u, const char * id)
  {
  gavl_msg_t * msg;

  //  fprintf(stderr, "Remove volume %s\n", id);
  
  msg = bg_msg_sink_get(u->ctrl.evt_sink);
  gavl_msg_set_id_ns(msg, GAVL_MSG_RESOURCE_DELETED, GAVL_MSG_NS_GENERIC);
  gavl_dictionary_set_string(&msg->header, GAVL_MSG_CONTEXT_ID, id);
  bg_msg_sink_put(u->ctrl.evt_sink);
  
  }

static int is_block_device(const char * name)
  {
  return gavl_string_starts_with(name, "/org/freedesktop/UDisks2/block_devices");
  }

static int is_drive(const char * name)
  {
  return gavl_string_starts_with(name, "/org/freedesktop/UDisks2/drive");
  }

/* Extract values from dbus formatted dictionaries */

static const gavl_value_t * get_property(const gavl_dictionary_t * obj, const char * iface,
                                         const char * name)
  {
  const gavl_dictionary_t * if1;

  if((if1 = gavl_dictionary_get_dictionary(obj, iface)))
    return gavl_dictionary_get(if1, name); 
  else
    return NULL;
  }

static const char * get_string_property(const gavl_dictionary_t * obj, const char * iface,
                                        const char * name)
  {
  const gavl_dictionary_t * if1;

  if((if1 = gavl_dictionary_get_dictionary(obj, iface)))
    return gavl_dictionary_get_string(if1, name); 
  else
    return NULL;
  }

static int get_int_property(const gavl_dictionary_t * obj, const char * iface,
                            const char * name, int * ret)
  {
  const gavl_dictionary_t * if1;

  if((if1 = gavl_dictionary_get_dictionary(obj, iface)))
    return gavl_dictionary_get_int(if1, name, ret); 
  else
    return 0;
  }

static const char * get_drive_location(udisks2_t * u, const char * name)
  {
  int i;
  const gavl_dictionary_t * dev;
  const char * drive;
  /* Need to find the block device with that drive */
  
  for(i = 0; i < u->block_devices.num_entries; i++)
    {
    if((dev = gavl_value_get_dictionary(&u->block_devices.entries[i].v)) &&
       (drive = get_string_property(dev, "org.freedesktop.UDisks2.Block",
                                    "Drive")) &&
       !strcmp(drive, name))
      {
      return get_string_property(dev, "org.freedesktop.UDisks2.Block", "Device");
      }
    }
  return NULL;
  }

static const char * glob_has_file(const glob_t * g, const char * name)
  {
  int i;
  const char * pos;

  for(i = 0; i < g->gl_pathc; i++)
    {
    if(!(pos = strrchr(g->gl_pathv[i], '/')))
      continue;
    pos++;

    if(!strcasecmp(pos, name))
      return g->gl_pathv[i];
    }
  return NULL;
  }

static void blockdevice_cb(void * priv, const char * name, const gavl_value_t * val)
  {
  const char * var_s;
  const char * location;
  const char * label;
  const char * media;
  const char * filesystem;
  int done = 0;
  char * location_priv;
  
  int var_i = 0;
  const gavl_value_t * var;

  const gavl_array_t * mountpoints;
  
  const gavl_dictionary_t * dict_dev;
  const gavl_dictionary_t * dict_drive;
  udisks2_t * u = priv;

  if(!(dict_dev = gavl_value_get_dictionary(val)) ||
     !(var_s = get_string_property(dict_dev, "org.freedesktop.UDisks2.Block", "Drive")) ||
     !strcmp(var_s, "/") ||
     !(dict_drive = gavl_dictionary_get_dictionary(&u->drives, var_s)) ||
     !get_int_property(dict_drive, "org.freedesktop.UDisks2.Drive",
                       "MediaRemovable", &var_i) ||
     !var_i ||
     !get_int_property(dict_drive, "org.freedesktop.UDisks2.Drive",
                       "MediaAvailable", &var_i) ||
     !var_i)
    goto fail; 
  
  /* Try to get the mount point */
  
  if(!(filesystem = get_string_property(dict_dev, "org.freedesktop.UDisks2.Block", "IdType")) ||
     !(var = get_property(dict_dev, "org.freedesktop.UDisks2.Filesystem",  "MountPoints")) ||
     !(mountpoints = gavl_value_get_array(var)) ||
     !(mountpoints->num_entries > 0) ||
     !(location = gavl_value_get_string(&mountpoints->entries[0])))
    {
    goto fail; 
    }
  media = get_string_property(dict_drive, "org.freedesktop.UDisks2.Drive", "Media");
  
  // fprintf(stderr, "Media: %s, location: %s, filesystem: %s\n", media, location, filesystem);
  
  /* Detect VCD */
  
  if(media && gavl_string_starts_with(media, "optical_cd") && !strcmp(filesystem, "iso9660"))
    {
    char * pattern;
    glob_t g;

    memset(&g, 0, sizeof(g));

    pattern = gavl_sprintf("%s/*", location);

    glob(pattern, 0, NULL, &g);

    if(glob_has_file(&g, "mpegav") &&
       glob_has_file(&g, "vcd"))
      {
      label = get_string_property(dict_dev, "org.freedesktop.UDisks2.Block", "IdLabel");

      location_priv = gavl_sprintf("vcd://%s", get_string_property(dict_dev, "org.freedesktop.UDisks2.Block", "Device"));
      gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Detected VCD");
      //      fprintf(stderr, "Detected VCD\n");

      add_volume(u, name, label, location_priv, GAVL_META_MEDIA_CLASS_ROOT_REMOVABLE_VCD);
      done = 1;

      free(location_priv);
      }
    else if(glob_has_file(&g, "mpeg2") &&
            glob_has_file(&g, "svcd"))
      {
      label = get_string_property(dict_dev, "org.freedesktop.UDisks2.Block", "IdLabel");
      
      location_priv = gavl_sprintf("vcd://%s", get_string_property(dict_dev, "org.freedesktop.UDisks2.Block", "Device"));
      gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Detected SVCD: %s", label);
      //      fprintf(stderr, "Detected SVCD: %s\n", label);

      add_volume(u, name, label, location_priv, GAVL_META_MEDIA_CLASS_ROOT_REMOVABLE_SVCD);
      
      done = 1;
      
      free(location_priv);
      }
    else
      {
      if((label = strrchr(location, '/')))
        label++;
      
      add_volume(u, name, label, location, GAVL_META_MEDIA_CLASS_ROOT_REMOVABLE_FILESYSTEM_CD);
      }
    
    globfree(&g);
    
    free(pattern);
    }
  
  /* Detect DVD */

  else if(media && gavl_string_starts_with(media, "optical_dvd") && !strcmp(filesystem, "udf"))
    {
    char * pattern;
    glob_t g;

    memset(&g, 0, sizeof(g));

    pattern = gavl_sprintf("%s/*", location);

    glob(pattern, 0, NULL, &g);

    if(glob_has_file(&g, "video_ts"))
      {
      label = get_string_property(dict_dev, "org.freedesktop.UDisks2.Block", "IdLabel");
      location_priv = gavl_sprintf("dvd://%s", get_string_property(dict_dev, "org.freedesktop.UDisks2.Block", "Device"));
      
      gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Detected video DVD: %s", label);
      //      fprintf(stderr, "Detected video DVD %s\n", label);

      add_volume(u, name, label, location_priv, GAVL_META_MEDIA_CLASS_ROOT_REMOVABLE_VIDEODVD);
      done = 1;
      
      free(location_priv);
      }
    else // Data DVD
      {
      if((label = strrchr(location, '/')))
        label++;
      
      add_volume(u, name, label, location, GAVL_META_MEDIA_CLASS_ROOT_REMOVABLE_FILESYSTEM_DVD);
      }
   
    globfree(&g);
    
    free(pattern);
    }

  
  /* Get label */

  if(!done)
    {
    if((label = strrchr(location, '/')))
      label++;
    else
      label = get_string_property(dict_drive, "org.freedesktop.UDisks2.Drive", "Model");
    
    /*
      TODO: Detect:

      GAVL_META_MEDIA_CLASS_ROOT_REMOVABLE_FILESYSTEM_HDD
      GAVL_META_MEDIA_CLASS_ROOT_REMOVABLE_FILESYSTEM_PENDRIVE
      GAVL_META_MEDIA_CLASS_ROOT_REMOVABLE_FILESYSTEM_MEMORYCARD
      GAVL_META_MEDIA_CLASS_ROOT_REMOVABLE_FILESYSTEM_MOBILE
      GAVL_META_MEDIA_CLASS_ROOT_REMOVABLE_FILESYSTEM_CD
      GAVL_META_MEDIA_CLASS_ROOT_REMOVABLE_FILESYSTEM_DVD
    */

#if 0    
    fprintf(stderr, "Media:      %s\n", media);
    fprintf(stderr, "Label:      %s\n", label);
    fprintf(stderr, "Filesystem: %s\n", filesystem);
    fprintf(stderr, "Name:       %s\n", name);
#endif

    /* Maybe find a smarter check if a filesystem is removable */
    if(gavl_string_starts_with(location, "/media"))
      add_volume(u, name, label, location, GAVL_META_MEDIA_CLASS_ROOT_REMOVABLE_FILESYSTEM);
    }

  return;
  
  fail:
  
  /* No usable volume found -> try to remove from volumes */

  remove_volume(u, name);
  
  }

static void drive_cb(void * priv, const char * name, const gavl_value_t * val)
  {
  int var_i = 0;
  const char * var_s;
  const char * location;
  const gavl_dictionary_t * dict_drive;

  udisks2_t * u = priv;

  //  fprintf(stderr, "Drive CB 1\n");

  if(!(dict_drive = gavl_value_get_dictionary(val)))
    return;
  
  if(!get_int_property(dict_drive, "org.freedesktop.UDisks2.Drive",
                       "MediaAvailable", &var_i))
    return;

  if(!var_i)
    {
    remove_volume(u, name);
    return;
    }
  
  if(!get_int_property(dict_drive, "org.freedesktop.UDisks2.Drive",
                       "MediaRemovable", &var_i) ||
     !var_i ||
     !get_int_property(dict_drive, "org.freedesktop.UDisks2.Drive",
                       "Optical", &var_i) ||
     !var_i ||
     !(var_s = get_string_property(dict_drive, "org.freedesktop.UDisks2.Drive",
                                   "Media")) ||
     !(location = get_drive_location(u, name)))
    return;
  
  //  fprintf(stderr, "Drive CB 2\n");

  //  fprintf(stderr, "Detected audio CD at %s\n", get_drive_location(v, name));
  
  if(gavl_string_starts_with(var_s, "optical_cd"))
    {
      
    if(get_int_property(dict_drive, "org.freedesktop.UDisks2.Drive",
                        "OpticalNumAudioTracks", &var_i) &&
       (var_i > 0))
      {
      /* Audio CD */
      char * uri;
      uri = gavl_sprintf("cda://%s", location);
      add_volume(u, name, "Audio CD", uri, GAVL_META_MEDIA_CLASS_ROOT_REMOVABLE_AUDIOCD);
      free(uri);
      }
    }
  }

static int handle_dbus_msg_volume(void * data, gavl_msg_t * msg)
  {
  udisks2_t * u = data;
  
  if((msg->NS == BG_MSG_NS_PRIVATE) &&
     (msg->ID == MSG_VOLUME_GENERIC))
    {
    const char * interface;
    const char * member;
    const char * path;

    interface = gavl_dictionary_get_string(&msg->header, BG_DBUS_META_INTERFACE);
    member    = gavl_dictionary_get_string(&msg->header, BG_DBUS_META_MEMBER);
    path      = gavl_dictionary_get_string(&msg->header, BG_DBUS_META_PATH);
    
    //    fprintf(stderr, "Got dbus message: %s.%s\n  Path: %s\n", interface, member, path);
    //    gavl_msg_dump(msg, 2);
    
    if(!interface || !member || !path)
      return 1;
    if(!strcmp(interface, "org.freedesktop.DBus.Properties"))
      {
      /* PropertiesChanged */
      if(!strcmp(member, "PropertiesChanged"))
        {
        gavl_dictionary_t * dst_dict;
        gavl_dictionary_t prop_dict;
        const char * prop_if;

        gavl_dictionary_init(&prop_dict);
        
        if(!(prop_if = gavl_msg_get_arg_string_c(msg, 0)) ||
           !(gavl_msg_get_arg_dictionary(msg, 1, &prop_dict)))
          {
          return 1;
          }

        if(!strcmp(prop_if, "org.freedesktop.UDisks2.Drive.Ata"))
          return 1;
        
        //        fprintf(stderr, "PropertiesChanged: %s %s\n", path, prop_if);
        //        gavl_dictionary_dump(&prop_dict, 2);
        //        fprintf(stderr, "\n");
        
        /* Store locally */

        if(is_drive(path))
          dst_dict = gavl_dictionary_get_dictionary_create(&u->drives, path);
        else if(is_block_device(path))
          dst_dict = gavl_dictionary_get_dictionary_create(&u->block_devices, path);
        else
          {
          gavl_dictionary_free(&prop_dict);
          return 1;
          }
        
        dst_dict = gavl_dictionary_get_dictionary_nc(dst_dict, prop_if);
        
        gavl_dictionary_update_fields(dst_dict, &prop_dict);

        if(is_drive(path))
          drive_cb(u, path, gavl_dictionary_get(&u->drives, path));
        else if(is_block_device(path))
          blockdevice_cb(u, path, gavl_dictionary_get(&u->block_devices, path));
        
        gavl_dictionary_free(&prop_dict);
        }
      }
    else if(!strcmp(interface, "org.freedesktop.DBus.ObjectManager"))
      {
      const char * obj;
      
      /* InterfacesAdded */
      if(!strcmp(member, "InterfacesAdded"))
        {
        gavl_dictionary_t * dict;
        
        gavl_dictionary_t if_dict;
        gavl_dictionary_init(&if_dict);

        
        obj = gavl_msg_get_arg_string_c(msg, 0);

        if(is_drive(obj))
          dict = gavl_dictionary_get_dictionary_create(&u->drives, obj);
        else if(is_block_device(obj))
          dict = gavl_dictionary_get_dictionary_create(&u->block_devices, obj);
        else
          return 1;
        
        gavl_msg_get_arg_dictionary(msg, 1, &if_dict);

        //        fprintf(stderr, "InterfacesAdded: %s %s\n", path, obj);
        //        gavl_dictionary_dump(&if_dict, 2);
        
        gavl_dictionary_update_fields(dict, &if_dict);
        gavl_dictionary_free(&if_dict);

        if(is_drive(obj))
          drive_cb(u, obj, gavl_dictionary_get(&u->drives, obj));
        else if(is_block_device(obj))
          blockdevice_cb(u, obj, gavl_dictionary_get(&u->block_devices, obj));
        }
      /* InterfacesRemoved */
      else if(!strcmp(member, "InterfacesRemoved"))
        {
        gavl_dictionary_t * dict;
        const char * iface;
        int i;
        gavl_array_t ifaces;
        obj = gavl_msg_get_arg_string_c(msg, 0);

        gavl_array_init(&ifaces);

        //        fprintf(stderr, "InterfacesRemoved: %s %s\n", path, obj);
        
        if((gavl_msg_get_arg_array(msg, 1, &ifaces)))
          {
          if(is_drive(obj))
            {
            dict = gavl_dictionary_get_dictionary_nc(&u->drives, obj);
            }
          else if(is_block_device(obj))
            {
            dict = gavl_dictionary_get_dictionary_nc(&u->block_devices, obj);
            }
          else
            dict = NULL;

          if(dict)
            {
            //            fprintf(stderr, "Interfaces removed 1\n");
            //            gavl_dictionary_dump(dict, 2);
            
            for(i = 0; i < ifaces.num_entries; i++)
              {
              if((iface = gavl_string_array_get(&ifaces, i)))
                {
                gavl_dictionary_set(dict, iface, NULL);
                //                fprintf(stderr, "iface: %s\n", iface);
                }
              }
              
            //            fprintf(stderr, "Interfaces removed 2\n");
            //            gavl_dictionary_dump(dict, 2);
            
            if(!dict->num_entries)
              {
              /* Remove object */
              if(is_drive(path))
                gavl_dictionary_set(&u->drives, obj, NULL);
              else if(is_block_device(path))
                gavl_dictionary_set(&u->block_devices, obj, NULL);
              remove_volume(u, obj);
              }
            }
          }
        gavl_array_free(&ifaces);
        }
      }
    
    //  gavl_msg_dump(msg, 2);
    }
  return 1;
  }

static void get_managed_objects_foreach_func(void * priv, const char * name, const gavl_value_t * val)
  {
  const gavl_dictionary_t * dict_src;
  gavl_dictionary_t * dict_dst;
  udisks2_t * u = priv;
  
  if((dict_src = gavl_value_get_dictionary(val)))
    {
    gavl_value_t val;
    gavl_value_init(&val);
    dict_dst = gavl_value_set_dictionary(&val);
    gavl_dictionary_copy(dict_dst, dict_src);

    if(is_block_device(name))
      {
      gavl_dictionary_set_nocopy(&u->block_devices, name, &val);
      //      fprintf(stderr, "Got block device: %s\n", name);
      }
    else if(is_drive(name))
      {
      gavl_dictionary_set_nocopy(&u->drives, name, &val);
      //      fprintf(stderr, "Got drive: %s\n", name);
      }
    else /* Should never happen */
      gavl_value_free(&val);
    }
  
  }


static void * create_udisks2()
  {
  
  udisks2_t * u = calloc(1, sizeof(*u));
  
  bg_controllable_init(&u->ctrl,
                       bg_msg_sink_create(handle_msg, u, 1),
                       bg_msg_hub_create(1));

  u->conn = bg_dbus_connection_get(DBUS_BUS_SYSTEM);
  
  if(!(u->daemon_addr = bg_dbus_get_name_owner(u->conn, "org.freedesktop.UDisks2")))
    return u;

  u->dbus_sink = bg_msg_sink_create(handle_dbus_msg_volume, u, 0);
  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Found UDisks2 daemon at %s", u->daemon_addr);
  
  return u;
  }

static void destroy_udisks2(void * priv)
  {
  udisks2_t * u = priv;
  
  bg_controllable_cleanup(&u->ctrl);
  if(u->daemon_addr)
    free(u->daemon_addr);
  if(u->dbus_sink)
    bg_msg_sink_destroy(u->dbus_sink);

  gavl_dictionary_free(&u->drives);
  gavl_dictionary_free(&u->block_devices);
  
  free(u);
  }

static int update_udisks2(void * priv)
  {
  udisks2_t * u = priv;
  char * rule;
  gavl_msg_t * res;
  DBusMessage * req;
  gavl_dictionary_t dict;
  int ret = 0;

  if(!u->initialized)
    {
    
    //  fprintf(stderr, "Update udisks\n");
  
    rule = gavl_sprintf("sender='%s',type='signal'", u->daemon_addr);
    bg_dbus_connection_add_listener(u->conn,
                                    rule,
                                    u->dbus_sink, BG_MSG_NS_PRIVATE, MSG_VOLUME_GENERIC);
    free(rule);
  
    req = dbus_message_new_method_call(u->daemon_addr,
                                       "/org/freedesktop/UDisks2",
                                       "org.freedesktop.DBus.ObjectManager",
                                       "GetManagedObjects");
  
    res = bg_dbus_connection_call_method(u->conn, req);
  
    dbus_message_unref(req);

    gavl_dictionary_init(&dict);
  
    if(res && gavl_msg_get_arg_dictionary(res, 0, &dict))
      {
      //    fprintf(stderr, "Managed Objects:");
      //    gavl_dictionary_dump(&dict, 2);
    
      gavl_dictionary_foreach(&dict, get_managed_objects_foreach_func, u);
      gavl_dictionary_free(&dict);
      gavl_msg_destroy(res);
    
      gavl_dictionary_foreach(&u->block_devices, blockdevice_cb, u);
    
      gavl_dictionary_foreach(&u->drives, drive_cb, u);
      }
    u->initialized = 1;
    ret++;
    }

  bg_msg_sink_iteration(u->dbus_sink);
  ret += bg_msg_sink_get_num(u->dbus_sink);
  
  return ret;
  }

static bg_controllable_t * get_controllable_udisks2(void * priv)
  {
  udisks2_t * u = priv;
  return &u->ctrl;
  }

bg_controllable_plugin_t the_plugin =
  {
    .common =
    {
      BG_LOCALE,
      .name =      "res_udisks2",
      .long_name = TRS("UDisks2 monitor"),
      .description = TRS("Detects removable media"),
      .type =     BG_PLUGIN_RESOURCE_DETECTOR,
      .flags =    0,
      .create =   create_udisks2,
      .destroy =   destroy_udisks2,
      .get_controllable =   get_controllable_udisks2,
      .priority =         1,
    },
    .update = update_udisks2,

  };

/* Include this into all plugin modules exactly once
   to let the plugin loader obtain the API version */
BG_GET_PLUGIN_API_VERSION;
