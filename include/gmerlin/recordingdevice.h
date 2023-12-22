
#ifndef BG_RECORDINGDEVICE_H_INCLUDED
#define BG_RECORDINGDEVICE_H_INCLUDED

#include <gmerlin/bgmsg.h>

typedef struct bg_recording_device_registry_s bg_recording_device_registry_t;

bg_recording_device_registry_t * bg_recording_device_registry_create();
int bg_recording_device_registry_update(bg_recording_device_registry_t * reg);
bg_msg_hub_t * bg_recording_device_registry_get_msg_hub(bg_recording_device_registry_t * reg);
void bg_recording_device_registry_destroy(bg_recording_device_registry_t * reg);

gavl_array_t * bg_get_recording_devices(int timeout);

void bg_list_recording_devices(int timeout);
void bg_opt_list_recording_sources(void * data, int * argc,
                                   char *** _argv, int arg);

#define BG_OPT_LIST_RECORDERS                  \
  { \
  .arg =         "-list-rec-src", \
  .help_string = TRS("List recording sources"), \
  .callback =    bg_opt_list_recording_sources, \
  }




#endif // BG_RECORDINGDEVICE_H_INCLUDED
