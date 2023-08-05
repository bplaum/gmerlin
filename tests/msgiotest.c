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

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include <gmerlin/parameter.h>
#include <gmerlin/streaminfo.h>
#include <gmerlin/bgmsg.h>
#include <gavl/gavl.h>
#include <gavl/utils.h>

int num_msgs = 3;

static void gen_msg(gavl_msg_t * msg, int num)
  {
  gavl_msg_set_id(msg, num);
  switch(num)
    {
    case 0:
      break; // ID only
    case 1:
      {
      float col[4] = { 0.1, 0.2, 0.3, 0.4 };
      gavl_msg_set_arg_int(msg, 0, 123);
      gavl_msg_set_arg_long(msg, 1, 123);
      gavl_msg_set_arg_string(msg, 2, NULL);
      gavl_msg_set_arg_string(msg, 3, "String");
      gavl_msg_set_arg_float(msg, 4, M_PI);
      gavl_msg_set_arg_color_rgb(msg, 5, col);
      gavl_msg_set_arg_color_rgba(msg, 6, col);
      break;
      }
    case 2:
      {
      gavl_audio_format_t afmt;
      gavl_video_format_t vfmt;
      gavl_dictionary_t m;
      double pos[2] = { 0.1, -0.2 };

      memset(&afmt, 0, sizeof(afmt));
      memset(&vfmt, 0, sizeof(vfmt));
      gavl_dictionary_init(&m);
      
      gavl_msg_set_arg_position(msg, 0, pos);

      afmt.samplerate = 48000;
      afmt.num_channels = 6;
      afmt.sample_format = GAVL_SAMPLE_FLOAT;
      afmt.channel_locations[0] = GAVL_CHID_LFE;
      afmt.channel_locations[1] = GAVL_CHID_FRONT_LEFT;
      afmt.channel_locations[2] = GAVL_CHID_FRONT_RIGHT;
      afmt.channel_locations[3] = GAVL_CHID_FRONT_CENTER;
      afmt.channel_locations[4] = GAVL_CHID_REAR_LEFT;
      afmt.channel_locations[5] = GAVL_CHID_REAR_RIGHT;
      gavl_msg_set_arg_audio_format(msg, 1, &afmt);
      
      vfmt.image_width  = 1920;
      vfmt.image_height = 1080;
      vfmt.frame_width  = 1920;
      vfmt.frame_height = 1080;
      vfmt.pixel_width  = 1;
      vfmt.pixel_height = 1;
      vfmt.pixelformat = GAVL_YUV_420_P;
      vfmt.timescale = 30000;
      vfmt.frame_duration = 1001;
      gavl_msg_set_arg_video_format(msg, 2, &vfmt);

      gavl_msg_set_arg_dictionary(msg, 3, &m);

      gavl_dictionary_set_string(&m, "tag1", "val1");
      gavl_dictionary_set_string(&m, "tag2", "val2");

      gavl_msg_set_arg_dictionary(msg, 4, &m);
      gavl_dictionary_free(&m);
      
      }
      break;
    case 3:
      break;
      
    }
  }

int main(int argc, char ** argv)
  {
  int i;
  gavl_msg_t * msg1;
  gavl_msg_t * msg2;

  uint8_t * buf;
  int len;
  
  msg1 = gavl_msg_create();
  msg2 = gavl_msg_create();

  for(i = 0; i < num_msgs; i++)
    {
    gen_msg(msg1, i);

    fprintf(stderr, "Message %d (orig)\n", i + 1);
    gavl_msg_dump(msg1, 2);

    len = 0;
    buf = gavl_msg_to_buffer(&len, msg1);

    fprintf(stderr, "Message %d (hex)\n", i + 1);
    gavl_hexdump(buf, len, 16);

    
    gavl_msg_from_buffer(buf, len, msg2);

    fprintf(stderr, "Message %d (copy)\n", i + 1);
    gavl_msg_dump(msg2, 2);

    if(buf)
      free(buf);
    gavl_msg_free(msg1);
    gavl_msg_free(msg2);
    }

  gavl_msg_destroy(msg1);
  gavl_msg_destroy(msg2);
  
  return EXIT_SUCCESS;
  }
