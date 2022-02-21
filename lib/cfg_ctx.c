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

#include <stdlib.h>
#include <string.h>


#include <config.h>

#include <gmerlin/cfg_registry.h>
#include <gmerlin/cfgctx.h>
#include <gavl/utils.h>
#include <gmerlin/utils.h>
#include <gmerlin/translation.h>
#include <gmerlin/log.h>
#define LOG_DOMAIN "cfgctx"

static const bg_parameter_info_t *
bg_parameter_find_ctx(bg_cfg_ctx_t * ctx,
                      const char * name);


void bg_cfg_ctx_init(bg_cfg_ctx_t * ctx,
                     const bg_parameter_info_t * p,
                     const char * name,
                     const char * long_name,
                     bg_set_parameter_func_t set_param,
                     void * cb_data)
  {
  memset(ctx, 0, sizeof(*ctx));
  ctx->name = gavl_strdup(name);
  ctx->long_name = gavl_strdup(long_name);
  ctx->p = bg_parameter_info_copy_array(p);
  ctx->set_param = set_param;
  ctx->cb_data = cb_data;
  }

void bg_cfg_ctx_apply(bg_cfg_ctx_t * ctx)
  {
  bg_cfg_section_apply(ctx->s,
                       ctx->p,
                       ctx->set_param,
                       ctx->cb_data);
  }

void bg_cfg_ctx_set_name(bg_cfg_ctx_t * ctx, const char * name)
  {
  ctx->name = gavl_strrep(ctx->name, name);
  }

void bg_cfg_ctx_free(bg_cfg_ctx_t * ctx)
  {
  if(ctx->p)
    bg_parameter_info_destroy_array(ctx->p);
  if(ctx->name)
    free(ctx->name);
  if(ctx->long_name)
    free(ctx->long_name);
  if(ctx->s_priv)
    bg_cfg_section_destroy(ctx->s_priv);
  }

void bg_cfg_ctx_copy(bg_cfg_ctx_t * dst, const bg_cfg_ctx_t * src)
  {
  /* Copy static members */
  memcpy(dst, src, sizeof(*dst));
  
  dst->name      = gavl_strdup(src->name);
  dst->long_name = gavl_strdup(src->long_name);
  
  dst->p = bg_parameter_info_copy_array(src->p);
  dst->set_param = NULL;
  dst->cb_data   = NULL;
  dst->sink      = NULL;
  }

bg_cfg_ctx_t * bg_cfg_ctx_copy_array(const bg_cfg_ctx_t * src)
  {
  bg_cfg_ctx_t * ret;
  int i;
  int num = 0;

  while(src[num].p)
    num++;

  ret = calloc(num+1, sizeof(*ret));
  
  for(i = 0; i < num; i++)
    bg_cfg_ctx_copy(&ret[i], &src[i]);
  return ret;
  }

void bg_cfg_ctx_destroy_array(bg_cfg_ctx_t * ctx)
  {
  int i = 0;
  
  while(ctx[i].p)
    {
    bg_cfg_ctx_free(&ctx[i]);
    i++;
    }
  free(ctx);
  }

bg_cfg_ctx_t * bg_cfg_ctx_find(bg_cfg_ctx_t * arr, const char * ctx)
  {
  int idx = 0;
  
  while(arr[idx].p)
    {
    if(arr[idx].name &&
       !strcmp(ctx, arr[idx].name))
      return arr + idx;
    idx++;
    }
  return NULL;
  }


/* Called by frontend */
void bg_cfg_ctx_set_parameter(void * data, const char * name,
                              const gavl_value_t * val)
  {
  bg_cfg_ctx_t * ctx = data;

  /* If we have a sink, use that instead */
  if(ctx->sink)
    {
    gavl_msg_t * msg;
    bg_cfg_ctx_t * ctx = data;
    const bg_parameter_info_t * info;
    
    // if(!strcmp(ctx->name, "$general"))
    
    //    fprintf(stderr, "bg_cfg_ctx_set_parameter %p %s.%s\n", ctx->sink, ctx->name, name);
    
    msg = bg_msg_sink_get(ctx->sink);
    
    if(name && (info = bg_parameter_find_ctx(ctx, name)))
      {
      bg_msg_set_parameter_ctx(msg, BG_MSG_SET_PARAMETER_CTX, ctx->name, name, val);
      }
    else
      {
      bg_msg_set_parameter_ctx(msg, BG_MSG_SET_PARAMETER_CTX, ctx->name, NULL, NULL);
      }
    
    bg_msg_sink_put(ctx->sink, msg);

    }
  else if(ctx->set_param)
    ctx->set_param(ctx->cb_data, name, val);
  }



const bg_parameter_info_t *
bg_cfg_ctx_find_parameter(bg_cfg_ctx_t * arr, const char * ctx, const char * name, bg_cfg_ctx_t ** cfg_ctx)
  {
  if(cfg_ctx)
    *cfg_ctx = NULL;

  if(!(arr = bg_cfg_ctx_find(arr, ctx)))
    return NULL;

  if(cfg_ctx)
    *cfg_ctx = arr;
  
  return bg_parameter_find(arr->p, name);
  }

/* Called by frontend */
void bg_cfg_ctx_apply_array(bg_cfg_ctx_t * ctx)
  {
  int i = 0;
  while(ctx[i].p)
    {
    if((ctx[i].set_param || ctx[i].sink) && ctx[i].s)
      {
#if 0
      
      bg_cfg_section_apply_noterminate(ctx[i].s,
                                       ctx[i].p,
                                       bg_cfg_ctx_set_parameter,
                                       &ctx[i]);
#else
      bg_cfg_section_apply(ctx[i].s,
                           ctx[i].p,
                           bg_cfg_ctx_set_parameter,
                           &ctx[i]);
#endif
      
      }
    else
      fprintf(stderr, "Not applying ctx %s\n", ctx[i].name);
    
    i++;
    }

  if(ctx[0].sink)
    bg_msg_set_parameter_ctx_term(ctx[0].sink);
  
  }

void bg_cfg_ctx_set_cb_array(bg_cfg_ctx_t * ctx,
                             bg_set_parameter_func_t set_param,
                             void * cb_data)
  {
  int i = 0;
  while(ctx[i].p)
    {
    ctx[i].set_param = set_param;
    ctx[i].cb_data   = cb_data;
    i++;
    }
  }

static const bg_parameter_info_t *
bg_parameter_find_ctx(bg_cfg_ctx_t * ctx,
                      const char * name)
  {
  char ** str = NULL;
  char ** str1 = NULL;
  
  int idx1, idx2;
  const bg_parameter_info_t * ret = NULL;
  const bg_parameter_info_t * info;
  const char * pos;
  if((pos = strchr(name, '.')))
    {
    const char * val_str = NULL;
  
    str = gavl_strbreak(name, '.');
    info = bg_parameter_find(ctx->p, str[0]);
    
    if(info->type == BG_PARAMETER_MULTI_CHAIN)
      {
      if(!ctx->s)
        goto fail;
      
      if(!str[0] || !str[1] || !str[2])
        goto fail;
      
      if(bg_cfg_section_get_parameter_string(ctx->s,
                                             str[0],
                                             &val_str) && val_str)
        {
        str1 = gavl_strbreak(val_str, ',');

        idx1 = atoi(str[1]);

        idx2 = 0;

        /* Check if array has enough entries */
        while(idx2 < idx1)
          {
          if(!str1[idx2])
            goto fail;
          idx2++;
          }

        /* Search for parameters */
        idx2 = 0;

        while(info->multi_names[idx2])
          {
          if(!strcmp(info->multi_names[idx2], str1[idx1]))
            {
            ret = bg_parameter_find(info->multi_parameters[idx2], str[2]);
            break;
            }
          idx2++;
          }
        }
      }
    }
  else
    ret = bg_parameter_find(ctx->p, name);

  fail:
  
  if(str)
    gavl_strbreak_free(str);
  
  if(str1)
    gavl_strbreak_free(str1);
  
  
  return ret;
  }


void bg_cfg_ctx_set_sink_array(bg_cfg_ctx_t * ctx,
                               bg_msg_sink_t * sink)
  {
  int i = 0;
  while(ctx[i].p)
    {
    ctx[i].sink = sink;
    i++;
    }
  
  }

void
bg_cfg_ctx_array_create_sections(bg_cfg_ctx_t * ctx, bg_cfg_section_t * parent)
  {
  int i = 0;
  while(ctx[i].p)
    {
    if(!parent)
      {
      ctx[i].s_priv = bg_cfg_section_create_from_parameters(ctx[i].name, ctx[i].p);
      ctx[i].s = ctx[i].s_priv;
      }
    else if(!bg_cfg_section_has_subsection(parent, ctx[i].name))
      {
      ctx[i].s = bg_cfg_section_find_subsection(parent, ctx[i].name);
      /* Set defaults */
      bg_cfg_section_create_items(ctx[i].s, ctx[i].p);
      }
    else
      ctx[i].s = bg_cfg_section_find_subsection(parent,ctx[i].name);
    i++;
    }
  };

void bg_cfg_ctx_array_clear_sections(bg_cfg_ctx_t * ctx)
  {
  int i = 0;
  while(ctx[i].p)
    {
    if(ctx[i].s_priv)
      {
      bg_cfg_section_destroy(ctx[i].s_priv);
      ctx[i].s_priv = NULL;
      }
    ctx[i].s = NULL;
    i++;
    }

  }
   
