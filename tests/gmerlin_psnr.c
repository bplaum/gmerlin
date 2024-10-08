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



#include <string.h>
#include <gmerlin/pluginregistry.h>
#include <gmerlin/utils.h>

int main(int argc, char ** argv)
  {
  double psnr[4];
    
  gavl_video_format_t format_1;
  gavl_video_format_t format_2;

  gavl_video_frame_t * frame_1;
  gavl_video_frame_t * frame_2;

  int index;

  memset(&format_1, 0, sizeof(format_1));
  memset(&format_2, 0, sizeof(format_2));
  
  if(argc < 3)
    {
    fprintf(stderr, "Usage: %s <image1> <image2>\n", argv[0]);
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
  
  //  fprintf(stderr, "Format:\n\n");
  //  gavl_video_format_dump(&format_1);
    
  gavl_video_frame_psnr(psnr, frame_1, frame_2, &format_1);
  index = 0;

  printf("# PSNR [dB]\n# ");
  
  if(gavl_pixelformat_is_gray(format_1.pixelformat))
    {
    printf("Gray  ");
    }
  else if(gavl_pixelformat_is_yuv(format_1.pixelformat))
    {
    printf("Y'  Cb    Cr   ");
    }
  else if(gavl_pixelformat_is_rgb(format_1.pixelformat))
    {
    printf("R   G     B    ");
    }
  if(gavl_pixelformat_has_alpha(format_1.pixelformat))
    {
    printf("A\n");
    }
  else
    printf("\n");
  
  if(gavl_pixelformat_is_gray(format_1.pixelformat))
    {
    printf("%5.2f", psnr[index]);
    index++;
    }
  else if(gavl_pixelformat_is_yuv(format_1.pixelformat) ||
          gavl_pixelformat_is_rgb(format_1.pixelformat))
    {
    printf("%5.2f %5.2f %5.2f ", psnr[index], psnr[index+1], psnr[index+2]);
    index+=3;
    }
  if(gavl_pixelformat_has_alpha(format_1.pixelformat))
    {
    printf("%5.2f\n", psnr[index]);
    }
  else
    printf("\n");
  
  return 0;
  }
