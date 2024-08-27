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

#include <gmerlin/plugin.h>
#include <gmerlin/translation.h>
#include <gmerlin/backend.h>

bg_backend_plugin_t the_plugin =
  {
    .common =
    {
      BG_LOCALE,
      .name =      "be_renderer_gmerlin",
      .long_name = TRS("gmerlin renderer backend"),
      .description = TRS("Controls remote gmerlin players"),
      .type =     BG_PLUGIN_BACKEND_RENDERER,
      .flags =    0,
      .create =   bg_backend_gmerlin_create,
      .destroy =   bg_backend_gmerlin_destroy,
      .get_controllable = bg_backend_gmerlin_get_controllable,
      .priority =         1,
    },
    .protocol = BG_BACKEND_URI_SCHEME_GMERLIN_RENDERER,
    .update = bg_backend_gmerlin_update,
    .open = bg_backend_gmerlin_open,
  };

/* Include this into all plugin modules exactly once
   to let the plugin loader obtain the API version */
BG_GET_PLUGIN_API_VERSION;
