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



#ifndef BG_CFG_CTX_H_INCLUDED
#define BG_CFG_CTX_H_INCLUDED

#include <gmerlin/cfg_registry.h>
#include <gmerlin/bgmsg.h>

/* Configuration context */

typedef struct
  {
  bg_parameter_info_t * p;
  bg_cfg_section_t * s;
  bg_cfg_section_t * s_priv; // Privately owned, will be free()d by bg_cfg_ctx_free

  bg_set_parameter_func_t set_param;
  void * cb_data;
  char * name;
  char * long_name;
  
  bg_msg_sink_t * sink;
  int msg_id;

  } bg_cfg_ctx_t;

void bg_cfg_ctx_init(bg_cfg_ctx_t * ctx,
                     const bg_parameter_info_t * p,
                     const char * name,
                     const char * long_name,
                     bg_set_parameter_func_t set_param,
                     void * cb_data);

void bg_cfg_ctx_apply(bg_cfg_ctx_t * ctx);
void bg_cfg_ctx_free(bg_cfg_ctx_t * ctx);
void bg_cfg_ctx_set_name(bg_cfg_ctx_t * ctx, const char * name);
void bg_cfg_ctx_copy(bg_cfg_ctx_t * dst, const bg_cfg_ctx_t * src);

bg_cfg_ctx_t * bg_cfg_ctx_copy_array(const bg_cfg_ctx_t * src);

void bg_cfg_ctx_destroy_array(bg_cfg_ctx_t * ctx);

//void bg_cfg_ctx_set_parameter_array(void * data, const char * name,
//                                    gavl_value_t * val);

void bg_cfg_ctx_set_parameter(void * data, const char * name,
                              const gavl_value_t * val);

const bg_parameter_info_t *
bg_cfg_ctx_find_parameter(bg_cfg_ctx_t * arr, const char * ctx, const char * name, bg_cfg_ctx_t ** cfg_ctx);

bg_cfg_ctx_t * bg_cfg_ctx_find(bg_cfg_ctx_t * arr, const char * ctx);

void bg_cfg_ctx_apply_array(bg_cfg_ctx_t * ctx);

void bg_cfg_ctx_set_cb_array(bg_cfg_ctx_t * ctx,
                             bg_set_parameter_func_t set_param,
                             void * cb_priv);

void bg_cfg_ctx_set_sink_array(bg_cfg_ctx_t * ctx,
                               bg_msg_sink_t * sink);

void bg_cfg_ctx_array_create_sections(bg_cfg_ctx_t * ctx,
                                      bg_cfg_section_t * parent);

void bg_cfg_ctx_array_clear_sections(bg_cfg_ctx_t * ctx);


#endif // BG_CFG_CTX_H_INCLUDED

