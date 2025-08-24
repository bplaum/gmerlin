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

#define GL_GLEXT_PROTOTYPES 1


#include <GL/gl.h>
#include <string.h>
#include <math.h>

#include <config.h>

#include <gavl/connectors.h>
#include <gavl/log.h>
#define LOG_DOMAIN "gl_coords"
#include <gavl/utils.h>

#include <gmerlin/glvideo.h>
#include <glvideo_private.h>

#define POS_LL 0
#define POS_UL 1
#define POS_LR 2
#define POS_UR 3

void bg_glvideo_update_vertex_buffer(port_t * port)
  {
  // int transposed = 0;
  double src_aspect;
  double squeeze_factor;
  double mat[2][3];
  double zoom_x;
  double zoom_y;
  double tmp;
  double dst_x = 0.0, dst_y = 0.0;
  
  gavl_rectangle_f_t src_rect;
  
  gavl_image_orientation_t orient =
    (port->fmt.orientation + port->g->orientation) % 8;

    
  squeeze_factor = pow(2.0, port->g->squeeze_factor);
  
  //  transposed = gavl_image_orientation_is_transposed(orient);
  
  src_aspect =
    (double)(port->fmt.image_width * port->fmt.pixel_width) /
    (double)(port->fmt.image_height * port->fmt.pixel_height);

  src_aspect *= squeeze_factor;

  //  fprintf(stderr, "Update vertex buffer: Port %d Size: %dx%d src_aspect: %f squeeze: %f\n",
  //          port->idx, port->fmt.image_width, port->fmt.image_height, src_aspect, squeeze_factor);

  
  if(!port->idx)
    gavl_rectangle_f_set_all(&src_rect, &port->fmt);
  else
    {
    gavl_rectangle_i_to_f(&src_rect, &port->cur->src_rect);
    dst_x = port->cur->dst_x;
    dst_y = port->cur->dst_y;
    }
  
  /* Set initial coordinates */

  /* Texture coords */
  port->vertices[POS_LL].tex[0] = src_rect.x                / (double)port->fmt.image_width;
  port->vertices[POS_LL].tex[1] = (src_rect.y + src_rect.h) / (double)port->fmt.image_height;
  port->vertices[POS_UR].tex[0] = (src_rect.x + src_rect.w) / (double)port->fmt.image_width;
  port->vertices[POS_UR].tex[1] = src_rect.y                / (double)port->fmt.image_height;
  
  port->vertices[POS_LR].tex[0] = port->vertices[POS_UR].tex[0];
  port->vertices[POS_LR].tex[1] = port->vertices[POS_LL].tex[1];
  port->vertices[POS_UL].tex[0] = port->vertices[POS_LL].tex[0];
  port->vertices[POS_UL].tex[1] = port->vertices[POS_UR].tex[1];
  
#if 0
  port->vertices[POS_LL].pos[0] = -src_aspect;
  port->vertices[POS_LL].pos[1] = -1.0;
  port->vertices[POS_UR].pos[0] =  src_aspect;
  port->vertices[POS_UR].pos[1] =  1.0;
#else

  /* 0 .. 1 */
  port->vertices[POS_LL].pos[0] = ( dst_x             / (double)port->fmt.image_width);
  port->vertices[POS_UR].pos[0] = ((dst_x+src_rect.w) / (double)port->fmt.image_width);

  /* -src_aspect .. src_aspect */
  port->vertices[POS_LL].pos[0] = src_aspect * (2.0 * port->vertices[POS_LL].pos[0] - 1.0);
  port->vertices[POS_UR].pos[0] = src_aspect * (2.0 * port->vertices[POS_UR].pos[0] - 1.0);

  /* 0 .. 1 */
  port->vertices[POS_LL].pos[1] =   dst_y               / (double)port->fmt.image_height;
  port->vertices[POS_UR].pos[1] = ( dst_y + src_rect.h ) / (double)port->fmt.image_height;
  
  /* -1 .. 1 */
  port->vertices[POS_LL].pos[1] = 2.0 * port->vertices[POS_LL].pos[1] - 1.0;
  port->vertices[POS_UR].pos[1] = 2.0 * port->vertices[POS_UR].pos[1] - 1.0;

  
#endif
    
  port->vertices[POS_LR].pos[0] = port->vertices[POS_UR].pos[0];
  port->vertices[POS_LR].pos[1] = port->vertices[POS_LL].pos[1];

  port->vertices[POS_UL].pos[0] = port->vertices[POS_LL].pos[0];
  port->vertices[POS_UL].pos[1] = port->vertices[POS_UR].pos[1];

  
  
  /* Apply orientation */

  gavl_set_orient_matrix_inv(orient, mat);

  gavl_2d_transform_transform_inplace(mat, port->vertices[POS_LL].pos);
  gavl_2d_transform_transform_inplace(mat, port->vertices[POS_UL].pos);
  gavl_2d_transform_transform_inplace(mat, port->vertices[POS_LR].pos);
  gavl_2d_transform_transform_inplace(mat, port->vertices[POS_UR].pos);

  if(!port->idx)
    {
    /* Fit into window (i.e. pixels) */
    tmp = fabs(port->vertices[POS_LL].pos[0]);
    zoom_x = (double)(port->g->window_width / 2) / tmp;

    tmp = fabs(port->vertices[POS_LL].pos[1]);
    zoom_y = (double)(port->g->window_height / 2) / tmp;
  
    // fprintf(stderr, "zoom_x: %f zoom_y: %f\n", zoom_x, zoom_y);
    port->g->coords_zoom = zoom_x < zoom_y ? zoom_x : zoom_y;
    }
  
  tmp = port->g->coords_zoom * port->g->zoom_factor;

  mat[0][0] = tmp / (double)(port->g->window_width / 2);
  mat[0][1] = 0.0;
  mat[1][0] = 0.0;
  mat[1][1] = tmp / (double)(port->g->window_height / 2);

  mat[0][2] = 0.0;
  mat[1][2] = 0.0;

  gavl_2d_transform_transform_inplace(mat, port->vertices[POS_LL].pos);
  gavl_2d_transform_transform_inplace(mat, port->vertices[POS_UL].pos);
  gavl_2d_transform_transform_inplace(mat, port->vertices[POS_LR].pos);
  gavl_2d_transform_transform_inplace(mat, port->vertices[POS_UR].pos);
  
#if 0
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
#endif
  
  /* Send to GPU */
  glBindBuffer(GL_ARRAY_BUFFER, port->vbo);  
  glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(port->vertices), port->vertices);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  }

