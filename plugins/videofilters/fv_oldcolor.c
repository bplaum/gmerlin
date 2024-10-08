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

#define LOG_DOMAIN "fv_oldcolor"

typedef struct
  {
  bg_colormatrix_t * mat;
  
  gavl_video_format_t format;
  gavl_video_options_t * global_opt;

  float coeffs[3][4];
  int style;
  float strength;
  float gain[3];

  gavl_video_source_t * in_src;
  gavl_video_source_t * out_src;
  } oldcolor_priv_t;

#define STYLE_BW    0
#define STYLE_TECH1 1
#define STYLE_TECH2 2

static const float coeffs_bw[3][4] =
  {
    { 0.299000,  0.587000,  0.114000, 0.0 },
    { 0.299000,  0.587000,  0.114000, 0.0 },
    { 0.299000,  0.587000,  0.114000, 0.0 },
  };

static const float coeffs_tech1[3][4] =
  {
    { 1.0, 0.0, 0.0, 0.0 },
    { 0.0, 0.5, 0.5, 0.0 },
    { 0.0, 0.5, 0.5, 0.0 },
  };

static const float coeffs_tech2[3][4] =
  {
    {  1.75  -0.50, -0.25, 0.0 },
    { -0.25,  1.50, -0.25, 0.0 },
    { -0.25, -0.50,  1.75, 0.0 },
  };

static const float coeffs_unity[3][4] =
  {
    { 1.0, 0.0, 0.0, 0.0 },
    { 0.0, 1.0, 0.0, 0.0 },
    { 0.0, 0.0, 1.0, 0.0 },
  };

static void interpolate(const float coeffs[3][4], float result[3][4], float strength, float * gain)
  {
  int i, j;
  for(i = 0; i < 3; i++)
    {
    for(j = 0; j < 4; j++)
      {
      result[i][j] = gain[i] * (strength * coeffs[i][j] + (1.0 - strength) * coeffs_unity[i][j]);
      }
    }
  }

static void * create_oldcolor()
  {
  oldcolor_priv_t * ret;
  ret = calloc(1, sizeof(*ret));
  ret->mat = bg_colormatrix_create();
  ret->global_opt = gavl_video_options_create();

  return ret;
  }

static void destroy_oldcolor(void * priv)
  {
  oldcolor_priv_t * vp;
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
      .name =        "style",
      .long_name =   TRS("Style"),
      .type =        BG_PARAMETER_STRINGLIST,
      .flags = BG_PARAMETER_SYNC,
      .val_default = GAVL_VALUE_INIT_STRING("tech1"),
      .multi_names =
      (char const *[])
      {
        "bw",
        "tech1",
        "tech2",
        NULL
      },
      .multi_labels =
      (char const *[])
      {
        "B/W",
        "Technicolor 2-Stripe",
        "Technicolor 3-Stripe",
        NULL
      },
    },
    {
      .name = "strength",
      .long_name = TRS("Strength"),
      .type = BG_PARAMETER_SLIDER_FLOAT,
      .flags = BG_PARAMETER_SYNC,
      .val_min =     GAVL_VALUE_INIT_FLOAT(0.0),
      .val_max =     GAVL_VALUE_INIT_FLOAT(1.0),
      .val_default = GAVL_VALUE_INIT_FLOAT(1.0),
      .num_digits = 3,
    },
    {
      .name = "r_gain",
      .long_name = TRS("Red gain"),
      .type = BG_PARAMETER_SLIDER_FLOAT,
      .flags = BG_PARAMETER_SYNC,
      .val_min =     GAVL_VALUE_INIT_FLOAT(0.0),
      .val_max =     GAVL_VALUE_INIT_FLOAT(2.0),
      .val_default = GAVL_VALUE_INIT_FLOAT(1.0),
      .num_digits = 3,
    },
    {
      .name = "g_gain",
      .long_name = TRS("Green gain"),
      .type = BG_PARAMETER_SLIDER_FLOAT,
      .flags = BG_PARAMETER_SYNC,
      .val_min =     GAVL_VALUE_INIT_FLOAT(0.0),
      .val_max =     GAVL_VALUE_INIT_FLOAT(2.0),
      .val_default = GAVL_VALUE_INIT_FLOAT(1.0),
      .num_digits = 3,
    },
    {
      .name = "b_gain",
      .long_name = TRS("Blue gain"),
      .type = BG_PARAMETER_SLIDER_FLOAT,
      .flags = BG_PARAMETER_SYNC,
      .val_min =     GAVL_VALUE_INIT_FLOAT(0.0),
      .val_max =     GAVL_VALUE_INIT_FLOAT(2.0),
      .val_default = GAVL_VALUE_INIT_FLOAT(1.0),
      .num_digits = 3,
    },
    { /* End of parameters */ },
  };


static const bg_parameter_info_t * get_parameters_oldcolor(void * priv)
  {
  return parameters;
  }

static void set_coeffs(oldcolor_priv_t * vp)
  {
  switch(vp->style)
    {
    case STYLE_BW:
      interpolate(coeffs_bw, vp->coeffs, vp->strength, vp->gain);
      break;
    case STYLE_TECH1:
      interpolate(coeffs_tech1, vp->coeffs, vp->strength, vp->gain);
      break;
    case STYLE_TECH2:
      interpolate(coeffs_tech2, vp->coeffs, vp->strength, vp->gain);
      break;
    }
  }

static void set_parameter_oldcolor(void * priv, const char * name,
                               const gavl_value_t * val)
  {
  oldcolor_priv_t * vp;
  int changed = 0;
  vp = priv;
  if(!name)
    return;
  
  if(!strcmp(name, "style"))
    {
    if(!strcmp(val->v.str, "bw"))
      {
      if(vp->style != STYLE_BW)
        {
        vp->style = STYLE_BW;
        changed = 1;
        }
      }
    if(!strcmp(val->v.str, "tech1"))
      {
      if(vp->style != STYLE_TECH1)
        {
        vp->style = STYLE_TECH1;
        changed = 1;
        }
      }
    if(!strcmp(val->v.str, "tech2"))
      {
      if(vp->style != STYLE_TECH2)
        {
        vp->style = STYLE_TECH2;
        changed = 1;
        }
      }
    }
  else if(!strcmp(name, "strength"))
    {
    if(vp->strength != val->v.d)
      {
      vp->strength = val->v.d;
      changed = 1;
      }
    }
  else if(!strcmp(name, "r_gain"))
    {
    if(vp->gain[0] != val->v.d)
      {
      vp->gain[0] = val->v.d;
      changed = 1;
      }
    }
  else if(!strcmp(name, "g_gain"))
    {
    if(vp->gain[1] != val->v.d)
      {
      vp->gain[1] = val->v.d;
      changed = 1;
      }
    }
  else if(!strcmp(name, "b_gain"))
    {
    if(vp->gain[2] != val->v.d)
      {
      vp->gain[2] = val->v.d;
      changed = 1;
      }
    }
  if(changed)
    {
    set_coeffs(vp);
    bg_colormatrix_set_rgb(vp->mat, vp->coeffs);
    }
  }


static gavl_source_status_t read_func(void * priv, gavl_video_frame_t ** frame)
  {
  oldcolor_priv_t * vp;
  gavl_source_status_t st;
  vp = priv;

  if((st = gavl_video_source_read_frame(vp->in_src, frame)) != GAVL_SOURCE_OK)
    return st;
  
  bg_colormatrix_process(vp->mat, *frame);
  return GAVL_SOURCE_OK;
  }

static gavl_video_source_t * connect_oldcolor(void * priv, gavl_video_source_t * src,
                                              const gavl_video_options_t * opt)
  {
  oldcolor_priv_t * vp;
  vp = priv;
  if(vp->out_src)
    gavl_video_source_destroy(vp->out_src);

  vp->in_src = src;
  
  gavl_video_format_copy(&vp->format, gavl_video_source_get_src_format(vp->in_src));
  if(opt)
    gavl_video_options_copy(vp->global_opt, opt);

  bg_colormatrix_init(vp->mat, &vp->format, 0, vp->global_opt);
  
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
      .name =      "fv_oldcolor",
      .long_name = TRS("Old color"),
      .description = TRS("Simulate old color- and B/W movies"),
      .type =     BG_PLUGIN_FILTER_VIDEO,
      .flags =    BG_PLUGIN_FILTER_1,
      .create =   create_oldcolor,
      .destroy =   destroy_oldcolor,
      .get_parameters =   get_parameters_oldcolor,
      .set_parameter =    set_parameter_oldcolor,
      .priority =         1,
    },
    
    .connect = connect_oldcolor,
  };

/* Include this into all plugin modules exactly once
   to let the plugin loader obtain the API version */
BG_GET_PLUGIN_API_VERSION;
