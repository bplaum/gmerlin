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




#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <gtk/gtk.h>

#include <config.h>

#include <gmerlin/pluginregistry.h>
#include <gmerlin/resourcemanager.h>
#include <gmerlin/iconfont.h>

#include <gui_gtk/driveselect.h>
#include <gui_gtk/gtkutils.h>

#include <gmerlin/utils.h>

struct bg_gtk_drivesel_s
  {
  /* 
  GtkWidget * window;
  GtkWidget * add_button;
  GtkWidget * close_button;
  */
  GtkWidget * dialog;
  
  bg_msg_sink_t * res_sink;
  GtkWidget * combo_box;

  guint idle_tag;

  bg_msg_sink_t * dlg_sink;
  const char * ctx;
  
  };

enum
  {
    COLUMN_HAS_ICON,
    COLUMN_PIXMAP,
    COLUMN_HAS_PIXMAP,
    COLUMN_LABEL,
    COLUMN_URI,
    COLUMN_ID,
    NUM_COLUMNS
  };

static int id_to_iter(bg_gtk_drivesel_t * d, GtkTreeIter * iter,
                      const char * id)
  {
  const char * iter_id;
  
  GtkTreeModel * model;
  GValue value={0,};
  
  model = gtk_combo_box_get_model(GTK_COMBO_BOX(d->combo_box));
  
  if(!gtk_tree_model_get_iter_first(model, iter))
    return 0;
  
  while(1)
    {
    gtk_tree_model_get_value(model, iter, COLUMN_ID, &value);

    if(!(iter_id = g_value_get_string(&value)))
      return 0;
    
    if(!strcmp(iter_id, id))
      {
      g_value_unset(&value);
      return 1;
      }
    g_value_unset(&value);

    if(!gtk_tree_model_iter_next(model, iter))
      break;
    }
  return 0;
  }


static int handle_msg(void * data, gavl_msg_t * msg)
  {
  //  fprintf(stderr, "Got resource message\n");
  //  gavl_msg_dump(msg, 2);
  bg_gtk_drivesel_t * d = data;
  
  switch(msg->NS)
    {
    case GAVL_MSG_NS_GENERIC:
      switch(msg->ID)
        {
        case GAVL_MSG_RESOURCE_ADDED:
          {
          const char * klass;
          const char * uri;
          gavl_dictionary_t dict;
          GtkTreeIter iter;
          GtkTreeModel * model;
          char * label_escaped;
          char * markup;
          int empty = 0;
          const char * id = gavl_dictionary_get_string(&msg->header, GAVL_MSG_CONTEXT_ID);
          model = gtk_combo_box_get_model(GTK_COMBO_BOX(d->combo_box));
          
          gavl_dictionary_init(&dict);
          gavl_msg_get_arg_dictionary(msg, 0, &dict);
          klass = gavl_dictionary_get_string(&dict, GAVL_META_CLASS);
          if(!klass || !gavl_string_starts_with(klass, "container.root.removable"))
            return 1;

          /* Check if we are empty */
          if(!gtk_tree_model_get_iter_first(model, &iter))
            empty = 1;
          
          if(!id_to_iter(d, &iter, id))
            {
            gtk_list_store_append(GTK_LIST_STORE(model), &iter);
            fprintf(stderr, "Resource added: %s\n", id);
            gavl_dictionary_dump(&dict, 2);
            }

          uri = gavl_dictionary_get_string(&dict, GAVL_META_URI);

          /* TODO: Load media info */
          // mi = bg_plugin_registry_load_media_info(bg_plugin_reg, uri, 0);
          
          label_escaped = g_markup_printf_escaped("%s", gavl_dictionary_get_string(&dict, GAVL_META_LABEL));
          
          markup = gavl_sprintf("<markup><span weight=\"bold\">%s</span>\n<span size=\"small\">%s</span></markup>",
                                label_escaped, uri);
          
          g_free(label_escaped);
          
          gtk_list_store_set(GTK_LIST_STORE(model), &iter, COLUMN_HAS_ICON, TRUE, COLUMN_LABEL, markup, COLUMN_ID, id,
                             COLUMN_URI, uri,
                             -1);
          
          if(empty)
            gtk_combo_box_set_active(GTK_COMBO_BOX(d->combo_box), 0);
          
          //          fprintf(stderr, "URI: %s\n", uri);
          
          gavl_dictionary_free(&dict);
          }
          break;
        case GAVL_MSG_RESOURCE_DELETED:
          {
          GtkTreeIter iter;
          const char * id = gavl_dictionary_get_string(&msg->header, GAVL_MSG_CONTEXT_ID);
          if(id_to_iter(d, &iter, id))
            {
            GtkTreeModel * model = gtk_combo_box_get_model(GTK_COMBO_BOX(d->combo_box));
            fprintf(stderr, "Resource deleted: %s\n", id);
            gtk_list_store_remove(GTK_LIST_STORE(model), &iter);
            }
          
          }
          break;
        }
      break;
    }
  
  return 1;
  }

static void
response_callback(GtkWidget *chooser,
                 gint       response_id,
                 gpointer data)
  {
  gavl_msg_t * msg;
  bg_gtk_drivesel_t * f = data;
  
  switch(response_id)
    {
    case GTK_RESPONSE_APPLY:
      {
      gavl_array_t filenames;
      GtkTreeIter iter;
      GtkTreeModel * model;
      char * str;
      fprintf(stderr, "Apply\n");
      gavl_array_init(&filenames);

      if(!gtk_combo_box_get_active_iter(GTK_COMBO_BOX(f->combo_box), &iter))
        return;

      model = gtk_combo_box_get_model(GTK_COMBO_BOX(f->combo_box));
      
      gtk_tree_model_get(model, &iter, COLUMN_URI, &str, -1);
      gavl_string_array_add(&filenames, str);
      
      msg = bg_msg_sink_get(f->dlg_sink);
      gavl_msg_set_id_ns(msg, BG_MSG_DIALOG_ADD_LOCATIONS, BG_MSG_NS_DIALOG);
      gavl_dictionary_set_string(&msg->header, GAVL_MSG_CONTEXT_ID, f->ctx); 
      gavl_msg_set_arg_array_nocopy(msg, 0, &filenames);
      bg_msg_sink_put(f->dlg_sink);
      gavl_array_free(&filenames);
      }
      break;
    default:
      {
      fprintf(stderr, "Close\n");
      gtk_widget_hide(f->dialog);

      msg = bg_msg_sink_get(f->dlg_sink);
      gavl_msg_set_id_ns(msg, BG_MSG_DIALOG_CLOSED, BG_MSG_NS_DIALOG);
      gavl_dictionary_set_string(&msg->header, GAVL_MSG_CONTEXT_ID, f->ctx); 
      bg_msg_sink_put(f->dlg_sink);
      }
      break;
    }
  
  }

static gboolean idle_func(gpointer user_data)
  {
  bg_gtk_drivesel_t * d = user_data;
  
  bg_msg_sink_iteration(d->res_sink);
  
  return TRUE;
  }

bg_gtk_drivesel_t *
bg_gtk_drivesel_create(const char * title,
                       bg_msg_sink_t * sink,
                       const char * ctx,
                       GtkWidget * parent_window)
  {
  bg_gtk_drivesel_t * ret;
  GtkWidget * table;
  GtkWidget * label;
  GtkWidget * content_area;
  bg_controllable_t * resman;
  GtkListStore * store;
  GtkCellRenderer * column;
  GtkDialogFlags flags;
  
#if 0
  COLUMN_ICON,
  COLUMN_LABEL,
  COLUMN_URI,
  COLUMN_ID,
#endif
  
  store = gtk_list_store_new(NUM_COLUMNS,
                             G_TYPE_BOOLEAN,  // has_icon
                             GDK_TYPE_PIXBUF, // pixmap
                             G_TYPE_BOOLEAN,  // has_pixmap
                             G_TYPE_STRING,   // Label
                             G_TYPE_STRING,   // uri
                             G_TYPE_STRING);  // id
    
  ret = calloc(1, sizeof(*ret));

  ret->dlg_sink = sink;
  ret->ctx = ctx;
  
  /* Create dialog */
  flags = GTK_DIALOG_DESTROY_WITH_PARENT;
  ret->dialog = gtk_dialog_new_with_buttons("Message",
                                            GTK_WINDOW(parent_window),
                                            flags,
                                            TR("Add"),
                                            GTK_RESPONSE_APPLY,
                                            TR("Close"),
                                            GTK_RESPONSE_CLOSE,
                                            NULL);

  g_signal_connect(G_OBJECT(ret->dialog), "response", G_CALLBACK(response_callback), ret);
  
  /* Create Menu */

  ret->combo_box = gtk_combo_box_new_with_model(GTK_TREE_MODEL(store));

  /* Icon */
  column = gtk_cell_renderer_text_new();
  gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(ret->combo_box), column, FALSE);
  g_object_set(G_OBJECT(column), "family", BG_ICON_FONT_FAMILY, NULL);
  g_object_set(G_OBJECT(column), "text", BG_ICON_MUSIC_ALBUM, NULL);

  g_object_set(G_OBJECT(column), "size-points", 24.0, NULL);
  g_object_set(G_OBJECT(column), "xalign", 0.5, NULL);
  g_object_set(G_OBJECT(column), "yalign", 0.5, NULL);
  
  gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(ret->combo_box), column,
                                 "visible", COLUMN_HAS_ICON,
                                 NULL);
  
  /* Label */
  column = gtk_cell_renderer_text_new();
  gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(ret->combo_box), column, TRUE);
  
  gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(ret->combo_box), column,
                                 "markup", COLUMN_LABEL,
                                 NULL);
  gtk_widget_show(ret->combo_box);
  
  /* Pack everything */
  
  table = gtk_grid_new();

  gtk_grid_set_column_spacing(GTK_GRID(table), 5);
  gtk_grid_set_row_spacing(GTK_GRID(table), 5);
  
  label = gtk_label_new(TR("Disk:"));
  gtk_widget_show(label);

  bg_gtk_table_attach(table, label, 0, 1, 1, 2, 0, 0);
  bg_gtk_table_attach(table, ret->combo_box, 1, 2, 1, 2, 0, 0);
  
  gtk_widget_show(table);

  content_area = gtk_dialog_get_content_area(GTK_DIALOG(ret->dialog));
  
  gtk_container_add(GTK_CONTAINER(content_area), table);
  
  resman = bg_resourcemanager_get_controllable();

  ret->res_sink = bg_msg_sink_create(handle_msg, ret, 0);
  
  bg_msg_hub_connect_sink(resman->evt_hub, ret->res_sink);

  ret->idle_tag = g_timeout_add(100, idle_func, ret);
  
  return ret;
  }



/* Destroy driveselector */

void bg_gtk_drivesel_destroy(bg_gtk_drivesel_t * drivesel)
  {
  if(drivesel->idle_tag > 0)
    g_source_remove(drivesel->idle_tag);
  free(drivesel);
  }

/* Show the window */

void bg_gtk_drivesel_run(bg_gtk_drivesel_t * drivesel, int modal)
  {
  gtk_window_set_modal(GTK_WINDOW(drivesel->dialog), modal);
  gtk_widget_show(drivesel->dialog);
  
  }
