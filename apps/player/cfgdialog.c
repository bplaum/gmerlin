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
#include <gmerlin/translation.h>
#include <gui_gtk/configdialog.h>

#include "gmerlin.h"

#define CONFIG_GENERAL   "general"
#define CONFIG_LOGWINDOW "logwindow"
#define CONFIG_SERVER    "server"
#define CONFIG_EXPORT    "export"

static int handle_cfg_message(void * priv, gavl_msg_t * msg)
  {
  gmerlin_t * g = priv;
  
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

          if(!ctx)
            return 1;

          if(!strcmp(ctx, CONFIG_GENERAL))
            {
            gmerlin_set_parameter(g, name, &val);


            if(name)
              gavl_dictionary_set(g->general_section, name, &val);
            }
          else if(!strcmp(ctx, CONFIG_LOGWINDOW))
            {
            bg_gtk_log_window_set_parameter(g->log_window, name, &val);
            if(name)
              gavl_dictionary_set(g->logwindow_section, name, &val);
            }
          else if(!strcmp(ctx, CONFIG_SERVER) ||
                  !strcmp(ctx, CONFIG_EXPORT))
            {
            bg_http_server_set_parameter(g->srv, name, &val);
            if(name)
              gavl_dictionary_set(g->remote_section, name, &val);
            }
          gavl_value_free(&val);
          }
          break;
        }
      break;
    }
  return 1;
  }


void gmerlin_configure(gmerlin_t * g)
  {
  int i;
  GtkTreeIter it;
  bg_cfg_ctx_t ctx;
  
  GtkWidget * w;

  if(!g->cfg_sink)
    g->cfg_sink = bg_msg_sink_create(handle_cfg_message, g, 1);
  
  
  w = bg_gtk_config_dialog_create_multi(BG_GTK_CONFIG_DIALOG_DESTROY,
                                        TR("Gmerlin confiuration"),
                                        g->mainwin.win);
  
  
  /* TODO: General */

  bg_cfg_ctx_init(&ctx, NULL,
                  CONFIG_GENERAL,
                  TR("General"),
                  NULL, NULL);
  ctx.parameters = gmerlin_get_parameters(g);
    
  ctx.sink = g->cfg_sink;
  ctx.s = g->general_section;
  
  bg_gtk_config_dialog_add_section(w, &ctx, NULL);
  bg_cfg_ctx_free(&ctx);
  
  bg_gtk_config_dialog_add_section(w, &g->cfg_player[BG_PLAYER_CFG_INPUT], NULL);
  
  bg_gtk_config_dialog_add_section(w, bg_plugin_config_get_ctx(BG_PLUGIN_INPUT), NULL);
  bg_gtk_config_dialog_add_section(w, bg_plugin_config_get_ctx(BG_PLUGIN_IMAGE_READER), NULL);

  bg_gtk_config_dialog_add_container(w, TR("Audio"), NULL, &it);

  bg_gtk_config_dialog_add_section(w, &g->cfg_player[BG_PLAYER_CFG_AUDIO], &it);
  bg_gtk_config_dialog_add_section(w, bg_plugin_config_get_ctx(BG_PLUGIN_FILTER_AUDIO),  &it);
  
  bg_gtk_config_dialog_add_container(w, TR("Video"), NULL, &it);

  bg_gtk_config_dialog_add_section(w, &g->cfg_player[BG_PLAYER_CFG_VIDEO], &it);
  bg_gtk_config_dialog_add_section(w, bg_plugin_config_get_ctx(BG_PLUGIN_FILTER_VIDEO),  &it);
  
  bg_gtk_config_dialog_add_container(w, TR("Media Database"), NULL, &it);
  
  i = 0;
  while(g->cfg_mdb[i].name)
    {
    bg_gtk_config_dialog_add_section(w, &g->cfg_mdb[i], &it);
    i++;
    }

  bg_gtk_config_dialog_add_container(w, TR("Visuzalization"), NULL, &it);

  bg_gtk_config_dialog_add_section(w, &g->cfg_player[BG_PLAYER_CFG_VISUALIZATION],
                                   &it);

  bg_gtk_config_dialog_add_section(w, bg_plugin_config_get_ctx(BG_PLUGIN_VISUALIZATION), &it);
  

  bg_gtk_config_dialog_add_section(w, &g->cfg_player[BG_PLAYER_CFG_OSD],
                                   NULL);

  /* */

  bg_gtk_config_dialog_add_container(w, TR("Remote access"), NULL, &it);
  
  bg_cfg_ctx_init(&ctx, NULL, 
                  CONFIG_SERVER,
                  TR("HTTP server"),
                  NULL, NULL);

  ctx.parameters = bg_http_server_get_parameters(g->srv);
  
  ctx.sink = g->cfg_sink;
  ctx.s = g->remote_section;

  bg_gtk_config_dialog_add_section(w, &ctx, &it);
  bg_cfg_ctx_free(&ctx);
  
  bg_cfg_ctx_init(&ctx, NULL, 
                  CONFIG_EXPORT,
                  TR("Media export"),
                  NULL, NULL);

  ctx.parameters = bg_media_dirs_get_parameters();
  
  ctx.sink = g->cfg_sink;
  ctx.s = g->remote_section;
  bg_gtk_config_dialog_add_section(w, &ctx, &it);
  bg_cfg_ctx_free(&ctx);

  
  
  /* */

  bg_cfg_ctx_init(&ctx, NULL, 
                  CONFIG_LOGWINDOW,
                  TR("Logwindow"),
                  NULL, NULL);
  
  ctx.parameters = bg_gtk_log_window_get_parameters(g->log_window);
  
  
  ctx.sink = g->cfg_sink;
  ctx.s = g->logwindow_section;
  bg_gtk_config_dialog_add_section(w, &ctx, NULL);
  bg_cfg_ctx_free(&ctx);

  /* */

  
  
  gtk_window_present(GTK_WINDOW(w));
  }
