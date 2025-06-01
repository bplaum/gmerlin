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



#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <sys/mman.h>


#include <config.h>
#include <gmerlin/translation.h>
#include <gmerlin/plugin.h>
#include <gmerlin/log.h>
#include <gmerlin/utils.h>
#include <gmerlin/state.h>

#include <gavl/hw_v4l2.h>

#include "v4l2_common.h"

#define LOG_DOMAIN "ov_v4l2"

typedef struct
  {
  gavl_v4l2_device_t * dev;
  gavl_hw_context_t * hwctx;
  
  bg_parameter_info_t * parameters;
  gavl_video_format_t gavl_fmt;

  struct v4l2_queryctrl * controls;
  int num_controls;

  gavl_video_sink_t * sink;

  bg_controllable_t ctrl;
  
  } ov_v4l2_t;

static void cleanup_v4l(ov_v4l2_t *);

static int handle_message(void * priv, gavl_msg_t * msg)
  {
  ov_v4l2_t * win = priv;

  switch(msg->NS)
    {
    case BG_MSG_NS_STATE:
      switch(msg->ID)
        {
        case BG_CMD_SET_STATE:
          {
          const char * var;
          const char * ctx;
          gavl_value_t val;
          gavl_value_init(&val);
          
          gavl_msg_get_state(msg,
                           NULL,
                           &ctx,
                           &var,
                           &val,
                           NULL);

          if(!strcmp(ctx, BG_STATE_CTX_OV))
            {
            if(!strcmp(var, BG_STATE_OV_FULLSCREEN))
              {
              }
            else if(!strcmp(var, BG_STATE_OV_CONTRAST))
              {
              }
            else if(!strcmp(var, BG_STATE_OV_BRIGHTNESS))
              {
              }
            else if(!strcmp(var, BG_STATE_OV_SATURATION))
              {
              }
            else if(!strcmp(var, BG_STATE_OV_ZOOM))
              {
              }
            else if(!strcmp(var, BG_STATE_OV_SQUEEZE))
              {
              }
            else if(!strcmp(var, BG_STATE_OV_TITLE))
              {
              }
            else if(!strcmp(var, BG_STATE_OV_VISIBLE))
              {
              int visible = 0;

              if(gavl_value_get_int(&val, &visible) &&
                 !visible)
                cleanup_v4l(win);
              }
            }
          gavl_value_free(&val);
          }
          break;
        }
      break;
    }
  return 1;
  }

static void * create_v4l2()
  {
  ov_v4l2_t * v4l;

  v4l = calloc(1, sizeof(*v4l));
  
  bg_controllable_init(&v4l->ctrl,
                       bg_msg_sink_create(handle_message, v4l, 1),
                       bg_msg_hub_create(1));
  
  return v4l;
  }

static void  destroy_v4l2(void * priv)
  {
  ov_v4l2_t * v4l = priv;

  /* Close the device just now */
  cleanup_v4l(v4l);
  if(v4l->parameters)
    bg_parameter_info_destroy_array(v4l->parameters);

  bg_controllable_cleanup(&v4l->ctrl);
  
  free(v4l);
  }

#if 0
static const bg_parameter_info_t parameters[] =
  {
    {
      .name =        "device_section",
      .long_name =   TRS("Device"),
      .type =        BG_PARAMETER_SECTION
    },
    {
      .name =        "device",
      .long_name =   TRS("V4L2 Device"),
      .type =        BG_PARAMETER_MULTI_MENU,
    },
    { /* End */ }
  };

static void create_parameters(ov_v4l2_t * v4l)
  {
  bg_parameter_info_t * info;
  v4l->parameters = bg_parameter_info_copy_array(parameters);
  
  info = v4l->parameters + 1;
  bgv4l2_create_device_selector(info, V4L2_CAP_VIDEO_OUTPUT);
  }

static const bg_parameter_info_t * get_parameters_v4l2(void * priv)
  {
  ov_v4l2_t * v4l = priv;
  if(!v4l->parameters)
    create_parameters(v4l);
  return v4l->parameters;
  }
#endif


#if 0
static void set_parameter_v4l2(void * priv, const char * name,
                               const gavl_value_t * val)
  {
  ov_v4l2_t * v4l;
  v4l = priv;

  if(!name)
    {
    return;
    }
  else if(!strcmp(name, "device"))
    {
    gavl_dictionary_free(&v4l->dev);
    gavl_dictionary_init(&v4l->dev);
    gavl_dictionary_copy(&v4l->dev, bg_multi_menu_get_selected(val));
    }
  else if(v4l->controls && (v4l->fd >= 0))
    {
    bgv4l2_set_device_parameter(v4l->fd,
                                v4l->controls,
                                v4l->num_controls,
                                name, val);
    }
  }

static int get_parameter_v4l2(void * priv, const char * name,
                              gavl_value_t * val)
  {
  ov_v4l2_t * v4l = priv;
  if(v4l->controls && (v4l->fd >= 0))
    {
    return bgv4l2_get_device_parameter(v4l->fd,
                                       v4l->controls,
                                       v4l->num_controls,
                                       name, val);
    }
  return 0;
  }
#endif



static void cleanup_v4l(ov_v4l2_t * v4l)
  {
  if(v4l->dev)
    {
    gavl_v4l2_device_close(v4l->dev);
    v4l->dev = NULL;
    }
  if(v4l->controls)
    {
    free(v4l->controls);
    v4l->controls = NULL;
    }
  
  return;
  }

static int open_v4l2(void * priv, const char * uri,
                     gavl_video_format_t * format, int src_flags)
  {
  ov_v4l2_t * v4l = priv;
  gavl_dictionary_t dev;
  char * host = NULL;
  char * path = NULL;
  
  fprintf(stderr, "Open v4l %s\n", uri);

  if(!gavl_url_split(uri, NULL, NULL, NULL, &host, NULL, &path))
    return 0;

  gavl_dictionary_init(&dev);
  
  if(!gavl_v4l2_get_device_info(path, &dev))
    return 0;

  v4l->hwctx = gavl_hw_ctx_create_v4l2(&dev);
  v4l->dev = gavl_hw_ctx_v4l2_get_device(v4l->hwctx);
  
  //  gavl_video_format_dump(format);

  if(!gavl_v4l2_device_init_output(v4l->dev, format))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Opening device failed");
    return 0;
    }

  v4l->sink = gavl_v4l2_device_get_video_sink(v4l->dev);
  
  return 1;
  
#if 0  
  int ret = 0;

  
  
  if(v4l->fd >= 0)
    {
    /* We accept the framerate settings but nothing else */
    v4l->gavl_fmt.framerate_mode = format->framerate_mode;
    v4l->gavl_fmt.timescale      = format->timescale;
    v4l->gavl_fmt.frame_duration = format->frame_duration;
    
    gavl_video_format_copy(format, &v4l->gavl_fmt);
    return 1;
    }

  /* For now we accept only square pixels */
  if((format->pixel_width != format->pixel_height) && keep_aspect)
    {
    gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Forcing square pixels");
    format->image_width *= format->pixel_width;
    format->image_width /= format->pixel_height;
    format->pixel_width = 1;
    format->pixel_height = 1;
    if(format->frame_width < format->image_width)
      format->frame_width = format->image_width;
    }
  
  CLEAR(v4l->v4l2_fmt);
  
  v4l->fd = bgv4l2_open_device(gavl_dictionary_get_string(&v4l->dev, BG_CFG_TAG_NAME),
                               V4L2_CAP_VIDEO_OUTPUT,
                               &cap);
  if(v4l->fd < 0)
    return 0;
    
  if(v4l->controls)
    free(v4l->controls);
    
  v4l->controls =
    bgv4l2_create_device_controls(v4l->fd, &v4l->num_controls);
  
  gavl_log(GAVL_LOG_DEBUG, LOG_DOMAIN, "Device name: %s", cap.card);

  /* Get the I/O method */
  if ((cap.capabilities & V4L2_CAP_STREAMING) && !v4l->force_rw)
    {
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Trying mmap i/o");
    v4l->io = BGV4L2_IO_METHOD_MMAP;
    }
  else if(cap.capabilities & V4L2_CAP_READWRITE)
    {
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Trying write i/o");
    v4l->io = BGV4L2_IO_METHOD_RW;
    }
  
  memset(&v4l->v4l2_fmt, 0, sizeof(v4l->v4l2_fmt));
  
  /* Set up pixelformat */
  pixelformats = get_pixelformats(v4l->fd);

  format->pixelformat = gavl_pixelformat_get_best(format->pixelformat,
                                                  pixelformats, NULL);

  gavl_log(GAVL_LOG_DEBUG, LOG_DOMAIN, "Using %s", gavl_pixelformat_to_string(format->pixelformat));

  /* Set up image size */
  v4l->v4l2_fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
  v4l->v4l2_fmt.fmt.pix.width  = format->image_width;
  v4l->v4l2_fmt.fmt.pix.height = format->image_height;
  v4l->v4l2_fmt.fmt.pix.pixelformat = bgv4l2_pixelformat_gavl_2_v4l2(format->pixelformat);

  if (-1 == bgv4l2_ioctl (v4l->fd, VIDIOC_S_FMT, &v4l->v4l2_fmt))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "VIDIOC_S_FMT failed: %s", strerror(errno));
    return 0;
    }
  
  gavl_video_format_copy(&v4l->gavl_fmt, format);

  /* Set up buffers */
  switch(v4l->io)
    {
    case BGV4L2_IO_METHOD_RW:
      ret = init_write(v4l);
      break;
    case BGV4L2_IO_METHOD_MMAP:
      ret = init_mmap(v4l);
    default:
      return 0;
    }

  if(ret)
    v4l->sink = gavl_video_sink_create(get_frame_v4l2,
                                       put_func_v4l2,
                                       v4l, format);

#endif
  return 0;
  }

static gavl_video_sink_t * get_sink_v4l2(void * data)
  {
  ov_v4l2_t * v4l = data;
  return v4l->sink;
  }

static bg_controllable_t * get_controllable_v4l2(void * data)
  {
  ov_v4l2_t * v4l = data;
  return &v4l->ctrl;
  }

static void close_v4l2(void * data)
  {
  // Noop (for now)
  }

static char const * const protocols = V4L2_PROTOCOL_OUTPUT;

static const char * get_protocols_v4l2(void * priv)
  {
  return protocols;
  }


const bg_ov_plugin_t the_plugin =
  {
    .common =
    {
      BG_LOCALE,
      .name =          "ov_v4l2",
      .long_name =     TRS("V4L2"),
      .description =   TRS("V4L2 output driver"),
      .type =          BG_PLUGIN_OUTPUT_VIDEO,
      .flags =         0,
      .priority =      1,
      .create =        create_v4l2,
      .destroy =       destroy_v4l2,

      //      .get_parameters = get_parameters_v4l2,
      //      .set_parameter =  set_parameter_v4l2,
      //      .get_parameter =  get_parameter_v4l2,
      .get_controllable = get_controllable_v4l2,
      .get_protocols    = get_protocols_v4l2,
    },
    //  .set_callbacks =  set_callbacks_v4l2,
    
    //    .set_window =         set_window_v4l2,
    //    .get_window =         get_window_v4l2,
    //    .set_window_options =   set_window_options_v4l2,
    //    .set_window_title =   set_window_title_v4l2,
    .open =               open_v4l2,
    .get_sink = get_sink_v4l2,
    
    //    .add_overlay_stream = add_overlay_stream_v4l2,
    //    .create_overlay =    create_overlay_v4l2,
    //    .set_overlay =        set_overlay_v4l2,

    
    //    .handle_events =  handle_events_v4l2,
    
    //    .destroy_overlay =   destroy_overlay_v4l2,
    .close =          close_v4l2,
    //    .update_aspect =  update_aspect_v4l2,
  };

/* Include this into all plugin modules exactly once
   to let the plugin loader obtain the API version */
BG_GET_PLUGIN_API_VERSION;
