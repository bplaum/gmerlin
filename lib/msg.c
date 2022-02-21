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
#include <assert.h>

#include <config.h>


#include <gavl/gavl.h>
#include <gavl/gavf.h>
#include <gavl/utils.h>

#include <gmerlin/utils.h>
#include <gmerlin/parameter.h>
#include <gmerlin/msgqueue.h>
#include <gmerlin/bggavl.h>

#include <gmerlin/translation.h>
#include <gmerlin/log.h>
#define LOG_DOMAIN "msg"

/* .parameters =
   .arg_0 = name
   .arg_1 = value
*/

void bg_msg_set_parameter_idx(gavl_msg_t * msg,
                              const char * name,
                              const gavl_value_t * val, int idx)
  {
  if(!name)
    return;
  
  gavl_msg_set_arg_string(msg, idx + 0, name);

  if(!name)
    {
    gavl_msg_set_arg_string(msg, idx + 1, NULL);
    return;
    }
  gavl_msg_set_arg(msg, idx + 1, val);
  }

void bg_msg_set_parameter(gavl_msg_t * msg,
                          const char * name,
                          const gavl_value_t * val)
  {
  bg_msg_set_parameter_idx(msg, name, val, 0);
  }

/* Called by frontend */
void bg_msg_set_parameter_ctx(gavl_msg_t * msg,
                              int id,
                              const char * ctx,
                              const char * name,
                              const gavl_value_t * val)

  {
  //  fprintf(stderr, "set_parameter_ctx %s %s\n", ctx, name);

  gavl_msg_set_id_ns(msg, id, BG_MSG_NS_PARAMETER);
  gavl_msg_set_arg_string(msg, 0, ctx);
  bg_msg_set_parameter_idx(msg, name, val, 1);
  }

void bg_msg_set_chain_parameter_ctx(gavl_msg_t * msg,
                                    const char * ctx,
                                    const char * name,
                                    int idx,
                                    const char * sub_name,
                                    const gavl_value_t * val)
  {
  gavl_msg_set_id_ns(msg, BG_MSG_SET_CHAIN_PARAMETER_CTX, BG_MSG_NS_PARAMETER);
  gavl_msg_set_arg_string(msg, 0, ctx);

  gavl_msg_set_arg_string(msg, 1, name);
  gavl_msg_set_arg_int(msg,    2, idx);
  bg_msg_set_parameter_idx(msg, sub_name, val, 3);
  }

void bg_msg_set_multi_parameter_ctx(gavl_msg_t * msg,
                                    const char * ctx,
                                    const char * name,
                                    const char * el_name,
                                    const char * sub_name,
                                    const gavl_value_t * val)
  {
  gavl_msg_set_id_ns(msg, BG_MSG_SET_MULTI_PARAMETER_CTX, BG_MSG_NS_PARAMETER);
  gavl_msg_set_arg_string(msg, 0, ctx);

  gavl_msg_set_arg_string(msg, 1, name);
  gavl_msg_set_arg_string(msg,    2, el_name);
  bg_msg_set_parameter_idx(msg, sub_name, val, 3);
  }

void bg_msg_set_parameter_ctx_term(bg_msg_sink_t * sink)
  {
  gavl_msg_t * msg;
  gavl_value_t val;

  msg = bg_msg_sink_get(sink);

  gavl_value_init(&val);
  gavl_value_set_string(&val, NULL);
  bg_msg_set_parameter_ctx(msg, BG_MSG_SET_PARAMETER_CTX, NULL, NULL, &val);
  bg_msg_sink_put(sink, msg);
  }

void bg_msg_get_parameter_idx(gavl_msg_t * msg,
                              const char ** name,
                              gavl_value_t * val,
                              int idx)
  {
  if(!(*name = gavl_msg_get_arg_string_c(msg, idx + 0)))
    return;
  
  gavl_value_copy(val, &msg->args[idx + 1]);
  }
  
void bg_msg_get_parameter(gavl_msg_t * msg,
                          const char ** name,
                          gavl_value_t * val)
  {
  bg_msg_get_parameter_idx(msg, name, val, 0);
  }

void bg_msg_get_parameter_ctx(gavl_msg_t * msg,
                              const char ** ctx,
                              const char ** name,
                              gavl_value_t * val)
  {
  if(ctx)
    *ctx = gavl_msg_get_arg_string_c(msg, 0);
  bg_msg_get_parameter_idx(msg, name, val, 1);
  }

void bg_msg_get_chain_parameter_ctx(gavl_msg_t * msg,
                                    const char ** ctx,
                                    const char ** name,
                                    int * idx,
                                    const char ** sub_name,
                                    gavl_value_t * val)
  {
  if(ctx)
    *ctx = gavl_msg_get_arg_string_c(msg, 0);
  if(name)
    *name = gavl_msg_get_arg_string_c(msg, 1);
  if(idx)
    *idx = gavl_msg_get_arg_int(msg, 2);
  bg_msg_get_parameter_idx(msg, sub_name, val, 3);
  }

void bg_msg_get_multi_parameter_ctx(gavl_msg_t * msg,
                                    const char ** ctx,
                                    const char ** name,
                                    const char ** el_name,
                                    const char ** sub_name,
                                    gavl_value_t * val)
  {
  if(ctx)
    *ctx = gavl_msg_get_arg_string_c(msg, 0);
  if(name)
    *name = gavl_msg_get_arg_string_c(msg, 1);
  if(el_name)
    *el_name = gavl_msg_get_arg_string_c(msg, 2);
  bg_msg_get_parameter_idx(msg, sub_name, val, 3);
  }


/* JSON Support */


struct json_object *  bg_msg_to_json(const gavl_msg_t * msg)
  {
  struct json_object * json = json_object_new_object();
  struct json_object * args;
  struct json_object * header;
  int i;
  
  header = json_object_new_object();
  bg_dictionary_to_json(&msg->header, header);

  json_object_object_add(json, "header", header);
  
  args = json_object_new_array();
  json_object_object_add(json, "args", args);
  
  for(i = 0; i < msg->num_args; i++)
    json_object_array_add(args, bg_value_to_json(&msg->args[i]));
  
  return json;
  }

char * bg_msg_to_json_str(const gavl_msg_t * msg)
  {
  char * ret;
  
  json_object * obj = bg_msg_to_json(msg);
  ret = gavl_strdup(json_object_to_json_string_ext(obj, 0));
  if(obj)
    json_object_put(obj);
  return ret;
  }

int bg_msg_from_json(gavl_msg_t * msg, json_object * obj)
  {
  int i, num_args;
  json_object * header;
  json_object * args;
  json_object * arg_object;
  
  /* Do some sanity checks */
  if(!json_object_is_type(obj, json_type_object))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "bg_msg_from_json: Arg has type %d", json_object_get_type(obj));
    return 0;
    }
  if(!json_object_object_get_ex(obj, "header", &header))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Header missing");
    return 0;
    }
  if(!json_object_is_type(header, json_type_object))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "bg_msg_from_json: Header has wrong type");
    return 0;
    }
  
  gavl_msg_free(msg);
  
  bg_dictionary_from_json(&msg->header, header);
  
  if(!json_object_object_get_ex(obj, "args", &args))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "bg_msg_from_json: Args missing");
    return 0;
    }
  if(!json_object_is_type(args, json_type_array))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "bg_msg_from_json: Args have wrong type");
    return 0;
    }
  
  num_args = json_object_array_length(args);

  if(num_args > GAVL_MSG_MAX_ARGS)
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "bg_msg_from_json: Too many args: %d > %d", num_args, GAVL_MSG_MAX_ARGS);
    return 0;
    }
  msg->num_args = num_args;
  
  for(i = 0; i < num_args; i++)
    {
    arg_object = json_object_array_get_idx(args, i);

    if(!bg_value_from_json(&msg->args[i], arg_object))
      {
      gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Reading arg %d/%d failed: %s", i+1, num_args,
             json_object_to_json_string(arg_object));
      return 0;
      }
    }

  gavl_msg_apply_header(msg);
  
  return 1;
  }

int
bg_msg_from_json_str(gavl_msg_t * msg, const char * str)
  {
  int ret = 0;
  struct json_object *obj = NULL;
          
  /* Handle message */
  if(!(obj = json_tokener_parse((const char*)str)))
    {
    gavl_log(GAVL_LOG_ERROR, LOG_DOMAIN, "Parsing json \"%s\" failed", str);
    }
  else if(bg_msg_from_json(msg, obj))
    ret = 1;
  
  if(obj)
    json_object_put(obj);

  return ret;
  }


int bg_msg_merge(gavl_msg_t * msg, gavl_msg_t * next)
  {
  const char * s1;
  const char * s2;
  
  if((msg->NS != next->NS) || (msg->ID != next->ID))
    return 0;

  if(msg->NS == BG_MSG_NS_STATE)
    {
    gavl_value_t * val;
    gavl_value_t * val_next;

    if(msg->ID == BG_CMD_SET_STATE)
      {
      if(!(s1 = gavl_msg_get_arg_string_c(msg, 1)) ||
         !(s2 = gavl_msg_get_arg_string_c(next, 1)) ||
         strcmp(s1, s2) ||
         !(s1 = gavl_msg_get_arg_string_c(msg, 2)) ||
         !(s2 = gavl_msg_get_arg_string_c(next, 2)) ||
         strcmp(s1, s2) ||
         !(val = gavl_msg_get_arg_nc(msg, 3)) ||
         !(val_next = gavl_msg_get_arg_nc(next, 3)) ||
         (val->type != val_next->type))
        return 0;

      gavl_value_reset(val);
      gavl_value_move(val, val_next);
      return 1;
      }
    else if(msg->ID == BG_CMD_SET_STATE_REL)
      {
      if(!(s1 = gavl_msg_get_arg_string_c(msg, 1)) ||
         !(s2 = gavl_msg_get_arg_string_c(msg, 1)) ||
         strcmp(s1, s2) ||
         !(s1 = gavl_msg_get_arg_string_c(msg, 2)) ||
         !(s2 = gavl_msg_get_arg_string_c(msg, 2)) ||
         strcmp(s1, s2) ||
         !(val = gavl_msg_get_arg_nc(msg, 3)) ||
         !(val_next = gavl_msg_get_arg_nc(next, 3)) ||
         (val->type != val_next->type))
        return 0;

      switch(val->type)
        {
        case GAVL_TYPE_INT:
          val->v.i += val_next->v.i;
          return 1;
          break;
        case GAVL_TYPE_LONG:
          val->v.l += val_next->v.l;
          return 1;
          break;
        case GAVL_TYPE_FLOAT:
          val->v.d += val_next->v.d;
          return 1;
          break;
        default:
          return 0;
        }
      }
    }
  return 0;
  }

