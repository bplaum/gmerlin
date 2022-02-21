
#include <config.h>
#include <locale.h>


#include <gtk/gtk.h>

#include <gmerlin/mdb.h>

#include <gmerlin/cfg_registry.h>
#include <gmerlin/parameter.h>
#include <gmerlin/cfgctx.h>

#include <gmerlin/utils.h>
#include <gmerlin/cmdline.h>
#include <gmerlin/translation.h>
#include <gmerlin/iconfont.h>
#include <gmerlin/application.h>

#include <gui_gtk/mdb.h>

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

const bg_cmdline_app_data_t app_data =
  {
    .package =  PACKAGE,
    .version =  VERSION,
    .synopsis = TRS("[options]\n"),
    .help_before = TRS("gmerlin GUI db editor\n"),
    .args = (bg_cmdline_arg_array_t[]) { { TRS("Options"), global_options },
                                       {  } },
  };


static gboolean destroy_callback(GtkWidget * w, GdkEventAny * event,
                                gpointer data)
  {
  fprintf(stderr, "destroy callback\n");
  gtk_main_quit();
  return FALSE;
  }


int main(int argc, char ** argv)
  {
  bg_mdb_t * mdb;
  GtkWidget * window;
  bg_gtk_mdb_tree_t * tree;
  
  bg_iconfont_init();

  bg_app_init("mdb-tree", TRS("GUI tree editor"));


  /* Create registries */
  bg_cmdline_init(&app_data);
  bg_cmdline_parse(global_options, &argc, &argv, NULL);

  bg_plugins_init();
  
  if(!(mdb = bg_mdb_create(path, do_create, NULL)))
    return EXIT_FAILURE;
  
  gtk_init(&argc, &argv);

  /* No, we don't like commas as decimal separators */
  setlocale(LC_NUMERIC, "C");

  tree = bg_gtk_mdb_tree_create(bg_mdb_get_controllable(mdb));
  window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

  g_signal_connect(G_OBJECT(window), "destroy",
                   G_CALLBACK(destroy_callback),
                   NULL);

  
  gtk_container_add(GTK_CONTAINER(window),
                    bg_gtk_mdb_tree_get_widget(tree));

  gtk_window_set_default_size(GTK_WINDOW(window),
                              600, 400);
  
  gtk_widget_show(window);

  gtk_main();

  fprintf(stderr, "gtk_main finished\n");
  
  bg_mdb_destroy(mdb);
  
  return EXIT_SUCCESS;
  }
