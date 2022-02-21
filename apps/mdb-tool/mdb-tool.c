#include <string.h>


#include <config.h>

#include <gmerlin/mdb.h>

#include <gmerlin/cfg_registry.h>
#include <gmerlin/parameter.h>
#include <gmerlin/cfgctx.h>

#include <gmerlin/utils.h>
#include <gmerlin/cmdline.h>
#include <gmerlin/translation.h>
#include <gmerlin/application.h>

static int do_create = 0;
static int do_monitor = 0;

char * path = NULL;

bg_mdb_t * mdb = NULL;
bg_controllable_t * mdb_ctrl;
bg_control_t ctrl;

static void opt_create(void * data, int * argc, char *** _argv, int arg)
  {
  do_create = 1;
  }

static void opt_monitor(void * data, int * argc, char *** _argv, int arg)
  {
  do_monitor = 1;
  }

static void opt_db(void * data, int * argc, char *** _argv, int arg)
  {
  if(arg >= *argc)
    {
    fprintf(stderr, "Option -db requires an argument\n");
    exit(-1);
    }
  path = gavl_strdup((*_argv)[arg]);
  bg_cmdline_remove_arg(argc, _argv, arg);
  }

static bg_cmdline_arg_t global_options[] =
  {
    {
      .arg =         "-create",
      .help_string = "Create DB if it doesn't exist",
      .callback =    opt_create,
    },
    {
      .arg =         "-monitor",
      .help_string = "Monitor the db until Ctrl+C is pressed",
      .callback =    opt_monitor,
    },
    {
      .arg =         "-db",
      .help_arg =    "<path>",
      .help_string = "DB path",
      .callback =    opt_db,
    },
    {
      /* End */
    }
  };

static void opt_rescan(void * data, int * argc, char *** _argv, int arg)
  {
  bg_mdb_rescan_sync(mdb);
  }


static void browse_obj(void * data, int * argc, char *** _argv, int arg)
  {
  gavl_msg_t * msg;
  
  if(arg >= *argc)
    {
    fprintf(stderr, "Option -browse-obj requires an argument\n");
    exit(-1);
    }

  msg = bg_msg_sink_get(ctrl.cmd_sink);
  gavl_msg_set_id_ns(msg, BG_FUNC_DB_BROWSE_OBJECT, BG_MSG_NS_DB);
  gavl_dictionary_set_string(&msg->header, GAVL_MSG_CONTEXT_ID, (*_argv)[arg]);
  bg_msg_sink_put(ctrl.cmd_sink, msg);
  
  bg_cmdline_remove_arg(argc, _argv, arg);
  }

static void browse_children(void * data, int * argc, char *** _argv, int arg)
  {
  gavl_msg_t * msg;

  if(arg >= *argc)
    {
    fprintf(stderr, "Option -browse-children requires an argument\n");
    exit(-1);
    }

  msg = bg_msg_sink_get(ctrl.cmd_sink);
  gavl_msg_set_id_ns(msg, BG_FUNC_DB_BROWSE_CHILDREN, BG_MSG_NS_DB);
  gavl_dictionary_set_string(&msg->header, GAVL_MSG_CONTEXT_ID, (*_argv)[arg]);
  bg_msg_sink_put(ctrl.cmd_sink, msg);
  
  bg_cmdline_remove_arg(argc, _argv, arg);
  }

static bg_cmdline_arg_t commands[] =
  {
    {
      .arg =         "-rescan",
      .help_string = "Rescan directories.",
      .callback =    opt_rescan,
    },
    {
      .arg =         "-browse-obj",
      .help_arg    = "<id>",
      .help_string = "Browse object",
      .callback =    browse_obj,
    },
    {
      .arg =         "-browse-children",
      .help_arg    = "<id>",
      .help_string = "Browse children",
      .callback =    browse_children,
    },
    /* TODO: add db dir, delete db dir, add media dir, delete media dir */
  
    {
      /* End */
    }
  };

const bg_cmdline_app_data_t app_data =
  {
    .package =  PACKAGE,
    .version =  VERSION,
    .synopsis = TRS("[options] command ...\n"),
    .help_before = TRS("gmerlin media DB tool\n"),
    .args = (bg_cmdline_arg_array_t[]) { { TRS("Options"), global_options },
                                         { TRS("Commands"), commands },
                                         {  } },
  };

static int handle_msg(void * data, gavl_msg_t * msg)
  {
  gavl_msg_dump(msg, 0);
  return 1;
  }

int main(int argc, char ** argv)
  {
  
  gavl_time_t t = GAVL_TIME_SCALE / 20;

  bg_app_init("mdb-tool", TRS("Manipulate Media DB"));
  
  bg_cmdline_init(&app_data);
  
  bg_control_init(&ctrl, bg_msg_sink_create(handle_msg, NULL, 0));
  
  bg_cmdline_parse(global_options, &argc, &argv, NULL);

  /* Create registries */

  bg_plugins_init("generic");
  
  if(!(mdb = bg_mdb_create(path, do_create, NULL)))
    return EXIT_FAILURE;

  mdb_ctrl =  bg_mdb_get_controllable(mdb);

  bg_controllable_connect(mdb_ctrl, &ctrl);
  
  bg_cmdline_parse(commands, &argc, &argv, NULL);
  
  if(do_monitor)
    {
    bg_handle_sigint();
    
    while(1)
      {
      if(bg_got_sigint() ||
         !bg_msg_sink_iteration(ctrl.evt_sink))
        break;

      if(!bg_msg_sink_get_num(ctrl.evt_sink))
        gavl_time_delay(&t);
      }
    }

  bg_mdb_stop(mdb);
  bg_mdb_destroy(mdb);

  bg_control_cleanup(&ctrl);
  
  return EXIT_SUCCESS;
  }
