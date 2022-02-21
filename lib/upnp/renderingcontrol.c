/*****************************************************************
 * gmerlin - a general purpose multimedia framework and applications
 *
 * Copyright (c) 2001 - 2012 Members of the Gmerlin project
 * gmerlin-general@lists.sourceforge.net
 * http://gmerlin.sourceforge.net
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * *****************************************************************/

#include <config.h>

#include <upnp/devicepriv.h>
//#include <upnp/servicepriv.h>
#include <upnp/mediarenderer.h>
#include <string.h>
#include <gmerlin/player.h>
#include <gmerlin/playermsg.h>
#include <gmerlin/player.h>

#include <gavl/utils.h>

#define ARG_InstanceID                    1
#define ARG_CurrentPresetNameList         2
#define ARG_PresetName                    3

#define ARG_CurrentBrightness             4
#define ARG_DesiredBrightness             5

#define ARG_CurrentContrast               6
#define ARG_DesiredContrast               7

#define ARG_CurrentSharpness              8
#define ARG_DesiredSharpness              9

#define ARG_CurrentRedVideoGain           10
#define ARG_DesiredRedVideoGain           11

#define ARG_CurrentGreenVideoGain         12
#define ARG_DesiredGreenVideoGain         13

#define ARG_CurrentBlueVideoGain          14
#define ARG_DesiredBlueVideoGain          15

#define ARG_CurrentRedVideoBlackLevel     16
#define ARG_DesiredRedVideoBlackLevel     17

#define ARG_CurrentGreenVideoBlackLevel   18
#define ARG_DesiredGreenVideoBlackLevel   19

#define ARG_CurrentBlueVideoBlackLevel    20
#define ARG_DesiredBlueVideoBlackLevel    21

#define ARG_CurrentColorTemperature       22
#define ARG_DesiredColorTemperature       23

#define ARG_CurrentHorizontalKeystone     24
#define ARG_DesiredHorizontalKeystone     25

#define ARG_CurrentVerticalKeystone       26
#define ARG_DesiredVerticalKeystone       27

#define ARG_CurrentMute                   28
#define ARG_DesiredMute                   29

#define ARG_CurrentVolume                 30
// #define ARG_CurrentVolumeDB               31
#define ARG_DesiredVolume                 32
// #define ARG_DesiredVolumeDB               33

#define ARG_CurrentLoudness               34
#define ARG_DesiredLoudness               35

#define ARG_MinValue                      36
#define ARG_MaxValue                      37

#define ARG_Channel                       38

#define VOLUME_MAX                        100

#define VOL_2_INT(v) ((int)((v) * 256.0 + 0.5))
#define INT_2_VOL(v) ((float)(v) / 256.0)

/* Service actions */

static int ListPresets(bg_upnp_service_t * s)
  {
  bg_upnp_service_set_arg_out(&s->req, ARG_CurrentPresetNameList);
  return 1;
  }

static int SelectPreset(bg_upnp_service_t * s)
  {
  return 1;
  }

static int GetVolume(bg_upnp_service_t * s)
  {
  bg_upnp_service_set_arg_out(&s->req, ARG_CurrentVolume);
  
  fprintf(stderr, "GetVolume: %d\n",
          bg_upnp_service_desc_sv_get_int(&s->desc, "Volume"));
  
  return 1;
  }

static int SetVolume(bg_upnp_service_t * s)
  {
  float vol = (float)bg_upnp_service_get_arg_in_uint(&s->req, ARG_DesiredVolume) / (float)VOLUME_MAX;
  bg_player_set_volume(s->dev->ctrl.cmd_sink, vol);
  fprintf(stderr, "SetVolume: %f\n", vol);
  return 1;
  }

#if 0
static int SetVolumeDB(bg_upnp_service_t * s)
  {
  float vol_dB = INT_2_VOL(bg_upnp_service_get_arg_in_uint(&s->req, ARG_DesiredVolumeDB));
  bg_player_set_volume(s->dev->ctrl.cmd_sink, vol_dB);
  fprintf(stderr, "SetVolumeDB: %f\n", vol_dB);
  return 1;
  }
static int GetVolumeDB(bg_upnp_service_t * s)
  {
  bg_upnp_service_set_arg_out(&s->req, ARG_CurrentVolumeDB);
  return 1;
  }
#endif

static int SetMute(bg_upnp_service_t * s)
  {
  bg_player_set_mute(s->dev->ctrl.cmd_sink, bg_upnp_service_get_arg_in_uint(&s->req, ARG_DesiredMute));
  return 1;
  }

static int GetMute(bg_upnp_service_t * s)
  {
  bg_upnp_service_set_arg_out(&s->req, ARG_CurrentMute);
  return 1;
  }

/* Initialize service description */

static const char * xml_namespace = "urn:schemas-upnp-org:metadata-1-0/AVT_RCS";

static void init_service_desc(bg_upnp_service_desc_t * d)
  {
  bg_upnp_sv_val_t  val;
  bg_upnp_sv_t * sv;
  bg_upnp_sa_desc_t * sa;
  bg_upnp_sv_range_t r;
  
  /* State variables */

  // LastChange
  sv = bg_upnp_service_desc_add_sv(d, "LastChange",
                                   BG_UPNP_SV_EVENT, BG_UPNP_SV_STRING);
  // Event frequency
  sv->interval = GAVL_TIME_SCALE / 5;
  sv->ns = xml_namespace;

  // PresetNameList
  sv = bg_upnp_service_desc_add_sv(d, "PresetNameList",
                                   BG_UPNP_SV_EVENT_MOD, BG_UPNP_SV_STRING);

  // Brightness
  /*
  r.min.ui = 0;
  r.max.ui = 200;
  r.step.ui = 1;
    
  sv = bg_upnp_service_desc_add_sv(d, "Brightness",
                                   BG_UPNP_SV_EVENT_MOD, BG_UPNP_SV_UINT2);
  bg_upnp_sv_set_range(sv, &r);
  */ 

  // Contrast
  /*
  r.min.ui = 0;
  r.max.ui = 200;
  r.step.ui = 1;
  
  sv = bg_upnp_service_desc_add_sv(d, "Contrast",
                                   BG_UPNP_SV_EVENT_MOD, BG_UPNP_SV_UINT2);
  bg_upnp_sv_set_range(sv, &r);
  */ 

  // Sharpness
  /*
  r.min.ui = 0;
  r.max.ui = 200;
  r.step.ui = 1;
  
  sv = bg_upnp_service_desc_add_sv(d, "Sharpness",
                                   BG_UPNP_SV_EVENT_MOD, BG_UPNP_SV_UINT2);
  bg_upnp_sv_set_range(sv, &r);
  */ 

  // RedVideoGain
  /*
  r.min.ui = 0;
  r.max.ui = 200;
  r.step.ui = 1;
  
  sv = bg_upnp_service_desc_add_sv(d, "RedVideoGain",
                                   BG_UPNP_SV_EVENT_MOD, BG_UPNP_SV_UINT2);
  bg_upnp_sv_set_range(sv, &r);
  */ 

  // GreenVideoGain
  /*
  r.min.ui = 0;
  r.max.ui = 200;
  r.step.ui = 1;
  
  sv = bg_upnp_service_desc_add_sv(d, "GreenVideoGain",
                                   BG_UPNP_SV_EVENT_MOD, BG_UPNP_SV_UINT2);
  bg_upnp_sv_set_range(sv, &r);
  */ 

  // BlueVideoGain
  /*
  r.min.ui = 0;
  r.max.ui = 200;
  r.step.ui = 1;
  
  sv = bg_upnp_service_desc_add_sv(d, "BlueVideoGain",
                                   BG_UPNP_SV_EVENT_MOD, BG_UPNP_SV_UINT2);
  bg_upnp_sv_set_range(sv, &r);
  */ 

  // RedVideoBlackLevel
  /*
  r.min.ui = 0;
  r.max.ui = 200;
  r.step.ui = 1;
  
  sv = bg_upnp_service_desc_add_sv(d, "RedVideoBlackLevel",
                                   BG_UPNP_SV_EVENT_MOD, BG_UPNP_SV_UINT2);
  bg_upnp_sv_set_range(sv, &r);
  */ 

  // GreenVideoBlackLevel
  /*
  r.min.ui = 0;
  r.max.ui = 200;
  r.step.ui = 1;
  
  sv = bg_upnp_service_desc_add_sv(d, "GreenVideoBlackLevel",
                                   BG_UPNP_SV_EVENT_MOD, BG_UPNP_SV_UINT2);
  bg_upnp_sv_set_range(sv, &r);
  */ 

  // BlueVideoBlackLevel
  /*
  r.min.ui = 0;
  r.max.ui = 200;
  r.step.ui = 1;
  
  sv = bg_upnp_service_desc_add_sv(d, "BlueVideoBlackLevel",
                                   BG_UPNP_SV_EVENT_MOD, BG_UPNP_SV_UINT2);
  bg_upnp_sv_set_range(sv, &r);
  */ 

  // ColorTemperature
  /*
  r.min.ui = 0;
  r.max.ui = 200;
  r.step.ui = 1;
  
  sv = bg_upnp_service_desc_add_sv(d, "ColorTemperature",
                                   BG_UPNP_SV_EVENT_MOD, BG_UPNP_SV_UINT2);
  bg_upnp_sv_set_range(sv, &r);
  */ 

  // HorizontalKeystone
  /*
  r.min.i = -100;
  r.max.i = 100;
  r.step.ui = 1;
  
  sv = bg_upnp_service_desc_add_sv(d, "HorizontalKeystone",
                                   BG_UPNP_SV_EVENT_MOD, BG_UPNP_SV_INT2);
  bg_upnp_sv_set_range(sv, &r);
  */ 
  
  // VerticalKeystone
  /*
  r.min.i = -100;
  r.max.i = 100;
  r.step.ui = 1;
  
  sv = bg_upnp_service_desc_add_sv(d, "VerticalKeystone",
                                   BG_UPNP_SV_EVENT_MOD, BG_UPNP_SV_INT2);
  bg_upnp_sv_set_range(sv, &r);
  */ 

  // Mute
  sv = bg_upnp_service_desc_add_sv(d, "Mute",
                                   BG_UPNP_SV_EVENT_MOD, BG_UPNP_SV_BOOLEAN);
    
  // Volume
  r.min.ui = 0;
  r.max.ui = VOLUME_MAX;
  r.step.ui = 1;
  
  sv = bg_upnp_service_desc_add_sv(d, "Volume",
                                   BG_UPNP_SV_EVENT_MOD, BG_UPNP_SV_UINT2);
  bg_upnp_sv_set_range(sv, &r);

  // VolumeDB
  r.min.i = -32767;
  r.max.i = 0;
  r.step.i = 1;
  
  sv = bg_upnp_service_desc_add_sv(d, "VolumeDB",
                                   BG_UPNP_SV_EVENT_MOD, BG_UPNP_SV_INT2);
  bg_upnp_sv_set_range(sv, &r);

  // Loudness
  /*
  sv = bg_upnp_service_desc_add_sv(d, "Loudness",
                                   BG_UPNP_SV_EVENT_MOD, BG_UPNP_SV_BOOLEAN);
  */ 
  
  // A_ARG_TYPE_Channel
  sv = bg_upnp_service_desc_add_sv(d, "A_ARG_TYPE_Channel",
                                   BG_UPNP_SV_ARG_ONLY, BG_UPNP_SV_STRING);

  val.s = "Master";
  bg_upnp_sv_add_allowed(sv, &val);
  
  // A_ARG_TYPE_InstanceID
  sv = bg_upnp_service_desc_add_sv(d, "A_ARG_TYPE_InstanceID",
                                   BG_UPNP_SV_ARG_ONLY, BG_UPNP_SV_UINT4);

  // A_ARG_TYPE_PresetName
  sv = bg_upnp_service_desc_add_sv(d, "A_ARG_TYPE_PresetName",
                                   BG_UPNP_SV_ARG_ONLY, BG_UPNP_SV_STRING);
  val.s = "FactoryDefaults";
  bg_upnp_sv_add_allowed(sv, &val);
  
  /* Actions */

  // ListPresets
  sa = bg_upnp_service_desc_add_action(d, "ListPresets", ListPresets);

  bg_upnp_sa_desc_add_arg_in(sa, "InstanceID",
                             "A_ARG_TYPE_InstanceID", 0,
                             ARG_InstanceID);

  bg_upnp_sa_desc_add_arg_out(sa, "CurrentPresetNameList",
                              "PresetNameList", 0,
                              ARG_CurrentPresetNameList);

  // SelectPreset
  sa = bg_upnp_service_desc_add_action(d, "SelectPreset", SelectPreset);
  bg_upnp_sa_desc_add_arg_in(sa, "InstanceID",
                             "A_ARG_TYPE_InstanceID", 0,
                             ARG_InstanceID);

  bg_upnp_sa_desc_add_arg_in(sa, "PresetName",
                             "A_ARG_TYPE_PresetName", 0,
                             ARG_PresetName);

  // GetVolume
  sa = bg_upnp_service_desc_add_action(d, "GetVolume", GetVolume);
  bg_upnp_sa_desc_add_arg_in(sa, "InstanceID",
                             "A_ARG_TYPE_InstanceID", 0,
                             ARG_InstanceID);
  bg_upnp_sa_desc_add_arg_in(sa, "Channel",
                             "A_ARG_TYPE_Channel", 0,
                             ARG_Channel);

  bg_upnp_sa_desc_add_arg_out(sa, "CurrentVolume", "Volume", 0, ARG_CurrentVolume);

  // GetVolumeDB
#if 0
  sa = bg_upnp_service_desc_add_action(d, "GetVolumeDB", GetVolumeDB);
  bg_upnp_sa_desc_add_arg_in(sa, "InstanceID",
                             "A_ARG_TYPE_InstanceID", 0,
                             ARG_InstanceID);
  bg_upnp_sa_desc_add_arg_in(sa, "Channel",
                             "A_ARG_TYPE_Channel", 0,
                             ARG_Channel);

  bg_upnp_sa_desc_add_arg_out(sa, "CurrentVolume", "VolumeDB", 0, ARG_CurrentVolumeDB);
#endif
  // SetVolume
  sa = bg_upnp_service_desc_add_action(d, "SetVolume", SetVolume);
  bg_upnp_sa_desc_add_arg_in(sa, "InstanceID",
                             "A_ARG_TYPE_InstanceID", 0,
                             ARG_InstanceID);
  bg_upnp_sa_desc_add_arg_in(sa, "Channel",
                             "A_ARG_TYPE_Channel", 0,
                             ARG_Channel);

  bg_upnp_sa_desc_add_arg_in(sa, "DesiredVolume", "Volume", 0, ARG_DesiredVolume);

  // SetVolumeDB
#if 0
  sa = bg_upnp_service_desc_add_action(d, "SetVolumeDB", SetVolumeDB);
  bg_upnp_sa_desc_add_arg_in(sa, "InstanceID",
                             "A_ARG_TYPE_InstanceID", 0,
                             ARG_InstanceID);
  bg_upnp_sa_desc_add_arg_in(sa, "Channel",
                             "A_ARG_TYPE_Channel", 0,
                             ARG_Channel);

  bg_upnp_sa_desc_add_arg_in(sa, "DesiredVolume", "VolumeDB", 0, ARG_DesiredVolumeDB);
#endif

  // GetMute
  sa = bg_upnp_service_desc_add_action(d, "GetMute", GetMute);
  bg_upnp_sa_desc_add_arg_in(sa, "InstanceID",
                             "A_ARG_TYPE_InstanceID", 0,
                             ARG_InstanceID);
  bg_upnp_sa_desc_add_arg_in(sa, "Channel",
                             "A_ARG_TYPE_Channel", 0,
                             ARG_Channel);

  bg_upnp_sa_desc_add_arg_out(sa, "CurrentMute", "Mute", 0, ARG_CurrentMute);

  // SetMute
  sa = bg_upnp_service_desc_add_action(d, "SetMute", SetMute);
  bg_upnp_sa_desc_add_arg_in(sa, "InstanceID",
                             "A_ARG_TYPE_InstanceID", 0,
                             ARG_InstanceID);
  bg_upnp_sa_desc_add_arg_in(sa, "Channel",
                             "A_ARG_TYPE_Channel", 0,
                             ARG_Channel);

  bg_upnp_sa_desc_add_arg_in(sa, "DesiredMute", "Mute", 0, ARG_DesiredMute);
  }

void bg_upnp_service_init_rendering_control(bg_upnp_service_t * ret)
  {
  bg_upnp_service_init(ret, BG_UPNP_SERVICE_ID_RC, "RenderingControl", 1);
  init_service_desc(&ret->desc);
  bg_upnp_service_start(ret);
  }

void bg_upnp_rendering_control_set_volume(bg_upnp_service_t * s,
                                          float volume)
  {
  bg_upnp_sv_t * sv;
  sv = bg_upnp_service_desc_sv_by_name(&s->desc, "VolumeDB");
  bg_upnp_sv_set_int(sv, VOL_2_INT(volume));

  sv = bg_upnp_service_desc_sv_by_name(&s->desc, "Volume");
  bg_upnp_sv_set_uint(sv, (int)(volume * VOLUME_MAX + 0.5));
  }

