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

#include <gui_gtk/fileselect.h>
#include <gui_gtk/urlselect.h>
#include <gui_gtk/driveselect.h>


typedef struct track_list_s track_list_t;


typedef struct
  {
  GtkWidget * add_files_item;
  GtkWidget * add_urls_item;
  GtkWidget * add_drives_item;
  GtkWidget * menu;
  } add_menu_t;

typedef struct
  {
  GtkWidget * move_up_item;
  GtkWidget * move_down_item;
  GtkWidget * remove_item;
  GtkWidget * configure_item;
  GtkWidget * encoder_item;
  GtkWidget * mass_tag_item;
  GtkWidget * split_at_chapters_item;
  GtkWidget * auto_number_item;
  GtkWidget * auto_rename_item;
  GtkWidget * menu;
  } selected_menu_t;

typedef struct
  {
  GtkWidget * cut_item;
  GtkWidget * copy_item;
  GtkWidget * paste_item;
  GtkWidget * menu;
  } edit_menu_t;

typedef struct
  {
  GtkWidget *      add_item;
  add_menu_t       add_menu;
  GtkWidget *      selected_item;
  selected_menu_t  selected_menu;

  GtkWidget *      edit_item;
  edit_menu_t      edit_menu;
  
  GtkWidget      * menu;
  } menu_t;


struct track_list_s
  {
  GtkWidget * treeview;
  GtkWidget * widget;

  /* Buttons */

  GtkWidget * add_file_button;
  GtkWidget * add_url_button;
  GtkWidget * add_drives_button;

  GtkWidget * delete_button;
  GtkWidget * config_button;
  GtkWidget * encoder_button;

  GtkWidget * cut_button;
  GtkWidget * copy_button;
  GtkWidget * paste_button;

  gavl_dictionary_t t;

  GtkTreeViewColumn * col_name;

  bg_transcoder_track_t * selected_track;
  int num_selected;

  gulong select_handler_id;

  gavl_dictionary_t * track_defaults_section;
  gavl_dictionary_t * encoder_section;
  const bg_parameter_info_t * encoder_parameters;
  
  GtkWidget * time_total;
  int show_tooltips;

  menu_t menu;

  char * clipboard;
  
  GtkAccelGroup * accel_group;

  bg_msg_sink_t * dlg_sink;

  gavl_dictionary_t dlg_section;
  
  };

void track_list_update(track_list_t * w);


track_list_t * track_list_create(gavl_dictionary_t * track_defaults_section,
                                 const bg_parameter_info_t * encoder_parameters,
                                 gavl_dictionary_t * encoder_section);

void track_list_destroy(track_list_t *);

GtkWidget * track_list_get_widget(track_list_t *);
GtkWidget * track_list_get_menu(track_list_t *);

bg_transcoder_track_t * track_list_get_track(track_list_t *);

void track_list_prepend_track(track_list_t *, const bg_transcoder_track_t *);

void track_list_load(track_list_t * t, const char * filename);
void track_list_save(track_list_t * t, const char * filename);

void track_list_add_url(track_list_t * t, char * url);

GtkAccelGroup * track_list_get_accel_group(track_list_t * t);
