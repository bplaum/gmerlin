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

#define _GNU_SOURCE

#include <time.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <glob.h>
#include <errno.h>

#include <gmerlin/mdb.h>
#include <gmerlin/utils.h>
#include <md5.h>
#include <gmerlin/bggavl.h>
#include <gmerlin/http.h>

#include <mdb_private.h>
#include <gmerlin/xmlutils.h>
#include <gmerlin/log.h>

#include <gavl/metatags.h>

#define LOG_DOMAIN "mdb.podcasts"

#include <libxml/HTMLparser.h>

/* Directory structure

   podcasts/subscriptions:               Array with just the URIs of the channels
   podcasts/index:                       Children of the root folder (array)
   podcasts/<md5>:                       Items of the channel (array)
   podcasts/saved/<md5>                 Saved items of that channel
   podcasts/saved/<md5>/index:           Metadata of all saved entries
   podcasts/saved/<md5>/<md5>-media.mp3: Media
   podcasts/saved/<md5>/<md5>-image.jpg: Image

   ID:
   /podcasts/<podcast_id>/<episode_id>
   /podcasts/<podcast_id>/saved/<episode_id>
   
 */

typedef struct
  {
  /* This contains just the URLs */
  gavl_array_t subscriptions;
  
  gavl_dictionary_t * root;
  
  char * dir;

  gavl_timer_t * timer;
  gavl_time_t last_update_time;
  gavl_time_t last_full_update_time; // Forced update every 24 hours

  int exported;
  
  } podcasts_t;

typedef struct
  {
  char * podcast_id;
  char * episode_id;
  int saved;
  const char * id;
  } parsed_id_t;

static int check_update(bg_mdb_backend_t * b);


static int parse_id(const char * ctx_id, parsed_id_t * ret)
  {
  int result = 0;
  int idx = 0;
  char ** str;

  ret->id = ctx_id;
  
  ctx_id++; // '/'

  str = gavl_strbreak(ctx_id, '/');

  if(!str)
    goto end;
    
  if(!str[idx])
    goto end;

  
  result = 1;
  
  idx++;
  
  if(!str[idx])
    goto end; // /podcasts
  
  ret->podcast_id = gavl_strdup(str[idx]);
  idx++;

  if(!str[idx])
    goto end; // /podcasts/<md5>
  
  if(!strcmp(str[idx], "saved"))
    {
    idx++;
    ret->saved = 1;
    }
  
  if(!str[idx])
    goto end; // /podcasts/<md5>/saved

  ret->episode_id = gavl_strdup(str[idx]);

  
  
  end:
  if(str)
    gavl_strbreak_free(str);
  return result;
  }

static void init_id(parsed_id_t * id)
  {
  memset(id, 0, sizeof(*id));
  }

static void free_id(parsed_id_t * id)
  {
  if(id->podcast_id)
    free(id->podcast_id);
  if(id->episode_id)
    free(id->episode_id);
  }

static void destroy_podcasts(bg_mdb_backend_t * b)
  {
  podcasts_t * p = b->priv;
  gavl_array_free(&p->subscriptions);
  
  if(p->dir)
    free(p->dir);
  
  free(b->priv);
  }

static int ping_podcasts(bg_mdb_backend_t * be)
  {
  int ret = 0;
  podcasts_t * p = be->priv;

  if(!p->exported)
    {
    char * dir = gavl_sprintf("%s/saved", p->dir);
    p->exported = 1;
    bg_mdb_export_media_directory(be->ctrl.evt_sink, dir);
    free(dir);
    ret++;
    }
  
  return check_update(be);
  }

static const char * months[] =
  {
   "Jan",
   "Feb",
   "Mar",
   "Apr",
   "May",
   "Jun",
   "Jul",
   "Aug",
   "Sep",
   "Oct",
   "Nov",
   "Dec"
  };

static char * parse_time(const char * pubDate)
  {
  char * ret = NULL;
  char ** substr;

  int month;
  int year;
  int day;

  substr = gavl_strbreak(pubDate, ' ');

  if(!substr ||
     !substr[0] ||
     !substr[1] ||
     !substr[2] ||
     !substr[3] ||
     !substr[4])
    goto fail;

  day = atoi(substr[1]);

  if((day > 31) || (day < 1))
    goto fail;
    
  for(month = 0; month < 12; month++)
    {
    if(!strcmp(months[month], substr[2]))
      break;
    }

  if(month == 12)
    goto fail;

  month++;
  
  year = atoi(substr[3]);
  
  ret = bg_sprintf("%04d-%02d-%02d %s", year, month, day,  substr[4]);
  
  fail:

  if(substr)
    gavl_strbreak_free(substr);

  return ret;
  
  }

/* Replace non-escaped "&" characters in podcast descriptions */
static int is_stray_ampersand(const char * pos)
  {
  const char * semicolon;

  pos++; // Skip '&'
  
  if(!(semicolon = strchr(pos, ';')))
    return 1;
  
  if(isalpha(*pos))  // &amp;
    {
    while(pos < semicolon)
      {
      if(!isalpha(*pos))
        return 1;
      pos++;
      }
    return 0;
    }
  else if(*pos == '#')   // &#38;
    {
    pos++;
    while(pos < semicolon)
      {
      if(!isdigit(*pos))
        return 1;
      pos++;
      }
    return 0;
    }
  else
    return 1;
  
  }

static char * fix_cdata(const char * str)
  {
  const char * end;
  const char * start = str;
  char * ret = NULL;

  while(1)
    {
    end = start;
    while((*end != '\0') && (*end != '&'))
      end++;

    if(end > start)
      ret = gavl_strncat(ret, start, end);

    if(*end == '\0')
      break;

    else if(*end == '&')
      {
      if(is_stray_ampersand(end))
        ret = gavl_strcat(ret, "&amp;");
      else
        ret = gavl_strncat(ret, end, end+1);
      start = end+1;
      }
    
    }
  return ret;
  }

static void parse_cdata_description(const char * str1, gavl_dictionary_t * m)
  {
  char * desc = NULL;
  htmlNodePtr child;
  htmlDocPtr doc;
  
  char * str = fix_cdata(str1);
  
  doc = htmlReadMemory(str, strlen(str), 
                                  NULL, "utf-8", 
                                  HTML_PARSE_NODEFDTD | HTML_PARSE_RECOVER);
  
  if(!doc)
    goto fail;
  
  //  child = bg_xml_find_doc_child(doc, )  
  child = doc->children;

  if(!(child = bg_xml_find_doc_child(doc, "html")) ||
     !(child = bg_xml_find_node_child(child, "body")) ||
     !(child = child->children))
    return;

  while(child)
    {
    if(child->type == XML_ELEMENT_NODE)
      {
      if(!strcmp((const char*)child->name, "img"))
        {
        //        fprintf(stderr, "Got image: %s\n", BG_XML_GET_PROP(child, "src"));
        //        gavl_dictionary_set_string(m, GAVL_META_LOGO_URL, BG_XML_GET_PROP(child, "src"));

        gavl_metadata_add_src(m, GAVL_META_POSTER_URL, NULL, BG_XML_GET_PROP(child, "src"));
        
        }
      else if(!strcmp((const char*)child->name, "br"))
        {
        desc = gavl_strcat(desc, "\n");
        }
      else if(!strcmp((const char*)child->name, "p"))
        {
        desc = gavl_strcat(desc, "\n\n");
        }
      
      }
    else if(child->type == XML_TEXT_NODE)
      {
      desc = gavl_strcat(desc, (const char*)child->content);
      }
    
    child = child->next;
    }

  if(desc)
    {
    gavl_dictionary_set_string(m, GAVL_META_DESCRIPTION,
                               gavl_strip_space(desc));
    }
  
  fail:
  free(str);
  if(doc)
    xmlFreeDoc(doc);
  }

static int has_saved_episodes(bg_mdb_backend_t * b, const char * podcast)
  {
  int ret = 0;
  char * path;
  podcasts_t * p = b->priv;

  path = gavl_sprintf("%s/saved/%s/index", p->dir, podcast);

  if(!access(path, R_OK))
    ret = 1;

  free(path);
  return ret;
  }

static int get_num_saved_episodes(bg_mdb_backend_t * b, const char * podcast)
  {
  gavl_array_t arr;
  char * path;
  int ret = 0;
  podcasts_t * p = b->priv;

  path = gavl_sprintf("%s/saved/%s/index", p->dir, podcast);
  
  if(access(path, R_OK))
    {
    free(path);
    return 0;
    }

  gavl_array_init(&arr);
  bg_array_load_xml(&arr, path, "items");
  ret = arr.num_entries;
  gavl_array_free(&arr);
  
  free(path);
  return ret;
  }


static int load_items(bg_mdb_backend_t * b, const char * uri, char * md5,
                      gavl_dictionary_t * channel, gavl_array_t * items)
  {
  int ret = 0;
  int idx = 0;
  xmlNodePtr image;
  gavl_dictionary_t * channel_m;
  gavl_dictionary_t * mdb_dict;
  int num_containers = 0;
  const char * category;
  const char * str;
  const char * podcast;

  xmlNodePtr node;
  xmlNodePtr item;
  xmlNodePtr child;

  xmlDocPtr doc = bg_xml_from_url(uri, NULL);
  
  //  podcasts_t * priv = b->priv;
  
  if(!doc)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Could not load %s", uri);
    return 0;
    }
  //  fprintf(stderr, "Loaded %s\n", uri);

  if(!(node = bg_xml_find_doc_child(doc, "rss")) ||
     !(node = bg_xml_find_node_child(node, "channel")))
    {
    goto fail;
    }
  
  if(!(podcast = bg_xml_node_get_child_content(node, "title")))
    goto fail;

  channel_m = gavl_dictionary_get_dictionary_create(channel, GAVL_META_METADATA);
  mdb_dict = gavl_dictionary_get_dictionary_create(channel, BG_MDB_DICT);
  gavl_dictionary_set_string(mdb_dict, GAVL_META_URI, uri);
  
  gavl_dictionary_set_string(channel_m, GAVL_META_TITLE, podcast);
  gavl_dictionary_set_string(channel_m, GAVL_META_LABEL, podcast);
  gavl_dictionary_set_string(channel_m, GAVL_META_CLASS, GAVL_META_CLASS_PODCAST);
  
  if((str = bg_xml_node_get_child_content(node, "pubDate")))
    {
    char * time_str = parse_time(str);
    
    if(time_str)
      gavl_dictionary_set_string_nocopy(channel_m, GAVL_META_DATE, time_str);
    }

  if((str = bg_xml_node_get_child_content(node, "lastBuildDate")))
    {
    char * time_str = parse_time(str);
    
    if(time_str)
      {
      if(!gavl_dictionary_get_string(channel_m, GAVL_META_DATE))
        gavl_dictionary_set_string(channel_m, GAVL_META_DATE, time_str);
      
      gavl_dictionary_set_string_nocopy(channel_m, GAVL_META_DATE_MODIFY, time_str);
      }
    
    }
  
  if((image = bg_xml_find_node_child(node, "image")))
    {
    xmlNodePtr url = bg_xml_find_node_child(image, "url");
    if(url && (str = bg_xml_node_get_text_content(url)))
      {
      gavl_metadata_add_src(channel_m, GAVL_META_POSTER_URL, NULL, str);
      }
    else if((str = BG_XML_GET_PROP(image, "href")))
      {
      //      gavl_dictionary_set_string(channel_m, GAVL_META_LOGO_URL, str);
      gavl_metadata_add_src(channel_m, GAVL_META_POSTER_URL, NULL, str);
      }
    
    }
  
  gavl_dictionary_set_string_nocopy(channel_m,
                                    GAVL_META_ID,
                                    bg_sprintf("%s/%s", BG_MDB_ID_PODCASTS, md5));
  
  item = NULL;

  if((str = bg_xml_node_get_child_content(node, "description")))
    {
    gavl_dictionary_set_string(channel_m, GAVL_META_DESCRIPTION, str);
    }

  if((category = bg_xml_node_get_child_content(node, "category")))
    gavl_dictionary_set_string(channel_m, GAVL_META_GENRE, category);
  
  while((item = bg_xml_find_next_node_child_by_name(node, item, "item")))
    {
    gavl_value_t it_val;
    gavl_dictionary_t * it;
    gavl_dictionary_t * it_m;
    xmlNodePtr enc;
    const char * url;
    const char * mimetype;
    
    if(!(str = bg_xml_node_get_child_content(item, "title")))
      continue;
    
    if(!(enc = bg_xml_find_node_child(item, "enclosure")))
      continue;
    
    gavl_value_init(&it_val);
    it = gavl_value_set_dictionary(&it_val);
    it_m = gavl_dictionary_get_dictionary_create(it, GAVL_META_METADATA);
    
    gavl_dictionary_set_string(it_m, GAVL_META_TITLE, str);
    gavl_dictionary_set_string(it_m, GAVL_META_LABEL, str);
    gavl_dictionary_set_string(it_m, GAVL_META_PODCAST, podcast);
    
    if(category)
      gavl_dictionary_set_string(it_m, GAVL_META_GENRE, category);
    
    if((str = bg_xml_node_get_child_content(item, "author")))
      gavl_dictionary_set_string(it_m, GAVL_META_AUTHOR, str);

    if((str = bg_xml_node_get_child_content(item, "duration")))
      {
      gavl_time_t duration;

      if(gavl_time_parse(str, &duration))
        gavl_dictionary_set_long(it_m, GAVL_META_APPROX_DURATION, duration);
      }

    if((child = bg_xml_find_next_node_child_by_name(item, NULL, "description")) ||
       (child = bg_xml_find_next_node_child_by_name(item, NULL, "summary")))
      {
      if((str = bg_xml_node_get_text_content(child)))
        gavl_dictionary_set_string(it_m, GAVL_META_DESCRIPTION, str);
      else
        {
        //   fprintf(stderr, "Got Node %s, type %d: %s\n", child->name, child->type, child->content);

        child = child->children;

        while(child)
          {
          if(child->type == XML_CDATA_SECTION_NODE)
            {
            parse_cdata_description((const char*)child->content, it_m);
            //            fprintf(stderr, "Got CDATA %s\n", child->content);
            }
          child = child->next;
          }
        }
      }
    
    
    if((str = bg_xml_node_get_child_content(item, "pubDate")))
      {
      char * time_str = parse_time(str);

      if(time_str)
        gavl_dictionary_set_string_nocopy(it_m, GAVL_META_DATE, time_str);
      }

    if((image = bg_xml_find_node_child(item, "image")))
      {
      xmlNodePtr url = bg_xml_find_node_child(image, "url");
      if(url && (str = bg_xml_node_get_text_content(url)))
        {
        gavl_metadata_add_src(it_m, GAVL_META_POSTER_URL, NULL, str);
        }
      else if((str = BG_XML_GET_PROP(image, "href")))
        {
        gavl_metadata_add_src(it_m, GAVL_META_POSTER_URL, NULL, str);
        }
      }

    if(!gavl_dictionary_get(it_m, GAVL_META_POSTER_URL))
      gavl_dictionary_set(it_m, GAVL_META_POSTER_URL, gavl_dictionary_get(channel_m, GAVL_META_POSTER_URL));
    
    url = BG_XML_GET_PROP(enc, "url");
    mimetype = BG_XML_GET_PROP(enc, "type");

    if(url && mimetype)
      {
      char url_md5[33];
      
      bg_get_filename_hash(url, url_md5);
      
      if(gavl_string_starts_with(mimetype, "audio/"))
        {
        gavl_dictionary_set_string(it_m, GAVL_META_CLASS, GAVL_META_CLASS_AUDIO_PODCAST_EPISODE);
        }
      else if(gavl_string_starts_with(mimetype, "video/"))
        {
        gavl_dictionary_set_string(it_m, GAVL_META_CLASS, GAVL_META_CLASS_VIDEO_PODCAST_EPISODE);
        }
      gavl_metadata_add_src(it_m, GAVL_META_SRC, mimetype, url);

      gavl_dictionary_set_string_nocopy(it_m, GAVL_META_ID, bg_sprintf("%s/%s/%s", BG_MDB_ID_PODCASTS, md5, url_md5));
      
      }
    else
      {
      gavl_dictionary_set_string(it_m, GAVL_META_CLASS, GAVL_META_CLASS_ITEM);
      gavl_dictionary_set_string_nocopy(it_m, GAVL_META_ID, bg_sprintf("%s/%s/%d", BG_MDB_ID_PODCASTS, md5, idx));
      }
    
    //    fprintf(stderr, "Parsed Item:\n");
    //    gavl_dictionary_dump(it, 2);
    
    gavl_array_splice_val_nocopy(items, -1, 0, &it_val);
    
    
    
    idx++;

    }

  if(has_saved_episodes(b, md5))
    num_containers = 1;
  
  gavl_track_set_num_children(channel, num_containers, items->num_entries);
  
  ret = 1;
  
  fail:
  
    
  xmlFreeDoc(doc);
  return ret;
  }


static void load_subscriptions(bg_mdb_backend_t * b)
  {
  char * filename;
  podcasts_t * priv = b->priv;
  filename = bg_sprintf("%s/%s", priv->dir, "subscriptions");
    
  if(!access(filename, R_OK))
    bg_array_load_xml(&priv->subscriptions,
                      filename, "subscriptions");
  
  free(filename);
  }

static void save_subscriptions(bg_mdb_backend_t * b)
  {
  char * filename;
  podcasts_t * priv = b->priv;
  
  filename = bg_sprintf("%s/%s", priv->dir, "subscriptions");
  
  bg_array_save_xml(&priv->subscriptions,
                    filename, "subscriptions");
  free(filename);
  }

static int subscribe(bg_mdb_backend_t * b, int i, const gavl_value_t * val, gavl_array_t * root_arr)
  {
  gavl_value_t channel_val;
  gavl_dictionary_t * channel;
  char md5[33];
  char * filename;
  gavl_array_t items;
  //  gavl_msg_t * evt;
  podcasts_t * p = b->priv;
  int ret = 0;
  //  fprintf(stderr, "Subscribe %s\n", uri);

  const char * uri = NULL;
  const char * klass;
  const gavl_dictionary_t * dict;

  if(!(dict = gavl_value_get_dictionary(val)) ||
     !(dict = gavl_track_get_metadata(dict)) ||
     !(klass = gavl_dictionary_get_string(dict, GAVL_META_CLASS)) ||
     strcmp(klass, GAVL_META_CLASS_LOCATION) ||
     !gavl_metadata_get_src(dict, GAVL_META_SRC, 0, NULL, &uri) ||
     !uri)
    {
    return ret;
    }
  
  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Subscribing to %s", uri);
  
  /* Check if we have this already */
  if(gavl_string_array_indexof(&p->subscriptions, uri) >= 0)
    {
    gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Already subscribed to %s", uri);
    return ret;
    }
  
  gavl_value_init(&channel_val);
  channel = gavl_value_set_dictionary(&channel_val);
  gavl_array_init(&items);
  
  bg_get_filename_hash(uri, md5);
  
  if(load_items(b, uri, md5, channel, &items))
    {
    
    gavl_string_array_insert_at(&p->subscriptions, i, uri);
    save_subscriptions(b);
    
    /* Save items */
    filename = bg_sprintf("%s/%s", p->dir, md5);
    bg_array_save_xml(&items, filename, "items");
    free(filename);

    gavl_array_splice_val_nocopy(root_arr, i, 0, &channel_val);
    ret = 1;
    }
  else
    {
    gavl_value_free(&channel_val);
    }

  gavl_array_free(&items);
  return ret;
  }

static void unsubscribe(bg_mdb_backend_t * b, int idx, gavl_array_t * index)
  {
  char * filename;
  char md5[33];
  podcasts_t * p = b->priv;

  bg_get_filename_hash(gavl_string_array_get(&p->subscriptions, idx), md5);
  
  filename = bg_sprintf("%s/%s", p->dir, md5);
  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Deleting %s", filename);
  
  remove(filename);
  free(filename);
  
  gavl_array_splice_val(index, idx, 1, NULL);
  gavl_array_splice_val(&p->subscriptions, idx, 1, NULL);
  }

#define TIME_MISSING   0
#define TIME_EQUAL     1
#define TIME_DIFFERENT 2

static int check_update(bg_mdb_backend_t * b)
  {
  int i;
  podcasts_t * p = b->priv;
  char * filename;
  gavl_array_t idx;
  //  gavl_array_t items;
  gavl_time_t current_time;
  
  int ret = 0;
  char md5[33];

  int num_changed = 0;
  int full = 0;
  
  current_time = gavl_timer_get(p->timer);
  
  if((p->last_update_time != GAVL_TIME_UNDEFINED) &&
     (current_time - p->last_update_time < (gavl_time_t)GAVL_TIME_SCALE * 3600))
    return 0;

  if((p->last_full_update_time == GAVL_TIME_UNDEFINED) ||
     ((current_time - p->last_full_update_time >= (gavl_time_t)GAVL_TIME_SCALE * 3600 * 24)))
    {
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Doing full update");
    full = 1;
    }
  gavl_array_init(&idx);
  filename = bg_sprintf("%s/index", p->dir);
  if(!access(filename, R_OK))
    bg_array_load_xml(&idx, filename, "items");
  free(filename);
  
  /* Load feeds */
  for(i = 0; i < p->subscriptions.num_entries; i++)
    {
    int pub_time = TIME_MISSING;
    int build_time = TIME_MISSING;
    
    const char * uri;
    
    const char * str;
    const char * str_new;
    const gavl_dictionary_t * m;
    const gavl_dictionary_t * m_new;

    gavl_dictionary_t * channel;
    
    gavl_array_t items_new;
    gavl_dictionary_t channel_new;

    gavl_msg_t * evt;
    int old_num = 0;
    
    gavl_array_init(&items_new);
    gavl_dictionary_init(&channel_new);

    if(i >= idx.num_entries)
      break;
    
    if(!(channel = gavl_value_get_dictionary_nc(&idx.entries[i])) ||
       !(m = gavl_dictionary_get_dictionary_create(channel, GAVL_META_METADATA)))
      continue;

    uri = gavl_string_array_get(&p->subscriptions, i);

    bg_get_filename_hash(uri, md5);
    
    if(!load_items(b, uri, md5, &channel_new, &items_new))
      {
      gavl_dictionary_reset(&channel_new);
      gavl_array_reset(&items_new);
      continue;
      }
    
    m_new = gavl_track_get_metadata(&channel_new);
    
    if(!full)
      {
      if((str = gavl_dictionary_get_string(m, GAVL_META_DATE)) &&
         (str_new = gavl_dictionary_get_string(m_new, GAVL_META_DATE)))
        {
        if(!strcmp(str, str_new))
          pub_time = TIME_EQUAL;
        else
          pub_time = TIME_DIFFERENT;
        }

      if((str = gavl_dictionary_get_string(m, GAVL_META_DATE_MODIFY)) &&
         (str_new = gavl_dictionary_get_string(m_new, GAVL_META_DATE_MODIFY)))
        {
        if(!strcmp(str, str_new))
          build_time = TIME_EQUAL;
        else
          build_time = TIME_DIFFERENT;
        }

      if((pub_time || build_time) &&
         (pub_time != TIME_DIFFERENT) &&
         (build_time != TIME_DIFFERENT))
        {
        /* Feed didn't change */
        gavl_array_free(&items_new);
        gavl_dictionary_free(&channel_new);
        continue;
        }
    
      gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Channel %s changed", uri);
      }
    
    /* Save data and send signal */
    num_changed++;

    old_num = gavl_track_get_num_children(channel);
    
    gavl_dictionary_free(channel);
    gavl_dictionary_move(channel, &channel_new);

    /* Update channel */
    
    evt = bg_msg_sink_get(b->ctrl.evt_sink);
    gavl_msg_set_id_ns(evt, BG_MSG_DB_OBJECT_CHANGED, BG_MSG_NS_DB);

    gavl_dictionary_set_string_nocopy(&evt->header, GAVL_MSG_CONTEXT_ID,
                                      bg_sprintf("%s/%s", BG_MDB_ID_PODCASTS, md5));
    
    gavl_msg_set_arg_dictionary(evt, 0, channel);
    bg_msg_sink_put(b->ctrl.evt_sink);
    
    /* Update channel items */

    evt = bg_msg_sink_get(b->ctrl.evt_sink);

    gavl_msg_set_id_ns(evt, BG_MSG_DB_SPLICE_CHILDREN, BG_MSG_NS_DB);

    gavl_dictionary_set_string_nocopy(&evt->header, GAVL_MSG_CONTEXT_ID,
                                      bg_sprintf("%s/%s", BG_MDB_ID_PODCASTS, md5));
    
    gavl_msg_set_last(evt, 1);
  
    gavl_msg_set_arg_int(evt, 0, 0); // idx
    gavl_msg_set_arg_int(evt, 1, old_num); // del
    gavl_msg_set_arg_array(evt, 2, &items_new);
    
    bg_msg_sink_put(b->ctrl.evt_sink);
    

    /* Save items */
    filename = bg_sprintf("%s/%s", p->dir, md5);
    bg_array_save_xml(&items_new, filename, "items");
    free(filename);
    
    gavl_array_free(&items_new);
    gavl_dictionary_free(&channel_new);
    }

  if(num_changed)
    {
    /* Save index */
    bg_mdb_set_next_previous(&idx);
    
    filename = bg_sprintf("%s/index", p->dir);
    bg_array_save_xml(&idx, filename, "items");
    free(filename);
    }
  
  gavl_array_free(&idx);
  
  p->last_update_time = gavl_timer_get(p->timer);

  if(full)
    {
    p->last_full_update_time = p->last_update_time;
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Full update done");
    }
  return ret;
  }

static void make_saved_container(gavl_dictionary_t * saved_dict, const parsed_id_t * id, int num_items)
  {
  gavl_dictionary_t * saved_m = gavl_dictionary_get_dictionary_create(saved_dict, GAVL_META_METADATA);
  gavl_dictionary_set_string(saved_m, GAVL_META_LABEL, "Downloaded episodes");
  gavl_dictionary_set_string(saved_m, GAVL_META_CLASS, GAVL_META_CLASS_CONTAINER);
  gavl_dictionary_set_string_nocopy(saved_m, GAVL_META_ID, gavl_sprintf(BG_MDB_ID_PODCASTS"/%s/saved", id->podcast_id));

  bg_mdb_set_editable(saved_dict);
  gavl_track_set_num_children(saved_dict, 0, num_items);
  }

static char * get_index_filename(bg_mdb_backend_t * b, const parsed_id_t * id)
  {
  podcasts_t * p = b->priv;
#if 0
  if(id->episode_id)
    return NULL;
  else
#endif
    if(!id->podcast_id) // /podcasts
    return gavl_sprintf("%s/index", p->dir);
  else if(id->saved)
    return gavl_sprintf("%s/saved/%s/index", p->dir, id->podcast_id);
  else
    return gavl_sprintf("%s/%s", p->dir, id->podcast_id);
  }

static int load_array(bg_mdb_backend_t * b, const parsed_id_t * id, gavl_array_t * arr)
  {
  int ret = 0;
  char * filename = NULL;
  char * podcast_id = NULL;
  
  filename = get_index_filename(b, id);
  
  if(filename && !access(filename, R_OK) && bg_array_load_xml(arr, filename, "items"))
    {
    /* Saved episodes */
    if(!id->saved && id->podcast_id)
      {
      int num_saved;
      num_saved = get_num_saved_episodes(b, id->podcast_id);
      if(num_saved)
        {
        gavl_value_t saved_val;
        gavl_dictionary_t * saved_dict;
        gavl_value_init(&saved_val);
        saved_dict = gavl_value_set_dictionary(&saved_val);
        
        make_saved_container(saved_dict, id, num_saved);
        gavl_array_splice_val_nocopy(arr, 0, 0, &saved_val);
        }
      }
    
    bg_mdb_set_next_previous(arr);
    ret = 1;
    }
  
  if(filename)
    free(filename);
  if(podcast_id)
    free(podcast_id);
  
  return ret;
  }

static void save_array(bg_mdb_backend_t * b, const parsed_id_t * id, gavl_array_t * arr)
  {
  char * filename = get_index_filename(b, id);
  
  if(filename)
    {
    bg_array_save_xml(arr, filename, "items");
    free(filename);
    }
  
  }

static void save_local(bg_mdb_backend_t * b, const parsed_id_t * id)
  {
  char * dirname = NULL;
  //  fprintf(stderr, "save_local: %s\n", id);
  gavl_array_t arr;
  const gavl_dictionary_t * dict_c;
  gavl_dictionary_t * m;
  gavl_dictionary_t * src;
  const char * uri;
  int num_episodes = 0;
  
  podcasts_t * priv = b->priv;
  int created_local_folder = 0;
  char * local_filename;

  gavl_value_t entry_val;
  gavl_dictionary_t * dict;
  gavl_msg_t * res;
  char * filename = NULL; 
  int ret = 0;
  
  gavl_array_init(&arr);
  gavl_value_init(&entry_val);
  
  /* Browse local object */

  if(!load_array(b, id, &arr) ||
     !(dict_c = gavl_get_track_by_id_arr(&arr, id->id)))
    goto fail;
  
  num_episodes = arr.num_entries;
  
  dict = gavl_value_set_dictionary(&entry_val);
  gavl_dictionary_copy(dict, dict_c);
  if(!(m = gavl_track_get_metadata_nc(dict)))
    goto fail;

  gavl_array_reset(&arr);
  
  /* Create local directory (if it doesn't exist already) */

  dirname = gavl_sprintf("%s/saved/%s", priv->dir, id->podcast_id);
  
  if(!gavl_is_directory(dirname, 1))
    {
    gavl_ensure_directory(dirname, 0);
    created_local_folder = 1;
    }
  
  /* Load saved index and check if we already have this episode */
  filename = gavl_sprintf("%s/index", dirname);
  if(!access(filename, R_OK))
    bg_array_load_xml(&arr, filename, "items");
  
  if(gavl_get_track_by_id_arr(&arr, id->id))
    {
    fprintf(stderr, "Err 1\n");
    goto fail;
    }
  /* Download remote media */
  if((src = gavl_metadata_get_src_nc(m, GAVL_META_POSTER_URL, 0)) &&
     (uri = gavl_dictionary_get_string(src, GAVL_META_URI)))
    {
    char * prefix = gavl_sprintf("%s/%s-image", dirname, id->episode_id);
    
    if(!(local_filename =  bg_http_download(uri, prefix)))
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Downloading poster %s failed", uri);
      goto fail;
      }
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Saved poster %s to %s", uri, local_filename);
    
    gavl_dictionary_set_string_nocopy(src, GAVL_META_URI, local_filename);
    free(prefix);
    }

  if((src = gavl_metadata_get_src_nc(m, GAVL_META_SRC, 0)) &&
     (uri = gavl_dictionary_get_string(src, GAVL_META_URI)))
    {
    char * prefix = gavl_sprintf("%s/%s-media", dirname, id->episode_id);

    if(!(local_filename = bg_http_download(uri, prefix)))
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Downloading media %s failed", uri);
      goto fail;
      }
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Saved media %s to %s", uri, local_filename);
    
    gavl_dictionary_set_string_nocopy(src, GAVL_META_URI, local_filename);
    free(prefix);
    }
  
  gavl_dictionary_set_string_nocopy(m, GAVL_META_ID,
                                    gavl_sprintf("%s/%s/saved/%s", BG_MDB_ID_PODCASTS,
                                                 id->podcast_id, id->episode_id));
  
  /* Save metadata */
  
  gavl_array_splice_val(&arr, -1, 0, &entry_val);
  bg_mdb_set_next_previous(&arr);
  bg_array_save_xml(&arr, filename, "items");

  free(filename);
  filename = NULL;
  
  /* Send events */

  if(created_local_folder)
    {
    gavl_value_t add;
    gavl_dictionary_t * dict;
    gavl_array_t index;
    
    /* Set splice event for podcast */
    gavl_value_init(&add);
    dict = gavl_value_set_dictionary(&add);
    
    make_saved_container(dict, id, 1);

    res = bg_msg_sink_get(b->ctrl.evt_sink);
    gavl_msg_set_id_ns(res, BG_MSG_DB_SPLICE_CHILDREN, BG_MSG_NS_DB);
    gavl_dictionary_set_string_nocopy(&res->header, GAVL_MSG_CONTEXT_ID, gavl_sprintf("%s/%s", BG_MDB_ID_PODCASTS, id->podcast_id));

    gavl_msg_set_last(res, 1);
  
    gavl_msg_set_arg_int(res, 0, 0); // idx
    gavl_msg_set_arg_int(res, 1, 0); // del
    gavl_msg_set_arg_nocopy(res, 2, &add);
    
    bg_msg_sink_put(b->ctrl.evt_sink);
    
    /* Set change event for podcast */
    gavl_array_init(&index);
    filename = gavl_sprintf("%s/index", priv->dir);
    bg_array_load_xml(&index, filename, "items");
    
    res = bg_msg_sink_get(b->ctrl.evt_sink);

    gavl_msg_set_id_ns(res, BG_MSG_DB_OBJECT_CHANGED, BG_MSG_NS_DB);
    gavl_dictionary_set_string(&res->header, GAVL_MSG_CONTEXT_ID, gavl_sprintf("%s/%s", BG_MDB_ID_PODCASTS, id->podcast_id));

    dict = gavl_get_track_by_id_arr_nc(&index, gavl_dictionary_get_string(&res->header, GAVL_MSG_CONTEXT_ID));
    
    gavl_track_set_num_children(dict, 1, num_episodes);
    gavl_msg_set_arg_dictionary(res, 0, dict);

    //    fprintf(stderr, "set changed callback\n");
    //    gavl_dictionary_dump(dict, 2);
    
    bg_msg_sink_put(b->ctrl.evt_sink);
    bg_array_save_xml(&index, filename, "items");
    gavl_array_free(&index);
    }
  else
    {
    gavl_value_t val;
    gavl_dictionary_t * dict;

    /* Set splice event for saved */
    
    res = bg_msg_sink_get(b->ctrl.evt_sink);
    gavl_msg_set_id_ns(res, BG_MSG_DB_SPLICE_CHILDREN, BG_MSG_NS_DB);
    gavl_dictionary_set_string_nocopy(&res->header, GAVL_MSG_CONTEXT_ID,
                                      gavl_sprintf("%s/%s/saved", BG_MDB_ID_PODCASTS, id->podcast_id));
    
    gavl_msg_set_last(res, 1);
  
    gavl_msg_set_arg_int(res, 0, arr.num_entries-1); // idx
    gavl_msg_set_arg_int(res, 1, 0); // del
    gavl_msg_set_arg(res, 2, &entry_val);
    
    bg_msg_sink_put(b->ctrl.evt_sink);
    
    /* Set change event for saved */
    gavl_value_init(&val);
    dict = gavl_value_set_dictionary(&val);
    make_saved_container(dict, id, arr.num_entries);
    
    //    fprintf(stderr, "set changed callback\n");
    //    gavl_dictionary_dump(dict, 2);
    
    res = bg_msg_sink_get(b->ctrl.evt_sink);
    gavl_msg_set_id_ns(res, BG_MSG_DB_OBJECT_CHANGED, BG_MSG_NS_DB);
    gavl_dictionary_set_string(&res->header, GAVL_MSG_CONTEXT_ID, gavl_track_get_id(dict));
    gavl_msg_set_arg_dictionary(res, 0, dict);
    bg_msg_sink_put(b->ctrl.evt_sink);
    gavl_value_free(&val);
    }

  ret = 1;
  
  fail:

  if(!ret)
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Saving podcast episode failed");
  
  gavl_array_free(&arr);
  gavl_value_free(&entry_val);

  
  if(filename)
    free(filename);
  if(dirname)
    free(dirname);
  
  }


static void browse_object(bg_mdb_backend_t * b, gavl_msg_t * msg, const parsed_id_t * id)
  {
  podcasts_t * p = b->priv;

  gavl_msg_t * res = NULL;
  
  fprintf(stderr, "Browse object %s\n", id->id);
          
  if(!id->podcast_id)
    {
    /* /podcasts */
    res = bg_msg_sink_get(b->ctrl.evt_sink);
    bg_mdb_set_browse_obj_response(res, p->root, msg, -1, -1);
    }
  else
    {
    int idx = -1;
    gavl_array_t arr;

    char * parent_id;
    parsed_id_t parent_id_parsed;

    init_id(&parent_id_parsed);
    parent_id = bg_mdb_get_parent_id(id->id);
    parse_id(parent_id, &parent_id_parsed);
    
    gavl_array_init(&arr);
    
    if(load_array(b, &parent_id_parsed, &arr) &&
       ((idx = gavl_get_track_idx_by_id_arr(&arr, id->id)) >= 0))
      {
      gavl_dictionary_t * dict;
      res = bg_msg_sink_get(b->ctrl.evt_sink);

      dict = gavl_value_get_dictionary_nc(&arr.entries[idx]);
      
      if(id->episode_id)
        bg_mdb_add_http_uris(b->db, dict);
      
      bg_mdb_set_browse_obj_response(res, dict, msg, idx, arr.num_entries);
      }
    gavl_array_free(&arr);
    free_id(&parent_id_parsed);

    }
  
  if(res)
    {
    fprintf(stderr, "Browse object: Sending response\n");
    bg_msg_sink_put(b->ctrl.evt_sink);
    }
  
  }

  
static int handle_msg_podcast(void * priv, gavl_msg_t * msg)
  {
  bg_mdb_backend_t * b = priv;
  podcasts_t * p = b->priv;
  
  //  fprintf(stderr, "Handle message\n");

  parsed_id_t id;
  const char * ctx_id = gavl_dictionary_get_string(&msg->header,
                                             GAVL_MSG_CONTEXT_ID);
  init_id(&id);

  if(ctx_id)
    {
    if(!parse_id(ctx_id, &id))
      return 1;
    }
  
  switch(msg->NS)
    {
    case BG_MSG_NS_DB:
      {
      switch(msg->ID)
        {
        case BG_FUNC_DB_BROWSE_OBJECT:
          browse_object(b, msg, &id);
          //          fprintf(stderr, "Browse object %s\n", id.id);
          break;
        case BG_FUNC_DB_BROWSE_CHILDREN:
          {
          gavl_array_t arr;
          int start, num, total = 0, one_answer;

          //          fprintf(stderr, "Browse children %s\n", id.id);
          
          bg_mdb_get_browse_children_request(msg, NULL, &start, &num, &one_answer);
          gavl_array_init(&arr);

          if(load_array(b, &id, &arr) &&
             bg_mdb_adjust_num(start, &num, arr.num_entries))
            {
            gavl_msg_t * res = bg_msg_sink_get(b->ctrl.evt_sink);
            
            if(num < arr.num_entries)
              {
              int i;
            
              gavl_array_t arr1;
              gavl_array_init(&arr1);

              for(i = 0; i < num; i++)
                gavl_array_splice_val(&arr1, i, 0, &arr.entries[i+start]);

              bg_mdb_add_http_uris_arr(b->db, &arr1);
              
              bg_mdb_set_browse_children_response(res, &arr1, msg, &start, 1, total);
              gavl_array_free(&arr1);
              }
            else
              {
              bg_mdb_add_http_uris_arr(b->db, &arr);
              bg_mdb_set_browse_children_response(res, &arr, msg, &start, 1, total);
              }
          
            bg_msg_sink_put(b->ctrl.evt_sink);
            
            }
            
          gavl_array_free(&arr);
          }
          
          break;
        case BG_CMD_DB_SPLICE_CHILDREN:
          {
          int i;
          gavl_array_t index;
          
          int last = 0;
          int idx = 0;
          int del = 0;
          gavl_value_t add;
          gavl_msg_t * res;
          
          gavl_value_init(&add);
          gavl_msg_get_splice_children(msg, &last, &idx, &del, &add);
          gavl_array_init(&index);
          
          if(!strcmp(ctx_id, BG_MDB_ID_PODCASTS))
            {
            gavl_value_t added_val;
            gavl_array_t * added_arr;

            gavl_value_init(&added_val);
            added_arr = gavl_value_set_array(&added_val);
            
            /* Load root container */
            load_array(b, &id, &index);
            
            if((idx < 0) || (idx > p->subscriptions.num_entries)) // Append
              idx = p->subscriptions.num_entries;
          
            if((del < 0) || (idx + del > p->subscriptions.num_entries))
              del = p->subscriptions.num_entries - idx;

            for(i = 0; i < del; i++)
              unsubscribe(b, idx, &index);

            /* TODO: Subscribe */
            if(add.type == GAVL_TYPE_DICTIONARY)
              {
              subscribe(b, idx, &add, added_arr);
              }
            else if(add.type == GAVL_TYPE_ARRAY)
              {
              const gavl_array_t * add_arr = gavl_value_get_array(&add);
              
              for(i = 0; i < add_arr->num_entries; i++)
                subscribe(b, idx+i, &add_arr->entries[i], added_arr);
              }
            
            save_subscriptions(b);

            gavl_array_splice_array(&index, idx, 0, added_arr);
            
            bg_mdb_set_next_previous(&index);

            save_array(b, &id, &index);
            
            gavl_array_free(&index);

            /* Signal root container children */
          
            res = bg_msg_sink_get(b->ctrl.evt_sink);
            gavl_msg_set_splice_children_nocopy(res, BG_MSG_NS_DB, BG_MSG_DB_SPLICE_CHILDREN, ctx_id, 1, idx, del, &added_val);
            bg_msg_sink_put(b->ctrl.evt_sink);
            
            /* Signal root container */
          
            res = bg_msg_sink_get(b->ctrl.evt_sink);
            gavl_msg_set_id_ns(res, BG_MSG_DB_OBJECT_CHANGED, BG_MSG_NS_DB);
          
            gavl_dictionary_set_string(&res->header, GAVL_MSG_CONTEXT_ID, BG_MDB_ID_PODCASTS);
    
            gavl_track_set_num_children(p->root, p->subscriptions.num_entries, 0);
          
            gavl_msg_set_arg_dictionary(res, 0, p->root);
            bg_msg_sink_put(b->ctrl.evt_sink);
            
            }
          else if(gavl_string_ends_with(ctx_id, "/saved"))
            {
            if(add.type !=  GAVL_TYPE_UNDEFINED)
              gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Cannot add items to saved folder");
            
            /* Remove saved */

            if(del <= 0)
              break;

            /* Load saved items */
            if(!load_array(b, &id, &index))
              {
              gavl_value_free(&add);
              break;
              }

            if(idx == -1)
              idx = index.num_entries-1;

            if(del < 0)
              del = index.num_entries - idx;

            if(idx + del > index.num_entries)
              del = index.num_entries - idx;

            for(i = 0; i < del; i++)
              {
              char * pattern;
              const gavl_dictionary_t * dict;
              const char * episode_id;
              
              /* Delete related media files */
              if((dict = gavl_value_get_dictionary(&index.entries[idx])) &&
                 (episode_id = gavl_track_get_id(dict)) &&
                 (episode_id = strrchr(episode_id, '/')))
                {
                glob_t g;
                int j;
                
                episode_id++;
                
                pattern = gavl_sprintf("%s/saved/%s/%s*", p->dir, id.podcast_id, episode_id);
                glob(pattern, 0, NULL /* errfunc */, &g);

                for(j = 0; j < g.gl_pathc; j++)
                  {
                  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Removing %s", g.gl_pathv[j]);
                  remove(g.gl_pathv[j]);
                  }
                
                globfree(&g);
                free(pattern);
                }
              }

            gavl_array_splice_val(&index, idx, del, NULL);
            bg_mdb_set_next_previous(&index);
            save_array(b, &id, &index);
            
            if(!index.num_entries)
              {
              parsed_id_t root_id;
              gavl_array_t root_idx;
              gavl_dictionary_t * parent_dict;
              
              char * tmp_string;
              /* Was the last entry, remove everything */
              tmp_string = gavl_sprintf("%s/saved/%s/index", p->dir, id.podcast_id);
              gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Removing %s", tmp_string);
              
              if(remove(tmp_string))
                gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Couldn't remove %s: %s", tmp_string, strerror(errno));
              
              free(tmp_string);
              
              tmp_string = gavl_sprintf("%s/saved/%s", p->dir, id.podcast_id);
              gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Removing %s", tmp_string);
              if(rmdir(tmp_string))
                gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Couldn't remove %s: %s", tmp_string, strerror(errno));

              free(tmp_string);
              
              /* signal parent children */

              // /podcasts/<md5>
              tmp_string = bg_mdb_get_parent_id(ctx_id);
              
              res = bg_msg_sink_get(b->ctrl.evt_sink);
              
              gavl_msg_set_splice_children_nocopy(res, BG_MSG_NS_DB, BG_MSG_DB_SPLICE_CHILDREN,
                                                  tmp_string, 1, 0, 1, NULL);
              bg_msg_sink_put(b->ctrl.evt_sink);
              
              /* signal parent */
              gavl_array_init(&root_idx);
              init_id(&root_id);
              
              load_array(b, &root_id, &root_idx);
              
              if((parent_dict = gavl_get_track_by_id_arr_nc(&root_idx, tmp_string)))
                {
                gavl_track_set_num_children(parent_dict,
                                            0, gavl_track_get_num_item_children(parent_dict));
                res = bg_msg_sink_get(b->ctrl.evt_sink);

                gavl_msg_set_id_ns(res, BG_MSG_DB_OBJECT_CHANGED, BG_MSG_NS_DB);
                gavl_dictionary_set_string(&res->header, GAVL_MSG_CONTEXT_ID, tmp_string);
                gavl_msg_set_arg_dictionary(res, 0, parent_dict);
                bg_msg_sink_put(b->ctrl.evt_sink);
                }
              
              save_array(b, &root_id, &root_idx);
              
              free_id(&root_id);
              gavl_array_free(&root_idx);
              free(tmp_string);
              }
            else
              {
              gavl_dictionary_t saved_dict;
                
              
              /* signal saved children */
              res = bg_msg_sink_get(b->ctrl.evt_sink);
              gavl_msg_set_splice_children_nocopy(res, BG_MSG_NS_DB, BG_MSG_DB_SPLICE_CHILDREN, ctx_id, 1, idx, del, NULL);
              bg_msg_sink_put(b->ctrl.evt_sink);
              
              /* signal saved */
              gavl_dictionary_init(&saved_dict);
              make_saved_container(&saved_dict, &id, index.num_entries);
              res = bg_msg_sink_get(b->ctrl.evt_sink);

              gavl_msg_set_id_ns(res, BG_MSG_DB_OBJECT_CHANGED, BG_MSG_NS_DB);

              gavl_dictionary_set_string(&res->header, GAVL_MSG_CONTEXT_ID, ctx_id);
              
              gavl_msg_set_arg_dictionary(res, 0, &saved_dict);
              bg_msg_sink_put(b->ctrl.evt_sink);
              
              gavl_dictionary_free(&saved_dict);
              }
            
            }
          
          }
          break;
        case BG_CMD_DB_SAVE_LOCAL:
          save_local(b, &id);
          break;
        case BG_CMD_DB_SORT:
          {
          if(!strcmp(ctx_id, BG_MDB_ID_PODCASTS))
            {
            int i;
            const gavl_dictionary_t * dict;
            gavl_value_t idx_val;
            gavl_array_t * idx;
            gavl_msg_t * res;
            char * filename;
            const char * uri;
            fprintf(stderr, "Sort podcasts %s\n", ctx_id);
            
            gavl_value_init(&idx_val);
            idx = gavl_value_set_array(&idx_val);
            filename = bg_sprintf("%s/index", p->dir);
            bg_array_load_xml(idx, filename, "items");
            gavl_sort_tracks_by_label(idx);
            bg_array_save_xml(idx, filename, "items");
            free(filename);

            gavl_array_splice_val(&p->subscriptions, 0, -1, NULL);

            for(i = 0; i < idx->num_entries; i++)
              {
              if((dict = gavl_value_get_dictionary(&idx->entries[i])) &&
                 (dict = gavl_dictionary_get_dictionary(dict, BG_MDB_DICT)) &&
                 (uri = gavl_dictionary_get_string(dict, GAVL_META_URI)))
                gavl_string_array_add(&p->subscriptions, uri);
              }
            
            save_subscriptions(b);

            res = bg_msg_sink_get(b->ctrl.evt_sink);
            gavl_msg_set_splice_children_nocopy(res, BG_MSG_NS_DB, BG_MSG_DB_SPLICE_CHILDREN, ctx_id, 1, 0,
                                                idx->num_entries, &idx_val);
            bg_msg_sink_put(b->ctrl.evt_sink);
            
            gavl_value_free(&idx_val);
            // gavl_sort_tracks_by_label(gavl_array_t * arr)
            
            }
          }
        }
      }
    }

  free_id(&id);
  
  return 1;
  }


void bg_mdb_create_podcasts(bg_mdb_backend_t * b)
  {
  podcasts_t * priv;
  char * dir;
  priv = calloc(1, sizeof(*priv));

  priv->timer = gavl_timer_create();
  priv->last_update_time = GAVL_TIME_UNDEFINED;
  priv->last_full_update_time = GAVL_TIME_UNDEFINED;
  
  priv->root = bg_mdb_get_root_container(b->db, GAVL_META_CLASS_ROOT_PODCASTS);

  gavl_timer_start(priv->timer);
  
  bg_mdb_add_can_add(priv->root, "item.location");
  bg_mdb_set_editable(priv->root);
  
  b->priv = priv;
  b->destroy = destroy_podcasts;
  b->ping_func = ping_podcasts;

  bg_mdb_container_set_backend(priv->root, MDB_BACKEND_PODCASTS);


  
  bg_controllable_init(&b->ctrl,
                       bg_msg_sink_create(handle_msg_podcast, b, 0),
                       bg_msg_hub_create(1));
  
  priv->dir = bg_sprintf("%s/%s", b->db->path, "podcasts");
  
  gavl_ensure_directory(priv->dir, 0);

  dir = gavl_sprintf("%s/saved", priv->dir);
  gavl_ensure_directory(dir, 0);
  free(dir);
  
  load_subscriptions(b);
  
  gavl_track_set_num_children(priv->root, priv->subscriptions.num_entries, 0);
  }
