
#include <gmerlin/frontend.h>
#include <frontend_priv.h>

bg_frontend_t * bg_frontend_create(bg_controllable_t * controllable)
  {
  bg_frontend_t * ret = calloc(1, sizeof(*ret));
  ret->controllable = controllable;
  return ret;
  }

void bg_frontend_init(bg_frontend_t * f)
  {
  
  }

void bg_frontend_destroy(bg_frontend_t * f)
  {
  if(f->cleanup_func && f->priv)
    f->cleanup_func(f->priv);
  bg_control_cleanup(&f->ctrl);
  free(f);
  }

int bg_frontend_ping(bg_frontend_t * f, gavl_time_t current_time)
  {
  int ret = 0;

  if(f->ping_func)
    ret += f->ping_func(f, current_time);
  
  return ret;
  }

int bg_frontend_finished(bg_frontend_t * f)
  {
  return !!(f->flags & BG_FRONTEND_FLAG_FINISHED);
  }
