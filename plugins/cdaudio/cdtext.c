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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <cdio/cdio.h>
#include <cdio/cdtext.h>
/* Stuff defined by cdio includes */
#undef PACKAGE
#undef PACKAGE_BUGREPORT
#undef PACKAGE_NAME
#undef PACKAGE_STRING
#undef PACKAGE_TARNAME
#undef PACKAGE_VERSION
#undef VERSION


#include <gmerlin/utils.h>


#include <gavl/metatags.h>


#include "cdaudio.h"

#define GET_FIELD(dst, key, track)                    \
  field = cdtext_get_const(cdtext, key, track);

int bg_cdaudio_get_metadata_cdtext(CdIo_t * cdio,
                                   gavl_dictionary_t * info,
                                   bg_cdaudio_index_t* idx)
  {
  int i;
  const char * field;
    
  /* Global information */

  const char * title  = NULL;
  const char * artist  = NULL;
  const char * author  = NULL;
  const char * album   = NULL;
  const char * genre   = NULL;
  const char * comment = NULL;
  const cdtext_t * cdtext;

  gavl_dictionary_t * m;
  
  /* Get information for the whole disc */
  
  cdtext = cdio_get_cdtext (cdio);

  if(!cdtext)
    return 0;
  
  artist  = cdtext_get_const(cdtext, CDTEXT_FIELD_PERFORMER, 0);
  author  = cdtext_get_const(cdtext, CDTEXT_FIELD_COMPOSER, 0); /* Composer overwrites songwriter */

  if(!author)
    author  = cdtext_get_const(cdtext, CDTEXT_FIELD_SONGWRITER, 0);
  
  album  = cdtext_get_const(cdtext, CDTEXT_FIELD_TITLE, 0);
  genre  = cdtext_get_const(cdtext, CDTEXT_FIELD_GENRE, 0);
  comment  = cdtext_get_const(cdtext, CDTEXT_FIELD_MESSAGE, 0);
  
  for(i = 0; i < idx->num_tracks; i++)
    {
    if(idx->tracks[i].is_audio)
      {
      
      GET_FIELD(title, CDTEXT_FIELD_TITLE, i+1);
      
      if(!title)
        return 0;

      m = gavl_track_get_metadata_nc(&info[idx->tracks[i].index]);
      
      gavl_dictionary_set_string(m, GAVL_META_TITLE, title);

      if((field = cdtext_get_const(cdtext, CDTEXT_FIELD_PERFORMER, i+1)))
        gavl_dictionary_set_string(m, GAVL_META_ARTIST, field);
      else
        gavl_dictionary_set_string(m, GAVL_META_ARTIST, artist);


      if((field = cdtext_get_const(cdtext, CDTEXT_FIELD_COMPOSER, i+1)))
        gavl_dictionary_set_string(m, GAVL_META_AUTHOR, field);
      else if((field = cdtext_get_const(cdtext, CDTEXT_FIELD_SONGWRITER, i+1)))
        gavl_dictionary_set_string(m, GAVL_META_AUTHOR, field);
      else if(author)
        gavl_dictionary_set_string(m, GAVL_META_AUTHOR, author);
      
      
      if((field = cdtext_get_const(cdtext, CDTEXT_FIELD_GENRE, i+1)))
        gavl_dictionary_set_string(m, GAVL_META_GENRE, field);
      else
        gavl_dictionary_set_string(m, GAVL_META_GENRE, genre);
      
      if((field = cdtext_get_const(cdtext, CDTEXT_FIELD_MESSAGE, i+1)))
        gavl_dictionary_set_string(m, GAVL_META_COMMENT, field);
      else
        gavl_dictionary_set_string(m, GAVL_META_COMMENT, comment);
      
      
      gavl_dictionary_set_string(m, GAVL_META_ALBUM, album);
      gavl_dictionary_set_string(m, GAVL_META_ALBUMARTIST, artist);
      }
    }
  return 1;
  
  }

#undef GET_FIELD

