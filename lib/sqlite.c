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

#include <config.h>
#include <bgsqlite.h>

#include <string.h>
#include <ctype.h>

#include <gmerlin/utils.h>
#include <gmerlin/log.h>
#include <gmerlin/mdb.h>

#define LOG_DOMAIN "sqlite"

void bg_sqlite_start_transaction(sqlite3 * db)
  {
  bg_sqlite_exec(db, "BEGIN TRANSACTION;", NULL, NULL);
  }

void bg_sqlite_end_transaction(sqlite3 * db)
  {
  bg_sqlite_exec(db, "END;", NULL, NULL);
  }

int
bg_sqlite_exec(sqlite3 * db,                              /* An open database */
               const char *sql,                           /* SQL to be evaluated */
               int (*callback)(void*,int,char**,char**),  /* Callback function */
               void * data)                               /* 1st argument to callback */
  {
  char * err_msg = NULL;
  int err;

  err = sqlite3_exec(db, sql, callback, data, &err_msg);

  //  fprintf(stderr, "SQL: %s\n", sql);

  if(err)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "SQL query: \"%s\" failed: %s",
           sql, err_msg);
    sqlite3_free(err_msg);
    return 0;
    }
  return 1;
  }

int bg_sqlite_int_callback(void * data, int argc, char **argv, char **azColName)
  {
  int64_t * ret = data;
  if(argv[0])
    *ret = strtoll(argv[0], NULL, 10);
  return 0;
  }

int bg_sqlite_string_callback(void * data, int argc,
                              char **argv, char **azColName)
  {
  char ** ret = data;
  if((argv[0]) && (*(argv[0]) != '\0'))
    *ret = gavl_strdup(argv[0]);
  return 0;
  }

int bg_sqlite_string_array_callback(void * data, int argc,
                                    char **argv, char **azColName)
  {
  gavl_array_t * arr = data;
  if((argv[0]) && (*(argv[0]) != '\0'))
    gavl_string_array_add(arr, argv[0]);
  return 0;
  }

int64_t bg_sqlite_get_int(sqlite3 * db, const char * sql)
  {
  int64_t ret = -1;
  if(!bg_sqlite_exec(db, sql, bg_sqlite_int_callback, &ret))
    return -1;
  return ret;
  }

int64_t bg_sqlite_get_max_int(sqlite3 * db, const char * table,
                           const char * row)
  {
  int result;
  char * sql;
  int64_t ret = 0;

  sql = sqlite3_mprintf("select max(%s) from %s;", row, table);
  result = bg_sqlite_exec(db, sql, bg_sqlite_int_callback, &ret);
  sqlite3_free(sql);
  if(!result)
    return -1;

  if(ret < 0)
    return -1;
  return ret;
  }

int64_t bg_sqlite_string_to_id(sqlite3 * db,
                               const char * table,
                               const char * id_row,
                               const char * string_row,
                               const char * str)
  {
  char * buf;
  int64_t ret = -1;
  int result;
  buf = sqlite3_mprintf("select %s from %s where %s = %Q;",
                        id_row, table, string_row, str);
  result = bg_sqlite_exec(db, buf, bg_sqlite_int_callback, &ret);
  sqlite3_free(buf);
  return result ? ret : -1 ;
  }

int64_t bg_sqlite_string_to_id_add(sqlite3 * db,
                                   const char * table,
                                   const char * id_row,
                                   const char * string_row,
                                   const char * str)
  {
  char * sql;
  int result;

  int64_t ret;
  ret = bg_sqlite_string_to_id(db, table, id_row, string_row, str);
  if(ret >= 0)
    return ret;
  ret = bg_sqlite_get_max_int(db, table, id_row);
  if(ret < 0)
    return ret;

  ret++;
  
  /* Insert into table */
  sql = sqlite3_mprintf("INSERT INTO %s "
                          "( %s, %s ) VALUES "
                          "( %"PRId64", %Q );",
                          table, id_row, string_row, ret, str);

  result = bg_sqlite_exec(db, sql, NULL, NULL);
  sqlite3_free(sql);
  if(!result)
    return -1;
  return ret;
  }

char * bg_sqlite_id_to_string(sqlite3 * db,
                              const char * table,
                              const char * string_row,
                              const char * id_row,
                              int64_t id)
  {
  char * buf;
  char * ret = NULL;
  int result;
  buf = sqlite3_mprintf("select %s from %s where %s = %"PRId64";",
                        string_row, table, id_row, id);
  result = bg_sqlite_exec(db, buf, bg_sqlite_string_callback, &ret);
  sqlite3_free(buf);
  return result ? ret : NULL;
  }

int bg_sqlite_get_string_array(sqlite3 * db,
                               const char * table,
                               const char * string_row,
                               gavl_array_t * ret)
  {
  char * buf;
  int result;
  buf = sqlite3_mprintf("select %s from %s;", string_row, table);
  result = bg_sqlite_exec(db, buf, bg_sqlite_string_array_callback, ret);
  sqlite3_free(buf);
  return result;
  }
                                 

int64_t bg_sqlite_id_to_id(sqlite3 * db,
                           const char * table,
                           const char * dst_row,
                           const char * src_row,
                           int64_t id)
  {
  char * buf;
  int64_t ret = -1;
  int result;
  buf = sqlite3_mprintf("select %s from %s where %s = %"PRId64";",
                        dst_row, table, src_row, id);
  result = bg_sqlite_exec(db, buf, bg_sqlite_int_callback, &ret);
  return result ? ret : -1;
  }

int64_t bg_sqlite_get_next_id(sqlite3 * db, const char * table)
  {
  int64_t ret = bg_sqlite_get_max_int(db, table, "ID");
  if(ret < 0)
    return -1;
  return ret + 1;
  }

#if 0
typedef struct
  {
  int64_t * val;
  int val_alloc;
  int num_val;
  } bg_sqlite_id_tab_t;
#endif

void bg_sqlite_id_tab_init(bg_sqlite_id_tab_t * tab)
  {
  memset(tab, 0, sizeof(*tab));
  }

void bg_sqlite_id_tab_free(bg_sqlite_id_tab_t * tab)
  {
  if(tab->val)
    free(tab->val);
  }

void bg_sqlite_id_tab_reset(bg_sqlite_id_tab_t * tab)
  {
  tab->num_val = 0;
  }

void bg_sqlite_id_tab_push(bg_sqlite_id_tab_t * val, int64_t id)
  {
  if(val->num_val + 1 > val->val_alloc)
    {
    val->val_alloc = val->num_val + 1024;
    val->val = realloc(val->val, val->val_alloc * sizeof(*val->val));
    }
  val->val[val->num_val] = id;
  val->num_val++;
  }

int bg_sqlite_append_id_callback(void * data, int argc, char **argv, char **azColName)
  {
  int64_t ret;
  bg_sqlite_id_tab_t * val = data;
  if(argv[0])
    {
    ret = strtoll(argv[0], NULL, 10);
    bg_sqlite_id_tab_push(val, ret);
    }
  return 0;
  }

void bg_sqlite_delete_by_id(sqlite3 * db,
                            const char * table,
                            int64_t id)
  {
  char * sql;
  sql = sqlite3_mprintf("DELETE FROM %s WHERE ID = %"PRId64";", table, id);
  bg_sqlite_exec(db, sql, NULL, NULL);
  sqlite3_free(sql);
  }

int bg_sqlite_select_join(sqlite3 * db, bg_sqlite_id_tab_t * tab,
                          const char * table_1,
                          const char * col_1,
                          int64_t val_1,
                          const char * table_2,
                          const char * col_2,
                          int64_t val_2)
  {
  char * sql;
  int result;
  sql = sqlite3_mprintf("SELECT a.ID from %s a INNER JOIN %s b on (a.ID = b.ID) & (a.%s = %"PRId64") & (b.%s = %"PRId64");", table_1, table_2, col_1, val_1, col_2, val_2);

  result = bg_sqlite_exec(db, sql, bg_sqlite_append_id_callback, tab);
  sqlite3_free(sql);
  return result;
  }

char * bg_sqlite_get_col_str(sqlite3_stmt * st, int col)
  {
  return gavl_strdup((const char*)sqlite3_column_text(st, col));
  }

static int compare_func(void* udp, int sizeA, const void* textA, int sizeB, const void* textB)
  {
  int result;

  char * sA = gavl_strndup(textA, textA + sizeA);
  char * sB = gavl_strndup(textB, textB + sizeB);
  result = strcoll(sA, sB);
  free(sA);
  free(sB);
  
  return result;
  }

void bg_sqlite_init_strcoll(sqlite3 * db)
  {
  sqlite3_create_collation(db, "strcoll", SQLITE_UTF8, NULL, compare_func);
  }

char * bg_sqlite_make_group_condition(const char * id)
  {
  id += BG_MDB_GROUP_PREFIX_LEN;

  if(!strcmp(id, "0-9"))
    {
    return gavl_strdup(" GLOB '[0-9]*'");
    }
  else if(!strcmp(id, "others"))
    {
    return gavl_strdup(" NOT GLOB '[0-9a-zA-Z]*'");
    }
  else if((*id >= 'a') && (*id <= 'z'))
    {
    //    return bg_sprintf(" LIKE '%c%%'", *id);
    return bg_sprintf(" GLOB '[%c%c]*'", *id, toupper(*id));
    }
  return NULL;
  }

int bg_sqlite_count_groups(sqlite3 * db, const char * tmpl)
  {
  int ret = 0;
  int i;
  char * sql;
  char * cond;
  
  for(i = 0; i < bg_mdb_num_groups; i++)
    {
    cond = bg_sqlite_make_group_condition(bg_mdb_groups[i].id);
    sql = gavl_sprintf(tmpl, cond);

    //    fprintf(stderr, "Count groups: %s\n", sql);
    
    if(bg_sqlite_get_int(db, sql) > 0)
      {
      //      fprintf(stderr, "Found\n", sql);
      ret++;
      }
#if 0
    else
      {
      fprintf(stderr, "Not found\n", sql);
      }
#endif
    free(cond);
    free(sql);
    }
  return ret;
  }

int bg_sqlite_add_groups(sqlite3 * db, gavl_array_t * ret,
                         const char * parent_id,
                         const char * tmpl,
                         const gavl_dictionary_t * m_tmpl,
                         int start, int num)
  {
  int i;
  char * sql;
  char * cond;
  int num_children;

  const char * last_id = NULL;
  gavl_dictionary_t * last_m = NULL;
  
  for(i = 0; i < bg_mdb_num_groups; i++)

    {
    cond = bg_sqlite_make_group_condition(bg_mdb_groups[i].id);
    sql = gavl_sprintf(tmpl, cond);

    num_children = bg_sqlite_get_int(db, sql);

    free(cond);
    free(sql);

    if(num_children > 0)
      {
      if(start > 0)
        {
        start--;
        last_id = bg_mdb_groups[i].id;
        }
      else if((num < 1) || (ret->num_entries < num))
        {
        const char * var;
        gavl_value_t val;
        gavl_dictionary_t * dict;
        gavl_dictionary_t * m;
        
        gavl_value_init(&val);
        dict = gavl_value_set_dictionary(&val);
        
        m = gavl_dictionary_get_dictionary_create(dict, GAVL_META_METADATA);
        gavl_dictionary_copy(m, m_tmpl);
        gavl_dictionary_set_string(m, GAVL_META_CLASS, GAVL_META_CLASS_CONTAINER);

        if(last_id)
          {
          gavl_dictionary_set_string_nocopy(m, GAVL_META_PREVIOUS_ID, gavl_sprintf("%s/%s", parent_id, last_id));
          last_id = NULL;
          }
        if(last_m)
          {
          gavl_dictionary_set_string_nocopy(last_m, GAVL_META_NEXT_ID, gavl_sprintf("%s/%s", parent_id, bg_mdb_groups[i].id));
          last_m = NULL;
          }
        
        var = gavl_dictionary_get_string(m_tmpl, GAVL_META_CHILD_CLASS);
        if(var && gavl_string_starts_with(var, "container"))
          gavl_track_set_num_children(dict, num_children, 0);
        else
          gavl_track_set_num_children(dict, 0, num_children);

        gavl_track_set_id_nocopy(dict, gavl_sprintf("%s/%s", parent_id, bg_mdb_groups[i].id));
        gavl_dictionary_set_string(m, GAVL_META_LABEL, bg_mdb_groups[i].label);
        
        gavl_array_splice_val_nocopy(ret, -1, 0, &val);

        //        if(ret->num_entries == num)
        //          break;

        last_id = bg_mdb_groups[i].id;
        last_m = m;
        }
      else
        {
        if(last_m)
          gavl_dictionary_set_string_nocopy(last_m, GAVL_META_NEXT_ID, gavl_sprintf("%s/%s", parent_id, bg_mdb_groups[i].id));
        break;
        }
      }
    
    }

  /* Set next/previous */
  
  
  return ret->num_entries;
  
  }

int bg_sqlite_set_group_container(sqlite3 * db, gavl_dictionary_t * ret,
                                  const char * id, const char * template,
                                  const char * child_class, int * idxp, int * totalp)
  {
  int count;
  int i, idx = 0;
  int num_children = 0;
  char * sql;
  char * cond;
  gavl_dictionary_t * m;
  gavl_array_t siblings;
  int result = 0;
  const char * group_id = strrchr(id, '/');
  
  if(!group_id)
    goto fail;

  group_id++;

  m = gavl_dictionary_get_dictionary_create(ret, GAVL_META_METADATA);
  
  gavl_dictionary_set_string(m, GAVL_META_CLASS, GAVL_META_CLASS_CONTAINER);
  gavl_dictionary_set_string(m, GAVL_META_LABEL, bg_mdb_get_group_label(group_id));
  gavl_dictionary_set_string(m, GAVL_META_CHILD_CLASS, child_class);
  
  /* Get number of children */
  
  gavl_array_init(&siblings);
  
  for(i = 0; i < bg_mdb_num_groups; i++)
    {
    cond = bg_sqlite_make_group_condition(bg_mdb_groups[i].id);
    sql = gavl_sprintf(template, cond);
    
    count = bg_sqlite_get_int(db, sql);
    free(sql);
    free(cond);

    if(!count)
      continue;

    if(!strcmp(group_id, bg_mdb_groups[i].id))
      {
      idx = siblings.num_entries;
      num_children = count;
      }
    gavl_string_array_add(&siblings, bg_mdb_groups[i].id);
    }

  if((idx < 0) || (num_children < 1))
    goto fail;

  if((idx > 0) || (idx < siblings.num_entries - 1))
    {
    char * parent_id;

    parent_id = bg_mdb_get_parent_id(id);

    if(idx > 0)
      gavl_dictionary_set_string_nocopy(m, GAVL_META_PREVIOUS_ID,
                                        gavl_sprintf("%s/%s", parent_id, gavl_string_array_get(&siblings, idx-1)));
    if(idx < siblings.num_entries - 1)
      gavl_dictionary_set_string_nocopy(m, GAVL_META_NEXT_ID,
                                        gavl_sprintf("%s/%s", parent_id, gavl_string_array_get(&siblings, idx+1)));
    free(parent_id);
    }

  if(gavl_string_starts_with(child_class, GAVL_META_CLASS_CONTAINER))
    gavl_track_set_num_children(ret, num_children, 0);
  else
    gavl_track_set_num_children(ret, 0, num_children);

  
    
  if(idxp)
    *idxp = idx;
  if(totalp)
    *totalp = siblings.num_entries;
  
  result = 1;

  fail:

  gavl_array_free(&siblings);
  
  return result;
  
  }
