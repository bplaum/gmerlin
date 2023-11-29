/*****************************************************************
 * gmerlin - a general purpose multimedia framework and applications
 *
 * Copyright (c) 2001 - 2012 Members of the Gmerlin project
 * gmerlin-general@lists.sourceforge.net
 * http://gmerlin.sourceforge.net
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


#include <config.h>

#include <string.h>
#include <stdio.h>

#include <gavl/gavl.h>

#include <gmerlin/parameter.h>
#include <gmerlin/bggavl.h>
#include <gmerlin/xmlutils.h>
#include <gmerlin/utils.h>
#include <gmerlin/http.h>
#include <gavl/metatags.h>

#include <gmerlin/log.h>
#define LOG_DOMAIN "bggavl"

static const char * el_key   = "el";
static const char * name_key = "name";
static const char * type_key = "t";


#define SP_INT(s) if(!strcmp(name, # s)) \
    { \
    opt->s = val->v.i;                     \
    return 1; \
    }

#define SP_INT_OPT(s, o) if(!strcmp(name, o))   \
    { \
    opt->s = val->v.i;                     \
    return 1; \
    }


#define SP_FLOAT(s) if(!strcmp(name, # s))      \
    { \
    opt->s = val->v.d;                     \
    return 1; \
    }

/* Audio stuff */

int bg_gavl_audio_set_parameter(void * data, const char * name, const gavl_value_t * val)
  {
  bg_gavl_audio_options_t * opt = (bg_gavl_audio_options_t *)data;
  if(!name)
    return 1;
  if(!strcmp(name, "q"))
    {
    if(val->v.i != gavl_audio_options_get_quality(opt->opt))
      opt->options_changed = 1;
    gavl_audio_options_set_quality(opt->opt, val->v.i);
    return 1;
    }
  SP_INT(fixed_samplerate);

  if(!strcmp(name, "sampleformat"))
    {
    gavl_sample_format_t force_format = GAVL_SAMPLE_NONE;
    if(!strcmp(val->v.str, "8"))
      force_format = GAVL_SAMPLE_S8;
    else if(!strcmp(val->v.str, "16"))
      force_format = GAVL_SAMPLE_S16;
    else if(!strcmp(val->v.str, "32"))
      force_format = GAVL_SAMPLE_S32;
    else if(!strcmp(val->v.str, "f"))
      force_format = GAVL_SAMPLE_FLOAT;
    else if(!strcmp(val->v.str, "d"))
      force_format = GAVL_SAMPLE_DOUBLE;
    if(force_format != opt->force_format)
      {
      opt->force_format = force_format;
      opt->options_changed = 1;
      }
    return 1;
    }
  
  SP_INT(samplerate);
  SP_INT(fixed_channel_setup);
  SP_INT(num_front_channels);
  SP_INT(num_rear_channels);
  SP_INT(num_lfe_channels);
  
  if(!strcmp(name, "front_to_rear"))
    {
    int old_flags, new_flags;
    if(!val->v.str)
      return 1;
    old_flags = gavl_audio_options_get_conversion_flags(opt->opt);
    new_flags = old_flags & ~GAVL_AUDIO_FRONT_TO_REAR_MASK;
    
    if(!strcmp(val->v.str, "copy"))
      new_flags |= GAVL_AUDIO_FRONT_TO_REAR_COPY;
    else if(!strcmp(val->v.str, "mute"))
      new_flags |= GAVL_AUDIO_FRONT_TO_REAR_MUTE;
    else if(!strcmp(val->v.str, "diff"))
      new_flags |= GAVL_AUDIO_FRONT_TO_REAR_DIFF;
    
    if(old_flags != new_flags)
      opt->options_changed = 1;
    
    gavl_audio_options_set_conversion_flags(opt->opt, new_flags);
    
    return 1;
    }

  else if(!strcmp(name, "stereo_to_mono"))
    {
    int old_flags, new_flags = GAVL_AUDIO_STEREO_TO_MONO_MIX;
    if(!val->v.str)
      return 1;
    old_flags = gavl_audio_options_get_conversion_flags(opt->opt);
    new_flags = (old_flags & ~GAVL_AUDIO_STEREO_TO_MONO_MASK);
    
    if(!strcmp(val->v.str, "left"))
      new_flags |= GAVL_AUDIO_STEREO_TO_MONO_LEFT;
    else if(!strcmp(val->v.str, "right"))
      new_flags |= GAVL_AUDIO_STEREO_TO_MONO_RIGHT;
    else if(!strcmp(val->v.str, "mix"))
      new_flags |= GAVL_AUDIO_STEREO_TO_MONO_MIX;

    if(old_flags |= new_flags)
      opt->options_changed = 1;
    
    gavl_audio_options_set_conversion_flags(opt->opt, new_flags);
    return 1;
    }

  else if(!strcmp(name, "dm"))
    {
    gavl_audio_dither_mode_t dither_mode = GAVL_AUDIO_DITHER_AUTO;
    if(!strcmp(val->v.str, "auto"))
      {
      dither_mode = GAVL_AUDIO_DITHER_AUTO;
      }
    else if(!strcmp(val->v.str, "none"))
      {
      dither_mode = GAVL_AUDIO_DITHER_NONE;
      }
    else if(!strcmp(val->v.str, "rect"))
      {
      dither_mode = GAVL_AUDIO_DITHER_RECT;
      }
    else if(!strcmp(val->v.str, "shaped"))
      {
      dither_mode = GAVL_AUDIO_DITHER_SHAPED;
      }

    if(dither_mode != gavl_audio_options_get_dither_mode(opt->opt))
      opt->options_changed = 1;
      
    gavl_audio_options_set_dither_mode(opt->opt, dither_mode);

    return 1;
    }

  
  else if(!strcmp(name, "sm"))
    {
    gavl_resample_mode_t resample_mode = GAVL_RESAMPLE_AUTO;
    if(!strcmp(val->v.str, "auto"))
      gavl_audio_options_set_resample_mode(opt->opt, GAVL_RESAMPLE_AUTO);

    else if(!strcmp(val->v.str, "linear"))
      resample_mode = GAVL_RESAMPLE_LINEAR;
    else if(!strcmp(val->v.str, "zoh"))
      resample_mode = GAVL_RESAMPLE_ZOH;
    else if(!strcmp(val->v.str, "sinc_fast"))
      resample_mode = GAVL_RESAMPLE_SINC_FAST;
    else if(!strcmp(val->v.str, "sinc_medium"))
      resample_mode = GAVL_RESAMPLE_SINC_MEDIUM;
    else if(!strcmp(val->v.str, "sinc_best"))
      resample_mode = GAVL_RESAMPLE_SINC_BEST;
    
    if(resample_mode != gavl_audio_options_get_resample_mode(opt->opt))
      opt->options_changed = 1;
    
    gavl_audio_options_set_resample_mode(opt->opt, resample_mode);
    
    return 1;
    }

  
  return 0;
  }

void bg_gavl_audio_options_init(bg_gavl_audio_options_t *opt)
  {
  memset(opt, 0, sizeof(*opt));
  opt->opt = gavl_audio_options_create();
  }

void bg_gavl_audio_options_free(bg_gavl_audio_options_t * opt)
  {
  if(opt->opt)
    gavl_audio_options_destroy(opt->opt);
  }


void bg_gavl_audio_options_set_format(const bg_gavl_audio_options_t * opt,
                                      const gavl_audio_format_t * in_format,
                                      gavl_audio_format_t * out_format)
  {
  int channel_index;

  if(in_format)
    gavl_audio_format_copy(out_format, in_format);

  if(opt->fixed_samplerate || !in_format)
    {
    out_format->samplerate = opt->samplerate;
    }
  if(opt->fixed_channel_setup || !in_format)
    {
    out_format->num_channels =
      opt->num_front_channels + opt->num_rear_channels + opt->num_lfe_channels;
    
    channel_index = 0;
    switch(opt->num_front_channels)
      {
      case 1:
        out_format->channel_locations[channel_index] = GAVL_CHID_FRONT_CENTER;
        break;
      case 2:
        out_format->channel_locations[channel_index] = GAVL_CHID_FRONT_LEFT;
        out_format->channel_locations[channel_index+1] = GAVL_CHID_FRONT_RIGHT;
        break;
      case 3:
        out_format->channel_locations[channel_index] = GAVL_CHID_FRONT_LEFT;
        out_format->channel_locations[channel_index+1] = GAVL_CHID_FRONT_RIGHT;
        out_format->channel_locations[channel_index+2] = GAVL_CHID_FRONT_CENTER;
        break;
      case 4:
        out_format->channel_locations[channel_index]   = GAVL_CHID_FRONT_LEFT;
        out_format->channel_locations[channel_index+1] = GAVL_CHID_FRONT_RIGHT;
        out_format->channel_locations[channel_index+2] = GAVL_CHID_FRONT_CENTER_LEFT;
        out_format->channel_locations[channel_index+3] = GAVL_CHID_FRONT_CENTER_LEFT;
        break;
      case 5:
        out_format->channel_locations[channel_index]   = GAVL_CHID_FRONT_LEFT;
        out_format->channel_locations[channel_index+1] = GAVL_CHID_FRONT_RIGHT;
        out_format->channel_locations[channel_index+2] = GAVL_CHID_FRONT_CENTER_LEFT;
        out_format->channel_locations[channel_index+3] = GAVL_CHID_FRONT_CENTER_LEFT;
        out_format->channel_locations[channel_index+4] = GAVL_CHID_FRONT_CENTER;
        break;
      }
    channel_index += opt->num_front_channels;
    
    switch(opt->num_rear_channels)
      {
      case 1:
        out_format->channel_locations[channel_index] = GAVL_CHID_REAR_CENTER;
        break;
      case 2:
        out_format->channel_locations[channel_index] = GAVL_CHID_REAR_LEFT;
        out_format->channel_locations[channel_index+1] = GAVL_CHID_REAR_RIGHT;
        break;
      case 3:
        out_format->channel_locations[channel_index] = GAVL_CHID_REAR_LEFT;
        out_format->channel_locations[channel_index+1] = GAVL_CHID_REAR_RIGHT;
        out_format->channel_locations[channel_index+2] = GAVL_CHID_REAR_CENTER;
        break;
      }
    channel_index += opt->num_rear_channels;
    switch(opt->num_lfe_channels)
      {
      case 1:
        out_format->channel_locations[channel_index] = GAVL_CHID_LFE;
        break;
      }
    channel_index += opt->num_lfe_channels;
    
    }
  if(opt->force_format != GAVL_SAMPLE_NONE)
    out_format->sample_format = opt->force_format;
  }

/* Video */

/* Definitions for standard resolutions */
  


/* Frame rates */

#define FRAME_RATE_FROM_INPUT  0
#define FRAME_RATE_USER        1
#define FRAME_RATE_23_976      2
#define FRAME_RATE_24          3
#define FRAME_RATE_25          4
#define FRAME_RATE_29_970      5
#define FRAME_RATE_30          6
#define FRAME_RATE_50          7
#define FRAME_RATE_59_940      8
#define FRAME_RATE_60          9
#define NUM_FRAME_RATES       10

static const struct
  {
  int rate;
  char * name;
  }
framerate_strings[NUM_FRAME_RATES] =
  {
    { FRAME_RATE_FROM_INPUT, "from_source"  },
    { FRAME_RATE_USER,       "user_defined" },
    { FRAME_RATE_23_976,     "23_976"       },
    { FRAME_RATE_24,         "24"           },
    { FRAME_RATE_25,         "25"           },
    { FRAME_RATE_29_970,     "29_970"       },
    { FRAME_RATE_30,         "30"           },
    { FRAME_RATE_50,         "50"           },
    { FRAME_RATE_59_940,     "59_940"       },
    { FRAME_RATE_60,         "60"           },
  };

static const struct
  {
  int rate;
  int timescale;
  int frame_duration;
  }
framerate_rates[NUM_FRAME_RATES] =
  {
    { FRAME_RATE_FROM_INPUT,     0,    0 },
    { FRAME_RATE_USER,           0,    0 },
    { FRAME_RATE_23_976,     24000, 1001 },
    { FRAME_RATE_24,            24,    1 },
    { FRAME_RATE_25,            25,    1 },
    { FRAME_RATE_29_970,     30000, 1001 },
    { FRAME_RATE_30,            30,    1 },
    { FRAME_RATE_50,            50,    1 },
    { FRAME_RATE_59_940,     60000, 1001 },
    { FRAME_RATE_60,            60,    1 },
  };

static int get_frame_rate_mode(const char * str)
  {
  int i;
  for(i = 0; i < NUM_FRAME_RATES; i++)
    {
    if(!strcmp(str, framerate_strings[i].name))
      {
      return framerate_strings[i].rate;
      }
    }
  return FRAME_RATE_USER;
  }
                

#define SP_FLAG(s, flag) if(!strcmp(name, s)) {               \
  flags = gavl_video_options_get_conversion_flags(opt->opt);  \
  if((val->v.i) && !(flags & flag))                         \
    {                                                         \
    opt->options_changed = 1;                                 \
    flags |= flag;                                            \
    }                                                         \
  else if(!(val->v.i) && (flags & flag))                    \
    {                                                         \
    opt->options_changed = 1;                                 \
    flags &= ~flag;                                           \
    }                                                         \
  gavl_video_options_set_conversion_flags(opt->opt, flags);   \
  return 1;                                                   \
  }

gavl_scale_mode_t bg_gavl_string_to_scale_mode(const char * str)
  {
  if(!strcmp(str, "auto"))
    return GAVL_SCALE_AUTO;
  else if(!strcmp(str, "nearest"))
    return GAVL_SCALE_NEAREST;
  else if(!strcmp(str, "bilinear"))
    return GAVL_SCALE_BILINEAR;
  else if(!strcmp(str, "quadratic"))
    return GAVL_SCALE_QUADRATIC;
  else if(!strcmp(str, "cubic_bspline"))
    return GAVL_SCALE_CUBIC_BSPLINE;
  else if(!strcmp(str, "cubic_mitchell"))
    return GAVL_SCALE_CUBIC_MITCHELL;
  else if(!strcmp(str, "cubic_catmull"))
    return GAVL_SCALE_CUBIC_CATMULL;
  else if(!strcmp(str, "sinc_lanczos"))
    return GAVL_SCALE_SINC_LANCZOS;
  else
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Unknown scale mode %s\n", str);
    return GAVL_SCALE_AUTO;
    }
      
  }

gavl_downscale_filter_t bg_gavl_string_to_downscale_filter(const char * str)
  {
  if(!strcmp(str, "auto"))
    return GAVL_DOWNSCALE_FILTER_AUTO;
  else if(!strcmp(str, "none"))
    return GAVL_DOWNSCALE_FILTER_NONE;
  else if(!strcmp(str, "wide"))
    return GAVL_DOWNSCALE_FILTER_WIDE;
  else if(!strcmp(str, "gauss"))
    return GAVL_DOWNSCALE_FILTER_GAUSS;
  else
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Unknown scale mode %s\n", str);
    return GAVL_DOWNSCALE_FILTER_GAUSS;
    }
  }

#define FRAME_SIZE_FROM_INPUT      0
#define FRAME_SIZE_USER            1
#define FRAME_SIZE_PAL_D1          2
#define FRAME_SIZE_PAL_D1_WIDE     3
#define FRAME_SIZE_PAL_DV          4
#define FRAME_SIZE_PAL_DV_WIDE     5
#define FRAME_SIZE_PAL_CVD         6
#define FRAME_SIZE_PAL_VCD         7
#define FRAME_SIZE_PAL_SVCD        8
#define FRAME_SIZE_PAL_SVCD_WIDE   9
#define FRAME_SIZE_NTSC_D1        10
#define FRAME_SIZE_NTSC_D1_WIDE   11
#define FRAME_SIZE_NTSC_DV        12
#define FRAME_SIZE_NTSC_DV_WIDE   13
#define FRAME_SIZE_NTSC_CVD       14
#define FRAME_SIZE_NTSC_VCD       15
#define FRAME_SIZE_NTSC_SVCD      16
#define FRAME_SIZE_NTSC_SVCD_WIDE 17
#define FRAME_SIZE_VGA            18
#define FRAME_SIZE_QVGA           19
#define FRAME_SIZE_720            20
#define FRAME_SIZE_1080           21
#define FRAME_SIZE_SQCIF          22
#define FRAME_SIZE_QCIF           23
#define FRAME_SIZE_CIF            24
#define FRAME_SIZE_4CIF           25
#define FRAME_SIZE_16CIF          26
#define NUM_FRAME_SIZES           27


static const struct
  {
  const char * name;
  int size;
  int image_width;
  int image_height;
  int pixel_width;
  int pixel_height;
  }
framesizes[] =
  {
    { "from_input",     FRAME_SIZE_FROM_INPUT,      0,      0,    0,    0},
    { "user_defined",   FRAME_SIZE_USER,            0,      0,    0,    0},
    { "pal_d1",         FRAME_SIZE_PAL_D1,          720,  576,   59,   54},
    { "pal_d1_wide",    FRAME_SIZE_PAL_D1_WIDE,     720,  576,  118,   81},
    { "pal_dv",         FRAME_SIZE_PAL_DV,          720,  576,   59,   54},
    { "pal_dv_wide",    FRAME_SIZE_PAL_DV_WIDE,     720,  576,  118,   81},
    { "pal_cvd",        FRAME_SIZE_PAL_CVD,         352,  576,   59,   27},
    { "pal_vcd",        FRAME_SIZE_PAL_VCD,         352,  288,   59,   54},
    { "pal_svcd",       FRAME_SIZE_PAL_SVCD,        480,  576,   59,   36},
    { "pal_svcd_wide",  FRAME_SIZE_PAL_SVCD_WIDE,   480,  576,   59,   27},
    { "ntsc_d1",        FRAME_SIZE_NTSC_D1,         720,  480,   10,   11},
    { "ntsc_d1_wide",   FRAME_SIZE_NTSC_D1_WIDE,    720,  480,   40,   33},
    { "ntsc_dv",        FRAME_SIZE_NTSC_DV,         720,  480,   10,   11},
    { "ntsc_dv_wide",   FRAME_SIZE_NTSC_DV_WIDE,    720,  480,   40,   33},
    { "ntsc_cvd",       FRAME_SIZE_NTSC_CVD,        352,  480,   20,   11},
    { "ntsc_vcd",       FRAME_SIZE_NTSC_VCD,        352,  240,   10,   11},
    { "ntsc_svcd",      FRAME_SIZE_NTSC_SVCD,       480,  480,   15,   11},
    { "ntsc_svcd_wide", FRAME_SIZE_NTSC_SVCD_WIDE,  480,  480,   20,   11},
    { "720",            FRAME_SIZE_720,             1280,  720,    1,    1},
    { "1080",           FRAME_SIZE_1080,            1920, 1080,    1,    1},
    { "vga",            FRAME_SIZE_VGA,             640,  480,    1,    1},
    { "qvga",           FRAME_SIZE_QVGA,            320,  240,    1,    1},
    { "sqcif",          FRAME_SIZE_SQCIF,           128,   96,   12,   11},
    { "qcif",           FRAME_SIZE_QCIF,            176,  144,   12,   11},
    { "cif",            FRAME_SIZE_CIF,             352,  288,   12,   11},
    { "4cif",           FRAME_SIZE_4CIF,            704,  576,   12,   11},
    { "16cif",          FRAME_SIZE_16CIF,           1408, 1152,   12,   11},
  };

static void set_frame_size_mode(bg_gavl_video_options_t * opt,
                                const gavl_value_t * val)
  {
  int i;
  for(i = 0; i < NUM_FRAME_SIZES; i++)
    {
    if(!strcmp(val->v.str, framesizes[i].name))
      opt->size = framesizes[i].size;
    }
  }

void bg_gavl_video_options_set_frame_size(const bg_gavl_video_options_t * opt,
                                          const gavl_video_format_t * in_format,
                                          gavl_video_format_t * out_format)
  {
  int i;
  
  if(opt->size == FRAME_SIZE_FROM_INPUT)
    {
    out_format->image_width =  in_format->image_width;
    out_format->image_height = in_format->image_height;
    out_format->frame_width =  in_format->frame_width;
    out_format->frame_height = in_format->frame_height;
    out_format->pixel_width  = in_format->pixel_width;
    out_format->pixel_height = in_format->pixel_height;
    return;
    }
  else if(opt->size == FRAME_SIZE_USER)
    {
    out_format->image_width = opt->user_image_width;
    out_format->image_height = opt->user_image_height;
    out_format->frame_width = opt->user_image_width;
    out_format->frame_height = opt->user_image_height;
    out_format->pixel_width = opt->user_pixel_width;
    out_format->pixel_height = opt->user_pixel_height;
    return;
    }
  
  for(i = 0; i < NUM_FRAME_SIZES; i++)
    {
    if(opt->size == framesizes[i].size)
      {
      out_format->image_width =  framesizes[i].image_width;
      out_format->image_height = framesizes[i].image_height;
      out_format->frame_width =  framesizes[i].image_width;
      out_format->frame_height = framesizes[i].image_height;
      out_format->pixel_width  = framesizes[i].pixel_width;
      out_format->pixel_height = framesizes[i].pixel_height;
      }
    }
  }

int bg_gavl_video_set_parameter(void * data, const char * name,
                                const gavl_value_t * val)
  {
  int flags;  
  bg_gavl_video_options_t * opt = (bg_gavl_video_options_t *)data;

  if(!name)
    return 1;
  if(!strcmp(name, "q"))
    {
    if(val->v.i != gavl_video_options_get_quality(opt->opt))
      opt->options_changed = 1;
    
    gavl_video_options_set_quality(opt->opt, val->v.i);
    return 1;
    }
  else if(!strcmp(name, "framerate"))
    {
    opt->framerate_mode = get_frame_rate_mode(val->v.str);
    return 1;
    }
  else if(!strcmp(name, "frame_size"))
    {
    set_frame_size_mode(opt, val);
    return 1;
    }
  else if(!strcmp(name, "pf"))
    {
    opt->pixelformat = gavl_string_to_pixelformat(val->v.str);
    return 1;
    }

  //  SP_INT(fixed_framerate);
  SP_INT(frame_duration);
  SP_INT(timescale);

  SP_INT_OPT(user_image_width, "w");
  SP_INT_OPT(user_image_height, "h");

  SP_INT_OPT(user_pixel_width, "pw");
  SP_INT_OPT(user_pixel_height, "ph");
  //  SP_INT(maintain_aspect);
  
  SP_FLAG("fd", GAVL_FORCE_DEINTERLACE);
  SP_FLAG("rc", GAVL_RESAMPLE_CHROMA);
  
  if(!strcmp(name, "alpha_mode"))
    {
    if(!strcmp(val->v.str, "ignore"))
      {
      gavl_video_options_set_alpha_mode(opt->opt, GAVL_ALPHA_IGNORE);
      }
    else if(!strcmp(val->v.str, "blend_color"))
      {
      gavl_video_options_set_alpha_mode(opt->opt, GAVL_ALPHA_BLEND_COLOR);
      }
    return 1;
    }
  else if(!strcmp(name, "background_color"))
    {
    gavl_video_options_set_background_color(opt->opt, val->v.color);
    return 1;
    }
  else if(!strcmp(name, "sm"))
    {
    gavl_video_options_set_scale_mode(opt->opt, bg_gavl_string_to_scale_mode(val->v.str));
    return 1;
    }
  else if(!strcmp(name, "so"))
    {
    gavl_video_options_set_scale_order(opt->opt, val->v.i);
    }
  else if(!strcmp(name, "dm"))
    {
    if(!strcmp(val->v.str, "none"))
      gavl_video_options_set_deinterlace_mode(opt->opt, GAVL_DEINTERLACE_NONE);
    else if(!strcmp(val->v.str, "copy"))
      gavl_video_options_set_deinterlace_mode(opt->opt, GAVL_DEINTERLACE_COPY);
    else if(!strcmp(val->v.str, "scale"))
      gavl_video_options_set_deinterlace_mode(opt->opt, GAVL_DEINTERLACE_SCALE);
    }
  else if(!strcmp(name, "ddm"))
    {
    if(!strcmp(val->v.str, "top"))
      gavl_video_options_set_deinterlace_drop_mode(opt->opt, GAVL_DEINTERLACE_DROP_TOP);
    else if(!strcmp(val->v.str, "bottom"))
      gavl_video_options_set_deinterlace_drop_mode(opt->opt, GAVL_DEINTERLACE_DROP_BOTTOM);
    }
  else if(!strcmp(name, "threads"))
    {
    opt->num_threads = val->v.i;
    if(!opt->thread_pool)
      {
      opt->thread_pool = gavl_thread_pool_create(opt->num_threads);
      gavl_video_options_set_thread_pool(opt->opt, opt->thread_pool);
      }
    return 1;
    }
  return 0;
  }

#undef SP_INT

void bg_gavl_video_options_init(bg_gavl_video_options_t * opt)
  {
  memset(opt, 0, sizeof(*opt));
  opt->opt = gavl_video_options_create();
  }

void bg_gavl_video_options_free(bg_gavl_video_options_t * opt)
  {
  if(opt->opt)
    gavl_video_options_destroy(opt->opt);
  if(opt->thread_pool)
    gavl_thread_pool_destroy(opt->thread_pool);
  }


void bg_gavl_video_options_set_framerate(const bg_gavl_video_options_t * opt,
                                         const gavl_video_format_t * in_format,
                                         gavl_video_format_t * out_format)
  {
  int i;
  if(opt->framerate_mode == FRAME_RATE_FROM_INPUT)
    {
    out_format->frame_duration = in_format->frame_duration;
    out_format->timescale =      in_format->timescale;
    out_format->framerate_mode = in_format->framerate_mode;
    return;
    }
  else if(opt->framerate_mode == FRAME_RATE_USER)
    {
    out_format->frame_duration = opt->frame_duration;
    out_format->timescale =      opt->timescale;
    out_format->framerate_mode = GAVL_FRAMERATE_CONSTANT;
    return;
    }
  else
    {
    for(i = 0; i < NUM_FRAME_RATES; i++)
      {
      if(opt->framerate_mode == framerate_rates[i].rate)
        {
        out_format->timescale      = framerate_rates[i].timescale;
        out_format->frame_duration = framerate_rates[i].frame_duration;
        out_format->framerate_mode = GAVL_FRAMERATE_CONSTANT;
        return;
        }
      }
    }
  }

static void set_interlace(const bg_gavl_video_options_t * opt,
                          const gavl_video_format_t * in_format,
                          gavl_video_format_t * out_format)
  {
  int flags = gavl_video_options_get_conversion_flags(opt->opt);
  if(flags & GAVL_FORCE_DEINTERLACE)
    out_format->interlace_mode = GAVL_INTERLACE_NONE;
  else
    out_format->interlace_mode = in_format->interlace_mode;
  }

void bg_gavl_video_options_set_pixelformat(const bg_gavl_video_options_t * opt,
                                           const gavl_video_format_t * in_format,
                                           gavl_video_format_t * out_format)
  {
  if(opt->pixelformat == GAVL_PIXELFORMAT_NONE)
    out_format->pixelformat = in_format->pixelformat;
  else
    out_format->pixelformat = opt->pixelformat;
  }

void bg_gavl_video_options_set_format(const bg_gavl_video_options_t * opt,
                                      const gavl_video_format_t * in_format,
                                      gavl_video_format_t * out_format)
  {
  bg_gavl_video_options_set_framerate(opt, in_format, out_format);
  set_interlace(opt, in_format, out_format);
  bg_gavl_video_options_set_frame_size(opt, in_format, out_format);
  }



int bg_overlay_too_old(gavl_time_t time, gavl_time_t ovl_time,
                       gavl_time_t ovl_duration)
  {
  if((ovl_duration >= 0) && (time > ovl_time + ovl_duration))
    return 1;
  return 0;
  }

int bg_overlay_too_new(gavl_time_t time, gavl_time_t ovl_time)
  {
  if(time < ovl_time)
    return 1;
  return 0;
  }

static int time_active = 0;

void bg_print_time_stop(FILE * out)
  {
  putc('\n', out);
  time_active = 0;
  }

void bg_print_time(FILE * out,
                   gavl_time_t time, gavl_time_t total_time)
  {
  char str[GAVL_TIME_STRING_LEN];
  int i;
  int len;
  
  if(time_active)
    putc('\r', out);
  
  gavl_time_prettyprint(time, str);

  fprintf(out, "[ ");
  len = strlen(str);

  for(i = 0; i < GAVL_TIME_STRING_LEN - len - 1; i++)
    {
    fprintf(out, " ");
    }
  fprintf(out, "%s ]/", str);

  gavl_time_prettyprint(total_time, str);

  fprintf(out, "[ ");
  len = strlen(str);

  for(i = 0; i < GAVL_TIME_STRING_LEN - len - 1; i++)
    {
    fprintf(out, " ");
    }
  fprintf(out, "%s ]", str);
  time_active = 1;
  }

/* json Support */

void bg_audio_format_to_json(const gavl_audio_format_t * fmt, struct json_object * obj)
  {
  gavl_dictionary_t dict;
  gavl_dictionary_init(&dict);
  gavl_audio_format_to_dictionary(fmt, &dict);
  bg_dictionary_to_json(&dict, obj);
  gavl_dictionary_free(&dict);
  }

int bg_audio_format_from_json(gavl_audio_format_t * fmt, struct json_object * obj)
  {
  int ret = 0;
  gavl_dictionary_t dict;
  gavl_dictionary_init(&dict);
  
  if(bg_dictionary_from_json(&dict, obj) &&
     gavl_audio_format_from_dictionary(fmt, &dict))
    ret = 1;
  
  gavl_dictionary_free(&dict);
  return ret;
  }

void bg_video_format_to_json(const gavl_video_format_t * fmt, struct json_object * obj)
  {
  gavl_dictionary_t dict;
  gavl_dictionary_init(&dict);
  gavl_video_format_to_dictionary(fmt, &dict);
  bg_dictionary_to_json(&dict, obj);
  gavl_dictionary_free(&dict);
  }

int bg_video_format_from_json(gavl_video_format_t * fmt, struct json_object * obj)
  {
  int ret = 0;
  gavl_dictionary_t dict;
  gavl_dictionary_init(&dict);
  
  if(bg_dictionary_from_json(&dict, obj) &&
     gavl_video_format_from_dictionary(fmt, &dict))
    ret = 1;
  
  gavl_dictionary_free(&dict);
  return ret;
  }

/* Value <-> json */

struct json_object * bg_value_to_json(const gavl_value_t * v)
  {
  const char * val;
  struct json_object * value = NULL;
  struct json_object * json = json_object_new_object();

  if(!(val = gavl_type_to_string(v->type)))
    return NULL;

  json_object_object_add(json, type_key,
                         json_object_new_string(val));

  switch(v->type)
    {
    case GAVL_TYPE_UNDEFINED:
      break;
    case GAVL_TYPE_INT:
      value = json_object_new_int(v->v.i);
      break;
    case GAVL_TYPE_LONG:
      value = json_object_new_int64(v->v.l);
      break;
    case GAVL_TYPE_FLOAT:
      value = json_object_new_double(v->v.d);
      break;
    case GAVL_TYPE_STRING:
      if(v->v.str)
        value = json_object_new_string(v->v.str);
      else
        value = NULL;
      break;
    case GAVL_TYPE_BINARY:
      {
      char * str = bg_base64_encode_buffer(v->v.buffer);
      if(v->v.str)
        value = json_object_new_string(str);
      else
        value = NULL;
      free(str);
      }
      break;
    case GAVL_TYPE_AUDIOFORMAT:
      value = json_object_new_object();
      bg_audio_format_to_json(v->v.audioformat, value);
      break;
    case GAVL_TYPE_VIDEOFORMAT:
      value = json_object_new_object();
      bg_video_format_to_json(v->v.videoformat, value);
      break;
    case GAVL_TYPE_COLOR_RGB:
      value = json_object_new_array();
      json_object_array_add(value, json_object_new_double(v->v.color[0]));
      json_object_array_add(value, json_object_new_double(v->v.color[1]));
      json_object_array_add(value, json_object_new_double(v->v.color[2]));
      break;
    case GAVL_TYPE_COLOR_RGBA:
      value = json_object_new_array();
      json_object_array_add(value, json_object_new_double(v->v.color[0]));
      json_object_array_add(value, json_object_new_double(v->v.color[1]));
      json_object_array_add(value, json_object_new_double(v->v.color[2]));
      json_object_array_add(value, json_object_new_double(v->v.color[3]));
      break;
    case GAVL_TYPE_POSITION:
      value = json_object_new_array();
      json_object_array_add(value, json_object_new_double(v->v.position[0]));
      json_object_array_add(value, json_object_new_double(v->v.position[1]));
      break;
    case GAVL_TYPE_DICTIONARY:
      {
      value = json_object_new_object();
      bg_dictionary_to_json(v->v.dictionary, value);
      }
      break;
    case GAVL_TYPE_ARRAY:
      {
      int i;
      value = json_object_new_array();
      for(i = 0; i < v->v.array->num_entries; i++)
        json_object_array_add(value, bg_value_to_json(&v->v.array->entries[i]));
      }
      break;
    }

  if(value)
    json_object_object_add(json, "v", value);
  return json;
  }


static int double_array_from_json(double * ret, struct json_object * obj, int num)
  {
  int i;

  if(!json_object_is_type(obj, json_type_array) ||
     (json_object_array_length(obj) != num))
    return 0;

  for(i = 0; i < num; i++)
    ret[i] = json_object_get_double(json_object_array_get_idx(obj, i));
  return 1;
  }

/* */
int bg_value_from_json_external(gavl_value_t * v, struct json_object * obj)
  {
  int ret = 0;
  
  switch(json_object_get_type(obj))
    {
    case json_type_null:
      break;
    case json_type_boolean:
      gavl_value_set_int(v, json_object_get_boolean(obj));
      break;
    case json_type_double:
      gavl_value_set_float(v, json_object_get_double(obj));
      break;
    case json_type_int:
      gavl_value_set_int(v, json_object_get_int(obj));
      break;
    case json_type_object:
      {
      gavl_dictionary_t * dict;
      gavl_value_t child;

      dict = gavl_value_set_dictionary(v);

        {
        json_object_object_foreach(obj, key, val)
          {
          gavl_value_init(&child);
          if(!bg_value_from_json_external(&child, val))
            goto fail;
          gavl_dictionary_set_nocopy(dict, key, &child);
          }
        }
      }
      break;
    case json_type_array:
      {
      gavl_array_t * arr;
      int i, num;
      gavl_value_t child;

      arr = gavl_value_set_array(v);
      
      num = json_object_array_length(obj);
      for(i = 0; i < num; i++)
        {
        gavl_value_init(&child);
        bg_value_from_json_external(&child, json_object_array_get_idx(obj, i));
        gavl_array_splice_val_nocopy(arr, i, 0, &child);
        }
      }
      break;
    case json_type_string:
      gavl_value_set_string(v, json_object_get_string(obj));
      break;
    }
  
  
  ret = 1;
  fail:
  
  return ret;
  }

int bg_value_from_json(gavl_value_t * v, struct json_object * obj)
  {
  gavl_type_t gavl_type;
  struct json_object * type;
  struct json_object * value;

  /* Handle NULL */
  if(json_object_is_type(obj, json_type_null))
    {
    gavl_value_init(v);
    return 1;
    }
  
  /* Do some sanity checks */
  if(!json_object_is_type(obj, json_type_object))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Wrong type for value %p (must be object): %s",
           obj, json_object_to_json_string(obj));
    return 0;
    }

  if(!json_object_object_get_ex(obj, type_key, &type))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "type missing %s",
           json_object_to_json_string(obj));
    return 0;
    }
  
  if(!json_object_is_type(type, json_type_string))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "type is no string %s",
           json_object_to_json_string(obj));
    return 0;
    }

  if(!(gavl_type = gavl_type_from_string(json_object_get_string(type))))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "No such type %s",
           json_object_get_string(type));
    return 0;
    }

  gavl_value_set_type(v, gavl_type);
  
  if(!json_object_object_get_ex(obj, "v", &value))
    {
    if(v->type == GAVL_TYPE_STRING)
      v->v.str = NULL;
    else
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "No value %s",
             json_object_to_json_string(obj));
      return 0;
      }
    }

  switch(v->type)
    {
    case GAVL_TYPE_UNDEFINED:
      break;
    case GAVL_TYPE_INT:
      v->v.i = json_object_get_int(value);
      break;
    case GAVL_TYPE_LONG:
      v->v.l = json_object_get_int64(value);
      break;
    case GAVL_TYPE_FLOAT:
      v->v.d = json_object_get_double(value);
      break;
    case GAVL_TYPE_STRING:
      v->v.str = gavl_strdup(json_object_get_string(value));
      break;
    case GAVL_TYPE_BINARY:
      {
      const char * str;
      gavl_buffer_t * buf = gavl_value_get_binary_nc(v);
      str = json_object_get_string(value);
      bg_base64_decode_buffer(str, buf);
      }
      break;
    case GAVL_TYPE_AUDIOFORMAT:
      {
      gavl_audio_format_t * afmt;
      afmt = gavl_value_set_audio_format(v);
      return bg_audio_format_from_json(afmt, value);
      }
      break;
    case GAVL_TYPE_VIDEOFORMAT:
      {
      gavl_video_format_t * vfmt;
      vfmt = gavl_value_set_video_format(v);
      return bg_video_format_from_json(vfmt, value);
      }
      break;
    case GAVL_TYPE_COLOR_RGB:
      double_array_from_json(v->v.color, value, 3);
      break;
    case GAVL_TYPE_COLOR_RGBA:
      double_array_from_json(v->v.color, value, 4);
      break;
    case GAVL_TYPE_POSITION:
      double_array_from_json(v->v.position, value, 2);
      break;
    case GAVL_TYPE_DICTIONARY:
      return bg_dictionary_from_json(v->v.dictionary, value);
      break;
    case GAVL_TYPE_ARRAY:
      {
      int i, num;
      struct json_object * el;
      gavl_array_t * arr;
      
      arr = gavl_value_set_array(v);
      
      num = json_object_array_length(value);

      arr->entries_alloc = num;
      arr->num_entries = num;
      arr->entries = calloc(arr->entries_alloc,
                            sizeof(*arr->entries));
      
      for(i = 0; i < num; i++)
        {
        el = json_object_array_get_idx(value, i);
        if(!bg_value_from_json(&arr->entries[i], el))
          return 0;
        }
      }
      break;
      
    }
  return 1;
  }



void bg_dictionary_to_json(const gavl_dictionary_t * dict,
                           struct json_object * json)
  {
  int i;
  //  fprintf(stderr, "bg_dictionary_to_json\n");
  //  gavl_dictionary_dump(dict, 2);
  for(i = 0; i < dict->num_entries; i++)
    {
    //    fprintf(stderr, "Adding object %s:\n", dict->entries[i].name);
    //    gavl_value_dump(&dict->entries[i].v, 2);
    
    json_object_object_add(json,
                           dict->entries[i].name,
                           bg_value_to_json(&dict->entries[i].v));
    //    fprintf(stderr, "\nAdding object %s done\n", dict->entries[i].name);
    }
  }

int bg_dictionary_from_json(gavl_dictionary_t * dict,
                            struct json_object * json)
  {
  gavl_value_t gval;
  json_object_object_foreach(json, key, val)
    {
    gavl_value_init(&gval);
    if(!bg_value_from_json(&gval, val))
      return 0;
    gavl_dictionary_set_nocopy(dict, key, &gval);
    }
  return 1;
  }

/* */

/*
 *  value <-> xml
 */


int bg_xml_2_value(xmlNodePtr xml_val,
                   gavl_value_t * v)
  {
  gavl_type_t gavl_type;
  int ret = 0;
  const char * tmp_string;
  char * type = NULL;

  type = BG_XML_GET_PROP(xml_val, type_key);

  if(!type)
    goto fail;
  
  if(!(gavl_type = gavl_type_from_string(type)))
    goto fail;

  gavl_value_set_type(v, gavl_type);
  
  switch(v->type)
    {
    case GAVL_TYPE_UNDEFINED:
      break;
    case GAVL_TYPE_INT:
      tmp_string = bg_xml_node_get_text_content(xml_val);
      if(sscanf(tmp_string, "%d", &v->v.i) != 1)
        goto fail;
      break;
    case GAVL_TYPE_LONG:
      tmp_string = bg_xml_node_get_text_content(xml_val);
      if(sscanf(tmp_string, "%"PRId64, &v->v.l) != 1)
        goto fail;
      break;
    case GAVL_TYPE_FLOAT:
      tmp_string = bg_xml_node_get_text_content(xml_val);
      if(sscanf(tmp_string, "%lf", &v->v.d) != 1)
        goto fail;
      break;
    case GAVL_TYPE_AUDIOFORMAT:
      {
      gavl_audio_format_t * afmt;
      int res = 0;
      gavl_dictionary_t dict;
      gavl_dictionary_init(&dict);

      afmt = gavl_value_set_audio_format(v);
      
      if(bg_xml_2_dictionary(xml_val, &dict) &&
         gavl_audio_format_from_dictionary(afmt, &dict))
        res = 1;
      gavl_dictionary_free(&dict);

      if(!res)
        goto fail;
      }
      break;
    case GAVL_TYPE_VIDEOFORMAT:
      {
      int res = 0;
      gavl_dictionary_t dict;
      gavl_video_format_t * vfmt;
      gavl_dictionary_init(&dict);

      vfmt = gavl_value_set_video_format(v);

      if(bg_xml_2_dictionary(xml_val, &dict) &&
         gavl_video_format_from_dictionary(vfmt, &dict))
        res = 1;
      gavl_dictionary_free(&dict);
      if(!res)
        goto fail;
      }
      break;
    case GAVL_TYPE_COLOR_RGB:
      tmp_string = bg_xml_node_get_text_content(xml_val);
      if(sscanf(tmp_string, "%lf %lf %lf", &v->v.color[0], &v->v.color[1], &v->v.color[2]) != 3)
        goto fail;
      break;
    case GAVL_TYPE_COLOR_RGBA:
      tmp_string = bg_xml_node_get_text_content(xml_val);
      if(sscanf(tmp_string, "%lf %lf %lf %lf", &v->v.color[0], &v->v.color[1], &v->v.color[2], &v->v.color[3]) != 4)
        goto fail;
      break;
    case GAVL_TYPE_POSITION:
      tmp_string = bg_xml_node_get_text_content(xml_val);
      if(sscanf(tmp_string, "%lf %lf", &v->v.position[0], &v->v.position[1]) != 2)
        goto fail;
      break;
    case GAVL_TYPE_STRING:
      tmp_string = bg_xml_node_get_text_content(xml_val);
      v->v.str = gavl_strdup(tmp_string);
      break;
    case GAVL_TYPE_BINARY:
      {
      gavl_buffer_t * buf = gavl_value_get_binary_nc(v);
      tmp_string = bg_xml_node_get_text_content(xml_val);
      bg_base64_decode_buffer(tmp_string, buf);
      }
      break;
    case GAVL_TYPE_DICTIONARY:
      bg_xml_2_dictionary(xml_val, v->v.dictionary);
      break;
    case GAVL_TYPE_ARRAY:
      {
      gavl_value_t val;
      xmlNodePtr node = xml_val->children;
      gavl_value_init(&val);
      while(node)
        {
        if(!node->name)
          {
          node = node->next;
          continue;
          }
        if(!BG_XML_STRCMP(node->name, el_key))
          {
          if(!bg_xml_2_value(node, &val))
            goto fail;
          gavl_array_splice_val_nocopy(v->v.array, -1, 0, &val);
          }
        node = node->next;
        }
      break;
      }
    }
  ret = 1;
  fail:
  
  if(type)
    xmlFree(type);
  
  return ret;
  }

// xmlDocPtr xml_doc, 
int bg_xml_2_dictionary(xmlNodePtr xml_metadata,
                        gavl_dictionary_t * ret)
  {
  xmlNodePtr node;
  char * name;
  
  node = xml_metadata->children;

  while(node)
    {
    if(!node->name)
      {
      node = node->next;
      continue;
      }
    if(!BG_XML_STRCMP(node->name, el_key))
      {
      if((name = BG_XML_GET_PROP(node, name_key)))
        {
        gavl_value_t val;
        gavl_value_init(&val);
        if(!bg_xml_2_value(node, &val))
          return 0;
        gavl_dictionary_set_nocopy(ret, name, &val);
        free(name);
        }
      }
    node = node->next;
    }
  return 1;
  }

static void value_2_xml(xmlNodePtr xml_val,
                        const gavl_value_t * v, int indent)
  {
  char * tmp_string;
  const char * type = gavl_type_to_string(v->type);
  
  if(!type)
    return;

  BG_XML_SET_PROP(xml_val, type_key, type);
  
  switch(v->type)
    {
    case GAVL_TYPE_UNDEFINED:
      break;
    case GAVL_TYPE_INT:
      tmp_string = bg_sprintf("%d", v->v.i);
      xmlAddChild(xml_val, BG_XML_NEW_TEXT(tmp_string));
      free(tmp_string);
      break;
    case GAVL_TYPE_LONG:
      tmp_string = bg_sprintf("%"PRId64, v->v.l);
      xmlAddChild(xml_val, BG_XML_NEW_TEXT(tmp_string));
      free(tmp_string);
      break;
    case GAVL_TYPE_FLOAT:
      tmp_string = bg_sprintf("%f", v->v.d);
      xmlAddChild(xml_val, BG_XML_NEW_TEXT(tmp_string));
      free(tmp_string);
      break;
    case GAVL_TYPE_AUDIOFORMAT:
      {
      gavl_dictionary_t dict;
      gavl_dictionary_init(&dict);
      gavl_audio_format_to_dictionary(v->v.audioformat, &dict);
      bg_dictionary_2_xml(xml_val, &dict, indent + 1);
      gavl_dictionary_free(&dict);
      }
      break;
    case GAVL_TYPE_VIDEOFORMAT:
      {
      gavl_dictionary_t dict;
      gavl_dictionary_init(&dict);
      gavl_video_format_to_dictionary(v->v.videoformat, &dict);
      bg_dictionary_2_xml(xml_val, &dict, indent + 1);
      gavl_dictionary_free(&dict);
      }
      break;
    case GAVL_TYPE_COLOR_RGB:
      tmp_string = bg_sprintf("%f %f %f",
                              v->v.color[0], v->v.color[1],
                              v->v.color[2]);
      xmlAddChild(xml_val, BG_XML_NEW_TEXT(tmp_string));
      free(tmp_string);
      break;
    case GAVL_TYPE_COLOR_RGBA:
      tmp_string = bg_sprintf("%f %f %f %f",
                              v->v.color[0], v->v.color[1],
                              v->v.color[2], v->v.color[3]);
      xmlAddChild(xml_val, BG_XML_NEW_TEXT(tmp_string));
      free(tmp_string);
      break;
    case GAVL_TYPE_POSITION:
      tmp_string = bg_sprintf("%f %f",
                              v->v.position[0], v->v.position[1]);
      xmlAddChild(xml_val, BG_XML_NEW_TEXT(tmp_string));
      free(tmp_string);
      break;
    case GAVL_TYPE_STRING:
      xmlAddChild(xml_val, BG_XML_NEW_TEXT(v->v.str));
      break;
    case GAVL_TYPE_BINARY:
      {
      char * str;
      str = bg_base64_encode_buffer(v->v.buffer);
      xmlAddChild(xml_val, BG_XML_NEW_TEXT(str));
      free(str);
      }
      break;
    case GAVL_TYPE_DICTIONARY:
      bg_dictionary_2_xml(xml_val, v->v.dictionary, indent);
      break;
    case GAVL_TYPE_ARRAY:
      {
      int i;
      xmlNodePtr el;
      const gavl_array_t * arr = v->v.array;

      for(i = 0; i < arr->num_entries; i++)
        {
        el = xmlNewTextChild(xml_val, NULL, (xmlChar*)el_key, NULL);
        value_2_xml(el, &arr->entries[i], indent + 1);
        }
      }
      break;
    }

  }

void bg_value_2_xml(xmlNodePtr xml_val,
                    const gavl_value_t * v)
  {
  value_2_xml(xml_val, v, 0);
  }

typedef struct
  {
  xmlNodePtr dict;
  int indent;
  } dict_to_xml_t;

static void xml_newline_and_indent(xmlNodePtr parent, int indent)
  {
  int i;
  char * buf = calloc(1, indent + 2);
  buf[0] = '\n';

  for(i = 0; i < indent; i++)
    buf[1+i] = ' ';
  
  xmlAddChild(parent, BG_XML_NEW_TEXT(buf));

  free(buf);
  
  }

static void dict_to_xml_func(void * priv, const char * name,
                                 const gavl_value_t * val)
  {
  xmlNodePtr child;

  dict_to_xml_t * d = priv;
  xmlNodePtr xml_dict = d->dict;

  xml_newline_and_indent(xml_dict, d->indent);
  
  child = xmlNewTextChild(xml_dict, NULL,
                          (xmlChar*)el_key, NULL);
  
  BG_XML_SET_PROP(child, name_key, name);
  value_2_xml(child, val, d->indent);
  }

void bg_dictionary_2_xml(xmlNodePtr xml_dict,
                         const gavl_dictionary_t * m, int indent)
  {
  dict_to_xml_t d;
  d.indent = indent + 1;
  d.dict = xml_dict;
  gavl_dictionary_foreach(m, dict_to_xml_func, &d);
  xml_newline_and_indent(xml_dict, indent);
  }

/* Load save */

static int load_value(xmlDocPtr xml_doc, const char * root, 
                      gavl_value_t * ret)
  {
  xmlNodePtr root_node;
  root_node = xml_doc->children;

  if(BG_XML_STRCMP(root_node->name, root))
    return 0;
  return bg_xml_2_value(root_node, ret);
  }

int bg_value_load_xml(gavl_value_t * ret,
                      const char * filename,
                      const char * root)
  {
  int result;
  xmlDocPtr xml_doc;
  
  if(!(xml_doc = bg_xml_parse_file(filename, 1)))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Couldn't open file %s", filename);
    return 0;
    }

  result = load_value(xml_doc, root, ret);
  
  xmlFreeDoc(xml_doc);
  return result;
  }

int bg_value_load_xml_string(gavl_value_t * ret,
                             const char * str, int len,
                             const char * root)
  {
  int result;
  xmlDocPtr xml_doc;
  
  if(!(xml_doc = xmlParseMemory(str, len)))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Couldn't parse xml");
    return 0;
    }

  result = load_value(xml_doc, root, ret);
  
  xmlFreeDoc(xml_doc);
  return result;
  }

void bg_value_save_xml(const gavl_value_t * val,
                       const char * filename,
                       const char * root)
  {
  xmlNodePtr root_node;
  xmlDocPtr  xml_doc;

  xml_doc = xmlNewDoc((xmlChar*)"1.0");
  root_node = xmlNewDocRawNode(xml_doc, NULL, (xmlChar*)root, NULL);
  xmlDocSetRootElement(xml_doc, root_node);
  
  bg_value_2_xml(root_node, val);
  bg_xml_save_file(xml_doc, filename, 1);
  xmlFreeDoc(xml_doc);
  }

char * bg_value_save_xml_string(const gavl_value_t * val,
                                const char * root)
  {
  char * ret;
  xmlNodePtr root_node;
  xmlDocPtr  xml_doc;

  xml_doc = xmlNewDoc((xmlChar*)"1.0");
  root_node = xmlNewDocRawNode(xml_doc, NULL, (xmlChar*)root, NULL);
  xmlDocSetRootElement(xml_doc, root_node);
  
  bg_value_2_xml(root_node, val);
  ret = bg_xml_save_to_memory(xml_doc);
  xmlFreeDoc(xml_doc);
  return ret;
  }

int bg_dictionary_load_xml(gavl_dictionary_t * ret,
                           const char * filename,
                           const char * root)
  {
  gavl_value_t val;
  gavl_value_init(&val);

  if(!bg_value_load_xml(&val, filename, root) ||
     (val.type != GAVL_TYPE_DICTIONARY))
    {
    gavl_value_free(&val);
    return 0;
    }

  gavl_dictionary_reset(ret);
  gavl_dictionary_move(ret, val.v.dictionary);
  gavl_value_free(&val);
  return 1;
  }

int bg_array_load_xml(gavl_array_t * ret,
                      const char * filename,
                      const char * root)
  {
  gavl_value_t val;
  gavl_value_init(&val);

  if(!bg_value_load_xml(&val, filename, root) ||
     (val.type != GAVL_TYPE_ARRAY))
    {
    gavl_value_free(&val);
    return 0;
    }

  gavl_array_reset(ret);
  gavl_array_move(ret, val.v.array);
  gavl_value_free(&val);
  return 1;
  }

int bg_dictionary_load_xml_string(gavl_dictionary_t * ret,
                                  const char * str,
                                  int len,
                                  const char * root)
  {
  gavl_value_t val;
  gavl_value_init(&val);

  if(!bg_value_load_xml_string(&val, str, len, root) ||
     (val.type != GAVL_TYPE_DICTIONARY))
    {
    gavl_value_free(&val);
    return 0;
    }
  gavl_dictionary_reset(ret);
  gavl_dictionary_move(ret, val.v.dictionary);
  gavl_value_free(&val);
  return 1;
  }

void bg_dictionary_save_xml(const gavl_dictionary_t * d,
                            const char * filename,
                            const char * root)
  {
  gavl_dictionary_t * d1;
  gavl_value_t val;
  gavl_value_init(&val);

  d1 = gavl_value_set_dictionary(&val);
  memcpy(d1, d, sizeof(*d));
  bg_value_save_xml(&val, filename, root);
  gavl_dictionary_init(d1);
  gavl_value_free(&val);
  }

void bg_array_save_xml(const gavl_array_t * d,
                       const char * filename,
                       const char * root)
  {
  gavl_array_t * d1;
  gavl_value_t val;
  gavl_value_init(&val);

  d1 = gavl_value_set_array(&val);
  memcpy(d1, d, sizeof(*d));
  bg_value_save_xml(&val, filename, root);
  gavl_array_init(d1);
  gavl_value_free(&val);
  }

char * bg_dictionary_save_xml_string(const gavl_dictionary_t * d,
                                     const char * root)
  {
  char * ret;
  gavl_dictionary_t * d1;
  gavl_value_t val;
  gavl_value_init(&val);

  d1 = gavl_value_set_dictionary(&val);
  memcpy(d1, d, sizeof(*d));
  
  ret = bg_value_save_xml_string(&val, root);
  gavl_dictionary_init(d1);
  gavl_value_free(&val);
  return ret;
  }

/* Load json url and return the parsed object */

json_object * bg_json_from_url(const char * url, char ** mimetype_ptr)
  {
  json_object * ret = NULL;
  
  gavl_dictionary_t dict;
  gavl_buffer_t buf;
  const char * var;

  gavl_dictionary_init(&dict);
  gavl_buffer_init(&buf);
  
  if(!bg_http_get(url, &buf, &dict))
    goto fail;

  if(!mimetype_ptr)
    {
    if(!(var = gavl_dictionary_get_string(&dict, GAVL_META_MIMETYPE)) ||
       strncasecmp(var, "application/json", 16))
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Invalid mimetype %s for json parsing from %s",
             var, url);
      goto fail;
      }
    }
  else
    *mimetype_ptr = gavl_strdup(gavl_dictionary_get_string(&dict, GAVL_META_MIMETYPE));

  ret = json_tokener_parse((const char*)buf.buf);
  
  fail:

  gavl_buffer_free(&buf);
  gavl_dictionary_free(&dict);

  return ret;
  }

const char * bg_json_dict_get_string(json_object * obj, const char * tag)
  {
  json_object * child;
  const char * ret;

  if(!json_object_object_get_ex(obj, tag, &child) ||
     !json_object_is_type(child, json_type_string) ||
     !(ret = json_object_get_string(child)))
    return NULL;
  return ret;
  }

int bg_json_dict_get_int(json_object * obj, const char * tag)
  {
  json_object * child;

  if(!json_object_object_get_ex(obj, tag, &child) ||
     !json_object_is_type(child, json_type_int))
    return 0;
  
  return json_object_get_int(child);
  }

double bg_json_dict_get_double(json_object * obj, const char * tag)
  {
  json_object * child;

  if(!json_object_object_get_ex(obj, tag, &child) ||
     !json_object_is_type(child, json_type_double))
    return 0;
  
  return json_object_get_double(child);
  }

int bg_json_dict_get_bool(json_object * obj, const char * tag)
  {
  json_object * child;

  if(!json_object_object_get_ex(obj, tag, &child) ||
     !json_object_is_type(child, json_type_boolean))
    return 0;
  
  return json_object_get_boolean(child);
  }


void bg_string_to_string_array(const char * str, gavl_array_t * arr)
  {
  int idx = 0;
  char ** str_arr;

  if(!str)
    return;
  
  str_arr = gavl_strbreak(str, ' ');
  while(str_arr[idx])
    {
    gavl_string_array_add(arr, str_arr[idx]);
    idx++;
    }
  gavl_strbreak_free(str_arr);
  }

char * bg_string_array_to_string(const gavl_array_t * arr)
  {
  int idx = 0;
  char * ret = NULL;
  const char * str;
  
  while((str = gavl_string_array_get(arr, idx)))
    {
    if(ret)
      ret = gavl_strcat(ret, " ");
    
    ret = gavl_strcat(ret, str);
    idx++;
    }
  return ret;
  }

