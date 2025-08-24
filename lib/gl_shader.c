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

#include <gmerlin/glvideo.h>
#include <glvideo_private.h>

/* Shader stuff */

static const char * gl_prefix =
  "#version 140\n";

static const char * gles_prefix =
  "#version 300 es\n"
  "precision mediump float;\n";


static const char * vertex_shader =
  "in vec2 pos;\n"
  "in vec2 tex;\n"
  "out vec2 TexCoord;\n"
  "void main()\n"
  " {\n"
  " gl_Position = vec4(pos, 0.0, 1.0);\n"
  " TexCoord = tex;\n"
  " }";

/* 1 plane */

static const char * fragment_shader_noplanar =
  "in vec2 TexCoord;\n" 
  "out vec4 FragColor;\n"
  "uniform sampler2D frame;\n"
  "uniform mat4 colormatrix;\n"
  "uniform vec4 coloroffset;\n"
  "void main()\n"
  "  {\n"
  "  FragColor = colormatrix * texture2D(frame, TexCoord) + coloroffset;\n"
  "  }";

static const char * fragment_shader_noplanar_nocm =
  "in vec2 TexCoord;\n" 
  "out vec4 FragColor;\n"
  "uniform sampler2D frame;\n"
  "void main()\n"
  "  {\n"
  "  FragColor = texture2D(frame, TexCoord);\n"
  "  }";

/* 3 planes */

static const char * fragment_shader_planar =
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

static const char * fragment_shader_planar_nocm =
  "in vec2 TexCoord;\n" 
  "out vec4 FragColor;\n"
  "uniform sampler2D frame;\n"
  "uniform sampler2D frame_u;\n"
  "uniform sampler2D frame_v;\n"
  "void main()\n"
  "  {\n"
  "  FragColor = vec4( texture2D(frame, TexCoord).r, texture2D(frame_u, TexCoord).r, texture2D(frame_v, TexCoord).r, 1.0 );\n"
  "  }";


/* Imported DMA buffer */

static const char * fragment_shader_ext =
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

static const char * fragment_shader_ext_nocm =
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


int bg_glvideo_create_shader(port_t * port, int cm)
  {
  const char * shader[2];
  int num = 0;
  int use_gles = 0;
  shader_program_t * p;
  int planar = 0;
  
  if(gavl_hw_ctx_get_type(port->g->hwctx) == GAVL_HW_EGL_GLES)
    use_gles = 1;

  if(cm)
    p = &port->shader_cm;
  else
    p = &port->shader_nocm;
  
  if(use_gles)
    shader[0] = gles_prefix;
  else
    shader[0] = gl_prefix;

  /* Vertex shader */
  
  shader[1] = vertex_shader;
  num = 2;


  p->program = glCreateProgram();
  
  p->vertex_shader = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(p->vertex_shader, num, shader, NULL);
  glCompileShader(p->vertex_shader);
  check_shader(p->vertex_shader, "vertex shader");
  glAttachShader(p->program, p->vertex_shader);

  /* Fragment shader */
  switch(port->mode)
    {
    case MODE_TEXTURE_TRANSFER:  // Copy frames from RAM to OpenGL Texture
      if(gavl_pixelformat_num_planes(port->fmt.pixelformat) > 1)
        {
        shader[1] = cm ? fragment_shader_planar : fragment_shader_planar_nocm;
        planar = 1;
        }
      else
        shader[1] = cm ? fragment_shader_noplanar : fragment_shader_noplanar_nocm;
      break;
    case MODE_IMPORT:            // Import directly
    case MODE_IMPORT_DMABUF:     // Import via DMA-Buffers
    case MODE_DMABUF_GETFRAME:   // Let the client render into a dma buffer
    case MODE_DMABUF_TRANSFER:   // Client supplies RAM buffers and we transfer
      shader[0] = cm ? fragment_shader_ext : fragment_shader_ext_nocm;
      num = 1;
      break;
    }
  p->fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(p->fragment_shader, num, shader, NULL);
  glCompileShader(p->fragment_shader);
  check_shader(p->fragment_shader, "fragment shader");
  glAttachShader(p->program, p->fragment_shader);
  
  glBindAttribLocation(p->program, ATTRIB_POS, "pos");
  glBindAttribLocation(p->program, ATTRIB_TEX, "tex");
  glBindFragDataLocation(p->program, 0, "colorOut");
  gavl_gl_log_error("glBindFragDataLocation");  
  
  glLinkProgram(p->program);
  gavl_gl_log_error("glLinkProgram");  

  
  /* After linking we can get the locations for the uniforms */
  p->frame_locations[0] = glGetUniformLocation(p->program, "frame");

  if(planar)
    {
    p->frame_locations[1] = glGetUniformLocation(p->program, "frame_u");
    p->frame_locations[2] = glGetUniformLocation(p->program, "frame_v");
    }

  if(cm)
    {
    p->colormatrix_location = glGetUniformLocation(p->program, "colormatrix");
    p->coloroffset_location = glGetUniformLocation(p->program, "coloroffset");
    }
  
  
  return 1;
  }
