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
#include <uuid/uuid.h>

#include <gtk/gtk.h>

#include <config.h>

#include <gavl/metatags.h>

#include <gmerlin/bgmsg.h>
#include <gmerlin/iconfont.h>
#include <gmerlin/utils.h>
#include <gmerlin/bggavl.h>

#include <gmerlin/translation.h>
#include <gmerlin/log.h>
#include <gmerlin/state.h>
#include <gmerlin/playermsg.h>

#include <gui_gtk/gtkutils.h>

#define LOG_DOMAIN "mdb.tree"

#include <gui_gtk/mdb.h>
#include "mdb_private.h"

#define NOTEBOOK_GROUP "mdb-tree"

#define MAX_BG_ICON_LOADS 5

#define DUMMY_ID "@DUMMY@"

// #define DUMP_MESSAGES

static char * iter_to_id_tree(GtkTreeView *treeview, GtkTreeIter * iter);


// static int row_is_expanded(bg_gtk_mdb_tree_t * t, const char * id);


static void set_pixbuf(void * data, const char * id, GdkPixbuf * pb)
  {
  GtkTreeIter iter;
  bg_gtk_mdb_tree_t * tree = data;
  
  GtkTreeModel * model;

  if(!id)
    goto fail;
  
  if(!bg_gtk_mdb_tree_id_to_iter(GTK_TREE_VIEW(tree->treeview), id, &iter))
    goto fail;

  model = gtk_tree_view_get_model(GTK_TREE_VIEW(tree->treeview));

  gtk_tree_store_set(GTK_TREE_STORE(model), &iter, TREE_COLUMN_HAS_PIXBUF, TRUE,
                     TREE_COLUMN_HAS_ICON, FALSE,
                     TREE_COLUMN_PIXBUF, pb, -1);
  
  fail:
  
  return;
  }

#if 1
static void pixbuf_from_uri_callback_tree(void * data, const char * id, GdkPixbuf * pb)
  {
  bg_gtk_mdb_tree_t * tree = data;
  if(pb)
    set_pixbuf(data, id, pb);
  
  tree->icons_loading--;
  }
#endif

static void pixbuf_from_uri_callback_list(void * data, const char *id, GdkPixbuf * pb)
  {
  bg_gtk_mdb_tree_t * tree = data;

  bg_gtk_mdb_list_set_pixbuf(tree, id, pb);

  tree->icons_loading--;
  }

static void queue_load_icon(bg_gtk_mdb_tree_t * t, gavl_array_t * arr)
  {
  const gavl_dictionary_t * dict;
  const char * id;
  const char * uri;

  if(!(dict = gavl_value_get_dictionary(&arr->entries[0])) ||
     !(id = gavl_dictionary_get_string(dict, GAVL_META_ID)) ||
     !(uri = gavl_dictionary_get_string(dict, GAVL_META_URI)))
    {
    gavl_array_splice_val(arr, 0, 1, NULL);
    return;
    }

  if(arr == &t->list_icons)
    {
    bg_gtk_pixbuf_from_uri_async(pixbuf_from_uri_callback_list, t, id, uri, LIST_ICON_WIDTH, LIST_ICON_HEIGHT);
    }
  else if(arr == &t->tree_icons)
    {
    bg_gtk_pixbuf_from_uri_async(pixbuf_from_uri_callback_tree, t, id, uri, TREE_ICON_WIDTH, TREE_ICON_HEIGHT);
    }
  t->icons_loading++;
  gavl_array_splice_val(arr, 0, 1, NULL);
  }

static void load_icons(bg_gtk_mdb_tree_t * t)
  {
  if(!t->tree_icons.num_entries &&
     !t->list_icons.num_entries)
    return;
  
  while(t->icons_loading < MAX_BG_ICON_LOADS)
    {
    if(t->tree_icons.num_entries)
      {
      queue_load_icon(t, &t->tree_icons);
      }

    if(t->icons_loading >= MAX_BG_ICON_LOADS)
      break;
    
    if(t->list_icons.num_entries)
      {
      queue_load_icon(t, &t->list_icons);
      }

    if(!t->tree_icons.num_entries &&
       !t->list_icons.num_entries)
      break;
    
    }
  
  return;
  }



static void open_album(bg_gtk_mdb_tree_t * t, const char * id);


/* Utils */


static char * iter_to_id_tree(GtkTreeView *treeview, GtkTreeIter * iter)
  {
  GtkTreeModel * model;
  GValue value={0,};
  char * ret;
  
  model = gtk_tree_view_get_model(treeview);
  gtk_tree_model_get_value(model, iter, TREE_COLUMN_ID, &value);
  
  ret = gavl_strdup(g_value_get_string(&value));
  g_value_unset(&value);
  return ret;
  }

static int find_child(GtkTreeModel * model, const char * id, const char * end, GtkTreeIter * iter)
  {
  GValue value={0,};
  const char * iter_id;
  int len = end - id;

  //  fprintf(stderr, "Find child %s\n", id);
  
  while(1)
    {
    gtk_tree_model_get_value(model, iter, TREE_COLUMN_ID, &value);

    iter_id = g_value_get_string(&value);
    
    if(iter_id && (strlen(iter_id) == len) && !strncmp(iter_id, id, len))
      {
      g_value_unset(&value);
      return 1;
      }
    
    g_value_unset(&value);
    if(!gtk_tree_model_iter_next(model, iter))
      break;
    }
  return 0;
  }

int bg_gtk_mdb_tree_id_to_iter(GtkTreeView *treeview, const char * id, GtkTreeIter * ret)
  {
  GtkTreeModel * model;
  GtkTreeIter child_iter;
  GtkTreeIter iter;

  const char * end;
  const char * pos;

  if(!strcmp(id, "/"))
    return 0;
  
  model = gtk_tree_view_get_model(treeview);

  if(!gtk_tree_model_iter_children(model, &child_iter, NULL))
    {
    if(strcmp(id, BG_PLAYQUEUE_ID))
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "bg_gtk_mdb_tree_id_to_iter(%s) Couldn't initialize child_iter", id);
    return 0;
    }
  
  end = strchr(id + 1, '/');
  if(!end)
    end = id + strlen(id);
  
  while(1)
    {
    if(!find_child(model, id, end, &child_iter))
      return 0;

    if(*end == '\0')
      {
      *ret = child_iter;
      return 1;
      }
    iter = child_iter;

    if(!gtk_tree_model_iter_children(model, &child_iter, &iter))
      return 0;
    
    if((pos = strchr(end+1, '/')))
      end = pos;
    else
      end += strlen(end);
    }

  return 0;
  }

#if 0
static int is_dummy_entry(bg_gtk_mdb_tree_t * w, GtkTreeIter * iter)
  {
  int ret = 0;
  const char * st;
  GValue value={0,};
  GtkTreeModel * model;
  model = gtk_tree_view_get_model(GTK_TREE_VIEW(w->treeview));

  gtk_tree_model_get_value(model, iter, TREE_COLUMN_ID, &value);
  st = g_value_get_string(&value);

  
  if(st && !strcmp(st, "DUMMY"))
    ret = 1;
    
  g_value_unset(&value);
  return ret;
  }
#endif

static void check_add_dummy_entry(bg_gtk_mdb_tree_t * widget, GtkTreeIter * parent_iter)
  {
  GtkTreeModel * model;
  GtkTreePath* path;
  int flags = 0;
  
  model = gtk_tree_view_get_model(GTK_TREE_VIEW(widget->treeview));
  
  /* Need to use gtk here to query expanded status */
  
  path = gtk_tree_model_get_path(model, parent_iter);
  
  if(gtk_tree_view_row_expanded(GTK_TREE_VIEW(widget->treeview), path))
    {
    gtk_tree_path_free(path);
    return;
    }
  gtk_tree_path_free(path);
  
  gtk_tree_model_get(model, parent_iter, TREE_COLUMN_FLAGS, &flags, -1);

  if(flags & TREE_HAS_DUMMY)
    return;
  
  if(flags & TREE_CAN_EXPAND)
    {
    GtkTreeIter child_iter;
    gtk_tree_store_append(GTK_TREE_STORE(model), &child_iter, parent_iter);
    gtk_tree_store_set(GTK_TREE_STORE(model), &child_iter, TREE_COLUMN_ID, DUMMY_ID, -1);

    flags |= TREE_HAS_DUMMY;
    gtk_tree_store_set(GTK_TREE_STORE(model), parent_iter, TREE_COLUMN_FLAGS, flags, -1);
    }
  }

static void check_delete_dummy_entry(bg_gtk_mdb_tree_t * w, GtkTreeIter * parent_iter)
  {
  GtkTreeIter child_iter;
  GtkTreeModel * model;
  int flags;
  char * id;
  
  model = gtk_tree_view_get_model(GTK_TREE_VIEW(w->treeview));

  gtk_tree_model_get(model, parent_iter, TREE_COLUMN_FLAGS, &flags, -1);

  if(!(flags & TREE_HAS_DUMMY))
    return;
  
  flags &= ~TREE_HAS_DUMMY;
  gtk_tree_store_set(GTK_TREE_STORE(model), parent_iter, TREE_COLUMN_FLAGS, flags, -1);
  
  if(!gtk_tree_model_iter_children(model, &child_iter, parent_iter))
    return;

  while(1)
    {
    id = NULL;
    gtk_tree_model_get(model, &child_iter, TREE_COLUMN_ID, &id, -1);
    if(id)
      {
      if(!strcmp(id, DUMMY_ID))
        {
        gtk_tree_store_remove(GTK_TREE_STORE(model), &child_iter);
        g_free(id);
        break;
        }
      g_free(id);
      }
    if(!gtk_tree_model_iter_next(model, &child_iter))
      break;
    }
  }


#define NORMAL_ATTRIBUTES "font_weight=\"bold\""
#define LOCKED_ATTRIBUTES "alpha=\"70%%\""


#define META_ATTRIBUTES "size=\"small\" alpha=\"70%%\""


static char * append_meta_tag(char * ret, const char * tag, char * icon)
  {
  char * tmp_string;
  char * tag_string;

  if(!tag)
    return ret;

  tmp_string = g_markup_printf_escaped("%s", tag);
  
  tag_string = gavl_sprintf("<span "META_ATTRIBUTES"><span font_family=\"%s\" weight=\"normal\">%s</span> %s</span> ",
                          BG_ICON_FONT_FAMILY, icon, tmp_string);
  g_free(tmp_string);

  ret = gavl_strcat(ret, tag_string);
  free(tag_string);
  return ret;
  }

char * bg_gtk_mdb_tree_create_markup(const gavl_dictionary_t * dict, const char * parent_klass)
  {
  gavl_time_t duration;

  const char * var = NULL;
  char * tmp_string;
  char * markup = NULL;
  const char * klass;
  
  int locked = 0;

  const gavl_dictionary_t * m = gavl_track_get_metadata(dict);
  
  if(!m)
    return NULL;

  //  fprintf(stderr, "bg_gtk_mdb_tree_create_markup\n");
  //  gavl_dictionary_dump(dict, 2);
  
  gavl_dictionary_get_int(m, GAVL_META_LOCKED, &locked);
  
  klass = gavl_dictionary_get_string(m, GAVL_META_CLASS);

  if((!strcmp(klass, GAVL_META_CLASS_AUDIO_BROADCAST) ||
      !strcmp(klass, GAVL_META_CLASS_VIDEO_BROADCAST)))
    var = gavl_dictionary_get_string(m, GAVL_META_STATION);
  
  if(!var)
    var = gavl_dictionary_get_string(m, GAVL_META_LABEL);
  
  tmp_string = g_markup_printf_escaped("%s", var);

  if(locked)
    {
    markup = gavl_sprintf("<markup><span "LOCKED_ATTRIBUTES">%s <span font_family=\"%s\" weight=\"normal\">%s</span></span>\n", tmp_string, BG_ICON_FONT_FAMILY, BG_ICON_LOCK);
    }
  else
    markup = gavl_sprintf("<markup><span "NORMAL_ATTRIBUTES">%s</span>\n", tmp_string);
  
  g_free(tmp_string);

  //  fprintf(stderr, "bg_gtk_mdb_tree_create_markup %s %d\n", klass, num_children);
  if(klass && gavl_string_starts_with(klass, "container"))
    {
    int items = 0;
    int containers = 0;
    const char * child_icon;
    const char * child_class;
    
    gavl_dictionary_get_int(m, GAVL_META_NUM_ITEM_CHILDREN, &items);
    gavl_dictionary_get_int(m, GAVL_META_NUM_CONTAINER_CHILDREN, &containers);

    if((child_class = bg_mdb_get_child_class(dict)) &&
       (child_icon = bg_get_type_icon(child_class)))
      {
      tmp_string = gavl_sprintf("<span "META_ATTRIBUTES"><span font_family=\"%s\" weight=\"normal\">%s</span> %d</span> ",
                              BG_ICON_FONT_FAMILY, child_icon, items + containers);
      markup = gavl_strcat(markup, tmp_string);
      free(tmp_string);
      }
    else
      {
      if(containers)
        {
        tmp_string = gavl_sprintf("<span "META_ATTRIBUTES"><span font_family=\"%s\" weight=\"normal\">%s</span> %d</span> ",
                                BG_ICON_FONT_FAMILY, BG_ICON_FOLDER, containers);
        
        markup = gavl_strcat(markup, tmp_string);
        free(tmp_string);
        }
      
      if(items)
        {
        if(containers)
          markup = gavl_strcat(markup , " ");
        
        tmp_string = gavl_sprintf("<span "META_ATTRIBUTES"><span font_family=\"%s\" weight=\"normal\">%s</span> %d</span> ",
                                BG_ICON_FONT_FAMILY, BG_ICON_FILE, items);
        
        markup = gavl_strcat(markup, tmp_string);
        free(tmp_string);
        
        }
      }
    
    }

  if(!strcmp(klass, GAVL_META_CLASS_SONG))
    {
    if((var = gavl_dictionary_get_arr(m, GAVL_META_ARTIST, 0)))
      {
      tmp_string = gavl_metadata_join_arr(m, GAVL_META_ARTIST, ", ");
      markup = append_meta_tag(markup, tmp_string, BG_ICON_MICROPHONE);
      free(tmp_string);
      }
  
    if((var = gavl_dictionary_get_string(m, GAVL_META_ALBUM)) &&
       (!parent_klass || strcmp(parent_klass, GAVL_META_CLASS_MUSICALBUM)))
      {
      markup = append_meta_tag(markup, var, BG_ICON_MUSIC_ALBUM);
      }
    }

  
  
  if((var = gavl_dictionary_get_arr(m, GAVL_META_COUNTRY, 0)))
    {
    tmp_string = gavl_metadata_join_arr(m, GAVL_META_COUNTRY, ", ");
    markup = append_meta_tag(markup, tmp_string, BG_ICON_FLAG);
    free(tmp_string);
    }
  if((var = gavl_dictionary_get_arr(m, GAVL_META_LANGUAGE, 0)))
    {
    tmp_string = gavl_metadata_join_arr(m, GAVL_META_LANGUAGE, ", ");
    markup = append_meta_tag(markup, tmp_string, BG_ICON_TALK);
    free(tmp_string);
    }
  
  if((var = gavl_dictionary_get_arr(m, GAVL_META_GENRE, 0)))
    {
    tmp_string = gavl_metadata_join_arr(m, GAVL_META_GENRE, ", ");
    markup = append_meta_tag(markup, tmp_string, BG_ICON_MASKS);
    free(tmp_string);
    }

  if((var = gavl_dictionary_get_string(m, GAVL_META_PODCAST)) &&
     (!parent_klass || (strcmp(parent_klass, GAVL_META_CLASS_PODCAST))))
    markup = append_meta_tag(markup, var, BG_ICON_RSS);
  
  if((var = gavl_dictionary_get_arr(m, GAVL_META_TAG, 0)))
    {
    tmp_string = gavl_metadata_join_arr(m, GAVL_META_TAG, ", ");
    markup = append_meta_tag(markup, tmp_string, BG_ICON_TAGS);
    free(tmp_string);
    }
  
  if((var = gavl_dictionary_get_string(m, GAVL_META_DATE)))
    {
    if(!gavl_string_starts_with(var, "9999"))
      {
      if(!gavl_string_ends_with(var, "99-99"))
        markup = append_meta_tag(markup, var, BG_ICON_CALENDAR);
      else
        {
        tmp_string = gavl_strndup(var, var + 4);
        markup = append_meta_tag(markup, tmp_string, BG_ICON_CALENDAR);
        free(tmp_string);
        }
      }
    }

  if(gavl_dictionary_get_long(m, GAVL_META_APPROX_DURATION, &duration) && (duration > 0))
    {
    char str[GAVL_TIME_STRING_LEN];
    gavl_time_prettyprint(duration, str);
    markup = append_meta_tag(markup, str, BG_ICON_CLOCK);
    }

  /* For movies we add another line */

  if(klass && gavl_string_starts_with(klass, GAVL_META_CLASS_MOVIE))
    {
    markup = gavl_strcat(markup , "\n");

    if((gavl_dictionary_get_arr(m, GAVL_META_DIRECTOR, 0)))
      {
      tmp_string = gavl_metadata_join_arr(m, GAVL_META_DIRECTOR, ", ");
      markup = append_meta_tag(markup, tmp_string, BG_ICON_MOVIE_MAKER);
      free(tmp_string);
      }

    if((gavl_dictionary_get_arr(m, GAVL_META_AUDIO_LANGUAGES, 0)))
      {
      tmp_string = gavl_metadata_join_arr(m, GAVL_META_AUDIO_LANGUAGES, ", ");
      markup = append_meta_tag(markup, tmp_string, BG_ICON_TALK);
      free(tmp_string);
      }

    if((gavl_dictionary_get_arr(m, GAVL_META_SUBTITLE_LANGUAGES, 0)))
      {
      tmp_string = gavl_metadata_join_arr(m, GAVL_META_SUBTITLE_LANGUAGES, ", ");
      markup = append_meta_tag(markup, tmp_string, BG_ICON_SUBTITLE);
      free(tmp_string);
      }

    }
    
  markup = gavl_strcat(markup , "</markup>");

  return markup;
  
  }

static void set_entry_tree(bg_gtk_mdb_tree_t * t, const gavl_dictionary_t * dict,
                           GtkTreeIter * iter)
  {
  char * markup;
  const char * klass;
  GtkTreeModel * model;
  const char * id;
  const char * var;
  int locked;
  int num_container_children;
  int num_item_children;
  int flags = 0;
  
  const gavl_dictionary_t * m = gavl_track_get_metadata(dict);
  
  model = gtk_tree_view_get_model(GTK_TREE_VIEW(t->treeview));

  locked = gavl_track_is_locked(dict);

  
  // fprintf(stderr, "set_entry_tree\n");
  // gavl_dictionary_dump(dict, 2);
  
  if(!m || !(id = gavl_dictionary_get_string(m, GAVL_META_ID)))
    return;
  
  if((klass = gavl_dictionary_get_string(m, GAVL_META_CLASS)))
    {
    gtk_tree_store_set(GTK_TREE_STORE(model), iter,
                       TREE_COLUMN_ICON, bg_get_type_icon(klass),
                       TREE_COLUMN_HAS_ICON, TRUE, -1);
    }

  flags |= bg_gtk_mdb_get_edit_flags(dict);
  
  num_container_children = gavl_track_get_num_container_children(dict);
  num_item_children = gavl_track_get_num_item_children(dict);

  if(!locked)
    {
    if((num_item_children > 0) ||
       bg_mdb_is_editable(dict))
      flags |= TREE_CAN_OPEN;
    if(num_container_children > 0)
      flags |= TREE_CAN_EXPAND;
    }
  
  gtk_tree_store_set(GTK_TREE_STORE(model), iter,
                     TREE_COLUMN_FLAGS, flags, -1);
  
  markup = bg_gtk_mdb_tree_create_markup(dict, NULL);
  gtk_tree_store_set(GTK_TREE_STORE(model), iter, TREE_COLUMN_LABEL, markup, -1);
  free(markup);

  bg_gtk_mdb_load_tree_icon(t, dict);
  
  gtk_tree_store_set(GTK_TREE_STORE(model), iter, TREE_COLUMN_ID, id, -1);

  if((var = gavl_dictionary_get_string(m, GAVL_META_SEARCH_TITLE)) ||
     (var = gavl_dictionary_get_string(m, GAVL_META_TITLE)) ||
     (var = gavl_dictionary_get_string(m, GAVL_META_LABEL)))
    gtk_tree_store_set(GTK_TREE_STORE(model), iter, TREE_COLUMN_SEARCH_STRING, var, -1);

  if((var = gavl_dictionary_get_string(m, GAVL_META_TOOLTIP)))
    gtk_tree_store_set(GTK_TREE_STORE(model), iter, TREE_COLUMN_TOOLTIP, var, -1);

  
  if(flags & TREE_CAN_EXPAND)
    {
    check_add_dummy_entry(t, iter);
    }
  else
    {
    check_delete_dummy_entry(t, iter);
    }
  }

list_t * bg_gtk_mdb_tree_find_list(bg_gtk_mdb_tree_t * t,
                                   const char * id)
  {
  list_t * ret;
  GList * iterator = t->lists;

  while(iterator)
    {
    ret = iterator->data;
    if(!strcmp(ret->id, id))
      return ret;

    iterator = iterator->next;
    }
  return NULL;
  }

static int handle_cache_msg(void * data, gavl_msg_t * msg)
  {
  bg_gtk_mdb_tree_t * t = data;
  switch(msg->NS)
    {
    case BG_MSG_NS_DB_CACHE:
      {
      switch(msg->ID)
        {
        case BG_MSG_DB_CACHE_ADD_LIST_ITEMS:
          {
          gavl_array_t arr;
          const char * parent_id;
          const char * sibling_before;
          list_t * list;
          
          gavl_array_init(&arr);
          if(!(parent_id = gavl_dictionary_get_string(&msg->header, GAVL_MSG_CONTEXT_ID)) ||
             !(list = bg_gtk_mdb_tree_find_list(t, parent_id)))
            return 1;

          
          sibling_before = gavl_msg_get_arg_string_c(msg, 0);
          gavl_msg_get_arg_array(msg, 1, &arr);

          // fprintf(stderr, "Add list items %s %s %d\n", parent_id, sibling_before, arr.num_entries);
          
          bg_gtk_mdb_list_add_entries(list, &arr, sibling_before);
          
          gavl_array_free(&arr);
          }
          break;
        case BG_MSG_DB_CACHE_ADD_TREE_ITEMS:
          {
          int i;
          gavl_array_t arr;
          const char * parent_id;
          const char * sibling_before;
          GtkTreeIter * parent_it;
          GtkTreeIter parent_it_s;
          GtkTreeIter sibling_it;
          GtkTreeIter new_it;
          GtkTreeModel * model = gtk_tree_view_get_model(GTK_TREE_VIEW(t->treeview));

          gavl_array_init(&arr);
          if(!(parent_id = gavl_dictionary_get_string(&msg->header, GAVL_MSG_CONTEXT_ID)))
            return 1;

          if(!strcmp(parent_id, BG_MDB_ID_ROOT))
            parent_it = NULL;
          else
            {
            if(!(bg_gtk_mdb_tree_id_to_iter(GTK_TREE_VIEW(t->treeview), parent_id, &parent_it_s)))
              return 1;
            parent_it = &parent_it_s;
            }
          gavl_msg_get_arg_array(msg, 1, &arr);

          if(!arr.num_entries)
            {
            gavl_array_free(&arr);
            return 1;
            }
          
          sibling_before = gavl_msg_get_arg_string_c(msg, 0);
          
          if(sibling_before)
            {
            if(!bg_gtk_mdb_tree_id_to_iter(GTK_TREE_VIEW(t->treeview), sibling_before, &sibling_it))
              {
              gavl_array_free(&arr);
              gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Adding tree items failed: Sibling before (%s) not found", sibling_before);
              return 1;
              }
            
            gtk_tree_store_insert_after(GTK_TREE_STORE(model),
                                        &new_it,
                                        NULL, &sibling_it);
            }
          else
            {
            gtk_tree_store_insert_after(GTK_TREE_STORE(model),
                                        &new_it,
                                        parent_it, NULL);
            }

          for(i = 0; i < arr.num_entries; i++)
            {
            set_entry_tree(t, gavl_value_get_dictionary(&arr.entries[i]),
                           &new_it);

            if(i < arr.num_entries-1)
              {
              sibling_it = new_it;
              gtk_tree_store_insert_after(GTK_TREE_STORE(model),
                                          &new_it,
                                          NULL, &sibling_it);
              }
            
            }
          
          if(strcmp(parent_id, BG_MDB_ID_ROOT) &&
             bg_gtk_mdb_tree_id_to_iter(GTK_TREE_VIEW(t->treeview), parent_id, parent_it))
            check_delete_dummy_entry(t, parent_it);
          }
          break;
          
        /* GAVL_MSG_CONTEXT_ID: ID
           arg0: dictionary
        */
        
        case BG_MSG_DB_CACHE_UPDATE_LIST_ITEM:
          {
          list_t * list;
          const char * id;
          char * parent_id;
          
          if(!(id = gavl_dictionary_get_string(&msg->header, GAVL_MSG_CONTEXT_ID)))
            return 1;

          parent_id = bg_mdb_get_parent_id(id);

          if((list = bg_gtk_mdb_tree_find_list(t, parent_id)))
            {
            gavl_dictionary_t dict;
            gavl_dictionary_init(&dict);
            gavl_msg_get_arg_dictionary(msg, 0, &dict);
            bg_gtk_mdb_list_update_entry(list, id, &dict);
            gavl_dictionary_free(&dict);
            }
          }
          break;
        case BG_MSG_DB_CACHE_UPDATE_TREE_ITEM:
          {
          const char * id;
          gavl_dictionary_t dict;
          GtkTreeIter it;
          id = gavl_dictionary_get_string(&msg->header, GAVL_MSG_CONTEXT_ID);

          if(!bg_gtk_mdb_tree_id_to_iter(GTK_TREE_VIEW(t->treeview), id, &it))
            return 1;

          gavl_dictionary_init(&dict);
          gavl_msg_get_arg_dictionary(msg, 0, &dict);
          set_entry_tree(t, &dict, &it);
          }
          break;
          
        /* GAVL_MSG_CONTEXT_ID: Parent
           arg0: array of IDs
        */
          
        case BG_MSG_DB_CACHE_DELETE_LIST_ITEMS:
          {
          const char * parent_id;
          list_t * list;
          
          if(!(parent_id = gavl_dictionary_get_string(&msg->header, GAVL_MSG_CONTEXT_ID)))
            return 1;

          if((list = bg_gtk_mdb_tree_find_list(t, parent_id)))
            {
            gavl_array_t arr;
            gavl_array_init(&arr);
            gavl_msg_get_arg_array(msg, 0, &arr);
            bg_gtk_mdb_list_delete_entries(list, &arr);
            gavl_array_free(&arr);
            }
          
          }
          break;
        case BG_MSG_DB_CACHE_DELETE_TREE_ITEMS:
          {
          int i;
          gavl_array_t arr;
          GtkTreeIter it;
          GtkTreeModel * model = gtk_tree_view_get_model(GTK_TREE_VIEW(t->treeview));
          
          gavl_array_init(&arr);
          gavl_msg_get_arg_array(msg, 0, &arr);

          for(i = 0; i < arr.num_entries; i++)
            {
            if(bg_gtk_mdb_tree_id_to_iter(GTK_TREE_VIEW(t->treeview), gavl_string_array_get(&arr, i), &it))
              {
              gtk_tree_store_remove(GTK_TREE_STORE(model), &it);
              }
            
            }
          
          gavl_array_free(&arr);
          }
          break;
        case BG_MSG_DB_CACHE_CONTAINER_INFO:
          {
          const char * id;
          list_t * list;
          if(!(id = gavl_dictionary_get_string(&msg->header, GAVL_MSG_CONTEXT_ID)))
            return 1;

          if((list = bg_gtk_mdb_tree_find_list(t, id)))
            {
            gavl_dictionary_t dict;
            gavl_dictionary_init(&dict);
            gavl_msg_get_arg_dictionary(msg, 0, &dict);
            bg_gdk_mdb_list_set_obj(list, &dict);
            gavl_dictionary_free(&dict);
            }
          
          }
          break;
          
        }
      }

    }
  return 1;
  }

static int handle_msg(void * data, gavl_msg_t * msg)
  {
  //  bg_gtk_mdb_tree_t * t = data;

#ifdef DUMP_MESSAGES
  gavl_dprintf("mdb_tree: Got message:\n");
  gavl_msg_dump(msg, 2);
#endif
  
  /* Handle events for the treeview */

  switch(msg->NS)
    {
    }
  
  return 1;
  }

/* Closed -> Tab */

static void open_album(bg_gtk_mdb_tree_t * t, const char * id)
  {
  list_t * list;
  int flags = FALSE;
  GtkTreeIter it;
  GValue value={0,};
  GtkTreeModel * model;

  if(bg_gtk_mdb_tree_find_list(t, id))
    return;

  if(!bg_gtk_mdb_tree_id_to_iter(GTK_TREE_VIEW(t->treeview), id, &it))
    return;

  model = gtk_tree_view_get_model(GTK_TREE_VIEW(t->treeview));
  
  gtk_tree_model_get(model, &it, TREE_COLUMN_FLAGS, &flags, -1);

  if(!(flags & TREE_CAN_OPEN))
    return;
  
  list = bg_gtk_mdb_list_create(t, id);

  gtk_notebook_append_page(GTK_NOTEBOOK(t->notebook), list->widget, list->tab);
  
  gtk_notebook_set_menu_label(GTK_NOTEBOOK(t->notebook), list->widget, list->menu_label);

  gtk_notebook_set_tab_reorderable(GTK_NOTEBOOK(t->notebook), list->widget, TRUE);
  gtk_notebook_set_tab_detachable(GTK_NOTEBOOK(t->notebook), list->widget, TRUE);

  g_value_init(&value, G_TYPE_BOOLEAN);
  
  g_value_set_boolean(&value, TRUE);
  gtk_container_child_set_property(GTK_CONTAINER(t->notebook), list->widget, "tab-fill", &value);
  
  g_value_set_boolean(&value, FALSE);
  gtk_container_child_set_property(GTK_CONTAINER(t->notebook), list->widget, "tab-expand", &value);
  g_value_unset(&value);

  }


static void row_collapsed_callback(GtkTreeView *treeview,
                                   GtkTreeIter *arg1,
                                   GtkTreePath *arg2,
                                   gpointer user_data)
  {
  gavl_msg_t * msg;
  
  GtkTreeIter child_iter;
  char * id;
  GtkTreeModel *tree_model;
  bg_gtk_mdb_tree_t * t = user_data;
  id = iter_to_id_tree(treeview, arg1);
  
  tree_model = gtk_tree_view_get_model(treeview);

  /* Empty the children */
  if(gtk_tree_model_iter_nth_child(tree_model, &child_iter, arg1, 0))
    {
    while(gtk_tree_store_remove(GTK_TREE_STORE(tree_model), &child_iter))
      ;
    }

  check_add_dummy_entry(t, arg1);
  
  msg = bg_msg_sink_get(t->cache_ctrl.cmd_sink);
  gavl_msg_set_id_ns(msg, BG_CMD_DB_CACHE_CONTAINER_COLLAPSE, BG_MSG_NS_DB_CACHE);
  gavl_dictionary_set_string(&msg->header, GAVL_MSG_CONTEXT_ID, id);
  bg_msg_sink_put(t->cache_ctrl.cmd_sink);

  
  free(id);
  }


static void row_expanded_callback(GtkTreeView *treeview,
                                  GtkTreeIter *arg1,
                                  GtkTreePath *arg2,
                                  gpointer user_data)
  {
  char * id;
  gavl_msg_t * msg;
  bg_gtk_mdb_tree_t * t = user_data;
  
  id = iter_to_id_tree(treeview, arg1);

  msg = bg_msg_sink_get(t->cache_ctrl.cmd_sink);

  gavl_msg_set_id_ns(msg, BG_CMD_DB_CACHE_CONTAINER_EXPAND, BG_MSG_NS_DB_CACHE);
  gavl_dictionary_set_string(&msg->header, GAVL_MSG_CONTEXT_ID, id);
  bg_msg_sink_put(t->cache_ctrl.cmd_sink);
  
  free(id);
  }



static void tree_popup_menu(bg_gtk_mdb_tree_t * t, const GdkEvent * evt)
  {
  GtkTreeIter iter;
  GtkTreeSelection * sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(t->treeview));
    
  memset(&t->menu_ctx, 0, sizeof(t->menu_ctx));

  if(gtk_tree_selection_get_selected(sel, NULL, &iter))
    {
    if(t->menu_ctx.id)
      {
      free(t->menu_ctx.id);
      t->menu_ctx.id = NULL;
      }
    
    t->menu_ctx.id = iter_to_id_tree(GTK_TREE_VIEW(t->treeview), &iter);
    if(!t->menu_ctx.id)
      return;

    t->menu_ctx.num_selected = 1;
    t->menu_ctx.list = NULL;
    }
  
  t->menu_ctx.widget = t->treeview;
    
  bg_gtk_mdb_popup_menu(t, evt);
  
  }
  
static gboolean tree_button_press_callback(GtkWidget * w, GdkEventButton * evt,
                                           gpointer data)
  {
  GtkTreeModel * model;
  GtkTreeIter iter;
  GtkTreePath * path = NULL;
  gboolean ret = FALSE;
  bg_gtk_mdb_tree_t * t = data;
  char * id = NULL;
  GtkTreeSelection * sel;
  
  model = gtk_tree_view_get_model(GTK_TREE_VIEW(t->treeview));
  sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(t->treeview));
  
  if(gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(t->treeview),
                                    evt->x, evt->y, &path,
                                    NULL,
                                    NULL,
                                    NULL))
    {
    gtk_tree_model_get_iter(model, &iter, path);
    id = iter_to_id_tree(GTK_TREE_VIEW(t->treeview), &iter);
    }
  else
    path = NULL;
  
  if((evt->button == 3) && (evt->type == GDK_BUTTON_PRESS))
    {
    if(path)
      gtk_tree_selection_select_iter(sel, &iter);
    
    tree_popup_menu(t, (const GdkEvent*)evt);
    ret = TRUE;
    goto end;
    }
  else if((evt->button == 1) && (evt->type == GDK_2BUTTON_PRESS) && path)
    {
    open_album(t, id);
    free(id);
    goto end;
    }

  end:
  
  if(path)
    gtk_tree_path_free(path);
  
  return ret;
  }

static gboolean tree_key_press_callback(GtkWidget * w, GdkEventKey * evt,
                                           gpointer data)
  {
  bg_gtk_mdb_tree_t * t = data;
  switch(evt->keyval)
    {
    case GDK_KEY_Menu:
      tree_popup_menu(t, (const GdkEvent *)evt);
      return TRUE;
    }
  
  return FALSE;
  }

static gboolean idle_func(gpointer user_data)
  {
  bg_gtk_mdb_tree_t * t = user_data;
  //  fprintf(stderr, "idle_func (tree)\n");

  bg_msg_sink_iteration(t->ctrl.evt_sink);
  bg_msg_sink_iteration(t->player_ctrl.evt_sink);

  bg_mdb_cache_ping(t->cache);
  
  load_icons(t);
  return TRUE;
  }

static void container_remove_func(GtkContainer *container,
                                  GtkWidget    *widget,
                                  gpointer      user_data)
  {
  list_t * list = user_data;
  //  fprintf(stderr, "container_remove_func\n");

  gtk_widget_destroy(list->window);
  list->window = NULL;
  
  //  gtk_widget_destroy(tree->window);
  fprintf(stderr, "container_remove_func done\n");
  }

static gboolean unset_drag_dest_callback(gpointer data)
  {
  gtk_drag_dest_unset(data);
  return G_SOURCE_REMOVE;
  }

static gboolean window_delete_callback(GtkWidget *widget,
                                        GdkEvent  *event,
                                        gpointer   data)
  {
  list_t * list = data;
  fprintf(stderr, "window_delete_callback\n");
  bg_gtk_mdb_list_destroy(list);
  return FALSE;
  }

static GtkNotebook * create_window_callback(GtkNotebook *notebook,
                                            GtkWidget   *page,
                                            gint         x,
                                            gint         y,
                                            gpointer     user_data)
  {
  GtkWidget * ret;
  list_t * lst = NULL;
  bg_gtk_mdb_tree_t * tree = user_data;

  const gavl_dictionary_t * dict;
  const char * str;

  GList * iter;

  iter = tree->lists;

  while(iter)
    {
    lst = iter->data;
    if(lst->widget == page)
      break;
    iter = iter->next;
    }
  if(!iter)
    return NULL;
  
  ret = gtk_notebook_new();
  gtk_widget_show(ret);
  gtk_notebook_set_group_name(GTK_NOTEBOOK(ret), NOTEBOOK_GROUP);
  
  lst->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  
  if((dict = bg_mdb_cache_get_object(tree->cache, lst->id)) &&
     (dict = gavl_track_get_metadata(dict)) &&
     (str = gavl_dictionary_get_string(dict, GAVL_META_LABEL)))
    gtk_window_set_title(GTK_WINDOW(lst->window), str);
  
  gtk_window_set_default_size(GTK_WINDOW(lst->window), 600, 400);
  gtk_window_set_position(GTK_WINDOW(lst->window), GTK_WIN_POS_MOUSE);
  
  g_signal_connect(G_OBJECT(ret), "remove", G_CALLBACK(container_remove_func), lst);
  g_signal_connect(G_OBJECT(lst->window), DELETE_EVENT, G_CALLBACK(window_delete_callback), lst);
  
  gtk_container_add(GTK_CONTAINER(lst->window), ret);
  
  gtk_widget_show(lst->window);

  /* Unset the drag dest of a notebook */
  g_idle_add(unset_drag_dest_callback, ret);
  
  return GTK_NOTEBOOK(ret);
  }


static int handle_player_message(void * priv, gavl_msg_t * msg)
  {
  bg_gtk_mdb_tree_t * t = priv;
  //  const char * ctx_id;
  
  switch(msg->NS)
    {
    case BG_MSG_NS_STATE:
      switch(msg->ID)
        {
        case BG_MSG_STATE_CHANGED:
          {
          gavl_value_t val;
          const char * ctx;
          const char * var;
          int last;
          
          gavl_value_init(&val);

          gavl_msg_get_state(msg,
                           &last,
                           &ctx,
                           &var,
                           &val, NULL);

          if(!strcmp(ctx, BG_PLAYER_STATE_CTX))
            {
            if(!strcmp(var, BG_PLAYER_STATE_CURRENT_TRACK))
              {
              const gavl_dictionary_t * track;
              const gavl_dictionary_t * m;
              const char * hash;
              GList *iter;
              
              if((track = gavl_value_get_dictionary(&val)) &&
                 (m = gavl_track_get_metadata(track)))
                {
                
                hash = gavl_dictionary_get_string(m, GAVL_META_HASH);

                for(iter = t->lists; iter != NULL; iter = iter->next)
                  {
                  list_t * list = iter->data;
                  bg_gtk_mdb_list_set_current(list, hash);
                  }
                
                t->cur = gavl_strrep(t->cur, hash);
                }
              else
                t->cur = gavl_strrep(t->cur, NULL);
              }
            }
          
          gavl_value_free(&val);
          }
          break;
        }
      break;
    }
  
  return 1;
  }

static int handle_dlg_message(void * data, gavl_msg_t * msg)
  {
  bg_gtk_mdb_tree_t * tree = data;
  
  switch(msg->NS)
    {
    case BG_MSG_NS_PARAMETER:
      {
      switch(msg->ID)
        {
        case BG_CMD_SET_PARAMETER:
          {
          const char * name = NULL;
          const char * ctx = NULL;
          gavl_value_t val;
          
          bg_msg_get_parameter_ctx(msg, &ctx, &name, &val);

          if(!ctx)
            return 1;

          if(name)
            {
            gavl_dictionary_set(&tree->dlg_dict, name, &val);
            return 1;
            }

          /*
           *   ctx && !name
           */
          
          if(!strcmp(ctx, MSG_ADD_STREAM))
            {
            const char * uri = gavl_dictionary_get_string(&tree->dlg_dict, GAVL_META_URI);
            const char * label = gavl_dictionary_get_string(&tree->dlg_dict, GAVL_META_LABEL);

            if(uri)
              {
              if(label)
                bg_gtk_mdb_add_stream_source(tree, label, uri);
              else
                bg_gtk_mdb_add_stream_source(tree, "New stream source", uri);
              }
            
            }
          else if(!strcmp(ctx, MSG_ADD_CONTAINER))
            {
            const char * klass = gavl_dictionary_get_string(&tree->dlg_dict, GAVL_META_CLASS);
            const char * label = gavl_dictionary_get_string(&tree->dlg_dict, GAVL_META_LABEL);
            
            if(klass)
              {
              if(!label)
                label = "Unnamed";
              bg_gtk_mdb_create_container_generic(tree, label, klass, NULL);
              }
            
            }
          

          
          }
          break;
        }
      }
      break;
    case GAVL_MSG_NS_GUI:
      {
      switch(msg->ID)
        {
        case BG_MSG_DIALOG_DIRECTORY:
          {
          bg_gtk_mdb_create_container_generic(tree, NULL, GAVL_META_CLASS_DIRECTORY,
                                              gavl_msg_get_arg_string_c(msg, 0));
          }
          break;
        }
      }
    case BG_MSG_NS_DIALOG:
      {
      switch(msg->ID)
        {
        case BG_MSG_DIALOG_ADD_LOCATIONS:
          {
          gavl_array_t arr;
          gavl_array_init(&arr);
          gavl_msg_get_arg_array(msg, 0, &arr);

          if(arr.num_entries > 0)
            {
            bg_msg_sink_t * sink;
            gavl_msg_t * msg;
            const char * id;

            if(tree->menu_ctx.list)
              id = tree->menu_ctx.list->id;
            else
              id = tree->menu_ctx.id;
            
            sink = tree->ctrl.cmd_sink;
            if(!strcmp(id, BG_PLAYQUEUE_ID))
              sink = tree->player_ctrl.cmd_sink;
            
            msg = bg_msg_sink_get(sink);
            bg_mdb_set_load_uris(msg, id, -1, &arr);
            bg_msg_sink_put(sink);
            }
          gavl_array_free(&arr);
          }
          break;
#if 0
        case BG_MSG_DIALOG_FILE_LOAD:
          {
          
          }
          break;
#endif
        case BG_MSG_DIALOG_FILE_SAVE:
          {
          const char * ctx =
            gavl_dictionary_get_string(&msg->header,
                                       GAVL_MSG_CONTEXT_ID);
          if(!ctx)
            return 1;

          if(!strcmp(ctx, MSG_SAVE_SELECTED))
            {
            char * str;
            int type;
            const char * filename = gavl_msg_get_arg_string_c(msg, 0);
            gavl_dictionary_t * selected = bg_gtk_mdb_list_extract_selected(tree->menu_ctx.list);
            type = gavl_msg_get_arg_int(msg, 1);
            str = bg_tracks_to_string(selected, type, 1);
            gavl_dictionary_destroy(selected);
            bg_write_file(filename, str, strlen(str));
            free(str);
            }
          else if(!strcmp(ctx, MSG_SAVE_ALBUM))
            {
            char * str;
            int type;
            gavl_dictionary_t * a;
            const char * filename = gavl_msg_get_arg_string_c(msg, 0);
            type = gavl_msg_get_arg_int(msg, 1);

            a = bg_mdb_cache_get_container(tree->cache, tree->menu_ctx.list->id, NULL);
            
            str = bg_tracks_to_string(a, type, 1);

            gavl_dictionary_destroy(a);
            

            bg_write_file(filename, str, strlen(str));
            free(str);
            }
          
          }
          break;
        }
      }
    }
  return 1;

  }


bg_gtk_mdb_tree_t * bg_gtk_mdb_tree_create(bg_controllable_t * mdb_ctrl)
  {
  GtkTreeStore      *store;
  bg_gtk_mdb_tree_t * ret;
  
  GtkCellRenderer   *text_renderer;
  GtkCellRenderer   *icon_renderer;
  GtkCellRenderer   *pixmap_renderer;
  uuid_t u;
  gavl_msg_t * msg;
  GtkTreeViewColumn *column;
  GtkWidget * scrolledwindow;

  ret = calloc(1, sizeof(*ret));

  bg_control_init(&ret->player_ctrl, bg_msg_sink_create(handle_player_message, ret, 0));
  bg_control_init(&ret->ctrl, bg_msg_sink_create(handle_msg, ret, 0));
  bg_control_init(&ret->cache_ctrl, bg_msg_sink_create(handle_cache_msg, ret, 1));
  
  ret->mdb_ctrl_p = mdb_ctrl;

  bg_controllable_connect(ret->mdb_ctrl_p, &ret->ctrl);
  
  ret->cache = bg_mdb_cache_create(mdb_ctrl, NULL);

  bg_controllable_connect(bg_mdb_cache_get_controllable(ret->cache),
                          &ret->cache_ctrl);
  
  store = gtk_tree_store_new(NUM_TREE_COLUMNS,
                             G_TYPE_STRING,   // icon
                             G_TYPE_BOOLEAN,  // has_icon
                             GDK_TYPE_PIXBUF, // pixbuf
                             G_TYPE_BOOLEAN,  // has_pixbuf
                             G_TYPE_STRING,   // label
                             G_TYPE_STRING,   // id
                             G_TYPE_STRING,   // search_string
                             G_TYPE_STRING,   // tooltip
                             G_TYPE_INT      // flags
                             );
  
  ret->treeview = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));

  gtk_tree_selection_set_mode(gtk_tree_view_get_selection(GTK_TREE_VIEW(ret->treeview)),
                              GTK_SELECTION_BROWSE);
  
  gtk_tree_view_set_tooltip_column(GTK_TREE_VIEW(ret->treeview), TREE_COLUMN_TOOLTIP);
  
  gtk_widget_add_events(ret->treeview, GDK_KEY_PRESS_MASK | GDK_BUTTON_PRESS_MASK);
  
  gtk_tree_view_set_search_column(GTK_TREE_VIEW(ret->treeview), TREE_COLUMN_SEARCH_STRING);
  
  gtk_tree_view_set_search_equal_func(GTK_TREE_VIEW(ret->treeview), bg_gtk_mdb_search_equal_func,
                                      NULL, NULL);
  
  scrolledwindow =
    gtk_scrolled_window_new(gtk_scrollable_get_hadjustment(GTK_SCROLLABLE(ret->treeview)),
                            gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(ret->treeview)));

  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolledwindow),
                                 GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
  gtk_container_add(GTK_CONTAINER(scrolledwindow), ret->treeview);
  gtk_widget_show(scrolledwindow);

  gtk_widget_set_events(ret->treeview, GDK_BUTTON_PRESS_MASK);
  
  g_signal_connect(G_OBJECT(ret->treeview), "row-collapsed",
                   G_CALLBACK(row_collapsed_callback), (gpointer)ret);
  
  ret->row_expanded_tag =
    g_signal_connect(G_OBJECT(ret->treeview), "row-expanded",
                     G_CALLBACK(row_expanded_callback), (gpointer)ret);
  
  g_signal_connect(G_OBJECT(ret->treeview), "button-press-event",
                   G_CALLBACK(tree_button_press_callback), (gpointer)ret);

  g_signal_connect(G_OBJECT(ret->treeview), "key-press-event",
                   G_CALLBACK(tree_key_press_callback), (gpointer)ret);
  
  column = gtk_tree_view_column_new();

  text_renderer = gtk_cell_renderer_text_new();
  icon_renderer = gtk_cell_renderer_text_new();

  g_object_set(G_OBJECT(icon_renderer), "family", BG_ICON_FONT_FAMILY, NULL);
  g_object_set(G_OBJECT(icon_renderer), "scale", 2.0, NULL);
  g_object_set(G_OBJECT(icon_renderer), "xalign", 0.5, NULL);

  
  gtk_tree_view_column_pack_start(column, icon_renderer, FALSE);
  gtk_tree_view_column_add_attribute(column, icon_renderer,
                                     "text", TREE_COLUMN_ICON);
  gtk_tree_view_column_add_attribute(column, icon_renderer,
                                     "visible", TREE_COLUMN_HAS_ICON);
  
  /* Icon (pixmap) */
  pixmap_renderer = gtk_cell_renderer_pixbuf_new();
  gtk_tree_view_column_pack_start(column, pixmap_renderer, FALSE);
  gtk_tree_view_column_add_attribute(column, pixmap_renderer, "pixbuf", TREE_COLUMN_PIXBUF);
  gtk_tree_view_column_add_attribute(column, pixmap_renderer, "visible", TREE_COLUMN_HAS_PIXBUF);

  g_object_set(G_OBJECT(pixmap_renderer), "xalign", 0.5, NULL);
  g_object_set(G_OBJECT(pixmap_renderer), "yalign", 0.5, NULL);
  //  g_object_set(G_OBJECT(pixmap_renderer), "xpad", 1, NULL);
  g_object_set(G_OBJECT(pixmap_renderer), "ypad", 1, NULL);

  gtk_tree_view_column_pack_end(column, text_renderer, TRUE);


  g_object_set(G_OBJECT(text_renderer), "ellipsize", PANGO_ELLIPSIZE_END, NULL);
  
  gtk_tree_view_column_add_attribute(column, text_renderer,
                                     "markup", TREE_COLUMN_LABEL);

  g_object_set(G_OBJECT(text_renderer), "yalign", 0.0, NULL);

  
  gtk_tree_view_append_column (GTK_TREE_VIEW(ret->treeview), column);
  
  gtk_tree_view_column_set_expand(column, TRUE);
  
  /* */
  
  gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(ret->treeview), FALSE);
  
  gtk_widget_show(ret->treeview);

  ret->notebook = gtk_notebook_new();

  gtk_notebook_set_group_name(GTK_NOTEBOOK(ret->notebook), NOTEBOOK_GROUP);

  
  g_signal_connect(G_OBJECT(ret->notebook), "create-window",
                   G_CALLBACK(create_window_callback),
                   ret);
  
  gtk_notebook_set_scrollable(GTK_NOTEBOOK(ret->notebook), TRUE);
  gtk_notebook_popup_enable(GTK_NOTEBOOK(ret->notebook));
  
  gtk_widget_show(ret->notebook);
  
  ret->box = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_paned_pack1(GTK_PANED(ret->box), scrolledwindow, TRUE, TRUE);
  gtk_paned_pack2(GTK_PANED(ret->box), ret->notebook, TRUE, TRUE);
  gtk_widget_show(ret->box);
  
  ret->idle_tag = g_timeout_add(50, idle_func, ret);

  ret->playback_id = calloc(1, 37);
  
  uuid_generate(u);
  uuid_unparse(u, ret->playback_id);
  
  bg_gtk_mdb_menu_init(&ret->menu, ret);
  
  
  ret->dlg_sink = bg_msg_sink_create(handle_dlg_message, ret, 1);

  /* Browse children of '/' */
  msg = bg_msg_sink_get(ret->cache_ctrl.cmd_sink);
  gavl_msg_set_id_ns(msg, BG_CMD_DB_CACHE_CONTAINER_EXPAND, BG_MSG_NS_DB_CACHE);
  gavl_dictionary_set_string(&msg->header, GAVL_MSG_CONTEXT_ID, "/");
  bg_msg_sink_put(ret->cache_ctrl.cmd_sink);
  
  return ret;
  }

GtkWidget * bg_gtk_mdb_tree_get_widget(bg_gtk_mdb_tree_t * w)
  {
  return w->box;
  }

void bg_gtk_mdb_tree_destroy(bg_gtk_mdb_tree_t * t)
  {
  /* Disconnect ourselves */
  if(t->player_ctrl_p)
    bg_controllable_disconnect(t->player_ctrl_p, &t->player_ctrl);
  
  bg_controllable_disconnect(t->mdb_ctrl_p, &t->ctrl);
  
  g_source_remove(t->idle_tag);
  
  bg_control_cleanup(&t->ctrl);
  bg_control_cleanup(&t->player_ctrl);

  gavl_array_free(&t->list_icons);
  gavl_array_free(&t->tree_icons);
  
  if(t->playback_id)
    free(t->playback_id);

  if(t->cur)
    free(t->cur);
  

  bg_msg_sink_destroy(t->dlg_sink);
  
  free(t);
  }

void bg_gtk_mdb_tree_unset_player_ctrl(bg_gtk_mdb_tree_t * t)
  {
  if(t->player_ctrl_p)
    {
    bg_controllable_disconnect(t->player_ctrl_p, &t->player_ctrl);
    t->player_ctrl_p = NULL;
    
    bg_mdb_cache_set_player(t->cache, NULL);
    }
  }

void bg_gtk_mdb_tree_set_player_ctrl(bg_gtk_mdb_tree_t * t, bg_controllable_t * player_ctrl)
  {
  t->player_ctrl_p = player_ctrl;
  if(t->player_ctrl_p)
    bg_controllable_connect(t->player_ctrl_p, &t->player_ctrl);
  bg_mdb_cache_set_player(t->cache, player_ctrl);
  }

void bg_gtk_mdb_tree_delete_selected_album(bg_gtk_mdb_tree_t * t)
  {
  GtkTreeSelection * selection;
  GtkTreeModel * model;
  GtkTreeIter iter;
  char * id = NULL;
  char * parent_id = NULL;

  selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(t->treeview));

  if(gtk_tree_selection_get_selected(selection, &model, &iter) &&
     (id = iter_to_id_tree(GTK_TREE_VIEW(t->treeview), &iter)) &&
     (parent_id = bg_mdb_get_parent_id(id)))
    {
    gavl_array_t arr;
    gavl_msg_t * msg = bg_msg_sink_get(t->cache_ctrl.cmd_sink);

    gavl_array_init(&arr);

    gavl_msg_set_id_ns(msg, BG_CMD_DB_CACHE_DELETE_CONTAINERS, BG_MSG_NS_DB_CACHE);
    gavl_dictionary_set_string(&msg->header, GAVL_MSG_CONTEXT_ID, parent_id);

    gavl_string_array_add(&arr, id);
    gavl_msg_set_arg_array(msg, 0, &arr);
    
    bg_msg_sink_put(t->cache_ctrl.cmd_sink);
    gavl_array_free(&arr);
    }
  else
    {
    //    fprintf(stderr, "Don't delete %s %s\n", id, parent_id);
    
    }
  
  if(parent_id)
    free(parent_id);
  
  return;
  }

void bg_gtk_mdb_list_set_current(list_t * list, const char * hash)
  {
  GtkTreeIter it;
  gchar * list_hash;
  GtkTreeModel * model = gtk_tree_view_get_model(GTK_TREE_VIEW(list->listview));
  
  if(!gtk_tree_model_get_iter_first(model, &it))
    return;

  while(1)
    {
    if(!hash)
      gtk_list_store_set(GTK_LIST_STORE(model), &it, LIST_COLUMN_CURRENT, "", -1);
    else
      {
      gtk_tree_model_get(model, &it, LIST_COLUMN_HASH, &list_hash, -1);
      if(list_hash && !strcmp(list_hash, hash))
        gtk_list_store_set(GTK_LIST_STORE(model), &it, LIST_COLUMN_CURRENT, BG_ICON_PLAY, -1);
      else
        gtk_list_store_set(GTK_LIST_STORE(model), &it, LIST_COLUMN_CURRENT, "", -1);
      }
    
    if(!gtk_tree_model_iter_next(model, &it))
      break;
    
      
    }
  }
