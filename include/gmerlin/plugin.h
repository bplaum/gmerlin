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

#ifndef BG_PLUGIN_H_INCLUDED
#define BG_PLUGIN_H_INCLUDED

#include <gavl/gavl.h>
#include <gavl/compression.h>
#include <gavl/connectors.h>
#include <gavl/metadata.h>
#include <gavl/edl.h>

#include <gmerlin/parameter.h>
#include <gmerlin/streaminfo.h>
#include <gmerlin/accelerator.h>
#include <gmerlin/bgmsg.h>
#include <gmerlin/mediaconnector.h>

/** \defgroup plugin Plugins
 *  \brief Plugin types and associated functions
 *
 *  Gmerlin plugins are structs which contain function pointers and
 *  other data. The API looks a bit complicated, but many functions are
 *  optional, so plugins can, in prinpiple, be very simple.
 *  All plugins are based on a common struct (\ref bg_plugin_common_t),
 *  which contains an identifier for the plugin type. The bg_plugin_common_t
 *  pointer can be casted to the derived plugin types.
 *
 *  The application calls the functions in the order, in which they are
 *  defined. Some functions are mandatory from the plugin view (i.e. they
 *  must be non-null), some functions are mandatory for the application
 *  (i.e. the application must check for them and call them if they are
 *  present.
 *
 *  The configuration of the plugins works entirely through the
 *  parameter passing mechanisms (see \ref parameter). Configurable
 *  plugins only need to define get_parameters and set_parameter methods.
 *  Encoding plugins have an additional layer, which allows setting
 *  parameters individually for each stream.
 *  
 *  Events, which are caught by the plugins (e.g. song name changes or
 *  mouse clicks) are propagated through optional callbacks.
 *  These are passed from the application to the plugin with the
 *  set_callbacks function.
 *  Applications should not rely on any callback to be actually supported by
 *  a plugin. Plugins should not rely on the presence of any callback
 */

#define BG_PLUGIN_INPUT_STATE_CTX         "input"
#define BG_PLUGIN_INPUT_STATE_METADATA    "metadata"
#define BG_PLUGIN_INPUT_STATE_SEEK_WINDOW "seekwin"


/** \defgroup plugin_flags Plugin flags
 *  \ingroup plugin
 *  \brief Macros for the plugin flags
 *
 *
 *  All plugins must have at least one flag set.
 *  @{
 */

#define BG_PLUGIN_FILE             (1<<7)  //!< Plugin can do I/O from stdin or stdout ("-")
#define BG_PLUGIN_PIPE             (1<<8)  //!< Plugin can do I/O from stdin or stdout ("-")
#define BG_PLUGIN_TUNER            (1<<9)  //!< Plugin has some kind of tuner. Channels will be loaded as tracks.
#define BG_PLUGIN_FILTER_1        (1<<10)  //!< Plugin acts as a filter with one input

#define BG_PLUGIN_BROADCAST       (1<<16)  //!< Plugin can broadcasts (e.g. webstreams)
#define BG_PLUGIN_DEVPARAM        (1<<17)  //!< Plugin has pluggable devices as parameters, which must be updated regurarly
#define BG_PLUGIN_OV_STILL        (1<<18)  //!< OV plugin supports still images
#define BG_PLUGIN_GAVF_IO         (1<<19)  //!< Plugin can read/write to/from a gavf I/O handle

#define BG_PLUGIN_NEEDS_HTTP_SERVER (1<<20) //!< Plugin needs a global http server instance
#define BG_PLUGIN_NEEDS_TERMINAL    (1<<21) //!< Plugin accesses the terminal (only one plugin can to this at once)

#define BG_PLUGIN_HANDLES_OVERLAYS   (1<<23)  //!< Plugin compresses overlays

#define BG_PLUGIN_UNSUPPORTED     (1<<25)  //!< Plugin is not supported. Only for a foreign API plugins


#define BG_PLUGIN_ALL 0xFFFFFFFF //!< Mask of all possible plugin flags

/** @}
 */

#define BG_PLUGIN_API_VERSION 43

/* Include this into all plugin modules exactly once
   to let the plugin loader obtain the API version */

#define BG_GET_PLUGIN_API_VERSION \
  int get_plugin_api_version() __attribute__ ((visibility("default"))); \
  int get_plugin_api_version() { return BG_PLUGIN_API_VERSION; }

#define BG_PLUGIN_PRIORITY_MIN 1
#define BG_PLUGIN_PRIORITY_MAX 10

/** \defgroup plugin_i Media input
 *  \ingroup plugin
 *  \brief Media input
 */ 


/***************************************************
 * Plugin API
 *
 * Plugin dlls contain a symbol "the_plugin",
 * which points to one of the structures below.
 * The member functions are described below.
 *
 ***************************************************/

/*
 * Plugin types
 */

/** \ingroup plugin
 *  \brief Plugin types
 */

typedef enum
  {
    BG_PLUGIN_NONE                       = 0,      //!< None or undefined
    BG_PLUGIN_INPUT                      = (1<<0), //!< Media input
    BG_PLUGIN_OUTPUT_AUDIO               = (1<<1), //!< Audio output
    BG_PLUGIN_OUTPUT_VIDEO               = (1<<2), //!< Video output
    BG_PLUGIN_ENCODER_AUDIO              = (1<<5), //!< Encoder for audio only
    BG_PLUGIN_ENCODER_VIDEO              = (1<<6), //!< Encoder for video only
    BG_PLUGIN_ENCODER_TEXT               = (1<<7), //!< Encoder for text subtitles only
    BG_PLUGIN_ENCODER_OVERLAY            = (1<<8), //!< Encoder for overlay subtitles only
    BG_PLUGIN_ENCODER                    = (1<<9), //!< Encoder for multiple kinds of streams
    BG_PLUGIN_IMAGE_READER               = (1<<11),//!< Image reader
    BG_PLUGIN_IMAGE_WRITER               = (1<<12), //!< Image writer
    BG_PLUGIN_FILTER_AUDIO               = (1<<13), //!< Audio filter
    BG_PLUGIN_FILTER_VIDEO               = (1<<14), //!< Video filter
    BG_PLUGIN_VISUALIZATION              = (1<<15), //!< Visualization
    BG_PLUGIN_COMPRESSOR_AUDIO           = (1<<16),  //!< Audio compressor
    BG_PLUGIN_COMPRESSOR_VIDEO           = (1<<17),  //!< Video compressor
    BG_PLUGIN_DECOMPRESSOR_AUDIO         = (1<<18),  //!< Audio decompressor
    BG_PLUGIN_DECOMPRESSOR_VIDEO         = (1<<19),  //!< Video decompressor

    BG_PLUGIN_BACKEND_MDB                = (1<<20),  //!< 
    BG_PLUGIN_BACKEND_RENDERER           = (1<<21),  //!< 
    BG_PLUGIN_RESOURCE_DETECTOR          = (1<<22),  //!< 

    BG_PLUGIN_FRONTEND_MDB               = (1<<23),  //!< 
    BG_PLUGIN_FRONTEND_RENDERER          = (1<<24),  //!< 

    BG_PLUGIN_CONTROL                    = (1<<25),  //!< 

  } bg_plugin_type_t;

/** \ingroup plugin
 *  \brief Device description
 *
 *  The find_devices() function of a plugin returns
 *  a NULL terminated array of devices. It's used mainly for input plugins,
 *  which access multiple drives. For output plugins, devices are normal parameters.
 */


/* Common part */

/** \ingroup plugin
 *  \brief Typedef for base structure common to all plugins
 */

typedef struct bg_plugin_common_s bg_plugin_common_t;

/** \ingroup plugin
 *  \brief Base structure common to all plugins
 */

struct bg_plugin_common_s
  {
  char * gettext_domain; //!< First argument for bindtextdomain().
  char * gettext_directory; //!< Second argument for bindtextdomain().
  
  char             * name;       //!< Unique short name
  char             * long_name;  //!< Humanized name for GUI widgets
  bg_plugin_type_t type;  //!< Type
  int              flags;  //!< Flags (see defines)
  
  char             * description; //!< Textual description 

  /*
   *  If there might be more than one plugin for the same
   *  job, there is a priority (0..10) which is used for the
   *  decision
   */
  
  int              priority; //!< Priority (between 1 and 10).
  
  /** \brief Create the instance, return handle.
   *  \returns A private handle, which is the first argument to all subsequent functions.
   */
  
  void * (*create)();
      
  /** \brief Destroy plugin instance
   *  \param priv The handle returned by the create() method
   *
   * Destroy everything, making it ready for dlclose()
   * This function might also be called on opened plugins,
   * so the plugins should call their close()-function from
   * within the destroy method.
   */

  void (*destroy)(void* priv);

  /** \brief Get available parameters
   *  \param priv The handle returned by the create() method
   *  \returns a NULL terminated parameter array.
   *
   *  The returned array is owned (an should be freed) by the plugin.
   */

  const bg_parameter_info_t * (*get_parameters)(void * priv);

  /** \brief Set configuration parameter (optional)
   */
    
  bg_set_parameter_func_t set_parameter;

  /** \brief Get configuration parameter (optional)
   *
   *  This must only return parameters, which are changed internally
   *  by the plugins.
   */
  
  bg_get_parameter_func_t get_parameter;

  /** \brief Get the controllablle
   */
  
  bg_controllable_t * (*get_controllable)(void * priv);

  /** \brief Get supported extensions
   *  \param priv The handle returned by the create() method
   *  \returns A space separated list of extensions
   */
  const char * (*get_extensions)(void * priv);

  /** \brief Get supported protocols
   *  \param priv The handle returned by the create() method
   *  \returns A space separated list of protocols
   */
  const char * (*get_protocols)(void * priv);
  
  };


/* Input plugin */


/*************************************************
 * MEDIA INPUT
 *************************************************/

/** \ingroup plugin_i
 *  \brief Typedef for input plugin
 */

typedef struct bg_input_plugin_s bg_input_plugin_t;


/** \ingroup plugin_i
 *  \brief Input plugin
 *
 *  This is for all kinds of media inputs (files, disks, urls, etc), except recording from
 *  hardware devices (see \ref plugin_r).
 *
 *
 */

struct bg_input_plugin_s
  {
  bg_plugin_common_t common; //!< Infos and functions common to all plugin types

  /* Set preferred hardware context for allocating frames */
  void (*set_video_hw_context)(void * priv, gavl_hw_context_t * ctx);

  
  /** \brief Get supported mimetypes
   *  \param priv The handle returned by the create() method
   *  \returns A space separated list of mimetypes
   */
  const char * (*get_mimetypes)(void * priv);

  
  /** \brief Open file/url/device
   *  \param priv The handle returned by the create() method
   *  \param arg Filename, URL or device name
   *  \returns 1 on success, 0 on failure
   */
  int (*open)(void * priv, const char * arg);
  
  int (*open_io)(void * priv, gavl_io_t * io);
  
  /** \brief Get the edl (optional)
   *  \param priv The handle returned by the create() method
   *  \returns The edl if any
   */

  //  const gavl_edl_t * (*get_edl)(void * priv);
  
  gavl_dictionary_t * (*get_media_info)(void * priv);
  
  /** \brief Start decoding
   *  \param priv The handle returned by the create() method
   *  \returns 1 on success, 0 on error
   *   
   *  After this call, all remaining members of the track info returned earlier
   *  (especially audio- and video formats) must be valid.
   *
   *  From the plugins point of view, this is the last chance to return 0
   *  if something fails
   */
  
  bg_media_source_t * (*get_src)(void * priv);
  
  /** \brief Get frame table
   *  \param priv The handle returned by the create() method
   *  \param stream Stream index (starting with 0)
   *  \returns A newly allocated frame table or NULL
   *
   *  The returned frame table must be freed with
   *  \ref gavl_frame_table_destroy.
   */
  
  gavl_frame_table_t * (*get_frame_table)(void * priv, int stream);
  
  /** \brief Stop playback
   *  \param priv The handle returned by the create() method
   *  
   * This is used for plugins in bypass mode to stop playback.
   * The plugin can be started again after
   */

  void (*stop)(void * priv);
  
  /** \brief Close plugin
   *  \param priv The handle returned by the create() method
   *
   *  Close the file/device/url.
   */

  void (*close)(void * priv);
  
  };

/** \defgroup plugin_oa Audio output
 *  \ingroup plugin
 *  \brief Audio output
 */ 

/** \ingroup plugin_oa
 *  \brief Typedef for audio output plugin
 */

typedef struct bg_oa_plugin_s bg_oa_plugin_t;

/** \ingroup plugin_oa
 *  \brief Audio output plugin
 *
 *  This plugin type implements audio playback through a soundcard.
 */

struct bg_oa_plugin_s
  {
  bg_plugin_common_t common; //!< Infos and functions common to all plugin types

  /** \brief Open plugin
   *  \param priv The handle returned by the create() method
   *  \param format The format of the media source
   *
   *  The format will be changed to the nearest format, which is supported
   *  by the plugin. To convert the source format to the output format,
   *  use a \ref gavl_audio_converter_t
   */

  int (*open)(void * priv, const char * uri, gavl_audio_format_t* format);

  /** \brief Start playback
   *  \param priv The handle returned by the create() method
   *
   *  Notify the plugin, that audio playback is about to begin.
   */

  int (*start)(void * priv);
  
  /** \brief Get audio sink
   *  \param priv The handle returned by the create() method
   *  \returns Audio sink
   */
  
  gavl_audio_sink_t * (*get_sink)(void * priv);
  
  /** \brief Get the number of buffered audio samples
   *  \param priv The handle returned by the create() method
   *  \returns The number of buffered samples (both soft- and hardware)
   *  
   *  This function is used for A/V synchronization with the soundcard. If this
   *  function is NULL, software synchronization will be used
   */

  int (*get_delay)(void * priv);
  
  /** \brief Stop playback
   *  \param priv The handle returned by the create() method
   *
   * Notify the plugin, that playback will stop. Playback can be starzed again with
   * start().
   */

  void (*stop)(void * priv);
    
  /** \brief Close plugin
   *  \param priv The handle returned by the create() method
   *
   * Close the plugin. After this call, the plugin can be opened with another format
   */
  
  void (*close)(void * priv);
  };

/*******************************************
 * VIDEO OUTPUT
 *******************************************/

/* Callbacks */

/** \defgroup plugin_ov Video output
 *  \ingroup plugin
 *  \brief Video output
 */ 

/* Plugin structure */

/** \ingroup plugin_ov
 * \brief Typedef for video output plugin
 */

typedef struct bg_ov_plugin_s bg_ov_plugin_t;

/** \ingroup plugin_ov
 * \brief Video output plugin
 *
 * This handles video output and still-image display.
 * In a window based system, it will typically open a new window,
 * which is owned by the plugin.
 */

struct bg_ov_plugin_s
  {
  bg_plugin_common_t common; //!< Infos and functions common to all plugin types
  
  /** \brief Get hw context
   *  \param priv The handle returned by the create() method
   *  \returns An inititialized hardware context
   *
   *  The hardware context can be used by input plugins to allocate
   *  the frames
   *   
   */
  
  gavl_hw_context_t * (*get_hw_context)(void * priv);
  
  /** \brief Set callbacks
   *  \param priv The handle returned by the create() method
   *  \param callbacks Callback structure initialized by the caller before
   */
  
  void (*set_accel_map)(void * priv, const bg_accelerator_map_t * accel_map);
  
  /** \brief Open plugin
   *  \param priv The handle returned by the create() method
   *  \param format Video format
   *  \param src_flags The source flags (see gavl_video_source_create())
   *
   *  The format will be changed to the nearest format, which is supported
   *  by the plugin. To convert the source format to the output format,
   *  use a \ref gavl_video_converter_t
   */
  
  int  (*open)(void * priv, const char * uri, gavl_video_format_t * format, int src_flags);
  
  /** \brief Add a stream for transparent overlays
   *  \param priv The handle returned by the create() method
   *  \param format Format of the overlays
   *  \returns The sink of the overlay stream
   *
   *  It's up to the plugin, if they are realized in hardware or
   *  with a gavl_overlay_blend_context_t, but they must be there.
   *  add_overlay_stream() must be called after open()
   *
   *  An application can have more than one overlay stream. Typical
   *  is one for subtitles and one for OSD.
   */
  
  gavl_video_sink_t * (*add_overlay_stream)(void * priv, gavl_video_format_t * format);
  
  /** \brief Return a video sink
   *  \param priv The handle returned by the create() method
   *  \returns The sink for this plugin
   */
  
  gavl_video_sink_t * (*get_sink)(void * priv);
  
  /** \brief Get all events from the queue and handle them
   *  \param priv The handle returned by the create() method
   *  
   *  This function  processes and handles all events, which were
   *  received from the windowing system. It calls mouse and key-callbacks,
   *  and redisplays the image when in still mode.
   */
  
  void (*handle_events)(void * priv);
  
  /** \brief Close the plugin
   *  \param priv The handle returned by the create() method
   *
   *  Close everything so the plugin can be opened with a differtent format
   *  after.
   */
  
  void (*close)(void * priv);
  
  };

/*******************************************
 * ENCODER
 *******************************************/

/** \defgroup plugin_e Encoder
 *  \ingroup plugin
 *  \brief Encoder
 */ 

/** \ingroup plugin_ov
 * \brief Typedef for callbacks for the encoder plugin
 *
 */

typedef struct bg_encoder_callbacks_s bg_encoder_callbacks_t;

/** \ingroup plugin_ov
 * \brief Callbacks for the encoder plugin
 *
 */

struct bg_encoder_callbacks_s
  {
  
  /** \brief   Output file callback
   *  \param   data The data member of this bg_encoder_callbacks_s struct
   *  \param   filename Name of the created file
   *  \returns 1 if the file may be created, 0 else
   *
   *  This is called whenever an output file is created.
   */
  
  int (*create_output_file)(void * data, const char * filename);

  /** \brief   Temp file callback
   *  \param   data The data member of this bg_encoder_callbacks_s struct
   *  \param   filename Name of the created file
   *  \returns 1 if the file may be created, 0 else
   *
   *  This is called whenever a temporary file is created.
   */

  int (*create_temp_file)(void * data, const char * filename);
  
  void * data;//!< Application specific data passed as the first argument to all callbacks.
  };


/** \ingroup plugin_e
 *  \brief Typedef for encoder plugin
 */

typedef struct bg_encoder_plugin_s bg_encoder_plugin_t;


/** \ingroup plugin_e
 *  \brief Encoder plugin
 */

struct bg_encoder_plugin_s
  {
  bg_plugin_common_t common; //!< Infos and functions common to all plugin types
  
  int max_audio_streams;  //!< Maximum number of audio streams. -1 means infinite
  int max_video_streams;  //!< Maximum number of video streams. -1 means infinite
  int max_text_streams;//!< Maximum number of text subtitle streams. -1 means infinite
  int max_overlay_streams;//!< Maximum number of overlay subtitle streams. -1 means infinite
  
  /** \brief Set callbacks
   *  \param priv The handle returned by the create() method
   *  \param cb Callback structure
   */
  
  void (*set_callbacks)(void * priv, bg_encoder_callbacks_t * cb);

  /** \brief Query for writing compressed audio packets
   *  \param priv The handle returned by the create() method
   *  \param format Format of the source
   *  \param info Compression info
   *  \returns 1 if stream compressed format can be written, 0 else
   *  
   *  Call this function after all global parameters are set.
   */
  
  int (*writes_compressed_audio)(void * priv,
                                 const gavl_audio_format_t * format,
                                 const gavl_compression_info_t * info);
  
  /** \brief Query for writing compressed video packets
   *  \param priv The handle returned by the create() method
   *  \param format Format of the source
   *  \param info Compression info
   *  \returns 1 if stream compressed format can be written, 0 else
   *  
   *  Call this function after all global parameters are set.
   */
  
  int (*writes_compressed_video)(void * priv,
                                 const gavl_video_format_t * format,
                                 const gavl_compression_info_t * info);

  /** \brief Query for writing compressed overlay packets
   *  \param priv The handle returned by the create() method
   *  \param format Format of the source
   *  \param info Compression info
   *  \returns 1 if stream compressed format can be written, 0 else
   *  
   *  Call this function after all global parameters are set.
   */
  
  int (*writes_compressed_overlay)(void * priv,
                                   const gavl_video_format_t * format,
                                   const gavl_compression_info_t * info);
    
  /** \brief Open a file
   *  \param priv The handle returned by the create() method
   *  \param filename Name of the file to be opened (without extension!)
   *  \param metadata Metadata to be written to the file
   *
   *  The extension is added automatically by the plugin.
   *  To keep track of the written files, use the \ref bg_encoder_callbacks_t.
   */
  
  int (*open)(void * data, const char * filename,
              const gavl_dictionary_t * metadata);
 

 /** \brief Open an encoder with a gavf IO handle
   *  \param priv The handle returned by the create() method
   *  \param io IO handle
   *  \param metadata Metadata to be written to the file
   *  \param chapter_list Chapter list (optional, can be NULL)
   */

  int (*open_io)(void * data, gavl_io_t * io,
                 const gavl_dictionary_t * metadata);

  
  
  /* Return per stream parameters */

  /** \brief Get audio related parameters
   *  \param priv The handle returned by the create() method
   *  \returns NULL terminated array of parameter descriptions
   *
   *  The returned parameters are owned by the plugin and must not be freed.
   */
  
  const bg_parameter_info_t * (*get_audio_parameters)(void * priv);

  /** \brief Get video related parameters
   *  \param priv The handle returned by the create() method
   *  \returns NULL terminated array of parameter descriptions
   *
   *  The returned parameters are owned by the plugin and must not be freed.
   */

  const bg_parameter_info_t * (*get_video_parameters)(void * priv);

  /** \brief Get text subtitle related parameters
   *  \param priv The handle returned by the create() method
   *  \returns NULL terminated array of parameter descriptions
   *
   *  The returned parameters are owned by the plugin and must not be freed.
   */

  const bg_parameter_info_t * (*get_text_parameters)(void * priv);

  /** \brief Get overlay subtitle related parameters
   *  \param priv The handle returned by the create() method
   *  \returns NULL terminated array of parameter descriptions
   *
   *  The returned parameters are owned by the plugin and must not be freed.
   */

  const bg_parameter_info_t * (*get_overlay_parameters)(void * priv);
  
  /* Add streams. The formats can be changed, be sure to get the
   * final formats with get_[audio|video]_format after starting the plugin
   * Return value is the index of the added stream.
   */

  /** \brief Add an audio stream
   *  \param priv The handle returned by the create() method
   *  \param language as ISO 639-2 code (3 characters+'\\0') or NULL
   *  \param format Format of the source
   *  \returns Index of this stream (starting with 0)
   *  
   *  The format might be changed to the nearest format supported by
   *  the plugin. Use \ref get_audio_format to get the actual format
   *  needed by the plugin, after \ref start() was called.
   */
  
  int (*add_audio_stream)(void * priv, const gavl_dictionary_t * m,
                          const gavl_audio_format_t * format);

  /** \brief Add an audio stream fpr compressed writing
   *  \param priv The handle returned by the create() method
   *  \param language as ISO 639-2 code (3 characters+'\\0') or NULL
   *  \param format Format of the source
   *  \param info Compression info of the source
   *  \returns Index of this stream (starting with 0)
   *  
   *  The format might be changed to the nearest format supported by
   *  the plugin. Use \ref get_audio_format to get the actual format
   *  needed by the plugin, after \ref start() was called.
   */
  
  int (*add_audio_stream_compressed)(void * priv, const gavl_dictionary_t * m,
                                     const gavl_audio_format_t * format,
                                     const gavl_compression_info_t * info);
  
  /** \brief Add a video stream
   *  \param priv The handle returned by the create() method
   *  \param format Format of the source
   *  \returns Index of this stream (starting with 0)
   *  
   *  The format might be changed to the nearest format supported by
   *  the plugin. Use \ref get_video_format to get the actual format
   *  needed by the plugin, after \ref start() was called.
   */
  
  int (*add_video_stream)(void * priv,
                          const gavl_dictionary_t * m,
                          const gavl_video_format_t * format);

  /** \brief Add a video stream for compressed writing
   *  \param priv The handle returned by the create() method
   *  \param format Format of the source
   *  \param info Compression info of the source
   *  \returns Index of this stream (starting with 0)
   *  
   *  The format might be changed to the nearest format supported by
   *  the plugin. Use \ref get_video_format to get the actual format
   *  needed by the plugin, after \ref start() was called.
   */
  
  int (*add_video_stream_compressed)(void * priv,
                                     const gavl_dictionary_t * m,
                                     const gavl_video_format_t * format,
                                     const gavl_compression_info_t * info);
  
  /** \brief Add a text subtitle stream
   *  \param priv The handle returned by the create() method
   *  \param language as ISO 639-2 code (3 characters+'\\0') or NULL
   *  \returns Index of this stream (starting with 0)
   */
  
  int (*add_text_stream)(void * priv,
                                  const gavl_dictionary_t * m,
                                  uint32_t * timescale);
  
  /** \brief Add an overlay subtitle stream
   *  \param priv The handle returned by the create() method
   *  \param m Metadata
   *  \param format Format of the source
   *  \returns Index of this stream (starting with 0)
   *
   *  The format might be changed to the nearest format supported by
   *  the plugin. Use \ref get_subtitle_overlay_format
   *  to get the actual format
   *  needed by the plugin, after \ref start was called.
   */
  
  int (*add_overlay_stream_compressed)(void * priv,
                                       const gavl_dictionary_t * m,
                                       const gavl_video_format_t * format,
                                       const gavl_compression_info_t * ci);
  
  /** \brief Add a text subtitle stream
   *  \param priv The handle returned by the create() method
   *  \param language as ISO 639-2 code (3 characters+'\\0') or NULL
   *  \param format Format of the source
   *  \returns Index of this stream (starting with 0)
   *
   *  The format might be changed to the nearest format supported by
   *  the plugin. Use \ref get_subtitle_overlay_format
   *  to get the actual format
   *  needed by the plugin, after \ref start was called.
   */
  
  int (*add_overlay_stream)(void * priv,
                            const gavl_dictionary_t * m,
                            const gavl_video_format_t * format);
  
  /* Set parameters for the streams */

  /** \brief Set audio encoding parameter
   *  \param priv The handle returned by the create() method
   *  \param stream Stream index (starting with 0)
   *  \param name Name of the parameter
   *  \param v Value
   *
   *  Use this function with parameters obtained by
   *  \ref get_audio_parameters.
   */
  
  void (*set_audio_parameter)(void * priv, int stream, const char * name,
                              const gavl_value_t * v);

  /** \brief Set video encoding parameter
   *  \param priv The handle returned by the create() method
   *  \param stream Stream index (starting with 0)
   *  \param name Name of the parameter
   *  \param v Value
   *
   *  Use this function with parameters obtained by
   *  \ref get_video_parameters.
   */

  
  void (*set_video_parameter)(void * priv, int stream, const char * name,
                              const gavl_value_t * v);

  /** \brief Set text subtitle encoding parameter
   *  \param priv The handle returned by the create() method
   *  \param stream Stream index (starting with 0)
   *  \param name Name of the parameter
   *  \param v Value
   *
   *  Use this function with parameters obtained by
   *  \ref get_subtitle_text_parameters.
   */
  
  void (*set_text_parameter)(void * priv, int stream,
                                      const char * name,
                                      const gavl_value_t * v);

  /** \brief Set text subtitle encoding parameter
   *  \param priv The handle returned by the create() method
   *  \param stream Stream index (starting with 0)
   *  \param name Name of the parameter
   *  \param v Value
   *
   *  Use this function with parameters obtained by
   *  \ref get_subtitle_overlay_parameters.
   */
  
  void (*set_overlay_parameter)(void * priv, int stream,
                                const char * name,
                                const gavl_value_t * v);
  
  /** \brief Setup multipass video encoding.
   *  \param priv The handle returned by the create() method
   *  \param stream Stream index (starting with 0)
   *  \param pass Number of this pass (starting with 1)
   *  \param total_passes Number of total passes
   *  \param stats_file Name of a file, which can be used for multipass statistics
   *  \returns 0 if multipass transcoding is not supported and can be ommitted, 1 else
   */
  int (*set_video_pass)(void * priv, int stream, int pass, int total_passes,
                        const char * stats_file);
  
  /** \brief Set up all codecs and prepare for encoding
   *  \param priv The handle returned by the create() method
   *  \returns 0 on error, 1 on success
   *
   *  Optional function for preparing the actual encoding. Applications must
   *  check for this function and call it when available.
   */
  
  int (*start)(void * priv);
  
  /** \brief Get audio sink
   *  \param priv The handle returned by the create() method
   *  \returns The audio sink for this stream
   */
  
  gavl_audio_sink_t * (*get_audio_sink)(void * priv, int stream);
  
  /** \brief Get audio packet sink
   *  \param priv The handle returned by the create() method
   *  \returns The audio sink for this stream
   */
  
  gavl_packet_sink_t * (*get_audio_packet_sink)(void * priv, int stream);
  
  /** \brief Get video sink
   *  \param priv The handle returned by the create() method
   *  \returns The packet sink for this stream
   */
  
  gavl_video_sink_t * (*get_video_sink)(void * priv, int stream);

  /** \brief Get video sink
   *  \param priv The handle returned by the create() method
   *  \returns The packet sink for this stream
   */
  
  gavl_packet_sink_t * (*get_video_packet_sink)(void * priv, int stream);

  /** \brief Get text subtitle sink
   *  \param priv The handle returned by the create() method
   *  \returns The packet sink for this stream
   */
  
  gavl_packet_sink_t * (*get_text_sink)(void * priv, int stream);
  
  /** \brief Get overlay subtitle sink
   *  \param priv The handle returned by the create() method
   *  \returns The video sink for this stream
   */
  
  gavl_video_sink_t * (*get_overlay_sink)(void * priv, int stream);

  /** \brief Get overlay subtitle sink
   *  \param priv The handle returned by the create() method
   *  \returns The packet sink for this stream
   */
  
  gavl_packet_sink_t * (*get_overlay_packet_sink)(void * priv, int stream);
  
  /** \brief Close encoder
   *  \param priv The handle returned by the create() method
   *  \param do_delete Set this to 1 to delete all created files
   *  \returns 1 is the file was sucessfully closed, 0 else
   *
   *  After calling this function, the plugin should be destroyed.
   */
  
  int (*close)(void * data, int do_delete);
  };

/** \defgroup plugin_ir Image support
 *  \ingroup plugin
 *  \brief Read and write image files
 *
 *  These support reading and writing of images in a variety
 *  of formats
 *
 *  @{
 */

/** \brief Typedef for image reader plugin
 */

typedef struct bg_image_reader_plugin_s bg_image_reader_plugin_t;

/** \brief Image reader plugin
 */

struct bg_image_reader_plugin_s
  {
  bg_plugin_common_t common; //!< Infos and functions common to all plugin types
  const char * mimetypes;  //!< Supported mimetypes
  
  /** \brief Read the file header
   *  \param priv The handle returned by the create() method
   *  \param filename Filename
   *  \param format Returns the format of the image
   *  \returns 1 on success, 0 on error.
   */
  
  int (*read_header)(void * priv, const char * filename,
                     gavl_video_format_t * format);

  /** \brief Get metadata
   *  \param priv The handle returned by the create() method
   *  \returns Metadata for the image or NULL
   */
  
  const gavl_dictionary_t * (*get_metadata)(void * priv);

  /** \brief Get compression info
   *  \param priv The handle returned by the create() method
   *  \param ci Returns the compression info
   *  \returns 1 if the compression info could be returned, 0 else
   */
  
  int (*get_compression_info)(void * priv, gavl_compression_info_t * ci);
  
  /** \brief Read the image
   *  \param priv The handle returned by the create() method
   *  \param frame The frame, where the image will be copied
   *  \returns 1 if the image was read, 0 else
   *  
   *  After reading the image the plugin is cleaned up, so \ref read_header()
   *  can be called again after that. If frame is NULL, no image is read,
   *  and the plugin is reset.
   */
  int (*read_image)(void * priv, gavl_video_frame_t * frame);
  };

/**  
 * \brief Typedef for callbacks for the image writer plugin
 */

typedef struct bg_iw_callbacks_s bg_iw_callbacks_t;

/**  
 * \brief Callbacks for the image writer plugin
 */

struct bg_iw_callbacks_s
  {
  
  /** \brief   Output file callback
   *  \param   data The data member of this bg_ov_callbacks_s struct
   *  \param   filename Name of the created file
   *  \returns 1 if the file may be created, 0 else
   *
   *  This is called whenever an output file is created.
   */
  
  int (*create_output_file)(void * data, const char * filename);
  
  void * data;//!< Application specific data passed as the first argument to all callbacks.
  };

/** \brief Typedef for image writer plugin
 *
 */

typedef struct bg_image_writer_plugin_s bg_image_writer_plugin_t;

/** \brief Image writer plugin
 *
 */

struct bg_image_writer_plugin_s
  {
  bg_plugin_common_t common; //!< Infos and functions common to all plugin types
  const char * mimetypes;  //!< Supported mimetypes

  /** \brief Set callbacks
   *  \param priv The handle returned by the create() method
   *  \param cb Callback structure
   */
  
  void (*set_callbacks)(void * priv, bg_iw_callbacks_t * cb);
  
  /** \brief Write the file header
   *  \param priv The handle returned by the create() method
   *  \param format Video format
   *  \returns 1 on success, 0 on error.
   *
   *  The format will be changed to the nearest format, which is supported
   *  by the plugin. To convert the source format to the output format,
   *  use a \ref gavl_video_converter_t
   */
  
  int (*write_header)(void * priv, const char * filename,
                      gavl_video_format_t * format, const gavl_dictionary_t * m);
  
  /** \brief Write the image
   *  \param priv The handle returned by the create() method
   *  \param frame The frame containing the image
   *  \returns 1 on success, 0 on error.
   *  
   *  After writing the image the plugin is cleaned up, so \ref write_header()
   *  can be called again after that. If frame is NULL, no image is read,
   *  and the plugin is reset.
   */

  int (*write_image)(void * priv, gavl_video_frame_t * frame);
  } ;

/**
 *  @}
 */


/** \defgroup plugin_filter A/V Filters
 *  \ingroup plugin
 *  \brief A/V Filters
 
 *  These can apply additional effects to uncomporessed A/V data.
 *  The API follows an asynchronous pull approach: You pass a callback
 *  for reading input frames. Then you call the read method to read one
 *  output frame. The plugin will read input data via the callback.
 *
 *  This mechanism allows filters, where the numbers of input frames/samples
 *  differs from the numbers of output frames/samples (e.g. when the filter
 *  does framerate conversion).
 *
 *  In principle, the API also supports filters with multiple input ports,
 *  but currently, only plugins with one input are available.
 *
 *  Not all filters support all formats. Applications should build filter chains
 *  with format converters between them. There are, however, some standard formats,
 *  which are supported by almost all filters, so the overall conversion overhead
 *  is usually zero in real-life situations.
 *
 *  @{
 */


/* Filters */

/** \brief Typedef for audio filter plugin
 *
 */

typedef struct bg_fa_plugin_s bg_fa_plugin_t;

/** \brief Audio filter plugin
 *
 */

struct bg_fa_plugin_s
  {
  bg_plugin_common_t common; //!< Infos and functions common to all plugin types

  /** \brief Reset
   *  \param priv The handle returned by the create() method
   *
   *  Optional, resets internal state, as if no frame has been processed before.
   */

  void (*reset)(void * priv);

  /** \brief Connect sources
   *  \param priv The handle returned by the create() method
   *  \param src Video source where this filter gets it's frames from
   *  \returns 1 The source to be passed to the subsequent filter
   *
   *  This can be implemented as a replacement for \ref connect_input_port,
   *  \ref set_input_format and \ref get_output_format \ref read_video.
   */
  
  gavl_audio_source_t * (*connect)(void * priv, gavl_audio_source_t *,
                                   const gavl_audio_options_t * opt);
  
  /** \brief Report, if the plugin must be reinitialized
   *  \param priv The handle returned by the create() method
   *  \returns 1 if the plugin must be reinitialized, 0 else
   *
   *  Optional, must be called after set_parameter() to check, if the
   *  filter must be reinitialized. Note, that the input and output formats can
   *  be changed in this case as well.
   */
  
  int (*need_restart)(void * priv);

  };

/** \brief Typedef for video filter plugin
 *
 */

typedef struct bg_fv_plugin_s bg_fv_plugin_t;

/** \brief Video filter plugin
 *
 */

struct bg_fv_plugin_s
  {
  bg_plugin_common_t common; //!< Infos and functions common to all plugin types

  /** \brief Reset
   *  \param priv The handle returned by the create() method
   *
   *  Optional, resets internal state, as if no frame has been processed before.
   */

  void (*reset)(void * priv);

  /** \brief Report, if the plugin must be reinitialized
   *  \param priv The handle returned by the create() method
   *  \returns 1 if the plugin must be reinitialized, 0 else
   *
   *  Optional, must be called after set_parameter() to check, if the
   *  filter must be reinitialized. Note, that the input and output formats can
   *  be changed in this case as well.
   */
  
  int (*need_restart)(void * priv);

  /** \brief Connect sources
   *  \param priv The handle returned by the create() method
   *  \param src Video source where this filter gets it's frames from
   *  \param opt gavl Options for converting and filtering
   *  \returns The source to be passed to the subsequent filter
   */
  
  gavl_video_source_t * (*connect)(void * priv,
                                   gavl_video_source_t * src,
                                   const gavl_video_options_t * opt);
    
  };


/**
 *  @}
 */


/** \defgroup plugin_visualization Audio Visualization plugins
 *  \ingroup plugin
 *  \brief Audio Visualization plugins
 *
 *
 *
 *  @{
 */

/** \brief Typedef for audio visualization plugin
 *
 */

typedef struct bg_visualization_plugin_s bg_visualization_plugin_t;


/** \brief Audio visualization plugin
 *
 *  These plugins get audio samples and run visualizations of them.
 *  Output can be either into a \ref gavl_video_frame_t or directly
 *  via OpenGL. Which method is used is denoted by the
 *  \ref BG_PLUGIN_VISUALIZE_FRAME and \ref BG_PLUGIN_VISUALIZE_GL
 *  flags.
 *
 *  For OpenGL, you need to pass a window ID to the
 *  plugin. The plugin is then responsible for creating Subwindows and
 *  setting up an OpenGL context. In General, it's stronly recommended to
 *  use the \ref bg_visualizer_t module to use visualizations.
 */

struct bg_visualization_plugin_s
  {
  bg_plugin_common_t common; //!< Infos and functions common to all plugin types
  
  /** \brief Open a frame based visualization plugin
   *  \param priv The handle returned by the create() method
   *  \param audio_format Audio format
   *  \param video_format Video format
   *
   *  The audio format parameter will most likely changed to the
   *  nearest supported format. In the video format parameter, you
   *  usually pass the desired render size. Everything else
   *  (except the framerate) will be set up by the plugin.
   */
  
  int (*open)(void * priv, gavl_audio_format_t * audio_format,
              gavl_video_format_t * video_format);
  
  gavl_audio_sink_t * (*get_sink)(void * priv);


  gavl_video_source_t * (*get_source)(void * priv);
  
  /** \brief Close a plugin
   *  \param priv The handle returned by the create() method
   */
  
  void (*close)(void * priv);
  
  };

/*
 *  Generic plugin for external or iternal modules, which communicate
 *  entirely via the controllable
 */

typedef struct 
  {
  bg_plugin_common_t common; //!< Infos and functions common to all plugin types

  /* Update the internal state, send messages. A zero return value incicates that
     nothing important happened and the client can savely sleep (e.g. for some 10s of
     milliseconds) before calling this function again. */
  
  int (*update)(void * priv);
  
  } bg_controllable_plugin_t;

typedef struct 
  {
  bg_plugin_common_t common; //!< Infos and functions common to all plugin types
  const char * protocol;
  
  /* Update the internal state, send messages. A zero return value incicates that
     nothing important happened and the client can savely sleep (e.g. for some 10s of
     milliseconds) before calling this function again. */
  
  int (*update)(void * priv);

  int (*open)(void * priv, const char * addr);

  } bg_backend_plugin_t;

typedef struct 
  {
  bg_plugin_common_t common; //!< Infos and functions common to all plugin types
  const char * protocols;
  
  /* Update the internal state, send messages. A zero return value incicates that
     nothing important happened and the client can savely sleep (e.g. for some 10s of
     milliseconds) before calling this function again. */
  
  int (*update)(void * priv);
  int (*open)(void * priv, const char * addr);
  void (*get_controls)(void * priv, gavl_dictionary_t * parent);
  
  } bg_control_plugin_t;


typedef struct 
  {
  bg_plugin_common_t common; //!< Infos and functions common to all plugin types
  
  /* Update the internal state, send messages. A zero return value incicates that
     nothing important happened and the client can savely sleep (e.g. for some 10s of
     milliseconds) before calling this function again. */
  
  int (*update)(void * priv);
  int (*open)(void * priv, bg_controllable_t * ctrl);

  //  int (*handle_message)(void * priv, gavl_msg_t * msg);
  
  } bg_frontend_plugin_t;

/** \ingroup plugin_codec
 *  \brief typedef for codec plugin
 *
 */

typedef struct bg_codec_plugin_s bg_codec_plugin_t;


/** \brief Codec plugin
 *
 *  Transform audio frames into compressed packets or back
 */

struct bg_codec_plugin_s
  {
  bg_plugin_common_t common; //!< Infos and functions common to all plugin types

  /** \brief Get supported compressions
   *  \param priv The handle returned by the create() method
   *  \returns A list of compressions terminated with GAVL_COMPRESSION_NONE
   */
  
  const gavl_codec_id_t * (*get_compressions)(void * priv);

  /** \brief Get supported codec tags
   *  \param priv The handle returned by the create() method
   *  \returns A list of codec tags terminated with GAVL_COMPRESSION_NONE
   *
   *  Codec tags are used with the compression ID GAVL_COMPRESSIO_EXTENDED
   *  for having any compression technique.
   */
  
  const uint32_t * (*get_codec_tags)(void * priv);
  
  /** \brief Connect audio encoder
   *  \param priv The handle returned by the create() method
   *  \param ci Compression info (must be freed by the caller)
   *  \param fmt Format of the source
   *  \param m Stream metadata (might get changed by the call)
   *  \returns An audio sink for sending uncompressed frames
   */
  
  gavl_audio_sink_t * (*open_encode_audio)(void * priv,
                                           gavl_dictionary_t * stream);
  
  /** \brief Connect video encoder
   *  \param priv The handle returned by the create() method
   *  \param ci Compression info (must be freed by the caller)
   *  \param fmt Format of the source
   *  \param m Stream metadata (might get changed by the call)
   *  \returns A video sink for sending uncompressed frames
   */
  
  gavl_video_sink_t * (*open_encode_video)(void * priv,
                                           gavl_dictionary_t * stream);

  /** \brief Connect overlay encoder
   *  \param priv The handle returned by the create() method
   *  \param ci Compression info (must be freed by the caller)
   *  \param fmt Format of the source
   *  \param m Stream metadata (might get changed by the call)
   *  \returns A video sink for sending uncompressed frames
   */
  
  gavl_video_sink_t * (*open_encode_overlay)(void * priv,
                                             gavl_dictionary_t * stream);
  
  /** \brief Set a packet sink
   *  \param priv The handle returned by the create() method
   *  \param sink A sink where the encoder can send completed packets
   */
  
  void (*set_packet_sink)(void * priv, gavl_packet_sink_t * s);
  
  /** \brief Connect audio decoder
   *  \param priv The handle returned by the create() method
   *  \param sink Source where get the packets
   *  \param fmt Format from the container (possibly incomplete)
   *  \param m Stream metadata (might get changed by the call)
   *  \returns An audio source for reading uncompressed frames
   */
  
  gavl_audio_source_t * (*open_decode_audio)(void * priv,
                                             gavl_packet_source_t * src,
                                             gavl_dictionary_t * stream);
  
  /** \brief Connect video decoder
   *  \param priv The handle returned by the create() method
   *  \param sink Source where get the packets
   *  \param fmt Format from the container (possibly incomplete)
   *  \param m Stream metadata (might get changed by the call)
   *  \returns A video source for reading uncompressed frames
   */
  
  gavl_video_source_t * (*open_decode_video)(void * priv,
                                             gavl_packet_source_t * src,
                                             gavl_dictionary_t * stream);
 
  /** \brief Connect overlay decoder
   *  \param priv The handle returned by the create() method
   *  \param sink Source where get the packets
   *  \param fmt Format from the container (possibly incomplete)
   *  \param m Stream metadata (might get changed by the call)
   *  \returns A video source for reading uncompressed overlays
   */

  gavl_video_source_t * (*open_decode_overlay)(void * priv,
                                               gavl_packet_source_t * src,
                                               gavl_dictionary_t * stream);

 
  /** \brief Reset a decoder
   *  \param priv The handle returned by the create() method
   *
   *  Call this after seeking
   */
  
  void (*reset)(void * priv);

  /** \brief Skip to a time
   *  \param priv The handle returned by the create() method
   *  \param t Time to skip to
   *  \returns Actual time of the next frame/sample
   */

  int64_t (*skip)(void * priv, int64_t t, int scale);

  /** \brief Set pass for an encoder
   *  \param priv The handle returned by the create() method
   *  \param pass Number of this pass (starting with 1)
   *  \param total_passes Number of total passes
   *  \param stats_file Name of a file, which can be used for multipass statistics
   *  \returns 0 if multipass transcoding is not supported and can be ommitted, 1 else
   */
  int (*set_pass)(void * priv, int pass, int total_passes,
                  const char * stats_file);
  
  };
  
/**
 *  @}
 */

#endif // BG_PLUGIN_H_INCLUDED

