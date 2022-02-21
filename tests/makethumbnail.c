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
 ******************************************************************/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <gmerlin/utils.h>
#include <gmerlin/pluginregistry.h>

int main(int argc, char ** argv)
  {

  gavl_video_format_t input_format;
  gavl_video_frame_t * input_frame;
  
  char * in_file;
  char * out_base;
  char * mimetype;
  int max_width;
  int max_height;
  char * path_abs;
  gavl_dictionary_t metadata;

  gavl_dictionary_init(&metadata);
  
  if(argc != 6)
    {
    fprintf(stderr, "Usage: %s file max_width max_height output_base mimetype\n",
            argv[0]);
    return -1;
    }

  in_file = argv[1];
  max_width =  atoi(argv[2]);
  max_height = atoi(argv[3]);
  out_base = argv[4];
  mimetype = argv[5];
  
  /* Create registries */
  bg_plugins_init();

  /* Load image */

  memset(&input_format, 0, sizeof(input_format));
  
  input_frame = bg_plugin_registry_load_image(bg_plugin_reg,
                                              in_file,
                                              &input_format, &metadata);

  if(!input_frame)
    return -1;

  fprintf(stderr, "Loaded %s\n", in_file);
  gavl_video_format_dump(&input_format);
  
  /* Save image */
  
  path_abs = bg_make_thumbnail(bg_plugin_reg, input_frame, &input_format,
                               &max_width, &max_height,
                               out_base, mimetype, &metadata);
  if(!path_abs)
    return -1;

  fprintf(stderr, "Saved %s\n", path_abs);
  
  gavl_video_frame_destroy(input_frame);
  free(path_abs);
  
  bg_plugins_cleanup();
  bg_cfg_registry_cleanup();

  gavl_dictionary_free(&metadata);
  
  return 0;
  }


