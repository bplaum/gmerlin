/*****************************************************************
 * Gmerlin - a general purpose multimedia framework and applications
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



#include <sys/utsname.h>
#include <string.h>

#include <config.h>


#include <gavl/gavl.h>
#include <gavl/utils.h>
#include <gavl/metatags.h>
#include <gavl/log.h>
#define LOG_DOMAIN "upnputils"

#include <gmerlin/upnp/devicedesc.h>
#include <gmerlin/upnp/upnputils.h>
#include <gmerlin/upnp/soap.h>
#include <gmerlin/mdb.h>
#include <gmerlin/utils.h>
#include <gmerlin/http.h>

char * bg_upnp_id_from_upnp(const char * id)
  {
  if(!strcmp(id, "0"))
    return gavl_strdup("/");
  else
    return gavl_strdup(id);
  }

char * bg_upnp_id_to_upnp(const char * id)
  {
  if(!strcmp(id, "/"))
    return gavl_strdup("0");
  else
    return gavl_strdup(id);
  }

char * bg_upnp_parent_id_to_upnp(const char * id)
  {
  char * ret;
  
  if(!strcmp(id, "/"))
    return gavl_strdup("-1");
  
  ret = bg_mdb_get_parent_id(id);
  if(!strcmp(ret, "/"))
    {
    free(ret);
    return gavl_strdup("0");
    }
  return ret;
  }

char * bg_upnp_make_server_string(void)
  {
  struct utsname os_info;
  uname(&os_info);
  return bg_sprintf("%s/%s UPnP/1.0 "PACKAGE"/"VERSION,
                    os_info.sysname, os_info.release);
  }

void bg_upnp_send_description(bg_http_connection_t * conn,
                              const char * desc1)
  {
  int len;
  int result;

  len = strlen(desc1);
  bg_http_connection_init_res(conn, "HTTP/1.1", 200, "OK");
  gavl_dictionary_set_int(&conn->res, "Content-Length", len);
  gavl_dictionary_set_string(&conn->res, "Content-Type", "text/xml; charset=UTF-8");
  gavl_dictionary_set_string_nocopy(&conn->res, "Server", bg_upnp_make_server_string());
  bg_http_header_set_date(&conn->res, "Date");

#if 0
  fprintf(stderr, "Send description\n");
  fprintf(stderr, "Req:\n");
  gavl_dictionary_dump(&conn->req, 2);
  fprintf(stderr, "Res:\n");
  gavl_dictionary_dump(&conn->res, 2);
  fprintf(stderr, "%s\n", desc1);
#endif
  
  if(!bg_http_connection_write_res(conn))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Cannot send upnp description: Writing response failed");
    return;
    }
  if(strcmp(conn->method, "HEAD"))
    {
    if((result = gavl_socket_write_data(conn->fd, (const uint8_t*)desc1, len)) < len)
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Cannot send upnp description: Sent %d of %d bytes", result, len);
      return;
      }
    }
  bg_http_connection_check_keepalive(conn);
  }

void bg_upnp_finish_soap_request(gavl_dictionary_t * soap,
                                 bg_http_connection_t * conn,
                                 bg_http_server_t *srv)
  {
  bg_soap_request_write_res(soap, conn);
  bg_http_server_put_connection(srv, conn);
  bg_http_connection_free(conn);
  bg_http_connection_init(conn);
  }

char * bg_upnp_create_icon_list(const gavl_array_t * arr)
  {
  int i;
  char * ret = NULL;
  const gavl_dictionary_t * icon;
  char * tmp_string;
  int w, h, depth;
  const char * uri;
  
  const char * mimetype;
  
  if(!arr || !arr->num_entries)
    return NULL;
  
  for(i = 0; i < arr->num_entries; i++)
    {
    if((icon = gavl_value_get_dictionary(&arr->entries[i])) &&
       (uri = gavl_dictionary_get_string(icon, GAVL_META_URI)) &&
       gavl_dictionary_get_int(icon, GAVL_META_WIDTH, &w) &&
       gavl_dictionary_get_int(icon, GAVL_META_HEIGHT, &h) &&
       (mimetype = gavl_dictionary_get_string(icon, GAVL_META_MIMETYPE)))
      {
      if(!strcmp(mimetype, "image/png"))
        depth = 32;
      else
        depth = 24;

      
      tmp_string = bg_sprintf("      <icon>"
                              "<mimetype>%s</mimetype>"
                              "<width>%d</width>"
                              "<height>%d</height>"
                              "<depth>%d</depth>"
                              "<url>%s</url></icon>\n", mimetype, w, h, depth, uri);
      
      if(!ret)
        ret = gavl_strdup("    <iconList>\n");

      ret = gavl_strcat(ret, tmp_string);
      free(tmp_string);
      }
    }

  if(ret)
    ret = gavl_strcat(ret, "    </iconList>\n");
  
  return ret;
  }

int bg_upnp_parse_bool(const char * str)
  {
  if(str && (!strcmp(str, "1") || !strcasecmp(str, "yes") || !strcasecmp(str, "true")))
    return 1;
  else
    return 0;
  }

/* DLNA */

// DLNA flags

#define DLNA_SenderPacedFlag      (1<<31)
#define DLNA_lop_npt              (1<<30)
#define DLNA_lop_bytes            (1<<29)
#define DLNA_playcontainer_param  (1<<28)
#define DLNA_s_0_Increasing       (1<<27)
#define DLNA_s_N_Increasing       (1<<26)
#define DLNA_rtsp_pause           (1<<25)
#define DLNA_tm_s                 (1<<24)
#define DLNA_tm_i                 (1<<23)
#define DLNA_tm_b                 (1<<22)
#define DLNA_http_stalling        (1<<21)
#define DLNA_1_5_version_flag     (1<<20)

char * bg_get_dlna_content_features(const gavl_dictionary_t * track,
                                    const gavl_dictionary_t * uri,
                                    int can_seek_http, int can_seek_dlna)
  {
  uint32_t flags;
  
  const char * klass;

  const gavl_dictionary_t * m1 = gavl_track_get_metadata(track);

#if 0
  fprintf(stderr, "bg_get_dlna_content_features\n");
  gavl_dictionary_dump(m1, 2);
  fprintf(stderr, "uri:\n");
  gavl_dictionary_dump(uri, 2);
#endif
  
  
  if(!(klass = gavl_dictionary_get_string(m1, GAVL_META_MEDIA_CLASS)))
    return  NULL;
  
  if(gavl_string_starts_with(klass, GAVL_META_MEDIA_CLASS_IMAGE))
    {
    int width  = -1;
    int height = -1;
    const char * mimetype = NULL;
    const char * profile_id;

    if(gavl_dictionary_get_int(m1, GAVL_META_WIDTH, &width) &&
       gavl_dictionary_get_int(m1, GAVL_META_HEIGHT, &height) &&
       (mimetype = gavl_dictionary_get_string(uri, GAVL_META_MIMETYPE)) &&
       (profile_id = bg_get_dlna_image_profile(mimetype, width, height)))
      return bg_sprintf("DLNA.ORG_PN=%s", profile_id);
    else
      return NULL;
    }
  
  if(gavl_string_starts_with(klass, GAVL_META_MEDIA_CLASS_AUDIO_FILE) ||
     gavl_string_starts_with(klass, GAVL_META_MEDIA_CLASS_VIDEO_FILE))
    {
    const char * profile_id = NULL;
    const char * mimetype = NULL;
    const char * location = NULL;
    const char * format   = NULL;
    
    char * ret = NULL;
    char * tmp_string;

    int is_http_media_uri;
    
    // TimeSeekRange.dlna.org: npt=00:05:35.3-00:05:37.5 
    // X-AvailableSeekRange: 1 npt=00:05:35.3-00:05:37.5 
    
    location = gavl_dictionary_get_string(uri, GAVL_META_URI);
    mimetype = gavl_dictionary_get_string(uri, GAVL_META_MIMETYPE);
    format = gavl_dictionary_get_string(uri, GAVL_META_FORMAT);

    if(!gavl_string_starts_with(location, "http://") &&
       !gavl_string_starts_with(location, "https://"))
      return NULL;
    
    //    fprintf(stderr, "Audio mimetype %s\n", mimetype);
    
    if(mimetype)
      {
      if(!strcmp(mimetype, "audio/mpeg") &&
         format && !strcmp(format, GAVL_META_FORMAT_MP3))
        profile_id = "MP3";

      /* LPCM */
      else if(gavl_string_starts_with(mimetype, "audio/L16;"))
        profile_id = "LPCM";
      else
        return NULL;
      }
    
    /* Check if we can seek. It is only possible for the mediafile handler. */
    is_http_media_uri = bg_is_http_media_uri(location);
    
    // DLNA.ORG_PN
    
    if(profile_id)
      {
      tmp_string = bg_sprintf("DLNA.ORG_PN=%s", profile_id);

      if(ret)
        ret = gavl_strcat(ret, ";");
      
      ret = gavl_strcat(ret, tmp_string);
      free(tmp_string);
      }
   
    // DLNA.ORG_OP
    tmp_string = bg_sprintf("DLNA.ORG_OP=%d%d", can_seek_dlna, can_seek_http);
    if(ret)
      ret = gavl_strcat(ret, ";");
    ret = gavl_strcat(ret, tmp_string);
    free(tmp_string);

    // DLNA.ORG_FLAGS
    flags = 0;
    
    
    if(!strcmp(klass, GAVL_META_MEDIA_CLASS_AUDIO_BROADCAST) ||
       !strcmp(klass, GAVL_META_MEDIA_CLASS_VIDEO_BROADCAST))
      flags |= DLNA_SenderPacedFlag;

    // #define DLNA_lop_npt              (1<<30) // Limited Random Access Data Availability
    // #define DLNA_lop_bytes            (1<<29) // Limited Random Access Data Availability
    // #define DLNA_playcontainer_param  (1<<28) // DLNA PlayContainer URI

    /* Would be used only for seeking in live-streams */
    // #define DLNA_s_0_Increasing       (1<<27) // Byte start position increasing
    // #define DLNA_s_N_Increasing       (1<<26) // Byte end position increasing

    // #define DLNA_rtsp_pause           (1<<25)

    flags |= DLNA_tm_s; // Stream is fast enough for realtime rendering (Streaming Mode Transfer Flag)
    
    // #define DLNA_tm_i                 (1<<23) //  Setting the tm-i flag to true for Audio-only or AV content is expressly prohibited

    if(is_http_media_uri)
      {
      flags |= DLNA_tm_b;
      flags |= DLNA_http_stalling;
      }
    
    flags |= DLNA_1_5_version_flag;

    /* 8 hexdigits primary-flags + 24 hexdigits reserved-data (all zero) */
    tmp_string = bg_sprintf("DLNA.ORG_FLAGS=%08x000000000000000000000000", flags);
    if(ret)
      ret = gavl_strcat(ret, ";");
    ret = gavl_strcat(ret, tmp_string);
    free(tmp_string);
    
    return ret;
    }
  
  return NULL;
  }
static const struct
  {
  const char * mimetype;
  int max_width;
  int max_height;
  const char * profile;
  }
image_profiles[] =
  {
    {
       .mimetype   = "image/jpeg",
       .max_width  = 160,
       .max_height = 160,
       .profile    = "JPEG_TN",
    },
    {
       .mimetype   = "image/jpeg",
       .max_width  = 640,
       .max_height = 480,
       .profile    = "JPEG_SM",
    },
    {
       .mimetype   = "image/jpeg",
       .max_width  = 1024,
       .max_height = 768,
       .profile    = "JPEG_MED",
    },
    {
       .mimetype   = "image/jpeg",
       .max_width  = 4096,
       .max_height = 4096,
       .profile    = "JPEG_LRG",
    },
    {
       .mimetype   = "image/png",
       .max_width  = 160,
       .max_height = 160,
       .profile    = "PNG_TN",
    },
    {
       .mimetype   = "image/png",
       .max_width  = 4096,
       .max_height = 4096,
       .profile    = "PNG_LRG",
    },
    { /* */ }
  };

const char * bg_get_dlna_image_profile(const char * mimetype, int width, int height)
  {
  int i = 0;
  while(image_profiles[i].mimetype)
    {
    if(!strcmp(mimetype, image_profiles[i].mimetype) &&
       (width <= image_profiles[i].max_width) &&
       (height <= image_profiles[i].max_height))
      return image_profiles[i].profile;
    else
      i++;
    }
  return NULL;
  }
