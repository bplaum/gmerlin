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
#include <gmerlin/bggavl.h>

#include "deinterlace.h"
#include "bgyadif.h"

#define LOG_DOMAIN "fv_deinterlace"

#define DEINTERLACE_NONE       0
#define DEINTERLACE_GAVL       1
#define DEINTERLACE_SCALE_HW   2
#define DEINTERLACE_YADIF      3
#define DEINTERLACE_YADIF_FAST 4

#define DEINTERLACE_OUTPUT_TOP    0
#define DEINTERLACE_OUTPUT_BOTTOM 1
#define DEINTERLACE_OUTPUT_FIRST  2
#define DEINTERLACE_OUTPUT_SECOND 3
#define DEINTERLACE_OUTPUT_BOTH   4

typedef struct deinterlace_priv_s
  {
  gavl_video_format_t in_format;
  gavl_video_format_t out_format;
  
  gavl_video_options_t * opt;
  gavl_video_options_t * global_opt;

  gavl_video_deinterlacer_t * deint;
  
  gavl_video_frame_t * src_field_1;
  
  int method;
  int sub_method;
  int src_field;
  int force;
  
  int need_restart;
  int need_reinit;
  
  int output_mode;

  bg_yadif_t * yadif;
  
  gavl_source_status_t (*deint_func)(struct deinterlace_priv_s * p,
                                     gavl_video_frame_t ** frame);

  gavl_video_source_t * in_src;
  gavl_video_source_t * out_src;
  } deinterlace_priv_t;

static void * create_deinterlace()
  {
  deinterlace_priv_t * ret;
  ret = calloc(1, sizeof(*ret));
  ret->deint = gavl_video_deinterlacer_create();
  ret->opt = gavl_video_deinterlacer_get_options(ret->deint);
  ret->yadif = bg_yadif_create();
  
  ret->src_field_1 = gavl_video_frame_create(NULL);
  ret->global_opt = gavl_video_options_create();
  return ret;
  }

static void destroy_deinterlace(void * priv)
  {
  deinterlace_priv_t * vp;
  vp = priv;
  if(vp->out_src)
    gavl_video_source_destroy(vp->out_src);
  
  gavl_video_deinterlacer_destroy(vp->deint);

  gavl_video_options_destroy(vp->global_opt);
  gavl_video_frame_null(vp->src_field_1);
  gavl_video_frame_destroy(vp->src_field_1);
  bg_yadif_destroy(vp->yadif);
  free(vp);
  }

static void reset_deinterlace(void * priv)
  {
  deinterlace_priv_t * vp;
  vp = priv;
  bg_yadif_reset(vp->yadif);
  }

static const bg_parameter_info_t parameters[] =
  {
    {
      .gettext_domain = PACKAGE,
      .gettext_directory = LOCALE_DIR,
      .name = "method",
      .long_name = TRS("Method"),
      .type = BG_PARAMETER_STRINGLIST,
      .flags = BG_PARAMETER_SYNC,
      .val_default = GAVL_VALUE_INIT_STRING("none"),
      .multi_names =  (char const *[]){ "none",
                               "copy",
                               "scale_hw",
                               "scale_sw",
                               "blend",
                               "yadif",
                               "yadif_fast",
                               NULL },
      
      .multi_labels = (char const *[]){ TRS("None"),
                               TRS("Scanline doubler"),
                               TRS("Scaler (hardware)"),
                               TRS("Scaler (software)"),
                               TRS("Blend"),
                               TRS("Yadif"),
                               TRS("Yadif (fast)"),
                               NULL },
      .multi_descriptions = (char const *[]){ TRS("Do nothing"),
                                     TRS("Simply double all scanlines. Very fast but \
low image quality"), 
                                     TRS("Drop one field and change the pixel aspect ratio such that a subsequent hardware scaler will scale the image to the original height"),
                                     TRS("Drop one field and scale the image to the original height"),
                                     TRS("yadif"),
                                     TRS("yadif (fast mode)"),
                                     NULL },
    },
    {
      .name = "force",
      .long_name = TRS("Force deinterlacing"),
      .type = BG_PARAMETER_CHECKBUTTON,
      .flags = BG_PARAMETER_SYNC,
      .help_string = "Always perform deinterlacing even if the source format pretends to be progressive",
    },
    {
      .name = "output_mode",
      .long_name = TRS("Output mode"),
      .type = BG_PARAMETER_STRINGLIST,
      .flags = BG_PARAMETER_SYNC,
      .val_default = GAVL_VALUE_INIT_STRING("top"),
      .help_string = TRS("Specify which field to output. Outputting both fields is not always supported."),
      .multi_names = (char const *[]){ "top", "bottom", "first", "second", "both", NULL },
      .multi_labels = (char const *[]){ TRS("Top field"),
                                        TRS("Bottom field"),
                                        TRS("First field"),
                                        TRS("Second field"),
                                        TRS("Both fields"),
                                        NULL },
    },
    BG_GAVL_PARAM_SCALE_MODE,
    { /* End of parameters */ },
  };

static const bg_parameter_info_t * get_parameters_deinterlace(void * priv)
  {
  return parameters;
  }

static void set_parameter_deinterlace(void * priv, const char * name,
                                      const gavl_value_t * val)
  {
  int new_method = DEINTERLACE_NONE;
  gavl_deinterlace_mode_t new_sub_method = 0;
  gavl_scale_mode_t new_scale_mode;
  int new_output_mode;
  
  deinterlace_priv_t * vp;
  vp = priv;

  new_output_mode = vp->output_mode;
  
  if(!name)
    return;

  if(!strcmp(name, "method"))
    {
    if(!strcmp(val->v.str, "none"))
      {
      new_method = DEINTERLACE_NONE;
      }
    else if(!strcmp(val->v.str, "copy"))
      {
      new_method = DEINTERLACE_GAVL;
      new_sub_method = GAVL_DEINTERLACE_COPY;
      }
    else if(!strcmp(val->v.str, "scale_hw"))
      {
      new_method = DEINTERLACE_SCALE_HW;
      }
    else if(!strcmp(val->v.str, "scale_sw"))
      {
      new_method = DEINTERLACE_GAVL;
      new_sub_method = GAVL_DEINTERLACE_SCALE;
      }
    else if(!strcmp(val->v.str, "blend"))
      {
      new_method = DEINTERLACE_GAVL;
      new_sub_method = GAVL_DEINTERLACE_BLEND;
      }
    else if(!strcmp(val->v.str, "yadif"))
      {
      new_method = DEINTERLACE_YADIF;
      }
    else if(!strcmp(val->v.str, "yadif_fast"))
      {
      new_method = DEINTERLACE_YADIF_FAST;
      }
    if((new_method != vp->method) || 
       (new_sub_method != vp->sub_method))
      {
      vp->need_restart = 1;
      vp->method = new_method;
      vp->sub_method = new_sub_method;

      if(new_method == DEINTERLACE_GAVL)
        {
        gavl_video_options_set_deinterlace_mode(vp->opt,
                                                new_sub_method);
        }
      }
    }
  else if(!strcmp(name, "force"))
    {
    if(vp->force != val->v.i)
      {
      vp->force = val->v.i;
      vp->need_restart = 1;
      }
    }
  else if(!strcmp(name, "output_mode"))
    {
    if(!strcmp(val->v.str, "top"))
      new_output_mode = DEINTERLACE_OUTPUT_TOP;
    else if(!strcmp(val->v.str, "bottom"))
      new_output_mode = DEINTERLACE_OUTPUT_BOTTOM;
    else if(!strcmp(val->v.str, "first"))
      new_output_mode = DEINTERLACE_OUTPUT_FIRST;
    else if(!strcmp(val->v.str, "second"))
      new_output_mode = DEINTERLACE_OUTPUT_SECOND;
    else if(!strcmp(val->v.str, "both"))
      new_output_mode = DEINTERLACE_OUTPUT_BOTH;
    
    if(new_output_mode != vp->output_mode)
      {
      vp->need_restart = 1;
      vp->output_mode = new_output_mode;
      }
    }
  else if(!strcmp(name, "scale_mode"))
    {
    new_scale_mode = bg_gavl_string_to_scale_mode(val->v.str);
    if(new_scale_mode != gavl_video_options_get_scale_mode(vp->opt))
      {
      gavl_video_options_set_scale_mode(vp->opt, new_scale_mode);
      vp->need_reinit = 1;
      }
    }
  else if(!strcmp(name, "scale_order"))
    {
    if(gavl_video_options_get_scale_order(vp->opt) != val->v.i)
      {
      gavl_video_options_set_scale_order(vp->opt, val->v.i);
      vp->need_reinit = 1;
      }
    }
  }

/*
 * Deinterlace functions
 */

static gavl_source_status_t deinterlace_none(struct deinterlace_priv_s * vp,
                                             gavl_video_frame_t ** frame)
  {
  return gavl_video_source_read_frame(vp->in_src, frame);
  }

static gavl_source_status_t
deinterlace_gavl(struct deinterlace_priv_s * vp,
                 gavl_video_frame_t ** frame)
  {
  gavl_source_status_t st;
  gavl_video_frame_t * in_frame = NULL;
  
  if((st = gavl_video_source_read_frame(vp->in_src, &in_frame)) !=
     GAVL_SOURCE_OK)
    return st;
  
  gavl_video_deinterlacer_deinterlace(vp->deint, in_frame, *frame);
  gavl_video_frame_copy_metadata(*frame, in_frame);
  return GAVL_SOURCE_OK;
  }

static gavl_source_status_t
deinterlace_scale_hw(struct deinterlace_priv_s * vp,
                     gavl_video_frame_t ** frame)
  {
  gavl_source_status_t st;
  gavl_video_frame_t * in_frame = NULL;

  if((st = gavl_video_source_read_frame(vp->in_src, &in_frame)) !=
     GAVL_SOURCE_OK)
    return st;
  
  gavl_video_frame_get_field(vp->in_format.pixelformat,
                             in_frame, vp->src_field_1, vp->src_field);
  
  gavl_video_frame_copy(&vp->out_format, *frame, vp->src_field_1);
  
  gavl_video_frame_copy_metadata(*frame, in_frame);
  return GAVL_SOURCE_OK;
  }

static gavl_source_status_t deinterlace_yadif(struct deinterlace_priv_s * vp,
                                              gavl_video_frame_t ** frame)
  {
  return bg_yadif_read(vp->yadif, frame, vp->in_src);
  }

static void transfer_global_options(gavl_video_options_t * opt,
                                    const gavl_video_options_t * global_opt)
  {
  gavl_video_options_set_quality(opt, gavl_video_options_get_quality(global_opt));
  gavl_video_options_set_thread_pool(opt, gavl_video_options_get_thread_pool(global_opt));
  }

static int need_restart_deinterlace(void * priv)
  {
  deinterlace_priv_t * vp;
  vp = priv;
  return vp->need_restart;
  }

static void
set_format(void * priv,
           const gavl_video_format_t * format)
  {
  deinterlace_priv_t * vp;
  int yadif_mode;
  vp = priv;

  transfer_global_options(vp->opt, vp->global_opt);
    
  vp->need_reinit = 1;

  gavl_video_format_copy(&vp->in_format, format);
  
  switch(vp->method)
    {
    case DEINTERLACE_NONE:
      vp->deint_func = deinterlace_none;
      gavl_video_format_copy(&vp->out_format, format);
      vp->out_format.interlace_mode = GAVL_INTERLACE_NONE;
      break;
    case DEINTERLACE_GAVL:
      vp->deint_func = deinterlace_gavl;
      gavl_video_format_copy(&vp->out_format, format);
      vp->out_format.interlace_mode = GAVL_INTERLACE_NONE;
      break;
    case DEINTERLACE_SCALE_HW:
      vp->deint_func = deinterlace_scale_hw;
        
      vp->out_format.image_height /= 2;
      vp->out_format.frame_height /= 2;
      vp->out_format.pixel_height *= 2;

      gavl_video_format_copy(&vp->out_format, format);
      vp->out_format.interlace_mode = GAVL_INTERLACE_NONE;
        
      break;
    case DEINTERLACE_YADIF:
    case DEINTERLACE_YADIF_FAST:
      vp->deint_func = deinterlace_yadif;

      yadif_mode = (vp->output_mode == DEINTERLACE_OUTPUT_BOTH) ?
        1 : 0;

      if(vp->method == DEINTERLACE_YADIF_FAST)
        yadif_mode += 2;
        
      bg_yadif_init(vp->yadif, &vp->in_format, &vp->out_format, vp->opt, yadif_mode);
      break;
    }
    
  vp->need_restart = 0;
  
  }

static gavl_source_status_t read_func(void * priv,
                                      gavl_video_frame_t ** frame)
  {
  deinterlace_priv_t * vp = priv;

  if(vp->need_reinit)
    {
    switch(vp->method)
      {
      case DEINTERLACE_GAVL:
        transfer_global_options(vp->opt, vp->global_opt);
        gavl_video_deinterlacer_init(vp->deint, &vp->in_format);
        break;
      default:
        break;
      }
    vp->need_reinit = 0;
    }
  return vp->deint_func(vp, frame);
  }

static gavl_video_source_t *
connect_deinterlace(void * priv,
                    gavl_video_source_t * src,
                    const gavl_video_options_t * opt)
  {
  deinterlace_priv_t * vp = priv;
  
  vp->in_src = src;
  if(vp->out_src)
    gavl_video_source_destroy(vp->out_src);

  set_format(vp, gavl_video_source_get_src_format(vp->in_src));
  if(opt)
    gavl_video_options_copy(vp->global_opt, opt);
  
  gavl_video_source_set_dst(vp->in_src, 0, &vp->in_format);

  vp->out_src =
    gavl_video_source_create_source(read_func, vp, 0, vp->in_src);
  return vp->out_src;
  }

const bg_fv_plugin_t the_plugin = 
  {
    .common =
    {
      BG_LOCALE,
      .name =      "fv_deinterlace",
      .long_name = TRS("Deinterlacer"),
      .description = TRS("Deinterlace with various algorithms"),
      .type =     BG_PLUGIN_FILTER_VIDEO,
      .flags =    BG_PLUGIN_FILTER_1,
      .create =   create_deinterlace,
      .destroy =   destroy_deinterlace,
      .get_parameters =   get_parameters_deinterlace,
      .set_parameter =    set_parameter_deinterlace,
      .priority =         1,
    },
    .connect = connect_deinterlace,
    .need_restart = need_restart_deinterlace,
    .reset = reset_deinterlace,
  };

/* Include this into all plugin modules exactly once
   to let the plugin loader obtain the API version */
BG_GET_PLUGIN_API_VERSION;
