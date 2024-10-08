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



#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <config.h>
#include <gmerlin/translation.h>
#include <gmerlin/plugin.h>
#include <gmerlin/utils.h>
#include <gmerlin/log.h>

#define LOG_DOMAIN "fa_samplerate"

#define RATE_SOURCE -1
#define RATE_USER   -2

typedef struct
  {
  int samplerate_cfg;
  int samplerate_user;
  int samplerate_current;
  
  int need_restart;

  gavl_audio_format_t format;

  gavl_audio_source_t * in_src;
  gavl_audio_source_t * out_src;

  } samplerate_priv_t;

static void * create_samplerate()
  {
  samplerate_priv_t * ret;
  ret = calloc(1, sizeof(*ret));
  return ret;
  }

static void destroy_samplerate(void * priv)
  {
  samplerate_priv_t * vp;
  vp = priv;
  if(vp->out_src)
    gavl_audio_source_destroy(vp->out_src);
  free(vp);
  }

static const bg_parameter_info_t parameters[] =
  {
    {
      .gettext_domain = PACKAGE,
      .gettext_directory = LOCALE_DIR,
      .name = "samplerate",
      .long_name = TRS("Samplerate [Hz]"),
      .type = BG_PARAMETER_STRINGLIST,
      .flags = BG_PARAMETER_SYNC,
      .val_default = GAVL_VALUE_INIT_STRING("source"),
      .multi_names = (const char*[]){ "source",
                                "48000",
                                "44100",
                                "32000",
                                "24000",
                                "22050",
                                "user",
                                NULL },
      .multi_labels = (const char*[]){ TRS("From source"),
                                 "48000",
                                 "44100",
                                 "32000",
                                 "24000",
                                 "22050",
                                 TRS("User defined"),
                                 NULL },
    },
    {
      .name = "user_rate",
      .long_name = TRS("User defined samplerate [Hz]"),
      .type = BG_PARAMETER_INT,
      .flags = BG_PARAMETER_SYNC,
      .val_min     = GAVL_VALUE_INIT_INT(8000),
      .val_max     = GAVL_VALUE_INIT_INT(192000),
      .val_default = GAVL_VALUE_INIT_INT(44100),
    },
    { /* End of parameters */ },
  };

static const bg_parameter_info_t * get_parameters_samplerate(void * priv)
  {
  return parameters;
  }

static int get_samplerate(samplerate_priv_t * vp)
  {
  if(vp->samplerate_cfg == RATE_SOURCE)
    {
    const gavl_audio_format_t * fmt;
    if(vp->in_src)
      {
      fmt = gavl_audio_source_get_src_format(vp->in_src);
      return fmt->samplerate;
      }
    else
      return 0;
    }
  else if(vp->samplerate_cfg == RATE_USER)
    return vp->samplerate_user;
  else
    return vp->samplerate_cfg;
  }

static void
set_parameter_samplerate(void * priv, const char * name,
                          const gavl_value_t * val)
  {
  samplerate_priv_t * vp;
  vp = priv;
  
  if(!name)
    {
    int current_rate = get_samplerate(vp);
    if(!current_rate || (current_rate != vp->samplerate_current))
      vp->need_restart = 1;
    return;
    }
  if(!strcmp(name, "samplerate"))
    {
    if(!strcmp(val->v.str, "source"))
      vp->samplerate_cfg = RATE_SOURCE;
    else if(!strcmp(val->v.str, "user"))
      vp->samplerate_cfg = RATE_USER;
    else
      vp->samplerate_cfg = atoi(val->v.str);
    }
  else if(!strcmp(name, "user_rate"))
    vp->samplerate_user = val->v.i;
  }

static int need_restart_samplerate(void * priv)
  {
  samplerate_priv_t * vp = priv;
  return vp->need_restart;
  }

static gavl_source_status_t read_func(void * priv,
                                      gavl_audio_frame_t ** frame)
  {
  samplerate_priv_t * vp = priv;
  return gavl_audio_source_read_frame(vp->in_src, frame);
  }

static gavl_audio_source_t *
connect_samplerate(void * priv,
                     gavl_audio_source_t * src,
                     const gavl_audio_options_t * opt)
  {
  gavl_audio_format_t format;
  int old_rate;
  samplerate_priv_t * vp = priv;
  
  vp->in_src = src;
  if(vp->out_src)
    gavl_audio_source_destroy(vp->out_src);

  vp->samplerate_current = get_samplerate(vp);
  
  gavl_audio_format_copy(&format, gavl_audio_source_get_src_format(vp->in_src));
  old_rate = format.samplerate;
  format.samplerate = vp->samplerate_current;

  if(format.samplerate != old_rate)
    {
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Resampling %f kHz to %f kHz",
             (float)old_rate / 1000.0, (float)format.samplerate / 1000.0);
    if(format.sample_format != GAVL_SAMPLE_DOUBLE)
      format.sample_format = GAVL_SAMPLE_FLOAT;
    }
  gavl_audio_source_set_dst(vp->in_src, 0, &format);
  vp->out_src = gavl_audio_source_create_source(read_func, vp, 0, vp->in_src);
  return vp->out_src;
  }

const bg_fa_plugin_t the_plugin = 
  {
    .common =
    {
      BG_LOCALE,
      .name =      "fa_samplerate",
      .long_name = TRS("Force samplerate"),
      .description = TRS("This forces a samplerate as input for the next filter."),
      .type =     BG_PLUGIN_FILTER_AUDIO,
      .flags =    BG_PLUGIN_FILTER_1,
      .create =   create_samplerate,
      .destroy =   destroy_samplerate,
      .get_parameters =   get_parameters_samplerate,
      .set_parameter =    set_parameter_samplerate,
      .priority =         1,
    },
    
    .connect = connect_samplerate,
    .need_restart = need_restart_samplerate,
    
  };

/* Include this into all plugin modules exactly once
   to let the plugin loader obtain the API version */
BG_GET_PLUGIN_API_VERSION;
