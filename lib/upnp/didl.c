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
#include <gmerlin/upnp/upnputils.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>

#include <gavl/metatags.h>
#include <gmerlin/translation.h>
#include <gmerlin/xmlutils.h>

#include <gmerlin/utils.h>
#include <gmerlin/upnp/didl.h>


#include <gmerlin/translation.h>
#include <gmerlin/log.h>
#define LOG_DOMAIN "didl"

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

const char * bg_didl_get_dlna_image_profile(const char * mimetype, int width, int height)
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
       (profile_id = bg_didl_get_dlna_image_profile(mimetype, width, height)))
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


static void remove_ns(char * filter_tag)
  {
  char * pos;

  if((pos = strchr(filter_tag, ':')))
    {
    pos++;
    memmove(filter_tag, pos, strlen(pos)+1);
    }
  }

char ** bg_didl_create_filter(const char * Filter)
  {
  int idx;
  char ** ret;
  char *pos1;
  int len;
  
  ret = gavl_strbreak(Filter, ',');

  idx = 0;

  /* Remove namespace prefixes */
  while(ret[idx])
    {
    char * el   = NULL;
    char * attr = NULL;

    if((pos1 = strchr(ret[idx], '@')))
      {
      if(pos1 > ret[idx]) // @blabla
        el = gavl_strndup(ret[idx], pos1);
      attr = gavl_strdup(pos1+1);
      }
    else
      el = gavl_strdup(ret[idx]);

    if(el)
      remove_ns(el);
    if(attr)
      remove_ns(attr);


    len = strlen(ret[idx]);
    if(el && attr)
      snprintf(ret[idx], len, "%s@%s", el, attr);
    else if(el)
      snprintf(ret[idx], len, "%s", el);
    else if(attr)
      snprintf(ret[idx], len, "@%s", attr);
    
    idx++;
    }

  return ret;
  }



xmlDocPtr bg_didl_create(void)
  {
  xmlDocPtr doc;  
  xmlNodePtr didl;
  
  doc = xmlNewDoc((xmlChar*)"1.0");
  didl = xmlNewDocRawNode(doc, NULL, (xmlChar*)"DIDL-Lite", NULL);
  xmlDocSetRootElement(doc, didl);

  xmlNewNs(didl,
           (xmlChar*)"urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/",
           NULL);
  xmlNewNs(didl,
           (xmlChar*)"http://purl.org/dc/elements/1.1/",
           (xmlChar*)"dc");
  xmlNewNs(didl,
           (xmlChar*)"urn:schemas-upnp-org:metadata-1-0/upnp/",
           (xmlChar*)"upnp");
  
  return doc;
  }

xmlNodePtr bg_didl_add_item(xmlDocPtr doc)
  {
  xmlNsPtr ns;
  xmlNodePtr parent = bg_xml_find_next_doc_child(doc, NULL);
  xmlNodePtr node;

  ns = xmlSearchNs(doc, parent, (const xmlChar *)"ns0");
  
  node = xmlNewTextChild(parent, ns, (xmlChar*)"item", NULL);
  xmlAddChild(parent, BG_XML_NEW_TEXT("\n"));
  xmlAddChild(node, BG_XML_NEW_TEXT("\n"));
  return node;
  }

xmlNodePtr bg_didl_add_container(xmlDocPtr doc)
  {
  xmlNodePtr parent = bg_xml_find_next_doc_child(doc, NULL);
  xmlNodePtr node = xmlNewTextChild(parent, NULL, (xmlChar*)"container", NULL);
  xmlAddChild(parent, BG_XML_NEW_TEXT("\n"));
  xmlAddChild(node, BG_XML_NEW_TEXT("\n"));
  return node;
  }

xmlNodePtr bg_didl_add_element(xmlDocPtr doc,
                               xmlNodePtr node,
                               const char * name,
                               const char * value)
  {
  xmlNodePtr ret;
  char * pos;
  char buf[128];
  strncpy(buf, name, 127);
  buf[127] = '\0';

  pos = strchr(buf, ':');
  if(pos)
    {
    xmlNsPtr ns;
    *pos = '\0';
    pos++;
    ns = xmlSearchNs(doc, node, (const xmlChar *)buf);
    ret= xmlNewTextChild(node, ns, (const xmlChar*)pos,
                           (const xmlChar*)value);
    }
  else  
    ret= xmlNewTextChild(node, NULL, (const xmlChar*)name,
                           (const xmlChar*)value);
  xmlAddChild(node, BG_XML_NEW_TEXT("\n"));
  return ret;
  }

void bg_didl_set_class(xmlDocPtr doc,
                       xmlNodePtr node,
                       const char * klass)
  {
  bg_didl_add_element(doc, node, "upnp:class", klass);
  }

void bg_didl_set_title(xmlDocPtr doc,
                           xmlNodePtr node,
                           const char * title)
  {
  bg_didl_add_element(doc, node, "dc:title", title);
  }

int bg_didl_filter_element(const char * name, char ** filter)
  {
  int i = 0;
  const char * pos;
  if(!filter)
    return 1;

  while(filter[i])
    {
    if(!strcmp(filter[i], name))
      return 1;

    // res@size implies res
    if((pos = strchr(filter[i], '@')) &&
       (pos - filter[i] == strlen(name)) &&
       !strncmp(filter[i], name, pos - filter[i]))
      return 1;
     i++;
    }
  return 0;
  }

int bg_didl_filter_attribute(const char * element, const char * attribute, char ** filter)
  {
  int i = 0;
  int allow_empty = 0;
  int len = strlen(element);
  
  if(!filter)
    return 1;

  if(!strcmp(element, "item") ||
     !strcmp(element, "container"))
    allow_empty = 1;
  
  while(filter[i])
    {
    if(!strncmp(filter[i], element, len) &&
       (*(filter[i] + len) == '@') &&
       !strcmp(filter[i] + len + 1, attribute))
      return 1;

    if(allow_empty &&
       (*(filter[i]) == '@') &&
       !strcmp(filter[i] + 1, attribute))
      return 1;
      
    i++;
    }
  return 0;
  }

xmlNodePtr bg_didl_add_element_string(xmlDocPtr doc,
                                    xmlNodePtr node,
                                    const char * name,
                                    const char * content, char ** filter)
  {
  if(!bg_didl_filter_element(name, filter))
    return NULL;
  return bg_didl_add_element(doc, node, name, content);
  }

xmlNodePtr bg_didl_add_element_int(xmlDocPtr doc,
                                       xmlNodePtr node,
                                       const char * name,
                                       int64_t content, char ** filter)
  {
  char buf[128];
  if(!bg_didl_filter_element(name, filter))
    return NULL;
  snprintf(buf, 127, "%"PRId64, content);
  return bg_didl_add_element(doc, node, name, buf);
  }

/* Filtering must be done by the caller!! */
void bg_didl_set_attribute_int(xmlNodePtr node, const char * name, int64_t val)
  {
  char buf[128];
  snprintf(buf, 127, "%"PRId64, val);
  BG_XML_SET_PROP(node, name, buf);
  }

/* TO / From internal metadata */

static const struct
  {
  const char * gavl_name;
  const char * didl_name;
  }
gavl_didl[] =
  {
    {GAVL_META_TITLE,              "dc:title"    },
    {GAVL_META_ARTIST,             "upnp:artist" },
    {GAVL_META_ALBUM,              "upnp:album" },
    {GAVL_META_GENRE,              "upnp:genre" },
    {GAVL_META_TRACKNUMBER,        "upnp:originalTrackNumber" },
    {GAVL_META_ACTOR,              "upnp:actor" },
    {GAVL_META_DIRECTOR,           "upnp:director" },
    {GAVL_META_PLOT,               "upnp:longDescription" },
    {GAVL_META_DATE,               "dc:date" },
    { /* End */ }
  };

static int get_year(xmlNodePtr node)
  {
  const char * str;
  int dummy1, dummy2, ret;

  str = bg_xml_node_get_child_content(node, "date");

  if(str && (sscanf(str, "%d-%d-%d", &ret, &dummy1, &dummy2) == 3))
    return ret;
  else
    return -1;
  }

void bg_metadata_from_didl(gavl_dictionary_t * m, xmlNodePtr didl)
  {
  int i;
  xmlNodePtr child;
  const char * pos;
  const char * gavl_name;
  char * gavl_name_nc = NULL;
  char * label = NULL;
  char * var;
  
  gavl_value_t val;
  
  child = didl->children;

  if((var = BG_XML_GET_PROP(didl, "id")))
    {
    gavl_dictionary_set_string(m, GAVL_META_ID, var);
    free(var);
    }
  
  while(child)
    {
    if(child->type != XML_ELEMENT_NODE)
      {
      child = child->next;
      continue;
      }

    if(!strcmp((char*)child->name, "res"))
      {
      gavl_dictionary_t * dict = NULL;

      if(!gavl_dictionary_get_string(m, GAVL_META_APPROX_DURATION) &&
         (var = BG_XML_GET_PROP(child, "duration")))
        {
        gavl_time_t dur;
        if(gavl_time_parse(var, &dur))
          gavl_dictionary_set_long(m, GAVL_META_APPROX_DURATION, dur);
        free(var);
        }

      if((var = BG_XML_GET_PROP(child, "protocolInfo")))
        {
        char ** fields = gavl_strbreak(var, ':');
        
        if(fields && fields[0] && fields[1] && fields[2] && strchr(fields[2], '/'))
          {
          char * tmp_string;
          tmp_string = bg_uri_to_string(bg_xml_node_get_text_content(child), -1);
          
          dict = gavl_metadata_add_src(m, GAVL_META_SRC, fields[2], tmp_string);
          free(tmp_string);
          }
        gavl_strbreak_free(fields);
        free(var);
        }
            
      if(dict)
        {
        gavl_value_init(&val);
        
        if((var = BG_XML_GET_PROP(child, "sampleFrequency")))
          {
          gavl_value_set_int(&val, atoi(var)); 
          gavl_dictionary_set_nocopy(dict, GAVL_META_AUDIO_SAMPLERATE, &val);
          free(var);
          }
        if((var = BG_XML_GET_PROP(child, "nrAudioChannels")))
          {
          gavl_value_set_int(&val, atoi(var)); 
          gavl_dictionary_set_nocopy(dict, GAVL_META_AUDIO_CHANNELS, &val);
          free(var);
          }
        if((var = BG_XML_GET_PROP(child, "bitrate")))
          {
          gavl_value_set_int(&val, 8*atoi(var)); 
          gavl_dictionary_set_nocopy(dict, GAVL_META_BITRATE, &val);
          free(var);
          }
        if((var = BG_XML_GET_PROP(child, "resolution")))
          {
          int w, h;
          if(sscanf(var, "%dx%d", &w, &h) == 2)
            {
            gavl_value_set_int(&val, w);
            gavl_dictionary_set_nocopy(dict, GAVL_META_WIDTH, &val);
            
            gavl_value_set_int(&val, h);
            gavl_dictionary_set_nocopy(dict, GAVL_META_HEIGHT, &val);
            }
          
          free(var);
          }
        
        }
      
      child = child->next;
      continue;
      }
    else if(!strcmp((char*)child->name, "albumArtURI"))
      {
      const char * var;
      int w = -1;
      int h = -1;
      
      if((var = BG_XML_GET_PROP(child, "size")))
        sscanf(var, "%dx%d", &w, &h);
      
      gavl_metadata_add_image_uri(m, GAVL_META_COVER_URL, w, h, NULL,
                                  bg_xml_node_get_text_content(child));
      child = child->next;
      continue;
      }
    else if(!strcmp((char*)child->name, "class"))
      {
      const char * gmerlin_class = NULL;
      const char * upnp_class = bg_xml_node_get_text_content(child);

      if(!strncasecmp(upnp_class, "object.item.videoItem", 21))
        {
        if(!strcmp(upnp_class, "object.item.videoItem.movie"))
          {
          int year;
          const char * title = bg_xml_node_get_child_content(didl, "title");
          if((year = get_year(didl)) > 0)
            {
            label = bg_sprintf("%s (%d)", title, year);
            gmerlin_class = GAVL_META_MEDIA_CLASS_MOVIE;
            }
          else
            gmerlin_class = GAVL_META_MEDIA_CLASS_VIDEO_FILE;
          }
        else
          gmerlin_class = GAVL_META_MEDIA_CLASS_VIDEO_FILE;
        }
      else if(!strncasecmp(upnp_class, "object.item.audioItem", 21))
        {
        if(!strcmp(upnp_class, "object.item.audioItem.audioBroadcast"))
          {
          gmerlin_class = GAVL_META_MEDIA_CLASS_AUDIO_BROADCAST;
          }
        else if(!strcmp(upnp_class, "object.item.audioItem.musicTrack"))
          {
          const char * artist;
          const char * album = bg_xml_node_get_child_content(didl, "album");
          const char * title = bg_xml_node_get_child_content(didl, "title");

          if(!(artist = bg_xml_node_get_child_content(didl, "artist")))
            artist = bg_xml_node_get_child_content(didl, "creator");
        
          if(artist && title && album)
            {
            label = bg_sprintf("%s - %s", artist, title);
            gmerlin_class = GAVL_META_MEDIA_CLASS_SONG;
            }
          else
            gmerlin_class = GAVL_META_MEDIA_CLASS_AUDIO_FILE;
          }
        else
          gmerlin_class = GAVL_META_MEDIA_CLASS_AUDIO_FILE;
        }
      else if(!strncasecmp(upnp_class, "object.item.imageItem", 21))
        gmerlin_class = GAVL_META_MEDIA_CLASS_IMAGE;
      
      if(gmerlin_class)
        gavl_dictionary_set_string(m, GAVL_META_MEDIA_CLASS, gmerlin_class);
      
      child = child->next;
      continue;
      }
    
    gavl_name = NULL;

    //    fprintf(stderr, "name: %s\n", child->name);
    
    i = 0;
    while(gavl_didl[i].gavl_name)
      {
      if((pos = strchr(gavl_didl[i].didl_name, ':')))
        pos++;
      else
        pos = gavl_didl[i].didl_name;

      if(!strcmp(pos, (char*)child->name))
        {
        gavl_name = gavl_didl[i].gavl_name;
        break;
        }
      i++;
      }
    
    if(!gavl_name)
      {
      if(!child->ns)
        {
        child = child->next;
        continue;
        }
      
      /* WARNING: This assumes standard prefixes */
      gavl_name_nc = bg_sprintf("%s:%s", child->ns->prefix, child->name);
      gavl_name = gavl_name_nc;
      }
    
    gavl_metadata_append(m, gavl_name, bg_xml_node_get_text_content(child));
    
    if(gavl_name_nc)
      {
      free(gavl_name_nc);
      gavl_name_nc = NULL;
      }

    child = child->next;
    }

  if(!label)
    label = gavl_strdup(bg_xml_node_get_child_content(didl, "title"));
  if(!label)
    label = gavl_strdup(TR("Unnamed item"));

  gavl_dictionary_set_string_nocopy(m, GAVL_META_LABEL, label);

  /* If we just loaded a movie, move image res entries from src to poster uri */

  if((gavl_name = gavl_dictionary_get_string(m, GAVL_META_MEDIA_CLASS)) &&
     gavl_string_starts_with(gavl_name, "item.video"))
    {
    const gavl_dictionary_t * img;
    int idx = 0;
    const char * mimetype;
    
    while(1)
      {
      if(!(img = gavl_metadata_get_src(m, GAVL_META_SRC, idx,
                                       &mimetype, NULL)))
        break;

      if(gavl_string_starts_with(mimetype, "image/"))
        {
        gavl_value_t child_val;
        gavl_dictionary_t * child;

        gavl_value_init(&child_val);
        child = gavl_value_set_dictionary(&child_val);
        gavl_dictionary_copy(child, img);
        gavl_dictionary_append_nocopy(m, GAVL_META_POSTER_URL, &child_val);
        gavl_dictionary_delete_item(m, GAVL_META_SRC, idx);
        }
      else
        idx++;
      }
    
    
    }
    
  
  }

static const struct
  {
  const char * gavl_name;
  const char * upnp_name;
  }
class_names[] =
  {
    { GAVL_META_MEDIA_CLASS_AUDIO_FILE,         "object.item.audioItem" },
    { GAVL_META_MEDIA_CLASS_VIDEO_FILE,         "object.item.videoItem" },
    { GAVL_META_MEDIA_CLASS_SONG,               "object.item.audioItem.musicTrack" },
    { GAVL_META_MEDIA_CLASS_MOVIE,              "object.item.videoItem.movie" },
    { GAVL_META_MEDIA_CLASS_TV_EPISODE,         "object.item.videoItem.movie" },
    { GAVL_META_MEDIA_CLASS_AUDIO_BROADCAST,    "object.item.audioItem.audioBroadcast" },
    { GAVL_META_MEDIA_CLASS_VIDEO_BROADCAST,    "object.item.videoItem.videoBroadcast" },
    { GAVL_META_MEDIA_CLASS_IMAGE,              "object.item.imageItem" },
        
    /* Container values */
    { GAVL_META_MEDIA_CLASS_CONTAINER,          "object.container" },
    { GAVL_META_MEDIA_CLASS_MUSICALBUM,         "object.container.album.musicAlbum" },
    { GAVL_META_MEDIA_CLASS_PLAYLIST,           "object.container.playlistContainer" },
    //    { GAVL_META_MEDIA_CLASS_CONTAINER_ACTOR,    "object.container" },
    //    { GAVL_META_MEDIA_CLASS_CONTAINER_DIRECTOR, "object.container" },
    { GAVL_META_MEDIA_CLASS_CONTAINER_ARTIST,   "object.container.person.musicArtist" },
    //    { GAVL_META_MEDIA_CLASS_CONTAINER_COUNTRY,  "object.container" },
    { GAVL_META_MEDIA_CLASS_CONTAINER_GENRE,    "object.container.genre" },
    //    { GAVL_META_MEDIA_CLASS_TV_SEASON,          "object.container" },
    //    { GAVL_META_MEDIA_CLASS_TV_SHOW,            "object.container" },
    { GAVL_META_MEDIA_CLASS_DIRECTORY,          "object.container.storageFolder" },
        
    /* Root Containers */
    //    { GAVL_META_MEDIA_CLASS_ROOT,               "object.container" },
    //    { GAVL_META_MEDIA_CLASS_ROOT_MUSICALBUMS,   "object.container" },
    //    { GAVL_META_MEDIA_CLASS_ROOT_SONGS,         "object.container" },
    //    { GAVL_META_MEDIA_CLASS_ROOT_PLAYLISTS,     "object.container" },
    //    { GAVL_META_MEDIA_CLASS_ROOT_MOVIES,        "object.container" },
    //    { GAVL_META_MEDIA_CLASS_ROOT_TV_SHOWS,      "object.container" },
    //    { GAVL_META_MEDIA_CLASS_ROOT_WEBRADIO,      "object.container" },
    //    { GAVL_META_MEDIA_CLASS_ROOT_DIRECTORIES,   "object.container" },
    //    { GAVL_META_MEDIA_CLASS_ROOT_PHOTOS,        "object.container" },
    //    { GAVL_META_MEDIA_CLASS_ROOT_LIBRARY,       "object.container" },
    //    { GAVL_META_MEDIA_CLASS_ROOT_INCOMING,      "object.container" },
    { /* End */ }
  };

static const char * get_class_name(const gavl_dictionary_t * dict)
  {
  int i;
  const char * gavl_class;

  if(!(gavl_class = gavl_dictionary_get_string(dict, GAVL_META_MEDIA_CLASS)))
    return NULL;

  i = 0;
  
  while(class_names[i].gavl_name)
    {
    if(!strcmp(class_names[i].gavl_name, gavl_class))
      return class_names[i].upnp_name;
    i++;
    }

  if(gavl_string_starts_with(gavl_class, "container"))
    return "object.container";
  
  return NULL;
  }

xmlNodePtr bg_track_to_didl(xmlDocPtr ret, const gavl_dictionary_t * track, char ** filter)
  {
  int i;
  const char * id;
  const char * var;
  const char * klass;
  char * tmp_string;
  xmlNodePtr node;
  const gavl_value_t * val;
  int tracknumber;
  
  const char * location;
  const char * mimetype;
  char * uri_encoded;

  const gavl_dictionary_t * src;
  
  const gavl_dictionary_t * image_uri;

  const gavl_dictionary_t * m;


  if(!track)
    return NULL;

  m = gavl_track_get_metadata(track);

  if(!m)
    return NULL;
  
  //  fprintf(stderr, "bg_track_to_didl\n");
  //  gavl_dictionary_dump(track, 2);
  
  if(!(klass = get_class_name(m)))
    {
    //    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Cannot convert to DIDL: class missing");
    
    //    fprintf(stderr, "Cannot convert to DIDL: class missing\n");
    //    gavl_dictionary_dump(m, 2);
    
    return NULL;
    }
  
  if(gavl_string_starts_with(klass, "object.container"))
    node = bg_didl_add_container(ret);
  else
    node = bg_didl_add_item(ret);

  /* Required properties */
  
  bg_didl_add_element(ret, node, "upnp:class", klass);
  
  if((id = gavl_dictionary_get_string(m, GAVL_META_ID)))
    BG_XML_SET_PROP(node, "id", id);
  else
    BG_XML_SET_PROP(node, "id", "item_id");
  
  tmp_string = bg_upnp_parent_id_to_upnp(id);
  BG_XML_SET_PROP(node, "parentID", tmp_string);
  free(tmp_string);
  
  BG_XML_SET_PROP(node, "restricted", "1");

  if((var = gavl_dictionary_get_string(m, GAVL_META_TITLE)))
    bg_didl_add_element(ret, node, "dc:title", var);
  else if((var = gavl_dictionary_get_string(m, GAVL_META_LABEL)))
    bg_didl_add_element(ret, node, "dc:title", var);

  if(bg_didl_filter_attribute("container", "childCount", filter) &&
     gavl_string_starts_with(klass, "object.container"))
    {
    int num_children = 0;

    gavl_dictionary_get_int(m, GAVL_META_NUM_CHILDREN, &num_children);

    tmp_string = bg_sprintf("%d", num_children);
    BG_XML_SET_PROP(node, "childCount", tmp_string);
    free(tmp_string);
    }

  /* Artist */
  if(bg_didl_filter_element("artist", filter) &&
     gavl_dictionary_get_item(m, GAVL_META_ARTIST, 0))
    {
    int i = 0;
    while((val = gavl_dictionary_get_item(m, GAVL_META_ARTIST, i)) &&
          (var = gavl_value_get_string(val)))
      {
      bg_didl_add_element(ret, node, "upnp:artist", var);
      i++;
      }
    }

  /* Genre */
  if(bg_didl_filter_element("genre", filter) &&
     gavl_dictionary_get_item(m, GAVL_META_GENRE, 0))
    {
    i = 0;
    while((val = gavl_dictionary_get_item(m, GAVL_META_GENRE, i)) &&
          (var = gavl_value_get_string(val)))
      {
      bg_didl_add_element(ret, node, "upnp:genre", var);
      i++;
      }
    }

  /* Director */
  if(bg_didl_filter_element("director", filter) &&
     gavl_dictionary_get_item(m, GAVL_META_DIRECTOR, 0))
    {
    i = 0;
    while((val = gavl_dictionary_get_item(m, GAVL_META_DIRECTOR, i)) &&
          (var = gavl_value_get_string(val)))
      {
      bg_didl_add_element(ret, node, "upnp:director", var);
      i++;
      }
    }

  /* Actor */
  if(bg_didl_filter_element("actor", filter) &&
     gavl_dictionary_get_item(m, GAVL_META_ACTOR, 0))
    {
    i = 0;
    while((val = gavl_dictionary_get_item(m, GAVL_META_ACTOR, i)) &&
          (var = gavl_value_get_string(val)))
      {
      bg_didl_add_element(ret, node, "upnp:actor", var);
      i++;
      }
    }

  /* Album  */
  if(bg_didl_filter_element("album", filter) &&
     (var = gavl_dictionary_get_string(m, GAVL_META_ALBUM)))
    {
    bg_didl_add_element(ret, node, "upnp:album", var);
    }

  /* Tracknumber */
  tracknumber = 0;

  if(bg_didl_filter_element("originalTrackNumber", filter) &&
     gavl_dictionary_get_int(m, GAVL_META_TRACKNUMBER, &tracknumber) &&
     (tracknumber > 0))
    {
    char * tmp_string = bg_sprintf("%d", tracknumber);
    bg_didl_add_element(ret, node, "upnp:originalTrackNumber", tmp_string);
    free(tmp_string);
    }
  
  /* Plot  */
  if(bg_didl_filter_element("longDescription", filter) &&
     (var = gavl_dictionary_get_string(m, GAVL_META_PLOT)))
    {
    bg_didl_add_element(ret, node, "upnp:longDescription", var);
    }

  /* res */

  i = 0;

  
  
  while((src = gavl_metadata_get_src(m, GAVL_META_SRC, i, &mimetype, &location)))
    {
    int can_seek_http = 0;
    gavl_time_t dur;
    xmlNodePtr child; 
    char * protocol_info = NULL;

    if(location && (gavl_string_starts_with(location, "/") || gavl_string_starts_with(location, "file://")))
      {
      can_seek_http = 1;
      }
    
    if(location && mimetype && (gavl_string_starts_with(location, "http://") ||
                                gavl_string_starts_with(location, "https://")))
      {
      char * content_features;
      
      content_features = bg_get_dlna_content_features(track, src, can_seek_http, 0);
      
      if(content_features)
        {
        protocol_info = bg_sprintf("http-get:*:%s:%s", mimetype, content_features);
        free(content_features);
        }
      else
        protocol_info = bg_sprintf("http-get:*:%s:*", mimetype);
      }
    
    if(!protocol_info)
      {
      i++;
      continue;
      }

    uri_encoded = bg_string_to_uri(location, -1);
    
    child = bg_xml_append_child_node(node, "res", uri_encoded);
    BG_XML_SET_PROP(child, "protocolInfo", protocol_info);

    //    fprintf(stderr, "Protocol info: %s\n", protocol_info);
    
    if(bg_didl_filter_attribute("res", "duration", filter) &&
       gavl_dictionary_get_long(m, GAVL_META_APPROX_DURATION, &dur) &&
       (dur > 0))
      {
      char buf[GAVL_TIME_STRING_LEN_MS];
      gavl_time_prettyprint_ms(dur, buf);
      BG_XML_SET_PROP(child, "duration", buf);
      }
    /* TODO: Bitrate, samplerate, channels, resolution */
    free(uri_encoded);
    free(protocol_info);
    i++;
    }
  
  /* Images */

  if(bg_didl_filter_element("albumArtURI", filter))
    {
    if((image_uri = gavl_dictionary_get_image_max_proto(m, GAVL_META_COVER_URL,
                                                        600, 600, "image/jpeg", "http")))
      {
      xmlNodePtr child; 
      uri_encoded = bg_string_to_uri(gavl_dictionary_get_string(image_uri, GAVL_META_URI), -1);
      child = bg_xml_append_child_node(node, "upnp:albumArtURI", uri_encoded);

      //      fprintf(stderr, "Image URI: %s\n", gavl_dictionary_get_string(image_uri, GAVL_META_URI));

      free(uri_encoded);

      if(bg_didl_filter_attribute("albumArtURI", "profileID", filter))
        {
        const char * mimetype;
        const char * dlna_id;
        int w = 0, h = 0;

        if(gavl_dictionary_get_int(image_uri, GAVL_META_WIDTH, &w) &&
           gavl_dictionary_get_int(image_uri, GAVL_META_HEIGHT, &h) &&
           (mimetype = gavl_dictionary_get_string(image_uri, GAVL_META_MIMETYPE)) &&
           (dlna_id = bg_didl_get_dlna_image_profile(mimetype, w, h)))
          {
          xmlNsPtr dlna_ns;
          dlna_ns = xmlNewNs(child,
                             (xmlChar*)"urn:schemas-dlna-org:metadata-1-0/",
                             (xmlChar*)"dlna");
          xmlSetNsProp(child, dlna_ns, (const xmlChar*)"profileID",
                       (const xmlChar*)dlna_id);
          }
        }
      
      }
    else if((var = gavl_dictionary_get_string(m, GAVL_META_LOGO_URL)))
      {
      uri_encoded = bg_string_to_uri(var, -1);
      bg_xml_append_child_node(node, "upnp:albumArtURI", uri_encoded);
      free(uri_encoded);
      }
    }
  
  
  return node;
  }

