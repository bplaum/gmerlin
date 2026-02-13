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

#include <gmerlin/utils.h>
#include <gmerlin/resourcemanager.h>
#include <gmerlin/pluginregistry.h>
#include <gmerlin/log.h>

#ifdef HAVE_DBUS
#include <gmerlin/bgdbus.h>
#endif  

void bg_global_cleanup()
  {
  bg_resourcemanager_cleanup();
  bg_plugins_cleanup();
  
  bg_cfg_registry_cleanup();
  
#ifdef HAVE_DBUS
  bg_dbus_cleanup();
#endif  

  bg_log_cleanup();
  
  }
