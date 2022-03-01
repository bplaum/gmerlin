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

#include <string.h>

#include <x11/x11.h>
#include <x11/x11_window_private.h>

#include <va/va.h>
#include <va/va_x11.h>

#include <gavl/hw_vaapi.h>
#include <gavl/hw_vaapi_x11.h>
#include <gmerlin/state.h>

#include <gmerlin/translation.h>
#define LOG_DOMAIN "vaapi_out"
#include <gmerlin/log.h>


typedef struct
  {
  VADisplay dpy;
  gavl_hw_context_t * hwctx;
  gavl_hw_context_t * hwctx_priv;
  
  gavl_video_frame_t * surface;

  VADisplayAttribute attr_b;
  VADisplayAttribute attr_s;
  VADisplayAttribute attr_c;
  } vaapi_priv_t;


static int init_vaapi(driver_data_t * d)
  {
  VADisplayAttribute *da;
  int num_da = 0;
  int i;
  vaapi_priv_t * priv;
  priv = calloc(1, sizeof(*priv));

  priv->hwctx_priv = gavl_hw_ctx_create_vaapi_x11(d->win->dpy);
  if(!priv->hwctx_priv)
    {
    free(priv);
    //    fprintf(stderr, "init_vaapi failed\n"); 
    return 0;
    }
  
  d->priv = priv;
  d->img_formats = gavl_hw_ctx_get_image_formats(priv->hwctx_priv);
  d->ovl_formats = gavl_hw_ctx_get_overlay_formats(priv->hwctx_priv);

  priv->dpy = gavl_hw_ctx_vaapi_x11_get_va_display(priv->hwctx_priv);

  num_da = vaMaxNumDisplayAttributes(priv->dpy);
  da = calloc(num_da, sizeof(*da));
  vaQueryDisplayAttributes(priv->dpy, da, &num_da);
  
  for(i = 0; i < num_da; i++)
    {
    switch(da[i].type)
      {
      case VADisplayAttribBrightness:
        if(da[i].flags & VA_DISPLAY_ATTRIB_SETTABLE)
          {
          memcpy(&priv->attr_b, &da[i], sizeof(da[i]));
          d->flags |= DRIVER_FLAG_BRIGHTNESS;
          //          fprintf(stderr, "VADisplayAttribBrightness\n");
          }
        break;
      case VADisplayAttribContrast:
        if(da[i].flags & VA_DISPLAY_ATTRIB_SETTABLE)
          {
          memcpy(&priv->attr_c, &da[i], sizeof(da[i]));
          d->flags |= DRIVER_FLAG_CONTRAST;
          //          fprintf(stderr, "VADisplayAttribContrast\n");
          } 
       break;
      case VADisplayAttribSaturation:
        if(da[i].flags & VA_DISPLAY_ATTRIB_SETTABLE)
          {
          memcpy(&priv->attr_s, &da[i], sizeof(da[i]));
          d->flags |= DRIVER_FLAG_SATURATION;
          //          fprintf(stderr, "VADisplayAttribSaturation\n");
          }
        break;
      default:
        break;
      }
    }
  
  priv->dpy = NULL;
  return 1;
  }

static int rescale(VADisplayAttribute * attr, float val, float min, float max)
  {
  int ret;
  ret = attr->min_value +
    (int)((attr->max_value - attr->min_value) * (val - min) / (max - min) +
          0.5);
  if(ret < attr->min_value)
    ret = attr->min_value;
  if(ret > attr->max_value)
    ret = attr->max_value;
  return ret;
  }

static void set_brightness_vaapi(driver_data_t* d,float val)
  {
  vaapi_priv_t * priv = d->priv;

  priv->attr_b.value =
    rescale(&priv->attr_b,
            val,
            BG_BRIGHTNESS_MIN,
            BG_BRIGHTNESS_MAX);

  vaSetDisplayAttributes(priv->dpy, &priv->attr_b, 1);
  }

static void set_saturation_vaapi(driver_data_t* d,float val)
  {
  vaapi_priv_t * priv = d->priv;

  priv->attr_s.value =
    rescale(&priv->attr_s,
            val,
            BG_SATURATION_MIN,
            BG_SATURATION_MAX);

  vaSetDisplayAttributes(priv->dpy, &priv->attr_s, 1);
  }


static void set_contrast_vaapi(driver_data_t* d,float val)
  {
  vaapi_priv_t * priv = d->priv;

  priv->attr_c.value =
    rescale(&priv->attr_c,
            val,
            BG_CONTRAST_MIN,
            BG_CONTRAST_MAX);

  vaSetDisplayAttributes(priv->dpy, &priv->attr_c, 1);
  }


static int open_vaapi(driver_data_t * d)
  {
  bg_x11_window_t * w;
  vaapi_priv_t * priv;
  
  priv = d->priv;
  w = d->win;

  /* Get context */

  if(w->video_format.hwctx)
    {
    /* Frames are in hardware already: Take these */
    SET_FLAG(w, FLAG_NO_GET_FRAME);
    priv->hwctx = w->video_format.hwctx;
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Using VAAPI (direct)");
    }
  else
    {
    priv->hwctx = priv->hwctx_priv;
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Using VAAPI (indirect)");
    }

  priv->dpy = gavl_hw_ctx_vaapi_x11_get_va_display(priv->hwctx);
  
  /* Get the format */

  
  
  /* Video format will have frame_size == image_size */
  
  w->video_format.frame_width  = w->video_format.image_width;
  w->video_format.frame_height = w->video_format.image_height;

  if(!TEST_FLAG(w, FLAG_NO_GET_FRAME))
    {
    priv->surface = gavl_hw_video_frame_create_hw(priv->hwctx,
                                                  &w->video_format);
    }
  
  return 1;
  }

static gavl_video_frame_t * create_frame_vaapi(driver_data_t * d)
  {
  vaapi_priv_t * priv = d->priv;
  bg_x11_window_t * w = d->win;
  /* Used only for transferring to HW */
  return gavl_hw_video_frame_create_ram(priv->hwctx,
                                        &w->video_format);
  }


static void put_frame_vaapi(driver_data_t * d, gavl_video_frame_t * f)
  {
  int i;
  vaapi_priv_t * priv;
  bg_x11_window_t * w = d->win;
  VAStatus status;
  unsigned int flags = 0;
  VASurfaceID surf;  
  
  priv = d->priv;

  
  if(!TEST_FLAG(w, FLAG_NO_GET_FRAME))
    {
    // fprintf(stderr, "put frame vaapi: %p %p %p %p\n", f, f->hwctx, f->user_data, priv->surface->hwctx);

    gavl_video_frame_ram_to_hw(&w->video_format,
                               priv->surface, f);
    f = priv->surface;
    }
  
  surf = gavl_vaapi_get_surface_id(f);

  /* Draw overlays */
  flags = 0;
  for(i = 0; i < w->num_overlay_streams; i++)
    {
    if(!w->overlay_streams[i]->active)
      continue;

    status = vaAssociateSubpicture(priv->dpy,
                                   gavl_vaapi_get_subpicture_id(w->overlay_streams[i]->ovl),
                                   &surf,
                                   1,
                                   w->overlay_streams[i]->ovl->src_rect.x, /* upper left offset in subpicture */
                                   w->overlay_streams[i]->ovl->src_rect.y,
                                   w->overlay_streams[i]->ovl->src_rect.w,
                                   w->overlay_streams[i]->ovl->src_rect.h,
                                   w->overlay_streams[i]->ovl->dst_x, /* upper left offset in surface */
                                   w->overlay_streams[i]->ovl->dst_y,
                                   w->overlay_streams[i]->ovl->src_rect.w,
                                   w->overlay_streams[i]->ovl->src_rect.h,
                                   /*
                                    * whether to enable chroma-keying, global-alpha, or screen relative mode
                                    * see VA_SUBPICTURE_XXX values
                                    */
                                   flags);


    }

  flags = VA_CLEAR_DRAWABLE;

#if 0  
  fprintf(stderr, "vaPutSurface: %d\n", surf);
  fprintf(stderr, "src: ");
  gavl_rectangle_f_dump(&w->src_rect);
  fprintf(stderr, "dst: ");
  gavl_rectangle_i_dump(&w->dst_rect);
  fprintf(stderr, "\n");
#endif
  
  if((status = vaPutSurface(priv->dpy, surf,
                            w->current->win, /* X Drawable */
                            (int)w->src_rect.x,  /* src_x  */
                            (int)w->src_rect.y,  /* src_y  */
                            (int)w->src_rect.w,  /* src_w  */
                            (int)w->src_rect.h,  /* src_h  */
                            w->dst_rect.x, /* dest_x */
                            w->dst_rect.y, /* dest_y */
                            w->dst_rect.w, /* dest_w */
                            w->dst_rect.h, /* dest_h */
                            NULL, /* client supplied destination clip list */
                            0,    /* number of clip rects in the clip list */
                            flags     /* PutSurface flags */
                            )) != VA_STATUS_SUCCESS)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN,
           "vaPutSurface failed: %s", vaErrorStr(status));
    }

  /* Deassociate overlays */

  for(i = 0; i < w->num_overlay_streams; i++)
    {
    if(!w->overlay_streams[i]->active)
      continue;
    status = vaDeassociateSubpicture(priv->dpy,
                                     gavl_vaapi_get_subpicture_id(w->overlay_streams[i]->ovl),
                                     &surf,
                                     1);
    }
  }

static void set_overlay_vaapi(driver_data_t* d, overlay_stream_t * str)
  {
  /* Swap */
  gavl_vaapi_video_frame_swap_bytes(&str->format,
                                    str->ovl, 0);
  gavl_vaapi_unmap_frame(str->ovl);
  }

static void unset_overlay_vaapi(driver_data_t* d, overlay_stream_t * str)
  {
  gavl_vaapi_map_frame(str->ovl);
  }

/* Overlay support */
static gavl_video_frame_t *
create_overlay_vaapi(driver_data_t* d, overlay_stream_t * str)
  {
  vaapi_priv_t * priv;
  gavl_video_frame_t * ret;
  priv = d->priv;
  ret = gavl_hw_video_frame_create_ovl(priv->hwctx, &str->format);
  return ret;
  }

static void
destroy_overlay_vaapi(driver_data_t* d, overlay_stream_t * str,
                      gavl_video_frame_t * frame)
  {
  gavl_video_frame_destroy(frame);
  }

static void init_overlay_stream_vaapi(driver_data_t* d, overlay_stream_t * str)
  {
  }

static void close_vaapi(driver_data_t * d)
  {
  vaapi_priv_t * priv;

  priv = d->priv;

  if(priv->surface)
    {
    gavl_video_frame_destroy(priv->surface);
    priv->surface = NULL;
    }
  }

static void cleanup_vaapi(driver_data_t * d)
  {
  vaapi_priv_t * priv;
  priv = d->priv;

  if(priv->hwctx_priv)
    {
    gavl_hw_ctx_destroy(priv->hwctx_priv);
    priv->hwctx_priv = NULL;
    }
  
  free(priv);
  }

static int supports_hw_vaapi(driver_data_t* d,
                             gavl_hw_context_t * ctx)
  {
  gavl_hw_type_t type = gavl_hw_ctx_get_type(ctx);
  switch(type)
    {
    case GAVL_HW_VAAPI_X11:
      return 1;
    break;
    default:
      break;
    }
  return 0;
  }


const video_driver_t vaapi_driver =
  {
    .name                = "VAAPI",
    .supports_hw         = supports_hw_vaapi,
    .can_scale           = 1,
    .init                = init_vaapi,
    .open                = open_vaapi,
    .set_brightness      = set_brightness_vaapi,
    .set_saturation      = set_saturation_vaapi,
    .set_contrast        = set_contrast_vaapi,

    .create_frame        = create_frame_vaapi,
    .put_frame           = put_frame_vaapi,
    .close               = close_vaapi,
    .cleanup             = cleanup_vaapi,
    .init_overlay_stream = init_overlay_stream_vaapi,
    .create_overlay      = create_overlay_vaapi,
    .set_overlay         = set_overlay_vaapi,
    .unset_overlay       = unset_overlay_vaapi,

    .destroy_overlay     = destroy_overlay_vaapi,
  };
