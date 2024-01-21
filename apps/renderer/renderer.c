
#include <string.h>
#include <uuid/uuid.h>

#include <config.h>

#include "renderer.h"

#include <gmerlin/bggavl.h>
#include <gmerlin/utils.h>
#include <gmerlin/translation.h>
#include <gmerlin/log.h>
#include <gmerlin/application.h>
#include <gmerlin/ov.h>

#define LOG_DOMAIN "renderer"
#define VAR_PREFIX "renderer"

const bg_parameter_info_t * renderer_get_parameters(renderer_t * s);

static const bg_parameter_info_t parameters[] =
  {
    {
      .name =      "loglevel",
      .type = BG_PARAMETER_INT,
      .long_name =  TRS("Log level"),
      .val_min = GAVL_VALUE_INIT_INT(0),
      .val_max = GAVL_VALUE_INIT_INT(4),
      .val_default = GAVL_VALUE_INIT_INT(3),
    },
    {
      .name =      "syslog",
      .long_name = TRS("Syslog name"),
      .type = BG_PARAMETER_STRING,
    },
    {
      .name =      "label",
      .long_name = TRS("Label"),
      .type = BG_PARAMETER_STRING,
      .val_default = GAVL_VALUE_INIT_STRING("Gmerlin renderer"),
    },
    {
      .name =      "vardir",
      .type = BG_PARAMETER_DIRECTORY,
      .long_name =  TRS("Storage directory"),
    },
    {
      .name =      "state_file",
      .type = BG_PARAMETER_FILE,
      .long_name =  TRS("State file"),
    },

    { /* End */ },
  };


void renderer_init(renderer_t * s, gavl_array_t * fe_arr)
  {
  gavl_dictionary_t * section;
  const gavl_value_t * uuid_val;
  const char * uuid = NULL;
  bg_controllable_t * player_ctrl;

  gavl_value_t icons_val;

  gavl_value_init(&icons_val);
    
  memset(s, 0, sizeof(*s));
  
  s->srv = bg_http_server_create();
  
  section = bg_cfg_registry_find_section(bg_cfg_registry, "renderer");
  bg_cfg_section_create_items(section, renderer_get_parameters(s));
  bg_cfg_section_apply(section, renderer_get_parameters(s), renderer_set_parameter, s);

  /* Create vardir */
  if(!s->vardir)
    s->vardir = bg_search_var_dir(VAR_PREFIX);

  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Using data directory: %s", s->vardir);
  
  
  /* Load state */

  if(!s->state_file)
    s->state_file = bg_sprintf("%s/state.xml", s->vardir);

  if(!bg_dictionary_load_xml(&s->state, s->state_file, "state") ||
     !(uuid_val =  bg_state_get(&s->state, "server", "uuid")) ||
     !(uuid = gavl_value_get_string(uuid_val)))
    {
    uuid_t u;
    char uuid_str[37];
    gavl_value_t v;
    
    uuid_generate(u);
    uuid_unparse(u, uuid_str);

    gavl_value_init(&v);
    gavl_value_set_string(&v, uuid_str);

    bg_state_set(&s->state, 1, "server", "uuid", &v, NULL, 0);
    
    uuid_val =  bg_state_get(&s->state, "server", "uuid");
    uuid = gavl_value_get_string(uuid_val);
    
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Generated uuid: %s", uuid);
    }
  else
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Using saved uuid: %s", uuid);
  
  s->player = bg_player_create();

  s->player_cfg = bg_player_get_cfg(s->player);
  
  player_ctrl = bg_player_get_controllable(s->player);

  /* Apply config */
  section = bg_cfg_registry_find_section(bg_cfg_registry, "player");
  bg_cfg_ctx_array_create_sections(s->player_cfg, section);
  bg_player_apply_cmdline(s->player_cfg);
  
  bg_cfg_ctx_set_sink_array(s->player_cfg, player_ctrl->cmd_sink);
  bg_cfg_ctx_apply_array(s->player_cfg);

  /* Start http part */
  bg_http_server_set_static_path(s->srv, "/static");
  bg_http_server_start(s->srv);
  
  /* Create frontends */
  s->frontends = bg_frontends_create(player_ctrl,
                                     BG_PLUGIN_FRONTEND_RENDERER, fe_arr, &s->num_frontends);
  
  bg_player_state_init(&s->state);
  
  bg_player_run(s->player);
  bg_state_apply(&s->state, player_ctrl->cmd_sink, BG_CMD_SET_STATE);

  gavl_value_init(&icons_val);
  
  }

void renderer_cleanup(renderer_t * s)
  {

  if(s->player)
    bg_player_quit(s->player);
  
  if(s->state_file)
    {
    bg_dictionary_save_xml(&s->state, s->state_file, "state");
    free(s->state_file);
    }

  
  gavl_dictionary_free(&s->state);

  bg_frontends_destroy(s->frontends, s->num_frontends);
  
  if(s->parameters)
    bg_parameter_info_destroy_array(s->parameters);

  if(s->srv)
    bg_http_server_destroy(s->srv);
  
  if(s->player)
    bg_player_destroy(s->player);
  }

int renderer_iteration(renderer_t * s)
  {
  int ret = 0;
  
  ret += bg_http_server_iteration(s->srv);

  ret += bg_frontends_ping(s->frontends, s->num_frontends);
  
  return ret;
  }

const bg_parameter_info_t * renderer_get_parameters(renderer_t * s)
  {
  if(!s->parameters)
    {
    const bg_parameter_info_t * arr[3];
    arr[0] = bg_http_server_get_parameters(s->srv);
    arr[1] = parameters;
    arr[2] = NULL;
    s->parameters = bg_parameter_info_concat_arrays(arr);
    }
  return s->parameters;
  }


void renderer_set_parameter(void * priv, const char * name,
                            const gavl_value_t * val)
  {
  renderer_t * s = priv;
  if(!name)
    {
    bg_http_server_set_parameter(s->srv, NULL, NULL);
    return;
    }
  else if(!strcmp(name, "vardir"))
    s->vardir = gavl_strrep(s->vardir, val->v.str);
  else if(!strcmp(name, "state_file"))
    s->state_file = gavl_strrep(s->state_file, val->v.str);
  else if(!strcmp(name, "label"))
    gavl_dictionary_set_string(&bg_app_vars, BG_APP_LABEL, val->v.str);
  else if(!strcmp(name, "syslog"))
    {
    if(val->v.str)
      bg_log_syslog_init(val->v.str);
    }
  else if(!strcmp(name, "loglevel"))
    gavl_set_log_verbose(val->v.i);
  else
    bg_http_server_set_parameter(s->srv, name, val);

  }
