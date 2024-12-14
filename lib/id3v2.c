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
#include <gmerlin/utils.h>

#include <gmerlin/utils.h>

#include <gmerlin/parameter.h>
#include <gmerlin/streaminfo.h>
#include <gavl/metatags.h>


/* Simple ID3 writer.
   We do the following:
   
   - Only ID3V2.4 tags are written
   - All metadata are converted to single line strings
   - We use UTF-8 encoding for the tags
*/

/* FOURCC stuff */

#define MK_FOURCC(a, b, c, d) ((a<<24)|(b<<16)|(c<<8)|d)

static int write_fourcc(gavl_io_t * output, uint32_t fourcc)
  {
  uint8_t data[4];

  data[0] = (fourcc >> 24) & 0xff;
  data[1] = (fourcc >> 16) & 0xff;
  data[2] = (fourcc >> 8) & 0xff;
  data[3] = (fourcc) & 0xff;

  if(gavl_io_write_data(output, data, 4) < 4)
    return 0;
  return 1;
  }


#define ID3V2_FRAME_TAG_ALTER_PRESERVATION  (1<<14)
#define ID3V2_FRAME_FILE_ALTER_PRESERVATION (1<<13)
#define ID3V2_FRAME_READ_ONLY               (1<<12)
#define ID3V2_FRAME_GROUPING                (1<<6)
#define ID3V2_FRAME_COMPRESSION             (1<<3)
#define ID3V2_FRAME_ENCRYPTION              (1<<2)
#define ID3V2_FRAME_UNSYNCHRONIZED          (1<<1)
#define ID3V2_FRAME_DATA_LENGTH             (1<<0)

typedef struct
  {
  uint32_t fourcc;

  char * cover_file;

  gavl_value_t val;
  } id3v2_frame_t;

/* Flags for ID3V2 Tag header */

#define ID3V2_TAG_UNSYNCHRONIZED  (1<<7)
#define ID3V2_TAG_EXTENDED_HEADER (1<<6)
#define ID3V2_TAG_EXPERIMENTAL    (1<<5)
#define ID3V2_TAG_FOOTER_PRESENT  (1<<4)

struct bg_id3v2_s
  {
  struct
    {
    uint8_t major_version;
    uint8_t minor_version;
    uint8_t flags;
    uint32_t size;
    } header;
  
  int num_frames;
  id3v2_frame_t * frames;

  };

static void add_frame(bg_id3v2_t * tag, uint32_t fourcc,
                      const gavl_value_t * val, const char * cover_file)
  {
  tag->frames = realloc(tag->frames,
                        (tag->num_frames+1)*sizeof(*(tag->frames)));
  memset(&tag->frames[tag->num_frames], 0,
         sizeof(tag->frames[tag->num_frames]));
  tag->frames[tag->num_frames].fourcc = fourcc;

  if(val)
    gavl_value_copy(&tag->frames[tag->num_frames].val, val);

  if(cover_file)
    tag->frames[tag->num_frames].cover_file = gavl_strdup(cover_file);
  
  tag->num_frames++;
  }

#define ADD_FRAME(key, fcc) \
  val = gavl_dictionary_get(m, key); \
  if(val) \
    { \
    add_frame(ret, fcc, val, NULL);                  \
    }

bg_id3v2_t * bg_id3v2_create(const gavl_dictionary_t * m, int add_cover)
  {
  int year;
  const char * cover_file = NULL;
  bg_id3v2_t * ret;
  const gavl_value_t * val;
  
  ret = calloc(1, sizeof(*ret));

  ret->header.major_version = 4;
  ret->header.minor_version = 4;
  ret->header.flags = 0;

  ADD_FRAME(GAVL_META_ARTIST,      MK_FOURCC('T', 'P', 'E', '1'));
  ADD_FRAME(GAVL_META_ALBUMARTIST, MK_FOURCC('T', 'P', 'E', '2'));

  ADD_FRAME(GAVL_META_TITLE,       MK_FOURCC('T', 'I', 'T', '2'));
  ADD_FRAME(GAVL_META_ALBUM,       MK_FOURCC('T', 'A', 'L', 'B'));
  ADD_FRAME(GAVL_META_TRACKNUMBER, MK_FOURCC('T', 'R', 'C', 'K'));
  ADD_FRAME(GAVL_META_GENRE,       MK_FOURCC('T', 'C', 'O', 'N'));
  ADD_FRAME(GAVL_META_AUTHOR,      MK_FOURCC('T', 'C', 'O', 'M'));
  ADD_FRAME(GAVL_META_COPYRIGHT,   MK_FOURCC('T', 'C', 'O', 'P'));

  year = bg_metadata_get_year(m);
  if(year)
    {
    gavl_value_t v1;
    gavl_value_init(&v1);
    gavl_value_set_int(&v1, year);
    add_frame(ret, MK_FOURCC('T', 'Y', 'E', 'R'), &v1, NULL);
    gavl_value_free(&v1);
    }

  ADD_FRAME(GAVL_META_COMMENT, MK_FOURCC('C', 'O', 'M', 'M'));
  
  if(add_cover && (cover_file = gavl_dictionary_get_string_image_max(m, GAVL_META_COVER_URL,
                                                                     600, 600, "image/jpeg")))
    {
    add_frame(ret, MK_FOURCC('A', 'P', 'I', 'C'), NULL, cover_file);
    }
  
  return ret;
  }

static int write_32_syncsave(gavl_io_t * output, uint32_t num)
  {
  uint8_t data[4];
  data[0] = (num >> 21) & 0x7f;
  data[1] = (num >> 14) & 0x7f;
  data[2] = (num >> 7) & 0x7f;
  data[3] = num & 0x7f;
  if(gavl_io_write_data(output, data, 4) < 4)
    return 0;
  return 1;
  }


static int write_string(gavl_io_t * output, const char * str,
                        gavl_charset_converter_t * cnv)
  {
  int len;

  if(cnv)
    {
    char * str1;
    str1 = gavl_convert_string(cnv, str, -1, NULL );
    len = strlen(str1)+1;
    if(gavl_io_write_data(output, (uint8_t*)str1, len) < len)
      return 0;
    free(str1);
    }
  else
    {
    len = strlen(str)+1;
    if(gavl_io_write_data(output, (uint8_t*)str, len) < len)
      return 0;
    }
  return 1;
  }

static int write_frame(gavl_io_t * output, id3v2_frame_t * frame,
                       uint8_t encoding)
  {
  uint8_t flags[2] = { 0x00, 0x00 };
  uint8_t comm_header[4] = { 'X', 'X', 'X', 0x00 };
  uint32_t size_pos, end_pos, size;
  gavl_charset_converter_t * cnv = NULL;
  int ret = 0;

  if((frame->val.type == GAVL_TYPE_STRING) &&
     !gavl_value_get_string(&frame->val))
    return 1;
  
  /* Write 10 bytes header */
  
  if(!write_fourcc(output, frame->fourcc))
    goto fail;
  
  size_pos = gavl_io_position(output);

  if(!write_32_syncsave(output, 0))
    goto fail;
  
  /* Frame flags are zero */

  if(gavl_io_write_data(output, flags, 2) < 2)
    goto fail;

  if(frame->cover_file)
    {
    uint8_t buf;
    
    gavl_buffer_t b;
    gavl_buffer_init(&b);

    if(!bg_read_file(frame->cover_file, &b))
      return 0;

    buf = 0x00;
    gavl_io_write_data(output, &buf, 1); // Encoding
    
    gavl_io_write_data(output, (uint8_t*)"image/jpeg", 11);
    buf = 0x03; /* front cover */
    gavl_io_write_data(output, &buf, 1);
    buf = 0;
    gavl_io_write_data(output, &buf, 1); // Description
    gavl_io_write_data(output, b.buf, b.len); // Image
    
    gavl_buffer_free(&b);
    }
  else
    {
    /* Encoding */
    if(gavl_io_write_data(output, &encoding, 1) < 1)
      return 0;
  
    /* For COMM frames, we need to set the language as well */

    if((frame->fourcc == MK_FOURCC('C','O','M','M')) &&
       (gavl_io_write_data(output, comm_header, 4) < 4))
      goto fail;
  
    switch(frame->val.type)
      {
      case GAVL_TYPE_INT:
        {
        char int_buf[32];
        snprintf(int_buf, 32, "%d", frame->val.v.i);
        if(!write_string(output, int_buf, NULL))
          goto fail;
        }
        break;
      case GAVL_TYPE_STRING:
        {
        const char * val_str;
        if(encoding == BG_ID3_ENCODING_LATIN1)
          cnv = gavl_charset_converter_create("UTF-8", "ISO-8859-1");
      
        if((val_str = gavl_value_get_string(&frame->val)) &&
           !write_string(output, val_str, cnv))
          goto fail;
        }
        break;
      case GAVL_TYPE_ARRAY:
        {
        const gavl_value_t * v;
        const char * s;
        int i = 0;
        
        if(encoding == BG_ID3_ENCODING_LATIN1)
          cnv = gavl_charset_converter_create("UTF-8", "ISO-8859-1");
        
        while((v = gavl_value_get_item(&frame->val, i)) &&
              (s = gavl_value_get_string(v)))
          {
          if(!write_string(output, s, cnv))
            goto fail;
          i++;
          }
        }
        break;
      default:
        goto fail;
      }
    }
  
  end_pos = gavl_io_position(output);
  size = end_pos - size_pos - 6;
  
  gavl_io_seek(output, size_pos, SEEK_SET);
  if(!write_32_syncsave(output, size))
    goto fail;
  
  gavl_io_seek(output, end_pos, SEEK_SET);
  
  ret = 1;
  fail:

  if(cnv)
    gavl_charset_converter_destroy(cnv);
  
  return ret;
  }

int bg_id3v2_write(gavl_io_t * output, const bg_id3v2_t * tag,
                   int encoding)
  {
  int i;
  uint32_t size_pos, size, end_pos;
  
  
  uint8_t header[6] = { 'I', 'D', '3', 0x04, 0x00, 0x00 };
  
  /* Return if the tag has zero frames */

  if(!tag->num_frames)
    return 1;

  /* Write header */

  if(gavl_io_write_data(output, header, 6) < 6)
    return 0;
  
  /* Write dummy size (will be filled in later) */

  size_pos = gavl_io_position(output);
  write_32_syncsave(output, 0);

  /* Write all frames */

  for(i = 0; i < tag->num_frames; i++)
    {
    write_frame(output, &tag->frames[i], encoding);
    }

  end_pos = gavl_io_position(output);
  size = end_pos - size_pos - 4;

  gavl_io_seek(output, size_pos, SEEK_SET);
  write_32_syncsave(output, size);
  gavl_io_seek(output, end_pos, SEEK_SET);
  return 1;
  }

void bg_id3v2_destroy(bg_id3v2_t * tag)
  {
  int i;

  if(tag->frames)
    {
    for(i = 0; i < tag->num_frames; i++)
      {
      gavl_value_free(&tag->frames[i].val);
      if(tag->frames[i].cover_file)
        free(tag->frames[i].cover_file);
      }
    free(tag->frames);
    }
  free(tag);
  }

