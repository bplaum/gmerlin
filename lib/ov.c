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

#include <config.h>
#include <stdlib.h>
#include <string.h>

#include <gmerlin/pluginregistry.h>
#include <gmerlin/log.h>
#include <gmerlin/state.h>
#include <gmerlin/ov.h>

#define LOG_DOMAIN "ov"

#define LOCK(p)   bg_plugin_lock(p->h)
#define UNLOCK(p) bg_plugin_unlock(p->h)

#define FLAG_EMULATE_OVL     (1<<0)
#define FLAG_EMULATE_STILL   (1<<1)
#define FLAG_STILL_MODE      (1<<2)
#define FLAG_OPEN            (1<<3)
#define FLAG_OVERLAY_CHANGED (1<<4)

typedef struct
  {
  gavl_overlay_blend_context_t * ctx;
  gavl_overlay_t * ovl;
  gavl_video_format_t format;
    
  gavl_video_sink_t * sink_int;
  gavl_video_sink_t * sink_ext;
  } ovl_stream_t;

struct bg_ov_s
  {
  bg_plugin_handle_t * h;
  bg_ov_plugin_t * plugin;
  void * priv;
  gavl_video_format_t format;

  int flags;

  ovl_stream_t ** ovl_str;
  int num_ovl_str;
  gavl_video_frame_t * still_frame;

  gavl_video_sink_t * sink_ext;
  gavl_video_sink_t * sink_int;

  gavl_dictionary_t ov_state;
  
  };

bg_ov_t * bg_ov_create(bg_plugin_handle_t * h)
  {
  bg_ov_t * ret = calloc(1, sizeof(*ret));

  ret->h = h;
  bg_plugin_ref(h);
  
  ret->priv = h->priv;
  ret->plugin = (bg_ov_plugin_t*)ret->h->plugin;
  return ret;
  }

bg_plugin_handle_t * bg_ov_get_plugin(bg_ov_t * ov)
  {
  return ov->h;
  }


void bg_ov_destroy(bg_ov_t * ov)
  {
  if(ov->flags & FLAG_OPEN)
    {
    LOCK(ov);
    ov->plugin->close(ov->priv);
    UNLOCK(ov);
    }
  bg_plugin_unref(ov->h);
  if(ov->sink_ext)
    gavl_video_sink_destroy(ov->sink_ext);

  gavl_dictionary_free(&ov->ov_state);
  free(ov);
  }

void bg_ov_set_window(bg_ov_t * ov, const char * window_id)
  {
  gavl_value_t val;
  gavl_value_init(&val);
  gavl_value_set_string(&val, window_id);
  bg_plugin_handle_set_state(ov->h, BG_STATE_CTX_OV, BG_STATE_OV_WINDOW_ID, &val);
  gavl_value_free(&val);
  }


void bg_ov_set_window_title(bg_ov_t * ov, const char * title)
  {
  bg_ov_plugin_set_window_title(ov->h, title);
  }

static gavl_video_frame_t * get_frame_func(void * priv)
  {
  gavl_video_frame_t * ret;
  bg_ov_t * ov = priv;
  LOCK(ov);
  ret = gavl_video_sink_get_frame(ov->sink_int);
  UNLOCK(ov);
  return ret;
  }

static void blend_overlays(bg_ov_t * ov, gavl_video_frame_t * f)
  {
  int i;
  for(i = 0; i < ov->num_ovl_str; i++)
    {
    if(ov->ovl_str[i]->ovl)
      {
      gavl_overlay_blend(ov->ovl_str[i]->ctx, f);
      }
    }
  ov->flags &= ~FLAG_OVERLAY_CHANGED;
  }

static gavl_sink_status_t put_video(bg_ov_t * ov, gavl_video_frame_t*frame)
  {
  gavl_sink_status_t ret;

  //  fprintf(stderr, "Put video %p\n", frame);
  
  ov->flags &= ~FLAG_STILL_MODE;
  
  if(ov->flags & FLAG_EMULATE_OVL)
    blend_overlays(ov, frame);
  LOCK(ov);
  ret = gavl_video_sink_put_frame(ov->sink_int, frame);
  UNLOCK(ov);

  bg_ov_handle_events(ov);
  
  return ret;
  }

static gavl_sink_status_t put_still(bg_ov_t * ov, gavl_video_frame_t*frame)
  {
  gavl_sink_status_t ret;
  
  //  fprintf(stderr, "Put still %p\n", frame);
  
  ov->flags |= FLAG_STILL_MODE;
  if(!(ov->plugin->common.flags & BG_PLUGIN_OV_STILL))
    ov->flags |= FLAG_EMULATE_STILL;

  /* Save this frame */

  if(ov->flags & (FLAG_EMULATE_STILL|FLAG_EMULATE_OVL))
    {
    if(!ov->still_frame)
      ov->still_frame = gavl_video_frame_create(&ov->format);

    gavl_video_frame_copy(&ov->format, ov->still_frame, frame);
    ov->still_frame->duration = -1;
    }
  
  if(ov->flags & FLAG_EMULATE_OVL)
    blend_overlays(ov, frame);
  
  LOCK(ov);
  ret = gavl_video_sink_put_frame(ov->sink_int, frame);
  UNLOCK(ov);

  bg_ov_handle_events(ov);
  
  return ret;
  }


static gavl_sink_status_t put_frame_func(void * priv, gavl_video_frame_t*frame)
  {
  bg_ov_t * ov = priv;
  if(frame->duration < 0)
    return put_still(ov, frame);
  else
    return put_video(ov, frame);
  }

int bg_ov_open(bg_ov_t * ov, gavl_video_format_t * format, int keep_aspect)
  {
  int ret;

  //  fprintf(stderr, "open_ov\n");
  
  LOCK(ov);
  ret = ov->plugin->open(ov->priv, format, keep_aspect);
  if(ret)
    ov->sink_int = ov->plugin->get_sink(ov->priv);
  UNLOCK(ov);

  if(!ret)
    return ret;
  
  gavl_video_format_copy(&ov->format, format);
  ov->flags = FLAG_OPEN;
  
  ov->sink_ext = gavl_video_sink_create(get_frame_func,
                                        put_frame_func, ov, format);
  
  //  fprintf(stderr, "open_ov done\n");

  return ret;
  }

gavl_video_sink_t * bg_ov_get_sink(bg_ov_t * ov)
  {
  return ov->sink_ext;
  }

static gavl_sink_status_t
put_overlay(void * priv, gavl_video_frame_t * frame)
  {
  ovl_stream_t * str = priv;

  if(frame && frame->src_rect.w && frame->src_rect.h)
    {
    str->ovl = frame;
    gavl_overlay_blend_context_set_overlay(str->ctx,
                                           str->ovl);
    }
  else
    str->ovl = NULL;
  return GAVL_SINK_OK;
  }

gavl_video_sink_t *
bg_ov_add_overlay_stream(bg_ov_t * ov, gavl_video_format_t * format)
  {
  ovl_stream_t * str;

  ov->ovl_str = realloc(ov->ovl_str,
                        (ov->num_ovl_str+1)*
                        sizeof(*ov->ovl_str));
  str = calloc(1, sizeof(*str));
  ov->ovl_str[ov->num_ovl_str] = str;
  
  ov->num_ovl_str++;
 
  if(!format->image_width || !format->image_height)
    {
    format->image_width = ov->format.image_width;
    format->image_height = ov->format.image_height;
    format->pixel_width = ov->format.pixel_width;
    format->pixel_height = ov->format.pixel_height;
    gavl_video_format_set_frame_size(format, 0, 0);

    }
 
  if(ov->plugin->add_overlay_stream)
    {
    /* Try hardware overlay */
    LOCK(ov);
    str->sink_int = ov->plugin->add_overlay_stream(ov->priv, format);
    UNLOCK(ov);
    
    if(str->sink_int)
      {
      gavl_log(GAVL_LOG_INFO, LOG_DOMAIN,
             "Using hardeware overlay for stream %d",
             ov->num_ovl_str-1);
      return str->sink_int;
      }
    }
  /* Software overlay */
  ov->flags |= FLAG_EMULATE_OVL;
  
  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN,
         "Using software overlay for stream %d",
         ov->num_ovl_str - 1);
  
  str->ctx = gavl_overlay_blend_context_create();
  
  gavl_overlay_blend_context_init(str->ctx,
                                  &ov->format, format);
  str->sink_ext = gavl_video_sink_create(NULL, put_overlay, str, format);
  
  gavl_video_format_copy(&str->format, format);
  
  return str->sink_ext;
  }

void bg_ov_handle_events(bg_ov_t * ov)
  {
  if(ov->flags & FLAG_STILL_MODE)
    {
    if(ov->flags & (FLAG_EMULATE_STILL|FLAG_OVERLAY_CHANGED))
      {
      gavl_video_frame_t * f;
      LOCK(ov);
      f = gavl_video_sink_get_frame(ov->sink_int);
      gavl_video_frame_copy(&ov->format, f, ov->still_frame);
      
      if(ov->flags & FLAG_EMULATE_OVL)
        blend_overlays(ov, f);

      gavl_video_sink_put_frame(ov->sink_int, f);
      UNLOCK(ov);
      }
    }
  
  if(ov->plugin->handle_events)
    {
    LOCK(ov);
    ov->plugin->handle_events(ov->priv);
    UNLOCK(ov);
    }
  }

  
void bg_ov_close(bg_ov_t * ov)
  {
  
  if(!(ov->flags & FLAG_OPEN))
    return;

  //  fprintf(stderr, "close_ov\n");
  
  LOCK(ov);
  ov->plugin->close(ov->priv);
  UNLOCK(ov);

  if(ov->still_frame)
    {
    gavl_video_frame_destroy(ov->still_frame);
    ov->still_frame = NULL;
    }

  if(ov->num_ovl_str)
    {
    int i;
    for(i = 0; i < ov->num_ovl_str; i++)
      {
      if(ov->ovl_str[i]->ctx)
        gavl_overlay_blend_context_destroy(ov->ovl_str[i]->ctx);
      free(ov->ovl_str[i]);
      }
    free(ov->ovl_str);
    ov->ovl_str = NULL;
    ov->num_ovl_str = 0;
    }

  if(ov->sink_ext)
    {
    gavl_video_sink_destroy(ov->sink_ext);
    ov->sink_ext = NULL;
    }
  
  //  fprintf(stderr, "close_ov done\n");

  }

void bg_ov_show_window(bg_ov_t * ov, int show)
  {
  //  fprintf(stderr, "bg_ov_show_window %d\n", show);
  
  bg_ov_plugin_set_visible(ov->h, show);
  }

void bg_ov_set_fullscreen(bg_ov_t * ov, int fullscreen)
  {
  bg_ov_plugin_set_fullscreen(ov->h, fullscreen);
  }

const bg_state_var_desc_t bg_ov_state_vars[] =
  {
    { BG_STATE_OV_BRIGHTNESS,       GAVL_TYPE_FLOAT,      GAVL_VALUE_INIT_FLOAT(0.0),
      GAVL_VALUE_INIT_FLOAT(BG_BRIGHTNESS_MIN), GAVL_VALUE_INIT_FLOAT(BG_BRIGHTNESS_MAX) },
    { BG_STATE_OV_SATURATION,       GAVL_TYPE_FLOAT,      GAVL_VALUE_INIT_FLOAT(0.0),
      GAVL_VALUE_INIT_FLOAT(BG_SATURATION_MIN), GAVL_VALUE_INIT_FLOAT(BG_SATURATION_MAX)   },
    { BG_STATE_OV_CONTRAST,         GAVL_TYPE_FLOAT,      GAVL_VALUE_INIT_FLOAT(0.0),
      GAVL_VALUE_INIT_FLOAT(BG_CONTRAST_MIN), GAVL_VALUE_INIT_FLOAT(BG_CONTRAST_MAX)   },
    { BG_STATE_OV_ZOOM,             GAVL_TYPE_FLOAT,      GAVL_VALUE_INIT_FLOAT(100.0),
      GAVL_VALUE_INIT_FLOAT(BG_OV_ZOOM_MIN), GAVL_VALUE_INIT_FLOAT(BG_OV_ZOOM_MAX) },
    { BG_STATE_OV_SQUEEZE,          GAVL_TYPE_FLOAT,      GAVL_VALUE_INIT_FLOAT(0.0),
      GAVL_VALUE_INIT_FLOAT(BG_OV_SQUEEZE_MIN), GAVL_VALUE_INIT_FLOAT(BG_OV_SQUEEZE_MAX)    },
    { BG_STATE_OV_FULLSCREEN,       GAVL_TYPE_INT,        GAVL_VALUE_INIT_INT(0)     },
    { BG_STATE_OV_VISIBLE,          GAVL_TYPE_INT,        GAVL_VALUE_INIT_INT(0)     },
    { BG_STATE_OV_TITLE,            GAVL_TYPE_STRING,     GAVL_VALUE_INIT_STRING("Video output") },
    { /* End */ },
  };
