/*****************************************************************
 * gmerlin - a general purpose multimedia framework and applications
 *
 * Copyright (c) 2001 - 2024 Members of the Gmerlin project
 * http://github.com/bplaum
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




#define CDRDAO_PARAMS                   \
  {                                     \
    .name = "cdrdao",                     \
    .long_name = TRS("Burn options"),        \
    .type = BG_PARAMETER_SECTION,         \
  },                                    \
  {                                     \
    .name = "cdrdao_run",                 \
    .long_name = TRS("Run cdrdao"),            \
    .type = BG_PARAMETER_CHECKBUTTON,     \
    .val_default = GAVL_VALUE_INIT_INT(0),          \
  },                                    \
  {                                     \
    .name =       "cdrdao_device",          \
    .long_name =  TRS("Device"),                 \
    .type = BG_PARAMETER_STRING,            \
    .val_default = GAVL_VALUE_INIT_STRING(NULL),                        \
    .help_string = TRS("Device to use as burner. Type \"cdrdao scanbus\" at the commandline for available devices. Leave this empty to use the default /dev/cdrecorder."), \
  }, \
  {                                     \
    .name =       "cdrdao_driver",          \
    .long_name =  TRS("Driver"),                 \
    .type = BG_PARAMETER_STRING,            \
    .val_default = GAVL_VALUE_INIT_STRING(NULL),   \
    .help_string = TRS("Driver to use. Check the cdrdao manual page and the cdrdao README for available drivers/options. Leave this empty to attempt autodetection."), \
  }, \
  {                                     \
  .name =       "cdrdao_eject",                           \
    .long_name =  TRS("Eject after burning"),                       \
    .type = BG_PARAMETER_CHECKBUTTON,                          \
    .val_default = GAVL_VALUE_INIT_INT(0),                               \
  },                                                       \
  {                                                      \
    .name =       "cdrdao_simulate",                           \
    .long_name =  TRS("Simulate"),                  \
    .type = BG_PARAMETER_CHECKBUTTON,                          \
    .val_default = GAVL_VALUE_INIT_INT(0),                 \
    },                                                    \
  {                                                 \
    .name =       "cdrdao_speed",                       \
    .long_name =  TRS("Speed"),                  \
    .type = BG_PARAMETER_INT,                          \
    .val_default = GAVL_VALUE_INIT_INT(0),                 \
    .val_min = GAVL_VALUE_INIT_INT(0),                 \
    .val_max = GAVL_VALUE_INIT_INT(1000),                 \
    .help_string = TRS("Set the writing speed. 0 means autodetect."), \
    }, \
    {                                                   \
    .name =       "cdrdao_nopause",                       \
    .long_name =  TRS("No pause"),                  \
    .type = BG_PARAMETER_CHECKBUTTON,               \
    .val_default = GAVL_VALUE_INIT_INT(0),                 \
    .help_string = TRS("Skip the 10 second pause before writing or simulating starts."), \
    }



typedef struct bg_cdrdao_s bg_cdrdao_t;

bg_cdrdao_t * bg_cdrdao_create();
void bg_cdrdao_destroy(bg_cdrdao_t *);
void bg_cdrdao_set_callbacks(bg_cdrdao_t *, bg_e_pp_callbacks_t * callbacks);

void bg_cdrdao_set_parameter(void * data, const char * name,
                             const gavl_value_t * val);

/* 1 if cdrdao was actually run */
int bg_cdrdao_run(bg_cdrdao_t *, const char * toc_file);

void bg_cdrdao_stop(bg_cdrdao_t *);
