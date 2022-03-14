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

#ifndef BG_STATE_H_INCLUDED
#define BG_STATE_H_INCLUDED

typedef struct
  {
  const char * name;
  gavl_type_t type;
  gavl_value_t val_default;
  
  gavl_value_t val_min;
  gavl_value_t val_max;
  } bg_state_var_desc_t;

void bg_state_init_ctx(gavl_dictionary_t * state, const char * ctx, const bg_state_var_desc_t * desc);

/* Set the allowed range for a state variable */
void bg_state_set_range(gavl_dictionary_t * state,
                        const char * ctx, const char * var,
                        const gavl_value_t * min,
                        const gavl_value_t * max);

int bg_state_get_range(gavl_dictionary_t * state,
                       const char * ctx, const char * var,
                       gavl_value_t * min,
                       gavl_value_t * max);

void bg_state_set_range_int(gavl_dictionary_t * state,
                            const char * ctx, const char * var,
                            int min, int max);

void bg_state_set_range_long(gavl_dictionary_t * state,
                             const char * ctx, const char * var,
                             int64_t min, int64_t max);

void bg_state_set_range_float(gavl_dictionary_t * state,
                              const char * ctx, const char * var,
                              double min, double max);

/* Return 0 if value has no allowed range (i.e. max <= min) */

int bg_state_add_value(gavl_dictionary_t * state,
                       const char * ctx, const char * var,
                       const gavl_value_t * add, gavl_value_t * ret);

int bg_state_clamp_value(gavl_dictionary_t * state,
                         const char * ctx, const char * var,
                         gavl_value_t * val);

int bg_state_toggle_value(gavl_dictionary_t * state,
                          const char * ctx, const char * var,
                          gavl_value_t * ret);

void bg_state_merge(gavl_dictionary_t * dst, const gavl_dictionary_t * src);

void bg_state_apply(gavl_dictionary_t * state, bg_msg_sink_t * sink, int id);
void bg_state_apply_ctx(gavl_dictionary_t * state, const char * ctx, bg_msg_sink_t * sink, int id);

void bg_state_set(gavl_dictionary_t * state,
                  int last,
                  const char * ctx,
                  const char * var,
                  const gavl_value_t * val,
                  bg_msg_sink_t * sink, int id);


const gavl_value_t * bg_state_get(gavl_dictionary_t * state,
                                  const char * ctx,
                                  const char * var);

/*
  
 */

void bg_msg_set_state(gavl_msg_t * msg,
                      int id,
                      int last,
                      const char * ctx,
                      const char * var,
                      const gavl_value_t * val);

void bg_msg_set_state_nocopy(gavl_msg_t * msg,
                             int id,
                             int last,
                             const char * ctx,
                             const char * var,
                             gavl_value_t * val);

void bg_msg_get_state(const gavl_msg_t * msg,
                      int * last,
                      const char ** ctx_p,
                      const char ** var_p,
                      gavl_value_t * val,
                      gavl_dictionary_t * dict);

const char * bg_state_get_uuid(gavl_dictionary_t * state,
                               const char * ctx,
                               const char * var,
                               bg_msg_sink_t * sink, int id);


/* String initalizer for UUID. Will be changed to a real UUID when encountered */

//                                "550e8400-e29b-11d4-a716-446655440000"

#define BG_STATE_UUID_INITILAIZER "----- GMERLIN UUID INITIALIZER -----"

/* Standardized variables */

#define BG_STATE_CTX_OV        "ov"

#define BG_STATE_OV_CONTRAST   "$contrast"
#define BG_STATE_OV_BRIGHTNESS "$brightness"
#define BG_STATE_OV_SATURATION "$saturation"
#define BG_STATE_OV_ZOOM       "$zoom"
#define BG_STATE_OV_SQUEEZE    "$squeeze"
#define BG_STATE_OV_FULLSCREEN "$fullscreen"
#define BG_STATE_OV_TITLE      "$title"    // Window title
#define BG_STATE_OV_VISIBLE    "$visible"  // 
#define BG_STATE_OV_WINDOW_ID  "$winid"    // Window ID

/* Min- Max values for Hue, Saturation, Brightness and Contrast */

#define BG_BRIGHTNESS_MIN  -10.0
#define BG_BRIGHTNESS_MAX   10.0
#define BG_BRIGHTNESS_DELTA  0.5

#define BG_SATURATION_MIN    -10.0
#define BG_SATURATION_MAX     10.0
#define BG_SATURATION_DELTA    0.5

#define BG_CONTRAST_MIN      -10.0
#define BG_CONTRAST_MAX       10.0
#define BG_CONTRAST_DELTA      0.5

#endif // BG_STATE_H_INCLUDED

