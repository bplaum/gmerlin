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
#include <unistd.h>
#include <math.h>

#include <mediadb_private.h>
#include <gmerlin/log.h>
#include <gmerlin/utils.h>
#include <string.h>
#include <ctype.h>

#include <gavl/metatags.h>

#define LOG_DOMAIN "db.videofile"

// "Unidentified","0","Movie (Single File)","1","TV Series","2","TV Episode","3","Home Video","4","TV Season","5","Movie (Multiple Files)","6");

static void free_video_file(void * obj)
  {
  bg_db_video_file_t * file = obj;
  if(file->video_codec)
    free(file->video_codec);
  }

static void dump_video_file(void * obj)
  {
  bg_db_video_file_t * file = obj;
  gavl_diprintf(2, "Video codec: %s\n", file->video_codec);
  
  }

static void del_video_file(bg_db_t * db, bg_db_object_t * obj) // Delete from db
  {
  bg_sqlite_delete_by_id(db->db, "VIDEO_FILES", obj->id);
  }

#define WIDTH_COL           1
#define HEIGHT_COL          2
#define ASPECT_COL          3
#define TIMESCALE_COL       4
#define FRAME_DURATION_COL  5
#define VIDEO_CODEC_COL     6
#define COLLECTION_COL      7
#define IDX_COL             8

static int query_video_file(bg_db_t * db, void * obj)
  {
  int result;
  int found = 0;
  const char * val;
  bg_db_video_file_t * f = obj;

  sqlite3_stmt * st = db->q_video_files;
  
  sqlite3_bind_int64(st, 1, f->file.obj.id);
  
  if((result = sqlite3_step(st)) == SQLITE_ROW)
    {
    BG_DB_GET_COL_INT(WIDTH_COL, f->width);
    BG_DB_GET_COL_INT(HEIGHT_COL, f->height);
    
    val = (const char*)sqlite3_column_text(st, ASPECT_COL);
    sscanf(val, "%d:%d", &f->aspect_num, &f->aspect_den);

    BG_DB_GET_COL_INT(TIMESCALE_COL, f->timescale);
    BG_DB_GET_COL_INT(FRAME_DURATION_COL, f->frame_duration);
    BG_DB_GET_COL_STRING(VIDEO_CODEC_COL, f->video_codec);    
    BG_DB_GET_COL_INT(COLLECTION_COL, f->collection);
    BG_DB_GET_COL_INT(IDX_COL, f->index);
    found = 1;
    }
  sqlite3_reset(st);
  sqlite3_clear_bindings(st);
  
  if(!found)
    return 0;
  return 1;
  }

const bg_db_object_class_t bg_db_video_file_class =
  {
  .name = "Video file",
  .del = del_video_file,
  .free = free_video_file,
  .query = query_video_file,
  .dump = &dump_video_file,
  .parent = &bg_db_file_class,
  };

static void
del_movie_part(bg_db_t * db, bg_db_object_t * obj) // Delete from db
  {
  bg_db_video_container_t * movie;
  bg_db_video_file_t * part = (bg_db_video_file_t *)obj;
  
  if(part->collection > 0)
    {
    movie = bg_db_object_query(db, part->collection);
    if(movie)
      {
      bg_db_object_remove_child(db, movie, part);
      if(!movie->obj.children)
        {
        gavl_log(GAVL_LOG_INFO, LOG_DOMAIN,
               "Removing empty multipart movie %s", movie->info.title);
        bg_db_object_delete(db, movie);
        }
      else
        bg_db_object_unref(movie);
      }
    }
  }

const bg_db_object_class_t bg_db_movie_part_class =
  {
  .name = "Movie part",
  .del = del_movie_part,
  .parent = &bg_db_video_file_class,
  };

static void insert_video_file(bg_db_t * db, void * obj)
  {
  char * sql;
  bg_db_video_file_t * f = obj;

  char * aspect_string = bg_sprintf("%d:%d", f->aspect_num, f->aspect_den);
  
  sql = sqlite3_mprintf("INSERT INTO VIDEO_FILES ( "
                        "ID, "
                        "WIDTH, "
                        "HEIGHT, "
                        "ASPECT, "
                        "TIMESCALE, "
                        "FRAME_DURATION, "
                        "VIDEO_CODEC, "
                        "COLLECTION, "
                        "IDX"
                        ") VALUES ("
                        "%"PRId64","
                        "%d,"
                        "%d,"
                        "%Q,"
                        "%d,"
                        "%d,"
                        "%Q,"
                        "%"PRId64","
                        "%d"
                        ");",
                        bg_db_object_get_id(obj),
                        f->width,
                        f->height,
                        aspect_string,
                        f->timescale,
                        f->frame_duration,
                        f->video_codec,
                        f->collection,
                        f->index);

  bg_sqlite_exec(db->db, sql, NULL, NULL);
  sqlite3_free(sql);
  free(aspect_string);
  }

/* Movie / episode */
static void del_episode(bg_db_t * db, bg_db_object_t * obj) // Delete from db
  {
  bg_db_video_container_t * season;
  bg_db_video_file_t * ep = (bg_db_video_file_t *)obj;
  
  if(ep->collection > 0)
    {
    season = bg_db_object_query(db, ep->collection);
    if(season)
      {
      bg_db_object_remove_child(db, season, ep);
      if(!season->obj.children)
        {
        gavl_log(GAVL_LOG_INFO, LOG_DOMAIN,
               "Removing empty season %s", season->info.title);
        bg_db_object_delete(db, season);
        }
      else
        bg_db_object_unref(season);
      }
    }
  bg_db_video_info_delete(db, obj);
  }

static void del_movie(bg_db_t * db, bg_db_object_t * obj) // Delete from db
  {
  bg_db_video_info_delete(db, obj);
  }

static void free_movie(void * obj)
  {
  bg_db_movie_t * file = obj;
  bg_db_video_info_free(&file->info);
  }

static int query_movie(bg_db_t * db, void * obj)
  {
  bg_db_movie_t * file = obj;
  bg_db_video_info_query(db, &file->info, obj);
  return 1;
  }

static void dump_movie(void * obj)
  {
  bg_db_movie_t * file = obj;
  bg_db_video_info_dump(&file->info);
  }

static void update_movie(bg_db_t * db, void * obj)
  {
  bg_db_movie_t * file = obj;
  bg_db_video_info_update(db, &file->info, obj);
  }

const bg_db_object_class_t bg_db_movie_class =
  {
  .name = "Movie",
  .del = del_movie,
  .free = free_movie,
  .query = query_movie,
  .dump = dump_movie,
  .update = update_movie,
  .parent = &bg_db_video_file_class,
  };

const bg_db_object_class_t bg_db_episode_class =
  {
  .name = "Episode",
  .del = del_episode,
  .free = free_movie,
  .query = query_movie,
  .dump = dump_movie,
  .update = update_movie,
  .parent = &bg_db_video_file_class,
  };

static const struct
  {
  int num;
  int den;
  }
aspect_ratios[] = 
  {
    { 4,  3 },
    { 16, 9 },
    { /* End */ }
  };
  
static void guess_aspect(const gavl_video_format_t * format, int * num, int * den)
  {
  int min_index = 0;
  double min_diff = 1.0;
  double test_aspect = 1.0;
  int i = 0;

  double aspect = (double)(format->image_width * format->pixel_width) /
    (double)(format->image_height * format->pixel_height);
  
  while(aspect_ratios[i].num)
    {
    test_aspect = (double)aspect_ratios[i].num / (double)aspect_ratios[i].den;
    if(!i || (fabs(test_aspect - aspect) < min_diff))
      {
      min_index = i;
      min_diff = fabs(test_aspect - aspect);
      }
    i++;
    }
  *num = aspect_ratios[min_index].num;
  *den = aspect_ratios[min_index].den;
  }

static int is_skip_char(char c)
  {
  return isspace(c) || (c == '_') || (c == '-');
  }

static const char * detect_date(const char * filename, bg_db_date_t * date)
  {
  const char * pos = strrchr(filename, '(');
  if(!pos)
    return NULL;
  
  bg_db_date_set_invalid(date);
  
  if(sscanf(pos, "(%d-%d-%d)", &date->year, &date->month, &date->day) < 3)
    {
    date->month = 0;
    date->day = 0;
    bg_db_date_set_invalid(date);
    if(sscanf(pos, "(%d)", &date->year) < 1)
      {
      bg_db_date_set_invalid(date);
      return NULL;
      }
    }
  pos--;
  while(is_skip_char(*pos) && (pos > filename))
    pos--;
  pos++;
  return pos;
  }

static const char * detect_episode_tag(const char * filename, const char * end, 
                                       int * season_p, int * idx_p)
  {
  const char * pos;
  int season, idx;
  if(!end)
    end = filename + strlen(filename);

  pos = filename;
  
  while(pos < end)
    {
    if((sscanf(pos, "S%dE%d", &season, &idx) == 2) ||
       (sscanf(pos, "s%de%d", &season, &idx) == 2))
      {
      if(season_p)
        *season_p = season;
      if(idx_p)
        *idx_p = idx;
      return pos;
      }
    pos++;
    }
  return NULL;
  }

static const char * detect_multipart_tag(const char * filename, int * part)
  {
  const char * pos = filename + strlen(filename) - 3;

  while(pos > filename)
    {
    if(!strncasecmp(pos, "CD", 2) && isdigit(pos[2]))
      {
      if(part)
        *part = atoi(pos+2);
      return pos;
      }
    else if(!strncasecmp(pos, "part", 4) && isdigit(pos[4]))
      {
      if(part)
        *part = atoi(pos+4);
      return pos;
      }
    pos--;
    }
  return NULL;
  }

/* "Movie title (1990)" */

static int detect_movie_singlefile(const char * filename,
                                   char ** title, bg_db_date_t * date)
  {
  const char * end = detect_date(filename, date);
  if(!end)
    end = filename + strlen(filename);
  *title = gavl_strndup(filename, end);
  return 1;
  }

/* "Movie title (1990) CD1" */

static int detect_movie_multifile(const char * filename,
                                  char ** label, char ** title,
                                  bg_db_date_t * date, int * idx)
  {
  const char * pos;
  const char * end = filename + strlen(filename);

  pos = detect_multipart_tag(filename, idx);

  if(!pos)
    return 0;
  
  end = detect_date(filename, date);
  if(!end)
    {
    end = pos;
    end--;
 
    while(is_skip_char(*end) && (end > filename))
      end--;
    end++;
    }

  *title = gavl_strndup(filename, end); 

  pos--;
  
  while(is_skip_char(*pos) && (pos > filename))
    pos--;
  
  pos++;
  *label = gavl_strndup(filename, pos);
  
  return 1;
  }

/* "Show - S1E2 - Episode (1990)" */

static int detect_episode(const char * filename, 
                          char ** show, int * season,
                          int * idx, char ** title, bg_db_date_t * date)
  {
  const char * tag;
  const char * pos;
  const char * end = detect_date(filename, date);
  if(!end)
    end = filename + strlen(filename);

  tag = detect_episode_tag(filename, end, season, idx);
  if(!tag)
    return 0;

  pos = tag;
  while(!is_skip_char(*pos) && (pos < end))
    pos++;
  if(pos == end)
    return 0;
  while(is_skip_char(*pos) && (pos < end))
    pos++;

  if(pos == end)
    return 0;
  
  *title = gavl_strndup(pos, end);

  pos = tag;
  pos--;

  while(is_skip_char(*pos) && (pos > filename))
    pos--;

  if(pos == filename)
    {
    free(*title);
    return 0;
    }
  pos++;
  *show = gavl_strndup(filename, pos);
  return 1;  
  }

static void detect_languages(bg_db_t * db, gavl_dictionary_t * t, bg_db_video_info_t * info)
  {
  int i, idx;
  const char * var;
  int num_audio_streams;
  int num_text_streams;
  int num_overlay_streams;
  
  /* Get audio languages */
  num_audio_streams = gavl_track_get_num_audio_streams(t);
  num_text_streams = gavl_track_get_num_text_streams(t);
  num_overlay_streams = gavl_track_get_num_overlay_streams(t);

  if(num_audio_streams)
    {
    info->audio_languages = calloc((num_audio_streams + 1), sizeof(*info->audio_languages));

    for(i = 0; i < num_audio_streams; i++)
      {
      var = gavl_dictionary_get_string(gavl_track_get_audio_metadata(t, i), GAVL_META_LANGUAGE);
      if(!var)
        var = "und";
      info->audio_languages[i] = gavl_strdup(bg_iso639_t_to_b(var));
      }

    /* Strings -> ID */

    info->audio_language_ids = malloc((num_audio_streams+1)*sizeof(*info->audio_language_ids));
    for(i = 0; i < num_audio_streams; i++)
      info->audio_language_ids[i] =
        bg_sqlite_string_to_id_add(db->db, "VIDEO_LANGUAGES",
                                   "ID", "NAME", info->audio_languages[i]);
    info->audio_language_ids[num_audio_streams] = -1;
    }
  
  /* Get languages */
  if(num_text_streams || num_overlay_streams)
    {
    info->subtitle_languages =
      calloc(num_text_streams + num_overlay_streams + 1, sizeof(*info->subtitle_languages));
    
    for(i = 0; i < num_text_streams; i++)
      {
      var = gavl_dictionary_get_string(gavl_track_get_text_metadata(t, i), GAVL_META_LANGUAGE);
      if(!var)
        var = "und";
      info->subtitle_languages[i] = gavl_strdup(bg_iso639_t_to_b(var));
      }

    idx = num_text_streams;

    for(i = 0; i < num_overlay_streams; i++)
      {
      var = gavl_dictionary_get_string(gavl_track_get_overlay_metadata(t, i), GAVL_META_LANGUAGE);
      if(!var)
        var = "und";
      info->subtitle_languages[idx] = gavl_strdup(bg_iso639_t_to_b(var));
      idx++;
      }
    
    /* Strings -> ID */

    info->subtitle_language_ids = malloc((num_text_streams+num_overlay_streams+1)*
                                         sizeof(*info->subtitle_language_ids));
    for(i = 0; i < num_text_streams+num_overlay_streams; i++)
      info->subtitle_language_ids[i] =
        bg_sqlite_string_to_id_add(db->db, "VIDEO_LANGUAGES",
                                   "ID", "NAME", info->subtitle_languages[i]);
    info->subtitle_language_ids[num_text_streams+num_overlay_streams] = -1;
    }
  }

void bg_db_video_file_create(bg_db_t * db, void * obj, gavl_dictionary_t * t)
  {
  const char * val;
  bg_db_date_t date;
  char * show_name = NULL;
  char * title = NULL;
  char * label = NULL;
  int season_idx;
  int idx; 
  const gavl_video_format_t * fmt;


  bg_db_video_file_t * f = obj;

  bg_db_date_set_invalid(&date);
  
  /* Get technical aspects */
  bg_db_object_set_type(obj, BG_DB_OBJECT_VIDEO_FILE);
  
  fmt = gavl_track_get_video_format(t, 0);

  f->width  = fmt->image_width;
  f->height = fmt->image_height;

  guess_aspect(fmt, &f->aspect_num, &f->aspect_den);
  
  f->timescale = fmt->timescale;
  f->frame_duration =
    (fmt->framerate_mode == GAVL_FRAMERATE_VARIABLE) ? 0 :
      fmt->frame_duration;
  
  val = gavl_dictionary_get_string(gavl_track_get_video_metadata(t, 0), GAVL_META_FORMAT);
  if(val)
    f->video_codec = gavl_strdup(val);
  
  /* Get logical aspects */
  
  if(detect_episode(f->file.obj.label, &show_name, &season_idx,
                    &idx, &title, &date))
    {
    bg_db_object_t * show;
    bg_db_object_t * season;
    char * season_name;
    const char * pos;

    bg_db_movie_t * m = (bg_db_movie_t *)f;

    //    fprintf(stderr,
    //            "Detected Episode\nShow: %s\nSeason: %d\nEpisode: %d\nTitle: %s\nYear: %d\n",
    //            show_name, season_idx, idx, title, date.year);
    bg_db_video_info_init(&m->info);
    bg_db_video_info_set_title_nocpy(&m->info, title);

    memcpy(&m->info.date, &date, sizeof(date));
    detect_languages(db, t, &m->info);
    
    bg_db_object_set_type(m, BG_DB_OBJECT_VIDEO_EPISODE);

    /* Lookup show */
    show = bg_db_tvseries_query(db, show_name);

    /* Lookup season */
    pos = detect_episode_tag(f->file.obj.label, NULL, NULL, NULL);
    while(tolower(*pos) != 'e')
      pos++;
    season_name = gavl_strndup(f->file.obj.label, pos);
    season = bg_db_season_query(db, show, season_name, season_idx);
    free(season_name);
    f->collection = season->id;
    f->index = idx;

    bg_db_video_info_insert(db, &m->info, m);

    bg_db_object_add_child(db, season, m);
    bg_db_object_unref(show);
    bg_db_object_unref(season);
    }
  else if(detect_movie_multifile(f->file.obj.label, &label, &title, &date, &idx))
    {
    bg_db_video_container_t * m;
    bg_db_object_set_type(f, BG_DB_OBJECT_MOVIE_PART);
#if 0
    fprintf(stderr,
            "Detected multifile movie\nTitle: %s\nYear: %d\nLabel: %s\n",
            title, date.year, label);
#endif
    m = (bg_db_video_container_t *)bg_db_movie_multipart_query(db,
                                                               label, title);

    if(!m)
      {
      /* Create object */
      m = bg_db_object_create(db);
      bg_db_object_set_type(m, BG_DB_OBJECT_MOVIE_MULTIPART);
      bg_db_video_info_init(&m->info);
      detect_languages(db, t, &m->info);
      
      bg_db_object_set_parent_id(db, m, -1);
      bg_db_object_set_label(m, label);

      bg_db_video_info_set_title(&m->info, title);
      memcpy(&m->info.date, &date, sizeof(date));
      bg_db_video_info_insert(db, &m->info, m);
      bg_db_create_vfolders(db, m);
      }
    
    f->collection = bg_db_object_get_id(m);
    f->index = idx;
    bg_db_object_add_child(db, m, f);
    bg_db_object_unref(m);
    }
  else if(detect_movie_singlefile(f->file.obj.label, &title, &date))
    {
    bg_db_movie_t * m = (bg_db_movie_t *)f;

    //    fprintf(stderr,
    //            "Detected Singlefile movie\nTitle: %s\nYear: %d\n",
    //            title, date.year);

    bg_db_video_info_init(&m->info);
    detect_languages(db, t, &m->info);

    bg_db_video_info_set_title_nocpy(&m->info, title);
    
    memcpy(&m->info.date, &date, sizeof(date));
    
    bg_db_object_set_type(m, BG_DB_OBJECT_MOVIE);
    bg_db_video_info_insert(db, &m->info, m);
    bg_db_create_vfolders(db, m);
    }
  
  insert_video_file(db, f);
  }


void bg_db_cleanup_video(bg_db_t * db)
  {
  bg_sqlite_id_tab_t tab;
  char * sql = NULL;
  int64_t count;
  int64_t total;
  int i;
  bg_sqlite_id_tab_init(&tab);
  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Cleaning up video data");

  /* Clean up empty genres */
  sql = sqlite3_mprintf("select ID from VIDEO_GENRES;");
  bg_sqlite_exec(db->db, sql, bg_sqlite_append_id_callback, &tab);
  sqlite3_free(sql);

  for(i = 0; i < tab.num_val; i++)
    {
    count = 0;
    sql = sqlite3_mprintf("select count(*) from VIDEO_GENRES_VIDEOS where GENRE_ID = %"PRId64";",
                          tab.val[i]);
    bg_sqlite_exec(db->db, sql, bg_sqlite_int_callback, &count);
    sqlite3_free(sql);

    if(!count)
      {
      char * name;
      name = bg_sqlite_id_to_string(db->db, "VIDEO_GENRES", "NAME", "ID", tab.val[i]);
      gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Removing empty video genre %s", name);
      free(name);
      bg_sqlite_delete_by_id(db->db, "VIDEO_GENRES", tab.val[i]);
      }
    }

  /* Clean up empty countries */
  bg_sqlite_id_tab_reset(&tab);

  sql = sqlite3_mprintf("select ID from VIDEO_COUNTRIES;");
  bg_sqlite_exec(db->db, sql, bg_sqlite_append_id_callback, &tab);
  sqlite3_free(sql);
  
  for(i = 0; i < tab.num_val; i++)
    {
    count = 0;
    sql = sqlite3_mprintf("select count(*) from VIDEO_COUNTRIES_VIDEOS where COUNTRY_ID = %"PRId64";",
                          tab.val[i]);
    bg_sqlite_exec(db->db, sql, bg_sqlite_int_callback, &count);
    sqlite3_free(sql);

    if(!count)
      {
      char * name;
      name = bg_sqlite_id_to_string(db->db, "VIDEO_COUNTRIES", "NAME", "ID", tab.val[i]);
      gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Removing empty video country %s", name);
      free(name);
      bg_sqlite_delete_by_id(db->db, "VIDEO_COUNTRIES", tab.val[i]);
      }
    }

  /* Clean up empty persons */
  bg_sqlite_id_tab_reset(&tab);

  sql = sqlite3_mprintf("select ID from VIDEO_PERSONS;");
  bg_sqlite_exec(db->db, sql, bg_sqlite_append_id_callback, &tab);
  sqlite3_free(sql);
  
  for(i = 0; i < tab.num_val; i++)
    {
    count = 0;
    total = 0;
    sql = sqlite3_mprintf("select count(*) from VIDEO_ACTORS_VIDEOS where PERSON_ID = %"PRId64";",
                          tab.val[i]);
    bg_sqlite_exec(db->db, sql, bg_sqlite_int_callback, &count);
    sqlite3_free(sql);

    total += count;
    count = 0;

    sql = sqlite3_mprintf("select count(*) from VIDEO_DIRECTORS_VIDEOS where PERSON_ID = %"PRId64";",
                          tab.val[i]);
    bg_sqlite_exec(db->db, sql, bg_sqlite_int_callback, &count);
    sqlite3_free(sql);

    total += count;
    
    if(!total)
      {
      char * name;
      name = bg_sqlite_id_to_string(db->db, "VIDEO_PERSONS", "NAME", "ID", tab.val[i]);
      gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Removing empty video person %s", name);
      free(name);
      bg_sqlite_delete_by_id(db->db, "VIDEO_PERSONS", tab.val[i]);
      }
    }

  /* Clean up empty languages */
  bg_sqlite_id_tab_reset(&tab);
  
  sql = sqlite3_mprintf("select ID from VIDEO_LANGUAGES;");
  bg_sqlite_exec(db->db, sql, bg_sqlite_append_id_callback, &tab);
  sqlite3_free(sql);
  
  for(i = 0; i < tab.num_val; i++)
    {
    count = 0;
    total = 0;
    sql = sqlite3_mprintf("select count(*) from VIDEO_AUDIO_LANGUAGES_VIDEOS where LANGUAGE_ID = %"PRId64";",
                          tab.val[i]);
    bg_sqlite_exec(db->db, sql, bg_sqlite_int_callback, &count);
    sqlite3_free(sql);

    total += count;
    count = 0;

    sql = sqlite3_mprintf("select count(*) from VIDEO_SUBTITLE_LANGUAGES_VIDEOS where LANGUAGE_ID = %"PRId64";",
                          tab.val[i]);
    bg_sqlite_exec(db->db, sql, bg_sqlite_int_callback, &count);
    sqlite3_free(sql);

    total += count;
    
    if(!total)
      {
      char * name;
      name = bg_sqlite_id_to_string(db->db, "VIDEO_LANGUAGES", "NAME", "ID", tab.val[i]);
      gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Removing empty video language %s", name);
      free(name);
      bg_sqlite_delete_by_id(db->db, "VIDEO_LANGUAGES", tab.val[i]);
      }
    }
  bg_sqlite_id_tab_free(&tab);

  
  }

void bg_db_cleanup_tvseries(bg_db_t * db)
  {
  bg_sqlite_id_tab_t tab;
  char * sql = NULL;
  int i;
  char * date = NULL;
  bg_db_video_container_t * c;
  
  bg_sqlite_id_tab_init(&tab);

  /* Get the year for seasons */
  sql = sqlite3_mprintf("select ID from VIDEO_INFOS where VIDEO_TYPE = %d;", BG_DB_OBJECT_SEASON);
  bg_sqlite_exec(db->db, sql, bg_sqlite_append_id_callback, &tab);
  sqlite3_free(sql);
  
  for(i = 0; i < tab.num_val; i++)
    {
    c = bg_db_object_query(db, tab.val[i]);
    
    sql = sqlite3_mprintf("SELECT date from VIDEO_INFOS WHERE ID IN "
                          "(SELECT ID FROM VIDEO_FILES WHERE COLLECTION = %"PRId64") "
                          "order by date limit 1;", tab.val[i]);
    date = NULL;
    bg_sqlite_exec(db->db, sql, bg_sqlite_string_callback, &date);
    sqlite3_free(sql);

    if(date)
      {
      bg_db_string_to_date(date, &c->info.date);
      free(date);
      }
    bg_db_object_unref(c);
    }

  bg_sqlite_id_tab_reset(&tab);

  /* Get the year for shows */
  sql = sqlite3_mprintf("select ID from VIDEO_INFOS where VIDEO_TYPE = %d;", BG_DB_OBJECT_TVSERIES);
  bg_sqlite_exec(db->db, sql, bg_sqlite_append_id_callback, &tab);
  sqlite3_free(sql);

  for(i = 0; i < tab.num_val; i++)
    {
    c = bg_db_object_query(db, tab.val[i]);
    
    sql = sqlite3_mprintf("SELECT date from VIDEO_INFOS WHERE ID IN "
                          "(SELECT ID FROM OBJECTS WHERE PARENT_ID = %"PRId64") "
                          "order by date limit 1;", tab.val[i]);
    date = NULL;
    bg_sqlite_exec(db->db, sql, bg_sqlite_string_callback, &date);
    sqlite3_free(sql);
    
    bg_db_string_to_date(date, &c->info.date);
    free(date);
    bg_db_object_unref(c);
    }
  
  
  bg_sqlite_id_tab_free(&tab);

  }
