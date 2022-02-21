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

#include <string.h>
#include <gmerlin/pluginregistry.h>
#include <gmerlin/utils.h>

int main(int argc, char ** argv)
  {
    
  gavl_video_format_t format_1;
  gavl_video_format_t format_2;

  gavl_video_frame_t * frame_1;
  gavl_video_frame_t * frame_2;
  gavl_video_frame_t * frame_3;
  
  gavl_dictionary_t m;
  
  memset(&format_1, 0, sizeof(format_1));
  memset(&format_2, 0, sizeof(format_2));
  memset(&m, 0, sizeof(m));
  
  if(argc < 4)
    {
    fprintf(stderr, "Usage: %s <image1> <image2> <output>\n", argv[0]);
    return -1;
    }
  
  /* Create registries */

  bg_plugins_init();
  
  frame_1 =
    bg_plugin_registry_load_image(bg_plugin_reg, argv[1], &format_1, NULL);  
  if(!frame_1)
    {
    fprintf(stderr, "Cannot open %s\n", argv[1]);
    return -1;
    }
  
  frame_2 =
    bg_plugin_registry_load_image(bg_plugin_reg, argv[2], &format_2, NULL);
  if(!frame_2)
    {
    fprintf(stderr, "Cannot open %s\n", argv[2]);
    return -1;
    }

  if((format_1.image_width != format_2.image_width) ||
     (format_1.image_height != format_2.image_height) ||
     (format_1.pixelformat != format_2.pixelformat))
    {
    fprintf(stderr, "Format mismatch\n");
    return -1;
    }
  
  fprintf(stderr, "Format:\n\n");
  gavl_video_format_dump(&format_1);

  frame_3 = gavl_video_frame_create(&format_1);
  
  gavl_video_frame_absdiff(frame_3,
                           frame_1,
                           frame_2,
                           &format_1);

  bg_plugin_registry_save_image(bg_plugin_reg,
                                argv[3],
                                frame_3,
                                &format_1, &m);

  return 0;
  }
