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

#include <string.h>

#define GL_GLEXT_PROTOTYPES 1

#include <GL/gl.h>

#include <config.h>
#include <gavl/gavl.h>
#include <gavl/connectors.h>
#include <gavl/hw.h>
#include <gavl/hw_dmabuf.h>
#include <gavl/hw_gl.h>
#include <gavl/hw_egl.h>
#include <gavl/log.h>
#define LOG_DOMAIN "glvideo"

#include <gavl/state.h>

#include <gmerlin/state.h>


#include <gmerlin/glvideo.h>
#include <glvideo_private.h>

// #define BG_GL_DEBUG
// #define DUMP_IMAGE_FORMATS

#ifdef BG_GL_DEBUG
void enable_debug();
#endif


static image_format_t * add_image_format(bg_glvideo_t * g, int * formats_alloc)
  {
  image_format_t * ret;
  if(g->num_image_formats == *formats_alloc)
    {
    *formats_alloc += 32;
    g->image_formats = realloc(g->image_formats,
                               *formats_alloc * sizeof(*g->image_formats));
    memset(g->image_formats + g->num_image_formats, 0,
           (*formats_alloc - g->num_image_formats) *
           sizeof(*g->image_formats));
    }
  ret = g->image_formats + g->num_image_formats;
  g->num_image_formats++;
  return ret;
  }


static int need_scaling(image_format_t * imgfmt, const gavl_video_format_t * fmt)
  {
  if((imgfmt->max_dim > 0) &&
     ((fmt->image_width > imgfmt->max_dim) ||
      (fmt->image_height > imgfmt->max_dim)))
    return 1;
  else
    return 0;
  }

static int choose_image_format(port_t * port, const gavl_video_format_t * fmt, int src_flags)
  {
  int i;
  int penalty;
  int min_penalty = -1;
  int min_index = -1;

  gavl_hw_type_t type;
  if(port->idx)
    type = gavl_hw_ctx_get_type(port->g->hwctx);
  else
    type = port->g->hw_mask;
  
  /* Check for direct rendering */
  if(!(src_flags & GAVL_SOURCE_SRC_ALLOC) && (type & GAVL_HW_GLES_MASK))
    {
    for(i = 0; i < port->g->num_image_formats; i++)
      {
      if((port->g->image_formats[i].pfmt == fmt->pixelformat) &&
         (port->g->image_formats[i].mode == GAVL_HW_FRAME_MODE_MAP))
        return i;
      }
    }

  /* 2nd best option: glTexSubImage2D */
  
  for(i = 0; i < port->g->num_image_formats; i++)
    {
    if(need_scaling(&port->g->image_formats[i], fmt))
      continue;

    if(port->g->image_formats[i].pfmt != fmt->pixelformat)
      continue;
    
    if(!(port->g->image_formats[i].type & type))
      continue;

    return i;
    
    }

  if(type & GAVL_HW_GLES_MASK)
    {
    /* 3rd best option: Copy to mmapped dma frame */
    for(i = 0; i < port->g->num_image_formats; i++)
      {
      if(port->g->image_formats[i].type != GAVL_HW_DMABUFFER)
        continue;

      if(port->g->image_formats[i].pfmt != fmt->pixelformat)
        continue;

      if(port->g->image_formats[i].dma_flags & GAVL_DMABUF_FLAG_SHUFFLE)
        continue;
      return i;
      }
    
    /* 4th best option: Shuffle to mmapped dma frame */
    for(i = 0; i < port->g->num_image_formats; i++)
      {
      if(port->g->image_formats[i].type != GAVL_HW_DMABUFFER)
        continue;

      if(port->g->image_formats[i].pfmt != fmt->pixelformat)
        continue;
      
      return i;
      }
    }
  
  /* From here on we need either scale or do pixelformat conversion. We prefer the latter */  

  for(i = 0; i < port->g->num_image_formats; i++)
    {
    if(need_scaling(&port->g->image_formats[i], fmt))
      continue;

    penalty = gavl_pixelformat_conversion_penalty(fmt->pixelformat,
                                                  port->g->image_formats[i].pfmt);


    penalty <<= 1;
    if(port->g->image_formats[i].mode == GAVL_HW_FRAME_MODE_TRANSFER)
      penalty += 1;
    
    penalty <<= 1;
    if(port->g->image_formats[i].dma_flags & GAVL_DMABUF_FLAG_SHUFFLE)
      penalty += 1;
    
    
    if((min_index < 0) || (min_penalty > penalty))
      {
      min_penalty = penalty;
      min_index = i;
      }


    
    }
  if(min_index >= 0)
    return min_index;

  else
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Image format selection failed");
    return -1;
    }

  }

static void append_dma_format(bg_glvideo_t * g, gavl_pixelformat_t pfmt,
                              int dma_fourcc, int dma_flags, int * formats_alloc)
  {
  image_format_t * fmt;
  fmt = add_image_format(g, formats_alloc);
  fmt->pfmt = pfmt;
  fmt->dma_fourcc = dma_fourcc;
  fmt->dma_flags = dma_flags;
  fmt->type = GAVL_HW_DMABUFFER;
  if(fmt->dma_flags & GAVL_DMABUF_FLAG_SHUFFLE)
    fmt->mode = GAVL_HW_FRAME_MODE_TRANSFER;
  else
    fmt->mode = GAVL_HW_FRAME_MODE_MAP;
  }

#ifdef DUMP_IMAGE_FORMATS
static void dump_image_formats(image_format_t * fmts, int num)
  {
  int i;
  fprintf(stderr, "Got image formats\n");
  
  for(i = 0; i < num; i++)
    {
    fprintf(stderr, "  gavl: %s type: %s, mode: %s",
            gavl_pixelformat_to_string(fmts[i].pfmt),
            gavl_hw_type_to_string(fmts[i].type),
            (fmts[i].mode == GAVL_HW_FRAME_MODE_MAP) ? "map" : "transfer"
            );
    if(fmts[i].dma_flags & GAVL_DMABUF_FLAG_SHUFFLE)
      fprintf(stderr, " shuffle");
    if(fmts[i].dma_fourcc)
      fprintf(stderr, " drm_fourcc: %c%c%c%c",
              (fmts[i].dma_fourcc) & 0xff,
              (fmts[i].dma_fourcc >> 8) & 0xff,
              (fmts[i].dma_fourcc >> 16) & 0xff,
              (fmts[i].dma_fourcc >> 24) & 0xff
              );
    if(fmts[i].max_dim)
      fprintf(stderr, " max_dim: %d", fmts[i].max_dim);
    fprintf(stderr, " Type: %s", gavl_hw_type_to_string(fmts[i].type));
    fprintf(stderr, "\n");
    }
  }
#endif



static void get_image_formats(bg_glvideo_t * g)
  {
  int i = 0;
  gavl_pixelformat_t * pixelformats = NULL;
  uint32_t * dma_formats;
  
  int num = 0;
  
  image_format_t * fmt;
  int formats_alloc = 0;
  /*
   *  Standard openGL textures: We support
   *  the OpenGL RGB(A) formats as well as planar YUV formats
   *  (converted to RGB via the colormatrix)
   */

  pixelformats = gavl_gl_get_image_formats(g->hwctx_gl, &num);
  
  while(pixelformats[i] != GAVL_PIXELFORMAT_NONE)
    {
    GLenum type;
    
    if(gavl_get_gl_format(pixelformats[i], NULL, NULL, &type))
      {
      fmt = add_image_format(g, &formats_alloc);
      fmt->pfmt = pixelformats[i];
      /* GL ES supports only 8 bit/channel */
      
      if(type == GL_UNSIGNED_BYTE)
        {
        /* TODO Use GL or GL ES depending on whether we are
           on an embedded platform or not */
        fmt->type = g->hw_mask & (GAVL_HW_GLES_MASK|GAVL_HW_GL_MASK);
        fmt->max_dim = gavl_hw_egl_get_max_texture_size(g->hwctx_gles);

        }
      else
        {
        /* > 8 bytes per channel is only supported by GL */
        fmt->type = g->hw_mask & GAVL_HW_GL_MASK;
        fmt->max_dim = gavl_hw_egl_get_max_texture_size(g->hwctx_gl);
        }
      fmt->mode = GAVL_HW_FRAME_MODE_TRANSFER;
      }
    
    i++;
    }
  free(pixelformats);
  
  dma_formats = gavl_hw_ctx_egl_get_dma_import_formats(g->hwctx_gles);
  i = 0;
  while(dma_formats[i])
    {
    gavl_pixelformat_t pfmt;
    int dma_flags;
    
    pfmt = gavl_drm_pixelformat_from_fourcc(dma_formats[i], &dma_flags, NULL);
    switch(pfmt)
      {
      case GAVL_YUV_420_P:
        append_dma_format(g, pfmt, dma_formats[i], dma_flags, &formats_alloc);
        append_dma_format(g, GAVL_YUVJ_420_P, dma_formats[i], dma_flags, &formats_alloc);
        break;
      case GAVL_YUV_422_P:
        append_dma_format(g, pfmt, dma_formats[i], dma_flags, &formats_alloc);
        append_dma_format(g, GAVL_YUVJ_422_P, dma_formats[i], dma_flags, &formats_alloc);
        break;
      case GAVL_YUV_444_P:
        append_dma_format(g, pfmt, dma_formats[i], dma_flags, &formats_alloc);
        append_dma_format(g, GAVL_YUVJ_444_P, dma_formats[i], dma_flags, &formats_alloc);
        break;
      
      case GAVL_PIXELFORMAT_NONE:
        break;
      default:
        append_dma_format(g, pfmt, dma_formats[i], dma_flags, &formats_alloc);
        break;
      }
    i++;
    }
  free(dma_formats);
#ifdef DUMP_IMAGE_FORMATS
  dump_image_formats(g->image_formats, g->num_image_formats);
#endif
  }

void bg_glvideo_set_gl(bg_glvideo_t * g)
  {
  //  fprintf(stderr, "set GL... %p\n", g->hwctx);
  gavl_hw_egl_set_current(g->hwctx, g->surface);
  }

void bg_glvideo_unset_gl(bg_glvideo_t * g)
  {
  gavl_hw_egl_unset_current(g->hwctx);
  //  fprintf(stderr, "unset GL %p\n", g->hwctx);
  }

/* Allocate and create members */

static port_t * append_port(bg_glvideo_t * g)
  {
  port_t * ret;

  if(g->num_ports == g->ports_alloc)
    {
    g->ports_alloc += 4;
    g->ports = realloc(g->ports, g->ports_alloc * sizeof(*g->ports));
    memset(g->ports + g->num_ports, 0, (g->ports_alloc - g->num_ports) * sizeof(*g->ports));
    }
  
  g->ports[g->num_ports] = calloc(1, sizeof(*(g->ports[g->num_ports])));
  ret = g->ports[g->num_ports];

  ret->g = g;
  ret->idx = g->num_ports;
  
  
  g->num_ports++;
  return ret;
  }

static void port_ensure_buffers(port_t * port)
  {
  
  
  if(port->vbo)
    return;
  
  glGenVertexArrays(1, &port->vao);
  gavl_gl_log_error("glGenVertexArrays");  
  
  glBindVertexArray(port->vao);
  gavl_gl_log_error("glBindVertexArray");  
  
  glGenBuffers(1, &port->vbo);
  gavl_gl_log_error("glGenBuffers");  
  
  glBindBuffer(GL_ARRAY_BUFFER, port->vbo);  
  gavl_gl_log_error("glBindBuffer");  

  //  fprintf(stderr, "glBufferData %ld %p\n", sizeof(port->vertices), NULL);
  glBufferData(GL_ARRAY_BUFFER, sizeof(port->vertices), NULL, GL_DYNAMIC_DRAW);
  gavl_gl_log_error("glBufferData");  
  
  /* Index 0: Position */
  glVertexAttribPointer(ATTRIB_POS, 2, GL_FLOAT, GL_FALSE, sizeof(port->vertices[0]), (void*)0);
  gavl_gl_log_error("glVertexAttribPointer");  

  glEnableVertexAttribArray(ATTRIB_POS);
  gavl_gl_log_error("glEnableVertexAttribArray");  
  
  /* Index 1: Texture coordinates */
  glVertexAttribPointer(ATTRIB_TEX, 2, GL_FLOAT, GL_FALSE, sizeof(port->vertices[0]),
                        (void*)(sizeof(port->vertices[0].pos)));
  gavl_gl_log_error("glVertexAttribPointer");  
  glEnableVertexAttribArray(ATTRIB_TEX);
  gavl_gl_log_error("glEnableVertexAttribArray");  

  glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);  
  }

/*
 *  Triangle strip:
 *
 *  1---3
 *  |   |
 *  0---2
 */

#define POS_LL 0
#define POS_UL 1
#define POS_LR 2
#define POS_UR 3



void bg_glvideo_window_coords_to_position(bg_glvideo_t * g, int x, int y, double * ret)
  {
  ret[0] = (((double)x - g->dst_rect.x) / g->dst_rect.w) * g->src_rect.w;
  ret[1] = (((double)y - g->dst_rect.y) / g->dst_rect.h) * g->src_rect.h;
  }






/*
 *  We need to do things in the proper order so we don't screw up things
 *
 *  1. Video size + video SAR = display_aspect_ratio (origial)
 *  2. 
 */


#if 0
static void update_vertex_buffer(port_t * port)
  {
  gavl_rectangle_f_t sr_crop, dr_crop;
  float mat[2][2];
  gavl_video_format_t window_format;
  
  int transposed = 0;
  /*
   *  Update the vertex buffer: *all* scaling and positioning
   *  is done here
   */
  
  memset(&window_format, 0, sizeof(window_format));
  window_format.image_width = port->g->window_width;
  window_format.image_height = port->g->window_height;
  window_format.pixel_width = 1;
  window_format.pixel_height = 1;
  gavl_video_format_set_frame_size(&window_format, 1, 1);

  
  if(!port->idx)
    {
    gavl_video_format_t video_format;

    double src_aspect =
      (double)(port->fmt.image_width * port->fmt.pixel_width) /
      (double)(port->fmt.image_height * port->fmt.pixel_height);

    gavl_video_format_copy(&video_format, &port->fmt);
    video_format.orientation = (port->g->orientation + port->fmt.orientation) % 8;

    transposed = gavl_image_orientation_is_transposed(video_format.orientation);

    if(transposed)
      src_aspect = 1.0 / src_aspect;

    
    
    /* TODO: Handle orientation */
    set_orient_matrix(video_format.orientation, mat);
    
    
    /*    
    fprintf(stderr, "Window format\n");
    gavl_video_format_dump(&window_format);
    
    fprintf(stderr, "Video format\n");
    gavl_video_format_dump(&port->fmt);
    */
    //  gavl_video_format_t video_format_n;
    
    gavl_rectangle_f_set_all(&port->g->src_rect, &port->fmt);
    
    gavl_rectangle_fit_aspect_f(&port->g->dst_rect,
                                &port->fmt,
                                &port->g->src_rect,
                                &window_format,
                                port->g->zoom_factor,
                                port->g->squeeze_factor, 1);
    
    
    gavl_rectangle_f_copy(&dr_crop, &port->g->dst_rect);
    gavl_rectangle_f_copy(&sr_crop, &port->g->src_rect);

    gavl_rectangle_crop_to_format_scale_f(&sr_crop, &dr_crop,
                                          &port->fmt,
                                          &window_format);
    /*
    fprintf(stderr, "src_rect:\n");
    gavl_rectangle_f_dump(&sr_crop);
    fprintf(stderr, "\ndst_rect:\n");
    gavl_rectangle_f_dump(&dr_crop);
    fprintf(stderr, "\n");
    */
    
    
    /* vertex_coord = src_coord * src_to_vertex_scale + src_to_vertex_off */

    /* window_coord = dst_rect.x + (video_coord / src_rect.w) * dst_rect.w; */
    
    /* vertex_coord = 2.0 * window_coord / window_format.image_size - 1.0 */

    /* vertex_coord = 2.0 * (dst_rect.x + (video_coord / src_rect.w) * dst_rect.w) / window_format.image_size - 1.0 */

    // coords_video_to_vertex(port->g, double x, double y, float * ret);
    //    fprintf(stderr, "Blupp %d\n", port->vbo);
    
    }
  else
    {
    gavl_rectangle_f_t dst_rect_src;

    set_orient_matrix(GAVL_IMAGE_ORIENT_NORMAL, mat);
    
    gavl_rectangle_i_to_f(&sr_crop, &port->cur->src_rect);

    /* Destination rectangle in source coordinates */
    gavl_rectangle_i_to_f(&dst_rect_src, &port->cur->src_rect);
    dst_rect_src.x = port->cur->dst_x;
    dst_rect_src.y = port->cur->dst_y;
    
    coords_video_to_window(port->g, dst_rect_src.x,                dst_rect_src.y,                &dr_crop.x, &dr_crop.y);
    coords_video_to_window(port->g, dst_rect_src.x+dst_rect_src.w, dst_rect_src.y+dst_rect_src.h, &dr_crop.w, &dr_crop.h);
    dr_crop.w -= dr_crop.x;
    dr_crop.h -= dr_crop.y;

    gavl_rectangle_crop_to_format_scale_f(&sr_crop,
                                          &dr_crop,
                                          &port->fmt,
                                          &window_format);
    
    }
  
  coords_window_to_vertex(port->g, dr_crop.x,             dr_crop.y + dr_crop.h, port->vertices[POS_LL].pos);
  coords_window_to_vertex(port->g, dr_crop.x + dr_crop.w, dr_crop.y + dr_crop.h, port->vertices[POS_LR].pos);
  coords_window_to_vertex(port->g, dr_crop.x,             dr_crop.y,             port->vertices[POS_UL].pos);
  coords_window_to_vertex(port->g, dr_crop.x + dr_crop.w, dr_crop.y,             port->vertices[POS_UR].pos);
  
  coords_video_to_texture(port, sr_crop.x,             sr_crop.y + sr_crop.h, port->vertices[POS_LL].tex);
  coords_video_to_texture(port, sr_crop.x + sr_crop.w, sr_crop.y + sr_crop.h, port->vertices[POS_LR].tex);
  coords_video_to_texture(port, sr_crop.x,             sr_crop.y, port->vertices[POS_UL].tex);
  coords_video_to_texture(port, sr_crop.x + sr_crop.w, sr_crop.y, port->vertices[POS_UR].tex);
    

#if 0
  if(port->idx > 0)
    {
    fprintf(stderr, "LL: %f,%f %f,%f\n",
            port->vertices[POS_LL].pos[0],
            port->vertices[POS_LL].pos[1],
            port->vertices[POS_LL].tex[0],
            port->vertices[POS_LL].tex[1]);
    fprintf(stderr, "LR: %f,%f %f,%f\n",
            port->vertices[POS_LR].pos[0],
            port->vertices[POS_LR].pos[1],
            port->vertices[POS_LR].tex[0],
            port->vertices[POS_LR].tex[1]);
    fprintf(stderr, "UL: %f,%f %f,%f\n",
            port->vertices[POS_UL].pos[0],
            port->vertices[POS_UL].pos[1],
            port->vertices[POS_UL].tex[0],
            port->vertices[POS_UL].tex[1]);
    fprintf(stderr, "UR: %f,%f %f,%f\n",
            port->vertices[POS_UR].pos[0],
            port->vertices[POS_UR].pos[1],
            port->vertices[POS_UR].tex[0],
            port->vertices[POS_UR].tex[1]);
    }
#endif
  
  //  glBindVertexArray(port->vao);
  glBindBuffer(GL_ARRAY_BUFFER, port->vbo);  
  //  gavl_gl_log_error("glBindBuffer");  
  //  fprintf(stderr, "Bind buffer (upload) %d %d %d (cur: %p)\n", port->idx, port->vbo, port->vao, eglGetCurrentContext());
  
  glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(port->vertices), port->vertices);
  //  gavl_gl_log_error("glBufferSubData");  
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  
  }
#endif

static void draw_port(port_t * port)
  {
  int i;
  gavl_gl_frame_info_t * info;
  shader_program_t * p;

  /*
   *   (re)-Upload colormatrix. We do this even if there is no
   *   current frame
   */
  
  if(port->g->flags & FLAG_COLORMATRIX_CHANGED)
    bg_glvideo_update_colormatrix(port);
  
  if(!port->cur)
    return;

  //  if(port->idx > 0)
  //    fprintf(stderr, "Draw overlay\n");
  
  info = port->cur->storage;
  
  if(port->flags & PORT_USE_COLORMATRIX)
    p = &port->shader_cm;
  else
    p = &port->shader_nocm;

  /*
   *  Update vertex buffer
   */
  
  if((port->g->flags & (FLAG_COORDS_CHANGED)) ||
     (port->flags & PORT_OVERLAY_CHANGED))
    bg_glvideo_update_vertex_buffer(port);
    //    update_vertex_buffer(port);

  glUseProgram(p->program);
  //  gavl_gl_log_error("glUseProgram");  

  //  if(port->idx > 0)
  //    fprintf(stderr, "Blupp 2 %p %p %d\n", port->cur, port->texture, info->num_textures);
  
  /* Set texture(s) */
  for(i = 0; i < info->num_textures; i++)
    {
    glActiveTexture(GL_TEXTURE0 + i);
    glBindTexture(info->texture_target, info->textures[i]);

    //    fprintf(stderr, "Texture %d %d: %d -> %d\n",
    //            info->texture_target - GL_TEXTURE_2D, i, info->textures[i], p->frame_locations[i]);
    glUniform1i(p->frame_locations[i], i);
#if 0
    fprintf(stderr, "Bind texture %d %d %d\n",
            info->texture_target - GL_TEXTURE_2D,
            GL_TEXTURE0 + i, p->frame_locations[i]);
#endif

    }
  
  glBindVertexArray(port->vao);
  //    gavl_gl_log_error("glBindVertexArray");  
  
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  //  fprintf(stderr, "glDrawArrays %d\n", port->idx);
  
  //  glBindBuffer(GL_ARRAY_BUFFER, 0);  
  glBindVertexArray(0);
  
  port->flags &= ~PORT_OVERLAY_CHANGED;
  
  }

static gavl_sink_status_t func_texture_direct(port_t * port, gavl_video_frame_t * f)
  {
  port->cur = f;
  
  return GAVL_SINK_OK;
  }

static void port_ensure_texture(port_t * port)
  {
  if(!port->texture)
    {
    bg_glvideo_set_gl(port->g);
    port->texture = gavl_gl_create_frame(&port->fmt);
    port->texture->hwctx = port->g->hwctx;
    bg_glvideo_unset_gl(port->g);
    }
  }

static gavl_sink_status_t func_texture_transfer(port_t * port, gavl_video_frame_t * f)
  {
  port_ensure_texture(port);
  bg_glvideo_set_gl(port->g);
  gavl_gl_frame_to_hw(&port->fmt, port->texture, f);
  bg_glvideo_unset_gl(port->g);
  
  //  gavl_video_frame_ram_to_hw(port->texture, f);
  
  port->cur = port->texture;
  return GAVL_SINK_OK;
  }

static gavl_sink_status_t func_import(port_t * port, gavl_video_frame_t * f)
  {
  port->cur = NULL;
  
  if(gavl_hw_ctx_transfer_video_frame(f, port->g->hwctx, &port->cur,
                                      &port->fmt))
    return GAVL_SINK_OK;
  else
    return GAVL_SINK_ERROR;
  }

static gavl_sink_status_t func_import_dmabuf(port_t * port, gavl_video_frame_t * f)
  {
  gavl_video_frame_t * dma_frame = NULL;
  
  if(gavl_hw_ctx_transfer_video_frame(f, port->hwctx_dma, &dma_frame,
                                      &port->fmt) &&
     gavl_hw_ctx_transfer_video_frame(dma_frame, port->g->hwctx, &port->cur,
                                      &port->fmt))
    return GAVL_SINK_OK;
  else
    return GAVL_SINK_ERROR;
  }

static gavl_sink_status_t func_dmabuf_transfer(port_t * port, gavl_video_frame_t * f)
  {
  gavl_sink_status_t st = GAVL_SINK_ERROR;
  gavl_video_frame_t * dma_frame = gavl_hw_video_frame_get(port->hwctx_dma);
  
  if(gavl_video_frame_ram_to_hw(dma_frame, f))
    {
    if(!port->texture)
      {
      port->texture = gavl_hw_video_frame_create(port->g->hwctx, 0);
      if(gavl_hw_ctx_transfer_video_frame(dma_frame, port->g->hwctx, &port->texture,
                                          &port->fmt))
        {
        port->cur = port->texture;
        st = GAVL_SINK_OK;
        }
      else
        gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Transferring dmabuf to EGL failed");
      }
    else
      {
      gavl_video_frame_copy_metadata(port->texture, f);
      port->cur = port->texture;
      st = GAVL_SINK_OK;
      }
    }
  else
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Transferring frame to dmabuf failed");
    }
  
  if(dma_frame)
    gavl_hw_video_frame_unref(dma_frame);
  
  return st;
  }

static gavl_sink_status_t func_dmabuf_getframe(port_t * port, gavl_video_frame_t * f)
  {
  //  fprintf(stderr, "func_dmabuf_getframe %d %p %p\n", port->idx, f, port->dma_frame);
  
  if(gavl_hw_ctx_transfer_video_frame(f, port->g->hwctx, &port->cur,
                                      &port->fmt))
    return GAVL_SINK_OK;
  else
    return GAVL_SINK_ERROR;
  }

static gavl_video_frame_t * sink_get_func(void * priv)
  {
  port_t * port = priv;

  return port->dma_frame;
  }

/* (re)draw everything */
static void draw(bg_glvideo_t * g)
  {
  int i;
  /* Draw everything */

  bg_glvideo_set_gl(g);

  
  /* Setup Viewport */
  glViewport(0, 0, g->window_width, g->window_height);

  glDisable(GL_DEPTH_TEST);
  glClearColor(0.0, 0.0, 0.0, 1.0);
  glClear(GL_COLOR_BUFFER_BIT);

  for(i = 0; i < g->num_ports; i++)
    {
    if(i == 1)
      {
      glEnable(GL_BLEND);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      }

    draw_port(g->ports[i]);
    }

  glDisable(GL_BLEND);
  
  /* Wait for completion (the DMA buffer can be re-used after that */
  gavl_hw_egl_wait(g->hwctx);
  
  gavl_hw_egl_swap_buffers(g->hwctx);
  
  bg_glvideo_unset_gl(g);

  /* Clear the flags */
  g->flags &= ~(FLAG_COORDS_CHANGED | FLAG_COLORMATRIX_CHANGED);
  
  
  }

/* Redraw in response to an expose event */
void bg_glvideo_redraw(bg_glvideo_t * g)
  {
  if(g->flags & (FLAG_STILL | FLAG_PAUSED))
    draw(g);
  }

static gavl_sink_status_t sink_put_func_bg(void * priv, gavl_video_frame_t * f)
  {
  port_t * port = priv;

  if(port->set_frame)
    {
    if(port->set_frame(priv, f) != GAVL_SINK_OK)
      return GAVL_SINK_ERROR; 
    }
  else
    return GAVL_SINK_ERROR; 

  draw(port->g);
  return GAVL_SINK_OK;
  
  };

static gavl_sink_status_t sink_put_func_ovl(void * priv, gavl_video_frame_t * f)
  {
  port_t * port = priv;

  /* Clear frame */
  if(!f)
    {
    port->cur = NULL;
    if(port->idx > 0)
      port->flags |= PORT_OVERLAY_CHANGED;
    //    fprintf(stderr, "Clear overlay\n");
    bg_glvideo_redraw(port->g);
    return GAVL_SINK_OK;
    }

  if(port->set_frame)
    {
    if(port->set_frame(priv, f) != GAVL_SINK_OK)
      return GAVL_SINK_ERROR; 
    else
      {
      if(port->idx > 0)
        port->flags |= PORT_OVERLAY_CHANGED;

      bg_glvideo_redraw(port->g);
      
      return GAVL_SINK_OK;
      }
    }
  else
    return GAVL_SINK_ERROR; 
  }

static void port_init_dma(port_t * port)
  {
  if(!port->idx)
    {
    port->hwctx_dma = port->g->hwctx_dma;
    }
  else
    {
    port->hwctx_dma_priv = gavl_hw_ctx_create_dma();
    port->hwctx_dma = port->hwctx_dma_priv;
    }
  
  }


static int port_init(port_t * port, gavl_video_format_t * fmt, int src_flags)
  {
  int format_idx;
  
  bg_glvideo_init_colormatrix(port, GAVL_PIXELFORMAT_NONE);

  
  //  gavl_video_sink_get_func get_func = NULL;
  //  gavl_video_sink_put_func put_func = NULL;
  
  //  int planar = 0;
  
  if(port->idx && (fmt->pixelformat == GAVL_PIXELFORMAT_NONE))
    fmt->pixelformat = GAVL_RGBA_32;

  
  if(fmt->hwctx)
    {
    gavl_hw_type_t src_type;

    if(port->idx)
      {
      /* Error */
      gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN,
               "Importing HW frames is not supported for overlay streams. Downloading to RAM");
      fmt->hwctx = NULL;
      goto nohw;
      }
    
    /* Use GL frames directly */
    src_type = gavl_hw_ctx_get_type(fmt->hwctx);
    
    if(src_type & (GAVL_HW_GL_MASK | GAVL_HW_GLES_MASK))
      {
      port->g->hwctx = fmt->hwctx;
      port->mode = MODE_TEXTURE_DIRECT;
      port->set_frame = func_texture_direct;
      
      //      planar = gavl_pixelformat_num_planes(fmt->pixelformat) > 1 ? 1 : 0;
      bg_glvideo_init_colormatrix(port, fmt->pixelformat);
      
      goto found;
      }

    else if(gavl_hw_ctx_can_import(port->g->hwctx_gles, fmt->hwctx) ||
            gavl_hw_ctx_can_export(fmt->hwctx, port->g->hwctx_gles))
      {
      port->mode = MODE_IMPORT;
      port->set_frame = func_import;


      if(!port->idx)
        {
        port->g->hwctx = port->g->hwctx_gles;
        gavl_hw_ctx_set_video(port->g->hwctx, fmt, GAVL_HW_FRAME_MODE_IMPORT);
        }
      goto found;
      }

    
    /* Import frames from other APIs via DMA-buf. This implictly assumes that the pixelformat is supported
       for EGL import */
#ifdef HAVE_DRM  
    else if(gavl_hw_ctx_can_import(port->g->hwctx_gles, port->g->hwctx_dma) &&
            gavl_hw_ctx_can_export(fmt->hwctx,          port->g->hwctx_dma))
      {
      port->mode = MODE_IMPORT_DMABUF;
      port->set_frame = func_import_dmabuf;
      
      if(!port->idx)
        port->g->hwctx = port->g->hwctx_gles;

      port_init_dma(port);
      gavl_hw_ctx_set_video(port->hwctx_dma, fmt, GAVL_HW_FRAME_MODE_IMPORT);
 
      goto found;
      }
#endif
    
    /* Don't know what to do */
    gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Cannot use hardware frames of type %s, downloading to RAM",
             gavl_hw_type_to_string(gavl_hw_ctx_get_type(fmt->hwctx)));
    
    fmt->hwctx = NULL;
    goto nohw;
    }

  nohw:
  
  /* Find closest format */

  format_idx = choose_image_format(port, fmt, src_flags);

  if(port->g->image_formats[format_idx].pfmt != fmt->pixelformat)
    {
    gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Doing pixelformat conversion (%s -> %s) for port %d",
             gavl_pixelformat_to_short_string(fmt->pixelformat),
             gavl_pixelformat_to_short_string(port->g->image_formats[format_idx].pfmt), 
             port->idx);
    
    src_flags &= ~GAVL_SOURCE_SRC_ALLOC;
    fmt->pixelformat = port->g->image_formats[format_idx].pfmt;
    }

  /* TODO: Apply scaling */

  switch(port->g->image_formats[format_idx].mode)
    {
    case GAVL_HW_FRAME_MODE_MAP:
      {
      
      port->mode = MODE_DMABUF_GETFRAME;

      port->set_frame = func_dmabuf_getframe;

      
      port->g->hwctx = port->g->hwctx_gles;
      port_init_dma(port);
      
      if(!port->idx)
        gavl_hw_ctx_set_video(port->g->hwctx, fmt, GAVL_HW_FRAME_MODE_IMPORT);

      gavl_hw_ctx_set_video(port->hwctx_dma, fmt, GAVL_HW_FRAME_MODE_MAP);
      
      port->dma_frame = gavl_hw_video_frame_get(port->hwctx_dma);
      
      goto found;
      
      }
      break;
    case GAVL_HW_FRAME_MODE_TRANSFER:
      {
      if(port->g->image_formats[format_idx].type & port->g->hw_mask)
        {
        /* GL Texture (indirect) */

        /* TODO: Prefer GL or GL ES (if both are supported) depending on platform */
#if 0
        if(port->g->image_formats[format_idx].type & GAVL_HW_GLES_MASK)
          port->g->hwctx = port->g->hwctx_gles;
        else
          port->g->hwctx = port->g->hwctx_gl;
#else
        if(!port->idx)
          {
          if(port->g->image_formats[format_idx].type & GAVL_HW_GL_MASK)
            port->g->hwctx = port->g->hwctx_gl;
          else
            port->g->hwctx = port->g->hwctx_gles;
          }
#endif
        
        port->mode = MODE_TEXTURE_TRANSFER;

        port->set_frame = func_texture_transfer;

        if(!port->idx)
          gavl_hw_ctx_set_video(port->g->hwctx, fmt, GAVL_HW_FRAME_MODE_TRANSFER);

        bg_glvideo_init_colormatrix(port, fmt->pixelformat);
        }
      else
        {
        /* Transfer to DMA frames */
        //        fprintf(stderr, "Transfer to DMA frames\n");
        port->g->hwctx = port->g->hwctx_gles;

        port_init_dma(port);
        gavl_hw_ctx_set_video(port->hwctx_dma, fmt, GAVL_HW_FRAME_MODE_TRANSFER);

        port->mode = MODE_DMABUF_TRANSFER;
        port->set_frame = func_dmabuf_transfer;

        
        
        //        bg_glvideo_init_colormatrix(port, fmt->pixelformat);
        }
      
      break;
      } 
    case GAVL_HW_FRAME_MODE_IMPORT:
      break;
    }
  
  //  bg_glvideo_set_gl(port->g);
  //  bg_glvideo_unset_gl(port->g);
  
  found:

  gavl_video_format_copy(&port->fmt, fmt);

  if(!port->idx && (port->fmt.framerate_mode == GAVL_FRAMERATE_STILL))
    port->g->flags |= FLAG_STILL;
  
  bg_glvideo_set_gl(port->g);

#ifdef BG_GL_DEBUG
  if(!port->idx)
    enable_debug();
#endif

  
  bg_glvideo_create_shader(port, 0);
  bg_glvideo_create_shader(port, 1);
  
  port_ensure_buffers(port);
  
  bg_glvideo_unset_gl(port->g);
  
  if(!port->idx)
    port->g->surface = gavl_hw_ctx_egl_create_window_surface(port->g->hwctx, port->g->native_window);
  
  port->sink = gavl_video_sink_create(port->dma_frame ? sink_get_func : NULL,
                                      !port->idx ? sink_put_func_bg : sink_put_func_ovl, port,
                                      fmt);
  return 1;
  }

static void port_destroy(port_t * port)
  {
  if(port->sink)
    gavl_video_sink_destroy(port->sink);
  
  if(port->texture)
    gavl_video_frame_destroy(port->texture);
  
  if(port->hwctx_dma_priv)
    gavl_hw_ctx_destroy(port->hwctx_dma_priv);

  glDeleteBuffers(1, &port->vbo);
  glDeleteVertexArrays(1, &port->vao);
  
  free(port);
  }
  
bg_glvideo_t * bg_glvideo_create(int type_mask, void * native_display, void * native_window)
  {
  bg_glvideo_t * g = calloc(1, sizeof(*g));

  g->native_display = native_display;
  g->native_window = native_window;
  g->hw_mask = type_mask;
  //  g->surface = EGL_NONE;

  g->zoom_factor = 1.0;
  g->squeeze_factor = 0.0;
  g->hwctx_dma = gavl_hw_ctx_create_dma();
  
  return g;
  }

void bg_glvideo_set_window_size(bg_glvideo_t * g, int w, int h)
  {
  if((g->window_width == w) &&
     (g->window_height == h))
    return;

  g->window_width = w;
  g->window_height = h;

  g->flags |= FLAG_COORDS_CHANGED;
  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Window size changed %d %d", w, h);
  }

void bg_glvideo_destroy(bg_glvideo_t * g)
  {
  bg_glvideo_close(g);

  if(g->hwctx_gl)
    gavl_hw_ctx_destroy(g->hwctx_gl);
  if(g->hwctx_gles)
    gavl_hw_ctx_destroy(g->hwctx_gles);
  if(g->hwctx_dma)
    gavl_hw_ctx_destroy(g->hwctx_dma);

  if(g->image_formats)
    free(g->image_formats);

  
  free(g);
  }

static const int default_attributes_gles[] =
  {
    EGL_RED_SIZE,   8,
    EGL_GREEN_SIZE, 8,
    EGL_BLUE_SIZE,  8,
    EGL_ALPHA_SIZE, 8,
    EGL_CONFORMANT, EGL_OPENGL_ES3_BIT,
    EGL_NONE
  };

static const int default_attributes_gl[] =
  {
    EGL_RED_SIZE,   8,
    EGL_GREEN_SIZE, 8,
    EGL_BLUE_SIZE,  8,
    EGL_ALPHA_SIZE, 8,
    EGL_NONE
  };


static void init_gl(bg_glvideo_t * g)
  {
  uint32_t * fourccs;
  
  if(g->hwctx_gl)
    return;
  
  g->hwctx_gles =
    gavl_hw_ctx_create_egl(default_attributes_gles,
                           g->hw_mask & GAVL_HW_GLES_MASK,
                           g->native_display);
  
  g->hwctx_gl =
    gavl_hw_ctx_create_egl(default_attributes_gl,
                           g->hw_mask & GAVL_HW_GL_MASK,
                           g->native_display);

  /* Get supported video formats */
  get_image_formats(g);

  /* Set the supported fourccs */
  fourccs = gavl_hw_ctx_egl_get_dma_import_formats(g->hwctx_gles);
  gavl_hw_ctx_dma_set_supported_formats(g->hwctx_dma, fourccs);
  free(fourccs);
  }

gavl_video_sink_t *
bg_glvideo_open(bg_glvideo_t * g,
                gavl_video_format_t * fmt,
                int src_flags)
  {
  port_t * port;
  init_gl(g);

  
  port = append_port(g);
  if(!port_init(port, fmt, src_flags))
    {
    return NULL;
    }
  g->flags |= (FLAG_COORDS_CHANGED | FLAG_COLORMATRIX_CHANGED);

  
  return port->sink;
  }

gavl_video_sink_t * bg_glvideo_add_overlay_stream(bg_glvideo_t * g, gavl_video_format_t * fmt, int src_flags)
  {
  port_t * port;

  port = append_port(g);
  if(!port_init(port, fmt, src_flags))
    {
    return NULL;
    }
  return port->sink;
  }

bg_controllable_t * bg_glvideo_get_controllable(bg_glvideo_t * g)
  {
  return &g->ctrl;
  }

void bg_glvideo_close(bg_glvideo_t * g)
  {
  int i;
  g->orientation = 0;
  
  if(!g->ports)
    return;

  
  bg_glvideo_set_gl(g);
  for(i = 0; i < g->ports_alloc; i++)
    {
    if(g->ports[i])
      port_destroy(g->ports[i]);
    }
  bg_glvideo_unset_gl(g);

  if(g->surface)
    {
    gavl_hw_ctx_egl_destroy_surface(g->hwctx, g->surface);
    g->surface = EGL_NO_SURFACE;
    }

  free(g->ports);
  g->ports = NULL;
  g->num_ports = 0;
  g->ports_alloc = 0;
  
  if(g->hwctx_gl)
    gavl_hw_ctx_reset(g->hwctx_gl);
  if(g->hwctx_gles)
    gavl_hw_ctx_reset(g->hwctx_gles);
  if(g->hwctx_dma)
    gavl_hw_ctx_reset(g->hwctx_dma);
  }

/* Handle client message */

int bg_glvideo_handle_message(bg_glvideo_t * g, gavl_msg_t * msg)
  {
  switch(msg->NS)
    {
    case GAVL_MSG_NS_STATE:
      switch(msg->ID)
        {
        case GAVL_CMD_SET_STATE:
          {
          int last = 0;
          const char * ctx = NULL;
          const char * var = NULL;
          gavl_value_t val;
          int ret = 1;
          
          gavl_value_init(&val);

          gavl_msg_get_state(msg,
                             &last,
                             &ctx, &var, &val, NULL);
          if(!strcmp(ctx, BG_STATE_CTX_OV))
            {
            if(!strcmp(var, BG_STATE_OV_CONTRAST))
              {
              double val_f = 0;

              if(gavl_value_get_float(&val, &val_f))
                {
                g->contrast = val_f;
                g->flags |= FLAG_COLORMATRIX_CHANGED;
                }
              }
            else if(!strcmp(var, BG_STATE_OV_SATURATION))
              {
              double val_f = 0;

              if(gavl_value_get_float(&val, &val_f))
                {
                g->saturation = val_f;
                g->flags |= FLAG_COLORMATRIX_CHANGED;
                }
              }
            else if(!strcmp(var, BG_STATE_OV_BRIGHTNESS))
              {
              double val_f = 0;

              if(gavl_value_get_float(&val, &val_f))
                {
                g->brightness = val_f;
                g->flags |= FLAG_COLORMATRIX_CHANGED;
                }
              }
            else if(!strcmp(var, BG_STATE_OV_ZOOM))
              {
              double val_f = 0;

              if(gavl_value_get_float(&val, &val_f))
                {
                g->zoom_factor = val_f / 100.0;
                g->flags |= FLAG_COORDS_CHANGED;
                }
              }
            else if(!strcmp(var, BG_STATE_OV_SQUEEZE))
              {
              double val_f = 0;

              if(gavl_value_get_float(&val, &val_f))
                {
                g->squeeze_factor = val_f;
                g->flags |= FLAG_COORDS_CHANGED;
                }
              }
            else if(!strcmp(var, BG_STATE_OV_ORIENTATION))
              {
              int val_i = 0;

              if(gavl_value_get_int(&val, &val_i))
                {
                g->orientation = val_i;
                g->flags |= FLAG_COORDS_CHANGED;
                }
              }
            else if(!strcmp(var, BG_STATE_OV_PAUSED))
              {
              int val_i = 0;
              
              if(gavl_value_get_int(&val, &val_i) && val_i)
                g->flags |= FLAG_PAUSED;
              else
                g->flags &= ~FLAG_PAUSED;
              gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Setting paused mode: %d", !!(g->flags & FLAG_PAUSED));
              }
            else
              ret = 0;
            }
          else
            ret = 0;

          if(ret)
            bg_glvideo_redraw(g);
          
          gavl_value_free(&val);
          return ret;
          }
        }
      break;
    }
  return 0;
  }


/* Debug */

#ifdef BG_GL_DEBUG


static void debugCallback(GLenum source, GLenum type, GLuint id,
                   GLenum severity, GLsizei length,
                   const GLchar* message, const void* userParam) {
    // Quelle des Fehlers ausgeben
    const char* sourceStr;
    const char* typeStr;
    const char* severityStr;
    
    switch (source) {
        case GL_DEBUG_SOURCE_API: sourceStr = "API"; break;
        case GL_DEBUG_SOURCE_WINDOW_SYSTEM: sourceStr = "WINDOW SYSTEM"; break;
        case GL_DEBUG_SOURCE_SHADER_COMPILER: sourceStr = "SHADER COMPILER"; break;
        case GL_DEBUG_SOURCE_THIRD_PARTY: sourceStr = "THIRD PARTY"; break;
        case GL_DEBUG_SOURCE_APPLICATION: sourceStr = "APPLICATION"; break;
        case GL_DEBUG_SOURCE_OTHER: sourceStr = "OTHER"; break;
        default: sourceStr = "UNKNOWN"; break;
    }
    
    // Fehlertyp ausgeben
    switch (type) {
        case GL_DEBUG_TYPE_ERROR: typeStr = "ERROR"; break;
        case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: typeStr = "DEPRECATED BEHAVIOR"; break;
        case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR: typeStr = "UNDEFINED BEHAVIOR"; break;
        case GL_DEBUG_TYPE_PORTABILITY: typeStr = "PORTABILITY"; break;
        case GL_DEBUG_TYPE_PERFORMANCE: typeStr = "PERFORMANCE"; break;
        case GL_DEBUG_TYPE_OTHER: typeStr = "OTHER"; break;
        default: typeStr = "UNKNOWN"; break;
    }
    
    // Schweregrad des Fehlers ausgeben
    switch (severity) {
        case GL_DEBUG_SEVERITY_HIGH: severityStr = "HIGH"; break;
        case GL_DEBUG_SEVERITY_MEDIUM: severityStr = "MEDIUM"; break;
        case GL_DEBUG_SEVERITY_LOW: severityStr = "LOW"; break;
        case GL_DEBUG_SEVERITY_NOTIFICATION: severityStr = "NOTIFICATION"; break;
        default: severityStr = "UNKNOWN"; break;
    }
    
    printf("OpenGL DEBUG - Source: %s, Type: %s, Severity: %s, Message: %s\n", 
           sourceStr, typeStr, severityStr, message);
}

void enable_debug()
  {
  glEnable(GL_DEBUG_OUTPUT);
  glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS); // Synchrone Ausgabe für besseres Debugging
  glDebugMessageCallback(debugCallback, NULL);
        
  // Optional: Filtern nach bestimmten Nachrichtentypen (alle aktivieren)
  glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, NULL, GL_TRUE);
        
  printf("OpenGL Debug-Output wurde erfolgreich aktiviert.\n");
  }

#endif // BG_GL_DEBUG
