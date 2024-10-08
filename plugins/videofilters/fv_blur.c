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
#include <math.h>

#include <config.h>
#include <gmerlin/translation.h>
#include <gmerlin/plugin.h>
#include <gmerlin/utils.h>
#include <gmerlin/log.h>


#define LOG_DOMAIN "fv_blur"

#define MODE_GAUSS      0
#define MODE_TRIANGULAR 1
#define MODE_BOX        2

typedef struct
  {
  int mode;
  float radius_h;
  float radius_v;
  int blur_chroma;
  int correct_nonsquare;
  int changed;
  gavl_video_options_t * opt;
  gavl_video_scaler_t * scaler;
  gavl_video_options_t * global_opt;
  
  gavl_video_format_t format;

  gavl_video_source_t * in_src;
  gavl_video_source_t * out_src;

  } blur_priv_t;

static void * create_blur()
  {
  blur_priv_t * ret;
  int flags;
  ret = calloc(1, sizeof(*ret));
  ret->scaler = gavl_video_scaler_create();
  ret->opt = gavl_video_scaler_get_options(ret->scaler);
  flags = gavl_video_options_get_conversion_flags(ret->opt);
  flags |= GAVL_CONVOLVE_NORMALIZE;
  gavl_video_options_set_conversion_flags(ret->opt,
                                          flags);

  ret->global_opt = gavl_video_options_create();
  return ret;
  }

static void transfer_global_options(gavl_video_options_t * opt,
                                    const gavl_video_options_t * global_opt)
  {
  gavl_video_options_set_quality(opt, gavl_video_options_get_quality(global_opt));
  gavl_video_options_set_thread_pool(opt, gavl_video_options_get_thread_pool(global_opt));
  
  }


static void destroy_blur(void * priv)
  {
  blur_priv_t * vp;
  vp = priv;
  if(vp->scaler)
    gavl_video_scaler_destroy(vp->scaler);
  if(vp->out_src)
    gavl_video_source_destroy(vp->out_src);

  gavl_video_options_destroy(vp->global_opt);

  
  free(vp);
  }

static float get_coeff_rectangular(float radius)
  {
  if(radius <= 1.0)
    return radius;
  return 1.0;
  }

static float get_coeff_triangular(float radius)
  {
  if(radius <= 1.0)
    return 1.0 - (1.0-radius)*(1.0-radius);
  return 1.0;
  }

static float get_coeff_gauss(float radius)
  {
  return erf(radius);
  }


static float * get_coeffs(float radius, int * r_i, int mode)
  {
  float * ret;
  float coeff, last_coeff;
  int i;
  float (*get_coeff)(float);
  if(radius == 0.0)
    return NULL;
  switch(mode)
    {
    case MODE_GAUSS:
      get_coeff = get_coeff_gauss;
      *r_i = 2*(int)(radius + 0.4999);
      break;
    case MODE_TRIANGULAR:
      get_coeff = get_coeff_triangular;
      *r_i = (int)(radius + 0.4999);
      break;
    case MODE_BOX:
      get_coeff = get_coeff_rectangular;
      *r_i = (int)(radius + 0.4999);
      break;
    default:
      return NULL;
    }
  if(*r_i < 1)
    return NULL;
  /* Allocate and set return values */
  ret = malloc(((*r_i * 2) + 1)*sizeof(*ret));

//  ret[*r_i] =   
  last_coeff = 0.0;
  coeff = get_coeff(0.5 / radius);
  ret[*r_i] = 2.0 * coeff;

  for(i = 1; i <= *r_i; i++)
    {
    last_coeff = coeff;
    coeff = get_coeff((i+0.5) / radius);
    ret[*r_i+i] = coeff - last_coeff;
    ret[*r_i-i] = ret[*r_i+i];
    }
  return ret;
  }

static void init_scaler(blur_priv_t * vp)
  {
  float * coeffs_h = NULL;
  float * coeffs_v = NULL;
  int num_coeffs_h = 0;
  int num_coeffs_v = 0;
  float radius_h;
  float radius_v;
  float pixel_aspect;
  int flags;
  
  radius_h = vp->radius_h;
  radius_v = vp->radius_v;

  flags = gavl_video_options_get_conversion_flags(vp->opt);
  if(vp->blur_chroma)
    flags |= GAVL_CONVOLVE_CHROMA;
  else
    flags &= ~GAVL_CONVOLVE_CHROMA;
  gavl_video_options_set_conversion_flags(vp->opt,
                                          flags);

  if(vp->correct_nonsquare)
    {
    pixel_aspect = 
      (float)(vp->format.pixel_width) / 
      (float)(vp->format.pixel_height);
    pixel_aspect = sqrt(pixel_aspect);
    radius_h /= pixel_aspect;
    radius_v *= pixel_aspect;
    }
  coeffs_h = get_coeffs(radius_h, &num_coeffs_h, vp->mode);
  coeffs_v = get_coeffs(radius_v, &num_coeffs_v, vp->mode);

  transfer_global_options(vp->opt, vp->global_opt);
  
  gavl_video_scaler_init_convolve(vp->scaler,
                                  &vp->format,
                                  num_coeffs_h, coeffs_h,
                                  num_coeffs_v, coeffs_v);
  if(coeffs_h) free(coeffs_h);
  if(coeffs_v) free(coeffs_v);

  vp->changed = 0;
  }

static const bg_parameter_info_t parameters[] =
  {
    {
      .gettext_domain = PACKAGE,
      .gettext_directory = LOCALE_DIR,
      .name = "mode",
      .long_name = TRS("Mode"),
      .type = BG_PARAMETER_STRINGLIST,
      .flags = BG_PARAMETER_SYNC,
      .val_default = GAVL_VALUE_INIT_STRING("gauss"),
      .multi_names = (char const *[]){ "gauss", "triangular", "box", 
                              NULL },
      .multi_labels = (char const *[]){ TRS("Gauss"), 
                               TRS("Triangular"), 
                               TRS("Rectangular"),
                              NULL },
    },
    {
      .name = "radius_h",
      .long_name = TRS("Horizontal radius"),
      .type = BG_PARAMETER_FLOAT,
      .flags = BG_PARAMETER_SYNC,
      .val_min = GAVL_VALUE_INIT_FLOAT(0.5),
      .val_max = GAVL_VALUE_INIT_FLOAT(50.0),
      .val_default = GAVL_VALUE_INIT_FLOAT(0.5),
      .num_digits = 1,
    },
    {
      .name = "radius_v",
      .long_name = TRS("Vertical radius"),
      .type = BG_PARAMETER_FLOAT,
      .flags = BG_PARAMETER_SYNC,
      .val_min = GAVL_VALUE_INIT_FLOAT(0.5),
      .val_max = GAVL_VALUE_INIT_FLOAT(50.0),
      .val_default = GAVL_VALUE_INIT_FLOAT(0.5),
      .num_digits = 1,
    },
    {
      .name = "blur_chroma",
      .long_name = "Blur chroma planes",
      .type = BG_PARAMETER_CHECKBUTTON,
      .val_default = GAVL_VALUE_INIT_INT(0),
      .flags = BG_PARAMETER_SYNC,
    },
    {
      .name = "correct_nonsquare",
      .long_name = "Correct radii for nonsquare pixels",
      .type = BG_PARAMETER_CHECKBUTTON,
      .val_default = GAVL_VALUE_INIT_INT(0),
      .flags = BG_PARAMETER_SYNC,
    },
    { /* End of parameters */ },
  };

static const bg_parameter_info_t * get_parameters_blur(void * priv)
  {
  return parameters;
  }

static void set_parameter_blur(void * priv, const char * name,
                               const gavl_value_t * val)
  {
  blur_priv_t * vp;
  vp = priv;

  if(!name)
    return;

  if(!strcmp(name, "radius_h"))
    {
    if(vp->radius_h != val->v.d)
      {
      vp->radius_h = val->v.d;
      vp->changed = 1;
      }
    }
  else if(!strcmp(name, "radius_v"))
    {
    if(vp->radius_v != val->v.d)
      {
      vp->radius_v = val->v.d;
      vp->changed = 1;
      }
    }
  else if(!strcmp(name, "correct_nonsquare"))
    {
    if(vp->correct_nonsquare != val->v.i)
      {
      vp->correct_nonsquare= val->v.i;
      vp->changed = 1;
      }
    }
  else if(!strcmp(name, "blur_chroma"))
    {
    if(vp->blur_chroma != val->v.i)
      {
      vp->blur_chroma = val->v.i;
      vp->changed = 1;
      }
    }
  else if(!strcmp(name, "mode"))
    {
    if(!strcmp(val->v.str, "gauss"))
      vp->mode = MODE_GAUSS;
    else if(!strcmp(val->v.str, "triangular"))
      vp->mode = MODE_TRIANGULAR;
    else if(!strcmp(val->v.str, "box"))
      vp->mode = MODE_BOX;
    vp->changed = 1;
    }
  }

static gavl_source_status_t read_func(void * priv,
                                      gavl_video_frame_t ** f)
  {
  gavl_source_status_t st;

  blur_priv_t * vp;
  
  vp = priv;

  if((vp->radius_h != 0.0) || (vp->radius_v != 0.0))
    {
    gavl_video_frame_t * in_frame = NULL;
    if((st = gavl_video_source_read_frame(vp->in_src, &in_frame)) !=
       GAVL_SOURCE_OK)
      return st;
    if(vp->changed)
      init_scaler(vp);
    
    gavl_video_scaler_scale(vp->scaler, in_frame, *f);
    gavl_video_frame_copy_metadata(*f, in_frame);
    return GAVL_SOURCE_OK;
    }
  else
    return gavl_video_source_read_frame(vp->in_src, f);
  }

static gavl_video_source_t * connect_blur(void * priv,
                                          gavl_video_source_t * src,
                                          const gavl_video_options_t * opt)
  {
  blur_priv_t * vp;
  vp = priv;
  if(vp->out_src)
    gavl_video_source_destroy(vp->out_src);

  vp->in_src = src;
  gavl_video_format_copy(&vp->format,
                         gavl_video_source_get_src_format(vp->in_src));
  
  if(opt)
    gavl_video_options_copy(vp->global_opt, opt);
  
  gavl_video_source_set_dst(vp->in_src, 0, &vp->format);
  vp->changed = 1;

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
      .name =      "fv_blur",
      .long_name = TRS("Blur"),
      .description = TRS("Blur filter based on gavl. Supports triangular, box and gauss blur."),
      .type =     BG_PLUGIN_FILTER_VIDEO,
      .flags =    BG_PLUGIN_FILTER_1,
      .create =   create_blur,
      .destroy =   destroy_blur,
      .get_parameters =   get_parameters_blur,
      .set_parameter =    set_parameter_blur,
      .priority =         1,
    },

    .connect = connect_blur,
  };

/* Include this into all plugin modules exactly once
   to let the plugin loader obtain the API version */
BG_GET_PLUGIN_API_VERSION;
