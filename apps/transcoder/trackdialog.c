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

#include <gmerlin/cfg_dialog.h>
#include <gmerlin/transcoder_track.h>
#include "trackdialog.h"

#include <gmerlin/textrenderer.h>


struct track_dialog_s
  {
  /* Config dialog */

  bg_dialog_t * cfg_dialog;

  void (*update_callback)(void * priv);
  void * update_priv;

  bg_parameter_info_t * audio_filter_parameters;
  bg_parameter_info_t * video_filter_parameters;

  bg_parameter_info_t * metadata_parameters;
  bg_parameter_info_t * general_parameters;

  gavl_dictionary_t * metadata;
  };

static void set_parameter(void * priv, const char * name, const gavl_value_t * val)
  {
  track_dialog_t * d;
  d = priv;

  if(!name)
    {
    if(d->update_callback)
      d->update_callback(d->update_priv);
    }
  }

static void set_metadata_parameter(void * priv, const char * name, const gavl_value_t * val)
  {
  track_dialog_t * d;
  d = priv;
  fprintf(stderr, "Set metadata parameter %s\n", name); 
  gavl_value_dump(val, 2);
  fprintf(stderr, "\n");
  
  bg_metadata_set_parameter(d->metadata, name, val);
  set_parameter(priv, name, val);
  }

track_dialog_t * track_dialog_create(bg_transcoder_track_t * t,
                                     void (*update_callback)(void * priv),
                                     void * update_priv, int show_tooltips)
  {
  int i, num;
  int num_text_streams;

  char * label;
  const char * var;

  track_dialog_t * ret;
  void * parent, * child;
  const char * plugin_name;
  const bg_plugin_info_t * plugin_info;

  const char * plugin_name1;
  const bg_plugin_info_t * plugin_info1;

  bg_audio_filter_chain_t * afc;
  bg_video_filter_chain_t * vfc;
  
  bg_gavl_audio_options_t ao;
  bg_gavl_video_options_t vo;

  gavl_dictionary_t * dict;
  gavl_dictionary_t * sec;
  gavl_dictionary_t * stream;
  
  memset(&ao, 0, sizeof(ao));
  memset(&vo, 0, sizeof(vo));

  bg_gavl_audio_options_init(&ao);
  bg_gavl_video_options_init(&vo);
  
  ret = calloc(1, sizeof(*ret));

  ret->update_callback = update_callback;
  ret->update_priv     = update_priv;
  
  ret->cfg_dialog = bg_dialog_create_multi(TR("Track options"));

  /* Filter parameter */
  afc = bg_audio_filter_chain_create(&ao);
  vfc = bg_video_filter_chain_create(&vo);

  ret->audio_filter_parameters =
    bg_parameter_info_copy_array(bg_audio_filter_chain_get_parameters(afc));
  ret->video_filter_parameters =
    bg_parameter_info_copy_array(bg_video_filter_chain_get_parameters(vfc));

  bg_audio_filter_chain_destroy(afc);
  bg_video_filter_chain_destroy(vfc);
  bg_gavl_audio_options_free(&ao);
  bg_gavl_video_options_free(&vo);

  /* General parameters */

  ret->general_parameters =
    bg_transcoder_track_create_parameters(t); 
  
  /* Metadata parameters */
  
  ret->metadata = gavl_track_get_metadata_nc(t);
  ret->metadata_parameters = bg_metadata_get_parameters(ret->metadata);
  
  /* General */
  
  bg_dialog_add(ret->cfg_dialog,
                TR("General"),
                bg_transcoder_track_get_cfg_general_nc(t),
                set_parameter, ret,
                ret->general_parameters);
  
  /* Metadata */

  bg_dialog_add(ret->cfg_dialog,
                TR("Metadata"),
                NULL,
                set_metadata_parameter,
                ret,
                ret->metadata_parameters);

  sec = bg_track_get_cfg_encoder_nc(t);
  /* Audio encoder */
  
  if(gavl_track_get_num_audio_streams(t) && sec)
    {
    if((dict = gavl_dictionary_get_dictionary_create(sec, "audio_encoder")) && 
       (plugin_name = gavl_dictionary_get_string(dict, BG_CFG_TAG_NAME)) &&
       (plugin_info = bg_plugin_find_by_name(plugin_name)) &&
       plugin_info->parameters)
      {
      label = TRD(plugin_info->long_name, plugin_info->gettext_domain);
      bg_dialog_add(ret->cfg_dialog,
                    label,
                    dict,
                    NULL,
                    NULL,
                    plugin_info->parameters);
      }
    }
  
  /* Video encoder */

  if(gavl_track_get_num_video_streams(t) && sec)
    {
    if((dict = gavl_dictionary_get_dictionary_create(sec, "video_encoder")) &&
       (plugin_name = gavl_dictionary_get_string(dict, BG_CFG_TAG_NAME)) &&
       (plugin_info = bg_plugin_find_by_name(plugin_name)) &&
       plugin_info->parameters)
      {
      label = TRD(plugin_info->long_name, plugin_info->gettext_domain);
      bg_dialog_add(ret->cfg_dialog,
                    label,
                    dict,
                    NULL,
                    NULL,
                    plugin_info->parameters);
      }
    }
  /* Subtitle text encoder */

  if(gavl_track_get_num_text_streams(t) && sec)
    {
    if((dict = gavl_dictionary_get_dictionary_create(sec, "text_encoder")) &&
       (plugin_name = gavl_dictionary_get_string(dict, BG_CFG_TAG_NAME)) &&
       (plugin_info = bg_plugin_find_by_name(plugin_name)) &&
       plugin_info->parameters)
      {
      label = TRD(plugin_info->long_name, plugin_info->gettext_domain);
      bg_dialog_add(ret->cfg_dialog,
                    label,
                    dict,
                    NULL,
                    NULL,
                    plugin_info->parameters);
      }
    
    }

  /* Subtitle overlay encoder */

  if(gavl_track_get_num_text_streams(t) && gavl_track_get_num_overlay_streams(t) && sec)
    {
    if((dict = gavl_dictionary_get_dictionary_create(sec, "overlay_encoder")) &&
       (plugin_name = gavl_dictionary_get_string(dict, BG_CFG_TAG_NAME)) &&
       (plugin_info = bg_plugin_find_by_name(plugin_name)) &&
       plugin_info->parameters)
      {
      label = TRD(plugin_info->long_name, plugin_info->gettext_domain);
      bg_dialog_add(ret->cfg_dialog,
                    label,
                    dict,
                    NULL,
                    NULL,
                    plugin_info->parameters);

      }
    }
  
  /* Audio streams */

  plugin_name = bg_transcoder_track_get_audio_encoder(t);
  
  if(!plugin_name || !strcmp(plugin_name, "$to_video"))
    plugin_name = bg_transcoder_track_get_video_encoder(t);

  if(plugin_name)
    plugin_info = bg_plugin_find_by_name(plugin_name);
  else
    plugin_info = NULL;

  num = gavl_track_get_num_audio_streams(t);
  
  for(i = 0; i < num; i++)
    {
    stream = gavl_track_get_audio_stream_nc(t, i);
    
    if(num > 1)
      {
      if((var = gavl_dictionary_get_string(bg_transcoder_track_get_cfg_general(stream), GAVL_META_LABEL)))
        label = bg_sprintf(TR("Audio #%d: %s"), i+1, var);
      else
        label = bg_sprintf(TR("Audio #%d"), i+1);
      }
    else
      {
      if((var = gavl_dictionary_get_string(bg_transcoder_track_get_cfg_general(stream), GAVL_META_LABEL)))
        label = bg_sprintf(TR("Audio: %s"), var);
      else
        label = bg_sprintf(TR("Audio"));
      }
    
    parent = bg_dialog_add_parent(ret->cfg_dialog, NULL,
                                  label);
    free(label);
    
    bg_dialog_add_child(ret->cfg_dialog, parent,
                        TR("General"),
                        bg_transcoder_track_get_cfg_general_nc(stream),
                        NULL,
                        NULL,
                        bg_transcoder_track_audio_get_general_parameters());

    bg_dialog_add_child(ret->cfg_dialog, parent,
                        TR("Filters"),
                        bg_transcoder_track_get_cfg_filter_nc(stream),
                        NULL,
                        NULL,
                        ret->audio_filter_parameters);
    
    if(plugin_info && plugin_info->audio_parameters)
      {
      label = TR("Encode options");
      
      if(plugin_info->audio_parameters[0].type != BG_PARAMETER_SECTION)
        {
        bg_dialog_add_child(ret->cfg_dialog, parent,
                            label,
                            bg_stream_get_cfg_encoder_nc(stream),
                            NULL,
                            NULL,
                            plugin_info->audio_parameters);
        }
      else
        {
        child = bg_dialog_add_parent(ret->cfg_dialog, parent, label);
        bg_dialog_add_child(ret->cfg_dialog, child,
                            NULL,
                            bg_stream_get_cfg_encoder_nc(stream),
                            NULL,
                            NULL,
                            plugin_info->audio_parameters);
        }
      }
    }

  /* Video streams */
  num = gavl_track_get_num_video_streams(t);
  
  if(num)
    {
    plugin_name = bg_transcoder_track_get_video_encoder(t);
    plugin_info = bg_plugin_find_by_name(plugin_name);
  
    for(i = 0; i < num; i++)
      {
      stream = gavl_track_get_video_stream_nc(t, i);
      
      if(num > 1)
        {
        if((var = gavl_dictionary_get_string(bg_transcoder_track_get_cfg_general(stream), GAVL_META_LABEL)))
          label = bg_sprintf(TR("Video #%d: %s"), i+1, var);
        else
          label = bg_sprintf(TR("Video #%d"), i+1);
        }
      else
        {
        if((var = gavl_dictionary_get_string(bg_transcoder_track_get_cfg_general(stream), GAVL_META_LABEL)))
          label = bg_sprintf(TR("Video: %s"), var);
        else
          label = bg_sprintf(TR("Video"));
        }

      parent = bg_dialog_add_parent(ret->cfg_dialog, NULL,
                                    label);
      free(label);
    
      bg_dialog_add_child(ret->cfg_dialog, parent,
                          TR("General"),
                          bg_transcoder_track_get_cfg_general_nc(stream),
                          NULL,
                          NULL,
                          bg_transcoder_track_video_get_general_parameters());

      bg_dialog_add_child(ret->cfg_dialog, parent,
                          TR("Filters"),
                          bg_transcoder_track_get_cfg_filter_nc(stream),
                          NULL,
                          NULL,
                          ret->video_filter_parameters);
    
      if(plugin_info && plugin_info->video_parameters)
        {
        label = TR("Encode options");
      
        if(plugin_info->video_parameters[0].type != BG_PARAMETER_SECTION)
          {
          bg_dialog_add_child(ret->cfg_dialog, parent,
                              label,
                              bg_stream_get_cfg_encoder_nc(stream),
                              NULL,
                              NULL,
                              plugin_info->video_parameters);
          }
        else
          {
          child = bg_dialog_add_parent(ret->cfg_dialog, parent,
                                       label);
          bg_dialog_add_child(ret->cfg_dialog, child,
                              NULL,
                              bg_stream_get_cfg_encoder_nc(stream),
                              NULL,
                              NULL,
                              plugin_info->video_parameters);
          }
        }
      }

    
    }
  
  
  /* Subtitle streams */

  plugin_name = bg_transcoder_track_get_text_encoder(t);
  if(!plugin_name || !strcmp(plugin_name, "$to_video"))
    plugin_name = bg_transcoder_track_get_video_encoder(t);
  
  if(plugin_name)
    plugin_info = bg_plugin_find_by_name(plugin_name);
  else
    plugin_info = NULL;

  plugin_name1 = bg_transcoder_track_get_overlay_encoder(t);
  if(!plugin_name1 || !strcmp(plugin_name1, "$to_video"))
    plugin_name1 = bg_transcoder_track_get_video_encoder(t);
  
  if(plugin_name1)
    plugin_info1 = bg_plugin_find_by_name(plugin_name1);
  else
    plugin_info1 = NULL;

  num = gavl_track_get_num_text_streams(t);
  
  for(i = 0; i < num; i++)
    {
    stream = gavl_track_get_text_stream_nc(t, i);
    
    if(num > 1)
      {
      if((var = gavl_dictionary_get_string(bg_transcoder_track_get_cfg_general(stream), GAVL_META_LABEL)))
        label = bg_sprintf(TR("Subtitles #%d: %s"), i+1, var);
      else
        label = bg_sprintf(TR("Subtitles #%d"), i+1);
      }
    else
      {
      if((var = gavl_dictionary_get_string(bg_transcoder_track_get_cfg_general(stream), GAVL_META_LABEL)))
        label = bg_sprintf(TR("Subtitles: %s"), var);
      else
        label = bg_sprintf(TR("Subtitles"));
      }
    
    parent = bg_dialog_add_parent(ret->cfg_dialog, NULL,
                                  label);
    free(label);
    
    bg_dialog_add_child(ret->cfg_dialog, parent,
                        TR("General"),
                        bg_transcoder_track_get_cfg_general_nc(stream),
                        NULL,
                        NULL,
                        bg_transcoder_track_text_get_general_parameters());

    bg_dialog_add_child(ret->cfg_dialog, parent,
                        TR("Textrenderer"),
                        bg_transcoder_track_get_cfg_general_nc(stream),
                        NULL,
                        NULL,
                        bg_text_renderer_get_parameters());

    if(plugin_info->text_parameters)
      {
      label = TR("Encode options (text)");
      
      bg_dialog_add_child(ret->cfg_dialog, parent,
                          label,
                          bg_transcoder_track_get_cfg_encoder_text_nc(stream),
                          NULL,
                          NULL,
                          plugin_info->text_parameters);
      }

    if(plugin_info1->overlay_parameters)
      {
      label = TR("Encode options (overlay)");
      
      bg_dialog_add_child(ret->cfg_dialog, parent,
                          label,
                          bg_transcoder_track_get_cfg_encoder_overlay_nc(stream),
                          NULL,
                          NULL,
                          plugin_info1->overlay_parameters);
      }
    }

  num              = gavl_track_get_num_overlay_streams(t);
  num_text_streams = gavl_track_get_num_text_streams(t);
    
  for(i = 0; i < num; i++)
    {
    stream = gavl_track_get_overlay_stream_nc(t, i);
    
    if(num > 1)
      {
      if((var = gavl_dictionary_get_string(bg_transcoder_track_get_cfg_general(stream), GAVL_META_LABEL)))
        label = bg_sprintf(TR("Subtitles #%d: %s"), i + 1 + num_text_streams, var);
      else
        label = bg_sprintf(TR("Subtitles #%d"), i + 1 + num_text_streams);
      }
    else
      {
      if((var = gavl_dictionary_get_string(bg_transcoder_track_get_cfg_general(stream), GAVL_META_LABEL)))
        label = bg_sprintf(TR("Subtitles: %s"), var);
      else
        label = bg_sprintf(TR("Subtitles"));
      }
    
    parent = bg_dialog_add_parent(ret->cfg_dialog, NULL,
                                  label);
    free(label);
    
    bg_dialog_add_child(ret->cfg_dialog, parent,
                        TR("General"),
                        bg_transcoder_track_get_cfg_general_nc(stream),
                        NULL,
                        NULL,
                        bg_transcoder_track_text_get_general_parameters());

    if(plugin_info1->overlay_parameters)
      {
      label = TR("Encode options");
      bg_dialog_add_child(ret->cfg_dialog, parent,
                          label,
                          bg_stream_get_cfg_encoder_nc(stream),
                          NULL,
                          NULL,
                          plugin_info1->overlay_parameters);
      }
    }
  return ret;
  
  }

void track_dialog_run(track_dialog_t * d, GtkWidget * parent)
  {
  bg_dialog_show(d->cfg_dialog, parent);
  }

void track_dialog_destroy(track_dialog_t * d)
  {
  bg_dialog_destroy(d->cfg_dialog);

  if(d->audio_filter_parameters)
    bg_parameter_info_destroy_array(d->audio_filter_parameters);
  if(d->video_filter_parameters)
    bg_parameter_info_destroy_array(d->video_filter_parameters);
  if(d->metadata_parameters)
    bg_parameter_info_destroy_array(d->metadata_parameters);
  if(d->general_parameters)
    bg_parameter_info_destroy_array(d->general_parameters);


  
  free(d);
  }

