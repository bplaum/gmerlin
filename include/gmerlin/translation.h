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



#ifndef BG_TRANSLATION_H_INCLUDED
#define BG_TRANSLATION_H_INCLUDED

#include <libintl.h>

/* Static strings (will be regognized by xgettext) */
#define TRS(s) s

/* Just add the string to the .po files, even if it doesn't occur
 * anywhere in the source. This can be used to translate messages
 * coming from other (not gettextized) libs.
 */

#define TRU(s)

#define TRD(s, d) (d ? dgettext(d, s) : dgettext(PACKAGE, s))

#define TR(s) dgettext(PACKAGE, s)

#define TR_DOM(str) dgettext((translation_domain ? translation_domain : PACKAGE), str)

void bg_translation_init();

void bg_bindtextdomain(const char * domainname, const char * dirname);

#define BG_LOCALE \
.gettext_domain = PACKAGE, \
.gettext_directory = LOCALE_DIR

#endif // BG_TRANSCODER_H_INCLUDED

