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

#include <string.h>
#include <math.h>


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

#ifdef HAVE_DRM
#include <gavl/hw_dmabuf.h>
#endif
   
typedef struct
  {
  gavl_pixelformat_t pfmt;
  gavl_hw_type_t type;
  gavl_hw_frame_mode_t mode; /* Transfer or map */

  uint32_t dma_fourcc;
  int      dma_flags;
  int max_w;
  int max_h;
  } image_format_t;

typedef struct
  {
  float  pos[3];
  float  tex[2];
  } vertex;


typedef struct
  {
  gavl_video_frame_t * texture;

  /* Shader program for the texture and for the Overlays */
  //  shader_program_t progs_cm[2];
  //  shader_program_t progs_nocm[2];

  shader_program_t shader_cm;
  shader_program_t shader_nocm;
  
#if 0
  shader_program_t prog_planar_cm;
  shader_program_t prog_noplanar_cm;
  shader_program_t prog_dma_cm;
  
  shader_program_t prog_planar_nocm;
  shader_program_t prog_noplanar_nocm;
  shader_program_t prog_dma_nocm;
#endif
  
  GLuint vbo; /* Vertex buffer */
  
  float brightness;
  float saturation;
  float contrast;

  int mode;

  int colormatrix_changed;
  int colormatrix_changed_ovl;

  double bsc[4][5]; // Brightness, saturation, contrast
  double cmat[4][5]; // Colorspace conversion

  int use_cmat;
  int use_cmat_ovl;
  
  
  gavl_video_frame_t * cur_frame;
  gavl_hw_type_t hwtype;

  image_format_t * formats;
  int num_formats;
  } gl_priv_t;

static int use_dma(int mode)
  {
  return (mode == MODE_DMABUF_IMPORT) ||
    (mode == MODE_DMABUF_GETFRAME) ||
    (mode == MODE_DMABUF_TRANSFER);
  }

static void
adjust_video_format_internal(driver_data_t* d, gavl_video_format_t * fmt, const image_format_t * imgfmt,
                              gavl_hw_frame_mode_t * mode);


static int find_format(driver_data_t* d, gavl_video_format_t * fmt, gavl_hw_frame_mode_t mode, int * penalty_ret, int ovl);

static int supports_hw_gl(driver_data_t* d,
                          gavl_hw_context_t * ctx)
  {
  gavl_hw_type_t type = gavl_hw_ctx_get_type(ctx);
  gl_priv_t * p = d->priv;
  
  if(type == p->hwtype)
    return 1;

  if(gavl_hw_ctx_can_import(d->hwctx_priv, ctx))
    return 1;
  
#ifdef HAVE_DRM  
  if(gavl_hw_ctx_can_import(d->hwctx_priv, d->win->dma_hwctx) &&
     gavl_hw_ctx_can_export(ctx, d->win->dma_hwctx))
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

static const int * default_attributes(gavl_hw_type_t type)
  {
  if(type == GAVL_HW_EGL_GLES_X11)
    return default_attributes_gles;
  else
    return default_attributes_gl;
  }

static void update_colormatrix(driver_data_t* d);

static void set_gl(driver_data_t * d)
  {
  //  fprintf(stderr, "set GL... %p", priv->hwctx_gl);
  gavl_hw_egl_set_current(d->hwctx, d->win->current->egl_surface);
  //  fprintf(stderr, "set GL done\n");
  }

static void unset_gl(driver_data_t * d)
  {
  gavl_hw_egl_unset_current(d->hwctx);
  //  fprintf(stderr, "unset GL %p\n", priv->hwctx_gl);
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

static const char * fragment_shader_gl_cm =
  "#version 110\n"
  "varying vec2 TexCoord;" 
  "uniform sampler2D frame;"
  "uniform mat4 colormatrix;"
  "uniform vec4 coloroffset;"
  "void main()"
  "  {"
  "  gl_FragColor = colormatrix * texture2D(frame, TexCoord) + coloroffset;"
  "  }";

static const char * fragment_shader_gl_nocm =
  "#version 110\n"
  "varying vec2 TexCoord;" 
  "uniform sampler2D frame;"
  "void main()"
  "  {"
  "  gl_FragColor = texture2D(frame, TexCoord);"
  "  }";


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


static const char * fragment_shader_gles_cm =
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

static const char * fragment_shader_gles_nocm =
  "#version 300 es\n"
  "precision mediump float;\n"
  "in vec2 TexCoord;\n" 
  "out vec4 FragColor;\n"
  "uniform sampler2D frame;\n"
  "void main()\n"
  "  {\n"
  "  FragColor = texture2D(frame, TexCoord);\n"
  "  }";

static const char * fragment_shader_planar_gles_cm =
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

static const char * fragment_shader_gles_ext_cm =
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
  "  }";

static const char * fragment_shader_gles_ext_nocm =
  "#version 300 es\n"
  "#extension GL_OES_EGL_image_external : require\n"
  "precision mediump float;\n"
  "in vec2 TexCoord;\n" 
  "out vec4 FragColor;\n"
  "uniform samplerExternalOES frame;\n"
  "void main()\n"
  "  {\n"
  "  FragColor = texture2D(frame, TexCoord);\n"
  "  }";

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

static void create_shader_program(driver_data_t * d, shader_program_t * p, int planar, int cm, int use_dma)
  {
  const char * shader[2];
  int num = 0;
  //  gl_priv_t * priv = d->priv;
  //  bg_x11_window_t * w;

  //  priv = d->priv;
  //  w = d->win;
  //  int num_planes = gavl_pixelformat_num_planes(d->win->video_format.pixelformat);

  
  p->program = glCreateProgram();
  
  /* Vertex shader */
  
  switch(gavl_hw_ctx_get_type(d->hwctx))
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
  
  switch(gavl_hw_ctx_get_type(d->hwctx))
    {
    case GAVL_HW_EGL_GL_X11:
      if(planar)
        shader[0] = fragment_shader_planar_gl;
      else
        {
        if(cm)
          shader[0] = fragment_shader_gl_cm;
        else
          shader[0] = fragment_shader_gl_nocm;
        }
      num = 1;
      break;
    case GAVL_HW_EGL_GLES_X11:
      if(use_dma)
        {
        if(cm)
          shader[0] = fragment_shader_gles_ext_cm;
        else
          shader[0] = fragment_shader_gles_ext_nocm;
        }
      else if(planar)
        {
        shader[0] = fragment_shader_planar_gles_cm;
        }
      else
        {
        if(cm)
          shader[0] = fragment_shader_gles_cm;
        else
          shader[0] = fragment_shader_gles_nocm;
        }
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

  if(planar)
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
               1, coloroffset);
  
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

#if 1
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
    if(fmts[i].max_w || fmts[i].max_h)
      fprintf(stderr, " max_size: %dx%d", fmts[i].max_w, fmts[i].max_h);
    
    fprintf(stderr, "\n");
    }
  }
#endif

static void init_image_formats(driver_data_t* d)
  {
  int num_dma_formats = 0;
  int num_gl_formats = 0;
  int i, idx = 0;
  gavl_pixelformat_t * gl_formats = NULL;
  EGLint *dma_formats;
  gavl_pixelformat_t pfmt;
  gl_priv_t * priv = d->priv;

  int max_texture_size = gavl_hw_egl_get_max_texture_size(d->hwctx_priv);
  
  gl_formats = gavl_gl_get_image_formats(d->hwctx_priv, &num_gl_formats);
  dma_formats = gavl_hw_ctx_egl_get_dma_import_formats(d->hwctx_priv, &num_dma_formats);
  
  priv->num_formats = num_gl_formats + num_dma_formats;
  
#ifdef HAVE_DRM

  /* Planar YUV formats can be video- or jpeg-scaled. They don't have
     explicit fourccs set but when imported as EGL image, we can
     use EGL_SAMPLE_RANGE_HINT_EXT and EGL_YUV_FULL_RANGE_EXT */

  for(i = 0; i < num_dma_formats; i++)
    {
    pfmt = gavl_drm_pixelformat_from_fourcc(dma_formats[i], NULL, NULL);
    switch(pfmt)
      {
      case GAVL_YUV_420_P:
      case GAVL_YUV_422_P:
      case GAVL_YUV_444_P:
        priv->num_formats++;
        break;
      default:
        break;
      }
    }
#endif

  priv->formats = calloc(priv->num_formats, sizeof(*priv->formats));
  
#ifdef HAVE_DRM
  
  /* Best option: Zero copy DMA frames */
  for(i = 0; i < num_dma_formats; i++)
    {
    /* Formats are stored from highest to smallest speed */
    if(((pfmt = gavl_drm_pixelformat_from_fourcc(dma_formats[i],
                                                 &priv->formats[idx].dma_flags, NULL)) !=
        GAVL_PIXELFORMAT_NONE) &&
       !(priv->formats[idx].dma_flags & GAVL_DMABUF_FLAG_SHUFFLE))
      {
      priv->formats[idx].pfmt = pfmt;
      priv->formats[idx].dma_fourcc = dma_formats[i];
      priv->formats[idx].mode = GAVL_HW_FRAME_MODE_MAP;
      priv->formats[idx].type = GAVL_HW_DMABUFFER;
      idx++;

      switch(pfmt)
        {
        case GAVL_YUV_420_P:
          priv->formats[idx].pfmt = GAVL_YUVJ_420_P;
          priv->formats[idx].dma_fourcc = dma_formats[i];
          priv->formats[idx].mode = GAVL_HW_FRAME_MODE_MAP;
          priv->formats[idx].type = GAVL_HW_DMABUFFER;
          idx++;
          break;
        case GAVL_YUV_422_P:
          priv->formats[idx].pfmt = GAVL_YUVJ_422_P;
          priv->formats[idx].dma_fourcc = dma_formats[i];
          priv->formats[idx].mode = GAVL_HW_FRAME_MODE_MAP;
          priv->formats[idx].type = GAVL_HW_DMABUFFER;
          idx++;
          break;
        case GAVL_YUV_444_P:
          priv->formats[idx].pfmt = GAVL_YUVJ_444_P;
          priv->formats[idx].dma_fourcc = dma_formats[i];
          priv->formats[idx].mode = GAVL_HW_FRAME_MODE_MAP;
          priv->formats[idx].type = GAVL_HW_DMABUFFER;
          idx++;
          break;
        default:
          break;
        }
      
      if(idx >= priv->num_formats)
        goto done;
      }
    }
#endif
  /* 2nd best option: glTexSubImage2D */
  
  for(i = 0; i < num_gl_formats; i++)
    {
    priv->formats[idx].pfmt = gl_formats[i];
    priv->formats[idx].mode = GAVL_HW_FRAME_MODE_TRANSFER;
    priv->formats[idx].type = gavl_hw_ctx_get_type(d->hwctx_priv);
    priv->formats[idx].max_w = max_texture_size;
    priv->formats[idx].max_h = max_texture_size;
    idx++;
    
    if(idx >= priv->num_formats)
      goto done;
    
    }
  /* 3nd best option: Shuffle unsupported pixelformat into DMA bufs */
#ifdef HAVE_DRM
  for(i = 0; i < num_dma_formats; i++)
    {
    /* Formats are stored from highest smallest speed */

    fprintf(stderr, "Testing fourcc %c%c%c%c\n",
            (dma_formats[i]) & 0xff,
            (dma_formats[i] >> 8) & 0xff,
            (dma_formats[i] >> 16) & 0xff,
            (dma_formats[i] >> 24) & 0xff);

    if(((pfmt = gavl_drm_pixelformat_from_fourcc(dma_formats[i],
                                                 &priv->formats[idx].dma_flags, NULL)) !=
        GAVL_PIXELFORMAT_NONE) &&
       (priv->formats[idx].dma_flags & GAVL_DMABUF_FLAG_SHUFFLE))
      {
      priv->formats[idx].pfmt = pfmt;
      priv->formats[idx].mode = GAVL_HW_FRAME_MODE_TRANSFER;
      priv->formats[idx].type = GAVL_HW_DMABUFFER;
      priv->formats[idx].dma_fourcc = dma_formats[i];
      
      idx++;

      if(idx >= priv->num_formats)
        goto done;
      }
    }
#endif

  done:
  dump_image_formats(priv->formats, priv->num_formats);
  
  }


static int init_gl_internal(driver_data_t * d, gavl_hw_type_t type)
  {
  gl_priv_t * priv;
  priv = calloc(1, sizeof(*priv));
  priv->hwtype = type;
  
  /* Create initial context to check if that works */
  if(!(d->hwctx_priv = gavl_hw_ctx_create_egl(default_attributes(priv->hwtype),
                                              priv->hwtype, d->win->dpy)))
    {
    free(priv);
    return 0;
    }
  
  d->priv = priv;
  
  
#if 0  
  fprintf(stderr, "Type: %s\n", gavl_hw_type_to_string(type));
  fprintf(stderr, "Image formats:\n");
  dump_image_formats(d->img_formats);
  fprintf(stderr, "Overlay formats:\n");
  dump_image_formats(d->ovl_formats);
#endif

  init_image_formats(d);
  
  d->flags |= (DRIVER_FLAG_BRIGHTNESS | DRIVER_FLAG_SATURATION | DRIVER_FLAG_CONTRAST);
  
  return 1;
  }

static int init_gl(driver_data_t * d)
  {
  return init_gl_internal(d, GAVL_HW_EGL_GL_X11);
  }

static int init_gles(driver_data_t * d)
  {
  return init_gl_internal(d, GAVL_HW_EGL_GLES_X11);
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

static void transform_vertex(float mat[2][2],
                             float src_x, float src_y, float * dst)
  {
  dst[0] = src_x * mat[0][0] + src_y * mat[0][1];
  dst[1] = src_x * mat[1][0] + src_y * mat[1][1];
  }

static void update_vertex_buffer(driver_data_t * d,
                                 float llx, float lly, float urx, float ury,
                                 float tllx, float tlly, float turx, float tury,
                                 gavl_image_orientation_t orient)
  {
  vertex v[4];
  gl_priv_t * priv;
  float mat[2][2];
  priv = d->priv;
  
  switch(orient)
    {
    case GAVL_IMAGE_ORIENT_NORMAL:  // EXIF: 1
      mat[0][0] = 1.0;
      mat[0][1] = 0.0;
      mat[1][0] = 0.0;
      mat[1][1] = 1.0;
      break;
    case GAVL_IMAGE_ORIENT_ROT90_CW:  // EXIF: 8
      mat[0][0] =  0.0;
      mat[0][1] = -1.0;
      mat[1][0] =  1.0;
      mat[1][1] =  0.0;
      break;
    case GAVL_IMAGE_ORIENT_ROT180_CW: // EXIF: 3
      mat[0][0] = -1.0;
      mat[0][1] =  0.0;
      mat[1][0] =  0.0;
      mat[1][1] = -1.0;
      break;
    case GAVL_IMAGE_ORIENT_ROT270_CW: // EXIF: 6
      mat[0][0] =  0.0;
      mat[0][1] =  1.0;
      mat[1][0] = -1.0;
      mat[1][1] =  0.0;
      break;
    case GAVL_IMAGE_ORIENT_FH:       // EXIF: 2
      mat[0][0] = -1.0;
      mat[0][1] = 0.0;
      mat[1][0] = 0.0;
      mat[1][1] = 1.0;
      break;
    case GAVL_IMAGE_ORIENT_FH_ROT90_CW:  // EXIF: 7
      mat[0][0] =  0.0;
      mat[0][1] =  1.0;
      mat[1][0] =  1.0;
      mat[1][1] =  0.0;
      break;
    case GAVL_IMAGE_ORIENT_FH_ROT180_CW: // EXIF: 4
      mat[0][0] =  1.0;
      mat[0][1] =  0.0;
      mat[1][0] =  0.0;
      mat[1][1] = -1.0;
      break;
    case GAVL_IMAGE_ORIENT_FH_ROT270_CW: // EXIF: 5
      mat[0][0] =  0.0;
      mat[0][1] =  -1.0;
      mat[1][0] =  -1.0;
      mat[1][1] =  0.0;
      break;
    default: // Keeps gcc quiet
      mat[0][0] = 1.0;
      mat[0][1] = 0.0;
      mat[1][0] = 0.0;
      mat[1][1] = 1.0;
      break;
      
    }
  
  transform_vertex(mat, llx, lly, v[0].pos);
  transform_vertex(mat, llx, ury, v[1].pos);
  transform_vertex(mat, urx, lly, v[2].pos);
  transform_vertex(mat, urx, ury, v[3].pos);
  
  v[0].pos[2] = 0.0;
  v[1].pos[2] = 0.0;
  v[2].pos[2] = 0.0;
  v[3].pos[2] = 0.0;
  
  v[0].tex[0] = tllx;
  v[0].tex[1] = tlly;
  v[1].tex[0] = tllx;
  v[1].tex[1] = tury;
  v[2].tex[0] = turx;
  v[2].tex[1] = tlly;
  v[3].tex[0] = turx;
  v[3].tex[1] = tury;
  
  glBindBuffer(GL_ARRAY_BUFFER, priv->vbo);  
  
  glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(v), v);

  /* Index 0: Position */
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(vertex), (void*)0);
  glEnableVertexAttribArray(0);
  
  /* Index 1: Texture coordinates */
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(vertex),
                        (void*)((void*)(&v[0].tex[0]) - (void*)(&v[0])));
  
  glEnableVertexAttribArray(1);
  
  }


static int open_gl(driver_data_t * d, int src_flags)
  {
  bg_x11_window_t * w;
  gl_priv_t * priv;
  gavl_hw_type_t src_type;
  int dma;
  int planar = 0;
  
  d->format_idx = -1;
  priv = d->priv;
  w = d->win;
  priv->mode = 0;

  if(w->video_format.hwctx)
    {
    /* Frames are in hardware already: Take these */

    src_type = gavl_hw_ctx_get_type(w->video_format.hwctx);

    if((src_type == GAVL_HW_EGL_GL_X11) || 
       (src_type == GAVL_HW_EGL_GLES_X11))
      {
      d->hwctx = w->video_format.hwctx;
      priv->mode = MODE_TEXTURE_DIRECT;

      planar = gavl_pixelformat_num_planes(w->video_format.pixelformat) > 1 ? 1 : 0;

      goto found;
      }
#ifdef HAVE_DRM  
    else if(gavl_hw_ctx_can_import(d->hwctx_priv, w->dma_hwctx) &&
            gavl_hw_ctx_can_export(w->video_format.hwctx, w->dma_hwctx))
      {
      priv->mode = MODE_DMABUF_IMPORT;
      d->hwctx = d->hwctx_priv;
      gavl_hw_ctx_set_video(d->hwctx, &w->video_format, GAVL_HW_FRAME_MODE_IMPORT);
      gavl_hw_ctx_set_video(w->dma_hwctx, &w->video_format, GAVL_HW_FRAME_MODE_IMPORT);
      goto found;
      }
#endif
    }
  
  d->format_idx = find_format(d, &w->video_format, d->frame_mode, NULL, 0);
  adjust_video_format_internal(d, &w->video_format, &priv->formats[d->format_idx], &d->frame_mode);
    
  switch(d->frame_mode)
    {
    case GAVL_HW_FRAME_MODE_MAP:
      priv->mode = MODE_DMABUF_GETFRAME;
      break;
    case GAVL_HW_FRAME_MODE_TRANSFER:
      if(priv->formats[d->format_idx].dma_flags & GAVL_DMABUF_FLAG_SHUFFLE)
        priv->mode = MODE_DMABUF_TRANSFER;
      else
        priv->mode = MODE_TEXTURE_TRANSFER;
      break;
    default:
      break;
    }
  
  d->hwctx = d->hwctx_priv;
  
  found:
  
  switch(priv->mode)
    {
    case MODE_TEXTURE_DIRECT:
      gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Using OpenGL%s via EGL (direct)",
               (src_type == GAVL_HW_EGL_GL_X11) ? "" : " ES");
      break;
    case MODE_TEXTURE_TRANSFER:
      gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Using OpenGL%s via EGL (indirect)",
               (priv->hwtype == GAVL_HW_EGL_GL_X11) ? "" : " ES");
      gavl_hw_ctx_set_video(d->hwctx, &w->video_format, d->frame_mode);
      break;
    case MODE_DMABUF_IMPORT:
      gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Showing DMA buffers with OpenGL ES via EGL");
      break;
    case MODE_DMABUF_GETFRAME:
      gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Rendering into DMA buffers");
      gavl_hw_ctx_set_video(w->dma_hwctx, &w->video_format, d->frame_mode);
      w->dma_frame = gavl_hw_video_frame_get(w->dma_hwctx);
      break;
    case MODE_DMABUF_TRANSFER:
      gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Transferring into DMA buffers");
      gavl_hw_ctx_set_video(w->dma_hwctx, &w->video_format, d->frame_mode);
      w->dma_frame = gavl_hw_video_frame_get(w->dma_hwctx);
      break;
    default:
      break;
    }
  
  w->normal.egl_surface = gavl_hw_ctx_egl_create_window_surface(d->hwctx, &w->normal.win);
  w->fullscreen.egl_surface = gavl_hw_ctx_egl_create_window_surface(d->hwctx, &w->fullscreen.win);
  
  //  bg_x11_window_start_gl(w);
  /* Get the format */

  set_gl(d);

  dma = use_dma(priv->mode);

  // static void create_shader_program(driver_data_t * d, shader_program_t * p, int planar, int cm, int use_dma)
  
  create_shader_program(d, &priv->shader_cm,   planar, 1, dma);
  create_shader_program(d, &priv->shader_nocm, planar, 0, dma);
  
  if((priv->mode != MODE_DMABUF_IMPORT) && (priv->mode != MODE_DMABUF_GETFRAME))
    //    set_pixelformat_matrix(priv->cmat, w->video_format.pixelformat);
    set_pixelformat_matrix(priv->cmat, d->fmt.pixelformat);
  update_colormatrix(d);
  create_vertex_buffer(d);
  
  unset_gl(d);
  
  return 1;
  }


static int is_unity(double cmat[4][5])
  {
  int i, j;
  
  for(i = 0; i < 4; i++)
    {
    for(j = 0; j < 5; j++)
      {
      if(i == j)
        {
        if(fabs(cmat[i][j] - 1.0) > 1e-6)
          return 0;
        }
      else
        {
        if(fabs(cmat[i][j]) > 1e-6)
          return 0;
        }
      }
    }
  return 1;
  }

static int set_frame_gl(driver_data_t * d, gavl_video_frame_t * f)
  {
  bg_x11_window_t * w;
  gl_priv_t * priv;

  
  priv = d->priv;
    
  w = d->win;
  
  if(priv->mode == MODE_TEXTURE_TRANSFER)
    {
    if(!priv->texture)
      priv->texture = gavl_hw_video_frame_create(d->hwctx, 1);

    //    gavl_video_frame_clear(f, &w->video_format);
    
    gavl_video_frame_ram_to_hw(priv->texture, f);
    f = priv->texture;

    priv->cur_frame = f;
    }

#ifdef HAVE_DRM
  else if(priv->mode == MODE_DMABUF_IMPORT)
    {
    gavl_video_frame_t * dma_frame = NULL;
    gavl_video_frame_t * gl_frame = NULL;

    if(!gavl_hw_ctx_transfer_video_frame(f, w->dma_hwctx, &dma_frame, &w->video_format))
      return 0;
    
    if(!gavl_hw_ctx_transfer_video_frame(dma_frame, d->hwctx, &gl_frame, &w->video_format))
      return 0;

    //    fprintf(stderr, "Imported frame %d %d %d\n", f->buf_idx, dma_frame->buf_idx, gl_frame->buf_idx);
    
    priv->cur_frame = gl_frame;
    }
  else if(priv->mode == MODE_DMABUF_GETFRAME)
    {
    gavl_video_frame_t * tex = NULL;
    if(!gavl_hw_ctx_transfer_video_frame(f, d->hwctx, &tex, &w->video_format))
      {
      }
    priv->cur_frame = tex;
    }

#endif
  else
    priv->cur_frame = f;

  return 1;
  }


static void put_frame_gl(driver_data_t * d)
  {
  int i;
  float tex_x1, tex_x2, tex_y1, tex_y2;
  float v_x1, v_x2, v_y1, v_y2;
  gavl_video_frame_t * ovl;
  gl_priv_t * priv;
  double cmat[4][5];
  
  shader_program_t * p = NULL;
  bg_x11_window_t * w  = d->win;

  gavl_gl_frame_info_t * info;
  
  priv = d->priv;
  
  set_gl(d);

  info = priv->cur_frame->storage;
  
  if(priv->colormatrix_changed)
    {
    if((priv->mode == MODE_DMABUF_IMPORT) || (priv->mode == MODE_DMABUF_GETFRAME))
      matrix_copy(cmat, priv->bsc);
    else
      matrixmult(priv->bsc, priv->cmat, cmat);

    if(is_unity(cmat))
      priv->use_cmat = 0;
    else
      priv->use_cmat = 1;

    //    fprintf(stderr, "Use colormatrix %d\n", priv->use_cmat);
    }
  
  if(priv->use_cmat)
    {
    glUseProgram(priv->shader_cm.program);
    p = &priv->shader_cm;
    
    if(priv->colormatrix_changed)
      {
      upload_colormatrix(d, p, cmat);
      priv->colormatrix_changed = 0;
      }
    
    }
  else
    {
    priv->colormatrix_changed = 0;
    glUseProgram(priv->shader_nocm.program);
    p = &priv->shader_nocm;
    }

  
  for(i = 0; i < info->num_textures; i++)
    {
    glActiveTexture(GL_TEXTURE0 + i);
    glBindTexture(info->texture_target, info->textures[i]);

    fprintf(stderr, "Texture %d %d: %d\n", info->texture_target - GL_TEXTURE_2D, i, info->textures[i]);

    glUniform1i(p->frame_locations[i], i);
#if 0
    fprintf(stderr, "Bind texture %d %d %d\n",
            priv->texture_target - GL_TEXTURE_2D,
            GL_TEXTURE0 + i, tex[i]);
#endif
    }
  
  glDisable(GL_DEPTH_TEST);

  glClearColor(0.0, 0.0, 0.0, 0.0);
  
  glClear(GL_COLOR_BUFFER_BIT);
  
  /* Setup Viewport */
  glViewport(w->dst_rect.x, w->dst_rect.y, w->dst_rect.w, w->dst_rect.h);


  /* Draw this */
  
  tex_x1 = (w->src_rect.x) / w->video_format_n.image_width;
  tex_y1 = (w->src_rect.y + w->src_rect.h) / w->video_format_n.image_height;
  
  tex_x2 = (w->src_rect.x + w->src_rect.w) / w->video_format_n.image_width;
  tex_y2 = (w->src_rect.y) / w->video_format_n.image_height;


  update_vertex_buffer(d,
                       // 0, 0, w->dst_rect.w, w->dst_rect.h,
                       -1.0, -1.0, 1.0, 1.0,
                       tex_x1, tex_y1, tex_x2, tex_y2, w->video_format.orientation);
  
  //  glColor4f(1.0, 1.0, 1.0, 1.0);
  //  glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_DECAL);


  glBindBuffer(GL_ARRAY_BUFFER, priv->vbo);
  
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

  glDisableVertexAttribArray(0);
  glDisableVertexAttribArray(1);
  
  //  glUseProgram(0);

  //  if(w->overlay_streams[0]->active)
  //    fprintf(stderr, "Put frame %d\n", w->overlay_streams[0]->active);
  
  /* Draw overlays */
  if(w->num_overlay_streams)
    {
    if(priv->colormatrix_changed_ovl)
      {
      if(is_unity(priv->bsc))
        priv->use_cmat_ovl = 0;
      else
        priv->use_cmat_ovl = 1;
      }

    if(priv->use_cmat_ovl)
      p = &priv->progs_cm[1];
    else
      p = &priv->progs_nocm[1];
    
    if(p->program)
      {
      glUseProgram(p->program);
      glUniform1i(p->frame_locations[0], 0);
      glActiveTexture(GL_TEXTURE0 + 0);
      
      if(priv->colormatrix_changed_ovl)
        {
        if(priv->use_cmat_ovl)
          upload_colormatrix(d, p, priv->bsc);
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

      ovl = w->overlay_streams[i]->ovl;

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
                           tex_x1, tex_y1, tex_x2, tex_y2, GAVL_IMAGE_ORIENT_NORMAL);
      
      glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
      
      }
    glDisable(GL_BLEND);
    }

  //  glDisable(GL_TEXTURE_2D);

  //  fprintf(stderr, "Swap buffers\n");
  gavl_hw_egl_swap_buffers(d->hwctx);
  
  glUseProgram(0);
  
  unset_gl(d);
  }

/* Overlay support */

static void set_overlay_gl(driver_data_t* d, overlay_stream_t * str,
                           gavl_video_frame_t * frame)
  {
  switch(str->mode)
    {
    case MODE_DMABUF_TRANSFER:
      gavl_video_frame_ram_to_hw(str->dma_frame, frame);
      break;
    case MODE_TEXTURE_TRANSFER:
      set_gl(d);
      gavl_gl_frame_to_hw(&str->format, str->ovl, frame);
      unset_gl(d);
      break;
    }
  gavl_video_frame_copy_metadata(str->ovl, frame);
  }


static void init_overlay_stream_gl(driver_data_t* d, overlay_stream_t * str)
  {
  int format_idx;
  gl_priv_t * priv;
  priv = d->priv;
  
  if(str->format.pixelformat == GAVL_PIXELFORMAT_NONE)
    str->format.pixelformat = GAVL_RGBA_32;
  
  if((format_idx = find_format(d, &str->format, d->frame_mode, NULL, 1)) >= 0)
    {
    fprintf(stderr, "Blupp: %s\n", gavl_pixelformat_to_string(priv->formats[format_idx].pfmt));
    }
  
  adjust_video_format_internal(d, &str->format, &priv->formats[format_idx], &str->frame_mode);

  if(priv->formats[format_idx].dma_fourcc)
    {
    fprintf(stderr, "Loading overlays into DMA Buffers\n");
    str->mode = MODE_DMABUF_TRANSFER;
    str->dma_hwctx = gavl_hw_ctx_create_dma();
    gavl_hw_ctx_set_video(str->dma_hwctx, &str->format, GAVL_HW_FRAME_MODE_TRANSFER);
    str->dma_frame = gavl_hw_video_frame_create(str->dma_hwctx, 1);

    str->ovl = gavl_hw_video_frame_create(d->hwctx, 0);

    if(!gavl_hw_ctx_transfer_video_frame(str->dma_frame,
                                         d->hwctx, &str->ovl,
                                         &str->format))
      return;
    }
  else
    {
    fprintf(stderr, "Loading overlays into OpenGL Textures\n");
    str->mode = MODE_TEXTURE_TRANSFER;

    gavl_hw_egl_set_current(d->hwctx, EGL_NO_SURFACE);
    str->ovl = gavl_gl_create_frame(&str->format);
    gavl_hw_egl_unset_current(d->hwctx);

    }
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

  delete_program(&priv->progs_cm[0]);
  delete_program(&priv->progs_cm[1]);
  delete_program(&priv->progs_nocm[0]);
  delete_program(&priv->progs_nocm[1]);
  
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
    gavl_hw_ctx_egl_destroy_surface(d->hwctx, w->normal.egl_surface);
    w->normal.egl_surface = EGL_NO_SURFACE;
    }
  if(w->fullscreen.egl_surface != EGL_NO_SURFACE)
    {
    gavl_hw_ctx_egl_destroy_surface(d->hwctx, w->fullscreen.egl_surface);
    w->fullscreen.egl_surface = EGL_NO_SURFACE;
    }

  if(d->hwctx_priv)
    gavl_hw_ctx_reset(d->hwctx_priv);
  }

static void cleanup_gl(driver_data_t * d)
  {
  gl_priv_t * priv;
  priv = d->priv;
  
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
  c = (priv->contrast   - BG_CONTRAST_MIN  ) / (BG_CONTRAST_MAX   - BG_CONTRAST_MIN  );
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

static int find_format(driver_data_t* d, gavl_video_format_t * fmt,
                       gavl_hw_frame_mode_t mode, int * penalty_ret, int ovl)
  {
  int i;
  int pass;
  int min_index = -1;
  int min_penalty = -1;
  int penalty;
  gl_priv_t * priv;

  fprintf(stderr, "find_format\n");

  priv = d->priv;
  
  for(pass = 0; pass < 3; pass++)
    {
    for(i = 0; i < priv->num_formats; i++)
      {
      if(ovl && !gavl_pixelformat_has_alpha(priv->formats[i].pfmt))
        continue;
      
      penalty = gavl_pixelformat_conversion_penalty(fmt->pixelformat,
                                                    priv->formats[i].pfmt);
      
      if(priv->formats[i].mode != mode)
        {
        if(pass < 1)
          continue;
        penalty += (1<<24);
        }
      /* Check max size */
      if((priv->formats[i].max_w && (fmt->image_width  > priv->formats[i].max_w)) ||
         (priv->formats[i].max_h && (fmt->image_height > priv->formats[i].max_h)))
        {
        if(pass < 2)
          continue;
        penalty += (1<<26);
        }
      
      if(priv->formats[i].dma_flags & GAVL_DMABUF_FLAG_SHUFFLE)
        penalty += (1<<25);
      
      fprintf(stderr, "Pass: %d, Format: %d, penalty: %d, fmt: %s\n", pass, i, penalty,
              gavl_pixelformat_to_string(priv->formats[i].pfmt) );
      
      if((min_penalty < 0) || (penalty < min_penalty))
        {
        min_penalty = penalty;
        min_index = i;
        }
      }
    if(min_index >= 0)
      break;
    }

  if(penalty_ret)
    *penalty_ret = min_penalty;
  
  return min_index;
  }

static void adjust_video_format_internal(driver_data_t* d, gavl_video_format_t * fmt,
                                         const image_format_t * imgfmt, gavl_hw_frame_mode_t * mode)
  {
  double shrink_x = 1.0, shrink_y = 1.0;

  fmt->pixelformat = imgfmt->pfmt;

  if(mode)
    *mode = imgfmt->mode;
  
  if((imgfmt->max_w > 0) && (fmt->image_width > imgfmt->max_w))
    shrink_x = (double)imgfmt->max_w / (double)fmt->image_width;
    
  if((imgfmt->max_h > 0) && (fmt->image_height > imgfmt->max_h))
    shrink_y = (double)imgfmt->max_h / (double)fmt->image_height;

  if((shrink_x < 1.0) || (shrink_y < 1.0))
    {
    /* To preserve the aspect ratio we shrink both dimensions by the
       smaller of both factors */
    if(shrink_y < shrink_x)
      {
      fmt->image_width = (int)((double)fmt->image_width * shrink_y + 0.5);
      fmt->image_height = imgfmt->max_h;
      }
    else
      {
      fmt->image_height = (int)((double)fmt->image_height * shrink_x + 0.5);
      fmt->image_width = imgfmt->max_w;
      }
    
    fmt->frame_width = 0;
    fmt->frame_height = 0;
    gavl_video_format_set_frame_size(fmt, 8, 1);
    }
  
  }


static void adjust_video_format_gl(driver_data_t* d, gavl_video_format_t * fmt, gavl_hw_frame_mode_t mode)
  {
  int idx;
  gl_priv_t * priv;
  
  priv = d->priv;
  
  idx = find_format(d, fmt, mode, &d->penalty, 0);
  if(idx >= 0)
    adjust_video_format_internal(d, fmt, &priv->formats[idx], &d->frame_mode);
  else
    fmt->pixelformat = GAVL_PIXELFORMAT_NONE;
  }

const video_driver_t gl_driver =
  {
    .name                = "OpenGL",
    .supports_hw         = supports_hw_gl,
    .adjust_video_format = adjust_video_format_gl,
    .init                = init_gl,
    .open                = open_gl,
    .set_brightness      = set_brightness_gl,
    .set_saturation      = set_saturation_gl,
    .set_contrast        = set_contrast_gl,
    .set_frame           = set_frame_gl,
    .put_frame           = put_frame_gl,
    .close               = close_gl,
    .cleanup             = cleanup_gl,
    .init_overlay_stream = init_overlay_stream_gl,
    .set_overlay         = set_overlay_gl,
  };

const video_driver_t gles_driver =
  {
    .name                = "OpenGL ES",
    .supports_hw         = supports_hw_gl,
    .adjust_video_format = adjust_video_format_gl,
    .init                = init_gles,
    .open                = open_gl,
    .set_brightness      = set_brightness_gl,
    .set_saturation      = set_saturation_gl,
    .set_contrast        = set_contrast_gl,
    .set_frame           = set_frame_gl,
    .put_frame           = put_frame_gl,
    .close               = close_gl,
    .cleanup             = cleanup_gl,
    .init_overlay_stream = init_overlay_stream_gl,
    .set_overlay         = set_overlay_gl,
  };
