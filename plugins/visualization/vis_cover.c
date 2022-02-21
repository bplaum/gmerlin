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

#include <config.h> 

#include <gavl/gavl.h> 

#include <gmerlin/translation.h> 
#include <gmerlin/log.h> 

#include <gmerlin/visualize.h> 
#include <gmerlin/playermsg.h> 


#define LOG_DOMAIN "vis_cover"

#include <gmerlin/plugin.h> 

typedef struct
  {
  gavl_audio_sink_t * asink;
  gavl_video_source_t * vsource;

  gavl_video_format_t default_vfmt;
  gavl_video_format_t vfmt;
  
  gavl_video_frame_t * frame;

  bg_controllable_t ctrl;
  
  } cover_t;

static gavl_sink_status_t put_audio_func(void * priv, gavl_audio_frame_t * f)
  {
  return GAVL_SINK_OK;
  }

static gavl_source_status_t render_frame(void * priv, gavl_video_frame_t ** frame)
  {
  cover_t * s = priv;
  *frame = s->frame;
  return GAVL_SOURCE_OK;
  }

static int open_cover(void * priv, gavl_audio_format_t * audio_format,
                      gavl_video_format_t * video_format)
  {
  cover_t * s = priv;
  
  //  fprintf(stderr, "**** Open Cover\n");
  
  gavl_video_format_copy(&s->vfmt, video_format);
  
  bg_visualize_set_format(&s->vfmt, &s->default_vfmt);

  s->vfmt.pixelformat = GAVL_YUV_420_P;
  s->vfmt.pixel_width = 1;
  s->vfmt.pixel_height = 1;

  gavl_video_format_set_frame_size(&s->vfmt, 0, 0);

#if 0  
  fprintf(stderr, "open cover 0:\n");
  gavl_video_format_dump(video_format);
  fprintf(stderr, "open cover 1:\n");
  gavl_video_format_dump(&s->vfmt);
  fprintf(stderr, "open cover 2:\n");
  gavl_video_format_dump(&s->default_vfmt);
#endif
  
  gavl_video_format_copy(video_format, &s->vfmt);

  s->frame = gavl_video_frame_create(&s->vfmt);
  gavl_video_frame_clear(s->frame, &s->vfmt);
  
  if(audio_format->samples_per_frame > 512)
    audio_format->samples_per_frame = 512;
  
  s->vsource = gavl_video_source_create(render_frame, s, GAVL_SOURCE_SRC_ALLOC, &s->vfmt);

  s->asink = gavl_audio_sink_create(NULL, put_audio_func, s, audio_format);
  
  return 1;
  }

static int load_frame(cover_t * s, const gavl_dictionary_t * track)
  {
  fprintf(stderr, "*** Load frame\n");
  //  gavl_dictionary_dump(track, 2);

  fprintf(stderr, "format\n");
  gavl_video_format_dump(&s->vfmt);
  fprintf(stderr, "default_format\n");
  gavl_video_format_dump(&s->default_vfmt);
  
  if(s->frame)
    gavl_video_frame_destroy(s->frame);
  
  if(!s->default_vfmt.image_width || !s->default_vfmt.image_height)
    {
    s->frame = NULL;
    }
  else
    s->frame = bg_plugin_registry_load_cover_cnv(bg_plugin_reg,
                                                 &s->vfmt,
                                                 gavl_track_get_metadata(track));
  if(!s->frame)
    {
    s->frame = gavl_video_frame_create(&s->vfmt);
    gavl_video_frame_clear(s->frame, &s->vfmt);
    }
  
  return 1;
  }

static int handle_msg(void * priv, gavl_msg_t * msg)
  {
  cover_t * s = priv;
  
  switch(msg->NS)
    {
    case BG_MSG_NS_STATE:
      switch(msg->ID)
        {
        case BG_MSG_STATE_CHANGED:
          {
          gavl_value_t val;
          int last = 0;
          const char * ctx;
          const char * var;
          
          gavl_value_init(&val);
          
          bg_msg_get_state(msg, &last, &ctx,
                           &var, &val, NULL);

          if(!strcmp(ctx, BG_PLAYER_STATE_CTX))
            {

            
            if(!strcmp(var, BG_PLAYER_STATE_CURRENT_TRACK))
              {
              //              fprintf(stderr, "vis_cover: Got track\n");
              //              gavl_dictionary_dump(gavl_value_get_dictionary(&val), 2);
              
              load_frame(s, gavl_value_get_dictionary(&val));
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

static gavl_audio_sink_t * get_sink_cover(void * priv)
  {
  cover_t * s = priv;
  return s->asink;
  }

static gavl_video_source_t * get_source_cover(void * priv)
  {
  cover_t * s = priv;
  return s->vsource;
  }

static void * create_cover()
  {
  cover_t * s = calloc(1, sizeof(*s));

  bg_controllable_init(&s->ctrl,
                       bg_msg_sink_create(handle_msg, s, 1),
                       bg_msg_hub_create(1));
  
  return s;
  }


static void close_cover(void * priv)
  {
  cover_t * s = priv;

  if(s->vsource)
    {
    gavl_video_source_destroy(s->vsource);
    s->vsource = NULL;
    }
  if(s->asink)
    {
    gavl_audio_sink_destroy(s->asink);
    s->asink = NULL;
    }

  if(s->frame)
    {
    gavl_video_frame_destroy(s->frame);
    s->frame = NULL;
    }
  
  }

static void destroy_cover(void * priv)
  {
  cover_t * s = priv;
  close_cover(priv);
  bg_controllable_cleanup(&s->ctrl);
  free(s);
  }


static const bg_parameter_info_t parameters[] =
  {
    BG_VISUALIZE_PLUGIN_PARAM_SIZE(1920, 1080),
    BG_VISUALIZE_PLUGIN_PARAM_FRAMERATE(10.0),
    { /* */ }
  };

static const bg_parameter_info_t * get_parameters_cover(void * data)
  {
  return parameters;
  }

static void set_parameter_cover(void * priv, const char * name,
                         const gavl_value_t * val)
  {
  cover_t * p = priv;

  if(!name)
    return;
  
  if(bg_visualize_set_format_parameter(&p->default_vfmt, name, val))
    return;
  
  }

static bg_controllable_t * get_controllable_cover(void * priv)
  {
  cover_t * s = priv;

  return &s->ctrl;
  }

const bg_visualization_plugin_t the_plugin = 
  {
    .common =
    {
      BG_LOCALE,
      .name =      "vis_cover",
      .long_name = TRS("Show cover"),
      .description = TRS("Show album cover"),
      .type =     BG_PLUGIN_VISUALIZATION,
      .flags =    0,
      .create =   create_cover,
      .destroy =   destroy_cover,
      .priority =         1,
      .get_parameters = get_parameters_cover,
      .set_parameter = set_parameter_cover,
      .get_controllable = get_controllable_cover,
    },
    .open = open_cover,
    .get_source = get_source_cover,
    .get_sink = get_sink_cover,
    .close = close_cover
  };

/* Include this into all plugin modules exactly once
   to let the plugin loader obtain the API version */
BG_GET_PLUGIN_API_VERSION;
