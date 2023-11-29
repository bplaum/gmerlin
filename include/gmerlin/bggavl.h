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


#ifndef BGGAVL_H_INCLUDED
#define BGGAVL_H_INCLUDED

#include <json-c/json.h>
#include <libxml/tree.h>
#include <libxml/parser.h>

#include <gavl/metadata.h>
#include <gavl/value.h>


/* Struct for converting audio */

typedef struct
  {
  gavl_audio_options_t * opt;
  int fixed_samplerate;
  int samplerate;
  int fixed_channel_setup;
  
  gavl_sample_format_t force_format;
  
  int num_front_channels;
  int num_rear_channels;
  int num_lfe_channels;

  int options_changed;
  } bg_gavl_audio_options_t;

int
bg_gavl_audio_set_parameter(void * data, const char * name,
                            const gavl_value_t * val);

void bg_gavl_audio_options_init(bg_gavl_audio_options_t *);

void bg_gavl_audio_options_free(bg_gavl_audio_options_t *);

void bg_gavl_audio_options_set_format(const bg_gavl_audio_options_t *,
                                      const gavl_audio_format_t * in_format,
                                      gavl_audio_format_t * out_format);


typedef struct
  {
  gavl_video_options_t * opt;
  gavl_pixelformat_t pixelformat;
  
  int framerate_mode;
  int frame_duration;
  int timescale;

  int size;
  int user_image_width;
  int user_image_height;
  int user_pixel_width;
  int user_pixel_height;
  
  gavl_thread_pool_t * thread_pool;
  int num_threads;
  int options_changed;
  } bg_gavl_video_options_t;

int bg_gavl_video_set_parameter(void * data, const char * name,
                                const gavl_value_t * val);

void bg_gavl_video_options_init(bg_gavl_video_options_t *);

void bg_gavl_video_options_free(bg_gavl_video_options_t *);

void bg_gavl_video_options_set_framerate(const bg_gavl_video_options_t *,
                                         const gavl_video_format_t * in_format,
                                         gavl_video_format_t * out_format);

void bg_gavl_video_options_set_frame_size(const bg_gavl_video_options_t * opt,
                                          const gavl_video_format_t * in_format,
                                          gavl_video_format_t * out_format);

void bg_gavl_video_options_set_pixelformat(const bg_gavl_video_options_t * opt,
                                           const gavl_video_format_t * in_format,
                                           gavl_video_format_t * out_format);

void bg_gavl_video_options_set_format(const bg_gavl_video_options_t *,
                                      const gavl_video_format_t * in_format,
                                      gavl_video_format_t * out_format);

void bg_gavl_video_options_set_rectangles(const bg_gavl_video_options_t * opt,
                                          const gavl_video_format_t * in_format,
                                          const gavl_video_format_t * out_format,
                                          int do_crop);

gavl_scale_mode_t bg_gavl_string_to_scale_mode(const char * str);
gavl_downscale_filter_t bg_gavl_string_to_downscale_filter(const char * str);

#if 0
void bg_gavl_video_options_set_framerate(const bg_gavl_video_options_t *,
                                         const gavl_video_format_t * in_format,
                                         gavl_video_format_t * out_format);
void bg_gavl_video_options_set_framesize(const bg_gavl_video_options_t *,
                                         const gavl_video_format_t * in_format,
                                         gavl_video_format_t * out_format);
void bg_gavl_video_options_set_interlace(const bg_gavl_video_options_t * opt,
                                         const gavl_video_format_t * in_format,
                                         gavl_video_format_t * out_format);
#endif

/* Useful code for gluing gavl and gmerlin */

#define BG_GAVL_PARAM_CONVERSION_QUALITY \
  {                                      \
  .name =        "q",     \
  .long_name =   TRS("Conversion Quality"),          \
  .type =        BG_PARAMETER_SLIDER_INT,               \
  .flags =       BG_PARAMETER_SYNC,                     \
  .val_min =     GAVL_VALUE_INIT_INT(GAVL_QUALITY_FASTEST),   \
  .val_max =     GAVL_VALUE_INIT_INT(GAVL_QUALITY_BEST),       \
  .val_default = GAVL_VALUE_INIT_INT(GAVL_QUALITY_DEFAULT),             \
  .help_string = TRS("Set the conversion quality for format conversions. \
Lower quality means more speed. Values above 3 enable slow high quality calculations.") \
   }

#define BG_GAVL_PARAM_FRAMERATE_USER           \
  {                                            \
  .name =      "timescale",                    \
  .long_name = TRS("Timescale"),               \
  .type =      BG_PARAMETER_INT,               \
  .val_min =     GAVL_VALUE_INIT_INT(1),               \
  .val_max =     GAVL_VALUE_INIT_INT(100000),          \
  .val_default = GAVL_VALUE_INIT_INT(25),              \
  .help_string = TRS("Timescale for user defined output framerate (Framerate = timescale / frame duration)."), \
  },                                           \
  {                                            \
  .name =      "frame_duration",               \
  .long_name = TRS("Frame duration"),          \
  .type =      BG_PARAMETER_INT,               \
  .val_min =     GAVL_VALUE_INIT_INT(1),               \
  .val_max =     GAVL_VALUE_INIT_INT(100000),          \
  .val_default = GAVL_VALUE_INIT_INT(1),               \
  .help_string = TRS("Frame duration for user defined output framerate (Framerate = timescale / frame duration)."), \
  }

#define BG_GAVL_PARAM_FRAMERATE_NAMES \
                   "user_defined",    \
                   "23_976",          \
                   "24",              \
                   "25",              \
                   "29_970",          \
                     "30",            \
                   "50",              \
                   "59_940",          \
                   "60", (char*)0

#define BG_GAVL_PARAM_FRAMERATE_LABELS                                   \
                   TRS("User defined"),                                  \
                   TRS("23.976 (NTSC encapsulated film rate)"),          \
                   TRS("24 (Standard international cinema film rate)"),  \
                   TRS("25 (PAL [625/50] video frame rate)"),            \
                   TRS("29.970 (NTSC video frame rate)"),                \
                   TRS("30 (NTSC drop-frame [525/60] video frame rate)"),\
                   TRS("50 (Double frame rate / progressive PAL)"),      \
                   TRS("59.940 (Double frame rate NTSC)"),               \
                   TRS("60 (Double frame rate drop-frame NTSC)"),        \
                    (char*)0


#define BG_GAVL_PARAM_FRAMERATE                        \
  {                                                    \
  .name =      "framerate",                            \
  .long_name = TRS("Framerate"),                       \
  .type =      BG_PARAMETER_STRINGLIST,                \
  .flags =       BG_PARAMETER_SYNC,                    \
  .val_default = GAVL_VALUE_INIT_STRING("from_source"),         \
  .multi_names = (char const *[]){ "from_source",      \
  BG_GAVL_PARAM_FRAMERATE_NAMES                        \
                  },                                   \
  .multi_labels = (char const *[]){ TRS("From Source"),\
  BG_GAVL_PARAM_FRAMERATE_LABELS                       \
                   },                                  \
  .help_string = TRS("Output framerate. For user defined framerate, enter the \
timescale and frame duration below (framerate = timescale / frame duration).")\
  },                                                   \
BG_GAVL_PARAM_FRAMERATE_USER

#define BG_GAVL_PARAM_FRAMERATE_NOSOURCE \
  {                                      \
  .name =      "framerate",              \
  .long_name = TRS("Framerate"),         \
  .type =      BG_PARAMETER_STRINGLIST,  \
  .flags =       BG_PARAMETER_SYNC,      \
  .val_default = GAVL_VALUE_INIT_STRING("25"),    \
  .multi_names = (char const *[]){       \
  BG_GAVL_PARAM_FRAMERATE_NAMES          \
                  },                     \
  .multi_labels = (char const *[]){      \
  BG_GAVL_PARAM_FRAMERATE_LABELS         \
                   },                    \
  .help_string = TRS("Output framerate. For user defined framerate, enter the \
timescale and frame duration below (framerate = timescale / frame duration).")\
  },                                     \
BG_GAVL_PARAM_FRAMERATE_USER


    
#define BG_GAVL_PARAM_DEINTERLACE           \
 {                                          \
  .name =      "dm",          \
  .long_name = TRS("Deinterlace mode"),     \
  .type =      BG_PARAMETER_STRINGLIST,     \
  .val_default = GAVL_VALUE_INIT_STRING("none"),     \
  .multi_names =  (char const *[]){ "none", "copy", "scale", (char*)0 },                \
  .multi_labels = (char const *[]){ TRS("None"), TRS("Copy"), TRS("Scale"), (char*)0 }, \
  .help_string = "Specify interlace mode. Higher modes are better but slower."          \
  },                                        \
  {                                         \
  .name =      "ddm",     \
  .long_name = "Drop mode",                 \
  .type =      BG_PARAMETER_STRINGLIST,     \
  .val_default = GAVL_VALUE_INIT_STRING("top"),      \
  .multi_names =   (char const *[]){ "top", "bottom", (char*)0 }, \
  .multi_labels =  (char const *[]){ TRS("Drop top field"), TRS("Drop bottom field"), (char*)0 },   \
  .help_string = TRS("Specifies which field the deinterlacer should drop.") \
  },                                        \
  {                                         \
  .name =      "fd",       \
  .long_name = TRS("Force deinterlacing"),  \
  .type =      BG_PARAMETER_CHECKBUTTON,    \
  .val_default = GAVL_VALUE_INIT_INT(0),            \
  .help_string = TRS("Force deinterlacing if you want progressive output and the input format pretends to be progressive also.")                                     \
  }

#define BG_GAVL_PARAM_PIXELFORMAT                      \
 {                                                     \
  .name =      "pf",                          \
  .long_name = TRS("Pixelformat"),                     \
  .type =      BG_PARAMETER_STRINGLIST,                \
  .val_default = GAVL_VALUE_INIT_STRING("YUV 444 Planar"),      \
  .multi_names =  (char const *[]){                    \
     "YUV 444 Planar",          "YUVA 4444 (8 bit)",   \
     "YUV 444 Planar (16 bit)", "YUVA 4444 (16 bit)",  \
     "YUV 444 (float)",         "YUVA 4444 (float)",   \
     "32 bpp RGB",              "32 bpp RGBA",         \
     "48 bpp RGB",              "64 bpp RGBA",         \
     "Float RGB",               "Float RGBA",          \
     (char*)0 },                                       \
  .multi_labels = (char const *[]){                    \
     TRS("Y'CbCr 8 bit"), TRS("Y'CbCrA 8 bit"),        \
     TRS("Y'CbCr 16 bit"), TRS("Y'CbCrA 16 bit"),      \
     TRS("Y'CbCr float"), TRS("Y'CbCrA float"),        \
     TRS("RGB 8 bit"), TRS("RGBA 8 bit"),              \
     TRS("RGB 16 bit"), TRS("RGBA 16 bit"),            \
     TRS("RGB float"), TRS("RGBA float"),              \
     (char*)0 },                                       \
  .help_string = TRS("Specify the pixelformat"),       \
  }

#define BG_GAVL_SCALE_MODE_NAMES \
   (char const *[]){ "auto",     \
              "nearest",         \
              "bilinear",        \
              "quadratic",       \
              "cubic_bspline",   \
              "cubic_mitchell",  \
              "cubic_catmull",   \
              "sinc_lanczos",    \
              (char*)0 }

#define BG_GAVL_SCALE_MODE_LABELS   \
  (char const *[]){ TRS("Auto"),    \
             TRS("Nearest"),        \
             TRS("Bilinear"),       \
             TRS("Quadratic"),      \
             TRS("Cubic B-Spline"), \
             TRS("Cubic Mitchell-Netravali"), \
             TRS("Cubic Catmull-Rom"), \
             TRS("Sinc with Lanczos window"), \
            (char*)0 }

#define BG_GAVL_TRANSFORM_MODE_NAMES \
   (char const *[]){ "auto",   \
              "nearest",       \
              "bilinear",      \
              "quadratic",     \
              "cubic_bspline", \
              (char*)0 }

#define BG_GAVL_TRANSFORM_MODE_LABELS \
  (char const *[]){ TRS("Auto"),      \
             TRS("Nearest"),          \
             TRS("Bilinear"),         \
             TRS("Quadratic"),        \
             TRS("Cubic B-Spline"),   \
            (char*)0 }

#define BG_GAVL_DOWNSCALE_FILTER_NAMES \
  (char const *[]){ "auto", \
                    "none", \
                    "wide", \
                    "gauss", \
                    (char*)0 }

#define BG_GAVL_DOWNSCALE_FILTER_LABELS \
  (char const *[]){ TRS("Auto"), \
                    TRS("None"),            \
                    TRS("Widening"), \
                    TRS("Gaussian preblur"), \
                    (char*)0 }

#define BG_GAVL_PARAM_SCALE_MODE               \
  {                                            \
  .name =        "sm",                 \
  .long_name =   TRS("Scale mode"),            \
  .type =        BG_PARAMETER_STRINGLIST,      \
  .flags =       BG_PARAMETER_SYNC,            \
  .multi_names = BG_GAVL_SCALE_MODE_NAMES,     \
  .multi_labels = BG_GAVL_SCALE_MODE_LABELS,   \
  .val_default = GAVL_VALUE_INIT_STRING("auto"),        \
  .help_string = TRS("Choose scaling method. Auto means to choose based on the conversion quality. Nearest is fastest, Sinc with Lanczos window is slowest."),          \
  },                                           \
  {                                            \
  .name =        "so",                \
  .long_name =   TRS("Scale order"),           \
  .type =        BG_PARAMETER_INT,             \
  .flags =       BG_PARAMETER_SYNC,            \
  .val_min =     GAVL_VALUE_INIT_INT(4),               \
  .val_max =     GAVL_VALUE_INIT_INT(1000),            \
  .val_default = GAVL_VALUE_INIT_INT(4),               \
  .help_string = TRS("Order for sinc scaling"),\
  }

#define BG_GAVL_PARAM_RESAMPLE_CHROMA     \
  {                                       \
  .name =        "rc",       \
  .long_name =   TRS("Resample chroma"),  \
  .type =        BG_PARAMETER_CHECKBUTTON,\
  .flags =       BG_PARAMETER_SYNC,       \
    .help_string = TRS("Always perform chroma resampling if chroma subsampling factors or chroma placements are different. Usually, this is only done for qualities above 3."), \
  }

#define BG_GAVL_PARAM_BACKGROUND \
   \
    { \
      .name =        "background_color", \
      .long_name =   TRS("Background color"), \
      .type =      BG_PARAMETER_COLOR_RGB, \
      .flags =       BG_PARAMETER_SYNC,                     \
      .val_default = GAVL_VALUE_INIT_COLOR_RGB(0.0, 0.0, 0.0),          \
      .help_string = TRS("Background color to use, when alpha mode above is \"Blend background color\"."), \
    }

#define BG_GAVL_PARAM_ALPHA                \
    { \
      .name =        "alpha_mode", \
      .long_name =   TRS("Alpha mode"), \
      .type =        BG_PARAMETER_STRINGLIST, \
      .flags =       BG_PARAMETER_SYNC,                     \
      .val_default = GAVL_VALUE_INIT_STRING("ignore"), \
      .multi_names = (char const *[]){"ignore", "blend_color", (char*)0}, \
      .multi_labels = (char const *[]){TRS("Ignore"), TRS("Blend background color"), (char*)0}, \
    .help_string = TRS("This option is used if the source has an alpha (=transparency) channel, but the output supports no transparency. Either, the transparency is ignored, or the background color you specify below is blended in."),\
       }, \
  BG_GAVL_PARAM_BACKGROUND

#define BG_GAVL_PARAM_FRAMESIZE_USER           \
    { \
      .name =      "w", \
      .long_name = TRS("User defined width"), \
      .type =      BG_PARAMETER_INT,    \
      .flags =     BG_PARAMETER_SYNC, \
      .val_min =     GAVL_VALUE_INIT_INT(1), \
      .val_max =     GAVL_VALUE_INIT_INT(100000), \
      .val_default = GAVL_VALUE_INIT_INT(640), \
      .help_string = TRS("User defined width in pixels. Only meaningful if you selected \"User defined\" for the image size."), \
    }, \
    {                                         \
      .name =      "h", \
      .long_name = TRS("User defined height"), \
      .type =      BG_PARAMETER_INT, \
      .flags =     BG_PARAMETER_SYNC, \
      .val_min =     GAVL_VALUE_INIT_INT(1), \
      .val_max =     GAVL_VALUE_INIT_INT(100000), \
      .val_default = GAVL_VALUE_INIT_INT(480), \
      .help_string = TRS("User defined height in pixels. Only meaningful if you selected \"User defined\" for the image size."), \
      }, \
    { \
      .name =      "pw", \
      .long_name = TRS("User defined pixel width"), \
      .type =      BG_PARAMETER_INT,    \
      .flags =     BG_PARAMETER_SYNC, \
      .val_min =     GAVL_VALUE_INIT_INT(1), \
      .val_max =     GAVL_VALUE_INIT_INT(100000),\
      .val_default = GAVL_VALUE_INIT_INT(1),\
      .help_string = TRS("User defined pixel width. Only meaningful if you selected \"User defined\" for the image size."),\
    },\
    {                                       \
      .name =      "ph",\
      .long_name = TRS("User defined pixel height"),\
      .type =      BG_PARAMETER_INT,\
      .flags =     BG_PARAMETER_SYNC,\
      .val_min =     GAVL_VALUE_INIT_INT(1),\
      .val_max =     GAVL_VALUE_INIT_INT(100000),\
      .val_default = GAVL_VALUE_INIT_INT(1),\
      .help_string = TRS("User defined pixel height. Only meaningful if you selected \"User defined\" for the image size."),\
    }


#define BG_GAVL_PARAM_FRAMESIZE_NAMES           \
        "user_defined", \
        "pal_d1", \
        "pal_d1_wide", \
        "pal_dv", \
        "pal_dv_wide", \
        "pal_cvd", \
        "pal_vcd", \
        "pal_svcd", \
        "pal_svcd_wide", \
        "ntsc_d1", \
        "ntsc_d1_wide", \
        "ntsc_dv", \
        "ntsc_dv_wide", \
        "ntsc_cvd", \
        "ntsc_vcd", \
        "ntsc_svcd", \
        "ntsc_svcd_wide", \
        "720", \
        "1080", \
        "vga", \
        "qvga", \
        "sqcif", \
        "qcif", \
        "cif", \
        "4cif", \
        "16cif", \
        (char*)0

#define BG_GAVL_PARAM_FRAMESIZE_LABELS           \
        TRS("User defined"), \
        TRS("PAL DVD D1 4:3 (720 x 576)"), \
        TRS("PAL DVD D1 16:9 (720 x 576)"), \
        TRS("PAL DV 4:3 (720 x 576)"), \
        TRS("PAL DV 16:9 (720 x 576)"), \
        TRS("PAL CVD (352 x 576)"), \
        TRS("PAL VCD (352 x 288)"), \
        TRS("PAL SVCD 4:3 (480 x 576)"), \
        TRS("PAL SVCD 16:9 (480 x 576)"), \
        TRS("NTSC DVD D1 4:3 (720 x 480)"), \
        TRS("NTSC DVD D1 16:9 (720 x 480)"), \
        TRS("NTSC DV 4:3 (720 x 480)"), \
        TRS("NTSC DV 16:9 (720 x 480)"), \
        TRS("NTSC CVD (352 x 480)"), \
        TRS("NTSC VCD (352 x 240)"), \
        TRS("NTSC SVCD 4:3 (480 x 480)"), \
        TRS("NTSC SVCD 16:9 (480 x 480)"), \
        TRS("HD 720p/i (1280x720)"), \
        TRS("HD 1080p/i (1920x1080)"), \
        TRS("VGA (640 x 480)"), \
        TRS("QVGA (320 x 240)"), \
        TRS("SQCIF (128 × 96)"), \
        TRS("QCIF (176 × 144)"), \
        TRS("CIF (352 × 288)"), \
        TRS("4CIF (704 × 576)"), \
        TRS("16CIF (1408 × 1152)"), \
        (char*)0

#define BG_GAVL_PARAM_FRAMESIZE_NOSOURCE       \
    { \
      .name =        "frame_size", \
      .long_name =   TRS("Image size"), \
      .type =        BG_PARAMETER_STRINGLIST, \
      .flags =     BG_PARAMETER_SYNC, \
      .multi_names = (char const *[]){ \
      BG_GAVL_PARAM_FRAMESIZE_NAMES \
        }, \
      .multi_labels =  (char const *[]){  \
      BG_GAVL_PARAM_FRAMESIZE_LABELS \
        }, \
      .val_default = GAVL_VALUE_INIT_STRING("pal_d1"), \
      .help_string = TRS("Set the output image size. For a user defined size, you must specify the width and height as well as the pixel width and pixel height."), \
    }, \
BG_GAVL_PARAM_FRAMESIZE_USER

#define BG_GAVL_PARAM_FRAMESIZE       \
    { \
      .name =        "frame_size", \
      .long_name =   TRS("Image size"), \
      .type =        BG_PARAMETER_STRINGLIST, \
      .flags =     BG_PARAMETER_SYNC, \
      .multi_names = (char const *[]){ \
      "from_source", \
      BG_GAVL_PARAM_FRAMESIZE_NAMES \
        }, \
      .multi_labels =  (char const *[]){  \
      TRS("From source"), \
      BG_GAVL_PARAM_FRAMESIZE_LABELS \
        }, \
      .val_default = GAVL_VALUE_INIT_STRING("from_source"), \
      .help_string = TRS("Set the output image size. For a user defined size, you must specify the width and height as well as the pixel width and pixel height."), \
    }, \
BG_GAVL_PARAM_FRAMESIZE_USER

#define BG_GAVL_PARAM_SAMPLERATE_NOSOURCE \
    {\
      .name =        "samplerate",\
      .long_name =   TRS("Samplerate"),\
      .type =        BG_PARAMETER_INT,\
      .flags =       BG_PARAMETER_SYNC,                     \
      .val_min =     GAVL_VALUE_INIT_INT(8000),\
      .val_max =     GAVL_VALUE_INIT_INT(192000),\
      .val_default = GAVL_VALUE_INIT_INT(44100),\
      .help_string = TRS("Samplerate"),\
    }


#define BG_GAVL_PARAM_SAMPLERATE                \
    {\
      .name =      "fixed_samplerate",\
      .long_name = TRS("Fixed samplerate"),\
      .type =      BG_PARAMETER_CHECKBUTTON,\
      .flags =       BG_PARAMETER_SYNC,                     \
      .val_default = GAVL_VALUE_INIT_INT(0),\
      .help_string = TRS("If disabled, the output samplerate is taken from the source. If enabled, the samplerate you specify below us used.")\
    },\
BG_GAVL_PARAM_SAMPLERATE_NOSOURCE

#define BG_GAVL_PARAM_CHANNEL_SETUP_NOSOURCE \
    {                                         \
    .name =        "num_front_channels",          \
    .long_name =   TRS("Front channels"),              \
    .type =        BG_PARAMETER_INT,              \
    .flags =       BG_PARAMETER_SYNC,                     \
    .val_min =     GAVL_VALUE_INIT_INT(1),                  \
    .val_max =     GAVL_VALUE_INIT_INT(5),                  \
    .val_default = GAVL_VALUE_INIT_INT(2),                  \
    },\
    {                                         \
    .name =        "num_rear_channels",          \
    .long_name =   TRS("Rear channels"),              \
    .type =        BG_PARAMETER_INT,              \
    .flags =       BG_PARAMETER_SYNC,                     \
    .val_min =     GAVL_VALUE_INIT_INT(0),                  \
    .val_max =     GAVL_VALUE_INIT_INT(3),                  \
    .val_default = GAVL_VALUE_INIT_INT(0),                  \
    },                                        \
    {                                         \
    .name =        "num_lfe_channels",          \
    .long_name =   TRS("LFE"),                        \
    .type =        BG_PARAMETER_CHECKBUTTON,     \
    .flags =       BG_PARAMETER_SYNC,                     \
    .val_default = GAVL_VALUE_INIT_INT(0),                  \
    },                                        \
    {                                 \
      .name =        "front_to_rear", \
      .long_name =   TRS("Front to rear mode"), \
      .type =        BG_PARAMETER_STRINGLIST, \
      .flags =       BG_PARAMETER_SYNC,                     \
      .val_default = GAVL_VALUE_INIT_STRING("copy"), \
      .multi_names =  (char const *[]){ "mute", \
                              "copy", \
                              "diff", \
                              (char*)0 }, \
      .multi_labels =  (char const *[]){ TRS("Mute"), \
                              TRS("Copy"), \
                              TRS("Diff"), \
                              (char*)0 }, \
      .help_string = TRS("Mix mode when the output format has rear channels, \
but the source doesn't."), \
    }, \
    { \
      .name =        "stereo_to_mono", \
      .long_name =   TRS("Stereo to mono mode"), \
      .type =        BG_PARAMETER_STRINGLIST, \
      .flags =       BG_PARAMETER_SYNC,                     \
      .val_default = GAVL_VALUE_INIT_STRING("mix"), \
      .multi_names =  (char const *[]){ "left", \
                              "right", \
                              "mix", \
                              (char*)0 }, \
      .multi_labels =  (char const *[]){ TRS("Choose left"), \
                              TRS("Choose right"), \
                              TRS("Mix"), \
                              (char*)0 }, \
      .help_string = TRS("Mix mode when downmixing Stereo to Mono."), \
    }


#define BG_GAVL_PARAM_CHANNEL_SETUP \
    { \
      .name =      "fixed_channel_setup", \
      .long_name = TRS("Fixed channel setup"), \
      .type =      BG_PARAMETER_CHECKBUTTON,\
      .flags =       BG_PARAMETER_SYNC,                     \
      .val_default = GAVL_VALUE_INIT_INT(0),\
      .help_string = TRS("If disabled, the output channel configuration is taken from the source. If enabled, the setup you specify below us used.") \
    }, \
BG_GAVL_PARAM_CHANNEL_SETUP_NOSOURCE


#define BG_GAVL_PARAM_FORCE_SAMPLEFORMAT \
    { \
      .name =      "sampleformat", \
      .long_name = TRS("Force sampleformat"), \
      .type =      BG_PARAMETER_STRINGLIST,\
      .flags =       BG_PARAMETER_SYNC, \
      .val_default = GAVL_VALUE_INIT_STRING("none"),\
      .multi_names  = (char const *[]){ "none", "8", "16", "32", "f", "d", (char*)0 },\
      .multi_labels = (char const *[]){ TRS("None"), TRS("8 bit"), TRS("16 bit"), TRS("32 bit"), TRS("Float"), TRS("Double"), (char*)0 }, \
      .help_string = TRS("Force a sampleformat to be used for processing. None means to take the input format.") \
    }

#define BG_GAVL_PARAM_SAMPLEFORMAT \
    { \
      .name =      "sampleformat", \
      .long_name = TRS("Sampleformat"), \
      .type =      BG_PARAMETER_STRINGLIST,\
      .flags =       BG_PARAMETER_SYNC, \
      .val_default = GAVL_VALUE_INIT_STRING("f"),\
      .multi_names  = (char const *[]){ "16", "32", "f", "d", (char*)0 },\
      .multi_labels = (char const *[]){ TRS("16 bit"), TRS("32 bit"), TRS("Float"), TRS("Double"), (char*)0 }, \
      .help_string = TRS("Sampleformat to be used for processing.") \
    }

#define BG_GAVL_PARAM_AUDIO_DITHER_MODE \
    { \
      .name =      "dm", \
      .long_name = TRS("Dither mode"), \
      .type =      BG_PARAMETER_STRINGLIST,\
      .flags =       BG_PARAMETER_SYNC,                     \
      .val_default = GAVL_VALUE_INIT_STRING("auto"),\
      .multi_names =  (char const *[]){ "auto", "none", "rect",        "tri",        "shaped", (char*)0 },\
      .multi_labels = (char const *[]){ TRS("Auto"), TRS("None"), TRS("Rectangular"), \
                               TRS("Triangular"), TRS("Shaped"), (char*)0 },\
      .help_string = TRS("Dither mode. Auto means to use the quality level. Subsequent options are ordered by increasing quality (i.e. decreasing speed).") \
    }

#define BG_GAVL_PARAM_RESAMPLE_MODE \
    { \
      .name =      "sm", \
      .long_name = TRS("Resample mode"), \
      .type =      BG_PARAMETER_STRINGLIST,\
      .flags =       BG_PARAMETER_SYNC,                     \
      .val_default = GAVL_VALUE_INIT_STRING("auto"),\
      .multi_names =  (char const *[]){ "auto", "zoh", "linear", "sinc_fast",  "sinc_medium", "sinc_best", (char*)0 },\
      .multi_labels = (char const *[]){ TRS("Auto"), TRS("Zero order hold"), TRS("Linear"), \
                               TRS("Sinc fast"),  TRS("Sinc medium"), TRS("Sinc best"), (char*)0 },\
      .help_string = TRS("Resample mode. Auto means to use the quality level. Subsequent options are ordered by increasing quality (i.e. decreasing speed).") \
    }

#define BG_GAVL_PARAM_THREADS         \
    {                                 \
      .name = "threads",              \
      .long_name = TRS("Number of threads"), \
      .type =      BG_PARAMETER_INT,\
      .val_default = GAVL_VALUE_INIT_INT(1),\
      .val_min     = GAVL_VALUE_INIT_INT(1),\
      .val_max     = GAVL_VALUE_INIT_INT(1024),\
      .help_string = TRS("Threads to launch for processing operations. Changing this requires program restart"), \
    }

/* Subtitle display decisions */
int bg_overlay_too_old(gavl_time_t time, gavl_time_t ovl_time,
                       gavl_time_t ovl_duration);

int bg_overlay_too_new(gavl_time_t time, gavl_time_t ovl_time);


/* Print time */

void bg_print_time_stop(FILE * out);

void bg_print_time(FILE * out,
                   gavl_time_t time, gavl_time_t total_time);



/* JSON interface */
void bg_audio_format_to_json(const gavl_audio_format_t * fmt, struct json_object * obj);
int bg_audio_format_from_json(gavl_audio_format_t * fmt, struct json_object * obj);
void bg_video_format_to_json(const gavl_video_format_t * fmt, struct json_object * obj);
int bg_video_format_from_json(gavl_video_format_t * fmt, json_object * obj);

struct json_object * bg_value_to_json(const gavl_value_t * m);
int bg_value_from_json(gavl_value_t * m, struct json_object * obj);

int bg_value_from_json_external(gavl_value_t * v, struct json_object * obj);

void bg_dictionary_to_json(const gavl_dictionary_t * dict,
                           struct json_object * json);

int bg_dictionary_from_json(gavl_dictionary_t * dict,
                            struct json_object * json);

const char * bg_json_dict_get_string(json_object * obj, const char * tag);
int bg_json_dict_get_int(json_object * obj, const char * tag);
double bg_json_dict_get_double(json_object * obj, const char * tag);
int bg_json_dict_get_bool(json_object * obj, const char * tag);

/* xml Interface */

int bg_xml_2_value(xmlNodePtr xml_val,
                   gavl_value_t * v);

void bg_value_2_xml(xmlNodePtr xml_val,
                    const gavl_value_t * v);

int bg_value_load_xml(gavl_value_t * ret,
                      const char * filename,
                      const char * root);

int bg_value_load_xml_string(gavl_value_t * ret,
                             const char * str, int len,
                             const char * root);

void bg_value_save_xml(const gavl_value_t * ret,
                       const char * filename,
                       const char * root);

char * bg_value_save_xml_string(const gavl_value_t * val,
                                const char * root);

int bg_dictionary_load_xml(gavl_dictionary_t * ret,
                           const char * filename,
                           const char * root);

void bg_dictionary_save_xml(const gavl_dictionary_t * val,
                            const char * filename,
                            const char * root);

int bg_array_load_xml(gavl_array_t * ret,
                      const char * filename,
                      const char * root);

void bg_array_save_xml(const gavl_array_t * val,
                       const char * filename,
                       const char * root);


int bg_dictionary_load_xml_string(gavl_dictionary_t * ret,
                                  const char * str, int len,
                                  const char * root);

char * bg_dictionary_save_xml_string(const gavl_dictionary_t * d,
                                     const char * root);

gavl_dictionary_t * bg_edl_load(const char * filename);

void bg_edl_save(const gavl_dictionary_t * edl, const char * filename);

/* XML Interface */

/** \brief Convert a libxml2 node into a metadata struct
 *  \param xml_doc Pointer to the xml document
 *  \param xml_metadata Pointer to the xml node containing the metadata
 *  \param ret Metadata container, where the info will be stored
 *
 *  See the libxml2 documentation for more infos
 */

int bg_xml_2_dictionary(xmlNodePtr xml_dictionary, gavl_dictionary_t * ret);

/** \brief Convert a metadata struct into a libxml2 node
 *  \param ret Metadata
 *  \param xml_metadata Pointer to the xml node for the metadata
 *
 *  See the libxml2 documentation for more infos
 */

void bg_dictionary_2_xml(xmlNodePtr xml_metadata,
                         const gavl_dictionary_t * ret, int indent);

json_object * bg_json_from_url(const char * url, char ** mimetype_ptr);

#define BG_TRACK_FORMAT_GMERLIN 1
#define BG_TRACK_FORMAT_XSPF    2
#define BG_TRACK_FORMAT_M3U     3
#define BG_TRACK_FORMAT_PLS     4
#define BG_TRACK_FORMAT_URILIST 5

#define bg_tracks_mimetype   "application/gmerlintracks"

char * bg_tracks_to_string(const gavl_dictionary_t * dict, int format, int local);
int bg_tracks_from_string(gavl_dictionary_t * dict, int format, const char * str, int len);

void bg_string_to_string_array(const char * str, gavl_array_t * arr);
char * bg_string_array_to_string(const gavl_array_t * arr);




#endif // BGGAVL_H_INCLUDED

