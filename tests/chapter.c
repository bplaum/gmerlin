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

#include <stdio.h>
#include <gtk/gtk.h>

#include <gmerlin/parameter.h>
#include <gmerlin/streaminfo.h>
#include <gmerlin/utils.h>

#include <gui_gtk/chapterdialog.h>
#include <gui_gtk/gtkutils.h>

static void create_chapter_list(gavl_chapter_list_t * ret)
  {
  int i;
  char * name;
  gavl_time_t t;
  
  gavl_chapter_list_set_timescale(ret, GAVL_TIME_SCALE);
  
  for(i = 0; i < 6; i++)
    {
    name = bg_sprintf("Chapter %c", 'A' + i);

    t = i * 600; 
    t *= GAVL_TIME_SCALE;
    
    gavl_chapter_list_insert(ret, i, t, name);
    free(name);
    }
  }

int main(int argc, char ** argv)
  {
  gavl_chapter_list_t cl;
  gavl_chapter_list_t * clp;
  
  gavl_time_t duration = (gavl_time_t)3600 * GAVL_TIME_SCALE;
  
  gavl_dictionary_init(&cl);

  clp = &cl;
  
  bg_gtk_init(&argc, &argv, NULL);;
  
  create_chapter_list(clp);
  bg_gtk_chapter_dialog_show(&clp, duration, NULL);

  if(clp)
    {
    gavl_dictionary_dump(clp, 0);
    gavl_dictionary_free(clp);
    }
  
  return 0;
  }
