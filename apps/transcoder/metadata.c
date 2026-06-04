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
#include <gmerlin/translation.h>
#include <gavl/gavl.h>
#include <gavl/utils.h>

#include "app.h"

#define PARAM_LABEL \
    { \
      .name =      GAVL_META_LABEL,       \
      .long_name = TRS("Label"), \
      .type =      BG_PARAMETER_STRING, \
      .help_string = TRS("Used for generation of output file names"),   \
    }

#define PARAM_ARTIST \
    { \
      .name =      GAVL_META_ARTIST, \
      .long_name = TRS("Artist"), \
      .type =      BG_PARAMETER_STRING, \
      .help_string = TRS("Separate multiple entries by semicolons"),   \
    }

#define PARAM_ACTOR                            \
    { \
      .name =      GAVL_META_ACTOR, \
      .long_name = TRS("Actor"), \
      .type =      BG_PARAMETER_STRING, \
      .help_string = TRS("Separate multiple entries by semicolons"),   \
    }

#define PARAM_DIRECTOR                          \
    { \
      .name =      GAVL_META_DIRECTOR, \
      .long_name = TRS("Director"), \
      .type =      BG_PARAMETER_STRING, \
      .help_string = TRS("Separate multiple entries by semicolons"),   \
    }

#define PARAM_ALBUMARTIST \
    { \
      .name =      GAVL_META_ALBUMARTIST, \
      .long_name = TRS("Album artist"), \
      .type =      BG_PARAMETER_STRING, \
      .help_string = TRS("Separate multiple entries by semicolons"),   \
    }

#define PARAM_TITLE \
    { \
      .name =      GAVL_META_TITLE, \
      .long_name = TRS("Title"), \
      .type =      BG_PARAMETER_STRING, \
    }

#define PARAM_ALBUM \
    { \
      .name =      GAVL_META_ALBUM, \
      .long_name = TRS("Album"), \
      .type =      BG_PARAMETER_STRING, \
    }

#define PARAM_TRACK \
    { \
      .name =      GAVL_META_TRACKNUMBER, \
      .long_name = TRS("Track"), \
      .type =      BG_PARAMETER_INT, \
    }

#define PARAM_GENRE \
    { \
      .name =      GAVL_META_GENRE, \
      .long_name = TRS("Genre"), \
      .type =      BG_PARAMETER_STRING, \
      .help_string = TRS("Separate multiple entries by semicolons"),   \
    }

#define PARAM_AUTHOR \
    { \
      .name =      GAVL_META_AUTHOR, \
      .long_name = TRS("Author"), \
      .type =      BG_PARAMETER_STRING, \
      .help_string = TRS("Separate multiple entries by semicolons"),   \
    }

#define PARAM_COPYRIGHT \
    { \
      .name =      GAVL_META_COPYRIGHT, \
      .long_name = TRS("Copyright"), \
      .type =      BG_PARAMETER_STRING, \
    }

#define PARAM_YEAR \
    { \
      .name =        GAVL_META_YEAR, \
      .long_name =   TRS("Year"), \
      .type =        BG_PARAMETER_STRING, \
    }

#define PARAM_DATE                              \
    { \
      .name =        GAVL_META_DATE, \
      .long_name =   TRS("Date"), \
      .type =        BG_PARAMETER_STRING, \
    }

#define PARAM_COMMENT \
    { \
      .name =      GAVL_META_COMMENT, \
      .long_name = TRS("Comment"), \
      .type =      BG_PARAMETER_STRING, \
    }

#define PARAM_DESCRIPTION               \
    { \
      .name =      GAVL_META_DESCRIPTION, \
      .long_name = TRS("Description"), \
      .type =      BG_PARAMETER_STRING_MULTILINE \
    }

const gavl_parameter_info_t metadata_podcastepisode_parameters[] =
  {
    PARAM_LABEL,
    PARAM_TITLE,
    PARAM_GENRE,
    PARAM_DATE,
    PARAM_AUTHOR,
    PARAM_ARTIST,
    PARAM_DESCRIPTION,
    { /* End of parameters */ }
  };

const gavl_parameter_info_t metadata_audio_parameters[] =
  {
    PARAM_LABEL,
    PARAM_ARTIST,
    PARAM_TITLE,
    PARAM_ALBUM,
    PARAM_TRACK,
    PARAM_GENRE,
    PARAM_AUTHOR,
    PARAM_ALBUMARTIST,
    PARAM_COPYRIGHT,
    PARAM_YEAR,
    PARAM_COMMENT,
    { /* End of parameters */ }
  };

const gavl_parameter_info_t metadata_video_parameters[] =
  {
    PARAM_LABEL,
    PARAM_TITLE,
    PARAM_GENRE,
    PARAM_DIRECTOR,
    PARAM_ACTOR,
    PARAM_DATE,
    PARAM_DESCRIPTION,
    { /* End of parameters */ }
  };

const gavl_parameter_info_t metadata_generic_parameters[] =
  {
    PARAM_TITLE,
    PARAM_COMMENT,
    PARAM_DATE,
    { /* End of parameters */ }
    
  };


const gavl_parameter_info_t * get_metadata_parameters(const char * klass)
  {
  // if(gavl_string_starts_with(klass, "item.audio"))

  if(!strcmp(klass, GAVL_META_CLASS_AUDIO_PODCAST_EPISODE) ||
     !strcmp(klass, GAVL_META_CLASS_VIDEO_PODCAST_EPISODE))
    return metadata_podcastepisode_parameters;
  if(gavl_string_starts_with(klass, "item.audio"))
    return metadata_audio_parameters;
  else if(gavl_string_starts_with(klass, "item.video"))
    return metadata_video_parameters;
  else
    return metadata_generic_parameters;
  }

const gavl_parameter_info_t metadata_bulk_parameters[] =
  {
    PARAM_ARTIST,
    PARAM_ALBUM,
    PARAM_GENRE,
    PARAM_AUTHOR,
    PARAM_ALBUMARTIST,
    PARAM_COPYRIGHT,
    PARAM_YEAR,
    PARAM_COMMENT,
    { /* End of parameters */ }
  };
