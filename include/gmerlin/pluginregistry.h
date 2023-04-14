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

#ifndef BG_PLUGINREGISTRY_H_INCLUDED
#define BG_PLUGINREGISTRY_H_INCLUDED

/* Plugin registry */
#include <pthread.h>

#include <gmerlin/plugin.h>
#include <gmerlin/cfg_registry.h>

/** \defgroup plugin_registry Plugin registry
 *  \brief Database of all installed plugins
 *
 *  The plugin registry keeps informations about all installed plugins.
 *  Furthermore, it manages default plugins and some other settings.
 *  Available plugins are cached in the file $HOME/.gmerlin/plugins.xml,
 *  which is used by all applications. Application specific data are
 *  stored in a \ref bg_cfg_section_t.
 *
 *  It allows you to search for plugins according to certain criteria.
 *  You get detailed information about plugins in \ref bg_plugin_info_t
 *  structs.
 */

/** \ingroup plugin_registry
 *  \brief URL variables handled by the registry
 */

#define BG_URL_VAR_TRACK    "track"      // Select track (see \ref bg_input_plugin_load_full)
#define BG_URL_VAR_VARIANT  "variant"    // Variant for multirate streams

#define BG_URL_VAR_SEEK    "seek"       // Seek to a spefific time in seconds
#define BG_URL_VAR_PLUGIN  "plugin"     // Force usage of a plugin
#define BG_URL_VAR_CMDLINE "cmdlineopt" // Respect the -i option

#define BG_INPUT_FLAG_PREFER_EDL          (1<<0)
#define BG_INPUT_FLAG_RESOLVE_REDIRECTORS (1<<1)
#define BG_INPUT_FLAG_SELECT_TRACK        (1<<2)
#define BG_INPUT_FLAG_GET_FORMAT          (1<<3)

/* For sections, which have a single plugin as parameter */
#define BG_PARAMETER_NAME_PLUGIN "p"


void
bg_track_set_force_raw(gavl_dictionary_t * dict, int force_raw);

void
bg_track_set_variant(gavl_dictionary_t * dict, int variant);

void
bg_track_set_current_location(gavl_dictionary_t * dict, const char * location);


int
bg_track_get_force_raw(const gavl_dictionary_t * dict);

int
bg_track_get_variant(const gavl_dictionary_t * dict);

const char *
bg_track_get_current_location(const gavl_dictionary_t * dict);



/** \ingroup plugin_registry
 *  \brief Identifiers for plugin APIs
 */

typedef enum
  {
    BG_PLUGIN_API_GMERLIN = 0, //!< Always 0 so native plugins can leave this empty
    BG_PLUGIN_API_LADSPA,      //!< Ladspa API
    BG_PLUGIN_API_LV,          //!< Libvisual
    BG_PLUGIN_API_FREI0R,      //!< frei0r
  } bg_plugin_api_t;

/** \ingroup plugin_registry
 *  \brief Typedef for plugin info
 */

typedef struct bg_plugin_info_s  bg_plugin_info_t;

/** \ingroup plugin_registry
 *  \brief Information about a plugin
 */

struct bg_plugin_info_s
  {
  char * gettext_domain; //!< First argument for bindtextdomain(). 
  char * gettext_directory; //!< Second argument for bindtextdomain().
  
  char * name;            //!< unique short name
  char * long_name;       //!< Humanized name

  gavl_value_t mimetypes_val;
  gavl_value_t extensions_val;
  gavl_value_t protocols_val;
  
  gavl_array_t * mimetypes;
  gavl_array_t * extensions;
  gavl_array_t * protocols;
  
  // char * mimetypes;       //!< Mimetypes, this plugin can handle
  // char * extensions;      //!< Extensions, this plugin can handle
  // char * protocols;       //!< Network protocols, this plugin can handle
  
  gavl_codec_id_t * compressions; //!< Compressions, this plugin can handle

  char * description;     //!< Description of what the plugin does

  char * module_filename; //!< Path of the shared module
  long   module_time;     //!< Modification time of the shared module, needed internally

  bg_plugin_api_t api;    //!< API of the plugin
  int index;              //!< Index inside the module. Always 0 for native plugins.
  
  bg_plugin_type_t type; //!< Plugin type
  int flags;             //!< Flags (see \ref plugin_flags)
  int priority;          //!< Priority (1..10)
  
  bg_plugin_info_t * next; //!< Used for chaining, never touch this

  bg_parameter_info_t * parameters; //!< Parameters, which can be passed to the plugin

  
  int max_audio_streams; //!< For encoders: Maximum number of audio streams (-1 means infinite)
  int max_video_streams; //!< For encoders: Maximum number of video streams (-1 means infinite)
  int max_text_streams;//!< For encoders: Maximum number of text subtitle streams (-1 means infinite)
  int max_overlay_streams;//!< For encoders: Maximum number of overlay subtitle streams (-1 means infinite)

  bg_parameter_info_t * audio_parameters; //!< Parameters, which can be passed to set_audio_parameter
  bg_parameter_info_t * video_parameters; //!< Parameters, which can be passed to set_video_parameter

  bg_parameter_info_t * text_parameters; //!< Parameters, which can be passed to set_text_parameter
  bg_parameter_info_t * overlay_parameters; //!< Parameters, which can be passed to set_overlay_parameter
  
  char * cmp_name; //!< Name used for alphabetical sorting. Not for external use.
  
  };

const char * bg_plugin_type_to_string(bg_plugin_type_t type);
bg_plugin_type_t bg_plugin_type_from_string(const char * name);


/** \ingroup plugin_registry
 *  \brief Creation options for a plugin registry
 *
 
 */

typedef struct
  {
  char ** blacklist; //!< Plugins, which should be ignored
  int dont_save;            //!< If 1, don't save the registry after it was created
  } bg_plugin_registry_options_t;

/** \ingroup plugin_registry
 *  \brief Opaque handle for a plugin registry
 *
 *  You don't want to know, what's inside here.
 */

typedef struct bg_plugin_registry_s bg_plugin_registry_t;

/** \ingroup plugin_registry
 *  \brief Typedef for plugin handle
 */

typedef struct bg_plugin_handle_s bg_plugin_handle_t;

/** \ingroup plugin_registry
 *  \brief Handle of a loaded plugin
 *
 *  When you load a plugin, the shared module will be loaded. Then, the
 *  create method of the plugin is called. The pointer obtained from the create method
 *  is stored in the priv member of the returned handle.
 */

struct bg_plugin_handle_s
  {
  /* Private members, should not be accessed! */
    
  void * dll_handle; //!< dll_handle (don't touch)
  pthread_mutex_t mutex; //!< dll_handle (don't touch, use \ref bg_plugin_lock and \ref bg_plugin_unlock)
  int refcount;          //!< Reference counter (don't touch, use \ref bg_plugin_ref and \ref bg_plugin_unref)
  
  /* These are for use by applications */
  
  const bg_plugin_common_t * plugin; //!< Common structure, cast this to the derived type (e.g. \ref bg_input_plugin_t).
  bg_plugin_common_t * plugin_nc; //!< Used for dynamic allocation. Never touch this.
  const bg_plugin_info_t * info; //!< Info about this plugin
  void * priv; //!< Private handle, passed as the first argument to most plugin functions

  //  char * location; //!< Applications can save the argument of an open call here

  gavl_dictionary_t state;
  //  bg_msg_sink_t * evt_sink; // For recording the state
  //  bg_msg_sink_t * cmd_sink; // 

  
  bg_controllable_t * ctrl_plugin; // From the plugin
  bg_control_t control;          // Internally used
  
  bg_controllable_t ctrl_ext;    // For exrernal use

  bg_media_source_t * src;
  bg_media_sink_t   * sink;
  };

/*
 *  pluginregistry.c
 */

/** \ingroup plugin_registry
 * \brief Create a plugin registry
 * \param section A configuration section
 *
 *  The configuration section will be owned exclusively
 *  by the plugin registry, applications should not touch it.
 */

void
bg_plugin_registry_create_1(bg_cfg_section_t * section);

/** \ingroup plugin_registry
 * \brief Create a plugin registry with options
 * \param section A configuration section
 * \param opt The options structure
 *
 *  The configuration section will be owned exclusively
 *  by the plugin registry, applications should not touch it.
 */

void
bg_plugin_registry_create_with_options(bg_cfg_section_t * section,
                                       const bg_plugin_registry_options_t * opt);



/** \ingroup plugin_registry
 * \brief Scan for pluggable devices
 * \param plugin_reg A plugin registry
 * \param type_mask Mask of all types you want to have the devices scanned for
 * \param flag_mask Mask of all flags you want to have the devices scanned for
 *
 *  Some plugins offer a list of supported devices as parameters. To update these
 *  (e.g. if pluggable devices are among them), call this function right after you
 *  created the plugin registry
 */

void bg_plugin_registry_scan_devices(bg_plugin_registry_t * plugin_reg,
                                     uint32_t type_mask, uint32_t flag_mask);


/** \ingroup plugin_registry
 * \brief Destroy a plugin registry
 * \param reg A plugin registry
 */

void bg_plugin_registry_destroy_1(bg_plugin_registry_t * reg);

/** \ingroup plugin_registry
 *  \brief Count plugins
 *  \param reg A plugin registry
 *  \param type_mask Mask of all types you want to have
 *  \param flag_mask Mask of all flags you want to have
 *  \returns Number of available plugins matching type_mask and flag_mask
 */

int bg_get_num_plugins(uint32_t type_mask, uint32_t flag_mask);

/** \ingroup plugin_registry
 *  \brief Find a plugin by index
 *  \param reg A plugin registry
 *  \param index Index
 *  \param type_mask Mask of all types you want to have
 *  \param flag_mask Mask of all flags you want to have
 *  \returns A plugin info or NULL
 *
 *  This function should be called after
 *  \ref bg_plugin_registry_get_num_plugins to get a particular plugin
 */

const bg_plugin_info_t *
bg_plugin_find_by_index(int index,
                        uint32_t type_mask, uint32_t flag_mask);

/** \ingroup plugin_registry
 *  \brief Find a plugin by it's unique short name
 *  \param reg A plugin registry
 *  \param name The name
 *  \returns A plugin info or NULL
 */

const bg_plugin_info_t *
bg_plugin_find_by_name(const char * name);

/** \ingroup plugin_registry
 *  \brief Find a plugin by the file extension
 *  \param reg A plugin registry
 *  \param filename The file, whose extension should match
 *  \param type_mask Mask of plugin types to be returned
 *  \returns A plugin info or NULL
 *
 *  This function returns the first plugin matching type_mask,
 *  whose extensions match filename.
 */
const bg_plugin_info_t *
bg_plugin_find_by_filename(const char * filename, int type_mask);

/** \ingroup plugin_registry
 *  \brief Find a plugin by the mime type
 *  \param reg A plugin registry
 *  \param mimetype Mimetype
 *  \param type_mask Mask of plugin types to be returned
 *  \returns A plugin info or NULL
 *
 *  This function returns the first plugin matching type_mask,
 *  whose extensions match filename.
 */
const bg_plugin_info_t *
bg_plugin_find_by_mimetype(const char * mimetype, int type_mask);



/** \ingroup plugin_registry
 *  \brief Find a plugin by the compression ID
 *  \param reg A plugin registry
 *  \param typemask Mask of plugin types to be returned
 *  \param flagmask Mask of plugin flags to be returned
 *  \returns A plugin info or NULL
 */

const bg_plugin_info_t *
bg_plugin_find_by_compression(gavl_codec_id_t id,
                              int typemask);


/** \ingroup plugin_registry
 *  \brief Find an input plugin for a network protocol
 *  \param reg A plugin registry
 *  \param protocol The network protocol (e.g. http)
 *  \returns A plugin info or NULL
 */
const bg_plugin_info_t *
bg_plugin_find_by_protocol(const char * protocol);


/* Another method: Return long names as strings (NULL terminated) */

/** \ingroup plugin_registry
 *  \brief Get a list of plugins
 *  \param reg A plugin registry
 *  \param type_mask Mask of all returned plugin types
 *  \param flag_mask Mask of all returned plugin flags
 *  \returns A NULL-terminated list of plugin names.
 *
 *  This functions returns plugin names suitable for adding to
 *  GUI menus. Use \ref bg_plugin_find_by_name to get
 *  the corresponding plugin infos.
 *
 *  Use \ref bg_plugin_registry_free_plugins to free the returned list.
 */

char ** bg_plugin_registry_get_plugins(uint32_t type_mask,
                                       uint32_t flag_mask);

/** \ingroup plugin_registry
 *  \brief Free a plugin list
 *  \param plugins List returned by \ref bg_plugin_registry_get_plugins
 */
void bg_plugin_registry_free_plugins(char ** plugins);


/*  Finally a version for finding/loading plugins */

/*
 *  info can be NULL
 *  If ret is non NULL before the call, the plugin will be unrefed
 *
 *  Return values are 0 for error, 1 on success
 *
 *  The following URL variables are supported:
 *
 *  track=n (starting with 1): Select specified track
 *  edl=[1|0]: Load EDL instead of raw file if both is available
 *
 *  Planned:
 *  seek=2.0
 *  seek=30%
 *  
 */

/** \ingroup plugin_registry
 *  \brief Load and open an input plugin
 *  \param reg A plugin registry
 *  \param location Filename or URL
 *  \param info Plugin to use (can be NULL for autodetection)
 *  \param ret Will return the plugin handle.
 *  \param callbacks Input callbacks (only for authentication)
 *  \returns 1 on success, 0 on error.
 *
 *  This is a convenience function to load an input file. If info is
 *  NULL, the plugin will be autodetected. The handle is stored in ret.
 *  If ret is non-null before the call, the old plugin will be unrefed.
 *
 */

bg_plugin_handle_t * bg_input_plugin_load(const char * location);

/** \ingroup plugin_registry
 *  \brief Load and open an input plugin with URL redirection
 *  \param reg A plugin registry
 *  \param location Filename or URL
 *  \param info Plugin to use (can be NULL for autodetection)
 *  \param ret Will return the plugin handle.
 *  \param callbacks Input callbacks (only for authentication)
 *  \param track Track index (might get changed for redirectors)
 *  \returns 1 on success, 0 on error.
 *
 *  Like \ref bg_input_plugin_load but additionally:
 *
 *  - Does URL redirection e.g. for m3u or pls files
 *  - Selects the proper track (the first or according to the "track" url variable)
 *
 */

bg_plugin_handle_t * bg_input_plugin_load_full(const char * location);

/** \ingroup plugin_registry
 *  \brief Load and open an edl decoder
 *  \param reg A plugin registry
 *  \param edl The edl to open
 *  \param info Plugin to use (can be NULL for autodetection)
 *  \param ret Will return the plugin handle.
 *  \param callbacks Input callbacks (only for authentication)
 *  \returns 1 on success, 0 on error.
 *
 *  This is a convenience function to load an input file. If info is
 *  NULL, the plugin will be autodetected. The handle is stored in ret.
 *  If ret is non-null before the call, the old plugin will be unrefed.
 */

bg_plugin_handle_t * bg_input_plugin_load_edl(const gavl_dictionary_t * edl);
bg_plugin_handle_t * bg_input_plugin_load_multi(const gavl_dictionary_t * track, bg_plugin_handle_t * h);

/*
 *  Load a track with the global plugin registry
 *  Handles:
 *
 *  - Multipart movies (GAVL_META_PARTS)
 *  - Multivariant streams (GAVL_META_VARIANTS)
 *  - Redirectors (GAVL_MEDIA_CLASS_LOCATION)
 *  - Devices
 *  ...
 *  variant is the index of the multivariat (multi bitrate-) streams.
 *  0 means first, with highest quality. Increase value after playback
 *  failed
 *  due to slow system or network.
 */

bg_plugin_handle_t *  bg_load_track(const gavl_dictionary_t * track);


void bg_tracks_resolve_locations(const gavl_value_t * src, gavl_array_t * dst, int flags);


/* Set the supported extensions and mimetypes for a plugin */

/** \ingroup plugin_registry
 *  \brief Set file extensions for a plugin
 *  \param reg A plugin registry
 *  \param plugin_name Name of the plugin
 *  \param extensions Space separated list of file extensions
 *
 *  The extensions will be saved in the plugin file
 */

void bg_plugin_registry_set_extensions(bg_plugin_registry_t * reg,
                                       const char * plugin_name,
                                       const char * extensions);

/** \ingroup plugin_registry
 *  \brief Set protocols for a plugin
 *  \param reg A plugin registry
 *  \param plugin_name Name of the plugin
 *  \param protocols Space separated list of protocols
 *
 *  The protocols will be saved in the plugin file
 */

void bg_plugin_registry_set_protocols(bg_plugin_registry_t * reg,
                                      const char * plugin_name,
                                      const char * protocols);

/** \ingroup plugin_registry
 *  \brief Set priority for a plugin
 *  \param reg A plugin registry
 *  \param plugin_name Name of the plugin
 *  \param priority Priority (BG_PLUGIN_PRIORITY_MIN..BG_PLUGIN_PRIORITY_MAX, should be 1..10)
 *
 *  The priority will be saved in the plugin file
 */

void bg_plugin_registry_set_priority(bg_plugin_registry_t * reg,
                                     const char * plugin_name,
                                     int priority);


/** \ingroup plugin_registry
 *  \brief Get the config section belonging to a plugin
 *  \param reg A plugin registry
 *  \param plugin_name Short name of the plugin
 *  \returns The config section belonging to the plugin or NULL
 */
bg_cfg_section_t *
bg_plugin_registry_get_section(bg_plugin_registry_t * reg,
                               const char * plugin_name);

/** \ingroup plugin_registry
 *  \brief Set a parameter info for selecting and configuring plugins
 *  \param reg A plugin registry
 *  \param type_mask Mask of all returned types
 *  \param flag_mask Mask of all returned flags
 *  \param ret Where the parameter info will be copied
 *
 */

void bg_plugin_registry_set_parameter_info(bg_plugin_registry_t * reg,
                                           uint32_t type_mask,
                                           uint32_t flag_mask,
                                           bg_parameter_info_t * ret);

/** \ingroup plugin_registry
 *  \brief Set a parameter info for selecting and configuring input plugins
 *  \param reg A plugin registry
 *  \param type_mask Mask of all returned types
 *  \param flag_mask Mask of all returned flags
 *  \param ret Where the parameter info will be copied
 *
 */

void bg_plugin_registry_set_parameter_info_input(bg_plugin_registry_t * reg,
                                                 uint32_t type_mask,
                                                 uint32_t flag_mask,
                                                 bg_parameter_info_t * ret);


/** \ingroup plugin_registry
 *  \brief Set a parameter of an input plugin
 *  \param data A plugin registry cast to void
 *  \param name Name
 *  \param val Value
 *
 */

void bg_plugin_registry_set_parameter_input(void * data, const char * name,
                                            const gavl_value_t * val);

int bg_plugin_registry_get_parameter_input(void * data, const char * name,
                                            gavl_value_t * val);


/** \ingroup plugin_registry
 *  \brief Create a parameter array for encoders
 *  \param reg       A plugin registry
 *  \param stream_type_mask Mask of all stream types to be encoded
 *  \param flag_mask Mask of all returned plugin flags
 *  \returns Parameter array for setting up encoders
 *
 *  Free the returned parameters with
 *  \ref bg_parameter_info_destroy_array
 *
 *  If you create a config section from the returned parameters
 *  (with \ref bg_cfg_section_create_from_parameters or \ref bg_cfg_section_create_items)
 *  the resulting encoding section will contain the complete encoder setup.
 *  It can be manipulated through the bg_encoder_section_*() functions.
 */

bg_parameter_info_t *
bg_plugin_registry_create_encoder_parameters(bg_plugin_registry_t * reg,
                                             uint32_t stream_type_mask,
                                             uint32_t flag_mask,
                                             int stream_params);

/** \ingroup plugin_registry
 *  \brief Create a parameter array for compressors
 *  \param plugin_reg A plugin registry
 *  \param flag_mask Mask of all returned plugin flags
 *  \returns Parameter array for setting up encoders
 *
 *  Free the returned parameters with
 *  \ref bg_parameter_info_destroy_array
 *
 */

// bg_parameter_info_t *
// bg_plugin_registry_create_compressor_parameters(bg_plugin_registry_t * reg,
//                                                uint32_t flag_mask);

const bg_parameter_info_t * bg_plugin_registry_get_audio_compressor_parameter();
const bg_parameter_info_t * bg_plugin_registry_get_video_compressor_parameter();
const bg_parameter_info_t * bg_plugin_registry_get_overlay_compressor_parameter();


/** \ingroup plugin_registry
 *  \brief Set a compressor parameter
 *  \param plugin_reg A plugin registry
 *  \param plugin Address of a plugin handle
 *  \param name Parameter name
 *  \param val Value
 *
 *  Call this with the parameters returned by
 *  \ref bg_plugin_registry_create_encoder_parameters and afterwards
 *  *plugin will be a codec plugin with all parameters set.
 */

void
bg_plugin_registry_set_compressor_parameter(bg_plugin_registry_t * plugin_reg,
                                            bg_plugin_handle_t ** plugin,
                                            const char * name,
                                            const gavl_value_t * val);

/** \ingroup plugin_registry
 *  \brief Get a compression ID from a compressor section
 *  \param plugin_reg A plugin registry
 *  \param section A section
 *  \returns The codec ID
 *
 *  Call this with a section corrsponding to the parameters created by
 *  \ref bg_plugin_registry_create_compressor_parameters
 */

gavl_codec_id_t
bg_plugin_registry_get_compressor_id(bg_plugin_registry_t * plugin_reg,
                                     bg_cfg_section_t * section);
  

/** \ingroup plugin_registry
 *  \brief Get the name for an encoding plugin
 *  \param plugin_reg A plugin registry
 *  \param s An encoder section (see \ref bg_plugin_registry_create_encoder_parameters)
 *  \param stream_type The stream type to encode
 *  \param stream_mask The mask passed to \ref bg_plugin_registry_create_encoder_parameters
 *  \returns Returns the plugin name or NULL if the stream will be encoded by the video encoder
 */

const char * 
bg_encoder_section_get_plugin(const bg_cfg_section_t * s,
                              gavl_stream_type_t stream_type);

/** \ingroup plugin_registry
 *  \brief Get the plugin configuration for an encoding plugin
 *  \param plugin_reg A plugin registry
 *  \param s An encoder section (see \ref bg_plugin_registry_create_encoder_parameters)
 *  \param stream_type The stream type to encode
 *  \param stream_mask The mask passed to \ref bg_plugin_registry_create_encoder_parameters
 *  \param section_ret If non-null returns the config section for the plugin
 *  \param params_ret If non-null returns the parameters for the plugin
 */

  
void
bg_encoder_section_get_plugin_config(bg_plugin_registry_t * plugin_reg,
                                     const bg_cfg_section_t * s,
                                     gavl_stream_type_t stream_type,
                                     const bg_cfg_section_t ** section_ret,
                                     const bg_parameter_info_t ** params_ret);

/** \ingroup plugin_registry
 *  \brief Get the stream configuration for an encoding plugin
 *  \param plugin_reg A plugin registry
 *  \param s An encoder section (see \ref bg_plugin_registry_create_encoder_parameters)
 *  \param stream_type The stream type to encode
 *  \param stream_mask The mask passed to \ref bg_plugin_registry_create_encoder_parameters
 *  \param section_ret If non-null returns the config section for the stream
 *  \param params_ret If non-null returns the parameters for the stream
 */

void
bg_encoder_section_get_stream_config(bg_plugin_registry_t * plugin_reg,
                                     const bg_cfg_section_t * s,
                                     gavl_stream_type_t stream_type,
                                     const bg_cfg_section_t ** section_ret,
                                     const bg_parameter_info_t ** params_ret);



/** \defgroup plugin_registry_defaults Defaults saved between sessions
 *  \ingroup plugin_registry
 *  \brief Plugin defaults
 *
 *  The registry stores a complete plugin setup for any kind of application. This includes the
 *  default plugins (see \ref bg_plugin_registry_get_default and bg_plugin_registry_set_default),
 *  their parameters,
 *  as well as flags, whether encoded streams should be multiplexed or not.
 *  It's up the the application if these informations are
 *  used or not.
 *
 *  These infos play no role inside the registry, but they are saved and reloaded
 *  between sessions. 
 *
 */



/* Rescan the available devices */


/** \ingroup plugin_registry
 *  \brief Load an image
 *  \param reg A plugin registry
 *  \param filename Image filename
 *  \param format Returns format of the image
 *  \param m Returns metadata
 *  \returns The frame, which contains the image
 *
 *  Use gavl_video_frame_destroy to free the
 *  return value
 */

gavl_video_frame_t * bg_plugin_registry_load_image(bg_plugin_registry_t * reg,
                                                   const char * filename,
                                                   gavl_video_format_t * format,
                                                   gavl_dictionary_t * m);

gavl_video_frame_t *
bg_plugin_registry_load_image_convert(bg_plugin_registry_t * r,
                                      const char * filename,
                                      const gavl_video_format_t * format,
                                      gavl_dictionary_t * m);


gavl_video_frame_t * 
bg_plugin_registry_load_cover(bg_plugin_registry_t * r,
                              gavl_video_format_t * fmt,
                              const gavl_dictionary_t * track);

gavl_video_frame_t * 
bg_plugin_registry_load_cover_full(bg_plugin_registry_t * r,
                                   gavl_video_format_t * fmt,
                                   const gavl_dictionary_t * track,
                                   int max_width, int max_height,
                                   gavl_pixelformat_t pfmt, int shrink);

gavl_video_frame_t * 
bg_plugin_registry_load_cover_cnv(bg_plugin_registry_t * r,
                                  const gavl_video_format_t * fmt,
                                  const gavl_dictionary_t * track);


void bg_plugin_registry_get_container_data(bg_plugin_registry_t * plugin_reg,
                                           gavl_dictionary_t * container,
                                           const gavl_dictionary_t * child);


/* Same as above for writing. Does implicit pixelformat conversion */

/** \ingroup plugin_registry
 *  \brief Save an image
 *  \param reg A plugin registry
 *  \param filename Image filename
 *  \param frame The frame, which contains the image
 *  \param format Returns format of the image
 *  \param m Metadata
 */

void
bg_plugin_registry_save_image(bg_plugin_registry_t * reg,
                              const char * filename,
                              gavl_video_frame_t * frame,
                              const gavl_video_format_t * format,
                              const gavl_dictionary_t * m);

#if 0
/** \brief Get thumbnail of a movie
 *  \param gml Location (should be a regular file)
 *  \param thumbnail_file If non-null, returns the filename of the thumbnail
 *  \param plugin_reg Plugin registry
 *  \param frame_ret If non-null, returns the video frame
 *  \param format_ret If non-null, the video format of the thumbnail will be
                      copied here
 *  \returns 1 if a unique thumbnail could be generated, 0 if a default thumbnail was returned
 *
 */

int bg_get_thumbnail(const char * gml,
                     bg_plugin_registry_t * plugin_reg,
                     char ** thumbnail_filename_ret,
                     gavl_video_frame_t ** frame_ret,
                     gavl_video_format_t * format_ret);
#endif

/** \brief Make a thumbnail of a video frame
 *  \param plugin_reg Plugin registry
 *  \param in_frame Input frame
 *  \param input_format Format of the input frame
 *  \param max_width Maximum width
 *  \param max_height Maximum height
 *  \param out_file_base Output filename without extension
 *  \param mimetype Mimetype of the output file
 *  \returns The filename of the generated thumbnail (to be freed by the caller) or NULL
 */

char * bg_make_thumbnail(gavl_video_frame_t * in_frame,
                         const gavl_video_format_t * input_format,
                         int * max_width, int * max_height,
                         const char * out_file_base,
                         const char * mimetype,
                         const gavl_dictionary_t * m);


/*
 *  These are the actual loading/unloading functions
 *  (loader.c)
 */

/* Load a plugin and return handle with reference count of 1 */

/** \ingroup plugin_registry
 *  \brief Load a plugin
 *  \param info The plugin info
 *  \returns The handle
 *
 *  Load a plugin and return handle with reference count of 1
 */

bg_plugin_handle_t * bg_plugin_load(const bg_plugin_info_t * info);

/** \ingroup plugin_registry
 *  \brief Load a plugin and apply custom options
 *  \param reg A plugin registry
 *  \param dict A dictionary conatining the name (BG_CFG_TAG_NAME) and options
 *  \returns The handle
 * 
 *  Load a plugin and return handle with reference count of 1
 */

bg_plugin_handle_t * bg_plugin_load_with_options(const gavl_dictionary_t * dict);


/** \ingroup plugin_registry
 *  \brief Load a video output plugin
 *  \param reg A plugin registry
 *  \param info The plugin info
 *  \param window_id The window ID or NULL
 *
 *  Load a video output plugin for embedding into an already existing window
 *  and return handle with reference count of 1
 */

bg_plugin_handle_t * bg_ov_plugin_load(const gavl_dictionary_t * options,
                                       const char * window_id);

/** \ingroup plugin_registry
 *  \brief Lock a plugin
 *  \param h A plugin handle
 */
void bg_plugin_lock(void * h);

/** \ingroup plugin_registry
 *  \brief Unlock a plugin
 *  \param h A plugin handle
 */
void bg_plugin_unlock(void * h);

/* Reference counting for input plugins */

/** \ingroup plugin_registry
 *  \brief Increase the reference count
 *  \param h A plugin handle
 */
void bg_plugin_ref(bg_plugin_handle_t * h);

/* Plugin will be unloaded when refcount is zero */

/** \ingroup plugin_registry
 *  \brief Decrease the reference count
 *  \param h A plugin handle
 *
 *  If the reference count gets zero, the plugin will
 *  be destroyed
 */
void bg_plugin_unref(bg_plugin_handle_t * h);

/** \ingroup plugin_registry
 *  \brief Decrease the reference count without locking
 *  \param h A plugin handle
 *
 *  Use this *only* if you know for sure, that the plugin is
 *  already locked and no other thread waits for the plugin to
 *  be unlocked.
 *  If the reference count gets zero, the plugin will
 *  be destroyed
 */

void bg_plugin_unref_nolock(bg_plugin_handle_t * h);

/** \ingroup plugin_registry
 *  \brief Create a plugin info from a plugin
 *  \param plugin A plugin
 *  \returns A newly allocated plugin info
 *
 *  This is used by internal plugins only.
 */

bg_plugin_info_t * bg_plugin_info_create(const bg_plugin_common_t * plugin);

/** \ingroup plugin_registry
 *  \brief Create an empty plugin handle
 *  \returns A newly allocated plugin handle
 *
 *  Use this function only if you create a plugin handle outside a plugin
 *  registry. Free the returned info with \ref bg_plugin_unref
 */

bg_plugin_handle_t * bg_plugin_handle_create();

/** \ingroup plugin_registry
 *  \brief Check if the plugin registry was changed
 *  \returns 1 if the registry was changed, 0 else
 *
 *  If this function returns 1, you should save the associated
 *  config registry also.
 */

int bg_plugin_registry_changed(bg_plugin_registry_t * reg);

/** \ingroup plugin_registry
 *  \brief Save the metadata to an xml file
 */

int bg_input_plugin_set_track(bg_plugin_handle_t * h, int track);
int bg_input_plugin_get_track(bg_plugin_handle_t * h);

void bg_input_plugin_seek(bg_plugin_handle_t * h, int64_t time, int scale);
void bg_input_plugin_start(bg_plugin_handle_t * h);

void bg_input_plugin_pause(bg_plugin_handle_t * h);
void bg_input_plugin_resume(bg_plugin_handle_t * h);


gavl_audio_source_t * bg_input_plugin_get_audio_source(bg_plugin_handle_t * h, int stream);
gavl_video_source_t * bg_input_plugin_get_video_source(bg_plugin_handle_t * h, int stream);

gavl_packet_source_t * bg_input_plugin_get_audio_packet_source(bg_plugin_handle_t * h, int stream);
gavl_packet_source_t * bg_input_plugin_get_video_packet_source(bg_plugin_handle_t * h, int stream);

gavl_packet_source_t * bg_input_plugin_get_overlay_packet_source(bg_plugin_handle_t * h, int stream);
gavl_video_source_t * bg_input_plugin_get_overlay_source(bg_plugin_handle_t * h, int stream);
  
gavl_packet_source_t * bg_input_plugin_get_text_source(bg_plugin_handle_t * h, int stream);

bg_msg_hub_t * bg_input_plugin_get_msg_hub_by_id(bg_plugin_handle_t * h, int id);



const gavl_dictionary_t * bg_input_plugin_get_edl(bg_plugin_handle_t * h);

gavl_dictionary_t * bg_input_plugin_get_media_info(bg_plugin_handle_t * h);



int bg_input_plugin_get_num_tracks(bg_plugin_handle_t * h);
gavl_dictionary_t * bg_input_plugin_get_track_info(bg_plugin_handle_t * h, int idx);
const char * bg_input_plugin_get_disk_name(bg_plugin_handle_t * h);

const gavl_dictionary_t * bg_plugin_registry_get_src(bg_plugin_registry_t * reg,
                                                     const gavl_dictionary_t * track,
                                                     int * idx_p);

void bg_plugin_registry_set_state(bg_plugin_registry_t * reg, gavl_dictionary_t * state);

gavl_dictionary_t * bg_plugin_registry_load_media_info(bg_plugin_registry_t * reg,
                                                       const char * location,
                                                       int flags);

#if 0
void bg_plugin_registry_tracks_from_locations(bg_plugin_registry_t * reg,
                                              const gavl_value_t * val,
                                              int flags,
                                              gavl_array_t * ret);
#endif

int bg_plugin_handle_set_state(bg_plugin_handle_t * h, const char * ctx, const char * name, const gavl_value_t * val);
int bg_plugin_handle_get_state(bg_plugin_handle_t * h, const char * ctx, const char * name, gavl_value_t * val);

void bg_plugin_handle_connect_control(bg_plugin_handle_t * ret);

int bg_file_is_blacklisted(const char * url);



/* Shortcuts */

void bg_ov_plugin_set_fullscreen(bg_plugin_handle_t * h, int fs);
void bg_ov_plugin_set_visible(bg_plugin_handle_t * h, int visible);
void bg_ov_plugin_set_window_title(bg_plugin_handle_t * h, const char * window_title);


void bg_plugin_registry_get_input_mimetypes(bg_plugin_registry_t * reg,
                                            gavl_array_t * ret);

void bg_plugin_registry_get_input_protocols(bg_plugin_registry_t * reg,
                                            gavl_array_t * ret);

int bg_track_is_multitrack_sibling(const gavl_dictionary_t * cur, const gavl_dictionary_t * next, int * next_idx);

void bg_track_find_subtitles(gavl_dictionary_t * track);

extern bg_plugin_registry_t * bg_plugin_reg;

void bg_plugins_init();
void bg_plugins_cleanup();

/* New interface for commandline programs */

void bg_plugin_registry_list_input(void * data, int * argc,
                                   char *** _argv, int arg);

void bg_plugin_registry_list_oa(void * data, int * argc,
                                char *** _argv, int arg);

void bg_plugin_registry_list_ov(void * data, int * argc,
                                char *** _argv, int arg);

void bg_plugin_registry_list_fa(void * data, int * argc,
                                char *** _argv, int arg);

void bg_plugin_registry_list_fv(void * data, int * argc,
                                char *** _argv, int arg);

void bg_plugin_registry_list_ra(void * data, int * argc,
                                char *** _argv, int arg);

void bg_plugin_registry_list_rv(void * data, int * argc,
                                char *** _argv, int arg);


void bg_plugin_registry_list_vis(void * data, int * argc,
                                         char *** _argv, int arg);

void bg_plugin_registry_list_plugin_parameters(void * data, int * argc,
                                               char *** _argv, int arg);

void bg_plugin_registry_list_plugins(bg_plugin_type_t type, int flags);

/* Options for choosing plugins from the commandline */

void bg_plugin_registry_opt_ip(void * data, int * argc,
                               char *** _argv, int arg);

void bg_plugin_registry_opt_oa(void * data, int * argc,
                               char *** _argv, int arg);

void bg_plugin_registry_opt_ov(void * data, int * argc,
                               char *** _argv, int arg);

void bg_plugin_registry_opt_fa(void * data, int * argc,
                               char *** _argv, int arg);

void bg_plugin_registry_opt_fv(void * data, int * argc,
                               char *** _argv, int arg);

void bg_plugin_registry_opt_ra(void * data, int * argc,
                               char *** _argv, int arg);

void bg_plugin_registry_opt_rv(void * data, int * argc,
                               char *** _argv, int arg);

void bg_plugin_registry_opt_vis(void * data, int * argc,
                                char *** _argv, int arg);

/* Store plugin options globally */
const gavl_value_t * bg_plugin_config_get(bg_plugin_type_t type);
void bg_plugin_config_set(bg_plugin_type_t type, const gavl_value_t *);
const gavl_dictionary_t * bg_plugin_config_get_section(bg_plugin_type_t type);
gavl_dictionary_t * bg_plugin_config_get_section_nc(bg_plugin_type_t type);


/* Parse a string of the format

  plugin[?param1=val1[&param2=val2]]

  and store it in the dictionary. The plugin name will be stored under the tag
  BG_CFG_TAG_NAME
    
  
*/

int bg_plugin_config_parse_single(gavl_dictionary_t * dict,
                                  const char * string);

/* Parse a string of the format

  plugin1[?param1=val1[&param2=val2]][$plugin2[?param1=val1[&param2=val2]]]

  and store the resulting dictionaries in an array
  
*/

int bg_plugin_config_parse_multi(gavl_array_t * arr,
                                 const char * str);


#define BG_PLUGIN_OPT_INPUT \
  { \
  .arg =         "-ip", \
  .help_arg    = TRS("plugin?opt1=val&opt2=val .."), \
  .help_string = TRS("Use a specific input plugin with options for opening media sources." \
                     "Use -list-input to list available plugins " \
                     "and -list-plugin-parameters <plugin> to list all supported options"), \
  .callback =    bg_plugin_registry_opt_ip, \
  }


#define BG_PLUGIN_OPT_LIST_INPUT \
  { \
  .arg =         "-list-input", \
  .help_string = TRS("List the names of the installed input plugins"), \
  .callback =    bg_plugin_registry_list_input, \
  }

#define BG_PLUGIN_OPT_LIST_OA        \
  { \
  .arg =         "-list-oa", \
  .help_string = TRS("List the names of the installed audio output plugins"), \
  .callback =    bg_plugin_registry_list_oa, \
  }

#define BG_PLUGIN_OPT_LIST_OV \
  { \
  .arg =         "-list-ov", \
  .help_string = TRS("List the names of the installed video output plugins"), \
  .callback =    bg_plugin_registry_list_ov, \
  }

#define BG_PLUGIN_OPT_LIST_FA \
  { \
  .arg =         "-list-fa", \
  .help_string = TRS("List the names of the installed audio filter plugins"), \
  .callback =    bg_plugin_registry_list_fa, \
  }

#define BG_PLUGIN_OPT_LIST_FV              \
  { \
  .arg =         "-list-fv", \
  .help_string = TRS("List the names of the installed video filter plugins"), \
  .callback =    bg_plugin_registry_list_fv, \
  }

#define BG_PLUGIN_OPT_LIST_RA                   \
  { \
  .arg =         "-list-ra", \
  .help_string = TRS("List the names of the installed audio recorder plugins"), \
  .callback =    bg_plugin_registry_list_ra, \
  }

#define BG_PLUGIN_OPT_LIST_RV              \
  { \
  .arg =         "-list-rv", \
  .help_string = TRS("List the names of the installed video recorder plugins"), \
  .callback =    bg_plugin_registry_list_rv, \
  }

#define BG_PLUGIN_OPT_LIST_VIS \
  { \
  .arg =         "-list-vis", \
  .help_string = TRS("List the names of the installed visualization plugins"), \
  .callback =    bg_plugin_registry_list_vis, \
  }

#define BG_PLUGIN_OPT_LIST_OPTIONS      \
  { \
  .arg =         "-list-plugin-parameters", \
  .help_arg    = "plugin", \
  .help_string = TRS("List the parameters supported by the plugin"),  \
  .callback =    bg_plugin_registry_list_plugin_parameters, \
  }

/* Options for choosing plugins */

#define BG_PLUGIN_OPT_OA      \
  { \
  .arg =         "-oa", \
  .help_arg    = "plugin[?param1=val1[&param2=val2...]]", \
  .help_string = TRS("Set audio output plugin. Use -list-oa to list available plugins "\
                     "and -list-plugin-parameters <plugin> to list all supported options"), \
  .callback =    bg_plugin_registry_opt_oa, \
  }

#define BG_PLUGIN_OPT_OV                    \
  { \
  .arg =         "-ov", \
  .help_arg    = "plugin[?param1=val1[&param2=val2...]]", \
  .help_string = TRS("Set video output plugin. Use -list-ov to list available plugins "\
                     "and -list-plugin-parameters <plugin> to list all supported options"), \
  .callback =    bg_plugin_registry_opt_ov, \
  }

#define BG_PLUGIN_OPT_FA      \
  { \
  .arg =         "-fa", \
  .help_arg    = "plugin[?param1=val1[&param2=val2...]]", \
  .help_string = TRS("Add an audio filter with options to the chain. This option can be "\
                     "given multiple times. Use -list-fa to list available plugins " \
                     "and -list-plugin-parameters <plugin> to list all supported options"), \
  .callback =    bg_plugin_registry_opt_fa, \
  }

#define BG_PLUGIN_OPT_FV      \
  { \
  .arg =         "-fv", \
  .help_arg    = "plugin[?param1=val1[&param2=val2...]]", \
  .help_string = TRS("Add a video filter with options to the chain. This option can be "\
                     "given multibple times. Use -list-fv to list available plugins " \
                     "and -list-plugin-parameters <plugin> to list all supported options"), \
  .callback =    bg_plugin_registry_opt_fv, \
  }

#define BG_PLUGIN_OPT_RA                        \
  { \
  .arg =         "-ra", \
  .help_arg    = "plugin[?param1=val1[&param2=val2...]]", \
  .help_string = TRS("Select audio recoder plugin. Use -list-ra to list available plugins " \
                     "and -list-plugin-parameters <plugin> to list all supported options"), \
  .callback =    bg_plugin_registry_opt_ra, \
  }

#define BG_PLUGIN_OPT_RV      \
  { \
  .arg =         "-rv", \
  .help_arg    = "plugin[?param1=val1[&param2=val2...]]", \
  .help_string = TRS("Use -list-rv to list available plugins " \
                     "and -list-plugin-parameters <plugin> to list all supported options"), \
  .callback =    bg_plugin_registry_opt_rv, \
  }

#define BG_PLUGIN_OPT_VIS     \
  { \
  .arg =         "-vis", \
  .help_arg    = "plugin[?param1=val1[&param2=val2...]]", \
  .help_string = TRS("Select a music visualization plugin" \
                     "Use -list-vis to list available plugins " \
                     "and -list-plugin-parameters <plugin> to list all supported options"), \
  .callback =    bg_plugin_registry_opt_vis, \
  }

const bg_parameter_info_t * bg_plugin_registry_get_plugin_parameter(bg_plugin_type_t type);




#endif // BG_PLUGINREGISTRY_H_INCLUDED

