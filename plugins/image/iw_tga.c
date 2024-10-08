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

#include <targa.h>

#include <gmerlin/log.h>
#define LOG_DOMAIN "iw_tga"

#define PADD(i, size) i = ((i + size - 1) / size) * size

/* TGA writer */

typedef struct
  {
  gavl_video_format_t format;
  int rle;

  bg_iw_callbacks_t * cb;
  char * filename;
  } tga_t;

static void * create_tga()
  {
  tga_t * ret;
  ret = calloc(1, sizeof(*ret));
  return ret;
  }

static void destroy_tga(void * priv)
  {
  tga_t * tga = priv;
  free(tga);
  }

static void set_callbacks_tga(void * data, bg_iw_callbacks_t * cb)
  {
  tga_t * e = data;
  e->cb = cb;
  }

static int write_header_tga(void * priv, const char * filename,
                            gavl_video_format_t * format,
                            const gavl_dictionary_t * m)
  {
  tga_t * tga = priv;

  if(gavl_pixelformat_has_alpha(format->pixelformat))
    format->pixelformat = GAVL_RGBA_32;
  else
    format->pixelformat = GAVL_BGR_24;

  gavl_video_format_copy(&tga->format, format);

  tga->filename = bg_filename_ensure_extension(filename, "tga");
  
  if(!bg_iw_cb_create_output_file(tga->cb, tga->filename))
    return 0;
  
  return 1;
  }

static int write_image_tga(void * priv, gavl_video_frame_t * frame)
  {
  tga_t * tga = priv;
  gavl_video_frame_t * tmp_frame;
  int result, ret = 1;

  errno = 0;

  if(tga->format.pixelformat == GAVL_RGBA_32)
    {
    tmp_frame = gavl_video_frame_create(&tga->format);
    gavl_video_frame_copy(&tga->format, tmp_frame, frame);
    if(tga->rle)
      {
      result = tga_write_rgb(tga->filename, tmp_frame->planes[0],
                             tga->format.image_width,
                             tga->format.image_height, 32,
                             frame->strides[0]);
      }
    else
      {
      result =tga_write_rgb_rle(tga->filename, tmp_frame->planes[0],
                                tga->format.image_width,
                                tga->format.image_height, 32,
                                frame->strides[0]);
      }
    gavl_video_frame_destroy(tmp_frame);
    }
  else
    {
    if(tga->rle)
      {
      result = tga_write_bgr(tga->filename, frame->planes[0],
                             tga->format.image_width,
                             tga->format.image_height, 24,
                             frame->strides[0]);
      }
    else
      {
      result = tga_write_bgr_rle(tga->filename, frame->planes[0],
                                 tga->format.image_width,
                                 tga->format.image_height, 24,
                                 frame->strides[0]);
      }
    }

  if(result != TGA_NOERR)
    {
    if(errno)
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Cannot save %s: %s",
             tga->filename, strerror(errno));
    else
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Cannot save %s: %s",
             tga->filename, tga_error(result));
    ret = 0;
    }

  free(tga->filename);
  tga->filename = NULL;
  
  return ret;
  }

/* Configuration stuff */

static const bg_parameter_info_t parameters[] =
  {
    {
      .name =        "rle",
      .long_name =   TRS("Do RLE compression"),
      .type =        BG_PARAMETER_CHECKBUTTON,
      .val_default = GAVL_VALUE_INIT_INT(0),
    },
    { /* End of parameters */ }
  };

static const bg_parameter_info_t * get_parameters_tga(void * p)
  {
  return parameters;
  }

static void set_parameter_tga(void * p, const char * name,
                              const gavl_value_t * val)
  {
  tga_t * tga;
  tga = p;
  
  if(!name)
    return;

  if(!strcmp(name, "rle"))
    tga->rle = val->v.i;
  }

static const char * get_extensions_tga(void * priv)
  {
  return "tga";
  }


const bg_image_writer_plugin_t the_plugin =
  {
    .common =
    {
      BG_LOCALE,
      .name =           "iw_tga",
      .long_name =      TRS("TGA writer"),
      .description =   TRS("Writer for TGA images"),
      .type =           BG_PLUGIN_IMAGE_WRITER,
      .flags =          BG_PLUGIN_FILE,
      .priority =       5,
      .create =         create_tga,
      .destroy =        destroy_tga,
      .get_parameters = get_parameters_tga,
      .set_parameter =  set_parameter_tga,
      .get_extensions = get_extensions_tga,
    },
    .set_callbacks = set_callbacks_tga,
    .write_header = write_header_tga,
    .write_image =  write_image_tga,
  };

/* Include this into all plugin modules exactly once
   to let the plugin loader obtain the API version */
BG_GET_PLUGIN_API_VERSION;
