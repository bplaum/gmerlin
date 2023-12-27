#include <gmerlin/plugin.h>

typedef struct decoder_s decoder_t;

struct decoder_s
  {
  void * priv;

  gavl_packet_source_t * psrc;
  gavl_video_source_t * vsrc; /* Owned */

  void (*decode)(decoder_t * dec, gavl_packet_t * p, gavl_video_frame_t * frame);
  void (*cleanup)(decoder_t * dec);
  
  gavl_video_format_t * fmt;
  gavl_video_frame_t * tmp_frame;
  };

/* Helper fuction */

void init_p207(decoder_t * dec);

