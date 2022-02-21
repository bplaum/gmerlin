#include <math.h>
#include <string.h>

#include <config.h> 

#include <gavl/gavl.h> 


#define GL_GLEXT_PROTOTYPES 1

#include <GL/gl.h>

#include <EGL/egl.h>
#include <gavl/hw_egl.h> 


#include <gavl/peakdetector.h> 

#include <gmerlin/translation.h> 
#include <gmerlin/log.h> 

#define LOG_DOMAIN "glscope"

#include <gmerlin/plugin.h> 

/*
 *  Working principle:
 *
 *  In each frame we first draw a texture of the last frame.
 *  In the fragment shader, we modulate the texture coordionates and the
 *  color.
 *
 *  Then we draw a foreground.
 * 
 */


#define FRAME_SAMPLES 512

#define MAX_FLASH_VERTICES 512

// #define FRAME_SAMPLES 2

#define MAX_FG_VERTICES FRAME_SAMPLES

#define TRANSFORM_ALLOW_MATRIX   (1<<0)
#define TRANSFORM_HAS_PARAMS     (1<<1)

/* Flags */

//#define FLAG_UPDATE_FLASH_VERTICES (1<<0)
//#define FLAG_FADE_FLASH_VERTICES   (1<<1)

#define FLASH_MODE_CROSS           0 // Cross (x-y)
#define FLASH_MODE_CROSS_DIAGONAL  1 // Cross diagonal
#define FLASH_MODE_STARS           2 // Stars
#define FLASH_MODE_POLYGON         3 // Polygon
#define FLASH_MODE_STAR            4 // Star
#define FLASH_MODE_RANDOM          5 // Random
#define FLASH_MODE_HLINES          6 // Horizontal lines 
#define FLASH_MODE_VLINES          7 // Vertical lines 
#define FLASH_MODE_PAINTER         8 // Painter
#define FLASH_MODE_PARAMETRIC      9 // Parametric curves

#define FLASH_MODE_MAX             10 // Max + 1 actually

#define MAX_PAINTERS               6
#define PAINTERS_MIN_WIDTH         4.0


typedef struct
  {
  const char * code; // Fragment shader snippet
  int flags;
  } texture_transform_t;

typedef struct
  {
  GLfloat matrix[16];
  GLfloat offset[4];
  } texture_matrix_t;

static const char * vertex_shader_tex;

static const char * fragment_shader_tex_head;
static const char * fragment_shader_tex_tail;

static const texture_transform_t fragment_shader_transforms[];
static int num_fragment_shader_transforms;

static const texture_matrix_t texture_matrices[];
static int num_texture_matrices;

static const texture_matrix_t texture_matrix_unity;


static const char * vertex_shader_fg;
static const char * fragment_shader_fg;

static const char * vertex_shader_flash;
static const char * fragment_shader_flash;

static void select_option(int num, int * ret1);
static int decide(float p);


typedef struct
  {
  int cnt;
  int end;
  } interpolator_t;

static void interpolator_init(interpolator_t * ip, int min, int max)
  {
  ip->cnt = 0;
  ip->end = min + (int)((float)rand()/RAND_MAX * (max - min) + 0.5);
  }

static double interpolator_get(const interpolator_t * ip)
  {
  if(ip->cnt >= ip->end)
    return 1.0;
  else
    {
    double ret = (double)ip->cnt / (double)(ip->end-1);
    ret = (1.0 - cos(ret * M_PI)) * 0.5;
    return ret;
    }
  }

static int interpolator_update(interpolator_t * ip)
  {
  if(!ip->end)
    return 0;
  
  ip->cnt++;
  if(ip->cnt == ip->end)
    {
    ip->cnt = 0;
    ip->end = 0;
    return 0;
    }
  return 1;
  }

static int interpolator_running(const interpolator_t * ip)
  {
  if(ip->end > 0)
    return 1;
  else
    return 0;
  }

static const texture_matrix_t * texture_matrix_interpolate(const interpolator_t * ip,
                                                           int * start_idx,
                                                           int * end_idx,
                                                           texture_matrix_t * tmp)
  {
  float fac;

  const texture_matrix_t * start;
  const texture_matrix_t * end;

  if(*start_idx < 0)
    return &texture_matrix_unity;
  
  if(*end_idx < 0)
    return &texture_matrices[*start_idx];
  
  if(!interpolator_running(ip))
    {
    *start_idx = *end_idx;
    *end_idx = -1;
    return &texture_matrices[*start_idx];
    }
  
  fac = interpolator_get(ip);

  start = &texture_matrices[*start_idx];
  end   = &texture_matrices[*end_idx];
  
  tmp->matrix[0] = end->matrix[0] * fac + start->matrix[0] * (1.0 - fac);
  tmp->matrix[1] = end->matrix[1] * fac + start->matrix[1] * (1.0 - fac);
  tmp->matrix[2] = end->matrix[2] * fac + start->matrix[2] * (1.0 - fac);
  tmp->matrix[3] = end->matrix[3] * fac + start->matrix[3] * (1.0 - fac);

  tmp->offset[0] = end->offset[0] * fac + start->offset[0] * (1.0 - fac);
  tmp->offset[1] = end->offset[1] * fac + start->offset[1] * (1.0 - fac);
  return tmp;
  }

static const void params_interpolate(const interpolator_t * ip,
                                         const GLfloat * start,
                                         const GLfloat * end,
                                         GLfloat * ret)
  {
  float fac;
  fac = interpolator_get(ip);
  
  ret[0] = end[0] * fac + start[0] * (1.0 - fac);
  ret[1] = end[1] * fac + start[1] * (1.0 - fac);
  ret[2] = end[2] * fac + start[2] * (1.0 - fac);
  ret[3] = end[3] * fac + start[3] * (1.0 - fac);
  }

typedef struct
  {
  GLfloat  pos[2];
  } vertex_t;

typedef struct
  {
  GLfloat  pos[2];
  GLfloat  color[4];
  } flash_vertex_t;

static const vertex_t tex_vertices[4] =
  {
    { { -1.0, -1.0 } }, // LL
    { { -1.0,  1.0 } }, // UL
    { {  1.0, -1.0 } }, // LR
    { {  1.0,  1.0 } }, // UR
  };

typedef struct
  {
  GLfloat col[2][3];
  } color_pair_t;

static const float fg_colors[][2][3] =
  {
    { { 0.2, 1.0, 0.2 }, { 1.0, 0.5, 0.2 } }, // Light green + orange
    { { 1.0, 0.2, 0.2 }, { 0.0, 1.0, 1.0 } }, // Light red + cyan
    { { 0.3, 0.3, 1.0 }, { 1.0, 0.7, 1.0 } }, // Light blue + pink
    { { 1.0, 0.5, 0.5 }, { 0.5, 1.0, 0.5 } },  // Light red + Light green
    { { 0.8, 0.8, 1.0 }, { 1.0, 1.0, 0.7 } }  // Light blue + Light yellow
  };

static const int num_fg_colors = sizeof(fg_colors) / sizeof(fg_colors[0]);

static const float border_colors[][4] =
  {
    { 0.2, 0.2,  0.2, 1.0 },  // Gray
    { 0.0, 0.0,  0.0, 1.0 },  // Black
    { 0.0, 0.0,  0.5, 1.0 },  // Dark blue
    { 0.3, 0.1,  0.3, 1.0 },  // Dark Pink
    { 0.0, 0.15,  0.0, 1.0 }, // Dark Green
    { 0.0, 0.15,  0.15, 1.0 }, // Dark Cyan
  };

static const int num_border_colors = sizeof(border_colors) / sizeof(border_colors[0]);

/* Color matrices */

static const struct
  {
  GLfloat matrix[16];
  GLfloat offset[4];
  }
colormatrices[] =
  {
#if 1
    /* Fade to black */
    {
      {
        0.98, 0.0,  0.0,  0.0,
        0.0,  0.98, 0.0,  0.0,
        0.0,  0.0,  0.98, 0.0,
        0.0,  0.0,  0.0,  1.0,
      },
      { 0.0, 0.0, 0.0, 0.0 }
    },
    /* Fade to dark blue */
    {
      {
        0.96, 0.0,  0.0,  0.0,
        0.0,  0.96, 0.0,  0.0,
        0.0,  0.0,  0.98, 0.0,
        0.0,  0.0,  0.0,  1.0,
      },
      { 0.0, 0.0, 0.01, 0.0 }
    },
    /* Fade to dark red */
    {
      {
        0.98, 0.0,  0.0,  0.0,
        0.0,  0.96, 0.0,  0.0,
        0.0,  0.0,  0.96, 0.0,
        0.0,  0.0,  0.0,  1.0,
      },
      { 0.01, 0.0, 0.00, 0.0 }
    },
    /* */
#endif
     {
      {
        0.96,  0.0,  0.02, 0.0,
        0.0,   0.96, 0.0,  0.0,
        0.02,  0.0,  0.96, 0.0,
        0.0,   0.0,  0.0,  1.0,
      },
      { 0.0, 0.0, 0.0, 0.0 }
    },
    {
      {
        0.96,  0.02, 0.0,  0.0,
        0.02,  0.96, 0.0,  0.0,
        0.0,   0.02, 0.96, 0.0,
        0.0,   0.0,  0.0,  1.0,
      },
      { 0.0, 0.0, 0.0, 0.0 }
    },
    
  };

static const int num_colormatrices = sizeof(colormatrices) / sizeof(colormatrices[0]);

static void params_init(GLfloat * ret)
  {
  //  fprintf(stderr, "attractors_init\n");
  
  ret[0] = ((GLfloat)rand() / RAND_MAX);
  ret[1] = ((GLfloat)rand() / RAND_MAX);
  ret[2] = ((GLfloat)rand() / RAND_MAX);
  ret[3] = ((GLfloat)rand() / RAND_MAX);
  }

typedef struct
  {
  int fac_1;
  int fac_2;

  int mode;   /* 0 .. 1 */
  int sign_1;
  int sign_2;
  } parametric_t;

static void parametric_init(parametric_t * p)
  {
  p->fac_1 = -1;
  select_option(6, &p->fac_1);

  p->fac_2 = p->fac_1;
  select_option(6, &p->fac_2);

  p->fac_1++;
  p->fac_2++;
  
  if(decide(0.5))
    p->sign_1 = -1;
  else
    p->sign_1 = 1;

  if(decide(0.5))
    p->sign_2 = -1;
  else
    p->sign_2 = 1;

  if(decide(0.5))
    p->mode = 1;
  else
    p->mode = 0;
  
  }

/* t: 0..1 */

static void get_parametric(const parametric_t * p, double t, float * x, float * y)
  {
  t *= 2.0 * M_PI;
  
  if(p->mode)
    {
    *x = p->sign_1 * sin(p->fac_1*t);
    *y = p->sign_2 * cos(p->fac_1*t);
    }
  else
    {
    *x = p->sign_1 * cos(p->fac_1*t);
    *y = p->sign_2 * sin(p->fac_1*t);
    }
  }


typedef struct
  {
  gavl_audio_sink_t * asink;
  gavl_video_source_t * vsource;

  gavl_audio_format_t afmt;
  gavl_video_format_t vfmt;
  
  gavl_video_frame_t * vframe_1;
  gavl_video_frame_t * vframe_2;
  gavl_video_frame_t * vframe_cur;
  gavl_video_frame_t * vframe_tex;
  
  gavl_audio_frame_t * aframe;
  
  gavl_hw_context_t * hwctx;
  
  /* Render to texture stuff */
  GLuint frame_buffer;
  GLuint depth_buffer;

  gavl_peak_detector_t * pd;

  /* Beat detection stuff */
  double peak;
  int beat_detected;
  float beat_volume;
  
  /* Foreground program */
  GLuint fg_vshader;
  GLuint fg_fshader;
  GLuint fg_program; 
  GLint fg_color_location;
  
  /* Texture program */
  GLuint tex_vshader;
  GLuint tex_fshader;
  GLuint tex_program; 

  GLint tex_location;

  /* Flash */
  GLuint flash_vshader;
  GLuint flash_fshader;
  GLuint flash_program; 

  GLenum flash_draw_mode;
  GLint flash_alpha_location;
  
  int num_flash_vertices;
  
  float flash_size;
  
  GLint colormatrix_location;
  GLint coloroffset_location;

  GLint texmatrix_location;
  GLint texoffset_location;
  GLint texaspect_location;

  /* Params */
  GLint params_location;
  
  /* Vertex buffers */
  
  GLuint tex_vbo; /* Vertex buffer (texture) */

  GLuint fg_vbo; /* Vertex buffer (foreground) */

  GLuint flash_vbo; /* Vertex buffer (flash) */
  
  vertex_t * fg_vertices;
  flash_vertex_t * flash_vertices;
  
  int fg_colidx;
  int fg_colidx_sub;
  
  int border_colidx;
  
  int fg_idx;
  int colormatrix_idx;
  int transform_idx;

  int64_t frame_counter;
  int64_t last_texture_change;

  int64_t last_matrix_change;
  int64_t last_params_change;
  
  /* Texture transformation matrix */
  int texture_mat_start;
  int texture_mat_end;
  
  interpolator_t texture_mat_ip;
  interpolator_t params_ip;
  
  interpolator_t flash_ip;
  int flash_style;
  
  GLfloat params_start[4];
  GLfloat params_end[4];

  int do_flash;

  struct
    {
    GLfloat coords[2];
    GLfloat dcoords[2];
    } painters[MAX_PAINTERS];

  parametric_t parametric[2];

  float aspect;
  
  } glscope_t;

static void new_tex_transform(glscope_t * s);
static void new_colormatrix(glscope_t * s);

static int decide(float probability)
  {
  float val = (float)rand() / RAND_MAX;
  return val < probability;
  }

static void select_option(int num, int * ret1)
  {
  int ret;
  
  /* 0 .. num */
  float val;

  if(*ret1 >= 0)
    num--;
  
  val = (num) * (float)rand() / RAND_MAX;

  ret = (int)(val);
  if(ret >= num)
    ret = num - 1;

  if((*ret1 >= 0) && (ret >= *ret1))
    ret++;
  
  *ret1 = ret;
  }

static gavl_sink_status_t put_audio_func(void * priv, gavl_audio_frame_t * f)
  {
  double peak = 0.0;
  
  glscope_t * s = priv;

  //  fprintf(stderr, "put_audio: %d\n", f->valid_samples);
  
  gavl_audio_frame_copy(&s->afmt,
                        s->aframe,
                        f,
                        0,
                        0,
                        f->valid_samples,
                        f->valid_samples);

  /* Beat detection */
  
  gavl_peak_detector_update(s->pd, f);

  gavl_peak_detector_get_peak(s->pd, NULL, NULL, &peak);

  gavl_peak_detector_reset(s->pd);
  
  if(peak < 1e-6)
    peak = 1e-6;

  peak = 20 * log10(peak);

  if((peak > -10.0) && (peak - s->peak > 4.0))
    {
    if(!s->beat_detected || (peak - s->peak > s->beat_volume))
      {
      s->beat_detected = 1;
      s->beat_volume = peak - s->peak;
      }
    }
  
  s->peak = peak;
  
  return GAVL_SINK_OK;
  }


static void draw_texture(glscope_t * s)
  {
  GLuint * tex;
  texture_matrix_t mat_buf;
  const texture_matrix_t * mat;
  
  int min_frames = 100;
  
  if(!s->vframe_tex)
    {
    glClearColor (0.0, 0.0, 0.0, 0.0);
    glClear(GL_COLOR_BUFFER_BIT);
    return;
    }

  if(fragment_shader_transforms[s->transform_idx].flags & (TRANSFORM_HAS_PARAMS|
                                                           TRANSFORM_ALLOW_MATRIX))
    min_frames = 300;
    
  if(s->beat_detected)
    {
    if(((s->frame_counter - s->last_texture_change) > min_frames) && decide(0.1))
      {
      s->last_texture_change = s->frame_counter;
      
      // fprintf(stderr, "New texture transform\n");
      new_tex_transform(s);
      }

    if(decide(0.1))
      {
      new_colormatrix(s);
      }

    if(decide(0.1))
      {
      select_option(num_border_colors, &s->border_colidx);
      }

    if((fragment_shader_transforms[s->transform_idx].flags & TRANSFORM_ALLOW_MATRIX) &&
       !interpolator_running(&s->texture_mat_ip) &&
       (s->frame_counter - s->last_matrix_change > 100) &&
       decide(0.1))
      {
      s->last_matrix_change = s->frame_counter;
      s->texture_mat_end = s->texture_mat_start;
      select_option(num_texture_matrices, &s->texture_mat_end);
      interpolator_init(&s->texture_mat_ip, 100, 200);
      }
    
    if((fragment_shader_transforms[s->transform_idx].flags & TRANSFORM_HAS_PARAMS) &&
       !interpolator_running(&s->params_ip) &&
       (s->frame_counter - s->last_params_change > 100) &&
       decide(0.1))
      {
      s->last_params_change = s->frame_counter;
      
      //      fprintf(stderr, "new params\n");
      
      memcpy(s->params_start, s->params_end, 4 * sizeof(s->params_end[0]));
      
      params_init(s->params_end);
      interpolator_init(&s->params_ip, 100, 200);
      }
    }

  glUseProgram(s->tex_program);
  
  /* Update texture matrix */
  mat = texture_matrix_interpolate(&s->texture_mat_ip,
                                   &s->texture_mat_start,
                                   &s->texture_mat_end,
                                   &mat_buf);

  glUniformMatrix2fv(s->texmatrix_location,
                     1, GL_TRUE, mat->matrix);
  glUniform2fv(s->texoffset_location, 1, mat->offset);

  glUniform1f(s->texaspect_location, s->aspect);
  
  /* Update params */
  if(fragment_shader_transforms[s->transform_idx].flags & TRANSFORM_HAS_PARAMS)
    {
    if(interpolator_running(&s->params_ip))
      {
      GLfloat tmp[4];
      params_interpolate(&s->params_ip,
                             s->params_start,
                             s->params_end,
                             tmp);
      glUniform4fv(s->params_location, 1, tmp);
      }
    else
      glUniform4fv(s->params_location, 1, s->params_end);
    }
  
  tex = s->vframe_tex->user_data;

  glUniform1i(s->tex_location, 0);
  glActiveTexture(GL_TEXTURE0 + 0);
  glBindTexture(GL_TEXTURE_2D, *tex);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
  glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border_colors[s->border_colidx]);
  
  glBindBuffer(GL_ARRAY_BUFFER, s->tex_vbo);
  
  /* Index 0: Position */
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(vertex_t), (void*)0);
  glEnableVertexAttribArray(0);
  
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

  glDisableVertexAttribArray(0);
  glDisableVertexAttribArray(1);

  glUseProgram(0);

  interpolator_update(&s->texture_mat_ip);
  interpolator_update(&s->params_ip);
  
  
  }

static void draw_foreground_vertices(glscope_t * s, int num, GLenum mode,
                                     const float * color)
  {
  //  fprintf(stderr, "draw_foreground_vertices %d\n", num);
  
  glBindBuffer(GL_ARRAY_BUFFER, s->fg_vbo);
  
  /* Update vertex array */
  glBufferSubData(GL_ARRAY_BUFFER, 0, num * sizeof(*s->fg_vertices), s->fg_vertices);

  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(*s->fg_vertices), (void*)0);
  glEnableVertexAttribArray(0);
  
  //  glBufferData(GL_ARRAY_BUFFER, MAX_FG_VERTICES * sizeof(*s->fg_vertices),
  //               s->fg_vertices, GL_DYNAMIC_DRAW);
  
  /* Set color */

  glUniform3fv(s->fg_color_location, 1, color);
  
  /* Draw */

  //  glDrawArrays(GL_POINTS, 0, num);
  glDrawArrays(mode, 0, num);

  glDisableVertexAttribArray(0);
  }

static const GLfloat * select_flash_color()
  {
  int idx = -1;
  int idx_sub = -1;

  select_option(num_fg_colors, &idx);
  select_option(2, &idx_sub);
  return fg_colors[idx][idx_sub];
  }

static void draw_flash(glscope_t * s)
  {
  if(!interpolator_running(&s->flash_ip))
    {
    /* Check whether to flash */
    
    if(s->beat_detected && decide(0.2))
      {
      s->flash_style = -1;

      select_option(FLASH_MODE_MAX, &s->flash_style);

      //      fprintf(stderr, "Init flash\n");
      
      //      s->flash_style = FLASH_MODE_PAINTER;
      // s->flash_style = FLASH_MODE_CROSS;
      
      glBindBuffer(GL_ARRAY_BUFFER, s->flash_vbo);
      
      switch(s->flash_style)
        {
        case FLASH_MODE_CROSS: // Cross (x-y)
          {
          int i;
          const GLfloat * color = select_flash_color();
          
          //  fprintf(stderr, "draw_foreground_vertices %d\n", num);
          s->num_flash_vertices = 4;
          
          s->flash_vertices[0].pos[0] = -1.0;
          s->flash_vertices[0].pos[1] = 0.0;

          s->flash_vertices[1].pos[0] = 1.0;
          s->flash_vertices[1].pos[1] = 0.0;
          
          s->flash_vertices[2].pos[0] = 0.0;
          s->flash_vertices[2].pos[1] = -1.0;

          s->flash_vertices[3].pos[0] = 0.0;
          s->flash_vertices[3].pos[1] = 1.0;

          for(i = 0; i < 4; i++)
            {
            memcpy(s->flash_vertices[i].color, color, 3 * sizeof(*color));
            s->flash_vertices[i].color[3] = 1.0;
            }
          
          interpolator_init(&s->flash_ip, 10, 20);

          s->flash_draw_mode = GL_LINES;
          s->flash_size = 20.0;
          }
          break;
        case FLASH_MODE_CROSS_DIAGONAL: // Cross diagonal
          {
          int i;
          const GLfloat * color = select_flash_color();
          
          //  fprintf(stderr, "draw_foreground_vertices %d\n", num);
          s->num_flash_vertices = 4;
          
          s->flash_vertices[0].pos[0] = -1.0;
          s->flash_vertices[0].pos[1] = -1.0;

          s->flash_vertices[1].pos[0] = 1.0;
          s->flash_vertices[1].pos[1] = 1.0;
          
          s->flash_vertices[2].pos[0] = 1.0;
          s->flash_vertices[2].pos[1] = -1.0;

          s->flash_vertices[3].pos[0] = -1.0;
          s->flash_vertices[3].pos[1] = 1.0;

          for(i = 0; i < 4; i++)
            {
            memcpy(s->flash_vertices[i].color, color, 3 * sizeof(*color));
            s->flash_vertices[i].color[3] = 1.0;
            }
          
          interpolator_init(&s->flash_ip, 10, 20);

          s->flash_draw_mode = GL_LINES;
          s->flash_size = 20.0;
          }
          break;
        case FLASH_MODE_STARS: // Stars
          {
          int i;

          for(i = 0; i < MAX_FLASH_VERTICES; i++)
            {
            s->flash_vertices[i].pos[0] = -1.0 + 2.0 * (double)rand() / (double)RAND_MAX;
            s->flash_vertices[i].pos[1] = -1.0 + 2.0 * (double)rand() / (double)RAND_MAX;
            
            s->flash_vertices[i].color[0] = 1.0 - 0.2 * ((double)rand() / (double)RAND_MAX);
            s->flash_vertices[i].color[1] = 1.0 - 0.2 * ((double)rand() / (double)RAND_MAX);
            s->flash_vertices[i].color[2] = 1.0 - 0.2 * ((double)rand() / (double)RAND_MAX);
            s->flash_vertices[i].color[3] = 0.9;
            }
          s->num_flash_vertices = MAX_FLASH_VERTICES;
          
          interpolator_init(&s->flash_ip, 60, 120);
          
          s->flash_draw_mode = GL_POINTS;
          s->flash_size = 4.0;
          }
          break;
        case FLASH_MODE_POLYGON: // Polygon
          {
          int i;
          const GLfloat * color = select_flash_color();

          float angle = 0.5 * M_PI + decide(0.5) * M_PI;
          
          s->num_flash_vertices = -1;

          select_option(6, &s->num_flash_vertices);

          s->num_flash_vertices += 3;

          for(i = 0; i < s->num_flash_vertices; i++)
            {
            s->flash_vertices[i].pos[0] = 0.8 * cos(angle) / s->aspect;
            s->flash_vertices[i].pos[1] = 0.8 * sin(angle);

            memcpy(s->flash_vertices[i].color, color, 3 * sizeof(*color));
            s->flash_vertices[i].color[3] = 1.0;
            
            angle += 2.0 * M_PI / s->num_flash_vertices;
            }

          interpolator_init(&s->flash_ip, 10, 30);
          
          s->flash_draw_mode = GL_LINE_LOOP;
          s->flash_size = 10.0;
          break;
          }
        case FLASH_MODE_STAR: // Star
          {
          int corners = -1;
          const GLfloat * color = select_flash_color();
          float r1 = 0.0;
          int i;
          float angle = 0.5 * M_PI + decide(0.5) * M_PI;
          
          select_option(4, &corners);
          corners += 3;
          r1 = 0.25 + 0.5 * ((float)rand() / (float)RAND_MAX);

          s->num_flash_vertices = 2 * corners;
          
          for(i = 0; i < corners; i++)
            {
            angle = 2*i * (2.0 * M_PI / (2*corners)) + 0.5 * M_PI;
            
            s->flash_vertices[2*i].pos[0] = 0.8 * cos(angle) / s->aspect;
            s->flash_vertices[2*i].pos[1] = 0.8 * sin(angle);

            memcpy(s->flash_vertices[2*i].color, color, 3 * sizeof(*color));
            s->flash_vertices[2*i].color[3] = 1.0;

            angle += 2.0 * M_PI / (2*corners);
            
            s->flash_vertices[2*i+1].pos[0] = r1 * 0.8 * cos(angle) / s->aspect;
            s->flash_vertices[2*i+1].pos[1] = r1 * 0.8 * sin(angle);

            memcpy(s->flash_vertices[2*i+1].color, color, 3 * sizeof(*color));
            s->flash_vertices[2*i+1].color[3] = 1.0;
            
            angle += 2.0 * M_PI / (2*corners);
            }
          
          s->flash_draw_mode = GL_LINE_LOOP;
          s->flash_size = 10.0;
          interpolator_init(&s->flash_ip, 10, 30);
          }
          break;
        case FLASH_MODE_RANDOM: // Random
          {
          int i;
          const GLfloat * color = select_flash_color();
          
          s->num_flash_vertices = -1;
          select_option(10, &s->num_flash_vertices);
          s->num_flash_vertices += 3;

          for(i = 0; i < s->num_flash_vertices; i++)
            {
            s->flash_vertices[i].pos[0] = -0.5 + ((float)rand()/RAND_MAX);
            s->flash_vertices[i].pos[1] = -0.5 + ((float)rand()/RAND_MAX);
            
            memcpy(s->flash_vertices[i].color, color, 3 * sizeof(*color));
            s->flash_vertices[i].color[3] = 1.0;
            }
          
          s->flash_draw_mode = GL_LINE_LOOP;
          s->flash_size = 5.0;
          interpolator_init(&s->flash_ip, 10, 30);
          break;
          }
        case FLASH_MODE_HLINES: // Horizontal lines 
          {
          int i;
          const GLfloat * color = select_flash_color();
          int num_lines = -1;

          float start, delta, pos;
          
          select_option(3, &num_lines);
          num_lines += 1;
          
          num_lines *= 2;

          s->num_flash_vertices = num_lines * 2;

          start = -0.8;
          delta = 1.6 / (num_lines - 1);
          pos = start;
          
          for(i = 0; i < num_lines ; i++)
            {
            s->flash_vertices[2*i].pos[0] = -0.8;
            s->flash_vertices[2*i].pos[1] = pos;

            s->flash_vertices[2*i + 1].pos[0] = 0.8;
            s->flash_vertices[2*i + 1].pos[1] = pos;

            memcpy(s->flash_vertices[2*i].color, color, 3 * sizeof(*color));
            s->flash_vertices[2*i].color[3] = 1.0;
            
            memcpy(s->flash_vertices[2*i+1].color, color, 3 * sizeof(*color));
            s->flash_vertices[2*i+1].color[3] = 1.0;

            pos += delta;
            }
          
          s->flash_draw_mode = GL_LINES;
          s->flash_size = 5.0;
          interpolator_init(&s->flash_ip, 10, 30);
          break;
          }
        case FLASH_MODE_VLINES: // Vertical lines 
          {
          int i;
          const GLfloat * color = select_flash_color();
          int num_lines = -1;

          float start, delta, pos;
          
          select_option(3, &num_lines);
          num_lines += 1;
          
          num_lines *= 2;

          s->num_flash_vertices = num_lines * 2;

          start = -0.8;
          delta = 1.6 / (num_lines - 1);
          pos = start;
          
          for(i = 0; i < num_lines ; i++)
            {
            s->flash_vertices[2*i].pos[1] = -0.8;
            s->flash_vertices[2*i].pos[0] = pos;

            s->flash_vertices[2*i + 1].pos[1] = 0.8;
            s->flash_vertices[2*i + 1].pos[0] = pos;

            memcpy(s->flash_vertices[2*i].color, color, 3 * sizeof(*color));
            s->flash_vertices[2*i].color[3] = 1.0;
            
            memcpy(s->flash_vertices[2*i+1].color, color, 3 * sizeof(*color));
            s->flash_vertices[2*i+1].color[3] = 1.0;

            pos += delta;
            }
          
          s->flash_draw_mode = GL_LINES;
          s->flash_size = 5.0;
          interpolator_init(&s->flash_ip, 10, 30);
          break;
          }
        case FLASH_MODE_PAINTER: // Painter
          {
          int i, j;
          int color_index = -1;
          int num_painters = -1;
          
          float radius = 0.2 + 0.5 * (float)(rand()) / RAND_MAX;

          
          //          select_option(MAX_PAINTERS-3, &num_painters); // 0 .. 2
          //          num_painters += 4; // 4 .. 6
          num_painters = MAX_PAINTERS;
          
          
          s->num_flash_vertices = num_painters * 8;

          select_option(num_fg_colors, &color_index);
          
          for(i = 0; i < num_painters; i++)
            {
            float angle = ((float)rand() / RAND_MAX) * 2.0 * M_PI;

            /* A painter is a cross with the alpha decaying from the center outwards */
            
            /* They are drawn with GL_LINES, even indices are the center */
            
            s->painters[i].coords[0] = radius * cos(angle) / s->aspect;
            s->painters[i].coords[1] = radius * sin(angle);

            angle = ((float)rand() / RAND_MAX) * 2.0 * M_PI;
            
            s->painters[i].dcoords[0] = -0.01 * sin(angle);
            s->painters[i].dcoords[1] = 0.01 * cos(angle);

            angle += 2.0 * M_PI / num_painters;
            
            /* Initialize vertex colors */

            memcpy(s->flash_vertices[8 * i + 0].color, fg_colors[color_index][decide(0.5)],
                   3 * sizeof(s->flash_vertices[8 * i + 0].color[0]));
            
            //            s->flash_vertices[8 * i + 0].color[0] = 1.0 - 0.3 * (float)(rand()) / RAND_MAX;
            //            s->flash_vertices[8 * i + 0].color[1] = 1.0 - 0.3 * (float)(rand()) / RAND_MAX;
            //            s->flash_vertices[8 * i + 0].color[2] = 1.0 - 0.3 * (float)(rand()) / RAND_MAX;

            for(j = 1; j < 8; j++)
              {
              memcpy(s->flash_vertices[8 * i + j].color,
                     s->flash_vertices[8 * i + 0].color,
                     3 * sizeof(s->flash_vertices[8 * i + 0].color[0]));
              }
            
            for(j = 0; j < 4; j++)
              {
              s->flash_vertices[8*i + 2*j].color[3] = 1.0;
              s->flash_vertices[8*i + 2*j + 1].color[3] = 0.5;
              }
            }

          interpolator_init(&s->flash_ip, 100, 300);
          s->flash_draw_mode = GL_LINES;
          s->flash_size = PAINTERS_MIN_WIDTH;
          break;
          }
        case FLASH_MODE_PARAMETRIC:
          {
          int i;
          int color_index = -1;
          float fac;
          int color_fac = -1;
          
          select_option(num_fg_colors, &color_index);
          select_option(3, &color_fac);
          color_fac++;
          
          interpolator_init(&s->flash_ip, 100, 300);
          s->flash_draw_mode = GL_LINE_LOOP;
          s->flash_size = PAINTERS_MIN_WIDTH;

          parametric_init(&s->parametric[0]);
          parametric_init(&s->parametric[1]);

          s->num_flash_vertices = MAX_FLASH_VERTICES;
          
          for(i = 0; i < MAX_FLASH_VERTICES; i++)
            {
            fac = (float)i / MAX_FLASH_VERTICES;
            
            fac = 0.5 * (1.0 + sin(2.0 * M_PI * color_fac * fac));
            
            s->flash_vertices[i].color[0] =
              fg_colors[color_index][0][0] * fac + fg_colors[color_index][1][0] * (1.0 - fac);

            s->flash_vertices[i].color[1] =
              fg_colors[color_index][0][1] * fac + fg_colors[color_index][1][1] * (1.0 - fac);

            s->flash_vertices[i].color[2] =
              fg_colors[color_index][0][2] * fac + fg_colors[color_index][1][2] * (1.0 - fac);
            
            s->flash_vertices[i].color[3] = 1.0;
            }
          
          }
        }

      switch(s->flash_style)
        {
        case FLASH_MODE_PAINTER:
        case FLASH_MODE_PARAMETRIC:
          break;
        default:
          /* Update vertex array */
          glBufferSubData(GL_ARRAY_BUFFER, 0, s->num_flash_vertices * sizeof(*s->flash_vertices), s->flash_vertices);
          break;
        }
      
      }
    
    }

  if(!interpolator_running(&s->flash_ip))
    return;
  
  /* Draw */

  //  fprintf(stderr, "Draw flash %d\n", s->num_flash_vertices);


  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  
  glUseProgram(s->flash_program);

  /* Update alpha */
  switch(s->flash_style)
    {
    case FLASH_MODE_PAINTER:
    case FLASH_MODE_PARAMETRIC:
      glUniform1f(s->flash_alpha_location, 1.0);
      break;
    default:
      glUniform1f(s->flash_alpha_location, 1.0 - interpolator_get(&s->flash_ip));
      break;
    }
      
  glBindBuffer(GL_ARRAY_BUFFER, s->flash_vbo);

  /* Update buffer */

  switch(s->flash_style)
    {
    case FLASH_MODE_PAINTER:
      {
      float angle;
      int i;
      float cross_size;
      int num_painters = s->num_flash_vertices / 8;

      if(s->beat_detected)
        s->flash_size += 0.05 * s->beat_volume;

      /* Decay */
      if(s->flash_size > PAINTERS_MIN_WIDTH)
        s->flash_size = PAINTERS_MIN_WIDTH + 0.95 * (s->flash_size - PAINTERS_MIN_WIDTH);

      //      fprintf(stderr, "Update painter %f\n", s->flash_size);
      
      cross_size = 0.015 * s->flash_size;
        
      for(i = 0; i < num_painters; i++)
        {
        angle = (float)(s->frame_counter % 5) * (M_PI * 0.5 / 5.0);
        
        s->painters[i].coords[0] += s->painters[i].dcoords[0];
        s->painters[i].coords[1] += s->painters[i].dcoords[1];

        s->flash_vertices[8*i].pos[0] = s->painters[i].coords[0];
        s->flash_vertices[8*i].pos[1] = s->painters[i].coords[1];

        s->flash_vertices[8*i+1].pos[0] = s->flash_vertices[8*i].pos[0] + cross_size * cos(angle);
        s->flash_vertices[8*i+1].pos[1] = s->flash_vertices[8*i].pos[1] + cross_size * sin(angle) * s->aspect;

        angle += M_PI * 0.5;
        
        s->flash_vertices[8*i+2].pos[0] = s->flash_vertices[8*i].pos[0];
        s->flash_vertices[8*i+2].pos[1] = s->flash_vertices[8*i].pos[1];

        s->flash_vertices[8*i+3].pos[0] = s->flash_vertices[8*i].pos[0] + cross_size * cos(angle);
        s->flash_vertices[8*i+3].pos[1] = s->flash_vertices[8*i].pos[1] + cross_size * sin(angle) * s->aspect;

        angle += M_PI * 0.5;
                
        s->flash_vertices[8*i+4].pos[0] = s->flash_vertices[8*i].pos[0];
        s->flash_vertices[8*i+4].pos[1] = s->flash_vertices[8*i].pos[1];
        
        s->flash_vertices[8*i+5].pos[0] = s->flash_vertices[8*i].pos[0] + cross_size * cos(angle);
        s->flash_vertices[8*i+5].pos[1] = s->flash_vertices[8*i].pos[1] + cross_size * sin(angle) * s->aspect;

        angle += M_PI * 0.5;
        
        s->flash_vertices[8*i+6].pos[0] = s->flash_vertices[8*i].pos[0];
        s->flash_vertices[8*i+6].pos[1] = s->flash_vertices[8*i].pos[1];

        s->flash_vertices[8*i+7].pos[0] = s->flash_vertices[8*i].pos[0] + cross_size * cos(angle);
        s->flash_vertices[8*i+7].pos[1] = s->flash_vertices[8*i].pos[1] + cross_size * sin(angle) * s->aspect;
        }
      
      glBufferSubData(GL_ARRAY_BUFFER, 0, s->num_flash_vertices * sizeof(*s->flash_vertices), s->flash_vertices);
      }
      break;
    case FLASH_MODE_PARAMETRIC:
      {
      int i;
      float x_1 = 0.0, y_1 = 0.0, x_2 = 0.0, y_2 = 0.0;
      float t;
      float fac = interpolator_get(&s->flash_ip);

      for(i = 0; i < MAX_FLASH_VERTICES; i++)
        {
        t = (float)i / MAX_FLASH_VERTICES;

        get_parametric(&s->parametric[0], t, &x_1, &y_1);
        get_parametric(&s->parametric[1], t, &x_2, &y_2);
        s->flash_vertices[i].pos[0] = 0.9 * (fac * x_1 + (1.0 - fac) * x_2) / s->aspect;
        s->flash_vertices[i].pos[1] = 0.9 * (fac * y_1 + (1.0 - fac) * y_2);
        }
      
      glBufferSubData(GL_ARRAY_BUFFER, 0, MAX_FLASH_VERTICES * sizeof(*s->flash_vertices), s->flash_vertices);
      }
      break;
    default:
      break;
    }

  if(s->flash_draw_mode == GL_POINTS)
    glPointSize(s->flash_size);
  else
    glLineWidth(s->flash_size);
  
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(*s->flash_vertices), (void*)0);
  glEnableVertexAttribArray(0);

  glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(*s->flash_vertices),
                        (void*)(offsetof(flash_vertex_t, color)));
  glEnableVertexAttribArray(1);

  glDrawArrays(s->flash_draw_mode, 0, s->num_flash_vertices);

  glUseProgram(0);

  glDisableVertexAttribArray(0);
  glDisableVertexAttribArray(1);

  glDisable(GL_BLEND);
  
  interpolator_update(&s->flash_ip);
  
  }

static void draw_fg_scope_h(glscope_t * s)
  {
  int i;

  glLineWidth(2.0 + s->beat_detected * 2.0);
  
  for(i = 0; i < FRAME_SAMPLES; i++)
    {
    s->fg_vertices[i].pos[0] = (((double)i / FRAME_SAMPLES) - 0.5) * 2.0;
    s->fg_vertices[i].pos[1] = s->aframe->channels.f[0][i] * 0.4 + 0.5;
    }
  draw_foreground_vertices(s, FRAME_SAMPLES, GL_LINE_STRIP, fg_colors[s->fg_colidx][s->fg_colidx_sub]);

  for(i = 0; i < FRAME_SAMPLES; i++)
    {
    s->fg_vertices[i].pos[1] = s->aframe->channels.f[1][i] * 0.4 - 0.5;
    }
  
  draw_foreground_vertices(s, FRAME_SAMPLES, GL_LINE_STRIP, fg_colors[s->fg_colidx][1-s->fg_colidx_sub]);
  }

static void draw_fg_scope_v(glscope_t * s)
  {
  int i;

  glLineWidth(2.0 + s->beat_detected * 2.0);
  
  for(i = 0; i < FRAME_SAMPLES; i++)
    {
    s->fg_vertices[i].pos[1] = (((double)i / FRAME_SAMPLES) - 0.5) * 2.0;
    s->fg_vertices[i].pos[0] = s->aframe->channels.f[0][i] * 0.4 + 0.3;
    }
  draw_foreground_vertices(s, FRAME_SAMPLES, GL_LINE_STRIP, fg_colors[s->fg_colidx][s->fg_colidx_sub]);

  for(i = 0; i < FRAME_SAMPLES; i++)
    {
    s->fg_vertices[i].pos[0] = s->aframe->channels.f[1][i] * 0.4 - 0.3;
    }
  
  draw_foreground_vertices(s, FRAME_SAMPLES, GL_LINE_STRIP, fg_colors[s->fg_colidx][1-s->fg_colidx_sub]);
  }

static void draw_fg_vectorscope_line(glscope_t * s)
  {
  int i;
  glLineWidth(2.0 + s->beat_detected * 2.0);
  
  for(i = 0; i < FRAME_SAMPLES; i++)
    {
    s->fg_vertices[i].pos[1] = 0.707 * (s->aframe->channels.f[0][i] + s->aframe->channels.f[1][i]);
    s->fg_vertices[i].pos[0] = 0.707 * (s->aframe->channels.f[0][i] - s->aframe->channels.f[1][i]);
    }
  
  draw_foreground_vertices(s, FRAME_SAMPLES, GL_LINE_STRIP, fg_colors[s->fg_colidx][s->fg_colidx_sub]);
  }

static void draw_fg_vectorscope_dots(glscope_t * s)
  {
  int i;
  glPointSize(2.0 + s->beat_detected * 2.0);
  
  for(i = 0; i < FRAME_SAMPLES; i++)
    {
    s->fg_vertices[i].pos[1] = 0.707 * (s->aframe->channels.f[0][i] + s->aframe->channels.f[1][i]);
    s->fg_vertices[i].pos[0] = 0.707 * (s->aframe->channels.f[0][i] - s->aframe->channels.f[1][i]);
    }
  
  draw_foreground_vertices(s, FRAME_SAMPLES, GL_POINTS, fg_colors[s->fg_colidx][s->fg_colidx_sub]);
  }

#define NUM_FOREGROUNDS 4

static void draw_foreground(glscope_t * s)
  {
  if(s->beat_detected)
    {
    if(decide(0.2))
      {
      select_option(NUM_FOREGROUNDS, &s->fg_idx);
      select_option(num_fg_colors, &s->fg_colidx);
      select_option(2, &s->fg_colidx_sub);
      }
    else if(decide(0.2))
      {
      select_option(num_fg_colors, &s->fg_colidx);
      select_option(2, &s->fg_colidx_sub);
      }

    }

  glUseProgram(s->fg_program);

  switch(s->fg_idx)
    {
    case 0:
      draw_fg_scope_h(s);
      break;
    case 1:
      draw_fg_scope_v(s);
      break;
    case 2:
      draw_fg_vectorscope_line(s);
      break;
    case 3:
      draw_fg_vectorscope_dots(s);
      break;
    }
  
  glUseProgram(0);
  }

static void draw_scope(glscope_t * s)
  {
  draw_texture(s);

  draw_flash(s);
  
  draw_foreground(s);
  
  s->beat_detected = 0;
  s->beat_volume = -20.0;
  
  }

static gavl_source_status_t render_frame(void * priv, gavl_video_frame_t ** frame)
  {
  GLuint * tex;
  glscope_t * s = priv;
  
  //  fprintf(stderr, "Render frame\n");

  gavl_hw_egl_set_current(s->hwctx, EGL_NO_SURFACE);
  
  glBindFramebuffer(GL_FRAMEBUFFER, s->frame_buffer);

  /* Attach texture image */
  tex = s->vframe_cur->user_data;
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, *tex, 0);
  glDrawBuffer(GL_COLOR_ATTACHMENT0);
  
  glViewport(0, 0, s->vfmt.image_width, s->vfmt.image_height);

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  
  glClearColor (0.0, 0.0, 0.0, 1.0);
  glClear(GL_COLOR_BUFFER_BIT);
  
  /* Draw image */
  
  draw_scope(s);
  
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  gavl_hw_egl_unset_current(s->hwctx);
  
  *frame = s->vframe_cur;

  if(s->vframe_cur == s->vframe_1)
    {
    s->vframe_cur = s->vframe_2;
    s->vframe_tex = s->vframe_1;
    }
  else
    {
    s->vframe_cur = s->vframe_1;
    s->vframe_tex = s->vframe_2;
    }

  s->frame_counter++;
  
  return GAVL_SOURCE_OK;
  }


static const int default_attributes[] =
  {
    EGL_RED_SIZE,   8,
    EGL_GREEN_SIZE, 8,
    EGL_BLUE_SIZE,  8,
    EGL_ALPHA_SIZE, 8,
    EGL_NONE
  };


static void create_vertex_buffers(glscope_t * s)
  {
  /* Texture */
  
  glGenBuffers(1, &s->tex_vbo);

  glBindBuffer(GL_ARRAY_BUFFER, s->tex_vbo);
  
  glBufferData(GL_ARRAY_BUFFER, sizeof(tex_vertices), tex_vertices, GL_STATIC_DRAW);

  /* Index 0: Position */
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(vertex_t), (void*)0);
  glEnableVertexAttribArray(0);
  
  /* Foreground */

  s->fg_vertices = calloc(MAX_FG_VERTICES, sizeof(*s->fg_vertices));
  
  glGenBuffers(1, &s->fg_vbo);
  glBindBuffer(GL_ARRAY_BUFFER, s->fg_vbo);

  glBufferData(GL_ARRAY_BUFFER, MAX_FG_VERTICES * sizeof(*s->fg_vertices),
               s->fg_vertices, GL_DYNAMIC_DRAW);

  /* Flash */

  s->flash_vertices = calloc(MAX_FLASH_VERTICES, sizeof(*s->flash_vertices));

  glGenBuffers(1, &s->flash_vbo);
  glBindBuffer(GL_ARRAY_BUFFER, s->flash_vbo);

  glBufferData(GL_ARRAY_BUFFER, MAX_FLASH_VERTICES * sizeof(*s->flash_vertices),
               s->flash_vertices, GL_DYNAMIC_DRAW);
  
  
  }

static void check_shader(GLuint shader, const char * name)
  {
  GLint isCompiled = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &isCompiled);
  if(isCompiled == GL_FALSE)
    {
    char * msg;
    GLint maxLength = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &maxLength);

    // The maxLength includes the NULL character

    msg = malloc(maxLength);
        
    glGetShaderInfoLog(shader, maxLength, &maxLength, msg);
    
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

static void check_program(GLuint program, const char * name)
  {
  GLint isCompiled = 0;
  glGetProgramiv(program, GL_LINK_STATUS, &isCompiled);
  if(isCompiled == GL_FALSE)
    {
    char * msg;
    GLint maxLength = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &maxLength);

    // The maxLength includes the NULL character

    msg = malloc(maxLength);
        
    glGetProgramInfoLog(program, maxLength, &maxLength, msg);
    
    // Provide the infolog in whatever manor you deem best.
    // Exit with failure.
    glDeleteProgram(program); // Don't leak the shader.

    fprintf(stderr, "%s error: %s\n", name, msg);
    free(msg);
    return;
    }
#if 0
  else
    {
    fprintf(stderr, "%s linked fine\n", name);
    }
#endif
  }


static void create_fg_shaders(glscope_t * s)
  {
  s->fg_program = glCreateProgram();
  
  /* Vertex shader */
  s->fg_vshader = glCreateShader(GL_VERTEX_SHADER);

  glShaderSource(s->fg_vshader, 1, &vertex_shader_fg, NULL);

  glCompileShader(s->fg_vshader);
  glAttachShader(s->fg_program, s->fg_vshader);
  
  /* Fragment shader */
  s->fg_fshader = glCreateShader(GL_FRAGMENT_SHADER);
  
  glShaderSource(s->fg_fshader,
                 1, &fragment_shader_fg, NULL);

  glCompileShader(s->fg_fshader);
  glAttachShader(s->fg_program, s->fg_fshader);

  check_shader(s->fg_fshader, "fg_fshader");
  check_shader(s->fg_vshader, "fg_vshader");
  
  glBindAttribLocation(s->fg_program, 0, "pos");
  glBindFragDataLocation(s->fg_program, 0, "colorOut");
  
  glLinkProgram(s->fg_program);

  check_program(s->fg_program, "fg_program");
  
  s->fg_color_location = glGetUniformLocation(s->fg_program, "fgcolor");

  }

static void create_flash_shaders(glscope_t * s)
  {
  s->flash_program = glCreateProgram();
  
  /* Vertex shader */
  s->flash_vshader = glCreateShader(GL_VERTEX_SHADER);

  glShaderSource(s->flash_vshader, 1, &vertex_shader_flash, NULL);

  glCompileShader(s->flash_vshader);
  glAttachShader(s->flash_program, s->flash_vshader);
  
  /* Fragment shader */
  s->flash_fshader = glCreateShader(GL_FRAGMENT_SHADER);
  
  glShaderSource(s->flash_fshader,
                 1, &fragment_shader_flash, NULL);

  glCompileShader(s->flash_fshader);
  glAttachShader(s->flash_program, s->flash_fshader);

  check_shader(s->flash_fshader, "flash_fshader");
  check_shader(s->flash_vshader, "flash_vshader");
  
  glBindAttribLocation(s->flash_program, 0, "pos");
  glBindAttribLocation(s->flash_program, 1, "colorIn");
  
  glBindFragDataLocation(s->flash_program, 0, "colorOut");
  
  glLinkProgram(s->flash_program);

  check_program(s->flash_program, "flash_program");

  s->flash_alpha_location = glGetUniformLocation(s->flash_program, "flashAlpha");
  
  }

static void new_colormatrix(glscope_t * s)
  {
  select_option(num_colormatrices, &s->colormatrix_idx);

  //  fprintf(stderr, "New colormatrix %d\n", s->colormatrix_idx);
  
  glUseProgram(s->tex_program);
  
  glUniformMatrix4fv(s->colormatrix_location,
                     1, GL_TRUE, colormatrices[s->colormatrix_idx].matrix);

  glUniform4fv(s->coloroffset_location, 1, colormatrices[s->colormatrix_idx].offset);
  }

static void new_tex_transform(glscope_t * s)
  {
  const char * source[3];
  
  if(s->tex_program)
    glDeleteProgram(s->tex_program);
  if(s->tex_fshader)
    glDeleteProgram(s->tex_fshader);
  
  s->tex_program = glCreateProgram();

  /* Fragment shader */
  s->tex_fshader = glCreateShader(GL_FRAGMENT_SHADER);

  select_option(num_fragment_shader_transforms, &s->transform_idx);
  
  source[0] = fragment_shader_tex_head;
  source[1] = fragment_shader_transforms[s->transform_idx].code;
  source[2] = fragment_shader_tex_tail;
  
  glShaderSource(s->tex_fshader,
                 3, source, NULL);
  
  glCompileShader(s->tex_fshader);
  
  check_shader(s->tex_fshader, "tex_fshader");

  glAttachShader(s->tex_program, s->tex_vshader);
  glAttachShader(s->tex_program, s->tex_fshader);
  
  glLinkProgram(s->tex_program);
  
  s->tex_location = glGetUniformLocation(s->fg_program, "texsampler");

  s->colormatrix_location = glGetUniformLocation(s->tex_program, "colormatrix");
  s->coloroffset_location = glGetUniformLocation(s->tex_program, "coloroffset");

  s->texmatrix_location = glGetUniformLocation(s->tex_program, "texmatrix");
  s->texoffset_location = glGetUniformLocation(s->tex_program, "texoffset");
  s->texaspect_location = glGetUniformLocation(s->tex_program, "aspect");

  s->params_location = glGetUniformLocation(s->tex_program, "params");
  
  glBindFragDataLocation(s->tex_program, 0, "colorOut");

  if(fragment_shader_transforms[s->transform_idx].flags & TRANSFORM_ALLOW_MATRIX)
    {
    s->texture_mat_start = -1;
    select_option(num_texture_matrices, &s->texture_mat_start);
    }
  else
    {
    s->texture_mat_start = -1;
    }

  if(fragment_shader_transforms[s->transform_idx].flags & TRANSFORM_HAS_PARAMS)
    {
    params_init(s->params_end);
    }
  
  s->texture_mat_end = -1;
  
  new_colormatrix(s);
  
  }

static void create_tex_shaders(glscope_t * s)
  {
  /* Vertex shader */
  s->tex_vshader = glCreateShader(GL_VERTEX_SHADER);

  glShaderSource(s->tex_vshader, 1, &vertex_shader_tex, NULL);

  glCompileShader(s->tex_vshader);

  check_shader(s->tex_vshader, "tex_vshader");
  
  glBindAttribLocation(s->tex_program, 0, "pos");
  
  new_tex_transform(s);
  
  }

static int open_glscope(void * priv, gavl_audio_format_t * audio_format,
                        gavl_video_format_t * video_format)
  {
  glscope_t * s = priv;
  
  
  gavl_video_format_copy(&s->vfmt, video_format);
  gavl_audio_format_copy(&s->afmt, audio_format);

  //  fprintf(stderr, "open_glscope\n");
  //  gavl_video_format_dump(&s->vfmt);

  s->hwctx = gavl_hw_ctx_create_egl(default_attributes, GAVL_HW_EGL_GL_X11, NULL);
  
  /* Adjust audio format */
  s->afmt.sample_format = GAVL_SAMPLE_FLOAT;
  s->afmt.interleave_mode = GAVL_INTERLEAVE_NONE;
  
  s->afmt.num_channels = 2;
  s->afmt.channel_locations[0] = GAVL_CHID_NONE;
  s->afmt.samples_per_frame = FRAME_SAMPLES;
  
  gavl_set_channel_setup(&s->afmt);

  /* Adjust video format */
  s->vfmt.pixelformat    = GAVL_RGBA_32;
  s->vfmt.hwctx = s->hwctx;
  s->vfmt.pixel_width = 1;
  s->vfmt.pixel_height = 1;
  
  gavl_video_format_set_frame_size(&s->vfmt, 1, 1);
  
  gavl_video_format_copy(video_format, &s->vfmt);
  gavl_audio_format_copy(audio_format, &s->afmt);
  
  s->asink = gavl_audio_sink_create(NULL, put_audio_func, priv, &s->afmt);
  s->vsource = gavl_video_source_create(render_frame,
                                        s, GAVL_SOURCE_SRC_ALLOC,
                                        &s->vfmt);
  
  s->aframe = gavl_audio_frame_create(&s->afmt);

  gavl_peak_detector_set_format(s->pd, &s->afmt);
  
  /* Generate frame buffer */
  
  s->vframe_1 = gavl_hw_video_frame_create_hw(s->hwctx, &s->vfmt);
  s->vframe_2 = gavl_hw_video_frame_create_hw(s->hwctx, &s->vfmt);

  s->vframe_cur = s->vframe_1;
  s->vframe_tex = NULL;
  
  gavl_hw_egl_set_current(s->hwctx, EGL_NO_SURFACE);
  
  /* Generate frame buffer */
  glGenFramebuffers(1, &s->frame_buffer);
  glBindFramebuffer(GL_FRAMEBUFFER, s->frame_buffer);

  /* Create and attach depth buffer */
  glGenRenderbuffers(1, &s->depth_buffer);
  glBindRenderbuffer(GL_RENDERBUFFER, s->depth_buffer);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, s->vfmt.image_width, s->vfmt.image_height);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, s->depth_buffer);
  
  if(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE)
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Created framebuffer");
  else
    gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Creating framebuffer failed");
  
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  /* Set aspect */
  s->aspect = (float)(s->vfmt.image_width) / (float)(s->vfmt.image_height);
  
  /* Create vertex buffers */

  s->transform_idx = -1;

  create_fg_shaders(s);
  create_tex_shaders(s);
  create_flash_shaders(s);
  
  create_vertex_buffers(s);

  /* Initialize options */
  
  s->colormatrix_idx = -1;
  new_colormatrix(s);

  s->border_colidx = -1;
  
  s->fg_colidx = -1;
  s->fg_colidx_sub = -1;
  s->fg_idx = -1;
  
  select_option(num_fg_colors, &s->fg_colidx);
  select_option(num_border_colors, &s->border_colidx);
  
  select_option(2, &s->fg_colidx_sub);
  select_option(NUM_FOREGROUNDS, &s->fg_idx);
  
  gavl_hw_egl_unset_current(s->hwctx);
  
  return 1;
  }

static gavl_audio_sink_t * get_sink_glscope(void * priv)
  {
  glscope_t * s = priv;
  return s->asink;
  }


static gavl_video_source_t * get_source_glscope(void * priv)
  {
  glscope_t * s = priv;
  return s->vsource;
  }

static void * create_glscope()
  {
  glscope_t * s;

  s = calloc(1, sizeof(*s));

  s->pd = gavl_peak_detector_create();
  
  return s;
  }

static void close_glscope(void * priv)
  {
  glscope_t * s = priv;

  if(s->asink)
    {
    gavl_audio_sink_destroy(s->asink);
    s->asink = NULL;
    }
    
  if(s->vsource)
    {
    gavl_video_source_destroy(s->vsource);
    s->vsource = NULL;
    }

  if(s->aframe)
    {
    gavl_audio_frame_destroy(s->aframe);
    s->aframe = NULL;
    }

  if(s->hwctx)
    {
    gavl_hw_ctx_destroy(s->hwctx);
    s->hwctx = NULL;
    }

  if(s->fg_vertices)
    {
    free(s->fg_vertices);
    s->fg_vertices = NULL;
    }
  
  if(s->flash_vertices)
    {
    free(s->flash_vertices);
    s->flash_vertices = NULL;
    }
  
  }


static void destroy_glscope(void * priv)
  {
  glscope_t * s = priv;

  close_glscope(priv);

  if(s->pd)
    gavl_peak_detector_destroy(s->pd);
  
  free(priv);
  }


const bg_visualization_plugin_t the_plugin = 
  {
    .common =
    {
      BG_LOCALE,
      .name =      "vis_glscope",
      .long_name = TRS("GL Scope"),
      .description = TRS("OpenGL scope plugin.\n"
                         "It's unofficial name is Corona since it was written during the Corona pandemia in 2020. "
                         "I decided to use the lockdown for learning OpenGL 3.0 shader programming."),
      .type =     BG_PLUGIN_VISUALIZATION,
      .flags =    0,
      .create =   create_glscope,
      .destroy =   destroy_glscope,
      .priority =         1,
      //      .get_parameters = get_parameters_glscope,
      //      .set_parameter = set_parameter_glscope,
    },
    .open = open_glscope,
    .get_source = get_source_glscope,
    .get_sink = get_sink_glscope,
    .close = close_glscope
  };

/* Include this into all plugin modules exactly once
   to let the plugin loader obtain the API version */
BG_GET_PLUGIN_API_VERSION;

/* Foreground */

#define GLSL_HEADER "#version 120\n"

// #define GLSL_HEADER ""

static const char * vertex_shader_fg =
  GLSL_HEADER
  "attribute vec2 pos;"
  "void main()"
  " {"
  " gl_Position = vec4(pos, 0.0, 1.0);"
  " }";

static const char * fragment_shader_fg =
  GLSL_HEADER
  "uniform vec3 fgcolor;"
  "void main()"
  "  {"
  "  gl_FragColor = vec4(fgcolor, 1.0);"
  "  }";

/* Flash */

static const char * vertex_shader_flash =
  GLSL_HEADER
  "attribute vec2 pos;"
  "attribute vec4 colorIn;"
  "uniform float flashAlpha;"
  "varying vec4 color;"
  "void main()"
  " {"
  " gl_Position = vec4(pos, 0.0, 1.0);"
  " color = vec4(colorIn.xyz, colorIn.w * flashAlpha);"
  " }";

static const char * fragment_shader_flash =
  GLSL_HEADER
  "varying vec4 color;" 
  "void main()"
  "  {"
  "  gl_FragColor = color;"
  "  }";


/* Texture */

static const char * vertex_shader_tex =
  GLSL_HEADER
  "attribute vec2 pos;"
  "varying vec2 TexCoord;"
  "void main()"
  " {"
  " gl_Position = vec4(pos, 0.0, 1.0);"
  " TexCoord = pos;"
  " }";

static const char * fragment_shader_tex_head =
  GLSL_HEADER
  "varying vec2 TexCoord;" 
  "uniform sampler2D texsampler;"
  "uniform mat4x4 colormatrix;"
  "uniform vec4 coloroffset = vec4(0.0, 0.0, 0.0, 0.0);"
  "uniform mat2x2 texmatrix = "
  "  mat2(1.0, 0.0,"
  "       0.0, 1.0);"
  "uniform vec2 texoffset = vec2(0.0, 0.0);"
  "uniform vec4 params = vec4(-1.2,1.0,1.2,1.0);"
  "uniform float aspect = 1.0;"
  
  "void main()"
  "  {"
  "  vec2 a = texmatrix * TexCoord + texoffset;"
  "  a.x *= aspect;"
  "  vec2 b;";

static const char * fragment_shader_tex_tail =
  "  b.x = ((b.x/aspect + 1.0) * 0.5);"
  "  b.y = (b.y + 1.0) * 0.5;"
  "  gl_FragColor = colormatrix * texture2D(texsampler, b) + coloroffset;"
  "  }";



static const texture_transform_t fragment_shader_transforms[] =
  {
#if 1
    {
    /* selective zoom 1 */
    "  float len = length(a);"
    "  b.x = a.x * (1.04 - len*len * 0.1);"
    "  b.y = a.y * (1.04 - len*len * 0.1);",
    },
    {
    /* selective zoom 2 */
    "  float len = length(a);"
    "  b.x = a.x * (0.91 + len*len * 0.1);"
    "  b.y = a.y * (0.91 + len*len * 0.1);",
    },
    {
    /* sine circular */
    "  float len = length(a);"
    "  len = sin(len * 20);"
    "  b.x = a.x * (1.0 + len * 0.007);"
    "  b.y = a.y * (1.0 + len * 0.007);",
    TRANSFORM_ALLOW_MATRIX,
    },
    {
    /* zoom out fast */
    "  b.x = 1.06 * a.x;"
    "  b.y = 1.06 * a.y;",
    },
#endif
    {
    /* zoom in fast */
    "  b.x = 0.94 * a.x;"
    "  b.y = 0.94 * a.y;",
    },
    {
    /* vector field 1 */
      "  b.x = a.x + 0.030 * (2.0*(-0.5+params.x)* a.x * a.x + 2.0*(-0.5+params.y)* a.y * a.y);"
    "  b.y = a.y - 0.016 * 2.0*(-0.5+params.z) * a.x * a.y * 4.0;",
    //    0,
    TRANSFORM_ALLOW_MATRIX | TRANSFORM_HAS_PARAMS
    },
#if 1
    {
    /* vector field 2 */
    "  b.x = a.x + 0.01 * sin(params.x*2.0+(a.x + a.y)*2.0 * (1.0 + params.y));"
    "  b.y = a.y + 0.01 * cos(params.z*2.0+(a.x - a.y)*2.0 * (1.0 + params.w));",
    //    0,
    //    TRANSFORM_ALLOW_MATRIX,
    TRANSFORM_HAS_PARAMS | TRANSFORM_ALLOW_MATRIX
    },
    {
    /* vector field 3 */
    "  vec2 c1 = vec2(-1.0 + 2.0 * params.x, 1.2);"
    "  float q1 = (-1.0 + 2.0 * params.y) * 0.04;"
    "  vec2 c2 = vec2(-1.0 + 2.0 * params.z, -1.2);"
    "  float q2 = (-1.0 + 2.0 * params.w) * 0.04;"
    "  vec2 e;"
    "  float len;"
    "  e = a - c1;"
    "  len = length(e);"
    "  e *= -q1 / (len);"
    "  b = a + e;"
    "  e = a - c2;"
    "  len = length(e);"
    "  e *= -q2 / (len);"
    "  b += e;",
    //    0,
    TRANSFORM_HAS_PARAMS | TRANSFORM_ALLOW_MATRIX,
    // TRANSFORM_HAS_PARAMS,
    },
#endif
  };

static int num_fragment_shader_transforms =
  sizeof(fragment_shader_transforms)/sizeof(fragment_shader_transforms[0]);

/* Texture transform matrices */

static const texture_matrix_t texture_matrices[] =
  {

    { /* Rotation & zoom out */
      { 0.99939082804808360937 * 1.02, 0.03489946723613097832*1.02,
        -0.03489946723613097832*1.02, 0.99939082804808360937 * 1.02 },
      { 0.0, 0.0 },
      //    "  b.x = (a.x * 0.99939082804808360937 + a.y * 0.03489946723613097832)*1.02;"
      //    "  b.y = (-a.x * 0.03489946723613097832 + a.y * 0.99939082804808360937)*1.02;",
    },
    {
      /* Rotation & zoom in */
      { 0.99939082804808360937 * 0.98, 0.03489946723613097832 * 0.98,
        -0.03489946723613097832 * 0.98, 0.99939082804808360937 * 0.98 },
      { 0.0, 0.0 }
      //    "  b.x = (a.x * 0.99939082804808360937 + a.y * 0.03489946723613097832)*0.98;"
      //    "  b.y = (-a.x * 0.03489946723613097832 + a.y * 0.99939082804808360937)*0.98;",
    },
    { /* Rotation & zoom out */
      { 0.99939082804808360937 * 1.02, -0.03489946723613097832*1.02,
        0.03489946723613097832*1.02, 0.99939082804808360937 * 1.02 },
      { 0.0, 0.0 },
      //    "  b.x = (a.x * 0.99939082804808360937 - a.y * 0.03489946723613097832)*1.02;"
      //    "  b.y = (a.x * 0.03489946723613097832 + a.y * 0.99939082804808360937)*1.02;",
    },
    {
      /* Rotation & zoom in */
      { 0.99939082804808360937 * 0.98, -0.03489946723613097832 * 0.98,
        0.03489946723613097832 * 0.98, 0.99939082804808360937 * 0.98 },
      { 0.0, 0.0 }
      //    "  b.x = (a.x * 0.99939082804808360937 - a.y * 0.03489946723613097832)*0.98;"
      //    "  b.y = (a.x * 0.03489946723613097832 + a.y * 0.99939082804808360937)*0.98;",
    },
  };

static int num_texture_matrices = sizeof(texture_matrices)/sizeof(texture_matrices[0]);
  
static const texture_matrix_t texture_matrix_unity =
  {
    { 1.0, 0.0,
      0.0, 1.0 },
    { 0.0, 0.0 }
  };
