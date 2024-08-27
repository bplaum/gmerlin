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
#include <gmerlin/pluginregistry.h>
#include <gmerlin/utils.h>
#include <gmerlin/ocr.h>

#include <stdio.h>
#include <string.h>

int main(int argc, char ** argv)
  {
  gavl_video_format_t format;
  gavl_video_frame_t * frame = NULL;
  bg_ocr_t * ocr = NULL;
  char * ret = NULL;
  gavl_value_t val;

  gavl_value_init(&val);
  
  /* Create config registry */
  bg_plugins_init();


  /* Create ocr */
  ocr = bg_ocr_create();

  gavl_value_set_string(&val, ".");
  bg_ocr_set_parameter(ocr, "tmpdir", &val);
  if(!ocr)
    goto fail;
  
  /* Load image */
  memset(&format, 0, sizeof(format));
  frame = bg_plugin_registry_load_image(bg_plugin_reg, argv[1],
                                        &format, NULL);

  if(!bg_ocr_init(ocr, &format, "deu"))
    return -1;

  if(!bg_ocr_run(ocr, &format, frame, &ret))
    return -1;

  fprintf(stderr, "Got string: %s\n", ret);

  fail:

  gavl_value_free(&val);
  
  if(ocr)
    bg_ocr_destroy(ocr);
  if(frame)
    gavl_video_frame_destroy(frame);

  bg_plugins_cleanup();
  bg_cfg_registry_cleanup();
  
  if(ret)
    free(ret);
  
  return 0;
  }
