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

#include <gmerlin/frontend.h>


bg_frontend_plugin_t the_plugin =
  {
    .common =
    {
      BG_LOCALE,
      .name =      "fe_mdb_gmerlin",
      .long_name = TRS("Control Media DB"),
      .description = TRS("Uses the native gmerlin control protocol"),
      .type =     BG_PLUGIN_FRONTEND_MDB,
      .flags =    BG_PLUGIN_NEEDS_HTTP_SERVER,
      .create =   bg_frontend_gmerlin_create,
      .destroy =   bg_frontend_gmerlin_destroy,
      .priority =         1,
    },
    .update = bg_frontend_gmerlin_ping,
    .open = bg_frontend_gmerlin_open_mdb,
  };

/* Include this into all plugin modules exactly once
   to let the plugin loader obtain the API version */
BG_GET_PLUGIN_API_VERSION;
