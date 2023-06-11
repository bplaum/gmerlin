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

/* */

#include <string.h>


#include <gtk/gtk.h>

#include <gavl/gavl.h>
#include <gavl/value.h>
#include <gavl/metatags.h>


#include <gui_gtk/backendmenu.h>
#include <gui_gtk/gtkutils.h>

#include <gmerlin/utils.h>
#include <gmerlin/iconfont.h>


struct bg_gtk_backend_menu_s
  {
  GtkWidget * menu;
  gavl_array_t * entries;
  bg_msg_sink_t * sink;
  bg_msg_sink_t * evt_sink;
  
  bg_backend_type_t type;
  
  gavl_array_t devs;

  int have_local;
  };

typedef struct
  {
  GtkWidget * ret;
  const char * uri;
  } item_by_uri_t;

static void item_by_id_cb(GtkWidget * w, gpointer data)
  {
  item_by_uri_t * d = data;
  char * uri;
  
  if(d->ret)
    return;
  
  if((uri = g_object_get_data(G_OBJECT(w), GAVL_META_URI)) &&
     !strcmp(uri, d->uri))
    d->ret = w;
  }

static GtkWidget * item_by_id(bg_gtk_backend_menu_t * m, const char * uri)
  {
  item_by_uri_t d;
  memset(&d, 0, sizeof(d));
  d.uri = uri;
  //  fprintf(stderr, "item_by_id: %s\n", uri);
  gtk_container_foreach(GTK_CONTAINER(m->menu), item_by_id_cb, &d);
  return d.ret;
  }

static const gavl_dictionary_t * dev_by_uri(gavl_array_t * arr, const char * uri)
  {
  int i;
  const gavl_dictionary_t * dict;
  const char * str;
  
  for(i = 0; i < arr->num_entries; i++)
    {
    if((dict = gavl_value_get_dictionary(&arr->entries[i])) &&
       (str = gavl_dictionary_get_string(dict, GAVL_META_URI)) &&
       !strcmp(str, uri))
      return dict;
    }
  return NULL;
  }

static int dev_idx_by_uri(gavl_array_t * arr, const char * uri)
  {
  int i;
  const gavl_dictionary_t * dict;
  const char * str;
  
  for(i = 0; i < arr->num_entries; i++)
    {
    if((dict = gavl_value_get_dictionary(&arr->entries[i])) &&
       (str = gavl_dictionary_get_string(dict, GAVL_META_URI)) &&
       !strcmp(str, uri))
      return i;
    }
  return -1;
  }

static void backend_menu_callback(GtkWidget * w, gpointer data)
  {
  char * uri;
  const gavl_dictionary_t * dev;
  
  bg_gtk_backend_menu_t * m = data;

  if((uri = g_object_get_data(G_OBJECT(w), GAVL_META_URI)) &&
     (dev = dev_by_uri(&m->devs, uri)))
    {
#if 0
    gavl_dictionary_dump(dev, 2);
    fprintf(stderr, "\n");
#endif
    //    fprintf(stderr, "Sel backend: %p\n", m->evt_sink);
    if(m->evt_sink)
      {
      gavl_msg_t * msg = bg_msg_sink_get(m->evt_sink);
      bg_msg_set_backend_info(msg, BG_CMD_SET_BACKEND, dev);
      bg_msg_sink_put(m->evt_sink, msg);
      }
    
    }
  
  }

static void add_item(bg_gtk_backend_menu_t * m, const gavl_dictionary_t * dict)
  {
  GtkWidget * item;
  char * uri;
  gavl_value_t val;
  gavl_dictionary_t * val_dict;
  GList *children;
  char * icon = NULL;
  int type = 0;
  const char * var;
  int self = 0;
  
  char * markup = bg_sprintf("<span weight=\"bold\">%s</span>\n%s",
                             gavl_dictionary_get_string(dict, GAVL_META_LABEL),
                             gavl_dictionary_get_string(dict, GAVL_META_URI));
  
  if(!gavl_dictionary_get_int(dict, BG_BACKEND_TYPE, &type) ||
     (type != m->type) ||
     (m->have_local && gavl_dictionary_get_int(dict, BG_BACKEND_LOCAL, &self) && self))
    return;
  
  if((var = gavl_dictionary_get_string_image_max(dict, GAVL_META_ICON_URL, 48, 48, NULL)))
    icon = gavl_strdup(var);
  else if((var = gavl_dictionary_get_string(dict, GAVL_META_ICON_NAME)))
    icon = bg_sprintf("appicon:%s", var);
  else if(type == BG_BACKEND_MEDIASERVER)
    icon = gavl_strdup("icon:"BG_ICON_SERVER);
  else if(type == BG_BACKEND_RENDERER)
    icon = gavl_strdup("icon:"BG_ICON_PLAYER);
  
  
  gavl_value_init(&val);
  val_dict = gavl_value_set_dictionary(&val);
  gavl_dictionary_copy(val_dict, dict);
  
  item = gtk_radio_menu_item_new(NULL);

  children = gtk_container_get_children(GTK_CONTAINER(m->menu));
  
  if(children && children->data)
    {
    gtk_radio_menu_item_join_group(GTK_RADIO_MENU_ITEM(item), GTK_RADIO_MENU_ITEM(children->data));
    g_list_free(children);
    }
  
  uri = gavl_dictionary_get_string_nc(val_dict, GAVL_META_URI);
  
  if(!icon && !strcmp(uri, "local"))
    {
    GdkPixbuf * pb;
    pb = gdk_pixbuf_scale_simple(bg_gtk_window_icon_pixbuf,
                                 16, 16,  GDK_INTERP_BILINEAR);
    //    window_pixbuf 
    item = bg_gtk_image_menu_item_new_full(item, markup, NULL, pb);
    g_object_unref(pb);
    }
  else
    {
    //    fprintf(stderr, "Icon: %s\n", icon);
    item = bg_gtk_image_menu_item_new_full(item, markup, icon, NULL);
    }
  
  gtk_widget_show(item);

  g_object_set_data(G_OBJECT(item), GAVL_META_URI, uri);
  
  gtk_menu_shell_append(GTK_MENU_SHELL(m->menu), item);

  g_signal_connect(G_OBJECT(item), "activate", G_CALLBACK(backend_menu_callback), m);
  
  //  fprintf(stderr, "add_item %s %s %s\n", uri, label, icon);
  //  gavl_dictionary_dump(val_dict, 2);
  gavl_array_splice_val_nocopy(&m->devs, -1, 0, &val);

  free(markup);
  }

static void del_item(bg_gtk_backend_menu_t * m, const char * uri)
  {
  int idx;
  GtkWidget * w;

  //  fprintf(stderr, "del_item: %s\n", uri);
  
  if((w = item_by_id(m, uri)))
    gtk_container_remove(GTK_CONTAINER(m->menu), w);

  if((idx = dev_idx_by_uri(&m->devs, uri)) >= 0)
    gavl_array_splice_val(&m->devs, idx, 1, NULL);
  }

static int handle_msg(void * priv, gavl_msg_t * msg)
  {
  bg_gtk_backend_menu_t * m = priv;
  gavl_dictionary_t dict;
  gavl_dictionary_init(&dict);
  
  switch(msg->NS)
    {
    case BG_MSG_NS_BACKEND:
      switch(msg->ID)
        {
        case BG_MSG_ADD_BACKEND:
          bg_msg_get_backend_info(msg, &dict);
          add_item(m, &dict);
          break;
        case BG_MSG_DEL_BACKEND:
          {
          const char * uri = gavl_msg_get_arg_string_c(msg, 0);
          del_item(m, uri);
          }
          break;
        }
      break;
    }
  gavl_dictionary_free(&dict);
  return 1;
  }

bg_gtk_backend_menu_t * bg_gtk_backend_menu_create(bg_backend_type_t type,
                                                   int have_local,
                                                   /* Will send BG_MSG_SET_BACKEND events */
                                                   bg_msg_sink_t * evt_sink)
  {
  bg_gtk_backend_menu_t * ret;
  const gavl_dictionary_t * dict;
  int i;
  //  bg_controllable_t * ctrl;
  
  ret = calloc(1, sizeof(*ret));

  ret->menu    = gtk_menu_new();
  ret->entries = bg_backend_registry_get();
  ret->type    = type;
  ret->evt_sink = evt_sink;

  ret->have_local = have_local;
  
  if(have_local)
    {
    /* Prepend local device */
    gavl_dictionary_t local_dev;
    gavl_dictionary_init(&local_dev);
    
    gavl_dictionary_set_string(&local_dev, GAVL_META_URI,       "local");
    gavl_dictionary_set_string(&local_dev, GAVL_META_LABEL,     "Local");
    gavl_dictionary_set_int(&local_dev,    BG_BACKEND_TYPE,     type);
    gavl_dictionary_set_string(&local_dev, BG_BACKEND_PROTOCOL, "gmerlin");
    
    add_item(ret, &local_dev);
    
    gavl_dictionary_free(&local_dev);
    }

  for(i = 0; i < ret->entries->num_entries; i++)
    {
    if((dict = gavl_value_get_dictionary(&ret->entries->entries[i])))
      add_item(ret, dict);
    }
  
  ret->sink = bg_msg_sink_create(handle_msg, ret, 0);
  
  bg_msg_hub_connect_sink(bg_backend_registry_get_evt_hub(), ret->sink);
  
  return ret;
  }

GtkWidget * bg_gtk_backend_menu_get_widget(bg_gtk_backend_menu_t * m)
  {
  return m->menu;
  }

void bg_gtk_backend_menu_destroy(bg_gtk_backend_menu_t * m)
  {
  gavl_array_free(&m->devs);
  free(m);
  }

void bg_gtk_backend_menu_ping(bg_gtk_backend_menu_t * m)
  {
  bg_msg_sink_iteration(m->sink);
  }
