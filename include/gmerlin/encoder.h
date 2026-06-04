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



#ifndef BG_ENCODER_H_INCLUDED
#define BG_ENCODER_H_INCLUDED

/* Frontend for encoder plugins:
   It transparently handles the case that each stream goes to a
   separate file or not.
*/

#include <gmerlin/transcoder_track.h>

typedef struct bg_encoder_s bg_encoder_t;

bg_encoder_t * bg_encoder_create(bg_transcoder_track_t * tt,
                                 int stream_mask, int flag_mask);


const bg_parameter_info_t * bg_encoder_get_stream_parameters(bg_encoder_t * enc,
                                                             gavl_stream_type_t type);

void
bg_encoder_set_callbacks(bg_encoder_t * e, bg_encoder_callbacks_t * cb);


/* Also closes all internal encoders */
void bg_encoder_destroy(bg_encoder_t * enc, int do_delete); 

int bg_encoder_open(bg_encoder_t * enc, const char * filename_base,
                    const gavl_dictionary_t * metadata);

int bg_encoder_writes_compressed_audio(bg_encoder_t * enc,
                                       const gavl_audio_format_t * format,
                                       const gavl_compression_info_t * info);

int bg_encoder_writes_compressed_video(bg_encoder_t * enc,
                                       const gavl_video_format_t * format,
                                       const gavl_compression_info_t * info);

int bg_encoder_writes_compressed_overlay(bg_encoder_t * enc,
                                         const gavl_video_format_t * format,
                                         const gavl_compression_info_t * info);

/* Add streams */
int bg_encoder_add_audio_stream(bg_encoder_t *, const gavl_dictionary_t * m,
                                const gavl_audio_format_t * format,
                                int index, const gavl_dictionary_t * s);

int bg_encoder_add_video_stream(bg_encoder_t *,
                                const gavl_dictionary_t * m,
                                const gavl_video_format_t * format,
                                int index, const gavl_dictionary_t * s);

int bg_encoder_add_audio_stream_compressed(bg_encoder_t *,
                                           const gavl_dictionary_t * m,
                                           const gavl_audio_format_t * format,
                                           const gavl_compression_info_t * info,
                                           int index);

int bg_encoder_add_video_stream_compressed(bg_encoder_t *,
                                           const gavl_dictionary_t * m,
                                           const gavl_video_format_t * format,
                                           const gavl_compression_info_t * info,
                                           int index);


int bg_encoder_add_text_stream(bg_encoder_t *,
                               const gavl_dictionary_t * m,
                               int timescale,
                               int index, const gavl_dictionary_t * s);

int bg_encoder_add_overlay_stream(bg_encoder_t *,
                                  const gavl_dictionary_t * m,
                                  const gavl_video_format_t * format,
                                  int index,
                                  gavl_stream_type_t source_format,
                                  const gavl_dictionary_t * s);

int bg_encoder_add_overlay_stream_compressed(bg_encoder_t *,
                                             const gavl_dictionary_t * m,
                                             const gavl_video_format_t * format,
                                             const gavl_compression_info_t * info,
                                             int index);

/* Get formats */
void bg_encoder_get_audio_format(bg_encoder_t *, int stream,
                                 gavl_audio_format_t*ret);
void bg_encoder_get_video_format(bg_encoder_t *, int stream,
                                 gavl_video_format_t*ret);
void bg_encoder_get_overlay_format(bg_encoder_t *, int stream,
                                            gavl_video_format_t*ret);
void bg_encoder_get_text_timescale(bg_encoder_t *, int stream,
                                            uint32_t * ret);

/* Start encoding */
int bg_encoder_start(bg_encoder_t *);

/* Write frame */

gavl_audio_sink_t *
bg_encoder_get_audio_sink(bg_encoder_t *, int stream);

gavl_video_sink_t *
bg_encoder_get_video_sink(bg_encoder_t *, int stream);

gavl_packet_sink_t *
bg_encoder_get_audio_packet_sink(bg_encoder_t *, int stream);

gavl_packet_sink_t *
bg_encoder_get_video_packet_sink(bg_encoder_t *, int stream);

gavl_packet_sink_t *
bg_encoder_get_overlay_packet_sink(bg_encoder_t * enc, int stream);

gavl_packet_sink_t *
bg_encoder_get_text_sink(bg_encoder_t *, int stream);

gavl_video_sink_t *
bg_encoder_get_overlay_sink(bg_encoder_t *, int stream);

#endif // BG_ENCODER_H_INCLUDED

