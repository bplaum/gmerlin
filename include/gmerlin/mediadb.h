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

#ifndef __MEDIADB_H_
#define __MEDIADB_H_


#include <time.h>
#include <gmerlin/pluginregistry.h>

/*
 *   object.item.imageItem.photo
 *   object.item.audioItem.musicTrack
 *   object.item.audioItem.audioBroadcast
 *   object.item.audioItem.audioBook
 *   object.item.videoItem.movie
 *   object.item.videoItem.videoBroadcast
 *   object.item.videoItem.musicVideoClip
 *   object.item.playlistItem
 *   object.item.textItem
 *   object.container
 *   object.container.storageFolder
 *   object.container.person
 *   object.container.person.musicArtist
 *   object.container.playlistContainer
 *   object.container.album.musicAlbum
 *   object.container.album.photoAlbum
 *   object.container.genre.musicGenre
 *   object.container.genre.movieGenre
 */

typedef struct bg_db_s bg_db_t;

typedef enum
  {
  BG_DB_SCAN_AUDIO = (1<<0),
  BG_DB_SCAN_VIDEO = (1<<1),
  BG_DB_SCAN_PHOTO = (1<<2),
  } bg_db_scan_type_t;

/*
 *  Date
 */

typedef struct
  {
  int day;   // 1 - 31 (0 = unknown)
  int month; // 1 - 12 (0 = unknown)
  int year;  // 2013, 9999 = unknown
  } bg_db_date_t;

int bg_db_date_equal(const bg_db_date_t * d1,
                     const bg_db_date_t * d2);

void bg_db_date_copy(bg_db_date_t * dst,
                     const bg_db_date_t * src);

#define BG_DB_DATE_STRING_LEN GAVL_METADATA_DATE_STRING_LEN

char * bg_db_date_to_string(const bg_db_date_t * d, char * str);
void bg_db_string_to_date(const char * str, bg_db_date_t * d);

#define BG_DB_TIME_STRING_LEN 20
time_t bg_db_string_to_time(const char * str);
char * bg_db_time_to_string(time_t time, char * str);

void bg_db_date_set_invalid(bg_db_date_t * d);
int bg_db_date_is_invalid(bg_db_date_t * d);

/*
 *  Object definitions
 */

#define BG_DB_FLAG_CONTAINER  (1<<0) // Real container
#define BG_DB_FLAG_NO_EMPTY   (1<<1) // Auto delete when empty
#define BG_DB_FLAG_VCONTAINER (1<<2) // Virtual container (needs browse_children method)
#define BG_DB_FLAG_FILE       (1<<3) // Derived from file
#define BG_DB_FLAG_IMAGE      (1<<4) // Derived from image
#define BG_DB_FLAG_VIDEO      (1<<5) // Derived from video

typedef enum
  {
  BG_DB_OBJECT_OBJECT       = (0<<16),
  BG_DB_OBJECT_FILE         = (1<<16) | BG_DB_FLAG_FILE,
  // object.item.audioItem.musicTrack
  BG_DB_OBJECT_AUDIO_FILE   = (2<<16) | BG_DB_FLAG_FILE,
  BG_DB_OBJECT_VIDEO_FILE   = (3<<16) | BG_DB_FLAG_FILE | BG_DB_FLAG_VIDEO,
  BG_DB_OBJECT_IMAGE_FILE   = (4<<16) | BG_DB_FLAG_FILE | BG_DB_FLAG_IMAGE,
  // Several image types follow
  BG_DB_OBJECT_PHOTO         = (5<<16) | BG_DB_FLAG_FILE | BG_DB_FLAG_IMAGE,
  BG_DB_OBJECT_ALBUM_COVER   = (6<<16) | BG_DB_FLAG_FILE | BG_DB_FLAG_IMAGE,
  BG_DB_OBJECT_VIDEO_PREVIEW = (7<<16) | BG_DB_FLAG_FILE | BG_DB_FLAG_IMAGE,
  BG_DB_OBJECT_MOVIE_ART     = (8<<16) | BG_DB_FLAG_FILE | BG_DB_FLAG_IMAGE,
  BG_DB_OBJECT_THUMBNAIL     = (9<<16) | BG_DB_FLAG_FILE | BG_DB_FLAG_IMAGE,

  // Several video file types follow

  // One episode of a series
  BG_DB_OBJECT_VIDEO_EPISODE    = (10<<16) | BG_DB_FLAG_FILE | BG_DB_FLAG_VIDEO,
  // One part of a multipart movie
  BG_DB_OBJECT_MOVIE_PART       = (11<<16) | BG_DB_FLAG_FILE | BG_DB_FLAG_VIDEO,
  // One movie in a single file
  BG_DB_OBJECT_MOVIE            = (12<<16) | BG_DB_FLAG_FILE | BG_DB_FLAG_VIDEO,

  BG_DB_OBJECT_VIDEO_NFO        = (13<<16) | BG_DB_FLAG_FILE, // .NFO file for video metadata
  
  BG_DB_OBJECT_RADIO_URL        = (14<<16),
  
  // Root container: Object ID 0, has all toplevel containers as children

  BG_DB_OBJECT_ROOT          = (50<<16) | BG_DB_FLAG_CONTAINER,

  // object.container.album.musicAlbum
  // Audio albums are *no* containers for the internal database
  // because they are referenced by their children not by the parent_id
  // but by the album
  BG_DB_OBJECT_AUDIO_ALBUM      = (51<<16) | BG_DB_FLAG_VCONTAINER,

  BG_DB_OBJECT_CONTAINER        = (52<<16) | BG_DB_FLAG_CONTAINER,
  // object.container.storageFolder
  BG_DB_OBJECT_DIRECTORY        = (53<<16) | BG_DB_FLAG_CONTAINER, 
  BG_DB_OBJECT_PLAYLIST         = (54<<16) | BG_DB_FLAG_VCONTAINER,
  // Virtual Folder */
  BG_DB_OBJECT_VFOLDER          = (55<<16) | BG_DB_FLAG_CONTAINER | BG_DB_FLAG_NO_EMPTY,
  BG_DB_OBJECT_VFOLDER_LEAF     = (56<<16) | BG_DB_FLAG_VCONTAINER | BG_DB_FLAG_NO_EMPTY,

  BG_DB_OBJECT_TVSERIES         = (57<<16) | BG_DB_FLAG_CONTAINER | BG_DB_FLAG_NO_EMPTY,
  BG_DB_OBJECT_SEASON           = (58<<16) | BG_DB_FLAG_VCONTAINER | BG_DB_FLAG_NO_EMPTY,

  // Movie consisting of multiple files
  BG_DB_OBJECT_MOVIE_MULTIPART  = (59<<16) | BG_DB_FLAG_VCONTAINER | BG_DB_FLAG_NO_EMPTY,

  // Root Containers: These are the only immediate children of Root (ID 0).
  // There is one for several media types

  BG_DB_OBJECT_ROOTFOLDER       = (60<<16) | BG_DB_FLAG_CONTAINER | BG_DB_FLAG_NO_EMPTY,
  
  } bg_db_object_type_t;

typedef struct bg_db_object_class_s bg_db_object_class_t;

typedef struct
  {
  int64_t id;
  bg_db_object_type_t type;
  
  int64_t ref_id;    // Original ID
  int64_t parent_id;  // ID of the real parent
  int children;      // For containers only

  int64_t size;
  gavl_time_t duration;
  char * label;

  /* Internal stuff, do not touch */

  int found;    // Used by sqlite
  int flags;    // Used in-memory only
  const bg_db_object_class_t * klass;

  } bg_db_object_t;

/* Directory on the file system */
typedef struct
  {
  bg_db_object_t obj;
  char * path;   // Relative to db file
  int scan_flags; // ORed flags if BG_DB_* types
  int update_id;
  int64_t scan_dir_id;
  } bg_db_dir_t;

/* File in the file system */
typedef struct
  {
  bg_db_object_t obj;
  char * path;  // Relative to db file
  time_t mtime;
  char * mimetype;
  int64_t mimetype_id;
  int64_t scan_dir_id;
  } bg_db_file_t;

/* 
 * Audio file 
 */

typedef struct
  {
  bg_db_file_t file;
  
  char * title;      // TITLE
  char * search_title;

  char * artist;     
  int64_t artist_id; // ARTIST

  char * genre;      // GENRE
  int64_t genre_id;

  bg_db_date_t date;

  char * album;
  int64_t album_id;

  int track;
  
  char * bitrate;    // BITRATE
  int samplerate;
  int channels; 

  char * albumartist;     // Not in db, set only while creating
  int64_t albumartist_id; // Not in db, set only while creating  
  } bg_db_audio_file_t;

/*
 *  Webradio/Webtv station
 */

typedef struct
  {
  bg_db_object_t obj;
  char * stream_url;
  char * station_url;
  char * mimetype;
  int64_t mimetype_id;

  char * audio_bitrate;
  int samplerate;
  int channels;
  } bg_db_url_t;

void bg_db_add_radio_album(bg_db_t * db, const char * album_file);

int64_t bg_db_url_by_location(bg_db_t * db, const char * location);

/* 
 * video file 
 */

typedef struct
  {
  bg_db_file_t file;

  int width;
  int height;
  int timescale;
  int frame_duration;

  /* Display aspect ratio */
  int aspect_num;
  int aspect_den;
  
  char * video_codec;

  int64_t collection; // Multipart movie or season
  int index;          // Index within collection
  
  } bg_db_video_file_t;


/* Audio Album */
typedef struct
  {
  bg_db_object_t obj;

  char * artist;
  int64_t artist_id;
  char * title;
  char * search_title;
  
  char * genre;      // GENRE
  int64_t genre_id;

  bg_db_date_t date;

  int64_t cover_id;
  } bg_db_audio_album_t;

/* Video info: Can be associated with a series, a season or a movie */

typedef struct
  {
  int64_t id;

  char * title;      // TITLE
  char * search_title;

  char * title_orig;

  char * rating;           // 0 .. 10.0, probably arbitrary
  char * parental_control;

  bg_db_date_t date;

  int64_t * director_ids; /* Director(s)           */
  int64_t * actor_ids;    /* Actor(s)              */
  int64_t * country_ids;  /* Production countries  */
  int64_t * genre_ids;    /* Genre(s)              */
  int64_t * audio_language_ids; /* Audio languages */
  int64_t * subtitle_language_ids; /* Subtitle languages */
  
  char ** directors;
  char ** actors;
  char ** countries;
  char ** genres;
  char ** audio_languages;
  char ** subtitle_languages;
  
  char * plot;

  int64_t poster_id;
  int64_t fanart_id;

  /* Where we got this info */
  int64_t nfo_file;
  } bg_db_video_info_t;

/* Movie or episode. In the latter case, the REF_ID points to the Season */

typedef struct
  {
  bg_db_video_file_t file;
  bg_db_video_info_t info;
  } bg_db_movie_t;

/* Video container. Can be a series, a season or a multipart movie */

typedef struct
  {
  bg_db_object_t obj;
  bg_db_video_info_t info;
  } bg_db_video_container_t;

/* Image */

typedef struct
  {
  bg_db_file_t file;
  int width;
  int height;
  int64_t context_id; // Context where this item belongs
  bg_db_date_t date;  // Date from EXIF
  } bg_db_image_file_t;

/* Virtual folder */

typedef enum
  {
    BG_DB_CAT_YEAR              = 1,
    BG_DB_CAT_GROUP             = 2,
    BG_DB_CAT_ARTIST            = 3,
    BG_DB_CAT_GENRE             = 4,
    BG_DB_CAT_ACTOR             = 5,
    BG_DB_CAT_DIRECTOR          = 6,
    BG_DB_CAT_COUNTRY           = 7,
  } bg_db_category_t;

#define BG_DB_VFOLDER_MAX_DEPTH 8

typedef struct
  {
  bg_db_category_t cat;
  int64_t val;
  } bg_db_vfolder_path_t;

typedef struct
  {
  bg_db_object_t obj;

  int depth;
  bg_db_object_type_t type;
  
  bg_db_vfolder_path_t path[BG_DB_VFOLDER_MAX_DEPTH];
 
  } bg_db_vfolder_t;


/* Playlist */
typedef struct
  {
  
  } bg_db_playlist_t;

/* Root Folder */
typedef struct
  {
  bg_db_object_t obj;
  bg_db_object_type_t child_type;
  } bg_db_rootfolder_t;

/*
 *  Public entry points
 */

bg_db_object_type_t bg_db_object_get_type(void * obj);

bg_db_t * bg_db_create(const char * dir,
                       bg_plugin_registry_t * plugin_reg, int create);

const char * bg_db_get_filename(bg_db_t *);

void bg_db_flush(bg_db_t * db);

void bg_db_destroy(bg_db_t *);

void bg_db_start_transaction(bg_db_t *);
void bg_db_end_transaction(bg_db_t *);

/* Edit functions */
void bg_db_add_directory(bg_db_t *, const char * dir, int scan_type);
void bg_db_del_directory(bg_db_t *, const char * dir);

void bg_db_add_album(bg_db_t *, const char * album);

/* Query functions */

typedef void (*bg_db_query_callback)(void * priv, void * obj);

/* Query from DB  */
void * bg_db_object_query(bg_db_t * db, int64_t id); 
int64_t bg_db_object_get_id(void * obj);

int
bg_db_query_sql(bg_db_t *, const char * sql, bg_db_query_callback cb, void * priv);

int64_t bg_db_object_by_location(bg_db_t * db, const char * location);

/* Get the id of a file from it's path */
int64_t bg_db_file_by_path(bg_db_t * db, const char * path);

void bg_db_object_dump(void * obj);
void bg_db_object_unref(void * obj);
void bg_db_object_ref(void * obj);

const char * bg_db_object_get_label(void * obj);

int
bg_db_query_children(bg_db_t *, void * obj, bg_db_query_callback cb, void * priv,
                     int start, int num, int * total_matches);

const char * bg_db_get_group(const char * str, int64_t * id);

/* Get thumbnails */

void bg_db_browse_thumbnails(bg_db_t * db, int64_t id, 
                             bg_db_query_callback cb, void * data);

void * bg_db_get_thumbnail(bg_db_t * db, int64_t id,
                           int max_width, int max_height, int max_size,
                           const char * mimetype);

/* Get Root folder for a given type */
void * bg_db_rootfolder_get(bg_db_t * db, bg_db_object_type_t type);


#endif //  __MEDIADB_H_
