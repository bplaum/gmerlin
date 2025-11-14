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



#ifndef BG_PARAMETER_H_INCLUDED
#define BG_PARAMETER_H_INCLUDED

#include <libxml/tree.h>
#include <libxml/parser.h>

#include <gavl/gavl.h>
#include <gavl/value.h>
#include <gavl/parameter.h>

/* ID tag (e.g. name of a config section or name of an element in a multi chain) */
#define BG_CFG_TAG_NAME "$NAME"

/* Dictionary or array containing child sections */
#define BG_CFG_TAG_CHILDREN "$CHILDREN"


/**  \defgroup parameter Parameter description
 *
 *   Parameters are universal data containers, which
 *   are the basis for all configuration mechanisms.
 *
 *   A configurable module foo, should provide at least 2 functions.
 *   One, which lets the application get a null-terminated array of parameter description
 *   and one of type \ref bg_set_parameter_func_t. It's up to the module, if the parameter array
 *   is allocated per instance or if it's just a static array. Some parameters (e.g. window
 *   coordinates) are not configured by a dialog. Instead, they are changed by the module.
 *   For these parameters, set \ref BG_PARAMETER_HIDE_DIALOG for the flags and provide another
 *   function of type \ref bg_get_parameter_func_t, which lets the core read the updated value.
 *
 *  @{
 */

/* Universal Parameter setting mechanism */

/** \brief Parameter type
 *
 *  These define both the data type and
 *  the appearance of the configuration widget.
 */

#define BG_PARAMETER_SECTION       GAVL_PARAMETER_SECTION
#define BG_PARAMETER_CHECKBUTTON   GAVL_PARAMETER_CHECKBUTTON
#define BG_PARAMETER_INT           GAVL_PARAMETER_INT
#define BG_PARAMETER_FLOAT         GAVL_PARAMETER_FLOAT
#define BG_PARAMETER_SLIDER_INT    GAVL_PARAMETER_SLIDER_INT
#define BG_PARAMETER_SLIDER_FLOAT  GAVL_PARAMETER_SLIDER_FLOAT
#define BG_PARAMETER_STRING        GAVL_PARAMETER_STRING
#define BG_PARAMETER_STRING_HIDDEN GAVL_PARAMETER_STRING_HIDDEN
#define BG_PARAMETER_STRINGLIST    GAVL_PARAMETER_STRINGLIST
#define BG_PARAMETER_COLOR_RGB     GAVL_PARAMETER_COLOR_RGB
#define BG_PARAMETER_COLOR_RGBA    GAVL_PARAMETER_COLOR_RGBA
#define BG_PARAMETER_FONT          GAVL_PARAMETER_FONT
#define BG_PARAMETER_FILE          GAVL_PARAMETER_FILE
#define BG_PARAMETER_DIRECTORY     GAVL_PARAMETER_DIRECTORY
#define BG_PARAMETER_MULTI_MENU    GAVL_PARAMETER_MULTI_MENU
#define BG_PARAMETER_MULTI_LIST    GAVL_PARAMETER_MULTI_LIST
#define BG_PARAMETER_MULTI_CHAIN   GAVL_PARAMETER_MULTI_CHAIN
#define BG_PARAMETER_TIME          GAVL_PARAMETER_TIME
#define BG_PARAMETER_POSITION      GAVL_PARAMETER_POSITION
#define BG_PARAMETER_BUTTON        GAVL_PARAMETER_BUTTON
#define BG_PARAMETER_DIRLIST       GAVL_PARAMETER_DIRLIST

#define bg_parameter_type_t gavl_parameter_type_t


/* Flags */


#define BG_PARAMETER_SYNC           (1<<0) //!< Apply the value whenever the widgets value changes
#define BG_PARAMETER_HIDE_DIALOG    (1<<1) //!< Don't make a configuration widget (for objects, which change values themselves)

/** \brief Typedef for parmeter description
 */

typedef gavl_parameter_info_t bg_parameter_info_t;


/* Prototype for setting/getting parameters */

/*
 *  NOTE: All applications MUST call a bg_set_parameter_func with
 *  a NULL name argument to signal, that all parameters are set now
 */

/** \brief Generic prototype for setting parameters in a module
 *  \param data Instance
 *  \param name Name of the parameter
 *  \param v Value
 *
 *  This function is usually called from "Apply" buttons in config dialogs.
 *  It's called subsequently for all defined püarameters. After that, it *must*
 *  be called with a NULL argument for the name to signal, that all parameters
 *  are set. Modules can do some additional setup stuff then. If not, the name == NULL
 *  case must be handled nevertheless.
 */

typedef void (*bg_set_parameter_func_t)(void * data, const char * name,
                                        const gavl_value_t * v);


/** \brief Copy a single parameter info
 *  \param src Source 
 *  \param dst Destination 
 */

void bg_parameter_info_copy(bg_parameter_info_t * dst,
                            const bg_parameter_info_t * src);


/** \brief Copy a NULL terminated parameter array
 *  \param src Source array
 *
 *  \returns A newly allocated parameter array, whose contents are copied from src.
 *
 *  Use \ref bg_parameter_info_destroy_array to free the returned array.
 */

bg_parameter_info_t *
bg_parameter_info_copy_array(const bg_parameter_info_t * src);

/** \brief Set the const pointers of a dynamically allocated parameter info
 *  \param info A parameter info
 *
 *  This copied the adresses of the *_nc pointers to their constant equivalents.
 *  Use this for each parameter in routines, which dynamically allocate parameter infos.
 */

void
bg_parameter_info_set_const_ptrs(bg_parameter_info_t * info);

/** \brief Free a NULL terminated parameter array
 *  \param info Parameter array
 */

void bg_parameter_info_destroy_array(bg_parameter_info_t * info);

/** \brief Free all memory held by this parameter info
 *  \param info Parameter info
 */

void bg_parameter_info_free(bg_parameter_info_t * info);



/** \brief Concatenate multiple arrays into one
 *  \param srcs NULL terminated array of source arrays
 *  \returns A newly allocated array
 */

bg_parameter_info_t *
bg_parameter_info_concat_arrays(bg_parameter_info_t const ** srcs);


/** \brief Find a parameter info
 *  \param info A parameter info
 *  \param name The name of the the parameter
 *  \returns Parameter info matching name or NULL
 *
 *  This function looks for a parameter info with the
 *  given name in an array or parameters. Sub-parameters
 *  are also searched.
 */

const bg_parameter_info_t *
bg_parameter_find(const bg_parameter_info_t * info,
                  const char * name);


/** \brief Convert a libxml2 node into a parameter array
 *  \param xml_doc Pointer to the xml document
 *  \param xml_parameters Pointer to the xml node containing the parameters
 *  \returns A newly allocated array
 *
 *  See the libxml2 documentation for more infos
 */


bg_parameter_info_t * bg_xml_2_parameters(xmlDocPtr xml_doc,
                                          xmlNodePtr xml_parameters);

/** \brief Convert a parameter array into a libxml2 node
 *  \param info Parameter array
 *  \param xml_parameters Pointer to the xml node for the parameters
 *
 *  See the libxml2 documentation for more infos
 */

void
bg_parameters_2_xml(const bg_parameter_info_t * info, xmlNodePtr xml_parameters);

/** \brief Dump a parameter array into a xml file
 *  \param info Parameter array
 *  \param filename File to dump to
 *
 *  Used for debugging
 */

void
bg_parameters_dump(const bg_parameter_info_t * info, const char * filename);


/* Parse parameters in the form: param1=val1&param2=val2 */

int bg_parameter_parse_string(const char * str, gavl_dictionary_t * ret,
                              const bg_parameter_info_t * params);


/** @}
 */


#endif // BG_PARAMETER_H_INCLUDED


