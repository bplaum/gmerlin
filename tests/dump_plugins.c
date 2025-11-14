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

#include <config.h>
#include <gmerlin/pluginregistry.h>
#include <gmerlin/utils.h>

#include <stdio.h>

#define FORMAT_HTML    0
#define FORMAT_TEXI    1

int format = FORMAT_HTML;

static struct
  {
  bg_plugin_type_t type;
  char * name;
  char * name_internal;
  }
plugin_types[] =
  {
    { BG_PLUGIN_INPUT, "Media input", "plugin_i" },
    { BG_PLUGIN_OUTPUT_AUDIO, "Audio output", "plugin_oa" },
    { BG_PLUGIN_OUTPUT_VIDEO, "Video output", "plugin_ov"  },
    { BG_PLUGIN_ENCODER_AUDIO, "Encoders for audio", "plugin_ea"  },
    { BG_PLUGIN_ENCODER_VIDEO, "Encoders for video", "plugin_ev"  },
    { BG_PLUGIN_ENCODER_TEXT, "Encoders for text subtitles", "plugin_est"  },
    { BG_PLUGIN_ENCODER_OVERLAY, "Encoders for overlay subtitles", "plugin_eso"  },
    { BG_PLUGIN_ENCODER, "Encoders for multiple stream types", "plugin_e"  },
    { BG_PLUGIN_IMAGE_READER, "Image readers", "plugin_ir"  },
    { BG_PLUGIN_IMAGE_WRITER, "Image writers", "plugin_iw"  },
    { BG_PLUGIN_FILTER_AUDIO, "Audio filters", "plugin_fa"  },
    { BG_PLUGIN_FILTER_VIDEO, "Video filters", "plugin_fv"  },
    { BG_PLUGIN_VISUALIZATION, "Visualizations", "plugin_vis"  },
    { },
  };

char * internal_name = "Internal plugin";

static const char * get_filename(const char * filename)
  {
  const char * pos;
  const char * ret = NULL;

  if(!filename)
    return  internal_name;
  
  pos = filename;
  while(*pos != '\0')
    {
    if(*pos == '/')
      ret = pos+1;
    pos++;
    }
  return ret;
  }

static void dump_plugins(bg_plugin_registry_t * plugin_reg,
                         int type)
  {
  int i, num;
  const bg_plugin_info_t * info;
  
  num = bg_get_num_plugins(type, BG_PLUGIN_ALL);

  for(i = 0; i < num; i++)
    {
    info = bg_plugin_find_by_index(i, type, BG_PLUGIN_ALL);
    if(info->api != BG_PLUGIN_API_GMERLIN)
      continue;
    
    if(format == FORMAT_HTML)
      {
      printf("<table border=\"1\" width=\"100%%\">\n");
      printf("  <tr><td width=\"15%%\">Internal name</td><td>%s</td></tr>\n", bg_plugin_info_set_name(info));
      printf("  <tr><td>External name</td><td>%s</td></tr>\n", bg_plugin_info_get_long_name(info));
      printf("  <tr><td>Module</td><td>%s</td></tr>\n", get_filename(info->module_filename));
      printf("  <tr><td valign=\"top\">Description</td><td>%s</td></tr>\n", info->description);
      
      printf("</table><p>\n");
      }
    else
      {
      printf("@subsection %s\n", bg_plugin_info_set_name(info));
      printf("@table @i\n");
      printf("@item Internal name\n%s\n", info->name);
      printf("@item Module\n%s\n", get_filename(info->module_filename));
      printf("@item Description\n%s\n", info->description);
      printf("@end table\n");
      
      }
    
    }

  
  
  }

int main(int argc, char ** argv)
  {
  int i;

  if(argc > 1)
    {
    if(!strcmp(argv[1], "-t"))
      format = FORMAT_TEXI;
    else
      {
      fprintf(stderr, "Usage: %s [-t]\n", argv[0]);
      return -1;
      }
    }
  
  /* Create plugin registry */
  bg_plugins_init();
  

  /*
    @menu
* Introduction: gui_intro.
* Static and dynamic .parameters = gui_statdyn.
* Configuring input plugins: gui_i.
* Configuring filters: gui_f.
* Log messages: gui_log.
* Tips: gui_tips.
@end menu
  */
  
  if(format == FORMAT_TEXI)
    {
    printf("@menu\n");
    }
  else
    {
    printf("<!DOCTYPE html PUBLIC \"-//W3C//DTD HTML 4.01//EN\">\n");
    printf("<html>\n");
    printf("<head>\n");
    printf("  <title>Gmerlin</title>\n");
    printf("  <link rel=\"stylesheet\" href=\"css/style.css\">\n");
    printf("</head>\n\n");

    printf("<body>\n");
    printf("<h1>Plugins</h1>\n");
    printf("The following are only the plugins, you can download from this site.\n");
    printf("Gmerlin applications also load <a href=\"http://www.ladspa.org\" target=\"_top\">ladspa</a>, \n");
    printf("<a href=\"http://sourceforge.net/projects/libvisual\" target=\"_top\">libvisual</a> \n");
    printf("and <a href=\"http://www.piksel.org/frei0r\" target=\"_top\">frei0r</a> plugins.\n");
    printf("<p>\n");

    }
  
  i = 0;
  while(plugin_types[i].name)
    {
    if(format == FORMAT_HTML)
      {
      printf("<a href=\"#%s\">%s</a><br>\n", plugin_types[i].name_internal,
             plugin_types[i].name);
      }
    else
      {
      printf("* %s: %s.\n", plugin_types[i].name,
             plugin_types[i].name_internal);
      
      }
    i++;
    }
  if(format == FORMAT_TEXI)
    {
    printf("@end menu\n\n");
    }
  
  i = 0;
  while(plugin_types[i].name)
    {
    if(format == FORMAT_HTML)
      {
      printf("<a name=\"%s\"></a><h2>%s</h2><br>\n",
             plugin_types[i].name_internal, plugin_types[i].name);
      }
    else
      {
      printf("@node %s\n@section %s\n\n",
             plugin_types[i].name_internal, plugin_types[i].name);
      }
    dump_plugins(bg_plugin_reg, plugin_types[i].type);
    i++;
    }

  if(format == FORMAT_HTML)
    {
    printf("</body></html>\n");
    }
  
  return 0;
  }
