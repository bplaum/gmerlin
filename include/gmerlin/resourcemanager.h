#ifndef BG_RESOURCEMANAGER_H_INCLUDED
#define BG_RESOURCEMANAGER_H_INCLUDED


#include <gmerlin/bgmsg.h>

#define BG_RESOURCE_EXPIRE_TIME  "ExpireTime"
#define BG_RESOURCE_PRIORITY     "Priority"

#define BG_RESOURCE_PRIORITY_MIN     1
#define BG_RESOURCE_PRIORITY_DEFAULT 2
#define BG_RESOURCE_PRIORITY_MAX     3

/*
 *  Unified resource manager for:
    - Removable HDDs and disks
    - Sources and sinks for audio and video
    - Remote backends (servers or renderers)
    - 
*/

/* Get the process wide resource manager, create a new one if necessary */
  

bg_controllable_t * bg_resourcemanager_get_controllable();

gavl_array_t * bg_resourcemanager_get(const char * klass);

void bg_resourcemanager_publish(const char * id, const gavl_dictionary_t * dict);
void bg_resourcemanager_unpublish(const char * id);

/* To be used by resource plugins (i.e. from the resource detector thread) only */

gavl_dictionary_t * bg_resource_get_by_id(int local, const char * id);
gavl_dictionary_t * bg_resource_get_by_idx(int local, int idx);

#endif // BG_RESOURCEMANAGER_H_INCLUDED
