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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <config.h>

#include <gavl/gavl.h>
#include <gavl/metadata.h>
#include <gavl/utils.h>

#include <gmerlin/translation.h>
#include <gmerlin/application.h>


#include <gmerlin/parameter.h>
#include <gmerlin/cfg_registry.h>
#include <gmerlin/cfgctx.h>
#include <gmerlin/cmdline.h>

#include <gmerlin/upnp/ssdp.h>

static gavl_time_t timeout = 5 * GAVL_TIME_SCALE;

static char * type = NULL;
static int version = -1;

static void opt_t(void * data, int * argc, char *** _argv, int arg)
  {
  float to;
  if(arg >= *argc)
    {
    fprintf(stderr, "Option -t requires an argument\n");
    exit(-1);
    }

  to = strtod((*_argv)[arg], NULL);
  timeout = gavl_seconds_to_time(to);
  
  bg_cmdline_remove_arg(argc, _argv, arg);
  }

static void opt_type(void * data, int * argc, char *** _argv, int arg)
  {
  if(arg >= *argc)
    {
    fprintf(stderr, "Option -type requires an argument\n");
    exit(-1);
    }
  type = (*_argv)[arg];
  bg_cmdline_remove_arg(argc, _argv, arg);
  }

static void opt_version(void * data, int * argc, char *** _argv, int arg)
  {
  if(arg >= *argc)
    {
    fprintf(stderr, "Option -vers requires an argument\n");
    exit(-1);
    }
  version = atoi((*_argv)[arg]);
  bg_cmdline_remove_arg(argc, _argv, arg);
  }

static bg_cmdline_arg_t global_options[] =
  {
    {
      .arg =         "-t",
      .help_arg =    "<timeout>",
      .help_string = "Set timeout in seconds",
      .callback =    opt_t,
    },
    {
      .arg =         "-type",
      .help_arg =    "<type>",
      .help_string = "Specify the Upnp device type (e.g. MediaRenderer)",
      .callback =    opt_type,
    },
    {
      .arg =         "-vers",
      .help_arg =    "<version>",
      .help_string = "Specify the Upnp device version",
      .callback =    opt_version,
    },
    {
      /* End */
    }
  };

const bg_cmdline_app_data_t app_data =
  {
    .package =  PACKAGE,
    .version =  VERSION,
    .synopsis = TRS("[options]\n"),
    .help_before = TRS("gavf info\n"),
    .args = (bg_cmdline_arg_array_t[]) { { TRS("Options"), global_options },
                                       {  } },
  };

static int handle_msg(void * priv, gavl_msg_t * msg)
  {
  switch(msg->NS)
    {
    case BG_MSG_NS_SSDP:
      {
      switch(msg->ID)
        {
        case BG_SSDP_MSG_ADD_DEVICE:
          {
          const char * type = NULL;
          const char * protocol = NULL;
          int version = 0;
          const char * desc_url = NULL;
          bg_ssdp_msg_get_add(msg, &protocol, &type, &version, &desc_url);
          fprintf(stderr, "%s.%d [%s]\n", type, version, desc_url);
          }
          break;
        case BG_SSDP_MSG_DEL_DEVICE:
          {
          
          }
          break;
        }
      }
      break;
    }
  
  return 1;
  }

int main(int argc, char ** argv)
  {
  bg_msg_sink_t * sink;
  gavl_timer_t * timer = gavl_timer_create();
  gavl_time_t delay_time = GAVL_TIME_SCALE / 100;
  bg_ssdp_t * s = bg_ssdp_create(NULL);

  bg_app_init("gmerlin-upnp-discover", TRS("Discover Upnp devices in the local network"));

  /* Handle commandline options */
  bg_cmdline_init(&app_data);
  bg_cmdline_parse(global_options, &argc, &argv, NULL);
    
  gavl_timer_start(timer);

  while(1)
    {
    if(gavl_timer_get(timer) >= timeout)
      break;
    
    bg_ssdp_update(s);
    gavl_time_delay(&delay_time);
    }

  sink = bg_msg_sink_create(handle_msg, NULL, 1);
  bg_ssdp_browse(s, sink);

  bg_msg_sink_destroy(sink);
  bg_ssdp_destroy(s);
  
  gavl_timer_destroy(timer);
  return 0;
  }
