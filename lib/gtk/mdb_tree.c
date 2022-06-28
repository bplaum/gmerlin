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

#include <string.h>
#include <uuid/uuid.h>

#include <gtk/gtk.h>

#include <config.h>

#include <gavl/metatags.h>

#include <gmerlin/msgqueue.h>
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

// #define DUMP_MESSAGES

static char * iter_to_id_tree(GtkTreeView *treeview, GtkTreeIter * iter);

static const gavl_dictionary_t * id_to_album(bg_gtk_mdb_tree_t * t, const char * id);

static void album_cleanup(album_t * l);

static int id_to_iter(GtkTreeView *treeview, const char * id, GtkTreeIter * ret);

static void splice_children_tree_internal(bg_gtk_mdb_tree_t * t,
                                          const char * id,
                                          int idx, int del, const gavl_value_t * add);

static int row_is_expanded(bg_gtk_mdb_tree_t * t, const char * id);

enum
{
  TREE_COLUMN_ICON,
  TREE_COLUMN_HAS_ICON,
  TREE_COLUMN_PIXBUF,
  TREE_COLUMN_HAS_PIXBUF,
  TREE_COLUMN_LABEL,
  TREE_COLUMN_COLOR,
  TREE_COLUMN_EXPANDED,
  TREE_COLUMN_ID,
  TREE_COLUMN_SEARCH_STRING,
  NUM_TREE_COLUMNS
};

static void set_pixbuf(void * data, GdkPixbuf * pb)
  {
  GtkTreeIter iter;
  load_image_t * d = data;
  GtkTreeModel * model;

  if(!d->id)
    goto fail;
  
  if(!id_to_iter(GTK_TREE_VIEW(d->tree->treeview), d->id, &iter))
    goto fail;

  model = gtk_tree_view_get_model(GTK_TREE_VIEW(d->tree->treeview));

  gtk_tree_store_set(GTK_TREE_STORE(model), &iter, TREE_COLUMN_HAS_PIXBUF, TRUE,
                     TREE_COLUMN_HAS_ICON, FALSE,
                     TREE_COLUMN_PIXBUF, pb, -1);
  
  fail:
  
  return;
  }

#if 1
static void pixbuf_from_uri_callback_tree(void * data, GdkPixbuf * pb)
  {
  load_image_t * d = data;

  if(pb)
    set_pixbuf(data, pb);
  if(d->id)
    free(d->id);
  d->tree->icons_loading--;
  free(d);
  }
#endif

static void pixbuf_from_uri_callback_list(void * data, GdkPixbuf * pb)
  {
  load_image_t * d = data;
  
  bg_gtk_mdb_list_set_pixbuf(data, pb);

  if(d->id)
    free(d->id);
  d->tree->icons_loading--;
  free(d);
  }


static void load_icons(bg_gtk_mdb_tree_t * t)
  {
  const char * id;
  GtkTreeIter iter;
  const gavl_dictionary_t * dict;
  //  GdkPixbuf * buf;

  gavl_timer_t * timer = NULL;

  /* Load icons:
     t->icons_to_load contains ids of tree nodes whose icons are to be read
     t->list_icons_to_load contains ids of list items whose icons are to be read

     We load icons until a time of 100 ms passed *OR* a maximum of MAX_BG_ICON_LOADS
     icons are loaded simultaneously in the background
  */
  
  while(t->icons_to_load.num_entries &&
        (id = gavl_value_get_string(&t->icons_to_load.entries[0])))
    {
    /* We first remove the entry from  the list so we won't end up here again */

    if(!timer)
      {
      timer = gavl_timer_create();
      gavl_timer_start(timer);
      }
    /* REMOVE */
    
    if(id_to_iter(GTK_TREE_VIEW(t->treeview), id, &iter) &&
       (dict = id_to_album(t, id)))
      {
      load_image_t * d;
      d = calloc(1, sizeof(*d));
      d->id = gavl_strdup(id);
      d->tree = t;
      t->icons_loading += bg_gtk_load_track_image_async(pixbuf_from_uri_callback_tree, d, dict, 48, 72);
      }
    
    /* END REMOVE */

    bg_gtk_mdb_array_set_flag_str(&t->icons_to_load, id, 0);
    
    if((gavl_timer_get(timer) > GAVL_TIME_SCALE/10) || (t->icons_loading > MAX_BG_ICON_LOADS))
      return;
    
    }

  while(t->list_icons_to_load.num_entries &&
        (id = gavl_value_get_string(&t->list_icons_to_load.entries[0])))
    {
    list_t * l;
    album_t * a;
    char * parent_id = NULL;
    
    if(!timer)
      {
      timer = gavl_timer_create();
      gavl_timer_start(timer);
      }
    
    /* REMOVE */

    if((parent_id = bg_mdb_get_parent_id(id)) &&
       (a = bg_gtk_mdb_album_is_open(t, parent_id)) &&
       (l = a->list) &&
       a->a &&
       bg_gtk_mdb_list_id_to_iter(GTK_TREE_VIEW(l->listview), &iter, id))
      {
      const gavl_dictionary_t * dict;
      load_image_t * d;
      d = calloc(1, sizeof(*d));
      d->id = gavl_strdup(id);
      d->tree = t;

      if(!a->a || !(dict = gavl_get_track_by_id(a->a, id)))
        {
        bg_gtk_mdb_array_set_flag_str(&t->list_icons_to_load, id, 0);
        continue;
        }
      
      t->icons_loading +=
        bg_gtk_load_track_image_async(pixbuf_from_uri_callback_list, d, dict, LIST_ICON_WIDTH, LIST_ICON_WIDTH * 3 / 2);
      }
    
    /* END REMOVE */

    bg_gtk_mdb_array_set_flag_str(&t->list_icons_to_load, id, 0);
    
    if((gavl_timer_get(timer) > GAVL_TIME_SCALE/10) || (t->icons_loading > MAX_BG_ICON_LOADS))
      return;
    
    }
  
  return;
  }


int bg_gtk_mdb_array_get_flag_str(const gavl_array_t * arr, const char * id)
  {
  if(gavl_string_array_indexof(arr, id) >= 0)
    return 1;
  else
    return 0;
  }

void bg_gtk_mdb_array_set_flag_str(gavl_array_t * arr, const char * id, int flag)
  {
  if(flag)
    gavl_string_array_add(arr, id);
  else
    gavl_string_array_delete(arr, id);
  }

static void splice_children_array(album_array_t * arr, const char * id,
                                  int idx, int del, gavl_value_t * add)
  {
  int i;
  
  for(i = 0; i < arr->num_albums; i++)
    {
    if(!strcmp(arr->albums[i]->id, id))
      {
      if(arr->albums[i]->list)
        bg_gtk_mdb_list_splice_children(arr->albums[i]->list, idx, del, add, 1);

      else if(arr->albums[i]->a)
        gavl_track_splice_children(arr->albums[i]->a, idx, del, add);
      }
    }
  }

static void set_object_array(album_array_t * arr, const char * id,
                             const gavl_dictionary_t * obj)
  {
  int i;
  
  for(i = 0; i < arr->num_albums; i++)
    {
    if(strcmp(arr->albums[i]->id, id))
      continue;

    if(!arr->albums[i]->a)
      {
      arr->albums[i]->a = gavl_dictionary_create();
      gavl_dictionary_copy(arr->albums[i]->a, obj);
      }
    else
      {
      const gavl_dictionary_t * src_m;
      gavl_dictionary_t * dst_m;

      src_m = gavl_track_get_metadata(obj);
      dst_m = gavl_track_get_metadata_nc(arr->albums[i]->a);
      gavl_dictionary_update_fields(dst_m, src_m);
      }
    
    if(arr->albums[i]->list)
      bg_gdk_mdb_list_set_obj(arr->albums[i]->list, arr->albums[i]->a);
    }
  
  
  }

#if 0
static void album_move(album_t * dst, album_t * src)
  {
  memcpy(dst, src, sizeof(*dst));
  memset(src, 0, sizeof(*src));
  }
#endif

static album_t * album_array_splice_nocopy(bg_gtk_mdb_tree_t * tree,
                                           album_array_t * arr, int idx, int del, const char * id, album_t * a)
  {
  int i;
  album_t * ret = NULL;
  const gavl_dictionary_t * dict;
  
  if(idx < 0)
    idx = arr->num_albums;
  if(del < 0)
    del = arr->num_albums - idx;

  if(del > 0)
    {
    for(i = 0; i < del; i++)
      {
      if(arr->albums[idx + i])
        album_cleanup(arr->albums[idx + i]);
      }
    if(idx + del < arr->num_albums)
      memmove(arr->albums + idx, arr->albums + (idx + del),
              (arr->num_albums - idx - del) * sizeof(*arr->albums));
    arr->num_albums -= del;
    memset(arr->albums + arr->num_albums, 0, del * sizeof(*arr->albums));
    }

  if(id)
    {
    if(arr->num_albums + 1 > arr->albums_alloc)
      {
      arr->albums_alloc += 32;
      arr->albums = realloc(arr->albums, arr->albums_alloc * sizeof(*arr->albums));
      memset(arr->albums + arr->num_albums, 0, (arr->albums_alloc - arr->num_albums) * sizeof(*arr->albums));
      }

    /* Make space for the element */
    if(idx < arr->num_albums)
      memmove(arr->albums + (idx + 1), arr->albums + idx, (arr->num_albums - idx) * sizeof(*arr->albums));

    if(a)
      arr->albums[idx] = a;
    else
      arr->albums[idx] = calloc(1, sizeof(*ret));
    
    ret = arr->albums[idx];
    
    ret->id = gavl_strrep(ret->id, id);
    ret->t = tree;

    if(!ret->a)
      {
      /* Try to get the track info */
      if((dict = id_to_album(ret->t, ret->id)))
        {
        ret->a = gavl_dictionary_create();
        gavl_dictionary_copy(ret->a, dict);
        }
      else
        {
        /* TODO: Load container metadata */
        }
      }
    
    
    arr->num_albums++;
    }
  
  return ret;
  }

static void album_array_free(album_array_t * arr)
  {
  album_array_splice_nocopy(NULL, arr, 0, -1, NULL, NULL);
  if(arr->albums)
    free(arr->albums);
  }

static album_t * album_array_move_1(bg_gtk_mdb_tree_t * tree,
                                    album_array_t * arr, int old_idx, int new_idx)
  {
  album_t * a;
  album_t * ret;
  gavl_value_t val;
  char * id;

  a = arr->albums[old_idx];
  arr->albums[old_idx] = NULL;
  
  /* Delete old */
  album_array_splice_nocopy(tree, arr, old_idx, 1, NULL, NULL);
  
  /* Add new */
  id = gavl_value_get_string_nc(&val);
  
  ret = album_array_splice_nocopy(tree, arr, new_idx, 0, id, a);
  
  return ret;
  }

static int album_array_has_id(album_array_t * arr, const char * id)
  {
  int i;

  for(i = 0; i < arr->num_albums; i++)
    {
    if(!strcmp(arr->albums[i]->id, id))
      return 1;
    }
  return 0;
  }

static album_t * album_array_get_by_id(album_array_t * arr, const char * id)
  {
  int i;

  if(!id)
    return NULL;
  
  for(i = 0; i < arr->num_albums; i++)
    {
    if(!strcmp(arr->albums[i]->id, id))
      return arr->albums[i];
    }
  return NULL;
  }


#if 0
static int album_array_has_iter(GtkTreeView *treeview, album_array_t * arr, GtkTreeIter * iter)
  {
  int ret = 0;
  char * id = iter_to_id_tree(treeview, iter);
  if(id)
    {
    ret = album_array_has_id(arr, id);
    free(id);
    }
  return ret;
  }
#endif

static void album_array_delete_by_id(bg_gtk_mdb_tree_t * tree,
                                     album_array_t * arr, const char * id)
  {
  int idx = 0;
  while(idx < arr->num_albums)
    {
    if(!strcmp(arr->albums[idx]->id, id))
      album_array_splice_nocopy(tree, arr, idx, 1, NULL, NULL);
    else
      idx++;
    }
  }

static void album_array_delete_by_ancestor(bg_gtk_mdb_tree_t * tree,
                                           album_array_t * arr, const char * id)
  {
  int idx = 0;

  //  fprintf(stderr, "album_array_delete_by_ancestor %s\n", id);
  
  while(idx < arr->num_albums)
    {
    if(bg_mdb_is_ancestor(id, arr->albums[idx]->id))
      {
      char * sub_id = gavl_strdup(arr->albums[idx]->id);
      album_array_delete_by_id(tree, arr, sub_id);
      free(sub_id);
      }
    else
      idx++;
    }
  
  }

static album_t * album_array_move_2(bg_gtk_mdb_tree_t * tree,
                                    album_array_t * dst, album_array_t * src, int dst_idx, int src_idx)
  {
  album_t * ret;
  
  ret = album_array_splice_nocopy(tree, dst, dst_idx, 0,
                                  src->albums[src_idx]->id,
                                  src->albums[src_idx]);
  
  src->albums[src_idx] = NULL;
  album_array_splice_nocopy(tree, src, src_idx, 1, NULL, NULL);
  return ret;
  }

static const gavl_dictionary_t * album_array_get_album(const album_array_t * arr, const char * id)
  {
  int i;
  
  for(i = 0; i < arr->num_albums; i++)
    {
    if(arr->albums[i]->id && !strcmp(id, arr->albums[i]->id) && arr->albums[i]->a)
      return arr->albums[i]->a;
    }
  return NULL;
  }

static void open_album(bg_gtk_mdb_tree_t * t, const char * id);

/* close_album() works by index because multiple album widgets can refer to the same album */

/* Tree widget */

static const gavl_dictionary_t * id_to_album(bg_gtk_mdb_tree_t * t, const char * id)
  {
  char * parent_id;
  const gavl_dictionary_t * ret;

  
  if((ret = album_array_get_album(&t->tab_albums, id)) ||
     (ret = album_array_get_album(&t->win_albums, id)) ||
     (ret = album_array_get_album(&t->exp_albums, id)))
    {
    return ret;
    }

  parent_id = bg_mdb_get_parent_id(id);

  if((ret = album_array_get_album(&t->tab_albums, parent_id)) ||
     (ret = album_array_get_album(&t->win_albums, parent_id)) ||
     (ret = album_array_get_album(&t->exp_albums, parent_id)))
    {
    ret = gavl_get_track_by_id(ret, id);
    }

  free(parent_id);

  if(ret)
    return ret;
  
  /* Browse object */
  
  bg_gtk_mdb_browse_object(t, id);
  return NULL;
  }

static void album_cleanup(album_t * a)
  {
  int idx;
  
  if(a->list && a->list->widget && ((idx = gtk_notebook_page_num(GTK_NOTEBOOK(a->t->notebook), a->list->widget)) >= 0))
    {
    gtk_notebook_remove_page(GTK_NOTEBOOK(a->t->notebook), idx);
    a->list->widget = NULL;
    }
  
  if(a->list)
    {
    bg_gtk_mdb_list_destroy(a->list);
    a->list = NULL;
    }
  
  if(!a->local)
    {
    if(a->id)
      free(a->id);

    if(a->a)
      gavl_dictionary_destroy(a->a);

    free(a);
    }
  
  //  memset(a, 0, sizeof(*a));
  }

/* Utils */

album_t * bg_gtk_mdb_album_is_open(bg_gtk_mdb_tree_t * tree, const char * id)
  {
  album_t * a;

  if(!(a = album_array_get_by_id(&tree->tab_albums, id)) &&
     !(a = album_array_get_by_id(&tree->win_albums, id)))
    return 0;

  return a;
  }

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

static int id_to_iter(GtkTreeView *treeview, const char * id, GtkTreeIter * ret)
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
    fprintf(stderr, "id_to_iter(%s) Couldn't initialize child_iter\n", id);
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

static void check_dummy_entry(bg_gtk_mdb_tree_t * widget, GtkTreeIter * parent_iter,
                            const gavl_dictionary_t * parent_obj)
  {
  GtkTreeIter child_iter;
  GtkTreeModel * model;
  GtkTreePath* path;
  
  model = gtk_tree_view_get_model(GTK_TREE_VIEW(widget->treeview));

  //  fprintf(stderr, "check dummy entry\n");
  //  gavl_dictionary_dump(parent_obj, 2);
    
  if(gavl_track_is_locked(parent_obj))
    {
    return;
    }

  /* Need to use gtk here to query expanded status */

  path = gtk_tree_model_get_path(model, parent_iter);
  
  if(gtk_tree_view_row_expanded(GTK_TREE_VIEW(widget->treeview), path))
    {
    gtk_tree_path_free(path);
    return;
    }
  gtk_tree_path_free(path);
  
  /* Container has no child containers */
  
  if(gavl_track_get_num_container_children(parent_obj) <= 0)
    {
    if(gtk_tree_model_iter_children(model, &child_iter, parent_iter) &&
       is_dummy_entry(widget, &child_iter))
      gtk_tree_store_remove(GTK_TREE_STORE(model), &child_iter);
    return;
    }
  
  

  /* Check if we already have a dummy entry */
  if(gtk_tree_model_iter_children(model, &child_iter, parent_iter) &&
     is_dummy_entry(widget, &child_iter))
    return;
  
  gtk_tree_store_append(GTK_TREE_STORE(model), &child_iter, parent_iter);
  gtk_tree_store_set(GTK_TREE_STORE(model), &child_iter, TREE_COLUMN_ID, "DUMMY", -1);
  }

static void delete_dummy_entry(bg_gtk_mdb_tree_t * w, GtkTreeIter * iter)
  {
  GtkTreeIter child_iter;
  GtkTreeModel * model;
  
  //  fprintf(stderr, "Delete dummy entry\n");

  model = gtk_tree_view_get_model(GTK_TREE_VIEW(w->treeview));
  if(!gtk_tree_model_iter_children(model, &child_iter, iter) ||
     !is_dummy_entry(w, &child_iter))
    return;
  
  gtk_tree_store_remove(GTK_TREE_STORE(model), &child_iter);
  }

static int row_is_expanded(bg_gtk_mdb_tree_t * t, const char * id)
  {
  return album_array_has_id(&t->exp_albums, id);
  }

static void row_set_expanded(bg_gtk_mdb_tree_t * t, const char * id, int expanded)
  {
  if(expanded == row_is_expanded(t, id))
    return;

  if(expanded)
    album_array_splice_nocopy(t, &t->exp_albums, -1, 0, id, NULL);
  else
    album_array_delete_by_id(t, &t->exp_albums, id);
  
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
  
  tag_string = bg_sprintf("<span "META_ATTRIBUTES"><span font_family=\"%s\" weight=\"normal\">%s</span> %s</span> ",
                          BG_ICON_FONT_FAMILY, icon, tmp_string);
  g_free(tmp_string);

  ret = gavl_strcat(ret, tag_string);
  free(tag_string);
  return ret;
  }

char * bg_gtk_mdb_tree_create_markup(const gavl_dictionary_t * dict, const char * parent_klass)
  {
  gavl_time_t duration;

  const char * var;
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
  
  klass = gavl_dictionary_get_string(m, GAVL_META_MEDIA_CLASS);

  if(!(var = gavl_dictionary_get_string(m, GAVL_META_STATION)))
    var = gavl_dictionary_get_string(m, GAVL_META_LABEL);
  
  tmp_string = g_markup_printf_escaped("%s", var);

  if(locked)
    {
    markup = bg_sprintf("<markup><span "LOCKED_ATTRIBUTES">%s <span font_family=\"%s\" weight=\"normal\">%s</span></span>\n", tmp_string, BG_ICON_FONT_FAMILY, BG_ICON_LOCK);
    }
  else
    markup = bg_sprintf("<markup><span "NORMAL_ATTRIBUTES">%s</span>\n", tmp_string);
  
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
      tmp_string = bg_sprintf("<span "META_ATTRIBUTES"><span font_family=\"%s\" weight=\"normal\">%s</span> %d</span> ",
                              BG_ICON_FONT_FAMILY, child_icon, items + containers);
      markup = gavl_strcat(markup, tmp_string);
      free(tmp_string);
      }
    else
      {
      if(containers)
        {
        tmp_string = bg_sprintf("<span "META_ATTRIBUTES"><span font_family=\"%s\" weight=\"normal\">%s</span> %d</span> ",
                                BG_ICON_FONT_FAMILY, BG_ICON_FOLDER, containers);
        
        markup = gavl_strcat(markup, tmp_string);
        free(tmp_string);
        }
      
      if(items)
        {
        if(containers)
          markup = gavl_strcat(markup , " ");
        
        tmp_string = bg_sprintf("<span "META_ATTRIBUTES"><span font_family=\"%s\" weight=\"normal\">%s</span> %d</span> ",
                                BG_ICON_FONT_FAMILY, BG_ICON_FILE, items);
        
        markup = gavl_strcat(markup, tmp_string);
        free(tmp_string);
        
        }
      }
    
    }

  if(!strcmp(klass, GAVL_META_MEDIA_CLASS_SONG))
    {
    if((var = gavl_dictionary_get_arr(m, GAVL_META_ARTIST, 0)))
      {
      tmp_string = gavl_metadata_join_arr(m, GAVL_META_ARTIST, ", ");
      markup = append_meta_tag(markup, tmp_string, BG_ICON_MICROPHONE);
      free(tmp_string);
      }
  
    if((var = gavl_dictionary_get_string(m, GAVL_META_ALBUM)) &&
       (!parent_klass || strcmp(parent_klass, GAVL_META_MEDIA_CLASS_MUSICALBUM)))
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

  if(klass && gavl_string_starts_with(klass, GAVL_META_MEDIA_CLASS_MOVIE))
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
  
  const gavl_dictionary_t * m = gavl_track_get_metadata(dict);
  
  model = gtk_tree_view_get_model(GTK_TREE_VIEW(t->treeview));

  locked = gavl_track_is_locked(dict);
  
  if(!m)
    return;

  id = gavl_dictionary_get_string(m, GAVL_META_ID);
  
  if((klass = gavl_dictionary_get_string(m, GAVL_META_MEDIA_CLASS)))
    {
    gtk_tree_store_set(GTK_TREE_STORE(model), iter,
                       TREE_COLUMN_ICON, bg_get_type_icon(klass),
                       TREE_COLUMN_HAS_ICON, TRUE, -1);
    }

  markup = bg_gtk_mdb_tree_create_markup(dict, NULL);
  gtk_tree_store_set(GTK_TREE_STORE(model), iter, TREE_COLUMN_LABEL, markup, -1);
  free(markup);

  bg_gtk_mdb_array_set_flag_str(&t->icons_to_load, id, 1);
  
  gtk_tree_store_set(GTK_TREE_STORE(model), iter, TREE_COLUMN_COLOR, "#000000", -1);
  gtk_tree_store_set(GTK_TREE_STORE(model), iter, TREE_COLUMN_ID, id, -1);

  if((var = gavl_dictionary_get_string(m, GAVL_META_SEARCH_TITLE)) ||
     (var = gavl_dictionary_get_string(m, GAVL_META_TITLE)) ||
     (var = gavl_dictionary_get_string(m, GAVL_META_LABEL)))
    gtk_tree_store_set(GTK_TREE_STORE(model), iter, TREE_COLUMN_SEARCH_STRING, var, -1);
      

  if(locked)
    {
    GtkTreePath * path = gtk_tree_model_get_path(model, iter);
    gtk_tree_view_collapse_row(GTK_TREE_VIEW(t->treeview), path);
    gtk_tree_path_free(path);

    album_array_delete_by_ancestor(t, &t->tab_albums, id);
    album_array_delete_by_ancestor(t, &t->win_albums, id);
    /* Not necessary? */
    album_array_delete_by_ancestor(t, &t->exp_albums, id);
    
    delete_dummy_entry(t, iter);
    }
  else
    check_dummy_entry(t, iter, dict);
  }

static int is_container(const gavl_value_t * val)
  {
  const gavl_dictionary_t * dict;
  const gavl_dictionary_t * m;
  const char * klass;

  if((dict = gavl_value_get_dictionary(val)) &&
     (m = gavl_track_get_metadata(dict)) &&
     (klass = gavl_dictionary_get_string(m, GAVL_META_MEDIA_CLASS)) &&
     gavl_string_starts_with(klass, "container"))
    return 1;
  
  return 0;
  }

/* Internal to GUI index */
static int tree_transform_idx(const gavl_dictionary_t * dict, int idx)
  {
  int i;
  const gavl_dictionary_t * d = NULL;
  const char * klass = NULL;
  int ret = 0;

  if(!(d = gavl_track_get_metadata(dict)) ||
     !(klass = gavl_dictionary_get_string(d, GAVL_META_MEDIA_CLASS)) ||
     !gavl_string_starts_with(klass, "container"))
    {
    //    fprintf(stderr, "Cannot transform index:\n");
    //    gavl_dictionary_dump(dict, 2);
    //    fprintf(stderr, "\n");
    
    return -1;
    }
  for(i = 0; i < idx; i++)
    {
    if((d = gavl_get_track(dict, i)) &&
       (d = gavl_track_get_metadata(d)) &&
       (klass = gavl_dictionary_get_string(d, GAVL_META_MEDIA_CLASS)) &&
       gavl_string_starts_with(klass, "container"))
      ret++;
    }
  return ret;
  }

static void splice_children_tree_internal(bg_gtk_mdb_tree_t * t,
                                          const char * id,
                                          int idx, int del, const gavl_value_t * add)
  {
  GtkTreeIter iter;
  GtkTreeIter * parent;
  GtkTreeIter parent1;
  int expanded = 0;
  GtkTreeModel * model;
  album_t * a;
  int num_added = 0;
  const gavl_dictionary_t * dict;
  const gavl_dictionary_t * child;
  int i;
  int idx_real;
  const gavl_array_t * arr;

  int offset = 0;

  /*  
  fprintf(stderr, "splice_children_tree_internal %s %d %d\n", id, idx, del);
  gavl_value_dump(add, 2);
  fprintf(stderr, "\n");
  */
  
  model = gtk_tree_view_get_model(GTK_TREE_VIEW(t->treeview));
    
  if(!id_to_iter(GTK_TREE_VIEW(t->treeview), id, &parent1))
    {
    parent = NULL;
    offset = t->local_folders;
    }
  else
    parent = &parent1;
  
  if(parent)
    expanded = row_is_expanded(t, id);
 
  if(!(a = album_array_get_by_id(&t->exp_albums, id)) ||
     !(dict = a->a))
    return;

  /* Delete dummy entry */
  if(!idx && parent && add && (add->type != GAVL_TYPE_UNDEFINED))
    delete_dummy_entry(t, parent);
  
  /* Remove entries */
  
  if(dict && (del > 0))
    {
    for(i = 0; i < del; i++)
      {
      if((idx_real = tree_transform_idx(dict, idx)) >= 0)
        {
        /* Remove container */
        
        if(gtk_tree_model_iter_nth_child(model, &iter, parent,
                                          idx_real + offset))
          {
          GValue value={0,};
          const char * iter_id;
          
          gtk_tree_model_get_value(model, &iter, TREE_COLUMN_ID, &value);
        
          iter_id = g_value_get_string(&value);
        
          album_array_delete_by_ancestor(t, &t->tab_albums, iter_id);
          album_array_delete_by_ancestor(t, &t->win_albums, iter_id);
          album_array_delete_by_ancestor(t, &t->exp_albums, iter_id);
        
          g_value_unset(&value);
        
          if(!gtk_tree_store_remove(GTK_TREE_STORE(model), &iter))
            break;
          
          }
        }
      }
    }

  /* Add entries */
  
  if(is_container(add))
    {
    idx_real = tree_transform_idx(dict, idx);

    child = gavl_value_get_dictionary(add);
    //    fprintf(stderr, "INSERT: %s %d %d %d\n", gavl_track_get_id(child), idx, idx_real, offset);
    gtk_tree_store_insert(GTK_TREE_STORE(model), &iter, parent, idx_real + offset);
    set_entry_tree(t, child, &iter);
    }
  else if((arr = gavl_value_get_array(add)))
    {
    for(i = 0; i < arr->num_entries; i++)
      {
      idx_real = tree_transform_idx(dict, idx);
      
      if(is_container(arr->entries + i))
        {
        child = gavl_value_get_dictionary(arr->entries + i);
        //        fprintf(stderr, "INSERT: %s %d %d %d %d\n", gavl_track_get_id(child), idx, idx_real, offset, num_added);
        gtk_tree_store_insert(GTK_TREE_STORE(model), &iter, parent, idx_real + num_added + offset);
        set_entry_tree(t, child, &iter);
        num_added++;
        }
      }
    }

  if(expanded && parent && (num_added > 0))
    {
    GtkTreePath * path = gtk_tree_model_get_path(model, parent);

    g_signal_handler_block(G_OBJECT(t->treeview), t->row_expanded_tag);
    gtk_tree_view_expand_row(GTK_TREE_VIEW(t->treeview), path, FALSE);
    g_signal_handler_unblock(G_OBJECT(t->treeview), t->row_expanded_tag);

    gtk_tree_path_free(path);
    }
  
  bg_gtk_widget_queue_redraw(t->treeview);
  
  }

static void splice_children_tree(bg_gtk_mdb_tree_t * t, gavl_msg_t * msg)
  {
  const char * ctx_id;

  int last;
  int idx;
  
  int del;

  gavl_value_t add;
  gavl_value_init(&add);

  //  fprintf(stderr, "Splice children tree\n");
  //  gavl_msg_dump(msg, 2);
  
  if(!(ctx_id = gavl_dictionary_get_string(&msg->header, GAVL_MSG_CONTEXT_ID)))
    return;

  gavl_msg_get_splice_children(msg, &last, &idx, &del, &add);

  /* Splice children first */
  splice_children_tree_internal(t, ctx_id, idx, del, &add);

  /* Splice lists also */
  splice_children_array(&t->tab_albums, ctx_id, idx, del, &add);
  splice_children_array(&t->win_albums, ctx_id, idx, del, &add);
  splice_children_array(&t->exp_albums, ctx_id, idx, del, &add);

  
  gavl_value_free(&add);
  }

void bg_gtk_mdb_tree_update_node(bg_gtk_mdb_tree_t * t,
                                 const char * ctx_id,
                                 const gavl_dictionary_t * new_dict)
  {
  //  album_t * a;
  
  GtkTreeIter iter;

  //  fprintf(stderr, "Object changed %s\n", ctx_id);
  //  gavl_dictionary_dump(new_dict, 2);
  //  fprintf(stderr, "\n");
  
  if(id_to_iter(GTK_TREE_VIEW(t->treeview), ctx_id, &iter))
    {
    gavl_dictionary_t * dict;
    album_t * a;
    const gavl_dictionary_t * m;
    char * parent_id = bg_mdb_get_parent_id(ctx_id);

    /* Check if the entry is visible (i.e. if the parent container is expanded */
    
    if((a = album_array_get_by_id(&t->exp_albums, parent_id)) &&
       a->a &&
       (dict = gavl_get_track_by_id_nc(a->a, ctx_id)))
      {
      int num_old = 0;
      int num_new = 0;
              
      m = gavl_track_get_metadata(dict);

      gavl_dictionary_get_int(m, GAVL_META_NUM_CHILDREN, &num_old);
      
      if(new_dict)
        bg_mdb_object_changed(dict, new_dict); 

      gavl_dictionary_get_int(m, GAVL_META_NUM_CHILDREN, &num_new);
      set_entry_tree(t, dict, &iter);

      //      fprintf(stderr, "bg_gtk_mdb_tree_update_node %s %d -> %d\n",
      //              ctx_id, num_old, num_new);
      
      if(!num_new)
        row_set_expanded(t, ctx_id, 0);
      }
    else if(!strcmp(ctx_id, BG_PLAYQUEUE_ID))
      {
      bg_mdb_object_changed(t->playqueue.a, new_dict); 
      set_entry_tree(t, t->playqueue.a, &iter);
      }
    
    free(parent_id);
    }

  }

static int handle_msg(void * data, gavl_msg_t * msg)
  {
  bg_gtk_mdb_tree_t * t = data;

#ifdef DUMP_MESSAGES
  gavl_dprintf("Got message:\n");
  gavl_msg_dump(msg, 2);
#endif
  
  /* Handle events for the treeview */

  switch(msg->NS)
    {
    case BG_MSG_NS_DB:
      {
      switch(msg->ID)
        {
        case BG_MSG_DB_SPLICE_CHILDREN:
          {
          if(t->have_children)
            splice_children_tree(t, msg);
          }
          break;
        case BG_MSG_DB_OBJECT_CHANGED:
          {
          gavl_dictionary_t new_dict;
          const char * ctx_id;
          
          ctx_id = gavl_dictionary_get_string(&msg->header, GAVL_MSG_CONTEXT_ID);
          gavl_dictionary_init(&new_dict);
          gavl_msg_get_arg_dictionary(msg, 0, &new_dict);
          bg_gtk_mdb_tree_update_node(t, ctx_id, &new_dict);

          set_object_array(&t->tab_albums, ctx_id, &new_dict);
          set_object_array(&t->win_albums, ctx_id, &new_dict);
          set_object_array(&t->exp_albums, ctx_id, &new_dict);
          
          gavl_dictionary_free(&new_dict);
          
          }
          break;
        case BG_RESP_DB_BROWSE_CHILDREN:
          {
          const char * ctx_id;
          ctx_id = gavl_dictionary_get_string(&msg->header, GAVL_MSG_CONTEXT_ID);
          
          splice_children_tree(t, msg);
          bg_gtk_mdb_array_set_flag_str(&t->browse_children_requests, ctx_id, 0);

          if(gavl_msg_get_last(msg))
            {
            if(!t->have_children)
              t->have_children = 1;
            }
          }
          break;
        case BG_RESP_DB_BROWSE_OBJECT:
          {
          const char * ctx_id;
          gavl_dictionary_t dict;
          gavl_dictionary_init(&dict);
          gavl_msg_get_arg_dictionary(msg, 0, &dict);

          ctx_id = gavl_dictionary_get_string(&msg->header, GAVL_MSG_CONTEXT_ID);
          
          set_object_array(&t->tab_albums, ctx_id, &dict);
          set_object_array(&t->win_albums, ctx_id, &dict);
          set_object_array(&t->exp_albums, ctx_id, &dict);
          
          gavl_dictionary_free(&dict);

          bg_gtk_mdb_array_set_flag_str(&t->browse_object_requests, ctx_id, 0);
          
          bg_gtk_mdb_browse_children(t, ctx_id);
          }
          break;
        }
      }
    }
  
  return 1;
  }

/* Closed -> Tab */

static void open_album(bg_gtk_mdb_tree_t * t, const char * id)
  {
  GValue value={0,};
  
  const gavl_dictionary_t * dict;

  album_t * a = NULL;

  if(!strcmp(id, BG_PLAYQUEUE_ID))
    {
    a = &t->playqueue;
    }
  else
    {
    if((dict = id_to_album(t, id)) &&
       gavl_track_is_locked(dict))
      {
      gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Album %s is locked", id);
      return;
      }

    if(!bg_mdb_is_editable(dict) &&
       !gavl_track_get_num_item_children(dict))
      {
      gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Won't open %s: No items and not editable", id);
      return;
      }
    }
  
  if(bg_gtk_mdb_album_is_open(t, id))
    {
    /* TODO: Raise tabs or windows */
    gavl_log(GAVL_LOG_WARNING, LOG_DOMAIN, "Album %s is already open", id);
    return;
    }
  
  /* Allocate list */

  if(!a)
    a = album_array_splice_nocopy(t, &t->tab_albums, -1, 0, id, NULL);
  else
    album_array_splice_nocopy(t, &t->tab_albums, -1, 0, id, a);
  
  a->list = bg_gtk_mdb_list_create(a);
  
  gtk_notebook_append_page(GTK_NOTEBOOK(t->notebook), a->list->widget, a->list->tab);
  
  gtk_notebook_set_menu_label(GTK_NOTEBOOK(t->notebook), a->list->widget, a->list->menu_label);

  gtk_notebook_set_tab_reorderable(GTK_NOTEBOOK(t->notebook), a->list->widget, TRUE);
  gtk_notebook_set_tab_detachable(GTK_NOTEBOOK(t->notebook), a->list->widget, TRUE);

  g_value_init(&value, G_TYPE_BOOLEAN);
  
  g_value_set_boolean(&value, TRUE);
  gtk_container_child_set_property(GTK_CONTAINER(t->notebook), a->list->widget, "tab-fill", &value);
  
  g_value_set_boolean(&value, FALSE);
  gtk_container_child_set_property(GTK_CONTAINER(t->notebook), a->list->widget, "tab-expand", &value);
  g_value_unset(&value);

  }

void bg_gtk_mdb_tree_close_window_album(bg_gtk_mdb_tree_t * t, int idx)
  {
  GtkWidget * window = NULL;

  
  window = t->win_albums.albums[idx]->list->window;
  album_array_splice_nocopy(t, &t->win_albums, idx, 1, NULL, NULL);

  if(window)
    gtk_widget_destroy(window);
  }

void bg_gtk_mdb_tree_close_tab_album(bg_gtk_mdb_tree_t * t, int idx)
  {
  
  gtk_notebook_remove_page(GTK_NOTEBOOK(t->notebook), idx);
  album_array_splice_nocopy(t, &t->tab_albums, idx, 1, NULL, NULL);
  }

void bg_gtk_mdb_browse_children(bg_gtk_mdb_tree_t * t, const char * id)
  {
  const gavl_dictionary_t * dict;
  gavl_msg_t * msg;
  
  if(!(dict = id_to_album(t, id)))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN,
           "browse_children called but no parent object is cached: %s", id);
    return;
    }

  if(bg_gtk_mdb_array_get_flag_str(&t->browse_children_requests, id))
    return;
  
  bg_gtk_mdb_array_set_flag_str(&t->browse_children_requests, id, 1);
  
  msg = bg_msg_sink_get(t->ctrl.cmd_sink);
  bg_mdb_set_browse_children_request(msg, id, 0, -1, 0);
  bg_msg_sink_put(t->ctrl.cmd_sink, msg);
  }

void bg_gtk_mdb_browse_object(bg_gtk_mdb_tree_t * t, const char * id)
  {
  gavl_msg_t * msg;

  if(bg_gtk_mdb_array_get_flag_str(&t->browse_object_requests, id))
    return;

  bg_gtk_mdb_array_set_flag_str(&t->browse_object_requests, id, 1);
  
  msg = bg_msg_sink_get(t->ctrl.cmd_sink);
  
  gavl_msg_set_id_ns(msg, BG_FUNC_DB_BROWSE_OBJECT, BG_MSG_NS_DB);
  gavl_dictionary_set_string(&msg->header, GAVL_MSG_CONTEXT_ID, id);

  bg_msg_sink_put(t->ctrl.cmd_sink, msg);
  }

static void row_collapsed_callback(GtkTreeView *treeview,
                                   GtkTreeIter *arg1,
                                   GtkTreePath *arg2,
                                   gpointer user_data)
  {
  album_t * a;
  GtkTreeIter child_iter;
  char * id;
  GtkTreeModel *tree_model;
  bg_gtk_mdb_tree_t * t = user_data;
  id = iter_to_id_tree(treeview, arg1);
  a = album_array_get_by_id(&t->exp_albums, id);
  
  tree_model = gtk_tree_view_get_model(treeview);

  if(gtk_tree_model_iter_nth_child(tree_model, &child_iter, arg1, 0))
    {
    if(!is_dummy_entry(t, &child_iter))
      {
      while(gtk_tree_store_remove(GTK_TREE_STORE(tree_model), &child_iter))
        ;
      
      check_dummy_entry(t, arg1, a->a);
      }
    }

  row_set_expanded(t, id, 0);
  
  free(id);
  }

#if 1
static int have_children(bg_gtk_mdb_tree_t * w, GtkTreeIter * iter)
  {
  GtkTreeModel * model;
  GtkTreeIter child_iter;
  model = gtk_tree_view_get_model(GTK_TREE_VIEW(w->treeview));
  if(!gtk_tree_model_iter_nth_child(model, &child_iter, iter, 0))
    return 1;
  if(is_dummy_entry(w, &child_iter))
    return 0;
  return 1;
  }
#endif

static void row_expanded_callback(GtkTreeView *treeview,
                                  GtkTreeIter *arg1,
                                  GtkTreePath *arg2,
                                  gpointer user_data)
  {
  char * id;
  album_t * a;
  int num;
  int total;

  bg_gtk_mdb_tree_t * t = user_data;

  id = iter_to_id_tree(treeview, arg1);

  row_set_expanded(t, id, 1);

  /* Browse children only if we don't have them already */

  
  if((a = album_array_get_by_id(&t->exp_albums, id)))
    {
    //    fprintf(stderr, "row_expanded_callback %s %p\n", id, a);
    num = gavl_get_num_tracks_loaded(a->a, &total);

    if(!total)
      goto end;

    else if((num == total) && !have_children(t, arg1))
      {
      splice_children_tree_internal(t, id, 0, 0, gavl_dictionary_get(a->a, GAVL_META_TRACKS));
      goto end;
      }
    }
  
  bg_gtk_mdb_browse_children(t, id);

  end:
  
  free(id);
  }

static const gavl_dictionary_t * get_selected_album(bg_gtk_mdb_tree_t * t,
                                                    const char * id)
  {
  album_t * a;
  const gavl_dictionary_t * ret = NULL;
  char * parent_id = bg_mdb_get_parent_id(id);
  
  if((a = album_array_get_by_id(&t->exp_albums, parent_id)) && a->a)
    ret = gavl_get_track_by_id(a->a, id);
  
  free(parent_id);
  return ret;
  }

static const gavl_dictionary_t * get_parent_album(bg_gtk_mdb_tree_t * t,
                                                  const char * id)
  {
  album_t * a;
  const gavl_dictionary_t * ret = NULL;
  char * parent_id = bg_mdb_get_parent_id(id);
  
  if((a = album_array_get_by_id(&t->exp_albums, parent_id)))
    ret = a->a;
  
  free(parent_id);
  return ret;
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

  model = gtk_tree_view_get_model(GTK_TREE_VIEW(t->treeview));
  
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
    memset(&t->menu_ctx, 0, sizeof(t->menu_ctx));

    t->menu_ctx.num_selected =
      gtk_tree_selection_count_selected_rows(gtk_tree_view_get_selection(GTK_TREE_VIEW(w)));
    
    /* Set the menu album */
    if(id && !(t->menu_ctx.album = get_selected_album(t, id)))
      goto end;

    t->menu_ctx.item = t->menu_ctx.album;
    t->menu_ctx.parent = get_parent_album(t, id);
    
    t->menu_ctx.widget = w;
    t->menu_ctx.tree = 1;
    
    bg_gtk_mdb_popup_menu(t, (const GdkEvent *)evt);
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

static gboolean idle_func(gpointer user_data)
  {
  bg_gtk_mdb_tree_t * t = user_data;
  //  fprintf(stderr, "idle_func (tree)\n");

  bg_msg_sink_iteration(t->ctrl.evt_sink);
  bg_msg_sink_iteration(t->player_ctrl.evt_sink);

  load_icons(t);
  return TRUE;
  }

static void container_remove_func(GtkContainer *container,
                                  GtkWidget    *widget,
                                  gpointer      user_data)
  {
  int i;
  bg_gtk_mdb_tree_t * tree = user_data;
  //  fprintf(stderr, "container_remove_func\n");

  for(i = 0; i < tree->win_albums.num_albums; i++)
    {
    if(tree->win_albums.albums[i]->list->widget == widget)
      {
      album_t * a;
      //      fprintf(stderr, "win -> tab %d\n", i);

      a = album_array_move_2(tree, &tree->tab_albums, &tree->win_albums, -1, i);
      gtk_widget_destroy(a->list->window);
      a->list->window = NULL;
      }
    }

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
  int i;

  bg_gtk_mdb_tree_t * tree = data;

  fprintf(stderr, "window_delete_callback\n");
  
  for(i = 0; i < tree->win_albums.num_albums; i++)
    {
    if(tree->win_albums.albums[i]->list->window == widget)
      {
      /*
       *  Prevent window from being destroyed by close_window_album() since the
       *  default gtk handlers do that
       */
      tree->win_albums.albums[i]->list->window = NULL;
      bg_gtk_mdb_tree_close_window_album(tree, i);
      break;
      }
    }
  return FALSE;
  }

static GtkNotebook * create_window_callback(GtkNotebook *notebook,
                                            GtkWidget   *page,
                                            gint         x,
                                            gint         y,
                                            gpointer     user_data)
  {
  GtkWidget * ret;
  int i;
  int src_idx = -1;
  list_t * lst = NULL;
  album_t * a = NULL;
  bg_gtk_mdb_tree_t * tree = user_data;

  const gavl_dictionary_t * m;
  const char * str;
  
  for(i = 0; i < tree->tab_albums.num_albums; i++)
    {
    if(tree->tab_albums.albums[i]->list->widget == page)
      {
      src_idx = i;
      break;
      }
    }

  if(src_idx < 0)
    return NULL;

  //  fprintf(stderr, "Tab -> window: %d\n", src_idx);

  a = album_array_move_2(tree, &tree->win_albums, &tree->tab_albums, -1, src_idx);
  lst = a->list;
  
  ret = gtk_notebook_new();
  gtk_widget_show(ret);
  gtk_notebook_set_group_name(GTK_NOTEBOOK(ret), NOTEBOOK_GROUP);

  //  gtk_notebook_set_scrollable(GTK_NOTEBOOK(ret), FALSE);
  
  lst->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

  if((a->a) &&
     (m = gavl_track_get_metadata(a->a)) &&
     (str = gavl_dictionary_get_string(m, GAVL_META_LABEL)))
    gtk_window_set_title(GTK_WINDOW(lst->window), str);
  
  gtk_window_set_default_size(GTK_WINDOW(lst->window), 600, 400);
  gtk_window_set_position(GTK_WINDOW(lst->window), GTK_WIN_POS_MOUSE);
  
  
  g_signal_connect(G_OBJECT(ret), "remove", G_CALLBACK(container_remove_func), user_data);
  g_signal_connect(G_OBJECT(lst->window), "delete_event", G_CALLBACK(window_delete_callback), user_data);

  gtk_container_add(GTK_CONTAINER(lst->window), ret);
  
  gtk_widget_show(lst->window);

  /* Unset the drag dest of a notebook */
  g_idle_add(unset_drag_dest_callback, ret);
  
  return GTK_NOTEBOOK(ret);
  }


static void
page_reordered_callback(GtkNotebook *notebook,
                        GtkWidget   *child,
                        guint        page_num,
                        gpointer     user_data)
  {
  int src_idx = -1;
  int i;
  
  bg_gtk_mdb_tree_t * t = user_data;

  for(i = 0; i < t->tab_albums.num_albums; i++)
    {
    if(t->tab_albums.albums[i]->list->widget == child)
      {
      src_idx = i;
      break;
      }
    }
  
  fprintf(stderr, "page_reordered_callback %d -> %d\n", src_idx, page_num);

  if(src_idx >= 0)
    album_array_move_1(t, &t->tab_albums, src_idx, page_num);
  }

static int handle_player_message(void * priv, gavl_msg_t * msg)
  {
  bg_gtk_mdb_tree_t * t = priv;
  //  const char * ctx_id;
  
  switch(msg->NS)
    {
    case BG_MSG_NS_DB:
      switch(msg->ID)
        {
        case BG_MSG_DB_SPLICE_CHILDREN:
        case BG_RESP_DB_BROWSE_CHILDREN:
          {
          gavl_value_t add;
          
          int last = 0;
          int idx = 0;
          int del = 0;
          //          GtkTreeIter iter;
                    
          gavl_value_init(&add);
          
          //          ctx_id = gavl_dictionary_get_string(&msg->header, GAVL_MSG_CONTEXT_ID);
          
          if(!gavl_msg_get_splice_children(msg, &last, &idx, &del, &add))
            return 1;
          
          /* Update children of local playqueue and list widget */

          if(t->playqueue.list)
            bg_gtk_mdb_list_splice_children(t->playqueue.list, idx, del, &add, 1);
          else
            gavl_track_splice_children(t->playqueue.a, idx, del, &add);

#if 0          
          /* Update update local playqueue (num_children) */

          if(id_to_iter(GTK_TREE_VIEW(t->treeview), BG_PLAYQUEUE_ID, &iter))
            set_entry_tree(t, t->playqueue.a, &iter);
          
          /* Update update tab label */

          if(t->playqueue.list)
            {
            char * markup;
            markup = bg_gtk_mdb_tree_create_markup(t->playqueue.a, NULL);
            gtk_label_set_markup(GTK_LABEL(t->playqueue.list->tab_label), markup);
            free(markup);
            }
#endif
          }
          break;
        case BG_MSG_DB_OBJECT_CHANGED:
        case BG_RESP_DB_BROWSE_OBJECT:
          {
          gavl_dictionary_t new_dict;
          const char * ctx_id;
          
          ctx_id = gavl_dictionary_get_string(&msg->header, GAVL_MSG_CONTEXT_ID);
          gavl_dictionary_init(&new_dict);
          gavl_msg_get_arg_dictionary(msg, 0, &new_dict);

          if(!ctx_id) // Should not be necessary
            break;

          bg_gtk_mdb_tree_update_node(t, ctx_id, &new_dict);

          set_object_array(&t->tab_albums, ctx_id, &new_dict);
          set_object_array(&t->win_albums, ctx_id, &new_dict);
          set_object_array(&t->exp_albums, ctx_id, &new_dict);
          
          gavl_dictionary_free(&new_dict);
          }
          break;
        }
      
      break;
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

          bg_msg_get_state(msg,
                           &last,
                           &ctx,
                           &var,
                           &val, NULL);

          if(!strcmp(ctx, BG_PLAYER_STATE_CTX))
            {
            if(!strcmp(var, BG_PLAYER_STATE_QUEUE_IDX))
              {
              int num, i, current;

              // fprintf(stderr, "queue index changed: %d\n", val.v.i);
              num = gavl_track_get_num_children(t->playqueue.a);

              for(i = 0; i < num; i++)
                {
                gavl_dictionary_t * track = gavl_get_track_nc(t->playqueue.a, i);

                current = gavl_track_get_gui_state(track, GAVL_META_GUI_CURRENT);

                if(current)
                  {
                  if(i == val.v.i)
                    {
                    // Nothing
                    }
                  else
                    {
                    /* TODO: Update */
                    if(t->playqueue.list)
                      {
                      gavl_dictionary_t new_dict;

                      gavl_dictionary_init(&new_dict);
                      gavl_track_set_gui_state(&new_dict, GAVL_META_GUI_CURRENT, 0);
                      
                      bg_gtk_mdb_album_update_track(&t->playqueue, gavl_track_get_id(track),
                                                    &new_dict);
                      gavl_dictionary_free(&new_dict);
                      }
                    else
                      {
                      gavl_track_set_gui_state(track, GAVL_META_GUI_CURRENT, 0);
                      }
                    }
                  }
                else
                  {
                  if(i == val.v.i)
                    {
                    /* TODO: Update */
                    if(t->playqueue.list)
                      {
                      gavl_dictionary_t new_dict;

                      gavl_dictionary_init(&new_dict);
                      gavl_track_set_gui_state(&new_dict, GAVL_META_GUI_CURRENT, 1);
                      
                      bg_gtk_mdb_album_update_track(&t->playqueue, gavl_track_get_id(track),
                                                    &new_dict);
                      
                      gavl_dictionary_free(&new_dict);
                      }
                    else
                      {
                      gavl_track_set_gui_state(track, GAVL_META_GUI_CURRENT, 1);
                      }
                    }
                  else
                    {
                    // Nothing
                    }
                  }
                }
              
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

bg_gtk_mdb_tree_t * bg_gtk_mdb_tree_create(bg_controllable_t * mdb_ctrl)
  {
  GtkTreeStore      *store;
  bg_gtk_mdb_tree_t * ret;
  
  GtkCellRenderer   *text_renderer;
  GtkCellRenderer   *icon_renderer;
  GtkCellRenderer   *pixmap_renderer;
  uuid_t u;
  
  GtkTreeViewColumn *column;
  GtkWidget * scrolledwindow;

  gavl_dictionary_t * m;
  
  ret = calloc(1, sizeof(*ret));

  bg_control_init(&ret->player_ctrl, bg_msg_sink_create(handle_player_message, ret, 0));
  
  bg_control_init(&ret->ctrl, bg_msg_sink_create(handle_msg, ret, 0));

  ret->mdb_ctrl_p = mdb_ctrl;
  bg_controllable_connect(ret->mdb_ctrl_p, &ret->ctrl);
  
  store = gtk_tree_store_new(NUM_TREE_COLUMNS,
                             G_TYPE_STRING,   // icon
                             G_TYPE_BOOLEAN,  // has_icon
                             GDK_TYPE_PIXBUF, // pixbuf
                             G_TYPE_BOOLEAN,  // has_pixbuf
                             G_TYPE_STRING,   // label
                             G_TYPE_STRING,   // color
                             G_TYPE_INT,      // expanded
                             G_TYPE_STRING,   // id
                             G_TYPE_STRING,   // search_string
                             G_TYPE_STRING    // current
                             );
  
  ret->treeview = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
  
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
  
  column = gtk_tree_view_column_new();

  text_renderer = gtk_cell_renderer_text_new();
  icon_renderer = gtk_cell_renderer_text_new();

  g_object_set(G_OBJECT(icon_renderer), "family", BG_ICON_FONT_FAMILY, NULL);
  g_object_set(G_OBJECT(icon_renderer), "scale", 2.0, NULL);
  g_object_set(G_OBJECT(icon_renderer), "xalign", 0.5, NULL);

  
  gtk_tree_view_column_pack_start(column, icon_renderer, FALSE);
  gtk_tree_view_column_add_attribute(column,
                                     icon_renderer,
                                     "foreground", TREE_COLUMN_COLOR);
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
  
#if 0  
  gtk_tree_view_column_add_attribute(column,
                                     text_renderer,
                                     "weight", TREE_COLUMN_WEIGHT);
#endif
  
  gtk_tree_view_column_add_attribute(column,
                                     text_renderer,
                                     "foreground", TREE_COLUMN_COLOR);

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

  g_signal_connect(G_OBJECT(ret->notebook), "page-reordered",
                   G_CALLBACK(page_reordered_callback), (gpointer)ret);

  
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
  
  /* Browse children of '/' */
  row_set_expanded(ret, "/", 1);

  /* Initialize playqueue */
  ret->playqueue.a = gavl_dictionary_create();
  ret->playqueue.local = 1;
  ret->playqueue.id = gavl_strdup(BG_PLAYQUEUE_ID);

#if 1
  m = gavl_dictionary_get_dictionary_create(ret->playqueue.a, GAVL_META_METADATA);
  gavl_dictionary_set_string(m, GAVL_META_LABEL, "Player queue");
  gavl_dictionary_set_string(m, GAVL_META_MEDIA_CLASS, GAVL_META_MEDIA_CLASS_ROOT_PLAYQUEUE);
  gavl_dictionary_set_string(m, GAVL_META_ID, BG_PLAYQUEUE_ID);
#endif
  
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
  
  album_array_free(&t->tab_albums); 
  album_array_free(&t->exp_albums);
  album_array_free(&t->win_albums);
  
  bg_control_cleanup(&t->ctrl);
  bg_control_cleanup(&t->player_ctrl);

  gavl_array_free(&t->browse_object_requests);
  gavl_array_free(&t->browse_children_requests);

  gavl_array_free(&t->icons_to_load);
  gavl_array_free(&t->list_icons_to_load);
  
  if(t->playback_id)
    free(t->playback_id);

  if(t->playqueue.a)
    gavl_dictionary_destroy(t->playqueue.a);
  
  if(t->playqueue.id)
    free(t->playqueue.id);

  
  free(t);
  }

void bg_gtk_mdb_tree_unset_player_ctrl(bg_gtk_mdb_tree_t * t)
  {
  if(t->player_ctrl_p)
    {
    GtkTreeIter iter;
    
    bg_controllable_disconnect(t->player_ctrl_p, &t->player_ctrl);
    t->player_ctrl_p = NULL;

    /* Remove player item */
    
    if(id_to_iter(GTK_TREE_VIEW(t->treeview), BG_PLAYQUEUE_ID, &iter))
      {
      if(t->playqueue.list)
        bg_gtk_mdb_list_splice_children(t->playqueue.list, 0, -1, NULL, 1);
      else
        gavl_track_splice_children(t->playqueue.a, 0, -1, NULL);
      
      set_entry_tree(t, t->playqueue.a, &iter);
      
      }

    
    }
  }

void bg_gtk_mdb_tree_set_player_ctrl(bg_gtk_mdb_tree_t * t, bg_controllable_t * player_ctrl)
  {
  gavl_msg_t * msg;
  GtkTreeIter iter;
  GtkTreeModel * model = gtk_tree_view_get_model(GTK_TREE_VIEW(t->treeview));
  
  t->player_ctrl_p = player_ctrl;
  bg_controllable_connect(t->player_ctrl_p, &t->player_ctrl);

  if(!id_to_iter(GTK_TREE_VIEW(t->treeview), BG_PLAYQUEUE_ID, &iter))
    {
    gtk_tree_store_insert(GTK_TREE_STORE(model), &iter, NULL, 0);
    set_entry_tree(t, t->playqueue.a, &iter);
    t->local_folders++;
    }

  /* Browse object */
  msg = bg_msg_sink_get(t->player_ctrl.cmd_sink);
  
  gavl_msg_set_id_ns(msg, BG_FUNC_DB_BROWSE_OBJECT, BG_MSG_NS_DB);
  gavl_dictionary_set_string(&msg->header, GAVL_MSG_CONTEXT_ID, BG_PLAYQUEUE_ID);

  bg_msg_sink_put(t->player_ctrl.cmd_sink, msg);

  
  /* Browse children */
  msg = bg_msg_sink_get(t->player_ctrl.cmd_sink);

  bg_mdb_set_browse_children_request(msg, BG_PLAYQUEUE_ID, 0, -1, 0);
  
  bg_msg_sink_put(t->player_ctrl.cmd_sink, msg);

  }

void bg_gtk_mdb_tree_delete_selected_album(bg_gtk_mdb_tree_t * t)
  {
  GtkTreeSelection * selection;
  GtkTreeModel * model;
  GtkTreeIter iter;
  char * id = NULL;
  album_t * a;
  char * parent_id = NULL;
  int idx;

  selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(t->treeview));

  if(gtk_tree_selection_get_selected(selection, &model, &iter) &&
     (id = iter_to_id_tree(GTK_TREE_VIEW(t->treeview), &iter)) &&
     (parent_id = bg_mdb_get_parent_id(id)) &&
     (a = album_array_get_by_id(&t->exp_albums, parent_id)) &&
     ((idx = gavl_get_track_idx_by_id(a->a, id)) >= 0))
    {
    gavl_msg_t * msg = bg_msg_sink_get(t->ctrl.cmd_sink);
    
    gavl_msg_set_id_ns(msg, BG_CMD_DB_SPLICE_CHILDREN, BG_MSG_NS_DB);
    gavl_dictionary_set_string(&msg->header, GAVL_MSG_CONTEXT_ID, parent_id);

    gavl_msg_set_id_ns(msg, BG_CMD_DB_SPLICE_CHILDREN, BG_MSG_NS_DB);
    
    gavl_dictionary_set_string(&msg->header, GAVL_MSG_CONTEXT_ID, parent_id);
    
    gavl_msg_set_arg_int(msg, 0, idx);
    gavl_msg_set_arg_int(msg, 1, 1);
    bg_msg_sink_put(t->ctrl.cmd_sink, msg);
    }
  else
    {
    //    fprintf(stderr, "Don't delete %s %s\n", id, parent_id);
    
    }
  
  if(parent_id)
    free(parent_id);
  
  return;
  }

