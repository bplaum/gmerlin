

/*
 *   Ringbuffer
 *
 *   - One writer thread
 *   - Multiple reader threads
 * 
 *   - Reader threads block when no data is available
 *
 */

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
