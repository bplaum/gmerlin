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

#include <stdlib.h>
#include <stdio.h>

#include <gavl/gavl.h>
#include <gmerlin/frametimer.h>

#define TIME_SCALE 1000

struct bg_frame_timer_s
  {
  int frame_duration;
  
  gavl_timer_t * timer;
  int64_t next_pts;

  /* Only used when bg_frame_timer_wait() is used */
  gavl_time_t capture_start_time;
  gavl_time_t last_capture_duration;
  
  gavl_time_t last_time;
  
  };

bg_frame_timer_t * bg_frame_timer_create(float framerate,
                                         uint32_t * timescale)
  {
  bg_frame_timer_t * ret;
  ret = calloc(1, sizeof(*ret));
  
  ret->timer = gavl_timer_create();
  ret->next_pts = GAVL_TIME_UNDEFINED;
  ret->frame_duration = (int)(TIME_SCALE / framerate);
  *timescale = TIME_SCALE;
  return ret;
  }

void bg_frame_timer_destroy(bg_frame_timer_t * t)
  {
  gavl_timer_destroy(t->timer);
  free(t);
  }

void bg_frame_timer_update(bg_frame_timer_t * t,
                           gavl_video_frame_t * frame)
  {
  int64_t diff;
  gavl_time_t current_time;
  int64_t real_duration;
  
  current_time = gavl_timer_get(t->timer);
  t->last_capture_duration = current_time - t->capture_start_time;
  
  if(t->next_pts == GAVL_TIME_UNDEFINED)
    {
    frame->timestamp = 0;
    frame->duration = t->frame_duration;
    gavl_timer_start(t->timer);
    t->next_pts = frame->duration;
    t->last_time = 0;
    return;
    }
  
  frame->timestamp = t->next_pts;
  
  /*
   * diff: True time minus guessed time
   *
   * Diff < 0 -> frame too early: Guessed duration was too large
   * Diff > 0 -> frame too late: Guessed duration was too small
   */
  
  diff = gavl_time_scale(TIME_SCALE, current_time) - t->next_pts;
  
  /* Real duration of the last frame */ 
  real_duration =
    gavl_time_scale(TIME_SCALE, current_time - t->last_time);

  /* Duration of this frame is real duration of the last frame
     plus the error */
  
  frame->duration = real_duration + diff;

  //  fprintf(stderr, "Cur: %"PRId64", Last: %"PRId64", diff: %"PRId64"\n",
  //          current_time, t->last_time, current_time - t->last_time);
  
  if(frame->duration <= 0)
    frame->duration = TIME_SCALE / 100; // 10 ms/100 fps
  
  t->last_time = current_time;
  t->next_pts += frame->duration;
  }

void bg_frame_timer_wait(bg_frame_timer_t * t)
  {
  gavl_time_t cur, diff;
  
  if(t->next_pts == GAVL_TIME_UNDEFINED)
    return;

  cur = gavl_timer_get(t->timer);
  diff =
    gavl_time_unscale(TIME_SCALE, t->frame_duration) - (cur - t->last_time) -
    t->last_capture_duration;
  
  if(diff > 0)
    gavl_time_delay(&diff);
  
  t->capture_start_time = gavl_timer_get(t->timer);
  }
