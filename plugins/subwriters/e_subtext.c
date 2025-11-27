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



#include <stdio.h>
#include <string.h>

#include <config.h>
#include <gmerlin/translation.h>

#include <gmerlin/plugin.h>
#include <gmerlin/pluginfuncs.h>
#include <gmerlin/utils.h>
#include <gmerlin/log.h>
#define LOG_DOMAIN "e_subtext"

#include <gavl/metatags.h>

typedef struct
  {
  FILE * output;  
  int format_index;
  char * filename;
  int titles_written;
  
  gavl_time_t last_time;
  gavl_time_t last_duration;
  
  gavl_dictionary_t metadata;
  
  bg_encoder_callbacks_t * cb;

  gavl_packet_sink_t * sink;
  
  } subtext_t;

static void write_time_srt(FILE * output, gavl_time_t time)
  {
  int msec, sec, m, h;

  msec = (time % GAVL_TIME_SCALE) / (GAVL_TIME_SCALE/1000);
  time /= GAVL_TIME_SCALE;
  sec = time % 60;
  time /= 60;
  m = time % 60;
  time /= 60;
  h = time;

  fprintf(output, "%02d:%02d:%02d,%03d", h, m, sec, msec);
  }

static void write_subtitle_srt(subtext_t * s, gavl_packet_t * p)
  {
  int i;
  char ** lines;

  if(!p->buf.len)
    {
    gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Ignoring empty subtitle");
    return;
    }

  /* Index */
  
  fprintf(s->output, "%d\r\n", s->titles_written + 1);
  
  /* Time */
  write_time_srt(s->output, p->pts);
  fprintf(s->output, " --> ");
  write_time_srt(s->output, p->pts+p->duration);
  fprintf(s->output, "\r\n");
  
  lines = gavl_strbreak((char*)p->buf.buf, '\n');
  i = 0;
  while(lines[i])
    {
    fprintf(s->output, "%s\r\n", lines[i]);
    i++;
    }
  /* Empty line */
  fprintf(s->output, "\r\n");

  fflush(s->output);
  
  gavl_strbreak_free(lines);
  }

static const struct
  {
  const char * extension;
  const char * name;
  void (*write_subtitle)(subtext_t * s, gavl_packet_t * p);
  void (*write_header)(subtext_t * s);
  }
formats[] =
  {
    {
      .extension =      "srt",
      .name =           "srt",
      .write_subtitle = write_subtitle_srt,
    },
  };

static void * create_subtext()
  {
  subtext_t * ret = calloc(1, sizeof(*ret));
  ret->last_time     = GAVL_TIME_UNDEFINED;
  ret->last_duration = GAVL_TIME_UNDEFINED;
  return ret;
  }

static void set_callbacks_subtext(void * data, bg_encoder_callbacks_t * cb)
  {
  subtext_t * e = data;
  e->cb = cb;
  }

static int open_subtext(void * data, const char * filename,
                        const gavl_dictionary_t * metadata)
  {
  subtext_t * e;
  e = data;
  
  e->filename =
    gavl_filename_ensure_extension(filename,
                                 formats[e->format_index].extension);

  if(!bg_encoder_cb_create_output_file(e->cb, e->filename))
    return 0;
  
  e->output = fopen(e->filename, "w");

  if(metadata)
    gavl_dictionary_copy(&e->metadata, metadata);
  
  return 1;
  }

static int add_text_stream_subtext(void * data,
                                   const gavl_dictionary_t * m,
                                   uint32_t * timescale)
  {
  *timescale = GAVL_TIME_SCALE;
  return 0;
  }

static gavl_sink_status_t write_subtitle_text_subtext(void * data, gavl_packet_t * p)
  {
  subtext_t * e;
  e = data;
  formats[e->format_index].write_subtitle(e, p);
  e->titles_written++;
  e->last_time     = p->pts;
  e->last_duration = p->duration;
  
  return 1;
  }

static int start_subtext(void * data)
  {
  subtext_t * e;
  e = data;
  
  if(formats[e->format_index].write_header)
    formats[e->format_index].write_header(e);
  e->sink = gavl_packet_sink_create(NULL, write_subtitle_text_subtext, e);
  return 1;
  }

static gavl_packet_sink_t * get_sink_subtext(void * data, int stream)
  {
  subtext_t * e;
  e = data;
  return e->sink;
  }

static int close_subtext(void * data, int do_delete)
  {
  subtext_t * e;
  e = data;
  if(e->output)
    {
    fclose(e->output);
    e->output = NULL;
    }
  if(do_delete)
    remove(e->filename);
  return 1;
  }

static void destroy_subtext(void * data)
  {
  subtext_t * e;
  e = data;
  if(e->output)
    close_subtext(data, 1);
  if(e->filename)
    free(e->filename);
  free(e);
  }

static const bg_parameter_info_t parameters[] =
  {
    {
      .name =         "format" ,
      .long_name =    TRS("Format"),
      .type =         BG_PARAMETER_STRINGLIST,
      .val_default =  GAVL_VALUE_INIT_STRING("srt"),
      .multi_names =  (char const *[]){ "srt", NULL },
      .multi_labels = (char const *[]){ TRS("Subrip (.srt)"), NULL },
    },
    { /* End of parameters */ }
  };

static const bg_parameter_info_t * get_parameters_subtext(void * data)
  {
  return parameters;
  }

static void set_parameter_subtext(void * data, const char * name,
                                  const gavl_value_t * val)
  {
  int i;
  subtext_t * e;
  e = data;

  if(!name)
    return;
  if(!strcmp(name, "format"))
    {
    for(i = 0; i < sizeof(formats)/sizeof(formats[0]); i++)
      {
      if(!strcmp(val->v.str, formats[i].name))
        {
        e->format_index = i;
        break;
        }
      }
    }
  }

static const char * get_extensions_subwriter(void * data)
  {
  return "srt";
  }

const bg_encoder_plugin_t the_plugin =
  {
    .common =
    {
      BG_LOCALE,
      .name =           "e_subtext",       /* Unique short name */
      .long_name =      TRS("Text subtitle exporter"),
      .description =    TRS("Plugin for exporting text subtitles. Supported format is SRT"),
      .type =           BG_PLUGIN_ENCODER_TEXT,
      .flags =          BG_PLUGIN_FILE,
      .priority =       BG_PLUGIN_PRIORITY_MAX,
      .create =         create_subtext,
      .destroy =        destroy_subtext,
      .get_parameters = get_parameters_subtext,
      .set_parameter =  set_parameter_subtext,
      .get_extensions = get_extensions_subwriter,
    },

    .max_text_streams = 1,
    
    .set_callbacks =        set_callbacks_subtext,
    
    .open =                 open_subtext,

    .add_text_stream =     add_text_stream_subtext,
    
    .start =                start_subtext,
    .get_text_sink = get_sink_subtext,
    
    .close =             close_subtext,
    
  };

/* Include this into all plugin modules exactly once
   to let the plugin loader obtain the API version */
BG_GET_PLUGIN_API_VERSION;
