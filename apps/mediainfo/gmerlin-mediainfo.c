#include <config.h>


#include <gmerlin/pluginregistry.h>
#include <gmerlin/cfgctx.h>

#include <gmerlin/cmdline.h>
#include <gmerlin/translation.h>
#include <gmerlin/utils.h>
#include <gmerlin/application.h>


static int input_flags = 0;

static void opt_edl(void * data, int * argc, char *** _argv, int arg)
  {
  input_flags |= BG_INPUT_FLAG_PREFER_EDL;
  }

static void opt_fmt(void * data, int * argc, char *** _argv, int arg)
  {
  input_flags |= BG_INPUT_FLAG_GET_FORMAT;
  }

static void opt_track(void * data, int * argc, char *** _argv, int arg)
  {
  input_flags |= BG_INPUT_FLAG_SELECT_TRACK;
  }

static bg_cmdline_arg_t global_options[] =
  {
    {
      .arg =         "-edl",
      .help_string = "Prefer edl",
      .callback =    opt_edl,
    },
    {
      .arg =         "-fmt",
      .help_string = "Start codecs to get complete formats",
      .callback =    opt_fmt,
    },
    {
      .arg =         "-track",
      .help_string = "Select and return single track (passed in the URL)",
      .callback =    opt_track,
    },
    {
      /* End */
    }
  };

const bg_cmdline_app_data_t app_data =
  {
    .package =  PACKAGE,
    .version =  VERSION,
    .synopsis = TRS("[options] <gml>\n"),
    .help_before = TRS("gmerlin mediainfo\n"),
    .args = (bg_cmdline_arg_array_t[]) { { TRS("Options"), global_options },
                                       {  } },
  };


int main(int argc, char ** argv)
  {
  gavl_dictionary_t * mi;
  
  /* Handle commandline options */
  bg_app_init("gmerlin-mediainfo", TRS("Print media information"), NULL);
  
  bg_cmdline_init(&app_data);
  bg_cmdline_parse(global_options, &argc, &argv, NULL);
  
  /* Create registries */

  bg_plugins_init();
  
  /* Load url */
  if((mi = bg_plugin_registry_load_media_info(bg_plugin_reg, argv[1], input_flags)))
    {
    gavl_dictionary_dump(mi, 0);
    gavl_dictionary_destroy(mi);
    }

  bg_plugins_cleanup();
  bg_cfg_registry_cleanup();
  
  }
