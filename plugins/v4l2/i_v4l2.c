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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gavl/hw_v4l2.h>

#include <config.h>
#include <gmerlin/translation.h>
#include <gmerlin/plugin.h>
#include <gmerlin/utils.h>
#include <gmerlin/frametimer.h>

#include "v4l2_common.h"

#include <gmerlin/log.h>
#define LOG_DOMAIN "i_v4l2"

#ifdef HAVE_V4LCONVERT
#include "convert.h"
#endif

#include <gavl/metatags.h>

/* Input module */

typedef struct
  {
  gavl_hw_context_t * hwctx;
  gavl_v4l2_device_t * dev;
  
  gavl_dictionary_t mi;  // Media info
  gavl_dictionary_t * s; // Stream

  bg_media_source_t src;
  bg_controllable_t ctrl;
  } v4l2_t;

static void close_v4l(void * priv)
  {
  v4l2_t * v4l = priv;
  
  if(v4l->hwctx)
    gavl_hw_ctx_destroy(v4l->hwctx);

  gavl_dictionary_free(&v4l->mi);
  bg_media_source_cleanup(&v4l->src);
  
  free(v4l);
  }

static int handle_cmd(void * data, gavl_msg_t * msg)
  {
  v4l2_t * v4l = data;
  
  switch(msg->NS)
    {
    case GAVL_MSG_NS_SRC:
      switch(msg->ID)
        {
        case GAVL_CMD_SRC_START:
          break;
        case GAVL_CMD_SRC_PAUSE:
          break;
        case GAVL_CMD_SRC_RESUME:
          break;
        }
      break;
    }
  return 1;
  }

static void * create_v4l()
  {
  v4l2_t * v4l;

  v4l = calloc(1, sizeof(*v4l));

  bg_controllable_init(&v4l->ctrl,
                       bg_msg_sink_create(handle_cmd, v4l, 1),
                       bg_msg_hub_create(1));
  
  return v4l;
  }

static void  destroy_v4l(void * priv)
  {
  v4l2_t * v4l;
  v4l = priv;
  close_v4l(priv);
  
  free(v4l);
  }


/* Configuration stuff */

static const bg_parameter_info_t parameters[] =
  {
    {
      .name =        "format",
      .long_name =   TRS("Format"),
      .type =        BG_PARAMETER_SECTION,
    },
    {
      .name =        "width",
      .long_name =   TRS("Width"),
      .type =        BG_PARAMETER_INT,
      .val_default = GAVL_VALUE_INIT_INT(640),
      .val_min =     GAVL_VALUE_INIT_INT(160),
      .val_max =     GAVL_VALUE_INIT_INT(1920),
    },
    {
      .name =        "height",
      .long_name =   TRS("User defined height"),
      .type =        BG_PARAMETER_INT,
      .val_default = GAVL_VALUE_INIT_INT(480),
      .val_min =     GAVL_VALUE_INIT_INT(112),
      .val_max =     GAVL_VALUE_INIT_INT(1080),
    },
    { /* End of parameters */ }
  };


static const bg_parameter_info_t * get_parameters_v4l(void * priv)
  {
  return NULL;
  }

static int get_parameter_v4l(void * priv, const char * name,
                             gavl_value_t * val)
  {
  return 0;
  }

static void set_parameter_v4l(void * priv, const char * name,
                              const gavl_value_t * val)
  {
  //  v4l2_t * v4l;
  //  v4l = priv;
  }

static int open_v4l(void * priv, const char * location)
  {
  int ret = 0;
  
  char * protocol = NULL;
  char * path     = NULL;
  char * hostname = NULL;
  gavl_dictionary_t dev;

  gavl_dictionary_t * t;
  
  v4l2_t * v4l;
  v4l = priv;
  
  gavl_dictionary_init(&dev);
  
  if(!gavl_url_split(location,
                     &protocol,
                     NULL,
                     NULL,
                     &hostname,
                     NULL,
                     &path))
    {
    goto fail;
    }

  if(strcmp(protocol, "v4l2-capture"))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Invalid protocol");
    goto fail;
    }
    
  if(!gavl_v4l2_get_device_info(path, &dev))
    goto fail;

  if((v4l->hwctx = gavl_hw_ctx_create_v4l2(&dev)))
    goto fail;

  t = gavl_append_track(&v4l->mi, NULL);
  v4l->s = gavl_track_add_video_stream(t);

  fprintf(stderr, "Open v4l\n");
  
  ret = 1;
  fail:
  gavl_dictionary_free(&dev);

  if(hostname)
    free(hostname);
  if(path)
    free(path);
  if(protocol)
    free(protocol);
  
    
  return ret;
  }

static char const * const protocols = "v4l4-capture";

static const char * get_protocols_v4l(void * priv)
  {
  return protocols;
  }

static gavl_dictionary_t * get_media_info_v4l(void * priv)
  {
  v4l2_t * v4l;
  v4l = priv;
  return &v4l->mi;
  }

static bg_media_source_t * get_src_v4l(void * priv)
  {
  v4l2_t * v4l;
  v4l = priv;

  return &v4l->src;
  
  }



const bg_input_plugin_t the_plugin =
  {
    .common =
    {
      BG_LOCALE,
      .name =          "i_v4l2",
      .long_name =     TRS("V4L2"),
      .description =   TRS("video4linux 2 recording plugin. Supports only video and no tuner decives."),
      .type =          BG_PLUGIN_RECORDER_VIDEO,
      .flags =         BG_PLUGIN_DEVPARAM,
      .priority =      BG_PLUGIN_PRIORITY_MAX,
      .create =        create_v4l,
      .destroy =       destroy_v4l,

      .get_parameters = get_parameters_v4l,
      .set_parameter =  set_parameter_v4l,
      .get_parameter =  get_parameter_v4l,
    },
    
    .get_protocols = get_protocols_v4l,
    .open          = open_v4l,
    .close         = close_v4l,
  };

/* Include this into all plugin modules exactly once
   to let the plugin loader obtain the API version */
BG_GET_PLUGIN_API_VERSION;
