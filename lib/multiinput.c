#include <string.h>

#include <config.h>

#include <gmerlin/translation.h>

#include <gmerlin/pluginregistry.h>
#include <gmerlin/utils.h>
#include <pluginreg_priv.h>

#include <gavl/trackinfo.h>

#include <gavl/log.h>
#define LOG_DOMAIN "multiinput"

typedef struct
  {
  gavl_dictionary_t mi;
  gavl_dictionary_t * ti;

  
  
  bg_media_source_t src;

  /* Global handle */
  bg_plugin_handle_t * h;
  bg_controllable_t controllable;
  
  } multi_t;

static void seek_multi(void * priv, int64_t * time, int scale)
  {
  int i;
  multi_t * m = priv;

  if(m->h)
    bg_input_plugin_seek(m->h, time, scale);

  for(i = 0; i < m->src.num_streams; i++)
    {
    if(m->src.streams[i]->user_data)
      bg_input_plugin_seek(m->src.streams[i]->user_data, time, scale);
    }
  
  }

static void start_multi(void * priv)
  {
  int i;
  multi_t * m = priv;

  if(m->h)
    bg_input_plugin_start(m->h);
  
  for(i = 0; i < m->src.num_streams; i++)
    {
    if(m->src.streams[i]->user_data)
      bg_input_plugin_start(m->src.streams[i]->user_data);
    }
  }

static int handle_cmd(void * data, gavl_msg_t * msg)
  {
  multi_t * priv = data;

  //  fprintf(stderr, "Handle CMD\n");
  //  gavl_msg_dump(msg, 2);
  
  switch(msg->NS)
    {
    case GAVL_MSG_NS_SRC:
      switch(msg->ID)
        {
        case GAVL_CMD_SRC_SELECT_TRACK:
          {
          int track = gavl_msg_get_arg_int(msg, 0);
          
          }
          break;
        case GAVL_CMD_SRC_START:
          start_multi(data);
          break;
        case GAVL_CMD_SRC_SEEK:
          {
          int64_t time = gavl_msg_get_arg_long(msg, 0);
          int scale = gavl_msg_get_arg_int(msg, 1);

          /* Seek */
          seek_multi(priv, &time, scale);
          }
          break;
        }
      break;
    }
  return 1;
  }


static bg_media_source_t * get_src_multi(void * priv)
  {
  multi_t * m = priv;
  return &m->src;
  }

static bg_controllable_t * get_controllable_multi(void * priv)
  {
  multi_t * m = priv;
  return &m->controllable;
  }


static gavl_dictionary_t * get_media_info_multi(void * priv)
  {
  multi_t * m = priv;
  return &m->mi;
  }

static int set_track_multi(void * priv, int track)
  {
  return 1;
  }

static void destroy_multi(void * priv)
  {
  
  }

static void close_multi(void * priv)
  {
  
  }


/*
 *  This meta plugin loads (optionally) one media file with multiple streams
 *  plus any number of additional streams from separate uris. This is used for
 *  subtitles from separate files and IPTV-scenarios, where different elementary
 *  streams come from different sources
 */

static const bg_input_plugin_t multi_plugin =
  {
    .common =
    {
      BG_LOCALE,
      .name =           "i_multi",
      .long_name =      TRS("Multi source decoder"),
      .description =    TRS("This metaplugin decodes multiple streams, which can come from separate sources"),
      .type =           BG_PLUGIN_INPUT,
      .flags =          0,
      .priority =       1,
      .create =         NULL,
      .destroy =        destroy_multi,

      .get_controllable = get_controllable_multi,
      //      .get_parameters = get_parameters_edl,
      //      .set_parameter =  set_parameter_edl
    },
    .get_media_info = get_media_info_multi,
    
    .get_src           = get_src_multi,
    
    /* Read one video frame (returns FALSE on EOF) */
    
    /*
     *  Do percentage seeking (can be NULL)
     *  Media streams are supposed to be seekable, if this
     *  function is non-NULL AND the duration field of the track info
     *  is > 0
     */
    //    .seek = seek_edl,
    /* Stop playback, close all decoders */
    //    .stop = stop_edl,
    .close = close_multi,
  };


int bg_input_plugin_load_multi(const gavl_dictionary_t * track,
                               bg_plugin_handle_t ** ret1,
                               bg_control_t * ctrl)
  {
  int num_streams;
  const gavl_dictionary_t * uri;
  
  /* Main URI */
  const gavl_dictionary_t * s;
  const gavl_dictionary_t * m;
  bg_plugin_handle_t * ret;
  int num_ext, i;
  multi_t * priv = calloc(1, sizeof(*priv));

  ret->plugin = (bg_plugin_common_t*)&multi_plugin;
  ret->info = bg_plugin_find_by_name(bg_plugin_reg, "i_multi");
  
  //  fprintf(stderr, "bg_input_plugin_load_edl\n");
  //  gavl_dictionary_dump(edl, 2);
  
  //  pthread_mutex_init(&ret->mutex, NULL);

  priv = calloc(1, sizeof(*priv));
  ret->priv = priv;
  ret->refcount = 1;

  priv->ti = gavl_append_track(&priv->mi, NULL);
  
  /* Load main uri */

  if((m = gavl_track_get_metadata(track)) &&
     (uri = gavl_dictionary_get_src(m, GAVL_META_SRC, 0, NULL, NULL)))
    {
    
    }

  num_ext = gavl_track_get_num_external_streams(track);

  for(i = 0; i < num_ext; i++)
    {
    s = gavl_track_get_external_stream(track, i);
    }
  
  bg_controllable_init(&priv->controllable,
                       bg_msg_sink_create(handle_cmd, priv, 1),
                       bg_msg_hub_create(1));

  bg_plugin_handle_connect_control(ret);

  
  }
