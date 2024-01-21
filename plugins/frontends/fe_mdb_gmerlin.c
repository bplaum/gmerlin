#include <config.h>
#include <gmerlin/translation.h>

#include <gmerlin/frontend.h>


bg_frontend_plugin_t the_plugin =
  {
    .common =
    {
      BG_LOCALE,
      .name =      "fe_mdb_gmerlin",
      .long_name = TRS("Control Media DB"),
      .description = TRS("Uses the native gmerlin control protocol"),
      .type =     BG_PLUGIN_FRONTEND_MDB,
      .flags =    0,
      .create =   bg_frontend_gmerlin_create,
      .destroy =   bg_frontend_gmerlin_destroy,
      .priority =         1,
    },
    .update = bg_frontend_gmerlin_ping,
    .open = bg_frontend_gmerlin_open_mdb,
  };

/* Include this into all plugin modules exactly once
   to let the plugin loader obtain the API version */
BG_GET_PLUGIN_API_VERSION;
