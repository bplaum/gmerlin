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

#include <config.h>
#include <string.h>
#include <glob.h>

#include <gmerlin/volumemanager.h>
#include <gmerlin/utils.h>

#include <gmerlin/translation.h>
#include <gmerlin/log.h>
#include <gmerlin/state.h>

#define LOG_DOMAIN "volumemanager"

#ifdef HAVE_DBUS

#include <gavl/metatags.h>
#include <gmerlin/bgdbus.h>

#define MSG_VOLUME_GENERIC 1

/*
 *  Removable media is handled via the UDisks2 dbus API.
 *
 *  We distibguish between optical and non-optical drives.
 *  For Optical drives, we get the necessary info from the org.freedesktop.UDisks2.Drive interface.
 *  For Filesystems, we use the org.freedesktop.UDisks2.Filesystem
 */

struct bg_volume_manager_s
  {
  bg_dbus_connection_t * conn;
  bg_msg_hub_t * hub;
  bg_msg_sink_t * evt_sink;
  
  bg_msg_sink_t * dbus_sink;

  /* DBUS */
  gavl_dictionary_t drives;
  gavl_dictionary_t block_devices;
  
  /* gavl */
  gavl_dictionary_t volumes;
  
  char * daemon_addr;
  
  };

static void update_state(bg_volume_manager_t * vol)
  {
  gavl_value_t val;
  gavl_msg_t * msg = bg_msg_sink_get(vol->evt_sink);

  gavl_value_init(&val);
  gavl_dictionary_copy(gavl_value_set_dictionary(&val), &vol->volumes);
  
  gavl_msg_set_state_nocopy(msg,
                          BG_MSG_STATE_CHANGED,
                          1,
                          "volumemanager",
                          "volumes",
                          &val);
  
  bg_msg_sink_put(vol->evt_sink);
  }

static void add_volume(bg_volume_manager_t * vol,
                       const char * id,
                       const char * label,
                       const char * location,
                       const char * media_class)
  {
  gavl_msg_t * msg;
  gavl_value_t val;
  gavl_dictionary_t * dict;

  /* Check if we have this already */
  if(gavl_dictionary_get_dictionary_nc(&vol->volumes, id))
    return;

  gavl_value_init(&val); 
  dict = gavl_value_set_dictionary(&val);

  gavl_dictionary_set_string(dict, GAVL_META_LABEL, label);
  gavl_dictionary_set_string(dict, GAVL_META_URI,   location);

  gavl_dictionary_set_string(dict, GAVL_META_MEDIA_CLASS, media_class);
  
  //  fprintf(stderr, "Add Volume: %s\n", id);
  //  gavl_dictionary_dump(dict, 2);
  //  fprintf(stderr, "\n");
  
  msg = bg_msg_sink_get(vol->evt_sink);

  gavl_msg_set_id_ns(msg, BG_MSG_ID_VOLUME_ADDED, BG_MSG_NS_VOLUMEMANAGER);
  gavl_msg_set_arg_string(msg, 0, id);
  gavl_msg_set_arg_dictionary(msg, 1, dict);
  
  bg_msg_sink_put(vol->evt_sink);
  
  gavl_dictionary_set_nocopy(&vol->volumes, id, &val);
  update_state(vol);
  }

static void remove_volume(bg_volume_manager_t * vol, const char * id)
  {
  gavl_msg_t * msg;
  gavl_dictionary_t * d;
  
  if(!(d = gavl_dictionary_get_dictionary_nc(&vol->volumes, id)))
    return;
  
  msg = bg_msg_sink_get(vol->evt_sink);
  gavl_msg_set_id_ns(msg, BG_MSG_ID_VOLUME_REMOVED, BG_MSG_NS_VOLUMEMANAGER);
  gavl_msg_set_arg_string(msg, 0, id);
  bg_msg_sink_put(vol->evt_sink);
  
  gavl_dictionary_set(&vol->volumes, id, NULL);
  update_state(vol);
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

static const char * get_drive_location(bg_volume_manager_t * v, const char * name)
  {
  int i;
  const gavl_dictionary_t * dev;
  const char * drive;
  /* Need to find the block device with that drive */
  
  for(i = 0; i < v->block_devices.num_entries; i++)
    {
    if((dev = gavl_value_get_dictionary(&v->block_devices.entries[i].v)) &&
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
  bg_volume_manager_t * v = priv;

  if(!(dict_dev = gavl_value_get_dictionary(val)) ||
     !(var_s = get_string_property(dict_dev, "org.freedesktop.UDisks2.Block", "Drive")) ||
     !strcmp(var_s, "/") ||
     !(dict_drive = gavl_dictionary_get_dictionary(&v->drives, var_s)) ||
     !get_int_property(dict_drive, "org.freedesktop.UDisks2.Drive",
                       "MediaRemovable", &var_i) ||
     !var_i ||
     !get_int_property(dict_drive, "org.freedesktop.UDisks2.Drive",
                       "MediaAvailable", &var_i) ||
     !var_i)
    goto fail; 

  //  fprintf(stderr, "Got removable block device %s\n", name);
  //  fprintf(stderr, "  Drive %s\n", var_s);
  
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
  
  /* Detect VCD */

  //  fprintf(stderr, "Media: %s, location: %s, filesystem: %s\n", media, location, filesystem);
  
  if(media && gavl_string_starts_with(media, "optical_cd") && !strcmp(filesystem, "iso9660"))
    {
    char * pattern;
    glob_t g;

    memset(&g, 0, sizeof(g));

    pattern = bg_sprintf("%s/*", location);

    glob(pattern, 0, NULL, &g);

    if(glob_has_file(&g, "mpegav") &&
       glob_has_file(&g, "vcd"))
      {
      label = get_string_property(dict_dev, "org.freedesktop.UDisks2.Block", "IdLabel");

      location_priv = bg_sprintf("vcd://%s", get_string_property(dict_dev, "org.freedesktop.UDisks2.Block", "Device"));
      gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Detected VCD");
      //      fprintf(stderr, "Detected VCD\n");

      add_volume(v, name, label, location_priv, GAVL_META_MEDIA_CLASS_ROOT_REMOVABLE_VCD);
      done = 1;

      free(location_priv);
      }
    else if(glob_has_file(&g, "mpeg2") &&
            glob_has_file(&g, "svcd"))
      {
      label = get_string_property(dict_dev, "org.freedesktop.UDisks2.Block", "IdLabel");
      
      location_priv = bg_sprintf("vcd://%s", get_string_property(dict_dev, "org.freedesktop.UDisks2.Block", "Device"));
      gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Detected SVCD: %s", label);
      //      fprintf(stderr, "Detected SVCD: %s\n", label);

      add_volume(v, name, label, location_priv, GAVL_META_MEDIA_CLASS_ROOT_REMOVABLE_SVCD);
      
      done = 1;
      
      free(location_priv);
      }
    else
      {
      if((label = strrchr(location, '/')))
        label++;
      
      add_volume(v, name, label, location, GAVL_META_MEDIA_CLASS_ROOT_REMOVABLE_FILESYSTEM_CD);
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

    pattern = bg_sprintf("%s/*", location);

    glob(pattern, 0, NULL, &g);

    if(glob_has_file(&g, "video_ts"))
      {
      label = get_string_property(dict_dev, "org.freedesktop.UDisks2.Block", "IdLabel");
      location_priv = bg_sprintf("dvd://%s", get_string_property(dict_dev, "org.freedesktop.UDisks2.Block", "Device"));
      
      gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Detected video DVD: %s", label);
      //      fprintf(stderr, "Detected video DVD %s\n", label);

      add_volume(v, name, label, location_priv, GAVL_META_MEDIA_CLASS_ROOT_REMOVABLE_VIDEODVD);
      done = 1;
      
      free(location_priv);
      }
    else // Data DVD
      {
      if((label = strrchr(location, '/')))
        label++;
      
      add_volume(v, name, label, location, GAVL_META_MEDIA_CLASS_ROOT_REMOVABLE_FILESYSTEM_DVD);
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
    add_volume(v, name, label, location, GAVL_META_MEDIA_CLASS_ROOT_REMOVABLE_FILESYSTEM);
    }

  return;
  
  fail:
  
  /* No usable volume found -> try to remove from volumes */

  remove_volume(v, name);
  
  }

static void drive_cb(void * priv, const char * name, const gavl_value_t * val)
  {
  int var_i = 0;
  const char * var_s;
  const char * location;
  const gavl_dictionary_t * dict_drive;

  bg_volume_manager_t * v = priv;

  //  fprintf(stderr, "Drive CB 1\n");

  if(!(dict_drive = gavl_value_get_dictionary(val)))
    return;
  
  if(!get_int_property(dict_drive, "org.freedesktop.UDisks2.Drive",
                       "MediaAvailable", &var_i))
    return;

  if(!var_i)
    {
    remove_volume(v, name);
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
     !(location = get_drive_location(v, name)))
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
      uri = bg_sprintf("cda://%s", location);
      add_volume(v, name, "Audio CD", uri, GAVL_META_MEDIA_CLASS_ROOT_REMOVABLE_AUDIOCD);
      free(uri);
      }
    }
  }



static int handle_dbus_msg_volume(void * data, gavl_msg_t * msg)
  {
  bg_volume_manager_t * vol = data;
  
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
          dst_dict = gavl_dictionary_get_dictionary_create(&vol->drives, path);
        else if(is_block_device(path))
          dst_dict = gavl_dictionary_get_dictionary_create(&vol->block_devices, path);
        else
          {
          gavl_dictionary_free(&prop_dict);
          return 1;
          }
        
        dst_dict = gavl_dictionary_get_dictionary_nc(dst_dict, prop_if);
        
        gavl_dictionary_update_fields(dst_dict, &prop_dict);

        if(is_drive(path))
          drive_cb(vol, path, gavl_dictionary_get(&vol->drives, path));
        else if(is_block_device(path))
          blockdevice_cb(vol, path, gavl_dictionary_get(&vol->block_devices, path));
        
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
          dict = gavl_dictionary_get_dictionary_create(&vol->drives, obj);
        else if(is_block_device(obj))
          dict = gavl_dictionary_get_dictionary_create(&vol->block_devices, obj);
        else
          return 1;
        
        gavl_msg_get_arg_dictionary(msg, 1, &if_dict);

        //        fprintf(stderr, "InterfacesAdded: %s %s\n", path, obj);
        //        gavl_dictionary_dump(&if_dict, 2);
        
        gavl_dictionary_update_fields(dict, &if_dict);
        gavl_dictionary_free(&if_dict);

        if(is_drive(obj))
          drive_cb(vol, obj, gavl_dictionary_get(&vol->drives, obj));
        else if(is_block_device(obj))
          blockdevice_cb(vol, obj, gavl_dictionary_get(&vol->block_devices, obj));
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
            dict = gavl_dictionary_get_dictionary_nc(&vol->drives, obj);
            }
          else if(is_block_device(obj))
            {
            dict = gavl_dictionary_get_dictionary_nc(&vol->block_devices, obj);
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
                gavl_dictionary_set(&vol->drives, obj, NULL);
              else if(is_block_device(path))
                gavl_dictionary_set(&vol->block_devices, obj, NULL);
              remove_volume(vol, obj);
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
  bg_volume_manager_t * v = priv;
  
  if((dict_src = gavl_value_get_dictionary(val)))
    {
    gavl_value_t val;
    gavl_value_init(&val);
    dict_dst = gavl_value_set_dictionary(&val);
    gavl_dictionary_copy(dict_dst, dict_src);

    if(is_block_device(name))
      {
      gavl_dictionary_set_nocopy(&v->block_devices, name, &val);
      //      fprintf(stderr, "Got block device: %s\n", name);
      }
    else if(is_drive(name))
      {
      gavl_dictionary_set_nocopy(&v->drives, name, &val);
      //      fprintf(stderr, "Got drive: %s\n", name);
      }
    else /* Should never happen */
      gavl_value_free(&val);
    }
  
  }

bg_volume_manager_t * bg_volume_manager_create()
  {
  gavl_dictionary_t dict;
  
  char * rule;

  bg_volume_manager_t * ret = calloc(1, sizeof(*ret));

  gavl_msg_t * res;
  DBusMessage * req;

  ret->conn = bg_dbus_connection_get(DBUS_BUS_SYSTEM);
  ret->hub = bg_msg_hub_create(1);
  ret->evt_sink = bg_msg_hub_get_sink(ret->hub);
  
  ret->dbus_sink = bg_msg_sink_create(handle_dbus_msg_volume, ret, 1);
  
  /* */
  
  if((ret->daemon_addr = bg_dbus_get_name_owner(ret->conn, "org.freedesktop.UDisks2")))
    {
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Found UDisks2 daemon at %s", ret->daemon_addr);
    rule = bg_sprintf("sender='%s',type='signal'", ret->daemon_addr);
    bg_dbus_connection_add_listener(ret->conn,
                                    rule,
                                    ret->dbus_sink, BG_MSG_NS_PRIVATE, MSG_VOLUME_GENERIC);
    free(rule);
    }
  
  
  req = dbus_message_new_method_call(ret->daemon_addr,
                                     "/org/freedesktop/UDisks2",
                                     "org.freedesktop.DBus.ObjectManager",
                                     "GetManagedObjects");

  res = bg_dbus_connection_call_method(ret->conn, req);
  
  dbus_message_unref(req);

  gavl_dictionary_init(&dict);
  
  if(res && gavl_msg_get_arg_dictionary(res, 0, &dict))
    {
    gavl_dictionary_foreach(&dict, get_managed_objects_foreach_func, ret);
    gavl_dictionary_free(&dict);
    gavl_msg_destroy(res);

    // fprintf(stderr, "Managed Objects:");
    // gavl_array_dump(&ret->dbus_objects, 2);

    gavl_dictionary_foreach(&ret->block_devices, blockdevice_cb, ret);
    
    gavl_dictionary_foreach(&ret->drives, drive_cb, ret);
    }
  
  return ret;
  }

void bg_volume_manager_destroy(bg_volume_manager_t * man)
  {
  if(man->hub)
    bg_msg_hub_destroy(man->hub);

  if(man->dbus_sink)
    bg_msg_sink_destroy(man->dbus_sink);
  
  gavl_dictionary_free(&man->drives);
  gavl_dictionary_free(&man->block_devices);
  
  gavl_dictionary_free(&man->volumes);
  
  free(man);
  }

bg_msg_hub_t * bg_volume_manager_get_msg_hub(bg_volume_manager_t * man)
  {
  return man->hub;
  }

#else

bg_volume_manager_t * bg_volume_manager_create()
  {
  return NULL;
  }

void bg_volume_manager_destroy(bg_volume_manager_t * volman)
  {
  
  }

bg_msg_hub_t * bg_volume_manager_get_msg_hub(bg_volume_manager_t * volman)
  {
  return NULL;
  }

#endif
