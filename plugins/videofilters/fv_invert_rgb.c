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
#include <string.h>
#include <stdio.h>

#include <config.h>
#include <gmerlin/translation.h>
#include <gmerlin/plugin.h>
#include <gmerlin/utils.h>
#include <gmerlin/log.h>
#include <gmerlin/colormatrix.h>

#define LOG_DOMAIN "fv_invert_rgb"

typedef struct invert_priv_s
  {
  bg_colormatrix_t * mat;
  
  gavl_video_format_t format;

  float coeffs[4][5];
  int invert[4];

  void (*process)(struct invert_priv_s * p, gavl_video_frame_t * f);

  gavl_video_options_t * global_opt;
  
  gavl_video_source_t * in_src;
  gavl_video_source_t * out_src;
  } invert_priv_t;

static const float coeffs_unity[4][5] =
  {
    { 1.0, 0.0, 0.0, 0.0, 0.0 },
    { 0.0, 1.0, 0.0, 0.0, 0.0 },
    { 0.0, 0.0, 1.0, 0.0, 0.0 },
    { 0.0, 0.0, 0.0, 1.0, 0.0 },
  };

static const float coeffs_invert[4][5] =
  {
    { -1.0,  0.0,  0.0,  0.0, 1.0 },
    {  0.0, -1.0,  0.0,  0.0, 1.0 },
    {  0.0,  0.0, -1.0,  0.0, 1.0 },
    {  0.0,  0.0,  0.0, -1.0, 1.0 },
  };

static void * create_invert()
  {
  invert_priv_t * ret;
  ret = calloc(1, sizeof(*ret));
  ret->mat = bg_colormatrix_create();
  ret->global_opt = gavl_video_options_create();
  
  return ret;
  }

static void destroy_invert(void * priv)
  {
  invert_priv_t * vp;
  vp = priv;
  bg_colormatrix_destroy(vp->mat);
  gavl_video_options_destroy(vp->global_opt);
  if(vp->out_src)
    gavl_video_source_destroy(vp->out_src);
  free(vp);
  }

static const bg_parameter_info_t parameters[] =
  {
    {
      .gettext_domain = PACKAGE,
      .gettext_directory = LOCALE_DIR,
      .name =        "r",
      .long_name =   TRS("Invert red"),
      .type =        BG_PARAMETER_CHECKBUTTON,
      .flags = BG_PARAMETER_SYNC,
      .val_default = GAVL_VALUE_INIT_INT(1),
    },
    {
      .name =        "g",
      .long_name =   TRS("Invert green"),
      .type =        BG_PARAMETER_CHECKBUTTON,
      .flags = BG_PARAMETER_SYNC,
      .val_default = GAVL_VALUE_INIT_INT(1),
    },
    {
      .name =        "b",
      .long_name =   TRS("Invert blue"),
      .type =        BG_PARAMETER_CHECKBUTTON,
      .flags = BG_PARAMETER_SYNC,
      .val_default = GAVL_VALUE_INIT_INT(1),
    },
    {
      .name =        "a",
      .long_name =   TRS("Invert alpha"),
      .type =        BG_PARAMETER_CHECKBUTTON,
      .flags = BG_PARAMETER_SYNC,
      .val_default = GAVL_VALUE_INIT_INT(1),
    },
    { /* End of parameters */ },
  };


static const bg_parameter_info_t * get_parameters_invert(void * priv)
  {
  return parameters;
  }

static void set_coeffs(invert_priv_t * vp)
  {
  int i, j;
  for(i = 0; i < 4; i++)
    {
    if(vp->invert[i])
      {
      for(j = 0; j < 5; j++)
        vp->coeffs[i][j] = coeffs_invert[i][j];
      }
    else
      {
      for(j = 0; j < 5; j++)
        vp->coeffs[i][j] = coeffs_unity[i][j];
      }
    }
  }

static void set_parameter_invert(void * priv, const char * name,
                               const gavl_value_t * val)
  {
  invert_priv_t * vp;
  int changed = 0;
  vp = priv;
  if(!name)
    return;
  
  if(!strcmp(name, "r"))
    {
    if(vp->invert[0] != val->v.i)
      {
      vp->invert[0] = val->v.i;
      changed = 1;
      }
    }
  else if(!strcmp(name, "g"))
    {
    if(vp->invert[1] != val->v.i)
      {
      vp->invert[1] = val->v.i;
      changed = 1;
      }
    }
  else if(!strcmp(name, "b"))
    {
    if(vp->invert[2] != val->v.i)
      {
      vp->invert[2] = val->v.i;
      changed = 1;
      }
    }
  else if(!strcmp(name, "a"))
    {
    if(vp->invert[3] != val->v.i)
      {
      vp->invert[3] = val->v.i;
      changed = 1;
      }
    }
  if(changed)
    {
    set_coeffs(vp);
    bg_colormatrix_set_rgba(vp->mat, vp->coeffs);
    }
  }

static void process_rgb24(invert_priv_t * vp, gavl_video_frame_t * frame)
  {
  int mask[3];
  int anti_mask[3];
  uint8_t * src;
  int i, j;
  mask[0] = vp->invert[0] ? 0x00 : 0xff;
  mask[1] = vp->invert[1] ? 0x00 : 0xff;
  mask[2] = vp->invert[2] ? 0x00 : 0xff;

  anti_mask[0] = ~mask[0];
  anti_mask[1] = ~mask[1];
  anti_mask[2] = ~mask[2];
  
  for(i = 0; i < vp->format.image_height; i++)
    {
    src = frame->planes[0] + i * frame->strides[0];
    /* The following should be faster than the 9 multiplications and 3
       additions per pixel */
    for(j = 0; j < vp->format.image_width; j++)
      {
      src[0] = (src[0] & mask[0]) | ((0xff - src[0]) & anti_mask[0]);
      src[1] = (src[1] & mask[1]) | ((0xff - src[1]) & anti_mask[1]);
      src[2] = (src[2] & mask[2]) | ((0xff - src[2]) & anti_mask[2]);
      src+=3;
      }
    }
  }

static void process_rgb32(invert_priv_t * vp, gavl_video_frame_t * frame)
  {
  int mask[3];
  int anti_mask[3];
  uint8_t * src;
  int i, j;
  mask[0] = vp->invert[0] ? 0x00 : 0xff;
  mask[1] = vp->invert[1] ? 0x00 : 0xff;
  mask[2] = vp->invert[2] ? 0x00 : 0xff;
  anti_mask[0] = ~mask[0];
  anti_mask[1] = ~mask[1];
  anti_mask[2] = ~mask[2];

  for(i = 0; i < vp->format.image_height; i++)
    {
    src = frame->planes[0] + i * frame->strides[0];
    /* The following should be faster than the 9 multiplications and 3
       additions per pixel */
    for(j = 0; j < vp->format.image_width; j++)
      {
      src[0] = (src[0] & mask[0]) | ((0xff - src[0]) & anti_mask[0]);
      src[1] = (src[1] & mask[1]) | ((0xff - src[1]) & anti_mask[1]);
      src[2] = (src[2] & mask[2]) | ((0xff - src[2]) & anti_mask[2]);
      src+=4;
      }
    }
  }

static void process_bgr24(invert_priv_t * vp, gavl_video_frame_t * frame)
  {
  int mask[3];
  int anti_mask[3];
  uint8_t * src;
  int i, j;
  mask[2] = vp->invert[0] ? 0x00 : 0xff;
  mask[1] = vp->invert[1] ? 0x00 : 0xff;
  mask[0] = vp->invert[2] ? 0x00 : 0xff;
  anti_mask[0] = ~mask[0];
  anti_mask[1] = ~mask[1];
  anti_mask[2] = ~mask[2];

  for(i = 0; i < vp->format.image_height; i++)
    {
    src = frame->planes[0] + i * frame->strides[0];
    /* The following should be faster than the 9 multiplications and 3
       additions per pixel */
    for(j = 0; j < vp->format.image_width; j++)
      {
      src[0] = (src[0] & mask[0]) | ((0xff - src[0]) & anti_mask[0]);
      src[1] = (src[1] & mask[1]) | ((0xff - src[1]) & anti_mask[1]);
      src[2] = (src[2] & mask[2]) | ((0xff - src[2]) & anti_mask[2]);
      src+=3;
      }
    }
  }

static void process_bgr32(invert_priv_t * vp, gavl_video_frame_t * frame)
  {
  int mask[3];
  int anti_mask[3];
  uint8_t * src;
  int i, j;
  mask[2] = vp->invert[0] ? 0x00 : 0xff;
  mask[1] = vp->invert[1] ? 0x00 : 0xff;
  mask[0] = vp->invert[2] ? 0x00 : 0xff;
  anti_mask[0] = ~mask[0];
  anti_mask[1] = ~mask[1];
  anti_mask[2] = ~mask[2];

  for(i = 0; i < vp->format.image_height; i++)
    {
    src = frame->planes[0] + i * frame->strides[0];
    /* The following should be faster than the 9 multiplications and 3
       additions per pixel */
    for(j = 0; j < vp->format.image_width; j++)
      {
      src[0] = (src[0] & mask[0]) | ((0xff - src[0]) & anti_mask[0]);
      src[1] = (src[1] & mask[1]) | ((0xff - src[1]) & anti_mask[1]);
      src[2] = (src[2] & mask[2]) | ((0xff - src[2]) & anti_mask[2]);
      src+=4;
      }
    }
  }

static void process_rgba32(invert_priv_t * vp, gavl_video_frame_t * frame)
  {
  int mask[4];
  int anti_mask[4];
  uint8_t * src;
  int i, j;
  mask[0] = vp->invert[0] ? 0x00 : 0xff;
  mask[1] = vp->invert[1] ? 0x00 : 0xff;
  mask[2] = vp->invert[2] ? 0x00 : 0xff;
  mask[3] = vp->invert[3] ? 0x00 : 0xff;
  anti_mask[0] = ~mask[0];
  anti_mask[1] = ~mask[1];
  anti_mask[2] = ~mask[2];
  anti_mask[3] = ~mask[3];

  for(i = 0; i < vp->format.image_height; i++)
    {
    src = frame->planes[0] + i * frame->strides[0];
    /* The following should be faster than the 9 multiplications and 3
       additions per pixel */
    for(j = 0; j < vp->format.image_width; j++)
      {
      src[0] = (src[0] & mask[0]) | ((0xff - src[0]) & anti_mask[0]);
      src[1] = (src[1] & mask[1]) | ((0xff - src[1]) & anti_mask[1]);
      src[2] = (src[2] & mask[2]) | ((0xff - src[2]) & anti_mask[2]);
      src[3] = (src[3] & mask[3]) | ((0xff - src[3]) & anti_mask[3]);
      src+=4;
      }
    }
  }


static void process_rgb48(invert_priv_t * vp, gavl_video_frame_t * frame)
  {
  int mask[3];
  int anti_mask[3];
  uint16_t * src;
  int i, j;
  mask[0] = vp->invert[0] ? 0x0000 : 0xffff;
  mask[1] = vp->invert[1] ? 0x0000 : 0xffff;
  mask[2] = vp->invert[2] ? 0x0000 : 0xffff;
  anti_mask[0] = ~mask[0];
  anti_mask[1] = ~mask[1];
  anti_mask[2] = ~mask[2];
  
  for(i = 0; i < vp->format.image_height; i++)
    {
    src = (uint16_t *)(frame->planes[0] + i * frame->strides[0]);
    /* The following should be faster than the 9 multiplications and 3
       additions per pixel */
    for(j = 0; j < vp->format.image_width; j++)
      {
      src[0] = (src[0] & mask[0]) | ((0xffff - src[0]) & anti_mask[0]);
      src[1] = (src[1] & mask[1]) | ((0xffff - src[1]) & anti_mask[1]);
      src[2] = (src[2] & mask[2]) | ((0xffff - src[2]) & anti_mask[2]);
      src+=3;
      }
    }
  }


static void process_rgba64(invert_priv_t * vp, gavl_video_frame_t * frame)
  {
  int mask[4];
  int anti_mask[4];
  uint16_t * src;
  int i, j;
  mask[0] = vp->invert[0] ? 0x0000 : 0xffff;
  mask[1] = vp->invert[1] ? 0x0000 : 0xffff;
  mask[2] = vp->invert[2] ? 0x0000 : 0xffff;
  mask[3] = vp->invert[3] ? 0x0000 : 0xffff;
  anti_mask[0] = ~mask[0];
  anti_mask[1] = ~mask[1];
  anti_mask[2] = ~mask[2];
  anti_mask[3] = ~mask[3];

  for(i = 0; i < vp->format.image_height; i++)
    {
    src = (uint16_t *)(frame->planes[0] + i * frame->strides[0]);
    /* The following should be faster than the 9 multiplications and 3
       additions per pixel */
    for(j = 0; j < vp->format.image_width; j++)
      {
      src[0] = (src[0] & mask[0]) | ((0xffff - src[0]) & anti_mask[0]);
      src[1] = (src[1] & mask[1]) | ((0xffff - src[1]) & anti_mask[1]);
      src[2] = (src[2] & mask[2]) | ((0xffff - src[2]) & anti_mask[2]);
      src[3] = (src[3] & mask[3]) | ((0xffff - src[3]) & anti_mask[3]);
      src+=4;
      }
    }
  }

static void process_rgb_float(invert_priv_t * vp, gavl_video_frame_t * frame)
  {
  float mask[3];
  float anti_mask[3];
  float * src;
  int i, j;
  mask[0] = vp->invert[0] ? 0.0 : 1.0;
  mask[1] = vp->invert[1] ? 0.0 : 1.0;
  mask[2] = vp->invert[2] ? 0.0 : 1.0;
  anti_mask[0] = 1.0 - mask[0];
  anti_mask[1] = 1.0 - mask[1];
  anti_mask[2] = 1.0 - mask[2];
  
  for(i = 0; i < vp->format.image_height; i++)
    {
    src = (float *)(frame->planes[0] + i * frame->strides[0]);
    /* The following should be faster than the 9 multiplications and 3
       additions per pixel */
    for(j = 0; j < vp->format.image_width; j++)
      {
      src[0] = (src[0] * mask[0]) + ((1.0 - src[0]) * anti_mask[0]);
      src[1] = (src[1] * mask[1]) + ((1.0 - src[1]) * anti_mask[1]);
      src[2] = (src[2] * mask[2]) + ((1.0 - src[2]) * anti_mask[2]);
      src+=3;
      }
    }
  }


static void process_rgba_float(invert_priv_t * vp, gavl_video_frame_t * frame)
  {
  float mask[4];
  float anti_mask[4];
  float * src;
  int i, j;
  mask[0] = vp->invert[0] ? 0.0 : 1.0;
  mask[1] = vp->invert[1] ? 0.0 : 1.0;
  mask[2] = vp->invert[2] ? 0.0 : 1.0;
  mask[3] = vp->invert[3] ? 0.0 : 1.0;
  anti_mask[0] = 1.0 - mask[0];
  anti_mask[1] = 1.0 - mask[1];
  anti_mask[2] = 1.0 - mask[2];
  anti_mask[3] = 1.0 - mask[3];
  
  for(i = 0; i < vp->format.image_height; i++)
    {
    src = (float *)(frame->planes[0] + i * frame->strides[0]);
    /* The following should be faster than the 9 multiplications and 3
       additions per pixel */
    for(j = 0; j < vp->format.image_width; j++)
      {
      src[0] = (src[0] * mask[0]) + ((1.0 - src[0]) * anti_mask[0]);
      src[1] = (src[1] * mask[1]) + ((1.0 - src[1]) * anti_mask[1]);
      src[2] = (src[2] * mask[2]) + ((1.0 - src[2]) * anti_mask[2]);
      src[3] = (src[3] * mask[3]) + ((1.0 - src[3]) * anti_mask[3]);
      src+=4;
      }
    }
  }


static void process_matrix(invert_priv_t * vp, gavl_video_frame_t * frame)
  {
  bg_colormatrix_process(vp->mat, frame);
  }

static void set_format(invert_priv_t * vp, const gavl_video_format_t * format)
  {
  gavl_video_format_copy(&vp->format, format);

  switch(vp->format.pixelformat)
    {
    case GAVL_RGB_24:
      vp->process = process_rgb24;
      break;
    case GAVL_RGB_32:
      vp->process = process_rgb32;
      break;
    case GAVL_BGR_24:
      vp->process = process_bgr24;
      break;
    case GAVL_BGR_32:
      vp->process = process_bgr32;
      break;
    case GAVL_RGBA_32:
      vp->process = process_rgba32;
      break;
    case GAVL_RGB_48:
      vp->process = process_rgb48;
      break;
    case GAVL_RGBA_64:
      vp->process = process_rgba64;
      break;
    case GAVL_RGB_FLOAT:
      vp->process = process_rgb_float;
      break;
    case GAVL_RGBA_FLOAT:
      vp->process = process_rgba_float;
      break;
    default:
      vp->process = process_matrix;
      bg_colormatrix_init(vp->mat, &vp->format, 0, vp->global_opt);
      break;
    }
  }

static gavl_source_status_t read_func(void * priv, gavl_video_frame_t ** frame)
  {
  gavl_source_status_t st;
  invert_priv_t * vp = priv;

  if((st = gavl_video_source_read_frame(vp->in_src, frame)) != GAVL_SOURCE_OK)
    return st;
  
  if(vp->invert[0] || vp->invert[1] || vp->invert[2] || vp->invert[3])
    vp->process(vp, *frame);
  return GAVL_SOURCE_OK;
  }

static gavl_video_source_t * connect_invert(void * priv, gavl_video_source_t * src,
                                            const gavl_video_options_t * opt)
  {
  invert_priv_t * vp;
  vp = priv;
  if(vp->out_src)
    gavl_video_source_destroy(vp->out_src);

  vp->in_src = src;
  set_format(vp, gavl_video_source_get_src_format(vp->in_src));
  if(opt)
    gavl_video_options_copy(vp->global_opt, opt);
  
  gavl_video_source_set_dst(vp->in_src, 0, &vp->format);

  vp->out_src =
    gavl_video_source_create_source(read_func,
                                    vp, 0,
                                    vp->in_src);
  return vp->out_src;
  }


const bg_fv_plugin_t the_plugin = 
  {
    .common =
    {
      BG_LOCALE,
      .name =      "fv_invert",
      .long_name = TRS("Invert RGBA"),
      .description = TRS("Invert single color channels. RGB(A) formats are processed directly, Y'CbCr(A) formats are processed with the colormatrix."),
      .type =     BG_PLUGIN_FILTER_VIDEO,
      .flags =    BG_PLUGIN_FILTER_1,
      .create =   create_invert,
      .destroy =   destroy_invert,
      .get_parameters =   get_parameters_invert,
      .set_parameter =    set_parameter_invert,
      .priority =         1,
    },
    
    .connect = connect_invert,
  };

/* Include this into all plugin modules exactly once
   to let the plugin loader obtain the API version */
BG_GET_PLUGIN_API_VERSION;
