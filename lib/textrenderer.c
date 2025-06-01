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



/* System includes */

#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <math.h>

/* Fontconfig */

// #include <fontconfig/fontconfig.h>

#include <cairo/cairo.h>
#include <pango/pango.h>
#include <pango/pangocairo.h>


/* Gmerlin */

#include <config.h>
#include <gmerlin/translation.h>
#include <gmerlin/log.h>
#define LOG_DOMAIN "textrenderer"


#include <gmerlin/bgcairo.h>

// #include <bgfreetype.h>

#include <gmerlin/parameter.h>
#include <gmerlin/textrenderer.h>
#include <gmerlin/utils.h>

/* Text alignment */

#define JUSTIFY_CENTER 0
#define JUSTIFY_LEFT   1
#define JUSTIFY_RIGHT  2

#define JUSTIFY_TOP    1
#define JUSTIFY_BOTTOM 2

#define MODE_SIMPLE    0
#define MODE_OUTLINE   1
#define MODE_BOX       2

static const bg_parameter_info_t parameters[] =
  {
    {
      .name =       "render_options",
      .long_name =  TRS("Render options"),
      .type =       BG_PARAMETER_SECTION,
    },
    {
      .name      = "mode",
      .long_name = TRS("Mode"),
      .type      = BG_PARAMETER_STRINGLIST,
      .multi_names  = (const char*[]){ "simple", "box", "outline", NULL },
      .multi_labels = (const char*[]){ TRS("Simple"), TRS("Box"), TRS("Outline"), NULL },
      .val_default  = GAVL_VALUE_INIT_STRING("outline"),
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
      .type =       BG_PARAMETER_COLOR_RGBA,
      .val_default = GAVL_VALUE_INIT_COLOR_RGBA(0.0, 0.0, 0.0, 1.0),
    },
    {
      .name =       "border_width",
      .long_name =  TRS("Border width"),
      .type =       BG_PARAMETER_FLOAT,
      .val_min =     GAVL_VALUE_INIT_FLOAT(0.0),
      .val_max =     GAVL_VALUE_INIT_FLOAT(10.0),
      .val_default = GAVL_VALUE_INIT_FLOAT(1.0),
      .num_digits =  2,
    },
    {
      .name =       "fontname",
      .long_name =  TRS("Font"),
      .type =       BG_PARAMETER_FONT,
      .val_default = GAVL_VALUE_INIT_STRING("Sans Bold 20")
    },
    {
      .name =       "justify_h",
      .long_name =  TRS("Horizontal justify"),
      .type =       BG_PARAMETER_STRINGLIST,
      .val_default = GAVL_VALUE_INIT_STRING("center"),
      .multi_names =  (char const *[]){ "center", "left", "right", NULL },
      .multi_labels = (char const *[]){ TRS("Center"), TRS("Left"), TRS("Right"), NULL  },
      .help_string = TRS("Horizontal justification of the text in the box"),
    },
    {
      .name =       "justify_v",
      .long_name =  TRS("Vertical justify"),
      .type =       BG_PARAMETER_STRINGLIST,
      .val_default = GAVL_VALUE_INIT_STRING("bottom"),
      .multi_names =  (char const *[]){ "center", "top", "bottom",NULL  },
      .multi_labels = (char const *[]){ TRS("Center"), TRS("Top"), TRS("Bottom"), NULL },
    },
    {
      .name =       "justify_box_h",
      .long_name =  TRS("Horizontal box justify"),
      .type =       BG_PARAMETER_STRINGLIST,
      .val_default = GAVL_VALUE_INIT_STRING("center"),
      .multi_names =  (char const *[]){ "center", "left", "right", NULL },
      .multi_labels = (char const *[]){ TRS("Center"), TRS("Left"), TRS("Right"), NULL  },
      .help_string = TRS("Horizontal justification of the text box on the screen"),
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
      .help_string = TRS("Ignore linebreaks")
    },
    {
      .name =       "box_color",
      .long_name =  TRS("Box color"),
      .type =       BG_PARAMETER_COLOR_RGBA,
      .val_default = GAVL_VALUE_INIT_COLOR_RGBA(0.2, 0.2, 0.2, 0.7),
    },
    {
      .name =       "box_radius",
      .long_name =  TRS("Box corner radius"),
      .type =       BG_PARAMETER_FLOAT,
      .val_default = GAVL_VALUE_INIT_FLOAT(10.0),
      .val_min     = GAVL_VALUE_INIT_FLOAT(0.0),
      .val_max     = GAVL_VALUE_INIT_FLOAT(20.0),
    },
    {
      .name =       "box_padding",
      .long_name =  TRS("Box padding"),
      .type =       BG_PARAMETER_FLOAT,
      .val_default = GAVL_VALUE_INIT_FLOAT(10.0),
     .val_min     = GAVL_VALUE_INIT_FLOAT(0.0),
      .val_max     = GAVL_VALUE_INIT_FLOAT(20.0),
    },
    {
      .name =       "default_format",
      .long_name =  TRS("Default format"),
      .type =       BG_PARAMETER_SECTION,
    },
    {
      .name =       "default_width",
      .long_name =  TRS("Default width"),
      .type =       BG_PARAMETER_INT,
      .val_min =     GAVL_VALUE_INIT_INT(0),
      .val_max =     GAVL_VALUE_INIT_INT(65535),
      .val_default = GAVL_VALUE_INIT_INT(640),
    },
    {
      .name =       "default_height",
      .long_name =  TRS("Default height"),
      .type =       BG_PARAMETER_INT,
      .val_min =     GAVL_VALUE_INIT_INT(0),
      .val_max =     GAVL_VALUE_INIT_INT(65535),
      .val_default = GAVL_VALUE_INIT_INT(480),
    },
    {
      .name =       "default_framerate",
      .long_name =  TRS("Default Framerate"),
      .type =       BG_PARAMETER_FLOAT,
      .val_default =  GAVL_VALUE_INIT_FLOAT(10.0),
      .num_digits = 3,
    },
    { /* End of parameters */ },
  };

typedef struct
  {
  int xmin, xmax, ymin, ymax;
  } bbox_t;

struct bg_text_renderer_s
  {
  int mode;
  
  int font_loaded;
  int font_changed;
  
  /* Configuration stuff */

  char * font;
  char * font_file;
  
  double font_size;
  
  float color[4];
  float border_color[4];
  float border_width;

  float box_color[4];
  float box_radius;
  float box_padding;
  
  gavl_video_format_t overlay_format;

  gavl_video_format_t fmt;   /* Scaled format: This is used by pango and cairo */

  gavl_video_format_t frame_format;

  int justify_h;
  int justify_box_h;
  int justify_v;
  int border_left, border_right, border_top, border_bottom;
  int ignore_linebreaks;

  int sub_h, sub_v; /* Chroma subsampling of the final destination frame */
  
  pthread_mutex_t config_mutex;

  int config_changed;

  int fixed_width;
  
  int default_width, default_height;
  float default_framerate;
  
  float scale_x;
  
  gavl_packet_source_t * psrc;
  gavl_video_source_t * vsrc;

  /* Pangocairo stuff */

  cairo_t * cr;
  gavl_video_frame_t * frame;
  PangoLayout * layout;  
  PangoTabArray * tab_array;
  };

bg_text_renderer_t * bg_text_renderer_create()
  {
  bg_text_renderer_t * ret = calloc(1, sizeof(*ret));
  pthread_mutex_init(&ret->config_mutex,NULL);
  return ret;
  }

static void cleanup(bg_text_renderer_t * r)
  {
  if(r->layout)
    {
    g_object_unref(r->layout);
    r->layout = NULL;
    }
  if(r->cr)
    {
    cairo_destroy(r->cr);
    r->cr = NULL;
    }
  if(r->tab_array)
    {
    pango_tab_array_free(r->tab_array);
    r->tab_array = NULL;
    }
  if(r->frame)
    {
    bg_cairo_frame_destroy(r->frame);
    r->frame = NULL;
    }
  }

void bg_text_renderer_destroy(bg_text_renderer_t * r)
  {
  cleanup(r);

  if(r->font)
    free(r->font);
  if(r->font_file)
    free(r->font_file);
  
  pthread_mutex_destroy(&r->config_mutex);

  if(r->vsrc)
    gavl_video_source_destroy(r->vsrc);

  free(r);
  }

const bg_parameter_info_t * bg_text_renderer_get_parameters()
  {
  return parameters;
  }

void bg_text_renderer_set_parameter(void * data, const char * name,
                                    const gavl_value_t * val)
  {
  bg_text_renderer_t * r;
  r = (bg_text_renderer_t *)data;

  if(!name)
    return;

  pthread_mutex_lock(&r->config_mutex);

  /* General text renderer */
  if(!strcmp(name, "fontname"))
    {
    if(!r->font || strcmp(val->v.str, r->font))
      {
      r->font = gavl_strrep(r->font, val->v.str);
      r->font_changed = 1;
      }
    }
  else if(!strcmp(name, "font_size"))
    {
    if(r->font_size != val->v.d)
      {
      r->font_size = val->v.d;
      r->font_changed = 1;
      }
    }
  else if(!strcmp(name, "mode"))
    {
    if(!strcmp(val->v.str, "simple"))
      r->mode = MODE_SIMPLE;
    else if(!strcmp(val->v.str, "box"))
      r->mode = MODE_BOX;
    else if(!strcmp(val->v.str, "outline"))
      r->mode = MODE_OUTLINE;
    }
   
  /* */
  else if(!strcmp(name, "color"))
    {
#if 0
    fprintf(stderr, "Setting color: %f %f %f %f\n",
            val->v.color[0], 
            val->v.color[1], 
            val->v.color[2], 
            val->v.color[3]); 
#endif
    r->color[0] = val->v.color[0];
    r->color[1] = val->v.color[1];
    r->color[2] = val->v.color[2];
    r->color[3] = val->v.color[3];
    }
  else if(!strcmp(name, "border_color"))
    {
    r->border_color[0] = val->v.color[0];
    r->border_color[1] = val->v.color[1];
    r->border_color[2] = val->v.color[2];
    r->border_color[3] = val->v.color[3];
    }
  else if(!strcmp(name, "border_width"))
    {
    r->border_width = val->v.d;
    }
  else if(!strcmp(name, "box_color"))
    {
#if 0
    fprintf(stderr, "Setting box color: %f %f %f %f\n",
            val->v.color[0], 
            val->v.color[1], 
            val->v.color[2], 
            val->v.color[3]); 
#endif
    r->box_color[0] = val->v.color[0];
    r->box_color[1] = val->v.color[1];
    r->box_color[2] = val->v.color[2];
    r->box_color[3] = val->v.color[3];
    }
  else if(!strcmp(name, "box_radius"))
    {
    r->box_radius = val->v.d;
    }
  else if(!strcmp(name, "box_padding"))
    {
    r->box_padding = val->v.d;
    }
  else if(!strcmp(name, "justify_h"))
    {
    if(!strcmp(val->v.str, "left"))
      r->justify_h = JUSTIFY_LEFT;
    else if(!strcmp(val->v.str, "right"))
      r->justify_h = JUSTIFY_RIGHT;
    else if(!strcmp(val->v.str, "center"))
      r->justify_h = JUSTIFY_CENTER;
    }
  else if(!strcmp(name, "justify_box_h"))
    {
    if(!strcmp(val->v.str, "left"))
      r->justify_box_h = JUSTIFY_LEFT;
    else if(!strcmp(val->v.str, "right"))
      r->justify_box_h = JUSTIFY_RIGHT;
    else if(!strcmp(val->v.str, "center"))
      r->justify_box_h = JUSTIFY_CENTER;
    }
  else if(!strcmp(name, "justify_v"))
    {
    if(!strcmp(val->v.str, "top"))
      r->justify_v = JUSTIFY_TOP;
    else if(!strcmp(val->v.str, "bottom"))
      r->justify_v = JUSTIFY_BOTTOM;
    else if(!strcmp(val->v.str, "center"))
      r->justify_v = JUSTIFY_CENTER;
    }
  else if(!strcmp(name, "border_left"))
    {
    r->border_left = val->v.i;
    }
  else if(!strcmp(name, "border_right"))
    {
    r->border_right = val->v.i;
    }
  else if(!strcmp(name, "border_top"))
    {
    r->border_top = val->v.i;
    }
  else if(!strcmp(name, "border_bottom"))
    {
    r->border_bottom = val->v.i;
    }
  else if(!strcmp(name, "ignore_linebreaks"))
    {
    r->ignore_linebreaks = val->v.i;
    }
  else if(!strcmp(name, "default_width"))
    {
    r->default_width = val->v.i;
    }
  else if(!strcmp(name, "default_height"))
    {
    r->default_height = val->v.i;
    }
  else if(!strcmp(name, "default_framerate"))
    {
    r->default_framerate = val->v.d;
    }
  r->config_changed = 1;
  pthread_mutex_unlock(&r->config_mutex);
  }

static
void init_nolock(bg_text_renderer_t * r)
  {
  double size;
  int size_is_absolute;
  PangoFontDescription * desc;
  double img_w, img_h;
  
  cleanup(r);

  /* Copy formats */

  // gavl_video_format_copy(&r->overlay_format, &r->frame_format);
  
  if(!r->overlay_format.image_width || !r->overlay_format.image_height)
    {
    r->overlay_format.image_width = r->frame_format.image_width;
    r->overlay_format.image_height = r->frame_format.image_height;
    r->overlay_format.frame_width = r->frame_format.frame_width;
    r->overlay_format.frame_height = r->frame_format.frame_height;
    r->overlay_format.pixel_width = r->frame_format.pixel_width;
    r->overlay_format.pixel_height = r->frame_format.pixel_height;
    }
  r->overlay_format.pixelformat = GAVL_RGBA_32;

  r->overlay_format.orientation = r->frame_format.orientation;
  
  if(!r->overlay_format.timescale)
    {
    r->overlay_format.timescale      = r->frame_format.timescale;
    r->overlay_format.frame_duration = r->frame_format.frame_duration;
    r->overlay_format.framerate_mode = r->frame_format.framerate_mode;
    }
  
  /* Decide about overlay format */

  gavl_pixelformat_chroma_sub(r->frame_format.pixelformat,
                              &r->sub_h, &r->sub_v);

  if(!r->sub_h)
    r->sub_h = 1;
  if(!r->sub_v)
    r->sub_v = 1;
  
  /* */

  
  r->frame = bg_cairo_frame_create(&r->overlay_format);
  
  /* Create drawing context */
  r->cr = bg_cairo_create(&r->overlay_format, r->frame);

  r->scale_x = (double)r->overlay_format.pixel_height / r->overlay_format.pixel_width;
  
  gavl_video_format_copy(&r->fmt, &r->overlay_format);
  r->fmt.image_width = (int)((float)r->overlay_format.image_width / r->scale_x);
  
  cairo_scale(r->cr, r->scale_x, 1.0);

  cairo_set_line_width(r->cr, r->border_width);
  cairo_set_line_join(r->cr, CAIRO_LINE_JOIN_ROUND);

  if(gavl_image_orientation_is_transposed(r->fmt.orientation))
    {
    img_w = r->fmt.image_height;
    img_h = r->fmt.image_width;
    }
  else
    {
    img_h = r->fmt.image_height;
    img_w = r->fmt.image_width;
    }
  
  //  fprintf(stderr, "Scale factor: %f\n", r->scale_x);
  
  /* Create textrenderer */
  r->layout = pango_cairo_create_layout(r->cr);
  
  if(r->ignore_linebreaks)
    pango_layout_set_single_paragraph_mode(r->layout, TRUE);
  
  switch(r->justify_h)
    {
    case JUSTIFY_LEFT:
      pango_layout_set_alignment(r->layout, PANGO_ALIGN_LEFT);
      break;
    case JUSTIFY_CENTER:
      pango_layout_set_alignment(r->layout, PANGO_ALIGN_CENTER);
      break;
    case JUSTIFY_RIGHT:
      pango_layout_set_alignment(r->layout, PANGO_ALIGN_RIGHT);
      break;
    }

  pango_layout_set_width(r->layout, PANGO_SCALE *
                         (img_w - r->border_left - r->border_right - 2 * r->box_padding));
  
  desc = pango_font_description_from_string(r->font);

  size_is_absolute = pango_font_description_get_size_is_absolute(desc);
  size = pango_font_description_get_size(desc);
  
  size *= img_h / 480.0;
  
  r->tab_array = pango_tab_array_new_with_positions(1, (size_is_absolute ? TRUE : FALSE),
                                                    PANGO_TAB_LEFT, (size * 2));
  
  if(size_is_absolute)
    pango_font_description_set_absolute_size(desc, size);
  else
    pango_font_description_set_size(desc, (int)size);
  
  pango_layout_set_font_description(r->layout, desc);

  pango_layout_set_tabs(r->layout, r->tab_array);
  
  pango_font_description_free(desc);
  r->config_changed = 0;
  }

void bg_text_renderer_get_frame_format(bg_text_renderer_t * r,
                                       gavl_video_format_t * frame_format)
  {
  gavl_video_format_copy(frame_format, &r->frame_format);
  }

gavl_video_frame_t * bg_text_renderer_render(bg_text_renderer_t * r, const char * string)
  {
  PangoRectangle rect;
  gavl_rectangle_f_t box;
  gavl_rectangle_f_t pango_rect;
  gavl_overlay_t * ovl;
  float transp[4] = { 0.0, 0.0, 0.0, 0.0 };
  //  float transp[4] = { 1.0, 0.0, 0.0, 1.0 };
  double matrix[2][3];
  cairo_matrix_t cairo_matrix;

  double coords1[2];
  double coords2[2];
  /* Transposed */
  int image_w;
  int image_h;
  int transposed;

  //  pango_matrix_t pm;
  
  /* Create orientation matrix */
  gavl_set_orient_matrix(r->fmt.orientation, matrix);
      
  
  /* Flip y axis */
  //  cairo_scale(r->cr, 1.0, -1.0);
  
  
  //  fprintf(stderr, "bg_text_renderer_render\n");

  transposed = gavl_image_orientation_is_transposed(r->fmt.orientation);
  
  if(transposed)
    {
    image_h = r->fmt.image_width;
    image_w = r->fmt.image_height;
    }
  else
    {
    image_w = r->fmt.image_width;
    image_h = r->fmt.image_height;
    }

  // cairo_translate(r->cr, -image_w/2, -image_h/2);

  
  pthread_mutex_lock(&r->config_mutex);
  
  if(r->config_changed)
    init_nolock(r);

  cairo_save(r->cr);
  cairo_translate(r->cr, r->fmt.image_width/2, r->fmt.image_height/2);

  cairo_scale(r->cr, 1.0, -1.0);
  
  /* Apply orientation */
  cairo_matrix_init(&cairo_matrix,
                    matrix[0][0], matrix[1][0],
                    matrix[0][1], matrix[1][1],
                    matrix[0][2], matrix[1][2]);
  cairo_transform(r->cr, &cairo_matrix);

  cairo_scale(r->cr, 1.0, -1.0);

  /* */
  
  ovl = r->frame; // Maybe changed by init_nolock()
  
  /* Render stuff */
  bg_cairo_surface_fill_rgba(NULL, r->cr, transp);

  pango_layout_set_markup(r->layout, string, -1);
  pango_cairo_update_layout(r->cr, r->layout);

  pango_layout_get_extents(r->layout, NULL, &rect);
  pango_rect.w = (float)(rect.width) / PANGO_SCALE;
  pango_rect.h = (float)(rect.height) / PANGO_SCALE;
  pango_rect.x = (float)(rect.x) / PANGO_SCALE;
  pango_rect.y = (float)(rect.y) / PANGO_SCALE;
  
  //  fprintf(stderr, "Pango rect: %dx%d+%d+%d\n", rect.width, rect.height, rect.x, rect.y);
  //  fprintf(stderr, "Pango rect: %.2fx%.2f+%.2f+%.2f\n",
  //          pango_rect.w, pango_rect.h, pango_rect.x, pango_rect.y);

  box.x = 0.0;
  box.y = 0.0;
  box.h = pango_rect.h + 2 * r->box_padding;
  box.w = pango_rect.w  + 2 * r->box_padding;
  
#if 1 
  switch(r->justify_box_h)
    {
    case JUSTIFY_LEFT:
      box.x = - image_w * 0.5 + r->border_left;
      break;
    case JUSTIFY_CENTER:
      box.x = - box.w / 2.0;
      break;
    case JUSTIFY_RIGHT:
      box.x = image_w * 0.5 - r->border_left - box.w;
      break;
    }
  
  switch(r->justify_v)
    {
    case JUSTIFY_TOP:
      box.y = - image_h / 2.0 + r->border_top;
      break;
    case JUSTIFY_CENTER:
      box.y = - box.h / 2.0;
      break;
    case JUSTIFY_BOTTOM:
      box.y = image_h / 2.0 - box.h - r->border_bottom;
      break;
    }
#endif
  
  /* Draw box */
  
  if(r->mode == MODE_BOX)
    {
    bg_cairo_make_rounded_box(r->cr, &box, r->box_radius);
    cairo_set_source_rgba(r->cr,
                          r->box_color[0],
                          r->box_color[1],
                          r->box_color[2],
                          r->box_color[3]);
    cairo_fill(r->cr);
    }
  
  /* Draw text */
  
  // fprintf(stderr, "moveto: %f %f\n", x, y);
  cairo_move_to(r->cr, box.x + r->box_padding - pango_rect.x, box.y + r->box_padding);
  
  cairo_set_source_rgba(r->cr,
                        r->color[0],
                        r->color[1],
                        r->color[2],
                        r->color[3]);
  pango_cairo_show_layout(r->cr, r->layout);
  
  /* Draw outline */
  if(r->mode == MODE_OUTLINE)
    {
    pango_cairo_layout_path(r->cr, r->layout);
    cairo_set_source_rgba(r->cr,
                          r->border_color[0],
                          r->border_color[1],
                          r->border_color[2],
                          r->border_color[3]);
    cairo_stroke(r->cr);
    }

  /* */

    /* Get source rectangle */
  //  fprintf(stderr, "Box: %fx%f+%f+%f\n", box.w, box.h, box.x, box.y);

  coords1[0] = box.x;
  coords1[1] = box.y;

  coords2[0] = box.x + box.w;
  coords2[1] = box.y + box.h;
  
  cairo_user_to_device(r->cr, &coords1[0], &coords1[1]);
  cairo_user_to_device(r->cr, &coords2[0], &coords2[1]);

  if(coords1[0] > coords2[0])
    {
    double swp = coords1[0];
    coords1[0] = coords2[0];
    coords2[0] = swp;
    }

  if(coords1[1] > coords2[1])
    {
    double swp = coords1[1];
    coords1[1] = coords2[1];
    coords2[1] = swp;
    }
  
  //  fprintf(stderr, "Box (src): %f,%f -> %f,%f\n",
  //          coords1[0], coords1[1], coords2[0], coords2[1]);

  coords1[0] = floor(coords1[0]);
  coords1[1] = floor(coords1[1]);

  coords2[0] = ceil(coords2[0]);
  coords2[1] = ceil(coords2[1]);

  
  ovl->src_rect.x = (int)coords1[0];
  ovl->src_rect.y = (int)coords1[1];
  
  ovl->src_rect.w = (int)(coords2[0] - coords1[0]);
  ovl->src_rect.h = (int)(coords2[1] - coords1[1]);
  
  ovl->dst_x = ovl->src_rect.x;
  ovl->dst_y = ovl->src_rect.y;
  
  bg_cairo_frame_done(&r->overlay_format, ovl);
#if 0
  fprintf(stderr, "Got Overlay: dst: %d,%d\n", ovl->dst_x, ovl->dst_y);
  gavl_rectangle_i_dump(&ovl->src_rect);
  fprintf(stderr, "\n");
  if(0)
    {
    float color[4] = { 1.0, 1.0, 1.0, 0.5};
    gavl_video_frame_fill(ovl, &r->overlay_format, color);
    }
#endif
  
  cairo_restore(r->cr);
  
  pthread_mutex_unlock(&r->config_mutex);


  return r->frame;
  }

static gavl_source_status_t read_video(void * priv, gavl_video_frame_t ** frame)
  {
  gavl_packet_t * p = NULL;
  gavl_source_status_t st;
  bg_text_renderer_t * r = priv;
  
  if((st = gavl_packet_source_read_packet(r->psrc, &p)) != GAVL_SOURCE_OK)
    return st;

  *frame =
    bg_text_renderer_render(r, (char*)p->buf.buf); // Assume the text is properly zero padded);
                            
  (*frame)->timestamp = p->pts;
  (*frame)->duration = p->duration;
  return GAVL_SOURCE_OK;
  }

static void init_common(bg_text_renderer_t * r,
                        const gavl_video_format_t * frame_format,
                        gavl_video_format_t * overlay_format)
  {
  /* Free previous initialization */
  if(r->frame)
    cleanup(r);
  
  if(overlay_format)
    gavl_video_format_copy(&r->overlay_format, overlay_format);
  else
    memset(&r->overlay_format, 0, sizeof(r->overlay_format));
  
  if(frame_format)
    {
    gavl_video_format_copy(&r->frame_format, frame_format);
    }
  else
    {
    memset(&r->frame_format, 0, sizeof(r->frame_format));
    r->frame_format.image_width  = r->default_width;
    r->frame_format.image_height = r->default_height;

    r->frame_format.frame_width  = r->default_width;
    r->frame_format.frame_height = r->default_height;

    r->frame_format.pixel_width = 1;
    r->frame_format.pixel_height = 1;
    r->frame_format.pixelformat = GAVL_RGB_24;
    r->frame_format.timescale = (int)(r->default_framerate * 1000 + 0.5);
    r->frame_format.frame_duration = 1000;
    }
  
  init_nolock(r);
  
  gavl_video_format_copy(overlay_format, &r->overlay_format);

  }

gavl_video_source_t * bg_text_renderer_connect(bg_text_renderer_t * r,
                                               gavl_packet_source_t * src,
                                               const gavl_video_format_t * frame_format,
                                               gavl_video_format_t * overlay_format)
  {
  pthread_mutex_lock(&r->config_mutex);
  
  r->psrc = src;

  init_common(r, frame_format, overlay_format);
  pthread_mutex_unlock(&r->config_mutex);
  
  if(r->vsrc)
    gavl_video_source_destroy(r->vsrc);
  
  r->vsrc = gavl_video_source_create(read_video, r, 
                                     GAVL_SOURCE_SRC_DISCONTINUOUS | GAVL_SOURCE_SRC_ALLOC, 
                                     overlay_format);
  return r->vsrc;
  }

void bg_text_renderer_init(bg_text_renderer_t * r,
                           const gavl_video_format_t * frame_format,
                           gavl_video_format_t * overlay_format)
  {
  pthread_mutex_lock(&r->config_mutex);
  init_common(r, frame_format, overlay_format);
  pthread_mutex_unlock(&r->config_mutex);
  }
