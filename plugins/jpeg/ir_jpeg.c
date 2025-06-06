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
#include <stdio.h>
#include <setjmp.h>

#include <config.h>

#include <gmerlin/translation.h>
#include <gmerlin/plugin.h>
#include <gmerlin/utils.h>

#include <gmerlin/log.h>
#define LOG_DOMAIN "ir_jpeg"


#include <jpeglib.h>

#ifdef HAVE_LIBEXIF
#include "exif.h"
#endif  

#include <gavl/metatags.h>


#define PADD(i, size) i = ((i + size - 1) / size) * size

/* Custom error handling */

typedef struct 
  {
  struct jpeg_error_mgr pub;	/* "public" fields */
  jmp_buf setjmp_buffer;	/* for return to caller */
  char * filename;
  int level;
  } error_mgr_t;

static void my_error_exit(j_common_ptr cinfo)
  {
  /* cinfo->err really points to a my_error_mgr struct, so coerce pointer */
  error_mgr_t * myerr = (error_mgr_t*)cinfo->err;

  /* Always display the message. */
  /* We could postpone this until after returning, if we chose. */
  (*cinfo->err->output_message) (cinfo);
  
  /* Return control to the setjmp point */
  longjmp(myerr->setjmp_buffer, 1);
  }

static void my_output_message(j_common_ptr cinfo)
  {
  char buffer[JMSG_LENGTH_MAX];
  error_mgr_t * myerr = (error_mgr_t*)cinfo->err;

  /* Create the message */
  cinfo->err->format_message(cinfo, buffer);
  gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "%s: %s", myerr->filename, buffer);
  }

/* JPEG reader */

typedef struct
  {
  struct jpeg_decompress_struct cinfo;

  //  struct jpeg_error_mgr jerr;
  error_mgr_t jerr;
  
  // For reading planar YUV images
   
  uint8_t ** yuv_rows[3];
  uint8_t *  rows_0[16];
  uint8_t *  rows_1[16];
  uint8_t *  rows_2[16];
  
  gavl_video_format_t format;
  gavl_dictionary_t metadata;

  gavl_buffer_t buf;
  
  } jpeg_t;

static
void * create_jpeg()
  {
  jpeg_t * ret;
  ret = calloc(1, sizeof(*ret));
  ret->cinfo.err = jpeg_std_error(&ret->jerr.pub);
  ret->jerr.pub.error_exit = my_error_exit;
  ret->jerr.pub.output_message = my_output_message;
  
  if(setjmp(ret->jerr.setjmp_buffer))
    return NULL;
  
  jpeg_create_decompress(&ret->cinfo);

  ret->yuv_rows[0] = ret->rows_0;
  ret->yuv_rows[1] = ret->rows_1;
  ret->yuv_rows[2] = ret->rows_2;

  return ret;
  }

static void destroy_jpeg(void* priv)
  {
  jpeg_t * jpeg = priv;

  if(setjmp(jpeg->jerr.setjmp_buffer))
    goto end;

  if(jpeg->jerr.filename)
    free(jpeg->jerr.filename);
  
  jpeg_destroy_decompress(&jpeg->cinfo);

  end:
  gavl_dictionary_free(&jpeg->metadata);

  gavl_buffer_free(&jpeg->buf);
  free(jpeg);
  }

/* Get available parameters */

// bg_parameter_info_t * (*get_parameters_jpeg)(void * priv);

/* Set configuration parameter */
    
// bg_parameter_func set_parameter;

static
int read_header_jpeg(void * priv, const char * filename,
                     gavl_video_format_t * format)
  {
  jpeg_t * jpeg = priv;

  gavl_dictionary_reset(&jpeg->metadata);
  gavl_buffer_reset(&jpeg->buf);
  
  jpeg->jerr.filename = gavl_strdup(filename);
  
  if(!bg_read_location(filename, &jpeg->buf, 0, 0, NULL))
    return 0;
  
  if(setjmp(jpeg->jerr.setjmp_buffer))
    return 0;

  
  jpeg_mem_src(&jpeg->cinfo, jpeg->buf.buf, jpeg->buf.len);
  
  if(jpeg_read_header(&jpeg->cinfo, TRUE) != JPEG_HEADER_OK)
    {
    return 0;
    }

  format->image_width = jpeg->cinfo.image_width;
  format->image_height = jpeg->cinfo.image_height;

  format->frame_width  = jpeg->cinfo.image_width;
  format->frame_height = jpeg->cinfo.image_height;
  format->pixel_width = 1;
  format->pixel_height = 1;

  /*
   *  Get the colorspace, we handle YUV 444, YUV 422, YUV 420 directly.
   *  All other formats are converted to RGB24
   */
  
  switch(jpeg->cinfo.jpeg_color_space)
    {
    case JCS_YCbCr:
      if((jpeg->cinfo.comp_info[0].h_samp_factor == 2) &&
         (jpeg->cinfo.comp_info[0].v_samp_factor == 2) &&
         (jpeg->cinfo.comp_info[1].h_samp_factor == 1) &&
         (jpeg->cinfo.comp_info[1].v_samp_factor == 1) &&
         (jpeg->cinfo.comp_info[2].h_samp_factor == 1) &&
         (jpeg->cinfo.comp_info[2].v_samp_factor == 1))
        {
        format->pixelformat = GAVL_YUVJ_420_P;
        
        PADD(format->frame_width, 16);
        PADD(format->frame_height, 16);
        }
      else if((jpeg->cinfo.comp_info[0].h_samp_factor == 2) &&
              (jpeg->cinfo.comp_info[0].v_samp_factor == 1) &&
              (jpeg->cinfo.comp_info[1].h_samp_factor == 1) &&
              (jpeg->cinfo.comp_info[1].v_samp_factor == 1) &&
              (jpeg->cinfo.comp_info[2].h_samp_factor == 1) &&
              (jpeg->cinfo.comp_info[2].v_samp_factor == 1))
        {
        format->pixelformat = GAVL_YUVJ_422_P;
        PADD(format->frame_width, 16);
        PADD(format->frame_height, 8);
        }
      else if((jpeg->cinfo.comp_info[0].h_samp_factor == 1) &&
              (jpeg->cinfo.comp_info[0].v_samp_factor == 1) &&
              (jpeg->cinfo.comp_info[1].h_samp_factor == 1) &&
              (jpeg->cinfo.comp_info[1].v_samp_factor == 1) &&
              (jpeg->cinfo.comp_info[2].h_samp_factor == 1) &&
              (jpeg->cinfo.comp_info[2].v_samp_factor == 1))
        {
        format->pixelformat = GAVL_YUVJ_444_P;
        PADD(format->frame_width,  8);
        PADD(format->frame_height, 8);
        }
      else
        {
        format->pixelformat = GAVL_RGB_24;
        }
      break;
    case JCS_GRAYSCALE:
      format->pixelformat = GAVL_GRAY_8;
      break;
    default:
      format->pixelformat = GAVL_RGB_24;
    }

#ifdef HAVE_LIBEXIF
  bg_exif_read_metadata(&jpeg->buf, &jpeg->metadata, format);
#endif

  gavl_video_format_copy(&jpeg->format, format);


  gavl_dictionary_set_string(&jpeg->metadata, GAVL_META_FORMAT, "JPEG");
  gavl_dictionary_set_string(&jpeg->metadata, GAVL_META_MIMETYPE, "image/jpeg");

  
  return 1;
  }

static const gavl_dictionary_t * get_metadata_jpeg(void * priv)
  {
  jpeg_t * jpeg = priv;
  return &jpeg->metadata;
  }


static 
int read_image_jpeg(void * priv, gavl_video_frame_t * frame)
  {
  int i;
  int num_lines;
  jpeg_t * jpeg = priv;

  if(setjmp(jpeg->jerr.setjmp_buffer))
    return 0;
  
  if(!frame)
    {
    jpeg_abort_decompress(&jpeg->cinfo);
    
    if(jpeg->jerr.filename)
      {
      free(jpeg->jerr.filename);
      jpeg->jerr.filename = NULL;
      }
    return 1;
    }
  
  if((jpeg->format.pixelformat != GAVL_RGB_24) &&
     (jpeg->format.pixelformat != GAVL_GRAY_8))
    jpeg->cinfo.raw_data_out = TRUE;
  
  jpeg_start_decompress(&jpeg->cinfo);
  
  switch(jpeg->format.pixelformat)
    {
    case GAVL_RGB_24:
    case GAVL_GRAY_8:
      while(jpeg->cinfo.output_scanline < jpeg->cinfo.output_height)
        {
        for(i = 0; i < 16; i++)
          {
          jpeg->rows_0[i] = frame->planes[0] + frame->strides[0] *
            (jpeg->cinfo.output_scanline + i);
          }
        num_lines = jpeg->cinfo.output_height - jpeg->cinfo.output_scanline;
        if(num_lines > 16)
          num_lines = 16;
        jpeg_read_scanlines(&jpeg->cinfo,
                            (JSAMPLE**)(jpeg->rows_0), num_lines);
        }
      break;
    case GAVL_YUVJ_420_P:
      while(jpeg->cinfo.output_scanline < jpeg->cinfo.output_height)
        {
        for(i = 0; i < 16; i++)
          {
          jpeg->rows_0[i] = frame->planes[0] + frame->strides[0] *
            (jpeg->cinfo.output_scanline + i);
          }
        for(i = 0; i < 8; i++)
          {
          jpeg->rows_1[i] = frame->planes[1] + frame->strides[1] *
            (jpeg->cinfo.output_scanline/2 + i);
          jpeg->rows_2[i] = frame->planes[2] + frame->strides[2] *
            (jpeg->cinfo.output_scanline/2 + i);
          }
        num_lines = jpeg->cinfo.output_height - jpeg->cinfo.output_scanline;
        if(num_lines > 16)
          num_lines = 16;
        jpeg_read_raw_data(&jpeg->cinfo, jpeg->yuv_rows, 16);
        }
      break;
    case GAVL_YUVJ_422_P:
    case GAVL_YUVJ_444_P:
      while(jpeg->cinfo.output_scanline < jpeg->cinfo.output_height)
        {
        for(i = 0; i < 8; i++)
          {
          jpeg->rows_0[i] = frame->planes[0] + frame->strides[0] *
            (jpeg->cinfo.output_scanline + i);
          jpeg->rows_1[i] = frame->planes[1] + frame->strides[1] *
            (jpeg->cinfo.output_scanline + i);
          jpeg->rows_2[i] = frame->planes[2] + frame->strides[2] *
            (jpeg->cinfo.output_scanline + i);
          }
        num_lines = jpeg->cinfo.output_height - jpeg->cinfo.output_scanline;
        if(num_lines > 8)
          num_lines = 8;
        jpeg_read_raw_data(&jpeg->cinfo, jpeg->yuv_rows, 8);
        }
      break;
    default:
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Illegal pixelformat");
      return 0;
    }
  jpeg_finish_decompress(&jpeg->cinfo);

  if(jpeg->jerr.filename)
    {
    free(jpeg->jerr.filename);
    jpeg->jerr.filename = NULL;
    }
  
  /* TODO: Handle rotation */
  
  
  return 1;
  }

static int get_compression_info_jpeg(void * priv, gavl_compression_info_t * ci)
  {
  ci->id = GAVL_CODEC_ID_JPEG;
  return 1;
  }

static const char * get_extensions_jpeg(void * priv)
  {
  return "jpeg jpg";
  }

const bg_image_reader_plugin_t the_plugin =
  {
    .common =
    {
      BG_LOCALE,
      .name =          "ir_jpeg",
      .long_name =     TRS("JPEG reader"),
      .description =   TRS("Reader for JPEG images"),
      .type =          BG_PLUGIN_IMAGE_READER,
      .flags =         BG_PLUGIN_FILE,
      .priority =      BG_PLUGIN_PRIORITY_MAX,
      .create =        create_jpeg,
      .destroy =       destroy_jpeg,
      .get_extensions = get_extensions_jpeg,
    },
    .mimetypes  = "image/jpeg",
    .read_header = read_header_jpeg,
    .get_metadata = get_metadata_jpeg,
    .get_compression_info = get_compression_info_jpeg,
    .read_image =  read_image_jpeg,
  };

/* Include this into all plugin modules exactly once
   to let the plugin loader obtain the API version */
BG_GET_PLUGIN_API_VERSION;
