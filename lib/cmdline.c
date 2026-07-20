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




#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <unistd.h>
#include <time.h>

#include <config.h>
#include <gmerlin/translation.h>
#include <gmerlin/application.h>

#include <gmerlin/cfg_registry.h>
#include <gmerlin/cfgctx.h>
#include <gmerlin/cmdline.h>
#include <gmerlin/utils.h>
#include <gmerlin/log.h>
#define LOG_DOMAIN "cmdline"

static const bg_cmdline_app_data_t * app_data;

gavl_dictionary_t bg_cmdline_options = { 0 };

/* Terminal related functions */

#define MAX_COLS 79 /* For line-wrapping */

static void opt_help(void * data, int * argc, char *** argv, int arg);
static void opt_version(void * data, int * argc, char *** argv, int arg);

static void opt_v(void * data, int * argc, char *** _argv, int arg);

  
static void opt_v(void * data, int * argc, char *** _argv, int arg)
  {
  int val = 0;

  if(arg >= *argc)
    {
    fprintf(stderr, "Option -v requires an argument\n");
    exit(-1);
    }
  val = atoi((*_argv)[arg]);  
  gavl_set_log_verbose(val);
  bg_cmdline_remove_arg(argc, _argv, arg);
  }

static void do_indent(FILE * out, int num)
  {
  int i;
  for(i = 0; i < num; i++)
    fprintf(out, " ");
  }

static void dump_string_term(FILE * out, const char * str, int indent,
                             const char * translation_domain)
  {
  const char * start;
  const char * end;
  const char * pos;
  
  str = TR_DOM(str);

  
  start = str;
  pos   = str;
  end   = str;
  
  while(1)
    {
    if(isspace(*pos))
      end = pos;

    if(pos - start + indent + 2 > MAX_COLS)
      {
      do_indent(out, indent + 2);
      
      fwrite(start, 1, end - start, out);
      
      fprintf(out, "\n");
      
      while(isspace(*end))
        end++;
      start = end;
      }
    else if(*pos == '\0')
      {
      do_indent(out, indent + 2);
      
      fwrite(start, 1, pos - start, out);
      fprintf(out, "\n");
      break;
      }
    else if(*pos == '\n')
      {
      do_indent(out, indent + 2);
      
      fwrite(start, 1, pos - start, out);

      fprintf(out, "\n");
      
      pos++;
      
      start = pos;
      end = pos;
      }
    
    else
      pos++;
    }

  }

static const char ansi_underline[] = { 27, '[', '4', 'm', '\0' };
static const char ansi_normal[] = { 27, '[', '0', 'm', '\0' };
static const char ansi_bold[] = { 27, '[', '1', 'm', '\0' };

static void print_string(FILE * out, const char * str)
  {
  fprintf(out, "%s", str);
  }

static void print_bold(FILE * out, char * str)
  {
  if(isatty(fileno(out)))
    fprintf(out, "%s%s%s", ansi_bold, str, ansi_normal);
  else
    fprintf(out, "%s", str);
  }

static void print_italic(FILE * out, const char * str)
  {
  
  if(isatty(fileno(out)))
    fprintf(out, "%s%s%s", ansi_underline, str, ansi_normal);
  else
    fprintf(out, "%s", str);
  }

static void print_linebreak(FILE * out)
  {
  fprintf(out, "\n\n");
  }

static void print_version(const bg_cmdline_app_data_t * app_data)
  {
  printf("%s (%s) %s\n", bg_app_get_name(), app_data->package, app_data->version);
  printf(TR("Copyright (C) 2001-2007 Members of the gmerlin project\n"));
  printf(TR("This is free software.  You may redistribute copies of it under the terms of\n\
the GNU General Public License <http://www.gnu.org/licenses/gpl.html>.\n\
There is NO WARRANTY.\n"));
  }

static void print_help(const bg_cmdline_arg_t* args)
  {
  int i = 0;
  FILE * out = stdout;
  const char * help_arg = NULL;
  
  while(args[i].arg)
    {

    help_arg = args[i].help_arg;
    
    fprintf(out, "  ");
    print_bold(out, args[i].arg);

    if(help_arg)
      {
      fprintf(out, " ");
      print_italic(out, help_arg);
      }
    fprintf(out, "\n");
    dump_string_term(out, args[i].help_string, 0, NULL);
    
    fprintf(out, "\n");
    i++;
    }
  
  }

static void opt_syslog(void * data, int * argc, char *** _argv, int arg)
  {
  if(arg >= *argc)
    {
    fprintf(stderr, "Option -syslog requires an argument\n");
    exit(-1);
    }
  bg_log_syslog_init((*_argv)[arg]);
  bg_cmdline_remove_arg(argc, _argv, arg);
  }

static void opt_stderr(void * data, int * argc, char *** _argv, int arg)
  {
  bg_log_stderr_init();
  }
  
static const bg_cmdline_arg_t auto_options[] =
  {
    {
      .arg =         "-help",
      .help_string = TRS("Print this help message and exit"),
      .callback =    opt_help,
    },
    {
      .arg =         "-version",
      .help_string = TRS("Print version info and exit"),
      .callback =    opt_version,
    },
    {
      .arg =        "-v",
      .help_arg =    "level",
      .help_string = "Set verbosity level (0..4)",
      .callback =    opt_v,
    },
    {
      .arg =        "-syslog",
      .help_arg =    "name",
      .help_string = "Log to syslog the specified name",
      .callback =    opt_syslog,
    },
    {
      .arg =        "-stderr",
      .help_string = "Always log to stderr",
      .callback =    opt_stderr,
    },
    { /* End of options */ }
  };


void bg_cmdline_print_help(char * argv0)
  {
  int i;
  char * tmp_string;
  
  tmp_string = gavl_sprintf(TRD(app_data->synopsis, app_data->package), argv0);
  printf("Usage: %s\n\n", tmp_string);
  free(tmp_string);
  printf("%s\n", app_data->help_before);
  i = 0;
  while(app_data->args[i].name)
    {
    printf("%s\n\n", app_data->args[i].name);
    print_help(app_data->args[i].args);
    i++;
    }
  print_bold(stdout, "Generic options\n");
  printf("\nThe following generic options are available for all gmerlin applications\n");
  print_linebreak(stdout);
  print_help(auto_options);

  if(app_data->env)
    {
    print_bold(stdout, TR("Environment variables\n\n"));
    i = 0;
    while(app_data->env[i].name)
      {
      print_bold(stdout, app_data->env[i].name);
      printf("\n");
      dump_string_term(stdout, app_data->env[i].desc,
                       0, NULL);
      i++;
      print_linebreak(stdout);
      }
    }
  if(app_data->files)
    {
    print_bold(stdout, TR("Files\n\n"));
    i = 0;
    while(app_data->files[i].name)
      {
      print_bold(stdout, app_data->files[i].name);
      printf("\n");
      dump_string_term(stdout, app_data->files[i].desc,
                       0, NULL);
      print_linebreak(stdout);
      i++;
      }
    }
  
  }


static void opt_help(void * data, int * argc, char *** argv, int arg)
  {
  bg_cmdline_print_help((*argv)[0]);
  exit(0);
  }


static void opt_version(void * data, int * argc, char *** argv, int arg)
  {
  print_version(app_data);
  exit(0);
  }


/* */

void bg_cmdline_remove_arg(int * argc, char *** _argv, int arg)
  {
  char ** argv = *_argv;
  /* Move the upper args down */
  if(arg < *argc - 1)
    memmove(argv + arg, argv + arg + 1, 
            (*argc - arg - 1) * sizeof(*argv));
  (*argc)--;
  }

static int arg_match(const bg_cmdline_arg_t * arg, char * str,
                     int * stream_idx)
  {
  char * pos;
  *stream_idx = -1;

  if(arg->flags & BG_CMDLINE_ARG_PER_STREAM)
    {
    if(!strcmp(arg->arg, str))
      return 1;

    if(!gavl_string_starts_with(str, arg->arg))
      return 0;

    pos = str + strlen(arg->arg);
    if(*pos != '-')
      return 0;

    pos++;

    *stream_idx = strtol(pos, &pos, 10);

    if(*pos != '\0')
      return 0;

    return 1;
    }
  
  
  if(!strcmp(arg->arg, str))
    return 1;
  
  return 0;
  }

static void cmdline_parse(const bg_cmdline_arg_t * args,
                          int * argc, char *** _argv,
                          int parse_auto)
  {
  int found;
  int i, j;
  char ** argv = *_argv;

  int stream_idx;
  
  i = 1;

  if(parse_auto)
    cmdline_parse(auto_options, argc, _argv, 0);
  
  if(!args)
    return;
  
  while(i < *argc)
    {
    j = 0;
    found = 0;
    
    if(!strcmp(argv[i], "--"))
      break;
    
    while(args[j].arg)
      {
      
      if(arg_match(&args[j], argv[i], &stream_idx))
        {
        bg_cmdline_remove_arg(argc, _argv, i);
        
        if(args[j].callback)
          args[j].callback(NULL, argc, _argv, i);
        
        if(args[j].flags & BG_CMDLINE_ARG_STRING)
          {
          if(i >= *argc)
            {
            fprintf(stderr, "Option %s requires an argument\n", args[j].arg);
            exit(-1);
            }
          gavl_dictionary_set_string(&bg_cmdline_options,
                                     args[j].arg+1, argv[i]);
          bg_cmdline_remove_arg(argc, _argv, i);
          }
        else if(args[j].flags & BG_CMDLINE_ARG_PARAM)
          {
          gavl_array_t * arr;
          if(i >= *argc)
            {
            fprintf(stderr, "Option %s requires an argument\n", args[j].arg);
            exit(-1);
            }

          if(args[j].flags & BG_CMDLINE_ARG_PER_STREAM)
            arr = bg_cmdline_get_stream_params_wr(args[j].arg + 1, stream_idx);
          else
            arr = bg_cmdline_get_params_wr(args[j].arg + 1);

          gavl_string_array_insert_at(arr, -1, argv[i]);
          bg_cmdline_remove_arg(argc, _argv, i);
          }
        
        found = 1;
        break;
        }
      else
        j++;
      }
    if(!found)
      i++;
    }
  }

void bg_cmdline_parse(bg_cmdline_arg_t * args, int * argc, char *** _argv)
  {
  cmdline_parse(args, argc, _argv, 1);
  }


char ** bg_cmdline_get_locations_from_args(int * argc, char *** _argv)
  {
  char ** ret;
  int seen_dashdash;
  char ** argv;
  int i, index;
  int num_locations = 0;
  argv = *_argv;
  
  /* Count the locations */

  for(i = 1; i < *argc; i++)
    {
    if(!strcmp(argv[i], "--"))
      {
      num_locations += *argc - 1 - i;
      break;
      }
    else if(argv[i][0] != '-')
      num_locations++;
    }

  if(!num_locations)
    return NULL;
  
  /* Allocate return value */

  ret = calloc(num_locations + 1, sizeof(*ret));

  i = 1;
  index = 0;
  seen_dashdash = 0;
  
  while(i < *argc)
    {
    if(seen_dashdash || (argv[i][0] != '-'))
      {
      ret[index++] = argv[i];
      bg_cmdline_remove_arg(argc, _argv, i);
      }
    else if(!strcmp(argv[i], "--"))
      {
      seen_dashdash = 1;
      bg_cmdline_remove_arg(argc, _argv, i);
      }
    else
      {
      i++;
      }
    }
  return ret;
  }


static void print_help_parameters(int indent,
                                  const bg_parameter_info_t * parameters)
  {
  int i = 0;
  int j;
  FILE * out = stdout;
  
  int pos;
  
  char time_string[GAVL_TIME_STRING_LEN];
  char * tmp_string;

  const char * translation_domain = NULL;

  indent += 2;
  
  if(!indent)
    {
    do_indent(out, indent+2);
    
    fprintf(out, TR("Supported options:\n\n"));
    }
  
  while(parameters[i].name)
    {
    if(parameters[i].gettext_domain)
      translation_domain = parameters[i].gettext_domain;
    if(parameters[i].gettext_directory)
      bg_bindtextdomain(translation_domain, parameters[i].gettext_directory);
    
    if((parameters[i].type == BG_PARAMETER_SECTION) ||
       (parameters[i].flags & BG_PARAMETER_HIDE_DIALOG))
      {
      i++;
      continue;
      }
    pos = 0;

    do_indent(out, indent+2);
    pos += indent+2;
      
    print_bold(out, parameters[i].name);
    pos += strlen(parameters[i].name);
      
    fprintf(out, "=");
    
    switch(parameters[i].type)
      {
      case BG_PARAMETER_SECTION:
      case BG_PARAMETER_BUTTON:
        break;
      case BG_PARAMETER_CHECKBUTTON:
        tmp_string = gavl_sprintf(TR("[1|0] (default: %d)"), parameters[i].val_default.v.i);
        print_string(out, tmp_string);
        free(tmp_string);
        print_linebreak(out);
        break;
      case BG_PARAMETER_INT:
      case BG_PARAMETER_SLIDER_INT:
        pos += fprintf(out, TR("<number> ("));
        if(parameters[i].val_min.v.i < parameters[i].val_max.v.i)
          {
          pos += fprintf(out, "%d..%d, ",
                         parameters[i].val_min.v.i, parameters[i].val_max.v.i);
          }
        fprintf(out, TR("default: %d)"), parameters[i].val_default.v.i);

        print_linebreak(out);
        break;
      case BG_PARAMETER_SLIDER_FLOAT:
      case BG_PARAMETER_FLOAT:
        pos += fprintf(out, TR("<number> ("));
        if(parameters[i].val_min.v.d < parameters[i].val_max.v.d)
          {
          tmp_string = gavl_sprintf("%%.%df..%%.%df, ",
                                  parameters[i].num_digits,
                                  parameters[i].num_digits);
          pos += fprintf(out, tmp_string,
                         parameters[i].val_min.v.d, parameters[i].val_max.v.d);
          free(tmp_string);
          }
        tmp_string =
          gavl_sprintf(TR("default: %%.%df)"),
                     parameters[i].num_digits);
        fprintf(out, tmp_string,
                parameters[i].val_default.v.d);
        free(tmp_string);

        print_linebreak(out);

        break;
      case BG_PARAMETER_STRING_MULTILINE:
      case BG_PARAMETER_STRING:
      case BG_PARAMETER_FONT:
      case BG_PARAMETER_FILE:
      case BG_PARAMETER_DIRECTORY:
        pos += fprintf(out, TR("<string>"));
        if(parameters[i].val_default.v.str)
          {
          tmp_string = gavl_sprintf(TR(" (Default: %s)"), parameters[i].val_default.v.str);
          print_string(out, tmp_string);
          free(tmp_string);
          }
        print_linebreak(out);

        break;
      case BG_PARAMETER_STRING_HIDDEN:
        pos += fprintf(out, TR("<string>"));

        print_linebreak(out);
        break;
      case BG_PARAMETER_STRINGLIST:
        pos += fprintf(out, TR("<string>"));
        print_linebreak(out);

        j = 0;

        pos = 0;
        do_indent(out, indent+2);
        pos += indent+2;
        pos += fprintf(out, TR("Supported strings: "));
        
        while(parameters[i].multi_names[j])
          {
          if(j) pos += fprintf(out, " ");

          if(pos + strlen(parameters[i].multi_names[j]+1) > MAX_COLS)
            {
            fprintf(out, "\n");
            pos = 0;
            do_indent(out, indent+2);
            pos += indent+2;
            }
          
          pos += fprintf(out, "%s", parameters[i].multi_names[j]);
          j++;
          }
        print_linebreak(out);
        do_indent(out, indent+2);
        pos += indent+2;

        tmp_string = gavl_sprintf(TR("Default: %s"), parameters[i].val_default.v.str);
        print_string(out, tmp_string);
        free(tmp_string);
        print_linebreak(out);
        
        break;
      case BG_PARAMETER_COLOR_RGB:
        fprintf(out, TR("<r>,<g>,<b> (default: %.3f,%.3f,%.3f)"),
                parameters[i].val_default.v.color[0],
                parameters[i].val_default.v.color[1],
                parameters[i].val_default.v.color[2]);
        print_linebreak(out);
        
        do_indent(out, indent+2);
        pos += indent+2;
        
        fprintf(out, TR("<r>, <g> and <b> are in the range 0.0..1.0"));
          fprintf(out, "\n");
        break;
      case BG_PARAMETER_COLOR_RGBA:
        fprintf(out, TR("<r>,<g>,<b>,<a> (default: %.3f,%.3f,%.3f,%.3f)"),
                parameters[i].val_default.v.color[0],
                parameters[i].val_default.v.color[1],
                parameters[i].val_default.v.color[2],
                parameters[i].val_default.v.color[3]);
        print_linebreak(out);
        
        do_indent(out, indent+2);
        pos += indent+2;

        fprintf(out, TR("<r>, <g>, <b> and <a> are in the range 0.0..1.0"));
        fprintf(out, "\n");
        break;
      case BG_PARAMETER_MULTI_MENU:
        print_string(out, TR("option[:var1=val1:var2=val2..]"));
        print_linebreak(out);
        pos = 0;

        do_indent(out, indent+2);
        pos += indent+2;

        pos += fprintf(out, TR("Supported options: "));
        j = 0;

        if(parameters[i].multi_names)
          {
          while(parameters[i].multi_names[j])
            {
            if(j) pos += fprintf(out, " ");

            if(pos + strlen(parameters[i].multi_names[j]) > MAX_COLS)
              {
              fprintf(out, "\n");
              pos = 0;
              do_indent(out, indent+2);
              pos += indent+2;
              }
            pos += fprintf(out, "%s", parameters[i].multi_names[j]);
            j++;
            }
          }
        else
          pos += fprintf(out, TR("<None>"));

        print_linebreak(out);
        
        do_indent(out, indent+2);
        pos += indent+2;
        fprintf(out, TR("Default: %s"), parameters[i].val_default.v.str);
        
        print_linebreak(out);
        break;
      case BG_PARAMETER_DIRLIST:
        pos += fprintf(out, TR("dir1[:dir2..]"));
        break;
      case BG_PARAMETER_MULTI_LIST:
      case BG_PARAMETER_MULTI_CHAIN:
        print_string(out, TR("{option[{suboptions}][:option[{suboptions}]...]}"));
        
        
        print_linebreak(out);

        pos = 0;
        
        do_indent(out, indent+2);
        pos += indent+2;
        
        pos += fprintf(out, TR("Supported options: "));
        j = 0;
        while(parameters[i].multi_names[j])
          {
          if(j) pos += fprintf(out, " ");
          
          if(pos + strlen(parameters[i].multi_names[j]+1) > MAX_COLS)
            {
            fprintf(out, "\n");
            pos = 0;
            do_indent(out, indent+4);
            pos += indent+4;
            }
          pos += fprintf(out, "%s", parameters[i].multi_names[j]);
          j++;
          }
        fprintf(out, "\n\n");
        
        break;
      case BG_PARAMETER_TIME:
        print_string(out, TR("{[[HH:]MM:]SS} ("));
        if(parameters[i].val_min.v.l < parameters[i].val_max.v.l)
          {
          gavl_time_prettyprint(parameters[i].val_min.v.l, time_string);
          fprintf(out, "%s..", time_string);
          
          gavl_time_prettyprint(parameters[i].val_max.v.l, time_string);
          fprintf(out, "%s, ", time_string);
          }
        gavl_time_prettyprint(parameters[i].val_default.v.l, time_string);
        fprintf(out, TR("default: %s)"), time_string);

        print_linebreak(out);

        do_indent(out, indent+2);
        pos += indent+2;
        fprintf(out, TR("Seconds can be fractional (i.e. with decimal point)\n"));
        break;
      case BG_PARAMETER_POSITION:
        fprintf(out, TR("<x>,<y> (default: %.3f,%.3f)"),
                parameters[i].val_default.v.position[0],
                parameters[i].val_default.v.position[1]);
        print_linebreak(out);
        
        do_indent(out, indent+2);
        pos += indent+2;
        
        fprintf(out, TR("<r>, <g> and <b> are in the range 0.0..1.0"));
          fprintf(out, "\n");
        break;


      }
    
    do_indent(out, indent+2);
    pos += indent+2;
    
    fprintf(out, "%s", TR_DOM(parameters[i].long_name));
    
    print_linebreak(out);
    
    if(parameters[i].help_string)
      {
      dump_string_term(out, parameters[i].help_string, indent, translation_domain);
      print_linebreak(out);
      }
    
    /* Print suboptions */

    if(parameters[i].multi_parameters)
      {
      j = 0;
      while(parameters[i].multi_names[j])
        {
        if(parameters[i].multi_parameters[j])
          {
          do_indent(out, indent+2);
          pos += indent+2;
          //          print_linebreak(out, format);
          
          if(parameters[i].type == BG_PARAMETER_MULTI_MENU)
            {
            tmp_string = gavl_sprintf(TR("Suboptions for %s=%s"),
                                    parameters[i].name,parameters[i].multi_names[j]);
            }
          else
            {
            tmp_string = gavl_sprintf(TR("Suboptions for %s"),
                                    parameters[i].multi_names[j]);
            }
          print_bold(out, tmp_string);
          free(tmp_string);
          print_linebreak(out);
          
          //          print_linebreak(out, format);
          //          print_linebreak(out, format);
          print_help_parameters(indent+2, parameters[i].multi_parameters[j]);
          }
        j++;
        }
      }
    
    i++;
    }
  
  }

void bg_cmdline_print_help_parameters(const bg_parameter_info_t * parameters)
  {
  print_help_parameters(0, parameters);
  }

void bg_cmdline_init(const bg_cmdline_app_data_t * data)
  {
  app_data = data;
  }


int bg_cmdline_check_unsupported(int argc, char ** argv)
  {
  int ret = 1;
  int i;
  for(i = 1; i < argc; i++)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Unsupported option: %s", argv[i]);
    ret = 0;
    }
  return ret;
  }

#define ID_LEN 16

void bg_cmdline_get_stream_params(const char * name,
                                   int idx, gavl_array_t * ret)
  {
  const gavl_dictionary_t * dict;
  const gavl_array_t * arr;
  char id[ID_LEN];
  
  if(!(dict = gavl_dictionary_get_dictionary(&bg_cmdline_options, name)))
    return;

  /* Global options */
  snprintf(id, ID_LEN, "%d", -1);

  if((arr = gavl_dictionary_get_array(dict, id)))
    gavl_array_splice_array(ret, -1, 0, arr);

  /* Per stream options */
  snprintf(id, ID_LEN, "%d", idx);
  
  if((arr = gavl_dictionary_get_array(dict, id)))
    gavl_array_splice_array(ret, -1, 0, arr);
  
  }

gavl_array_t * bg_cmdline_get_stream_params_wr(const char * name,
                                               int idx)
  {
  gavl_dictionary_t * dict;

  char id[ID_LEN];
  snprintf(id, ID_LEN, "%d", idx);
  dict = gavl_dictionary_get_dictionary_create(&bg_cmdline_options, name);
  return gavl_dictionary_get_array_create(dict, id);
  }

const gavl_array_t * bg_cmdline_get_params(const char * name)
  {
  return gavl_dictionary_get_array(&bg_cmdline_options, name);
  }

/* Get options array for writing */
gavl_array_t * bg_cmdline_get_params_wr(const char * name)
  {
  return gavl_dictionary_get_array_create(&bg_cmdline_options, name);
  
  }

/*
 *  Can be called multiple times.
 */

int bg_cmdline_apply_params(gavl_dictionary_t * dst,
                            const gavl_parameter_info_t * info,
                            const gavl_array_t * arr)
  {
  int i;
  char * pos;
  char * val;
  char * var;
  const gavl_parameter_info_t * param;
  const gavl_parameter_info_t * sub_param;
  gavl_dictionary_t sub_params;
  gavl_value_t value;
  
  //  const gavl_parameter_info_t * subparam;

  gavl_dictionary_init(&sub_params);
  
  for(i = 0; i < arr->num_entries; i++)
    {
    if(!(var = gavl_strdup(gavl_string_array_get(arr, i))))
      continue;

    if(!(pos = strchr(var, '=')) || (strlen(pos) == 1))
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Invalid option: %s", var);
      free(var);
      return 0;
      }

    *pos = '\0';
    
    if(!(param = bg_parameter_find_in_array(info, var)))
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Unknown variable: %s", var);
      free(var);
      return 0;
      }

    pos++;
    val = pos;
    
    if(param->multi_names && param->multi_parameters)
      {
      int j;

      if((pos = strchr(val, ':'))) // Suboptions
        {
        *pos = '\0';
        pos++;
        }

      /* Find this option */
      j = 0;
      while(param->multi_names[j])
        {
        if(!strcmp(param->multi_names[j], val))
          break;
        j++;
        }

      if(!(param->multi_names[j]))
        {
        gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Unknown option: %s\n", val);
        free(var);
        return 0;
        }
      
      if(param->multi_parameters[j])
        {
        bg_cfg_section_create_items(&sub_params, param->multi_parameters[j]);
        
        if(pos)
          {
          /* Apply sub parameters */
          char * var_sub;
          char * val_sub;
          int end = 0;
        
          while(1)
            {
            var_sub = pos;
          
            if(!(pos = strchr(var_sub, '=')))
              {
              gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Error parsing option: %s\n", pos);
              free(var);
              gavl_dictionary_free(&sub_params);
              return 0;
              }
            *pos = '\0';
            pos++;

            val_sub = pos;

            if((pos = strchr(val_sub, ':')))
              *pos = '\0';
            else
              end = 1;

            fprintf(stderr, "Var: %s val: %s\n", var_sub, val_sub);

            if(!(sub_param = bg_parameter_find_in_array(param->multi_parameters[j], var_sub)))
              {
              gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Unknown variable: %s", var_sub);
              free(var);
              gavl_dictionary_free(&sub_params);
              return 0;
              }

            gavl_value_init(&value);
            value.type = gavl_parameter_type_to_gavl(sub_param->type);
            if((value.type != GAVL_TYPE_UNDEFINED) &&
               gavl_value_from_string(&value, val_sub))
              gavl_dictionary_set_nocopy(&sub_params, var_sub, &value);
          
          
            if(end)
              break;
          
            pos++;
          
            }
          }


        
        }
      
      
      if(param->type == GAVL_PARAMETER_MULTI_CHAIN)
        {
        /* Append to options */
        }
      else if(param->type == GAVL_PARAMETER_MULTI_MENU)
        {
        fprintf(stderr, "Got multi menu: %s=%s\n", var, val);
        gavl_dictionary_set_string(&sub_params, BG_CFG_TAG_NAME, val);

        gavl_dictionary_dump(&sub_params, 2);
        gavl_dictionary_set_dictionary(dst, var, &sub_params);
        
        }
      /* TODO: GAVL_PARAMETER_MULTI_LIST */
      
      }
    
    else
      {
      /* Convert value from string */
      gavl_value_init(&value);
      value.type = gavl_parameter_type_to_gavl(param->type);
      if((value.type != GAVL_TYPE_UNDEFINED) &&
         gavl_value_from_string(&value, val))
        gavl_dictionary_set_nocopy(dst, var, &value);
      
      }
    
    gavl_dictionary_reset(&sub_params);
    free(var);
    }
  
  return 1;
  
  }
