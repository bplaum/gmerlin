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

#include <gavl/gavl.h>

#include <config.h>

#include <gmerlin/translation.h>

#include <gmerlin/player.h>

#include <playerprivate.h>


#include <gmerlin/pluginregistry.h>
#include <gmerlin/visualize.h>
#include <gmerlin/utils.h>
#include <gmerlin/bgmsg.h>
#include <gmerlin/ringbuffer.h>


#include <pthread.h>

#include <gmerlin/log.h>
#include <gmerlin/state.h>
#include <gmerlin/ov.h>

#define FLAG_RUNNING          (1<<0)
#define FLAG_VIS_INIT         (1<<1)
#define FLAG_VIS_CONNECTED    (1<<2)
#define FLAG_VIS_OV_CONNECTED (1<<3)

struct bg_visualizer_s
  {
  bg_plugin_handle_t * h;
  bg_visualization_plugin_t * plugin;
  bg_controllable_t * plugin_ctrl;
  gavl_value_t plugin_cfg;
  
  bg_ov_t * ov;

  bg_player_t * player;
  
  bg_controllable_t ctrl;

  int state;

  /* Protects ring buffer and internal source */
  pthread_mutex_t audio_mutex;
  
  gavl_audio_sink_t * asink_ext;


  gavl_audio_sink_t * asink_int; /* Owned by the visualization plugin */
  
  gavl_audio_source_t * asrc;

  gavl_video_sink_t * vsink;
  gavl_video_source_t * vsrc;
  
  bg_msg_sink_t * msink;
  bg_msg_hub_t * mhub;

  pthread_t th;

  gavl_video_format_t vfmt_vis; /* Produced by the visualization plugin */
  gavl_video_format_t vfmt_default; /* Default configuration.  */
  gavl_video_format_t vfmt_ov; /* Frames passed to the ov module */
  
  gavl_audio_format_t afmt_int; /* Sent to the visualization plugin */
  gavl_audio_format_t afmt_ext; /* Frames passed from the client    */

  int64_t audio_seq;
  
  bg_ring_buffer_t * buf;

  int flags;

  int64_t next_pts;
  int64_t sample_counter;
  
  bg_osd_t * osd;

  int got_pause;
  int got_resume;
  
  };

static void visualizer_init_nolock(bg_visualizer_t * v, const gavl_audio_format_t * fmt);


static void cleanup_audio(bg_visualizer_t * v)
  {
  if(v->asink_ext)
    {
    gavl_audio_sink_destroy(v->asink_ext);
    v->asink_ext = NULL;
    }
  
  if(v->buf)
    {
    bg_ring_buffer_destroy(v->buf);
    v->buf = NULL;
    }
  
  if(v->asrc)
    {
    gavl_audio_source_destroy(v->asrc);
    v->asrc = NULL;
    }
  }

static const bg_parameter_info_t parameters[] =
  {
    {
      .name =        "width",
      .long_name =   TRS("Default width"),
      .type =        BG_PARAMETER_INT,
      .val_min =     GAVL_VALUE_INIT_INT(16),
      .val_max =     GAVL_VALUE_INIT_INT(32768),
      .val_default = GAVL_VALUE_INIT_INT(1280),
      .help_string = TRS("Desired image with. The visualization plugin might override this."),
    },
    {
      .name =       "height",
      .long_name =  TRS("Default height"),
      .type =       BG_PARAMETER_INT,
      .val_min =    GAVL_VALUE_INIT_INT(16),
      .val_max =    GAVL_VALUE_INIT_INT(32768),
      .val_default = GAVL_VALUE_INIT_INT(720),
      .help_string = TRS("Desired image height. The visualization plugin might override this."),
    },
    {
      .name =        "framerate",
      .long_name =   TRS("Default framerate"),
      .type =        BG_PARAMETER_FLOAT,
      .val_min =     GAVL_VALUE_INIT_FLOAT(1.0),
      .val_max =     GAVL_VALUE_INIT_FLOAT(200.0),
      .val_default = GAVL_VALUE_INIT_FLOAT(30.0),
      .help_string = TRS("Target framerate. The visualization plugin might override this."),
      .num_digits = 2,
    },
    { /* End of parameters */ }
  };

const bg_parameter_info_t * bg_visualizer_get_parameters(void)
  {
  return parameters;
  }

void bg_visualizer_destroy(bg_visualizer_t * v)
  {
  if(v->h)
    bg_plugin_unref(v->h);
  
  if(v->msink)
    bg_msg_sink_destroy(v->msink);
  if(v->mhub)
    bg_msg_hub_destroy(v->mhub);
    
  if(v->asink_ext)
    gavl_audio_sink_destroy(v->asink_ext);
  
  if(v->buf)
    bg_ring_buffer_destroy(v->buf);

  if(v->asrc)
    gavl_audio_source_destroy(v->asrc);
  
  pthread_mutex_destroy(&v->audio_mutex);

  gavl_value_free(&v->plugin_cfg);

  free(v);
  }

static void load_plugin(bg_visualizer_t * v, int plugin)
  {
  //  fprintf(stderr, "load_plugin: %d\n", plugin);

  const gavl_array_t * arr;
  const gavl_dictionary_t * dict;

  if(!(dict = bg_plugin_config_get_section(BG_PLUGIN_VISUALIZATION)) ||
     !(arr = gavl_dictionary_get_array(dict, BG_PLUGIN_CONFIG_PLUGIN)) ||
     !(dict = gavl_value_get_dictionary(&arr->entries[plugin])))
    {
    if(dict)
      {
      fprintf(stderr, "load_plugin failed\n");
      gavl_dictionary_dump(dict, 2);
      }
    
    /* Error */
    bg_ov_show_window(v->ov, 0);
    return;
    }
  
  if(v->plugin_ctrl)
    {
    bg_controllable_t * ctrl = NULL;
    bg_plugin_handle_t * h = bg_ov_get_plugin(v->ov);

    bg_msg_hub_disconnect_sink(v->mhub, v->plugin_ctrl->cmd_sink);

    if((h->plugin->get_controllable) &&
       (ctrl = h->plugin->get_controllable(h->priv)))
      bg_msg_hub_disconnect_sink(ctrl->evt_hub, v->plugin_ctrl->cmd_sink);
    
    v->flags &= ~FLAG_VIS_OV_CONNECTED;
    
    v->plugin_ctrl = NULL;
    }
  
  v->flags &= ~FLAG_VIS_CONNECTED;
  
  if(v->h)
    {
    bg_ov_close(v->ov);
    bg_plugin_unref(v->h);
    v->h = NULL;
    v->plugin = NULL;
    v->asink_int = NULL;
    v->plugin_ctrl = NULL;
    v->vsrc = NULL;
    }

  if(!(v->h = bg_plugin_load_with_options(dict)))
    {
    bg_ov_show_window(v->ov, 0);
    return;
    }

  v->plugin = (bg_visualization_plugin_t*)v->h->plugin;
    
  if(v->h->plugin->get_controllable)
    v->plugin_ctrl = v->h->plugin->get_controllable(v->h->priv);
  else
    v->plugin_ctrl = NULL;
  }

void bg_visualizer_set_parameter(void * priv, const char * name, const gavl_value_t * val)
  {
  bg_visualizer_t * v = priv;

  if(!name)
    {
    gavl_video_format_set_frame_size(&v->vfmt_default, 1, 1);
    }
  else if(!strcmp(name, "width"))
    {
    v->vfmt_default.image_width = val->v.i;
    }
  else if(!strcmp(name, "height"))
    {
    v->vfmt_default.image_height = val->v.i;
    }
  else if(!strcmp(name, "framerate"))
    {
    /*
     * This makes timing calculations the most simple.
     *
     * We assume this video timescale implicitly later on.
     */
    
    v->vfmt_default.timescale      = GAVL_TIME_SCALE;
    v->vfmt_default.frame_duration = (int)((double)GAVL_TIME_SCALE / val->v.d + 0.5);
    
    }
   
  }

static int handle_message(void * priv, gavl_msg_t * msg)
  {
  bg_visualizer_t * v = priv;

  switch(msg->NS)
    {
    case BG_MSG_NS_VISUALIZER:
      switch(msg->ID)
        {
        case BG_CMD_VISUALIZER_SET_PLUGIN:
          {
          gavl_audio_format_t last_fmt;
          int idx = gavl_msg_get_arg_int(msg, 0);
          
          /* Load another visualization */
          pthread_mutex_lock(&v->audio_mutex);

          v->flags &= ~FLAG_VIS_INIT;
      
          gavl_audio_format_copy(&last_fmt, &v->afmt_ext);
      
          load_plugin(v, idx);
          
          bg_ov_close(v->ov);
          
          visualizer_init_nolock(v, &last_fmt);
      
          pthread_mutex_unlock(&v->audio_mutex);
      
          /* Reset time */
          v->next_pts = GAVL_TIME_UNDEFINED;
          v->sample_counter = 0;
          }
          return 1;
          break;
        case BG_CMD_VISUALIZER_PAUSE:
          //          fprintf(stderr, "Pause visualization\n");
          v->got_pause = 1;
          return 1;
          break;
        }
      break;
    default:
      {
      //      fprintf(stderr, "Visualize: Handle message\n");
      //      gavl_msg_dump(

      /* Pass to plugin */
      bg_msg_sink_put_copy(bg_msg_hub_get_sink(v->mhub), msg);
      }
    }
  
  return 1;
  }

static void * visualize_thread(void * priv)
  {
  gavl_source_status_t st;
  gavl_audio_frame_t * aframe;
  gavl_video_frame_t * vframe;
  
  bg_visualizer_t * v = priv;

  gavl_time_t cur_time;
  gavl_time_t diff_time;
  gavl_timer_t * timer;

  //  int got_aframe = 0;
  
  //  fprintf(stderr, "started visualization thread\n");

  v->next_pts = 0;
  v->sample_counter = 0;
  
  timer = gavl_timer_create();
  gavl_timer_start(timer);
  
  if(v->player)
    {
    bg_controllable_t * ctrl = bg_player_get_controllable(v->player);
    bg_msg_hub_connect_sink(ctrl->evt_hub, v->msink);
    }
  
  while(1)
    {
    /* Process messages */

    if(!bg_msg_sink_iteration(v->msink))
      {
      //      fprintf(stderr, "bg_msg_sink_iteration returned 0\n");
      break;
      }
    //    else
    //      fprintf(stderr, "bg_msg_sink_iteration returned 1\n");
    
    /* Process audio frames */

    pthread_mutex_lock(&v->audio_mutex);

    if(!v->asrc)
      {
      gavl_time_t delay_time;
      
      pthread_mutex_unlock(&v->audio_mutex);
      delay_time = GAVL_TIME_SCALE / 10;

      gavl_time_delay(&delay_time);
      
      continue;
      }
    
    aframe = NULL;
    
    while((st = gavl_audio_source_read_frame(v->asrc, &aframe) == GAVL_SOURCE_OK))
      {
      /* Resync after pause */
      if(v->sample_counter < 0)
        v->sample_counter = gavl_time_scale(v->afmt_int.samplerate,
                                            v->next_pts - v->vfmt_vis.frame_duration / 2);

      v->sample_counter += aframe->valid_samples;

#if 0      
      fprintf(stderr, "Put audio %"PRId64" %"PRId64" %"PRId64" %d\n",
              v->next_pts,
              gavl_time_unscale(v->afmt_int.samplerate, v->sample_counter),
              v->next_pts -
              gavl_time_unscale(v->afmt_int.samplerate, v->sample_counter),
              aframe->valid_samples);
#endif 

      if(v->asink_int)
        gavl_audio_sink_put_frame(v->asink_int, aframe);
      
      if(v->next_pts == GAVL_TIME_UNDEFINED)
        {
        gavl_time_t t = gavl_time_unscale(v->afmt_int.samplerate, v->sample_counter);
        gavl_timer_set(timer, t);
        v->next_pts = t + v->vfmt_vis.frame_duration / 2;
        }
      
      
      /* Read maximum 1 video frame ahead */
      if(gavl_time_unscale(v->afmt_int.samplerate, v->sample_counter) >=  v->next_pts)
        break;
      
      aframe = NULL;
      //      got_aframe = 1;
      }

    if(v->next_pts == GAVL_TIME_UNDEFINED)
      {
      gavl_time_t delay_time;
      
      pthread_mutex_unlock(&v->audio_mutex);
      delay_time = GAVL_TIME_SCALE / 100;
      gavl_time_delay(&delay_time);
      continue;
      }
    
    if((st != GAVL_SOURCE_OK))
      {
      if(v->got_pause)
        {
        gavl_audio_frame_t * tmp_frame;
      
        /* Send one muted frame */
        tmp_frame = gavl_audio_frame_create(&v->afmt_int);
        gavl_audio_frame_mute(tmp_frame, &v->afmt_int);
        gavl_audio_sink_put_frame(v->asink_int, tmp_frame);
        gavl_audio_frame_destroy(tmp_frame);
        v->got_pause = 0;
        v->sample_counter = -1;
        }
      else
        {
        if(v->next_pts > gavl_time_unscale(v->afmt_int.samplerate, v->sample_counter))
          v->sample_counter = gavl_time_scale(v->afmt_int.samplerate, v->next_pts + v->vfmt_vis.frame_duration);
        }
      }
    
    pthread_mutex_unlock(&v->audio_mutex);
    
    /* Render image */

    vframe = NULL;

    if(v->vsrc)
      {
      gavl_video_source_read_frame(v->vsrc, &vframe);

      //    fprintf(stderr, "Get video %"PRId64"\n", v->next_pts);
    
      vframe->timestamp = v->next_pts;
      vframe->duration = v->vfmt_vis.frame_duration;
    
      v->next_pts += v->vfmt_vis.frame_duration;

      /* Check for resync */

      //    got_aframe = 0;
    
      /* Show image */

      if(v->osd)
        bg_osd_update(v->osd);
    
      cur_time = gavl_timer_get(timer);

      diff_time = vframe->timestamp - cur_time;

      //    fprintf(stderr, "F: %"PRId64" C: %"PRId64" D: %"PRId64"\n",
      //            vframe->timestamp, cur_time, diff_time);
    
      if(diff_time > 0)
        gavl_time_delay(&diff_time);
      else if(cur_time > v->next_pts) // Skip frames
        {
        v->next_pts = ((cur_time / v->vfmt_vis.frame_duration) + 1) * v->vfmt_vis.frame_duration;
        }
    
      gavl_video_sink_put_frame(v->vsink, vframe);
      bg_ov_handle_events(v->ov);
      
      }
    
    
    }

  gavl_timer_destroy(timer);

  if(v->player)
    {
    bg_controllable_t * ctrl = bg_player_get_controllable(v->player);
    bg_msg_hub_disconnect_sink(ctrl->evt_hub, v->msink);
    }

  if((v->flags & FLAG_VIS_OV_CONNECTED))
    {
    bg_controllable_t * ctrl = NULL;
    bg_plugin_handle_t * h = bg_ov_get_plugin(v->ov);
      
    if((h->plugin->get_controllable) &&
       (ctrl = h->plugin->get_controllable(h->priv)))
      bg_msg_hub_disconnect_sink(ctrl->evt_hub, v->plugin_ctrl->cmd_sink);
      
    v->flags &= ~FLAG_VIS_OV_CONNECTED;
    }
  
  //  fprintf(stderr, "finishing visualization thread\n");
  
  return NULL;
  }

void bg_visualizer_start(bg_visualizer_t * vis, bg_ov_t * ov)
  {
  vis->ov = ov;
  
  //  pthread_create();
  
  if(vis->flags & FLAG_RUNNING)
    return;
  
  //  fprintf(stderr, "bg_visualizer_start %p\n", vis);

  pthread_create(&vis->th, NULL, visualize_thread, vis);
  
  vis->flags |= FLAG_RUNNING;
  }

void bg_visualizer_stop(bg_visualizer_t * vis)
  {
  //  fprintf(stderr, "bg_visualizer_stop %p %p\n", vis, vis->msink);

  if(vis->flags & FLAG_RUNNING)
    {
    gavl_msg_t * msg = bg_msg_sink_get(vis->msink);
    gavl_msg_set_id_ns(msg, GAVL_CMD_QUIT, GAVL_MSG_NS_GENERIC);
    bg_msg_sink_put(vis->msink);

    //    fprintf(stderr, "pthread_join...");
    pthread_join(vis->th, NULL);
    //    fprintf(stderr, "done\n");
    }
  
  bg_ov_close(vis->ov);

  vis->flags &= ~(FLAG_VIS_INIT|FLAG_RUNNING); 
  
  cleanup_audio(vis);
  }

bg_visualizer_t * bg_visualizer_create(bg_player_t * player)
  {
  bg_visualizer_t * ret;
  ret = calloc(1, sizeof(*ret));

  ret->msink = bg_msg_sink_create(handle_message, ret, 0);
  ret->mhub = bg_msg_hub_create(1);
  
  pthread_mutex_init(&ret->audio_mutex, NULL);
  
  
  if(player)
    {
    ret->player = player;
    ret->osd = ret->player->video_stream.osd;
    }

  
  
  return ret;
  }

static gavl_sink_status_t sink_put_func(void * priv, gavl_audio_frame_t * f)
  {
  bg_visualizer_t * v = priv;

  bg_ring_buffer_write(v->buf, f);
  
  //  fprintf(stderr, "Visualizer Put frame\n");
  
  return GAVL_SINK_OK;
  }

static gavl_source_status_t get_audio(void * priv, gavl_audio_frame_t ** frame)
  {
  bg_visualizer_t * v = priv;

  if(!bg_ring_buffer_read(v->buf, *frame, &v->audio_seq))
    return GAVL_SOURCE_AGAIN;
  else
    return GAVL_SOURCE_OK;
  
  }

static void init_visualization(bg_visualizer_t * v)
  {
  gavl_audio_format_copy(&v->afmt_int, &v->afmt_ext);
  gavl_video_format_copy(&v->vfmt_vis, &v->vfmt_default);

  //  fprintf(stderr, "init_visualization\n");
  //  gavl_video_format_dump(&v->vfmt_default);
  
  if(!v->plugin->open(v->h->priv, &v->afmt_int, &v->vfmt_vis))
    {
    //    fprintf(stderr, "Opening plugin failed\n");
    }

  v->vsrc = v->plugin->get_source(v->h->priv);
  
  //  fprintf(stderr, "init_visualization 2\n");
  //  gavl_audio_format_dump(&v->afmt_int);

  
  v->asink_int = v->plugin->get_sink(v->h->priv);

  gavl_video_format_copy(&v->vfmt_ov, &v->vfmt_vis);
    
  if(bg_ov_open(v->ov, NULL, &v->vfmt_ov,
                gavl_video_source_get_src_flags(v->vsrc)))
    {
#if 0
    fprintf(stderr, "Opened OV for visualization\n");
    gavl_video_format_dump(&v->vfmt_ov);
    fprintf(stderr, "vis:\n");
    gavl_video_format_dump(&v->vfmt_vis);
#endif

    v->vsink = bg_ov_get_sink(v->ov);
    bg_ov_show_window(v->ov, 1);
    }
    
  if(v->osd)
    {
    gavl_video_sink_t * osd_sink; 
    gavl_video_format_t osd_fmt;
    gavl_video_source_t * osd_src;
    
    memset(&osd_fmt, 0, sizeof(osd_fmt));
      
    //    fprintf(stderr, "Opened OSD for visualization\n");

    bg_osd_clear(v->osd);

    osd_src = bg_osd_init(v->osd, &v->vfmt_ov);
    
    gavl_video_format_copy(&osd_fmt, gavl_video_source_get_src_format(osd_src));
    osd_sink = bg_ov_add_overlay_stream(v->ov, &osd_fmt);
    bg_osd_set_sink(v->osd, osd_sink);
    }

  if(v->plugin_ctrl)
    {
    if(!(v->flags & FLAG_VIS_CONNECTED))
      {
      bg_msg_hub_connect_sink(v->mhub, v->plugin_ctrl->cmd_sink);
      v->flags |= FLAG_VIS_CONNECTED;
      }

    if(!(v->flags & FLAG_VIS_OV_CONNECTED))
      {
      bg_controllable_t * ctrl = NULL;
      bg_plugin_handle_t * h = bg_ov_get_plugin(v->ov);
      
      if((h->plugin->get_controllable) &&
         (ctrl = h->plugin->get_controllable(h->priv)))
        bg_msg_hub_connect_sink(ctrl->evt_hub, v->plugin_ctrl->cmd_sink);
      
      v->flags |= FLAG_VIS_OV_CONNECTED;
      }
    }


  gavl_video_source_set_dst(v->vsrc, 0, &v->vfmt_ov);
    
  v->flags |= FLAG_VIS_INIT;
  
  }

static void visualizer_init_nolock(bg_visualizer_t * v, const gavl_audio_format_t * fmt)
  {
  //  fprintf(stderr, "bg_visualizer_init_nolock\n");
  //  gavl_audio_format_dump(fmt);

  /* Close down earlier visualization */
  cleanup_audio(v);
  
  gavl_audio_format_copy(&v->afmt_ext, fmt);
  
  v->asink_ext = gavl_audio_sink_create(NULL, sink_put_func, v, &v->afmt_ext);

  v->asrc = gavl_audio_source_create(get_audio, v, 0,  &v->afmt_ext);

  v->buf = bg_ring_buffer_create_audio(3, &v->afmt_ext,
                                       BG_RINGBUFFER_OVERWRITE | BG_RINGBUFFER_SINGLE_READER);

  v->audio_seq = 0;

  if(!(v->flags & FLAG_VIS_INIT) && v->plugin)
    init_visualization(v);
  
  gavl_audio_source_set_dst(v->asrc, 0, &v->afmt_int);

  
  }

int bg_visualizer_init(bg_visualizer_t * v, const gavl_audio_format_t * fmt)
  {
  //  fprintf(stderr, "bg_visualizer_init\n");
  //  gavl_audio_format_dump(fmt);
  
  //  gavl_dictionary_dump(metadata, 2);
  
  pthread_mutex_lock(&v->audio_mutex);
  visualizer_init_nolock(v, fmt);
  pthread_mutex_unlock(&v->audio_mutex);
  
  return 1;
  }

int bg_visualize_set_format_parameter(gavl_video_format_t * fmt, const char * name,
                                      const gavl_value_t * val)
  {
  if(!strcmp(name, "width"))
    fmt->image_width = val->v.i;
  else if(!strcmp(name, "height"))
    fmt->image_height = val->v.i;
  else if(!strcmp(name, "framerate"))
    {
    fmt->timescale      = GAVL_TIME_SCALE;
    fmt->frame_duration = (int)((double)GAVL_TIME_SCALE / val->v.d + 0.5);
    }
  else
    return 0;

  return 1;
  }

void bg_visualize_set_format(gavl_video_format_t * fmt,
                            const gavl_video_format_t * default_fmt)
  {
  if(!fmt->image_width)
    fmt->image_width = default_fmt->image_width;
  if(!fmt->image_height)
    fmt->image_height = default_fmt->image_height;

  if(fmt->frame_duration > GAVL_TIME_SCALE)
    fmt->frame_duration = default_fmt->frame_duration;
  
  }

void bg_visualizer_update(bg_visualizer_t * v, gavl_audio_frame_t * frame)
  {
  gavl_audio_sink_put_frame(v->asink_ext, frame);
  }

void bg_visualizer_set_plugin_by_index(bg_visualizer_t * v, int plugin)
  {
  gavl_msg_t * msg = bg_msg_sink_get(v->msink);

  gavl_msg_set_id_ns(msg, BG_CMD_VISUALIZER_SET_PLUGIN, BG_MSG_NS_VISUALIZER);
  gavl_msg_set_arg_int(msg, 0, plugin);
  bg_msg_sink_put(v->msink);
  }
  
void bg_visualizer_pause(bg_visualizer_t * v)
  {
  gavl_msg_t * msg = bg_msg_sink_get(v->msink);
  gavl_msg_set_id_ns(msg, BG_CMD_VISUALIZER_PAUSE, BG_MSG_NS_VISUALIZER);
  bg_msg_sink_put(v->msink);
  }
