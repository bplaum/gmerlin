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

#include <config.h>

#include <math.h> // pow

#include <gavl/gavl.h>
#include <gavl/utils.h>

#include <gmerlin/translation.h>
#include <gmerlin/iconfont.h>

#include <gui_gtk/configdialog.h>
#include <gui_gtk/gtkutils.h>

static const char * WIDGET_DATA  = "widget-data";
static const char * SECTION_DATA = "section-data";

enum
{
  COLUMN_CHILD,
  COLUMN_LABEL,
  NUM_COLUMNS
};

#define FLAG_DIALOG_ADD (1<<0)
#define FLAG_DIRTY      (1<<1)

typedef struct
  {
  bg_cfg_ctx_t ctx;
  gavl_dictionary_t cur;
  gavl_dictionary_t orig;
  bg_msg_sink_t * sink;

  int flags;
  GtkWidget * w; // Section container

  GtkWidget * config_widget; // Widget, which launched the current config dialog
  char * subsection;
  } section_data_t;

typedef struct
  {
  gavl_parameter_type_t type;
  char * property;
  GObject * obj;

  } widget_data_t;

static void position_callback(GtkWidget * w, gpointer data);

static void time_callback(GtkWidget * w, gpointer data);
static void multimenu_callback(GObject *object, GParamSpec *pspec, gpointer user_data);
static section_data_t * get_section(GtkWidget * w);
static void set_widget(GtkWidget * w, const gavl_value_t * val);

/* Return pointer to the cur dict */
static gavl_value_t * widget_get_value(GtkWidget * w);

static GtkWidget * get_treeview(GtkWidget * w)
  {
  return bg_gtk_find_widget_by_name(w, "treeview");
  }

static GtkWidget * get_stack(GtkWidget * w)
  {
  return bg_gtk_find_widget_by_name(w, "stack");
  }

static gavl_dictionary_t * get_multi_dict(const gavl_dictionary_t * param,
                                          gavl_value_t * val,
                                          GtkWidget * w)
  {
  int type;
  
  /* Store value */
  if(gavl_dictionary_get_int(param, GAVL_PARAMETER_TYPE, &type))
    {
    switch(type)
      {
      case GAVL_PARAMETER_MULTI_MENU:
        return gavl_value_get_dictionary_nc(widget_get_value(w));
        break;
      case GAVL_PARAMETER_MULTI_LIST:
      case GAVL_PARAMETER_MULTI_CHAIN:
        {
        gavl_array_t * arr;
        int idx;

        if((idx = bg_gtk_simple_list_get_selected(w)) < 0)
          return NULL;

        arr = gavl_value_get_array_nc(widget_get_value(w));
        return gavl_value_get_dictionary_nc(&arr->entries[idx]);
        }
        break;
      }
    }
  return NULL;
  }

/* Handle messages from sub dialogs */
static int handle_config_message(void * data, gavl_msg_t * msg)
  {
  section_data_t * section = data;

  //  fprintf(stderr, "handle_config_message\n");
  
  switch(msg->NS)
    {
    case BG_MSG_NS_PARAMETER:
      switch(msg->ID)
        {
        case BG_CMD_SET_PARAMETER:
          {
          const char * name;
          const char * ctx;
          const char * subsection;
          gavl_value_t val;
          const gavl_dictionary_t * param_parent;
          gavl_value_init(&val);
          bg_msg_get_parameter(msg, &name, &val);
          ctx = gavl_dictionary_get_string(&msg->header, GAVL_MSG_CONTEXT_ID);
          subsection = gavl_dictionary_get_string(&msg->header, BG_MSG_PARAMETER_SUBSECTION);

          
#if 1
          fprintf(stderr, "handle_config_message\n");
          fprintf(stderr, "BG_MSG_SET_PARAMETER\n");
          fprintf(stderr, "  ctx:        %s\n", ctx);
          fprintf(stderr, "  name:       %s\n", name);
          fprintf(stderr, "  subsection: %s\n", subsection);
          gavl_value_dump(&val, 2);
          fprintf(stderr, "\n");
#endif
          if(!name)
            {
            return 1;
            }

          param_parent =
            gavl_parameter_get_param_by_name(section->ctx.params,
                                             gtk_widget_get_name(section->config_widget));
          
          if(section->flags & FLAG_DIALOG_ADD)
            {
            gavl_value_t * val_arr;
            gavl_array_t * arr;

            val_arr = widget_get_value(section->config_widget);
            arr = gavl_value_get_array_nc(val_arr);
            
            //            fprintf(stderr, "Add %p %p\n", param_parent, arr);

            if(!gavl_value_get_dictionary(&val))
              {
              fprintf(stderr, "Type is no dictionary\n");
              return 1;
              }

            gavl_array_splice_val_nocopy(arr, -1, 0, &val);
            set_widget(section->config_widget, val_arr);
            }
          else
            {
            gavl_value_t * val_arr;
            gavl_dictionary_t * dst_dict = NULL;

            val_arr = widget_get_value(section->config_widget);
            
            fprintf(stderr, "Config %s %s %s %p\n", ctx, name, subsection, param_parent);
            gavl_value_dump(&val, 2);
            
            if((dst_dict = get_multi_dict(param_parent, val_arr,
                                          section->config_widget)))
              {
              /* Store in the right place */
              if(subsection)
                dst_dict = bg_cfg_section_find_subsection(dst_dict, subsection);
              gavl_dictionary_set_nocopy(dst_dict, name, &val);
              }
            }
          
          break;
          }
        }
      break;
    }
  return 1;
  }

static void file_callback(GtkWidget * w, gpointer data);

static void destroy_widget(gpointer data)
  {
  widget_data_t * w = data;
  if(w->property)
    free(w->property);
  free(w);
  }

static void destroy_section(gpointer data)
  {
  section_data_t * s = data;
  bg_cfg_ctx_free(&s->ctx);
  gavl_dictionary_free(&s->cur);
  if(s->subsection)
    free(s->subsection);
  }

static void apply_foreach_func(void * priv, const char * name,
                               const gavl_value_t * val)
  {
  const gavl_value_t * val_saved = NULL;
  section_data_t * section = priv;
  
  if(!name || (*name == '$'))
    return;

  /* Check if the value changed */
  if(section->ctx.s && (val_saved = gavl_dictionary_get(section->ctx.s, name)) &&
     !gavl_value_compare(val_saved, val))
    {
    return;
    }
  
  //  fprintf(stderr, "Value changed: %s %s\n", name, section->ctx.name);
  
  section->flags |= FLAG_DIRTY;
  
  if(section->ctx.sink)
    {
    gavl_msg_t * msg;

    msg = bg_msg_sink_get(section->ctx.sink);
    gavl_msg_set_id_ns(msg, BG_CMD_SET_PARAMETER, BG_MSG_NS_PARAMETER);
    gavl_dictionary_set_string(&msg->header, GAVL_MSG_CONTEXT_ID, section->ctx.name);
    if(section->subsection)
      {
      gavl_dictionary_set_string(&msg->header,
                                 BG_MSG_PARAMETER_SUBSECTION,
                                 section->subsection);
      }
    bg_msg_set_parameter(msg, name, val);
    bg_msg_sink_put(section->ctx.sink);
    }

  if(section->ctx.s)
    gavl_dictionary_set(section->ctx.s, name, val);
  }
                       
static void apply_section(GtkWidget * w, gpointer data)
  {
  section_data_t * section;

  if(!(section = get_section(w)))
    return;
  
  fprintf(stderr, "Apply section: %s\n", bg_cfg_section_get_name(&section->cur));
  
  gavl_dictionary_foreach(&section->cur, apply_foreach_func, section);
    
  if(section->flags & FLAG_DIRTY)
    {
    if(section->ctx.sink)
      {
      gavl_msg_t * msg;
    
      msg = bg_msg_sink_get(section->ctx.sink);
      gavl_msg_set_id_ns(msg, BG_CMD_SET_PARAMETER, BG_MSG_NS_PARAMETER);
      gavl_dictionary_set_string(&msg->header, GAVL_MSG_CONTEXT_ID, section->ctx.name);
      bg_msg_set_parameter(msg, NULL, NULL);
      bg_msg_sink_put(section->ctx.sink);
      }
    section->flags &= ~FLAG_DIRTY;
    }
  
  }

static void do_apply(GtkWidget * w)
  {
  GtkWidget * container;
  
  if(!(container = get_stack(w)))
    container = gtk_dialog_get_content_area(GTK_DIALOG(w));

  gtk_container_foreach(GTK_CONTAINER(container), apply_section, NULL);
  }

static void response_callback(GtkDialog* self,
                              gint response_id,
                              gpointer user_data)
  {
  switch(response_id)
    {
    case GTK_RESPONSE_APPLY:
      do_apply(GTK_WIDGET(self));
      break;
    case GTK_RESPONSE_OK:
      do_apply(GTK_WIDGET(self));
      /* Close */
      g_idle_add ((GSourceFunc) bg_gtk_destroy_widget, self);
      break;
    case GTK_RESPONSE_CANCEL:
    case GTK_RESPONSE_CLOSE:
      /* Close */
      g_idle_add ((GSourceFunc) bg_gtk_destroy_widget, self);
      break;
    }
  }

static void select_row_callback(GtkTreeSelection * sel,
                                gpointer data)
  {
  GtkTreeIter iter;
  void *pointer;
  GtkTreeModel * model;
  GtkWidget * dialog = data;
  GtkWidget * stack = get_stack(dialog);
  
  if(!gtk_tree_selection_get_selected(sel, &model, &iter))
    return;
  
  gtk_tree_model_get(GTK_TREE_MODEL(model), &iter, COLUMN_CHILD, &pointer, -1);
  gtk_stack_set_visible_child(GTK_STACK(stack), pointer);
  }

static gboolean row_selectable(GtkTreeSelection* selection,
                               GtkTreeModel* model,
                               GtkTreePath* path,
                               gboolean path_currently_selected,
                               gpointer data)
  {
  GtkTreeIter iter;
  GtkWidget * child;
  gtk_tree_model_get_iter(model, &iter, path);

  gtk_tree_model_get(model, &iter, 
                     COLUMN_CHILD, &child,
                     -1);
  if(child)
    return TRUE;
  else
    return FALSE;
  }

static void ensure_treeview(GtkWidget * w)
  {
  GtkWidget * child;
  GtkTreeStore *store;
  GtkCellRenderer   *text_renderer;
  GtkTreeViewColumn *column;
  GtkTreeSelection  *selection;
  GtkWidget * box;

  GtkWidget *content_area;
  
  if(get_treeview(w))
    return;
  
  store = gtk_tree_store_new(NUM_COLUMNS,
                             G_TYPE_POINTER, G_TYPE_STRING);

  child = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
  gtk_widget_set_name(child, "treeview");
  gtk_widget_set_vexpand(child, TRUE);
  
  gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(child), 0);
  text_renderer = gtk_cell_renderer_text_new();
  column = gtk_tree_view_column_new ();
  gtk_tree_view_column_pack_start(column, text_renderer, TRUE);
  gtk_tree_view_column_add_attribute(column,
                                     text_renderer,
                                     "text", COLUMN_LABEL);
  gtk_tree_view_append_column (GTK_TREE_VIEW(child), column);
  selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(child));
  gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);

  gtk_tree_selection_set_select_function(selection, 
                                         row_selectable,
                                         NULL,  // userdata
                                         NULL); // destroy notify

  g_signal_connect(G_OBJECT(selection), "changed",
                   G_CALLBACK(select_row_callback), (gpointer)w);
  
  content_area = gtk_dialog_get_content_area(GTK_DIALOG(w));  
  box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);

  bg_gtk_box_pack_start(box, child, 0);

  child = gtk_stack_new();
  gtk_widget_set_name(child, "stack");
  
  bg_gtk_box_pack_start(box, child, 1);

  gtk_container_add(GTK_CONTAINER(content_area), box);
  gtk_widget_show_all(box);
  }

static section_data_t * get_section(GtkWidget * w)
  {
  section_data_t * ret;

  while(1)
    {
    if((ret = g_object_get_data(G_OBJECT(w), SECTION_DATA)))
      return ret;

    if(!(w = gtk_widget_get_parent(w)))
      return NULL;
    
    }
  return NULL;
  }

static gavl_value_t * widget_get_value(GtkWidget * w)
  {
  section_data_t * s = get_section(w);
  return gavl_dictionary_get_nc(&s->cur, gtk_widget_get_name(w));
  }

static void widget_changed(GtkWidget * w, const gavl_value_t * val)
  {
  section_data_t * s = get_section(w);
  
  gavl_dictionary_set(&s->cur, gtk_widget_get_name(w), val);
  }

static void property_callback(GObject *object, GParamSpec *pspec, gpointer user_data)
  {
  widget_data_t * data;
  GtkWidget * w = user_data;
  GValue value = G_VALUE_INIT;
  gavl_value_t gavl_val;
 
  data = g_object_get_data(G_OBJECT(w), WIDGET_DATA);

  gavl_value_init(&gavl_val);
  
  g_object_get_property(object, pspec->name, &value);

  if(bg_g_value_to_gavl(&value, &gavl_val, data->type))
    {
    widget_changed(w, &gavl_val);
    }

  gavl_value_free(&gavl_val);
  g_value_unset(&value);

  }

static void set_widget(GtkWidget * w, const gavl_value_t * val)
  {
  widget_data_t * data;
  data = g_object_get_data(G_OBJECT(w), WIDGET_DATA);

#if 0
  fprintf(stderr, "set_widget %s\n", gtk_widget_get_name(w));
  gavl_value_dump(val, 2);
  fprintf(stderr, "\n");
#endif
  
  switch(data->type)
    {
    case GAVL_PARAMETER_CHECKBUTTON:
    case GAVL_PARAMETER_INT:
    case GAVL_PARAMETER_SLIDER_INT:
    case GAVL_PARAMETER_FLOAT:
    case GAVL_PARAMETER_SLIDER_FLOAT:
    case GAVL_PARAMETER_STRING:
    case GAVL_PARAMETER_STRING_HIDDEN:
    case GAVL_PARAMETER_FONT:
    case GAVL_PARAMETER_COLOR_RGB:
    case GAVL_PARAMETER_COLOR_RGBA:
    case GAVL_PARAMETER_STRINGLIST:
      {
      GValue value = G_VALUE_INIT;
      
      if(bg_g_value_from_gavl(&value, val, data->type))
        {
        g_signal_handlers_block_by_func(data->obj, G_CALLBACK(property_callback), w);
        g_object_set_property(data->obj, data->property, &value);
        g_signal_handlers_unblock_by_func(data->obj, G_CALLBACK(property_callback), w);
        }
      g_value_unset(&value);
      }
      break;
    case GAVL_PARAMETER_FILE:
    case GAVL_PARAMETER_DIRECTORY:
      {
      const char * filename = gavl_value_get_string(val);

      g_signal_handlers_block_by_func(w, G_CALLBACK(file_callback), w);
      gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(w), filename);
      g_signal_handlers_unblock_by_func(w, G_CALLBACK(file_callback), w);
      
      }
      break;
    case GAVL_PARAMETER_DIRLIST:
      {
      int i;
      const gavl_array_t * arr = gavl_value_get_array(val);
      bg_gtk_simple_list_clear(w);
      for(i = 0; i < arr->num_entries; i++)
        bg_gtk_simple_list_add(w, gavl_string_array_get(arr, i), NULL, -1);
      }
      break;
    case GAVL_PARAMETER_POSITION:
      {
      GtkWidget * spinbutton;
      const double * pos = gavl_value_get_position(val);

      spinbutton = bg_gtk_find_widget_by_name(w, "X");
      g_signal_handlers_block_by_func(spinbutton, G_CALLBACK(position_callback), w);
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(spinbutton), pos[0]);
      g_signal_handlers_unblock_by_func(spinbutton, G_CALLBACK(position_callback), w);
      
      spinbutton = bg_gtk_find_widget_by_name(w, "Y");
      g_signal_handlers_block_by_func(spinbutton, G_CALLBACK(position_callback), w);
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(spinbutton), pos[1]);
      g_signal_handlers_unblock_by_func(spinbutton, G_CALLBACK(position_callback), w);
      
      }
      break;
    case GAVL_PARAMETER_TIME:
      {
      GtkWidget * spinbutton;
      int64_t t = 0;
      gavl_value_get_long(val, &t);

      spinbutton = bg_gtk_find_widget_by_name(w, "MS");
      g_signal_handlers_block_by_func(spinbutton, G_CALLBACK(time_callback), w);
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(spinbutton), (t % GAVL_TIME_SCALE) / 1000 );
      g_signal_handlers_unblock_by_func(spinbutton, G_CALLBACK(time_callback), w);
      
      t /= GAVL_TIME_SCALE;

      spinbutton = bg_gtk_find_widget_by_name(w, "S");
      g_signal_handlers_block_by_func(spinbutton, G_CALLBACK(time_callback), w);
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(spinbutton), t % 60);
      g_signal_handlers_unblock_by_func(spinbutton, G_CALLBACK(time_callback), w);

      t /= 60;

      spinbutton = bg_gtk_find_widget_by_name(w, "M");
      g_signal_handlers_block_by_func(spinbutton, G_CALLBACK(time_callback), w);
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(spinbutton), t % 60);
      g_signal_handlers_unblock_by_func(spinbutton, G_CALLBACK(time_callback), w);

      t /= 60;

      spinbutton = bg_gtk_find_widget_by_name(w, "H");
      g_signal_handlers_block_by_func(spinbutton, G_CALLBACK(time_callback), w);
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(spinbutton), t);
      g_signal_handlers_unblock_by_func(spinbutton, G_CALLBACK(time_callback), w);
      
      }
      break;
    case GAVL_PARAMETER_MULTI_MENU:
      {
      const char * name;
      gavl_value_t * val_dst;
      gavl_dictionary_t * dict_dst;
      
      const gavl_dictionary_t * dict = gavl_value_get_dictionary(val);
      
#if 0
      fprintf(stderr, "Set multi menu: 1\n");
      gavl_dictionary_dump(dict, 2);
      fprintf(stderr, "\n");
#endif
      
      val_dst = widget_get_value(w);
      
      if(!(name = gavl_dictionary_get_string(dict, BG_CFG_TAG_NAME)))
        break;
      
      g_signal_handlers_block_by_func(w, G_CALLBACK(multimenu_callback), w);
      gtk_combo_box_set_active_id(GTK_COMBO_BOX(w), name);
      g_signal_handlers_unblock_by_func(w, G_CALLBACK(multimenu_callback), w);

      if(val_dst == val)
        break;

      dict_dst = gavl_value_get_dictionary_nc(val_dst);
      
      gavl_dictionary_reset(dict_dst);
      gavl_dictionary_copy(dict_dst, dict);

#if 0      
      fprintf(stderr, "Set multi menu: 2 %p %p\n",
              dict_dst, dict);
      
      gavl_dictionary_dump(dict_dst, 2);
      fprintf(stderr, "\n");
#endif       
      }
      break;
    case GAVL_PARAMETER_MULTI_LIST:
    case GAVL_PARAMETER_MULTI_CHAIN:
      {
      int i;
      const char * name;
      const char * label;
      section_data_t * section;
      
      const gavl_dictionary_t * dict;
      const gavl_dictionary_t * param;
      const gavl_dictionary_t * opt;
      
      const gavl_array_t * arr = gavl_value_get_array(val);
      bg_gtk_simple_list_clear(w);
      
      section = get_section(w);

      param = gavl_parameter_get_param_by_name(section->ctx.params, gtk_widget_get_name(w));
      
      for(i = 0; i < arr->num_entries; i++)
        {
        if(!(dict = gavl_value_get_dictionary(&arr->entries[i])) ||
           !(name = gavl_dictionary_get_string(dict, BG_CFG_TAG_NAME)))
          continue;
        
        opt = gavl_parameter_get_option_by_name(param, name);
        
        if(!(label = gavl_dictionary_get_string(opt, GAVL_META_LABEL)))
          label = name;
        
        bg_gtk_simple_list_add(w, name, label, i);
        }
      }
      break;
    case GAVL_PARAMETER_BUTTON:
      break;
    case GAVL_PARAMETER_SECTION:
      break;
    }
  }

static widget_data_t * set_cfg_widget(GtkWidget * w, const char * name, gavl_parameter_type_t type,
                                      const char * property, GObject * obj, const char * help_string,
                                      const char * translation_domain)
  {
  widget_data_t * wid = calloc(1, sizeof(*wid));

  if(property)
    {
    char * tmp_string;
    tmp_string = gavl_sprintf("notify::%s", property);
    g_signal_connect(obj ? obj : G_OBJECT(w), tmp_string, G_CALLBACK(property_callback), w);
    free(tmp_string);
    }

  if(help_string)
    bg_gtk_tooltips_set_tip(w, help_string, translation_domain);
  
  gtk_widget_set_name(w, name);

  wid->type     = type;
  wid->property = gavl_strdup(property);
  wid->obj      = obj ? obj : G_OBJECT(w);
  
  g_object_set_data_full(G_OBJECT(w), WIDGET_DATA, wid, destroy_widget);
  return wid;
  }

/* Button callbacks */

static void dirlist_get(GtkWidget * w, gavl_value_t * val)
  {
  int idx = 0;
  gavl_array_t * arr;
  char * str;
  gavl_value_init(val);
  arr = gavl_value_set_array(val);

  while((str = bg_gtk_simple_list_get_name(w, idx)))
    {
    gavl_string_array_add_nocopy(arr, str);
    idx++;
    }
  }

static void
dirlist_add_response(GtkWidget *chooser,
                     gint       response_id,
                     gpointer data)
  {
  GtkWidget * w = data;
  if(response_id == GTK_RESPONSE_OK)
    {
    gavl_array_t * arr;
    gavl_value_t val;
    
    char * str = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));

    dirlist_get(w, &val);
    arr = gavl_value_get_array_nc(&val);
    gavl_string_array_add(arr, str);
    //    fprintf(stderr, "Got response %s\n", str);
    g_free(str);
    
    widget_changed(w, &val);
    set_widget(w, &val);
    }
  
  g_idle_add ((GSourceFunc) bg_gtk_destroy_widget, chooser);
  }

static void dirlist_add_callback(GtkWidget * dummy, gpointer data)
  {
  GtkWidget * chooser;
  GtkWidget * w = data;
  GtkWidget * parent = bg_gtk_get_toplevel(w);
  
  chooser =
    gtk_file_chooser_dialog_new(TR("Add directory"),
                                GTK_WINDOW(parent),
                                GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
                                TR("_Cancel"),
                                GTK_RESPONSE_CANCEL,
                                TR("_OK"),
                                GTK_RESPONSE_OK,
                                NULL);

  gtk_window_set_modal(GTK_WINDOW(chooser), 1);
  
  /* Set callbacks */
  
  g_signal_connect(G_OBJECT(chooser), "response",
                   G_CALLBACK(dirlist_add_response),
                   w);
  
  /* Run the widget */
  
  gtk_window_present(GTK_WINDOW(chooser));
  
  }


static void dirlist_delete_callback(GtkWidget * dummy, gpointer data)
  {
  GtkWidget * w = data;
  gavl_array_t * arr;
  gavl_value_t val;
  int selected = bg_gtk_simple_list_get_selected(w);

  dirlist_get(w, &val);
  arr = gavl_value_get_array_nc(&val);
  gavl_array_splice_val(arr, selected, 1, NULL);
  widget_changed(w, &val);
  set_widget(w, &val);
  gavl_value_free(&val);
  }

static void multilist_config_callback(GtkWidget * dummy, gpointer data)
  {
  const gavl_dictionary_t * param;
  const gavl_dictionary_t * item;
  bg_cfg_ctx_t sub_ctx;
  GtkWidget * w = data;
  section_data_t * section = get_section(w);
  
  const char * name;
  const char * label;
  const char * item_name;
  GtkWidget * dialog;
  
  int selected;

  selected = bg_gtk_simple_list_get_selected(w);
  if(selected < 0)
    return;

  name = gtk_widget_get_name(w);
  
  param = gavl_parameter_get_param_by_name(section->ctx.params, name);
  label = gavl_dictionary_get_string(param, GAVL_META_LABEL);
  
  if(!label)
    label = name;

  item_name = bg_gtk_simple_list_get_name(w, selected);

  item = gavl_parameter_get_option_by_name(param, item_name);

  label = gavl_dictionary_get_string(item, GAVL_META_LABEL);
  if(!label)
    label = item_name;
  
  bg_cfg_ctx_init(&sub_ctx, NULL, name, label, NULL, NULL);

  if(item)
    {
    gavl_dictionary_copy(&sub_ctx.params_priv, item);
    sub_ctx.params = &sub_ctx.params_priv;
    }

  sub_ctx.s = get_multi_dict(param, widget_get_value(w), w);
  
  if(!section->sink)
    section->sink = bg_msg_sink_create(handle_config_message, section, 1);

  sub_ctx.sink = section->sink;
  section->config_widget = w;
  
  section->flags &= ~FLAG_DIALOG_ADD;
  
  dialog = bg_gtk_config_dialog_create_single(BG_GTK_CONFIG_DIALOG_OK_CANCEL |
                                              BG_GTK_CONFIG_DIALOG_DESTROY,
                                              label, w, &sub_ctx);
  
  gtk_window_present(GTK_WINDOW(dialog));
  bg_cfg_ctx_free(&sub_ctx);
  }

static void multichain_add_callback(GtkWidget * dummy, gpointer data)
  {
  const char * name;
  const char * label;
  GtkWidget * dialog;
  
  bg_cfg_ctx_t sub_ctx;
  const gavl_dictionary_t * param;
  gavl_dictionary_t * param_menu;
  GtkWidget * w = data;
  section_data_t * section = get_section(w);
  
  name = gtk_widget_get_name(w);
  
  if(!(param = gavl_parameter_get_param_by_name(section->ctx.params, name)))
    {
    fprintf(stderr, "No param found %s\n", name);
    gavl_dictionary_dump(section->ctx.params, 2);
    }
  
  label = gavl_dictionary_get_string(param, GAVL_META_LABEL);
  
  bg_cfg_ctx_init(&sub_ctx, NULL, name, label, NULL, NULL);

  
  sub_ctx.params = &sub_ctx.params_priv;

  param_menu = gavl_parameter_append_param(&sub_ctx.params_priv,
                                           NULL, NULL, 0);

  gavl_dictionary_copy(param_menu, param);
  gavl_dictionary_set_int(param_menu, GAVL_PARAMETER_TYPE, GAVL_PARAMETER_MULTI_MENU);

  if(!section->sink)
    section->sink = bg_msg_sink_create(handle_config_message, section, 1);
  
  sub_ctx.sink = section->sink;
  section->flags |= FLAG_DIALOG_ADD;

  section->config_widget = w;
  
  dialog = bg_gtk_config_dialog_create_single(BG_GTK_CONFIG_DIALOG_DESTROY |
                                              BG_GTK_CONFIG_DIALOG_ADD_CLOSE,
                                              label, w, &sub_ctx);

  gtk_window_present(GTK_WINDOW(dialog));
  
  
  bg_cfg_ctx_free(&sub_ctx);
  
  }

static void multichain_delete_callback(GtkWidget * dummy, gpointer data)
  {
  int idx;
  GtkWidget * w = data;
  gavl_array_t * arr;
  gavl_value_t * val;
  
  if((idx = bg_gtk_simple_list_get_selected(w)) < 0)
    return;

  val = widget_get_value(w);
  arr = gavl_value_get_array_nc(val);

  gavl_array_splice_val(arr, idx, 1, NULL);
  set_widget(w, val);
  }

static void multimenu_config_callback(GtkWidget * dummy, gpointer data)
  {
  section_data_t * section;
  const char * name;
  const char * opt_name;
  const char * label;
  const gavl_dictionary_t * opt;
  const gavl_dictionary_t * param;
  GtkWidget * dialog;
  bg_cfg_ctx_t sub_ctx;
  GtkWidget * w = data;
  
  int idx = gtk_combo_box_get_active(GTK_COMBO_BOX(w));
  
  name = gtk_widget_get_name(w);

  section = get_section(w);
  
  if(!(param = gavl_parameter_get_param_by_name(section->ctx.params, name)) ||
     !(opt = gavl_parameter_get_option(param, idx)))
    return;
  
  opt_name = gavl_dictionary_get_string(opt, GAVL_META_NAME);
  label = gavl_dictionary_get_string(opt, GAVL_META_LABEL);
  if(!label)
    label = opt_name;
  
  bg_cfg_ctx_init(&sub_ctx, NULL, opt_name, label, NULL, NULL);
  
  /* */

  if(opt)
    {
    sub_ctx.params = &sub_ctx.params_priv;
    gavl_dictionary_copy(&sub_ctx.params_priv, opt);
    }

  sub_ctx.s = get_multi_dict(param, widget_get_value(w), w);
  
  if(!section->sink)
    section->sink = bg_msg_sink_create(handle_config_message, section, 1);
  
  sub_ctx.sink = section->sink;

  section->flags &= ~FLAG_DIALOG_ADD;

  section->config_widget = w;
  
  dialog = bg_gtk_config_dialog_create_single(BG_GTK_CONFIG_DIALOG_OK_CANCEL |
                                              BG_GTK_CONFIG_DIALOG_DESTROY,
                                              label, w, &sub_ctx);

  gtk_window_present(GTK_WINDOW(dialog));

  bg_cfg_ctx_free(&sub_ctx);
  }

/* Non property callbacks */

static void file_callback(GtkWidget * w, gpointer data)
  {
  char * str;
  gavl_value_t val;
  GtkWidget * cfg_widget = data;
  str = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(w));
  gavl_value_init(&val);
  gavl_value_set_string(&val, str);
  free(str);
  widget_changed(cfg_widget, &val);
  gavl_value_free(&val);
  }

static void time_callback(GtkWidget * w, gpointer data)
  {
  gavl_value_t val;
  GtkWidget * spinbutton;
  gavl_time_t time;
  GtkWidget * cfg_widget = data;
  
  spinbutton = bg_gtk_find_widget_by_name(cfg_widget, "H");
  time = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spinbutton));
  time *= 60;
  spinbutton = bg_gtk_find_widget_by_name(cfg_widget, "M");
  time += gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spinbutton));
  time *= 60;
  spinbutton = bg_gtk_find_widget_by_name(cfg_widget, "S");
  time += gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spinbutton));
  time *= 1000;
  spinbutton = bg_gtk_find_widget_by_name(cfg_widget, "MS");
  time += gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spinbutton));
  time *= 1000;
  gavl_value_init(&val);
  gavl_value_set_long(&val, time);
  widget_changed(cfg_widget, &val);
  }

static void position_callback(GtkWidget * w, gpointer data)
  {
  gavl_value_t val;
  GtkWidget * spinbutton;
  GtkWidget * cfg_widget = data;
  double * pos;
  gavl_value_init(&val);
  pos = gavl_value_set_position(&val);
  
  spinbutton = bg_gtk_find_widget_by_name(cfg_widget, "X");
  pos[0] = gtk_spin_button_get_value(GTK_SPIN_BUTTON(spinbutton));
  spinbutton = bg_gtk_find_widget_by_name(cfg_widget, "Y");
  pos[1] = gtk_spin_button_get_value(GTK_SPIN_BUTTON(spinbutton));
  widget_changed(cfg_widget, &val);
  gavl_value_free(&val);
  }

static void multimenu_callback(GObject *object, GParamSpec *pspec, gpointer user_data)
  {
  section_data_t * section;
  const char * name;
  const gavl_dictionary_t * opt;
  gavl_dictionary_t * dst_dict;
  gavl_value_t * val;
  
  GtkWidget * w = user_data;
  
  int idx = gtk_combo_box_get_active(GTK_COMBO_BOX(w));

  name = gtk_widget_get_name(w);
  
  //  fprintf(stderr, "multimenu_callback: %d\n", idx);
  section = get_section(w);

  if((opt = gavl_parameter_get_param_by_name(section->ctx.params, name)) &&
     (opt = gavl_parameter_get_option(opt, idx)))
    {
    val = widget_get_value(w);
    
    dst_dict = gavl_value_get_dictionary_nc(val);
    gavl_dictionary_reset(dst_dict);
    gavl_dictionary_set_string(dst_dict, BG_CFG_TAG_NAME, name);

    bg_cfg_section_set_from_params(dst_dict, opt);

    /*    
    fprintf(stderr, "Got value\n");
    gavl_dictionary_dump(dst_dict, 2);
    widget_changed(w, val);
    */
    }
  }


static void set_widget_callback(void * data, const char * name,
                                const gavl_value_t * v)
  {
  GtkWidget * w = data;
  if(!(w = bg_gtk_find_widget_by_name(w, name)))
    return;
  set_widget(w, v);
  }

static void button_callback(GtkWidget * w, gpointer data)
  {
  gavl_msg_t * msg;
  section_data_t * section = data;
  fprintf(stderr, "Button clicked %s\n", gtk_widget_get_name(w));

  msg = bg_msg_sink_get(section->ctx.sink);
  gavl_msg_set_id_ns(msg, BG_CMD_PARAMETER_BUTTON, BG_MSG_NS_PARAMETER);
  gavl_dictionary_set_string(&msg->header, GAVL_MSG_CONTEXT_ID, section->ctx.name);
  gavl_msg_set_arg_string(msg, 0, gtk_widget_get_name(w));
  bg_msg_sink_put(section->ctx.sink);
  }

static void add_config_widgets(GtkWidget * grid,
                               const gavl_dictionary_t * params,
                               bg_cfg_ctx_t * ctx,
                               const char * translation_domain, const char * subsection)
  {
  int i;
  const gavl_dictionary_t * param;
  gavl_dictionary_t * sec;
  gavl_parameter_type_t type;
  const char * long_name;
  const char * help_string;
  const char * name;
  int ypos = 0;
  GtkWidget * w;
  int num_params = gavl_parameter_num_params(params);

  section_data_t * s = calloc(1, sizeof(*s));

  s->ctx.name      = gavl_strdup(ctx->name);
  s->ctx.long_name = gavl_strdup(ctx->long_name);

  s->subsection = gavl_strdup(subsection);
  
  if(params)
    {
    s->ctx.params = &s->ctx.params_priv;
    gavl_dictionary_copy(&s->ctx.params_priv, params);
    }

  gtk_grid_set_row_spacing(GTK_GRID(grid), 5);
  gtk_grid_set_column_spacing(GTK_GRID(grid), 5);
  
  /* Create section with dialog state */
  bg_cfg_section_set_from_params(&s->cur, s->ctx.params);
  
  /* Get saved data */
  if((sec = ctx->s))
    {
    if(subsection)
      sec = bg_cfg_section_find_subsection(sec, subsection);
    gavl_dictionary_update_fields_nocreate(&s->cur, sec);
    s->ctx.s = gavl_dictionary_clone(sec);
    }
  
  /* Copy callbacks */
  s->ctx.set_param = ctx->set_param;
  s->ctx.cb_data   = ctx->cb_data;
  s->ctx.sink      = ctx->sink;
  s->ctx.msg_id    = ctx->msg_id;
  s->w             = grid;

  g_object_set_data_full(G_OBJECT(grid), SECTION_DATA, s, destroy_section);
  
  for(i = 0; i < num_params; i++)
    {
    param = gavl_parameter_get_param(params, i);
    type = gavl_parameter_info_get_type(param);

    if(!(name = gavl_dictionary_get_string(param, GAVL_META_NAME)))
      continue;
    
    if(!(long_name = gavl_dictionary_get_string(param, GAVL_META_LABEL)))
      long_name = name;

    help_string = gavl_dictionary_get_string(param, GAVL_PARAMETER_HELP);

    w = NULL;
    
    switch(type)
      {
      case GAVL_PARAMETER_CHECKBUTTON:
        {
        w = gtk_check_button_new_with_label(TR_DOM(long_name));
        
        set_cfg_widget(w, name, type, "active", NULL, help_string, translation_domain);
        
        gtk_widget_set_hexpand(w, TRUE);
        gtk_grid_attach(GTK_GRID(grid), w, 0, ypos, 3, 1);
        ypos++;
        }
        break;
      case GAVL_PARAMETER_INT:
      case GAVL_PARAMETER_FLOAT:
        {
        double min;
        double max;
        double step;
        int num_digits = 0;
        GtkWidget * label;
        
        const gavl_value_t * val;

        if(!(val = gavl_dictionary_get(param, GAVL_PARAMETER_MIN)) ||
           !gavl_value_get_float(val, &min))
          min = 0.0;

        if(!(val = gavl_dictionary_get(param, GAVL_PARAMETER_MAX)) ||
           !gavl_value_get_float(val, &max))
          max = 100.0;

        if(!(val = gavl_dictionary_get(param, GAVL_PARAMETER_NUM_DIGITS)) ||
           !gavl_value_get_int(val, &num_digits))
          num_digits = 0;

        step = pow(10, -num_digits);
        
        w = gtk_spin_button_new_with_range(min, max, step);

        gtk_spin_button_set_digits(GTK_SPIN_BUTTON(w), num_digits);
        

        gtk_widget_set_hexpand(w, TRUE);

        label = gtk_label_new(long_name);
        gtk_widget_set_halign(label, GTK_ALIGN_START);
        
        gtk_grid_attach(GTK_GRID(grid), label, 0, ypos, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), w, 1, ypos, 2, 1);

        set_cfg_widget(w, name, type, "value", NULL, help_string, translation_domain);
        
        ypos++;
        }
        break;
      case GAVL_PARAMETER_SLIDER_INT:
      case GAVL_PARAMETER_SLIDER_FLOAT:
        {
        double min;
        double max;
        double step;
        GtkWidget * label;
        const gavl_value_t * val;
        int num_digits = 0;

        if(!(val = gavl_dictionary_get(param, GAVL_PARAMETER_MIN)) ||
           !gavl_value_get_float(val, &min))
          min = 0.0;

        if(!(val = gavl_dictionary_get(param, GAVL_PARAMETER_MAX)) ||
           !gavl_value_get_float(val, &max))
          max = 100.0;

        if(!(val = gavl_dictionary_get(param, GAVL_PARAMETER_NUM_DIGITS)) ||
           !gavl_value_get_int(val, &num_digits))
          num_digits = 0;

        step = pow(10, -num_digits);
        
        w = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL,
                                     min, max,  step);
        
        gtk_scale_set_value_pos(GTK_SCALE(w), GTK_POS_LEFT);
        gtk_scale_set_digits(GTK_SCALE(w), num_digits);
        
        gtk_widget_set_hexpand(w, TRUE);
        
        label = gtk_label_new(long_name);
        gtk_widget_set_halign(label, GTK_ALIGN_START);
        
        gtk_grid_attach(GTK_GRID(grid), label, 0, ypos, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), w, 1, ypos, 2, 1);
        
        set_cfg_widget(w, name, type, "value", G_OBJECT(gtk_range_get_adjustment(GTK_RANGE(w))),
                       help_string, translation_domain);
        
        ypos++;
        }
        break;
      case GAVL_PARAMETER_STRING:
      case GAVL_PARAMETER_STRING_HIDDEN:
        {
        GtkWidget * label;
        
        w = gtk_entry_new();

        if(type == BG_PARAMETER_STRING_HIDDEN)
          gtk_entry_set_visibility(GTK_ENTRY(w), FALSE);

        gtk_widget_set_hexpand(w, TRUE);
        label = gtk_label_new(long_name);
        gtk_widget_set_halign(label, GTK_ALIGN_START);
        set_cfg_widget(w, name, type, "text", NULL, help_string, translation_domain);
        
        gtk_grid_attach(GTK_GRID(grid), label, 0, ypos, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), w, 1, ypos, 2, 1);
        
        ypos++;
        }
        break;
      case GAVL_PARAMETER_FONT:
        {
        GtkWidget * label;
        
        w = gtk_font_button_new();
        gtk_font_button_set_title(GTK_FONT_BUTTON(w), long_name);
        label = gtk_label_new(long_name);
        gtk_widget_set_halign(label, GTK_ALIGN_START);
        gtk_widget_set_hexpand(w, TRUE);
        set_cfg_widget(w, name, type, "font", NULL, help_string, translation_domain);
        
        gtk_grid_attach(GTK_GRID(grid), label, 0, ypos, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), w, 1, ypos, 2, 1);
        
        ypos++;
        
        }
        break;
      case GAVL_PARAMETER_FILE:
      case GAVL_PARAMETER_DIRECTORY:
        {
        GtkWidget * label;

        GtkFileChooserAction action;

        if(type == GAVL_PARAMETER_FILE)
          action = GTK_FILE_CHOOSER_ACTION_OPEN;
        else
          action = GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER;
        
        label = gtk_label_new(long_name);
        gtk_widget_set_halign(label, GTK_ALIGN_START);
        w = gtk_file_chooser_button_new(long_name, action);
        gtk_widget_set_hexpand(w, TRUE);
        set_cfg_widget(w, name, type, NULL, NULL, help_string, translation_domain);
        g_signal_connect(w, "file-set", G_CALLBACK(file_callback), w);
        
        gtk_grid_attach(GTK_GRID(grid), label, 0, ypos, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), w, 1, ypos, 2, 1);
        
        ypos++;
        
        }
        break;
      case GAVL_PARAMETER_COLOR_RGB:
      case GAVL_PARAMETER_COLOR_RGBA:
        {
        GtkWidget * label;
        
        label = gtk_label_new(long_name);
        gtk_widget_set_halign(label, GTK_ALIGN_START);
        w = gtk_color_button_new();

        if(type == GAVL_PARAMETER_COLOR_RGB)
          gtk_color_chooser_set_use_alpha(GTK_COLOR_CHOOSER(w), FALSE);
        else if(type == GAVL_PARAMETER_COLOR_RGBA)
          gtk_color_chooser_set_use_alpha(GTK_COLOR_CHOOSER(w), TRUE);
        
        gtk_grid_attach(GTK_GRID(grid), label, 0, ypos, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), w, 1, ypos, 2, 1);

        set_cfg_widget(w, name, type, "rgba", NULL, help_string, translation_domain);
        
        ypos++;
        }
        break;
      case GAVL_PARAMETER_STRINGLIST:
      case GAVL_PARAMETER_MULTI_MENU:
        {
        GtkWidget * label;
        int i;
        int num;
        label = gtk_label_new(long_name);
        gtk_widget_set_halign(label, GTK_ALIGN_START);
        w = gtk_combo_box_text_new();

        gtk_widget_set_hexpand(w, TRUE);
        
        if(type == GAVL_PARAMETER_MULTI_MENU)
          {
          
          //          fprintf(stderr, "Add multi menu %s\n", name);
          
          //          gavl_value_set_dictionary(&wid->val);
          set_cfg_widget(w, name, type, NULL, NULL, help_string, translation_domain);
          g_signal_connect(w, "notify::active-id", G_CALLBACK(multimenu_callback), w);
          
          }
        else
          {
          set_cfg_widget(w, name, type, "active-id", NULL, help_string, translation_domain);
          }
        
        num = gavl_parameter_num_options(param);
        
        for(i = 0; i < num; i++)
          {
          const char * name;
          const char * label;
          const gavl_dictionary_t * opt = gavl_parameter_get_option(param, i);

          name = gavl_dictionary_get_string(opt, GAVL_META_NAME);
          label = gavl_dictionary_get_string(opt, GAVL_META_LABEL);
          if(!label)
            label = name;
          
          gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(w),
                                    name, label);
          }
        
        gtk_grid_attach(GTK_GRID(grid), label, 0, ypos, 1, 1);

        if(type == GAVL_PARAMETER_STRINGLIST)
          gtk_grid_attach(GTK_GRID(grid), w, 1, ypos, 2, 1);
        else
          {
          GtkWidget * button;
          button = bg_gtk_create_icon_button(BG_ICON_CONFIG);
          g_signal_connect(G_OBJECT(button), "clicked", G_CALLBACK(multimenu_config_callback), w);
          gtk_grid_attach(GTK_GRID(grid), w, 1, ypos, 1, 1);
          gtk_grid_attach(GTK_GRID(grid), button, 2, ypos, 1, 1);
          }
        
        ypos++;
        
        }
        break;
      case GAVL_PARAMETER_DIRLIST:
      case GAVL_PARAMETER_MULTI_LIST:
      case GAVL_PARAMETER_MULTI_CHAIN:
        {
        GtkWidget * label;
        GtkWidget * button;
        GtkWidget * box;
        if(type == GAVL_PARAMETER_DIRLIST)
          w = bg_gtk_simple_list_create(0);
        else
          w = bg_gtk_simple_list_create(1);

        set_cfg_widget(w, name, type, NULL, NULL, help_string, translation_domain);
        
        label = gtk_label_new(long_name);
        gtk_widget_set_halign(label, GTK_ALIGN_START);

        gtk_grid_attach(GTK_GRID(grid), label, 0, ypos, 2, 1);
        ypos++;
        
        gtk_grid_attach(GTK_GRID(grid), w, 0, ypos, 2, 3);
        box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

        if(type == GAVL_PARAMETER_DIRLIST)
          {
          /* Buttons */
          button = bg_gtk_create_icon_button(BG_ICON_ADD);
          g_signal_connect(G_OBJECT(button), "clicked", G_CALLBACK(dirlist_add_callback), w);
          bg_gtk_box_pack_start(box, button, 0);
          button = bg_gtk_create_icon_button(BG_ICON_TRASH);
          g_signal_connect(G_OBJECT(button), "clicked", G_CALLBACK(dirlist_delete_callback), w);
          bg_gtk_box_pack_start(box, button, 0);
          }
        else if(type == GAVL_PARAMETER_MULTI_LIST)
          {
          //          const gavl_dictionary_t * item;
          //          int num_items;
          //          int i;
          //          gavl_dictionary_t * dict;
          //          gavl_array_t * arr;
          //          const char * name;
          
          /* Buttons */
          button = bg_gtk_create_icon_button(BG_ICON_CONFIG);
          g_signal_connect(G_OBJECT(button), "clicked", G_CALLBACK(multilist_config_callback), w);
          bg_gtk_box_pack_start(box, button, 0);
          
          }
        else if(type == GAVL_PARAMETER_MULTI_CHAIN)
          {
          /* Buttons */
          button = bg_gtk_create_icon_button(BG_ICON_ADD);
          g_signal_connect(G_OBJECT(button), "clicked", G_CALLBACK(multichain_add_callback), w);
          bg_gtk_box_pack_start(box, button, 0);
          
          button = bg_gtk_create_icon_button(BG_ICON_CONFIG);
          g_signal_connect(G_OBJECT(button), "clicked", G_CALLBACK(multilist_config_callback), w);
          bg_gtk_box_pack_start(box, button, 0);

          button = bg_gtk_create_icon_button(BG_ICON_TRASH);
          g_signal_connect(G_OBJECT(button), "clicked", G_CALLBACK(multichain_delete_callback), w);
          bg_gtk_box_pack_start(box, button, 0);
          
          /* Setup value */
          //          gavl_value_set_array(&wid->val);
          }
        
        gtk_grid_attach(GTK_GRID(grid), box, 2, ypos, 1, 1);
        
        ypos++;
        }
        break;
      case GAVL_PARAMETER_POSITION:
        {
        GtkWidget * label;
        GtkWidget * spinbutton;
        GtkWidget * box;

        box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
        set_cfg_widget(box, name, type, NULL, NULL, help_string, translation_domain);
        
        label = gtk_label_new(long_name);
        gtk_widget_set_halign(label, GTK_ALIGN_START);
        
        gtk_grid_attach(GTK_GRID(grid), label, 0, ypos, 2, 1);
        ypos++;

        label = gtk_label_new(TR("X"));
        bg_gtk_box_pack_start(box, label, 0);
        
        spinbutton = gtk_spin_button_new_with_range(0.0, 1.0, 0.01);
        gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spinbutton), 3);
        gtk_widget_set_name(spinbutton, "X");

        g_signal_connect(spinbutton, "value-changed", G_CALLBACK(position_callback), box);
        bg_gtk_box_pack_start(box, spinbutton, 1);
        
        label = gtk_label_new(TR("Y"));
        bg_gtk_box_pack_start(box, label, 0);
        
        spinbutton = gtk_spin_button_new_with_range(0.0, 1.0, 0.01);
        gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spinbutton), 3);
        gtk_widget_set_name(spinbutton, "Y");
        g_signal_connect(spinbutton, "value-changed", G_CALLBACK(position_callback), box);
        bg_gtk_box_pack_start(box, spinbutton, 1);
        
        gtk_grid_attach(GTK_GRID(grid), box, 0, ypos, 3, 1);
        ypos++;
        }
        break;
      case GAVL_PARAMETER_TIME:
        {
        GtkWidget * label;
        GtkWidget * spinbutton;
        GtkWidget * box;

        label = gtk_label_new(long_name);
        gtk_widget_set_halign(label, GTK_ALIGN_START);
        
        gtk_grid_attach(GTK_GRID(grid), label, 0, ypos, 3, 1);
        ypos++;
        
        box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
        set_cfg_widget(box, name, type, NULL, NULL, help_string, translation_domain);

        
        label = gtk_label_new(TR("H"));
        bg_gtk_box_pack_start(box, label, 0);
        
        spinbutton = gtk_spin_button_new_with_range(0.0, 1000.0, 1.0);
        gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spinbutton), 0);
        gtk_widget_set_name(spinbutton, "H");
        g_signal_connect(spinbutton, "value-changed", G_CALLBACK(time_callback), box);
        bg_gtk_box_pack_start(box, spinbutton, 1);
        
        label = gtk_label_new(TR("M"));
        bg_gtk_box_pack_start(box, label, 0);
        spinbutton = gtk_spin_button_new_with_range(0.0, 59.0, 1.0);
        gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spinbutton), 0);
        gtk_widget_set_name(spinbutton, "M");
        g_signal_connect(spinbutton, "value-changed", G_CALLBACK(time_callback), box);
        bg_gtk_box_pack_start(box, spinbutton, 1);

        label = gtk_label_new(TR("S"));
        bg_gtk_box_pack_start(box, label, 0);
        spinbutton = gtk_spin_button_new_with_range(0.0, 59.0, 1.0);
        gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spinbutton), 0);
        gtk_widget_set_name(spinbutton, "S");
        g_signal_connect(spinbutton, "value-changed", G_CALLBACK(time_callback), box);
        bg_gtk_box_pack_start(box, spinbutton, 1);

        label = gtk_label_new(TR("MS"));
        bg_gtk_box_pack_start(box, label, 0);
        spinbutton = gtk_spin_button_new_with_range(0.0, 999.0, 1.0);
        gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spinbutton), 0);
        gtk_widget_set_name(spinbutton, "MS");
        g_signal_connect(spinbutton, "value-changed", G_CALLBACK(time_callback), box);
        bg_gtk_box_pack_start(box, spinbutton, 1);

        gtk_grid_attach(GTK_GRID(grid), box, 0, ypos, 3, 1);
        ypos++;
        }
        break;
      case GAVL_PARAMETER_BUTTON:
        {
        w = gtk_button_new_with_label(TR_DOM(long_name));

        g_signal_connect(G_OBJECT(w), "clicked", G_CALLBACK(button_callback), s);
        
        set_cfg_widget(w, name, type, NULL, NULL, help_string, translation_domain);
        
        gtk_widget_set_hexpand(w, TRUE);
        gtk_grid_attach(GTK_GRID(grid), w, 0, ypos, 3, 1);
        ypos++;
        
        }
        
        break;
      case GAVL_PARAMETER_SECTION:
        break;
      }
    }
  gavl_dictionary_foreach(&s->cur, set_widget_callback, grid);
  gtk_widget_show_all(grid);
  }

static void add_to_stack(GtkWidget * treeview, GtkWidget * stack,
                         const gavl_dictionary_t * dict,
                         bg_cfg_ctx_t * ctx,
                         const char * translation_domain, GtkTreeIter * parent,
                         const char * subsection)
  {
  const char * label;
  char * name;
  
  GtkWidget * grid;
  GtkTreeIter iter;
  //  GtkTreeIter parent;
  
  GtkTreeModel * model = gtk_tree_view_get_model(GTK_TREE_VIEW(treeview));

  fprintf(stderr, "add_to_stack: %s\n", subsection);
  
  grid = gtk_grid_new();
  gtk_grid_set_row_spacing(GTK_GRID(grid), 5);
  gtk_grid_set_column_spacing(GTK_GRID(grid), 5);

  name = gavl_sprintf("%p", grid);
  add_config_widgets(grid, dict, ctx, translation_domain, subsection);
  gtk_stack_add_named(GTK_STACK(stack), grid, name);
  free(name);
  
  gtk_tree_store_append(GTK_TREE_STORE(model), &iter, parent);
  
  if(!(label = gavl_dictionary_get_string(dict, GAVL_META_LABEL)))
    label = ctx->long_name;
  
  gtk_tree_store_set(GTK_TREE_STORE(model), &iter, COLUMN_LABEL, label, COLUMN_CHILD, grid, -1);
  
  }

void bg_gtk_config_dialog_add_container(GtkWidget * dialog, const char * label,
                                        GtkTreeIter * parent, GtkTreeIter * ret)
  {
  GtkTreeModel * model;
  GtkWidget * treeview;

  if(!(treeview = get_treeview(dialog)))
    return; // Error

  model = gtk_tree_view_get_model(GTK_TREE_VIEW(treeview));

  gtk_tree_store_append(GTK_TREE_STORE(model), ret, parent);
  gtk_tree_store_set(GTK_TREE_STORE(model), ret, COLUMN_LABEL, label, COLUMN_CHILD, NULL, -1);
  
  }

void bg_gtk_config_dialog_add_section(GtkWidget * w,
                                      bg_cfg_ctx_t * ctx, GtkTreeIter * parent)
  {
  int num_sections;
  GtkWidget * treeview = NULL;
  GtkWidget * stack;
  GtkWidget * grid;
  const char * translation_domain;
  const char * gettext_directory;

  const gavl_dictionary_t * section;
  const gavl_dictionary_t * subsection;
  bg_cfg_ctx_finalize(ctx);
  
  translation_domain = gavl_dictionary_get_string(ctx->params, GAVL_PARAMETER_GETTEXT_DOMAIN);
  gettext_directory = gavl_dictionary_get_string(ctx->params, GAVL_PARAMETER_GETTEXT_DIRECTORY);

  if(gettext_directory)
    bg_bindtextdomain(translation_domain, gettext_directory);
  
  num_sections   = gavl_parameter_num_sections(ctx->params);
  
  section = ctx->params;
  
  if(num_sections == 1)
    section = gavl_parameter_get_section(section, 0);
  
  if(num_sections < 2)
    {
    /* Add single */
    if(!(treeview = get_treeview(w)))
      {
      GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(w));
      
      /* Add to main area */
      grid = gtk_grid_new();

      add_config_widgets(grid,
                         ctx->params,
                         ctx,
                         translation_domain, NULL);

      
      gtk_container_add(GTK_CONTAINER(content_area), grid);
      
      return;
      }

    stack = get_stack(w);
    
    add_to_stack(treeview, stack,
                 section, ctx, translation_domain, parent, NULL);
    
    }
  else
    {
    int i;
    
    /* Add tree */
    ensure_treeview(w);

    treeview = get_treeview(w);
    stack = get_stack(w);

    for(i = 0; i < num_sections; i++)
      {
      subsection = gavl_parameter_get_section(section, i);
      
      add_to_stack(treeview, stack,
                   subsection, ctx, translation_domain, parent,
                   gavl_dictionary_get_string(subsection, GAVL_META_NAME));
      
      }
    }
  }

static GtkWidget *
config_dialog_create(int flags, const char * title, GtkWidget * parent)
  {
  GtkWidget * ret;
  GtkDialogFlags dlgflags = GTK_DIALOG_MODAL;

  parent = bg_gtk_get_toplevel(parent);

  if(flags & BG_GTK_CONFIG_DIALOG_OK_CANCEL)
    ret = gtk_dialog_new_with_buttons(title,
                                      GTK_WINDOW(parent),
                                      dlgflags,
                                      TRS("Ok"),
                                      GTK_RESPONSE_OK,
                                      TRS("Cancel"),
                                      GTK_RESPONSE_CLOSE,
                                      NULL);
  else if(flags & BG_GTK_CONFIG_DIALOG_ADD_CLOSE)
    ret = gtk_dialog_new_with_buttons(title,
                                      GTK_WINDOW(parent),
                                      dlgflags,
                                      TRS("Add"),
                                      GTK_RESPONSE_APPLY,
                                      TRS("Close"),
                                      GTK_RESPONSE_CLOSE,
                                      NULL);
  else
    ret = gtk_dialog_new_with_buttons(title,
                                      GTK_WINDOW(parent),
                                      dlgflags,
                                      TRS("Apply"),
                                      GTK_RESPONSE_APPLY,
                                      TRS("Close"),
                                      GTK_RESPONSE_CLOSE,
                                      NULL);

  g_signal_connect(ret, "response", G_CALLBACK(response_callback), ret);
  return ret;
  }

GtkWidget *
bg_gtk_config_dialog_create_multi(int flags, const char * title, GtkWidget * parent)
  {
  GtkWidget * ret;
  ret = config_dialog_create(flags, title, parent);
  ensure_treeview(ret);
  return ret;
  }

GtkWidget *
bg_gtk_config_dialog_create_single(int flags, const char * title, GtkWidget * parent,
                                   bg_cfg_ctx_t * ctx)
  {
  GtkWidget * ret = config_dialog_create(flags, title, parent);
  bg_gtk_config_dialog_add_section(ret, ctx, NULL);
  return ret;
  }
