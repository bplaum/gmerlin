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



#include <stdlib.h>

/* Fontconfig */

#include <fontconfig/fontconfig.h>

#include <gavl/gavl.h>
#include <gavl/metadata.h>
#include <gavl/msg.h>

#include <gmerlin/iconfont.h>
#include <gmerlin/utils.h>
#include <gmerlin/player.h>
#include <gmerlin/playermsg.h>

/* We assume that this is accesses when no other threads
   are active */
static int initialized = 0;

void bg_iconfont_init(void)
  {
  char * icon_font;
  FcInit();

  icon_font = bg_search_file_read("web/css", "GmerlinIcons.ttf");

  if(icon_font)
    {
    FcConfigAppFontAddFile(NULL, (FcChar8*)icon_font);
    free(icon_font);
    }
  initialized = 1;
  }

static const struct
  {
  int mode;
  const char * icon;
  }
play_mode_icons[] =
  {
    { BG_PLAYER_MODE_NORMAL,  BG_ICON_ARROW_RIGHT },
    { BG_PLAYER_MODE_REPEAT,  BG_ICON_REPEAT      },
    { BG_PLAYER_MODE_SHUFFLE, BG_ICON_SHUFFLE     },
    { BG_PLAYER_MODE_ONE,     BG_ICON_NO_ADVANCE  },
    { BG_PLAYER_MODE_LOOP,    BG_ICON_REPEAT_1    },
    { BG_PLAYER_MODE_MAX,     NULL                },
  };

const char * bg_play_mode_to_icon(int play_mode)
  {
  int i = 0;

  while(play_mode_icons[i].icon)
    {
    if(play_mode == play_mode_icons[i].mode)
      return play_mode_icons[i].icon;
    i++;
    }
  return NULL;
  }

void bg_iconfont_cleanup(void)
  {
  if(initialized)
    FcFini();
  }

#if 0 // Keeps valgrind happy but causes weird crashes

#if defined(__GNUC__) && defined(__ELF__)
static void __cleanup() __attribute__ ((destructor));
 
static void __cleanup()
  {
  FcFini();
  }

#endif

#endif
