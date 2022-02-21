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

/* Image galleries

   We get root directories, which can contain nested subfolders and then
   image folders.

   A directory containing at least one other directory is considered a
   generic container. A directory with no subdirectory is considered a photo album
   and all image files in that directory are read
   
   /images/dir-1/
   
*/

static int get_file_info(const char * path, gavl_dictionary_t * ret)
  {
  /* Check if it is a regular file */
  
  }

static int browse_object(
