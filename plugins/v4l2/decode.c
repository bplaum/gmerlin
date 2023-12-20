
#include <config.h>
#include <gmerlin/translation.h>

#include "decode.h"

static void * create_decoder()
  {
  decoder_t * dec = calloc(1, sizeof(*dec));
  return dec;
  }

static gavl_source_status_t read_video(void * priv, gavl_video_frame_t ** f)
  {
  gavl_source_status_t st;
  gavl_packet_t * p = NULL;
  decoder_t * dec = priv;
  
  if((st = gavl_packet_source_read_packet(dec->psrc, &p)) != GAVL_SOURCE_OK)
    return st;

  dec->decode(dec, p, *f);
  gavl_packet_to_videoframe(p, *f);
  return GAVL_SOURCE_OK;
  }
  
static gavl_video_source_t * open_decode_video(void * priv,
                                               gavl_packet_source_t * src,
                                               gavl_dictionary_t * stream)
  {
  gavl_compression_info_t ci;
  decoder_t * dec = priv;

  gavl_compression_info_init(&ci);
  gavl_stream_get_compression_info(stream, &ci);

  dec->fmt = gavl_stream_get_video_format_nc(stream);
  dec->psrc = src;
  
  switch(ci.codec_tag)
    {
    case GAVL_MK_FOURCC('P', '2', '0', '7'):
      init_p207(dec);
      break;
    default:
      break;
    }

  dec->vsrc = gavl_video_source_create(read_video, dec, 0, dec->fmt);
  return dec->vsrc;
  }

static const uint32_t codec_tags[] = 
  {
    GAVL_MK_FOURCC('P', '2', '0', '7'),
    0,
  };

static const uint32_t * get_codec_tags(void * priv)
  {
  return codec_tags;
  }

static void destroy_decoder(void * priv)
  {
  decoder_t * dec = priv;
  if(dec->vsrc)
    gavl_video_source_destroy(dec->vsrc);
  if(dec->tmp_frame)
    gavl_video_frame_destroy(dec->tmp_frame);
  if(dec->cleanup)
    dec->cleanup(dec);
  free(dec);
  }

const bg_codec_plugin_t the_plugin =
  {
    .common =
    {
      BG_LOCALE,
      .name =            "c_v4l2",       /* Unique short name */
      .long_name =       "V4L2 decoder",
      .description =     "Decoder for formats from V4L2 drivers",
      .type =            BG_PLUGIN_DECOMPRESSOR_VIDEO,
      .flags =           0,
      .priority =        5,
      .create =          create_decoder,
      .destroy =         destroy_decoder,
    },
    .open_decode_video = open_decode_video,
    .get_codec_tags = get_codec_tags,
  };
/* Include this into all plugin modules exactly once
   to let the plugin loader obtain the API version */
BG_GET_PLUGIN_API_VERSION;
