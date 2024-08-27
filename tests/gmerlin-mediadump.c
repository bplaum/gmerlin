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


#include <config.h>

#include <gavl/log.h>
#define LOG_DOMAIN "mediadump"

#include <gmerlin/pluginregistry.h>
#include <gmerlin/cfgctx.h>

#include <gmerlin/cmdline.h>
#include <gmerlin/translation.h>
#include <gmerlin/utils.h>
#include <gmerlin/application.h>

static int input_flags = 0;
// static int num_frames = 10;


static void opt_track(void * data, int * argc, char *** _argv, int arg)
  {
  input_flags |= BG_INPUT_FLAG_SELECT_TRACK;
  }

static bg_cmdline_arg_t global_options[] =
  {
    {
      .arg =         "-track",
      .help_string = "Select and return single track (passed in the URL)",
      .callback =    opt_track,
    },
    {
      /* End */
    }
  };

const bg_cmdline_app_data_t app_data =
  {
    .package =  PACKAGE,
    .version =  VERSION,
    .synopsis = TRS("[options] <gml>\n"),
    .help_before = TRS("mediadump\n"),
    .args = (bg_cmdline_arg_array_t[]) { { TRS("Options"), global_options },
                                       {  } },
  };

int main(int argc, char ** argv)
  {
  int i, num_streams;
  bg_plugin_handle_t * h;
  gavl_dictionary_t track;
  const gavl_dictionary_t * ti;
  gavl_audio_frame_t * af;
  gavl_video_frame_t * vf;
  gavl_packet_t * pkt;
  bg_media_source_stream_t * st;
  
  bg_app_init("mediadump", TRS("Dump media frames"), NULL);

  bg_cmdline_init(&app_data);
  bg_cmdline_parse(global_options, &argc, &argv, NULL);
  
  /* Create registries */

  bg_plugins_init();

  gavl_dictionary_init(&track);
  gavl_track_from_location(&track, argv[1]);

  if(!(h = bg_load_track(&track)))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Couln't open location");
    return EXIT_FAILURE;
    }

  /* Enable streams */
  ti = bg_input_plugin_get_track_info(h, -1);
  
  num_streams = bg_media_source_get_num_streams(h->src, GAVL_STREAM_AUDIO);
  for(i = 0; i < num_streams; i++)
    {
    bg_media_source_set_audio_action(h->src, i,
                                     BG_STREAM_ACTION_DECODE);
    }
  num_streams = bg_media_source_get_num_streams(h->src, GAVL_STREAM_VIDEO);
  for(i = 0; i < num_streams; i++)
    {
    bg_media_source_set_video_action(h->src, i,
                                     BG_STREAM_ACTION_DECODE);
    }
  num_streams = bg_media_source_get_num_streams(h->src, GAVL_STREAM_TEXT);
  for(i = 0; i < num_streams; i++)
    {
    bg_media_source_set_text_action(h->src, i,
                                    BG_STREAM_ACTION_DECODE);
    }
  num_streams = bg_media_source_get_num_streams(h->src, GAVL_STREAM_OVERLAY);
  for(i = 0; i < num_streams; i++)
    {
    bg_media_source_set_overlay_action(h->src, i,
                                       BG_STREAM_ACTION_DECODE);
    }
  
  bg_input_plugin_start(h);

  /* Initialize sources */
  
  fprintf(stderr, "Loaded track");
  gavl_dictionary_dump(ti, 2);
  fprintf(stderr, "\n");

  num_streams = bg_media_source_get_num_streams(h->src, GAVL_STREAM_AUDIO);
  for(i = 0; i < num_streams; i++)
    {
    st =  bg_media_source_get_audio_stream(h->src, i);

    if(st->asrc)
      gavl_audio_source_set_dst(st->asrc, 0, NULL);
    else
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Audio initialisation failed");
      return EXIT_FAILURE;
      }
    }
  num_streams = bg_media_source_get_num_streams(h->src, GAVL_STREAM_VIDEO);
  for(i = 0; i < num_streams; i++)
    {
    st =  bg_media_source_get_video_stream(h->src, i);
    if(st->vsrc)
      gavl_video_source_set_dst(st->vsrc, 0, NULL);
    else
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Video initialisation failed");
      return EXIT_FAILURE;
      }
    }
  num_streams = bg_media_source_get_num_streams(h->src, GAVL_STREAM_OVERLAY);
  for(i = 0; i < num_streams; i++)
    {
    st =  bg_media_source_get_overlay_stream(h->src, i);
    if(st->vsrc)
      gavl_video_source_set_dst(st->vsrc, 0, NULL);
    else
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Overlay initialisation failed");
      return EXIT_FAILURE;
      }
    }
  
  /* Read frames */
  num_streams = bg_media_source_get_num_streams(h->src, GAVL_STREAM_AUDIO);
  for(i = 0; i < num_streams; i++)
    {
    const gavl_audio_format_t * fmt;

    st =  bg_media_source_get_audio_stream(h->src, i);
    af = NULL;

    fmt = gavl_audio_source_get_dst_format(st->asrc);
    
    fprintf(stderr, "Reading frame from audio stream %d...", i+1);
    
    if(gavl_audio_source_read_frame(st->asrc, &af) == GAVL_SOURCE_OK)
      {
      
      fprintf(stderr, "done, PTS: %"PRId64" [%"PRId64"], duration: %d\n",
              af->timestamp, gavl_time_unscale(fmt->samplerate, af->timestamp),
              af->valid_samples);
      }
    else
      {
      fprintf(stderr, "failed\n");
      break;
      }
    }
  num_streams = bg_media_source_get_num_streams(h->src, GAVL_STREAM_VIDEO);
  for(i = 0; i < num_streams; i++)
    {
    const gavl_video_format_t * fmt;
    st =  bg_media_source_get_video_stream(h->src, i);
    fmt = gavl_video_source_get_dst_format(st->vsrc);

    vf = NULL;

    fprintf(stderr, "Reading frame from video stream %d...", i+1);
    
    if(gavl_video_source_read_frame(st->vsrc, &vf) == GAVL_SOURCE_OK)
      {
      fprintf(stderr, "done, PTS: %"PRId64" [%"PRId64"], duration: %"PRId64"\n",
              vf->timestamp, gavl_time_unscale(fmt->timescale, vf->timestamp),
              vf->duration);
      
      }
    else
      {
      fprintf(stderr, "failed\n");
      break;
      }

    }
  num_streams = bg_media_source_get_num_streams(h->src, GAVL_STREAM_TEXT);
  for(i = 0; i < num_streams; i++)
    {
    int timescale;
    st =  bg_media_source_get_text_stream(h->src, i);

    timescale = gavl_packet_source_get_timescale(st->psrc);
    
    pkt = NULL;

    fprintf(stderr, "Reading frame from text stream %d...", i+1);
    
    if(gavl_packet_source_read_packet(st->psrc, &pkt) == GAVL_SOURCE_OK)
      {
      fprintf(stderr, "done, PTS: %"PRId64" [%"PRId64"], duration: %"PRId64"\n",
              pkt->pts, gavl_time_unscale(timescale, pkt->pts),
              pkt->duration);
      }
    else
      {
      fprintf(stderr, "failed\n");
      break;
      }

    }
  num_streams = bg_media_source_get_num_streams(h->src, GAVL_STREAM_OVERLAY);
  for(i = 0; i < num_streams; i++)
    {
    const gavl_video_format_t * fmt;
    st =  bg_media_source_get_overlay_stream(h->src, i);
    fmt = gavl_video_source_get_dst_format(st->vsrc);
    vf = NULL;

    fprintf(stderr, "Reading frame from overlay stream %d...", i+1);
    

    if(gavl_video_source_read_frame(st->vsrc, &vf) == GAVL_SOURCE_OK)
      {
      fprintf(stderr, "done, PTS: %"PRId64" [%"PRId64"], duration: %"PRId64"\n",
              vf->timestamp, gavl_time_unscale(fmt->timescale, vf->timestamp),
              vf->duration);
      }
    else
      {
      fprintf(stderr, "failed\n");
      break;
      }
    }
  
  
  
  bg_plugin_unref(h);

  return EXIT_SUCCESS;
  }
