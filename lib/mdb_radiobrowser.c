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

#include <string.h>

#include <config.h>

#include <gmerlin/mdb.h>
#include <gmerlin/utils.h>
#include <gmerlin/bggavl.h>

#include <mdb_private.h>
#include <gavl/metatags.h>

#include <md5.h>


#include <gmerlin/translation.h>
#include <gmerlin/log.h>
#define LOG_DOMAIN "mdb.radiobrowser"

#define STORAGE_DIR "radiobrowser"

#define RB_ID "rbid"

/* 24 hours */
#define MAX_CACHE_AGE (24*60*60)

// #define MAX_CACHE_AGE 0


/*
 * /streams/radiobrowser
 *   /by-tag
 *     /j
 *       /jazz
 *         Blabla
 *   /by-country
 *     /Germany
 *       BlaBla
 *   /by-language
 *     /German
 *       BlaBla
 */

// #define RADIO_BROWSER_ROOT "http://www.radio-browser.info/webservice/"

#define RADIO_BROWSER_ROOT "https://de1.api.radio-browser.info/"

#define TAGS_URL     \
  RADIO_BROWSER_ROOT \
  "json/tags"

#define COUNTRIES_URL \
  RADIO_BROWSER_ROOT  \
  "json/countrycodes"

#define LANGUAGES_URL \
  RADIO_BROWSER_ROOT  \
  "json/languages"

#define ROOT_BY_LANGUAGE "/language"
#define ROOT_BY_COUNTRY  "/country"

#define ROOT_BY_TAG      "/tag"

#define GROUP_THRESHOLD 500

typedef struct
  {
  gavl_array_t tag_groups;
  
  gavl_array_t languages;
  gavl_array_t countries;

  char * root_id;
  int root_id_len;
  } rb_t;

static const struct
  {
  const char * label;
  const char * id;
  }
root_folders[] =
  {
    { "Languages", ROOT_BY_LANGUAGE },
    { "Countries", ROOT_BY_COUNTRY  },
    { "Tags",      ROOT_BY_TAG      },
  };

static const int num_root_folders = sizeof(root_folders)/sizeof(root_folders[0]);

static const char * dict_get_string(json_object * obj, const char * tag);

static int dict_get_int(json_object * obj, const char * tag);

static gavl_dictionary_t * browse_object(bg_mdb_backend_t * be,
                                         const char * ctx_id, int * idx, int * total);

static const gavl_dictionary_t * find_by_md5(const gavl_array_t * arr, const char * md5);

static const gavl_dictionary_t * get_tag_group(rb_t * rb, const char * id)
  {
  int i;
  const gavl_dictionary_t * ret;
  const gavl_dictionary_t * m;
  const char * var;
  
  for(i = 0; i < rb->tag_groups.num_entries; i++)
    {
    if((ret = gavl_value_get_dictionary(&rb->tag_groups.entries[i])) &&
       (m = gavl_track_get_metadata(ret)) &&
       (var = gavl_dictionary_get_string(m, RB_ID)) &&
       !strcmp(var, id))
      return ret;
    }
  return NULL;
  }


static const char * get_root_label(const char * id)
  {
  int i;
  for(i = 0; i < num_root_folders; i++)
    {
    if(!strcmp(root_folders[i].id, id))
      return root_folders[i].label;
    }
  return NULL;
  }
  

static const char * dict_get_string(json_object * obj, const char * tag)
  {
  json_object * child;
  const char * ret;

  if(!json_object_object_get_ex(obj, tag, &child) ||
     !json_object_is_type(child, json_type_string) ||
     !(ret = json_object_get_string(child)))
    return NULL;
  return ret;
  }

static int dict_get_int(json_object * obj, const char * tag)
  {
  json_object * child;
  int ret;

  if(!json_object_object_get_ex(obj, tag, &child) ||
     !json_object_is_type(child, json_type_int) ||
     !(ret = json_object_get_int(child)))
    return -1;
  return ret;
  }

static void destroy_rb(bg_mdb_backend_t * b)
  {
  rb_t * rb = b->priv;
  gavl_array_free(&rb->tag_groups);
  gavl_array_free(&rb->languages);
  gavl_array_free(&rb->countries);
  
  if(rb->root_id)
    free(rb->root_id);
  
  free(b->priv);
  }

/* Child of a "root folder", i.e. */

static void set_root_child(rb_t * rb, gavl_dictionary_t * dict, const gavl_array_t * arr,
                           int idx)
  {
  gavl_dictionary_t * m;
  const gavl_dictionary_t * arr_dict;
  const char * path = NULL;
  char * id;
  
  m = gavl_dictionary_get_dictionary_create(dict, GAVL_META_METADATA);

  if(arr == &rb->languages)
    {
    path = ROOT_BY_LANGUAGE;
    gavl_dictionary_set_string(m, GAVL_META_MEDIA_CLASS, GAVL_META_MEDIA_CLASS_CONTAINER_LANGUAGE);
    gavl_dictionary_set_string(m, GAVL_META_CHILD_CLASS, GAVL_META_MEDIA_CLASS_AUDIO_BROADCAST);
    }
  else if(arr == &rb->countries)
    {
    path = ROOT_BY_COUNTRY;
    gavl_dictionary_set_string(m, GAVL_META_MEDIA_CLASS, GAVL_META_MEDIA_CLASS_CONTAINER_COUNTRY);
    gavl_dictionary_set_string(m, GAVL_META_CHILD_CLASS, GAVL_META_MEDIA_CLASS_AUDIO_BROADCAST);
    }
  else if(arr == &rb->tag_groups)
    {
    path = ROOT_BY_TAG;
    gavl_dictionary_set_string(m, GAVL_META_MEDIA_CLASS, GAVL_META_MEDIA_CLASS_CONTAINER_TAG);
    gavl_dictionary_set_string(m, GAVL_META_CHILD_CLASS, GAVL_META_MEDIA_CLASS_CONTAINER);
    }
  
  if(path && (arr_dict = gavl_value_get_dictionary(&arr->entries[idx])))
    {
    if(arr == &rb->tag_groups)
      arr_dict = gavl_dictionary_get_dictionary(arr_dict, GAVL_META_METADATA);
    
    gavl_dictionary_copy(m, arr_dict);
    
    /* Replace ID */
    id = bg_sprintf("%s%s/%s", rb->root_id, path, gavl_dictionary_get_string(arr_dict, GAVL_META_ID));
    gavl_dictionary_set_string_nocopy(m, GAVL_META_ID, id);
    }
  
  //  bg_mdb_set_group_parent(arr, dict, GROUP_THRESHOLD);
  
  }

#if 0
static const struct
  {
  const char * codec;
  const char * mimetype;
  }
codecs[] =
  {
    { "MP3",  "audio/mpeg"  },
    { "AAC",  "audio/x-aac"   },
    { "AAC+", "audio/aacp"  },
    { "FLV",  "video/x-flv" },
    { "OGG",  "audio/ogg"   },
    { /* end */             }
  };

static const char * get_mimetype(json_object * child)
  {
  int i = 0;
  const char * var = dict_get_string(child, "codec");

  // fprintf(stderr, "get_mimetype: %s\n", var);

  if(!var)
    return NULL;

  while(codecs[i].codec)
    {
    if(!strcmp(codecs[i].codec, var))
      return codecs[i].mimetype;
    i++;
    }
  return NULL;
  }
#endif

static int set_station(bg_mdb_backend_t * be,
                       const char * parent_id, gavl_dictionary_t * dict, json_object * child)
  {
  const char * v;
  gavl_dictionary_t * m;
  char * tmp_string;

  
  if(!json_object_is_type(child, json_type_object))
    return 0;
    
  m = gavl_dictionary_get_dictionary_create(dict, GAVL_META_METADATA);
  gavl_dictionary_set_string(m, GAVL_META_LABEL,            dict_get_string(child, "name"));
  gavl_dictionary_set_string(m, GAVL_META_STATION,          dict_get_string(child, "name"));
  gavl_dictionary_set_string(m, GAVL_META_LOGO_URL, dict_get_string(child, "favicon"));
  
  gavl_dictionary_set_string(m, GAVL_META_COUNTRY,     dict_get_string(child, "country"));
  gavl_dictionary_set_string(m, GAVL_META_STATION_URL, dict_get_string(child, "homepage"));
  gavl_dictionary_set_string(m, GAVL_META_MEDIA_CLASS, GAVL_META_MEDIA_CLASS_AUDIO_BROADCAST);

  if((v = dict_get_string(child, "tags")))
    {
    int i = 0;
    char ** arr;
    arr = gavl_strbreak(v, ',');

    if(arr)
      {
      while(arr[i])
        {
        gavl_dictionary_append_string_array(m, GAVL_META_TAG, arr[i]);
        i++;
        }
      gavl_strbreak_free(arr);
      }
    }
  
  v = dict_get_string(child, "stationuuid");
  
  tmp_string = bg_sprintf("%s/%s", parent_id, v);
  gavl_dictionary_set_string_nocopy(m, GAVL_META_ID, tmp_string);

  /* Streams are set via a proxy mechanism */
  
  tmp_string = bg_sprintf("%sm3u/url/%s", RADIO_BROWSER_ROOT, v);
  gavl_metadata_add_src(m, GAVL_META_SRC, "audio/x-mpegurl", tmp_string);

  //  fprintf(stderr, "Adding mp3 url: %s\n", tmp_string);

  free(tmp_string);
  
  tmp_string = bg_sprintf("%spls/url/%s", RADIO_BROWSER_ROOT, v);
  gavl_metadata_add_src(m, GAVL_META_SRC, "audio/x-scpls", tmp_string);
  //  fprintf(stderr, "Adding pls url: %s\n", tmp_string);
  
  free(tmp_string);

 
  return 1;
  }

static void send_array(rb_t * rb, const char * path, const gavl_array_t * arr,
                       gavl_array_t * dst, int start, int num)
  {
  int i;
  gavl_value_t val;
  gavl_dictionary_t * dict;

  if(!bg_mdb_adjust_num(start, &num, arr->num_entries))
    return;
  
  for(i = 0; i < num; i++)
    {
    gavl_value_init(&val);
    dict = gavl_value_set_dictionary(&val);
    set_root_child(rb, dict, arr, i + start);
    gavl_array_splice_val_nocopy(dst, -1, 0, &val);
    }
  }

static void send_stations(bg_mdb_backend_t * be,
                          const char * url1, gavl_array_t * dst, const char * parent_id)
  {
  int i, num;
  json_object * obj;
  char * url;
  //  gavl_dictionary_t * cache_obj;
  //  gavl_value_t cache_val;
  
  //  rb_t * rb = be->priv;
  
  //  const gavl_array_t * cache_arr;
  
  url = bg_string_to_uri(url1, -1);
  if(!(obj = bg_json_from_url(url, NULL)))
    {
    free(url);
    return;
    }

  free(url);
  
  if(!json_object_is_type(obj, json_type_array))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Wrong type (must be array): %s",
           json_object_to_json_string(obj));
    }
  
  num = json_object_array_length(obj);

  for(i = 0; i < num; i++)
    {
    gavl_value_t val;
    gavl_dictionary_t * dict;
    
    json_object * child = json_object_array_get_idx(obj, i);

    gavl_value_init(&val);

    dict = gavl_value_set_dictionary(&val);
    if(set_station(be, parent_id, dict, child))
      gavl_array_splice_val_nocopy(dst, -1, 0, &val);
    else
      gavl_value_free(&val);
    }
  
  json_object_put(obj);
  }

static gavl_dictionary_t * create_root_folder(bg_mdb_backend_t * be, const char * id)
  {
  gavl_dictionary_t * m;
  const gavl_array_t * arr = NULL;
  gavl_dictionary_t * ret;
  
  rb_t * rb = be->priv;

  ret = gavl_dictionary_create();
  m = gavl_dictionary_get_dictionary_create(ret, GAVL_META_METADATA);
  
  if(!strcmp(id, ROOT_BY_LANGUAGE))
    {
    arr = &rb->languages;
    gavl_dictionary_set_string(m, GAVL_META_CHILD_CLASS, GAVL_META_MEDIA_CLASS_CONTAINER_LANGUAGE);
    }
  else if(!strcmp(id, ROOT_BY_COUNTRY))
    {
    arr = &rb->countries;
    gavl_dictionary_set_string(m, GAVL_META_CHILD_CLASS, GAVL_META_MEDIA_CLASS_CONTAINER_COUNTRY);
    }
  else if(!strcmp(id, ROOT_BY_TAG))
    {
    arr = &rb->tag_groups;
    gavl_dictionary_set_string(m, GAVL_META_CHILD_CLASS, GAVL_META_MEDIA_CLASS_CONTAINER_TAG);
    }

  gavl_dictionary_set_string(m, GAVL_META_MEDIA_CLASS, GAVL_META_MEDIA_CLASS_CONTAINER);
  gavl_dictionary_set_string_nocopy(m, GAVL_META_ID, bg_sprintf("/webradio/radiobrowser%s", id));
  gavl_dictionary_set_string(m, GAVL_META_LABEL, get_root_label(id));
  
  gavl_track_set_num_children(ret, arr->num_entries, 0);

  //  fprintf(stderr, "create_root_folder\n");
  //  gavl_dictionary_dump(ret, 2);
  
  return ret;
  }

static gavl_dictionary_t * create_root_folder_child(bg_mdb_backend_t * be,
                                                    gavl_array_t * arr,
                                                    const char * name)
  {
  int i;
  rb_t * rb = be->priv;
  
  for(i = 0; i < arr->num_entries; i++)
    {
    const gavl_dictionary_t * dict;
    const char * id;
    gavl_dictionary_t * ret;

    if(!(dict = gavl_value_get_dictionary(&arr->entries[i])))
      continue;

    if((arr == &rb->tag_groups) && !(dict = gavl_track_get_metadata(dict)))
      continue;
    
    if((id = gavl_dictionary_get_string(dict, GAVL_META_ID)) &&
       !strcmp(id, name))
      {
      ret = gavl_dictionary_create();
      set_root_child(rb, ret, arr, i);
      return ret;
      }
    }
  return NULL;
  }

static gavl_dictionary_t * browse_leaf(bg_mdb_backend_t * be,
                                       const char * ctx_id)
  {
  int num;
  json_object * obj = NULL;

  gavl_dictionary_t * ret = NULL;
  //  const gavl_dictionary_t * dict = NULL;
  char * url = NULL;
  const char * pos;
  json_object * child;
  char * parent_id = NULL;

  pos = strrchr(ctx_id, '/');
  if(!pos)
    goto fail;
  
  url = bg_sprintf(RADIO_BROWSER_ROOT "json/stations/byid/%s", pos+1);
  
  if(!(obj = bg_json_from_url(url, NULL)))
    goto fail;
  
  if(!json_object_is_type(obj, json_type_array))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Wrong type (must be array): %s",
           json_object_to_json_string(obj));
    goto fail;
    }
  
  if((num = json_object_array_length(obj)) != 1)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Expected 1 array element, got %d", num);
    goto fail;
    }
  
  child = json_object_array_get_idx(obj, 0);
  
  ret = gavl_dictionary_create();

  parent_id = bg_mdb_get_parent_id(ctx_id);
  
  if(!set_station(be, parent_id, ret, child))
    {
    gavl_dictionary_destroy(ret);
    ret = NULL;
    goto fail;
    }

  fail:
  
  if(url)
    free(url);
  
  if(parent_id)
    free(parent_id);
  
  if(obj)
    json_object_put(obj);
  
  return ret;
  }

static gavl_dictionary_t * browse_object(bg_mdb_backend_t * be,
                                         const char * ctx_id, int * idx, int * total)
  {
  gavl_dictionary_t * ret = NULL;
  const char * ctx_id_orig = ctx_id;
  
  const char * pos;
  rb_t * rb = be->priv;

  //  fprintf(stderr, "browse_object: %s\n", ctx_id);
  
  if(*ctx_id == '\0')
    {
    /* Should be handled by the core */
    return NULL;
    }
  else if(gavl_string_starts_with(ctx_id, ROOT_BY_LANGUAGE))
    {
    ctx_id += strlen(ROOT_BY_LANGUAGE);

    if(*ctx_id == '\0')
      {
      // /webradio/radiobrowser/language
      ret = create_root_folder(be, ROOT_BY_LANGUAGE);
      }
    else
      {
      ctx_id++;

      if((pos = strchr(ctx_id, '/')))
        {
        // /webradio/radiobrowser/language/BlaBla/id
        ret = browse_leaf(be, ctx_id_orig);
        }
      else
        {
        // /webradio/radiobrowser/language/Blabla
        ret = create_root_folder_child(be, &rb->languages, ctx_id);
        }
      }
    }
  else if(gavl_string_starts_with(ctx_id, ROOT_BY_COUNTRY))
    {
    ctx_id += strlen(ROOT_BY_COUNTRY);

    if(*ctx_id == '\0')
      {
      // /webradio/radiobrowser/country
      ret = create_root_folder(be, ROOT_BY_COUNTRY);
      }
    else
      {
      // /webradio/radiobrowser/country/Blabla
      ctx_id++;
      if((pos = strchr(ctx_id, '/')))
        {
        // /webradio/radiobrowser/country/BlaBla/id
        ret = browse_leaf(be, ctx_id_orig);
        }
      else
        {
        // /webradio/radiobrowser/country/BlaBla
        ret = create_root_folder_child(be, &rb->countries, ctx_id);
        }
      }
    }

  else if(gavl_string_starts_with(ctx_id, ROOT_BY_TAG))
    {
    ctx_id += strlen(ROOT_BY_TAG);

    //    fprintf(stderr, "Browse Object %s\n", ctx_id);
    
    if(*ctx_id == '\0')
      {
      // /webradio/radiobrowser/tag
      ret = create_root_folder(be, ROOT_BY_TAG);
      }
    else
      {
      ctx_id++;
      if(!(pos = strchr(ctx_id, '/')))
        {
        // /webradio/radiobrowser/tag/b
        ret = create_root_folder_child(be, &rb->tag_groups, ctx_id);
        }
      else
        {
        char * group_id;
        
        group_id = gavl_strndup(ctx_id, pos);
        
        ctx_id = pos + 1;

        // /webradio/radiobrowser/tag/b/md5
        
        if(!(pos = strchr(ctx_id, '/')))
          {
          const gavl_dictionary_t * group;
          const gavl_dictionary_t * tag;
          gavl_dictionary_t * m;

          if((group = get_tag_group(rb, group_id)) &&
             (tag = find_by_md5(gavl_dictionary_get_array(group, GAVL_META_CHILDREN), ctx_id)))
            {
            char * new_id;
            
            ret = gavl_dictionary_create();
            m = gavl_dictionary_get_dictionary_create(ret, GAVL_META_METADATA);
            gavl_dictionary_copy(m, tag);
            
            gavl_dictionary_set_string(m, GAVL_META_MEDIA_CLASS, GAVL_META_MEDIA_CLASS_CONTAINER_TAG);
            gavl_dictionary_set_string(m, GAVL_META_CHILD_CLASS, GAVL_META_MEDIA_CLASS_AUDIO_BROADCAST);
            
            new_id = bg_sprintf("/webradio/radiobrowser"ROOT_BY_TAG"/%s/%s",
                                group_id, 
                                gavl_dictionary_get_string(m, GAVL_META_ID));
            
            gavl_dictionary_set_string_nocopy(m, GAVL_META_ID, new_id);
            
            
            }
          
          }
        else
          {
          // /webradio/radiobrowser/tag/b/BlaBla/id
          ret = browse_leaf(be, ctx_id_orig);
          }

                            
                            
        }
      }
#if 0
    if(ret)
      {
      fprintf(stderr, "ret:\n");
      gavl_dictionary_dump(ret, 2);
      }
#endif
    }
  return ret;
  }

static const gavl_dictionary_t * find_by_md5(const gavl_array_t * arr, const char * md5)
  {
  int i;
  const char * md5_array;
  const gavl_dictionary_t * dict;

  for(i = 0; i < arr->num_entries; i++)
    {
    if((dict = gavl_value_get_dictionary(&arr->entries[i])) &&
       (md5_array = gavl_dictionary_get_string(dict, GAVL_META_ID)) &&
       !strcmp(md5_array, md5))
      {
      return dict;
      }
    }
  return NULL;
  }

static const char * get_orig_id(const gavl_array_t * arr, const char * md5, int * num_children)
  {
  const gavl_dictionary_t * dict;

  dict = find_by_md5(arr, md5);

  if(!dict)
    return NULL;
  
  if(num_children)
    {
    if(!gavl_dictionary_get_int(dict, GAVL_META_NUM_CHILDREN, num_children))
      *num_children = 0;
    }
  
  return gavl_dictionary_get_string(dict, RB_ID);
  }


static int handle_msg(void * priv, gavl_msg_t * msg)
  {
  gavl_msg_t * res;
  bg_mdb_backend_t * be = priv;
  rb_t * rb = be->priv;
  gavl_value_t val;
  gavl_dictionary_t * dict;
  gavl_dictionary_t * m;
  const char * pos;
  const char * orig_id;

  char * url;
  
  //  fprintf(stderr, "handle_msg_rb %d %d\n", msg->ID, msg->NS);
  
  switch(msg->NS)
    {
    case BG_MSG_NS_DB:
      {
      const char * ctx_id_orig;

      const char * ctx_id = gavl_dictionary_get_string(&msg->header,
                                                       GAVL_MSG_CONTEXT_ID);
      ctx_id_orig = ctx_id;
      
      ctx_id += rb->root_id_len;
      
      switch(msg->ID)
        {
        case BG_FUNC_DB_BROWSE_OBJECT:
          {
          int idx = -1, total = -1;
          //          bg_mdb_page_t page;
          
          //          bg_mdb_page_init_browse_object(be->db, &page, &ctx_id);
          
          if((dict = browse_object(be, ctx_id, &idx, &total)))
            {
            // bg_mdb_page_apply_object(be->db, &page, dict);
            
            res = bg_msg_sink_get(be->ctrl.evt_sink);
            bg_mdb_set_browse_obj_response(res, dict, msg, -1, -1);
            bg_msg_sink_put(be->ctrl.evt_sink, res);
            gavl_dictionary_destroy(dict);
            }
          
          //          bg_mdb_page_free(&page);
          }
          break;
        case BG_FUNC_DB_BROWSE_CHILDREN:
          {
          gavl_array_t arr;
          
          int i;
          int start, num, one_answer;
          const char * ctx_id;
          int total = 0;
          //          bg_mdb_page_t page;
          
          //          const char * ctx_id_orig;

          bg_mdb_get_browse_children_request(msg, &ctx_id, &start, &num, &one_answer);

          //          bg_mdb_page_init_browse_children(be->db, &page, &ctx_id, &start, &num);
          
          //          fprintf(stderr, "radiobrowser: Browse children %d %d %s\n", start, num, ctx_id);
          
          ctx_id_orig = ctx_id;
          ctx_id += rb->root_id_len;
          
          gavl_array_init(&arr);
          
          if(*ctx_id == '\0')
            {
            if(start >= num_root_folders)
              break;

            if(!bg_mdb_adjust_num(start, &num, num_root_folders))
              break;
            
            for(i = 0; i < num; i++)
              {
              gavl_value_init(&val);
              dict = gavl_value_set_dictionary(&val);
              m = gavl_dictionary_get_dictionary_create(dict, GAVL_META_METADATA);
              gavl_dictionary_set_string(m, GAVL_META_LABEL, root_folders[i + start].label);
              
              if(!strcmp(root_folders[i + start].id, ROOT_BY_LANGUAGE))
                {
                gavl_track_set_num_children(dict, rb->languages.num_entries, 0);
                gavl_dictionary_set_string(m, GAVL_META_CHILD_CLASS, GAVL_META_MEDIA_CLASS_CONTAINER_LANGUAGE);
                }
              else if(!strcmp(root_folders[i + start].id, ROOT_BY_COUNTRY))
                {
                gavl_track_set_num_children(dict, rb->countries.num_entries, 0);
                gavl_dictionary_set_string(m, GAVL_META_CHILD_CLASS, GAVL_META_MEDIA_CLASS_CONTAINER_COUNTRY);
                }
              else if(!strcmp(root_folders[i + start].id, ROOT_BY_TAG))
                {
                gavl_track_set_num_children(dict, rb->tag_groups.num_entries, 0);
                gavl_dictionary_set_string(m, GAVL_META_CHILD_CLASS, GAVL_META_MEDIA_CLASS_CONTAINER);
                }
              gavl_dictionary_set_string_nocopy(m, GAVL_META_ID, bg_sprintf("%s%s", rb->root_id,
                                                                            root_folders[i + start].id));

              gavl_dictionary_set_string(m, GAVL_META_MEDIA_CLASS, GAVL_META_MEDIA_CLASS_CONTAINER);
              
              gavl_array_splice_val_nocopy(&arr, -1, 0, &val);
              }
            total = num_root_folders;
            }
          else if(gavl_string_starts_with(ctx_id, ROOT_BY_LANGUAGE))
            {
            ctx_id += strlen(ROOT_BY_LANGUAGE);

            if(*ctx_id == '\0')
              {
              // /webradio/radiobrowser/language
              send_array(rb, ROOT_BY_LANGUAGE, &rb->languages, &arr, start, num);
              total = rb->languages.num_entries;
              }
            else
              {
              ctx_id++;

              if((pos = strchr(ctx_id, '/')))
                {
                // /webradio/radiobrowser/language/BlaBla/id
                }
              else
                {
                int num_stations;
                
                orig_id = get_orig_id(&rb->languages, ctx_id, &num_stations);

                if(!bg_mdb_adjust_num(start, &num, num_stations))
                  break;
                
                // /webradio/radiobrowser/language/Blabla
                url = bg_sprintf("%s/json/stations/bylanguageexact/%s?offset=%d&limit=%d",
                                 RADIO_BROWSER_ROOT, orig_id, start, num);
                send_stations(be, url, &arr, ctx_id_orig);
                free(url);
                total = num_stations;
                }
              }
            }
          else if(gavl_string_starts_with(ctx_id, ROOT_BY_COUNTRY))
            {
            ctx_id += strlen(ROOT_BY_COUNTRY);

            if(*ctx_id == '\0')
              {
              send_array(rb, ROOT_BY_COUNTRY, &rb->countries, &arr, start, num);
              total = rb->countries.num_entries;
              }
            else
              {
              // /webradio/radiobrowser/country/Blabla
              ctx_id++;
              if((pos = strchr(ctx_id, '/')))
                {
                // /webradio/radiobrowser/country/BlaBla/id
                
                }
              else
                {
                int num_stations;
                
                orig_id = get_orig_id(&rb->countries, ctx_id, &num_stations);

                if(!bg_mdb_adjust_num(start, &num, num_stations))
                  break;
                
                // /webradio/radiobrowser/country/BlaBla
                url = bg_sprintf("%s/json/stations/bycountrycodeexact/%s?offset=%d&limit=%d",
                                 RADIO_BROWSER_ROOT, orig_id, start, num);
                send_stations(be, url, &arr, ctx_id_orig);
                free(url);
                total = num_stations;
                }
              
              }
            
            }
          else if(gavl_string_starts_with(ctx_id, ROOT_BY_TAG))
            {
            ctx_id += strlen(ROOT_BY_TAG);

            if(*ctx_id == '\0')
              {
              // /webradio/radiobrowser/tag
              
              send_array(rb, ROOT_BY_TAG, &rb->tag_groups, &arr, start, num);

              //              fprintf(stderr, "ROOT_BY_TAG\n");
              //              gavl_array_dump(&arr, 2);

              total = rb->tag_groups.num_entries;
              }
            else
              {
              char * group_id;
              const gavl_dictionary_t * group;
              
              ctx_id++;
              
              if(!(pos = strchr(ctx_id, '/')))
                {
                const gavl_array_t * children;
                int num_tags = 0;
                int i;
                // /webradio/radiobrowser/tag/b
                group_id = gavl_strdup(ctx_id);
                group = get_tag_group(rb, group_id);

                children = gavl_dictionary_get_array(group, GAVL_META_CHILDREN);

                if(children)
                  num_tags = children->num_entries;
                
                if(!bg_mdb_adjust_num(start, &num, num_tags))
                  break;
                
                if(!bg_mdb_adjust_num(start, &num, children->num_entries))
                  break;

                for(i = start; i < start + num; i++)
                  {
                  char * new_id;
                  const gavl_dictionary_t * src;
                  gavl_dictionary_t * dst;
                  
                  if(!(src = gavl_value_get_dictionary(&children->entries[i])))
                    continue;
                  
                  gavl_value_init(&val);
                  dst = gavl_value_set_dictionary(&val);
                  dst = gavl_dictionary_get_dictionary_create(dst, GAVL_META_METADATA);

                  gavl_dictionary_copy(dst, src);
                  
                  gavl_dictionary_set_string(dst, GAVL_META_MEDIA_CLASS, GAVL_META_MEDIA_CLASS_CONTAINER_TAG);
                  gavl_dictionary_set_string(dst, GAVL_META_CHILD_CLASS, GAVL_META_MEDIA_CLASS_AUDIO_BROADCAST);
                  
                  new_id = bg_sprintf("/webradio/radiobrowser"ROOT_BY_TAG"/%s/%s",
                                      group_id, 
                                      gavl_dictionary_get_string(dst, GAVL_META_ID));
                  
                  gavl_dictionary_set_string_nocopy(dst, GAVL_META_ID, new_id);
                  
                  //                  fprintf(stderr, "Adding tag:\n");
                  //                  gavl_dictionary_dump(dst, 2);
 
                  gavl_array_splice_val_nocopy(&arr, -1, 0, &val);

                  
                  }
                
                }
              else
                {
                group_id = gavl_strndup(ctx_id, pos);
                
                ctx_id = pos + 1;
                
                if((pos = strchr(ctx_id, '/')))
                  {
                  // /webradio/radiobrowser/tag/b/BlaBla/id
                
                  }
                else
                  {
                  int num_stations = 0;
                  const gavl_array_t * children;
                  
                  // /webradio/radiobrowser/tag/b/BlaBla
                  // ctx_id: b/BlaBla
                  
                  // orig_id = get_orig_id(&rb->tags, ctx_id, &num_stations);
                  
                  group = get_tag_group(rb, group_id);

                  children = gavl_dictionary_get_array(group, GAVL_META_CHILDREN);
                  
                  orig_id = get_orig_id(children, ctx_id, &num_stations);
                  
                  // num_stations = gavl_track_get_num_children(group);
                  
                  if(!bg_mdb_adjust_num(start, &num, num_stations))
                    break;

                  
                  // /webradio/radiobrowser/tag/BlaBla
                  url = bg_sprintf("%s/json/stations/bytagexact/%s?offset=%d&limit=%d",
                                   RADIO_BROWSER_ROOT, orig_id, start, num);
                  //                  fprintf(stderr, "url: %s\n", url);
                  
                  send_stations(be, url, &arr, ctx_id_orig);
                  free(url);
                  total = num_stations;
                  }

                
                }
              free(group_id);
              }
            }
          
          //          fprintf(stderr, "RB Browse children\n");
          res = bg_msg_sink_get(be->ctrl.evt_sink);

          bg_mdb_set_browse_children_response(res, &arr, msg, &start, 1, total);
          bg_msg_sink_put(be->ctrl.evt_sink, res);
          gavl_array_free(&arr);
          }
          break;
        }
      
      break;
      }
    case GAVL_MSG_NS_GENERIC:
      switch(msg->ID)
        {
        case GAVL_CMD_QUIT:
          return 0;
        }
      break;
    }
  
  return 1;
  }

/* tags, languages, countries */

static int compare_country(const void * p1, const void * p2, void * data)
  {
  const char * s1;
  const char * s2;
  
  const gavl_dictionary_t * dict1;
  const gavl_dictionary_t * dict2;

  if(!(dict1 = gavl_value_get_dictionary(p1)) ||
     !(dict2 = gavl_value_get_dictionary(p2)) ||
     !(s1 = gavl_dictionary_get_string(dict1, GAVL_META_LABEL)) ||
     !(s2 = gavl_dictionary_get_string(dict2, GAVL_META_LABEL)))
    return 0;
  
  return strcoll(s1, s2);
  }

static void load_array(bg_mdb_backend_t * be, gavl_array_t * ret,
                       const char * url, const char * cache_name)
  {
  int i, num;
  json_object * obj;
  //  const gavl_value_t * val;
  gavl_value_t val1;

  
  if(!(obj = bg_json_from_url(url, NULL)))
    return;
  
  if(!json_object_is_type(obj, json_type_array))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Wrong type (must be array): %s",
           json_object_to_json_string(obj));
    }
  
  num = json_object_array_length(obj);

  for(i = 0; i < num; i++)
    {
    char md5[33];
    const char * v;
    gavl_value_t val;
    gavl_dictionary_t * dict;
    int num;    
    json_object * child = json_object_array_get_idx(obj, i);

    if(!json_object_is_type(child, json_type_object))
      continue;

    gavl_value_init(&val);
    dict = gavl_value_set_dictionary(&val);

    v = dict_get_string(child, "name");

     if(!strcmp(cache_name, "countries"))
       {
       gavl_dictionary_set_string(dict, GAVL_META_LABEL, gavl_get_country_label(v));
       gavl_dictionary_set_string(dict, GAVL_META_COUNTRY_CODE_2, v);
       }
    else
      gavl_dictionary_set_string(dict, GAVL_META_LABEL, v);
   
    gavl_dictionary_set_string(dict, RB_ID, v);

    gavl_dictionary_set_string(dict, GAVL_META_ID, bg_md5_buffer_str(v, strlen(v), md5));
    
    if((num = dict_get_int(child, "stationcount")) > 0)
      {
      gavl_dictionary_set_int(dict, GAVL_META_NUM_CHILDREN, num);
      gavl_dictionary_set_int(dict, GAVL_META_NUM_ITEM_CHILDREN, num);
      gavl_dictionary_set_int(dict, GAVL_META_NUM_CONTAINER_CHILDREN, 0);
      }
    
    gavl_array_splice_val_nocopy(ret, -1, 0, &val);
    }

  if(!strcmp(cache_name, "countries"))
    gavl_array_sort(ret, compare_country, NULL);
  
    
  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Loaded %s, %d entries", url, num);

  
  //  fprintf(stderr, "Loaded %s, %d entries", url, num);
  //  gavl_array_dump(ret, 2);
  //  fprintf(stderr, "\n");
  
  json_object_put(obj);

  gavl_value_init(&val1);
  val1.type = GAVL_TYPE_ARRAY;
  val1.v.array = ret;
  }

static void make_tag_groups(rb_t * rb, gavl_array_t * tags)
  {
  int i, j;

  const char * var;
  const gavl_dictionary_t * dict;

  gavl_value_t children_val;
  gavl_array_t * children;
  
  for(i = 0; i < bg_mdb_num_groups; i++)
    {
    j = 0;

    gavl_value_init(&children_val);
    children = gavl_value_set_array(&children_val);
    
    while(j < tags->num_entries)
      {
      if((dict = gavl_value_get_dictionary(&tags->entries[j])) &&
         (var = gavl_dictionary_get_string(dict, GAVL_META_LABEL)) &&
         (bg_mdb_test_group_condition(bg_mdb_groups[i].id, var)))
        {
        gavl_array_splice_val_nocopy(children, -1, 0, &tags->entries[j]);
        gavl_array_splice_val_nocopy(tags, j, 1, NULL);
        }
      else
        j++;
      }

    if(children->num_entries)
      {
      gavl_value_t group_val;
      gavl_dictionary_t * group_dict;
      gavl_dictionary_t * m;

      gavl_value_init(&group_val);
      group_dict = gavl_value_set_dictionary(&group_val);

      gavl_dictionary_set_nocopy(group_dict, GAVL_META_CHILDREN, &children_val);

      m = gavl_dictionary_get_dictionary_create(group_dict, GAVL_META_METADATA);
      
      gavl_dictionary_set_string(m, GAVL_META_LABEL, bg_mdb_groups[i].label);
      gavl_dictionary_set_string(m, RB_ID, bg_mdb_groups[i].id);
      gavl_dictionary_set_string(m, GAVL_META_ID, bg_mdb_groups[i].id);

      gavl_dictionary_set_int(m, GAVL_META_NUM_CHILDREN, children->num_entries);
      gavl_dictionary_set_int(m, GAVL_META_NUM_ITEM_CHILDREN, 0);
      gavl_dictionary_set_int(m, GAVL_META_NUM_CONTAINER_CHILDREN, children->num_entries);
      gavl_dictionary_set_string(m, GAVL_META_MEDIA_CLASS, GAVL_META_MEDIA_CLASS_CONTAINER);
      gavl_dictionary_set_string(m, GAVL_META_CHILD_CLASS, GAVL_META_MEDIA_CLASS_CONTAINER_TAG);
#if 0
      fprintf(stderr, "Created tag group %d entries\n", children->num_entries);
      gavl_dictionary_dump(m, 2);

      fprintf(stderr, "Children\n");
      gavl_array_dump(gavl_dictionary_get_array(group_dict, GAVL_META_CHILDREN), 2);
#endif 
      gavl_array_splice_val_nocopy(&rb->tag_groups, -1, 0, &group_val);
      
      }
    else
      gavl_value_free(&children_val);
    
    }
  
  }

static int ping_rb(bg_mdb_backend_t * be)
  {
  gavl_array_t tags;
  rb_t * rb = be->priv;

  gavl_array_init(&tags);
  load_array(be, &tags,      TAGS_URL, "tags");
  
  /* Create groups array */
  make_tag_groups(rb, &tags);
  gavl_array_free(&tags);
  
  load_array(be, &rb->languages, LANGUAGES_URL, "languages");
  load_array(be, &rb->countries, COUNTRIES_URL, "countries");
  
  be->ping_func = NULL;
  return 1;
  }

void bg_mdb_create_radio_browser(bg_mdb_backend_t * b)
  {
  rb_t * priv;
  gavl_dictionary_t * container;
  gavl_dictionary_t * child;
  gavl_dictionary_t * child_m;
  const gavl_dictionary_t * container_m;
  
  priv = calloc(1, sizeof(*priv));
  
  container = bg_mdb_get_root_container(b->db, GAVL_META_MEDIA_CLASS_ROOT_STREAMS);
  
  child = gavl_append_track(container, NULL);
  
  child_m = gavl_track_get_metadata_nc(child);
  
  container_m = gavl_track_get_metadata(container);
  
  gavl_dictionary_set_string(child_m, GAVL_META_LABEL, "radio-browser.info");
  gavl_dictionary_set_string(child_m, GAVL_META_MEDIA_CLASS, GAVL_META_MEDIA_CLASS_CONTAINER);

  gavl_dictionary_set_string_nocopy(child_m, GAVL_META_ID,
                                    bg_sprintf("%s/radiobrowser",
                                               gavl_dictionary_get_string(container_m, GAVL_META_ID)));

  /* Update container children. Must be done after setting the media class */
  gavl_track_update_children(container);
  
  /* languaue, country, tag */
  gavl_track_set_num_children(child, 3, 0);
  
  priv->root_id     = gavl_strdup(gavl_dictionary_get_string(child_m, GAVL_META_ID));
  priv->root_id_len = strlen(priv->root_id);
  
  bg_mdb_container_set_backend(child, MDB_BACKEND_RADIO_BROWSER);
  
  bg_controllable_init(&b->ctrl,
                       bg_msg_sink_create(handle_msg, b, 0),
                       bg_msg_hub_create(1));
  
  b->priv = priv;
  b->destroy = destroy_rb;
  b->ping_func = ping_rb;
  }
