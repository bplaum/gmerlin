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

#define LOG_DOMAIN "fv_colormatrix_rgb"

typedef struct
  {
  bg_colormatrix_t * mat;
  
  
  gavl_video_format_t format;

  
  float coeffs[4][5];
  int force_alpha;
  int need_restart;

  gavl_video_options_t * global_opt;

  gavl_video_source_t * in_src;
  gavl_video_source_t * out_src;

  } colormatrix_priv_t;

static int need_restart_colormatrix(void * priv)
  {
  colormatrix_priv_t * vp;
  vp = priv;
  return vp->need_restart;
  }


static void * create_colormatrix()
  {
  colormatrix_priv_t * ret;
  ret = calloc(1, sizeof(*ret));
  ret->mat = bg_colormatrix_create();
  ret->global_opt = gavl_video_options_create();

  return ret;
  }


static void destroy_colormatrix(void * priv)
  {
  colormatrix_priv_t * vp;
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
      .name = "Red",
      .long_name = TRS("Red"),
      .type = BG_PARAMETER_SECTION,
      .flags = BG_PARAMETER_SYNC,
    },
    {
      .name = "r_to_r",
      .long_name = TRS("Red -> Red"),
      .type = BG_PARAMETER_SLIDER_FLOAT,
      .flags = BG_PARAMETER_SYNC,
      .val_min =     GAVL_VALUE_INIT_FLOAT(-2.0),
      .val_max =     GAVL_VALUE_INIT_FLOAT(2.0),
      .val_default = GAVL_VALUE_INIT_FLOAT(1.0),
      .num_digits =  3,
    },
    {
      .name = "g_to_r",
      .long_name = TRS("Green -> Red"),
      .type = BG_PARAMETER_SLIDER_FLOAT,
      .flags = BG_PARAMETER_SYNC,
      .val_min =     GAVL_VALUE_INIT_FLOAT(-2.0),
      .val_max =     GAVL_VALUE_INIT_FLOAT(2.0),
      .val_default = GAVL_VALUE_INIT_FLOAT(0.0),
      .num_digits =  3,
    },
    {
      .name = "b_to_r",
      .long_name = TRS("Blue -> Red"),
      .type = BG_PARAMETER_SLIDER_FLOAT,
      .flags = BG_PARAMETER_SYNC,
      .val_min =     GAVL_VALUE_INIT_FLOAT(-2.0),
      .val_max =     GAVL_VALUE_INIT_FLOAT(2.0),
      .val_default = GAVL_VALUE_INIT_FLOAT(0.0),
      .num_digits =  3,
    },
    {
      .name = "a_to_r",
      .long_name = TRS("Alpha -> Red"),
      .type = BG_PARAMETER_SLIDER_FLOAT,
      .flags = BG_PARAMETER_SYNC,
      .val_min =     GAVL_VALUE_INIT_FLOAT(-2.0),
      .val_max =     GAVL_VALUE_INIT_FLOAT(2.0),
      .val_default = GAVL_VALUE_INIT_FLOAT(0.0),
      .num_digits =  3,
    },
    {
      .name = "off_r",
      .long_name = TRS("Red offset"),
      .type = BG_PARAMETER_SLIDER_FLOAT,
      .flags = BG_PARAMETER_SYNC,
      .val_min =     GAVL_VALUE_INIT_FLOAT(-2.0),
      .val_max =     GAVL_VALUE_INIT_FLOAT(2.0),
      .val_default = GAVL_VALUE_INIT_FLOAT(0.0),
      .num_digits =  3,
    },
    {
      .gettext_domain = PACKAGE,
      .gettext_directory = LOCALE_DIR,
      .name = "Green",
      .long_name = TRS("Green"),
      .type = BG_PARAMETER_SECTION,
      .flags = BG_PARAMETER_SYNC,
    },
    {
      .name = "r_to_g",
      .long_name = TRS("Red -> Green"),
      .type = BG_PARAMETER_SLIDER_FLOAT,
      .flags = BG_PARAMETER_SYNC,
      .val_min =     GAVL_VALUE_INIT_FLOAT(-2.0),
      .val_max =     GAVL_VALUE_INIT_FLOAT(2.0),
      .val_default = GAVL_VALUE_INIT_FLOAT(0.0),
      .num_digits =  3,
    },
    {
      .name = "g_to_g",
      .long_name = TRS("Green -> Green"),
      .type = BG_PARAMETER_SLIDER_FLOAT,
      .flags = BG_PARAMETER_SYNC,
      .val_min =     GAVL_VALUE_INIT_FLOAT(-2.0),
      .val_max =     GAVL_VALUE_INIT_FLOAT(2.0),
      .val_default = GAVL_VALUE_INIT_FLOAT(1.0),
      .num_digits =  3,
    },
    {
      .name = "b_to_g",
      .long_name = TRS("Blue -> Green"),
      .type = BG_PARAMETER_SLIDER_FLOAT,
      .flags = BG_PARAMETER_SYNC,
      .val_min =     GAVL_VALUE_INIT_FLOAT(-2.0),
      .val_max =     GAVL_VALUE_INIT_FLOAT(2.0),
      .val_default = GAVL_VALUE_INIT_FLOAT(0.0),
      .num_digits =  3,
    },
    {
      .name = "a_to_g",
      .long_name = TRS("Alpha -> Green"),
      .type = BG_PARAMETER_SLIDER_FLOAT,
      .flags = BG_PARAMETER_SYNC,
      .val_min =     GAVL_VALUE_INIT_FLOAT(-2.0),
      .val_max =     GAVL_VALUE_INIT_FLOAT(2.0),
      .val_default = GAVL_VALUE_INIT_FLOAT(0.0),
      .num_digits =  3,
    },
    {
      .name = "off_g",
      .long_name = TRS("Green offset"),
      .type = BG_PARAMETER_SLIDER_FLOAT,
      .flags = BG_PARAMETER_SYNC,
      .val_min =     GAVL_VALUE_INIT_FLOAT(-2.0),
      .val_max =     GAVL_VALUE_INIT_FLOAT(2.0),
      .val_default = GAVL_VALUE_INIT_FLOAT(0.0),
      .num_digits =  3,
    },
    {
      .gettext_domain = PACKAGE,
      .gettext_directory = LOCALE_DIR,
      .name = "Blue",
      .long_name = TRS("Blue"),
      .type = BG_PARAMETER_SECTION,
      .flags = BG_PARAMETER_SYNC,
    },
    {
      .name = "r_to_b",
      .long_name = TRS("Red -> Blue"),
      .type = BG_PARAMETER_SLIDER_FLOAT,
      .flags = BG_PARAMETER_SYNC,
      .val_min =     GAVL_VALUE_INIT_FLOAT(-2.0),
      .val_max =     GAVL_VALUE_INIT_FLOAT(2.0),
      .val_default = GAVL_VALUE_INIT_FLOAT(0.0),
      .num_digits =  3,
    },
    {
      .name = "g_to_b",
      .long_name = TRS("Green -> Blue"),
      .type = BG_PARAMETER_SLIDER_FLOAT,
      .flags = BG_PARAMETER_SYNC,
      .val_min =     GAVL_VALUE_INIT_FLOAT(-2.0),
      .val_max =     GAVL_VALUE_INIT_FLOAT(2.0),
      .val_default = GAVL_VALUE_INIT_FLOAT(0.0),
      .num_digits =  3,
    },
    {
      .name = "b_to_b",
      .long_name = TRS("Blue -> Blue"),
      .type = BG_PARAMETER_SLIDER_FLOAT,
      .flags = BG_PARAMETER_SYNC,
      .val_min =     GAVL_VALUE_INIT_FLOAT(-2.0),
      .val_max =     GAVL_VALUE_INIT_FLOAT(2.0),
      .val_default = GAVL_VALUE_INIT_FLOAT(1.0),
      .num_digits =  3,
    },
    {
      .name = "a_to_b",
      .long_name = TRS("Alpha -> Blue"),
      .type = BG_PARAMETER_SLIDER_FLOAT,
      .flags = BG_PARAMETER_SYNC,
      .val_min =     GAVL_VALUE_INIT_FLOAT(-2.0),
      .val_max =     GAVL_VALUE_INIT_FLOAT(2.0),
      .val_default = GAVL_VALUE_INIT_FLOAT(0.0),
      .num_digits =  3,
    },
    {
      .name = "off_b",
      .long_name = TRS("Blue offset"),
      .type = BG_PARAMETER_SLIDER_FLOAT,
      .flags = BG_PARAMETER_SYNC,
      .val_min =     GAVL_VALUE_INIT_FLOAT(-2.0),
      .val_max =     GAVL_VALUE_INIT_FLOAT(2.0),
      .val_default = GAVL_VALUE_INIT_FLOAT(0.0),
      .num_digits =  3,
    },
    {
      .gettext_domain = PACKAGE,
      .gettext_directory = LOCALE_DIR,
      .name = "Alpha",
      .long_name = TRS("Alpha"),
      .type = BG_PARAMETER_SECTION,
      .flags = BG_PARAMETER_SYNC,
    },
    {
      .name = "r_to_a",
      .long_name = TRS("Red -> Alpha"),
      .type = BG_PARAMETER_SLIDER_FLOAT,
      .flags = BG_PARAMETER_SYNC,
      .val_min =     GAVL_VALUE_INIT_FLOAT(-2.0),
      .val_max =     GAVL_VALUE_INIT_FLOAT(2.0),
      .val_default = GAVL_VALUE_INIT_FLOAT(0.0),
      .num_digits =  3,
    },
    {
      .name = "g_to_a",
      .long_name = TRS("Green -> Alpha"),
      .type = BG_PARAMETER_SLIDER_FLOAT,
      .flags = BG_PARAMETER_SYNC,
      .val_min =     GAVL_VALUE_INIT_FLOAT(-2.0),
      .val_max =     GAVL_VALUE_INIT_FLOAT(2.0),
      .val_default = GAVL_VALUE_INIT_FLOAT(0.0),
      .num_digits =  3,
    },
    {
      .name = "b_to_a",
      .long_name = TRS("Blue -> Alpha"),
      .type = BG_PARAMETER_SLIDER_FLOAT,
      .flags = BG_PARAMETER_SYNC,
      .val_min =     GAVL_VALUE_INIT_FLOAT(-2.0),
      .val_max =     GAVL_VALUE_INIT_FLOAT(2.0),
      .val_default = GAVL_VALUE_INIT_FLOAT(0.0),
      .num_digits =  3,
    },
    {
      .name = "a_to_a",
      .long_name = TRS("Alpha -> Alpha"),
      .type = BG_PARAMETER_SLIDER_FLOAT,
      .flags = BG_PARAMETER_SYNC,
      .val_min =     GAVL_VALUE_INIT_FLOAT(-2.0),
      .val_max =     GAVL_VALUE_INIT_FLOAT(2.0),
      .val_default = GAVL_VALUE_INIT_FLOAT(1.0),
      .num_digits =  3,
    },
    {
      .name = "off_a",
      .long_name = TRS("Alpha offset"),
      .type = BG_PARAMETER_SLIDER_FLOAT,
      .flags = BG_PARAMETER_SYNC,
      .val_min =     GAVL_VALUE_INIT_FLOAT(-2.0),
      .val_max =     GAVL_VALUE_INIT_FLOAT(2.0),
      .val_default = GAVL_VALUE_INIT_FLOAT(0.0),
      .num_digits =  3,
    },
    {
      .name = "misc",
      .long_name = TRS("Misc"),
      .type = BG_PARAMETER_SECTION,
    },
    {
      .name = "force_alpha",
      .long_name = TRS("Force alpha"),
      .type = BG_PARAMETER_CHECKBUTTON,
      .flags = BG_PARAMETER_SYNC,
      .help_string = TRS("Create video with alpha channel even if the input format has no alpha channel. Use this to generate the alpha channel from other channels using the colormatrix."),
    },
    { /* End of parameters */ },
  };

static const bg_parameter_info_t * get_parameters_colormatrix(void * priv)
  {
  return parameters;
  }

#define MATRIX_PARAM(n, r, c) \
  else if(!strcmp(name, n)) \
    { \
    if(vp->coeffs[r][c] != val->v.d) \
      { \
      vp->coeffs[r][c] = val->v.d; \
      changed = 1; \
      } \
    }

static void set_parameter_colormatrix(void * priv, const char * name,
                               const gavl_value_t * val)
  {
  int changed = 0;
  colormatrix_priv_t * vp;
  vp = priv;

  if(!name)
    return;
  MATRIX_PARAM("r_to_r", 0, 0)
  MATRIX_PARAM("g_to_r", 0, 1)
  MATRIX_PARAM("b_to_r", 0, 2)
  MATRIX_PARAM("a_to_r", 0, 3)
  MATRIX_PARAM( "off_r", 0, 4)

  MATRIX_PARAM("r_to_g", 1, 0)
  MATRIX_PARAM("g_to_g", 1, 1)
  MATRIX_PARAM("b_to_g", 1, 2)
  MATRIX_PARAM("a_to_g", 1, 3)
  MATRIX_PARAM( "off_g", 1, 4)

  MATRIX_PARAM("r_to_b", 2, 0)
  MATRIX_PARAM("g_to_b", 2, 1)
  MATRIX_PARAM("b_to_b", 2, 2)
  MATRIX_PARAM("a_to_b", 2, 3)
  MATRIX_PARAM( "off_b", 2, 4)

  MATRIX_PARAM("r_to_a", 3, 0)
  MATRIX_PARAM("g_to_a", 3, 1)
  MATRIX_PARAM("b_to_a", 3, 2)
  MATRIX_PARAM("a_to_a", 3, 3)
  MATRIX_PARAM( "off_a", 3, 4)

  else if(!strcmp(name, "force_alpha"))
    {
    if(vp->force_alpha != val->v.i)
      {
      vp->force_alpha = val->v.i;
      vp->need_restart = 1;
      }
    }
  
  if(changed)
    bg_colormatrix_set_rgba(vp->mat, vp->coeffs);
  }

#if 0
static void set_input_format_colormatrix(void * priv,
                                         gavl_video_format_t * format,
                                         int port)
  {
  colormatrix_priv_t * vp;
  int flags = 0;
  vp = priv;
  if(vp->force_alpha)
    flags |= BG_COLORMATRIX_FORCE_ALPHA;

  if(!port)
    {
    bg_colormatrix_init(vp->mat, format, flags, vp->global_opt);
    gavl_video_format_copy(&vp->format, format);
    }
  vp->need_restart = 0;
  }
#endif

static gavl_source_status_t read_func(void * priv,
                                      gavl_video_frame_t ** f)
  {
  gavl_source_status_t st;
  colormatrix_priv_t * vp = priv;

  if((st = gavl_video_source_read_frame(vp->in_src, f)) !=
     GAVL_SOURCE_OK)
    return st;

  bg_colormatrix_process(vp->mat, *f);
  return GAVL_SOURCE_OK;
  }

static gavl_video_source_t *
connect_colormatrix(void * priv, gavl_video_source_t * src,
                    const gavl_video_options_t * opt)
  {
  int flags = 0;
  colormatrix_priv_t * vp = priv;
  if(vp->out_src)
    gavl_video_source_destroy(vp->out_src);

  if(vp->force_alpha)
    flags |= BG_COLORMATRIX_FORCE_ALPHA;
  
  vp->in_src = src;
  gavl_video_format_copy(&vp->format,
                         gavl_video_source_get_src_format(vp->in_src));

  bg_colormatrix_init(vp->mat, &vp->format, flags, vp->global_opt);
  
  if(opt)
    gavl_video_options_copy(vp->global_opt, opt);
  
  gavl_video_source_set_dst(vp->in_src, 0, &vp->format);
  
  vp->need_restart = 0;

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
      .name =      "fv_colormatrix_rgb",
      .long_name = TRS("RGB Colormatrix"),
      .description = TRS("Generic colormatrix (RGBA). You pass the coefficients in RGB(A) coordinates, but the processing will work in Y'CbCr(A) as well."),
      .type =     BG_PLUGIN_FILTER_VIDEO,
      .flags =    BG_PLUGIN_FILTER_1,
      .create =   create_colormatrix,
      .destroy =   destroy_colormatrix,
      .get_parameters =   get_parameters_colormatrix,
      .set_parameter =    set_parameter_colormatrix,
      .priority =         1,
    },
    .connect = connect_colormatrix,
    .need_restart = need_restart_colormatrix,
    
  };

/* Include this into all plugin modules exactly once
   to let the plugin loader obtain the API version */
BG_GET_PLUGIN_API_VERSION;
