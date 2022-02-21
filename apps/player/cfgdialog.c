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
#include <gmerlin/translation.h>

#include "gmerlin.h"

void gmerlin_create_dialog(gmerlin_t * g)
  {
  void * parent;
  int i;
  const bg_parameter_info_t * parameters;
  
  g->cfg_dialog = bg_dialog_create_multi(TR("Gmerlin confiuration"));

  bg_dialog_set_sink(g->cfg_dialog, g->player_ctrl->cmd_sink);
  
  /* Add sections */

  parameters = gmerlin_get_parameters(g);

  bg_dialog_add(g->cfg_dialog,
                TR("General"),
                g->general_section,
                gmerlin_set_parameter,
                g,
                parameters);
  
  bg_dialog_add_ctx(g->cfg_dialog,
                    &g->cfg_player[BG_PLAYER_CFG_INPUT]);

  bg_dialog_add(g->cfg_dialog,
                TR("Input plugins"),
                NULL,
                bg_plugin_registry_set_parameter_input,
                bg_plugin_reg,
                g->input_plugin_parameters);

  bg_dialog_add(g->cfg_dialog,
                TR("Image readers"),
                NULL,
                bg_plugin_registry_set_parameter_input,
                bg_plugin_reg,
                g->image_reader_parameters);

  /* Audio */
  parent = bg_dialog_add_parent(g->cfg_dialog, NULL, TR("Audio"));

  bg_dialog_add_child_ctx(g->cfg_dialog,
                          parent,
                          &g->cfg_player[BG_PLAYER_CFG_AUDIO]);

  bg_dialog_add_child_ctx(g->cfg_dialog,
                          parent,
                          &g->cfg_player[BG_PLAYER_CFG_AUDIOFILTER]);

  bg_dialog_add_child_ctx(g->cfg_dialog,
                          parent,
                          &g->cfg_player[BG_PLAYER_CFG_AUDIOPLUGIN]);

  /* Video */
  parent = bg_dialog_add_parent(g->cfg_dialog, NULL, TR("Video"));

  bg_dialog_add_child_ctx(g->cfg_dialog,
                          parent,
                          &g->cfg_player[BG_PLAYER_CFG_VIDEO]);

  bg_dialog_add_child_ctx(g->cfg_dialog,
                          parent,
                          &g->cfg_player[BG_PLAYER_CFG_VIDEOFILTER]);

  bg_dialog_add_child_ctx(g->cfg_dialog,
                          parent,
                          &g->cfg_player[BG_PLAYER_CFG_VIDEOPLUGIN]);

  /* Media DB */
  parent = bg_dialog_add_parent(g->cfg_dialog, NULL, TR("Media Database"));

  i = 0;
  while(g->cfg_mdb[i].name)
    {
    bg_dialog_add_child_ctx(g->cfg_dialog,
                            parent,
                            &g->cfg_mdb[i]);
    i++;
    }
    
  /* Visualization */
  bg_dialog_add_ctx(g->cfg_dialog,
                    &g->cfg_player[BG_PLAYER_CFG_VISUALIZATION]);
  
  /* OSD */
  bg_dialog_add_ctx(g->cfg_dialog,
                    &g->cfg_player[BG_PLAYER_CFG_OSD]);

  /* Remote */
  parent = bg_dialog_add_parent(g->cfg_dialog, NULL, TR("Remote access"));

  parameters = bg_http_server_get_parameters(g->srv);
  
  bg_dialog_add_child(g->cfg_dialog,
                      parent,
                      TR("HTTP server"),
                      g->remote_section,
                      bg_http_server_set_parameter,
                      g->srv,
                      parameters);

  parameters = bg_media_dirs_get_parameters();
  
  bg_dialog_add_child(g->cfg_dialog,
                      parent,
                      TR("Media export"),
                      g->remote_section,
                      bg_http_server_set_parameter,
                      g->srv,
                      parameters);
  
  /* Logging */
  parameters = bg_gtk_log_window_get_parameters(g->log_window);
  
  bg_dialog_add(g->cfg_dialog,
                TR("Log window"),
                g->logwindow_section,
                bg_gtk_log_window_set_parameter,
                g->log_window,
                parameters);
#if 0
  /* LCDProc */
  parameters = bg_lcdproc_get_parameters(g->lcdproc);
  
  bg_dialog_add(g->cfg_dialog,
                TR("LCDproc"),
                g->lcdproc_section,
                bg_lcdproc_set_parameter,
                (void*)(g->lcdproc),
                parameters);
#endif
  }


void gmerlin_configure(gmerlin_t * g)
  {
  bg_dialog_show(g->cfg_dialog, g->mainwin.win);
  }
