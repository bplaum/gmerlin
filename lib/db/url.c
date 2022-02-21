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

#include <config.h>

#include <string.h>


#include <mediadb_private.h>
#include <gmerlin/log.h>
#include <gmerlin/utils.h>
#include <gmerlin/tree.h>

#include <gavl/metatags.h>

#define LOG_DOMAIN "db_url"

#define STREAM_URL_COL    1
#define STATION_URL_COL   2
#define MIMETYPE_COL      3
#define AUDIO_BITRATE_COL 4
#define SAMPLERATE_COL    5
#define CHANNELS_COL      6

static int query_url(bg_db_t * db, void * file1)
  {
  int result;
  bg_db_url_t * f = file1;
  int found = 0;

  sqlite3_stmt * st = db->q_urls;

  sqlite3_bind_int64(st, 1, f->obj.id);
  
  if((result = sqlite3_step(st)) == SQLITE_ROW)
    {
    BG_DB_GET_COL_STRING(STREAM_URL_COL, f->stream_url);
    BG_DB_GET_COL_STRING(STATION_URL_COL, f->station_url);
    BG_DB_GET_COL_INT(MIMETYPE_COL, f->mimetype_id);
    BG_DB_GET_COL_STRING(AUDIO_BITRATE_COL, f->audio_bitrate);
    BG_DB_GET_COL_INT(SAMPLERATE_COL, f->samplerate);
    BG_DB_GET_COL_INT(CHANNELS_COL, f->channels);
    found = 1;
    }
  sqlite3_reset(st);
  sqlite3_clear_bindings(st);
  
  if(!found)
    return 0;
  
  f->mimetype = bg_db_string_cache_get(db->mimetypes, db->db,
                                       f->mimetype_id);
  return 1;
  }

static void del_url(bg_db_t * db, bg_db_object_t * obj) // Delete from db
  {
  bg_sqlite_delete_by_id(db->db, "URLS", obj->id);
  }

static void free_url(void * obj)
  {
  bg_db_url_t * f = obj;
  if(f->stream_url)
    free(f->stream_url);
  if(f->station_url)
    free(f->station_url);
  if(f->mimetype)
    free(f->mimetype);
  if(f->audio_bitrate)
    free(f->audio_bitrate);
  }

const bg_db_object_class_t bg_db_radio_url_class =
  {
    .name = "Radio URL",
    .del = del_url,
    .free = free_url,
    .query = query_url,
    // .update = update_audioalbum,
    // .dump = dump_audioalbum,
    // .get_children = get_children_root,
    .parent = NULL, // Object
  };


void * bg_db_get_radio_url_folder(bg_db_t * db, const char * path1)
  {
  void * ret;
  void * parent = bg_db_rootfolder_get(db, BG_DB_OBJECT_RADIO_URL);
  ret = bg_db_container_get(db, parent, path1);
  bg_db_object_unref(parent);
  return ret;
  }

void bg_db_add_radio_url(bg_db_t * db, void * parent,
                         const char * url, const char * label)
  {
  bg_plugin_handle_t * h = NULL;
  bg_input_plugin_t * plugin = NULL;
  gavl_dictionary_t * ti;
  const gavl_dictionary_t * m;
  bg_db_object_t * obj;
  bg_db_url_t * u;
  const char * str;
  char * sql;
  int bitrate;
  const gavl_audio_format_t * fmt;
  const gavl_dictionary_t * sm;
  const char * mimetype_ptr;
  
  int num_audio_streams;
  int num_video_streams;
  
  /* Open URL and fetch metadata */

  if(!bg_input_plugin_load(db->plugin_reg, url, &h, NULL))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Open URL %s failed", url);
    goto fail;
    }
  plugin = (bg_input_plugin_t *)h->plugin;

  /* Only one track supported */
  if(bg_input_plugin_get_num_tracks(h) < 1)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "URL %s contains no tracks", url);
    goto fail;
    }
  ti = bg_input_plugin_get_track_info(h, 0);


  if(!bg_input_plugin_set_track(h, 0))
    goto fail;
  
 
  num_audio_streams = gavl_track_get_num_audio_streams(ti);
  num_video_streams = gavl_track_get_num_video_streams(ti);
 
  if((num_audio_streams != 1) || (num_video_streams))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "URL %s is no radio stream", url);
    goto fail;
    }

  if(plugin->set_audio_stream)
    plugin->set_audio_stream(h->priv, 0, BG_STREAM_ACTION_DECODE);

  if(plugin->start && !plugin->start(h->priv))
    goto fail;
  
  m = gavl_track_get_metadata(ti);
  
  /* Create object */
  obj = bg_db_object_create(db);
  
  bg_db_object_set_type(obj, BG_DB_OBJECT_RADIO_URL);
  u = (bg_db_url_t *)obj;

  u->stream_url = gavl_strdup(url);
  u->station_url = gavl_strdup(gavl_dictionary_get_string(m, GAVL_META_RELURL));

  if(label)
    bg_db_object_set_label(u, label);
  else if((str = gavl_dictionary_get_string(m, GAVL_META_STATION)))
    bg_db_object_set_label(u, str);
  else
    bg_db_object_set_label(u, url);

  gavl_dictionary_get_src(m, GAVL_META_SRC, 0, &mimetype_ptr, NULL);
  u->mimetype = gavl_strdup(mimetype_ptr);
  
  /* Add to db */

  if(u->mimetype)
    {
    u->mimetype_id = 
      bg_sqlite_string_to_id_add(db->db, "MIMETYPES",
                                 "ID", "NAME", u->mimetype);
    }

  sm = gavl_track_get_audio_metadata(ti, 0);

  gavl_dictionary_get_int(sm, GAVL_META_BITRATE, &bitrate);
  fmt = gavl_track_get_audio_format(ti, 0);

  u->samplerate = fmt->samplerate;
  u->channels   = fmt->num_channels;

  if(bitrate == GAVL_BITRATE_VBR)
    u->audio_bitrate = gavl_strdup("VBR");
  else if(bitrate == GAVL_BITRATE_LOSSLESS)
    u->audio_bitrate = gavl_strdup("Lossless");
  else if(bitrate <= 0)
    u->audio_bitrate = gavl_strdup("Unknown");
  else
    u->audio_bitrate = bg_sprintf("%d", bitrate / 1000);
  
  
  sql = sqlite3_mprintf("INSERT INTO URLS ( ID, STREAM_URL, STATION_URL, MIMETYPE, "
                        "AUDIO_BITRATE, SAMPLERATE, CHANNELS ) VALUES ( %"PRId64", %Q,"
                        " %Q, %"PRId64", %Q, %d, %d );",
                        u->obj.id, u->stream_url, u->station_url, u->mimetype_id,
                        u->audio_bitrate, u->samplerate, u->channels);
  
  bg_sqlite_exec(db->db, sql, NULL, NULL);
  sqlite3_free(sql);

  bg_db_object_set_parent(db, u, parent);

  bg_db_object_unref(u);
  
  fail:

  if(plugin)
    plugin->close(h->priv);

  if(h)
    bg_plugin_unref(h);
  
  }

static int compare_url_by_location(const bg_db_object_t * obj, const void * data)
  {
  const bg_db_url_t * url;
  if(obj->type == BG_DB_OBJECT_RADIO_URL)
    {
    url = (const bg_db_url_t*)obj;
    if(!strcmp(url->stream_url, data))
      return 1;
    }
  return 0;
  } 

int64_t bg_db_url_by_location(bg_db_t * db, const char * location)
  {
  int64_t ret;

  ret = bg_db_cache_search(db, compare_url_by_location, location);
  if(ret > 0)
    return ret;
  
  return bg_sqlite_string_to_id(db->db, "URLS", "ID", "STREAM_URL", location);
  }


void bg_db_add_radio_album(bg_db_t * db, const char * album_file)
  {
  bg_album_entry_t * entries;
  bg_album_entry_t * e;
  bg_album_entry_t * before;
  char * name;
  bg_db_object_t * container;
  bg_sqlite_id_tab_t tab;
  bg_db_url_t * u;
  int i;

  gavl_buffer_t buf;
  gavl_buffer_init(&buf);
  
  if(!bg_read_file(album_file, &buf))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Album file %s could not be opened", album_file);
    return;
    }
  
  entries = bg_album_entries_new_from_xml((char*)buf.buf);
  
  if(!entries)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Album file %s contains no entries",
           album_file);
    return;
    }

  name = bg_path_to_label(album_file);

  /* Get parent container */
  container = bg_db_get_radio_url_folder(db, name);
  free(name);

  /* Query children and check if the entries are in the album */
  bg_sqlite_id_tab_init(&tab);
  bg_db_get_children(db, bg_db_object_get_id(container), &tab);

  for(i = 0; i < tab.num_val; i++)
    {
    u = bg_db_object_query(db, tab.val[i]);
    e = entries;
    while(e)
      {
      const char * location;

      if(!gavl_dictionary_get_src(&e->m, GAVL_META_SRC, 0, NULL, &location))
        {
        e = e->next;
        continue;
        }
      
      if(!strcmp(location, u->stream_url))
        break;
      else
        e = e->next;
      }

    if(e)
      {
      /* Remove album entry */
      if(e == entries)
        entries = entries->next;
      else
        {
        before = entries;
        while(before->next != e)
          before = before->next;
        before->next = e->next;
        }
      bg_album_entry_destroy(e);
      bg_db_object_unref(u);
      }
    else
      bg_db_object_delete(db, u);
    }
  
  /* Add stations */
  e = entries;
  
  while(e)
    {
    const char * location;
    const char * name = gavl_dictionary_get_string(&e->m, GAVL_META_LABEL);

    if(!gavl_dictionary_get_src(&e->m, GAVL_META_SRC, 0, NULL, &location))
      {
      e = e->next;
      continue;
      }
    
    if(*location == '/') // Absolute path
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Unsupported location %s",
             location);
      }
    else
      bg_db_add_radio_url(db, container, location, name);
    e = e->next;
    }

  bg_db_object_unref(container);
  bg_album_entries_destroy(entries);
  }
