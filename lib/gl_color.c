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


#include <stdlib.h>
#include <string.h>
#include <math.h>


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
#define LOG_DOMAIN "gl_color"

#include <gmerlin/glvideo.h>
#include <gmerlin/state.h>
#include <glvideo_private.h>

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


static void upload_colormatrix(port_t * port, const double mat[4][5])
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

  glUseProgram(port->shader_cm.program);
  
  glUniformMatrix4fv(port->shader_cm.colormatrix_location,
                     1, GL_TRUE, colormatrix);
  gavl_gl_log_error("glUniformMatrix4fv");  

  glUniform4fv(port->shader_cm.coloroffset_location,
               1, coloroffset);
  gavl_gl_log_error("glUniform4fv");  

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
    matrix_init(ret);
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

// Inspired by https://docs.rainmeter.net/tips/colormatrix-guide/

#define LUM_R 0.299000
#define LUM_G 0.587000
#define LUM_B 0.114000

static void set_bsc_matrix(double bsc[4][5], double brightness, double saturation, double contrast)
  {
  double b, s, c;

  double sr, sg, sb;
  double t;
  
  
  /* Scale 0.0 .. 1.0 */
  b = (brightness - BG_BRIGHTNESS_MIN) / (BG_BRIGHTNESS_MAX - BG_BRIGHTNESS_MIN);
  c = (contrast   - BG_CONTRAST_MIN  ) / (BG_CONTRAST_MAX   - BG_CONTRAST_MIN  );
  s = (saturation - BG_SATURATION_MIN) / (BG_SATURATION_MAX - BG_SATURATION_MIN);
  
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
  
  
  bsc[0][0] = c * (sr + s);
  bsc[0][1] = c * sg;
  bsc[0][2] = c * sb;
  bsc[0][3] = 0.0;
  bsc[0][4] = b + t;
  
  bsc[1][0] = c * sr;
  bsc[1][1] = c * (sg + s);
  bsc[1][2] = c * sb;
  bsc[1][3] = 0.0;
  bsc[1][4] = b + t;
  
  bsc[2][0] = c * sr;
  bsc[2][1] = c * sg;
  bsc[2][2] = c * (sb + s);
  bsc[2][3] = 0.0;
  bsc[2][4] = b + t;
  
  bsc[3][0] = 0.0;
  bsc[3][1] = 0.0;
  bsc[3][2] = 0.0;
  bsc[3][3] = 1.0;
  bsc[3][4] = 0.0;
  
  }

void bg_glvideo_init_colormatrix(port_t * port, gavl_pixelformat_t fmt)
  {
  set_pixelformat_matrix(port->cmat, fmt);
  }

void bg_glvideo_update_colormatrix(port_t * port)
  {
  double cmat[4][5];
  double bsc[4][5];
  set_bsc_matrix(bsc, port->g->brightness, port->g->saturation, port->g->contrast);

  matrixmult(bsc, port->cmat, cmat);
  if(is_unity(cmat))
    {
    port->flags &= ~PORT_USE_COLORMATRIX;
    }
  else
    {
    port->flags |= PORT_USE_COLORMATRIX;
    upload_colormatrix(port, cmat);
    
    }
  }
