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

#include <gmerlin.h>

#include <gmerlin/iconfont.h>
#include <gmerlin/utils.h>

#include <gavl/metatags.h>
#include <gavl/state.h>
#include <gavl/log.h>
#define LOG_DOMAIN "mainwindow"

#include <gui_gtk/gtkutils.h>

#define DELAY_TIME 10

#define DEFAULT_LABEL "<markup><span weight=\"bold\">Gmerlin Player (version "VERSION")</span></markup>"

#define ERROR_LABEL "<markup><span weight=\"bold\">Playback error, check log messages for details</span></markup>"

/*
  typedef struct
  {
  GtkWidget * win;
  GtkWidget * play_button;
  GtkWidget * stop_button;
  GtkWidget * next_button;
  GtkWidget * prev_button;
  GtkWidget * menu_button;
  GtkWidget * close_button;
  
  } main_window_t;

*/

/* Seek flags */

#define SEEK_ACTIVE     (1<<0)
#define SEEK_WAS_PAUSED (1<<1)
#define SEEK_PAUSE_SENT (1<<2)

#define DISPLAY_MODE_NORMAL   0
#define DISPLAY_MODE_REM      1
#define DISPLAY_MODE_ALL      2
#define DISPLAY_MODE_ALL_REM  3
#define NUM_DISPLAY_MODES     4



static const char * icon_css = \
  "* { font-family: "BG_ICON_FONT_FAMILY"; }";

static void button_callback(GtkWidget * w, gpointer data)
  {
  main_window_t * win = data;
  if(w == win->play_button)
    {
    bg_player_play(win->player_ctrl.cmd_sink);
    }
  else if(w == win->pause_button)
    {
    bg_player_pause(win->player_ctrl.cmd_sink);
    }
  else if(w == win->stop_button)
    {
    bg_player_stop(win->player_ctrl.cmd_sink);
    }
  else if(w == win->next_button)
    {
    bg_player_next(win->player_ctrl.cmd_sink);
    }
  else if(w == win->prev_button)
    {
    bg_player_prev(win->player_ctrl.cmd_sink);
    }
  else if(w == win->close_button)
    {
    bg_gtk_quit();
    }
  else if(w == win->menu_button)
    {
    gtk_menu_popup_at_widget(GTK_MENU(main_menu_get_widget(win->g->main_menu)),
                             win->menu_button,
                             GDK_GRAVITY_CENTER,
                             GDK_GRAVITY_NORTH_WEST,
                             NULL);
    }
  }

static GtkWidget * player_button_create(main_window_t * win,
                                        const char * icon)
  {
  GtkStyleContext *context;
  
  GtkWidget * ret;
  
  ret = gtk_button_new_with_label(icon);
  
  context = gtk_widget_get_style_context(ret);
  gtk_style_context_add_class(context,"player-button");

  gtk_style_context_add_provider(context,
                                 GTK_STYLE_PROVIDER(win->skin_provider),
                                 GTK_STYLE_PROVIDER_PRIORITY_USER);
  
  gtk_style_context_add_provider(context,
                                 GTK_STYLE_PROVIDER(win->icon_provider),
                                 GTK_STYLE_PROVIDER_PRIORITY_USER);
  
  g_signal_connect(G_OBJECT(ret), "clicked", G_CALLBACK(button_callback), win);

  gtk_widget_set_halign(ret, GTK_ALIGN_START);
  gtk_widget_set_hexpand(ret, FALSE);
  
  gtk_widget_show(ret);
  return ret;
  }

static GtkWidget * title_button_create(main_window_t * win,
                                       const char * icon)
  {
  GtkWidget * ret;
  GtkStyleContext *context;
  
  ret = gtk_button_new_with_label(icon);
  context = gtk_widget_get_style_context(ret);
  gtk_style_context_add_class(context,"title-button");
  
  gtk_style_context_add_provider(context,
                                 GTK_STYLE_PROVIDER(win->skin_provider),
                                 GTK_STYLE_PROVIDER_PRIORITY_USER);

  gtk_style_context_add_provider(context,
                                 GTK_STYLE_PROVIDER(win->icon_provider),
                                 GTK_STYLE_PROVIDER_PRIORITY_USER);
  
  gtk_widget_show(ret);
  g_signal_connect(G_OBJECT(ret), "clicked", G_CALLBACK(button_callback), win);
  return ret;
  }

static void set_css_class(GtkWidget * w, 
                          GtkCssProvider * skin_provider,
                          GtkCssProvider * icon_provider,
                          const char * klass)
  {
  GtkStyleContext *context;
  context = gtk_widget_get_style_context(w);
  gtk_style_context_add_provider(context,
                                 GTK_STYLE_PROVIDER(skin_provider),
                                 GTK_STYLE_PROVIDER_PRIORITY_USER);
  if(icon_provider)
    gtk_style_context_add_provider(context,
                                   GTK_STYLE_PROVIDER(icon_provider),
                                   GTK_STYLE_PROVIDER_PRIORITY_USER);
    

  gtk_style_context_add_class(context, klass);
  
  }


static void scale_value_changed(GtkWidget* self,
                                gpointer data)
  {
  double value;
  main_window_t * w = data;
  
  value = gtk_range_get_value(GTK_RANGE(w->seek_slider));
  
  if(self == w->seek_slider)
    {
    
    if(w->seek_flags & SEEK_ACTIVE)
      {
      if(!(w->seek_flags & SEEK_WAS_PAUSED) &&
         !(w->seek_flags & SEEK_PAUSE_SENT))
        {
        bg_player_pause(w->player_ctrl.cmd_sink);
        w->seek_flags |= SEEK_PAUSE_SENT;
        }
      }
    bg_player_seek_perc(w->player_ctrl.cmd_sink, value);
    }
  if(self == w->volume_slider)
    {
    bg_player_set_volume(w->player_ctrl.cmd_sink, gtk_range_get_value(GTK_RANGE(w->volume_slider)));
    }
  }

static void hide_label(GtkWidget * w)
  {
  GtkStyleContext * context = gtk_widget_get_style_context(w);

  if(!gtk_style_context_has_class(context, "invisible"))
    gtk_style_context_add_class(context,"invisible");

  }

static void show_label(GtkWidget * w)
  {
  GtkStyleContext * context = gtk_widget_get_style_context(w);
  if(gtk_style_context_has_class(context, "invisible"))
    gtk_style_context_remove_class(context,"invisible");
  
  }

static void set_time_label(main_window_t * w, gavl_time_t t)
  {
  char str[GAVL_TIME_STRING_LEN];
  
#if 0  
  switch(w->display_mode)
    {
    case DISPLAY_MODE_NORMAL:
      gavl_dictionary_get_long(dict, BG_PLAYER_TIME, &t);
      break;
    case DISPLAY_MODE_REM:
      gavl_dictionary_get_long(dict, BG_PLAYER_TIME_REM, &t);
      break;
    case DISPLAY_MODE_ALL:
      gavl_dictionary_get_long(dict, BG_PLAYER_TIME_ABS, &t);
      break;
    case DISPLAY_MODE_ALL_REM:
      gavl_dictionary_get_long(dict, BG_PLAYER_TIME_REM_ABS, &t);
      break;
    }
#endif
  
  if(t > (gavl_time_t)GAVL_TIME_SCALE*3600*24*356)
    gavl_time_prettyprint_local(t, str);
  else
    gavl_time_prettyprint(t, str);
  
  gtk_label_set_text(GTK_LABEL(w->time_label), str);
  }


static void set_display_mode(main_window_t * w)
  {
  const gavl_value_t * time_val;
  gavl_time_t t;
  const char * time_name = NULL;
  
  /* Update labels */
  switch(w->display_mode)
    {
    case DISPLAY_MODE_NORMAL:
      hide_label(w->all_label);
      hide_label(w->rem_label);
      time_name = BG_PLAYER_STATE_TIME;
      break;
    case DISPLAY_MODE_REM:
      hide_label(w->all_label);
      show_label(w->rem_label);
      time_name = BG_PLAYER_STATE_TIME_REM;
      break;
    case DISPLAY_MODE_ALL:
      show_label(w->all_label);
      hide_label(w->rem_label);
      time_name = BG_PLAYER_STATE_TIME_ABS;
      break;
    case DISPLAY_MODE_ALL_REM:
      show_label(w->all_label);
      show_label(w->rem_label);
      time_name = BG_PLAYER_STATE_TIME_REM_ABS;
      break;
    }

  if(time_name &&
     (time_val = bg_state_get(&w->g->state,
                              BG_PLAYER_STATE_CTX,
                              time_name)) &&     // long
     gavl_value_get_long(time_val, &t))
    set_time_label(w, t);
  
  }

static gboolean button_press_event(GtkWidget* self, GdkEventButton * evt,
                                          gpointer data)
  {
  main_window_t * w = data;
  if(self == w->seek_slider)
    {
    if(!(w->seek_flags & SEEK_ACTIVE))
      {
      const gavl_value_t * status_val;

      w->seek_flags = 0;
      
      if((status_val = bg_state_get(&w->g->state,
                                    BG_PLAYER_STATE_CTX,
                                    BG_PLAYER_STATE_STATUS)) &&     // dictionary
         (status_val->v.i == BG_PLAYER_STATUS_PAUSED))
        w->seek_flags |= SEEK_WAS_PAUSED;
      
      w->seek_flags |= SEEK_ACTIVE;
      }
    }
  else if(self == w->volume_slider)
    w->volume_active = 1;
  return FALSE;
  }

static gboolean button_release_event(GtkWidget* self, GdkEventButton * evt,
                                            gpointer data)
  {
  main_window_t * w = data;
  if(self == w->seek_slider)
    {
    if(!(w->seek_flags & SEEK_WAS_PAUSED))
      bg_player_pause(w->player_ctrl.cmd_sink);
    w->seek_flags = 0;
    }
  else if(self == w->volume_slider)
    w->volume_active = 0;
  return FALSE;
  }

#define META_ATTRIBUTES "size=\"small\" alpha=\"70%%\""

static char * add_metadata_string(char * markup, const char * str, const char * icon)
  {
  char * tmp_string = g_markup_printf_escaped("<span "META_ATTRIBUTES"><span font_family=\""BG_ICON_FONT_FAMILY"\" weight=\"normal\">%s</span> %s</span>",
                                              icon, str);

  if(markup)
    markup = gavl_strcat(markup, " ");
  
  markup = gavl_strcat(markup, tmp_string);
  g_free(tmp_string);
  return markup;
  }

static char * add_metadata_markup(char * markup, const gavl_dictionary_t * m, const char * key, const char * icon)
  {
  const char * var;
  
  if(!(var = gavl_dictionary_get_string(m, key)))
    return markup;
  
  return add_metadata_string(markup, var, icon);
  }

static char * add_metadata_markup_array(char * markup, const gavl_dictionary_t * m, const char * key, const char * icon)
  {
  char * tmp_string;
  
  if(!(tmp_string = gavl_metadata_join_arr(m, key, ", ")))
    return markup;
  
  markup = add_metadata_string(markup, tmp_string, icon);
  free(tmp_string);
  return markup;
  }

static char * get_track_markup(const gavl_dictionary_t * m)
  {
  const char * var;
  char * tmp_string;
  char * markup = NULL;
  char * metadata = NULL;
  gavl_time_t duration = -1;
  
  if((var = gavl_dictionary_get_string(m, GAVL_META_LABEL)))
    {
    tmp_string = g_markup_printf_escaped("%s", var);
    markup = gavl_sprintf("<markup><span font_weight=\"bold\">%s</span>\n", tmp_string);
    free(tmp_string);
    }
  else
    return gavl_strdup(DEFAULT_LABEL);
  
  metadata = add_metadata_markup_array(metadata, m, GAVL_META_ARTIST, BG_ICON_MICROPHONE);
  metadata = add_metadata_markup(metadata, m, GAVL_META_ALBUM, BG_ICON_MUSIC_ALBUM);
  metadata = add_metadata_markup_array(metadata, m, GAVL_META_GENRE, BG_ICON_MASKS);
  metadata = add_metadata_markup_array(metadata, m, GAVL_META_TAG, BG_ICON_TAGS);

  metadata = add_metadata_markup(metadata, m, GAVL_META_STATION, BG_ICON_RADIO);
  
  metadata = add_metadata_markup_array(metadata, m, GAVL_META_COUNTRY, BG_ICON_FLAG);
  if((var = gavl_dictionary_get_string(m, GAVL_META_DATE)))
    {
    if(!gavl_string_starts_with(var, "9999"))
      {
      if(!gavl_string_ends_with(var, "99-99"))
        metadata = add_metadata_string(metadata, var, BG_ICON_CALENDAR);
      else
        {
        tmp_string = gavl_strndup(var, var + 4);
        metadata = add_metadata_string(metadata, tmp_string, BG_ICON_CALENDAR);
        free(tmp_string);
        }
      }
    }

  // markup = add_metadata_markup_array(markup, m, GAVL_META_APPROX_DURATION, BG_ICON_CLOCK);
  
  if(gavl_dictionary_get_long(m, GAVL_META_APPROX_DURATION, &duration) && (duration > 0))
    {
    char str[GAVL_TIME_STRING_LEN];
    gavl_time_prettyprint(duration, str);
    metadata = add_metadata_string(metadata, str, BG_ICON_CLOCK);
    }
  
  if(metadata)
    markup = gavl_strcat(markup, metadata);
  
  markup = gavl_strcat(markup, "</markup>");

  if(metadata)
    free(metadata);

  return markup;
  }

static void set_volume_icon(main_window_t * w, double volume)
  {
  if(volume < 1.0/3.0)
    gtk_label_set_text(GTK_LABEL(w->volume_label), BG_ICON_VOLUME_MIN);
  else if(volume < 2.0/3.0)
    gtk_label_set_text(GTK_LABEL(w->volume_label), BG_ICON_VOLUME_MID);
  else
    gtk_label_set_text(GTK_LABEL(w->volume_label), BG_ICON_VOLUME_MAX);
  }

static gboolean do_configure(gpointer data)
  {
  gmerlin_t * gmerlin;
  gmerlin = (gmerlin_t*)data;
  gmerlin_configure(gmerlin);
  return FALSE;
  }

static int handle_player_message_gmerlin(void * data, gavl_msg_t * msg)
  {
  main_window_t * w = data;
  switch(msg->NS)
    {
    case GAVL_MSG_NS_GENERIC:
      switch(msg->ID)
        {
        case GAVL_CMD_SET_RESOURCE:
          {
          const char * klass;
          const char * uri;
          gavl_dictionary_t dev;
          gavl_dictionary_init(&dev);
          gavl_msg_get_arg_dictionary_c(msg, 0, &dev);
          
          //          fprintf(stderr, "Set Resource\n");
          //          gavl_dictionary_dump(&dev, 2);
          
          if((klass = gavl_dictionary_get_string(&dev, GAVL_META_CLASS)) &&
              (uri = gavl_dictionary_get_string(&dev, GAVL_META_URI)))
            {
            if(!strcmp(klass, GAVL_META_CLASS_BACKEND_MDB))
              {
              pthread_mutex_lock(&w->g->backend_mutex);

              gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Changing MDB: %s", uri);
              
              gmerlin_disconnect_mdb(w->g);
              w->g->mdb_ctrl = NULL;

              if(w->g->mdb_backend)
                {
                bg_plugin_unref(w->g->mdb_backend);
                w->g->mdb_backend = NULL;
                }
                
              if(!strcmp(uri, "local"))
                w->g->mdb_ctrl = bg_mdb_get_controllable(w->g->mdb);
              else
                {
                if((w->g->mdb_backend =
                    bg_backend_handle_create(&dev)))
                  w->g->mdb_ctrl = bg_backend_handle_get_controllable(w->g->mdb_backend);

                }
                
              gmerlin_connect_mdb(w->g);

              pthread_mutex_unlock(&w->g->backend_mutex);

              }
            else if(!strcmp(klass, GAVL_META_CLASS_BACKEND_RENDERER))
              {
              pthread_mutex_lock(&w->g->backend_mutex);
              
              gmerlin_disconnect_player(w->g);
              w->g->player_ctrl = NULL;
                
              if(w->g->player_backend)
                {
                bg_plugin_unref(w->g->player_backend);
                w->g->player_backend = NULL;
                }

              gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Changing renderer: %s", uri);
              
              if(!strcmp(uri, "local"))
                w->g->player_ctrl = bg_player_get_controllable(w->g->player);
              else
                {
                if((w->g->player_backend = bg_backend_handle_create(&dev)))
                  w->g->player_ctrl = bg_backend_handle_get_controllable(w->g->player_backend);
                else
                  {
                  gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Creating renderer failed");
                  break;
                  }
                }
                
              gmerlin_connect_player(w->g);
              pthread_mutex_unlock(&w->g->backend_mutex);
              }  
            else if(!strcmp(klass, GAVL_META_CLASS_SINK_AUDIO))
              {
              gavl_value_t val;
              gavl_msg_t * msg;

              fprintf(stderr, "Set audio sink\n");
              
              gavl_value_init(&val);
              gavl_value_set_string(&val, uri);
              
              msg = bg_msg_sink_get(w->player_ctrl.cmd_sink);
              
              gavl_msg_set_state(msg,
                                 BG_CMD_SET_STATE,
                                 1,
                                 BG_PLAYER_STATE_CTX,
                                 BG_PLAYER_STATE_OA_URI,
                                 &val);
              
              bg_msg_sink_put(w->player_ctrl.cmd_sink);
              gavl_value_free(&val);
              }
            else if(!strcmp(klass, GAVL_META_CLASS_SINK_VIDEO))
              {
              gavl_value_t val;
              gavl_msg_t * msg;
              
              fprintf(stderr, "Set video sink\n");
              gavl_value_init(&val);
              gavl_value_set_string(&val, uri);
              
              msg = bg_msg_sink_get(w->player_ctrl.cmd_sink);
              
              gavl_msg_set_state(msg,
                                 BG_CMD_SET_STATE,
                                 1,
                                 BG_PLAYER_STATE_CTX,
                                 BG_PLAYER_STATE_OV_URI,
                                 &val);
              
              bg_msg_sink_put(w->player_ctrl.cmd_sink);
              gavl_value_free(&val);
              }
            }
          //          gavl_dictionary_dump(&dev, 2);
          gavl_dictionary_free(&dev);
          //          fprintf(stderr, "\n");
          }
          
          break;
        }
      break;
    
    case BG_MSG_NS_STATE:
      switch(msg->ID)
        {
        case BG_MSG_STATE_CHANGED:
          {
          gavl_value_t val;
          const char * ctx;
          const char * var;
          int last;
          
          gavl_value_init(&val);
          
          gavl_msg_get_state(msg, &last, &ctx, &var, &val, &w->g->state);

          if(!strcmp(ctx, BG_PLAYER_STATE_CTX))
            {
            if(!strcmp(var, BG_PLAYER_STATE_TIME) && (w->display_mode == DISPLAY_MODE_NORMAL))          // dictionary
              {
              gavl_time_t t;
              if(gavl_value_get_long(&val, &t))
                set_time_label(w, t);
              }
            else if(!strcmp(var, BG_PLAYER_STATE_TIME_REM) && (w->display_mode == DISPLAY_MODE_REM))          // long
              {
              gavl_time_t t;
              if(gavl_value_get_long(&val, &t))
                set_time_label(w, t);
              }
            else if(!strcmp(var, BG_PLAYER_STATE_TIME_ABS) && (w->display_mode == DISPLAY_MODE_ALL))          // long
              {
              gavl_time_t t;
              if(gavl_value_get_long(&val, &t))
                set_time_label(w, t);
              }
            else if(!strcmp(var, BG_PLAYER_STATE_TIME_REM_ABS) && (w->display_mode == DISPLAY_MODE_ALL_REM))      // long
              {
              gavl_time_t t;
              if(gavl_value_get_long(&val, &t))
                set_time_label(w, t);
              }
            else if(!strcmp(var, BG_PLAYER_STATE_TIME_PERC))         // float
              {
              if(!(w->seek_flags & SEEK_ACTIVE))
                {
                double perc = -1.0;
                
                if(gavl_value_get_float(&val, &perc) &&
                   (perc >= 0.0))
                  {
                  g_signal_handler_block(G_OBJECT(w->seek_slider), w->seek_change_id);
                  gtk_range_set_value(GTK_RANGE(w->seek_slider), perc);
                  g_signal_handler_unblock(G_OBJECT(w->seek_slider), w->seek_change_id);
                  }
                }
              }
            else if(!strcmp(var, BG_PLAYER_STATE_VOLUME))     // float
              {
              g_signal_handler_block(G_OBJECT(w->volume_slider), w->volume_change_id);
              gtk_range_set_value(GTK_RANGE(w->volume_slider), val.v.d);
              g_signal_handler_unblock(G_OBJECT(w->volume_slider), w->volume_change_id);
              
              set_volume_icon(w, val.v.d);
              }
            else if(!strcmp(var, BG_PLAYER_STATE_STATUS))       // int
              {
              GtkStyleContext * context;
              const char * icon = bg_player_get_state_icon(val.v.i);
              gtk_label_set_text(GTK_LABEL(w->status_label), icon);

              context = gtk_widget_get_style_context(w->track_info);

              if(val.v.i == BG_PLAYER_STATUS_ERROR)
                {
                if(gtk_style_context_has_class(context, "track-info"))
                  {
                  gtk_style_context_remove_class(context,"track-info");
                  gtk_style_context_add_class(context,"error-info");
                  }
                }
              else
                {
                if(gtk_style_context_has_class(context, "error-info"))
                  {
                  gtk_style_context_remove_class(context,"error-info");
                  gtk_style_context_add_class(context,"track-info");
                  }
                }
              
              switch(val.v.i)
                {
                case BG_PLAYER_STATUS_STOPPED:
                  gtk_widget_hide(w->track_image);
                  gtk_widget_hide(w->track_icon);
                  gtk_label_set_markup(GTK_LABEL(w->track_info), DEFAULT_LABEL);
                  gtk_widget_set_sensitive(w->seek_slider, FALSE);
                  break;
                case BG_PLAYER_STATUS_ERROR:
                  {
                  gtk_widget_hide(w->track_image);
                  gtk_widget_hide(w->track_icon);
                  
                  gtk_label_set_markup(GTK_LABEL(w->track_info), ERROR_LABEL);
                  gtk_widget_set_sensitive(w->seek_slider, FALSE);
                  bg_gtk_log_window_flush(w->g->log_window);
                  }
                  break;
                case BG_PLAYER_STATUS_PLAYING:
                  w->seek_flags = 0;
                  break;
                }
              
              }
            else if(!strcmp(var, BG_PLAYER_STATE_MODE))       // int
              {
              const char * icon = bg_play_mode_to_icon(val.v.i);
              gtk_label_set_text(GTK_LABEL(w->mode_label), icon);
              }
            else if(!strcmp(var, BG_PLAYER_STATE_CURRENT_TRACK))         // dictionary
              {
              char * markup;
              const gavl_dictionary_t * dict;
              const gavl_dictionary_t * m;
              const gavl_dictionary_t * cl;
              GdkPixbuf * pbuf;
              int num_audio_streams;
              int num_video_streams;
              int num_text_streams;
              int num_overlay_streams;
              int i;
              int can_seek;
              gavl_time_t duration;
              
              duration = GAVL_TIME_UNDEFINED;
              can_seek = 0;
              
              // fprintf(stderr, "Track changed\n");
              
              if(!(dict = gavl_value_get_dictionary(&val)) ||
                 !(m = gavl_track_get_metadata(dict)))
                break;
              
              bg_gtk_trackinfo_set(w->g->info_window, dict);
              
              if((pbuf = bg_gtk_load_track_image(dict, 80, 80)))
                {
                gtk_image_set_from_pixbuf(GTK_IMAGE(w->track_image), pbuf);
                gtk_widget_show(w->track_image);
                gtk_widget_hide(w->track_icon);
                }
              else
                {
                gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Got no track image");
                //  gavl_dictionary_dump(m, 2);
                gtk_widget_hide(w->track_image);
                }
              
              /* TODO: Track icon */
              
              gavl_dictionary_get_long(m, GAVL_META_APPROX_DURATION, &duration);
              
              gavl_dictionary_get_int(m, GAVL_META_CAN_SEEK, &can_seek);
              
              if((duration < 0) && !gavl_dictionary_get(m, GAVL_STATE_SRC_SEEK_WINDOW))
                {
                gtk_range_set_range(GTK_RANGE(w->seek_slider), 0.0, 0.0);
                }
              else
                {
                gtk_range_set_range(GTK_RANGE(w->seek_slider), 0.0, 1.0);
                }
              
              if(can_seek) // Can seek
                {
                gtk_widget_set_sensitive(w->seek_slider, TRUE);
                }
              else
                {
                gtk_widget_set_sensitive(w->seek_slider, FALSE);
                }


              markup = get_track_markup(m);
              gtk_label_set_markup(GTK_LABEL(w->track_info), markup);
              free(markup);

              /* Update menu */
              num_audio_streams   = gavl_track_get_num_audio_streams(dict);
              num_video_streams   = gavl_track_get_num_video_streams(dict);
              num_text_streams    = gavl_track_get_num_text_streams(dict);
              num_overlay_streams = gavl_track_get_num_overlay_streams(dict);
              main_menu_set_num_streams(w->g->main_menu,
                                        num_audio_streams,
                                        num_video_streams,
                                        num_text_streams + num_overlay_streams);
              
              for(i = 0; i < num_audio_streams; i++)
                main_menu_set_audio_info(w->g->main_menu, i, gavl_track_get_audio_metadata(dict, i));

              for(i = 0; i < num_video_streams; i++)
                main_menu_set_video_info(w->g->main_menu, i, gavl_track_get_video_metadata(dict, i));

              for(i = 0; i < num_text_streams; i++)
                main_menu_set_subtitle_info(w->g->main_menu, i, gavl_track_get_text_metadata(dict, i));

              for(i = 0; i < num_overlay_streams; i++)
                main_menu_set_subtitle_info(w->g->main_menu, i + num_text_streams,
                                            gavl_track_get_overlay_metadata(dict, i));

              if((cl = gavl_dictionary_get_chapter_list(m)))
                main_menu_set_chapters(w->g->main_menu, cl);
              
              
              }
            else if(!strcmp(var, BG_PLAYER_STATE_MUTE))         // int
              {
              if(val.v.i)
                gtk_label_set_text(GTK_LABEL(w->volume_label), BG_ICON_VOLUME_MUTE);
              else
                set_volume_icon(w, gtk_range_get_value(GTK_RANGE(w->volume_slider)));
                
              }
            else if(!strcmp(var, BG_PLAYER_STATE_CHAPTER))          // int
              {
              int chapter;
              if(!gavl_value_get_int(&val, &chapter))
                return 1;
              main_menu_chapter_changed(w->g->main_menu, chapter);
              }
            else if(!strcmp(var, BG_PLAYER_STATE_AUDIO_STREAM_CURRENT))          // int
              {
              int stream;
              if(!gavl_value_get_int(&val, &stream))
                return 1;
              main_menu_set_audio_index(w->g->main_menu, stream);
              }
            else if(!strcmp(var, BG_PLAYER_STATE_VIDEO_STREAM_CURRENT))          // int
              {
              int stream;
              if(!gavl_value_get_int(&val, &stream))
                return 1;
              main_menu_set_video_index(w->g->main_menu, stream);
              }
            else if(!strcmp(var, BG_PLAYER_STATE_SUBTITLE_STREAM_CURRENT))          // int
              {
              int stream;
              if(!gavl_value_get_int(&val, &stream))
                return 1;
              main_menu_set_subtitle_index(w->g->main_menu, stream);
              }
            else if(!strcmp(var, BG_PLAYER_STATE_OV_URI))          // string
              {
              // fprintf(stderr, "Got OV uri: %s\n", gavl_value_get_string(&val));
              main_menu_set_ov_uri(w->g->main_menu, gavl_value_get_string(&val));
              }
            else if(!strcmp(var, BG_PLAYER_STATE_OA_URI))          // string
              {
              // fprintf(stderr, "Got OA uri: %s\n", gavl_value_get_string(&val));
              main_menu_set_oa_uri(w->g->main_menu, gavl_value_get_string(&val));
              }
#if 0
            else
              fprintf(stderr, "State changed %s\n", var);
#endif
            }
          else if(!strcmp(ctx, BG_GTK_LOGWINDOW_STATE_CTX))
            {
            if(!strcmp(var, BG_GTK_LOGWINDOW_VISIBLE))
              {
              int enable = 0;
              if(!gavl_value_get_int(&val, &enable))
                return 1;
              main_menu_set_log_window_item(w->g->main_menu, enable);
              }
            }
          else if(!strcmp(ctx, GAVL_STATE_CTX_SRC))
            {
            fprintf(stderr, "src state changed: %s\n", var);
            gavl_value_dump(&val, 2);
            }
          gavl_value_free(&val);
          }
          break;
        }
      break;





    case BG_MSG_NS_PLAYER:
      {
      int arg_i_1;
      switch(msg->ID)
        {
        case BG_PLAYER_MSG_ACCEL:
          arg_i_1 = gavl_msg_get_arg_int(msg, 0);
          switch(arg_i_1)
            {
            case BG_PLAYER_ACCEL_PLAY:
              {
              const gavl_value_t * status_val;
              
              if(!(status_val = bg_state_get(&w->g->state,
                                             BG_PLAYER_STATE_CTX,
                                             BG_PLAYER_STATE_STATUS)))
                break;
              
              if(status_val->v.i == BG_PLAYER_STATUS_PLAYING)
                return 1;
              else if(status_val->v.i == BG_PLAYER_STATUS_PAUSED)
                bg_player_pause(w->player_ctrl.cmd_sink);
              else
                bg_player_play(w->player_ctrl.cmd_sink);
              break;
              }
            case ACCEL_QUIT:
              bg_gtk_quit();
              return 0;
              break;
            case ACCEL_CURRENT_TO_FAVOURITES:
              // bg_media_tree_copy_current_to_favourites(win->gmerlin->tree);
              return 1;
              break;
            case ACCEL_OPTIONS:
              g_idle_add(do_configure, w->g);
              break;
            case ACCEL_GOTO_CURRENT:
              // bg_gtk_tree_window_goto_current(win->gmerlin->tree_window);
              break;
              }
              break;
            }
          
        }
      

    }
  return 1;
  }

static gboolean idle_callback(gpointer data)
  {
  main_window_t * w = data;
  bg_msg_sink_iteration(w->player_ctrl.evt_sink);
  
  /* Handle remote registry */
  main_menu_ping(w->g->main_menu);

  return TRUE;
  }

static void mode_pressed(GtkGestureMultiPress *gesture,
                         int                   n_press,
                         double                x,
                         double                y,
                         gpointer              user_data)
  {
  main_window_t * w = user_data;

  gavl_value_t val;
  gavl_msg_t * msg;
  
  gavl_value_init(&val);
  gavl_value_set_int(&val, 1); // Toggle
    
  msg = bg_msg_sink_get(w->player_ctrl.cmd_sink);

  gavl_msg_set_state(msg,
                     BG_CMD_SET_STATE_REL,
                     1,
                     BG_PLAYER_STATE_CTX,
                     BG_PLAYER_STATE_MODE,
                     &val);
    
  bg_msg_sink_put(w->player_ctrl.cmd_sink);
  }

static void time_pressed(GtkGestureMultiPress *gesture,
                         int                   n_press,
                         double                x,
                         double                y,
                         gpointer              user_data)
  {
  main_window_t * w = user_data;

  w->display_mode++;
  if(w->display_mode >= NUM_DISPLAY_MODES)
    w->display_mode = DISPLAY_MODE_NORMAL;
  set_display_mode(w);
  
  }



void main_window_init(main_window_t * ret, gmerlin_t * g)
  {
  GtkWidget * main_grid;
  GtkWidget * subgrid;
  GtkWidget * subsubgrid;
  GtkWidget * headerbar;
  char * path;

  GtkGesture *gesture;
  
  ret->g = g;
  
  /* Load css stuff */
  ret->icon_provider = gtk_css_provider_new();
  ret->skin_provider = gtk_css_provider_new();

  if((path = bg_search_file_read("player", "gmerlin-player.css")))
    {
    gtk_css_provider_load_from_path(ret->skin_provider,
                                    path,
                                    NULL);
    free(path);
    }

  gtk_css_provider_load_from_data(ret->icon_provider,
                                  icon_css, -1,
                                  NULL);
  
  
  ret->win = bg_gtk_window_new(GTK_WINDOW_TOPLEVEL);

  set_css_class(ret->win, ret->skin_provider, NULL, "playerwindow");
  
  /* Header bar */
  headerbar = gtk_header_bar_new();
  set_css_class(headerbar, ret->skin_provider, NULL, "windowheader");

  gtk_window_set_titlebar(GTK_WINDOW(ret->win), headerbar);
  gtk_header_bar_set_title(GTK_HEADER_BAR(headerbar), "Gmerlin Player");
  
  gtk_header_bar_set_has_subtitle(GTK_HEADER_BAR(headerbar), FALSE);
  ret->close_button = title_button_create(ret, BG_ICON_X);
  ret->menu_button =  title_button_create(ret, BG_ICON_MENU);

  gtk_header_bar_pack_start(GTK_HEADER_BAR(headerbar), ret->menu_button);
  gtk_header_bar_pack_end(GTK_HEADER_BAR(headerbar), ret->close_button);
  
  gtk_widget_show(headerbar);

  /* */
  
  main_grid = gtk_grid_new();

  /* Display grid */
  subgrid = gtk_grid_new();

  /* Upper row */
  subsubgrid = gtk_grid_new();
  
  ret->status_label = gtk_label_new(BG_ICON_STOP);
  set_css_class(ret->status_label, ret->skin_provider, ret->icon_provider, "status-label");
  gtk_widget_set_hexpand(ret->status_label, FALSE);
  gtk_widget_set_vexpand(ret->status_label, FALSE);
  gtk_widget_set_halign(ret->status_label, GTK_ALIGN_START); 
  gtk_widget_show(ret->status_label);
  
  ret->mode_label = gtk_label_new(BG_ICON_ARROW_RIGHT);
  set_css_class(ret->mode_label, ret->skin_provider, ret->icon_provider, "mode-label");
  gtk_widget_set_hexpand(ret->mode_label, FALSE);
  gtk_widget_set_vexpand(ret->mode_label, FALSE);
  gtk_widget_set_halign(ret->mode_label, GTK_ALIGN_START); 
  gtk_widget_show(ret->mode_label);

  ret->mode_box = gtk_event_box_new();
  bg_gtk_tooltips_set_tip(ret->mode_box,
                          "Playback mode\nClick to change",
                          PACKAGE);

  gesture = gtk_gesture_multi_press_new(ret->mode_box);
  g_signal_connect(G_OBJECT(gesture), "pressed", G_CALLBACK(mode_pressed), ret);
  
  
  //  gtk_widget_add_controller(ret->mode_box, GTK_EVENT_CONTROLLER(gesture));
  
  gtk_container_add(GTK_CONTAINER(ret->mode_box), ret->mode_label);
  gtk_widget_set_hexpand(ret->mode_box, FALSE);
  gtk_widget_set_vexpand(ret->mode_box, FALSE);
  gtk_widget_set_halign(ret->mode_label, GTK_ALIGN_START); 

  gtk_widget_show(ret->mode_box);
  
  
  gtk_grid_attach(GTK_GRID(subsubgrid), ret->status_label, 0, 0, 1, 2);

  gtk_grid_attach(GTK_GRID(subsubgrid), ret->mode_box, 1, 0, 1, 2);

  
  ret->time_label = gtk_label_new("0:00");
  set_css_class(ret->time_label, ret->skin_provider, NULL, "time-label");
  gtk_widget_set_hexpand(ret->time_label, TRUE);
  gtk_widget_set_halign(ret->time_label, GTK_ALIGN_END); 
  gtk_widget_show(ret->time_label);
  
  ret->time_box = gtk_event_box_new();

  gesture = gtk_gesture_multi_press_new(ret->time_box);
  g_signal_connect(G_OBJECT(gesture), "pressed", G_CALLBACK(time_pressed), ret);
  
  
  gtk_widget_set_hexpand(ret->time_box, TRUE);
  gtk_widget_set_vexpand(ret->time_box, FALSE);
  gtk_widget_set_halign(ret->time_box, GTK_ALIGN_END); 
  bg_gtk_tooltips_set_tip(ret->time_box,
                          "Click to change time display mode",
                          PACKAGE);
  
  gtk_widget_show(ret->time_box);
  gtk_container_add(GTK_CONTAINER(ret->time_box), ret->time_label);
  
  gtk_grid_attach(GTK_GRID(subsubgrid), ret->time_box, 2, 0, 1, 2);

  ret->all_label = gtk_label_new("ALL");
  gtk_widget_set_hexpand(ret->all_label, FALSE);
  gtk_widget_set_vexpand(ret->all_label, FALSE);
  set_css_class(ret->all_label, ret->skin_provider, NULL, "all-label");

  gtk_widget_show(ret->all_label);
  
  gtk_grid_attach(GTK_GRID(subsubgrid), ret->all_label, 3, 0, 1, 1);
  
  ret->rem_label = gtk_label_new("REM");
  gtk_widget_set_hexpand(ret->rem_label, FALSE);
  gtk_widget_set_vexpand(ret->rem_label, FALSE);
  set_css_class(ret->rem_label, ret->skin_provider, NULL, "rem-label");
  gtk_widget_show(ret->rem_label);
  
  gtk_grid_attach(GTK_GRID(subsubgrid), ret->rem_label, 3, 1, 1, 1);

  
  gtk_widget_set_hexpand(subgrid, TRUE);
  gtk_widget_set_vexpand(subgrid, TRUE);

  set_css_class(subgrid, ret->skin_provider, NULL, "display");

  gtk_widget_show(subsubgrid);
  gtk_grid_attach(GTK_GRID(subgrid), subsubgrid, 0, 0, 1, 1);

  /* Lower row */

  subsubgrid = gtk_grid_new();
  set_css_class(subsubgrid, ret->skin_provider, NULL, "track-display");
  
  ret->track_image = gtk_image_new();
  set_css_class(ret->track_image, ret->skin_provider, NULL, "track-image");
  gtk_grid_attach(GTK_GRID(subsubgrid), ret->track_image, 0, 0, 1, 1);

  gtk_widget_set_valign(ret->track_image, GTK_ALIGN_CENTER); 
  gtk_widget_set_halign(ret->track_image, GTK_ALIGN_START); 
  gtk_widget_set_hexpand(ret->track_image, FALSE);
  
  ret->track_icon = gtk_label_new(NULL);
  set_css_class(ret->track_icon, ret->skin_provider, ret->icon_provider, "track-icon");
  gtk_grid_attach(GTK_GRID(subsubgrid), ret->track_icon, 1, 0, 1, 1);

  ret->track_info = gtk_label_new(DEFAULT_LABEL);
  set_css_class(ret->track_info, ret->skin_provider, NULL, "track-info");

  gtk_label_set_line_wrap(GTK_LABEL(ret->track_info), TRUE);

  gtk_widget_set_valign(ret->track_info, GTK_ALIGN_START); 
  gtk_widget_set_halign(ret->track_info, GTK_ALIGN_START); 
  gtk_widget_set_hexpand(ret->track_info, TRUE);
  
  
  gtk_widget_show(ret->track_info);
  gtk_grid_attach(GTK_GRID(subsubgrid), ret->track_info, 2, 0, 1, 1);

  gtk_widget_show(subsubgrid);
  gtk_grid_attach(GTK_GRID(subgrid), subsubgrid, 0, 1, 1, 1);
  
#if 0
    {
    GValue val;
    GtkStyleContext* c = gtk_widget_get_style_context(subsubgrid);

    gtk_style_context_get_style_property(c, "min-width", &val);
    fprintf(stderr, "max_width: %d\n", g_value_get_int(&val));
    g_value_unset(&val);

    gtk_style_context_get_style_property(c, "min-height", &val);
    fprintf(stderr, "max_height: %d\n", g_value_get_int(&val));
    g_value_unset(&val);
    }
#endif
  
  /* Pack display grid */
  
  gtk_widget_show(subgrid);
  
  gtk_grid_attach(GTK_GRID(main_grid), subgrid, 0, 0, 1, 1);
  
  /* Seek slider */
  
  //  subgrid = gtk_grid_new();
  //  gtk_grid_set_column_homogeneous(GTK_GRID(subgrid),
  //                                  TRUE);
  
  ret->seek_slider = gtk_scale_new_with_range (GTK_ORIENTATION_HORIZONTAL,
                                               0.0, 1.0, 0.01);

  set_css_class(ret->seek_slider, ret->skin_provider, NULL, "seek-slider");

  ret->seek_change_id =
    g_signal_connect(G_OBJECT(ret->seek_slider), "value-changed", G_CALLBACK(scale_value_changed), ret);
  
  g_signal_connect(G_OBJECT(ret->seek_slider), "button-press-event", G_CALLBACK(button_press_event), ret);
  g_signal_connect(G_OBJECT(ret->seek_slider), "button-release-event", G_CALLBACK(button_release_event), ret);

  
  gtk_scale_set_draw_value(GTK_SCALE(ret->seek_slider), FALSE);
  gtk_widget_show(ret->seek_slider);
  gtk_grid_attach(GTK_GRID(main_grid), ret->seek_slider, 0, 1, 1, 1);
  
  /* Player buttons and volume */
  
  subgrid = gtk_grid_new();
  
  ret->play_button  = player_button_create(ret, BG_ICON_PLAY);
  ret->stop_button  = player_button_create(ret, BG_ICON_STOP);
  ret->pause_button = player_button_create(ret, BG_ICON_PAUSE);
  ret->next_button  = player_button_create(ret, BG_ICON_NEXT);
  ret->prev_button  = player_button_create(ret, BG_ICON_PREV);
  
  /* Volume label */
  ret->volume_label = gtk_label_new(BG_ICON_VOLUME_MIN);

  set_css_class(ret->volume_label, ret->skin_provider, ret->icon_provider, "volume-label");
  
  gtk_widget_set_vexpand(ret->volume_label, FALSE);
  gtk_widget_set_valign(ret->volume_label, GTK_ALIGN_CENTER); 

  gtk_widget_show(ret->volume_label);
  
  /* Volume slider */
  
  ret->volume_slider = gtk_scale_new_with_range (GTK_ORIENTATION_HORIZONTAL,
                                                 0.0, 1.0, 0.01);

  ret->volume_change_id =
    g_signal_connect(G_OBJECT(ret->volume_slider), "value-changed", G_CALLBACK(scale_value_changed), ret);

  g_signal_connect(G_OBJECT(ret->volume_slider), "button-press-event", G_CALLBACK(button_press_event), ret);
  g_signal_connect(G_OBJECT(ret->volume_slider), "button-release-event", G_CALLBACK(button_release_event), ret);

  
  set_css_class(ret->volume_slider, ret->skin_provider, NULL, "volume-slider");
  
  gtk_scale_set_draw_value(GTK_SCALE(ret->volume_slider), FALSE);

  gtk_widget_set_hexpand(ret->volume_slider, TRUE);
  gtk_widget_set_halign(ret->volume_slider, GTK_ALIGN_FILL); 
  
  gtk_widget_show(ret->volume_slider);
  
  gtk_grid_attach(GTK_GRID(subgrid), ret->prev_button, 0, 0, 1, 1);

  gtk_grid_attach(GTK_GRID(subgrid), ret->stop_button, 1, 0, 1, 1);

  gtk_grid_attach(GTK_GRID(subgrid), ret->play_button, 2, 0, 1, 1);

  gtk_grid_attach(GTK_GRID(subgrid), ret->pause_button, 3, 0, 1, 1);
  
  gtk_grid_attach(GTK_GRID(subgrid), ret->next_button, 4, 0, 1, 1);

  gtk_grid_attach(GTK_GRID(subgrid), ret->volume_label, 5, 0, 1, 1);

  gtk_grid_attach(GTK_GRID(subgrid), ret->volume_slider, 6, 0, 1, 1);
  
  gtk_widget_set_vexpand(subgrid, FALSE);
  gtk_widget_set_valign(subgrid, GTK_ALIGN_FILL); 
  
  gtk_widget_set_hexpand(subgrid, TRUE);
  gtk_widget_show(subgrid);
  
  gtk_grid_attach(GTK_GRID(main_grid), subgrid, 0, 2, 2, 1);
  /* */
  
  gtk_widget_show(main_grid);
  gtk_container_add(GTK_CONTAINER(ret->win), main_grid);
  
  bg_control_init(&ret->player_ctrl, bg_msg_sink_create(handle_player_message_gmerlin, ret, 0));
  
  g_timeout_add(DELAY_TIME, idle_callback, (gpointer)ret);
  set_display_mode(ret);  
  gtk_window_add_accel_group (GTK_WINDOW(ret->win), ret->g->accel_group);
  
  }

void main_window_show(main_window_t * w)
  {
  gtk_widget_show(w->win);
  }

void main_window_connect(main_window_t * w)
  {
  bg_controllable_connect(w->g->player_ctrl, &w->player_ctrl);
  }

void main_window_disconnect(main_window_t * w)
  {
  bg_controllable_disconnect(w->g->player_ctrl, &w->player_ctrl);
  }
