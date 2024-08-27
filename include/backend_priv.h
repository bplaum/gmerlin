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



#ifndef BACKEND_PRIV_H_INCLUDED
#define BACKEND_PRIV_H_INCLUDED
#include <config.h>

#include <pthread.h>

typedef struct bg_remote_dev_backend_s bg_remote_dev_backend_t;

// extern bg_backend_registry_t * bg_backend_reg;

#define MPRIS2_NAME_PREFIX     "org.mpris.MediaPlayer2."
#define MPRIS2_NAME_PREFIX_LEN 23

/* Hide backends, which are exported by the same process */
#define SHADOW_LOCAL_BACKENDS


#endif // BACKEND_PRIV_H_INCLUDED
