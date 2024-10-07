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
#include <stdio.h>

#include <unistd.h>

#include <config.h>
#include <gmerlin/translation.h>

#include "cdaudio.h"
#include <gmerlin/utils.h>
#include <gmerlin/log.h>

#include <gavl/utils.h>
#include <gavl/http.h>

#define LOG_DOMAIN "i_cdaudio"

#include <gavl/metatags.h>

#define FRAME_SAMPLES 588

// #define MIMETYPE "audio/x-gmerlin-cda"

typedef struct
  {
  bg_parameter_info_t * parameters;
  char * device_name;
  gavl_audio_format_t * fmt;
  
  gavl_dictionary_t mi;
  void * ripper;

  char disc_id[DISCID_SIZE];

  //  int fd;

  CdIo_t *cdio;
  
  bg_cdaudio_index_t * index;

  char * trackname_template;
  int use_cdtext;
  int use_local;

  /* We initialize ripping on demand to speed up CD loading in the
     transcoder */
  int rip_initialized;
  
  /* Configuration stuff */

#ifdef HAVE_MUSICBRAINZ
  int use_musicbrainz;
  char * musicbrainz_host;
  int    musicbrainz_port;
  char * musicbrainz_proxy_host;
  int    musicbrainz_proxy_port;
#endif

#ifdef HAVE_LIBCDDB
  int    use_cddb;
  char * cddb_host;
  int    cddb_port;
  char * cddb_path;
  char * cddb_proxy_host;
  int    cddb_proxy_port;
  char * cddb_proxy_user;
  char * cddb_proxy_pass;
  int cddb_timeout;
#endif
  
  int current_track;
  int current_sector; /* For ripping only */
      
  int first_sector;
  
  
  int old_seconds;
  bg_cdaudio_status_t status;

  int paused;
  
  bg_controllable_t controllable;

  bg_media_source_t ms;
  gavl_dictionary_t * cur;
  
  } cdaudio_t;

static int set_track_cdaudio(void * data, int track);
static int start_cdaudio(void * priv);

static void destroy_cd_data(cdaudio_t* cd)
  {
  gavl_dictionary_free(&cd->mi);

  if(cd->index)
    {
    bg_cdaudio_index_destroy(cd->index);
    cd->index = NULL;
    }

  }


static void close_cdaudio(void * priv);

static bg_controllable_t * get_controllable_cdaudio(void * priv)
  {
  cdaudio_t * cd = priv;
  return &cd->controllable;
  }

static void seek_cdaudio(void * priv, int64_t * time, int scale)
  {
  gavl_audio_source_t * asrc;
  /* We seek with frame accuracy (1/75 seconds) */

  uint32_t sample_position, samples_to_skip;
  
  cdaudio_t * cd = priv;
  
  if(!cd->rip_initialized)
    {
    bg_cdaudio_rip_init(cd->ripper, cd->cdio,
                        cd->first_sector);
    cd->rip_initialized = 1;
    }
  
  sample_position = gavl_time_rescale(scale, 44100, *time);
  
  cd->current_sector =
    sample_position / FRAME_SAMPLES + cd->index->tracks[cd->current_track].first_sector;
  samples_to_skip = sample_position % FRAME_SAMPLES;
  
  /* Seek to the point */

  bg_cdaudio_rip_seek(cd->ripper, cd->current_sector);
  
  /* Set skipped samples */

  asrc = bg_media_source_get_audio_source(&cd->ms, 0);
  
  gavl_audio_source_reset(asrc);
  gavl_audio_source_skip(asrc, samples_to_skip);
  }


static int handle_cmd(void * data, gavl_msg_t * msg)
  {
  cdaudio_t * priv = data;
  
  switch(msg->NS)
    {
    case GAVL_MSG_NS_SRC:
      switch(msg->ID)
        {
        case GAVL_CMD_SRC_SELECT_TRACK:
          {
          int track = gavl_msg_get_arg_int(msg, 0);
          if(!set_track_cdaudio(priv, track))
            {
            }
          }
          break;

        case GAVL_CMD_SRC_SEEK:
          {
          int64_t time = gavl_msg_get_arg_long(msg, 0);
          int scale = gavl_msg_get_arg_int(msg, 1);
          seek_cdaudio(priv, &time, scale);
          }
          break;
        case GAVL_CMD_SRC_START:
          {
          start_cdaudio(data);
          }
          
        }
      break;
    }
  return 1;
  }


static void * create_cdaudio()
  {
  cdaudio_t * ret;
  ret = calloc(1, sizeof(*ret));
  ret->ripper = bg_cdaudio_rip_create();

  bg_controllable_init(&ret->controllable,
                       bg_msg_sink_create(handle_cmd, ret, 1),
                       bg_msg_hub_create(1));
  
  return ret;
  }

#if 0
static void set_callbacks_cdaudio(void * data,
                           bg_input_callbacks_t * callbacks)
  {
  cdaudio_t * cd;
  cd = data;
  cd->callbacks = callbacks;
  }
#endif

#define MY_FREE(ptr) if(cd->ptr) free(cd->ptr)

static void destroy_cdaudio(void * data)
  {
  cdaudio_t * cd;
  cd = data;

  destroy_cd_data(cd);

  bg_media_source_cleanup(&cd->ms);
  
  MY_FREE(device_name);

  if(cd->ripper)
    bg_cdaudio_rip_destroy(cd->ripper);

  if(cd->parameters)
    bg_parameter_info_destroy_array(cd->parameters);

  MY_FREE(trackname_template);
  
#ifdef HAVE_MUSICBRAINZ
  MY_FREE(musicbrainz_host);
  MY_FREE(musicbrainz_proxy_host);
#endif

#ifdef HAVE_LIBCDDB
  MY_FREE(cddb_host);
  MY_FREE(cddb_path);
  MY_FREE(cddb_proxy_host);
  MY_FREE(cddb_proxy_user);
  MY_FREE(cddb_proxy_pass);
#endif

  bg_controllable_cleanup(&cd->controllable);
  
  free(data);
  }



static int open_cdaudio(void * data, const char * arg)
  {
  int have_local_metadata = 0;
  int have_metadata = 0;
  int i, j;
  gavl_dictionary_t * m;
  gavl_dictionary_t * track;
  gavl_dictionary_t * sm;
  const char * pos;
  char * tmp_filename;
  cdaudio_t * cd = data;
  //  char * uri;

  /* Destroy data from previous open */
  destroy_cd_data(cd);

  if((pos = strstr(arg, "://")))
    arg = pos + 3;
  
  cd->device_name = gavl_strrep(cd->device_name, arg);
  gavl_url_get_vars(cd->device_name, NULL);

  //  uri = gavl_sprintf("cda://%s", cd->device_name);
  
  cd->cdio = bg_cdaudio_open(cd->device_name);
  if(!cd->cdio)
    return 0;

  cd->index = bg_cdaudio_get_index(cd->cdio);
  if(!cd->index)
    return 0;

  //  bg_cdaudio_index_dump(cd->index);
  
  /* Create track infos */
  
  for(i = 0; i < cd->index->num_tracks; i++)
    {
    if(cd->index->tracks[i].is_audio)
      {
      gavl_dictionary_t * ti;
      gavl_stream_stats_t stats;
      
      j = cd->index->tracks[i].index;

      ti = gavl_append_track(&cd->mi, NULL);
      
      gavl_track_set_num_audio_streams(ti, 1);
      m = gavl_track_get_metadata_nc(ti);
      
      // gavl_metadata_add_src(m, GAVL_META_SRC, MIMETYPE, uri);

      memset(&stats, 0, sizeof(stats));

      stats.duration_min = FRAME_SAMPLES;
      stats.duration_max = FRAME_SAMPLES;

      stats.size_min = FRAME_SAMPLES * 4;
      stats.size_max = FRAME_SAMPLES * 4;

      stats.pts_end =
        (int64_t)(cd->index->tracks[i].last_sector -
                   cd->index->tracks[i].first_sector + 1) * FRAME_SAMPLES;

      
      cd->fmt = gavl_track_get_audio_format_nc(ti, 0);

      
      cd->fmt->samplerate = 44100;
      cd->fmt->num_channels = 2;
      cd->fmt->sample_format = GAVL_SAMPLE_S16;
      cd->fmt->interleave_mode = GAVL_INTERLEAVE_ALL;
      cd->fmt->samples_per_frame = FRAME_SAMPLES;

      sm = gavl_track_get_audio_metadata_nc(ti, 0);
      
      gavl_dictionary_set_string(sm, GAVL_META_FORMAT, "CD Audio");
      
      gavl_set_channel_setup(cd->fmt);

      gavl_stream_stats_apply_audio(&stats, cd->fmt, NULL, sm);
      
      gavl_dictionary_set_long(m,
                             GAVL_META_APPROX_DURATION,
                             ((int64_t)(cd->index->tracks[i].last_sector -
                                        cd->index->tracks[i].first_sector + 1) *
                              GAVL_TIME_SCALE) / 75);

      gavl_dictionary_set_string(m, GAVL_META_FORMAT,
                        "CD Audio");
      gavl_dictionary_set_int(m, GAVL_META_TRACKNUMBER,
                            j+1);

      gavl_dictionary_set_int(m, GAVL_META_CAN_PAUSE, 1);
      gavl_dictionary_set_int(m, GAVL_META_CAN_SEEK, 1);
      
      gavl_track_append_msg_stream(ti, GAVL_META_STREAM_ID_MSG_PROGRAM);
      }
    }

  gavl_track_update_children(&cd->mi);
  
  //  free(uri);
  
  /* Create the disc ID */

  bg_cdaudio_get_disc_id(cd->index, cd->disc_id);
  
  /* Now, try to get the metadata */

  /* 1st try: Check for cdtext */

  if(cd->use_cdtext)
    {
    if(bg_cdaudio_get_metadata_cdtext(cd->cdio,
                                      &cd->mi,
                                      cd->index))
      {
      gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Got metadata from CD-Text");
      have_metadata = 1;
      have_local_metadata = 1; /* We never save cdtext infos */
      }
    }

  /* 2nd try: Local file */

  if(!have_metadata && cd->use_local)
    {
    tmp_filename = bg_search_file_read("cdaudio", cd->disc_id);
    if(tmp_filename)
      {
      if(bg_cdaudio_load(&cd->mi, tmp_filename))
        {
        have_metadata = 1;
        have_local_metadata = 1;
        gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Got metadata from gmerlin cache (%s)", tmp_filename);
        }
      free(tmp_filename);
      }
    }
 

#ifdef HAVE_MUSICBRAINZ
  if(cd->use_musicbrainz && !have_metadata)
    {
    if(bg_cdaudio_get_metadata_musicbrainz(cd->index, &cd->mi,
                                           cd->disc_id,
                                           cd->musicbrainz_host,
                                           cd->musicbrainz_port,
                                           cd->musicbrainz_proxy_host,
                                           cd->musicbrainz_proxy_port))
      {
      gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Got metadata from musicbrainz (%s)", cd->musicbrainz_host);
      have_metadata = 1;
      }
    }
#endif

#ifdef HAVE_LIBCDDB
  if(cd->use_cddb && !have_metadata)
    {
    if(bg_cdaudio_get_metadata_cddb(cd->index,
                                    &cd->mi,
                                    cd->cddb_host,
                                    cd->cddb_port,
                                    cd->cddb_path,
                                    cd->cddb_proxy_host,
                                    cd->cddb_proxy_port,
                                    cd->cddb_proxy_user,
                                    cd->cddb_proxy_user,
                                    cd->cddb_timeout))
      {
      gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Got metadata from CDDB (%s)", cd->cddb_host);
      have_metadata = 1;
      /* Disable gmerlin caching */
      //      have_local_metadata = 1;
      }
    }
#endif
  
  if(have_metadata && !have_local_metadata)
    {
    tmp_filename = bg_search_file_write("cdaudio", cd->disc_id);
    if(tmp_filename)
      {
      bg_cdaudio_save(&cd->mi, tmp_filename);
      free(tmp_filename);
      }
    }
  
  if(!have_metadata)
    {
    for(i = 0; i < cd->index->num_tracks; i++)
      {
      if(cd->index->tracks[i].is_audio)
        {
        j = cd->index->tracks[i].index;
        track = gavl_get_track_nc(&cd->mi, j);
        m = gavl_track_get_metadata_nc(track);

        /* Finalize track */
        gavl_dictionary_set(m, GAVL_META_CLASS, NULL);
        gavl_track_finalize(track);
        
        if(cd->index->tracks[i].is_audio)
          gavl_dictionary_set_string_nocopy(m,
                                            GAVL_META_LABEL,
                                            gavl_sprintf(TR("Audio CD track %02d"), j+1));
        }
      }
    }
  else
    {
    for(i = 0; i < cd->index->num_tracks; i++)
      {
      if(cd->index->tracks[i].is_audio)
        {
        j = cd->index->tracks[i].index;
        if(cd->index->tracks[i].is_audio)
          {
          track = gavl_get_track_nc(&cd->mi, j);
          m = gavl_track_get_metadata_nc(track);
            
          /* Finalize track */
          gavl_dictionary_set(m, GAVL_META_CLASS, NULL);
          gavl_track_finalize(track);
          
          gavl_dictionary_set_string_nocopy(m,
                                            GAVL_META_LABEL,
                                            bg_create_track_name(m,
                                                                 cd->trackname_template));
          
          if(!j)
            {
            const char * var;
            var = gavl_dictionary_get_string(m, GAVL_META_ALBUM);
            
            m = gavl_dictionary_get_dictionary_create(&cd->mi, GAVL_META_METADATA);
            
            if(var)
              gavl_dictionary_set_string(m, GAVL_META_LABEL, var);
            
            gavl_dictionary_set_string(m, GAVL_META_CLASS, GAVL_META_CLASS_ROOT_REMOVABLE_AUDIOCD);
            }
          }
        }
      }
    }
  
  /* Set disk label */
  m = gavl_track_get_metadata_nc(&cd->mi);
  gavl_dictionary_set(m, GAVL_META_LABEL, gavl_dictionary_get(m, GAVL_META_TITLE));
  
  /* We close it again, so other apps won't cry */

  close_cdaudio(cd);
  
  return 1;
  }

static gavl_dictionary_t * get_media_info_cdaudio(void * data)
  {
  cdaudio_t * cd = data;
  return &cd->mi;
  }

static int set_track_cdaudio(void * data, int track)
  {
  int i;
  cdaudio_t * cd = data;

  for(i = 0; i < cd->index->num_tracks; i++)
    {
    if(cd->index->tracks[i].is_audio && (cd->index->tracks[i].index == track))
      {
      cd->current_track = i;
      break;
      }
    }

  cd->cur = gavl_get_track_nc(&cd->mi, cd->current_track);
  cd->first_sector = cd->index->tracks[cd->current_track].first_sector;

  bg_media_source_cleanup(&cd->ms);
  bg_media_source_init(&cd->ms);
  
  bg_media_source_set_from_track(&cd->ms, cd->cur);

  
  return 1;
  }


static gavl_source_status_t read_frame(void * priv, gavl_audio_frame_t ** fp)
  {
  gavl_audio_frame_t * f = *fp;
  cdaudio_t * cd = priv;

  if(cd->current_sector > cd->index->tracks[cd->current_track].last_sector)
    return GAVL_SOURCE_EOF;
  
  if(!cd->rip_initialized)
    {
    bg_cdaudio_rip_init(cd->ripper, cd->cdio,
                        cd->first_sector);
    cd->rip_initialized = 1;
    }
  bg_cdaudio_rip_rip(cd->ripper, f);
  
  f->valid_samples = FRAME_SAMPLES;
  f->timestamp =
    (cd->current_sector - cd->index->tracks[cd->current_track].first_sector) *
    FRAME_SAMPLES;

  cd->current_sector++;
  return GAVL_SOURCE_OK;
  }

static int start_cdaudio(void * priv)
  {
  bg_media_source_stream_t * s;
  cdaudio_t * cd = priv;
  
  s = bg_media_source_get_audio_stream(&cd->ms, 0);
  s->asrc_priv = gavl_audio_source_create(read_frame, cd, 0,
                                          /* Format is the same for all tracks */
                                          cd->fmt);
  s->asrc = s->asrc_priv;

  s = bg_media_source_get_stream_by_id(&cd->ms, GAVL_META_STREAM_ID_MSG_PROGRAM);
  s->msghub_priv = bg_msg_hub_create(1);
  s->msghub = s->msghub_priv;
  
  if(!cd->cdio)
    {
    cd->cdio = bg_cdaudio_open(cd->device_name);
    if(!cd->cdio)
      return 0;
    }
  
  cd->current_sector = cd->first_sector;

  if(!cd->rip_initialized)
    {
    bg_cdaudio_rip_init(cd->ripper, cd->cdio,
                        cd->first_sector);
    cd->rip_initialized = 1;
    }
  else
    bg_cdaudio_rip_seek(cd->ripper, cd->first_sector);
  
  return 1;
  }

static bg_media_source_t *
get_src_cdaudio(void * priv)
  {
  cdaudio_t * cd = priv;
  return &cd->ms;
  }

static void stop_cdaudio(void * priv)
  {
  cdaudio_t * cd = priv;
  if(cd->rip_initialized)
    {
    bg_cdaudio_rip_close(cd->ripper);
    cd->rip_initialized = 0;
    }
  
  cd->cdio = NULL;
  }



static void close_cdaudio(void * priv)
  {
  cdaudio_t * cd = priv;
  if(cd->cdio)
    {
    bg_cdaudio_close(cd->cdio);
    cd->cdio = NULL;
    }
  }

/* Configuration stuff */

static const bg_parameter_info_t parameters[] =
  {
    {
      .name =      "general",
      .long_name = TRS("General"),
      .type =      BG_PARAMETER_SECTION
    },
    {
      .name =        "trackname_template",
      .long_name =   TRS("Trackname template"),
      .type =        BG_PARAMETER_STRING,
      .val_default = GAVL_VALUE_INIT_STRING("%p - %t"),
      .help_string = TRS("Template for track name generation from metadata\n\
%p:    Artist\n\
%a:    Album\n\
%g:    Genre\n\
%t:    Track name\n\
%<d>n: Track number (d = number of digits, 1-9)\n\
%y:    Year\n\
%c:    Comment")
    },
    {
      .name =        "use_cdtext",
      .long_name =   TRS("Use CD-Text"),
      .type =        BG_PARAMETER_CHECKBUTTON,
      .val_default = GAVL_VALUE_INIT_INT(1),
      .help_string = TRS("Try to get CD metadata from CD-Text"),
    },
    {
      .name =        "use_local",
      .long_name =   TRS("Use locally saved metadata"),
      .type =        BG_PARAMETER_CHECKBUTTON,
      .val_default = GAVL_VALUE_INIT_INT(1),
      .help_string = TRS("Whenever we obtain CD metadata from the internet, we save them into \
$HOME/.gmerlin/cdaudio. If you got wrong metadata for a CD,\
 disabling this option will retrieve the metadata again and overwrite the saved data."),
    },
#ifdef HAVE_MUSICBRAINZ
    {
      .name =      "musicbrainz",
      .long_name = TRS("Musicbrainz"),
      .type =      BG_PARAMETER_SECTION
    },
    {
      .name =        "use_musicbrainz",
      .long_name =   TRS("Use Musicbrainz"),
      .type =        BG_PARAMETER_CHECKBUTTON,
      .val_default = GAVL_VALUE_INIT_INT(1)
    },
    {
      .name =        "musicbrainz_host",
      .long_name =   TRS("Server"),
      .type =        BG_PARAMETER_STRING,
    },
    {
      .name =        "musicbrainz_port",
      .long_name =   TRS("Port"),
      .type =         BG_PARAMETER_INT,
      .val_min =      GAVL_VALUE_INIT_INT(0),
      .val_max =      GAVL_VALUE_INIT_INT(65535),
      .val_default =  GAVL_VALUE_INIT_INT(0)
    },
    {
      .name =        "musicbrainz_proxy_host",
      .long_name =   TRS("Proxy"),
      .type =        BG_PARAMETER_STRING,
      .help_string = TRS("Proxy server (leave empty for direct connection)")
    },
    {
      .name =        "musicbrainz_proxy_port",
      .long_name =   TRS("Proxy port"),
      .type =         BG_PARAMETER_INT,
      .val_min =      GAVL_VALUE_INIT_INT(1),
      .val_max =      GAVL_VALUE_INIT_INT(65535),
      .val_default =  GAVL_VALUE_INIT_INT(80),
      .help_string = TRS("Proxy port")
    },
#endif
#ifdef HAVE_LIBCDDB
    {
      .name =      "cddb",
      .long_name = TRS("Cddb"),
      .type =      BG_PARAMETER_SECTION
    },
    {
      .name =        "use_cddb",
      .long_name =   TRS("Use Cddb"),
      .type =        BG_PARAMETER_CHECKBUTTON,
      .val_default = GAVL_VALUE_INIT_INT(1)
    },
    {
      .name =        "cddb_host",
      .long_name =   TRS("Server"),
      .type =        BG_PARAMETER_STRING,
      .val_default = GAVL_VALUE_INIT_STRING("freedb.org")
    },
    {
      .name =        "cddb_port",
      .long_name =   TRS("Port"),
      .type =         BG_PARAMETER_INT,
      .val_min =      GAVL_VALUE_INIT_INT(1),
      .val_max =      GAVL_VALUE_INIT_INT(65535),
      .val_default =  GAVL_VALUE_INIT_INT(80)
    },
    {
      .name =        "cddb_path",
      .long_name =   TRS("Path"),
      .type =        BG_PARAMETER_STRING,
      .val_default = GAVL_VALUE_INIT_STRING("/~cddb/cddb.cgi")
    },
    {
      .name =        "cddb_proxy_host",
      .long_name =   TRS("Proxy"),
      .type =        BG_PARAMETER_STRING,
      .help_string = TRS("Proxy server (leave empty for direct connection)")
    },
    {
      .name =        "cddb_proxy_port",
      .long_name =   TRS("Proxy port"),
      .type =         BG_PARAMETER_INT,
      .val_min =      GAVL_VALUE_INIT_INT(1),
      .val_max =      GAVL_VALUE_INIT_INT(65535),
      .val_default =  GAVL_VALUE_INIT_INT(80),
      .help_string = TRS("Proxy port")
    },
    {
      .name =        "cddb_proxy_user",
      .long_name =   TRS("Proxy username"),
      .type =        BG_PARAMETER_STRING,
      .help_string = TRS("User name for proxy (leave empty for poxies, which don't require authentication)")
    },
    {
      .name =        "cddb_proxy_pass",
      .long_name =   TRS("Proxy password"),
      .type =        BG_PARAMETER_STRING_HIDDEN,
      .help_string = TRS("Password for proxy")
    },
    {
      .name =        "cddb_timeout",
      .long_name =   TRS("Timeout"),
      .type =         BG_PARAMETER_INT,
      .val_min =      GAVL_VALUE_INIT_INT(0),
      .val_max =      GAVL_VALUE_INIT_INT(1000),
      .val_default =  GAVL_VALUE_INIT_INT(10),
      .help_string = TRS("Timeout (in seconds) for connections to the CDDB server")
    },
#endif
    { /* End of parmeters */ }
  };

static const bg_parameter_info_t * get_parameters_cdaudio(void * data)
  {
  cdaudio_t * cd = data;
  bg_parameter_info_t const * srcs[3];

  if(!cd->parameters)
    {
    srcs[0] = parameters;
    srcs[1] = bg_cdaudio_rip_get_parameters();
    srcs[2] = NULL;
    cd->parameters = bg_parameter_info_concat_arrays(srcs);
    }
    
  return cd->parameters;
  }

static void set_parameter_cdaudio(void * data, const char * name,
                                  const gavl_value_t * val)
  {
  cdaudio_t * cd = data;

  if(!name)
    return;

  if(bg_cdaudio_rip_set_parameter(cd->ripper, name, val))
    return;

  if(!strcmp(name, "trackname_template"))
    cd->trackname_template = gavl_strrep(cd->trackname_template, val->v.str);

  if(!strcmp(name, "use_cdtext"))
    cd->use_cdtext = val->v.i;
  if(!strcmp(name, "use_local"))
    cd->use_local = val->v.i;
  
#ifdef HAVE_MUSICBRAINZ
  if(!strcmp(name, "use_musicbrainz"))
    cd->use_musicbrainz = val->v.i;
  if(!strcmp(name, "musicbrainz_host"))
    cd->musicbrainz_host = gavl_strrep(cd->musicbrainz_host, val->v.str);
  if(!strcmp(name, "musicbrainz_port"))
    cd->musicbrainz_port = val->v.i;
  if(!strcmp(name, "musicbrainz_proxy_host"))
    cd->musicbrainz_proxy_host = gavl_strrep(cd->musicbrainz_proxy_host, val->v.str);
  if(!strcmp(name, "musicbrainz_proxy_port"))
    cd->musicbrainz_proxy_port = val->v.i;
#endif

#ifdef HAVE_LIBCDDB
  if(!strcmp(name, "use_cddb"))
    cd->use_cddb = val->v.i;
  if(!strcmp(name, "cddb_host"))
    cd->cddb_host = gavl_strrep(cd->cddb_host, val->v.str);
  if(!strcmp(name, "cddb_port"))
    cd->cddb_port = val->v.i;
  if(!strcmp(name, "cddb_path"))
    cd->cddb_path = gavl_strrep(cd->cddb_path, val->v.str);
  if(!strcmp(name, "cddb_proxy_host"))
    cd->cddb_proxy_host = gavl_strrep(cd->cddb_proxy_host, val->v.str);
  if(!strcmp(name, "cddb_proxy_port"))
    cd->cddb_proxy_port = val->v.i;
  if(!strcmp(name, "cddb_proxy_user"))
    cd->cddb_proxy_user = gavl_strrep(cd->cddb_proxy_user, val->v.str);
  if(!strcmp(name, "cddb_proxy_pass"))
    cd->cddb_proxy_pass = gavl_strrep(cd->cddb_proxy_pass, val->v.str);
  if(!strcmp(name, "cddb_timeout"))
    cd->cddb_timeout = val->v.i;
#endif
  }


char const * const protocols = "cda";
// char const * const mimetypes = MIMETYPE;

static const char * get_protocols(void * priv)
  {
  return protocols;
  }


const bg_input_plugin_t the_plugin =
  {
    .common =
    {
      BG_LOCALE,
      .name =          "i_cdaudio",
      .long_name =     TRS("Audio CD player/ripper"),
      .description =   TRS("Plugin for audio CDs. Supports both playing with direct connection from the CD-drive to the souncard and ripping with cdparanoia. Metadata are obtained from Musicbrainz, freedb or CD-text. Metadata are cached in $HOME/.gmerlin/cdaudio."),
      .type =          BG_PLUGIN_INPUT,

      .flags =         0,      
      .priority =      BG_PLUGIN_PRIORITY_MAX,
      .create =        create_cdaudio,
      .destroy =       destroy_cdaudio,
      .get_parameters = get_parameters_cdaudio,
      .set_parameter =  set_parameter_cdaudio,
      .get_controllable = get_controllable_cdaudio
    },
    .get_protocols = get_protocols,
    
    /* Open file/device */
    .open = open_cdaudio,
    
  /* For file and network plugins, this can be NULL */

    .get_media_info = get_media_info_cdaudio,

    /* Set track */
    //    .select_track =             ,
    /* Set streams */
    
    .get_src = get_src_cdaudio,
    
    /* Stop playback, close all decoders */
    .stop =         stop_cdaudio,
    .close =        close_cdaudio,
  };
/* Include this into all plugin modules exactly once
   to let the plugin loader obtain the API version */
BG_GET_PLUGIN_API_VERSION;
