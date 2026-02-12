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



#include <config.h>
#include <gmerlin/plugin.h>
#include <gmerlin/translation.h>

#include <gavl/utils.h>
#include <gavl/log.h>
#define LOG_DOMAIN "testpic"

#include <gavl/hw.h>



#define FLAG_ALLOC (1<<0)
#define FLAG_HW    (1<<1)

typedef struct
  {
  gavl_dictionary_t mi;
  gavl_dictionary_t * track_info;

  gavl_video_format_t * fmt;

  bg_media_source_t ms;

  bg_controllable_t controllable;
  
  gavl_video_frame_t * frame;
  gavl_video_frame_t * frame_priv;

  int flags;
  int frames_displayed;
  
  gavl_hw_context_t * hwctx;  
  } input_t;

static bg_media_source_t * get_src_input(void * priv)
  {
  input_t * inp = priv;
  return &inp->ms;
  }

static gavl_source_status_t
read_video_func_alloc(void * data, gavl_video_frame_t ** fp)
  {
  input_t * inp = data;

  if(inp->fmt->framerate_mode == GAVL_FRAMERATE_STILL)
    {
    if(inp->frames_displayed >= 1)
      return GAVL_SOURCE_AGAIN;
    }

  if(inp->fmt->framerate_mode != GAVL_FRAMERATE_STILL)
    inp->frame->timestamp = (int64_t)inp->frames_displayed * inp->fmt->frame_duration;
  else
    inp->frame->timestamp = 0;
    
  inp->frames_displayed++;
  
  *fp = inp->frame;
  return GAVL_SOURCE_OK;
  }


static gavl_source_status_t
read_video_func_nonalloc(void * data, gavl_video_frame_t ** fp)
  {
  input_t * inp = data;

  if(inp->fmt->framerate_mode == GAVL_FRAMERATE_STILL)
    {
    if(inp->frames_displayed >= 1)
      return GAVL_SOURCE_AGAIN;
    }
  
  gavl_video_frame_copy(inp->fmt, *fp, inp->frame);
  gavl_video_frame_copy_metadata(*fp, inp->frame);

  if(inp->fmt->framerate_mode != GAVL_FRAMERATE_STILL)
    (*fp)->timestamp = (int64_t)inp->frames_displayed * inp->fmt->frame_duration;
  else
    (*fp)->timestamp = 0;
  
  inp->frames_displayed++;
  
  return GAVL_SOURCE_OK;
  }

static void set_video_hw_context_input(void * priv, gavl_hw_context_t * hwctx)
  {
  input_t * inp = priv;
  inp->hwctx = hwctx;
  }



static int set_track_input(void * priv)
  {
  input_t * inp = priv;
  
  bg_media_source_cleanup(&inp->ms);
  bg_media_source_init(&inp->ms);

  bg_media_source_set_from_track(&inp->ms, inp->track_info);

  return 1;
  }

static bg_controllable_t * get_controllable_input(void * priv)
  {
  input_t * inp = priv;
  return &inp->controllable;
  }

static void destroy_input(void* priv)
  {
  input_t * inp = priv;
  bg_controllable_cleanup(&inp->controllable);

  if(inp->frame_priv)
    gavl_video_frame_destroy(inp->frame_priv);
  
  free(priv);
  }

/* x = 0, y = 0: top left corner */

static void get_color(input_t * inp, float x, float y, float * ret)
  {
  int bar_index;

  float rgba[4];
  float blend[4];
  float blend_factor = y;
  
  int stripe_index = (int)(x * 16.0);

  if(stripe_index < 0)
    stripe_index = 0;
  if(stripe_index > 15)
    stripe_index = 15;

  bar_index = stripe_index / 2;
  
  switch(bar_index)
    {
    case 0:
      /* White */
      rgba[0] = 1.0;
      rgba[1] = 1.0;
      rgba[2] = 1.0;
      break;
    case 1:
      /* Yellow */
      rgba[0] = 1.0;
      rgba[1] = 1.0;
      rgba[2] = 0.0;
      break;
    case 2:
      /* Cyan */
      rgba[0] = 0.0;
      rgba[1] = 1.0;
      rgba[2] = 1.0;
      break;
    case 3:
      /* Green */
      rgba[0] = 0.0;
      rgba[1] = 1.0;
      rgba[2] = 0.0;
      break;
    case 4:
      /* Magenta */
      rgba[0] = 1.0;
      rgba[1] = 0.0;
      rgba[2] = 1.0;
      break;
    case 5:
      /* Red */
      rgba[0] = 1.0;
      rgba[1] = 0.0;
      rgba[2] = 0.0;
      break;
    case 6:
      /* Blue */
      rgba[0] = 0.0;
      rgba[1] = 0.0;
      rgba[2] = 1.0;
      break;
    case 7:
      /* Black */
      rgba[0] = 0.0;
      rgba[1] = 0.0;
      rgba[2] = 0.0;
      break;
    }
  rgba[3] = 1.0;

  if(gavl_pixelformat_has_alpha(inp->fmt->pixelformat))
    {
    if(y <= 0.5)
      {
      if(stripe_index & 1)
        {
        blend[0] = 0.0;
        blend[1] = 0.0;
        blend[2] = 0.0;
        blend[3] = 1.0;
        }
      else
        {
        blend[0] = 1.0;
        blend[1] = 1.0;
        blend[2] = 1.0;
        blend[3] = 1.0;
        }
      blend_factor *= 2.0;
      }
    else
      {
      blend_factor = (blend_factor - 0.5) * 2.0;

      blend[0] = rgba[0];
      blend[1] = rgba[1];
      blend[2] = rgba[2];
      blend[3] = 0.0;

      }
    }
  else
    {
    blend_factor = y;
    if(stripe_index & 1)
      {
      blend[0] = 0.0;
      blend[1] = 0.0;
      blend[2] = 0.0;
      blend[3] = 1.0;
      }
    else
      {
      blend[0] = 1.0;
      blend[1] = 1.0;
      blend[2] = 1.0;
      blend[3] = 1.0;
      }
    }

  ret[0] = blend_factor * blend[0] + (1.0 - blend_factor) * rgba[0];
  ret[1] = blend_factor * blend[1] + (1.0 - blend_factor) * rgba[1];
  ret[2] = blend_factor * blend[2] + (1.0 - blend_factor) * rgba[2];
  ret[3] = blend_factor * blend[3] + (1.0 - blend_factor) * rgba[3];
  
  }

static void rgb_to_yuv(const float * rgb, float * yuv)
  {
  // Y (Luminanz): 0.0 - 1.0
  yuv[0] = 0.299f * rgb[0] + 0.587f * rgb[1] + 0.114f * rgb[2];
  
  // Cb (Blau-Chrominanz): -0.5 bis +0.5
  yuv[1] = -0.168736f * rgb[0] - 0.331264f * rgb[1] + 0.5f * rgb[2];
  
  // Cr (Rot-Chrominanz): -0.5 bis +0.5
  yuv[2] = 0.5f * rgb[0] - 0.418688f * rgb[1] - 0.081312f * rgb[2];
  }

#define RGB_FLOAT_TO_8(val, dst)  dst=(uint8_t)((val)*255.0+0.5)
#define RGB_FLOAT_TO_16(val, dst) dst=(uint16_t)((val)*65535.0+0.5)

#define Y_FLOAT_TO_8(val,dst)  dst=(int)(val * 219.0+0.5) + 16
#define UV_FLOAT_TO_8(val,dst) dst=(int)(val * 224.0+0.5) + 128

#define Y_FLOAT_TO_16(val,dst)  dst=(int)(val * 219.0 * (float)0x100+0.5) + 0x1000
#define UV_FLOAT_TO_16(val,dst) dst=(int)(val * 224.0 * (float)0x100+0.5) + 0x8000

#define YJ_FLOAT_TO_8(val,dst)  dst=(int)(val * 255.0+0.5)
#define UVJ_FLOAT_TO_8(val,dst) dst=(int)(val * 255.0+0.5) + 128

#define PACK_8_TO_RGB16(r,g,b,pixel) pixel=((((((r<<5)&0xff00)|g)<<6)&0xfff00)|b)>>3;
#define PACK_8_TO_BGR16(r,g,b,pixel) pixel=((((((b<<5)&0xff00)|g)<<6)&0xfff00)|r)>>3;

#define PACK_8_TO_RGB15(r,g,b,pixel) pixel=((((((r<<5)&0xff00)|g)<<5)&0xfff00)|b)>>3;
#define PACK_8_TO_BGR15(r,g,b,pixel) pixel=((((((b<<5)&0xff00)|g)<<5)&0xfff00)|r)>>3;


static void paint_frame(input_t * inp)
  {
  int i;
  int j;
  int k;
  float x;
  float y;

  float rgba[4];
  float yuv[3];
  
  int sub_v = 1;
  int sub_h = 1;
  int advance;
  int num_planes = 1;
  
  void * ptrs[GAVL_MAX_PLANES];

  fprintf(stderr, "Paint frame: %d %d\n",
          (int)(inp->frame->planes[2] -  inp->frame->planes[1]),
          (int)(inp->frame->planes[1] -  inp->frame->planes[0]));
  
  if(gavl_pixelformat_is_planar(inp->fmt->pixelformat))
    {
    advance = gavl_pixelformat_bytes_per_component(inp->fmt->pixelformat);
    gavl_pixelformat_chroma_sub(inp->fmt->pixelformat, &sub_h, &sub_v);
    num_planes = gavl_pixelformat_num_planes(inp->fmt->pixelformat);
    }
  
  else
    {
    advance = gavl_pixelformat_bytes_per_pixel(inp->fmt->pixelformat);
    }
  
  for(i = 0; i < inp->fmt->image_height; i++)
    {
    y = (float)i / (float)(inp->fmt->image_height-1);
    
    for(j = 0; j < inp->fmt->image_width; j++)
      {
      x = (float)j / (float)(inp->fmt->image_width-1);

      /* TODO: Apply EXIF orientation */
      
      get_color(inp, x, y, rgba);

      ptrs[0] = inp->frame->planes[0] +
        inp->frame->strides[0] * i + advance * j;

      for(k = 1; k < num_planes; k++)
        {
        ptrs[k] = inp->frame->planes[k] +
          inp->frame->strides[k] * (i/sub_v) +
          advance * (j/sub_h);
        
        }
      
      switch(inp->fmt->pixelformat)
        {
        case GAVL_PIXELFORMAT_NONE:
          break;
        case GAVL_GRAY_8:
          {
          uint8_t * out = ptrs[0];
          rgb_to_yuv(rgba, yuv);
          RGB_FLOAT_TO_8(yuv[0], out[0]);
          }
          break;
        case GAVL_GRAY_16:
          {
          uint16_t * out = ptrs[0];
          rgb_to_yuv(rgba, yuv);
          RGB_FLOAT_TO_16(yuv[0], out[0]);
          }
          break;
        case GAVL_GRAY_FLOAT:
          {
          float * out = ptrs[0];
          rgb_to_yuv(rgba, yuv);
          *out = yuv[0];
          }
          break;
        case GAVL_GRAYA_16:
          {
          uint8_t * out = ptrs[0];
          rgb_to_yuv(rgba, yuv);
          RGB_FLOAT_TO_8(yuv[0], out[0]);
          RGB_FLOAT_TO_8(rgba[3], out[1]);
          }
          break;
        case GAVL_GRAYA_32:
          {
          uint16_t * out = ptrs[0];
          rgb_to_yuv(rgba, yuv);
          RGB_FLOAT_TO_16(yuv[0], out[0]);
          RGB_FLOAT_TO_16(rgba[3], out[1]);
          }
          break;
        case GAVL_GRAYA_FLOAT:
          {
          float * out = ptrs[0];
          rgb_to_yuv(rgba, yuv);
          out[0] = yuv[0];
          out[1] = rgba[3];
          }
          break;
        case GAVL_RGB_15:
          {
          uint16_t * out = ptrs[0];
          uint8_t r, g, b;
          RGB_FLOAT_TO_8(rgba[0], r);
          RGB_FLOAT_TO_8(rgba[1], g);
          RGB_FLOAT_TO_8(rgba[2], b);
          PACK_8_TO_RGB15(r,g,b,out[0]);
          }
          break;
        case GAVL_BGR_15:
          {
          uint16_t * out = ptrs[0];
          uint8_t r, g, b;
          RGB_FLOAT_TO_8(rgba[0], r);
          RGB_FLOAT_TO_8(rgba[1], g);
          RGB_FLOAT_TO_8(rgba[2], b);
          PACK_8_TO_BGR15(r,g,b,out[0]);
          }
          break;
        case GAVL_RGB_16:
          {
          uint16_t * out = ptrs[0];
          uint8_t r, g, b;
          RGB_FLOAT_TO_8(rgba[0], r);
          RGB_FLOAT_TO_8(rgba[1], g);
          RGB_FLOAT_TO_8(rgba[2], b);
          PACK_8_TO_RGB16(r,g,b,out[0]);
          }
          break;
        case GAVL_BGR_16:
          {
          uint16_t * out = ptrs[0];
          uint8_t r, g, b;
          RGB_FLOAT_TO_8(rgba[0], r);
          RGB_FLOAT_TO_8(rgba[1], g);
          RGB_FLOAT_TO_8(rgba[2], b);
          PACK_8_TO_BGR16(r,g,b,out[0]);
          }
          break;
        case GAVL_RGB_24:
        case GAVL_RGB_32:
          {
          uint8_t * out = ptrs[0];

          RGB_FLOAT_TO_8(rgba[0], out[0]);
          RGB_FLOAT_TO_8(rgba[1], out[1]);
          RGB_FLOAT_TO_8(rgba[2], out[2]);
          
          }
          break;
        case GAVL_BGR_24:
        case GAVL_BGR_32:
          {
          uint8_t * out = ptrs[0];
          RGB_FLOAT_TO_8(rgba[0], out[2]);
          RGB_FLOAT_TO_8(rgba[1], out[1]);
          RGB_FLOAT_TO_8(rgba[2], out[0]);
          }
          break;
        case GAVL_RGBA_32:
          {
          uint8_t * out = ptrs[0];
          RGB_FLOAT_TO_8(rgba[0], out[0]);
          RGB_FLOAT_TO_8(rgba[1], out[1]);
          RGB_FLOAT_TO_8(rgba[2], out[2]);
          RGB_FLOAT_TO_8(rgba[3], out[3]);
          }
          break;
        case GAVL_RGB_48:
          {
          uint16_t * out = ptrs[0];
          RGB_FLOAT_TO_16(rgba[0], out[0]);
          RGB_FLOAT_TO_16(rgba[1], out[1]);
          RGB_FLOAT_TO_16(rgba[2], out[2]);
          }
          break;
        case GAVL_RGBA_64:
          {
          uint16_t * out = ptrs[0];
          RGB_FLOAT_TO_16(rgba[0], out[0]);
          RGB_FLOAT_TO_16(rgba[1], out[1]);
          RGB_FLOAT_TO_16(rgba[2], out[2]);
          RGB_FLOAT_TO_16(rgba[3], out[3]);
          }
          break;
        case GAVL_RGB_FLOAT:
          {
          float * out = ptrs[0];
          out[0] = rgba[0];
          out[1] = rgba[1];
          out[2] = rgba[2];
          }
          break;
        case GAVL_RGBA_FLOAT:
          {
          float * out = ptrs[0];
          out[0] = rgba[0];
          out[1] = rgba[1];
          out[2] = rgba[2];
          out[3] = rgba[3];
          }
          break;
        case GAVL_YUY2:
          {
          uint8_t * out = ptrs[0];

          rgb_to_yuv(rgba, yuv);

          Y_FLOAT_TO_8(yuv[0], out[0]);

          if(!(j & 1)) // Even
            {
            UV_FLOAT_TO_8(yuv[1], out[1]);
            UV_FLOAT_TO_8(yuv[2], out[3]);
            }
          }
	  break;
        case GAVL_UYVY:
          {
          uint8_t * out = ptrs[0];

          rgb_to_yuv(rgba, yuv);

          Y_FLOAT_TO_8(yuv[0], out[1]);
          
          if(!(j & 1))
            {
            UV_FLOAT_TO_8(yuv[1], out[0]);
            UV_FLOAT_TO_8(yuv[2], out[2]);
            }
          }
          break;
        case GAVL_YUVA_32:
          {
          uint8_t * out = ptrs[0];
          rgb_to_yuv(rgba, yuv);
          Y_FLOAT_TO_8(yuv[0], out[0]);
          UV_FLOAT_TO_8(yuv[1], out[1]);
          UV_FLOAT_TO_8(yuv[2], out[2]);
          RGB_FLOAT_TO_8(rgba[3], out[3]);
          }
          break;
        case GAVL_YUVA_64:
          {
          uint16_t * out = ptrs[0];
          rgb_to_yuv(rgba, yuv);
          Y_FLOAT_TO_16(yuv[0], out[0]);
          UV_FLOAT_TO_16(yuv[1], out[1]);
          UV_FLOAT_TO_16(yuv[2], out[2]);
          RGB_FLOAT_TO_16(rgba[3], out[3]);
          }
          break;
        case GAVL_YUVA_FLOAT:
          {
          float * out = ptrs[0];
          rgb_to_yuv(rgba, yuv);
          out[0] = yuv[0];
          out[1] = yuv[1];
          out[2] = yuv[2];
          out[3] = rgba[3];
          }
          break;
        case GAVL_YUV_FLOAT:
          {
          float * out = ptrs[0];
          rgb_to_yuv(rgba, yuv);
          out[0] = yuv[0];
          out[1] = yuv[1];
          out[2] = yuv[2];
          }
          break;
        case GAVL_YUV_420_P:
        case GAVL_YUV_410_P:
        case GAVL_YUV_411_P:
        case GAVL_YUV_422_P:
        case GAVL_YUV_444_P:
          {
          uint8_t * out_y = ptrs[0];
          uint8_t * out_u = ptrs[1];
          uint8_t * out_v = ptrs[2];
          rgb_to_yuv(rgba, yuv);
          Y_FLOAT_TO_8(yuv[0], out_y[0]);
          UV_FLOAT_TO_8(yuv[1], out_u[0]);
          UV_FLOAT_TO_8(yuv[2], out_v[0]);
                    
          }
          break;
        case GAVL_YUV_422_P_16:
        case GAVL_YUV_444_P_16:
          {
          uint16_t * out_y = ptrs[0];
          uint16_t * out_u = ptrs[1];
          uint16_t * out_v = ptrs[2];
          rgb_to_yuv(rgba, yuv);
          Y_FLOAT_TO_16(yuv[0], out_y[0]);
          UV_FLOAT_TO_16(yuv[1], out_u[0]);
          UV_FLOAT_TO_16(yuv[2], out_v[0]);
          }
          break;
        case GAVL_YUVJ_420_P:
        case GAVL_YUVJ_422_P:
        case GAVL_YUVJ_444_P:
          {
          uint8_t * out_y = ptrs[0];
          uint8_t * out_u = ptrs[1];
          uint8_t * out_v = ptrs[2];
          rgb_to_yuv(rgba, yuv);
          YJ_FLOAT_TO_8(yuv[0], out_y[0]);
          UVJ_FLOAT_TO_8(yuv[1], out_u[0]);
          UVJ_FLOAT_TO_8(yuv[2], out_v[0]);
          break;
          }
        }
      
      }
    
    }
  
  }

static int handle_cmd_input(void * data, gavl_msg_t * msg)
  {
  input_t * inp = data;

  //  fprintf(stderr, "handle_cmd_input:\n");
  //  gavl_msg_dump(msg, 2);
  
  switch(msg->NS)
    {
    case GAVL_MSG_NS_SRC:
      switch(msg->ID)
        {
        case GAVL_CMD_SRC_START:
          {
          bg_media_source_stream_t * s;

          if(inp->flags & FLAG_HW)
            {
            if(inp->hwctx)
              {
              int i = 0;
              const gavl_pixelformat_t * fmts = gavl_hw_ctx_get_image_formats(inp->hwctx, GAVL_HW_FRAME_MODE_MAP);

              while(fmts[i] != GAVL_PIXELFORMAT_NONE)
                {
                if(fmts[i] == inp->fmt->pixelformat)
                  {
                  gavl_hw_ctx_set_video_creator(inp->hwctx, inp->fmt, GAVL_HW_FRAME_MODE_MAP);
                  inp->frame = gavl_hw_video_frame_get_write(inp->hwctx);
                  break;
                  }
                i++;
                }
              if(!inp->frame)
                gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Hardware surface not supported for %s",
                         gavl_pixelformat_to_string(inp->fmt->pixelformat));
      
              }
            else
              gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Hardware surface requested but no hardware context available");
            }

          if(!inp->frame)
            {
            inp->frame_priv = gavl_video_frame_create(inp->fmt);
            inp->frame = inp->frame_priv;
            }
  
          paint_frame(inp);

  
          s = bg_media_source_get_video_stream(&inp->ms, 0);

          if(inp->flags & FLAG_ALLOC)
            s->vsrc_priv = gavl_video_source_create(read_video_func_alloc, inp, GAVL_SOURCE_SRC_ALLOC, inp->fmt);
          else
            s->vsrc_priv = gavl_video_source_create(read_video_func_nonalloc, inp, 0, inp->fmt);

          s->vsrc = s->vsrc_priv;
  


          
          }
          break;
        case GAVL_CMD_SRC_SELECT_TRACK:
          {
          set_track_input(inp);
          }
          break;
          
          
        }
      break;
    }
  return 1;
  }


static void * create_input()
  {
  input_t * inp = calloc(1, sizeof(*inp));

  bg_controllable_init(&inp->controllable,
                       bg_msg_sink_create(handle_cmd_input, inp, 1),
                       bg_msg_hub_create(1));

  inp->track_info = gavl_append_track(&inp->mi, NULL);
  
  return inp;
  }

/*
 *  gmerlin-play testpic:///?w=1920&h=1080&rate=25.0&alloc=1&pfmt=rgb48
 */

static int open_input(void * priv, const char * filename)
  {
  gavl_dictionary_t * s;
  gavl_dictionary_t * tm;
  
  input_t * inp = priv;
  gavl_dictionary_t vars;
  const char * var;
  gavl_dictionary_init(&vars);
  
  /* Create stream (must be first) */

  s = gavl_track_append_video_stream(inp->track_info);
  tm = gavl_track_get_metadata_nc(inp->track_info);
  inp->fmt = gavl_stream_get_video_format_nc(s);

  /* Default format */
  inp->fmt->image_width = 1920;
  inp->fmt->image_height = 1080;
  inp->fmt->pixel_width = 1;
  inp->fmt->pixel_height = 1;
  inp->fmt->timescale = GAVL_TIME_SCALE;
  
  inp->fmt->framerate_mode = GAVL_FRAMERATE_STILL;
  inp->fmt->pixelformat = GAVL_YUV_420_P;
  
  gavl_url_get_vars_c(filename, &vars);

  if((var = gavl_dictionary_get_string(&vars, "w")))
    inp->fmt->image_width = atoi(var);
  if((var = gavl_dictionary_get_string(&vars, "h")))
    inp->fmt->image_height = atoi(var);
  if((var = gavl_dictionary_get_string(&vars, "pfmt")))
    inp->fmt->pixelformat = gavl_short_string_to_pixelformat(var);
  if((var = gavl_dictionary_get_string(&vars, "alloc")) && atoi(var))
    inp->flags |= FLAG_ALLOC;
  if((var = gavl_dictionary_get_string(&vars, "hw")) && atoi(var))
    inp->flags |= (FLAG_ALLOC|FLAG_HW);
  
  gavl_video_format_set_frame_size(inp->fmt, 16, 16);

  gavl_dictionary_set_string_nocopy(tm, GAVL_META_LABEL, gavl_sprintf("%dx%d-%s", inp->fmt->image_width, inp->fmt->image_height,
                                                                      gavl_pixelformat_to_short_string(inp->fmt->pixelformat)));

  
  gavl_track_append_msg_stream(inp->track_info, GAVL_META_STREAM_ID_MSG_PROGRAM);

  gavl_dictionary_set_int(tm, GAVL_META_CAN_PAUSE, 1);

  gavl_track_finalize(inp->track_info);

  fprintf(stderr, "Test picture format:\n");
  gavl_video_format_dump(inp->fmt);
  
  //  input_t * inp = priv;
  return 1;
  }

static gavl_dictionary_t * get_media_info_input(void * priv)
  {
  input_t * inp = priv;
  return &inp->mi;
  }

static void close_input(void * priv)
  {
  
  }

static char const * const protocols = "testpic";

static const char * get_protocols_input(void * priv)
  {
  return protocols;
  }

static void set_video_hw_context(void * priv, gavl_hw_context_t * hwctx)
  {
  input_t * inp = priv;
  inp->hwctx = hwctx;
  }


const bg_input_plugin_t the_plugin =
  {
    .common =
    {
      BG_LOCALE,
      .name =           "i_testpic",
      .long_name =      TRS("Test image generator"),
      .description =    TRS("Test picture generator, can be confiured at the commandline"),
      .type =           BG_PLUGIN_INPUT,
      .flags =          BG_PLUGIN_FILE,
      .priority =       5,
      .create =         create_input,
      .destroy =        destroy_input,
      .get_controllable =  get_controllable_input,
      .get_protocols =  get_protocols_input
      
    },
    .open =          open_input,

    //    .get_num_tracks = bg_avdec_get_num_tracks,
    .get_media_info = get_media_info_input,

    .set_video_hw_context = set_video_hw_context_input,
    
    .set_video_hw_context = set_video_hw_context,
    
    .get_src              = get_src_input,

    
    .close = close_input,
  };

/* Include this into all plugin modules exactly once
   to let the plugin loader obtain the API version */
BG_GET_PLUGIN_API_VERSION;
