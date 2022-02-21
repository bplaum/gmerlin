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

#ifndef __BG_CFG_REGISTRY_H_
#define __BG_CFG_REGISTRY_H_

#include <gmerlin/parameter.h>
// #include <gmerlin/cmdline.h>


/** \defgroup cfg_registry Configuration registry
 *
 *  This is a registry for configuration data, which stores the configuration
 *  of a whole application. Each module has it's own section, sections can
 *  have subsections. Inside the section, the configuration is stored as
 *  name-value pairs.
 *
 *  You can save a registry in an xml-file and load it again. Furthermore,
 *  sections can be attached to GUI-widgets. Special
 *  routines are available to copy all values from/to a section by using
 *  functions of type \ref bg_set_parameter_func_t and
 *  \ref bg_get_parameter_func_t.
 */

/** \defgroup cfg_section Configuration section
 *  \ingroup cfg_registry
 *
 *  Sections are nodes in the configuration tree. They can contain
 *  name-value pairs and child sections. Usually, config sections are
 *  kept within a configuration registry to store the applications
 *  configuration data.
 *
 *  They can, however, be used indepentently from a registry as
 *  universal data containers.
 */

/** \ingroup cfg_section
 *  \brief Configuration section
 *
 *  Opaque container for configuration data and child sections
 */

typedef gavl_dictionary_t bg_cfg_section_t;

/** \ingroup cfg_registry
 *  \brief Configuration registry
 *
 *  Opaque container for configuration sections.
 */

typedef gavl_dictionary_t bg_cfg_registry_t;

/** \ingroup cfg_registry
 *  \brief Create an empty configuration registry
 *  \returns A newly allocated and empty registry.
 *
 *  To free the registry, use \ref bg_cfg_registry_destroy.
 */

// bg_cfg_registry_t * bg_cfg_registry_create_1();

/** \ingroup cfg_registry
 *  \brief Destroy configuration registry and free all associated memory
 *  \param reg A configuration registry.
 */

// void bg_cfg_registry_destroy_1(bg_cfg_registry_t * reg);

/* cfg_xml.c */

/** \ingroup cfg_registry
 *  \brief Load a configuration registry from an xml- file
 *  \param reg A configuration registry.
 *  \param filename Name of the file
 */

int bg_cfg_registry_load(bg_cfg_registry_t * reg, const char * filename);

/** \ingroup cfg_registry
 *  \brief Save a configuration registry to an xml-file
 *  \param reg A configuration registry.
 *  \param filename Name of the file
 */

void bg_cfg_registry_save(void);

void bg_cfg_registry_save_to(const bg_cfg_registry_t * reg, const char * filename);

/* The name and xml tag of the section must be set before */

/** \ingroup cfg_section
 *  \brief Convert a configuration section into a libxml2 node
 *  \param section Configuration section
 *  \param xml_section Pointer to the xml node for the section
 *
 *  See the libxml2 documentation for more infos
 */

void bg_cfg_section_2_xml(const bg_cfg_section_t * section, xmlNodePtr xml_section);

/** \ingroup cfg_section
 *  \brief Convert libxml2 node into a configuration section
 *  \param xml_doc Pointer to the xml document
 *  \param xml_section Pointer to the xml node for the section
 *  \param section Configuration section
 *
 *  See the libxml2 documentation for more infos
 */

void bg_cfg_xml_2_section(xmlDocPtr xml_doc, xmlNodePtr xml_section,
                          bg_cfg_section_t * section);

/** \ingroup cfg_section
 *  \brief Dump a config section to a file
 *  \param section Configuration section
 *  \param filename File to write this to
 *
 *  Used for debugging
 */

void bg_cfg_section_dump(bg_cfg_section_t * section, const char * filename);

/*
 *  Path looks like "section:subsection:subsubsection"
 */

/** \ingroup cfg_registry
 *  \brief Find a section in the registry
 *  \param reg A configuration registry
 *  \param path The path
 *  \returns Configuration section
 *
 *  Path looks like "section:subsection:subsubsection". If the section
 *  does not exist, an empty section is created (including possibly
 *  missing parent sections).
 */

bg_cfg_section_t * bg_cfg_registry_find_section(bg_cfg_registry_t * reg,
                                                const char * path);

/** \ingroup cfg_section
 *  \brief Find a child of a section
 *  \param section A configuration section
 *  \param name name of the subsection
 *  \returns Configuration section
 *
 *  If the child section does not exist, an empty section is created.
 */

bg_cfg_section_t * bg_cfg_section_find_subsection(bg_cfg_section_t * section,
                                                  const char * name);

void bg_cfg_section_delete_subsection_by_name(bg_cfg_section_t * section,
                                              const char * name);

/** \ingroup cfg_section
 *  \brief Find a child of a section (const version)
 *  \param section A configuration section
 *  \param name name of the subsection
 *  \returns Configuration section or NULL
 */


const bg_cfg_section_t * bg_cfg_section_find_subsection_c(const bg_cfg_section_t * s,
                                                          const char * name);

#if 0
/** \ingroup cfg_section
 *  \brief Create a subsection at the specified position
 *  \param section A configuration section
 *  \param pos Position of the subsection (starting with 0)
 *  \returns Configuration section
 */

bg_cfg_section_t * bg_cfg_section_create_subsection_at_pos(bg_cfg_section_t * section,
                                                           int pos);

/** \ingroup cfg_section
 *  \brief Move a subsection to the specified position
 *  \param section A configuration section
 *  \param child Subsection to be moved
 *  \param pos New position of the subsection (starting with 0)
 */

void bg_cfg_section_move_child(bg_cfg_section_t * section, bg_cfg_section_t * child,
                               int pos);

#endif

/* 
 *  Create/destroy config sections
 */

/** \ingroup cfg_section
 *  \brief Create an empty config section
 *  \param name Name 
 *  \returns Configuration section
 */

bg_cfg_section_t * bg_cfg_section_create(const char * name);

/** \ingroup cfg_section
 *  \brief Create a config section from a parameter array
 *  \param name Name 
 *  \param parameters A parameter array 
 *  \returns Configuration section
 *
 *  Creates a configuration section from a parameter array.
 *  The values in the section are set from the defaults given in the
 *  array.
 */

bg_cfg_section_t *
bg_cfg_section_create_from_parameters(const char * name,
                                      const bg_parameter_info_t * parameters);

/** \ingroup cfg_section
 *  \brief Create items from a parameter info
 *  \param section Configuration section
 *  \param parameters A parameter array 
 *
 *  This iterates through parameters and creates all missing
 *  entries with the values set to their defaults
 */

void bg_cfg_section_create_items(bg_cfg_section_t * section,
                                 const bg_parameter_info_t * parameters);

/** \ingroup cfg_section
 *  \brief Destroy a config section 
 *  \param section Configuration section
 */

void bg_cfg_section_destroy(bg_cfg_section_t * section);

/** \ingroup cfg_section
 *  \brief Duplicate a configuration section 
 *  \param src Configuration section
 *  \returns A newly allocated section with all values copied from src.
 */

bg_cfg_section_t * bg_cfg_section_copy(const bg_cfg_section_t * src);

/** \ingroup cfg_section
 *  \brief Set values in a configuration section from another section
 *  \param src Source section
 *  \param dst Destination section
 *
 *  This function iterates through all entries of src and copies the values
 *  to dst. Values, which don't exist in dst, are created. The same is then
 *  done for all children of src.
 */

void bg_cfg_section_transfer(bg_cfg_section_t * src, bg_cfg_section_t * dst);

/*
 *  Get/Set section names
 */

/** \ingroup cfg_section
 *  \brief Get the name of a configuration section
 *  \param section Configuration section
 *  \returns The name
 */

const char * bg_cfg_section_get_name(bg_cfg_section_t * section);

#define bg_cfg_section_set_parameter_int(s,n,v)    gavl_dictionary_set_int(s,n,v)
#define bg_cfg_section_set_parameter_float(s,n,v)  gavl_dictionary_set_float(s,n,v)
#define bg_cfg_section_set_parameter_string(s,n,v) gavl_dictionary_set_string(s,n,v)
#define bg_cfg_section_set_parameter_time(s,n,v)   gavl_dictionary_set_long(s,n,v)


/*
 *  Get/Set values
 */

/** \ingroup cfg_section
 *  \brief Store a value in the section
 *  \param section The configuration section
 *  \param info The parameter destription
 *  \param value The value to be stored
 *
 *  If the value does not exist in the section, it is created
 *  from the parameter description.
 */

void bg_cfg_section_set_parameter(bg_cfg_section_t * section,
                                  const bg_parameter_info_t * info,
                                  const gavl_value_t * value);

/** \ingroup cfg_section
 *  \brief Set values from an option string
 *  \param section The configuration section
 *  \param info The parameter destription
 *  \param str A string describing the values
 *
 *  This takes a string from the commandline and
 *  stores it in the section.
 *
 *  \todo Document syntax for all parameter types
 */

int bg_cfg_section_set_parameters_from_string(bg_cfg_section_t * section,
                                              const bg_parameter_info_t * info,
                                              const char * str);

/** \ingroup cfg_section
 *  \brief Read a value from the section
 *  \param section The configuration section
 *  \param info The parameter destription
 *  \param value The value will be stored here
 *
 *  If the value does not exist in the section, it is created
 *  from the parameter description.
 */

void bg_cfg_section_get_parameter(bg_cfg_section_t * section,
                                  const bg_parameter_info_t * info,
                                  gavl_value_t * value);

const gavl_value_t * bg_cfg_section_get_parameter_c(const bg_cfg_section_t * section,
                                                            const char * name);


/** \ingroup cfg_section
 *  \brief Delete a subsection
 *  \param section The configuration section
 *  \param subsection The child section to be deleten
 *
 *  If the subsection if no child of section, this function does nothing.
 */

void bg_cfg_section_delete_subsection(bg_cfg_section_t * section,
                                      bg_cfg_section_t * subsection);

/** \ingroup cfg_section
 *  \brief Delete all subsections
 *  \param section The configuration section
 */

void bg_cfg_section_delete_subsections(bg_cfg_section_t * section);

/* Get parameter values, return 0 if no such entry */

/** \ingroup cfg_section
 *  \brief Get an integer value from a section
 *  \param section The configuration section
 *  \param name Name of the entry
 *  \param value Returns value
 *  \returns 1 if entry was available, 0 else.
 */ 

int bg_cfg_section_get_parameter_int(const bg_cfg_section_t * section,
                                      const char * name, int * value);

/** \ingroup cfg_section
 *  \brief Get an float value from a section
 *  \param section The configuration section
 *  \param name Name of the entry
 *  \param value Returns value
 *  \returns 1 if entry was available, 0 else.
 */ 

int bg_cfg_section_get_parameter_float(const bg_cfg_section_t * section,
                                       const char * name, float * value);

/** \ingroup cfg_section
 *  \brief Get an string value from a section
 *  \param section The configuration section
 *  \param name Name of the entry
 *  \param value Returns value
 *  \returns 1 if entry was available, 0 else.
 */ 

int bg_cfg_section_get_parameter_string(const bg_cfg_section_t * section,
                                        const char * name, const char ** value);

/** \ingroup cfg_section
 *  \brief Get an time value from a section
 *  \param section The configuration section
 *  \param name Name of the entry
 *  \param value Returns value
 *  \returns 1 if entry was available, 0 else.
 */ 

int bg_cfg_section_get_parameter_time(const bg_cfg_section_t * section,
                                      const char * name, gavl_time_t * value);


/* Apply all values found in the parameter info */

/** \ingroup cfg_section
 *  \brief Send all parameters to a module
 *  \param section The configuration section
 *  \param parameters Parameter array
 *  \param func Function to be called
 *  \param callback_data First argument passed to func
 *
 *  This function iterates though all parameters and calls
 *  func with the stored values. It is the main function to transfer
 *  data from the section to a module.
 */ 

void bg_cfg_section_apply(const bg_cfg_section_t * section,
                          const bg_parameter_info_t * parameters,
                          bg_set_parameter_func_t func,
                          void * callback_data);

/** \ingroup cfg_section
 *  \brief Send all parameters to a module without terminating
 *  \param section The configuration section
 *  \param infos Parameter array
 *  \param func Function to be called
 *  \param callback_data First argument passed to func
 *
 *  This function works like \ref bg_cfg_section_apply but doesn't
 *  call func with a NULL name argument at the end.
 */ 

void bg_cfg_section_apply_noterminate(bg_cfg_section_t * section,
                                      const bg_parameter_info_t * infos,
                                      bg_set_parameter_func_t func,
                                      void * callback_data);

/** \ingroup cfg_section
 *  \brief Get parameters from a module
 *  \param section The configuration section
 *  \param parameters Parameter array
 *  \param func Function to be called
 *  \param callback_data First argument passed to func
 *
 *  This function iterates though all parameters and calls
 *  func with the stored values. It is the main function to transfer
 *  data from the module to a section. It is used only, if the module
 *  has parameters, which are changed internally.
 */ 

void bg_cfg_section_get(bg_cfg_section_t * section,
                        const bg_parameter_info_t * parameters,
                        bg_get_parameter_func_t func,
                        void * callback_data);

/** \ingroup cfg_section
 *  \brief Qurey if a child section if available
 *  \param section The configuration section
 *  \param name Name of the child section
 *  \returns 1 if the child section is available, 0 else.
 */ 

int bg_cfg_section_has_subsection(const bg_cfg_section_t * section,
                                  const char * name);

/** \ingroup cfg_section
 *  \brief Restore default values of a section
 *  \param section The configuration section
 *  \param info Parameter info
 */ 

void bg_cfg_section_restore_defaults(bg_cfg_section_t * section,
                                     const bg_parameter_info_t * info);

/* Global init functions */

extern bg_cfg_registry_t * bg_cfg_registry;

void bg_cfg_registry_init(const char * dir);

void bg_cfg_registry_cleanup();

/* Commandline options for loading and saving the registry from the command line */
void bg_cfg_registry_opt_sc(void * data, int * argc, char *** _argv, int arg);

void bg_cfg_registry_opt_c(void * data, int * argc, char *** _argv, int arg);

/* Save config if bg_cfg_registry_opt_sc was called before */
void bg_cfg_registry_save_config();


#define BG_OPT_SAVE_CONFIG \
  { \
  .arg = "-sc", \
  .help_arg = "<file>", \
  .help_string = TRS("Save current config to a file"), \
  .callback = bg_cfg_registry_opt_sc, \
  }

#define BG_OPT_LOAD_CONFIG                      \
  { \
  .arg = "-c", \
  .help_arg = "<file>", \
  .help_string = TRS("Load config from a file"), \
  .callback = bg_cfg_registry_opt_c, \
  }


#endif /* __BG_CFG_REGISTRY_H_ */
