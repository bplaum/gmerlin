/*
 *  Application wide variables
 *
 *  They are set once from main() and are considered read-only
 *  later on
 */

#include <gavl/gavl.h>
#include <gavl/value.h>
#include <gavl/metatags.h>

#include <gmerlin/msgqueue.h>
#include <gmerlin/state.h>


extern gavl_dictionary_t bg_app_vars;

#define BG_APP_NAME     "Name"
#define BG_APP_LABEL    GAVL_META_LABEL
#define BG_APP_CFG_DIR  "configdir"



/* State variables */

#define BG_APP_STATE_NETWORK_NODE "Node"

// #define BG_APP_NETWORK_ICONS     GAVL_META_ICON_URL
// #define BG_APP_NETWORK_ICON_NAME GAVL_META_ICON_NAME

// #define BG_APP_NETWORK_NAME      GAVL_META_LABEL

void bg_app_init(const char * name,
                 const char * label);

const char * bg_app_get_name();
const char * bg_app_get_label();
const char * bg_app_get_label();
const char * bg_app_get_window_icon();

const char * bg_app_get_config_dir();
void bg_app_set_config_dir(const char * p);
void bg_app_set_window_icon(const char * icon);

void bg_array_add_application_icons(gavl_array_t * arr, const char * prefix, const char * name);

void bg_dictionary_add_application_icons(gavl_dictionary_t * dict,
                                         const char * prefix,
                                         const char * name);

/* Call this once with the sink, which will send the events to the websocket context */

void bg_set_network_node_info(const char * node_name, const gavl_array_t * icons, const char * icon_name,
                              bg_msg_sink_t * sink);
  
