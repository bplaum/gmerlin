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



#ifndef BGSQLITE_H_INCLUDED
#define BGSQLITE_H_INCLUDED


#include <gavl/gavl.h>
#include <gavl/value.h>

#include <inttypes.h>
#include <sqlite3.h>



int
bg_sqlite_exec(sqlite3 * db,                              /* An open database */
               const char *sql,                           /* SQL to be evaluated */
               int (*callback)(void*,int,char**,char**),  /* Callback function */
               void * data);                              /* 1st argument to callback */


int64_t bg_sqlite_string_to_id(sqlite3 * db,
                               const char * table,
                               const char * id_row,
                               const char * string_row,
                               const char * str);

int64_t bg_sqlite_string_to_id_add(sqlite3 * db,
                                   const char * table,
                                   const char * id_row,
                                   const char * string_row,
                                   const char * str);


char * bg_sqlite_id_to_string(sqlite3 * db,
                              const char * table,
                              const char * string_row,
                              const char * id_row,
                              int64_t id);

int64_t bg_sqlite_id_to_id(sqlite3 * db,
                           const char * table,
                           const char * dst_row,
                           const char * src_row,
                           int64_t id);

void bg_sqlite_delete_by_id(sqlite3 * db,
                            const char * table,
                            int64_t id);

int bg_sqlite_get_string_array(sqlite3 * db,
                               const char * table,
                               const char * string_row,
                               gavl_array_t * ret);

int64_t bg_sqlite_get_max_int(sqlite3 * db, const char * table,
                              const char * row);

int64_t bg_sqlite_get_next_id(sqlite3 * db, const char * table);

/* Get an array of int's */

typedef struct
  {
  int64_t * val;
  int val_alloc;
  int num_val;
  } bg_sqlite_id_tab_t;

void bg_sqlite_id_tab_init(bg_sqlite_id_tab_t * tab);
void bg_sqlite_id_tab_free(bg_sqlite_id_tab_t * tab);
void bg_sqlite_id_tab_reset(bg_sqlite_id_tab_t * tab);
void bg_sqlite_id_tab_push(bg_sqlite_id_tab_t * val, int64_t id);

int bg_sqlite_append_id_callback(void * data, int argc, char **argv, char **azColName);

int bg_sqlite_int_callback(void * data, int argc, char **argv, char **azColName);
int bg_sqlite_string_callback(void * data, int argc, char **argv, char **azColName);

int bg_sqlite_string_array_callback(void * data, int argc,
                                    char **argv, char **azColName);


int bg_sqlite_select_join(sqlite3 * db, bg_sqlite_id_tab_t * tab,
                          const char * table_1,
                          const char * col_1,
                          int64_t val_1,
                          const char * table_2,
                          const char * col_2,
                          int64_t val_2);

char * bg_sqlite_get_col_str(sqlite3_stmt * st, int col);

void bg_sqlite_start_transaction(sqlite3 * db);
void bg_sqlite_end_transaction(sqlite3 * db);

int64_t bg_sqlite_get_int(sqlite3 * db, const char * sql);



void bg_sqlite_init_strcoll(sqlite3 * db);

char * bg_sqlite_make_group_condition(const char * id);

/* tmpl must be an sqlite statement, which contains exactly one %s for inserting the
   group condition and returns 1 if group exists or not */
int bg_sqlite_count_groups(sqlite3 * db, const char * tmpl);

/* tmpl must be an sqlite statement, which contains exactly one %s for inserting the
   group condition and returns the number of group members */
int bg_sqlite_add_groups(sqlite3 * db, gavl_array_t * ret,
                         const char * parent_id,
                         const char * tmpl,
                         const gavl_dictionary_t * m_tmpl,
                         int start, int num);

int bg_sqlite_set_group_container(sqlite3 * db, gavl_dictionary_t * ret, const char * id, const char * template,
                                  const char * child_class, int * idxp, int * totalp);


#endif // BGSQLITE_H_INCLUDED
