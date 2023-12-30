#ifndef BG_RESOURCEMANAGER_H_INCLUDED
#define BG_RESOURCEMANAGER_H_INCLUDED


#include <gmerlin/bgmsg.h>


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

#endif // BG_RESOURCEMANAGER_H_INCLUDED
