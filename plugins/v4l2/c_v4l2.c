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

/* V4l2 formats decoder */
#include <config.h>
#include <gmerlin/translation.h>
#include <gmerlin/plugin.h>


#include "decode.h"



const bg_codec_plugin_t the_plugin =
  {
    .common =
    {
      BG_LOCALE,
      .name =            "c_v4l2",       /* Unique short name */
      .long_name =       "V4L video decoder",
      .description =     "Decoder for many formats used by V4L2 drivers. Taken from libv4lconvert.",
      .type =            BG_PLUGIN_DECOMPRESSOR_VIDEO,
      .flags =           0,
      .priority =        5,
      .create =          bg_v4l2_decoder_create,
      .destroy =         bg_v4l2_decoder_destroy,
    },
    .open_decode_video = open_video,
    .get_codec_tags = get_codec_tags,
  };
/* Include this into all plugin modules exactly once
   to let the plugin loader obtain the API version */
BG_GET_PLUGIN_API_VERSION;
