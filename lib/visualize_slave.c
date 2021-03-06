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
#include <dlfcn.h>
#include <unistd.h>

#include <gavl/gavl.h>

#include <config.h>
#include <gavl/gavlsocket.h>

#include <gmerlin/translation.h>

#include <gmerlin/pluginregistry.h>
#include <gmerlin/visualize.h>


#include <visualize_priv.h>
#include <gmerlin/utils.h>
#include <gmerlin/state.h>



#include <gmerlin/log.h>

#define LOG_DOMAIN "visualizer_slave"

#ifdef HAVE_LV
#include <bglv.h>
#endif



static char * socket_name = NULL;
static int conn_fd = -1;
gavf_io_t * io = NULL;

bg_msg_sink_t * msg_sink = NULL;

/* Messages from the application to the visualizer */

typedef struct
  {
  gavl_audio_converter_t * cnv;
  gavl_audio_frame_t     * in_frame_1;
  gavl_audio_frame_t     * in_frame_2;
  pthread_mutex_t in_mutex;
  
  int do_convert;
  
  gavl_audio_frame_t * out_frame;
  
  gavl_audio_format_t in_format;
  gavl_audio_format_t out_format;
  
  int last_samples_read;
  int frame_done;
  
  gavl_volume_control_t * gain_control;
  pthread_mutex_t gain_mutex;

  } audio_buffer_t;

static audio_buffer_t * audio_buffer_create()
  {
  audio_buffer_t * ret;
  ret = calloc(1, sizeof(*ret));
  ret->cnv = gavl_audio_converter_create();
  pthread_mutex_init(&ret->in_mutex, NULL);
  pthread_mutex_init(&ret->gain_mutex, NULL);
  
  ret->gain_control = gavl_volume_control_create();
  
  return ret;
  }

static void audio_buffer_cleanup(audio_buffer_t * b)
  {
  if(b->in_frame_1)
    {
    gavl_audio_frame_destroy(b->in_frame_1);
    b->in_frame_1 = NULL;
    }
  if(b->in_frame_2)
    {
    gavl_audio_frame_destroy(b->in_frame_2);
    b->in_frame_2 = NULL;
    }
  if(b->out_frame)
    {
    gavl_audio_frame_destroy(b->out_frame);
    b->out_frame = NULL;
    }
  b->last_samples_read = 0;
  b->frame_done = 0;
  }


static void audio_buffer_destroy(audio_buffer_t * b)
  {
  audio_buffer_cleanup(b);
  gavl_audio_converter_destroy(b->cnv);
  gavl_volume_control_destroy(b->gain_control);
  pthread_mutex_destroy(&b->in_mutex);
  pthread_mutex_destroy(&b->gain_mutex);
  free(b);
  }

static void audio_buffer_init(audio_buffer_t * b,
                              const gavl_audio_format_t * in_format,
                              const gavl_audio_format_t * out_format)
  {
  gavl_audio_format_t frame_format;
  /* Cleanup */
  audio_buffer_cleanup(b);
  gavl_audio_format_copy(&b->in_format, in_format);
  gavl_audio_format_copy(&b->out_format, out_format);

  /* For visualizations, we ignore the samplerate completely.
     Perfect synchronization is mathematically impossible anyway. */
  
  b->out_format.samplerate = b->in_format.samplerate;
  
  b->do_convert = gavl_audio_converter_init(b->cnv,
                                            &b->in_format,
                                            &b->out_format);

  b->in_frame_1 = gavl_audio_frame_create(&b->in_format);

  gavl_audio_format_copy(&frame_format, out_format);
  frame_format.samples_per_frame = b->in_format.samples_per_frame;
  
  b->in_frame_2 = gavl_audio_frame_create(&frame_format);
  
  b->out_frame = gavl_audio_frame_create(&b->out_format);
  
  gavl_volume_control_set_format(b->gain_control, &frame_format);
  }

static void audio_buffer_put(audio_buffer_t * b,
                             const gavl_audio_frame_t * f)
  {
  pthread_mutex_lock(&b->in_mutex);
  
  b->in_frame_1->valid_samples =
    gavl_audio_frame_copy(&b->in_format,
                          b->in_frame_1,
                          f,
                          0, /* dst_pos */
                          0, /* src_pos */
                          b->in_format.samples_per_frame, /* dst_size */
                          f->valid_samples /* src_size */ ); 
  pthread_mutex_unlock(&b->in_mutex);
  }

static void audio_buffer_set_gain(audio_buffer_t * b, float gain)
  {
  pthread_mutex_lock(&b->gain_mutex);
  gavl_volume_control_set_volume(b->gain_control, gain);
  pthread_mutex_unlock(&b->gain_mutex);
  }

static gavl_audio_frame_t * audio_buffer_get(audio_buffer_t * b)
  {
  int samples_copied;
  /* Check if there is new audio */
  pthread_mutex_lock(&b->in_mutex);

  if(b->in_frame_1->valid_samples)
    {
    if(b->do_convert)
      {
      gavl_audio_convert(b->cnv, b->in_frame_1, b->in_frame_2);
      samples_copied = b->in_frame_1->valid_samples;
      }
    else
      samples_copied =
        gavl_audio_frame_copy(&b->in_format,
                              b->in_frame_2,
                              b->in_frame_1,
                              0, /* dst_pos */
                              0, /* src_pos */
                              b->in_format.samples_per_frame, /* dst_size */
                              b->in_frame_1->valid_samples    /* src_size */ ); 
    b->in_frame_2->valid_samples = samples_copied;
    b->last_samples_read         = samples_copied;
    b->in_frame_1->valid_samples = 0;
    
    pthread_mutex_lock(&b->gain_mutex);
    gavl_volume_control_apply(b->gain_control, b->in_frame_2);
    pthread_mutex_unlock(&b->gain_mutex);
    }
  pthread_mutex_unlock(&b->in_mutex);
  
  /* If the frame was output the last time, set valid_samples to 0 */
  if(b->frame_done)
    {
    b->out_frame->valid_samples = 0;
    b->frame_done = 0;
    }
  
  /* Copy to output frame and check if there are enough samples */
  
  samples_copied =
    gavl_audio_frame_copy(&b->out_format,
                          b->out_frame,
                          b->in_frame_2,
                          b->out_frame->valid_samples, /* dst_pos */
                          b->last_samples_read - b->in_frame_2->valid_samples, /* src_pos */
                          b->out_format.samples_per_frame - b->out_frame->valid_samples, /* dst_size */
                          b->in_frame_2->valid_samples /* src_size */ ); 
  
  b->out_frame->valid_samples += samples_copied;
  b->in_frame_2->valid_samples -= samples_copied;
    
  if(b->out_frame->valid_samples == b->out_format.samples_per_frame)
    {
    b->frame_done = 1;
    return b->out_frame;
    }
  return NULL;
  }

typedef struct
  {
  bg_plugin_handle_t * vis_handle;
  bg_plugin_handle_t * ov_handle;
  bg_plugin_api_t vis_api;
  
  audio_buffer_t * audio_buffer;
  
  gavl_video_converter_t * video_cnv;

  int do_convert_video;
    
  bg_ov_plugin_t * ov_plugin;
  
  bg_visualization_plugin_t * vis_plugin;

  gavl_video_format_t video_format_in;
  gavl_video_format_t video_format_in_real;
  gavl_video_format_t video_format_out;
  
  pthread_t video_thread;
  
  pthread_mutex_t running_mutex;
  pthread_mutex_t stop_mutex;
  pthread_mutex_t vis_mutex;
  pthread_mutex_t ov_mutex;
  
  int do_stop;

  gavl_video_frame_t * video_frame_in;
  //  gavl_video_frame_t * video_frame_out;

  gavl_timer_t * timer;
  
  gavl_time_t last_frame_time;

  gavl_audio_source_t * asrc;
  
  gavl_audio_format_t audio_format_in;
  gavl_audio_format_t audio_format_out;
  
  int do_ov;
  
  char * window_id;
  
  gavl_audio_frame_t * read_frame;
  
  pthread_mutex_t fps_mutex;
  float fps;

  gavl_video_sink_t * sink;
  
  } bg_visualizer_slave_t;

static int init_plugin(bg_visualizer_slave_t * v);

static bg_plugin_handle_t *
load_plugin_gmerlin(const char * filename)
  {
  int (*get_plugin_api_version)();
  bg_plugin_handle_t * ret;
  ret = calloc(1, sizeof(*ret));
  
  ret->dll_handle = dlopen(filename, RTLD_NOW | RTLD_GLOBAL);
  if(!(ret->dll_handle))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN,
           "Cannot dlopen plugin module %s: %s", filename,
           dlerror());
    goto fail;
    }
  
  get_plugin_api_version = dlsym(ret->dll_handle, "get_plugin_api_version");
  if(!get_plugin_api_version)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN,
           "cannot get API version: %s", dlerror());
    goto fail;
    }
  if(get_plugin_api_version() != BG_PLUGIN_API_VERSION)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN,
           "Wrong API version: Got %d expected %d",
           get_plugin_api_version(), BG_PLUGIN_API_VERSION);
    goto fail;
    }
  ret->plugin = dlsym(ret->dll_handle, "the_plugin");
  if(!ret)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN,
           "No symbol the_plugin: %s",
           dlerror());
    goto fail;
    }
  ret->priv = ret->plugin->create();
  return ret;
  fail:
  return NULL;
  }

#ifdef HAVE_LV
static bg_plugin_handle_t *
load_plugin_lv(const char * name, int plugin_flags, const char * window_id)
  {
  bg_plugin_handle_t * ret;
  ret = calloc(1, sizeof(*ret));
  if(!bg_lv_load(ret, name, plugin_flags, window_id))
    {
    free(ret);
    return NULL;
    }
  return ret;
  }
#endif

// #define BG_VIS_MSG_CB_MOTION // x, y, mask
// #define // x, y, button, mask
// #define BG_VIS_MSG_CB_BUTTON_REL // x, y, button, mask

#if 0
static int ov_button_callback(void * data, int x, int y,
                              int button, int mask)
  {
  bg_msg_t * msg;
  bg_visualizer_slave_t * s = data;
  
  msg = bg_msg_queue_lock_write(s->cb_queue);
  bg_msg_set_id_ns(msg, BG_VIS_MSG_CB_BUTTON);
  bg_msg_set_arg_int(msg, 0, x);
  bg_msg_set_arg_int(msg, 1, y);
  bg_msg_set_arg_int(msg, 2, button);
  bg_msg_set_arg_int(msg, 3, mask);
  bg_msg_queue_unlock_write(s->cb_queue);
  return 1;
  }

static int ov_button_release_callback(void * data, int x, int y,
                                      int button, int mask)
  {
  bg_msg_t * msg;
  bg_visualizer_slave_t * s = data;

  msg = bg_msg_queue_lock_write(s->cb_queue);
  bg_msg_set_id_ns(msg, BG_VIS_MSG_CB_BUTTON_REL);
  bg_msg_set_arg_int(msg, 0, x);
  bg_msg_set_arg_int(msg, 1, y);
  bg_msg_set_arg_int(msg, 2, button);
  bg_msg_set_arg_int(msg, 3, mask);
  bg_msg_queue_unlock_write(s->cb_queue);
  return 1;
  }

static int ov_motion_callback(void * data, int x, int y,
                              int mask)
  {
  bg_msg_t * msg;
  bg_visualizer_slave_t * s = data;

  msg = bg_msg_queue_lock_write(s->cb_queue);
  bg_msg_set_id_ns(msg, BG_VIS_MSG_CB_MOTION);
  bg_msg_set_arg_int(msg, 0, x);
  bg_msg_set_arg_int(msg, 1, y);
  bg_msg_set_arg_int(msg, 2, mask);
  bg_msg_queue_unlock_write(s->cb_queue);
  return 1;
  }
#endif

static int write_callback(void * data, const uint8_t * ptr, int len)
  {
  return write(conn_fd, ptr, len);
  }

static int read_callback(void * data, uint8_t * ptr, int len)
  {
  return read(conn_fd, ptr, len);
  }


static bg_visualizer_slave_t *
bg_visualizer_slave_create(int argc, char ** argv)
  {
  int i;
  bg_visualizer_slave_t * ret;
  char * window_id = NULL;
  char * plugin_module = NULL;
  char * ov_module = NULL;
  
  /* Handle arguments and load plugins */
  i = 1;
  while(i < argc)
    {
    if(!strcmp(argv[i], "-w"))
      {
      window_id = argv[i+1];
      i += 2;
      }
    else if(!strcmp(argv[i], "-p"))
      {
      plugin_module = argv[i+1];
      i += 2;
      }
    else if(!strcmp(argv[i], "-o"))
      {
      ov_module = argv[i+1];
      i += 2;
      }
    else if(!strcmp(argv[i], "-s"))
      {
      socket_name = argv[i+1];
      i+=2;
      }
    }
  
  /* Sanity checks */
  if(!window_id)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "No window ID given");
    return NULL;
    }
  if(!plugin_module)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "No plugin given");
    return NULL;
    }

  if(!socket_name)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "No control socket given");
    return NULL;
    }

  if((conn_fd = bg_socket_connect_unix(socket_name)) < 0)
    return NULL;
  io = gavf_io_create(read_callback, write_callback, NULL, NULL, NULL, NULL);
  
  ret = calloc(1, sizeof(*ret));
  ret->audio_buffer = audio_buffer_create();
  ret->window_id = window_id;


  
  /* Create callbacks */
  //  ret->cb.button_release_callback = ov_button_release_callback;
  //  ret->cb.button_callback = ov_button_callback;
  //  ret->cb.motion_callback = ov_motion_callback;
  //  ret->cb.data = ret;
  
  pthread_mutex_init(&ret->stop_mutex, NULL);
  pthread_mutex_init(&ret->running_mutex, NULL);
  pthread_mutex_init(&ret->vis_mutex, NULL);
  pthread_mutex_init(&ret->ov_mutex, NULL);
  pthread_mutex_init(&ret->fps_mutex, NULL);
  
  ret->timer = gavl_timer_create();

  /* Load ov module */
  if(ov_module)
    {
    gavl_value_t val;
    
    ret->do_ov = 1;
    ret->video_cnv = gavl_video_converter_create();
    
    ret->ov_handle = load_plugin_gmerlin(ov_module);
    if(!ret->ov_handle)
      return NULL;
    
    ret->ov_plugin = (bg_ov_plugin_t*)ret->ov_handle->plugin;

    gavl_value_init(&val);
    gavl_value_set_string(&val, ret->window_id);
    bg_plugin_handle_set_state(ret->ov_handle, BG_STATE_CTX_OV, BG_STATE_OV_WINDOW_ID, &val);
    gavl_value_free(&val);
    
    }
  ret->vis_api = BG_PLUGIN_API_GMERLIN;
  
#ifdef HAVE_LV
  if(!strncmp(plugin_module, "vis_lv_", 7))
    {
    if(ret->ov_handle)
      ret->vis_handle =
        load_plugin_lv(plugin_module,
                       BG_PLUGIN_VISUALIZE_FRAME, ret->window_id);
    else
      ret->vis_handle =
        load_plugin_lv(plugin_module,
                       BG_PLUGIN_VISUALIZE_GL, ret->window_id);
    ret->vis_api = BG_PLUGIN_API_LV;
    }
  else
#endif
    ret->vis_handle =
      load_plugin_gmerlin(plugin_module);

  if(!ret->vis_handle)
    return NULL;
  
  ret->vis_plugin = (bg_visualization_plugin_t*)(ret->vis_handle->plugin);
  
  
  return ret;
  }

static void uload_plugin(bg_plugin_handle_t * h, bg_plugin_api_t api)
  {
#ifdef HAVE_LV
  if(api == BG_PLUGIN_API_LV)
    {
    bg_lv_unload(h);
    free(h);
    return;
    }
#endif
  h->plugin->destroy(h->priv);
  dlclose(h->dll_handle);
  free(h);
  }

static void bg_visualizer_slave_destroy(bg_visualizer_slave_t * v)
  {
  pthread_mutex_destroy(&v->stop_mutex);

  if(v->video_cnv)
    gavl_video_converter_destroy(v->video_cnv);

  audio_buffer_destroy(v->audio_buffer);
  gavl_timer_destroy(v->timer);

  pthread_mutex_destroy(&v->running_mutex);
  pthread_mutex_destroy(&v->fps_mutex);
  pthread_mutex_destroy(&v->stop_mutex);
  pthread_mutex_destroy(&v->ov_mutex);
  pthread_mutex_destroy(&v->vis_mutex);

  /* Close vis plugin */
  v->vis_plugin->close(v->vis_handle->priv);
  uload_plugin(v->vis_handle, v->vis_api);
  
  /* Close OV Plugin */
  if(v->do_ov)
    {
    if(v->video_frame_in)
      {
      gavl_video_frame_destroy(v->video_frame_in);
      v->video_frame_in = NULL;
      }
    v->ov_plugin->close(v->ov_handle->priv);
    uload_plugin(v->ov_handle, BG_PLUGIN_API_GMERLIN);
    }
  
  free(v);
  }

static void * video_thread_func(void * data)
  {
  int do_stop;
  bg_visualizer_slave_t * v;
  gavl_audio_frame_t * audio_frame;
  gavl_time_t diff_time, current_time;
  float last_fps = -1.0;
  int64_t frame_time;
  gavl_video_frame_t * out_frame = NULL;
  
  v = (bg_visualizer_slave_t*)data;
  
  pthread_mutex_lock(&v->running_mutex);
  while(1)
    {
    /* Check if we should stop */
    pthread_mutex_lock(&v->stop_mutex);
    do_stop = v->do_stop;
    pthread_mutex_unlock(&v->stop_mutex);
    if(do_stop)
      break;
    
    /* Draw frame */
    pthread_mutex_lock(&v->vis_mutex);
    
    /* Check if we should update audio */

    audio_frame = audio_buffer_get(v->audio_buffer);
    if(audio_frame)
      v->vis_plugin->update(v->vis_handle->priv, audio_frame);
    
    /* Draw frame */
    
    if(!(v->do_ov))
      v->vis_plugin->draw_frame(v->vis_handle->priv, NULL);
    else if(v->do_convert_video)
      {
      out_frame = gavl_video_sink_get_frame(v->sink);
      v->vis_plugin->draw_frame(v->vis_handle->priv, v->video_frame_in);
      gavl_video_convert(v->video_cnv, v->video_frame_in, out_frame);
      }
    else
      {
      out_frame = gavl_video_sink_get_frame(v->sink);
      v->vis_plugin->draw_frame(v->vis_handle->priv, out_frame);
      }
    pthread_mutex_unlock(&v->vis_mutex);
    
    /* Wait until we can show the frame */
    current_time = gavl_timer_get(v->timer);
    
    diff_time = v->last_frame_time +
      v->video_format_in.frame_duration - current_time;
    
    if(diff_time > GAVL_TIME_SCALE / 1000)
      gavl_time_delay(&diff_time);
    
    /* Show frame */
    
    if(v->do_ov)
      {
      pthread_mutex_lock(&v->ov_mutex);

      gavl_video_sink_put_frame(v->sink, out_frame);
      frame_time = gavl_timer_get(v->timer);
      
      v->ov_plugin->handle_events(v->ov_handle->priv);
      pthread_mutex_unlock(&v->ov_mutex);
      }
    else
      {
      pthread_mutex_lock(&v->vis_mutex);
      v->vis_plugin->show_frame(v->vis_handle->priv);
      frame_time = gavl_timer_get(v->timer);
      pthread_mutex_unlock(&v->vis_mutex);
      }
    if(v->last_frame_time < frame_time)
      {
      if(last_fps < 0.0)
        {
        pthread_mutex_lock(&v->fps_mutex);
        v->fps = (double)(GAVL_TIME_SCALE) /
          (double)(frame_time - v->last_frame_time);
        last_fps = v->fps;
        pthread_mutex_unlock(&v->fps_mutex);
        }
      else
        {
        pthread_mutex_lock(&v->fps_mutex);
        v->fps = 0.95 * last_fps +
          0.05 * (double)(GAVL_TIME_SCALE) /
          (double)(frame_time - v->last_frame_time);
        last_fps = v->fps;
        pthread_mutex_unlock(&v->fps_mutex);
        }
      }
    v->last_frame_time = frame_time;
    }
  pthread_mutex_unlock(&v->running_mutex);
  return NULL;
  }

static int bg_visualizer_slave_stop(bg_visualizer_slave_t * v)
  {
  if(!pthread_mutex_trylock(&v->running_mutex))
    {
    pthread_mutex_unlock(&v->running_mutex);
    return 0;
    }
  
  /* Join threads */
  
  pthread_mutex_lock(&v->stop_mutex);
  v->do_stop = 1;
  pthread_mutex_unlock(&v->stop_mutex);
  
  pthread_join(v->video_thread, NULL);
  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Joined thread");
  gavl_timer_stop(v->timer);

  if(v->asrc)
    {
    gavl_audio_source_destroy(v->asrc);
    v->asrc = NULL;
    }
  return 1;
  }

static int bg_visualizer_slave_start(bg_visualizer_slave_t * v)
  {
  if(pthread_mutex_trylock(&v->running_mutex))
    return 0;
  
  pthread_mutex_unlock(&v->running_mutex);
  
  v->fps = -1.0;
  v->do_stop = 0;
  v->last_frame_time = 0; 
  gavl_timer_set(v->timer, 0);
  gavl_timer_start(v->timer);
  
  pthread_create(&v->video_thread, NULL, video_thread_func, v);
  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Started thread");
  return 1;
  }

static void
bg_visualizer_slave_set_audio_format(bg_visualizer_slave_t * v,
                                     const gavl_audio_format_t * format,
                                     gavl_dictionary_t * m)
  {
  int was_running;
  was_running = bg_visualizer_slave_stop(v);
  pthread_mutex_lock(&v->audio_buffer->in_mutex);
  
  gavl_audio_format_copy(&v->audio_format_in, format);

  if(was_running)
    audio_buffer_init(v->audio_buffer, &v->audio_format_in, &v->audio_format_out);
  pthread_mutex_unlock(&v->audio_buffer->in_mutex);
  if(was_running)
    bg_visualizer_slave_start(v);
  }

static void cleanup_plugin(bg_visualizer_slave_t * v)
  {
  }

static int init_plugin(bg_visualizer_slave_t * v)
  {
  gavl_audio_format_copy(&v->audio_format_out, &v->audio_format_in);

  /* Set members, which might be missing */
  v->video_format_in.pixel_width  = 1;
  v->video_format_in.pixel_height = 1;
  
  /* Set video format */
  gavl_video_format_copy(&v->video_format_in_real, &v->video_format_in);
  
  /* Open visualizer plugin */
  
  if(v->do_ov)
    {
    v->vis_plugin->open_ov(v->vis_handle->priv, &v->audio_format_out,
                           &v->video_format_in_real);
    
    gavl_video_format_copy(&v->video_format_out, &v->video_format_in_real);
    
    /* Open OV Plugin */
    if(!v->ov_plugin->open(v->ov_handle->priv, &v->video_format_out, 0))
      return 0;

    v->sink = v->ov_plugin->get_sink(v->ov_handle->priv);
    
    /* Initialize video converter */
    
    v->do_convert_video =
      gavl_video_converter_init(v->video_cnv, &v->video_format_in_real,
                                &v->video_format_out);
    
    if(v->do_convert_video)
      v->video_frame_in = gavl_video_frame_create(&v->video_format_in_real);
    }
  else
    {
    if(!v->vis_plugin->open_win(v->vis_handle->priv, &v->audio_format_out,
                                v->window_id))
      return 0;
    gavl_video_format_copy(&v->video_format_out, &v->video_format_in);
    }
  
  audio_buffer_init(v->audio_buffer, &v->audio_format_in, &v->audio_format_out);
  return 1;
  }

static int write_message(gavl_msg_t * msg)
  {
  int result;
  result = gavl_msg_write(msg, io);
  return result;
  }

static int handle_msg(void * data, gavl_msg_t * msg)
  {
  write_message(msg);
  return 1;
  }

int main(int argc, char ** argv)
  {
  gavl_audio_format_t audio_format;
  float arg_f;
  
  int keep_going;
  bg_visualizer_slave_t * s;
  gavl_msg_t * msg;

  char * parameter_name = NULL;
  gavl_value_t parameter_value;
  int counter = 0.0;
  gavl_dsp_context_t * ctx = NULL;
  int result;
  gavl_dictionary_t m_global;
  gavl_dictionary_t m_stream;
  
  ctx = gavl_dsp_context_create();
  
  memset(&parameter_value, 0, sizeof(parameter_value));

  gavl_dictionary_init(&m_global);
  
  if(isatty(fileno(stdin)))
    {
    printf("This program is not meant to be started from the commandline.\nThe official frontend API for visualizatons is in " PREFIX "/include/gmerlin/visualize.h\n");
    return -1;
    }
  
  msg_sink = bg_msg_sink_create(handle_msg, NULL, 0);
  gavl_log_add_dest(msg_sink);
    
  s = bg_visualizer_slave_create(argc, argv);

  msg = gavl_msg_create();

  keep_going = 1;

  while(keep_going)
    {
//    fprintf(stderr, "Read message slave...\n"); 
    result = gavl_msg_read(msg, io);
//    fprintf(stderr, "Read message slave done %d\n", result);
    if(!result)
      break;

#if 0    
    switch(gavl_msg_get_id(msg))
      {
      case BG_VIS_MSG_AUDIO_FORMAT:
        {
        gavl_dictionary_free(&m_stream);
        gavl_dictionary_init(&m_stream);
        
        memset(&audio_format, 0, sizeof(audio_format));
        gavl_msg_get_arg_audio_format(msg, 0, &audio_format);
        gavl_msg_get_arg_dictionary(msg, 1, &m_stream);
        
        bg_visualizer_slave_set_audio_format(s, &audio_format, &m_stream);
        s->asrc = gavl_audio_source_create_io(io, &audio_format,
                                              &m_stream);
                
        }
        break;
      case BG_VIS_MSG_AUDIO_DATA:
        {
        gavl_audio_frame_t * f = NULL;
        gavl_source_status_t st;

        if(!s->asrc || (st = gavl_audio_source_read_frame(s->asrc, &f)) != GAVL_SOURCE_OK)
          break;

        if(!pthread_mutex_trylock(&s->running_mutex))
          {
          pthread_mutex_unlock(&s->running_mutex);
          break;
          }
        else
          audio_buffer_put(s->audio_buffer, f);
        }
        break;
      case BG_VIS_MSG_VIS_PARAM:
        bg_msg_get_parameter(msg,
                             &parameter_name,
                             &parameter_value);
       
        pthread_mutex_lock(&s->vis_mutex);
        s->vis_plugin->common.set_parameter(s->vis_handle->priv,
                                            parameter_name,
                                            &parameter_value);
        pthread_mutex_unlock(&s->vis_mutex);
        if(parameter_name)
          {
          free(parameter_name);
          parameter_name = NULL;
          bg_parameter_value_free(&parameter_value);
          }
        
        break;
      case BG_VIS_MSG_OV_PARAM:
        bg_msg_get_parameter(msg,
                             &parameter_name,
                             &parameter_value);
        pthread_mutex_lock(&s->ov_mutex);


        s->ov_plugin->common.set_parameter(s->ov_handle->priv,
                                           parameter_name,
                                           &parameter_value);
        pthread_mutex_unlock(&s->ov_mutex);
        if(parameter_name)
          {
          free(parameter_name);
          parameter_name = NULL;
          bg_parameter_value_free(&parameter_value);
          }
        break;
      case BG_VIS_MSG_GAIN:
        arg_f = gavl_msg_get_arg_float(msg, 0);
        audio_buffer_set_gain(s->audio_buffer, arg_f);
        break;
      case BG_VIS_MSG_FPS:
        s->video_format_in.timescale = GAVL_TIME_SCALE;
        s->video_format_in.frame_duration =
          (int)(GAVL_TIME_SCALE / gavl_msg_get_arg_float(msg, 0));
        break;
      case BG_VIS_MSG_IMAGE_SIZE:
        s->video_format_in.image_width =
          gavl_msg_get_arg_int(msg, 0);
        s->video_format_in.image_height =
          gavl_msg_get_arg_int(msg, 1);

        s->video_format_in.frame_width =
          s->video_format_in.image_width;
        s->video_format_in.frame_height =
          s->video_format_in.image_height;
        break;
      case BG_VIS_MSG_START:
        if(!init_plugin(s))
          gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Starting visualization failed");
        else
          bg_visualizer_slave_start(s);
        break;
      case BG_VIS_MSG_QUIT:
        keep_going = 0;
        break;
      case BG_VIS_MSG_TELL:
        bg_msg_sink_iteration(msg_sink);
        
        if(counter > 10)
          {
          counter = 0;

          gavl_msg_set_id(msg, BG_VIS_SLAVE_MSG_FPS);
          pthread_mutex_lock(&s->fps_mutex);
          gavl_msg_set_arg_float(msg, 0, s->fps);
          pthread_mutex_unlock(&s->fps_mutex);
          write_message(msg);
          gavl_msg_free(msg);
          
          }
        counter++;
        gavl_msg_set_id(msg, BG_VIS_SLAVE_MSG_END);
        write_message(msg);
        gavl_msg_free(msg);
        break;
      }
#endif
    }
  bg_visualizer_slave_stop(s);
  cleanup_plugin(s);
  
  bg_visualizer_slave_destroy(s);
  gavl_msg_free(msg);

  gavl_dsp_context_destroy(ctx);

  gavl_log_remove_dest(msg_sink);
  
  
  return 0;
  }
