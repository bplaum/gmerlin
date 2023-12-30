
#include <gmerlin/resourcemanager.h>
#include <gmerlin/utils.h>
#include <gmerlin/pluginregistry.h>



int main(int argc, char ** argv)
  {
  gavl_time_t t = GAVL_TIME_SCALE/10;

  bg_handle_sigint();
  
  bg_plugins_init();

  /* This creates the resource manager */
  bg_resourcemanager_get_controllable();

  while(1)
    {
    gavl_time_delay(&t);

    if(bg_got_sigint())
      break;
    }
  
  }


