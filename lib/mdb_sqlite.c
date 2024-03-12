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
#include <string.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <ctype.h>

#include <gmerlin/mdb.h>
#include <gmerlin/utils.h>

#include <gmerlin/translation.h>
#include <gmerlin/log.h>
#define LOG_DOMAIN "mdb.sqlite"

#include <mdb_private.h>
#include <gavl/metatags.h>
#include <gavl/utils.h>

/* Switch of re-scanning for debugging */
// #define NO_RESCAN


#define META_PARENT_ID "ParentID"

#define META_DB_ID       "DBID"
#define META_SCAN_DIR_ID "ScanDirID"

#define META_POSTER_ID    "PosterID"
#define META_WALLPAPER_ID "WallaperID"
#define META_COVER_ID     "CoverID"
#define META_NFO_ID       "NFOID"

#define META_IMAGE_TYPE   "IMAGETYPE"


#define DATE_UNDEFINED "9999-99-99"
//#define META_MIMETYPE_ID  "MimeTypeID"

#define DEL_FLAG_RELATED  (1<<0)
#define DEL_FLAG_CHILDREN (1<<1)
#define DEL_FLAG_PARENT   (1<<2)

/* Folder structure generated completely on the fly
 * using SQL queries
 * Queries, which take too long are cached
 
   /songs
   /songs/artist/a/
   /songs/artist/b/
   /songs/genre/1/a/
   /songs/genre/1/b/
   /songs/genre-artist/1/2
   /songs/genre-artist/1/6
   /songs/genre-year/3/2006/
   /songs/genre-artist/1/6
   /songs/year/1970/a/
   /albums/artist/a/2
   /albums/artist/b/10
   /albums/genre-artist/1/10
   /albums/genre-artist/1/11
   /albums/genre-year/1/1959
   /albums/genre-year/1/1960
   /albums/year/1970/a/
   /series
   /series/all
   /series/genre/1
   /series/genre/2
   /movies
   /movies/all
   /movies/genre/1
   /movies/genre/2
   /movies/director/a/2
   /movies/director/a/5
   /movies/actor/a/2
   /movies/actor/a/5
   /movies/country/2
   /movies/country/5
   /movies/year/1967
   /movies/year/1969
   
*/

typedef enum
  {
    IMAGE_TYPE_IMAGE      = 0,
    IMAGE_TYPE_COVER      = 1,
    IMAGE_TYPE_POSTER     = 2,
    IMAGE_TYPE_WALLPAPER  = 3,
  } image_type_t;

typedef enum
  {
    TYPE_SONG         = 1,
    TYPE_ALBUM        = 2,
    TYPE_TV_SHOW      = 3,
    TYPE_TV_SEASON    = 4,
    TYPE_TV_EPISODE   = 5,
    TYPE_MOVIE        = 6,
    TYPE_IMAGE        = 7,
    TYPE_NFO          = 8,
    TYPE_MOVIE_PART   = 9,
  } type_id_t;

static const struct
  {
  const char * gavl_id;
  const type_id_t sqlite_id;
  }
type_ids[] = 
  {
    { GAVL_META_CLASS_SONG,            TYPE_SONG       },
    { GAVL_META_CLASS_MUSICALBUM,      TYPE_ALBUM      },
    { GAVL_META_CLASS_TV_SHOW,         TYPE_TV_SHOW    },
    { GAVL_META_CLASS_TV_SEASON,       TYPE_TV_SEASON  },
    { GAVL_META_CLASS_TV_EPISODE,      TYPE_TV_EPISODE },
    { GAVL_META_CLASS_IMAGE,           TYPE_IMAGE      },
    { GAVL_META_CLASS_MOVIE,           TYPE_MOVIE      },
    { GAVL_META_CLASS_MOVIE_PART,      TYPE_MOVIE_PART },
    { "nfo",                                 TYPE_NFO        },
    {  /* End */                                             }
  };


static int browse_children_ids(bg_mdb_backend_t * b, const char * id,
                               gavl_array_t * ret);


static void update_root_containers(bg_mdb_backend_t * b);

static void create_root_containers(bg_mdb_backend_t * b);


static void delete_object_internal(bg_mdb_backend_t * b, int64_t id, type_id_t type,
                                   const char * uri, int del_flags);

static void delete_object(bg_mdb_backend_t * b, const gavl_dictionary_t * obj,
                          int del_flags);



static int64_t add_object(bg_mdb_backend_t * b, gavl_dictionary_t * track,
                          int64_t scan_dir_id, int64_t obj_id);

static type_id_t get_type_id(const char * media_class)
  {
  int i = 0;

  while(type_ids[i].gavl_id)
    {
    if(!strcmp(type_ids[i].gavl_id, media_class))
      return type_ids[i].sqlite_id;
    i++;
    }
  return 0;
  }

static const char * get_type_class(type_id_t type)
  {
  int i = 0;

  while(type_ids[i].gavl_id)
    {
    if(type_ids[i].sqlite_id == type)
      return type_ids[i].gavl_id;
    i++;
    }
  return NULL;
  }

static int is_blacklisted(const char * location)
  {
  return bg_file_is_blacklisted(location) ||
    gavl_string_ends_with_i(location, ".srt"); // TODO: Make this more elegant
  }

static int append_string_callback(void * data, int argc, char **argv, char **azColName)
  {
  gavl_value_t val;
  gavl_value_init(&val);
  gavl_value_set_string(&val, argv[0]);
  gavl_array_splice_val_nocopy(data, -1, 0, &val);
  return 0;
  }


typedef struct
  {
  const char * name;
  gavl_type_t type;
  const char * id_table;
  } column_t;

typedef struct
  {
  const char * name;
  const char * array_table_name;
  const char * id_table_name;
  } array_t;

#define FILE_COLS(TYPE)                     \
  { GAVL_META_URI,      GAVL_TYPE_STRING }, \
  { GAVL_META_MTIME,    GAVL_TYPE_LONG   },    \
  { GAVL_META_MIMETYPE, GAVL_TYPE_STRING, TYPE"_mimetypes" }

#define AUDIO_COLS                     \
  { GAVL_META_AUDIO_BITRATE,    GAVL_TYPE_INT    }, \
  { GAVL_META_AUDIO_CODEC,      GAVL_TYPE_STRING }, \
  { GAVL_META_AUDIO_CHANNELS,   GAVL_TYPE_INT   },  \
  { GAVL_META_AUDIO_SAMPLERATE, GAVL_TYPE_INT   }

#define VIDEO_COLS                            \
  { GAVL_META_VIDEO_CODEC,  GAVL_TYPE_STRING }, \
  { GAVL_META_WIDTH,        GAVL_TYPE_INT   },  \
  { GAVL_META_HEIGHT,       GAVL_TYPE_INT   }

#define VIDEO_PART_COLS               \
  { GAVL_META_APPROX_DURATION, GAVL_TYPE_LONG }, \

// #define VIDEO_PART_SRC_COLS(TYPE) FILE_COLS(TYPE)

typedef struct
  {
  const type_id_t type;
  const char * table_name;
  const column_t * cols;
  const column_t * src_cols;   // Taken from src[0]

  /* Table for parts of (mutlipart) movies */
  const char * part_cols_table;

  const array_t * arrays;      // Typically string arrays
  
  const int pass;
  } obj_table_t;

static const obj_table_t obj_tables[] = 
  {
    /* Image */
    {
      .type       = TYPE_IMAGE,
      .table_name = "images",
      .pass       = 1,
      .cols       = (const column_t[])
      {
        { META_DB_ID,                GAVL_TYPE_LONG   },
        { META_SCAN_DIR_ID,          GAVL_TYPE_LONG   },
        { META_IMAGE_TYPE,           GAVL_TYPE_INT    },
        {/* End */  },
      },
      .src_cols = (const column_t[])
      {
        FILE_COLS("image"),
        { GAVL_META_WIDTH,  GAVL_TYPE_INT },
        { GAVL_META_HEIGHT, GAVL_TYPE_INT },
        { /* End */ }
      },
    },
    /* NFOs */
    {
      .type       = TYPE_NFO,
      .table_name = "nfos",
      .pass       = 1,
      .cols       = (const column_t[])
      {
        { META_DB_ID,         GAVL_TYPE_LONG   },
        { META_SCAN_DIR_ID,   GAVL_TYPE_LONG   },
        { GAVL_META_URI,      GAVL_TYPE_STRING },
        { GAVL_META_MTIME,    GAVL_TYPE_LONG   },
        {/* End */  },
      },
    },
    /* Song */
    {
      .type       = TYPE_SONG,
      .table_name = "songs",
      .pass       = 2,
      .cols       = (const column_t[])
      {
        { META_DB_ID,                GAVL_TYPE_LONG   },
        { GAVL_META_TITLE,           GAVL_TYPE_STRING },
        { GAVL_META_SEARCH_TITLE,    GAVL_TYPE_STRING },
        { META_PARENT_ID,             GAVL_TYPE_LONG   },
        { GAVL_META_TRACKNUMBER,     GAVL_TYPE_INT    },
        { GAVL_META_APPROX_DURATION, GAVL_TYPE_LONG   },
        { GAVL_META_DATE,            GAVL_TYPE_STRING },
        AUDIO_COLS,
        { META_SCAN_DIR_ID,          GAVL_TYPE_LONG },
        { META_COVER_ID,             GAVL_TYPE_LONG },
        {/* End */  },
      },
      .src_cols = (const column_t[]){ FILE_COLS("song"), { /* End */ } },
      .arrays     = (const array_t[])
      {
        { GAVL_META_ARTIST, "song_artists_arr", "song_artists" },
        { GAVL_META_GENRE,  "song_genres_arr",  "song_genres" },
        {/* End */  },
      },
    },
    /* Album */
    {
      .type       = TYPE_ALBUM,
      .table_name = "albums",
      .pass       = 2,
      .cols       = (const column_t[])
      {
        { META_DB_ID,                GAVL_TYPE_LONG   },
        { GAVL_META_TITLE,           GAVL_TYPE_STRING },
        { GAVL_META_SEARCH_TITLE,    GAVL_TYPE_STRING },
        { GAVL_META_APPROX_DURATION, GAVL_TYPE_LONG   },
        { GAVL_META_DATE,            GAVL_TYPE_STRING },
        { GAVL_META_NUM_CHILDREN,    GAVL_TYPE_INT    },
        { META_COVER_ID,             GAVL_TYPE_LONG   },
        {/* End */  },
      },
      .arrays     = (const array_t[])
      {
        { GAVL_META_ARTIST, "album_artists_arr", "album_artists" },
        { GAVL_META_GENRE,  "album_genres_arr",  "album_genres" },
        {/* End */  },
      },
    },
    /* TV Shows */
    {
      .type       = TYPE_TV_SHOW,
      .table_name = "shows",
      .pass       = 2,
      .cols       = (const column_t[])
      {
        { META_DB_ID,             GAVL_TYPE_LONG   },
        { GAVL_META_TITLE,        GAVL_TYPE_STRING },
        { GAVL_META_SEARCH_TITLE, GAVL_TYPE_STRING },
        { GAVL_META_NUM_CHILDREN, GAVL_TYPE_INT    },
        { GAVL_META_PLOT, GAVL_TYPE_STRING    },
        { META_POSTER_ID,             GAVL_TYPE_LONG   },
        { META_WALLPAPER_ID,          GAVL_TYPE_LONG   },
        { META_NFO_ID,                GAVL_TYPE_LONG   },
        {/* End */  },
      },
      .arrays     = (const array_t[])
      {
        { GAVL_META_GENRE, "show_genres_arr", "show_genres" },
        {/* End */  },
      },
    },
    /* TV Sseasons */
    {
      .type       = TYPE_TV_SEASON,
      .table_name = "seasons",
      .pass       = 2,
      .cols       = (const column_t[])
      {
        { META_DB_ID,             GAVL_TYPE_LONG    },
        { META_PARENT_ID,           GAVL_TYPE_LONG    },
        { GAVL_META_SEASON,       GAVL_TYPE_INT     },
        { GAVL_META_APPROX_DURATION, GAVL_TYPE_LONG },
        { GAVL_META_DATE,         GAVL_TYPE_STRING  },
        { GAVL_META_NUM_CHILDREN, GAVL_TYPE_INT     },
        { META_POSTER_ID,             GAVL_TYPE_LONG   },
        { META_WALLPAPER_ID,           GAVL_TYPE_LONG   },
        {/* End */  },
      },
    },
    /* TV Episodes */
    {
      .type       = TYPE_TV_EPISODE,
      .table_name = "episodes",
      .pass       = 2,
      .cols       = (const column_t[])
      {
        { META_DB_ID,                GAVL_TYPE_LONG   },
        { META_PARENT_ID,            GAVL_TYPE_LONG   },
        { GAVL_META_TITLE,           GAVL_TYPE_STRING },
        { GAVL_META_SEARCH_TITLE,    GAVL_TYPE_STRING },
        { GAVL_META_EPISODENUMBER,   GAVL_TYPE_INT    },
        { GAVL_META_APPROX_DURATION, GAVL_TYPE_LONG },
        { GAVL_META_DATE,            GAVL_TYPE_STRING },
        { META_SCAN_DIR_ID,          GAVL_TYPE_LONG },
        { META_POSTER_ID,            GAVL_TYPE_LONG   },
        { META_WALLPAPER_ID,         GAVL_TYPE_LONG   },
        AUDIO_COLS,
        VIDEO_COLS,
        {/* End */  },
      },
      .src_cols = (const column_t[]){ FILE_COLS("episode"), { /* End */ }  },
    },
    /* Movies */
    {
      .type       = TYPE_MOVIE,
      .table_name = "movies",
      .pass       = 2,
      .cols       = (const column_t[])
      {
        { META_DB_ID,                GAVL_TYPE_LONG   },
        { GAVL_META_TITLE,           GAVL_TYPE_STRING },
        { GAVL_META_SEARCH_TITLE,    GAVL_TYPE_STRING },
        { GAVL_META_ORIGINAL_TITLE,  GAVL_TYPE_STRING },
        { GAVL_META_APPROX_DURATION, GAVL_TYPE_LONG   },
        { GAVL_META_PLOT,            GAVL_TYPE_STRING },
        { GAVL_META_DATE,            GAVL_TYPE_STRING },
        { META_POSTER_ID,            GAVL_TYPE_LONG   },
        { META_WALLPAPER_ID,         GAVL_TYPE_LONG   },
        { META_NFO_ID,               GAVL_TYPE_LONG   },
        AUDIO_COLS,
        VIDEO_COLS,
        {/* End */  },
      },

      .part_cols_table = "movie_parts",
      
      .arrays     = (const array_t[])
      {
        { GAVL_META_DIRECTOR,           "movie_directors_arr",          "movie_directors" },
        { GAVL_META_ACTOR,              "movie_actors_arr",             "movie_actors"    },
        { GAVL_META_GENRE,              "movie_genres_arr",             "movie_genres"    },
        { GAVL_META_COUNTRY,            "movie_countries_arr",          "movie_countries" },
        { GAVL_META_AUDIO_LANGUAGES,    "movie_audio_languages_arr",    "movie_audio_languages" },
        { GAVL_META_SUBTITLE_LANGUAGES, "movie_subtitle_languages_arr", "movie_subtitle_languages" },
        {/* End */  },
      },
    },
    /* Movie parts */
    {
      .type       = TYPE_MOVIE_PART,
      .table_name = "movie_parts",
      .pass       = 2,
      .cols       = (const column_t[])
      {
        { META_DB_ID,                GAVL_TYPE_LONG   },
        { META_SCAN_DIR_ID,          GAVL_TYPE_LONG   },
        { GAVL_META_APPROX_DURATION, GAVL_TYPE_LONG   },
        { GAVL_META_IDX,             GAVL_TYPE_INT    },
        { META_PARENT_ID,            GAVL_TYPE_LONG   },
        {/* End */  },
      },
      .src_cols = (const column_t[]){ FILE_COLS("movie"),
                                      { /* End */ }  },
      
    },
    { /* End */ }
  };



static const column_t * has_col(const obj_table_t * tab, const char * col)
  {
  int i = 0;
  
  while(tab->cols[i].name)
    {
    if(!strcmp(tab->cols[i].name, col))
      return &tab->cols[i];
    i++;
    }
  return NULL;
  }

#if 1
static const column_t * has_src_col(const obj_table_t * tab, const char * col)
  {
  int i = 0;

  if(!tab->src_cols)
    return NULL;
  
  while(tab->src_cols[i].name)
    {
    if(!strcmp(tab->src_cols[i].name, col))
      return &tab->src_cols[i];
    i++;
    }
  return NULL;
  }
#endif

static int has_array(const obj_table_t * tab, const char * col)
  {
  int i = 0;

  if(!tab->arrays)
    return 0;
  
  while(tab->arrays[i].name)
    {
    if(!strcmp(tab->arrays[i].name, col))
      return 1;
    i++;
    }
  return 0;
  }

static const obj_table_t * get_obj_table(type_id_t id)
  {
  int i = 0;
  while(obj_tables[i].table_name)
    {
    if(id == obj_tables[i].type)
      return &obj_tables[i];
    i++;
    }
  return NULL;
  }

static const struct
  {
  const gavl_type_t t;
  const char * label;
  }
type_names[] = 
  {
    { GAVL_TYPE_INT,    "INTEGER" },
    { GAVL_TYPE_LONG,   "INTEGER" },
    { GAVL_TYPE_STRING, "TEXT"    },
    { /* End */                   },
  };

static const char * get_type_label(gavl_type_t t)
  {
  int i = 0;
  while(type_names[i].label)
    {
    if(type_names[i].t == t)
      return type_names[i].label;
    i++;
    }
  return NULL;
  };


static void append_file_nocopy(gavl_array_t * arr, char * filename, int64_t mtime)
  {
  gavl_value_t val;
  gavl_dictionary_t * dict;

  gavl_value_init(&val);
  dict = gavl_value_set_dictionary(&val);

  gavl_dictionary_set_long(dict, GAVL_META_MTIME, mtime);
  gavl_dictionary_set_string_nocopy(dict, GAVL_META_URI, filename);

  gavl_array_splice_val_nocopy(arr, -1, 0, &val);
  }


typedef struct
  {
  /* Used by the callback for object queries */
  gavl_dictionary_t * obj;
  const obj_table_t * table;
  const char * tag; // For string arrays
  bg_mdb_backend_t * be;
  } query_t;

typedef struct
  {
  sqlite3 * db;

  int num_added;
  int num_removed;

  gavl_array_t add_containers;
  gavl_array_t update_containers;
  
  gavl_dictionary_t movies;
  gavl_dictionary_t series;
  gavl_dictionary_t albums;
  gavl_dictionary_t songs;

  const char * movies_id;
  const char * series_id;
  const char * albums_id;
  const char * songs_id;

  int have_params;
  
  } sqlite_priv_t;

static void set_image_type(bg_mdb_backend_t * b, int64_t image_id, int type)
  {
  char * sql;
  sqlite_priv_t * p = b->priv;
  sql = bg_sprintf("UPDATE images SET "META_IMAGE_TYPE" = %d WHERE "META_DB_ID" = %"PRId64";", type, image_id);
  bg_sqlite_exec(p->db, sql, NULL, NULL);
  }


static void lock_root_containers(bg_mdb_backend_t * b, int lock)
  {
  sqlite_priv_t * p = b->priv;

  if(p->movies_id)
    bg_mdb_track_lock(b, lock, &p->movies);
  
  if(p->series_id)
    bg_mdb_track_lock(b, lock, &p->series);

  if(p->albums_id)
    bg_mdb_track_lock(b, lock, &p->albums);

  if(p->songs_id)
    bg_mdb_track_lock(b, lock, &p->songs);
  }

static int create_tables(bg_mdb_backend_t * b)
  {
  char * sql;
  char * tmp_string;
  int i, j;

  sqlite_priv_t * p = b->priv;
  
  i = 0;

  /* Object table */  
  if(!bg_sqlite_exec(p->db, "CREATE TABLE objects("META_DB_ID" INTEGER PRIMARY KEY, TYPE INTEGER);", NULL, NULL) ||
     !bg_sqlite_exec(p->db, "CREATE TABLE scandirs(ID INTEGER PRIMARY KEY, PATH TEXT);", NULL, NULL))
    return 0;
  
  while(obj_tables[i].table_name)
    {
    sql = bg_sprintf("CREATE TABLE %s(", obj_tables[i].table_name);

    j = 0;

    while(obj_tables[i].cols[j].name)
      {
      if(j)
        sql = gavl_strcat(sql, ", ");

      if(obj_tables[i].cols[j].id_table)
        tmp_string = bg_sprintf("\"%s\" INTEGER", obj_tables[i].cols[j].name);
      else
        tmp_string = bg_sprintf("\"%s\" %s",
                                obj_tables[i].cols[j].name, get_type_label(obj_tables[i].cols[j].type));

      sql = gavl_strcat(sql, tmp_string);
      free(tmp_string);
      
      if(!j)
        sql = gavl_strcat(sql, " PRIMARY KEY");
      
      j++;
      }

    if(obj_tables[i].src_cols)
      {
      j = 0;
      
      while(obj_tables[i].src_cols[j].name)
        {
        sql = gavl_strcat(sql, ", ");

        if(obj_tables[i].src_cols[j].id_table)
          tmp_string = bg_sprintf("\"%s\" INTEGER", obj_tables[i].src_cols[j].name);
        else
          tmp_string = bg_sprintf("\"%s\" %s",
                                  obj_tables[i].src_cols[j].name, get_type_label(obj_tables[i].src_cols[j].type));
        
        sql = gavl_strcat(sql, tmp_string);
        free(tmp_string);
        j++;
        }
      }
    
    sql = gavl_strcat(sql, ");");

    if(!bg_sqlite_exec(p->db, sql, NULL, NULL))
      {
      free(sql);
      return 0;
      }
    free(sql);
    
    /* Arrays */

    if(obj_tables[i].arrays)
      {
      j = 0;
      
      while(obj_tables[i].arrays[j].name)
        {
        sql = bg_sprintf("CREATE TABLE IF NOT EXISTS %s(ID INTEGER PRIMARY KEY, NAME TEXT);",
                         obj_tables[i].arrays[j].id_table_name);

        if(!bg_sqlite_exec(p->db, sql, NULL, NULL))
          return 0;
        
        free(sql);

        sql = bg_sprintf("CREATE TABLE IF NOT EXISTS %s(ID INTEGER PRIMARY KEY, OBJ_ID INTEGER, NAME_ID INTEGER);",
                         obj_tables[i].arrays[j].array_table_name);

        if(!bg_sqlite_exec(p->db, sql, NULL, NULL))
          return 0;
        
        free(sql);

        j++;
        }
      }
    

    /* ID tables */
    j = 0;
    while(obj_tables[i].cols[j].name)
      {
      if(obj_tables[i].cols[j].id_table)
        {
        sql = bg_sprintf("CREATE TABLE IF NOT EXISTS %s(ID INTEGER PRIMARY KEY, NAME TEXT);",
                         obj_tables[i].cols[j].id_table);
        if(!bg_sqlite_exec(p->db, sql, NULL, NULL))
          return 0;
        
        free(sql);
        
        }
      j++;
      }

    if(obj_tables[i].src_cols)
      {
      j = 0;
      while(obj_tables[i].src_cols[j].name)
        {
        if(obj_tables[i].src_cols[j].id_table)
          {
          sql = bg_sprintf("CREATE TABLE IF NOT EXISTS %s(ID INTEGER PRIMARY KEY, NAME TEXT);",
                           obj_tables[i].src_cols[j].id_table);

          if(!bg_sqlite_exec(p->db, sql, NULL, NULL))
            return 0;
          free(sql);
          }
        j++;
        }
      }
    
    /* 
    const char * part_cols_table;
    const column_t * part_cols;
    const column_t * part_src_cols; // Taken from src[0]
    */
    
    i++;
    }
  return 1;
  }

static void append_cols(sqlite_priv_t * p,
                        const gavl_dictionary_t * dict, const column_t * cols,
                        char ** sql, char ** sql2, int first)
  {
  int i = 0;
  int num = 0;
  const gavl_value_t * val;
  char * tmp_string;

  if(first)
    num = 0;
  else
    num = 1;
  
  while(cols[i].name)
    {
    if(!(val = gavl_dictionary_get(dict, cols[i].name)))
      {
      i++;
      continue;
      }
    
    switch(cols[i].type)
      {
      case GAVL_TYPE_INT:
        {
        int val_i;
        if(gavl_value_get_int(val, &val_i))
          {
          if(num)
            *sql = gavl_strcat(*sql, ", ");
          *sql = gavl_strcat(*sql, cols[i].name);

          if(num)
            *sql2 = gavl_strcat(*sql2, ", ");
          
          
          tmp_string = sqlite3_mprintf("%d", val_i);
          
          *sql2 = gavl_strcat(*sql2, tmp_string);
          sqlite3_free(tmp_string);
          num++;
          }
        }
        break;
      case GAVL_TYPE_LONG:
        {
        int64_t val_l;
        if(gavl_value_get_long(val, &val_l))
          {
          if(num)
            *sql = gavl_strcat(*sql, ", ");
          *sql = gavl_strcat(*sql, cols[i].name);

          if(num)
            *sql2 = gavl_strcat(*sql2, ", ");
          
          tmp_string = sqlite3_mprintf("%" PRId64, val_l);

          *sql2 = gavl_strcat(*sql2, tmp_string);
          sqlite3_free(tmp_string);
          num++;
          }
        }
        break;
      case GAVL_TYPE_STRING:
        {
        const char * val_s;
        if((val_s = gavl_value_get_string(val)))
          {
          if(num)
            *sql = gavl_strcat(*sql, ", ");
          *sql = gavl_strcat(*sql, cols[i].name);
          
          if(num)
            *sql2 = gavl_strcat(*sql2, ", ");
          
          if(cols[i].id_table)
            tmp_string = sqlite3_mprintf("%"PRId64,
                                         bg_sqlite_string_to_id_add(p->db,
                                                                    cols[i].id_table,
                                                                    "ID", "NAME", val_s));
          else
            tmp_string = sqlite3_mprintf("%Q", val_s);
          
          *sql2 = gavl_strcat(*sql2, tmp_string);
          sqlite3_free(tmp_string);
          num++;
          }
        }
        break;
      default:
        break;
      }
    i++;
    }
  }

static void append_array(sqlite_priv_t * p,
                         const gavl_dictionary_t * dict, const array_t * arr, int64_t object_id)
  {
  int j = 0, result;
  int64_t string_id;
  int64_t row_id = -1;
  char * sql;
  
  const char * str;
  
  while((str = gavl_dictionary_get_string_array(dict, arr->name, j)))
    {
    //    if(!strcmp(arr->name, GAVL_META_SUBTITLE_LANGUAGES))
    //      fprintf(stderr, "Blupp\n");
    
    
    string_id = bg_sqlite_string_to_id_add(p->db, arr->id_table_name, "ID", "NAME", str);


    if(row_id < 0)
      row_id = bg_sqlite_get_max_int(p->db, arr->array_table_name, "ID");

    row_id++;

    sql = sqlite3_mprintf("INSERT INTO %s (ID, OBJ_ID, NAME_ID) VALUES (%"PRId64", %"PRId64", %"PRId64"); ",
                          arr->array_table_name, row_id, object_id, string_id);
    
    result = bg_sqlite_exec(p->db, sql, NULL, NULL);
    sqlite3_free(sql);
    if(!result)
      return;
    
    j++;
    }
  }

static int64_t create_object(sqlite_priv_t * p, type_id_t type)
  {
  int result;
  char * sql;
  int64_t id;

  if((id = bg_sqlite_get_max_int(p->db, "objects", META_DB_ID)) < 0)
    return -1;

  id++;

  sql = sqlite3_mprintf("INSERT INTO OBJECTS (" META_DB_ID ", TYPE) VALUES (%"PRId64", %d); ",
                        id, type);

  result = bg_sqlite_exec(p->db, sql, NULL, NULL);
  sqlite3_free(sql);
  if(!result)
    return -1;
  
  p->num_added++;
  
  return id;
  }

/* Automatically create containers for some types */

static void set_dict_value(bg_mdb_backend_t * be,
                           gavl_dictionary_t * dict,
                           const column_t * col,
                           const char * val)
  {
  sqlite_priv_t * p;
  
  if(!val)
    return;

  p = be->priv;
  
  switch(col->type)
    {
    case GAVL_TYPE_INT:
      {
      int val_i = atoi(val);
      gavl_dictionary_set_int(dict, col->name, val_i);
      }
      break;
    case GAVL_TYPE_LONG:
      {
      int64_t val_l = strtoll(val, NULL, 10);
      gavl_dictionary_set_long(dict, col->name, val_l);
      }
      break;
    case GAVL_TYPE_STRING:
      if(col->id_table)
        {
        int64_t val_l = strtoll(val, NULL, 10);
        gavl_dictionary_set_string_nocopy(dict, col->name,
                                          bg_sqlite_id_to_string(p->db, col->id_table,
                                                                 "NAME", "ID", val_l));
        }
      else
        gavl_dictionary_set_string(dict, col->name, val);
      break;
    default:
      break;
    }
  }

static int query_object_callback(void * data, int argc, char **argv, char **azColName)
  {
  int i;
  int j;
  gavl_dictionary_t * m;
  query_t * q = data;
  
  q->obj = gavl_dictionary_create();
  m = gavl_dictionary_get_dictionary_create(q->obj, GAVL_META_METADATA);
  
  for(i = 0; i < argc; i++)
    {
    j = 0;
    while(q->table->cols[j].name)
      {
      if(!strcasecmp(azColName[i], q->table->cols[j].name))
        {
        set_dict_value(q->be, m, &q->table->cols[j], argv[i]);
        break;
        }
      j++;
      }
    }
  return 0;
  }

static int query_object_callback_full(void * data, int argc, char **argv, char **azColName)
  {
  int arg_idx;
  int col_idx;

  gavl_dictionary_t * m;
  query_t * q = data;
  
  q->obj   = gavl_dictionary_create();

  m = gavl_dictionary_get_dictionary_create(q->obj, GAVL_META_METADATA);
  
  arg_idx = 0;
  col_idx = 0;
  while(q->table->cols[col_idx].name)
    {
    set_dict_value(q->be, m, &q->table->cols[col_idx], argv[arg_idx]);
    col_idx++;
    arg_idx++;
    }

  if(q->table->src_cols)
    {
    gavl_dictionary_t * src;

    col_idx = 0;
    src = gavl_metadata_add_src(m, GAVL_META_SRC, NULL, NULL);
    
    while(q->table->src_cols[col_idx].name)
      {
      set_dict_value(q->be, src, &q->table->src_cols[col_idx], argv[arg_idx]);
      col_idx++;
      arg_idx++;
      }
    }
  return 0;
  }


static int query_array_callback(void * data, int argc, char **argv, char **azColName)
  {
  char * tmp_string;
  
  gavl_dictionary_t * m;
  query_t * q = data;
  m = gavl_track_get_metadata_nc(q->obj);
  gavl_dictionary_append_string_array(m, q->tag, argv[0]);

  tmp_string = bg_sprintf("%s"GAVL_META_ID, q->tag);
  gavl_dictionary_append_string_array(m, tmp_string, argv[1]);
  free(tmp_string);
  
  return 0;
  }

static void set_image_url(gavl_dictionary_t * m,
                          const gavl_dictionary_t * image_obj,
                          const char * key)
  {
  gavl_dictionary_t * src;
  src = gavl_metadata_add_src(m, key, NULL, NULL);
  
  image_obj = gavl_track_get_src(image_obj, GAVL_META_SRC, 0, NULL, NULL);
  
  gavl_dictionary_copy(src, image_obj);
  }


/*
      sql = sqlite3_mprintf("SELECT "GAVL_META_APPROX_DURTION", "
                            GAVL_META_URI", "
                            META_MIMETYPE_ID", "
                            GAVL_META_MTIME" FROM movie_parts WHERE "
                            META_PARENT_ID" = %"PRId64" ORDER BY "GAVL_META_IDX";", id);
  
  
 */

typedef struct
  {
  gavl_dictionary_t * obj;
  sqlite3 * db;
  } query_part_t;
  

static int query_part_callback(void * data, int argc, char **argv, char **azColName)
  {
  
  gavl_dictionary_t * part;
  
  //  gavl_dictionary_t * m;
  char * mimetype;
  query_part_t * q = data;
  int64_t mimetype_id;
  int64_t duration;
  int64_t mtime;
  const char * uri;
  gavl_dictionary_t * src;
  
  duration    = strtoll(argv[0], NULL, 10);
  uri         = argv[1];
  mimetype_id = strtoll(argv[2], NULL, 10);
  mtime       = strtoll(argv[3], NULL, 10);
  
  mimetype = bg_sqlite_id_to_string(q->db, "movie_mimetypes", "NAME", "ID", mimetype_id);
  
  //  m = gavl_track_get_metadata_nc(q->obj);
  
  //  src = gavl_metadata_get_src(m, GAVL_META_SRC, 0, NULL, NULL);
  //  parts = gavl_dictionary_get_array_nc(m, GAVL_META_PARTS);

  part = gavl_track_append_part(q->obj, mimetype, uri);
  src = gavl_track_get_src_nc(part, GAVL_META_SRC, 0);
  gavl_dictionary_set_long(src, GAVL_META_MTIME, mtime);
  gavl_dictionary_set_long(src, GAVL_META_APPROX_DURATION, duration);
  
  if(mimetype)
    free(mimetype);
  
  return 0;
  }

static void query_images(bg_mdb_backend_t * b,
                         gavl_dictionary_t * m)
  {
  query_t q1;
  int64_t sub_id;
  char * sql;
  sqlite_priv_t * p = b->priv;

  memset(&q1, 0, sizeof(q1));
  q1.be = b;
  
  /* Cover */
  if(gavl_dictionary_get_long(m, META_COVER_ID, &sub_id) && (sub_id > 0))
    {
    q1.obj = NULL;
    q1.table = get_obj_table(TYPE_IMAGE);
    
    sql = sqlite3_mprintf("SELECT * FROM images WHERE "META_DB_ID" = %"PRId64";", sub_id);
    bg_sqlite_exec(p->db, sql, query_object_callback_full, &q1);
    
    sqlite3_free(sql);

    
    if(q1.obj)
      {
      set_image_url(m, q1.obj, GAVL_META_COVER_URL);
      gavl_dictionary_destroy(q1.obj);
      q1.obj = NULL;
      }
    }
  /* Poster */
  if(gavl_dictionary_get_long(m, META_POSTER_ID, &sub_id) && (sub_id > 0))
    {
    q1.obj = NULL;
    q1.table = get_obj_table(TYPE_IMAGE);
    
    sql = sqlite3_mprintf("SELECT * FROM images WHERE "META_DB_ID" = %"PRId64";", sub_id);
    bg_sqlite_exec(p->db, sql, query_object_callback_full, &q1);
    sqlite3_free(sql);
    if(q1.obj)
      {
      set_image_url(m, q1.obj, GAVL_META_POSTER_URL);
      gavl_dictionary_destroy(q1.obj);
      q1.obj = NULL;
      }
    }
  /* Wallapaper */
  if(gavl_dictionary_get_long(m, META_WALLPAPER_ID, &sub_id) && (sub_id > 0))
    {
    q1.obj = NULL;
    q1.table = get_obj_table(TYPE_IMAGE);

    sql = sqlite3_mprintf("SELECT * FROM images WHERE "META_DB_ID" = %"PRId64";", sub_id);
    bg_sqlite_exec(p->db, sql, query_object_callback_full, &q1);
    sqlite3_free(sql);
    if(q1.obj)
      {
      set_image_url(m, q1.obj, GAVL_META_WALLPAPER_URL);
      gavl_dictionary_destroy(q1.obj);
      q1.obj = NULL;
      }
    }

  }

static gavl_dictionary_t * query_sqlite_object(bg_mdb_backend_t * b, int64_t id, type_id_t type)
  {
  char * sql;
  int result;
  sqlite_priv_t * p = b->priv;
  query_t q;
  query_t q1;
  gavl_dictionary_t * m;
  int64_t sub_id;

  
  memset(&q, 0, sizeof(q));
  memset(&q1, 0, sizeof(q1));

  q.be = b;
  q1.be = b;
  
  /* Get type */

  if(type < 0)
    type = bg_sqlite_id_to_id(p->db, "OBJECTS", "TYPE", "ID", id);
  
  if((type < 0) || !(q.table = get_obj_table(type)))
    return 0;
  
  /* Query object */
  sql = sqlite3_mprintf("SELECT * FROM %s WHERE "META_DB_ID" = %"PRId64";", q.table->table_name, id);

  result = bg_sqlite_exec(p->db, sql, query_object_callback_full, &q);
  sqlite3_free(sql);
  if(!result)
    return NULL;

  /* Query arrays */
  if(q.table->arrays)
    {
    int i = 0;
    while(q.table->arrays[i].name)
      {
      /* SELECT show_genres.NAME FROM
         show_genres_arr INNER JOIN show_genres ON show_genres.ID = show_genres_arr.NAME_ID
         WHERE show_genres_arr.OBJ_ID = 2
         ORDER BY show_genres_arr.ID; */
      
      sql = sqlite3_mprintf("SELECT %s.NAME, %s.ID FROM "
                            "%s INNER JOIN %s ON %s.ID = %s.NAME_ID "
                            "WHERE %s.OBJ_ID = %"PRId64" ORDER BY %s.ID;",
                            q.table->arrays[i].id_table_name,
                            q.table->arrays[i].id_table_name,
                            q.table->arrays[i].array_table_name,
                            q.table->arrays[i].id_table_name,
                            q.table->arrays[i].id_table_name,
                            q.table->arrays[i].array_table_name,
                            q.table->arrays[i].array_table_name,
                            id,
                            q.table->arrays[i].array_table_name);

      //      fprintf(stderr, "Query array\n%s\n", sql);
      
      q.tag = q.table->arrays[i].name;
      
      result = bg_sqlite_exec(p->db, sql, query_array_callback, &q);
      sqlite3_free(sql);
      if(!result)
        return 0;
      
      i++;
      }
    }

  if(!q.obj)
    return NULL;
  
  m = gavl_track_get_metadata_nc(q.obj);
  
  /* Image URIs */

  query_images(b, m);
  /* NFO */
  if(gavl_dictionary_get_long(m, META_NFO_ID, &sub_id) && (sub_id > 0))
    {
    
    }
  
  if(gavl_dictionary_get_long(m, META_PARENT_ID, &sub_id) && (sub_id > 0))
    {
    gavl_dictionary_set_string_nocopy(m, GAVL_META_ALBUM,
                                      bg_sqlite_id_to_string(p->db, "albums",
                                                             GAVL_META_TITLE, META_DB_ID, sub_id));
    }

  /*
    const char * mimetype;

  */
  
  /* Parts */
  
  if(q.obj)
    {
    if(type == TYPE_MOVIE)
      {
      query_part_t qp;
      qp.db = p->db;
      qp.obj = q.obj;

      sql = sqlite3_mprintf("SELECT "GAVL_META_APPROX_DURATION", "
                            GAVL_META_URI", "
                            GAVL_META_MIMETYPE", "
                            GAVL_META_MTIME" FROM movie_parts WHERE "
                            META_PARENT_ID" = %"PRId64" ORDER BY "GAVL_META_IDX";", id);
      bg_sqlite_exec(p->db, sql, query_part_callback, &qp);
      sqlite3_free(sql);
      
      gavl_dictionary_set_string(gavl_track_get_metadata_nc(q.obj), GAVL_META_CLASS,
                                 GAVL_META_CLASS_MOVIE);

      //      fprintf(stderr, "Got movie:\n");
      //      gavl_dictionary_dump(q.obj, 2);
      }
    else
      gavl_dictionary_set_string(gavl_track_get_metadata_nc(q.obj),
                                 GAVL_META_CLASS, get_type_class(type));
    }

  return q.obj;
  }

typedef struct
  {
  char * sql;
  const obj_table_t * tab;
  } update_foreach_t;

static void update_foreach_func(void * priv, const char * name,
                                const gavl_value_t * val)
  {
  char * tmp_string = NULL;
  update_foreach_t * f = priv;

  if(!strcmp(name, META_DB_ID))
    return;

  if(!strcmp(name, GAVL_META_NUM_CONTAINER_CHILDREN))
    return;
  
  if(!strcmp(name, GAVL_META_NUM_ITEM_CHILDREN))
    return;
  
  switch(val->type)
    {
    case GAVL_TYPE_INT:
      tmp_string = sqlite3_mprintf("%s = %d", name, val->v.i);
      break;
    case GAVL_TYPE_LONG:
      tmp_string = sqlite3_mprintf("%s = %"PRId64, name, val->v.l);
      break;
    case GAVL_TYPE_STRING:
      tmp_string = sqlite3_mprintf("%s = %Q", name, val->v.str);
      break;
    default:
      return;
    }

  if(!f->sql)
    f->sql = bg_sprintf("UPDATE %s SET ", f->tab->table_name);
  else
    f->sql = gavl_strcat(f->sql, ", ");

  f->sql = gavl_strcat(f->sql, tmp_string);

  sqlite3_free(tmp_string);

  }

static void update_object(bg_mdb_backend_t * b,
                          const gavl_dictionary_t * dict,
                          const obj_table_t * tab)
  {
  int64_t id;
  update_foreach_t f;
  char * tmp_string;
  int result;
  sqlite_priv_t * p = b->priv;
  const gavl_dictionary_t * m = gavl_track_get_metadata(dict);

  gavl_dictionary_get_long(m, META_DB_ID, &id);
  
  f.sql = NULL;
  f.tab = tab;
  
  gavl_dictionary_foreach(m, update_foreach_func, &f);

  tmp_string = bg_sprintf(" WHERE "META_DB_ID" = %"PRId64";", id);
  f.sql = gavl_strcat(f.sql, tmp_string);
  free(tmp_string);

  result = bg_sqlite_exec(p->db, f.sql, NULL, NULL);
  free(f.sql);
  if(!result)
    return;
  }

static int64_t add_child_album(bg_mdb_backend_t * b, gavl_dictionary_t * dict)
  {
  const char * title;
  const char * artist;
  const gavl_dictionary_t * m;
  int result;
  char * sql;
  int64_t artist_id;
  int64_t album_id;
  gavl_dictionary_t album_dict;
  gavl_dictionary_t * album_m;
  
  sqlite_priv_t * p = b->priv;

  query_t q;
  memset(&q, 0, sizeof(q));
  q.be = b;

  //  fprintf(stderr, "add child album:\n");
  //  gavl_dictionary_dump(dict, 2);
  
  m = gavl_track_get_metadata(dict);
  
  title  = gavl_dictionary_get_string(m, GAVL_META_ALBUM);
  artist = gavl_dictionary_get_string_array(m, GAVL_META_ALBUMARTIST, 0);

  artist_id = bg_sqlite_string_to_id(p->db, "album_artists", "ID", "NAME", artist);
  if(artist_id < 0)
    goto create_new;
  
  /* Query db */
  
  q.table = get_obj_table(TYPE_ALBUM);
  
  sql = sqlite3_mprintf("SELECT "META_DB_ID", "
                        GAVL_META_APPROX_DURATION", "
                        GAVL_META_DATE", "
                        GAVL_META_NUM_CHILDREN" FROM albums WHERE "
                        META_DB_ID" IN (SELECT OBJ_ID FROM album_artists_arr WHERE NAME_ID = %"PRId64") "
                        "AND "GAVL_META_TITLE" = %Q;", artist_id, title);
  
  result = bg_sqlite_exec(p->db, sql, query_object_callback, &q);

  sqlite3_free(sql);
  if(!result)
    goto create_new;

  else if(q.obj)
    {
    const char * album_date;
    const char * song_date;
    int num = 0;
    gavl_time_t container_duration = 0;
    gavl_time_t child_duration = 0;
    
    album_m = gavl_track_get_metadata_nc(q.obj);

    gavl_dictionary_get_int(album_m, GAVL_META_NUM_CHILDREN, &num);
    num++;
    gavl_dictionary_set_int(album_m, GAVL_META_NUM_CHILDREN, num);
    
    if((album_date = gavl_dictionary_get_string(album_m, GAVL_META_DATE)) &&
       (song_date = gavl_dictionary_get_string(m, GAVL_META_DATE)) &&
       strcmp(album_date, DATE_UNDEFINED) &&
       strcmp(album_date, song_date))
      {
      gavl_dictionary_set_string(album_m, GAVL_META_DATE, DATE_UNDEFINED);
      }

    /* Duration */
    gavl_dictionary_get_long(album_m, GAVL_META_APPROX_DURATION, &container_duration);
    gavl_dictionary_get_long(m, GAVL_META_APPROX_DURATION, &child_duration);
    container_duration += child_duration;
    gavl_dictionary_set_long(album_m, GAVL_META_APPROX_DURATION, container_duration);

    gavl_dictionary_get_long(album_m, META_DB_ID, &album_id);
    
    update_object(b, q.obj, q.table);

    gavl_dictionary_destroy(q.obj);
    q.obj = NULL;
    return album_id;
    }
  
  create_new:
  
  gavl_dictionary_init(&album_dict);

  album_m = gavl_dictionary_get_dictionary_create(&album_dict, GAVL_META_METADATA);
  gavl_dictionary_set_string(album_m, GAVL_META_CLASS,
                             GAVL_META_CLASS_MUSICALBUM);
  
  album_id = create_object(p, TYPE_ALBUM);
  
  bg_plugin_registry_get_container_data(bg_plugin_reg, &album_dict, dict);

  gavl_track_set_num_children(&album_dict, 0, 1);
  
  gavl_dictionary_set(album_m, GAVL_META_APPROX_DURATION , gavl_dictionary_get(m, GAVL_META_APPROX_DURATION));
  gavl_dictionary_set_long(album_m, META_DB_ID, album_id);
  add_object(b, &album_dict, -1, album_id);
  
  gavl_dictionary_free(&album_dict);
  
  return album_id;
  }

static int64_t add_child_tv_show(bg_mdb_backend_t * b, gavl_dictionary_t * episode)
  {
  /* TODO: Read .nfo file */
  int64_t show_id;
  const gavl_dictionary_t * m;
  int result;
  char * sql;
  const char * show;
  gavl_dictionary_t show_dict;
  gavl_dictionary_t * show_m;

  sqlite_priv_t * p = b->priv;
  query_t q;
  memset(&q, 0, sizeof(q));
  q.be = b;
  
  m = gavl_track_get_metadata(episode);

  q.table = get_obj_table(TYPE_TV_SHOW);
  
  if(!(show = gavl_dictionary_get_string(m, GAVL_META_SHOW)))
    return -1;
  
  sql = sqlite3_mprintf("SELECT "META_DB_ID", "
                        GAVL_META_NUM_CHILDREN" FROM shows WHERE "
                        GAVL_META_TITLE" = %Q;", show);
  
  result = bg_sqlite_exec(p->db, sql, query_object_callback, &q);
  sqlite3_free(sql);
  if(!result)
    goto create_new;
  else if(q.obj)
    {
    int num = 0;
    gavl_dictionary_t * show_m = gavl_track_get_metadata_nc(q.obj);

    gavl_dictionary_get_int(show_m, GAVL_META_NUM_CHILDREN, &num);
    num++;
    gavl_dictionary_set_int(show_m, GAVL_META_NUM_CHILDREN, num);
    
    gavl_dictionary_get_long(show_m, META_DB_ID, &show_id);
    
    update_object(b, q.obj, q.table);

    gavl_dictionary_destroy(q.obj);
    q.obj = NULL;
    return show_id;
    }
  
  create_new:

  gavl_dictionary_init(&show_dict);
  show_m = gavl_dictionary_get_dictionary_create(&show_dict, GAVL_META_METADATA);

  gavl_dictionary_set_string(show_m, GAVL_META_CLASS,
                             GAVL_META_CLASS_TV_SHOW);
  
  show_id = create_object(p, TYPE_TV_SHOW);

  bg_plugin_registry_get_container_data(bg_plugin_reg, &show_dict, episode);
  
  gavl_track_set_num_children(&show_dict, 1, 0);
  
  gavl_dictionary_set_long(show_m, META_DB_ID, show_id);
  
  // gavl_dictionary_set(season_m, GAVL_META_GENRE, gavl_dictionary_get(m, GAVL_META_GENRE));
  
  add_object(b, &show_dict, -1, show_id);
  gavl_dictionary_free(&show_dict);
  
  return show_id;
  }

static int64_t add_child_tv_season(bg_mdb_backend_t * b, gavl_dictionary_t * episode)
  {
  const char * show;
  int season = -1;
  int64_t show_id;
  int64_t season_id;
  
  const gavl_dictionary_t * m;
  int result;
  char * sql;
  gavl_dictionary_t season_dict;
  gavl_dictionary_t * season_m;
  
  sqlite_priv_t * p = b->priv;
  query_t q;
  memset(&q, 0, sizeof(q));
  q.be = b;
  
  m = gavl_track_get_metadata(episode);

  if(!(show = gavl_dictionary_get_string(m, GAVL_META_SHOW)) ||
     !gavl_dictionary_get_int(m, GAVL_META_SEASON, &season))
    return -1;
  
  show_id = bg_sqlite_string_to_id(p->db, "shows", META_DB_ID, GAVL_META_TITLE, show);
  if(show_id < 0)
    goto create_new;
  
  /* Query db */
  
  q.table = get_obj_table(TYPE_TV_SEASON);
  
  sql = sqlite3_mprintf("SELECT "META_DB_ID", "
                        GAVL_META_APPROX_DURATION", "
                        GAVL_META_DATE", "
                        GAVL_META_NUM_CHILDREN" FROM seasons WHERE "
                        META_PARENT_ID" = %"PRId64" AND "GAVL_META_SEASON" = %d;",
                        show_id, season);
  
  result = bg_sqlite_exec(p->db, sql, query_object_callback, &q);
  sqlite3_free(sql);
  if(!result)
    goto create_new;

  else if(q.obj)
    {
    const char * season_date;
    const char * episode_date;
    int num = 0;
    gavl_time_t container_duration = 0;
    gavl_time_t child_duration = 0;
    
    season_m = gavl_track_get_metadata_nc(q.obj);

    gavl_dictionary_get_int(season_m, GAVL_META_NUM_CHILDREN, &num);
    num++;
    gavl_dictionary_set_int(season_m, GAVL_META_NUM_CHILDREN, num);
    
    if((season_date = gavl_dictionary_get_string(season_m, GAVL_META_DATE)) &&
       (episode_date = gavl_dictionary_get_string(m, GAVL_META_DATE)))
      {
      if(strcmp(episode_date, season_date) < 0)
        gavl_dictionary_set_string(season_m, GAVL_META_DATE, season_date);
      }
    
    /* Duration */
    gavl_dictionary_get_long(season_m, GAVL_META_APPROX_DURATION, &container_duration);
    gavl_dictionary_get_long(m, GAVL_META_APPROX_DURATION, &child_duration);
    container_duration += child_duration;
    gavl_dictionary_set_long(season_m, GAVL_META_APPROX_DURATION, container_duration);

    gavl_dictionary_get_long(season_m, META_DB_ID, &season_id);
    
    update_object(b, q.obj, q.table);

    gavl_dictionary_destroy(q.obj);
    q.obj = NULL;
    return season_id;
    }
  
  create_new:

  show_id = add_child_tv_show(b, episode);
  
  gavl_dictionary_init(&season_dict);
  season_m = gavl_dictionary_get_dictionary_create(&season_dict, GAVL_META_METADATA);
  gavl_dictionary_set_long(season_m, META_DB_ID, season_id);
  gavl_dictionary_set_long(season_m, META_PARENT_ID, show_id);

  gavl_dictionary_set_string(season_m, GAVL_META_CLASS, GAVL_META_CLASS_TV_SEASON);
  
  season_id = create_object(p, TYPE_TV_SEASON);
  
  bg_plugin_registry_get_container_data(bg_plugin_reg, &season_dict, episode);
  
  gavl_dictionary_set(season_m, GAVL_META_DATE, gavl_dictionary_get(m, GAVL_META_DATE));
  
  gavl_track_set_num_children(&season_dict, 0, 1);
  
  gavl_dictionary_set(season_m, GAVL_META_APPROX_DURATION , gavl_dictionary_get(m, GAVL_META_APPROX_DURATION));
  
  add_object(b, &season_dict, -1, season_id);
  
  gavl_dictionary_free(&season_dict);
  
  return season_id;
  // #endif
  //  return -1;
  }

static int64_t add_movie_part(bg_mdb_backend_t * b, const gavl_dictionary_t * part_track)
  {
  const gavl_dictionary_t * part_m;
  int result;
  char * sql;
  query_t q;
  const char * title;
  const char * date;
  int64_t movie_id;

  gavl_dictionary_t movie_dict;

  gavl_dictionary_t * movie_m;
  sqlite_priv_t * p = b->priv;
  
  part_m = gavl_track_get_metadata(part_track);
  
  memset(&q, 0, sizeof(q));
  q.be = b;
  
  /* Query db */

  title = gavl_dictionary_get_string(part_m, GAVL_META_TITLE);
  date = gavl_dictionary_get_string(part_m, GAVL_META_DATE);
  
  q.table = get_obj_table(TYPE_MOVIE);
  
  sql = sqlite3_mprintf("SELECT "META_DB_ID", "
                        GAVL_META_APPROX_DURATION" FROM movies WHERE "
                        GAVL_META_TITLE" = %Q AND "GAVL_META_DATE" = %Q;",
                        title, date);

  result = bg_sqlite_exec(p->db, sql, query_object_callback, &q);
  sqlite3_free(sql);

  if(!result)
    goto create_new;
  
  else if(q.obj)
    {
    gavl_time_t movie_duration = 0;
    gavl_time_t part_duration = 0;
    
    movie_m = gavl_track_get_metadata_nc(q.obj);
    gavl_dictionary_get_long(movie_m, META_DB_ID, &movie_id);
    
    /* Duration */
    gavl_dictionary_get_long(movie_m, GAVL_META_APPROX_DURATION, &movie_duration);
    gavl_dictionary_get_long(part_m, GAVL_META_APPROX_DURATION, &part_duration);
    movie_duration += part_duration;
    gavl_dictionary_set_long(movie_m, GAVL_META_APPROX_DURATION, movie_duration);
    update_object(b, q.obj, q.table);

    gavl_dictionary_destroy(q.obj);
    q.obj = NULL;
    
    return movie_id;
    }
  
  create_new:

  gavl_dictionary_init(&movie_dict);

  gavl_dictionary_copy(&movie_dict, part_track);

  movie_m = gavl_track_get_metadata_nc(&movie_dict);
  gavl_dictionary_set(movie_m, GAVL_META_SRC, NULL);

  gavl_dictionary_set_string(movie_m, GAVL_META_CLASS, GAVL_META_CLASS_MOVIE);
  
  movie_id = create_object(p, TYPE_MOVIE);
  add_object(b, &movie_dict, -1, movie_id);
  gavl_dictionary_free(&movie_dict);
  
  return movie_id;
  }

static void delete_objects(bg_mdb_backend_t * b,
                           const gavl_array_t * arr,
                           int del_flags)
  {
  int i;
  
  const gavl_dictionary_t * dict;

  //  fprintf(stderr, "delete_objects: %p\n", arr);
  //  gavl_array_dump(arr, 2);
  
  for(i = 0; i < arr->num_entries; i++)
    {
    if((dict = gavl_value_get_dictionary(&arr->entries[i])))
      delete_object(b, dict, del_flags);
    }
  }

typedef struct
  {
  type_id_t type;
  gavl_array_t * ret;
  } get_related_array_t;

static int get_related_array_cb(void * data, int argc, char **argv, char **azColName)
  {
  int i;
  gavl_value_t val;
  gavl_dictionary_t * dict;
  get_related_array_t * q = data;

  gavl_value_init(&val);
  dict = gavl_value_set_dictionary(&val);
  
  for(i = 0; i < argc; i++)
    {
    if(!strcmp(azColName[i], GAVL_META_URI))
      {
      gavl_dictionary_set_string(dict, GAVL_META_URI, argv[i]);
      }
    else if(!strcmp(azColName[i], META_DB_ID))
      {
      gavl_dictionary_set_long(dict, META_DB_ID, strtoll(argv[i], NULL, 10));
      }
    }
  gavl_dictionary_set_int(dict, "TYPE", q->type);
  gavl_array_splice_val_nocopy(q->ret, -1, 0, &val);
  return 0;
  }

static void get_related_array(bg_mdb_backend_t * b,
                              gavl_array_t * ret, const char * tag, int64_t id)
  {
  int i;
  get_related_array_t priv;
  char * sql;
  sqlite_priv_t * p = b->priv;
  
  memset(&priv, 0, sizeof(priv));
  priv.ret = ret;
  
  i = 0;

  while(obj_tables[i].table_name)
    {
    if(!has_col(&obj_tables[i], tag))
      {
      i++;
      continue;
      }
    
    priv.type = obj_tables[i].type;
    if(has_src_col(&obj_tables[i], GAVL_META_URI))
      {
      sql = bg_sprintf("SELECT "META_DB_ID", "GAVL_META_URI" from %s WHERE %s = %"PRId64";",
                       obj_tables[i].table_name, tag, id);
      }
    else
      {
      sql = bg_sprintf("SELECT "META_DB_ID" from %s WHERE %s = %"PRId64";",
                       obj_tables[i].table_name, tag, id);
      }

    //    fprintf(stderr, "get_realated_array: %s\n", sql);
    
    if(!bg_sqlite_exec(p->db, sql, get_related_array_cb, &priv))
      {
      free(sql);
      return;
      }

    //    fprintf(stderr, "get_realated_array: %p %s\n", ret, sql);
    //    gavl_array_dump(ret, 2);
    
    free(sql);
    i++;
    }
  }

/* Get array of children */
static void get_child_array(bg_mdb_backend_t * b,
                            gavl_array_t * ret, type_id_t type, int64_t id)
  {
  get_related_array_t priv;
  char * sql;
  sqlite_priv_t * p = b->priv;

  const obj_table_t * tab;
  
  memset(&priv, 0, sizeof(priv));
  priv.ret = ret;
  priv.type = type;
  
  tab = get_obj_table(type);
  
  if(has_src_col(tab, GAVL_META_URI))
    {
    sql = bg_sprintf("SELECT "META_DB_ID", "GAVL_META_URI" from %s WHERE %s = %"PRId64";",
                     tab->table_name, META_PARENT_ID, id);
    }
  else
    {
    sql = bg_sprintf("SELECT "META_DB_ID" from %s WHERE %s = %"PRId64";",
                     tab->table_name, META_PARENT_ID, id);
    }
  
  if(!bg_sqlite_exec(p->db, sql, get_related_array_cb, &priv))
    {
    free(sql);
    return;
    }
  free(sql);
  }

static void
delete_object_internal(bg_mdb_backend_t * b, int64_t id, type_id_t type,
                       const char * uri, int del_flags)
  {
  int i;

  query_t q;
  char * sql;
  int64_t parent_id = -1;
  type_id_t parent_type = 0;
  type_id_t child_type = 0;
  sqlite_priv_t * p = b->priv;

  gavl_dictionary_t * parent_m;
  const gavl_dictionary_t * obj_m;
  gavl_dictionary_t * obj = NULL;

  int is_item = 0;
  int num_children;
  int64_t duration;
  int64_t parent_duration = GAVL_TIME_UNDEFINED;
  
  memset(&q, 0, sizeof(q));
  q.be = b;
  
  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Deleting object %"PRId64, id);
  
  /* Remove from parent */
  
  switch(type)
    {
    case TYPE_SONG:
      parent_type = TYPE_ALBUM;
      is_item = 1;
      break;
    case TYPE_TV_SEASON:
      parent_type = TYPE_TV_SHOW;
      child_type = TYPE_TV_EPISODE;
      break;
    case TYPE_TV_EPISODE:
      parent_type = TYPE_TV_SEASON;
      is_item = 1;
      break;
    case TYPE_MOVIE_PART:
      parent_type = TYPE_MOVIE;
      is_item = 1;
      break;
    case TYPE_ALBUM:
      child_type = TYPE_SONG;
      break;
    case TYPE_TV_SHOW:
      child_type = TYPE_TV_SEASON;
      break;
    case TYPE_MOVIE:
      child_type = TYPE_MOVIE_PART;
      is_item = 1;
      break;
    case TYPE_IMAGE:
      if(del_flags & DEL_FLAG_RELATED)
        {
        gavl_array_t arr;
        gavl_array_init(&arr);
        /* Delete related movies and songs */
        
        get_related_array(b, &arr, META_POSTER_ID, id);
        get_related_array(b, &arr, META_WALLPAPER_ID, id);
        get_related_array(b, &arr, META_COVER_ID, id);

        delete_objects(b, &arr, del_flags);
        gavl_array_free(&arr);
        }
      break;
    case TYPE_NFO:
      if(del_flags & DEL_FLAG_RELATED)
        {
        /* Delete related movies and shows */
        gavl_array_t arr;
        gavl_array_init(&arr);
        get_related_array(b, &arr, META_NFO_ID, id);
        
        delete_objects(b, &arr, del_flags);
        gavl_array_free(&arr);
        }
      break;
    }

  if((del_flags & DEL_FLAG_CHILDREN) && (child_type > 0))
    {
    gavl_array_t arr;
    gavl_array_init(&arr);
    get_child_array(b, &arr, child_type, id);
    delete_objects(b, &arr, del_flags & ~DEL_FLAG_PARENT);
    gavl_array_free(&arr);
    }
  
  if((del_flags & DEL_FLAG_PARENT) && (parent_type > 0))
    {
    q.table = get_obj_table(type);

    sql = bg_sprintf("SELECT "META_DB_ID", "GAVL_META_APPROX_DURATION", "META_PARENT_ID" "
                     "from %s WHERE "META_DB_ID" = %"PRId64";",
                       q.table->table_name, id);
      
    
    bg_sqlite_exec(p->db, sql, query_object_callback, &q);
    free(sql);
    
    if(!q.obj)
      goto go_on;

    obj = q.obj;
    q.obj = NULL;
    
    obj_m = gavl_track_get_metadata(obj);
    
    q.table = get_obj_table(parent_type);
    
    gavl_dictionary_get_long(obj_m, META_PARENT_ID, &parent_id);

    if(parent_type == TYPE_MOVIE)
      {
      sql = bg_sprintf("SELECT "META_DB_ID", "GAVL_META_APPROX_DURATION" "
                       "from %s WHERE "META_DB_ID" = %"PRId64";",
                       q.table->table_name, parent_id);
      }
    else if(parent_type == TYPE_TV_SHOW)
      {
      sql = bg_sprintf("SELECT "META_DB_ID", "GAVL_META_NUM_CHILDREN" "
                       "from %s WHERE "META_DB_ID" = %"PRId64";",
                       q.table->table_name, parent_id);
      }
    else
      {
      sql = bg_sprintf("SELECT "META_DB_ID", "GAVL_META_APPROX_DURATION", "
                       GAVL_META_NUM_CHILDREN" from %s WHERE "META_DB_ID" = %"PRId64";",
                       q.table->table_name, parent_id);
      }
    
    bg_sqlite_exec(p->db, sql, query_object_callback, &q);
    free(sql);
    
    if(!q.obj)
      goto go_on;
    
    parent_m = gavl_track_get_metadata_nc(q.obj);

    if(gavl_dictionary_get_long(parent_m, GAVL_META_APPROX_DURATION, &parent_duration))
      {
      gavl_dictionary_get_long(obj_m, GAVL_META_APPROX_DURATION, &duration);
      parent_duration -= duration;

      if(parent_duration <= 0)
        {
        //        fprintf(stderr, "Deleting parent object (duration <= 0)\n");
        delete_object_internal(b, parent_id, parent_type, NULL, del_flags);
        goto go_on;
        }
      gavl_dictionary_set_long(parent_m, GAVL_META_APPROX_DURATION, parent_duration);
      }
    
    if((num_children = gavl_track_get_num_children(q.obj)) > 0)
      {
      num_children--;
      if(num_children <= 0)
        {
        //        fprintf(stderr, "Deleting parent object (num_children <= 0)\n");
        delete_object_internal(b, parent_id, parent_type, NULL, 0);
        goto go_on;
        }

      if(is_item)
        gavl_track_set_num_children(q.obj, 0, num_children);
      else
        gavl_track_set_num_children(q.obj, num_children, 0);
      }
    
    update_object(b, q.obj, q.table);
    }
  
  go_on:
  
  if(obj)
    gavl_dictionary_destroy(obj);
  if(q.obj)
    gavl_dictionary_destroy(q.obj);
  
  /* Dedicated table */
  if((q.table = get_obj_table(type)))
    {
    sql = bg_sprintf("DELETE FROM %s WHERE "META_DB_ID" = %"PRId64";",
                     q.table->table_name, id);
    bg_sqlite_exec(p->db, sql, NULL, NULL);
    free(sql);
    
    /* Remove arrays */

    if(q.table->arrays)
      {
      i = 0;
      
      while(q.table->arrays[i].name)
        {
        /* Remove from array */
        sql = bg_sprintf("DELETE FROM %s WHERE OBJ_ID = %"PRId64";",
                         q.table->arrays[i].array_table_name, id);
        bg_sqlite_exec(p->db, sql, NULL, NULL);
        free(sql);

        /* Remove orphaned IDs */
        sql = bg_sprintf("DELETE FROM %s WHERE ID NOT IN (SELECT DISTINCT NAME_ID FROM %s);",
                         q.table->arrays[i].id_table_name, q.table->arrays[i].array_table_name);
        bg_sqlite_exec(p->db, sql, NULL, NULL);
        free(sql);
        
        i++;
        }
      }
      
    }

  /* Object table */
  sql = bg_sprintf("DELETE FROM objects WHERE "META_DB_ID" = %"PRId64";", id);
  bg_sqlite_exec(p->db, sql, NULL, NULL);
  free(sql);

  
  
  
  }

static void delete_object(bg_mdb_backend_t * b, const gavl_dictionary_t * obj, int del_flags)
  {
  int64_t id;
  type_id_t type;
  const char * uri;

  uri = gavl_dictionary_get_string(obj, GAVL_META_URI);
  gavl_dictionary_get_long(obj, META_DB_ID, &id);
  gavl_dictionary_get_int(obj, "TYPE", (int*)&type);
  
  delete_object_internal(b, id, type, uri, del_flags);
  }


static int64_t add_object(bg_mdb_backend_t * b, gavl_dictionary_t * track,
                          int64_t scan_dir_id, int64_t obj_id)
  {
  int64_t id;
  int i, result;
  const char * klass;
  gavl_dictionary_t * m;
  type_id_t type;
  const obj_table_t * tab;
  char * sql;
  char * sql2;
  sqlite_priv_t * p = b->priv;
  const char * var;
  int no_obj_table = 0;

  //  bg_mdb_create_thumbnails(b->db, track);
  
  m = gavl_track_get_metadata_nc(track);

  if(!(klass = gavl_dictionary_get_string(m, GAVL_META_CLASS)))
    return -1;

  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Adding object %s %s",
           gavl_dictionary_get_string(m, GAVL_META_LABEL), 
           klass);
  
  /* Single part movies are handled same as multipart in the db */
  if(!strcmp(klass, GAVL_META_CLASS_MOVIE) && (obj_id < 0))
    {
    gavl_dictionary_set_string(m, GAVL_META_CLASS, GAVL_META_CLASS_MOVIE_PART);
    klass = gavl_dictionary_get_string(m, GAVL_META_CLASS);
    }
  
  if(!(type = get_type_id(klass)) ||
     !(tab = get_obj_table(type)))
    return -1;

  /* OBJECTS */

  no_obj_table = 0;
  
  if(has_col(tab, GAVL_META_DATE) &&
     !gavl_dictionary_get_string(m, GAVL_META_DATE))
    gavl_dictionary_set_string(m, GAVL_META_DATE, DATE_UNDEFINED);

  if(has_col(tab, META_IMAGE_TYPE) &&
     !gavl_dictionary_get_int(m, META_IMAGE_TYPE, &i))
    gavl_dictionary_set_int(m, META_IMAGE_TYPE, IMAGE_TYPE_IMAGE);
  
  /* Parent container */
  switch(type)
    {
    case TYPE_SONG:
      gavl_dictionary_set_long(m, META_PARENT_ID, add_child_album(b, track));
      break;
    case TYPE_TV_EPISODE:
      gavl_dictionary_set_long(m, META_PARENT_ID, add_child_tv_season(b, track));
      break;
    case TYPE_MOVIE_PART:
      /* Check if the movie already exists */
      gavl_dictionary_set_long(m, META_PARENT_ID, add_movie_part(b, track));
      break;
    default:
      break;
    }

  /* IDs for external resoures */
  if(has_col(tab, META_POSTER_ID))
    {
    if((var = gavl_dictionary_get_string_image_uri(m, GAVL_META_POSTER_URL, 0, NULL, NULL, NULL)))
      {
      id = bg_sqlite_string_to_id(p->db, "images", META_DB_ID, GAVL_META_URI, var);
      gavl_dictionary_set_long(m, META_POSTER_ID, id);
      set_image_type(b, id, IMAGE_TYPE_POSTER);
      }
    else
      gavl_dictionary_set_long(m, META_POSTER_ID, -1);
    }
  if(has_col(tab, META_WALLPAPER_ID))
    {
    if((var = gavl_dictionary_get_string_image_uri(m, GAVL_META_WALLPAPER_URL, 0, NULL, NULL, NULL)))
      {
      id = bg_sqlite_string_to_id(p->db, "images", META_DB_ID, GAVL_META_URI, var);
      gavl_dictionary_set_long(m, META_WALLPAPER_ID, id);
      set_image_type(b, id, IMAGE_TYPE_WALLPAPER);
      }
    else
      gavl_dictionary_set_long(m, META_WALLPAPER_ID, -1);
    }
  if(has_col(tab, META_COVER_ID))
    {
    if((var = gavl_dictionary_get_string_image_uri(m, GAVL_META_COVER_URL, 0, NULL, NULL, NULL)))
      {
      id = bg_sqlite_string_to_id(p->db, "images", META_DB_ID, GAVL_META_URI, var);
      gavl_dictionary_set_long(m, META_COVER_ID, id);
      set_image_type(b, id, IMAGE_TYPE_COVER);
      }
    else
      gavl_dictionary_set_long(m, META_COVER_ID, -1);
    }
  if(has_col(tab, META_NFO_ID))
    {
    if((var = gavl_dictionary_get_string(m, GAVL_META_NFO_FILE)))
      gavl_dictionary_set_long(m, META_NFO_ID,
                               bg_sqlite_string_to_id(p->db, "nfos", META_DB_ID, GAVL_META_URI, var));
    else
      gavl_dictionary_set_long(m, META_NFO_ID, -1);
    }

  if(has_array(tab, GAVL_META_COUNTRY))
    {
    if(!gavl_dictionary_get_item(m, GAVL_META_COUNTRY, 0))
      gavl_dictionary_set_string(m, GAVL_META_COUNTRY, "Unknown");
    }
  
  if(scan_dir_id >= 0)
    gavl_dictionary_set_long(m, META_SCAN_DIR_ID, scan_dir_id);
  
  /* Dedicated obj table */

  if(!no_obj_table)
    {
    if((obj_id < 0) && ((obj_id = create_object(p, type)) < 0))
      return -1;
    
    if(!strcmp(klass, GAVL_META_CLASS_MOVIE))
      {
      gavl_dictionary_set_long(m, META_PARENT_ID, obj_id);
      }
    
    gavl_dictionary_set_long(m, META_DB_ID, obj_id);
  
  
    sql  = bg_sprintf("INSERT INTO %s (", tab->table_name);
    sql2 = bg_sprintf(") VALUES (");
  
    append_cols(p, m, tab->cols, &sql, &sql2, 1);

    if(tab->src_cols)
      {
      const gavl_dictionary_t * src = gavl_metadata_get_src(m, GAVL_META_SRC, 0,
                                                              NULL, NULL);
      append_cols(p, src, tab->src_cols, &sql, &sql2, 0);
      }
  
    sql = gavl_strcat(sql, sql2);
    sql = gavl_strcat(sql, ");");
    
    result = bg_sqlite_exec(p->db, sql, NULL, NULL);

    free(sql);
    free(sql2);
  
    if(!result)
      return -1;

    }
  
  /* Arrays */
  if(!no_obj_table)
    {
    i = 0;

    if(tab->arrays)
      {
      while(tab->arrays[i].name)
        {
        append_array(p, m, &tab->arrays[i], obj_id);
        i++;
        }
      }
    }

  
  
  return obj_id;
  }

static void scan_directory(bg_mdb_backend_t * b, const char * dir, gavl_array_t * files)
  {
#if 0
  struct
    {
    struct dirent d;
    char b[NAME_MAX]; /* Make sure there is enough memory */
    } dent;
#endif
  
  struct dirent * dent_ptr = NULL;
  DIR * d;
  struct stat st;
  char * filename;

  /* Load files (recursively) */
  
  d = opendir(dir);

  while((dent_ptr = readdir(d)))
    {
    if(!dent_ptr)
      break;

    if(dent_ptr->d_name[0] == '.')
      continue;
    
    filename = bg_sprintf("%s/%s", dir, dent_ptr->d_name);

#ifdef _DIRENT_HAVE_D_TYPE

    if(dent_ptr->d_type == DT_DIR)
      {
      scan_directory(b, filename, files);
      free(filename);
      continue;
      }
    else if((dent_ptr->d_type == DT_REG) ||
            (dent_ptr->d_type == DT_LNK))
      {
      /* File */
      if(!stat(filename, &st))
        {
        append_file_nocopy(files, filename, st.st_mtime);
        filename = NULL;
        }
      }
    else
      {
      free(filename);
      continue;
      }
#else
    if(!stat(filename, &st))
      {
      if(S_ISDIR(st.st_mode))
        {
        scan_directory(b, filename, files);
        free(filename);
        continue;
        }
      else
        {
        /* File */
        append_file_nocopy(files, filename, st.st_mtime);
        filename = NULL;
        }
      }
#endif // _DIRENT_HAVE_D_TYPE

    if(filename)
      free(filename);
    }
  
  closedir(d);

  }

static int first_pass(const char * location)
  {
  const char * mimetype = NULL;
  if(!(mimetype = bg_url_to_mimetype(location)))
    return 0;
  
  if(!strncmp(mimetype, "image/", 6))
    return 1;
  
  if(!strcmp(mimetype, "text/x-nfo"))
    return 1;
  
  return 0;
  }

static void add_files(bg_mdb_backend_t * b, gavl_array_t * arr, int64_t scan_dir_id)
  {
  int i;
  const gavl_dictionary_t * dict;
  const char * location;
  const char * mimetype;
  gavl_dictionary_t * mi;
  gavl_dictionary_t * track;
  
  /* Handle .nfo and images first */
  i = 0;
  while(i < arr->num_entries)
    {
    if((dict = gavl_value_get_dictionary(&arr->entries[i])) &&
       (location = gavl_dictionary_get_string(dict, GAVL_META_URI)))
      {
      if(!first_pass(location))
        {
        i++;
        continue;
        }
      mimetype = bg_url_to_mimetype(location);

      
      if(!strcmp(mimetype, "text/x-nfo"))
        {
        int64_t mtime;
        
        if(gavl_dictionary_get_long(dict, GAVL_META_MTIME, &mtime))
          {
          gavl_dictionary_t * m;
          gavl_dictionary_t * obj = gavl_dictionary_create();

          gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Adding file: %s",
                 location);
          
          m = gavl_dictionary_get_dictionary_create(obj, GAVL_META_METADATA);
          gavl_dictionary_copy(m, dict);
          gavl_dictionary_set_string(m, GAVL_META_CLASS, "nfo");
          
          add_object(b, obj, scan_dir_id, -1);
          
          gavl_dictionary_destroy(obj); 
          }
        
        gavl_array_splice_val(arr, i, 1, NULL);
        continue;
        }

      mi = NULL;
      
      if(!is_blacklisted(location)) // TODO: Make this more elegant
        {
        gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Loading %s", location);
      
        mi = bg_plugin_registry_load_media_info(bg_plugin_reg,
                                                location,
                                                BG_INPUT_FLAG_GET_FORMAT);
        }
      
      if(mi)
        {
        /* Multitrack files are ignored for now */
        if((gavl_get_num_tracks(mi) == 1) && (track = gavl_get_track_nc(mi, 0)))
          {
          gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Adding file: %s", location);
          add_object(b, track, scan_dir_id, -1);
          }
        gavl_dictionary_destroy(mi);
        gavl_array_splice_val(arr, i, 1, NULL);
        }
      else
        i++;
      }
    else
      i++;
    }
  
  
  /* Rest of the stuff */
  
  for(i = 0; i < arr->num_entries; i++)
    {
    if((dict = gavl_value_get_dictionary(&arr->entries[i])) &&
       (location = gavl_dictionary_get_string(dict, GAVL_META_URI)))
      {
      mi = NULL;

      if(!is_blacklisted(location))
        {
        gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Loading %s", location);
        mi = bg_plugin_registry_load_media_info(bg_plugin_reg,
                                                location,
                                                BG_INPUT_FLAG_GET_FORMAT);
        
        }
      if(mi)
        {
        /* Multitrack files are ignored for now */
        if((gavl_get_num_tracks(mi) == 1) && (track = gavl_get_track_nc(mi, 0)))
          {
          gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Adding file: %s", location);
          add_object(b, track, scan_dir_id, -1);
          }
        gavl_dictionary_destroy(mi);
        }
      }
    
    }
  }

/*
 *  0: URI
 *  1: Object ID
 *  2: MTIME
 */

typedef struct
  {
  int type;
  gavl_array_t arr;
  } get_files_t;

static int get_files_db_callback(void * data, int argc, char **argv, char **azColName)
  {
  gavl_value_t val;
  gavl_dictionary_t * dict;
  get_files_t * gf = data;

  gavl_value_init(&val);
  dict = gavl_value_set_dictionary(&val);

  gavl_dictionary_set_string(dict, GAVL_META_URI, argv[0]);
  gavl_dictionary_set_long(dict,   META_DB_ID, strtoll(argv[1], NULL, 10));

  if(!argv[2])
    gavl_dictionary_set_long(dict,   GAVL_META_MTIME, 0);
  else
    gavl_dictionary_set_long(dict,   GAVL_META_MTIME, strtoll(argv[2], NULL, 10));
  gavl_dictionary_set_int(dict,    "TYPE", gf->type);
  
  gavl_array_splice_val_nocopy(&gf->arr, -1, 0, &val);
  return 0;
  }

static int find_by_uri(gavl_array_t * files, const char * uri)
  {
  int i;
  gavl_dictionary_t * dict;
  const char * var;
  
  for(i = 0; i < files->num_entries; i++)
    {
    if((dict = gavl_value_get_dictionary_nc(&files->entries[i])) &&
       (var = gavl_dictionary_get_string(dict, GAVL_META_URI)) &&
       !(strcmp(var, uri)))
      return i;
    }
  return -1;
  }

static void get_files_db(bg_mdb_backend_t * b, gavl_array_t * ret, int64_t id, int pass)
  {
  int i;
  get_files_t gf;
  char * sql;
  sqlite_priv_t * priv;
  priv = b->priv;
  
  memset(&gf, 0, sizeof(gf));

  i = 0;
  while(obj_tables[i].table_name)
    {
    if(!has_col(&obj_tables[i], META_SCAN_DIR_ID) ||
       (!has_src_col(&obj_tables[i], GAVL_META_URI) &&
        !has_col(&obj_tables[i], GAVL_META_URI)) ||
       (pass != obj_tables[i].pass))
      {
      i++;
      continue;
      }
    gf.type = obj_tables[i].type;
    sql = bg_sprintf("SELECT "GAVL_META_URI", "META_DB_ID", "GAVL_META_MTIME" FROM %s WHERE "
                     META_SCAN_DIR_ID" = %"PRId64";", obj_tables[i].table_name, id);

    //    fprintf(stderr, "Get files SQL: %s\n", sql);

    if(!bg_sqlite_exec(priv->db, sql, get_files_db_callback, &gf))
      return;
    free(sql);
    i++;
    }

  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Got %d files from database", gf.arr.num_entries);

  //  fprintf(stderr, "Got %d files from database\n", gf.arr.num_entries);

  gavl_array_splice_array(ret, -1, 0, &gf.arr);
  gavl_array_free(&gf.arr);
  
  }

static void delete_directory(bg_mdb_backend_t * b, const char * dir)
  {
  gavl_array_t files;
  int64_t id;
  int i;
  char * sql;
  
  sqlite_priv_t * priv = b->priv;

  bg_mdb_unexport_media_directory(b->ctrl.evt_sink, dir);
  
  if((id = bg_sqlite_string_to_id(priv->db, "scandirs", "ID", "PATH", dir)) < 0)
    {
    gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "No such directory %s in db", dir);
    return;
    }
  gavl_array_init(&files);
  
  get_files_db(b, &files, id, 1);
  get_files_db(b, &files, id, 2);
  
  bg_sqlite_start_transaction(priv->db);
  
  for(i = 0; i < files.num_entries; i++)
    delete_object(b, gavl_value_get_dictionary(&files.entries[i]),
                  DEL_FLAG_RELATED | DEL_FLAG_PARENT | DEL_FLAG_CHILDREN);
  
  sql = bg_sprintf("DELETE FROM scandirs WHERE ID = %"PRId64";", id);
  bg_sqlite_exec(priv->db, sql, NULL, NULL);
  free(sql);
  
  bg_sqlite_end_transaction(priv->db);

  gavl_array_free(&files);
  }

static void add_directory(bg_mdb_backend_t * b, const char * dir)
  {
  int64_t id;
  sqlite_priv_t * priv;
  gavl_array_t files_fs;
  int is_new = 0;
  
  priv = b->priv;

  id = bg_sqlite_string_to_id(priv->db, "scandirs", "ID", "PATH", dir);

  if(id > 0)
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Re-scanning directory %s", dir);
  else
    {
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Adding directory %s", dir);
    is_new = 1;
    }
  
  gavl_array_init(&files_fs);
  scan_directory(b, dir, &files_fs);
  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Scanned directory, found %d files", files_fs.num_entries);
  
  bg_sqlite_start_transaction(priv->db);

  /* Already present. Update */
  if(id > 0)
    {
    int db_idx;
    const char * uri;
    const gavl_dictionary_t * file_fs;
    const gavl_dictionary_t * file_db;
    int64_t mtime_fs;
    int64_t mtime_db;
    int file_idx;
    gavl_array_t files_db;

    gavl_array_init(&files_db);

    get_files_db(b, &files_db, id, 1);

    /* Remove objects from data, where the file disappeared */
    db_idx = 0;
    
    while(db_idx < files_db.num_entries)
      {
      if((file_db = gavl_value_get_dictionary(&files_db.entries[db_idx])) &&
         (uri = gavl_dictionary_get_string(file_db,GAVL_META_URI)) &&
         (file_idx = find_by_uri(&files_fs, uri)) < 0)
        {
        delete_object(b, file_db, DEL_FLAG_RELATED | DEL_FLAG_PARENT | DEL_FLAG_CHILDREN );
        gavl_array_splice_val(&files_db, db_idx, 1, NULL);
        }
      else
        db_idx++;
      }
    
    /* Remove outdated files from database. up-to-date files are removed from the db */
    
    file_idx = 0;
    
    while(file_idx < files_fs.num_entries)
      {
      mtime_fs = -1;
      mtime_db = -1;

      if(!(file_fs = gavl_value_get_dictionary(&files_fs.entries[file_idx])) ||
         !(uri = gavl_dictionary_get_string(file_fs,GAVL_META_URI)) ||
         ((db_idx = find_by_uri(&files_db, uri)) < 0) ||
         !(file_db = gavl_value_get_dictionary(&files_db.entries[db_idx])) ||
         !gavl_dictionary_get_long(file_fs, GAVL_META_MTIME, &mtime_fs) ||
         !gavl_dictionary_get_long(file_db, GAVL_META_MTIME, &mtime_db))
        {
        //        fprintf(stderr, "Skipping %s %d %"PRId64" %"PRId64"\n", uri,
        //                db_idx, mtime_fs, mtime_db);
        file_idx++;
        continue;
        }

      
      /* File on filesystem is newer than db entry: Remove so we can re-add it later */

      if(mtime_fs > mtime_db)  // Entry out of date: Remove from db
        {
        gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "%s out of date, removing for re-adding later", uri);
        
        delete_object(b, file_db, DEL_FLAG_RELATED | DEL_FLAG_PARENT | DEL_FLAG_CHILDREN );
        gavl_array_splice_val(&files_db, db_idx, 1, NULL);
        file_idx++;
        }
      else // Entry up to date: Remove from files
        {
        gavl_array_splice_val(&files_fs, file_idx, 1, NULL);
        }
      }
    
    gavl_array_reset(&files_db);
    get_files_db(b, &files_db, id, 2);

    /* Remove objects from data, where the file disappeared */
    db_idx = 0;
    
    while(db_idx < files_db.num_entries)
      {
      if((file_db = gavl_value_get_dictionary(&files_db.entries[db_idx])) &&
         (uri = gavl_dictionary_get_string(file_db,GAVL_META_URI)) &&
         (file_idx = find_by_uri(&files_fs, uri)) < 0)
        {
        delete_object(b, file_db, DEL_FLAG_RELATED | DEL_FLAG_PARENT | DEL_FLAG_CHILDREN );
        gavl_array_splice_val(&files_db, db_idx, 1, NULL);
        }
      else
        db_idx++;
      }
    
    /* Remove outdated files from database. up-to-date files are removed from the db */
    
    file_idx = 0;
    
    while(file_idx < files_fs.num_entries)
      {
      mtime_fs = -1;
      mtime_db = -1;
      
      if(!(file_fs = gavl_value_get_dictionary(&files_fs.entries[file_idx])) ||
         !(uri = gavl_dictionary_get_string(file_fs,GAVL_META_URI)) ||
         ((db_idx = find_by_uri(&files_db, uri)) < 0) ||
         !(file_db = gavl_value_get_dictionary(&files_db.entries[db_idx])) ||
         !gavl_dictionary_get_long(file_fs, GAVL_META_MTIME, &mtime_fs) ||
         !gavl_dictionary_get_long(file_db, GAVL_META_MTIME, &mtime_db))
        {
        //        fprintf(stderr, "Skipping %s %d %"PRId64" %"PRId64"\n", uri,
        //                db_idx, mtime_fs, mtime_db);
        file_idx++;
        continue;
        }
      
      /* File on filesystem is newer than db entry: Remove so we can re-add it later */

      if(mtime_fs > mtime_db)  // Entry out of date: Remove from db
        {
        gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "%s out of date, removing for re-adding later", uri);
        
        delete_object(b, file_db, DEL_FLAG_RELATED | DEL_FLAG_PARENT | DEL_FLAG_CHILDREN );
        gavl_array_splice_val(&files_db, db_idx, 1, NULL);
        file_idx++;
        }
      else // Entry up to date: Remove from files
        {
        gavl_array_splice_val(&files_fs, file_idx, 1, NULL);
        }
      }

    
    gavl_array_free(&files_db);
    }
  else // scandir not in db
    {
    id = bg_sqlite_string_to_id_add(priv->db, "scandirs", "ID", "PATH", dir);
    }
  
  
  /* Add new files */
  add_files(b, &files_fs, id);

  bg_sqlite_end_transaction(priv->db);

  if(is_new)
    {
    bg_mdb_export_media_directory(b->ctrl.evt_sink, dir);
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Done adding %s", dir);
    }
  else
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Done re-scanning %s", dir);
  }

static void destroy_sqlite(bg_mdb_backend_t * b)
  {
  sqlite_priv_t * priv;
  priv = b->priv;
  sqlite3_close(priv->db);

  gavl_dictionary_free(&priv->songs);
  gavl_dictionary_free(&priv->movies);
  gavl_dictionary_free(&priv->albums);
  gavl_dictionary_free(&priv->series);
  
  free(priv);
  }

/* Browse functions */

static int starts_with(const char ** id, const char * str)
  {
  if(gavl_string_starts_with(*id, str))
    {
    (*id) += strlen(str);
    return 1;
    }
  else
    return 0;
  }

static void finalize_metadata_album(gavl_dictionary_t * m)
  {
  int i;
  int num1, num2;
  int year;
  
  const char * str;
  const char * id;
  
  if((num1 = gavl_dictionary_get_num_items(m, GAVL_META_ARTIST)) &&
     (num2 = gavl_dictionary_get_num_items(m, GAVL_META_ARTIST GAVL_META_ID)) &&
     (num1 == num2))
    {
    for(i = 0; i < num1; i++)
      {
      str = gavl_dictionary_get_string_array(m, GAVL_META_ARTIST, i);
      id = gavl_dictionary_get_string_array(m, GAVL_META_ARTIST GAVL_META_ID, i);

      gavl_dictionary_append_string_array_nocopy(m, GAVL_META_ARTIST "Container", 
                                                 bg_sprintf("/albums/artist/%s/%s", bg_mdb_get_group_id(str), id) );
      }
    }

  if((num1 = gavl_dictionary_get_num_items(m, GAVL_META_GENRE)) &&
     (num2 = gavl_dictionary_get_num_items(m, GAVL_META_GENRE GAVL_META_ID)) &&
     (num1 == num2))
    {
    for(i = 0; i < num1; i++)
      {
      id = gavl_dictionary_get_string_array(m, GAVL_META_GENRE GAVL_META_ID, i);
      gavl_dictionary_append_string_array_nocopy(m, GAVL_META_GENRE "Container", 
                                                 bg_sprintf("/albums/genre-artist/%s", id) );
      }
    }
  
  if((year = gavl_dictionary_get_year(m, GAVL_META_DATE)))
    gavl_dictionary_set_string_nocopy(m, GAVL_META_YEAR "Container", bg_sprintf("/albums/year/%d", year));
  
  }

static void finalize_metadata_song(gavl_dictionary_t * m)
  {
  /* Make "links" for artist and genre */

  int i;
  int num1, num2;
  int year;

  const char * str;
  const char * id;
  
  if((num1 = gavl_dictionary_get_num_items(m, GAVL_META_ARTIST)) &&
     (num2 = gavl_dictionary_get_num_items(m, GAVL_META_ARTIST GAVL_META_ID)) &&
     (num1 == num2))
    {
    for(i = 0; i < num1; i++)
      {
      str = gavl_dictionary_get_string_array(m, GAVL_META_ARTIST, i);
      id = gavl_dictionary_get_string_array(m, GAVL_META_ARTIST GAVL_META_ID, i);

      gavl_dictionary_append_string_array_nocopy(m, GAVL_META_ARTIST "Container", 
                                                 bg_sprintf("/songs/artist/%s/%s", bg_mdb_get_group_id(str), id) );
      }

    
    }

  if((num1 = gavl_dictionary_get_num_items(m, GAVL_META_GENRE)) &&
     (num2 = gavl_dictionary_get_num_items(m, GAVL_META_GENRE GAVL_META_ID)) &&
     (num1 == num2))
    {

    for(i = 0; i < num1; i++)
      {
      id = gavl_dictionary_get_string_array(m, GAVL_META_GENRE GAVL_META_ID, i);
      
      gavl_dictionary_append_string_array_nocopy(m, GAVL_META_GENRE "Container", 
                                                 bg_sprintf("/songs/genre-artist/%s", id) );
      }
    
    }

  
  if((year = gavl_dictionary_get_year(m, GAVL_META_DATE)))
    gavl_dictionary_set_string_nocopy(m, GAVL_META_YEAR "Container", bg_sprintf("/songs/year/%d", year));

  
  }

static void finalize_metadata_movie(gavl_dictionary_t * m)
  {

  int i;

  int num1, num2;
  int year;

  const char * str;
  const char * id;
  
  if((num1 = gavl_dictionary_get_num_items(m, GAVL_META_ACTOR)) &&
     (num2 = gavl_dictionary_get_num_items(m, GAVL_META_ACTOR GAVL_META_ID)) &&
     (num1 == num2))
    {
    for(i = 0; i < num1; i++)
      {
      str = gavl_dictionary_get_string_array(m, GAVL_META_ACTOR, i);
      id = gavl_dictionary_get_string_array(m, GAVL_META_ACTOR GAVL_META_ID, i);
    
      gavl_dictionary_append_string_array_nocopy(m, GAVL_META_ACTOR "Container", 
                                                 bg_sprintf("/movies/actor/%s/%s", bg_mdb_get_group_id(str), id) );
      }
    
    
    }

  if((num1 = gavl_dictionary_get_num_items(m, GAVL_META_DIRECTOR)) &&
     (num2 = gavl_dictionary_get_num_items(m, GAVL_META_DIRECTOR GAVL_META_ID)) &&
     (num1 == num2))
    {
    for(i = 0; i < num1; i++)
      {
      str = gavl_dictionary_get_string_array(m, GAVL_META_DIRECTOR, i);
      id = gavl_dictionary_get_string_array(m, GAVL_META_DIRECTOR GAVL_META_ID, i);
    
      gavl_dictionary_append_string_array_nocopy(m, GAVL_META_DIRECTOR "Container", 
                                                 bg_sprintf("/movies/director/%s/%s", bg_mdb_get_group_id(str), id) );
      }

    
    }

  if((num1 = gavl_dictionary_get_num_items(m, GAVL_META_GENRE)) &&
     (num2 = gavl_dictionary_get_num_items(m, GAVL_META_GENRE GAVL_META_ID)) &&
     (num1 == num2))
    {
    for(i = 0; i < num1; i++)
      {
      id = gavl_dictionary_get_string_array(m, GAVL_META_GENRE GAVL_META_ID, i);
      
      gavl_dictionary_append_string_array_nocopy(m, GAVL_META_GENRE "Container", 
                                                 bg_sprintf("/movies/genre/%s", id) );
      }
    
    }

  if((num1 = gavl_dictionary_get_num_items(m, GAVL_META_COUNTRY)) &&
     (num2 = gavl_dictionary_get_num_items(m, GAVL_META_COUNTRY GAVL_META_ID)) &&
     (num1 == num2))
    {
    for(i = 0; i < num1; i++)
      {
      id = gavl_dictionary_get_string_array(m, GAVL_META_COUNTRY GAVL_META_ID, i);
    
      gavl_dictionary_append_string_array_nocopy(m, GAVL_META_COUNTRY "Container", 
                                                 bg_sprintf("/movies/country/%s", id) );
      }
    }

  if((year = gavl_dictionary_get_year(m, GAVL_META_DATE)))
    gavl_dictionary_set_string_nocopy(m, GAVL_META_YEAR "Container", bg_sprintf("/movies/year/%d", year));
  }

static int browse_object_series(bg_mdb_backend_t * b, const char * id, gavl_dictionary_t * ret)
  {
  char * rest;
  int64_t series_id;
  gavl_dictionary_t * m;
  gavl_dictionary_t * obj;
  sqlite_priv_t * s;
  int num = 0;
  s = b->priv;
  m = gavl_track_get_metadata_nc(ret);
  
  id++;
  
  series_id = strtoll(id, &rest, 10);
        
  id = rest;
  
  if(*id == '\0')  // /series/all/20
    {
    if(!(obj = query_sqlite_object(b, series_id, TYPE_TV_SHOW)))
      return 0;

    gavl_dictionary_merge2(m, gavl_track_get_metadata(obj));
    gavl_dictionary_set(m, GAVL_META_LABEL, gavl_dictionary_get(m, GAVL_META_TITLE));

    /* add "all episodes" */
    gavl_dictionary_get_int(m, GAVL_META_NUM_CHILDREN, &num);
    num++;
    gavl_dictionary_set_int(m, GAVL_META_NUM_CHILDREN, num);
    gavl_dictionary_set_int(m, GAVL_META_NUM_CONTAINER_CHILDREN, num);
    
    gavl_dictionary_destroy(obj);
    return 1;
    }
  else if(*id == '/')  // /series/all/20/...
    {
    int64_t season_id;

    id++;

    if(!strncmp(id, "all", 3))
      {
      season_id = -1;
      id += 3;
      }
    else
      {
      season_id = strtoll(id, &rest, 10);
      id = rest;
      }
    
    if(*id == '\0')  // /series/all/20/40
      {
      if(season_id == -1) // /series/all/20/all
        {
        query_t q;
        char * sql;
        
        // int64_t poster_id, wallpaper_id;
        int64_t num_children = 0;
        
        gavl_dictionary_set_string(m, GAVL_META_CLASS, GAVL_META_CLASS_CONTAINER);
        gavl_dictionary_set_string(m, GAVL_META_LABEL, "All episodes");

        gavl_dictionary_set_string(m, GAVL_META_CHILD_CLASS, GAVL_META_CLASS_TV_EPISODE);
        
        
        sql = bg_sprintf("SELECT COUNT("META_DB_ID") FROM episodes WHERE "META_PARENT_ID" in (SELECT "META_DB_ID" FROM seasons "
                         "WHERE "META_PARENT_ID" = %"PRId64");", series_id);

        bg_sqlite_exec(s->db, sql, bg_sqlite_int_callback, &num_children);
        free(sql);

        gavl_track_set_num_children(ret, 0, num_children);
        
        /* Poster- and wallpaper ID */
        memset(&q, 0, sizeof(q));
        q.be = b;
        q.table = get_obj_table(TYPE_TV_SHOW);
        
        sql = bg_sprintf("SELECT "META_POSTER_ID", "META_WALLPAPER_ID" FROM shows WHERE "META_DB_ID" = %"PRId64";", series_id);
        bg_sqlite_exec(s->db, sql, query_object_callback, &q);
        free(sql);
        
        if(q.obj)
          {
          const gavl_dictionary_t * m1;

          //          fprintf(stderr, "Object:\n");
          //          gavl_dictionary_dump(q.obj, 2);
          //          fprintf(stderr, "\n");

          if((m1 = gavl_track_get_metadata(q.obj)))
            {
            gavl_dictionary_merge2(m, m1);
            }
          gavl_dictionary_destroy(q.obj);

          query_images(b, m);
          }
        
        }
      else
        {
        int season;
        int num;
        
        if(!(obj = query_sqlite_object(b, season_id, TYPE_TV_SEASON)))
          return 0;
      
        gavl_dictionary_merge2(m, gavl_track_get_metadata(obj));

        gavl_dictionary_get_int(m, GAVL_META_SEASON, &season);
        gavl_dictionary_get_int(m, GAVL_META_NUM_CHILDREN, &num);
        gavl_dictionary_set_int(m, GAVL_META_NUM_ITEM_CHILDREN, num);
        
        gavl_dictionary_set_string_nocopy(m, GAVL_META_LABEL, bg_sprintf("Season %d", season));
      
        gavl_dictionary_destroy(obj);
        }
      return 1;
      }
    else if(*id == '/')  // /series/all/20/40/
      {
      int64_t episode_id;
      id++;
      episode_id = strtoll(id, &rest, 10);

      if(!(obj = query_sqlite_object(b, episode_id, TYPE_TV_EPISODE)))
        return 0;
      
      gavl_dictionary_merge2(m, gavl_track_get_metadata(obj));

      gavl_dictionary_set(m, GAVL_META_LABEL, gavl_dictionary_get(m, GAVL_META_TITLE));
      
      gavl_dictionary_destroy(obj);
      return 1;
      }
    }
  else
    return 0;

  return 0;
  }

static int browse_object_internal(bg_mdb_backend_t * b, const char * id_p, gavl_dictionary_t * ret)
  {
  gavl_dictionary_t * m;
  sqlite_priv_t * s;
  int64_t artist_id;
  int64_t genre_id;
  char * rest;
  char * sql;
  const char * id;
  gavl_dictionary_t * obj;
  
  s = b->priv;

  m = gavl_track_get_metadata_nc(ret);
  
  if(id_p)
    {
    id = id_p;
    gavl_dictionary_set_string(m, GAVL_META_ID, id);
    }
  else
    id = gavl_dictionary_get_string(m, GAVL_META_ID);

  if(s->songs_id && starts_with(&id, s->songs_id))
    {
    if(*id != '/')
      return 0;
    id++;

    if(starts_with(&id, "artist")) // /songs/artist
      {
      if(*id == '\0')
        {
        gavl_array_t arr;
        gavl_array_init(&arr);
        
        gavl_dictionary_set_string(m, GAVL_META_LABEL, "Artist");
        gavl_dictionary_set_string(m, GAVL_META_CLASS, GAVL_META_CLASS_CONTAINER);
        
        bg_sqlite_exec(s->db, "SELECT NAME FROM song_artists", append_string_callback, &arr);

        gavl_track_set_num_children(ret, bg_mdb_get_num_groups(&arr), 0);
        gavl_array_free(&arr);
        return 1;
        }
      else if(*id == '/')
        {
        const char * pos;

        id++;

        if(!(pos = strchr(id, '/'))) // /songs/artist/a
          {
          gavl_array_t arr;
          gavl_array_init(&arr);
          bg_sqlite_exec(s->db, "SELECT NAME FROM song_artists", append_string_callback, &arr);

          gavl_track_set_num_children(ret, bg_mdb_get_group_size(&arr, id), 0);
          
          gavl_dictionary_set_string(m, GAVL_META_LABEL, bg_mdb_get_group_label(id));

          gavl_dictionary_set_string(m, GAVL_META_CLASS, GAVL_META_CLASS_CONTAINER);
          gavl_dictionary_set_string(m, GAVL_META_CHILD_CLASS, GAVL_META_CLASS_CONTAINER_ARTIST);
          gavl_array_free(&arr);
          return 1;
          }
        else  // /songs/artist/a/152
          {
          char * rest;
          int64_t artist_id;
          
          id = pos;
          id++;
          artist_id = strtoll(id, &rest, 10);

          id = rest;

          if(*id == '\0')  // /songs/artist/a/123
            {
            gavl_dictionary_set_string_nocopy(m, GAVL_META_LABEL,
                                              bg_sqlite_id_to_string(s->db,
                                                                     "song_artists",
                                                                     "NAME", "ID", artist_id));

            gavl_dictionary_set_string(m, GAVL_META_CLASS,
                                       GAVL_META_CLASS_CONTAINER_ARTIST);
            gavl_dictionary_set_string(m, GAVL_META_CHILD_CLASS, GAVL_META_CLASS_SONG);

            /* num_children */
            sql = bg_sprintf("SELECT count(OBJ_ID) "
                             "FROM song_artists_arr WHERE "
                             "NAME_ID = %"PRId64";", artist_id);

            gavl_track_set_num_children(ret, 0, (int)bg_sqlite_get_int(s->db, sql));
            
            free(sql);
            
            return 1;
            }
          else if(*id == '/')   // /songs/artist/a/123/1200
            {
            int64_t song_id;
            id++;
            song_id = strtoll(id, &rest, 10);
            
            if(!(obj = query_sqlite_object(b, song_id, TYPE_SONG)))
              return 0;
            
            gavl_dictionary_merge2(m, gavl_track_get_metadata(obj));
            gavl_dictionary_set(m, GAVL_META_LABEL, gavl_dictionary_get(m, GAVL_META_TITLE));

            finalize_metadata_song(m);
            
            gavl_dictionary_destroy(obj);
            return 1;
            }
          else
            return 0;
          }
        }
      else
        return 0;
      }
    else if(starts_with(&id, "genre-artist")) // /songs/genre-artist
      {
      if(*id == '\0')
        {
        gavl_dictionary_set_string(m, GAVL_META_LABEL, "Genre-Artist");
        gavl_dictionary_set_string(m, GAVL_META_CLASS, GAVL_META_CLASS_CONTAINER);
        gavl_dictionary_set_string(m, GAVL_META_CHILD_CLASS, GAVL_META_CLASS_CONTAINER_GENRE);
                
        gavl_track_set_num_children(ret, bg_sqlite_get_int(s->db, "SELECT COUNT(ID) FROM song_genres;"), 0);
        return 1;
        }
      else if(*id == '/')  // /songs/genre-artist/1
        {
        id++;
        genre_id = strtoll(id, &rest, 10);

        if(id == rest)
          return 0;

        id = rest;

        if(*id == '\0')
          {
          gavl_dictionary_set_string_nocopy(m, GAVL_META_LABEL,
                                            bg_sqlite_id_to_string(s->db,
                                                                   "song_genres",
                                                                   "NAME", "ID", genre_id));
          
          gavl_dictionary_set_string(m, GAVL_META_CLASS, GAVL_META_CLASS_CONTAINER_GENRE);
          gavl_dictionary_set_string(m, GAVL_META_CHILD_CLASS, GAVL_META_CLASS_CONTAINER_ARTIST);
          
          sql = bg_sprintf("SELECT count(DISTINCT song_artists_arr.NAME_ID) "
                           "FROM song_artists_arr INNER JOIN song_genres_arr "
                           "ON song_artists_arr.OBJ_ID = song_genres_arr.OBJ_ID "
                           "WHERE song_genres_arr.NAME_ID = %"PRId64";", genre_id);

          gavl_track_set_num_children(ret, (int)bg_sqlite_get_int(s->db, sql), 0);
          free(sql);
          
          
          return 1;
          }
        else if(*id == '/')
          {
          id++;
          artist_id = strtoll(id, &rest, 10);

          id = rest;

          if(*id == '\0')
            {
            gavl_dictionary_set_string(m, GAVL_META_CLASS, GAVL_META_CLASS_CONTAINER_ARTIST);
            gavl_dictionary_set_string(m, GAVL_META_CHILD_CLASS, GAVL_META_CLASS_SONG);
          
            gavl_dictionary_set_string_nocopy(m, GAVL_META_LABEL,
                                              bg_sqlite_id_to_string(s->db,
                                                                     "song_artists",
                                                                     "NAME", "ID", artist_id));

            sql = bg_sprintf("SELECT count(song_artists_arr.OBJ_ID) "
                             "FROM song_artists_arr INNER JOIN song_genres_arr "
                             "ON song_artists_arr.OBJ_ID = song_genres_arr.OBJ_ID "
                             "WHERE song_genres_arr.NAME_ID = %"PRId64" AND "
                             "song_artists_arr.NAME_ID = %"PRId64";", genre_id, artist_id);

            gavl_track_set_num_children(ret, 0, (int)bg_sqlite_get_int(s->db, sql));
            free(sql);
            
            return 1;
            }
          else if(*id == '/')
            {
            int64_t obj_id;
            id++;

            obj_id = strtoll(id, &rest, 10);

            if(!(obj = query_sqlite_object(b, obj_id, TYPE_SONG)))
              return 0;
            
            gavl_dictionary_merge2(m, gavl_track_get_metadata(obj));
            gavl_dictionary_set(m, GAVL_META_LABEL, gavl_dictionary_get(m, GAVL_META_TITLE));

            finalize_metadata_song(m);
            
            gavl_dictionary_destroy(obj);
            return 1;
            }
          else          
            return 0;

          }
        else
          return 0;
        
        }
      else
        return 0;
      }
    else if(starts_with(&id, "genre-year"))
      {
      if(*id == '\0')
        {
        gavl_dictionary_set_string(m, GAVL_META_LABEL, "Genre-Year");
        gavl_dictionary_set_string(m, GAVL_META_CLASS, GAVL_META_CLASS_CONTAINER);
        gavl_dictionary_set_string(m, GAVL_META_CHILD_CLASS, GAVL_META_CLASS_CONTAINER_GENRE);
        
        gavl_track_set_num_children(ret, bg_sqlite_get_int(s->db, "SELECT COUNT(ID) FROM song_genres;"), 0);
        
        return 1;
        }
      else if(*id == '/')
        {
        id++;
        genre_id = strtoll(id, &rest, 10);

        if(id == rest)
          return 0;
        id = rest;

        if(*id == '\0') // /songs/genre-year/1
          {
          gavl_dictionary_set_string_nocopy(m, GAVL_META_LABEL,
                                            bg_sqlite_id_to_string(s->db,
                                                                   "song_genres",
                                                                   "NAME", "ID", genre_id));
          
          gavl_dictionary_set_string(m, GAVL_META_CLASS, GAVL_META_CLASS_CONTAINER_GENRE);
          gavl_dictionary_set_string(m, GAVL_META_CHILD_CLASS, GAVL_META_CLASS_CONTAINER_YEAR);
          
          sql = bg_sprintf("SELECT count(DISTINCT substr(songs."GAVL_META_DATE", 1, 4)) FROM "
                           "songs INNER JOIN song_genres_arr ON "
                           "song_genres_arr.OBJ_ID = songs.DBID WHERE "
                           "song_genres_arr.NAME_ID = %"PRId64" ;", genre_id);

          gavl_track_set_num_children(ret, bg_sqlite_get_int(s->db, sql), 0);
          free(sql);
          
          return 1;
          }
        else if(*id == '/')  // /songs/genre-year/1/1960...
          {
          int year;

          id++;
          
          year = strtol(id, &rest, 10);

          id = rest;

          if(*id == '\0')  // /songs/genre-year/1/1960
            {
            gavl_dictionary_set_string(m, GAVL_META_CLASS, GAVL_META_CLASS_CONTAINER_YEAR);
            gavl_dictionary_set_string(m, GAVL_META_CHILD_CLASS, GAVL_META_CLASS_SONG);

            if(year < 9999)
              gavl_dictionary_set_string_nocopy(m, GAVL_META_LABEL, bg_sprintf("%d", year));
            else
              gavl_dictionary_set_string(m, GAVL_META_LABEL, "Unknown");
                        
            /* num children */
            sql = bg_sprintf("SELECT count(songs."META_DB_ID") FROM "
                             "songs INNER JOIN song_genres_arr ON "
                             "song_genres_arr.OBJ_ID = songs.DBID WHERE "
                             "song_genres_arr.NAME_ID = %"PRId64" AND songs."GAVL_META_DATE" GLOB '%d*';", genre_id, year);

            gavl_track_set_num_children(ret, 0, bg_sqlite_get_int(s->db, sql));
            
            free(sql);
            
            return 1;
            }
          else if(*id == '/')
            {
            int64_t song_id;

            id++;
            song_id = strtoll(id, NULL, 10);
            
            if(!(obj = query_sqlite_object(b, song_id, TYPE_SONG)))
              return 0;
            
            gavl_dictionary_merge2(m, gavl_track_get_metadata(obj));
            gavl_dictionary_set(m, GAVL_META_LABEL, gavl_dictionary_get(m, GAVL_META_TITLE));

            finalize_metadata_song(m);
            
            gavl_dictionary_destroy(obj);
            return 1;
            }
          else
            return 0;
          
          }
        else
          return 0;
        
        }
      else
        return 0;
      }
    else if(starts_with(&id, "genre")) // songs/genre...
      {
      if(*id == '\0') // songs/genre
        {
        gavl_dictionary_set_string(m, GAVL_META_LABEL, "Genre");
        gavl_dictionary_set_string(m, GAVL_META_CLASS, GAVL_META_CLASS_CONTAINER);
        gavl_dictionary_set_string(m, GAVL_META_CHILD_CLASS, GAVL_META_CLASS_CONTAINER_GENRE);

        gavl_track_set_num_children(ret, bg_sqlite_get_int(s->db, "SELECT COUNT(ID) FROM song_genres;"), 0);
        
        return 1;
        }
      else if(*id == '/') // songs/genre/...
        {
        const char * pos;
        id++;
        genre_id = strtoll(id, &rest, 10);

        if(id == rest)
          return 0;

        id = rest;
        if(*id == '\0')  // songs/genre/1
          {
          gavl_array_t arr;
          gavl_array_init(&arr);
          
          gavl_dictionary_set_string_nocopy(m, GAVL_META_LABEL,
                                     bg_sqlite_id_to_string(s->db,
                                                            "song_genres",
                                                            "NAME", "ID", genre_id));
          
          gavl_dictionary_set_string(m, GAVL_META_CLASS, GAVL_META_CLASS_CONTAINER_GENRE);
          gavl_dictionary_set_string(m, GAVL_META_CHILD_CLASS, GAVL_META_CLASS_CONTAINER);
          
          /* num children */

          sql = bg_sprintf("SELECT songs."GAVL_META_SEARCH_TITLE" "
                           "FROM song_genres_arr INNER JOIN songs "
                           "ON song_genres_arr.OBJ_ID = songs."META_DB_ID" "
                           "WHERE song_genres_arr.NAME_ID = %"PRId64";", genre_id);

          bg_sqlite_exec(s->db, sql, append_string_callback, &arr);

          gavl_track_set_num_children(ret, bg_mdb_get_num_groups(&arr), 0);
          gavl_array_free(&arr);
          free(sql);
          
          return 1;
          }
        else if(*id == '/') // songs/genre/1/...
          {
          id++;

          if(!(pos = strchr(id, '/')))
            {
            
            gavl_dictionary_set_string(m, GAVL_META_CLASS, GAVL_META_CLASS_CONTAINER);
            gavl_dictionary_set_string(m, GAVL_META_CHILD_CLASS, GAVL_META_CLASS_SONG);
            
            gavl_dictionary_set_string(m, GAVL_META_LABEL, bg_mdb_get_group_label(id));

            if(!gavl_dictionary_get(m, GAVL_META_NUM_CHILDREN))
              {
              int64_t num_children = 0;
              char * cond;
              
              cond = bg_sqlite_make_group_condition(id);
            
              sql = bg_sprintf("SELECT count(songs."META_DB_ID") "
                               "FROM song_genres_arr INNER JOIN songs "
                               "ON song_genres_arr.OBJ_ID = songs."META_DB_ID" "
                               "WHERE song_genres_arr.NAME_ID = %"PRId64" AND songs."GAVL_META_SEARCH_TITLE" %s;",
                               genre_id, cond);

              bg_sqlite_exec(s->db, sql, bg_sqlite_int_callback, &num_children);

              gavl_track_set_num_children(ret, 0, num_children);
              
              free(cond);
              free(sql);
              }
            
            return 1;
            }
          else
            {
            int64_t song_id;
            id = pos;
            id++;

            song_id = strtoll(id, NULL, 10);
            
            if(!(obj = query_sqlite_object(b, song_id, TYPE_SONG)))
              return 0;
            
            gavl_dictionary_merge2(m, gavl_track_get_metadata(obj));
            gavl_dictionary_set(m, GAVL_META_LABEL, gavl_dictionary_get(m, GAVL_META_TITLE));

            finalize_metadata_song(m);
            
            gavl_dictionary_destroy(obj);
            return 1;
            }
          
          }
        else
          return 0;

        }
      else
        return 0;
      }

    else if(starts_with(&id, "year"))
      {
      if(*id == '\0')
        {
        gavl_dictionary_set_string(m, GAVL_META_LABEL, "Year");
        gavl_dictionary_set_string(m, GAVL_META_CLASS, GAVL_META_CLASS_CONTAINER);
        gavl_dictionary_set_string(m, GAVL_META_CHILD_CLASS, GAVL_META_CLASS_CONTAINER_YEAR);
        
        gavl_track_set_num_children(ret, bg_sqlite_get_int(s->db,
                                                           "SELECT count(DISTINCT substr("GAVL_META_DATE", 1, 4)) FROM songs;"), 0);
        return 1;
        }
      else if(*id == '/')  // /songs/year/1960...
        {
        int year;

        id++;
          
        year = strtol(id, &rest, 10);

        id = rest;

        if(*id == '\0')  // /songs/year/1960
          {
          gavl_dictionary_set_string(m, GAVL_META_CLASS, GAVL_META_CLASS_CONTAINER_YEAR);
          gavl_dictionary_set_string(m, GAVL_META_CHILD_CLASS, GAVL_META_CLASS_SONG);

          if(year < 9999)
            gavl_dictionary_set_string_nocopy(m, GAVL_META_LABEL, bg_sprintf("%d", year));
          else
            gavl_dictionary_set_string(m, GAVL_META_LABEL, "Unknown");
                        
          /* num children */
          sql = bg_sprintf("SELECT count("META_DB_ID") FROM "
                           "songs WHERE "
                           GAVL_META_DATE" GLOB '%d*';", year);
          
          gavl_track_set_num_children(ret, 0, bg_sqlite_get_int(s->db, sql));
          
          free(sql);
          
          return 1;
          }
#if 1
        else if(*id == '/')
          {
          int64_t song_id;
          
          id++;
          song_id = strtoll(id, NULL, 10);
          
          if(!(obj = query_sqlite_object(b, song_id, TYPE_SONG)))
            return 0;
          
          gavl_dictionary_merge2(m, gavl_track_get_metadata(obj));
          gavl_dictionary_set(m, GAVL_META_LABEL, gavl_dictionary_get(m, GAVL_META_TITLE));
          
          finalize_metadata_song(m);
          
          gavl_dictionary_destroy(obj);
          return 1;
          }
        else
          return 0;
        
#endif
        }
      
      else
        return 0;


      }

    

    }
  else if(s->albums_id && starts_with(&id, s->albums_id))
    {
    if(*id != '/')
      return 0;
    id++;

    if(starts_with(&id, "artist"))  // /albums/artist
      {
      if(*id == '\0')
        {
        gavl_array_t arr;
        gavl_array_init(&arr);

        gavl_dictionary_set_string(m, GAVL_META_LABEL, "Artist");
        gavl_dictionary_set_string(m, GAVL_META_CLASS, GAVL_META_CLASS_CONTAINER);
        gavl_dictionary_set_string(m, GAVL_META_CHILD_CLASS, GAVL_META_CLASS_CONTAINER);
                    
        bg_sqlite_exec(s->db, "SELECT NAME FROM album_artists", append_string_callback, &arr);

        gavl_track_set_num_children(ret, bg_mdb_get_num_groups(&arr), 0);
        gavl_array_free(&arr);
        return 1;
        }
      else if(*id == '/')   // /albums/artist/a
        {
        const char * pos;
        id++;

        if(!(pos = strchr(id, '/'))) // /albums/artist/a
          {
          gavl_array_t arr;
          gavl_array_init(&arr);
          bg_sqlite_exec(s->db, "SELECT NAME FROM album_artists", append_string_callback, &arr);
          gavl_track_set_num_children(ret, bg_mdb_get_group_size(&arr, id), 0);

          gavl_dictionary_set_string(m, GAVL_META_LABEL, bg_mdb_get_group_label(id));
          gavl_dictionary_set_string(m, GAVL_META_CLASS, GAVL_META_CLASS_CONTAINER);
          gavl_dictionary_set_string(m, GAVL_META_CHILD_CLASS, GAVL_META_CLASS_CONTAINER_ARTIST);
          gavl_array_free(&arr);
          return 1;
          }
        else  // /albums/artist/a/152
          {
          int64_t artist_id;
          char * rest = NULL;

          id = pos;
          id++;
          
          artist_id = strtoll(id, &rest, 10);
          
          if(!(pos = strchr(id, '/'))) // /albums/artist/a/152
            {
            gavl_dictionary_set_string(m, GAVL_META_CLASS,
                                       GAVL_META_CLASS_CONTAINER_ARTIST);
            gavl_dictionary_set_string(m, GAVL_META_CHILD_CLASS, GAVL_META_CLASS_MUSICALBUM);
            
            gavl_dictionary_set_string_nocopy(m, GAVL_META_LABEL,
                                       bg_sqlite_id_to_string(s->db,
                                                              "album_artists",
                                                              "NAME", "ID", artist_id));

            /* num_children */
            sql = bg_sprintf("SELECT count(OBJ_ID) "
                             "FROM album_artists_arr WHERE "
                             "NAME_ID = %"PRId64";", artist_id);

            gavl_track_set_num_children(ret, (int)bg_sqlite_get_int(s->db, sql), 0);

            free(sql);
            
            return 1;
            }
          else  // /albums/artist/a/152/1001
            {
            int64_t album_id;
            
            id = pos;
            id++;
            album_id = strtoll(id, &rest, 10);

            id = rest;
            
            if(*id == '\0')
              {
              int year;
              int num_children = 0;
              
              if(!(obj = query_sqlite_object(b, album_id, TYPE_ALBUM)))
                return 0;
              
              gavl_dictionary_merge2(m, gavl_track_get_metadata(obj));

              if((year = gavl_dictionary_get_year(m,
                                                  GAVL_META_DATE)))
                {
                gavl_dictionary_set_string_nocopy(m, GAVL_META_LABEL,
                                                  bg_sprintf("%s (%d)",
                                                             gavl_dictionary_get_string(m, GAVL_META_TITLE), year));
                }
              else
                {
                gavl_dictionary_set(m, GAVL_META_LABEL,
                                    gavl_dictionary_get(m, GAVL_META_TITLE));
                }

              if(gavl_dictionary_get_int(m, GAVL_META_NUM_CHILDREN, &num_children))
                gavl_dictionary_set_int(m, GAVL_META_NUM_ITEM_CHILDREN, num_children);

              gavl_dictionary_set_int(m, GAVL_META_NUM_CONTAINER_CHILDREN, 0);

              finalize_metadata_album(m);
              
              gavl_dictionary_destroy(obj);
              return 1;
              }
            else if(*id == '/')
              {
              int64_t song_id;

              id++;
              song_id = strtoll(id, &rest, 10);
              
              if(!(obj = query_sqlite_object(b, song_id, TYPE_SONG)))
                return 0;
              
              gavl_dictionary_merge2(m, gavl_track_get_metadata(obj));
              gavl_dictionary_set(m, GAVL_META_LABEL, gavl_dictionary_get(m, GAVL_META_TITLE));

              finalize_metadata_song(m);
              
              gavl_dictionary_destroy(obj);
              return 1;
              }
            else
              return 0;
            
            }
          }
        }
      else
        return 0;
      }
    else if(starts_with(&id, "genre-artist"))
      {
      if(*id == '\0') // /albums/genre-artist
        {
        gavl_dictionary_set_string(m, GAVL_META_LABEL, "Genre-Artist");
        gavl_dictionary_set_string(m, GAVL_META_CLASS, GAVL_META_CLASS_CONTAINER);
        gavl_dictionary_set_string(m, GAVL_META_CHILD_CLASS, GAVL_META_CLASS_CONTAINER_GENRE);

        gavl_track_set_num_children(ret, bg_sqlite_get_int(s->db, "SELECT COUNT(ID) FROM album_genres;"), 0);
        
        return 1;
        }
      else if(*id == '/')  // /albums/genre-artist/1
        {
        id++;
        genre_id = strtoll(id, &rest, 10);

        if(id == rest)
          return 0;

        id = rest;
        if(*id == '\0')  // /albums/genre-artist/1
          {
          gavl_dictionary_set_string_nocopy(m, GAVL_META_LABEL,
                                     bg_sqlite_id_to_string(s->db,
                                                            "album_genres",
                                                            "NAME", "ID", genre_id));
          
          gavl_dictionary_set_string(m, GAVL_META_CLASS, GAVL_META_CLASS_CONTAINER_GENRE);
          gavl_dictionary_set_string(m, GAVL_META_CHILD_CLASS, GAVL_META_CLASS_CONTAINER_ARTIST);

          sql = bg_sprintf("SELECT count(DISTINCT album_artists_arr.NAME_ID) "
                           "FROM album_artists_arr INNER JOIN album_genres_arr "
                           "ON album_artists_arr.OBJ_ID = album_genres_arr.OBJ_ID "
                           "WHERE album_genres_arr.NAME_ID = %"PRId64";", genre_id);

          gavl_track_set_num_children(ret, (int)bg_sqlite_get_int(s->db, sql), 0);
          free(sql);

          return 1;
          }
        else if(*id == '/') // /albums/genre-artist/1/123
          {
          id++;
          artist_id = strtoll(id, &rest, 10);

          id = rest;

          if(*id == '\0')
            {
            gavl_dictionary_set_string(m, GAVL_META_CLASS, GAVL_META_CLASS_CONTAINER_ARTIST);
            gavl_dictionary_set_string(m, GAVL_META_CHILD_CLASS, GAVL_META_CLASS_MUSICALBUM);

            gavl_dictionary_set_string_nocopy(m, GAVL_META_LABEL,
                                       bg_sqlite_id_to_string(s->db,
                                                              "album_artists",
                                                              "NAME", "ID", artist_id));

            sql = bg_sprintf("SELECT count(album_artists_arr.OBJ_ID) "
                             "FROM album_artists_arr INNER JOIN album_genres_arr "
                             "ON album_artists_arr.OBJ_ID = album_genres_arr.OBJ_ID "
                             "WHERE album_genres_arr.NAME_ID = %"PRId64" AND "
                             "album_artists_arr.NAME_ID = %"PRId64";", genre_id, artist_id);

            gavl_track_set_num_children(ret, (int)bg_sqlite_get_int(s->db, sql), 0);
            free(sql);
            return 1;
            }
          else if(*id == '/')
            {
            // /albums/genre-artist/1/123/1000

            int64_t obj_id;
            id++;

            obj_id = strtoll(id, &rest, 10);

            id = rest;

            if(*id == '\0')
              {
              int year;
              int num_children = 0;
              
              if(!(obj = query_sqlite_object(b, obj_id, TYPE_ALBUM)))
                return 0;
            
              gavl_dictionary_merge2(m, gavl_dictionary_get_dictionary(obj, GAVL_META_METADATA));
              
              if((year = gavl_dictionary_get_year(m,
                                                  GAVL_META_DATE)))
                {
                gavl_dictionary_set_string_nocopy(m, GAVL_META_LABEL,
                                                  bg_sprintf("%s (%d)",
                                                             gavl_dictionary_get_string(m, GAVL_META_TITLE), year));
                }
              else
                {
                gavl_dictionary_set(m, GAVL_META_LABEL,
                                    gavl_dictionary_get(m, GAVL_META_TITLE));
                }

              if(gavl_dictionary_get_int(m, GAVL_META_NUM_CHILDREN, &num_children))
                gavl_dictionary_set_int(m, GAVL_META_NUM_ITEM_CHILDREN, num_children);

              gavl_dictionary_set_int(m, GAVL_META_NUM_CONTAINER_CHILDREN, 0);

              finalize_metadata_album(m);
                            
              gavl_dictionary_destroy(obj);
              return 1;
              }
            else if(*id == '/') // /albums/genre-artist/1/123/1000/1001
              {
              id++;
              obj_id = strtoll(id, NULL, 10);
              
              if(!(obj = query_sqlite_object(b, obj_id, TYPE_SONG)))
                return 0;
            
              gavl_dictionary_merge2(m, gavl_dictionary_get_dictionary(obj, GAVL_META_METADATA));
              gavl_dictionary_set(m, GAVL_META_LABEL, gavl_dictionary_get(m, GAVL_META_TITLE));

              finalize_metadata_song(m);
              
              gavl_dictionary_destroy(obj);
              return 1;
              }
            
            }
          else
            return 0;
          }
        else
          return 0;

        }
      else
        return 0;
      }
    else if(starts_with(&id, "genre-year"))
      {
      if(*id == '\0')
        {
        gavl_dictionary_set_string(m, GAVL_META_LABEL, "Genre-Year");
        gavl_dictionary_set_string(m, GAVL_META_CLASS, GAVL_META_CLASS_CONTAINER);
        gavl_dictionary_set_string(m, GAVL_META_CHILD_CLASS, GAVL_META_CLASS_CONTAINER_GENRE);
        
        gavl_track_set_num_children(ret, bg_sqlite_get_int(s->db, "SELECT COUNT(ID) FROM album_genres;"), 0);
        return 1;
        }
      else if(*id == '/')
        {
        id++;
        genre_id = strtoll(id, &rest, 10);

        if(id == rest)
          return 0;
        id = rest;
        if(*id == '\0')
          {
          gavl_dictionary_set_string_nocopy(m, GAVL_META_LABEL,
                                     bg_sqlite_id_to_string(s->db,
                                                            "album_genres",
                                                            "NAME", "ID", genre_id));
          
          gavl_dictionary_set_string(m, GAVL_META_CLASS, GAVL_META_CLASS_CONTAINER_GENRE);
          gavl_dictionary_set_string(m, GAVL_META_CHILD_CLASS, GAVL_META_CLASS_CONTAINER_YEAR);

          sql = bg_sprintf("SELECT count(DISTINCT substr(albums."GAVL_META_DATE", 1, 4)) FROM "
                           "albums INNER JOIN album_genres_arr ON "
                           "album_genres_arr.OBJ_ID = albums.DBID WHERE "
                           "album_genres_arr.NAME_ID = %"PRId64" ;", genre_id);

          gavl_track_set_num_children(ret, bg_sqlite_get_int(s->db, sql), 0);
          free(sql);

          return 1;
          }

        else if(*id == '/')  // /albums/genre-year/1/1960...
          {
          int year;

          id++;
          
          year = strtol(id, &rest, 10);

          id = rest;

          if(*id == '\0')  // /albums/genre-year/1/1960
            {
            gavl_dictionary_set_string(m, GAVL_META_CLASS, GAVL_META_CLASS_CONTAINER_YEAR);
            gavl_dictionary_set_string(m, GAVL_META_CHILD_CLASS, GAVL_META_CLASS_MUSICALBUM);

            if(year < 9999)
              gavl_dictionary_set_string_nocopy(m, GAVL_META_LABEL, bg_sprintf("%d", year));
            else
              gavl_dictionary_set_string(m, GAVL_META_LABEL, "Unknown");
            
            /* num children */
            sql = bg_sprintf("SELECT count(albums."META_DB_ID") FROM "
                             "albums INNER JOIN album_genres_arr ON "
                             "album_genres_arr.OBJ_ID = albums.DBID WHERE "
                             "album_genres_arr.NAME_ID = %"PRId64" AND albums."GAVL_META_DATE" GLOB '%d*';", genre_id, year);

            gavl_track_set_num_children(ret, bg_sqlite_get_int(s->db, sql), 0);
            free(sql);
            
            return 1;
            }
          else if(*id == '/')
            {
#if 1
            int64_t album_id;
            const char * pos;
            
            id++;

            if(!(pos = strchr(id, '/')))
              {
              int num_children = 0;
              
              album_id = strtoll(id, NULL, 10);
            
              if(!(obj = query_sqlite_object(b, album_id, TYPE_ALBUM)))
                return 0;
            
              gavl_dictionary_merge2(m, gavl_track_get_metadata(obj));
              gavl_dictionary_set(m, GAVL_META_LABEL, gavl_dictionary_get(m, GAVL_META_TITLE));

              if(gavl_dictionary_get_int(m, GAVL_META_NUM_CHILDREN, &num_children))
                gavl_dictionary_set_int(m, GAVL_META_NUM_ITEM_CHILDREN, num_children);

              gavl_dictionary_set_int(m, GAVL_META_NUM_CONTAINER_CHILDREN, 0);

              finalize_metadata_album(m);
                            
              gavl_dictionary_destroy(obj);
              return 1;
              }
            else
              {
              int64_t song_id;
              id = pos;
              id++;
              song_id = strtoll(id, &rest, 10);
              
              if(!(obj = query_sqlite_object(b, song_id, TYPE_SONG)))
                return 0;
              
              gavl_dictionary_merge2(m, gavl_track_get_metadata(obj));
              gavl_dictionary_set(m, GAVL_META_LABEL, gavl_dictionary_get(m, GAVL_META_TITLE));

              finalize_metadata_song(m);
              
              gavl_dictionary_destroy(obj);
              return 1;
              }
            
#endif
            }
          else
            return 0;
          }
        else
          return 0;

        }
      else
        return 0;
      }

    else if(starts_with(&id, "year"))
      {
      if(*id == '\0')
        {
        gavl_dictionary_set_string(m, GAVL_META_LABEL, "Year");
        gavl_dictionary_set_string(m, GAVL_META_CLASS, GAVL_META_CLASS_CONTAINER);
        gavl_dictionary_set_string(m, GAVL_META_CHILD_CLASS, GAVL_META_CLASS_CONTAINER_YEAR);
        
        gavl_track_set_num_children(ret, bg_sqlite_get_int(s->db,
                                                           "SELECT count(DISTINCT substr("GAVL_META_DATE", 1, 4)) FROM albums;"), 0);
        return 1;
        }
      else if(*id == '/')  // /albums/year/1960...
        {
        int year;

        id++;
          
        year = strtol(id, &rest, 10);

        id = rest;

        if(*id == '\0')  // /albums/year/1960
          {
          gavl_dictionary_set_string(m, GAVL_META_CLASS, GAVL_META_CLASS_CONTAINER_YEAR);
          gavl_dictionary_set_string(m, GAVL_META_CHILD_CLASS, GAVL_META_CLASS_MUSICALBUM);

          if(year < 9999)
            gavl_dictionary_set_string_nocopy(m, GAVL_META_LABEL, bg_sprintf("%d", year));
          else
            gavl_dictionary_set_string(m, GAVL_META_LABEL, "Unknown");
                        
          /* num children */
          sql = bg_sprintf("SELECT count("META_DB_ID") FROM "
                           "albums WHERE "
                           GAVL_META_DATE" GLOB '%d*';", year);
          
          gavl_track_set_num_children(ret, bg_sqlite_get_int(s->db, sql), 0);
          
          free(sql);
          
          return 1;
          }

        else if(*id == '/')
          {
          
          int64_t obj_id;
          id++;

          obj_id = strtoll(id, &rest, 10);

          id = rest;

          if(*id == '\0') // /albums/year/1960/1000/1001
            {
            int num_children = 0;
              
            if(!(obj = query_sqlite_object(b, obj_id, TYPE_ALBUM)))
              return 0;
            
            gavl_dictionary_merge2(m, gavl_dictionary_get_dictionary(obj, GAVL_META_METADATA));
              
            gavl_dictionary_set(m, GAVL_META_LABEL,
                                gavl_dictionary_get(m, GAVL_META_TITLE));
            
            if(gavl_dictionary_get_int(m, GAVL_META_NUM_CHILDREN, &num_children))
              gavl_dictionary_set_int(m, GAVL_META_NUM_ITEM_CHILDREN, num_children);

            gavl_dictionary_set_int(m, GAVL_META_NUM_CONTAINER_CHILDREN, 0);

            finalize_metadata_album(m);
                            
            gavl_dictionary_destroy(obj);
            return 1;
            }
          else if(*id == '/') // /albums/year/1960/1000/1001
            {
            id++;
            obj_id = strtoll(id, NULL, 10);
              
            if(!(obj = query_sqlite_object(b, obj_id, TYPE_SONG)))
              return 0;
            
            gavl_dictionary_merge2(m, gavl_dictionary_get_dictionary(obj, GAVL_META_METADATA));
            gavl_dictionary_set(m, GAVL_META_LABEL, gavl_dictionary_get(m, GAVL_META_TITLE));

            finalize_metadata_song(m);
              
            gavl_dictionary_destroy(obj);
            return 1;
            }
            
          }
        else
          return 0;
        }
      
      else
        return 0;
      }
    
    }
  else if(s->movies_id && starts_with(&id, s->movies_id))
    {
    if(*id != '/')
      return 0;
    id++;

    if(starts_with(&id, "actor"))
      {
      if(*id == '\0')
        {
        gavl_array_t arr;
        gavl_array_init(&arr);

        gavl_dictionary_set_string(m, GAVL_META_LABEL, "Actor");
        gavl_dictionary_set_string(m, GAVL_META_CLASS, GAVL_META_CLASS_CONTAINER);
        gavl_dictionary_set_string(m, GAVL_META_CHILD_CLASS, GAVL_META_CLASS_CONTAINER);
        
        /* num children */
        
        bg_sqlite_exec(s->db, "SELECT NAME FROM movie_actors", append_string_callback, &arr);

        gavl_track_set_num_children(ret, bg_mdb_get_num_groups(&arr), 0);
        gavl_array_free(&arr);
        
        return 1;
        }
      else if(*id == '/')
        {
        char * pos;
        id++;
        
        if(!(pos = strchr(id, '/'))) // /movies/actor/a
          {
          gavl_array_t arr;
          gavl_array_init(&arr);
          bg_sqlite_exec(s->db, "SELECT NAME FROM movie_actors", append_string_callback, &arr);

          gavl_track_set_num_children(ret, bg_mdb_get_group_size(&arr, id), 0);

          gavl_dictionary_set_string(m, GAVL_META_LABEL, bg_mdb_get_group_label(id));
          gavl_dictionary_set_string(m, GAVL_META_CLASS, GAVL_META_CLASS_CONTAINER);
          gavl_dictionary_set_string(m, GAVL_META_CHILD_CLASS, GAVL_META_CLASS_CONTAINER_ACTOR);
          gavl_array_free(&arr);
          return 1;
          }
        else  //  /movies/actor/a/152
          {
          int64_t actor_id;
          id = pos;
          id++;
          
          actor_id = strtoll(id, &rest, 10);
          
          id = rest;
          
          if(*id == '\0')
            {
            gavl_dictionary_set_string(m, GAVL_META_CLASS, GAVL_META_CLASS_CONTAINER_ACTOR);
            gavl_dictionary_set_string(m, GAVL_META_CHILD_CLASS, GAVL_META_CLASS_MOVIE);
            
            gavl_dictionary_set_string_nocopy(m, GAVL_META_LABEL,
                                              bg_sqlite_id_to_string(s->db,
                                                                     "movie_actors",
                                                                     "NAME", "ID", actor_id));

            
            sql = bg_sprintf("SELECT count(OBJ_ID) "
                             "FROM movie_actors_arr WHERE "
                             "NAME_ID = %"PRId64";", actor_id);

            gavl_track_set_num_children(ret, 0, (int)bg_sqlite_get_int(s->db, sql));
            free(sql);
            
            return 1;
            }
          else if(*id == '/') //   //  /movies/actor/a/152/22009
            {
            int64_t movie_id;
            id++;
            movie_id = strtoll(id, &rest, 10);
            
            if(!(obj = query_sqlite_object(b, movie_id, TYPE_MOVIE)))
              return 0;
            
            gavl_dictionary_merge2(m, gavl_track_get_metadata(obj));
            gavl_dictionary_set(m, GAVL_META_LABEL, gavl_dictionary_get(m, GAVL_META_TITLE));

            finalize_metadata_movie(m);
            /* Copy parts */
            gavl_dictionary_set(ret, GAVL_META_PARTS, gavl_dictionary_get(obj, GAVL_META_PARTS));
            
            
            gavl_dictionary_destroy(obj);
            return 1;
            }
          else
            return 0;
          }
        }
      else
        return 0;
      }
    else if(starts_with(&id, "all"))
      {
      if(*id == '\0')
        {
        gavl_dictionary_set_string(m, GAVL_META_LABEL, "All");
        gavl_dictionary_set_string(m, GAVL_META_CLASS, GAVL_META_CLASS_CONTAINER);
        gavl_dictionary_set_string(m, GAVL_META_CHILD_CLASS, GAVL_META_CLASS_MOVIE);

        /* num_children */

        gavl_track_set_num_children(ret, 0, bg_sqlite_get_int(s->db, "SELECT count("META_DB_ID") FROM movies"));
        return 1;
        }
      else if(*id == '/')
        {
        int64_t movie_id;
        id++;
        movie_id = strtoll(id, &rest, 10);
            
        if(!(obj = query_sqlite_object(b, movie_id, TYPE_MOVIE)))
          return 0;
            
        gavl_dictionary_merge2(m, gavl_track_get_metadata(obj));
        gavl_dictionary_set(m, GAVL_META_LABEL, gavl_dictionary_get(m, GAVL_META_TITLE));
        finalize_metadata_movie(m);
        /* Copy parts */
        gavl_dictionary_set(ret, GAVL_META_PARTS, gavl_dictionary_get(obj, GAVL_META_PARTS));

        gavl_dictionary_destroy(obj);
        return 1;
        }
      else
        return 0;
      }
    else if(starts_with(&id, "country"))
      {
      if(*id == '\0')
        {
        gavl_dictionary_set_string(m, GAVL_META_LABEL, "Country");
        gavl_dictionary_set_string(m, GAVL_META_CLASS, GAVL_META_CLASS_CONTAINER);
        gavl_dictionary_set_string(m, GAVL_META_CHILD_CLASS, GAVL_META_CLASS_CONTAINER_COUNTRY);

        /* num_children */

        gavl_track_set_num_children(ret, bg_sqlite_get_int(s->db, "SELECT count(ID) FROM movie_countries"), 0);
        return 1;
        }
      else if(*id == '/')
        {
        int64_t country_id;
        id++;

        country_id = strtoll(id, &rest, 10);

        id = rest;

        if(*id == '\0')
          {
          gavl_dictionary_set_string(m, GAVL_META_CLASS, GAVL_META_CLASS_CONTAINER_COUNTRY);
          gavl_dictionary_set_string(m, GAVL_META_CHILD_CLASS, GAVL_META_CLASS_MOVIE);

          gavl_dictionary_set_string_nocopy(m, GAVL_META_LABEL,
                                            bg_sqlite_id_to_string(s->db,
                                                                   "movie_countries",
                                                                   "NAME", "ID", country_id));
          
          sql = bg_sprintf("SELECT count(OBJ_ID) "
                           "FROM movie_countries_arr WHERE "
                           "NAME_ID = %"PRId64";", country_id);

          gavl_track_set_num_children(ret, 0, (int)bg_sqlite_get_int(s->db, sql));
          free(sql);
          
          return 1;
          }
        else if(*id == '/')
          {
          int64_t movie_id;
          id++;
          movie_id = strtoll(id, &rest, 10);
            
          if(!(obj = query_sqlite_object(b, movie_id, TYPE_MOVIE)))
            return 0;
            
          gavl_dictionary_merge2(m, gavl_track_get_metadata(obj));
          gavl_dictionary_set(m, GAVL_META_LABEL, gavl_dictionary_get(m, GAVL_META_TITLE));
          finalize_metadata_movie(m);
          /* Copy parts */
          gavl_dictionary_set(ret, GAVL_META_PARTS, gavl_dictionary_get(obj, GAVL_META_PARTS));

          gavl_dictionary_destroy(obj);
          return 1;
          }
        else
          return 0;

        
        }
      else
        return 0;
      }
    else if(starts_with(&id, "language"))
      {
      if(*id == '\0')
        {
        gavl_dictionary_set_string(m, GAVL_META_LABEL, "Language");
        gavl_dictionary_set_string(m, GAVL_META_CLASS, GAVL_META_CLASS_CONTAINER);
        gavl_dictionary_set_string(m, GAVL_META_CHILD_CLASS, GAVL_META_CLASS_CONTAINER_LANGUAGE);

        /* num_children */

        gavl_track_set_num_children(ret, bg_sqlite_get_int(s->db,
                                                           "SELECT count(ID) FROM movie_audio_languages"), 0);
        
        return 1;
        }
      else if(*id == '/')
        {
        int64_t language_id;
        id++;

        language_id = strtoll(id, &rest, 10);

        id = rest;

        if(*id == '\0')
          {
          gavl_dictionary_set_string(m, GAVL_META_CLASS, GAVL_META_CLASS_CONTAINER_LANGUAGE);
          gavl_dictionary_set_string(m, GAVL_META_CHILD_CLASS, GAVL_META_CLASS_MOVIE);

          gavl_dictionary_set_string_nocopy(m, GAVL_META_LABEL,
                                            bg_sqlite_id_to_string(s->db,
                                                                   "movie_audio_languages",
                                                                   "NAME", "ID", language_id));
          
          sql = bg_sprintf("SELECT count(OBJ_ID) "
                           "FROM movie_audio_languages_arr WHERE "
                           "NAME_ID = %"PRId64";", language_id);

          gavl_track_set_num_children(ret, 0, (int)bg_sqlite_get_int(s->db, sql));
          free(sql);
          
          return 1;
          }
        else if(*id == '/')
          {
          int64_t movie_id;
          id++;
          movie_id = strtoll(id, &rest, 10);
            
          if(!(obj = query_sqlite_object(b, movie_id, TYPE_MOVIE)))
            return 0;
            
          gavl_dictionary_merge2(m, gavl_track_get_metadata(obj));
          gavl_dictionary_set(m, GAVL_META_LABEL, gavl_dictionary_get(m, GAVL_META_TITLE));
          finalize_metadata_movie(m);
          /* Copy parts */
          gavl_dictionary_set(ret, GAVL_META_PARTS, gavl_dictionary_get(obj, GAVL_META_PARTS));
          
          gavl_dictionary_destroy(obj);
          return 1;
          }
        else
          return 0;

        
        }
      else
        return 0;
      }
    
    else if(starts_with(&id, "director"))
      {
      if(*id == '\0')
        {
        gavl_array_t arr;
        gavl_array_init(&arr);

        gavl_dictionary_set_string(m, GAVL_META_LABEL, "Director");
        gavl_dictionary_set_string(m, GAVL_META_CLASS, GAVL_META_CLASS_CONTAINER);
        gavl_dictionary_set_string(m, GAVL_META_CHILD_CLASS, GAVL_META_CLASS_CONTAINER);

        /* num children */
        
        bg_sqlite_exec(s->db, "SELECT NAME FROM movie_directors", append_string_callback, &arr);

        gavl_track_set_num_children(ret, bg_mdb_get_num_groups(&arr), 0);
        gavl_array_free(&arr);
        
        return 1;
        }
      else if(*id == '/')
        {
        id++;

        if(*id == '\0')
          {
          /* Invalid ID */
          gavl_dictionary_set_string(m, GAVL_META_CLASS, GAVL_META_CLASS_CONTAINER);
          return 1;
          }
        else
          {
          char * pos;
          
          if(!(pos = strchr(id, '/'))) // /movies/director/a
            {
            gavl_array_t arr;
            gavl_array_init(&arr);

            bg_sqlite_exec(s->db, "SELECT NAME FROM movie_actors", append_string_callback, &arr);

            gavl_track_set_num_children(ret, bg_mdb_get_group_size(&arr, id), 0);
            gavl_dictionary_set_string(m, GAVL_META_LABEL, bg_mdb_get_group_label(id));
            gavl_dictionary_set_string(m, GAVL_META_CLASS, GAVL_META_CLASS_CONTAINER);
            gavl_dictionary_set_string(m, GAVL_META_CHILD_CLASS, GAVL_META_CLASS_CONTAINER_DIRECTOR);

            gavl_array_free(&arr);
            return 1;
            }
          else  // /movies/director/a/152
            {
            int64_t director_id;
            id = pos;
            id++;
          
            director_id = strtoll(id, &rest, 10);
          
            id = rest;
          
            if(*id == '\0')
              {
              gavl_dictionary_set_string(m, GAVL_META_CLASS, GAVL_META_CLASS_CONTAINER_DIRECTOR);
              gavl_dictionary_set_string(m, GAVL_META_CHILD_CLASS, GAVL_META_CLASS_MOVIE);

              gavl_dictionary_set_string_nocopy(m, GAVL_META_LABEL,
                                                bg_sqlite_id_to_string(s->db,
                                                                       "movie_directors",
                                                                       "NAME", "ID", director_id));
            

              sql = bg_sprintf("SELECT count(OBJ_ID) "
                               "FROM movie_directors_arr WHERE "
                               "NAME_ID = %"PRId64";", director_id);

              gavl_track_set_num_children(ret, 0, (int)bg_sqlite_get_int(s->db, sql));
              free(sql);
              
              return 1;
              }
            else if(*id == '/')
              {
              int64_t movie_id;
              id++;
              movie_id = strtoll(id, &rest, 10);
            
              if(!(obj = query_sqlite_object(b, movie_id, TYPE_MOVIE)))
                return 0;
            
              gavl_dictionary_merge2(m, gavl_track_get_metadata(obj));
              gavl_dictionary_set(m, GAVL_META_LABEL, gavl_dictionary_get(m, GAVL_META_TITLE));
              finalize_metadata_movie(m);
              /* Copy parts */
              gavl_dictionary_set(ret, GAVL_META_PARTS, gavl_dictionary_get(obj, GAVL_META_PARTS));
              
              gavl_dictionary_destroy(obj);
              return 1;
              }
            else
              return 0;
            }
          }
        }
      }
    else if(starts_with(&id, "genre"))
      {
      if(*id == '\0')
        {
        gavl_dictionary_set_string(m, GAVL_META_LABEL, "Genre");
        gavl_dictionary_set_string(m, GAVL_META_CLASS, GAVL_META_CLASS_CONTAINER);
        gavl_dictionary_set_string(m, GAVL_META_CHILD_CLASS, GAVL_META_CLASS_CONTAINER_GENRE);

        /* num_children */

        gavl_track_set_num_children(ret, bg_sqlite_get_int(s->db, "SELECT COUNT(ID) FROM movie_genres;"), 0);
        return 1;
        }
      else if(*id == '/') // /movies/genre/1
        {
        int64_t genre_id;
        id++;
        
        genre_id = strtoll(id, &rest, 10);
        
        id = rest;

        if(*id == '\0')
          {
          gavl_dictionary_set_string(m, GAVL_META_CLASS, GAVL_META_CLASS_CONTAINER_GENRE);
          gavl_dictionary_set_string(m, GAVL_META_CHILD_CLASS, GAVL_META_CLASS_MOVIE);

          gavl_dictionary_set_string_nocopy(m, GAVL_META_LABEL,
                                            bg_sqlite_id_to_string(s->db,
                                                                   "movie_genres",
                                                                   "NAME", "ID", genre_id));

          sql = bg_sprintf("SELECT count(OBJ_ID) "
                           "FROM movie_genres_arr WHERE "
                           "NAME_ID = %"PRId64";", genre_id);

          gavl_track_set_num_children(ret, 0, (int)bg_sqlite_get_int(s->db, sql));
          free(sql);
          
          return 1;
          }
        else if(*id == '/')
          {
          int64_t movie_id;
          id++;
          movie_id = strtoll(id, &rest, 10);
            
          if(!(obj = query_sqlite_object(b, movie_id, TYPE_MOVIE)))
            return 0;
            
          gavl_dictionary_merge2(m, gavl_track_get_metadata(obj));
          gavl_dictionary_set(m, GAVL_META_LABEL, gavl_dictionary_get(m, GAVL_META_TITLE));
          finalize_metadata_movie(m);
          /* Copy parts */
          gavl_dictionary_set(ret, GAVL_META_PARTS, gavl_dictionary_get(obj, GAVL_META_PARTS));
          
          gavl_dictionary_destroy(obj);
          return 1;
          }
        else
          return 0;
        }
      else
        return 0;
      }
    else if(starts_with(&id, "year"))
      {
      if(*id == '\0')
        {
        gavl_dictionary_set_string(m, GAVL_META_LABEL, "Year");
        gavl_dictionary_set_string(m, GAVL_META_CLASS, GAVL_META_CLASS_CONTAINER);
        gavl_dictionary_set_string(m, GAVL_META_CHILD_CLASS, GAVL_META_CLASS_CONTAINER_YEAR);

        /* num_children */

        gavl_track_set_num_children(ret, bg_sqlite_get_int(s->db,
                                                           "SELECT count(DISTINCT substr("GAVL_META_DATE", 1, 4)) FROM movies"), 0);
        
        return 1;
        }
      else if(*id == '/')
        {
        int year;
        id++;
        
        year = strtol(id, &rest, 10);
        
        id = rest;

        if(*id == '\0')
          {
          gavl_dictionary_set_string(m, GAVL_META_CLASS, GAVL_META_CLASS_CONTAINER_YEAR);
          gavl_dictionary_set_string(m, GAVL_META_CHILD_CLASS, GAVL_META_CLASS_MOVIE);

          gavl_dictionary_set_string_nocopy(m, GAVL_META_LABEL, bg_sprintf("%d", year));

          /* num_children */
          sql = bg_sprintf("SELECT count("META_DB_ID") FROM movies where substr("GAVL_META_DATE", 1, 4) = '%d';", year);

          gavl_track_set_num_children(ret, 0, bg_sqlite_get_int(s->db, sql));
          free(sql);
          return 1;
          }
        else if(*id == '/')
          {
          int64_t movie_id;
          id++;
          movie_id = strtoll(id, &rest, 10);
            
          if(!(obj = query_sqlite_object(b, movie_id, TYPE_MOVIE)))
            return 0;
            
          gavl_dictionary_merge2(m, gavl_track_get_metadata(obj));
          gavl_dictionary_set(m, GAVL_META_LABEL, gavl_dictionary_get(m, GAVL_META_TITLE));
          finalize_metadata_movie(m);
          /* Copy parts */
          gavl_dictionary_set(ret, GAVL_META_PARTS, gavl_dictionary_get(obj, GAVL_META_PARTS));
          
          gavl_dictionary_destroy(obj);
          return 1;
          }
        else
          return 0;
        }
      else
        return 0;
      }
    }
  else if(s->series_id && starts_with(&id, s->series_id))
    {
    if(*id != '/')
      return 0;
    id++;

    if(starts_with(&id, "all"))
      {
      if(*id == '\0') // /series/all
        {
        gavl_dictionary_set_string(m, GAVL_META_LABEL, "All");
        gavl_dictionary_set_string(m, GAVL_META_CLASS, GAVL_META_CLASS_CONTAINER);
        gavl_dictionary_set_string(m, GAVL_META_CHILD_CLASS, GAVL_META_CLASS_TV_SHOW);

        gavl_track_set_num_children(ret, bg_sqlite_get_int(s->db, "SELECT COUNT("META_DB_ID") FROM shows;"), 0);
        
        return 1;
        }
      else if(*id == '/')  // /series/all/..
        {
        return browse_object_series(b, id, ret);
        }
      else
        return 0;
      }
    else if(starts_with(&id, "genre"))
      {
      if(*id == '\0')
        {
        gavl_dictionary_set_string(m, GAVL_META_LABEL, "Genre");
        gavl_dictionary_set_string(m, GAVL_META_CLASS, GAVL_META_CLASS_CONTAINER);
        gavl_dictionary_set_string(m, GAVL_META_CHILD_CLASS, GAVL_META_CLASS_CONTAINER_GENRE);
        
        gavl_track_set_num_children(ret, bg_sqlite_get_int(s->db, "SELECT COUNT(ID) FROM show_genres;"), 0);
        return 1;
        }
      else if(*id == '/')
        {
        id++;

        genre_id = strtoll(id, &rest, 10);
        
        if(id == rest)
          return 0;
        id = rest;
        if(*id == '\0')
          {
          gavl_dictionary_set_string_nocopy(m, GAVL_META_LABEL,
                                     bg_sqlite_id_to_string(s->db,
                                                            "show_genres",
                                                            "NAME", "ID", genre_id));
          
          gavl_dictionary_set_string(m, GAVL_META_CLASS, GAVL_META_CLASS_CONTAINER_GENRE);
          /* TODO: num children */

          sql = bg_sprintf("SELECT count(OBJ_ID) "
                           "FROM show_genres_arr WHERE "
                           "NAME_ID = %"PRId64";", genre_id);

          gavl_track_set_num_children(ret, (int)bg_sqlite_get_int(s->db, sql), 0);
          free(sql);

          return 1;
          }
        else if(*id == '/')
          return browse_object_series(b, id, ret);
        else
          return 0;

        }
      else
        return 0;
      }
    }
  
  return 0;
  }

static int browse_object(bg_mdb_backend_t * b, const char * id_p, gavl_dictionary_t * ret)
  {
  char * parent_id = NULL;
  
  if(!browse_object_internal(b, id_p, ret))
    return 0;

  if(!id_p)
    id_p = gavl_track_get_id(ret);
  
  /* Get next and previous */
  if((parent_id = bg_mdb_get_parent_id(id_p)))
    {
    gavl_array_t arr;
    gavl_array_init(&arr);
    
    if(browse_children_ids(b, parent_id, &arr))
      {
      int i;
      const char * last_id = NULL;
      const char * id = NULL;
      const gavl_dictionary_t * dict;

      for(i = 0; i < arr.num_entries; i++)
        {
        if((dict = gavl_value_get_dictionary(&arr.entries[i])) &&
           (id = gavl_track_get_id(dict)))
          {
          if(!strcmp(id, id_p))
            {
            gavl_dictionary_t * m;
            m = gavl_track_get_metadata_nc(ret);
            
            if(last_id)
              gavl_dictionary_set_string(m, GAVL_META_PREVIOUS_ID, last_id);

            if((i < arr.num_entries-1) &&
               (dict = gavl_value_get_dictionary(&arr.entries[i+1])) &&
               (id = gavl_track_get_id(dict)))
              gavl_dictionary_set_string(m, GAVL_META_NEXT_ID, id);
            break;
            }
          last_id = id;
          }
        }
      }
    
    gavl_array_free(&arr);
    free(parent_id);
    }
  //  bg_track_find_subtitles(ret);
  bg_mdb_add_http_uris(b->db, ret);
  return 1;
  }

typedef struct
  {
  gavl_array_t * arr;
  const char * parent_id;
  } append_id_t;

static gavl_dictionary_t * append_id(gavl_array_t * arr, char * id)
  {
  gavl_dictionary_t * dict;
  gavl_dictionary_t * m;

  gavl_value_t val;

  gavl_value_init(&val);
    
  dict = gavl_value_set_dictionary(&val);
  m = gavl_dictionary_get_dictionary_create(dict, GAVL_META_METADATA);
  gavl_dictionary_set_string_nocopy(m, GAVL_META_ID, id);
  gavl_array_splice_val_nocopy(arr, -1, 0, &val);
  return dict;
  }

static int append_id_callback(void * data, int argc, char **argv, char **azColName)
  {
  append_id_t * a = data;
  append_id(a->arr, bg_sprintf("%s/%s", a->parent_id, argv[0]));
  return 0;
  }

static int browse_children_series(bg_mdb_backend_t * b, const char * id,
                                  append_id_t * a)
  {
  char * rest;
  int64_t series_id;
  char * sql;
  sqlite_priv_t * s;

  id++;

  series_id = strtoll(id, &rest, 10);

  id = rest;

  s = b->priv;

  if(*id == '\0')
    {
    sql = bg_sprintf("SELECT "META_DB_ID" FROM seasons WHERE "META_PARENT_ID" = %"PRId64" ORDER BY "GAVL_META_SEASON";", series_id);
    bg_sqlite_exec(s->db, sql, append_id_callback, a);  
    free(sql);

    append_id(a->arr, bg_sprintf("%s/all", a->parent_id));
    return 1;
    }
  else if(*id == '/')
    {
    id++;

    if(!strcmp(id, "all"))
      {
      sql = bg_sprintf("SELECT "META_DB_ID" FROM episodes WHERE "META_PARENT_ID" in (SELECT "META_DB_ID" FROM seasons "
                       "WHERE "META_PARENT_ID" = %"PRId64") ORDER BY "GAVL_META_SEARCH_TITLE" COLLATE strcoll;", series_id);
      
      bg_sqlite_exec(s->db, sql, append_id_callback, a);  
      free(sql);
      }
    else
      {
      int64_t season_id = strtoll(id, &rest, 10);
      sql = bg_sprintf("SELECT "META_DB_ID" FROM episodes WHERE "META_PARENT_ID" = %"PRId64" ORDER BY "GAVL_META_EPISODENUMBER";", season_id);
      bg_sqlite_exec(s->db, sql, append_id_callback, a);  
      free(sql);
      }
    return 1;
    }
  else
    return 0;

  return 0;
  
  }

static int browse_children_ids(bg_mdb_backend_t * b, const char * id,
                               gavl_array_t * ret)
  {
  sqlite_priv_t * s;
  gavl_value_t val;
  const char * id_orig = id;
  append_id_t a;
  char * sql;
  //  int result;
  int64_t genre_id;
  char * rest;
  
  s = b->priv;
  
  gavl_value_init(&val);
  
  if(s->songs_id && starts_with(&id, s->songs_id))
    {
    if(*id == '\0') // /songs
      {
      append_id(ret, bg_sprintf("%s/artist", s->songs_id));
      append_id(ret, bg_sprintf("%s/genre", s->songs_id));
      append_id(ret, bg_sprintf("%s/genre-artist", s->songs_id));
      append_id(ret, bg_sprintf("%s/genre-year", s->songs_id));
      append_id(ret, bg_sprintf("%s/year", s->songs_id));
      
      return 1;
      }
    else
      {
      if(*id != '/')
        return 0;
      id++;

      a.arr       = ret;
      a.parent_id = id_orig;
      
      if(starts_with(&id, "artist"))
        {
        if(*id == '\0')
          {
          /* Groups */
          int i, j;
          int num;
          gavl_array_t arr;
          gavl_array_init(&arr);
          
          bg_sqlite_exec(s->db, "SELECT NAME FROM song_artists", append_string_callback, &arr);
          
          for(i = 0; i < bg_mdb_num_groups; i++)
            {
            num = 0;
            
            for(j = 0; j < arr.num_entries; j++)
              {
              if(bg_mdb_test_group_condition(bg_mdb_groups[i].id, 
                                      gavl_value_get_string(&arr.entries[j])))
                num++;
              }

            if(num)
              {
              gavl_dictionary_t * dict, *m;
              
              dict = append_id(ret, bg_sprintf("%s/artist/%s", s->songs_id, bg_mdb_groups[i].id));

              gavl_track_set_num_children(dict, num, 0);
              
              m = gavl_track_get_metadata_nc(dict);
              gavl_dictionary_set_string(m, GAVL_META_LABEL, bg_mdb_groups[i].label);
              }
            
            // gavl_value_set_string_nocopy(&val, bg_sprintf("%s/genre-year", s->songs_id));
            // gavl_array_splice_val_nocopy(ret, -1, 0, &val);
            }
          gavl_array_free(&arr);
          return 1;
          }
        else if(*id == '/') // /songs/artist/a
          {
          const char * pos;
          id++;

          if(!(pos = strchr(id, '/')))  // /songs/artist/a
            {
            char * cond;

            if((cond = bg_sqlite_make_group_condition(id)))
              {
              sql = bg_sprintf("SELECT ID FROM song_artists WHERE NAME %s ORDER BY NAME;", cond);
              bg_sqlite_exec(s->db, sql, append_id_callback, &a);  
              free(sql);
              free(cond);
              }
            return 1;
            }
          else   // /songs/artist/a/123
            {
            int64_t artist_id;
            
            id = pos;
            id++;
            artist_id = strtoll(id, NULL, 10);

            sql = bg_sprintf("SELECT songs."META_DB_ID" FROM songs INNER JOIN "
                             "song_artists_arr "
                             "ON songs."META_DB_ID" = song_artists_arr.OBJ_ID "
                             "WHERE song_artists_arr.NAME_ID = %"PRId64" ORDER BY "
                             "songs."GAVL_META_SEARCH_TITLE" COLLATE strcoll;", artist_id);

            bg_sqlite_exec(s->db, sql, append_id_callback, &a);  
            free(sql);
            return 1;
            }
          }
        else
          {
          
          }
        
        }
      else if(starts_with(&id, "genre-artist"))
        {
        if(*id == '\0') // songs/genre-artist
          {
          bg_sqlite_exec(s->db,                                      
                         "SELECT ID FROM song_genres ORDER BY NAME", /* SQL to be evaluated */
                         append_id_callback, &a);
          return 1;
          }
        else if(*id == '/')
          {
          id++;
          genre_id = strtoll(id, &rest, 10);
          
          if(id == rest)
            return 0;
          id = rest;

          if(*id == '\0') // songs/genre-artist/1
            {
            sql = bg_sprintf("SELECT ID FROM song_artists WHERE ID in (SELECT DISTINCT song_artists_arr.NAME_ID "
                             "FROM song_artists_arr INNER JOIN song_genres_arr "
                             "ON song_artists_arr.OBJ_ID = song_genres_arr.OBJ_ID "
                             "WHERE song_genres_arr.NAME_ID = %"PRId64") ORDER BY NAME;", genre_id);
            bg_sqlite_exec(s->db, sql, append_id_callback, &a);  
            free(sql);
            return 1;
            }
          else if(*id == '/')  // songs/genre-artist/1/123
            {
            int64_t artist_id;
            id++;
            artist_id = strtoll(id, &rest, 10);
            
            id = rest;
            
            if(*id == '\0')
              {
              sql = bg_sprintf("SELECT song_artists_arr.OBJ_ID "
                               "FROM "
                               "song_artists_arr INNER JOIN song_genres_arr "
                               "ON song_artists_arr.OBJ_ID = song_genres_arr.OBJ_ID "
                               "INNER JOIN songs "
                               "ON songs."META_DB_ID" = song_genres_arr.OBJ_ID "
                               "WHERE song_genres_arr.NAME_ID = %"PRId64" AND "
                               "song_artists_arr.NAME_ID = %"PRId64" ORDER BY songs."GAVL_META_SEARCH_TITLE" COLLATE strcoll;", genre_id, artist_id);
              bg_sqlite_exec(s->db, sql, append_id_callback, &a);  
              free(sql);
              return 1;
              }
            else
              return 0;
            }
          else
            return 0;

          }
        else
          return 0;
        
        }
      else if(starts_with(&id, "genre-year"))
        {
        if(*id == '\0')
          {
          bg_sqlite_exec(s->db,                                      
                         "SELECT ID FROM song_genres ORDER BY NAME", /* SQL to be evaluated */
                         append_id_callback, &a);
          return 1;
          }
        else if(*id == '/')
          {
          id++;
          genre_id = strtoll(id, &rest, 10);
        
          if(id == rest)
            return 0;
          id = rest;

          if(*id == '\0') // /songs/genre-year/1
            {
            sql = bg_sprintf("SELECT DISTINCT substr(songs."GAVL_META_DATE", 1, 4) FROM "
                             "songs INNER JOIN song_genres_arr ON "
                             "song_genres_arr.OBJ_ID = songs.DBID WHERE "
                             "song_genres_arr.NAME_ID = %"PRId64" ORDER BY songs."GAVL_META_DATE";", genre_id);
            bg_sqlite_exec(s->db,                                    
                           sql,
                           append_id_callback, &a);
            free(sql);
            return 1;
            }
          else if(*id == '/') // /songs/genre-year/1/1960
            {
            int year;
            id++;

            year = strtol(id, &rest, 10);
        
            if(id == rest)
              return 0;
            id = rest;

            if(*id == '\0')
              {
              sql = bg_sprintf("SELECT songs."META_DB_ID" FROM "
                               "songs INNER JOIN song_genres_arr ON "
                               "song_genres_arr.OBJ_ID = songs.DBID WHERE "
                               "song_genres_arr.NAME_ID = %"PRId64" AND songs."GAVL_META_DATE" GLOB '%d*' "
                               "ORDER BY songs."GAVL_META_SEARCH_TITLE" COLLATE strcoll;", genre_id, year);
              bg_sqlite_exec(s->db,                                      
                             sql,
                             append_id_callback, &a);
              
              free(sql);
              return 1;
              }
            else 
              return 0;
            }
          else
            return 0;
          
          }
      
        }

      /* /songs/year */

      else if(starts_with(&id, "year"))
        {
        if(*id == '\0')
          {
          bg_sqlite_exec(s->db,                                      
                         "SELECT DISTINCT substr("GAVL_META_DATE", 1, 4) FROM "
                         "songs ORDER BY "GAVL_META_DATE";",
                         append_id_callback, &a);
          return 1;
          }
        else if(*id == '/') // /songs/year/1960
          {
          int year;
          id++;

          year = strtol(id, &rest, 10);
        
          if(id == rest)
            return 0;
          id = rest;

          if(*id == '\0')
            {
            sql = bg_sprintf("select "META_DB_ID" FROM "
                             "songs WHERE "
                             GAVL_META_DATE" GLOB '%d*' "
                             "ORDER BY "GAVL_META_SEARCH_TITLE" COLLATE strcoll;", year);
            bg_sqlite_exec(s->db,                                      
                           sql,
                           append_id_callback, &a);
              
            free(sql);
            return 1;
            }
          else 
            return 0;
          }
        else
          return 0;
          
        
        }

      /* /songs/genre */
         
      else if(starts_with(&id, "genre"))
        {
        if(*id == '\0')
          {
          bg_sqlite_exec(s->db,                                        
                         "SELECT ID FROM song_genres ORDER BY NAME", /* SQL to be evaluated */
                         append_id_callback, &a);
          return 1;
          }
        else if(*id == '/')
          {
          id++;
          genre_id = strtoll(id, &rest, 10);
        
          if(id == rest)
            return 0;
          id = rest;

          if(*id == '\0')
            {
            gavl_array_t arr;
            int i, j, num;
            gavl_array_init(&arr);
            
            sql = bg_sprintf("SELECT songs."GAVL_META_SEARCH_TITLE" "
                             "FROM song_genres_arr INNER JOIN songs "
                             "ON song_genres_arr.OBJ_ID = songs."META_DB_ID" "
                             "WHERE song_genres_arr.NAME_ID = %"PRId64";", genre_id);
            
            bg_sqlite_exec(s->db, sql, append_string_callback, &arr);
            
            free(sql);

            for(i = 0; i < bg_mdb_num_groups; i++)
              {
              num = 0;
            
              for(j = 0; j < arr.num_entries; j++)
                {
                if(bg_mdb_test_group_condition(bg_mdb_groups[i].id, 
                                        gavl_value_get_string(&arr.entries[j])))
                  num++;
                }

              if(num)
                {
                gavl_dictionary_t * dict, *m;
                
                dict = append_id(ret, bg_sprintf("%s/genre/%"PRId64"/%s", s->songs_id, genre_id, bg_mdb_groups[i].id));

                m = gavl_track_get_metadata_nc(dict);

                gavl_track_set_num_children(dict, 0, num);
                
                gavl_dictionary_set_string(m, GAVL_META_LABEL, bg_mdb_groups[i].label);
                }
            
              // gavl_value_set_string_nocopy(&val, bg_sprintf("%s/genre-year", s->songs_id));
              // gavl_array_splice_val_nocopy(ret, -1, 0, &val);

              }
            gavl_array_free(&arr);
            return 1;
            }
          else if(*id == '/')
            {
            char * pos;
            id++;

            if(!(pos = strchr(id, '/')))
              {
              char * cond = bg_sqlite_make_group_condition(id);

              if(cond)
                {
                sql = bg_sprintf("SELECT songs."META_DB_ID" "
                                 "FROM song_genres_arr INNER JOIN songs "
                                 "ON song_genres_arr.OBJ_ID = songs."META_DB_ID" "
                                 "WHERE song_genres_arr.NAME_ID = %"PRId64" AND "
                                 "songs."GAVL_META_SEARCH_TITLE" %s ORDER BY songs."GAVL_META_SEARCH_TITLE" COLLATE strcoll;", genre_id, cond);

                bg_sqlite_exec(s->db, sql, append_id_callback, &a);  
                free(sql);
                free(cond);
                return 1;
                }
              }
            else
              return 0;
            }
          else
            return 0;
          }
        else
          return 0;
        }
      }
    }
  else if(s->albums_id && starts_with(&id, s->albums_id))
    {
    if(*id == '\0') // /albums
      {
      append_id(ret, bg_sprintf("%s/artist", s->albums_id));
      append_id(ret, bg_sprintf("%s/genre-artist", s->albums_id));
      append_id(ret, bg_sprintf("%s/genre-year", s->albums_id));
      append_id(ret, bg_sprintf("%s/year", s->albums_id));
      return 1;
      }
    else
      {
      if(*id != '/')
        return 0;

      id++;

      a.arr       = ret;
      a.parent_id = id_orig;

      if(starts_with(&id, "artist"))
        {
        if(*id == '\0')
          {
          /* Groups */
          int i, j;
          int num;
          gavl_array_t arr;
          gavl_array_init(&arr);
          
          bg_sqlite_exec(s->db, "SELECT NAME FROM album_artists", append_string_callback, &arr);
          
          for(i = 0; i < bg_mdb_num_groups; i++)
            {
            num = 0;
            
            for(j = 0; j < arr.num_entries; j++)
              {
              if(bg_mdb_test_group_condition(bg_mdb_groups[i].id, 
                                      gavl_value_get_string(&arr.entries[j])))
                num++;
              }

            if(num)
              {
              gavl_dictionary_t * dict, *m;
              
              dict = append_id(ret, bg_sprintf("%s/artist/%s", s->albums_id, bg_mdb_groups[i].id));

              gavl_track_set_num_children(dict, num, 0);
                            
              m = gavl_track_get_metadata_nc(dict);

              gavl_track_set_num_children(dict, num, 0);
              gavl_dictionary_set_string(m, GAVL_META_LABEL, bg_mdb_groups[i].label);
              }
            }
          gavl_array_free(&arr);
          return 1;
          }
        else
          {
          const char * pos;
          id++;

          if(!(pos = strchr(id, '/')))  // /albums/artist/a
            {
            char * cond;

            if((cond = bg_sqlite_make_group_condition(id)))
              {
              sql = bg_sprintf("SELECT ID FROM album_artists WHERE NAME %s ORDER BY NAME;", cond);
              bg_sqlite_exec(s->db, sql, append_id_callback, &a);  
              free(sql);
              free(cond);
              }
            return 1;
            }
          else   // /albums/artist/a/123
            {
            int64_t artist_id;
            
            id = pos;
            id++;
            artist_id = strtoll(id, &rest, 10);

            id = rest;
            
            if(*id == '\0')
              {
              sql = bg_sprintf("SELECT albums."META_DB_ID" FROM albums INNER JOIN album_artists_arr "
                               "ON albums."META_DB_ID" = album_artists_arr.OBJ_ID "
                               "WHERE album_artists_arr.NAME_ID = %"PRId64" "
                               "ORDER BY albums."GAVL_META_DATE", albums."GAVL_META_SEARCH_TITLE" COLLATE strcoll;", artist_id);
              bg_sqlite_exec(s->db, sql, append_id_callback, &a);  
              free(sql);
              return 1;
              }
            else if(*id == '/')
              {
              int64_t album_id;
            
              id++;
              album_id = strtoll(id, &rest, 10);

              sql = bg_sprintf("SELECT "META_DB_ID" FROM songs "
                               "WHERE "META_PARENT_ID" = %"PRId64" "
                               "ORDER BY "GAVL_META_TRACKNUMBER";", album_id);
              bg_sqlite_exec(s->db, sql, append_id_callback, &a);  
              free(sql);
              return 1;
              
              }
            else
              return 0;
            }
          
          
          }
      
        }
      else if(starts_with(&id, "genre-artist"))
        {
        if(*id == '\0')
          {
          bg_sqlite_exec(s->db,                                        
                         "SELECT ID FROM album_genres ORDER BY NAME", /* SQL to be evaluated */
                         append_id_callback, &a);
          return 1;
          }
        else if(*id == '/') // /genre/artist/1
          {
          id++;
          genre_id = strtoll(id, &rest, 10);
        
          if(id == rest)
            return 0;
          id = rest;

          if(*id == '\0')
            {
            sql = bg_sprintf("SELECT ID FROM album_artists WHERE ID in (SELECT DISTINCT album_artists_arr.NAME_ID "
                             "FROM album_artists_arr INNER JOIN album_genres_arr "
                             "ON album_artists_arr.OBJ_ID = album_genres_arr.OBJ_ID "
                             "WHERE album_genres_arr.NAME_ID = %"PRId64") ORDER BY NAME;", genre_id);
            bg_sqlite_exec(s->db, sql, append_id_callback, &a);  
            free(sql);
            return 1;
            }
          else if(*id == '/')  // /genre/artist/1/7
            {

            int64_t artist_id;
            id++;
            artist_id = strtoll(id, &rest, 10);
            
            id = rest;
            
            if(*id == '\0')
              {
              sql = bg_sprintf("SELECT album_artists_arr.OBJ_ID "
                               "FROM "
                               "album_artists_arr INNER JOIN album_genres_arr "
                               "ON album_artists_arr.OBJ_ID = album_genres_arr.OBJ_ID "
                               "INNER JOIN albums "
                               "ON albums."META_DB_ID" = album_genres_arr.OBJ_ID "
                               "WHERE album_genres_arr.NAME_ID = %"PRId64" AND "
                               "album_artists_arr.NAME_ID = %"PRId64" ORDER BY albums."GAVL_META_DATE", albums."GAVL_META_SEARCH_TITLE" COLLATE strcoll;",
                               genre_id, artist_id);
              bg_sqlite_exec(s->db, sql, append_id_callback, &a);  
              free(sql);
              return 1;
              }
            else if(*id == '/') // /genre/artist/1/7/110

              {
              int64_t album_id;
              id++;
              album_id = strtoll(id, &rest, 10);

              id = rest;

              if(*id == '\0')
                {
                sql = bg_sprintf("SELECT "META_DB_ID" FROM songs "
                                 "WHERE "META_PARENT_ID" = %"PRId64" "
                                 "ORDER BY "GAVL_META_TRACKNUMBER";", album_id);
                bg_sqlite_exec(s->db, sql, append_id_callback, &a);  
                free(sql);
                return 1;
                }
              else
                return 0;
              }
            else
              return 0;
            }
          else
            return 0;
          
          }
        else
          return 0;
        }
      else if(starts_with(&id, "genre-year"))
        {
        if(*id == '\0')
          {
          bg_sqlite_exec(s->db,                                       
                         "SELECT ID FROM album_genres ORDER BY NAME", /* SQL to be evaluated */
                         append_id_callback, &a);
          return 1;
          }
        else if(*id == '/')
          {
          id++;
          
          genre_id = strtoll(id, &rest, 10);
        
          if(id == rest)
            return 0;
          id = rest;
          
          if(*id == '\0')
            {

            sql = bg_sprintf("SELECT DISTINCT substr(albums."GAVL_META_DATE", 1, 4) FROM "
                             "albums INNER JOIN album_genres_arr ON "
                             "album_genres_arr.OBJ_ID = albums.DBID WHERE "
                             "album_genres_arr.NAME_ID = %"PRId64" ORDER BY albums."GAVL_META_DATE";", genre_id);
            bg_sqlite_exec(s->db,                                      
                           sql,
                           append_id_callback, &a);
            free(sql);
            return 1;

            
            }
          else if(*id == '/')
            {

            int year;
            id++;

            year = strtol(id, &rest, 10);
        
            if(id == rest)
              return 0;
            id = rest;

            if(*id == '\0')
              {
              sql = bg_sprintf("SELECT albums."META_DB_ID" FROM "
                               "albums INNER JOIN album_genres_arr ON "
                               "album_genres_arr.OBJ_ID = albums.DBID WHERE "
                               "album_genres_arr.NAME_ID = %"PRId64" AND albums."GAVL_META_DATE" GLOB '%d*' "
                               "ORDER BY albums."GAVL_META_SEARCH_TITLE" COLLATE strcoll;", genre_id, year);
              bg_sqlite_exec(s->db,                                      
                             sql,
                             append_id_callback, &a);
              
              free(sql);
              return 1;
              }
            else if(*id == '/')
              {
              int64_t album_id;
            
              id++;
              album_id = strtoll(id, &rest, 10);

              sql = bg_sprintf("SELECT "META_DB_ID" FROM songs "
                               "WHERE "META_PARENT_ID" = %"PRId64" "
                               "ORDER BY "GAVL_META_TRACKNUMBER";", album_id);
              bg_sqlite_exec(s->db, sql, append_id_callback, &a);  
              free(sql);
              return 1;
              

              }
            
            }
          else
            return 0;
          }
        else
          return 0;
        
        }

      /* /albums/year */

      else if(starts_with(&id, "year"))
        {
        if(*id == '\0')
          {
          bg_sqlite_exec(s->db,                                      
                         "SELECT DISTINCT substr("GAVL_META_DATE", 1, 4) FROM "
                         "albums ORDER BY "GAVL_META_DATE";",
                         append_id_callback, &a);
          return 1;
          }
        else if(*id == '/') // /songs/year/1960
          {
          int year;
          id++;

          year = strtol(id, &rest, 10);
        
          if(id == rest)
            return 0;
          id = rest;

          if(*id == '\0')
            {
            sql = bg_sprintf("select "META_DB_ID" FROM "
                             "albums WHERE "
                             GAVL_META_DATE" GLOB '%d*' "
                             "ORDER BY "GAVL_META_SEARCH_TITLE" COLLATE strcoll;", year);
            bg_sqlite_exec(s->db,                                      
                           sql,
                           append_id_callback, &a);
              
            free(sql);
            return 1;
            }
          else if(*id == '/')
            {
            int64_t album_id;
            
            id++;
            album_id = strtoll(id, &rest, 10);

            sql = bg_sprintf("SELECT "META_DB_ID" FROM songs "
                             "WHERE "META_PARENT_ID" = %"PRId64" "
                             "ORDER BY "GAVL_META_TRACKNUMBER";", album_id);
            bg_sqlite_exec(s->db, sql, append_id_callback, &a);  
            free(sql);
            return 1;
            }
          else 
            return 0;
          }
        else
          return 0;
          
        
        }



      
      }

    }
  else if(s->movies_id && starts_with(&id, s->movies_id))
    {
    if(*id == '\0') // /movies
      {
      append_id(ret, bg_sprintf("%s/all", s->movies_id));
      append_id(ret, bg_sprintf("%s/actor", s->movies_id));
      append_id(ret, bg_sprintf("%s/director", s->movies_id));
      append_id(ret, bg_sprintf("%s/genre", s->movies_id));
      append_id(ret, bg_sprintf("%s/year", s->movies_id));
      append_id(ret, bg_sprintf("%s/country", s->movies_id));
      append_id(ret, bg_sprintf("%s/language", s->movies_id));
      return 1;
      }
    else
      {
      if(*id != '/')
        return 0;
      id++;
      
      a.arr       = ret;
      a.parent_id = id_orig;

      if(starts_with(&id, "actor"))
        {
        if(*id == '\0') // /movies/actor
          {
          /* Groups */
          int i, j;
          int num;
          gavl_array_t arr;
          gavl_array_init(&arr);
          
          bg_sqlite_exec(s->db, "SELECT NAME FROM movie_actors", append_string_callback, &arr);
          
          for(i = 0; i < bg_mdb_num_groups; i++)
            {
            num = 0;
            
            for(j = 0; j < arr.num_entries; j++)
              {
              if(bg_mdb_test_group_condition(bg_mdb_groups[i].id, 
                                      gavl_value_get_string(&arr.entries[j])))
                num++;
              }

            if(num)
              {
              gavl_dictionary_t * dict, *m;
              
              dict = append_id(ret, bg_sprintf("%s/actor/%s", s->movies_id, bg_mdb_groups[i].id));

              m = gavl_track_get_metadata_nc(dict);

              gavl_track_set_num_children(dict, num, 0);
              gavl_dictionary_set_string(m, GAVL_META_LABEL, bg_mdb_groups[i].label);
              }
            }
          gavl_array_free(&arr);
          return 1;
          }
        else if(*id == '/')
          {
          const char * pos;
          id++;

          if(!(pos = strchr(id, '/')))  // /movies/actor/a
            {
            char * cond;

            if((cond = bg_sqlite_make_group_condition(id)))
              {
              sql = bg_sprintf("SELECT ID FROM movie_actors WHERE NAME %s ORDER BY NAME;", cond);
              bg_sqlite_exec(s->db, sql, append_id_callback, &a);  
              free(sql);
              free(cond);
              }
            return 1;
            }
          else   // /movie/actor/a/123
            {
            int64_t actor_id;
            
            id = pos;
            id++;
            actor_id = strtoll(id, NULL, 10);

            sql = bg_sprintf("SELECT movies."META_DB_ID" FROM movies INNER JOIN "
                             "movie_actors_arr "
                             "ON movies."META_DB_ID" = movie_actors_arr.OBJ_ID "
                             "WHERE movie_actors_arr.NAME_ID = %"PRId64" ORDER BY "
                             "movies."GAVL_META_DATE";", actor_id);

            bg_sqlite_exec(s->db, sql, append_id_callback, &a);  
            free(sql);
            return 1;
            }
          }
        else
          return 0;
        
        }
      else if(starts_with(&id, "all"))
        {
        if(*id == '\0') // /movies/all
          {
          // fprintf(stderr, "SELECT "META_DB_ID" FROM movies ORDER BY "GAVL_META_SEARCH_TITLE" COLLATE strcoll;\n"); /* SQL to be evaluated */
          
          bg_sqlite_exec(s->db,                                        
                         "SELECT "META_DB_ID" FROM movies ORDER BY "GAVL_META_SEARCH_TITLE" COLLATE strcoll;", /* SQL to be evaluated */
                         append_id_callback, &a);
          return 1;
          }
        else if(*id == '/')
          {
          
          }
        else
          return 0;
        
        }
      else if(starts_with(&id, "country"))
        {
        if(*id == '\0') // /movies/country
          {
          bg_sqlite_exec(s->db,                                        
                         "SELECT ID FROM movie_countries ORDER BY NAME;", /* SQL to be evaluated */
                         append_id_callback, &a);
          return 1;
          }
        else if(*id == '/')
          {
          int64_t country_id;
            
          id++;
          country_id = strtoll(id, NULL, 10);
          
          sql = bg_sprintf("SELECT movies."META_DB_ID" FROM movies INNER JOIN "
                           "movie_countries_arr "
                           "ON movies."META_DB_ID" = movie_countries_arr.OBJ_ID "
                           "WHERE movie_countries_arr.NAME_ID = %"PRId64" ORDER BY "
                           "movies."GAVL_META_SEARCH_TITLE";", country_id);
          
          bg_sqlite_exec(s->db, sql, append_id_callback, &a);  
          free(sql);
          return 1;
          
          }
        else
          return 0;
      
        }
      else if(starts_with(&id, "language"))
        {
        if(*id == '\0') // /movies/language
          {
          bg_sqlite_exec(s->db,                                        
                         "SELECT ID FROM movie_audio_languages ORDER BY NAME", /* SQL to be evaluated */
                         append_id_callback, &a);
          return 1;
          }
        else if(*id == '/')
          {
          int64_t language_id;
            
          id++;
          language_id = strtoll(id, NULL, 10);
          
          sql = bg_sprintf("SELECT movies."META_DB_ID" FROM movies INNER JOIN "
                           "movie_audio_languages_arr "
                           "ON movies."META_DB_ID" = movie_audio_languages_arr.OBJ_ID "
                           "WHERE movie_audio_languages_arr.NAME_ID = %"PRId64" ORDER BY "
                           "movies."GAVL_META_SEARCH_TITLE" COLLATE strcoll;", language_id);
          
          bg_sqlite_exec(s->db, sql, append_id_callback, &a);  
          free(sql);
          return 1;
          
          }
        else
          return 0;
      
        }
      else if(starts_with(&id, "director"))
        {
        if(*id == '\0')
          {
          /* Groups */
          int i, j;
          int num;
          gavl_array_t arr;
          gavl_array_init(&arr);
          
          bg_sqlite_exec(s->db, "SELECT NAME FROM movie_directors", append_string_callback, &arr);
          
          for(i = 0; i < bg_mdb_num_groups; i++)
            {
            num = 0;
            
            for(j = 0; j < arr.num_entries; j++)
              {
              if(bg_mdb_test_group_condition(bg_mdb_groups[i].id, 
                                      gavl_value_get_string(&arr.entries[j])))
                num++;
              }

            if(num)
              {
              gavl_dictionary_t * dict, *m;
              
              dict = append_id(ret, bg_sprintf("%s/director/%s", s->movies_id, bg_mdb_groups[i].id));

              m = gavl_track_get_metadata_nc(dict);

              gavl_track_set_num_children(dict, num, 0);
              gavl_dictionary_set_string(m, GAVL_META_LABEL, bg_mdb_groups[i].label);
              }
            }
          gavl_array_free(&arr);
          return 1;
          }
        else if(*id == '/')
          {
          const char * pos;
          id++;

          if(!(pos = strchr(id, '/')))  // /movies/director/a
            {
            char * cond;

            if((cond = bg_sqlite_make_group_condition(id)))
              {
              sql = bg_sprintf("SELECT ID FROM movie_directors WHERE NAME %s ORDER BY NAME;", cond);
              bg_sqlite_exec(s->db, sql, append_id_callback, &a);  
              free(sql);
              free(cond);
              }
            return 1;
            }
          else   // /movie/director/a/123
            {
            int64_t director_id;
            
            id = pos;
            id++;
            director_id = strtoll(id, NULL, 10);

            sql = bg_sprintf("SELECT movies."META_DB_ID" FROM movies INNER JOIN "
                             "movie_directors_arr "
                             "ON movies."META_DB_ID" = movie_directors_arr.OBJ_ID "
                             "WHERE movie_directors_arr.NAME_ID = %"PRId64" ORDER BY "
                             "movies."GAVL_META_DATE";", director_id);

            bg_sqlite_exec(s->db, sql, append_id_callback, &a);  
            free(sql);
            return 1;
            }
          }
        else
          return 0;

      
        }
      else if(starts_with(&id, "genre"))
        {
        if(*id == '\0')
          {
          bg_sqlite_exec(s->db,                                        
                         "SELECT ID FROM movie_genres ORDER BY NAME", /* SQL to be evaluated */
                         append_id_callback, &a);
          return 1;
          }
        else if(*id == '/')
          {
          int64_t genre_id;
            
          id++;
          genre_id = strtoll(id, NULL, 10);
          
          sql = bg_sprintf("SELECT movies."META_DB_ID" FROM movies INNER JOIN "
                           "movie_genres_arr "
                           "ON movies."META_DB_ID" = movie_genres_arr.OBJ_ID "
                           "WHERE movie_genres_arr.NAME_ID = %"PRId64" ORDER BY "
                           "movies."GAVL_META_SEARCH_TITLE" COLLATE strcoll;", genre_id);
          
          bg_sqlite_exec(s->db, sql, append_id_callback, &a);  
          free(sql);
          return 1;
          
          }
        else
          return 0;

      
        }
      else if(starts_with(&id, "year"))
        {
        if(*id == '\0')
          {
          bg_sqlite_exec(s->db,                                        
                         "SELECT DISTINCT substr("GAVL_META_DATE", 1, 4) FROM movies ORDER BY "GAVL_META_DATE";",
                         append_id_callback, &a);
          return 1;
          }
        else if(*id == '/')
          {
          int year;
            
          id++;
          year = strtol(id, &rest, 10);

          id = rest;
          
          if(*id == '\0')
            {
            sql = bg_sprintf("SELECT "META_DB_ID" FROM movies WHERE substr("GAVL_META_DATE", 1, 4) = '%d' ORDER BY "GAVL_META_SEARCH_TITLE" COLLATE strcoll;", year);
            bg_sqlite_exec(s->db,                                        
                           sql, append_id_callback, &a);
            free(sql);
            return 1;
            }
          else
            return 0;
          }
        else
          return 0;
        }
      }

    }
  /* series */
  else if(s->series_id && starts_with(&id, s->series_id))
    {
    if(*id == '\0') // /series
      {
      append_id(ret, bg_sprintf("%s/all", s->series_id));
      append_id(ret, bg_sprintf("%s/genre", s->series_id));
      return 1;
      }
    else
      {
      if(*id != '/')
        return 0;
      id++;

      a.arr       = ret;
      a.parent_id = id_orig;

      if(starts_with(&id, "all"))
        {
        if(*id == '\0')
          {
          bg_sqlite_exec(s->db,                                         
                         "SELECT "META_DB_ID" FROM shows ORDER BY "GAVL_META_SEARCH_TITLE, /* SQL to be evaluated */
                         append_id_callback, &a);
          return 1;
          }
        else
          {
          return browse_children_series(b, id, &a);
          }
        }
      else if(starts_with(&id, "genre"))
        {
        if(*id == '\0')
          {
          bg_sqlite_exec(s->db,                                        
                         "SELECT ID FROM show_genres ORDER BY NAME", /* SQL to be evaluated */
                         append_id_callback, &a);
          return 1;
          }
        else if(*id == '/')
          {
          id++;
          genre_id = strtoll(id, &rest, 10);
        
          if(id == rest)
            return 0;
          id = rest;

          if(*id == '\0')
            {
            sql = bg_sprintf("SELECT shows."META_DB_ID" FROM shows INNER JOIN "
                             "show_genres_arr "
                             "ON shows."META_DB_ID" = show_genres_arr.OBJ_ID "
                             "WHERE show_genres_arr.NAME_ID = %"PRId64" ORDER BY "
                             "shows."GAVL_META_SEARCH_TITLE" COLLATE strcoll;", genre_id);
            bg_sqlite_exec(s->db, sql, append_id_callback, &a);  
            free(sql);
            return 1;
            }
          else
            return browse_children_series(b, id, &a);
          }
      
        }
      }
    }

  return 0;
  }

static int browse_children(bg_mdb_backend_t * b, const gavl_msg_t * msg)
  {
  int i;
  gavl_array_t children_ids;
  gavl_array_t children_arr;
  gavl_value_t val;
  gavl_time_t time_msg = 0;
  gavl_time_t current_time = 0;
  const char * id;
  //  const char * child_id;
  gavl_dictionary_t * dict;
  gavl_dictionary_t * m;
  int idx;
  int ret = 0;
  int start, num;
  int do_flush = 0;

  int total = 0;
  int one_answer;
  
  gavl_time_t start_time = gavl_timer_get(b->db->timer);
  
  bg_mdb_get_browse_children_request(msg, &id, &start, &num, &one_answer);
  
  gavl_array_init(&children_ids);
  gavl_array_init(&children_arr);

  if(!browse_children_ids(b, id, &children_ids))
    goto fail;

  if(!one_answer)
    do_flush = 1;
  
  if(!bg_mdb_adjust_num(start, &num, children_ids.num_entries))
    goto fail;

  total = children_ids.num_entries;
  
  idx = start;
  
  for(i = 0; i < num; i++)
    {
    gavl_value_init(&val);

    dict = gavl_value_set_dictionary(&val);

    gavl_dictionary_move(dict, gavl_value_get_dictionary_nc(&children_ids.entries[i + start])); 
    
    if(!browse_object(b, NULL, dict))
      goto fail;

    /* Next next and previous sibling */
    m = gavl_track_get_metadata_nc(dict);
    
    if(i + start > 0)
      {
      const gavl_dictionary_t * prev_m;
      
      if((prev_m = gavl_value_get_dictionary(&children_ids.entries[i+start-1])) &&
         (prev_m = gavl_track_get_metadata(prev_m)))
        gavl_dictionary_set_string(m, GAVL_META_PREVIOUS_ID, gavl_dictionary_get_string(prev_m, GAVL_META_ID));
      }
    if(i + start < children_ids.num_entries-1)
      {
      const gavl_dictionary_t * next_m;
      
      if((next_m = gavl_value_get_dictionary(&children_ids.entries[i+1])) &&
         (next_m = gavl_track_get_metadata(next_m)))
        gavl_dictionary_set_string(m, GAVL_META_NEXT_ID, gavl_dictionary_get_string(next_m, GAVL_META_ID));
      }

    
    gavl_array_splice_val_nocopy(&children_arr, -1, 0, &val);

    if(do_flush)
      {
      current_time = gavl_timer_get(b->db->timer) - start_time;
    
      if(current_time - time_msg > GAVL_TIME_SCALE)
        {
        int last = 0;
        /* Flush objects */
        gavl_msg_t * res = bg_msg_sink_get(b->ctrl.evt_sink);

        if(idx + children_arr.num_entries == children_ids.num_entries)
          last = 1;
        
        bg_mdb_set_browse_children_response(res, &children_arr, msg, &idx, last, total);
        bg_msg_sink_put(b->ctrl.evt_sink);
        time_msg = current_time;
        gavl_array_reset(&children_arr);
        }
      }
    
    }
  
  
  /* Flush remaining objects */
  if(children_arr.num_entries > 0)
    {
    gavl_msg_t * res = bg_msg_sink_get(b->ctrl.evt_sink);
    bg_mdb_set_browse_children_response(res, &children_arr, msg, &idx, 1, total);
    bg_msg_sink_put(b->ctrl.evt_sink);
    }
  
  ret = 1;
  fail:
  
  gavl_array_free(&children_ids);
  gavl_array_free(&children_arr);
  
  return ret;
  }

static int make_thumbnail_callback(void * data, int argc, char **argv, char **azColName)
  {
  bg_mdb_t * mdb = data;
  bg_mdb_make_thumbnails(mdb, argv[0]);
  return 0;
  }

static void make_thumbnails(bg_mdb_backend_t * be)
  {
  char * sql;
  sqlite_priv_t * p = be->priv;

  sql = bg_sprintf("SELECT "GAVL_META_URI" FROM images WHERE "META_IMAGE_TYPE" = %d;", IMAGE_TYPE_COVER);
  bg_sqlite_exec(p->db, sql, make_thumbnail_callback, be->db);
  free(sql);

  sql = bg_sprintf("SELECT "GAVL_META_URI" FROM images WHERE "META_IMAGE_TYPE" = %d;", IMAGE_TYPE_POSTER);
  bg_sqlite_exec(p->db, sql, make_thumbnail_callback, be->db);
  free(sql);
  }

static int handle_msg_sqlite(void * priv, gavl_msg_t * msg)
  {
  sqlite_priv_t * s;
  
  bg_mdb_backend_t * be = priv;
  s = be->priv;

  switch(msg->NS)
    {
    case BG_MSG_NS_DB:
      {
      switch(msg->ID)
        {
        case BG_FUNC_DB_BROWSE_OBJECT:
          {
          gavl_dictionary_t dict;
          gavl_msg_t * res;
          const char * id;
          id = gavl_dictionary_get_string(&msg->header, GAVL_MSG_CONTEXT_ID);

          gavl_dictionary_init(&dict);
          gavl_dictionary_get_dictionary_create(&dict, GAVL_META_METADATA);
          
          if(!browse_object(be, id, &dict))
            {
            gavl_dictionary_free(&dict);
            return 1;
            }
          
          res = bg_msg_sink_get(be->ctrl.evt_sink);
          bg_mdb_set_browse_obj_response(res, &dict, msg, -1, -1);
          bg_msg_sink_put(be->ctrl.evt_sink);
          gavl_dictionary_free(&dict);
          }
          break;
        case BG_FUNC_DB_BROWSE_CHILDREN:
          browse_children(be, msg);
          break;
        case BG_FUNC_DB_RESCAN:
          {
          int i;
          gavl_array_t sql_dirs;
          gavl_msg_t * res;
          
          gavl_array_init(&sql_dirs);
          bg_sqlite_get_string_array(s->db, "scandirs", "PATH", &sql_dirs);
          
          /* Rescan */
          lock_root_containers(be, 1);

          for(i = 0; i < sql_dirs.num_entries; i++)
            add_directory(be, gavl_string_array_get(&sql_dirs, i));

          /* Update thumbnails */
          make_thumbnails(be);
          
          lock_root_containers(be, 0);
          
          gavl_array_free(&sql_dirs);

          /* Send done event */
          
          res = bg_msg_sink_get(be->ctrl.evt_sink);
          gavl_msg_set_id_ns(res, BG_RESP_DB_RESCAN, BG_MSG_NS_DB);
          gavl_msg_set_resp_for_req(res, msg);
          bg_msg_sink_put(be->ctrl.evt_sink);
          }
          break;
        /* SQL Specific */
        case BG_FUNC_DB_ADD_SQL_DIR:
          {
          gavl_value_t sql_dirs_val;
          gavl_array_t * sql_dirs;
          gavl_msg_t * resp;
          const char * dir = gavl_msg_get_arg_string_c(msg, 0);
          gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Adding SQL dir %s", dir);
          
          gavl_value_init(&sql_dirs_val);
          sql_dirs = gavl_value_set_array(&sql_dirs_val);
          bg_sqlite_get_string_array(s->db, "scandirs", "PATH", sql_dirs);
          
          if(gavl_string_array_indexof(sql_dirs, dir) < 0)
            {
            lock_root_containers(be, 1);
            add_directory(be, dir);
            /* Update thumbnails */
            make_thumbnails(be);
            lock_root_containers(be, 0);
            update_root_containers(be);

            /* Also update config registry */
            gavl_string_array_add(sql_dirs, dir);

            resp = bg_msg_sink_get(be->ctrl.evt_sink);
            bg_msg_set_parameter_ctx(resp, BG_MSG_PARAMETER_CHANGED_CTX, MDB_BACKEND_SQLITE, "dirs", &sql_dirs_val);
            bg_msg_sink_put(be->ctrl.evt_sink);
            
            }
          else
            gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Directory %s already added", dir);

          gavl_value_free(&sql_dirs_val);
          
          /* */
          resp = bg_msg_sink_get(be->ctrl.evt_sink);
          gavl_msg_set_id_ns(resp, BG_RESP_ADD_SQL_DIR, BG_MSG_NS_DB);
          gavl_msg_set_resp_for_req(resp, msg);
          bg_msg_sink_put(be->ctrl.evt_sink);
          gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Added SQL dir");
          }
          break;
        case BG_FUNC_DB_DEL_SQL_DIR:
          {
          int idx;
          gavl_value_t sql_dirs_val;
          gavl_array_t * sql_dirs;
          gavl_msg_t * resp;
          
          const char * dir = gavl_msg_get_arg_string_c(msg, 0);
          gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Deleting SQL dir %s", dir);
          
          gavl_value_init(&sql_dirs_val);
          sql_dirs = gavl_value_set_array(&sql_dirs_val);
          bg_sqlite_get_string_array(s->db, "scandirs", "PATH", sql_dirs);
          
          if((idx = gavl_string_array_indexof(sql_dirs, dir)) >= 0)
            {
            lock_root_containers(be, 1);
            delete_directory(be, dir);
            lock_root_containers(be, 0);
            update_root_containers(be);
            
            /* Also update config registry */
            gavl_array_splice_val(sql_dirs, idx, 1, NULL);
            resp = bg_msg_sink_get(be->ctrl.evt_sink);
            bg_msg_set_parameter_ctx(resp, BG_MSG_PARAMETER_CHANGED_CTX,
                                     MDB_BACKEND_SQLITE, "dirs", &sql_dirs_val);
            bg_msg_sink_put(be->ctrl.evt_sink);
            
            }
          else
            gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Directory %s not there", dir);
          
          gavl_value_free(&sql_dirs_val);

          /* */
          resp = bg_msg_sink_get(be->ctrl.evt_sink);
          gavl_msg_set_id_ns(resp, BG_RESP_DEL_SQL_DIR, BG_MSG_NS_DB);
          gavl_msg_set_resp_for_req(resp, msg);
          bg_msg_sink_put(be->ctrl.evt_sink);
          gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Deleted SQL dir");
          }
          break;
        }
      }
    case GAVL_MSG_NS_GENERIC:
      switch(msg->ID)
        {
        case GAVL_CMD_QUIT:
          return 0;
          break;
        }
      break;
    case BG_MSG_NS_PARAMETER:
      switch(msg->ID)
        {
        case BG_MSG_SET_PARAMETER:
          {
          const char * name = NULL;
          gavl_value_t val;
          
          gavl_value_init(&val);

          bg_msg_get_parameter(msg, &name, &val);

          if(!name)
            {
            s->have_params = 1;
            return 1;
            }

          if(!strcmp(name, "dirs"))
            {
            int i;
            const gavl_array_t * dirs;
            gavl_array_t sql_dirs;
            const char * dir;
            int locked = 0;
            int changed = 0;
            
            gavl_array_init(&sql_dirs);
            dirs = gavl_value_get_array(&val);
            bg_sqlite_get_string_array(s->db, "scandirs", "PATH", &sql_dirs);
            
            /* Check which directories to add */
            for(i = 0; i < dirs->num_entries; i++)
              {
              dir = gavl_string_array_get(dirs, i);

              if(gavl_string_array_indexof(&sql_dirs, dir) < 0)
                {
                /* Add */
                if(!locked)
                  {
                  lock_root_containers(be, 1);
                  locked = 1;
                  }
                add_directory(be, dir);
                changed = 1;
                }
              }
            
            /* Check which directories to delete */
            for(i = 0; i < sql_dirs.num_entries; i++)
              {
              dir = gavl_string_array_get(&sql_dirs, i);

              if(gavl_string_array_indexof(dirs, dir) < 0)
                {
                /* Add */
                if(!locked)
                  {
                  lock_root_containers(be, 1);
                  locked = 1;
                  }
                delete_directory(be, dir);
                changed = 1;
                }
              }
            
            if(locked)
              {
              lock_root_containers(be, 0);
              locked = 1;
              }
            
            gavl_array_free(&sql_dirs);
            
            if(changed || !s->have_params)
              update_root_containers(be);

            if(!s->have_params)
              {
              int i;
              /* Export directories */
              for(i = 0; i < dirs->num_entries; i++)
                bg_mdb_export_media_directory(be->ctrl.evt_sink, gavl_string_array_get(dirs, i));
              }
            }
          
          /* Pass to core to store it in the config registry */
          if(s->have_params)
            {
            gavl_msg_t * resp = bg_msg_sink_get(be->ctrl.evt_sink);
            bg_msg_set_parameter_ctx(resp, BG_MSG_PARAMETER_CHANGED_CTX, MDB_BACKEND_SQLITE, name, &val);
            bg_msg_sink_put(be->ctrl.evt_sink);
            }
          
          gavl_value_free(&val);
          }
        }
      break;
    }
  return 1;
  }

static const bg_parameter_info_t parameters[] =
  {
    {
      .name = "dirs",
      .long_name = TRS("Directories"),
      .type = BG_PARAMETER_DIRLIST,
    },
    { /* End */ },
  };

static void create_root_containers(bg_mdb_backend_t * b)
  {
  sqlite_priv_t * s = b->priv;

  /* Songs */
  bg_mdb_init_root_container(&s->songs, GAVL_META_CLASS_ROOT_SONGS);
  bg_mdb_container_set_backend(&s->songs, MDB_BACKEND_SQLITE);
  // Artist
  // Genre
  // Genre-Artist
  // Genre-Year
  // Year
  gavl_track_set_num_children(&s->songs, 5, 0);

  //  fprintf(stderr, "Created songs:\n");
  //  gavl_dictionary_dump(&s->songs, 2);
  
  
  /* Movies */

  bg_mdb_init_root_container(&s->movies, GAVL_META_CLASS_ROOT_MOVIES);
  bg_mdb_container_set_backend(&s->movies, MDB_BACKEND_SQLITE);

  // Actor
  // All
  // Country
  // Language
  // Director
  // Genre
  // Year
  gavl_track_set_num_children(&s->movies, 7, 0);

  //  fprintf(stderr, "Created movies:\n");
  //  gavl_dictionary_dump(&s->movies, 2);

  
  /* Albums */
  bg_mdb_init_root_container(&s->albums, GAVL_META_CLASS_ROOT_MUSICALBUMS);
  bg_mdb_container_set_backend(&s->albums, MDB_BACKEND_SQLITE);

  // Artist
  // Genre-Artist
  // Genre-Year
  // Year
  gavl_track_set_num_children(&s->albums, 4, 0);

  //  fprintf(stderr, "Created albums:\n");
  //  gavl_dictionary_dump(&s->albums, 2);
  
  /* Series */
  bg_mdb_init_root_container(&s->series, GAVL_META_CLASS_ROOT_TV_SHOWS);
  bg_mdb_container_set_backend(&s->series, MDB_BACKEND_SQLITE);
  
  // All
  // Genre
  gavl_track_set_num_children(&s->series, 2, 0);

  //  fprintf(stderr, "Created series:\n");
  //  gavl_dictionary_dump(&s->series, 2);
  
  }

static void update_root_containers(bg_mdb_backend_t * b)
  {
  int64_t num;
  sqlite_priv_t * s = b->priv;
  
  num = bg_sqlite_get_int(s->db, "SELECT COUNT("META_DB_ID") FROM songs;");

  if(num && !s->songs_id)
    {
    s->songs_id = bg_mdb_get_klass_id(GAVL_META_CLASS_ROOT_SONGS);
    bg_mdb_add_root_container(b->ctrl.evt_sink, &s->songs);
    }
  else if(!num && s->songs_id)
    {
    bg_mdb_delete_root_container(b->ctrl.evt_sink, s->songs_id);
    s->songs_id = NULL;
    }
  
  num = bg_sqlite_get_int(s->db, "SELECT COUNT("META_DB_ID") FROM albums;");

  if(num && !s->albums_id)
    {
    s->albums_id = bg_mdb_get_klass_id(GAVL_META_CLASS_ROOT_MUSICALBUMS);
    bg_mdb_add_root_container(b->ctrl.evt_sink, &s->albums);
    }
  else if(!num && s->albums_id)
    {
    bg_mdb_delete_root_container(b->ctrl.evt_sink, s->albums_id);
    s->albums_id = NULL;
    }
  
  num = bg_sqlite_get_int(s->db, "SELECT COUNT("META_DB_ID") FROM movies;");

  if(num && !s->movies_id)
    {
    s->movies_id = bg_mdb_get_klass_id(GAVL_META_CLASS_ROOT_MOVIES);
    bg_mdb_add_root_container(b->ctrl.evt_sink, &s->movies);
    }
  else if(!num && s->movies_id)
    {
    bg_mdb_delete_root_container(b->ctrl.evt_sink, s->movies_id);
    s->movies_id = NULL;
    }
  
  num = bg_sqlite_get_int(s->db, "SELECT COUNT("META_DB_ID") FROM shows;");

  if(num && !s->series_id)
    {
    s->series_id = bg_mdb_get_klass_id(GAVL_META_CLASS_ROOT_TV_SHOWS);
    bg_mdb_add_root_container(b->ctrl.evt_sink, &s->series);
    }
  else if(!num && s->series_id)
    {
    bg_mdb_delete_root_container(b->ctrl.evt_sink, s->series_id);
    s->series_id = NULL;
    }
  
  
  }

void bg_mdb_create_sqlite(bg_mdb_backend_t * b)
  {
  int exists = 0;
  char * filename;
  sqlite_priv_t * priv;
  int result;
  
  priv = calloc(1, sizeof(*priv));

  b->parameters = parameters;

#ifndef NO_RESCAN  
  b->flags |= BE_FLAG_RESCAN;
#endif
  
  filename = bg_sprintf("%s/db.sqlite", b->db->path);
  
  /* Create */

  if(!access(filename, R_OK | W_OK))
    exists = 1;
  
  result = sqlite3_open(filename, &priv->db);
  
  if(result)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Cannot open database %s: %s", filename,
           sqlite3_errmsg(priv->db));
    sqlite3_close(priv->db);
    priv->db = NULL;
    free(filename);
    return;
    }
  free(filename);
  
  b->priv = priv;
  b->destroy = destroy_sqlite;

  bg_sqlite_init_strcoll(priv->db);
  
  bg_controllable_init(&b->ctrl,
                       bg_msg_sink_create(handle_msg_sqlite, b, 0),
                       bg_msg_hub_create(1));

  if(!exists && !create_tables(b))
    return; // Should not happen if the path is writeable

  create_root_containers(b);
  }

void bg_mdb_add_sql_directory(bg_controllable_t * db, const char * dir)
  {
  gavl_msg_t * cmd = bg_msg_sink_get(db->cmd_sink);

  gavl_msg_set_id_ns(cmd, BG_FUNC_DB_ADD_SQL_DIR, BG_MSG_NS_DB);
  gavl_msg_set_arg_string(cmd, 0, dir);
  
  bg_msg_sink_put(db->cmd_sink);
  }

void bg_mdb_del_sql_directory(bg_controllable_t * db, const char * dir)
  {
  gavl_msg_t * cmd = bg_msg_sink_get(db->cmd_sink);

  gavl_msg_set_id_ns(cmd, BG_FUNC_DB_DEL_SQL_DIR, BG_MSG_NS_DB);
  gavl_msg_set_arg_string(cmd, 0, dir);

  bg_msg_sink_put(db->cmd_sink);
  }

void bg_mdb_add_sql_directory_sync(bg_controllable_t * db, const char * dir)
  {
  gavl_msg_t msg;
  gavl_msg_init(&msg);
  
  gavl_msg_set_id_ns(&msg, BG_FUNC_DB_ADD_SQL_DIR, BG_MSG_NS_DB);
  gavl_msg_set_arg_string(&msg, 0, dir);
  
  bg_controllable_call_function(db, &msg, NULL, NULL, 1000*2*3600);
  gavl_msg_free(&msg);
  
  }

void bg_mdb_del_sql_directory_sync(bg_controllable_t * db, const char * dir)
  {
  gavl_msg_t msg;
  gavl_msg_init(&msg);
  
  gavl_msg_set_id_ns(&msg, BG_FUNC_DB_DEL_SQL_DIR, BG_MSG_NS_DB);
  gavl_msg_set_arg_string(&msg, 0, dir);
  
  bg_controllable_call_function(db, &msg, NULL, NULL, 1000*2*3600);
  gavl_msg_free(&msg);
  }
