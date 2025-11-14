/*****************************************************************
 * gmerlin - a general purpose multimedia framework and applications
 *
 * Copyright (c) 2001 - 2024 Members of the Gmerlin project
 * http://github.com/bplaum
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

#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>

#include <config.h>

#include <gavl/metatags.h>

#include <gmerlin/translation.h>

#include <gmerlin/pluginregistry.h>
#include <gmerlin/utils.h>
#include <gmerlin/filters.h>

#include <gmerlin/gui_gtk/configdialog.h>
#include <gmerlin/transcoder_track.h>
#include "tracklist.h"
#include "trackdialog.h"

#include <gmerlin/textrenderer.h>

#define AUDIO_CTX   "a"
#define VIDEO_CTX   "v"
#define TEXT_CTX    "t"
#define OVERLAY_CTX "o"

static const bg_plugin_info_t * get_encoder(bg_transcoder_track_t * track,
                                            gavl_stream_type_t type)
  {
  const char * name = NULL;
  
  switch(type)
    {
    case GAVL_STREAM_AUDIO:
      name = bg_transcoder_track_get_audio_encoder(track);
      break;
    case GAVL_STREAM_VIDEO:
      name = bg_transcoder_track_get_video_encoder(track);
      break;
    case GAVL_STREAM_TEXT:
      name = bg_transcoder_track_get_text_encoder(track);
      break;
    case GAVL_STREAM_OVERLAY:
      name = bg_transcoder_track_get_overlay_encoder(track);
      break;
    default:
      break;
    }

  if(!(name && (type != GAVL_STREAM_VIDEO)))
    name = bg_transcoder_track_get_video_encoder(track);

  if(!name)
    return NULL;

  return bg_plugin_find_by_name(name);
  
  }

static gavl_dictionary_t * get_stream_dictionary(bg_transcoder_track_t * track, const char * ctx)
  {
  int idx;
  gavl_stream_type_t type = GAVL_STREAM_NONE;
  gavl_dictionary_t * dict = NULL;
  
  char ** str = gavl_strbreak(ctx, ':');

  if(!str || !str[0])
    goto fail;

  if(!strcmp(str[0], AUDIO_CTX))
    type = GAVL_STREAM_AUDIO;
  else if(!strcmp(str[0], VIDEO_CTX))
    type = GAVL_STREAM_VIDEO;
  else if(!strcmp(str[0], TEXT_CTX))
    type = GAVL_STREAM_TEXT;
  else if(!strcmp(str[0], OVERLAY_CTX))
    type = GAVL_STREAM_OVERLAY;
  
  if(type == GAVL_STREAM_NONE)
    goto fail;

  if(!str[1])
    goto fail;

  idx = atoi(str[1]);

  if(!str[2])
    goto fail;

  
  
  if(!(dict = gavl_track_get_stream_nc(track, type, idx)))
    goto fail;

  return gavl_dictionary_get_dictionary_nc(dict, str[2]);
  
  fail:
  if(str)
    gavl_strbreak_free(str);
  return dict;
  }

int track_dialog_handle_message(track_list_t * list, gavl_msg_t * msg)
  {
  bg_transcoder_track_t * track = list->selected_track;
  
  switch(msg->NS)
    {
    case BG_MSG_NS_PARAMETER:
      switch(msg->ID)
        {
        case BG_CMD_SET_PARAMETER:
          {
          const char * name;
          const char * ctx;
          gavl_dictionary_t * dict;

          gavl_value_t val;
          gavl_value_init(&val);
          bg_msg_get_parameter(msg, &name, &val);
          ctx = gavl_dictionary_get_string(&msg->header, GAVL_MSG_CONTEXT_ID);

          //          fprintf(stderr, "set parameter %s %s\n", ctx, name);
          
          if(!ctx)
            return 0;

          if(!strcmp(ctx, BG_TRANSCODER_TRACK_GENERAL))
            {
            /* Got general option */
            dict = bg_transcoder_track_get_cfg_general_nc(track);
            gavl_dictionary_set(dict, name, &val);
            return 1;
            }
          else if(!strcmp(ctx, GAVL_META_METADATA))
            {
            /* Got Metadata */
            bg_metadata_set_parameter(gavl_track_get_metadata_nc(track), name, &val);

            if(!name)
              track_list_update(list);
            
            return 1;
            }
          else if(name && (dict = get_stream_dictionary(track, ctx)))
            {
            gavl_dictionary_set(dict, name, &val);
            return 1;
            }
          
          }
        }
    }
  return 0;
  }

GtkWidget * track_dialog_create(track_list_t * list)
  {
  bg_cfg_ctx_t ctx;
  GtkWidget * ret;
  int i;
  int num;
  GtkTreeIter it;
  char * tmp_string;
  gavl_dictionary_t * stream;
  const bg_plugin_info_t * enc = NULL;
  const bg_plugin_info_t * enc1 = NULL;

  bg_transcoder_track_t * track = list->selected_track;
  
  ret = bg_gtk_config_dialog_create_multi(BG_GTK_CONFIG_DIALOG_OK_CANCEL,
                                          TR("Track options"),
                                          list->widget);
  

  memset(&ctx, 0, sizeof(ctx));

  /* General */
  ctx.long_name = gavl_strdup(TR("General"));
  ctx.name =  gavl_strdup(BG_TRANSCODER_TRACK_GENERAL);
  
  ctx.s = bg_transcoder_track_get_cfg_general_nc(track);
  ctx.parameters = bg_transcoder_track_get_general_parameters();
  ctx.sink = list->dlg_sink;
  
  bg_gtk_config_dialog_add_section(ret, &ctx,NULL);
  bg_cfg_ctx_free(&ctx);
  memset(&ctx, 0, sizeof(ctx));
  
  /* Metadata */
  ctx.long_name = gavl_strdup(TR("Metadata"));
  ctx.name =  gavl_strdup(GAVL_META_METADATA);
  ctx.parameters_priv = bg_metadata_get_parameters(gavl_track_get_metadata(track));
  ctx.parameters = ctx.parameters_priv;
  ctx.sink = list->dlg_sink;

  
  bg_gtk_config_dialog_add_section(ret, &ctx,NULL);
  
  bg_cfg_ctx_free(&ctx);
  memset(&ctx, 0, sizeof(ctx));
  
  /* Audio */

  num = gavl_track_get_num_audio_streams(track);

  if(num)
    enc = get_encoder(track, GAVL_STREAM_AUDIO);

  for(i = 0; i < num; i++)
    {
    if(num == 1)
      tmp_string = gavl_strdup("Audio");
    else
      tmp_string = gavl_sprintf("Audio stream #%d", i+1);

    stream = gavl_track_get_audio_stream_nc(track, i);

    bg_gtk_config_dialog_add_container(ret, tmp_string, NULL, &it);
    free(tmp_string);
    
    ctx.long_name = gavl_strdup(TR("General"));
    ctx.name =  gavl_sprintf(AUDIO_CTX":%d:"BG_TRANSCODER_TRACK_GENERAL, i);
    ctx.parameters = bg_transcoder_track_audio_get_general_parameters();
    ctx.sink = list->dlg_sink;
    
    ctx.s = bg_transcoder_track_get_cfg_general_nc(stream);
    
    bg_gtk_config_dialog_add_section(ret, &ctx, &it);
    bg_cfg_ctx_free(&ctx);
    memset(&ctx, 0, sizeof(ctx));
        
    ctx.long_name = gavl_strdup(TR("Filters"));
    ctx.name =  gavl_sprintf(AUDIO_CTX":%d:"BG_TRANSCODER_TRACK_FILTER, i);
    ctx.parameters = bg_audio_filter_chain_get_parameters();
    ctx.sink = list->dlg_sink;
    
    ctx.s = bg_transcoder_track_get_cfg_filter_nc(stream);
    
    bg_gtk_config_dialog_add_section(ret, &ctx, &it);
    
    bg_cfg_ctx_free(&ctx);
    memset(&ctx, 0, sizeof(ctx));

    if(enc && enc->audio_parameters)
      {
      ctx.long_name = gavl_strdup(TR("Encoder"));
      ctx.name =  gavl_sprintf(AUDIO_CTX":%d:"BG_TRANSCODER_TRACK_ENCODER, i);
      ctx.s = bg_transcoder_track_get_cfg_encoder_nc(stream);
      ctx.parameters = enc->audio_parameters;
      ctx.sink = list->dlg_sink;

      bg_gtk_config_dialog_add_section(ret, &ctx, &it);

      
      bg_cfg_ctx_free(&ctx);
      memset(&ctx, 0, sizeof(ctx));

      }
    
    
    }
  
  /* Video */
  num = gavl_track_get_num_video_streams(track); 

  if(num)
    enc = get_encoder(track, GAVL_STREAM_VIDEO);
  
  for(i = 0; i < num; i++)
    {
    if(num == 1)
      tmp_string = gavl_strdup("Video");
    else
      tmp_string = gavl_sprintf("Video stream #%d", i+1);
    bg_gtk_config_dialog_add_container(ret, tmp_string, NULL, &it);
    free(tmp_string);

    stream = gavl_track_get_video_stream_nc(track, i);
    
    ctx.long_name = gavl_strdup(TR("General"));
    ctx.name =  gavl_sprintf(VIDEO_CTX":%d:"BG_TRANSCODER_TRACK_GENERAL, i);
    ctx.parameters = bg_transcoder_track_video_get_general_parameters();
    ctx.sink = list->dlg_sink;
    
    ctx.s = bg_transcoder_track_get_cfg_general_nc(stream);

    bg_gtk_config_dialog_add_section(ret, &ctx, &it);
    bg_cfg_ctx_free(&ctx);
    memset(&ctx, 0, sizeof(ctx));
    
    ctx.long_name = gavl_strdup(TR("Filters"));
    ctx.name =  gavl_sprintf(VIDEO_CTX":%d:"BG_TRANSCODER_TRACK_FILTER, i);
    ctx.parameters = bg_video_filter_chain_get_parameters();
    
    ctx.s = bg_transcoder_track_get_cfg_filter_nc(stream);
    ctx.sink = list->dlg_sink;
    
    bg_gtk_config_dialog_add_section(ret, &ctx, &it);
    
    
    bg_cfg_ctx_free(&ctx);
    memset(&ctx, 0, sizeof(ctx));
    
    if(enc && enc->video_parameters)
      {
      ctx.long_name = gavl_strdup(TR("Encoder"));
      ctx.name =  gavl_sprintf(VIDEO_CTX":%d:"BG_TRANSCODER_TRACK_ENCODER, i);
      ctx.parameters = enc->video_parameters;
      ctx.sink = list->dlg_sink;
      
      ctx.s = bg_transcoder_track_get_cfg_encoder_nc(stream);

      bg_gtk_config_dialog_add_section(ret, &ctx, &it);
      bg_cfg_ctx_free(&ctx);
      memset(&ctx, 0, sizeof(ctx));
      }
    
    }
  
  /* Text */
  num = gavl_track_get_num_text_streams(track); 

  if(num)
    {
    enc = get_encoder(track, GAVL_STREAM_TEXT);
    enc1 = get_encoder(track, GAVL_STREAM_OVERLAY);
    }
  for(i = 0; i < num; i++)
    {
    if(num == 1)
      tmp_string = gavl_strdup("Text");
    else
      tmp_string = gavl_sprintf("Text stream #%d", i+1);
    bg_gtk_config_dialog_add_container(ret, tmp_string, NULL, &it);
    free(tmp_string);

    stream = gavl_track_get_text_stream_nc(track, i);
    
    ctx.long_name = gavl_strdup(TR("General"));
    ctx.name =  gavl_sprintf(TEXT_CTX":%d:"BG_TRANSCODER_TRACK_GENERAL, i);
    ctx.parameters = bg_transcoder_track_text_get_general_parameters();
    ctx.sink = list->dlg_sink;
    
    ctx.s = bg_transcoder_track_get_cfg_general_nc(stream);

    bg_gtk_config_dialog_add_section(ret, &ctx, &it);
    bg_cfg_ctx_free(&ctx);
    memset(&ctx, 0, sizeof(ctx));

    if(enc && enc->text_parameters)
      {

      ctx.long_name = gavl_strdup(TR("Text encoder"));
      ctx.name =  gavl_sprintf(TEXT_CTX":%d:"BG_TRANSCODER_TRACK_GENERAL, i);
      ctx.parameters = enc->text_parameters;
      ctx.sink = list->dlg_sink;
      
      ctx.s = bg_transcoder_track_get_cfg_encoder_text_nc(stream);

      bg_gtk_config_dialog_add_section(ret, &ctx, &it);
      bg_cfg_ctx_free(&ctx);
      memset(&ctx, 0, sizeof(ctx));


      }

    if(enc1 && enc1->overlay_parameters)
      {
      ctx.long_name = gavl_strdup(TR("Overlay encoder"));
      ctx.name =  gavl_sprintf(TEXT_CTX":%d:"BG_TRANSCODER_TRACK_GENERAL, i);
      ctx.parameters = enc1->overlay_parameters;
      ctx.sink = list->dlg_sink;
      
      ctx.s = bg_transcoder_track_get_cfg_encoder_overlay_nc(stream);

      bg_gtk_config_dialog_add_section(ret, &ctx, &it);
      bg_cfg_ctx_free(&ctx);
      memset(&ctx, 0, sizeof(ctx));
      }
    
    }
  
  /* Overlay */
  num = gavl_track_get_num_overlay_streams(track);

  if(num)
    enc = get_encoder(track, GAVL_STREAM_OVERLAY);
  
  for(i = 0; i < num; i++)
    {
    if(num == 1)
      tmp_string = gavl_strdup("Overlays");
    else
      tmp_string = gavl_sprintf("Overlay stream #%d", i+1);
    bg_gtk_config_dialog_add_container(ret, tmp_string, NULL, &it);
    free(tmp_string);

    stream = gavl_track_get_overlay_stream_nc(track, i);
    
    ctx.long_name = gavl_strdup(TR("General"));
    ctx.name =  gavl_sprintf(OVERLAY_CTX":%d:"BG_TRANSCODER_TRACK_GENERAL, i);
    ctx.parameters = bg_transcoder_track_overlay_get_general_parameters();
    ctx.sink = list->dlg_sink;
    
    ctx.s = bg_transcoder_track_get_cfg_general_nc(stream);
    
    bg_gtk_config_dialog_add_section(ret, &ctx, &it);
    bg_cfg_ctx_free(&ctx);
    memset(&ctx, 0, sizeof(ctx));
    }
  
  return ret;
  
  }

