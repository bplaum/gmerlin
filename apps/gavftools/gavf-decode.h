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

#include "gavftools.h"
// #include <gmerlin/tree.h>

#include <gavl/metatags.h>

extern bg_stream_action_t * audio_actions;
extern bg_stream_action_t * video_actions;
extern bg_stream_action_t * text_actions;
extern bg_stream_action_t * overlay_actions;

//extern char * album_file;
//extern char * m3u_file;
//extern char * xspf_file;

extern int shuffle;
extern int loop;

extern int default_samplerate;
extern int default_channels;


int load_input_file(const char * file, bg_mediaconnector_t * conn,
                    bg_control_t * ctrl,
                    bg_plugin_handle_t ** hp, 
                    gavl_dictionary_t * m,
                    int force_decompress);

#if 0
int load_album_entry(bg_album_entry_t * entry,
                     bg_mediaconnector_t * conn,
                     bg_plugin_handle_t ** hp, 
                     gavl_dictionary_t * m);
#endif


typedef struct
  {
  bg_mediaconnector_stream_t * in_stream;

  gavl_audio_source_t * asrc;
  gavl_video_source_t * vsrc;
  gavl_packet_source_t * psrc;

  gavl_audio_frame_t * mute_aframe;
  gavl_video_frame_t * mute_vframe;

  gavl_audio_frame_t * next_aframe;
  gavl_video_frame_t * next_vframe;

  gavl_audio_format_t afmt;
  gavl_video_format_t vfmt;

  int64_t pts_offset;
  int64_t pts;

  /* Time to mute the output (Audio or video) so we can keep A/V sync across multiple files */
  int64_t mute_time;

  int in_scale;
  int out_scale;

  gavl_source_status_t st;
  
  //  album_t * a;
  } stream_t;

/* decode_album.c */

// void album_init(album_t * a);
// void album_free(album_t * a);
// int init_decode_album(album_t * a);
// int album_set_eof(album_t * a);

/* decode_albumstream.c */

int stream_replug(stream_t * s, bg_mediaconnector_stream_t * in_stream);
int stream_create(bg_mediaconnector_stream_t * in_stream);
