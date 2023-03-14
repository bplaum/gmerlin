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

#include "gtk_dialog.h"
#include <gmerlin/utils.h>
#include <gmerlin/iconfont.h>

#include <gui_gtk/multiinfo.h>
#include <gui_gtk/gtkutils.h>

typedef struct
  {
  GtkWidget * label;
  GtkWidget * combo;
  GtkWidget * config_button;
  GtkWidget * info_button;
  
  int selected;
  const char * translation_domain;
  
  gavl_value_t val;
  gavl_dictionary_t * dict;
  } multi_menu_t;

static void get_value(bg_gtk_widget_t * w)
  {
  multi_menu_t * priv;
  
  priv = w->priv;

  gavl_dictionary_free(priv->dict);
  gavl_dictionary_init(priv->dict);
  gavl_dictionary_copy(priv->dict, w->value.v.dictionary); 
  
  if(w->info->multi_names)
    {
    const char * name = bg_multi_menu_get_selected_name(&w->value);

    if(!name)
      gtk_combo_box_set_active(GTK_COMBO_BOX(priv->combo), 0);
    else
      {
      int i = 0;

      while(w->info->multi_names[i])
        {
        if(!strcmp(name, w->info->multi_names[i]))
          {
          gtk_combo_box_set_active(GTK_COMBO_BOX(priv->combo), i);
          //        fprintf(stderr, "Get value %d\n", i);
          break;
          }
        i++;
        }
      }
    
    }
  
  }

static void set_value(bg_gtk_widget_t * w)
  {
  multi_menu_t * priv;
  gavl_dictionary_t * dict;
  
  priv = w->priv;
  
  dict = gavl_value_set_dictionary(&w->value);
  gavl_dictionary_copy(dict, priv->dict); 
  
  //  fprintf(stderr, "Set value\n");
  //  gavl_dictionary_dump(bg_multi_menu_get_selected(&w->value), 2);
  //  fprintf(stderr, "\n");
  }

static void destroy(bg_gtk_widget_t * w)
  {
  multi_menu_t * priv = w->priv;
  gavl_value_free(&priv->val);
  free(priv);
  }

static void attach(void * priv, GtkWidget * table,
                   int * row)
  {
  GtkWidget * box;
  multi_menu_t * s = priv;
  
  box = bg_gtk_hbox_new(5);
  
  //  bg_gtk_table_attach_defaults(table, b->button,
  //                            0, 1, *row, *row+1);

  bg_gtk_table_attach(table, s->label, 0, 1, *row, *row+1, 0, 0);
  bg_gtk_table_attach(table, s->combo, 1, 2, *row, *row+1, 1, 0);
  
  bg_gtk_box_pack_start(box, s->config_button, 1);
  bg_gtk_box_pack_start(box, s->info_button, 1);
  gtk_widget_show(box);
  
  bg_gtk_table_attach(table, box, 2, 3, *row, *row+1, 0, 0);
  
  (*row)++;
  }

static const gtk_widget_funcs_t funcs =
  {
    .get_value = get_value,
    .set_value = set_value,
    .destroy =   destroy,
    .attach =    attach
  };

static void combo_box_change_callback(GtkWidget * wid, gpointer data)
  {
  bg_gtk_widget_t * w;
  multi_menu_t * priv;
  
  w = data;
  priv = w->priv;
  priv->selected = gtk_combo_box_get_active(GTK_COMBO_BOX(priv->combo));
  if(w->info->multi_parameters && w->info->multi_parameters[priv->selected])
    gtk_widget_set_sensitive(priv->config_button, 1);
  else
    gtk_widget_set_sensitive(priv->config_button, 0);

  bg_multi_menu_set_selected_name(&priv->val, w->info->multi_names[priv->selected]);
  
  if(w->info->flags & BG_PARAMETER_SYNC)
    bg_gtk_change_callback(NULL, w);
  }

static void set_param(void * data, const char * name, const gavl_value_t * val)
  {
  bg_gtk_widget_t * w;
  multi_menu_t * priv;
  bg_msg_sink_t * sink;
  
  w = data;
  priv = w->priv;

  
  if((sink = bg_dialog_get_sink(w->dialog)) && w->section->ctx)
    {
    if(!name)
      bg_msg_set_parameter_ctx_term(sink);
    else
      {
      gavl_msg_t * msg;
      msg = bg_msg_sink_get(sink);
      
      bg_msg_set_multi_parameter_ctx(msg, w->section->ctx, w->info->name,
                                     w->info->multi_names[priv->selected],
                                     name, val);
      bg_msg_sink_put(sink, msg);
      }
    }
  }

static void button_callback(GtkWidget * wid, gpointer data)
  {
  bg_gtk_widget_t * w;
  multi_menu_t * priv;
  bg_dialog_t * dialog;
  const char * label;
  
  w = data;
  priv = w->priv;

  if(wid == priv->info_button)
    {
    bg_gtk_multi_info_show(w->info, priv->selected,
                           priv->translation_domain, priv->info_button);
    }
  else if(wid == priv->config_button)
    {
    if(w->info->multi_labels && w->info->multi_labels[priv->selected])
      label = TRD(w->info->multi_labels[priv->selected], priv->translation_domain);
    else
      label = w->info->multi_names[priv->selected];
    
    dialog = bg_dialog_create(bg_multi_menu_get_selected_nc(&priv->val),
                              set_param,
                              data, w->info->multi_parameters[priv->selected], label);
    bg_dialog_show(dialog, priv->config_button);
    }
  }

void bg_gtk_create_multi_menu(bg_gtk_widget_t * w,
                              const char * translation_domain)
  {
  int i;
  multi_menu_t * priv = calloc(1, sizeof(*priv));

  priv->translation_domain = translation_domain;
  
  w->funcs = &funcs;
  w->priv = priv;

  bg_multi_menu_create(&priv->val, w->info);
  priv->dict = gavl_value_get_dictionary_nc(&priv->val);
  
  priv->config_button = bg_gtk_create_icon_button(BG_ICON_CONFIG);
  priv->info_button   = bg_gtk_create_icon_button(BG_ICON_INFO);
  
  g_signal_connect(G_OBJECT(priv->config_button), "clicked",
                   G_CALLBACK(button_callback), w);
  g_signal_connect(G_OBJECT(priv->info_button), "clicked",
                   G_CALLBACK(button_callback), w);
  
  gtk_widget_show(priv->config_button);
  gtk_widget_show(priv->info_button);

  priv->combo = bg_gtk_combo_box_new_text();

  
  if(w->info->help_string)
    {
    bg_gtk_tooltips_set_tip(priv->combo,
                            w->info->help_string, translation_domain);
    }
  
  i = 0;

  if(w->info->multi_names)
    {
    while(w->info->multi_names[i])
      {
      if(w->info->multi_labels && w->info->multi_labels[i])
        bg_gtk_combo_box_append_text(priv->combo,
                                  TR_DOM(w->info->multi_labels[i]));
      else
        bg_gtk_combo_box_append_text(priv->combo,
                                  w->info->multi_names[i]);
      i++;
      }
    w->callback_id =
      g_signal_connect(G_OBJECT(priv->combo),
                       "changed", G_CALLBACK(combo_box_change_callback),
                       (gpointer)w);
    w->callback_widget = priv->combo;
    }
  else
    {
    gtk_widget_set_sensitive(priv->config_button, 0);
    gtk_widget_set_sensitive(priv->info_button, 0);
    }
  
  gtk_widget_show(priv->combo);
  
  priv->label = gtk_label_new(TR_DOM(w->info->long_name));

  gtk_widget_set_halign(priv->label, GTK_ALIGN_START);
  gtk_widget_set_valign(priv->label, GTK_ALIGN_CENTER);

  gtk_widget_show(priv->label);
  }
