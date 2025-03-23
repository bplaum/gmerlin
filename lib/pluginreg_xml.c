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



#include <string.h>
#include <locale.h>

#include <gmerlin/pluginregistry.h>
#include <pluginreg_priv.h>
#include <gmerlin/utils.h>
#include <gmerlin/xmlutils.h>
#include <gmerlin/bggavl.h>


static const struct
  {
  char * name;
  bg_plugin_type_t type;
  }
type_names[] =
  {
    { "Input",                   BG_PLUGIN_INPUT },
    { "OutputAudio",             BG_PLUGIN_OUTPUT_AUDIO },
    { "OutputVideo",             BG_PLUGIN_OUTPUT_VIDEO     },
    { "EncoderAudio",            BG_PLUGIN_ENCODER_AUDIO    },
    { "EncoderVideo",            BG_PLUGIN_ENCODER_VIDEO    },
    { "EncoderSubtitleText",     BG_PLUGIN_ENCODER_TEXT     },
    { "EncoderSubtitleOverlay",  BG_PLUGIN_ENCODER_OVERLAY  },
    { "Encoder",                 BG_PLUGIN_ENCODER          },
    { "ImageReader",             BG_PLUGIN_IMAGE_READER     },
    { "ImageWriter",             BG_PLUGIN_IMAGE_WRITER     },
    { "AudioFilter",             BG_PLUGIN_FILTER_AUDIO     },
    { "VideoFilter",             BG_PLUGIN_FILTER_VIDEO     },
    { "Visualization",           BG_PLUGIN_VISUALIZATION    },
    { "AudioCompressor",         BG_PLUGIN_COMPRESSOR_AUDIO },
    { "VideoCompressor",         BG_PLUGIN_COMPRESSOR_VIDEO },
    { "AudioDecompressor",       BG_PLUGIN_DECOMPRESSOR_AUDIO },
    { "VideoDecompressor",       BG_PLUGIN_DECOMPRESSOR_VIDEO },
    { "ResourceDetector",        BG_PLUGIN_RESOURCE_DETECTOR },
    { "BackendMDB",              BG_PLUGIN_BACKEND_MDB     },
    { "BackendRenderer",         BG_PLUGIN_BACKEND_RENDERER   },
    { "FrontendMDB",             BG_PLUGIN_FRONTEND_MDB     },
    { "FrontendRenderer",        BG_PLUGIN_FRONTEND_RENDERER   },
    { "Control",                 BG_PLUGIN_CONTROL   },
    { NULL,                      BG_PLUGIN_NONE }
  };

static const struct
  {
  char * name;
  int api;
  }
api_names[] =
  {
    { "gmerlin",                 BG_PLUGIN_API_GMERLIN },
    { "ladspa",                  BG_PLUGIN_API_LADSPA  },
    { "lv",                      BG_PLUGIN_API_LV  },
    { "frei0r",                  BG_PLUGIN_API_FREI0R },
    { NULL,                  BG_PLUGIN_NONE }
  };

static const struct
  {
  char * name;
  int flag;
  }
flag_names[] =
  {
    { "File",           BG_PLUGIN_FILE             },
    { "Pipe",           BG_PLUGIN_PIPE            }, /* Plugin reads from stdin */
    { "Tuner",          BG_PLUGIN_TUNER           }, /* Plugin has tuner */
    { "Filter1",        BG_PLUGIN_FILTER_1        }, /* Filter with one input port */
    { "EmbedWindow",    BG_PLUGIN_EMBED_WINDOW    },
    { "Broadcast",      BG_PLUGIN_BROADCAST       },
    { "Devparam",       BG_PLUGIN_DEVPARAM        },
    { "OVStill",        BG_PLUGIN_OV_STILL        },
    { "Overlays",       BG_PLUGIN_HANDLES_OVERLAYS },
    { "GAVFIO",                  BG_PLUGIN_GAVF_IO            },
    { "Unsupported",    BG_PLUGIN_UNSUPPORTED     },
    { "NeedsHTTP",      BG_PLUGIN_NEEDS_HTTP_SERVER     },
    { "NeedsTerminal",  BG_PLUGIN_NEEDS_TERMINAL  },
    { NULL,    0                                  },
  };

static const char * const plugin_key            = "PLUGIN";
static const char * const plugin_registry_key   = "PLUGIN_REGISTRY";

static const char * const name_key              = "NAME";
static const char * const long_name_key         = "LONG_NAME";
static const char * const description_key       = "DESCRIPTION";
static const char * const mimetypes_key         = "MIMETYPES";
static const char * const extensions_key        = "EXTENSIONS";
static const char * const compressions_key      = "COMPRESSIONS";
static const char * const codec_tags_key        = "CODEC_TAGS";
static const char * const protocols_key         = "PROTOCOLS";
static const char * const module_filename_key   = "MODULE_FILENAME";
static const char * const module_time_key       = "MODULE_TIME";
static const char * const type_key              = "TYPE";
static const char * const flags_key             = "FLAGS";
static const char * const priority_key          = "PRIORITY";
static const char * const max_audio_streams_key = "MAX_AUDIO_STREAMS";
static const char * const max_video_streams_key = "MAX_VIDEO_STREAMS";
static const char * const max_text_streams_key = "MAX_TEXT_STREAMS";
static const char * const max_overlay_streams_key = "MAX_OVERLAY_STREAMS";

static const char * const parameters_key       = "PARAMETERS";
static const char * const audio_parameters_key = "AUDIO_PARAMETERS";
static const char * const video_parameters_key = "VIDEO_PARAMETERS";
static const char * const text_parameters_key = "TEXT_PARAMETERS";
static const char * const overlay_parameters_key = "OVERLAY_PARAMETERS";

static const char * const gettext_domain_key   = "GETTEXT_DOMAIN";
static const char * const gettext_directory_key       = "GETTEXT_DIRECTORY";

static const char * const api_key                  = "API";
static const char * const index_key                = "INDEX";

const char * bg_plugin_type_to_string(bg_plugin_type_t type)
  {
  int idx = 0;
  while(type_names[idx].name)
    {
    if(type == type_names[idx].type)
      {
      return type_names[idx].name;
      }
    idx++;
    }
  return NULL;
  }

bg_plugin_type_t bg_plugin_type_from_string(const char * name)
  {
  int idx = 0;
  while(type_names[idx].name)
    {
    if(!strcmp(name, type_names[idx].name))
      {
      return type_names[idx].type;
      }
    idx++;
    }
  return 0;
  }


static bg_plugin_info_t * load_plugin(xmlDocPtr doc, xmlNodePtr node)
  {
  char * tmp_string;
  xmlNodePtr cur;
  int index;
  char * start_ptr;
  char * end_ptr;
  
  bg_plugin_info_t * ret;

  ret = calloc(1, sizeof(*ret));
  
  cur = node->children;
    
  while(cur)
    {
    if(!cur->name)
      {
      cur = cur->next;
      continue;
      }

    if(!BG_XML_STRCMP(cur->name, parameters_key))
      {
      ret->parameters = bg_xml_2_parameters(doc, cur);
      cur = cur->next;
      continue;
      }
    else if(!BG_XML_STRCMP(cur->name, audio_parameters_key))
      {
      ret->audio_parameters = bg_xml_2_parameters(doc, cur);
      cur = cur->next;
      continue;
      }
    else if(!BG_XML_STRCMP(cur->name, video_parameters_key))
      {
      ret->video_parameters = bg_xml_2_parameters(doc, cur);
      cur = cur->next;
      continue;
      }
    else if(!BG_XML_STRCMP(cur->name, text_parameters_key))
      {
      ret->text_parameters = bg_xml_2_parameters(doc, cur);
      cur = cur->next;
      continue;
      }
    else if(!BG_XML_STRCMP(cur->name, overlay_parameters_key))
      {
      ret->overlay_parameters = bg_xml_2_parameters(doc, cur);
      cur = cur->next;
      continue;
      }
    
    tmp_string = (char*)xmlNodeListGetString(doc, cur->children, 1);

    if(!BG_XML_STRCMP(cur->name, name_key))
      {
      ret->name = gavl_strrep(ret->name, tmp_string);
      }
    else if(!BG_XML_STRCMP(cur->name, long_name_key))
      {
      ret->long_name = gavl_strrep(ret->long_name, tmp_string);
      }
    else if(!BG_XML_STRCMP(cur->name, description_key))
      {
      ret->description = gavl_strrep(ret->description, tmp_string);
      }
    else if(!BG_XML_STRCMP(cur->name, mimetypes_key))
      {
      ret->mimetypes = gavl_value_set_array(&ret->mimetypes_val);
      bg_string_to_string_array(tmp_string, ret->mimetypes);
      }
    else if(!BG_XML_STRCMP(cur->name, extensions_key))
      {
      ret->extensions = gavl_value_set_array(&ret->extensions_val);
      bg_string_to_string_array(tmp_string, ret->extensions);
      }
    else if(!BG_XML_STRCMP(cur->name, protocols_key))
      {
      ret->protocols = gavl_value_set_array(&ret->protocols_val);
      bg_string_to_string_array(tmp_string, ret->protocols);
      }
    else if(!BG_XML_STRCMP(cur->name, compressions_key))
      {
      int num;
      char ** comp_list;

      comp_list = gavl_strbreak(tmp_string, ' ');

      num = 0;
      while(comp_list[num])
        num++;
      ret->compressions = calloc(num+1, sizeof(*ret->compressions));

      num = 0;
      
      while(comp_list[num])
        {
        ret->compressions[num] = gavl_compression_from_short_name(comp_list[num]);
        num++;
        }
      gavl_strbreak_free(comp_list);
      }
    else if(!BG_XML_STRCMP(cur->name, codec_tags_key))
      {
      int num;
      char ** comp_list;

      comp_list = gavl_strbreak(tmp_string, ' ');

      num = 0;
      while(comp_list[num])
        num++;
      ret->codec_tags = calloc(num+1, sizeof(*ret->codec_tags));

      num = 0;
      
      while(comp_list[num])
        {
        ret->codec_tags[num] = atoi(comp_list[num]);
        num++;
        }
      gavl_strbreak_free(comp_list);
      }
    else if(!BG_XML_STRCMP(cur->name, module_filename_key))
      {
      ret->module_filename = gavl_strrep(ret->module_filename, tmp_string);
      }
    else if(!BG_XML_STRCMP(cur->name, gettext_domain_key))
      {
      ret->gettext_domain = gavl_strrep(ret->gettext_domain, tmp_string);
      }
    else if(!BG_XML_STRCMP(cur->name, gettext_directory_key))
      {
      ret->gettext_directory = gavl_strrep(ret->gettext_directory, tmp_string);
      }
    else if(!BG_XML_STRCMP(cur->name, module_time_key))
      {
      sscanf(tmp_string, "%ld", &ret->module_time);
      }
    else if(!BG_XML_STRCMP(cur->name, priority_key))
      {
      sscanf(tmp_string, "%d", &ret->priority);
      }
    else if(!BG_XML_STRCMP(cur->name, index_key))
      {
      sscanf(tmp_string, "%d", &ret->index);
      }
    else if(!BG_XML_STRCMP(cur->name, type_key))
      {
      ret->type = bg_plugin_type_from_string(tmp_string);
      }
    else if(!BG_XML_STRCMP(cur->name, api_key))
      {
      index = 0;
      while(api_names[index].name)
        {
        if(!strcmp(tmp_string, api_names[index].name))
          {
          ret->api = api_names[index].api;
          break;
          }
        index++;
        }
      }
    else if(!BG_XML_STRCMP(cur->name, flags_key))
      {
      start_ptr = tmp_string;
      
      while(1)
        {
        if(!start_ptr) break;
        
        end_ptr = strchr(start_ptr, '|');
        if(!end_ptr)
          end_ptr = &start_ptr[strlen(start_ptr)];

        index = 0;
        while(flag_names[index].name)
          {
          if(!strncmp(flag_names[index].name, start_ptr, end_ptr - start_ptr))
            ret->flags |= flag_names[index].flag;
          index++;
          }
        if(*end_ptr == '\0')
          break;
        start_ptr = end_ptr;
        
        start_ptr++;
        }
      }
    else if(!BG_XML_STRCMP(cur->name, max_audio_streams_key))
      {
      ret->max_audio_streams = atoi(tmp_string);
      }
    else if(!BG_XML_STRCMP(cur->name, max_video_streams_key))
      {
      ret->max_video_streams = atoi(tmp_string);
      }
    else if(!BG_XML_STRCMP(cur->name, max_text_streams_key))
      {
      ret->max_text_streams = atoi(tmp_string);
      }
    else if(!BG_XML_STRCMP(cur->name, max_overlay_streams_key))
      {
      ret->max_overlay_streams = atoi(tmp_string);
      }
    xmlFree(tmp_string);
    cur = cur->next;
    }
  return ret;
  }

static const char * get_flag_name(uint32_t flag)
  {
  int index = 0;
  
  while(flag_names[index].name)
    {
    if(flag_names[index].flag == flag)
      break;
    else
      index++;
    }
  return flag_names[index].name;
  }


static void save_plugin(xmlNodePtr parent, const bg_plugin_info_t * info)
  {
  char buffer[1024];
  int index;
  int i;
  int num_flags;
  const char * flag_name;
  const char * type_name;

  uint32_t flag;
  
  xmlNodePtr xml_plugin;
  xmlNodePtr xml_item;
  char * tmp_string;
  
  //  fprintf(stderr, "Save plugin: %s\n", info->name);
  
  xmlAddChild(parent, BG_XML_NEW_TEXT("\n"));
    
  xml_plugin = xmlNewTextChild(parent, NULL,
                               (xmlChar*)plugin_key, NULL);
  xmlAddChild(xml_plugin, BG_XML_NEW_TEXT("\n"));

  xml_item = xmlNewTextChild(xml_plugin, NULL, (xmlChar*)name_key, NULL);
  xmlAddChild(xml_item, BG_XML_NEW_TEXT(info->name));
  xmlAddChild(xml_plugin, BG_XML_NEW_TEXT("\n"));

  xml_item = xmlNewTextChild(xml_plugin, NULL, (xmlChar*)long_name_key, NULL);
  xmlAddChild(xml_item, BG_XML_NEW_TEXT(info->long_name));
  xmlAddChild(xml_plugin, BG_XML_NEW_TEXT("\n"));

  xml_item = xmlNewTextChild(xml_plugin, NULL, (xmlChar*)description_key, NULL);
  xmlAddChild(xml_item, BG_XML_NEW_TEXT(info->description));
  xmlAddChild(xml_plugin, BG_XML_NEW_TEXT("\n"));
  
  xml_item = xmlNewTextChild(xml_plugin, NULL, (xmlChar*)module_filename_key,
                             NULL);
  xmlAddChild(xml_item, BG_XML_NEW_TEXT(info->module_filename));
  xmlAddChild(xml_plugin, BG_XML_NEW_TEXT("\n"));
  
  if(info->extensions && info->extensions->num_entries)
    {
    tmp_string = bg_string_array_to_string(info->extensions);
    
    xml_item = xmlNewTextChild(xml_plugin, NULL, (xmlChar*)extensions_key, NULL);
    xmlAddChild(xml_item, BG_XML_NEW_TEXT(tmp_string));
    xmlAddChild(xml_plugin, BG_XML_NEW_TEXT("\n"));
    free(tmp_string);
    }
  if(info->protocols && info->protocols->num_entries)
    {
    tmp_string = bg_string_array_to_string(info->protocols);

    xml_item = xmlNewTextChild(xml_plugin, NULL, (xmlChar*)protocols_key, NULL);
    xmlAddChild(xml_item, BG_XML_NEW_TEXT(tmp_string));
    xmlAddChild(xml_plugin, BG_XML_NEW_TEXT("\n"));
    free(tmp_string);
    }
  if(info->mimetypes && info->mimetypes->num_entries)
    {
    tmp_string = bg_string_array_to_string(info->mimetypes);

    xml_item = xmlNewTextChild(xml_plugin, NULL, (xmlChar*)mimetypes_key, NULL);
    xmlAddChild(xml_item, BG_XML_NEW_TEXT(tmp_string));
    xmlAddChild(xml_plugin, BG_XML_NEW_TEXT("\n"));
    free(tmp_string);
    }
  if(info->compressions)
    {
    int index = 0;
    tmp_string = NULL;
    
    while(info->compressions[index] != GAVL_CODEC_ID_NONE)
      {
      if(index)
        tmp_string = gavl_strcat(tmp_string, " ");
      tmp_string = gavl_strcat(tmp_string, gavl_compression_get_short_name(info->compressions[index]));
      index++;
      }
    
    xml_item = xmlNewTextChild(xml_plugin, NULL, (xmlChar*)compressions_key, NULL);
    xmlAddChild(xml_item, BG_XML_NEW_TEXT(tmp_string));
    xmlAddChild(xml_plugin, BG_XML_NEW_TEXT("\n"));
    free(tmp_string);
    }
  if(info->codec_tags)
    {
    int index = 0;
    tmp_string = NULL;
    
    while(info->codec_tags[index])
      {
      char buf[16];
      
      if(index)
        tmp_string = gavl_strcat(tmp_string, " ");
      
      snprintf(buf, 16, "%d", info->codec_tags[index]);
      tmp_string = gavl_strcat(tmp_string, buf);
      index++;
      }
    
    xml_item = xmlNewTextChild(xml_plugin, NULL, (xmlChar*)codec_tags_key, NULL);
    xmlAddChild(xml_item, BG_XML_NEW_TEXT(tmp_string));
    xmlAddChild(xml_plugin, BG_XML_NEW_TEXT("\n"));
    free(tmp_string);
    }

  if(info->gettext_domain)
    {
    xml_item = xmlNewTextChild(xml_plugin, NULL, (xmlChar*)gettext_domain_key, NULL);
    xmlAddChild(xml_item, BG_XML_NEW_TEXT(info->gettext_domain));
    xmlAddChild(xml_plugin, BG_XML_NEW_TEXT("\n"));
    }
  if(info->gettext_directory)
    {
    xml_item = xmlNewTextChild(xml_plugin, NULL, (xmlChar*)gettext_directory_key, NULL);
    xmlAddChild(xml_item, BG_XML_NEW_TEXT(info->gettext_directory));
    xmlAddChild(xml_plugin, BG_XML_NEW_TEXT("\n"));
    }

  xml_item = xmlNewTextChild(xml_plugin, NULL, (xmlChar*)module_time_key, NULL);
  sprintf(buffer, "%ld", info->module_time);
  xmlAddChild(xml_item, BG_XML_NEW_TEXT(buffer));
  xmlAddChild(xml_plugin, BG_XML_NEW_TEXT("\n"));

  xml_item = xmlNewTextChild(xml_plugin, NULL, (xmlChar*)priority_key, NULL);
  sprintf(buffer, "%d", info->priority);
  xmlAddChild(xml_item, BG_XML_NEW_TEXT(buffer));
  xmlAddChild(xml_plugin, BG_XML_NEW_TEXT("\n"));


  
  if(info->parameters)
    {
    xml_item = xmlNewTextChild(xml_plugin, NULL, (xmlChar*)parameters_key, NULL);
    bg_parameters_2_xml(info->parameters, xml_item);
    xmlAddChild(xml_plugin, BG_XML_NEW_TEXT("\n"));
    }
  if(info->audio_parameters)
    {
    xml_item = xmlNewTextChild(xml_plugin, NULL, (xmlChar*)audio_parameters_key, NULL);
    bg_parameters_2_xml(info->audio_parameters, xml_item);
    xmlAddChild(xml_plugin, BG_XML_NEW_TEXT("\n"));
    }
  if(info->video_parameters)
    {
    xml_item = xmlNewTextChild(xml_plugin, NULL, (xmlChar*)video_parameters_key, NULL);
    bg_parameters_2_xml(info->video_parameters, xml_item);
    xmlAddChild(xml_plugin, BG_XML_NEW_TEXT("\n"));
    }
  if(info->text_parameters)
    {
    xml_item = xmlNewTextChild(xml_plugin, NULL, (xmlChar*)text_parameters_key, NULL);
    bg_parameters_2_xml(info->text_parameters, xml_item);
    xmlAddChild(xml_plugin, BG_XML_NEW_TEXT("\n"));
    }
  if(info->overlay_parameters)
    {
    xml_item = xmlNewTextChild(xml_plugin, NULL, (xmlChar*)overlay_parameters_key, NULL);
    bg_parameters_2_xml(info->overlay_parameters, xml_item);
    xmlAddChild(xml_plugin, BG_XML_NEW_TEXT("\n"));
    }
  
  if(info->type & (BG_PLUGIN_ENCODER_AUDIO|
                   BG_PLUGIN_ENCODER_VIDEO|
                   BG_PLUGIN_ENCODER |
                   BG_PLUGIN_ENCODER_TEXT |
                   BG_PLUGIN_ENCODER_OVERLAY))
    {
    xml_item = xmlNewTextChild(xml_plugin, NULL, (xmlChar*)max_audio_streams_key, NULL);
    sprintf(buffer, "%d", info->max_audio_streams);
    xmlAddChild(xml_item, BG_XML_NEW_TEXT(buffer));
    xmlAddChild(xml_plugin, BG_XML_NEW_TEXT("\n"));

    xml_item = xmlNewTextChild(xml_plugin, NULL, (xmlChar*)max_video_streams_key, NULL);
    sprintf(buffer, "%d", info->max_video_streams);
    xmlAddChild(xml_item, BG_XML_NEW_TEXT(buffer));
    xmlAddChild(xml_plugin, BG_XML_NEW_TEXT("\n"));

    xml_item = xmlNewTextChild(xml_plugin, NULL, (xmlChar*)max_text_streams_key, NULL);
    sprintf(buffer, "%d", info->max_text_streams);
    xmlAddChild(xml_item, BG_XML_NEW_TEXT(buffer));
    xmlAddChild(xml_plugin, BG_XML_NEW_TEXT("\n"));

    xml_item = xmlNewTextChild(xml_plugin, NULL, (xmlChar*)max_overlay_streams_key, NULL);
    sprintf(buffer, "%d", info->max_overlay_streams);
    xmlAddChild(xml_item, BG_XML_NEW_TEXT(buffer));
    xmlAddChild(xml_plugin, BG_XML_NEW_TEXT("\n"));
    
    }
  
  index = 0;
  
  if((type_name = bg_plugin_type_to_string(info->type)))
    {
    xml_item = xmlNewTextChild(xml_plugin, NULL, (xmlChar*)type_key, NULL);
    xmlAddChild(xml_item, BG_XML_NEW_TEXT(type_name));
    xmlAddChild(xml_plugin, BG_XML_NEW_TEXT("\n"));
    }
  
  if(info->api)
    {
    index = 0;
    while(api_names[index].name)
      {
      if(info->api == api_names[index].api)
        {
        xml_item = xmlNewTextChild(xml_plugin, NULL, (xmlChar*)api_key, NULL);
        xmlAddChild(xml_item, BG_XML_NEW_TEXT(api_names[index].name));
        xmlAddChild(xml_plugin, BG_XML_NEW_TEXT("\n"));
        break;
        }
      index++;
      }

    xml_item = xmlNewTextChild(xml_plugin, NULL, (xmlChar*)index_key, NULL);
    sprintf(buffer, "%d", info->index);
    xmlAddChild(xml_item, BG_XML_NEW_TEXT(buffer));
    xmlAddChild(xml_plugin, BG_XML_NEW_TEXT("\n"));
    }
  
  /* Write flags */

  if(info->flags)
    {
    num_flags = 0;
    
    for(i = 0; i < 32; i++)
      {
      flag = (1<<i);
      if(info->flags & flag)
        num_flags++;
      }
    buffer[0] = '\0';
    index = 0;
  
    for(i = 0; i < 32; i++)
      {
      flag = (1<<i);
      if(!(info->flags & flag))
        continue;
    
      flag_name = get_flag_name(flag);
      strcat(buffer, flag_name);
      if(index < num_flags-1)
        strcat(buffer, "|");
      index++;
      }
    xml_item = xmlNewTextChild(xml_plugin, NULL, (xmlChar*)flags_key, NULL);
    xmlAddChild(xml_item, BG_XML_NEW_TEXT(buffer));
    xmlAddChild(xml_plugin, BG_XML_NEW_TEXT("\n"));
    }

  }

bg_plugin_info_t * bg_plugin_registry_load(const char * filename)
  {
  bg_plugin_info_t * ret;
  bg_plugin_info_t * end;
  bg_plugin_info_t * new;

  xmlDocPtr xml_doc;
  xmlNodePtr node;
  ret = NULL;
  end = NULL;
  
  xml_doc = bg_xml_parse_file(filename, 1);

  if(!xml_doc)
    return NULL;

  node = xml_doc->children;

  if(BG_XML_STRCMP(node->name, plugin_registry_key))
    {
    xmlFreeDoc(xml_doc);
    return NULL;
    }

  node = node->children;
    
  while(node)
    {
    if(node->name && !BG_XML_STRCMP(node->name, plugin_key))
      {
      new = load_plugin(xml_doc, node);
      if(!new->module_filename)
        bg_plugin_info_destroy(new);
      else if(!ret)
        {
        ret = new;
        end = ret;
        }
      else
        {
        end->next = new;
        end = end->next;
        }
      }
    node = node->next;
    }
  
  xmlFreeDoc(xml_doc);
  
  return ret;
  }


void bg_plugin_registry_save(bg_plugin_info_t * info)
  {
  xmlDocPtr  xml_doc;
  xmlNodePtr xml_registry;
  char * filename;

  filename = bg_search_file_write("", "plugins.xml");
  if(!filename)
    {
    return;
    }
  
  xml_doc = xmlNewDoc((xmlChar*)"1.0");
  xml_registry = xmlNewDocRawNode(xml_doc, NULL, (xmlChar*)plugin_registry_key, NULL);
  xmlDocSetRootElement(xml_doc, xml_registry);
  while(info)
    {
    if(info->module_filename) /* We save only external plugins */
      save_plugin(xml_registry, info);
    info = info->next;
    }
  
  xmlAddChild(xml_registry, BG_XML_NEW_TEXT("\n"));

  bg_xml_save_file(xml_doc, filename, 1);
  xmlFreeDoc(xml_doc);
  free(filename);
  }
