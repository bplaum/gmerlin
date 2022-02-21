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
#include <unistd.h>
#include <signal.h>

#include <config.h>
#include <gmerlin/translation.h>
#include <gmerlin/plugin.h>
#include <gmerlin/utils.h>

#include <gmerlin/subprocess.h>
#include <gmerlin/log.h>
#include <gavl/metatags.h>

#define LOG_DOMAIN "i_mikmod"

// the last number ist mono(1)/stereo(2)
#define MONO8      81
#define MONO16     161
#define STEREO8    82
#define STEREO16   162

#define FRAME_SAMPLES 1024

typedef struct
  {
  bg_subprocess_t * proc;  
  gavl_dictionary_t mi;
  gavl_dictionary_t * track_info;
  int frequency;
  int output;
  int hidden_patterns;
  int force_volume;
  int use_surround;
  int use_interpolate;

  int block_align;
  int eof;
  
  // gavl_audio_source_t * src;

  bg_media_source_t ms;
  
  } i_mikmod_t;

#ifdef dump
static void dump_mikmod(void * data, const char * path)
  {
  FILE * out = stderr;
  i_mikmod_t * mikmod;
  mikmod = data;
   
  fprintf(out, "  DUMP_MIKMOD:\n");
  fprintf(out, "    Path:            %s\n", path);
  fprintf(out, "    Frequency:       %d\n", mikmod->frequency);

  if(mikmod->output == MONO8)
    fprintf(out, "    Output:          8m\n");
  else if(mikmod->output == MONO16)
    fprintf(out, "    Output:          8s\n");
  else if(mikmod->output == STEREO8)
    fprintf(out, "    Output:          16m\n");
  else if(mikmod->output == STEREO16)
    fprintf(out, "    Output:          16s\n");
  else
    fprintf(out, "    Output:          buggy mikmod.c\n");   

  fprintf(out, "    Use surround:    %d\n", mikmod->use_surround);
  fprintf(out, "    Hidden patterns: %d\n", mikmod->hidden_patterns);
  fprintf(out, "    Force volume:    %d\n", mikmod->force_volume);
  fprintf(out, "    Use interpolate: %d\n", mikmod->use_interpolate);
  }
#endif

static void * create_mikmod()
  {
  i_mikmod_t * ret = calloc(1, sizeof(*ret));
  return ret;
  }

  
/* Read one audio frame (returns FALSE on EOF) */
static gavl_source_status_t
read_func_mikmod(void * data,
                 gavl_audio_frame_t ** fp)
  {
  int result;
  i_mikmod_t * e = data;
  gavl_audio_frame_t * f = *fp;

  int num_samples = FRAME_SAMPLES;
  
  result = bg_subprocess_read_data(e->proc->stdout_fd,
                                   f->samples.u_8,
                                   num_samples * e->block_align);
  
  if(result < 0)
    return GAVL_SOURCE_EOF;
  
  if(result < num_samples * e->block_align)
    e->eof = 1;
  
  f->valid_samples = result / e->block_align;
  return f->valid_samples ? GAVL_SOURCE_OK : GAVL_SOURCE_EOF;
  }
  
// arg = path of mod
static int open_mikmod(void * data, const char * arg)
  {
  int result;
  char *command;
  i_mikmod_t * mik = data;
  gavl_audio_frame_t * test_frame;
  gavl_audio_format_t * fmt;
  
  // if no mikmod installed 
  if(!bg_search_file_exec("mikmod", NULL))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Cannot find mikmod executable");
    return 0;
    }
  
  /* Create track infos */
  gavl_track_set_num_audio_streams(mik->track_info, 1);
  
  fmt = gavl_track_get_audio_format_nc(mik->track_info, 0);
  
  fmt->samplerate = mik->frequency;
  
  if(mik->output == MONO8 || mik->output == MONO16)
    fmt->num_channels = 1;
  else if(mik->output == STEREO8 || mik->output == STEREO16)
    fmt->num_channels = 2;

  gavl_set_channel_setup(fmt);
  
  if(mik->output == MONO8 || mik->output == STEREO8)
    fmt->sample_format = GAVL_SAMPLE_U8;
  else if(mik->output == MONO16 || mik->output == STEREO16)
    fmt->sample_format = GAVL_SAMPLE_S16;

  fmt->interleave_mode = GAVL_INTERLEAVE_ALL;
  fmt->samples_per_frame = FRAME_SAMPLES;

  gavl_dictionary_set_string(gavl_track_get_audio_metadata_nc(mik->track_info, 0),
                             GAVL_META_FORMAT, "mikmod audio");
  
  gavl_set_channel_setup(fmt);

  command = bg_sprintf("mikmod -q --playmode 0 --noloops --exitafter -f %d -d stdout", mik->frequency);
    
  if(mik->output == MONO8)
    {
    command = gavl_strcat(command, " -o 8m");
    mik->block_align = 1;
    }
  else if(mik->output == MONO16)
    {
    command = gavl_strcat(command, " -o 16m");
    mik->block_align = 2;
    }
  else if(mik->output == STEREO8)
    {
    command = gavl_strcat(command, " -o 8s");
    mik->block_align = 2;
    }
  else if(mik->output == STEREO16)
    {
    command = gavl_strcat(command, " -o 16s");
    mik->block_align = 4;
    }
  if(mik->use_surround)
    command = gavl_strcat(command, " -s");
  if(mik->use_interpolate)
    command = gavl_strcat(command, " -i");
  if(mik->force_volume)
    command = gavl_strcat(command, " -fa");
  if(mik->hidden_patterns)
    command = gavl_strcat(command, " -c");

  command = gavl_strcat(command, " ");
  command = gavl_strcat(command, arg);
  
  /* Test file compatibility */
  mik->proc = bg_subprocess_create(command, 0, 1, 0);
  test_frame = gavl_audio_frame_create(fmt);

  result = read_func_mikmod(mik, &test_frame);
  
  if(result == GAVL_SOURCE_OK)
    {
    bg_subprocess_kill(mik->proc, SIGKILL);
    bg_subprocess_close(mik->proc);
    mik->proc = bg_subprocess_create(command, 0, 1, 0);
    }
  else
    {
    bg_subprocess_close(mik->proc);
    mik->proc = NULL;
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Unrecognized fileformat");
    }

  gavl_audio_frame_destroy(test_frame);
  
  
  
  mik->src = gavl_audio_source_create(read_func_mikmod,
                                      mik, 0, fmt);
  
  free(command);
#ifdef dump
  dump_mikmod(data, (const char*)arg);
#endif
  return result;
  }

static gavl_dictionary_t * get_media_info_mikmod(void * data)
  {
  i_mikmod_t * e = data;
  return &e->mi;
  }

static bg_media_source_t *
get_src_mikmod(void * p)
  {
  i_mikmod_t * priv = p;
  return &priv->ms;
  }

static void close_mikmod(void * data)
  {
  i_mikmod_t * e = data;
  if(e->proc)
    {
    if(!e->eof)
      bg_subprocess_kill(e->proc, SIGKILL);
    bg_subprocess_close(e->proc);
    e->proc = NULL;
    }
  if(e->src)
    gavl_audio_source_destroy(e->src);

  gavl_dictionary_reset(&e->mi);
  e->track_info = NULL;
  }

static void destroy_mikmod(void * data)
  {
  i_mikmod_t * e = data;
  close_mikmod(data);
  free(e);
  }

/* Configuration */

static const bg_parameter_info_t parameters[] = 
  {
    {
      .name =        "output",
      .long_name =   TRS("Output format"),
      .type =        BG_PARAMETER_STRINGLIST,
      .multi_names = (char const *[]){ "mono8",
                                       "stereo8",
                                       "mono16",
                                       "stereo16",
                                       NULL },
      .multi_labels =  (char const *[]){ TRS("Mono 8bit"),
                                         TRS("Stereo 8bit"),
                                         TRS("Mono 16bit"),
                                         TRS("Stereo 16bit"), NULL },
      
      .val_default = GAVL_VALUE_INIT_STRING("stereo16"),
    },
      {
      .name =        "mixing_frequency",
      .long_name =   TRS("Samplerate"),
      .type =         BG_PARAMETER_INT,
      .val_min =     GAVL_VALUE_INIT_INT(4000),
      .val_max =     GAVL_VALUE_INIT_INT(60000),
      .val_default = GAVL_VALUE_INIT_INT(44100)      
      // .help_string = "Mixing frequency for the Track"
    },
    {
      .name = "look_for_hidden_patterns_in_module",
      .long_name = TRS("Look for hidden patterns in module"),
      .opt =  "hidden",
      .type = BG_PARAMETER_CHECKBUTTON,
    },
    {
      .name = "use_surround_mixing",
      .long_name = TRS("Use surround mixing"),
      .opt =  "sur",
      .type = BG_PARAMETER_CHECKBUTTON,
    },
    {
      .name = "force_volume_fade_at_the_end_of_module",
      .long_name = TRS("Force volume fade at the end of module"),
      .opt =  "fade",
      .type = BG_PARAMETER_CHECKBUTTON,
    },
    {
      .name = "use_interpolate_mixing",
      .long_name = TRS("Use interpolate mixing"),
      .opt =  "interpol",
      .type = BG_PARAMETER_CHECKBUTTON,
    },
    { /* End of parameters */ }
  };

static const bg_parameter_info_t * get_parameters_mikmod(void * data)
  {
  return parameters;
  }

static void set_parameter_mikmod(void * data, const char * name,
                                 const gavl_value_t * val)
  {
  i_mikmod_t * mikmod;
  mikmod = data;
  if(!name)
    return;
  else if(!strcmp(name, "output"))
    {
    if(!strcmp(val->v.str, "mono8"))
      mikmod->output = MONO8;
    else if(!strcmp(val->v.str, "mono16"))
      mikmod->output = MONO16;
    else if(!strcmp(val->v.str, "stereo8"))
      mikmod->output = STEREO8;
    else if(!strcmp(val->v.str, "stereo16"))
      mikmod->output = STEREO16;
    }
  else if(!strcmp(name, "mixing_frequency"))
    mikmod->frequency = val->v.i;
  else if(!strcmp(name, "look_for_hidden_patterns_in_module"))
    mikmod->hidden_patterns = val->v.i;
  else if(!strcmp(name, "force_volume_fade_at_the_end_of_module"))
    mikmod->force_volume = val->v.i;
  else if(!strcmp(name, "use_interpolate_mixing"))
    mikmod->use_interpolate = val->v.i;
  else if(!strcmp(name, "use_surround_mixing"))
    mikmod->use_surround = val->v.i;
  }

static char const * const extensions =
  "it xm mod mtm  s3m stm ult far med dsm amf imf 669";

static const char * get_extensions(void * priv)
  {
  return extensions;
  }

const bg_input_plugin_t the_plugin =
  {
    .common =
    {
      BG_LOCALE,
      .name =            "i_mikmod",       /* Unique short name */
      .long_name =       TRS("mikmod input plugin"),
      .description =     TRS("Simple wrapper, which calls the mikmod program"),
      .type =            BG_PLUGIN_INPUT,
      .flags =           BG_PLUGIN_FILE,
      .priority =        1,
      .create =          create_mikmod,
      .destroy =         destroy_mikmod,
      .get_parameters =  get_parameters_mikmod,
      .set_parameter =   set_parameter_mikmod,
    },
    .get_extensions =    get_extensions,
    .open =              open_mikmod,
    .get_media_info =    get_media_info_mikmod,
    .get_audio_source =  get_audio_source_mikmod,
    
    .close =              close_mikmod
    
  };

/* Include this into all plugin modules exactly once
   to let the plugin loader obtain the API version */
BG_GET_PLUGIN_API_VERSION;
