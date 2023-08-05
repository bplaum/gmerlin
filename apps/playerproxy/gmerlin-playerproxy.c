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
#include <signal.h>
#include <locale.h>
#include <ctype.h>



#include <config.h>

#include <gavl/metatags.h>
#include <gavl/gavlsocket.h>

#include <gmerlin/cfg_registry.h>
#include <gmerlin/cfgctx.h>
#include <gmerlin/cmdline.h>
#include <gmerlin/utils.h>
// #include <gmerlin/remote.h>
#include <gmerlin/translation.h>
#include <gmerlin/player.h>
#include <gmerlin/playermsg.h>



#include <gmerlin/bgmsg.h>
#include <gmerlin/http.h>
#include <gmerlin/httpserver.h>
#include <gmerlin/websocket.h>
#include <gmerlin/backend.h>

#include <gmerlin/bggavl.h>
#include <gmerlin/log.h>
#include <gmerlin/application.h>
#include <gmerlin/subprocess.h>

#define LOG_DOMAIN "gmerlin-playerproxy"

#define META_COMMAND_SUFFIX "CommandSuffix"

static gavl_array_t arr;

typedef struct
  {
  bg_subprocess_t * p;
  char * dev;
  } proc_t;


proc_t * procs = NULL;
int procs_alloc = 0;
int num_procs = 0;

static void start_proc(const char * addr, const char * label, const char * suffix)
  {
  const char * command;
  
  //  command = bg_sprintf("gmerlin-remote -addr %s %s", addr, suffix);
  command = bg_sprintf("gmerlin-remote -addr %s -label \"%s\" %s 2>> /dev/null >> /dev/null",
                       addr, label, suffix);
  
  if(procs_alloc <= num_procs)
    {
    procs_alloc += 16;

    procs = realloc(procs, procs_alloc * sizeof(*procs));
    memset(procs + num_procs, 0, (procs_alloc - num_procs) * sizeof(*procs));
    }

  procs[num_procs].dev = gavl_strdup(addr);
  procs[num_procs].p = bg_subprocess_create(command, 0, 0, 0);
  num_procs++;
  }

static void stop_proc(int idx)
  {
  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Closing proxy for %s", procs[idx].dev);
   
  bg_subprocess_kill(procs[idx].p, SIGTERM);
  bg_subprocess_close(procs[idx].p);

  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Closed proxy for %s", procs[idx].dev);
  
  free(procs[idx].dev);

  if(idx < num_procs-1)
    memmove(procs + idx, procs + idx + 1, (num_procs-1-idx) * sizeof(*procs));

  num_procs--;
      
  memset(procs + num_procs, 0, sizeof(*procs));
  }

static void stop_procs(const char * addr)
  {
  int i = 0;

  while(i < num_procs)
    {
    if(!strcmp(procs[i].dev, addr))
      {
      stop_proc(i);
      }
    else
      i++;
    }
  
  }



static void opt_p(void * data, int * argc, char *** _argv, int arg)
  {
  char * str;
  char * pos;
  char * addr = NULL;
  
  gavl_value_t val;
  gavl_dictionary_t * dict;
  
  if(arg >= *argc)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Option -p requires an argument");
    exit(-1);
    }

  str = (*_argv)[arg];

  while(isspace(*str) && (*str != '\0'))
    str++;

  pos = str;

  while(!isspace(*pos) && (*pos != '\0'))
    pos++;

  addr = gavl_strndup(str, pos);

  while(isspace(*pos) && (*pos != '\0'))
    pos++;
  
  gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Addr: %s, command prefix: %s", addr, pos);

  gavl_value_init(&val);
  dict = gavl_value_set_dictionary(&val);
  
  gavl_dictionary_set_string_nocopy(dict, GAVL_META_URI, addr);
  gavl_dictionary_set_string_nocopy(dict, META_COMMAND_SUFFIX, pos);

  gavl_array_splice_val_nocopy(&arr, -1, 0, &val);
  
  bg_cmdline_remove_arg(argc, _argv, arg);
  }

static bg_cmdline_arg_t global_options[] =
  {
    {
      .arg =         "-p",
      .help_arg =    "\"<addr> -opt1 -opt2 ...\"",
      .help_string = TRS("Append a remote address along with the frontends. This option can be used multiple times"),
      .callback =    opt_p,
    },
    { /* End of options */ }
  };


const bg_cmdline_app_data_t app_data =
  {
    .package =  PACKAGE,
    .version =  VERSION,
    .synopsis = TRS("[options]\n"),
    .help_before = TRS("Launch control proxies"),
    .args = (bg_cmdline_arg_array_t[]) { { TRS("Global options"), global_options },
                                         {  } },

  };

static int handle_msg(void * data, gavl_msg_t * msg)
  {
  //  fprintf(stderr, "Got message\n");
  //  gavl_msg_dump(msg, 2);

  switch(msg->NS)
    {
    case BG_MSG_NS_BACKEND:

      switch(msg->ID)
        {
        case BG_MSG_ADD_BACKEND:
          {
          int i;
          
          gavl_dictionary_t dict;
          const char * new_addr;
          const char * addr;
          const gavl_dictionary_t * p;
          
          gavl_dictionary_init(&dict);
          bg_msg_get_backend_info(msg, &dict);
          
          new_addr = gavl_dictionary_get_string(&dict, GAVL_META_URI);
          
          for(i = 0; i < arr.num_entries; i++)
            {
            if((p = gavl_value_get_dictionary(&arr.entries[i])) &&
               (addr = gavl_dictionary_get_string(p, GAVL_META_URI)) &&
               gavl_string_starts_with(new_addr, addr))
              {
              start_proc(new_addr,
                         gavl_dictionary_get_string(&dict, GAVL_META_LABEL),
                         gavl_dictionary_get_string(p, META_COMMAND_SUFFIX));
              /* Start only one proxy for each device */
              break;
              }
            }
          
          //          fprintf(stderr, "Added device: %s\n", );
          }

          break;
        case BG_MSG_DEL_BACKEND:
          stop_procs(gavl_msg_get_arg_string_c(msg, 0));
          
          //          fprintf(stderr, "Deleted device: %s\n", gavl_msg_get_arg_string_c(msg, 0));
          break;
        }

      
    }
  
  return 1;
  }

int main(int argc, char ** argv)
  {
  bg_msg_sink_t * sink;
  gavl_time_t delay_time = GAVL_TIME_SCALE / 50; // 20 ms
  int i;
  int res;
  
  setlocale(LC_ALL, "");
  setlocale(LC_NUMERIC, "C");

  gavl_array_init(&arr);
  
  bg_handle_sigint();

  bg_cmdline_init(&app_data);
  
  if(argc < 2)
    bg_cmdline_print_help(argv[0], 0);

  bg_cmdline_init(&app_data);

  bg_cmdline_parse(global_options, &argc, &argv, NULL);

  sink = bg_msg_sink_create(handle_msg, NULL, 0);


  bg_msg_hub_connect_sink(bg_backend_registry_get_evt_hub(), sink);
  
  
  while(1)
    {
    res = 0;
    
    if(bg_got_sigint())
      break;
    
    bg_msg_sink_iteration(sink);
    res += bg_msg_sink_get_num(sink);

    i = 0;
    
    while(i < num_procs)
      {
      if(bg_subprocess_done(procs[i].p))
        stop_proc(i);
      else
        i++;
      }

    if(!res)
      gavl_time_delay(&delay_time);
    }

  while(num_procs)
    stop_proc(0);
  
  }
