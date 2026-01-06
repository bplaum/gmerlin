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



#ifndef BG_MSGQUEUE_H_INCLUDED
#define BG_MSGQUEUE_H_INCLUDED

#include <gavl/gavl.h>
#include <gavl/gavldsp.h>
#include <gavl/metatags.h>
#include <gavl/msg.h>
#include <gavl/io.h>

#include <json-c/json.h>

typedef struct
  {
  uint32_t id;
  uint32_t ns;
  } bg_msg_desc_t;


/** \defgroup messages Messages
 *  \brief Communication inside and between applications
 *
 *  Gmerlin messages are a universal method to do communication between
 *  processes or threads. Each message consists of an integer ID and
 *  a number of arguments. Arguments can be strings, numbers or complex types
 *  like \ref gavl_audio_format_t. For inter-thread comminucation, you can pass pointers
 *  as arguments as well.
 *
 *  For multithread applications, there are message queues (\ref bg_msg_queue_t). They
 *  are thread save FIFO structures, which allow asynchronous communication between threads.
 *
 *  For communication via sockets, there are the ultra-simple functions \ref bg_msg_read_socket and
 *  \ref bg_msg_write_socket, which can be used to build network protocols
 *  (e.g. remote control of applications)
 *  @{
 */

/*
 *  This on will be used for remote controls,
 *  return FALSE on error
 */

/** \brief Set a parameter
 *  \param msg A message
 *  \param type Type of the parameter
 *  \param name Name of the parameter
 *  \param val Value for the parameter
 */

void bg_msg_set_parameter(gavl_msg_t * msg,
                          const char * name,
                          const gavl_value_t * val);

/** \brief Set a parameter plus context
 *  \param msg A message
 *  \param ctx Context
 *  \param name Name of the parameter
 *  \param type Type of the parameter
 *  \param val Value for the parameter
 */

void bg_msg_set_parameter_ctx(gavl_msg_t * msg,
                              int id,
                              const char * ctx,
                              const char * name,
                              const gavl_value_t * val);

void bg_msg_set_chain_parameter_ctx(gavl_msg_t * msg,
                                    const char * ctx,
                                    const char * name,
                                    int idx,
                                    const char * sub_name,
                                    const gavl_value_t * val);

#if 0
void bg_msg_set_multi_parameter_ctx(gavl_msg_t * msg,
                                    const char * ctx,
                                    const char * name,
                                    const char * el_name,
                                    const char * sub_name,
                                    const gavl_value_t * val);
#endif

/** \brief Get a parameter
 *  \param msg A message
 *  \param type Type of the parameter
 *  \param name Name of the parameter
 *  \param val Value for the parameter
 *
 *  Name and val must be freed when no longer used
 */

void bg_msg_get_parameter(gavl_msg_t * msg,
                          const char ** name,
                          gavl_value_t * val);

/** \brief Get a parameter
 *  \param msg A message
 *  \param ctx Returns context
 *  \param name Returns the name of the parameter
 *  \param type Type of the parameter
 *  \param val Value for the parameter
 *
 *  Name and val must be freed when no longer used
 */

void bg_msg_get_parameter_ctx(gavl_msg_t * msg,
                              const char ** ctx,
                              const char ** name,
                              gavl_value_t * val);

void bg_msg_get_chain_parameter_ctx(gavl_msg_t * msg,
                                    const char ** ctx,
                                    const char ** name,
                                    int * idx,
                                    const char ** sub_name,
                                    gavl_value_t * val);

void bg_msg_get_multi_parameter_ctx(gavl_msg_t * msg,
                                    const char ** ctx,
                                    const char ** name,
                                    const char ** el_name,
                                    const char ** sub_name,
                                    gavl_value_t * val);


int bg_msg_merge(gavl_msg_t * msg, gavl_msg_t * next);

void bg_msg_add_function_tag(gavl_msg_t * msg);


/*
 *  Write support
 */



/** @} */

struct json_object *  bg_msg_to_json(const gavl_msg_t * msg);

int bg_msg_from_json(gavl_msg_t * msg, json_object * obj);

int
bg_msg_from_json_str(gavl_msg_t * msg, const char * str);


char * bg_msg_to_json_str(const gavl_msg_t * msg);
void bg_msg_to_json_buf(const gavl_msg_t * msg, gavl_buffer_t * buf);

void bg_msg_dump(gavl_msg_t * msg, int indent);


/* msg sink */

/* The callback returns 0 if the qrogram is about to quit
   and no further commandxs need to be processed */

// typedef int (*gavl_handle_msg_func)(void *, gavl_msg_t *);

typedef struct bg_msg_sink_s bg_msg_sink_t;

gavl_msg_t * bg_msg_sink_get(bg_msg_sink_t * sink);

void bg_msg_sink_put(bg_msg_sink_t * sink);
void bg_msg_sink_put_copy(bg_msg_sink_t * sink, const gavl_msg_t * msg);

/* Messages processed in the last call to bg_msg_sink_iteration */
int bg_msg_sink_get_num(bg_msg_sink_t * sink);

/* Wait until at least one message arrives */
void bg_msg_sink_wait(bg_msg_sink_t * sink, int timeout);

bg_msg_sink_t * bg_msg_sink_create(gavl_handle_msg_func cb, void * cb_data, int sync);
void bg_msg_sink_destroy(bg_msg_sink_t *);

// gavl_handle_msg_func
int bg_msg_sink_handle(void * sink, gavl_msg_t * msg);

/* For asynchronous sinks */
int bg_msg_sink_iteration(bg_msg_sink_t *);

/* Terminate parameter setting */
void bg_msg_set_parameter_ctx_term(bg_msg_sink_t * sink);

void bg_msg_sink_set_id(bg_msg_sink_t * sink, const char * id);
int bg_msg_sink_has_id(bg_msg_sink_t * sink, const char * id);

gavl_msg_t * bg_msg_sink_get_read(bg_msg_sink_t * sink);
void bg_msg_sink_done_read(bg_msg_sink_t * sink, gavl_msg_t * m);



// const char * bg_msg_sink_get_id(bg_msg_sink_t * sink);



/* msg hub */

typedef struct bg_msg_hub_s bg_msg_hub_t;

bg_msg_hub_t * bg_msg_hub_create(int sync);

void bg_msg_hub_connect_sink(bg_msg_hub_t *, bg_msg_sink_t *);
void bg_msg_hub_disconnect_sink(bg_msg_hub_t *, bg_msg_sink_t *);

const gavl_dictionary_t * bg_msg_hub_get_state(bg_msg_hub_t * h);

bg_msg_sink_t * bg_msg_hub_get_sink(bg_msg_hub_t *);

void bg_msg_hub_send_cb(bg_msg_hub_t * h,
                        void (*set_message)(gavl_msg_t * message,
                                            const void * data),
                        const void * data);

void bg_msg_hub_set_connect_cb(bg_msg_hub_t * h,
                               void (*cb)(bg_msg_sink_t * s,
                                          void * data),
                               void * data);



void
bg_msg_hub_destroy(bg_msg_hub_t *);

/* Controllable and control */

typedef struct
  {
  bg_msg_sink_t * cmd_sink; // Owned
  bg_msg_hub_t * evt_hub;   // Owned
  bg_msg_sink_t * evt_sink; // Ptr, taken from evt_hub

  void * priv;
  void (*cleanup)(void *);

  /* Some connections need a regular ping while
   */
  void * ping_data;
  void (*ping_func)(void *);
  
  } bg_controllable_t;

typedef struct
  {
  bg_msg_sink_t * evt_sink;  // Owned, can be NULL
  bg_controllable_t * ctrl;  // Ptr
  bg_msg_sink_t * cmd_sink;  // Owned, created internally

  void * priv;
  void (*cleanup)(void *);

  char id[37];
  } bg_control_t;

#define BG_FUNCTION_TAG GAVL_MSG_FUNCTION_TAG

gavl_dictionary_t * 
bg_function_push(gavl_array_t * arr, gavl_msg_t * msg);

gavl_dictionary_t *
bg_function_get(gavl_array_t * arr, const gavl_msg_t * msg, int * idx);


void
bg_controllable_init(bg_controllable_t * ctrl,
                     bg_msg_sink_t * cmd_sink, // Owned
                     bg_msg_hub_t * evt_hub);   // Owned

int
bg_controllable_call_function(bg_controllable_t * c, gavl_msg_t * func,
                              gavl_handle_msg_func cb, void * data, int timeout);

void
bg_controllable_cleanup(bg_controllable_t * ctrl);   // Owned

void
bg_controllable_connect(bg_controllable_t * ctrl,
                        bg_control_t * c);

void
bg_controllable_disconnect(bg_controllable_t * ctrl,
                           bg_control_t * c);

void
bg_controllable_cleanup(bg_controllable_t * ctrl);

void bg_control_init(bg_control_t * c,
                     bg_msg_sink_t * evt_sink);


void bg_control_cleanup(bg_control_t * c);

/* Gmerlin specific messages */

#define BG_MSG_NS_PARAMETER         100
// #define BG_MSG_NS_BACKEND           101
#define BG_MSG_NS_PLAYER            102 // playermsg.h
#define BG_MSG_NS_TRANSCODER        103
#define BG_MSG_NS_VISUALIZER        104
#define BG_MSG_NS_GUI_PLAYER        105
#define BG_MSG_NS_UPNP              106
#define BG_MSG_NS_DIALOG            107

#define BG_MSG_NS_PLAYER_PRIVATE    109
#define BG_MSG_NS_MENU              111
#define BG_MSG_NS_STATE             GAVL_MSG_NS_STATE
#define BG_MSG_NS_RECORDER          113
#define BG_MSG_NS_DB                114 // mdb.h
#define BG_MSG_NS_SSDP              115 // ssdh.h
#define BG_MSG_NS_MDB_PRIVATE       116 // mdb_private.h

#define BG_MSG_NS_DB_CACHE          117 // mdb.h


#define BG_MSG_NS_PRIVATE           200 // Used only within a single .c file

/*
 *  BG_MSG_NS_PARAMETER
 */

#define BG_MSG_PARAMETER_SUBSECTION "subsection"

/*
    arg0: name  (string)
    arg1: val
 */

#define BG_CMD_SET_PARAMETER           1


/*
    header context: ctx   (string)
    arg0: name  (string)
    arg1 val
 */

#define BG_MSG_PARAMETER_CHANGED_CTX   3

/*
    arg0: ctx   (string)
    arg1: name  (string)    // Name of the chain parameter 
    arg2: idx   (int)       // Index of the chain element 
    arg3: sub_name (string) // Name of the element parameter
    arg4: val
 */

#define BG_CMD_SET_CHAIN_PARAMETER_CTX  4

/*
    arg0: ctx      (string)
    arg1: name     (string) // Name of the multi parameter 
    arg2: el_name  (string) // Name of the element
    arg3: sub_name (string) // Name of the element parameter
    arg4: val
 */

#define BG_CMD_SET_MULTI_PARAMETER_CTX  5

/*
 *  Button pressed
 */

#define BG_CMD_PARAMETER_BUTTON         6

/* State */

// BG_MSG_NS_STATE

/* 
 *  arg0: last (int)
 *  arg1: context (string)
 *  arg2: var (string)
 *  arg3: val (value)
 */

#define BG_MSG_STATE_CHANGED        1

/* 
 *  arg0: last (int)
 *  arg1: context (string)
 *  arg2: var (string)
 *  arg3: val (value)
 *
 *  context can be a path into nested subdirectories like /ctx/dict/subdict
 */

#define BG_CMD_SET_STATE            100

/* 
 *  arg0: last (int)
 *  arg1: context (string)
 *  arg2: var (string)
 *  arg3: difference (value)
 *
 *  context can be a path into nested subdirectories like /ctx/dict/subdict
 */

#define BG_CMD_SET_STATE_REL        101


// BG_MSG_NS_VISUALIZER

/* 
 *  arg0: plugin (int)
 */

#define BG_CMD_VISUALIZER_SET_PLUGIN  1

/* Prepare the visualizer for a longer interruption of audio samples */

#define BG_CMD_VISUALIZER_PAUSE       2

// BG_MSG_NS_DIALOG
// GAVL_MSG_CONTEXT_ID must always be the name of the dialog


/* Commands */


#define BG_MSG_DIALOG_ADD_LOCATIONS      1
#define BG_MSG_DIALOG_CLOSED             2
#define BG_MSG_DIALOG_FILE_LOAD          3  // Fileselector callback (arg0: string)
#define BG_MSG_DIALOG_FILE_SAVE          4  // Fileselector callback (arg0: string)
#define BG_MSG_DIALOG_DIRECTORY          5  // Fileselector callback (arg0: string)
#define BG_MSG_DIALOG_URI                6  // uriselector callback (arg0: string)



/**@} */


#endif /* BG_MSGQUEUE_H_INCLUDED */


