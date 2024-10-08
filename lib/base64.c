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
#include <stdlib.h>


#include <gmerlin/utils.h>

/* Public domain libb64 included  here for simplicity.
 * - Removed multi inclusion guards
 * - Made everything static
 */

/*
cencode.h - c header for a base64 encoding algorithm

This is part of the libb64 project, and has been placed in the public domain.
For details, see http://sourceforge.net/projects/libb64
*/


typedef enum
  {
    step_A, step_B, step_C
  } base64_encodestep;

typedef struct
  {
  base64_encodestep step;
  char result;
  int stepcount;
  } base64_encodestate;

static void base64_init_encodestate(base64_encodestate* state_in);

static char base64_encode_value(char value_in);

static int base64_encode_block(const char* plaintext_in, int length_in,
                               char* code_out, base64_encodestate* state_in);

static int base64_encode_blockend(char* code_out, base64_encodestate* state_in);

/*
  cdecode.h - c header for a base64 decoding algorithm

  This is part of the libb64 project, and has been placed in the public domain.
  For details, see http://sourceforge.net/projects/libb64
*/

typedef enum
  {
    step_a, step_b, step_c, step_d
  } base64_decodestep;

typedef struct
  {
  base64_decodestep step;
  char plainchar;
  } base64_decodestate;

static void base64_init_decodestate(base64_decodestate* state_in);

static int base64_decode_value(char value_in);

static int base64_decode_block(const char* code_in, const int length_in,
                               char* plaintext_out, base64_decodestate* state_in);

/*
  cdecoder.c - c source to a base64 decoding algorithm implementation

  This is part of the libb64 project, and has been placed in the public domain.
  For details, see http://sourceforge.net/projects/libb64
*/


static int base64_decode_value(char value_in)
  {
  static const char decoding[] = {62,-1,-1,-1,63,52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-2,-1,-1,-1,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,-1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51};
  static const char decoding_size = sizeof(decoding);
  value_in -= 43;
  if (value_in < 0 || value_in > decoding_size) return -1;
  return decoding[(int)value_in];
  }

static void base64_init_decodestate(base64_decodestate* state_in)
  {
  state_in->step = step_a;
  state_in->plainchar = 0;
  }

static int base64_decode_block(const char* code_in, const int length_in, char* plaintext_out, base64_decodestate* state_in)
  {
  const char* codechar = code_in;
  char* plainchar = plaintext_out;
  char fragment;
	
  *plainchar = state_in->plainchar;
	
  switch (state_in->step)
    {
    while (1)
      {
      case step_a:
        do {
        if (codechar == code_in+length_in)
          {
          state_in->step = step_a;
          state_in->plainchar = *plainchar;
          return plainchar - plaintext_out;
          }
        fragment = (char)base64_decode_value(*codechar++);
        } while (fragment < 0);
        *plainchar    = (fragment & 0x03f) << 2;
      case step_b:
        do {
        if (codechar == code_in+length_in)
          {
          state_in->step = step_b;
          state_in->plainchar = *plainchar;
          return plainchar - plaintext_out;
          }
        fragment = (char)base64_decode_value(*codechar++);
        } while (fragment < 0);
        *plainchar++ |= (fragment & 0x030) >> 4;
        *plainchar    = (fragment & 0x00f) << 4;
      case step_c:
        do {
        if (codechar == code_in+length_in)
          {
          state_in->step = step_c;
          state_in->plainchar = *plainchar;
          return plainchar - plaintext_out;
          }
        fragment = (char)base64_decode_value(*codechar++);
        } while (fragment < 0);
        *plainchar++ |= (fragment & 0x03c) >> 2;
        *plainchar    = (fragment & 0x003) << 6;
      case step_d:
        do {
        if (codechar == code_in+length_in)
          {
          state_in->step = step_d;
          state_in->plainchar = *plainchar;
          return plainchar - plaintext_out;
          }
        fragment = (char)base64_decode_value(*codechar++);
        } while (fragment < 0);
        *plainchar++   |= (fragment & 0x03f);
      }
    }
  /* control should not reach here */
  return plainchar - plaintext_out;
  }

/*
  cencoder.c - c source to a base64 encoding algorithm implementation

  This is part of the libb64 project, and has been placed in the public domain.
  For details, see http://sourceforge.net/projects/libb64
*/

const int CHARS_PER_LINE = 72;

static void base64_init_encodestate(base64_encodestate* state_in)
  {
  state_in->step = step_A;
  state_in->result = 0;
  state_in->stepcount = 0;
  }

static char base64_encode_value(char value_in)
  {
  static const char* encoding = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  if (value_in > 63) return '=';
  return encoding[(int)value_in];
  }

static int base64_encode_block(const char* plaintext_in, int length_in,
                               char* code_out, base64_encodestate* state_in)
  {
  const char* plainchar = plaintext_in;
  const char* const plaintextend = plaintext_in + length_in;
  char* codechar = code_out;
  char result;
  char fragment;
	
  result = state_in->result;
	
  switch (state_in->step)
    {
    while (1)
      {
      case step_A:
        if (plainchar == plaintextend)
          {
          state_in->result = result;
          state_in->step = step_A;
          return codechar - code_out;
          }
        fragment = *plainchar++;
        result = (fragment & 0x0fc) >> 2;
        *codechar++ = base64_encode_value(result);
        result = (fragment & 0x003) << 4;
      case step_B:
        if (plainchar == plaintextend)
          {
          state_in->result = result;
          state_in->step = step_B;
          return codechar - code_out;
          }
        fragment = *plainchar++;
        result |= (fragment & 0x0f0) >> 4;
        *codechar++ = base64_encode_value(result);
        result = (fragment & 0x00f) << 2;
      case step_C:
        if (plainchar == plaintextend)
          {
          state_in->result = result;
          state_in->step = step_C;
          return codechar - code_out;
          }
        fragment = *plainchar++;
        result |= (fragment & 0x0c0) >> 6;
        *codechar++ = base64_encode_value(result);
        result  = (fragment & 0x03f) >> 0;
        *codechar++ = base64_encode_value(result);
			
        ++(state_in->stepcount);
        if (state_in->stepcount == CHARS_PER_LINE/4)
          {
          *codechar++ = '\n';
          state_in->stepcount = 0;
          }
      }
    }
  /* control should not reach here */
  return codechar - code_out;
  }

static int base64_encode_blockend(char* code_out, base64_encodestate* state_in)
  {
  char* codechar = code_out;
  
  switch (state_in->step)
    {
    case step_B:
      *codechar++ = base64_encode_value(state_in->result);
      *codechar++ = '=';
      *codechar++ = '=';
      break;
    case step_C:
      *codechar++ = base64_encode_value(state_in->result);
      *codechar++ = '=';
      break;
    case step_A:
      break;
    }
  // *codechar++ = '\n';
	
  return codechar - code_out;
  }



/* Base64 stuff */

int bg_base64_decode(const char * in,
                     int in_len,
                     uint8_t ** out,
                     int * out_alloc)
  {
  base64_decodestate st;
  int alloc_len;
  
  if(in_len < 0)
    in_len = strlen(in);

  alloc_len = in_len;
  
  if(*out_alloc < alloc_len)
    {
    *out_alloc = (alloc_len + 128);
    *out = realloc(*out, *out_alloc);
    }

  base64_init_decodestate(&st);
  return base64_decode_block(in, in_len, (char*)(*out), &st);
  }

static int get_encoded_len(int input_size)
  {
  // http://stackoverflow.com/questions/1533113/calculate-the-size-to-a-base-64-encoded-message
  unsigned int adjustment = ( (input_size % 3) ? (3 - (input_size % 3)) : 0);
  unsigned int code_padded_size = ( (input_size + adjustment) / 3) * 4;
  unsigned int newline_size = ((code_padded_size) / CHARS_PER_LINE) * 2;
  unsigned int total_size = code_padded_size + newline_size;
  
  return total_size + 10; // Who knows
  }

int bg_base64_encode(const uint8_t * in,
                     int in_len,
                     char ** out,
                     int * out_alloc)
  {
  base64_encodestate st;
  int len;
  int alloc_len = get_encoded_len(in_len);
  
  base64_init_encodestate(&st);

  if(*out_alloc < alloc_len)
    {
    *out_alloc = (alloc_len + 128);
    *out = realloc(*out, *out_alloc);
    }

  len = base64_encode_block((const char*)in, in_len,
                            *out, &st);

  len += base64_encode_blockend((*out)+len, &st);
  (*out)[len] = '\0';
  return len;
  }

char * bg_base64_encode_buffer(const gavl_buffer_t * buf)
  {
  char * ret = NULL;
  int ret_alloc = 0;

  if(!buf->len)
    return calloc(1, 1); // ""
  
  bg_base64_encode(buf->buf, buf->len, &ret, &ret_alloc);
  return ret;
  }

void bg_base64_decode_buffer(const char * str, gavl_buffer_t * buf)
  {
  gavl_buffer_reset(buf);
  buf->len = bg_base64_decode(str, -1, &buf->buf, &buf->alloc);
  }
