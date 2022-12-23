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

static int set_track_multi(void * priv, int track);

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
  const char * uri;
  
  for(i = 0; i < m->src.num_streams; i++)
    {
    uri = NULL;
    if(!gavl_track_get_src(m->src.streams[i]->s, GAVL_META_SRC, 0, NULL, &uri))
      {
      /* Stream belongs to main handle */
      m->h->src->streams[i]->action = m->src.streams[i]->action;
      }
    }

  if(m->h)
    bg_input_plugin_start(m->h);

  /* Set up pipelines */

  for(i = 0; i < m->src.num_streams; i++)
    {
    if(m->src.streams[i]->action == BG_STREAM_ACTION_OFF)
      continue;
    
    uri = NULL;
    if(!gavl_track_get_src(m->src.streams[i]->s, GAVL_META_SRC, 0, NULL, &uri))
      {
      /* Stream belongs to main handle */
      m->src.streams[i]->psrc = m->h->src->streams[i]->psrc;
      m->src.streams[i]->vsrc = m->h->src->streams[i]->vsrc;
      m->src.streams[i]->asrc = m->h->src->streams[i]->asrc;
      }
    else
      {
      bg_plugin_handle_t * h;
      gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Loading external uri: %s", uri);
      h = bg_input_plugin_load(uri);
      /* Stream is external */
      m->src.streams[i]->user_data = h;
      bg_input_plugin_set_track(h, 0);

      /* For now, we assume that from exteral uris, we always load stream 0 */
      h->src->streams[0]->action = m->src.streams[i]->action;
      bg_input_plugin_start(h);
      m->src.streams[i]->psrc = h->src->streams[0]->psrc;
      m->src.streams[i]->vsrc = h->src->streams[0]->vsrc;
      m->src.streams[i]->asrc = h->src->streams[0]->asrc;
      }

    /* Set stream formats */
    if(m->src.streams[i]->asrc)
      {
      gavl_audio_format_copy(gavl_stream_get_audio_format_nc(m->src.streams[i]->s),
                             gavl_audio_source_get_src_format(m->src.streams[i]->asrc));
      }
    if(m->src.streams[i]->vsrc)
      {
      gavl_video_format_copy(gavl_stream_get_video_format_nc(m->src.streams[i]->s),
                             gavl_video_source_get_src_format(m->src.streams[i]->vsrc));
      }
    
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
          set_track_multi(priv, track);
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
  int i;
  multi_t * m = priv;

  if(m->h)
    bg_input_plugin_set_track(m->h, 0);

  /* Close all external plugins */
  for(i = 0; i < m->src.num_streams; i++)
    {
    if(m->src.streams[i]->user_data)
      {
      bg_plugin_unref(m->src.streams[i]->user_data);
      m->src.streams[i]->user_data = NULL;
      }
    }
  
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

bg_plugin_info_t * bg_multi_input_get_info()
  {
  return bg_plugin_info_create(&multi_plugin.common);
  }


bg_plugin_handle_t * bg_input_plugin_load_multi(const gavl_dictionary_t * track, bg_plugin_handle_t * h)
  {
  //  int num_streams;
  const char * uri = NULL;
  gavl_stream_type_t type;
  
  /* Main URI */
  const gavl_dictionary_t * m;
  const gavl_dictionary_t * s;
  
  bg_plugin_handle_t * ret;
  int num, i;
  bg_media_source_stream_t * src_s;
  multi_t * priv = calloc(1, sizeof(*priv));

  ret = calloc(1, sizeof(*ret));

  ret->plugin = (bg_plugin_common_t*)&multi_plugin;
  ret->info = bg_plugin_find_by_name(bg_plugin_reg, "i_multi");

  if(!track)
    track = bg_input_plugin_get_track_info(h, -1);
  
  //  fprintf(stderr, "bg_input_plugin_load_multi\n");
  //  gavl_dictionary_dump(track, 2);
  
  //  pthread_mutex_init(&ret->mutex, NULL);

  ret->priv = priv;
  ret->refcount = 1;

  priv->ti = gavl_append_track(&priv->mi, NULL);
  priv->src.track = priv->ti;
  
  /* Load main uri */

  if(!h)
    {
    if((m = gavl_track_get_metadata(priv->ti)) &&
       (gavl_metadata_get_src(m, GAVL_META_SRC, 0, NULL, &uri)))
      {
      gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Loading primary uri: %s", uri);
      h = bg_input_plugin_load(uri);
      bg_input_plugin_set_track(h, 0);
      }
    }

  if(h)
    {
    gavl_dictionary_t * ti;
    
    ti = bg_input_plugin_get_track_info(h, 0);

        
    num = gavl_track_get_num_streams_all(ti);
    for(i = 0; i < num; i++)
      {
      if(!(s = gavl_track_get_stream_all(ti, i)) ||
         ((type = gavl_stream_get_type(s)) == GAVL_STREAM_NONE))
        return 0;
      src_s = bg_media_source_append_stream(&priv->src, type);
      gavl_dictionary_reset(src_s->s);
      gavl_dictionary_copy(src_s->s, s);
    
      if(type == GAVL_STREAM_MSG)
        gavl_stream_get_id(src_s->s, &src_s->stream_id);
      }
    priv->h = h;
    }
  
  num = gavl_track_get_num_external_streams(track);

  for(i = 0; i < num; i++)
    {
    if((s = gavl_track_get_external_stream(track, i)) &&
       ((type = gavl_stream_get_type(s)) != GAVL_STREAM_NONE))
      {
      src_s = bg_media_source_append_stream(&priv->src, type);
      gavl_dictionary_reset(src_s->s);
      gavl_dictionary_copy(src_s->s, s);
      }
    }
  
  bg_controllable_init(&priv->controllable,
                       bg_msg_sink_create(handle_cmd, priv, 1),
                       bg_msg_hub_create(1));
  
  bg_plugin_handle_connect_control(ret);

  //  fprintf(stderr, "Loaded multi plugin\n");
  //  gavl_dictionary_dump(priv->ti, 2);
  return ret;
  }
