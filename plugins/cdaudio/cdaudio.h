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



#include <config.h>
#include <gmerlin/plugin.h>

#include <cdio/cdio.h>

#define DISCID_SIZE 33

/* Index structure */

typedef struct
  {
  int num_tracks;
  int num_audio_tracks;
  
  struct
    {
    uint32_t first_sector;
    uint32_t last_sector;

    /* We read in all available tracks. This flag signals if we can play audio */
    int is_audio;
    int index; /* Index into the track_info structre */
    } * tracks;

  } bg_cdaudio_index_t;

void bg_cdaudio_index_dump(bg_cdaudio_index_t*);
void bg_cdaudio_index_destroy(bg_cdaudio_index_t*);

/* CD status (obtained periodically during playback) */

typedef struct
  {
  int track;
  int sector;
  } bg_cdaudio_status_t;

/* Stuff, which varies from OS to OS.
   For now, linux is the only supported
   platform */

CdIo_t * bg_cdaudio_open(const char * device);

bg_cdaudio_index_t * bg_cdaudio_get_index(CdIo_t *);

void bg_cdaudio_close(CdIo_t*);

int bg_cdaudio_play(CdIo_t*, int first_sector, int last_sector);
void bg_cdaudio_stop(CdIo_t*);

/*
 * Get the status (time and track) of the currently played CD
 * The st structure MUST be saved between calls
 */


void bg_cdaudio_get_disc_id(bg_cdaudio_index_t * idx, char disc_id[DISCID_SIZE]);


/* Functions for getting the metadata */

#ifdef HAVE_MUSICBRAINZ
/* Try to get the metadata using musicbrainz */
int bg_cdaudio_get_metadata_musicbrainz(bg_cdaudio_index_t*,
                                        gavl_dictionary_t * mi,
                                        char * disc_id,
                                        char * musicbrainz_host,
                                        int musicbrainz_port,
                                        char * musicbrainz_proxy_host,
                                        int musicbrainz_proxy_port);
#endif

#ifdef HAVE_LIBCDDB

int bg_cdaudio_get_metadata_cddb(bg_cdaudio_index_t * idx,
                                 gavl_dictionary_t * mi,
                                 char * cddb_host,
                                 int cddb_port,
                                 char * cddb_path,
                                 char * cddb_proxy_host,
                                 int cddb_proxy_port,
                                 char * cddb_proxy_user,
                                 char * cddb_proxy_pass,
                                 int timeout);
#endif

/*
 *  Try to get metadata via CDtext. Requires a valid and open
 *  CDrom, returns False on failure
 */

int bg_cdaudio_get_metadata_cdtext(CdIo_t*,
                                   gavl_dictionary_t * mi,
                                   bg_cdaudio_index_t*);


/*
 *  Ripping support
 *  Several versions (cdparanoia, simple linux ripper) can go here
 */

void * bg_cdaudio_rip_create();

int bg_cdaudio_rip_init(void *, CdIo_t *cdio, int start_sector);

int bg_cdaudio_rip_rip(void * data, gavl_audio_frame_t * f);

/* Sector is absolute */

void bg_cdaudio_rip_seek(void * data, int sector);

void bg_cdaudio_rip_close(void * data);

const bg_parameter_info_t * bg_cdaudio_rip_get_parameters();

int
bg_cdaudio_rip_set_parameter(void * data, const char * name,
                             const gavl_value_t * val);

void bg_cdaudio_rip_destroy(void * data);

/* Load and save cd metadata */

int bg_cdaudio_load(gavl_dictionary_t * mi, const char * filename);
void bg_cdaudio_save(gavl_dictionary_t * mi,
                     const char * filename);

