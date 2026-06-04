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

/*
 * app.c - Application-level lifecycle callbacks.
 *
 * Porting notes (GTK-3 → GTK-4):
 *   gtk_widget_show_all() → gtk_widget_show()  (recursive show is gone in GTK-4)
 *   GtkBox packing: gtk_box_pack_start() → gtk_box_append()
 *   GtkHeaderBar: gtk_header_bar_set_show_close_button() →
 *                 gtk_header_bar_set_show_title_buttons()
 */

#include <config.h>

#include "app.h"
#include "mainwindow.h"

#include <gui_gtk/fileselect.h>
#include <gui_gtk/urlselect.h>
#include <gui_gtk/driveselect.h>
#include <gui_gtk/configdialog.h>

#include <gmerlin/application.h>

#include <gmerlin/pluginregistry.h>
#include <gmerlin/iconfont.h>
#include <gmerlin/bggavl.h>
#include <gmerlin/utils.h>
#include <gmerlin/translation.h>

#include <gavl/log.h>
#define LOG_DOMAIN "transcoder_app"

#define RENAME_MASK "rename-mask"
#define OPEN_PATH   "open_path"

static const char * multi_tags[] =
  {
    GAVL_META_ARTIST,
    GAVL_META_ALBUMARTIST,
    GAVL_META_AUTHOR,
    GAVL_META_GENRE,
    GAVL_META_COUNTRY,
    GAVL_META_DIRECTOR,
    GAVL_META_ACTOR,
    NULL,
  };

static void splice(app_data_t * ad, int idx, int del, gavl_array_t * add);


static int handle_transcoder_message(void * data, gavl_msg_t * msg)
  {
  app_data_t * ad = data;
  switch(msg->NS)
    {
    case GAVL_MSG_NS_GENERIC:

      switch(msg->ID)
        {
        case GAVL_MSG_QUIT:
          //          fprintf(stderr, "transcoding complete\n");
          transcoder_destroy(ad->t);
          ad->t = NULL;
          gavl_dictionary_reset(&ad->cur);
          gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ad->progress), 0.0);
          /* Check for next track */
          if(ad->tracks->num_entries)
            {
            gavl_dictionary_move(&ad->cur, gavl_value_get_dictionary_nc(&ad->tracks->entries[0]));
  
            if(!(ad->t = transcoder_create(&ad->cur, ad->transcoder_sink)))
              return 1;
            splice(ad, 0, 1, NULL);
            }
          else
            app_default_status(ad);
          break;
        case GAVL_MSG_PROGRESS:
          {
          double percentage;
          const char * str;

          percentage = gavl_msg_get_arg_float(msg, 0);
          str = gavl_msg_get_arg_string_c(msg, 1);

          gtk_label_set_text(GTK_LABEL(ad->status_left), str);
          gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ad->progress), percentage);
          }
          break;
        }
      break;
    }
  return 1;
  }

static int is_multi_tag(const char * tag)
  {
  int i = 0;
  while(multi_tags[i])
    {
    if(!strcmp(tag, multi_tags[i]))
      return 1;
    i++;
    }
  return 0;
  }

static void set_encoder_plugin(gavl_dictionary_t * track, const gavl_dictionary_t * plugin);


static int num_selected(app_data_t * ad)
  {
  int ret = 0;
  int i;
  
  for(i = 0; i < ad->tracks->num_entries; i++)
    {
    GtkListBoxRow * row = gtk_list_box_get_row_at_index(GTK_LIST_BOX(ad->listbox), i);
    
    if(gtk_list_box_row_is_selected(row))
      ret++;

    if(ret > 1)
      return ret;
    }
  return ret;
  }

static gavl_dictionary_t * get_first_selected(app_data_t * ad)
  {
  int i;
  
  for(i = 0; i < ad->tracks->num_entries; i++)
    {
    GtkListBoxRow * row = gtk_list_box_get_row_at_index(GTK_LIST_BOX(ad->listbox), i);
    
    if(gtk_list_box_row_is_selected(row))
      {
      return gavl_value_get_dictionary_nc(&ad->tracks->entries[i]);
      }
    }
  return NULL;
  }


static GtkWidget * create_list_entry(app_data_t * ad, const gavl_dictionary_t * track)
  {
  GtkWidget * label;
  const gavl_dictionary_t * m;
  GtkWidget * ret;
  gavl_time_t duration = GAVL_TIME_UNDEFINED;
  const char * klass;
  const char * label_s;
  const char * icon;
  char time_str[GAVL_TIME_STRING_LEN];
  
  m = gavl_track_get_metadata(track);

  klass = gavl_dictionary_get_string(m, GAVL_META_CLASS);
  label_s = gavl_dictionary_get_string(m, GAVL_META_LABEL);
  gavl_dictionary_get_long(m, GAVL_META_APPROX_DURATION, &duration);
  
  ret = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);

  
  label = gtk_label_new(NULL);

  if(klass && (icon = bg_get_type_icon(klass)))
    {
    char * markup =
      g_markup_printf_escaped("<span size=\"16000\" font_family=\"%s\" weight=\"normal\">%s</span>",
                              BG_ICON_FONT_FAMILY, icon);
    gtk_label_set_markup(GTK_LABEL(label), markup);
    g_free(markup);
    }
  gtk_widget_set_halign(label, GTK_ALIGN_START);
  gtk_size_group_add_widget(ad->sg_type, label);
  gtk_container_add(GTK_CONTAINER(ret), label);


  
  label = gtk_label_new(label_s);
  gtk_widget_set_halign(label, GTK_ALIGN_START);

  gtk_widget_set_hexpand(label, TRUE);
    
  
  gtk_container_add(GTK_CONTAINER(ret), label);


  gavl_time_prettyprint(duration, time_str);
  label = gtk_label_new(time_str);

  gtk_widget_set_halign(label, GTK_ALIGN_END);
  gtk_size_group_add_widget(ad->sg_duration, label);
  gtk_label_set_xalign(GTK_LABEL(label), 1.0);
  
  gtk_container_add(GTK_CONTAINER(ret), label);

  gtk_widget_show_all(ret);
  return ret;
  }

static void set_parameter_multi(gavl_dictionary_t * dict, const char * name, const gavl_value_t * val)
  {
  char ** arr = NULL;
  int i;
  gavl_dictionary_set(dict, name, NULL);

  if(!val)
    return;
  
  if(val->type != GAVL_TYPE_STRING)
    {
    gavl_dictionary_set(dict, name, val);
    return;
    }
  
  if(val && (arr = gavl_strbreak(val->v.str, ';')))
    {
    i = 0;
    while(arr[i])
      {
      gavl_dictionary_append_string_array(dict, name, arr[i]);
      i++;
      }
    }
  if(arr)
    gavl_strbreak_free(arr);
  return;
  }


static void set_parameter_selected(app_data_t * ad, const char * ctx, const char * name,
                                   const gavl_value_t * val)
  {
  int i;
  int tracknumber = 1;
  const char * rename_mask = NULL;
  if(!name)
    return;
  
  for(i = 0; i < ad->tracks->num_entries; i++)
    {
    gavl_dictionary_t * track;
    gavl_dictionary_t * dst;

    GtkListBoxRow * row = gtk_list_box_get_row_at_index(GTK_LIST_BOX(ad->listbox), i);
    
    if(!gtk_list_box_row_is_selected(row) ||
       !(track = gavl_value_get_dictionary_nc(&ad->tracks->entries[i])))
      continue;

    if(!strcmp(ctx, TRACK_METADATA))
      {
      if(!name)
        continue;

      dst = gavl_track_get_metadata_nc(track);
      
      /* string -> array */
      if(is_multi_tag(name))
        {
        set_parameter_multi(dst, name, val);
        }
      else
        {
        gavl_dictionary_set(dst, name, val);
        }

      if(strcmp(name, GAVL_META_YEAR))
        gavl_dictionary_set_string_nocopy(dst, GAVL_META_DATE,
                                          gavl_sprintf("%s-99-99", gavl_value_get_string(val)));
      
      }
    else if(!strcmp(ctx, TRACK_RENAME))
      {
      if(!name)
        continue;
      if(!strcmp(name, RENAME_MASK))
        {
        char * label;
        dst = gavl_track_get_metadata_nc(track);
        rename_mask = gavl_value_get_string(val);
        label = bg_create_track_name(dst, rename_mask);
        if(label)
          gavl_dictionary_set_string_nocopy(dst, GAVL_META_LABEL, label);
        }
      }
    else if(!strcmp(ctx, TRACK_MASSTAG))
      {
      const char * str;

      if(!name)
        continue;
      
      dst = gavl_track_get_metadata_nc(track);
      
      /* Handle cheat codes */
      if((str = gavl_value_get_string(val)))
        {
        if(!strcmp(str, "!")) // Leave unchanged
          continue;
        else if(!strcmp(str, "-")) // Clear tag
          {
          gavl_dictionary_set(dst, name, NULL);
          continue;
          }
        }

      if(strcmp(name, GAVL_META_YEAR))
        gavl_dictionary_set_string_nocopy(dst, GAVL_META_DATE,
                                          gavl_sprintf("%s-99-99", gavl_value_get_string(val)));
      
      if(is_multi_tag(name))
        {
        set_parameter_multi(dst, name, val);
        }
      else
        {
        gavl_dictionary_set(dst, name, val);
        }
        
      }
    else if(!strcmp(ctx, TRACK_ENCODER))
      {
      /* */
      if(strcmp(name, "plugin"))
        set_encoder_plugin(track, gavl_value_get_dictionary(val));

      dst = bg_track_get_config_nc(track, BG_TRACK_CONFIG_ENCODER);
      gavl_dictionary_set(dst, name, val);
      }
    else if(!strcmp(ctx, TRACK_AUTONUMBER))
      {
      dst = gavl_track_get_metadata_nc(track);
      gavl_dictionary_set_int(dst, GAVL_META_TRACKNUMBER, tracknumber);
      tracknumber++;
      }
    else if(gavl_string_starts_with(ctx, STREAM_PREFIX))
      {
      char ** str;
      str = gavl_strbreak(ctx, ':');

      if(str && str[0] && str[1] && str[2] && !str[3])
        {
        int idx = atoi(str[1]);
        
        if(!strcmp(str[0], STREAM_AUDIO))
          dst = gavl_track_get_stream_nc(track, GAVL_STREAM_AUDIO, idx);
        else if(!strcmp(str[0], STREAM_VIDEO))
          dst = gavl_track_get_stream_nc(track, GAVL_STREAM_VIDEO, idx);
        else if(!strcmp(str[0], STREAM_TEXT))
          dst = gavl_track_get_stream_nc(track, GAVL_STREAM_TEXT, idx);
        else if(!strcmp(str[0], STREAM_OVERLAY))
          dst = gavl_track_get_stream_nc(track, GAVL_STREAM_OVERLAY, idx);
        
        if(dst && (dst = bg_track_get_config_nc(dst, str[2])))
          gavl_dictionary_set(dst, name, val);
        
        }
      gavl_strbreak_free(str);
      }
    
    gtk_container_remove(GTK_CONTAINER(row), gtk_bin_get_child(GTK_BIN(row)));
    gtk_container_add(GTK_CONTAINER(row), create_list_entry(ad, track));
    
    }

  if(rename_mask)
    gavl_dictionary_set_string(&ad->state, RENAME_MASK, rename_mask);
  
  }

static void splice(app_data_t * ad, int idx, int del, gavl_array_t * add)
  {
  int i;
  if(idx < 0)
    idx = ad->tracks->num_entries;
  if(del < 0)
    del = ad->tracks->num_entries - idx;

  gavl_array_splice_array(ad->tracks, idx, del, add);

  if(del)
    {
    for(i = idx; i < idx + del; i++)
      {
      GtkListBoxRow *row;
      if((row = gtk_list_box_get_row_at_index(GTK_LIST_BOX(ad->listbox), idx)))
        gtk_container_remove(GTK_CONTAINER(ad->listbox), GTK_WIDGET(row));
      }
    }
  if(add && add->num_entries)
    {
    for(i = 0; i < add->num_entries; i++)
      {
      GtkWidget * row = 
        create_list_entry(ad, gavl_value_get_dictionary(&add->entries[i]));

      gtk_list_box_insert(GTK_LIST_BOX(ad->listbox), row, idx + i);
      }
    }
  }

void show_tracks_init(app_data_t * ad)
  {
  int i;

  for(i = 0; i < ad->tracks->num_entries; i++)
    {
    GtkWidget * row = 
      create_list_entry(ad, gavl_value_get_dictionary(&ad->tracks->entries[i]));
    gtk_list_box_insert(GTK_LIST_BOX(ad->listbox), row, i);
    }
  
  }

#define OPT_LANGUAGE { \
  .name = GAVL_META_LANGUAGE,  \
  .long_name = TRS("Language"), \
  .type = GAVL_PARAMETER_STRINGLIST,     \
  .multi_names = bg_language_codes,     \
    .multi_labels = bg_language_labels,        \
  .val_default = GAVL_VALUE_INIT_STRING("und") \
}

#define OPT_ACTION_AV {                                                 \
  .name = "action",                                                     \
    .long_name = TRS("Action"),                                         \
    .type = GAVL_PARAMETER_STRINGLIST,                                  \
    .multi_names = (const char*[]){ "transcode", "copy", "forget", NULL },    \
    .multi_labels = (const char*[]){ "Transcode", "Copy (if possible)", "Forget", NULL }, \
  .val_default = GAVL_VALUE_INIT_STRING("copy") \
  }

#define OPT_ACTION_TEXT {                                                 \
  .name = "action",                                                     \
    .long_name = TRS("Action"),                                         \
    .type = GAVL_PARAMETER_STRINGLIST,                                  \
    .multi_names = (const char*[]){ "copy", "forget", NULL },    \
    .multi_labels = (const char*[]){ "Copy (if possible)", "Forget", NULL }, \
  .val_default = GAVL_VALUE_INIT_STRING("copy") \
  }

static const gavl_parameter_info_t audio_parameters[] =
  {
    OPT_ACTION_AV,
    OPT_LANGUAGE,
    { },
  };

static const gavl_parameter_info_t video_parameters[] =
  {
    OPT_ACTION_AV,
    { },
    
  };

static const gavl_parameter_info_t text_parameters[] =
  {
    OPT_ACTION_TEXT,
    OPT_LANGUAGE,
    { },
    
  };

static const gavl_parameter_info_t overlay_parameters[] =
  {
    OPT_ACTION_AV,
    OPT_LANGUAGE,
    { },
    
  };


static const gavl_parameter_info_t output_parameters[] =
  {
    {
      .name =      "output_path",
      .long_name = TRS("Output Directory"),
      .type =      BG_PARAMETER_DIRECTORY,
      .val_default = GAVL_VALUE_INIT_STRING("."),
    },
    { /* End of parameters */ }
  };

static void set_encoder_plugin(gavl_dictionary_t * track, const gavl_dictionary_t * plugin)
  {
  int j = 0;
  const gavl_dictionary_t * audio_encoder_config;
  const gavl_dictionary_t * video_encoder_config;
  const gavl_dictionary_t * overlay_encoder_config;
  const gavl_dictionary_t * text_encoder_config;
  gavl_dictionary_t * dst;
  gavl_dictionary_t * stream;
  gavl_stream_type_t type;
  
  audio_encoder_config   = bg_cfg_section_find_subsection_c(plugin, "$audio");
  video_encoder_config   = bg_cfg_section_find_subsection_c(plugin, "$video");
  overlay_encoder_config = bg_cfg_section_find_subsection_c(plugin, "$overlay");
  text_encoder_config    = bg_cfg_section_find_subsection_c(plugin, "$text");
  
  while((stream = gavl_track_get_stream_all_nc(track, j)))
    {
    type = gavl_stream_get_type(stream);

    switch(type)
      {
      case GAVL_STREAM_AUDIO:
        dst = bg_track_get_config_nc(stream, BG_TRACK_CONFIG_ENCODER);
        gavl_dictionary_reset(dst);
        
        if(audio_encoder_config)
          gavl_dictionary_copy(dst, audio_encoder_config);
        
        break;
      case GAVL_STREAM_VIDEO:
        dst = bg_track_get_config_nc(stream, BG_TRACK_CONFIG_ENCODER);
        gavl_dictionary_reset(dst);
        
        if(video_encoder_config)
          gavl_dictionary_copy(dst, video_encoder_config);
        break;
      case GAVL_STREAM_TEXT:
        dst = bg_track_get_config_nc(stream, BG_TRACK_CONFIG_ENCODER);
        gavl_dictionary_reset(dst);

        if(text_encoder_config)
          gavl_dictionary_copy(dst, text_encoder_config);
        break;
      case GAVL_STREAM_OVERLAY:
        dst = bg_track_get_config_nc(stream, BG_TRACK_CONFIG_ENCODER);
        gavl_dictionary_reset(dst);

        if(overlay_encoder_config)
          gavl_dictionary_copy(dst, overlay_encoder_config);
        
        break;
      default:
        break;
      }
    j++;
    } 
  }

static void set_encoder_config(gavl_dictionary_t * track, const gavl_dictionary_t * cfg)
  {
  gavl_dictionary_t * dst;
  
  const gavl_dictionary_t * plugin;
  
  dst = bg_track_get_config_nc(track, BG_TRACK_CONFIG_ENCODER);
  gavl_dictionary_copy(dst, cfg);

  plugin = gavl_dictionary_get_dictionary(cfg, "plugin");
  set_encoder_plugin(track, plugin);
  }

static void add_transcoder_config(gavl_array_t * arr)
  {
  int i, j;
  gavl_stream_type_t type;
  const gavl_dictionary_t * encoder_config = bg_cfg_registry_find_section(bg_cfg_registry, PREFS_ENCODER);

  const gavl_dictionary_t * audio_config   = bg_cfg_registry_find_section(bg_cfg_registry, PREFS_AUDIO);
  const gavl_dictionary_t * video_config   = bg_cfg_registry_find_section(bg_cfg_registry, PREFS_VIDEO);
  const gavl_dictionary_t * text_config    = bg_cfg_registry_find_section(bg_cfg_registry, PREFS_TEXT);
  const gavl_dictionary_t * overlay_config = bg_cfg_registry_find_section(bg_cfg_registry, PREFS_OVERLAY);

  const gavl_dictionary_t * af_config   = bg_cfg_registry_find_section(bg_cfg_registry, PREFS_AUDIOFILTERS);
  const gavl_dictionary_t * vf_config   = bg_cfg_registry_find_section(bg_cfg_registry, PREFS_VIDEOFILTERS);
  
  gavl_dictionary_t * track;
  gavl_dictionary_t * stream;
  gavl_dictionary_t * dst;

  //  fprintf(stderr, "Add transcoder config\n");
  //  gavl_dictionary_dump(encoder_config, 2);
  
  for(i = 0; i < arr->num_entries; i++)
    {
    if(!(track = gavl_value_get_dictionary_nc(&arr->entries[i])))
      continue;

    set_encoder_config(track, encoder_config);
    
    gavl_dictionary_set(track, BG_TRACK_CONFIG_TAG, NULL);
    dst = bg_track_get_config_nc(track, BG_TRACK_CONFIG_ENCODER);
    gavl_dictionary_copy(dst, encoder_config);
    
    j = 0;
    while((stream = gavl_track_get_stream_all_nc(track, j)))
      {
      type = gavl_stream_get_type(stream);

      switch(type)
        {
        case GAVL_STREAM_AUDIO:
          dst = bg_track_get_config_nc(stream, BG_TRACK_CONFIG_TRANSCODE);
          gavl_dictionary_copy(dst, audio_config);

          dst = bg_track_get_config_nc(stream, BG_TRACK_CONFIG_FILTER);
          gavl_dictionary_copy(dst, af_config);

          break;
        case GAVL_STREAM_VIDEO:
          dst = bg_track_get_config_nc(stream, BG_TRACK_CONFIG_TRANSCODE);
          gavl_dictionary_copy(dst, video_config);

          dst = bg_track_get_config_nc(stream, BG_TRACK_CONFIG_FILTER);
          gavl_dictionary_copy(dst, vf_config);

          break;
        case GAVL_STREAM_TEXT:
          dst = bg_track_get_config_nc(stream, BG_TRACK_CONFIG_TRANSCODE);
          gavl_dictionary_copy(dst, text_config);
          break;
        case GAVL_STREAM_OVERLAY:
          dst = bg_track_get_config_nc(stream, BG_TRACK_CONFIG_TRANSCODE);
          gavl_dictionary_copy(dst, overlay_config);
          break;
        default:
          break;
        }
      
      j++;
      } 
    
    }
  
  } 

static int load_streams(gavl_array_t * arr)
  {
  int i;
  gavl_dictionary_t * track;
  for(i = 0; i < arr->num_entries; i++)
    {
    if(!(track = gavl_value_get_dictionary_nc(&arr->entries[i])))
      continue;

    if(!gavl_dictionary_get(track, GAVL_META_STREAMS))
      {
      int num_variants;
      bg_plugin_handle_t * h;
      gavl_dictionary_t * ti;
      /* Open location to load the streams */
      if(!(h = bg_load_track(track, 0, &num_variants)))
        return 0;
      ti = bg_input_plugin_get_track_info(h, -1);
      gavl_dictionary_copy_value(track, ti, GAVL_META_STREAMS);
      bg_plugin_unref(h);
      }
    }
  return 1;
  }

static void add_gmerlin_tracks(app_data_t * ad, gavl_array_t * arr)
  {
  
  /* Add encoder specific stuff */
  add_transcoder_config(arr);

  if(!load_streams(arr))
    return;
  
  fprintf(stderr, "Add tracks\n");
  gavl_array_dump(arr, 2);
  fprintf(stderr, "\n");


  splice(ad, -1, 0, arr);
  
  
  }

static void add_locations(app_data_t * ad, const gavl_array_t * arr)
  {
  gavl_array_t tracks;
  const char * path;

  gavl_value_t add_val;
  gavl_array_t * add_arr;

  /* Store open path */
  if(arr && (path = gavl_string_array_get(arr, 0)) &&
     (path[0] == '/'))
    {
    gavl_dictionary_set_string_nocopy(&ad->state, OPEN_PATH, gavl_strndup(path, strrchr(path, '/')));
    }
  
  gavl_value_init(&add_val);
  gavl_array_init(&tracks);
  add_arr = gavl_value_set_array(&add_val);
  
  gavl_tracks_from_locations(add_arr, arr);

  bg_tracks_resolve_locations(&add_val, &tracks, BG_INPUT_FLAG_GET_FORMAT);
          
  add_gmerlin_tracks(ad, &tracks);
  
  gavl_array_free(&tracks);
  gavl_value_free(&add_val);
  }
  
static int handle_dlg_message(void * data, gavl_msg_t * msg)
  {
  //  bg_gtk_mdb_tree_t * tree = data;

  app_data_t * ad = data;
  
  switch(msg->NS)
    {
    case BG_MSG_NS_PARAMETER:
      {
      switch(msg->ID)
        {
        case BG_CMD_SET_PARAMETER:
          {
          const char * name = NULL;
          const char * ctx = NULL;
          gavl_value_t val;
          
          bg_msg_get_parameter_ctx(msg, &ctx, &name, &val);

          if(!ctx)
            return 1;

          fprintf(stderr, "Parameter: %s %s\n", ctx, name);
          gavl_value_dump(&val, 2);
          fprintf(stderr, "\n");
          
          if(gavl_string_starts_with(ctx, PREFS_PREFIX))
            {
            if(name)
              {
              /* Preferences */
              gavl_dictionary_t * s =
                bg_cfg_registry_find_section(bg_cfg_registry, ctx);
              gavl_dictionary_set_nocopy(s, name, &val);
              }
            }
          else if(!strcmp(ctx, TRACK_YEARS))
            {
            int idx;
            gavl_dictionary_t * dict;

            if(!name)
              return 1;
            
            idx = atoi(name);
            
            if((dict = gavl_value_get_dictionary_nc(&ad->tracks->entries[idx])) &&
               (dict = gavl_track_get_metadata_nc(dict)))
              {
              gavl_dictionary_set_string_nocopy(dict, GAVL_META_DATE,
                                                gavl_sprintf("%s-99-99", gavl_value_get_string(&val)));
              gavl_dictionary_set_nocopy(dict, GAVL_META_YEAR, &val);
              }
            }
          else if(gavl_string_starts_with(ctx, TRACK_PREFIX) ||
                  gavl_string_starts_with(ctx, STREAM_PREFIX))
            set_parameter_selected(ad, ctx, name, &val);
          
          gavl_value_free(&val);
          
          break;
          }
        }
      }
      break;
    case BG_MSG_NS_DIALOG:
      {
      switch(msg->ID)
        {
        case BG_MSG_DIALOG_ADD_LOCATIONS:
          {
          gavl_array_t arr; /* Locations (as strings) */
          
          gavl_array_init(&arr);
          gavl_msg_get_arg_array(msg, 0, &arr);
          add_locations(ad, &arr);
          gavl_array_free(&arr);
          }
          break;
        }
      }
    }
  return 1;

  }


/* Actions */
#if 0
static app_data_t * get_app_data_app(gpointer user_data)
  {
  GtkApplication *app = GTK_APPLICATION(user_data);
  GtkWindow *window = gtk_application_get_active_window(app);
  return g_object_get_data(G_OBJECT(window), "app-data");
  }
#endif

static app_data_t * get_app_data_win(gpointer user_data)
  {
  GtkWindow *window = user_data;
  return g_object_get_data(G_OBJECT(window), "app-data");
  }

static void action_quit(GSimpleAction *action, GVariant *param, gpointer app)
  {
  (void)action;
  (void)param;

  /* Destroy window explicitly so the application state is saved */
  gtk_widget_destroy(GTK_WIDGET(gtk_application_get_active_window(GTK_APPLICATION(app))));
  
  g_application_quit(G_APPLICATION(app));
  }

static void action_about(GSimpleAction *action, GVariant *param, gpointer user_data)
  {
  (void)action;
  (void)param;
  (void)user_data;
  g_print("About dialog would go here.\n");
  }

static void action_addfiles(GSimpleAction *action, GVariant *param, gpointer user_data)
  {
  const char * open_path;
  app_data_t * ad = get_app_data_win(user_data);
  g_print("Add files.\n");

  if(!(open_path = gavl_dictionary_get_string(&ad->state, OPEN_PATH)))
    open_path = ".";
  
  bg_gtk_load_media_files("Load file(s)",
                          open_path,
                          ad->listbox,
                          ad->dlg_sink);
  
  }

static void action_addurl(GSimpleAction *action, GVariant *param, gpointer user_data)
  {
  app_data_t * ad = get_app_data_win(user_data);
  g_print("Add url.\n");

  bg_gtk_urlsel_show("Load URL",
                     ad->dlg_sink,
                     ad->listbox);

  }

static void action_addmedia(GSimpleAction *action, GVariant *param, gpointer user_data)
  {
  app_data_t * ad = get_app_data_win(user_data);
  g_print("Add media.\n");

  bg_gtk_drivesel_show("Add media", ad->dlg_sink, ad->listbox);
  
    
  }

static void action_copy(GSimpleAction *action, GVariant *param, gpointer user_data)
  {
  (void)action;
  (void)param;
  (void)user_data;
  g_print("Copy.\n");
  }

static void action_cancel(GSimpleAction *action, GVariant *param, gpointer user_data)
  {
  gavl_array_t arr;
  gavl_value_t val;

  app_data_t * ad = get_app_data_win(user_data);

  if(!ad->t)
    return;

  transcoder_destroy(ad->t);
  ad->t = NULL;

  /* Move track back to the list */
  gavl_array_init(&arr);
  gavl_value_init(&val);
  gavl_dictionary_move(gavl_value_set_dictionary(&val), &ad->cur);
  
  gavl_array_splice_val_nocopy(&arr, 0, 0, &val);
  splice(ad, 0, 0, &arr);
  gavl_array_free(&arr);
  
  app_default_status(ad);
  }

static void action_start(GSimpleAction *action, GVariant *param, gpointer user_data)
  {
  app_data_t * ad = get_app_data_win(user_data);
  
  (void)action;
  (void)param;
  
  g_print("Start\n");

  if(ad->t || !ad->tracks || !ad->tracks->num_entries)
    {
    return;
    }

  gavl_dictionary_move(&ad->cur, gavl_value_get_dictionary_nc(&ad->tracks->entries[0]));
  if(!(ad->t = transcoder_create(&ad->cur, ad->transcoder_sink)))
    return;
  
  splice(ad, 0, 1, NULL);
  
  };

/* Paste callbacks */

static const char * paste_formats[] =
  {
    TRANSCODER_TRACKS_MIMETYPE,
    bg_tracks_mimetype,
    "text/uri-list",
    "text/plain",
    NULL,
  };

static void paste_contents_received(GtkClipboard     *clipboard,
                                    GtkSelectionData *selection,
                                    gpointer          user_data)
  {
  GdkAtom type = gtk_selection_data_get_data_type(selection);
  gchar  *type_name = gdk_atom_name(type);

  gint length = gtk_selection_data_get_length(selection);
  const guchar *data = gtk_selection_data_get_data(selection);

  app_data_t * ad = get_app_data_win(user_data);
  
  fprintf(stderr, "paste contents received %s\n", type_name); 

  if(!strcmp(type_name, "text/uri-list") ||
     !strcmp(type_name, "text/plain"))
    {
    //    char ** bg_urilist_decode(const char * str, int len);
    gavl_array_t arr;
    gavl_array_init(&arr);
    bg_urilist_decode((char*)data, length, &arr);

    add_locations(ad, &arr);
    gavl_array_free(&arr);
    }
  else if(!strcmp(type_name, TRANSCODER_TRACKS_MIMETYPE))
    {
    gavl_dictionary_t dict;

    gavl_dictionary_init(&dict);
    bg_tracks_from_string(&dict, BG_TRACK_FORMAT_GMERLIN, (char*)data, length);

    splice(ad, -1, 0, gavl_get_tracks_nc(&dict));
    
    
    gavl_dictionary_free(&dict);
    
    }
  else if(!strcmp(type_name, bg_tracks_mimetype))
    {
    gavl_dictionary_t dict;

    fprintf(stderr, "Paste gmerlin tracks: %s\n", data);
    
    gavl_dictionary_init(&dict);
    bg_tracks_from_string(&dict, BG_TRACK_FORMAT_GMERLIN, (char*)data, length);

    add_gmerlin_tracks(ad, gavl_get_tracks_nc(&dict));

    
    gavl_dictionary_free(&dict);
    }
  
  g_free(type_name);
  }

static void paste_targets_received(GtkClipboard *clipboard,
                                   GdkAtom      *atoms,
                                   gint          n_atoms,
                                   gpointer      user_data)
  {
  int i;
  int idx = 0;
  GdkAtom format;

  while(paste_formats[idx])
    {
    format = gdk_atom_intern(paste_formats[idx], FALSE);

    for(i = 0; i < n_atoms; i++)
      {
      if(format == atoms[i])
        {
        gtk_clipboard_request_contents(clipboard, format,
                                       paste_contents_received, user_data);
        return;
        }
      }

    idx++;
    }
  
  }


static void action_paste(GSimpleAction *action, GVariant *param, gpointer user_data)
  {
  
  GtkClipboard *cb = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
  gtk_clipboard_request_targets(cb, paste_targets_received, user_data);
  g_print("Paste.\n");

  }

static void action_selall(GSimpleAction *action, GVariant *param, gpointer user_data)
  {
  app_data_t * ad = get_app_data_win(user_data);
  g_print("Select all\n");
  
  gtk_list_box_select_all(GTK_LIST_BOX(ad->listbox));

  }


static void action_delete(GSimpleAction *action, GVariant *param, gpointer user_data)
  {
  int i, end;
  app_data_t * ad = get_app_data_win(user_data);

  i = 0;
  
  while(i < ad->tracks->num_entries)
    {
    GtkListBoxRow * row = gtk_list_box_get_row_at_index(GTK_LIST_BOX(ad->listbox), i);

    if(gtk_list_box_row_is_selected(row))
      {
      end = i + 1;

      while(end < ad->tracks->num_entries)
        {
        row = gtk_list_box_get_row_at_index(GTK_LIST_BOX(ad->listbox), end);

        if(!gtk_list_box_row_is_selected(row))
          break;

        end++;
        }
      splice(ad, i, end - i, NULL);
      }
    else
      i++;
    
    }

  }

/* Create encoder configuration */

static void init_encoder_config(bg_cfg_ctx_t * ctx, const char * name)
  {
  bg_cfg_ctx_init(ctx, NULL, name, "Encoder plugin", NULL, NULL);

  ctx->parameters_priv = calloc(2, sizeof(*ctx->parameters_priv));
  ctx->parameters_priv[0].name = gavl_strdup("plugin");
  ctx->parameters_priv[0].long_name = gavl_strdup("Format");
  ctx->parameters_priv[0].type = GAVL_PARAMETER_MULTI_MENU;
  
  bg_plugin_registry_set_parameter_info(bg_plugin_reg, 
                                        BG_PLUGIN_ENCODER, BG_PLUGIN_FILE,
                                        &ctx->parameters_priv[0]);

  ctx->parameters = ctx->parameters_priv;

  
  }


static void action_preferences(GSimpleAction *action, GVariant *param, gpointer user_data)
  {
  app_data_t * ad = get_app_data_win(user_data);
  GtkWidget * dlg;
  GtkTreeIter it;

  bg_cfg_ctx_t ctx;
  
  dlg = bg_gtk_config_dialog_create_multi(BG_GTK_CONFIG_DIALOG_OK_CANCEL,
                                          TRS("Preferences"),
                                          ad->listbox);

  /* Output */

  bg_cfg_ctx_init(&ctx,
                  output_parameters,
                  PREFS_OUTPUT, TRS("Output options"),
                  NULL, // bg_set_parameter_func_t set_param,
                  NULL // void * cb_data
                  );
  ctx.sink = ad->dlg_sink;
  ctx.s = bg_cfg_registry_find_section(bg_cfg_registry, PREFS_OUTPUT);
  bg_gtk_config_dialog_add_section(dlg, &ctx, NULL);

  /* Encoder */
  init_encoder_config(&ctx, PREFS_ENCODER);
  ctx.sink = ad->dlg_sink;
  ctx.s = bg_cfg_registry_find_section(bg_cfg_registry, PREFS_ENCODER);
  bg_gtk_config_dialog_add_section(dlg, &ctx, NULL);
  
  /* Audio */
  bg_gtk_config_dialog_add_container(dlg, "Audio defaults", NULL, &it);
  bg_cfg_ctx_init(&ctx,
                  audio_parameters,
                  PREFS_AUDIO, TRS("General"),
                  NULL, // bg_set_parameter_func_t set_param,
                  NULL // void * cb_data
                  );
  ctx.sink = ad->dlg_sink;
  ctx.s = bg_cfg_registry_find_section(bg_cfg_registry, PREFS_AUDIO);
  bg_gtk_config_dialog_add_section(dlg, &ctx, &it);
  bg_cfg_ctx_free(&ctx);
  
  /* Audiofilters */
  bg_cfg_ctx_copy(&ctx, bg_plugin_config_get_ctx(BG_PLUGIN_FILTER_AUDIO));

  ctx.name = gavl_strrep(ctx.name, PREFS_AUDIOFILTERS);
  ctx.s = bg_cfg_registry_find_section(bg_cfg_registry, PREFS_AUDIOFILTERS);
  ctx.sink = ad->dlg_sink;
  
  bg_gtk_config_dialog_add_section(dlg, &ctx, &it);

  /* Video */
  bg_gtk_config_dialog_add_container(dlg, "Video defaults", NULL, &it);
  bg_cfg_ctx_init(&ctx,
                  video_parameters,
                  PREFS_VIDEO, TRS("General"),
                  NULL, // bg_set_parameter_func_t set_param,
                  NULL // void * cb_data
                  );
  ctx.sink = ad->dlg_sink;
  ctx.s = bg_cfg_registry_find_section(bg_cfg_registry, PREFS_VIDEO);
  bg_gtk_config_dialog_add_section(dlg, &ctx, &it);
  bg_cfg_ctx_free(&ctx);
  
  /* Videofilters */
  bg_cfg_ctx_copy(&ctx, bg_plugin_config_get_ctx(BG_PLUGIN_FILTER_VIDEO));

  ctx.name = gavl_strrep(ctx.name, PREFS_VIDEOFILTERS);
  ctx.s = bg_cfg_registry_find_section(bg_cfg_registry, PREFS_VIDEOFILTERS);
  ctx.sink = ad->dlg_sink;
  
  bg_gtk_config_dialog_add_section(dlg, &ctx, &it);

  /* Text */

  bg_cfg_ctx_init(&ctx,
                  text_parameters,
                  PREFS_VIDEO, TRS("Text defaults"),
                  NULL, // bg_set_parameter_func_t set_param,
                  NULL // void * cb_data
                  );
  ctx.sink = ad->dlg_sink;
  ctx.s = bg_cfg_registry_find_section(bg_cfg_registry, PREFS_TEXT);
  bg_gtk_config_dialog_add_section(dlg, &ctx, NULL);

  /* Overlay */

  bg_cfg_ctx_init(&ctx,
                  overlay_parameters,
                  PREFS_VIDEO, TRS("Overlay defaults"),
                  NULL, // bg_set_parameter_func_t set_param,
                  NULL // void * cb_data
                  );
  ctx.sink = ad->dlg_sink;
  ctx.s = bg_cfg_registry_find_section(bg_cfg_registry, PREFS_OVERLAY);
  bg_gtk_config_dialog_add_section(dlg, &ctx, NULL);
  
  /* */
  
  gtk_window_present(GTK_WINDOW(dlg));

  //  g_print("Preferences\n");
  }


static void action_config_encoder(GSimpleAction *action, GVariant *param, gpointer user_data)
  {
  GtkWidget * dlg;
  bg_cfg_ctx_t ctx;
  app_data_t * ad = get_app_data_win(user_data);
  int num_sel = num_selected(ad);

  //  g_print("Config encoder %d\n", num_sel);

  if(!num_sel)
    return;

  init_encoder_config(&ctx, TRACK_ENCODER);
  ctx.s = get_first_selected(ad);
  ctx.s = bg_track_get_config_nc(ctx.s, BG_TRACK_CONFIG_ENCODER);
  ctx.sink = ad->dlg_sink;
  
  dlg = bg_gtk_config_dialog_create_single(BG_GTK_CONFIG_DIALOG_OK_CANCEL,
                                           TRS("Select encoder"),
                                           ad->listbox,
                                           &ctx);
  bg_cfg_ctx_free(&ctx);
  gtk_window_present(GTK_WINDOW(dlg));
  }

static void add_stream_config(const bg_plugin_info_t * enc_info,
                              app_data_t * ad,
                              GtkWidget * dlg, gavl_dictionary_t * stream,
                              gavl_stream_type_t type, int idx, GtkTreeIter * parent)
  {
  bg_cfg_ctx_t ctx;
  switch(type)
    {
    case GAVL_STREAM_AUDIO:
      
      bg_cfg_ctx_init(&ctx,
                      audio_parameters,
                      NULL,
                      parent ? "General" : "Audio options",
                      NULL, NULL);
      ctx.name = gavl_sprintf("%s:%d:%s", STREAM_AUDIO, idx, BG_TRACK_CONFIG_TRANSCODE);
      ctx.s = bg_track_get_config_nc(stream, BG_TRACK_CONFIG_TRANSCODE);
      ctx.sink = ad->dlg_sink;
      bg_gtk_config_dialog_add_section(dlg, &ctx, parent);
      bg_cfg_ctx_free(&ctx);

      bg_cfg_ctx_copy(&ctx, bg_plugin_config_get_ctx(BG_PLUGIN_FILTER_AUDIO));
      if(ctx.name)
        free(ctx.name);
      ctx.name = gavl_sprintf("%s:%d:%s", STREAM_AUDIO, idx, BG_TRACK_CONFIG_FILTER);
      ctx.s = bg_track_get_config_nc(stream, BG_TRACK_CONFIG_FILTER);
      ctx.long_name = gavl_strrep(ctx.long_name, TR("Filters"));
      ctx.sink = ad->dlg_sink;
      bg_gtk_config_dialog_add_section(dlg, &ctx, parent);
      bg_cfg_ctx_free(&ctx);

      if(enc_info && enc_info->audio_parameters)
        {
        bg_cfg_ctx_init(&ctx,
                        enc_info->audio_parameters,
                        NULL,
                        "Encoder", NULL, NULL);
        ctx.sink = ad->dlg_sink;
        ctx.name = gavl_sprintf("%s:%d:%s", STREAM_AUDIO, idx, BG_TRACK_CONFIG_ENCODER);
        ctx.s = bg_track_get_config_nc(stream, BG_TRACK_CONFIG_ENCODER);
      
        bg_gtk_config_dialog_add_section(dlg, &ctx, parent);
        bg_cfg_ctx_free(&ctx);
        }
      
      break;
    case GAVL_STREAM_VIDEO:

      bg_cfg_ctx_init(&ctx,
                      video_parameters,
                      NULL,
                      parent ? "General" : "Video options",
                      NULL, NULL);
      ctx.name = gavl_sprintf("%s:%d:%s", STREAM_VIDEO, idx, BG_TRACK_CONFIG_TRANSCODE);
      ctx.s = bg_track_get_config_nc(stream, BG_TRACK_CONFIG_TRANSCODE);
      ctx.sink = ad->dlg_sink;
      bg_gtk_config_dialog_add_section(dlg, &ctx, parent);
      bg_cfg_ctx_free(&ctx);

      bg_cfg_ctx_copy(&ctx, bg_plugin_config_get_ctx(BG_PLUGIN_FILTER_VIDEO));
      if(ctx.name)
        free(ctx.name);
      ctx.name = gavl_sprintf("%s:%d:%s", STREAM_VIDEO, idx, BG_TRACK_CONFIG_FILTER);
      ctx.s = bg_track_get_config_nc(stream, BG_TRACK_CONFIG_FILTER);
      ctx.long_name = gavl_strrep(ctx.long_name, TR("Filters"));
      ctx.sink = ad->dlg_sink;
      bg_gtk_config_dialog_add_section(dlg, &ctx, parent);
      bg_cfg_ctx_free(&ctx);

      if(enc_info && enc_info->video_parameters)
        {
        bg_cfg_ctx_init(&ctx,
                        enc_info->video_parameters,
                        NULL,
                        "Encoder", NULL, NULL);
        ctx.sink = ad->dlg_sink;
        ctx.name = gavl_sprintf("%s:%d:%s", STREAM_VIDEO, idx, BG_TRACK_CONFIG_ENCODER);
        ctx.s = bg_track_get_config_nc(stream, BG_TRACK_CONFIG_ENCODER);

        bg_gtk_config_dialog_add_section(dlg, &ctx, parent);
        bg_cfg_ctx_free(&ctx);
        }


      break;
    case GAVL_STREAM_TEXT:

      bg_cfg_ctx_init(&ctx,
                      text_parameters,
                      NULL,
                      parent ? "General" : "Text options",
                      NULL, NULL);
      ctx.name = gavl_sprintf("%s:%d:%s", STREAM_TEXT, idx, BG_TRACK_CONFIG_TRANSCODE);
      ctx.s = bg_track_get_config_nc(stream, BG_TRACK_CONFIG_TRANSCODE);
      ctx.sink = ad->dlg_sink;
      bg_gtk_config_dialog_add_section(dlg, &ctx, parent);
      bg_cfg_ctx_free(&ctx);

      if(enc_info && enc_info->text_parameters)
        {
        bg_cfg_ctx_init(&ctx,
                        enc_info->text_parameters,
                        NULL,
                        "Encoder", NULL, NULL);
        ctx.sink = ad->dlg_sink;
        ctx.s = bg_track_get_config_nc(stream, BG_TRACK_CONFIG_ENCODER);
        ctx.name = gavl_sprintf("%s:%d:%s", STREAM_TEXT, idx, BG_TRACK_CONFIG_ENCODER);
        bg_gtk_config_dialog_add_section(dlg, &ctx, parent);
        bg_cfg_ctx_free(&ctx);
        }


      break;
    case GAVL_STREAM_OVERLAY:
      bg_cfg_ctx_init(&ctx,
                      overlay_parameters,
                      NULL,
                      parent ? "General" : "Overlay options",
                      NULL, NULL);
      ctx.name = gavl_sprintf("%s:%d:%s", STREAM_OVERLAY, idx, BG_TRACK_CONFIG_TRANSCODE);
      ctx.s = bg_track_get_config_nc(stream, BG_TRACK_CONFIG_TRANSCODE);
      ctx.sink = ad->dlg_sink;
      bg_gtk_config_dialog_add_section(dlg, &ctx, parent);
      bg_cfg_ctx_free(&ctx);
      
      if(enc_info && enc_info->overlay_parameters)
        {
        bg_cfg_ctx_init(&ctx,
                        enc_info->overlay_parameters,
                        NULL,
                        "Encoder", NULL, NULL);
        ctx.sink = ad->dlg_sink;
        ctx.s = bg_track_get_config_nc(stream, BG_TRACK_CONFIG_ENCODER);
        ctx.name = gavl_sprintf("%s:%d:%s", STREAM_OVERLAY, idx, BG_TRACK_CONFIG_ENCODER);
        bg_gtk_config_dialog_add_section(dlg, &ctx, parent);
        bg_cfg_ctx_free(&ctx);
        }

      break;
    default:
      break;
    }
  
  }
static void action_config_streams(GSimpleAction *action, GVariant *param, gpointer user_data)
  {
  int num_audio_streams;
  int num_video_streams;
  int num_text_streams;
  int num_overlay_streams;
  int num_streams;
  int i;
  GtkWidget * dlg;
  gavl_dictionary_t * stream;

  const bg_plugin_info_t * enc_info = NULL;
  const gavl_dictionary_t * dict;
  
  gavl_dictionary_t * track;
  app_data_t * ad = get_app_data_win(user_data);
  int num_sel = num_selected(ad);
  
  if(num_sel != 1)
    {
    return;
    }

  dlg = bg_gtk_config_dialog_create_multi(BG_GTK_CONFIG_DIALOG_OK_CANCEL,
                                          TRS("Configure streams"),
                                          ad->listbox);
  
  track = get_first_selected(ad);

  num_audio_streams = gavl_track_get_num_audio_streams(track);
  num_video_streams = gavl_track_get_num_video_streams(track);
  num_text_streams = gavl_track_get_num_text_streams(track);
  num_overlay_streams = gavl_track_get_num_overlay_streams(track);
  
  num_streams = num_audio_streams +
    num_video_streams +
    num_text_streams +
    num_overlay_streams;

  if((dict = bg_track_get_config(track, BG_TRACK_CONFIG_ENCODER)) &&
     (dict = gavl_dictionary_get_dictionary(dict, "plugin")))
    {
    const char * name;

    if((name = gavl_dictionary_get_string(dict, BG_CFG_TAG_NAME)))
      enc_info = bg_plugin_find_by_name(name);
    }
  
  if(num_streams == 1)
    {
    
    if(num_audio_streams)
      {
      stream = gavl_track_get_audio_stream_nc(track, 0);
      add_stream_config(enc_info, ad, dlg, stream, GAVL_STREAM_AUDIO, 0, NULL);
      }
    else if(num_video_streams)
      {
      stream = gavl_track_get_video_stream_nc(track, 0);
      add_stream_config(enc_info, ad, dlg, stream, GAVL_STREAM_VIDEO, 0, NULL);
      }
    else if(num_text_streams)
      {
      stream = gavl_track_get_text_stream_nc(track, 0);
      add_stream_config(enc_info, ad, dlg, stream, GAVL_STREAM_TEXT, 0, NULL);
      }
    else if(num_overlay_streams)
      {
      stream = gavl_track_get_overlay_stream_nc(track, 0);
      add_stream_config(enc_info, ad, dlg, stream, GAVL_STREAM_OVERLAY, 0, NULL);
      }
    }
  else
    {
    char * stream_label;
    GtkTreeIter it;
    for(i = 0; i < num_audio_streams; i++)
      {
      stream_label = num_audio_streams > 1 ? gavl_sprintf("Audio stream %d", i+1) : gavl_strdup("Audio stream");
      bg_gtk_config_dialog_add_container(dlg, stream_label, NULL, &it);
      stream = gavl_track_get_audio_stream_nc(track, 0);
      add_stream_config(enc_info, ad, dlg, stream, GAVL_STREAM_AUDIO, i, &it);
      }
    for(i = 0; i < num_video_streams; i++)
      {
      stream_label = num_video_streams > 1 ? gavl_sprintf("Video stream %d", i+1) : gavl_strdup("Video stream");
      bg_gtk_config_dialog_add_container(dlg, stream_label, NULL, &it);
      stream = gavl_track_get_video_stream_nc(track, 0);
      add_stream_config(enc_info, ad, dlg, stream, GAVL_STREAM_VIDEO, i, &it);
      }
    for(i = 0; i < num_text_streams; i++)
      {
      stream_label = num_text_streams > 1 ? gavl_sprintf("Text stream %d", i+1) : gavl_strdup("Text stream");
      bg_gtk_config_dialog_add_container(dlg, stream_label, NULL, &it);
      stream = gavl_track_get_text_stream_nc(track, 0);
      add_stream_config(enc_info, ad, dlg, stream, GAVL_STREAM_TEXT, i, &it);
      }
    for(i = 0; i < num_overlay_streams; i++)
      {
      stream_label = num_text_streams > 1 ? gavl_sprintf("Overlay stream %d", i+1) : gavl_strdup("Overlay stream");
      bg_gtk_config_dialog_add_container(dlg, stream_label, NULL, &it);
      stream = gavl_track_get_overlay_stream_nc(track, 0);
      add_stream_config(enc_info, ad, dlg, stream, GAVL_STREAM_OVERLAY, i, &it);
      }
    
    }

  gtk_window_present(GTK_WINDOW(dlg));
  
  g_print("Config streams %d\n", num_sel);
  }

static void action_config_metadata(GSimpleAction *action, GVariant *param, gpointer user_data)
  {
  GtkWidget * dlg;
  
  app_data_t * ad = get_app_data_win(user_data);
  int num_sel = num_selected(ad);
  bg_cfg_ctx_t ctx;
  gavl_dictionary_t * first;

  g_print("Config metadata %d\n", num_sel);
  
  if(!num_sel)
    return;

  first = get_first_selected(ad);
  first = gavl_track_get_metadata_nc(first);
  
  if(num_sel == 1)
    {
    //    const bg_parameter_info_t * parameters;
    
    /* Tag Single */
    const char * klass =
      gavl_dictionary_get_string(first, GAVL_META_CLASS);

    
    bg_cfg_ctx_init(&ctx,
                    get_metadata_parameters(klass),
                    TRACK_METADATA,
                    "Metadata",
                    NULL, NULL);
    }
  else
    {
    /* Mass tag */
    bg_cfg_ctx_init(&ctx,
                    metadata_bulk_parameters,
                    TRACK_MASSTAG,
                    "Mass tag",
                    NULL, NULL);
    }
  ctx.sink = ad->dlg_sink;
    
  ctx.s = first;

  dlg = bg_gtk_config_dialog_create_single(BG_GTK_CONFIG_DIALOG_OK_CANCEL,
                                           TRS("Configure metadata"),
                                           ad->listbox,
                                           &ctx);
  bg_cfg_ctx_free(&ctx);
  gtk_window_present(GTK_WINDOW(dlg));


  }

static void action_config_years(GSimpleAction *action, GVariant *param, gpointer user_data)
  {
  GtkWidget * dlg;
  bg_cfg_ctx_t ctx;
  int i;
  
  app_data_t * ad = get_app_data_win(user_data);
  int num_sel = num_selected(ad);
  
  if(!num_sel)
    return;
  
  fprintf(stderr, "Config years...\n");
  
  bg_cfg_ctx_init(&ctx,
                  NULL,
                  TRACK_YEARS,
                  "Release years",
                  NULL, NULL);

  ctx.params = &ctx.params_priv;
  ctx.sink = ad->dlg_sink;
  
  for(i = 0; i < ad->tracks->num_entries; i++)
    {
    GtkListBoxRow * row = gtk_list_box_get_row_at_index(GTK_LIST_BOX(ad->listbox), i);
    
    if(gtk_list_box_row_is_selected(row))
      {
      char * name;
      gavl_dictionary_t * param;
      const gavl_dictionary_t * dict;

      if(!(dict = gavl_value_get_dictionary(&ad->tracks->entries[i])) ||
         !(dict = gavl_track_get_metadata(dict)))
        continue;
      
      name = gavl_sprintf("%d", i);

      param =
        gavl_parameter_append_param(&ctx.params_priv, name,
                                    gavl_dictionary_get_string(dict, GAVL_META_LABEL),
                                    GAVL_PARAMETER_STRING);
      gavl_dictionary_set_string(param, GAVL_PARAMETER_DEFAULT,
                                 gavl_dictionary_get_string(dict, GAVL_META_YEAR));
      }
    }

  dlg = bg_gtk_config_dialog_create_single(BG_GTK_CONFIG_DIALOG_OK_CANCEL,
                                           TRS("Configure years"),
                                           ad->listbox,
                                           &ctx);

  
  bg_cfg_ctx_free(&ctx);
  gtk_window_present(GTK_WINDOW(dlg));
  
  }

static void action_config_autorename(GSimpleAction *action, GVariant *param, gpointer user_data)
  {
  GtkWidget * dlg;
  gavl_dictionary_t * p;
  bg_cfg_ctx_t ctx;
  const char * val;
  app_data_t * ad = get_app_data_win(user_data);
  g_print("autorename\n");

  bg_cfg_ctx_init(&ctx, NULL, TRACK_RENAME, "Auto rename", NULL, NULL);
  
  p = gavl_parameter_append_param(&ctx.params_priv, RENAME_MASK,
                                  "Mask", GAVL_PARAMETER_STRING);

  ctx.params = &ctx.params_priv;
  
  val = gavl_dictionary_get_string(&ad->state, RENAME_MASK);
  if(!val)
    val = "%2n %p - %t";

  gavl_dictionary_set_string(p, GAVL_PARAMETER_DEFAULT, val);
  
  ctx.sink = ad->dlg_sink;
  
  dlg = bg_gtk_config_dialog_create_single(BG_GTK_CONFIG_DIALOG_OK_CANCEL,
                                           TRS("Auto rename"),
                                           ad->listbox,
                                           &ctx);
  bg_cfg_ctx_free(&ctx);
  gtk_window_present(GTK_WINDOW(dlg));

  
  }

static void action_config_autorenumber(GSimpleAction *action, GVariant *param, gpointer user_data)
  {
  app_data_t * ad = get_app_data_win(user_data);
  set_parameter_selected(ad, TRACK_AUTONUMBER, TRACK_AUTONUMBER, NULL);
  }



/*
 * Actions are registered on the *application* and referenced in the menu
 * as "app.<name>".  Window-specific actions (e.g. "win.zoom-in") would be
 * registered on the GtkApplicationWindow instead.
 */
static const GActionEntry app_actions[] =
  {
    { "quit",  action_quit,  NULL, NULL, NULL },
    { "about", action_about, NULL, NULL, NULL },
  };

static const GActionEntry win_actions[] =
  {
    { "addmedia",  action_addmedia,  NULL, NULL, NULL },
    { "addfiles",  action_addfiles,  NULL, NULL, NULL },
    { "addurl",  action_addurl,  NULL, NULL, NULL },
    { "copy",   action_copy,   NULL, NULL, NULL },
    { "paste",  action_paste,  NULL, NULL, NULL },
    { "selall",  action_selall,  NULL, NULL, NULL },
    { "delete",  action_delete,  NULL, NULL, NULL },
    { "selall",  action_selall,  NULL, NULL, NULL },
    { "delete",  action_delete,  NULL, NULL, NULL },
    { "start",  action_start,  NULL, NULL, NULL },
    { "cancel",  action_cancel,  NULL, NULL, NULL },
    /* Configuration */
    { "preferences",  action_preferences,  NULL, NULL, NULL },
    { "cfgencoder",  action_config_encoder,  NULL, NULL, NULL },
    { "cfgstreams",  action_config_streams,  NULL, NULL, NULL },
    { "cfgmetadata",  action_config_metadata,  NULL, NULL, NULL },
    { "cfgrename",  action_config_autorename,  NULL, NULL, NULL },
    { "cfgrenumber",  action_config_autorenumber,  NULL, NULL, NULL },
    { "cfgyears",  action_config_years,  NULL, NULL, NULL },
  };

/* ── Menu construction ──────────────────────────────────────────────────── */
static GMenuModel *build_menubar(void)
  {
  GMenu *menu;
  GMenu *menubar = g_menu_new();
  /* ── File ── */
  menu = g_menu_new();
  g_menu_append(menu, "_Start transcoding", "win.start");
  g_menu_append(menu, "_Add files...", "win.addfiles");
  g_menu_append(menu, "Add _URL...",   "win.addurl");
  g_menu_append(menu, "Add _Media...", "win.addmedia");
  g_menu_append(menu, "_Cancel transcoding", "win.cancel");
  
  g_menu_append_section(menu, NULL, G_MENU_MODEL(g_menu_new())); /* separator */
  g_menu_append(menu, "_Quit", "app.quit");
  g_menu_append_submenu(menubar, "_File", G_MENU_MODEL(menu));
  g_object_unref(menu);

  /* ── Edit ── */
  menu = g_menu_new();
  g_menu_append(menu, "_Copy",  "win.copy");
  g_menu_append(menu, "_Paste", "win.paste");
  g_menu_append(menu, "Select all", "win.selall");
  g_menu_append(menu, "Delete selected", "win.delete");

  g_menu_append_submenu(menubar, "_Edit", G_MENU_MODEL(menu));
  g_object_unref(menu);

  /* Configuration */
  menu = g_menu_new();
  g_menu_append(menu, "Preferences...",  "win.preferences");
  g_menu_append(menu, "Encoder...",  "win.cfgencoder");
  g_menu_append(menu, "Streams...", "win.cfgstreams");
  g_menu_append(menu, "Metadata...", "win.cfgmetadata");
  g_menu_append(menu, "Release years...", "win.cfgyears");
  g_menu_append(menu, "Rename...", "win.cfgrename");
  g_menu_append(menu, "Automatic numbering", "win.cfgrenumber");
  g_menu_append_submenu(menubar, "_Configure", G_MENU_MODEL(menu));
  g_object_unref(menu);
  
  
  /* ── Help ── */
  menu = g_menu_new();
  g_menu_append(menu, "_About", "app.about");
  g_menu_append_submenu(menubar, "_Help", G_MENU_MODEL(menu));
  g_object_unref(menu);
  return G_MENU_MODEL(menubar);
  }

static void register_accelerators(GtkApplication *app)
  {
  /* gtk_application_set_accels_for_action() is identical in GTK-4. */
  const char *accels_quit[]  = { "<Primary>q", NULL };
  const char *accels_copy[]   = { "<Primary>c", NULL };
  const char *accels_paste[]  = { "<Primary>v", NULL };
  const char *accels_selall[]  = { "<Primary>a", NULL };
  const char *accels_metadata[]  = { "<Primary>m", NULL };
  const char *accels_delete[]  = { "<Primary>Delete", NULL };

  gtk_application_set_accels_for_action(app, "app.quit",  accels_quit);
  gtk_application_set_accels_for_action(app, "win.copy",  accels_copy);
  gtk_application_set_accels_for_action(app, "win.paste", accels_paste);
  gtk_application_set_accels_for_action(app, "win.selall", accels_selall);
  gtk_application_set_accels_for_action(app, "win.delete", accels_delete);
  gtk_application_set_accels_for_action(app, "win.cfgmetadata", accels_metadata);
  }

void register_window_actions(GtkWidget * w)
  {
  g_action_map_add_action_entries(G_ACTION_MAP(w),
                                  win_actions,
                                  G_N_ELEMENTS(win_actions),
                                  w);
  
  }

static gboolean idle_func(void * data)
  {
  app_data_t * ad = data;

  bg_msg_sink_iteration(ad->transcoder_sink);
  
  return G_SOURCE_CONTINUE;
  }

app_data_t * create_app_data(void)
  {
  char * state_dir;
  char * filename;
  
  app_data_t * ret;
  ret = calloc(1, sizeof(*ret));
  ret->sg_type     = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);
  ret->sg_duration = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);
  ret->dlg_sink = bg_msg_sink_create(handle_dlg_message, ret, 1);

  ret->transcoder_sink = bg_msg_sink_create(handle_transcoder_message, ret, 0);
  
  /* load state */
  state_dir = gavl_search_state_dir(PACKAGE, "transcoder", NULL);
  filename = gavl_sprintf("%s/state.xml", state_dir);

  bg_dictionary_load_xml(&ret->state, filename, "state");
  free(state_dir);
  free(filename);

  ret->tracks = gavl_dictionary_get_array_create(&ret->state, "tracks");

  ret->idle_tag = g_timeout_add(100, idle_func, ret);

  
  return ret;
  }

void destroy_app_data(void * priv)
  {
  char * state_dir;
  char * filename;

  app_data_t * data = priv;
  bg_msg_sink_destroy(data->dlg_sink);
  bg_msg_sink_destroy(data->transcoder_sink);

  state_dir = gavl_search_state_dir(PACKAGE, "transcoder", NULL);
  filename = gavl_sprintf("%s/state.xml", state_dir);
  bg_dictionary_save_xml(&data->state, filename, "state");
  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Saved state to %s", filename);
  free(state_dir);
  free(filename);
  
  gavl_dictionary_free(&data->state);
  g_source_remove(data->idle_tag);
  
  free(data);
  }

void app_startup(GtkApplication *app, gpointer user_data)
  {
  GMenuModel *menubar;
  //  (void)user_data;

  bg_app_init("transcoder",
              "Gmerlin transcoder",
              "transcoder");
  
  bg_iconfont_init();
  
  bg_plugins_init();


  
  /*
   * Good place to:
   *   - load GMenu / GMenuModel from a .ui file
   *   - register GAction on the application
   *   - load CSS via GtkCssProvider
   */
    /* Register application-wide actions. */
  g_action_map_add_action_entries(G_ACTION_MAP(app),
                                  app_actions,
                                  G_N_ELEMENTS(app_actions),
                                  app);
  
  /* Build and attach the menubar. */
  menubar = build_menubar();
  gtk_application_set_menubar(app, menubar);
  g_object_unref(menubar);
  register_accelerators(app);
  
  //  (void)app;
  }

void app_activate(GtkApplication *app, gpointer user_data)
  {
  
  GtkWidget *win = app_window_new(app);

  /*
   * GTK-3: gtk_widget_show_all() makes every child visible recursively.
   * GTK-4: replace with gtk_widget_show(win) — children are shown automatically.
   */
  gtk_widget_show_all(win);
  }

void app_shutdown(GtkApplication *app, gpointer user_data)
  {
  (void)app;
  (void)user_data;

  /* Release any application-wide resources here. */

  bg_cfg_registry_save();
  }
