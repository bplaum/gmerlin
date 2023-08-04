#include <string.h>


#include <config.h>

#include <gavl/log.h>
#define LOG_DOMAIN "mdb-tool"

#include <gmerlin/mdb.h>

#include <gmerlin/cfg_registry.h>
#include <gmerlin/parameter.h>
#include <gmerlin/cfgctx.h>

#include <gmerlin/utils.h>
#include <gmerlin/cmdline.h>
#include <gmerlin/translation.h>
#include <gmerlin/application.h>
#include <gmerlin/websocket.h>

// static int do_create = 0;
// static int do_monitor = 0;


char * path = NULL;

bg_mdb_t * mdb = NULL;
bg_controllable_t * mdb_ctrl = NULL;
bg_websocket_connection_t * conn = NULL;

static int ensure_mdb(int do_create)
  {
  if(do_create)
    {
    if(gavl_string_starts_with(path, "gmerlin-mdb://"))
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN,
               "create option does not work with remote DBs");
      return 0;
      }
    else
      {
      if(!(mdb = bg_mdb_create(path, 1, NULL)))
        return EXIT_FAILURE;
    
      mdb_ctrl =  bg_mdb_get_controllable(mdb);
      }
    }
  else
    {
    if(gavl_string_starts_with(path, "gmerlin-mdb://"))
      {
      if(!(conn = bg_websocket_connection_create(path, 3000, NULL)))
        {
        gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Couldn't connect to %s", path);
        return EXIT_FAILURE;
        }
      mdb_ctrl = bg_websocket_connection_get_controllable(conn);
      }
    else
      {
      if(!(mdb = bg_mdb_create(path, 0, NULL)))
        return EXIT_FAILURE;
    
      mdb_ctrl =  bg_mdb_get_controllable(mdb);
      }
    }
  return 1;
  }

static void opt_create(void * data, int * argc, char *** _argv, int arg)
  {
  ensure_mdb(1);
  }

#if 0
static void opt_monitor(void * data, int * argc, char *** _argv, int arg)
  {
  do_monitor = 1;
  }
#endif

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
      .arg =         "-db",
      .help_arg =    "<path>",
      .help_string = "DB path or websocket address (starting with gmerlin-mdb://)",
      .callback =    opt_db,
    },
    {
      /* End */
    }
  };

#if 0
static int handle_message_rescan(void * data, gavl_msg_t * msg)
  {
  int * ret = data;

  if((msg->NS == BG_MSG_NS_DB) && (msg->ID == BG_MSG_DB_RESCAN_DONE))
    *ret = 1;
  
  return 1;
  }
#endif

static void opt_rescan(void * data, int * argc, char *** _argv, int arg)
  {
  if(!ensure_mdb(0))
    return;

#if 1
  bg_mdb_rescan_sync(mdb_ctrl);
#else  
  if(mdb)
    bg_mdb_rescan_sync(mdb_ctrl);
  else if(conn)
    {
    gavl_time_t delay_time = GAVL_TIME_SCALE/20; // 50 ms
    int done = 0;
    bg_msg_sink_t * sink = bg_msg_sink_create(handle_message_rescan, &done, 0);

    bg_msg_hub_connect_sink(mdb_ctrl->evt_hub, sink);

    bg_mdb_rescan(mdb_ctrl);
  
    while(1)
      {
      bg_websocket_connection_iteration(conn);
      bg_msg_sink_iteration(sink);

      if(done)
        break;
    
      if(!bg_msg_sink_get_num(sink))
        gavl_time_delay(&delay_time);
      }
  
    bg_msg_hub_disconnect_sink(mdb_ctrl->evt_hub, sink);
    bg_msg_sink_destroy(sink);
    }
#endif
  }

static void browse_obj(void * data, int * argc, char *** _argv, int arg)
  {
  gavl_msg_t * msg;
  
  if(arg >= *argc)
    {
    fprintf(stderr, "Option -browse-obj requires an argument\n");
    exit(-1);
    }

  if(!ensure_mdb(0))
    return;
  
  msg = bg_msg_sink_get(mdb_ctrl->cmd_sink);
  gavl_msg_set_id_ns(msg, BG_FUNC_DB_BROWSE_OBJECT, BG_MSG_NS_DB);
  gavl_dictionary_set_string(&msg->header, GAVL_MSG_CONTEXT_ID, (*_argv)[arg]);
  bg_msg_sink_put(mdb_ctrl->cmd_sink);
  
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

  if(!ensure_mdb(0))
    return;

  msg = bg_msg_sink_get(mdb_ctrl->cmd_sink);
  gavl_msg_set_id_ns(msg, BG_FUNC_DB_BROWSE_CHILDREN, BG_MSG_NS_DB);
  gavl_dictionary_set_string(&msg->header, GAVL_MSG_CONTEXT_ID, (*_argv)[arg]);
  bg_msg_sink_put(mdb_ctrl->cmd_sink);
  
  bg_cmdline_remove_arg(argc, _argv, arg);
  }

static void add_sql_dir(void * data, int * argc, char *** _argv, int arg)
  {
  if(arg >= *argc)
    {
    fprintf(stderr, "Option -add-sql-dir requires an argument\n");
    exit(-1);
    }

  if(!ensure_mdb(0))
    return;
  
  bg_mdb_add_sql_directory(mdb_ctrl, (*_argv)[arg]);
  
  bg_cmdline_remove_arg(argc, _argv, arg);
  }

static void del_sql_dir(void * data, int * argc, char *** _argv, int arg)
  {
  if(arg >= *argc)
    {
    fprintf(stderr, "Option -del-sql-dir requires an argument\n");
    exit(-1);
    }
  if(!ensure_mdb(0))
    return;
  bg_mdb_del_sql_directory(mdb_ctrl, (*_argv)[arg]);
  
  bg_cmdline_remove_arg(argc, _argv, arg);
  }

static bg_cmdline_arg_t commands[] =
  {
    {
      .arg =         "-create",
      .help_string = "Create DB",
      .callback =    opt_create,
    },
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
    {
      .arg =         "-add-sql-dir",
      .help_arg    = "<path>",
      .help_string = "Add SQL directory",
      .callback =    add_sql_dir,
    },
    {
      .arg =         "-del-sql-dir",
      .help_arg    = "<path>",
      .help_string = "Delete SQL directory",
      .callback =    del_sql_dir,
    },
    /* TODO: add more */
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


int main(int argc, char ** argv)
  {
  //  gavl_time_t t = GAVL_TIME_SCALE / 20;

  bg_app_init("mdb-tool", TRS("Manipulate Media DB"));
  
  bg_cmdline_init(&app_data);
  
  bg_cmdline_parse(global_options, &argc, &argv, NULL);

  /* Create registries */

  bg_plugins_init("generic");

#if 0  
  if(gavl_string_starts_with(path, "gmerlin-mdb://"))
    {
    if(!(conn = bg_websocket_connection_create(path, 3000, NULL)))
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Couldn't connect to %s", path);
      return EXIT_FAILURE;
      }
    mdb_ctrl = bg_websocket_connection_get_controllable(conn);
    }
  else
    {
    if(!(mdb = bg_mdb_create(path, do_create, NULL)))
      return EXIT_FAILURE;
    
    mdb_ctrl =  bg_mdb_get_controllable(mdb);
    }
  
  bg_controllable_connect(mdb_ctrl, &ctrl);
#endif
  
  bg_cmdline_parse(commands, &argc, &argv, NULL);

#if 0
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
#endif
  
  if(mdb)
    {
    bg_mdb_stop(mdb);
    bg_mdb_destroy(mdb);
    }

  if(conn)
    bg_websocket_connection_destroy(conn);
  
  return EXIT_SUCCESS;
  }
