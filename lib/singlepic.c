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

#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#include <config.h>
#include <gmerlin/translation.h>

#include <gmerlin/cfg_registry.h>
#include <gmerlin/pluginregistry.h>
#include <gmerlin/pluginfuncs.h>

#include <gmerlin/utils.h>
#include <gmerlin/singlepic.h>
#include <gmerlin/log.h>
#include <gmerlin/bggavl.h>

#include <gavl/metatags.h>
#include <gavl/packetindex.h>

#define LOG_DOMAIN_ENC "singlepicture-encoder"
#define LOG_DOMAIN_DEC "singlepicture-decoder"

#define TIMESCALE_KEY "#TIMESCALE: "
#define FRAMEDUR_KEY  "#FRAMEDUR: "

#define DURATION_KEY  "#DURATION: "
#define PTS_KEY       "#PTS: "


/* Image list */


static char * get_extensions(uint32_t type_mask, uint32_t flag_mask)
  {
  int num, i;
  char * ret = NULL;
  const bg_plugin_info_t * info;
  gavl_array_t  arr;
  gavl_array_init(&arr);
  
  num = bg_get_num_plugins(type_mask, flag_mask);
  if(!num)
    return NULL;

  for(i = 0; i < num; i++)
    {
    info = bg_plugin_find_by_index(i, type_mask, flag_mask);
    gavl_array_splice_array(&arr, -1, 0, info->extensions);
    }
  
  ret = bg_string_array_to_string(&arr);
  gavl_array_free(&arr);
  
  return ret;
  }

/* Input stuff */

static const bg_parameter_info_t parameters_input[] =
  {
    {
      .name =      "timescale",
      .long_name = TRS("Timescale"),
      .type =      BG_PARAMETER_INT,
      .val_min =     GAVL_VALUE_INIT_INT(1),
      .val_max =     GAVL_VALUE_INIT_INT(100000),
      .val_default = GAVL_VALUE_INIT_INT(25)
    },
    {
      .name =      "frame_duration",
      .long_name = TRS("Frame duration"),
      .type =      BG_PARAMETER_INT,
      .val_min =     GAVL_VALUE_INIT_INT(1),
      .val_max =     GAVL_VALUE_INIT_INT(100000),
      .val_default = GAVL_VALUE_INIT_INT(1)
    },
    { /* End of parameters */ }
  };

static const bg_parameter_info_t parameters_input_still[] =
  {
    {
      .name =         "display_time",
      .long_name =    TRS("Display time"),
      .type =         BG_PARAMETER_TIME,
      .val_min =      GAVL_VALUE_INIT_LONG(0),
      .val_max =      GAVL_VALUE_INIT_LONG(3600 * (gavl_time_t)GAVL_TIME_SCALE),
      .val_default =  GAVL_VALUE_INIT_LONG(0),
      .help_string =  TRS("Time to pass until the next track will be selected. 0 means infinite.")
    },
    { /* End of parameters */ }
  };


typedef struct
  {
  gavl_dictionary_t mi;
  gavl_dictionary_t * track_info;

  gavl_video_format_t * fmt;
  gavl_compression_info_t ci;
  
  //  int timescale;
  //  int frame_duration;

  gavl_time_t display_time;
  
  bg_plugin_handle_t * handle;
  bg_image_reader_plugin_t * image_reader;

  char * filename_buffer;
  int do_still;
  
  bg_controllable_t controllable;

  bg_media_source_t ms;

  gavl_packet_index_t idx;
  int idx_pos;
  
  gavl_io_t * io;

  char * line;
  int line_alloc;
  
  char * path;
  } input_t;

static int init_reader(input_t * inp)
  {
  int64_t line_start;
  int64_t pts = 0;
  int64_t duration = 0;
  
  inp->fmt->framerate_mode = GAVL_FRAMERATE_VARIABLE;
  
  /* Read global variables */
  while(1)
    {
    line_start = gavl_io_position(inp->io);


    if(!gavl_io_read_line(inp->io, &inp->line, &inp->line_alloc, 1024))
      return 0;
    /* Global parameters */
    if(gavl_string_starts_with(inp->line, TIMESCALE_KEY))
      inp->fmt->timescale = atoi(inp->line + strlen(TIMESCALE_KEY));
    else if(gavl_string_starts_with(inp->line, FRAMEDUR_KEY))
      {
      inp->fmt->frame_duration = atoi(inp->line + strlen(FRAMEDUR_KEY));
      inp->fmt->framerate_mode = GAVL_FRAMERATE_CONSTANT;
      }
    else
      break;
    }

  while(1)
    {
    if(gavl_string_starts_with(inp->line, PTS_KEY))
      pts = strtoll(inp->line + strlen(PTS_KEY), NULL, 10);
    else if(gavl_string_starts_with(inp->line, DURATION_KEY))
      duration = strtoll(inp->line + strlen(DURATION_KEY), NULL, 10);
    else
      {
      /* Filename */
      if(inp->fmt->framerate_mode == GAVL_FRAMERATE_CONSTANT)
        duration = inp->fmt->frame_duration;
      
      gavl_packet_index_add(&inp->idx,
                            line_start,
                            strlen(inp->line),
                            0,
                            pts,
                            GAVL_PACKET_KEYFRAME, duration);
      pts += duration;
      duration = 0;
      }
    line_start = gavl_io_position(inp->io);
    if(!gavl_io_read_line(inp->io, &inp->line, &inp->line_alloc, 1024))
      break;
    }
  
  return 1;
  }

static int probe_image(input_t * inp, const char * filename)
  {
  const bg_plugin_info_t * info;
  
  info = bg_plugin_find_by_filename(filename, BG_PLUGIN_IMAGE_READER);
  if(!info)
    return 0;
  
  inp->handle = bg_plugin_load(info);

  if(!inp->handle)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN_DEC,
           "Plugin %s could not be loaded",
           info->name);
    return 0;
    }
  
  inp->image_reader = (bg_image_reader_plugin_t*)inp->handle->plugin;

  if(!inp->image_reader->read_header(inp->handle->priv,
                                     inp->filename_buffer,
                                     inp->fmt))
    return 0;
  
  if(inp->image_reader->get_compression_info)
    inp->image_reader->get_compression_info(inp->handle->priv, &inp->ci);

  inp->image_reader->read_image(inp->handle->priv, NULL);
  
  return 1;
  }

static const bg_parameter_info_t * get_parameters_input(void * priv)
  {
  return parameters_input;
  }

static const bg_parameter_info_t * get_parameters_input_still(void * priv)
  {
  return parameters_input_still;
  }

static void set_parameter_input(void * priv, const char * name,
                                const gavl_value_t * val)
  {
  input_t * inp = priv;

  if(!name)
    return;

  if(!strcmp(name, "display_time"))
    {
    inp->display_time = val->v.l;
    if(!inp->display_time)
      inp->display_time = GAVL_TIME_UNDEFINED;
    }
  }

static void finalize_metadata_input(gavl_video_format_t * format,
                                    gavl_dictionary_t * m,
                                    const char * mimetype,
                                    const char * uri)
  {
  struct stat st;
  // const char * format = NULL;

  if(!mimetype)
    mimetype = gavl_dictionary_get_string(m, GAVL_META_MIMETYPE);
  
  if(mimetype)
    {
    gavl_dictionary_t * src;
    
    //    format = bg_mimetype_to_name(mimetype);
    src = gavl_metadata_add_image_uri(m, GAVL_META_SRC, format->image_width, format->image_height,
                                      mimetype, uri);
    
    /* Clear toplevel fields */
    gavl_dictionary_set(m, GAVL_META_MIMETYPE, NULL);
    gavl_dictionary_set(m, GAVL_META_FORMAT, NULL);

    if(!stat(uri, &st))
      gavl_dictionary_set_long(src, GAVL_META_MTIME, st.st_mtime);
    }
  }

static char * get_filename(input_t * inp)
  {
  if(inp->do_still)
    return inp->filename_buffer;
  
  gavl_io_seek(inp->io, inp->idx.entries[inp->idx_pos].position, SEEK_SET);
  
  if(gavl_io_read_data(inp->io, (uint8_t*)inp->line, inp->idx.entries[inp->idx_pos].size) < inp->idx.entries[inp->idx_pos].size)
    return NULL;
  
  inp->line[inp->idx.entries[inp->idx_pos].size]='\0';
  
  /* TODO */
  if(*inp->line == '/')
    return inp->line;
  else if(inp->path)
    {
    if(!inp->filename_buffer)
      inp->filename_buffer = malloc(PATH_MAX);
    snprintf(inp->filename_buffer, PATH_MAX, "%s/%s", inp->path, inp->line);
    return inp->filename_buffer;
    }
  else
    return inp->line;
  }

static int open_input(void * priv, const char * filename)
  {
  const char * pos;
  const gavl_dictionary_t * m;
  
  gavl_dictionary_t * s;
  input_t * inp = priv;
  gavl_dictionary_t * tm;
  gavl_stream_stats_t stats;

  /* Create stream (must be first) */

  s = gavl_track_append_video_stream(inp->track_info);
  tm = gavl_track_get_metadata_nc(inp->track_info);
  inp->fmt = gavl_stream_get_video_format_nc(s);
  
  
  if((pos = strrchr(filename, '/')))
    inp->path = gavl_strndup(filename, pos);
  
  if(!(inp->io = gavl_io_from_filename(filename, 0)))
    return 0;
  
  if(!init_reader(inp))
    return 0;
  
  gavl_stream_stats_init(&stats);
  gavl_packet_index_set_stream_stats(&inp->idx, 0, &stats);
  
  
  if(!probe_image(inp, get_filename(inp)))
    return 0;

  gavl_packet_index_set_stream_stats(&inp->idx, 0, &stats);
  gavl_stream_set_stats(s, &stats);
  gavl_stream_set_compression_info(s, &inp->ci);
  
  gavl_track_append_msg_stream(inp->track_info, GAVL_META_STREAM_ID_MSG_PROGRAM);
  
  /* Get track name */

  bg_set_track_name_default(inp->track_info, filename);
  
  gavl_dictionary_set_int(tm, GAVL_META_CAN_SEEK, 1);
  gavl_dictionary_set_int(tm, GAVL_META_CAN_PAUSE, 1);
  
  /* Load image header and get format */

  if(!probe_image(inp, inp->filename_buffer))
    return 0;
  

  /* Metadata */

  if(inp->image_reader->get_metadata &&
     (m = inp->image_reader->get_metadata(inp->handle->priv)))
    {
    gavl_dictionary_merge2(tm, m);
    }
  
  gavl_dictionary_set_string(gavl_track_get_video_metadata_nc(inp->track_info, 0),
                             GAVL_META_FORMAT, "Single images");

  finalize_metadata_input(inp->fmt, gavl_track_get_metadata_nc(inp->track_info),
                          BG_IMGLIST_MIMETYPE,
                          filename);
  
  gavl_track_finalize(inp->track_info);
  return 1;
  }

static int open_stills_input(void * priv, const char * filename)
  {
  const char * tag;
  const gavl_dictionary_t * m;
  
  gavl_dictionary_t * tm;
  gavl_dictionary_t * s;
  input_t * inp = priv;
  
  /* Check if the first file exists */
  
  if(access(filename, R_OK))
    return 0;
  
  /* Create stream */
  
  s = gavl_track_append_video_stream(inp->track_info);
  
  tm = gavl_track_get_metadata_nc(inp->track_info);
  
  inp->fmt = gavl_stream_get_video_format_nc(s);
  
  gavl_dictionary_set_long(tm, GAVL_META_APPROX_DURATION, inp->display_time);
  
  /* Get track name */

  bg_set_track_name_default(inp->track_info, filename);

  inp->filename_buffer = gavl_strrep(inp->filename_buffer, filename);
  
  if(!probe_image(inp, inp->filename_buffer))
    return 0;
  inp->fmt->timescale = GAVL_TIME_SCALE;
  inp->fmt->frame_duration = 0;
  inp->fmt->framerate_mode = GAVL_FRAMERATE_STILL;

  /* Add message stream */
  gavl_track_append_msg_stream(inp->track_info, GAVL_META_STREAM_ID_MSG_PROGRAM);
  

  
  /* Metadata */

  if(inp->image_reader->get_metadata &&
     (m = inp->image_reader->get_metadata(inp->handle->priv)))
    {
    gavl_dictionary_merge2(tm, m);
    }

  tag = gavl_dictionary_get_string(tm, GAVL_META_FORMAT);
  if(tag)
    gavl_dictionary_set_string(gavl_track_get_video_metadata_nc(inp->track_info, 0),
                               GAVL_META_FORMAT, tag);
  else
    gavl_dictionary_set_string(gavl_track_get_video_metadata_nc(inp->track_info, 0),
                               GAVL_META_FORMAT, "Image");
  
  gavl_dictionary_set_string(tm,
                             GAVL_META_FORMAT, "Still image");


  finalize_metadata_input(inp->fmt, gavl_track_get_metadata_nc(inp->track_info), NULL, filename);
  gavl_track_finalize(inp->track_info);
  return 1;

  }

static gavl_dictionary_t * get_media_info_input(void * priv)
  {
  input_t * inp = priv;
  return &inp->mi;
  }

static int check_eof(input_t * inp)
  {
  if(inp->do_still)
    {
    if(inp->idx_pos)
      return 1;
    }
  else if(inp->idx_pos >= inp->idx.num_entries)
    return 1;
  
  return 0;
  }

static gavl_source_status_t
read_video_func_input(void * priv, gavl_video_frame_t ** fp)
  {
  gavl_video_format_t format;
  input_t * inp = priv;
  gavl_video_frame_t * f = *fp;
  char * filename;

  
  if(check_eof(inp))
    return GAVL_SOURCE_EOF;

  
  filename = get_filename(inp);
  //  fprintf(stderr, "read_video_func_input %s %d\n", filename, inp->idx_pos);
  
  if(!inp->image_reader->read_header(inp->handle->priv, filename, &format))
    return GAVL_SOURCE_EOF;
  
  if(!inp->image_reader->read_image(inp->handle->priv, f))
    {
    return GAVL_SOURCE_EOF;
    }
  
  if(f)
    {
    if(inp->do_still)
      {
      f->timestamp = 0;
      f->duration = -1;
      }
    else
      {
      f->timestamp = inp->idx.entries[inp->idx_pos].pts;
      f->duration  = inp->idx.entries[inp->idx_pos].duration;
      }
    }
  inp->idx_pos++;
  return GAVL_SOURCE_OK;
  }

static gavl_source_status_t read_video_packet_input(void * priv, gavl_packet_t ** p)
  {
  FILE * in;
  input_t * inp = priv;
  char * filename;

  if(check_eof(inp))
    return GAVL_SOURCE_EOF;

  filename = get_filename(inp);
  
  in = fopen(filename, "rb");
  if(!in)
    return 0;

  gavl_buffer_reset(&(*p)->buf);

  if(!bg_read_file(filename, &(*p)->buf))
    return GAVL_SOURCE_EOF;
  
  if(inp->do_still)
    {
    (*p)->pts = 0;
    (*p)->duration = -1;
    }
  else
    {
    (*p)->pts = inp->idx.entries[inp->idx_pos].pts;
    (*p)->duration  = inp->idx.entries[inp->idx_pos].duration;
    }
  
  (*p)->flags = GAVL_PACKET_KEYFRAME;
  inp->idx_pos++;
  return 1;
  }


static int start_input(void * priv)
  {
  bg_media_source_stream_t * s;

  input_t * inp = priv;
  const gavl_video_format_t * fmt;
  fmt = gavl_track_get_video_format(inp->track_info, 0);
  
  s = bg_media_source_get_video_stream(&inp->ms, 0);
  
  if(s->action == BG_STREAM_ACTION_DECODE)
    {
    s->vsrc_priv = gavl_video_source_create(read_video_func_input, inp, 0, fmt);
    s->vsrc = s->vsrc_priv;
    }

  else if(s->action == BG_STREAM_ACTION_READRAW)
    {
    gavl_compression_info_t ci;
    gavl_compression_info_init(&ci);
    gavl_stream_get_compression_info(gavl_track_get_video_stream(inp->track_info, 0), &ci);
    
    s->psrc_priv =
      gavl_packet_source_create(read_video_packet_input,
                                inp, 0,
                                gavl_track_get_video_stream(inp->track_info, 0));
    
    s->psrc = s->psrc_priv;
    
    gavl_compression_info_free(&ci);
    }

  s = bg_media_source_get_msg_stream_by_id(&inp->ms, GAVL_META_STREAM_ID_MSG_PROGRAM);
  s->msghub_priv = bg_msg_hub_create(1);
  s->msghub = s->msghub_priv;
  
  return 1;
  }

static bg_media_source_t * get_src_input(void * priv)
  {
  input_t * inp = priv;
  return &inp->ms;
  }

static int set_track_input(void * priv)
  {
  input_t * inp = priv;
  
  /* Reset image reader */
  inp->idx_pos = 0;
  

  bg_media_source_cleanup(&inp->ms);
  bg_media_source_init(&inp->ms);

  bg_media_source_set_from_track(&inp->ms, inp->track_info);
  
  return 1;
  }

static void seek_input(void * priv, int64_t * time, int scale)
  {
  input_t * inp = priv;
  int64_t time_scaled = gavl_time_rescale(scale, inp->fmt->timescale, *time);

  inp->idx_pos = gavl_packet_index_seek(&inp->idx, 0, time_scaled);
  time_scaled = inp->idx.entries[inp->idx_pos].pts;
  *time = gavl_time_rescale(inp->fmt->timescale, scale, time_scaled);
  }

static void stop_input(void * priv)
  {
  /*
  input_t * inp = priv;

  

  if(inp->action != BG_STREAM_ACTION_DECODE)
    return;
  */
  }

#define FREE(p) if(p) { free(p); p = NULL; }

static void close_input(void * priv)
  {
  input_t * inp = priv;
  FREE(inp->filename_buffer);
  FREE(inp->path);
  FREE(inp->line);
  
  gavl_dictionary_reset(&inp->mi);
  /* Unload the plugin */
  if(inp->handle)
    bg_plugin_unref(inp->handle);
  inp->handle = NULL;
  inp->image_reader = NULL;

  gavl_compression_info_free(&inp->ci);
  gavl_compression_info_init(&inp->ci);
  
  bg_media_source_cleanup(&inp->ms);
  bg_media_source_init(&inp->ms);
  }

static void destroy_input(void* priv)
  {
  input_t * inp = priv;
  close_input(priv);
  bg_controllable_cleanup(&inp->controllable);
  free(priv);
  }

static bg_controllable_t * get_controllable_input(void * priv)
  {
  input_t * inp = priv;
  return &inp->controllable;
  }

static const bg_input_plugin_t input_plugin =
  {
    .common =
    {
      BG_LOCALE,
      .name =           bg_singlepic_input_name,
      .long_name =      TRS("Image video input plugin"),
      .description =    TRS("This plugin reads series of images as a video. It uses the installed image readers."),
      .type =           BG_PLUGIN_INPUT,
      .flags =          BG_PLUGIN_FILE,
      .priority =       5,
      .create =         NULL,
      .destroy =        destroy_input,
      .get_parameters = get_parameters_input,
      .set_parameter =  set_parameter_input,
      .get_controllable =  get_controllable_input
      
    },
    .open =          open_input,

    //    .get_num_tracks = bg_avdec_get_num_tracks,
    .get_media_info = get_media_info_input,
    /*
     *  Start decoding.
     *  Track info is the track, which should be played.
     *  The plugin must take care of the "active" fields
     *  in the stream infos to check out, which streams are to be decoded
     */
    .get_src              = get_src_input,
    
    /* Stop playback, close all decoders */
    .stop = stop_input,
    .close = close_input,
  };

static const bg_input_plugin_t input_plugin_stills =
  {
    .common =
    {
      BG_LOCALE,
      .name =           bg_singlepic_stills_input_name,
      .long_name =      "Still image input plugin",
      .description =    TRS("This plugin reads images as stills. It uses the installed image readers."),
      .type =           BG_PLUGIN_INPUT,
      .flags =          BG_PLUGIN_FILE,
      .priority =       BG_PLUGIN_PRIORITY_MAX,
      .create =         NULL,
      .destroy =        destroy_input,
      .get_parameters = get_parameters_input_still,
      .set_parameter =  set_parameter_input,
      .get_controllable =  get_controllable_input
      
    },
    .open =          open_stills_input,

    //    .select_track =     ,

    .get_media_info = get_media_info_input,
    
    /*
     *  Start decoding.
     *  Track info is the track, which should be played.
     *  The plugin must take care of the "active" fields
     *  in the stream infos to check out, which streams are to be decoded
     */
    .get_src = get_src_input,
    
    /* Read one video frame (returns FALSE on EOF) */
    /*
     *  Do percentage seeking (can be NULL)
     *  Media streams are supposed to be seekable, if this
     *  function is non-NULL AND the duration field of the track info
     *  is > 0
     */
    //    .seek = seek_input,
    /* Stop playback, close all decoders */
    .stop = stop_input,
    .close = close_input,
  };


const bg_plugin_common_t * bg_singlepic_input_get()
  {
  return (const bg_plugin_common_t*)&input_plugin;
  }

const bg_plugin_common_t * bg_singlepic_stills_input_get()
  {
  return (const bg_plugin_common_t*)&input_plugin_stills;
  }

static bg_plugin_info_t * get_input_info(const bg_input_plugin_t * plugin, int still)
  {
  bg_plugin_info_t * ret;
  
  if(!bg_get_num_plugins(BG_PLUGIN_IMAGE_READER,
                         BG_PLUGIN_FILE))
    return NULL;

  ret = bg_plugin_info_create(&plugin->common);
  ret->extensions = gavl_value_set_array(&ret->extensions_val);

  if(still)  
    {
    char * str;
    str = get_extensions(BG_PLUGIN_IMAGE_READER,
                         BG_PLUGIN_FILE);
    bg_string_to_string_array(str, ret->extensions);
    free(str);
    }
  else
    gavl_string_array_add(ret->extensions, BG_IMGLIST_EXT);
  
  return ret;
  }

bg_plugin_info_t * bg_singlepic_input_info(void)
  {
  bg_plugin_info_t * ret;
  ret = get_input_info(&input_plugin, 0);
  if(ret)
    ret->parameters = bg_parameter_info_copy_array(parameters_input);
  return ret;
  }

bg_plugin_info_t * bg_singlepic_stills_input_info(void)
  {
  bg_plugin_info_t * ret;
  ret = get_input_info(&input_plugin_stills, 1);
  if(ret)
    ret->parameters = bg_parameter_info_copy_array(parameters_input_still);
  return ret;
  }

static int handle_cmd_input(void * data, gavl_msg_t * msg)
  {
  input_t * priv = data;

  //  fprintf(stderr, "handle_cmd_input:\n");
  //  gavl_msg_dump(msg, 2);
  
  switch(msg->NS)
    {
    case GAVL_MSG_NS_SRC:
      switch(msg->ID)
        {
        case GAVL_CMD_SRC_START:
          {
          bg_media_source_stream_t * s;
          s = bg_media_source_get_video_stream(&priv->ms, 0);
          /* Close image reader */
          if(s->action == BG_STREAM_ACTION_READRAW)
            {
            /* Unload the plugin */
            if(priv->handle)
              bg_plugin_unref(priv->handle);
            priv->handle = NULL;
            priv->image_reader = NULL;
            }
          start_input(data);
          }
          break;
        case GAVL_CMD_SRC_SELECT_TRACK:
          {
          set_track_input(priv);
          }
          break;

        case GAVL_CMD_SRC_SEEK:
          {
          int64_t time = gavl_msg_get_arg_long(msg, 0);
          int scale = gavl_msg_get_arg_int(msg, 1);
          
          /* Seek */
          if(priv->do_still)
            {
            gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN_DEC,
                   "Seeking not supported for still image");
            return 1;
            }
          
          seek_input(priv, &time, scale);

          }
          break;

          
        }
      break;
    }
  return 1;
  }


void * bg_singlepic_input_create()
  {
  input_t * ret;
  ret = calloc(1, sizeof(*ret));
  bg_controllable_init(&ret->controllable,
                       bg_msg_sink_create(handle_cmd_input, ret, 1),
                       bg_msg_hub_create(1));

  ret->track_info = gavl_append_track(&ret->mi, NULL);
  return ret;
  }

void * bg_singlepic_stills_input_create()
  {
  input_t * ret;
  ret = calloc(1, sizeof(*ret));
  ret->track_info = gavl_append_track(&ret->mi, NULL);

  bg_controllable_init(&ret->controllable,
                       bg_msg_sink_create(handle_cmd_input, ret, 1),
                       bg_msg_hub_create(1));

  ret->do_still = 1;
  return ret;
  }

/* Encoder stuff */

static const bg_parameter_info_t parameters_encoder[] =
  {
    {
      .name =        "plugin",
      .long_name =   TRS("Plugin"),
      .type =        BG_PARAMETER_MULTI_MENU,
    },
    { /* End */ }
  };

typedef struct
  {
  char * filename_base;
  int base_len;
  
  bg_plugin_handle_t * plugin_handle;
  bg_image_writer_plugin_t * image_writer;
  
  gavl_dictionary_t metadata;
  
  bg_parameter_info_t * parameters;
  
  int64_t frame_counter;
  
  char * mask;
  char * filename_buffer;
  
  gavl_video_format_t format;
  
  bg_encoder_callbacks_t * cb;
  
  bg_iw_callbacks_t iw_callbacks;
  
  gavl_video_sink_t * sink;
  gavl_packet_sink_t * psink;
  
  int64_t pts;
  int64_t duration;

  int compressed;
  
  FILE * list;
  
  } encoder_t;

static int iw_callbacks_create_output_file(void * priv, const char * filename)
  {
  encoder_t * e = priv;

  if(e->frame_counter < 0)
    return 1;
  
  if(!e->frame_counter)
    {
    switch(e->format.framerate_mode)
      {
      case GAVL_FRAMERATE_CONSTANT:
        fprintf(e->list, "%s%d\n", TIMESCALE_KEY, e->format.timescale);
        fprintf(e->list, "%s%d\n", FRAMEDUR_KEY, e->format.frame_duration);
        break;
      case GAVL_FRAMERATE_VARIABLE:
      case GAVL_FRAMERATE_STILL:
        fprintf(e->list, "%s%d\n", TIMESCALE_KEY, e->format.timescale);
        break;
      case GAVL_FRAMERATE_UNKNOWN:
        return 0;
      }
    }
  
  if(e->format.framerate_mode == GAVL_FRAMERATE_VARIABLE)
    {
    fprintf(e->list, "%s%"PRId64"\n", PTS_KEY, e->pts);
    if(e->duration > 0)
      fprintf(e->list, "%s%"PRId64"\n", DURATION_KEY, e->duration);
    }
  else if(e->format.framerate_mode == GAVL_FRAMERATE_CONSTANT)
    {
    if((e->pts != 0) && !e->frame_counter)
      fprintf(e->list, "%s%"PRId64"\n", PTS_KEY, e->pts);
    }
  else if(e->format.framerate_mode == GAVL_FRAMERATE_STILL)
    {
    fprintf(e->list, "%s%"PRId64"\n", PTS_KEY, e->pts);
    }
  
  fprintf(e->list, "%s\n", filename + e->base_len);
  
  return 1;
  }

static bg_parameter_info_t *
create_encoder_parameters(void)
  {
  bg_parameter_info_t * ret = bg_parameter_info_copy_array(parameters_encoder);
  bg_plugin_registry_set_parameter_info(bg_plugin_reg,
                                        BG_PLUGIN_IMAGE_WRITER,
                                        BG_PLUGIN_FILE, &ret[0]);
  return ret;
  }

static const bg_parameter_info_t * get_parameters_encoder(void * priv)
  {
  encoder_t * enc = priv;
  
  if(!enc->parameters)
    enc->parameters = create_encoder_parameters();
  return enc->parameters;
  }

static void set_parameter_encoder(void * priv, const char * name, 
                                  const gavl_value_t * val)
  {
  encoder_t * e = priv;
  
  if(!name)
    {
    return;
    }
  else if(!strcmp(name, "plugin"))
    {
    const char * plugin_name = bg_multi_menu_get_selected_name(val);
    /* Load plugin */

    if(!e->plugin_handle || strcmp(e->plugin_handle->info->name, plugin_name))
      {
      if(e->plugin_handle)
        {
        bg_plugin_unref(e->plugin_handle);
        e->plugin_handle = NULL;
        }

      e->plugin_handle = bg_plugin_load_with_options(bg_multi_menu_get_selected(val));
      e->image_writer = (bg_image_writer_plugin_t*)(e->plugin_handle->plugin);
      
      if(e->image_writer->set_callbacks)
        e->image_writer->set_callbacks(e->plugin_handle->priv, &e->iw_callbacks);
      }
    }
  else
    {
    if(e->plugin_handle && e->plugin_handle->plugin->set_parameter)
      {
      e->plugin_handle->plugin->set_parameter(e->plugin_handle->priv, name, val);
      }
    }
  }

static void set_callbacks_encoder(void * data, bg_encoder_callbacks_t * cb)
  {
  encoder_t * e = data;
  e->cb = cb;
  }

static void create_mask(encoder_t * e, const char * ext)
  {
  int filename_len;
  const char * pos;
  if((pos = strrchr(e->filename_base, '/')))
    {
    pos++;
    e->base_len = (int)(pos - e->filename_base);
    }
  else
    {
    e->base_len = 0;
    pos = e->filename_base;
    }
  
  e->mask = gavl_sprintf("%s/%s-%%08"PRId64, e->filename_base, pos);
  
  if(ext)
    {
    e->mask = gavl_strcat(e->mask, ".");
    e->mask = gavl_strcat(e->mask, ext);
    }
  
  filename_len = strlen(e->mask) + 8 + 32;
  e->filename_buffer = malloc(filename_len);
  }

static int open_encoder(void * data, const char * filename,
                        const gavl_dictionary_t * metadata)
  {
  char * list_file;
  encoder_t * e = data;
  
  e->filename_base = gavl_strrep(e->filename_base, filename);

  if(!gavl_ensure_directory(e->filename_base, 1))
    return 0;

  list_file = gavl_sprintf("%s."BG_IMGLIST_EXT, e->filename_base);
  if(!bg_encoder_cb_create_output_file(e->cb, list_file))
    return 0;

  if(!(e->list = fopen(list_file, "w")))
    return 0;

  
  if(metadata)
    gavl_dictionary_copy(&e->metadata, metadata);
  
  return 1;
  }

static int write_frame_header(encoder_t * e)
  {
  int ret;
  
  /* Create filename */

  if(e->frame_counter < 0)
    sprintf(e->filename_buffer, e->mask, 0);
  else
    sprintf(e->filename_buffer, e->mask, e->frame_counter);
  
  ret = e->image_writer->write_header(e->plugin_handle->priv,
                                      e->filename_buffer,
                                      &e->format, &e->metadata);

  if(ret && (e->frame_counter < 0))
    {
    gavl_video_frame_t * f = gavl_video_frame_create(&e->format);
    gavl_video_frame_clear(f, &e->format);

    ret = e->image_writer->write_image(e->plugin_handle->priv, f);
    gavl_video_frame_destroy(f);
    }
  
  e->frame_counter++;
  return ret;
  }

static gavl_sink_status_t
write_video_func_encoder(void * data, gavl_video_frame_t * frame)
  {
  int ret = 1;
  encoder_t * e = data;

  e->pts = frame->timestamp;
  e->pts = frame->timestamp;
  
  ret = write_frame_header(e);
  
  if(ret)
    ret = e->image_writer->write_image(e->plugin_handle->priv, frame);
  
  return ret ? GAVL_SINK_OK : GAVL_SINK_ERROR;
  }

static gavl_sink_status_t write_packet_encoder(void * data, gavl_packet_t * packet)
  {
  FILE * out;
  encoder_t * e = data;
  sprintf(e->filename_buffer, e->mask, e->frame_counter);

  //  if(!bg_encoder_cb_create_output_file(e->cb, e->filename_buffer))
  //    return GAVL_SINK_ERROR;

  e->pts = packet->pts;
  e->duration = packet->duration;
  
  out = fopen(e->filename_buffer, "wb");
  if(!out)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN_ENC, "Cannot open file %s: %s",
           e->filename_buffer, strerror(errno));
    return GAVL_SINK_ERROR;
    }
  
  if(fwrite(packet->buf.buf, 1, packet->buf.len, out) < packet->buf.len)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN_ENC, "Couldn't write data to %s: %s",
           e->filename_buffer, strerror(errno));
    return GAVL_SINK_ERROR;
    }
  fclose(out);
  e->frame_counter++;
  return GAVL_SINK_OK;
  }

static int start_encoder(void * data)
  {
  encoder_t * e = data;

  if(e->compressed)
    e->psink = gavl_packet_sink_create(NULL, write_packet_encoder,
                                       e);
  else
    e->sink = gavl_video_sink_create(NULL, write_video_func_encoder,
                                     e, &e->format);
  
  return !!e->psink || !!e->sink;
  }

static gavl_video_sink_t * get_video_sink_encoder(void * priv, int stream)
  {
  encoder_t * e = priv;
  return e->sink;
  }

static gavl_packet_sink_t * get_packet_sink_encoder(void * priv, int stream)
  {
  encoder_t * e = priv;
  return e->psink;
  }

static int writes_compressed_video(void * priv,
                                   const gavl_video_format_t * format,
                                   const gavl_compression_info_t * info)
  {
  int separate = 0;
  
  if(gavl_compression_get_extension(info->id, &separate) &&
     separate)
    return 1;
  return 0;
  }

static int add_video_stream_encoder(void * data,
                                    const gavl_dictionary_t * m,
                                    const gavl_video_format_t * format)
  {
  encoder_t * e = data;
  
  gavl_video_format_copy(&e->format, format);
  create_mask(e, NULL);
  /* Write image header so we know the format */
  e->frame_counter = -1;
  write_frame_header(e);
  return 0;
  }

static int
add_video_stream_compressed_encoder(void * data,
                                    const gavl_dictionary_t * m,
                                    const gavl_video_format_t * format,
                                    const gavl_compression_info_t * info)
  {
  encoder_t * e = data;
  create_mask(e, gavl_compression_get_extension(info->id, NULL));
  return 0;
  }


#define STR_FREE(s) if(s){free(s);s=NULL;}

static int close_encoder(void * data, int do_delete)
  {
  int64_t i;
  
  encoder_t * e = data;

  if(do_delete)
    {
    for(i = 0; i < e->frame_counter; i++)
      {
      sprintf(e->filename_buffer, e->mask, i);
      remove(e->filename_buffer);
      }
    }
  
  STR_FREE(e->mask);
  STR_FREE(e->filename_buffer);
  STR_FREE(e->filename_base);
  
  gavl_dictionary_reset(&e->metadata);
  
  if(e->plugin_handle)
    {
    bg_plugin_unref(e->plugin_handle);
    e->plugin_handle = NULL;
    }

  if(e->sink)
    {
    gavl_video_sink_destroy(e->sink);
    e->sink = NULL;
    }

  if(e->list)
    {
    fclose(e->list);
    e->list = NULL;
    }
  return 1;
  }

static void destroy_encoder(void * data)
  {
  encoder_t * e = data;
  close_encoder(data, 0);

  if(e->parameters)
    {
    bg_parameter_info_destroy_array(e->parameters);
    }
  free(e);
  }


const bg_encoder_plugin_t encoder_plugin =
  {
    .common =
    {
      BG_LOCALE,
      .name =           bg_singlepic_encoder_name,
      .long_name =      "Single images",
      .description =    TRS("This plugin encodes a video as a series of images. It uses the installed image writers."),

      .type =           BG_PLUGIN_ENCODER_VIDEO,
      .flags =          BG_PLUGIN_FILE,
      .priority =       BG_PLUGIN_PRIORITY_MAX,
      .create =         NULL,
      .destroy =        destroy_encoder,
      .get_parameters = get_parameters_encoder,
      .set_parameter =  set_parameter_encoder
    },
    
    /* Maximum number of audio/video streams. -1 means infinite */
    
    .max_audio_streams = 0,
    .max_video_streams = 1,

    /* Open a file, filename base is without extension, which
       will be added by the plugin */
    
    .set_callbacks =     set_callbacks_encoder,
    
    .open =              open_encoder,

    .writes_compressed_video = writes_compressed_video,
    
    .add_video_stream =  add_video_stream_encoder,
    .add_video_stream_compressed =  add_video_stream_compressed_encoder,
    
    .start =             start_encoder,

    .get_video_sink =    get_video_sink_encoder,
    .get_video_packet_sink =    get_packet_sink_encoder,
    
    //    .write_video_frame = write_video_frame_encoder,
    //    .write_video_packet = write_video_packet_encoder,
    
    
    /* Close it */

    .close =             close_encoder,
  };

const bg_plugin_common_t * bg_singlepic_encoder_get()
  {
  return (bg_plugin_common_t*)(&encoder_plugin);
  }

bg_plugin_info_t * bg_singlepic_encoder_info()
  {
  bg_plugin_info_t * ret;
  
  if(!bg_get_num_plugins(BG_PLUGIN_IMAGE_WRITER, BG_PLUGIN_FILE))
    return NULL;
  ret = bg_plugin_info_create(&encoder_plugin.common);
  ret->parameters = create_encoder_parameters();
  return ret;
  }

void * bg_singlepic_encoder_create()
  {
  encoder_t * ret;
  ret = calloc(1, sizeof(*ret));

  ret->iw_callbacks.data = ret;
  ret->iw_callbacks.create_output_file = iw_callbacks_create_output_file;
  
  return ret;
  }
