#ifndef BG_FRONTEND_H_INCLUDED
#define BG_FRONTEND_H_INCLUDED

#include <gmerlin/parameter.h>
#include <gmerlin/httpserver.h>

typedef struct bg_frontend_s bg_frontend_t;

bg_frontend_t *
bg_frontend_create_mdb_upnp(bg_http_server_t * srv,
                            bg_controllable_t * ctrl);

bg_frontend_t * bg_frontend_create_mdb_gmerlin(bg_http_server_t * srv, bg_controllable_t * ctrl);

bg_frontend_t *
bg_frontend_create_player_upnp(bg_http_server_t * srv,
                               bg_controllable_t * ctrl);

bg_frontend_t *
bg_frontend_create_player_ncurses(bg_controllable_t * ctrl);

bg_frontend_t *
bg_frontend_create_player_console(bg_controllable_t * ctrl, int display_time);

bg_frontend_t * bg_frontend_create_player_gmerlin(bg_http_server_t * srv, bg_controllable_t * ctrl);

// bg_frontend_t * bg_frontend_create_player_mpris(/* */ bg_controllable_t * ctrl);
// bg_frontend_t * bg_frontend_create_player_mpd(/* */ bg_controllable_t * ctrl);

void bg_frontend_destroy(bg_frontend_t *);
int bg_frontend_ping(bg_frontend_t *, gavl_time_t current_time);

#endif // BG_FRONTEND_H_INCLUDED
