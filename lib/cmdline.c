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


/* Terminal related functions */

#define MAX_COLS 79 /* For line-wrapping */

static void opt_help(void * data, int * argc, char *** argv, int arg);
static void opt_help_man(void * data, int * argc, char *** argv, int arg);
static void opt_help_texi(void * data, int * argc, char *** argv, int arg);
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

static void do_indent(FILE * out, int num, bg_help_format_t format)
  {
  switch(format)
    {
    case BG_HELP_FORMAT_TERM:
    case BG_HELP_FORMAT_PLAIN:
      {
      int i;
      for(i = 0; i < num; i++)
        fprintf(out, " ");
      }
      break;
    case BG_HELP_FORMAT_MAN:
      //      fprintf(out, ".RS %d\n", num);
      break;
    case BG_HELP_FORMAT_TEXI:
      break;
    }
  }

static void dump_string_term(FILE * out, const char * str, int indent,
                             const char * translation_domain, bg_help_format_t format)
  {
  const char * start;
  const char * end;
  const char * pos;
  
  str = TR_DOM(str);

  if(format == BG_HELP_FORMAT_MAN)
    do_indent(out, indent + 2, format);
  
  start = str;
  pos   = str;
  end   = str;
  
  while(1)
    {
    if(isspace(*pos))
      end = pos;

    if(pos - start + indent + 2 > MAX_COLS)
      {
      if(format != BG_HELP_FORMAT_MAN)
        do_indent(out, indent + 2, format);
      
      fwrite(start, 1, end - start, out);
      
      fprintf(out, "\n");
      
      while(isspace(*end))
        end++;
      start = end;
      }
    else if(*pos == '\0')
      {
      if(format != BG_HELP_FORMAT_MAN)
        do_indent(out, indent + 2, format);
      
      fwrite(start, 1, pos - start, out);
      fprintf(out, "\n");
      break;
      }
    else if(*pos == '\n')
      {
      if(format != BG_HELP_FORMAT_MAN)
        do_indent(out, indent + 2, format);
      
      fwrite(start, 1, pos - start, out);

      if(format == BG_HELP_FORMAT_TEXI)
        fprintf(out, "@*\n");
      else
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

static void print_string(FILE * out, const char * str, bg_help_format_t format)
  {
  switch(format)
    {
    case BG_HELP_FORMAT_TEXI:
      {
      const char * pos;
      pos = str;
      while(*pos)
        {
        if((*pos == '{') || (*pos == '}') || (*pos == '@'))
          fprintf(out, "@%c", *pos);
        else
          fprintf(out, "%c", *pos);
        pos++;
        }
      }
      break;
    default:
      fprintf(out, "%s", str);
      break;
    }
  }

static void print_bold(FILE * out, char * str, bg_help_format_t format)
  {
  switch(format)
    {
    case BG_HELP_FORMAT_TERM:
      fprintf(out, "%s%s%s", ansi_bold, str, ansi_normal);
      break;
    case BG_HELP_FORMAT_PLAIN:
      fprintf(out, "%s", str);
      /* Do nothing */
      break;
    case BG_HELP_FORMAT_MAN:
      fprintf(out, ".B %s\n", str);
      break;
    case BG_HELP_FORMAT_TEXI:
      fprintf(out, "@b{");
      print_string(out, str, format);
      fprintf(out, "}");
      break;
    }
  }

static void print_italic(FILE * out, const char * str, bg_help_format_t format)
  {
  switch(format)
    {
    case BG_HELP_FORMAT_TERM:
      fprintf(out, "%s%s%s", ansi_underline, str, ansi_normal);
      break;
    case BG_HELP_FORMAT_PLAIN:
      /* Do nothing */
      fprintf(out, "%s", str);
      break;
    case BG_HELP_FORMAT_MAN:
      fprintf(out, ".I %s\n", str);
      break;
    case BG_HELP_FORMAT_TEXI:
      fprintf(out, "@i{");
      print_string(out, str, format);
      fprintf(out, "}");
      break;
    }
  }

static void print_linebreak(FILE * out, bg_help_format_t format)
  {
  if(format == BG_HELP_FORMAT_MAN)
    fprintf(out, "\n.P\n");
  else if(format == BG_HELP_FORMAT_TEXI)
    fprintf(out, "@*@*\n");
  else
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

static void print_help(const bg_cmdline_arg_t* args, bg_help_format_t format)
  {
  int i = 0;
  char * tmp_string;
  FILE * out = stdout;
  const char * help_arg = NULL;
  
  if(format == BG_HELP_FORMAT_TEXI)
    fprintf(out, "@table @i\n");
  while(args[i].arg)
    {

    if(args[i].parameters)
      {
      help_arg = "param1=val1[&param2=val2...]";
      }
    else
      help_arg = args[i].help_arg;
    
    switch(format)
      {
      case BG_HELP_FORMAT_PLAIN:
      case BG_HELP_FORMAT_TERM:
        fprintf(out, "  ");
        print_bold(out, args[i].arg, format);

        if(help_arg)
          {
          fprintf(out, " ");
          print_italic(out, help_arg, format);
          }
        fprintf(out, "\n");
        dump_string_term(out, args[i].help_string, 0, NULL, format);
    
        break;
      case BG_HELP_FORMAT_TEXI:
        fprintf(out, "@item %s", args[i].arg);
        if(help_arg)
          {
          fprintf(out, " ");
          print_string(out, help_arg, format);
          }
        fprintf(out, "\n");
        print_string(out, args[i].help_string, format);
        fprintf(out, "@*\n");
        break;
      case BG_HELP_FORMAT_MAN:
        tmp_string = gavl_escape_string(gavl_strdup(args[i].arg), "-");
        print_bold(out, tmp_string, format);
        free(tmp_string);
        
        if(help_arg)
          print_italic(out, help_arg, format);
        fprintf(out, "\n");
        fprintf(out, ".RS 2\n");
        dump_string_term(out, args[i].help_string, 0, NULL, format);
        fprintf(out, ".RE\n");

        break;
      }
    
    if(args[i].parameters)
      {
      bg_cmdline_print_help_parameters(args[i].parameters, format);
      }
    fprintf(out, "\n");
    i++;
    }
  if(format == BG_HELP_FORMAT_TEXI)
    fprintf(out, "@end table\n");
  
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
      .arg =         "-help-man",
      .help_string = TRS("Print this help message as a manual page and exit"),
      .callback =    opt_help_man,
    },
    {
      .arg =         "-help-texi",
      .help_string = TRS("Print this help message in texinfo format and exit"),
      .callback =    opt_help_texi,
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


void bg_cmdline_print_help(char * argv0, bg_help_format_t format)
  {
  int i;
  char * tmp_string;
  
  switch(format)
    {
    case BG_HELP_FORMAT_TERM:
    case BG_HELP_FORMAT_PLAIN:
      tmp_string = bg_sprintf(TRD(app_data->synopsis, app_data->package), argv0);
      printf("Usage: %s\n\n", tmp_string);
      free(tmp_string);
      printf("%s\n", app_data->help_before);
      i = 0;
      while(app_data->args[i].name)
        {
        printf("%s\n\n", app_data->args[i].name);
        print_help(app_data->args[i].args, format);
        i++;
        }
      print_bold(stdout, "Generic options\n", format);
      printf("\nThe following generic options are available for all gmerlin applications\n");
      print_linebreak(stdout, format);
      print_help(auto_options, format);

      if(app_data->env)
        {
        print_bold(stdout, TR("Environment variables\n\n"), format);
        i = 0;
        while(app_data->env[i].name)
          {
          print_bold(stdout, app_data->env[i].name, format);
          printf("\n");
          dump_string_term(stdout, app_data->env[i].desc,
                           0, NULL, format);
          i++;
          print_linebreak(stdout, format);
          }
        }
      if(app_data->files)
        {
        print_bold(stdout, TR("Files\n\n"), format);
        i = 0;
        while(app_data->files[i].name)
          {
          print_bold(stdout, app_data->files[i].name, format);
          printf("\n");
          dump_string_term(stdout, app_data->files[i].desc,
                           0, NULL, format);
          print_linebreak(stdout, format);
          i++;
          }
        }
      
      
      break;
    case BG_HELP_FORMAT_MAN:
      {
      char date_str[512];
      struct tm brokentime;
      time_t t;
      char ** args;
      char * string_uc;
      
      time(&t);
      localtime_r(&t, &brokentime);
      strftime(date_str, 511, "%B %Y", &brokentime);

      string_uc = bg_toupper(bg_app_get_name());
      
      printf(".TH %s 1 \"%s\" Gmerlin \"User Manuals\"\n", string_uc,
             date_str);

      free(string_uc);

      if(bg_app_get_label())
        printf(".SH NAME\n%s \\- %s\n", bg_app_get_name(),
               bg_app_get_label());
      else
        printf(".SH NAME\n%s\n", bg_app_get_name());
        
      printf(".SH SYNOPSIS\n.B %s \n", bg_app_get_name());
      tmp_string = gavl_strdup(TRD(app_data->synopsis, app_data->package));
      
      args = gavl_strbreak(tmp_string, ' ');
      i = 0;
      while(args && (args+i) && args[i])
        {
        printf(".I %s\n", args[i]);
        i++;
        }
      gavl_strbreak_free(args);
      
      if(app_data->help_before)
        printf(".SH DESCRIPTION\n%s\n", TRD(app_data->help_before, app_data->package));

      i = 0;
      while(app_data->args[i].name)
        {
        string_uc = bg_toupper(app_data->args[i].name);
        printf(".SH %s\n\n", string_uc);
        free(string_uc);
        print_help(app_data->args[i].args, format);
        i++;
        }
      printf(".SH GENERIC OPTIONS\nThe following generic options are available for all gmerlin applications\n\n");
      print_help(auto_options, format);

      if(app_data->env)
        {
        printf(TR(".SH ENVIRONMENT VARIABLES\n"));
        i = 0;
        while(app_data->env[i].name)
          {
          print_bold(stdout, app_data->env[i].name, format);
          printf("\n");
          printf(".RS 2\n");
          dump_string_term(stdout, app_data->env[i].desc,
                           0, NULL, format);
          printf(".RE\n");
          i++;
          }
        }
      if(app_data->files)
        {
        printf(TR(".SH FILES\n"));
        i = 0;
        while(app_data->files[i].name)
          {
          print_bold(stdout, app_data->files[i].name, format);
          printf("\n");
          printf(".RS 2\n");
          dump_string_term(stdout, app_data->files[i].desc,
                           0, NULL, format);
          printf(".RE\n");
          print_linebreak(stdout, format);
          i++;
          }
        }
      }
      break;
    case BG_HELP_FORMAT_TEXI:
      printf("@table @b\n");
      printf("@item Synopsis\n");
      printf("@b{%s} @i{%s}@*\n", bg_app_get_name(),
             TRD(app_data->synopsis, app_data->package));
      if(app_data->help_before)
        {
        printf("@item Description\n");
        printf("%s@*\n", TRD(app_data->help_before, app_data->package));
        }
      i = 0;
      while(app_data->args[i].name)
        {
        printf("@item %s\n", app_data->args[i].name);
        print_help(app_data->args[i].args, format);
        i++;
        }
      printf("@item Generic options\n");
      printf("The following generic options are available for all gmerlin applications@*\n");
      print_help(auto_options, format);

      if(app_data->env)
        {
        printf(TR("@item Environment variables\n"));
        printf("@table @env\n");
        i = 0;
        while(app_data->env[i].name)
          {
          printf("@item %s\n", app_data->env[i].name);
          printf("%s@*\n", TRD(app_data->env[i].desc, app_data->package));
          i++;
          }
        printf("@end table\n");
        }

      if(app_data->files)
        {
        printf(TR("@item Files\n"));
        printf("@table @file\n");
        i = 0;
        while(app_data->files[i].name)
          {
          printf("@item %s\n", app_data->files[i].name);
          printf("%s@*\n", TRD(app_data->files[i].desc, app_data->package));
          i++;
          }
        printf("@end table\n");
        }
      printf("@end table\n");
      break;
    }

  }


static void opt_help(void * data, int * argc, char *** argv, int arg)
  {
  if(isatty(fileno(stdout)))
    bg_cmdline_print_help((*argv)[0], BG_HELP_FORMAT_TERM);
  else
    bg_cmdline_print_help((*argv)[0], BG_HELP_FORMAT_PLAIN);
  exit(0);
  }

static void opt_help_man(void * data, int * argc, char *** argv, int arg)
  {
  bg_cmdline_print_help((*argv)[0], BG_HELP_FORMAT_MAN);
  exit(0);
  }

static void opt_help_texi(void * data, int * argc, char *** argv, int arg)
  {
  bg_cmdline_print_help((*argv)[0], BG_HELP_FORMAT_TEXI);
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

static void cmdline_parse(const bg_cmdline_arg_t * args,
                          int * argc, char *** _argv,
                          void * callback_data,
                          int parse_auto)
  {
  int found;
  int i, j;
  char ** argv = *_argv;

  i = 1;

  if(parse_auto)
    cmdline_parse(auto_options, argc, _argv, NULL, 0);
  
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
      const char * colon_pos;
      int idx;
      
      if(!strcmp(args[j].arg, argv[i]))
        {
        bg_cmdline_remove_arg(argc, _argv, i);
        if(args[j].callback)
          args[j].callback(callback_data, argc, _argv, i);
        if(args[j].argv)
          {
          if(i >= *argc)
            {
            fprintf(stderr, "Option %s requires an argument\n", args[j].arg);
            exit(-1);
            }
          *args[j].argv = argv[i];
          bg_cmdline_remove_arg(argc, _argv, i);
          }
        else if(args[j].s && args[j].parameters)
          {
          if(i >= *argc)
            {
            fprintf(stderr, "Option %s requires an argument\n", args[j].arg);
            exit(-1);
            }

          bg_cmdline_apply_options(args[j].s,
                                   NULL,
                                   NULL,
                                   args[j].parameters,
                                   argv[i]);
          
          bg_cmdline_remove_arg(argc, _argv, i);
          }
        
        found = 1;
        break;
        }
      else if(args[j].callback_idx && (colon_pos = strchr(argv[i], '/')) &&
              !strncmp(argv[i], args[j].arg, colon_pos - argv[i]) &&
              ((idx = atoi(colon_pos+1)) > 0))
        {
        args[j].callback_idx(callback_data, idx, argc, _argv, i);
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

void bg_cmdline_parse(bg_cmdline_arg_t * args, int * argc, char *** _argv,
                      void * callback_data)
  {
  cmdline_parse(args, argc, _argv, callback_data, 1);
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


int bg_cmdline_apply_options(bg_cfg_section_t * section,
                             bg_set_parameter_func_t set_parameter,
                             void * data,
                             const bg_parameter_info_t * parameters,
                             const char * option_string)
  {
  bg_cfg_section_t * section_priv = NULL;

  if(!section)
    {
    section_priv = 
      bg_cfg_section_create_from_parameters("sec", parameters);
    section = section_priv;
    }

  if(!bg_cfg_section_set_parameters_from_string(section,
                                                parameters,
                                                option_string))
    {
    if(section_priv)
      bg_cfg_section_destroy(section_priv);
    return 0;
    }
  /* Now, apply the section */
  if(set_parameter)
    bg_cfg_section_apply(section, parameters, set_parameter, data);

  if(section_priv)
    bg_cfg_section_destroy(section_priv);
  return 1;
  }

int bg_cmdline_apply_options_ctx(bg_cfg_ctx_t * ctx,
                                 const char * option_string)
  {
  return bg_cmdline_apply_options(ctx->s,
                                  ctx->set_param,
                                  ctx->cb_data,
                                  ctx->p,
                                  option_string);
  }

static char * create_stream_key(int stream)
  {
  if(stream < 0)
    return gavl_strdup("opt");
  else
    return bg_sprintf("opt_%d", stream);
  }

int bg_cmdline_set_stream_options(gavl_dictionary_t * m,
                                  const char * option_string)
  {
  int stream;
  char * key;
  /* Options can be prepended by <num>: to specify a particular
     stream or global */
  if(isdigit(*option_string))
    {
    char * rest;
    stream = strtol(option_string, &rest, 10);
    if(!rest || (*rest != ':'))
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Invalid stream options %s", option_string);
      return 0;
      }
    if(stream <= 0)
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Invalid stream index %d", stream);
      return 0;
      }

    stream--; // off by one
    rest++;   // Skip ':'

    key = create_stream_key(stream);
    gavl_dictionary_set_string(m, key, rest);
    free(key);
    }
  else
    {
    key = create_stream_key(-1);
    gavl_dictionary_set_string(m, key, option_string);
    free(key);
    }
  return 1;
  }

const char *
bg_cmdline_get_stream_options(gavl_dictionary_t * m, int stream)
  {
  const char * ret;
  char * key;

  /* Try specific option */
  key = create_stream_key(stream);
  ret = gavl_dictionary_get_string(m, key);
  free(key);

  if(ret)
    return ret;

  /* Try generic option */
  key = create_stream_key(-1);
  ret = gavl_dictionary_get_string(m, key);
  free(key);

  return ret; // Can be NULL

  }

static void print_help_parameters(int indent,
                                  const bg_parameter_info_t * parameters,
                                  bg_help_format_t format)
  {
  int i = 0;
  int j;
  FILE * out = stdout;
  
  int pos;
  
  char time_string[GAVL_TIME_STRING_LEN];
  char * tmp_string;

  const char * translation_domain = NULL;

  indent += 2;
  if(format == BG_HELP_FORMAT_MAN)
    fprintf(out, ".RS 2\n");
  else if(format == BG_HELP_FORMAT_TEXI)
    fprintf(out, "@table @r\n");
  
  if(!indent)
    {
    do_indent(out, indent+2, format);
    
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

    if(format == BG_HELP_FORMAT_MAN)
      {
      do_indent(out, indent+2, format);
      fprintf(out, ".BR ");
       
      //      pos += fprintf(out, spaces);
      //      pos += fprintf(out, "  ");
      
      fprintf(out, "%s", parameters[i].name);
      pos += strlen(parameters[i].name);
      fprintf(out, " \"=");
      }
    else if(format == BG_HELP_FORMAT_TEXI)
      {
      fprintf(out, "@item ");

      pos += indent+2;
      
      print_bold(out, parameters[i].name, format);
      pos += strlen(parameters[i].name);
      
      fprintf(out, "=");
      }
    else
      {
      do_indent(out, indent+2, format);
      pos += indent+2;
      
      print_bold(out, parameters[i].name, format);
      pos += strlen(parameters[i].name);
      
      fprintf(out, "=");
      }
    
    switch(parameters[i].type)
      {
      case BG_PARAMETER_SECTION:
      case BG_PARAMETER_BUTTON:
        break;
      case BG_PARAMETER_CHECKBUTTON:
        tmp_string = bg_sprintf(TR("[1|0] (default: %d)"), parameters[i].val_default.v.i);
        print_string(out, tmp_string, format);
        free(tmp_string);
        if(format == BG_HELP_FORMAT_MAN)
          fprintf(out, "\"");
        print_linebreak(out, format);
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

        if(format == BG_HELP_FORMAT_MAN)
          fprintf(out, "\"");
        print_linebreak(out, format);
        break;
      case BG_PARAMETER_SLIDER_FLOAT:
      case BG_PARAMETER_FLOAT:
        pos += fprintf(out, TR("<number> ("));
        if(parameters[i].val_min.v.d < parameters[i].val_max.v.d)
          {
          tmp_string = bg_sprintf("%%.%df..%%.%df, ",
                                  parameters[i].num_digits,
                                  parameters[i].num_digits);
          pos += fprintf(out, tmp_string,
                         parameters[i].val_min.v.d, parameters[i].val_max.v.d);
          free(tmp_string);
          }
        tmp_string =
          bg_sprintf(TR("default: %%.%df)"),
                     parameters[i].num_digits);
        fprintf(out, tmp_string,
                parameters[i].val_default.v.d);
        free(tmp_string);

        if(format == BG_HELP_FORMAT_MAN)
          fprintf(out, "\"");
        print_linebreak(out, format);

        break;
      case BG_PARAMETER_STRING:
      case BG_PARAMETER_FONT:
      case BG_PARAMETER_FILE:
      case BG_PARAMETER_DIRECTORY:
        pos += fprintf(out, TR("<string>"));
        if(parameters[i].val_default.v.str)
          {
          tmp_string = bg_sprintf(TR(" (Default: %s)"), parameters[i].val_default.v.str);
          print_string(out, tmp_string, format);
          free(tmp_string);
          }
        if(format == BG_HELP_FORMAT_MAN)
          fprintf(out, "\"");
        print_linebreak(out, format);

        break;
      case BG_PARAMETER_STRING_HIDDEN:
        pos += fprintf(out, TR("<string>"));

        if(format == BG_HELP_FORMAT_MAN)
          fprintf(out, "\"");
        print_linebreak(out, format);
        break;
      case BG_PARAMETER_STRINGLIST:
        pos += fprintf(out, TR("<string>"));
        if(format == BG_HELP_FORMAT_MAN)
          fprintf(out, "\"");
        print_linebreak(out, format);

        j = 0;

        pos = 0;
        do_indent(out, indent+2, format);
        pos += indent+2;
        pos += fprintf(out, TR("Supported strings: "));
        
        while(parameters[i].multi_names[j])
          {
          if(j) pos += fprintf(out, " ");

          if(pos + strlen(parameters[i].multi_names[j]+1) > MAX_COLS)
            {
            if((format == BG_HELP_FORMAT_TERM) || (format == BG_HELP_FORMAT_PLAIN))
              fprintf(out, "\n");
            pos = 0;
            do_indent(out, indent+2, format);
            pos += indent+2;
            }
          
          pos += fprintf(out, "%s", parameters[i].multi_names[j]);
          j++;
          }
        print_linebreak(out, format);
        do_indent(out, indent+2, format);
        pos += indent+2;

        tmp_string = bg_sprintf(TR("Default: %s"), parameters[i].val_default.v.str);
        print_string(out, tmp_string, format);
        free(tmp_string);
        print_linebreak(out, format);
        
        break;
      case BG_PARAMETER_COLOR_RGB:
        fprintf(out, TR("<r>,<g>,<b> (default: %.3f,%.3f,%.3f)"),
                parameters[i].val_default.v.color[0],
                parameters[i].val_default.v.color[1],
                parameters[i].val_default.v.color[2]);
        if(format == BG_HELP_FORMAT_MAN)
          fprintf(out, "\"");
        print_linebreak(out, format);
        
        do_indent(out, indent+2, format);
        pos += indent+2;
        
        fprintf(out, TR("<r>, <g> and <b> are in the range 0.0..1.0"));
        if(format != BG_HELP_FORMAT_MAN)
          fprintf(out, "\n");
        else
          fprintf(out, "\n.P\n");
        break;
      case BG_PARAMETER_COLOR_RGBA:
        fprintf(out, TR("<r>,<g>,<b>,<a> (default: %.3f,%.3f,%.3f,%.3f)"),
                parameters[i].val_default.v.color[0],
                parameters[i].val_default.v.color[1],
                parameters[i].val_default.v.color[2],
                parameters[i].val_default.v.color[3]);
        if(format == BG_HELP_FORMAT_MAN)
          fprintf(out, "\"");
        print_linebreak(out, format);
        
        do_indent(out, indent+2, format);
        pos += indent+2;

        fprintf(out, TR("<r>, <g>, <b> and <a> are in the range 0.0..1.0"));
        if(format != BG_HELP_FORMAT_MAN)
          fprintf(out, "\n");
        else
          fprintf(out, "\n.P\n");
        break;
      case BG_PARAMETER_MULTI_MENU:
        print_string(out, TR("option[{suboptions}]"), format);
        if(format == BG_HELP_FORMAT_MAN)
          fprintf(out, "\"");
        print_linebreak(out, format);
        pos = 0;

        do_indent(out, indent+2, format);
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
              if(format != BG_HELP_FORMAT_MAN)
                fprintf(out, "\n");
              pos = 0;
              do_indent(out, indent+2, format);
              pos += indent+2;
              }
            pos += fprintf(out, "%s", parameters[i].multi_names[j]);
            j++;
            }
          }
        else
          pos += fprintf(out, TR("<None>"));

        print_linebreak(out, format);
        
        do_indent(out, indent+2, format);
        pos += indent+2;
        fprintf(out, TR("Default: %s"), parameters[i].val_default.v.str);
        
        print_linebreak(out, format);
        break;
      case BG_PARAMETER_DIRLIST:
        pos += fprintf(out, TR("dir1[:dir2..]"));
        break;
      case BG_PARAMETER_MULTI_LIST:
      case BG_PARAMETER_MULTI_CHAIN:
        print_string(out, TR("{option[{suboptions}][:option[{suboptions}]...]}"), format);

        // print_string(out, TR("option1?suboption1=b&suboption2=d$ }][:option[{suboptions}]...]}"), format);
        
        if(format == BG_HELP_FORMAT_MAN)
          fprintf(out, "\"");
        
        print_linebreak(out, format);

        pos = 0;
        
        do_indent(out, indent+2, format);
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
            do_indent(out, indent+4, format);
            pos += indent+4;
            }
          pos += fprintf(out, "%s", parameters[i].multi_names[j]);
          j++;
          }
        fprintf(out, "\n\n");
        
        break;
      case BG_PARAMETER_TIME:
        print_string(out, TR("{[[HH:]MM:]SS} ("), format);
        if(parameters[i].val_min.v.l < parameters[i].val_max.v.l)
          {
          gavl_time_prettyprint(parameters[i].val_min.v.l, time_string);
          fprintf(out, "%s..", time_string);
          
          gavl_time_prettyprint(parameters[i].val_max.v.l, time_string);
          fprintf(out, "%s, ", time_string);
          }
        gavl_time_prettyprint(parameters[i].val_default.v.l, time_string);
        fprintf(out, TR("default: %s)"), time_string);

        if(format == BG_HELP_FORMAT_MAN)
          fprintf(out, "\"");
        print_linebreak(out, format);

        do_indent(out, indent+2, format);
        pos += indent+2;
        fprintf(out, TR("Seconds can be fractional (i.e. with decimal point)\n"));
        break;
      case BG_PARAMETER_POSITION:
        fprintf(out, TR("<x>,<y> (default: %.3f,%.3f)"),
                parameters[i].val_default.v.position[0],
                parameters[i].val_default.v.position[1]);
        if(format == BG_HELP_FORMAT_MAN)
          fprintf(out, "\"");
        print_linebreak(out, format);
        
        do_indent(out, indent+2, format);
        pos += indent+2;
        
        fprintf(out, TR("<r>, <g> and <b> are in the range 0.0..1.0"));
        if(format != BG_HELP_FORMAT_MAN)
          fprintf(out, "\n");
        else
          fprintf(out, "\n.P\n");
        break;


      }
    
    do_indent(out, indent+2, format);
    pos += indent+2;
    
    fprintf(out, "%s", TR_DOM(parameters[i].long_name));
    
    print_linebreak(out, format);
    
    if(parameters[i].help_string)
      {
      dump_string_term(out, parameters[i].help_string, indent, translation_domain, format);
      print_linebreak(out, format);
      }
    
    /* Print suboptions */

    if(parameters[i].multi_parameters)
      {
      j = 0;
      while(parameters[i].multi_names[j])
        {
        if(parameters[i].multi_parameters[j])
          {
          do_indent(out, indent+2, format);
          pos += indent+2;
          //          print_linebreak(out, format);
          
          if(parameters[i].type == BG_PARAMETER_MULTI_MENU)
            {
            tmp_string = bg_sprintf(TR("Suboptions for %s=%s"),
                                    parameters[i].name,parameters[i].multi_names[j]);
            }
          else
            {
            tmp_string = bg_sprintf(TR("Suboptions for %s"),
                                    parameters[i].multi_names[j]);
            }
          print_bold(out, tmp_string, format);
          free(tmp_string);
          print_linebreak(out, format);
          
          //          print_linebreak(out, format);
          //          print_linebreak(out, format);
          print_help_parameters(indent+2, parameters[i].multi_parameters[j], format);
          }
        j++;
        }
      }
    
    i++;
    }
  
  if(format == BG_HELP_FORMAT_MAN)
    fprintf(out, ".RE\n");
  else if(format == BG_HELP_FORMAT_TEXI)
    fprintf(out, "@end table\n");

  }

void bg_cmdline_print_help_parameters(const bg_parameter_info_t * parameters,
                                      bg_help_format_t format)
  {
  print_help_parameters(0, parameters, format);
  }

void bg_cmdline_init(const bg_cmdline_app_data_t * data)
  {
  app_data = data;
  }

void bg_cmdline_arg_set_parameters(bg_cmdline_arg_t * args, const char * opt,
                                   const bg_parameter_info_t * params)
  {
  int i = 0;
  while(args[i].arg)
    {
    if(!strcmp(args[i].arg, opt))
      {
      args[i].parameters = params;
      return;
      }
    i++;
    }
  }

void bg_cmdline_arg_set_cfg_ctx(bg_cmdline_arg_t * args, const char * opt,
                                bg_cfg_ctx_t * ctx)
  {
  int i = 0;
  while(args[i].arg)
    {
    if(!strcmp(args[i].arg, opt))
      {
      args[i].parameters = ctx->p;
      args[i].s          = ctx->s;
      return;
      }
    i++;
    }
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

/* Parse parameters in the form a=b&c=d. Returns 0 if an unsupported parameter was encountered */

int bg_cmdline_parse_parameters(const char * string,
                                const bg_parameter_info_t * parameters,
                                gavl_dictionary_t * dict)
  {
  int ret = 0;
  int i = 0;
  char * pos;
  
  const bg_parameter_info_t * info = NULL;
  char ** vars = gavl_strbreak(string, '&');
  
  if(!vars)
    return 1;
  
  while(vars[i])
    {
    if(!(pos = gavl_find_char(vars[i], '=')))
      return 0;

    *pos = '\0';
    pos++;

    if(parameters && !(info = bg_parameter_find(parameters, vars[i])))
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Unknown parameter %s", vars[i]);
      goto fail;
      }

    if(info)
      {
      gavl_value_t val;
      gavl_value_init(&val);
      val.type = bg_parameter_type_to_gavl(info->type);
      gavl_value_from_string(&val, pos);
      gavl_dictionary_set_nocopy(dict, vars[i], &val);
      }
    else
      gavl_dictionary_set_string(dict, vars[i], pos);
    
    i++;
    }

  ret = 1;
  
  fail:

  if(vars)
    gavl_strbreak_free(vars);

  return ret;
  }
