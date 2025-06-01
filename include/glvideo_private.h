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




typedef struct
  {
  float  pos[2];
  float  tex[2];
  } vertex;

typedef enum
  {
    MODE_TEXTURE_TRANSFER = 1, // Copy frames from RAM to OpenGL Texture
    MODE_TEXTURE_DIRECT   = 2, // Use OpenGL textures created somewhere else
    MODE_IMPORT           = 3, // Import directly
    MODE_IMPORT_DMABUF    = 4, // Import via DMA-Buffers
    MODE_DMABUF_GETFRAME  = 5, // Let the client render into a dma buffer
    MODE_DMABUF_TRANSFER  = 6, // Client supplies RAM buffers and we transfer
                               // them (with shuffling) to a DMA buffer
  } video_mode_t;

/* Global status flags */

#define FLAG_COLORMATRIX_CHANGED (1<<0)
#define FLAG_COORDS_CHANGED      (1<<1)
#define FLAG_STILL               (1<<2)
#define FLAG_PAUSED              (1<<3)

/* Port status flags */

#define PORT_COLORSPACE_CNV      (1<<0)
#define PORT_USE_COLORMATRIX     (1<<1)
#define PORT_OVERLAY_CHANGED     (1<<2)

/* Image format */

typedef struct
  {
  gavl_pixelformat_t pfmt;
  gavl_hw_type_t type;
  gavl_hw_frame_mode_t mode; /* Transfer or map */

  uint32_t dma_fourcc;
  int      dma_flags;
  int max_dim;

  } image_format_t;

/* Shader program */

typedef struct
  {
  GLuint program;
  GLuint fragment_shader;
  GLuint vertex_shader;
  
  GLint frame_locations[3];
  GLint colormatrix_location;
  GLint coloroffset_location;

  /* Indices (obtained with getAttribLocation) */
  //  GLint pos;
  //  GLint tex;

  } shader_program_t;


/* Port */

typedef struct port_s
  {
  gavl_video_format_t fmt;
  gavl_video_sink_t * sink;
  video_mode_t mode;

  gavl_hw_context_t * hwctx_dma;
  gavl_hw_context_t * hwctx_dma_priv;
  
  /* Currently displayed frame */
  gavl_video_frame_t * cur;

  /* DMA frame (can be passed to sink) */
  gavl_video_frame_t * dma_frame;

  /* Texture (for uploading) */
  gavl_video_frame_t * texture;
  
  /* Shader with/without colormatrix */
  
  shader_program_t shader_cm;
  shader_program_t shader_nocm;
  
  shader_program_t * shader;
  
  bg_glvideo_t * g;
  int idx;

  gavl_sink_status_t (*set_frame)(struct port_s * port, gavl_video_frame_t * f);

  GLuint vbo; /* Vertex buffer */
  GLuint vao; /* Array buffer */
  
  int flags;
  double cmat[4][5]; // Pixelformat coversion matrix (or unity matrix)

  vertex vertices[4];
  
  } port_t;

struct bg_glvideo_s 
  {
  void * native_display;
  void * native_window;
  EGLSurface surface;

  port_t ** ports;
  int num_ports;
  int ports_alloc;
  
  int window_width;
  int window_height;
  bg_controllable_t ctrl;

  /* EGL contexts */
  gavl_hw_context_t * hwctx_gl;
  gavl_hw_context_t * hwctx_gles;
  gavl_hw_context_t * hwctx_dma;
  
  /* External or hwctx_gl or hwctx_gles */
  gavl_hw_context_t * hwctx;

  /* Masks of hardware types (one for GL, one for GL ES)
     depeding on the surrouding window system */
  int hw_mask;

  int flags;

  /* Pointer to the format of the first port */
  const gavl_video_format_t * vfmt;
  
  image_format_t * image_formats;
  int num_image_formats;

  float zoom_factor;
  float squeeze_factor;

  float brightness;
  float saturation;
  float contrast;

  float coords_zoom;
  
  /* *uncropped* rectangles used for coordinate transform */
  gavl_rectangle_f_t src_rect;
  gavl_rectangle_f_t dst_rect;

  int orientation;

  };

int bg_glvideo_create_shader(port_t * port, int cm);
void bg_glvideo_set_gl(bg_glvideo_t * g);
void bg_glvideo_unset_gl(bg_glvideo_t * g);

void bg_glvideo_update_colormatrix(port_t * port);
void bg_glvideo_init_colormatrix(port_t * port, gavl_pixelformat_t fmt);

void bg_glvideo_update_vertex_buffer(port_t * port);


#define ATTRIB_POS 0
#define ATTRIB_TEX 1
