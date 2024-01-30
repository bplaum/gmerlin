#ifndef BG_FRONTEND_H_INCLUDED
#define BG_FRONTEND_H_INCLUDED

#include <gmerlin/parameter.h>
#include <gmerlin/httpserver.h>

typedef struct bg_frontend_s bg_frontend_t;


void bg_frontend_destroy(bg_frontend_t *);
int bg_frontend_ping(bg_frontend_t *);

int bg_frontend_set_option(gavl_array_t * frontends, const char * opt);

bg_frontend_t ** bg_frontends_create(bg_controllable_t * ctrl,
                                     int type_mask, gavl_array_t * frontends, int * num);

int bg_frontends_ping(bg_frontend_t **, int num_frontends);
void bg_frontends_destroy(bg_frontend_t **, int num_frontends);

/* gmerlin frontend */
int bg_frontend_gmerlin_ping(void * data);
void * bg_frontend_gmerlin_create();
void bg_frontend_gmerlin_destroy(void * priv);
int bg_frontend_gmerlin_open_mdb(void * data, bg_controllable_t * ctrl);
int bg_frontend_gmerlin_open_renderer(void * data, bg_controllable_t * ctrl);

#endif // BG_FRONTEND_H_INCLUDED
