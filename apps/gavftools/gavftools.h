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
#include <gmerlin/pluginregistry.h>
#include <gmerlin/utils.h>
#include <gmerlin/cfgctx.h>
#include <gmerlin/cmdline.h>
#include <gmerlin/log.h>
#include <gmerlin/translation.h>
#include <gmerlin/bgplug.h>
#include <gmerlin/mediaconnector.h>
#include <gmerlin/bggavl.h>
#include <gmerlin/application.h>


extern char * gavftools_in_file;
extern char * gavftools_out_file;

void gavftools_init();
void gavftools_cleanup();

int gavftools_stop();

bg_cfg_section_t *
gavftools_iopt_section(void);

bg_cfg_section_t *
gavftools_oopt_section(void);

void
gavftools_opt_aq(void * data, int * argc, char *** _argv, int arg);

void
gavftools_opt_vq(void * data, int * argc, char *** _argv, int arg);


void
gavftools_opt_iopt(void * data, int * argc, char *** _argv, int arg);

void
gavftools_opt_oopt(void * data, int * argc, char *** _argv, int arg);

void
gavftools_opt_as(void * data, int * argc, char *** _argv, int arg);

void
gavftools_opt_as_idx(void * data, int idx, int * argc, char *** _argv, int arg);

void
gavftools_opt_vs(void * data, int * argc, char *** _argv, int arg);

void
gavftools_opt_vs_idx(void * data, int idx, int * argc, char *** _argv, int arg);

void
gavftools_opt_os(void * data, int * argc, char *** _argv, int arg);

void
gavftools_opt_os_idx(void * data, int idx, int * argc, char *** _argv, int arg);

void
gavftools_opt_ts(void * data, int * argc, char *** _argv, int arg);

void
gavftools_opt_ts_idx(void * data, int idx, int * argc, char *** _argv, int arg);

void
gavftools_opt_v(void * data, int * argc, char *** _argv, int arg);

void
gavftools_opt_m(void * data, int * argc, char *** _argv, int arg);


extern bg_gavl_audio_options_t gavltools_aopt;
extern bg_gavl_video_options_t gavltools_vopt;

bg_plug_t * gavftools_create_in_plug();
bg_plug_t * gavftools_create_out_plug();

// bg_stream_action_t * gavftools_get_stream_actions(int num, gavl_stream_type_t type);

void gavftools_set_stream_actions(bg_media_source_t * s);

void gavftools_run(gavl_handle_msg_func func,
                   void * func_data);

#define GAVFTOOLS_INPLUG_OPTIONS                \
  {                                             \
    .arg =         "-i",                        \
    .help_arg =    "<input_file>",                 \
    .help_string = TRS("Input file or location"),  \
    .argv    =    &gavftools_in_file,              \
  },                                             \
  { \
    .arg =         "-iopt", \
    .help_arg =    "<input_options>", \
    .help_string = "", \
    .callback =    gavftools_opt_iopt, \
  }

#define GAVFTOOLS_OOPT_OPTIONS \
  {                                       \
    .arg =         "-oopt",                     \
    .help_arg =    "<output_options>",          \
    .help_string = TRS("options"),              \
    .callback =    gavftools_opt_oopt,          \
  }

#define GAVFTOOLS_M_OPTIONS \
  {                                      \
    .arg =         "-m",                 \
    .help_arg =    "name=value",         \
    .help_string = TRS("Set global metadata of the output (can be used multiple times). Use \"name=\" to clear a field"), \
    .callback    = gavftools_opt_m,      \
  }

#define GAVFTOOLS_OUTPLUG_OPTIONS                \
  { \
    .arg =         "-o", \
    .help_arg =    "<output>", \
    .help_string = TRS("Output file or location"), \
    .argv    =    &gavftools_out_file, \
  }, \
  GAVFTOOLS_M_OPTIONS, \
  GAVFTOOLS_OOPT_OPTIONS

#define GAVFTOOLS_AUDIO_STREAM_OPTIONS \
  { \
    .arg =         "-as", \
    .help_arg =    "<stream_selector>", \
    .help_string = TRS("Comma separated list of characters 'd' (decode), 'm' (mute) or 'c' (read compressed)"), \
    .callback =    gavftools_opt_as, \
    .callback_idx =    gavftools_opt_as_idx, \
  }

#define GAVFTOOLS_VIDEO_STREAM_OPTIONS          \
  { \
    .arg =         "-vs", \
    .help_arg =    "<stream_selector>", \
    .help_string = TRS("'d' (decode), 'm' (mute) or 'c' (read compressed)."), \
    .callback =    gavftools_opt_vs, \
    .callback_idx =    gavftools_opt_vs_idx, \
  }

#define GAVFTOOLS_TEXT_STREAM_OPTIONS          \
  { \
    .arg =         "-ts", \
    .help_arg =    "<stream_selector>", \
    .help_string = TRS("Comma separated list of characters 'm' (mute) or 'c' (read)"), \
    .callback =    gavftools_opt_ts, \
    .callback_idx =    gavftools_opt_ts_idx, \
  }

#define GAVFTOOLS_OVERLAY_STREAM_OPTIONS           \
  { \
    .arg =         "-os", \
    .help_arg =    "<stream_selector>", \
    .help_string = TRS("Comma separated list of characters 'd' (decode), 'm' (mute) or 'c' (read compressed)"), \
    .callback =    gavftools_opt_os, \
    .callback_idx =    gavftools_opt_os_idx, \
  }


#define GAVFTOOLS_AQ_OPTIONS                \
  {                                         \
  .arg =         "-aq",                                  \
    .help_arg =    "options",                             \
    .help_string = "Audio conversion options",        \
    .callback =    gavftools_opt_aq,                               \
  }

#define GAVFTOOLS_VQ_OPTIONS                \
  {                                         \
  .arg =         "-vq",                                  \
    .help_arg =    "options",                             \
    .help_string = "Video conversion options",        \
    .callback =    gavftools_opt_vq,                               \
  }

bg_stream_action_t gavftools_get_stream_action(gavl_stream_type_t type,
                                               int num);

// void gavftools_set_compressor_options(bg_cmdline_arg_t * global_options);

void gavftools_block_sigpipe(void);

void gavftools_set_cmdline_parameters(bg_cmdline_arg_t * args);

int gavftools_open_out_plug_from_in_plug(bg_plug_t * out_plug,
                                         const char * name,
                                         bg_plug_t * in_plug);

// void gavftools_set_stream_actions(bg_plug_t * p);

int gavftools_open_input(bg_plug_t * in_plug, const char * ifile);

extern bg_plug_t * gavftools_out_plug;
extern bg_plug_t * gavftools_in_plug;
extern const gavl_dictionary_t * gavftools_media_info_in;
extern bg_controllable_t       * gavftools_ctrl_in;

/* track number from the &track= uri argument. -1 means to loop through all tracks */
extern int gavftools_track;
extern bg_mediaconnector_t gavftools_conn;
extern bg_media_source_t * gavftools_src;

