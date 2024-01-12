#include <config.h>

#include <gmerlin/utils.h>
#include <gmerlin/resourcemanager.h>
#include <gmerlin/pluginregistry.h>
#include <gmerlin/log.h>

#ifdef HAVE_DBUS
#include <gmerlin/bgdbus.h>
#endif  

void bg_global_cleanup()
  {
  bg_resourcemanager_cleanup();
  bg_plugins_cleanup();
  
  bg_cfg_registry_cleanup();
  
#ifdef HAVE_DBUS
  bg_dbus_cleanup();
#endif  

  bg_log_cleanup();
  
  }
