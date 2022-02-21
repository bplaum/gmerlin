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
#include <unistd.h>

#include <mediadb_private.h>
#include <gmerlin/log.h>
#include <gmerlin/utils.h>
#include <string.h>

#define LOG_DOMAIN "db.audiofile"

static void free_audio_file(void * obj)
  {
  bg_db_audio_file_t * file = obj;
  if(file->title)
    free(file->title);
  if(file->search_title)
    free(file->search_title);
  if(file->bitrate)
    free(file->bitrate);
  if(file->genre)
    free(file->genre);
  if(file->album)
    free(file->album);
  if(file->artist)
    free(file->artist);
  if(file->albumartist)
    free(file->albumartist);
  }

static void dump_audio_file(void * obj)
  {
  bg_db_audio_file_t * file = obj;
  gavl_diprintf(2, "Title:       %s\n", file->title);
  gavl_diprintf(2, "SearchTitle: %s\n", file->title);
  gavl_diprintf(2, "Artist:      %s\n", file->artist);
  gavl_diprintf(2, "Year:        %d\n", file->date.year);
  gavl_diprintf(2, "Album:       %s (%"PRId64")\n", file->album, file->album_id);
  gavl_diprintf(2, "Track:       %d\n", file->track);
  gavl_diprintf(2, "Bitrate:     %s\n", file->bitrate);
  gavl_diprintf(2, "Genre:       %s\n", file->genre);
  gavl_diprintf(2, "Samplerate:  %d\n", file->samplerate);
  gavl_diprintf(2, "Channels:    %d\n", file->channels);
  }

static void del_audio_file(bg_db_t * db, bg_db_object_t * obj) // Delete from db
  {
  bg_sqlite_delete_by_id(db->db, "AUDIO_FILES", obj->id);
  bg_db_audio_file_remove_from_album(db, (bg_db_audio_file_t*)obj);
  }


#define TITLE_COL         1
#define SEARCH_TITLE_COL  2
#define ARTIST_COL        3
#define GENRE_COL         4
#define DATE_COL          5
#define ALBUM_COL         6
#define TRACK_COL         7
#define BITRATE_COL       8
#define SAMPLERATE_COL    9
#define CHANNELS_COL     10


static int query_audio_file(bg_db_t * db, void * obj)
  {
  int result;
  int found = 0;
  bg_db_audio_file_t * f = obj;

  sqlite3_stmt * st = db->q_audio_files;
  
  sqlite3_bind_int64(st, 1, f->file.obj.id);
  
  if((result = sqlite3_step(st)) == SQLITE_ROW)
    {
    BG_DB_GET_COL_STRING(TITLE_COL, f->title);
    BG_DB_GET_COL_STRING(SEARCH_TITLE_COL, f->search_title);
    BG_DB_GET_COL_INT(ARTIST_COL, f->artist_id);
    BG_DB_GET_COL_INT(GENRE_COL, f->genre_id);
    BG_DB_GET_COL_DATE(DATE_COL, f->date);
    BG_DB_GET_COL_INT(ALBUM_COL, f->album_id);
    BG_DB_GET_COL_INT(TRACK_COL, f->track);
    BG_DB_GET_COL_STRING(BITRATE_COL, f->bitrate);
    BG_DB_GET_COL_INT(SAMPLERATE_COL, f->samplerate);
    BG_DB_GET_COL_INT(CHANNELS_COL, f->channels);
    found = 1;
    }
  sqlite3_reset(st);
  sqlite3_clear_bindings(st);
  
  if(!found)
    return 0;
  
  f->artist = bg_db_string_cache_get(db->audio_artists, db->db,
                                     f->artist_id);
  f->genre = bg_db_string_cache_get(db->audio_genres, db->db,
                                    f->genre_id);
  f->album = bg_db_string_cache_get(db->audio_albums, db->db,
                                    f->album_id);
  return 1;
  }

const bg_db_object_class_t bg_db_audio_file_class =
  {
  .name = "Audio file",
  .del = del_audio_file,
  .free = free_audio_file,
  .query = query_audio_file,
  .dump = &dump_audio_file,
  .parent = &bg_db_file_class,
  };

/* Add to db */
static void audio_file_add(bg_db_t * db, bg_db_audio_file_t * f)
  {
  char * sql;

  char date_string[BG_DB_DATE_STRING_LEN];

  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Adding audio file %s", f->file.path);
  
  /* Date */
  bg_db_date_to_string(&f->date, date_string);
  
  sql = sqlite3_mprintf("INSERT INTO AUDIO_FILES ( ID, TITLE, SEARCH_TITLE, ARTIST, GENRE, DATE, ALBUM, TRACK, BITRATE, SAMPLERATE, CHANNELS ) "
                        "VALUES"
                        " ( %"PRId64", %Q, %Q, %"PRId64", %"PRId64", %Q, %"PRId64", %d, %Q, %d, %d );",
                        bg_db_object_get_id(f) , f->title, f->search_title, f->artist_id, f->genre_id,
                        date_string, f->album_id, 
                        f->track, f->bitrate, f->samplerate, f->channels);

  bg_sqlite_exec(db->db, sql, NULL, NULL);
  sqlite3_free(sql);
  }

void bg_db_audio_file_create(bg_db_t * db, void * obj, gavl_dictionary_t * t)
  {
  int bitrate = 0;
  const char * var;
  const gavl_audio_format_t * fmt;
  const gavl_dictionary_t * m;
  const gavl_dictionary_t * m1;
  
  bg_db_audio_file_t * f = obj;
  
  bg_db_object_set_type(obj, BG_DB_OBJECT_AUDIO_FILE);

  m1 = gavl_track_get_metadata(t);
  
  if((var = gavl_dictionary_get_string(m1, GAVL_META_TITLE)))
    {
    f->title = gavl_strdup(var);
    f->search_title = gavl_strdup(bg_db_get_search_string(f->title));
    }
  
  if((var = gavl_dictionary_get_string(m1, GAVL_META_ARTIST)))
    f->artist = bg_strip_space(gavl_strdup(var));
  if((var = gavl_dictionary_get_string(m1, GAVL_META_ALBUMARTIST)))
    f->albumartist = bg_strip_space(gavl_strdup(var));
  if((var = gavl_dictionary_get_string(m1, GAVL_META_GENRE)))
    f->genre = bg_strip_space(gavl_strdup(var));
  
  if(!(f->date.year = bg_metadata_get_year(m1)))
    bg_db_date_set_invalid(&f->date);
  
  if((var = gavl_dictionary_get_string(m1, GAVL_META_ALBUM)))
    f->album = bg_strip_space(gavl_strdup(var));

  gavl_dictionary_get_int(m1, GAVL_META_TRACKNUMBER, &f->track);

  fmt = gavl_track_get_audio_format(t, 0);

  m = gavl_track_get_audio_metadata(t, 0);

  gavl_dictionary_get_int(m, GAVL_META_BITRATE, &bitrate);
  f->samplerate = fmt->samplerate;
  f->channels =   fmt->num_channels;

  if(bitrate == GAVL_BITRATE_VBR)
    f->bitrate = gavl_strdup("VBR");
  else if(bitrate == GAVL_BITRATE_LOSSLESS)
    f->bitrate = gavl_strdup("Lossless");
  else if(bitrate <= 0)
    f->bitrate = gavl_strdup("Unknown");
  else
    f->bitrate = bg_sprintf("%d", bitrate / 1000);

  /* Artist */
  if(f->artist)
    {
    f->artist_id = 
      bg_sqlite_string_to_id_add(db->db, "AUDIO_ARTISTS",
                                 "ID", "NAME", f->artist);
    }

  /* Genre */
  if(f->genre)
    {
    f->genre_id = 
      bg_sqlite_string_to_id_add(db->db, "AUDIO_GENRES",
                                 "ID", "NAME", f->genre);
    }

  /* Albumartist */
  if(f->albumartist)
    {
    f->albumartist_id = 
      bg_sqlite_string_to_id_add(db->db, "AUDIO_ARTISTS",
                                 "ID", "NAME", f->albumartist);
    }

  /* Add to album */
  if(f->album && f->albumartist)
    bg_db_audio_file_add_to_album(db, f);

  /* Add to db */
  audio_file_add(db, f);
  bg_db_create_vfolders(db, f);
  }

void bg_db_cleanup_audio(bg_db_t * db)
  {
  int i;
  int64_t count;
  int64_t total;
  char * sql = NULL;
  bg_sqlite_id_tab_t tab;
  bg_sqlite_id_tab_init(&tab);

  total = 0;

  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Cleaning up audio data");
    
  sql = sqlite3_mprintf("select ID from AUDIO_GENRES;");
  bg_sqlite_exec(db->db, sql, bg_sqlite_append_id_callback, &tab);
  sqlite3_free(sql);
  
  for(i = 0; i < tab.num_val; i++)
    {
    /* Count songs with that genre */
    count = 0;
    sql = sqlite3_mprintf("select count(*) from AUDIO_FILES where GENRE = %"PRId64";",
                          tab.val[i]);
    bg_sqlite_exec(db->db, sql, bg_sqlite_int_callback, &count);
    sqlite3_free(sql);

    total = count;
    
    /* Count albums with that genre */
    count = 0;
    sql = sqlite3_mprintf("select count(*) from AUDIO_ALBUMS where GENRE = %"PRId64";",
                          tab.val[i]);
    bg_sqlite_exec(db->db, sql, bg_sqlite_int_callback, &count);
    sqlite3_free(sql);

    total += count;

    /* Check if empty */
    if(!total)
      {
      char * name;
      name = bg_sqlite_id_to_string(db->db, "AUDIO_GENRES", "NAME", "ID", tab.val[i]);
      gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Removing empty audio genre %s", name);
      free(name);
      bg_sqlite_delete_by_id(db->db, "AUDIO_GENRES", tab.val[i]);
      }
    }

  bg_sqlite_id_tab_reset(&tab);

  /* Artists */
  sql = sqlite3_mprintf("select ID from AUDIO_ARTISTS;");
  bg_sqlite_exec(db->db, sql, bg_sqlite_append_id_callback, &tab);
  sqlite3_free(sql);

  for(i = 0; i < tab.num_val; i++)
    {
    /* Count songs with that artist */
    count = 0;
    sql = sqlite3_mprintf("select count(*) from AUDIO_FILES where ARTIST = %"PRId64";",
                          tab.val[i]);
    bg_sqlite_exec(db->db, sql, bg_sqlite_int_callback, &count);
    sqlite3_free(sql);

    total = count;
    
    /* Count albums with that artist */
    count = 0;
    sql = sqlite3_mprintf("select count(*) from AUDIO_ALBUMS where ARTIST = %"PRId64";",
                          tab.val[i]);
    bg_sqlite_exec(db->db, sql, bg_sqlite_int_callback, &count);
    sqlite3_free(sql);

    total += count;

    /* Check if empty */
    if(!total)
      {
      char * name;
      name = bg_sqlite_id_to_string(db->db, "AUDIO_ARTISTS", "NAME", "ID", tab.val[i]);
      gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Removing empty audio artist %s", name);
      free(name);
      bg_sqlite_delete_by_id(db->db, "AUDIO_ARTISTS", tab.val[i]);
      }
    }
  
  
  bg_sqlite_id_tab_free(&tab);
  }

