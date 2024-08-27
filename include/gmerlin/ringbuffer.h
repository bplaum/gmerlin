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




/*
 *   Ringbuffer
 *
 *   - One writer thread
 *   - Multiple reader threads
 * 
 *   - Reader threads block when no data is available
 *
 */

#ifndef BG_RINGBUFFER_H_INCLUDED
#define BG_RINGBUFFER_H_INCLUDED

/* Overwrite the buffer if the reader was too slow */
#define BG_RINGBUFFER_OVERWRITE     (1<<0)

/* Only one reader. */
#define BG_RINGBUFFER_SINGLE_READER (1<<1)

typedef struct bg_ring_buffer_s bg_ring_buffer_t;

typedef void * (*bg_ring_buffer_alloc_func)(void * priv);
typedef void (*bg_ring_buffer_free_func)(void * priv, void * buffer);
typedef void (*bg_ring_buffer_copy_func)(void * priv, void * dst, const void * src);

bg_ring_buffer_t * bg_ring_buffer_create(int num_elements,
                                         bg_ring_buffer_alloc_func alloc_func,
                                         bg_ring_buffer_free_func free_func,
                                         bg_ring_buffer_copy_func copy_func,
                                         void * priv, int flags);

void bg_ring_buffer_destroy(bg_ring_buffer_t * buf);

void bg_ring_buffer_write(bg_ring_buffer_t * buf, const void * data);

/* The sequence number identifies the buffer element. When you call read() the first time,
   set it to zero. When doing continuous reads, it is incremented by one after the call. 
   If was incremented by more than one, it means that data were skipped */
   
int bg_ring_buffer_read(bg_ring_buffer_t * buf, void * data, int64_t * seqno);

bg_ring_buffer_t * bg_ring_buffer_create_audio(int num_elements,
                                               gavl_audio_format_t * fmt,
                                               int flags);

#endif // BG_RINGBUFFER_H_INCLUDED
