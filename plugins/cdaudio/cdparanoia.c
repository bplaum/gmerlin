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



#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <config.h>
#include <gmerlin/translation.h>

#include "cdaudio.h"
#define DO_NOT_WANT_PARANOIA_COMPATIBILITY

#ifdef HAVE_CDIO_CDDA_H
#include <cdio/cdda.h>
#elif defined HAVE_CDIO_PARANOIA_CDDA_H
#include <cdio/paranoia/cdda.h>
#endif

#ifdef HAVE_CDIO_PARANOIA_H
#include <cdio/paranoia.h>
#elif defined HAVE_CDIO_PARANOIA_PARANOIA_H
#include <cdio/paranoia/paranoia.h>
#endif



/*
 *  Ripping support
 *  Several versions (cdparanoia, simple linux ripper) can go here
 */

typedef struct
  {
  cdrom_drive_t *drive;
  cdrom_paranoia_t *paranoia;
  
  /* Configuration options (mostly correspond to commandline options) */
  
  int speed;

  int disable_paranoia;       // -Z
  int disable_extra_paranoia; // -Y
  int max_retries;            // (0: unlimited)
  
  CdIo_t *cdio;
  int current_sector;
  } cdparanoia_priv_t;


void * bg_cdaudio_rip_create()
  {
  cdparanoia_priv_t * ret;
  ret = calloc(1, sizeof(*ret));
  return ret;
  }

int bg_cdaudio_rip_init(void * data,
                        CdIo_t *cdio, int start_sector)
  {
  char * msg = NULL;
  int paranoia_mode;
  cdparanoia_priv_t * priv;
  priv = data;
  
  priv->cdio = cdio;
  
  /* cdparanoia */
  if(!priv->disable_paranoia)
    {
    priv->drive = cdio_cddap_identify_cdio(cdio, 1, &msg);

    if(!priv->drive)
      return 0;

    cdio_cddap_verbose_set(priv->drive,CDDA_MESSAGE_FORGETIT,CDDA_MESSAGE_FORGETIT);

    if(priv->speed != -1)
      cdio_cddap_speed_set(priv->drive, priv->speed);

    cdio_cddap_open(priv->drive);
  
    paranoia_mode=PARANOIA_MODE_FULL^PARANOIA_MODE_NEVERSKIP;

    if(priv->disable_extra_paranoia)
      {
      paranoia_mode|=PARANOIA_MODE_OVERLAP; /* cdda2wav style overlap
                                               check only */
      paranoia_mode&=~PARANOIA_MODE_VERIFY;
      }

    priv->paranoia = cdio_paranoia_init(priv->drive);
    cdio_paranoia_seek(priv->paranoia, start_sector, SEEK_SET);
    cdio_paranoia_modeset(priv->paranoia,paranoia_mode);
    }
  /* Simple audio reading */
  else
    {
    priv->current_sector = start_sector;
    }

  return 1;
  }

static void paranoia_callback(long inpos, paranoia_cb_mode_t function)
  {
  
  }

int bg_cdaudio_rip_rip(void * data, gavl_audio_frame_t * f)
  {
  cdparanoia_priv_t * priv = data;
  
  if(!priv->disable_paranoia)
    {
    int16_t * samples;

    //  fprintf(stderr, "read sector paranoia...");

    samples = cdio_paranoia_read(priv->paranoia, paranoia_callback);
    
    //    fprintf(stderr, "Ok: %d\n", f->valid_samples);
    
    memcpy(f->samples.s_16, samples, 588 * 4);
    }
  else
    {
    driver_return_code_t err;
    err = cdio_read_audio_sector(priv->cdio, f->samples.s_16, priv->current_sector);
    if(err != DRIVER_OP_SUCCESS)
      {
      //      fprintf(stderr, "Failed\n");
      return 0;
      }
    
    priv->current_sector++;
    }
  
  return 1;
  }

/* Sector is absolute */

void bg_cdaudio_rip_seek(void * data, int sector)
  {
  cdparanoia_priv_t * priv = data;
  
  if(!priv->disable_paranoia)
    cdio_paranoia_seek(priv->paranoia, sector, SEEK_SET);
  else
    priv->current_sector = sector;
  }

void bg_cdaudio_rip_close(void * data)
  {
  cdparanoia_priv_t * priv = data;
  
  if(priv->paranoia)
    {
    cdio_paranoia_free(priv->paranoia);
    priv->paranoia = NULL;
    }
  if(priv->drive)
    {
    cdio_cddap_close(priv->drive);
    priv->drive = NULL;
    }
  }


void bg_cdaudio_rip_destroy(void * data)
  {
  cdparanoia_priv_t * priv;
  priv = data;
  free(priv);
  }

static const bg_parameter_info_t parameters[] = 
  {
    {
      .name =       "cdparanoia",
      .long_name =  TRS("Cdparanoia"),
      .type =       BG_PARAMETER_SECTION,
    },
    {
      .name =       "cdparanoia_speed",
      .long_name =  TRS("Speed"),
      .type =       BG_PARAMETER_STRINGLIST,
      .val_default = GAVL_VALUE_INIT_STRING("Auto"),
      .multi_names = (char const *[]){ TRS("Auto"),
                              "4",
                              "8",
                              "16",
                              "32",
                              NULL },
    },
    {
      .name =        "cdparanoia_max_retries",
      .long_name =   TRS("Maximum retries"),
      .type =        BG_PARAMETER_INT,
      .val_min =     GAVL_VALUE_INIT_INT(0),
      .val_max =     GAVL_VALUE_INIT_INT(200),
      .val_default = GAVL_VALUE_INIT_INT(20),
      .help_string = TRS("Maximum number of retries, 0 = infinite")
    },
    {
      .name =        "cdparanoia_disable_paranoia",
      .long_name =   TRS("Disable paranoia"),
      .type =        BG_PARAMETER_CHECKBUTTON,
      .val_default = GAVL_VALUE_INIT_INT(0),
      .help_string = TRS("Disable all data verification and correction features.")
    },
    {
      .name =        "cdparanoia_disable_extra_paranoia",
      .long_name =   TRS("Disable extra paranoia"),
      .type =        BG_PARAMETER_CHECKBUTTON,
      .val_default = GAVL_VALUE_INIT_INT(0),
      .help_string = TRS("Disables intra-read data verification; only overlap checking at\
read boundaries is performed. It can wedge if errors  occur  in \
the attempted overlap area. Not recommended.")
    },
    { /* End of parameters */ }
  };

const bg_parameter_info_t * bg_cdaudio_rip_get_parameters()
  {
  return parameters;
  }


int
bg_cdaudio_rip_set_parameter(void * data, const char * name,
                             const gavl_value_t * val)
  {
  cdparanoia_priv_t * priv;
  priv = data;

  if(!name)
    return 0;

  if(!strcmp(name, "cdparanoia_speed"))
    {
    if(!strcmp(val->v.str, "Auto"))
      priv->speed = -1;
    else
      priv->speed = atoi(val->v.str);
    return 1;
    }
  else if(!strcmp(name, "cdparanoia_max_retries"))
    {
    priv->max_retries = val->v.i;
    return 1;
    }
  else if(!strcmp(name, "cdparanoia_disable_paranoia"))
    {
    priv->disable_paranoia = val->v.i;
    return 1;
    }
  else if(!strcmp(name, "cdparanoia_disable_extra_paranoia"))
    {
    priv->disable_extra_paranoia = val->v.i;
    return 1;
    }
  return 0;
  }
