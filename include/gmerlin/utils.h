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



#ifndef BG_UTILS_H_INCLUDED
#define BG_UTILS_H_INCLUDED

#include <stdio.h>
#include <wchar.h>

#include <gavl/gavl.h>
#include <gavl/metadata.h>
#include <gavl/utils.h>

/** \defgroup utils Utilities
 *  \brief Utility functions
 *
 *  These are lots of utility functions, which should be used, whereever
 *  possible, instead of their libc counterparts (if any). That's because
 *  they also handle portability issues, ans it's easier to make one single
 *  function portable (with \#ifdefs if necessary) and use it throughout the code.
 */

/** \defgroup files Filesystem support
 *  \ingroup utils
 *  \brief Functions for files and directories
 *
 *  @{
 */

/** \brief Append a trailing slash to a path name
 *  \param path Old path (will eventually be freed).
 *  \returns The path, which is garantueed to end with a '/'
 */

/* Append a trailing '/' if it's missing. Argument must be free()able */

char * bg_fix_path(char * path);


/** \brief Search for a directory to store data
 *  \param directory Subdirectory
 *  \returns A directory name or NULL
 *
 *  This function first seeks in the system gmerlin data directory
 *  (e.g. /usr/local/share/gmerlin), then in $HOME/.gmerlin for the
 *  specified file, which must be readable. The subdirectory can also contain
 *  subdirectories e.g. "player/tree".
 */

char * bg_search_var_dir(const char * directory);


/** \brief Search for a file for reading
 *  \param directory Directory
 *  \param file Filename
 *  \returns A filename or NULL
 *
 *  This function first seeks in the system gmerlin data directory
 *  (e.g. /usr/local/share/gmerlin), then in $HOME/.gmerlin for the
 *  specified file, which must be readable. The directory can also contain
 *  subdirectories e.g. "player/tree".
 */

char * bg_search_file_read(const char * directory, const char * file);

/** \brief Search for a file for writing
 *  \param directory Directory
 *  \param file Filename
 *  \returns A filename or NULL
 *
 *  This function first seeks in the in $HOME/.gmerlin for the
 *  specified file, which must be writable. If the file doesn't
 *  exist, an empty file is created. If the directory doesn't exist,
 *  it's created as well. The directory can also contain
 *  subdirectories e.g. "player/tree".
 */

char * bg_search_file_write(const char * directory, const char * file);

char * bg_search_file_write_nocreate(const char * directory, const char * file);

/** \brief Search for an executable
 *  \param file Name of the file (without dirtectory)
 *  \param path If non NULL, the complete path to the exectuable will be returned
 *  \returns 1 if executeable is found anywhere in $PATH:/opt/gmerlin/bin, 0 else.
 *
 *  If path is non NULL, it will contain the path to the executable,
 *  which must be freed after
 */

int bg_search_file_exec(const char * file, char ** path);

/** \brief Find an URL launcher
 *  \returns A newly allocated string, which must be freed
 *
 * This returnes the path of a webbrowser. Under gnome, it will be the
 * your default webbrowser, under other systems, this function will try a
 * list of known webbrowsers.
 */
 
char * bg_find_url_launcher();

/** \brief Display html help
 *  \param path Path
 *
 *  Launch a webbrowser and display a html file.
 *  Path is something lile "userguide/Player.html"
 */

void bg_display_html_help(const char * path);

/** \brief Create a unique filename.
 *  \param format Printf like format. Must contain "%08x" as the only placeholder.
 *  \returns A newly allocated string
 *
 *  Create a unique filename, and create an empty file of this name.
 */

char * bg_create_unique_filename(char * format);

/** \brief Get the canonical filename
 *  \param name Filename
 *  \returns A newly allocated filename
 *
 *  On Glibc systems, this calls canonicalize_file_name(3)
 */

char * bg_canonical_filename(const char * name);

/** \brief Ensure a file extension
 *  \param filename Filename (with or without extension)
 *  \param ext Extension to look for
 *  \returns A newly allocated filename
 *
 *  This function checks case insensitively if filename
 *  ends with the extension. If yes, it returns a copy of
 *  the filename. If not, it returns a copy with the extension
 *  appended. The extention must not start with the dot.
 */

char * bg_filename_ensure_extension(const char * filename,
                                    const char * ext);

/** \brief Get the base name of a path
 *  \param path Path name
 *  \returns A newly allocated label
 *
 *  This splits off leading directory names and the file extension
 *  and returns a newly allocated string
 */

char * bg_path_to_label(const char * path);


/** @} */

/** \defgroup strings String utilities
 *  \ingroup utils
 *  \brief String utilities
 *
 * @{
 */

/** \brief Convert an UTF-8 string to uppercase
 *  \param str String
 *  \returns A newly allocated string.
 */

char * bg_toupper(const char * str);

/** \brief Check if a string looks like an URL.
 *  \param str A string
 *  \returns 1 if the string looks like an URL, 0 else
 *
 *  This checks mostly for the occurrence of "://" at a location, where it makes
 *  sense.
 */

int bg_string_is_url(const char * str);



/*
 *  \brief Get a string suitable for the Host variable
 *  \param host Hostname component of an URL
 *  \param port Port or 0
 *  \returns host:port or host
 */

char * bg_url_get_host(const char * host, int port);

/*
 *  \brief Split off URL variables
 *  \param path Path component of an URL
 *  \param vars Place to store the variables
 *
 *  This will split off variables
 *  like path?var1=val1&var2=val2 and store them into the
 *  metadata structure. 
 *  
 */
 

/*
 *  \brief Get the track index
 *  \param path Path component of an URL
 *  \returns track index or 0
 */


int bg_url_get_track(const char * path);

int bg_url_extract_track(char ** path);


/*
 *  \brief Append URL variables
 *  \param path Path component of an URL
 *  \param vars Variables
 *
 *  This will append variables
 *  like path?var1=val1&var2=val2 from the
 *  metadata structure. 
 */

char * bg_url_append_vars(char * path,
                          const gavl_dictionary_t * vars);



/** \brief Get MD5 hash of a filename
 *  \param gml
 *  \param ret Returns the MD5 sum
 *
 *  This creates an MD5 hash of a gml. For regular
 *  files this is compatible with the thumbnailing
 *  specification
 */

void bg_get_filename_hash(const char * gml, char ret[33]);

/* Create UUID from URI */

char * bg_uri_to_uuid(const char * uri, char ret[37]);

/** \brief Print into a string
 *  \param format printf like format
 *
 *  All other arguments must match the format like in printf.
 *  This function allocates the returned string, thus it must be
 *  freed by the caller.
 */

char * bg_sprintf(const char * format,...) __attribute__ ((format (printf, 1, 2)));

/** \brief Break a string into substrings
 *  \param str String
 *  \param delim Delimiter for the substrings
 *  \returns A NULL terminated array of substrings
 *
 *  Free the result with \ref bg_strbreak_free.
 */

// char ** bg_strbreak(const char * str, char delim);

/** \brief Free a substrings array
 *  \param retval Array
 *
 *  Use this for substring arrays returned by \ref bg_strbreak.
 */

// void bg_strbreak_free(char ** retval);

/** \brief Scramble a string
 *  \param str String to be scrambled
 *  \returns A newly allocated scrambled string
 *
 * Note:
 * Don't even think about using this for security sensitive stuff.
 * It's for saving passwords in files, which should be readable by the
 * the owner only.
 */

char * bg_scramble_string(const char * str);

/** \brief Descramble a string
 *  \param str String to be descrambled
 *  \returns A newly allocated descrambled string
 *
 * Note:
 * Don't even think about using this for security sensitive stuff.
 * It's for saving passwords in files, which should be readable by the
 * the owner only.
 */

char * bg_descramble_string(const char * str);

/** \brief Convert a binary string (in system charset) to an URI
 *  \param pos1 The string
 *  \param len or -1
 *
 * This e.g. replaces " " by "%20".
 */

char * bg_string_to_uri(const char * pos1, int len);

/** \brief Convert an URI to a a binary string (in system charset)
 *  \param pos1 The string
 *  \param len or -1
 *
 * This e.g. replaces "%20" by " ".
 */

char * bg_uri_to_string(const char * pos1, int len);

/** \brief Decode an  URI list
 *  \param str String
 *  \param len Length of the string or -1
 *
 *  This one decodes a string of MIME type text/urilist into
 *  a gmerlin usable array of location strings.
 *  The returned array is NULL terminated, it must be freed by the
 *  caller with bg_urilist_free.
 */

char ** bg_urilist_decode(const char * str, int len);

/** \brief Free an URI list
 *  \param uri_list Decoded URI list returned by \ref bg_uri_to_string
 */

void bg_urilist_free(char ** uri_list);

/** \brief Convert a string from the system character set to UTF-8
 *  \param str String
 *  \param len Length or -1
 *  \returns A newly allocated string
 *
 *  The "system charset" is obtained with nl_langinfo().
 */

char * bg_system_to_utf8(const char * str, int len);

/** \brief Convert a string from UTF-8 to the system character set
 *  \param str String
 *  \param len Length or -1
 *  \returns A newly allocated string
 *
 *  The "system charset" is obtained with nl_langinfo().
 */

char * bg_utf8_to_system(const char * str, int len);

/** \brief Get a language name
 *  \param iso An iso-639 3 character code
 *  \returns The name of the language or NULL
 */

const char * bg_get_language_name(const char * iso);



/** \brief Check if a string occurs in a space-separated list of strings
 *  \param str String
 *  \param key_list Space separated list of keys
 *  \returns 1 of str occurs in key_list, 0 else
 */

int bg_string_match(const char * str, const char * key_list);

/** \brief Convert a multibyte string to a wide character string
 *  \param str String
 *  \returns A newly allocated wide character string
 */

wchar_t * bg_str_to_wchar(const char * str);

/** \brief Convert a wide character string to a byte string
 *  \param wstr String
 *  \returns A newly allocated multibyte string
 */

char * bg_wchar_to_str(const wchar_t * wstr);

/** \brief Capitalize the first character of each word in a string
 *  \param str String
 *  \returns A newly allocated capitalized string
 */

char * bg_capitalize(const char * str);

/** \brief Strip leading and trailing whitespace from a string
 *  \param str String
 *  \returns The same string without whitespaces or NULL
 */

char * bg_strip_space(char * str);

/* @} */

/** \defgroup misc Misc stuff
 *  \ingroup utils
 *
 *  @{
 */

/** \brief Convert an audio format to a string
 *  \param format An audio format
 *  \param use_tabs 1 to use tabs for separating field names and values
 *  \returns A newly allocated string
 */

char * bg_audio_format_to_string(gavl_audio_format_t * format, int use_tabs);


/** \brief Convert a video format to a string
 *  \param format A video format
 *  \param use_tabs 1 to use tabs for separating field names and values
 *  \returns A newly allocated string
 */

char * bg_video_format_to_string(gavl_video_format_t * format, int use_tabs);

/** \brief Create a stream label
 *  \param index Index of the stream (starting with 0)
 *  \param m Metadata
 *  \returns A newly allocated string
 */

char * bg_get_stream_label(int index, const gavl_dictionary_t * m);

/** \brief Dump to stderr
 *  \param format Format (printf compatible)
 */

void bg_dprintf(const char * format, ...) __attribute__ ((format (printf, 1, 2)));

/** \brief Dump to stderr with intendation
 *  \param indent How many spaces to prepend
 *  \param format Format (printf compatible)
 */

void bg_diprintf(int indent, const char * format, ...) __attribute__ ((format (printf, 2, 3)));

/** \brief Read an entire file into a buffer
 *  \param filename Name of the file
 *  \param len Returns the length in bytes
 *  \returns A buffer containing the entire file
 */

int bg_read_file(const char * filename, gavl_buffer_t * buf);

int bg_read_file_range(const char * filename, gavl_buffer_t * buf, int64_t start, int64_t len);

int bg_read_location(const char * location,
                     gavl_buffer_t * ret,
                     int64_t start, int64_t size,
                     gavl_dictionary_t * dict);


/** \brief Write an entire file into a buffer
 *  \param filename Name of the file
 *  \param data Data to write
 *  \param len Length in bytes
 *  \returns 1 on success, 0 on failure
 */

int bg_write_file(const char * filename, void * data, int len);

/** \brief Lock a file for exclusive access
 *  \param f An open file
 *  \param wr An open file
 *  \returns 1 on success, 0 on failure
 */

int bg_lock_file(FILE * f, int wr);

/** \brief Lock a file for exclusive access without waiting
 *  \param f An open file
 *  \param wr An open file
 *  \returns 1 on success, 0 on failure
 *
 *  Like \ref bg_lock_file but doesn't wait
 */

int bg_lock_file_nowait(FILE * f, int wr);

/** \brief Unlock a file for exclusive access
 *  \param f An open file
 *  \returns 1 on success, 0 on failure
 */

int bg_unlock_file(FILE * f);


FILE * bg_lock_directory(const char * directory);
void bg_unlock_directory(FILE * lockfile, const char * directory);

/** \brief Get the size of an open file
 *  \param f A file opened for reading
 *  \returns File size in bytes
 */

size_t bg_file_size(FILE * f);

/**
 */

int bg_cache_directory_cleanup(const char * cache_dir);

int bg_load_cache_item(const char * cache_dir,
                       const char * md5,
                       const char ** mimetype,
                       gavl_buffer_t * buf);

void bg_save_cache_item(const char * cache_dir,
                        const char * md5,
                        const char * mimetype,
                        const gavl_buffer_t * buf);



  
/** \brief Convert a ISO 639-2/B language code to a ISO 639-2/T code
 *  \param code ISO 639-2/B
 *  \returns ISO 639-2/T code
 */

const char * bg_iso639_b_to_t(const char * code);

/** \brief Convert a ISO 639-2/T language code to a ISO 639-2/B code
 *  \param code ISO 639-2/T
 *  \returns ISO 639-2/B code
 */

const char * bg_iso639_t_to_b(const char * code);


/** \brief Set a date and time field of the metadata from the lcoal time
 *  \param m Metadata
 *  \param field Field to set
 */

void bg_metadata_date_now(gavl_dictionary_t * m, const char * key);

/** \brief Make the current process become a daemon
 */

void bg_daemonize();

/** \brief Mime types
 */

#define BG_MIME_SUPPORTS_MULTITRACK (1<<0)

typedef struct
  {
  const char * ext;
  const char * mimetype;
  const char * name;
  const char ** other_extensions;
  int flags;
  } bg_mimetype_t;

extern bg_mimetype_t bg_mimetypes[];

/** \brief Translate mimetype to file extension
 *  \param mimetype Mimetype
 *  \returns extension or NULL
 */

const char * bg_mimetype_to_ext(const char * mimetype);

/** \brief Translate mimetype to format name
 *  \param mimetype Mimetype
 *  \returns name or NULL
 */

const char * bg_mimetype_to_name(const char * mimetype);

/** \brief Translate file extension to mimetype
 *  \param extension Extension
 *  \returns Mimetype or NULL
 */

const char * bg_ext_to_mimetype(const char * ext);

const char * bg_url_to_mimetype(const char * url);

int bg_file_supports_multitrack(const char * path);


int bg_base64_decode(const char * in,
                     int in_len,
                     uint8_t ** out,
                     int * out_alloc);

int bg_base64_encode(const uint8_t * in,
                     int in_len,
                     char ** out,
                     int * out_alloc);

char * bg_base64_encode_buffer(const gavl_buffer_t * buf);
void bg_base64_decode_buffer(const char * str, gavl_buffer_t * buf);

// resblock needs to be 20 bytes
void * bg_sha1_buffer(const char *buffer, size_t len, void *resblock);

char * bg_get_chapter_label(int index, int64_t t, int scale, const char * name);

const char * bg_tempdir();

/* Remove file, directories are removed recursively */
int bg_remove_file(const char * file);

char * bg_search_desktop_file(const char * file);
int bg_read_desktop_file(const char * file, gavl_dictionary_t * ret);

char * bg_search_application_icon(const char * file, int size);

/* @} */

void bg_sigint_raise();

void bg_handle_sigint();

int bg_got_sigint();


/* ID3V2 write support.
   Used by the encoder plugins and for generating ID3V2 tags on the fly. */

typedef struct bg_id3v2_s bg_id3v2_t;

bg_id3v2_t * bg_id3v2_create(const gavl_dictionary_t*, int add_cover);

/* Output must be seekable */
#define BG_ID3_ENCODING_LATIN1    0x00
#define BG_ID3_ENCODING_UTF8      0x03

int bg_id3v2_write(gavl_io_t * output, const bg_id3v2_t *, int encoding);

void bg_id3v2_destroy(bg_id3v2_t *);

/* NPT timestamp parsing and unparsing */

int bg_npt_parse(const char * str, gavl_time_t * ret);
char * bg_npt_to_string(gavl_time_t time);

int bg_npt_parse_range(const char * str, gavl_time_t * start, gavl_time_t * end);
char * bg_npt_range_to_string(gavl_time_t start, gavl_time_t end);

/* Output must be seekable */
int bg_flac_cover_tag_write(gavl_io_t * output, const gavl_dictionary_t * image_uri, int last_tag);

const char * bg_get_search_string(const char * str);

extern char const * const bg_language_codes[];
extern char const * const bg_language_labels[];

/* Support for radio-browser URIs */
char * bg_rb_make_uri(const char * station_uuid);
char * bg_rb_resolve_uri(const char * uri);
int bg_rb_check_uri(const char * uri);
char  *bg_get_rb_server();

/* Global cleanup function */
void bg_global_cleanup();


#ifdef DEBUG
#define bg_debug(f,...) fprintf(stderr, f, __VA_ARGS__)
#else
#define bg_debug(f,...)
#endif



#endif // BG_UTILS_H_INCLUDED

