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

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <config.h>

#include <gmerlin/translation.h>
#include <gmerlin/application.h>
#include <gavl/keycodes.h>
 
#include "gmerlin.h"
#include "player_remote.h"

#include <gmerlin/utils.h>
#include <gui_gtk/auth.h>
#include <gui_gtk/gtkutils.h>

#include <gavl/metatags.h>


const char * client_config_vars[] =
  {
    "cfg",
    NULL,
  };


static bg_accelerator_t accels[] =
  {
    { GAVL_KEY_q,         GAVL_KEY_CONTROL_MASK,                   ACCEL_QUIT                    },
    { GAVL_KEY_o,         GAVL_KEY_CONTROL_MASK,                   ACCEL_OPTIONS                 },
    { GAVL_KEY_g,         GAVL_KEY_CONTROL_MASK,                   ACCEL_GOTO_CURRENT            },
    { GAVL_KEY_F9,        0,                                     ACCEL_CURRENT_TO_FAVOURITES   },
    { GAVL_KEY_NONE,      0,                                     0                             },
  };

static gboolean delete_callback(GtkWidget * w, GdkEventAny * event,
                                gpointer data)
  {
  gmerlin_t * g = (gmerlin_t*)data;
  
  if(w == bg_gtk_info_window_get_widget(g->info_window))
    {
    gtk_widget_hide(w);
    main_menu_set_info_window_item(g->main_menu, FALSE);
    return TRUE;
    }
  else if(w == bg_gtk_log_window_get_widget(g->log_window))
    {
    gtk_widget_hide(w);
    main_menu_set_log_window_item(g->main_menu, FALSE);
    return TRUE;
    }
  else if(w == g->mdb_window)
    {
    gtk_widget_hide(w);
    main_menu_set_mdb_window_item(g->main_menu, FALSE);
    return TRUE;
    }
  
  return FALSE;
  }
                

                

static void gmerlin_apply_config(gmerlin_t * g)
  {
  const bg_parameter_info_t * parameters;

  bg_cfg_ctx_apply_array(g->cfg_player);
  
  parameters = bg_http_server_get_parameters(g->srv);
  bg_cfg_section_apply(g->remote_section, parameters,
                       bg_http_server_set_parameter, (void*)(g->srv));

  parameters = bg_media_dirs_get_parameters();
  bg_cfg_section_apply(g->remote_section, parameters,
                       bg_http_server_set_parameter, (void*)(g->srv));

  
  parameters = gmerlin_get_parameters(g);

  bg_cfg_section_apply(g->general_section, parameters,
                       gmerlin_set_parameter, (void*)(g));
#if 0
  parameters = bg_lcdproc_get_parameters(g->lcdproc);
  bg_cfg_section_apply(g->lcdproc_section, parameters,
                       bg_lcdproc_set_parameter, (void*)(g->lcdproc));
#endif
  parameters = bg_gtk_log_window_get_parameters(g->log_window);
  bg_cfg_section_apply(g->logwindow_section, parameters,
                       bg_gtk_log_window_set_parameter, (void*)(g->log_window));

  
  }

static void gmerlin_apply_state(gmerlin_t * g)
  {
  bg_state_apply(&g->state, g->player_ctrl->cmd_sink, BG_CMD_SET_STATE);
  }

static void gmerlin_get_config(gmerlin_t * g)
  {
  const bg_parameter_info_t * parameters;
#if 0
  parameters = display_get_parameters(g->player_window->display);

  bg_cfg_section_apply(g->display_section, parameters,
                       display_set_parameter, (void*)(g->player_window->display));
  parameters = bg_media_tree_get_parameters(g->tree);
  bg_cfg_section_apply(g->tree_section, parameters,
                       bg_media_tree_set_parameter, (void*)(g->tree));

  parameters = bg_player_get_audio_parameters(g->player);
  
  bg_cfg_section_apply(g->audio_section, parameters,
                       bg_player_set_audio_parameter, (void*)(g->player));

  parameters = bg_player_get_audio_filter_parameters(g->player);
  
  bg_cfg_section_apply(g->audio_filter_section, parameters,
                       bg_player_set_audio_filter_parameter, (void*)(g->player));

  parameters = bg_player_get_video_parameters(g->player);
  
  bg_cfg_section_apply(g->video_section, parameters,
                       bg_player_set_video_parameter, (void*)(g->player));

  parameters = bg_player_get_video_filter_parameters(g->player);
  
  bg_cfg_section_apply(g->video_filter_section, parameters,
                       bg_player_set_video_filter_parameter,
                       (void*)(g->player));
  
  parameters = bg_player_get_subtitle_parameters(g->player);
  
  bg_cfg_section_apply(g->subtitle_section, parameters,
                       bg_player_set_subtitle_parameter, (void*)(g->player));
#endif

  parameters = gmerlin_get_parameters(g);

  bg_cfg_section_get(g->general_section, parameters,
                     gmerlin_get_parameter, (void*)(g));
  
  }


static const bg_parameter_info_t input_plugin_parameters[] =
  {
    {
      .name = "input_plugins",
      .long_name = "Input plugins",
      .flags = BG_PARAMETER_PLUGIN,
    },
    { /* */ },
  };

static const bg_parameter_info_t image_reader_parameters[] =
  {
    {
      .name = "image_readers",
      .long_name = "Image readers",
      .flags = BG_PARAMETER_PLUGIN,
    },
    { /* */ },
  };

void gmerlin_connect_player(gmerlin_t * gmerlin)
  {
  bg_gtk_mdb_tree_set_player_ctrl(gmerlin->mdb_tree, gmerlin->player_ctrl);

  main_window_connect(&gmerlin->mainwin);
  }

void gmerlin_disconnect_player(gmerlin_t * gmerlin)
  {
  bg_gtk_mdb_tree_unset_player_ctrl(gmerlin->mdb_tree);
  
  main_window_disconnect(&gmerlin->mainwin);
  }

void gmerlin_connect_mdb(gmerlin_t * gmerlin)
  {
  gmerlin->mdb_tree = bg_gtk_mdb_tree_create(gmerlin->mdb_ctrl);
  gtk_container_add(GTK_CONTAINER(gmerlin->mdb_window),
                    bg_gtk_mdb_tree_get_widget(gmerlin->mdb_tree));
  bg_gtk_mdb_tree_set_player_ctrl(gmerlin->mdb_tree, gmerlin->player_ctrl);
  }

void gmerlin_disconnect_mdb(gmerlin_t * gmerlin)
  {
  GtkWidget * w = bg_gtk_mdb_tree_get_widget(gmerlin->mdb_tree);
  
  bg_gtk_mdb_tree_destroy(gmerlin->mdb_tree);
  gtk_container_remove(GTK_CONTAINER(gmerlin->mdb_window), w);
  gmerlin->mdb_tree = NULL;
  }

static int handle_http_backends(bg_http_connection_t * conn, void * priv)
  {
  int ret = 0;
  
  gmerlin_t * g = priv;
  
  if(!gavl_string_starts_with(conn->path, "/backend/"))
    return 0; 

  conn->path += 9;

  if(g->player_backend && gavl_string_starts_with(conn->path, "renderer/"))
    {
    conn->path += 9;
    ret += bg_backend_handle_handle(conn, g->player_backend);
    }
  else if(g->mdb_backend && gavl_string_starts_with(conn->path, "server/"))
    {
    conn->path += 7;
    ret += bg_backend_handle_handle(conn, g->player_backend);
    }
  return ret;
  }

static int handle_http_client_config(bg_http_connection_t * conn, void * priv)
  {
  gmerlin_t * g = priv;

  /* Check if we need to send the default renderer */
  if(!strcmp(conn->method, "GET") && gavl_string_starts_with(conn->path, "cfg?"))
    {
    const char * client_id;
    client_id = gavl_dictionary_get_string(&conn->url_vars, BG_URL_VAR_CLIENT_ID);

    if(!client_id)
      return 0;

    // fprintf(stderr, "Blupp handle_http_client_config %s\n", client_id);
    
    if(!bg_server_storage_get(g->client_config, client_id, "cfg", NULL))
      {
      const char * root_uri;
      
      char * host = NULL;
      int port = 80;
      /* Create a default value */
      char * cfg;

      root_uri = bg_http_server_get_root_url(g->srv);

      bg_url_split(root_uri, NULL, NULL, NULL, &host, &port, NULL);
      
      cfg = bg_sprintf("{\"renderer\":\"gmerlin-renderer://%s:%d/ws/renderer\",\"style\":\"dark\"}", host, port);
      
      bg_server_storage_put(g->client_config, client_id, "cfg", cfg, strlen(cfg));
      free(cfg);
      }

    }
  
  return bg_server_storage_handle_http(conn, g->client_config);
  //  return 0;
  }

static const char * manifest_file =
  "{\n"
    "\"short_name\": \"%s\",\n"
    "\"name\": \"%s\",\n"
    "\"display\": \"standalone\",\n"
    "\"icons\": [ \n"
      "{\n"
      "\"src\": \"static/icons/player_16.png\",\n"
      "\"sizes\": \"16x16\",\n"
      "\"type\": \"image/png\",\n"
      "\"density\": 1.0\n"
      "},\n"
      "{\n"
      "\"src\": \"static/icons/player_48.png\",\n"
      "\"sizes\": \"48x48\",\n"
      "\"type\": \"image/png\",\n"
      "\"density\": 1.0\n"
      "},\n"
      "{\n"
      "\"src\": \"static/icons/player_96.png\",\n"
      "\"sizes\": \"96x96\",\n"
      "\"type\": \"image/png\",\n"
      "\"density\": 1.0\n"
      "} ],\n"
  "\"start_url\": \"%s\"\n"
  "}\n";

static int server_handle_manifest(bg_http_connection_t * conn, void * data)
  {
  const char * var;
  int result = 0;
  
  if(!strcmp(conn->path, "/manifest.json"))
    {
    char * start_url = NULL;
    
    if((var = gavl_dictionary_get_string(&conn->req, "Referer")))
      {
      gavl_dictionary_t url_vars;
      char * protocol = NULL;
      char * host = NULL;
      int port = 0;
      const char * cid;
      char * m = NULL;
      int len = 0;
      
      bg_http_connection_check_keepalive(conn);
      
      gavl_dictionary_init(&url_vars);
      //      fprintf(stderr, "Referer: %s\n", var);
      bg_url_split(var, &protocol, NULL, NULL, &host, &port, NULL);

      bg_url_get_vars_c(var, &url_vars);
      if((cid = gavl_dictionary_get_string(&url_vars, BG_URL_VAR_CLIENT_ID)))
        {
        start_url = bg_sprintf("%s://%s:%d/?%s=%s", protocol, host, port,
                               BG_URL_VAR_CLIENT_ID, cid);
        m = bg_sprintf(manifest_file, bg_app_get_label(), bg_app_get_label(), start_url);
        len = strlen(m);
        
        bg_http_connection_init_res(conn, conn->protocol, 200, "OK");
        gavl_dictionary_set_string(&conn->res, "Content-Type", "application/manifest+json");
        gavl_dictionary_set_long(&conn->res, "Content-Length", len);

        if(!bg_http_connection_write_res(conn))
          {
          bg_http_connection_clear_keepalive(conn);
          goto cleanup;
          }
        result = 1;
        }

      if(!result)
        {
        bg_http_connection_init_res(conn, conn->protocol, 400, "Bad Request");
        if(!bg_http_connection_write_res(conn))
          {
          bg_http_connection_clear_keepalive(conn);
          goto cleanup;
          }
        }
      
      if(result && !gavl_socket_write_data(conn->fd, (const uint8_t*)m, len))
        bg_http_connection_clear_keepalive(conn);
      
      cleanup:

      if(protocol)
        free(protocol);
      if(host)
        free(host);
      
      gavl_dictionary_free(&url_vars);

      //      fprintf(stderr, "Got manifest:\n%s\n", m);
      
      if(m)
        free(m);
      
      }
    
    return result;
    }
  return 0;
  }



gmerlin_t * gmerlin_create(const gavl_dictionary_t * saved_state, const char * db_path)
  {
  gmerlin_t * ret;
  bg_cfg_section_t * cfg_section;
  char * network_name;
  char * tmp_string;

  gavl_dictionary_t * node_vars = gavl_dictionary_get_dictionary_create(&bg_app_vars, BG_APP_STATE_NETWORK_NODE);
  
  network_name = bg_sprintf("Gmerlin player");

  gavl_dictionary_set_string(node_vars, GAVL_META_LABEL, network_name);
  
  //  gavl_dictionary_t root_metadata;
  
  ret = calloc(1, sizeof(*ret));

  pthread_mutex_init(&ret->stop_mutex, NULL);
  
  tmp_string = bg_search_var_dir("player/client_config");
  ret->client_config = bg_server_storage_create(tmp_string, 16, client_config_vars);
  free(tmp_string);
  
  ret->display_section =
    bg_cfg_registry_find_section(bg_cfg_registry, "Display");
  ret->tree_section =
    bg_cfg_registry_find_section(bg_cfg_registry, "Tree");
  ret->general_section =
    bg_cfg_registry_find_section(bg_cfg_registry, "General");
  ret->lcdproc_section =
    bg_cfg_registry_find_section(bg_cfg_registry, "LCDproc");
  ret->remote_section =
    bg_cfg_registry_find_section(bg_cfg_registry, "Remote");
  ret->logwindow_section =
    bg_cfg_registry_find_section(bg_cfg_registry, "Logwindow");

  ret->input_plugin_parameters =
    bg_parameter_info_copy_array(input_plugin_parameters);
  bg_plugin_registry_set_parameter_info_input(bg_plugin_reg,
                                              BG_PLUGIN_INPUT,
                                              BG_PLUGIN_FILE|
                                              BG_PLUGIN_URL|
                                              BG_PLUGIN_REMOVABLE|
                                              BG_PLUGIN_TUNER,
                                              ret->input_plugin_parameters);
  
  ret->image_reader_parameters =
    bg_parameter_info_copy_array(image_reader_parameters);
  bg_plugin_registry_set_parameter_info_input(bg_plugin_reg,
                                              BG_PLUGIN_IMAGE_READER,
                                              BG_PLUGIN_FILE,
                                              ret->image_reader_parameters);
  

  
  /* Create player instance */
  
  ret->player = bg_player_create();
  ret->player_ctrl = bg_player_get_controllable(ret->player);
 
  
  ret->cfg_player = bg_cfg_ctx_copy_array(bg_player_get_cfg(ret->player));
  
  bg_cfg_ctx_set_cb_array(ret->cfg_player,
                          NULL, NULL);
  bg_cfg_ctx_set_sink_array(ret->cfg_player,
                            ret->player_ctrl->cmd_sink);
  
  bg_player_add_accelerators(ret->player, accels);
  
  cfg_section = bg_cfg_registry_find_section(bg_cfg_registry, "player");
  bg_cfg_ctx_array_create_sections(ret->cfg_player, cfg_section);
  bg_player_apply_cmdline(ret->cfg_player);
  
  // Called from gmerlin_run()
  // bg_cfg_ctx_apply_array(ret->cfg_player);
  
  /* Media DB */
  
  /* TODO: Maybe add PID and hostname */
  /* TODO: Add icons */
  
  ret->srv = bg_http_server_create();

  if(!(ret->mdb = bg_mdb_create(db_path, 1, ret->srv)))
    goto fail;

  /* From here we can direct log messages to the GUI */
  ret->log_window = bg_gtk_log_window_create(TR("Gmerlin player"));
  bg_cfg_section_apply(ret->logwindow_section,
                       bg_gtk_log_window_get_parameters(ret->log_window),
                       bg_gtk_log_window_set_parameter,
                       (void*)ret->log_window);

  g_signal_connect(G_OBJECT(bg_gtk_log_window_get_widget(ret->log_window)),
                   "delete_event",
                   G_CALLBACK(delete_callback), ret);
  
  bg_mdb_set_root_name(ret->mdb, network_name);
  
  ret->cfg_mdb = bg_mdb_get_cfg(ret->mdb);
  
  //  gavl_dictionary_free(&root_metadata);
  
  ret->mdb_ctrl = bg_mdb_get_controllable(ret->mdb);
  
  /* Start creating the GUI */

  ret->accel_group = gtk_accel_group_new();
  
  ret->mdb_tree = bg_gtk_mdb_tree_create(ret->mdb_ctrl);
  
  ret->mdb_window = bg_gtk_window_new(GTK_WINDOW_TOPLEVEL);
  g_signal_connect(ret->mdb_window,
                   "delete_event",
                   G_CALLBACK(delete_callback), ret);
  gtk_window_set_title(GTK_WINDOW(ret->mdb_window), TR("Gmerlin Media DB"));

  
  gtk_container_add(GTK_CONTAINER(ret->mdb_window),
                    bg_gtk_mdb_tree_get_widget(ret->mdb_tree));

  gtk_window_set_default_size(GTK_WINDOW(ret->mdb_window),
                              600, 400);
  
  
  /* Needs to come early */
  //  bg_control_init(&ret->ctrl, bg_msg_sink_create(gmerlin_handle_message, ret, 0));
  
  /* Create player window */
  
  main_window_init(&ret->mainwin, ret);
  

  gtk_window_add_accel_group(GTK_WINDOW(ret->mdb_window), ret->accel_group);
  
  //  gmerlin_skin_load(&ret->skin, "Default");
  //  gmerlin_skin_set(ret);

  /* Create subwindows */

  ret->info_window = bg_gtk_info_window_create();
  g_signal_connect(G_OBJECT(bg_gtk_info_window_get_widget(ret->info_window)),
                   "delete_event",
                   G_CALLBACK(delete_callback), ret);
  
  //  ret->lcdproc = bg_lcdproc_create(ret->player);

  bg_http_server_set_generate_client_ids(ret->srv);
  bg_http_server_set_root_file(ret->srv, "/static/app.html");

  bg_http_server_add_handler(ret->srv, server_handle_manifest, BG_HTTP_PROTO_HTTP, NULL, NULL);
  
  bg_http_server_add_handler(ret->srv,
                             handle_http_backends,
                             BG_HTTP_PROTO_HTTP,
                             NULL, // E.g. /static/ can be NULL
                             ret);
  
  bg_http_server_set_static_path(ret->srv, "/static");
  bg_http_server_set_mdb(ret->srv, ret->mdb);

  bg_http_server_add_handler(ret->srv,
                             handle_http_client_config,
                             BG_HTTP_PROTO_HTTP,
                             "/storage/", // E.g. /static/ can be NULL
                             ret);
  
  gmerlin_connect_player(ret);
  
  bg_http_server_set_default_port(ret->srv, PLAYER_REMOTE_PORT);
  
  gmerlin_create_dialog(ret);
  
  bg_player_state_init(&ret->state, NULL, NULL, NULL);
  /* Apply the state before the frontends are created */

  if(saved_state)
    {
    bg_state_merge(&ret->state, saved_state);
    bg_player_state_reset(&ret->state);
    }
  
#ifdef HAVE_DBUS
  ret->dbus_frontend =
    bg_frontend_create_player_mpris2(ret->player_ctrl,
                                     "org.mpris.MediaPlayer2.gmerlin-player",
                                     "gmerlin-player");
#endif

  
  ret->upnp_renderer_frontend =
    bg_frontend_create_player_upnp(ret->srv,
                                   ret->player_ctrl);
  
  ret->upnp_server_frontend =
    bg_frontend_create_mdb_upnp(ret->srv,
                                ret->mdb_ctrl);
  
  ret->gmerlin_server_frontend =
    bg_frontend_create_mdb_gmerlin(ret->srv,
                                   ret->mdb_ctrl);

  ret->gmerlin_renderer_frontend =
    bg_frontend_create_player_gmerlin(ret->srv,
                                      ret->player_ctrl);
  
  
  free(network_name);

  ret->main_menu = main_menu_create(ret);

  gtk_widget_show(ret->mdb_window);
  main_menu_set_mdb_window_item(ret->main_menu, 1);
  
  return ret;
  
  fail:
  gmerlin_destroy(ret);
  return NULL;
  }

void gmerlin_destroy(gmerlin_t * g)
  {
  /* Join frontend thread */
  pthread_mutex_lock(&g->stop_mutex);
  if(g->frontend_thread_running)
    {
    g->stop = 1;
    pthread_mutex_unlock(&g->stop_mutex);
    pthread_join(g->frontend_thread, NULL);
    }
  else
    pthread_mutex_unlock(&g->stop_mutex);

  /* Shut down mdb threads */
  if(g->mdb)
    bg_mdb_stop(g->mdb);
  
  /* Must destroy the dialogs early, because the
     destructors might reference parameter infos,
     which belong to other modules */
  if(g->cfg_dialog)
    bg_dialog_destroy(g->cfg_dialog);
  
  //  bg_lcdproc_destroy(g->lcdproc);
  if(g->srv)
    bg_http_server_destroy(g->srv);

  
#ifdef HAVE_DBUS
  if(g->dbus_frontend)
    bg_frontend_destroy(g->dbus_frontend);
#endif

  if(g->upnp_server_frontend)
    bg_frontend_destroy(g->upnp_server_frontend);

  if(g->upnp_renderer_frontend)
    bg_frontend_destroy(g->upnp_renderer_frontend);

  if(g->gmerlin_server_frontend)
    bg_frontend_destroy(g->gmerlin_server_frontend);

  if(g->gmerlin_renderer_frontend)
    bg_frontend_destroy(g->gmerlin_renderer_frontend);
  
  if(g->player)
    bg_player_destroy(g->player);
  
  /* Process last messages (state is stored here) */

  if(g->mainwin.player_ctrl.evt_sink)
    bg_msg_sink_iteration(g->mainwin.player_ctrl.evt_sink);

  if(g->mdb)
    bg_mdb_destroy(g->mdb);
  
  if(g->input_plugin_parameters)
    bg_parameter_info_destroy_array(g->input_plugin_parameters);

  if(g->image_reader_parameters)
    bg_parameter_info_destroy_array(g->image_reader_parameters);
  
  if(g->info_window)
    bg_gtk_info_window_destroy(g->info_window);

  if(g->log_window)
    bg_gtk_log_window_destroy(g->log_window);

  if(g->cfg_player)
    {
    bg_cfg_ctx_array_clear_sections(g->cfg_player);
    bg_cfg_ctx_destroy_array(g->cfg_player);
    }
  
  gavl_dictionary_free(&g->state);
  
  if(g->client_config)
    bg_server_storage_destroy(g->client_config);
  
  pthread_mutex_destroy(&g->stop_mutex);

  main_menu_destroy(g->main_menu);
  
  
  free(g);
  
  }

static void * frontend_thread(void * data)
  {
  int i;
  gmerlin_t * g = data;
  
  gavl_time_t delay_time = GAVL_TIME_SCALE / 100; /* 10 ms */
  while(1)
    {
    pthread_mutex_lock(&g->stop_mutex);
    if(g->stop)
      {
      pthread_mutex_unlock(&g->stop_mutex);
      break;
      }
    else
      pthread_mutex_unlock(&g->stop_mutex);

    i = 0;
    
    /* Handle remote control */
    i += bg_http_server_iteration(g->srv);


#ifdef HAVE_DBUS
    i += bg_frontend_ping(g->dbus_frontend, bg_http_server_get_time(g->srv));
#endif

    i += bg_frontend_ping(g->upnp_renderer_frontend, bg_http_server_get_time(g->srv));
    i += bg_frontend_ping(g->upnp_server_frontend, bg_http_server_get_time(g->srv));

    i += bg_frontend_ping(g->gmerlin_renderer_frontend, bg_http_server_get_time(g->srv));
    i += bg_frontend_ping(g->gmerlin_server_frontend, bg_http_server_get_time(g->srv));
    
    if(!i)
      gavl_time_delay(&delay_time);
    
    }
  return NULL;
  }

void gmerlin_run(gmerlin_t * g, const char ** locations)
  {
  const char * label;
  gavl_value_t icons_val;
  gavl_array_t * icons_arr;
  
  char * tmp_string;
  gavl_dictionary_t root_metadata;
  gavl_dictionary_t * node_vars;
  
  gmerlin_apply_config(g);
  gmerlin_apply_state(g); 
    
  bg_http_server_start(g->srv);

  
  gavl_dictionary_init(&root_metadata);
  
  /* Add icons with absolute URLs and the network node name */

  gavl_value_init(&icons_val);
  icons_arr = gavl_value_set_array(&icons_val);

  
  tmp_string = bg_sprintf("%s/static/icons/", bg_http_server_get_root_url(g->srv));
  gavl_dictionary_set(&root_metadata, GAVL_META_ICON_URL, NULL);
  bg_array_add_application_icons(icons_arr, tmp_string, "player");
  free(tmp_string);

  gavl_dictionary_set(&root_metadata, GAVL_META_ICON_URL, &icons_val);
  
  bg_mdb_merge_root_metadata(g->mdb, &root_metadata);
  gavl_dictionary_free(&root_metadata);
  
  node_vars = gavl_dictionary_get_dictionary_create(&bg_app_vars, BG_APP_STATE_NETWORK_NODE);
  
  label = gavl_dictionary_get_string(node_vars, GAVL_META_LABEL);
  
  bg_set_network_node_info(label, icons_arr, NULL, g->player_ctrl->evt_sink);
  bg_set_network_node_info(label, icons_arr, NULL, g->mdb_ctrl->evt_sink);
  
  
  /* */
  
  bg_player_run(g->player);
  
  if(locations)
    gmerlin_play_locations(g, locations);

  /* Start frontend thread */
  pthread_create(&g->frontend_thread, NULL, frontend_thread, g);
  g->frontend_thread_running = 1;

  main_window_show(&g->mainwin);
  
  gtk_main();

  /* The following saves the coords */
  
  bg_player_quit(g->player);

  gmerlin_get_config(g);

  }



static const bg_parameter_info_t parameters[] =
  {
#if 0
    {
      .name =      "general_options",
      .long_name = TRS("General Options"),
      .type =      BG_PARAMETER_SECTION,
    },
#endif
    {
      .name =      "skip_error_tracks",
      .long_name = TRS("Skip error tracks"),
      .type =      BG_PARAMETER_CHECKBUTTON,
      .val_default = GAVL_VALUE_INIT_INT(1),
      .help_string = TRS("If a track cannot be opened, switch to the next one")
    },
    {
      .name =        "show_tooltips",
      .long_name =   TRS("Show tooltips"),
      .type =        BG_PARAMETER_CHECKBUTTON,
      .val_default = GAVL_VALUE_INIT_INT(1),
    },
    { /* End of Parameters */ }
  };

const bg_parameter_info_t * gmerlin_get_parameters(gmerlin_t * g)
  {
  return parameters;
  }

void gmerlin_set_parameter(void * data, const char * name,
                           const gavl_value_t * val)
  {
  gmerlin_t * g = (gmerlin_t*)data;


  if(!name)
    return;

  //  fprintf(stderr, "gmerlin_set_parameter %s\n", name);
  //  gavl_value_dump(val, 2);
  
  
  if(!strcmp(name, "skip_error_tracks"))
    {
    if(val->v.i)
      g->playback_flags |= PLAYBACK_SKIP_ERROR;
    else
      g->playback_flags &= ~PLAYBACK_SKIP_ERROR;
    }
  else if(!strcmp(name, "show_tooltips"))
    {
    bg_gtk_set_tooltips(val->v.i);
    }
  }

int gmerlin_get_parameter(void * data, const char * name, gavl_value_t * val)
  {
  if(!name)
    return 0;
  return 0;
  }
 

void gmerlin_add_locations(gmerlin_t * g, const char ** locations)
  {
  fprintf(stderr, "BUG: gmerlin_add_locations not implemented yet\n");
  }

void gmerlin_play_locations(gmerlin_t * g, const char ** locations)
  {
  fprintf(stderr, "BUG: gmerlin_play_locations not implemented yet\n");
  }

#if 0
static bg_album_t * open_device(gmerlin_t * g, char * device)
  {
  bg_album_t * album;
  album = bg_media_tree_get_device_album(g->tree, device);

  if(!album)
    return album;
  
  if(!bg_album_is_open(album))
    {
    bg_album_open(album);
    bg_gtk_tree_window_open_album_window(g->tree_window, album);
    bg_gtk_tree_window_update(g->tree_window);
    }
  return album;
  }
#endif

void gmerlin_open_device(gmerlin_t * g, const char * device)
  {
#if 0
  open_device(g, device);
#else
  fprintf(stderr, "gmerlin_open_device not implemented yet\n");
#endif
  }

void gmerlin_play_device(gmerlin_t * g, const char * device)
  {
#if 0
  bg_album_t * album;
  bg_album_entry_t * entry;
  album = open_device(g, device);

  if(!album)
    return;
  
  entry = bg_album_get_entry(album, 0);

  if(!entry)
    return;
  
  bg_album_set_current(album, entry);
  bg_album_play(album);
#else
  fprintf(stderr, "gmerlin_play_device not implemented yet\n");
#endif
  }

