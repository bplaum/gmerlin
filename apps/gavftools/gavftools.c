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

#include <pthread.h>
#include <signal.h>
#include <string.h>

#include "gavftools.h"

#define LOG_DOMAIN "gavftools"

bg_plug_t * gavftools_out_plug  = NULL;
bg_plug_t * gavftools_in_plug  = NULL;

const gavl_dictionary_t * gavftools_media_info_in = NULL;
bg_controllable_t       * gavftools_ctrl_in = NULL;

int gavftools_multi_thread = 1;

char * gavftools_in_file  = "gavf://-";
char * gavftools_out_file = "gavf://-";

int gavftools_track = 0;

bg_mediaconnector_t gavftools_conn;

/* Quality parameters */

bg_cfg_section_t * aq_section = NULL;
bg_cfg_section_t * vq_section = NULL;

static gavl_dictionary_t stream_options;

bg_media_source_t * gavftools_src = NULL;


static const bg_parameter_info_t aq_params[] =
  {
    BG_GAVL_PARAM_CONVERSION_QUALITY,
    BG_GAVL_PARAM_RESAMPLE_MODE,
    BG_GAVL_PARAM_AUDIO_DITHER_MODE,
    { /* End */ },
  };

static const bg_parameter_info_t vq_params[] =
  {
    BG_GAVL_PARAM_CONVERSION_QUALITY,
    BG_GAVL_PARAM_DEINTERLACE,
    BG_GAVL_PARAM_SCALE_MODE,
    BG_GAVL_PARAM_RESAMPLE_CHROMA,
    BG_GAVL_PARAM_ALPHA,
    BG_GAVL_PARAM_BACKGROUND,
    { /* End */ },
  };

/* Conversion options */

bg_gavl_audio_options_t gavltools_aopt;
bg_gavl_video_options_t gavltools_vopt;


int gavftools_stop()
  {
  return bg_got_sigint();
  }

void gavftools_init()
  {
  bg_plugins_init();

  bg_mediaconnector_init(&gavftools_conn);  
  
  if(bg_plugin_registry_changed(bg_plugin_reg))
    {
    gavl_log(GAVL_LOG_INFO, LOG_DOMAIN, "Plugin registry changed");
    bg_cfg_registry_save();
    }

  bg_handle_sigint();

  /* Quality options */
  
  bg_gavl_audio_options_init(&gavltools_aopt);
  bg_gavl_video_options_init(&gavltools_vopt);

  aq_section = bg_cfg_section_create_from_parameters("aq", aq_params);
  vq_section = bg_cfg_section_create_from_parameters("vq", vq_params);
  gavl_dictionary_init(&stream_options);
  
  }


static bg_cfg_section_t * iopt_section = NULL;
static bg_cfg_section_t * oopt_section = NULL;


static char ** meta_fields = NULL;
int num_meta_fields = 0;

void gavftools_cleanup(int save_regs)
  {
  if(gavftools_in_plug)
    bg_plug_destroy(gavftools_in_plug);
  
  if(gavftools_out_plug)
    bg_plug_destroy(gavftools_out_plug);

  
  bg_plugins_cleanup();
  bg_cfg_registry_cleanup();
  

  if(iopt_section)
    bg_cfg_section_destroy(iopt_section);
  if(oopt_section)
    bg_cfg_section_destroy(oopt_section);
  
  if(meta_fields)
    free(meta_fields);

  bg_gavl_audio_options_free(&gavltools_aopt);
  bg_gavl_video_options_free(&gavltools_vopt);

  if(aq_section)
    bg_cfg_section_destroy(aq_section);
  if(vq_section)
    bg_cfg_section_destroy(vq_section);

  xmlCleanupParser();
 
  }


bg_cfg_section_t * gavftools_iopt_section(void)
  {
  if(!iopt_section)
    {
    iopt_section =
      bg_cfg_section_create_from_parameters("iopt",
                                            bg_plug_get_input_parameters());
    }
  return iopt_section;
  }

bg_cfg_section_t * gavftools_oopt_section(void)
  {
  if(!oopt_section)
    {
    oopt_section =
      bg_cfg_section_create_from_parameters("oopt",
                                            bg_plug_get_output_parameters());
    }
  return oopt_section;
  }


static void set_aq_parameter(void * data, const char * name,
                             const gavl_value_t * val)
  {
  bg_gavl_audio_set_parameter(&gavltools_aopt, name, val);
  }

static void set_vq_parameter(void * data, const char * name,
                             const gavl_value_t * val)
  {
  bg_gavl_video_set_parameter(&gavltools_vopt, name, val);
  }


void
gavftools_opt_aq(void * data, int * argc, char *** _argv, int arg)
  {
  if(arg >= *argc)
    {
    fprintf(stderr, "Option -aq requires an argument\n");
    exit(-1);
    }
  if(!bg_cmdline_apply_options(aq_section,
                               set_aq_parameter,
                               NULL,
                               aq_params,
                               (*_argv)[arg]))
    exit(-1);
  bg_cmdline_remove_arg(argc, _argv, arg);
  }

void
gavftools_opt_vq(void * data, int * argc, char *** _argv, int arg)
  {
  if(arg >= *argc)
    {
    fprintf(stderr, "Option -vq requires an argument\n");
    exit(-1);
    }
  if(!bg_cmdline_apply_options(vq_section,
                               set_vq_parameter,
                               NULL,
                               vq_params,
                               (*_argv)[arg]))
    exit(-1);
  bg_cmdline_remove_arg(argc, _argv, arg);
  }


void
gavftools_opt_iopt(void * data, int * argc, char *** _argv, int arg)
  {
  if(arg >= *argc)
    {
    fprintf(stderr, "Option -iopt requires an argument\n");
    exit(-1);
    }
  if(!bg_cmdline_apply_options(gavftools_iopt_section(),
                               NULL,
                               NULL,
                               bg_plug_get_input_parameters(),
                               (*_argv)[arg]))
    exit(-1);
  bg_cmdline_remove_arg(argc, _argv, arg);
  }

void
gavftools_opt_oopt(void * data, int * argc, char *** _argv, int arg)
  {
  if(arg >= *argc)
    {
    fprintf(stderr, "Option -oopt requires an argument\n");
    exit(-1);
    }
  if(!bg_cmdline_apply_options(gavftools_oopt_section(),
                               NULL,
                               NULL,
                               bg_plug_get_output_parameters(),
                               (*_argv)[arg]))
    exit(-1);
  bg_cmdline_remove_arg(argc, _argv, arg);
  }

void
gavftools_opt_m(void * data, int * argc, char *** _argv, int arg)
  {
  if(arg >= *argc)
    {
    fprintf(stderr, "Option -m requires an argument\n");
    exit(-1);
    }
  meta_fields = realloc(meta_fields, (num_meta_fields+1) * sizeof(*meta_fields));
  meta_fields[num_meta_fields] = (*_argv)[arg];
  num_meta_fields++;
  bg_cmdline_remove_arg(argc, _argv, arg);
  }


void
gavftools_block_sigpipe(void)
  {
  signal(SIGPIPE, SIG_IGN);
#if 0
  sigset_t newset;
  /* Block SIGPIPE */
  sigemptyset(&newset);
  sigaddset(&newset, SIGPIPE);
  pthread_sigmask(SIG_BLOCK, &newset, NULL);
#endif
  }

/* Stream actions */
#if 0
static bg_stream_action_t * stream_actions_from_arg(const char * arg, int * num_p)
  {
  int i;
  char ** actions_c;
  bg_stream_action_t * ret;
  int num;

  actions_c = gavl_strbreak(arg, ',');
  num = 0;
  while(actions_c[num])
    num++;

  ret = calloc(num, sizeof(*ret));

  for(i = 0; i < num; i++)
    {
    if(*(actions_c[i]) == 'd')
      ret[i] = BG_STREAM_ACTION_DECODE;
    else if(*(actions_c[i]) == 'm')
      ret[i] = BG_STREAM_ACTION_OFF;
    else if(*(actions_c[i]) == 'c')
      ret[i] = BG_STREAM_ACTION_READRAW;
    }
  *num_p = num;
  gavl_strbreak_free(actions_c);
  return ret;
  }
#endif

static void
stream_option_idx(const char * opt, void * data, int idx, int * argc, char *** _argv, int arg)
  {
  gavl_dictionary_t * dict;
  char * name;
  
  if(arg >= *argc)
    {
    fprintf(stderr, "Option -%s requires an argument\n", opt);
    exit(-1);
    }

  
  dict = gavl_dictionary_get_dictionary_create(&stream_options, opt);

  name = gavl_sprintf("%d", idx);
  gavl_dictionary_set_string(dict, name, (*_argv)[arg]);
  free(name);
  
  bg_cmdline_remove_arg(argc, _argv, arg);
  }

static void
stream_option(const char * opt, void * data, int * argc, char *** _argv, int arg)
  {
  gavl_dictionary_t * dict;
  if(arg >= *argc)
    {
    fprintf(stderr, "Option -%s requires an argument\n", opt);
    exit(-1);
    }

  dict = gavl_dictionary_get_dictionary_create(&stream_options, opt);

  gavl_dictionary_set_string(dict, "default", (*_argv)[arg]);
  
  bg_cmdline_remove_arg(argc, _argv, arg);
  }

void
gavftools_opt_as(void * data, int * argc, char *** _argv, int arg)
  {
  stream_option("as", data, argc, _argv, arg);
  }

void
gavftools_opt_as_idx(void * data, int idx, int * argc, char *** _argv, int arg)
  {
  stream_option_idx("as", data, idx, argc, _argv, arg);
  }

void
gavftools_opt_vs(void * data, int * argc, char *** _argv, int arg)
  {
  if(arg >= *argc)
    {
    fprintf(stderr, "Option -vs requires an argument\n");
    exit(-1);
    }

  bg_cmdline_remove_arg(argc, _argv, arg);
  
  }

void
gavftools_opt_vs_idx(void * data, int idx, int * argc, char *** _argv, int arg)
  {
  char * name;
  if(arg >= *argc)
    {
    fprintf(stderr, "Option -vs requires an argument\n");
    exit(-1);
    }
  name = bg_sprintf("vs-%d", idx);
  gavl_dictionary_set_string(&stream_options, name, (*_argv)[arg]);
  free(name);

  bg_cmdline_remove_arg(argc, _argv, arg);
  
  }

void
gavftools_opt_os(void * data, int * argc, char *** _argv, int arg)
  {
  if(arg >= *argc)
    {
    fprintf(stderr, "Option -os requires an argument\n");
    exit(-1);
    }

  bg_cmdline_remove_arg(argc, _argv, arg);

  }

void
gavftools_opt_os_idx(void * data, int idx, int * argc, char *** _argv, int arg)
  {
  char * name;
  if(arg >= *argc)
    {
    fprintf(stderr, "Option -os requires an argument\n");
    exit(-1);
    }
  name = bg_sprintf("os-%d", idx);
  gavl_dictionary_set_string(&stream_options, name, (*_argv)[arg]);
  free(name);
  
  bg_cmdline_remove_arg(argc, _argv, arg);

  }

void
gavftools_opt_ts(void * data, int * argc, char *** _argv, int arg)
  {
  if(arg >= *argc)
    {
    fprintf(stderr, "Option -ts requires an argument\n");
    exit(-1);
    }
  
  bg_cmdline_remove_arg(argc, _argv, arg);
  }

void
gavftools_opt_ts_idx(void * data, int idx, int * argc, char *** _argv, int arg)
  {
  char * name;
  if(arg >= *argc)
    {
    fprintf(stderr, "Option -ts requires an argument\n");
    exit(-1);
    }
  name = bg_sprintf("ts-%d", idx);
  gavl_dictionary_set_string(&stream_options, name, (*_argv)[arg]);
  free(name);

  bg_cmdline_remove_arg(argc, _argv, arg);
  }

#if 0
bg_stream_action_t *
gavftools_get_stream_actions(int num, gavl_stream_type_t type)
  {
  int i;
  int num_actions;
  bg_stream_action_t * actions;
  bg_stream_action_t * ret;
  bg_stream_action_t def_action = BG_STREAM_ACTION_READRAW;
  
  if(!num)
    return NULL;
  
  switch(type)
    {
    case GAVL_STREAM_AUDIO:
      actions = a_actions;
      num_actions = num_a_actions;
      break;
    case GAVL_STREAM_VIDEO:
      actions = v_actions;
      num_actions = num_v_actions;
      break;
    case GAVL_STREAM_TEXT:
      actions = t_actions;
      num_actions = num_t_actions;
      break;
    case GAVL_STREAM_OVERLAY:
      actions = o_actions;
      num_actions = num_o_actions;
      break;
    default:
      return NULL;
    }

  if(!num_actions)
    {
    ret = calloc(num, sizeof(*ret));
    for(i = 0; i < num; i++)
      ret[i] = def_action;
    return ret;
    }
  
  ret = calloc(num, sizeof(*ret));
  
  i = num;
  if(i > num_actions)
    i = num_actions;

  memcpy(ret, actions, i * sizeof(*actions));

  if(num > num_actions)
    {
    for(i = num_actions; i < num; i++)
      ret[i] = BG_STREAM_ACTION_OFF;
    }
  return ret;
  }
#endif

static const char * get_stream_option(int idx, const char * option)
  {
  const gavl_dictionary_t * dict;
  
  const char * var;
  char * name;
  
  dict = gavl_dictionary_get_dictionary(&stream_options, option);
  if(!dict)
    return NULL;

  name = bg_sprintf("%d", idx+1);
  var = gavl_dictionary_get_string(&stream_options, name);
  free(name);
  
  if(var)
    return var;
  
  return
    gavl_dictionary_get_string(dict, "default");
  }

bg_stream_action_t gavftools_get_stream_action(gavl_stream_type_t type, int idx)
  {
  const char * var;
  const char * option = NULL;
  
  switch(type)
    {
    case GAVL_STREAM_AUDIO:
      option = "as";
      break;
    case GAVL_STREAM_VIDEO:
      option = "vs";
      break;
    case GAVL_STREAM_TEXT:
      option = "ts";
      break;
    case GAVL_STREAM_OVERLAY:
      option = "os";
      break;
    case GAVL_STREAM_MSG:
    default:
      return BG_STREAM_ACTION_DECODE;
      break;
    }
  
  var = get_stream_option(idx, option);
  
  if(!var)
    return BG_STREAM_ACTION_OFF;

  switch(var[0])
    {
    case 'd':
      return BG_STREAM_ACTION_DECODE;
      break;
    case 'c':
      return  BG_STREAM_ACTION_READRAW;
      break;
    case 'm':
    default:
      return BG_STREAM_ACTION_OFF;
      break;
    }
  
  }
  
static void set_stream_action(bg_media_source_t * src,
                              bg_media_source_stream_t * s, int idx, const char * option)
  {
  bg_stream_action_t act;
  
  const char * opt = get_stream_option(idx, option);

  if(!opt)
    opt = "c";
  
  switch(opt[0])
    {
    case 'd':
      act = BG_STREAM_ACTION_DECODE;
      break;
    case 'c':
      act = BG_STREAM_ACTION_READRAW;
      break;
    case 'm':
    default:
      act = BG_STREAM_ACTION_OFF;
      break;
    }
  
  switch(s->type)
    {
    case GAVL_STREAM_AUDIO:
    case GAVL_STREAM_VIDEO:
    case GAVL_STREAM_OVERLAY:
      if(act == BG_STREAM_ACTION_READRAW)
        {
        if(!gavl_stream_get_compression_info(s->s, NULL))
          act = BG_STREAM_ACTION_DECODE;
        }
      break;
    default:
      break;
    }

  bg_media_source_set_stream_action(src, s->type, idx, act);
  }
  
void gavftools_set_stream_actions(bg_media_source_t * src)
  {
  int i;
  int num;

  num = bg_media_source_get_num_streams(src, GAVL_STREAM_AUDIO);
  for(i = 0; i < num; i++)
    set_stream_action(src, bg_media_source_get_audio_stream(src, i), i, "as");
  
  num = bg_media_source_get_num_streams(src, GAVL_STREAM_VIDEO);
  for(i = 0; i < num; i++)
    set_stream_action(src, bg_media_source_get_video_stream(src, i), i, "vs");

  num = bg_media_source_get_num_streams(src, GAVL_STREAM_TEXT);
  for(i = 0; i < num; i++)
    set_stream_action(src, bg_media_source_get_text_stream(src, i), i, "ts");
  
  num = bg_media_source_get_num_streams(src, GAVL_STREAM_OVERLAY);
  for(i = 0; i < num; i++)
    set_stream_action(src, bg_media_source_get_overlay_stream(src, i), i, "os");
  
  }



bg_plug_t * gavftools_create_in_plug()
  {
  bg_plug_t * in_plug;
  in_plug = bg_plug_create_reader();
  bg_cfg_section_apply(gavftools_iopt_section(),
                       bg_plug_get_input_parameters(),
                       bg_plug_set_parameter,
                       in_plug);

  /* Initialize messages */
  bg_plug_get_controllable(in_plug);
  
  return in_plug;
  }

bg_plug_t * gavftools_create_out_plug()
  {
  bg_plug_t * out_plug;
  out_plug = bg_plug_create_writer();
  bg_cfg_section_apply(gavftools_oopt_section(),
                       bg_plug_get_output_parameters(),
                       bg_plug_set_parameter,
                       out_plug);

  /* Initialize messages */
  //  bg_plug_get_control(out_plug);
  
  return out_plug;
  }

void gavftools_set_cmdline_parameters(bg_cmdline_arg_t * args)
  {
  bg_cmdline_arg_set_parameters(args, "-iopt",
                                bg_plug_get_input_parameters());
  bg_cmdline_arg_set_parameters(args, "-oopt",
                                bg_plug_get_output_parameters());
  bg_cmdline_arg_set_parameters(args, "-aq", aq_params);
  bg_cmdline_arg_set_parameters(args, "-vq", aq_params);
  }


int gavftools_open_out_plug_from_in_plug(bg_plug_t * out_plug,
                                         const char * name,
                                         bg_plug_t * in_plug)
  {
  /* TODO: Fix this */
  //  gavl_dictionary_t m;
  //  gavl_dictionary_init(&m);
  //  gavl_dictionary_copy(&m, bg_plug_get_metadata(in_plug));
  //  gavftools_set_output_metadata(&m);
  if(!name)
    name = gavftools_out_file;
  if(!bg_plug_open_location(out_plug, name))
    {
    //    gavl_dictionary_free(&m);
    return 0;
    }
  // bg_plug_transfer_messages(in_plug, out_plug);
  
  //  gavl_dictionary_free(&m);
  return 1;
  }

#if 0
static void set_stream_actions(bg_plug_t * in_plug, gavl_stream_type_t type)
  {
  int num, i;
  bg_stream_action_t * actions = NULL;
  bg_media_source_t * s = bg_plug_get_source(in_plug);
  
  num = gavl_track_get_num_streams(s->track, type);
  
  if(!num)
    return;
  
  actions = gavftools_get_stream_actions(num, type);

  for(i = 0; i < num; i++)
    bg_media_source_set_stream_action(s, type, i, actions[i]);

  
  
  if(actions)
    free(actions);
  }

void gavftools_set_stream_actions(bg_plug_t * p)
  {
  bg_media_source_t * s = bg_plug_get_source(p);

  set_stream_actions(p, GAVL_STREAM_AUDIO);
  set_stream_actions(p, GAVL_STREAM_VIDEO);
  set_stream_actions(p, GAVL_STREAM_TEXT);
  set_stream_actions(p, GAVL_STREAM_OVERLAY);

  bg_media_source_set_stream_action(s, GAVL_STREAM_MSG, 0, BG_STREAM_ACTION_DECODE);
  // set_stream_actions(p, GAVL_STREAM_MSG);
  }
#endif

int gavftools_open_input(bg_plug_t * in_plug, const char * ifile)
  {
  int ret = 0;
  
  if(!bg_plug_open_location(in_plug, ifile))
    goto fail;

  if(!(gavftools_media_info_in = bg_plug_get_media_info(in_plug)))
    goto fail;

  //  fprintf(stderr, "gavftools_open_input\n");
  //  gavl_dictionary_dump(dict, 2);

  gavftools_ctrl_in = bg_plug_get_controllable(in_plug);
  
  ret = 1;
  
  fail:
  
  return ret;
  }

static void send_start_messages(gavl_handle_msg_func func,
                                void * func_data)
  {
  gavl_msg_t msg;

  gavl_msg_init(&msg);
  gavl_msg_set_id_ns(&msg, GAVL_CMD_SRC_SELECT_TRACK, GAVL_MSG_NS_SRC);
  gavl_msg_set_arg_int(&msg, 0, 0);
  func(func_data, &msg);
  gavl_msg_free(&msg);

  /* Select all tracks */
  
  
  gavl_msg_init(&msg);
  gavl_msg_set_id_ns(&msg, GAVL_CMD_SRC_START, GAVL_MSG_NS_SRC);
  func(func_data, &msg);
  gavl_msg_free(&msg);
  
  }

#define STATE_IDLE    1
#define STATE_RUNNING 2

static void handle_msg(gavl_handle_msg_func func,
                       void * func_data, gavl_msg_t * msg)
  {
  switch(msg->NS)
    {
    case GAVL_MSG_NS_SRC:
      {
      switch(msg->ID)
        {
        case GAVL_CMD_SRC_SELECT_TRACK:
          {
          if(gavftools_multi_thread)
            bg_mediaconnector_create_threads(&gavftools_conn, 1);

          /* Send to input */
          if(gavftools_ctrl_in)
            bg_msg_sink_put_copy(gavftools_ctrl_in->cmd_sink, msg);
          
          func(func_data, msg);

          if(gavftools_in_plug)
            gavftools_src = bg_plug_get_source(gavftools_in_plug);
          
          /* TODO: Reset out plug */
          
          
          
          }
          break;
        case GAVL_CMD_SRC_START:
          {
          
          /* Send to input */
          if(gavftools_ctrl_in)
            bg_msg_sink_put_copy(gavftools_ctrl_in->cmd_sink, msg);
          
          func(func_data, msg);

          /*
              gavftools_src should have it's final value now.
              Initialize connector
           */
          
          if(gavftools_out_plug)
            {
            if(!bg_plug_start_program(gavftools_out_plug, gavftools_src))
              {
              
              }
            }
          
          if(gavftools_multi_thread)
            {
            bg_mediaconnector_create_threads(&gavftools_conn, 1);
            bg_mediaconnector_threads_init_separate(&gavftools_conn);
            bg_mediaconnector_threads_start(&gavftools_conn);
            }
          
          }
          break;
        case GAVL_CMD_SRC_SEEK:
          {
          /* Send to input */
          if(gavftools_ctrl_in)
            bg_msg_sink_put_copy(gavftools_ctrl_in->cmd_sink, msg);
          
          func(func_data, msg);
          /* TODO: Reset out plug */
          
          }
          break;
        case GAVL_CMD_SRC_PAUSE:
          {
          
          if(gavftools_multi_thread)
            {
            
            }
          
          }
          break;
        case GAVL_CMD_SRC_RESUME:
          {
          
          
          }
          break;
        }
      }
    }
  }

/*
 *   Run modes:
 *   * Multi-threaded and interactive
 *   * Single threaded and non-interactive.
 *   
 */

static void run_singlethread(gavl_handle_msg_func func,
                             void * func_data)
  {
  send_start_messages(func, func_data);

  while(1)
    {
    
    }

  }

static void run_multithread(gavl_handle_msg_func func,
                            void * func_data)
  {
  gavf_io_t * io = NULL;
  gavf_t * g;
  
  if(gavftools_out_plug &&
     (g = bg_plug_get_gavf(gavftools_out_plug)))
    io = gavf_get_io(g);
  
  while(1)
    {
    if(gavftools_stop())
      return;

    if(gavf_io_can_read(io, 50))
      {
      gavl_msg_t msg;
      gavl_msg_init(&msg);

      if(!gavl_msg_read(&msg, io))
        {
        gavl_msg_free(&msg);
        return;
        }
          
      handle_msg(func, func_data, &msg);
          
      gavl_msg_free(&msg);
      }

    
    }
  }

void gavftools_run(gavl_handle_msg_func func,
                   void * func_data)
  {
  gavf_t * g;
  gavf_io_t * io;

  if(gavftools_out_plug && gavftools_media_info_in)
    bg_plug_set_media_info(gavftools_out_plug, gavftools_media_info_in);
  
  if(gavftools_out_plug &&
     (g = bg_plug_get_gavf(gavftools_out_plug)) &&
     (io = gavf_get_io(g)))
    {
    int flags = gavf_io_get_flags(io);
    
    if(flags & GAVF_IO_IS_REGULAR)
      gavftools_multi_thread = 0;
    }
  
  if(!gavftools_multi_thread)
    run_singlethread(func,  func_data);
  else
    run_multithread(func,  func_data);

#if 0  
    {
    
    while(1)
      {
      if(gavftools_stop())
        return;
      
      }
    }
  else
    {
    /* Multi thread */
    
    if(!read_msg) // Batch
      {
      send_start_messages(func, func_data);

      while(1)
        {
        if(gavftools_stop())
          return;
        }
      }

    else
      {
      /* Interactive */

      while(1)
        {
        
        }

      }

    }
#endif  
  }
