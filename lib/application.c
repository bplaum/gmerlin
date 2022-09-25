
#include <gavl/gavl.h>
#include <gavl/value.h>
#include <gavl/metatags.h>

#include <gmerlin/application.h>
#include <gmerlin/utils.h>

#define WINDOW_ICON "window_icon"

/* Application support */

gavl_dictionary_t bg_app_vars;

void bg_app_init(const char * app_name,
                         const char * app_label)
  {
  gavl_dictionary_init(&bg_app_vars);

  gavl_dictionary_set_string(&bg_app_vars, BG_APP_NAME, app_name);
  gavl_dictionary_set_string(&bg_app_vars, BG_APP_LABEL, app_label);
  }

const char * bg_app_get_name()
  {
  return gavl_dictionary_get_string(&bg_app_vars, BG_APP_NAME);
  }

const char * bg_app_get_label()
  {
  return gavl_dictionary_get_string(&bg_app_vars, BG_APP_LABEL);
  }

const char * bg_app_get_window_icon()
  {
  return gavl_dictionary_get_string(&bg_app_vars, WINDOW_ICON);
  }

void bg_app_set_window_icon(const char * str)
  {
  gavl_dictionary_set_string(&bg_app_vars, WINDOW_ICON, str);
  }

const char * config_dir_default = "generic";

const char * bg_app_get_config_dir()
  {
  const char * ret;
  if((ret = gavl_dictionary_get_string(&bg_app_vars, BG_APP_CFG_DIR)))
    return ret;
  else
    return config_dir_default;
  }

void bg_app_set_config_dir(const char * p)
  {
  gavl_dictionary_set_string(&bg_app_vars, BG_APP_CFG_DIR, p);
  }

static void add_application_icon(gavl_array_t * arr,
                                 char * uri,
                                 int size,
                                 const char * mimetype)
  {
  gavl_dictionary_t * dict;
  
  gavl_value_t val;
  gavl_value_init(&val);

  dict = gavl_value_set_dictionary(&val);
  gavl_dictionary_set_string_nocopy(dict, GAVL_META_URI, uri);
  gavl_dictionary_set_string(dict, GAVL_META_MIMETYPE, mimetype);

  gavl_dictionary_set_int(dict, GAVL_META_WIDTH,  size);
  gavl_dictionary_set_int(dict, GAVL_META_HEIGHT, size);
  gavl_array_splice_val_nocopy(arr, -1, 0, &val);
  }


void bg_array_add_application_icons(gavl_array_t * arr, const char * prefix, const char * name)
  {
  char * slash;
  
  if(!gavl_string_ends_with(prefix, "/"))
    slash = "/";
  else
    slash = "";
  
  add_application_icon(arr, bg_sprintf("%s%s%s_16.png", prefix, slash, name), 16, "image/png");
  add_application_icon(arr, bg_sprintf("%s%s%s_48.png", prefix, slash, name), 48, "image/png");
  add_application_icon(arr, bg_sprintf("%s%s%s_48.bmp", prefix, slash, name), 48, "image/bmp");
  add_application_icon(arr, bg_sprintf("%s%s%s_48.jpg", prefix, slash, name), 48, "image/jpeg");
  add_application_icon(arr, bg_sprintf("%s%s%s_96.png", prefix, slash, name), 96, "image/png");
  add_application_icon(arr, bg_sprintf("%s%s%s_96.bmp", prefix, slash, name), 96, "image/bmp");
  add_application_icon(arr, bg_sprintf("%s%s%s_96.jpg", prefix, slash, name), 96, "image/jpeg");
  }

void bg_app_add_application_icons(const char * prefix,
                                  const char * name)
  {
  bg_dictionary_add_application_icons(&bg_app_vars, prefix, name);
  };

const gavl_array_t * bg_app_get_application_icons()
  {
  return gavl_dictionary_get_array(&bg_app_vars, GAVL_META_ICON_URL);
  // bg_dictionary_add_application_icons(&bg_app_vars, prefix, name);
  };

void bg_dictionary_add_application_icons(gavl_dictionary_t * m,
                                         const char * prefix,
                                         const char * name)
  {
  gavl_value_t val;
  gavl_array_t * arr;
  
  gavl_value_init(&val);

  arr = gavl_value_set_array(&val);

  bg_array_add_application_icons(arr, prefix, name);
  
  gavl_dictionary_set_nocopy(m, GAVL_META_ICON_URL, &val);
  }

void bg_set_network_node_info(const char * node_name, const gavl_array_t * icons, const char * icon,
                              bg_msg_sink_t * sink)
  {
  gavl_array_t * arr;
  
  gavl_value_t val;

  gavl_value_init(&val);
  gavl_value_set_string(&val, node_name);
  
  bg_state_set(NULL, 0, BG_APP_STATE_NETWORK_NODE, GAVL_META_LABEL, &val, sink, BG_MSG_STATE_CHANGED);

  gavl_value_reset(&val);

  if(icons)
    {
    arr = gavl_value_set_array(&val);
    gavl_array_copy(arr, icons);

    bg_state_set(NULL, 1, BG_APP_STATE_NETWORK_NODE, GAVL_META_ICON_URL, &val, sink, BG_MSG_STATE_CHANGED);
    }
  else if(icon)
    {
    gavl_value_set_string(&val, icon);
    bg_state_set(NULL, 1, BG_APP_STATE_NETWORK_NODE, GAVL_META_ICON_NAME, &val, sink, BG_MSG_STATE_CHANGED);
    }
  
  gavl_value_free(&val);
  }
  
