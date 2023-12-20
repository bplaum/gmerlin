/*****************************************************************
 * gmerlin - a general purpose multimedia framework and applications
 *
 * copyright (c) 2001 - 2012 members of the gmerlin project
 * gmerlin-general@lists.sourceforge.net
 * http://gmerlin.sourceforge.net
 *
 * this program is free software: you can redistribute it and/or modify
 * it under the terms of the gnu general public license as published by
 * the free software foundation, either version 2 of the license, or
 * (at your option) any later version.
 *
 * this program is distributed in the hope that it will be useful,
 * but without any warranty; without even the implied warranty of
 * merchantability or fitness for a particular purpose.  see the
 * gnu general public license for more details.
 *
 * you should have received a copy of the gnu general public license
 * along with this program.  if not, see <http://www.gnu.org/licenses/>.
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
