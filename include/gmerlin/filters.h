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

#ifndef BG_FILTERS_H_INCLUDED
#define BG_FILTERS_H_INCLUDED

#include <gavl/gavl.h>
#include <gmerlin/bggavl.h>
#include <gmerlin/plugin.h>



/** \defgroup filter_chain Filter chains
 * \brief Chains of A/V filters
 *
 * @{
 */

#define BG_FILTER_CHAIN_PARAM_PLUGINS "f"

/** \brief Audio filter chain
 *
 *  Opaque handle for an audio filter chain. You don't want to know,
 *  what's inside.
 */

typedef struct bg_audio_filter_chain_s bg_audio_filter_chain_t;

/** \brief Video filter chain
 *
 *  Opaque handle for a video filter chain. You don't want to know,
 *  what's inside.
 */

typedef struct bg_video_filter_chain_s bg_video_filter_chain_t;

/* Audio */

/** \brief Create an audio filter chain
 *  \param opt Conversion options
 *  \param plugin_reg A plugin registry
 *
 *  The conversion options should be valid for the whole lifetime of the filter chain.
 */

bg_audio_filter_chain_t *
bg_audio_filter_chain_create(const bg_gavl_audio_options_t * opt,
                             bg_plugin_registry_t * plugin_reg);

/** \brief Return parameters
 *  \param ch An audio filter chain
 *  \returns A NULL terminated array of parameter descriptions
 *
 *  Usually, there will be a parameter of type BG_PARAMETER_MULTI_CHAIN,  which includes
 *  all installed filters and their respective parameters.
 */

const bg_parameter_info_t *
bg_audio_filter_chain_get_parameters(bg_audio_filter_chain_t * ch);

/** \brief Set a parameter for an audio chain
 *  \param data An audio filter chain as void*
 *  \param name Name
 *  \param val Value
 *
 *  In some cases the filter chain must be rebuilt after setting a parameter.
 *  The application should therefore call \ref bg_audio_filter_chain_need_restart
 *  and call \ref bg_audio_filter_chain_init if necessary.
 */

void bg_audio_filter_chain_set_parameter(void * data,
                                         const char * name,
                                         const gavl_value_t * val);

/** \brief Check if an audio filter chain needs to be restarted
 *  \param ch An audio filter chain
 *  \returns 1 if the chain must be restarted, 0 else
 *
 *  If this returns true, you should call \ref bg_audio_filter_chain_init.
 *  It's usually used after
 *  \ref bg_audio_filter_chain_set_parameter.
 */

int bg_audio_filter_chain_need_restart(bg_audio_filter_chain_t * ch);



/** \brief Destroy an audio filter chain
 *  \param ch An audio filter chain
 */

void bg_audio_filter_chain_destroy(void * ch);

/** \brief Lock an audio filter chain
 *  \param ch An audio filter chain
 *
 *  In multithreaded enviroments, you must lock the chain for calls to
 *  \ref bg_audio_filter_chain_set_parameter and \ref bg_audio_filter_chain_read.
 */

void bg_audio_filter_chain_lock(void * ch);

/** \brief Unlock an audio filter chain
 *  \param ch An audio filter chain
 *
 *  In multithreaded enviroments, you must lock the chain for calls to
 *  \ref bg_audio_filter_chain_set_parameter and \ref bg_audio_filter_chain_read.
 */

void bg_audio_filter_chain_unlock(void * ch);

/** \brief Reset an audio filter chain
 *  \param ch An audio filter chain
 *  
 *  Set the internal state as if no sample has been processed since last init
 */

void bg_audio_filter_chain_reset(bg_audio_filter_chain_t * ch);

/** \brief Connect using audio sources
 *  \param ch A audio filter chain
 *  \param src Audio source to get frames from
 *  \returns Audio source for reading frames
 *
 *  This is a replacement for \ref bg_audio_filter_chain_connect_input,
 *  \ref bg_audio_filter_chain_init and
 *  \ref bg_audio_filter_chain_set_out_format
 */

gavl_audio_source_t *
bg_audio_filter_chain_connect(bg_audio_filter_chain_t * ch,
                              gavl_audio_source_t * src);

bg_msg_sink_t * bg_audio_filter_chain_get_cmd_sink(bg_audio_filter_chain_t * ch);

/* Video */

/** \brief Create a video filter chain
 *  \param opt Conversion options
 *  \param plugin_reg A plugin registry
 *
 *  The conversion options should be valid for the whole lifetime of the filter chain.
 */

bg_video_filter_chain_t *
bg_video_filter_chain_create(const bg_gavl_video_options_t * opt,
                             bg_plugin_registry_t * plugin_reg);

/** \brief Return parameters
 *  \param ch A video filter chain
 *  \returns A NULL terminated array of parameter descriptions
 *
 *  Usually, there will be a parameter of type BG_PARAMETER_MULTI_CHAIN,  which includes
 *  all installed filters and their respective parameters.
 */

const bg_parameter_info_t *
bg_video_filter_chain_get_parameters(bg_video_filter_chain_t * ch);

/** \brief Set a parameter for a video chain
 *  \param data A video converter as void*
 *  \param name Name
 *  \param val Value
 *
 *  In some cases the filter chain must be rebuilt after setting a parameter.
 *  The application should therefore call
 *  \ref bg_video_filter_chain_need_restart
 *  and call \ref bg_video_filter_chain_init if necessary.
 */

void bg_video_filter_chain_set_parameter(void * data, const char * name,
                                         const gavl_value_t * val);

/** \brief Check if a video filter chain needs to be restarted
 *  \param ch A video filter chain
 *  \returns 1 if the chain must be restarted, 0 else
 *
 *  If this returns true, you should call \ref bg_video_filter_chain_init.
 *  It's usually used after
 *  \ref bg_video_filter_chain_set_parameter.
 */

int bg_video_filter_chain_need_restart(bg_video_filter_chain_t * ch);

/** \brief Connect using video sources
 *  \param ch A video filter chain
 *  \param src Video source to get frames from
 *  \returns Video source for reading frames
 *
 *  This is a replacement for \ref bg_video_filter_chain_connect_input,
 *  \ref bg_video_filter_chain_init and
 *  \ref bg_video_filter_chain_set_out_format
 */

gavl_video_source_t *
bg_video_filter_chain_connect(bg_video_filter_chain_t * ch,
                              gavl_video_source_t * src);

/** \brief Destroy a video filter chain
 *  \param ch A video filter chain
 */

void bg_video_filter_chain_destroy(void * ch);

/** \brief Lock a video filter chain
 *  \param ch A video filter chain
 *
 *  In multithreaded enviroments, you must lock the chain for calls to
 *  \ref bg_video_filter_chain_set_parameter and \ref bg_video_filter_chain_read.
 */

void bg_video_filter_chain_lock(void * ch);

/** \brief Unlock a video filter chain
 *  \param ch A video filter chain
 *
 *  In multithreaded enviroments, you must lock the chain for calls to
 *  \ref bg_video_filter_chain_set_parameter and \ref bg_video_filter_chain_read.
 */

void bg_video_filter_chain_unlock(void * ch);

/** \brief Reset a video filter chain
 *  \param ch A video filter chain
 *  
 *  Set the internal state as if no sample has been processed since last init
 */

void bg_video_filter_chain_reset(bg_video_filter_chain_t * ch);

bg_msg_sink_t * bg_video_filter_chain_get_cmd_sink(bg_video_filter_chain_t * ch);

/**
 * @}
 */

#endif // BG_FILTERS_H_INCLUDED

