#include <config.h>

#include <gavl/gavl.h>


#include <gmerlin/parameter.h>
#include <gmerlin/http.h>
#include <gmerlin/httpserver.h>
#include <gmerlin/websocket.h>
#include <gmerlin/frontend.h>
#include <gmerlin/player.h>

#ifdef HAVE_DBUS
#include <gmerlin/bgdbus.h>
#endif

typedef struct
  {
  bg_player_t * player;
  bg_cfg_ctx_t * player_cfg;
  
  //  char * label; // Visible in the network

  bg_http_server_t * srv;
  
  bg_frontend_t ** frontends;
  int num_frontends;
  
  bg_parameter_info_t * parameters;

  char * vardir;
  
  gavl_dictionary_t state;
  char * state_file;
  } renderer_t;

void renderer_init(renderer_t * s, gavl_array_t * fe_arr);


void renderer_cleanup(renderer_t * s);
int renderer_iteration(renderer_t * s);

void renderer_set_parameter(void * priv, const char * name,
                            const gavl_value_t * val);
