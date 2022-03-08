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

#ifndef LANGUAGE_TABLE_H_INCLUDED
#define LANGUAGE_TABLE_H_INCLUDED

extern char const * const bg_language_codes[];
extern char const * const bg_language_labels[];

typedef struct
  {
  const char * label;
  const char * code_alpha_2;
  const char * code_alpha_3;
  int code_numeric;
  } bg_country_t;

extern const bg_country_t bg_countries[];


#endif // LANGUAGE_TABLE_H_INCLUDED
