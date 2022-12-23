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
#include <gmerlin/pluginregistry.h>
#include <gmerlin/utils.h>

#define PACKET_CACHE_MAX 16

// #define DUMP_COMPRESSED

static int load_file(bg_plugin_registry_t * plugin_reg,
                     bg_plugin_handle_t ** input_handle,
                     bg_input_plugin_t ** input_plugin,
                     gavl_video_source_t ** src,
                     const char * file)
  {
  gavl_dictionary_t * ti;
  if(!(*input_handle = bg_input_plugin_load(file)))
    {
    fprintf(stderr, "Cannot open %s\n", file);
    return 0;
    }
  *input_plugin = (bg_input_plugin_t*)(*input_handle)->plugin;

  ti = bg_input_plugin_get_track_info(*input_handle, 0);
  
  bg_input_plugin_set_track(*input_handle, 0);
  
  if(!gavl_track_get_num_video_streams(ti))
    {
    fprintf(stderr, "File %s has no video\n", file);
    return 0;
    }

  /* Select first stream */

  bg_media_source_set_video_action((*input_handle)->src, 0, BG_STREAM_ACTION_DECODE);
  
  /* Start playback */
  bg_input_plugin_start(*input_handle);
  
  
  /* Get video format */

  *src = bg_media_source_get_video_source((*input_handle)->src, 0);
  
  return 1;
  }

static int load_file_compressed(bg_plugin_registry_t * plugin_reg,
                                bg_plugin_handle_t ** input_handle,
                                bg_input_plugin_t ** input_plugin,
                                const char * file,
                                gavl_video_format_t * format,
                                gavl_compression_info_t * ci, gavl_packet_source_t ** src)
  {
  gavl_dictionary_t * ti;
  if(!(*input_handle = bg_input_plugin_load(file)))
    {
    fprintf(stderr, "Cannot open %s\n", file);
    return 0;
    }
  *input_plugin = (bg_input_plugin_t*)((*input_handle)->plugin);

  ti = bg_input_plugin_get_track_info(*input_handle, 0);

  bg_input_plugin_set_track(*input_handle, 0);
  
  if(!gavl_track_get_num_video_streams(ti))
    {
    fprintf(stderr, "File %s has no video\n", file);
    return 0;
    }

  if(!gavl_track_get_audio_compression_info(ti, 0, ci))
    {
    fprintf(stderr, "File %s doesn't support compressed output\n",
            file);
    return 0;
    }
  
#ifdef DUMP_COMPRESSED
  gavl_compression_info_dump(ci);
#endif  
  /* Select first stream */
  bg_media_source_set_video_action((*input_handle)->src, 0, BG_STREAM_ACTION_READRAW);
  
  /* Start playback */
  bg_input_plugin_start((*input_handle));
  
  /* Get video format */
  
  gavl_video_format_copy(format, gavl_track_get_video_format(ti, 0));

  *src = bg_media_source_get_video_packet_source((*input_handle)->src, 0);

  return 1;
  }


int main(int argc, char ** argv)
  {
  gavl_video_format_t format_o;
  gavl_video_format_t format_c;
  gavl_video_format_t format_ssim;
  gavl_video_format_t format_cmp;

  gavl_video_frame_t * frame_o;
  gavl_video_frame_t * frame_c;
  gavl_video_frame_t * frame_ssim_res;
  
  
  int i, j;
  
  bg_plugin_handle_t * input_handle_o;
  bg_plugin_handle_t * input_handle_c;
  bg_plugin_handle_t * input_handle_cmp;
  
  bg_input_plugin_t * input_plugin_o;
  bg_input_plugin_t * input_plugin_c;
  bg_input_plugin_t * input_plugin_cmp;
  
  gavl_packet_t packet;
  gavl_compression_info_t ci;
  int frame = 0;
  float * ssim_ptr;

  int64_t duration = 0;
  
  double psnr[4];
  double ssim;
  
  struct
    {
    int64_t pts;
    int frame_bytes;
    }
  packet_cache[PACKET_CACHE_MAX];
  int packet_cache_size = 0;

  int frame_bytes = 0;

  int64_t frame_bytes_sum = 0;
  double psnr_sum = 0.0;
  double ssim_sum = 0.0;
  int found_packet = 0;
  
  gavl_video_source_t * src_c;
  gavl_video_source_t * src_o;
  gavl_packet_source_t * src_p;
  
  memset(&format_o, 0, sizeof(format_o));
  memset(&format_c, 0, sizeof(format_c));
  memset(&packet, 0, sizeof(packet));
  memset(&ci, 0, sizeof(ci));
  
  if(argc < 3)
    {
    fprintf(stderr, "Usage: %s <video1> <video2>\n", argv[0]);
    return -1;
    }
  
  /* Create registries */

  bg_plugins_init();
  
  /* Load inputs */

  if(!load_file(bg_plugin_reg,
                &input_handle_o,
                &input_plugin_o,
                &src_o,
                argv[1]))
    {
    fprintf(stderr, "Cannot open %s\n", argv[1]);
    return -1;
    }

  if(!load_file(bg_plugin_reg,
                &input_handle_c,
                &input_plugin_c,
                &src_c,
                argv[2]))
    {
    fprintf(stderr, "Cannot open %s\n", argv[2]);
    return -1;
    }

  gavl_video_format_copy(&format_o, gavl_video_source_get_src_format(src_o));
  gavl_video_format_copy(&format_c, gavl_video_source_get_src_format(src_c));
  
  if(!load_file_compressed(bg_plugin_reg,
                           &input_handle_cmp,
                           &input_plugin_cmp,
                           argv[2],
                           &format_cmp, &ci, &src_p))
    {
    fprintf(stderr, "Cannot open %s\n", argv[2]);
    return -1;
    }

  if((format_o.image_width != format_c.image_width) ||
     (format_o.image_height != format_c.image_height) ||
     (format_o.pixelformat != format_c.pixelformat))
    {
    fprintf(stderr, "Format mismatch\n");
    return -1;
    }

  gavl_video_format_copy(&format_ssim, &format_o);
  format_ssim.pixelformat = GAVL_GRAY_FLOAT;

  gavl_video_source_set_dst(src_o, 0, &format_ssim);
  gavl_video_source_set_dst(src_c, 0, &format_ssim);
  

  frame_ssim_res = gavl_video_frame_create(&format_ssim);
  
  while(1)
    {
    frame_o = NULL;
    frame_c = NULL;

    if(gavl_video_source_read_frame(src_o, &frame_o) != GAVL_SOURCE_OK)
      break;

    if(gavl_video_source_read_frame(src_c, &frame_c) != GAVL_SOURCE_OK)
      break;
    
    duration += frame_c->duration;
    
    /* Get PSNR */
    gavl_video_frame_psnr(psnr, frame_c, frame_o, &format_ssim);

    /* Get SSIM */
#if 1
    gavl_video_frame_ssim(frame_c, frame_o, frame_ssim_res, &format_ssim);
    ssim = 0.0;
    for(i = 0; i < format_ssim.image_height; i++)
      {
      ssim_ptr =
        (float*)(frame_ssim_res->planes[0] + i * frame_ssim_res->strides[0]);
      for(j = 0; j < format_ssim.image_width; j++)
        ssim += ssim_ptr[j];
      }
    ssim /= (float)(format_ssim.image_height * format_ssim.image_width);
#endif
    /* Fill packet cache */

    while(packet_cache_size < PACKET_CACHE_MAX)
      {
      gavl_packet_t * p = &packet;
      if(gavl_packet_source_read_packet(src_p, &p) == GAVL_SOURCE_EOF)
        break;
#ifdef DUMP_COMPRESSED
      gavl_packet_dump(&packet);
#endif
      
      packet_cache[packet_cache_size].pts = packet.pts;
      packet_cache[packet_cache_size].frame_bytes = packet.buf.len;
      packet_cache_size++;
      }

    found_packet = 0;
    
    /* Get frame size for this frame */
    for(i = 0; i < packet_cache_size; i++)
      {
      if(packet_cache[i].pts == frame_c->timestamp)
        {
        frame_bytes = packet_cache[i].frame_bytes;
        found_packet = 1;
        if(i < packet_cache_size-1)
          {
          memmove(&packet_cache[i], &packet_cache[i+1],
                  sizeof(packet_cache[i]) *
                  (packet_cache_size-1 - i));
          packet_cache_size--;
          break;
          }
        }
      }
    
    if(!found_packet)
      {
      fprintf(stderr, "Found no packet with pts = %"PRId64"\n",
              frame_c->timestamp);
      return -1;
      }
    
    printf("%d %d %.6f %.6f\n", frame++, frame_bytes, psnr[0], ssim);

    fflush(stdout);
    
    frame_bytes_sum += frame_bytes;
    psnr_sum += psnr[0];
    ssim_sum += ssim;
    
    }
  
  printf("# Average values\n");
  printf("# birate      PSNR   SSIM\n");
  printf("# %.2f        %f     %f\n",
         8.0 * frame_bytes_sum /
         gavl_time_to_seconds(gavl_time_unscale(format_c.timescale, duration)),
         psnr_sum / frame, ssim_sum / frame);
  return 0;
  }
