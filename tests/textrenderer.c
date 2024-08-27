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



#include <stdlib.h>
#include <string.h>

#include <gmerlin/parameter.h>
#include <gmerlin/textrenderer.h>
#include <gmerlin/utils.h>
#include <gmerlin/pluginregistry.h>
#include <gmerlin/iconfont.h>

int main(int argc, char ** argv)
  {
  gavl_video_format_t frame_format;
  gavl_video_format_t ovl_format;
  
  gavl_overlay_t * ovl;
  
  gavl_value_t val;
  bg_text_renderer_t * r;

  double * col;
  
  if(argc != 3)
    {
    fprintf(stderr, "usage: %s <font-name> <string>\n", argv[0]);
    return 1;
    }

  bg_iconfont_init();
  
  /* Create */
  r = bg_text_renderer_create();

  /* Set parameters */
  col = gavl_value_set_color_rgba(&val);

  col[0] = 1.0;
  col[1] = 1.0;
  col[2] = 1.0;
  col[3] = 0.8;
  bg_text_renderer_set_parameter(r, "color", &val);

  col = gavl_value_set_color_rgba(&val);
  col[0] = 0.0;
  col[1] = 0.0;
  col[2] = 1.0;
  col[3] = 1.0;
  bg_text_renderer_set_parameter(r, "border_color", &val);

  col = gavl_value_set_color_rgba(&val);
  col[0] = 0.2;
  col[1] = 0.2;
  col[2] = 0.2;
  col[3] = 0.7;
  bg_text_renderer_set_parameter(r, "box_color", &val);

  gavl_value_set_float(&val, 10.0);
  bg_text_renderer_set_parameter(r, "box_radius", &val);

  gavl_value_set_float(&val, 10.0);
  bg_text_renderer_set_parameter(r, "box_padding", &val);
  
  /* Border width */
  gavl_value_set_float(&val, 0.0);
  bg_text_renderer_set_parameter(r, "border_width", &val);
  
  /* font */
  gavl_value_set_string(&val, argv[1]);
  bg_text_renderer_set_parameter(r, "fontname", &val);

  /* align */
  gavl_value_set_string(&val, "center");
  bg_text_renderer_set_parameter(r, "justify_h", &val);

  gavl_value_set_string(&val, "center");
  bg_text_renderer_set_parameter(r, "justify_v", &val);

  /* Border width */
  gavl_value_set_int(&val, 20);
  bg_text_renderer_set_parameter(r, "border_left", &val);

  gavl_value_set_int(&val, 20);
  bg_text_renderer_set_parameter(r, "border_right", &val);

  gavl_value_set_int(&val, 20);
  bg_text_renderer_set_parameter(r, "border_top", &val);

  gavl_value_set_int(&val, 20);
  bg_text_renderer_set_parameter(r, "border_botton", &val);
  
  /* Initialize */
  
  memset(&frame_format, 0, sizeof(frame_format));
  frame_format.image_width  = 640;
  frame_format.image_height = 480;
  frame_format.frame_width  = 640;
  frame_format.frame_height = 480;
  frame_format.pixel_width  = 1;
  frame_format.pixel_height = 1;
  frame_format.pixelformat =  GAVL_RGB_FLOAT;
  //  frame_format.pixelformat =  GAVL_YUV_444_P;
  
  bg_text_renderer_init(r, &frame_format, &ovl_format);

  /* Render */

  ovl = bg_text_renderer_render(r, argv[2]);
  
  /* Save png */
  bg_plugins_init();

  bg_plugin_registry_save_image(bg_plugin_reg, "text.png", ovl,
                                &ovl_format, NULL);
  
  bg_plugins_cleanup();
  bg_cfg_registry_cleanup();
  
  /* Cleanup */
    
  bg_text_renderer_destroy(r);

  return 0;
  }
