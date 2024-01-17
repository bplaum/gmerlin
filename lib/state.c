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

#include <config.h>

#include <gavl/gavl.h>
#include <gavl/gavf.h>
#include <gavl/utils.h>

#include <gmerlin/utils.h>
#include <gmerlin/parameter.h>
#include <gmerlin/bgmsg.h>
#include <gmerlin/bggavl.h>
#include <gmerlin/state.h>

#include <gmerlin/translation.h>
#include <gmerlin/log.h>
#define LOG_DOMAIN "state"

#define DESC_MIN "min"
#define DESC_MAX "max"

#define DESC_DICT "$desc"

typedef struct
  {
  int id;
  bg_msg_sink_t * sink;
  const char * ctx;

  const gavl_dictionary_t * child;
  } apply_t;

static const gavl_dictionary_t * state_var_get_desc(const gavl_dictionary_t * dict, const char * ctx, const char * var)
  {
  if(!(dict = gavl_dictionary_get_dictionary(dict, DESC_DICT)) ||
     !(dict = gavl_dictionary_get_recursive(dict, ctx)) ||
     !(dict = gavl_dictionary_get_dictionary(dict, var)))
    return NULL;
  return dict;
  }

static gavl_dictionary_t * state_var_get_desc_create(gavl_dictionary_t * dict, const char * ctx, const char * var)
  {
  if(!(dict = gavl_dictionary_get_dictionary_create(dict, DESC_DICT)) ||
     !(dict = gavl_dictionary_get_recursive_create(dict, ctx)) ||
     !(dict = gavl_dictionary_get_dictionary_create(dict, var)))
    return NULL;
  return dict;
  }

static void apply_func_child(void * priv, const char * name, const gavl_value_t * val)
  {
  gavl_msg_t * msg;
  apply_t * a = priv;
  
  msg = bg_msg_sink_get(a->sink);
  gavl_msg_set_state(msg, a->id, gavl_dictionary_is_last(a->child, name), a->ctx, name, val);
  bg_msg_sink_put(a->sink);
  }

void bg_state_apply_ctx(gavl_dictionary_t * state, const char * ctx, bg_msg_sink_t * sink, int id)
  {
  apply_t a;
  memset(&a, 0, sizeof(a));

  a.id = id;
  a.sink = sink;
  a.ctx = ctx;
  a.child = gavl_dictionary_get_recursive_create(state, ctx);
  
  gavl_dictionary_foreach(a.child, apply_func_child, &a);
  }

static void apply_func(void * priv, const char * name, const gavl_value_t * val)
  {
  apply_t * a = priv;

  //  if(gavl_string_starts_with(name
  
  if(gavl_string_starts_with(name, DESC_DICT))
    {
    /* NOP */
    }
  else
    {
    a->ctx = name;
    a->child = val->v.dictionary;
    gavl_dictionary_foreach(a->child, apply_func_child, a);
    }
  
  }

void bg_state_apply(gavl_dictionary_t * state, bg_msg_sink_t * sink, int id)
  {
  apply_t a;
  a.id = id;
  a.sink = sink;
  gavl_dictionary_foreach(state, apply_func, &a);
  }


int bg_state_set(gavl_dictionary_t * state,
                  int last,
                  const char * ctx,
                  const char * var,
                  const gavl_value_t * val,
                  bg_msg_sink_t * sink, int id)
  {
  int changed = 0;
  gavl_dictionary_t * child;
  
  if(state)
    {
    child = gavl_dictionary_get_recursive_create(state, ctx);
    changed = gavl_dictionary_set(child, var, val);
    }
  else
    changed = 1;
  
  if(sink && (last || changed))
    {
    gavl_msg_t * msg = bg_msg_sink_get(sink);
    gavl_msg_set_state(msg, id, last, ctx, var, val);
    bg_msg_sink_put(sink);
    }
  return changed;
  }

const gavl_value_t * bg_state_get(const gavl_dictionary_t * state,
                                  const char * ctx,
                                  const char * var)
  {
  const gavl_dictionary_t * child;

  if(!(child = gavl_dictionary_get_recursive(state, ctx)))
    {
    //    fprintf(stderr, "No such ctx %s\n", ctx);
    return NULL;
    }

  return gavl_dictionary_get(child, var);
  }

void bg_state_set_range(gavl_dictionary_t * state,
                        const char * ctx, const char * var,
                        const gavl_value_t * min,
                        const gavl_value_t * max)
  {
  gavl_dictionary_t * desc;

  if(!min || (min->type == GAVL_TYPE_UNDEFINED) ||
     !max || (max->type == GAVL_TYPE_UNDEFINED))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Invalid range passed to bg_state_set_range");
    return;
    }

  desc = state_var_get_desc_create(state, ctx, var);

  gavl_dictionary_set(desc, DESC_MIN, min);
  gavl_dictionary_set(desc, DESC_MAX, max);
  }

int bg_state_get_range(gavl_dictionary_t * state,
                       const char * ctx, const char * var,
                       gavl_value_t * min,
                       gavl_value_t * max)
  {
  const gavl_dictionary_t * desc;
  const gavl_value_t * val;

  if(!(desc = state_var_get_desc(state, ctx, var)))
    return 0;
  
  if(!(val = gavl_dictionary_get(desc, DESC_MIN)))
    return 0;
  gavl_value_copy(min, val);

  if(!(val = gavl_dictionary_get(desc, DESC_MAX)))
    return 0;
  gavl_value_copy(max, val);
  
  return 1;
  }

void bg_state_set_range_int(gavl_dictionary_t * state,
                            const char * ctx, const char * var,
                            int min, int max)
  {
  gavl_value_t val_min;
  gavl_value_t val_max;
  gavl_value_init(&val_min);
  gavl_value_init(&val_max);
  gavl_value_set_int(&val_min, min);
  gavl_value_set_int(&val_max, max);
  
  bg_state_set_range(state, ctx, var, &val_min, &val_max);
  }

void bg_state_set_range_long(gavl_dictionary_t * state,
                             const char * ctx, const char * var,
                             int64_t min, int64_t max)
  {
  gavl_value_t val_min;
  gavl_value_t val_max;
  gavl_value_init(&val_min);
  gavl_value_init(&val_max);
  gavl_value_set_long(&val_min, min);
  gavl_value_set_long(&val_max, max);
  
  bg_state_set_range(state, ctx, var, &val_min, &val_max);
  
  }

void bg_state_set_range_float(gavl_dictionary_t * state,
                              const char * ctx, const char * var,
                              double min, double max)
  {
  gavl_value_t val_min;
  gavl_value_t val_max;
  gavl_value_init(&val_min);
  gavl_value_init(&val_max);
  gavl_value_set_float(&val_min, min);
  gavl_value_set_float(&val_max, max);
  
  bg_state_set_range(state, ctx, var, &val_min, &val_max);
  }

void bg_state_init_ctx(gavl_dictionary_t * state,
                       const char * ctx,
                       const bg_state_var_desc_t * desc)
  {
  int i = 0;

  while(desc[i].name)
    {
    if((desc[i].val_min.type != GAVL_TYPE_UNDEFINED) &&
       (desc[i].val_max.type != GAVL_TYPE_UNDEFINED))
      {
      bg_state_set_range(state, ctx, desc[i].name,
                         &desc[i].val_min, &desc[i].val_max);
      }
    
    //    fprintf(stderr, "State init ctx: %p %s %s\n", state, ctx, desc[i].name);

    if(desc[i].val_default.type != GAVL_TYPE_UNDEFINED)
      bg_state_set(state,
                   1,
                   ctx,
                   desc[i].name,
                   &desc[i].val_default,
                   NULL, 0);
    else
      {
      gavl_value_t val;
      gavl_value_init(&val);
      gavl_value_set_type(&val, desc[i].type);
      bg_state_set(state,
                   1,
                   ctx,
                   desc[i].name,
                   &val, NULL, 0);
      }
    i++;
    }
  }

static void check_max(gavl_value_t * val1, const gavl_value_t * max)
  {
  switch(val1->type)
    {
    case GAVL_TYPE_INT:
      {
      int max_i;
      if(gavl_value_get_int(max, &max_i) &&
         (val1->v.i > max_i))
        val1->v.i = max_i;
      }
      break;
    case GAVL_TYPE_LONG:
      {
      int64_t max_l;
      if(gavl_value_get_long(max, &max_l) &&
         (val1->v.l > max_l))
        val1->v.l = max_l;
      }
      break;
    case GAVL_TYPE_FLOAT:
      {
      double max_d;
      if(gavl_value_get_float(max, &max_d) &&
         (val1->v.d > max_d))
        val1->v.d = max_d;
      }
      break;
    default:
      break;
    }
  }

static void check_min(gavl_value_t * val1, const gavl_value_t * min)
  {
  switch(val1->type)
    {
    case GAVL_TYPE_INT:
      {
      int min_i;
      if(gavl_value_get_int(min, &min_i) &&
         (val1->v.i < min_i))
        val1->v.i = min_i;
      }
      break;
    case GAVL_TYPE_LONG:
      {
      int64_t min_l;
      if(gavl_value_get_long(min, &min_l) &&
         (val1->v.l < min_l))
        val1->v.l = min_l;
      }
      break;
    case GAVL_TYPE_FLOAT:
      {
      double min_d;
      if(gavl_value_get_float(min, &min_d) &&
         (val1->v.d < min_d))
        val1->v.d = min_d;
      }
      break;
    default:
      break;
    }
  
  }

int bg_state_clamp_value(gavl_dictionary_t * state,
                         const char * ctx, const char * var,
                         gavl_value_t * val)
  {
  gavl_value_t val_min;
  gavl_value_t val_max;

  gavl_value_init(&val_min);
  gavl_value_init(&val_max);
  
  if(!bg_state_get_range(state, ctx, var,
                         &val_min,
                         &val_max))
    return 1;

  /* min must have the same type and be smaller than max */
  if(gavl_value_compare(&val_min, &val_max) >= 0)
    return 0;
  
  check_max(val, &val_max);
  check_min(val, &val_min);
  return 1;
  }

int bg_state_toggle_value(gavl_dictionary_t * state,
                          const char * ctx, const char * var,
                          gavl_value_t * ret)
  {
  const gavl_value_t * val;

  if(!(val = bg_state_get(state, ctx, var)))
    return 0;
  
  gavl_value_copy(ret, val);

  switch(val->type)
    {
    case GAVL_TYPE_INT:
      {
      if(ret->v.i)
        ret->v.i = 0;
      else
        ret->v.i = 1;
      }
      break;
    case GAVL_TYPE_LONG:
      {
      if(ret->v.l)
        ret->v.l = 0;
      else
        ret->v.l = 1;
      }
      break;
    default:
      return 0;
    }
  return 1;
  }

int bg_state_add_value(gavl_dictionary_t * state,
                       const char * ctx, const char * var,
                       const gavl_value_t * add, gavl_value_t * ret)
  {
  const gavl_value_t * val;

  if(!(val = bg_state_get(state, ctx, var)))
    return 0;
  
  gavl_value_copy(ret, val);
  
  switch(val->type)
    {
    case GAVL_TYPE_INT:
      {
      int add_i;
      if(gavl_value_get_int(add, &add_i))
        ret->v.i += add_i;
      else
        return 0;
      }
      break;
    case GAVL_TYPE_LONG:
      {
      int64_t add_l;
      if(gavl_value_get_long(add, &add_l))
        ret->v.l += add_l;
      else
        return 0;
      
      }
      break;
    case GAVL_TYPE_FLOAT:
      {
      double add_d;
      if(gavl_value_get_float(add, &add_d))
        ret->v.d += add_d;
      else
        return 0;
      }
      break;
    default:
      return 0;
    }
  
  return bg_state_clamp_value(state, ctx, var, ret);
  }

int bg_state_handle_set_rel(gavl_dictionary_t * state, gavl_msg_t * msg)
  {
  gavl_value_t val;
  gavl_value_t add;

  const char * ctx;
  const char * var;
          
  int last = 0;

  if((msg->NS != BG_MSG_NS_STATE) ||
     (msg->ID != BG_CMD_SET_STATE_REL))
    return 0;
  
  gavl_value_init(&val);
  gavl_value_init(&add);
  
  gavl_msg_get_state(msg, &last, &ctx, &var, &add, NULL);
          
  /* Add (and clamp) value */

  bg_state_add_value(state, ctx, var, &add, &val);

  gavl_msg_free(msg);
  gavl_msg_init(msg);
  gavl_msg_set_state(msg, BG_CMD_SET_STATE, last, ctx, var, &val);
  
  gavl_value_free(&val);
  gavl_value_free(&add);
  return 1;
  }


/* Merge */

static void merge_func_child(void * priv, const char * name, const gavl_value_t * val)
  {
  gavl_dictionary_set(priv, name, val);
  }

static void merge_func(void * priv, const char * name, const gavl_value_t * val)
  {
  gavl_dictionary_t * dst;
  if(val->type != GAVL_TYPE_DICTIONARY)
    return;
  dst = gavl_dictionary_get_dictionary_create(priv, name);
  gavl_dictionary_foreach(val->v.dictionary, merge_func_child, dst);
  }

void bg_state_merge(gavl_dictionary_t * dst, const gavl_dictionary_t * src)
  {
  gavl_dictionary_foreach(src, merge_func, dst);
  }


