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

/*
 * window.c - Main application window construction.
 *
 * Porting notes (GTK-3 → GTK-4):
 *   gtk_application_window_new()  → unchanged
 *   gtk_header_bar_set_show_close_button() →
 *       gtk_header_bar_set_show_title_buttons()
 *   gtk_box_pack_start() → gtk_box_append()
 *   GtkButtonBox → GtkBox (ButtonBox is removed in GTK-4)
 *   gtk_widget_show_all() in app.c covers children; no change needed here.
 */

#include <config.h>


#include "mainwindow.h"
#include "app.h"




/* ── Forward declarations for private helpers ──────────────────────────── */
static GtkWidget *build_header_bar(void);
static GtkWidget *build_content(app_data_t * ad);

/* ── Signal handlers ───────────────────────────────────────────────────── */
#if 0
static void on_button_clicked(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    (void)user_data;
    g_print("Button clicked!\n");
}
#endif
/* ── Public constructor ─────────────────────────────────────────────────── */
GtkWidget *app_window_new(GtkApplication *app)
  {
  app_data_t * ad;
  GtkWidget *header;
  GtkWidget *content;
  GtkWidget *win = gtk_application_window_new(app);
  gtk_window_set_title(GTK_WINDOW(win), "Gmerlin Transcoder "VERSION);
  gtk_window_set_default_size(GTK_WINDOW(win), 800, 600);
  gtk_application_window_set_show_menubar(GTK_APPLICATION_WINDOW(win), TRUE);

  ad = create_app_data();
  g_object_set_data_full(G_OBJECT(win), "app-data", ad,
                         (GDestroyNotify)destroy_app_data);
  
  /* Header bar (replaces the default title bar). */
  header = build_header_bar();
  gtk_window_set_titlebar(GTK_WINDOW(win), header);

  /* Main content area. */
  content = build_content(ad);
  gtk_container_add(GTK_CONTAINER(win), content);
  /*
   * GTK-4: gtk_container_add() → gtk_window_set_child(GTK_WINDOW(win), content)
   */

  register_window_actions(win);
  app_default_status(ad);

  return win;
  }

void app_default_status(app_data_t * ad)
  {
  gtk_label_set_text(GTK_LABEL(ad->status_left), "Gmerlin Transcoder "VERSION);
  gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ad->progress), 0.0);
  }

void app_set_progress(app_data_t * ad, double fraction)
  {
  if(fraction < 0.0)
    {
    gtk_widget_set_visible(ad->progress, FALSE);
    return;
    }
  gtk_widget_set_visible(ad->progress, TRUE);
  gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(ad->progress), fraction);
  }

/* ── Private helpers ────────────────────────────────────────────────────── */

static GtkWidget *build_header_bar(void)
  {
  GtkWidget *btn;
  GtkWidget *bar = gtk_header_bar_new();

  /* GTK-3 */
  gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(bar), TRUE);
  /* GTK-4: gtk_header_bar_set_show_title_buttons(GTK_HEADER_BAR(bar), TRUE); */

  gtk_header_bar_set_title(GTK_HEADER_BAR(bar), "Gmerlin transcoder "VERSION);
  /* GTK-4: gtk_header_bar_set_title_widget() with a GtkLabel if needed. */

  /* Example: a button on the right side of the header bar. */
  btn = gtk_button_new_from_icon_name("system-run", GTK_ICON_SIZE_BUTTON);
  gtk_widget_set_tooltip_text(btn, "Start transcoding");
  gtk_actionable_set_action_name(GTK_ACTIONABLE(btn), "win.start");
  gtk_header_bar_pack_end(GTK_HEADER_BAR(bar), btn);

  btn = gtk_button_new_from_icon_name("process-stop", GTK_ICON_SIZE_BUTTON);
  gtk_widget_set_tooltip_text(btn, "Stop transcoding");
  gtk_actionable_set_action_name(GTK_ACTIONABLE(btn), "win.cancel");
  gtk_header_bar_pack_end(GTK_HEADER_BAR(bar), btn);
  
  btn = gtk_button_new_from_icon_name("list-add-symbolic", GTK_ICON_SIZE_BUTTON);
  gtk_widget_set_tooltip_text(btn, "Add files");
  gtk_actionable_set_action_name(GTK_ACTIONABLE(btn), "win.addfiles");
  gtk_header_bar_pack_end(GTK_HEADER_BAR(bar), btn);

  btn = gtk_button_new_from_icon_name("list-remove-symbolic", GTK_ICON_SIZE_BUTTON);
  gtk_widget_set_tooltip_text(btn, "Delete selected files");
  gtk_actionable_set_action_name(GTK_ACTIONABLE(btn), "win.delete");
  gtk_header_bar_pack_end(GTK_HEADER_BAR(bar), btn);
  
  return bar;
  }

static GtkWidget *build_content(app_data_t * ad)
  {
  GtkWidget *scroll;
  /* Vertical box as the root layout container. */
  GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8); 
  GtkWidget *hbox;
  GtkWidget *footer;
  GtkCssProvider *css;
  /* GTK-4: gtk_box_new() is identical. */

  gtk_container_set_border_width(GTK_CONTAINER(vbox), 12);
  /* GTK-4: gtk_widget_set_margin_*(widget, 12) per side — border_width is gone. */

  /* List */
  ad->listbox = gtk_list_box_new();
  gtk_list_box_set_selection_mode(GTK_LIST_BOX(ad->listbox), GTK_SELECTION_MULTIPLE);
  //  g_signal_connect(listbox, "row-selected", G_CALLBACK(on_row_selected), NULL);
  
  /* Scrolledwindow */
  scroll = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_widget_set_vexpand(scroll, TRUE);
  gtk_container_add(GTK_CONTAINER(scroll), ad->listbox);
  
  gtk_container_add(GTK_CONTAINER(vbox), scroll);

  /* Footer */
  
  
  hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_vexpand(hbox, FALSE);
  gtk_widget_set_hexpand(hbox, TRUE);
  gtk_widget_set_valign(hbox, GTK_ALIGN_CENTER);
  
  footer = gtk_overlay_new();

  ad->status_left = gtk_label_new("");
  ad->status_right = gtk_label_new("");
  gtk_widget_set_margin_start(ad->status_left, 6);
  gtk_widget_set_margin_end(ad->status_right, 6);
  
  gtk_widget_set_halign(ad->status_left, GTK_ALIGN_START);
  gtk_widget_set_halign(ad->status_right, GTK_ALIGN_END);

  gtk_container_add(GTK_CONTAINER(hbox), ad->status_left);
  gtk_container_add(GTK_CONTAINER(hbox), ad->status_right);
  
  ad->progress = gtk_progress_bar_new();

  css = gtk_css_provider_new();
  gtk_css_provider_load_from_data(css,
                                  "progressbar trough {"
                                  "  min-height: 2em;"
                                  "}"
                                  "progressbar trough > progress {"
                                  "  min-height: 2em;"
                                  "}",
                                  -1, NULL);
  
  gtk_style_context_add_provider(
                                 gtk_widget_get_style_context(ad->progress),
                                 GTK_STYLE_PROVIDER(css),
                                 GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  
  g_object_unref(css);

  //  gtk_widget_set_size_request(ad->progress, -1, 40);
  gtk_widget_set_valign(ad->progress, GTK_ALIGN_CENTER);
  //  gtk_widget_set_visible(ad->progress, FALSE);   /* erstmal versteckt */
#if 0
  sg = gtk_size_group_new(GTK_SIZE_GROUP_VERTICAL);
  gtk_size_group_add_widget(sg, ad->status_left);
  gtk_size_group_add_widget(sg, ad->status_right);
  gtk_size_group_add_widget(sg, ad->progress);
  g_object_unref(sg);  /* overlay and widgets hold their own refs */
#endif
  gtk_container_add(GTK_CONTAINER(footer), ad->progress);
  gtk_overlay_add_overlay(GTK_OVERLAY(footer), hbox);

  gtk_container_add(GTK_CONTAINER(vbox), footer);
  
  /* Load initial tracks */
  show_tracks_init(ad);
  
  return vbox;
  }
