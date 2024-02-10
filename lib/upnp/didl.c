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


int bg_didl_filter_element(const char * name, char ** filter)
  {
  int i = 0;
  const char * pos;
  if(!filter)
    return 1;

  if((pos = strchr(name, ':')))
    name = pos+1;
  
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


/* Class names */

static const struct
  {
  const char * gavl_class;
  const char * didl_class;
  }
class_names[] =
  {
    { GAVL_META_CLASS_AUDIO_FILE,         "object.item.audioItem" },
    { GAVL_META_CLASS_VIDEO_FILE,         "object.item.videoItem" },
    { GAVL_META_CLASS_SONG,               "object.item.audioItem.musicTrack" },
    { GAVL_META_CLASS_MOVIE,              "object.item.videoItem.movie" },
    { GAVL_META_CLASS_TV_EPISODE,         "object.item.videoItem.movie" },
    { GAVL_META_CLASS_AUDIO_BROADCAST,    "object.item.audioItem.audioBroadcast" },
    { GAVL_META_CLASS_VIDEO_BROADCAST,    "object.item.videoItem.videoBroadcast" },
    { GAVL_META_CLASS_IMAGE,              "object.item.imageItem" },
    
    
    /* Container values */
    { GAVL_META_CLASS_CONTAINER,          "object.container" },
    { GAVL_META_CLASS_MUSICALBUM,         "object.container.album.musicAlbum" },
    { GAVL_META_CLASS_PLAYLIST,           "object.container.playlistContainer" },
    //    { GAVL_META_CLASS_CONTAINER_ACTOR,    "object.container" },
    //    { GAVL_META_CLASS_CONTAINER_DIRECTOR, "object.container" },
    { GAVL_META_CLASS_CONTAINER_ARTIST,   "object.container.person.musicArtist" },
    //    { GAVL_META_CLASS_CONTAINER_COUNTRY,  "object.container" },
    { GAVL_META_CLASS_CONTAINER_GENRE,    "object.container.genre" },
    //    { GAVL_META_CLASS_TV_SEASON,          "object.container" },
    //    { GAVL_META_CLASS_TV_SHOW,            "object.container" },
    { GAVL_META_CLASS_DIRECTORY,          "object.container.storageFolder" },
        
    /* Root Containers */
    //    { GAVL_META_CLASS_ROOT,               "object.container" },
    //    { GAVL_META_CLASS_ROOT_MUSICALBUMS,   "object.container" },
    //    { GAVL_META_CLASS_ROOT_SONGS,         "object.container" },
    //    { GAVL_META_CLASS_ROOT_PLAYLISTS,     "object.container" },
    //    { GAVL_META_CLASS_ROOT_MOVIES,        "object.container" },
    //    { GAVL_META_CLASS_ROOT_TV_SHOWS,      "object.container" },
    //    { GAVL_META_CLASS_ROOT_WEBRADIO,      "object.container" },
    //    { GAVL_META_CLASS_ROOT_DIRECTORIES,   "object.container" },
    //    { GAVL_META_CLASS_ROOT_PHOTOS,        "object.container" },
    //    { GAVL_META_CLASS_ROOT_LIBRARY,       "object.container" },
    //    { GAVL_META_CLASS_ROOT_INCOMING,      "object.container" },
    { /* End */ }
  };

static const char * class_gavl_to_didl(const char * gavl_class)
  {
  int i = 0;
  
  while(class_names[i].gavl_class)
    {
    if(!strcmp(class_names[i].gavl_class, gavl_class))
      return class_names[i].didl_class;
    i++;
    }

  if(gavl_string_starts_with(gavl_class, "container"))
    return "object.container";
  else if(gavl_string_starts_with(gavl_class, GAVL_META_CLASS_AUDIO_FILE))
    return "object.item.audioItem";
  else if(gavl_string_starts_with(gavl_class, GAVL_META_CLASS_VIDEO_FILE))
    return "object.item.videoItem";
  else
    return "object.item";
  }

static const char * class_didl_to_gavl(const char * didl_class)
  {
  int i = 0;
  
  while(class_names[i].gavl_class)
    {
    if(!strcmp(class_names[i].didl_class, didl_class))
      return class_names[i].gavl_class;
    i++;
    }

  if(gavl_string_starts_with(didl_class, "object.container"))
    return GAVL_META_CLASS_CONTAINER;
  else if(gavl_string_starts_with(didl_class, "object.item.audioItem"))
    return GAVL_META_CLASS_AUDIO_FILE;
  else if(gavl_string_starts_with(didl_class, "object.item.videoItem"))
    return GAVL_META_CLASS_VIDEO_FILE;
  else if(gavl_string_starts_with(didl_class, "object.item.imageItem"))
    return GAVL_META_CLASS_IMAGE;
  
  return NULL;
  
  }

/* TO / From internal metadata */

#define FLAG_IS_INTEGER    (1<<0)
#define FLAG_IS_MULTI_DIDL (1<<1)
#define FLAG_IS_MULTI_GAVL (1<<2)
#define FLAG_IS_URI        (1<<3) // didl URIs need to be escaped
#define FLAG_REQUIRED      (1<<4) // Will never be filtered

typedef struct
  {
  const char * gavl_name;
  const char * didl_name;
  int flags;
  } name_tab_t;

static const name_tab_t
gavl_didl_names[] =
  {
    // http://upnp.org/specs/av/UPnP-av-ContentDirectory-v4-Service.pdf
    {GAVL_META_ARTIST,             "upnp:artist",   },
    {GAVL_META_ARTIST,             "dc:creator",  FLAG_IS_MULTI_DIDL|FLAG_IS_MULTI_GAVL   },
    {GAVL_META_ACTOR,              "upnp:actor",  FLAG_IS_MULTI_DIDL|FLAG_IS_MULTI_GAVL   },
    {GAVL_META_AUTHOR,             "upnp:author", FLAG_IS_MULTI_DIDL|FLAG_IS_MULTI_GAVL   },
    {GAVL_META_DIRECTOR,           "upnp:director", FLAG_IS_MULTI_DIDL|FLAG_IS_MULTI_GAVL },

    {GAVL_META_GENRE,              "upnp:genre", FLAG_IS_MULTI_DIDL|FLAG_IS_MULTI_GAVL    },
    {GAVL_META_ALBUM,              "upnp:album", FLAG_IS_MULTI_DIDL },
    
    {GAVL_META_TITLE,              "dc:title",    FLAG_REQUIRED },
    {GAVL_META_TRACKNUMBER,        "upnp:originalTrackNumber", FLAG_IS_INTEGER },
    {GAVL_META_PLOT,               "upnp:longDescription",  },
    {GAVL_META_DATE,               "dc:date" },
    { /* End */ }
  };


static const name_tab_t * name_by_gavl(const name_tab_t * arr, const char * gavl_name)
  {
  int idx = 0;

  while(arr[idx].gavl_name)
    {
    if(!strcmp(arr[idx].gavl_name, gavl_name))
      return &arr[idx];
    idx++;
    }
  return NULL;
  }

static const name_tab_t * name_by_didl(const name_tab_t * arr, const char * didl_name)
  {
  int idx = 0;
  const char * pos;
  
  while(arr[idx].gavl_name)
    {
    if(!(pos = strchr(arr[idx].didl_name, ':')))
      pos = arr[idx].didl_name;
    else
      pos++;
    
    if(!strcmp(pos, didl_name))
      return &arr[idx];
    idx++;
    }
  return NULL;
  }
  
static int node_didl_to_gavl(const xmlNodePtr node,
                             gavl_dictionary_t * metadata)
  {
  const char * val;
  const name_tab_t * tab;

  if(!(tab = name_by_didl(gavl_didl_names, (const char*)node->name)))
    return 0;

  if(!(val = bg_xml_node_get_text_content(node)))
    return 1;
  
  if(tab->flags & FLAG_IS_MULTI_GAVL)
    gavl_metadata_append(metadata, tab->gavl_name, val);
  else if(tab->flags & FLAG_IS_INTEGER)
    gavl_dictionary_set_int(metadata, tab->gavl_name, atoi(val));
  else
    gavl_dictionary_set_string(metadata, tab->gavl_name, val);
  return 1;
  }

typedef struct 
  {
  xmlDocPtr doc;
  xmlNodePtr node;
  char ** filter;
  
  } gavl_to_didl_t;

// gavl_dictionary_foreach_func
static void node_gavl_to_didl(void * priv, const char * name,
                              const gavl_value_t * val)
  {
  const gavl_value_t * item_val;
  const char * item;
  const name_tab_t * tab;
  
  gavl_to_didl_t * d = priv;

  if(!(tab = name_by_gavl(gavl_didl_names, name)))
    return;
  
  if(!bg_didl_filter_element(tab->didl_name, d->filter))
    return;
  
  if(tab->flags & FLAG_IS_INTEGER)
    {
    int val_i = 0;

    if(gavl_value_get_int(val, &val_i))
      {
      char * tmp_string = gavl_sprintf("%d", val_i);
      bg_didl_add_element(d->doc, d->node, tab->didl_name, tmp_string);
      free(tmp_string);
      }
    }
  else if(tab->flags & FLAG_IS_MULTI_DIDL)
    {
    int i = 0;
    
    while((item_val = gavl_value_get_item(val, i)) &&
          (item = gavl_value_get_string(val)))
      {
      bg_didl_add_element(d->doc, d->node, tab->didl_name, item);
      i++;
      }
        
    }
  else
    {
    if((item_val = gavl_value_get_item(val, 0)) &&
       (item = gavl_value_get_string(val)))
      bg_didl_add_element(d->doc, d->node, tab->didl_name, item);
    }
  return;
  }

/* res translation */

static const name_tab_t
res_attrs[] =
  {
    {GAVL_META_BITRATE,            "bitrate",          FLAG_IS_INTEGER },
    {GAVL_META_AUDIO_SAMPLERATE,   "sampleFrequency ", FLAG_IS_INTEGER },
    {GAVL_META_AUDIO_CHANNELS,     "nrAudioChannels",  FLAG_IS_INTEGER },
    { /* End */ }
  };

static void res_to_src(const xmlNodePtr node, gavl_dictionary_t * src)
  {
  int idx = 0;
  char * var;
  while(res_attrs[idx].didl_name)
    {
    if((var = BG_XML_GET_PROP(node, res_attrs[idx].didl_name)))
      {
      if(res_attrs[idx].flags & FLAG_IS_INTEGER)
        gavl_dictionary_set_int(src, res_attrs[idx].gavl_name, atoi(var));
      free(var);
      }
    idx++;
    }

  if((var = BG_XML_GET_PROP(node, "resolution")))
    {
    int w, h;
    if(sscanf(var, "%dx%d", &w, &h) == 2)
      {
      gavl_dictionary_set_int(src, GAVL_META_WIDTH, w);
      gavl_dictionary_set_int(src, GAVL_META_HEIGHT, h);
      }
    free(var);
    }
  }

typedef struct
  {
  xmlNodePtr node;
  char ** filter;
  } src_to_res_t;

static void src_to_res_func(void * priv, const char * name,
                            const gavl_value_t * val)
  {
  src_to_res_t * data = priv;

  const name_tab_t * tab;

  if(!(tab = name_by_gavl(res_attrs, name)))
    return;
  
  if(!bg_didl_filter_attribute("res", tab->didl_name, data->filter))
    return;

  if(tab->flags & FLAG_IS_INTEGER)
    {
    int i = 0;

    if(gavl_value_get_int(val, &i))
      {
      char * tmp_string;
      tmp_string = gavl_sprintf("%d", i);
      BG_XML_SET_PROP(data->node, tab->didl_name, tmp_string);
      free(tmp_string);
      }
    }
  
  }

static void src_to_res(const gavl_dictionary_t * src, xmlNodePtr node, char ** filter)
  {
  src_to_res_t data;
  int w, h;
  
  data.node = node;
  data.filter = filter;
  
  gavl_dictionary_foreach(src, src_to_res_func, &data);

  if(bg_didl_filter_attribute("res", "resolution", filter) &&
     gavl_dictionary_get_int(src, GAVL_META_WIDTH, &w) &&
     gavl_dictionary_get_int(src, GAVL_META_HEIGHT, &h))
    {
    char * tmp_string;
    tmp_string = gavl_sprintf("%dx%d", w, h);
    BG_XML_SET_PROP(data.node, "resolution", tmp_string);
    free(tmp_string);
    }
  
  }


void bg_track_from_didl(gavl_dictionary_t * track, xmlNodePtr didl)
  {
  xmlNodePtr child;
  const char * gavl_name;
  char * var;
  int have_hash = 0;
  
  gavl_dictionary_t * m = gavl_dictionary_get_dictionary_create(track, GAVL_META_METADATA);
  
  child = didl->children;

  if((var = BG_XML_GET_PROP(didl, "id")))
    {
    gavl_dictionary_set_string(m, GAVL_META_ID, var);
    free(var);
    }

  if((var = BG_XML_GET_PROP(didl, "childCount")))
    {
    int num_children;
    int num_container_children = -1;
    int num_item_children = -1;

    num_children = atoi(var);
    free(var);

    if((var = BG_XML_GET_PROP(didl, "childContainerCount")))
      {
      num_container_children = atoi(var);
      num_item_children = num_children - num_container_children;
      free(var);
      }
    else
      {
      /* Ugly but should be harmless */
      num_container_children = num_children;
      num_item_children = num_children;
      }
    
    gavl_dictionary_set_int(m, GAVL_META_NUM_CONTAINER_CHILDREN, num_container_children);
    gavl_dictionary_set_int(m, GAVL_META_NUM_ITEM_CHILDREN,      num_item_children);
    gavl_dictionary_set_int(m, GAVL_META_NUM_CHILDREN,           num_children);
    }
  
  while(child)
    {
    if(child->type != XML_ELEMENT_NODE)
      {
      child = child->next;
      continue;
      }

    if(node_didl_to_gavl(child, m))
      {
      child = child->next;
      continue;
      }
    
    if(!strcmp((char*)child->name, "res"))
      {
      gavl_dictionary_t * dict = NULL;

      if(!gavl_dictionary_get(m, GAVL_META_APPROX_DURATION) &&
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

          if(!have_hash)
            {
            char hash[GAVL_MD5_LENGTH];
            gavl_md5_buffer_str(tmp_string, strlen(tmp_string), hash);
            gavl_dictionary_set_string(m, GAVL_META_HASH, hash);
            have_hash = 1;
            }
          
          free(tmp_string);
          }
        gavl_strbreak_free(fields);
        free(var);
        }
            
      if(dict)
        res_to_src(child, dict);
      
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
      const char * gavl_class;
      const char * upnp_class;
      
      if((upnp_class = bg_xml_node_get_text_content(child)) &&
         (gavl_class = class_didl_to_gavl(upnp_class)))
        gavl_dictionary_set_string(m, GAVL_META_CLASS, gavl_class);
      
      child = child->next;
      continue;
      }
    
    child = child->next;

    }

  gavl_track_set_label(track);
  
  /* If we just loaded a movie, move image res entries from src to poster uri */

  if((gavl_name = gavl_dictionary_get_string(m, GAVL_META_CLASS)) &&
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

xmlNodePtr bg_track_to_didl(xmlDocPtr ret, const gavl_dictionary_t * track, char ** filter)
  {
  int i;
  const char * id;
  const char * var;
  const char * gavl_class;
  const char * didl_class;
  char * tmp_string;
  
  const char * location;
  const char * mimetype;
  char * uri_encoded;

  const gavl_dictionary_t * src;
  
  const gavl_dictionary_t * image_uri;

  const gavl_dictionary_t * m;

  gavl_to_didl_t data;
  data.doc = ret;
  data.filter = filter;
  
  if(!track)
    return NULL;

  m = gavl_track_get_metadata(track);

  if(!m)
    return NULL;
  
  //  fprintf(stderr, "bg_track_to_didl\n");
  //  gavl_dictionary_dump(track, 2);

  if(!(gavl_class = gavl_dictionary_get_string(m, GAVL_META_CLASS)) ||
     !(didl_class = class_gavl_to_didl(gavl_class)))
    return NULL;
    
  if(gavl_string_starts_with(didl_class, "object.container"))
    data.node = bg_didl_add_container(ret);
  else
    data.node = bg_didl_add_item(ret);

  /* Required properties */
  
  bg_didl_add_element(ret, data.node, "upnp:class", didl_class);
  
  if((id = gavl_dictionary_get_string(m, GAVL_META_ID)))
    BG_XML_SET_PROP(data.node, "id", id);
  else
    BG_XML_SET_PROP(data.node, "id", "item_id");
  
  tmp_string = bg_upnp_parent_id_to_upnp(id);
  BG_XML_SET_PROP(data.node, "parentID", tmp_string);
  free(tmp_string);
  
  BG_XML_SET_PROP(data.node, "restricted", "1");

  if((var = gavl_dictionary_get_string(m, GAVL_META_TITLE)))
    bg_didl_add_element(ret, data.node, "dc:title", var);
  else if((var = gavl_dictionary_get_string(m, GAVL_META_LABEL)))
    bg_didl_add_element(ret, data.node, "dc:title", var);

  if(bg_didl_filter_attribute("container", "childCount", filter) &&
     gavl_string_starts_with(didl_class, "object.container"))
    {
    int num_children = 0;
    
    gavl_dictionary_get_int(m, GAVL_META_NUM_CHILDREN, &num_children);

    tmp_string = bg_sprintf("%d", num_children);
    BG_XML_SET_PROP(data.node, "childCount", tmp_string);
    free(tmp_string);
    }

  if(bg_didl_filter_attribute("container", "childContainerCount", filter) &&
     gavl_string_starts_with(didl_class, "object.container"))
    {
    int num_children = 0;
    
    gavl_dictionary_get_int(m, GAVL_META_NUM_CONTAINER_CHILDREN, &num_children);
    
    tmp_string = bg_sprintf("%d", num_children);
    BG_XML_SET_PROP(data.node, "childContainerCount", tmp_string);
    free(tmp_string);
    }

  gavl_dictionary_foreach(m, node_gavl_to_didl, &data);
  
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
    
    child = bg_xml_append_child_node(data.node, "res", uri_encoded);
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
    /* Bitrate, samplerate, channels, resolution */
    src_to_res(src, child, filter);

    free(uri_encoded);
    free(protocol_info);
    i++;
    }
  
  /* Images */

  if(bg_didl_filter_element("albumArtURI", filter))
    {
    if((image_uri = gavl_dictionary_get_image_max_proto(m, GAVL_META_COVER_URL,
                                                        600, 600, "image/jpeg", "http")) ||
       (image_uri = gavl_dictionary_get_image_max_proto(m, GAVL_META_POSTER_URL,
                                                        600, 600, "image/jpeg", "http")))
      {
      xmlNodePtr child; 
      uri_encoded = bg_string_to_uri(gavl_dictionary_get_string(image_uri, GAVL_META_URI), -1);
      child = bg_xml_append_child_node(data.node, "upnp:albumArtURI", uri_encoded);

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
           (dlna_id = bg_get_dlna_image_profile(mimetype, w, h)))
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
      bg_xml_append_child_node(data.node, "upnp:albumArtURI", uri_encoded);
      free(uri_encoded);
      }
    }
  
  
  return data.node;
  }

