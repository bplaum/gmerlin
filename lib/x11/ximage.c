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



#include <x11/x11.h>
#include <x11/x11_window_private.h>

/* XImage video driver */

static int init_ximage(driver_data_t * d)
  {
  d->img_formats = malloc(2*sizeof(*d->img_formats));
  d->img_formats[0] = bg_x11_window_get_pixelformat(d->win->dpy, d->win->visual, d->win->depth);
  d->img_formats[1] = GAVL_PIXELFORMAT_NONE;
  return 1;
  }

typedef struct 
  {
  XImage * x11_image;
  XShmSegmentInfo shminfo;
  } ximage_frame_t;

static gavl_video_frame_t * create_frame_ximage(driver_data_t * d)
  {
  ximage_frame_t * frame;
  gavl_video_frame_t * ret = NULL;
  bg_x11_window_t * w = d->win;
  
  frame = calloc(1, sizeof(*frame));

  if(TEST_FLAG(w, FLAG_HAVE_SHM))
    {
    /* Create Shm Image */
    frame->x11_image = XShmCreateImage(w->dpy, w->visual,
                                       w->depth, ZPixmap,
                                       NULL, &frame->shminfo,
                                       w->window_format.frame_width,
                                       w->window_format.frame_height);
    if(!frame->x11_image)
      {
      CLEAR_FLAG(w, FLAG_HAVE_SHM);
      free(frame);
      frame = NULL;
      }
    else
      {
      if(!bg_x11_window_create_shm(w, &frame->shminfo,
                                frame->x11_image->height *
                                frame->x11_image->bytes_per_line))
        {
        XDestroyImage(frame->x11_image);
        CLEAR_FLAG(w, FLAG_HAVE_SHM);
        }
      frame->x11_image->data = frame->shminfo.shmaddr;

      ret = gavl_video_frame_create(NULL);
      ret->planes[0] = (uint8_t*)(frame->x11_image->data);
      ret->strides[0] = frame->x11_image->bytes_per_line;
      }
    }
  
  if(!ret)
    {
    /* Use gavl to allocate memory aligned scanlines */
    ret = gavl_video_frame_create(&w->window_format);
    frame->x11_image = XCreateImage(w->dpy, w->visual, w->depth,
                                    ZPixmap,
                                    0, (char*)(ret->planes[0]),
                                    w->window_format.frame_width,
                                    w->window_format.frame_height,
                                    32,
                                    ret->strides[0]);
    }
  ret->storage = frame;

  gavl_video_frame_clear(ret, &w->window_format);
  
  return ret;
  }

static void put_frame_ximage(driver_data_t * d, gavl_video_frame_t * f)
  {
  ximage_frame_t * frame = (ximage_frame_t*)f->storage;
  bg_x11_window_t * w = d->win;
  
  if(TEST_FLAG(w, FLAG_HAVE_SHM))
    {
    XShmPutImage(w->dpy,            /* dpy        */
                 w->current->win, /* d          */
                 w->gc,             /* gc         */
                 frame->x11_image, /* image      */
                 w->dst_rect.x,    /* src_x      */
                 w->dst_rect.y,    /* src_y      */
                 w->dst_rect.x,          /* dst_x      */
                 w->dst_rect.y,          /* dst_y      */
                 w->dst_rect.w,    /* src_width  */
                 w->dst_rect.h,   /* src_height */
                 True                  /* send_event */);
    SET_FLAG(w, FLAG_WAIT_FOR_COMPLETION);
    }
  else
    {
    XPutImage(w->dpy,            /* dpy        */
              w->current->win, /* d          */
              w->gc,             /* gc         */
              frame->x11_image, /* image      */
              w->dst_rect.x,    /* src_x      */
              w->dst_rect.y,    /* src_y      */
              w->dst_rect.x,          /* dst_x      */
              w->dst_rect.y,          /* dst_y      */
              w->dst_rect.w,    /* src_width  */
              w->dst_rect.h);   /* src_height */
    }
  }

static void destroy_frame_ximage(driver_data_t * d, gavl_video_frame_t * f)
  {
  ximage_frame_t * frame = (ximage_frame_t*)f->storage;
  bg_x11_window_t * w = d->win;
  if(frame->x11_image)
    XFree(frame->x11_image);
  if(TEST_FLAG(w, FLAG_HAVE_SHM))
    {
    bg_x11_window_destroy_shm(w, &frame->shminfo);
    gavl_video_frame_null(f);
    gavl_video_frame_destroy(f);
    }
  else
    {
    gavl_video_frame_destroy(f);
    }
  free(frame);
  }

const video_driver_t ximage_driver =
  {
    .name               = "XImage",
    .init               = init_ximage,
    .create_frame       = create_frame_ximage,
    .put_frame          = put_frame_ximage,
    .destroy_frame      = destroy_frame_ximage
  };
