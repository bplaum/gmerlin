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

/*
 * Singlepicture "metaplugins" are for the application like
 * all other plugin. The internal difference is, that they
 * are created based on the available image writers.

 * They just translate the Video stream API into the one for
 * single pictures
 */

/*
 *  Create info structures.
 *  Called by the registry after all external plugins
 *  are detected
 */

bg_plugin_info_t * bg_singlepic_input_info(void);
bg_plugin_info_t * bg_singlepic_stills_input_info(void);
bg_plugin_info_t * bg_singlepic_encoder_info(void);

/*
 *  Get the static plugin infos
 */

const bg_plugin_common_t * bg_singlepic_input_get(void);
const bg_plugin_common_t * bg_singlepic_stills_input_get(void);
const bg_plugin_common_t * bg_singlepic_encoder_get(void);

/*
 *  Create the plugins (These are a replacement for the create() methods
 */

void * bg_singlepic_input_create(void);
void * bg_singlepic_stills_input_create(void);
void * bg_singlepic_encoder_create(void);

#define bg_singlepic_encoder_name        "e_singlepic"
#define bg_singlepic_input_name        "i_singlepic"
#define bg_singlepic_stills_input_name "i_singlepic_stills"
