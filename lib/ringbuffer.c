#include <inttypes.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>


#include <gavl/gavl.h>

#include <gmerlin/ringbuffer.h>



typedef struct buf_element_s
  {
  int64_t seqno;

  void * data;
    
  } buf_element_t;

struct bg_ring_buffer_s
  {
  int write_idx;
  
  int64_t seqno;
  
  int num_elements;
  buf_element_t * elements;
  
  bg_ring_buffer_free_func free_func;
  bg_ring_buffer_copy_func copy_func;
  void * priv;

  int flags;

  pthread_mutex_t mutex;
  
  };

bg_ring_buffer_t * bg_ring_buffer_create(int num_elements,
                                         bg_ring_buffer_alloc_func alloc_func,
                                         bg_ring_buffer_free_func free_func,
                                         bg_ring_buffer_copy_func copy_func,
                                         void * priv, int flags)
  {
  int i;
  bg_ring_buffer_t * ret;

  ret = calloc(1, sizeof(*ret));

  ret->free_func = free_func;
  ret->copy_func = copy_func;
  ret->priv = priv;

  ret->num_elements = num_elements;
  ret->elements = calloc(ret->num_elements, sizeof(*ret->elements));

  ret->seqno = 1;

  for(i = 0; i < ret->num_elements; i++)
    {
    ret->elements[i].data = alloc_func(priv);
    }

  pthread_mutex_init(&ret->mutex, NULL);

  ret->flags = flags;

  if(!(ret->flags & BG_RINGBUFFER_OVERWRITE))
    {
    fprintf(stderr, "buffers without BG_RINGBUFFER_OVERWRITE are not supported yet\n");
    return NULL;
    }
  if(!(ret->flags & BG_RINGBUFFER_SINGLE_READER))
    {
    fprintf(stderr, "buffers without BG_RINGBUFFER_SINGLE_READER are not supported yet\n");
    return NULL;
    }
  
  return ret;
  
  }

void bg_ring_buffer_destroy(bg_ring_buffer_t * buf)
  {
  int i;
  if(buf->elements)
    {
    for(i = 0; i < buf->num_elements; i++)
      {
      buf->free_func(buf->priv, buf->elements[i].data);
      }
    free(buf->elements);
    }
  pthread_mutex_destroy(&buf->mutex);
  free(buf);
  }

void bg_ring_buffer_write(bg_ring_buffer_t * buf, const void * data)
  {
  pthread_mutex_lock(&buf->mutex);

  if(!(buf->flags & BG_RINGBUFFER_OVERWRITE))
    {
    /* TODO: Wait until the buffer is consumed */
    }

  buf->copy_func(buf->priv, buf->elements[buf->write_idx].data, data);
  buf->elements[buf->write_idx].seqno = buf->seqno;
  buf->seqno++;

  buf->write_idx++;
  if(buf->write_idx == buf->num_elements)
    buf->write_idx = 0;
  
  pthread_mutex_unlock(&buf->mutex);
  }

/* The sequence number identifies the buffer element. When you call read() the first time,
   set it to zero. When doing continuous reads, it is incremented by one after the call. 
   If was incremented by more than one, it means that data were skipped */
   
int bg_ring_buffer_read(bg_ring_buffer_t * buf, void * data, int64_t * seqno)
  {
  int i;
  int min_idx = -1;
  int64_t min_seq = -1;

  int ret = 0;
  
  pthread_mutex_lock(&buf->mutex);

  /* First read: Take lowest seqno */
  if(*seqno <= 0)
    {
    for(i = 0; i < buf->num_elements; i++)
      {
      if((buf->elements[i].seqno > 0) &&
         ((min_seq < 0) || (min_seq > buf->elements[i].seqno)))
        {
        min_seq = buf->elements[i].seqno;
        min_idx = i;
        }
      }
    }
  else
    {
    
    for(i = 0; i < buf->num_elements; i++)
      {
      if(*seqno == buf->elements[i].seqno)
        {
        min_seq = buf->elements[i].seqno;
        min_idx = i;
        break;
        }
      
      if((buf->elements[i].seqno > 0) &&
         (*seqno < buf->elements[i].seqno) &&
         ((min_seq < 0) || (min_seq > buf->elements[i].seqno)))
        {
        min_seq = buf->elements[i].seqno;
        min_idx = i;
        }
      }

    
    }
  
  if(min_idx >= 0)
    {
    *seqno = min_seq + 1;
    buf->copy_func(buf->priv, data, buf->elements[min_idx].data);
    ret = 1;
    }
  
  pthread_mutex_unlock(&buf->mutex);

  return ret;
  }

/* Audio buffer */

static void * alloc_func_audio(void * priv)
  {
  return gavl_audio_frame_create(priv);
  }


static void free_func_audio(void * priv, void * buffer)
  {
  gavl_audio_frame_destroy(buffer);
  }

static void copy_func_audio(void * priv, void * dst1, const void * src1)
  {
  const gavl_audio_frame_t * src = src1;
  gavl_audio_frame_t * dst = dst1;
  gavl_audio_frame_copy(priv, dst, src, 0, 0,
                        src->valid_samples,
                        src->valid_samples);
  dst->valid_samples = src->valid_samples;
  }

bg_ring_buffer_t * bg_ring_buffer_create_audio(int num_elements,
                                               gavl_audio_format_t * fmt,
                                               int flags)
  {
  return bg_ring_buffer_create(num_elements,
                               alloc_func_audio,
                               free_func_audio,
                               copy_func_audio,
                               fmt, flags);
  }

