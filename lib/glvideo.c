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

#define IGNORE_SHUFFLE

static void init_gl(bg_glvideo_t * g);

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

  //  fprintf(stderr, "Append port 1: %d\n", g->num_ports);
  
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

  //  if(g->num_ports == 1)
  //    fprintf(stderr, "Append port 2: %d\n", g->num_ports);
  
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

static void draw_port(port_t * port)
  {
  int i;
  gavl_gl_frame_info_t * info;
  shader_program_t * p;

  /*
   *   (re)-Upload colormatrix. We do this even if there is no
   *   current frame
   */

  /*
  if(port->idx > 0)
    fprintf(stderr, "Draw port %d %d %p\n", port->idx, !!(port->g->flags & (FLAG_COORDS_CHANGED)), port->cur);
  */
  
  if(!port->cur)
    return;

  if(port->flags & PORT_COLORMATRIX_CHANGED)
    {
    bg_glvideo_update_colormatrix(port);
    port->flags &= ~PORT_COLORMATRIX_CHANGED;
    }
  
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

  bg_glvideo_init_colormatrix_gl(port);

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

  /* For now we don't import anything but DMA buffers.
     If this changes, we probably need to adapt the following */
  if(f)
    {
    //    fprintf(stderr, "func_import: %p %p %d\n", f, f->hwctx, f->buf_idx);
    
    bg_glvideo_init_colormatrix_dmabuf(port, f);
    
    if(gavl_hw_ctx_transfer_video_frame(f, port->g->hwctx, &port->cur,
                                        &port->fmt))
      return GAVL_SINK_OK;
    else
      return GAVL_SINK_ERROR;
    }
  else
    return GAVL_SINK_OK;
  }


static gavl_sink_status_t func_dmabuf_transfer(port_t * port, gavl_video_frame_t * f)
  {
  gavl_sink_status_t st = GAVL_SINK_ERROR;
  gavl_video_frame_t * dma_frame = gavl_hw_video_frame_get_write(port->hwctx_dma);

  bg_glvideo_init_colormatrix_unity(port);
  
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
  bg_glvideo_init_colormatrix_dmabuf(port, f);
  
  if(f)
    {
    port->cur = port->texture;
    gavl_video_frame_copy_metadata(port->cur, f);
    }
  return GAVL_SINK_OK;
  
  //  fprintf(stderr, "func_dmabuf_getframe %d %p %p %p\n", port->idx, f, port->dma_frame, port->cur);
  
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
  //  glClearColor(0.0, 1.0, 0.0, 1.0);
  glClear(GL_COLOR_BUFFER_BIT);

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  
  for(i = 0; i < g->num_ports; i++)
    {
    draw_port(g->ports[i]);
    }

  glDisable(GL_BLEND);
  
  /* Wait for completion (the DMA buffer can be re-used after that */
  gavl_hw_egl_wait(g->hwctx);
  
  gavl_hw_egl_swap_buffers(g->hwctx);
  
  bg_glvideo_unset_gl(g);

  /* Clear the flags */
  g->flags &= ~(FLAG_COORDS_CHANGED);
  
  
  }

/* Redraw in response to an expose event or a changed overlay */
void bg_glvideo_redraw(bg_glvideo_t * g)
  {
  if((g->flags & FLAG_STARTED) &&
     (g->flags & (FLAG_STILL | FLAG_PAUSED)))
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

  port->g->flags |= FLAG_STARTED;

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
    //    fprintf(stderr, "Set overlay %p\n", f);
    
    if(port->set_frame(priv, f) != GAVL_SINK_OK)
      {
      //      fprintf(stderr, "Set overlay failed\n");
      return GAVL_SINK_ERROR;
      }
    else
      {
      //      fprintf(stderr, "Set overlay done %p\n", port->cur);
      
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
  port->hwctx_dma = gavl_hw_ctx_create_dma();

  }

static int port_init(port_t * port, gavl_video_format_t * fmt, int src_flags)
  {
  int penalty = -1;
  const gavl_dictionary_t * hw = NULL;
  const gavl_dictionary_t * hw_test = NULL;
  gavl_pixelformat_t pfmt = GAVL_PIXELFORMAT_NONE;
  port->flags |= PORT_COLORMATRIX_CHANGED;
  
  /* Default for overlays */
  if(port->idx && (fmt->pixelformat == GAVL_PIXELFORMAT_NONE))
    fmt->pixelformat = GAVL_RGBA_32;

  if(fmt->hwctx)
    {
    //    gavl_hw_type_t src_type;

    if(port->idx)
      {
      /* Error */

      /* Importing hardware surfaces works only for port zero because the rendering
         context can import only from one source context and that's hopefully the one from
         port zero */
      
      gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN,
               "Importing HW frames is not supported for overlay streams. Downloading to RAM");
      fmt->hwctx = NULL;
      goto nohw;
      }

    if(gavl_hw_ctx_can_import(port->g->hwctx_gles, fmt->hwctx))
      {
      port->mode = MODE_IMPORT;
      port->g->hwctx = port->g->hwctx_gles;
      goto found;
      }
    
    /* Don't know what to do */
    gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Cannot use hardware frames of type %s, downloading to RAM",
             gavl_hw_type_to_string(gavl_hw_ctx_get_type(fmt->hwctx)));
    
    fmt->hwctx = NULL;
    goto nohw;
    }

  nohw:

  if(port->idx && (port->g->hwctx == port->g->hwctx_gl))
    {
    if((hw = gavl_hw_buf_desc_supports_video_format(gavl_hw_ctx_get_transfer_formats(port->g->hwctx_gl), fmt)))
      {
      port->mode = MODE_TEXTURE_TRANSFER;
      goto found;
      }
    
    hw = gavl_hw_buf_desc_get_pixelformat_conversion(gavl_hw_ctx_get_transfer_formats(port->g->hwctx_gl),
                                                     fmt, &pfmt, &penalty);
    port->mode = MODE_TEXTURE_TRANSFER;
    }
  else
    {
    if((hw = gavl_hw_buf_desc_supports_video_format(&port->g->import_map_formats, fmt)))
      {
      port->mode = MODE_DMABUF_GETFRAME;
      goto found;
      }
    else if(!port->idx && (hw = gavl_hw_buf_desc_supports_video_format(&port->g->transfer_formats, fmt)))
      {
      port->mode = MODE_TEXTURE_TRANSFER;
      goto found;
      }
    else if(port->idx && (hw = gavl_hw_buf_desc_supports_video_format(gavl_hw_ctx_get_transfer_formats(port->g->hwctx), fmt)))
      {
      port->mode = MODE_TEXTURE_TRANSFER;
      goto found;
      }
    else if(!port->idx && (hw = gavl_hw_buf_desc_supports_video_format(&port->g->import_transfer_formats, fmt)))
      {
      port->mode = MODE_DMABUF_TRANSFER;
      goto found;
      }
    
    /* Seems the pixelformat is not supported directly */
    hw = gavl_hw_buf_desc_get_pixelformat_conversion(&port->g->import_map_formats,
                                                     fmt, &pfmt, &penalty);
    port->mode = MODE_DMABUF_GETFRAME;
    
    if(!port->idx && (hw_test = gavl_hw_buf_desc_get_pixelformat_conversion(&port->g->transfer_formats,
                                                                            fmt, &pfmt, &penalty)))
      {
      port->mode = MODE_TEXTURE_TRANSFER;
      hw = hw_test;
      }

    if(port->idx && (hw_test = gavl_hw_buf_desc_get_pixelformat_conversion(gavl_hw_ctx_get_transfer_formats(port->g->hwctx),
                                                                           fmt, &pfmt, &penalty)))
      {
      port->mode = MODE_TEXTURE_TRANSFER;
      hw = hw_test;
      }
    
    }

  if(pfmt != fmt->pixelformat)
    {
    gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Doing pixelformat conversion (%s -> %s) for port %d",
             gavl_pixelformat_to_short_string(fmt->pixelformat),
             gavl_pixelformat_to_short_string(pfmt), 
             port->idx);
    
    src_flags &= ~GAVL_SOURCE_SRC_ALLOC;
    fmt->pixelformat = pfmt;
    }
  
  found:

  if(!port->idx && hw)
    {
    int hwtype = 0;
    gavl_dictionary_get_int(hw, GAVL_HW_BUF_TYPE, &hwtype);
    if(hwtype == GAVL_HW_EGL_GL)
      port->g->hwctx = port->g->hwctx_gl;
    else
      port->g->hwctx = port->g->hwctx_gles;
    }
  
  switch(port->mode)
    {
    case MODE_DMABUF_GETFRAME:
      port->set_frame = func_dmabuf_getframe;
      port_init_dma(port);
      gavl_hw_ctx_set_map_formats(port->hwctx_dma, &port->g->import_map_formats);
      
      gavl_hw_ctx_set_video_creator(port->hwctx_dma, fmt, GAVL_HW_FRAME_MODE_MAP);
      
      port->dma_frame = gavl_hw_video_frame_get_write(port->hwctx_dma);
      
      port->texture = gavl_hw_video_frame_create(port->g->hwctx, 0);

      if(!gavl_hw_ctx_transfer_video_frame(port->dma_frame, port->g->hwctx, &port->texture, fmt))
        return 0;
      
      gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Port %d: Rendering into DMA buffers", port->idx);
      break;
    case MODE_DMABUF_TRANSFER:

      port_init_dma(port);
      gavl_hw_ctx_set_map_formats(port->hwctx_dma, &port->g->import_transfer_formats);
      gavl_hw_ctx_set_video_creator(port->hwctx_dma, fmt, GAVL_HW_FRAME_MODE_TRANSFER);
      
      port->set_frame = func_dmabuf_transfer;
      gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Port %d: Transferring video data into DMA buffers", port->idx);
      
      break;
    case MODE_TEXTURE_TRANSFER:
      /* GL Texture (indirect) */
      port->set_frame = func_texture_transfer;
#if 0
      if(!port->idx)
        gavl_hw_ctx_set_video_creator(port->g->hwctx, fmt, GAVL_HW_FRAME_MODE_TRANSFER);
#endif
      
      gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Port %d: Transferring video data into textures", port->idx);
      break;
    case MODE_IMPORT:
      port->set_frame = func_import;
      
      port->g->hwctx = port->g->hwctx_gles;
      gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Port %d: Importing %s surfaces",
               port->idx, gavl_hw_type_to_string(gavl_hw_ctx_get_type(fmt->hwctx)));
      gavl_hw_ctx_set_video_importer(port->g->hwctx, fmt->hwctx, fmt);
      break;
    }
  
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
  
  if(port->hwctx_dma)
    gavl_hw_ctx_destroy(port->hwctx_dma);

  glDeleteBuffers(1, &port->vbo);
  glDeleteVertexArrays(1, &port->vao);
  
  free(port);
  }
  
bg_glvideo_t * bg_glvideo_create(GLenum platform, void * native_display)
  {
  bg_glvideo_t * g = calloc(1, sizeof(*g));

  g->native_display = native_display;
  
  g->zoom_factor = 1.0;
  g->squeeze_factor = 0.0;
  g->platform = platform;
  
  init_gl(g);
  
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
  bg_glvideo_redraw(g);
  }

void bg_glvideo_destroy(bg_glvideo_t * g)
  {
  bg_glvideo_close(g);

  if(g->hwctx_gl)
    gavl_hw_ctx_destroy(g->hwctx_gl);
  if(g->hwctx_gles)
    gavl_hw_ctx_destroy(g->hwctx_gles);

  gavl_array_free(&g->import_map_formats);
  gavl_array_free(&g->transfer_formats);
  gavl_array_free(&g->import_transfer_formats);
  
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

static void get_buffer_formats(bg_glvideo_t * g)
  {
  const gavl_array_t * arr;

  const gavl_array_t * arr_fourcc;
  const gavl_dictionary_t * dict_src;

  
  /* Gather supported pixelformats */
  
  arr = gavl_hw_ctx_get_import_formats(g->hwctx_gles);
  if(arr &&
     (arr->num_entries == 1) &&
     (dict_src = gavl_value_get_dictionary(&arr->entries[0])) &&
     (arr_fourcc = gavl_dictionary_get_array(dict_src, GAVL_HW_BUF_DMA_FOURCC)))
    {
    int fourcc = 0;
    int i;
    gavl_dictionary_t * dict_map = gavl_hw_buf_desc_append(&g->import_map_formats, GAVL_HW_DMABUFFER);
    gavl_dictionary_t * dict_transfer = gavl_hw_buf_desc_append(&g->import_transfer_formats, GAVL_HW_DMABUFFER);
    
    for(i = 0; i < arr_fourcc->num_entries; i++)
      {
      if(gavl_value_get_int(&arr_fourcc->entries[i], &fourcc))
        {
        int flags;
        int pfmt = 0;

        if((pfmt = gavl_drm_pixelformat_from_fourcc(fourcc, &flags, NULL)) != GAVL_PIXELFORMAT_NONE)
          {
          if((flags & GAVL_DMABUF_FLAG_SHUFFLE) && !gavl_pixelformat_is_rgb(pfmt))
            {
            /* Transfer to DMA buffers */
            gavl_hw_buf_desc_append_format(dict_transfer, GAVL_HW_BUF_PIXELFORMAT, pfmt);
            gavl_hw_buf_desc_append_format(dict_transfer, GAVL_HW_BUF_DMA_FOURCC,  fourcc);
            
            }
          else
            {
            /* Direct rendering DMA buffers */
            gavl_hw_buf_desc_append_format(dict_map, GAVL_HW_BUF_PIXELFORMAT, pfmt);
            gavl_hw_buf_desc_append_format(dict_map, GAVL_HW_BUF_DMA_FOURCC,  fourcc);
            }
          }
        }
      }
    }

  gavl_array_splice_array(&g->transfer_formats, -1, 0, gavl_hw_ctx_get_transfer_formats(g->hwctx_gles));
  gavl_array_splice_array(&g->transfer_formats, -1, 0, gavl_hw_ctx_get_transfer_formats(g->hwctx_gl));

#if 0
  fprintf(stderr, "Got pixelformats (import)\n");
  gavl_hw_buffer_formats_dump(&g->import_map_formats);

  fprintf(stderr, "Got pixelformats (transfer to texture)\n");
  gavl_hw_buffer_formats_dump(&g->transfer_formats);
  
  fprintf(stderr, "Got pixelformats (transfer to DMA buffer)\n");
  gavl_hw_buffer_formats_dump(&g->import_transfer_formats);
  
#endif
  }

  
static void init_gl(bg_glvideo_t * g)
  {
  
  if(g->hwctx_gl)
    return;
  
  g->hwctx_gles =
    gavl_hw_ctx_create_egl(g->platform,
                           default_attributes_gles,
                           GAVL_HW_EGL_GLES,
                           g->native_display);
  
  g->hwctx_gl =
    gavl_hw_ctx_create_egl(g->platform,
                           default_attributes_gl,
                           GAVL_HW_EGL_GL,
                           g->native_display);

  get_buffer_formats(g);
  }

gavl_video_sink_t *
bg_glvideo_open(bg_glvideo_t * g,
                gavl_video_format_t * fmt,
                int src_flags, EGLSurface window_surface)
  {
  port_t * port;

  g->native_window = window_surface;
  
  port = append_port(g);
  if(!port_init(port, fmt, src_flags))
    {
    return NULL;
    }
  g->flags |= (FLAG_COORDS_CHANGED | FLAG_OPEN);
  g->flags &= ~FLAG_STARTED;
  
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

  g->flags &= ~FLAG_OPEN;

  }

const gavl_array_t * bg_glvideo_get_import_formats(bg_glvideo_t * g)
  {
  return &g->import_map_formats;
  }


/* Handle client message */

static void cmatrix_changed(bg_glvideo_t * g)
  {
  int i;
  for(i = 0; i < g->num_ports; i++)
    g->ports[i]->flags |= PORT_COLORMATRIX_CHANGED;
  }

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
                cmatrix_changed(g);
                }
              }
            else if(!strcmp(var, BG_STATE_OV_SATURATION))
              {
              double val_f = 0;

              if(gavl_value_get_float(&val, &val_f))
                {
                g->saturation = val_f;
                cmatrix_changed(g);
                }
              }
            else if(!strcmp(var, BG_STATE_OV_BRIGHTNESS))
              {
              double val_f = 0;

              if(gavl_value_get_float(&val, &val_f))
                {
                g->brightness = val_f;
                cmatrix_changed(g);
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

int bg_glvideo_port_has_dma(port_t * port)
  {
  switch(port->mode)
    {
    case MODE_TEXTURE_TRANSFER: // Copy frames from RAM to OpenGL Texture
      return 0;
      break;
    case MODE_IMPORT: // Import directly
    case MODE_DMABUF_GETFRAME: // Let the client render into a dma buffer
    case MODE_DMABUF_TRANSFER: // Client supplies RAM buffers and we transfer
      return 1;
      break;
    }
  return 0;
  }

void bg_glvideo_resync(bg_glvideo_t * g)
  {
  int i;
  if(g->hwctx)
    gavl_hw_ctx_resync(g->hwctx);

  for(i = 0; i < g->num_ports; i++)
    {
    if(g->ports[i]->hwctx_dma)
      gavl_hw_ctx_resync(g->ports[i]->hwctx_dma);
    }
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


