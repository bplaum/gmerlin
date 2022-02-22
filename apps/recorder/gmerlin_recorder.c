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

#include <gmerlin/recorder.h>
#include "recorder_window.h"

#include <gmerlin/utils.h>

#include <gtk/gtk.h>
#include <gui_gtk/gtkutils.h>

#define WINDOW_ICON "recorder_icon.png"
#define WINDOW_NAME "gmerlin-recorder"
#define WINDOW_CLASS "gmerlin-recorder"

int main(int argc, char ** argv)
  {
  bg_recorder_window_t * win;

  /* We must initialize the random number generator if we want the
     Vorbis encoder to work */
  srand(time(NULL));
  
  /* Create config registry */
  bg_cfg_registry_init("recorder");
  bg_plugins_init();
  
  /* Initialize gtk */
  bg_gtk_init(&argc, &argv, WINDOW_ICON);

  win = bg_recorder_window_create(bg_cfg_registry, bg_plugin_reg);

  bg_recorder_window_run(win);

  bg_recorder_window_destroy(win);
  
  /* Save config */
  
  bg_cfg_registry_save();
  bg_plugins_cleanup();
  bg_cfg_registry_cleanup();
  
  return 0;
  }
