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

#include <sys/types.h> /* stat() */
#include <sys/stat.h>  /* stat() */
#include <unistd.h>    /* stat() */

#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <gmerlin/utils.h>
#include <gmerlin/pluginregistry.h>
#include <gmerlin/subprocess.h>

#include <gmerlin/log.h>
#define LOG_DOMAIN "thumbnails"

/*
 *  freedesktop.org variant (disabled for now)
 */

#if 0
static int thumbnail_up_to_date(const char * thumbnail_file,
                                bg_plugin_registry_t * plugin_reg,
                                gavl_video_frame_t ** frame,
                                gavl_video_format_t * format,
                                int64_t mtime)
  {
  gavl_dictionary_t metadata;
  int64_t test_mtime;
  int ret = 0;
  const char * val;
  memset(&metadata, 0, sizeof(metadata));
  memset(format, 0, sizeof(*format));
  
  *frame = bg_plugin_registry_load_image(plugin_reg,
                                         thumbnail_file,
                                         format,
                                         &metadata);
  
  val = gavl_dictionary_get_string(&metadata, "Thumb::MTime");
  if(val)
    {
    test_mtime = strtoll(val, NULL, 10);
    if(mtime == test_mtime)
      ret = 1;
    }
  gavl_dictionary_free(&metadata);
  return ret;
  }

static void make_fail_thumbnail(const char * gml,
                                const char * thumb_filename,
                                bg_plugin_registry_t * plugin_reg,
                                int64_t mtime)
  {
  gavl_video_format_t format;
  gavl_video_frame_t * frame;
  gavl_dictionary_t metadata;
  char * tmp_string;
  
  memset(&format, 0, sizeof(format));

  gavl_dictionary_init(&metadata);
  
  format.image_width = 1;
  format.image_height = 1;
  format.frame_width = 1;
  format.frame_height = 1;
  format.pixel_width = 1;
  format.pixel_height = 1;
  format.pixelformat = GAVL_RGBA_32;
  
  frame = gavl_video_frame_create(&format);
  gavl_video_frame_clear(frame, &format);
  
  tmp_string = bg_string_to_uri(gml, -1);
  gavl_dictionary_set_string_nocopy(&metadata, "Thumb::URI", tmp_string);

  tmp_string = bg_sprintf("%"PRId64, mtime);
  gavl_dictionary_set_string_nocopy(&metadata, "Thumb::MTime", tmp_string);

  bg_plugin_registry_save_image(plugin_reg,
                                thumb_filename,
                                frame,
                                &format, &metadata);
  gavl_dictionary_free(&metadata);
  gavl_video_frame_destroy(frame);
  }

int bg_get_thumbnail(const char * gml,
                     bg_plugin_registry_t * plugin_reg,
                     char ** thumbnail_filename_ret,
                     gavl_video_frame_t ** frame_ret,
                     gavl_video_format_t * format_ret)
  {
  bg_subprocess_t * sp;
  char hash[33];
  char * home_dir;
  
  char * thumb_filename_normal = NULL;
  char * thumb_filename_fail = NULL;

  char * thumbs_dir_normal = NULL;
  char * thumbs_dir_fail = NULL;
  char * command;
  
  int ret = 0;
  gavl_video_frame_t * frame = NULL;
  gavl_video_format_t format;
  struct stat st;

  if(stat(gml, &st))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Cannot stat %s: %s",
           gml, strerror(errno));
    return 0;
    }
  
  /* Get and create directories */
  home_dir = getenv("HOME");
  if(!home_dir)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Cannot get home directory");
    return 0;
    }
  
  thumbs_dir_normal = bg_sprintf("%s/.thumbnails/normal",       home_dir);
  thumbs_dir_fail   = bg_sprintf("%s/.thumbnails/fail/gmerlin", home_dir);
  
  if(!bg_ensure_directory(thumbs_dir_normal, 1) ||
     !bg_ensure_directory(thumbs_dir_fail, 1))
    goto done;
  
  bg_get_filename_hash(gml, hash);

  thumb_filename_normal = bg_sprintf("%s/%s.png", thumbs_dir_normal, hash);
  thumb_filename_fail = bg_sprintf("%s/%s.png", thumbs_dir_fail, hash);
  
  if(access(thumb_filename_normal, R_OK)) /* Thumbnail file not present */
    {
    /* Check if there is a failed thumbnail */
    if(!access(thumb_filename_fail, R_OK))
      {
      if(thumbnail_up_to_date(thumb_filename_fail, plugin_reg, 
                              &frame, &format, st.st_mtime))
        {
        gavl_video_frame_destroy(frame);
        frame = NULL;
        goto done;
        }
      else /* Failed thumbnail is *not* up to date, remove it */
        {
        remove(thumb_filename_fail);
        gavl_video_frame_destroy(frame);
        frame = NULL;
        }
      }
    /* else: Regenerate */
    }
  else /* Thumbnail file present */
    {
    /* Check if the thumbnail is recent */
    if(thumbnail_up_to_date(thumb_filename_normal, plugin_reg,
                            &frame, &format, st.st_mtime))
      {
      if(thumbnail_filename_ret)
        {
        *thumbnail_filename_ret = thumb_filename_normal;
        thumb_filename_normal = NULL;
        }
      if(frame_ret)
        {
        *frame_ret = frame;
        frame = NULL;
        }
      if(format_ret)
        gavl_video_format_copy(format_ret, &format);
      ret = 1;
      goto done;
      }
    else
      {
      remove(thumb_filename_normal);
      gavl_video_frame_destroy(frame);
      frame = NULL;
      }
    /* else: Regenerate */
    }

  /* Regenerate */
  command = bg_sprintf("gmerlin-video-thumbnailer \"%s\" %s", gml, thumb_filename_normal);
  sp = bg_subprocess_create(command, 0, 0, 0);
  bg_subprocess_close(sp);
  free(command);
  
  if(!access(thumb_filename_normal, R_OK)) /* Thumbnail generation succeeded */
    {
    if(frame_ret && format_ret)
      {
      *frame_ret = bg_plugin_registry_load_image(plugin_reg,
                                                thumb_filename_normal,
                                                format_ret,
                                                NULL);
      }
    if(thumbnail_filename_ret)
      {
      *thumbnail_filename_ret = thumb_filename_normal;
      thumb_filename_normal = NULL;
      }
    
    ret = 1;
    goto done;
    }
  else /* Thumbnail generation failed */
    {
    make_fail_thumbnail(gml, thumb_filename_fail,
                        plugin_reg,
                        st.st_mtime);
    }
  
  done:
  
  free(thumbs_dir_normal);
  free(thumbs_dir_fail);

  if(thumb_filename_normal)
    free(thumb_filename_normal);
  if(thumb_filename_fail)
    free(thumb_filename_fail);
  if(frame)
    gavl_video_frame_destroy(frame);
  
  return ret;
  }

#endif

typedef struct
  {
  char * filename;
  } iw_t;

static int create_file(void * data, const char * filename)
  {
  iw_t * iw = data;
  iw->filename = gavl_strdup(filename);
  return 1;
  }

char * bg_make_thumbnail(bg_plugin_registry_t * plugin_reg,
                         gavl_video_frame_t * in_frame,
                         const gavl_video_format_t * input_format,
                         int * max_width, int * max_height,
                         const char * out_file_base,
                         const char * mimetype,
                         const gavl_dictionary_t * m)
  {
  int result = 0;
  int do_convert;
  gavl_video_frame_t * output_frame = NULL;
  bg_image_writer_plugin_t * output_plugin;
  bg_plugin_handle_t * output_handle = NULL;
  const bg_plugin_info_t * plugin_info;
  iw_t iw;
  bg_iw_callbacks_t cb;
  gavl_video_format_t out_format;
  gavl_video_converter_t * cnv;
  double ext_x, ext_y;
  double ar;
  gavl_video_options_t * opt;
  gavl_dictionary_t m_out;
  int orientation = GAVL_META_IMAGE_ORIENT_NORMAL;
  
  cnv = gavl_video_converter_create();

  gavl_dictionary_init(&m_out);
  
  opt = gavl_video_converter_get_options(cnv);
  gavl_video_options_set_quality(opt, 3);
  
  memset(&iw, 0, sizeof(iw));
  memset(&cb, 0, sizeof(cb));
  
  cb.create_output_file = create_file;
  cb.data = &iw;

  /* Set output format */
  ar = (double)input_format->image_width / (double)input_format->image_height;
  
  gavl_video_format_copy(&out_format, input_format);
    
  ext_x = (double)input_format->image_width / (double)(*max_width);
  ext_y = (double)input_format->image_height / (double)(*max_height);
  
  if((ext_x > 1.0) || (ext_y > 1.0))
    {
    if(ext_x > ext_y) // Fit to max_width
      {
      out_format.image_width  = *max_width;
      out_format.image_height = (int)((double)(*max_width) / ar + 0.5);
      }
    else // Fit to max_height
      {
      out_format.image_height  = *max_height;
      out_format.image_width = (int)((double)(*max_height) * ar + 0.5);
      }
    }

  *max_height = out_format.image_height;
  *max_width  = out_format.image_width;
  
  out_format.pixel_width = 1;
  out_format.pixel_height = 1;
  out_format.interlace_mode = GAVL_INTERLACE_NONE;

  out_format.frame_width = out_format.image_width;
  out_format.frame_height = out_format.image_height;

  plugin_info =
    bg_plugin_find_by_mimetype(plugin_reg, mimetype, BG_PLUGIN_IMAGE_WRITER);
  
  if(!plugin_info)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "No plugin for %s", mimetype);
    goto end;
    }

  output_handle = bg_plugin_load(plugin_reg, plugin_info);

  if(!output_handle)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Loading %s failed", plugin_info->long_name);
    goto end;
    }
  
  output_plugin = (bg_image_writer_plugin_t*)output_handle->plugin;

  output_plugin->set_callbacks(output_handle->priv, &cb);

  if(m && gavl_dictionary_get_int(m, GAVL_META_IMAGE_ORIENTATION, &orientation) &&
     orientation)
    {
    gavl_dictionary_set_int(&m_out, GAVL_META_IMAGE_ORIENTATION, orientation);
    }
  
  if(!output_plugin->write_header(output_handle->priv,
                                  out_file_base, &out_format, &m_out))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Writing image header failed");
    goto end;
    }

  /* Initialize video converter */
  do_convert = gavl_video_converter_init(cnv, input_format, &out_format);

  if(do_convert)
    {
    output_frame = gavl_video_frame_create(&out_format);
    gavl_video_frame_clear(output_frame, &out_format);
    gavl_video_convert(cnv, in_frame, output_frame);
    if(!output_plugin->write_image(output_handle->priv,
                                   output_frame))
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Writing image failed");
      goto end;
      }
    }
  else
    {
    if(!output_plugin->write_image(output_handle->priv,
                                   in_frame))
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Writing image failed");
      goto end;
      }
    }
  result = 1;
  
  end:

  if(cnv)
    gavl_video_converter_destroy(cnv);
  if(output_frame)
    gavl_video_frame_destroy(output_frame);
  if(output_handle)
    bg_plugin_unref(output_handle);
  
  gavl_dictionary_free(&m_out);

  if(result)
    return iw.filename;
  
  if(iw.filename)
    free(iw.filename);
  return NULL;
  
  }

