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


#include <gmerlin/mdb.h>
#include <gmerlin/utils.h>
#include <md5.h>
#include <gmerlin/bggavl.h>

#include <mdb_private.h>
#include <gmerlin/xmlutils.h>
#include <gmerlin/log.h>

#include <gavl/metatags.h>

#define LOG_DOMAIN "mdb.podcasts"

#include <libxml/HTMLparser.h>

/* Directory structure

   podcasts/subscriptions: Array with just the URIs of the channels
   podcasts/index:         Children of the root folder (array)
   podcasts/<md5>:         Items of the channel (array)
   

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
  
  } podcasts_t;

static int check_update(bg_mdb_backend_t * b);

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

static void parse_cdata_description(const char * str, gavl_dictionary_t * m)
  {
  char * desc = NULL;
  htmlNodePtr child;
  htmlDocPtr doc = htmlReadMemory(str, strlen(str), 
                                  NULL, "utf-8", 
                                  HTML_PARSE_NODEFDTD | HTML_PARSE_RECOVER);
  
  if(!doc)
    return;

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
        gavl_dictionary_set_string(m, GAVL_META_LOGO_URL, BG_XML_GET_PROP(child, "src"));
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

  xmlFreeDoc(doc);
  
  }

static int load_items(bg_mdb_backend_t * b, const char * uri, char * md5,
                      gavl_dictionary_t * channel, gavl_array_t * items)
  {
  int ret = 0;
  int idx = 0;
  xmlNodePtr image;
  gavl_dictionary_t * channel_m;
  
  const char * str;
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
  
  if(!(str = bg_xml_node_get_child_content(node, "title")))
    goto fail;

  channel_m = gavl_dictionary_get_dictionary_create(channel, GAVL_META_METADATA);

  gavl_dictionary_set_string(channel_m, GAVL_META_TITLE, str);
  gavl_dictionary_set_string(channel_m, GAVL_META_LABEL, str);
  gavl_dictionary_set_string(channel_m, GAVL_META_MEDIA_CLASS, GAVL_META_MEDIA_CLASS_PODCAST);
  
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
      gavl_dictionary_set_string(channel_m, GAVL_META_LOGO_URL, str);
      }
    else if((str = BG_XML_GET_PROP(image, "href")))
      {
      gavl_dictionary_set_string(channel_m, GAVL_META_LOGO_URL, str);
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
        gavl_dictionary_set_string(it_m, GAVL_META_LOGO_URL, str);
        }
      else if((str = BG_XML_GET_PROP(image, "href")))
        {
        gavl_dictionary_set_string(it_m, GAVL_META_LOGO_URL, str);
        }
      }

    if(!gavl_dictionary_get(it_m, GAVL_META_LOGO_URL))
      gavl_dictionary_set_string(it_m, GAVL_META_LOGO_URL,
                                 gavl_dictionary_get_string(channel_m, GAVL_META_LOGO_URL));
    
    url = BG_XML_GET_PROP(enc, "url");
    mimetype = BG_XML_GET_PROP(enc, "type");

    if(url && mimetype)
      {
      char url_md5[33];
      
      bg_get_filename_hash(url, url_md5);
      
      if(gavl_string_starts_with(mimetype, "audio/"))
        {
        gavl_dictionary_set_string(it_m, GAVL_META_MEDIA_CLASS, GAVL_META_MEDIA_CLASS_AUDIO_PODCAST_EPISODE);
        }
      else if(gavl_string_starts_with(mimetype, "video/"))
        {
        gavl_dictionary_set_string(it_m, GAVL_META_MEDIA_CLASS, GAVL_META_MEDIA_CLASS_VIDEO_PODCAST_EPISODE);
        }
      gavl_metadata_add_src(it_m, GAVL_META_SRC, mimetype, url);

      gavl_dictionary_set_string_nocopy(it_m, GAVL_META_ID, bg_sprintf("%s/%s/%s", BG_MDB_ID_PODCASTS, md5, url_md5));
      
      }
    else
      {
      gavl_dictionary_set_string(it_m, GAVL_META_MEDIA_CLASS, GAVL_META_MEDIA_CLASS_ITEM);
      gavl_dictionary_set_string_nocopy(it_m, GAVL_META_ID, bg_sprintf("%s/%s/%d", BG_MDB_ID_PODCASTS, md5, idx));
      }
    
    //    fprintf(stderr, "Parsed Item:\n");
    //    gavl_dictionary_dump(it, 2);
    
    gavl_array_splice_val_nocopy(items, -1, 0, &it_val);

    
    
    idx++;

    }

  bg_mdb_set_idx_total(items, 0, items->num_entries);
  bg_mdb_set_next_previous(items);
  
  gavl_track_set_num_children(channel, 0, items->num_entries);
  
  ret = 1;
  
  fail:
  
    
  xmlFreeDoc(doc);
  return ret;
  }

/* reload xml files */
static void refresh_subscriptions(bg_mdb_backend_t * b)
  {
  
  }


static void load_subscriptions(bg_mdb_backend_t * b)
  {
  char * filename;
  podcasts_t * priv = b->priv;
  filename = bg_sprintf("%s/%s", priv->dir, "subscriptions");
    
  if(!access(filename, R_OK) &&
     bg_array_load_xml(&priv->subscriptions,
                       filename, "subscriptions"))
    {
    refresh_subscriptions(b);
    }
    
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

static void subscribe(bg_mdb_backend_t * b, const char * uri)
  {
  gavl_value_t channel_val;
  gavl_dictionary_t * channel;
  char md5[33];
  char * filename;
  gavl_array_t items;

  gavl_msg_t * evt;
  
  podcasts_t * p = b->priv;
  //  fprintf(stderr, "Subscribe %s\n", uri);

  /* Check if we have this already */
  if(gavl_string_array_indexof(&p->subscriptions, uri) >= 0)
    {
    gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Already subscribed to %s", uri);
    return;
    }

  gavl_value_init(&channel_val);
  channel = gavl_value_set_dictionary(&channel_val);
  gavl_array_init(&items);
  
  bg_get_filename_hash(uri, md5);
  
  if(load_items(b, uri, md5, channel, &items))
    {
    gavl_array_t idx;
    gavl_array_init(&idx);

    gavl_string_array_add(&p->subscriptions, uri);
    save_subscriptions(b);
    
    /* Save items */
    filename = bg_sprintf("%s/%s", p->dir, md5);
    bg_array_save_xml(&items, filename, "items");
    free(filename);

    /* Save root container */
    filename = bg_sprintf("%s/index", p->dir);
    
    if(!access(filename, R_OK))
      bg_array_load_xml(&idx, filename, "items");
    
    gavl_array_splice_val_nocopy(&idx, -1, 0, &channel_val);
    bg_mdb_set_next_previous(&idx);
    bg_array_save_xml(&idx, filename, "items");
    free(filename);
    
    /* Update root folder */

    evt = bg_msg_sink_get(b->ctrl.evt_sink);
    gavl_msg_set_id_ns(evt, BG_MSG_DB_OBJECT_CHANGED, BG_MSG_NS_DB);

    gavl_dictionary_set_string(&evt->header, GAVL_MSG_CONTEXT_ID, BG_MDB_ID_PODCASTS);
    
    gavl_track_set_num_children(p->root, idx.num_entries, 0);
    
    gavl_msg_set_arg_dictionary(evt, 0, p->root);
    bg_msg_sink_put(b->ctrl.evt_sink, evt);

    /* Update root children */

    evt = bg_msg_sink_get(b->ctrl.evt_sink);
    gavl_msg_set_id_ns(evt, BG_MSG_DB_SPLICE_CHILDREN, BG_MSG_NS_DB);
    gavl_dictionary_set_string(&evt->header, GAVL_MSG_CONTEXT_ID, BG_MDB_ID_PODCASTS);

    gavl_msg_set_last(evt, 1);
  
    gavl_msg_set_arg_int(evt, 0, idx.num_entries - 1); // idx
    gavl_msg_set_arg_int(evt, 1, 0); // del
    gavl_msg_set_arg(evt, 2, &idx.entries[idx.num_entries - 1]);
    
    bg_msg_sink_put(b->ctrl.evt_sink, evt);
    
    gavl_array_free(&idx);
    }
  else
    {
    gavl_value_free(&channel_val);
    }

  gavl_array_free(&items);
  
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
    bg_msg_sink_put(b->ctrl.evt_sink, evt);
    
    /* Update channel items */

    evt = bg_msg_sink_get(b->ctrl.evt_sink);

    gavl_msg_set_id_ns(evt, BG_MSG_DB_SPLICE_CHILDREN, BG_MSG_NS_DB);

    gavl_dictionary_set_string_nocopy(&evt->header, GAVL_MSG_CONTEXT_ID,
                                      bg_sprintf("%s/%s", BG_MDB_ID_PODCASTS, md5));
    
    gavl_msg_set_last(evt, 1);
  
    gavl_msg_set_arg_int(evt, 0, 0); // idx
    gavl_msg_set_arg_int(evt, 1, old_num); // del
    gavl_msg_set_arg_array(evt, 2, &items_new);
    
    bg_msg_sink_put(b->ctrl.evt_sink, evt);
    

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
    p->last_full_update_time = p->last_update_time;
  
  return ret;
  }


static int handle_msg(void * priv, gavl_msg_t * msg)
  {
  bg_mdb_backend_t * b = priv;
  podcasts_t * p = b->priv;
  
  fprintf(stderr, "Handle message\n");

  switch(msg->NS)
    {
    case BG_MSG_NS_DB:
      {
      switch(msg->ID)
        {
        case BG_FUNC_DB_BROWSE_OBJECT:
          {
          const char * pos;
          
          gavl_msg_t * res = NULL;
          const char * ctx_id = gavl_dictionary_get_string(&msg->header,
                                                           GAVL_MSG_CONTEXT_ID);


          fprintf(stderr, "Browse object %s\n", ctx_id);
          
          ctx_id += strlen(BG_MDB_ID_PODCASTS);

          if(*ctx_id == '\0')
            {
            /* /podcasts/ */
            res = bg_msg_sink_get(b->ctrl.evt_sink);
            bg_mdb_set_browse_obj_response(res, p->root, msg, -1, -1);
            }
          else
            {
            const gavl_dictionary_t * dict;
            gavl_array_t arr;
            char * md5 = NULL;
            char * filename = NULL;
            int idx = -1;
            
            ctx_id++;

            gavl_array_init(&arr);

            if((pos = strchr(ctx_id, '/')))
              {
              /* /podcasts/<md5>/<idx> */
              
              md5 = gavl_strndup(ctx_id, pos);
              
              filename = bg_sprintf("%s/%s", p->dir, md5);
              
              pos++;

              idx = atoi(pos);

              if(bg_array_load_xml(&arr, filename, "items") &&
                 (idx >= 0) && (idx < arr.num_entries) &&
                 (dict = gavl_value_get_dictionary(&arr.entries[idx])))
                {
                res = bg_msg_sink_get(b->ctrl.evt_sink);
                bg_mdb_set_browse_obj_response(res, dict, msg, idx, arr.num_entries);
                }
              
              }
            else
              {
              int i;
              const char * test_id;
              /* /podcasts/md5 */
              md5 = gavl_strdup(ctx_id);
              
              filename = bg_sprintf("%s/index", p->dir);
              
              if(bg_array_load_xml(&arr, filename, "items"))
                {
                for(i = 0; i < arr.num_entries; i++)
                  {
                  if((dict = gavl_value_get_dictionary(&arr.entries[i])) &&
                     (test_id = gavl_track_get_id(dict)) &&
                     (pos = strrchr(test_id, '/')) &&
                     (!strcmp(pos + 1, md5)))
                    {
                    res = bg_msg_sink_get(b->ctrl.evt_sink);
                    bg_mdb_set_browse_obj_response(res, dict, msg, i, arr.num_entries);
                    }
                  }
                
                }
                 
              }

            if(filename)
              free(filename);

            if(md5)
              free(md5);
            
            gavl_array_free(&arr);
            
            
            
            }
          
          if(res)
            {
            fprintf(stderr, "Browse object: Sending response\n");
            
            bg_msg_sink_put(b->ctrl.evt_sink, res);
            }
          }


          break;
        case BG_FUNC_DB_BROWSE_CHILDREN:
          {
          gavl_array_t arr;
          /* Flush messages */
          gavl_msg_t * res;
          const char * ctx_id;
          int start, num, total = 0, one_answer;

          char * filename;
          
          ctx_id = gavl_dictionary_get_string(&msg->header, GAVL_MSG_CONTEXT_ID);

          bg_mdb_get_browse_children_request(msg, &ctx_id, &start, &num, &one_answer);
          
          //     fprintf(stderr, "Browse children %s\n", ctx_id);
          
          if(!strcmp(ctx_id, BG_MDB_ID_PODCASTS))
            {
            filename = bg_sprintf("%s/index", p->dir);
            }
          else
            {
            ctx_id += strlen(BG_MDB_ID_PODCASTS);
            filename = bg_sprintf("%s%s", p->dir, ctx_id);
            }

          gavl_array_init(&arr);
          
          if(!bg_array_load_xml(&arr, filename, "items") ||
             !bg_mdb_adjust_num(start, &num, arr.num_entries))
            {
            free(filename);
            gavl_array_free(&arr);
            return 1;
            }

          res = bg_msg_sink_get(b->ctrl.evt_sink);
          
          if(num < arr.num_entries)
            {
            int i;
            
            gavl_array_t arr1;
            gavl_array_init(&arr1);

            for(i = 0; i < num; i++)
              gavl_array_splice_val(&arr1, i, 0, &arr.entries[i+start]);

            bg_mdb_set_browse_children_response(res, &arr1, msg, &start, 1, total);
            gavl_array_free(&arr1);
            }
          else
            {
            bg_mdb_set_browse_children_response(res, &arr, msg, &start, 1, total);
            }
          
          bg_msg_sink_put(b->ctrl.evt_sink, res);
          
          gavl_array_free(&arr);

          }
          break;
        case BG_CMD_DB_LOAD_URIS:
          {
          const gavl_value_t * loc;
          
          fprintf(stderr, "Load URIs\n");
          
          // idx = gavl_msg_get_arg_int(msg, 0);
          loc = gavl_msg_get_arg_c(msg, 1);

          if(loc->type == GAVL_TYPE_STRING)
            subscribe(b, gavl_value_get_string(loc));
          else if(loc->type == GAVL_TYPE_ARRAY)
            {
            const char * str;
            int i;
            const gavl_array_t * arr = gavl_value_get_array(loc);
            
            for(i = 0; i < arr->num_entries; i++)
              {
              if((str = gavl_string_array_get(arr, i)))
                subscribe(b, str);
              }
            }
          
          }
          break;
        case BG_CMD_DB_SPLICE_CHILDREN:
          {
          int i;
          gavl_array_t index;
          char * index_filename;
          
          int last = 0;
          int idx = 0;
          int del = 0;
          gavl_value_t add;
          const char * ctx_id;
          gavl_msg_t * res;
          
          
          ctx_id = gavl_dictionary_get_string(&msg->header, GAVL_MSG_CONTEXT_ID);

          if(strcmp(ctx_id, BG_MDB_ID_PODCASTS))
            break;

          gavl_value_init(&add);
          gavl_msg_get_splice_children(msg, &last, &idx, &del, &add);
          
          /* Load root container */
          index_filename = bg_sprintf("%s/index", p->dir);

          gavl_array_init(&index);
          bg_array_load_xml(&index, index_filename, "items");

          if((idx < 0) || (idx > p->subscriptions.num_entries)) // Append
            idx = p->subscriptions.num_entries;
          
          if((del < 0) || (idx + del > p->subscriptions.num_entries))
            del = p->subscriptions.num_entries - idx;

          for(i = 0; i < del; i++)
            unsubscribe(b, idx, &index);
          
          save_subscriptions(b);
          
          bg_mdb_set_next_previous(&index);
          bg_array_save_xml(&index, index_filename, "items");
          
          free(index_filename);
          gavl_array_free(&index);

          /* Signal root container children */
          
          res = bg_msg_sink_get(b->ctrl.evt_sink);

          gavl_msg_set_id_ns(res, BG_MSG_DB_SPLICE_CHILDREN, BG_MSG_NS_DB);
          gavl_dictionary_set_string(&res->header, GAVL_MSG_CONTEXT_ID, ctx_id);

          gavl_msg_set_last(res, 1);
  
          gavl_msg_set_arg_int(res, 0, idx); // idx
          gavl_msg_set_arg_int(res, 1, del); // del
          gavl_msg_set_arg(res, 2, NULL);

          bg_msg_sink_put(b->ctrl.evt_sink, res);
          
          /* Signal root container */
          
          res = bg_msg_sink_get(b->ctrl.evt_sink);
          gavl_msg_set_id_ns(res, BG_MSG_DB_OBJECT_CHANGED, BG_MSG_NS_DB);
          
          gavl_dictionary_set_string(&res->header, GAVL_MSG_CONTEXT_ID, BG_MDB_ID_PODCASTS);
    
          gavl_track_set_num_children(p->root, p->subscriptions.num_entries, 0);
          
          gavl_msg_set_arg_dictionary(res, 0, p->root);
          bg_msg_sink_put(b->ctrl.evt_sink, res);
          
          }
        }
      }
    }
  

  return 1;
  }


void bg_mdb_create_podcasts(bg_mdb_backend_t * b)
  {
  podcasts_t * priv;

  priv = calloc(1, sizeof(*priv));

  priv->timer = gavl_timer_create();
  priv->last_update_time = GAVL_TIME_UNDEFINED;
  priv->last_full_update_time = GAVL_TIME_UNDEFINED;
  
  priv->root = bg_mdb_get_root_container(b->db, GAVL_META_MEDIA_CLASS_ROOT_PODCASTS);

  gavl_timer_start(priv->timer);
  
  bg_mdb_add_can_add(priv->root, "item.location");
  bg_mdb_set_editable(priv->root);
  
  b->priv = priv;
  b->destroy = destroy_podcasts;
  b->ping_func = ping_podcasts;

  bg_mdb_container_set_backend(priv->root, MDB_BACKEND_PODCASTS);
  
  
  bg_controllable_init(&b->ctrl,
                       bg_msg_sink_create(handle_msg, b, 0),
                       bg_msg_hub_create(1));
  
  priv->dir = bg_sprintf("%s/%s", b->db->path, "podcasts");
  
  bg_ensure_directory(priv->dir, 0);

  load_subscriptions(b);
  
  gavl_track_set_num_children(priv->root, priv->subscriptions.num_entries, 0);
  }
