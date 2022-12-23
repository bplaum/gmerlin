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

#include <string.h>

#include <gavl/gavl.h>
#include <gavl/value.h>
#include <gavl/trackinfo.h>
#include <gavl/metatags.h>
#include <gavl/utils.h>

#include <gmerlin/bggavl.h>
#include <gmerlin/xmlutils.h>
#include <gmerlin/utils.h>
#include <gmerlin/httpserver.h>

#define GMERLIN_TRACK_ROOT "GMERLIN_TRACKS"

/*
 *   Text based I/O routines for tracks, used for drag&drop, copy & paste and
 *   as transfer formats
 */

static const char * get_uri(const gavl_dictionary_t * m, int local)
  {

  /* TODO: kick out lpcm uris, which might come first */
  // int bg_is_http_media_uri(const char * uri)
  
  const char * location = NULL;
  if(local)
    {
    if(!gavl_metadata_get_src(m, GAVL_META_SRC, 0, NULL, &location))
      return NULL;
    return location;
    }
  else
    {
    int idx = 0;
    
    while(gavl_metadata_get_src(m, GAVL_META_SRC, idx, NULL, &location))
      {
      if(gavl_string_starts_with(location, "http://"))
        {
        if(!idx || bg_is_http_media_uri(location))
          return location;
        }
      idx++;
      }
    }
  return NULL;
  }

static char * write_gmerlin(const gavl_dictionary_t * dict)
  {
  return  bg_dictionary_save_xml_string(dict, GMERLIN_TRACK_ROOT);
  }

static int read_gmerlin(gavl_dictionary_t * dict, const char * str, int len)
  {
  return bg_dictionary_load_xml_string(dict, str, len, GMERLIN_TRACK_ROOT);
  }

static char * write_xspf(const gavl_dictionary_t * dict, int local)
  {
  int i, num_tracks;
  char * ret;
  xmlDocPtr  xml_doc;
  xmlNsPtr ns;
  xmlNodePtr root;
  xmlNodePtr tracklist;
  
  xml_doc = xmlNewDoc((xmlChar*)"1.0");

  root = xmlNewDocRawNode(xml_doc, NULL, (xmlChar*)"playlist", NULL);
  xmlDocSetRootElement(xml_doc, root);

  ns =
    xmlNewNs(root,
             (xmlChar*)"http://xspf.org/ns/0/",
             NULL);
  xmlSetNs(root, ns);
  xmlAddChild(root, BG_XML_NEW_TEXT("\n"));

  BG_XML_SET_PROP(root, "version", "1");

  tracklist = bg_xml_append_child_node(root, "trackList", NULL);

  num_tracks = gavl_get_num_tracks(dict);
  
  for(i = 0; i < num_tracks; i++)
    {
    char * tmp_string;
    gavl_time_t duration;
    const char * val;
    const char * location;
    xmlNodePtr track;
    const gavl_dictionary_t * m;
    const gavl_dictionary_t * child;
    int val_i;
    
    if(!(child = gavl_get_track(dict, i)) ||
       !(m = gavl_track_get_metadata(child)) ||
       !(val = gavl_dictionary_get_string(m, GAVL_META_MEDIA_CLASS)) ||
       gavl_string_starts_with(val, "container"))
      continue;

    track = bg_xml_append_child_node(tracklist, "track", NULL);
    
    /* title */
    if((val = gavl_dictionary_get_string(m, GAVL_META_TITLE)) ||
       (val = gavl_dictionary_get_string(m, GAVL_META_LABEL)))
      bg_xml_append_child_node(track, "title", val);

    /* creator */
    if((val = gavl_dictionary_get_string(m, GAVL_META_ARTIST)))
      bg_xml_append_child_node(track, "creator", val);

    /* album */
    if((val = gavl_dictionary_get_string(m, GAVL_META_ALBUM)))
      bg_xml_append_child_node(track, "album", val);

    /* annotation */
    if((val = gavl_dictionary_get_string(m, GAVL_META_COMMENT)))
      bg_xml_append_child_node(track, "annotation", val);
    
    /* info */
    if((val = gavl_dictionary_get_string(m, GAVL_META_STATION_URL)))
      bg_xml_append_child_node(track, "info", val);

    /* image */
    if((val = gavl_dictionary_get_string_image_uri(m, GAVL_META_COVER_URL, 0, NULL, NULL, NULL)) ||
       (val = gavl_dictionary_get_string_image_uri(m, GAVL_META_POSTER_URL, 0, NULL, NULL, NULL)))
      {
      tmp_string = bg_string_to_uri(val, -1);
      bg_xml_append_child_node(track, "image", tmp_string);
      free(tmp_string);
      }

    /* trackNum */
    if(gavl_dictionary_get_int(m, GAVL_META_TRACKNUMBER, &val_i))
      {
      char * tmp_string = bg_sprintf("%d", val_i);
      bg_xml_append_child_node(track, "trackNum", tmp_string);
      free(tmp_string);
      }
    
    /* duration */
    if(gavl_dictionary_get_long(m, GAVL_META_APPROX_DURATION, &duration) && (duration > 0))
      {
      char * tmp_string = bg_sprintf("%"PRId64, duration / (GAVL_TIME_SCALE/1000));
      bg_xml_append_child_node(track, "duration", tmp_string);
      free(tmp_string);
      }

    /* location */
    if((location = get_uri(m, local)))
      {
      tmp_string = bg_string_to_uri(location, -1);
      bg_xml_append_child_node(track, "location", tmp_string);
      free(tmp_string);
      }
    
    }
  
  ret = bg_xml_save_to_memory(xml_doc);
  xmlFreeDoc(xml_doc);
  return ret;
  }

static int read_xspf(gavl_dictionary_t * dict, const char * str, int len)
  {
  return 0;
  
  }

static char * write_m3u(const gavl_dictionary_t * dict, int local)
  {
  char * ret;
  int i, num_tracks;
  char * tmp_string;
  
  ret = gavl_strdup("#EXTM3U\n");

  num_tracks = gavl_get_num_tracks(dict);
  
  for(i = 0; i < num_tracks; i++)
    {
    gavl_time_t duration;
    const char * val;
    const char * location;
    const gavl_dictionary_t * m;
    const gavl_dictionary_t * child;

    if(!(child = gavl_get_track(dict, i)) ||
       !(m = gavl_track_get_metadata(child)) ||
       !(val = gavl_dictionary_get_string(m, GAVL_META_MEDIA_CLASS)) ||
       gavl_string_starts_with(val, "container"))
      continue;

    if(!(val = gavl_dictionary_get_string(m, GAVL_META_TITLE)) &&
       !(val = gavl_dictionary_get_string(m, GAVL_META_LABEL)))
      continue;

    if(!(location = get_uri(m, local)))
      continue;
    
    if(gavl_dictionary_get_long(m, GAVL_META_APPROX_DURATION, &duration) && (duration > 0))
      duration /= GAVL_TIME_SCALE; // Full seconds
    else
      duration = -1;
    
    tmp_string = bg_sprintf("#EXTINF:%"PRId64",%s\n%s\n", duration, val, location);
    ret = gavl_strcat(ret, tmp_string);

    free(tmp_string);
    }
  
  return ret;
  }

static void init_item(gavl_value_t * val)
  {
  gavl_dictionary_t * dict;
  gavl_dictionary_t * m;

  dict = gavl_value_set_dictionary(val);
  m = gavl_dictionary_get_dictionary_create(dict, GAVL_META_METADATA);
  gavl_dictionary_set_string(m, GAVL_META_MEDIA_CLASS, GAVL_META_MEDIA_CLASS_LOCATION);
  }

static int read_m3u(gavl_dictionary_t * ret, const char * str, int len)
  {
  int i = 0;
  char ** lines;
  char * pos;
  char * end;

  gavl_value_t val;
  gavl_dictionary_t * dict;
  gavl_dictionary_t * m;
  
  lines =  gavl_strbreak(str, '\n');

  /* Remove DOS linebreaks */
  i = 0;
  while(lines[i])
    {
    if((pos = strchr(lines[i], '\r')))
      *pos = '\0';
    i++;
    }

  i = 0;

  gavl_value_init(&val);
  dict = NULL;
  m = NULL;
  
  while(lines[i])
    {
    if(gavl_string_starts_with(lines[i], "#EXTINF:"))
      {
      int64_t seconds;
      
      if(dict)
        gavl_value_reset(&val);
      
      init_item(&val);
      dict = gavl_value_get_dictionary_nc(&val);
      m = gavl_dictionary_get_dictionary_nc(dict, GAVL_META_METADATA);
      
      pos = lines[i] + 8;

      seconds = strtoll(pos, &end, 10);

      if(end > pos)
        gavl_dictionary_set_long(m, GAVL_META_APPROX_DURATION, seconds * GAVL_TIME_SCALE);

      if((pos = strchr(end, ',')))
        {
        pos++;
        gavl_dictionary_set_string(m, GAVL_META_LABEL, pos);
        }
      }
    else if(*(lines[i]) != '#')
      {
      if(!dict)
        init_item(&val);

      dict = gavl_value_get_dictionary_nc(&val);
      m = gavl_dictionary_get_dictionary_nc(dict, GAVL_META_METADATA);
      
      gavl_metadata_add_src(m, GAVL_META_SRC, NULL, lines[i]);
      gavl_track_splice_children_nocopy(ret, -1, 0, &val);
      
      dict = NULL;
      m    = NULL;
      }
    i++;
    }
  gavl_strbreak_free(lines);
  return 1;
  }

static char * write_pls(const gavl_dictionary_t * dict, int local)
  {
  char * ret;
  int i, num_tracks;
  char * tmp_string;
  int idx = 0;
  
  ret = gavl_strdup("[playlist]\n");

  num_tracks = gavl_get_num_tracks(dict);
  
  for(i = 0; i < num_tracks; i++)
    {
    gavl_time_t duration;
    const char * val;
    const char * location;
    const gavl_dictionary_t * m;
    const gavl_dictionary_t * child;

    if(!(child = gavl_get_track(dict, i)) ||
       !(m = gavl_track_get_metadata(child)) ||
       !(val = gavl_dictionary_get_string(m, GAVL_META_MEDIA_CLASS)) ||
       gavl_string_starts_with(val, "container"))
      continue;

    if(!(val = gavl_dictionary_get_string(m, GAVL_META_TITLE)) &&
       !(val = gavl_dictionary_get_string(m, GAVL_META_LABEL)))
      continue;
    
    if(!(location = get_uri(m, local)))
      continue;
    
    if(gavl_dictionary_get_long(m, GAVL_META_APPROX_DURATION, &duration) && (duration > 0))
      duration /= GAVL_TIME_SCALE; // Full seconds
    else
      duration = -1;

    idx++;
    tmp_string = bg_sprintf("File%d=%s\r\nTitle%d=%s\r\nLength%d=%"PRId64"\r\n",
                            idx, location,
                            idx, val,
                            idx, duration);
    
    ret = gavl_strcat(ret, tmp_string);

    free(tmp_string);
    }

  tmp_string = bg_sprintf("NumberOfEntries=%d\nVersion=2\r\n", idx);
  ret = gavl_strcat(ret, tmp_string);
  free(tmp_string);
  
  return ret;
  }

static int read_pls(gavl_dictionary_t * dict, const char * str, int len)
  {
  return 0;
  
  }

static char * write_urilist(const gavl_dictionary_t * dict, int local)
  {
  char * ret;
  int i, num_tracks;
  char * tmp_string;
  int idx = 0;
  
  ret = NULL;

  num_tracks = gavl_get_num_tracks(dict);
  
  for(i = 0; i < num_tracks; i++)
    {
    const char * location;
    const char * val;
    const gavl_dictionary_t * m;
    const gavl_dictionary_t * child;

    if(!(child = gavl_get_track(dict, i)) ||
       !(m = gavl_track_get_metadata(child)) ||
       !(val = gavl_dictionary_get_string(m, GAVL_META_MEDIA_CLASS)) ||
       gavl_string_starts_with(val, "container"))
      continue;
    
    if(!(location = get_uri(m, local)))
      continue;
    
    idx++;
    tmp_string = bg_string_to_uri(location, -1);
    ret = gavl_strcat(ret, tmp_string);
    free(tmp_string);

    ret = gavl_strcat(ret, "\r\n");
    }
  //  fprintf(stderr, "write urilist\n%s\n", ret);
  return ret;
  }

static int read_urilist(gavl_dictionary_t * dict, const char * str, int len)
  {
  return 0;
  }

char * bg_tracks_to_string(const gavl_dictionary_t * dict, int format, int local)
  {
  //  fprintf(stderr, "bg_tracks_to_string %d\n", format);
  //  gavl_dictionary_dump(dict, 2);
  //  fprintf(stderr, "\n");
  
  switch(format)
    {
    case BG_TRACK_FORMAT_GMERLIN:
      return write_gmerlin(dict);
      break;
    case BG_TRACK_FORMAT_XSPF:
      return write_xspf(dict, local);
      break;
    case BG_TRACK_FORMAT_M3U:
      return write_m3u(dict, local);
      break;
    case BG_TRACK_FORMAT_PLS:
      return write_pls(dict, local);
      break;
    case BG_TRACK_FORMAT_URILIST:
      return write_urilist(dict, local);
      break;
    }
  return NULL;
  }

int bg_tracks_from_string(gavl_dictionary_t * dict, int format, const char * str, int len)
  {
  if(len < 0)
    len = strlen(str);

  switch(format)
    {
    case BG_TRACK_FORMAT_GMERLIN:
      return read_gmerlin(dict, str, len);
      break;
    case BG_TRACK_FORMAT_XSPF:
      return read_xspf(dict, str, len);
      break;
    case BG_TRACK_FORMAT_M3U:
      return read_m3u(dict, str, len);
      break;
    case BG_TRACK_FORMAT_PLS:
      return read_pls(dict, str, len);
      break;
    case BG_TRACK_FORMAT_URILIST:
      return read_urilist(dict, str, len);
      break;
    }
  return 0;
  }
