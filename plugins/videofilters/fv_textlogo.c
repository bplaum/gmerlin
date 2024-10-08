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

#include <gmerlin/textrenderer.h>

#define LOG_DOMAIN "fv_textlogo"


typedef struct
  {
  gavl_video_format_t format;
  gavl_video_format_t ovl_format;
  
  gavl_overlay_t * ovl;
  
  bg_text_renderer_t * renderer;
  gavl_overlay_blend_context_t * blender;

  char * textlogo;

  int need_overlay;

  gavl_video_source_t * in_src;
  gavl_video_source_t * out_src;
  } tc_priv_t;

static void * create_textlogo()
  {
  tc_priv_t * ret;
  ret = calloc(1, sizeof(*ret));
  ret->renderer = bg_text_renderer_create();
  ret->blender = gavl_overlay_blend_context_create();
  return ret;
  }

static void destroy_textlogo(void * priv)
  {
  tc_priv_t * vp;
  vp = priv;
  bg_text_renderer_destroy(vp->renderer);
  gavl_overlay_blend_context_destroy(vp->blender);
  
  if(vp->textlogo)
    free(vp->textlogo);
  free(vp);
  }

static const bg_parameter_info_t parameters[] =
  {
    {
      .gettext_domain = PACKAGE,
      .gettext_directory = LOCALE_DIR,
      .name =       "general",
      .long_name =  TRS("General"),
      .type =       BG_PARAMETER_SECTION,
    },
    {
      .name = "text",
      .long_name = TRS("Text"),
      .type = BG_PARAMETER_STRING,
      .val_default = GAVL_VALUE_INIT_STRING("Enter text"),
      .help_string = TRS("Text to display"),
    },
    {
      .name =       "render_options",
      .long_name =  TRS("Render options"),
      .type =       BG_PARAMETER_SECTION,
    },
    {
      .name =       "color",
      .long_name =  TRS("Text color"),
      .type =       BG_PARAMETER_COLOR_RGBA,
      .val_default = GAVL_VALUE_INIT_COLOR_RGBA(1.0, 1.0, 1.0, 1.0),
    },
    {
      .name =       "border_color",
      .long_name =  TRS("Border color"),
      .type =       BG_PARAMETER_COLOR_RGB,
      .val_default = GAVL_VALUE_INIT_COLOR_RGB(0.0, 0.0, 0.0),
    },
    {
      .name =       "border_width",
      .long_name =  TRS("Border width"),
      .type =       BG_PARAMETER_FLOAT,
      .val_min =     GAVL_VALUE_INIT_FLOAT(0.0),
      .val_max =     GAVL_VALUE_INIT_FLOAT(10.0),
      .val_default = GAVL_VALUE_INIT_FLOAT(2.0),
      .num_digits =  2,
    },
    {
      .name =       "font",
      .long_name =  TRS("Font"),
      .type =       BG_PARAMETER_FONT,
      .val_default = GAVL_VALUE_INIT_STRING("Courier-20")
    },
    {
      .name =       "justify_h",
      .long_name =  TRS("Horizontal justify"),
      .type =       BG_PARAMETER_STRINGLIST,
      .val_default = GAVL_VALUE_INIT_STRING("right"),
      .multi_names =  (char const *[]){ "center", "left", "right", NULL },
      .multi_labels = (char const *[]){ TRS("Center"), TRS("Left"), TRS("Right"), NULL  },
            
    },
    {
      .name =       "justify_v",
      .long_name =  TRS("Vertical justify"),
      .type =       BG_PARAMETER_STRINGLIST,
      .val_default = GAVL_VALUE_INIT_STRING("top"),
      .multi_names =  (char const *[]){ "center", "top", "bottom", NULL  },
      .multi_labels = (char const *[]){ TRS("Center"), TRS("Top"), TRS("Bottom"), NULL },
    },
    {
      .name =        "cache_size",
      .long_name =   TRS("Cache size"),
      .type =        BG_PARAMETER_INT,
      .flags = BG_PARAMETER_HIDE_DIALOG,
      .val_min =     GAVL_VALUE_INIT_INT(1),
      .val_max =     GAVL_VALUE_INIT_INT(65535),
      .val_default = GAVL_VALUE_INIT_INT(255),
      
      .help_string = TRS("Specify, how many different characters are cached for faster rendering. For European languages, this never needs to be larger than 255."),
    },
    {
      .name =        "border_left",
      .long_name =   TRS("Left border"),
      .type =        BG_PARAMETER_INT,
      .val_min =     GAVL_VALUE_INIT_INT(0),
      .val_max =     GAVL_VALUE_INIT_INT(65535),
      .val_default = GAVL_VALUE_INIT_INT(10),
      .help_string = TRS("Distance from the left text border to the image border"),
    },
    {
      .name =        "border_right",
      .long_name =   TRS("Right border"),
      .type =        BG_PARAMETER_INT,
      .val_min =     GAVL_VALUE_INIT_INT(0),
      .val_max =     GAVL_VALUE_INIT_INT(65535),
      .val_default = GAVL_VALUE_INIT_INT(10),
      .help_string = TRS("Distance from the right text border to the image border"),
    },
    {
      .name =        "border_top",
      .long_name =   TRS("Top border"),
      .type =        BG_PARAMETER_INT,
      .val_min =     GAVL_VALUE_INIT_INT(0),
      .val_max =     GAVL_VALUE_INIT_INT(65535),
      .val_default = GAVL_VALUE_INIT_INT(10),
      .help_string = TRS("Distance from the top text border to the image border"),
    },
    {
      .name =        "border_bottom",
      .long_name =   "Bottom border",
      .type =        BG_PARAMETER_INT,
      .val_min =     GAVL_VALUE_INIT_INT(0),
      .val_max =     GAVL_VALUE_INIT_INT(65535),
      .val_default = GAVL_VALUE_INIT_INT(10),
      .help_string = TRS("Distance from the bottom text border to the image border"),
    },
    {
      .name =        "ignore_linebreaks",
      .long_name =   TRS("Ignore linebreaks"),
      .type =        BG_PARAMETER_CHECKBUTTON,
      .flags = BG_PARAMETER_HIDE_DIALOG,
      .help_string = TRS("Ignore linebreaks")
    },
    { /* End of parameters */ },
  };

static const bg_parameter_info_t * get_parameters_textlogo(void * priv)
  {
  return parameters;
  }

static void
set_parameter_textlogo(void * priv, const char * name,
                      const gavl_value_t * val)
  {
  tc_priv_t * vp;
  vp = priv;

  if(!name)
    {
    bg_text_renderer_set_parameter(vp->renderer,
                                   NULL, NULL);
    vp->need_overlay = 1;
    }
  else if(!strcmp(name, "text"))
    vp->textlogo = gavl_strrep(vp->textlogo, val->v.str);
  else
    bg_text_renderer_set_parameter(vp->renderer,
                                   name, val);
  }

static void set_format(void * priv,
                       const gavl_video_format_t * format)
  {
  tc_priv_t * vp;
  vp = priv;
  
  gavl_video_format_copy(&vp->format, format);

  bg_text_renderer_init(vp->renderer,
                        &vp->format,
                        &vp->ovl_format);
  
  gavl_overlay_blend_context_init(vp->blender,
                                  &vp->format,
                                  &vp->ovl_format);

  if(vp->ovl)
    gavl_video_frame_destroy(vp->ovl);
  vp->ovl = gavl_video_frame_create(&vp->ovl_format);

  vp->need_overlay = 1;

  }

static gavl_source_status_t
read_func(void * priv, gavl_video_frame_t ** frame)
  {
  gavl_source_status_t st;
  
  tc_priv_t * vp = priv;

  if((st = gavl_video_source_read_frame(vp->in_src, frame)) !=
     GAVL_SOURCE_OK)
    return st;
  
  if(vp->need_overlay)
    {
    gavl_video_frame_clear(vp->ovl, &vp->ovl_format);

    if(vp->textlogo)
      vp->ovl = bg_text_renderer_render(vp->renderer, vp->textlogo);
    
    gavl_overlay_blend_context_set_overlay(vp->blender, vp->ovl);
    
    vp->need_overlay = 0;
    }
  
  gavl_overlay_blend(vp->blender, *frame);

  return GAVL_SOURCE_OK;
  }

static gavl_video_source_t *
connect_textlogo(void * priv, gavl_video_source_t * src,
                 const gavl_video_options_t * opt)
  {
  tc_priv_t * vp = priv;
  vp->in_src = src;
  if(vp->out_src)
    gavl_video_source_destroy(vp->out_src);
  set_format(vp, gavl_video_source_get_src_format(vp->in_src));
  
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
      .name =      "fv_textlogo",
      .long_name = TRS("Text logo"),
      .description = TRS("Burn a static text onto video frames"),
      .type =     BG_PLUGIN_FILTER_VIDEO,
      .flags =    BG_PLUGIN_FILTER_1,
      .create =   create_textlogo,
      .destroy =   destroy_textlogo,
      .get_parameters =   get_parameters_textlogo,
      .set_parameter =    set_parameter_textlogo,
      .priority =         1,
    },
    
    .connect = connect_textlogo,
    
  };

/* Include this into all plugin modules exactly once
   to let the plugin loader obtain the API version */
BG_GET_PLUGIN_API_VERSION;
