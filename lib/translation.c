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



#include <config.h>
#include <pthread.h>
#include <locale.h>

#include <gmerlin/translation.h>


static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static int translation_initialized = 0;

void bg_translation_init()
  {
  pthread_mutex_lock(&mutex);

  if(!translation_initialized)
    {
    bg_bindtextdomain (PACKAGE, LOCALE_DIR);
    translation_initialized = 1;
    }
  pthread_mutex_unlock(&mutex);
  }

void bg_bindtextdomain(const char * domainname, const char * dirname)
  {
  bindtextdomain(domainname, dirname);
  setlocale(LC_NUMERIC, "C");
  }
