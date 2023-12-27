

#include <gmerlin/recordingdevice.h>

int main(int argc, char ** argv)
  {
  //  bg_list_recording_devices(1000);
  bg_recording_device_registry_t * reg;
  reg = bg_recording_device_registry_create();
  bg_recording_device_registry_destroy(reg);
  }
