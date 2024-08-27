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


#include <stdio.h>
#include <string.h>
#include <stdlib.h>


#include <gavl/gavl.h>
#include <gavl/value.h>
#include <gavl/metatags.h>


#include <bgsqlite.h>
#include <gmerlin/utils.h>

static int append_id_callback(void * data, int argc, char **argv, char **azColName)
  {
  gavl_value_t val;

  //  fprintf(stderr, "Append ID: %s\n", argv[0]);
  
  gavl_value_init(&val);
  gavl_value_set_string(&val, argv[0]);
  gavl_array_splice_val_nocopy(data, -1, 0, &val);
  
  return 0;
  }

/* albums */

static int query_album_callback(void * data, int argc, char **argv, char **azColName)
  {
  gavl_dictionary_set_string(data, azColName[0], argv[0]);
  gavl_dictionary_set_string(data, azColName[1], argv[1]);
  return 0;
  }

static int dump_song_callback(void * data, int argc, char **argv, char **azColName)
  {
  if(!strcmp(argv[1], "9999"))
    printf("%s\n", argv[0]);
  else
    printf("%s (%s)\n", argv[0], argv[1]);
  
  return 0;
  }

static void dump_albums_album(sqlite3 * db, int64_t album, const char * artist, int songs)
  {
  gavl_dictionary_t dict;
  char * sql;
  const char * var;
  
  gavl_dictionary_init(&dict);
  sql = bg_sprintf("SELECT "GAVL_META_TITLE", "GAVL_META_DATE" FROM albums WHERE DBID = %"PRId64";", album);
  
  bg_sqlite_exec(db, sql, query_album_callback, &dict);
  
  var = gavl_dictionary_get_string(&dict, GAVL_META_DATE);

  if(gavl_string_starts_with(var, "9999"))
    {
    printf("%s - %s\n", artist, gavl_dictionary_get_string(&dict, GAVL_META_TITLE));
    }
  else
    {
    char * date_str;
    date_str = gavl_strndup(var, var + 4);
    printf("%s - %s - %s\n", artist, date_str, gavl_dictionary_get_string(&dict, GAVL_META_TITLE));
    free(date_str);
    }
  
  free(sql);
  gavl_dictionary_free(&dict);

  if(songs)
    {
    int i;
    gavl_array_t songs;
    gavl_array_init(&songs);

    sql = bg_sprintf("SELECT DBID FROM songs WHERE ParentID = %"PRId64" ORDER BY "GAVL_META_TRACKNUMBER";", album);
    bg_sqlite_exec(db, sql, append_id_callback, &songs);
    free(sql);

    for(i = 0; i < songs.num_entries; i++)
      {
      int j;
      gavl_array_t artists;
      gavl_array_init(&artists);

      sql = bg_sprintf("SELECT song_artists.NAME FROM song_artists JOIN song_artists_arr ON song_artists.ID = song_artists_arr.NAME_ID WHERE song_artists_arr.OBJ_ID = %s ORDER BY song_artists_arr.ID;", gavl_value_get_string(&songs.entries[i]));
      bg_sqlite_exec(db, sql, append_id_callback, &artists);
      free(sql);
      
      for(j = 0; j < artists.num_entries; j++)
        {
        if(j)
          printf(", ");
        else
          printf("  ");
        printf("%s", gavl_value_get_string(&artists.entries[j]));
        }
      printf(" - ");

      sql = bg_sprintf("SELECT TITLE, substr(DATE, 1, 4) FROM songs WHERE DBID = %s;", gavl_value_get_string(&songs.entries[i]));
      bg_sqlite_exec(db, sql, dump_song_callback, NULL);
      free(sql);
      
      gavl_array_free(&artists);
      }

    gavl_array_free(&songs);
    }

  }

static void dump_albums_artist(sqlite3 * db, int64_t genre, int64_t artist, int songs)
  {
  int i;
  char * sql;
  char * artist_str;
  gavl_array_t albums;
  gavl_array_init(&albums);

  sql = bg_sprintf("SELECT album_artists_arr.OBJ_ID "
                   "FROM "
                   "album_artists_arr INNER JOIN album_genres_arr "
                   "ON album_artists_arr.OBJ_ID = album_genres_arr.OBJ_ID "
                   "INNER JOIN albums "
                   "ON albums.DBID = album_genres_arr.OBJ_ID "
                   "WHERE album_genres_arr.NAME_ID = %"PRId64" AND "
                   "album_artists_arr.NAME_ID = %"PRId64" ORDER BY albums."GAVL_META_DATE", albums."GAVL_META_SEARCH_TITLE";",
                   genre, artist);

  //  fprintf(stderr, "%s\n", sql);
  
  bg_sqlite_exec(db, sql, append_id_callback, &albums);  /* An open database */
  free(sql);

  artist_str = bg_sqlite_id_to_string(db, "album_artists", "NAME", "ID", artist);
  printf("ARTIST: %s\n", artist_str);
  
  for(i = 0; i < albums.num_entries; i++)
    {
    const char * album_str = gavl_value_get_string(&albums.entries[i]);
    dump_albums_album(db, strtoll(album_str, NULL, 10), artist_str, songs);
    }

  free(artist_str);
  gavl_array_free(&albums);
  }

static void dump_albums_genre(sqlite3 * db, int64_t genre, int songs)
  {
  int i;
  char * sql;
  char * genre_str = NULL;
  gavl_array_t artists;
  gavl_array_init(&artists);

  genre_str = bg_sqlite_id_to_string(db, "album_genres", "NAME", "ID", genre);
  printf("GENRE: %s\n", genre_str);
  free(genre_str);
  
  sql = bg_sprintf("SELECT ID FROM album_artists WHERE ID in (SELECT DISTINCT album_artists_arr.NAME_ID "
                   "FROM album_artists_arr INNER JOIN album_genres_arr "
                   "ON album_artists_arr.OBJ_ID = album_genres_arr.OBJ_ID "
                   "WHERE album_genres_arr.NAME_ID = %"PRId64") ORDER BY NAME;", genre);

  //  fprintf(stderr, "%s\n", sql);
  
  bg_sqlite_exec(db, sql, append_id_callback, &artists);  /* An open database */
  free(sql);

  for(i = 0; i < artists.num_entries; i++)
    {
    const char * artist_str = gavl_value_get_string(&artists.entries[i]);
    dump_albums_artist(db, genre, strtoll(artist_str, NULL, 10), songs);
    }
  
  gavl_array_free(&artists);
  }

static void dump_albums(sqlite3 * db, int songs)
  {
  int i;
  char * sql;
  
  gavl_array_t genres;
  gavl_array_init(&genres);

  sql = bg_sprintf("SELECT ID FROM album_genres ORDER BY NAME;");
  bg_sqlite_exec(db, sql, append_id_callback, &genres);  /* An open database */
  free(sql);

  for(i = 0; i < genres.num_entries; i++)
    {
    const char * genre_str = gavl_value_get_string(&genres.entries[i]);
    dump_albums_genre(db, strtoll(genre_str, NULL, 10), songs);
    }
  
  gavl_array_free(&genres);
  }

/* Movie */


static int query_movie_callback(void * data, int argc, char **argv, char **azColName)
  {
  printf("%s (%s)\n", argv[0], argv[1]);
  return 0;
  }

static void dump_movies(sqlite3 * db)
  {
  char * sql;
  sql = bg_sprintf("SELECT "GAVL_META_TITLE", substr("GAVL_META_DATE", 1, 4) FROM movies ORDER BY "GAVL_META_TITLE";");
  bg_sqlite_exec(db, sql, query_movie_callback, NULL);  /* An open database */
  free(sql);
  }

static void write_string(FILE * out, const char * str)
  {
  while(*str != '\0')
    {
    if(*str == '&')
      fprintf(out, "&amp;");
    else
      fwrite(str, 1, 1, out);
    str++;
    }
  }

static void query_array(sqlite3 * db, gavl_array_t * ret, const char * id, const char * table)
  {
  char * sql;
  sql = bg_sprintf("SELECT %s.NAME FROM %s JOIN %s_arr on %s.ID = %s_arr.NAME_ID where %s_arr.OBJ_ID = %s ORDER BY %s_arr.ID;",
                   table, table, table, table, table, table, id, table);
  bg_sqlite_exec(db, sql, append_id_callback, ret);  /* An open database */
  free(sql);
  }

static int query_movie_nfo_callback(void * data, int argc, char **argv, char **azColName)
  {
  int i;
  FILE * out;

  char * pos;
  char * basename;
  char * nfoname;
  const char * title;
  const char * originaltitle;
  const char * year;
  const char * plot;
  const char * id;

  gavl_array_t arr;
  
  id            = argv[0];
  title         = argv[1];
  year          = argv[2];
  originaltitle = argv[3];
  plot          = argv[4];
  
  basename = bg_sprintf("%s (%s)", title, year);

  /* Remove characters forbidden in filenames */
  pos = basename;
  while(*pos != '\0')
    {
    if((*pos == '?') ||
       (*pos == '/') ||
       (*pos == ':') ||
       (*pos == '.'))
      *pos = '-';
    pos++;
    }

  // fprintf(stderr, "Basename: %s\n", basename);

  gavl_ensure_directory(basename, 0);

  nfoname = bg_sprintf("%s/%s.nfo", basename, basename);

  out = fopen(nfoname, "w");

  fprintf(out, "<movie>\n");

  fprintf(out, "<title>");
  write_string(out, title);
  fprintf(out, "</title>\n");
  
  fprintf(out, "<year>");
  write_string(out, year);
  fprintf(out, "</year>\n");
  
  if(originaltitle)
    {
    fprintf(out, "<originaltitle>");
    write_string(out, originaltitle);
    fprintf(out, "</originaltitle>\n");
    }

  if(plot)
    {
    fprintf(out, "<outline>");
    write_string(out, plot);
    fprintf(out, "</outline>\n");
    }

  gavl_array_init(&arr);
  
  query_array(data, &arr, id, "movie_genres");
  fprintf(out, "<genre>");
  for(i = 0; i < arr.num_entries; i++)
    {
    if(i)
      fprintf(out, ",");
    
    write_string(out, gavl_value_get_string(&arr.entries[i]));
    }
  fprintf(out, "</genre>\n");
  gavl_array_reset(&arr);

  query_array(data, &arr, id, "movie_directors");
  for(i = 0; i < arr.num_entries; i++)
    {
    fprintf(out, "<director>");
    write_string(out, gavl_value_get_string(&arr.entries[i]));
    fprintf(out, "</director>\n");
    }
  gavl_array_reset(&arr);

  query_array(data, &arr, id, "movie_actors");
  for(i = 0; i < arr.num_entries; i++)
    {
    fprintf(out, "<actor><name>");
    write_string(out, gavl_value_get_string(&arr.entries[i]));
    fprintf(out, "</name></actor>\n");
    }
  gavl_array_reset(&arr);

  query_array(data, &arr, id, "movie_countries");
  for(i = 0; i < arr.num_entries; i++)
    {
    fprintf(out, "<country>");
    write_string(out, gavl_value_get_string(&arr.entries[i]));
    fprintf(out, "</country>\n");
    }
  gavl_array_reset(&arr);

  
  fprintf(out, "</movie>\n");
  
  fclose(out);

  fprintf(stderr, "Wrote %s\n", nfoname);
  
  free(basename);
  free(nfoname);

  
  return 0;
  }

static void dump_movie_nfos(sqlite3 * db)
  {
  char * sql;
  sql = bg_sprintf("SELECT DBID, "GAVL_META_TITLE", substr("GAVL_META_DATE", 1, 4), "GAVL_META_ORIGINAL_TITLE", "GAVL_META_PLOT
                   " FROM movies ORDER BY "GAVL_META_TITLE";");
  bg_sqlite_exec(db, sql, query_movie_nfo_callback, db);  /* An open database */
  free(sql);
  }

typedef struct
  {
  const char * show;
  int season;
  } season_t;

static int query_episode_callback(void * data, int argc, char **argv, char **azColName)
  {
  season_t * s = data;

  if(gavl_string_starts_with(argv[2], "9999"))
    {
    printf("%s S%02dE%02d %s\n", s->show, s->season, atoi(argv[0])+1, argv[1]);
    }
  else if(gavl_string_ends_with(argv[2], "99"))
    {
    char * tmp_string = gavl_strndup(argv[2], argv[2] + 4);
    printf("%s S%02dE%02d %s (%s)\n", s->show, s->season, atoi(argv[0])+1, argv[1], tmp_string);
    free(tmp_string);
    }
  else
    printf("%s S%02dE%02d %s (%s)\n", s->show, s->season, atoi(argv[0])+1, argv[1], argv[2]);
  return 0;
  }

static void dump_episodes_season(sqlite3 * db, const char * show, int64_t season_id)
  {
  char * sql;

  season_t s;

  s.show = show;
  s.season = bg_sqlite_id_to_id(db, "seasons", GAVL_META_SEASON, "DBID", season_id);
  
  sql = bg_sprintf("SELECT "GAVL_META_IDX", "GAVL_META_TITLE", "GAVL_META_DATE" FROM episodes WHERE ParentID = %"PRId64" ORDER BY "GAVL_META_IDX";",
                   season_id);
  bg_sqlite_exec(db, sql, query_episode_callback, &s);  /* An open database */
  free(sql);
  }

static void dump_episodes_show(sqlite3 * db, int64_t show_id)
  {
  int i;
  char * sql;
  char * show;
  gavl_array_t seasons;
  gavl_array_init(&seasons);

  show = bg_sqlite_id_to_string(db, "shows", GAVL_META_TITLE, "DBID", show_id);
  
  sql = bg_sprintf("SELECT DBID FROM seasons WHERE ParentID = %"PRId64" ORDER BY "GAVL_META_SEASON";", show_id);
  bg_sqlite_exec(db, sql, append_id_callback, &seasons);  /* An open database */
  free(sql);

  for(i = 0; i < seasons.num_entries; i++)
    {
    const char * season_str = gavl_value_get_string(&seasons.entries[i]);
    dump_episodes_season(db, show, strtoll(season_str, NULL, 10));
    }
  
  free(show);
  gavl_array_free(&seasons);
  }

static void dump_episodes(sqlite3 * db)
  {
  int i;
  char * sql;
  
  gavl_array_t shows;
  gavl_array_init(&shows);

  sql = bg_sprintf("SELECT DBID FROM shows ORDER BY "GAVL_META_TITLE";");
  bg_sqlite_exec(db, sql, append_id_callback, &shows);  /* An open database */
  free(sql);

  for(i = 0; i < shows.num_entries; i++)
    {
    const char * show_str = gavl_value_get_string(&shows.entries[i]);
    dump_episodes_show(db, strtoll(show_str, NULL, 10));
    }
  
  gavl_array_free(&shows);
  
  }

int main(int argc, char ** argv)
  {
  int i;
  sqlite3 * db;

  if(argc < 2)
    {
    fprintf(stderr, "Usage: %s [-albums] [-songs] [-nfos] [-movies] [-movie-nfos] [-shows] [-episodes]", argv[0]);
    return 0;
    }
  
  if(sqlite3_open(argv[argc-1], &db))
    {
    fprintf(stderr, "Cannot open database %s\n", argv[argc-1]);
    return 0;
    }
  
  for(i = 1; i < argc-1; i++)
    {
    if(!strcmp(argv[i], "-albums"))
      {
      /* Dump albums */
      dump_albums(db, 0);
      }
    if(!strcmp(argv[i], "-songs"))
      {
      /* Dump songs */
      dump_albums(db, 1);
      }
    if(!strcmp(argv[i], "-nfos"))
      {
      /* Dump nfos */
      
      }
    if(!strcmp(argv[i], "-movies"))
      {
      /* Dump movies */
      dump_movies(db);
      }
    if(!strcmp(argv[i], "-movie-nfos"))
      {
      /* Dump movies */
      dump_movie_nfos(db);
      }
    if(!strcmp(argv[i], "-shows"))
      {
      /* Dump shows */
      
      }
    if(!strcmp(argv[i], "-episodes"))
      {
      /* Dump episodes */
      dump_episodes(db);
      }
    }
  }

