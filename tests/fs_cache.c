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

#include <string.h>


#include <gavl/gavl.h>
#include <gmerlin/mdb.h>
#include <gmerlin/pluginregistry.h>

#include <mdb_private.h>

int main(int argc, char ** argv)
  {
  gavl_array_t arr;
  int total_entries = 0;
  bg_mdb_fs_cache_t c;

  int flags = BG_MDB_FS_MASK_AUDIO | \
    BG_MDB_FS_MASK_VIDEO | \
    BG_MDB_FS_MASK_IMAGE | \
    BG_MDB_FS_MASK_DIRECTORY | \
    BG_MDB_FS_MASK_MULTITRACK;

  
  
  gavl_array_init(&arr);
  
  bg_plugins_init();
  memset(&c, 0, sizeof(c));

  if(!bg_mdb_fs_cache_init(&c, "./cache.db"))
    return 0;

  if(!bg_mdb_fs_browse_children(&c, argv[1], &arr, 0, 0, flags, &total_entries))
    {
    fprintf(stderr, "Browsing failed\n");
    return EXIT_FAILURE;
    }

  fprintf(stderr, "Browse result\n");
  gavl_array_dump(&arr, 2);
  
  }
