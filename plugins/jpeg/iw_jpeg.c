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
#include <string.h>
#include <errno.h>

#include <config.h>
#include <gmerlin/translation.h>
#include <gmerlin/plugin.h>
#include <gmerlin/pluginfuncs.h>
#include <gmerlin/utils.h>

#include <gmerlin/log.h>
#define LOG_DOMAIN "iw_jpeg"

#include <jpeglib.h>

#ifdef HAVE_LIBEXIF
#include "exif.h"
#endif  

#define PADD(i, size) i = ((i + size - 1) / size) * size

/* JPEG writer */

typedef struct
  {
  //  char * filename;
  struct jpeg_compress_struct cinfo;
  struct jpeg_error_mgr jerr;
 
  // For writing planar YUV images
   
  uint8_t ** yuv_rows[3];
  uint8_t *  rows_0[16];
  uint8_t *  rows_1[16];
  uint8_t *  rows_2[16];
  
  FILE * output;

  gavl_pixelformat_t pixelformat;
  
  int quality;

  bg_iw_callbacks_t * cb;

  } jpeg_t;

static void * create_jpeg()
  {
  jpeg_t * ret;
  ret = calloc(1, sizeof(*ret));
  ret->cinfo.err = jpeg_std_error(&ret->jerr);
  jpeg_create_compress(&ret->cinfo);

  ret->yuv_rows[0] = ret->rows_0;
  ret->yuv_rows[1] = ret->rows_1;
  ret->yuv_rows[2] = ret->rows_2;

  return ret;
  }

static void set_callbacks_jpeg(void * data, bg_iw_callbacks_t * cb)
  {
  jpeg_t * e = data;
  e->cb = cb;
  }

static void destroy_jpeg(void * priv)
  {
  jpeg_t * jpeg = priv;
  jpeg_destroy_compress(&jpeg->cinfo);
  free(jpeg);
  }

static
int write_header_jpeg(void * priv, const char * filename,
                      gavl_video_format_t * format,
                      const gavl_dictionary_t * metadata)
  {
  jpeg_t * jpeg = priv;

  char * real_filename;
  
  real_filename = bg_filename_ensure_extension(filename, bg_mimetype_to_ext("image/jpeg"));
  
  if(!bg_iw_cb_create_output_file(jpeg->cb, real_filename))
    {
    free(real_filename);
    return 0;
    }
  
  jpeg->output = fopen(real_filename, "wb");
  free(real_filename);

  if(!jpeg->output)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Cannot open %s: %s",
           filename, strerror(errno));
    return 0;
    }
  jpeg_stdio_dest(&jpeg->cinfo, jpeg->output);

  jpeg->cinfo.image_width  = format->image_width;
  jpeg->cinfo.image_height = format->image_height;

  /* We always have 3 components */

  jpeg->cinfo.input_components = 3;

  /* Set defaults */

  jpeg_set_defaults(&jpeg->cinfo);
  
  /* Adjust pixelformats */

  format->pixelformat = jpeg->pixelformat;

  jpeg_set_colorspace(&jpeg->cinfo, JCS_YCbCr);
  jpeg->cinfo.raw_data_in = TRUE;

  format->chroma_placement = GAVL_CHROMA_PLACEMENT_DEFAULT;
  format->interlace_mode = GAVL_INTERLACE_NONE;
  
  switch(format->pixelformat)
    {
    case GAVL_YUVJ_420_P:
      jpeg->cinfo.comp_info[0].h_samp_factor = 2;
      jpeg->cinfo.comp_info[0].v_samp_factor = 2;

      jpeg->cinfo.comp_info[1].h_samp_factor = 1;
      jpeg->cinfo.comp_info[1].v_samp_factor = 1;

      jpeg->cinfo.comp_info[2].h_samp_factor = 1;
      jpeg->cinfo.comp_info[2].v_samp_factor = 1;

      PADD(format->frame_width, 16);
      PADD(format->frame_height, 16);
      break;
    case GAVL_YUVJ_422_P:
      jpeg->cinfo.comp_info[0].h_samp_factor = 2;
      jpeg->cinfo.comp_info[0].v_samp_factor = 1;
      
      jpeg->cinfo.comp_info[1].h_samp_factor = 1;
      jpeg->cinfo.comp_info[1].v_samp_factor = 1;

      jpeg->cinfo.comp_info[2].h_samp_factor = 1;
      jpeg->cinfo.comp_info[2].v_samp_factor = 1;

      PADD(format->frame_width, 16);
      PADD(format->frame_height, 8);
            
      break;
    case GAVL_YUVJ_444_P:
      jpeg->cinfo.comp_info[0].h_samp_factor = 1;
      jpeg->cinfo.comp_info[0].v_samp_factor = 1;
      
      jpeg->cinfo.comp_info[1].h_samp_factor = 1;
      jpeg->cinfo.comp_info[1].v_samp_factor = 1;

      jpeg->cinfo.comp_info[2].h_samp_factor = 1;
      jpeg->cinfo.comp_info[2].v_samp_factor = 1;

      PADD(format->frame_width,  8);
      PADD(format->frame_height, 8);
      
      break;
    default:
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Illegal pixelformat: %s",
             gavl_pixelformat_to_string(jpeg->pixelformat));      
      break;
    }

  /* Set compression parameters */
#if JPEG_LIB_VERSION >= 70
  jpeg->cinfo.do_fancy_downsampling = FALSE;
#endif

  jpeg_set_quality(&jpeg->cinfo, jpeg->quality, TRUE);
  jpeg_start_compress(&jpeg->cinfo, TRUE);

  /* Write EXIF */

#ifdef HAVE_LIBEXIF
  if(metadata)
    {
    gavl_buffer_t buf;
    gavl_buffer_init(&buf);
    bg_exif_write_metadata(metadata, format, &buf);
    jpeg_write_marker(&jpeg->cinfo, JPEG_APP0 + 1, buf.buf, buf.len);
    }
#endif  
 
  return 1;
  }

static
int write_image_jpeg(void * priv, gavl_video_frame_t * frame)
  {
  int i;
  int num_lines;
  jpeg_t * jpeg = priv;

  switch(jpeg->pixelformat)
    {
    case GAVL_YUVJ_420_P:
      while(jpeg->cinfo.next_scanline < jpeg->cinfo.image_height)
        {
        for(i = 0; i < 16; i++)
          {
          jpeg->rows_0[i] = frame->planes[0] + frame->strides[0] *
            (jpeg->cinfo.next_scanline + i);
          }
        for(i = 0; i < 8; i++)
          {
          jpeg->rows_1[i] = frame->planes[1] + frame->strides[1] *
            (jpeg->cinfo.next_scanline/2 + i);
          jpeg->rows_2[i] = frame->planes[2] + frame->strides[2] *
            (jpeg->cinfo.next_scanline/2 + i);
          }
        num_lines = jpeg->cinfo.image_height - jpeg->cinfo.next_scanline;
        if(num_lines > 16)
          num_lines = 16;
        jpeg_write_raw_data(&jpeg->cinfo, jpeg->yuv_rows, 16);
        }
      break;
    case GAVL_YUVJ_422_P:
    case GAVL_YUVJ_444_P:
      while(jpeg->cinfo.next_scanline < jpeg->cinfo.image_height)
        {
        for(i = 0; i < 8; i++)
          {
          jpeg->rows_0[i] = frame->planes[0] + frame->strides[0] *
            (jpeg->cinfo.next_scanline + i);
          jpeg->rows_1[i] = frame->planes[1] + frame->strides[1] *
            (jpeg->cinfo.next_scanline + i);
          jpeg->rows_2[i] = frame->planes[2] + frame->strides[2] *
            (jpeg->cinfo.next_scanline + i);
          }
        num_lines = jpeg->cinfo.image_height - jpeg->cinfo.next_scanline;
        if(num_lines > 8)
          num_lines = 8;
        jpeg_write_raw_data(&jpeg->cinfo, jpeg->yuv_rows, 8);
        }
      break;
    default:
      return 0;
    }
  jpeg_finish_compress(&jpeg->cinfo);
  fclose(jpeg->output);
  return 1;
  }

/* Configuration stuff */

static const bg_parameter_info_t parameters[] =
  {
    {
      .name =        "quality",
      .long_name =   TRS("Quality"),
      .type =        BG_PARAMETER_SLIDER_INT,
      .val_min =     GAVL_VALUE_INIT_INT(0),
      .val_max =     GAVL_VALUE_INIT_INT(100),
      .val_default = GAVL_VALUE_INIT_INT(95),
    },
    {
      .name =               "chroma_sampling",
      .long_name =          TRS("Chroma sampling"),
      .type =               BG_PARAMETER_STRINGLIST,
      .val_default =        GAVL_VALUE_INIT_STRING("4:2:0"),
      .multi_names = (char const *[]) { "4:2:0",
                           "4:2:2",
                           "4:4:4",
                           NULL },
      
    },
    { /* End of parameters */ }
  };

static const bg_parameter_info_t * get_parameters_jpeg(void * p)
  {
  return parameters;
  }


static void set_parameter_jpeg(void * p, const char * name,
                               const gavl_value_t * val)
  {
  jpeg_t * jpeg;
  jpeg = p;
  
  if(!name)
    return;

  if(!strcmp(name, "quality"))
    jpeg->quality = val->v.i;
  else if(!strcmp(name, "chroma_sampling"))
    {
    if(!strcmp(val->v.str, "4:2:0"))
      {
      jpeg->pixelformat = GAVL_YUVJ_420_P;
      }
    else if(!strcmp(val->v.str, "4:2:2"))
      {
      jpeg->pixelformat = GAVL_YUVJ_422_P;
      }
    else if(!strcmp(val->v.str, "4:4:4"))
      {
      jpeg->pixelformat = GAVL_YUVJ_444_P;
      }
    }
  }

static const char * get_extensions_jpeg(void * priv)
  {
  return "jpeg jpg";
  }

const bg_image_writer_plugin_t the_plugin =
  {
    .common =
    {
      BG_LOCALE,
      .name =           "iw_jpeg",
      .long_name =      TRS("JPEG writer"),
      .description =    TRS("Writer for JPEG images"),
      .type =           BG_PLUGIN_IMAGE_WRITER,
      .flags =          BG_PLUGIN_FILE,
      .priority =       BG_PLUGIN_PRIORITY_MAX,
      .create =         create_jpeg,
      .destroy =        destroy_jpeg,
      .get_parameters = get_parameters_jpeg,
      .set_parameter =  set_parameter_jpeg,
      .get_extensions = get_extensions_jpeg,
    },
    .mimetypes = "image/jpeg",
    .set_callbacks = set_callbacks_jpeg,
    .write_header = write_header_jpeg,
    .write_image =  write_image_jpeg,
  };

/* Include this into all plugin modules exactly once
   to let the plugin loader obtain the API version */
BG_GET_PLUGIN_API_VERSION;
