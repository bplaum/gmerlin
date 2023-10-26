/*****************************************************************
 * gmerlin _ a general purpose multimedia framework and applications
 *
 * Copyright (c) 2001 - 2012 Members of the Gmerlin project
 * gmerlin_general@lists.sourceforge.net
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

#ifndef BG_ICONFONT_H_INCLUDED
#define BG_ICONFONT_H_INCLUDED

#define BG_ICON_BRIGHTNESS    "!"
#define BG_ICON_CLOCK         "#"
#define BG_ICON_FLAG          "$"
#define BG_ICON_CALENDAR      "%"
#define BG_ICON_SEEK_BACKWARD  "("
#define BG_ICON_SEEK_FORWARD  ")"

#define BG_ICON_TALK          "*"
#define BG_ICON_PLAYER        "+"
#define BG_ICON_PERSON        ","
#define BG_ICON_PERSONS       "-"
#define BG_ICON_TRANSITION    "."
#define BG_ICON_LEVELUP       "/"
#define BG_ICON_PHOTO         "0"
#define BG_ICON_TV            "1"
#define BG_ICON_FILM          "2"
#define BG_ICON_MUSIC         "3"
#define BG_ICON_PLAYLIST      "4"
#define BG_ICON_MUSIC_ALBUM   "5"
#define BG_ICON_RADIO_STATION "6"
#define BG_ICON_IMAGE         "7"
#define BG_ICON_REPEAT_1      "8"
#define BG_ICON_FILE          "9"
#define BG_ICON_BOOKMARK      ":"
#define BG_ICON_BOOKMARK_ADD  ";"
#define BG_ICON_CONTRAST      "="
#define BG_ICON_SATURATION    "?"
#define BG_ICON_NO_ADVANCE    "@"
#define BG_ICON_PLAY          "A"
#define BG_ICON_NEXT          "B"
#define BG_ICON_PREV          "C"
#define BG_ICON_PAUSE         "D"
#define BG_ICON_STOP          "E"
#define BG_ICON_CIRCLE        "F"
#define BG_ICON_BOX_RADIO     "G"
#define BG_ICON_ARROW_RIGHT   "H"
#define BG_ICON_ARROW_LEFT    "I"
#define BG_ICON_ARROW_DOWN    "J"
#define BG_ICON_ARROW_UP      "K"
#define BG_ICON_MICROPHONE    "L"
#define BG_ICON_FOLDER        "M"
#define BG_ICON_FOLDER_OPEN   "N"
#define BG_ICON_FOLDER_B      "O"
#define BG_ICON_FOLDER_B_OPEN "P"
#define BG_ICON_MOVIE_MAKER   "Q"
#define BG_ICON_SHUFFLE       "R"
#define BG_ICON_REPEAT        "S"
#define BG_ICON_NETWORK       "T"
#define BG_ICON_RETURN        "U"
#define BG_ICON_CHEVRON_UP    "V"
#define BG_ICON_CHEVRON_DOWN  "W"
#define BG_ICON_HOME          "X"
#define BG_ICON_CONFIG        "Y"
#define BG_ICON_MENU          "Z"
#define BG_ICON_DOWNLOAD      "^"
#define BG_ICON_MASKS         "_"
#define BG_ICON_LOAD          "a"
#define BG_ICON_ADD           "b"
#define BG_ICON_SAVE          "c"
#define BG_ICON_VOLUME_MIN    "d"
#define BG_ICON_VOLUME_MID    "e"
#define BG_ICON_VOLUME_MAX    "f"
#define BG_ICON_VOLUME_MUTE   "g"
#define BG_ICON_TRASH         "h"
#define BG_ICON_WARNING       "i"
#define BG_ICON_INFO          "j"
#define BG_ICON_CHECK         "k"
#define BG_ICON_HEART         "l"
#define BG_ICON_SUBTITLE      "m"
#define BG_ICON_VIEW_ICONS    "n"
#define BG_ICON_VIEW_LIST     "o"
#define BG_ICON_CAMERA        "p"
#define BG_ICON_CHEVRON_LEFT  "q"
#define BG_ICON_CHEVRON_RIGHT "r"
#define BG_ICON_ZOOM          "s"
#define BG_ICON_STAR          "t"
#define BG_ICON_SERVER        "u"
#define BG_ICON_X             "v"
#define BG_ICON_BOX           "w"
#define BG_ICON_BOX_CHECKED   "x"
#define BG_ICON_SMILEY        "y"
#define BG_ICON_SEARCH        "z"
#define BG_ICON_BAR           "|"
#define BG_ICON_PLAYQUEUE     "~"

// http://www.utf8-zeichentabelle.de/unicode-utf8-table.pl?names=-&unicodeinhtml=hex
#define BG_ICON_NAVIGATE      (char[]){ 0xc2, 0xa1, 0x00 }
#define BG_ICON_ENTER         (char[]){ 0xc2, 0xa2, 0x00 }
#define BG_ICON_LEAVE         (char[]){ 0xc2, 0xa3, 0x00 }
#define BG_ICON_LIBRARY       (char[]){ 0xc2, 0xa4, 0x00 }
#define BG_ICON_HDD           (char[]){ 0xc2, 0xa5, 0x00 }
#define BG_ICON_CUT           (char[]){ 0xc2, 0xa6, 0x00 }
#define BG_ICON_COPY          (char[]){ 0xc2, 0xa7, 0x00 }
#define BG_ICON_PASTE         (char[]){ 0xc2, 0xa8, 0x00 }
#define BG_ICON_SORT          (char[]){ 0xc2, 0xa9, 0x00 }
#define BG_ICON_GLOBE         (char[]){ 0xc2, 0xaa, 0x00 }
#define BG_ICON_TAG           (char[]){ 0xc2, 0xab, 0x00 }
#define BG_ICON_TAGS          (char[]){ 0xc2, 0xac, 0x00 }
/* 0xc2, 0xac is unused */
#define BG_ICON_CURRENT       (char[]){ 0xc2, 0xae, 0x00 }
#define BG_ICON_PENDRIVE      (char[]){ 0xc2, 0xaf, 0x00 }
#define BG_ICON_CDROM         (char[]){ 0xc2, 0xb0, 0x00 }
#define BG_ICON_MEMORYCARD    (char[]){ 0xc2, 0xb1, 0x00 }
#define BG_ICON_MOBILE        (char[]){ 0xc2, 0xb2, 0x00 }
#define BG_ICON_LOCK          (char[]){ 0xc2, 0xb3, 0x00 }
#define BG_ICON_HELP          (char[]){ 0xc2, 0xb4, 0x00 }

#define BG_ICON_EYE           (char[]){ 0xc2, 0xb5, 0x00 }
#define BG_ICON_RADIO         (char[]){ 0xc2, 0xb7, 0x00 }
#define BG_ICON_RESTART       (char[]){ 0xc2, 0xb8, 0x00 }
#define BG_ICON_POWER         (char[]){ 0xc2, 0xb9, 0x00 }
#define BG_ICON_RSS           (char[]){ 0xc2, 0xba, 0x00 }
#define BG_ICON_VIDEOCAMERA   (char[]){ 0xc2, 0xbb, 0x00 }

#define BG_ICON_CHEVRON2_LEFT  (char[]){ 0xc2, 0xbc, 0x00 }
#define BG_ICON_CHEVRON2_RIGHT (char[]){ 0xc2, 0xbd, 0x00 }
#define BG_ICON_CHEVRON2_UP    (char[]){ 0xc2, 0xbe, 0x00 }
#define BG_ICON_CHEVRON2_DOWN  (char[]){ 0xc2, 0xbf, 0x00 }
#define BG_ICON_RUN            (char[]){ 0xc3, 0x80, 0x00 }
#define BG_ICON_LOG            (char[]){ 0xc3, 0x81, 0x00 }
#define BG_ICON_FILTER         (char[]){ 0xc3, 0x82, 0x00 }
#define BG_ICON_CHAPTERS       (char[]){ 0xc3, 0x83, 0x00 }
#define BG_ICON_PLUGIN         (char[]){ 0xc3, 0x84, 0x00 }

#define BG_ICON_INCOMING      BG_ICON_ENTER

#define BG_ICON_FONT_FAMILY "GmerlinIcons"

void bg_iconfont_init(void);
const char * bg_play_mode_to_icon(int play_mode);


#endif // BG_ICONFONT_H_INCLUDED
 
