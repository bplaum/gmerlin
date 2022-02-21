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

#ifndef __BG_STREAMINFO_H_
#define __BG_STREAMINFO_H_

#include <gavl/gavl.h>
#include <gavl/metadata.h>
#include <gavl/chapterlist.h>
#include <gavl/trackinfo.h>

#include <libxml/tree.h>
#include <libxml/parser.h>

#include <gmerlin/msgqueue.h>


/** \defgroup streaminfo Track information
 *  \ingroup plugin_i
 *
 *  These structures describe media tracks with their streams.
 *  They are returned by the input plugin.
 *
 *  @{
 */

/************************************************
 * Types for describing media streams
 ************************************************/

/* Languages are ISO 639-2 (3 character code) */

/*
 * media type definitions
 */

/** \brief Create trackname from metadata
 *  \param m Metadata
 *  \param format Format string
 *  \returns A newly allocated track name or NULL
 *
 *  The format string can contain arbitrary characters and the
 *  following placeholders
 *
 *  - %p: Artist
 *  - %a: Album
 *  - %g: Genre
 *  - %t: Track name
 *  - %c: Comment
 *  - %y: Year
 *  - %\<d\>n: Track number with \<d\> digits
 *
 *  If the string corresponding to a placeholder is NULL, the whole
 *  function is aborted and NULL is returned.
 *  
 */

char * bg_create_track_name(const gavl_dictionary_t * m, const char * format);

/** \brief Convert metadata to a humanized string
 *  \param m Metadata
 *  \param use_tabs Indicate, that tabs (\\t) should be used in the output
 *  \returns A newly allocated string
 */

char * bg_metadata_to_string(const gavl_dictionary_t * m, int use_tabs);

/** \brief Try to get the year from the metadata
 *  \param m Metadata
 *  \returns The year as int
 *
 *  The date string can be in multiple formats. This function
 *  tries to extract the year and return it as int.
 */

int bg_metadata_get_year(const gavl_dictionary_t * m);

/** \brief Try to get the bitrate from the metadata
 *  \param m Metadata
 *  \returns A string usable as bitrate display
 *
 */

char * bg_metadata_bitrate_string(const gavl_dictionary_t * m, const char * key);


/** \brief Get parameters for editing metadata
 *  \param m Metadata
 *  \returns A NULL-terminated array of parameter descriptions
 *
 *  Using this function and \ref bg_metadata_set_parameter lets
 *  you set metadata with the usual configuration methods.
 *  The default values of the returned descriptions are set from
 *  the Metadata.
 *
 *  Call \ref bg_parameter_info_destroy_array to free the returned array
 */

bg_parameter_info_t * bg_metadata_get_parameters(const gavl_dictionary_t * m);

/** \brief Get parameters for editing metadata
 *  \param m Metadata
 *  \returns A NULL-terminated array of parameter descriptions
 *
 *  This function works exactly like \ref bg_metadata_get_parameters
 *  but it returns only the parameters suitable for mass tagging.
 *  Using this function and \ref bg_metadata_set_parameter lets
 *  you set metadata with the usual configuration methods.
 *  The default values of the returned descriptions are set from
 *  the Metadata.
 *
 *  Call \ref bg_parameter_info_destroy_array to free the returned array
 */

bg_parameter_info_t * bg_metadata_get_parameters_common(const gavl_dictionary_t * m);


/** \brief Change metadata by setting parameters
 *  \param data Metadata casted to void
 *  \param name Name of the parameter
 *  \param v Value
 */

void bg_metadata_set_parameter(void * data, const char * name,
                               const gavl_value_t * v);

/** \brief Set default chapter names
 *  \param list A chapter list
 *
 *  If no names for the chapters are avaiable, this function will
 *  set them to "Chapter 1", "Chapter 2" etc.
 */

void bg_chapter_list_set_default_names(gavl_chapter_list_t * list);

/** \brief Track info
 */

// typedef gavl_dictionary_t bg_track_info_t;

/** \brief Free all allocated memory in a track info
 *  \param info Track info
 *
 *  This one can be called by plugins to free
 *  all allocated memory contained in a track info.
 *  Note, that you have to free() the structure
 *  itself after.
 */

// void bg_track_info_free(bg_track_info_t * info);

/** \brief Set the track name from the filename/URL
 *  \param info Track info
 *  \param location filename or URL
 *
 *  This is used for cases, where the input plugin didn't set a track name,
 *  and the name cannot (or shouldn't) be set from the metadata.
 *  If location is an URL, the whole URL will be copied into the name field.
 *  If location is a local filename, the path and extension will be removed.
 */

void bg_set_track_name_default(gavl_dictionary_t * info,
                               const char * location);

/** \brief Get a track name from the filename/URL
 *  \param location filename or URL
 *  \returns A newly allocated track name which must be freed by the caller
 *  \param track Track index
 *  \param num_tracks Total number of tracks of the location
 *
 *  If location is an URL, the whole URL will be copied into the name field.
 *  If location is a local filename, the path and extension will be removed.
 */

char * bg_get_track_name_default(const char * location);

/** \brief Get the duration of a track
 *  \param info A track info
 *  \returns The approximate duration or GAVL_TIME_UNDEFINED
 */

const char * bg_get_type_icon(const char * media_class);
const char * bg_dictionary_get_id(gavl_dictionary_t * m);

typedef int (*bg_test_child_func)(const gavl_dictionary_t * dict, void * data);

void bg_dictionary_delete_children(const gavl_dictionary_t * container,
                                   bg_test_child_func func,
                                   void * data, bg_msg_sink_t * sink, int msg_id);

void bg_dictionary_delete_children_by_flag(const gavl_dictionary_t * container,
                                           const char * flag,
                                           bg_msg_sink_t * sink, int msg_id);

gavl_array_t * bg_dictionary_extract_children(const gavl_dictionary_t * container,
                                              bg_test_child_func func,
                                              void * data);

gavl_array_t * bg_dictionary_extract_children_by_flag(const gavl_dictionary_t * container,
                                                      const char * flag);


void bg_msg_set_splice_children(gavl_msg_t * msg, int msg_id, const char * album_id,
                                int last, int idx, int del, const gavl_value_t * add);

void bg_dictionary_delete_children_nc(gavl_dictionary_t * container,
                                      bg_test_child_func func,
                                      void * data);

void bg_dictionary_delete_children_by_flag_nc(gavl_dictionary_t * container,
                                              const char * flag);


#define BG_TRACK_ENCODER         "encoder"

#define bg_track_get_cfg_encoder_nc(dict) gavl_dictionary_get_dictionary_create(dict, BG_TRACK_ENCODER)
#define bg_track_get_cfg_encoder(dict)    gavl_dictionary_get_dictionary(dict, BG_TRACK_ENCODER)

#define bg_stream_get_cfg_encoder_nc(dict) gavl_dictionary_get_dictionary_create(dict, BG_TRACK_ENCODER)
#define bg_stream_get_cfg_encoder(dict)    gavl_dictionary_get_dictionary(dict, BG_TRACK_ENCODER)

/**
 *  @}
 */


#endif // /__BG_STREAMINFO_H_
