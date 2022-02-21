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

#include <gavl/metatags.h>

#include <upnp/devicepriv.h>
#include <upnp/mediaserver.h>
#include <upnp/didl.h>

#include <string.h>
#include <ctype.h>
#include <fnmatch.h>
#include <unistd.h>

#include <gmerlin/utils.h>

#include <gmerlin/upnp/soap.h>


/* Service actions */

#define ARG_SearchCaps        1
#define ARG_SortCaps          2
#define ARG_Id                3
#define ARG_ObjectID          4
#define ARG_BrowseFlag        5
#define ARG_Filter            6
#define ARG_StartingIndex     7
#define ARG_RequestedCount    8
#define ARG_SortCriteria      9
#define ARG_Result            10
#define ARG_NumberReturned    11
#define ARG_TotalMatches      12
#define ARG_UpdateID          13

static int GetSearchCapabilities(bg_upnp_service_t * s)
  {
  char * ret = calloc(1, 1);
  bg_upnp_service_set_arg_out_string(&s->req, ARG_SearchCaps, ret);
  return 1;
  }

static int GetSortCapabilities(bg_upnp_service_t * s)
  {
  char * ret = calloc(1, 1);
  bg_upnp_service_set_arg_out_string(&s->req, ARG_SortCaps, ret);
  return 1;
  }

static int GetSystemUpdateID(bg_upnp_service_t * s)
  {
  bg_upnp_service_set_arg_out_int(&s->req, ARG_Id, 1);
  return 1;
  }

/* DIDL stuff */

static void set_res_bitrate(xmlNodePtr child, char ** filter, int bitrate)
  {
  if(bg_didl_filter_attribute("res", "bitrate", filter) && (bitrate > 0))
    bg_didl_set_attribute_int(child, "bitrate", bitrate / 8);
  }

static void set_res_samplerate(xmlNodePtr child,
                               char ** filter, int samplerate,
                               const bg_upnp_recompressor_t * recompressor)
  {
  if(recompressor && strcasecmp(recompressor->name, "Opus"))
    samplerate = 48000;
  
  if(bg_didl_filter_attribute("res", "sampleFrequency", filter))
    bg_didl_set_attribute_int(child, "sampleFrequency", samplerate);
  }

static void set_res_channels(xmlNodePtr child, char ** filter, int channels)
  {
  if(bg_didl_filter_attribute("res", "nrAudioChannels", filter))
    bg_didl_set_attribute_int(child, "nrAudioChannels", channels);
  }

static void set_res_duration(xmlNodePtr child, char ** filter, gavl_time_t duration)
  {
  if((duration > 0) && bg_didl_filter_attribute("res", "duration", filter))
    {
    char buf[GAVL_TIME_STRING_LEN_MS];
    gavl_time_prettyprint_ms(duration, buf);
    BG_XML_SET_PROP(child, "duration", buf);
    }
  
  }

static void set_res_resolution(xmlNodePtr child, char ** filter, int w, int h)
  {
  char * str;
  if(bg_didl_filter_attribute("res", "resolution", filter))
    {
    str = bg_sprintf("%dx%d", w, h);
    BG_XML_SET_PROP(child, "resolution", str);
    free(str);
    }
  }

static void set_res_size(xmlNodePtr child, char ** filter, int64_t size)
  {
  if(bg_didl_filter_attribute("res", "size", filter))
    bg_didl_set_attribute_int(child, "size", size);
  }

static void set_res_format(xmlNodePtr child, void * f, char ** filter,
                           const bg_upnp_transcoder_t * transcoder,
                           const bg_upnp_recompressor_t * recompressor)
  {
  bg_db_object_type_t t = bg_db_object_get_type(f);
  /* Format specific stuff */

  if(t == BG_DB_OBJECT_AUDIO_FILE)
    {
    /* Audio attributes */

    bg_db_audio_file_t * af = f;

    if(transcoder)
      set_res_bitrate(child, filter, transcoder->get_bitrate(f));
    else if(isdigit(af->bitrate[0]))
      set_res_bitrate(child, filter, 1000 * atoi(af->bitrate));

    set_res_samplerate(child, filter, af->samplerate, NULL); 
    set_res_channels(child, filter, af->channels);   
    set_res_duration(child, filter, af->file.obj.duration);

    set_res_size(child, filter, af->file.obj.size);
    }
  else if(t & BG_DB_FLAG_IMAGE)
    {
    bg_db_image_file_t * imf = f;
    set_res_resolution(child, filter, imf->width, imf->height);
    set_res_size(child, filter, imf->file.obj.size);
    }
  else if(t & BG_DB_FLAG_VIDEO)
    {
    bg_db_video_file_t * vf = f;
    set_res_resolution(child, filter, vf->width, vf->height);
    set_res_duration(child, filter, vf->file.obj.duration);
    set_res_size(child, filter, vf->file.obj.size);
    }
  else if(t == BG_DB_OBJECT_RADIO_URL)
    {
    bg_db_url_t * u = f;

    set_res_channels(child, filter, u->channels);
    set_res_samplerate(child, filter, u->samplerate, recompressor);
    
    if(recompressor)
      set_res_bitrate(child, filter, 
                      bg_upnp_recompressor_get_bitrate(recompressor, f) * 1000);
    else if(u->audio_bitrate && isdigit(u->audio_bitrate[0]))
      set_res_bitrate(child, filter, 1000*atoi(u->audio_bitrate));
    }
  else if(t == BG_DB_OBJECT_PLAYLIST)
    {
    set_res_channels(child, filter, bg_upnp_playlist_stream_afmt.num_channels);
    set_res_samplerate(child, filter, bg_upnp_playlist_stream_afmt.samplerate, recompressor);
    
    if(recompressor)
      set_res_bitrate(child, filter, 
                      bg_upnp_recompressor_get_bitrate(recompressor, f) * 1000);
    }
  
  }

static char * encode_path_uri(const char * path)
  {
  const char * end;
  const char * pos = strrchr(path, '/');

  if(!pos)
    return NULL;
  pos++;

  end = strrchr(pos, '.');
  if(end)
    return bg_string_to_uri(pos, end - pos);
  else
    return bg_string_to_uri(pos, -1);
  }

static void bg_didl_create_res_file(xmlDocPtr doc,
                                    xmlNodePtr node,
                                    void * f, const char * url_base, 
                                    char ** filter,
                                    const bg_upnp_client_t * cl,
                                    int flags)
  {
  bg_db_file_t * file = f;
  char * tmp_string;
  xmlNodePtr child;
  int i;
  int num = 0;
  char * uri_enc = NULL;
  const char * ext;
  const char * client_mimetype = NULL;
  
  if(!bg_didl_filter_element("res", filter))
    return;

  client_mimetype = bg_upnp_client_translate_mimetype(cl, file->mimetype);
  
  /* Local file */
  if(flags & BG_UPNP_CLIENT_ORIG_LOCATION)
    {
    tmp_string = bg_string_to_uri(file->path, -1);
    child = bg_xml_append_child_node(node, "res", tmp_string);
    free(tmp_string);
 
    tmp_string = bg_sprintf("*:*:%s:*", client_mimetype);
    BG_XML_SET_PROP(child, "protocolInfo", tmp_string);
    free(tmp_string);

    set_res_format(child, f, filter, NULL, NULL);
    if(bg_didl_filter_attribute("res", "size", filter))
      bg_didl_set_attribute_int(child, "size", file->obj.size);
    }
  
  /* Original (via http) */
  if(bg_upnp_client_supports_mimetype(cl, file->mimetype))
    {
    if(!uri_enc)
      uri_enc = encode_path_uri(file->path);

    ext = strrchr(file->path, '.');
    if(!ext)
      ext = "";
    
    tmp_string = bg_sprintf("%smedia/%"PRId64"/%s%s", url_base,
                            bg_db_object_get_id(f), uri_enc, ext);
    
    child = bg_xml_append_child_node(node, "res", tmp_string);
    free(tmp_string);
    
    tmp_string = bg_sprintf("http-get:*:%s:*",
                            bg_upnp_client_translate_mimetype(cl, file->mimetype));
    BG_XML_SET_PROP(child, "protocolInfo", tmp_string);
    free(tmp_string);
    
    num++;

    set_res_format(child, f, filter, NULL, NULL);
    }

  if(num && !(flags & BG_UPNP_CLIENT_MULTIPLE_RES))
    goto end;

  i = 0;
  while(bg_upnp_transcoders[i].name)
    {
    /* Skip unusable transcoders */
    if(fnmatch(bg_upnp_transcoders[i].in_mimetype, file->mimetype, 0) ||
       !strcmp(file->mimetype, bg_upnp_transcoders[i].out_mimetype) ||
       !bg_upnp_client_supports_mimetype(cl, bg_upnp_transcoders[i].out_mimetype))
      {
      i++;
      continue;
      }

    client_mimetype = bg_upnp_client_translate_mimetype(cl, bg_upnp_transcoders[i].out_mimetype);
    
    if(!uri_enc)
      uri_enc = encode_path_uri(file->path);
    
    tmp_string = bg_sprintf("%stranscode/%"PRId64"/%s/%s%s", url_base,
                            bg_db_object_get_id(f), bg_upnp_transcoders[i].name,
                            uri_enc, bg_upnp_transcoders[i].ext);
    child = bg_xml_append_child_node(node, "res", tmp_string);
    free(tmp_string);
    
    tmp_string = bg_upnp_transcoder_make_protocol_info(&bg_upnp_transcoders[i], f, client_mimetype);
    BG_XML_SET_PROP(child, "protocolInfo", tmp_string);
    free(tmp_string);
#if 0    
    if(bg_didl_filter_attribute("res", "size", filter))
      bg_didl_set_attribute_int(child, "size",
                                bg_upnp_transcoder_get_size(bg_upnp_transcoders[i].transcoder, f));
#endif
    set_res_format(child, f, filter, &bg_upnp_transcoders[i], NULL);

    num++;

    if(num && !(flags & BG_UPNP_CLIENT_MULTIPLE_RES))
      goto end;
    i++;
    }
  
  end:
  if(uri_enc)
    free(uri_enc);
  }

static void bg_didl_create_res_recompress(xmlDocPtr doc,
                                          xmlNodePtr node,
                                          void * f,
                                          const char * url_base, 
                                          const char * path,
                                          const char * mimetype,
                                          char ** filter,
                                          const bg_upnp_client_t * cl)
  {
  xmlNodePtr child;
  int i;
  int num = 0;
  char * tmp_string;
  const char * client_mimetype = NULL;
  
  i = 0;
  while(bg_upnp_recompressors[i].name)
    {
    if(num && !(cl->flags & BG_UPNP_CLIENT_MULTIPLE_RES))
      break;
    
    if(fnmatch(bg_upnp_recompressors[i].in_mimetype, mimetype, 0) ||
       !strcmp(mimetype, bg_upnp_recompressors[i].out_mimetype) ||
       !bg_upnp_client_supports_mimetype(cl, bg_upnp_recompressors[i].out_mimetype))
      {
      i++;
      continue;
      }

    tmp_string = bg_sprintf("%srecompress/%s/%s", url_base,
                            bg_upnp_recompressors[i].name, path);

    child = bg_xml_append_child_node(node, "res", tmp_string);
    free(tmp_string);
    
    set_res_format(child, f, filter, NULL, &bg_upnp_recompressors[i]);

    client_mimetype = bg_upnp_client_translate_mimetype(cl, bg_upnp_recompressors[i].out_mimetype);
    
    tmp_string = bg_upnp_recompressor_make_protocol_info(&bg_upnp_recompressors[i], f, client_mimetype);
    BG_XML_SET_PROP(child, "protocolInfo", tmp_string);
    free(tmp_string);

    num++;
    i++;
    }
  
  }

static const char * pls_formats[] =
  {
   "pls",
   "m3u",
   "xspf",
    NULL
  };
  
static void bg_didl_create_res_pls(xmlDocPtr doc,
                                   xmlNodePtr node,
                                   void * f, const char * url_base, 
                                   char ** filter,
                                   const bg_upnp_client_t * cl)
  {
  xmlNodePtr child;
  char * tmp_string;
  char * path;
  char * path_enc;

  int i = 0;
  bg_db_object_t * obj = f;
  const char * mimetype;
  
  while(pls_formats[i])
    {
    mimetype = bg_ext_to_mimetype(pls_formats[i]);
    
    // fprintf(stderr, "bg_didl_create_res_pls %d\n", i);
    if(bg_upnp_client_supports_mimetype(cl, mimetype))
      {
      path = bg_sprintf("%s/%"PRId64"/%s.%s",
                        pls_formats[i],
                        bg_db_object_get_id(f),
                        obj->label,
                        pls_formats[i]);

      path_enc = bg_string_to_uri(path, -1);
      
      tmp_string = bg_sprintf("%s%s", url_base, path_enc);
      
      child = bg_xml_append_child_node(node, "res", tmp_string);
      free(tmp_string);
      free(path);
      free(path_enc);

      /* Duration (optional) */
      set_res_duration(child, filter, obj->duration);
      
      tmp_string = bg_sprintf("http-get:*:%s:*",
                              bg_upnp_client_translate_mimetype(cl, mimetype));
      BG_XML_SET_PROP(child, "protocolInfo", tmp_string);
      free(tmp_string);
      }
    i++;
    
    if(i && !(cl->flags & BG_UPNP_CLIENT_MULTIPLE_RES))
      return;
    }
  }

#define QUERY_PLS_AS_STREAM (1<<0)


static const char * get_dlna_image_profile(bg_db_object_t * obj)
  {
  bg_db_image_file_t * img = (bg_db_image_file_t*)(obj);
  return bg_didl_get_dlna_image_profile(img->file.mimetype, img->width, img->height);
  }
  
typedef struct
  {
  const char * upnp_parent;
  int parent_type;
  xmlDocPtr didl;
  bg_upnp_device_t * dev;
  char ** filter;
  bg_db_t * db;
  const bg_upnp_client_t * cl;
  int flags;
  char * url_base;
  } query_t;

static char * make_file_url(const char * prefix, void * obj1)
  {
  char * uri;
  bg_db_file_t * f;
  const char * ext = NULL;
  char * path_enc;
  f = obj1;
  ext = strrchr(f->path, '.');
  if(!ext)
    ext = "";

  path_enc = bg_string_to_uri(bg_db_object_get_label(obj1), -1);
  uri = bg_sprintf("%smedia/%"PRId64"/%s%s", prefix, bg_db_object_get_id(obj1),
                   path_enc, ext);

  free(path_enc);
  return uri;
  }

static void add_album_art_url(xmlNodePtr node,
                              bg_db_object_t * cover, query_t * q)
  {
  xmlNodePtr child;
  char * uri;
  const char * dlna_id;
  int64_t id = bg_db_object_get_id(cover);
  
  if(bg_db_object_get_type(cover) == BG_DB_OBJECT_THUMBNAIL)
    {
    uri  = bg_sprintf("%smedia/%"PRId64"/%"PRId64".jpg", q->url_base, id, id);
    }
  else
    {
    uri = make_file_url(q->url_base, cover);
    }
  
  if(!(child = bg_didl_add_element_string(q->didl, node,
                                          "upnp:albumArtURI", uri, NULL)))
    goto fail;

  
  dlna_id = get_dlna_image_profile(cover);
  if(dlna_id)
    {
    xmlNsPtr dlna_ns;
    dlna_ns = xmlNewNs(child,
                       (xmlChar*)"urn:schemas-dlna-org:metadata-1-0/",
                       (xmlChar*)"dlna");
    xmlSetNsProp(child, dlna_ns, (const xmlChar*)"profileID",
                 (const xmlChar*)dlna_id);
    }
  
  fail:
  if(uri)
    free(uri);
  
  }

static void add_album_art(xmlNodePtr node, int64_t cover_id, query_t * q)
  {
  bg_db_object_t * cover;
  bg_db_object_t * cover_thumb; 
  int thumbnail_size = q->cl->album_thumbnail_width;
  if(!thumbnail_size)
    thumbnail_size = 160;

  cover = bg_db_object_query(q->db, cover_id);
  
  cover_thumb = bg_db_get_thumbnail(q->db, cover_id,
                                    thumbnail_size,
                                    thumbnail_size, -1, "image/jpeg");
  
  /* Cover thumbnail */
  if(cover_thumb)
    {
    add_album_art_url(node, cover_thumb, q);
    bg_db_object_unref(cover_thumb);
    }

  /* Original size */
  add_album_art_url(node, cover, q);
  bg_db_object_unref(cover);
  }

static void add_movie_art(xmlNodePtr node, int64_t poster_id, query_t * q)
  {
  void * poster_thumb;
  void * poster;
  int flags;
  
  poster = bg_db_object_query(q->db, poster_id);
  poster_thumb = bg_db_get_thumbnail(q->db, poster_id, 220, 330, -1, "image/jpeg");

  flags = q->cl->flags;
  flags &= ~BG_UPNP_CLIENT_ORIG_LOCATION;

  if(poster)
    {
    bg_didl_create_res_file(q->didl, node, poster, q->url_base,
                            q->filter, q->cl, flags);

    bg_db_object_unref(poster);
    }
  if(poster_thumb)
    {
    bg_didl_create_res_file(q->didl, node, poster_thumb, q->url_base,
                            q->filter, q->cl, flags);

    bg_db_object_unref(poster_thumb);
    }
  }

static void add_video_info(query_t * q, xmlNodePtr node,
                           const bg_db_video_info_t * info)
  {
  int i;
  bg_didl_set_title(q->didl, node, info->title);

  /* Genre */
  if(info->genres)
    {
    i = 0;
    while(info->genres[i])
      {
      bg_didl_add_element_string(q->didl, node, "upnp:genre",
                                 info->genres[i], q->filter);
      i++;
      }
    }
  /* Directors */
  if(info->directors)
    {
    i = 0;
    while(info->directors[i])
      {
      bg_didl_add_element_string(q->didl, node, "upnp:director",
                                 info->directors[i], q->filter);
      i++;
      }
    }
  /* Actors */
  if(info->actors)
    {
    i = 0;
    while(info->actors[i])
      {
      bg_didl_add_element_string(q->didl, node, "upnp:actor",
                                 info->actors[i], q->filter);
      i++;
      }
    }
  /* Plot */
  if(info->plot)
    bg_didl_add_element_string(q->didl, node, "upnp:longDescription",
                               info->plot, q->filter);

  /* Date */
  bg_didl_set_date(q->didl, node, &info->date, q->filter);

  }

static int num_fake_children(bg_db_object_t * obj);

static const struct
  {
  bg_db_object_type_t type;
  const char ** search_classes;
  }
searchclasses[] =
  {
    {BG_DB_OBJECT_AUDIO_ALBUM, (const char*[]){ "object.container.album.musicAlbum",    NULL } },
    {BG_DB_OBJECT_AUDIO_FILE,  (const char*[]){ "object.item.audioItem.musicTrack",     NULL } },
    {BG_DB_OBJECT_PLAYLIST,    (const char*[]){ "object.container.playlistContainer",   NULL } },
    {BG_DB_OBJECT_RADIO_URL,   (const char*[]){ "object.item.audioItem.audioBroadcast", NULL } },
    {BG_DB_OBJECT_MOVIE,       (const char*[]){ "object.item.videoItem.movie", "object.container.album.videoAlbum", NULL } },
    {BG_DB_OBJECT_TVSERIES,    (const char*[]){ "object.item.videoItem.movie", NULL } },
    {BG_DB_OBJECT_PHOTO,       (const char*[]){ "object.item.imageItem.photo", NULL } },
    {BG_DB_OBJECT_DIRECTORY,   (const char*[]){ "object.container.storageFolder", NULL } },
    { /* End */ }
  };




/* 
 *  UPNP IDs are like paths witht the numeric IDs as elements:
 *
 *  0/10/100289
 *  
 *  This was we can map the same db objects into arbitrary locations into the
 *  tree and they always have unique upnp IDs as required by the standard.
 */

static
xmlNodePtr bg_didl_add_object(query_t * q, bg_db_object_t * obj, 
                              const char * upnp_parent, const char * upnp_id)
  {
  const char * pos;
  char * tmp_string;
  xmlNodePtr node;
  xmlNodePtr child;
  bg_db_object_type_t type;
  int is_container = 0;
  
  type = bg_db_object_get_type(obj);

  if(type & (BG_DB_FLAG_CONTAINER|BG_DB_FLAG_VCONTAINER))
    {
    if((type != BG_DB_OBJECT_PLAYLIST) ||
       !(q->flags & QUERY_PLS_AS_STREAM))
      is_container = 1;
    }
  else
    is_container = 0;
  
  if(is_container)
    node = bg_didl_add_container(q->didl);
  else
    node = bg_didl_add_item(q->didl);
  
  if(upnp_id)
    {
    BG_XML_SET_PROP(node, "id", upnp_id);
    pos = strrchr(upnp_id, '/');
    if(pos)
      {
      tmp_string = gavl_strndup(upnp_id, pos);
      BG_XML_SET_PROP(node, "parentID", tmp_string);
      free(tmp_string);
      }
    else
      BG_XML_SET_PROP(node, "parentID", "-1");
    }
  else if(upnp_parent)
    {
    tmp_string = bg_sprintf("%s/%"PRId64"", upnp_parent, obj->id);
    BG_XML_SET_PROP(node, "id", tmp_string);
    free(tmp_string);
    BG_XML_SET_PROP(node, "parentID", upnp_parent);
    }
  /* Required */
  BG_XML_SET_PROP(node, "restricted", "1");

  /* Optional */
  if(is_container &&
     bg_didl_filter_attribute("container", "childCount", q->filter))
    bg_didl_set_attribute_int(node, "childCount",
                              obj->children + num_fake_children(obj));
  
  switch(type)
    {
    case BG_DB_OBJECT_AUDIO_FILE:
      {
      bg_db_audio_file_t * o = (bg_db_audio_file_t *)obj;
#if 0
      if(o->artist && o->title)
        {
        tmp_string = bg_sprintf("%s - %s", o->artist, o->title);
        bg_didl_set_title(q->didl, node,  tmp_string);
        free(tmp_string);
        }
      else
#endif
      if(o->title)
        bg_didl_set_title(q->didl, node,  o->title);
      else
        bg_didl_set_title(q->didl, node,  bg_db_object_get_label(obj));
      bg_didl_set_class(q->didl, node,  "object.item.audioItem.musicTrack");

      //      bg_didl_set_class(didl, node,  "object.item.audioItem");

      bg_didl_create_res_file(q->didl, node, obj, q->url_base,
                              q->filter, q->cl, q->cl->flags);
      
      if(o->artist)
        {
        bg_didl_add_element_string(q->didl, node, "upnp:artist", o->artist, q->filter);
        bg_didl_add_element_string(q->didl, node, "dc:creator", o->artist, q->filter);
        }
      if(o->genre)
        bg_didl_add_element_string(q->didl, node, "upnp:genre", o->genre, q->filter);
      if(o->album)
        bg_didl_add_element_string(q->didl, node, "upnp:album", o->album, q->filter);

      if(o->track > 0)
        bg_didl_add_element_int(q->didl, node, "upnp:originalTrackNumber", o->track,
                             q->filter);
      
      bg_didl_set_date(q->didl, node, &o->date, q->filter);
#if 1
      /* Album art */
      if(o->album && bg_didl_filter_element("upnp:albumArtURI", q->filter))
        {
        bg_db_audio_album_t * album = bg_db_object_query(q->db, o->album_id);
        if(album)
          {
          if(album->cover_id > 0)
            add_album_art(node, album->cover_id, q);
          bg_db_object_unref(album);
          }
        }
#endif
      //      bg_didl_create_res_file(didl, node, obj, q->url_base, q->filter, q->cl);
      }
      break;
    case BG_DB_OBJECT_VIDEO_FILE:
    case BG_DB_OBJECT_PHOTO:
      bg_didl_set_title(q->didl, node,  bg_db_object_get_label(obj));
      bg_didl_set_class(q->didl, node,  "object.item");
      break;
    case BG_DB_OBJECT_ROOT:
      {
      bg_didl_set_title(q->didl, node,  q->dev->name);
      bg_didl_set_class(q->didl, node,  "object.container");
      }
      break;
    case BG_DB_OBJECT_AUDIO_ALBUM:
      {
      bg_db_audio_album_t * o = (bg_db_audio_album_t *)obj;
      bg_didl_set_title(q->didl, node, o->title);
      bg_didl_set_class(q->didl, node, "object.container.album.musicAlbum");
      bg_didl_set_date(q->didl, node, &o->date, q->filter);
      if(o->artist)
        {
        bg_didl_add_element_string(q->didl, node, "upnp:artist", o->artist, q->filter);
        bg_didl_add_element_string(q->didl, node, "dc:creator", o->artist, q->filter);
        }
      if(o->genre)
        bg_didl_add_element_string(q->didl, node, "upnp:genre", o->genre, q->filter);

      if((o->cover_id > 0) && bg_didl_filter_element("upnp:albumArtURI", q->filter))
        add_album_art(node, o->cover_id, q);

      bg_didl_create_res_pls(q->didl, node, o, q->url_base, q->filter, q->cl);
      
      if((child = bg_didl_add_element_string(q->didl, node, "upnp:searchClass",
                                          "object.item.audioItem.musicTrack",
                                          q->filter)))
        BG_XML_SET_PROP(child, "includeDerived", "false");

      }
      break;
    case BG_DB_OBJECT_CONTAINER:
      bg_didl_set_title(q->didl, node,  bg_db_object_get_label(obj));
      bg_didl_set_class(q->didl, node,  "object.container");
      break;
    case BG_DB_OBJECT_DIRECTORY:
      bg_didl_set_title(q->didl, node,  bg_db_object_get_label(obj));
      bg_didl_set_class(q->didl, node,  "object.container.storageFolder");
      /* Optional */
      bg_didl_add_element_int(q->didl, node, "upnp:storageUsed", obj->size, q->filter);
      break;
    case BG_DB_OBJECT_PLAYLIST:
      
      bg_didl_set_title(q->didl, node,  bg_db_object_get_label(obj));
      
      if(q->flags & QUERY_PLS_AS_STREAM)
        {
        bg_didl_set_class(q->didl, node,
                          "object.item.audioItem.audioBroadcast");

        /* Res */
        if(bg_didl_filter_element("res", q->filter))
          {
          char * path = NULL;
          int num = 0;
          
          path = bg_sprintf("ondemand/%"PRId64"/stream",
                            bg_db_object_get_id(obj));
          

          if(!num || (q->cl->flags & BG_UPNP_CLIENT_MULTIPLE_RES))
            {
            /* Recompressors */
            bg_didl_create_res_recompress(q->didl,
                                          node,
                                          obj,
                                          q->url_base, 
                                          path,
                                          "audio/gavf",
                                          q->filter,
                                          q->cl);
            }
          }
        
        }
      else
        {
        bg_didl_set_class(q->didl, node,
                          "object.container.playlistContainer");
        bg_didl_create_res_pls(q->didl, node, obj, q->url_base,
                               q->filter, q->cl);
        }
      break;
    case BG_DB_OBJECT_RADIO_URL:
      {
      bg_db_url_t * u = (bg_db_url_t *)obj;
      bg_didl_set_title(q->didl, node, bg_db_object_get_label(obj));

      bg_didl_set_class(q->didl, node,
                        "object.item.audioItem.audioBroadcast");
      /* Res */
      /* Original format */

      if(bg_didl_filter_element("res", q->filter))
        {
        char * path = NULL;
        char * tmp_string;
        int bitrate_i;
        const char * ext = bg_mimetype_to_ext(u->mimetype);

        path = bg_sprintf("ondemand/%"PRId64"/stream",
                          bg_db_object_get_id(obj));
        
        tmp_string = bg_sprintf("%s%s.%s",
                                q->url_base, path, ext);
        child = bg_xml_append_child_node(node, "res", tmp_string);
        free(tmp_string);

        tmp_string = bg_sprintf("http-get:*:%s:*", u->mimetype);
        BG_XML_SET_PROP(child, "protocolInfo", tmp_string);
        free(tmp_string);

        set_res_format(child, u, q->filter, NULL, NULL);

        if(bg_didl_filter_attribute("res", "bitrate", q->filter) &&
           u->audio_bitrate && isdigit(u->audio_bitrate[0]) && ((bitrate_i = atoi(u->audio_bitrate)) > 0))
          bg_didl_set_attribute_int(child, "bitrate", (1000 * bitrate_i) / 8);

        bg_didl_create_res_recompress(q->didl,
                                      node,
                                      obj,
                                      q->url_base, 
                                      path,
                                      u->mimetype,
                                      q->filter,
                                      q->cl);
        }
      
      }
      break;
    case BG_DB_OBJECT_VFOLDER:
      bg_didl_set_title(q->didl, node,  bg_db_object_get_label(obj));
      bg_didl_set_class(q->didl, node,  "object.container");
      break;
    case BG_DB_OBJECT_VFOLDER_LEAF:
      {
      bg_db_vfolder_t * f;
      f = (bg_db_vfolder_t*)obj;

      if(((f->type == BG_DB_OBJECT_AUDIO_FILE) ||
          (f->type == BG_DB_OBJECT_AUDIO_ALBUM)) &&
         (f->path[f->depth-1].cat == BG_DB_CAT_ARTIST))
        {
        bg_didl_set_class(q->didl, node,  "object.container.person.musicArtist");
        }
      else
        bg_didl_set_class(q->didl, node,  "object.container");
      
      bg_didl_set_title(q->didl, node,  bg_db_object_get_label(obj));
      bg_didl_set_class(q->didl, node,  "object.container");
      
      }
      break;
    case BG_DB_OBJECT_ROOTFOLDER:
      {
      int i = 0, j;
      xmlNodePtr n;
      bg_db_rootfolder_t * f;
      f = (bg_db_rootfolder_t*)obj;
      
      bg_didl_set_title(q->didl, node,  bg_db_object_get_label(obj));
      bg_didl_set_class(q->didl, node,  "object.container");
      
      if(bg_didl_filter_element("upnp:searchClass", q->filter))
        {
        while(searchclasses[i].type)
          {
          if(searchclasses[i].type == f->child_type)
            break;
          i++;
          }

        if(searchclasses[i].type)
          {
          j = 0;
          while(searchclasses[i].search_classes[j])
            {
            n = bg_didl_add_element(q->didl, node,
                                    "upnp:searchClass",
                                    searchclasses[i].search_classes[j]);
            BG_XML_SET_PROP(n, "includeDerived", "false");
            j++;
            }
          }
        }
      
      }
      break;
    case BG_DB_OBJECT_MOVIE:
      {
      bg_db_movie_t * o = (bg_db_movie_t*)obj;
      bg_didl_set_class(q->didl, node,  "object.item.videoItem.movie");
      add_video_info(q, node, &o->info);
      bg_didl_create_res_file(q->didl, node, obj,
                              q->url_base, q->filter, q->cl, q->cl->flags);
      
      if((o->info.poster_id > 0) && (q->cl->flags & BG_UPNP_CLIENT_MULTIPLE_RES))
        add_movie_art(node, o->info.poster_id, q);
      }
      break;
    case BG_DB_OBJECT_MOVIE_MULTIPART:
      {
      bg_db_video_container_t * o = (bg_db_video_container_t*)obj;
      bg_didl_set_class(q->didl, node,  "object.container.album.videoAlbum");
      bg_didl_create_res_pls(q->didl, node, o, q->url_base, q->filter, q->cl);
      add_video_info(q, node, &o->info);
      if((o->info.poster_id > 0) && (q->cl->flags & BG_UPNP_CLIENT_MULTIPLE_RES))
        add_movie_art(node, o->info.poster_id, q);

      }
      break;
    case BG_DB_OBJECT_TVSERIES:
      {
      bg_db_video_container_t * o = (bg_db_video_container_t*)obj;
      bg_didl_set_class(q->didl, node,  "object.container.album.videoAlbum");
      add_video_info(q, node, &o->info);
      bg_didl_add_element_string(q->didl, node, "upnp:seriesTitle",
                                 o->info.title, q->filter);

      if((o->info.poster_id > 0) && (q->cl->flags & BG_UPNP_CLIENT_MULTIPLE_RES))
        add_movie_art(node, o->info.poster_id, q);
      }
      break;
    case BG_DB_OBJECT_SEASON:
      {
      bg_db_video_container_t * o = (bg_db_video_container_t*)obj;
      bg_didl_set_class(q->didl, node,  "object.container.album.videoAlbum");
      add_video_info(q, node, &o->info);
      bg_didl_create_res_pls(q->didl, node, obj,
                             q->url_base, q->filter, q->cl);

      if((o->info.poster_id > 0) && (q->cl->flags & BG_UPNP_CLIENT_MULTIPLE_RES))
        add_movie_art(node, o->info.poster_id, q);
      
      if(bg_didl_filter_element("upnp:seriesTitle", q->filter))
        {
        bg_db_video_container_t * parent = bg_db_object_query(q->db, obj->parent_id);
        bg_didl_add_element_string(q->didl, node, "upnp:seriesTitle",
                                   parent->info.title, q->filter);
        bg_db_object_unref(parent);
        }
      }
      break;
    case BG_DB_OBJECT_VIDEO_EPISODE:
      {
      bg_db_movie_t * o = (bg_db_movie_t*)obj;
      bg_didl_set_class(q->didl, node,  "object.item.videoItem.movie");
      add_video_info(q, node, &o->info);
      bg_didl_create_res_file(q->didl, node, obj, q->url_base,
                              q->filter, q->cl, q->cl->flags);
      bg_didl_add_element_int(q->didl, node, "upnp:episodeNumber",
                              o->file.index, q->filter);
      
      //      if(o->info.poster_id > 0)
      //        add_movie_art(node, o->info.poster_id, q);
      }
      break;
    case BG_DB_OBJECT_MOVIE_PART:
      {
      // bg_db_video_file_t * o = (bg_db_video_file_t*)obj;
      bg_didl_set_title(q->didl, node,  bg_db_object_get_label(obj));
      bg_didl_set_class(q->didl, node,  "object.item.videoItem.movie");
      bg_didl_create_res_file(q->didl, node, obj, q->url_base,
                              q->filter, q->cl, q->cl->flags);
      }
      break;
    /* The following objects should never get returned */
    default:
      bg_didl_set_title(q->didl, node,  bg_db_object_get_label(obj));
      bg_didl_set_class(q->didl, node,  "object.item");
      break;
    }

  return NULL;
  }

static void query_callback(void * priv, void * obj)
  {
  query_t * q = priv;
  bg_didl_add_object(q, obj, q->upnp_parent, NULL);
  }

/* We add some fake objects, which are not present in the dabatase */

static int num_fake_children(bg_db_object_t * obj)
  {
  bg_db_object_type_t type;
  type = bg_db_object_get_type(obj);
  if(type == BG_DB_OBJECT_ROOTFOLDER)
    {
    bg_db_rootfolder_t * vf;
    vf = (bg_db_rootfolder_t *)obj;
    if(vf->child_type == BG_DB_OBJECT_RADIO_URL)
      return 1;
    }
  return 0;
  }

/* Add a fake object to a parent container */
static void add_playlist_streams_container(bg_db_object_t * parent, query_t * q,
                                           const char * id, const char * parent_id)
  {
  char * tmp_string;
  xmlNodePtr node;
  node = bg_didl_add_container(q->didl);
  bg_didl_set_title(q->didl, node,  "Playlist streams");
  bg_didl_set_class(q->didl, node,  "object.container");

  if(parent_id)
    {
    tmp_string = bg_sprintf("%s/pls-streams", parent_id);
    BG_XML_SET_PROP(node, "id", tmp_string);
    free(tmp_string);
    }
  else
    BG_XML_SET_PROP(node, "id", id);

  BG_XML_SET_PROP(node, "restricted", "1");

  if(bg_didl_filter_attribute("container", "childCount", q->filter))
    bg_didl_set_attribute_int(node, "childCount", parent->children);
  }

static void add_fake_child(bg_db_object_t * parent,
                           query_t * q, const char * parent_id,
                           int index)
  {
  bg_db_object_type_t type;
    
  type = bg_db_object_get_type(parent);
  if(type == BG_DB_OBJECT_ROOTFOLDER)
    {
    bg_db_rootfolder_t * vf;
    vf = (bg_db_rootfolder_t *)parent;
    if(vf->child_type == BG_DB_OBJECT_RADIO_URL)
      add_playlist_streams_container(parent, q, NULL, parent_id);
    }
  }

/* Get children of a fake container */
static int browse_fake_children(query_t * q,
                                const char * upnp_id, int start,
                                int num, int * total_matches)
  {
  int ret = 0;
  const char * pos;
  
  if((pos = strrchr(upnp_id, '/')) && !strcmp(pos, "/pls-streams"))
    {
    bg_db_object_t * parent;
    q->upnp_parent = upnp_id;
    q->flags |= QUERY_PLS_AS_STREAM;

    parent = bg_db_rootfolder_get(q->db, BG_DB_OBJECT_PLAYLIST);

    if(parent)
      {
      ret = bg_db_query_children(q->db, parent, query_callback, q,
                                 start, num, total_matches);
      bg_db_object_unref(parent);
      }
    }
  return ret;
  }

/* Get children of a fake container */
static int browse_fake_metadata(query_t * q,
                                const char * upnp_id)
  {
  const char * pos;

  if((pos = strrchr(upnp_id, '/')) && !strcmp(pos, "/pls-streams"))
    {
    int64_t id;
    bg_db_object_t * obj;
    
    pos--;
    while(isdigit(*pos) && (pos > upnp_id))
      pos--;

    if(!isdigit(*pos))
      pos++;
    
    id = strtoll(pos, NULL, 10);
    
    obj = bg_db_object_query(q->db, id);
    add_playlist_streams_container(obj, q, upnp_id, NULL);
    bg_db_object_unref(obj);
    }
  return 1;
  }
  
static int Browse(bg_upnp_service_t * s)
  {
  bg_mediaserver_t * priv;
  int64_t id;
  char * ret;  
  query_t q;
  const char * ObjectID =
    bg_upnp_service_get_arg_in_string(&s->req, ARG_ObjectID);
  const char * BrowseFlag =
    bg_upnp_service_get_arg_in_string(&s->req, ARG_BrowseFlag);
  const char * Filter =
    bg_upnp_service_get_arg_in_string(&s->req, ARG_Filter);
  int StartingIndex =
    bg_upnp_service_get_arg_in_int(&s->req, ARG_StartingIndex);
  int RequestedCount =
    bg_upnp_service_get_arg_in_int(&s->req, ARG_RequestedCount);

  int NumberReturned = 0;
  int TotalMatches = 0;
  
  priv = s->dev->priv;
  
  //  fprintf(stderr, "Browse: Id: %s, Flag: %s, Filter: %s, Start: %d, Num: %d\n",
  //          ObjectID, BrowseFlag, Filter, StartingIndex, RequestedCount);

  if(!ObjectID)
    return 0;
  
  memset(&q, 0, sizeof(q));
  
  q.dev = s->dev;

  if(strcmp(Filter, "*"))
    q.filter = bg_strbreak(Filter, ',');
  else
    q.filter = NULL;
  
  q.db = priv->db;
  q.cl = bg_upnp_detect_client(s->req.req);
  q.didl = bg_didl_create();

  //  if((var = gavl_dictionary_get_string(s->req.req, "Host")))
  //    q.url_base = bg_sprintf("http://%s/", var);
  //  else
  q.url_base = gavl_strdup(q.dev->url_base);
  
  bg_db_start_transaction(priv->db);
  
  if(isdigit(ObjectID[strlen(ObjectID)-1]))
    {
    const char * pos;
    bg_db_object_t * obj;

    if((pos = strrchr(ObjectID, '/')))
      pos++;
    else
      pos = ObjectID;
    
    id = strtoll(pos, NULL, 10);
    
    if(!strcmp(BrowseFlag, "BrowseMetadata"))
      {
      obj = bg_db_object_query(priv->db, id);

      if(strstr(ObjectID, "pls-streams"))
        q.flags |= QUERY_PLS_AS_STREAM;
            
      bg_didl_add_object(&q, obj, NULL, ObjectID);
      bg_db_object_unref(obj);
      NumberReturned = 1;
      TotalMatches = 1;
      }
    else
      {
      int num_fake, i;
      q.upnp_parent = ObjectID;
      obj = bg_db_object_query(priv->db, id);
      NumberReturned =
        bg_db_query_children(priv->db, obj,
                             query_callback, &q,
                             StartingIndex, RequestedCount, &TotalMatches);
      num_fake = num_fake_children(obj);
      
      TotalMatches += num_fake;
            
      for(i = 0; i < num_fake; i++)
        {
        if(RequestedCount && (NumberReturned == RequestedCount))
          break;
        
        add_fake_child(obj, &q, ObjectID, i);
        NumberReturned++;
        }
      bg_db_object_unref(obj);
      }
    }
  else
    {
    if(!strcmp(BrowseFlag, "BrowseMetadata"))
      {
      browse_fake_metadata(&q, ObjectID);
      NumberReturned = 1;
      TotalMatches = 1;
      }
    else
      {
      NumberReturned =
        browse_fake_children(&q, ObjectID, StartingIndex, RequestedCount, &TotalMatches);
      }
    }
  
  ret = bg_xml_save_to_memory_opt(q.didl, XML_SAVE_NO_DECL);
  
  bg_upnp_service_set_arg_out_int(&s->req, ARG_NumberReturned, NumberReturned);
  bg_upnp_service_set_arg_out_int(&s->req, ARG_TotalMatches, TotalMatches);
  
  fprintf(stderr, "didl-test:\n%s\n", ret);

  //  fprintf(stderr, "NumberReturned: %d, TotalMatches: %d\n",
  //             NumberReturned, TotalMatches);
  
  // Remove trailing linebreak
  if(ret[strlen(ret)-1] == '\n')
    ret[strlen(ret)-1] = '\0';
  
  xmlFreeDoc(q.didl);

  if(q.filter)
    bg_strbreak_free(q.filter);
  if(q.url_base)
    free(q.url_base);
  
  bg_db_end_transaction(priv->db);
  
  bg_upnp_service_set_arg_out_string(&s->req, ARG_Result, ret);
  bg_upnp_service_set_arg_out_int(&s->req, ARG_UpdateID, 0);
  
  //  fprintf(stderr, "didl:\n%s\n", ret);
  return 1;
  }


/* Initialize service description */

static void init_service_desc(bg_upnp_service_desc_t * d)
  {
  bg_upnp_sv_val_t  val;
  bg_upnp_sv_t * sv;
  bg_upnp_sa_desc_t * sa;

  /*
    bg_upnp_service_desc_add_sv(d, "TransferIDs",
                                BG_UPNP_SV_EVENT,
                                BG_UPNP_SV_STRING);

   */
  
  bg_upnp_service_desc_add_sv(d, "A_ARG_TYPE_ObjectID",
                              BG_UPNP_SV_ARG_ONLY,
                              BG_UPNP_SV_STRING);
  bg_upnp_service_desc_add_sv(d, "A_ARG_TYPE_Result",
                              BG_UPNP_SV_ARG_ONLY,
                              BG_UPNP_SV_STRING);
  //  Optional
  //  bg_upnp_service_desc_add_sv(d, "A_ARG_TYPE_SearchCriteria",
  //                              BG_UPNP_SV_ARG_ONLY,
  //                              BG_UPNP_SV_STRING);

  sv = bg_upnp_service_desc_add_sv(d, "A_ARG_TYPE_BrowseFlag",
                                   BG_UPNP_SV_ARG_ONLY,
                                   BG_UPNP_SV_STRING);

  val.s = "BrowseMetadata";
  bg_upnp_sv_add_allowed(sv, &val);
  val.s = "BrowseDirectChildren";
  bg_upnp_sv_add_allowed(sv, &val);
  
  bg_upnp_service_desc_add_sv(d, "A_ARG_TYPE_Filter",
                              BG_UPNP_SV_ARG_ONLY,
                              BG_UPNP_SV_STRING);
  bg_upnp_service_desc_add_sv(d, "A_ARG_TYPE_SortCriteria",
                              BG_UPNP_SV_ARG_ONLY,
                              BG_UPNP_SV_STRING);
  bg_upnp_service_desc_add_sv(d, "A_ARG_TYPE_Index",
                              BG_UPNP_SV_ARG_ONLY,
                              BG_UPNP_SV_INT4);
  bg_upnp_service_desc_add_sv(d, "A_ARG_TYPE_Count",
                              BG_UPNP_SV_ARG_ONLY,
                              BG_UPNP_SV_INT4);
  bg_upnp_service_desc_add_sv(d, "A_ARG_TYPE_UpdateID",
                              BG_UPNP_SV_ARG_ONLY,
                              BG_UPNP_SV_INT4);
  /*
  bg_upnp_service_desc_add_sv(d, "A_ARG_TYPE_TransferID",
                              BG_UPNP_SV_ARG_ONLY,
                              BG_UPNP_SV_INT4);
  bg_upnp_service_desc_add_sv(d, "A_ARG_TYPE_TransferStatus",
                              BG_UPNP_SV_ARG_ONLY,
                              BG_UPNP_SV_STRING);
  bg_upnp_service_desc_add_sv(d, "A_ARG_TYPE_TransferLength",
                              BG_UPNP_SV_ARG_ONLY,
                              BG_UPNP_SV_STRING);
  bg_upnp_service_desc_add_sv(d, "A_ARG_TYPE_TransferTotal",
                              BG_UPNP_SV_ARG_ONLY,
                              BG_UPNP_SV_STRING);
  bg_upnp_service_desc_add_sv(d, "A_ARG_TYPE_TagValueList",
                              BG_UPNP_SV_ARG_ONLY,
                              BG_UPNP_SV_STRING);
  bg_upnp_service_desc_add_sv(d, "A_ARG_TYPE_URI",
                              BG_UPNP_SV_ARG_ONLY,
                              BG_UPNP_SV_STRING);
  */

  bg_upnp_service_desc_add_sv(d, "SearchCapabilities",
                              0, BG_UPNP_SV_STRING);
  bg_upnp_service_desc_add_sv(d, "SortCapabilities",
                              0, BG_UPNP_SV_STRING);
  bg_upnp_service_desc_add_sv(d, "SystemUpdateID",
                              BG_UPNP_SV_EVENT, BG_UPNP_SV_INT4);
  /*
  bg_upnp_service_desc_add_sv(d, "ContainerUpdateIDs",
                              BG_UPNP_SV_EVENT, BG_UPNP_SV_STRING);
  */

  /* Actions */

  sa = bg_upnp_service_desc_add_action(d, "GetSearchCapabilities",
                                       GetSearchCapabilities);

  bg_upnp_sa_desc_add_arg_out(sa, "SearchCaps",
                              "SearchCapabilities", 0,
                              ARG_SearchCaps);

  sa = bg_upnp_service_desc_add_action(d, "GetSortCapabilities",
                                       GetSortCapabilities);

  bg_upnp_sa_desc_add_arg_out(sa, "SortCaps",
                              "SortCapabilities", 0,
                              ARG_SortCaps);
  
  sa = bg_upnp_service_desc_add_action(d, "GetSystemUpdateID",
                                       GetSystemUpdateID);

  bg_upnp_sa_desc_add_arg_out(sa, "Id",
                              "SystemUpdateID", 0,
                              ARG_Id);

  sa = bg_upnp_service_desc_add_action(d, "Browse", Browse);
  bg_upnp_sa_desc_add_arg_in(sa, "ObjectID",
                             "A_ARG_TYPE_ObjectID", 0,
                             ARG_ObjectID);
  bg_upnp_sa_desc_add_arg_in(sa, "BrowseFlag",
                             "A_ARG_TYPE_BrowseFlag", 0,
                             ARG_BrowseFlag);
  bg_upnp_sa_desc_add_arg_in(sa, "Filter",
                             "A_ARG_TYPE_Filter", 0,
                             ARG_Filter);
  bg_upnp_sa_desc_add_arg_in(sa, "StartingIndex",
                             "A_ARG_TYPE_Index", 0,
                             ARG_StartingIndex);
  bg_upnp_sa_desc_add_arg_in(sa, "RequestedCount",
                             "A_ARG_TYPE_Count", 0,
                             ARG_RequestedCount);
  bg_upnp_sa_desc_add_arg_in(sa, "SortCriteria",
                             "A_ARG_TYPE_SortCriteria", 0,
                             ARG_SortCriteria);
  
  bg_upnp_sa_desc_add_arg_out(sa, "Result",
                              "A_ARG_TYPE_Result", 0,
                              ARG_Result);
  bg_upnp_sa_desc_add_arg_out(sa, "NumberReturned",
                              "A_ARG_TYPE_Count", 0,
                              ARG_NumberReturned);
  bg_upnp_sa_desc_add_arg_out(sa, "TotalMatches",
                              "A_ARG_TYPE_Count", 0,
                              ARG_TotalMatches);
  bg_upnp_sa_desc_add_arg_out(sa, "UpdateID",
                              "A_ARG_TYPE_UpdateID", 0,
                              ARG_UpdateID);
  
  /*

    sa = bg_upnp_service_desc_add_action(d, "Search");
    sa = bg_upnp_service_desc_add_action(d, "CreateObject");
    sa = bg_upnp_service_desc_add_action(d, "DestroyObject");
    sa = bg_upnp_service_desc_add_action(d, "UpdateObject");
    sa = bg_upnp_service_desc_add_action(d, "ImportResource");
    sa = bg_upnp_service_desc_add_action(d, "ExportResource");
    sa = bg_upnp_service_desc_add_action(d, "StopTransferResource");
    sa = bg_upnp_service_desc_add_action(d, "GetTransferProgress");
  */

  
  
  }


void bg_upnp_service_init_content_directory(bg_upnp_service_t * ret,
                                            bg_db_t * db)
  {
  bg_upnp_service_init(ret, BG_UPNP_SERVICE_ID_CD, "ContentDirectory", 1);
  init_service_desc(&ret->desc);
  bg_upnp_service_start(ret);
  }

/* Client side */

static int browse_upnp(const char * url, const char * id, int self, xmlDocPtr * ret_xml, char ** ret_str)
  {
  int ret = 0;
  const char * browse_flag = self ? "BrowseMetadata" : "BrowseDirectChildren";
  const char * didl;

  bg_soap_t soap;

  bg_soap_init(&soap, url, "ContentDirectory", 1, "Browse");

  bg_soap_request_add_argument(soap.req, "ObjectID", id);
  bg_soap_request_add_argument(soap.req, "BrowseFlag", browse_flag);
  bg_soap_request_add_argument(soap.req, "Filter", "*");
  bg_soap_request_add_argument(soap.req, "StartingIndex", "0");
  bg_soap_request_add_argument(soap.req, "RequestedCount", "0");
  bg_soap_request_add_argument(soap.req, "SortCriteria", "");
  
  if(!bg_soap_request(&soap))
    goto fail;

  if(!(didl = bg_soap_response_get_argument(soap.res, "Result")))
    goto fail;
  
#ifdef DUMP_DIDL
  fprintf(stderr, "Got DIDL: %s\n", didl);
#endif

  if(ret_str)
    *ret_str = gavl_strdup(didl);
  else
    *ret_xml = xmlParseMemory(didl, strlen(didl));

  ret = 1;
  fail:
  
  bg_soap_free(&soap);
  return ret;
  }

char * bg_upnp_contentdirectory_browse_str(const char * url, const char * id, int self)
  {
  char * ret = NULL;
  
  if(browse_upnp(url, id, self, NULL, &ret))
    return ret;
  else
    return NULL;
  }

xmlDocPtr bg_upnp_contentdirectory_browse_xml(const char * url, const char * id, int self)
  {
  xmlDocPtr ret = NULL;

  if(browse_upnp(url, id, self, &ret, NULL))
    return ret;
  else
    return NULL;
  }
