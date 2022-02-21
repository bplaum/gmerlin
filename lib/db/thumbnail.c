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

#include <mediadb_private.h>
#include <gmerlin/log.h>
#include <gmerlin/utils.h>
#include <string.h>

#define LOG_DOMAIN "db.thumbnail"

#define TH_CACHE_SIZE 32

void 
bg_db_make_thumbnail(bg_db_t * db,
                     void * obj,
                     int max_width, int max_height,
                     const char * mimetype)
  {
  bg_db_file_t * thumb;
  int ret = 0;
  /* Formats */
  gavl_video_format_t input_format;
  
  /* Frames */
  gavl_video_frame_t * input_frame = NULL;

  const char * src_ext;
  bg_db_image_file_t * image = (bg_db_image_file_t *)obj;

  char * path_abs;
  bg_db_scan_item_t item;
  
  char * out_file_base = NULL;
  
  src_ext = strrchr(image->file.path, '.');
  if(src_ext)
    src_ext++;
  
  /* Return early */
  if(image->file.mimetype &&
     !strcasecmp(image->file.mimetype, mimetype) &&
     (image->width <= max_width) &&
     (image->height <= max_height))
    return;

  /* Check if a thumbnail already exists */

  thumb = bg_db_get_thumbnail(db, bg_db_object_get_id(obj),
                              max_width, max_height, 0,
                              mimetype);

  if(thumb)
    {
    bg_db_image_file_t * img = (bg_db_image_file_t *)thumb;
    if((img->width == max_width) || (img->height == max_height))
      {
      gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Thumbnail already exists");
      bg_db_object_unref(thumb);
      return;
      }
    bg_db_object_unref(thumb);
    }

  /* Generate thumbnail */
  thumb = bg_db_object_create(db);
  out_file_base = bg_sprintf("gmerlin-db/thumbnails/%016"PRId64, bg_db_object_get_id(thumb));
  out_file_base = bg_db_filename_to_abs(db, out_file_base);
  
  memset(&input_format, 0, sizeof(input_format));
  
  input_frame = bg_plugin_registry_load_image(db->plugin_reg,
                                              image->file.path,
                                              &input_format, NULL);

  
  /* Save image */
  
  path_abs = bg_make_thumbnail(db->plugin_reg, input_frame, &input_format,
                               &max_width, &max_height,
                               out_file_base, mimetype);
  if(!path_abs)
    goto end;
  
  /* Create a new image object */

  memset(&item, 0, sizeof(item));
  if(!bg_db_scan_item_set(&item, path_abs))
    goto end;
  
  thumb = bg_db_file_create_from_object(db, (bg_db_object_t*)thumb, ~0, &item, -1);
  if(!thumb)
    goto end;
  
  bg_db_object_set_type(thumb, BG_DB_OBJECT_THUMBNAIL);
  thumb->obj.ref_id = bg_db_object_get_id(image);
  bg_db_object_set_parent_id(db, thumb, -1);
  bg_db_scan_item_free(&item);
  
  ret = 1;
  
  end:
  
  if(out_file_base)
    free(out_file_base);
  if(input_frame)
    gavl_video_frame_destroy(input_frame);
  
  if(!ret)
    {
    bg_db_object_delete(db, thumb);
    return;
    }
  bg_db_object_unref(thumb);
  }

void bg_db_browse_thumbnails(bg_db_t * db, int64_t id, 
                             bg_db_query_callback cb, void * data)
  {
  char * sql;
  int i;
  bg_db_object_t * image;
  bg_sqlite_id_tab_t tab;
  bg_sqlite_id_tab_init(&tab);
  
  image = bg_db_object_query(db, id);
  cb(data, image);
  bg_db_object_unref(image);
  
  sql = sqlite3_mprintf("SELECT ID FROM OBJECTS WHERE (TYPE = %d) & (REF_ID = %"PRId64");",
                        BG_DB_OBJECT_THUMBNAIL, id);
  bg_sqlite_exec(db->db, sql, bg_sqlite_append_id_callback, &tab);
  sqlite3_free(sql);

  for(i = 0; i < tab.num_val; i++)
    {
    image = bg_db_object_query(db, tab.val[i]);
    cb(data, image);
    bg_db_object_unref(image);
    }

  bg_sqlite_id_tab_free(&tab);
  
  }

typedef struct
  {
  int max_width;
  int max_height;
  int max_size;
  const char * mimetype;
  bg_db_image_file_t * ret;
  } browse_t;

static void browse_callback(void * data, void * obj)
  {
  browse_t * b = data;
  bg_db_image_file_t * image = obj;
  
  if(b->mimetype && strcmp(image->file.mimetype, b->mimetype))
    return;
  
  if((image->width > b->max_width) || (image->height > b->max_height))
    return;

  if((b->max_size > 0) && (image->file.obj.size > b->max_size))
    return;
  
  if(!b->ret || (b->ret->width < image->width))
    {
    if(b->ret)
      bg_db_object_unref(b->ret);
    b->ret = image;
    bg_db_object_ref(b->ret);
    }
  }


void bg_db_thumbnail_cache_init(bg_db_thumbnail_cache_t * c)
  {
  c->alloc = TH_CACHE_SIZE;
  c->items = calloc(c->alloc, sizeof(*c->items));
  }

void bg_db_thumbnail_cache_free(bg_db_thumbnail_cache_t * c)
  {
  int i;
  for(i = 0; i < c->size; i++)
    {
    if(c->items[i].mimetype)
      free(c->items[i].mimetype);
    }
  free(c->items);
  }

static int64_t cache_get(bg_db_thumbnail_cache_t * c,
                         int64_t id,
                         int max_width, int max_height, int max_size,
                         const char * mimetype)
  {
  int i;
  for(i = 0; i < c->size; i++)
    {
    if((c->items[i].ref_id == id) &&
       (c->items[i].max_width == max_width) &&
       (c->items[i].max_height == max_height) &&
       (c->items[i].max_size == max_size) &&
       !(strcmp(c->items[i].mimetype, mimetype)))
      return c->items[i].thumb_id;
    }
  return -1;
  }

static void cache_put(bg_db_thumbnail_cache_t * c,
                      int64_t id,
                      int max_width, int max_height, int max_size,
                      const char * mimetype, int thumb_id)
  {
  if(c->size == c->alloc)
    {
    if(c->items[c->size - 1].mimetype)
      free(c->items[c->size - 1].mimetype);
    c->size--;
    memmove(c->items + 1, c->items, c->size * sizeof(*c->items));
    }
  c->items[0].ref_id = id;
  c->items[0].thumb_id = thumb_id;
  c->items[0].max_width = max_width;
  c->items[0].max_height = max_height;
  c->items[0].max_size = max_size;
  c->items[0].mimetype = gavl_strdup(mimetype);
  c->size++;
  }
  
void * bg_db_get_thumbnail(bg_db_t * db, int64_t id,
                           int max_width, int max_height, int max_size,
                           const char * mimetype)
  {
  browse_t b;
  int64_t thumb_id;

  if((thumb_id = cache_get(&db->th_cache, id, max_width, max_height, max_size,
                           mimetype)) > 0)
    return bg_db_object_query(db, thumb_id);
  
  memset(&b, 0, sizeof(b));
  b.max_width = max_width;
  b.max_height = max_height;
  b.max_size = max_size;
  b.mimetype = mimetype;

  bg_db_browse_thumbnails(db, id, browse_callback, &b);
  
  if(b.ret)
    {
    cache_put(&db->th_cache, id, max_width, max_height, max_size,
              mimetype, bg_db_object_get_id(b.ret));
    }
  return b.ret;
  }

