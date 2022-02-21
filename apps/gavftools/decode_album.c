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

#include "gavf-decode.h"
#include <string.h>
#include <errno.h>

#include <gmerlin/http.h>
#include <gmerlin/xmlutils.h>
#include <gmerlin/xspf.h>

/* Stat */

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <ctype.h>

#define LOG_DOMAIN "gavf-decode.album"

void album_init(album_t * a)
  {
  memset(a, 0, sizeof(*a));
  }

void album_free(album_t * a)
  {
  if(a->entries)
    free(a->entries);
  if(a->first)
    bg_album_entries_destroy(a->first);

  bg_mediaconnector_free(&a->in_conn);
  bg_mediaconnector_free(&a->out_conn);
  if(a->h)
    bg_plugin_unref(a->h);
  gavl_dictionary_free(&a->m);
  }


static bg_album_entry_t * load_gmerlin_album(const char * filename)
  {
  bg_album_entry_t * ret;
  gavl_buffer_t buf;
  gavl_buffer_init(&buf);
  
  if(!bg_read_file(filename, &buf))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Album file %s could not be opened",
           album_file);
    return NULL;
    }

  ret = bg_album_entries_new_from_xml((char*)buf.buf);
  gavl_buffer_free(&buf);
  return ret;
  }

static bg_album_entry_t * load_m3u(const char * filename)
  {
  int seconds;
  int idx;
  char * pos;
  char * m3u;
  char ** lines;
  bg_album_entry_t * ret = NULL;
  bg_album_entry_t * end = NULL;
  bg_album_entry_t * new_entry;

  gavl_buffer_t buf;
  gavl_buffer_init(&buf);
  
  if(!bg_read_location(filename,
                       &buf, 0, 0, NULL))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Playlist file %s could not be opened",
           filename);
    return NULL;
    }

  m3u = (char*)buf.buf;
  
  lines = gavl_strbreak(m3u, '\n');

  idx = 0;
  while(lines[idx])
    {
    if((pos = strchr(lines[idx], '\r')))
      *pos = '\0';
    idx++;
    }

  idx = 0;

  /* Create entries */
  while(lines[idx])
    {
    if(!strncmp(lines[idx], "#EXTM3U", 7))
      {
      idx++;
      continue;
      }

    /* Create entry */

    if(!strncmp(lines[idx], "#EXTINF:", 8))
      {
      new_entry = calloc(1, sizeof(*new_entry));
      
      /* Get duration (approximate) */
      pos = lines[idx] + 8;
      seconds = atoi(lines[idx]);
      if(seconds > 0)
        gavl_dictionary_set_long(&new_entry->m, GAVL_META_APPROX_DURATION,
                                 (int64_t)seconds * GAVL_TIME_SCALE);
      
      pos = strchr(pos, ',');
      if(pos)
        {
        pos++;
        while(isspace(*pos) && (*pos != '\0'))
          pos++;
        if(*pos != '\0')
          gavl_dictionary_set_string(&new_entry->m, GAVL_META_LABEL, pos);
        }
      idx++;
      }
    else if(*(lines[idx]) != '#')
      {
      if(!new_entry)
        new_entry = calloc(1, sizeof(*new_entry));

      gavl_metadata_add_src(&new_entry->m, GAVL_META_SRC,
                            NULL,
                            lines[idx]);
      
      if(!end)
        {
        ret = new_entry;
        end = ret;
        }
      else
        {
        end->next = new_entry;
        end = end->next;
        }
      idx++;
      new_entry = NULL;
      }
    else
      idx++;
    }
  
  gavl_strbreak_free(lines);
  gavl_buffer_free(&buf);
  
  return ret;
  }

static bg_album_entry_t * load_xspf(const char * filename)
  {
  xmlDocPtr doc;
  bg_album_entry_t * ret = NULL;
  bg_album_entry_t * end = NULL;
  bg_album_entry_t * new_entry;
  char * mimetype = NULL;
  xmlNodePtr track;
  gavl_dictionary_t m;
    
  
  if(!strncasecmp(filename, "http://", 7))
    doc = bg_xml_from_url(filename, &mimetype);
  else
    doc = bg_xml_parse_file(filename, 0);
  
  if(mimetype)
    free(mimetype);
  
  if(!doc)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Playlist file %s could not be opened",
           filename);
    return NULL;
    }

  track = NULL;

  gavl_dictionary_init(&m);
  
  while((track = bg_xspf_get_track(doc, track, &m)))
    {
    new_entry = calloc(1, sizeof(*new_entry));
    gavl_dictionary_copy(&new_entry->m, &m);
    
    if(!end)
      {
      ret = new_entry;
      end = ret;
      }
    else
      {
      end->next = new_entry;
      end = end->next;
      }
    new_entry = NULL;
    
    gavl_dictionary_free(&m);
    gavl_dictionary_init(&m);
    }
   
  xmlFreeDoc(doc);
  return ret;
  
  }



static int album_load(album_t * a)
  {
  int i;
  bg_album_entry_t * e;

  if(album_file)
    a->first = load_gmerlin_album(album_file);
  else if(m3u_file)
    a->first = load_m3u(m3u_file);
  else if(xspf_file)
    a->first = load_xspf(xspf_file);
  
  if(!a->first)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Album file %s contains no entries",
           album_file);
    return 0;
    }

  /* Count entries */
  e = a->first;
  while(e)
    {
    a->num_entries++;
    e = e->next;
    }

  /* Set up array */
  a->entries = calloc(a->num_entries, sizeof(a->entries));
  e = a->first;

  for(i = 0; i < a->num_entries; i++)
    {
    a->entries[i] = e;
    e = e->next;
    }

  /* Shuffle */
  if(shuffle)
    {
    int idx;
    for(i = 0; i < a->num_entries; i++)
      {
      idx = rand() % a->num_entries;
      e = a->entries[i];
      a->entries[i] = a->entries[idx];
      a->entries[idx] = e;
      }
    }
  return 1;
  }

static bg_album_entry_t * album_next(album_t * a)
  {
  bg_album_entry_t * ret;
  
  if(a->current_entry == a->num_entries)
    {
    if(!loop)
      return NULL;
    else a->current_entry = 0;
    }
  
  ret = a->entries[a->current_entry];
  
  a->current_entry++;
  return ret;
  }

static void create_streams(album_t * a, gavl_stream_type_t type)
  {
  int i, num;
  num = bg_mediaconnector_get_num_streams(&a->in_conn, type);
  for(i = 0; i < num; i++)
    stream_create(bg_mediaconnector_get_stream(&a->in_conn,
                                                type, i), a);
  }

int init_decode_album(album_t * a)
  {
  int ret = 0;
  const char * location;
  bg_album_entry_t * e;

  if(!album_load(a))
    return ret;
  
  e = album_next(a);

  if(!load_album_entry(e, &a->in_conn, &a->h, &a->m))
    return ret;

  /* Set up the conn2 from conn1 */
  a->num_streams = a->in_conn.num_streams;

  create_streams(a, GAVL_STREAM_AUDIO);
  create_streams(a, GAVL_STREAM_VIDEO);
  create_streams(a, GAVL_STREAM_TEXT);
  create_streams(a, GAVL_STREAM_OVERLAY);

  a->active_streams = a->num_streams;

  gavl_dictionary_get_src(&e->m, GAVL_META_SRC, 0, NULL, &location);
  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Loaded %s", location);
  
  ret = 1;
  return ret;
  }

static int match_streams(album_t * a, const char *location,
                         gavl_stream_type_t type)
  {
  if(bg_mediaconnector_get_num_streams(&a->in_conn, type) !=
     bg_mediaconnector_get_num_streams(&a->out_conn, type))
    {
    gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN,
           "Skipping %s (number of %s stream doesn't match)",
           location, gavf_stream_type_name(type));
    return 0;
    }
  return 1;
  }

static int replug_streams(album_t * a, gavl_stream_type_t type)
  {
  int i, num;
  bg_mediaconnector_stream_t * is, * os;
  
  num = bg_mediaconnector_get_num_streams(&a->in_conn, type);
  for(i = 0; i < num; i++)
    {
    is = bg_mediaconnector_get_stream(&a->in_conn, type, i);
    os = bg_mediaconnector_get_stream(&a->out_conn, type, i);
    if(!stream_replug(os->priv, is))
      return 0;
    }
  return 1;
  }


int album_set_eof(album_t * a)
  {
  int i;
  bg_album_entry_t * e;
  gavl_time_t test_time;
  stream_t * s;
  gavf_t * g;
  const char * location = NULL;
  
  a->active_streams--;

  if(a->active_streams > 0)
    return 0;

  /* Get end time */

  for(i = 0; i < a->num_streams; i++)
    {
    if((a->out_conn.streams[i]->type == GAVL_STREAM_AUDIO) ||
       (a->out_conn.streams[i]->type == GAVL_STREAM_VIDEO))
      {
      s = a->out_conn.streams[i]->priv;

      test_time = gavl_time_unscale(s->out_scale, s->pts);
      if(a->end_time < test_time)
        a->end_time = test_time;
      }       
    }
  
  while(1)
    {
    gavl_dictionary_free(&a->m);
    gavl_dictionary_init(&a->m);
    
    bg_mediaconnector_free(&a->in_conn);
    bg_mediaconnector_init(&a->in_conn);
    if(a->h)
      {
      bg_plugin_unref(a->h);
      a->h = NULL;
      }
    
    e = album_next(a);
    if(!e)
      {
      a->eof = 1;
      return 0;
      }
    
    /* Load next track */
    gavl_dictionary_get_src(&e->m, GAVL_META_SRC, 0, NULL, &location);
    
    if(!load_album_entry(e, &a->in_conn,
                         &a->h, &a->m))
      {
      gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Skipping %s", location);
      continue;
      }
    
    if(!match_streams(a, location, GAVL_STREAM_AUDIO) ||
       !match_streams(a, location, GAVL_STREAM_VIDEO) ||
       !match_streams(a, location, GAVL_STREAM_TEXT) ||
       !match_streams(a, location, GAVL_STREAM_OVERLAY))
      continue;

    /* replug streams */
    if(!replug_streams(a, GAVL_STREAM_AUDIO) ||
       !replug_streams(a, GAVL_STREAM_VIDEO) ||
       !replug_streams(a, GAVL_STREAM_TEXT) ||
       !replug_streams(a, GAVL_STREAM_OVERLAY))
      continue;
    
    break;
    }

  g = bg_plug_get_gavf(a->out_plug);

  gavl_metadata_delete_compression_fields(&a->m);
  gavf_update_metadata(g, &a->m);

  a->active_streams = a->num_streams;
    
  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Loaded %s",
         location);
  
  return 1;
  }
