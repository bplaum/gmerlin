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

#include <string.h>

#include <gmerlin/utils.h>
#include <gmerlin/log.h>
#include <gmerlin/pluginregistry.h>

/*
  const char * name;
  const char * out_mimetype;
  const char * in_mimetype;
  const char * ext;
  int supported;

  int (*is_supported)(bg_plugin_registry_t * plugin_reg);
  char * (*make_command)(const char * src, const char * dst, const char * log_opt);
  char * (*make_protocol_info)(bg_db_object_t * obj, const char * mimetype);
  int (*get_bitrate)(bg_db_object_t * obj);
*/

/* mp3 */
static const int mp3_bitrate = 320;

static int is_supported_mp3(bg_plugin_registry_t * plugin_reg)
  {
  return !!bg_plugin_find_by_name(plugin_reg, "c_lame");
  }

static char * make_command_mp3(void)
  {
  return bg_sprintf("gavf-recompress -ac 'codec=c_lame{cbr_bitrate=%d}'",
                    mp3_bitrate);
  }

static int get_bitrate_mp3(bg_db_object_t * obj)
  {
  return mp3_bitrate;
  }

/* AAC */

static const int aac_bitrate = 128;

static int is_supported_aac(bg_plugin_registry_t * plugin_reg)
  {
  return !!bg_plugin_find_by_name(plugin_reg, "c_faac");
  }

static char * make_command_aac(void)
  {
  return bg_sprintf("gavf-recompress -ac 'codec=c_faac{object_type=mpeg4_lc:bitrate=%d}'",
                    aac_bitrate);
  }

static int get_bitrate_aac(bg_db_object_t * obj)
  {
  return aac_bitrate;
  }

/* Vorbis */

static int is_supported_vorbis(bg_plugin_registry_t * plugin_reg)
  {
  return !!bg_plugin_find_by_name(plugin_reg, "c_vorbisenc");
  }

static char * make_command_vorbis(void)
  {
  return bg_sprintf("gavf-recompress -ac 'codec=c_vorbisenc{quality=5.0:bitrate_mode=vbr}'");
  }

/* Opus */

static int is_supported_opus(bg_plugin_registry_t * plugin_reg)
  {
  return !!bg_plugin_find_by_name(plugin_reg, "c_opusenc");
  }

static char * make_command_opus(void)
  {
  return bg_sprintf("gavf-recompress -ac 'codec=c_opusenc'");
  }


/* FLAC */

static int is_supported_flac(bg_plugin_registry_t * plugin_reg)
  {
  return !!bg_plugin_find_by_name(plugin_reg, "c_flacenc");
  }

static char * make_command_flac(void)
  {
  return bg_sprintf("gavf-recompress -ac 'codec=c_flacenc{compression_level=8}'");
  }

const bg_upnp_recompressor_t bg_upnp_recompressors[] =
  {
    {
      .name         = "MP3",
      .out_mimetype = "audio/mpeg",
      .in_mimetype  = "audio/*",
      .ext          = ".mp3",
      .is_supported = is_supported_mp3,
      .make_command = make_command_mp3,
      .get_bitrate  = get_bitrate_mp3,
    },
    {
      .name         = "AAC",
      .out_mimetype = "audio/aac",
      .in_mimetype  = "audio/*",
      .ext          = ".aac",
      .is_supported = is_supported_aac,
      .make_command = make_command_aac,
      .get_bitrate  = get_bitrate_aac,
    },
    {
      .name         = "Vorbis",
      .out_mimetype = "audio/ogg; codecs=vorbis",
      .in_mimetype  = "audio/*",
      .ext          = ".ogg",
      .is_supported = is_supported_vorbis,
      .make_command = make_command_vorbis,
    },
    {
      .name         = "Opus",
      .out_mimetype = "audio/opus",
      .in_mimetype  = "audio/*",
      .ext          = ".opus",
      .is_supported = is_supported_opus,
      .make_command = make_command_opus,
    },
    {
      .name         = "Flac",
      .out_mimetype = "audio/ogg; codecs=flac",
      .in_mimetype  = "audio/*",
      .ext          = ".ogg",
      .is_supported = is_supported_flac,
      .make_command = make_command_flac,
    },
    {
      /* End */
    },
    
  };

char *
bg_upnp_recompressor_make_protocol_info(const bg_upnp_recompressor_t * c,
                                        bg_db_object_t * obj,
                                        const char * mimetype)
  {
  if(!mimetype)
    mimetype = c->out_mimetype;

  if(c->make_protocol_info)
    return c->make_protocol_info(obj, mimetype);
  else
    return bg_sprintf("http-get:*:%s:*", mimetype);
  }

int bg_upnp_recompressor_get_bitrate(const bg_upnp_recompressor_t * c,
                                       bg_db_object_t * obj)
  {
  if(c->get_bitrate)
    return c->get_bitrate(obj);
  else
    return 0;
  }

const bg_upnp_recompressor_t *
bg_upnp_recompressor_by_name(const char * name)
  {
  int i;
  const char * end;
  int name_len;
  
  if(!(end = strchr(name, '/')))
    end = name + strlen(name);

  name_len = end - name;
  
  i = 0;
  while(bg_upnp_recompressors[i].name)
    {
    if((strlen(bg_upnp_recompressors[i].name) == name_len) &&
       !strncmp(bg_upnp_recompressors[i].name, name, name_len))
      return &bg_upnp_recompressors[i];
    i++;
    }
  return NULL;
  }

