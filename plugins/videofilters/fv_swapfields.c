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
#include <stdio.h>

#include <config.h>
#include <gmerlin/translation.h>
#include <gmerlin/plugin.h>
#include <gmerlin/utils.h>
#include <gmerlin/log.h>

#define LOG_DOMAIN "fv_swapfields"

typedef struct
  {
  gavl_interlace_mode_t out_interlace;
  
  gavl_video_format_t format;
  gavl_video_format_t field_format[2];
  
  gavl_video_frame_t * fields[2];
  
  gavl_video_frame_t * last_field; // Saved field from last frame
  gavl_video_frame_t * next_field; // Saved field for next frame
  
  gavl_video_frame_t * cpy_field;
  
  int init;

  int framerate_mult;
  
  int delay_field; // Which field to delay
  int noop; // Do nothing
  
  int64_t next_pts;

  gavl_video_source_t * in_src;
  gavl_video_source_t * out_src;
  } swapfields_priv_t;

static void * create_swapfields()
  {
  swapfields_priv_t * ret;
  ret = calloc(1, sizeof(*ret));
  ret->cpy_field = gavl_video_frame_create(NULL);
  return ret;
  }

static void destroy_swapfields(void * priv)
  {
  swapfields_priv_t * vp;
  vp = priv;
  if(vp->cpy_field)
    {
    gavl_video_frame_null(vp->cpy_field);
    gavl_video_frame_destroy(vp->cpy_field);
    }

  if(vp->fields[0])
    gavl_video_frame_destroy(vp->fields[0]);
  if(vp->fields[1])
    gavl_video_frame_destroy(vp->fields[1]);
  if(vp->out_src)
    gavl_video_source_destroy(vp->out_src);
  free(vp);
  }

static void reset_swapfields(void * priv)
  {
  swapfields_priv_t * vp = priv;
  vp->init = 1;
  }

static void set_format(void * priv, const gavl_video_format_t * format)
  {
  swapfields_priv_t * vp;
  vp = priv;
  
  vp->framerate_mult = 1;
  vp->noop = 0;
    
  gavl_video_format_copy(&vp->format, format);
  gavl_get_field_format(format, &vp->field_format[0], 0);
  gavl_get_field_format(format, &vp->field_format[1], 1);
  
  if(vp->format.interlace_mode == GAVL_INTERLACE_TOP_FIRST)
    {
    vp->format.interlace_mode = GAVL_INTERLACE_BOTTOM_FIRST;

    /* Top first -> bottom first: Delay bottom field */
    vp->delay_field = 1;
    }
  else if(vp->format.interlace_mode == GAVL_INTERLACE_BOTTOM_FIRST)
    {
    vp->format.interlace_mode = GAVL_INTERLACE_TOP_FIRST;

    /* Bottom first -> top first: Delay top field */
    vp->delay_field = 0;
    }
  else
    {
    gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN,
           "Unsupported interlace mode, need top-first or bottom-first");
    vp->noop = 1;
    }

  if(!vp->noop)
    {
    if(vp->format.frame_duration % 2)
      {
      vp->framerate_mult = 2;
      vp->format.timescale *= 2;
      vp->format.frame_duration *= 2;
      }
    }
  if(vp->fields[0])
    {
    gavl_video_frame_destroy(vp->fields[0]);
    vp->fields[0] = NULL;
    }
  if(vp->fields[1])
    {
    gavl_video_frame_destroy(vp->fields[1]);
    vp->fields[1] = NULL;
    }
  vp->init = 1;
  }

static gavl_source_status_t
read_func(void * priv, gavl_video_frame_t ** frame)
  {
  gavl_source_status_t st;

  swapfields_priv_t * vp;
  int64_t pts;
  gavl_video_frame_t * swp;
  
  vp = priv;

  /* Do nothing */
  if(vp->noop)
    return gavl_video_source_read_frame(vp->in_src, frame);
  
  if(!vp->fields[0])
    vp->fields[0] = gavl_video_frame_create(&vp->field_format[0]);
  if(!vp->fields[1])
    vp->fields[1] = gavl_video_frame_create(&vp->field_format[1]);
  
  if(vp->init)
    {
    if((st = gavl_video_source_read_frame(vp->in_src, frame)) !=
       GAVL_SOURCE_OK)
      return st;
    
    vp->last_field = vp->fields[0];
    vp->next_field = vp->fields[1];
    
    /* Save field for later use */
    gavl_video_frame_get_field(vp->format.pixelformat,
                               *frame,
                               vp->cpy_field, vp->delay_field);
    
    gavl_video_frame_copy(&vp->field_format[vp->delay_field],
                          vp->last_field, vp->cpy_field);
    vp->init = 0;
    vp->next_pts = (*frame)->timestamp * vp->framerate_mult +
      ((*frame)->duration * vp->framerate_mult) / 2;
    }

  if((st = gavl_video_source_read_frame(vp->in_src, frame)) !=
     GAVL_SOURCE_OK)
    return st;
  
  gavl_video_frame_get_field(vp->format.pixelformat,
                             *frame,
                             vp->cpy_field, vp->delay_field);

  /* Save field for later use */
  gavl_video_frame_copy(&vp->field_format[vp->delay_field],
                        vp->next_field, vp->cpy_field);
  
  /* Copy field from last frame */
  gavl_video_frame_copy(&vp->field_format[vp->delay_field],
                        vp->cpy_field, vp->last_field);

  /* Swap pointers */
  swp = vp->next_field;
  vp->next_field = vp->last_field;
  vp->last_field = swp;
  
  /* Adjust pts */
  pts = (*frame)->timestamp;
  (*frame)->timestamp = vp->next_pts;

  vp->next_pts = pts * vp->framerate_mult +
    ((*frame)->duration * vp->framerate_mult) / 2;

  (*frame)->duration *= vp->framerate_mult;

  //  fprintf(stderr, "PTS: %ld duration: %ld\n",
  //          frame->timestamp, frame->duration);
  
  return GAVL_SOURCE_OK;
  }

static gavl_video_source_t *
connect_swapfields(void * priv, gavl_video_source_t * src,
                   const gavl_video_options_t * opt)
  {
  swapfields_priv_t * vp = priv;
  vp->init = 1;
  vp->in_src = src;
  if(vp->out_src)
    gavl_video_source_destroy(vp->out_src);
  set_format(vp, gavl_video_source_get_src_format(vp->in_src));
  gavl_video_source_set_dst(vp->in_src, 0, &vp->format);

  vp->out_src =
    gavl_video_source_create_source(read_func,
                                    vp, 0,
                                    vp->in_src);
  return vp->out_src;
  }

const bg_fv_plugin_t the_plugin = 
  {
    .common =
    {
      BG_LOCALE,
      .name =      "fv_swapfields",
      .long_name = TRS("Swap fields"),
      .description = TRS("Swap field order"),
      .type =     BG_PLUGIN_FILTER_VIDEO,
      .flags =    BG_PLUGIN_FILTER_1,
      .create =   create_swapfields,
      .destroy =   destroy_swapfields,
      .priority =         1,
    },
    .reset = reset_swapfields,
    
    .connect = connect_swapfields,
    
  };

/* Include this into all plugin modules exactly once
   to let the plugin loader obtain the API version */
BG_GET_PLUGIN_API_VERSION;
