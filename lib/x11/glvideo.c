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


#define GL_GLEXT_PROTOTYPES 1

#include <x11/x11.h>
#include <x11/x11_window_private.h>

#include <GL/gl.h>

#include <gavl/hw_gl.h>
#include <gavl/hw_egl.h>

#include <gmerlin/translation.h>
#include <gmerlin/log.h>
#define LOG_DOMAIN "glvideo"

#include <gmerlin/state.h>

#ifdef HAVE_V4L2
// #include <gavl/hw_v4l2.h>
#endif

#ifdef HAVE_DRM_DRM_FOURCC_H 
#define HAVE_DRM

#else // !HAVE_DRM_DRM_FOURCC_H 
#ifdef HAVE_LIBDRM_DRM_FOURCC_H 
#define HAVE_DRM
#endif
#endif // !HAVE_DRM_DRM_FOURCC_H 

#ifdef HAVE_DRM
#include <gavl/hw_dmabuf.h>
#endif

#ifndef GL_TEXTURE_EXTERNAL_OES
#define GL_TEXTURE_EXTERNAL_OES 0x8D65
#endif

/* Copy frames from RAM */
#define MODE_TEXTURE_COPY    1

/* Use textures created somewhere else */
#define MODE_TEXTURE_DIRECT  2

/* Use textures imported from DMA-Buffers */
#define MODE_TEXTURE_DMABUF  3

typedef struct
  {
  float  pos[3];
  float  tex[2];
  } vertex;

typedef struct
  {
  GLuint program;
  GLuint fragment_shader;
  GLuint vertex_shader;
  
  GLint frame_locations[3];
  GLint colormatrix_location;
  GLint coloroffset_location;
  } shader_program_t;

typedef struct
  {
  gavl_hw_context_t * hwctx_gl;
  gavl_hw_context_t * hwctx_priv;

#ifdef HAVE_DRM
  gavl_hw_context_t * hwctx_dmabuf;
#endif
  
  gavl_video_frame_t * texture;

  /* Shader program for the texture and for the Overlays */
  shader_program_t progs[2];
  
  GLuint vbo; /* Vertex buffer */
  
  float brightness;
  float saturation;
  float contrast;

  int mode;

  int colormatrix_changed;
  int colormatrix_changed_ovl;

  double bsc[4][5]; // Brightness, saturation, contrast
  double cmat[4][5]; // Colorspace conversion
  } gl_priv_t;

static int supports_hw_gl(driver_data_t* d,
                          gavl_hw_context_t * ctx)
  {
  gavl_hw_type_t type = gavl_hw_ctx_get_type(ctx);

#ifdef HAVE_DRM  
  gl_priv_t * p = d->priv;
#endif
  
  switch(type)
    {
    case GAVL_HW_EGL_GL_X11:
    case GAVL_HW_EGL_GLES_X11:
      return 1;
      break;
    default:
      break;
    }

#if 0 // No hardware context supports these yet
  if(gavl_hw_ctx_exports_type(ctx, GAVL_HW_EGL_GL_X11) ||
     gavl_hw_ctx_exports_type(ctx, GAVL_HW_EGL_GLES_X11))
    return 1;

  if(gavl_hw_ctx_imports_type(priv->hwctx_priv, gavl_hw_ctx_get_type(ctx)))
    return 1;
#endif

#ifdef HAVE_DRM  
  if(gavl_hw_ctx_imports_type(p->hwctx_priv, GAVL_HW_DMABUFFER) &&
     gavl_hw_ctx_exports_type(ctx, GAVL_HW_DMABUFFER))
    return 1;
#endif
  
  return 0;
  }

static void matrixmult(const double coeffs1[4][5],
                       const double coeffs2[4][5],
                       double result[4][5])
  {
  int i, j;

  for(i = 0; i < 4; i++) // dst rows
    {
    for(j = 0; j < 5; j++) // dst col
      {
      result[i][j] = 
        coeffs1[i][0] * coeffs2[0][j] +
        coeffs1[i][1] * coeffs2[1][j] +
        coeffs1[i][2] * coeffs2[2][j] +
        coeffs1[i][3] * coeffs2[3][j];
      }
    /* Last col */
    result[i][4] += coeffs1[i][4];
    }
  }

static void matrix_copy(double dst[4][5],
                        const double src[4][5])
  {
  int i;
  for(i = 0; i < 4; i++)
    memcpy(dst[i], src[i], 5 * sizeof(dst[i][0]));
  };

static void matrix_init(double dst[4][5])
  {
  int i, j;

  for(i = 0; i < 4; i++)
    {
    for(j = 0; j < 5; j++)
      {
      if(i == j)
        dst[i][j] = 1.0;
      else
        dst[i][j] = 0.0;
      }
    }
  }
                        

static const int default_attributes[] =
  {
    EGL_RED_SIZE,   8,
    EGL_GREEN_SIZE, 8,
    EGL_BLUE_SIZE,  8,
    EGL_ALPHA_SIZE, 8,
    EGL_CONFORMANT, EGL_OPENGL_ES3_BIT,
    EGL_NONE
  };

static void update_colormatrix(driver_data_t* d);

static void set_gl(driver_data_t * d)
  {
  gl_priv_t * priv = d->priv;
  gavl_hw_egl_set_current(priv->hwctx_gl, d->win->current->egl_surface);
  }

static void unset_gl(driver_data_t * d)
  {
  gl_priv_t * priv = d->priv;
  gavl_hw_egl_unset_current(priv->hwctx_gl);
  }

static const char * vertex_shader_gl = 
  "#version 110\n"
  "attribute vec3 pos;"
  "attribute vec2 tex;"
  "varying vec2 TexCoord;"
  "void main()"
  " {"
  " gl_Position = vec4(pos, 1.0);"
  " TexCoord = tex;"
  " }";

static const char * vertex_shader_gles = 
  "#version 300 es\n"
  "in vec3 pos;\n"
  "in vec2 tex;\n"
  "out vec2 TexCoord;\n"
  "void main()\n"
  " {\n"
  " gl_Position = vec4(pos, 1.0);\n"
  " TexCoord = tex;\n"
  " }";

static const char * fragment_shader_gl =
  "#version 110\n"
  "varying vec2 TexCoord;" 
  "uniform sampler2D frame;"
  "uniform mat4 colormatrix;"
  "uniform vec4 coloroffset;"
  "void main()"
  "  {"
  "  gl_FragColor = colormatrix * texture2D(frame, TexCoord) + coloroffset;"
  "  }";

#if 0
static const char * fragment_shader_planar_gl =
  "#version 110\n"
  "varying vec2 TexCoord;" 
  "uniform sampler2D frame;"
  "uniform sampler2D frame_u;"
  "uniform sampler2D frame_v;"
  "uniform mat4 colormatrix;"
  "uniform vec4 coloroffset;"
  "void main()"
  "  {"
  "  vec4 color = vec4( texture2D(frame, TexCoord).r, texture2D(frame_u, TexCoord).r, texture2D(frame_v, TexCoord).r, 1.0 );\n"
  "  gl_FragColor = colormatrix * color + coloroffset;"
  "  }";
#endif

static const char * fragment_shader_gles =
  "#version 300 es\n"
  "precision mediump float;\n"
  "in vec2 TexCoord;\n" 
  "out vec4 FragColor;\n"
  "uniform sampler2D frame;\n"
  "uniform mat4 colormatrix;\n"
  "uniform vec4 coloroffset;\n"
  "void main()\n"
  "  {\n"
  "  FragColor = colormatrix * texture2D(frame, TexCoord) + coloroffset;\n"
  "  }";

static const char * fragment_shader_planar_gles =
  "#version 300 es\n"
  "precision mediump float;\n"
  "in vec2 TexCoord;\n" 
  "out vec4 FragColor;\n"
  "uniform sampler2D frame;\n"
  "uniform sampler2D frame_u;\n"
  "uniform sampler2D frame_v;\n"
  "uniform mat4 colormatrix;\n"
  "uniform vec4 coloroffset;\n"
  "void main()\n"
  "  {\n"
  "  vec4 color = vec4( texture2D(frame, TexCoord).r, texture2D(frame_u, TexCoord).r, texture2D(frame_v, TexCoord).r, 1.0 );\n"
  "  FragColor = colormatrix * color + coloroffset;\n"
  "  }";

static const char * fragment_shader_gles_ext =
  "#version 300 es\n"
  "#extension GL_OES_EGL_image_external : require\n"
  "precision mediump float;\n"
  "in vec2 TexCoord;\n" 
  "out vec4 FragColor;\n"
  "uniform samplerExternalOES frame;\n"
  "uniform mat4 colormatrix;\n"
  "uniform vec4 coloroffset;\n"
  "void main()\n"
  "  {\n"
  "  FragColor = colormatrix * texture2D(frame, TexCoord) + coloroffset;\n"
  /*  "  FragColor = texture2D(frame, TexCoord);\n" */
  "  }";

/*
void gavl_video_format_get_chroma_offset(const gavl_video_format_t * format,
                                         int field, int plane,
                                         float * off_x, float * off_y)
*/

static void check_shader(GLuint shader, const char * name)
  {
  GLint isCompiled = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &isCompiled);
  if(isCompiled == GL_FALSE)
    {
    char * msg;
    //    GLint maxLength = 0;
    GLint Length = 0;
    //    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &maxLength);
    // The maxLength includes the NULL character
    //    fprintf(stderr, "Max error length: %d\n", maxLength);
    
    msg = malloc(1024);
        
    //    glGetShaderInfoLog(shader, maxLength, &maxLength, msg);

    glGetShaderInfoLog(shader, 1024, &Length, msg);
    msg[Length] = '\0';
    
    // Provide the infolog in whatever manor you deem best.
    // Exit with failure.
    glDeleteShader(shader); // Don't leak the shader.
    
    fprintf(stderr, "%s error: %s\n", name, msg);
    free(msg);
    return;
    }
#if 0
  else
    {
    fprintf(stderr, "%s compiled fine\n", name);
    }
#endif
  }

static void create_shader_program(driver_data_t * d, shader_program_t * p, int background)
  {
  const char * shader[2];
  int num = 0;
  gl_priv_t * priv = d->priv;
  //  bg_x11_window_t * w;

  //  priv = d->priv;
  //  w = d->win;
  int num_planes = gavl_pixelformat_num_planes(d->win->video_format.pixelformat);
    
  p->program = glCreateProgram();
  
  /* Vertex shader */
  
  switch(gavl_hw_ctx_get_type(priv->hwctx_gl))
    {
    case GAVL_HW_EGL_GL_X11:
      shader[0] = vertex_shader_gl;
      num = 1;
      break;
    case GAVL_HW_EGL_GLES_X11:
      shader[0] = vertex_shader_gles;
      num = 1;
      break;
    default:
      break;
    }
  
  p->vertex_shader = glCreateShader(GL_VERTEX_SHADER);
  //  fprintf(stderr, "Created vertex shader (ID: %d) %08x\n", p->vertex_shader, glGetError());
  
  glShaderSource(p->vertex_shader, num, shader, NULL);
  
  glCompileShader(p->vertex_shader);

  //  fprintf(stderr, "Compiled vertex shader (ID: %d) %08x\n", p->vertex_shader, glGetError());
  
  check_shader(p->vertex_shader, "vertex shader");
  
  glAttachShader(p->program, p->vertex_shader);
  
  /* Fragment shader */
  p->fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
  //  fprintf(stderr, "Created fragment shader (ID: %d) %08x\n", p->fragment_shader, glGetError());
  
  switch(gavl_hw_ctx_get_type(priv->hwctx_gl))
    {
    case GAVL_HW_EGL_GL_X11:
      shader[0] = fragment_shader_gl;
      num = 1;
      break;
    case GAVL_HW_EGL_GLES_X11:
      if((priv->mode == MODE_TEXTURE_DMABUF) && background)
        shader[0] = fragment_shader_gles_ext;
      else if(background && (num_planes > 1))
        shader[0] = fragment_shader_planar_gles;
      else
        shader[0] = fragment_shader_gles;
      num = 1;
      break;
    default:
      break;
    }
  
  glShaderSource(p->fragment_shader,
                 num, shader, NULL);

  glGetError();
  
  glCompileShader(p->fragment_shader);
  
  // fprintf(stderr, "Compiled fragment shader (ID: %d) %08x\n", priv->fragment_shader, glGetError());
  
  check_shader(p->fragment_shader, "fragment shader");
  
  glAttachShader(p->program, p->fragment_shader);

  
  glBindAttribLocation(p->program, 0, "pos");
  glBindAttribLocation(p->program, 1, "tex");

  glBindFragDataLocation(p->program, 0, "colorOut");
  
  glLinkProgram(p->program);
  
  p->frame_locations[0] = glGetUniformLocation(p->program, "frame");

  if(background && (num_planes == 3))
    {
    p->frame_locations[1] = glGetUniformLocation(p->program, "frame_u");
    p->frame_locations[2] = glGetUniformLocation(p->program, "frame_v");
    }
  
  p->colormatrix_location = glGetUniformLocation(p->program, "colormatrix");
  p->coloroffset_location = glGetUniformLocation(p->program, "coloroffset");
  
  
  }

static void upload_colormatrix(driver_data_t * d, shader_program_t * p, const double mat[4][5])
  {
  GLfloat colormatrix[16];
  GLfloat coloroffset[4];

  colormatrix[0]  = mat[0][0];
  colormatrix[1]  = mat[0][1];
  colormatrix[2]  = mat[0][2];
  colormatrix[3]  = mat[0][3];

  colormatrix[4]  = mat[1][0];
  colormatrix[5]  = mat[1][1];
  colormatrix[6]  = mat[1][2];
  colormatrix[7]  = mat[1][3];

  colormatrix[8]  = mat[2][0];
  colormatrix[9]  = mat[2][1];
  colormatrix[10] = mat[2][2];
  colormatrix[11] = mat[2][3];

  colormatrix[12] = mat[3][0];
  colormatrix[13] = mat[3][1];
  colormatrix[14] = mat[3][2];
  colormatrix[15] = mat[3][3];
  
  coloroffset[0]  = mat[0][4];
  coloroffset[1]  = mat[1][4];
  coloroffset[2]  = mat[2][4];
  coloroffset[3]  = mat[3][4];

  glUniformMatrix4fv(p->colormatrix_location,
                     1, GL_TRUE, colormatrix);
  
  glUniform4fv(p->coloroffset_location,
               1, coloroffset );
  
  }

/*
 *  Y:    16.0 .. 235.0 -> 16.0/255.0 .. 235.0/255.0 
 *  UV:   16.0 .. 240.0 -> 16.0/255.0 .. 240.0/255.0 
 */

static const double yuv_shift_matrix[4][5] =
  {
    { 1.0, 0.0, 0.0, 0.0, -16.0/255.0 },
    { 0.0, 1.0, 0.0, 0.0, -16.0/255.0 },
    { 0.0, 0.0, 1.0, 0.0, -16.0/255.0 },
    { 0.0, 0.0, 0.0, 1.0, 0.0 },
  };

static const double yuv_unscale_matrix[4][5] =
  {
    { 255.0/(235.0-16.0),                0.0,                0.0, 0.0, 0.0 },
    { 0.0,                255.0/(240.0-16.0),                0.0, 0.0, -0.5 },
    { 0.0,                               0.0, 255.0/(240.0-16.0), 0.0, -0.5 },
    { 0.0,                               0.0,                0.0, 1.0, 0.0 },
  };

static const double yuvj_unscale_matrix[4][5] =
  {
    { 1.0, 0.0, 0.0, 0.0, 0.0 },
    { 0.0, 1.0, 0.0, 0.0, -0.5 },
    { 0.0, 0.0, 1.0, 0.0, -0.5 },
    { 0.0, 0.0, 0.0, 1.0, 0.0 },
  };


static const double yuv_rgb_matrix[4][5] =
  {
    /* yr         ur         vr   ar   or */
    { 1.0,  0.000000,  1.402000, 0.0, 0.0 },
    /* yg         ug         vg   ag   og */
    { 1.0, -0.344136, -0.714136, 0.0, 0.0},
    /* yb         ub         vb   ab   ob */
    { 1.0,  1.772000,  0.000000, 0.0, 0.0},
    /* ya         ua         va   aa   oa */
    { 0.0,  0.000000,  0.000000, 1.0, 0.0 },
  };

/*
 *  RGB_out = [SBC] * RGB_in
 *  RGB_out = [SBC] * [YUVRGB] * YUV_in
 *  RGB_out = [SBC] * [YUVRGB] * [YUVunscale] * YUV_scaled
 *  RGB_out = [SBC] * [YUVRGB] * [YUVuscale] * [YUVshift] * YUV_unshifted
 *  RGB_out = [SBC] * [CMAT] * YUV_unshifted
 *  CMAT = [YUVRGB] * [YUVscale] * [YUVshift]
 */

static void set_pixelformat_matrix(double ret[4][5], gavl_pixelformat_t pfmt)
  {
  double tmp[4][5];

#if 1  
  if(pfmt & GAVL_PIXFMT_YUV)
    {
    matrix_copy(ret, yuv_rgb_matrix);
    
    if(pfmt & GAVL_PIXFMT_YUVJ)
      {
      matrixmult(ret, yuvj_unscale_matrix, tmp);
      matrix_copy(ret, tmp);
      }
    else
      {
      matrixmult(ret, yuv_unscale_matrix, tmp);
      matrix_copy(ret, tmp);
      
      matrixmult(ret, yuv_shift_matrix, tmp);
      matrix_copy(ret, tmp);
      }
    }
  else // RGB
#endif
    matrix_init(ret);
  }

static int init_gl(driver_data_t * d)
  {
  gl_priv_t * priv;
  priv = calloc(1, sizeof(*priv));
  /* Create initial context to check if that works */

  if(!(priv->hwctx_priv = gavl_hw_ctx_create_egl(default_attributes, GAVL_HW_EGL_GLES_X11, d->win->dpy)))
    {
    free(priv);
    return 0;
    }

  d->priv = priv;
  
  d->img_formats = gavl_hw_ctx_get_image_formats(priv->hwctx_priv);
  d->ovl_formats = gavl_hw_ctx_get_overlay_formats(priv->hwctx_priv);

  d->flags |= (DRIVER_FLAG_BRIGHTNESS | DRIVER_FLAG_SATURATION | DRIVER_FLAG_CONTRAST);
  
  return 1;
  }


static void set_overlay_gl(driver_data_t* d, overlay_stream_t * str)
  {
  gavl_video_frame_ram_to_hw(&str->format, str->ovl_hw, str->ovl);
  }

static void create_vertex_buffer(driver_data_t * d)
  {
  vertex v[4];
  
  gl_priv_t * priv;
  priv = d->priv;

  memset(v, 0, sizeof(v));
  
  glGenBuffers(1, &priv->vbo);

  glBindBuffer(GL_ARRAY_BUFFER, priv->vbo);  
  
  glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_DYNAMIC_DRAW);

  
  }

static void update_vertex_buffer(driver_data_t * d,
                                 float llx, float lly, float urx, float ury,
                                 float tllx, float tlly, float turx, float tury)
  {
  vertex v[4];
  gl_priv_t * priv;
  priv = d->priv;

  v[0].pos[0] = llx;
  v[0].pos[1] = lly;
  v[0].pos[2] = 0.0;

  v[0].tex[0] = tllx;
  v[0].tex[1] = tlly;
  
  v[1].pos[0] = llx;
  v[1].pos[1] = ury;
  v[1].pos[2] = 0.0;

  v[1].tex[0] = tllx;
  v[1].tex[1] = tury;
  
  v[2].pos[0] = urx;
  v[2].pos[1] = lly;
  v[2].pos[2] = 0.0;

  v[2].tex[0] = turx;
  v[2].tex[1] = tlly;

  v[3].pos[0] = urx;
  v[3].pos[1] = ury;
  v[3].pos[2] = 0.0;

  v[3].tex[0] = turx;
  v[3].tex[1] = tury;
  
  glBindBuffer(GL_ARRAY_BUFFER, priv->vbo);  
  
  //  glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_DYNAMIC_DRAW);

  glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(v), v);

  /* Index 0: Position */
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(vertex), (void*)0);
  glEnableVertexAttribArray(0);
  
  /* Index 1: Texture coordinates */
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(vertex),
                        (void*)((void*)(&v[0].tex[0]) - (void*)(&v[0])));
  
  glEnableVertexAttribArray(1);
  
  }


static int open_gl(driver_data_t * d)
  {
  bg_x11_window_t * w;
  gl_priv_t * priv;
  gavl_hw_type_t src_type;
  
  priv = d->priv;
  w = d->win;
  priv->mode = 0;
  
  //  priv->texture_target = GL_TEXTURE_2D;
  
  if(w->video_format.hwctx)
    {
    /* Frames are in hardware already: Take these */
    SET_FLAG(w, FLAG_NO_GET_FRAME);

    src_type = gavl_hw_ctx_get_type(w->video_format.hwctx);

    if((src_type == GAVL_HW_EGL_GL_X11) || 
       (src_type == GAVL_HW_EGL_GLES_X11))
      {
      priv->hwctx_gl = w->video_format.hwctx;
      priv->mode = MODE_TEXTURE_DIRECT;
      }
#ifdef HAVE_DRM  
    else if(gavl_hw_ctx_imports_type(priv->hwctx_priv, GAVL_HW_DMABUFFER) &&
            gavl_hw_ctx_exports_type(w->video_format.hwctx, GAVL_HW_DMABUFFER))
      {
      priv->mode = MODE_TEXTURE_DMABUF;
      priv->hwctx_gl = priv->hwctx_priv;
      priv->hwctx_dmabuf = gavl_hw_ctx_create_dma();
      }
#endif
    }
  else
    {
    priv->hwctx_gl = priv->hwctx_priv;
    priv->mode = MODE_TEXTURE_COPY;
    }
  
  switch(priv->mode)
    {
    case MODE_TEXTURE_DIRECT:
      gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Using OpenGL %s via EGL (direct)", (src_type == GAVL_HW_EGL_GL_X11) ? "" : "ES");
      break;
    case MODE_TEXTURE_COPY:
      gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Using OpenGL ES via EGL (indirect)");
      break;
    case MODE_TEXTURE_DMABUF:
      gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Showing DMA buffers with OpenGL ES");
      break;
    default:
      break;
    }
  
  w->normal.egl_surface = gavl_hw_ctx_egl_create_window_surface(priv->hwctx_gl, &w->normal.win);
  w->fullscreen.egl_surface = gavl_hw_ctx_egl_create_window_surface(priv->hwctx_gl, &w->fullscreen.win);

  
  //  bg_x11_window_start_gl(w);
  /* Get the format */

  set_gl(d);
  
  create_shader_program(d, &priv->progs[0], 1);
  
  create_shader_program(d, &priv->progs[1], 0);

  if(priv->mode != MODE_TEXTURE_DMABUF)
    set_pixelformat_matrix(priv->cmat, w->video_format.pixelformat);
  
  update_colormatrix(d);
  create_vertex_buffer(d);
  
  unset_gl(d);
  
  return 1;
  }

static gavl_video_frame_t * create_frame_gl(driver_data_t * d)
  {
  gavl_video_frame_t * ret;
  gl_priv_t * priv = d->priv;
  bg_x11_window_t * w = d->win;
  /* Used only for transferring to HW */
  ret = gavl_hw_video_frame_create_ram(priv->hwctx_gl, &w->video_format);
  gavl_video_frame_clear(ret, &w->video_format);
  return ret;
  }

static void put_frame_gl(driver_data_t * d, gavl_video_frame_t * f)
  {
  int i;
  bg_x11_window_t * w;
  float tex_x1, tex_x2, tex_y1, tex_y2;
  float v_x1, v_x2, v_y1, v_y2;
  gavl_video_frame_t * ovl;
  gl_priv_t * priv;
  gavl_gl_frame_info_t * info;
  
  priv = d->priv;
  
  w = d->win;
  
  if(!TEST_FLAG(w, FLAG_NO_GET_FRAME))
    {
    if(!priv->texture)
      priv->texture = gavl_hw_video_frame_create_hw(priv->hwctx_gl, &w->video_format);
    
    gavl_video_frame_ram_to_hw(&w->video_format, priv->texture, f);
    f = priv->texture;
    info = f->storage;
    }

#ifdef HAVE_DRM
  else if(priv->mode == MODE_TEXTURE_DMABUF)
    {
    gavl_video_frame_t * dma_frame = NULL;

    if(!gavl_hw_ctx_transfer_video_frame(f, priv->hwctx_dmabuf, &dma_frame, &w->video_format))
      {
      
      }
    
    if(!gavl_hw_ctx_transfer_video_frame(dma_frame, priv->hwctx_gl, &f, &w->video_format))
      {
      
      }
    info = f->storage;
    }
#endif
  else
    info = f->storage;
  
  set_gl(d);

    
  glUseProgram(priv->progs[0].program);

  if(priv->colormatrix_changed)
    {
    double mat[4][5];

    if(priv->mode == MODE_TEXTURE_DMABUF)
      upload_colormatrix(d, &priv->progs[0], priv->bsc);
    else
      {
      matrixmult(priv->bsc, priv->cmat, mat);
      upload_colormatrix(d, &priv->progs[0], mat);
      }
    priv->colormatrix_changed = 0;
    }
  
  /* Put image into texture */
  //  glEnable(GL_TEXTURE_2D);

  for(i = 0; i < info->num_textures; i++)
    {
    glActiveTexture(GL_TEXTURE0 + i);
    glBindTexture(info->texture_target, info->textures[i]);
    glUniform1i(priv->progs[0].frame_locations[i], i);
#if 0
    fprintf(stderr, "Bind texture %d %d %d\n",
            priv->texture_target - GL_TEXTURE_2D,
            GL_TEXTURE0 + i, tex[i]);
#endif
    }
  
  glDisable(GL_DEPTH_TEST);

  glClearColor (0.0, 0.0, 0.0, 0.0);
  
  glClear(GL_COLOR_BUFFER_BIT);
  
  /* Setup Viewport */
  glViewport(w->dst_rect.x, w->dst_rect.y, w->dst_rect.w, w->dst_rect.h);


  /* Draw this */
  
  tex_x1 = (w->src_rect.x) / w->video_format.image_width;
  tex_y1 = (w->src_rect.y + w->src_rect.h) / w->video_format.image_height;
  
  tex_x2 = (w->src_rect.x + w->src_rect.w) / w->video_format.image_width;
  tex_y2 = (w->src_rect.y) / w->video_format.image_height;


  update_vertex_buffer(d,
                       // 0, 0, w->dst_rect.w, w->dst_rect.h,
                       -1.0, -1.0, 1.0, 1.0,
                       tex_x1, tex_y1, tex_x2, tex_y2);
  
  //  glColor4f(1.0, 1.0, 1.0, 1.0);
  //  glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_DECAL);


  glBindBuffer(GL_ARRAY_BUFFER, priv->vbo);
  
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

  glDisableVertexAttribArray(0);
  glDisableVertexAttribArray(1);
  
  //  glUseProgram(0);

  /* Draw overlays */
  if(w->num_overlay_streams)
    {
    if(priv->progs[1].program)
      {
      glUseProgram(priv->progs[1].program);
      glUniform1i(priv->progs[1].frame_locations[0], 0);
      glActiveTexture(GL_TEXTURE0 + 0);

      if(priv->colormatrix_changed_ovl)
        {
        upload_colormatrix(d, &priv->progs[1], priv->bsc);
        priv->colormatrix_changed_ovl = 0;
        }
      }
    
    //    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    for(i = 0; i < w->num_overlay_streams; i++)
      {
      if(!w->overlay_streams[i]->active)
        continue;

      ovl = w->overlay_streams[i]->ovl_hw;

      info = ovl->storage;
      
      tex_x1 = (float)(ovl->src_rect.x) /
        w->overlay_streams[i]->format.image_width;
      
      tex_y1 = (float)(ovl->src_rect.y) /
        w->overlay_streams[i]->format.image_height;
    
      tex_x2 = (float)(ovl->src_rect.x + ovl->src_rect.w) /
        w->overlay_streams[i]->format.image_width;

      tex_y2 = (float)(ovl->src_rect.y + ovl->src_rect.h) /
        w->overlay_streams[i]->format.image_height;

      /* Pixel coordinates */
      
      v_x1 = ovl->dst_x - w->src_rect.x;
      v_y1 = ovl->dst_y - w->src_rect.y;

      v_x2 = v_x1 + ovl->src_rect.w;
      v_y2 = v_y1 + ovl->src_rect.h;

      /* Normalized coordinates */

      v_x1 /= w->src_rect.w;
      v_x2 /= w->src_rect.w;


      v_y1 = 1.0 - v_y1 / w->src_rect.h;
      v_y2 = 1.0 - v_y2 / w->src_rect.h;

      /* 0 .. 1 -> -1 .. 1 */

      v_x1 = (v_x1 - 0.5) * 2.0;
      v_x2 = (v_x2 - 0.5) * 2.0;
      
      v_y1 = (v_y1 - 0.5) * 2.0;
      v_y2 = (v_y2 - 0.5) * 2.0;

      //   glActiveTexture(GL_TEXTURE0 + 0);
      glBindTexture(info->texture_target, info->textures[0]);
      
      update_vertex_buffer(d,
                           // 0, 0, w->dst_rect.w, w->dst_rect.h,
                           v_x1, v_y1, v_x2, v_y2,
                           tex_x1, tex_y1, tex_x2, tex_y2);
      
      glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
      
      }
    glDisable(GL_BLEND);
    }

  //  glDisable(GL_TEXTURE_2D);

  gavl_hw_egl_swap_buffers(priv->hwctx_gl);
  
  glUseProgram(0);
  
  unset_gl(d);
  }

/* Overlay support */

static gavl_video_frame_t *
create_overlay_gl(driver_data_t* d, overlay_stream_t * str)
  {
  gl_priv_t * priv;
  gavl_video_frame_t * ret;
  priv = d->priv;
  ret = gavl_hw_video_frame_create_ovl(priv->hwctx_gl, &str->format);
  return ret;
  }


static void init_overlay_stream_gl(driver_data_t* d, overlay_stream_t * str)
  {
  gl_priv_t * priv;
  priv = d->priv;
  str->ovl_hw = gavl_hw_video_frame_create_hw(priv->hwctx_gl, &str->format);
  }

static void delete_program(shader_program_t * p)
  {
  if(p->program)
    glDeleteProgram(p->program);

  if(p->fragment_shader)
    glDeleteShader(p->fragment_shader);

  if(p->vertex_shader)
    glDeleteShader(p->vertex_shader);
  
  memset(p, 0, sizeof(*p));
  
  }
  
static void close_gl(driver_data_t * d)
  {
  bg_x11_window_t * w;
  gl_priv_t * priv;


  priv = d->priv;
  
  w = d->win;

  delete_program(&priv->progs[0]);
  delete_program(&priv->progs[1]);
  
  set_gl(d);

  if(priv->vbo)
    {
    glDeleteBuffers(1, &priv->vbo);
    priv->vbo = 0;
    }
  unset_gl(d);
  
  if(priv->texture)
    {
    gavl_video_frame_destroy(priv->texture);
    priv->texture = NULL;
    }

  if(w->normal.egl_surface != EGL_NO_SURFACE)
    {
    gavl_hw_ctx_egl_destroy_surface(priv->hwctx_gl, w->normal.egl_surface);
    w->normal.egl_surface = EGL_NO_SURFACE;
    }
  if(w->fullscreen.egl_surface != EGL_NO_SURFACE)
    {
    gavl_hw_ctx_egl_destroy_surface(priv->hwctx_gl, w->fullscreen.egl_surface);
    w->fullscreen.egl_surface = EGL_NO_SURFACE;
    }

#ifdef HAVE_DRM  
  if(priv->hwctx_dmabuf)
    {
    gavl_hw_ctx_destroy(priv->hwctx_dmabuf);
    priv->hwctx_dmabuf = NULL;
    }
#endif
  if(priv->hwctx_priv)
    {
    /* Re-create the hw context */
    gavl_hw_ctx_destroy(priv->hwctx_priv);
    priv->hwctx_priv = gavl_hw_ctx_create_egl(default_attributes, GAVL_HW_EGL_GLES_X11, d->win->dpy);
    }

  
  }

static void cleanup_gl(driver_data_t * d)
  {
  gl_priv_t * priv;
  priv = d->priv;

  
  if(priv->hwctx_priv)
    gavl_hw_ctx_destroy(priv->hwctx_priv);
  
  free(priv);
  }

// Inspired by https://docs.rainmeter.net/tips/colormatrix-guide/

#define LUM_R 0.299000
#define LUM_G 0.587000
#define LUM_B 0.114000

static void update_colormatrix(driver_data_t* d)
  {
  float b, s, c;

  float sr, sg, sb;
  float t;
  
  gl_priv_t * priv;
  //  float coeffs_yuva[4][5]; // Colormatrix with offset in YCbCrA colorspace
  
  priv = d->priv;

  /* Scale 0.0 .. 1.0 */
  b = (priv->brightness - BG_BRIGHTNESS_MIN) / (BG_BRIGHTNESS_MAX - BG_BRIGHTNESS_MIN);
  c = (priv->contrast - BG_CONTRAST_MIN) / (BG_CONTRAST_MAX - BG_CONTRAST_MIN);
  s = (priv->saturation - BG_SATURATION_MIN) / (BG_SATURATION_MAX - BG_SATURATION_MIN);

  //  fprintf(stderr, "update_colormatrix_1 %f %f %f\n",
  //          priv->brightness, priv->contrast, priv->saturation);

    
  c *= 2.0; // c: 0 .. 2.0, 1.0 = neutral

  b = b * 2.0 - 0.5 * (1.0 + c); // 
  
  s *= 2.0;
  
  //  fprintf(stderr, "update_colormatrix_2 %f %f %f\n", b, c, s);


  sr = (1 - s) * LUM_R;
  sg = (1 - s) * LUM_G;
  sb = (1 - s) * LUM_B;
  t = (1.0 - c) / 2.0;
  
  
  priv->bsc[0][0] = c * (sr + s);
  priv->bsc[0][1] = c * sg;
  priv->bsc[0][2] = c * sb;
  priv->bsc[0][3] = 0.0;
  priv->bsc[0][4] = b + t;
  
  priv->bsc[1][0] = c * sr;
  priv->bsc[1][1] = c * (sg + s);
  priv->bsc[1][2] = c * sb;
  priv->bsc[1][3] = 0.0;
  priv->bsc[1][4] = b + t;
  
  priv->bsc[2][0] = c * sr;
  priv->bsc[2][1] = c * sg;
  priv->bsc[2][2] = c * (sb + s);
  priv->bsc[2][3] = 0.0;
  priv->bsc[2][4] = b + t;
  
  priv->bsc[3][0] = 0.0;
  priv->bsc[3][1] = 0.0;
  priv->bsc[3][2] = 0.0;
  priv->bsc[3][3] = 1.0;
  priv->bsc[3][4] = 0.0;
  
  
  priv->colormatrix_changed = 1;
  priv->colormatrix_changed_ovl = 1;
  }

static void set_brightness_gl(driver_data_t* d,float val)
  {
  gl_priv_t * priv;
  priv = d->priv;

  fprintf(stderr, "set_brightness_gl %f\n", val);
  
  priv->brightness = val;
  update_colormatrix(d);
  }

static void set_saturation_gl(driver_data_t* d,float val)
  {
  gl_priv_t * priv;
  priv = d->priv;

  fprintf(stderr, "set_saturation_gl %f\n", val);

  priv->saturation = val;
  update_colormatrix(d);
  
  }
static void set_contrast_gl(driver_data_t* d,float val)
  {
  gl_priv_t * priv;
  priv = d->priv;

  fprintf(stderr, "set_contrast_gl %f\n", val);
  
  priv->contrast = val;
  update_colormatrix(d);
  }


const video_driver_t gl_driver =
  {
    .name               = "OpenGL",
    .can_scale          = 1,
    .supports_hw        = supports_hw_gl,
    .init               = init_gl,
    .open               = open_gl,
    .create_frame       = create_frame_gl,
    .set_brightness     = set_brightness_gl,
    .set_saturation     = set_saturation_gl,
    .set_contrast       = set_contrast_gl,
    .put_frame          = put_frame_gl,
    .close              = close_gl,
    .cleanup            = cleanup_gl,
    .init_overlay_stream = init_overlay_stream_gl,
    .create_overlay     = create_overlay_gl,
    .set_overlay        = set_overlay_gl,
  };