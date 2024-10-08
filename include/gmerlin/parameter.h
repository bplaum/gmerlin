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

/* ID tag (e.g. name of a config section or name of an element in a multi chain) */
#define BG_CFG_TAG_NAME "$NAME"

/* Dictionary or array containing child sections */
#define BG_CFG_TAG_CHILDREN "$CHILDREN"

/* Integer index for MULTI_MENU */
#define BG_CFG_TAG_IDX "$IDX"



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

typedef enum
  {
    BG_PARAMETER_SECTION,       //!< Dummy type. It contains no data but acts as a separator in notebook style configuration windows
    BG_PARAMETER_CHECKBUTTON,   //!< Bool
    BG_PARAMETER_INT,           //!< Integer spinbutton
    BG_PARAMETER_FLOAT,         //!< Float spinbutton
    BG_PARAMETER_SLIDER_INT,    //!< Integer slider
    BG_PARAMETER_SLIDER_FLOAT,  //!< Float slider
    BG_PARAMETER_STRING,        //!< String (one line only)
    BG_PARAMETER_STRING_HIDDEN, //!< Encrypted string (displays as ***)
    BG_PARAMETER_STRINGLIST,    //!< Popdown menu with string values
    BG_PARAMETER_COLOR_RGB,     //!< RGB Color
    BG_PARAMETER_COLOR_RGBA,    //!< RGBA Color
    BG_PARAMETER_FONT,          //!< Font (contains fontconfig compatible fontname)
    BG_PARAMETER_FILE,          //!< File
    BG_PARAMETER_DIRECTORY,     //!< Directory
    BG_PARAMETER_MULTI_MENU,    //!< Menu with config- and infobutton
    BG_PARAMETER_MULTI_LIST,    //!< List with config- and infobutton
    BG_PARAMETER_MULTI_CHAIN,   //!< Several subitems (including suboptions) can be arranged in a chain
    BG_PARAMETER_TIME,          //!< Time
    BG_PARAMETER_POSITION,      //!< Position (x/y coordinates, scaled 0..1)
    BG_PARAMETER_BUTTON,        //!< Pressing the button causes set_parameter to be called with NULL value
    BG_PARAMETER_DIRLIST,       //!< List of directories
  } bg_parameter_type_t;

gavl_type_t bg_parameter_type_to_gavl(bg_parameter_type_t type);

/* Flags */


#define BG_PARAMETER_SYNC           (1<<0) //!< Apply the value whenever the widgets value changes
#define BG_PARAMETER_HIDE_DIALOG    (1<<1) //!< Don't make a configuration widget (for objects, which change values themselves)
#define BG_PARAMETER_PLUGIN         (1<<3) //!< Parameter refers to a plugin
#define BG_PARAMETER_OWN_SECTION    (1<<4) //!< For parameters of the type BG_PARAMETER_SECTION: Following parameters should be stored in an own section
#define BG_PARAMETER_GLOBAL_PRESET  (1<<5) //!< For parameters of the type BG_PARAMETER_SECTION: There should be one preset for all following sections

/** \brief Typedef for parmeter description
 */

typedef struct bg_parameter_info_s bg_parameter_info_t;

/** \brief Parmeter description
 *
 *  Usually, parameter infos are passed around as NULL-terminated arrays.
 */

struct bg_parameter_info_s
  {
  char * name; //!< Unique name. Can contain alphanumeric characters plus underscore.
  char * long_name; //!< Long name (for labels)

  char * gettext_domain; //!< First argument for bindtextdomain(). In an array, it's valid for subsequent entries too.
  char * gettext_directory; //!< Second argument for bindtextdomain(). In an array, it's valid for subsequent entries too.
  
  bg_parameter_type_t type; //!< Type

  int flags; //!< Mask of BG_PARAMETER_* defines
  
  gavl_value_t val_default; //!< Default value
  gavl_value_t val_min; //!< Minimum value (for arithmetic types)
  gavl_value_t val_max; //!< Maximum value (for arithmetic types)
  
  /* Names which can be passed to set_parameter (NULL terminated) */

  char const * const * multi_names; //!< Names for multi option parameters (NULL terminated)

  /* Long names are optional, if they are NULL,
     the short names are used */

  char const * const * multi_labels; //!< Optional labels for multi option parameters
  char const * const * multi_descriptions; //!< Optional descriptions (will be displayed by info buttons)
    
  /*
   *  These are parameters for each codec.
   *  The name members of these MUST be unique with respect to the rest
   *  of the parameters passed to the same set_parameter func
   */

  struct bg_parameter_info_s const * const * multi_parameters; //!< Parameters for each option. The name members of these MUST be unique with respect to the rest of the parameters passed to the same set_parameter func
  
  int num_digits; //!< Number of digits for floating point parameters
  
  char * help_string; //!< Help strings for tooltips or --help option 

  char * preset_path; //!< Path for storing configuration presets

  char ** multi_names_nc; //!< When allocating dynamically, use this instead of multi_names and call \ref bg_parameter_info_set_const_ptrs at the end

  char ** multi_labels_nc; //!< When allocating dynamically, use this instead of multi_labels and call \ref bg_parameter_info_set_const_ptrs at the end

  char ** multi_descriptions_nc; //!< When allocating dynamically, use this instead of multi_descriptions and call \ref bg_parameter_info_set_const_ptrs at the end 

  struct bg_parameter_info_s ** multi_parameters_nc; //!< When allocating dynamically, use this instead of multi_parameters and call \ref bg_parameter_info_set_const_ptrs at the end 

  };

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

#if 1 // TODO: Kick this out completely

/** \brief Generic prototype for getting parameters from a module
 *  \param data Instance
 *  \param name Name of the parameter
 *  \param v Value
 *
 *  \returns 1 if a parameter was found and set, 0 else.
 *
 *  Provide this function, if your module changes parameters by itself.
 *  Set the \ref BG_PARAMETER_HIDE_DIALOG to prevent building config
 *  dialogs for those parameters.
 */

typedef int (*bg_get_parameter_func_t)(void * data, const char * name,
                                       gavl_value_t * v);
#endif

/** \brief Copy a single parameter info
 *  \param src Source 
 *  \param dst Destination 
 */

void bg_parameter_info_copy(bg_parameter_info_t * dst,
                            const bg_parameter_info_t * src);

/** \brief Copy a single parameter info
 *  \param dst Destination 
 *  \param src Source
 *  \param pfx Prefix
 *
 *  Like \ref bg_parameter_info_copy but prepends a prefix to the parameter name
 */

void bg_parameter_info_copy_pfx(bg_parameter_info_t * dst,
                                const bg_parameter_info_t * src, const char * pfx);


/** \brief Copy a NULL terminated parameter array
 *  \param src Source array
 *
 *  \returns A newly allocated parameter array, whose contents are copied from src.
 *
 *  Use \ref bg_parameter_info_destroy_array to free the returned array.
 */

bg_parameter_info_t *
bg_parameter_info_copy_array(const bg_parameter_info_t * src);

/** \brief Copy a NULL terminated parameter array
 *  \param src Source array
 *  \param pfx Prefix
 *  \returns A newly allocated parameter array, whose contents are copied from src.
 *
 *  Like \ref bg_parameter_info_copy_array but prepends a prefix to the name
 *  Use \ref bg_parameter_info_destroy_array to free the returned array.
 */

bg_parameter_info_t *
bg_parameter_info_copy_array_pfx(const bg_parameter_info_t * src, const char * pfx);

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


/** \brief Copy a parameter value
 *  \param dst Destination
 *  \param src Source
 *  \param info Parameter description
 *
 *  Make sure, that dst is either memset to 0 or contains
 *  data, which was created by \ref bg_parameter_value_copy
 */

#define bg_parameter_value_copy(dst, src) gavl_value_copy(dst, src)

/** \brief Free a parameter value
 *  \param val A parameter value
 *  \param type Type of the parameter
 */

#define bg_parameter_value_free(val) gavl_value_free(val)
                             


/** \brief Concatenate multiple arrays into one
 *  \param srcs NULL terminated array of source arrays
 *  \returns A newly allocated array
 */

bg_parameter_info_t *
bg_parameter_info_concat_arrays(bg_parameter_info_t const ** srcs);

/** \brief Get the index for a multi-options parameter
 *  \param info A parameter info
 *  \param val The value
 *  \returns The index of val in the multi_names array
 *
 *  If val does not occur in the multi_names[] array,
 *  try the default value. If that fails as well, return 0.
 */

int bg_parameter_get_selected(const bg_parameter_info_t * info,
                              const gavl_value_t * val);


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

// Append values to info->multi_names and info->multi_labels
void bg_parameter_info_append_option(bg_parameter_info_t * dst, const char * opt, const char * label);

/* Parse parameters in the form: param1=val1&param2=val2 */

int bg_parameter_parse_string(const char * str, gavl_dictionary_t * ret, const bg_parameter_info_t * params);



const gavl_dictionary_t * bg_multi_menu_get_selected(const gavl_value_t * val);
gavl_dictionary_t * bg_multi_menu_get_selected_nc(gavl_value_t * val);

const char * bg_multi_menu_get_selected_name(const gavl_value_t * val);

void bg_multi_menu_set_selected(gavl_value_t * val, const gavl_dictionary_t * dict);

void bg_multi_menu_set_selected_name(gavl_value_t * val, const char * name);

void bg_multi_menu_set_selected_idx(gavl_value_t * val, int idx);

int bg_multi_menu_get_selected_idx(const gavl_value_t * val);

void bg_multi_menu_create(gavl_value_t * val,
                          const bg_parameter_info_t * info);
void bg_multi_menu_update(gavl_value_t * val,
                          const bg_parameter_info_t * info);

int bg_multi_menu_get_num(const gavl_value_t * val);
const char * bg_multi_menu_get_name(gavl_value_t * val, int idx);
int bg_multi_menu_has_name(const gavl_value_t * val, const char * name);
void bg_multi_menu_remove(gavl_value_t * val, int idx);


/** @}
 */


#endif // BG_PARAMETER_H_INCLUDED


