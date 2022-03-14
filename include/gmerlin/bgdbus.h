/*****************************************************************
 * Gmerlin - a general purpose multimedia framework and applications
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

#ifndef BGDBUS_H_INCLUDED
#define BGDBUS_H_INCLUDED

#include <dbus/dbus.h>

#include <gmerlin/frontend.h>
#include <gmerlin/msgqueue.h>
// dbus-monitor "type='signal',member='NameOwnerChanged',arg0namespace='org.mpris.MediaPlayer2'"

/* These *must* match the specifiers for dbus_bus_add_match() */
#define BG_DBUS_META_SENDER        "sender"
#define BG_DBUS_META_INTERFACE     "interface"
#define BG_DBUS_META_TYPE          "type"
#define BG_DBUS_META_MEMBER        "member"
#define BG_DBUS_META_PATH          "path"
#define BG_DBUS_META_SERIAL        "serial"
#define BG_DBUS_META_REPLY_SERIAL  "reply_serial"
#define BG_DBUS_META_DESTINATION   "destination"

#define BG_DBUS_MPRIS_URI_SCHEME "mpris2"
#define BG_MPD_URI_SCHEME        "mpd"

typedef void (*bg_dbus_msg_callback)(void * priv, DBusMessage * msg);

typedef struct bg_dbus_connection_s bg_dbus_connection_t;

bg_dbus_connection_t * bg_dbus_connection_get(DBusBusType type);

const char * bg_dbus_connection_get_addr(DBusBusType type);

char * bg_dbus_connection_request_name(bg_dbus_connection_t * conn, const char * name);

void bg_dbus_connection_lock(bg_dbus_connection_t *);
void bg_dbus_connection_unlock(bg_dbus_connection_t *);

// int bg_dbus_connection_update(bg_dbus_connection_t *);

/* Either call bg_dbus_update() regularly.... */

/* ... or start and stop the thread so you don't have to care */

void bg_dbus_connection_unref(bg_dbus_connection_t *);

void
bg_dbus_connection_add_listener(bg_dbus_connection_t * conn,
                                const char * rule,
                                bg_msg_sink_t * sink, int ns, int id);

gavl_msg_t * bg_dbus_connection_call_method(bg_dbus_connection_t * conn,
                                            DBusMessage * req);

void
bg_dbus_connection_del_listeners(bg_dbus_connection_t * conn, bg_msg_sink_t * sink);

int bg_dbus_get_int_property(bg_dbus_connection_t * conn,
                             const char * addr,
                             const char * path,
                             const char * interface,
                             const char * name);

char * bg_dbus_get_string_property(bg_dbus_connection_t * conn,
                                   const char * addr,
                                   const char * path,
                                   const char * interface,
                                   const char * name);

int64_t bg_dbus_get_long_property(bg_dbus_connection_t * conn,
                                  const char * addr,
                                  const char * path,
                                  const char * interface,
                                  const char * name);


gavl_array_t * bg_dbus_get_array_property(bg_dbus_connection_t * conn,
                                          const char * addr,
                                          const char * path,
                                          const char * interface,
                                          const char * name);


void bg_dbus_set_double_property(bg_dbus_connection_t * conn,
                                 const char * addr,
                                 const char * path,
                                 const char * interface,
                                 const char * name, double val);


gavl_dictionary_t *
bg_dbus_get_properties(bg_dbus_connection_t * conn,
                       const char * addr,
                       const char * path,
                       const char * interface);

char * bg_dbus_get_name_owner(bg_dbus_connection_t * conn,
                              const char * name);


/* Mpris2 frontend */
bg_frontend_t *
bg_frontend_create_player_mpris2(bg_controllable_t * ctrl,
                                 const char * bus_name,
                                 const char * desktop_file);


typedef struct
  {
  const char * name;
  const char * dbus_type;
  int writable;
  gavl_value_t val_default;

  void (*val_to_variant)(const gavl_value_t * val, DBusMessageIter *iter);
  
  } bg_dbus_property_t;


void bg_dbus_msg_dump(DBusMessage * msg);


void bg_value_to_dbus_variant(const char * sig,
                              const gavl_value_t * val,
                              DBusMessageIter *iter,
                              void (*val_to_variant)(const gavl_value_t*, DBusMessageIter*));

void bg_dbus_properties_init(const bg_dbus_property_t * desc, gavl_dictionary_t * prop);

void bg_dbus_property_get(const bg_dbus_property_t * desc, gavl_dictionary_t * prop,
                          const char * name,
                          const gavl_msg_t * req, bg_dbus_connection_t * conn);

void bg_dbus_properties_get(const bg_dbus_property_t * desc, const gavl_dictionary_t * prop,
                            const gavl_msg_t * req, bg_dbus_connection_t * conn);

void bg_dbus_set_property_local(bg_dbus_connection_t * conn,
                                const char * path,
                                const char * interface,
                                const char * name, const gavl_value_t * val,
                                const bg_dbus_property_t * properties,
                                gavl_dictionary_t * dict,
                                int send_msg);

DBusMessage * bg_dbus_msg_new_reply(const gavl_msg_t * msg);


void bg_dbus_send_error(bg_dbus_connection_t * conn,
                        const gavl_msg_t * msg,
                        const char * error_name, const char * error_msg);

void bg_dbus_send_msg(bg_dbus_connection_t * conn, DBusMessage * msg);

void bg_dbus_register_object(bg_dbus_connection_t * conn, const char * path,
                             bg_msg_sink_t * sink, const char * introspection_xml);

void bg_dbus_unregister_object(bg_dbus_connection_t * conn, const char * path, bg_msg_sink_t * sink);
  




#endif // BGDBUS_H_INCLUDED

