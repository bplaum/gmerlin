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

#include <signal.h>

#include <config.h>
#include <gmerlin/recorder.h>
#include <gmerlin/pluginregistry.h>
#include <gavl/gavl.h>
#include <gmerlin/utils.h>
#include <gmerlin/cfgctx.h>
#include <gmerlin/cmdline.h>
#include <gmerlin/log.h>
#include <gmerlin/translation.h>
#include <gmerlin/application.h>

static bg_recorder_t * recorder;


static int do_record = 0;

bg_cfg_ctx_t * rec_cfg = NULL;

#define DELAY_TIME 50
#define PING_INTERVAL 20

int got_sigint = 0;

static void sigint_handler(int sig)
  {
  fprintf(stderr, "\nCTRL+C caught\n");
  got_sigint = 1;
  }

static void set_sigint_handler()
  {
  struct sigaction sa;
  sa.sa_flags = 0;
  sigemptyset(&sa.sa_mask);
  sa.sa_handler = sigint_handler;
  if (sigaction(SIGINT, &sa, NULL) == -1)
    {
    fprintf(stderr, "sigaction failed\n");
    }
  }

static void opt_aud(void * data, int * argc, char *** _argv, int arg)
  {
  if(arg >= *argc)
    {
    fprintf(stderr, "Option -aud requires an argument\n");
    exit(-1);
    }
  if(!bg_cmdline_apply_options(rec_cfg[BG_RECORDER_CFG_AUDIO].s,
                               rec_cfg[BG_RECORDER_CFG_AUDIO].set_param,
                               recorder,
                               rec_cfg[BG_RECORDER_CFG_AUDIO].p,
                               (*_argv)[arg]))
    exit(-1);
  bg_cmdline_remove_arg(argc, _argv, arg);
  }


static void opt_vid(void * data, int * argc, char *** _argv, int arg)
  {
  if(arg >= *argc)
    {
    fprintf(stderr, "Option -vid requires an argument\n");
    exit(-1);
    }
  if(!bg_cmdline_apply_options(rec_cfg[BG_RECORDER_CFG_VIDEO].s,
                               rec_cfg[BG_RECORDER_CFG_VIDEO].set_param,
                               recorder,
                               rec_cfg[BG_RECORDER_CFG_VIDEO].p,
                               (*_argv)[arg]))
    exit(-1);
  bg_cmdline_remove_arg(argc, _argv, arg);
  }


static void opt_vm(void * data, int * argc, char *** _argv, int arg)
  {
  if(arg >= *argc)
    {
    fprintf(stderr, "Option -vm requires an argument\n");
    exit(-1);
    }
  if(!bg_cmdline_apply_options(rec_cfg[BG_RECORDER_CFG_MONITOR].s,
                               rec_cfg[BG_RECORDER_CFG_MONITOR].set_param,
                               recorder,
                               rec_cfg[BG_RECORDER_CFG_MONITOR].p,
                               (*_argv)[arg]))
    exit(-1);
  bg_cmdline_remove_arg(argc, _argv, arg);
  }

static void opt_r(void * data, int * argc, char *** _argv, int arg)
  {
  do_record = 1;
  }

static void opt_enc(void * data, int * argc, char *** _argv, int arg)
  {
  if(arg >= *argc)
    {
    fprintf(stderr, "Option -enc requires an argument\n");
    exit(-1);
    }
  if(!bg_cmdline_apply_options(rec_cfg[BG_RECORDER_CFG_ENCODERS].s,
                               rec_cfg[BG_RECORDER_CFG_ENCODERS].set_param,
                               recorder,
                               rec_cfg[BG_RECORDER_CFG_ENCODERS].p,
                               (*_argv)[arg]))
    exit(-1);
  bg_cmdline_remove_arg(argc, _argv, arg);
  }

static void opt_o(void * data, int * argc, char *** _argv, int arg)
  {
  if(arg >= *argc)
    {
    fprintf(stderr, "Option -o requires an argument\n");
    exit(-1);
    }

  if(!bg_cmdline_apply_options(rec_cfg[BG_RECORDER_CFG_OUTPUT].s,
                               rec_cfg[BG_RECORDER_CFG_OUTPUT].set_param,
                               recorder,
                               rec_cfg[BG_RECORDER_CFG_OUTPUT].p,
                               (*_argv)[arg]))
    exit(-1);
  bg_cmdline_remove_arg(argc, _argv, arg);
  }

static void opt_syslog(void * data, int * argc, char *** _argv, int arg)
  {
  if(arg >= *argc)
    {
    fprintf(stderr, "Option -syslog requires an argument\n");
    exit(-1);
    }
  bg_log_syslog_init((*_argv)[arg]);
  bg_cmdline_remove_arg(argc, _argv, arg);
  }

static bg_cmdline_arg_t global_options[] =
  {
    {
      .arg =         "-aud",
      .help_arg =    "<audio_options>",
      .help_string = "Set audio recording options",
      .callback =    opt_aud,
    },
    {
      .arg =         "-vid",
      .help_arg =    "<video_options>",
      .help_string = "Set video recording options",
      .callback =    opt_vid,
    },
    BG_PLUGIN_OPT_FA,
    BG_PLUGIN_OPT_FV,
    {
      .arg =         "-vm",
      .help_arg =    "<video monitor options>",
      .help_string = "Set video monitor options",
      .callback =    opt_vm,
    },
    {
      .arg =         "-enc",
      .help_arg =    "<encoding_options>",
      .help_string = "Set encoding options",
      .callback =    opt_enc,
      
    },
    {
      .arg =         "-o",
      .help_arg =    "<output_options>",
      .help_string = "Set output options",
      .callback =    opt_o,
      
    },
    {
      .arg =         "-r",
      .help_string = "Record to file",
      .callback =    opt_r,
    },
    {
      .arg =         "-syslog",
      .help_arg =    "<name>",
      .help_string = "Set log messages to syslog",
      .callback =    opt_syslog,
    },
    BG_PLUGIN_OPT_LIST_FA,
    BG_PLUGIN_OPT_LIST_FV,
    BG_PLUGIN_OPT_LIST_OPTIONS,
    {
      /* End */
    }
  };

static void update_global_options()
  {
  global_options[0].parameters = rec_cfg[BG_RECORDER_CFG_AUDIO].p;
  global_options[1].parameters = rec_cfg[BG_RECORDER_CFG_VIDEO].p;
  global_options[2].parameters = rec_cfg[BG_RECORDER_CFG_AUDIOFILTER].p;
  global_options[3].parameters = rec_cfg[BG_RECORDER_CFG_VIDEOFILTER].p;
  global_options[4].parameters = rec_cfg[BG_RECORDER_CFG_MONITOR].p;
  global_options[5].parameters = rec_cfg[BG_RECORDER_CFG_ENCODERS].p;
  global_options[6].parameters = rec_cfg[BG_RECORDER_CFG_OUTPUT].p;
  }

const bg_cmdline_app_data_t app_data =
  {
    .package =  PACKAGE,
    .version =  VERSION,
    .synopsis = TRS("[options]\n"),
    .help_before = TRS("Commandline recorder\n"),
    .args = (bg_cmdline_arg_array_t[]) { { TRS("Options"), global_options },
                                       {  } },
    .files = (bg_cmdline_ext_doc_t[])
    { { "~/.gmerlin/plugins.xml",
        TRS("Cache of the plugin registry (shared by all applications)") },
      { "~/.gmerlin/generic/cfg.xml",
        TRS("Default plugin parameters are read from there. Use gmerlin_plugincfg to change them.") },
      { /* End */ }
    },
    
  };


  
int main(int argc, char ** argv)
  {
  int timeout_counter = 0;

  gavl_time_t delay_time = DELAY_TIME * 1000;

  bg_app_init("gmerlin-record", TRS("Gmerlin commandline recorder"));
  
  set_sigint_handler();
  
  /* We must initialize the random number generator if we want the
     Vorbis encoder to work */
  srand(time(NULL));
  
  /* Create plugin regsitry */
  bg_plugins_init();

  recorder = bg_recorder_create(bg_plugin_reg);
  rec_cfg = bg_recorder_get_cfg(recorder);
  
  /* Create config sections */

  bg_cfg_ctx_array_create_sections(rec_cfg, NULL);

  update_global_options();

  /* Apply default options */
  bg_cfg_ctx_apply_array(rec_cfg);
  
  /* Get commandline options */
  bg_cmdline_init(&app_data);
  bg_cmdline_parse(global_options, &argc, &argv, NULL);
  
  bg_recorder_record(recorder, do_record);
  bg_recorder_run(recorder);
  
  while(1)
    {
    if(!timeout_counter)
      {
      if(!bg_recorder_ping(recorder))
        break;
      }

    if(got_sigint)
      break;
    
    timeout_counter++;
    if(timeout_counter >= PING_INTERVAL)
      timeout_counter = 0;
    
    gavl_time_delay(&delay_time);
    }
  
  bg_recorder_destroy(recorder);
  return 0;
  }
