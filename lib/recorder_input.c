#include <string.h>

#include <config.h>


#include <gmerlin/translation.h>

#include <gmerlin/pluginregistry.h>
#include <gmerlin/utils.h>
#include <pluginreg_priv.h>

#include <gavl/trackinfo.h>

#include <gavl/log.h>
#define LOG_DOMAIN "recorder"

static bg_parameter_info_t params[] =
  {
    {
      .type        = BG_PARAMETER_SECTION,
      .name        = "audio",
      .long_name   = TRS("Audio"),
    },
    {
      .type        = BG_PARAMETER_CHECKBUTTON,
      .name        = "ea",
      .long_name   = TRS("Enable audio"),
      .val_default = GAVL_VALUE_INIT_INT(1),
      
    },
    {
      .type        = BG_PARAMETER_INT,
      .name        = "samplerate",
      .long_name   = TRS("Samplerate"),
      .help_string = TRS("Audio samplerate"),
      .val_default = GAVL_VALUE_INIT_INT(48000),
      .val_min     = GAVL_VALUE_INIT_INT(16000),
      .val_max     = GAVL_VALUE_INIT_INT(192000),
    },
    {
      .type        = BG_PARAMETER_INT,
      .name        = "channels",
      .long_name   = TRS("Channels"),
      .help_string = TRS("Number of channels"),
      .val_default = GAVL_VALUE_INIT_INT(2),
      .val_min     = GAVL_VALUE_INIT_INT(1),
      .val_max     = GAVL_VALUE_INIT_INT(2),
    },
    {
      .type        = BG_PARAMETER_SECTION,
      .name        = "video",
      .long_name   = TRS("Video"),
      
    },
    {
      .type        = BG_PARAMETER_CHECKBUTTON,
      .name        = "ev",
      .long_name   = TRS("Enable video"),
      .val_default = GAVL_VALUE_INIT_INT(1),
    },
    {
      .type        = BG_PARAMETER_INT,
      .name        = "width",
      .long_name   = TRS("Width"),
      .help_string = TRS("Image width"),
      .val_default = GAVL_VALUE_INIT_INT(640),
      .val_min     = GAVL_VALUE_INIT_INT(320),
      .val_max     = GAVL_VALUE_INIT_INT(4096),
    },
    {
      .type        = BG_PARAMETER_INT,
      .name        = "height",
      .long_name   = TRS("Height"),
      .help_string = TRS("Image height"),
      .val_default = GAVL_VALUE_INIT_INT(480),
      .val_min     = GAVL_VALUE_INIT_INT(240),
      .val_max     = GAVL_VALUE_INIT_INT(3072),
    },
    {
      .type        = BG_PARAMETER_INT,
      .name        = "timescale",
      .long_name   = TRS("Timescale"),
      .help_string = TRS("Tics per second"),
      .val_default = GAVL_VALUE_INIT_INT(30),
      .val_min     = GAVL_VALUE_INIT_INT(1),
      .val_max     = GAVL_VALUE_INIT_INT(1000),
    },
    {
      .type        = BG_PARAMETER_INT,
      .name        = "frame_duration",
      .long_name   = TRS("Frame duration"),
      .help_string = TRS("Duration of one video frame in timescale tics"),
      .val_default = GAVL_VALUE_INIT_INT(1),
      .val_min     = GAVL_VALUE_INIT_INT(1),
      .val_max     = GAVL_VALUE_INIT_INT(3000),
    },
    { /* */ }
  };

typedef struct
  {
  bg_plugin_handle_t * handle;
  bg_recorder_plugin_t * plugin;

  int enable;

  gavl_audio_format_t * fmt;
  gavl_dictionary_t * info;

  gavl_audio_format_t cfg_fmt;
  } audio_stream_t;

typedef struct
  {
  bg_plugin_handle_t * handle;
  
  bg_recorder_plugin_t * plugin;
  int enable;

  gavl_video_format_t * fmt;
  gavl_dictionary_t * info;

  gavl_video_format_t cfg_fmt;

  } video_stream_t;

typedef struct
  {
  gavl_dictionary_t media_info;

  gavl_dictionary_t * track_info;
  
  bg_media_source_t src;
  
  audio_stream_t as;
  video_stream_t vs;

  bg_controllable_t ctrl;

  } recorder_t;



static void destroy_input(void * priv)
  {
  recorder_t * r = priv;
  
  free(r);
  }

static const bg_parameter_info_t * get_parameters_recorder(void * priv)
  {
  //  recorder_t * r = priv;
  
  return params;
  }

static void set_parameter_recorder(void * priv, const char * name, const gavl_value_t * val)
  {
  recorder_t * r = priv;

  if(!name)
    return;

  else if(!strcmp(name, "ea"))
    {
    r->as.enable = val->v.i;
    }
  else if(!strcmp(name, "ev"))
    {
    r->vs.enable = val->v.i;
    }
  else if(!strcmp(name, "samplerate"))
    {
    r->as.cfg_fmt.samplerate = val->v.i;
    }
  else if(!strcmp(name, "channels"))
    {
    r->as.cfg_fmt.num_channels = val->v.i;
    }
  else if(!strcmp(name, "width"))
    {
    r->vs.cfg_fmt.image_width = val->v.i;
    }
  else if(!strcmp(name, "height"))
    {
    r->vs.cfg_fmt.image_height = val->v.i;
    }
  else if(!strcmp(name, "frame_duration"))
    {
    r->vs.cfg_fmt.frame_duration = val->v.i;
    }
  else if(!strcmp(name, "timescale"))
    {
    r->vs.cfg_fmt.timescale = val->v.i;
    }
  }
/*
  recorder://
    
 */

static int open_recorder(void * priv, const char * filename)
  {
  const gavl_value_t * cfg_val;
  const bg_plugin_info_t * plugin_info;
  gavl_dictionary_t * m;
  gavl_dictionary_t vars;
  const char * var;
  recorder_t * r = priv;
  bg_media_source_init(&r->src);

  /* We support the URL variables a=[0|1] and v=[0|1] */
  gavl_dictionary_init(&vars);
  bg_url_get_vars_c(filename, &vars);

  if((var = gavl_dictionary_get_string(&vars, "a")))
    r->as.enable = atoi(var);
  if((var = gavl_dictionary_get_string(&vars, "v")))
    r->vs.enable = atoi(var);
  
  if(!r->as.enable && !r->vs.enable)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Neither audio nor video enabled");
    return 0;
    }

  /* Load audio recorder */

  if(r->as.enable)
    {

    if((cfg_val = bg_plugin_config_get(BG_PLUGIN_RECORDER_AUDIO)))
      {
      r->as.handle = bg_plugin_load_with_options(bg_plugin_reg,
                                                 gavl_value_get_dictionary(cfg_val));
      }
    else if((plugin_info = bg_plugin_find_by_index(bg_plugin_reg,
                                                   0,
                                                   BG_PLUGIN_RECORDER_AUDIO, 0)))
      {
      // Use registry default
      r->as.handle = bg_plugin_load(bg_plugin_reg, plugin_info);
      }
  
    if(r->as.handle)
      {
      r->as.plugin = (bg_recorder_plugin_t*)r->as.handle->plugin;
      r->as.info = gavl_track_append_audio_stream(r->track_info);
      r->as.fmt = gavl_stream_get_audio_format_nc(r->as.info);
      }
    }
  
  
  /* Load video recorder */

  if(r->vs.enable)
    {
    if((cfg_val = bg_plugin_config_get(BG_PLUGIN_RECORDER_VIDEO)))
      {
      r->vs.handle = bg_plugin_load_with_options(bg_plugin_reg,
                                                 gavl_value_get_dictionary(cfg_val));
    
      }
    else if((plugin_info = bg_plugin_find_by_index(bg_plugin_reg, 0,
                                                   BG_PLUGIN_RECORDER_VIDEO, 0)))
      {
      r->vs.handle = bg_plugin_load(bg_plugin_reg, plugin_info);
      }

    if(r->vs.handle)
      {
      r->vs.plugin = (bg_recorder_plugin_t*)r->vs.handle->plugin;
      r->vs.info = gavl_track_append_video_stream(r->track_info);
      r->vs.fmt = gavl_stream_get_video_format_nc(r->vs.info);
      }
    }
  
  /* Set general metadata */
  m = gavl_track_get_metadata_nc(r->track_info);

  if(r->vs.handle)
    {
    if(r->as.handle)
      gavl_dictionary_set_string(m, GAVL_META_LABEL, "Audio/Video Recorder");
    else
      gavl_dictionary_set_string(m, GAVL_META_LABEL, "Video Recorder");

    gavl_dictionary_set_string(m, GAVL_META_MEDIA_CLASS, GAVL_META_MEDIA_CLASS_VIDEO_RECORDER);
    }
  else
    {
    gavl_dictionary_set_string(m, GAVL_META_LABEL, "Audio Recorder");
    gavl_dictionary_set_string(m, GAVL_META_MEDIA_CLASS, GAVL_META_MEDIA_CLASS_AUDIO_RECORDER);
    }

  gavl_metadata_add_src(m, GAVL_META_SRC, NULL, filename);
  /* Create media source */
    
  bg_media_source_set_from_track(&r->src, r->track_info);
  
#if 0
  
  if(!open_audio(r) ||
     !open_video(r))
    return 0;
#endif
  
  
  return 1;
  }

static bg_controllable_t *
get_controllable_recorder(void * priv)
  {
  recorder_t * r = priv;
  
  return &r->ctrl;
  }

static gavl_dictionary_t *
get_media_info_recorder(void * priv)
  {
  recorder_t * r = priv;

  return &r->media_info;
  }

static bg_media_source_t *
get_src_recorder(void * priv)
  {
  recorder_t * r = priv;
  
  return &r->src;
  }

static void
close_recorder(void * priv)
  {
  //  recorder_t * r = priv;
  
  return;
  }

static const bg_input_plugin_t recorder_input =
  {
    .common =
    {
      BG_LOCALE,
      .name =           bg_recorder_input_name,
      .long_name =      "Recorder input plugin",
      .description =    TRS("Record media from Hardware devices"),
      .type =           BG_PLUGIN_INPUT,
      .flags =          BG_PLUGIN_URL,
      .priority =       BG_PLUGIN_PRIORITY_MAX,
      .create =         NULL,
      .destroy =        destroy_input,
      .get_parameters = get_parameters_recorder,
      .set_parameter =  set_parameter_recorder,
      .get_controllable =  get_controllable_recorder
      
    },
    
    .open =          open_recorder,

    //    .select_track =     ,

    .get_media_info = get_media_info_recorder,
    
    /*
     *  Start decoding.
     *  Track info is the track, which should be played.
     *  The plugin must take care of the "active" fields
     *  in the stream infos to check out, which streams are to be decoded
     */
    .get_src = get_src_recorder,
    
    /* Read one video frame (returns FALSE on EOF) */
    /*
     *  Do percentage seeking (can be NULL)
     *  Media streams are supposed to be seekable, if this
     *  function is non-NULL AND the duration field of the track info
     *  is > 0
     */
    //    .seek = seek_input,
    /* Stop playback, close all decoders */
    //    .stop = stop_recorder,
    .close = close_recorder,
  };

static int open_audio(recorder_t * r)
  {
  bg_media_source_stream_t * st;
  
  if(!r->as.enable || !r->as.info)
    return 1;

  gavl_audio_format_copy(r->as.fmt, &r->as.cfg_fmt);
  
  if(!r->as.plugin->open(r->as.handle->priv,
                         r->as.fmt, NULL, gavl_stream_get_metadata_nc(r->as.info)))
    return 0;

  st = bg_media_source_get_audio_stream(&r->src, 0);
  st->asrc = r->as.plugin->get_audio_source(r->as.handle->priv);
  
  return 1;
  }

static int open_video(recorder_t * r)
  {
  bg_media_source_stream_t * st;

  if(!r->vs.enable || !r->vs.info)
    return 1;

  gavl_video_format_copy(r->vs.fmt, &r->vs.cfg_fmt);
  
  if(!r->vs.plugin->open(r->vs.handle->priv,
                         NULL, r->vs.fmt, gavl_stream_get_metadata_nc(r->vs.info)))
    return 0;

  st = bg_media_source_get_video_stream(&r->src, 0);
  st->vsrc = r->vs.plugin->get_video_source(r->vs.handle->priv);
  
  return 1;
  }

static int start(recorder_t * r)
  {
  if(!open_audio(r) || !open_video(r))
    return 0;

  if(r->as.handle)
    bg_input_plugin_start(r->as.handle);
  if(r->vs.handle)
    bg_input_plugin_start(r->vs.handle);
  
  return 1;
  }



static int handle_cmd_input(void * data, gavl_msg_t * msg)
  {
  recorder_t * r = data;
  
  switch(msg->NS)
    {
    case GAVL_MSG_NS_SRC:
      switch(msg->ID)
        {
        case GAVL_CMD_SRC_START:
          if(!start(r))
            {
            
            }
          break;
        }
    }
  return 1;
  }

void * bg_recorder_input_create()
  {
  recorder_t * r = calloc(1, sizeof(*r));

  /* Initialize media info */

  r->track_info = gavl_append_track(&r->media_info, NULL);

  
  bg_controllable_init(&r->ctrl,
                       bg_msg_sink_create(handle_cmd_input, r, 1),
                       bg_msg_hub_create(1));
  
  return r;
  }

const bg_plugin_common_t* bg_recorder_input_get(void)
  {
  return (bg_plugin_common_t*)&recorder_input;
  }

bg_plugin_info_t *        bg_recorder_input_info(void)
  {
  bg_plugin_info_t * ret;
  
  if(!bg_plugin_registry_get_num_plugins(bg_plugin_reg, BG_PLUGIN_RECORDER_AUDIO, 0) &&
     !bg_plugin_registry_get_num_plugins(bg_plugin_reg, BG_PLUGIN_RECORDER_VIDEO, 0))
    return NULL;
  
  ret = bg_plugin_info_create(&recorder_input.common);
  ret->parameters = bg_parameter_info_copy_array(params);

  ret->protocols = gavl_value_set_array(&ret->protocols_val);
  gavl_string_array_add(ret->protocols, "recorder");
  
  return ret;
  }

