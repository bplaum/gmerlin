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

#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include <config.h>


#include <gavl/gavl.h>
#include <gavl/metadata.h>
#include <gavl/metatags.h>

#include <gmerlin/translation.h>
#include <gmerlin/player.h>
#include <gavl/keycodes.h>

#include <gmerlin/parameter.h>
#include <gmerlin/bgmsg.h>
#include <gmerlin/textrenderer.h>
#include <gmerlin/osd.h>
#include <gmerlin/utils.h>
#include <gmerlin/iconfont.h>
#include <gmerlin/player.h>
#include <gmerlin/playermsg.h>

#define FLOAT_BAR_SIZE       18
#define FLOAT_BAR_SIZE_TOTAL 20

#define TIME_BAR_SIZE        60
#define TIME_BAR_SIZE_TOTAL  62

#define PACKET_FLAG_UPDATE   (GAVL_PACKET_FLAG_PRIV<<0)
#define PACKET_FLAG_CLEAR    (GAVL_PACKET_FLAG_PRIV<<1)

#define MENU_LINES           4

typedef enum
  {
    OSD_NONE = 0,
    OSD_VOLUME,
    OSD_BRIGHTNESS,
    OSD_SATURATION,
    OSD_CONTRAST,
    OSD_INFO,
    OSD_TIME,
    OSD_AUDIO_STREAM_MENU,
    OSD_SUBTITLE_STREAM_MENU,
    OSD_CHAPTER_MENU,
    OSD_WARNING,
    OSD_MUTE,
    OSD_PAUSE,
  } osd_type_t;

/* OSD Menu */

/*
  -> 
  
 */

static int handle_message(void * priv, gavl_msg_t * msg);


typedef struct
  {
  char * item_0;
  
  char ** items;
  int items_alloc;
  int num_items;

  int first; // First visible item
  int sel;   // Selected item
  int cur;   // Current item
  
  bg_msg_sink_t * c;
  void (*cb)(bg_msg_sink_t * sink, int idx);
  } menu_t;

static void menu_clear(menu_t * m)
  {
  int i;
  for(i = 0; i < m->num_items; i++)
    {
    free(m->items[i]);
    m->items[i] = NULL;
    }
  m->num_items = 0;
  }

static void menu_alloc(menu_t * m, int num)
  {
  // fprintf(stderr, "menu alloc %d\n", num);
  
  menu_clear(m);

  if(m->item_0)
    num++;
  
  if(m->items_alloc < num)
    {
    m->items_alloc = num + 16;
    m->items = realloc(m->items, m->items_alloc * sizeof(*m->items));
    memset(m->items, 0, m->items_alloc * sizeof(*m->items));
    }
  }

static void menu_free(menu_t * m)
  {
  menu_clear(m);
  if(m->items)
    free(m->items);
  }

static void menu_set(menu_t * m, int idx, char * val)
  {
  if((idx >= m->items_alloc) || (idx < 0))
    return;

  if(m->item_0)
    {
    idx++;
    if(!m->items[0])
      m->items[0] = gavl_strdup(m->item_0);
    }
  
  if(m->items[idx])
    free(m->items[idx]);
  m->items[idx] = val;

  if(m->num_items < idx + 1)
    m->num_items = idx + 1;
  }

static void menu_adjust(menu_t * m)
  {
  if(m->sel < m->first)
    m->first = m->sel;
  else if(m->sel >= m->first + MENU_LINES)
    m->first = m->sel - (MENU_LINES - 1);
  }

static void menu_set_cur(menu_t * m, int cur)
  {
  //  fprintf(stderr, "set_cur: %d\n", cur);
  m->cur = cur;
  if(m->item_0)
    m->cur++;
  }

static char * append_icon(char * str, const char * icon)
  {
  char * tmp_string =
    bg_sprintf("<span font_family=\"%s\" weight=\"normal\">%s</span>",
               BG_ICON_FONT_FAMILY, icon);
  
  str = gavl_strcat(str, tmp_string);
  free(tmp_string);
  return str;
  }

static char * menu_markup(menu_t * m)
  {
  char * tmp_string;
  int i, end;
  char * ret = gavl_strdup("<markup>");

  /* Up arrow */
  if(m->first > 0)
    {
    ret = gavl_strcat(ret, "\t");
    ret = append_icon(ret, BG_ICON_CHEVRON_UP);
    }

  if(m->num_items > MENU_LINES)
    ret = gavl_strcat(ret, "\n");
  
  end = m->first + MENU_LINES;
  if(end > m->num_items)
    end = m->num_items;
  
  for(i = m->first; i < end; i++)
    {
    if(i > m->first)
      ret = gavl_strcat(ret, "\n");
    
    if(i == m->sel)
      ret = append_icon(ret, BG_ICON_ARROW_RIGHT);
    
    ret = gavl_strcat(ret, "\t");

    if(i == m->cur)
      ret = append_icon(ret, BG_ICON_BOX_RADIO);
    else
      ret = append_icon(ret, BG_ICON_BOX);

    tmp_string = bg_sprintf(" %s", m->items[i]);
    ret = gavl_strcat(ret, tmp_string);
    free(tmp_string);
    }

  if(m->num_items > MENU_LINES)
    ret = gavl_strcat(ret, "\n");
  
  /* Down arrow */
  if(end < m->num_items)
    {
    ret = gavl_strcat(ret, "\t");
    ret = append_icon(ret, BG_ICON_CHEVRON_DOWN);
    }
  ret = gavl_strcat(ret, "</markup>");

  // fprintf(stderr, "Menu: %s\n", ret);

  return ret;
  }

struct bg_osd_s
  {
  bg_text_renderer_t * renderer;
  int enable;

  osd_type_t current_osd;
  
  gavl_time_t duration;
  float font_size;
  gavl_timer_t * timer;
  gavl_video_sink_t * sink;  

  gavl_video_frame_t * ovl;
  gavl_video_format_t fmt;
  
  gavl_packet_t p;
  gavl_packet_source_t * psrc;
  gavl_video_source_t * vsrc;
  
  /* Relevant track information */
  gavl_dictionary_t track;

  /* State variables */
  int audio_stream;
  int video_stream;
  int subtitle_stream;
  int chapter;
  
  bg_control_t ctrl;

  pthread_mutex_t mutex;

  gavl_time_t track_time;
  gavl_time_t track_duration;
  double percentage;
  
  char track_duration_str[GAVL_TIME_STRING_LEN];

  menu_t audio_menu;
  menu_t subtitle_menu;
  menu_t chapter_menu;
  
  menu_t * cur_menu;

  int chapter_timescale;

  float player_volume;
  
  bg_player_t * player;
  };

static gavl_source_status_t read_packet(void * priv, gavl_packet_t ** p)
  {
  bg_osd_t * osd = priv;
  *p = &osd->p;
  return GAVL_SOURCE_OK;
  }

static void set_font(bg_osd_t * osd, float size)
  {
  gavl_value_t val;
  gavl_value_init(&val);
  gavl_value_set_string_nocopy(&val, bg_sprintf("%s %.1f", BG_ICON_FONT_FAMILY, size));
  bg_text_renderer_set_parameter(osd->renderer, "fontname", &val);
  gavl_value_free(&val);
  }

bg_osd_t * bg_osd_create(bg_controllable_t * c)
  {
  bg_osd_t * ret;
  ret = calloc(1, sizeof(*ret));

  bg_control_init(&ret->ctrl, bg_msg_sink_create(handle_message, ret, 0));
  ret->renderer = bg_text_renderer_create();

  /* We set special parameters for the textrenderer */
  
  set_font(ret, 20.0);
  
  ret->timer = gavl_timer_create();
  gavl_timer_start(ret->timer);

  //  ret->psrc = gavl_packet_source_create_text(read_packet,
  //                                             ret, GAVL_SOURCE_SRC_ALLOC, GAVL_TIME_SCALE);

  ret->psrc = gavl_packet_source_create(read_packet,
                                        ret, GAVL_SOURCE_SRC_ALLOC, NULL);
  
  
  pthread_mutex_init(&ret->mutex, NULL);

  ret->player_volume = -1.0;

  bg_controllable_connect(c, &ret->ctrl);

  ret->audio_menu.c = ret->ctrl.cmd_sink;
  ret->audio_menu.cb = bg_player_set_audio_stream;
  ret->audio_menu.item_0 = TR("Off");
  
  ret->subtitle_menu.cb = bg_player_set_subtitle_stream;
  ret->subtitle_menu.item_0 = TR("Off");
  ret->subtitle_menu.c = ret->ctrl.cmd_sink;
  
  ret->chapter_menu.cb = bg_player_set_chapter;
  ret->chapter_menu.c = ret->ctrl.cmd_sink;
  
  return ret;
  }

void bg_osd_destroy(bg_osd_t * osd)
  {
  bg_text_renderer_destroy(osd->renderer);
  gavl_timer_destroy(osd->timer);
  bg_control_cleanup(&osd->ctrl); 
  gavl_packet_source_destroy(osd->psrc);
  pthread_mutex_destroy(&osd->mutex);

  gavl_dictionary_free(&osd->track);
  
  menu_free(&osd->audio_menu);
  menu_free(&osd->subtitle_menu);
  menu_free(&osd->chapter_menu);

  free(osd);
  }

static const bg_parameter_info_t parameters[] =
  {
    {
      .name =        "enable_osd",
      .long_name =   TRS("Enable OSD"),
      .type =        BG_PARAMETER_CHECKBUTTON,
      .val_default = GAVL_VALUE_INIT_INT(1),
    },
    /* The following stuff is copied from textrenderer.c */
    {
      .name      = "mode",
      .long_name = TRS("Mode"),
      .type      = BG_PARAMETER_STRINGLIST,
      .multi_names  = (const char*[]){ "simple", "box", "outline", NULL },
      .multi_labels = (const char*[]){ TRS("Simple"), TRS("Box"), TRS("Outline"), NULL },
      .val_default  = GAVL_VALUE_INIT_STRING("box"),
    },
    {
      .name =       "fontname",
      .long_name =  TRS("Font"),
      .type =       BG_PARAMETER_FONT,
      .val_default = GAVL_VALUE_INIT_STRING("Sans Bold 20")
    },
    {
      .name =       "color",
      .long_name =  TRS("Foreground color"),
      .type =       BG_PARAMETER_COLOR_RGBA,
      .val_default = GAVL_VALUE_INIT_COLOR_RGBA(1.0, 1.0, 1.0, 1.0),
    },
    {
      .name =       "border_color",
      .long_name =  TRS("Border color"),
      .type =       BG_PARAMETER_COLOR_RGB,
      .val_default = GAVL_VALUE_INIT_COLOR_RGB(0.0, 0.0, 0.0),
    },
    {
      .name =       "border_width",
      .long_name =  TRS("Border width"),
      .type =       BG_PARAMETER_FLOAT,
      .val_min =     GAVL_VALUE_INIT_FLOAT(0.0),
      .val_max =     GAVL_VALUE_INIT_FLOAT(10.0),
      .val_default = GAVL_VALUE_INIT_FLOAT(0.0),
      .num_digits =  2,
    },
    {
      .name =       "justify_h",
      .long_name =  TRS("Horizontal justify"),
      .type =       BG_PARAMETER_STRINGLIST,
      .val_default = GAVL_VALUE_INIT_STRING("center"),
      .multi_names =  (char const *[]){ "center", "left", "right", NULL },
      .multi_labels = (char const *[]){ TRS("Center"), TRS("Left"), TRS("Right"), NULL  },
    },
    {
      .name =       "justify_v",
      .long_name =  TRS("Vertical justify"),
      .type =       BG_PARAMETER_STRINGLIST,
      .val_default = GAVL_VALUE_INIT_STRING("center"),
      .multi_names =  (char const *[]){ "center", "top", "bottom", NULL  },
      .multi_labels = (char const *[]){ TRS("Center"), TRS("Top"), TRS("Bottom"), NULL },
    },
    {
      .name =        "border_left",
      .long_name =   TRS("Left border"),
      .type =        BG_PARAMETER_INT,
      .val_min =     GAVL_VALUE_INIT_INT(0),
      .val_max =     GAVL_VALUE_INIT_INT(65535),
      .val_default = GAVL_VALUE_INIT_INT(10),
      .help_string = TRS("Distance from the left text border to the image border"),
    },
    {
      .name =        "border_right",
      .long_name =   TRS("Right border"),
      .type =        BG_PARAMETER_INT,
      .val_min =     GAVL_VALUE_INIT_INT(0),
      .val_max =     GAVL_VALUE_INIT_INT(65535),
      .val_default = GAVL_VALUE_INIT_INT(10),
      .help_string = TRS("Distance from the right text border to the image border"),
    },
    {
      .name =        "border_top",
      .long_name =   TRS("Top border"),
      .type =        BG_PARAMETER_INT,
      .val_min =     GAVL_VALUE_INIT_INT(0),
      .val_max =     GAVL_VALUE_INIT_INT(65535),
      .val_default = GAVL_VALUE_INIT_INT(10),
      .help_string = TRS("Distance from the top text border to the image border"),
    },
    {
      .name =        "border_bottom",
      .long_name =   TRS("Bottom border"),
      .type =        BG_PARAMETER_INT,
      .val_min =     GAVL_VALUE_INIT_INT(0),
      .val_max =     GAVL_VALUE_INIT_INT(65535),
      .val_default = GAVL_VALUE_INIT_INT(10),
      .help_string = TRS("Distance from the bottom text border to the image border"),
    },
    {
      .name =       "box_color",
      .long_name =  TRS("Box color"),
      .type =       BG_PARAMETER_COLOR_RGBA,
      .val_default = GAVL_VALUE_INIT_COLOR_RGBA(0.2, 0.2, 0.2, 0.7),
    },
    {
      .name =       "box_radius",
      .long_name =  TRS("Box corner radius"),
      .type =       BG_PARAMETER_FLOAT,
      .val_default = GAVL_VALUE_INIT_FLOAT(10.0),
      .val_min     = GAVL_VALUE_INIT_FLOAT(0.0),
      .val_max     = GAVL_VALUE_INIT_FLOAT(20.0),
    },
    {
      .name =       "box_padding",
      .long_name =  TRS("Box padding"),
      .type =       BG_PARAMETER_FLOAT,
      .val_default = GAVL_VALUE_INIT_FLOAT(10.0),
      .val_min     = GAVL_VALUE_INIT_FLOAT(0.0),
      .val_max     = GAVL_VALUE_INIT_FLOAT(20.0),
    },
    {
      .name =        "duration",
      .long_name =   TRS("Duration (milliseconds)"),
      .type =        BG_PARAMETER_INT,
      .val_min =     GAVL_VALUE_INIT_INT(0),
      .val_max =     GAVL_VALUE_INIT_INT(10000),
      .val_default = GAVL_VALUE_INIT_INT(2000),
    },
    { /* End of parameters */ }
  };


const bg_parameter_info_t * bg_osd_get_parameters(bg_osd_t * osd)
  {
  return parameters;
  }

void bg_osd_set_parameter(void * data, const char * name,
                          const gavl_value_t * val)
  {
  bg_osd_t * osd;
  if(!name)
    return;
  osd = (bg_osd_t*)data;

  if(!strcmp(name, "enable_osd"))
    osd->enable = val->v.i;
  else if(!strcmp(name, "duration"))
    osd->duration = val->v.i * ((GAVL_TIME_SCALE) / 1000);
  else
    bg_text_renderer_set_parameter(osd->renderer, name, val);
  }

void bg_osd_init(bg_osd_t * osd, gavl_video_sink_t * sink, 
                 const gavl_video_format_t * frame_format)
  {
  gavl_video_format_t fmt;
  osd->sink = sink;
  gavl_video_format_copy(&osd->fmt, gavl_video_sink_get_format(osd->sink));
  osd->fmt.timescale = GAVL_TIME_SCALE;

  gavl_video_format_copy(&fmt, &osd->fmt);

  osd->vsrc = bg_text_renderer_connect(osd->renderer,
                                       osd->psrc,
                                       frame_format,
                                       &fmt);

  gavl_video_source_set_dst(osd->vsrc, 0, &osd->fmt);
  
  gavl_timer_stop(osd->timer);
  gavl_timer_start(osd->timer);
  }

static void print_bar(char * buf, float val, int num)
  {
  int i, val_i;
  val_i = (int)(val * num + 0.5);
  
  if(val_i > num)
    val_i = num;

  *buf = '['; buf++;
  
  for(i = 0; i < val_i; i++)
    {
    *buf = '|'; buf++;
    }
  for(i = val_i; i < num; i++)
    {
    *buf = '\\'; buf++;
    }
  *buf = ']'; buf++;
  *buf = '\0';
  }


static char * make_time_string(bg_osd_t * osd)
  {
  char * tmp_string;
  char track_time_str[GAVL_TIME_STRING_LEN];

  char * str = gavl_strdup("<markup>");

  gavl_time_prettyprint(osd->track_time, track_time_str);
  
  tmp_string = bg_sprintf("<span font_family=\"monospace\">%s</span>", track_time_str);
  str = gavl_strcat(str, tmp_string);
  free(tmp_string);
    
  if(osd->percentage >= 0.0)
    {
    char bar[TIME_BAR_SIZE_TOTAL+1];

    tmp_string = bg_sprintf(" / <span font_family=\"monospace\">%s</span>\n", osd->track_duration_str);
    str = gavl_strcat(str, tmp_string);
    free(tmp_string);
    
    print_bar(bar, osd->percentage, TIME_BAR_SIZE);
    tmp_string = bg_sprintf("<span font_family=\"%s\" weight=\"normal\">%s</span>",
                            BG_ICON_FONT_FAMILY, bar);
    
    str = gavl_strcat(str, tmp_string);
    free(tmp_string);
    }
  
  str = gavl_strcat(str, "</markup>");

  return str;
  }

/* Call once before displaying a frame */
void bg_osd_update(bg_osd_t * osd)
  {
  gavl_time_t current_time;
  gavl_time_t last_pts = 0;
  gavl_time_t last_duration = 0;

  pthread_mutex_lock(&osd->mutex);
  
  current_time = gavl_timer_get(osd->timer);
  /* Check if OSD changed */

  if(osd->p.buf.buf)
    {
    if((osd->p.flags & PACKET_FLAG_UPDATE) && osd->ovl)
      {
      last_pts      = osd->ovl->timestamp;
      last_duration = osd->ovl->duration;
      }
    
    osd->ovl = gavl_video_sink_get_frame(osd->sink);
    
    gavl_video_source_read_frame(osd->vsrc, &osd->ovl);

    if((osd->p.flags & PACKET_FLAG_UPDATE) && osd->ovl)
      {
      osd->ovl->timestamp = last_pts;
      osd->ovl->duration = last_duration;
      }
    else
      {
      osd->ovl->duration = osd->p.duration;
      osd->ovl->timestamp = current_time;
      }
    
    gavl_video_sink_put_frame(osd->sink, osd->ovl);
    gavl_packet_free(&osd->p);
    gavl_packet_init(&osd->p);
    }
  
  /* Check if OSD became invalid */
  if(osd->ovl)
    {
    if((osd->p.flags & PACKET_FLAG_CLEAR) ||
       (current_time > osd->ovl->timestamp + osd->ovl->duration))
      {
      gavl_video_sink_put_frame(osd->sink, NULL);
      osd->ovl = NULL;
      osd->current_osd = OSD_NONE;
      osd->p.flags = 0;
      osd->cur_menu = NULL;
      }
    }
  pthread_mutex_unlock(&osd->mutex);
  }


static void osd_set_nolock(bg_osd_t * osd, char * str,
                           const char * align_h, const char * align_v, int type, int flags, int dur_mult)
  {
  gavl_value_t val;
  if(!osd || !osd->enable)
    {
    if(str)
      free(str);
    return;
    }
  gavl_packet_free(&osd->p);
  gavl_packet_init(&osd->p);
    
  osd->p.buf.buf       = (uint8_t *)str;
  osd->p.buf.len   = strlen(str);
  osd->p.buf.alloc = osd->p.buf.len;
  osd->p.flags = flags;
  osd->p.duration = osd->duration * dur_mult; 

  gavl_value_init(&val);
  gavl_value_set_string(&val, align_h);
  bg_text_renderer_set_parameter(osd->renderer, "justify_h", &val);
  gavl_value_set_string(&val, align_v);
  bg_text_renderer_set_parameter(osd->renderer, "justify_v", &val);
  gavl_value_free(&val);
  
  osd->current_osd = type;
  }

static void osd_set(bg_osd_t * osd, char * str,
                    const char * align_h, const char * align_v, int type, int flags, int dur_mult)
  {
  if(!osd)
    return;
  pthread_mutex_lock(&osd->mutex);
  osd_set_nolock(osd, str, align_h, align_v, type, flags, dur_mult);
  pthread_mutex_unlock(&osd->mutex);
  }

/* Show a single icon */
static void show_icon(bg_osd_t * osd, const char * icon, int type)
  {
  char * ret = 
    bg_sprintf("<markup><span font_family=\"%s\" weight=\"normal\" size=\"larger\">%s</span></markup>",
               BG_ICON_FONT_FAMILY, icon);
  osd_set(osd, ret, "center", "center", type, 0, 1);
  }

static void show_error(bg_osd_t * osd)
  {
  show_icon(osd, BG_ICON_WARNING, OSD_WARNING);
  }

int bg_osd_clear(bg_osd_t * osd)
  {
  int ret = 0;
  pthread_mutex_lock(&osd->mutex);

  gavl_packet_free(&osd->p);
  gavl_packet_init(&osd->p);
  
  if(osd->ovl)
    {
    osd->p.flags = PACKET_FLAG_CLEAR;
    ret = 1;
    }
  pthread_mutex_unlock(&osd->mutex);
  return ret;
  }

static void print_float(bg_osd_t * osd, float val, char * c, int type)
  {
  char * str;
  char buf1[FLOAT_BAR_SIZE_TOTAL+32];
  char * buf = buf1;

  //  fprintf(stderr, "print_float 1: %s\n", c);
  
  *buf = *c; buf++;
  *buf = ' '; buf++;
  *buf = '\0';
  
  print_bar(buf, val, FLOAT_BAR_SIZE);

  str = bg_sprintf("<markup><span size=\"larger\" font_family=\"%s\" weight=\"normal\">%s</span></markup>",
                   BG_ICON_FONT_FAMILY, buf1);

  osd_set(osd, str, "center", "center", type, 0, 1);
  
  //  fprintf(stderr, "print_float 2: %s\n", str);

  }

static void bg_osd_set_volume_changed(bg_osd_t * osd, float val)
  {
  if(!osd->enable)
    return;

  if(val > 0.66)
    print_float(osd, val, BG_ICON_VOLUME_MAX, OSD_VOLUME);
  else if(val > 0.33)
    print_float(osd, val, BG_ICON_VOLUME_MID, OSD_VOLUME);
  else if(val > 0.01)
    print_float(osd, val, BG_ICON_VOLUME_MIN, OSD_VOLUME);
  else
    print_float(osd, val, BG_ICON_VOLUME_MUTE, OSD_VOLUME);
  }

static void bg_osd_set_brightness_changed(bg_osd_t * osd, float val)
  {
  if(!osd->enable)
    return;
  print_float(osd,
              (val - BG_BRIGHTNESS_MIN)/(BG_BRIGHTNESS_MAX-BG_BRIGHTNESS_MIN),
              BG_ICON_BRIGHTNESS, OSD_BRIGHTNESS);
  }

static void bg_osd_set_contrast_changed(bg_osd_t * osd, float val)
  {
  if(!osd->enable)
    return;
  print_float(osd,
              (val - BG_CONTRAST_MIN)/(BG_CONTRAST_MAX-BG_CONTRAST_MIN),
              BG_ICON_CONTRAST, OSD_CONTRAST);
  }

static void bg_osd_set_saturation_changed(bg_osd_t * osd, float val)
  {
  if(!osd->enable)
    return;
  print_float(osd,
              (val - BG_SATURATION_MIN)/(BG_SATURATION_MAX-BG_SATURATION_MIN),
              BG_ICON_SATURATION, OSD_SATURATION);
  }

static void clear_info(bg_osd_t * osd)
  {
  gavl_dictionary_reset(&osd->track);
  
  osd->track_time     = GAVL_TIME_UNDEFINED;
  osd->track_duration = GAVL_TIME_UNDEFINED;

  gavl_time_prettyprint(osd->track_duration, osd->track_duration_str);
  }

static char * append_header(char * str, const char * val)
  {
  const char * nl = "";
  char * tmp_string;
  if(str && (strlen(str) > 8))
    nl = "\n";

  tmp_string = bg_sprintf("%s<span size=\"larger\" weight=\"heavy\">%s</span>", nl, val);
  str = gavl_strcat(str, tmp_string);
  free(tmp_string);
  return str;
  }
  
static char * append_row(char * str, const char * icon, const char * val)
  {
  const char * nl = "";
  char * tmp_string;

  if(str && (strlen(str) > 8))
    nl = "\n";
  
  tmp_string = bg_sprintf("%s<span font_family=\"%s\" weight=\"normal\">%s</span>\t%s",
                          nl, BG_ICON_FONT_FAMILY, icon, val);

  str = gavl_strcat(str, tmp_string);
  free(tmp_string);
  return str;
  }

void bg_osd_show_info(bg_osd_t * osd)
  {
  int year;
  const char * var;
  char * tmp_string;
  const gavl_dictionary_t * m;
  const gavl_dictionary_t * sm;

  const gavl_audio_format_t * afmt;
  const gavl_video_format_t * vfmt;
  
  char * str = gavl_strdup("<markup>");
  
  m = gavl_track_get_metadata(&osd->track);
  
  if((var = gavl_dictionary_get_string(m, GAVL_META_TITLE)) ||
     (var = gavl_dictionary_get_string(m, GAVL_META_LABEL)))
    str = append_header(str, var);

  if((tmp_string = gavl_metadata_join_arr(m, GAVL_META_ARTIST, ", ")))
    {
    str = append_row(str, BG_ICON_MICROPHONE, tmp_string);
    free(tmp_string);
    }

  if((var = gavl_dictionary_get_string(m, GAVL_META_ALBUM)))
    {
    str = append_row(str, BG_ICON_MUSIC_ALBUM, var);
    }
  
  if((tmp_string = gavl_metadata_join_arr(m, GAVL_META_COUNTRY, ", ")))
    {
    str = append_row(str, BG_ICON_FLAG, tmp_string);
    free(tmp_string);
    }

  if((year = bg_metadata_get_year(m)) > 0)
    {
    char year_str[16];
    snprintf(year_str, 15, "%d", year);
    str = append_row(str, BG_ICON_CALENDAR, year_str);
    }
  
  if((tmp_string = gavl_metadata_join_arr(m, GAVL_META_DIRECTOR, ", ")))
    {
    str = append_row(str, BG_ICON_MOVIE_MAKER, tmp_string);
    free(tmp_string);
    }

  if((var = gavl_dictionary_get_string(m, GAVL_META_FORMAT)))
    str = append_row(str, BG_ICON_INFO, var);
  
  sm = gavl_track_get_audio_metadata(&osd->track, osd->audio_stream);
  afmt = gavl_track_get_audio_format(&osd->track, osd->audio_stream);
  
  if((sm = gavl_track_get_audio_metadata(&osd->track, osd->audio_stream)) &&
     (afmt = gavl_track_get_audio_format(&osd->track, osd->audio_stream)) &&
     (var = gavl_dictionary_get_string(sm, GAVL_META_FORMAT)))
    {
    char * bitrate;
    char * tmp_string = bg_sprintf("%s, %.2f kHz, %d Ch", var,
                                   (float)afmt->samplerate / 1000.0,
                                   afmt->num_channels);
    
    bitrate = bg_metadata_bitrate_string(sm, GAVL_META_BITRATE);
    if(bitrate)
      {
      tmp_string = gavl_strcat(tmp_string, ", ");
      tmp_string = gavl_strcat(tmp_string, bitrate);
      free(bitrate);
      }

    str = append_row(str, BG_ICON_MUSIC, tmp_string);
    free(tmp_string);
    }

  if((sm = gavl_track_get_video_metadata(&osd->track, osd->video_stream)) &&
     (vfmt = gavl_track_get_video_format(&osd->track, osd->video_stream)) &&
     (var = gavl_dictionary_get_string(sm, GAVL_META_FORMAT)))
    {
    char * bitrate;
    char * tmp_string = bg_sprintf("%s, %dx%d", var,
                                   vfmt->image_width,
                                   vfmt->image_height);
    
    bitrate = bg_metadata_bitrate_string(sm, GAVL_META_BITRATE);
    if(bitrate)
      {
      tmp_string = gavl_strcat(tmp_string, ", ");
      tmp_string = gavl_strcat(tmp_string, bitrate);
      free(bitrate);
      }
    
    str = append_row(str, BG_ICON_FILM, tmp_string);
    free(tmp_string);
    }
  
  str = gavl_strcat(str, "</markup>");

  /* align */
  osd_set(osd, str, "left", "center", OSD_INFO, 0, 2);
  }

void bg_osd_show_time(bg_osd_t * osd)
  {
  char * str = make_time_string(osd);
  osd_set(osd, str, "center", "bottom", OSD_TIME, 0, 2);
  }

static int handle_message(void * priv, gavl_msg_t * msg)
  {
  bg_osd_t * osd = priv;

  switch(msg->NS)
    {
#if 0
    case GAVL_MSG_NS_SRC:
      switch(msg->id)
        {
        }
      break;
#endif
    case BG_MSG_NS_STATE:
      switch(gavl_msg_get_id(msg))
        {
        case BG_MSG_STATE_CHANGED:
          {
          gavl_value_t val;
          const char * ctx;
          const char * var;
          int last = 0;
          
          gavl_value_init(&val);

          bg_msg_get_state(msg,
                           &last,
                           &ctx,
                           &var,
                           &val, NULL);

          if(!strcmp(ctx, BG_STATE_CTX_OV))
            {
            if(!strcmp(var, BG_STATE_OV_CONTRAST))     // float
              {
              double v;
              if(!gavl_value_get_float(&val, &v))
                return 1;
            
              bg_osd_set_contrast_changed(osd, v);
              }
            else if(!strcmp(var, BG_STATE_OV_SATURATION))     // float
              {
              double v;
              if(!gavl_value_get_float(&val, &v))
                return 1;
            
              bg_osd_set_saturation_changed(osd, v);
              }
            else if(!strcmp(var, BG_STATE_OV_BRIGHTNESS))     // float
              {
              double v;
              if(!gavl_value_get_float(&val, &v))
                return 1;
            
              bg_osd_set_brightness_changed(osd, v);
              }
            }
          if(!strcmp(ctx, BG_PLAYER_STATE_CTX))
            {
            if(!strcmp(var, BG_PLAYER_STATE_VOLUME))     // float
              {
              double vol;
              if(!gavl_value_get_float(&val, &vol))
                return 1;

              bg_osd_set_volume_changed(osd, vol);
              osd->player_volume = vol;
              }
            else if(!strcmp(var, BG_PLAYER_STATE_STATUS))       // int
              {
              int new_state;
              if(!gavl_value_get_int(&val, &new_state))
                return 1;

              switch(new_state)
                {
                case BG_PLAYER_STATUS_STOPPED:
                case BG_PLAYER_STATUS_CHANGING:
                  clear_info(osd);
                  break;
                case BG_PLAYER_STATUS_PAUSED:
                  show_icon(osd, BG_ICON_PAUSE, OSD_PAUSE);
                  break;
                case BG_PLAYER_STATUS_PLAYING:
                  if(osd->current_osd == OSD_PAUSE)
                    bg_osd_clear(osd);
                  break;
                }
              }
            else if(!strcmp(var, BG_PLAYER_STATE_CURRENT_TRACK))         // dictionary
              {
              int i;
              int num;
              int num_text_streams;
              
              const gavl_dictionary_t * dict;
              const gavl_dictionary_t * m;
              
              if(!(dict = gavl_value_get_dictionary(&val)) ||
                 !(m = gavl_track_get_metadata(dict)))
                return 1;
              
              gavl_dictionary_reset(&osd->track);
              gavl_dictionary_copy(&osd->track, dict);

              if(gavl_dictionary_get_long(m, GAVL_META_APPROX_DURATION, &osd->track_duration))
                gavl_time_prettyprint(osd->track_duration, osd->track_duration_str);
              
              if((dict = gavl_dictionary_get_chapter_list(m)))
                {
                /* */
                int timescale;
                const gavl_dictionary_t * chapter;
                int64_t chapter_time;
                char * label;
          
                num = gavl_chapter_list_get_num(dict);
                timescale = gavl_chapter_list_get_timescale(dict);

                menu_alloc(&osd->chapter_menu, num);

                for(i = 0; i < num; i++)
                  {
                  chapter = gavl_chapter_list_get(dict, i);

                  if(!(gavl_dictionary_get_long(chapter, GAVL_CHAPTERLIST_TIME, &chapter_time)))
                    return 1;
            
                  label = bg_get_chapter_label(i, chapter_time, timescale,
                                               gavl_dictionary_get_string(chapter, GAVL_META_LABEL));

                  menu_set(&osd->chapter_menu, i, label);
                  }
                
                }

              num = gavl_track_get_num_audio_streams(&osd->track);
              menu_alloc(&osd->audio_menu, num);
              
              for(i = 0; i < num; i++)
                menu_set(&osd->audio_menu, i, bg_get_stream_label(i, gavl_track_get_audio_metadata(&osd->track, i)));

              num_text_streams = gavl_track_get_num_text_streams(&osd->track);
              num              = gavl_track_get_num_overlay_streams(&osd->track);
              
              menu_alloc(&osd->subtitle_menu, num_text_streams + num);

              for(i = 0; i < num_text_streams; i++)
                menu_set(&osd->subtitle_menu, i,
                         bg_get_stream_label(i, gavl_track_get_text_metadata(&osd->track, i)));
              
              for(i = 0; i < num; i++)
                menu_set(&osd->subtitle_menu, i + num_text_streams,
                         bg_get_stream_label(i + num_text_streams,
                                             gavl_track_get_overlay_metadata(&osd->track, i)));
              
              if(gavl_track_get_num_video_streams(&osd->track))
                {
                /* Display track info */
                bg_osd_show_info(osd);
                }
              }
            else if(!strcmp(var, BG_PLAYER_STATE_CURRENT_TIME))          // dictionary
              {
              const gavl_dictionary_t * dict;

              if(!(dict = gavl_value_get_dictionary(&val)) ||
                 !gavl_dictionary_get_long(dict, BG_PLAYER_TIME, &osd->track_time))
                return 1;
              
              gavl_dictionary_get_float(dict, BG_PLAYER_TIME_PERC, &osd->percentage);
              
              if(osd->current_osd == OSD_TIME)
                {
                char * str = make_time_string(osd);
                osd_set(osd, str, "left", "bottom", OSD_TIME, PACKET_FLAG_UPDATE, 2);
                }
              }
            else if(!strcmp(var, BG_PLAYER_STATE_MODE))          // int
              {
              }
            else if(!strcmp(var, BG_PLAYER_STATE_MUTE))          // int
              {
              int mute;
              if(gavl_value_get_int(&val, &mute) && mute)
                show_icon(osd, BG_ICON_VOLUME_MUTE, OSD_MUTE);
              else
                bg_osd_set_volume_changed(osd, osd->player_volume);
              break;
              }
            else if(!strcmp(var, BG_PLAYER_STATE_AUDIO_STREAM_CURRENT))          // int
              {
              int val_i;
              if(gavl_value_get_int(&val, &val_i))
                {
                menu_set_cur(&osd->audio_menu, val_i);
                osd->audio_menu.sel = osd->audio_menu.cur;
                menu_adjust(&osd->audio_menu);
                
                if(osd->current_osd == OSD_AUDIO_STREAM_MENU)
                  {
                  char * str;
                  str = menu_markup(&osd->audio_menu);
                  osd_set_nolock(osd, str, "left", "center", osd->current_osd, 0, 2);
                  }
                }
              }
            else if(!strcmp(var, BG_PLAYER_STATE_VIDEO_STREAM_CURRENT))          // int
              {
              
              }
            else if(!strcmp(var, BG_PLAYER_STATE_SUBTITLE_STREAM_CURRENT))          // int
              {
              int val_i;
              if(gavl_value_get_int(&val, &val_i))
                {
                menu_set_cur(&osd->subtitle_menu, val_i);
                osd->subtitle_menu.sel = osd->subtitle_menu.cur;
                menu_adjust(&osd->subtitle_menu);
                
                if(osd->current_osd == OSD_SUBTITLE_STREAM_MENU)
                  {
                  char * str;
                  str = menu_markup(&osd->subtitle_menu);
                  osd_set_nolock(osd, str, "left", "center", osd->current_osd, 0, 2);
                  }
                }
              }
            else if(!strcmp(var, BG_PLAYER_STATE_CHAPTER))            // int
              {
              int val_i;
              if(gavl_value_get_int(&val, &val_i))
                menu_set_cur(&osd->chapter_menu, val_i);
              
              if((osd->current_osd == OSD_CHAPTER_MENU) && osd->cur_menu)
                {
                char * str;
                str = menu_markup(&osd->chapter_menu);
                osd_set(osd, str, "left", "center", OSD_CHAPTER_MENU, PACKET_FLAG_UPDATE, 2);
                }
              }
            }
          gavl_value_free(&val);
          }
          break;
        }
      break; 
    }
  
  
  return 1;
  }

void bg_osd_handle_messages(bg_osd_t * osd)
  {
  if(!osd)
    return;
  bg_msg_sink_iteration(osd->ctrl.evt_sink);
  }

int bg_osd_key_pressed(bg_osd_t * osd, int key, int mask)
  {
  int ret     = 0;
  int menu_changed = 0;
  menu_t * m;
  //  fprintf(stderr, "bg_osd_key_pressed %d %08x\n", key , mask);

  if(key == GAVL_KEY_ESCAPE)
    return bg_osd_clear(osd);

  pthread_mutex_lock(&osd->mutex);
  m = osd->cur_menu;

  if(!m)
    {
    pthread_mutex_unlock(&osd->mutex);
    return 0;
    }
  
  switch(key)
    {
    case GAVL_KEY_UP:
      ret = 1;
      if(m->sel < 1)
        break;
      m->sel--;
      menu_changed = 1;
      break;
    case GAVL_KEY_DOWN:
      ret = 1;
      if(m->sel >= m->num_items - 1)
        break;
      m->sel++;
      menu_changed = 1;
      break;
    case GAVL_KEY_RETURN:
      ret = 1;
      if(m->sel != m->cur)
        {
        int idx = m->sel;
        if(m->item_0)
          idx--;
        m->cur = m->sel;
        
        if(m->cb)
          m->cb(m->c, idx);
        }
      break;
    }

  if(menu_changed)
    {
    char * str; 
    menu_adjust(m);
    str = menu_markup(m);
    osd_set_nolock(osd, str, "left", "center", osd->current_osd, 0, 2);
    }
  
  pthread_mutex_unlock(&osd->mutex);
  
  return ret;
  }


static void show_menu(bg_osd_t * osd, osd_type_t type)
  {
  char * str;
  
  if(!osd->cur_menu->num_items)
    {
    show_error(osd);
    return;
    }

  osd->cur_menu->sel = osd->cur_menu->cur;
  menu_adjust(osd->cur_menu);
  str = menu_markup(osd->cur_menu);
  osd_set(osd, str, "left", "center", type, 0, 2);
  // osd_set(osd, str, "right", "center", type, 0, 2);
  }

void bg_osd_show_audio_menu(bg_osd_t * osd)
  {
  //  fprintf(stderr, "Audio menu\n");
  osd->cur_menu = &osd->audio_menu;
  show_menu(osd, OSD_AUDIO_STREAM_MENU);
  }

void bg_osd_show_subtitle_menu(bg_osd_t * osd)
  {
  //  fprintf(stderr, "Subtitle menu\n");
  osd->cur_menu = &osd->subtitle_menu;
  show_menu(osd, OSD_SUBTITLE_STREAM_MENU);
  }

void bg_osd_show_chapter_menu(bg_osd_t * osd)
  {
  //  fprintf(stderr, "Chapter menu\n");
  osd->cur_menu = &osd->chapter_menu;
  show_menu(osd, OSD_CHAPTER_MENU);
  }
