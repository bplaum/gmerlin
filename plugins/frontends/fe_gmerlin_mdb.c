#include <frontend.h>


bg_frontend_plugin_t the_plugin =
  {
    .common =
    {
      BG_LOCALE,
      .name =      "fe_mpris",
      .long_name = TRS("Mpris2 frontend"),
      .description = TRS("Makes gmerlin controllable via Mpris2"),
      .type =     BG_PLUGIN_FRONTEND_RENDERER,
      .flags =    0,
      .create =   create_mpris2,
      .destroy =   destroy_mpris2,
      .get_controllable = bg_backend_gmerlin_get_controllable,
      .priority =         1,
    },
    .handle_message = handle_player_message_mpris,
    .update = ping_mpris2,
    .open = open_mpris2,
  };

/* Include this into all plugin modules exactly once
   to let the plugin loader obtain the API version */
BG_GET_PLUGIN_API_VERSION;
