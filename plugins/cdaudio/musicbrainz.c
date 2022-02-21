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
#include <stdlib.h>
#include <musicbrainz5/mb5_c.h>

#include <config.h>
#include <gmerlin/translation.h>
#include <gmerlin/log.h>
#include <gmerlin/bggavl.h>

#include <gmerlin/http.h>


#define LOG_DOMAIN "musicbrainz"
#include <gmerlin/utils.h>
#include "cdaudio.h"
#include <gavl/metatags.h>


#if 0
/*
 * Test CDindex generation (the example from http://musicbrainz.org/disc.html)
 *
 * Return value must be "MUtMmKN402WPj3_VFsgUelxpc8U-"
 */

static void test_cdindex()
  {
  bg_cdaudio_index_t * idx;
  char disc_id[DISCID_SIZE];

  idx = calloc(1, sizeof(*idx));
  idx->num_tracks = 15;
  idx->tracks = calloc(idx->num_tracks, sizeof(*(idx->tracks)));

  idx->tracks[0].first_sector = 0+150;
  idx->tracks[1].first_sector = 18641+150;
  idx->tracks[2].first_sector = 34667+150;
  idx->tracks[3].first_sector = 56350+150;
  idx->tracks[4].first_sector = 77006+150;
  idx->tracks[5].first_sector = 106094+150;
  idx->tracks[6].first_sector = 125729+150;
  idx->tracks[7].first_sector = 149785+150;
  idx->tracks[8].first_sector = 168885+150;
  idx->tracks[9].first_sector = 185910+150;
  idx->tracks[10].first_sector = 205829+150;
  idx->tracks[11].first_sector = 230142+150;
  idx->tracks[12].first_sector = 246659+150;
  idx->tracks[13].first_sector = 265614+150;
  idx->tracks[14].first_sector = 289479+150;
  idx->tracks[14].last_sector  = 325731+150;
  get_cdindex_id(idx, disc_id);


  free(idx->tracks);
  free(idx);
  }
#endif

#define GET_STRING(func, arg, ret)                  \
 ret = NULL;                                        \
 RequiredLength = func(arg,ret,0);                  \
 ret = malloc(RequiredLength+1);                    \
 func(arg,ret,RequiredLength+1)

static void get_artists(Mb5ArtistCredit c, gavl_dictionary_t * dict, const char * key)
  {
  int RequiredLength=0;
  char * name;
  int num, i;
  Mb5NameCredit cr;
  Mb5Artist a;
  Mb5NameCreditList list = mb5_artistcredit_get_namecreditlist(c);

  num = mb5_namecredit_list_size(list);

  for(i = 0; i < num; i++)
    {
    if((cr = mb5_namecredit_list_item(list, i)) &&
       (a = mb5_namecredit_get_artist(cr)))
      {
      GET_STRING(mb5_artist_get_name, a, name);
      if(name)
        gavl_dictionary_append_string_array_nocopy(dict, key, name);
      }
    }
  }

static void fetch_cover_art(gavl_dictionary_t * m, const char * id)
  {
  int i;
  gavl_buffer_t buf;
  gavl_value_t val;
  const gavl_dictionary_t * dict;
  const gavl_dictionary_t * image;
  const gavl_array_t * images;
  int val_i;
  json_object * obj = NULL;

  char * uri = NULL;
  const char * src = NULL;
  const char * mimetype = NULL;

  gavl_buffer_init(&buf);
  gavl_value_init(&val);
  
  uri = bg_sprintf("http://coverartarchive.org/release/%s", id);

  if(!(obj = bg_json_from_url(uri, NULL)))
    goto fail;

  bg_value_from_json_external(&val, obj);
  

  if(!(dict = gavl_value_get_dictionary(&val)) ||
     !(images = gavl_dictionary_get_array(dict, "images")))
    goto fail;

  for(i = 0; i < images->num_entries; i++)
    {
    if((image = gavl_value_get_dictionary(&images->entries[i])) &&
       gavl_dictionary_get_int(image, "front", &val_i) &&
       val_i &&
       (src = gavl_dictionary_get_string(image, "image")))
      {
      if(gavl_string_ends_with(src, ".jpg"))
        mimetype = "image/jpeg";
      else if(gavl_string_ends_with(src, ".png"))
        mimetype = "image/png";

      gavl_metadata_add_image_uri(m, GAVL_META_COVER_URL, -1, -1, mimetype, src);
      break;
      }
    }
  
  //  fprintf(stderr, "Got json object\n");
  //  gavl_value_dump(&val, 2);
  
  fail:

  gavl_value_free(&val);
  
  free(uri);
  gavl_buffer_free(&buf);

  if(obj)
    json_object_put(obj);
  
  }

int bg_cdaudio_get_metadata_musicbrainz(bg_cdaudio_index_t * idx,
                                        gavl_dictionary_t * mi,
                                        char * disc_id,
                                        char * musicbrainz_host,
                                        int musicbrainz_port,
                                        char * musicbrainz_proxy_host,
                                        int musicbrainz_proxy_port)
  {
#if 1
  int ret = 0;
  int RequiredLength=0;
  Mb5Query Query = NULL;
  Mb5Metadata Metadata1 = NULL;
  //  tQueryResult Result;
  int HTTPCode;
  Mb5Disc Disc = NULL;
  Mb5ReleaseList ReleaseList = NULL;
  int ThisRelease=0;

  Mb5Metadata Metadata2=NULL;
  Mb5Release Release=NULL;

  char *ParamNames[1];
  char *ParamValues[1];
  
  char * ReleaseID = NULL;

  Mb5Release FullRelease= NULL;
  Mb5MediumList MediumList = NULL;

  Mb5Medium Medium = NULL;
  
  int ThisMedium;
  char *MediumTitle = NULL;
  char *ReleaseTitle = NULL;
  
  Mb5TrackList TrackList=NULL;
  Mb5ReleaseGroup ReleaseGroup = NULL;
  gavl_dictionary_t * disk_dict;

  char * Date = NULL;

  char * FullReleaseID = NULL;
  
  if(!(Query = mb5_query_new(PACKAGE"-"VERSION,musicbrainz_host,musicbrainz_port)))
    goto fail;
  
  //  fprintf(stderr, 

  Metadata1 = mb5_query_query(Query,"discid",disc_id,"",0,NULL,NULL);

  //  Result = mb5_query_get_lastresult(Query);

  HTTPCode = mb5_query_get_lasthttpcode(Query);

  if(HTTPCode != 200)
    {
    char * ErrorMessage;
    GET_STRING(mb5_query_get_lasterrormessage, Query, ErrorMessage);
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Musicbrains lookup failed: %d, %s", HTTPCode, ErrorMessage);
    free(ErrorMessage);
    goto fail;
    }

  if(!Metadata1)
    goto fail;
  
  if(!(Disc=mb5_metadata_get_disc(Metadata1)))
    goto fail;
  
  if(!(ReleaseList=mb5_disc_get_releaselist(Disc)))
    goto fail;

  disk_dict = gavl_dictionary_get_dictionary_create(mi, GAVL_META_METADATA);
  
  /*
   *if we want to keep an object around for a while, we can
   *clone it. We are now responsible for deleting the object
   */
  
  printf("Found %d release(s)\n",mb5_release_list_size(ReleaseList));

  ThisRelease=0; // Used to be a loop loop
  
  //  for (ThisRelease=0;ThisRelease<mb5_release_list_size(ReleaseList);ThisRelease++)
  //    {

  if(!(Release=mb5_release_list_item(ReleaseList,ThisRelease)))
    goto fail;

  GET_STRING(mb5_release_get_title,Release,ReleaseTitle);
  fprintf(stderr, "Release Title: %s\n", ReleaseTitle);

  if(!gavl_dictionary_get(disk_dict, GAVL_META_TITLE))
    gavl_dictionary_set_string_nocopy(disk_dict, GAVL_META_TITLE, ReleaseTitle);
  else
    free(ReleaseTitle);
  
  //  fprintf(stderr, "Release artists\n");
  //  get_artists(mb5_release_get_artistcredit(Release), disk_dict, GAVL_META_ARTIST);
  
  /* The releases returned from LookupDiscID don't contain full information */
  
  ParamNames[0] = gavl_strdup("inc");
  ParamValues[0] = gavl_strdup("artists labels recordings release-groups url-rels discids artist-credits");

  GET_STRING(mb5_release_get_id,Release,ReleaseID);
  //  mb5_release_get_id(Release,ReleaseID,sizeof(ReleaseID));

  if(!(Metadata2=mb5_query_query(Query,"release",ReleaseID,"",1,ParamNames,ParamValues)))
    goto fail;
  
  if(!(FullRelease=mb5_metadata_get_release(Metadata2)))
    goto fail;

  GET_STRING(mb5_release_get_id,FullRelease,FullReleaseID);
  fprintf(stderr, "Fullrelease ID: %s\n", FullReleaseID);

  /* TODO: Fetch json from http://coverartarchive.org/release/<ID>
     and parse json */

  fetch_cover_art(disk_dict, FullReleaseID);
  
  GET_STRING(mb5_release_get_title,FullRelease,ReleaseTitle);
  fprintf(stderr, "Fullrelease Title: %s\n", ReleaseTitle);

  GET_STRING(mb5_release_get_date,FullRelease,Date);
  fprintf(stderr, "Date: %s\n", Date);

  gavl_dictionary_set_date(disk_dict, 
                           GAVL_META_DATE,
                           atoi(Date), 99, 99);
  
  free(Date);
  
  if(!gavl_dictionary_get(disk_dict, GAVL_META_TITLE))
    gavl_dictionary_set_string_nocopy(disk_dict, GAVL_META_TITLE, ReleaseTitle);
  else
    free(ReleaseTitle);
  
  /*
   * However, these releases will include information for all media in the release
   * So we need to filter out the only the media we want.
   */
  
  if(!(MediumList=mb5_release_media_matching_discid(FullRelease,disc_id)))
    goto fail;
  
  if(mb5_medium_list_size(MediumList) <= 0)
    goto fail;
 
  if((ReleaseGroup=mb5_release_get_releasegroup(FullRelease)))
    {
    GET_STRING(mb5_releasegroup_get_title,ReleaseGroup,ReleaseTitle);
    printf("Release group title: '%s'\n",ReleaseTitle);

    if(!gavl_dictionary_get(disk_dict, GAVL_META_TITLE))
      gavl_dictionary_set_string_nocopy(disk_dict, GAVL_META_TITLE, ReleaseTitle);
    else
      free(ReleaseTitle);
    
    //fprintf(stderr, "Release group artists\n");
    //    get_artists(mb5_releasegroup_get_artistcredit(ReleaseGroup), NULL);
    
    get_artists(mb5_releasegroup_get_artistcredit(ReleaseGroup), disk_dict, GAVL_META_ARTIST);
    
    }
  else
    printf("No release group for this release\n");
  
  printf("Found %d media item(s)\n",mb5_medium_list_size(MediumList));
  
  ThisMedium = 0;
    
  //    for (ThisMedium=0;ThisMedium<mb5_medium_list_size(MediumList);ThisMedium++)
  //      {
  
  if(!(Medium=mb5_medium_list_item(MediumList,ThisMedium)))
    goto fail;

  TrackList=mb5_medium_get_tracklist(Medium);

  GET_STRING(mb5_medium_get_title,Medium,MediumTitle);
                          
  printf("Found media: '%s', position %d\n",MediumTitle,mb5_medium_get_position(Medium));

  if (TrackList)
    {
    int ThisTrack=0;

    for (ThisTrack=0;ThisTrack<mb5_track_list_size(TrackList);ThisTrack++)
      {
      gavl_dictionary_t * track_dict;
      char *TrackTitle=0;

      Mb5Track Track=mb5_track_list_item(TrackList,ThisTrack);
      Mb5Recording Recording=mb5_track_get_recording(Track);

      if(!(track_dict = gavl_get_track_nc(mi, ThisTrack)) ||
         !(track_dict = gavl_track_get_metadata_nc(track_dict)))
        continue;

      gavl_dictionary_set(track_dict, GAVL_META_ALBUMARTIST,
                          gavl_dictionary_get(disk_dict, GAVL_META_ARTIST));

      gavl_dictionary_set(track_dict, GAVL_META_ALBUM,
                          gavl_dictionary_get(disk_dict, GAVL_META_TITLE));

      gavl_dictionary_set(track_dict, GAVL_META_DATE,
                          gavl_dictionary_get(disk_dict, GAVL_META_DATE));

      gavl_dictionary_set(track_dict, GAVL_META_COVER_URL,
                          gavl_dictionary_get(disk_dict, GAVL_META_COVER_URL));
      
      if(Recording)
        {
        GET_STRING(mb5_recording_get_title,Recording,TrackTitle);
        //        fprintf(stderr, "Recording artists\n");
        get_artists(mb5_recording_get_artistcredit(Recording), track_dict, GAVL_META_ARTIST);
        }
      else
        {
        GET_STRING(mb5_track_get_title,Track,TrackTitle);
        //        fprintf(stderr, "Track artists\n");
        get_artists(mb5_track_get_artistcredit(Track), track_dict, GAVL_META_ARTIST);
        }
      gavl_dictionary_set_string_nocopy(track_dict, GAVL_META_TITLE, TrackTitle);
      }
    }
  
  free(ParamValues[0]);
  free(ParamNames[0]);
  
  ret = 1;
  
  fail:

  /* We must delete the original query object */

  if(MediumTitle)
    free(MediumTitle);
  
  if(MediumList)
    mb5_medium_list_delete(MediumList);
  
  if(ReleaseID)
    free(ReleaseID);

  if(FullReleaseID)
    free(FullReleaseID);

  if(Metadata2)
    mb5_metadata_delete(Metadata2);
  
  if(Metadata1)
    mb5_metadata_delete(Metadata1);


  
  if(Query)
    mb5_query_delete(Query);
  
  return ret;
#else
  int num_tracks, is_multiple_artist, i, j;
  
  int ret = 0;
  char * args[2];
  char data[256], temp[256], album_name[256], artist[256];
  
  int result;
  musicbrainz_t m;
  
  m = mb_New();
  mb_UseUTF8(m, 1);
  mb_SetDepth(m, 4);

  mb_SetServer(m, musicbrainz_host, musicbrainz_port);

  if(musicbrainz_proxy_host && *musicbrainz_proxy_host)
    mb_SetProxy(m, musicbrainz_proxy_host, musicbrainz_proxy_port);
  
  //  test_cdindex();
    
  args[0] = disc_id;
  args[1] = NULL;
  
  result =  mb_QueryWithArgs(m, MBQ_GetCDInfoFromCDIndexId, args);

  if(!result)
    goto fail;

  /* Now extract the metadata */

  // Select the first album
  if(!mb_Select1(m, MBS_SelectAlbum, 1))
    {
    goto fail;
    }

  // Pull back the album id to see if we got the album
  if (!mb_GetResultData(m, MBE_AlbumGetAlbumId, data, 256))
    {
    goto fail;
    }


  mb_GetResultData(m, MBE_AlbumGetAlbumName, album_name, 256);


  num_tracks = mb_GetResultInt(m, MBE_AlbumGetNumTracks);
  is_multiple_artist = 0;
  
  // Check to see if there is more than one artist for this album
  for(i = 1; i <= num_tracks; i++)
    {
    if (!mb_GetResultData1(m, MBE_AlbumGetArtistId, data, 256, i))
      break;
    
    if (i == 1)
      strcpy(temp, data);
    
    if(strcmp(temp, data))
      {
      is_multiple_artist = 1;
      break;
      }
    }

  if(!is_multiple_artist)
    mb_GetResultData1(m, MBE_AlbumGetArtistName, artist, 256, 1);


  for(i = 0; i < num_tracks; i++)
    {
    if(!idx->tracks[i].is_audio)
      continue;
    
    j = idx->tracks[i].index;
    
    /* Title */
    mb_GetResultData1(m, MBE_AlbumGetTrackName, data, 256, i+1);

    gavl_dictionary_set_string(&info[j].metadata, GAVL_META_TITLE, data);
    
    /* Artist */
    if(is_multiple_artist)
      {
      mb_GetResultData1(m, MBE_AlbumGetArtistName, data, 256, i+1);
      gavl_dictionary_set_string(&info[j].metadata, GAVL_META_ARTIST, data);
      }
    else
      gavl_dictionary_set_string(&info[j].metadata, GAVL_META_ARTIST, artist);
    
    /* Album name */

    gavl_dictionary_set_string(&info[j].metadata, GAVL_META_ALBUM, album_name);
    }
  ret = 1;
  fail:
  mb_Delete(m);
  return ret;

#endif

  }
