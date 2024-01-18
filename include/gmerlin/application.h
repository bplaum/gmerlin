/*
 *  Application wide variables
 *
 *  They are set once from main() and are considered read-only
 *  later on
 */

#ifndef BG_APPLICATION_H_INCLUDED
#define BG_APPLICATION_H_INCLUDED

#include <gavl/gavl.h>
#include <gavl/value.h>
#include <gavl/metatags.h>

#include <gmerlin/bgmsg.h>
#include <gmerlin/state.h>

extern gavl_dictionary_t bg_app_vars;

#define BG_APP_NAME      "Name"
#define BG_APP_LABEL     GAVL_META_LABEL
#define BG_APP_CFG_DIR   "configdir"
#define BG_APP_ICON_NAME "IconName"


void bg_app_init(const char * name,
                 const char * label,
                 const char * icon_name);

const char * bg_app_get_name();
const char * bg_app_get_label();
// const char * bg_app_get_window_icon();

const char * bg_app_get_icon_name();

char * bg_app_get_icon_file();


const char * bg_app_get_config_dir();
void bg_app_set_config_dir(const char * p);
// void bg_app_set_window_icon(const char * icon);


void bg_array_add_application_icons(gavl_array_t * arr, const char * prefix, const char * name);


// const gavl_array_t * bg_app_get_application_icons();
// void bg_app_add_application_icons(const char * prefix, const char * name);

void bg_dictionary_add_application_icons(gavl_dictionary_t * dict,
                                         const char * prefix,
                                         const char * name);


#endif // BG_APPLICATION_H_INCLUDED
