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

#include <gtk/gtk.h>
#include <gui_gtk/audio.h>
#include <gui_gtk/gtkutils.h>

#include <gmerlin/pluginregistry.h>
#include <gmerlin/utils.h>

typedef struct
  {
  bg_recorder_plugin_t * ra_plugin;
  bg_plugin_handle_t * ra_handle;
  bg_gtk_vumeter_t * meter;
  gavl_audio_format_t format;
  gavl_audio_source_t * src;
  }
idle_data_t;

static gboolean idle_callback(gpointer data)
  {
  idle_data_t * id = data;
  gavl_audio_frame_t * frame = NULL;

  if(gavl_audio_source_read_frame(id->src, &frame) != GAVL_SOURCE_OK)
    return FALSE;
  
  bg_gtk_vumeter_update(id->meter, frame);
  return TRUE;
  }

int main(int argc, char ** argv)
  {
  GtkWidget * window;
  idle_data_t id;
  const bg_plugin_info_t * info;
  gavl_dictionary_t m;
  
  bg_gtk_init(&argc, &argv, NULL);

  gavl_dictionary_init(&m);
  
  window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  
  id.meter = bg_gtk_vumeter_create(2, 0);
  
  gtk_container_add(GTK_CONTAINER(window),
                    bg_gtk_vumeter_get_widget(id.meter));

  /* Create plugin registry */

  bg_plugins_init();
  
  /* Load and open plugin */

  info = bg_plugin_find_by_index(bg_plugin_reg,
                                 BG_PLUGIN_RECORDER_AUDIO, 0, 0);
  
  id.ra_handle = bg_plugin_load(bg_plugin_reg, info);
  id.ra_plugin = (bg_recorder_plugin_t*)id.ra_handle->plugin;
  
  /* The soundcard might be busy from last time,
     give the kernel some time to free the device */
  
  if(!id.ra_plugin->open(id.ra_handle->priv, &id.format, NULL, &m))
    {
    fprintf(stderr, "Couldn't open audio device");
    return -1;
    }

  id.src = id.ra_plugin->get_audio_source(id.ra_handle->priv);

  /* */
  bg_gtk_vumeter_set_format(id.meter, &id.format);
  
  g_idle_add(idle_callback, &id);
  
  gtk_widget_show(window);

  gtk_main();

  return 0;
  }
