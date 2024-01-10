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

#ifndef BG_LOG_H_INCLUDED
#define BG_LOG_H_INCLUDED

/* Gmerlin log facilities */

#include <gmerlin/parameter.h>
#include <gmerlin/bgmsg.h>
#include <gavl/log.h>

#include <libintl.h>

/** \defgroup log Logging
 *  \brief Global logging facilities
 *
 *  The logging mechanism is used for everything,
 *  which would be done with fprintf(stderr, ...) in
 *  simpler applications. It is the only mechanism, which
 *  used global variables, i.e. the functions \ref gavl_log_add_dest
 *  and \ref gavl_log_set_verbose. They should be called just once during
 *  application startup. The function \ref gavl_log can, of course, be
 *  called from multiple threads simultaneously.
 */



/** \ingroup log
 *  \brief Add the log destination
 *  \param q Message sink
 *
 *  This adds a sink to the global message hub.
 *  The format of the logging messages is simple: The message id is equal
 *  to the log level (see \ref bg_msg_get_id).
 *  The first two arguments are strings for the domain and the actual message
 *  respectively (see \ref bg_msg_get_arg_string).
 *
 *  Note, that logging will become asynchronous with this method. Also, single threaded
 *  applications always must remember to handle messages from the log queue
 *  after they did something critical.
 */

void bg_log_add_dest(bg_msg_sink_t * q);

void bg_log_remove_dest(bg_msg_sink_t * q);


/** \ingroup log
 *  \brief Set the log destination
 *  \param q Message sink
 *
 *  This sets a global message queue to which log messages will be sent.
 *  The format of the logging messages is simple: The message id is equal
 *  to the log level (see \ref bg_msg_get_id).
 *  The first two arguments are strings for the domain and the actual message
 *  respectively (see \ref bg_msg_get_arg_string).
 *
 *  Note, that logging will become asynchronous with this method. Also, single threaded
 *  applications always must remember to handle messages from the log queue
 *  after they did something critical.
 */


/** \ingroup log
 *  \brief Convert a log level to a human readable string
 *  \param level Log level
 *  \returns A human readable string describing the log level
 */

// const char * gavl_log_level_to_string(gavl_log_level_t level);

/** \ingroup log
 *  \brief Set verbosity mask
 *  \param mask ORed log levels, which should be printed
 *
 *  Note, that this function is not thread save and has no effect
 *  if logging is done with a message queue.
 */

// void gavl_log_set_verbose(int mask);

/** \ingroup log
 *  \brief Set verbosity level
 *  \param level Value between 0 and 4
 *
 *  0: Disable logging
 *  1: Errors
 *  2: Errors+Warnings
 *  3: Errors+Warnings+Infos
 *  4: Errors+Warnings+Infos+Debug
 */

// void gavl_log_set_verbose_level(int level);

// int gavl_log_get_verbose_level();


/** \ingroup log
 *  \brief Get last error message
 *  \returns The last error message, must be freed
 *
 *  Use this only if you didn't set an log destination and you
 *  can make sure, that only your thread can trigger an error.
 */

// char * gavl_log_last_error();

/** \ingroup log
 *  \brief Initialize syslog logging
 *  \param name Application name
 */

void bg_log_syslog_init(const char * name);

/** \ingroup log
 *  \brief Return the syslog name
 *  \returns The name passed to \ref gavl_log_syslog_init or NULL
 */

const char * bg_log_syslog_name();

void bg_log_stderr_init();

void bg_log_cleanup();

/** \ingroup log
 *  Flush the log queue, must be done periodically by the
 *  application
 */

// void gavl_log_syslog_flush();



#endif // BG_LOG_H_INCLUDED

