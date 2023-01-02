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

#ifndef BG_TRANSCODER_TRACK_H_INCLUDED
#define BG_TRANSCODER_TRACK_H_INCLUDED

#include <libxml/tree.h>
#include <libxml/parser.h>


#include <gmerlin/streaminfo.h>


/* This defines a track with all information
   necessary for transcoding */

#define BG_TRANSCODER_TRACK_GENERAL         "$general"
#define BG_TRANSCODER_TRACK_ENCODER         "$encoder"
#define BG_TRANSCODER_TRACK_FILTER          "$filter"

#define BG_TRANSCODER_TRACK_ENCODER_TEXT    "$encoder_text"
#define BG_TRANSCODER_TRACK_ENCODER_OVERLAY "$encoder_overlay"
#define BG_TRANSCODER_TRACK_TEXTRENDERER    "textrenderer"

#define BG_TRANSCODER_TRACK_XML_ROOT        "transcoder_tracks"


typedef gavl_dictionary_t bg_transcoder_track_t;

#define bg_transcoder_track_get_cfg_general_nc(dict)         gavl_dictionary_get_dictionary_create(dict, BG_TRANSCODER_TRACK_GENERAL)
#define bg_transcoder_track_get_cfg_encoder_text_nc(dict)    gavl_dictionary_get_dictionary_create(dict, BG_TRANSCODER_TRACK_ENCODER_TEXT)
#define bg_transcoder_track_get_cfg_encoder_overlay_nc(dict) gavl_dictionary_get_dictionary_create(dict, BG_TRANSCODER_TRACK_ENCODER_OVERLAY)
#define bg_transcoder_track_get_cfg_textrenderer_nc(dict)    gavl_dictionary_get_dictionary_create(dict, BG_TRANSCODER_TRACK_TEXTRENDERER)
#define bg_transcoder_track_get_cfg_filter_nc(dict)          gavl_dictionary_get_dictionary_create(dict, BG_TRANSCODER_TRACK_FILTER)

#define bg_transcoder_track_get_cfg_general(dict)         gavl_dictionary_get_dictionary(dict, BG_TRANSCODER_TRACK_GENERAL)
#define bg_transcoder_track_get_cfg_encoder_text(dict)    gavl_dictionary_get_dictionary(dict, BG_TRANSCODER_TRACK_ENCODER_TEXT)
#define bg_transcoder_track_get_cfg_encoder_overlay(dict) gavl_dictionary_get_dictionary(dict, BG_TRANSCODER_TRACK_ENCODER_OVERLAY)
#define bg_transcoder_track_get_cfg_textrenderer(dict)    gavl_dictionary_get_dictionary(dict, BG_TRANSCODER_TRACK_TEXTRENDERER)
#define bg_transcoder_track_get_cfg_filter(dict)          gavl_dictionary_get_dictionary(dict, BG_TRANSCODER_TRACK_FILTER)



const bg_parameter_info_t *
bg_transcoder_track_get_general_parameters(void);

const bg_parameter_info_t *
bg_transcoder_track_audio_get_general_parameters(void);

const bg_parameter_info_t *
bg_transcoder_track_video_get_general_parameters(void);

const bg_parameter_info_t *
bg_transcoder_track_text_get_general_parameters(void);

const bg_parameter_info_t *
bg_transcoder_track_overlay_get_general_parameters(void);

void bg_transcoder_track_get_encoders(const bg_transcoder_track_t * t,
                                      bg_cfg_section_t * encoder_section);
void bg_transcoder_track_set_encoders(bg_transcoder_track_t * t,
                                      const bg_cfg_section_t * encoder_section);

gavl_array_t *
bg_transcoder_track_create(const gavl_dictionary_t * track,
                           bg_cfg_section_t * section,
                           bg_cfg_section_t * encoder_section);

gavl_array_t *
bg_transcoder_track_create_from_urilist(const char * list,
                                        int len,
                                        bg_cfg_section_t * section,
                                        bg_cfg_section_t * encoder_section);

gavl_array_t *
bg_transcoder_track_create_from_albumentries(const char * xml_string,
                                             bg_cfg_section_t * section,
                                             bg_cfg_section_t * encoder_section);


/* For putting informations into the track list */

/* strings must be freed after */

const char * bg_transcoder_track_get_name(const bg_transcoder_track_t * t);

const char * bg_transcoder_track_get_audio_encoder(const bg_transcoder_track_t * t);
const char * bg_transcoder_track_get_video_encoder(const bg_transcoder_track_t * t);
const char * bg_transcoder_track_get_text_encoder(const bg_transcoder_track_t * t);
const char * bg_transcoder_track_get_overlay_encoder(const bg_transcoder_track_t * t);

void bg_transcoder_track_get_duration(const bg_transcoder_track_t * t,
                                      gavl_time_t * ret, gavl_time_t * ret_total);

bg_parameter_info_t *
bg_transcoder_track_create_parameters(bg_transcoder_track_t * track);

void
bg_transcoder_track_split_at_chapters(gavl_array_t * ret, const bg_transcoder_track_t * t);

/* transcoder_track_xml.c */

void bg_transcoder_tracks_save(bg_transcoder_track_t * t,
                               const char * filename);

bg_transcoder_track_t *
bg_transcoder_tracks_load(const char * filename,
                          bg_plugin_registry_t * plugin_reg);

#endif // BG_TRANSCODER_TRACK_H_INCLUDED
