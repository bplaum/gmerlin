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

#ifndef BG_CMDLINE_H_INCLUDED
#define BG_CMDLINE_H_INCLUDED

#include <gavl/metadata.h>
#include <gmerlin/parameter.h>
#include <gmerlin/cfg_registry.h>
#include <gmerlin/cfgctx.h>

/* Automatic generation of user documentation */

extern gavl_dictionary_t bg_cmdline_options;

    
/*
 *  Remove the nth arg from an argc/argv pair
 */

void bg_cmdline_remove_arg(int * argc, char *** argv, int arg);

/* cmdline options */


/* Simple string: "-opt val" is stored as opt = val int the global dict */
#define BG_CMDLINE_ARG_STRING     (1<<0) 
#define BG_CMDLINE_ARG_PARAM      (1<<2)
#define BG_CMDLINE_ARG_PER_STREAM (1<<1)

/* Get concatenated options (global then per stream) */
void bg_cmdline_get_stream_params(const char * name,
                                   int idx, gavl_array_t * ret);

/* Get options array for writing */
gavl_array_t * bg_cmdline_get_stream_params_wr(const char * name,
                                                int idx);

/* Get concatenated options (global then per stream) */
const gavl_array_t * bg_cmdline_get_params(const char * name);

/* Get options array for writing */
gavl_array_t * bg_cmdline_get_params_wr(const char * name);

int bg_cmdline_apply_params(gavl_dictionary_t * dst,
                            const gavl_parameter_info_t * info,
                            const gavl_array_t * arr);



/*
  Per stream options can be:

  Applied to all streams:
  
  -af "plugin=eq:gain=1"
  -af "plugin=comp:ratio=3"

  -vp default_timescale=25
  -vp default_frame_duration=1

  -ap "codec=mp3:bitrate=128"
  
  
  Applied only to first stream:

  -af-0 plugin="eq:gain=1"

  -af plugin="eq:gain=1"
  
  
*/



typedef struct
  {
  char * arg;
  char * help_arg; /* Something like <file> */
  char * help_string;
  
  /* Callback will be called if present */
  void (*callback)(void * data, int * argc, char *** argv, int arg);
  
  int flags;
  
  } bg_cmdline_arg_t;

typedef struct
  {
  char             * name;
  bg_cmdline_arg_t * args;
  } bg_cmdline_arg_array_t;

typedef struct
  {
  char * name;
  char * desc;
  } bg_cmdline_ext_doc_t;

/* Static data for commandline interface. This
   is important for generating the manual page */

typedef struct
  {
  char * package;
  char * version;

  char * synopsis;
  char * help_before;
  const bg_cmdline_arg_array_t * args; /* Null terminated */
  
  const bg_cmdline_ext_doc_t * env;
  const bg_cmdline_ext_doc_t * files;
  
  char * help_after;
  } bg_cmdline_app_data_t;

void bg_cmdline_init(const bg_cmdline_app_data_t * app_data);

void bg_cmdline_parse(bg_cmdline_arg_t *, int * argc, char *** argv);

char ** bg_cmdline_get_locations_from_args(int * argc, char *** argv);

int bg_cmdline_check_unsupported(int argc, char ** argv);

void bg_cmdline_print_help(char * argv0);

/* Commandline -> Config registry and parameters */


void bg_cmdline_print_help_parameters(const bg_parameter_info_t * parameters);


void bg_cmdline_print_version(const char * application);


#endif // BG_CMDLINE_H_INCLUDED

