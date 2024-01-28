
#include <config.h>
#include <gmerlin/translation.h>

#include <gmerlin/frontend.h>


bg_frontend_plugin_t the_plugin =
  {
    .common =
    {
      BG_LOCALE,
      .name =      "fe_renderer_gmerlin",
      .long_name = TRS("Control players"),
      .description = TRS("Uses the native gmerlin control protocol"),
      .type =     BG_PLUGIN_FRONTEND_RENDERER,
      .flags =    BG_PLUGIN_NEEDS_HTTP_SERVER,
      .create =   bg_frontend_gmerlin_create,
      .destroy =   bg_frontend_gmerlin_destroy,
      .priority =         1,
    },
    .update = bg_frontend_gmerlin_ping,
    .open = bg_frontend_gmerlin_open_renderer,
  };

/* Include this into all plugin modules exactly once
   to let the plugin loader obtain the API version */
BG_GET_PLUGIN_API_VERSION;
