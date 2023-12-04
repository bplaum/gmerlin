
#ifndef BG_RECORDINGDEVICE_H_INCLUDED
#define BG_RECORDINGDEVICE_H_INCLUDED

#include <gmerlin/bgmsg.h>

typedef struct bg_recording_device_registry_s bg_recording_device_registry_t;

bg_recording_device_registry_t * bg_recording_device_registry_create();
int bg_recording_device_registry_update(bg_recording_device_registry_t * reg);
bg_msg_hub_t * bg_recording_device_registry_get_msg_hub(bg_recording_device_registry_t * reg);
void bg_recording_device_registry_destroy(bg_recording_device_registry_t * reg);

#endif // BG_RECORDINGDEVICE_H_INCLUDED
