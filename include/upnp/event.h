#include <gavl/gavl.h>
#include <gavl/value.h>

#include <gmerlin/httpserver.h>

/* Event context (server /device side)*/

void bg_upnp_event_context_init_server(gavl_dictionary_t * dict,
                                      const char * dir,
                                      bg_http_server_t * srv);

void bg_upnp_event_context_server_set_value(gavl_dictionary_t * dict, const char * name,
                                            const char * val,
                                            gavl_time_t update_interval);

const char * bg_upnp_event_context_server_get_value(const gavl_dictionary_t * dict, const char * name);

/* Send moderate events */

int bg_upnp_event_context_server_update(gavl_dictionary_t * dict,
                                          gavl_time_t current_time);

/* Event context (client / control side) */

int bg_upnp_event_context_init_client(gavl_dictionary_t * dict,
                                      bg_http_server_t * srv,
                                      bg_msg_sink_t * sink);

