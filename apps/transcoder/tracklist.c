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



#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include <config.h>

#include <gavl/metatags.h>

#include <gmerlin/pluginregistry.h>
#include <gmerlin/utils.h>

#include <gmerlin/iconfont.h>
#include <gmerlin/bggavl.h>
#include <gmerlin/translation.h>
#include <gmerlin/log.h>
#define LOG_DOMAIN "tracklist"

#include <gui_gtk/gtkutils.h>
#include <gui_gtk/mdb.h>
#include <gui_gtk/configdialog.h>

#include <gmerlin/transcoder_track.h>
#include "tracklist.h"
#include "trackdialog.h"

void track_list_update(track_list_t * w);


#define cp_tracks_name "gmerlin_transcoder_tracks"

/* 0 means unset */

#define DND_GMERLIN_TRACKS    1
#define DND_TEXT_URI_LIST     2
#define DND_TEXT_PLAIN        3
#define DND_TRANSCODER_TRACKS 4

#define CTX_ENCODERS "enc"
#define CTX_MASSTAG  "masstag"
#define CTX_RENAME   "rename"

#define RENAME_MASK  "rename-mask"
#define OPEN_PATH    "open_path"

static GtkTargetEntry dnd_dst_entries[] =
  {
    { bg_gtk_atom_tracks_name,   0, DND_GMERLIN_TRACKS   },
    {"text/uri-list",            0, DND_TEXT_URI_LIST    },
    {"text/plain",               0, DND_TEXT_PLAIN       },
  };

static GtkTargetEntry copy_paste_entries[] =
  {
    { cp_tracks_name , 0, DND_TRANSCODER_TRACKS },
  };

static int is_urilist(char * target_name)
  {
  if(!strcmp(target_name, "text/uri-list") ||
     !strcmp(target_name, "text/plain"))
    return 1;
  
  return 0;
  }


enum
  {
    COLUMN_INDEX,
    COLUMN_TYPE,
    COLUMN_NAME,
    COLUMN_DURATION,
    NUM_COLUMNS
  };



/* Called when the selection changed */

static void select_row_callback(GtkTreeSelection * sel,
                                gpointer data)
  {
  GtkTreeIter iter;
  GtkTreeModel * model;
  GtkTreeSelection * selection;
  int num_tracks, i;
  bg_transcoder_track_t * track;
  
  track_list_t * w = data;

  
  model = gtk_tree_view_get_model(GTK_TREE_VIEW(w->treeview));
  
  selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(w->treeview));
  
  w->num_selected = 0;

  num_tracks = gavl_get_num_tracks(&w->t);
  
  if(!gtk_tree_model_get_iter_first(model, &iter) || !num_tracks)
    {
    gtk_widget_set_sensitive(w->config_button, 0);
    gtk_widget_set_sensitive(w->encoder_button, 0);
    w->selected_track = NULL;
    //    gtk_widget_set_sensitive(w->up_button, 0);
    //    gtk_widget_set_sensitive(w->down_button, 0);
    gtk_widget_set_sensitive(w->delete_button, 0);
    gtk_widget_set_sensitive(w->cut_button, 0);
    gtk_widget_set_sensitive(w->copy_button, 0);
    
    gtk_widget_set_sensitive(w->menu.selected_menu.move_up_item, 0);
    gtk_widget_set_sensitive(w->menu.selected_menu.move_down_item, 0);
    gtk_widget_set_sensitive(w->menu.selected_menu.configure_item, 0);
    gtk_widget_set_sensitive(w->menu.selected_menu.remove_item, 0);
    gtk_widget_set_sensitive(w->menu.selected_menu.encoder_item, 0);
    
    gtk_widget_set_sensitive(w->menu.edit_menu.cut_item, 0);
    gtk_widget_set_sensitive(w->menu.edit_menu.copy_item, 0);
    return;
    }
  
  for(i = 0; i < num_tracks; i++)
    {
    track = gavl_get_track_nc(&w->t, i);
    
    if(gtk_tree_selection_iter_is_selected(selection, &iter))
      {
      gavl_track_set_gui_state(track, GAVL_META_GUI_SELECTED, 1);
      w->selected_track = track;
      w->num_selected++;
      }
    else
      gavl_track_set_gui_state(track, GAVL_META_GUI_SELECTED, 0);
    
    if(!gtk_tree_model_iter_next(model, &iter))
      break;
    }
  
  if(w->num_selected == 1)
    {
    gtk_widget_set_sensitive(w->config_button, 1);
    gtk_widget_set_sensitive(w->encoder_button, 1);
    //    gtk_widget_set_sensitive(w->up_button, 1);
    //    gtk_widget_set_sensitive(w->down_button, 1);
    gtk_widget_set_sensitive(w->delete_button, 1);
    gtk_widget_set_sensitive(w->cut_button, 1);
    gtk_widget_set_sensitive(w->copy_button, 1);
    
    gtk_widget_set_sensitive(w->menu.selected_menu.move_up_item, 1);
    gtk_widget_set_sensitive(w->menu.selected_menu.move_down_item, 1);
    gtk_widget_set_sensitive(w->menu.selected_menu.configure_item, 1);
    gtk_widget_set_sensitive(w->menu.selected_menu.remove_item, 1);
    gtk_widget_set_sensitive(w->menu.selected_menu.encoder_item, 1);
    gtk_widget_set_sensitive(w->menu.selected_menu.mass_tag_item, 1);
    gtk_widget_set_sensitive(w->menu.selected_menu.split_at_chapters_item, 1);
    gtk_widget_set_sensitive(w->menu.selected_menu.auto_number_item, 1);
    gtk_widget_set_sensitive(w->menu.selected_menu.auto_rename_item, 1);

    gtk_widget_set_sensitive(w->menu.edit_menu.cut_item, 1);
    gtk_widget_set_sensitive(w->menu.edit_menu.copy_item, 1);
    
    
    }
  else if(w->num_selected == 0)
    {
    gtk_widget_set_sensitive(w->config_button, 0);
    gtk_widget_set_sensitive(w->encoder_button, 0);
    w->selected_track = NULL;
    //    gtk_widget_set_sensitive(w->up_button, 0);
    //    gtk_widget_set_sensitive(w->down_button, 0);
    gtk_widget_set_sensitive(w->delete_button, 0);
    gtk_widget_set_sensitive(w->cut_button, 0);
    gtk_widget_set_sensitive(w->copy_button, 0);

    gtk_widget_set_sensitive(w->menu.selected_menu.move_up_item, 0);
    gtk_widget_set_sensitive(w->menu.selected_menu.move_down_item, 0);
    gtk_widget_set_sensitive(w->menu.selected_menu.configure_item, 0);
    gtk_widget_set_sensitive(w->menu.selected_menu.remove_item, 0);
    gtk_widget_set_sensitive(w->menu.selected_menu.encoder_item, 0);
    gtk_widget_set_sensitive(w->menu.selected_menu.mass_tag_item, 0);
    gtk_widget_set_sensitive(w->menu.selected_menu.split_at_chapters_item, 0);
    gtk_widget_set_sensitive(w->menu.selected_menu.auto_number_item, 0);
    gtk_widget_set_sensitive(w->menu.selected_menu.auto_rename_item, 0);

    gtk_widget_set_sensitive(w->menu.edit_menu.cut_item, 0);
    gtk_widget_set_sensitive(w->menu.edit_menu.copy_item, 0);
    }
  else
    {
    gtk_widget_set_sensitive(w->config_button, 0);
    w->selected_track = NULL;
    gtk_widget_set_sensitive(w->encoder_button, 1);
    //    gtk_widget_set_sensitive(w->up_button, 1);
    //    gtk_widget_set_sensitive(w->down_button, 1);
    gtk_widget_set_sensitive(w->delete_button, 1);
    gtk_widget_set_sensitive(w->cut_button, 1);
    gtk_widget_set_sensitive(w->copy_button, 1);

    gtk_widget_set_sensitive(w->menu.selected_menu.move_up_item, 1);
    gtk_widget_set_sensitive(w->menu.selected_menu.move_down_item, 1);
    gtk_widget_set_sensitive(w->menu.selected_menu.configure_item, 0);
    gtk_widget_set_sensitive(w->menu.selected_menu.remove_item, 1);
    gtk_widget_set_sensitive(w->menu.selected_menu.encoder_item, 1);
    gtk_widget_set_sensitive(w->menu.selected_menu.mass_tag_item, 1);
    gtk_widget_set_sensitive(w->menu.selected_menu.split_at_chapters_item, 0);
    gtk_widget_set_sensitive(w->menu.selected_menu.auto_number_item, 1);
    gtk_widget_set_sensitive(w->menu.selected_menu.auto_rename_item, 1);
    
    gtk_widget_set_sensitive(w->menu.edit_menu.cut_item, 1);
    gtk_widget_set_sensitive(w->menu.edit_menu.copy_item, 1);
    }
  }

/* Update the entire list */

static void set_duration(track_list_t * w, gavl_time_t duration)
  {
  char * format = "<span font_family=\"%s\" weight=\"normal\">%s</span> %s";
  char * markup;
  char time_str[GAVL_TIME_STRING_LEN];
  gavl_time_prettyprint(duration, time_str);

  markup = g_markup_printf_escaped(format, BG_ICON_FONT_FAMILY, BG_ICON_CLOCK, time_str);

  gtk_label_set_markup(GTK_LABEL(w->time_total), markup);
  g_free(markup);
  }

void track_list_update(track_list_t * w)
  {
  int i;
  GtkTreeModel * model;
  GtkTreeIter iter;
  const bg_transcoder_track_t * track;
  gavl_time_t track_duration;
  int num_tracks;
  
  gavl_time_t duration_total;
  const char * name;
  
  char string_buffer[GAVL_TIME_STRING_LEN + 32];
  const char * type;
  
  GtkTreeSelection * selection;
   
  selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(w->treeview));

  model = gtk_tree_view_get_model(GTK_TREE_VIEW(w->treeview));

  g_signal_handler_block(G_OBJECT(selection), w->select_handler_id);
   
  gtk_list_store_clear(GTK_LIST_STORE(model));

  i = 0;

  num_tracks = gavl_get_num_tracks(&w->t);
  
  duration_total = 0;
  
  for(i = 0; i < num_tracks; i++)
    {
    track = gavl_get_track(&w->t, i);
    
    gtk_list_store_append(GTK_LIST_STORE(model), &iter);

    name = bg_transcoder_track_get_name(track);
    
    /* Set index */
    sprintf(string_buffer, "%d.", i+1);
    
    gtk_list_store_set(GTK_LIST_STORE(model),
                       &iter, COLUMN_INDEX,
                       string_buffer, -1);

    /* Set type */

    if((type = gavl_dictionary_get_string(gavl_track_get_metadata(track), GAVL_META_CLASS)) &&
       (type = bg_get_type_icon(type)))
      gtk_list_store_set(GTK_LIST_STORE(model),
                         &iter, COLUMN_TYPE,
                         type, -1);
    
    /* Set name */
    gtk_list_store_set(GTK_LIST_STORE(model),
                       &iter, COLUMN_NAME,
                       name, -1);
    
    /* Set time */
    
    track_duration = gavl_track_get_duration(track);
    
    if(duration_total != GAVL_TIME_UNDEFINED)
      {
      if(track_duration == GAVL_TIME_UNDEFINED)
        duration_total = GAVL_TIME_UNDEFINED;
      else
        duration_total += track_duration;
      }

    if(track_duration)
      {
      gavl_time_prettyprint(track_duration, string_buffer);
      gtk_list_store_set(GTK_LIST_STORE(model),
                         &iter,
                         COLUMN_DURATION,
                         string_buffer, -1);
      }
    
    /* Select track */

    if(gavl_track_get_gui_state(track, GAVL_META_GUI_SELECTED))
      gtk_tree_selection_select_iter(selection, &iter);
    }
  g_signal_handler_unblock(G_OBJECT(selection), w->select_handler_id);

  set_duration(w, duration_total);

  select_row_callback(NULL, w);
  
  }

/* Callback functions for the clipboard */

static void clipboard_get_func(GtkClipboard *clipboard,
                               GtkSelectionData *selection_data,
                               guint info,
                               gpointer data)
  {
  GdkAtom type_atom;
  track_list_t * w = data;
  
  type_atom = gdk_atom_intern("STRING", FALSE);
  if(!type_atom)
    return;
  
  gtk_selection_data_set(selection_data, type_atom, 8, (uint8_t*)w->clipboard,
                         strlen(w->clipboard)+1);
  }

static void clipboard_clear_func(GtkClipboard *clipboard,
                                 gpointer data)
  {
  track_list_t * w = data;
  if(w->clipboard)
    {
    free(w->clipboard);
    w->clipboard = NULL;
    }
  }

static void
clipboard_received_func_tracks(GtkClipboard *clipboard,
                               GtkSelectionData *selection_data,
                               gpointer data)
  {
  char * text;
  gavl_dictionary_t dict;
  track_list_t * w = data;
  
  text = (char*)gtk_selection_data_get_text(selection_data);
  
  if(!text || strlen((char*)text) <= 0)
    return;
  
  gavl_dictionary_init(&dict);
  
  if(bg_dictionary_load_xml_string(&dict, text, -1, BG_TRANSCODER_TRACK_XML_ROOT))
    {
    
    const gavl_array_t * new_children;

    if((new_children = gavl_get_tracks_nc(&dict)) &&  new_children->num_entries)
      {
      int old_num;
      gavl_array_t * children =  gavl_get_tracks_nc(&w->t);
      old_num = children->num_entries;
      
      /* Clear old selection, select all pasted tracks */

      gavl_tracks_set_gui_state(children, GAVL_META_GUI_SELECTED, 0, 0, -1);
      
      gavl_array_splice_array(children, -1, 0, new_children);
      gavl_tracks_set_gui_state(children, GAVL_META_GUI_SELECTED, 1, old_num, -1);
      
      track_list_update(w);  
      }
    }
  
  gavl_dictionary_free(&dict);
  g_free(text);
  }

static void
clipboard_received_func_gmerlin_tracks(GtkClipboard *clipboard,
                                       GtkSelectionData *selection_data,
                                       gpointer data)
  {
  gavl_dictionary_t dict;
  const gavl_array_t * gmerlin_tracks;
  gavl_array_t * new_tracks;
  //  const char * start;
  //  const char * end;
  track_list_t * l = data;
  
  gavl_dictionary_init(&dict);
  
  bg_tracks_from_string(&dict, BG_TRACK_FORMAT_GMERLIN,
                        (const char*)gtk_selection_data_get_data(selection_data),
                        gtk_selection_data_get_length(selection_data));

  gmerlin_tracks = gavl_get_tracks(&dict);

  new_tracks = bg_transcoder_tracks_import(gmerlin_tracks,
                                           l->track_defaults_section,
                                           l->encoder_section);
    
  if(new_tracks)
    {
    gavl_array_splice_array(gavl_get_tracks_nc(&l->t), -1, 0, new_tracks);
    gavl_array_destroy(new_tracks);
    }
    
  /* Cleanup */
  gavl_dictionary_free(&dict);
  track_list_update(l);  
  
  //  g_free(text);
  
  }

static void do_copy(track_list_t * w)
  {
  gavl_dictionary_t sel;
  gavl_array_t * arr = NULL;
  GtkClipboard *clipboard;
  
  gavl_dictionary_init(&sel);
  
  arr = bg_dictionary_extract_children_by_flag(&w->t, GAVL_META_GUI_SELECTED);
  
  gavl_array_splice_array_nocopy(gavl_get_tracks_nc(&sel), -1, 0, arr);
  
  clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
  
  gtk_clipboard_set_with_data(clipboard,
                              copy_paste_entries,
                              sizeof(copy_paste_entries)/
                              sizeof(copy_paste_entries[0]),
                              clipboard_get_func,
                              clipboard_clear_func,
                              (gpointer)w);
  
  if(w->clipboard)
    free(w->clipboard);
  
  w->clipboard = bg_dictionary_save_xml_string(&sel, BG_TRANSCODER_TRACK_XML_ROOT);
  gavl_dictionary_free(&sel);

  if(arr)
    gavl_array_destroy(arr);
  }

static void do_cut(track_list_t * l)
  {
  do_copy(l);
  bg_dictionary_delete_children_by_flag_nc(&l->t, GAVL_META_GUI_SELECTED);
  }

static void target_received_func(GtkClipboard *clipboard,
                                 GdkAtom *atoms,
                                 gint n_atoms,
                                 gpointer data)
  {
  track_list_t * l;
  int i = 0;
  char * atom_name;
  l = data;
  
  for(i = 0; i < n_atoms; i++)
    {
    atom_name = gdk_atom_name(atoms[i]);
    
    if(!atom_name)
      return;
    else if(!strcmp(atom_name, cp_tracks_name))
      {
      gtk_clipboard_request_contents(clipboard,
                                     atoms[i],
                                     clipboard_received_func_tracks,
                                     l);
      g_free(atom_name);
      return;
      }
    else if(!strcmp(atom_name, bg_gtk_atom_tracks_name))
      {
      gtk_clipboard_request_contents(clipboard,
                                     atoms[i],
                                     clipboard_received_func_gmerlin_tracks,
                                     l);
      g_free(atom_name);
      return;
      }
    else
      g_free(atom_name);
    }
  }

static void do_paste(track_list_t * l)
  {
  GtkClipboard *clipboard;
  
  clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
  
  gdk_atom_intern(cp_tracks_name, FALSE);
  
  gtk_clipboard_request_targets(clipboard,
                                target_received_func, l);  
  }

static void move_up(track_list_t * l)
  {
  gavl_array_t * arr;
  
  arr = bg_dictionary_extract_children_by_flag(&l->t, GAVL_META_GUI_SELECTED);
  bg_dictionary_delete_children_by_flag_nc(&l->t, GAVL_META_GUI_SELECTED);
  gavl_array_splice_array_nocopy(gavl_get_tracks_nc(&l->t), 0, 0, arr);
  gavl_array_destroy(arr);
  
  track_list_update(l);
  }

static void move_down(track_list_t * l)
  {
  gavl_array_t * arr;
  
  arr = bg_dictionary_extract_children_by_flag(&l->t, GAVL_META_GUI_SELECTED);
  bg_dictionary_delete_children_by_flag_nc(&l->t, GAVL_META_GUI_SELECTED);
  gavl_array_splice_array_nocopy(gavl_get_tracks_nc(&l->t), -1, 0, arr);
  gavl_array_destroy(arr);
  
  track_list_update(l);
  }

static void add_files(track_list_t * l, const gavl_array_t * files)
  {
  gavl_array_t * new_tracks;
  int i = 0;

  for(i = 0; i < files->num_entries; i++)
    {
    new_tracks =
      bg_transcoder_track_create(gavl_string_array_get(files, i),
                                 l->track_defaults_section,
                                 l->encoder_section);
    
    if(new_tracks)
      gavl_array_splice_array_nocopy(gavl_get_tracks_nc(&l->t), -1, 0, new_tracks);

    track_list_update(l);
    
    if(new_tracks)
      gavl_array_destroy(new_tracks);
    }
  
  
  }

static int handle_dlg_message(void * data, gavl_msg_t * msg)
  {
  track_list_t * l = data;

  if(l->selected_track && (track_dialog_handle_message(l, msg)))
    return 1;
  
  switch(msg->NS)
    {
    case BG_MSG_NS_PARAMETER:
      switch(msg->ID)
        {
        case BG_CMD_SET_PARAMETER:
          {
          const char * name;
          const char * ctx;
          
          gavl_value_t val;
          gavl_value_init(&val);
          bg_msg_get_parameter(msg, &name, &val);
          ctx = gavl_dictionary_get_string(&msg->header, GAVL_MSG_CONTEXT_ID);

#if 1
          fprintf(stderr, "BG_MSG_SET_PARAMETER\n");
          fprintf(stderr, "  ctx:  %s\n", ctx);
          fprintf(stderr, "  name: %s\n", name);
          gavl_value_dump(&val, 2);
          fprintf(stderr, "\n");
#endif

          if(!ctx)
            return 1;

          if(!strcmp(ctx, CTX_MASSTAG) && name)
            {

            int j, num;
            gavl_dictionary_t * m;
            gavl_dictionary_t * track;
              
            if(val.type == GAVL_TYPE_STRING)
              {
              /* Don't touch the field if empty */
              if(!val.v.str || (*val.v.str == '\0'))
                {
                return 1;
                }
              /* Delete field if field was "-" */
              else if(!strcmp(val.v.str, "-"))
                gavl_value_reset(&val);
              }
            
            num = gavl_get_num_tracks(&l->t);
            for(j = 0; j < num; j++)
              {
              if((track = gavl_get_track_nc(&l->t, j)) &&
                 gavl_track_get_gui_state(track, GAVL_META_GUI_SELECTED))
                {
                m = gavl_track_get_metadata_nc(track);
                bg_metadata_set_parameter(m, name, &val);
                }
              }
            }
          else if(!strcmp(ctx, CTX_RENAME) && name)
            {
            gavl_dictionary_t * track;
            int i, num;
            char * new_name;
    
            num = gavl_get_num_tracks(&l->t);
            for(i = 0; i < num; i++)
              {
              if((track = gavl_get_track_nc(&l->t, i)) &&
                 gavl_track_get_gui_state(track, GAVL_META_GUI_SELECTED))
                {
                gavl_dictionary_t * m = gavl_track_get_metadata_nc(track);
                if((new_name = bg_create_track_name(m, val.v.str)))
                  gavl_dictionary_set_string_nocopy(m, GAVL_META_LABEL, new_name);
                }
              }
            
            /* Save for later use */
            gavl_dictionary_set(bg_cfg_registry_find_section(bg_cfg_registry, "track_list"),
                                RENAME_MASK, &val);
            
            track_list_update(l);
            }
          
          if(!name)
            {
            if(!strcmp(ctx, CTX_ENCODERS))
              {
              int i, num;
              gavl_dictionary_t * track;
              num = gavl_get_num_tracks(&l->t);
              for(i = 0; i < num; i++)
                {
                if((track = gavl_get_track_nc(&l->t, i)) &&
                   gavl_track_get_gui_state(track, GAVL_META_GUI_SELECTED))
                  {
                  bg_transcoder_track_set_encoders(track, &l->dlg_section);
                  }
                }
              }
            }
          else
            {
            gavl_dictionary_set(&l->dlg_section, name, &val);
            }
          gavl_value_free(&val);
          }
        }
      break;
    case BG_MSG_NS_DIALOG:
      {
      switch(msg->ID)
        {
        case BG_MSG_DIALOG_ADD_LOCATIONS:
          {
          gavl_array_t arr;
          gavl_array_init(&arr);
          gavl_msg_get_arg_array(msg, 0, &arr);
          add_files(l, &arr);
          gavl_array_free(&arr);
          }
          break;
        }
      }
    }
  return 1;
  }


static const gavl_dictionary_t * get_first_selected(track_list_t * l)
  {
  int num, i;
  const gavl_dictionary_t * track = NULL;

  num = gavl_get_num_tracks(&l->t);
  for(i = 0; i < num; i++)
    {
    if((track = gavl_get_track(&l->t, i)) &&
       gavl_track_get_gui_state(track, GAVL_META_GUI_SELECTED))
      {
      return track;
      break;
      }
    }
  return NULL;
  }

static void configure_encoders(track_list_t * l)
  {
  GtkWidget * dlg;
  bg_cfg_ctx_t ctx;
  
  const gavl_dictionary_t * first_selected = NULL;
  
  if(!(first_selected = get_first_selected(l)))
    {
    gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "configure_encoders: No tracks selected");
    return;
    }

  bg_cfg_ctx_init(&ctx, l->encoder_parameters, CTX_ENCODERS,
                  TR("Encoders"), NULL, NULL);
  
  ctx.s_priv = bg_cfg_section_create("Encoders");
  ctx.s = ctx.s_priv;
  
  /* All settings, which are not in the track, are taken from the
     current encoder settings */

  gavl_dictionary_copy(ctx.s, l->encoder_section);
  bg_transcoder_track_get_encoders(first_selected, ctx.s);
  
  ctx.sink = l->dlg_sink;

  //  fprintf(stderr, "Encoders:\n");
  //  gavl_dictionary_dump(ctx.s, 2);
  /* Store the encoders also locally because we receive only the changed ones
     from the dialog */
  gavl_dictionary_reset(&l->dlg_section);
  gavl_dictionary_copy(&l->dlg_section, ctx.s);
  
  dlg = bg_gtk_config_dialog_create_single(BG_GTK_CONFIG_DIALOG_OK_CANCEL,
                                           TR("Encoders"),
                                           l->widget,
                                           &ctx);
  gtk_window_present(GTK_WINDOW(dlg));
  bg_cfg_ctx_free(&ctx);
  }

static void mass_tag(track_list_t * l)
  {
  bg_cfg_ctx_t ctx;
  GtkWidget * dlg;
  const bg_transcoder_track_t * first_selected;

  bg_parameter_info_t * params;
  gavl_dictionary_t * s;
  int i;

  if(!(first_selected = get_first_selected(l)))
    return;
  
  params = bg_metadata_get_parameters_common(NULL);
  s = bg_cfg_section_create_from_parameters("Mass tag", params);

  /* Copy parameters from the first selected track. Also set the
     help string. */
  i = 0;
  while(params[i].name)
    {
    gavl_dictionary_set(s, params[i].name, gavl_dictionary_get(gavl_track_get_metadata(first_selected), params[i].name));

    if(!params[i].help_string)
      params[i].help_string = gavl_strrep(params[i].help_string,
                                      TRS("Use \"-\" to clear this field for all tracks. Empty string means to leave it unchanged for all tracks"));
    else
      params[i].help_string = gavl_strcat(params[i].help_string,
                                      TRS(" Use \"-\" to clear this field for all tracks. Empty string means to leave it unchanged for all tracks"));
    
    i++;
    }

  
  bg_cfg_ctx_init(&ctx, params, CTX_MASSTAG,
                  TR("Mass tag"), NULL, NULL);

  ctx.sink = l->dlg_sink;
  ctx.s = s;
  
  dlg = bg_gtk_config_dialog_create_single(BG_GTK_CONFIG_DIALOG_OK_CANCEL,
                                           TR("Mass tag"),
                                           l->widget,
                                           &ctx);
  gtk_window_present(GTK_WINDOW(dlg));
  bg_cfg_ctx_free(&ctx);

  bg_parameter_info_destroy_array(params);
  }

static void split_at_chapters(track_list_t * w)
  {
  int i;
  int num_added;
  gavl_array_t * tracks;
  const gavl_dictionary_t * track;
  
  //  fprintf(stderr, "split_at_chapters\n");
  
  tracks = gavl_get_tracks_nc(&w->t);


  i = 0;
  
  while(i < tracks->num_entries)
    {
    if((track = gavl_value_get_dictionary(&tracks->entries[i])) &&
       gavl_track_get_gui_state(track, GAVL_META_GUI_SELECTED))
      {
      gavl_array_t new_tracks;
      gavl_array_init(&new_tracks);

      bg_transcoder_track_split_at_chapters(&new_tracks, track);

      if(new_tracks.num_entries > 0)
        {
        num_added = new_tracks.num_entries;
        gavl_array_splice_array_nocopy(tracks, i, 1, &new_tracks);
        i += num_added;
        }
      else
        i++;
      
      gavl_array_free(&new_tracks);
      }
    else
      i++;
    }
  track_list_update(w);  
  }

static const bg_parameter_info_t auto_rename_parameters[] =
  {
    {
      .name =        RENAME_MASK,
      .long_name =   TRS("Format for track names"),
      .type =        BG_PARAMETER_STRING,
      .help_string = TRS("Format specifier for tracknames from\n\
metadata\n\
%p:    Artist\n\
%a:    Album\n\
%g:    Genre\n\
%t:    Track name\n\
%<d>n: Track number with <d> digits\n\
%y:    Year\n\
%c:    Comment"),
      
    },
    { /* End */ },
  };


static void auto_rename(track_list_t * l)
  {
  bg_parameter_info_t * auto_rename_params;
  GtkWidget * dlg;
  bg_cfg_ctx_t ctx;
  const char * str;
  
  auto_rename_params =
    bg_parameter_info_copy_array(auto_rename_parameters);

  if((str = gavl_dictionary_get_string(bg_cfg_registry_find_section(bg_cfg_registry, "track_list"),
                                       RENAME_MASK)))
    gavl_value_set_string(&auto_rename_params[0].val_default,
                          str);
  else
    gavl_value_set_string(&auto_rename_params[0].val_default,
                          "%2n %p - %t");
  
  bg_cfg_ctx_init(&ctx, auto_rename_params, CTX_RENAME,
                  TR("Auto rename"), NULL, NULL);
  
  ctx.sink = l->dlg_sink;
  
  dlg = bg_gtk_config_dialog_create_single(BG_GTK_CONFIG_DIALOG_OK_CANCEL,
                                           TR("Auto rename"),
                                           l->widget,
                                           &ctx);
  gtk_window_present(GTK_WINDOW(dlg));
  bg_cfg_ctx_free(&ctx);
  
  bg_parameter_info_destroy_array(auto_rename_params);
  }


static void auto_number(track_list_t * l)
  {
  bg_transcoder_track_t * track;
  int idx = 1;
  int i, num;

  num = gavl_get_num_tracks(&l->t);
  for(i = 0; i < num; i++)
    {
    if((track = gavl_get_track_nc(&l->t, i)) &&
       gavl_track_get_gui_state(track, GAVL_META_GUI_SELECTED))
      {
      bg_cfg_section_set_parameter_int(gavl_track_get_metadata_nc(track), GAVL_META_TRACKNUMBER, idx);
      idx++;
      }
    }
  }


static void button_callback(GtkWidget * w, gpointer data)
  {
  track_list_t * t = data;

  if((w == t->add_file_button) || (w == t->menu.add_menu.add_files_item))
    {
    bg_gtk_load_media_files(TR("Add Files"), NULL,
                            t->add_file_button, t->dlg_sink);
    }
  else if((w == t->add_url_button) || (w == t->menu.add_menu.add_urls_item))
    {
    bg_gtk_urlsel_show(TR("Add URLs"),
                       t->dlg_sink, 
                       bg_gtk_get_toplevel(t->add_url_button));
    }
  else if((w == t->add_drives_button) || (w == t->menu.add_menu.add_drives_item))
    {
    bg_gtk_drivesel_show("Add drive",
                         t->dlg_sink,
                         t->add_drives_button);
    }
  else if((w == t->delete_button) || (w == t->menu.selected_menu.remove_item))
    {
    bg_dictionary_delete_children_by_flag_nc(&t->t, GAVL_META_GUI_SELECTED);
    track_list_update(t);
    }
  else if(w == t->menu.selected_menu.move_up_item)
    {
    move_up(t);
    }
  else if(w == t->menu.selected_menu.move_down_item)
    {
    move_down(t);
    }
  else if((w == t->config_button) || (w == t->menu.selected_menu.configure_item))
    {
    GtkWidget * dlg;
    
    dlg = track_dialog_create(t);
    gtk_window_present(GTK_WINDOW(dlg));
    
    }
  else if((w == t->menu.edit_menu.cut_item) || (w == t->cut_button))
    {
    do_cut(t);
    }
  else if((w == t->menu.edit_menu.copy_item) || (w == t->copy_button))
    {
    do_copy(t);
    }
  else if((w == t->menu.edit_menu.paste_item) || (w == t->paste_button))
    {
    do_paste(t);
    }
  else if(w == t->menu.selected_menu.mass_tag_item)
    {
    mass_tag(t);
    }
  else if(w == t->menu.selected_menu.split_at_chapters_item)
    {
    split_at_chapters(t);
    }
  else if(w == t->menu.selected_menu.auto_number_item)
    {
    auto_number(t);
    }
  else if(w == t->menu.selected_menu.auto_rename_item)
    {
    auto_rename(t);
    }
  
  else if((w == t->encoder_button) ||
          (w == t->menu.selected_menu.encoder_item))
    {
    configure_encoders(t);
    }
  }

/* Menu stuff */

static GtkWidget *
create_item(track_list_t * w, GtkWidget * parent,
            const char * label, const char * pixmap)
  {
  GtkWidget * ret;
  char * path = NULL;
  
  if(pixmap)
    path = bg_search_file_read("icons", pixmap);

  ret = bg_gtk_image_menu_item_new(label, path);
  
  if(path)
    free(path);

  g_signal_connect(G_OBJECT(ret), "activate", G_CALLBACK(button_callback),
                   (gpointer)w);
  gtk_widget_show(ret);
  gtk_menu_shell_append(GTK_MENU_SHELL(parent), ret);
  return ret;
  }

static GtkWidget *
create_icon_item(track_list_t * w, GtkWidget * parent,
                 const char * label, const char * icon)
  {
  GtkWidget * ret;

  ret = bg_gtk_icon_menu_item_new(label, icon);
  
  g_signal_connect(G_OBJECT(ret), "activate", G_CALLBACK(button_callback),
                   (gpointer)w);
  gtk_widget_show(ret);
  gtk_menu_shell_append(GTK_MENU_SHELL(parent), ret);
  return ret;
  }

static void init_menu(track_list_t * t)
  {
  /* Add */

  t->menu.add_menu.menu = gtk_menu_new();

  t->menu.add_menu.add_files_item =
    create_icon_item(t, t->menu.add_menu.menu, TR("Files..."), BG_ICON_FOLDER_OPEN);
  t->menu.add_menu.add_urls_item =
    create_icon_item(t, t->menu.add_menu.menu, TR("Urls..."), BG_ICON_GLOBE);
  t->menu.add_menu.add_drives_item =
    create_icon_item(t, t->menu.add_menu.menu, TR("Drives..."), BG_ICON_MUSIC_ALBUM);
  gtk_widget_show(t->menu.add_menu.menu);
  
  /* Selected */

  t->menu.selected_menu.menu = gtk_menu_new();

  t->menu.selected_menu.move_up_item =
    create_icon_item(t, t->menu.selected_menu.menu, TR("Move Up"), BG_ICON_CHEVRON2_UP);
  t->menu.selected_menu.move_down_item =
    create_icon_item(t, t->menu.selected_menu.menu, TR("Move Down"), BG_ICON_CHEVRON2_DOWN);
  t->menu.selected_menu.remove_item =
    create_icon_item(t, t->menu.selected_menu.menu, TR("Remove"), BG_ICON_TRASH);
  t->menu.selected_menu.configure_item =
    create_icon_item(t, t->menu.selected_menu.menu, TR("Configure..."), BG_ICON_CONFIG);
  t->menu.selected_menu.mass_tag_item =
    create_item(t, t->menu.selected_menu.menu, TR("Mass tag..."), NULL);
  t->menu.selected_menu.split_at_chapters_item =
    create_item(t, t->menu.selected_menu.menu, TR("Split at chapters"), NULL);
  t->menu.selected_menu.auto_number_item =
    create_item(t, t->menu.selected_menu.menu, TR("Auto numbering"), NULL);
  t->menu.selected_menu.auto_rename_item =
    create_item(t, t->menu.selected_menu.menu, TR("Auto rename..."), NULL);

  t->menu.selected_menu.encoder_item =
    create_icon_item(t, t->menu.selected_menu.menu, TR("Change encoders..."), BG_ICON_PLUGIN);

  /* Edit */

  t->menu.edit_menu.menu = gtk_menu_new();

  t->menu.edit_menu.cut_item =
    create_icon_item(t, t->menu.edit_menu.menu, TR("Cut"), BG_ICON_CUT);
  gtk_widget_add_accelerator(t->menu.edit_menu.cut_item, "activate", t->accel_group,
                             GDK_KEY_x, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
  
  t->menu.edit_menu.copy_item =
    create_icon_item(t, t->menu.edit_menu.menu, TR("Copy"), BG_ICON_COPY);
  gtk_widget_add_accelerator(t->menu.edit_menu.copy_item, "activate", t->accel_group,
                             GDK_KEY_c, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
  t->menu.edit_menu.paste_item =
    create_icon_item(t, t->menu.edit_menu.menu, TR("Paste"), BG_ICON_PASTE);
  gtk_widget_add_accelerator(t->menu.edit_menu.paste_item, "activate", t->accel_group,
                             GDK_KEY_v, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
  
  gtk_widget_show(t->menu.add_menu.menu);
  
  /* Root menu */

  t->menu.menu = gtk_menu_new();
  
  t->menu.add_item =
    create_item(t, t->menu.menu, TR("Add..."), NULL);
  
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(t->menu.add_item),
                            t->menu.add_menu.menu);

  t->menu.selected_item =
    create_item(t, t->menu.menu, TR("Selected..."), NULL);

  gtk_menu_item_set_submenu(GTK_MENU_ITEM(t->menu.selected_item),
                            t->menu.selected_menu.menu);

  t->menu.edit_item =
    create_item(t, t->menu.menu, TR("Edit..."), NULL);

  gtk_menu_item_set_submenu(GTK_MENU_ITEM(t->menu.edit_item),
                            t->menu.edit_menu.menu);
  

  gtk_widget_set_sensitive(t->menu.selected_menu.move_up_item, 0);
  gtk_widget_set_sensitive(t->menu.selected_menu.move_down_item, 0);
  gtk_widget_set_sensitive(t->menu.selected_menu.configure_item, 0);
  gtk_widget_set_sensitive(t->menu.selected_menu.remove_item, 0);
  gtk_widget_set_sensitive(t->menu.selected_menu.encoder_item, 0);
  gtk_widget_set_sensitive(t->menu.selected_menu.mass_tag_item, 0);
  gtk_widget_set_sensitive(t->menu.selected_menu.split_at_chapters_item, 0);
  gtk_widget_set_sensitive(t->menu.selected_menu.auto_number_item, 0);
  gtk_widget_set_sensitive(t->menu.selected_menu.auto_rename_item, 0);
  
  gtk_widget_set_sensitive(t->menu.edit_menu.cut_item, 0);
  gtk_widget_set_sensitive(t->menu.edit_menu.copy_item, 0);
  }

GtkWidget * track_list_get_menu(track_list_t * t)
  {
  return t->menu.menu;
  }

static gboolean button_press_callback(GtkWidget * w, GdkEventButton * evt,
                                      gpointer data)
  {
  track_list_t * t = data;

  if(evt->button == 3)
    {
    gtk_menu_popup_at_pointer(GTK_MENU(t->menu.menu), (GdkEvent*)evt);
    return TRUE;
    }
  return FALSE;
  }

/* */

void track_list_add_url(track_list_t * l, char * url)
  {
  gavl_array_t * new_tracks;

  new_tracks =
    bg_transcoder_track_create(url, l->track_defaults_section,
                               l->encoder_section);
  gavl_array_splice_array_nocopy(gavl_get_tracks_nc(&l->t), -1, 0, new_tracks);
  gavl_array_destroy(new_tracks);
  track_list_update(l);  
  }

static void drag_received_callback(GtkWidget *widget,
                                   GdkDragContext *drag_context,
                                   gint x,
                                   gint y,
                                   GtkSelectionData *data,
                                   guint info,
                                   guint time,
                                   gpointer d)
  {
  gavl_array_t * new_tracks = NULL;
  track_list_t * l = d;

  const guchar * data_buf;
  gint data_len;
  char * target_name;
  
  target_name = gdk_atom_name(gtk_selection_data_get_target(data));
  if(!target_name)
    return;
  
  data_buf = gtk_selection_data_get_data_with_length(data, &data_len);
  
  if(is_urilist(target_name))
    {
    new_tracks =
      bg_transcoder_track_create_from_urilist((char*)data_buf,
                                              data_len,
                                              l->track_defaults_section, l->encoder_section);
    

    gavl_array_splice_array_nocopy(gavl_get_tracks_nc(&l->t), -1, 0, new_tracks);

    if(new_tracks)
      {
      gavl_array_destroy(new_tracks);
      track_list_update(l);  
      }
    }
  else if(!strcmp(target_name, bg_gtk_atom_tracks_name))
    {
    gavl_dictionary_t dict;
    const gavl_array_t * gmerlin_tracks;
    
    gavl_dictionary_init(&dict);
    bg_tracks_from_string(&dict, BG_TRACK_FORMAT_GMERLIN, (char*)data_buf, data_len);
    
    gmerlin_tracks = gavl_get_tracks(&dict);

    new_tracks = bg_transcoder_tracks_import(gmerlin_tracks,
                                             l->track_defaults_section,
                                             l->encoder_section);
    
    if(new_tracks)
      {
      gavl_array_splice_array_nocopy(gavl_get_tracks_nc(&l->t), -1, 0, new_tracks);
      gavl_array_destroy(new_tracks);
      }
    
    /* Cleanup */
    gavl_dictionary_free(&dict);
    track_list_update(l);  
    }
  
  gtk_drag_finish(drag_context,
                  TRUE, /* Success */
                  0, /* Delete */
                  time);
  
  g_free(target_name);
  }

/* Buttons */

static GtkWidget * create_icon_button(track_list_t * l,
                                      const char * icon,
                                      const char * tooltip)
  {
  GtkWidget * ret;
  GtkWidget * label;

  char * markup = g_markup_printf_escaped("<span size=\"16000\" font_family=\"%s\" weight=\"normal\">%s</span>",
                                          BG_ICON_FONT_FAMILY, icon);
  label = gtk_label_new(NULL);
  gtk_label_set_markup(GTK_LABEL(label), markup);
  g_free(markup);

  gtk_widget_show(label);
  
  ret = gtk_button_new();
  gtk_container_add(GTK_CONTAINER(ret), label);
  
  g_signal_connect(G_OBJECT(ret), "clicked", G_CALLBACK(button_callback),
                   l);
  gtk_widget_show(ret);

  bg_gtk_tooltips_set_tip(ret, tooltip, PACKAGE);

  return ret;
  }


track_list_t * track_list_create(gavl_dictionary_t * track_defaults_section,
                                 const bg_parameter_info_t * encoder_parameters,
                                 gavl_dictionary_t * encoder_section)
  {
  GtkWidget * scrolled;
  GtkWidget * box;
  track_list_t * ret;

  GtkTreeViewColumn * col;
  GtkListStore *store;
  GtkCellRenderer *renderer;
  GtkTreeSelection * selection;
  char * tmp_path;
  
  ret = calloc(1, sizeof(*ret));

  ret->dlg_sink = bg_msg_sink_create(handle_dlg_message, ret, 1);
  
  ret->track_defaults_section = track_defaults_section;
  ret->encoder_section = encoder_section;
  ret->encoder_parameters = encoder_parameters;
  
  ret->time_total = gtk_label_new(NULL);
  gtk_widget_show(ret->time_total);
  
  ret->accel_group = gtk_accel_group_new();
    
  bg_gtk_tooltips_set_tip(ret->time_total,
                          TRS("Total playback time"),
                          PACKAGE);

  /* Create buttons */
  ret->add_file_button =
    create_icon_button(ret,
                         BG_ICON_FOLDER_OPEN,
                         TRS("Append files to the task list"));

  ret->add_url_button =
    create_icon_button(ret,
                       BG_ICON_GLOBE,
                       TRS("Append URLs to the task list"));

  ret->add_drives_button =
    create_icon_button(ret,
                       BG_ICON_MUSIC_ALBUM,
                       TRS("Append removable media to the task list"));
  
  ret->delete_button =
    create_icon_button(ret,
                       BG_ICON_TRASH,
                       TRS("Delete selected tracks"));

  ret->config_button =
    create_icon_button(ret,
                       BG_ICON_CONFIG,
                       TRS("Configure selected track"));

  ret->encoder_button =
    create_icon_button(ret,
                         BG_ICON_PLUGIN,
                         TRS("Change encoder plugins for selected tracks"));

  ret->copy_button =
    create_icon_button(ret,
                       BG_ICON_COPY,
                       TRS("Copy selected tracks to clipboard"));
  ret->cut_button =
    create_icon_button(ret,BG_ICON_CUT,
                       TRS("Cut selected tracks to clipboard"));

  ret->paste_button =
    create_icon_button(ret,
                       BG_ICON_PASTE,
                       TRS("Paste tracks from clipboard"));
 
  
  gtk_widget_set_sensitive(ret->delete_button, 0);
  gtk_widget_set_sensitive(ret->encoder_button, 0);
  gtk_widget_set_sensitive(ret->config_button, 0);
  //  gtk_widget_set_sensitive(ret->up_button, 0);
  //  gtk_widget_set_sensitive(ret->down_button, 0);
  gtk_widget_set_sensitive(ret->cut_button, 0);
  gtk_widget_set_sensitive(ret->copy_button, 0);
  
  /* Create list view */
  
  store = gtk_list_store_new(NUM_COLUMNS,
                             G_TYPE_STRING,   // Index
                             G_TYPE_STRING,   // Type
                             G_TYPE_STRING,   // Name
                             G_TYPE_STRING);  // Duration
  ret->treeview = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
  gtk_widget_set_size_request(ret->treeview, 200, 100);

  selection =
    gtk_tree_view_get_selection(GTK_TREE_VIEW(ret->treeview));
  gtk_tree_selection_set_mode(selection, GTK_SELECTION_MULTIPLE);

  ret->select_handler_id =
    g_signal_connect(G_OBJECT(selection), "changed",
                     G_CALLBACK(select_row_callback), (gpointer)ret);
  
  gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(ret->treeview), 0);
  
  gtk_drag_dest_set(ret->treeview,
                    GTK_DEST_DEFAULT_ALL,
                    /*
                      GTK_DEST_DEFAULT_HIGHLIGHT |
                      GTK_DEST_DEFAULT_DROP |
                      GTK_DEST_DEFAULT_MOTION,
                    */
                    dnd_dst_entries,
                    sizeof(dnd_dst_entries)/sizeof(dnd_dst_entries[0]),
                    GDK_ACTION_COPY | GDK_ACTION_MOVE);

  g_signal_connect(G_OBJECT(ret->treeview), "drag-data-received",
                   G_CALLBACK(drag_received_callback),
                   (gpointer)ret);
#if 0
  g_signal_connect(G_OBJECT(ret->treeview), "drag-drop",
                   G_CALLBACK(drag_drop_callback),
                   (gpointer)ret);
#endif
  g_signal_connect(G_OBJECT(ret->treeview), "button-press-event",
                   G_CALLBACK(button_press_callback), (gpointer)ret);
  
  
  /* Create columns */

  /* Index */
  
  renderer = gtk_cell_renderer_text_new();
  g_object_set(G_OBJECT(renderer), "xalign", 1.0, NULL);
  
  col = gtk_tree_view_column_new ();
  
  gtk_tree_view_column_set_title(col, "I");
  
  gtk_tree_view_column_pack_start(col, renderer, FALSE);
  
  gtk_tree_view_column_add_attribute(col, renderer, "text", COLUMN_INDEX);

  gtk_tree_view_column_set_sizing(col,
                                  GTK_TREE_VIEW_COLUMN_GROW_ONLY);
  gtk_tree_view_append_column (GTK_TREE_VIEW(ret->treeview), col);

  /* Type */

  renderer = gtk_cell_renderer_text_new();
  g_object_set(G_OBJECT(renderer), "family", BG_ICON_FONT_FAMILY, NULL);
  g_object_set(G_OBJECT(renderer), "xalign", 0.5, NULL);
  
  col = gtk_tree_view_column_new();
  gtk_tree_view_column_set_title(col, "T");
    
  gtk_tree_view_column_pack_start(col, renderer, FALSE);
  
  gtk_tree_view_column_add_attribute(col, renderer,
                                     "text", COLUMN_TYPE);
  
  gtk_tree_view_column_set_sizing(col, GTK_TREE_VIEW_COLUMN_GROW_ONLY);
  gtk_tree_view_append_column(GTK_TREE_VIEW(ret->treeview),
                              col);

  
  /* Name */
  
  renderer = gtk_cell_renderer_text_new();
  g_object_set(G_OBJECT(renderer), "xalign", 0.0, NULL);
  g_object_set(G_OBJECT(renderer), "ellipsize", PANGO_ELLIPSIZE_END, NULL);
  gtk_tree_view_column_set_title(col, "N");

  col = gtk_tree_view_column_new ();
  ret->col_name = col;
  
  gtk_tree_view_column_pack_start(col, renderer, TRUE);
  
  gtk_tree_view_column_add_attribute(col, renderer,
                                     "text", COLUMN_NAME);
  
  gtk_tree_view_column_set_sizing(col,
                                  GTK_TREE_VIEW_COLUMN_FIXED);
  gtk_tree_view_column_set_expand(col, TRUE);
  
  gtk_tree_view_append_column (GTK_TREE_VIEW(ret->treeview),
                               col);
    
  /* Duration */
  
  renderer = gtk_cell_renderer_text_new();
  g_object_set(G_OBJECT(renderer), "xalign", 1.0, NULL);
  
  col = gtk_tree_view_column_new ();
  
  gtk_tree_view_column_set_title(col, "D");
  
  gtk_tree_view_column_pack_start(col, renderer, FALSE);
  
  gtk_tree_view_column_add_attribute(col,
                                     renderer,
                                     "text", COLUMN_DURATION);
  
  gtk_tree_view_column_set_sizing(col,
                                  GTK_TREE_VIEW_COLUMN_GROW_ONLY);
  gtk_tree_view_append_column (GTK_TREE_VIEW(ret->treeview),
                               col);

  /* Done with columns */
  
  gtk_widget_show(ret->treeview);

  /* Pack */
  
  scrolled =
    gtk_scrolled_window_new(gtk_scrollable_get_hadjustment(GTK_SCROLLABLE(ret->treeview)),
                            gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(ret->treeview)));
  
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                 GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_container_add(GTK_CONTAINER(scrolled), ret->treeview);
  gtk_widget_show(scrolled);
    
  ret->widget = gtk_grid_new();
  bg_gtk_table_attach_defaults(ret->widget, scrolled, 0, 1, 0, 1);

  box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_box_pack_start(GTK_BOX(box), ret->add_file_button, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(box), ret->add_url_button, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(box), ret->add_drives_button, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(box), ret->delete_button, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(box), ret->encoder_button, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(box), ret->config_button, FALSE, FALSE, 0);
  //  gtk_box_pack_start(GTK_BOX(box), ret->up_button, FALSE, FALSE, 0);
  //  gtk_box_pack_start(GTK_BOX(box), ret->down_button, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(box), ret->cut_button, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(box), ret->copy_button, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(box), ret->paste_button, FALSE, FALSE, 0);
  
  gtk_box_pack_end(GTK_BOX(box), ret->time_total,
                   FALSE, FALSE, 0);
  gtk_widget_show(box);

  bg_gtk_table_attach(ret->widget,
                      box, 0, 1, 1, 2, 0, 0);
  
  gtk_widget_show(ret->widget);
  init_menu(ret);
  
  /* Load tracks */

  tmp_path = bg_search_file_read("transcoder", "tracklist.xml");
  
  if(tmp_path)
    {
    bg_dictionary_load_xml(&ret->t, tmp_path, BG_TRANSCODER_TRACK_XML_ROOT);
    free(tmp_path);
    track_list_update(ret);
    }
  return ret;
  }

void track_list_destroy(track_list_t * t)
  {
  char * tmp_path;

  tmp_path = bg_search_file_write("transcoder", "tracklist.xml");

  if(tmp_path)
    {
    bg_dictionary_save_xml(&t->t, tmp_path, BG_TRANSCODER_TRACK_XML_ROOT);
    free(tmp_path);
    }

  gavl_dictionary_free(&t->t);
  gavl_dictionary_free(&t->dlg_section);

  if(t->dlg_sink)
    bg_msg_sink_destroy(t->dlg_sink);
  
  g_object_unref(t->accel_group);

  if(t->clipboard)
    free(t->clipboard);

  free(t);
  }

GtkWidget * track_list_get_widget(track_list_t * t)
  {
  return t->widget;
  }

GtkAccelGroup * track_list_get_accel_group(track_list_t * t)
  {
  return t->accel_group;
  }



bg_transcoder_track_t * track_list_get_track(track_list_t * t)
  {
  bg_transcoder_track_t * ret;
  gavl_value_t val;
  gavl_value_init(&val);
  
  if(!gavl_array_shift(gavl_get_tracks_nc(&t->t), &val))
    return NULL;

  if((ret = gavl_value_get_dictionary_nc(&val)))
    {
    gavl_value_init(&val);
    track_list_update(t);
    }
  else
    gavl_value_free(&val);
  
  return ret;
  }

void track_list_prepend_track(track_list_t * t, const bg_transcoder_track_t * track)
  {
  gavl_dictionary_t * dict;
  gavl_value_t val;
  gavl_value_init(&val);
  
  dict = gavl_value_set_dictionary(&val);
  gavl_dictionary_copy(dict, track);
  
  gavl_array_splice_val_nocopy(gavl_get_tracks_nc(&t->t), 0, 0, &val);
  track_list_update(t);
  }

void track_list_load(track_list_t * t, const char * filename)
  {
  gavl_dictionary_t dict;
  gavl_dictionary_init(&dict);
  
  if(bg_dictionary_load_xml(&dict, filename, BG_TRANSCODER_TRACK_XML_ROOT))
    {
    const gavl_array_t * children;

    if((children = gavl_get_tracks(&dict)) && children->num_entries)
      {
      gavl_array_splice_array(gavl_get_tracks_nc(&t->t), -1, 0, children);
      track_list_update(t);  
      }
    }
  
  gavl_dictionary_free(&dict);
  }

void track_list_save(track_list_t * t, const char * filename)
  {
  bg_dictionary_save_xml(&t->t, filename, BG_TRANSCODER_TRACK_XML_ROOT);
  }


