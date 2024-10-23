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



#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <config.h>


#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <ctype.h>
#include <language_table.h>
#include <wctype.h>
#include <errno.h>
#include <limits.h>
#include <uuid/uuid.h>

/* stat stuff */
#include <sys/types.h>
#include <sys/stat.h>

#include <unistd.h>
#include <gmerlin/charset.h>

#include <gmerlin/utils.h>
#include <gmerlin/log.h>
#define LOG_DOMAIN "utils"

#include <gmerlin/translation.h>
#include <gmerlin/pluginregistry.h>

#include <gavl/metatags.h>
#include <gavl/gavlsocket.h>
#include <gavl/http.h>
#include <gmerlin/http.h>

static char * strip_space(char * str, int do_free);

char * bg_fix_path(char * path)
  {
  char * ret;
  int len;
  
  if(!path)
    return path;

  len = strlen(path);
  
  if(!len)
    {
    free(path);
    return NULL;
    }
  if(path[len-1] != '/')
    {
    ret = malloc(len+2);
    strcpy(ret, path);
    free(path);
    ret[len] = '/';
    ret[len+1] = '\0';
    
    return ret;
    }
  else
    return path;
  }
  
char * bg_create_unique_filename(char * template)
  {
  char * filename;
  struct stat stat_buf;
  FILE * file;
  int err = 0;
  uint32_t count;

  count = 0;

  filename = gavl_sprintf(template, 0);

  while(1)
    {
    
    if(stat(filename, &stat_buf) == -1)
      {
      /* Create empty file */
      file = fopen(filename, "w");
      if(file)
        {
        fclose(file);
        }
      else
        {
        gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Cannot open file \"%s\" for writing",
               filename);
        err = 1;
        }
      if(err)
        {
        free(filename);
        return NULL;
        }
      else
        return filename;
      }
    count++;
    sprintf(filename, template, count);
    }
  }

#if 0
char ** bg_strbreak(const char * str, char delim)
  {
  int num_entries;
  char *pos, *end = NULL;
  const char *pos_c;
  char ** ret;
  int i;
  if(!str || (*str == '\0'))
    return NULL;
    
  pos_c = str;
  
  num_entries = 1;
  while((pos_c = strchr(pos_c, delim)))
    {
    num_entries++;
    pos_c++;
    }
  ret = calloc(num_entries+1, sizeof(char*));

  ret[0] = gavl_strdup(str);
  
  pos = ret[0];
  for(i = 0; i < num_entries; i++)
    {
    if(i)
      {
      ret[i] = pos;
      }
    if(i < num_entries-1)
      {
      end = strchr(pos, delim);
      *end = '\0';
      }
    end++;
    pos = end;
    }

  for(i = 0; i < num_entries; i++)
    strip_space(ret[i], 0);
  
  return ret;
  }

void bg_strbreak_free(char ** retval)
  {
  free(retval[0]);
  free(retval);
  }
#endif


int bg_string_is_url(const char * str)
  {
  const char * pos, * end_pos;
  pos = str;
  end_pos = strstr(str, "://");

  if(!end_pos)
    return 0;
  
  while(pos != end_pos)
    {
    if(!isalnum(*pos) && (*pos != '+') && (*pos != '-') && (*pos != '.') && (*pos != '\0'))
      return 0;
    pos++;
    }
  return 1;
  }

char * bg_path_to_label(const char * path)
  {
  const char * start;
  const char * end;
  start = strrchr(path, '/');
  if(!start)
    start = path;
  else
    start++;

  end = strrchr(start, '.');
  if(!end)
    end = start + strlen(start);

  return gavl_strndup(start, end);
  }


int gavl_url_split(const char * url,
                 char ** protocol,
                 char ** user,
                 char ** password,
                 char ** hostname,
                 int * port,
                 char ** path)
  {
  const char * pos1;
  const char * pos2;

  /* For detecting user:pass@blabla.com/file */

  const char * colon_pos;
  const char * at_pos;
  const char * slash_pos;
  
  pos1 = url;

  /* Sanity check */
  
  pos2 = strstr(url, "://");
  if(!pos2)
    return 0;

  /* Protocol */
    
  if(protocol)
    *protocol = gavl_strndup( pos1, pos2);

  pos2 += 3;
  pos1 = pos2;

  /* Check for user and password */

  colon_pos = strchr(pos1, ':');
  at_pos = strchr(pos1, '@');
  slash_pos = strchr(pos1, '/');

  if(colon_pos && at_pos && at_pos &&
     (colon_pos < at_pos) && 
     (at_pos < slash_pos))
    {
    if(user)
      *user = gavl_strndup( pos1, colon_pos);
    pos1 = colon_pos + 1;
    if(password)
      *password = gavl_strndup( pos1, at_pos);
    pos1 = at_pos + 1;
    pos2 = pos1;
    }
  
  /* Hostname */

  if(*pos1 == '[') // IPV6
    {
    pos1++;
    pos2 = strchr(pos1, ']');
    if(!pos2)
      return 0;

    if(hostname)
      *hostname = gavl_strndup( pos1, pos2);
    pos2++;
    }
  else
    {
    while((*pos2 != '\0') && (*pos2 != ':') && (*pos2 != '/'))
      pos2++;
    if(hostname)
      *hostname = gavl_strndup( pos1, pos2);
    }
  
  switch(*pos2)
    {
    case '\0':
      if(port)
        *port = -1;
      return 1;
      break;
    case ':':
      /* Port */
      pos2++;
      if(port)
        *port = atoi(pos2);
      while(isdigit(*pos2))
        pos2++;
      break;
    default:
      if(port)
        *port = -1;
      break;
    }

  if(path)
    {
    pos1 = pos2;
    pos2 = pos1 + strlen(pos1);
    if(pos1 != pos2)
      *path = gavl_strndup( pos1, pos2);
    else
      *path = NULL;
    }
  return 1;
  }

#if 0

/*
 *  Split off vars like path?var1=val1&var2=val2#fragment
 */

static void url_vars_to_metadata(const char * pos, gavl_dictionary_t * vars)
  {
  int i;
  char ** str;
  i = 0;
  
  str = gavl_strbreak(pos, '&');

  if(!str)
    return;
  
  while(str[i])
    {
    char * key;
    pos = strchr(str[i], '=');
    if(!pos)
      gavl_dictionary_set_int(vars, str[i], 1);
    else
      {
      key = gavl_strndup(str[i], pos);
      pos++;

      gavl_dictionary_set_string(vars, key, pos);
      free(key);
      }
    i++;
    }
  gavl_strbreak_free(str);
  }

void bg_url_get_vars_c(const char * path,
                       gavl_dictionary_t * vars)
  {
  const char * pos = strrchr(path, '?');
  if(!pos)
    return;
  pos++;

  url_vars_to_metadata(pos, vars);
  }
 
void bg_url_get_vars(char * path,
                     gavl_dictionary_t * vars)
  {
  char * pos;

  /* Hack */
#if 0
  pos = strchr(path, '#');
  if(pos)
    *pos = '\0';
#endif
  /* End Hack */

  pos = strrchr(path, '?');
  if(!pos)
    return;

  *pos = '\0';
  
  if(!vars)
    return;
  
  pos++;

  if(vars)
    url_vars_to_metadata(pos, vars);
  }
#endif

int bg_url_get_track(const char * path)
  {
  int ret = 0;
  gavl_dictionary_t vars;
  gavl_dictionary_init(&vars);
  gavl_url_get_vars_c(path, &vars);
  if(gavl_dictionary_get_int(&vars, GAVL_URL_VAR_TRACK, &ret))
    ret--;
  
  gavl_dictionary_free(&vars);
  return ret;
  }

int bg_url_extract_track(char ** path)
  {
  int ret = 0;
  gavl_dictionary_t vars;
  gavl_dictionary_init(&vars);

  gavl_url_get_vars(*path, &vars);

  if(gavl_dictionary_get_int(&vars, GAVL_URL_VAR_TRACK, &ret))
    {
    ret--;
    gavl_dictionary_set(&vars, GAVL_URL_VAR_TRACK, NULL);
    }
  
  *path = bg_url_append_vars(*path, &vars);
  gavl_dictionary_free(&vars);
  return ret;
  }
  
  

char * bg_url_append_vars(char * path,
                          const gavl_dictionary_t * vars)
  {
  char sep;
  char * tmp_string;
  char * val_string;
  int i;
  int idx = 0;
  
  for(i = 0; i < vars->num_entries; i++)
    {
    if(!strchr(path, '?'))
      sep = '?';
    else
      sep = '&';

    if(!(val_string = gavl_value_to_string(&vars->entries[i].v)))
      {
      gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Value type %s not supported in URL variables",
             gavl_type_to_string(vars->entries[i].v.type));
      continue;
      }
    
    tmp_string = gavl_sprintf("%c%s=%s", sep, vars->entries[i].name, val_string);
    idx++;
    
    path = gavl_strcat(path, tmp_string);
    free(tmp_string);
    free(val_string);
    }
  return path;
  }

char * bg_url_get_host(const char * host, int port)
  {
  if(port > 0)
    return gavl_sprintf("%s:%d", host, port);
  else
    return gavl_strdup(host);
  }

/* Scramble and descramble password (taken from gftp) */

char * bg_scramble_string(const char * str)
  {
  char *newstr, *newpos;
  
  newstr = malloc (strlen (str) * 2 + 2);
  newpos = newstr;
  
  *newpos++ = '$';

  while (*str != 0)
    {
    *newpos++ = ((*str >> 2) & 0x3c) | 0x41;
    *newpos++ = ((*str << 2) & 0x3c) | 0x41;
    str++;
    }
  *newpos = 0;

  return (newstr);
  }

char * bg_descramble_string(const char *str)
  {
  const char *strpos;
  char *newstr, *newpos;
  int error;
  
  if (*str != '$')
    return (gavl_strdup(str));
  
  strpos = str + 1;
  newstr = malloc (strlen (strpos) / 2 + 1);
  newpos = newstr;
  
  error = 0;
  while (*strpos != '\0' && (*strpos + 1) != '\0')
    {
    if ((*strpos & 0xc3) != 0x41 ||
        (*(strpos + 1) & 0xc3) != 0x41)
      {
      error = 1;
      break;
      }
    
    *newpos++ = ((*strpos & 0x3c) << 2) |
      ((*(strpos + 1) & 0x3c) >> 2);
    
    strpos += 2;
    }
  
  if(error)
    {
    free (newstr);
    return (gavl_strdup(str));
    }
  
  *newpos = '\0';
  return (newstr);
  }

const char * bg_get_language_name(const char * iso)
  {
  int i = 0;
  while(bg_language_codes[i])
    {
    if((bg_language_codes[i][0] == iso[0]) &&
       (bg_language_codes[i][1] == iso[1]) &&
       (bg_language_codes[i][2] == iso[2]))
      return bg_language_labels[i];
    i++;
    }
  return iso;
  }

int bg_string_match(const char * key,
                    const char * key_list)
  {
  const char * pos;
  const char * end;

  pos = key_list;
      

  if(!key_list)
    return 0;
  
  while(1)
    {
    end = pos;
    while(!isspace(*end) && (*end != '\0'))
      end++;
    if(end == pos)
      break;

    if((strlen(key) == (int)(end-pos)) &&
       !strncasecmp(pos, key, (int)(end-pos)))
      {
      return 1;
      }
    pos = end;
    if(*pos == '\0')
      break;
    else
      {
      while(isspace(*pos) && (*pos != '\0'))
        pos++;
      }
    }
  return 0;
  }

/* Used mostly for generating manual pages,
   it's horribly inefficient */

char * bg_toupper(const char * str)
  {
  wchar_t * tmp_string_1;
  wchar_t * tmp_string_2;
  wchar_t * pos_1;
  wchar_t * pos_2;
  char * ret;
  
  tmp_string_1 = bg_str_to_wchar(str);
  tmp_string_2 = malloc((wcslen(tmp_string_1) + 1) * sizeof(*tmp_string_2));
  
  pos_1 = tmp_string_1;
  pos_2 = tmp_string_2;

  while(*pos_1)
    {
    *pos_2 = towupper(*pos_1);
    pos_1++;
    pos_2++;
    }
  *pos_2 = 0;

  ret = bg_wchar_to_str(tmp_string_2);
  free(tmp_string_1);
  free(tmp_string_2);
  return ret;
  }

char * bg_capitalize(const char * str)
  {
  wchar_t * tmp_string_1;
  wchar_t * tmp_string_2;
  wchar_t * pos_1;
  wchar_t * pos_2;
  char * ret;
  int capitalize = 1;
  int len2;
  
  tmp_string_1 = bg_str_to_wchar(str);

  len2 = (wcslen(tmp_string_1) + 1) * sizeof(*tmp_string_2);

  /* Workaround a buggy libc implementation of wcstombs */
  len2 /= 32;
  len2++;
  len2 *= 32;
  
  tmp_string_2 = calloc(1, len2);
  
  pos_1 = tmp_string_1;
  pos_2 = tmp_string_2;

  while(*pos_1)
    {
    if(capitalize)
      {
      if(!iswspace(*pos_1)) // a -> A
        {
        *pos_2 = towupper(*pos_1);
        capitalize = 0;
        }
      else
        *pos_2 = *pos_1;
      }
    else if(iswspace(*pos_1))
      {
      *pos_2 = *pos_1;
      capitalize = 1;
      }
    else
      {
       // A -> a
      *pos_2 = towlower(*pos_1);
      }
    
    pos_1++;
    pos_2++;
    }
  *pos_2 = 0;
  
  ret = bg_wchar_to_str(tmp_string_2);
  free(tmp_string_1);
  free(tmp_string_2);
  return ret;
  }

void bg_get_filename_hash(const char * gml, char ret[GAVL_MD5_LENGTH])
  {
  char * uri;
  uri = bg_string_to_uri(gml, -1);
  gavl_md5_buffer_str(uri, strlen(uri), ret);
  free(uri);
  }

void bg_dprintf(const char * format, ...)
  {
  va_list argp; /* arg ptr */
  va_start( argp, format);
  vfprintf(stderr, format, argp);
  va_end(argp);
  }

void bg_diprintf(int indent, const char * format, ...)
  {
  int i;
  va_list argp; /* arg ptr */
  for(i = 0; i < indent; i++)
    bg_dprintf( " ");
  
  va_start( argp, format);
  vfprintf(stderr, format, argp);
  va_end(argp);
  }

char * bg_filename_ensure_extension(const char * filename,
                                    const char * ext)
  {
  const char * pos;

  if((pos = strrchr(filename, '.')) &&
     (!strcasecmp(pos+1, ext)))
    return gavl_strdup(filename);
  else
    return gavl_sprintf("%s.%s", filename, ext);
  }

char * bg_get_stream_label(int index, const gavl_dictionary_t * m)
  {
  char * label;
  const char * info;
  const char * language;
  
  info = gavl_dictionary_get_string(m, GAVL_META_LABEL);
  language = gavl_dictionary_get_string(m, GAVL_META_LANGUAGE);
  
  if(info && language)
    label = gavl_sprintf("%s [%s]", info, bg_get_language_name(language));
  else if(info)
    label = gavl_sprintf("%s", info);
  else if(language)
    label = gavl_sprintf(TR("Stream %d [%s]"), index+1, bg_get_language_name(language));
  else
    label = gavl_sprintf(TR("Stream %d"), index+1);
  return label;
  }

char * bg_canonical_filename(const char * name)
  {
  char * ret = realpath(name, NULL);
#if 0
  if(!ret)
    fprintf(stderr, "Cannot canonicalize filename %s: %s\n", name, strerror(errno));
#endif
  return ret;
  }

static const struct
  {
  const char * bcode;
  const char * tcode;
  }
iso639tab[] =
  {
    { "alb", "sqi" },
    { "arm", "hye" },
    { "baq", "eus" },
    { "bur", "mya" },
    { "chi", "zho" },
    { "cze", "ces" },
    { "dut", "nld" },
    { "fre", "fra" },
    { "geo", "kat" },
    { "ger", "deu" },
    { "gre", "ell" },
    { "ice", "isl" },
    { "mac", "mkd" },
    { "mao", "mri" },
    { "may", "msa" },
    { "per", "fas" },
    { "rum", "ron" },
    { "slo", "slk" },
    { "tib", "bod" },
    { "wel", "cym" },
    { /* End */    }
  };

const char * bg_iso639_b_to_t(const char * code)
  {
  int i = 0;
  while(iso639tab[i].bcode)
    {
    if(!strcmp(code, iso639tab[i].bcode))
      return iso639tab[i].tcode;
    i++;
    }
  return code;
  }

const char * bg_iso639_t_to_b(const char * code)
  {
  int i = 0;
  while(iso639tab[i].bcode)
    {
    if(!strcmp(code, iso639tab[i].tcode))
      return iso639tab[i].bcode;
    i++;
    }
  return code;
  }

wchar_t * bg_str_to_wchar(const char * str)
  {
  size_t len;
  wchar_t * ret;

  len = mbstowcs(NULL,str,0)+1;
  ret = calloc(len, sizeof(*ret));

  if(mbstowcs(ret,str,len) == (size_t)-1)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Invalid multibyte sequence in mbstowcs");
    free(ret);
    return NULL;
    }
  ret[len-1] = 0;
  return ret;
  }

char * bg_wchar_to_str(const wchar_t * wstr)
  {
  size_t len;
  char * ret;

  len = wcstombs(NULL,wstr,0)+1;
  ret = calloc(len, sizeof(*ret));
  if(wcstombs(ret,wstr,len-1) == (size_t)-1)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Invalid multibyte sequence in wcstombs");
    free(ret);
    return NULL;
    }
  return ret;
  }

static char * strip_space(char * str, int do_free)
  {
  char * pos = str;

  while(isspace(*pos) && *pos != '\0')
    pos++;

  if((*pos == '\0'))
    {
    if(do_free)
      {
      free(str);
      return NULL;
      }
    else
      {
      *str = '\0';
      return str;
      }
    }
  
  if(pos > str)
    memmove(str, pos, strlen(pos)+1);

  pos = str + (strlen(str)-1);
  while(isspace(*pos))
    pos--;

  pos++;
  *pos = '\0';
  return str;
  }


char * bg_strip_space(char * str)
  {
  return strip_space(str, 1);
  }

/* very basic mimetype support */

bg_mimetype_t bg_mimetypes[] =
  {
    /* Application */
    { "ttf",  "application/x-font-ttf", "TrueType"       },
    /* Text */
    { "html", "text/html",              "HTML", (const char*[]){ "htm", NULL }    },
    { "js",   "text/javascript",        "Javascript"     },
    { "css",  "text/css",               "CSS Stylesheet" },
    { "srt",  "application/x-subrip",   "SubRip subtitle" },
    /* audio */
    { "pls",  "audio/x-scpls",           "PLS", NULL, BG_MIME_SUPPORTS_MULTITRACK  },
    { "m3u",  "audio/x-mpegurl",         "M3U", NULL, BG_MIME_SUPPORTS_MULTITRACK  },
    { "m3u",  "application/mpegurl",     "M3U", NULL, BG_MIME_SUPPORTS_MULTITRACK  },
    { "m3u8", "application/x-mpegURL",   "M3U8", NULL, BG_MIME_SUPPORTS_MULTITRACK },
    { "mp3",  "audio/mpeg",              "MP3"            },
    { "aac",  "audio/aacp",              "AAC+"         },
    { "aac",  "audio/aac",               "AAC"            },
    { "flac", "audio/flac",              "Flac"           },
    { "ogg",  "audio/ogg",               "Ogg", NULL, BG_MIME_SUPPORTS_MULTITRACK  },
    { "ogg",  "audio/ogg; codecs=vorbis", "Vorbis"        },
    { "ogg",  "audio/ogg; codecs=flac",   "Flac"          },
    { "gavf",  "audio/gavf",              "GAVF"          },
    { "wav",   "audio/wav",               "WAV"           },
    { "wv",    "audio/x-wavpack",         "WV"           },

    /* Image */
    { "png",  "image/png",              "PNG"            },
    { "jpg",  "image/jpeg",             "JPG", (const char*[]){ "jpeg", NULL }  },
    { "gif",  "image/gif",              "GIF"            },
    { "svg",  "image/svg+xml",          "SVG"            },
    { "ico",  "image/vnd.microsoft.icon", "ICO"          },
    { "ico",  "image/x-icon",             "ICO"          },
    { "ico",  "image/icon",               "ICO"          },
    { "tif",  "image/tiff",               "TIFF", (const char*[]){ "tiff", NULL } },

    /* Video */
    { "ts",   "video/MP2P",             "TS"             },
    { "mp4",  "video/mp4",              "MP4"            },
    { "mpg",  "video/mpeg",             "MPG"            },
    { "mov",  "video/quicktime",        "MOV"            },
    { "flv",  "video/x-flv",            "FLV"            },
    { "mkv",  "video/x-matroska",       "MKV"            },
    { "avi",  "video/x-msvideo",        "AVI"            },
    { "gavf", "video/gavf",             "GAVF"           },
    { BG_IMGLIST_EXT, BG_IMGLIST_MIMETYPE, "Image list"  },
    /* Other stuff */
    { "xspf", "application/xspf+xml",  "XSPF", NULL, BG_MIME_SUPPORTS_MULTITRACK      },
    { "nfo",  "text/x-nfo",            "NFO"             },
    { "cue",  "application/x-cue",     "Cuesheet", NULL, BG_MIME_SUPPORTS_MULTITRACK  },
    { /* End */ },
  };


const char * bg_mimetype_to_ext(const char * mimetype)
  {
  int i = 0;
  while(bg_mimetypes[i].mimetype)
    {
    if(!strcmp(bg_mimetypes[i].mimetype, mimetype))
      return bg_mimetypes[i].ext;
    i++;
    }
  return NULL;
  }

const char * bg_mimetype_to_name(const char * mimetype)
  {
  int i = 0;
  while(bg_mimetypes[i].mimetype)
    {
    if(!strcmp(bg_mimetypes[i].mimetype, mimetype))
      return bg_mimetypes[i].name;
    i++;
    }
  return NULL;
  }

static bg_mimetype_t * mimetype_from_extension(const char * ext)
  {
  int i = 0;
  while(bg_mimetypes[i].ext)
    {
    if(!strcasecmp(bg_mimetypes[i].ext, ext))
      return &bg_mimetypes[i];

    if(bg_mimetypes[i].other_extensions)
      {
      int j = 0;
      while(bg_mimetypes[i].other_extensions[j])
        {
        if(!strcasecmp(bg_mimetypes[i].other_extensions[j], ext))
          return &bg_mimetypes[i];
        j++;
        }
      }

    i++;
    }
  return NULL;
  }


const char * bg_ext_to_mimetype(const char * ext)
  {
  bg_mimetype_t * t;

  if((t = mimetype_from_extension(ext)))
    return t->mimetype;
  else
    return NULL;
  }

int bg_file_supports_multitrack(const char * path)
  {
  bg_mimetype_t * t;
  const char * ext = strrchr(path, '.');

  if(!ext)
    return 0;
  ext++;
  
  if((t = mimetype_from_extension(ext)))
    return !!(t->flags & BG_MIME_SUPPORTS_MULTITRACK);
  else
    return 0;
  }

const char * bg_url_to_mimetype(const char * url)
  {
  const char * ret = NULL;
  char * pos;
  char * ext;
  
  if((pos = strrchr(url, '.')))
    ext = gavl_strdup(pos+1);
  else
    return NULL;
  
  if((pos = strchr(ext, '?')))
    *pos = '\0';

  ret = bg_ext_to_mimetype(ext);

  free(ext);
  return ret;
  }




char * bg_get_chapter_label(int index, int64_t t, int scale, const char * name)
  {
  char time_string[GAVL_TIME_STRING_LEN];

  gavl_time_prettyprint(gavl_time_unscale(scale, t),
                        time_string);
  
  if(name)
    return gavl_sprintf("%s [%s]", name, time_string);
  else
    return gavl_sprintf(TR("Chapter %d [%s]"), index+1, time_string);

  }


static const char * search_string_skip[] =
  {
    "a ",
    "the ",
    "der ",
    "die ",
    "das ",
    "ein ",
    "eine ",
    "le ",
    "les ",
    "la ",
    "l'",
    "os ",
    "'", // 'Round Midnight
    NULL
  };


const char * bg_get_search_string(const char * str)
  {
  int i, len;
  i = 0;
  
  while(search_string_skip[i])
    {
    len = strlen(search_string_skip[i]);
    if(!strncasecmp(str, search_string_skip[i], len))
      return str + len;
    i++;
    }
  return str;
  }

/* Create UUID from URI */

char * bg_uri_to_uuid(const char * uri, char ret[37])
  {
  uuid_t ns;
  uuid_t uuid;

  uuid_parse("4c7a190f-4472-40ae-b801-f085e56dbcb8", ns);
  
  uuid_generate_md5(uuid, ns, uri, strlen(uri));
  uuid_unparse(uuid, ret);
  return ret;
  }


/* Radiobrowser URIs */

#define RB_PROTOCOL "gmerlin-radiobrowser:///"

static pthread_mutex_t rb_server_mutex = PTHREAD_MUTEX_INITIALIZER;
static char * rb_server = NULL;

static int check_rb_server(const char * host)
  {
  int ret = 0;
  gavl_buffer_t buf;
  char * uri;

  /* Try do download the list of codecs */
  
  gavl_buffer_init(&buf);
  uri = gavl_sprintf("https://%s/json/codecs", host);

  ret = bg_http_get(uri, &buf, NULL);

  free(uri);
  gavl_buffer_free(&buf);
  return ret;
  }

static char * lookup_rb_server()
  {
  int i = 0;
  char * ret;
  gavl_socket_address_t ** addr;
  
  /* Full DNS lookup */
  addr = gavl_lookup_hostname_full("all.api.radio-browser.info", SOCK_STREAM);
  if(!addr)
    return NULL;
  
  /* Reverse DNS lookups */
  while(addr[i])
    {
    if((ret = gavl_socket_address_get_hostname(addr[i])) &&
       check_rb_server(ret))
      break;
    else
      ret = NULL;
    i++;
    }
  
  /* Free */
  gavl_socket_address_free_array(addr);
  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Looked up radiobrowser server: %s", ret);
  
  return ret;
  }

static const char * get_rb_server()
  {
  /* TODO: Re-read server every hour or so */
  if(!rb_server)
    rb_server = lookup_rb_server();
  return rb_server;
  }

char *bg_get_rb_server()
  {
  char * ret = NULL;
  pthread_mutex_lock(&rb_server_mutex);
  ret = gavl_strdup(get_rb_server());
  pthread_mutex_unlock(&rb_server_mutex);
  return ret;
  }

char * bg_rb_make_uri(const char * station_uuid)
  {
  return gavl_sprintf(RB_PROTOCOL"%s", station_uuid);
  }

int bg_rb_check_uri(const char * uri)
  {
  return gavl_string_starts_with(uri, RB_PROTOCOL);
  }

char * bg_rb_resolve_uri(const char * uri)
  {
  char * ret;
  const char * srv;
  
  if(!bg_rb_check_uri(uri))
    return NULL;

  uri += strlen(RB_PROTOCOL);
  
  pthread_mutex_lock(&rb_server_mutex);

  //  https://de1.api.radio-browser.info/m3u/url/492a6362-66d5-11ea-be63-52543be04c81

  if(!(srv = get_rb_server()))
    return NULL;
  
  ret = gavl_sprintf("https://%s/m3u/url/%s", srv, uri);
  
  pthread_mutex_unlock(&rb_server_mutex);
  return ret;
  }


