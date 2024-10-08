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

typedef enum
  {
    BG_HELP_FORMAT_PLAIN,
    BG_HELP_FORMAT_TERM,
    BG_HELP_FORMAT_MAN,
    BG_HELP_FORMAT_TEXI,
  } bg_help_format_t;

    
/*
 *  Remove the nth arg from an argc/argv pair
 */

void bg_cmdline_remove_arg(int * argc, char *** argv, int arg);

/* cmdline options */

typedef struct
  {
  char * arg;
  char * help_arg; /* Something like <file> */
  char * help_string;
  
  /* Callback will be called if present */
  void (*callback)(void * data, int * argc, char *** argv, int arg);

  /* Callback with index will be called if present */
  void (*callback_idx)(void * data, int idx, int * argc, char *** argv, int arg);

  /* Parameters are used to generate a help string */
  const bg_parameter_info_t * parameters;

  /* If non NULL, the argv will be placed here */
  char ** argv;

  /* If non NULL, the parameters are stored here */
  bg_cfg_section_t * s;

  /* Option refers to a plugin. It will generate a generic help string */
  int plugin_type;
  
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

void bg_cmdline_parse(bg_cmdline_arg_t *,
                      int * argc, char *** argv, void * callback_data);

void bg_cmdline_arg_set_parameters(bg_cmdline_arg_t * args, const char * opt,
                                   const bg_parameter_info_t * params);

void bg_cmdline_arg_set_cfg_ctx(bg_cmdline_arg_t * args, const char * opt,
                                bg_cfg_ctx_t * ctx);

char ** bg_cmdline_get_locations_from_args(int * argc, char *** argv);

int bg_cmdline_check_unsupported(int argc, char ** argv);

void bg_cmdline_print_help(char * argv0, bg_help_format_t);

/* Commandline -> Config registry and parameters */


void bg_cmdline_print_help_parameters(const bg_parameter_info_t * parameters,
                                      bg_help_format_t);

int bg_cmdline_apply_options(bg_cfg_section_t * section,
                             bg_set_parameter_func_t set_parameter,
                             void * data,
                             const bg_parameter_info_t * parameters,
                             const char * option_string);

int bg_cmdline_apply_options_ctx(bg_cfg_ctx_t * ctx,
                                 const char * option_string);

int bg_cmdline_set_stream_options(gavl_dictionary_t * m,
                                  const char * option_string);

const char *
bg_cmdline_get_stream_options(gavl_dictionary_t * m, int stream);

void bg_cmdline_print_version(const char * application);

void bg_opt_v(void * data, int * argc, char *** _argv, int arg);

int bg_cmdline_parse_parameters(const char * string,
                                const bg_parameter_info_t * parameters,
                                gavl_dictionary_t * ret);

#endif // BG_CMDLINE_H_INCLUDED

