/*

# PAC207 decoder
#               Bertrik.Sikken. Thomas Kaiser (C) 2005
#               Copyright (C) 2003 2004 2005 Michel Xhaard

# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU Lesser General Public License as published by
# the Free Software Foundation; either version 2.1 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

# Note this code was originally licensed under the GNU GPL instead of the
# GNU LGPL, its license has been changed with permission, see the permission
# mails at the end of this file.

*/

#include <string.h>
#include "decode.h"

#include <gavl/log.h>
#define LOG_DOMAIN "p207"

#define CLIP(color) (unsigned char)(((color)>0xFF)?0xff:(((color)<0)?0:(color)))

typedef struct
  {
  struct
    {
    unsigned char is_abs;
    unsigned char len;
    signed char val;
    }
    table[256];
  
  } p207_t;


static void init_pixart_decoder(p207_t * p)
{
    int i;
    int is_abs, val, len;
    for (i = 0; i < 256; i++) {
	is_abs = 0;
	val = 0;
	len = 0;
	if ((i & 0xC0) == 0) {
	    /* code 00 */
	    val = 0;
	    len = 2;
	} else if ((i & 0xC0) == 0x40) {
	    /* code 01 */
	    val = -1;
	    len = 2;
	} else if ((i & 0xC0) == 0x80) {
	    /* code 10 */
	    val = +1;
	    len = 2;
	} else if ((i & 0xF0) == 0xC0) {
	    /* code 1100 */
	    val = -2;
	    len = 4;
	} else if ((i & 0xF0) == 0xD0) {
	    /* code 1101 */
	    val = +2;
	    len = 4;
	} else if ((i & 0xF8) == 0xE0) {
	    /* code 11100 */
	    val = -3;
	    len = 5;
	} else if ((i & 0xF8) == 0xE8) {
	    /* code 11101 */
	    val = +3;
	    len = 5;
	} else if ((i & 0xFC) == 0xF0) {
	    /* code 111100 */
	    val = -4;
	    len = 6;
	} else if ((i & 0xFC) == 0xF4) {
	    /* code 111101 */
	    val = +4;
	    len = 6;
	} else if ((i & 0xF8) == 0xF8) {
	    /* code 11111xxxxxx */
	    is_abs = 1;
	    val = 0;
	    len = 5;
	}
	p->table[i].is_abs = is_abs;
	p->table[i].val = val;
	p->table[i].len = len;
    }
}

static inline unsigned char getByte(const unsigned char *inp,
				    unsigned int bitpos)
{
    const unsigned char *addr;
    addr = inp + (bitpos >> 3);
    return (addr[0] << (bitpos & 7)) | (addr[1] >> (8 - (bitpos & 7)));
}

static inline unsigned short getShort(const unsigned char *pt)
{
    return ((pt[0] << 8) | pt[1]);
}

static int
pac_decompress_row(p207_t * p, const unsigned char *inp, unsigned char *outp, int width,
    int step_size, int abs_bits)
{
    int col;
    int val;
    int bitpos;
    unsigned char code;


    /* first two pixels are stored as raw 8-bit */
    *outp++ = inp[2];
    *outp++ = inp[3];
    bitpos = 32;

    /* main decoding loop */
    for (col = 2; col < width; col++) {
	/* get bitcode */

	code = getByte(inp, bitpos);
	bitpos += p->table[code].len;

	/* calculate pixel value */
	if (p->table[code].is_abs) {
	    /* absolute value: get 6 more bits */
	    code = getByte(inp, bitpos);
	    bitpos += abs_bits;
	    *outp++ = code & ~(0xff >> abs_bits);
	} else {
	    /* relative to left pixel */
	    val = outp[-2] + p->table[code].val * step_size;
	    *outp++ = CLIP(val);
	}
    }

    /* return line length, rounded up to next 16-bit word */
    return 2 * ((bitpos + 15) / 16);
}

static void decode_p207(decoder_t * dec, gavl_packet_t * p, gavl_video_frame_t * frame)
  {

  //int v4lconvert_decode_pac207(struct v4lconvert_data *data,
  //  const unsigned char *inp, int src_size, unsigned char *outp,
  //  int width, int height)
  //  {
/* we should received a whole frame with header and EOL marker
in myframe->data and return a GBRG pattern in frame->tmpbuffer
remove the header then copy line by line EOL is set with 0x0f 0xf0 marker
or 0x1e 0xe1 for compressed line*/
  unsigned short word;
  int row;
  p207_t * priv = dec->priv;
  const unsigned char *inp = p->buf.buf;
  int src_size = p->buf.len;
  
  unsigned char *outp;
  const unsigned char *end = inp + src_size;
  
  outp = dec->tmp_frame->planes[0];
  
  /* iterate over all rows */
  for (row = 0; row < dec->fmt->image_height; row++)
    {
    if ((inp + 2) > end)
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Incomplete pac207 frame");
      return;
      }
    word = getShort(inp);
    switch (word)
      {
      case 0x0FF0:
        memcpy(outp, inp + 2, dec->fmt->image_width);
        inp += (2 + dec->fmt->image_width);
        break;
      case 0x1EE1:
        inp += pac_decompress_row(priv, inp, outp, dec->fmt->image_width, 5, 6);
        break;

      case 0x2DD2:
        inp += pac_decompress_row(priv, inp, outp, dec->fmt->image_width, 9, 5);
        break;

      case 0x3CC3:
        inp += pac_decompress_row(priv, inp, outp, dec->fmt->image_width, 17, 4);
        break;

      case 0x4BB4:
        /* skip or copy line? */
        memcpy(outp, outp - 2 * dec->fmt->image_width, dec->fmt->image_width);
        inp += 2;
        break;

      default: /* corrupt frame */
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "unknown pac207 row header: 0x%04x", (int)word);
      return;
      }
    outp += dec->tmp_frame->strides[0];
    }
  
  gavl_video_frame_debayer(NULL, dec->tmp_frame, frame, GAVL_BAYER_BGGR, dec->fmt);
  return;
  }

static void cleanup_p207(decoder_t * dec)
  {
  p207_t * priv = dec->priv;
  free(priv);
  }

void init_p207(decoder_t * dec)
  {
  gavl_video_format_t tmp_format;
  p207_t * priv;
  dec->cleanup = cleanup_p207;
  dec->decode = decode_p207;

  priv = calloc(1, sizeof(*priv));
  init_pixart_decoder(priv);
  dec->priv = priv;
  dec->fmt->pixelformat = GAVL_RGB_24;
  gavl_video_format_copy(&tmp_format, dec->fmt);
  tmp_format.pixelformat = GAVL_GRAY_8;
  dec->tmp_frame = gavl_video_frame_create(&tmp_format);
  }

