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

#include <config.h>
#include <gtk/gtk.h>
#include <string.h>
#include <stdio.h>

#include <gmerlin/cfg_dialog.h>
#include <gmerlin/cmdline.h>
#include <gmerlin/iconfont.h>
#include <gmerlin/application.h>

#include <gui_gtk/gtkutils.h>


#define PARAMETER_FLAGS BG_PARAMETER_SYNC

static const bg_parameter_info_t multimenu_1_info[] =
  {
    {
      .name =      "multimenu_1_checkbutton_1",
      .long_name = "Multimenu 1 Checkbutton 1",
      .type =      BG_PARAMETER_CHECKBUTTON,
      .flags =     PARAMETER_FLAGS,
    },
    {
      .name =      "multimenu_1_checkbutton_2",
      .long_name = "Multimenu 1 Checkbutton 2",
      .type =      BG_PARAMETER_CHECKBUTTON,
      .flags =     PARAMETER_FLAGS,
    },
    { /* End of Parameters */ }
  };

static const bg_parameter_info_t multimenu_2_info[] =
  {
    {
      .name =      "multimenu_2_checkbutton_1",
      .long_name = "Multimenu 2 Checkbutton 1",
      .type =      BG_PARAMETER_CHECKBUTTON,
      .flags =     PARAMETER_FLAGS,
    },
    {
      .name =      "multimenu_2_checkbutton_2",
      .long_name = "Multimenu 2 Checkbutton 2",
      .type =      BG_PARAMETER_CHECKBUTTON,
      .flags =     PARAMETER_FLAGS,
    },
    { /* End of Parameters */ }
  };

static const bg_parameter_info_t multilist_1_info[] =
  {
    {
      .name =      "multilist_1_checkbutton_1",
      .long_name = "Multilist 1 Checkbutton 1",
      .type =      BG_PARAMETER_CHECKBUTTON,
      .flags =     PARAMETER_FLAGS,
    },
    {
      .name =      "multilist_1_checkbutton_2",
      .long_name = "Multilist 1 Checkbutton 2",
      .type =      BG_PARAMETER_CHECKBUTTON,
      .flags =     PARAMETER_FLAGS,
    },
    { /* End of Parameters */ }
  };

static const bg_parameter_info_t multilist_2_info[] =
  {
    {
      .name =      "multilist_2_checkbutton_1",
      .long_name = "Multilist 2 Checkbutton 1",
      .type =      BG_PARAMETER_CHECKBUTTON,
      .flags =     PARAMETER_FLAGS,
    },
    {
      .name =      "multilist_2_checkbutton_2",
      .long_name = "Multilist 2 Checkbutton 2",
      .type =      BG_PARAMETER_CHECKBUTTON,
      .flags =     PARAMETER_FLAGS,
    },
    { /* End of Parameters */ }
  };

static const bg_parameter_info_t multichain_1_info[] =
  {
    {
      .name =      "multichain_1_checkbutton_1",
      .long_name = "Multichain 1 Checkbutton 1",
      .type =      BG_PARAMETER_CHECKBUTTON,
      .flags =     PARAMETER_FLAGS,
    },
    {
      .name =      "multichain_1_checkbutton_2",
      .long_name = "Multichain 1 Checkbutton 2",
      .type =      BG_PARAMETER_CHECKBUTTON,
      .flags =     PARAMETER_FLAGS,
    },
    { /* End of Parameters */ }
  };

static const bg_parameter_info_t multichain_2_info[] =
  {
    {
      .name =      "multichain_2_checkbutton_1",
      .long_name = "Multichain 2 Checkbutton 1",
      .type =      BG_PARAMETER_CHECKBUTTON,
      .flags =     PARAMETER_FLAGS,
    },
    {
      .name =      "multichain_2_checkbutton_2",
      .long_name = "Multichain 2 Checkbutton 2",
      .type =      BG_PARAMETER_CHECKBUTTON,
      .flags =     PARAMETER_FLAGS,
    },
    { /* End of Parameters */ }
  };

static const bg_parameter_info_t * multilist_parameters[] =
  {
    multilist_1_info,
    multilist_2_info,
    NULL
  };

static const bg_parameter_info_t * multimenu_parameters[] =
  {
    multimenu_1_info,
    multimenu_2_info,
    NULL
  };

static const bg_parameter_info_t * multichain_parameters[] =
  {
    multichain_1_info,
    multichain_2_info,
    NULL
  };

static bg_parameter_info_t info_1[] =
  {
#if 0
    {
      .name =      "section_1",
      .long_name = "Section 1",
      .type =      BG_PARAMETER_SECTION
    },
#endif
    {
      .name =        "checkbutton",
      .long_name =   "Check Button",
      .type =        BG_PARAMETER_CHECKBUTTON,
      .flags =     PARAMETER_FLAGS,
      .val_default = GAVL_VALUE_INIT_INT(1),
      .help_string = "Checkbutton help",
      .preset_path = "test1",
    },
    {
      .name =        "button",
      .long_name =   "Button",
      .type =        BG_PARAMETER_BUTTON,
      .flags =     PARAMETER_FLAGS,
      .val_default = GAVL_VALUE_INIT_INT(1),
      .help_string = "Button help"
    },
    {
      .name =      "spinbutton_float",
      .long_name = "Floating point Spinbutton",
      .type =      BG_PARAMETER_FLOAT,
      .flags =     PARAMETER_FLAGS,
      .val_default = GAVL_VALUE_INIT_FLOAT(250.0),
      .val_min =     GAVL_VALUE_INIT_FLOAT(200.0),
      .val_max =     GAVL_VALUE_INIT_FLOAT(300.0),
      .num_digits = 3,
      .help_string = "Floating point Spinbutton help",
    },
    {
      .name =      "spinbutton_int",
      .long_name = "Integer Spinbutton",
      .type =      BG_PARAMETER_INT,
      .flags =     PARAMETER_FLAGS,
      .val_default = GAVL_VALUE_INIT_INT(250),
      .val_min =     GAVL_VALUE_INIT_INT(200),
      .val_max =     GAVL_VALUE_INIT_INT(300),
      .help_string = "Integer Spinbutton help",
    },
    {
      .name =      "time",
      .long_name = "Time",
      .type =      BG_PARAMETER_TIME,
      .flags =     PARAMETER_FLAGS,
      .help_string = "Time help",
    },
    {
      .name =        "slider_float",
      .long_name =   "Floating point Slider",
      .type =        BG_PARAMETER_SLIDER_FLOAT,
      .flags =     PARAMETER_FLAGS,
      .val_default = GAVL_VALUE_INIT_FLOAT(250.0),
      .val_min =     GAVL_VALUE_INIT_FLOAT(1.0),
      .val_max =     GAVL_VALUE_INIT_FLOAT(300.0),
      .num_digits =  1,
      .help_string = "Floating point Slider help",
    },
    {
      .name =        "slider_int",
      .long_name =   "Integer Slider",
      .type =        BG_PARAMETER_SLIDER_INT,
      .flags =     PARAMETER_FLAGS,
      .val_default = GAVL_VALUE_INIT_INT(250),
      .val_min =     GAVL_VALUE_INIT_INT(200),
      .val_max =     GAVL_VALUE_INIT_INT(300),
      .help_string =   "Integer Slider help",
    },
    {
      .name =      "string",
      .long_name = "String",
      .type =      BG_PARAMETER_STRING,
      .flags =     PARAMETER_FLAGS,
      .val_default = GAVL_VALUE_INIT_STRING("Some string"),
      .help_string =   "String help",
    },
    {
      .name =      "string_hidden",
      .long_name = "String (hidden)",
      .type =      BG_PARAMETER_STRING_HIDDEN,
      .flags =     PARAMETER_FLAGS,
      .val_default = GAVL_VALUE_INIT_STRING("Some string (hidden)"),
      .help_string =   "String hidden help",
    },
    {
      .name =        "stringlist",
      .long_name =   "Stringlist",
      .type =        BG_PARAMETER_STRINGLIST,
      .flags =     PARAMETER_FLAGS,
      .val_default = GAVL_VALUE_INIT_STRING("option_2"),
      .multi_names =  (char const *[]){ "option_1",
                                        "option_2",
                                        "option_3",
                                        NULL },
      .multi_labels = (char const *[]){ "Option 1",
                                        "Option 2",
                                        "Option 3",
                                        NULL },
      .help_string =   "Stringlist help"
    },
    { /* End of parameters */ }
  };
    
static bg_parameter_info_t info_2[] =
  {
#if 0
    {
      .name =      "section_2",
      .long_name = "Section 2",
      .type =      BG_PARAMETER_SECTION
    },
#endif
    {
      .name =      "color_rgb",
      .long_name = "Color RGB",
      .type =      BG_PARAMETER_COLOR_RGB,
      .flags =     PARAMETER_FLAGS,
      .val_default = GAVL_VALUE_INIT_COLOR_RGB(0.0, 1.0, 0.0),
      .help_string =   "Color RGB help",
      .preset_path = "test2",
    },
    {
      .name =      "color_rgba",
      .long_name = "Color RGBA",
      .type =      BG_PARAMETER_COLOR_RGBA,
      .flags =     PARAMETER_FLAGS,
      .val_default = GAVL_VALUE_INIT_COLOR_RGBA(0.0, 1.0, 0.0, 0.5),
      .help_string =   "Color RGBA help",
    },
    {
      .name =      "position",
      .long_name = "Position",
      .type =      BG_PARAMETER_POSITION,
      .flags =     PARAMETER_FLAGS,
      .val_default = GAVL_VALUE_INIT_POSITION(0.5, 0.5),
      .help_string =   "Position help",
      .num_digits = 2,
    },
    {
      .name =        "file",
      .long_name =   "File",
      .type =        BG_PARAMETER_FILE,
      .flags =     PARAMETER_FLAGS,
      .val_default = GAVL_VALUE_INIT_STRING("/usr/include/stdio.h"),
      .help_string =   "File help",
    },
    {
      .name =        "directory",
      .long_name =   "Directory",
      .type =        BG_PARAMETER_DIRECTORY,
      .flags =     PARAMETER_FLAGS,
      .val_default = GAVL_VALUE_INIT_STRING("/usr/local"),
      .help_string =   "Directory help",
    },
    {
      .name =      "font",
      .long_name = "Font",
      .type =      BG_PARAMETER_FONT,
      .flags =     PARAMETER_FLAGS,
      .val_default = GAVL_VALUE_INIT_STRING("Sans 12"),
      .help_string =   "Font help",
    },
    { /* End of parameters */ }
  };


static bg_parameter_info_t info_3[] =
  {
#if 0
    {
      .name =      "section_3",
      .long_name = "Section 3",
      .type =      BG_PARAMETER_SECTION
    },
#endif
    {
      .name =               "multimenu",
      .long_name =          "Multimenu",
      .type =               BG_PARAMETER_MULTI_MENU,
      .flags =     PARAMETER_FLAGS,
      .multi_names =        (char const *[]){ "multimenu_1", "multimenu_2", NULL },
      .multi_labels =   (char const *[]){ "Multimenu 1", "Multimenu 2", NULL },
      .multi_descriptions = (char const *[]){ "Multimenu 1", "Multimenu 2", NULL },
      .multi_parameters =   multimenu_parameters,
      .help_string =   "Multimenu help",
      .preset_path = "test3",
    },
#if 1
    {
      .name =               "multilist",
      .long_name =          "Multilist",
      .type =               BG_PARAMETER_MULTI_LIST,
      .flags =     PARAMETER_FLAGS,
      .val_default = GAVL_VALUE_INIT_STRING("multilist_1,multilist_2"),
      .multi_names =        (char const *[]){ "multilist_1", "multilist_2", NULL },
      .multi_labels =   (char const *[]){ "Multilist 1", "Multilist 2", NULL },
      .multi_descriptions = (char const *[]){ "Multilist 1", "Multilist 2", NULL },
      .multi_parameters =   multilist_parameters,
      .help_string =   "Multilist help",
    },
#endif
    { /* End of parameters */ }
  };

static bg_parameter_info_t info_4[] =
  {
    {
      .name =               "multichain",
      .long_name =          "Multichain",
      .type =               BG_PARAMETER_MULTI_CHAIN,
      .flags =              PARAMETER_FLAGS,
      .multi_names =        (char const *[]){ "multichain_1", "multichain_2", NULL },
      .multi_labels =       (char const *[]){ "Multichain 1", "Multichain 2", NULL },
      .multi_descriptions = (char const *[]){ "Multichain 1", "Multichain 2", NULL },
      .multi_parameters =   multichain_parameters,
      .help_string =        "Multichain help",
      .preset_path =        "test4",
    },
    { /* End of parameters */ }
  };

static bg_parameter_info_t info_5[] =
  {
    {
      .name =               "dirlist",
      .long_name =          "Dirlist",
      .type =               BG_PARAMETER_DIRLIST,
      .flags =              PARAMETER_FLAGS,
      .help_string =        "Directory list",
      .preset_path =        "test5",
    },
    { /* End of parameters */ }
  };

static void set_parameter(void * data, const char * name,
                          const gavl_value_t * v)
  {
  char time_buf[GAVL_TIME_STRING_LEN];

  if(!name)
    {
    fprintf(stderr, "NULL Parameter name\n");
    return;
    }

  if(!v)
    {
    fprintf(stderr, "NULL Parameter\n");
    return;
    }
  
  switch(v->type)
    {
    case GAVL_TYPE_INT:
      fprintf(stderr, "Integer value %s: %d\n", name, v->v.i);
      break;
    case GAVL_TYPE_LONG:
      gavl_time_prettyprint(v->v.l, time_buf);
      fprintf(stderr, "Long %s\n", time_buf);
      break;
    case GAVL_TYPE_FLOAT:
      fprintf(stderr, "Float value %s: %f\n", name, v->v.d);
      break;
    case GAVL_TYPE_COLOR_RGB:
    case GAVL_TYPE_COLOR_RGBA:
      fprintf(stderr, "Color %s: %f %f %f %f\n", name,
              v->v.color[0], v->v.color[1],
              v->v.color[2], v->v.color[3]);
      break;
    case GAVL_TYPE_POSITION:
      fprintf(stderr, "Position %s: %f %f\n", name, v->v.position[0], v->v.position[1]);
      break;
    case GAVL_TYPE_STRING:
      fprintf(stderr, "String %s: %s\n", name, v->v.str);
      break;
    case GAVL_TYPE_DICTIONARY:
      fprintf(stderr, "Dictionary %s\n", name);
      gavl_dictionary_dump(v->v.dictionary, 2);
      fprintf(stderr, "\n");
      break;
    case GAVL_TYPE_ARRAY:
      fprintf(stderr, "Array %s\n", name);
      gavl_array_dump(v->v.array, 2);
      fprintf(stderr, "\n");
      break;
    default:
      fprintf(stderr, "Unknown type\n");
      break;
    }
  }

static bg_cfg_ctx_t cfg_ctx[] =
  {
    {
      .p = info_1,
      .name = "ctx1",
      .set_param = set_parameter,
    },
    {
      .p = info_2,
      .name = "ctx2",
      .set_param = set_parameter,
    },
    {
      .p = info_3,
      .name = "ctx3",
      .set_param = set_parameter,
    },
    {
      .p = info_4,
      .name = "ctx4",
      .set_param = set_parameter,
    },
    {
      .p = info_5,
      .name = "ctx5",
      .set_param = set_parameter,
    },
    { /* End */ },
  };

static void opt_opt1(void * data, int * argc, char *** _argv, int arg)
  {
  if(arg >= *argc)
    {
    fprintf(stderr, "Option -opt1 requires an argument\n");
    exit(-1);
    }
  if(!bg_cmdline_apply_options(cfg_ctx[0].s,
                               set_parameter,
                               NULL,
                               info_1,
                               (*_argv)[arg]))
    exit(-1);
  bg_cmdline_remove_arg(argc, _argv, arg);
  }

static void opt_opt2(void * data, int * argc, char *** _argv, int arg)
  {
  if(arg >= *argc)
    {
    fprintf(stderr, "Option -opt2 requires an argument\n");
    exit(-1);
    }
  if(!bg_cmdline_apply_options(cfg_ctx[1].s,
                               set_parameter,
                               NULL,
                               info_2,
                               (*_argv)[arg]))
    exit(-1);
  bg_cmdline_remove_arg(argc, _argv, arg);
  }

static void opt_opt3(void * data, int * argc, char *** _argv, int arg)
  {
  if(arg >= *argc)
    {
    fprintf(stderr, "Option -opt3 requires an argument\n");
    exit(-1);
    }
  if(!bg_cmdline_apply_options(cfg_ctx[2].s,
                               set_parameter,
                               NULL,
                               info_3,
                               (*_argv)[arg]))
    exit(-1);
  bg_cmdline_remove_arg(argc, _argv, arg);
  }

static void opt_opt4(void * data, int * argc, char *** _argv, int arg)
  {
  if(arg >= *argc)
    {
    fprintf(stderr, "Option -opt4 requires an argument\n");
    exit(-1);
    }
  if(!bg_cmdline_apply_options(cfg_ctx[3].s,
                               set_parameter,
                               NULL,
                               info_4,
                               (*_argv)[arg]))
    exit(-1);
  bg_cmdline_remove_arg(argc, _argv, arg);
  }

static void opt_opt5(void * data, int * argc, char *** _argv, int arg)
  {
  if(arg >= *argc)
    {
    fprintf(stderr, "Option -opt5 requires an argument\n");
    exit(-1);
    }
  if(!bg_cmdline_apply_options(cfg_ctx[3].s,
                               set_parameter,
                               NULL,
                               info_5,
                               (*_argv)[arg]))
    exit(-1);
  bg_cmdline_remove_arg(argc, _argv, arg);
  }

static bg_cmdline_arg_t global_options[] =
  {
    {
      .arg =         "-opt1",
      .help_string = "Set Options 1",
      .callback =    opt_opt1,
      .parameters =  info_1,
    },
    {
      .arg =         "-opt2",
      .help_string = "Set Options 2",
      .callback =    opt_opt2,
      .parameters =  info_2,
    },
    {
      .arg =         "-opt3",
      .help_string = "Set Options 3",
      .callback =    opt_opt3,
      .parameters =  info_3,
    },
    {
      .arg =         "-opt4",
      .help_string = "Set Options 4",
      .callback =    opt_opt4,
      .parameters =  info_4,
    },
    {
      .arg =         "-opt5",
      .help_string = "Set Options 5",
      .callback =    opt_opt5,
      .parameters =  info_5,
    },
    { /* End of options */ }
  };


bg_cmdline_app_data_t app_data =
  {
    .package =  PACKAGE,
    .version =  VERSION,
    .synopsis = TRS("[Options]"),
    .help_before = TRS("Configure test\n"),
    .args = (bg_cmdline_arg_array_t[]) { { TRS("Options"),
                                           global_options },
                                       {  } },
  };

static int handle_msg(void * priv, gavl_msg_t * msg)
  {
  fprintf(stderr, "Dialog sent message:\n");
  gavl_msg_dump(msg, 2 );
  return 1;
  }

  
int main(int argc, char ** argv)
  {
  bg_msg_sink_t * sink;
  bg_dialog_t * test_dialog;
  bg_cfg_registry_t * registry;

  bg_app_init("cfgtest", "cfgtester");

  
  bg_iconfont_init();
  
  registry = gavl_dictionary_create();
  bg_cfg_registry_load(registry, "cfg.xml");

  cfg_ctx[0].s = bg_cfg_registry_find_section(registry, "section_1");
  cfg_ctx[1].s = bg_cfg_registry_find_section(registry, "section_2");
  cfg_ctx[2].s = bg_cfg_registry_find_section(registry, "section_3");
  cfg_ctx[3].s = bg_cfg_registry_find_section(registry, "section_4");
  cfg_ctx[4].s = bg_cfg_registry_find_section(registry, "section_5");

  cfg_ctx[0].long_name = "Section 1";
  cfg_ctx[1].long_name = "Section 2";
  cfg_ctx[2].long_name = "Section 3";
  cfg_ctx[3].long_name = "Section 4";
  cfg_ctx[4].long_name = "Section 5";
  
  bg_cmdline_parse(global_options, &argc, &argv, NULL);
  
  bg_gtk_init(&argc, &argv, NULL);
  
  //  test_dialog = bg_dialog_create(section, set_param, NULL, info, "Test dialog");

  test_dialog = bg_dialog_create_multi("Test dialog");

  sink = bg_msg_sink_create(handle_msg, NULL, 1);
  
  bg_dialog_add_ctx(test_dialog, &cfg_ctx[0]);
  bg_dialog_add_ctx(test_dialog, &cfg_ctx[1]);
  bg_dialog_add_ctx(test_dialog, &cfg_ctx[2]);
  bg_dialog_add_ctx(test_dialog, &cfg_ctx[3]);
  bg_dialog_add_ctx(test_dialog, &cfg_ctx[4]);
  
  bg_dialog_set_sink(test_dialog, sink);
  
  bg_dialog_show(test_dialog, NULL);

  /* Apply sections */
  fprintf(stderr, "*** Applying section ***\n");  
  bg_cfg_section_apply(cfg_ctx[0].s, info_1,set_parameter,NULL);
  bg_cfg_section_apply(cfg_ctx[0].s, info_2,set_parameter,NULL);
  bg_cfg_section_apply(cfg_ctx[0].s, info_3,set_parameter,NULL);
  bg_cfg_section_apply(cfg_ctx[0].s, info_4,set_parameter,NULL);
  bg_cfg_section_apply(cfg_ctx[0].s, info_5,set_parameter,NULL);
  
  bg_cfg_registry_save_to(registry, "cfg.xml");

  gavl_dictionary_destroy(registry);
  bg_dialog_destroy(test_dialog);
  
  return 0;
  }
                      
