#include <config.h>

#include <gmerlin/plugin.h>
#include <gmerlin/translation.h>
#include <gmerlin/backend.h>

bg_backend_plugin_t the_plugin =
  {
    .common =
    {
      BG_LOCALE,
      .name =      "be_mdb_gmerlin",
      .long_name = TRS("gmerlin server backend"),
      .description = TRS("Controls remote gmerlin servers"),
      .type =     BG_PLUGIN_BACKEND_MDB,
      .flags =    0,
      .create =   bg_backend_gmerlin_create,
      .destroy =   bg_backend_gmerlin_destroy,
      .get_controllable = bg_backend_gmerlin_get_controllable,
      .priority =         1,
    },
    .protocol = BG_BACKEND_URI_SCHEME_GMERLIN_MDB,
    .update = bg_backend_gmerlin_update,
    .open = bg_backend_gmerlin_open,
  };

/* Include this into all plugin modules exactly once
   to let the plugin loader obtain the API version */
BG_GET_PLUGIN_API_VERSION;
