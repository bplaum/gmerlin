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


#include <gmerlin/utils.h>
#include <gavl/metatags.h>

int bg_flac_cover_tag_write(gavf_io_t * output, const gavl_dictionary_t * image_uri, int last_tag)
  {
  const char * mimetype;
  const char * uri;
  int width = 0;
  int height = 0;
  uint8_t buffer[4];
  int ret = 0;
  int len;
  gavl_buffer_t b;

  int64_t end_pos;
  int64_t start_pos;
  
  gavl_buffer_init(&b);

  if(!(uri = gavl_dictionary_get_string(image_uri, GAVL_META_URI)) ||
     !(mimetype = gavl_dictionary_get_string(image_uri, GAVL_META_MIMETYPE)) ||
     !gavl_dictionary_get_int(image_uri, GAVL_META_WIDTH, &width) ||
     !gavl_dictionary_get_int(image_uri, GAVL_META_HEIGHT, &height) ||
     !bg_read_file(uri, &b))
    goto fail;

  start_pos = gavf_io_position(output);
  
  buffer[0] = 0x06;

  if(last_tag)
    buffer[0] |= 0x80;

  /* Size */
  buffer[1] = 0x00;
  buffer[2] = 0x00;
  buffer[3] = 0x00;

  gavf_io_write_data(output, buffer, 4);
  gavf_io_write_32_be(output, 0x03); // Cover (front)

  len = strlen(mimetype);
  gavf_io_write_32_be(output, len);
  gavf_io_write_data(output, (const uint8_t*)mimetype, len);

  len = strlen("Cover");
  gavf_io_write_32_be(output, len);
  gavf_io_write_data(output, (const uint8_t*)"Cover", len);
  
  gavf_io_write_32_be(output, width);
  gavf_io_write_32_be(output, height);
  gavf_io_write_32_be(output, 24);
  gavf_io_write_32_be(output, 0); // For indexed-color pictures (e.g. GIF), the number
                                  // of colors used, or 0 for non-indexed pictures. 

  gavf_io_write_32_be(output, b.len);
  gavf_io_write_data(output, b.buf, b.len);

  end_pos = gavf_io_position(output);
  
  gavf_io_seek(output, start_pos + 1, SEEK_SET);
  
  gavf_io_write_24_be(output, (uint32_t)(end_pos - start_pos - 4));
  gavf_io_seek(output, end_pos, SEEK_SET);
  
  ret = 1;
  fail:

  gavl_buffer_free(&b);
  return ret;
  
  }
